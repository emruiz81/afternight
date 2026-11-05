// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Shared helpers for the colour-calibration suite: contexts, RGB frames, a planted colour field
// whose truth is known before anything measures it, and a tangent-plane solution over it.
//
// Everything here is deterministic and platform-independent: positions, magnitudes and colours are
// drawn from an integer hash, stars are rendered as the same round Gaussian the fit is written
// against, and no result depends on the order the stars were drawn in.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace color_test
{

[[nodiscard]] inline an::EngineContext makeEngine(std::uint32_t concurrency = 8u,
                                                  std::uint64_t memoryLimit = 256u * 1024u * 1024u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryLimit;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] inline an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 4u,
                                                        std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A three-channel linear RGB description, or a one-channel one when asked.
[[nodiscard]] inline an::ImageDescriptor rgbDescriptor(std::uint32_t width, std::uint32_t height,
                                                       an::SampleType sampleType = an::SampleType::Float32,
                                                       std::uint32_t channels = 3u,
                                                       an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                                       an::ColorSpace colorSpace = an::ColorSpace::LinearRgb)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = sampleType,
                                             .channelSchema =
                                                 channels == 3u ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono,
                                             .colorSpace = channels == 3u ? colorSpace : an::ColorSpace::Unspecified,
                                             .numericDomain = domain,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// The tight planar layout of a description.
[[nodiscard]] inline an::ImageLayout planarLayout(const an::ImageDescriptor& descriptor)
{
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar, 1u);
    EXPECT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A checked frame over caller storage, failing the test rather than the caller when it refuses.
template <typename SampleT>
[[nodiscard]] an::StarFrameView frameOf(const std::vector<SampleT>& samples, const an::ImageLayout& layout,
                                        const an::ImageDescriptor& descriptor,
                                        const an::KernelValidityView& validity = {})
{
    const an::Result<an::StarFrameView> made =
        an::StarFrameView::create(samples.data(), samples.size() * sizeof(SampleT), layout, descriptor, validity);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::StarFrameView{};
}

/// One integer hash, so a field is the same field on every platform.
[[nodiscard]] inline std::uint64_t fieldHash(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

/// A hashed value in [0, 1).
[[nodiscard]] inline double fieldUnit(std::uint64_t value) noexcept
{
    return static_cast<double>(fieldHash(value) >> 11) / static_cast<double>(1ull << 53);
}

/// A hashed value in [low, high).
[[nodiscard]] inline double fieldRange(std::uint64_t value, double low, double high) noexcept
{
    return low + fieldUnit(value) * (high - low);
}

/// A zero-mean unit-variance draw from twelve uniforms.
[[nodiscard]] inline double fieldNormal(std::uint64_t value) noexcept
{
    double sum = 0.0;
    for (std::uint64_t index = 0u; index < 12u; ++index)
    {
        sum += fieldUnit(value * 12u + index);
    }
    return sum - 6.0;
}

/// One planted colour star, as the field knows it.
struct PlantedColorStar
{
    /// Centre in engine pixel coordinates.
    double x = 0.0;
    double y = 0.0;

    /// Standard deviation of the round Gaussian, in pixels.
    double sigma = 1.6;

    /// Green peak above the background.
    double greenAmplitude = 0.0;

    /// The catalog G magnitude assigned to this star.
    double magnitudeG = 0.0;

    /// The catalog colour index, or absent for a star the catalog has no colour for.
    std::optional<double> colorIndexBpMinusRp{};

    /// Red and blue peaks above their backgrounds, as rendered.
    double redAmplitude = 0.0;
    double blueAmplitude = 0.0;
};

/// What a planted colour field was asked to look like.
struct ColorFieldRequest
{
    std::uint32_t width = 512u;
    std::uint32_t height = 512u;
    std::uint32_t cell = 64u;
    std::array<double, 3u> background{0.050, 0.062, 0.041};
    double noise = 0.0012;
    double minimumAmplitude = 0.10;
    double maximumAmplitude = 0.55;
    double minimumSigma = 1.4;
    double maximumSigma = 2.2;

    /// The true red and blue channel scale the field was rendered with. A perfect calibration
    /// recovers gains of one over these.
    double redFactor = 1.0;
    double blueFactor = 1.0;

    /// Whether every star carries a colour index; false leaves every third star without one.
    bool everyStarColored = true;
    std::uint64_t seed = 11u;
};

/// A rendered three-plane field and what was planted in it.
struct ColorField
{
    std::vector<float> samples{};
    std::vector<PlantedColorStar> stars{};
    ColorFieldRequest request{};
};

/// The expected ratios of the model under test, restated so the fixture does not depend on it.
[[nodiscard]] inline std::array<double, 3u> modelRatios(std::optional<double> colorIndex) noexcept
{
    const double color = std::clamp(colorIndex.value_or(0.0), -0.5, 3.5);
    return {std::exp(0.32 * color), 1.0, std::exp(-0.40 * color)};
}

/// Plants one round Gaussian star per cell, with per-channel amplitudes following the colour model
/// scaled by the requested channel factors, on three planar Float32 planes.
[[nodiscard]] inline ColorField makeColorField(const ColorFieldRequest& request)
{
    ColorField field{};
    field.request = request;
    const std::size_t planeSize = static_cast<std::size_t>(request.width) * request.height;
    field.samples.assign(planeSize * 3u, 0.0f);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::fill(field.samples.begin() + static_cast<std::ptrdiff_t>(channel * planeSize),
                  field.samples.begin() + static_cast<std::ptrdiff_t>((channel + 1u) * planeSize),
                  static_cast<float>(request.background[channel]));
    }

    const std::uint32_t cellsX = request.width / request.cell;
    const std::uint32_t cellsY = request.height / request.cell;
    const double margin = static_cast<double>(request.cell);
    std::uint64_t ordinal = 0u;
    for (std::uint32_t cellY = 0u; cellY < cellsY; ++cellY)
    {
        for (std::uint32_t cellX = 0u; cellX < cellsX; ++cellX)
        {
            const std::uint64_t key =
                request.seed ^ (static_cast<std::uint64_t>(cellX) << 1u) ^ (static_cast<std::uint64_t>(cellY) << 22u);
            PlantedColorStar star{};
            star.x = (static_cast<double>(cellX) + 0.3 + 0.4 * fieldUnit(key + 1u)) * request.cell;
            star.y = (static_cast<double>(cellY) + 0.3 + 0.4 * fieldUnit(key + 2u)) * request.cell;
            ++ordinal;
            if (star.x < margin || star.y < margin || star.x > request.width - margin ||
                star.y > request.height - margin)
            {
                continue;
            }
            star.sigma = fieldRange(key + 3u, request.minimumSigma, request.maximumSigma);
            star.greenAmplitude = fieldRange(key + 4u, request.minimumAmplitude, request.maximumAmplitude);
            star.magnitudeG = 9.0 + 3.0 * (1.0 - (star.greenAmplitude - request.minimumAmplitude) /
                                                     (request.maximumAmplitude - request.minimumAmplitude));
            if (request.everyStarColored || (ordinal % 3u) != 0u)
            {
                star.colorIndexBpMinusRp = fieldRange(key + 5u, -0.3, 2.6);
            }
            const std::array<double, 3u> ratios = modelRatios(star.colorIndexBpMinusRp);
            star.redAmplitude = star.greenAmplitude * ratios[0] * request.redFactor;
            star.blueAmplitude = star.greenAmplitude * ratios[2] * request.blueFactor;
            field.stars.push_back(star);
        }
    }

    for (const PlantedColorStar& star : field.stars)
    {
        const std::array<double, 3u> amplitudes{star.redAmplitude, star.greenAmplitude, star.blueAmplitude};
        const double reach = 5.0 * star.sigma;
        const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
        const auto right = static_cast<std::int64_t>(std::ceil(star.x + reach));
        const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
        const auto bottom = static_cast<std::int64_t>(std::ceil(star.y + reach));
        const double inverse = 1.0 / (2.0 * star.sigma * star.sigma);
        for (std::int64_t row = (std::max)(std::int64_t{0}, top);
             row <= (std::min)(static_cast<std::int64_t>(request.height) - 1, bottom); ++row)
        {
            for (std::int64_t column = (std::max)(std::int64_t{0}, left);
                 column <= (std::min)(static_cast<std::int64_t>(request.width) - 1, right); ++column)
            {
                const double dx = static_cast<double>(column) + 0.5 - star.x;
                const double dy = static_cast<double>(row) + 0.5 - star.y;
                const double shape = std::exp(-(dx * dx + dy * dy) * inverse);
                const std::size_t at = static_cast<std::size_t>(row) * request.width + static_cast<std::size_t>(column);
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    field.samples[channel * planeSize + at] += static_cast<float>(amplitudes[channel] * shape);
                }
            }
        }
    }

    if (request.noise > 0.0)
    {
        for (std::size_t index = 0u; index < field.samples.size(); ++index)
        {
            field.samples[index] += static_cast<float>(
                request.noise * fieldNormal(request.seed * 1000003u + static_cast<std::uint64_t>(index)));
        }
    }
    return field;
}

/// A north-up tangent-plane solution over a frame, at a stated scale.
[[nodiscard]] inline an::WcsSolution
makeSolution(std::uint32_t width, std::uint32_t height, double scaleArcsecPerPixel = 2.4,
             an::SkyCoordinate center = {.rightAscensionDegrees = 83.6331, .declinationDegrees = 22.0145},
             double rotationDegrees = 17.0)
{
    const double angle = rotationDegrees * std::numbers::pi / 180.0;
    const double scaleDegrees = scaleArcsecPerPixel / 3600.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * width, .y = 0.5 * height};
    request.referencePoint = center;
    request.linear = {.cd11 = -scaleDegrees * cosine,
                      .cd12 = scaleDegrees * sine,
                      .cd21 = scaleDegrees * sine,
                      .cd22 = scaleDegrees * cosine};
    request.imageWidth = width;
    request.imageHeight = height;
    const an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.value();
}

/// The catalog entries a planted field's stars become under a solution, one per star in field order.
[[nodiscard]] inline std::vector<an::CatalogEntry> catalogEntriesOf(const ColorField& field,
                                                                    const an::WcsSolution& solution)
{
    std::vector<an::CatalogEntry> entries;
    entries.reserve(field.stars.size());
    for (std::size_t index = 0u; index < field.stars.size(); ++index)
    {
        const PlantedColorStar& star = field.stars[index];
        const an::Result<an::SkyCoordinate> direction = solution.pixelToWorld({.x = star.x, .y = star.y});
        EXPECT_TRUE(direction.hasValue()) << direction.status().message();
        an::CatalogEntry entry;
        entry.identifier = "Gaia DR3 " + std::to_string(4000000000000000000ull + index);
        entry.coordinate = direction.hasValue() ? *direction : an::SkyCoordinate{};
        entry.magnitude = star.magnitudeG;
        entries.push_back(std::move(entry));
    }
    return entries;
}

/// The colour indices beside those entries, in the same order.
[[nodiscard]] inline std::vector<std::optional<double>> colorIndicesOf(const ColorField& field)
{
    std::vector<std::optional<double>> colors;
    colors.reserve(field.stars.size());
    for (const PlantedColorStar& star : field.stars)
    {
        colors.push_back(star.colorIndexBpMinusRp);
    }
    return colors;
}

/// A catalog built from entries, failing the test rather than the caller when the build refuses.
[[nodiscard]] inline an::SkyCatalog
buildOrFail(const an::ExecutionContext& execution, std::vector<an::CatalogEntry> entries,
            const an::SkyCatalogRequest& request = {.photometricBand = "G", .buildNameIndex = false})
{
    an::Result<an::SkyCatalog> built = an::buildSkyCatalog(execution, std::move(entries), request);
    EXPECT_TRUE(built.hasValue()) << built.status().message();
    return built.hasValue() ? std::move(*built) : an::SkyCatalog{};
}

/// The mean of one channel over a rectangle of a planar Float32 field.
[[nodiscard]] inline double meanOver(const std::vector<float>& samples, std::uint32_t width, std::uint32_t height,
                                     std::uint32_t channel, std::uint32_t x0, std::uint32_t y0, std::uint32_t w,
                                     std::uint32_t h)
{
    const std::size_t planeSize = static_cast<std::size_t>(width) * height;
    double sum = 0.0;
    std::uint64_t count = 0u;
    for (std::uint32_t y = y0; y < y0 + h; ++y)
    {
        for (std::uint32_t x = x0; x < x0 + w; ++x)
        {
            sum += samples[channel * planeSize + static_cast<std::size_t>(y) * width + x];
            ++count;
        }
    }
    return count == 0u ? 0.0 : sum / static_cast<double>(count);
}

} // namespace color_test
