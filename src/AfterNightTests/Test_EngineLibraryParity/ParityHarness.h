// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Test_EngineLibraryParity - the differential sweep against the libraries whose operations this
// engine computes itself.
//
// Where OpenCV or GSL implements an operation this engine computes itself, this suite runs both on
// the same inputs - seeded, over sizes, parameters and border rules - and
// holds the engine to a stated distance from the library. The distance is never "whatever came
// out": each case names why the two may differ at all - the library's lookup table, its fixed-point
// coordinate step, its single-precision scale - and the tolerance is that reason's bound, so a
// difference the reason does not explain fails the sweep.
//
// The suite links the two libraries for their answers only. Nothing of the engine includes them:
// the engine's own headers and sources are exactly what every other suite sees.
//
// This header is the harness the domain files share: the engine and execution contexts, layouts,
// seeded fixtures, the comparison and the one line it prints per case so a run's log doubles as
// the measurement record.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <opencv2/core.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace parity_test
{

/// One engine context for a sweep.
///
/// @param concurrency how many workers the context may lend.
/// @return the context, or an empty one after a failed expectation.
[[nodiscard]] inline an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One execution for a sweep.
///
/// @param engine the context the execution leases.
/// @param concurrency the share of workers it asks for.
/// @param backend which implementation family it prefers.
/// @return the execution, or an empty one after a failed expectation.
[[nodiscard]] inline an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 3u,
                                                        an::ComputeBackend backend = an::ComputeBackend::Automatic)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A packed planar layout: one plane per channel, rows without padding.
///
/// @param type the representation.
/// @param width samples per row.
/// @param height rows.
/// @param channels planes.
/// @return the layout.
[[nodiscard]] inline an::ImageLayout planarLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                  std::uint32_t channels = 1u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A packed interleaved layout: every pixel's samples side by side, which is how the imaging library
/// holds a colour image.
///
/// @param type the representation.
/// @param width pixels per row.
/// @param height rows.
/// @param channels samples per pixel.
/// @return the layout.
[[nodiscard]] inline an::ImageLayout interleavedLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                       std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = channels,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width) * channels * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A single-channel descriptor for a geometry plan.
///
/// @param width samples per row.
/// @param height rows.
/// @param sampleType the representation.
/// @return the descriptor.
[[nodiscard]] inline an::ImageDescriptor monoDescriptor(std::uint32_t width, std::uint32_t height,
                                                        an::SampleType sampleType = an::SampleType::Float32)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::None, 0u, 0u);
    EXPECT_TRUE(cfa.has_value());
    const std::optional<an::ImageDescriptor> made =
        an::ImageDescriptor::create({.extent = *extent,
                                     .sampleType = sampleType,
                                     .channelSchema = an::ChannelSchema::Mono,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::RawSensorCode,
                                     .cfa = *cfa,
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    EXPECT_TRUE(made.has_value());
    return *made;
}

/// A seeded generator whose sequence is the same on every host and every run.
class Sequence
{
public:
    explicit Sequence(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    /// The next value, uniform in [0, 1).
    [[nodiscard]] float next() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>((m_state >> 40) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    /// The next value, uniform in [0, 1), at double resolution.
    [[nodiscard]] double nextDouble() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(m_state >> 11) / 9007199254740992.0;
    }

private:
    std::uint64_t m_state;
};

/// A plane of uniform noise in [0, 1): the worst case for any tolerance, because nothing is smooth.
///
/// @param width samples per row.
/// @param height rows.
/// @param seed which plane.
/// @return the samples, row-major.
[[nodiscard]] inline std::vector<float> uniformPlane(std::uint32_t width, std::uint32_t height, std::uint64_t seed)
{
    Sequence sequence{seed};
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (float& sample : samples)
    {
        sample = sequence.next();
    }
    return samples;
}

/// A smooth plane in [0, 1]: a sky gradient, a few broad waves and a handful of Gaussian stars.
///
/// Its gradients are bounded, which is what makes a fixed-point coordinate error bound meaningful,
/// and its stars give a registration something to lock on to.
///
/// @param width samples per row.
/// @param height rows.
/// @param seed which plane.
/// @param shiftX how far the stars sit to the right of their seeded positions, in pixels.
/// @param shiftY how far the stars sit below their seeded positions, in pixels.
/// @return the samples, row-major.
[[nodiscard]] inline std::vector<float> smoothPlane(std::uint32_t width, std::uint32_t height, std::uint64_t seed,
                                                    double shiftX = 0.0, double shiftY = 0.0)
{
    Sequence sequence{seed};
    constexpr std::size_t kStars = 24u;
    std::vector<double> starX(kStars);
    std::vector<double> starY(kStars);
    std::vector<double> starAmplitude(kStars);
    std::vector<double> starSigma(kStars);
    for (std::size_t star = 0u; star < kStars; ++star)
    {
        starX[star] = 3.0 + sequence.nextDouble() * (static_cast<double>(width) - 6.0) + shiftX;
        starY[star] = 3.0 + sequence.nextDouble() * (static_cast<double>(height) - 6.0) + shiftY;
        starAmplitude[star] = 0.15 + 0.5 * sequence.nextDouble();
        starSigma[star] = 1.1 + 1.2 * sequence.nextDouble();
    }
    const double waveX = 0.7 + sequence.nextDouble();
    const double waveY = 0.7 + sequence.nextDouble();
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double u = static_cast<double>(x) / (std::max)(1u, width - 1u);
            const double v = static_cast<double>(y) / (std::max)(1u, height - 1u);
            double value = 0.12 + 0.08 * u + 0.05 * v +
                           0.04 * std::sin(6.283185307179586 * waveX * u) * std::cos(6.283185307179586 * waveY * v);
            for (std::size_t star = 0u; star < kStars; ++star)
            {
                const double dx = static_cast<double>(x) - starX[star];
                const double dy = static_cast<double>(y) - starY[star];
                value +=
                    starAmplitude[star] * std::exp(-(dx * dx + dy * dy) / (2.0 * starSigma[star] * starSigma[star]));
            }
            samples[static_cast<std::size_t>(y) * width + x] = static_cast<float>((std::min)(1.0, value));
        }
    }
    return samples;
}

/// A plane of exactly representable code values in [0, 1]: `k / (levels - 1)` for a seeded `k`.
///
/// @param width samples per row.
/// @param height rows.
/// @param seed which plane.
/// @param levels how many codes, 256 or 65536.
/// @param codes where the codes go, in the same order.
/// @return the samples, row-major.
[[nodiscard]] inline std::vector<float> quantizedPlane(std::uint32_t width, std::uint32_t height, std::uint64_t seed,
                                                       std::uint32_t levels, std::vector<std::uint32_t>& codes)
{
    const std::vector<float> smooth = smoothPlane(width, height, seed);
    std::vector<float> samples(smooth.size(), 0.0f);
    codes.assign(smooth.size(), 0u);
    Sequence sequence{seed ^ 0x9E3779B97F4A7C15ull};
    for (std::size_t index = 0u; index < smooth.size(); ++index)
    {
        const double noisy = (std::min)(0.999999, (std::max)(0.0, static_cast<double>(smooth[index]) +
                                                                      0.05 * (sequence.nextDouble() - 0.5)));
        const auto code = static_cast<std::uint32_t>(noisy * levels);
        codes[index] = code;
        samples[index] = static_cast<float>(static_cast<double>(code) / (levels - 1u));
    }
    return samples;
}

/// What one comparison found.
struct Comparison
{
    /// Largest absolute difference.
    double maxAbs = 0.0;

    /// Largest difference relative to the larger magnitude, over samples not both tiny.
    double maxRel = 0.0;

    /// Mean absolute difference.
    double meanAbs = 0.0;

    /// How many samples lay beyond the stated tolerance.
    std::size_t beyond = 0u;

    /// Which sample lay furthest beyond it, or the largest difference when none did.
    std::size_t worst = 0u;

    /// How many samples were compared.
    std::size_t count = 0u;

    /// The engine's value at `worst`.
    double engineAtWorst = 0.0;

    /// The library's value at `worst`.
    double libraryAtWorst = 0.0;
};

/// Compares an engine answer against a library answer under a stated tolerance.
///
/// A sample is beyond tolerance when `|engine - library| > tolAbs + tolRel * |library|`. A
/// not-a-number on either side counts as beyond unless both are.
///
/// @param engine what this engine produced.
/// @param library what the library produced, the same length.
/// @param tolAbs the absolute allowance.
/// @param tolRel the allowance proportional to the library's magnitude.
/// @return the findings.
[[nodiscard]] inline Comparison compare(std::span<const float> engine, std::span<const float> library, double tolAbs,
                                        double tolRel)
{
    Comparison found;
    found.count = (std::min)(engine.size(), library.size());
    EXPECT_EQ(engine.size(), library.size());
    double sum = 0.0;
    double worstExcess = -1.0;
    for (std::size_t index = 0u; index < found.count; ++index)
    {
        const double a = engine[index];
        const double b = library[index];
        const bool aNaN = std::isnan(a);
        const bool bNaN = std::isnan(b);
        if (aNaN || bNaN)
        {
            if (aNaN != bNaN)
            {
                ++found.beyond;
                found.worst = index;
                found.engineAtWorst = a;
                found.libraryAtWorst = b;
                worstExcess = std::numeric_limits<double>::infinity();
                found.maxAbs = std::numeric_limits<double>::infinity();
            }
            continue;
        }
        const double difference = std::abs(a - b);
        sum += difference;
        found.maxAbs = (std::max)(found.maxAbs, difference);
        const double magnitude = (std::max)(std::abs(a), std::abs(b));
        if (magnitude > 1.0e-6)
        {
            found.maxRel = (std::max)(found.maxRel, difference / magnitude);
        }
        const double excess = difference - (tolAbs + tolRel * std::abs(b));
        if (excess > 0.0)
        {
            ++found.beyond;
        }
        if (excess > worstExcess)
        {
            worstExcess = excess;
            found.worst = index;
            found.engineAtWorst = a;
            found.libraryAtWorst = b;
        }
    }
    found.meanAbs = found.count > 0u ? sum / static_cast<double>(found.count) : 0.0;
    return found;
}

/// Prints the one line per case that makes a run's log the measurement record.
///
/// @param domain which sweep.
/// @param label which case.
/// @param found what the comparison found.
/// @param tolAbs the absolute allowance it was held to.
/// @param tolRel the relative allowance it was held to.
inline void report(const char* domain, const std::string& label, const Comparison& found, double tolAbs, double tolRel)
{
    std::cout << "parity | " << domain << " | " << label << " | n=" << found.count << " | maxAbs=" << found.maxAbs
              << " | maxRel=" << found.maxRel << " | meanAbs=" << found.meanAbs << " | beyond=" << found.beyond
              << " | tol=" << tolAbs << "+" << tolRel << "*|x|" << (found.beyond != 0u ? " | FAIL" : "") << "\n";
}

/// Runs a comparison, prints its line and expects nothing beyond the tolerance.
///
/// @param domain which sweep.
/// @param label which case.
/// @param engine what this engine produced.
/// @param library what the library produced.
/// @param tolAbs the absolute allowance.
/// @param tolRel the relative allowance.
/// @return the findings, for a caller that wants to examine them further.
inline Comparison expectParity(const char* domain, const std::string& label, std::span<const float> engine,
                               std::span<const float> library, double tolAbs, double tolRel = 0.0)
{
    const Comparison found = compare(engine, library, tolAbs, tolRel);
    report(domain, label, found, tolAbs, tolRel);
    EXPECT_EQ(found.beyond, 0u) << domain << " / " << label << ": " << found.beyond << " of " << found.count
                                << " samples beyond " << tolAbs << " + " << tolRel << " * |library|; worst at "
                                << found.worst << ": engine " << found.engineAtWorst << " against library "
                                << found.libraryAtWorst << " (max abs " << found.maxAbs << ", max rel " << found.maxRel
                                << ")";
    return found;
}

/// A single-channel Float32 matrix holding a copy of a plane.
///
/// @param samples the plane, row-major and packed.
/// @param width samples per row.
/// @param height rows.
/// @return the matrix, owning its own copy.
[[nodiscard]] inline cv::Mat matrixOf(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    cv::Mat matrix(static_cast<int>(height), static_cast<int>(width), CV_32FC1);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        std::copy_n(samples.data() + static_cast<std::size_t>(y) * width, width,
                    matrix.ptr<float>(static_cast<int>(y)));
    }
    return matrix;
}

/// The samples of a single-channel Float32 matrix, row-major and packed.
///
/// @param matrix the matrix.
/// @return the samples.
[[nodiscard]] inline std::vector<float> planeOf(const cv::Mat& matrix)
{
    EXPECT_EQ(matrix.type(), CV_32FC1);
    std::vector<float> samples(static_cast<std::size_t>(matrix.rows) * static_cast<std::size_t>(matrix.cols), 0.0f);
    for (int y = 0; y < matrix.rows; ++y)
    {
        std::copy_n(matrix.ptr<float>(y), matrix.cols, samples.data() + static_cast<std::size_t>(y) * matrix.cols);
    }
    return samples;
}

/// A plane size for a sweep.
struct PlaneSize
{
    std::uint32_t width;
    std::uint32_t height;
};

/// The sizes every sweep walks: tiny, odd, and large enough for the vector paths to run.
inline constexpr PlaneSize kSweepSizes[] = {{7u, 5u}, {16u, 9u}, {33u, 21u}, {64u, 48u}, {127u, 61u}, {256u, 192u}};

/// A label for a case.
///
/// @param size the plane size.
/// @return `WxH`.
[[nodiscard]] inline std::string sizeLabel(PlaneSize size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

} // namespace parity_test
