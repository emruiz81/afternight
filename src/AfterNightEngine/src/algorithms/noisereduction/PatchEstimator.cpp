// AfterNightEngine - the two-pass Non-Local Bayes patch estimator.
//
// Provenance: adapted from Marc Lebrun's implementation of NL-Bayes published on IPOL,
// "Implementation of the 'Non-Local Bayes' (NL-Bayes) Image Denoising Algorithm"
// (https://www.ipol.im/pub/art/2013/16/, `nl-bayes_20130617`), whose files state
// `Copyright (c) 2013, Marc Lebrun <marc.lebrun.ik@gmail.com>` and are distributed under the GNU General
// Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. What was carried is the arithmetic, value for value: the
// colour decorrelation, the similarity search, the featureless-group shortcut, the barycentre-centred
// covariance, the Cholesky inverse, the paste trick and the aggregation. The parameter tables sit
// with the plan, in `PatchDenoisePlan::passParameters`.
// The provenance note is `docs/dev/PATCH_DENOISE_PROVENANCE.md`.
//
// What changed is everything around that arithmetic, and the places inside it where the published
// code's behaviour is unspecified or reads the wrong samples. There is no allocation here: a run
// takes one block per worker from the operation's engine context, sized from the tuning before the
// first patch, and every buffer below is a span into it, in place of the published per-group
// vectors and sub-image copies. The tile grid is a function of the frame extent rather than of the
// host's core count, so two hosts denoising one frame get one answer. The order patches are
// collected in is a strict total order on distance and then position, rather than the unspecified
// order a partial sort leaves, so two standard libraries get one answer too. The second pass's
// featureless test measures each channel's own samples, where the published code reads its group
// with the wrong stride. A non-finite basic sample is replaced by the noisy one rather than by its
// left neighbour. And a cancelled run stops at a tile boundary having written only working frames,
// never the caller's destination.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "algorithms/noisereduction/PatchDenoiseInternal.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

namespace an::patch_detail
{
namespace
{

/// What the estimator says it is doing on its first pass, once per tile it finishes.
constexpr const char* kBasicPassStage = "patch denoise: basic estimate";

/// What the estimator says it is doing on its second pass.
constexpr const char* kFinalPassStage = "patch denoise: final estimate";

/// One candidate patch as one sortable integer: its distance from the reference in the upper half and
/// where its first sample sits in the tile in the lower.
///
/// The position is part of the order on purpose. The published method sorts on distance alone and
/// keeps whatever order the sort happened to leave equal candidates in, which is unspecified: two
/// standard libraries partition an array of ties differently and the two answers then differ in the
/// last bits of every aggregated sample. Ordering ties by position makes the comparison a strict
/// total order, so the selected group and the order it is accumulated in are the same everywhere. A
/// distance is a sum of squares from positive zero, so its sign bit is clear and its bits order as
/// its value does; one integer comparison of two keys is the comparison of their distances and then
/// of their positions.
using CandidateKey = std::uint64_t;

/// The key of one candidate.
///
/// @param distance the sum of squared differences from the reference patch.
/// @param position where the candidate patch's first sample sits in the tile.
/// @return the key.
[[nodiscard]] constexpr CandidateKey candidateKey(float distance, std::uint32_t position) noexcept
{
    return (static_cast<CandidateKey>(std::bit_cast<std::uint32_t>(distance)) << 32u) | position;
}

/// The distance a key carries.
///
/// @param key the key.
/// @return the distance, exactly as it was keyed.
[[nodiscard]] constexpr float candidateDistance(CandidateKey key) noexcept
{
    return std::bit_cast<float>(static_cast<std::uint32_t>(key >> 32u));
}

/// The position a key carries.
///
/// @param key the key.
/// @return where the candidate patch's first sample sits in the tile.
[[nodiscard]] constexpr std::uint32_t candidatePosition(CandidateKey key) noexcept
{
    return static_cast<std::uint32_t>(key);
}

/// The scratch one worker needs to estimate one tile.
///
/// Spans rather than owners: the whole block is one allocation the run made, and this only says
/// where each part of it starts.
struct PassScratch
{
    /// The bordered tile read out of the noisy frame.
    WorkingFrame noisyTile{};

    /// The bordered tile read out of the guide, on the second pass only.
    WorkingFrame guideTile{};

    /// Where the aggregated estimate accumulates.
    WorkingFrame estimate{};

    /// How much weight each sample of the estimate has accumulated.
    std::span<float> weight{};

    /// Which reference positions have not been covered by an accepted group yet.
    std::span<std::uint8_t> mask{};

    /// The group of similar patches, component-major: component `j` of patch `i` sits at
    /// `j * patches + i`, and the components run channel, then row, then column - so one channel is
    /// always one contiguous run whichever pass built it. On the second pass it is read from the
    /// basic estimate, and on both it is overwritten by the estimate.
    std::span<float> group{};

    /// The noisy group, on the second pass only.
    std::span<float> noisyGroup{};

    /// Where the similar patches were found.
    std::span<std::uint32_t> index{};

    /// The candidates the search ranked.
    std::span<CandidateKey> candidates{};

    /// The distances of one row of the search window, accumulated side by side.
    std::span<float> windowRow{};

    /// The reference patch, cached so the distance loop does not re-read it from the frame.
    std::span<float> reference{};

    /// The product of the inverted covariance and the group, on the first pass only; the second
    /// writes that product into `group`.
    std::span<float> product{};

    /// The group copied patch-major - component `j` of patch `i` at `i * components + j` - which is
    /// what the covariance is accumulated from.
    std::span<float> patchMajor{};

    /// The group's barycentre.
    std::span<float> barycentre{};

    /// The group's covariance, overwritten by its inverse.
    std::span<float> covariance{};
};

/// Rounds a byte count up to the alignment every part of a scratch block starts at.
///
/// @param bytes what a part needs.
/// @return what it occupies.
[[nodiscard]] constexpr std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) / kDefaultBufferAlignmentBytes * kDefaultBufferAlignmentBytes;
}

/// The sizes every part of one worker's scratch needs, in samples or elements.
struct ScratchShape
{
    /// How many samples one bordered tile of one channel holds.
    std::uint64_t tileSamples = 0u;

    /// How many samples one bordered tile of every channel holds.
    std::uint64_t tileFrame = 0u;

    /// How many samples one patch of one channel holds.
    std::uint64_t patchSamples = 0u;

    /// How many components a patch of every channel has, which is the covariance's side.
    std::uint64_t components = 0u;

    /// How many search positions one row of the search window holds.
    std::uint64_t windowSide = 0u;

    /// How many search positions one reference has.
    std::uint64_t windowPositions = 0u;

    /// The largest number of patches a group can hold.
    std::uint64_t groupPatches = 0u;

    /// How many samples the largest group holds.
    std::uint64_t groupSamples = 0u;
};

/// What one pass's scratch has to be able to hold.
///
/// @param parameters the pass's tuning.
/// @param secondPass whether this is the final estimate.
/// @param tileWidth the bordered tile width.
/// @param tileHeight the bordered tile height.
/// @param channels how many channels the frame carries.
/// @return the shape.
[[nodiscard]] ScratchShape scratchShape(const PatchDenoisePassParameters& parameters, bool secondPass,
                                        std::uint32_t tileWidth, std::uint32_t tileHeight,
                                        std::uint32_t channels) noexcept
{
    ScratchShape shape;
    shape.tileSamples = static_cast<std::uint64_t>(tileWidth) * tileHeight;
    shape.tileFrame = shape.tileSamples * channels;
    shape.patchSamples = static_cast<std::uint64_t>(parameters.patchSize) * parameters.patchSize;
    shape.components = secondPass ? shape.patchSamples * channels : shape.patchSamples;
    shape.windowSide = parameters.searchWindow;
    shape.windowPositions = static_cast<std::uint64_t>(parameters.searchWindow) * parameters.searchWindow;
    // The first pass keeps a fixed count of the closest patches; the second keeps everything below a
    // distance threshold, which in the worst case is the whole search window.
    shape.groupPatches = secondPass ? shape.windowPositions : parameters.similarPatches;
    shape.groupSamples = shape.groupPatches * shape.patchSamples * channels;
    return shape;
}

/// How many bytes one worker's scratch needs for one pass.
///
/// @param shape what the pass has to hold.
/// @param secondPass whether this is the final estimate.
/// @return the byte count.
[[nodiscard]] std::uint64_t passScratchBytes(const ScratchShape& shape, bool secondPass) noexcept
{
    std::uint64_t bytes = 0u;
    bytes += alignedBytes(shape.tileFrame * sizeof(float));
    bytes += secondPass ? alignedBytes(shape.tileFrame * sizeof(float)) : 0u;
    bytes += alignedBytes(shape.tileFrame * sizeof(float));
    bytes += alignedBytes(shape.tileFrame * sizeof(float));
    bytes += alignedBytes(shape.tileSamples * sizeof(std::uint8_t));
    bytes += alignedBytes(shape.groupSamples * sizeof(float));
    bytes += secondPass ? alignedBytes(shape.groupSamples * sizeof(float)) : 0u;
    bytes += alignedBytes(shape.groupPatches * sizeof(std::uint32_t));
    bytes += alignedBytes(shape.windowPositions * sizeof(CandidateKey));
    bytes += alignedBytes(shape.windowSide * sizeof(float));
    bytes += alignedBytes(shape.patchSamples * sizeof(float));
    bytes += secondPass ? 0u : alignedBytes(shape.groupSamples * sizeof(float));
    bytes += alignedBytes(shape.groupSamples * sizeof(float));
    bytes += alignedBytes(shape.components * sizeof(float));
    bytes += alignedBytes(shape.components * shape.components * sizeof(float));
    return bytes;
}

/// Carves one worker's scratch out of its block.
///
/// @param block the worker's own bytes, at least `passScratchBytes`.
/// @param shape what the pass has to hold.
/// @param secondPass whether this is the final estimate.
/// @param tileWidth the bordered tile width.
/// @param tileHeight the bordered tile height.
/// @param channels how many channels the frame carries.
/// @return the spans.
[[nodiscard]] PassScratch carveScratch(std::uint8_t* block, const ScratchShape& shape, bool secondPass,
                                       std::uint32_t tileWidth, std::uint32_t tileHeight,
                                       std::uint32_t channels) noexcept
{
    std::uint8_t* cursor = block;
    const auto take = [&cursor](std::uint64_t bytes) noexcept {
        std::uint8_t* const taken = cursor;
        cursor += alignedBytes(bytes);
        return taken;
    };
    const auto floats = [&take](std::uint64_t count) noexcept {
        return std::span<float>{reinterpret_cast<float*>(take(count * sizeof(float))), static_cast<std::size_t>(count)};
    };

    PassScratch scratch;
    scratch.noisyTile = WorkingFrame{
        .base = floats(shape.tileFrame).data(), .width = tileWidth, .height = tileHeight, .channels = channels};
    if (secondPass)
    {
        scratch.guideTile = WorkingFrame{
            .base = floats(shape.tileFrame).data(), .width = tileWidth, .height = tileHeight, .channels = channels};
    }
    scratch.estimate = WorkingFrame{
        .base = floats(shape.tileFrame).data(), .width = tileWidth, .height = tileHeight, .channels = channels};
    scratch.weight = floats(shape.tileFrame);
    scratch.mask = std::span<std::uint8_t>{take(shape.tileSamples * sizeof(std::uint8_t)),
                                           static_cast<std::size_t>(shape.tileSamples)};
    scratch.group = floats(shape.groupSamples);
    if (secondPass)
    {
        scratch.noisyGroup = floats(shape.groupSamples);
    }
    scratch.index =
        std::span<std::uint32_t>{reinterpret_cast<std::uint32_t*>(take(shape.groupPatches * sizeof(std::uint32_t))),
                                 static_cast<std::size_t>(shape.groupPatches)};
    scratch.candidates =
        std::span<CandidateKey>{reinterpret_cast<CandidateKey*>(take(shape.windowPositions * sizeof(CandidateKey))),
                                static_cast<std::size_t>(shape.windowPositions)};
    scratch.windowRow = floats(shape.windowSide);
    scratch.reference = floats(shape.patchSamples);
    if (!secondPass)
    {
        scratch.product = floats(shape.groupSamples);
    }
    scratch.patchMajor = floats(shape.groupSamples);
    scratch.barycentre = floats(shape.components);
    scratch.covariance = floats(shape.components * shape.components);
    return scratch;
}

/// Subtracts the barycentre of a group from every one of its patches.
///
/// @param group the group, changed in place.
/// @param barycentre where the mean of each component goes.
/// @param patches how many patches the group holds.
/// @param components how many components each patch has.
void centreGroup(std::span<float> group, std::span<float> barycentre, std::uint32_t patches,
                 std::uint64_t components) noexcept
{
    const float inverse = 1.0f / static_cast<float>(patches);
    for (std::uint64_t component = 0u; component < components; ++component)
    {
        float* const row = group.data() + component * patches;
        float sum = 0.0f;
        for (std::uint32_t patch = 0u; patch < patches; ++patch)
        {
            sum += row[patch];
        }
        const float mean = sum * inverse;
        barycentre[static_cast<std::size_t>(component)] = mean;
        for (std::uint32_t patch = 0u; patch < patches; ++patch)
        {
            row[patch] -= mean;
        }
    }
}

/// The covariance of a centred group.
///
/// Each entry is the products of its two components summed over the patches in patch order, from
/// zero, and then scaled by the patch count's reciprocal. The group is first copied patch-major, so
/// that one patch's components are one contiguous run: every entry of the lower triangle then takes
/// that patch's product at once, side by side with the others, and each still sums its own terms in
/// patch order.
///
/// @param group the centred group.
/// @param patchMajor scratch for the patch-major copy, of the group's size.
/// @param covariance where the symmetric matrix goes.
/// @param patches how many patches the group holds.
/// @param components how many components each patch has.
void groupCovariance(std::span<const float> group, std::span<float> patchMajor, std::span<float> covariance,
                     std::uint32_t patches, std::uint64_t components) noexcept
{
    for (std::uint64_t component = 0u; component < components; ++component)
    {
        const float* const run = group.data() + component * patches;
        for (std::uint32_t patch = 0u; patch < patches; ++patch)
        {
            patchMajor[static_cast<std::size_t>(patch * components + component)] = run[patch];
        }
    }
    std::fill_n(covariance.data(), components * components, 0.0f);
    for (std::uint32_t patch = 0u; patch < patches; ++patch)
    {
        const float* const sample = patchMajor.data() + patch * components;
        for (std::uint64_t i = 0u; i < components; ++i)
        {
            const float left = sample[i];
            float* const row = covariance.data() + i * components;
            for (std::uint64_t j = 0u; j <= i; ++j)
            {
                row[j] += left * sample[j];
            }
        }
    }
    const float normalization = 1.0f / static_cast<float>(patches);
    for (std::uint64_t i = 0u; i < components; ++i)
    {
        for (std::uint64_t j = 0u; j <= i; ++j)
        {
            const float scaled = covariance[static_cast<std::size_t>(i * components + j)] * normalization;
            covariance[static_cast<std::size_t>(i * components + j)] = scaled;
            covariance[static_cast<std::size_t>(i + j * components)] = scaled;
        }
    }
}

/// Transposes a square matrix in place.
///
/// @param matrix the matrix.
/// @param order its side.
void transposeSquare(std::span<float> matrix, std::uint64_t order) noexcept
{
    for (std::uint64_t i = 0u; i + 1u < order; ++i)
    {
        std::uint64_t p = i * (order + 1u) + 1u;
        std::uint64_t q = i * (order + 1u) + order;
        for (std::uint64_t j = 0u; j + 1u + i < order; ++j, ++p, q += order)
        {
            const float held = matrix[static_cast<std::size_t>(p)];
            matrix[static_cast<std::size_t>(p)] = matrix[static_cast<std::size_t>(q)];
            matrix[static_cast<std::size_t>(q)] = held;
        }
    }
}

/// Inverts a symmetric positive-definite matrix in place, by Cholesky factorisation.
///
/// The published method's own inverse, reproduced exactly including its in-place factorisation and
/// its early exit. A matrix that is not positive definite leaves the buffer partly factorised, which
/// is why the caller must treat a refusal as "this group keeps its noisy values" rather than reading
/// the buffer.
///
/// @param matrix the matrix, replaced by its inverse on success.
/// @param order its side.
/// @return true when the matrix was positive definite.
[[nodiscard]] bool invertSymmetric(std::span<float> matrix, std::uint64_t order) noexcept
{
    float* const values = matrix.data();
    for (std::uint64_t j = 0u, p = 0u; j < order; ++j, p += order + 1u)
    {
        for (std::uint64_t q = j * order; q < p; ++q)
        {
            values[p] -= values[q] * values[q];
        }
        if (!(values[p] > 0.0f))
        {
            return false;
        }
        values[p] = std::sqrt(values[p]);
        for (std::uint64_t k = j + 1u, q = p + order; k < order; ++k, q += order)
        {
            float sum = 0.0f;
            for (std::uint64_t r = j * order, s = k * order; r < p; ++r, ++s)
            {
                sum += values[r] * values[s];
            }
            values[q] -= sum;
            values[q] /= values[p];
        }
    }

    transposeSquare(matrix, order);

    for (std::uint64_t j = 0u, p = 0u; j < order; ++j, p += order + 1u)
    {
        values[p] = 1.0f / values[p];
        for (std::uint64_t q = j, t = 0u; q < p; t += order + 1u, q += order)
        {
            float sum = 0.0f;
            for (std::uint64_t s = q, r = t; s < p; s += order, ++r)
            {
                sum -= values[s] * values[r];
            }
            values[q] = sum * values[p];
        }
    }

    for (std::uint64_t j = 0u, p = 0u; j < order; ++j, p += order + 1u)
    {
        for (std::uint64_t q = j, t = p - j; q <= p; q += order, ++t)
        {
            float sum = 0.0f;
            for (std::uint64_t k = j, r = p, s = q; k < order; ++k, ++r, ++s)
            {
                sum += values[r] * values[s];
            }
            values[t] = sum;
            values[q] = sum;
        }
    }
    return true;
}

/// Multiplies a square matrix by a component-major group.
///
/// Each product sample is its matrix row's products with the patch's components summed in column
/// order, from zero. The patches are walked innermost, so every sample of a product row takes one
/// column's term at once, side by side, reading the group along its rows rather than down them, and
/// each still sums its own terms in column order.
///
/// @param product where the result goes, of the group's shape. Must share no memory with `group`.
/// @param matrix the square matrix.
/// @param group the group.
/// @param order the matrix side, which is the component count.
/// @param patches how many patches the group holds.
void multiplyGroup(std::span<float> product, std::span<const float> matrix, std::span<const float> group,
                   std::uint64_t order, std::uint32_t patches) noexcept
{
    for (std::uint64_t row = 0u; row < order; ++row)
    {
        const float* const matrixRow = matrix.data() + row * order;
        float* const target = product.data() + row * patches;
        std::fill_n(target, patches, 0.0f);
        for (std::uint64_t column = 0u; column < order; ++column)
        {
            const float coefficient = matrixRow[column];
            const float* const source = group.data() + column * patches;
            for (std::uint32_t patch = 0u; patch < patches; ++patch)
            {
                target[patch] += coefficient * source[patch];
            }
        }
    }
}

/// The Bessel-corrected variance of one contiguous run of a group.
///
/// @param run the samples.
/// @return the variance, or zero for a run of one sample.
[[nodiscard]] float runVariance(std::span<const float> run) noexcept
{
    if (run.size() < 2u)
    {
        return 0.0f;
    }
    float mean = 0.0f;
    float squares = 0.0f;
    for (const float value : run)
    {
        mean += value;
        squares += value * value;
    }
    const auto population = static_cast<float>(run.size());
    return (squares - mean * mean / population) / (population - 1.0f);
}

/// Collects the closest patches to a reference one, on the first pass.
///
/// @param tile the noisy tile.
/// @param scratch the worker's buffers.
/// @param parameters the pass's tuning.
/// @param reference where the reference patch's first sample sits.
void collectFirstPassGroup(const WorkingFrame& tile, PassScratch& scratch, const PatchDenoisePassParameters& parameters,
                           std::uint32_t reference) noexcept
{
    const std::uint32_t window = parameters.searchWindow;
    const std::uint32_t patchSize = parameters.patchSize;
    const std::uint32_t width = tile.width;
    const std::uint64_t planeSamples = tile.planeSamples();
    const std::uint32_t similar = parameters.similarPatches;
    const std::uint32_t origin = reference - (window - 1u) / 2u * (width + 1u);

    for (std::uint32_t row = 0u; row < patchSize; ++row)
    {
        for (std::uint32_t column = 0u; column < patchSize; ++column)
        {
            scratch.reference[static_cast<std::size_t>(row) * patchSize + column] =
                tile.base[reference + static_cast<std::uint64_t>(row) * width + column];
        }
    }

    // A candidate's distance sums its squared differences over the patch's rows and then its columns,
    // from zero. The candidates of one window row are independent sums, so they advance side by side,
    // one term at a time, and each still takes its terms in that order.
    float* const distances = scratch.windowRow.data();
    for (std::uint32_t i = 0u; i < window; ++i)
    {
        const std::uint32_t rowStart = i * width + origin;
        std::fill_n(distances, window, 0.0f);
        for (std::uint32_t row = 0u; row < patchSize; ++row)
        {
            const float* const referenceRow = scratch.reference.data() + static_cast<std::size_t>(row) * patchSize;
            const float* const line = tile.base + rowStart + static_cast<std::uint64_t>(row) * width;
            for (std::uint32_t column = 0u; column < patchSize; ++column)
            {
                const float value = referenceRow[column];
                const float* const candidates = line + column;
                for (std::uint32_t j = 0u; j < window; ++j)
                {
                    const float difference = value - candidates[j];
                    distances[j] += difference * difference;
                }
            }
        }
        for (std::uint32_t j = 0u; j < window; ++j)
        {
            scratch.candidates[static_cast<std::size_t>(i) * window + j] = candidateKey(distances[j], rowStart + j);
        }
    }

    CandidateKey* const first = scratch.candidates.data();
    CandidateKey* const last = first + scratch.candidates.size();
    std::nth_element(first, first + (similar - 1u), last);
    std::sort(first, first + similar);
    for (std::uint32_t n = 0u; n < similar; ++n)
    {
        scratch.index[n] = candidatePosition(first[n]);
    }

    std::uint64_t component = 0u;
    for (std::uint32_t channel = 0u; channel < tile.channels; ++channel)
    {
        const std::uint64_t plane = static_cast<std::uint64_t>(channel) * planeSamples;
        for (std::uint32_t row = 0u; row < patchSize; ++row)
        {
            for (std::uint32_t column = 0u; column < patchSize; ++column, ++component)
            {
                float* const target = scratch.group.data() + component * similar;
                const std::uint64_t offset = static_cast<std::uint64_t>(row) * width + column;
                for (std::uint32_t n = 0u; n < similar; ++n)
                {
                    target[n] = tile.base[plane + scratch.index[n] + offset];
                }
            }
        }
    }
}

/// Collects the patches similar to a reference one, on the second pass.
///
/// @param noisy the noisy tile.
/// @param guide the basic estimate's tile, on which the distances are measured.
/// @param scratch the worker's buffers.
/// @param parameters the pass's tuning.
/// @param reference where the reference patch's first sample sits.
/// @return how many patches were kept, which is at least one.
[[nodiscard]] std::uint32_t collectSecondPassGroup(const WorkingFrame& noisy, const WorkingFrame& guide,
                                                   PassScratch& scratch, const PatchDenoisePassParameters& parameters,
                                                   std::uint32_t reference) noexcept
{
    const std::uint32_t window = parameters.searchWindow;
    const std::uint32_t patchSize = parameters.patchSize;
    const std::uint32_t width = noisy.width;
    const std::uint32_t channels = noisy.channels;
    const std::uint64_t planeSamples = noisy.planeSamples();
    const std::uint32_t origin = reference - (window - 1u) / 2u * (width + 1u);

    // A candidate's distance sums over the channels, then the patch's rows, then its columns, from
    // zero; the candidates of one window row advance side by side, as on the first pass.
    float* const distances = scratch.windowRow.data();
    for (std::uint32_t i = 0u; i < window; ++i)
    {
        const std::uint32_t rowStart = i * width + origin;
        std::fill_n(distances, window, 0.0f);
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const std::uint64_t plane = static_cast<std::uint64_t>(channel) * planeSamples;
            for (std::uint32_t row = 0u; row < patchSize; ++row)
            {
                const float* const referenceRow =
                    guide.base + plane + reference + static_cast<std::uint64_t>(row) * width;
                const float* const line = guide.base + plane + rowStart + static_cast<std::uint64_t>(row) * width;
                for (std::uint32_t column = 0u; column < patchSize; ++column)
                {
                    const float value = referenceRow[column];
                    const float* const candidates = line + column;
                    for (std::uint32_t j = 0u; j < window; ++j)
                    {
                        const float difference = value - candidates[j];
                        distances[j] += difference * difference;
                    }
                }
            }
        }
        for (std::uint32_t j = 0u; j < window; ++j)
        {
            scratch.candidates[static_cast<std::size_t>(i) * window + j] = candidateKey(distances[j], rowStart + j);
        }
    }

    CandidateKey* const first = scratch.candidates.data();
    std::sort(first, first + scratch.candidates.size());

    const float threshold =
        (std::max)(parameters.similarityDistance, candidateDistance(first[parameters.similarPatches - 1u]));
    std::uint32_t kept = 0u;
    for (std::size_t n = 0u; n < scratch.candidates.size(); ++n)
    {
        if (candidateDistance(first[n]) < threshold)
        {
            scratch.index[kept] = candidatePosition(first[n]);
            ++kept;
        }
    }
    if (kept == 0u)
    {
        scratch.index[0] = candidatePosition(first[0]);
        kept = 1u;
    }

    std::uint64_t component = 0u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::uint64_t plane = static_cast<std::uint64_t>(channel) * planeSamples;
        for (std::uint32_t row = 0u; row < patchSize; ++row)
        {
            for (std::uint32_t column = 0u; column < patchSize; ++column, ++component)
            {
                const std::uint64_t offset = static_cast<std::uint64_t>(row) * width + column;
                float* const noisyTarget = scratch.noisyGroup.data() + component * kept;
                float* const guideTarget = scratch.group.data() + component * kept;
                for (std::uint32_t n = 0u; n < kept; ++n)
                {
                    noisyTarget[n] = noisy.base[plane + scratch.index[n] + offset];
                    guideTarget[n] = guide.base[plane + scratch.index[n] + offset];
                }
            }
        }
    }
    return kept;
}

/// Aggregates one estimated group back into the tile, and marks its positions as covered.
///
/// The paste trick is the published method's: a position that has been written, and its four
/// immediate neighbours, are not used as references again.
///
/// @param scratch the worker's buffers.
/// @param parameters the pass's tuning.
/// @param patches how many patches the group holds.
void aggregateGroup(PassScratch& scratch, const PatchDenoisePassParameters& parameters, std::uint32_t patches) noexcept
{
    const WorkingFrame& estimate = scratch.estimate;
    const std::uint32_t width = estimate.width;
    const std::uint32_t patchSize = parameters.patchSize;
    const std::uint64_t planeSamples = estimate.planeSamples();
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(patchSize) * patchSize;

    for (std::uint32_t n = 0u; n < patches; ++n)
    {
        const std::uint32_t position = scratch.index[n];
        for (std::uint32_t channel = 0u; channel < estimate.channels; ++channel)
        {
            const std::uint64_t plane = static_cast<std::uint64_t>(channel) * planeSamples;
            const std::uint64_t base = static_cast<std::uint64_t>(channel) * patchSamples;
            for (std::uint32_t row = 0u; row < patchSize; ++row)
            {
                for (std::uint32_t column = 0u; column < patchSize; ++column)
                {
                    const std::uint64_t component = base + static_cast<std::uint64_t>(row) * patchSize + column;
                    const std::uint64_t target = plane + position + static_cast<std::uint64_t>(row) * width + column;
                    estimate.base[target] += scratch.group[static_cast<std::size_t>(component * patches + n)];
                    scratch.weight[static_cast<std::size_t>(target)] += 1.0f;
                }
            }
        }
        scratch.mask[position] = 0u;
        scratch.mask[position - width] = 0u;
        scratch.mask[position + width] = 0u;
        scratch.mask[position - 1u] = 0u;
        scratch.mask[position + 1u] = 0u;
    }
}

/// Runs one estimator pass over one tile.
///
/// References are every `referenceStep`-th position in raster order, as the published walk takes
/// them, among those the mask still marks; the mask starts by excluding a margin of one search
/// window at every edge of the tile. With `beta` the covariance weight and `sigma` the tuned noise
/// level, the first pass replaces each centred noisy patch `P` by
/// `P - beta * sigma^2 * inverse(C) * P`, with `C` the covariance of its own channel's group; the
/// second replaces it by `P - beta * sigma^2 * inverse(C + beta * sigma^2 * I) * P`, with `C` the
/// covariance of the basic group over every channel. Both add the noisy barycentre back. A
/// covariance that is not positive definite leaves that group's noisy values in place and is
/// counted.
///
/// @param scratch the worker's buffers, whose tiles have already been read.
/// @param parameters the pass's tuning.
/// @param secondPass whether this is the final estimate.
/// @param tunedSigma the noise level on the estimator's own scale.
/// @param counters what the tile counted, added to whatever they already hold.
void estimateTile(PassScratch& scratch, const PatchDenoisePassParameters& parameters, bool secondPass, float tunedSigma,
                  EstimatorCounters& counters) noexcept
{
    const WorkingFrame& tile = scratch.estimate;
    const std::uint32_t width = tile.width;
    const std::uint32_t height = tile.height;
    const std::uint32_t channels = tile.channels;
    const std::uint32_t window = parameters.searchWindow;
    const std::uint32_t patchSize = parameters.patchSize;
    const std::uint64_t patchSamples = static_cast<std::uint64_t>(patchSize) * patchSize;
    const std::uint64_t components = secondPass ? patchSamples * channels : patchSamples;
    const float diagonal = parameters.covarianceWeight * tunedSigma * tunedSigma;
    const float threshold =
        tunedSigma * tunedSigma * parameters.homogeneityGamma * (secondPass ? 1.0f : static_cast<float>(channels));

    std::fill(scratch.estimate.base, scratch.estimate.base + scratch.estimate.sampleCount(), 0.0f);
    std::fill(scratch.weight.begin(), scratch.weight.end(), 0.0f);
    std::fill(scratch.mask.begin(), scratch.mask.end(), static_cast<std::uint8_t>(0));
    for (std::uint32_t y = window; y + window < height; ++y)
    {
        for (std::uint32_t x = window; x + window < width; ++x)
        {
            scratch.mask[static_cast<std::size_t>(y) * width + x] = 1u;
        }
    }

    const auto planeSamples = static_cast<std::uint32_t>(tile.planeSamples());
    for (std::uint32_t reference = 0u; reference < planeSamples; reference += parameters.referenceStep)
    {
        if (scratch.mask[reference] == 0u)
        {
            continue;
        }
        std::uint32_t patches = parameters.similarPatches;
        if (secondPass)
        {
            patches = collectSecondPassGroup(scratch.noisyTile, scratch.guideTile, scratch, parameters, reference);
        }
        else
        {
            collectFirstPassGroup(scratch.noisyTile, scratch, parameters, reference);
        }
        ++counters.groups;

        // The spread the featureless shortcut compares is measured on the samples the estimate would
        // be built from - the noisy group on the second pass, the group itself on the first - and one
        // channel is one contiguous run of the group whichever pass built it. The published method
        // indexes that measure as if the group were patch-major, which it is not, so on the second
        // pass its partitions are strided subsets rather than channels; the correct partition is
        // used here, so the two can disagree about which second-pass groups are featureless.
        bool estimated = false;
        if (parameters.homogeneousArea)
        {
            const std::span<const float> measured =
                secondPass ? std::span<const float>{scratch.noisyGroup} : std::span<const float>{scratch.group};
            const auto channelRun = static_cast<std::size_t>(patchSamples * patches);
            float spread = 0.0f;
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                spread += runVariance(measured.subspan(static_cast<std::size_t>(channel) * channelRun, channelRun));
            }
            if (secondPass)
            {
                spread /= static_cast<float>(channels);
            }
            if (spread < threshold)
            {
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const std::span<float> run =
                        scratch.group.subspan(static_cast<std::size_t>(channel) * channelRun, channelRun);
                    float mean = 0.0f;
                    for (const float value : run)
                    {
                        mean += value;
                    }
                    mean /= static_cast<float>(run.size());
                    std::fill(run.begin(), run.end(), mean);
                }
                ++counters.homogeneousGroups;
                estimated = true;
            }
        }

        if (!estimated && secondPass)
        {
            centreGroup(scratch.group, scratch.barycentre, patches, components);
            centreGroup(scratch.noisyGroup, scratch.barycentre, patches, components);
            groupCovariance(scratch.group, scratch.patchMajor, scratch.covariance, patches, components);
            for (std::uint64_t k = 0u; k < components; ++k)
            {
                scratch.covariance[static_cast<std::size_t>(k * components + k)] += diagonal;
            }
            const std::uint64_t count = components * patches;
            if (invertSymmetric(scratch.covariance, components))
            {
                multiplyGroup(scratch.group, scratch.covariance, scratch.noisyGroup, components, patches);
                for (std::uint64_t k = 0u; k < count; ++k)
                {
                    scratch.group[static_cast<std::size_t>(k)] = scratch.noisyGroup[static_cast<std::size_t>(k)] -
                                                                 diagonal * scratch.group[static_cast<std::size_t>(k)];
                }
            }
            else
            {
                ++counters.inversionFailures;
                for (std::uint64_t k = 0u; k < count; ++k)
                {
                    scratch.group[static_cast<std::size_t>(k)] = scratch.noisyGroup[static_cast<std::size_t>(k)];
                }
            }
            for (std::uint64_t component = 0u, k = 0u; component < components; ++component)
            {
                for (std::uint32_t patch = 0u; patch < patches; ++patch, ++k)
                {
                    scratch.group[static_cast<std::size_t>(k)] +=
                        scratch.barycentre[static_cast<std::size_t>(component)];
                }
            }
        }
        else if (!estimated)
        {
            const auto channelRun = static_cast<std::size_t>(patchSamples * patches);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const std::span<float> group =
                    scratch.group.subspan(static_cast<std::size_t>(channel) * channelRun, channelRun);
                centreGroup(group, scratch.barycentre, patches, components);
                groupCovariance(group, scratch.patchMajor, scratch.covariance, patches, components);
                if (invertSymmetric(scratch.covariance, components))
                {
                    multiplyGroup(scratch.product, scratch.covariance, group, components, patches);
                    for (std::size_t k = 0u; k < channelRun; ++k)
                    {
                        group[k] -= diagonal * scratch.product[k];
                    }
                }
                else
                {
                    ++counters.inversionFailures;
                }
                for (std::uint64_t component = 0u, k = 0u; component < components; ++component)
                {
                    for (std::uint32_t patch = 0u; patch < patches; ++patch, ++k)
                    {
                        group[static_cast<std::size_t>(k)] += scratch.barycentre[static_cast<std::size_t>(component)];
                    }
                }
            }
        }

        aggregateGroup(scratch, parameters, patches);
    }
}

/// Divides the aggregated estimate by its weight, falling back where nothing was aggregated.
///
/// Every aggregated sample has weight one per patch that covered it, so the division is a plain
/// average of the group estimates that reached it.
///
/// @param scratch the worker's buffers.
/// @param secondPass whether the fallback is the guide rather than the noisy tile.
void resolveTile(PassScratch& scratch, bool secondPass) noexcept
{
    const WorkingFrame& estimate = scratch.estimate;
    const WorkingFrame& fallback = secondPass ? scratch.guideTile : scratch.noisyTile;
    const std::uint64_t count = estimate.sampleCount();
    for (std::uint64_t i = 0u; i < count; ++i)
    {
        const float weight = scratch.weight[static_cast<std::size_t>(i)];
        estimate.base[i] = weight > 0.0f ? estimate.base[i] / weight : fallback.base[i];
    }
}

/// Runs one estimator pass over a whole frame.
///
/// Each tile is read with its boundary reflected from the frames, estimated, and only its interior
/// written, so a tile's answer does not depend on which worker ran it or when. Reports progress as
/// tiles finish, in a total of the pass's tiles.
///
/// @param execution the operation policy and engine-context lease.
/// @param noisy the frame the estimate is built from.
/// @param guide the basic estimate, on the second pass only.
/// @param output where the pass's answer goes. Must share no memory with `noisy` or `guide`,
///        because one tile's interior is written while another still reads its boundary.
/// @param parameters the pass's tuning.
/// @param secondPass whether this is the final estimate.
/// @param tunedSigma the noise level on the estimator's own scale.
/// @param counters what the pass counted, added to whatever they already hold; the first pass also
///        sets the tile count and both raise the working bytes to their own peak.
/// @param operation the name carried by any failure.
/// @return success, or an execution, cancellation or memory failure.
[[nodiscard]] Status runPass(const ExecutionContext& execution, const WorkingFrame& noisy, const WorkingFrame& guide,
                             const WorkingFrame& output, const PatchDenoisePassParameters& parameters, bool secondPass,
                             float tunedSigma, EstimatorCounters& counters, const char* operation)
{
    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    PatchDenoisePlan::tileGrid(noisy.width, noisy.height, parameters.boundary, tilesX, tilesY);
    const std::uint32_t tileCount = tilesX * tilesY;
    if (!secondPass)
    {
        counters.tileCount = tileCount;
    }

    // Every tile gets the buffers the largest one needs, and a smaller tile simply uses less of
    // them, so one scratch size serves the pass.
    const std::uint32_t widestInterior = (noisy.width + tilesX - 1u) / tilesX;
    const std::uint32_t tallestInterior = (noisy.height + tilesY - 1u) / tilesY;
    const std::uint32_t tileWidth = widestInterior + 2u * parameters.boundary;
    const std::uint32_t tileHeight = tallestInterior + 2u * parameters.boundary;
    const ScratchShape shape = scratchShape(parameters, secondPass, tileWidth, tileHeight, noisy.channels);

    const TaskCost cost{.bytesPerItem = shape.tileFrame * sizeof(float),
                        .operationsPerItem = shape.tileSamples * shape.windowPositions};
    const std::uint32_t workers = (std::max)(plannedWorkerCount(execution, tileCount, cost), 1u);
    const std::uint64_t perWorker = passScratchBytes(shape, secondPass);
    Result<plane_detail::ScopedContextMemory> taken = plane_detail::ScopedContextMemory::take(
        execution, perWorker * workers, kDefaultBufferAlignmentBytes, operation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    const plane_detail::ScopedContextMemory memory = std::move(taken).value();

    // One counter set per worker, merged in slot order afterwards, so the totals do not depend on
    // which worker finished first.
    Result<plane_detail::ScopedContextMemory> countersTaken = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(workers) * sizeof(EstimatorCounters), kDefaultBufferAlignmentBytes,
        operation);
    if (!countersTaken.hasValue())
    {
        return countersTaken.status();
    }
    const plane_detail::ScopedContextMemory workerCounters = std::move(countersTaken).value();
    counters.workingBytes = (std::max)(counters.workingBytes, memory.bytes() + workerCounters.bytes());
    auto* const perWorkerCounters = static_cast<EstimatorCounters*>(workerCounters.data());
    for (std::uint32_t worker = 0u; worker < workers; ++worker)
    {
        perWorkerCounters[worker] = EstimatorCounters{};
    }

    // Tiles finish on whichever worker ran them, so the count is shared, and a report that arrives after
    // a larger one is dropped by the execution rather than moving the display backwards.
    const char* const stage = secondPass ? kFinalPassStage : kBasicPassStage;
    std::atomic<std::uint64_t> tilesFinished{0u};
    const Status walked = parallelForWorkers(execution, tileCount, cost, [&](WorkerTask task) -> Status {
        std::uint8_t* const block =
            static_cast<std::uint8_t*>(memory.data()) + static_cast<std::uint64_t>(task.workerSlot) * perWorker;
        const PassScratch worker = carveScratch(block, shape, secondPass, tileWidth, tileHeight, noisy.channels);
        for (std::uint64_t item = task.range.begin; item < task.range.end; ++item)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
            const FrameTile tile = tileAt(noisy.width, noisy.height, tilesX, tilesY, static_cast<std::uint32_t>(item));
            const std::uint32_t thisWidth = tile.interiorWidth + 2u * parameters.boundary;
            const std::uint32_t thisHeight = tile.interiorHeight + 2u * parameters.boundary;
            const auto thisFrame = static_cast<std::size_t>(thisWidth) * thisHeight * noisy.channels;

            PassScratch local = worker;
            local.noisyTile.width = thisWidth;
            local.noisyTile.height = thisHeight;
            local.guideTile.width = thisWidth;
            local.guideTile.height = thisHeight;
            local.estimate.width = thisWidth;
            local.estimate.height = thisHeight;
            local.weight = worker.weight.first(thisFrame);
            local.mask = worker.mask.first(static_cast<std::size_t>(thisWidth) * thisHeight);

            readTile(noisy, tile, parameters.boundary, local.noisyTile);
            if (secondPass)
            {
                readTile(guide, tile, parameters.boundary, local.guideTile);
            }
            estimateTile(local, parameters, secondPass, tunedSigma, perWorkerCounters[task.workerSlot]);
            resolveTile(local, secondPass);
            writeTileInterior(local.estimate, tile, parameters.boundary, output);
            execution.reportProgress(tilesFinished.fetch_add(1u, std::memory_order_relaxed) + 1u, tileCount, stage);
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    execution.reportProgress(tileCount, tileCount, stage);

    for (std::uint32_t worker = 0u; worker < workers; ++worker)
    {
        counters.groups += perWorkerCounters[worker].groups;
        counters.homogeneousGroups += perWorkerCounters[worker].homogeneousGroups;
        counters.inversionFailures += perWorkerCounters[worker].inversionFailures;
    }
    return Status::success();
}

/// Decorrelates or recorrelates a frame's colour channels in place.
///
/// The published method's own transforms for three and four channels, both orthonormal, so the
/// inverse is the transpose and the round trip is the identity to within rounding. A frame of any
/// other channel count is left alone; the plan admits only one of them, the monochrome frame.
///
/// @param frame the frame to transform.
/// @param forward true to decorrelate, false to put the colours back.
void transformColourSpace(const WorkingFrame& frame, bool forward) noexcept
{
    const std::uint64_t samples = frame.planeSamples();
    if (frame.channels == 3u)
    {
        const float a = 1.0f / std::sqrt(3.0f);
        const float b = 1.0f / std::sqrt(2.0f);
        float* const first = frame.plane(0u);
        float* const second = frame.plane(1u);
        float* const third = frame.plane(2u);
        if (forward)
        {
            const float c = 2.0f * a * std::sqrt(2.0f);
            for (std::uint64_t i = 0u; i < samples; ++i)
            {
                const float red = first[i];
                const float green = second[i];
                const float blue = third[i];
                first[i] = a * (red + green + blue);
                second[i] = b * (red - blue);
                third[i] = c * (0.25f * red - 0.5f * green + 0.25f * blue);
            }
        }
        else
        {
            const float c = a / b;
            for (std::uint64_t i = 0u; i < samples; ++i)
            {
                const float y = first[i];
                const float u = second[i];
                const float v = third[i];
                first[i] = a * y + b * u + c * 0.5f * v;
                second[i] = a * y - c * v;
                third[i] = a * y - b * u + c * 0.5f * v;
            }
        }
        return;
    }
    if (frame.channels != 4u)
    {
        return;
    }
    const float a = 0.5f;
    const float b = 1.0f / std::sqrt(2.0f);
    float* const first = frame.plane(0u);
    float* const second = frame.plane(1u);
    float* const third = frame.plane(2u);
    float* const fourth = frame.plane(3u);
    for (std::uint64_t i = 0u; i < samples; ++i)
    {
        const float w = first[i];
        const float x = second[i];
        const float y = third[i];
        const float z = fourth[i];
        if (forward)
        {
            first[i] = a * (w + x + y + z);
            second[i] = b * (x - y);
            third[i] = a * (-w + x + y - z);
            fourth[i] = b * (-w + z);
        }
        else
        {
            first[i] = a * w - a * y - b * z;
            second[i] = a * w + b * x + a * y;
            third[i] = a * w - b * x + a * y;
            fourth[i] = a * w - a * y + b * z;
        }
    }
}

} // namespace

Status runPatchEstimator(const ExecutionContext& execution, const WorkingFrame& noisy, const WorkingFrame& basic,
                         const WorkingFrame& output, const EstimatorTuning& tuning, float tunedSigma,
                         EstimatorCounters& counters, const char* operation)
{
    // The first pass runs in the decorrelated colour space, where the channels of a colour frame
    // carry independent noise and a patch distance means what the method assumes it means. `output`
    // holds that decorrelated copy until the second pass overwrites it, which is what keeps the
    // operation to three frames rather than four.
    std::copy_n(noisy.base, noisy.sampleCount(), output.base);
    transformColourSpace(output, true);

    Status first = Status::success();
    {
        // The two passes are the two halves of the estimator's progress.
        const ProgressRange half(execution, 0u, 1u, 2u);
        first = runPass(execution, output, WorkingFrame{}, basic, tuning.first, false, tunedSigma, counters, operation);
    }
    if (first.failed())
    {
        return first;
    }

    transformColourSpace(basic, false);
    // A basic sample that is not finite would poison every distance the second pass measures. The
    // published implementation repairs a non-finite sample by copying its left neighbour, which
    // spreads whatever went wrong sideways; the noisy sample at the same place is the one value
    // that is certainly meaningful.
    const std::uint64_t samples = basic.sampleCount();
    for (std::uint64_t i = 0u; i < samples; ++i)
    {
        if (!std::isfinite(basic.base[i]))
        {
            basic.base[i] = noisy.base[i];
        }
    }

    const ProgressRange half(execution, 1u, 2u, 2u);
    return runPass(execution, noisy, basic, output, tuning.second, true, tunedSigma, counters, operation);
}

} // namespace an::patch_detail
