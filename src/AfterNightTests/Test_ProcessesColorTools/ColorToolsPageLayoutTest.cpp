// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour calibration tools as mirror pages: each in its column at 600 px of height, in both densities, with every
/// control in the column and reached by Tab and nothing clipped sideways where text is measured; each giving through
/// its page the result its window gives for the same parameters and the same reference; and the two that mark an area
/// of the target - Color Calibration and Background Neutralization - marking it on the page's view of the target: the
/// same rectangle of the image, the same result, nothing of it left on the target's own view.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"
#include "Test_ProcessesShared/SelectionDrag.h"

#include "controls/ImageProcessControl.h"

#include <QPushButton>

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
using namespace an::tests::processes::colortools;

/// The tool of a name; fails the case when the family has none.
[[nodiscard]] const ColorTool* familyTool(std::string_view name)
{
    const ColorTool* tool = toolNamed(name);
    EXPECT_NE(tool, nullptr) << "no tool named " << name;
    return tool;
}

/// A view of a tool's scene, owned by the case.
[[nodiscard]] ViewHandle sceneTarget(ProcessTestContext& context, const ColorTool& tool, const std::string& name)
{
    std::optional<an::consumer::ImageAsset> asset = sceneAsset(context.engineSession(), tool.scene);
    return asset.has_value() ? assetView(std::move(*asset), name) : ViewHandle();
}

/// The reference a run reads beside its target when the tool needs one - a view of the second scene - listed among the
/// context's views; none for a tool that reads its target.
[[nodiscard]] ViewHandle openReference(ProcessTestContext& context, const ColorTool& tool)
{
    if (!tool.needsReference)
    {
        return ViewHandle();
    }
    std::optional<an::consumer::ImageAsset> asset = sceneAsset(context.engineSession(), "scene-rgb-float32-second");
    ViewHandle reference = asset.has_value() ? assetView(std::move(*asset), kReferenceView) : ViewHandle();
    if (reference != nullptr)
    {
        context.addViewControl(reference.get());
    }
    return reference;
}

/// The page of a tool held to its column; reported skipped where text is not measured as the interface measures it.
void fitsItsColumn(std::string_view name)
{
    const ColorTool* tool = familyTool(name);
    ASSERT_NE(tool, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle reference = openReference(*context, *tool);
    ViewHandle target = sceneTarget(*context, *tool, "page target");
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());
    const bool measured =
        shared::expectPageFitsItsColumn(tool->descriptorId, *target, *context, an::ui::ProcessSurface::MirrorPage);
    context->removeViewControl(target.get(), false);
    if (reference != nullptr)
    {
        context->removeViewControl(reference.get(), false);
    }
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// The page of a tool gives the window's result, its dialog as the window cases run it and its reference the same.
void appliesAsInAWindow(std::string_view name)
{
    const ColorTool* tool = familyTool(name);
    ASSERT_NE(tool, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle reference = openReference(*context, *tool);
    shared::MirrorApply how;
    how.configure = [tool](QWidget& window) {
        tool->configure(window, "-");
        if (tool->needsReference)
        {
            tool->chooseReference(window, QString::fromLatin1(kReferenceView));
        }
    };
    shared::expectMirrorPageAppliesAsTheWindow(
        tool->descriptorId, *context, [&]() { return sceneTarget(*context, *tool, "applied target"); }, how);
    if (reference != nullptr)
    {
        context->removeViewControl(reference.get(), false);
    }
}

/// The selection of a tool's background area drawn on the view the person works on - its target's own view in a window,
/// the page's view of it in a page - and the tool applied with it: what the run left in the target, and the area the
/// dialog read from the drag.
struct MarkedRun
{
    std::optional<fixtures::FixtureSamples> result;
    QRect area;
};

[[nodiscard]] MarkedRun markAndApply(ProcessTestContext& context, const ColorTool& tool, const bool asPage,
                                     const QRect& drawn)
{
    MarkedRun run;
    ViewHandle target = sceneTarget(context, tool, "marked target");
    EXPECT_NE(target, nullptr);
    if (target == nullptr)
    {
        return run;
    }
    activate(context, *target);
    PagePresenter presenter;
    presenter.adopting = asPage;
    context.presenter = &presenter;
    const QPointer<ImageProcessControl> process =
        dynamic_cast<ImageProcessControl*>(shared::launchWindowOf(tool.descriptorId, *target));
    EXPECT_NE(process, nullptr);
    if (process != nullptr)
    {
        EXPECT_EQ(process->hasPageLayout(), asPage);
        ImageViewControl* const marked = process->interactionView();
        EXPECT_EQ(marked == target.get(), !asPage) << "a page marks on its own view of the target";
        EXPECT_TRUE(settleView(*marked));
        clickGroup(*process, "backgroundRoiGroupBox", true);
        widgetOf<QPushButton>(*process, "backgroundRoiSelectionButton")->click();
        shared::dragNewSelection(*marked, drawn);
        EXPECT_EQ(marked->getViewWidget()->getCurrentSelectionArea(), drawn) << "the rectangle of the image drawn";
        run.area = QRect(widgetOf<QSpinBox>(*process, "backgroundRoiLeftValue")->value(),
                         widgetOf<QSpinBox>(*process, "backgroundRoiTopValue")->value(),
                         widgetOf<QSpinBox>(*process, "backgroundRoiWidthValue")->value(),
                         widgetOf<QSpinBox>(*process, "backgroundRoiHeightValue")->value());
        widgetOf<QPushButton>(*process, "backgroundRoiSelectionButton")->click();
        EXPECT_TRUE(process->triggerPrimaryAction());
        EXPECT_TRUE(pumpUntil([&]() { return shared::mirrorRunEnded(process, *target); }));
        run.result = documentSamples(target->document());
    }
    shared::closeOpenedProcessWindows();
    context.presenter = nullptr;
    dropView(context, target);
    return run;
}

/// A tool's background area drawn on the page's view gives the window's area and result.
void selectionOnThePageGivesTheWindowsResult(std::string_view name)
{
    const ColorTool* tool = familyTool(name);
    ASSERT_NE(tool, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    const QRect drawn(3, 2, 8, 5);
    const MarkedRun window = markAndApply(*context, *tool, false, drawn);
    const MarkedRun page = markAndApply(*context, *tool, true, drawn);
    EXPECT_EQ(window.area, drawn) << "the window's dialog reads the rectangle drawn";
    EXPECT_EQ(page.area, window.area) << "the page's dialog reads the same rectangle of the image";
    ASSERT_TRUE(window.result.has_value() && page.result.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*window.result, *page.result)) << "the page calibrates as the window does";
}

/// On a page a tool's background area is drawn on the page's view only: the target's own view shows no selection.
void targetViewShowsNoSelectionInPageMode(std::string_view name)
{
    const ColorTool* tool = familyTool(name);
    ASSERT_NE(tool, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = sceneTarget(*context, *tool, "page target");
    ASSERT_NE(target, nullptr);
    activate(*context, *target);
    {
        PagePresenter presenter;
        context->presenter = &presenter;
        auto* const process = dynamic_cast<ImageProcessControl*>(shared::launchWindowOf(tool->descriptorId, *target));
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(process->hasPageLayout());
        ImageViewControl* const marked = process->interactionView();
        ASSERT_NE(marked, target.get());
        clickGroup(*process, "backgroundRoiGroupBox", true);
        widgetOf<QPushButton>(*process, "backgroundRoiSelectionButton")->click();
        shared::dragNewSelection(*marked, QRect(2, 2, 6, 4));
        EXPECT_TRUE(marked->getViewWidget()->isSelectionAreaEnabled()) << "the page's view shows the area";
        EXPECT_EQ(marked->getViewWidget()->getImageViewSelectionStyle(), ImageViewSelectionStyle::AREA_SELECTION_STYLE);
        EXPECT_FALSE(target->getViewWidget()->isSelectionAreaEnabled()) << "the target's view shows none";
        EXPECT_NE(target->getViewWidget()->getImageViewSelectionStyle(), ImageViewSelectionStyle::AREA_SELECTION_STYLE);
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    dropView(*context, target);
}

} // namespace

#define AN_COLOR_TOOLS_PAGE(Name)              \
    TEST(PageLayout, Name##FitsItsColumn)      \
    {                                          \
        fitsItsColumn(#Name);                  \
    }                                          \
    TEST(PageLayout, Name##AppliesAsInAWindow) \
    {                                          \
        appliesAsInAWindow(#Name);             \
    }

AN_COLOR_TOOLS_PAGE(ColorCalibration)
AN_COLOR_TOOLS_PAGE(BackgroundNeutralization)
AN_COLOR_TOOLS_PAGE(LinearMatch)
AN_COLOR_TOOLS_PAGE(Scnr)

// Color Calibration's background area drawn on the page's view of the target is the area the same drag draws on the
// target's own view, and the page calibrates the image as the window does.
TEST(ColorCalibration, SelectionOnThePageGivesTheWindowsResult)
{
    selectionOnThePageGivesTheWindowsResult("ColorCalibration");
}

// On a page Color Calibration draws its area on the page's view only.
TEST(ColorCalibration, TargetViewShowsNoSelectionInPageMode)
{
    targetViewShowsNoSelectionInPageMode("ColorCalibration");
}

// Background Neutralization's area drawn on the page's view of the target is the area the same drag draws on the
// target's own view, and the page neutralizes the image as the window does.
TEST(BackgroundNeutralization, SelectionOnThePageGivesTheWindowsResult)
{
    selectionOnThePageGivesTheWindowsResult("BackgroundNeutralization");
}

// On a page Background Neutralization draws its area on the page's view only.
TEST(BackgroundNeutralization, TargetViewShowsNoSelectionInPageMode)
{
    targetViewShowsNoSelectionInPageMode("BackgroundNeutralization");
}
