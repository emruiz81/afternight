// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's comparison difference through the engine, held to its reference records.
//
// Each case states which difference the comparison view asks for over two neutral inputs. This computes it with the
// engine's image difference from the same inputs and compares with the record by its classification. The record
// holds the difference in the first image's representation; the operation writes Float32, so an integer record is
// compared with the engine's own conversion of that answer into the record's representation, under the first image's
// domain - which is what a consumer that keeps the first image's representation would write.

#include "fixtures/common/DifferenceCases.h"
#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Pointwise/ImageDifference.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
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

/// What a difference reads from an image the adapter made.
struct SourceView
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    const void* samples = nullptr;
};

std::optional<SourceView> viewOf(const adapter::EngineImage& image)
{
    return std::visit(
        [](const auto& typed) -> std::optional<SourceView> {
            const auto descriptor = typed.descriptor();
            const auto view = typed.view();
            if (!descriptor.has_value() || !view.has_value())
            {
                return std::nullopt;
            }
            return SourceView{*descriptor, view->layout(), view->data()};
        },
        image);
}

/// One sample of the engine's Float32 answer in the record's representation, by the engine's own conversion.
double inRecordRepresentation(float value, fixtures::FixtureSampleType type, an::NumericDomain domain)
{
    an::ConversionPolicy policy{};
    policy.domain =
        domain == an::NumericDomain::RawSensorCode ? an::DomainMapping::CodeValue : an::DomainMapping::FullScale;
    double converted = 0.0;
    const bool known = an::dispatchSampleType(adapter::engineSampleType(type), [&]<typename SampleT>() {
        converted = static_cast<double>(an::convertSample<float, SampleT>(value, policy).value);
    });
    return known ? converted : std::numeric_limits<double>::quiet_NaN();
}

/// What the engine produces for a case, as neutral samples of the record's representation.
an::Result<fixtures::FixtureSamples> differencedByEngine(const an::consumer::EngineSession& session,
                                                         const fixtures::DifferenceCase& item,
                                                         fixtures::FixtureSampleType recordType)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const fixtures::FixtureImage* first = fixtures::fixtureNamed(item.input);
    const fixtures::FixtureImage* second = fixtures::fixtureNamed(item.second);
    std::string error;
    const std::optional<fixtures::FixtureSamples> firstSamples =
        first == nullptr ? std::nullopt : fixtures::materializeFixture(*first, error);
    const std::optional<fixtures::FixtureSamples> secondSamples =
        second == nullptr ? std::nullopt : fixtures::materializeFixture(*second, error);
    if (!firstSamples.has_value() || !secondSamples.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, an::StatusDetail{.message = error});
    }
    const an::Result<adapter::EngineImage> left = adapter::makeEngineImage(engine, *first, *firstSamples);
    const an::Result<adapter::EngineImage> right = adapter::makeEngineImage(engine, *second, *secondSamples);
    if (!left.hasValue() || !right.hasValue())
    {
        return left.hasValue() ? right.status() : left.status();
    }
    const std::optional<SourceView> leftView = viewOf(*left);
    const std::optional<SourceView> rightView = viewOf(*right);
    if (!leftView.has_value() || !rightView.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::StatusDetail{.message = "an input cannot be viewed"});
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::Result<an::ImageDifferencePlan> plan = an::planImageDifference(an::ImageDifferenceRequest{
        .measure = item.measure == fixtures::DifferenceMeasure::Grey ? an::ImageDifferenceMeasure::MeanAbsolute
                                                                     : an::ImageDifferenceMeasure::Absolute});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageLayoutRequest> request = an::imageDifferenceLayout(*plan, leftView->descriptor);
    if (!request.hasValue())
    {
        return request.status();
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    if (!layout.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::StatusDetail{.message = "the difference's layout cannot be built"});
    }
    std::vector<float> destination(layout->totalBytes() / sizeof(float), 0.0f);
    const an::Result<an::ImageDifferenceResult> computed = an::computeImageDifference(
        execution, *plan,
        an::ImageDifferenceSource{
            .descriptor = &leftView->descriptor, .samples = leftView->samples, .layout = &leftView->layout},
        an::ImageDifferenceSource{
            .descriptor = &rightView->descriptor, .samples = rightView->samples, .layout = &rightView->layout},
        destination.data(), *layout);
    if (!computed.hasValue())
    {
        return computed.status();
    }
    const std::uint32_t channels = layout->extent().channelCount();
    fixtures::FixtureSamples result(layout->extent().width(), layout->extent().height(), channels, recordType);
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < layout->extent().height(); ++y)
        {
            for (std::uint32_t x = 0u; x < layout->extent().width(); ++x)
            {
                const float value =
                    destination[static_cast<std::size_t>(*layout->sampleByteOffset(c, y, x, 0u) / sizeof(float))];
                result.setValue(x, y, c,
                                inRecordRepresentation(value, recordType, leftView->descriptor.numericDomain()));
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
                largest = (std::max)(largest, std::abs(left.value(x, y, channel) - right.value(x, y, channel)));
            }
        }
    }
    return largest;
}

class DifferenceExpectationTest : public ::testing::Test
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
            fixtures::readExpectationFile(*directory / fixtures::kDifferenceExpectationFile, error);
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

// Every recorded comparison, computed through the engine from the same inputs and held to the record by its
// classification.
TEST_F(DifferenceExpectationTest, EveryComparisonMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::DifferenceCase* item = fixtures::differenceCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(item->input);
        ASSERT_NE(image, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> differenced =
            differencedByEngine(*m_session, *item, record.result.sampleType());
        ASSERT_TRUE(differenced.hasValue()) << record.id << ": " << differenced.status().message();
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *differenced);
        const double largest = largestDifference(record.result, *differenced);
        RecordProperty(record.id, std::to_string(largest));
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largest;
    }
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(DifferenceExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_TRUE(ids.insert(record.id).second) << record.id;
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
    for (const fixtures::DifferenceCase& item : fixtures::differenceCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
