// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The two detail filters' dialogs, the high pass and the unsharp mask. The high pass blurs a plane every sample of
/// which is valid by the engine's compiled Gaussian, as the Gaussian blur's own dialog does, so its Apply is the source
/// less that dialog's answer, raised to mid-grey and held to full scale; the unsharp mask, whose blur mirrors the edge
/// sample, keeps the engine's detail filter. Neither refuses a frame wider than a band of its blur fits the stencil's
/// scratch. An interactive frame runs over the target reduced by the preview view's factor at the radius reduced with
/// it, and beyond one kernel's reach blurs by the real-time preview's boxes; the settled frame a change rests on is the
/// Apply's answer, sample for sample.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "processes/GaussianBlurProcess.h"
#include "processes/HighPassFilterProcess.h"
#include "processes/UnsharpMaskProcess.h"
#include "session/ImageDocument.h"
#include "session/PreviewProxies.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>

#include <QCheckBox>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;

constexpr std::uint32_t kSceneWidth = 640u;
constexpr std::uint32_t kSceneHeight = 480u;

/// One sample of a grey scene: stars on a rippled sky, a grain no two neighbours share and a hot pixel in about a
/// thousand, so a high pass finds detail of every size and the clamp to full scale has something to hold.
[[nodiscard]] float sceneSample(std::uint32_t x, std::uint32_t y)
{
    double value = 0.2 + 0.08 * std::sin(static_cast<double>(x) / 7.0) * std::cos(static_cast<double>(y) / 5.0);
    for (std::uint32_t star = 0u; star < 60u; ++star)
    {
        const double dx = static_cast<double>(x) - static_cast<double>((star * 97u) % kSceneWidth);
        const double dy = static_cast<double>(y) - static_cast<double>((star * 61u) % kSceneHeight);
        value += 0.5 * std::exp(-(dx * dx + dy * dy) / 6.0);
    }
    const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u);
    value += 0.02 * (static_cast<double>(hash % 1000u) / 1000.0 - 0.5);
    if (hash % 997u == 0u)
    {
        value = 0.95;
    }
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

/// The scene as a grey Float32 document.
[[nodiscard]] std::optional<an::ui::ImageDocument> sceneDocument(const an::consumer::EngineSession& session)
{
    return an::tests::viewer::makeDocument<float>(
        session, kSceneWidth, kSceneHeight, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return sceneSample(x, y); });
}

/// A grey Float32 document of one value everywhere.
[[nodiscard]] std::optional<an::ui::ImageDocument> flatDocument(const an::consumer::EngineSession& session,
                                                                std::uint32_t width, std::uint32_t height, float value)
{
    return an::tests::viewer::makeDocument<float>(
        session, width, height, an::tests::viewer::Channels::Grey,
        [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
}

/// The samples of a run's result; fails the case when there are none.
[[nodiscard]] std::optional<fixtures::FixtureSamples> samplesOf(const an::Result<an::consumer::ImageAsset>& result)
{
    if (!result.hasValue())
    {
        ADD_FAILURE() << result.status().message();
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readAssetSamples(*result, error);
    if (!samples.has_value())
    {
        ADD_FAILURE() << error;
    }
    return samples;
}

/// What a window's body makes of a document under a run, with the parameters the window holds now.
[[nodiscard]] std::optional<fixtures::FixtureSamples> bodyOver(const ProbedWindow& window,
                                                               const an::consumer::EngineSession& session,
                                                               const an::ui::ImageDocument& document,
                                                               const an::ui::ProcessRun& run)
{
    return samplesOf(runProbeBody(*window.probe, session, document, run));
}

/// An Apply's run.
[[nodiscard]] an::ui::ProcessRun applyRun()
{
    return an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply};
}

/// A preview frame's run of a kind, over a source that stands for a factor of the target's pixels each way.
[[nodiscard]] an::ui::ProcessRun previewRun(an::ui::PreviewFrameKind kind, std::uint32_t factor)
{
    an::ui::ProcessRun run{.kind = an::ui::ProcessRunKind::Preview, .frameKind = kind};
    run.proxyFactor = factor;
    return run;
}

/// A grey plane's samples, a row after another.
[[nodiscard]] std::vector<float> planeOf(const fixtures::FixtureSamples& samples)
{
    std::vector<float> plane(static_cast<std::size_t>(samples.width()) * samples.height());
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            plane[static_cast<std::size_t>(y) * samples.width() + x] = static_cast<float>(samples.value(x, y, 0u));
        }
    }
    return plane;
}

/// The layout of a grey Float32 plane whose rows are packed tight.
[[nodiscard]] an::ImageLayout tightPlane(std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    const std::uint64_t row = static_cast<std::uint64_t>(width) * sizeof(float);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(an::ImageLayoutRequest{.extent = *extent,
                                                                  .elementSizeBytes = sizeof(float),
                                                                  .samplesPerPixel = 1u,
                                                                  .planeCount = 1u,
                                                                  .rowAlignmentBytes = 1u},
                                           row, row * height, an::defaultEngineLimits());
    return *layout;
}

/// Holds a grey image to a plane, sample for sample, and says where the first difference is.
void expectSamples(const fixtures::FixtureSamples& shown, const std::vector<float>& expected, const char* what)
{
    ASSERT_EQ(static_cast<std::size_t>(shown.width()) * shown.height(), expected.size()) << what;
    std::uint64_t differ = 0u;
    std::ostringstream first;
    for (std::uint32_t y = 0u; y < shown.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < shown.width(); ++x)
        {
            const float value = static_cast<float>(shown.value(x, y, 0u));
            const float wanted = expected[static_cast<std::size_t>(y) * shown.width() + x];
            if (value != wanted && differ++ == 0u)
            {
                first << "(" << x << "," << y << ") " << value << " against " << wanted;
            }
        }
    }
    EXPECT_EQ(differ, 0u) << what << ": " << differ << " samples differ, the first at " << first.str();
}

/// Holds two grey images of one size to each other, sample for sample.
void expectSameSamples(const fixtures::FixtureSamples& shown, const fixtures::FixtureSamples& expected,
                       const char* what)
{
    ASSERT_EQ(shown.width(), expected.width()) << what;
    ASSERT_EQ(shown.height(), expected.height()) << what;
    expectSamples(shown, planeOf(expected), what);
}

/// The largest difference, within a part of the target, between a frame whose samples lie on a rectangle of the target
/// that holds the part and an Apply's answer over the whole target.
[[nodiscard]] double largestWithin(const QRect& within, const fixtures::FixtureSamples& framed, const QRect& framedOn,
                                   const fixtures::FixtureSamples& applied)
{
    double largest = 0.0;
    for (int y = within.top(); y <= within.bottom(); ++y)
    {
        for (int x = within.left(); x <= within.right(); ++x)
        {
            const double difference = framed.value(static_cast<std::uint32_t>(x - framedOn.x()),
                                                   static_cast<std::uint32_t>(y - framedOn.y()), 0u) -
                                      applied.value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0u);
            largest = (std::max)(largest, std::abs(difference));
        }
    }
    return largest;
}

/// A document of the target reduced by a factor, as an interactive frame over what the preview view paints runs over.
[[nodiscard]] std::optional<an::ui::ImageDocument>
reducedDocument(const an::consumer::EngineSession& session, const an::ui::ImageDocument& document, std::uint32_t factor)
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!scope.hasValue() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> made =
        an::ui::reduceForPreview(session, scope->engine(), scope->execution(), *lease, factor);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> held = an::ui::ImageDocument::create(
        std::move(*made), an::ui::DocumentNames{.displayName = QStringLiteral("reduced")});
    EXPECT_TRUE(held.hasValue()) << held.status().message();
    if (!held.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*held);
}

/// Opens a window over the scene, sets it as `prepare` does, turns its preview on with no preview area, and holds that
/// the settled frame a change of its parameters rests on is the Apply's answer, sample for sample: first with the
/// preview view at four times, where the change made as `change` makes it is shown at once by an interactive frame over
/// the part shown at full scale - the Apply's answer there too unless the window's interactive frames approximate at
/// these parameters - and then with the view at a quarter, where the interactive frame runs over the target reduced.
void expectTheSettledFramesAreTheApplys(const std::function<ProbedWindow(ImageViewControl&)>& open,
                                        const std::function<void(QWidget&)>& prepare,
                                        const std::function<void(QWidget&)>& change, bool interactiveApproximates)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::ui::ImageDocument> document = sceneDocument(session);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "scene", std::move(*document));
    const ProbedWindow window = open(target.view());
    prepare(*window);
    setChecked(*window, "preview", true);
    window->show();
    ASSERT_TRUE(settleRun(*window, target.view()));
    ImageViewControl* preview = window->previewView();
    ASSERT_NE(preview, nullptr);
    if (QCheckBox* area = preview->getRTPreviewAreaCheckBox(); area != nullptr && area->isChecked())
    {
        area->setChecked(false);
        ASSERT_TRUE(settleRun(*window, target.view()));
    }
    ImageViewWidget* widget = preview->getViewWidget();
    ASSERT_NE(widget, nullptr);

    // At four times the view shows a part of the target at full scale.
    widget->scaleImageView(4.0, true, true);
    ASSERT_TRUE(settleRun(*window, target.view()));
    ASSERT_TRUE(pumpUntilSettled([preview]() { return preview->isWorkIdle(); }));
    const std::optional<an::ui::ViewRegion> shownRegion = widget->shownImageRegion();
    ASSERT_TRUE(shownRegion.has_value());
    const QRect shown(static_cast<int>(shownRegion->x), static_cast<int>(shownRegion->y),
                      static_cast<int>(shownRegion->width), static_cast<int>(shownRegion->height));
    window.probe->forgetFrames();
    change(*window);
    ASSERT_TRUE(settleRun(*window, target.view()));
    const std::optional<fixtures::FixtureSamples> applied =
        bodyOver(window, session, target.view().document(), applyRun());
    ASSERT_TRUE(applied.has_value());
    const auto findFrames = [&window]() {
        // The last of each kind: the frames of the parameters the window holds now.
        std::optional<RecordedFrame> interactive;
        std::optional<RecordedFrame> settled;
        for (const RecordedFrame& frame : window.probe->recordedFrames())
        {
            (frame.kind == an::ui::PreviewFrameKind::Interactive ? interactive : settled) = frame;
        }
        return std::pair{interactive, settled};
    };
    {
        const auto [interactive, settled] = findFrames();
        ASSERT_TRUE(interactive.has_value() && interactive->samples.has_value()) << "no interactive frame ran";
        ASSERT_TRUE(settled.has_value() && settled->samples.has_value()) << "no settled frame ran";
        EXPECT_TRUE(interactive->partOfTarget);
        EXPECT_FALSE(settled->partOfTarget);
        expectSameSamples(*settled->samples, *applied, "the settled frame after a change at four times");
        ASSERT_TRUE(interactive->part.contains(shown)) << "the frame does not take in the part shown";
        const double largest = largestWithin(shown, *interactive->samples, interactive->part, *applied);
        if (interactiveApproximates)
        {
            EXPECT_GT(largest, 0.0) << "the interactive frame ran the Apply's blur";
            EXPECT_LT(largest, 0.02) << "the interactive frame strays from the Apply's answer";
        }
        else
        {
            EXPECT_EQ(largest, 0.0) << "the interactive frame over the part shown is not the Apply's answer there";
        }
    }

    // At a quarter the view shows the whole target reduced.
    widget->scaleImageView(0.25, true, true);
    ASSERT_TRUE(settleRun(*window, target.view()));
    window.probe->forgetFrames();
    window.probe->askForPreview();
    ASSERT_TRUE(settleRun(*window, target.view()));
    {
        const auto [interactive, settled] = findFrames();
        ASSERT_TRUE(interactive.has_value() && interactive->samples.has_value()) << "no interactive frame ran";
        ASSERT_TRUE(settled.has_value() && settled->samples.has_value()) << "no settled frame ran";
        EXPECT_GE(interactive->proxyFactor, 2u);
        EXPECT_LT(interactive->samples->width(), kSceneWidth);
        expectSameSamples(*settled->samples, *applied, "the settled frame after a change at a quarter");
    }
}

} // namespace

// The high pass blurs a plane every sample of which is valid by the engine's compiled Gaussian - the kernels the
// Gaussian blur's dialog blurs by, one where it reaches the radius and several passes beyond - so an Apply is the
// source less that dialog's answer, raised to mid-grey and held to full scale in Float32, sample for sample.
TEST(DetailFilterTest, AHighPassApplyIsTheSourceLessTheGaussianBlursAnswer)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::ui::ImageDocument> document = sceneDocument(session);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "scene", std::move(*document));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());

    for (const double radius : {3.0, 40.0})
    {
        SCOPED_TRACE(::testing::Message() << "radius " << radius);
        std::optional<fixtures::FixtureSamples> applied;
        {
            const ProbedWindow highPass = openProbed<HighPassFilterProcess>(target.view());
            setReal(*highPass.window, "radiusValue", radius);
            applied = bodyOver(highPass, session, target.view().document(), applyRun());
        }
        std::optional<fixtures::FixtureSamples> blurred;
        {
            const ProbedWindow gaussian = openProbed<GaussianBlurProcess>(target.view());
            setReal(*gaussian.window, "sigmaValue", radius);
            blurred = bodyOver(gaussian, session, target.view().document(), applyRun());
        }
        ASSERT_TRUE(applied.has_value() && blurred.has_value());
        const std::vector<float> sky = planeOf(*source);
        const std::vector<float> base = planeOf(*blurred);
        std::vector<float> expected(sky.size());
        for (std::size_t index = 0u; index < sky.size(); ++index)
        {
            const float detail = sky[index] - base[index];
            expected[index] = std::clamp(detail + 0.5f, 0.0f, 1.0f);
        }
        expectSamples(*applied, expected, "the high pass's Apply");
    }
}

// The unsharp mask's blur mirrors the edge sample, which the compiled kernels do not, so its Apply is the engine's
// detail filter over the stored values, accumulating in Float64, sample for sample.
TEST(DetailFilterTest, AnUnsharpMaskApplyIsTheEnginesDetailFilter)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::ui::ImageDocument> document = sceneDocument(session);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "scene", std::move(*document));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());

    const ProbedWindow window = openProbed<UnsharpMaskProcess>(target.view());
    setReal(*window.window, "radiusValue", 2.5);
    setReal(*window.window, "amountValue", 1.5);
    const std::optional<fixtures::FixtureSamples> applied =
        bodyOver(window, session, target.view().document(), applyRun());
    ASSERT_TRUE(applied.has_value());

    const an::Result<an::DetailFilterPlan> plan =
        an::planDetailFilter(an::DetailFilterRequest{.method = an::DetailFilterMethod::UnsharpMask,
                                                     .sigmaX = 2.5,
                                                     .sigmaY = 2.5,
                                                     .amount = 1.5,
                                                     .borders = {.border = an::KernelBorderMode::Reflect}});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const std::vector<float> sky = planeOf(*source);
    std::vector<float> expected(sky.size(), 0.0f);
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const an::ImageLayout layout = tightPlane(kSceneWidth, kSceneHeight);
    const an::Result<an::SpatialFilterResult> filtered = an::filterImageDetail(
        scope->execution(), an::SampleType::Float32, sky.data(), layout, expected.data(), layout, *plan);
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    expectSamples(*applied, expected, "the unsharp mask's Apply");
}

// A band of a blur across a whole row of a frame this wide holds more than the stencil's scratch at these radii, which
// the engine refused. The high pass now blurs such a frame by the compiled kernels, and the unsharp mask's detail
// filter divides its rows into strips of columns. A flat frame stays flat: mid-grey under the high pass, itself under
// the unsharp mask.
TEST(DetailFilterTest, NeitherDetailFilterRefusesAFrameWiderThanABandOfItsBlurFits)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    struct Case
    {
        const char* what;
        std::uint32_t width;
        double radius;
        std::function<ProbedWindow(ImageViewControl&)> open;
        const char* radiusControl;
        float expected;
    };
    const std::vector<Case> cases{
        {"high pass", 7000u, 20.0, [](ImageViewControl& view) { return openProbed<HighPassFilterProcess>(view); },
         "radiusValue", 0.5f},
        {"unsharp mask", 13000u, 10.0, [](ImageViewControl& view) { return openProbed<UnsharpMaskProcess>(view); },
         "radiusValue", 0.25f}};
    for (const Case& wide : cases)
    {
        SCOPED_TRACE(wide.what);
        // One row of output with its halo across the whole width is more than the ceiling holds.
        const std::uint64_t reach = static_cast<std::uint64_t>(std::ceil(4.0 * wide.radius));
        ASSERT_GT((2u * reach + 1u) * wide.width * sizeof(double), an::kMaxStencilScratchBytes);
        std::optional<an::ui::ImageDocument> document = flatDocument(session, wide.width, 16u, 0.25f);
        ASSERT_TRUE(document.has_value());
        const TargetView target(*context, "wide", std::move(*document));
        const ProbedWindow window = wide.open(target.view());
        setReal(*window.window, wide.radiusControl, wide.radius);
        const std::optional<fixtures::FixtureSamples> applied =
            bodyOver(window, session, target.view().document(), applyRun());
        ASSERT_TRUE(applied.has_value());
        double largest = 0.0;
        for (std::uint32_t y = 0u; y < applied->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < applied->width(); ++x)
            {
                largest = (std::max)(largest, std::abs(applied->value(x, y, 0u) - static_cast<double>(wide.expected)));
            }
        }
        EXPECT_LT(largest, 1.0e-6);
    }
}

// An interactive frame over the target reduced by the preview view's factor blurs the reduction by the radius reduced
// with it: it is what the dialog's Apply makes of the reduction at that radius.
TEST(DetailFilterTest, AnInteractiveFrameOverAReductionBlursItByTheRadiusReduced)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::ui::ImageDocument> document = sceneDocument(session);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "scene", std::move(*document));
    constexpr std::uint32_t kFactor = 4u;
    const std::optional<an::ui::ImageDocument> reduced = reducedDocument(session, target.view().document(), kFactor);
    ASSERT_TRUE(reduced.has_value());

    {
        SCOPED_TRACE("high pass");
        const ProbedWindow window = openProbed<HighPassFilterProcess>(target.view());
        setReal(*window.window, "radiusValue", 12.0);
        const std::optional<fixtures::FixtureSamples> framed =
            bodyOver(window, session, *reduced, previewRun(an::ui::PreviewFrameKind::Interactive, kFactor));
        setReal(*window.window, "radiusValue", 12.0 / kFactor);
        const std::optional<fixtures::FixtureSamples> applied = bodyOver(window, session, *reduced, applyRun());
        ASSERT_TRUE(framed.has_value() && applied.has_value());
        expectSameSamples(*framed, *applied, "the high pass over a reduction");
    }
    {
        SCOPED_TRACE("unsharp mask");
        const ProbedWindow window = openProbed<UnsharpMaskProcess>(target.view());
        setReal(*window.window, "amountValue", 1.5);
        setReal(*window.window, "radiusValue", 8.0);
        const std::optional<fixtures::FixtureSamples> framed =
            bodyOver(window, session, *reduced, previewRun(an::ui::PreviewFrameKind::Interactive, kFactor));
        setReal(*window.window, "radiusValue", 8.0 / kFactor);
        const std::optional<fixtures::FixtureSamples> applied = bodyOver(window, session, *reduced, applyRun());
        ASSERT_TRUE(framed.has_value() && applied.has_value());
        expectSameSamples(*framed, *applied, "the unsharp mask over a reduction");
    }
}

// Beyond one kernel's reach an interactive frame blurs by the real-time preview's boxes, whose cost does not grow with
// the radius, and states their reach as the margin it reads around a part; an Apply, and a settled frame as the Apply,
// blur by the exact Gaussian's passes. The two pictures differ, and by little.
TEST(DetailFilterTest, AnInteractiveFrameBeyondOneKernelBlursByThePreviewBoxes)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    std::optional<an::ui::ImageDocument> document = sceneDocument(session);
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "scene", std::move(*document));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());
    constexpr double kRadius = 60.0;

    const ProbedWindow window = openProbed<HighPassFilterProcess>(target.view());
    setReal(*window.window, "radiusValue", kRadius);
    const an::Result<an::GaussianPlan> boxes = an::planRealtimePreviewGaussianBlur(an::RealtimePreviewGaussianRequest{
        .sigmaX = kRadius, .sigmaY = kRadius, .borders = {.border = an::KernelBorderMode::ReflectExcludingEdge}});
    ASSERT_TRUE(boxes.hasValue()) << boxes.status().message();
    EXPECT_GT(boxes->horizontalRadius(), an::kMaxStencilRadius);
    EXPECT_EQ(window.probe->regionMargin(), boxes->horizontalRadius());

    // The boxes' blur of the scene, taken away from it in Float32.
    const std::vector<float> sky = planeOf(*source);
    std::vector<float> base(sky.size(), 0.0f);
    {
        an::Result<an::consumer::OperationScope> scope =
            session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const an::ImageLayout layout = tightPlane(kSceneWidth, kSceneHeight);
        const an::Result<an::SpatialFilterResult> blurred = an::blurImageRealtimePreviewGaussian(
            scope->execution(), an::SampleType::Float32, sky.data(), layout, base.data(), layout, *boxes);
        ASSERT_TRUE(blurred.hasValue()) << blurred.status().message();
    }
    std::vector<float> expected(sky.size());
    for (std::size_t index = 0u; index < sky.size(); ++index)
    {
        const float detail = sky[index] - base[index];
        expected[index] = std::clamp(detail + 0.5f, 0.0f, 1.0f);
    }
    an::ui::ProcessRun overPart = previewRun(an::ui::PreviewFrameKind::Interactive, 1u);
    overPart.partOfTarget = true;
    const std::optional<fixtures::FixtureSamples> framed =
        bodyOver(window, session, target.view().document(), overPart);
    ASSERT_TRUE(framed.has_value());
    expectSamples(*framed, expected, "an interactive frame beyond one kernel");

    const std::optional<fixtures::FixtureSamples> applied =
        bodyOver(window, session, target.view().document(), applyRun());
    const std::optional<fixtures::FixtureSamples> settled =
        bodyOver(window, session, target.view().document(), previewRun(an::ui::PreviewFrameKind::Settled, 1u));
    ASSERT_TRUE(applied.has_value() && settled.has_value());
    expectSameSamples(*settled, *applied, "a settled frame beyond one kernel");
    const QRect whole(0, 0, static_cast<int>(kSceneWidth), static_cast<int>(kSceneHeight));
    const double largest = largestWithin(whole, *framed, whole, *applied);
    EXPECT_GT(largest, 0.0) << "the interactive frame blurred as the Apply does";
    EXPECT_LT(largest, 0.02) << "the boxes stray from the Gaussian";
}

// A change of a detail filter's parameters is shown at once by an interactive frame over what the preview view paints,
// and then by a settled frame over the whole image, which is the Apply's answer, sample for sample - whether the view
// shows a part of the target at full scale or the whole of it reduced. Forty pixels is beyond one kernel's reach, so
// the high pass's interactive frame over the part shown blurs by the boxes and comes close to the Apply's answer.
TEST(DetailFilterTest, ASettledHighPassFrameIsTheApplysAnswer)
{
    expectTheSettledFramesAreTheApplys([](ImageViewControl& view) { return openProbed<HighPassFilterProcess>(view); },
                                       [](QWidget&) {}, [](QWidget& window) { setReal(window, "radiusValue", 40.0); },
                                       true);
}

// One kernel reaches every radius the unsharp mask's dialog offers, so its interactive frame over the part shown is the
// Apply's answer there too.
TEST(DetailFilterTest, ASettledUnsharpMaskFrameIsTheApplysAnswer)
{
    expectTheSettledFramesAreTheApplys([](ImageViewControl& view) { return openProbed<UnsharpMaskProcess>(view); },
                                       [](QWidget& window) { setReal(window, "amountValue", 2.0); },
                                       [](QWidget& window) { setReal(window, "radiusValue", 4.0); }, false);
}
