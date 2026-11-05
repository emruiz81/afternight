// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's frame-kind reading, combined-image metadata and history steps through the engine, held to their
// reference records.
//
// Each case states one computation over neutral inputs. A frame's kind is read from its cards by
// `readFrameTypeCards`. A combination's sources are what the FITS decoder would hand over for their cards - the cards
// preserved and the acquisition record projected from them - combined by `combinedImageMetadata`, written by the FITS
// writer and read back, because a header is what the record holds and what a user sees. A history step is
// written with `writeImageSnapshot`, read with `readImageSnapshot` and adopted as an image.

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/MetadataSnapshotCases.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/CombinedMetadata.h>
#include <AfterNightEngine/IO/FrameTypeCards.h>
#include <AfterNightEngine/IO/ImageSnapshot.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
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

constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();

an::Status failed(const std::string& message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, an::StatusDetail{.message = message});
}

/// What a combination declares: a new image restating its description and meaning, on its first source's grid.
constexpr an::OperationEffect kCombinationEffect{.description = an::DescriptionEffect::Restates,
                                                 .semantics = an::SemanticEffect::Restates,
                                                 .validity =
                                                     an::ValidityTransition{.policy = an::ValidityPolicy::Combine}};

/// A description's cards as a header carries them.
an::Result<an::CodecMetadata> codecOf(const fixtures::FixtureImage& image)
{
    std::vector<an::CodecCard> cards;
    for (const fixtures::FixtureCard& card : image.cards)
    {
        an::Result<an::CodecCard> made = an::CodecCard::create(an::CodecNamespace::Fits, card.keyword,
                                                               fixtures::metadataCardValue(card), card.comment);
        if (!made.hasValue())
        {
            return made.status();
        }
        cards.push_back(std::move(*made));
    }
    return an::CodecMetadata::create({}, std::move(cards));
}

/// The metadata the FITS decoder hands over for a description's cards: the cards, and the facts projected from them.
an::Result<an::ImageMetadata> decodedMetadataOf(const std::string& name)
{
    const fixtures::FixtureImage* image = fixtures::metadataSnapshotInput(name);
    if (image == nullptr)
    {
        return failed("no input is named " + name);
    }
    an::Result<an::CodecMetadata> codec = codecOf(*image);
    if (!codec.hasValue())
    {
        return codec.status();
    }
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::projectAcquisition(*codec).acquisition);
    if (!acquisition.hasValue())
    {
        return acquisition.status();
    }
    return an::ImageMetadata::create(std::move(*acquisition), {}, {}, {}, std::move(*codec), std::nullopt);
}

double kindCode(an::FrameType type)
{
    switch (type)
    {
    case an::FrameType::Bias:
        return 0.0;
    case an::FrameType::Dark:
        return 1.0;
    case an::FrameType::Flat:
        return 2.0;
    case an::FrameType::Light:
        return 3.0;
    case an::FrameType::Unknown:
        break;
    }
    return -1.0;
}

/// A fresh directory for one computation's files.
std::filesystem::path scratchDirectory(const std::string& name)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("engine-metadata-" + name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    return directory;
}

std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

/// Combines the case's sources, writes the result as FITS, reads it back, and states what its header says.
an::Result<std::vector<double>> combinedRow(const an::consumer::EngineSession& session,
                                            const fixtures::MetadataSnapshotCase& item)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    std::vector<an::ImageMetadata> sources;
    std::vector<std::string> names{item.input};
    names.insert(names.end(), item.others.begin(), item.others.end());
    for (const std::string& name : names)
    {
        an::Result<an::ImageMetadata> metadata = decodedMetadataOf(name);
        if (!metadata.hasValue())
        {
            return metadata.status();
        }
        sources.push_back(std::move(*metadata));
    }
    std::vector<const an::ImageMetadata*> pointers;
    for (const an::ImageMetadata& source : sources)
    {
        pointers.push_back(&source);
    }
    an::Result<an::ImageMetadata> combined =
        an::combinedImageMetadata(pointers, kCombinationEffect,
                                  an::OperationOutcome{.operation = "composeBlend", .semantics = an::ImageSemantics{}});
    if (!combined.hasValue())
    {
        return combined.status();
    }

    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create({.extent = *an::ImageExtent::create(1u, 1u, 1u, engine.limits()),
                                     .sampleType = an::SampleType::Float32,
                                     .channelSchema = an::ChannelSchema::Mono,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::NormalizedLinear,
                                     .cfa = {},
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    an::Result<an::Image<float>> image = an::Image<float>::createFilled(
        engine, *descriptor, an::StorageArrangement::Planar, 0.25f, std::move(*combined));
    if (!image.hasValue())
    {
        return image.status();
    }
    an::Result<an::OutputIntent> intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    an::Result<an::RawFrame> frame = an::borrowImageFrame(*image);
    if (!intent.hasValue() || !frame.hasValue())
    {
        return failed("the combined image cannot be written");
    }
    const std::filesystem::path directory = scratchDirectory("combined");
    const std::string path = utf8Of(directory / "combined.fits");
    std::vector<an::CodecCard> cards;
    {
        const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
        if (!admitted.hasValue())
        {
            return admitted.status();
        }
        const an::ExecutionContext& execution = admitted->execution();
        an::Result<an::OutputReport> written = an::writeImage(engine, execution,
                                                              {.destinationPath = path,
                                                               .format = an::CodecFormat::Fits,
                                                               .intent = &*intent,
                                                               .durability = an::OutputDurability::CloseOnly},
                                                              *frame, image->metadata());
        if (!written.hasValue())
        {
            return written.status();
        }
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(engine, execution, an::CodecReadRequest{.path = path});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::span<const an::CodecCard> read = decoded->metadata.codec().cards();
        cards.assign(read.begin(), read.end());
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

    const an::CodecMetadata header = *an::CodecMetadata::create({}, cards);
    const std::optional<double> exposure = an::projectAcquisition(header).acquisition.exposureSeconds;
    std::vector<double> row{exposure.value_or(kNotANumber)};
    for (const std::string_view keyword : fixtures::kCombinationKeywords)
    {
        const an::CodecCard* card = header.find(an::CodecNamespace::Fits, keyword);
        row.push_back(card != nullptr ? 1.0 : 0.0);
        row.push_back(card != nullptr ? fixtures::metadataCardNumber(card->value()).value_or(kNotANumber)
                                      : kNotANumber);
    }
    return row;
}

/// Keeps an image as a history step and restores it, as a consumer does: the image is an asset of the session, the step
/// is written under its read lease, and the restored step is a new asset with an identity of its own.
an::Result<fixtures::FixtureSamples> historyRoundTrip(const an::consumer::EngineSession& session,
                                                      const fixtures::FixtureImage& image,
                                                      const fixtures::FixtureSamples& samples)
{
    an::Result<an::consumer::ImageAsset> kept = adapter::makeImageAsset(session, image, samples);
    if (!kept.hasValue())
    {
        return kept.status();
    }
    const an::Result<an::consumer::AssetReadLease> lease = kept->read();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Result<an::ArtifactIdentity> identity = an::ArtifactIdentity::create("history-step", "fixture");
    const std::filesystem::path directory = scratchDirectory(image.name);
    const std::string path = utf8Of(directory / "step.ansnap");
    std::string error;
    std::optional<an::consumer::ImageAsset> restored =
        lease->visit([&](const auto& typed) -> std::optional<an::consumer::ImageAsset> {
            using Typed = std::decay_t<decltype(typed)>;
            an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
            if (!frame.hasValue())
            {
                error = frame.status().message();
                return std::nullopt;
            }
            {
                const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
                if (!admitted.hasValue())
                {
                    error = admitted.status().message();
                    return std::nullopt;
                }
                an::Result<an::ImageSnapshotWriteReport> written = an::writeImageSnapshot(
                    admitted->engine(), admitted->execution(),
                    {.destinationPath = path, .identity = &*identity, .durability = an::OutputDurability::CloseOnly},
                    *frame, typed.metadata());
                if (!written.hasValue())
                {
                    error = written.status().message();
                    return std::nullopt;
                }
            }
            an::Result<an::ImageSnapshotContents> contents = [&]() -> an::Result<an::ImageSnapshotContents> {
                const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
                if (!admitted.hasValue())
                {
                    return admitted.status();
                }
                return an::readImageSnapshot(admitted->engine(), admitted->execution(), path);
            }();
            if (!contents.hasValue())
            {
                error = contents.status().message();
                return std::nullopt;
            }
            an::Result<Typed> adopted = Typed::adopt(std::move(contents->frame), std::move(contents->metadata));
            if (!adopted.hasValue())
            {
                error = adopted.status().message();
                return std::nullopt;
            }
            an::Result<an::consumer::ImageAsset> asset =
                an::consumer::ImageAsset::adopt(session, adapter::EngineImage(std::move(*adopted)));
            if (!asset.hasValue())
            {
                error = asset.status().message();
                return std::nullopt;
            }
            return std::move(*asset);
        });
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    if (!restored.has_value())
    {
        return failed(error);
    }
    if (restored->identity() == kept->identity())
    {
        return failed("the restored step kept the identity of the image it was taken from");
    }
    std::optional<fixtures::FixtureSamples> read = adapter::readAssetSamples(*restored, error);
    if (!read.has_value())
    {
        return failed(error);
    }
    return std::move(*read);
}

an::Result<fixtures::FixtureSamples> producedByEngine(const an::consumer::EngineSession& session,
                                                      const fixtures::MetadataSnapshotCase& item)
{
    const fixtures::FixtureImage* image = fixtures::metadataSnapshotInput(item.input);
    if (image == nullptr)
    {
        return failed("no input is named " + item.input);
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    if (!samples.has_value())
    {
        return failed(error);
    }
    switch (item.kind)
    {
    case fixtures::MetadataSnapshotCaseKind::FrameKind: {
        an::Result<an::CodecMetadata> codec = codecOf(*image);
        if (!codec.hasValue())
        {
            return codec.status();
        }
        return fixtures::metadataRow({kindCode(an::readFrameTypeCards(*codec).type)});
    }
    case fixtures::MetadataSnapshotCaseKind::Combination: {
        an::Result<std::vector<double>> row = combinedRow(session, item);
        if (!row.hasValue())
        {
            return row.status();
        }
        return fixtures::metadataRow(*row);
    }
    case fixtures::MetadataSnapshotCaseKind::HistoryRoundTrip:
        return historyRoundTrip(session, *image, *samples);
    }
    return failed("the case names no computation");
}

/// The samples of two records that differ, a pair of not-a-numbers counting as the same.
std::vector<std::uint32_t> differingSamples(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    std::vector<std::uint32_t> differing;
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return {std::numeric_limits<std::uint32_t>::max()};
    }
    for (std::uint32_t x = 0u; x < left.width(); ++x)
    {
        const double want = left.value(x, 0u, 0u);
        const double got = right.value(x, 0u, 0u);
        if (!(std::isnan(want) && std::isnan(got)) && want != got)
        {
            differing.push_back(x);
        }
    }
    return differing;
}

double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    for (std::uint32_t x = 0u; x < left.width() && x < right.width(); ++x)
    {
        const double difference = std::abs(left.value(x, 0u, 0u) - right.value(x, 0u, 0u));
        if (std::isfinite(difference))
        {
            largest = (std::max)(largest, difference);
        }
    }
    return largest;
}

class MetadataSnapshotExpectationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::Result<an::consumer::EngineSession> session = adapter::createFixtureSession(2u);
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));
        std::string error;
        const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
        ASSERT_TRUE(directory.has_value()) << error;
        const std::optional<std::vector<fixtures::Expectation>> records =
            fixtures::readExpectationFile(*directory / fixtures::kMetadataSnapshotExpectationFile, error);
        ASSERT_TRUE(records.has_value()) << error;
        m_records = *records;
    }

    void TearDown() override
    {
        m_session.reset();
    }

    std::optional<an::consumer::EngineSession> m_session;
    std::vector<fixtures::Expectation> m_records;
};

} // namespace

// Every recorded computation, run through the engine from the same inputs and held to the record by its
// classification; an accepted difference is held to the samples and the bound its case states.
TEST_F(MetadataSnapshotExpectationTest, EveryComputationMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::MetadataSnapshotCase* item = fixtures::metadataSnapshotCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* image = fixtures::metadataSnapshotInput(item->input);
        ASSERT_NE(image, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> produced = producedByEngine(*m_session, *item);
        ASSERT_TRUE(produced.hasValue()) << record.id << ": " << produced.status().message();
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *produced);
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason;
        if (record.classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_EQ(differingSamples(record.result, *produced), item->differing)
                << record.id << ": " << item->acceptedReason;
            EXPECT_LE(largestDifference(record.result, *produced), item->acceptedBound) << record.id;
        }
    }
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(MetadataSnapshotExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_TRUE(ids.insert(record.id).second) << record.id;
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
    for (const fixtures::MetadataSnapshotCase& item : fixtures::metadataSnapshotCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
