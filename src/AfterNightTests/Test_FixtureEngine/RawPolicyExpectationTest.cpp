// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's RAW opens through the engine, held to their reference records under the same options.
//
// Each record is opened the way the product opens it, in one session whose own registry carries a camera RAW adapter
// with no balance. A full-colour frame is read through a registry the read names, whose adapter carries the record's
// balance. A mosaic is read to its codes through the session's own registry and balanced by the filter-array step the
// record's mode resolves to, stretched to sixteen-bit full scale and truncated, which is what the record's loader did
// inside the load. Either is turned by its recorded orientation when the record asks. The last case opens a viewer's
// record and a workflow's at once in that one session and holds each to its own record.
//
// A full-colour record holds the frame's interleaved samples written across its three planes, never turned, so it is
// held to exactly that: the engine's answer, unturned where the record is not turned, written interleaved across three
// planes and held at 65534 or below, is the record sample for sample.

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/RawPolicyCases.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

an::Status failed(const std::string& message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, an::StatusDetail{.message = message});
}

/// The full-colour balance a persisted mode maps to.
an::RawFullColorWhiteBalance fullColourBalance(std::string_view mode)
{
    if (mode == "none")
    {
        return an::RawFullColorWhiteBalance::None;
    }
    if (mode == "camera_as_shot")
    {
        return an::RawFullColorWhiteBalance::Camera;
    }
    if (mode == "auto_wb")
    {
        return an::RawFullColorWhiteBalance::Automatic;
    }
    if (mode == "custom")
    {
        return an::RawFullColorWhiteBalance::Custom;
    }
    return an::RawFullColorWhiteBalance::Daylight;
}

/// The mosaic balance a persisted mode maps to.
an::CfaWhiteBalanceMode mosaicBalance(std::string_view mode)
{
    if (mode == "none")
    {
        return an::CfaWhiteBalanceMode::None;
    }
    if (mode == "camera_as_shot")
    {
        return an::CfaWhiteBalanceMode::Camera;
    }
    if (mode == "auto_wb")
    {
        return an::CfaWhiteBalanceMode::Automatic;
    }
    if (mode == "custom")
    {
        return an::CfaWhiteBalanceMode::Custom;
    }
    return an::CfaWhiteBalanceMode::Daylight;
}

/// A registry holding one camera RAW adapter configured with a record's full-colour balance.
an::Result<std::shared_ptr<const an::CodecRegistry>> registryFor(const fixtures::RawLoadPolicy& policy)
{
    an::Result<std::shared_ptr<const an::ImageCodec>> codec = an::createRawCodec(
        {.fullColorWhiteBalance = fullColourBalance(policy.whiteBalance),
         .fullColorMultipliers = {policy.customGains[0], policy.customGains[1], policy.customGains[2]}});
    if (!codec.hasValue())
    {
        return codec.status();
    }
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> codecs{*codec};
    return an::CodecRegistry::create(codecs);
}

/// A tight planar layout of sixteen-bit samples over an extent.
an::Result<an::ImageLayout> planarLayout(const an::EngineContext& engine, const an::ImageExtent& extent)
{
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create({.extent = extent,
                                                                           .elementSizeBytes = 2u,
                                                                           .samplesPerPixel = 1u,
                                                                           .planeCount = extent.channelCount(),
                                                                           .rowAlignmentBytes = 1u},
                                                                          engine.limits());
    if (!layout.has_value())
    {
        return failed("no tight layout for the frame");
    }
    return *layout;
}

/// Planar sixteen-bit samples as the neutral layer holds them.
fixtures::FixtureSamples neutralOf(const an::ImageLayout& layout, const std::uint16_t* samples)
{
    const an::ImageExtent& extent = layout.extent();
    fixtures::FixtureSamples result(extent.width(), extent.height(), extent.channelCount(),
                                    fixtures::FixtureSampleType::UInt16);
    for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
        {
            const auto* row = reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::byte*>(samples) +
                                                                     channel * layout.planeStrideBytes() +
                                                                     y * layout.rowStrideBytes());
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                result.setValue(x, y, channel, row[x]);
            }
        }
    }
    return result;
}

/// Opens one record's DNG as the product does, in an operation of one worker the session admits.
an::Result<fixtures::FixtureSamples> openThroughEngine(const an::consumer::EngineSession& session,
                                                       const fixtures::RawPolicyCase& item, const std::string& path)
{
    const an::Result<an::consumer::OperationScope> admitted = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 1u});
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::EngineContext& engine = admitted->engine();
    const an::ExecutionContext& execution = admitted->execution();
    const fixtures::RawPolicyInput* input = fixtures::rawPolicyInput(item.input);
    if (input == nullptr)
    {
        return failed("no input is named " + item.input);
    }
    const bool mosaic = input->image.channels == fixtures::FixtureChannels::Mono;
    // A mosaic's codes do not depend on the full-colour balance, so it reads through the session's own registry; a
    // full-colour frame reads through one carrying the record's balance.
    const auto read = [&]() -> an::Result<an::CodecDecodeResult> {
        if (mosaic)
        {
            return an::decodeImage(engine, execution, {.path = path});
        }
        an::Result<std::shared_ptr<const an::CodecRegistry>> registry = registryFor(item.policy);
        if (!registry.hasValue())
        {
            return registry.status();
        }
        return an::decodeImage(engine, execution, **registry, {.path = path});
    };
    an::Result<an::CodecDecodeResult> decoded = read();
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    const std::optional<an::RawCameraMetadata>& camera = decoded->metadata.acquisition().rawCamera();
    if (!camera.has_value())
    {
        return failed("the decode stated no camera record");
    }
    const an::RawFrameDescriptor raw = *decoded->frame.descriptor();
    an::Result<an::ImageLayout> layout = planarLayout(engine, raw.extent());
    if (!layout.hasValue())
    {
        return layout.status();
    }
    std::vector<std::uint16_t> samples(layout->totalBytes() / 2u);
    if (mosaic)
    {
        an::CfaWhiteBalanceRequest request{
            .mode = mosaicBalance(item.policy.whiteBalance),
            .custom = {item.policy.customGains[0], item.policy.customGains[1], item.policy.customGains[2]}};
        std::optional<an::CfaGreyWorld> measured;
        if (request.mode == an::CfaWhiteBalanceMode::Automatic)
        {
            an::Result<an::CfaChannelValues> clip = an::cfaGreyWorldClipLevels(*camera);
            if (!clip.hasValue())
            {
                return clip.status();
            }
            an::Result<an::CfaGreyWorld> grey =
                an::measureCfaGreyWorld(execution, an::SampleType::UInt16, decoded->frame.bytes().data(),
                                        *decoded->frame.layout(), raw.cfa(), {.clippedAbove = *clip});
            if (!grey.hasValue())
            {
                return grey.status();
            }
            measured = *grey;
            request.measured = &*measured;
        }
        an::Result<an::CfaWhiteBalance> balance = an::resolveCfaWhiteBalance(*camera, request);
        if (!balance.hasValue())
        {
            return balance.status();
        }
        an::Result<an::CfaChannelResult> gained = an::applyCfaChannelGains(
            execution, an::SampleType::UInt16, decoded->frame.bytes().data(), *decoded->frame.layout(), raw.cfa(),
            samples.data(), *layout, balance->multipliers(),
            an::ConversionPolicy{.rounding = an::RoundingMode::TowardZero, .domain = an::DomainMapping::CodeValue});
        if (!gained.hasValue())
        {
            return gained.status();
        }
    }
    else
    {
        // The layout is held in a local: the optional that carries it would end with this statement.
        const std::optional<an::ImageLayout> sourceLayout = decoded->frame.layout();
        const an::ImageLayout& source = *sourceLayout;
        for (std::uint32_t channel = 0u; channel < raw.extent().channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < raw.extent().height(); ++y)
            {
                std::memcpy(reinterpret_cast<std::byte*>(samples.data()) + channel * layout->planeStrideBytes() +
                                y * layout->rowStrideBytes(),
                            decoded->frame.bytes().data() + channel * source.planeStrideBytes() +
                                y * source.rowStrideBytes(),
                            static_cast<std::size_t>(layout->minimumRowBytes()));
            }
        }
    }
    if (!item.policy.autoRotate)
    {
        return neutralOf(*layout, samples.data());
    }
    const std::optional<an::ImageOrientation> orientation = an::imageOrientationFromRawFlip(camera->orientation);
    const std::optional<an::ImageDescriptor> described = raw.toImageDescriptor();
    if (!orientation.has_value() || !described.has_value())
    {
        return failed("the frame's orientation or description is not one the engine states");
    }
    an::Result<an::GeometryPlan> plan = an::planOrientation(*described, *orientation);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::ImageLayout> turnedLayout = planarLayout(engine, plan->destinationDescriptor().extent());
    if (!turnedLayout.hasValue())
    {
        return turnedLayout.status();
    }
    std::vector<std::uint16_t> turned(turnedLayout->totalBytes() / 2u);
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, plan->destinationDescriptor().extent());
    if (!mask.hasValue())
    {
        return mask.status();
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
    if (!maskView.has_value())
    {
        return failed("no view of the turned frame's mask");
    }
    an::Result<an::GeometryResult> ran = an::transformImageGeometry(
        execution, an::SampleType::UInt16, samples.data(), *layout, turned.data(), *turnedLayout, *plan, {}, *maskView);
    if (!ran.hasValue())
    {
        return ran.status();
    }
    return neutralOf(*turnedLayout, turned.data());
}

/// A full-colour answer written the way the record holds one: each pixel's three samples one after another, across
/// the three planes in turn, each held at 65534 or below.
fixtures::FixtureSamples asTheRecordHoldsIt(const fixtures::FixtureSamples& answer)
{
    const std::uint32_t width = answer.width();
    const std::uint32_t height = answer.height();
    const std::uint64_t pixels = std::uint64_t{width} * height;
    fixtures::FixtureSamples written(width, height, 3u, fixtures::FixtureSampleType::UInt16);
    for (std::uint64_t pixel = 0u; pixel < pixels; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const double value = (std::min)(answer.value(static_cast<std::uint32_t>(pixel % width),
                                                         static_cast<std::uint32_t>(pixel / width), channel),
                                            65534.0);
            const std::uint64_t at = pixel * 3u + channel;
            const auto plane = static_cast<std::uint32_t>(at / pixels);
            const std::uint64_t within = at % pixels;
            written.setValue(static_cast<std::uint32_t>(within % width), static_cast<std::uint32_t>(within / width),
                             plane, value);
        }
    }
    return written;
}

/// Whether one answer is another turned a quarter clockwise: the turned answer shows at (x, y) the sample the
/// unturned one holds at (y, height - 1 - x).
bool turnedClockwise(const fixtures::FixtureSamples& unturned, const fixtures::FixtureSamples& turned)
{
    if (turned.width() != unturned.height() || turned.height() != unturned.width() ||
        turned.channelCount() != unturned.channelCount())
    {
        return false;
    }
    for (std::uint32_t channel = 0u; channel < turned.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < turned.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < turned.width(); ++x)
            {
                if (turned.value(x, y, channel) != unturned.value(y, unturned.height() - 1u - x, channel))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

/// How many samples two records differ in, and the largest difference among them.
struct Difference
{
    std::uint64_t samples = 0u;
    double largest = 0.0;
};

Difference differenceOf(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    Difference difference;
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        difference.samples = std::numeric_limits<std::uint64_t>::max();
        return difference;
    }
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double gap = std::abs(left.value(x, y, channel) - right.value(x, y, channel));
                if (gap != 0.0)
                {
                    ++difference.samples;
                    difference.largest = (std::max)(difference.largest, gap);
                }
            }
        }
    }
    return difference;
}

class RawPolicyExpectationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fixtures::RawLoadPolicy balanceNothing;
        balanceNothing.whiteBalance = "none";
        // The session's own registry balances nothing, as a workflow reads RAW frames.
        an::consumer::EngineSessionSettings settings;
        settings.maxConcurrency = 2u;
        settings.codecRegistry = [balanceNothing] { return registryFor(balanceNothing); };
        an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));

        m_directory = std::filesystem::temp_directory_path() /
                      ("engine-raw-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_directory);
        for (const fixtures::RawPolicyInput& input : fixtures::rawPolicyInputs())
        {
            std::string error;
            const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(input.image, error);
            ASSERT_TRUE(samples.has_value()) << error;
            const std::filesystem::path path = m_directory / (input.image.name + ".dng");
            ASSERT_TRUE(fixtures::writeRawPolicyDng(input, *samples, path, error)) << error;
            const std::u8string utf8 = path.u8string();
            m_paths[input.image.name] = std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
            m_samples[input.image.name] = *samples;
        }

        std::string error;
        const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
        ASSERT_TRUE(directory.has_value()) << error;
        const std::optional<std::vector<fixtures::Expectation>> records =
            fixtures::readExpectationFile(*directory / fixtures::kRawPolicyExpectationFile, error);
        ASSERT_TRUE(records.has_value()) << error;
        m_records = *records;
    }

    void TearDown() override
    {
        m_session.reset();
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    /// The record of a case, or null.
    const fixtures::Expectation* recordOf(const std::string& id) const
    {
        for (const fixtures::Expectation& record : m_records)
        {
            if (record.id == id)
            {
                return &record;
            }
        }
        return nullptr;
    }

    /// Holds a produced answer to its record by the record's classification, and an accepted one to its case's count
    /// and bound; a full-colour answer is also held, written the record's way, to the record exactly.
    void expectMatches(const fixtures::RawPolicyCase& item, const fixtures::FixtureSamples& produced) const
    {
        const fixtures::Expectation* record = recordOf(item.id());
        ASSERT_NE(record, nullptr) << item.id() << " has no record";
        if (item.recordLayout != fixtures::RawRecordLayout::AsDecoded)
        {
            fixtures::FixtureSamples answer = produced;
            if (item.recordLayout == fixtures::RawRecordLayout::InterleavedUnturned)
            {
                fixtures::RawPolicyCase unturned = item;
                unturned.policy.autoRotate = false;
                const an::Result<fixtures::FixtureSamples> plain =
                    openThroughEngine(*m_session, unturned, m_paths.at(item.input));
                ASSERT_TRUE(plain.hasValue()) << item.id() << ": " << plain.status().message();
                EXPECT_TRUE(turnedClockwise(*plain, produced)) << item.id() << ": the answer is not the frame turned";
                answer = *plain;
            }
            fixtures::Expectation exact = *record;
            exact.classification = fixtures::ExpectationClass::Exact;
            exact.acceptedReason.clear();
            const fixtures::ExpectationComparison interleaved =
                fixtures::compareWithExpectation(exact, asTheRecordHoldsIt(answer));
            EXPECT_TRUE(interleaved.matched) << item.id() << ", written the record's way: " << interleaved.reason;
            if (item.recordLayout == fixtures::RawRecordLayout::InterleavedUnturned)
            {
                return;
            }
        }
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(*record, produced);
        EXPECT_TRUE(comparison.matched) << item.id() << ": " << comparison.reason;
        const Difference difference = differenceOf(record->result, produced);
        if (record->classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_EQ(difference.samples, item.differing) << item.id() << ": " << item.acceptedReason;
            EXPECT_LE(difference.largest, item.acceptedBound) << item.id();
        }
    }

    /// What the session's engine context holds now: zero once every open has let its frames go.
    [[nodiscard]] std::uint64_t liveEngineBytes() const
    {
        const an::Result<an::consumer::SessionHold> hold = m_session->hold();
        return hold.hasValue() ? hold->engine().memoryResource()->counters().liveBytes : ~0ull;
    }

    std::optional<an::consumer::EngineSession> m_session;
    std::filesystem::path m_directory;
    std::map<std::string, std::string> m_paths;
    std::map<std::string, fixtures::FixtureSamples> m_samples;
    std::vector<fixtures::Expectation> m_records;
};

} // namespace

// Every recorded open, made through the engine from the same file and held to the record by its classification; an
// accepted difference is held to the count and the bound its case states.
TEST_F(RawPolicyExpectationTest, EveryOpenMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::RawPolicyCase* item = fixtures::rawPolicyCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        ASSERT_TRUE(fixtures::expectationDescribes(record, fixtures::rawPolicyInput(item->input)->image,
                                                   m_samples.at(item->input)))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> produced =
            openThroughEngine(*m_session, *item, m_paths.at(item->input));
        ASSERT_TRUE(produced.hasValue()) << record.id << ": " << produced.status().message();
        expectMatches(*item, *produced);
    }
    EXPECT_EQ(liveEngineBytes(), 0u);
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(RawPolicyExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_TRUE(ids.insert(record.id).second) << record.id;
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
    for (const fixtures::RawPolicyCase& item : fixtures::rawPolicyCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}

// The viewer opens with the user's daylight record while a workflow, which balances nothing, opens the same file in
// the same session: each answer is its own record's, while both run.
TEST_F(RawPolicyExpectationTest, AViewersAndAWorkflowsOpensAtOnceEachMatchTheirOwnRecord)
{
    for (const fixtures::RawPolicyInput& input : fixtures::rawPolicyInputs())
    {
        const fixtures::RawPolicyCase* viewer = fixtures::rawPolicyCase("raw-open.daylight/" + input.image.name);
        const fixtures::RawPolicyCase* workflow = fixtures::rawPolicyCase("raw-open.none/" + input.image.name);
        ASSERT_NE(viewer, nullptr);
        ASSERT_NE(workflow, nullptr);
        const std::string& path = m_paths.at(input.image.name);
        std::vector<an::Result<fixtures::FixtureSamples>> viewerOpens;
        std::vector<an::Result<fixtures::FixtureSamples>> workflowOpens;
        std::latch start(2);
        std::thread other([&] {
            start.arrive_and_wait();
            for (int round = 0; round < 6; ++round)
            {
                workflowOpens.push_back(openThroughEngine(*m_session, *workflow, path));
            }
        });
        start.arrive_and_wait();
        for (int round = 0; round < 6; ++round)
        {
            viewerOpens.push_back(openThroughEngine(*m_session, *viewer, path));
        }
        other.join();
        for (const an::Result<fixtures::FixtureSamples>& opened : viewerOpens)
        {
            ASSERT_TRUE(opened.hasValue()) << input.image.name << ": " << opened.status().message();
            expectMatches(*viewer, *opened);
        }
        for (const an::Result<fixtures::FixtureSamples>& opened : workflowOpens)
        {
            ASSERT_TRUE(opened.hasValue()) << input.image.name << ": " << opened.status().message();
            expectMatches(*workflow, *opened);
        }
    }
    EXPECT_EQ(liveEngineBytes(), 0u);
}
