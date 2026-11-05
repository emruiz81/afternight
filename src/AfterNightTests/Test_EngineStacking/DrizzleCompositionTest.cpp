// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
an::ImageDescriptor descriptorFor(std::uint32_t width, std::uint32_t height)
{
    return *an::ImageDescriptor::create(
        {.extent = *an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits()),
         .sampleType = an::SampleType::Float32,
         .channelSchema = an::ChannelSchema::Mono,
         .colorSpace = an::ColorSpace::Unspecified,
         .numericDomain = an::NumericDomain::NormalizedLinear,
         .cfa = {},
         .coordinateOrigin = an::CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
}

TEST(DrizzleCompositionTest, ReferenceUnionIntersectionAndExplicitRoiShareTheCheckedCanvas)
{
    const auto descriptor = descriptorFor(9u, 7u);
    const auto layout = *an::ImageLayout::create({descriptor.extent(), 4u, 1u, 1u, 1u}, an::defaultEngineLimits());
    const std::array<double, 3u> shifts{-2.0, 0.0, 3.0};
    std::array<an::RegistrationTransform, 3u> transforms;
    std::array<an::RegistrationBounds, 3u> bounds;
    std::array<std::vector<float>, 3u> samples;
    std::vector<an::DrizzleFrameData> data;
    for (std::size_t frame = 0u; frame < 3u; ++frame)
    {
        an::RegistrationTransformRequest request;
        request.sourceToReference[2] = shifts[frame];
        transforms[frame] = *an::RegistrationTransform::create(request);
        bounds[frame] = *an::computeRegistrationBounds(transforms[frame], 9u, 7u);
        samples[frame].assign(63u, static_cast<float>(frame + 1u));
        data.push_back({{layout, std::as_bytes(std::span{samples[frame]}), an::SampleType::Float32}});
    }
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto source = an::ResidentDrizzleFrameSource::create(data, an::DrizzleSourceAccess::Region);
    for (const auto mode : {an::RegistrationCanvasMode::Reference, an::RegistrationCanvasMode::Union,
                            an::RegistrationCanvasMode::Intersection})
    {
        auto canvas = an::planRegistrationCanvas(bounds, 1u, mode);
        ASSERT_TRUE(canvas.hasValue());
        const auto verify = [&](const an::RegistrationCanvas& selected) {
            an::DrizzleRequest request{.input = descriptor, .frameCount = 3u};
            request.parameters.scale = 1.0;
            request.parameters.kernel = an::DrizzleKernel::Point;
            request.output.coverageMap = true;
            request.output.extent = an::ImageExtent::create(selected.width, selected.height, 1u, execution->limits());
            const auto model = *an::DrizzleModel::create(request);
            std::vector<an::DrizzleFrameParameters> frames;
            for (const auto& transform : transforms)
                frames.push_back({*an::DrizzleMapDescriptor::create(
                    model, {.transform = transform, .postTranslation = selected.origin})});
            an::DrizzleExecutionRequest run;
            run.planning.maximumSourceRows = run.planning.maximumDestinationRows = 2u;
            auto result = an::executeDrizzle(*engine, *execution, model, *source, frames, run);
            ASSERT_TRUE(result.hasValue()) << result.status().message();
            for (std::uint32_t y = 0u; y < selected.height; ++y)
                for (std::uint32_t x = 0u; x < selected.width; ++x)
                {
                    double total = 0.0;
                    std::uint32_t count = 0u;
                    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
                    {
                        const double sourceX = x - selected.origin.x - shifts[frame];
                        const double sourceY = y - selected.origin.y;
                        if (sourceX >= 0.0 && sourceX < 9.0 && sourceY >= 0.0 && sourceY < 7.0)
                        {
                            total += frame + 1u;
                            ++count;
                        }
                    }
                    const auto index = static_cast<std::size_t>(y) * selected.width + x;
                    EXPECT_FLOAT_EQ(static_cast<const float*>(result->image.data())[index],
                                    count == 0u ? 0.0f : static_cast<float>(total / count));
                    EXPECT_EQ(static_cast<const std::uint32_t*>(result->coverageMap.data())[index], count);
                }
        };
        verify(*canvas);
        if (mode == an::RegistrationCanvasMode::Reference)
        {
            const an::RegistrationBounds roi{2.0, 1.0, 7.0, 5.0};
            const auto cropped = an::planRegistrationCanvas({&roi, 1u}, 0u, mode);
            ASSERT_TRUE(cropped.hasValue());
            verify(*cropped);
        }
    }
    const std::array<an::RegistrationBounds, 2u> disjoint{{{0.0, 0.0, 2.0, 2.0}, {3.0, 3.0, 5.0, 5.0}}};
    EXPECT_EQ(an::planRegistrationCanvas(disjoint, 0u, an::RegistrationCanvasMode::Intersection).status().code(),
              an::ErrorCode::EmptySelection);
}

TEST(DrizzleCompositionTest, CompactCanvasCoefficientsComposeWithSourceSipBeforeDrizzle)
{
    an::DrizzleRequest request{.input = descriptorFor(9u, 7u)};
    request.parameters.kernel = an::DrizzleKernel::Point;
    request.parameters.scale = 2.0;
    const auto model = *an::DrizzleModel::create(request);
    an::SipDistortionRequest sip;
    sip.referencePixel = {4.5, 3.5};
    sip.forwardX.order = sip.forwardY.order = 2u;
    sip.forwardX.coefficients[2][0] = 0.001;
    const auto distortion = *an::SipDistortion::create(sip);
    const auto mapping =
        *an::DrizzleMapDescriptor::create(model, {.postTranslation = {1.0, -1.0}, .sourceDistortion = distortion});
    const an::LocalNormalizationGridShape shape{.gridSize = 1u, .width = 9u, .height = 7u, .channelCount = 1u};
    const std::array<double, 4u> target{0.1, 0.3, 0.2, 0.4};
    const std::array<double, 4u> reference{0.2, 0.2, 0.2, 0.2};
    const std::array<double, 4u> scale{1.0, 2.0, 1.0, 2.0};
    for (std::uint32_t y = 0u; y < 7u; ++y)
        for (std::uint32_t x = 0u; x < 9u; ++x)
        {
            const auto mapped = an::mapDrizzlePoint(mapping, {static_cast<double>(x), static_cast<double>(y)});
            ASSERT_TRUE(mapped.hasValue());
            // Convert enlarged sampling coordinates back to the coefficient canvas's sampling coordinates.
            const double canvasX = (mapped->x + 0.5) / mapping.scale() - 0.5;
            const double canvasY = (mapped->y + 0.5) / mapping.scale() - 0.5;
            const auto a =
                an::evaluateLocalGrid(shape, target, an::LocalGridInterpolation::Bilinear, 0u, canvasX, canvasY);
            const auto b =
                an::evaluateLocalGrid(shape, reference, an::LocalGridInterpolation::Bilinear, 0u, canvasX, canvasY);
            const auto k =
                an::evaluateLocalGrid(shape, scale, an::LocalGridInterpolation::Bilinear, 0u, canvasX, canvasY);
            ASSERT_TRUE(a.hasValue() && b.hasValue() && k.hasValue());
            const double correctedX = x + 0.001 * (static_cast<double>(x) - 4.0) * (static_cast<double>(x) - 4.0) + 1.0;
            const double tx = std::clamp(correctedX / 8.0, 0.0, 1.0);
            const double ty = std::clamp((static_cast<double>(y) - 1.0) / 6.0, 0.0, 1.0);
            const double expected = (1.0 + tx) * (0.5 - (0.1 + 0.2 * tx + 0.1 * ty)) + 0.2;
            EXPECT_NEAR(*k * (0.5 - *a) + *b, expected, 1e-12);
        }
}
} // namespace
