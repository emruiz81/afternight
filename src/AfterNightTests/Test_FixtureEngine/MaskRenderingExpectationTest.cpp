// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's mask rendering and mask drawing through the engine, held to their reference records.
//
// Each overlay case renders a neutral mask with the engine's mask overlay, and each drawing case draws a stroke with
// the engine's brush, stamp and layer blend, or feathers a shape's coverage and screens it over the paint. The answer
// is restated in the record's shape - the layer's colour planes for a replace or a multiply, the layer's four planes
// for a one-channel tint - and compared by the record's classification. The records read every mask as a fraction of
// full scale whatever it holds, so a mask or image stating a sensor code or a physical quantity is restated as
// normalized, which is what they assume.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/MaskRenderingCases.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Masks/MaskOverlay.h>
#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>
#include <AfterNightEngine/Algorithms/Spatial/DrawingPrimitives.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

/// What an overlay reads from an image the adapter made, its domain restated as the viewer read it.
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
            an::ImageDescriptorRequest request = descriptor->toRequest();
            if (request.numericDomain != an::NumericDomain::NormalizedLinear &&
                request.numericDomain != an::NumericDomain::EncodedNonlinear)
            {
                request.numericDomain = an::NumericDomain::NormalizedLinear;
            }
            const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(request);
            if (!restated.has_value())
            {
                return std::nullopt;
            }
            return SourceView{*restated, view->layout(), view->data()};
        },
        image);
}

an::Status failure(const std::string& message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, an::StatusDetail{.message = message});
}

/// An image the adapter made, kept alive beside the view of it.
struct Made
{
    std::optional<adapter::EngineImage> image;
    std::optional<SourceView> view;
};

an::Result<Made> make(const an::EngineContext& engine, const std::string& name)
{
    const fixtures::FixtureImage* fixture = fixtures::fixtureNamed(name);
    if (fixture == nullptr)
    {
        return failure("no fixture is named " + name);
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*fixture, error);
    if (!samples.has_value())
    {
        return failure(error);
    }
    an::Result<adapter::EngineImage> image = adapter::makeEngineImage(engine, *fixture, *samples);
    if (!image.hasValue())
    {
        return image.status();
    }
    Made made;
    made.image.emplace(std::move(*image));
    made.view = viewOf(*made.image);
    if (!made.view.has_value())
    {
        return failure(name + " cannot be viewed");
    }
    return made;
}

/// The layer the engine writes for an overlay case, as four planes of bytes in red, green, blue, alpha order.
an::Result<fixtures::FixtureSamples> overlaidByEngine(const an::consumer::EngineSession& session,
                                                      const fixtures::MaskOverlayCase& item)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<Made> mask = make(engine, item.mask);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    std::optional<Made> image;
    if (!item.image.empty())
    {
        an::Result<Made> made = make(engine, item.image);
        if (!made.hasValue())
        {
            return made.status();
        }
        image.emplace(std::move(*made));
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    an::MaskOverlayRequest request;
    request.mode = item.mode == fixtures::MaskOverlayCaseMode::Tint      ? an::MaskOverlayMode::Tint
                   : item.mode == fixtures::MaskOverlayCaseMode::Replace ? an::MaskOverlayMode::Replace
                                                                         : an::MaskOverlayMode::Multiply;
    request.tint = an::MaskOverlayColor{.red = item.red, .green = item.green, .blue = item.blue, .alpha = 255u};
    request.invertMask = item.invert;
    const an::Result<an::MaskOverlayPlan> plan = an::planMaskOverlay(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const SourceView& maskView = *mask->view;
    const an::Result<an::ImageLayoutRequest> layoutRequest = an::maskOverlayLayout(*plan, maskView.descriptor);
    if (!layoutRequest.hasValue())
    {
        return layoutRequest.status();
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*layoutRequest, an::defaultEngineLimits());
    if (!layout.has_value())
    {
        return failure("the overlay's layout cannot be built");
    }
    std::vector<std::uint8_t> bytes(layout->totalBytes(), 0u);
    const an::MaskOverlaySource maskSource{
        .descriptor = &maskView.descriptor, .samples = maskView.samples, .layout = &maskView.layout};
    std::optional<an::MaskOverlaySource> imageSource;
    if (image.has_value())
    {
        imageSource = an::MaskOverlaySource{
            .descriptor = &image->view->descriptor, .samples = image->view->samples, .layout = &image->view->layout};
    }
    const an::Result<an::MaskOverlayResult> rendered = an::renderMaskOverlay(
        execution, *plan, maskSource, imageSource.has_value() ? &*imageSource : nullptr, bytes.data(), *layout);
    if (!rendered.hasValue())
    {
        return rendered.status();
    }
    const std::uint32_t width = layout->extent().width();
    const std::uint32_t height = layout->extent().height();
    fixtures::FixtureSamples result(width, height, 4u, fixtures::FixtureSampleType::UInt8);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            for (std::uint32_t c = 0u; c < 4u; ++c)
            {
                result.setValue(x, y, c, bytes[static_cast<std::size_t>(y * layout->rowStrideBytes() + x * 4u + c)]);
            }
        }
    }
    return result;
}

/// The first `channels` planes of a layer.
fixtures::FixtureSamples firstPlanes(const fixtures::FixtureSamples& layer, std::uint32_t channels)
{
    fixtures::FixtureSamples planes(layer.width(), layer.height(), channels, layer.sampleType());
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        const std::span<const std::byte> from = layer.plane(c);
        std::memcpy(planes.mutablePlane(c).data(), from.data(), from.size());
    }
    return planes;
}

/// What the engine draws for a drawing case, as one Float32 plane.
an::Result<fixtures::FixtureSamples> drawnByEngine(const an::consumer::EngineSession& session,
                                                   const fixtures::DrawingCase& item)
{
    const fixtures::FixtureImage* fixture = fixtures::fixtureNamed(item.input);
    if (fixture == nullptr)
    {
        return failure("no fixture is named " + item.input);
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> input = fixtures::materializeFixture(*fixture, error);
    if (!input.has_value())
    {
        return failure(error);
    }
    const std::uint32_t width = input->width();
    const std::uint32_t height = input->height();
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * sizeof(float);
    const auto plane = [&](std::vector<float>& samples) {
        return *an::PlaneView<float>::create(samples, width, height, 1u, rowBytes);
    };
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    std::vector<float> base;
    std::vector<float> source(static_cast<std::size_t>(width) * height, 0.0f);
    std::vector<float> drawn(source.size(), 0.0f);
    an::LayerBlendRequest blend{.opacity = item.opacity};
    if (item.kind == fixtures::DrawingCaseKind::Stroke)
    {
        base = fixtures::importedPlane(*input);
        const an::Result<an::BrushPlan> brush =
            an::planBrush(an::BrushRequest{.diameter = item.diameter,
                                           .hardness = item.hardness,
                                           .shape = item.square ? an::BrushTipShape::Square : an::BrushTipShape::Round,
                                           .spacing = item.spacing,
                                           .flow = item.flow});
        if (!brush.hasValue())
        {
            return brush.status();
        }
        std::vector<float> tip(static_cast<std::size_t>(brush->tipSide()) * brush->tipSide(), 0.0f);
        const an::Status written = an::writeBrushTip(
            *brush, *an::PlaneView<float>::create(tip, brush->tipSide(), brush->tipSide(), 1u,
                                                  static_cast<std::uint64_t>(brush->tipSide()) * sizeof(float)));
        if (written.failed())
        {
            return written;
        }
        const an::PlaneView<const float> tipView = *an::PlaneView<const float>::create(
            tip, brush->tipSide(), brush->tipSide(), 1u, static_cast<std::uint64_t>(brush->tipSide()) * sizeof(float));
        for (std::size_t index = 0u; index < item.points.size(); ++index)
        {
            const fixtures::DrawingStrokePoint& point = item.points[index];
            const an::BrushPoint end{.x = point.x, .y = point.y};
            an::BrushSegment segment = an::makeBrushSegment(end, end, end);
            if (index > 0u)
            {
                const fixtures::DrawingStrokePoint& previous = item.points[index - 1u];
                const fixtures::DrawingStrokePoint& before = item.points[index >= 2u ? index - 2u : index - 1u];
                segment = an::makeBrushSegment({.x = before.x, .y = before.y}, {.x = previous.x, .y = previous.y}, end);
            }
            const an::Result<std::uint32_t> count = an::countBrushSegmentDabs(*brush, segment);
            if (!count.hasValue())
            {
                return count.status();
            }
            std::vector<an::BrushDab> dabs(*count);
            const an::Result<std::uint32_t> placed = an::placeBrushSegmentDabs(*brush, segment, dabs);
            if (!placed.hasValue())
            {
                return placed.status();
            }
            const an::Result<an::BrushStampResult> stamped =
                an::stampBrushDabs(execution, *brush, tipView, dabs, point.pressure, plane(source));
            if (!stamped.hasValue())
            {
                return stamped.status();
            }
        }
        blend.mode = item.mode == fixtures::DrawingBlendMode::Multiply  ? an::LayerBlendMode::Multiply
                     : item.mode == fixtures::DrawingBlendMode::Screen  ? an::LayerBlendMode::Screen
                     : item.mode == fixtures::DrawingBlendMode::Overlay ? an::LayerBlendMode::Overlay
                     : item.mode == fixtures::DrawingBlendMode::Erase   ? an::LayerBlendMode::Erase
                                                                        : an::LayerBlendMode::Normal;
    }
    else
    {
        const fixtures::FixtureImage* paint = fixtures::fixtureNamed(item.base);
        const std::optional<fixtures::FixtureSamples> paintSamples =
            paint == nullptr ? std::nullopt : fixtures::materializeFixture(*paint, error);
        if (!paintSamples.has_value())
        {
            return failure("the paint plane " + item.base + " cannot be made");
        }
        base = fixtures::importedPlane(*paintSamples);
        std::vector<float> coverage = fixtures::shapeCoverage(*input);
        const an::Result<an::CoverageFeatherPlan> feather =
            an::planCoverageFeather(an::CoverageFeatherRequest{.feather = item.feather});
        if (!feather.hasValue())
        {
            return feather.status();
        }
        const an::Result<an::CoverageFeatherResult> feathered =
            an::featherCoverage(execution, *feather, plane(coverage), plane(source));
        if (!feathered.hasValue())
        {
            return feathered.status();
        }
        blend.mode = an::LayerBlendMode::Screen;
    }
    const an::Result<an::LayerBlendPlan> blendPlan = an::planLayerBlend(blend);
    if (!blendPlan.hasValue())
    {
        return blendPlan.status();
    }
    const an::Result<an::LayerBlendResult> blended =
        an::blendLayerPlane(execution, *blendPlan, plane(base), plane(source), plane(drawn));
    if (!blended.hasValue())
    {
        return blended.status();
    }
    fixtures::FixtureSamples result(width, height, 1u, fixtures::FixtureSampleType::Float32);
    std::memcpy(result.mutableBytes().data(), drawn.data(), drawn.size() * sizeof(float));
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

class MaskRenderingExpectationTest : public ::testing::Test
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
        const std::optional<std::vector<fixtures::Expectation>> overlays =
            fixtures::readExpectationFile(*directory / fixtures::kMaskOverlayExpectationFile, error);
        ASSERT_TRUE(overlays.has_value()) << error;
        m_overlays = *overlays;
        const std::optional<std::vector<fixtures::Expectation>> drawings =
            fixtures::readExpectationFile(*directory / fixtures::kDrawingExpectationFile, error);
        ASSERT_TRUE(drawings.has_value()) << error;
        m_drawings = *drawings;
    }

    void TearDown() override
    {
        m_session.reset();
    }

    std::optional<an::consumer::EngineSession> m_session;
    std::vector<fixtures::Expectation> m_overlays;
    std::vector<fixtures::Expectation> m_drawings;
};

} // namespace

// Every recorded overlay, rendered by the engine from the same inputs and held to the record by its
// classification. A colour mask's record is three tinted layers the toolkit stacks; the engine's one layer is
// held to their exact stack within the case's bound.
TEST_F(MaskRenderingExpectationTest, EveryOverlayMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_overlays.empty());
    for (const fixtures::Expectation& record : m_overlays)
    {
        const fixtures::MaskOverlayCase* item = fixtures::maskOverlayCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* mask = fixtures::fixtureNamed(item->mask);
        ASSERT_NE(mask, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*mask, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *mask, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> layer = overlaidByEngine(*m_session, *item);
        ASSERT_TRUE(layer.hasValue()) << record.id << ": " << layer.status().message();

        // A replace or a multiply is opaque, and a grey one is grey.
        if (item->mode != fixtures::MaskOverlayCaseMode::Tint)
        {
            for (std::uint32_t y = 0u; y < layer->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < layer->width(); ++x)
                {
                    ASSERT_EQ(layer->value(x, y, 3u), 255.0) << record.id;
                    if (record.result.channelCount() == 1u)
                    {
                        ASSERT_EQ(layer->value(x, y, 1u), layer->value(x, y, 0u)) << record.id;
                        ASSERT_EQ(layer->value(x, y, 2u), layer->value(x, y, 0u)) << record.id;
                    }
                }
            }
        }
        if (item->mode == fixtures::MaskOverlayCaseMode::Tint && record.result.channelCount() > 4u)
        {
            // The toolkit stacks one layer per channel; the engine's one layer is that stack.
            double largest = 0.0;
            for (std::uint32_t y = 0u; y < layer->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < layer->width(); ++x)
                {
                    double clear = 1.0;
                    for (std::uint32_t at = 3u; at < record.result.channelCount(); at += 4u)
                    {
                        clear *= 1.0 - record.result.value(x, y, at) / 255.0;
                    }
                    largest = (std::max)(largest, std::abs((1.0 - clear) * 255.0 - layer->value(x, y, 3u)));
                    ASSERT_EQ(layer->value(x, y, 0u), item->red) << record.id;
                    ASSERT_EQ(layer->value(x, y, 1u), item->green) << record.id;
                    ASSERT_EQ(layer->value(x, y, 2u), item->blue) << record.id;
                }
            }
            RecordProperty(record.id, std::to_string(largest));
            EXPECT_LE(largest, item->acceptedBound) << record.id;
            continue;
        }
        const fixtures::FixtureSamples candidate = item->mode == fixtures::MaskOverlayCaseMode::Tint
                                                       ? *layer
                                                       : firstPlanes(*layer, record.result.channelCount());
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, candidate);
        const double largest = largestDifference(record.result, candidate);
        RecordProperty(record.id, std::to_string(largest));
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largest;
        if (item->classification == fixtures::ExpectationClass::Accepted && item->acceptedBound > 0.0)
        {
            EXPECT_LE(largest, item->acceptedBound) << record.id;
        }
    }
}

// Every recorded drawing, drawn by the engine from the same inputs and held to the record by its classification; an
// accepted one is held to its case's bound.
TEST_F(MaskRenderingExpectationTest, EveryDrawingMatchesItsRecordByItsClassification)
{
    ASSERT_FALSE(m_drawings.empty());
    for (const fixtures::Expectation& record : m_drawings)
    {
        const fixtures::DrawingCase* item = fixtures::drawingCase(record.id);
        ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
        const fixtures::FixtureImage* input = fixtures::fixtureNamed(item->input);
        ASSERT_NE(input, nullptr) << record.id;
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*input, error);
        ASSERT_TRUE(samples.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *input, *samples))
            << record.id << " was captured from another input than this one";
        const an::Result<fixtures::FixtureSamples> drawn = drawnByEngine(*m_session, *item);
        ASSERT_TRUE(drawn.hasValue()) << record.id << ": " << drawn.status().message();
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *drawn);
        const double largest = largestDifference(record.result, *drawn);
        RecordProperty(record.id, std::to_string(largest));
        EXPECT_TRUE(comparison.matched) << record.id << ": " << comparison.reason << "; the largest difference is "
                                        << largest;
        if (item->classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_LE(largest, item->acceptedBound) << record.id;
        }
    }
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(MaskRenderingExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const std::vector<fixtures::Expectation>* records : {&m_overlays, &m_drawings})
    {
        for (const fixtures::Expectation& record : *records)
        {
            EXPECT_TRUE(ids.insert(record.id).second) << record.id;
            EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
            EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
        }
    }
    for (const fixtures::MaskOverlayCase& item : fixtures::maskOverlayCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
    for (const fixtures::DrawingCase& item : fixtures::drawingCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
