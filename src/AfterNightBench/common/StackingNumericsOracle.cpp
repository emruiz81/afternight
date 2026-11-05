// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definitions of the comet composite and rejection-map statistics rows.

#include "StackingNumericsOracle.h"

#include "StackingNumericsScenarios.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace an::bench
{

namespace
{

/// The comet's profile at a distance squared from its centre.
[[nodiscard]] double cometProfile(double distance2) noexcept
{
    const double spread = 1.0 + distance2 / (kCometWidth * kCometWidth);
    return kCometPeak / (spread * spread);
}

/// The comet added to one row of a plane at a centre, in double, rounded once.
void addComet(std::vector<double>& plane, std::uint32_t width, std::uint32_t height, double centreX, double centreY)
{
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const double dy = (static_cast<double>(y) + 0.5) - centreY;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = (static_cast<double>(x) + 0.5) - centreX;
            plane[static_cast<std::size_t>(y) * width + x] += cometProfile(dx * dx + dy * dy);
        }
    }
}

[[nodiscard]] std::vector<float> narrowed(const std::vector<double>& plane)
{
    std::vector<float> samples(plane.size());
    for (std::size_t index = 0u; index < plane.size(); ++index)
    {
        samples[index] = static_cast<float>(plane[index]);
    }
    return samples;
}

/// The product's feathered circle.
[[nodiscard]] double featherWeight(double distance, double radius, double feather) noexcept
{
    if (distance <= radius)
    {
        return 1.0;
    }
    if (feather <= 0.0 || distance >= radius + feather)
    {
        return 0.0;
    }
    return 1.0 - ((distance - radius) / feather);
}

/// The product's median of the annulus around an aperture, or of the whole plane below eight samples.
[[nodiscard]] double annulusMedian(std::span<const float> plane, std::uint32_t width, std::uint32_t height,
                                   double centreX, double centreY, double radius)
{
    const double inner = (std::max)(radius * 1.5, radius + 1.0);
    const double outer = (std::max)(inner + 2.0, radius * 2.5);
    std::vector<double> samples;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const double dy = (static_cast<double>(y) + 0.5) - centreY;
        if (std::fabs(dy) > outer + 1.0)
        {
            continue;
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = (static_cast<double>(x) + 0.5) - centreX;
            const double distance2 = dx * dx + dy * dy;
            if (distance2 >= inner * inner && distance2 <= outer * outer)
            {
                samples.push_back(static_cast<double>(plane[static_cast<std::size_t>(y) * width + x]));
            }
        }
    }
    if (samples.size() < 8u)
    {
        samples.assign(plane.begin(), plane.end());
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2u;
    return samples.size() % 2u == 0u ? 0.5 * (samples[middle - 1u] + samples[middle]) : samples[middle];
}

/// One layer of a frame split at an aperture, rounded to Float32 as the engine writes it.
[[nodiscard]] std::vector<float> splitLayer(std::span<const float> frame, std::uint32_t width, std::uint32_t height,
                                            double centreX, double centreY, bool cometLayer)
{
    const double sky = annulusMedian(frame, width, height, centreX, centreY, kCometRadius);
    const double feather = (std::max)(1.0, kCometRadius * 0.35);
    std::vector<float> layer(frame.size());
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const double dy = (static_cast<double>(y) + 0.5) - centreY;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = (static_cast<double>(x) + 0.5) - centreX;
            const double weight = featherWeight(std::hypot(dx, dy), kCometRadius, feather);
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const double source = static_cast<double>(frame[index]);
            const double star = source * (1.0 - weight) + sky * weight;
            layer[index] = static_cast<float>(cometLayer ? sky + (source - star) : star);
        }
    }
    return layer;
}

/// A deterministic hash of an element.
[[nodiscard]] std::uint64_t elementHash(std::uint64_t index) noexcept
{
    std::uint64_t value = (index + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull;
    value ^= value >> 31u;
    value *= 0x94D049BB133111EBull;
    return value ^ (value >> 29u);
}

/// The clusters one map reports and the largest, by an eight-connected flood fill of each channel plane.
struct MapClusters
{
    std::uint64_t found = 0u;
    std::uint64_t largest = 0u;
};

[[nodiscard]] MapClusters clustersOf(std::span<const std::uint32_t> map, std::uint32_t width, std::uint32_t height,
                                     std::uint32_t channels)
{
    MapClusters clusters;
    const std::size_t plane = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> visited(plane);
    std::vector<std::size_t> stack;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::uint32_t* const counts = map.data() + channel * plane;
        std::fill(visited.begin(), visited.end(), std::uint8_t{0u});
        for (std::size_t seed = 0u; seed < plane; ++seed)
        {
            if (visited[seed] != 0u || counts[seed] == 0u)
            {
                continue;
            }
            visited[seed] = 1u;
            stack.assign(1u, seed);
            std::uint64_t pixels = 0u;
            while (!stack.empty())
            {
                const std::size_t index = stack.back();
                stack.pop_back();
                ++pixels;
                const auto x = static_cast<std::int64_t>(index % width);
                const auto y = static_cast<std::int64_t>(index / width);
                for (std::int64_t ny = y - 1; ny <= y + 1; ++ny)
                {
                    for (std::int64_t nx = x - 1; nx <= x + 1; ++nx)
                    {
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        {
                            continue;
                        }
                        const std::size_t neighbour =
                            static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx);
                        if (visited[neighbour] == 0u && counts[neighbour] != 0u)
                        {
                            visited[neighbour] = 1u;
                            stack.push_back(neighbour);
                        }
                    }
                }
            }
            if (pixels >= kRejectionMinimumClusterPixels)
            {
                ++clusters.found;
                clusters.largest = (std::max)(clusters.largest, pixels);
            }
        }
    }
    return clusters;
}

} // namespace

bool requiresStackingNumericsOracle(const ScenarioSpec& scenario)
{
    return scenario.id == kCometComposite || scenario.id == kRejectionMapStatistics;
}

double cometStarAlignedX(std::uint32_t frame) noexcept
{
    return kCometStartX + static_cast<double>(frame) * kCometStep;
}

std::vector<float> cometStarAlignedFrame(std::span<const float> field, std::uint32_t width, std::uint32_t height,
                                         std::uint32_t frame)
{
    std::vector<double> plane(field.begin(), field.begin() + static_cast<std::ptrdiff_t>(width) * height);
    addComet(plane, width, height, cometStarAlignedX(frame), kCometY);
    return narrowed(plane);
}

std::vector<float> cometCometAlignedFrame(std::span<const float> field, std::uint32_t width, std::uint32_t height,
                                          std::uint32_t frame)
{
    const auto shift = static_cast<std::uint32_t>(static_cast<double>(frame) * kCometStep);
    std::vector<double> plane(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::uint32_t from = (std::min)(x + shift, width - 1u);
            plane[static_cast<std::size_t>(y) * width + x] =
                static_cast<double>(field[static_cast<std::size_t>(y) * width + from]);
        }
    }
    addComet(plane, width, height, kCometStartX, kCometY);
    return narrowed(plane);
}

void checkCometCompositeOutput(std::span<const float> field, std::uint32_t width, std::uint32_t height,
                               std::span<const float> composite, ResultRecord& result)
{
    const std::size_t samples = static_cast<std::size_t>(width) * height;
    std::vector<double> starStack(samples, 0.0);
    std::vector<double> cometStack(samples, 0.0);
    for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
    {
        const std::vector<float> starFrame = cometStarAlignedFrame(field, width, height, frame);
        const std::vector<float> cometFrame = cometCometAlignedFrame(field, width, height, frame);
        const std::vector<float> starLayer =
            splitLayer(starFrame, width, height, cometStarAlignedX(frame), kCometY, false);
        const std::vector<float> cometLayer = splitLayer(cometFrame, width, height, kCometStartX, kCometY, true);
        for (std::size_t index = 0u; index < samples; ++index)
        {
            starStack[index] += static_cast<double>(starLayer[index]);
            cometStack[index] += static_cast<double>(cometLayer[index]);
        }
    }
    const double maskFeather = (std::max)(2.0, kCometRadius * 0.45);
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const double dy = (static_cast<double>(y) + 0.5) - kCometY;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            double weight = 0.0;
            for (std::uint32_t frame = 0u; frame < kCometFrames; ++frame)
            {
                const double dx = (static_cast<double>(x) + 0.5) - cometStarAlignedX(frame);
                weight = (std::max)(weight, featherWeight(std::hypot(dx, dy), kCometRadius, maskFeather));
            }
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const double star = starStack[index] / kCometFrames;
            const double comet = cometStack[index] / kCometFrames;
            const double expected = star * (1.0 - weight) + comet * weight;
            largest = (std::max)(largest, std::fabs(static_cast<double>(composite[index]) - expected));
        }
    }
    result.correctnessMetric = largest;
}

std::vector<std::uint32_t> rejectionLowCounts(std::span<const float> field, std::uint32_t width, std::uint32_t height,
                                              std::uint32_t channels)
{
    (void)field;
    std::vector<std::uint32_t> counts(static_cast<std::size_t>(width) * height * channels, 0u);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t index = (static_cast<std::size_t>(channel) * height + y) * width + x;
                std::uint32_t count = (elementHash(index) % 64u) == 0u ? 1u : 0u;
                if (x % 97u < 3u && y % 89u < 3u)
                {
                    count = 2u;
                }
                counts[index] = count;
            }
        }
    }
    return counts;
}

std::vector<std::uint32_t> rejectionHighCounts(std::span<const float> field, std::uint32_t width, std::uint32_t height,
                                               std::uint32_t channels)
{
    std::vector<std::uint32_t> counts(static_cast<std::size_t>(width) * height * channels, 0u);
    for (std::size_t index = 0u; index < counts.size(); ++index)
    {
        const double value = static_cast<double>(field[index]);
        if (value > 0.12)
        {
            counts[index] =
                (std::min)(kRejectionFrames, 1u + static_cast<std::uint32_t>(std::floor((value - 0.12) / 0.04)));
        }
    }
    return counts;
}

std::vector<float> rejectionFractionMap(std::span<const std::uint32_t> counts)
{
    std::vector<float> map(counts.size());
    for (std::size_t index = 0u; index < counts.size(); ++index)
    {
        map[index] = static_cast<float>(static_cast<double>(counts[index]) / static_cast<double>(kRejectionFrames));
    }
    return map;
}

void checkRejectionMapStatistics(std::span<const std::uint32_t> low, std::span<const std::uint32_t> high,
                                 std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                 const RejectionMapStatistics& produced, ResultRecord& result)
{
    std::uint64_t either = 0u;
    for (std::size_t index = 0u; index < low.size(); ++index)
    {
        either += (low[index] != 0u || high[index] != 0u) ? 1u : 0u;
    }
    const MapClusters lowClusters = clustersOf(low, width, height, channels);
    const MapClusters highClusters = clustersOf(high, width, height, channels);
    const double elements = static_cast<double>(low.size());
    const std::uint64_t largest = (std::max)(lowClusters.largest, highClusters.largest);
    const std::array<double, 5u> expected{
        static_cast<double>(either), static_cast<double>(either) / elements,
        static_cast<double>((std::min)(lowClusters.found, std::uint64_t{kRejectionMaximumClusters}) +
                            (std::min)(highClusters.found, std::uint64_t{kRejectionMaximumClusters})),
        static_cast<double>(largest), static_cast<double>(largest) / elements};
    const std::array<double, 5u> actual{produced.eitherElements, produced.eitherFraction, produced.clusters,
                                        produced.largestClusterPixels, produced.largestClusterFraction};
    double difference = 0.0;
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        difference = (std::max)(difference, std::fabs(expected[index] - actual[index]));
    }
    result.correctnessMetric = difference;
    result.counters["eitherElements"] = static_cast<double>(either);
    result.counters["lowClustersFound"] = static_cast<double>(lowClusters.found);
    result.counters["highClustersFound"] = static_cast<double>(highClusters.found);
}

} // namespace an::bench
