// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the comet composite: the aperture radius and circle, the split of a frame into a star
// layer and a comet layer, the feathered mask over every aperture, and the blend of a comet stack into a star
// stack. Each operation is held to the product's arithmetic written out here as the oracle, and the whole
// composition is run once through the stack executor over a synthetic comet that moves across fixed stars.

#include <AfterNightEngine/Algorithms/Stacking/CometComposite.h>

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    return engine.memoryResource()->counters().liveBytes;
}

[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value_or(an::ValidityDescriptor{});
}

/// One planar image owned here, of any representation, with an optional packed mask.
struct Plane
{
    an::SampleType sampleType = an::SampleType::Float32;
    std::optional<an::ImageDescriptor> descriptor;
    std::optional<an::ImageLayout> layout;
    std::vector<double> storage;
    an::ValidityDescriptor validity{};
    std::vector<std::uint8_t> maskBits;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;

    Plane(an::SampleType type, std::uint32_t width, std::uint32_t height, std::uint32_t channels,
          an::ValidityRule rule = an::ValidityRule::AllValid)
        : sampleType(type), validity(ruleOf(rule))
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
        EXPECT_TRUE(extent.has_value());
        const an::ChannelSchema schema = channels == 3u   ? an::ChannelSchema::Rgb
                                         : channels == 1u ? an::ChannelSchema::Mono
                                         : channels == 4u ? an::ChannelSchema::Rgba
                                                          : an::ChannelSchema::Named;
        descriptor = an::ImageDescriptor::create(
            {.extent = *extent,
             .sampleType = type,
             .channelSchema = schema,
             .colorSpace = schema == an::ChannelSchema::Rgb ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified,
             .numericDomain = an::NumericDomain::NormalizedLinear,
             .cfa = {},
             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
             .pixelAspectRatio = 1.0});
        EXPECT_TRUE(descriptor.has_value());
        const std::uint32_t element = an::sampleTypeSizeBytes(type);
        layout = an::ImageLayout::create({.extent = *extent,
                                          .elementSizeBytes = element,
                                          .samplesPerPixel = 1u,
                                          .planeCount = channels,
                                          .rowAlignmentBytes = element},
                                         an::defaultEngineLimits());
        EXPECT_TRUE(layout.has_value());
        storage.assign(static_cast<std::size_t>(layout->totalBytes() / sizeof(double)) + 2u, 0.0);
    }

    [[nodiscard]] std::uint32_t width() const noexcept
    {
        return layout->extent().width();
    }

    [[nodiscard]] std::uint32_t height() const noexcept
    {
        return layout->extent().height();
    }

    [[nodiscard]] std::uint32_t channels() const noexcept
    {
        return layout->extent().channelCount();
    }

    [[nodiscard]] std::byte* bytes() noexcept
    {
        return reinterpret_cast<std::byte*>(storage.data());
    }

    [[nodiscard]] const std::byte* bytes() const noexcept
    {
        return reinterpret_cast<const std::byte*>(storage.data());
    }

    [[nodiscard]] std::uint64_t offsetOf(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const noexcept
    {
        return channel * layout->planeStrideBytes() + y * layout->rowStrideBytes() +
               static_cast<std::uint64_t>(x) * layout->elementSizeBytes();
    }

    /// Writes a value, as its representation stores it: an integer as a fraction of full scale.
    void put(std::uint32_t channel, std::uint32_t x, std::uint32_t y, double value)
    {
        std::byte* const at = bytes() + offsetOf(channel, x, y);
        switch (sampleType)
        {
        case an::SampleType::Float64:
            std::memcpy(at, &value, sizeof(double));
            break;
        case an::SampleType::UInt16: {
            const auto code = static_cast<std::uint16_t>(std::lround(std::clamp(value, 0.0, 1.0) * 65535.0));
            std::memcpy(at, &code, sizeof(code));
            break;
        }
        default: {
            const auto narrow = static_cast<float>(value);
            std::memcpy(at, &narrow, sizeof(float));
            break;
        }
        }
    }

    /// The value the engine reads, in the working representation `working`.
    [[nodiscard]] double read(std::uint32_t channel, std::uint32_t x, std::uint32_t y,
                              an::SampleType working = an::SampleType::Float32) const
    {
        const std::byte* const at = bytes() + offsetOf(channel, x, y);
        double value = 0.0;
        switch (sampleType)
        {
        case an::SampleType::Float64: {
            std::memcpy(&value, at, sizeof(double));
            break;
        }
        case an::SampleType::UInt16: {
            std::uint16_t code = 0u;
            std::memcpy(&code, at, sizeof(code));
            value = static_cast<double>(code) / 65535.0;
            break;
        }
        default: {
            float narrow = 0.0f;
            std::memcpy(&narrow, at, sizeof(float));
            value = static_cast<double>(narrow);
            break;
        }
        }
        return working == an::SampleType::Float32 ? static_cast<double>(static_cast<float>(value)) : value;
    }

    template <typename SampleT> [[nodiscard]] SampleT get(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        SampleT sample{};
        std::memcpy(&sample, bytes() + offsetOf(channel, x, y), sizeof(SampleT));
        return sample;
    }

    void addMask()
    {
        const std::uint64_t rowBytes = an::packedMaskRowBytes(width());
        maskBits.assign(static_cast<std::size_t>(rowBytes * height()), 0xFFu);
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width(), height(), 1u, an::defaultEngineLimits());
        mask = an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>(maskBits), *extent);
        ASSERT_TRUE(mask.has_value());
    }

    void clearBit(std::uint32_t x, std::uint32_t y)
    {
        const std::uint64_t offset = static_cast<std::uint64_t>(y) * mask->rowStrideBytes() + x / 8u;
        maskBits[static_cast<std::size_t>(offset)] &= static_cast<std::uint8_t>(~(1u << (x % 8u)));
    }

    [[nodiscard]] an::CometCompositeSource source() const noexcept
    {
        return an::CometCompositeSource{
            .descriptor = &*descriptor,
            .samples = bytes(),
            .layout = &*layout,
            .validity = an::KernelValidityView{.descriptor = validity, .mask = mask.has_value() ? &*mask : nullptr}};
    }
};

/// A star-field-like frame: a sky, a few stars, a comet, and a little deterministic texture.
void paintField(Plane& plane, double cometX, double cometY, double starShift, std::uint32_t salt)
{
    const std::array<std::array<double, 2u>, 5u> stars{
        {{12.5, 10.5}, {70.5, 14.5}, {20.5, 52.5}, {84.5, 50.5}, {48.5, 8.5}}};
    for (std::uint32_t channel = 0u; channel < plane.channels(); ++channel)
    {
        const double sky = 0.08 + 0.02 * channel;
        for (std::uint32_t y = 0u; y < plane.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < plane.width(); ++x)
            {
                const double px = static_cast<double>(x) + 0.5;
                const double py = static_cast<double>(y) + 0.5;
                const std::uint64_t index = (static_cast<std::uint64_t>(y) * plane.width() + x) * 7u + channel + salt;
                double value = sky + 0.004 * static_cast<double>((index * 2654435761ull) % 997u) / 997.0;
                for (const auto& star : stars)
                {
                    const double dx = px - (star[0] + starShift);
                    const double dy = py - star[1];
                    value += 0.5 * std::exp(-(dx * dx + dy * dy) / (2.0 * 1.2 * 1.2));
                }
                const double cx = px - cometX;
                const double cy = py - cometY;
                value += 0.6 * std::exp(-(cx * cx + cy * cy) / (2.0 * 2.0 * 2.0));
                plane.put(channel, x, y, value);
            }
        }
    }
}

/// The product's feathered circle.
[[nodiscard]] double productWeight(double distance, double radius, double feather)
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

/// The product's median of a sorted copy: the middle value, or the mean of the two middle values.
[[nodiscard]] double productMedian(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2u;
    return values.size() % 2u == 0u ? 0.5 * (values[middle - 1u] + values[middle]) : values[middle];
}

/// The product's sky: the annulus median, or the whole channel's below eight samples, over valid samples.
[[nodiscard]] double productSky(const Plane& frame, std::uint32_t channel, const an::CometApertureCircle& aperture,
                                an::SampleType working, const std::vector<bool>* rejected = nullptr)
{
    const double radius = aperture.radius;
    const double inner = (std::max)(radius * 1.5, radius + 1.0);
    const double outer = (std::max)(inner + 2.0, radius * 2.5);
    std::vector<double> annulus;
    std::vector<double> whole;
    for (std::uint32_t y = 0u; y < frame.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < frame.width(); ++x)
        {
            if (rejected != nullptr && (*rejected)[static_cast<std::size_t>(y) * frame.width() + x])
            {
                continue;
            }
            const double value = frame.read(channel, x, y, working);
            whole.push_back(value);
            const double dx = (static_cast<double>(x) + 0.5) - aperture.center.x;
            const double dy = (static_cast<double>(y) + 0.5) - aperture.center.y;
            const double distance2 = dx * dx + dy * dy;
            if (distance2 >= inner * inner && distance2 <= outer * outer)
            {
                annulus.push_back(value);
            }
        }
    }
    return annulus.size() >= 8u ? productMedian(annulus) : productMedian(whole);
}

/// The product's two layers of one sample.
[[nodiscard]] std::pair<double, double> productLayers(double source, double sky, double weight)
{
    const double star = source * (1.0 - weight) + sky * weight;
    return {star, sky + (source - star)};
}

/// Runs a split into two fresh layers of the plan's representation.
struct SplitOutput
{
    std::optional<Plane> star;
    std::optional<Plane> comet;
    an::Result<an::CometApertureSplitResult> result = an::Status::failure(an::ErrorCode::Internal);
};

[[nodiscard]] SplitOutput runSplit(const an::ExecutionContext& execution, const an::CometApertureSplitPlan& plan,
                                   const Plane& frame, bool writeStar = true, bool writeComet = true)
{
    SplitOutput output;
    const an::SampleType type = plan.request().outputSampleType;
    output.star.emplace(type, frame.width(), frame.height(), frame.channels());
    output.comet.emplace(type, frame.width(), frame.height(), frame.channels());
    output.result = an::splitCometAperture(execution, plan, frame.source(), writeStar ? output.star->bytes() : nullptr,
                                           &*output.star->layout, writeComet ? output.comet->bytes() : nullptr,
                                           &*output.comet->layout);
    return output;
}

[[nodiscard]] an::CometApertureSplitPlan makeSplitPlan(const an::CometApertureSplitRequest& request)
{
    const an::Result<an::CometApertureSplitPlan> plan = an::planCometApertureSplit(request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    return *plan;
}

/// A translation, checked.
[[nodiscard]] an::RegistrationTransform translation(double dx, double dy)
{
    const an::Result<an::RegistrationTransform> transform =
        an::RegistrationTransform::create({.linearKind = an::RegistrationTransformKind::Similarity,
                                           .sourceToReference = {1.0, 0.0, dx, 0.0, 1.0, dy, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    return transform.hasValue() ? *transform : an::RegistrationTransform{};
}

TEST(CometCompositeTest, TheApertureRadiusFollowsTheProductsOrder)
{
    EXPECT_EQ(an::resolveCometApertureRadius({.apertureRadius = 6.5, .measuredDiameter = 20.0}), 6.5);
    EXPECT_EQ(an::resolveCometApertureRadius({.apertureRadius = 0.0, .measuredDiameter = 20.0}), 10.0);
    EXPECT_EQ(an::resolveCometApertureRadius({.apertureRadius = kNaN, .measuredFullWidth = 4.0}), 6.0);
    // A narrow comet's radius is never less than two pixels.
    EXPECT_EQ(an::resolveCometApertureRadius({.measuredFullWidth = 0.8}), 2.0);
    EXPECT_EQ(an::resolveCometApertureRadius({.measuredDiameter = -3.0, .measuredFullWidth = 0.0}), std::nullopt);
    EXPECT_EQ(an::resolveCometApertureRadius({}), std::nullopt);

    // Frames without a radius take the run's: the upper median of the radii that were resolved.
    const std::array<an::CometApertureMeasurement, 6u> measurements{
        an::CometApertureMeasurement{.apertureRadius = 5.0},    an::CometApertureMeasurement{},
        an::CometApertureMeasurement{.measuredDiameter = 18.0}, an::CometApertureMeasurement{.measuredFullWidth = 4.0},
        an::CometApertureMeasurement{.apertureRadius = 7.0},    an::CometApertureMeasurement{.measuredDiameter = kNaN}};
    std::array<double, 6u> radii{};
    const an::Result<std::uint32_t> filled = an::resolveCometApertureRadii(measurements, radii);
    ASSERT_TRUE(filled.hasValue()) << filled.status().message();
    EXPECT_EQ(*filled, 2u);
    // Resolved radii 5, 9, 6 and 7: the upper median of four is the third, 7.
    EXPECT_EQ(radii, (std::array<double, 6u>{5.0, 7.0, 9.0, 6.0, 7.0, 7.0}));

    std::array<double, 5u> shortRadii{};
    EXPECT_EQ(an::resolveCometApertureRadii(measurements, shortRadii).status().code(),
              an::ErrorCode::IncompatibleArguments);
    const std::array<an::CometApertureMeasurement, 2u> none{};
    std::array<double, 2u> unresolved{};
    EXPECT_EQ(an::resolveCometApertureRadii(none, unresolved).status().code(), an::ErrorCode::EmptySelection);
}

TEST(CometCompositeTest, TheCircleIsTheProjectedCentreAndTheMeanPolygonDistanceScaled)
{
    an::CometApertureProjection projection;
    projection.outputCenter = {.x = 40.25, .y = 18.5};
    projection.outputPolygon = {{.x = 44.25, .y = 18.5},
                                {.x = 40.25, .y = 24.5},
                                {.x = 36.25, .y = 18.5},
                                {.x = 40.25, .y = 18.5},
                                {.x = kNaN, .y = 3.0}};
    // Distances 4, 6, 4; the point on the centre and the one that is not finite are skipped.
    const an::Result<an::CometApertureCircle> circle = an::cometApertureCircleOf(projection, 9.0);
    ASSERT_TRUE(circle.hasValue()) << circle.status().message();
    EXPECT_DOUBLE_EQ(circle->center.x, 40.25);
    EXPECT_DOUBLE_EQ(circle->center.y, 18.5);
    EXPECT_DOUBLE_EQ(circle->radius, 14.0 / 3.0);
    const an::Result<an::CometApertureCircle> drizzled = an::cometApertureCircleOf(projection, 9.0, 2.0);
    ASSERT_TRUE(drizzled.hasValue());
    EXPECT_DOUBLE_EQ(drizzled->center.x, 80.5);
    EXPECT_DOUBLE_EQ(drizzled->center.y, 37.0);
    EXPECT_DOUBLE_EQ(drizzled->radius, 28.0 / 3.0);

    // No polygon is the source radius.
    an::CometApertureProjection bare;
    bare.outputCenter = {.x = 3.0, .y = 4.0};
    const an::Result<an::CometApertureCircle> fallback = an::cometApertureCircleOf(bare, 6.0, 1.5);
    ASSERT_TRUE(fallback.hasValue());
    EXPECT_DOUBLE_EQ(fallback->radius, 9.0);

    EXPECT_EQ(an::cometApertureCircleOf(bare, 0.0).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::cometApertureCircleOf(bare, 6.0, 0.5).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::cometApertureCircleOf(bare, 6.0, 4.5).status().code(), an::ErrorCode::InvalidArgument);
    bare.outputCenter.x = kNaN;
    EXPECT_EQ(an::cometApertureCircleOf(bare, 6.0).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CometCompositeTest, TheSplitLayersAreTheProductsArithmetic)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Dyadic centres, so the product's integer-centred distances and the engine's agree to the bit.
    const an::CometApertureCircle aperture{.center = {.x = 40.25, .y = 30.75}, .radius = 6.5};
    for (const an::SampleType frameType : {an::SampleType::Float32, an::SampleType::Float64, an::SampleType::UInt16})
    {
        for (const an::SampleType outputType : {an::SampleType::Float32, an::SampleType::Float64})
        {
            Plane frame(frameType, 96u, 64u, 2u);
            paintField(frame, 40.25, 30.75, 0.0, 3u);
            const an::CometApertureSplitPlan plan =
                makeSplitPlan({.aperture = aperture, .outputSampleType = outputType});
            EXPECT_DOUBLE_EQ(plan.feather(), (std::max)(1.0, 6.5 * 0.35));
            const SplitOutput split = runSplit(execution, plan, frame);
            ASSERT_TRUE(split.result.hasValue()) << split.result.status().message();
            EXPECT_EQ(split.result->run.selection.total, 96u * 64u);
            EXPECT_EQ(split.result->run.selection.valid, 96u * 64u);
            EXPECT_EQ(split.result->run.outputsWritten, 96u * 64u * 2u);
            for (std::uint32_t channel = 0u; channel < 2u; ++channel)
            {
                const double sky = productSky(frame, channel, aperture, outputType);
                EXPECT_EQ(split.result->background[channel], sky) << channel;
                EXPECT_FALSE(split.result->wholeChannelBackground[channel]);
                EXPECT_GE(split.result->annulusSamples[channel], 8u);
                for (std::uint32_t y = 0u; y < 64u; ++y)
                {
                    for (std::uint32_t x = 0u; x < 96u; ++x)
                    {
                        const double dx = (static_cast<double>(x) + 0.5) - aperture.center.x;
                        const double dy = (static_cast<double>(y) + 0.5) - aperture.center.y;
                        const double weight = productWeight(std::hypot(dx, dy), aperture.radius, plan.feather());
                        const auto [star, comet] = productLayers(frame.read(channel, x, y, outputType), sky, weight);
                        if (outputType == an::SampleType::Float32)
                        {
                            ASSERT_EQ(std::bit_cast<std::uint32_t>(split.star->get<float>(channel, x, y)),
                                      std::bit_cast<std::uint32_t>(static_cast<float>(star)))
                                << x << ',' << y;
                            ASSERT_EQ(std::bit_cast<std::uint32_t>(split.comet->get<float>(channel, x, y)),
                                      std::bit_cast<std::uint32_t>(static_cast<float>(comet)))
                                << x << ',' << y;
                        }
                        else
                        {
                            ASSERT_EQ(std::bit_cast<std::uint64_t>(split.star->get<double>(channel, x, y)),
                                      std::bit_cast<std::uint64_t>(star))
                                << x << ',' << y;
                            ASSERT_EQ(std::bit_cast<std::uint64_t>(split.comet->get<double>(channel, x, y)),
                                      std::bit_cast<std::uint64_t>(comet))
                                << x << ',' << y;
                        }
                    }
                }
            }
        }
    }
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(CometCompositeTest, TheSkyIsTheWholeChannelsWhenTheAnnulusHoldsTooFewSamples)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Plane frame(an::SampleType::Float32, 40u, 30u, 1u);
    paintField(frame, 2.0, 2.0, 0.0, 9u);
    // An aperture beyond the frame's corner leaves its annulus almost empty.
    const an::CometApertureCircle aperture{.center = {.x = -9.0, .y = -9.0}, .radius = 4.0};
    const an::CometApertureSplitPlan plan = makeSplitPlan({.aperture = aperture});
    const SplitOutput split = runSplit(execution, plan, frame);
    ASSERT_TRUE(split.result.hasValue()) << split.result.status().message();
    EXPECT_LT(split.result->annulusSamples[0], 8u);
    EXPECT_TRUE(split.result->wholeChannelBackground[0]);
    EXPECT_EQ(split.result->background[0], productSky(frame, 0u, aperture, an::SampleType::Float32));
    EXPECT_GT(split.result->workingBytes, 0u);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(CometCompositeTest, TheSplitKeepsTheFramesValidityAndLeavesRejectedSamplesOutOfTheSky)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::CometApertureCircle aperture{.center = {.x = 24.5, .y = 20.5}, .radius = 4.0};
    const an::CometApertureSplitPlan plan = makeSplitPlan({.aperture = aperture});

    // Under a finite-only rule a not-a-number sample is rejected: out of the sky, not-a-number in both layers.
    Plane finite(an::SampleType::Float32, 48u, 40u, 1u, an::ValidityRule::FiniteOnly);
    paintField(finite, 24.5, 20.5, 0.0, 5u);
    std::vector<bool> rejected(48u * 40u, false);
    for (const auto& [x, y] : {std::pair{31u, 20u}, std::pair{24u, 28u}, std::pair{17u, 21u}, std::pair{2u, 3u}})
    {
        finite.put(0u, x, y, kNaN);
        rejected[static_cast<std::size_t>(y) * 48u + x] = true;
    }
    const SplitOutput split = runSplit(execution, plan, finite);
    ASSERT_TRUE(split.result.hasValue()) << split.result.status().message();
    EXPECT_EQ(split.result->run.selection.valid, 48u * 40u - 4u);
    EXPECT_EQ(split.result->background[0], productSky(finite, 0u, aperture, an::SampleType::Float32, &rejected));
    EXPECT_TRUE(std::isnan(split.star->get<float>(0u, 31u, 20u)));
    EXPECT_TRUE(std::isnan(split.comet->get<float>(0u, 2u, 3u)));
    EXPECT_FALSE(std::isnan(split.star->get<float>(0u, 30u, 20u)));

    // Under a mask the rejected samples are written as zero, which the mask beside the layers keeps.
    Plane masked(an::SampleType::Float32, 48u, 40u, 1u, an::ValidityRule::ExplicitMask);
    paintField(masked, 24.5, 20.5, 0.0, 5u);
    masked.addMask();
    masked.clearBit(31u, 20u);
    masked.put(0u, 31u, 20u, 50.0);
    std::vector<bool> maskedOut(48u * 40u, false);
    maskedOut[20u * 48u + 31u] = true;
    const SplitOutput maskedSplit = runSplit(execution, plan, masked);
    ASSERT_TRUE(maskedSplit.result.hasValue()) << maskedSplit.result.status().message();
    EXPECT_EQ(maskedSplit.result->background[0], productSky(masked, 0u, aperture, an::SampleType::Float32, &maskedOut));
    EXPECT_EQ(maskedSplit.star->get<float>(0u, 31u, 20u), 0.0f);
    EXPECT_EQ(maskedSplit.comet->get<float>(0u, 31u, 20u), 0.0f);

    for (const an::ValidityRule rule : {an::ValidityRule::ZeroIsNoData})
    {
        Plane valued(an::SampleType::Float32, 48u, 40u, 1u, rule);
        paintField(valued, 24.5, 20.5, 0.0, 5u);
        EXPECT_EQ(runSplit(execution, plan, valued).result.status().code(), an::ErrorCode::Unsupported);
    }

    // A sample that is not finite where the rule admits it is malformed data.
    Plane admitted(an::SampleType::Float32, 48u, 40u, 1u);
    paintField(admitted, 24.5, 20.5, 0.0, 5u);
    admitted.put(0u, 5u, 5u, kNaN);
    EXPECT_EQ(runSplit(execution, plan, admitted).result.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(CometCompositeTest, EitherLayerIsWrittenAloneAsItIsBesideTheOther)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Plane frame(an::SampleType::Float32, 64u, 48u, 3u);
    paintField(frame, 30.5, 22.5, 0.0, 1u);
    const an::CometApertureSplitPlan plan =
        makeSplitPlan({.aperture = {.center = {.x = 30.5, .y = 22.5}, .radius = 5.0}, .feather = 0.0});
    const SplitOutput both = runSplit(execution, plan, frame);
    const SplitOutput starOnly = runSplit(execution, plan, frame, true, false);
    const SplitOutput cometOnly = runSplit(execution, plan, frame, false, true);
    ASSERT_TRUE(both.result.hasValue() && starOnly.result.hasValue() && cometOnly.result.hasValue());
    // The layers hold the work being compared: a sky above zero, and the frame where the comet is not.
    ASSERT_GT(both.result->background[0], 0.05);
    ASSERT_EQ(starOnly.star->get<float>(0u, 2u, 2u), static_cast<float>(frame.read(0u, 2u, 2u)));
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 48u; ++y)
        {
            for (std::uint32_t x = 0u; x < 64u; ++x)
            {
                ASSERT_EQ(std::bit_cast<std::uint32_t>(starOnly.star->get<float>(channel, x, y)),
                          std::bit_cast<std::uint32_t>(both.star->get<float>(channel, x, y)));
                ASSERT_EQ(std::bit_cast<std::uint32_t>(cometOnly.comet->get<float>(channel, x, y)),
                          std::bit_cast<std::uint32_t>(both.comet->get<float>(channel, x, y)));
            }
        }
    }
    // A hard edge: one inside the radius, zero beyond it.
    EXPECT_EQ(both.star->get<float>(0u, 30u, 22u), static_cast<float>(both.result->background[0]));
    EXPECT_EQ(both.comet->get<float>(0u, 0u, 0u), static_cast<float>(both.result->background[0]));
}

TEST(CometCompositeTest, AFloatingFrameIsReadWhereItLiesAsTheWorkingPlanesWouldReadIt)
{
    // A floating frame is split from its own samples, with no working copy the size of the frame; an integer frame
    // still goes through working planes. Both answer the product's arithmetic, which the cases above hold; here the
    // reading in place is held to what only it does.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::CometApertureCircle aperture{.center = {.x = 40.25, .y = 30.75}, .radius = 6.5};
    const an::CometApertureSplitPlan plan = makeSplitPlan({.aperture = aperture});

    // Memory: an annulus's worth for a floating frame, the frame's worth of working planes for an integer one.
    Plane floatingFrame(an::SampleType::Float32, 256u, 192u, 2u);
    paintField(floatingFrame, 40.25, 30.75, 0.0, 4u);
    const std::uint64_t frameBytes = floatingFrame.layout->totalBytes();
    const std::uint64_t before = engine.memoryResource()->counters().totalBytes;
    const SplitOutput inPlace = runSplit(execution, plan, floatingFrame);
    ASSERT_TRUE(inPlace.result.hasValue()) << inPlace.result.status().message();
    EXPECT_LT(engine.memoryResource()->counters().totalBytes - before, frameBytes / 8u);
    EXPECT_LT(inPlace.result->workingBytes, frameBytes / 8u);
    Plane integerFrame(an::SampleType::UInt16, 256u, 192u, 2u);
    paintField(integerFrame, 40.25, 30.75, 0.0, 4u);
    const SplitOutput throughPlanes = runSplit(execution, plan, integerFrame);
    ASSERT_TRUE(throughPlanes.result.hasValue()) << throughPlanes.result.status().message();
    EXPECT_GE(throughPlanes.result->workingBytes, frameBytes);

    // The same frame interleaved is read at its stride, into the same layers.
    const std::optional<an::ImageLayout> interleavedLayout =
        an::ImageLayout::create({.extent = floatingFrame.layout->extent(),
                                 .elementSizeBytes = 4u,
                                 .samplesPerPixel = 2u,
                                 .planeCount = 1u,
                                 .rowAlignmentBytes = 4u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(interleavedLayout.has_value());
    std::vector<float> interleaved(static_cast<std::size_t>(interleavedLayout->totalBytes() / sizeof(float)), -1.0f);
    const std::size_t rowSamples = static_cast<std::size_t>(interleavedLayout->rowStrideBytes() / sizeof(float));
    for (std::uint32_t y = 0u; y < 192u; ++y)
    {
        for (std::uint32_t x = 0u; x < 256u; ++x)
        {
            for (std::uint32_t channel = 0u; channel < 2u; ++channel)
            {
                interleaved[y * rowSamples + static_cast<std::size_t>(x) * 2u + channel] =
                    floatingFrame.get<float>(channel, x, y);
            }
        }
    }
    const an::CometCompositeSource interleavedSource{
        .descriptor = &*floatingFrame.descriptor, .samples = interleaved.data(), .layout = &*interleavedLayout};
    Plane interleavedStar(an::SampleType::Float32, 256u, 192u, 2u);
    Plane interleavedComet(an::SampleType::Float32, 256u, 192u, 2u);
    const an::Result<an::CometApertureSplitResult> interleavedSplit =
        an::splitCometAperture(execution, plan, interleavedSource, interleavedStar.bytes(), &*interleavedStar.layout,
                               interleavedComet.bytes(), &*interleavedComet.layout);
    ASSERT_TRUE(interleavedSplit.hasValue()) << interleavedSplit.status().message();
    for (std::uint32_t channel = 0u; channel < 2u; ++channel)
    {
        EXPECT_EQ(interleavedSplit->background[channel], inPlace.result->background[channel]) << channel;
    }
    EXPECT_EQ(std::memcmp(interleavedStar.bytes(), inPlace.star->bytes(), frameBytes), 0);
    EXPECT_EQ(std::memcmp(interleavedComet.bytes(), inPlace.comet->bytes(), frameBytes), 0);
    ASSERT_GT(inPlace.result->background[0], 0.05) << "the layers hold the work being compared";

    // A mask with a plane per channel: a pixel one channel rejects is rejected in both, out of either sky.
    Plane masked(an::SampleType::Float32, 96u, 64u, 2u, an::ValidityRule::ExplicitMask);
    paintField(masked, 40.25, 30.75, 0.0, 7u);
    masked.put(1u, 51u, 30u, 50.0);
    masked.put(0u, 28u, 25u, 50.0);
    const std::uint64_t maskRowBytes = an::packedMaskRowBytes(96u);
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(maskRowBytes * 64u * 2u), 0xFFu);
    const auto clear = [&](std::uint32_t plane, std::uint32_t x, std::uint32_t y) {
        bits[static_cast<std::size_t>((plane * 64u + y) * maskRowBytes + x / 8u)] &=
            static_cast<std::uint8_t>(~(1u << (x % 8u)));
    };
    clear(1u, 51u, 30u);
    clear(0u, 28u, 25u);
    const std::optional<an::ImageExtent> twoPlanes = an::ImageExtent::create(96u, 64u, 2u, an::defaultEngineLimits());
    ASSERT_TRUE(twoPlanes.has_value());
    const std::optional<an::ValidityMaskView<const std::uint8_t>> perChannel =
        an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>(bits), *twoPlanes);
    ASSERT_TRUE(perChannel.has_value());
    ASSERT_EQ(perChannel->planeCount(), 2u);
    an::CometCompositeSource maskedSource = masked.source();
    maskedSource.validity.mask = &*perChannel;
    Plane maskedStar(an::SampleType::Float32, 96u, 64u, 2u);
    Plane maskedComet(an::SampleType::Float32, 96u, 64u, 2u);
    const an::Result<an::CometApertureSplitResult> maskedSplit =
        an::splitCometAperture(execution, plan, maskedSource, maskedStar.bytes(), &*maskedStar.layout,
                               maskedComet.bytes(), &*maskedComet.layout);
    ASSERT_TRUE(maskedSplit.hasValue()) << maskedSplit.status().message();
    EXPECT_EQ(maskedSplit->run.selection.valid, 96u * 64u - 2u);
    std::vector<bool> rejected(96u * 64u, false);
    rejected[30u * 96u + 51u] = true;
    rejected[25u * 96u + 28u] = true;
    for (std::uint32_t channel = 0u; channel < 2u; ++channel)
    {
        EXPECT_EQ(maskedSplit->background[channel],
                  productSky(masked, channel, aperture, an::SampleType::Float32, &rejected))
            << channel;
        for (const auto& [x, y] : {std::pair{51u, 30u}, std::pair{28u, 25u}})
        {
            EXPECT_EQ(maskedStar.get<float>(channel, x, y), 0.0f) << channel << ' ' << x << ',' << y;
            EXPECT_EQ(maskedComet.get<float>(channel, x, y), 0.0f) << channel << ' ' << x << ',' << y;
        }
        EXPECT_NE(maskedStar.get<float>(channel, 52u, 30u), 0.0f);
    }

    // A sample that is not finite where it is admitted is refused before either layer is written.
    Plane malformed(an::SampleType::Float32, 96u, 64u, 1u);
    paintField(malformed, 40.25, 30.75, 0.0, 9u);
    malformed.put(0u, 70u, 50u, kNaN);
    Plane untouched(an::SampleType::Float32, 96u, 64u, 1u);
    std::fill(untouched.storage.begin(), untouched.storage.end(), 7.0);
    const std::vector<double> sentinel = untouched.storage;
    EXPECT_EQ(an::splitCometAperture(execution, plan, malformed.source(), untouched.bytes(), &*untouched.layout,
                                     nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(untouched.storage, sentinel) << "nothing is written before the frame is found malformed";
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(CometCompositeTest, MalformedSplitsAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::CometApertureCircle aperture{.center = {.x = 10.0, .y = 10.0}, .radius = 3.0};
    EXPECT_EQ(
        an::planCometApertureSplit({.aperture = {.center = {.x = kNaN, .y = 1.0}, .radius = 3.0}}).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planCometApertureSplit({.aperture = {.center = {.x = 1.0, .y = 1.0}, .radius = 0.0}}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planCometApertureSplit({.aperture = aperture, .feather = -1.0}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planCometApertureSplit({.aperture = aperture, .outputSampleType = an::SampleType::UInt16}).status().code(),
        an::ErrorCode::InvalidArgument);

    const an::CometApertureSplitPlan plan = makeSplitPlan({.aperture = aperture});
    Plane frame(an::SampleType::Float32, 24u, 20u, 1u);
    paintField(frame, 10.0, 10.0, 0.0, 2u);
    Plane layer(an::SampleType::Float32, 24u, 20u, 1u);
    Plane narrow(an::SampleType::Float32, 23u, 20u, 1u);
    Plane wide(an::SampleType::Float64, 24u, 20u, 1u);
    EXPECT_EQ(
        an::splitCometAperture(execution, plan, frame.source(), nullptr, nullptr, nullptr, nullptr).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::splitCometAperture(execution, plan, frame.source(), layer.bytes(), nullptr, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::splitCometAperture(execution, plan, frame.source(), narrow.bytes(), &*narrow.layout, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::splitCometAperture(execution, plan, frame.source(), wide.bytes(), &*wide.layout, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::splitCometAperture(execution, plan, frame.source(), frame.bytes(), &*frame.layout, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::splitCometAperture(execution, plan, frame.source(), layer.bytes(), &*layer.layout, layer.bytes(),
                                     &*layer.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    Plane five(an::SampleType::Float32, 8u, 8u, 5u);
    Plane fiveLayer(an::SampleType::Float32, 8u, 8u, 5u);
    EXPECT_EQ(
        an::splitCometAperture(execution, plan, five.source(), fiveLayer.bytes(), &*fiveLayer.layout, nullptr, nullptr)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::cometApertureSplitLayout(plan, *five.descriptor).status().code(),
              an::ErrorCode::IncompatibleArguments);
    const an::Result<an::ImageLayoutRequest> layout = an::cometApertureSplitLayout(plan, *frame.descriptor);
    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout->elementSizeBytes, 4u);
    EXPECT_EQ(layout->planeCount, 1u);

    const an::ExecutionContext unheld;
    EXPECT_EQ(an::splitCometAperture(unheld, plan, frame.source(), layer.bytes(), &*layer.layout, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    // A stopped operation on an engine of its own, because the live execution above has this engine's budget.
    const an::EngineContext other = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(other, stop.get_token());
    EXPECT_EQ(an::splitCometAperture(stopped, plan, frame.source(), layer.bytes(), &*layer.layout, nullptr, nullptr)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(liveBytes(other), 0u);
}

TEST(CometCompositeTest, TheMaskIsTheLargestFeatheredWeightOfEveryAperture)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<an::CometApertureCircle, 3u> apertures{
        an::CometApertureCircle{.center = {.x = 20.25, .y = 15.5}, .radius = 5.0},
        an::CometApertureCircle{.center = {.x = 27.5, .y = 16.75}, .radius = 4.0},
        an::CometApertureCircle{.center = {.x = 60.0, .y = 40.0}, .radius = 3.0}};
    for (const std::optional<double> feather :
         {std::optional<double>{}, std::optional<double>{0.0}, std::optional<double>{6.5}})
    {
        Plane mask(an::SampleType::Float32, 72u, 48u, 1u);
        const an::Result<an::CometMaskResult> drawn =
            an::renderCometMask(execution, apertures, {.feather = feather}, mask.bytes(), *mask.layout);
        ASSERT_TRUE(drawn.hasValue()) << drawn.status().message();
        EXPECT_EQ(drawn->apertures, 3u);
        std::uint64_t covered = 0u;
        for (std::uint32_t y = 0u; y < 48u; ++y)
        {
            for (std::uint32_t x = 0u; x < 72u; ++x)
            {
                float expected = 0.0f;
                for (const an::CometApertureCircle& aperture : apertures)
                {
                    const double edge = feather.value_or((std::max)(2.0, aperture.radius * 0.45));
                    const double dx = (static_cast<double>(x) + 0.5) - aperture.center.x;
                    const double dy = (static_cast<double>(y) + 0.5) - aperture.center.y;
                    const double weight = productWeight(std::hypot(dx, dy), aperture.radius, edge);
                    if (weight > 0.0)
                    {
                        expected = static_cast<float>((std::max)(static_cast<double>(expected), weight));
                    }
                }
                ASSERT_EQ(std::bit_cast<std::uint32_t>(mask.get<float>(0u, x, y)),
                          std::bit_cast<std::uint32_t>(expected))
                    << x << ',' << y;
                covered += expected > 0.0f ? 1u : 0u;
            }
        }
        EXPECT_EQ(drawn->coveredPixels, covered);
    }

    // No aperture draws zeros everywhere.
    Plane empty(an::SampleType::Float32, 8u, 8u, 1u);
    empty.put(0u, 3u, 3u, 0.75);
    const an::Result<an::CometMaskResult> nothing =
        an::renderCometMask(execution, {}, {}, empty.bytes(), *empty.layout);
    ASSERT_TRUE(nothing.hasValue());
    EXPECT_EQ(nothing->coveredPixels, 0u);
    EXPECT_EQ(empty.get<float>(0u, 3u, 3u), 0.0f);

    Plane twoChannels(an::SampleType::Float32, 8u, 8u, 2u);
    Plane doubles(an::SampleType::Float64, 8u, 8u, 1u);
    EXPECT_EQ(an::renderCometMask(execution, apertures, {}, twoChannels.bytes(), *twoChannels.layout).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::renderCometMask(execution, apertures, {}, doubles.bytes(), *doubles.layout).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::renderCometMask(execution, apertures, {}, nullptr, *empty.layout).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::renderCometMask(execution, apertures, {.feather = -2.0}, empty.bytes(), *empty.layout).status().code(),
        an::ErrorCode::InvalidArgument);
    const std::array<an::CometApertureCircle, 1u> flat{an::CometApertureCircle{.center = {.x = 1.0, .y = 1.0}}};
    EXPECT_EQ(an::renderCometMask(execution, flat, {}, empty.bytes(), *empty.layout).status().code(),
              an::ErrorCode::InvalidArgument);
}

/// The product's bilinear read: zero outside the rectangle the pixel centres span.
[[nodiscard]] double productBilinear(const Plane& image, std::uint32_t channel, double u, double v)
{
    const auto width = static_cast<int>(image.width());
    const auto height = static_cast<int>(image.height());
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0 || v < 0.0 || u > static_cast<double>(width - 1) ||
        v > static_cast<double>(height - 1))
    {
        return 0.0;
    }
    const int x0 = (std::clamp)(static_cast<int>(std::floor(u)), 0, width - 1);
    const int y0 = (std::clamp)(static_cast<int>(std::floor(v)), 0, height - 1);
    const int x1 = (std::min)(x0 + 1, width - 1);
    const int y1 = (std::min)(y0 + 1, height - 1);
    const double fx = u - static_cast<double>(x0);
    const double fy = v - static_cast<double>(y0);
    const auto at = [&](int x, int y) {
        return image.read(channel, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                          image.sampleType == an::SampleType::Float64 ? an::SampleType::Float64
                                                                      : an::SampleType::Float32);
    };
    const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
    const double bottom = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
    return top * (1.0 - fy) + bottom * fy;
}

TEST(CometCompositeTest, TheBlendReadsTheCometStackWhereTheRegistrationsPutEachPosition)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const double scale : {1.0, 2.0})
    {
        const auto width = static_cast<std::uint32_t>(48.0 * scale);
        const auto height = static_cast<std::uint32_t>(36.0 * scale);
        Plane star(an::SampleType::Float32, width, height, 2u);
        Plane comet(an::SampleType::Float32, width + 6u, height + 4u, 2u);
        paintField(star, 20.0 * scale, 15.0 * scale, 0.0, 4u);
        paintField(comet, 23.0 * scale, 13.0 * scale, 2.0, 8u);
        Plane mask(an::SampleType::Float32, width, height, 1u);
        const std::array<an::CometApertureCircle, 2u> apertures{
            an::CometApertureCircle{.center = {.x = 20.0 * scale, .y = 15.0 * scale}, .radius = 6.0 * scale},
            an::CometApertureCircle{.center = {.x = 1.0, .y = 1.0}, .radius = 4.0}};
        ASSERT_TRUE(an::renderCometMask(execution, apertures, {}, mask.bytes(), *mask.layout).hasValue());

        // The star plane is the reference frame's own; the comet stack's canvas sits three columns right and two
        // rows up of it, and the comet registration moves the reference frame by (0.75, -1.5).
        const an::CometCompositeBlendRequest request{.starTransform = translation(0.0, 0.0),
                                                     .starPostTranslation = {.x = 0.0, .y = 0.0},
                                                     .cometTransform = translation(0.75, -1.5),
                                                     .cometPostTranslation = {.x = 3.0, .y = 2.0},
                                                     .scale = scale};
        const an::Result<an::CometCompositeBlendPlan> plan = an::planCometCompositeBlend(request);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        Plane composite(an::SampleType::Float32, width, height, 2u);
        const an::Result<an::CometCompositeBlendResult> blended = an::blendCometComposite(
            execution, *plan, star.source(), comet.source(), mask.source(), composite.bytes(), *composite.layout);
        ASSERT_TRUE(blended.hasValue()) << blended.status().message();
        EXPECT_EQ(blended->unmappedPixels, 0u);
        EXPECT_EQ(blended->keptPixels, 0u);
        std::uint64_t weighed = 0u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double weight = static_cast<double>(mask.get<float>(0u, x, y));
                weighed += weight > 0.0 ? 1u : 0u;
                const double u = ((static_cast<double>(x) + 0.5) / scale + 0.75 + 3.0) * scale - 0.5;
                const double v = ((static_cast<double>(y) + 0.5) / scale - 1.5 + 2.0) * scale - 0.5;
                for (std::uint32_t channel = 0u; channel < 2u; ++channel)
                {
                    const float starValue = star.get<float>(channel, x, y);
                    const float expected = weight > 0.0
                                               ? static_cast<float>(static_cast<double>(starValue) * (1.0 - weight) +
                                                                    productBilinear(comet, channel, u, v) * weight)
                                               : starValue;
                    ASSERT_EQ(std::bit_cast<std::uint32_t>(composite.get<float>(channel, x, y)),
                              std::bit_cast<std::uint32_t>(expected))
                        << scale << ' ' << x << ',' << y << ' ' << channel;
                }
            }
        }
        EXPECT_EQ(blended->blendedPixels, weighed);
        EXPECT_EQ(blended->run.selection.total, static_cast<std::uint64_t>(width) * height);
        EXPECT_EQ(blended->run.outputsWritten, static_cast<std::uint64_t>(width) * height * 2u);
    }
}

TEST(CometCompositeTest, TheBlendKeepsTheStarStackWhereTheCometStackOrTheMaskSaysNothing)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Plane star(an::SampleType::Float32, 32u, 24u, 1u, an::ValidityRule::FiniteOnly);
    Plane comet(an::SampleType::Float32, 32u, 24u, 1u, an::ValidityRule::FiniteOnly);
    paintField(star, 16.0, 12.0, 0.0, 1u);
    paintField(comet, 16.0, 12.0, 0.0, 2u);
    Plane mask(an::SampleType::Float64, 32u, 24u, 1u, an::ValidityRule::ExplicitMask);
    mask.addMask();
    for (std::uint32_t y = 0u; y < 24u; ++y)
    {
        for (std::uint32_t x = 0u; x < 32u; ++x)
        {
            mask.put(0u, x, y, x >= 10u && x < 22u ? 1.4 : -0.5);
        }
    }
    // A rejected star sample is copied, a rejected comet sample keeps the star stack, a masked-out weight is zero.
    star.put(0u, 12u, 12u, kNaN);
    comet.put(0u, 15u, 6u, kNaN);
    mask.clearBit(18u, 18u);
    const an::Result<an::CometCompositeBlendPlan> plan = an::planCometCompositeBlend({});
    ASSERT_TRUE(plan.hasValue());
    Plane composite(an::SampleType::Float32, 32u, 24u, 1u);
    const an::Result<an::CometCompositeBlendResult> blended = an::blendCometComposite(
        execution, *plan, star.source(), comet.source(), mask.source(), composite.bytes(), *composite.layout);
    ASSERT_TRUE(blended.hasValue()) << blended.status().message();
    EXPECT_TRUE(std::isnan(composite.get<float>(0u, 12u, 12u)));
    // The comet sample at (15, 6) is read by the pixels whose bilinear reads reach it: (14..15, 5..6).
    EXPECT_EQ(composite.get<float>(0u, 15u, 6u), star.get<float>(0u, 15u, 6u));
    EXPECT_EQ(composite.get<float>(0u, 14u, 5u), star.get<float>(0u, 14u, 5u));
    EXPECT_EQ(composite.get<float>(0u, 18u, 18u), star.get<float>(0u, 18u, 18u));
    // The weight is held to one, so a blended pixel is the comet stack's.
    EXPECT_EQ(composite.get<float>(0u, 11u, 3u), comet.get<float>(0u, 11u, 3u));
    // A negative weight is zero.
    EXPECT_EQ(composite.get<float>(0u, 2u, 3u), star.get<float>(0u, 2u, 3u));
    EXPECT_EQ(blended->keptPixels, 4u);
    EXPECT_EQ(blended->blendedPixels, 12u * 24u - 4u - 1u - 1u);
}

TEST(CometCompositeTest, MalformedBlendsAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    EXPECT_EQ(an::planCometCompositeBlend({.scale = 0.9}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planCometCompositeBlend({.starPostTranslation = {.x = kNaN, .y = 0.0}}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planCometCompositeBlend({.cometPostTranslation = {.x = 0.0, .y = kNaN}}).status().code(),
              an::ErrorCode::InvalidArgument);
    const an::Result<an::CometCompositeBlendPlan> plan = an::planCometCompositeBlend({});
    ASSERT_TRUE(plan.hasValue());
    Plane star(an::SampleType::Float32, 16u, 12u, 3u);
    Plane comet(an::SampleType::Float32, 20u, 14u, 3u);
    Plane mask(an::SampleType::Float32, 16u, 12u, 1u);
    Plane out(an::SampleType::Float32, 16u, 12u, 3u);
    const auto code = [&](const Plane& s, const Plane& c, const Plane& m, Plane& d) {
        return an::blendCometComposite(execution, *plan, s.source(), c.source(), m.source(), d.bytes(), *d.layout)
            .status()
            .code();
    };
    ASSERT_TRUE(an::blendCometComposite(execution, *plan, star.source(), comet.source(), mask.source(), out.bytes(),
                                        *out.layout)
                    .hasValue());
    Plane integer(an::SampleType::UInt16, 16u, 12u, 3u);
    EXPECT_EQ(code(integer, comet, mask, out), an::ErrorCode::IncompatibleArguments);
    Plane oneChannelComet(an::SampleType::Float32, 20u, 14u, 1u);
    EXPECT_EQ(code(star, oneChannelComet, mask, out), an::ErrorCode::IncompatibleArguments);
    Plane wideMask(an::SampleType::Float32, 17u, 12u, 1u);
    EXPECT_EQ(code(star, comet, wideMask, out), an::ErrorCode::IncompatibleArguments);
    Plane twoChannelMask(an::SampleType::Float32, 16u, 12u, 2u);
    EXPECT_EQ(code(star, comet, twoChannelMask, out), an::ErrorCode::IncompatibleArguments);
    Plane doubleOut(an::SampleType::Float64, 16u, 12u, 3u);
    EXPECT_EQ(code(star, comet, mask, doubleOut), an::ErrorCode::InvalidArgument);
    Plane smallOut(an::SampleType::Float32, 15u, 12u, 3u);
    EXPECT_EQ(code(star, comet, mask, smallOut), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::blendCometComposite(execution, *plan, star.source(), comet.source(), mask.source(), star.bytes(),
                                      *star.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const an::ExecutionContext unheld;
    EXPECT_EQ(
        an::blendCometComposite(unheld, *plan, star.source(), comet.source(), mask.source(), out.bytes(), *out.layout)
            .status()
            .code(),
        an::ErrorCode::NotInitialized);
}

TEST(CometCompositeTest, NoAnswerDependsOnTheThreadCount)
{
    Plane frame(an::SampleType::Float32, 160u, 120u, 3u);
    paintField(frame, 70.25, 60.5, 0.0, 6u);
    const an::CometApertureSplitPlan plan =
        makeSplitPlan({.aperture = {.center = {.x = 70.25, .y = 60.5}, .radius = 9.0}});
    const std::array<an::CometApertureCircle, 2u> apertures{
        an::CometApertureCircle{.center = {.x = 70.25, .y = 60.5}, .radius = 9.0},
        an::CometApertureCircle{.center = {.x = 80.0, .y = 58.0}, .radius = 9.0}};
    std::vector<std::vector<double>> answers;
    for (const std::uint32_t threads : {1u, 3u, 8u})
    {
        const an::EngineContext engine = makeEngine(threads);
        const an::ExecutionContext execution = makeExecution(engine);
        const SplitOutput split = runSplit(execution, plan, frame);
        ASSERT_TRUE(split.result.hasValue()) << split.result.status().message();
        Plane mask(an::SampleType::Float32, 160u, 120u, 1u);
        const an::Result<an::CometMaskResult> drawn =
            an::renderCometMask(execution, apertures, {}, mask.bytes(), *mask.layout);
        ASSERT_TRUE(drawn.hasValue());
        // Something is written, so a stub that writes nothing does not pass by agreeing with itself.
        ASSERT_GT(drawn->coveredPixels, 0u);
        Plane composite(an::SampleType::Float32, 160u, 120u, 3u);
        const an::Result<an::CometCompositeBlendResult> blended = an::blendCometComposite(
            execution, *an::planCometCompositeBlend({.cometTransform = translation(0.5, 0.25)}), split.star->source(),
            split.comet->source(), mask.source(), composite.bytes(), *composite.layout);
        ASSERT_TRUE(blended.hasValue()) << blended.status().message();
        ASSERT_GT(blended->blendedPixels, 0u);
        std::vector<double> answer;
        const std::array<const Plane*, 4u> planes{&*split.star, &*split.comet, &mask, &composite};
        for (const Plane* plane : planes)
        {
            answer.insert(answer.end(), plane->storage.begin(), plane->storage.end());
        }
        answers.push_back(std::move(answer));
    }
    ASSERT_EQ(answers.size(), 3u);
    EXPECT_EQ(std::memcmp(answers[0].data(), answers[1].data(), answers[0].size() * sizeof(double)), 0);
    EXPECT_EQ(std::memcmp(answers[0].data(), answers[2].data(), answers[0].size() * sizeof(double)), 0);
}

/// A plain average of frames through the stack executor, read back as one Float32 plane; empty on failure.
///
/// The stack runs in an engine of its own, because the caller's live execution holds its engine's budget.
[[nodiscard]] std::vector<float> stackAverage(const std::vector<Plane>& frames)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::StackRequest request{
        .input = an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(frames.size()),
                                           .descriptor = *frames[0].descriptor}};
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    std::vector<an::StackSourceBand> bands;
    for (std::uint32_t index = 0u; index < frames.size(); ++index)
    {
        bands.push_back(an::StackSourceBand{.sampleType = an::SampleType::Float32,
                                            .samples = frames[index].bytes(),
                                            .layout = &*frames[index].layout,
                                            .frameIndex = index,
                                            .firstRow = 0u,
                                            .validity = an::StackBandValidity{.descriptor = frames[index].validity}});
    }
    an::Result<an::ResidentStackFrameSource> source = an::ResidentStackFrameSource::create(bands);
    EXPECT_TRUE(source.hasValue()) << source.status().message();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::StackResult> stacked = an::executeStack(engine, execution, *model, *source, {}, {}, {});
    EXPECT_TRUE(stacked.hasValue()) << stacked.status().message();
    std::vector<float> plane;
    if (!stacked.hasValue())
    {
        return plane;
    }
    const std::optional<an::ImageLayout> layout = stacked->image.layout();
    for (std::uint32_t y = 0u; y < frames[0].height(); ++y)
    {
        for (std::uint32_t x = 0u; x < frames[0].width(); ++x)
        {
            float sample = 0.0f;
            std::memcpy(&sample,
                        static_cast<const std::byte*>(stacked->image.data()) + y * layout->rowStrideBytes() +
                            static_cast<std::uint64_t>(x) * sizeof(float),
                        sizeof(float));
            plane.push_back(sample);
        }
    }
    return plane;
}

TEST(CometCompositeTest, TheCompositeKeepsTheStarsAndTheCometSharpThroughTheStackExecutor)
{
    // Six frames of a comet moving four pixels a frame across five fixed stars: the star-aligned frames show the
    // stars still and the comet moving, the comet-aligned ones the comet still and the stars moving back.
    constexpr std::uint32_t kFrames = 6u;
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 64u;
    constexpr double kCometY = 32.5;
    constexpr double kRadius = 6.0;
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<Plane> starLayers;
    std::vector<Plane> cometLayers;
    std::vector<Plane> plainFrames;
    std::vector<an::CometApertureCircle> apertures;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const double cometX = 30.5 + 4.0 * frame;
        Plane starAligned(an::SampleType::Float32, kWidth, kHeight, 1u);
        paintField(starAligned, cometX, kCometY, 0.0, frame);
        Plane cometAligned(an::SampleType::Float32, kWidth, kHeight, 1u);
        paintField(cometAligned, 30.5, kCometY, -4.0 * frame, frame + 50u);
        const an::CometApertureCircle onStars{.center = {.x = cometX, .y = kCometY}, .radius = kRadius};
        const an::CometApertureCircle onComet{.center = {.x = 30.5, .y = kCometY}, .radius = kRadius};
        const SplitOutput starSplit =
            runSplit(execution, makeSplitPlan({.aperture = onStars}), starAligned, true, false);
        const SplitOutput cometSplit =
            runSplit(execution, makeSplitPlan({.aperture = onComet}), cometAligned, false, true);
        ASSERT_TRUE(starSplit.result.hasValue() && cometSplit.result.hasValue());
        starLayers.push_back(*starSplit.star);
        cometLayers.push_back(*cometSplit.comet);
        plainFrames.push_back(std::move(starAligned));
        apertures.push_back(onStars);
    }
    const std::vector<float> starStack = stackAverage(starLayers);
    const std::vector<float> cometStack = stackAverage(cometLayers);
    const std::vector<float> plainStack = stackAverage(plainFrames);
    ASSERT_EQ(starStack.size(), static_cast<std::size_t>(kWidth) * kHeight);
    ASSERT_EQ(cometStack.size(), starStack.size());
    ASSERT_EQ(plainStack.size(), starStack.size());

    Plane starImage(an::SampleType::Float32, kWidth, kHeight, 1u);
    Plane cometImage(an::SampleType::Float32, kWidth, kHeight, 1u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            starImage.put(0u, x, y, starStack[static_cast<std::size_t>(y) * kWidth + x]);
            cometImage.put(0u, x, y, cometStack[static_cast<std::size_t>(y) * kWidth + x]);
        }
    }
    Plane mask(an::SampleType::Float32, kWidth, kHeight, 1u);
    ASSERT_TRUE(an::renderCometMask(execution, apertures, {}, mask.bytes(), *mask.layout).hasValue());
    Plane composite(an::SampleType::Float32, kWidth, kHeight, 1u);
    const an::Result<an::CometCompositeBlendResult> blended =
        an::blendCometComposite(execution, *an::planCometCompositeBlend({}), starImage.source(), cometImage.source(),
                                mask.source(), composite.bytes(), *composite.layout);
    ASSERT_TRUE(blended.hasValue()) << blended.status().message();

    const auto at = [&](const std::vector<float>& plane, std::uint32_t x, std::uint32_t y) {
        return static_cast<double>(plane[static_cast<std::size_t>(y) * kWidth + x]);
    };
    // The plain stack leaves a trail a sixth of the comet's height along the track; the composite does not.
    const double sky = 0.08 + 0.002;
    EXPECT_GT(at(plainStack, 46u, 32u) - sky, 0.05);
    EXPECT_LT(std::fabs(static_cast<double>(composite.get<float>(0u, 46u, 32u)) - sky), 0.01);
    // The comet is whole at the reference frame's position: the comet stack's own value there.
    EXPECT_EQ(composite.get<float>(0u, 30u, 32u), cometImage.get<float>(0u, 30u, 32u));
    EXPECT_GT(static_cast<double>(composite.get<float>(0u, 30u, 32u)) - sky, 0.5);
    // Away from the mask the composite is the star stack, and a star there is sharp.
    EXPECT_EQ(composite.get<float>(0u, 12u, 10u), starImage.get<float>(0u, 12u, 10u));
    EXPECT_GT(static_cast<double>(composite.get<float>(0u, 12u, 10u)) - sky, 0.4);
    EXPECT_EQ(liveBytes(engine), 0u);
}

} // namespace
