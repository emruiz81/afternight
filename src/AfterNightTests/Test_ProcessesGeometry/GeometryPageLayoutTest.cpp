// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The geometry and filter-array processes launched on an image as mirror pages: each in its column at 600 px of
/// height, in both densities, with every control in the column and reached by Tab and nothing clipped sideways where
/// text is measured; each giving through its page the result its window gives for the same parameters; and Crop and
/// Rotate, which mark a rectangle on the view the person works on, marking it on the page's view of the target - the
/// same rectangle of the image, the same result - and leaving nothing of it on the target's own view.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"
#include "Test_ProcessesShared/SelectionDrag.h"

#include "app/FileCompanions.h"
#include "controls/ImageProcessControl.h"

#include <QPushButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
