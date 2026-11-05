// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Colour conversion against the imaging library's `cv::cvtColor`, for the trip to CIE Lab and back.
//
// Both sides use D65 and the sRGB primaries. From linear RGB the two agree to a few thousandths of
// a unit, which settles the matrix, the white point and the cube-root compression. From
// sRGB-encoded RGB the library linearises the transfer through a 1,024-entry spline table whose
// error is largest near black, where the compression's slope is steepest; the sweep therefore also
// evaluates the standard formulas in double for every pixel and reports how far each side sits
// from them, so the tolerance the library needs is seen to be the library's; from linear RGB the
// residual is the library carrying its white point and matrix to six figures. Half a unit in a Lab
// coordinate is a quarter of the smallest difference an observer can see.

#include "ParityHarness.h"

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>

#include <opencv2/imgproc.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "color";

/// Interleaved RGB triples: seeded noise plus the corners, a grey ramp and a run of near-black
/// values, so the extremes and the steep end of the compression are in.
[[nodiscard]] std::vector<float> rgbPlane(PlaneSize size, std::uint64_t seed)
{
    Sequence sequence{seed};
    std::vector<float> samples(static_cast<std::size_t>(size.width) * size.height * 3u, 0.0f);
    for (float& sample : samples)
    {
        sample = sequence.next();
    }
    constexpr std::array<std::array<float, 3u>, 8u> kCorners{{{0.0f, 0.0f, 0.0f},
                                                              {1.0f, 1.0f, 1.0f},
                                                              {1.0f, 0.0f, 0.0f},
                                                              {0.0f, 1.0f, 0.0f},
                                                              {0.0f, 0.0f, 1.0f},
                                                              {0.5f, 0.5f, 0.5f},
                                                              {0.03f, 0.03f, 0.03f},
                                                              {0.9f, 0.1f, 0.6f}}};
    std::size_t pixel = 0u;
    for (const std::array<float, 3u>& corner : kCorners)
    {
        if ((pixel + 1u) * 3u > samples.size())
        {
            break;
        }
        samples[pixel * 3u] = corner[0];
        samples[pixel * 3u + 1u] = corner[1];
        samples[pixel * 3u + 2u] = corner[2];
        ++pixel;
    }
    for (std::size_t step = 0u; step < 32u && (pixel + 1u) * 3u <= samples.size(); ++step, ++pixel)
    {
        const float grey = static_cast<float>(step) / 31.0f;
        samples[pixel * 3u] = grey;
        samples[pixel * 3u + 1u] = grey;
        samples[pixel * 3u + 2u] = grey;
    }
    for (std::size_t step = 0u; step < 32u && (pixel + 1u) * 3u <= samples.size(); ++step, ++pixel)
    {
        samples[pixel * 3u] = static_cast<float>(step) / 2000.0f;
        samples[pixel * 3u + 1u] = static_cast<float>(step) / 3000.0f;
        samples[pixel * 3u + 2u] = static_cast<float>(step) / 1500.0f;
    }
    return samples;
}

[[nodiscard]] cv::Mat colourMatrixOf(std::span<const float> samples, PlaneSize size)
{
    cv::Mat matrix(static_cast<int>(size.height), static_cast<int>(size.width), CV_32FC3);
    for (std::uint32_t y = 0u; y < size.height; ++y)
    {
        std::copy_n(samples.data() + static_cast<std::size_t>(y) * size.width * 3u,
                    static_cast<std::size_t>(size.width) * 3u, matrix.ptr<float>(static_cast<int>(y)));
    }
    return matrix;
}

[[nodiscard]] std::vector<float> colourPlaneOf(const cv::Mat& matrix)
{
    EXPECT_EQ(matrix.type(), CV_32FC3);
    std::vector<float> samples(static_cast<std::size_t>(matrix.rows) * matrix.cols * 3u, 0.0f);
    for (int y = 0; y < matrix.rows; ++y)
    {
        std::copy_n(matrix.ptr<float>(y), static_cast<std::size_t>(matrix.cols) * 3u,
                    samples.data() + static_cast<std::size_t>(y) * matrix.cols * 3u);
    }
    return samples;
}

[[nodiscard]] std::vector<float> engineConvert(const an::ExecutionContext& execution, std::span<const float> source,
                                               PlaneSize size, const an::ColorEncoding& from,
                                               const an::ColorEncoding& to)
{
    const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(from, to);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> destination(source.size(), -7.0f);
    if (!plan.hasValue())
    {
        return destination;
    }
    const an::ImageLayout layout = interleavedLayout(an::SampleType::Float32, size.width, size.height, 3u);
    const an::Status ran = an::convertImageColor(execution, *plan, an::SampleType::Float32, source.data(), layout,
                                                 destination.data(), layout);
    EXPECT_TRUE(ran.ok()) << ran.message();
    return destination;
}

/// One channel of an interleaved triple as a plane, so each Lab coordinate reports its own line.
[[nodiscard]] std::vector<float> channelOf(std::span<const float> samples, std::size_t channel)
{
    std::vector<float> plane(samples.size() / 3u, 0.0f);
    for (std::size_t pixel = 0u; pixel < plane.size(); ++pixel)
    {
        plane[pixel] = samples[pixel * 3u + channel];
    }
    return plane;
}

/// The sRGB transfer, inverted, in double.
[[nodiscard]] double linearOf(double encoded)
{
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

/// CIE Lab under D65 from the standard formulas in double: the independent reference each side is
/// measured against.
[[nodiscard]] std::vector<float> exactLab(std::span<const float> rgb, bool srgbEncoded)
{
    std::vector<float> lab(rgb.size(), 0.0f);
    const auto compress = [](double t) {
        constexpr double kThreshold = 216.0 / 24389.0;
        return t > kThreshold ? std::cbrt(t) : (841.0 / 108.0) * t + 4.0 / 29.0;
    };
    for (std::size_t pixel = 0u; pixel * 3u + 2u < rgb.size(); ++pixel)
    {
        double r = rgb[pixel * 3u];
        double g = rgb[pixel * 3u + 1u];
        double b = rgb[pixel * 3u + 2u];
        if (srgbEncoded)
        {
            r = linearOf(r);
            g = linearOf(g);
            b = linearOf(b);
        }
        const double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
        const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
        const double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
        const double fx = compress(x / 0.95047);
        const double fy = compress(y);
        const double fz = compress(z / 1.08883);
        lab[pixel * 3u] = static_cast<float>(116.0 * fy - 16.0);
        lab[pixel * 3u + 1u] = static_cast<float>(500.0 * (fx - fy));
        lab[pixel * 3u + 2u] = static_cast<float>(200.0 * (fy - fz));
    }
    return lab;
}

struct TransferPair
{
    an::ColorTransferFunction engine;
    int toLab;
    int fromLab;
    const char* name;
    double labTolerance;
};

constexpr std::array<TransferPair, 2u> kTransfers{{
    {an::ColorTransferFunction::Srgb, cv::COLOR_RGB2Lab, cv::COLOR_Lab2RGB, "srgb", 0.5},
    {an::ColorTransferFunction::Linear, cv::COLOR_LRGB2Lab, cv::COLOR_Lab2LRGB, "linear", 0.05},
}};

constexpr std::array<const char*, 3u> kCoordinates{"L", "a", "b"};

TEST(ColorParityTest, RgbToLabAgreesWithTheLibraryToTheLibrarysTransferTableAndBothToTheStandardFormulas)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<PlaneSize, 3u> kSizes{{{16u, 9u}, {64u, 48u}, {127u, 61u}}};
    for (const TransferPair& transfer : kTransfers)
    {
        const an::ColorEncoding rgb{.schema = an::ChannelSchema::Rgb,
                                    .primaries = an::RgbPrimaries::Srgb,
                                    .transfer = transfer.engine,
                                    .whitePoint = an::ChromaticWhitePoint::D65};
        const an::ColorEncoding lab{.schema = an::ChannelSchema::Lab,
                                    .primaries = an::RgbPrimaries::Srgb,
                                    .transfer = an::ColorTransferFunction::Linear,
                                    .whitePoint = an::ChromaticWhitePoint::D65,
                                    .perceptual = an::PerceptualEncoding::NaturalUnits};
        for (const PlaneSize size : kSizes)
        {
            const std::vector<float> source = rgbPlane(size, 10000u + size.width);
            const std::vector<float> oursLab = engineConvert(execution, source, size, rgb, lab);
            cv::Mat libraryLab;
            cv::cvtColor(colourMatrixOf(source, size), libraryLab, transfer.toLab);
            const std::vector<float> theirsLab = colourPlaneOf(libraryLab);
            const std::vector<float> reference = exactLab(source, transfer.engine == an::ColorTransferFunction::Srgb);
            const std::string label = std::string("rgb(") + transfer.name + ")->lab " + sizeLabel(size);
            for (std::size_t coordinate = 0u; coordinate < 3u; ++coordinate)
            {
                const std::vector<float> ours = channelOf(oursLab, coordinate);
                const std::vector<float> theirs = channelOf(theirsLab, coordinate);
                const std::vector<float> exact = channelOf(reference, coordinate);
                expectParity(kDomain, label + " " + kCoordinates[coordinate], ours, theirs, transfer.labTolerance);
                const Comparison oursFromExact = compare(ours, exact, 1.0e9, 0.0);
                const Comparison theirsFromExact = compare(theirs, exact, 1.0e9, 0.0);
                std::cout << "parity | " << kDomain << " | " << label << " " << kCoordinates[coordinate]
                          << " | from the standard formulas: engine maxAbs=" << oursFromExact.maxAbs
                          << " meanAbs=" << oursFromExact.meanAbs << " | library maxAbs=" << theirsFromExact.maxAbs
                          << " meanAbs=" << theirsFromExact.meanAbs << "\n";
            }

            // Back from the library's Lab, so both sides start from the same triples.
            const std::vector<float> oursRgb = engineConvert(execution, theirsLab, size, lab, rgb);
            cv::Mat libraryRgb;
            cv::cvtColor(libraryLab, libraryRgb, transfer.fromLab);
            expectParity(kDomain, std::string("lab->rgb(") + transfer.name + ") " + sizeLabel(size), oursRgb,
                         colourPlaneOf(libraryRgb), 1.0e-3);
        }
    }
}

} // namespace
