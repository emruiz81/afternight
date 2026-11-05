// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's astrometry conveniences through the engine, held to their reference records.
//
// Each case states one computation over a neutral input. This runs it with the engine's operations - the sexagesimal
// reader and writer, the pointing and optical hints, the plate scale and focal length, the spherical mean, the Julian
// epoch, the header-only card reader and the detection channel policy - and compares with the record by its
// classification. A header card goes to the engine as the FITS string or number a header carries, which is the value
// the record's reader held too.

#include "fixtures/common/AstrometryConvenienceCases.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Algorithms/Astrometry/Sexagesimal.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/FitsHeaderCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
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

/// A description's cards as a header carries them.
std::vector<an::CodecCard> codecCardsOf(const fixtures::FixtureImage& image)
{
    std::vector<an::CodecCard> cards;
    for (const fixtures::FixtureCard& card : image.cards)
    {
        an::Result<an::CodecCard> made = an::CodecCard::create(an::CodecNamespace::Fits, card.keyword,
                                                               fixtures::astrometryCardValue(card), card.comment);
        if (made.hasValue())
        {
            cards.push_back(std::move(*made));
        }
    }
    return cards;
}

std::vector<double> pointingRow(const an::Result<an::PointingHint>& hint)
{
    if (!hint.hasValue())
    {
        return {0.0, 0.0, 0.0, -1.0};
    }
    return {1.0, hint->direction.rightAscensionDegrees, hint->direction.declinationDegrees,
            static_cast<double>(static_cast<std::uint8_t>(hint->source))};
}

std::vector<double> valueRow(const an::Result<double>& value)
{
    return value.hasValue() ? std::vector<double>{1.0, *value} : std::vector<double>{0.0, 0.0};
}

std::vector<double> headerRow(const an::Result<std::vector<an::CodecCard>>& cards)
{
    std::vector<double> row{cards.hasValue() ? static_cast<double>(cards->size()) : -1.0};
    for (const std::string_view keyword : fixtures::kHeaderCardKeywords)
    {
        double number = kNotANumber;
        if (cards.hasValue())
        {
            for (const an::CodecCard& card : *cards)
            {
                if (card.keyword() == keyword)
                {
                    number = fixtures::astrometryCardNumber(card.value()).value_or(kNotANumber);
                    break;
                }
            }
        }
        row.push_back(number);
    }
    return row;
}

/// The overlay's own detection parameters.
an::StarDetectionRequest overlayRequest()
{
    an::StarDetectionRequest request;
    request.searchRadius = 5u;
    request.noiseSigma = 1.0;
    request.minimumRoundness = 0.4;
    request.model = an::PsfModel::Gaussian;
    request.maximumStars = 5000u;
    return request;
}

an::Result<std::vector<double>> detectionRow(const an::consumer::EngineSession& session,
                                             const fixtures::FixtureImage& image,
                                             const fixtures::FixtureSamples& samples)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<an::ImageDescriptor> descriptor = adapter::describeEngineImage(engine, image);
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }
    const std::optional<an::ImageLayoutRequest> request =
        an::layoutRequestFor(*descriptor, an::StorageArrangement::Planar, 1u);
    if (!request.has_value())
    {
        return failed("the star field has no planar layout");
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, engine.limits());
    if (!layout.has_value())
    {
        return failed("the star field's layout is refused");
    }
    const an::Result<an::StarFrameView> frame =
        an::StarFrameView::create(samples.bytes().data(), samples.bytes().size(), *layout, *descriptor);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    // The overlay's solution: a tangent plane at about an arcsecond a pixel, its reference at the frame centre.
    const an::Result<an::WcsSolution> solution = an::WcsSolution::create(
        an::WcsSolutionRequest{.referencePixel = {.x = image.width / 2.0, .y = image.height / 2.0},
                               .referencePoint = {.rightAscensionDegrees = 83.8, .declinationDegrees = -5.4},
                               .linear = {.cd11 = -2.8e-4, .cd12 = 0.0, .cd21 = 0.0, .cd22 = 2.8e-4},
                               .projection = an::WcsProjection::Gnomonic,
                               .imageWidth = image.width,
                               .imageHeight = image.height});
    if (!solution.hasValue())
    {
        return solution.status();
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::Result<an::StarDetectionChannelResult> detected = an::detectStarsByChannelPolicy(
        execution, *frame, overlayRequest(), an::greenThenRedChannelPolicy(*descriptor), &*solution);
    if (!detected.hasValue())
    {
        return detected.status();
    }
    const std::vector<an::StarDetectionChannelAttempt>& attempts = detected->attempts;
    return std::vector<double>{
        static_cast<double>(attempts.size()), attempts.empty() ? -1.0 : static_cast<double>(attempts[0].channel),
        attempts.size() < 2u ? -1.0 : static_cast<double>(attempts[1].channel), static_cast<double>(detected->channel)};
}

an::Result<fixtures::FixtureSamples> producedByEngine(const an::consumer::EngineSession& session,
                                                      const fixtures::AstrometryCase& item)
{
    const fixtures::FixtureImage* image = fixtures::astrometryConvenienceInput(item.input);
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
    case fixtures::AstrometryCaseKind::ParseRightAscension:
        return fixtures::astrometryRow(
            valueRow(an::parseRightAscensionDegrees(fixtures::astrometryInputText(*samples))));
    case fixtures::AstrometryCaseKind::ParseDeclination:
        return fixtures::astrometryRow(valueRow(an::parseDeclinationDegrees(fixtures::astrometryInputText(*samples))));
    case fixtures::AstrometryCaseKind::FormatRightAscension:
    case fixtures::AstrometryCaseKind::FormatDeclination: {
        const double degrees = fixtures::astrometryInputNumbers(*samples).front();
        const an::Result<std::string> text = item.kind == fixtures::AstrometryCaseKind::FormatRightAscension
                                                 ? an::formatRightAscension(degrees, 3u)
                                                 : an::formatDeclination(degrees, 3u);
        if (!text.hasValue())
        {
            return text.status();
        }
        return fixtures::astrometryTextRow(*text);
    }
    case fixtures::AstrometryCaseKind::PlateSolveHint:
        return fixtures::astrometryRow(
            pointingRow(an::readPointingHint(codecCardsOf(*image), an::plateSolveHintCards())));
    case fixtures::AstrometryCaseKind::ObservedPointing:
        return fixtures::astrometryRow(
            pointingRow(an::readPointingHint(codecCardsOf(*image), an::observedPointingCards())));
    case fixtures::AstrometryCaseKind::ScaleHint:
        return fixtures::astrometryRow(valueRow(an::resolvePlateScaleHint(an::readOpticalHints(codecCardsOf(*image)))));
    case fixtures::AstrometryCaseKind::DerivedFocalLength: {
        const an::Result<double> pixel =
            an::resolveEffectivePixelSizeMicrometres(an::readOpticalHints(codecCardsOf(*image)));
        if (!pixel.hasValue())
        {
            return fixtures::astrometryRow(valueRow(pixel));
        }
        return fixtures::astrometryRow(valueRow(an::computeFocalLengthMillimetres(*pixel, item.solvedScale)));
    }
    case fixtures::AstrometryCaseKind::MosaicCentre: {
        const std::vector<double> numbers = fixtures::astrometryInputNumbers(*samples);
        std::vector<an::SkyCoordinate> directions;
        for (std::size_t index = 0u; index + 1u < numbers.size(); index += 2u)
        {
            directions.push_back(
                an::SkyCoordinate{.rightAscensionDegrees = numbers[index], .declinationDegrees = numbers[index + 1u]});
        }
        const an::Result<an::SkyCoordinate> centre = an::sphericalMeanDirection(directions);
        return fixtures::astrometryRow(
            centre.hasValue() ? std::vector<double>{1.0, centre->rightAscensionDegrees, centre->declinationDegrees}
                              : std::vector<double>{0.0, 0.0, 0.0});
    }
    case fixtures::AstrometryCaseKind::EpochYear: {
        const an::Result<an::UtcInstant> instant = an::parseDateCard(fixtures::astrometryInputText(*samples));
        return fixtures::astrometryRow(instant.hasValue() ? std::vector<double>{1.0, an::julianEpochYear(*instant)}
                                                          : std::vector<double>{0.0, 0.0});
    }
    case fixtures::AstrometryCaseKind::HeaderCards:
        return fixtures::astrometryRow(headerRow(an::parseFitsHeaderCards(fixtures::astrometryInputText(*samples))));
    case fixtures::AstrometryCaseKind::DetectionPolicy: {
        const an::Result<std::vector<double>> row = detectionRow(session, *image, *samples);
        if (!row.hasValue())
        {
            return row.status();
        }
        return fixtures::astrometryRow(*row);
    }
    }
    return failed("the case names no computation");
}

double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return std::numeric_limits<double>::infinity();
    }
    for (std::uint32_t x = 0u; x < left.width(); ++x)
    {
        const double difference = std::abs(left.value(x, 0u, 0u) - right.value(x, 0u, 0u));
        if (std::isfinite(difference))
        {
            largest = (std::max)(largest, difference);
        }
    }
    return largest;
}

class AstrometryExpectationTest : public ::testing::Test
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
            fixtures::readExpectationFile(*directory / fixtures::kAstrometryConvenienceExpectationFile, error);
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
// classification; an accepted difference is held to the bound its case states.
TEST_F(AstrometryExpectationTest, EveryComputationMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::AstrometryCase* item = fixtures::astrometryConvenienceCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* image = fixtures::astrometryConvenienceInput(item->input);
        ASSERT_NE(image, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> produced = producedByEngine(*m_session, *item);
        ASSERT_TRUE(produced.hasValue()) << record.id << ": " << produced.status().message();
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *produced);
        const double largest = largestDifference(record.result, *produced);
        RecordProperty(record.id, std::to_string(largest));
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largest;
        if (record.classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_LE(largest, item->acceptedBound) << record.id << ": " << item->acceptedReason;
        }
    }
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(AstrometryExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_TRUE(ids.insert(record.id).second) << record.id;
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
    for (const fixtures::AstrometryCase& item : fixtures::astrometryConvenienceCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
