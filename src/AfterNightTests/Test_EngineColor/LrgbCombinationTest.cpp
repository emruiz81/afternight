// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the LRGB combination: a colour image's chrominance combined with a luminance image's
// lightness.
//
// The oracle follows the header's definition one pixel at a time through the published per-pixel colour conversion
// and midtones transfer: the colour decomposed into CIE L*a*b*, the luminance stretched and the lightness replaced or
// blended, a* and b* moved along their own hue to the stretched chroma, the colour recomposed and held to the unit
// interval, each channel weighed and held again, in double and written once. The operation converts through the
// colour conversion's own stage arithmetic, so a Float32 result is held to the oracle bit for bit and a sixteen-bit
// one to half a code.

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Color/LrgbCombination.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 3u)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageDescriptor describe(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t channels,
                                           an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = type,
                                   .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = domain,
                                   .cfa = {},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ImageLayout layoutOf(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t channels, bool interleaved, std::uint32_t paddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    const std::uint32_t element = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = element,
                                         .samplesPerPixel = interleaved ? channels : 1u,
                                         .planeCount = interleaved ? 1u : channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t row =
        static_cast<std::uint64_t>(width * (interleaved ? channels : 1u) + paddingSamples) * element;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, row, row * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

template <typename SampleT> struct Owned
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::LrgbCombinationSource source(an::KernelValidityView validity = {}) const
    {
        return an::LrgbCombinationSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }

    [[nodiscard]] SampleT& at(std::uint32_t c, std::uint32_t x, std::uint32_t y)
    {
        return samples[index(c, x, y)];
    }

    [[nodiscard]] SampleT at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        return samples[index(c, x, y)];
    }

    [[nodiscard]] std::size_t index(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        const std::optional<std::uint64_t> offset =
            layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel());
        return static_cast<std::size_t>(*offset / sizeof(SampleT));
    }
};

class Sequence
{
public:
    explicit Sequence(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    [[nodiscard]] double next() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>((m_state >> 40) & 0xFFFFFFu) / static_cast<double>(0x1000000u);
    }

private:
    std::uint64_t m_state;
};

template <typename SampleT>
[[nodiscard]] Owned<SampleT> image(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                   std::uint64_t seed, double low = 0.0, double high = 1.0, bool interleaved = false,
                                   an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const an::ImageLayout layout = layoutOf(type, width, height, channels, interleaved, interleaved ? 0u : 2u);
    Owned<SampleT> made{describe(type, width, height, channels, domain), layout,
                        std::vector<SampleT>(layout.totalBytes() / sizeof(SampleT), SampleT{})};
    Sequence sequence{seed};
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double value = low + sequence.next() * (high - low);
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    made.at(c, x, y) = static_cast<SampleT>(value);
                }
                else
                {
                    made.at(c, x, y) = static_cast<SampleT>(
                        std::round(value * static_cast<double>((std::numeric_limits<SampleT>::max)())));
                }
            }
        }
    }
    return made;
}

template <typename SampleT> [[nodiscard]] double fraction(SampleT value)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<double>(value);
    }
    else
    {
        return an::convertSample<SampleT, double>(value, an::ConversionPolicy{}).value;
    }
}

[[nodiscard]] an::LrgbCombinationPlan plan(const an::LrgbCombinationRequest& request = {})
{
    const an::Result<an::LrgbCombinationPlan> made = an::planLrgbCombination(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

template <typename ResultT>
[[nodiscard]] Owned<ResultT> destinationFor(const an::LrgbCombinationPlan& made, const an::ImageDescriptor& color,
                                            bool interleaved = false)
{
    const an::Result<an::ImageLayoutRequest> request = an::lrgbCombinationLayout(made, color);
    EXPECT_TRUE(request.hasValue()) << request.status().message();
    const std::uint32_t width = color.extent().width();
    const std::uint32_t height = color.extent().height();
    const an::ImageLayout layout = layoutOf(an::kSampleTypeOf<ResultT>, width, height, 3u, interleaved);
    return Owned<ResultT>{describe(an::kSampleTypeOf<ResultT>, width, height, 3u), layout,
                          std::vector<ResultT>(layout.totalBytes() / sizeof(ResultT), ResultT{})};
}

/// One pixel combined as the header defines it, from samples read as Float32 and written as Float32.
[[nodiscard]] std::array<double, 3u> combinedPixel(const an::LrgbCombinationRequest& request,
                                                   const std::array<double, 3u>& rgb, double luminance)
{
    an::ColorEncoding lab;
    lab.schema = an::ChannelSchema::Lab;
    lab.whitePoint = request.colorEncoding.whitePoint;
    const an::Result<an::ColorConversionPlan> forward = an::planColorConversion(request.colorEncoding, lab);
    const an::Result<an::ColorConversionPlan> reverse = an::planColorConversion(lab, request.colorEncoding);
    EXPECT_TRUE(forward.hasValue() && reverse.hasValue());
    std::array<double, 3u> source{static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])};
    std::array<double, 3u> cie{};
    EXPECT_TRUE(an::convertColorPixel(*forward, source, cie).ok());
    if (request.chromaMidtones != 0.5)
    {
        const double chroma = std::hypot(cie[1], cie[2]);
        const double stretched =
            200.0 * *an::evaluateMidtonesTransfer(
                        chroma / 200.0, {.midtones = request.chromaMidtones, .shadows = 0.0, .highlights = 1.0});
        const double scale = chroma > 0.0 ? stretched / chroma : 0.0;
        cie[1] *= scale;
        cie[2] *= scale;
    }
    double lightness = static_cast<float>(luminance);
    if (request.lightnessMidtones != 0.5)
    {
        lightness = *an::evaluateMidtonesTransfer(
            lightness, {.midtones = request.lightnessMidtones, .shadows = 0.0, .highlights = 1.0});
    }
    cie[0] = 100.0 * (request.lightnessWeight == 1.0
                          ? lightness
                          : (cie[0] / 100.0) * (1.0 - request.lightnessWeight) + lightness * request.lightnessWeight);
    std::array<double, 3u> back{};
    EXPECT_TRUE(an::convertColorPixel(*reverse, cie, back).ok());
    std::array<double, 3u> result{};
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        double value = std::clamp(back[c], 0.0, 1.0);
        if (request.channelWeights[c] != 1.0)
        {
            value = std::clamp(value * request.channelWeights[c], 0.0, 1.0);
        }
        result[c] = static_cast<float>(value);
    }
    return result;
}

template <typename ColorT, typename LuminanceT, typename ResultT>
void expectCombination(const an::ExecutionContext& execution, const an::LrgbCombinationRequest& request,
                       const Owned<ColorT>& color, const Owned<LuminanceT>& luminance, Owned<ResultT>& result,
                       double bound)
{
    const an::LrgbCombinationPlan made = plan(request);
    const an::Result<an::LrgbCombinationResult> done =
        an::combineLrgb(execution, made, color.source(), luminance.source(), result.samples.data(), result.layout);
    ASSERT_TRUE(done.hasValue()) << done.status().message();
    const std::uint32_t width = color.descriptor.extent().width();
    const std::uint32_t height = color.descriptor.extent().height();
    EXPECT_EQ(done->run.selection.total, static_cast<std::uint64_t>(width) * height);
    EXPECT_EQ(done->run.outputsWritten, static_cast<std::uint64_t>(width) * height * 3u);
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::array<double, 3u> expected = combinedPixel(
                request, {fraction(color.at(0u, x, y)), fraction(color.at(1u, x, y)), fraction(color.at(2u, x, y))},
                fraction(luminance.at(0u, x, y)));
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                largest = (std::max)(largest, std::abs(fraction(result.at(c, x, y)) - expected[c]));
            }
        }
    }
    ::testing::Test::RecordProperty("largest", std::to_string(largest));
    EXPECT_LE(largest, bound);
}

} // namespace

TEST(LrgbCombinationTest, ThePlanAndTheLayoutStateWhatIsWritten)
{
    EXPECT_EQ(an::planLrgbCombination({.lightnessWeight = 1.5}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planLrgbCombination({.lightnessMidtones = -0.1}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planLrgbCombination({.chromaMidtones = std::numeric_limits<double>::quiet_NaN()}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planLrgbCombination({.channelWeights = {1.0, -0.2, 1.0}}).status().code(),
              an::ErrorCode::InvalidArgument);
    an::LrgbCombinationRequest lab;
    lab.colorEncoding.schema = an::ChannelSchema::Lab;
    EXPECT_EQ(an::planLrgbCombination(lab).status().code(), an::ErrorCode::InvalidArgument);
    const an::LrgbCombinationPlan made = plan({.outputSampleType = an::SampleType::UInt16});
    EXPECT_EQ(made.request().colorEncoding.transfer, an::ColorTransferFunction::Srgb);
    const an::Result<an::ImageLayoutRequest> layout =
        an::lrgbCombinationLayout(made, describe(an::SampleType::Float32, 7u, 3u, 3u));
    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout->elementSizeBytes, 2u);
    EXPECT_EQ(layout->planeCount, 3u);
    EXPECT_EQ(an::lrgbCombinationLayout(made, describe(an::SampleType::Float32, 7u, 3u, 1u)).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

// The defaults replace the colour image's lightness with the luminance image.
TEST(LrgbCombinationTest, TheDefaultsReplaceTheLightnessWithTheLuminance)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> color = image<float>(19u, 11u, 3u, 3u, 0.02, 0.9);
    const Owned<float> luminance = image<float>(19u, 11u, 1u, 5u, 0.0, 1.0);
    Owned<float> result = destinationFor<float>(plan(), color.descriptor);
    expectCombination(execution, an::LrgbCombinationRequest{}, color, luminance, result, 0.0);
}

// A blended lightness, a stretched luminance and chroma and three channel weights, each as defined.
TEST(LrgbCombinationTest, ABlendedStretchedAndWeightedCombinationIsItsDefinition)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> color = image<float>(23u, 13u, 3u, 7u, 0.0, 1.0, true);
    const Owned<float> luminance = image<float>(23u, 13u, 1u, 9u, 0.0, 1.0);
    const an::LrgbCombinationRequest request{
        .lightnessWeight = 0.6, .lightnessMidtones = 0.3, .chromaMidtones = 0.4, .channelWeights = {0.9, 1.0, 1.7}};
    Owned<float> result = destinationFor<float>(plan(request), color.descriptor, true);
    expectCombination(execution, request, color, luminance, result, 0.0);
    std::uint64_t heldAtOne = 0u;
    for (std::uint32_t y = 0u; y < 13u; ++y)
    {
        for (std::uint32_t x = 0u; x < 23u; ++x)
        {
            heldAtOne += result.at(2u, x, y) == 1.0f ? 1u : 0u;
        }
    }
    EXPECT_GT(heldAtOne, 0u) << "a weight above one reaches the hold";
}

// With the colour image's own lightness kept and nothing stretched, the colour comes back as it went.
TEST(LrgbCombinationTest, KeepingTheLightnessAndStretchingNothingReturnsTheColour)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> color = image<float>(17u, 9u, 3u, 11u, 0.01, 0.99);
    const Owned<float> luminance = image<float>(17u, 9u, 1u, 13u);
    const an::LrgbCombinationPlan made = plan({.lightnessWeight = 0.0});
    Owned<float> result = destinationFor<float>(made, color.descriptor);
    ASSERT_TRUE(
        an::combineLrgb(execution, made, color.source(), luminance.source(), result.samples.data(), result.layout)
            .hasValue());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 9u; ++y)
        {
            for (std::uint32_t x = 0u; x < 17u; ++x)
            {
                ASSERT_NEAR(result.at(c, x, y), color.at(c, x, y), 5.0e-6) << c << "," << x << "," << y;
            }
        }
    }
}

// Integer images are read at full scale and the result is written to the nearest code.
TEST(LrgbCombinationTest, IntegerImagesAreReadAtFullScaleAndWrittenToTheNearestCode)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<std::uint16_t> color = image<std::uint16_t>(13u, 7u, 3u, 15u, 0.05, 0.95, true);
    const Owned<std::uint8_t> luminance = image<std::uint8_t>(13u, 7u, 1u, 17u);
    const an::LrgbCombinationRequest request{.lightnessWeight = 0.8, .outputSampleType = an::SampleType::UInt16};
    Owned<std::uint16_t> result = destinationFor<std::uint16_t>(plan(request), color.descriptor);
    expectCombination(execution, request, color, luminance, result, 0.5 / 65535.0 + 1.0e-12);
}

// A pixel either image rejects keeps the colour image's samples, and is counted.
TEST(LrgbCombinationTest, AnInvalidPixelKeepsTheColourImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> color = image<float>(8u, 6u, 3u, 19u, 0.1, 0.8);
    Owned<float> luminance = image<float>(8u, 6u, 1u, 21u);
    luminance.at(0u, 3u, 2u) = std::numeric_limits<float>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::LrgbCombinationPlan made = plan();
    Owned<float> result = destinationFor<float>(made, color.descriptor);
    const an::Result<an::LrgbCombinationResult> done = an::combineLrgb(
        execution, made, color.source(), luminance.source(an::KernelValidityView{.descriptor = *finite}),
        result.samples.data(), result.layout);
    ASSERT_TRUE(done.hasValue()) << done.status().message();
    EXPECT_EQ(done->run.selection.valid, 47u);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_EQ(result.at(c, 3u, 2u), color.at(c, 3u, 2u)) << c;
    }
    EXPECT_EQ(an::combineLrgb(execution, made, color.source(), luminance.source(), result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

// A source not in a normalized domain, a luminance image of another shape and an overlapping destination are
// refused.
TEST(LrgbCombinationTest, MismatchedInputsAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::LrgbCombinationPlan made = plan();
    const Owned<float> color = image<float>(8u, 6u, 3u, 23u);
    const Owned<float> luminance = image<float>(8u, 6u, 1u, 25u);
    Owned<float> result = destinationFor<float>(made, color.descriptor);
    const Owned<float> physical = image<float>(8u, 6u, 1u, 27u, 0.0, 1.0, false, an::NumericDomain::PhysicalScaled);
    EXPECT_EQ(an::combineLrgb(execution, made, color.source(), physical.source(), result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const Owned<float> wide = image<float>(9u, 6u, 1u, 29u);
    EXPECT_EQ(an::combineLrgb(execution, made, color.source(), wide.source(), result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::combineLrgb(execution, made, color.source(), color.source(), result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::combineLrgb(execution, made, color.source(), luminance.source(),
                              const_cast<float*>(color.samples.data()), color.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

// The result does not depend on the number of threads.
TEST(LrgbCombinationTest, TheResultDoesNotDependOnTheNumberOfThreads)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const Owned<float> color = image<float>(211u, 89u, 3u, 31u);
    const Owned<float> luminance = image<float>(211u, 89u, 1u, 33u);
    const an::LrgbCombinationPlan made =
        plan({.lightnessWeight = 0.7, .lightnessMidtones = 0.4, .chromaMidtones = 0.6});
    Owned<float> serial = destinationFor<float>(made, color.descriptor);
    Owned<float> parallel = destinationFor<float>(made, color.descriptor);
    ASSERT_TRUE(an::combineLrgb(one, made, color.source(), luminance.source(), serial.samples.data(), serial.layout)
                    .hasValue());
    ASSERT_TRUE(
        an::combineLrgb(eight, made, color.source(), luminance.source(), parallel.samples.data(), parallel.layout)
            .hasValue());
    EXPECT_EQ(std::memcmp(serial.samples.data(), parallel.samples.data(), serial.samples.size() * sizeof(float)), 0);
    std::uint64_t written = 0u;
    for (const float value : serial.samples)
    {
        written += value != 0.0f ? 1u : 0u;
    }
    EXPECT_GT(written, serial.samples.size() / 2u) << "the comparison is of real work";
}
