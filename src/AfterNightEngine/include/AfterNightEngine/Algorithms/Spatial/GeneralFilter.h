// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reusable plans for the remaining local image filters: the box average, the high-pass and unsharp
/// mask, edge tightening, morphology, and contrast-limited adaptive histogram equalization.
///
/// These operations share the spatial family's representation, validity, border, allocation and
/// write-back rules, stated in `Algorithms/Spatial/SpatialFilter.h`. A box average is expressed
/// through the same separable stencil as convolution; detail filters build on the published Gaussian
/// plan; edge tightening runs four dense stencils; morphology alone changes the reducer over a named
/// footprint. No operation carries a private edge convention of its own.
///
/// Every operation is planned once and executed any number of times. A plan holds only checked
/// scalar policy, so it is immutable, cheap to copy and safe to share between threads, and every
/// execution checks its plan's request again before it reads a sample. The one borrowed piece is a
/// custom morphology footprint, whose bytes the caller keeps alive for as long as a plan built from
/// it is used. Working planes, histograms and lookup tables come from the execution's engine context
/// for one call, and their byte counts are reported.
///
/// A centre the validity rule rejects is never written. A stencil or morphology neighbour it rejects
/// is replaced by the request's substitute, and equalization leaves a rejected sample out of its
/// tile histogram.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <span>

namespace an
{

/// Largest number of morphology passes one plan may request.
inline constexpr std::uint32_t kMaxMorphologyIterations = 16u;

/// One rectangular box-average request.
struct BoxFilterRequest
{
    /// Horizontal half-width, in pixels, no larger than `kMaxStencilRadius`.
    std::uint32_t radiusX = 1u;

    /// Vertical half-width, in pixels, no larger than `kMaxStencilRadius`.
    std::uint32_t radiusY = 1u;

    /// How an outside or invalid neighbour is supplied.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the result promises and how it becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A checked box average that may be reused for images of any extent.
class BoxFilterPlan
{
public:
    /// Records an already checked request.
    ///
    /// @param request the radii, border and write-back policy.
    AFTERNIGHTENGINE_API explicit BoxFilterPlan(const BoxFilterRequest& request) noexcept;

    /// The request from which the plan was derived.
    ///
    /// @return the recorded request.
    [[nodiscard]] AFTERNIGHTENGINE_API const BoxFilterRequest& request() const noexcept;

    /// Horizontal reach of the plan.
    ///
    /// @return the requested horizontal radius, in pixels.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t horizontalRadius() const noexcept;

    /// Vertical reach of the plan.
    ///
    /// @return the requested vertical radius, in pixels.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t verticalRadius() const noexcept;

private:
    /// The checked request.
    BoxFilterRequest m_request{};
};

/// Checks and records a box-average request.
///
/// Thread-safe and allocation-free.
///
/// @param request the radii, border and write-back policy.
/// @return a reusable plan; `ErrorCode::OutOfRange` for a radius above `kMaxStencilRadius`, carrying
///         the larger radius and the limit; or `ErrorCode::InvalidArgument` for an unknown border
///         mode, `KernelBorderMode::NotApplicable`, a non-finite border or invalid-neighbour value,
///         an output contract or conversion field outside its enumeration, or a write-back policy
///         naming `DomainMapping::FullScale`. Whether Float32 can keep the output promise for a
///         given representation is decided at execution, where the representation is known.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BoxFilterPlan> planBoxFilter(const BoxFilterRequest& request);

/// Applies a planned box average to every channel.
///
/// The two one-dimensional arrays are uniform and normalized independently, and a radius of zero
/// along an axis is the identity along it. Execution is routed through `convolveImageSeparable`, so
/// border resolution, invalid-neighbour substitution, cancellation, scheduling and write back have
/// one implementation for both operations.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the source image.
/// @param sourceLayout where the source samples sit.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where the destination samples sit.
/// @param plan a plan returned by `planBoxFilter`.
/// @param selection which source samples mean something.
/// @return the spatial counts and working plan; whatever `planBoxFilter` reports when the plan's
///         request is checked again; or any failure `convolveImageSeparable` documents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
filterImageBox(const ExecutionContext& execution, SampleType sampleType, const void* source,
               const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
               const BoxFilterPlan& plan, const SpatialSelection& selection = {});

/// Typed spelling of `filterImageBox`.
///
/// @tparam SampleT one of the eight resident sample types.
/// @param execution the operation policy and engine-context lease.
/// @param source checked source image.
/// @param destination checked disjoint destination image.
/// @param plan reusable box plan.
/// @param selection which samples mean something.
/// @return whatever `filterImageBox` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
filterBox(const ExecutionContext& execution, const ImageView<const SampleT>& source,
          const ImageView<SampleT>& destination, const BoxFilterPlan& plan, const SpatialSelection& selection = {})
{
    return filterImageBox(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                          destination.layout(), plan, selection);
}

/// Which detail image is produced from a source and its Gaussian low pass.
enum class DetailFilterMethod : std::uint8_t
{
    /// Write `source - lowPass + offset`.
    HighPass = 0u,

    /// Write `source + amount * (source - lowPass) + offset`.
    UnsharpMask = 1u
};

/// Whether a value names a supported detail operation.
///
/// @param method the value to inspect.
/// @return true for both declared methods.
[[nodiscard]] constexpr bool isDetailFilterMethod(DetailFilterMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(DetailFilterMethod::UnsharpMask);
}

/// A detail-filter request.
struct DetailFilterRequest
{
    /// Which arithmetic combines the source and low pass.
    DetailFilterMethod method = DetailFilterMethod::UnsharpMask;

    /// Horizontal Gaussian standard deviation, in pixels. Must be finite and above
    /// `kMinimumGaussianSigma`; the low pass is truncated at `kDefaultGaussianTruncation` sigmas.
    double sigmaX = 1.0;

    /// Vertical Gaussian standard deviation, in pixels, under the same rules as `sigmaX`.
    double sigmaY = 1.0;

    /// Detail multiplier for `UnsharpMask`; finite and non-negative. Checked but not read by
    /// `HighPass`.
    double amount = 1.0;

    /// Constant added after detail arithmetic, in stored code-value units.
    double offset = 0.0;

    /// Inclusive lower clamp in stored code-value units.
    double lowest = 0.0;

    /// Inclusive upper clamp in stored code-value units, not below `lowest`.
    ///
    /// The default interval `[0, 1]` suits a normalized floating-point image; an integer image
    /// states its own, or every result is clamped into the unit interval of its code values.
    double highest = 1.0;

    /// How the Gaussian answers outside and invalid neighbours.
    StencilBorderPolicy borders{.border = KernelBorderMode::ReflectExcludingEdge};

    /// What the result promises and how it becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A checked detail operation and its reusable Gaussian plan.
class DetailFilterPlan
{
public:
    /// Records a checked request and low-pass plan.
    ///
    /// @param request the detail arithmetic and bounds.
    /// @param gaussian the checked low-pass plan.
    AFTERNIGHTENGINE_API DetailFilterPlan(const DetailFilterRequest& request, const GaussianPlan& gaussian) noexcept;

    /// The checked request.
    ///
    /// @return the recorded request.
    [[nodiscard]] AFTERNIGHTENGINE_API const DetailFilterRequest& request() const noexcept;

    /// The low-pass plan used by the detail operation.
    ///
    /// @return the exact separable Gaussian plan.
    [[nodiscard]] AFTERNIGHTENGINE_API const GaussianPlan& gaussian() const noexcept;

private:
    /// The checked arithmetic request.
    DetailFilterRequest m_request{};

    /// The checked exact Gaussian plan.
    GaussianPlan m_gaussian;
};

/// Checks a detail operation and builds its exact Gaussian low pass.
///
/// @param request method, sigmas, amount, offset, clamp and output policy.
/// @return a reusable plan; `ErrorCode::InvalidArgument` for an unknown method, a non-finite value,
///         a negative amount or unordered bounds; whatever `planGaussianBlur` refuses; or
///         `ErrorCode::OutOfRange` when the low pass's radius is above `kMaxStencilRadius`, whose
///         tap arrays the stencil family's separable path holds, carrying the radius and the limit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DetailFilterPlan> planDetailFilter(const DetailFilterRequest& request);

/// Applies a planned high-pass or unsharp-mask operation.
///
/// Each channel is low-passed by the plan's Gaussian through the stencil family's separable path,
/// with Float64 accumulation - never through `blurImageGaussian`'s symmetric Float32 path - and the
/// arithmetic `DetailFilterMethod` names is then applied in Float64 at every accepted centre and
/// held inside `[lowest, highest]`. A centre the validity rule rejects is left untouched, and a
/// rejected neighbour of the low pass is replaced by the border policy's substitute.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the source image.
/// @param sourceLayout where the source samples sit.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where destination samples sit.
/// @param plan checked detail and Gaussian plan.
/// @param selection which samples mean something.
/// @return the accumulated spatial counts; whatever `planDetailFilter` reports when the plan's
///         request is checked again; or any failure `convolveImageSeparable` documents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
filterImageDetail(const ExecutionContext& execution, SampleType sampleType, const void* source,
                  const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                  const DetailFilterPlan& plan, const SpatialSelection& selection = {});

/// Parameters for the four-direction edge-tightening filter.
///
/// The four stencils use the family's two-axis border rule - a constant border of zero by default -
/// as every operation of the family does, so a tap past the left or right end of a row reads the
/// border and never the adjacent row.
struct EdgeTightenRequest
{
    /// Multiplier applied to the four directional edge magnitudes; finite and non-negative.
    double amount = 1.0;

    /// Constant added after subtracting the edge magnitude, in stored code-value units.
    double offset = 0.0;

    /// Inclusive lower clamp in stored code-value units.
    double lowest = 0.0;

    /// Inclusive upper clamp in stored code-value units, not below `lowest`.
    ///
    /// The default interval `[0, 1]` suits a normalized floating-point image; an integer image
    /// states its own, or every result is clamped into the unit interval of its code values.
    double highest = 1.0;

    /// How every directional stencil answers outside and invalid neighbours.
    StencilBorderPolicy borders{.border = KernelBorderMode::Constant};

    /// What the result promises and how it becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A checked, reusable four-direction edge-tightening plan.
class EdgeTightenPlan
{
public:
    /// Records a request whose arithmetic, border and output policies were checked.
    ///
    /// @param request the checked edge-tightening request.
    explicit AFTERNIGHTENGINE_API EdgeTightenPlan(const EdgeTightenRequest& request) noexcept;

    /// The checked request.
    ///
    /// @return the recorded edge arithmetic and policies.
    [[nodiscard]] AFTERNIGHTENGINE_API const EdgeTightenRequest& request() const noexcept;

private:
    /// The checked edge arithmetic and policies.
    EdgeTightenRequest m_request{};
};

/// Checks the four-direction edge-tightening arithmetic and policies.
///
/// @param request amount, offset, clamp, border and output policy.
/// @return a reusable plan; or `ErrorCode::InvalidArgument` for a negative or non-finite amount, a
///         non-finite offset or clamp bound, a lower bound above the upper one, an unknown border
///         mode, `KernelBorderMode::NotApplicable`, a non-finite border or invalid-neighbour value,
///         an output contract or conversion field outside its enumeration, or a write-back policy
///         naming `DomainMapping::FullScale`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<EdgeTightenPlan> planEdgeTighten(const EdgeTightenRequest& request);

/// Subtracts a weighted four-direction Sobel magnitude from every valid source sample.
///
/// The four immutable three-by-three kernels are applied with the dense stencil's arithmetic, border
/// and validity rules: each response is a Float64 sum of all nine taps in the stencil's order, an
/// outside neighbour answered by the border rule and an invalid one by `invalidNeighbor`, narrowed
/// to Float32. Their absolute values are summed in Float32 in the kernels' order, scaled by 0.05 and
/// `amount`, and subtracted from the source before `offset` and the inclusive clamp are applied. An
/// invalid centre leaves its destination sample untouched.
///
/// One pass computes all four responses of a centre from its one neighbourhood. An execution that
/// forces `ComputeBackend::Reference` runs the four dense convolutions and the passes that
/// accumulate their magnitudes into a Float32 plane instead - eight passes, the composition this
/// operation is defined by - and both write the same bits.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the source image.
/// @param sourceLayout where source samples sit.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where destination samples sit.
/// @param plan checked edge-tightening plan.
/// @param selection which samples mean something.
/// @return the spatial counts and the passes that ran - one, or eight on the reference backend;
///         whatever `planEdgeTighten` reports when the plan's request is checked again; or any
///         failure `convolveImageDense` documents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
filterImageEdgeTighten(const ExecutionContext& execution, SampleType sampleType, const void* source,
                       const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                       const EdgeTightenPlan& plan, const SpatialSelection& selection = {});

/// Shapes available to a morphology plan.
enum class MorphologyShape : std::uint8_t
{
    /// Every position in the rectangular extent participates.
    Rectangle = 0u,

    /// Positions whose normalized elliptical distance is at most one participate.
    ///
    /// A zero radius along one axis reduces the ellipse to the centre line along the other. The
    /// rasterization is this engine's own and differs from the imaging library's element at some
    /// radii.
    Ellipse = 1u,

    /// Positions whose Manhattan distance from the centre is within the radius participate. The
    /// two stated radii must be equal.
    Diamond = 2u,

    /// The horizontal and vertical centre lines participate.
    Cross = 3u,

    /// The two diagonals through the centre participate. The two stated radii must be equal.
    DiagonalCross = 4u,

    /// Positions whose corresponding byte in `MorphologyRequest::customFootprint` is nonzero.
    Custom = 5u
};

/// Whether a value names a supported morphology footprint.
///
/// @param shape the value to inspect.
/// @return true for each declared shape, including a caller-supplied footprint.
[[nodiscard]] constexpr bool isMorphologyShape(MorphologyShape shape) noexcept
{
    return static_cast<std::uint8_t>(shape) <= static_cast<std::uint8_t>(MorphologyShape::Custom);
}

/// Morphological operations built from erosion and dilation.
enum class MorphologyOperation : std::uint8_t
{
    /// Minimum over the footprint.
    Erode = 0u,

    /// Maximum over the footprint.
    Dilate = 1u,

    /// Erosion followed by dilation.
    Open = 2u,

    /// Dilation followed by erosion.
    Close = 3u,

    /// Dilation minus erosion.
    Gradient = 4u,

    /// Source minus its opening.
    TopHat = 5u,

    /// Closing minus source.
    BlackHat = 6u,

    /// Writes one where every sample under the footprint is nonzero and zero elsewhere.
    ///
    /// Positions the footprint excludes are ignored, and there is no second set of positions that
    /// must be background. A neighbour the validity rule rejects, and one outside the image under
    /// `KernelBorderMode::Constant`, reads `MorphologyRequest::constantBorder`, so a nonzero
    /// constant counts as foreground.
    HitMiss = 7u
};

/// Whether a value names a supported morphology operation.
///
/// @param operation the value to inspect.
/// @return true for each declared operation.
[[nodiscard]] constexpr bool isMorphologyOperation(MorphologyOperation operation) noexcept
{
    return static_cast<std::uint8_t>(operation) <= static_cast<std::uint8_t>(MorphologyOperation::HitMiss);
}

/// A morphology request.
struct MorphologyRequest
{
    /// Which primitive or composition runs.
    MorphologyOperation operation = MorphologyOperation::Erode;

    /// Which positions inside the extent participate.
    MorphologyShape shape = MorphologyShape::Rectangle;

    /// Horizontal half-width, in pixels, no larger than `kMaxStencilRadius`. `Diamond` and
    /// `DiagonalCross` need it equal to `radiusY`.
    std::uint32_t radiusX = 1u;

    /// Vertical half-width, in pixels, no larger than `kMaxStencilRadius`.
    std::uint32_t radiusY = 1u;

    /// Times the complete operation is repeated, from one through `kMaxMorphologyIterations`.
    ///
    /// Each repetition applies the whole composition to the previous one's answer, so an opening
    /// repeated twice is opened twice rather than eroded twice and then dilated twice.
    std::uint32_t iterations = 1u;

    /// How outside positions are supplied. `KernelBorderMode::NotApplicable` is refused.
    KernelBorderMode border = KernelBorderMode::ReflectExcludingEdge;

    /// Finite value used by `KernelBorderMode::Constant` and for invalid neighbours.
    float constantBorder = 0.0f;

    /// What the result promises and how it becomes a stored sample.
    SpatialOutputPolicy output{};

    /// Row-major footprint bytes for `Custom`, exactly `(2 * radiusX + 1) * (2 * radiusY + 1)`.
    ///
    /// A nonzero byte includes that position. The memory is borrowed by the request and by any
    /// plan built from it, so it must outlive planning and every execution that uses the plan. It
    /// must be empty for every named shape.
    std::span<const std::uint8_t> customFootprint{};
};

/// A checked morphology footprint and composition.
class MorphologyPlan
{
public:
    /// Records an already checked request.
    ///
    /// For a custom shape, the plan borrows the footprint memory in `request`; that memory must
    /// outlive every use of the plan.
    ///
    /// @param request operation, shape, reach, repetitions, footprint and policies.
    AFTERNIGHTENGINE_API explicit MorphologyPlan(const MorphologyRequest& request) noexcept;

    /// The checked request.
    ///
    /// @return the recorded request.
    [[nodiscard]] AFTERNIGHTENGINE_API const MorphologyRequest& request() const noexcept;

    /// Passes over the image required for one execution.
    ///
    /// @return the iteration count times the passes one repetition makes: one for erosion, dilation
    ///         or hit-miss, two for an opening or closing, and three - two primitives and the
    ///         subtraction - for a gradient, top-hat or black-hat.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t imagePasses() const noexcept;

private:
    /// The checked request.
    MorphologyRequest m_request{};
};

/// Checks and records a morphology request.
///
/// A custom footprint must contain exactly one byte per position and at least one nonzero byte.
/// Named shapes refuse a custom array instead of silently ignoring it. The returned plan borrows
/// custom bytes, which must remain alive through every execution using the plan.
///
/// @param request operation, shape, reach, repetitions, optional custom footprint and policies.
/// @return a reusable plan; `ErrorCode::InvalidArgument` for an unknown operation, shape or border,
///         `KernelBorderMode::NotApplicable`, a non-finite constant, unequal radii for a diamond or
///         diagonal cross, a custom footprint with no nonzero byte, custom bytes supplied for a
///         named shape, an output contract or conversion field outside its enumeration, or a
///         write-back policy naming `DomainMapping::FullScale`; `ErrorCode::IncompatibleArguments`
///         when a custom footprint's length differs from its extent; or `ErrorCode::OutOfRange` for
///         an iteration count of zero or a radius or iteration count above its published ceiling.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MorphologyPlan> planMorphology(const MorphologyRequest& request);

/// Applies a planned morphology operation to every channel.
///
/// Each primitive visits every position of the footprint for every output, so its cost grows with
/// the footprint's area. A neighbour outside the image follows the border mode and one the validity
/// rule rejects reads `MorphologyRequest::constantBorder`; a centre the rule rejects is left
/// untouched. The operation holds four Float32 working planes beside the family's, whatever it
/// composes, and reports each radius as the request's radius times the iteration count.
///
/// A composition or a repetition reads planes an earlier pass wrote. When the selection can reject
/// a sample, each such plane is first seeded with what its own pass reads, so a centre that pass
/// skipped still holds its rejected sample for the next pass to reject again; the working memory is
/// never read before it is written. Those copies are not among `MorphologyPlan::imagePasses`.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the source image.
/// @param sourceLayout where source samples sit.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where destination samples sit.
/// @param plan checked footprint and composition.
/// @param selection which source samples mean something.
/// @return spatial counts and working-memory facts; whatever `planMorphology` reports when the
///         plan's request is checked again; the image, selection and memory failures
///         `convolveImageSeparable` documents; or a cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
filterImageMorphology(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                      const MorphologyPlan& plan, const SpatialSelection& selection = {});

/// Configuration of contrast-limited adaptive histogram equalization.
struct AdaptiveHistogramRequest
{
    /// Number of tile columns, at least one.
    std::uint32_t tileColumns = 8u;

    /// Number of tile rows, at least one.
    std::uint32_t tileRows = 8u;

    /// Histogram resolution, from two through 65536.
    std::uint32_t binCount = 256u;

    /// Maximum average-bin multiple before excess counts are redistributed; finite and positive.
    double clipLimit = 2.0;

    /// Lowest code value represented by the histogram, and the lowest the result takes. A sample
    /// below it counts in the first bin.
    double lowest = 0.0;

    /// Highest code value represented by the histogram, strictly above `lowest`, and the highest the
    /// result takes. A sample above it counts in the last bin.
    double highest = 1.0;

    /// What the result promises and how it becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// A checked adaptive-histogram plan.
class AdaptiveHistogramPlan
{
public:
    /// Records an already checked request.
    ///
    /// @param request tile grid, histogram range and output policy.
    AFTERNIGHTENGINE_API explicit AdaptiveHistogramPlan(const AdaptiveHistogramRequest& request) noexcept;

    /// The checked request.
    ///
    /// @return the recorded request.
    [[nodiscard]] AFTERNIGHTENGINE_API const AdaptiveHistogramRequest& request() const noexcept;

private:
    /// The checked request.
    AdaptiveHistogramRequest m_request{};
};

/// Counts and plan facts from adaptive histogram equalization.
struct AdaptiveHistogramResult
{
    /// Common spatial counts, layout path and working memory.
    SpatialFilterResult filter{};

    /// Histogram tiles processed per channel.
    std::uint64_t tileCount = 0u;

    /// Bins in every tile histogram.
    std::uint32_t binCount = 0u;
};

/// Checks and records an adaptive-histogram request.
///
/// @param request tile grid, histogram range, clip limit and output policy.
/// @return a reusable plan; `ErrorCode::OutOfRange` for a zero tile count or a bin count outside
///         `[2, 65536]`; or `ErrorCode::InvalidArgument` for a clip limit that is not finite and
///         positive, a histogram range that is not finite and increasing, an output contract or
///         conversion field outside its enumeration, or a write-back policy naming
///         `DomainMapping::FullScale`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AdaptiveHistogramPlan>
planAdaptiveHistogramEqualization(const AdaptiveHistogramRequest& request);

/// Applies contrast-limited adaptive histogram equalization to every channel.
///
/// The frame is divided into `tileColumns` by `tileRows` tiles as equal as whole pixels allow. Each
/// tile's histogram counts the samples the validity rule accepts in equal-width bins over
/// `[lowest, highest]`, is clipped at `clipLimit` times its average bin with the clipped counts
/// redistributed evenly, and becomes a cumulative mapping onto `[lowest, highest]`. Each accepted
/// sample is mapped through the tiles around it, bilinearly interpolated at pixel centres, so tile
/// boundaries do not become image edges; a pixel beyond the outermost tile centres takes the border
/// tile's own mapping. A tile with no admitted samples contributes the source value clamped to
/// `[lowest, highest]`, without bin quantization. The tables are rebuilt for each channel, so their memory does not
/// grow with the channel count.
///
/// The tile centres are the centres of their pixels - a pixel at `x` maps to the tile coordinate
/// `(x + 0.5) / tileWidth - 0.5` - which places them half a pixel away from OpenCV's equalization; the
/// geometric rule is kept on purpose.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the representation used by both images.
/// @param source first byte of the source image.
/// @param sourceLayout where source samples sit.
/// @param destination first byte of the disjoint destination image.
/// @param destinationLayout where destination samples sit.
/// @param plan checked tile and histogram plan.
/// @param selection which samples contribute and which centres may be written.
/// @return counts and plan facts; whatever `planAdaptiveHistogramEqualization` reports when the
///         plan's request is checked again; `ErrorCode::OutOfRange` for a grid with more tiles than
///         the image has pixels along an axis; `ErrorCode::LimitExceeded` when the size of the tile
///         tables overflows; the image, selection and memory failures `convolveImageSeparable`
///         documents; or a cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AdaptiveHistogramResult>
equalizeImageAdaptiveHistogram(const ExecutionContext& execution, SampleType sampleType, const void* source,
                               const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                               const AdaptiveHistogramPlan& plan, const SpatialSelection& selection = {});

/// Metadata effect of adaptive histogram equalization.
///
/// The sample values gain a local nonlinear transfer and an orchestrator must state that new
/// processing meaning. Geometry, astrometry, source cards and output intent remain applicable.
inline constexpr OperationEffect kAdaptiveHistogramEffect{.semantics = SemanticEffect::Restates};

} // namespace an
