// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The two operations this engine took over from the imaging library it used to call through a
// Python interpreter, against that library's own recorded answers.
//
// The library is a Python package and cannot be linked here, so its answers are recorded by
// `build/scripts/tools/generate_skimage_goldens.py` into the header beside this file. Everything
// else about this sweep is what the rest of the suite does: the library's answer on a stated input,
// this engine's answer on the same input, and a tolerance that is the reason the two may differ at
// all rather than a number chosen to pass.
//
// **The unsharp mask is meant to be the same operation.** Both compute `source + amount * (source -
// gaussian(source))` and clip to the unit interval; both truncate the Gaussian at four sigmas,
// renormalize the truncated taps and reflect about the edge sample. The sigmas in the sweep are the
// ones where the two truncation roundings - this engine's `ceil(4 sigma)` and the library's
// `int(4 sigma + 0.5)` - give the same radius, so a difference in radius cannot hide inside the
// tolerance. What remains is the order two separable passes accumulate in at single precision.
//
// **The total-variation denoiser is meant to be the same iteration.** Both run Chambolle's
// projection with a step of one over twice the dimension count, the same forward gradient with the
// last row and column at zero, and the same divergence. The cases run with a zero tolerance so both
// take exactly the stated number of iterations, which is what separates the arithmetic from the
// stopping rule. The error a fixed-point iteration carries compounds over its iterations, so the
// tolerance grows with the iteration count and is stated per case.

#include "ParityHarness.h"
#include "SkimageGoldens.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/TotalVariationDenoise.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "skimage";

/// The fixture both sides were given, as a plane.
///
/// @return the samples.
[[nodiscard]] std::vector<float> fixturePlane()
{
    return std::vector<float>(skimage_goldens::kPlane.begin(), skimage_goldens::kPlane.end());
}

/// One recorded case, as a plane.
///
/// @param recorded the recorded array.
/// @return the samples.
[[nodiscard]] std::vector<float> goldenPlane(std::span<const float> recorded)
{
    return std::vector<float>(recorded.begin(), recorded.end());
}

/// This engine's unsharp mask over the fixture.
///
/// @param execution the operation to run under.
/// @param sigma the Gaussian standard deviation, which the library calls a radius.
/// @param amount the detail multiplier.
/// @return the samples, or an empty plane after a failed expectation.
[[nodiscard]] std::vector<float> engineUnsharp(const an::ExecutionContext& execution, const double sigma,
                                               const double amount)
{
    an::DetailFilterRequest request;
    request.method = an::DetailFilterMethod::UnsharpMask;
    request.sigmaX = sigma;
    request.sigmaY = sigma;
    request.amount = amount;
    request.offset = 0.0;
    request.lowest = 0.0;
    request.highest = 1.0;
    // The library blurs with `mode='reflect'`, which mirrors about a line half a sample outside the
    // image so that the edge sample repeats.
    request.borders = an::StencilBorderPolicy{.border = an::KernelBorderMode::Reflect};

    const an::Result<an::DetailFilterPlan> plan = an::planDetailFilter(request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return {};
    }
    const std::vector<float> source = fixturePlane();
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout =
        planarLayout(an::SampleType::Float32, skimage_goldens::kPlaneWidth, skimage_goldens::kPlaneHeight, 1u);
    const an::Result<an::SpatialFilterResult> filtered = an::filterImageDetail(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *plan);
    EXPECT_TRUE(filtered.hasValue()) << filtered.status().message();
    return destination;
}

/// This engine's Chambolle denoise over the fixture.
///
/// @param execution the operation to run under.
/// @param weight the denoising weight.
/// @param iterations how many iterations to run, exactly.
/// @return the samples, or an empty plane after a failed expectation.
[[nodiscard]] std::vector<float> engineChambolle(const an::ExecutionContext& execution, const double weight,
                                                 const std::uint32_t iterations)
{
    an::TotalVariationRequest request;
    request.method = an::TotalVariationMethod::Chambolle;
    request.weight = weight;
    request.maximumIterations = iterations;
    request.tolerance = 0.0;

    const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    if (!plan.hasValue())
    {
        return {};
    }
    const std::vector<float> source = fixturePlane();
    std::vector<float> destination(source.size(), 0.0f);
    const an::ImageLayout layout =
        planarLayout(an::SampleType::Float32, skimage_goldens::kPlaneWidth, skimage_goldens::kPlaneHeight, 1u);
    const an::Result<an::TotalVariationResult> denoised = an::denoiseImageTotalVariation(
        execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *plan);
    EXPECT_TRUE(denoised.hasValue()) << denoised.status().message();
    if (denoised.hasValue())
    {
        EXPECT_EQ(denoised->iterationsRun, iterations);
        EXPECT_FALSE(denoised->converged);
    }
    return destination;
}

TEST(SkimageParityTest, TheUnsharpMaskAgreesWithTheLibraryToTheRoundingOfTwoSeparablePasses)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Two separable passes over a kernel of at most seventeen taps, at single precision, over values
    // of at most one, with the detail multiplied by at most two and a half afterwards.
    constexpr double kPassRounding = 4.0e-6;

    expectParity(kDomain, "unsharp | sigma=1 amount=1", engineUnsharp(execution, 1.0, 1.0),
                 goldenPlane(skimage_goldens::kUnsharpR1A1), kPassRounding);
    expectParity(kDomain, "unsharp | sigma=1 amount=2.5", engineUnsharp(execution, 1.0, 2.5),
                 goldenPlane(skimage_goldens::kUnsharpR1A25), kPassRounding);
    expectParity(kDomain, "unsharp | sigma=2 amount=1", engineUnsharp(execution, 2.0, 1.0),
                 goldenPlane(skimage_goldens::kUnsharpR2A1), kPassRounding);
    expectParity(kDomain, "unsharp | sigma=2 amount=0.5", engineUnsharp(execution, 2.0, 0.5),
                 goldenPlane(skimage_goldens::kUnsharpR2A05), kPassRounding);
}

TEST(SkimageParityTest, TheTotalVariationDenoiserAgreesWithTheLibrarysOwnIteration)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // A fixed-point iteration carries its rounding forward, so the allowance is one single-precision
    // step per iteration over values of at most one - which is what these two numbers are.
    constexpr double kFiftyIterations = 6.0e-6;
    constexpr double kTwoHundredIterations = 2.4e-5;

    expectParity(kDomain, "chambolle | weight=0.05 iterations=50", engineChambolle(execution, 0.05, 50u),
                 goldenPlane(skimage_goldens::kChambolleW005I50), kFiftyIterations);
    expectParity(kDomain, "chambolle | weight=0.2 iterations=50", engineChambolle(execution, 0.2, 50u),
                 goldenPlane(skimage_goldens::kChambolleW02I50), kFiftyIterations);
    expectParity(kDomain, "chambolle | weight=0.05 iterations=200", engineChambolle(execution, 0.05, 200u),
                 goldenPlane(skimage_goldens::kChambolleW005I200), kTwoHundredIterations);
}

TEST(SkimageParityTest, TheRecordedFixtureIsTheOneTheGeneratorDescribes)
{
    // A recording whose input drifted from what the generator describes would disagree with the
    // engine everywhere, and the sweep above would read as a numeric failure rather than as a stale
    // file. The fixture is a function of the generator's own lines and nothing else, so those lines
    // are written here too and the two have to meet. Only the exponential can differ between the two
    // languages' libraries, and it differs at the last bit of a double.
    ASSERT_EQ(skimage_goldens::kPlane.size(),
              static_cast<std::size_t>(skimage_goldens::kPlaneWidth) * skimage_goldens::kPlaneHeight);
    std::uint64_t state = 20260920u;
    std::size_t index = 0u;
    for (std::uint32_t y = 0u; y < skimage_goldens::kPlaneHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < skimage_goldens::kPlaneWidth; ++x, ++index)
        {
            state = (state * 1103515245u + 12345u) & 0x7FFFFFFFu;
            const double speckle = static_cast<double>(state) / static_cast<double>(0x7FFFFFFF) - 0.5;
            const double gradient =
                0.25 + 0.5 * (static_cast<double>(x) / static_cast<double>(skimage_goldens::kPlaneWidth - 1u));
            const double dx1 = static_cast<double>(x) - 7.0;
            const double dy1 = static_cast<double>(y) - 6.0;
            const double dx2 = static_cast<double>(x) - 17.0;
            const double dy2 = static_cast<double>(y) - 12.0;
            const double first = 0.3 * std::exp(-((dx1 * dx1 + dy1 * dy1) / 12.0));
            const double second = -0.2 * std::exp(-((dx2 * dx2 + dy2 * dy2) / 20.0));
            const double value = gradient + first + second + 0.06 * speckle;
            const double held = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
            EXPECT_NEAR(skimage_goldens::kPlane[index], static_cast<float>(held), 1.0e-6f)
                << "at (" << x << ", " << y << ")";
        }
    }
}

} // namespace
