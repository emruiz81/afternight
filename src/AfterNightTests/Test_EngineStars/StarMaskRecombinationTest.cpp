// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the star-mask recombination: a starless image and its stars put back together, optionally
// through a mask.
//
// Oracles are written from the header's definitions: each image read as a fraction of full scale, the stars' edges
// tightened by the published spatial filter, the sum held at one in a normalized domain, the mask held to the unit
// interval and inverted on request, the blend `starless * (1 - mask) + sum * mask` computed in double and written
// once; a pixel any source rejects keeps the starless image's samples.

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Algorithms/Stars/StarMaskRecombination.h>
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
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
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

/// Samples of one representation in a layout, with a descriptor, owning their storage.
template <typename SampleT> struct Owned
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::StarMaskRecombinationSource source(an::KernelValidityView validity = {}) const
    {
        return an::StarMaskRecombinationSource{
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

/// A deterministic unit-interval sequence.
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
    const an::ImageLayout layout = layoutOf(type, width, height, channels, interleaved, interleaved ? 0u : 3u);
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

[[nodiscard]] an::StarMaskRecombinationPlan plan(const an::StarMaskRecombinationRequest& request = {})
{
    const an::Result<an::StarMaskRecombinationPlan> made = an::planStarMaskRecombination(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

template <typename ResultT>
[[nodiscard]] Owned<ResultT> destinationFor(const an::StarMaskRecombinationPlan& made,
                                            const an::ImageDescriptor& starless, const an::ImageDescriptor& stars,
                                            bool interleaved = false)
{
    const an::Result<an::ImageLayoutRequest> request = an::starMaskRecombinationLayout(made, starless, stars);
    EXPECT_TRUE(request.hasValue()) << request.status().message();
    EXPECT_EQ(request->elementSizeBytes, static_cast<std::uint32_t>(sizeof(ResultT)));
    const std::uint32_t width = starless.extent().width();
    const std::uint32_t height = starless.extent().height();
    const std::uint32_t channels = starless.extent().channelCount();
    const an::ImageLayout layout = layoutOf(an::kSampleTypeOf<ResultT>, width, height, channels, interleaved);
    return Owned<ResultT>{describe(an::kSampleTypeOf<ResultT>, width, height, channels), layout,
                          std::vector<ResultT>(layout.totalBytes() / sizeof(ResultT), ResultT{})};
}

template <typename StarlessT, typename StarsT, typename ResultT>
[[nodiscard]] an::StarMaskRecombinationResult
recombine(const an::ExecutionContext& execution, const an::StarMaskRecombinationPlan& made,
          const Owned<StarlessT>& starless, const Owned<StarsT>& stars, const an::StarMaskRecombinationSource* mask,
          Owned<ResultT>& result, an::KernelValidityView starlessValidity = {})
{
    const an::Result<an::StarMaskRecombinationResult> done = an::recombineStarMask(
        execution, made, starless.source(starlessValidity), stars.source(), mask, result.samples.data(), result.layout);
    EXPECT_TRUE(done.hasValue()) << done.status().message();
    return done.hasValue() ? *done : an::StarMaskRecombinationResult{};
}

/// An integer mask sample as the recombination holds it: its fraction of full scale in a Float32 working plane.
[[nodiscard]] double workingWeight(std::uint16_t code)
{
    const double exact = static_cast<double>(code) / 65535.0;
    return static_cast<double>(static_cast<float>(exact));
}

[[nodiscard]] double unitHeld(double value)
{
    return value > 0.0 ? (value < 1.0 ? value : 1.0) : 0.0;
}

} // namespace

TEST(StarMaskRecombinationTest, ThePlanStatesTheResultsRepresentationAsTheHigherRankedInput)
{
    EXPECT_EQ(an::planStarMaskRecombination({.edgeTightening = -0.1}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planStarMaskRecombination({.edgeTightening = std::numeric_limits<double>::quiet_NaN()}).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planStarMaskRecombination({.outputSampleType = static_cast<an::SampleType>(9u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    const an::StarMaskRecombinationPlan promoted = plan();
    const auto higher = [&](an::SampleType a, an::SampleType b) {
        return an::starMaskRecombinationSampleType(promoted, describe(a, 4u, 3u, 1u), describe(b, 4u, 3u, 1u));
    };
    EXPECT_EQ(higher(an::SampleType::UInt16, an::SampleType::Float32), an::SampleType::Float32);
    EXPECT_EQ(higher(an::SampleType::Float64, an::SampleType::Float32), an::SampleType::Float64);
    // A signed integer ranks below the unsigned one of its width, and a narrower below a wider.
    EXPECT_EQ(higher(an::SampleType::Int16, an::SampleType::UInt16), an::SampleType::UInt16);
    EXPECT_EQ(higher(an::SampleType::UInt8, an::SampleType::Int8), an::SampleType::UInt8);
    EXPECT_EQ(higher(an::SampleType::UInt16, an::SampleType::Int32), an::SampleType::Int32);
    EXPECT_EQ(higher(an::SampleType::UInt32, an::SampleType::Int32), an::SampleType::UInt32);
    const an::StarMaskRecombinationPlan stated = plan({.outputSampleType = an::SampleType::UInt8});
    EXPECT_EQ(an::starMaskRecombinationSampleType(stated, describe(an::SampleType::Float32, 4u, 3u, 1u),
                                                  describe(an::SampleType::Float64, 4u, 3u, 1u)),
              an::SampleType::UInt8);
    EXPECT_EQ(an::starMaskRecombinationLayout(promoted, describe(an::SampleType::Float32, 4u, 3u, 1u),
                                              describe(an::SampleType::Float32, 4u, 3u, 3u))
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

// In a normalized domain the sum is held at one; in a physical domain it is not held.
TEST(StarMaskRecombinationTest, TheSumIsHeldAtOneInANormalizedDomainAndNotInAPhysicalOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const an::NumericDomain domain : {an::NumericDomain::NormalizedLinear, an::NumericDomain::PhysicalScaled})
    {
        const Owned<float> starless = image<float>(23u, 9u, 3u, 3u, 0.0, 0.8, false, domain);
        const Owned<float> stars = image<float>(23u, 9u, 3u, 5u, 0.0, 0.7, true, domain);
        const an::StarMaskRecombinationPlan made = plan();
        Owned<float> result = destinationFor<float>(made, starless.descriptor, stars.descriptor);
        const an::StarMaskRecombinationResult done = recombine(execution, made, starless, stars, nullptr, result);
        EXPECT_EQ(done.run.selection.total, 23u * 9u);
        EXPECT_EQ(done.run.selection.valid, 23u * 9u);
        EXPECT_EQ(done.run.outputsWritten, 23u * 9u * 3u);
        std::uint64_t held = 0u;
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 9u; ++y)
            {
                for (std::uint32_t x = 0u; x < 23u; ++x)
                {
                    double sum = static_cast<double>(starless.at(c, x, y)) + static_cast<double>(stars.at(c, x, y));
                    if (domain == an::NumericDomain::NormalizedLinear && sum > 1.0)
                    {
                        sum = 1.0;
                        ++held;
                    }
                    ASSERT_EQ(result.at(c, x, y), static_cast<float>(sum)) << c << "," << x << "," << y;
                }
            }
        }
        if (domain == an::NumericDomain::NormalizedLinear)
        {
            EXPECT_GT(held, 0u) << "the fixture reaches above one";
        }
    }
}

// A mask blends the sum over the starless image: one shows the sum, zero the starless image; held to the unit
// interval, inverted on request, one channel for every channel or one per channel. A mask is read into a Float32
// working plane like every other source, so an integer mask's weight is its fraction rounded to Float32.
TEST(StarMaskRecombinationTest, AMaskBlendsTheSumOverTheStarlessImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> starless = image<float>(17u, 8u, 3u, 7u, 0.0, 0.5);
    const Owned<float> stars = image<float>(17u, 8u, 3u, 9u, 0.0, 0.6);
    Owned<float> grey = image<float>(17u, 8u, 1u, 11u, -0.2, 1.2);
    const Owned<std::uint16_t> perChannel = image<std::uint16_t>(17u, 8u, 3u, 13u, 0.0, 1.0, true);
    for (const bool invert : {false, true})
    {
        const an::StarMaskRecombinationPlan made = plan({.invertMask = invert});
        const an::StarMaskRecombinationSource greySource = grey.source();
        const an::StarMaskRecombinationSource channelSource = perChannel.source();
        Owned<float> byGrey = destinationFor<float>(made, starless.descriptor, stars.descriptor);
        Owned<float> byChannel = destinationFor<float>(made, starless.descriptor, stars.descriptor);
        (void)recombine(execution, made, starless, stars, &greySource, byGrey);
        (void)recombine(execution, made, starless, stars, &channelSource, byChannel);
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 8u; ++y)
            {
                for (std::uint32_t x = 0u; x < 17u; ++x)
                {
                    const double base = static_cast<double>(starless.at(c, x, y));
                    const double sum = (std::min)(base + static_cast<double>(stars.at(c, x, y)), 1.0);
                    for (const auto& [mask, result] :
                         {std::pair{unitHeld(static_cast<double>(grey.at(0u, x, y))), &byGrey},
                          std::pair{unitHeld(workingWeight(perChannel.at(c, x, y))), &byChannel}})
                    {
                        const double weight = invert ? 1.0 - mask : mask;
                        ASSERT_EQ(result->at(c, x, y), static_cast<float>(base * (1.0 - weight) + sum * weight))
                            << c << "," << x << "," << y << " invert " << invert;
                    }
                }
            }
        }
    }
}

// The stars' edges are tightened by the published filter before they are added.
TEST(StarMaskRecombinationTest, TheStarsEdgesAreTightenedBeforeTheyAreAdded)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> starless = image<float>(21u, 13u, 1u, 15u, 0.0, 0.3);
    Owned<float> stars = image<float>(21u, 13u, 1u, 17u, 0.0, 0.05);
    stars.at(0u, 10u, 6u) = 0.8f;
    stars.at(0u, 11u, 6u) = 0.5f;
    stars.at(0u, 10u, 7u) = 0.45f;
    const an::StarMaskRecombinationPlan made = plan({.edgeTightening = 0.8});
    Owned<float> result = destinationFor<float>(made, starless.descriptor, stars.descriptor);
    (void)recombine(execution, made, starless, stars, nullptr, result);

    // The oracle tightens a tightly packed copy of the stars with the published filter.
    const an::ImageLayout tight = layoutOf(an::SampleType::Float32, 21u, 13u, 1u, false);
    std::vector<float> packed(21u * 13u);
    for (std::uint32_t y = 0u; y < 13u; ++y)
    {
        for (std::uint32_t x = 0u; x < 21u; ++x)
        {
            packed[y * 21u + x] = stars.at(0u, x, y);
        }
    }
    std::vector<float> tightened(packed.size(), 0.0f);
    const an::Result<an::EdgeTightenPlan> edge = an::planEdgeTighten(an::EdgeTightenRequest{.amount = 0.8});
    ASSERT_TRUE(edge.hasValue());
    ASSERT_TRUE(an::filterImageEdgeTighten(execution, an::SampleType::Float32, packed.data(), tight, tightened.data(),
                                           tight, *edge)
                    .hasValue());
    std::uint64_t changed = 0u;
    for (std::uint32_t y = 0u; y < 13u; ++y)
    {
        for (std::uint32_t x = 0u; x < 21u; ++x)
        {
            const double sum =
                (std::min)(static_cast<double>(starless.at(0u, x, y)) + static_cast<double>(tightened[y * 21u + x]),
                           1.0);
            ASSERT_EQ(result.at(0u, x, y), static_cast<float>(sum)) << x << "," << y;
            changed += tightened[y * 21u + x] != packed[y * 21u + x] ? 1u : 0u;
        }
    }
    EXPECT_GT(changed, 0u) << "the tightening changes the stars";
}

// When every image is Float32 and every sample is selected, the recombination runs straight from the images into the
// destination and restates the edge tightening's arithmetic. The finite-only rule selects the same samples of these
// images through the working route, so the two must write one answer, bit for bit: for one channel and three, planar
// and interleaved, with no mask, a grey mask and a mask per channel, inverted or not, and with tightenings that reach
// both of the filter's clamps.
TEST(StarMaskRecombinationTest, TheDirectWalkIsTheWorkingRoute)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    std::uint64_t seed = 101u;
    for (const std::uint32_t channels : {1u, 3u})
    {
        for (const bool interleaved : {false, true})
        {
            for (const double amount : {0.0, 0.5, 6.0})
            {
                for (const std::uint32_t maskKind : {0u, 1u, 2u})
                {
                    for (const bool invert : {false, true})
                    {
                        const Owned<float> starless = image<float>(23u, 9u, channels, ++seed, 0.0, 0.6, interleaved);
                        const Owned<float> stars = image<float>(23u, 9u, channels, ++seed, 0.0, 0.7, interleaved);
                        const std::uint32_t maskChannels = maskKind == 2u ? channels : 1u;
                        const Owned<float> mask =
                            image<float>(23u, 9u, maskChannels, ++seed, -0.2, 1.2, interleaved && maskChannels > 1u);
                        const an::StarMaskRecombinationSource maskSource = mask.source();
                        const an::StarMaskRecombinationSource* const weights = maskKind == 0u ? nullptr : &maskSource;
                        const an::StarMaskRecombinationPlan made =
                            plan({.edgeTightening = amount, .invertMask = invert});
                        Owned<float> direct = destinationFor<float>(made, starless.descriptor, stars.descriptor);
                        Owned<float> working = destinationFor<float>(made, starless.descriptor, stars.descriptor);
                        const an::StarMaskRecombinationResult directRun =
                            recombine(execution, made, starless, stars, weights, direct);
                        const an::StarMaskRecombinationResult workingRun =
                            recombine(execution, made, starless, stars, weights, working,
                                      an::KernelValidityView{.descriptor = *finite});
                        // Both routes did the work they are compared on: every sample written, most of them not zero.
                        const std::uint64_t samples = 23u * 9u * channels;
                        EXPECT_EQ(directRun.run.outputsWritten, samples);
                        EXPECT_EQ(workingRun.run.outputsWritten, samples);
                        std::uint32_t differing = 0u;
                        std::uint64_t nonZero = 0u;
                        for (std::size_t index = 0u; index < direct.samples.size(); ++index)
                        {
                            differing += std::bit_cast<std::uint32_t>(direct.samples[index]) !=
                                                 std::bit_cast<std::uint32_t>(working.samples[index])
                                             ? 1u
                                             : 0u;
                            nonZero += direct.samples[index] != 0.0f ? 1u : 0u;
                        }
                        EXPECT_GT(nonZero, samples / 2u);
                        EXPECT_EQ(differing, 0u)
                            << channels << " channels, " << (interleaved ? "interleaved" : "planar") << ", amount "
                            << amount << ", mask " << maskKind << (invert ? ", inverted" : "");
                    }
                }
            }
        }
    }
}

// Integer images are read at full scale and the result takes the higher representation, rounded to nearest.
TEST(StarMaskRecombinationTest, IntegerImagesAreReadAtFullScaleAndWrittenInTheHigherRepresentation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<std::uint16_t> starless = image<std::uint16_t>(11u, 6u, 3u, 19u, 0.0, 0.6);
    const Owned<std::uint8_t> stars = image<std::uint8_t>(11u, 6u, 3u, 21u, 0.0, 0.6);
    const an::StarMaskRecombinationPlan made = plan();
    EXPECT_EQ(an::starMaskRecombinationSampleType(made, starless.descriptor, stars.descriptor), an::SampleType::UInt16);
    Owned<std::uint16_t> result = destinationFor<std::uint16_t>(made, starless.descriptor, stars.descriptor, true);
    (void)recombine(execution, made, starless, stars, nullptr, result);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 11u; ++x)
            {
                const double sum = (std::min)(fraction(starless.at(c, x, y)) + fraction(stars.at(c, x, y)), 1.0);
                // The working sample is Float32, written once to the nearest code.
                ASSERT_EQ(
                    result.at(c, x, y),
                    (an::convertSample<float, std::uint16_t>(static_cast<float>(sum), an::ConversionPolicy{}).value));
            }
        }
    }
}

// A pixel any source rejects keeps the starless image's samples, and is counted.
TEST(StarMaskRecombinationTest, AnInvalidPixelKeepsTheStarlessImage)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> starless = image<float>(9u, 7u, 3u, 23u, 0.0, 0.5);
    starless.at(1u, 2u, 3u) = std::numeric_limits<float>::quiet_NaN();
    const Owned<float> stars = image<float>(9u, 7u, 3u, 25u, 0.0, 0.5);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::StarMaskRecombinationPlan made = plan();
    Owned<float> result = destinationFor<float>(made, starless.descriptor, stars.descriptor);
    const an::StarMaskRecombinationResult done =
        recombine(execution, made, starless, stars, nullptr, result, an::KernelValidityView{.descriptor = *finite});
    EXPECT_EQ(done.run.selection.valid, 9u * 7u - 1u);
    EXPECT_EQ(result.at(0u, 2u, 3u), starless.at(0u, 2u, 3u));
    EXPECT_EQ(result.at(1u, 2u, 3u), 0.0f) << "a sample the starless image rejects is zero";
    EXPECT_EQ(result.at(2u, 2u, 3u), starless.at(2u, 2u, 3u));
    EXPECT_EQ(result.at(0u, 3u, 3u), static_cast<float>(static_cast<double>(starless.at(0u, 3u, 3u)) +
                                                        static_cast<double>(stars.at(0u, 3u, 3u))));
    // Without the rule the not-a-number is a sample the image claims means something.
    EXPECT_EQ(an::recombineStarMask(execution, made, starless.source(), stars.source(), nullptr, result.samples.data(),
                                    result.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

// Images of different domains, a sensor-code pair, a mask that is not a fraction of full scale or not their shape,
// and a destination overlapping a source are refused.
TEST(StarMaskRecombinationTest, MismatchedInputsAreRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StarMaskRecombinationPlan made = plan();
    const Owned<float> starless = image<float>(8u, 5u, 1u, 27u);
    const Owned<float> physical = image<float>(8u, 5u, 1u, 29u, 0.0, 1.0, false, an::NumericDomain::PhysicalScaled);
    Owned<float> result = destinationFor<float>(made, starless.descriptor, starless.descriptor);
    EXPECT_EQ(an::recombineStarMask(execution, made, starless.source(), physical.source(), nullptr,
                                    result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const Owned<std::uint16_t> codes =
        image<std::uint16_t>(8u, 5u, 1u, 31u, 0.0, 1.0, false, an::NumericDomain::RawSensorCode);
    Owned<std::uint16_t> codeResult = destinationFor<std::uint16_t>(made, codes.descriptor, codes.descriptor);
    EXPECT_EQ(an::recombineStarMask(execution, made, codes.source(), codes.source(), nullptr, codeResult.samples.data(),
                                    codeResult.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const an::StarMaskRecombinationSource physicalMask = physical.source();
    EXPECT_EQ(an::recombineStarMask(execution, made, starless.source(), starless.source(), &physicalMask,
                                    result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    const Owned<float> wide = image<float>(9u, 5u, 1u, 33u);
    const an::StarMaskRecombinationSource wideMask = wide.source();
    EXPECT_EQ(an::recombineStarMask(execution, made, starless.source(), starless.source(), &wideMask,
                                    result.samples.data(), result.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::recombineStarMask(execution, made, starless.source(), starless.source(), nullptr,
                                    const_cast<float*>(starless.samples.data()), starless.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

// The result does not depend on the number of threads.
TEST(StarMaskRecombinationTest, TheResultDoesNotDependOnTheNumberOfThreads)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const Owned<float> starless = image<float>(301u, 97u, 3u, 35u, 0.0, 0.7);
    const Owned<float> stars = image<float>(301u, 97u, 3u, 37u, 0.0, 0.5);
    const Owned<float> mask = image<float>(301u, 97u, 1u, 39u);
    const an::StarMaskRecombinationSource maskSource = mask.source();
    const an::StarMaskRecombinationPlan made = plan({.edgeTightening = 0.5});
    Owned<float> serial = destinationFor<float>(made, starless.descriptor, stars.descriptor);
    Owned<float> parallel = destinationFor<float>(made, starless.descriptor, stars.descriptor);
    (void)recombine(one, made, starless, stars, &maskSource, serial);
    (void)recombine(eight, made, starless, stars, &maskSource, parallel);
    EXPECT_EQ(std::memcmp(serial.samples.data(), parallel.samples.data(), serial.samples.size() * sizeof(float)), 0);
    std::uint64_t written = 0u;
    for (const float value : serial.samples)
    {
        written += value != 0.0f ? 1u : 0u;
    }
    EXPECT_GT(written, serial.samples.size() / 2u) << "the comparison is of real work";
}
