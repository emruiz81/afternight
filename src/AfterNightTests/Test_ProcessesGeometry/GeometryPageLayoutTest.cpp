// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The geometry and filter-array processes launched on an image as mirror pages: each in its column at 600 px of
/// height, in both densities, with every control in the column and reached by Tab and nothing clipped sideways where
/// text is measured; each giving through its page the result its window gives for the same parameters; each run on its
/// page, over a sequence or an image, stopped by the page's Cancel with no progress window opened; and Crop and
/// Rotate, which mark a rectangle on the view the person works on, marking it on the page's view of the target - the
/// same rectangle of the image, the same result - and leaving nothing of it on the target's own view.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"
#include "Test_ProcessesShared/SelectionDrag.h"

#include "app/FileCompanions.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "processes/CfaProcessSupport.h"
#include "processes/MergeCFAProcess.h"

#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::geometry;

/// The process of an action; fails the case when the family has none.
[[nodiscard]] const GeometryProcess* familyProcess(std::string_view action)
{
    const GeometryProcess* process = processOfAction(action);
    EXPECT_NE(process, nullptr) << "no process of " << action;
    return process;
}

/// What a page case opens a process on, kept for the case: a view of the process's scene, or of a sequence of its
/// frames for Extract Sequence, whose file lives in a folder of its own.
struct PageTarget
{
    std::unique_ptr<QTemporaryDir> directory;
    ViewHandle view;
};

/// The asset of a fixture scene.
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                                 std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples(scene);
    if (image == nullptr || !samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = an::tests::fixtures::engine::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// A view a process of the family opens on, owned by the case: its scene, or a sequence of three of its frames for
/// Extract Sequence.
[[nodiscard]] PageTarget openPageTarget(ProcessTestContext& context, const GeometryProcess& process,
                                        const std::string& name)
{
    PageTarget target;
    if (std::string_view(process.name) == "ExtractSequence")
    {
        target.directory = std::make_unique<QTemporaryDir>();
        const QString path = writeSceneSequence(context.engineSession(), *target.directory,
                                                {"scene-rgb-float32", "scene-rgb-float32-second", "scene-rgb-float32"});
        an::Result<an::ui::ImageDocument> document =
            an::ui::openSequenceDocument(context.engineSession(), path, an::SequenceFormat::ImageList, {});
        EXPECT_TRUE(document.hasValue()) << document.status().message();
        if (!document.hasValue())
        {
            return target;
        }
        document->setNames(an::ui::DocumentNames{.displayName = QString::fromStdString(name), .sourcePath = path});
        target.view.reset(new ImageViewControl());
        target.view->initialize(name, ViewControlMode::standard);
        target.view->resize(640, 480);
        target.view->setSequenceDocument(std::move(*document), false);
        target.view->initializeNewImageHistory("Sequence opened");
        EXPECT_TRUE(settleView(*target.view));
        return target;
    }
    std::optional<an::consumer::ImageAsset> asset = sceneAsset(context.engineSession(), process.scene);
    if (asset.has_value())
    {
        target.view = assetView(std::move(*asset), name);
    }
    return target;
}

/// The page of a process held to its column; reported skipped where text is not measured as the interface measures it.
void fitsItsColumn(std::string_view action)
{
    const GeometryProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    PageTarget target = openPageTarget(*context, *process, "page target");
    ASSERT_NE(target.view, nullptr);
    context->addViewControl(target.view.get());
    context->setActiveImageViewControl(target.view.get());
    const bool measured = shared::expectPageFitsItsColumn(process->descriptorId, *target.view, *context,
                                                          an::ui::ProcessSurface::MirrorPage);
    context->removeViewControl(target.view.get(), false);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// The page of a process gives the window's result, its parameters set to the mode its window cases run.
void appliesAsInAWindow(std::string_view action)
{
    const GeometryProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    std::vector<PageTarget> targets;
    shared::MirrorApply how;
    how.configure = [process](QWidget& window) { process->configure(window, "-"); };
    how.resultIsANewImage = process->landing == Landing::NewViews;
    shared::expectMirrorPageAppliesAsTheWindow(
        process->descriptorId, *context,
        [&]() {
            targets.push_back(openPageTarget(*context, *process, "applied target"));
            // The case owns the view through the handle it hands back; the folder stays with the case.
            return std::move(targets.back().view);
        },
        how);
}

/// The process a launch over a view opened, as a process of no preview; null when none was.
[[nodiscard]] ImageProcessControl* launched(const GeometryProcess& process, ImageViewControl& target)
{
    return dynamic_cast<ImageProcessControl*>(shared::launchWindowOf(process.descriptorId, target));
}

/// What a Crop or Rotate run left in its target: the samples of the image after it.
[[nodiscard]] std::optional<fixtures::FixtureSamples> targetSamples(ImageViewControl& target)
{
    return documentSamples(target.document());
}

/// A run of a process over a sequence of three frames of its scene, on its page, stopped by the page's Cancel: no
/// progress window, the run's two bars, and the sequence left as it was.
void sequenceRunStopsAtThePagesCancel(std::string_view action)
{
    const GeometryProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path =
        writeSceneSequence(context->engineSession(), directory, {process->scene, process->scene, process->scene});
    ASSERT_FALSE(path.isEmpty());
    ImageViewControl* const target = openSequenceView(*context, path, an::SequenceFormat::ImageList, "page frames");
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->isSequence());
    ASSERT_TRUE(settleView(*target));
    shared::expectMirrorPageRunStopsAtItsCancel(process->descriptorId, *context, *target,
                                                [process](QWidget& window) { process->configure(window, "-"); });
}

/// A run of a process over its scene, on its page, stopped by the page's Cancel: the image left as it was.
void imageRunStopsAtThePagesCancel(std::string_view action)
{
    const GeometryProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    PageTarget target = openPageTarget(*context, *process, "stopped target");
    ASSERT_NE(target.view, nullptr);
    context->addViewControl(target.view.get());
    shared::expectMirrorPageRunStopsAtItsCancel(process->descriptorId, *context, *target.view,
                                                [process](QWidget& window) { process->configure(window, "-"); });
    dropView(*context, target.view);
}

} // namespace

#define AN_GEOMETRY_PAGE(Name, Action)         \
    TEST(PageLayout, Name##FitsItsColumn)      \
    {                                          \
        fitsItsColumn(Action);                 \
    }                                          \
    TEST(PageLayout, Name##AppliesAsInAWindow) \
    {                                          \
        appliesAsInAWindow(Action);            \
    }

AN_GEOMETRY_PAGE(Crop, "actionCrop")
AN_GEOMETRY_PAGE(Rotate, "actionRotate")
AN_GEOMETRY_PAGE(Resize, "actionResize")
AN_GEOMETRY_PAGE(IntegerResample, "actionIntegerResample")
AN_GEOMETRY_PAGE(CfaIntegerResample, "actionCFA_Integer_Resample")
AN_GEOMETRY_PAGE(Debayer, "actionDebayer")
AN_GEOMETRY_PAGE(SplitCfa, "actionSplit_CFA")
AN_GEOMETRY_PAGE(ExtractSequence, "actionExtract_Sequence")

// A run on a page shows in the page's row and opens no progress window, over a sequence as over an image; the page's
// Cancel stops it, and the target is left as it was.
#define AN_GEOMETRY_PAGE_RUN(Name, Action)                  \
    TEST(PageRun, Name##OverASequenceStopsAtThePagesCancel) \
    {                                                       \
        sequenceRunStopsAtThePagesCancel(Action);           \
    }                                                       \
    TEST(PageRun, Name##OverAnImageStopsAtThePagesCancel)   \
    {                                                       \
        imageRunStopsAtThePagesCancel(Action);              \
    }

AN_GEOMETRY_PAGE_RUN(Crop, "actionCrop")
AN_GEOMETRY_PAGE_RUN(Rotate, "actionRotate")
AN_GEOMETRY_PAGE_RUN(Resize, "actionResize")
AN_GEOMETRY_PAGE_RUN(IntegerResample, "actionIntegerResample")
AN_GEOMETRY_PAGE_RUN(CfaIntegerResample, "actionCFA_Integer_Resample")
AN_GEOMETRY_PAGE_RUN(Debayer, "actionDebayer")
AN_GEOMETRY_PAGE_RUN(SplitCfa, "actionSplit_CFA")

// Extract Sequence runs over a sequence only.
TEST(PageRun, ExtractSequenceOverASequenceStopsAtThePagesCancel)
{
    sequenceRunStopsAtThePagesCancel("actionExtract_Sequence");
}

// A crop's rectangle drawn on the page's view of the target is the rectangle of the image the same drag draws on the
// target's own view in a window, and its Apply crops the image to it as the window's does.
TEST(Crop, SelectionOnThePageGivesTheWindowsResult)
{
    const GeometryProcess* process = familyProcess("actionCrop");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    const QRect drawn(3, 2, 9, 6);
    std::optional<fixtures::FixtureSamples> results[2];
    for (const bool asPage : {false, true})
    {
        SCOPED_TRACE(asPage ? "in a page" : "in a window");
        PageTarget target = openPageTarget(*context, *process, "crop target");
        ASSERT_NE(target.view, nullptr);
        activate(*context, *target.view);
        PagePresenter presenter;
        presenter.adopting = asPage;
        context->presenter = &presenter;
        const QPointer<ImageProcessControl> crop = launched(*process, *target.view);
        ASSERT_NE(crop, nullptr);
        ASSERT_EQ(crop->hasPageLayout(), asPage);
        ImageViewControl* const marked = crop->interactionView();
        ASSERT_NE(marked, nullptr);
        EXPECT_EQ(marked == target.view.get(), !asPage) << "a page marks on its own view of the target";
        ASSERT_TRUE(settleView(*marked));

        auto* const newSelection = widgetOf<QPushButton>(*crop, "newSelectionButton");
        ASSERT_NE(newSelection, nullptr);
        newSelection->click();
        shared::dragNewSelection(*marked, drawn);
        EXPECT_EQ(marked->getViewWidget()->getCurrentSelectionArea(), drawn) << "the rectangle of the image drawn";
        EXPECT_EQ(widgetOf<QSpinBox>(*crop, "targetPosX")->value(), drawn.x());
        EXPECT_EQ(widgetOf<QSpinBox>(*crop, "targetPosY")->value(), drawn.y());
        EXPECT_EQ(widgetOf<QSpinBox>(*crop, "targetWidth")->value(), drawn.width());
        EXPECT_EQ(widgetOf<QSpinBox>(*crop, "targetHeight")->value(), drawn.height());

        ASSERT_TRUE(crop->triggerPrimaryAction());
        ASSERT_TRUE(pumpUntil([&]() { return shared::mirrorRunEnded(crop, *target.view); }));
        results[asPage ? 1 : 0] = targetSamples(*target.view);
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
        dropView(*context, target.view);
    }
    ASSERT_TRUE(results[0].has_value() && results[1].has_value());
    EXPECT_EQ(results[0]->width(), static_cast<std::uint32_t>(drawn.width()));
    EXPECT_EQ(results[0]->height(), static_cast<std::uint32_t>(drawn.height()));
    EXPECT_TRUE(fixtures::sameSamples(*results[0], *results[1])) << "the page crops as the window does";
}

// On a page the crop's rectangle is drawn on the page's view only: the target's own view shows no selection, before a
// rectangle is drawn and after.
TEST(Crop, TargetViewShowsNoSelectionInPageMode)
{
    const GeometryProcess* process = familyProcess("actionCrop");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    PageTarget target = openPageTarget(*context, *process, "crop page target");
    ASSERT_NE(target.view, nullptr);
    activate(*context, *target.view);
    {
        PagePresenter presenter;
        context->presenter = &presenter;
        ImageProcessControl* const crop = launched(*process, *target.view);
        ASSERT_NE(crop, nullptr);
        ASSERT_TRUE(crop->hasPageLayout());
        ImageViewControl* const marked = crop->interactionView();
        ASSERT_NE(marked, target.view.get());
        EXPECT_TRUE(marked->getViewWidget()->isSelectionAreaEnabled()) << "the page's view shows the crop";
        EXPECT_FALSE(target.view->getViewWidget()->isSelectionAreaEnabled()) << "the target's view shows none";

        const QRect onTheTarget = target.view->getViewWidget()->getCurrentSelectionArea();
        widgetOf<QPushButton>(*crop, "newSelectionButton")->click();
        shared::dragNewSelection(*marked, QRect(2, 2, 6, 5));
        EXPECT_TRUE(marked->getViewWidget()->isSelectionAreaEnabled());
        EXPECT_EQ(marked->getViewWidget()->getCurrentSelectionArea(), QRect(2, 2, 6, 5));
        EXPECT_FALSE(target.view->getViewWidget()->isSelectionAreaEnabled());
        EXPECT_EQ(target.view->getViewWidget()->getCurrentSelectionArea(), onTheTarget)
            << "the drag drew nothing on the target's view";
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    dropView(*context, target.view);
}

// Rotate's crop, moved on the page's view of the turned image, is the rectangle the same move gives on the target's own
// view in a window, and its Apply turns and crops the image as the window's does.
TEST(Rotate, SelectionOnThePageGivesTheWindowsResult)
{
    const GeometryProcess* process = familyProcess("actionRotate");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    std::optional<fixtures::FixtureSamples> results[2];
    QRect moved[2];
    for (const bool asPage : {false, true})
    {
        SCOPED_TRACE(asPage ? "in a page" : "in a window");
        PageTarget target = openPageTarget(*context, *process, "rotate target");
        ASSERT_NE(target.view, nullptr);
        activate(*context, *target.view);
        PagePresenter presenter;
        presenter.adopting = asPage;
        context->presenter = &presenter;
        const QPointer<ImageProcessControl> rotate = launched(*process, *target.view);
        ASSERT_NE(rotate, nullptr);
        ASSERT_EQ(rotate->hasPageLayout(), asPage);
        ImageViewControl* const marked = rotate->interactionView();
        ASSERT_NE(marked, nullptr);
        ASSERT_TRUE(settleView(*marked));
        setDouble(*rotate, "rotationAngle", 30.0);
        setGroupChecked(*rotate, "groupBox", true);
        setInt(*rotate, "targetWidth", 10);
        setInt(*rotate, "targetHeight", 8);
        pumpQuietly();
        EXPECT_TRUE(marked->getViewWidget()->isRotationPreviewModeEnabled()) << "the turn is drawn where it is marked";

        shared::dragSelectionBy(*marked, QPoint(4, 3));
        moved[asPage ? 1 : 0] = QRect(
            widgetOf<QSpinBox>(*rotate, "targetPosX")->value(), widgetOf<QSpinBox>(*rotate, "targetPosY")->value(),
            widgetOf<QSpinBox>(*rotate, "targetWidth")->value(), widgetOf<QSpinBox>(*rotate, "targetHeight")->value());

        ASSERT_TRUE(rotate->triggerPrimaryAction());
        ASSERT_TRUE(pumpUntil([&]() { return shared::mirrorRunEnded(rotate, *target.view); }));
        results[asPage ? 1 : 0] = targetSamples(*target.view);
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
        dropView(*context, target.view);
    }
    EXPECT_EQ(moved[0], moved[1]) << "the same move gives the same rectangle of the turned image";
    EXPECT_NE(moved[0].topLeft(), QPoint(0, 0)) << "the drag moved the crop";
    ASSERT_TRUE(results[0].has_value() && results[1].has_value());
    EXPECT_TRUE(fixtures::sameSamples(*results[0], *results[1])) << "the page turns and crops as the window does";
}

// On a page Rotate draws its turn and its crop on the page's view only: the target's own view shows neither.
TEST(Rotate, TargetViewShowsNoSelectionInPageMode)
{
    const GeometryProcess* process = familyProcess("actionRotate");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    PageTarget target = openPageTarget(*context, *process, "rotate page target");
    ASSERT_NE(target.view, nullptr);
    activate(*context, *target.view);
    {
        PagePresenter presenter;
        context->presenter = &presenter;
        ImageProcessControl* const rotate = launched(*process, *target.view);
        ASSERT_NE(rotate, nullptr);
        ASSERT_TRUE(rotate->hasPageLayout());
        ImageViewControl* const marked = rotate->interactionView();
        ASSERT_NE(marked, target.view.get());
        setDouble(*rotate, "rotationAngle", 30.0);
        setGroupChecked(*rotate, "groupBox", true);
        pumpQuietly();
        EXPECT_TRUE(marked->getViewWidget()->isRotationPreviewModeEnabled());
        EXPECT_TRUE(marked->getViewWidget()->isSelectionAreaEnabled());
        EXPECT_FALSE(target.view->getViewWidget()->isRotationPreviewModeEnabled()) << "the target's view is not turned";
        EXPECT_FALSE(target.view->getViewWidget()->isSelectionAreaEnabled()) << "nor shows the crop";
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    dropView(*context, target.view);
}

namespace
{

/// The four planes of the family's mosaic as views of their own, cfa0 to cfa3, made active in turn.
[[nodiscard]] std::vector<ViewHandle> mergePlanes(ProcessTestContext& context)
{
    std::vector<ViewHandle> planes;
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(context.engineSession(), "mosaic-rggb-uint16");
    EXPECT_TRUE(mosaic.has_value());
    if (!mosaic.has_value())
    {
        return planes;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        context.engineSession(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
    EXPECT_TRUE(scope.hasValue() && lease.hasValue());
    if (!scope.hasValue() || !lease.hasValue())
    {
        return planes;
    }
    an::Result<std::vector<an::consumer::ImageAsset>> split =
        an::ui::processes::splitMosaicPlanes(*scope, an::ui::ProcessSource::of(*lease));
    EXPECT_TRUE(split.hasValue());
    lease->release();
    scope->finish(an::Status::success());
    if (!split.hasValue())
    {
        return planes;
    }
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        planes.push_back(assetView(std::move((*split)[plane]), "cfa" + std::to_string(plane)));
        activate(context, *planes.back());
    }
    return planes;
}

/// Chooses the four planes in a Merge CFA window or page.
void choosePlanes(QWidget& window, const std::vector<ViewHandle>& planes)
{
    for (std::size_t plane = 0u; plane < planes.size(); ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(window, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    pumpQuietly();
}

constexpr const char* kMergeCfa = "sensor_calibration.actionMerge_CFA";

} // namespace

// Merge CFA's page, launched on no image, in its column at 600 px of height in both densities: its canvas names the
// planes still to choose.
TEST(PageLayout, MergeCfaFitsItsColumn)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    std::vector<ViewHandle> planes = mergePlanes(*context);
    ASSERT_EQ(planes.size(), 4u);
    const bool measured =
        shared::expectPageFitsItsColumn(kMergeCfa, *planes[0], *context, an::ui::ProcessSurface::PreviewPage);
    for (ViewHandle& plane : planes)
    {
        dropView(*context, plane);
    }
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

// The four planes chosen in Merge CFA's page give the mosaic its window gives.
TEST(PageLayout, MergeCfaAppliesAsInAWindow)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    std::vector<ViewHandle> planes = mergePlanes(*context);
    ASSERT_EQ(planes.size(), 4u);
    std::optional<fixtures::FixtureSamples> results[2];
    for (const bool asPage : {false, true})
    {
        SCOPED_TRACE(asPage ? "in a page" : "in a window");
        PagePresenter presenter;
        presenter.adopting = asPage;
        context->presenter = &presenter;
        auto* const process = dynamic_cast<ImageRTPreviewControl*>(shared::launchWindowOf(kMergeCfa, *planes[0]));
        ASSERT_NE(process, nullptr);
        EXPECT_EQ(process->hasPageLayout(), asPage);
        choosePlanes(*process, planes);
        if (asPage)
        {
            EXPECT_TRUE(process->pageLayout()->canvasMessage().isEmpty()) << "every plane is chosen";
        }
        const std::size_t made = context->madeViews.size();
        const QPointer<ImageRTPreviewControl> guard(process);
        ASSERT_TRUE(pumpUntil([&guard]() { return guard.isNull() || guard->triggerPrimaryAction(); }));
        ASSERT_TRUE(pumpUntil([&guard]() { return guard.isNull() || guard->isProcessIdle(); }));
        ASSERT_EQ(context->madeViews.size(), made + 1u) << "the Apply made the mosaic";
        ImageViewControl* const mosaic = context->madeViews.back();
        ASSERT_TRUE(pumpUntil([mosaic]() { return mosaic->isWorkIdle(); }));
        results[asPage ? 1 : 0] = documentSamples(mosaic->document());
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    ASSERT_TRUE(results[0].has_value() && results[1].has_value());
    EXPECT_TRUE(fixtures::sameSamples(*results[0], *results[1])) << "the page's mosaic is the window's";
    for (ViewHandle& plane : planes)
    {
        dropView(*context, plane);
    }
}

// Merge CFA joining four sequences on a page shows its run in the page's row - Overall and Processing - and opens no
// progress window; the page's Cancel stops the join, and nothing is published.
TEST(MergeCfa, ASequencePageOpensNoProgressWindowAndCancels)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::vector<ImageViewControl*> planes;
    for (int plane = 0; plane < 4; ++plane)
    {
        const QString path =
            writeSceneSequence(context->engineSession(), directory, {"mosaic-rggb-uint16", "mosaic-rggb-uint16"},
                               an::SequenceFormat::ImageList, QStringLiteral("plane%1").arg(plane));
        ImageViewControl* const view =
            openSequenceView(*context, path, an::SequenceFormat::ImageList, "plane-sequence-" + std::to_string(plane));
        ASSERT_NE(view, nullptr);
        ASSERT_TRUE(view->isSequence());
        planes.push_back(view);
    }
    PagePresenter presenter;
    context->presenter = &presenter;
    auto* const process = dynamic_cast<MergeCFAProcess*>(shared::launchWindowOf(kMergeCfa, *planes[0]));
    ASSERT_NE(process, nullptr);
    an::ui::ProcessPageLayout* const page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    for (std::size_t plane = 0u; plane < planes.size(); ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*process, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    ASSERT_TRUE(pumpUntil([process]() { return process->isProcessIdle(); }));
    const std::size_t made = context->madeViews.size();

    const ProgressWindowsShown windows;
    {
        PoolHold hold;
        ASSERT_TRUE(hold.held());
        ASSERT_TRUE(process->triggerPrimaryAction()) << "Ok joins the sequences";
        QCoreApplication::processEvents();
        EXPECT_TRUE(process->isRunning());
        EXPECT_TRUE(process->runState().sequence);
        EXPECT_EQ(page->runBars(), 2) << "Overall and Processing";
        ASSERT_TRUE(page->cancelButton()->isVisible());
        page->cancelButton()->click();
    }
    ASSERT_TRUE(pumpUntil([process]() { return !process->isRunning(); }));
    QCoreApplication::processEvents();
    EXPECT_EQ(windows.count(), 0) << "no progress window on a page";
    EXPECT_EQ(process->runState().phase, an::ui::ProcessRunState::Phase::Cancelled);
    EXPECT_EQ(context->madeViews.size(), made) << "nothing published";
    EXPECT_EQ(process->runsPublished(), 0u);
    shared::closeOpenedProcessWindows();
    context->presenter = nullptr;
}

// Merge CFA joining four images on a page shows its run in the page's row and opens no progress window; the page's
// Cancel stops the join, nothing is published, it says nothing and the page stays open.
TEST(MergeCfa, AnImagePageRunStopsAtItsCancel)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    std::vector<ViewHandle> planes = mergePlanes(*context);
    ASSERT_EQ(planes.size(), 4u);
    PagePresenter presenter;
    context->presenter = &presenter;
    const QPointer<MergeCFAProcess> process =
        dynamic_cast<MergeCFAProcess*>(shared::launchWindowOf(kMergeCfa, *planes[0]));
    ASSERT_NE(process, nullptr);
    an::ui::ProcessPageLayout* const page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    choosePlanes(*process, planes);
    ASSERT_TRUE(pumpUntil([&process]() { return process.isNull() || process->isProcessIdle(); }));
    ASSERT_FALSE(process.isNull());
    const std::size_t made = context->madeViews.size();
    const int errors = context->errorCount();

    const ProgressWindowsShown windows;
    {
        PoolHold hold;
        ASSERT_TRUE(hold.held());
        ASSERT_TRUE(process->triggerPrimaryAction()) << "Ok joins the planes";
        QCoreApplication::processEvents();
        EXPECT_TRUE(process->isRunning());
        EXPECT_TRUE(process->runState().running());
        EXPECT_FALSE(process->runState().sequence);
        EXPECT_EQ(page->runBars(), 1);
        ASSERT_TRUE(page->cancelButton()->isVisible()) << "the page's Cancel is offered";
        EXPECT_TRUE(page->cancelButton()->isEnabled());
        page->cancelButton()->click();
    }
    ASSERT_TRUE(pumpUntil([&process]() { return process.isNull() || !process->isRunning(); }));
    pumpQuietly();
    ASSERT_FALSE(process.isNull()) << "a stopped join leaves the page open";
    EXPECT_EQ(windows.count(), 0) << "no progress window on a page";
    EXPECT_EQ(process->runState().phase, an::ui::ProcessRunState::Phase::Cancelled)
        << "the page's Cancel stopped the join";
    EXPECT_EQ(context->madeViews.size(), made) << "nothing published";
    EXPECT_EQ(process->runsPublished(), 0u);
    EXPECT_EQ(context->errorCount(), errors) << "a stop the person asked for is quiet";
    shared::closeOpenedProcessWindows();
    context->presenter = nullptr;
    for (ViewHandle& plane : planes)
    {
        dropView(*context, plane);
    }
}

// Merge CFA's preview, once its four planes are chosen, is the mosaic Ok makes of them.
TEST(MergeCfa, ThePreviewIsTheResult)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    std::vector<ViewHandle> planes = mergePlanes(*context);
    ASSERT_EQ(planes.size(), 4u);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(shared::launchWindowOf(kMergeCfa, *planes[0]));
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->previewView(), nullptr) << "no preview until every plane is chosen";
    choosePlanes(*process, planes);
    ASSERT_TRUE(pumpUntil([process]() {
        return process->previewView() != nullptr && process->runTelemetry().previewsCommitted >= 1u &&
               process->isProcessIdle();
    })) << "the preview shows the mosaic";
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(process->previewView()->document());
    ASSERT_TRUE(shown.has_value());

    const std::size_t made = context->madeViews.size();
    const QPointer<ImageRTPreviewControl> guard(process);
    ASSERT_TRUE(guard->triggerPrimaryAction());
    ASSERT_TRUE(pumpUntil([&guard]() { return guard.isNull() || guard->isProcessIdle(); }));
    ASSERT_EQ(context->madeViews.size(), made + 1u);
    ImageViewControl* const mosaic = context->madeViews.back();
    ASSERT_TRUE(pumpUntil([mosaic]() { return mosaic->isWorkIdle(); }));
    const std::optional<fixtures::FixtureSamples> applied = documentSamples(mosaic->document());
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(shown->width(), applied->width()) << "the preview is the whole mosaic, twice a plane's size";
    EXPECT_EQ(shown->height(), applied->height());
    EXPECT_TRUE(fixtures::sameSamples(*shown, *applied)) << "the preview is the mosaic Ok makes";
    shared::closeOpenedProcessWindows();
    for (ViewHandle& plane : planes)
    {
        dropView(*context, plane);
    }
}
