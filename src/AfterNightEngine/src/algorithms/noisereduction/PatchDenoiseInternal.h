// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what the two patch-denoising operations share.
//
// The public entry points in `Algorithms/NoiseReduction/PatchDenoise.h` do the same three things for
// both operations: check the arguments, widen the caller's storage into a Float32 frame the
// estimators work on, and write the answer back under the stated conversion. What differs is the
// estimator in the middle, and each of them is a translation unit of its own because they are large
// and have nothing in common but this header.
//
// A frame here is channel-major and tightly packed - `channels` planes of `width * height` floats,
// one after another - which is the layout both published methods index against. That is a deliberate
// choice rather than an inherited one: a patch estimator addresses a rectangle of one channel and
// then the same rectangle of the next, so an interleaved frame would cost a stride multiply in the
// innermost loop of the whole operation. The caller's image is gathered into this once.
//
// Nothing here allocates outside the engine context. The frames are spans over one block the entry
// point took from the operation's engine context, and each estimator takes its workers' scratch and
// counters from that context too, sized from its tuning and the tile grid before the first patch.

#pragma once

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>

/// The patch-denoising domain's private machinery: the working frame, the tile grid, the two
/// estimators the public entry points share, and the domain's failure builders.
namespace an::patch_detail
{

/// A channel-major stack of tightly packed Float32 planes.
///
/// Trivially copyable; it points into memory it does not own.
struct WorkingFrame
{
    /// The first sample of the first channel. Never null in a frame that describes anything.
    float* base = nullptr;

    /// How many pixels one row holds.
    std::uint32_t width = 0u;

    /// How many rows the frame holds.
    std::uint32_t height = 0u;

    /// How many channels follow one another.
    std::uint32_t channels = 0u;

    /// How many samples one channel holds.
    ///
    /// @return width times height.
    [[nodiscard]] constexpr std::uint64_t planeSamples() const noexcept
    {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    }

    /// How many samples the whole frame holds.
    ///
    /// @return the plane size times the channel count.
    [[nodiscard]] constexpr std::uint64_t sampleCount() const noexcept
    {
        return planeSamples() * static_cast<std::uint64_t>(channels);
    }

    /// How many bytes the whole frame holds.
    ///
    /// @return the sample count in bytes.
    [[nodiscard]] constexpr std::uint64_t byteCount() const noexcept
    {
        return sampleCount() * sizeof(float);
    }

    /// The first sample of one channel.
    ///
    /// @param channel which channel, below `channels`.
    /// @return the pointer.
    [[nodiscard]] constexpr float* plane(std::uint32_t channel) const noexcept
    {
        return base + static_cast<std::uint64_t>(channel) * planeSamples();
    }
};

/// One coordinate reflected back inside an extent, with the edge sample repeated.
///
/// The rule the whole family resolves a border with, the one `KernelBorderMode::Reflect` names: the
/// mirror sits half a sample outside the edge, so -1 reads 0, -2 reads 1, and `extent` reads
/// `extent - 1`, and the edge sample appears twice. Applied repeatedly until the position lands
/// inside, so a coordinate several extents away is still answered.
///
/// @param position where the read wants to be, possibly outside.
/// @param extent how many samples there are. Must be at least one.
/// @return the sample index inside the extent.
[[nodiscard]] std::uint32_t symmetricCoordinate(std::int64_t position, std::uint32_t extent) noexcept;

/// A tile of the frame, with the boundary it keeps around itself.
///
/// The interior is what is kept; the boundary is denoised and discarded, and exists so that a patch
/// near the interior's edge still has a full search window to look in.
struct FrameTile
{
    /// First interior column, in frame coordinates.
    std::uint32_t originX = 0u;

    /// First interior row, in frame coordinates.
    std::uint32_t originY = 0u;

    /// How many interior columns this tile keeps.
    std::uint32_t interiorWidth = 0u;

    /// How many interior rows this tile keeps.
    std::uint32_t interiorHeight = 0u;
};

/// How many tiles a frame is divided into along each axis, given the interiors its tiles may have.
///
/// The preferred interior when the frame holds sixteen tiles of it; otherwise the largest interior
/// down to the smallest that gives sixteen, or the smallest when none does. Either way at most
/// `kMaximumPatchDenoiseTiles`, and at least one tile along an axis the interior does not fit.
///
/// @param width the frame width.
/// @param height the frame height.
/// @param preferredInterior the interior a tile has when the frame is large enough.
/// @param smallestInterior the least interior a tile may have. At least one, and at most the preferred.
/// @param tilesX how many tiles across, written on return.
/// @param tilesY how many tiles down, written on return.
void tileGridBetween(std::uint32_t width, std::uint32_t height, std::uint32_t preferredInterior,
                     std::uint32_t smallestInterior, std::uint32_t& tilesX, std::uint32_t& tilesY) noexcept;

/// Where one tile of a grid sits.
///
/// The grid divides the frame into `tilesX` by `tilesY` interiors of as equal a size as integer
/// division allows, which is what makes the division a function of the extent alone.
///
/// @param width the frame width.
/// @param height the frame height.
/// @param tilesX how many tiles across.
/// @param tilesY how many tiles down.
/// @param index which tile, below `tilesX * tilesY`, in row-major order.
/// @return the tile.
[[nodiscard]] FrameTile tileAt(std::uint32_t width, std::uint32_t height, std::uint32_t tilesX, std::uint32_t tilesY,
                               std::uint32_t index) noexcept;

/// Reads one bordered tile out of a frame, reflecting through `symmetricCoordinate` where it reaches
/// outside.
///
/// @param frame the frame to read.
/// @param tile which interior to centre on.
/// @param boundary how many samples of border to keep on every side.
/// @param destination a frame of exactly the bordered size and the source's channel count.
void readTile(const WorkingFrame& frame, const FrameTile& tile, std::uint32_t boundary,
              const WorkingFrame& destination) noexcept;

/// Writes one tile's interior back into a frame.
///
/// @param tileFrame the bordered tile holding the answer.
/// @param tile which interior it belongs to.
/// @param boundary how many samples of border the tile carries.
/// @param frame the frame to write into.
void writeTileInterior(const WorkingFrame& tileFrame, const FrameTile& tile, std::uint32_t boundary,
                       const WorkingFrame& frame) noexcept;

/// The tuning both estimator passes run with.
struct EstimatorTuning
{
    /// The basic estimate's parameters.
    PatchDenoisePassParameters first{};

    /// The final estimate's parameters.
    PatchDenoisePassParameters second{};
};

/// What one estimator run counted.
struct EstimatorCounters
{
    /// How many patch groups were formed, over both passes and every tile.
    std::uint64_t groups = 0u;

    /// How many were declared featureless and replaced by their mean.
    std::uint64_t homogeneousGroups = 0u;

    /// How many covariance matrices were not positive definite.
    std::uint64_t inversionFailures = 0u;

    /// How many bytes of working memory were held at once: the larger of the two passes' scratch,
    /// raised to whatever it already holds.
    std::uint64_t workingBytes = 0u;

    /// How many tiles the first pass divided the frame into.
    std::uint32_t tileCount = 0u;
};

/// Denoises a frame without touching it: reads `noisy`, writes `basic` and `output`.
///
/// Two passes. The first collects, around each reference patch, the closest patches in a search
/// window and replaces the group by its Bayesian estimate; the second repeats the search on the
/// first pass's answer, where the distances mean something, and estimates the noisy patches under
/// the covariance the basic ones imply. Each runs over its own tile grid, derived from the extent
/// and the pass's boundary, so the answer does not depend on the thread count. The first pass runs
/// in the decorrelated colour space, keeping that copy of `noisy` in `output` until the second
/// overwrites it. Each pass takes its workers' scratch from the engine context before its first
/// tile, and reports progress as its tiles finish, in one half of the range the caller installed.
///
/// @param execution the operation policy and engine-context lease.
/// @param noisy the frame to denoise, in the caller's channel order.
/// @param basic scratch of the noisy frame's shape, which receives the first pass's answer.
/// @param output where the second pass's answer goes. The three frames must share no memory: the
///        first pass reads `output` while writing `basic`, and the second reads `noisy` and `basic`
///        while writing tile interiors into `output`.
/// @param tuning the parameters both passes run with.
/// @param tunedSigma the noise level on the estimator's own scale.
/// @param counters what the run counted: the group counts are added to whatever they already hold,
///        the tile count is replaced and the working bytes are raised to this run's peak.
/// @param operation the name carried by any failure.
/// @return success, or an execution, cancellation or memory failure.
[[nodiscard]] Status runPatchEstimator(const ExecutionContext& execution, const WorkingFrame& noisy,
                                       const WorkingFrame& basic, const WorkingFrame& output,
                                       const EstimatorTuning& tuning, float tunedSigma, EstimatorCounters& counters,
                                       const char* operation);

/// The geometry a guided-shrinkage run works to.
struct GuidedShrinkageGeometry
{
    /// Half the side of a patch, in pixels.
    std::uint32_t radius = 0u;

    /// The side of the square transform, in samples. A power of two at or above `2 * radius + 1`.
    ///
    /// A patch is the whole transform side, with its centre `radius` samples in from its top-left
    /// corner, so it extends `transformSize - radius - 1` samples past the centre on the far sides.
    std::uint32_t transformSize = 0u;

    /// The noise standard deviation, in the frame's own units.
    float sigma = 0.0f;

    /// The standard deviation of the spatial half of the patch weight, in pixels.
    float spatialSigma = 0.0f;

    /// What multiplies the noise variance in the range half of the patch weight.
    float rangeFactor = 0.0f;

    /// How much aggregation weight every pixel must accumulate before the run stops.
    float coverageThreshold = 0.0f;
};

/// What one guided-shrinkage run counted.
struct GuidedShrinkageCounters
{
    /// How many patches were placed, over every tile.
    std::uint64_t patchesPlaced = 0u;

    /// How many of those took the low-coverage path, where the patch weight was too small for a
    /// transform to say anything and the guide was accepted as it stood.
    std::uint64_t guideAcceptedPatches = 0u;

    /// How many bytes of working memory were held at once.
    std::uint64_t workingBytes = 0u;

    /// How many tiles the frame was divided into.
    std::uint32_t tileCount = 0u;
};

/// Shrinks a noisy frame's spectrum where a guide says there is nothing.
///
/// Over a tile grid derived from the extent and the transform side, each tile is read with its pad
/// reflected from the frames and colour-decorrelated when it has three channels; patches are then
/// placed at the least-covered pixel until every pixel of the interior has accumulated
/// `coverageThreshold` of aggregation weight, and the interior is written back. A sample's
/// aggregation weight is the square of its bilateral weight. Each worker's scratch is taken from the
/// engine context before the first tile, one pair of transform plans serves every tile, cancellation
/// is checked before every tile and every patch, and progress is reported as tiles finish, in a
/// total of the tiles.
///
/// @param execution the operation policy and engine-context lease.
/// @param noisy the original frame.
/// @param guide the denoised frame that says where the detail is, of the same shape.
/// @param output where the answer goes, of the same shape. Must share no memory with either input,
///        because one tile's interior is written while another still reads its pad.
/// @param geometry the patch size, the noise level and the weights.
/// @param counters what the run counted: the patch counts are added to whatever they already hold,
///        the tile count is replaced and the working bytes are raised to this run's peak.
/// @param operation the name carried by any failure.
/// @return success, or an execution, cancellation, transform-library or memory failure, or
///         `ErrorCode::Internal` when a transform buffer does not satisfy the adapter.
[[nodiscard]] Status runGuidedShrinkage(const ExecutionContext& execution, const WorkingFrame& noisy,
                                        const WorkingFrame& guide, const WorkingFrame& output,
                                        const GuidedShrinkageGeometry& geometry, GuidedShrinkageCounters& counters,
                                        const char* operation);

/// One failure from the patch-denoising domain.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status patchFailure(ErrorCode code, const char* operation, const char* message);

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status patchLimitFailure(ErrorCode code, const char* operation, const char* message,
                                       std::uint64_t requested, std::uint64_t limit);

} // namespace an::patch_detail
