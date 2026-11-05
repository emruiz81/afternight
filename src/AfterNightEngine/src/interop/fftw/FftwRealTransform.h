// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one place this engine actually calls FFTW.
//
// The public adapter in `Interop/FftwAdapter.h` describes an array and checks whether a plan can
// exist over it; this private header is where a plan does exist. It stays inside the FFTW adapter
// domain because the include rules put the library's own header in exactly one directory, and it
// exposes no FFTW type: a plan crosses this boundary as an opaque handle, so an algorithm that
// executes a transform never acquires the library's headers by including this one.
//
// Two facts about the library shape the interface:
//
// **Planning mutates global state; executing does not.** FFTW's planner reads and writes
// process-wide wisdom, so two threads creating or destroying plans concurrently race even when the
// plans are unrelated. Execution through the new-array interface touches only the plan and the
// arrays. This wrapper therefore serializes creation and destruction behind one mutex - guarding
// the library's own process-wide planner, which is state this engine did not create and cannot
// scope - and leaves execution free, which is what lets one plan serve several buffers of one
// operation.
//
// **A plan remembers the geometry and alignment it was created over.** The new-array execute is
// only defined for an array of the same shape, stride and alignment class as the original, so
// `execute` and `executeSplit` re-check the base address against the alignment the plan was built
// at rather than trusting the caller to remember.
//
// Phase correlation, the dual-domain shrinkage denoiser and the deconvolution convolution engine
// are the callers.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Interop/FftwAdapter.h>

#include <cstdint>

/// The FFTW adapter's plan wrapper: the one place the engine calls the library, behind handles that
/// name no FFTW type.
namespace an::fftw_detail
{

/// Which way a plan transforms.
enum class FftwTransformDirection : std::uint8_t
{
    /// Real samples in, Hermitian half-spectrum out.
    Forward = 0u,

    /// Hermitian half-spectrum in, real samples out, unnormalized - the round trip gains a factor
    /// of the sample count, exactly as the library defines it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Inverse = 1u
};

/// One single-precision two-dimensional real transform plan, either in place or split.
///
/// An in-place plan (`create`) is created over a padded real array - `2 * (width / 2 + 1)` reals
/// per row, which is `fftwRealRowReals(width, FftwPlacement::InPlace)` and is validated here rather
/// than assumed - and a split plan (`createSplit`) over a tightly packed real array and a separate
/// half-spectrum. Either may then be executed, through its own entry point, over any buffers of
/// exactly that geometry and at least that alignment class. The class is the largest power of two
/// dividing the planning array's address, capped at `kFftwDefaultAlignmentBytes`.
///
/// Move-only, because a plan is a real allocation inside the library, and destroying it twice is a
/// double free this engine would be blamed for.
class FftwRealTransformPlan
{
public:
    /// A plan holding nothing.
    FftwRealTransformPlan() noexcept = default;

    /// Takes over another plan, leaving it holding nothing.
    ///
    /// @param other the plan to take over.
    FftwRealTransformPlan(FftwRealTransformPlan&& other) noexcept;

    /// Destroys anything held, under the planner lock, and takes over another plan.
    ///
    /// @param other the plan to take over.
    /// @return this plan.
    FftwRealTransformPlan& operator=(FftwRealTransformPlan&& other) noexcept;

    /// Copying is deleted: two owners of one library plan would destroy it twice.
    FftwRealTransformPlan(const FftwRealTransformPlan&) = delete;

    /// Copying is deleted. See the copy constructor.
    FftwRealTransformPlan& operator=(const FftwRealTransformPlan&) = delete;

    /// Destroys the plan, under the planner lock.
    ~FftwRealTransformPlan();

    /// Creates a plan over a separate real array and half-spectrum.
    ///
    /// The out-of-place counterpart of `create`, and the faster of the two: an in-place real
    /// transform has to work inside a padded row layout, and on the frames this engine transforms
    /// that costs about twice what the separated form does. The cost is one more buffer, which an
    /// operation that already holds a spectrum per kernel was going to pay anyway.
    ///
    /// The real array must be single-precision, real, out-of-place - so exactly `width` reals per
    /// row - and the complex array must describe the matching half-spectrum, `width / 2 + 1` complex
    /// elements per row and the same number of rows. Planning uses the library's estimate mode.
    ///
    /// An inverse plan of this shape destroys its input, which is what the library offers for a
    /// multi-dimensional half-spectrum; the caller must treat the spectrum as consumed.
    ///
    /// @param direction which way the plan transforms.
    /// @param real the real array the plan is measured over.
    /// @param spectrum the half-spectrum the plan is measured over.
    /// @param operation the name carried by any failure.
    /// @return the plan; `ErrorCode::InvalidArgument` for descriptions that are not a matching
    ///         single-precision out-of-place real plane and interleaved half-spectrum; or
    ///         `ErrorCode::ExternalFailure` when the library declines, carrying its name.
    [[nodiscard]] static Result<FftwRealTransformPlan> createSplit(FftwTransformDirection direction,
                                                                   const FftwArrayDescription& real,
                                                                   const FftwArrayDescription& spectrum,
                                                                   const char* operation);

    /// Executes a split plan over one real buffer and one half-spectrum.
    ///
    /// Both buffers must have exactly the planned geometry and at least the planned alignment class.
    ///
    /// Thread-safe: two threads may execute one plan over disjoint buffer pairs concurrently.
    ///
    /// @param real the first real of the real buffer.
    /// @param spectrum the first real of the half-spectrum buffer.
    /// @param operation the name carried by any failure.
    /// @return success; `ErrorCode::InvalidState` when this plan holds nothing or is not a split
    ///         plan; or `ErrorCode::InvalidArgument` for a null or misaligned buffer.
    [[nodiscard]] Status executeSplit(void* real, void* spectrum, const char* operation) const;

    /// Creates a plan over one described in-place real array.
    ///
    /// The description must be single-precision, real, with positive dimensions and exactly the
    /// in-place padded row stride; its base address is where the plan measures alignment, and every
    /// later execution must match it. Planning uses the library's estimate mode, so the array's
    /// contents survive planning and the cost is a table lookup rather than a measurement run.
    ///
    /// @param direction which way the plan transforms.
    /// @param array the in-place array the plan is measured over.
    /// @param operation the name carried by any failure.
    /// @return the plan; `ErrorCode::InvalidArgument` for a description that is not a
    ///         single-precision in-place real plane; or `ErrorCode::ExternalFailure` when the
    ///         library declines, carrying its name.
    [[nodiscard]] static Result<FftwRealTransformPlan> create(FftwTransformDirection direction,
                                                              const FftwArrayDescription& array, const char* operation);

    /// Whether this plan holds anything.
    ///
    /// @return true when a plan exists.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_plan != nullptr;
    }

    /// Executes the plan in place over one buffer of the planned geometry.
    ///
    /// The buffer must have exactly the planned dimensions and row stride and at least the planned
    /// alignment class, which is what the new-array interface requires; the base address is checked
    /// here so a mismatch is a refusal rather than a silently slower or wrong transform.
    ///
    /// Thread-safe: two threads may execute one plan over disjoint buffers concurrently.
    ///
    /// @param base the first real of the buffer to transform in place.
    /// @param operation the name carried by any failure.
    /// @return success; `ErrorCode::InvalidState` when this plan holds nothing or is a split plan;
    ///         or `ErrorCode::InvalidArgument` for a null or misaligned buffer.
    [[nodiscard]] Status execute(void* base, const char* operation) const;

private:
    /// Builds a plan wrapper over the library's own handle.
    ///
    /// @param plan the library's plan.
    /// @param direction which way it transforms.
    /// @param alignmentBytes the alignment class the plan was measured at.
    /// @param split whether the plan separates its real array from its half-spectrum.
    FftwRealTransformPlan(void* plan, FftwTransformDirection direction, std::uint64_t alignmentBytes,
                          bool split = false) noexcept
        : m_plan(plan), m_direction(direction), m_alignmentBytes(alignmentBytes), m_split(split)
    {
    }

    /// The library's plan, or null when nothing is held. Opaque here so no FFTW type crosses this
    /// header.
    void* m_plan = nullptr;

    /// Which way the plan transforms.
    FftwTransformDirection m_direction = FftwTransformDirection::Forward;

    /// The alignment class the plan was measured at, in bytes.
    std::uint64_t m_alignmentBytes = 1u;

    /// Whether the plan separates its real array from its half-spectrum.
    bool m_split = false;
};

} // namespace an::fftw_detail
