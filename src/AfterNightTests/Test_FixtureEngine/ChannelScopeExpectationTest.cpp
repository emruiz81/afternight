// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's per-channel scope through the engine, held to its reference records.
//
// The behavior corpus records the twenty-two channel choices of the product's process framework running one
// channel-dispatched process, the gamma correction at 2.2, over one colour and one grey scene. This runs the same
// process through the engine's channel scope - the gamma kernel written out as the scope's operation, so the
// comparison is of the scope and not of the process - and holds each answer to its record within the bound the
// scope's conversion explains. The records read every colour image as sRGB-encoded with each component packed into
// the unit interval, so the scope is asked for exactly that. Their CIE and luma-chroma recompositions also clamp every
// colour to the unit interval, which the engine does not do to a floating result: those answers are compared after
// the same clamp, and every difference beyond the bound must be one the clamp explains.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

/// The behavior corpus file the per-channel records live in.
constexpr const char* kGammaRecords = "behavior/gamma-correction.expectations";

/// One channel choice of the product, the engine scope that replaces it, and how close the engine must come.
struct ScopeCase
{
    /// The corpus mode, `channel-<choice>`.
    const char* mode;
    /// The engine scope.
    an::ChannelScope scope;
    /// Whether the record clamps every recomposed colour to the unit interval, which the engine does not do to a
    /// floating result; the engine's answer is then compared after the same clamp.
    bool recordClamps;
    /// The largest absolute difference allowed against the record, after that clamp where it applies.
    double bound;
    /// Why the bound is what it is.
    const char* reason;
};

constexpr double kExact = 0.0;
constexpr const char* kSame = "the scope runs the operation over the very samples the record did";
constexpr const char* kCylindrical =
    "HSL and HSV are computed on the stored samples with no transfer function and no clamp, as in the record";
constexpr double kCieBound = 5.0e-5;
constexpr const char* kCie = "the record inverts XYZ through a printed matrix that is not the inverse of its forward "
                             "one, and the engine inverts the matrix it used";
constexpr double kLumaChromaBound = 1.0e-3;
constexpr const char* kLumaChroma = "the record's luma-chroma coefficients are rounded to three decimals in each "
                                    "direction, and the engine derives both from the BT.601 weights";

const std::vector<ScopeCase>& scopeCases()
{
    static const std::vector<ScopeCase> cases{
        {"channel-rgb", an::ChannelScope::Whole, false, kExact, kSame},
        {"channel-grayscale", an::ChannelScope::Whole, false, kExact, kSame},
        {"channel-red", an::ChannelScope::Red, false, kExact, kSame},
        {"channel-green", an::ChannelScope::Green, false, kExact, kSame},
        {"channel-blue", an::ChannelScope::Blue, false, kExact, kSame},
        {"channel-cie-l", an::ChannelScope::CieLightness, true, kCieBound, kCie},
        {"channel-cie-a", an::ChannelScope::CieA, true, kCieBound, kCie},
        {"channel-cie-b", an::ChannelScope::CieB, true, kCieBound, kCie},
        {"channel-cie-c", an::ChannelScope::CieChroma, true, kCieBound, kCie},
        {"channel-cie-h", an::ChannelScope::CieHue, true, kCieBound, kCie},
        {"channel-cie-x", an::ChannelScope::CieX, true, kCieBound, kCie},
        {"channel-cie-y", an::ChannelScope::CieY, true, kCieBound, kCie},
        {"channel-cie-z", an::ChannelScope::CieZ, true, kCieBound, kCie},
        {"channel-hsl-hue", an::ChannelScope::HslHue, false, kExact, kCylindrical},
        {"channel-hsl-saturation", an::ChannelScope::HslSaturation, false, kExact, kCylindrical},
        {"channel-hsl-lightness", an::ChannelScope::HslLightness, false, kExact, kCylindrical},
        {"channel-hsv-hue", an::ChannelScope::HsvHue, false, kExact, kCylindrical},
        {"channel-hsv-saturation", an::ChannelScope::HsvSaturation, false, kExact, kCylindrical},
        {"channel-hsv-value", an::ChannelScope::HsvValue, false, kExact, kCylindrical},
        {"channel-ycbcr-luma", an::ChannelScope::YCbCrLuma, true, kLumaChromaBound, kLumaChroma},
        {"channel-ycbcr-blue-chroma", an::ChannelScope::YCbCrBlueChroma, true, kLumaChromaBound, kLumaChroma},
        {"channel-ycbcr-red-chroma", an::ChannelScope::YCbCrRedChroma, true, kLumaChromaBound, kLumaChroma},
    };
    return cases;
}

const ScopeCase* scopeCaseOf(const std::string& id)
{
    for (const ScopeCase& item : scopeCases())
    {
        if (id.rfind(std::string("gamma-correction.") + item.mode + "/", 0u) == 0u)
        {
            return &item;
        }
    }
    return nullptr;
}

/// The gamma kernel over a Float32 plane: each sample to the power one over 2.2, computed in double.
class GammaOperation final : public an::ChannelScopeOperation
{
public:
    an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                   const an::ChannelScopeOutput& output) override
    {
        if (input.descriptor->sampleType() != an::SampleType::Float32)
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments);
        }
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        const std::uint32_t channels = input.descriptor->extent().channelCount();
        for (std::uint32_t c = 0u; c < channels; ++c)
        {
            for (std::uint32_t y = 0u; y < in.extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < in.extent().width(); ++x)
                {
                    const float value = *reinterpret_cast<const float*>(
                        static_cast<const std::uint8_t*>(input.samples) +
                        *in.sampleByteOffset(c / in.samplesPerPixel(), y, x, c % in.samplesPerPixel()));
                    *reinterpret_cast<float*>(
                        static_cast<std::uint8_t*>(output.samples) +
                        *out.sampleByteOffset(c / out.samplesPerPixel(), y, x, c % out.samplesPerPixel())) =
                        static_cast<float>(std::pow(static_cast<double>(value), 1.0 / 2.2));
                }
            }
        }
        return an::Status::success();
    }
};

/// What the engine's scope produces for a case's input, as neutral samples.
an::Result<fixtures::FixtureSamples> scopedByEngine(const an::consumer::EngineSession& session, const ScopeCase& item,
                                                    const fixtures::FixtureImage& image,
                                                    const fixtures::FixtureSamples& samples)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<adapter::EngineImage> made = adapter::makeEngineImage(engine, image, samples);
    if (!made.hasValue())
    {
        return made.status();
    }
    const auto* typed = std::get_if<an::Image<float>>(&*made);
    if (typed == nullptr)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   an::StatusDetail{.message = "the per-channel records are over Float32 scenes"});
    }
    const auto descriptor = typed->descriptor();
    const auto view = typed->view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::StatusDetail{.message = "the input cannot be viewed"});
    }
    const an::ImageLayout sourceLayout = view->layout();
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::Result<an::ChannelScopePlan> plan = an::planChannelScope(
        an::ChannelScopeRequest{.scope = item.scope,
                                .sourceEncoding = an::ColorEncoding{.schema = an::ChannelSchema::Rgb,
                                                                    .transfer = an::ColorTransferFunction::Srgb},
                                .units = an::ChannelScopeUnits::UnitInterval,
                                .yCbCr = an::YCbCrVariant::Rec601FullRange});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    std::vector<float> destination(sourceLayout.totalBytes() / sizeof(float), 0.0f);
    GammaOperation operation;
    const an::Result<an::ChannelScopeResult> scoped = an::applyChannelScope(
        execution, *plan,
        an::ChannelScopeSource{.descriptor = &*descriptor, .samples = view->data(), .layout = &sourceLayout}, operation,
        destination.data(), sourceLayout);
    if (!scoped.hasValue())
    {
        return scoped.status();
    }
    const std::uint32_t channels = descriptor->extent().channelCount();
    fixtures::FixtureSamples result(samples.width(), samples.height(), channels, fixtures::FixtureSampleType::Float32);
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                const std::uint64_t offset = *sourceLayout.sampleByteOffset(c / sourceLayout.samplesPerPixel(), y, x,
                                                                            c % sourceLayout.samplesPerPixel());
                result.setValue(x, y, c,
                                static_cast<double>(destination[static_cast<std::size_t>(offset / sizeof(float))]));
            }
        }
    }
    return result;
}

double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double a = left.value(x, y, channel);
                const double b = right.value(x, y, channel);
                if (std::isnan(a) && std::isnan(b))
                {
                    continue;
                }
                largest = (std::max)(largest, std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::infinity()
                                                                             : std::abs(a - b));
            }
        }
    }
    return largest;
}

class ChannelScopeExpectationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::Result<an::consumer::EngineSession> session = adapter::createFixtureSession(4u);
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));
        std::string error;
        const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
        ASSERT_TRUE(directory.has_value()) << error;
        const std::optional<std::vector<fixtures::Expectation>> records =
            fixtures::readExpectationFile(*directory / kGammaRecords, error);
        ASSERT_TRUE(records.has_value()) << error;
        for (const fixtures::Expectation& record : *records)
        {
            if (scopeCaseOf(record.id) != nullptr)
            {
                m_records.push_back(record);
            }
        }
    }

    void TearDown() override
    {
        m_session.reset();
    }

    std::optional<an::consumer::EngineSession> m_session;
    std::vector<fixtures::Expectation> m_records;
};

} // namespace

// Every recorded channel choice, run through the engine's scope from the same input and held to the record within the
// bound its conversion explains.
TEST_F(ChannelScopeExpectationTest, EveryChannelChoiceMatchesItsRecordWithinItsStatedBound)
{
    ASSERT_EQ(m_records.size(), scopeCases().size());
    for (const fixtures::Expectation& record : m_records)
    {
        const ScopeCase* item = scopeCaseOf(record.id);
        ASSERT_NE(item, nullptr) << record.id;
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(record.inputName);
        ASSERT_NE(image, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> scoped = scopedByEngine(*m_session, *item, *image, *samples);
        ASSERT_TRUE(scoped.hasValue()) << record.id << ": " << scoped.status().message();
        ASSERT_EQ(scoped->channelCount(), record.result.channelCount()) << record.id;
        const double largest = largestDifference(record.result, *scoped);
        RecordProperty(record.id, std::to_string(largest));
        if (!item->recordClamps)
        {
            EXPECT_TRUE(fixtures::sameSamples(record.result, *scoped))
                << record.id << " (" << item->reason << "): the largest difference is " << largest;
            continue;
        }
        // The record clamps the recomposed colour; the engine leaves a floating colour where the conversion put it.
        // After the same clamp the two agree within the bound, and every sample further apart than the bound is one
        // the engine left outside the unit interval.
        fixtures::FixtureSamples clamped = *scoped;
        std::uint64_t outside = 0u;
        for (std::uint32_t c = 0u; c < clamped.channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < clamped.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < clamped.width(); ++x)
                {
                    const double value = scoped->value(x, y, c);
                    const double held = (std::min)((std::max)(value, 0.0), 1.0);
                    clamped.setValue(x, y, c, held);
                    if (std::abs(value - record.result.value(x, y, c)) > item->bound)
                    {
                        EXPECT_NE(value, held) << record.id << ": a difference the clamp does not explain at " << x
                                               << "," << y << " channel " << c;
                        ++outside;
                    }
                }
            }
        }
        const double clampedLargest = largestDifference(record.result, clamped);
        RecordProperty(record.id + " clamped", std::to_string(clampedLargest));
        RecordProperty(record.id + " outside", std::to_string(outside));
        EXPECT_LE(clampedLargest, item->bound) << record.id << " (" << item->reason << ")";
    }
}

// Every channel choice has one record, and no record compared was produced by the engine under test.
TEST_F(ChannelScopeExpectationTest, EveryChannelChoiceHasARecord)
{
    std::set<std::string> modes;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
        EXPECT_TRUE(modes.insert(scopeCaseOf(record.id)->mode).second) << record.id;
    }
    for (const ScopeCase& item : scopeCases())
    {
        EXPECT_EQ(modes.count(item.mode), 1u) << item.mode;
    }
}
