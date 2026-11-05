// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The guided dual-domain shrinkage against a plain reading of the published method.
//
// Everything the stage does before its transforms is Float32 arithmetic in a stated order - the
// bilateral weights, the regression plane, the weighted sums, the coverage pyramid that decides where
// the next patch goes - and on the reference backend its exponential is the scalar one, so the oracle
// below reproduces all of it bit for bit and places exactly the same patches. Only the transforms
// differ: the oracle takes a direct discrete Fourier transform in double precision where the stage
// runs its library's, so the answer is held to a small absolute tolerance rather than to its bits,
// and the patch counts are held exactly.

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

/// A position reflected back inside an extent with the edge sample repeated.
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

/// The smallest power of two at or above a value.
[[nodiscard]] std::uint32_t powerOfTwoAtLeast(std::uint32_t value)
{
    std::uint32_t rounded = 1u;
    while (rounded < value)
    {
        rounded <<= 1u;
    }
    return rounded;
}

/// The method's minimum pyramid over the aggregation weight of every interior pixel: power-of-two
/// levels halving to one cell, cells outside the interior infinitely covered, and a tie going to the
/// upper row and then the left column.
class Coverage
{
public:
    Coverage(std::uint32_t rows, std::uint32_t columns) : m_rows(rows), m_columns(columns)
    {
        std::uint32_t levelRows = powerOfTwoAtLeast(rows);
        std::uint32_t levelColumns = powerOfTwoAtLeast(columns);
        while (true)
        {
            m_shape.emplace_back(levelRows, levelColumns);
            m_level.emplace_back(static_cast<std::size_t>(levelRows) * levelColumns, 0.0f);
            if (levelRows == 1u && levelColumns == 1u)
            {
                break;
            }
            levelRows = levelRows > 1u ? levelRows / 2u : 1u;
            levelColumns = levelColumns > 1u ? levelColumns / 2u : 1u;
        }
        for (std::uint32_t row = 0u; row < m_shape[0].first; ++row)
        {
            for (std::uint32_t column = 0u; column < m_shape[0].second; ++column)
            {
                m_level[0][static_cast<std::size_t>(row) * m_shape[0].second + column] =
                    row < rows && column < columns ? 0.0f : std::numeric_limits<float>::infinity();
            }
        }
        for (std::size_t level = 1u; level < m_level.size(); ++level)
        {
            rebuild(level, 0u, m_shape[level].first - 1u, 0u, m_shape[level].second - 1u);
        }
    }

    [[nodiscard]] float minimum() const
    {
        return m_level.back()[0];
    }

    void findMinimum(std::uint32_t& row, std::uint32_t& column) const
    {
        std::uint32_t currentRow = 0u;
        std::uint32_t currentColumn = 0u;
        for (std::size_t above = m_level.size() - 1u; above > 0u; --above)
        {
            const std::size_t level = above - 1u;
            const std::uint32_t rowSpan = m_shape[level].first > m_shape[above].first ? 2u : 1u;
            const std::uint32_t columnSpan = m_shape[level].second > m_shape[above].second ? 2u : 1u;
            const std::uint32_t baseRow = currentRow * rowSpan;
            const std::uint32_t baseColumn = currentColumn * columnSpan;
            float best = std::numeric_limits<float>::infinity();
            bool chosen = false;
            for (std::uint32_t dr = 0u; dr < rowSpan; ++dr)
            {
                for (std::uint32_t dc = 0u; dc < columnSpan; ++dc)
                {
                    const float value = at(level, baseRow + dr, baseColumn + dc);
                    if (!chosen || value < best)
                    {
                        best = value;
                        currentRow = baseRow + dr;
                        currentColumn = baseColumn + dc;
                        chosen = true;
                    }
                }
            }
        }
        row = currentRow;
        column = currentColumn;
    }

    void increase(const std::vector<float>& weights, std::uint32_t side, std::int64_t originRow,
                  std::int64_t originColumn)
    {
        const std::int64_t firstRow = (std::max)(std::int64_t{0}, originRow);
        const std::int64_t lastRow = (std::min)(std::int64_t{m_rows}, originRow + side) - 1;
        const std::int64_t firstColumn = (std::max)(std::int64_t{0}, originColumn);
        const std::int64_t lastColumn = (std::min)(std::int64_t{m_columns}, originColumn + side) - 1;
        if (lastRow < firstRow || lastColumn < firstColumn)
        {
            return;
        }
        for (std::int64_t row = firstRow; row <= lastRow; ++row)
        {
            for (std::int64_t column = firstColumn; column <= lastColumn; ++column)
            {
                m_level[0][static_cast<std::size_t>(row) * m_shape[0].second + static_cast<std::size_t>(column)] +=
                    weights[static_cast<std::size_t>((row - originRow) * side + (column - originColumn))];
            }
        }
        auto low = static_cast<std::uint32_t>(firstRow);
        auto high = static_cast<std::uint32_t>(lastRow);
        auto left = static_cast<std::uint32_t>(firstColumn);
        auto right = static_cast<std::uint32_t>(lastColumn);
        for (std::size_t level = 1u; level < m_level.size(); ++level)
        {
            if (m_shape[level].first < m_shape[level - 1u].first)
            {
                low /= 2u;
                high /= 2u;
            }
            if (m_shape[level].second < m_shape[level - 1u].second)
            {
                left /= 2u;
                right /= 2u;
            }
            rebuild(level, low, high, left, right);
        }
    }

private:
    [[nodiscard]] float at(std::size_t level, std::uint32_t row, std::uint32_t column) const
    {
        return m_level[level][static_cast<std::size_t>(row) * m_shape[level].second + column];
    }

    void rebuild(std::size_t level, std::uint32_t firstRow, std::uint32_t lastRow, std::uint32_t firstColumn,
                 std::uint32_t lastColumn)
    {
        const std::size_t below = level - 1u;
        const std::uint32_t rowSpan = m_shape[below].first > m_shape[level].first ? 2u : 1u;
        const std::uint32_t columnSpan = m_shape[below].second > m_shape[level].second ? 2u : 1u;
        for (std::uint32_t row = firstRow; row <= lastRow && row < m_shape[level].first; ++row)
        {
            for (std::uint32_t column = firstColumn; column <= lastColumn && column < m_shape[level].second; ++column)
            {
                float smallest = std::numeric_limits<float>::infinity();
                for (std::uint32_t dr = 0u; dr < rowSpan; ++dr)
                {
                    for (std::uint32_t dc = 0u; dc < columnSpan; ++dc)
                    {
                        smallest = (std::min)(smallest, at(below, row * rowSpan + dr, column * columnSpan + dc));
                    }
                }
                m_level[level][static_cast<std::size_t>(row) * m_shape[level].second + column] = smallest;
            }
        }
    }

    std::uint32_t m_rows;
    std::uint32_t m_columns;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> m_shape;
    std::vector<std::vector<float>> m_level;
};

/// The scalar reference exponential the stage uses on the reference backend.
[[nodiscard]] float exponential(float value)
{
    return static_cast<float>(std::exp(static_cast<double>(value)));
}

/// A patch's bilateral weight: each sample's squared difference from the centre over the range
/// variance, plus its squared distance from the centre over twice the spatial variance, negated and
/// exponentiated.
[[nodiscard]] std::vector<float> bilateral(const std::vector<float>& guide, std::uint32_t side, std::uint32_t radius,
                                           float rangeVariance, float spatialVariance)
{
    std::vector<float> weight(guide.size());
    const float central = guide[static_cast<std::size_t>(radius) * side + radius];
    for (std::uint32_t row = 0u; row < side; ++row)
    {
        const auto rowOffset = static_cast<float>(static_cast<std::int32_t>(row) - static_cast<std::int32_t>(radius));
        for (std::uint32_t column = 0u; column < side; ++column)
        {
            const auto columnOffset =
                static_cast<float>(static_cast<std::int32_t>(column) - static_cast<std::int32_t>(radius));
            const std::size_t position = static_cast<std::size_t>(row) * side + column;
            const float delta = guide[position] - central;
            const float difference = 0.0f + delta * delta;
            const float exponent = difference / rangeVariance +
                                   (rowOffset * rowOffset + columnOffset * columnOffset) / (2.0f * spatialVariance);
            weight[position] = exponential(-exponent);
        }
    }
    return weight;
}

/// The discrete Fourier transform of a square patch in double precision, row by row and then column
/// by column; `sign` is -1 forwards and +1 backwards, and nothing is divided out.
[[nodiscard]] std::vector<std::complex<double>> transform(std::vector<std::complex<double>> samples, std::uint32_t side,
                                                          double sign)
{
    std::vector<std::complex<double>> twiddle(side);
    for (std::uint32_t k = 0u; k < side; ++k)
    {
        const double angle = sign * 2.0 * std::numbers::pi * k / side;
        twiddle[k] = std::complex<double>(std::cos(angle), std::sin(angle));
    }
    std::vector<std::complex<double>> line(side);
    for (int pass = 0; pass < 2; ++pass)
    {
        const std::size_t along = pass == 0 ? 1u : side;
        const std::size_t across = pass == 0 ? side : 1u;
        for (std::uint32_t index = 0u; index < side; ++index)
        {
            for (std::uint32_t k = 0u; k < side; ++k)
            {
                std::complex<double> sum = 0.0;
                for (std::uint32_t n = 0u; n < side; ++n)
                {
                    sum += samples[index * across + n * along] * twiddle[(static_cast<std::uint64_t>(k) * n) % side];
                }
                line[k] = sum;
            }
            for (std::uint32_t k = 0u; k < side; ++k)
            {
                samples[index * across + k * along] = line[k];
            }
        }
    }
    return samples;
}

/// A real patch as complex samples.
[[nodiscard]] std::vector<std::complex<double>> complexOf(const std::vector<float>& samples)
{
    return std::vector<std::complex<double>>(samples.begin(), samples.end());
}

/// What the oracle produced.
struct OracleRun
{
    std::vector<float> answer;
    std::uint64_t patches = 0u;
    std::uint64_t accepted = 0u;
};

/// The stage over one monochrome frame that is one tile: every patch placed at the least-covered
/// pixel until every pixel carries the threshold, each shrunk or, when its weight is too small,
/// replaced by its guide, and the aggregate divided by its weight.
[[nodiscard]] OracleRun oracleShrink(const std::vector<float>& noisy, const std::vector<float>& guide,
                                     std::uint32_t width, std::uint32_t height,
                                     const an::GuidedShrinkageRequest& request, std::uint32_t side)
{
    const std::uint32_t radius = request.radius;
    const std::uint32_t paddedWidth = width + side - 1u;
    const std::uint32_t paddedHeight = height + side - 1u;
    const auto pad = [&](const std::vector<float>& frame) {
        std::vector<float> tile(static_cast<std::size_t>(paddedWidth) * paddedHeight);
        for (std::uint32_t row = 0u; row < paddedHeight; ++row)
        {
            for (std::uint32_t column = 0u; column < paddedWidth; ++column)
            {
                tile[static_cast<std::size_t>(row) * paddedWidth + column] =
                    frame[static_cast<std::size_t>(reflect(std::int64_t{row} - radius, height)) * width +
                          reflect(std::int64_t{column} - radius, width)];
            }
        }
        return tile;
    };
    const std::vector<float> noisyTile = pad(noisy);
    const std::vector<float> guideTile = pad(guide);
    std::vector<float> answer(noisyTile.size(), 0.0f);
    std::vector<float> covered(noisyTile.size(), 0.0f);

    const auto sigma = static_cast<float>(request.sigma);
    const float noiseVariance = sigma * sigma;
    const float shrinkageRange = static_cast<float>(request.rangeFactor) * noiseVariance;
    const float shrinkageSpatial = static_cast<float>(request.spatialSigma) * static_cast<float>(request.spatialSigma);
    const float regressionRange = shrinkageRange * 10.0f;
    const float regressionSpatial = shrinkageSpatial * 2.0f;
    const auto threshold = static_cast<float>(request.coverageThreshold);
    const std::size_t patchSamples = static_cast<std::size_t>(side) * side;
    const auto offset = [&](std::uint32_t index) {
        return static_cast<float>(static_cast<std::int32_t>(index) - static_cast<std::int32_t>(radius));
    };

    OracleRun run;
    Coverage coverage(height, width);
    while (coverage.minimum() < threshold)
    {
        std::uint32_t patchRow = 0u;
        std::uint32_t patchColumn = 0u;
        coverage.findMinimum(patchRow, patchColumn);
        std::vector<float> patch(patchSamples);
        std::vector<float> guidePatch(patchSamples);
        for (std::uint32_t row = 0u; row < side; ++row)
        {
            for (std::uint32_t column = 0u; column < side; ++column)
            {
                const std::size_t source =
                    static_cast<std::size_t>(patchRow + row) * paddedWidth + patchColumn + column;
                patch[static_cast<std::size_t>(row) * side + column] = noisyTile[source];
                guidePatch[static_cast<std::size_t>(row) * side + column] = guideTile[source];
            }
        }

        const std::vector<float> regressionWeight =
            bilateral(guidePatch, side, radius, regressionRange, regressionSpatial);
        const float central = guidePatch[static_cast<std::size_t>(radius) * side + radius];
        float a = 0.0f;
        float b = 0.0f;
        float c = 0.0f;
        for (std::uint32_t row = 0u; row < side; ++row)
        {
            for (std::uint32_t column = 0u; column < side; ++column)
            {
                const float w = regressionWeight[static_cast<std::size_t>(row) * side + column];
                a += offset(row) * offset(row) * w;
                b += offset(row) * offset(column) * w;
                c += offset(column) * offset(column) * w;
            }
        }
        const float determinant = a * c - b * b;
        float rowSlope = 0.0f;
        float columnSlope = 0.0f;
        if (!(std::abs(determinant) < 1.0e-4f))
        {
            float d = 0.0f;
            float e = 0.0f;
            for (std::uint32_t row = 0u; row < side; ++row)
            {
                for (std::uint32_t column = 0u; column < side; ++column)
                {
                    const std::size_t position = static_cast<std::size_t>(row) * side + column;
                    const float residual = patch[position] - central;
                    d += offset(row) * residual * regressionWeight[position];
                    e += offset(column) * residual * regressionWeight[position];
                }
            }
            rowSlope = (c * d - b * e) / determinant;
            columnSlope = (a * e - b * d) / determinant;
        }
        for (std::uint32_t row = 0u; row < side; ++row)
        {
            for (std::uint32_t column = 0u; column < side; ++column)
            {
                const std::size_t position = static_cast<std::size_t>(row) * side + column;
                const float plane = rowSlope * offset(row) + columnSlope * offset(column);
                patch[position] -= plane;
                guidePatch[position] -= plane;
            }
        }
        std::vector<float> weight = bilateral(guidePatch, side, radius, shrinkageRange, shrinkageSpatial);
        float total = 0.0f;
        for (const float w : weight)
        {
            total += w;
        }
        ++run.patches;

        if (total < 10.0f)
        {
            ++run.accepted;
            for (float& w : weight)
            {
                w = w * w;
            }
            for (std::uint32_t row = 0u; row < side; ++row)
            {
                for (std::uint32_t column = 0u; column < side; ++column)
                {
                    const std::size_t position = static_cast<std::size_t>(row) * side + column;
                    const std::size_t target =
                        static_cast<std::size_t>(patchRow + row) * paddedWidth + patchColumn + column;
                    const float restored = guidePatch[position] + rowSlope * offset(row) + columnSlope * offset(column);
                    answer[target] += restored * weight[position];
                    covered[target] += weight[position];
                }
            }
        }
        else
        {
            float noisySum = 0.0f;
            float guideSum = 0.0f;
            float squared = 0.0f;
            for (std::size_t i = 0u; i < patchSamples; ++i)
            {
                noisySum += weight[i] * patch[i];
                guideSum += weight[i] * guidePatch[i];
                squared += weight[i] * weight[i];
            }
            const float noisyMean = noisySum / total;
            const float guideMean = guideSum / total;
            std::vector<float> modifiedNoisy(patchSamples);
            std::vector<float> modifiedGuide(patchSamples);
            for (std::size_t i = 0u; i < patchSamples; ++i)
            {
                modifiedNoisy[i] = weight[i] * patch[i] + (1.0f - weight[i]) * noisyMean;
                modifiedGuide[i] = weight[i] * guidePatch[i] + (1.0f - weight[i]) * guideMean;
            }
            std::vector<std::complex<double>> noisySpectrum = transform(complexOf(modifiedNoisy), side, -1.0);
            const std::vector<std::complex<double>> guideSpectrum = transform(complexOf(modifiedGuide), side, -1.0);
            const double noiseFloor = static_cast<double>(squared * noiseVariance);
            for (std::size_t frequency = 1u; frequency < patchSamples; ++frequency)
            {
                const double ratio = std::norm(guideSpectrum[frequency]) / noiseFloor;
                noisySpectrum[frequency] *= ratio > 0.0 ? std::exp(-0.8 / ratio) : 0.0;
            }
            const std::vector<std::complex<double>> shrunk = transform(std::move(noisySpectrum), side, 1.0);
            const double count = static_cast<double>(side) * side;
            for (std::uint32_t row = 0u; row < side; ++row)
            {
                for (std::uint32_t column = 0u; column < side; ++column)
                {
                    const std::size_t position = static_cast<std::size_t>(row) * side + column;
                    const std::size_t target =
                        static_cast<std::size_t>(patchRow + row) * paddedWidth + patchColumn + column;
                    const float w = weight[position];
                    const float slope = rowSlope * offset(row) + columnSlope * offset(column);
                    const float restored =
                        static_cast<float>(shrunk[position].real() / count) - (1.0f - w) * noisyMean + slope * w;
                    answer[target] += restored * w;
                    covered[target] += w * w;
                }
            }
            for (float& w : weight)
            {
                w = w * w;
            }
        }
        coverage.increase(weight, side, std::int64_t{patchRow} - radius, std::int64_t{patchColumn} - radius);
    }

    run.answer.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const std::size_t source = static_cast<std::size_t>(row + radius) * paddedWidth + column + radius;
            const float value = covered[source] > 0.0f ? answer[source] / covered[source] : guideTile[source];
            run.answer[static_cast<std::size_t>(row) * width + column] =
                std::clamp(value, 0.0f, static_cast<float>(request.whiteLevel));
        }
    }
    return run;
}

} // namespace

TEST(GuidedShrinkageOracleTest, APatchSequenceAndItsAggregateAreThePublishedMethods)
{
    // A frame with a gradient, a ripple, a few bright round sources and noise, against a smoothed
    // copy of itself as the guide: most patches take the transform path, and the ones centred on a
    // bright source's core carry too little weight for it and accept the guide.
    constexpr std::uint32_t kWidth = 88u;
    constexpr std::uint32_t kHeight = 76u;
    Sequence sequence{61u};
    std::vector<float> noisy(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            double value = 0.2 + 0.15 * x / kWidth + 0.04 * std::sin(y / 6.0);
            for (std::uint32_t source = 0u; source < 3u; ++source)
            {
                const double cx = kWidth * (0.25 + 0.25 * source);
                const double cy = kHeight * (0.7 - 0.2 * source);
                value += 0.6 * std::exp(-((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (2.0 * (1.2 + source)));
            }
            noisy[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<float>(value + 0.03 * (sequence.next() - 0.5f));
        }
    }
    std::vector<float> guide = noisy;
    for (std::uint32_t y = 1u; y + 1u < kHeight; ++y)
    {
        for (std::uint32_t x = 1u; x + 1u < kWidth; ++x)
        {
            float sum = 0.0f;
            for (std::uint32_t dy = 0u; dy < 3u; ++dy)
            {
                for (std::uint32_t dx = 0u; dx < 3u; ++dx)
                {
                    sum += noisy[static_cast<std::size_t>(y + dy - 1u) * kWidth + x + dx - 1u];
                }
            }
            guide[static_cast<std::size_t>(y) * kWidth + x] = sum / 9.0f;
        }
    }

    an::GuidedShrinkageRequest request;
    request.sigma = 0.01;
    request.radius = 8u;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue());
    const an::EngineContext engine = makeEngine(2u);
    const an::ExecutionContext execution = makeExecution(engine, 2u, an::ComputeBackend::Reference);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> answer(noisy.size(), -1.0f);
    const an::Result<an::GuidedShrinkageResult> ran = an::shrinkImageWithGuide(
        execution, an::SampleType::Float32, noisy.data(), layout, guide.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    ASSERT_EQ(ran->tileCount, 1u) << "the oracle reads the frame as one tile";

    const OracleRun expected = oracleShrink(noisy, guide, kWidth, kHeight, request, plan->transformSize());
    EXPECT_EQ(ran->patchesPlaced, expected.patches);
    EXPECT_EQ(ran->guideAcceptedPatches, expected.accepted);
    EXPECT_GT(expected.accepted, 0u) << "a bright core should take the guide";
    EXPECT_GT(expected.patches, expected.accepted) << "most patches should be shrunk";
    double largest = 0.0;
    for (std::size_t i = 0u; i < answer.size(); ++i)
    {
        largest = (std::max)(largest, std::abs(static_cast<double>(answer[i]) - expected.answer[i]));
    }
    // The transforms differ in their rounding, about one unit in the last place of a sample here; the
    // defects this is meant to catch move the answer by thousandths.
    EXPECT_LT(largest, 2.0e-6) << "largest difference " << largest;
}
