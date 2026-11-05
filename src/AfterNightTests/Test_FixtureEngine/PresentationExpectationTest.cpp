// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's display paths presented through the engine and held to their reference records.
//
// Each case states a product path's decisions in neutral terms; this executable turns them into the presentation's
// request - the viewer's high-quality gradient into its promotion, the brightness and contrast levels into an offset
// and a factor, the record's product into the presentation's output - presents the same input, and compares with
// the record by its classification. A path that hands on the stretched image in the source's representation
// is the presentation's working image converted back by the engine's own conversion; the lens presents a view of its
// region rather than a copy.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/PresentationCases.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleConversion.h>
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

/// What a presentation reads, taken from an image the adapter made.
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

/// The lens's region as a view of the source's own samples, with the source's strides.
std::optional<SourceView> regionOf(const SourceView& whole, const fixtures::PresentationCase& item)
{
    const std::uint32_t channels = whole.descriptor.extent().channelCount();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(item.cropWidth, item.cropHeight, channels, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::createWithStrides(
        an::ImageLayoutRequest{.extent = *extent,
                               .elementSizeBytes = whole.layout.elementSizeBytes(),
                               .samplesPerPixel = whole.layout.samplesPerPixel(),
                               .planeCount = whole.layout.planeCount(),
                               .rowAlignmentBytes = whole.layout.elementSizeBytes()},
        whole.layout.rowStrideBytes(), whole.layout.planeStrideBytes(), an::defaultEngineLimits());
    an::ImageDescriptorRequest request = whole.descriptor.toRequest();
    request.extent = *extent;
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    if (!layout.has_value() || !descriptor.has_value())
    {
        return std::nullopt;
    }
    const std::uint64_t offset =
        static_cast<std::uint64_t>(item.cropY) * whole.layout.rowStrideBytes() +
        static_cast<std::uint64_t>(item.cropX) * whole.layout.samplesPerPixel() * whole.layout.elementSizeBytes();
    return SourceView{*descriptor, *layout, static_cast<const std::uint8_t*>(whole.samples) + offset};
}

/// The presentation a product path asks for.
an::DisplayPresentationRequest requestFor(const fixtures::PresentationCase& item)
{
    an::DisplayPresentationRequest request;
    request.promotion =
        item.highQualityGradient ? an::PresentationPromotion::Gradient : an::PresentationPromotion::Quantized16;
    request.stretch = item.autostretch ? an::PresentationStretch::Automatic : an::PresentationStretch::Linear;
    request.linked = item.linked;
    // The product balances colour only when it is linked, because an unlinked stretch undoes a balance.
    request.neutralize = item.neutralize && item.linked;
    request.calibrate = item.calibrate && item.linked;
    request.saturation = item.saturation;
    request.saturationBackgroundFactor = item.saturationBackground;
    request.saturationBackgroundLocation =
        item.saturationFromMean ? an::SaturationBackgroundLocation::Mean : an::SaturationBackgroundLocation::Median;
    request.brightness = fixtures::presentationBrightnessOffset(item.brightness);
    request.contrast = fixtures::presentationContrastFactor(item.contrast);
    switch (item.product)
    {
    case fixtures::PresentationProduct::Bytes:
        request.output = an::PresentationOutput::Interleaved8;
        break;
    case fixtures::PresentationProduct::Rgba:
        request.output = an::PresentationOutput::Rgba8;
        break;
    case fixtures::PresentationProduct::Native:
        request.output = an::PresentationOutput::WorkingFloat32;
        break;
    }
    return request;
}

/// A working sample in a representation, as the engine converts one.
double nativeValue(float value, fixtures::FixtureSampleType type)
{
    const an::ConversionPolicy policy{};
    switch (type)
    {
    case fixtures::FixtureSampleType::UInt8:
        return an::convertSample<float, std::uint8_t>(value, policy).value;
    case fixtures::FixtureSampleType::Int8:
        return an::convertSample<float, std::int8_t>(value, policy).value;
    case fixtures::FixtureSampleType::UInt16:
        return an::convertSample<float, std::uint16_t>(value, policy).value;
    case fixtures::FixtureSampleType::Int16:
        return an::convertSample<float, std::int16_t>(value, policy).value;
    case fixtures::FixtureSampleType::UInt32:
        return an::convertSample<float, std::uint32_t>(value, policy).value;
    case fixtures::FixtureSampleType::Int32:
        return an::convertSample<float, std::int32_t>(value, policy).value;
    case fixtures::FixtureSampleType::Float32:
        return value;
    case fixtures::FixtureSampleType::Float64:
        return an::convertSample<float, double>(value, policy).value;
    }
    return 0.0;
}

/// What the engine produces for a case, as neutral samples of the record's shape.
an::Result<fixtures::FixtureSamples> presentedByEngine(const an::consumer::EngineSession& session,
                                                       const fixtures::PresentationCase& item,
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
    std::optional<SourceView> source = viewOf(*made);
    if (source.has_value() && item.path == fixtures::PresentationPath::Lens)
    {
        source = regionOf(*source, item);
    }
    // The records read every floating sample as a fraction of full scale. The presentation refuses a floating
    // physical quantity or sensor code, which has none, so a consumer that wants that reading restates the domain
    // on purpose; this does the same.
    const bool floating = image.sampleType == fixtures::FixtureSampleType::Float32 ||
                          image.sampleType == fixtures::FixtureSampleType::Float64;
    if (source.has_value() && floating &&
        (source->descriptor.numericDomain() == an::NumericDomain::PhysicalScaled ||
         source->descriptor.numericDomain() == an::NumericDomain::RawSensorCode))
    {
        an::ImageDescriptorRequest restated = source->descriptor.toRequest();
        restated.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> normalized = an::ImageDescriptor::create(restated);
        source = normalized.has_value()
                     ? std::optional<SourceView>(SourceView{*normalized, source->layout, source->samples})
                     : std::nullopt;
    }
    if (!source.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::StatusDetail{.message = "the input cannot be viewed"});
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::DisplayPresentationRequest request = requestFor(item);
    const an::DisplayPresentationSource view{
        .descriptor = &source->descriptor, .samples = source->samples, .layout = &source->layout};
    const an::Result<an::DisplayPresentationPlan> plan = an::planDisplayPresentation(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const bool preview = item.path == fixtures::PresentationPath::PreviewRgba ||
                         item.path == fixtures::PresentationPath::PreviewSubsampled;
    const an::Result<an::RealtimePreviewDisplayPresentationPlan> previewPlan =
        an::planRealtimePreviewDisplayPresentation(request, item.subsample);
    if (!previewPlan.hasValue())
    {
        return previewPlan.status();
    }
    an::Result<an::ImageLayoutRequest> layoutRequest =
        preview ? an::realtimePreviewDisplayPresentationLayout(*previewPlan, source->descriptor)
                : an::displayPresentationLayout(*plan, source->descriptor);
    if (!layoutRequest.hasValue())
    {
        return layoutRequest.status();
    }
    if (request.output != an::PresentationOutput::WorkingFloat32)
    {
        layoutRequest->rowAlignmentBytes = 1u;
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*layoutRequest, engine.limits());
    if (!layout.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::StatusDetail{.message = "the output cannot be laid out"});
    }
    std::vector<std::uint8_t> output(static_cast<std::size_t>(layout->totalBytes()) + 64u, 0u);
    void* destination = output.data() + (64u - reinterpret_cast<std::uintptr_t>(output.data()) % 64u) % 64u;

    const an::Result<an::DisplayPresentationResult> presented = [&]() -> an::Result<an::DisplayPresentationResult> {
        if (item.path == fixtures::PresentationPath::PreviewRgba)
        {
            // The preview's statistics are the whole source's, taken once by the production analysis.
            const an::Result<an::DisplayPresentationAnalysis> analysis =
                an::analyzeDisplayPresentation(execution, *plan, view);
            if (!analysis.hasValue())
            {
                return analysis.status();
            }
            return an::presentImageRealtimePreview(execution, *previewPlan, view, destination, *layout, &*analysis);
        }
        if (preview)
        {
            return an::presentImageRealtimePreview(execution, *previewPlan, view, destination, *layout);
        }
        return an::presentImage(execution, *plan, view, destination, *layout);
    }();
    if (!presented.hasValue())
    {
        return presented.status();
    }

    const std::uint32_t width = layout->extent().width();
    const std::uint32_t height = layout->extent().height();
    const std::uint32_t channels = layout->extent().channelCount();
    const auto* bytes = static_cast<const std::uint8_t*>(destination);
    if (request.output == an::PresentationOutput::WorkingFloat32)
    {
        fixtures::FixtureSamples result(width, height, channels, image.sampleType);
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto* row = reinterpret_cast<const float*>(bytes + channel * layout->planeStrideBytes() +
                                                                 y * layout->rowStrideBytes());
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    result.setValue(x, y, channel, nativeValue(row[x], image.sampleType));
                }
            }
        }
        return result;
    }
    fixtures::FixtureSamples result(width, height, channels, fixtures::FixtureSampleType::UInt8);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                result.setValue(x, y, channel, bytes[y * layout->rowStrideBytes() + x * channels + channel]);
            }
        }
    }
    return result;
}

/// The largest difference between two results of one shape, for a failure message.
double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return std::numeric_limits<double>::infinity();
    }
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
                largest = (std::max)(largest, std::abs(a - b));
            }
        }
    }
    return largest;
}

class PresentationExpectationTest : public ::testing::Test
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
            fixtures::readExpectationFile(*directory / fixtures::kPresentationExpectationFile, error);
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

// Every recorded product display path, presented through the engine from the same input and held to the record by
// its classification: within a code, within a stated bound, or accepted for the reason it states.
TEST_F(PresentationExpectationTest, EveryPathMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::PresentationCase* item = fixtures::presentationCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(item->input);
        ASSERT_NE(image, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> presented = presentedByEngine(*m_session, *item, *image, *samples);
        ASSERT_TRUE(presented.hasValue()) << record.id << ": " << presented.status().message();
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *presented);
        const double largest = largestDifference(record.result, *presented);
        // The largest difference of every record, accepted ones included, goes into the run's report.
        RecordProperty(record.id, std::to_string(largest));
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largest;
    }
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(PresentationExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_TRUE(ids.insert(record.id).second) << record.id;
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
    for (const fixtures::PresentationCase& item : fixtures::presentationCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
