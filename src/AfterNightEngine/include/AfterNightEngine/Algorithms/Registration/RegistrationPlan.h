// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The canvas a registered set is written on, and the warp that writes it.
///
/// A transform says where one frame's pixels go. It does not say how large the picture is, which
/// part of it is kept, whether two frames have anything in common, or what it costs to resample one
/// - and those are the decisions a registration pass actually makes. This header owns them, as
/// values that are computed once and then inspected, rather than as arguments recomputed at every
/// place that needs them.
///
/// **The canvas is planned from bounds, not from images.** Every frame contributes the rectangle its
/// mapped boundary occupies, and the mode decides whether the result is the reference frame's
/// rectangle, the union of all of them, or their intersection. Nothing here reads a pixel, so the
/// plan for a five-hundred-frame set costs five hundred rectangles.
///
/// **A warp takes the cheapest route that is exact.** A linear transform over an undistorted frame
/// is a projective warp, which the geometry module already executes for every resident
/// representation; anything else needs a coordinate map, because a polynomial and a plate distortion
/// have no matrix. The route is resolved while planning and reported afterwards, so a run says which
/// one it took instead of leaving a reader to infer it from a duration.
///
/// Positions are engine pixel coordinates - the first pixel's centre is (0.5, 0.5). The sampling
/// kernels put centres at integers, and the half pixel between the two conventions is added here,
/// once, where the coordinates are handed over.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <span>

namespace an
{

class ExecutionContext;

/// Which frames the output canvas is sized to hold.
enum class RegistrationCanvasMode : std::uint8_t
{
    /// The reference frame's own mapped rectangle. Every other frame is cropped to it.
    Reference = 0u,

    /// The union of every frame's rectangle, which is what a mosaic needs.
    Union,

    /// The intersection of every frame's rectangle: the region every frame covers.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Intersection
};

/// Whether a value names a canvas mode this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mode the value to test.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isRegistrationCanvasMode(RegistrationCanvasMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(RegistrationCanvasMode::Intersection);
}

/// The canvas mode's own name.
///
/// Thread-safe and allocation-free.
///
/// @param mode the mode to name. A value outside the enumeration is named
///        `"UnknownRegistrationCanvasMode"`.
/// @return a null-terminated literal compiled into this library, which outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* registrationCanvasModeName(RegistrationCanvasMode mode) noexcept;

/// The largest canvas extent this engine plans, on either axis.
///
/// The image model's own extent ceiling still applies when a descriptor is built from the plan;
/// this is the arithmetic bound that keeps the rounding below from overflowing.
inline constexpr std::uint32_t kMaximumRegistrationCanvasExtent = 1000000u;

/// The output plane a registered set is written on.
///
/// `origin` is what every frame's mapped position is offset by to land on this canvas: it is the
/// negation of the bounding rectangle's corner, so the rectangle's top-left maps to (0, 0). It is
/// the `postTranslation` a warp of any frame in this set is given.
struct RegistrationCanvas
{
    /// The rectangle the canvas covers, in reference-frame pixels.
    RegistrationBounds boundingBox{};

    /// The offset added to a mapped position to place it on the canvas.
    PixelCoordinate origin{};

    /// Columns the warp writes.
    std::uint32_t width = 0u;

    /// Rows the warp writes.
    std::uint32_t height = 0u;

    /// Whether two canvases are the same plane.
    ///
    /// @param left one canvas.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationCanvas& left,
                                                   const RegistrationCanvas& right) noexcept = default;
};

/// Plans the output plane of a registered set.
///
/// The rectangle is the reference frame's under `Reference`, the union of every frame's under
/// `Union`, and the intersection of every frame's under `Intersection`. A union or reference canvas
/// rounds its extent up so no mapped sample falls outside it; an intersection rounds down, because
/// a region only some frames cover is not an intersection.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param bounds one mapped rectangle per frame, in the set's own order.
/// @param referenceIndex which entry is the reference, below `bounds.size()`.
/// @param mode which frames the canvas holds.
/// @return the plane; `ErrorCode::InvalidArgument` for an empty set, an unknown mode, a reference
///         index outside the set, or a non-finite or inverted rectangle;
///         `ErrorCode::EmptySelection` when the selected rectangle has no area, which is what an
///         intersection of frames with nothing in common produces; or `ErrorCode::LimitExceeded`
///         when an axis exceeds `kMaximumRegistrationCanvasExtent`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationCanvas>
planRegistrationCanvas(std::span<const RegistrationBounds> bounds, std::uint32_t referenceIndex,
                       RegistrationCanvasMode mode);

/// How a frame's samples reach the canvas.
enum class RegistrationSamplingRoute : std::uint8_t
{
    /// The frame's pixels sit on the canvas at a whole-pixel offset, so they are read where they
    /// are. No resampling, no interpolation, and no second copy of the frame.
    DirectRectangle = 0u,

    /// The frame has to be resampled, because its transform rotates, scales, shears, projects,
    /// bends or shifts it by a fraction of a pixel.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Warp
};

/// Whether a value names a sampling route this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param route the value to test.
/// @return true for both enumerators.
[[nodiscard]] constexpr bool isRegistrationSamplingRoute(RegistrationSamplingRoute route) noexcept
{
    return static_cast<std::uint8_t>(route) <= static_cast<std::uint8_t>(RegistrationSamplingRoute::Warp);
}

/// The route's own name.
///
/// Thread-safe and allocation-free.
///
/// @param route the route to name. A value outside the enumeration is named
///        `"UnknownRegistrationSamplingRoute"`.
/// @return a null-terminated literal compiled into this library, which outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* registrationSamplingRouteName(RegistrationSamplingRoute route) noexcept;

/// Where one frame sits on the canvas, and whether reading it needs a resample.
struct RegistrationPlacement
{
    /// Which route this frame takes.
    RegistrationSamplingRoute route = RegistrationSamplingRoute::Warp;

    /// The frame's first column on the canvas. Meaningful only for `DirectRectangle`.
    std::int64_t offsetX = 0;

    /// The frame's first row on the canvas. Meaningful only for `DirectRectangle`.
    std::int64_t offsetY = 0;

    /// Whether two placements are the same.
    ///
    /// @param left one placement.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationPlacement& left,
                                                   const RegistrationPlacement& right) noexcept = default;
};

/// Decides whether a frame's samples can be read where they are.
///
/// The direct route needs all of it: no polynomial, no plate distortion, an identity linear part,
/// and a translation that lands on whole pixels once the canvas origin is added. Anything else
/// resamples, and saying so here is what lets a caller that only wants statistics over an overlap
/// avoid materialising a warped frame it will throw away.
///
/// Never fails: a frame either qualifies or does not.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the frame's source-to-reference model.
/// @param canvasOrigin the canvas offset from `planRegistrationCanvas`.
/// @param sourceDistortion the frame's own plate distortion, or null when it has none.
/// @return the route and, for a direct one, the whole-pixel offset the frame sits at.
[[nodiscard]] AFTERNIGHTENGINE_API RegistrationPlacement
resolveRegistrationPlacement(const RegistrationTransform& transform, const PixelCoordinate& canvasOrigin,
                             const SipDistortion* sourceDistortion = nullptr) noexcept;

/// A rectangle of the canvas, in whole pixels.
struct RegistrationRegion
{
    /// First column.
    std::uint32_t x = 0u;

    /// First row.
    std::uint32_t y = 0u;

    /// Columns covered, at least one for a region that exists.
    std::uint32_t width = 0u;

    /// Rows covered, at least one for a region that exists.
    std::uint32_t height = 0u;

    /// How many pixels the region covers.
    ///
    /// @return the product of the extents, which cannot overflow a 64-bit count.
    [[nodiscard]] constexpr std::uint64_t pixelCount() const noexcept
    {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    }

    /// Whether two regions are the same rectangle.
    ///
    /// @param left one region.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationRegion& left,
                                                   const RegistrationRegion& right) noexcept = default;
};

/// One frame's contribution to overlap discovery.
struct RegistrationFootprint
{
    /// The rectangle the frame's mapped boundary occupies, in reference-frame pixels.
    RegistrationBounds bounds{};

    /// The frame's own pixel count, which the relative overlap threshold is measured against.
    std::uint64_t framePixels = 0u;
};

/// The most frames one overlap search relates.
///
/// The normalization solver's own ceiling, so a set this search accepts is a set that solver can
/// take.
inline constexpr std::uint32_t kMaxRegistrationOverlapFrames = 4096u;

/// What makes a pair of frames worth measuring.
struct RegistrationOverlapRequest
{
    /// The fewest canvas pixels a pair must share.
    std::uint64_t minimumOverlapPixels = 1000u;

    /// The share of the smaller frame a pair must cover, in [0, 1].
    ///
    /// The effective threshold is the larger of this and `minimumOverlapPixels`, so a set of small
    /// frames is not admitted by an absolute count that means nothing at their size.
    double minimumOverlapFraction = 0.01;

    /// Whether two requests state the same thresholds.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationOverlapRequest& left,
                                                   const RegistrationOverlapRequest& right) noexcept = default;
};

/// One geometrically overlapping pair.
struct RegistrationOverlap
{
    /// The lower frame index of the pair.
    std::uint32_t frameA = 0u;

    /// The higher frame index of the pair.
    std::uint32_t frameB = 0u;

    /// Where the two frames overlap, on the canvas: every canvas pixel the shared rectangle
    /// touches, rounded outwards to whole pixels and clipped to the canvas.
    RegistrationRegion region{};

    /// Whether two candidates describe the same overlap.
    ///
    /// @param left one candidate.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationOverlap& left,
                                                   const RegistrationOverlap& right) noexcept = default;
};

/// What one overlap search found.
struct RegistrationOverlapReport
{
    /// Pairs examined, which is every unordered pair of the set.
    std::uint64_t pairsExamined = 0u;

    /// Pairs whose shared region met both thresholds.
    std::uint64_t pairsAccepted = 0u;

    /// Accepted pairs actually written, which is the smaller of `pairsAccepted` and the
    /// destination's length.
    std::uint64_t pairsWritten = 0u;

    /// Whether the destination was too short to hold every accepted pair.
    ///
    /// The report is complete either way - `pairsAccepted` is the true count - so a caller resizes
    /// and asks again rather than discovering the loss later.
    bool truncated = false;
};

/// Finds every pair of frames that shares enough of the canvas to be measured.
///
/// An all-pairs test of axis-aligned rectangles, clipped to the canvas, which is the pre-filter a
/// normalization solver needs before it reads any samples: the pairs it rejects here are pairs it
/// would otherwise open two frames to measure. Pairs are written in ascending frame order, so the
/// answer does not depend on the thread count.
///
/// The region is the same rectangle for both frames, because it is stated on the canvas. A frame
/// whose placement is direct reads it at its own whole-pixel offset; a frame whose placement is a
/// warp resamples that rectangle and no more.
///
/// @param execution operation policy and engine-context lease, for cancellation.
/// @param footprints one entry per frame, in the set's own order.
/// @param canvas the plane the pairs are stated on.
/// @param request the thresholds a pair must meet.
/// @param overlaps where accepted pairs are written; may be empty to count them first.
/// @return what was found; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for a footprint whose rectangle is not finite or is
///         inverted, a zero frame pixel count, an out-of-range fraction, or a canvas with no extent
///         or a non-finite origin;
///         `ErrorCode::LimitExceeded` above `kMaxRegistrationOverlapFrames`; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationOverlapReport>
findRegistrationOverlaps(const ExecutionContext& execution, std::span<const RegistrationFootprint> footprints,
                         const RegistrationCanvas& canvas, const RegistrationOverlapRequest& request,
                         std::span<RegistrationOverlap> overlaps);

/// What one coordinate map is asked to fill.
///
/// The caller owns both planes. They are separate spans rather than one interleaved buffer because
/// every consumer of a coordinate map in this engine reads the two axes independently.
struct RegistrationMapRequest
{
    /// Width of the mapped grid, in pixels. Must not be zero.
    std::uint32_t width = 0u;

    /// Height of the mapped grid, in pixels. Must not be zero.
    std::uint32_t height = 0u;

    /// Horizontal destinations, row-major and exactly `width * height` long.
    std::span<float> xMap{};

    /// Vertical destinations, row-major and exactly `width * height` long.
    std::span<float> yMap{};

    /// The offset the forward map adds after everything else.
    PixelCoordinate postTranslation{};

    /// Where an inverse solve starts. Ignored by a forward map, which solves nothing.
    RegistrationInverseSeed seed = RegistrationInverseSeed::AuthoritativeSeed;
};

/// What a coordinate map did.
struct RegistrationMapReport
{
    /// Pixels that received a finite position.
    std::uint64_t mapped = 0u;

    /// Pixels left holding the unmapped sentinel `unmappedCoordinate` writes.
    std::uint64_t unmapped = 0u;

    /// Newton steps run across every pixel that needed one.
    std::uint64_t newtonIterations = 0u;

    /// Pixels whose answer came from the previous position along the row.
    std::uint64_t warmSeedAccepted = 0u;

    /// Pixels whose warm solve was abandoned and re-solved from the linear inverse.
    std::uint64_t warmSeedRejected = 0u;

    /// The largest round-trip distance any solved pixel left, in pixels.
    double maximumResidualPixels = 0.0;

    /// Whether two reports contain the same figures.
    ///
    /// @param left one report.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationMapReport& left,
                                                   const RegistrationMapReport& right) noexcept = default;
};

/// Fills a grid with the source position every destination pixel reads from.
///
/// The map a resampler consumes: for destination pixel `(x, y)` it holds the engine pixel
/// coordinate in the source frame whose value belongs there - the inverse image of that pixel's
/// centre, `(x + 0.5, y + 0.5)`, stored in single precision. The canvas translation is removed, the
/// registration transform is inverted, and the frame's own plate distortion - which the transform
/// was fitted without - is undone last, so the position lands where the distorted sensor actually
/// recorded it.
///
/// A pixel that does not invert receives `unmappedCoordinate` in both planes and is counted rather
/// than failing the map: one corner outside a polynomial's domain does not invalidate a frame.
///
/// Runs on the operation's scheduler grant, one tile per band of whole rows, and checks
/// cancellation per tile. A row-neighbour warm start is confined to one tile's rows, so the answer
/// does not depend on how the grid was divided.
///
/// @param execution the operation this map runs in.
/// @param transform the frame's source-to-reference model.
/// @param sourceDistortion the frame's own plate distortion, or null when it has none.
/// @param request the grid, the caller-owned planes, the canvas translation and the seed policy.
/// @return what the map did; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a zero dimension, an unknown seed policy or a
///         non-finite translation; `ErrorCode::IncompatibleArguments` when either plane is not
///         exactly `width * height` long; `ErrorCode::Aliasing` when the planes overlap;
///         `ErrorCode::LimitExceeded` when the pixel count does not fit; or a scheduling or
///         cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationMapReport>
computeRegistrationDestinationToSourceMap(const ExecutionContext& execution, const RegistrationTransform& transform,
                                          const SipDistortion* sourceDistortion, const RegistrationMapRequest& request);

/// Fills a grid with the canvas position every source pixel is written to.
///
/// The other direction, and the one a footprint or a drizzle pass wants: for source pixel
/// `(x, y)` it holds the engine canvas position its centre `(x + 0.5, y + 0.5)` lands on,
/// distortion first and canvas translation last. It solves nothing, so its report carries no
/// Newton or warm-seed counts.
///
/// @param execution the operation this map runs in.
/// @param transform the frame's source-to-reference model.
/// @param sourceDistortion the frame's own plate distortion, or null when it has none.
/// @param request the grid, the caller-owned planes and the canvas translation; its seed policy is
///        validated and unused.
/// @return what the map did, with the same failures the destination-to-source map returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationMapReport>
computeRegistrationSourceToDestinationMap(const ExecutionContext& execution, const RegistrationTransform& transform,
                                          const SipDistortion* sourceDistortion, const RegistrationMapRequest& request);

/// What a warp is asked to produce.
struct RegistrationWarpRequest
{
    /// Destination columns, at least one.
    std::uint32_t width = 1u;

    /// Destination rows, at least one.
    std::uint32_t height = 1u;

    /// The canvas offset added to every mapped position, from `planRegistrationCanvas`.
    PixelCoordinate canvasOrigin{};

    /// Interpolation, border and invalid-neighbour behavior.
    SamplingKernelParameters sampling{.interpolation = KernelInterpolation::Linear,
                                      .border = KernelBorderMode::Constant,
                                      .invalid = SamplingInvalidPolicy::RequireAll};

    /// Where an inverse solve starts on the coordinate-map route.
    RegistrationInverseSeed seed = RegistrationInverseSeed::AuthoritativeSeed;
};

/// A validated, reusable warp of one frame onto one canvas.
///
/// The plan resolves the route and the destination description once, so a set of frames sharing a
/// canvas pays for that resolution per frame rather than per tile. It is an ordinary exported
/// value: `warpRegisteredImage` rechecks its invariants, so a modified member cannot produce
/// unchecked arithmetic.
class RegistrationWarpPlan
{
public:
    /// Records a plan whose request has already been checked.
    ///
    /// Callers normally use `planRegistrationWarp`.
    ///
    /// @param route which way the samples are produced.
    /// @param sourceDescriptor the frame's description.
    /// @param destinationDescriptor the canvas description the result will have.
    /// @param transform the frame's source-to-reference model.
    /// @param distortion the frame's plate distortion; a default one corrects nothing.
    /// @param request the extent, canvas offset, sampling policy and seed policy.
    AFTERNIGHTENGINE_API
    RegistrationWarpPlan(RegistrationSamplingRoute route, const ImageDescriptor& sourceDescriptor,
                         const ImageDescriptor& destinationDescriptor, const RegistrationTransform& transform,
                         const SipDistortion& distortion, const RegistrationWarpRequest& request) noexcept;

    /// Which way this plan produces samples.
    ///
    /// @return `DirectRectangle` when the frame is copied at a whole-pixel offset, `Warp`
    ///         otherwise.
    [[nodiscard]] AFTERNIGHTENGINE_API RegistrationSamplingRoute route() const noexcept;

    /// Whether the warp needs a per-pixel coordinate map rather than one matrix.
    ///
    /// @return true when the transform carries a polynomial or the frame carries a distortion.
    [[nodiscard]] AFTERNIGHTENGINE_API bool needsCoordinateMap() const noexcept;

    /// The frame's description.
    ///
    /// @return the checked source descriptor.
    [[nodiscard]] AFTERNIGHTENGINE_API const ImageDescriptor& sourceDescriptor() const noexcept;

    /// The description the destination must have.
    ///
    /// @return the canvas extent with the source's meaning otherwise preserved.
    [[nodiscard]] AFTERNIGHTENGINE_API const ImageDescriptor& destinationDescriptor() const noexcept;

    /// The frame's source-to-reference model.
    ///
    /// @return the checked transform.
    [[nodiscard]] AFTERNIGHTENGINE_API const RegistrationTransform& transform() const noexcept;

    /// The frame's plate distortion.
    ///
    /// @return the model, which corrects nothing when the frame has none.
    [[nodiscard]] AFTERNIGHTENGINE_API const SipDistortion& distortion() const noexcept;

    /// The extent, canvas offset and policies the plan was built from.
    ///
    /// @return the checked request.
    [[nodiscard]] AFTERNIGHTENGINE_API const RegistrationWarpRequest& request() const noexcept;

private:
    /// Which way samples are produced.
    RegistrationSamplingRoute m_route;

    /// The frame's description.
    ImageDescriptor m_sourceDescriptor;

    /// The canvas description.
    ImageDescriptor m_destinationDescriptor;

    /// The frame's source-to-reference model.
    RegistrationTransform m_transform;

    /// The frame's plate distortion.
    SipDistortion m_distortion;

    /// The extent, canvas offset and policies.
    RegistrationWarpRequest m_request;
};

/// Builds a warp of one frame onto one canvas.
///
/// The destination description keeps the source's meaning and takes the canvas extent, its
/// astrometric solution invalidated and its output intent withdrawn, because the samples have moved.
/// An unresolved mosaic is refused: resampling a colour filter array averages different filters,
/// which is a demosaic nobody asked for.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param source the frame's description.
/// @param transform the frame's source-to-reference model.
/// @param distortion the frame's plate distortion; a default one means the frame has none.
/// @param request the extent, canvas offset, sampling policy and seed policy.
/// @return the plan; `ErrorCode::InvalidArgument` for a zero extent, an unknown sampling or seed
///         policy, a non-finite canvas offset, or a destination description the image model
///         refuses; `ErrorCode::IncompatibleArguments` for an unresolved mosaic source; or
///         `ErrorCode::LimitExceeded` when the canvas exceeds the image model's extent limits.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationWarpPlan>
planRegistrationWarp(const ImageDescriptor& source, const RegistrationTransform& transform,
                     const SipDistortion& distortion, const RegistrationWarpRequest& request);

/// Counts and execution facts from one warp.
struct RegistrationWarpResult
{
    /// Source selections and destination writes, summed over channels.
    KernelRunResult run{};

    /// Which route actually ran.
    RegistrationSamplingRoute route = RegistrationSamplingRoute::Warp;

    /// Channels warped.
    std::uint32_t channelCount = 0u;

    /// Workers the execution could use.
    std::uint32_t plannedWorkers = 0u;

    /// Context-owned coordinate and tile memory held at once.
    ///
    /// The whole point of tiling the destination: a full-canvas coordinate map of a large sensor is
    /// two floats per pixel per axis on top of the frame itself, and this is the number that says
    /// the map never reached that size.
    std::uint64_t workingBytes = 0u;

    /// What the coordinate map did, for a warp that needed one. Zero for the other routes.
    RegistrationMapReport mapping{};

    /// Destination samples left unwritten because no source position mapped to them.
    std::uint64_t unmappedSamples = 0u;
};

/// Warps one frame onto the canvas.
///
/// Three routes, resolved by the plan and reported in the result. A frame at a whole-pixel offset
/// with an identity linear part is copied. A linear transform over an undistorted frame becomes a
/// projective warp, executed by `Algorithms/Geometry/Geometry.h` for whatever representation the
/// frame is resident in. Anything else - a polynomial, a plate distortion, or both - is resampled
/// through a per-tile coordinate map, which needs Float32 samples because that is what this
/// engine's interpolating gather reads; another representation is refused by name so the caller
/// converts through `Image/ImageConversion.h` and the copy shows up in a profile.
///
/// `destinationValidity` is required and is rewritten completely: a set bit means the warp wrote
/// that sample, and every padding bit stays clear. Validation of layouts, policies and the plan
/// completes before the first sample write; cancellation after that may leave whole tiles written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the resident representation of both images.
/// @param source the first byte of the frame.
/// @param sourceLayout where the frame's samples sit.
/// @param destination the first byte of the disjoint canvas.
/// @param destinationLayout where the canvas's samples sit.
/// @param plan a checked plan whose descriptors match the call.
/// @param selection the frame's validity rule and optional packed bits.
///        Its mask storage must not overlap either writable output buffer; overlap is refused
///        with `ErrorCode::Aliasing` before any write. The output mask must also be disjoint from
///        both image buffers.
///        For a whole image, `maskPlane` is zero; a per-channel mask is indexed by the channel
///        being sampled. Invalid samples remain unwritten. A shared output mask is valid only
///        where every channel was written. Planar and interleaved layouts may differ between images.
/// @param destinationValidity packed output bits, one shared plane or one per channel.
/// @return counts and execution facts; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for a null or misaligned buffer;
///         `ErrorCode::IncompatibleArguments` when the representation or a layout disagrees with
///         the plan, a validity mask or the output mask does not cover its image, or the plan no
///         longer agrees with its own request; `ErrorCode::Aliasing` when the images overlap or a
///         mask overlaps writable storage; `ErrorCode::InvalidData` when a linear transform has no
///         inverse; `ErrorCode::Unsupported` when a coordinate-map route is asked for a
///         representation other than Float32; or a sampling-policy, memory, scheduling or
///         cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationWarpResult>
warpRegisteredImage(const ExecutionContext& execution, SampleType sampleType, const void* source,
                    const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                    const RegistrationWarpPlan& plan, const KernelValidityView& selection,
                    const ValidityMaskView<std::uint8_t>& destinationValidity);

/// A warp restates the description, invalidates the frame's own astrometric solution, drops the
/// source's codec cards and withdraws an output intent, and its explicit output mask is the
/// regenerated validity claim.
///
/// This is the transition that makes an image registered, and it belongs here rather than with the
/// model estimation: estimating a transform reads coordinate records and changes no image.
inline constexpr OperationEffect kRegistrationWarpEffect{
    .description = DescriptionEffect::Restates,
    .solution = SolutionEffect::Invalidates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = {.policy = ValidityPolicy::Regenerate,
                 .produced = *ValidityDescriptor::create({.rule = ValidityRule::ExplicitMask})}};

} // namespace an
