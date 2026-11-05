// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's stacking and drizzle numerics through the engine, held to their reference records.
//
// Each case states one computation over neutral inputs. This runs it with the engine's operations - the comet
// composite's split, mask and blend, the rejection-map counts and cluster analysis, the drizzle canvas over the frames'
// mapped rectangles, and the exposure and signal-to-noise weights - and compares with the record by its
// classification. Positions the case states in engine pixel coordinates go to the engine as they are; a frame's
// registration, which the record holds integer-centred, is conjugated once at the boundary.

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/StackingNumericsCases.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/CometComposite.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

an::Status failed(const std::string& message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, an::StatusDetail{.message = message});
}

/// An image the adapter made, as a composite operation reads it.
struct SourceView
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    const void* samples = nullptr;

    [[nodiscard]] an::CometCompositeSource source() const noexcept
    {
        return an::CometCompositeSource{.descriptor = &descriptor, .samples = samples, .layout = &layout};
    }
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

/// An owned planar Float32 image of the stated extent.
struct Float32Plane
{
    std::optional<an::ImageLayout> layout;
    std::vector<float> samples;

    Float32Plane(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
        layout = an::ImageLayout::create({.extent = *extent,
                                          .elementSizeBytes = 4u,
                                          .samplesPerPixel = 1u,
                                          .planeCount = channels,
                                          .rowAlignmentBytes = 4u},
                                         an::defaultEngineLimits());
        samples.assign(static_cast<std::size_t>(layout->totalBytes() / sizeof(float)) + 1u, 0.0f);
    }

    [[nodiscard]] fixtures::FixtureSamples toFixture() const
    {
        const an::ImageExtent& extent = layout->extent();
        fixtures::FixtureSamples result(extent.width(), extent.height(), extent.channelCount(),
                                        fixtures::FixtureSampleType::Float32);
        for (std::uint32_t c = 0u; c < extent.channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < extent.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < extent.width(); ++x)
                {
                    result.setValue(
                        x, y, c,
                        samples[static_cast<std::size_t>(*layout->sampleByteOffset(c, y, x, 0u) / sizeof(float))]);
                }
            }
        }
        return result;
    }

    /// The plane as a one-channel Float32 descriptor.
    [[nodiscard]] an::ImageDescriptor descriptor() const
    {
        return *an::ImageDescriptor::create({.extent = layout->extent(),
                                             .sampleType = an::SampleType::Float32,
                                             .channelSchema = an::ChannelSchema::Mono,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0});
    }
};

fixtures::FixtureSamples row(const std::vector<double>& values)
{
    fixtures::FixtureSamples samples(static_cast<std::uint32_t>(values.size()), 1u, 1u,
                                     fixtures::FixtureSampleType::Float64);
    for (std::uint32_t index = 0u; index < values.size(); ++index)
    {
        samples.setValue(index, 0u, 0u, values[index]);
    }
    return samples;
}

/// A frame's registration: the record's integer-centred rotation and translation, conjugated so that it maps
/// half-centred positions as the record's mapped integer-centred ones.
an::Result<an::RegistrationTransform> engineTransform(double dx, double dy, double degrees)
{
    const double radians = degrees * std::numbers::pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double tx = dx + 0.5 - (cosine * 0.5 - sine * 0.5);
    const double ty = dy + 0.5 - (sine * 0.5 + cosine * 0.5);
    return an::RegistrationTransform::create(
        {.linearKind =
             degrees == 0.0 ? an::RegistrationTransformKind::Similarity : an::RegistrationTransformKind::Affine,
         .sourceToReference = {cosine, -sine, tx, sine, cosine, ty, 0.0, 0.0, 1.0}});
}

/// Every aperture of a mask or blend case, projected and made a circle on the plane.
an::Result<std::vector<an::CometApertureCircle>> circlesOf(const an::ExecutionContext& execution,
                                                           const fixtures::StackingNumericsCase& item,
                                                           std::uint32_t planeWidth, std::uint32_t planeHeight)
{
    const auto source = [&](std::uint32_t pixels) {
        return static_cast<std::uint32_t>((std::max)(1L, std::lround(static_cast<double>(pixels) / item.scale)));
    };
    std::vector<an::CometApertureCircle> circles;
    for (const fixtures::CometCaseAperture& aperture : item.apertures)
    {
        const an::Result<an::RegistrationTransform> transform = engineTransform(aperture.dx, aperture.dy, 0.0);
        if (!transform.hasValue())
        {
            return transform.status();
        }
        const an::Result<an::CometApertureProjection> projection =
            an::projectCometAperture(execution, {.sourceCenter = {.x = aperture.x, .y = aperture.y},
                                                 .sourceRadiusPixels = aperture.radius,
                                                 .sourceWidth = source(planeWidth),
                                                 .sourceHeight = source(planeHeight),
                                                 .outputWidth = source(planeWidth),
                                                 .outputHeight = source(planeHeight),
                                                 .boundarySamples = 48u,
                                                 .sourceToReference = *transform});
        if (!projection.hasValue())
        {
            return projection.status();
        }
        const an::Result<an::CometApertureCircle> circle =
            an::cometApertureCircleOf(*projection, aperture.radius, item.scale);
        if (!circle.hasValue())
        {
            return circle.status();
        }
        circles.push_back(*circle);
    }
    return circles;
}

/// The mask of a mask or blend case, drawn on a plane of the stated extent.
an::Result<Float32Plane> maskOf(const an::ExecutionContext& execution, const fixtures::StackingNumericsCase& item,
                                std::uint32_t width, std::uint32_t height)
{
    const an::Result<std::vector<an::CometApertureCircle>> circles = circlesOf(execution, item, width, height);
    if (!circles.hasValue())
    {
        return circles.status();
    }
    Float32Plane mask(width, height, 1u);
    const an::Result<an::CometMaskResult> drawn =
        an::renderCometMask(execution, *circles, {}, mask.samples.data(), *mask.layout);
    if (!drawn.hasValue())
    {
        return drawn.status();
    }
    return mask;
}

/// The packed counts of a map input.
std::vector<std::uint32_t> countsOf(const fixtures::FixtureSamples& samples)
{
    std::vector<std::uint32_t> counts;
    for (std::uint32_t c = 0u; c < samples.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                counts.push_back(static_cast<std::uint32_t>(samples.value(x, y, c)));
            }
        }
    }
    return counts;
}

an::Result<fixtures::FixtureSamples> producedByEngine(const an::consumer::EngineSession& session,
                                                      const fixtures::StackingNumericsCase& item)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const fixtures::FixtureImage* first = fixtures::stackingNumericsInput(item.input);
    std::string error;
    const std::optional<fixtures::FixtureSamples> firstSamples =
        first == nullptr ? std::nullopt : fixtures::materializeFixture(*first, error);
    if (!firstSamples.has_value())
    {
        return failed("the input cannot be made: " + error);
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    switch (item.kind)
    {
    case fixtures::StackingNumericsCaseKind::CometSplit: {
        const an::Result<adapter::EngineImage> frame = adapter::makeEngineImage(engine, *first, *firstSamples);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        const std::optional<SourceView> view = viewOf(*frame);
        if (!view.has_value())
        {
            return failed("the frame cannot be viewed");
        }
        const fixtures::CometCaseAperture& aperture = item.apertures.front();
        const an::Result<an::CometApertureSplitPlan> plan = an::planCometApertureSplit(
            {.aperture = {.center = {.x = aperture.x, .y = aperture.y}, .radius = aperture.radius}});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const an::ImageExtent& extent = view->descriptor.extent();
        Float32Plane layer(extent.width(), extent.height(), extent.channelCount());
        const an::Result<an::CometApertureSplitResult> split =
            an::splitCometAperture(execution, *plan, view->source(), item.cometLayer ? nullptr : layer.samples.data(),
                                   &*layer.layout, item.cometLayer ? layer.samples.data() : nullptr, &*layer.layout);
        if (!split.hasValue())
        {
            return split.status();
        }
        return layer.toFixture();
    }
    case fixtures::StackingNumericsCaseKind::CometMask: {
        const an::Result<Float32Plane> mask = maskOf(execution, item, first->width, first->height);
        if (!mask.hasValue())
        {
            return mask.status();
        }
        return mask->toFixture();
    }
    case fixtures::StackingNumericsCaseKind::CometBlend: {
        const fixtures::FixtureImage* second = fixtures::stackingNumericsInput(item.second);
        const std::optional<fixtures::FixtureSamples> secondSamples =
            second == nullptr ? std::nullopt : fixtures::materializeFixture(*second, error);
        if (!secondSamples.has_value())
        {
            return failed("the comet stack cannot be made");
        }
        const an::Result<adapter::EngineImage> star = adapter::makeEngineImage(engine, *first, *firstSamples);
        const an::Result<adapter::EngineImage> comet = adapter::makeEngineImage(engine, *second, *secondSamples);
        if (!star.hasValue() || !comet.hasValue())
        {
            return star.hasValue() ? comet.status() : star.status();
        }
        const std::optional<SourceView> starView = viewOf(*star);
        const std::optional<SourceView> cometView = viewOf(*comet);
        const an::Result<Float32Plane> mask = maskOf(execution, item, first->width, first->height);
        if (!starView.has_value() || !cometView.has_value() || !mask.hasValue())
        {
            return mask.hasValue() ? failed("a stack cannot be viewed") : mask.status();
        }
        const an::Result<an::RegistrationTransform> cometTransform = engineTransform(item.cometDx, item.cometDy, 0.0);
        if (!cometTransform.hasValue())
        {
            return cometTransform.status();
        }
        const an::Result<an::CometCompositeBlendPlan> plan =
            an::planCometCompositeBlend({.cometTransform = *cometTransform, .scale = item.scale});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const an::ImageDescriptor maskDescriptor = mask->descriptor();
        const an::CometCompositeSource maskSource{
            .descriptor = &maskDescriptor, .samples = mask->samples.data(), .layout = &*mask->layout};
        Float32Plane composite(first->width, first->height, starView->descriptor.extent().channelCount());
        const an::Result<an::CometCompositeBlendResult> blended =
            an::blendCometComposite(execution, *plan, starView->source(), cometView->source(), maskSource,
                                    composite.samples.data(), *composite.layout);
        if (!blended.hasValue())
        {
            return blended.status();
        }
        return composite.toFixture();
    }
    case fixtures::StackingNumericsCaseKind::RejectionMaps: {
        const fixtures::FixtureImage* second = fixtures::stackingNumericsInput(item.second);
        const std::optional<fixtures::FixtureSamples> highSamples =
            second == nullptr ? std::nullopt : fixtures::materializeFixture(*second, error);
        if (!highSamples.has_value())
        {
            return failed("the high map cannot be made");
        }
        const std::vector<std::uint32_t> low = countsOf(*firstSamples);
        const std::vector<std::uint32_t> high = countsOf(*highSamples);
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
            firstSamples->width(), firstSamples->height(), firstSamples->channelCount(), an::defaultEngineLimits());
        const an::Result<an::RejectionMapCounts> counted =
            an::countRejectionMaps(execution, low, high, *extent, {.minimumRejections = item.minimum});
        if (!counted.hasValue())
        {
            return counted.status();
        }
        std::vector<an::RejectionClusterDetection> clusters(32u);
        const an::Result<an::RejectionClusterReport> lowClusters = an::analyzeRejectionClusters(
            execution, low, *extent, {.source = an::RejectionClusterSource::Low, .minimumRejections = item.minimum},
            clusters);
        const an::Result<an::RejectionClusterReport> highClusters = an::analyzeRejectionClusters(
            execution, high, *extent, {.source = an::RejectionClusterSource::High, .minimumRejections = item.minimum},
            clusters);
        if (!lowClusters.hasValue() || !highClusters.hasValue())
        {
            return lowClusters.hasValue() ? highClusters.status() : lowClusters.status();
        }
        return row(
            {static_cast<double>(counted->lowElements), static_cast<double>(counted->highElements),
             static_cast<double>(counted->eitherElements), counted->eitherFraction,
             static_cast<double>(lowClusters->clustersReported + highClusters->clustersReported),
             static_cast<double>((std::max)(lowClusters->largestClusterPixels, highClusters->largestClusterPixels)),
             (std::max)(lowClusters->largestClusterFraction, highClusters->largestClusterFraction)});
    }
    case fixtures::StackingNumericsCaseKind::DrizzleCanvas: {
        an::Result<an::DrizzleCanvas> canvas = failed("no canvas was planned");
        if (item.plane.has_value())
        {
            const std::array<double, 4>& plane = *item.plane;
            canvas = an::planDrizzleCanvasOnPlane({.minimumX = plane[0],
                                                   .minimumY = plane[1],
                                                   .maximumX = plane[0] + plane[2],
                                                   .maximumY = plane[1] + plane[3]},
                                                  item.scale);
        }
        else
        {
            std::vector<an::RegistrationBounds> bounds;
            for (const fixtures::CanvasCaseFrame& frame : item.canvasFrames)
            {
                const an::Result<an::RegistrationTransform> transform =
                    engineTransform(frame.dx, frame.dy, frame.degrees);
                if (!transform.hasValue())
                {
                    return transform.status();
                }
                const an::Result<an::RegistrationBounds> mapped =
                    an::computeRegistrationBounds(*transform, first->width, first->height);
                if (!mapped.hasValue())
                {
                    return mapped.status();
                }
                bounds.push_back(*mapped);
            }
            const an::RegistrationCanvasMode mode = item.mode == "mosaic" ? an::RegistrationCanvasMode::Union
                                                    : item.mode == "intersection"
                                                        ? an::RegistrationCanvasMode::Intersection
                                                        : an::RegistrationCanvasMode::Reference;
            canvas = an::planDrizzleCanvas(bounds, item.referenceIndex,
                                           {.mode = mode, .scale = item.scale, .pixelFraction = item.pixelFraction});
        }
        if (!canvas.hasValue())
        {
            return canvas.status();
        }
        return row({canvas->boundingBox.minimumX, canvas->boundingBox.minimumY, canvas->boundingBox.width(),
                    canvas->boundingBox.height(), static_cast<double>(canvas->width),
                    static_cast<double>(canvas->height), static_cast<double>(canvas->edgeGuard),
                    static_cast<double>(canvas->analysisWidth), static_cast<double>(canvas->analysisHeight)});
    }
    case fixtures::StackingNumericsCaseKind::Weighting: {
        const an::Result<an::ImageDescriptor> descriptor = adapter::describeEngineImage(engine, *first);
        if (!descriptor.hasValue())
        {
            return descriptor.status();
        }
        an::StackRequest request{
            .input = an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(item.figures.size()),
                                               .descriptor = *descriptor}};
        request.weighting.method =
            item.weighting == "exposure" ? an::StackWeighting::Exposure : an::StackWeighting::SignalToNoise;
        const std::optional<an::StackModel> model = an::StackModel::create(request);
        if (!model.has_value())
        {
            return failed("the stack model is refused");
        }
        std::vector<an::StackFrameMeasurement> measurements(item.figures.size());
        for (std::size_t frame = 0u; frame < item.figures.size(); ++frame)
        {
            if (item.weighting == "exposure")
            {
                measurements[frame].exposureSeconds = item.figures[frame];
            }
            else
            {
                measurements[frame].signalToNoise = item.figures[frame];
            }
        }
        std::vector<double> weights(item.figures.size(), 0.0);
        const an::Result<an::StackFrameWeightingResult> computed =
            an::computeStackFrameWeights(execution, *model, measurements, weights);
        if (!computed.hasValue())
        {
            return computed.status();
        }
        return row(weights);
    }
    }
    return failed("the case names no computation");
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

class StackingNumericsExpectationTest : public ::testing::Test
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
        for (const char* file : {fixtures::kCometCompositeExpectationFile, fixtures::kStackingNumericsExpectationFile})
        {
            const std::optional<std::vector<fixtures::Expectation>> records =
                fixtures::readExpectationFile(*directory / file, error);
            ASSERT_TRUE(records.has_value()) << error;
            m_records.insert(m_records.end(), records->begin(), records->end());
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

// Every recorded computation, run through the engine from the same inputs and held to the record by its
// classification; an accepted difference is held to the bound its case states.
TEST_F(StackingNumericsExpectationTest, EveryComputationMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_records.empty());
    for (const fixtures::Expectation& record : m_records)
    {
        const fixtures::StackingNumericsCase* item = fixtures::stackingNumericsCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* image = fixtures::stackingNumericsInput(item->input);
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
TEST_F(StackingNumericsExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const fixtures::Expectation& record : m_records)
    {
        EXPECT_TRUE(ids.insert(record.id).second) << record.id;
        EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
        EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
    }
    for (const fixtures::StackingNumericsCase& item : fixtures::stackingNumericsCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
