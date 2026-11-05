// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the channel scope: one plane of a colour image in a named encoding, extracted,
// processed by the caller's operation and recomposed.
//
// The oracle for a component of another encoding is the scope's own sequence replayed per pixel through
// `convertColorPixel`, the colour transform's scalar meaning: promote, convert, narrow to the working
// plane's representation, put into the scope's units, apply the operation, take out of the units, convert
// back and narrow. The image path folds one division into a matrix for the sRGB-to-Lab conversion, so those
// comparisons carry a tolerance of a few units in the last place of a Float32; everything else is exact.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

constexpr an::ColorEncoding kSrgbEncoded{.schema = an::ChannelSchema::Rgb, .transfer = an::ColorTransferFunction::Srgb};

[[nodiscard]] an::ImageDescriptor describe(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           an::ChannelSchema schema,
                                           an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                           an::ColorSpace space = an::ColorSpace::Unspecified)
{
    const std::uint32_t channels =
        schema == an::ChannelSchema::Mono ? 1u : (schema == an::ChannelSchema::Rgba ? 4u : 3u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = type,
                                                               .channelSchema = schema,
                                                               .colorSpace = space,
                                                               .numericDomain = domain,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ChannelScopePlan plan(an::ChannelScope scope, const an::ColorEncoding& encoding = kSrgbEncoded,
                                        an::ChannelScopeUnits units = an::ChannelScopeUnits::UnitInterval)
{
    an::Result<an::ChannelScopePlan> made =
        an::planChannelScope(an::ChannelScopeRequest{.scope = scope, .sourceEncoding = encoding, .units = units});
    EXPECT_TRUE(made.hasValue()) << an::channelScopeName(scope) << ": " << made.status().message();
    return *made;
}

[[nodiscard]] std::vector<an::ChannelScope> everyScope()
{
    std::vector<an::ChannelScope> scopes;
    for (std::uint32_t index = 0u; index < an::kChannelScopeCount; ++index)
    {
        scopes.push_back(static_cast<an::ChannelScope>(index));
    }
    return scopes;
}

[[nodiscard]] bool encoded(an::ChannelScope scope)
{
    return scope != an::ChannelScope::Whole && scope != an::ChannelScope::Red && scope != an::ChannelScope::Green &&
           scope != an::ChannelScope::Blue;
}

/// Samples of one representation in a layout, with a descriptor, owning their storage.
template <typename SampleT> struct Owned
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::ChannelScopeSource source(an::KernelValidityView validity = {}) const
    {
        return an::ChannelScopeSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }

    [[nodiscard]] std::size_t index(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        return static_cast<std::size_t>(
            *layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel()) /
            sizeof(SampleT));
    }

    [[nodiscard]] SampleT at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        return samples[index(c, x, y)];
    }
};

/// An image of `schema` with every colour sample in [0.03, 0.97] of full scale, and an alpha of its own.
template <typename SampleT>
[[nodiscard]] Owned<SampleT> colourImage(std::uint32_t width, std::uint32_t height, an::ChannelSchema schema,
                                         bool interleave, std::uint64_t seed)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const std::uint32_t channels =
        schema == an::ChannelSchema::Rgba ? 4u : (schema == an::ChannelSchema::Mono ? 1u : 3u);
    const an::ImageLayout layout =
        interleave ? interleavedLayout(type, width, height, channels) : planarLayout(type, width, height, channels, 3u);
    Owned<SampleT> image{describe(type, width, height, schema), layout,
                         std::vector<SampleT>(layout.totalBytes() / sizeof(SampleT), SampleT{})};
    Sequence sequence{seed};
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double unit = 0.03 + 0.94 * static_cast<double>(sequence.next());
                SampleT value{};
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    value = static_cast<SampleT>(unit);
                }
                else
                {
                    value = static_cast<SampleT>(
                        std::lround(unit * static_cast<double>((std::numeric_limits<SampleT>::max)())));
                }
                image.samples[image.index(c, x, y)] = value;
            }
        }
    }
    return image;
}

[[nodiscard]] an::ImageLayout layoutOf(const an::ImageLayoutRequest& request)
{
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] double gamma(double value)
{
    return std::pow((std::max)(value, 0.0), 1.0 / 2.2);
}

[[nodiscard]] bool sameBits(double left, double right)
{
    return (std::isnan(left) && std::isnan(right)) || left == right;
}

/// An operation that maps every sample through a function and records what it was handed.
class MapOperation final : public an::ChannelScopeOperation
{
public:
    explicit MapOperation(std::function<double(double)> map) : m_map(std::move(map)) {}

    an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                   const an::ChannelScopeOutput& output) override
    {
        ++calls;
        inputSamples = input.samples;
        outputSamples = output.samples;
        sampleType = input.descriptor->sampleType();
        schema = input.descriptor->channelSchema();
        domain = input.descriptor->numericDomain();
        channels = input.descriptor->extent().channelCount();
        rule = input.validity.descriptor.rule();
        mask = input.validity.mask;
        maskPlane = input.validity.maskPlane;
        sameDescription = output.descriptor == input.descriptor || *output.descriptor == *input.descriptor;
        startedAsCopy = true;
        handed.clear();
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        const bool known = an::dispatchSampleType(sampleType, [&]<typename SampleT>() {
            for (std::uint32_t c = 0u; c < channels; ++c)
            {
                for (std::uint32_t y = 0u; y < in.extent().height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < in.extent().width(); ++x)
                    {
                        const SampleT value = *reinterpret_cast<const SampleT*>(
                            static_cast<const std::uint8_t*>(input.samples) +
                            *in.sampleByteOffset(c / in.samplesPerPixel(), y, x, c % in.samplesPerPixel()));
                        SampleT* const written = reinterpret_cast<SampleT*>(
                            static_cast<std::uint8_t*>(output.samples) +
                            *out.sampleByteOffset(c / out.samplesPerPixel(), y, x, c % out.samplesPerPixel()));
                        startedAsCopy =
                            startedAsCopy && sameBits(static_cast<double>(*written), static_cast<double>(value));
                        handed.push_back(static_cast<double>(value));
                        if constexpr (std::is_floating_point_v<SampleT>)
                        {
                            *written = static_cast<SampleT>(m_map(static_cast<double>(value)));
                        }
                        else
                        {
                            const double maximum = static_cast<double>((std::numeric_limits<SampleT>::max)());
                            const double mapped = m_map(static_cast<double>(value) / maximum);
                            *written =
                                static_cast<SampleT>(std::lround((std::min)((std::max)(mapped, 0.0), 1.0) * maximum));
                        }
                    }
                }
            }
        });
        return known ? an::Status::success() : an::Status::failure(an::ErrorCode::Internal);
    }

    int calls = 0;
    const void* inputSamples = nullptr;
    void* outputSamples = nullptr;
    an::SampleType sampleType = an::SampleType::UInt8;
    an::ChannelSchema schema = an::ChannelSchema::Named;
    an::NumericDomain domain = an::NumericDomain::PhysicalScaled;
    std::uint32_t channels = 0u;
    an::ValidityRule rule = an::ValidityRule::AllValid;
    const an::ValidityMaskView<const std::uint8_t>* mask = nullptr;
    std::uint32_t maskPlane = 0u;
    bool sameDescription = false;
    bool startedAsCopy = false;
    std::vector<double> handed;

private:
    std::function<double(double)> m_map;
};

/// The scope's sequence for one pixel of a Float32 source, replayed through the engine's scalar conversion.
[[nodiscard]] std::array<float, 3u> oracleFloat32(const an::ChannelScopePlan& made, std::array<float, 3u> rgb,
                                                  const std::function<double(double)>& map, double scale,
                                                  float* handedOut = nullptr)
{
    std::array<double, 3u> wide{rgb[0], rgb[1], rgb[2]};
    std::array<double, 3u> component{};
    EXPECT_TRUE(an::convertColorPixel(*made.forward(), wide, component).ok());
    std::array<float, 3u> working{static_cast<float>(component[0]), static_cast<float>(component[1]),
                                  static_cast<float>(component[2])};
    const std::uint32_t k = an::channelScopeComponent(made.request().scope);
    const float handed = scale == 1.0 ? working[k] : static_cast<float>(static_cast<double>(working[k]) * scale);
    if (handedOut != nullptr)
    {
        *handedOut = handed;
    }
    const float processed = static_cast<float>(map(static_cast<double>(handed)));
    working[k] = scale == 1.0 ? processed : static_cast<float>(static_cast<double>(processed) / scale);
    const std::array<double, 3u> back{working[0], working[1], working[2]};
    std::array<double, 3u> result{};
    EXPECT_TRUE(an::convertColorPixel(*made.inverse(), back, result).ok());
    return {static_cast<float>(result[0]), static_cast<float>(result[1]), static_cast<float>(result[2])};
}

/// The units scale a scope's plane is handed in: the white's tristimulus value for unit-interval XYZ.
[[nodiscard]] double unitScale(an::ChannelScope scope, an::ChannelScopeUnits units)
{
    if (units != an::ChannelScopeUnits::UnitInterval || an::channelScopeEncoding(scope) != an::ChannelSchema::Xyz)
    {
        return 1.0;
    }
    return 1.0 / an::whitePointTristimulus(an::ChromaticWhitePoint::D65)[an::channelScopeComponent(scope)];
}

TEST(ChannelScopeTest, ThePlannerRefusesWhatItCannotScope)
{
    EXPECT_EQ(an::planChannelScope({.scope = static_cast<an::ChannelScope>(an::kChannelScopeCount)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planChannelScope({.units = static_cast<an::ChannelScopeUnits>(2u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planChannelScope({.yCbCr = static_cast<an::YCbCrVariant>(2u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    const an::ColorEncoding noGamma{.schema = an::ChannelSchema::Rgb, .transfer = an::ColorTransferFunction::PureGamma};
    EXPECT_EQ(
        an::planChannelScope({.scope = an::ChannelScope::CieLightness, .sourceEncoding = noGamma}).status().code(),
        an::ErrorCode::InvalidArgument);
    const an::ColorEncoding withAlpha{.schema = an::ChannelSchema::Rgba};
    EXPECT_EQ(an::planChannelScope({.scope = an::ChannelScope::Red, .sourceEncoding = withAlpha}).status().code(),
              an::ErrorCode::InvalidArgument)
        << "alpha is the scope's to carry, so the encoding names red, green and blue";
    const an::ColorEncoding lab{.schema = an::ChannelSchema::Lab};
    EXPECT_EQ(an::planChannelScope({.sourceEncoding = lab}).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ChannelScopeTest, EveryScopeNamesItsEncodingItsComponentAndItself)
{
    struct Row
    {
        an::ChannelScope scope;
        an::ChannelSchema encoding;
        std::uint32_t component;
    };
    const std::array<Row, an::kChannelScopeCount> table{{
        {an::ChannelScope::Whole, an::ChannelSchema::Named, 0u},
        {an::ChannelScope::Red, an::ChannelSchema::Rgb, 0u},
        {an::ChannelScope::Green, an::ChannelSchema::Rgb, 1u},
        {an::ChannelScope::Blue, an::ChannelSchema::Rgb, 2u},
        {an::ChannelScope::CieLightness, an::ChannelSchema::Lab, 0u},
        {an::ChannelScope::CieA, an::ChannelSchema::Lab, 1u},
        {an::ChannelScope::CieB, an::ChannelSchema::Lab, 2u},
        {an::ChannelScope::CieChroma, an::ChannelSchema::Lch, 1u},
        {an::ChannelScope::CieHue, an::ChannelSchema::Lch, 2u},
        {an::ChannelScope::CieX, an::ChannelSchema::Xyz, 0u},
        {an::ChannelScope::CieY, an::ChannelSchema::Xyz, 1u},
        {an::ChannelScope::CieZ, an::ChannelSchema::Xyz, 2u},
        {an::ChannelScope::HslHue, an::ChannelSchema::Hsl, 0u},
        {an::ChannelScope::HslSaturation, an::ChannelSchema::Hsl, 1u},
        {an::ChannelScope::HslLightness, an::ChannelSchema::Hsl, 2u},
        {an::ChannelScope::HsvHue, an::ChannelSchema::Hsv, 0u},
        {an::ChannelScope::HsvSaturation, an::ChannelSchema::Hsv, 1u},
        {an::ChannelScope::HsvValue, an::ChannelSchema::Hsv, 2u},
        {an::ChannelScope::HsiHue, an::ChannelSchema::Hsi, 0u},
        {an::ChannelScope::HsiSaturation, an::ChannelSchema::Hsi, 1u},
        {an::ChannelScope::HsiIntensity, an::ChannelSchema::Hsi, 2u},
        {an::ChannelScope::YCbCrLuma, an::ChannelSchema::YCbCr, 0u},
        {an::ChannelScope::YCbCrBlueChroma, an::ChannelSchema::YCbCr, 1u},
        {an::ChannelScope::YCbCrRedChroma, an::ChannelSchema::YCbCr, 2u},
    }};
    std::set<std::string> names;
    for (std::uint32_t index = 0u; index < an::kChannelScopeCount; ++index)
    {
        const Row& row = table[index];
        EXPECT_EQ(static_cast<std::uint32_t>(row.scope), index) << "the order is a contract";
        EXPECT_TRUE(an::isChannelScope(row.scope));
        EXPECT_EQ(an::channelScopeEncoding(row.scope), row.encoding) << an::channelScopeName(row.scope);
        EXPECT_EQ(an::channelScopeComponent(row.scope), row.component) << an::channelScopeName(row.scope);
        const std::string name = an::channelScopeName(row.scope);
        EXPECT_NE(name, "UnknownChannelScope");
        EXPECT_TRUE(names.insert(name).second) << name;
    }
    const an::ChannelScope outside = static_cast<an::ChannelScope>(an::kChannelScopeCount);
    EXPECT_FALSE(an::isChannelScope(outside));
    EXPECT_STREQ(an::channelScopeName(outside), "UnknownChannelScope");
    EXPECT_EQ(an::channelScopeEncoding(outside), an::ChannelSchema::Named);
    EXPECT_EQ(an::channelScopeComponent(outside), 0u);
}

TEST(ChannelScopeTest, AScopeOfAnotherEncodingPlansTheWayThereAndBack)
{
    for (const an::ChannelScope scope : everyScope())
    {
        const an::ChannelScopePlan made = plan(scope);
        if (!encoded(scope))
        {
            EXPECT_FALSE(made.forward().has_value()) << an::channelScopeName(scope);
            EXPECT_FALSE(made.inverse().has_value());
            continue;
        }
        ASSERT_TRUE(made.forward().has_value()) << an::channelScopeName(scope);
        ASSERT_TRUE(made.inverse().has_value());
        EXPECT_EQ(made.forward()->source(), kSrgbEncoded);
        EXPECT_EQ(made.forward()->destination().schema, an::channelScopeEncoding(scope));
        EXPECT_EQ(made.inverse()->source(), made.forward()->destination());
        EXPECT_EQ(made.inverse()->destination(), kSrgbEncoded);
    }
    const an::ChannelScopePlan unit = plan(an::ChannelScope::CieLightness);
    const an::ChannelScopePlan natural =
        plan(an::ChannelScope::CieLightness, kSrgbEncoded, an::ChannelScopeUnits::Natural);
    EXPECT_EQ(unit.forward()->destination().perceptual, an::PerceptualEncoding::UnitPacked);
    EXPECT_EQ(natural.forward()->destination().perceptual, an::PerceptualEncoding::NaturalUnits);
    EXPECT_EQ(plan(an::ChannelScope::HslHue).forward()->destination().hue, an::HueConvention::TurnFraction);
    EXPECT_EQ(plan(an::ChannelScope::HsvHue, kSrgbEncoded, an::ChannelScopeUnits::Natural).forward()->destination().hue,
              an::HueConvention::Degrees);
    // HSL, HSV, HSI and luma-chroma are computed on the samples as they are encoded, not decoded first.
    EXPECT_EQ(plan(an::ChannelScope::HsvValue).forward()->destination().transfer, an::ColorTransferFunction::Srgb);
    const an::Result<an::ChannelScopePlan> bt709 = an::planChannelScope({.scope = an::ChannelScope::YCbCrLuma,
                                                                         .sourceEncoding = kSrgbEncoded,
                                                                         .yCbCr = an::YCbCrVariant::Rec709FullRange});
    ASSERT_TRUE(bt709.hasValue());
    EXPECT_EQ(bt709->forward()->destination().yCbCr, an::YCbCrVariant::Rec709FullRange);
}

TEST(ChannelScopeTest, EveryEncodedComponentIsProcessedThroughTheEnginesOwnConversion)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(23u, 11u, an::ChannelSchema::Rgb, false, 5u);
    for (const an::ChannelScope scope : everyScope())
    {
        if (!encoded(scope))
        {
            continue;
        }
        const an::ChannelScopePlan made = plan(scope);
        const an::ImageLayout destinationLayout = interleavedLayout(an::SampleType::Float32, 23u, 11u, 3u);
        std::vector<float> destination(destinationLayout.totalBytes() / sizeof(float), -1.0f);
        MapOperation operation(gamma);
        const an::Result<an::ChannelScopeResult> result =
            an::applyChannelScope(execution, made, source.source(), operation, destination.data(), destinationLayout);
        ASSERT_TRUE(result.hasValue()) << an::channelScopeName(scope) << ": " << result.status().message();
        EXPECT_EQ(result->scope, scope);
        EXPECT_EQ(result->invalidPixels, 0u);
        EXPECT_EQ(operation.calls, 1);
        EXPECT_EQ(operation.sampleType, an::SampleType::Float32);
        EXPECT_EQ(operation.schema, an::ChannelSchema::Mono);
        EXPECT_EQ(operation.channels, 1u);
        EXPECT_EQ(operation.rule, an::ValidityRule::AllValid);
        EXPECT_TRUE(operation.sameDescription);
        EXPECT_TRUE(operation.startedAsCopy) << an::channelScopeName(scope);

        const double scale = unitScale(scope, an::ChannelScopeUnits::UnitInterval);
        const double tolerance = an::channelScopeEncoding(scope) == an::ChannelSchema::Lab ||
                                         an::channelScopeEncoding(scope) == an::ChannelSchema::Lch
                                     ? 4.0e-6
                                     : 0.0;
        std::size_t pixel = 0u;
        for (std::uint32_t y = 0u; y < 11u; ++y)
        {
            for (std::uint32_t x = 0u; x < 23u; ++x, ++pixel)
            {
                float handed = 0.0f;
                const std::array<float, 3u> expected = oracleFloat32(
                    made, {source.at(0u, x, y), source.at(1u, x, y), source.at(2u, x, y)}, gamma, scale, &handed);
                EXPECT_NEAR(operation.handed[pixel], handed, tolerance)
                    << an::channelScopeName(scope) << " handed at " << x << "," << y;
                for (std::uint32_t c = 0u; c < 3u; ++c)
                {
                    const float written = destination[(static_cast<std::size_t>(y) * 23u + x) * 3u + c];
                    if (tolerance == 0.0)
                    {
                        EXPECT_EQ(written, expected[c])
                            << an::channelScopeName(scope) << " " << c << " at " << x << "," << y;
                    }
                    else
                    {
                        EXPECT_NEAR(written, expected[c], 2.0 * tolerance)
                            << an::channelScopeName(scope) << " " << c << " at " << x << "," << y;
                    }
                }
            }
        }
    }
}

TEST(ChannelScopeTest, AnOperationThatChangesNothingRecomposesTheSource)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ColorEncoding linear{};
    const Owned<double> wide = colourImage<double>(19u, 7u, an::ChannelSchema::Rgb, true, 9u);
    const Owned<std::uint16_t> codes = colourImage<std::uint16_t>(19u, 7u, an::ChannelSchema::Rgba, false, 10u);
    for (const an::ChannelScope scope : everyScope())
    {
        if (scope == an::ChannelScope::Whole)
        {
            continue;
        }
        const an::ChannelScopePlan made = plan(scope, linear);
        MapOperation identity([](double value) { return value; });
        std::vector<double> out(wide.samples.size(), -1.0);
        ASSERT_TRUE(an::applyChannelScope(execution, made, wide.source(), identity, out.data(), wide.layout).hasValue())
            << an::channelScopeName(scope);
        for (std::size_t index = 0u; index < out.size(); ++index)
        {
            EXPECT_NEAR(out[index], wide.samples[index], 1.0e-12) << an::channelScopeName(scope) << " " << index;
        }

        // Sixteen-bit codes come back to the code they were; alpha is copied.
        MapOperation keep([](double value) { return value; });
        std::vector<std::uint16_t> back(codes.samples.size(), 0u);
        const an::Result<an::ChannelScopeResult> result =
            an::applyChannelScope(execution, made, codes.source(), keep, back.data(), codes.layout);
        ASSERT_TRUE(result.hasValue()) << an::channelScopeName(scope) << ": " << result.status().message();
        EXPECT_EQ(keep.sampleType, encoded(scope) ? an::SampleType::Float32 : an::SampleType::UInt16)
            << "a component of another encoding is a Float32 working plane";
        for (std::uint32_t y = 0u; y < 7u; ++y)
        {
            for (std::uint32_t x = 0u; x < 19u; ++x)
            {
                for (std::uint32_t c = 0u; c < 4u; ++c)
                {
                    const std::size_t index = codes.index(c, x, y);
                    const int difference = static_cast<int>(back[index]) - static_cast<int>(codes.samples[index]);
                    EXPECT_LE(std::abs(difference), c == 3u ? 0 : 1) << an::channelScopeName(scope) << " " << c;
                }
            }
        }
    }
}

TEST(ChannelScopeTest, AStoredChannelIsProcessedAloneAndTheOthersAreCopied)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<std::uint16_t> packed = colourImage<std::uint16_t>(21u, 9u, an::ChannelSchema::Rgba, true, 13u);
    const Owned<float> planar = colourImage<float>(21u, 9u, an::ChannelSchema::Rgb, false, 14u);
    for (const an::ChannelScope scope : {an::ChannelScope::Red, an::ChannelScope::Green, an::ChannelScope::Blue})
    {
        const std::uint32_t k = an::channelScopeComponent(scope);
        const an::ChannelScopePlan made = plan(scope);
        MapOperation operation(gamma);
        std::vector<std::uint16_t> out(packed.samples.size(), 0u);
        ASSERT_TRUE(
            an::applyChannelScope(execution, made, packed.source(), operation, out.data(), packed.layout).hasValue());
        EXPECT_EQ(operation.sampleType, an::SampleType::UInt16) << "a stored channel keeps its representation";
        EXPECT_EQ(operation.domain, an::NumericDomain::NormalizedLinear);
        EXPECT_TRUE(operation.startedAsCopy);
        for (std::uint32_t y = 0u; y < 9u; ++y)
        {
            for (std::uint32_t x = 0u; x < 21u; ++x)
            {
                for (std::uint32_t c = 0u; c < 4u; ++c)
                {
                    const std::size_t index = packed.index(c, x, y);
                    if (c != k)
                    {
                        EXPECT_EQ(out[index], packed.samples[index]);
                        continue;
                    }
                    const double expected = std::lround(gamma(packed.samples[index] / 65535.0) * 65535.0);
                    EXPECT_EQ(out[index], static_cast<std::uint16_t>(expected));
                }
            }
        }

        // A planar source's channel is handed where it lies.
        MapOperation viewing(gamma);
        std::vector<float> planarOut(planar.samples.size(), -1.0f);
        ASSERT_TRUE(an::applyChannelScope(execution, made, planar.source(), viewing, planarOut.data(), planar.layout)
                        .hasValue());
        EXPECT_EQ(viewing.inputSamples, static_cast<const void*>(planar.samples.data() + planar.index(k, 0u, 0u)));
        // An interleaved one's is copied into a plane of its own.
        const auto* begin = reinterpret_cast<const std::uint8_t*>(packed.samples.data());
        const auto* handed = static_cast<const std::uint8_t*>(operation.inputSamples);
        EXPECT_TRUE(handed < begin || handed >= begin + packed.samples.size() * sizeof(std::uint16_t));
    }
}

TEST(ChannelScopeTest, TheWholeImageIsTheOperationsOutput)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(8u, 6u, an::ChannelSchema::Rgba, true, 17u);
    const an::ChannelScopePlan made = plan(an::ChannelScope::Whole);
    const an::ImageLayout planarDestination = planarLayout(an::SampleType::Float32, 8u, 6u, 4u);
    std::vector<float> out(planarDestination.totalBytes() / sizeof(float), -1.0f);
    MapOperation operation(gamma);
    const an::Result<an::ChannelScopeResult> result =
        an::applyChannelScope(execution, made, source.source(), operation, out.data(), planarDestination);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(operation.inputSamples, static_cast<const void*>(source.samples.data()));
    EXPECT_EQ(operation.outputSamples, static_cast<void*>(out.data()));
    EXPECT_EQ(operation.channels, 4u);
    EXPECT_TRUE(operation.startedAsCopy) << "the destination starts as a copy of the source";
    for (std::uint32_t c = 0u; c < 4u; ++c)
    {
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 8u; ++x)
            {
                const float written =
                    out[static_cast<std::size_t>(*planarDestination.sampleByteOffset(c, y, x, 0u) / sizeof(float))];
                EXPECT_EQ(written, static_cast<float>(gamma(source.at(c, x, y))));
            }
        }
    }

    // A grey image's only scope is the whole image, and it may be any representation.
    const Owned<std::int16_t> grey = colourImage<std::int16_t>(5u, 4u, an::ChannelSchema::Mono, false, 18u);
    std::vector<std::int16_t> greyOut(grey.samples.size(), 0);
    MapOperation keep([](double value) { return value; });
    EXPECT_TRUE(an::applyChannelScope(execution, made, grey.source(), keep, greyOut.data(), grey.layout).hasValue());
    EXPECT_EQ(
        an::applyChannelScope(execution, plan(an::ChannelScope::Red), grey.source(), keep, greyOut.data(), grey.layout)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
}

TEST(ChannelScopeTest, ExtractionWritesThePlaneTheOperationWouldBeHanded)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<std::uint16_t> source = colourImage<std::uint16_t>(17u, 6u, an::ChannelSchema::Rgb, true, 21u);
    for (const an::ChannelScope scope : everyScope())
    {
        if (scope == an::ChannelScope::Whole)
        {
            continue;
        }
        const an::ChannelScopePlan made = plan(scope);
        const an::Result<an::ImageDescriptor> described = an::channelScopePlaneDescriptor(made, source.descriptor);
        ASSERT_TRUE(described.hasValue()) << described.status().message();
        EXPECT_EQ(described->channelSchema(), an::ChannelSchema::Mono);
        EXPECT_EQ(described->colorSpace(), an::ColorSpace::Unspecified);
        EXPECT_EQ(described->extent().channelCount(), 1u);
        EXPECT_EQ(described->sampleType(), encoded(scope) ? an::SampleType::Float32 : an::SampleType::UInt16);
        if (encoded(scope))
        {
            const an::ColorEncoding target = made.forward()->destination();
            EXPECT_EQ(described->numericDomain(), *an::numericDomainOfEncoding(target));
        }
        const an::Result<an::ImageLayoutRequest> request = an::channelScopePlaneLayout(made, source.descriptor);
        ASSERT_TRUE(request.hasValue());
        const an::ImageLayout layout = layoutOf(*request);
        std::vector<std::uint8_t> plane(layout.totalBytes(), 0xCDu);
        const an::Result<an::ChannelScopeResult> extracted =
            an::extractChannelScope(execution, made, source.source(), plane.data(), layout);
        ASSERT_TRUE(extracted.hasValue()) << an::channelScopeName(scope) << ": " << extracted.status().message();

        MapOperation operation([](double value) { return value; });
        std::vector<std::uint16_t> out(source.samples.size(), 0u);
        ASSERT_TRUE(
            an::applyChannelScope(execution, made, source.source(), operation, out.data(), source.layout).hasValue());
        std::size_t pixel = 0u;
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 17u; ++x, ++pixel)
            {
                const std::uint8_t* at = plane.data() + *layout.sampleByteOffset(0u, y, x, 0u);
                double value = 0.0;
                if (encoded(scope))
                {
                    float sample = 0.0f;
                    std::memcpy(&sample, at, sizeof(float));
                    value = sample;
                }
                else
                {
                    std::uint16_t sample = 0u;
                    std::memcpy(&sample, at, sizeof(std::uint16_t));
                    value = sample;
                    EXPECT_EQ(sample, source.at(an::channelScopeComponent(scope), x, y));
                }
                EXPECT_EQ(value, operation.handed[pixel]) << an::channelScopeName(scope);
            }
        }
    }
    EXPECT_EQ(an::extractChannelScope(execution, plan(an::ChannelScope::Whole), source.source(), nullptr, source.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ChannelScopeTest, TheUnitIntervalHoldsAnInGamutColourAndNaturalUnitsAreTheCieOnes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ColorEncoding linear{};
    // White, a mid grey and a saturated red, as linear Float64.
    const an::ImageDescriptor descriptor = describe(an::SampleType::Float64, 3u, 1u, an::ChannelSchema::Rgb);
    const an::ImageLayout sourceLayout = interleavedLayout(an::SampleType::Float64, 3u, 1u, 3u);
    const std::vector<double> samples{1.0, 1.0, 1.0, 0.18, 0.18, 0.18, 0.9, 0.05, 0.05};
    const an::ChannelScopeSource source{.descriptor = &descriptor, .samples = samples.data(), .layout = &sourceLayout};
    const auto extract = [&](an::ChannelScope scope, an::ChannelScopeUnits units) {
        const an::ChannelScopePlan made = plan(scope, linear, units);
        const an::ImageLayout layout = layoutOf(*an::channelScopePlaneLayout(made, descriptor));
        std::vector<double> plane(layout.totalBytes() / sizeof(double), -1.0);
        EXPECT_TRUE(an::extractChannelScope(execution, made, source, plane.data(), layout).hasValue());
        return std::array<double, 3u>{plane[0], plane[1], plane[2]};
    };
    const std::array<double, 3u> unitL = extract(an::ChannelScope::CieLightness, an::ChannelScopeUnits::UnitInterval);
    const std::array<double, 3u> naturalL = extract(an::ChannelScope::CieLightness, an::ChannelScopeUnits::Natural);
    EXPECT_NEAR(unitL[0], 1.0, 1.0e-12);
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        EXPECT_NEAR(naturalL[index], 100.0 * unitL[index], 1.0e-9);
    }
    for (const an::ChannelScope scope : {an::ChannelScope::CieX, an::ChannelScope::CieY, an::ChannelScope::CieZ})
    {
        const std::array<double, 3u> unit = extract(scope, an::ChannelScopeUnits::UnitInterval);
        const std::array<double, 3u> natural = extract(scope, an::ChannelScopeUnits::Natural);
        EXPECT_NEAR(unit[0], 1.0, 1.0e-12) << "the white is one in every tristimulus component";
        EXPECT_NEAR(natural[0],
                    an::whitePointTristimulus(an::ChromaticWhitePoint::D65)[an::channelScopeComponent(scope)], 1.0e-12);
    }
    for (const an::ChannelScope scope :
         {an::ChannelScope::CieA, an::ChannelScope::CieB, an::ChannelScope::CieChroma, an::ChannelScope::CieHue,
          an::ChannelScope::HslHue, an::ChannelScope::HsvSaturation, an::ChannelScope::HsiIntensity,
          an::ChannelScope::YCbCrBlueChroma, an::ChannelScope::YCbCrRedChroma})
    {
        for (const double value : extract(scope, an::ChannelScopeUnits::UnitInterval))
        {
            EXPECT_GE(value, 0.0) << an::channelScopeName(scope);
            EXPECT_LE(value, 1.0) << an::channelScopeName(scope);
        }
    }
    // The red's hue: a fraction of a turn in the unit interval, degrees in natural units.
    const std::array<double, 3u> turns = extract(an::ChannelScope::HsvHue, an::ChannelScopeUnits::UnitInterval);
    const std::array<double, 3u> degrees = extract(an::ChannelScope::HsvHue, an::ChannelScopeUnits::Natural);
    EXPECT_NEAR(degrees[2], 360.0 * turns[2], 1.0e-9);
}

TEST(ChannelScopeTest, APixelWithNoColourIsHandedAsNaNAndRecomposedAsTheSourceHeldIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> source = colourImage<float>(9u, 5u, an::ChannelSchema::Rgba, false, 25u);
    source.samples[source.index(1u, 4u, 2u)] = std::numeric_limits<float>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::ChannelScopeSource ruled = source.source(an::KernelValidityView{.descriptor = *finite});
    const an::ChannelScopePlan made = plan(an::ChannelScope::HslLightness);
    MapOperation operation(gamma);
    std::vector<float> out(source.samples.size(), -1.0f);
    const an::Result<an::ChannelScopeResult> result =
        an::applyChannelScope(execution, made, ruled, operation, out.data(), source.layout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->invalidPixels, 1u);
    EXPECT_EQ(operation.rule, an::ValidityRule::FiniteOnly);
    EXPECT_TRUE(std::isnan(operation.handed[2u * 9u + 4u]));
    std::size_t nanCount = 0u;
    for (const double value : operation.handed)
    {
        nanCount += std::isnan(value) ? 1u : 0u;
    }
    EXPECT_EQ(nanCount, 1u);
    for (std::uint32_t c = 0u; c < 4u; ++c)
    {
        const std::size_t index = source.index(c, 4u, 2u);
        EXPECT_TRUE(sameBits(out[index], source.samples[index])) << c;
        const std::size_t neighbour = source.index(c, 3u, 2u);
        if (c < 3u)
        {
            EXPECT_NE(out[neighbour], source.samples[neighbour]) << "a neighbour is recomposed";
        }
        else
        {
            EXPECT_EQ(out[neighbour], source.samples[neighbour]) << "alpha is copied";
        }
    }

    // The extraction marks the same pixel.
    const an::ImageLayout layout = layoutOf(*an::channelScopePlaneLayout(made, source.descriptor));
    std::vector<float> plane(layout.totalBytes() / sizeof(float), 0.0f);
    const an::Result<an::ChannelScopeResult> extracted =
        an::extractChannelScope(execution, made, ruled, plane.data(), layout);
    ASSERT_TRUE(extracted.hasValue());
    EXPECT_EQ(extracted->invalidPixels, 1u);
    EXPECT_TRUE(std::isnan(plane[*layout.sampleByteOffset(0u, 2u, 4u, 0u) / sizeof(float)]));
}

TEST(ChannelScopeTest, AStoredChannelIsHandedTheSourcesOwnRuleAndItsMaskPlane)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(10u, 4u, an::ChannelSchema::Rgb, true, 29u);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(10u, 4u, 3u, an::defaultEngineLimits());
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(2u) * 4u * 3u, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *extent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const std::optional<an::ValidityDescriptor> masked =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(masked.has_value());
    for (const an::ChannelScope scope : {an::ChannelScope::Red, an::ChannelScope::Green, an::ChannelScope::Blue})
    {
        MapOperation operation(gamma);
        std::vector<float> out(source.samples.size(), 0.0f);
        ASSERT_TRUE(an::applyChannelScope(execution, plan(scope),
                                          source.source(an::KernelValidityView{.descriptor = *masked, .mask = &mask}),
                                          operation, out.data(), source.layout)
                        .hasValue());
        EXPECT_EQ(operation.rule, an::ValidityRule::ExplicitMask);
        EXPECT_EQ(operation.mask, &mask);
        EXPECT_EQ(operation.maskPlane, an::channelScopeComponent(scope)) << "a per-channel mask gives each its plane";
    }
}

TEST(ChannelScopeTest, WhatAScopeCannotReadIsRefusedBeforeItWrites)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ChannelScopePlan lightness = plan(an::ChannelScope::CieLightness);
    Owned<float> source = colourImage<float>(6u, 4u, an::ChannelSchema::Rgb, false, 33u);
    std::vector<float> out(source.samples.size(), -1.0f);
    MapOperation operation(gamma);
    const auto codeOf = [&](const an::ChannelScopePlan& made, const an::ChannelScopeSource& from, void* to,
                            const an::ImageLayout& layout) {
        return an::applyChannelScope(execution, made, from, operation, to, layout).status().code();
    };

    // A floating physical quantity, a sensor code, and a basis the encoding does not name.
    Owned<float> physical = source;
    physical.descriptor =
        describe(an::SampleType::Float32, 6u, 4u, an::ChannelSchema::Rgb, an::NumericDomain::PhysicalScaled);
    EXPECT_EQ(codeOf(lightness, physical.source(), out.data(), source.layout), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(codeOf(plan(an::ChannelScope::Red), physical.source(), out.data(), source.layout), an::ErrorCode::Ok)
        << "a stored channel converts nothing, so any domain is read";
    const Owned<std::uint16_t> sensor = [&] {
        Owned<std::uint16_t> made = colourImage<std::uint16_t>(6u, 4u, an::ChannelSchema::Rgb, false, 34u);
        made.descriptor =
            describe(an::SampleType::UInt16, 6u, 4u, an::ChannelSchema::Rgb, an::NumericDomain::RawSensorCode);
        return made;
    }();
    std::vector<std::uint16_t> sensorOut(sensor.samples.size(), 0u);
    EXPECT_EQ(codeOf(lightness, sensor.source(), sensorOut.data(), sensor.layout),
              an::ErrorCode::IncompatibleArguments);
    Owned<float> linearBasis = source;
    linearBasis.descriptor = describe(an::SampleType::Float32, 6u, 4u, an::ChannelSchema::Rgb,
                                      an::NumericDomain::NormalizedLinear, an::ColorSpace::LinearRgb);
    EXPECT_EQ(codeOf(lightness, linearBasis.source(), out.data(), source.layout), an::ErrorCode::IncompatibleArguments)
        << "an image that says it is linear is not read as sRGB-encoded";
    EXPECT_EQ(codeOf(plan(an::ChannelScope::CieLightness, an::ColorEncoding{}), linearBasis.source(), out.data(),
                     source.layout),
              an::ErrorCode::Ok);

    // A destination of another extent or representation, or overlapping the source.
    const an::ImageLayout narrow = planarLayout(an::SampleType::Float32, 5u, 4u, 3u);
    EXPECT_EQ(codeOf(lightness, source.source(), out.data(), narrow), an::ErrorCode::IncompatibleArguments);
    const an::ImageLayout wide = planarLayout(an::SampleType::Float64, 6u, 4u, 3u);
    std::vector<double> wideOut(wide.totalBytes() / sizeof(double));
    EXPECT_EQ(codeOf(lightness, source.source(), wideOut.data(), wide), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(codeOf(lightness, source.source(), source.samples.data(), source.layout),
              an::ErrorCode::IncompatibleArguments);
    an::ChannelScopeSource broken = source.source();
    broken.layout = nullptr;
    EXPECT_EQ(codeOf(lightness, broken, out.data(), source.layout), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(operation.calls, 2) << "only the two accepted calls reached the operation";

    // An execution that holds nothing, and a stopped one.
    EXPECT_EQ(
        an::applyChannelScope(an::ExecutionContext{}, lightness, source.source(), operation, out.data(), source.layout)
            .status()
            .code(),
        an::ErrorCode::NotInitialized);
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> stopped = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(stopped.hasValue());
    EXPECT_EQ(an::applyChannelScope(*stopped, lightness, source.source(), operation, out.data(), source.layout)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(operation.calls, 2);
}

/// An operation that fails, by returning or by throwing.
class FailingOperation final : public an::ChannelScopeOperation
{
public:
    explicit FailingOperation(bool throwIt) : m_throw(throwIt) {}

    an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput&, const an::ChannelScopeOutput&) override
    {
        if (m_throw)
        {
            throw std::runtime_error("the caller's operation gave up");
        }
        return an::Status::failure(an::ErrorCode::InvalidData);
    }

private:
    bool m_throw = false;
};

TEST(ChannelScopeTest, AnOperationsFailureIsReturnedAndTheScopesMemoryIsGivenBack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(32u, 16u, an::ChannelSchema::Rgb, true, 37u);
    std::vector<float> out(source.samples.size(), -1.0f);
    const std::uint64_t before = engine.memoryResource()->counters().liveBytes;
    for (const an::ChannelScope scope : {an::ChannelScope::Whole, an::ChannelScope::Green, an::ChannelScope::CieHue})
    {
        FailingOperation returning(false);
        EXPECT_EQ(an::applyChannelScope(execution, plan(scope), source.source(), returning, out.data(), source.layout)
                      .status()
                      .code(),
                  an::ErrorCode::InvalidData);
        FailingOperation throwing(true);
        const an::Result<an::ChannelScopeResult> thrown =
            an::applyChannelScope(execution, plan(scope), source.source(), throwing, out.data(), source.layout);
        ASSERT_FALSE(thrown.hasValue());
        EXPECT_EQ(thrown.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before) << an::channelScopeName(scope);
    }

    an::EngineContextOptions small;
    small.maxConcurrency = 2u;
    small.limits.memoryBudgetBytes = 4096u;
    an::Result<an::EngineContext> tight = an::EngineContext::create(small);
    ASSERT_TRUE(tight.hasValue()) << tight.status().message();
    const an::ExecutionContext starved = makeExecution(*tight, 2u);
    MapOperation operation(gamma);
    EXPECT_EQ(an::applyChannelScope(starved, plan(an::ChannelScope::CieLightness), source.source(), operation,
                                    out.data(), source.layout)
                  .status()
                  .code(),
              an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(operation.calls, 0);
}

TEST(ChannelScopeTest, TheAnswerDoesNotDependOnTheThreads)
{
    const Owned<std::uint16_t> source = colourImage<std::uint16_t>(257u, 63u, an::ChannelSchema::Rgb, true, 41u);
    for (const an::ChannelScope scope :
         {an::ChannelScope::CieLightness, an::ChannelScope::HsvValue, an::ChannelScope::Blue})
    {
        std::optional<std::vector<std::uint16_t>> reference;
        for (const std::uint32_t threads : {1u, 4u, 8u})
        {
            const an::EngineContext engine = makeEngine(8u);
            const an::ExecutionContext execution = makeExecution(engine, threads);
            MapOperation operation(gamma);
            std::vector<std::uint16_t> out(source.samples.size(), 0u);
            ASSERT_TRUE(
                an::applyChannelScope(execution, plan(scope), source.source(), operation, out.data(), source.layout)
                    .hasValue());
            if (!reference.has_value())
            {
                reference = out;
                continue;
            }
            EXPECT_EQ(out, *reference) << an::channelScopeName(scope) << " at " << threads;
        }
    }
}

/// What a destination is filled with before a call: no source sample is negative, so a copy can never put it there.
constexpr float kPoison = -7.0f;

/// What the stripe operation writes: no source sample holds it either.
constexpr float kStripe = 0.25f;

/// A Float32 operation that writes `kStripe` into every sample of its output, or only where `x + y` is even, says
/// what it declared about that, and counts the samples of its output that held the input's value when it was handed.
class StripeOperation final : public an::ChannelScopeOperation
{
public:
    StripeOperation(bool declaresEverySample, bool writesEverySample)
        : m_declares(declaresEverySample), m_writesEverySample(writesEverySample)
    {
    }

    an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                   const an::ChannelScopeOutput& output) override
    {
        ++calls;
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        for (std::uint32_t c = 0u; c < in.extent().channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < in.extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < in.extent().width(); ++x)
                {
                    const float value = *reinterpret_cast<const float*>(
                        static_cast<const std::uint8_t*>(input.samples) +
                        *in.sampleByteOffset(c / in.samplesPerPixel(), y, x, c % in.samplesPerPixel()));
                    float* const written = reinterpret_cast<float*>(
                        static_cast<std::uint8_t*>(output.samples) +
                        *out.sampleByteOffset(c / out.samplesPerPixel(), y, x, c % out.samplesPerPixel()));
                    copiedSamples += *written == value ? 1u : 0u;
                    if (m_writesEverySample || (x + y) % 2u == 0u)
                    {
                        *written = kStripe;
                    }
                }
            }
        }
        return an::Status::success();
    }

    [[nodiscard]] bool writesEveryOutputSample() const noexcept override
    {
        return m_declares;
    }

    int calls = 0;
    std::uint64_t copiedSamples = 0u;

private:
    bool m_declares = false;
    bool m_writesEverySample = false;
};

/// Whether the stripe operation writes the sample at a position.
[[nodiscard]] bool striped(std::uint32_t x, std::uint32_t y)
{
    return (x + y) % 2u == 0u;
}

// The whole image's output is the caller's destination. An operation that declares it writes every sample of it is
// handed the destination as it was - none of the source copied in - so where it writes nothing the poison stays; one
// that declares nothing finds the source there, copied before it runs.
TEST(ChannelScopeTest, AnOperationThatWritesEverySampleFindsTheDestinationAsItWas)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(33u, 17u, an::ChannelSchema::Rgb, false, 53u);
    const std::uint32_t total = 33u * 17u * 3u;

    std::vector<float> declared(source.samples.size(), kPoison);
    StripeOperation lying(true, false);
    ASSERT_TRUE(an::applyChannelScope(execution, plan(an::ChannelScope::Whole), source.source(), lying, declared.data(),
                                      source.layout)
                    .hasValue());
    EXPECT_EQ(lying.calls, 1);
    EXPECT_EQ(lying.copiedSamples, 0u) << "no sample of the source was copied into the output it was handed";
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 17u; ++y)
        {
            for (std::uint32_t x = 0u; x < 33u; ++x)
            {
                EXPECT_EQ(declared[source.index(c, x, y)], striped(x, y) ? kStripe : kPoison)
                    << c << " " << x << " " << y;
            }
        }
    }

    std::vector<float> covered(source.samples.size(), kPoison);
    StripeOperation everySample(true, true);
    ASSERT_TRUE(an::applyChannelScope(execution, plan(an::ChannelScope::Whole), source.source(), everySample,
                                      covered.data(), source.layout)
                    .hasValue());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 17u; ++y)
        {
            for (std::uint32_t x = 0u; x < 33u; ++x)
            {
                EXPECT_EQ(covered[source.index(c, x, y)], kStripe);
            }
        }
    }

    std::vector<float> copied(source.samples.size(), kPoison);
    StripeOperation partial(false, false);
    ASSERT_TRUE(an::applyChannelScope(execution, plan(an::ChannelScope::Whole), source.source(), partial, copied.data(),
                                      source.layout)
                    .hasValue());
    EXPECT_EQ(partial.copiedSamples, total) << "every sample of the output started as the source's";
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 17u; ++y)
        {
            for (std::uint32_t x = 0u; x < 33u; ++x)
            {
                EXPECT_EQ(copied[source.index(c, x, y)], striped(x, y) ? kStripe : source.at(c, x, y));
            }
        }
    }
}

// A destination stated as holding the source has none of it copied in again: the whole image's operation runs over it
// as it is, a stored channel writes that channel back and leaves the others, and a component of another encoding
// still recomposes every colour sample. The statement is the caller's, so a destination that does not hold the source
// keeps its poison wherever nothing writes.
TEST(ChannelScopeTest, ADestinationThatHoldsTheSourceHasNothingOfItCopiedAgain)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(29u, 13u, an::ChannelSchema::Rgb, false, 59u);

    std::vector<float> whole(source.samples.size(), kPoison);
    StripeOperation partial(false, false);
    ASSERT_TRUE(an::applyChannelScope(execution, plan(an::ChannelScope::Whole), source.source(), partial, whole.data(),
                                      source.layout, an::ChannelScopeDestination::HoldsSource)
                    .hasValue());
    EXPECT_EQ(partial.copiedSamples, 0u);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 13u; ++y)
        {
            for (std::uint32_t x = 0u; x < 29u; ++x)
            {
                EXPECT_EQ(whole[source.index(c, x, y)], striped(x, y) ? kStripe : kPoison);
            }
        }
    }

    for (const an::ChannelScopeDestination holds :
         {an::ChannelScopeDestination::HoldsSource, an::ChannelScopeDestination::Unwritten})
    {
        std::vector<float> green(source.samples.size(), kPoison);
        StripeOperation everySample(true, true);
        ASSERT_TRUE(an::applyChannelScope(execution, plan(an::ChannelScope::Green), source.source(), everySample,
                                          green.data(), source.layout, holds)
                        .hasValue());
        const bool held = holds == an::ChannelScopeDestination::HoldsSource;
        for (std::uint32_t y = 0u; y < 13u; ++y)
        {
            for (std::uint32_t x = 0u; x < 29u; ++x)
            {
                EXPECT_EQ(green[source.index(1u, x, y)], kStripe);
                EXPECT_EQ(green[source.index(0u, x, y)], held ? kPoison : source.at(0u, x, y));
                EXPECT_EQ(green[source.index(2u, x, y)], held ? kPoison : source.at(2u, x, y));
            }
        }
    }

    std::vector<float> lightness(source.samples.size(), kPoison);
    MapOperation identity([](double value) { return value; });
    ASSERT_TRUE(an::applyChannelScope(execution, plan(an::ChannelScope::CieLightness), source.source(), identity,
                                      lightness.data(), source.layout, an::ChannelScopeDestination::HoldsSource)
                    .hasValue());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 13u; ++y)
        {
            for (std::uint32_t x = 0u; x < 29u; ++x)
            {
                EXPECT_NEAR(lightness[source.index(c, x, y)], source.at(c, x, y), 1e-5f);
            }
        }
    }

    StripeOperation unused(false, false);
    std::vector<float> refused(source.samples.size(), kPoison);
    EXPECT_EQ(an::applyChannelScope(execution, plan(an::ChannelScope::Whole), source.source(), unused, refused.data(),
                                    source.layout, static_cast<an::ChannelScopeDestination>(2u))
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(unused.calls, 0);
    EXPECT_EQ(refused, std::vector<float>(source.samples.size(), kPoison));
}

TEST(ChannelScopeTest, TheScopesEffectIsTheOperationsWithTheDescriptionPreserved)
{
    const an::OperationEffect gammaLike{.samples = an::SampleEffect::Writes, .semantics = an::SemanticEffect::Restates};
    const an::Result<an::OperationEffect> scoped = an::channelScopeEffect(gammaLike);
    ASSERT_TRUE(scoped.hasValue()) << scoped.status().message();
    EXPECT_EQ(*scoped, gammaLike);

    const an::OperationEffect resampling{.samples = an::SampleEffect::Writes,
                                         .description = an::DescriptionEffect::Restates,
                                         .solution = an::SolutionEffect::Invalidates};
    EXPECT_EQ(an::channelScopeEffect(resampling).status().code(), an::ErrorCode::IncompatibleArguments);
    const an::OperationEffect reading{.samples = an::SampleEffect::Reads};
    EXPECT_EQ(an::channelScopeEffect(reading).status().code(), an::ErrorCode::IncompatibleArguments);
    an::OperationEffect inconsistent{};
    inconsistent.validity.policy = static_cast<an::ValidityPolicy>(9u);
    EXPECT_EQ(an::channelScopeEffect(inconsistent).status().code(), an::ErrorCode::InvalidArgument);
}

// ---- A prepared scope: the forward conversion made once, and every later call over the same samples.

/// Every scope of another encoding, in both units.
[[nodiscard]] std::vector<an::ChannelScopePlan> everyEncodedPlan(const an::ColorEncoding& encoding = kSrgbEncoded)
{
    std::vector<an::ChannelScopePlan> plans;
    for (const an::ChannelScope scope : everyScope())
    {
        if (!encoded(scope))
        {
            continue;
        }
        for (const an::ChannelScopeUnits units : {an::ChannelScopeUnits::UnitInterval, an::ChannelScopeUnits::Natural})
        {
            plans.push_back(plan(scope, encoding, units));
        }
    }
    return plans;
}

/// The same source, carrying a preparation.
[[nodiscard]] an::ChannelScopeSource withPreparation(an::ChannelScopeSource source,
                                                     const an::ChannelScopePreparation& prepared)
{
    source.prepared = &prepared;
    return source;
}

/// Whether two operations were handed the same samples, a quiet NaN matching a quiet NaN.
[[nodiscard]] bool sameHanded(const std::vector<double>& left, const std::vector<double>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        if (!sameBits(left[index], right[index]))
        {
            return false;
        }
    }
    return true;
}

/// The name of a plan, for a failure message.
[[nodiscard]] std::string nameOf(const an::ChannelScopePlan& made)
{
    return std::string(an::channelScopeName(made.request().scope)) +
           (made.request().units == an::ChannelScopeUnits::Natural ? " natural" : " unit");
}

/// Runs a plan over a source twice - as it is, and carrying a preparation made of it - each through a fresh operation
/// the factory makes, into destinations that start alike, and expects the same: the image recomposed bit for bit, the
/// plane handed to the operation, the count of pixels with no colour, and whether the output started as the input.
template <typename SampleT, typename MakeOperation>
void expectPreparedAsUnprepared(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                const an::ChannelScopePlan& made, const Owned<SampleT>& source,
                                const an::KernelValidityView& validity, const MakeOperation& makeOperation,
                                const std::string& what)
{
    auto unpreparedOperation = makeOperation();
    std::vector<SampleT> unprepared(source.samples.size(), SampleT{});
    const an::Result<an::ChannelScopeResult> ran = an::applyChannelScope(
        execution, made, source.source(validity), unpreparedOperation, unprepared.data(), source.layout);
    ASSERT_TRUE(ran.hasValue()) << what << ": " << ran.status().message();

    const an::Result<an::ChannelScopePreparation> prepared =
        an::prepareChannelScope(engine, execution, made, source.source(validity));
    ASSERT_TRUE(prepared.hasValue()) << what << ": " << prepared.status().message();
    ASSERT_TRUE(prepared->valid()) << what;
    EXPECT_EQ(prepared->invalidPixels(), ran->invalidPixels) << what;
    EXPECT_EQ(prepared->width(), source.descriptor.extent().width()) << what;
    EXPECT_EQ(prepared->height(), source.descriptor.extent().height()) << what;

    auto preparedOperation = makeOperation();
    std::vector<SampleT> fromPrepared(source.samples.size(), SampleT{});
    const an::Result<an::ChannelScopeResult> again =
        an::applyChannelScope(execution, made, withPreparation(source.source(validity), *prepared), preparedOperation,
                              fromPrepared.data(), source.layout);
    ASSERT_TRUE(again.hasValue()) << what << ": " << again.status().message();
    EXPECT_EQ(again->scope, ran->scope) << what;
    EXPECT_EQ(again->invalidPixels, ran->invalidPixels) << what;
    EXPECT_EQ(std::memcmp(unprepared.data(), fromPrepared.data(), unprepared.size() * sizeof(SampleT)), 0)
        << what << ": the prepared call recomposed another image";
    EXPECT_EQ(preparedOperation.calls, 1) << what;
    if constexpr (std::is_same_v<decltype(preparedOperation), MapOperation>)
    {
        EXPECT_TRUE(sameHanded(unpreparedOperation.handed, preparedOperation.handed))
            << what << ": the prepared call handed the operation another plane";
        EXPECT_EQ(preparedOperation.startedAsCopy, unpreparedOperation.startedAsCopy) << what;
        EXPECT_EQ(preparedOperation.rule, unpreparedOperation.rule) << what;
        EXPECT_EQ(preparedOperation.sampleType, unpreparedOperation.sampleType) << what;
        EXPECT_EQ(preparedOperation.domain, unpreparedOperation.domain) << what;
    }
}

/// `expectPreparedAsUnprepared` for every scope of another encoding over images of one representation: colour and
/// colour with alpha, planar and interleaved, of a width whose rows are padded and of one whose are not.
template <typename SampleT>
void expectEveryPreparedScopeAsUnprepared(const an::EngineContext& engine, const an::ExecutionContext& execution)
{
    for (const std::uint32_t width : {37u, 64u})
    {
        for (const an::ChannelSchema schema : {an::ChannelSchema::Rgb, an::ChannelSchema::Rgba})
        {
            for (const bool interleave : {false, true})
            {
                const Owned<SampleT> source =
                    colourImage<SampleT>(width, 7u, schema, interleave, 61u + width + (interleave ? 1u : 0u));
                for (const an::ChannelScopePlan& made : everyEncodedPlan())
                {
                    const std::string what = std::string(an::sampleTypeName(an::kSampleTypeOf<SampleT>)) + " " +
                                             std::to_string(width) +
                                             (schema == an::ChannelSchema::Rgba ? " RGBA" : " RGB") +
                                             (interleave ? " interleaved " : " planar ") + nameOf(made);
                    expectPreparedAsUnprepared(
                        engine, execution, made, source, {}, [] { return MapOperation(gamma); }, what);
                }
            }
        }
    }
}

// Every scope of another encoding, in every representation, arrangement and unit: a call handed the preparation of
// its source hands its operation the plane the unprepared call hands it, and recomposes the image it recomposes, bit
// for bit - the conversion back is the same conversion over the same samples.
TEST(ChannelScopeTest, APreparedScopeRecomposesWhatTheUnpreparedCallDoesInEveryRepresentation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    expectEveryPreparedScopeAsUnprepared<std::uint8_t>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<std::int8_t>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<std::uint16_t>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<std::int16_t>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<std::uint32_t>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<std::int32_t>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<float>(engine, execution);
    expectEveryPreparedScopeAsUnprepared<double>(engine, execution);
}

// A pixel with no colour - a sample its value rule rejects, or one its mask clears - is marked in the preparation as
// the unprepared call marks it, handed to the operation as a quiet NaN, counted, and recomposed as the source held it.
TEST(ChannelScopeTest, APreparedScopeHandsAndRecomposesAPixelWithNoColourAsTheUnpreparedCallDoes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    const std::optional<an::ValidityDescriptor> masked =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(finite.has_value() && masked.has_value());

    Owned<float> ruled = colourImage<float>(23u, 9u, an::ChannelSchema::Rgba, false, 71u);
    ruled.samples[ruled.index(0u, 3u, 1u)] = std::numeric_limits<float>::quiet_NaN();
    ruled.samples[ruled.index(2u, 17u, 6u)] = std::numeric_limits<float>::infinity();

    const Owned<std::uint16_t> codes = colourImage<std::uint16_t>(23u, 9u, an::ChannelSchema::Rgb, true, 72u);
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(23u, 9u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(3u) * 9u, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *extent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    for (const auto& [x, y] : {std::pair{0u, 0u}, std::pair{11u, 4u}, std::pair{22u, 8u}})
    {
        ASSERT_TRUE(writable->setValid(0u, x, y, false));
    }
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();

    for (const an::ChannelScopePlan& made : everyEncodedPlan())
    {
        expectPreparedAsUnprepared(
            engine, execution, made, ruled, an::KernelValidityView{.descriptor = *finite},
            [] { return MapOperation(gamma); }, "finite-only " + nameOf(made));
        expectPreparedAsUnprepared(
            engine, execution, made, codes, an::KernelValidityView{.descriptor = *masked, .mask = &mask},
            [] { return MapOperation(gamma); }, "masked " + nameOf(made));
    }
}

// An operation that declares it writes every output sample is handed an output that starts as whatever the scope's
// memory held, prepared or not, and one that declares nothing finds its input there; either recomposes what the
// unprepared call recomposes.
TEST(ChannelScopeTest, APreparedScopeStartsTheOutputAsTheUnpreparedCallDoes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(31u, 7u, an::ChannelSchema::Rgb, false, 73u);
    for (const an::ChannelScopePlan& made :
         {plan(an::ChannelScope::CieLightness), plan(an::ChannelScope::CieY), plan(an::ChannelScope::HsvValue)})
    {
        expectPreparedAsUnprepared(
            engine, execution, made, source, {}, [] { return StripeOperation(true, true); },
            "every sample " + nameOf(made));
        expectPreparedAsUnprepared(
            engine, execution, made, source, {}, [] { return StripeOperation(false, false); },
            "part of the samples " + nameOf(made));
        StripeOperation partial(false, false);
        const an::Result<an::ChannelScopePreparation> prepared =
            an::prepareChannelScope(engine, execution, made, source.source());
        ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();
        std::vector<float> out(source.samples.size(), kPoison);
        ASSERT_TRUE(an::applyChannelScope(execution, made, withPreparation(source.source(), *prepared), partial,
                                          out.data(), source.layout)
                        .hasValue());
        EXPECT_EQ(partial.copiedSamples, 31u * 7u) << nameOf(made) << ": the output started as the input";
    }
}

// The operation reads the preparation's component where it lies, and the conversion back reads the preparation's other
// planes: handed the preparation of one image, a call over another of the same shape converts nothing of it and
// recomposes the first.
TEST(ChannelScopeTest, APreparedCallConvertsNothingOfTheSamplesItIsHandedWith)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> first = colourImage<float>(19u, 8u, an::ChannelSchema::Rgb, false, 75u);
    const Owned<float> second = colourImage<float>(19u, 8u, an::ChannelSchema::Rgb, false, 76u);
    for (const an::ChannelScopePlan& made :
         {plan(an::ChannelScope::CieLightness), plan(an::ChannelScope::HslSaturation), plan(an::ChannelScope::CieZ)})
    {
        const an::Result<an::ChannelScopePreparation> prepared =
            an::prepareChannelScope(engine, execution, made, first.source());
        ASSERT_TRUE(prepared.hasValue()) << nameOf(made) << ": " << prepared.status().message();
        const std::optional<an::ImageLayout> planes = prepared->layout();
        ASSERT_TRUE(planes.has_value());
        EXPECT_EQ(planes->planeCount(), 3u);
        EXPECT_EQ(prepared->sampleType(), an::SampleType::Float32);
        EXPECT_GE(prepared->byteCount(), 3u * 19u * 8u * sizeof(float));

        MapOperation identity([](double value) { return value; });
        std::vector<float> out(second.samples.size(), kPoison);
        ASSERT_TRUE(an::applyChannelScope(execution, made, withPreparation(second.source(), *prepared), identity,
                                          out.data(), second.layout)
                        .hasValue());
        const std::uint32_t component = an::channelScopeComponent(made.request().scope);
        EXPECT_EQ(identity.inputSamples, static_cast<const std::uint8_t*>(prepared->samples()) +
                                             static_cast<std::uint64_t>(component) * planes->planeStrideBytes())
            << nameOf(made) << ": the operation reads the preparation's component where it lies";

        MapOperation keep([](double value) { return value; });
        std::vector<float> firstBack(first.samples.size(), kPoison);
        ASSERT_TRUE(
            an::applyChannelScope(execution, made, first.source(), keep, firstBack.data(), first.layout).hasValue());
        EXPECT_EQ(out, firstBack) << nameOf(made) << ": the first image is recomposed, not the second";
    }
}

// A preparation states what it was made for, and a call refuses one of another request, another size or another
// representation, or one that holds nothing, before it writes; neither the whole image nor a stored channel has a
// conversion to prepare, and the budget may refuse the planes.
TEST(ChannelScopeTest, APreparationOfAnotherScopeOrShapeIsRefusedBeforeAnythingIsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> source = colourImage<float>(12u, 5u, an::ChannelSchema::Rgb, false, 77u);
    const an::ChannelScopePlan lightness = plan(an::ChannelScope::CieLightness);

    for (const an::ChannelScope scope : {an::ChannelScope::Whole, an::ChannelScope::Red, an::ChannelScope::Blue})
    {
        EXPECT_EQ(an::prepareChannelScope(engine, execution, plan(scope), source.source()).status().code(),
                  an::ErrorCode::IncompatibleArguments)
            << an::channelScopeName(scope);
    }
    const an::Result<an::ChannelScopePreparation> prepared =
        an::prepareChannelScope(engine, execution, lightness, source.source());
    ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();

    const auto refusedWith = [&](const an::ChannelScopePlan& made, const an::ChannelScopeSource& from,
                                 const an::ImageLayout& layout) {
        MapOperation operation(gamma);
        std::vector<std::uint8_t> out(layout.totalBytes(), 0xCDu);
        const an::ErrorCode code =
            an::applyChannelScope(execution, made, from, operation, out.data(), layout).status().code();
        EXPECT_EQ(operation.calls, 0);
        EXPECT_EQ(out, std::vector<std::uint8_t>(layout.totalBytes(), 0xCDu)) << "nothing was written";
        return code;
    };
    EXPECT_EQ(refusedWith(plan(an::ChannelScope::CieA), withPreparation(source.source(), *prepared), source.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation of another component";
    EXPECT_EQ(refusedWith(plan(an::ChannelScope::CieLightness, kSrgbEncoded, an::ChannelScopeUnits::Natural),
                          withPreparation(source.source(), *prepared), source.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation in other units";
    EXPECT_EQ(refusedWith(plan(an::ChannelScope::CieLightness, an::ColorEncoding{}),
                          withPreparation(source.source(), *prepared), source.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation of another source encoding";
    const an::ColorEncoding powerOf22{
        .schema = an::ChannelSchema::Rgb, .transfer = an::ColorTransferFunction::PureGamma, .transferGamma = 2.2};
    const an::ColorEncoding powerOf18{
        .schema = an::ChannelSchema::Rgb, .transfer = an::ColorTransferFunction::PureGamma, .transferGamma = 1.8};
    const an::Result<an::ChannelScopePreparation> powered =
        an::prepareChannelScope(engine, execution, plan(an::ChannelScope::CieLightness, powerOf22), source.source());
    ASSERT_TRUE(powered.hasValue()) << powered.status().message();
    EXPECT_EQ(refusedWith(plan(an::ChannelScope::CieLightness, powerOf18), withPreparation(source.source(), *powered),
                          source.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation under another power law";
    EXPECT_EQ(refusedWith(plan(an::ChannelScope::Green), withPreparation(source.source(), *prepared), source.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation carried with a stored channel";
    const Owned<float> larger = colourImage<float>(13u, 5u, an::ChannelSchema::Rgb, false, 78u);
    EXPECT_EQ(refusedWith(lightness, withPreparation(larger.source(), *prepared), larger.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation of another size";
    const Owned<double> wider = colourImage<double>(12u, 5u, an::ChannelSchema::Rgb, false, 79u);
    EXPECT_EQ(refusedWith(lightness, withPreparation(wider.source(), *prepared), wider.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation in another working representation";
    const an::ChannelScopePreparation nothing;
    EXPECT_FALSE(nothing.valid());
    EXPECT_EQ(nothing.samples(), nullptr);
    EXPECT_FALSE(nothing.layout().has_value());
    EXPECT_EQ(refusedWith(lightness, withPreparation(source.source(), nothing), source.layout),
              an::ErrorCode::IncompatibleArguments)
        << "a preparation of nothing";

    // Three Float32 planes of 32 by 16 take more than the budget.
    an::EngineContextOptions small;
    small.maxConcurrency = 2u;
    small.limits.memoryBudgetBytes = 4096u;
    an::Result<an::EngineContext> tight = an::EngineContext::create(small);
    ASSERT_TRUE(tight.hasValue()) << tight.status().message();
    const an::ExecutionContext starved = makeExecution(*tight, 2u);
    const Owned<float> large = colourImage<float>(32u, 16u, an::ChannelSchema::Rgb, false, 80u);
    EXPECT_EQ(an::prepareChannelScope(*tight, starved, lightness, large.source()).status().code(),
              an::ErrorCode::MemoryBudgetExceeded);
    const std::uint64_t before = engine.memoryResource()->counters().liveBytes;
    {
        an::Result<an::ChannelScopePreparation> held =
            an::prepareChannelScope(engine, execution, lightness, source.source());
        ASSERT_TRUE(held.hasValue());
        EXPECT_GE(engine.memoryResource()->counters().liveBytes, before + held->byteCount());
        an::ChannelScopePreparation moved = std::move(*held);
        EXPECT_FALSE(held->valid());
        EXPECT_TRUE(moved.valid());
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before) << "the planes go with the preparation";
}

// Extraction copies the preparation's component: the plane the unprepared extraction writes, marked as it marks it.
TEST(ChannelScopeTest, ExtractionFromAPreparationWritesThePlaneTheUnpreparedExtractionWrites)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> source = colourImage<float>(14u, 6u, an::ChannelSchema::Rgba, true, 81u);
    source.samples[source.index(1u, 5u, 2u)] = std::numeric_limits<float>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    const an::ChannelScopeSource ruled = source.source(an::KernelValidityView{.descriptor = *finite});
    for (const an::ChannelScopePlan& made : everyEncodedPlan())
    {
        const an::ImageLayout layout = layoutOf(*an::channelScopePlaneLayout(made, source.descriptor));
        std::vector<std::uint8_t> unprepared(layout.totalBytes(), 0xCDu);
        const an::Result<an::ChannelScopeResult> extracted =
            an::extractChannelScope(execution, made, ruled, unprepared.data(), layout);
        ASSERT_TRUE(extracted.hasValue()) << nameOf(made) << ": " << extracted.status().message();
        const an::Result<an::ChannelScopePreparation> prepared =
            an::prepareChannelScope(engine, execution, made, ruled);
        ASSERT_TRUE(prepared.hasValue()) << nameOf(made) << ": " << prepared.status().message();
        std::vector<std::uint8_t> fromPrepared(layout.totalBytes(), 0xCDu);
        const an::Result<an::ChannelScopeResult> copied =
            an::extractChannelScope(execution, made, withPreparation(ruled, *prepared), fromPrepared.data(), layout);
        ASSERT_TRUE(copied.hasValue()) << nameOf(made) << ": " << copied.status().message();
        EXPECT_EQ(copied->invalidPixels, extracted->invalidPixels) << nameOf(made);
        EXPECT_EQ(fromPrepared, unprepared) << nameOf(made);
    }
}

// A preparation made on one thread serves a call on eight, and the answer is the unprepared call's on either.
TEST(ChannelScopeTest, APreparedAnswerDoesNotDependOnTheThreads)
{
    const Owned<std::uint16_t> source = colourImage<std::uint16_t>(257u, 63u, an::ChannelSchema::Rgb, true, 83u);
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 8u);
    for (const an::ChannelScope scope :
         {an::ChannelScope::CieLightness, an::ChannelScope::HsvValue, an::ChannelScope::YCbCrLuma})
    {
        MapOperation reference(gamma);
        std::vector<std::uint16_t> expected(source.samples.size(), 0u);
        ASSERT_TRUE(
            an::applyChannelScope(single, plan(scope), source.source(), reference, expected.data(), source.layout)
                .hasValue());
        const an::Result<an::ChannelScopePreparation> prepared =
            an::prepareChannelScope(engine, single, plan(scope), source.source());
        ASSERT_TRUE(prepared.hasValue());
        MapOperation operation(gamma);
        std::vector<std::uint16_t> out(source.samples.size(), 0u);
        ASSERT_TRUE(an::applyChannelScope(wide, plan(scope), withPreparation(source.source(), *prepared), operation,
                                          out.data(), source.layout)
                        .hasValue());
        EXPECT_EQ(out, expected) << an::channelScopeName(scope);
    }
}

} // namespace
