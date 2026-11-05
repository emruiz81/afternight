// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The authoritative scalar meaning of a compiled expression.
///
/// This is the oracle, and it is part of the deliverable rather than scaffolding, for the reason the
/// pointwise evaluator is: everything else that will ever compute an expression - the image walk, a
/// chain lowered into the fused pointwise executor, a future device backend - is defined as producing
/// what this produces for one pixel. It takes one pixel and does the obvious thing in the obvious
/// order.
///
/// Three conventions are stated here rather than inferred.
///
/// **A sample's value is its own code value.** An eight- or sixteen-bit source contributes the
/// integer it stores. This engine does not normalize behind a caller's back, and an expression that
/// wants a fraction of full scale says so.
///
/// **A read outside an image is zero, and so is a channel an image does not have.** An expression may
/// compute any coordinate, so both are answered the same way, and the answer is the one that cannot be
/// a memory fault.
///
/// **The random stream is a function of where it is drawn, not of when.** A draw is derived from the
/// execution's seed, the pixel and how many draws that pixel has already made - never from a generator
/// a thread carries - so the same expression over the same images gives the same image on one thread
/// and on sixty-four.

#pragma once

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace an
{

/// The colour conversions one compiled expression needs, planned once.
///
/// A colour component is a conversion between two stated encodings, and planning one derives and
/// inverts matrices. Doing that per pixel would cost more than the conversion, and putting the plans
/// in the program would put a colour type in a header that has no other reason to carry one, so they
/// live in their own handle: built once from a program, borrowed by every evaluation of it.
///
/// Copyable and movable; copies share the same immutable plans. Safe to read from any number of
/// threads at once.
class PixelMathColorPlans
{
public:
    /// No plans, which is what an expression with no colour component needs.
    PixelMathColorPlans() = default;

    /// Copies a handle. Both then refer to the same immutable plans.
    PixelMathColorPlans(const PixelMathColorPlans&) = default;

    /// Takes over a handle.
    PixelMathColorPlans(PixelMathColorPlans&&) noexcept = default;

    /// Copies a handle over this one.
    ///
    /// @return this handle.
    PixelMathColorPlans& operator=(const PixelMathColorPlans&) = default;

    /// Takes over a handle, leaving the other empty.
    ///
    /// @return this handle.
    PixelMathColorPlans& operator=(PixelMathColorPlans&&) noexcept = default;

    /// Releases this handle's share of the plans.
    ~PixelMathColorPlans() = default;

    /// How many conversions were planned.
    ///
    /// One per colour read the program carries, in the same order.
    ///
    /// @return the count, zero for a handle that holds none.
    [[nodiscard]] AFTERNIGHTENGINE_API std::size_t count() const noexcept;

private:
    friend class PixelMathColorPlanner;

    std::shared_ptr<const void> m_plans;
};

/// Plans every colour conversion a compiled expression asks for.
///
/// The source encoding is derived from what the expression's images say their samples mean: a
/// normalized-linear image is linear sRGB and an encoded-nonlinear one carries the sRGB transfer
/// function, both under D65 primaries. That assumption is stated here, in one place, rather than in
/// each of the eighteen accessors. An expression that needs another statement converts the image with
/// a colour transform first.
///
/// Thread-safe and reentrant.
///
/// @param program the compiled expression.
/// @return the plans, empty when the program reads no colour component; the colour transform's
///         failure when an encoding it derived describes no conversion; or `ErrorCode::Internal`
///         for a colour read this build cannot plan, which a program from `compilePixelMath` never
///         holds.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelMathColorPlans> planPixelMathColors(const PixelMathProgram& program);

/// One read-only image bound to a compiled expression.
///
/// The samples and layout are borrowed for the call. The layout's extent and element width must
/// describe the declaration the program carries for this image, and `sampleType` must match that
/// element width. `executePixelMath` checks both; `evaluatePixelMathPixel` checks neither, and
/// bounds its reads by the layout it is given rather than by the declaration.
struct PixelMathImageView
{
    /// The resident representation the layout's element width describes.
    SampleType sampleType = SampleType::Float32;

    /// First byte of the image storage, borrowed for the call.
    const void* samples = nullptr;

    /// The validated geometry and strides of `samples`, borrowed for the call.
    const ImageLayout* layout = nullptr;
};

/// Where one evaluation is happening.
struct PixelMathEvaluationSite
{
    /// The column being written.
    std::uint32_t x = 0u;

    /// The row being written.
    std::uint32_t y = 0u;

    /// The channel being written.
    std::uint32_t channel = 0u;

    /// The execution's random seed. Two evaluations with the same seed at the same pixel draw the
    /// same values.
    std::uint64_t randomSeed = 0u;
};

/// The working memory one evaluation needs, owned by the caller.
///
/// Sized from the program rather than from a limit, and reused across pixels by the image walk, so
/// evaluating a frame allocates nothing per pixel.
struct PixelMathEvaluationScratch
{
    /// One value per variable the program declares. Reset by the evaluator before it starts, so a
    /// caller may pass the same span for every pixel.
    std::span<double> variables{};

    /// At least `PixelMathProgram::stackDepth()` values. Its contents on entry are not read.
    std::span<double> stack{};
};

/// Evaluates one pixel of one channel.
///
/// The images are bound in the program's own order, so `images[0]` is the target. The statistics are
/// the program's requests for the channel being written, one value per request.
///
/// Thread-safe and reentrant with respect to the program and the bound images, which are only read.
/// Allocation-free: every value it needs is in the scratch the caller supplied.
///
/// @param program the compiled expression.
/// @param images exactly `program.images().size()` borrowed images.
/// @param statistics exactly `program.statistics().size()` values, for `site.channel`.
/// @param colorPlans the conversions `planPixelMathColors` produced for this program, or an empty
///        handle when it reads no colour component.
/// @param site where the evaluation is happening and what its random stream is.
/// @param scratch working memory at least as large as the program asks for.
/// @return the value the expression computes; `ErrorCode::InvalidArgument` when the program is
///         empty, `images` or `statistics` is not exactly the length the program asks for, the
///         scratch is smaller than it asks for, an image has a null sample or layout pointer, or
///         `colorPlans` does not hold one conversion per colour read; the colour transform's failure
///         for a pixel it cannot convert; or `ErrorCode::Internal` for an instruction this build
///         cannot execute or a program whose stack use is inconsistent.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
evaluatePixelMathPixel(const PixelMathProgram& program, std::span<const PixelMathImageView> images,
                       std::span<const double> statistics, const PixelMathColorPlans& colorPlans,
                       const PixelMathEvaluationSite& site, const PixelMathEvaluationScratch& scratch);

} // namespace an
