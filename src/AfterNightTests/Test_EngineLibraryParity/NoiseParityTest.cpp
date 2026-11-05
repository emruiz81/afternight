// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The bilateral filter against the imaging library's `cv::bilateralFilter`.
//
// The two read the same circular window with the same spatial weights and the same border rule. They
// part on the range weight: this engine evaluates the exponential, the library reads it from a table
// of 256 bins spanning the image's range with linear interpolation between bins. The interpolation
// error of a Gaussian sampled every `delta` is at most `(delta / sigma)^2 / 8` of the weight, and a
// weight error moves an output by at most that fraction of the window's contrast, so the tolerance
// is that bound over the image's range - the library's own approximation, not a number chosen to
// pass. A range sigma wide against the bins lands near single precision; a narrow one shows the
// table.

#include "ParityHarness.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>

#include <opencv2/imgproc.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "noise";

struct BorderPair
{
    an::KernelBorderMode engine;
    int library;
    const char* name;
};

constexpr std::array<BorderPair, 2u> kBorders{{
    {an::KernelBorderMode::ReflectExcludingEdge, cv::BORDER_REFLECT_101, "reflect-101"},
    {an::KernelBorderMode::Clamp, cv::BORDER_REPLICATE, "replicate"},
}};

TEST(NoiseParityTest, TheBilateralFilterAgreesWithTheLibraryToItsRangeTablesInterpolationBound)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::array<double, 4u> kRangeSigmas{0.02, 0.05, 0.1, 0.3};
    constexpr std::array<double, 3u> kSpatialSigmas{1.0, 2.0, 3.0};
    constexpr std::array<std::uint32_t, 4u> kRadii{1u, 2u, 3u, 5u};
    constexpr std::array<PlaneSize, 4u> kSizes{{{7u, 5u}, {33u, 21u}, {64u, 48u}, {127u, 61u}}};
    for (const double rangeSigma : kRangeSigmas)
    {
        for (const double spatialSigma : kSpatialSigmas)
        {
            for (const std::uint32_t radius : kRadii)
            {
                for (const BorderPair& border : kBorders)
                {
                    const an::Result<an::BilateralPlan> plan =
                        an::planBilateralFilter({.spatialSigma = spatialSigma,
                                                 .rangeSigma = rangeSigma,
                                                 .radius = radius,
                                                 .borders = {.border = border.engine}});
                    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                    for (std::size_t sizeIndex = 0u; sizeIndex < kSizes.size(); ++sizeIndex)
                    {
                        const PlaneSize size = kSizes[sizeIndex];
                        const std::vector<float> source =
                            sizeIndex % 2u == 0u ? uniformPlane(size.width, size.height, 8000u + size.width)
                                                 : smoothPlane(size.width, size.height, 8000u + size.width);
                        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, size.width, size.height);
                        std::vector<float> ours(source.size(), -7.0f);
                        const an::Result<an::NoiseReductionResult> ran = an::filterImageBilateral(
                            execution, an::SampleType::Float32, source.data(), layout, ours.data(), layout, *plan);
                        ASSERT_TRUE(ran.hasValue()) << ran.status().message();

                        const cv::Mat input = matrixOf(source, size.width, size.height);
                        cv::Mat output;
                        cv::bilateralFilter(input, output, static_cast<int>(2u * radius + 1u), rangeSigma, spatialSigma,
                                            border.library);

                        const auto [lowest, highest] = std::minmax_element(source.begin(), source.end());
                        const double range = static_cast<double>(*highest) - static_cast<double>(*lowest);
                        const double binWidth = range / 256.0;
                        const double tolerance =
                            (binWidth / rangeSigma) * (binWidth / rangeSigma) / 8.0 * range + 2.0e-6;
                        expectParity(kDomain,
                                     "bilateral rs=" + std::to_string(rangeSigma) +
                                         " ss=" + std::to_string(spatialSigma) + " r=" + std::to_string(radius) + " " +
                                         border.name + " " + sizeLabel(size) +
                                         (sizeIndex % 2u == 0u ? " uniform" : " smooth"),
                                     ours, planeOf(output), tolerance);
                    }
                }
            }
        }
    }
}

} // namespace
