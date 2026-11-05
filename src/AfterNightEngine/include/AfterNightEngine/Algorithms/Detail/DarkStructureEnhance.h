// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Making the dark structures of a frame darker, and only those.
///
/// A dust lane, a dark nebula and the gap between two spiral arms are all the same kind of thing: a
/// region that is darker than the smooth illumination around it, at a scale far larger than the
/// noise and far smaller than the frame. This operation finds those regions and applies a
/// darkening transfer function through them, so that the structure they describe becomes visible
/// without the rest of the picture moving at all.
///
/// **The mask is the whole operation.** Everything difficult happens before a single sample is
/// stretched: the frame's largest scales are separated from it twice, the difference between them
/// says where the picture is darker than its own smooth version, and that difference is rescaled,
/// denoised and clamped into a fraction. The stretch that follows is one pointwise curve blended
/// through that fraction. So the mask is a first-class output here rather than an optional
/// diagnostic, and `buildImageDarkStructureMask` is a call in its own right - a caller adjusting the
/// strength of the effect rebuilds no mask, and a caller inspecting one is not obliged to apply it.
///
/// **The scales are separated by the engine's own multiscale transform.**
/// `smoothImageMultiscaleResidual` produces the one layer the mask needs directly, rather than
/// reconstructing it from a decomposition that holds every layer of every channel, so the memory the
/// operation holds does not grow with the number of layers it removes. The answer is the same: the
/// residual of a starlet decomposition is its cascade of smoothings, and asking for it by name rather
/// than by subtraction is what makes the two decompositions affordable.
///
/// **This operation works in units of a stated white level.** A midtones transfer function is
/// defined on the unit interval and a mask is a fraction by construction, so unlike the
/// neighbourhood filters this family cannot simply carry stored numbers across. The white level is
/// therefore a field rather than an assumption: a statement about what a fully exposed sample is.

#pragma once

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

class EngineContext;
class ExecutionContext;

/// The fewest large scales the mask may remove from the frame.
///
/// Seven. Removing fewer leaves structure at scales the mask should be measuring the illumination
/// over, so the mask starts selecting the objects themselves.
inline constexpr std::uint32_t kMinDarkStructureRemovedLayers = 7u;

/// The most large scales the mask may remove.
///
/// Twelve, the point at which the deepest starlet layer's tap spacing reaches further than any frame
/// this engine accepts.
inline constexpr std::uint32_t kMaxDarkStructureRemovedLayers = 12u;

/// How many fine scales are removed before the frame is compared with its own smooth version.
///
/// Six. The first stage takes away everything smaller than this so that the comparison is between
/// two smooth pictures rather than between a picture and its noise.
inline constexpr std::uint32_t kDarkStructureFineLayers = 6u;

/// How many layers the mask's own denoising decomposition holds.
///
/// Three: one layer of noise that is discarded outright, one of fine structure that is shrunk, and
/// the residual, which is the mask's shape.
inline constexpr std::uint32_t kDarkStructureMaskLayers = 3u;

/// How many standard deviations of its own noise a mask coefficient must exceed to survive.
///
/// The noise is the median absolute deviation of the shrunk layer around its own median, scaled to
/// a standard deviation.
inline constexpr double kDarkStructureMaskThresholdSigmas = 3.0;

/// How many times the shrinkage is applied to the mask's fine layer.
///
/// The same threshold is applied repeatedly rather than re-decomposing, which attenuates what
/// survives a little further each time.
inline constexpr std::uint32_t kDarkStructureMaskShrinkPasses = 5u;

/// The most times the darkening may be applied in one call.
inline constexpr std::uint32_t kMaxDarkStructureIterations = 10u;

/// The narrowest range of mask values that still describes a structure.
///
/// A frame whose dark-structure measurement spans no more than this, in the image's own sample
/// units, holds no dark structure the operation can normalise, and its mask is reported as
/// degenerate and left at zero rather than amplified out of rounding noise. A denoised mask whose
/// values span no more than this is treated the same way.
inline constexpr double kMinDarkStructureMaskRange = 1.0e-7;

/// What a caller asks a dark-structure enhancement for.
struct DarkStructureRequest
{
    /// How many large scales are removed to produce the smooth picture the frame is compared with.
    ///
    /// Within `[kMinDarkStructureRemovedLayers, kMaxDarkStructureRemovedLayers]`. More layers mean
    /// a smoother comparison and therefore a mask that selects larger structures.
    std::uint32_t removedLayers = 8u;

    /// Which smoothing kernel the starlet transform applies.
    ///
    /// Every kernel the transform implements is accepted, and one outside the enumeration is refused
    /// by name.
    MultiscaleScalingFunction scaling = MultiscaleScalingFunction::BSpline5;

    /// The transfer function applied where the mask selects.
    ///
    /// A midtones balance above one half darkens and below it brightens; both halves are accepted,
    /// because brightening exactly the same structures is a legitimate thing to ask for. The endpoints
    /// are in units of the white level.
    MidtonesTransferParameters transfer{.midtones = 0.7, .shadows = 0.0, .highlights = 1.0};

    /// How many times the transfer is applied, each time through the same mask.
    ///
    /// Within `[1, kMaxDarkStructureIterations]`.
    std::uint32_t iterations = 1u;

    /// What a fully exposed sample is, in the image's own sample units.
    ///
    /// Must be finite and above zero. The transfer and the blend run in units of this, which is
    /// what makes the transfer function's unit-interval domain mean something over an integer
    /// representation, and the enhanced value is held inside `[0, whiteLevel]`. The mask needs no
    /// white level, because it is rescaled by its own extremes.
    double whiteLevel = 1.0;

    /// Whether the mask is denoised before it is used.
    ///
    /// The measurement the mask is built from is a difference of two smoothings, so it carries the
    /// frame's noise at the scales neither smoothing removed. Turning this off is faster and shows
    /// that noise in the applied result.
    bool denoiseMask = true;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A validated dark-structure request, with the two transforms it will run resolved.
///
/// Trivially copyable and allocation-free.
class DarkStructurePlan
{
public:
    /// A plan describing no enhancement, which no execution accepts.
    DarkStructurePlan() noexcept = default;

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] AFTERNIGHTENGINE_API const DarkStructureRequest& request() const noexcept;

    /// The transform that removes the frame's fine scales.
    ///
    /// @return the plan, whose layer count is `kDarkStructureFineLayers + 1`.
    [[nodiscard]] AFTERNIGHTENGINE_API const MultiscaleTransformPlan& finePlan() const noexcept;

    /// The transform that produces the smooth picture the frame is compared with.
    ///
    /// @return the plan, whose layer count is the request's removed-layer count plus one.
    [[nodiscard]] AFTERNIGHTENGINE_API const MultiscaleTransformPlan& coarsePlan() const noexcept;

    /// The transform the mask's own denoising runs.
    ///
    /// @return the plan, whose layer count is `kDarkStructureMaskLayers`.
    [[nodiscard]] AFTERNIGHTENGINE_API const MultiscaleTransformPlan& maskPlan() const noexcept;

    /// How many times the operation walks the whole image.
    ///
    /// Two passes per smoothing of the two cascades, the difference and the rescales, the mask's
    /// decomposition and reconstruction when the request denoises it, and two passes - the transfer
    /// and the blend - per darkening iteration.
    ///
    /// @return the pass count, or zero for a plan no request validated.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

    /// How many bytes of context memory the mask's decomposition holds.
    ///
    /// The largest single allocation the operation makes, and the number a caller checks against a
    /// budget: everything else it holds is a working plane or two of one channel.
    ///
    /// @param width how many pixels one row holds.
    /// @param height how many rows.
    /// @return the byte count, which is zero when the request does not denoise the mask or no
    ///         request validated this plan.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t maskLayerBytes(std::uint32_t width,
                                                                    std::uint32_t height) const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<DarkStructurePlan> planDarkStructure(const DarkStructureRequest& request);

    /// Records pieces validated by the friend builder.
    ///
    /// @param request the layer count, the kernel, the transfer and the white level.
    /// @param fine the transform that removes the fine scales.
    /// @param coarse the transform that produces the smooth comparison.
    /// @param mask the transform the mask's denoising runs.
    DarkStructurePlan(const DarkStructureRequest& request, const MultiscaleTransformPlan& fine,
                      const MultiscaleTransformPlan& coarse, const MultiscaleTransformPlan& mask) noexcept;

    /// The request this plan records.
    DarkStructureRequest m_request{};

    /// The transform that removes the frame's fine scales.
    MultiscaleTransformPlan m_fine{};

    /// The transform that produces the smooth comparison.
    MultiscaleTransformPlan m_coarse{};

    /// The transform the mask's denoising runs.
    MultiscaleTransformPlan m_mask{};

    /// Whether this plan was built by a validated request.
    bool m_valid = false;
};

/// Validates a dark-structure request and derives the transforms that would run it.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the layer count, the kernel, the transfer, the white level and the output policy.
/// @return the plan; `ErrorCode::InvalidArgument` for a kernel, output contract or conversion field
///         outside its enumeration, a non-finite or non-positive white level, a transfer whose
///         midtones or endpoints are not finite values in the unit interval or whose endpoints are
///         inverted, or a write-back policy naming `DomainMapping::FullScale`; or
///         `ErrorCode::OutOfRange` for a removed-layer count or
///         iteration count outside its stated interval, or a layer count whose deepest tap spacing
///         exceeds what the transform allows, each carrying the value and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DarkStructurePlan> planDarkStructure(const DarkStructureRequest& request);

/// What one dark-structure operation did.
struct DarkStructureResult
{
    /// Pixel selection and writes, summed over the channels the operation wrote. A mask build
    /// writes one plane and reports its samples as outputs.
    KernelRunResult run{};

    /// How many channels were read.
    std::uint32_t channelCount = 0u;

    /// How many times the transfer was applied, which is zero for a mask build.
    std::uint32_t iterationsRun = 0u;

    /// How many stored samples the operation actually moved.
    std::uint64_t samplesChanged = 0u;

    /// The smallest finite value the dark-structure measurement took, in the image's own sample
    /// units, or zero when it took none.
    ///
    /// Reported before the measurement is rescaled into a mask, which is what makes it possible to
    /// tell a frame with no dark structure from one whose structure the rescaling normalised away.
    double measurementLowest = 0.0;

    /// The largest such value.
    double measurementHighest = 0.0;

    /// Whether that range, or the range of the denoised mask, was too narrow to normalise, leaving a
    /// mask of zeros.
    ///
    /// Not a failure: a frame with no dark structure has no dark structure to enhance, and the
    /// operation says so and leaves every sample inside `[0, whiteLevel]` where it was.
    bool maskDegenerate = false;

    /// How many mask coefficients the denoising shrinkage moved, summed over its passes, so a
    /// coefficient every pass moves counts once per pass.
    std::uint64_t maskCoefficientsShrunk = 0u;

    /// How many source samples the validity rule rejected and the border substitute replaced.
    std::uint64_t substitutedSamples = 0u;

    /// How many times the operation walked the whole image.
    std::uint64_t imagePasses = 0u;

    /// Context memory held at once, in bytes, across the working planes and the mask decomposition.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// The width the arithmetic ran at, which this operation always reports as
    /// `ComputePrecision::Float32`.
    ComputePrecision precision = ComputePrecision::Float32;
};

/// Measures where an image is darker than its own smooth version, and writes that as a mask.
///
/// The untyped entry point, and the one the exported library carries. The frame's fine scales are
/// removed, the result's large scales are removed again, and the difference between that second
/// smoothing and the frame is where the frame sits below its own illumination. That difference is
/// averaged over the channels, rescaled onto the unit interval by its own extremes, denoised
/// through a three-layer transform, rescaled again and clamped.
///
/// The mask is one plane whatever the image's channel count, because a dark structure is a place
/// rather than a colour.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at stage, layer and
/// row-block boundaries, and is not failure-atomic with respect to the mask plane.
///
/// Progress is reported through the execution's sink as each channel's measurement and the mask's
/// own denoise finish, in a total of the channels plus one when the request denoises the mask.
///
/// @param engine the context whose memory resource and budget own the mask decomposition.
/// @param execution the operation policy and engine-context lease, leased from `engine`.
/// @param sampleType what the source samples are.
/// @param source where they start. Must not be null.
/// @param sourceLayout where they sit.
/// @param plan the validated request.
/// @param mask the plane the mask goes into, of exactly the source's extent.
/// @param selection which samples mean something.
/// @return what the build did; `ErrorCode::NotInitialized` for an empty execution, an empty engine
///         context or an unvalidated plan; `ErrorCode::InvalidArgument` for a null buffer, a
///         representation this build does not know, a layout whose element size is not that
///         representation's, or a selection whose explicit-mask rule has no packed mask or whose
///         other rule is given one; `ErrorCode::IncompatibleArguments` when the execution is not
///         leased from `engine`, when the mask plane or the selection's packed mask is not the
///         source's extent, or when the promise cannot be kept in Float32;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution stops it; or
///         the memory resource's refusal of a working plane or of the mask decomposition.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DarkStructureResult>
buildImageDarkStructureMask(const EngineContext& engine, const ExecutionContext& execution, SampleType sampleType,
                            const void* source, const ImageLayout& sourceLayout, const DarkStructurePlan& plan,
                            PlaneView<float> mask, const SpatialSelection& selection = {});

/// Measures where a checked typed image view is darker than its own smooth version.
///
/// The typed spelling of `buildImageDarkStructureMask`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param engine the context whose memory owns the mask decomposition.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param plan the validated request.
/// @param mask the plane the mask goes into.
/// @param selection which samples mean something.
/// @return whatever `buildImageDarkStructureMask` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<DarkStructureResult>
buildDarkStructureMask(const EngineContext& engine, const ExecutionContext& execution,
                       const ImageView<const SampleT>& source, const DarkStructurePlan& plan, PlaneView<float> mask,
                       const SpatialSelection& selection = {})
{
    return buildImageDarkStructureMask(engine, execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), plan,
                                       mask, selection);
}

/// Darkens the structures of an image the mask selects, and leaves the rest of it alone.
///
/// The untyped entry point, and the one the exported library carries. The mask is built exactly as
/// `buildImageDarkStructureMask` builds it, and then each iteration evaluates the request's
/// transfer function over the whole frame and blends the transferred value towards the original by
/// the mask: a sample the mask calls one becomes the transferred value outright, a sample it calls
/// zero keeps its value, and the iterations compound. Every iteration's blend is clamped to
/// `[0, whiteLevel]`, including where the mask is zero, so a sample outside that interval is moved
/// to its nearer end wherever it lies. Inside it, an unselected sample is kept to within the
/// Float32 rounding of dividing by the white level and multiplying back.
///
/// A sample the validity rule rejects leaves its destination sample untouched.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at stage, iteration
/// and row-block boundaries, and is not failure-atomic.
///
/// Progress is reported through the execution's sink as the mask's passes finish and then as each
/// enhancement iteration of each channel finishes, in one total covering both, and ends at that total
/// when a degenerate mask skipped its denoise.
///
/// @param engine the context whose memory resource and budget own the mask decomposition.
/// @param execution the operation policy and engine-context lease, leased from `engine`.
/// @param sampleType what the samples are, on both sides.
/// @param source where the frame's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the enhanced samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param plan the validated request.
/// @param mask where to also write the mask the operation built, or null to keep it internal. When
///        present it must be exactly the source's extent.
/// @param selection which samples mean something.
/// @return what the enhancement did; whatever `buildImageDarkStructureMask` refuses, plus
///         `ErrorCode::InvalidArgument` for a null destination or a destination layout whose
///         element size is not the representation's, and `ErrorCode::IncompatibleArguments` for a
///         destination that is not the source's extent or that overlaps the source.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DarkStructureResult>
enhanceImageDarkStructures(const EngineContext& engine, const ExecutionContext& execution, SampleType sampleType,
                           const void* source, const ImageLayout& sourceLayout, void* destination,
                           const ImageLayout& destinationLayout, const DarkStructurePlan& plan,
                           const PlaneView<float>* mask = nullptr, const SpatialSelection& selection = {});

/// Darkens the structures a mask selects in a checked typed image view.
///
/// The typed spelling of `enhanceImageDarkStructures`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param engine the context whose memory owns the mask decomposition.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param plan the validated request.
/// @param mask where to also write the mask, or null.
/// @param selection which samples mean something.
/// @return whatever `enhanceImageDarkStructures` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<DarkStructureResult>
enhanceDarkStructures(const EngineContext& engine, const ExecutionContext& execution,
                      const ImageView<const SampleT>& source, const ImageView<SampleT>& destination,
                      const DarkStructurePlan& plan, const PlaneView<float>* mask = nullptr,
                      const SpatialSelection& selection = {})
{
    return enhanceImageDarkStructures(engine, execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                      destination.data(), destination.layout(), plan, mask, selection);
}

/// What a mask build does to the image it reads.
///
/// It writes a plane the caller owns rather than an image, so it reads samples and changes nothing
/// about the description, the meaning, the solution, the cards or the output decision of anything.
inline constexpr OperationEffect kDarkStructureMaskEffect{.samples = SampleEffect::Reads};

/// What an enhancement does to the image it writes.
///
/// It changes samples and nothing else. The extent, the representation and the channel schema are
/// the source's; a frame whose dark structures have been darkened is still a frame of the same
/// thing; nothing moves a star, because the transfer is per sample; and no source card and no
/// output decision is invalidated by it.
///
/// Validity is preserved, and that is a claim about the arithmetic: a rejected sample is replaced
/// by the border substitute once, before the first transform, and its own destination sample is
/// left untouched.
inline constexpr OperationEffect kDarkStructureEnhanceEffect{};

} // namespace an
