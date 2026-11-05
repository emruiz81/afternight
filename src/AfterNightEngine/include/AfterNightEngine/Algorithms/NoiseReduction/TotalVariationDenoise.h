// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Total-variation denoising, as an operation of this engine rather than a
/// call into somebody else's.
///
/// Both algorithms are published ones and both are computed here, so a denoise needs no interpreter,
/// no particular version of an imaging library, and no copy of the frame in and out.
///
/// **The two methods solve the same problem and are not interchangeable in their parameters.** Both
/// minimize the Rudin-Osher-Fatemi functional - the total variation of the answer plus its distance
/// from the input - and they differ in how they get there and in which side of the trade-off their
/// weight names. Chambolle's dual iteration takes `weight` as the denoising strength, so a larger
/// number smooths more; the split-Bregman method takes it as the fidelity weight of the same
/// functional, so a larger number smooths *less*. That is the convention each algorithm is published
/// and tabulated with, and renaming one of them here would silently change what a caller's saved
/// setting means.
///
/// **An iterative operation reports progress and stops when asked.** Both methods report into a fixed
/// total that their last report reaches, check cancellation once per iteration, and report how many
/// iterations actually ran and whether the stopping rule was met or the iteration limit was.
///
/// **A sample the validity rule rejects leaves its destination unchanged.** The solve substitutes
/// zero for every rejected input before iteration, so a rejected non-finite value cannot contaminate
/// valid neighbours. Zero is a fixed boundary convention, not an estimate of missing data: it can
/// influence nearby valid answers. Only selected samples are written, for every storage layout.
///
/// Chambolle's iteration reproduces scikit-image's answer; the split-Bregman method reaches the same
/// minimum by a route whose intermediate states differ from it.

#pragma once

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

class ExecutionContext;

/// Which published algorithm reaches the total-variation minimum.
enum class TotalVariationMethod : std::uint8_t
{
    /// Chambolle's projection iteration on the dual variable.
    ///
    /// `TotalVariationRequest::weight` is the denoising strength: larger smooths more.
    Chambolle = 0u,

    /// The split-Bregman method, iterated by Gauss-Seidel sweeps.
    ///
    /// `TotalVariationRequest::weight` is the fidelity weight: larger smooths *less*. It is also the
    /// only method of the two that offers the anisotropic variation.
    SplitBregman = 1u
};

/// Whether a value names a total-variation method this build supports.
///
/// @param method the value to inspect.
/// @return true for both supported methods.
[[nodiscard]] constexpr bool isTotalVariationMethod(TotalVariationMethod method) noexcept
{
    return method == TotalVariationMethod::Chambolle || method == TotalVariationMethod::SplitBregman;
}

/// The name of a total-variation method, for a diagnostic or a report.
///
/// @param method which method.
/// @return a static, never-null name; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* totalVariationMethodName(TotalVariationMethod method) noexcept;

/// The most iterations either method will run.
///
/// Both converge geometrically and neither is worth more than this; a request above it is refused by
/// name rather than left to run for minutes because a digit was mistyped.
inline constexpr std::uint32_t kMaxTotalVariationIterations = 10000u;

/// The smallest weight either method is defined for.
///
/// Both divide by the weight, so a weight at or below this is a division by zero dressed as a
/// parameter.
inline constexpr double kMinimumTotalVariationWeight = 1.0e-8;

/// What a caller asks a total-variation denoiser for.
struct TotalVariationRequest
{
    /// Which of the two published algorithms runs.
    TotalVariationMethod method = TotalVariationMethod::Chambolle;

    /// The weight of the functional, in the sense the chosen method publishes.
    ///
    /// Denoising strength for `Chambolle`, fidelity for `SplitBregman`. Must be finite and above
    /// `kMinimumTotalVariationWeight`.
    double weight = 0.1;

    /// The most sweeps to run. From one through `kMaxTotalVariationIterations`.
    std::uint32_t maximumIterations = 200u;

    /// When to stop early, in the units the chosen method measures its own progress in.
    ///
    /// `Chambolle` compares the change in the functional against this fraction of its first value;
    /// `SplitBregman` compares the root-mean-square change of the answer against it directly. Zero
    /// runs exactly `maximumIterations` sweeps, which is what a caller reproducing a published
    /// figure wants. Must be finite and not negative.
    double tolerance = 2.0e-4;

    /// Whether `SplitBregman` measures the gradient's length or its two components separately.
    ///
    /// True is the isotropic variation, which does not prefer the axes. Ignored by `Chambolle`,
    /// which has no anisotropic form.
    bool isotropic = true;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A validated total-variation request and what running it will cost.
///
/// Trivially copyable and allocation-free.
class TotalVariationPlan
{
public:
    /// A plan describing no denoiser, which no execution accepts.
    TotalVariationPlan() noexcept = default;

    /// Records a request and derives what it implies.
    ///
    /// Public for the reason `BilateralPlan`'s constructor is: the free builder below is where the
    /// rules live, and this only derives what a checked request implies.
    ///
    /// @param request the method, weight, limits and output policy.
    AFTERNIGHTENGINE_API explicit TotalVariationPlan(const TotalVariationRequest& request) noexcept;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const TotalVariationRequest& request() const noexcept;

    /// How many whole Float32 planes the method keeps beside the channel it is working on.
    ///
    /// Six for either method: `Chambolle` keeps the answer, the divergence and two each of the dual
    /// variable and the gradient, and `SplitBregman` keeps the answer, the previous answer whose
    /// distance decides when to stop, and two each of the shrunk gradient and the Bregman variable.
    ///
    /// @return the plane count, which is zero for a plan describing no denoiser.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t workingPlanesPerChannel() const noexcept;

    /// How many times the method walks the whole image, at most.
    ///
    /// @return the iteration limit; an early stop makes the real number smaller, and the result
    ///         reports which happened.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// The request this plan records.
    TotalVariationRequest m_request{};

    /// The working planes the recorded method keeps.
    std::uint32_t m_workingPlanes = 0u;
};

/// What a total-variation denoise did, beyond what every noise-reduction operation reports.
struct TotalVariationResult
{
    /// Selection, writes, working memory, layout path and planned workers, plus samples moved.
    NoiseReductionResult noise{};

    /// The most iterations any channel ran.
    ///
    /// Channels are solved one at a time and a flat channel converges sooner than a noisy one, so
    /// this is the longest of them rather than a number every channel shares.
    std::uint32_t iterationsRun = 0u;

    /// Whether every channel met the stopping rule before the iteration limit.
    ///
    /// False also when `TotalVariationRequest::tolerance` is zero, because a run that was asked for
    /// a fixed number of sweeps did not stop early and should not claim to have converged.
    bool converged = false;

    /// The largest final step any channel's last iteration took, in the method's own units.
    ///
    /// Comparable with `TotalVariationRequest::tolerance` and with nothing else.
    double finalChange = 0.0;
};

/// Validates a request and derives what running it costs.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the method, weight, limits and output policy.
/// @return the plan; `ErrorCode::InvalidArgument` for an unknown method, a non-finite weight or one
///         at or below `kMinimumTotalVariationWeight`, a non-finite or negative tolerance, a zero
///         iteration count, an output contract or conversion field outside its enumeration, or a
///         write-back policy naming `DomainMapping::FullScale`; or `ErrorCode::OutOfRange` when the
///         iteration count is above `kMaxTotalVariationIterations`, carrying the count and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<TotalVariationPlan>
planTotalVariationDenoise(const TotalVariationRequest& request);

/// Removes noise from every channel of an image while keeping its edges.
///
/// The untyped entry point, and the one the exported library carries. Total variation is the sum of
/// the gradient magnitudes over the frame; noise raises it far more than structure does, so the
/// answer that minimizes it under a fidelity constraint loses the noise and keeps the edges. Unlike
/// a neighbourhood filter this reads and rewrites the whole channel many times, which is why it
/// takes an iteration budget and reports what it spent.
///
/// The arrangement, the gather and scatter working planes and the write-back are the neighbourhood
/// family's, as for `filterImageBilateral`; the validity rule is the one stated above. Beside them
/// the method keeps `TotalVariationPlan::workingPlanesPerChannel` Float32 planes, reused for every
/// channel, plus one substituted input plane when the validity rule is not `AllValid`. The
/// arithmetic is Float32 throughout, which is the representation the published algorithms are
/// tabulated in.
///
/// **Progress is reported in a fixed total of `maximumIterations` per channel**, which the last
/// report reaches whether the run converged or exhausted its budget, so a display never stops part
/// way. Cancellation is checked once per iteration, and a stopped run writes nothing further.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, and is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the denoised samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated method, weight and limits.
/// @param selection which samples mean something.
/// @return the filter counts, the samples moved and what the iteration spent; the image, selection
///         and cancellation failures `filterImageBilateral` documents; whatever
///         `planTotalVariationDenoise` reports when the plan's request is checked again, and
///         `ErrorCode::InvalidArgument` for a plan it did not produce; or
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the working
///         planes do not fit the engine context's budget or its allocator refuses them.
[[nodiscard]] AFTERNIGHTENGINE_API Result<TotalVariationResult>
denoiseImageTotalVariation(const ExecutionContext& execution, SampleType sampleType, const void* source,
                           const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                           const TotalVariationPlan& plan, const SpatialSelection& selection = {});

/// Removes noise from a checked typed image view while keeping its edges.
///
/// The typed spelling of `denoiseImageTotalVariation`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated method, weight and limits.
/// @param selection which samples mean something.
/// @return whatever `denoiseImageTotalVariation` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<TotalVariationResult>
denoiseTotalVariation(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                      const ImageView<SampleT>& destination, const TotalVariationPlan& plan,
                      const SpatialSelection& selection = {})
{
    return denoiseImageTotalVariation(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                      destination.data(), destination.layout(), plan, selection);
}

} // namespace an
