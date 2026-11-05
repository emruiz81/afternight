// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The patch estimator against a plain reading of the published method, sample for sample.
//
// The estimator takes a frame through two passes of patch groups, each group's covariance, its
// Cholesky inverse and a matrix product, and every one of those is a run of floating-point sums
// whose order decides the last bits. The engine is free to walk them in whatever order is fastest -
// independent sums side by side, a group copied patch-major, candidates ordered by one integer key -
// but not to change any one sum's order. So the oracle below takes every sum the way the method
// writes it, one after another from zero, and the operation has to agree with it bit for bit, on a
// frame whose structure keeps most groups off the featureless shortcut, on one that spans several
// tiles, on a colour frame, and with the shortcut switched off.

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

/// A channel-major stack of tightly packed planes.
struct Frame
{
    /// The samples, channel after channel.
    std::vector<float> samples;

    /// How many pixels one row holds.
    std::uint32_t width = 0u;

    /// How many rows.
    std::uint32_t height = 0u;

    /// How many channels.
    std::uint32_t channels = 0u;

    /// How many samples one channel holds.
    [[nodiscard]] std::size_t plane() const
    {
        return static_cast<std::size_t>(width) * height;
    }
};

/// A position reflected back inside an extent with the edge sample repeated: -1 reads 0, and the
/// extent reads its last sample.
[[nodiscard]] std::uint32_t reflect(std::int64_t position, std::uint32_t extent)
{
    if (extent <= 1u)
    {
        return 0u;
    }
    const auto last = static_cast<std::int64_t>(extent) - 1;
    while (position < 0 || position > last)
    {
        position = position < 0 ? -position - 1 : 2 * last - position + 1;
    }
    return static_cast<std::uint32_t>(position);
}

/// One candidate patch and how far it is from the reference.
struct Candidate
{
    /// The squared distance.
    float distance = 0.0f;

    /// Where its first sample sits in the tile.
    std::uint32_t position = 0u;
};

/// The method's order on candidates: closer first, and the earlier of two equally close ones.
[[nodiscard]] bool closer(const Candidate& left, const Candidate& right)
{
    return left.distance < right.distance || (left.distance == right.distance && left.position < right.position);
}

/// Subtracts each component's mean over the patches from it.
void centre(std::vector<float>& group, std::size_t offset, std::vector<float>& barycentre, std::uint32_t patches,
            std::size_t components)
{
    const float inverse = 1.0f / static_cast<float>(patches);
    for (std::size_t component = 0u; component < components; ++component)
    {
        float sum = 0.0f;
        for (std::uint32_t patch = 0u; patch < patches; ++patch)
        {
            sum += group[offset + component * patches + patch];
        }
        const float mean = sum * inverse;
        barycentre[component] = mean;
        for (std::uint32_t patch = 0u; patch < patches; ++patch)
        {
            group[offset + component * patches + patch] -= mean;
        }
    }
}

/// The covariance of a centred group, each entry a dot product over the patches in patch order.
[[nodiscard]] std::vector<float> covariance(const std::vector<float>& group, std::size_t offset, std::uint32_t patches,
                                            std::size_t components)
{
    std::vector<float> matrix(components * components, 0.0f);
    const float normalization = 1.0f / static_cast<float>(patches);
    for (std::size_t i = 0u; i < components; ++i)
    {
        for (std::size_t j = 0u; j <= i; ++j)
        {
            float value = 0.0f;
            for (std::uint32_t patch = 0u; patch < patches; ++patch)
            {
                value += group[offset + i * patches + patch] * group[offset + j * patches + patch];
            }
            matrix[i * components + j] = value * normalization;
            matrix[i + j * components] = value * normalization;
        }
    }
    return matrix;
}

/// The published method's in-place Cholesky inverse of a symmetric positive-definite matrix.
[[nodiscard]] bool invert(std::vector<float>& values, std::size_t order)
{
    for (std::size_t j = 0u, p = 0u; j < order; ++j, p += order + 1u)
    {
        for (std::size_t q = j * order; q < p; ++q)
        {
            values[p] -= values[q] * values[q];
        }
        if (!(values[p] > 0.0f))
        {
            return false;
        }
        values[p] = std::sqrt(values[p]);
        for (std::size_t k = j + 1u, q = p + order; k < order; ++k, q += order)
        {
            float sum = 0.0f;
            for (std::size_t r = j * order, s = k * order; r < p; ++r, ++s)
            {
                sum += values[r] * values[s];
            }
            values[q] -= sum;
            values[q] /= values[p];
        }
    }
    for (std::size_t i = 0u; i + 1u < order; ++i)
    {
        std::size_t p = i * (order + 1u) + 1u;
        std::size_t q = i * (order + 1u) + order;
        for (std::size_t j = 0u; j + 1u + i < order; ++j, ++p, q += order)
        {
            std::swap(values[p], values[q]);
        }
    }
    for (std::size_t j = 0u, p = 0u; j < order; ++j, p += order + 1u)
    {
        values[p] = 1.0f / values[p];
        for (std::size_t q = j, t = 0u; q < p; t += order + 1u, q += order)
        {
            float sum = 0.0f;
            for (std::size_t s = q, r = t; s < p; s += order, ++r)
            {
                sum -= values[s] * values[r];
            }
            values[q] = sum * values[p];
        }
    }
    for (std::size_t j = 0u, p = 0u; j < order; ++j, p += order + 1u)
    {
        for (std::size_t q = j, t = p - j; q <= p; q += order, ++t)
        {
            float sum = 0.0f;
            for (std::size_t k = j, r = p, s = q; k < order; ++k, ++r, ++s)
            {
                sum += values[r] * values[s];
            }
            values[t] = sum;
            values[q] = sum;
        }
    }
    return true;
}

/// A matrix times a component-major group, each sample a dot product in column order.
[[nodiscard]] std::vector<float> multiply(const std::vector<float>& matrix, const std::vector<float>& group,
                                          std::size_t offset, std::size_t order, std::uint32_t patches)
{
    std::vector<float> product(order * patches, 0.0f);
    for (std::size_t row = 0u; row < order; ++row)
    {
        for (std::uint32_t patch = 0u; patch < patches; ++patch)
        {
            float sum = 0.0f;
            for (std::size_t column = 0u; column < order; ++column)
            {
                sum += matrix[row * order + column] * group[offset + column * patches + patch];
            }
            product[row * patches + patch] = sum;
        }
    }
    return product;
}

/// The Bessel-corrected variance of a run, from its sum and its sum of squares.
[[nodiscard]] float variance(const std::vector<float>& group, std::size_t offset, std::size_t count)
{
    if (count < 2u)
    {
        return 0.0f;
    }
    float mean = 0.0f;
    float squares = 0.0f;
    for (std::size_t i = 0u; i < count; ++i)
    {
        mean += group[offset + i];
        squares += group[offset + i] * group[offset + i];
    }
    const auto population = static_cast<float>(count);
    return (squares - mean * mean / population) / (population - 1.0f);
}

/// One pass over one bordered tile: the reference walk, the groups, their estimates and the
/// aggregation, then the division by the aggregated weight.
[[nodiscard]] Frame estimateTile(const Frame& noisy, const Frame& guide, const an::PatchDenoisePassParameters& pass,
                                 bool second, float sigma)
{
    const std::uint32_t width = noisy.width;
    const std::uint32_t height = noisy.height;
    const std::uint32_t channels = noisy.channels;
    const std::size_t plane = noisy.plane();
    const std::uint32_t window = pass.searchWindow;
    const std::uint32_t side = pass.patchSize;
    const std::size_t patchSamples = static_cast<std::size_t>(side) * side;
    const std::size_t components = second ? patchSamples * channels : patchSamples;
    const float diagonal = pass.covarianceWeight * sigma * sigma;
    const float threshold = sigma * sigma * pass.homogeneityGamma * (second ? 1.0f : static_cast<float>(channels));

    Frame estimate{std::vector<float>(noisy.samples.size(), 0.0f), width, height, channels};
    std::vector<float> weight(noisy.samples.size(), 0.0f);
    std::vector<std::uint8_t> mask(plane, 0u);
    for (std::uint32_t y = window; y + window < height; ++y)
    {
        for (std::uint32_t x = window; x + window < width; ++x)
        {
            mask[static_cast<std::size_t>(y) * width + x] = 1u;
        }
    }

    const Frame& searched = second ? guide : noisy;
    for (std::uint32_t reference = 0u; reference < plane; reference += pass.referenceStep)
    {
        if (mask[reference] == 0u)
        {
            continue;
        }
        const std::uint32_t origin = reference - (window - 1u) / 2u * (width + 1u);
        std::vector<Candidate> candidates;
        for (std::uint32_t i = 0u; i < window; ++i)
        {
            for (std::uint32_t j = 0u; j < window; ++j)
            {
                const std::uint32_t position = i * width + j + origin;
                float distance = 0.0f;
                for (std::uint32_t channel = 0u; channel < (second ? channels : 1u); ++channel)
                {
                    for (std::uint32_t row = 0u; row < side; ++row)
                    {
                        for (std::uint32_t column = 0u; column < side; ++column)
                        {
                            const std::size_t offset = channel * plane + static_cast<std::size_t>(row) * width + column;
                            const float difference =
                                searched.samples[reference + offset] - searched.samples[position + offset];
                            distance += difference * difference;
                        }
                    }
                }
                candidates.push_back(Candidate{distance, position});
            }
        }
        std::sort(candidates.begin(), candidates.end(), closer);
        std::vector<std::uint32_t> index;
        if (second)
        {
            const float cut = (std::max)(pass.similarityDistance, candidates[pass.similarPatches - 1u].distance);
            for (const Candidate& candidate : candidates)
            {
                if (candidate.distance < cut)
                {
                    index.push_back(candidate.position);
                }
            }
            if (index.empty())
            {
                index.push_back(candidates[0].position);
            }
        }
        else
        {
            for (std::uint32_t n = 0u; n < pass.similarPatches; ++n)
            {
                index.push_back(candidates[n].position);
            }
        }
        const auto patches = static_cast<std::uint32_t>(index.size());

        // Component-major: component c of patch n at c * patches + n, the components running
        // channel, then row, then column.
        std::vector<float> group(patchSamples * channels * patches);
        std::vector<float> noisyGroup(second ? group.size() : 0u);
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t row = 0u; row < side; ++row)
            {
                for (std::uint32_t column = 0u; column < side; ++column)
                {
                    const std::size_t component =
                        channel * patchSamples + static_cast<std::size_t>(row) * side + column;
                    const std::size_t offset = channel * plane + static_cast<std::size_t>(row) * width + column;
                    for (std::uint32_t n = 0u; n < patches; ++n)
                    {
                        group[component * patches + n] = searched.samples[index[n] + offset];
                        if (second)
                        {
                            noisyGroup[component * patches + n] = noisy.samples[index[n] + offset];
                        }
                    }
                }
            }
        }

        const std::size_t channelRun = patchSamples * patches;
        bool estimated = false;
        if (pass.homogeneousArea)
        {
            const std::vector<float>& measured = second ? noisyGroup : group;
            float spread = 0.0f;
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                spread += variance(measured, channel * channelRun, channelRun);
            }
            if (second)
            {
                spread /= static_cast<float>(channels);
            }
            if (spread < threshold)
            {
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    float mean = 0.0f;
                    for (std::size_t i = 0u; i < channelRun; ++i)
                    {
                        mean += group[channel * channelRun + i];
                    }
                    mean /= static_cast<float>(channelRun);
                    std::fill_n(group.begin() + static_cast<std::ptrdiff_t>(channel * channelRun), channelRun, mean);
                }
                estimated = true;
            }
        }
        if (!estimated && second)
        {
            std::vector<float> barycentre(components);
            centre(group, 0u, barycentre, patches, components);
            centre(noisyGroup, 0u, barycentre, patches, components);
            std::vector<float> matrix = covariance(group, 0u, patches, components);
            for (std::size_t k = 0u; k < components; ++k)
            {
                matrix[k * components + k] += diagonal;
            }
            if (invert(matrix, components))
            {
                const std::vector<float> product = multiply(matrix, noisyGroup, 0u, components, patches);
                for (std::size_t k = 0u; k < group.size(); ++k)
                {
                    group[k] = noisyGroup[k] - diagonal * product[k];
                }
            }
            else
            {
                group = noisyGroup;
            }
            for (std::size_t component = 0u; component < components; ++component)
            {
                for (std::uint32_t n = 0u; n < patches; ++n)
                {
                    group[component * patches + n] += barycentre[component];
                }
            }
        }
        else if (!estimated)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const std::size_t offset = channel * channelRun;
                std::vector<float> barycentre(components);
                centre(group, offset, barycentre, patches, components);
                std::vector<float> matrix = covariance(group, offset, patches, components);
                if (invert(matrix, components))
                {
                    const std::vector<float> product = multiply(matrix, group, offset, components, patches);
                    for (std::size_t k = 0u; k < channelRun; ++k)
                    {
                        group[offset + k] -= diagonal * product[k];
                    }
                }
                for (std::size_t component = 0u; component < components; ++component)
                {
                    for (std::uint32_t n = 0u; n < patches; ++n)
                    {
                        group[offset + component * patches + n] += barycentre[component];
                    }
                }
            }
        }

        for (std::uint32_t n = 0u; n < patches; ++n)
        {
            const std::uint32_t position = index[n];
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                for (std::uint32_t row = 0u; row < side; ++row)
                {
                    for (std::uint32_t column = 0u; column < side; ++column)
                    {
                        const std::size_t component =
                            channel * patchSamples + static_cast<std::size_t>(row) * side + column;
                        const std::size_t target =
                            channel * plane + position + static_cast<std::size_t>(row) * width + column;
                        estimate.samples[target] += group[component * patches + n];
                        weight[target] += 1.0f;
                    }
                }
            }
            mask[position] = 0u;
            mask[position - width] = 0u;
            mask[position + width] = 0u;
            mask[position - 1u] = 0u;
            mask[position + 1u] = 0u;
        }
    }

    const Frame& fallback = second ? guide : noisy;
    for (std::size_t i = 0u; i < estimate.samples.size(); ++i)
    {
        estimate.samples[i] = weight[i] > 0.0f ? estimate.samples[i] / weight[i] : fallback.samples[i];
    }
    return estimate;
}

/// One pass over a whole frame: every tile of the grid the pass's boundary gives, read with its
/// boundary reflected from the frame, estimated, and its interior written back.
[[nodiscard]] Frame runPass(const Frame& noisy, const Frame& guide, const an::PatchDenoisePassParameters& pass,
                            bool second, float sigma)
{
    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    an::PatchDenoisePlan::tileGrid(noisy.width, noisy.height, pass.boundary, tilesX, tilesY);
    Frame output{std::vector<float>(noisy.samples.size(), 0.0f), noisy.width, noisy.height, noisy.channels};
    for (std::uint32_t tileRow = 0u; tileRow < tilesY; ++tileRow)
    {
        for (std::uint32_t tileColumn = 0u; tileColumn < tilesX; ++tileColumn)
        {
            const auto firstX = static_cast<std::uint32_t>(std::uint64_t{noisy.width} * tileColumn / tilesX);
            const auto lastX = static_cast<std::uint32_t>(std::uint64_t{noisy.width} * (tileColumn + 1u) / tilesX);
            const auto firstY = static_cast<std::uint32_t>(std::uint64_t{noisy.height} * tileRow / tilesY);
            const auto lastY = static_cast<std::uint32_t>(std::uint64_t{noisy.height} * (tileRow + 1u) / tilesY);
            const std::uint32_t tileWidth = lastX - firstX + 2u * pass.boundary;
            const std::uint32_t tileHeight = lastY - firstY + 2u * pass.boundary;
            const auto read = [&](const Frame& frame) {
                Frame tile{std::vector<float>(static_cast<std::size_t>(tileWidth) * tileHeight * frame.channels),
                           tileWidth, tileHeight, frame.channels};
                for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
                {
                    for (std::uint32_t row = 0u; row < tileHeight; ++row)
                    {
                        const std::uint32_t sourceRow =
                            reflect(std::int64_t{firstY} + row - pass.boundary, frame.height);
                        for (std::uint32_t column = 0u; column < tileWidth; ++column)
                        {
                            const std::uint32_t sourceColumn =
                                reflect(std::int64_t{firstX} + column - pass.boundary, frame.width);
                            tile.samples[channel * tile.plane() + static_cast<std::size_t>(row) * tileWidth + column] =
                                frame.samples[channel * frame.plane() +
                                              static_cast<std::size_t>(sourceRow) * frame.width + sourceColumn];
                        }
                    }
                }
                return tile;
            };
            const Frame noisyTile = read(noisy);
            const Frame guideTile = second ? read(guide) : Frame{};
            const Frame estimate = estimateTile(noisyTile, guideTile, pass, second, sigma);
            for (std::uint32_t channel = 0u; channel < noisy.channels; ++channel)
            {
                for (std::uint32_t row = firstY; row < lastY; ++row)
                {
                    for (std::uint32_t column = firstX; column < lastX; ++column)
                    {
                        output
                            .samples[channel * output.plane() + static_cast<std::size_t>(row) * output.width + column] =
                            estimate.samples[channel * estimate.plane() +
                                             static_cast<std::size_t>(row - firstY + pass.boundary) * tileWidth +
                                             column - firstX + pass.boundary];
                    }
                }
            }
        }
    }
    return output;
}

/// The method's colour decorrelation of a three-channel frame, forwards or back.
void decorrelate(Frame& frame, bool forward)
{
    if (frame.channels != 3u)
    {
        return;
    }
    const float a = 1.0f / std::sqrt(3.0f);
    const float b = 1.0f / std::sqrt(2.0f);
    const std::size_t plane = frame.plane();
    for (std::size_t i = 0u; i < plane; ++i)
    {
        float& first = frame.samples[i];
        float& second = frame.samples[plane + i];
        float& third = frame.samples[2u * plane + i];
        if (forward)
        {
            const float c = 2.0f * a * std::sqrt(2.0f);
            const float red = first;
            const float green = second;
            const float blue = third;
            first = a * (red + green + blue);
            second = b * (red - blue);
            third = c * (0.25f * red - 0.5f * green + 0.25f * blue);
        }
        else
        {
            const float c = a / b;
            const float y = first;
            const float u = second;
            const float v = third;
            first = a * y + b * u + c * 0.5f * v;
            second = a * y - c * v;
            third = a * y - b * u + c * 0.5f * v;
        }
    }
}

/// The whole operation as the method states it for a request that measures nothing: the frame on the
/// tuning scale, the basic estimate in the decorrelated space, the final estimate against it, the
/// answer clamped to the tuning scale's range and brought back.
[[nodiscard]] std::vector<float> oracleDenoise(const Frame& source, const an::PatchDenoisePlan& plan)
{
    const double tuningScale = plan.tuningScale();
    const double tunedSigma = plan.request().sigma * tuningScale;
    const an::PatchDenoisePassParameters first = plan.passParameters(tunedSigma, false);
    const an::PatchDenoisePassParameters second = plan.passParameters(tunedSigma, true);
    const auto sigma = static_cast<float>(tunedSigma);

    Frame noisy = source;
    for (float& sample : noisy.samples)
    {
        sample *= static_cast<float>(tuningScale);
    }
    Frame decorrelated = noisy;
    decorrelate(decorrelated, true);
    Frame basic = runPass(decorrelated, Frame{}, first, false, sigma);
    decorrelate(basic, false);
    for (std::size_t i = 0u; i < basic.samples.size(); ++i)
    {
        if (!std::isfinite(basic.samples[i]))
        {
            basic.samples[i] = noisy.samples[i];
        }
    }
    Frame answer = runPass(noisy, basic, second, true, sigma);
    const auto ceiling = static_cast<float>(an::kPatchDenoiseTuningScale);
    for (float& sample : answer.samples)
    {
        sample = std::clamp(sample, 0.0f, ceiling) * static_cast<float>(1.0 / tuningScale);
    }
    return answer.samples;
}

/// A frame with enough structure that most patch groups are not featureless: a gradient, a ripple,
/// a few bright round sources and a little noise, different in every channel.
[[nodiscard]] Frame structuredFrame(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                    std::uint64_t seed)
{
    Sequence sequence{seed};
    Frame frame{std::vector<float>(static_cast<std::size_t>(width) * height * channels), width, height, channels};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                double value = 0.15 + 0.1 * channel + 0.2 * x / width + 0.05 * std::sin(y / 5.0 + channel);
                for (std::uint32_t source = 0u; source < 4u; ++source)
                {
                    const double cx = width * (0.2 + 0.2 * source);
                    const double cy = height * (0.8 - 0.19 * source);
                    const double distance = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    value += 0.5 * std::exp(-distance / (2.0 * (1.5 + source)));
                }
                value += 0.04 * (sequence.next() - 0.5f);
                frame.samples[channel * frame.plane() + static_cast<std::size_t>(y) * width + x] =
                    static_cast<float>(value);
            }
        }
    }
    return frame;
}

/// Denoises a frame through the operation, at a stated width, and returns the answer.
[[nodiscard]] std::vector<float> engineDenoise(const Frame& frame, const an::PatchDenoisePlan& plan,
                                               std::uint32_t workers)
{
    const an::EngineContext engine = makeEngine(workers);
    const an::ExecutionContext execution = makeExecution(engine, workers);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, frame.width, frame.height, frame.channels);
    std::vector<float> answer(frame.samples.size(), -1.0f);
    const an::Result<an::PatchDenoiseResult> ran = an::denoiseImagePatches(
        execution, an::SampleType::Float32, frame.samples.data(), layout, answer.data(), layout, plan);
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    return answer;
}

/// How many samples of two answers differ in any bit, and where the first one is.
[[nodiscard]] testing::AssertionResult sameBits(const std::vector<float>& actual, const std::vector<float>& expected)
{
    if (actual.size() != expected.size())
    {
        return testing::AssertionFailure() << "sizes differ";
    }
    std::size_t differing = 0u;
    std::size_t first = 0u;
    for (std::size_t i = 0u; i < actual.size(); ++i)
    {
        if (std::bit_cast<std::uint32_t>(actual[i]) != std::bit_cast<std::uint32_t>(expected[i]))
        {
            first = differing == 0u ? i : first;
            ++differing;
        }
    }
    if (differing == 0u)
    {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << differing << " of " << actual.size() << " samples differ; the first at "
                                       << first << " is " << actual[first] << " against " << expected[first];
}

/// A request that measures nothing, with the featureless shortcut on or off.
[[nodiscard]] an::PatchDenoisePlan planFor(std::uint32_t channels, bool shortcut)
{
    an::PatchDenoiseRequest request;
    request.sigma = 0.02;
    request.whiteLevel = 1.0;
    request.homogeneousAreaFirstPass = shortcut;
    request.homogeneousAreaSecondPass = shortcut;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, channels);
    EXPECT_TRUE(plan.hasValue());
    return *plan;
}

} // namespace

TEST(PatchEstimatorOracleTest, OneTileOfAMonochromeFrameIsThePublishedMethodSumForSum)
{
    const Frame frame = structuredFrame(60u, 52u, 1u, 17u);
    for (const bool shortcut : {true, false})
    {
        const an::PatchDenoisePlan plan = planFor(1u, shortcut);
        EXPECT_TRUE(sameBits(engineDenoise(frame, plan, 3u), oracleDenoise(frame, plan))) << shortcut;
    }
}

TEST(PatchEstimatorOracleTest, AFrameOfSeveralTilesIsThePublishedMethodSumForSum)
{
    // Both passes divide this frame into several tiles, and the second into sixteen.
    const Frame frame = structuredFrame(208u, 200u, 1u, 29u);
    const an::PatchDenoisePlan plan = planFor(1u, true);
    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    an::PatchDenoisePlan::tileGrid(frame.width, frame.height, plan.passParameters(5.1, true).boundary, tilesX, tilesY);
    ASSERT_EQ(tilesX * tilesY, 16u);
    EXPECT_TRUE(sameBits(engineDenoise(frame, plan, 4u), oracleDenoise(frame, plan)));
}

TEST(PatchEstimatorOracleTest, AColourFrameIsThePublishedMethodSumForSum)
{
    // Three channels: the basic estimate runs per channel in the decorrelated space and the final one
    // over every channel's components at once.
    const Frame frame = structuredFrame(48u, 44u, 3u, 43u);
    for (const bool shortcut : {true, false})
    {
        const an::PatchDenoisePlan plan = planFor(3u, shortcut);
        EXPECT_TRUE(sameBits(engineDenoise(frame, plan, 2u), oracleDenoise(frame, plan))) << shortcut;
    }
}
