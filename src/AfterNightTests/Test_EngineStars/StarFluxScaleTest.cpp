// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The photometric scale between two frames, spelled as a composition.
//
// A local-normalization scale from stars detects them on both frames, keeps the unsaturated ones
// with enough signal, pairs each reference star with the nearest target star, and takes a robust
// median of their flux ratios, falling back to a residual estimator below sixteen pairs. Here that
// answer is the detector on each frame, a caller-side pairing by position, the stated flux estimator
// of each star and the median of the ratios; the fallback is the normalization domain's own scale
// estimate. This case proves the composition recovers a planted factor, so no facade has to be
// written for it.

#include <AfterNightEngine/Algorithms/Photometry/PhotometricCalibration.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "StarFieldFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

constexpr double kPlantedFactor = 1.25;

/// The floor on pairs before a star-derived scale is trusted.
constexpr std::size_t kMinimumPairs = 16u;

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageDescriptor descriptorOf(const std::uint32_t width, const std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = extent.value_or(an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits()).value()),
        .sampleType = an::SampleType::Float32,
        .channelSchema = an::ChannelSchema::Mono,
        .colorSpace = an::ColorSpace::Unspecified,
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value();
}

[[nodiscard]] an::ImageLayout layoutOf(const an::ImageDescriptor& descriptor)
{
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(descriptor, an::StorageArrangement::Planar, 1u);
    EXPECT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

[[nodiscard]] std::vector<an::DetectedStar> detect(const an::ExecutionContext& execution,
                                                   const std::vector<float>& samples,
                                                   const an::ImageDescriptor& descriptor, const an::ImageLayout& layout)
{
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(samples.data(), layout.totalBytes(), layout, descriptor);
    EXPECT_TRUE(frame.hasValue()) << frame.status().message();
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(an::StarDetectionRequest{}, descriptor, layout);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    an::StarDetectionResult detected;
    if (frame.hasValue() && plan.hasValue())
    {
        const an::Status ran = an::detectStars(execution, *frame, *plan, detected);
        EXPECT_TRUE(ran.ok()) << ran.message();
    }
    return std::move(detected.stars);
}

/// The admission rule for a star that takes part in the scale: unsaturated, with a finite positive
/// amplitude and a position inside the frame.
[[nodiscard]] bool admitted(const an::DetectedStar& star, const std::uint32_t width, const std::uint32_t height)
{
    return !star.psf.saturated && std::isfinite(star.psf.amplitude) && star.psf.amplitude > 0.0 &&
           std::isfinite(star.positionX) && std::isfinite(star.positionY) && star.positionX >= 0.0 &&
           star.positionY >= 0.0 && star.positionX <= width && star.positionY <= height;
}

/// The ratio of each reference star's flux to the nearest target star's within the radius, in the
/// order the reference stars come.
[[nodiscard]] std::vector<double> fluxRatios(std::span<const an::DetectedStar> reference,
                                             std::span<const an::DetectedStar> target, const double radius,
                                             const std::uint32_t width, const std::uint32_t height)
{
    std::vector<double> ratios;
    std::vector<bool> used(target.size(), false);
    for (const an::DetectedStar& star : reference)
    {
        if (!admitted(star, width, height))
        {
            continue;
        }
        std::size_t best = target.size();
        double bestDistance = radius;
        for (std::size_t index = 0u; index < target.size(); ++index)
        {
            if (used[index] || !admitted(target[index], width, height))
            {
                continue;
            }
            const double distance =
                std::hypot(target[index].positionX - star.positionX, target[index].positionY - star.positionY);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = index;
            }
        }
        if (best == target.size())
        {
            continue;
        }
        used[best] = true;
        const std::optional<double> referenceFlux = an::starInstrumentalFlux(star, an::StarFluxEstimator::PsfVolume);
        const std::optional<double> targetFlux =
            an::starInstrumentalFlux(target[best], an::StarFluxEstimator::PsfVolume);
        if (!referenceFlux.has_value() || !targetFlux.has_value() || *referenceFlux <= 0.0 || *targetFlux <= 0.0)
        {
            continue;
        }
        ratios.push_back(*targetFlux / *referenceFlux);
    }
    return ratios;
}

[[nodiscard]] double medianOf(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    return count % 2u == 1u ? values[count / 2u] : 0.5 * (values[count / 2u - 1u] + values[count / 2u]);
}

} // namespace

TEST(StarFluxScaleTest, TheMedianRatioOfPairedPsfVolumesRecoversAPlantedFactor)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 384u;
    fieldRequest.height = 384u;
    fieldRequest.seed = 17u;
    const an::test::StarField reference = an::test::makeStarField(fieldRequest);
    ASSERT_GE(reference.stars.size(), 30u);

    // The target is the same sky with every star - and the noise - a quarter brighter above it.
    std::vector<float> target(reference.samples.size(), 0.0f);
    for (std::size_t index = 0u; index < target.size(); ++index)
    {
        const double above = static_cast<double>(reference.samples[index]) - fieldRequest.background;
        target[index] = static_cast<float>(fieldRequest.background + kPlantedFactor * above);
    }

    const an::ImageDescriptor descriptor = descriptorOf(fieldRequest.width, fieldRequest.height);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::vector<an::DetectedStar> referenceStars = detect(execution, reference.samples, descriptor, layout);
    const std::vector<an::DetectedStar> targetStars = detect(execution, target, descriptor, layout);
    ASSERT_GE(referenceStars.size(), kMinimumPairs);
    ASSERT_GE(targetStars.size(), kMinimumPairs);

    const std::vector<double> ratios =
        fluxRatios(referenceStars, targetStars, 1.5, fieldRequest.width, fieldRequest.height);
    ASSERT_GE(ratios.size(), kMinimumPairs);
    EXPECT_NEAR(medianOf(ratios), kPlantedFactor, 0.03);

    // Every admitted pair agrees with the factor on its own to a few per cent: the volume of a fitted
    // profile scales with its amplitude.
    std::size_t agreeing = 0u;
    for (const double ratio : ratios)
    {
        agreeing += std::abs(ratio - kPlantedFactor) < 0.08 ? 1u : 0u;
    }
    EXPECT_GE(agreeing, ratios.size() * 9u / 10u);

    // A frame that is the reference itself measures a unit scale from the same pairing.
    const std::vector<double> unity =
        fluxRatios(referenceStars, referenceStars, 0.5, fieldRequest.width, fieldRequest.height);
    ASSERT_GE(unity.size(), kMinimumPairs);
    EXPECT_DOUBLE_EQ(medianOf(unity), 1.0);
}
