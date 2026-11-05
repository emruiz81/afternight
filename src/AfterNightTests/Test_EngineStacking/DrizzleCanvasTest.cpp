// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the drizzle canvas: the output plane a drizzled set is written on, the guard an
// intersection keeps at its edges and the analysis plane a local normalization measures on, with the
// product's arithmetic written out as the oracle, and the canvas composed with the drizzle descriptor
// and the local-normalization grid it feeds.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Normalization/DrizzleLocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{

/// A translation by whole and fractional pixels.
[[nodiscard]] an::RegistrationTransform translation(double dx, double dy)
{
    const an::Result<an::RegistrationTransform> transform =
        an::RegistrationTransform::create({.linearKind = an::RegistrationTransformKind::Similarity,
                                           .sourceToReference = {1.0, 0.0, dx, 0.0, 1.0, dy, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    return transform.hasValue() ? *transform : an::RegistrationTransform{};
}

/// The rectangle a frame of the stated size occupies under a transform.
[[nodiscard]] an::RegistrationBounds boundsOf(const an::RegistrationTransform& transform, std::uint32_t width,
                                              std::uint32_t height)
{
    const an::Result<an::RegistrationBounds> bounds = an::computeRegistrationBounds(transform, width, height);
    EXPECT_TRUE(bounds.hasValue()) << bounds.status().message();
    return bounds.hasValue() ? *bounds : an::RegistrationBounds{};
}

/// The product's guard: `max(1, ceil(scale * (1 + pixelFraction / 2)))` above a scale of one.
[[nodiscard]] std::uint32_t productGuard(double scale, double pixelFraction)
{
    if (!std::isfinite(scale) || scale <= 1.0 + 1.0e-6)
    {
        return 0u;
    }
    const double fraction = std::clamp(std::isfinite(pixelFraction) ? pixelFraction : 1.0, 0.0, 1.0);
    return static_cast<std::uint32_t>((std::max)(1.0, std::ceil(scale * (1.0 + 0.5 * fraction))));
}

TEST(DrizzleCanvasTest, AReferenceCanvasIsTheReferenceRectangleEnlargedAndRoundedUp)
{
    const std::array<an::RegistrationBounds, 3u> bounds{boundsOf(translation(0.0, 0.0), 100u, 80u),
                                                        boundsOf(translation(3.25, -1.5), 100u, 80u),
                                                        boundsOf(translation(-2.75, 4.0), 100u, 80u)};
    for (const double scale : {1.0, 1.5, 2.0, 3.0, 4.0})
    {
        const an::Result<an::DrizzleCanvas> canvas =
            an::planDrizzleCanvas(bounds, 0u, {.mode = an::RegistrationCanvasMode::Reference, .scale = scale});
        ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
        EXPECT_EQ(canvas->boundingBox, bounds[0]);
        EXPECT_DOUBLE_EQ(canvas->origin.x, 0.0);
        EXPECT_DOUBLE_EQ(canvas->origin.y, 0.0);
        EXPECT_EQ(canvas->width, static_cast<std::uint32_t>(std::ceil(100.0 * scale))) << scale;
        EXPECT_EQ(canvas->height, static_cast<std::uint32_t>(std::ceil(80.0 * scale))) << scale;
        EXPECT_EQ(canvas->edgeGuard, 0u);
        EXPECT_EQ(canvas->outputWidth, canvas->width);
        EXPECT_EQ(canvas->outputHeight, canvas->height);
        EXPECT_EQ(canvas->analysisWidth, 100u);
        EXPECT_EQ(canvas->analysisHeight, 80u);
    }

    // Another reference moves the rectangle, and the origin with it.
    const an::Result<an::DrizzleCanvas> second =
        an::planDrizzleCanvas(bounds, 1u, {.mode = an::RegistrationCanvasMode::Reference, .scale = 2.0});
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_DOUBLE_EQ(second->origin.x, -3.25);
    EXPECT_DOUBLE_EQ(second->origin.y, 1.5);
}

TEST(DrizzleCanvasTest, AUnionCanvasHoldsEveryFrameAndRoundsUp)
{
    const std::array<an::RegistrationBounds, 3u> bounds{boundsOf(translation(0.0, 0.0), 64u, 48u),
                                                        boundsOf(translation(10.4, -3.3), 64u, 48u),
                                                        boundsOf(translation(-5.7, 7.9), 64u, 48u)};
    const an::Result<an::DrizzleCanvas> canvas =
        an::planDrizzleCanvas(bounds, 0u, {.mode = an::RegistrationCanvasMode::Union, .scale = 1.5});
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
    const double width = (64.0 + 10.4) - (-5.7);
    const double height = (48.0 + 7.9) - (-3.3);
    EXPECT_DOUBLE_EQ(canvas->boundingBox.minimumX, -5.7);
    EXPECT_DOUBLE_EQ(canvas->boundingBox.minimumY, -3.3);
    EXPECT_DOUBLE_EQ(canvas->origin.x, 5.7);
    EXPECT_DOUBLE_EQ(canvas->origin.y, 3.3);
    EXPECT_EQ(canvas->width, static_cast<std::uint32_t>(std::ceil(canvas->boundingBox.width() * 1.5)));
    EXPECT_EQ(canvas->height, static_cast<std::uint32_t>(std::ceil(canvas->boundingBox.height() * 1.5)));
    EXPECT_NEAR(canvas->boundingBox.width(), width, 1e-12);
    EXPECT_NEAR(canvas->boundingBox.height(), height, 1e-12);
    EXPECT_EQ(canvas->analysisWidth, static_cast<std::uint32_t>(std::ceil(canvas->boundingBox.width())));
    EXPECT_EQ(canvas->analysisHeight, static_cast<std::uint32_t>(std::ceil(canvas->boundingBox.height())));
    EXPECT_EQ(canvas->edgeGuard, 0u);
}

TEST(DrizzleCanvasTest, AnIntersectionMovesItsCornersInwardAndGuardsItsEdges)
{
    const std::array<an::RegistrationBounds, 3u> bounds{boundsOf(translation(0.0, 0.0), 120u, 90u),
                                                        boundsOf(translation(2.6, -1.2), 120u, 90u),
                                                        boundsOf(translation(-3.4, 0.7), 120u, 90u)};
    // The raw intersection runs from (2.6, 0.7) to (116.6, 88.8); its whole pixels from (3, 1) to
    // (116, 88), so the rectangle is 113 by 87.
    for (const double scale : {1.0, 1.5, 2.0, 2.5, 3.0})
    {
        for (const double fraction : {0.1, 0.6, 0.9, 1.0})
        {
            const an::Result<an::DrizzleCanvas> canvas = an::planDrizzleCanvas(
                bounds, 0u,
                {.mode = an::RegistrationCanvasMode::Intersection, .scale = scale, .pixelFraction = fraction});
            ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
            EXPECT_DOUBLE_EQ(canvas->boundingBox.minimumX, 3.0);
            EXPECT_DOUBLE_EQ(canvas->boundingBox.minimumY, 1.0);
            EXPECT_DOUBLE_EQ(canvas->boundingBox.maximumX, 116.0);
            EXPECT_DOUBLE_EQ(canvas->boundingBox.maximumY, 88.0);
            EXPECT_DOUBLE_EQ(canvas->origin.x, -3.0);
            EXPECT_DOUBLE_EQ(canvas->origin.y, -1.0);
            const std::uint32_t guard = productGuard(scale, fraction);
            const double shortened = guard > 0u ? 0.0 : 1.0;
            const auto width = static_cast<std::uint32_t>(std::floor(113.0 * scale) - shortened);
            const auto height = static_cast<std::uint32_t>(std::floor(87.0 * scale) - shortened);
            EXPECT_EQ(canvas->width, width) << scale << ' ' << fraction;
            EXPECT_EQ(canvas->height, height) << scale << ' ' << fraction;
            EXPECT_EQ(canvas->edgeGuard, guard) << scale << ' ' << fraction;
            EXPECT_EQ(canvas->outputWidth, width - 2u * guard);
            EXPECT_EQ(canvas->outputHeight, height - 2u * guard);
            EXPECT_EQ(canvas->analysisWidth, 113u);
            EXPECT_EQ(canvas->analysisHeight, 87u);
        }
    }
}

TEST(DrizzleCanvasTest, TheGuardIsTheProductsAndACanvasTooSmallForItKeepsNone)
{
    for (const double scale : {1.0, 1.0 + 5.0e-7, 1.0 + 2.0e-6, 1.25, 1.5, 2.0, 3.3, 4.0})
    {
        for (const double fraction : {-1.0, 0.0, 0.05, 0.5, 0.9, 1.0, 1.7, std::numeric_limits<double>::quiet_NaN()})
        {
            EXPECT_EQ(an::drizzleIntersectionGuard(scale, fraction), productGuard(scale, fraction))
                << scale << ' ' << fraction;
        }
    }
    EXPECT_EQ(an::drizzleIntersectionGuard(std::numeric_limits<double>::quiet_NaN(), 0.9), 0u);
    EXPECT_EQ(an::drizzleIntersectionGuard(std::numeric_limits<double>::infinity(), 0.9), 0u);
    EXPECT_EQ(an::drizzleIntersectionGuard(2.0, 0.9), 3u);
    EXPECT_EQ(an::drizzleIntersectionGuard(4.0, 1.0), 6u);
    EXPECT_GT(an::drizzleIntersectionGuard(1.0e300, 1.0), 0u);

    // Three reference pixels at a scale of two hold six output pixels, not more than twice a guard of
    // three, so the crop is skipped and the record says so; four hold eight, and keep it.
    const std::array<an::RegistrationBounds, 2u> small{boundsOf(translation(0.0, 0.0), 3u, 3u),
                                                       boundsOf(translation(0.0, 0.0), 3u, 3u)};
    const an::Result<an::DrizzleCanvas> canvas = an::planDrizzleCanvas(
        small, 0u, {.mode = an::RegistrationCanvasMode::Intersection, .scale = 2.0, .pixelFraction = 0.9});
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
    EXPECT_EQ(canvas->width, 6u);
    EXPECT_EQ(canvas->edgeGuard, 0u);
    EXPECT_EQ(canvas->outputWidth, 6u);
    const std::array<an::RegistrationBounds, 2u> four{boundsOf(translation(0.0, 0.0), 4u, 4u),
                                                      boundsOf(translation(0.0, 0.0), 4u, 4u)};
    const an::Result<an::DrizzleCanvas> kept = an::planDrizzleCanvas(
        four, 0u, {.mode = an::RegistrationCanvasMode::Intersection, .scale = 2.0, .pixelFraction = 0.9});
    ASSERT_TRUE(kept.hasValue()) << kept.status().message();
    EXPECT_EQ(kept->width, 8u);
    EXPECT_EQ(kept->edgeGuard, 3u);
    EXPECT_EQ(kept->outputWidth, 2u);
}

TEST(DrizzleCanvasTest, APlaneAlreadyChosenIsEnlargedToTheNearestPixelAndNotGuarded)
{
    const an::RegistrationBounds plane{.minimumX = -12.25, .minimumY = 4.5, .maximumX = 87.0, .maximumY = 70.75};
    for (const double scale : {1.0, 1.5, 2.0, 2.25, 4.0})
    {
        const an::Result<an::DrizzleCanvas> canvas = an::planDrizzleCanvasOnPlane(plane, scale);
        ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
        EXPECT_EQ(canvas->boundingBox, plane);
        EXPECT_DOUBLE_EQ(canvas->origin.x, 12.25);
        EXPECT_DOUBLE_EQ(canvas->origin.y, -4.5);
        EXPECT_EQ(canvas->width, static_cast<std::uint32_t>(std::lround(99.25 * scale))) << scale;
        EXPECT_EQ(canvas->height, static_cast<std::uint32_t>(std::lround(66.25 * scale))) << scale;
        EXPECT_EQ(canvas->edgeGuard, 0u);
        EXPECT_EQ(canvas->outputWidth, canvas->width);
        EXPECT_EQ(canvas->analysisWidth, 100u);
        EXPECT_EQ(canvas->analysisHeight, 67u);
    }
    // Halfway cases go away from zero, as the product's rounding did.
    const an::Result<an::DrizzleCanvas> halfway =
        an::planDrizzleCanvasOnPlane({.minimumX = 0.0, .minimumY = 0.0, .maximumX = 10.25, .maximumY = 3.25}, 2.0);
    ASSERT_TRUE(halfway.hasValue()) << halfway.status().message();
    EXPECT_EQ(halfway->width, 21u);
    EXPECT_EQ(halfway->height, 7u);
}

TEST(DrizzleCanvasTest, MalformedRequestsAreRefused)
{
    const std::array<an::RegistrationBounds, 2u> bounds{boundsOf(translation(0.0, 0.0), 40u, 30u),
                                                        boundsOf(translation(1.0, 1.0), 40u, 30u)};
    const auto refusal = [&](std::span<const an::RegistrationBounds> set, std::uint32_t reference,
                             const an::DrizzleCanvasRequest& request) {
        const an::Result<an::DrizzleCanvas> canvas = an::planDrizzleCanvas(set, reference, request);
        EXPECT_FALSE(canvas.hasValue());
        return canvas.hasValue() ? an::ErrorCode::Internal : canvas.status().code();
    };
    for (const double scale :
         {0.5, 0.999, 4.001, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        EXPECT_EQ(refusal(bounds, 0u, {.scale = scale}), an::ErrorCode::InvalidArgument) << scale;
        EXPECT_EQ(an::planDrizzleCanvasOnPlane(bounds[0], scale).status().code(), an::ErrorCode::InvalidArgument)
            << scale;
    }
    for (const double fraction : {0.0, -0.5, 1.01, std::numeric_limits<double>::quiet_NaN()})
    {
        EXPECT_EQ(refusal(bounds, 0u, {.pixelFraction = fraction}), an::ErrorCode::InvalidArgument) << fraction;
    }
    EXPECT_EQ(refusal({}, 0u, {}), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal(bounds, 2u, {}), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refusal(bounds, 0u, {.mode = static_cast<an::RegistrationCanvasMode>(7u)}),
              an::ErrorCode::InvalidArgument);
    const std::array<an::RegistrationBounds, 1u> inverted{
        an::RegistrationBounds{.minimumX = 5.0, .minimumY = 0.0, .maximumX = 1.0, .maximumY = 3.0}};
    EXPECT_EQ(refusal(inverted, 0u, {}), an::ErrorCode::InvalidArgument);

    // Frames with nothing in common, and frames that share less than one whole pixel.
    const std::array<an::RegistrationBounds, 2u> apart{boundsOf(translation(0.0, 0.0), 10u, 10u),
                                                       boundsOf(translation(20.0, 0.0), 10u, 10u)};
    EXPECT_EQ(refusal(apart, 0u, {.mode = an::RegistrationCanvasMode::Intersection}), an::ErrorCode::EmptySelection);
    const std::array<an::RegistrationBounds, 2u> sliver{boundsOf(translation(0.0, 0.0), 10u, 10u),
                                                        boundsOf(translation(9.4, 0.3), 10u, 10u)};
    EXPECT_EQ(refusal(sliver, 0u, {.mode = an::RegistrationCanvasMode::Intersection}), an::ErrorCode::EmptySelection);
    // One whole pixel at a scale of one loses its last pixel to the shortening and holds none.
    const std::array<an::RegistrationBounds, 2u> one{boundsOf(translation(0.0, 0.0), 1u, 1u),
                                                     boundsOf(translation(0.0, 0.0), 1u, 1u)};
    EXPECT_EQ(refusal(one, 0u, {.mode = an::RegistrationCanvasMode::Intersection, .scale = 1.0}),
              an::ErrorCode::EmptySelection);

    const double huge = static_cast<double>(an::kMaximumRegistrationCanvasExtent) + 10.0;
    const std::array<an::RegistrationBounds, 1u> wide{
        an::RegistrationBounds{.minimumX = 0.0, .minimumY = 0.0, .maximumX = huge, .maximumY = 10.0}};
    EXPECT_EQ(refusal(wide, 0u, {}), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(an::planDrizzleCanvasOnPlane(wide[0], 1.0).status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(an::planDrizzleCanvasOnPlane(inverted[0], 1.0).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planDrizzleCanvasOnPlane({.minimumX = 1.0, .minimumY = 1.0, .maximumX = 1.0, .maximumY = 4.0}, 1.0)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(DrizzleCanvasTest, TheCanvasIsWhatTheDrizzleDescriptorMapsOnto)
{
    // A frame translated onto a union canvas: its corners land on the canvas's edges once the
    // descriptor takes the canvas's origin as its post-translation and its extent as the output's.
    constexpr std::uint32_t kWidth = 30u;
    constexpr std::uint32_t kHeight = 20u;
    constexpr double kScale = 2.0;
    const std::array<an::RegistrationTransform, 2u> transforms{translation(0.0, 0.0), translation(-4.5, 3.0)};
    const std::array<an::RegistrationBounds, 2u> bounds{boundsOf(transforms[0], kWidth, kHeight),
                                                        boundsOf(transforms[1], kWidth, kHeight)};
    const an::Result<an::DrizzleCanvas> canvas =
        an::planDrizzleCanvas(bounds, 0u, {.mode = an::RegistrationCanvasMode::Union, .scale = kScale});
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();

    const std::optional<an::ImageExtent> input =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    const std::optional<an::ImageExtent> output =
        an::ImageExtent::create(canvas->width, canvas->height, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(input.has_value() && output.has_value());
    const std::optional<an::ImageDescriptor> image =
        an::ImageDescriptor::create({.extent = *input,
                                     .sampleType = an::SampleType::Float32,
                                     .channelSchema = an::ChannelSchema::Mono,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::NormalizedLinear,
                                     .cfa = {},
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    ASSERT_TRUE(image.has_value());
    an::DrizzleRequest request{.input = *image, .frameCount = 2u};
    request.parameters.scale = kScale;
    request.output.extent = *output;
    const an::Result<an::DrizzleModel> model = an::DrizzleModel::create(request);
    ASSERT_TRUE(model.hasValue()) << model.status().message();

    for (std::size_t frame = 0u; frame < transforms.size(); ++frame)
    {
        const an::Result<an::DrizzleMapDescriptor> descriptor = an::DrizzleMapDescriptor::create(
            *model, an::DrizzleMappingRequest{.transform = transforms[frame], .postTranslation = canvas->origin});
        ASSERT_TRUE(descriptor.hasValue()) << descriptor.status().message();
        // The first pixel's top-left corner sits at (-0.5, -0.5) in the descriptor's integer-centred
        // convention, the last pixel's bottom-right at (width - 0.5, height - 0.5).
        const an::Result<an::PixelCoordinate> first = an::mapDrizzlePoint(*descriptor, {-0.5, -0.5});
        const an::Result<an::PixelCoordinate> last =
            an::mapDrizzlePoint(*descriptor, {static_cast<double>(kWidth) - 0.5, static_cast<double>(kHeight) - 0.5});
        ASSERT_TRUE(first.hasValue() && last.hasValue());
        EXPECT_GE(first->x + 0.5, -1e-9) << frame;
        EXPECT_GE(first->y + 0.5, -1e-9) << frame;
        EXPECT_LE(last->x + 0.5, static_cast<double>(canvas->width) + 1e-9) << frame;
        EXPECT_LE(last->y + 0.5, static_cast<double>(canvas->height) + 1e-9) << frame;
        // The analysis plane is the canvas before the scale.
        EXPECT_NEAR((first->x + 0.5) / kScale, bounds[frame].minimumX + canvas->origin.x, 1e-12) << frame;
        EXPECT_NEAR((first->y + 0.5) / kScale, bounds[frame].minimumY + canvas->origin.y, 1e-12) << frame;
    }
    // The frame that sets the canvas's left and bottom edges reaches them exactly.
    const an::Result<an::DrizzleMapDescriptor> shifted = an::DrizzleMapDescriptor::create(
        *model, an::DrizzleMappingRequest{.transform = transforms[1], .postTranslation = canvas->origin});
    ASSERT_TRUE(shifted.hasValue());
    const an::Result<an::PixelCoordinate> corner = an::mapDrizzlePoint(*shifted, {-0.5, -0.5});
    ASSERT_TRUE(corner.hasValue());
    EXPECT_NEAR(corner->x, -0.5, 1e-12);
}

TEST(DrizzleCanvasTest, TheLocalNormalizationGridDescribesTheAnalysisPlane)
{
    const std::array<an::RegistrationBounds, 2u> bounds{boundsOf(translation(0.0, 0.0), 200u, 150u),
                                                        boundsOf(translation(1.5, -2.25), 200u, 150u)};
    const an::Result<an::DrizzleCanvas> canvas = an::planDrizzleCanvas(
        bounds, 0u, {.mode = an::RegistrationCanvasMode::Intersection, .scale = 2.0, .pixelFraction = 0.8});
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
    const an::Result<an::LocalNormalizationGridShape> shape = an::drizzleLocalNormalizationShape(*canvas, 8u, 3u);
    ASSERT_TRUE(shape.hasValue()) << shape.status().message();
    EXPECT_EQ(shape->gridSize, 8u);
    EXPECT_EQ(shape->width, canvas->analysisWidth);
    EXPECT_EQ(shape->height, canvas->analysisHeight);
    EXPECT_EQ(shape->channelCount, 3u);
    EXPECT_TRUE(an::checkLocalNormalizationGridShape(*shape).ok());
    EXPECT_EQ(shape->width, 198u);
    EXPECT_EQ(shape->height, 147u);

    EXPECT_EQ(an::drizzleLocalNormalizationShape(*canvas, 0u, 3u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::drizzleLocalNormalizationShape(*canvas, an::kMaxLocalNormalizationGridSize + 1u, 3u).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::drizzleLocalNormalizationShape(*canvas, 8u, 0u).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::drizzleLocalNormalizationShape(an::DrizzleCanvas{}, 8u, 1u).status().code(),
              an::ErrorCode::InvalidArgument);
}

} // namespace
