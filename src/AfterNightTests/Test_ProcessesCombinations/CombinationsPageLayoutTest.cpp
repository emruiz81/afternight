// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The combinations launched on an image as pages: each in its column at 600 px of height, in both densities, with
/// every control in the column and reached by Tab and nothing clipped sideways where text is measured; each that
/// applies giving through its page the result its window gives for the same parameters and the same images beside its
/// target - in the target, or in the new image it makes; Image Compare, which applies nothing, comparing on its page as
/// in its window; and NB Color Mapper's controls scrolled by the page's body rather than by a scroll area of their own.

#include "Test_ProcessesCombinations/CombinationsTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"

#include "components/AnCollapsibleSection.h"
#include "processes/ImageCompareProcess.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/SplitComparisonDisplayModule.h"

#include <QAbstractButton>
#include <QScrollArea>
#include <QStringList>
#include <QTableWidget>
#include <QToolButton>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::combinations;

/// The process of an action; fails the case when the family has none.
[[nodiscard]] const CombinationsProcess* familyProcess(std::string_view action)
{
    const CombinationsProcess* process = processOfAction(action);
    EXPECT_NE(process, nullptr) << "no process of " << action;
    return process;
}

/// A new view of the process's scene, as its scene is opened for the family's cases.
[[nodiscard]] ViewHandle sceneTarget(ProcessTestContext& context, const CombinationsProcess& process,
                                     const std::string& name)
{
    const std::optional<an::tests::fixtures::FixtureSamples> scene = sceneSamples(process.scene);
    if (!scene.has_value())
    {
        return ViewHandle();
    }
    std::optional<an::consumer::ImageAsset> asset = sceneAsset(context.engineSession(), *scene);
    EXPECT_TRUE(asset.has_value());
    return asset.has_value() ? assetView(std::move(*asset), name) : ViewHandle();
}

/// The page of a process held to its column, over its scene with the images it reads open beside it; reported skipped
/// where text is not measured as the interface measures it.
void fitsItsColumn(std::string_view action)
{
    const CombinationsProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets scene = openScene(*context, *process);
    ASSERT_NE(scene.target, nullptr);
    context->addViewControl(scene.target.get());
    context->setActiveImageViewControl(scene.target.get());
    const bool measured = shared::expectPageFitsItsColumn(process->descriptorId, *scene.target, *context);
    dropTargets(*context, scene);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// The page of a process gives the window's result, its parameters set to the mode the family's window cases set and
/// its second images chosen among the same views.
void appliesAsInAWindow(std::string_view action)
{
    const CombinationsProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    // The images the process reads beside its target, opened once over the scene for both runs; the scene's own view is
    // let go, each run opening a target of its own.
    Targets sides = openScene(*context, *process);
    ASSERT_NE(sides.target, nullptr);
    dropView(*context, sides.target);
    shared::PageApply how;
    how.configure = [process, &sides](ImageRTPreviewControl& window) {
        process->configure(window, sides, process->changingMode);
    };
    how.resultIsANewImage = process->landing == ResultLanding::NewView;
    shared::expectPageAppliesAsTheWindow(
        process->descriptorId, *context,
        [&context, process]() { return sceneTarget(*context, *process, "applied target"); }, how);
    dropTargets(*context, sides);
}

/// Whether a comparison's split overlay and histograms have landed.
[[nodiscard]] bool settleComparison(const ImageCompareProcess& compare)
{
    return pumpUntil([&compare]() { return !compare.isOverlayPending() && compare.histogramsIdle(); });
}

} // namespace

#define AN_COMBINATIONS_PAGE_FITS(Name, Action) \
    TEST(PageLayout, Name##FitsItsColumn)       \
    {                                           \
        fitsItsColumn(Action);                  \
    }

#define AN_COMBINATIONS_PAGE_APPLIES(Name, Action) \
    TEST(PageLayout, Name##AppliesAsInAWindow)     \
    {                                              \
        appliesAsInAWindow(Action);                \
    }

AN_COMBINATIONS_PAGE_FITS(ImageBlend, "actionImage_Blend")
AN_COMBINATIONS_PAGE_APPLIES(ImageBlend, "actionImage_Blend")
AN_COMBINATIONS_PAGE_FITS(LRGBCombination, "actionLRGB_Combination")
AN_COMBINATIONS_PAGE_APPLIES(LRGBCombination, "actionLRGB_Combination")
AN_COMBINATIONS_PAGE_FITS(NBRGBCombination, "actionNBRGB_Combination")
AN_COMBINATIONS_PAGE_APPLIES(NBRGBCombination, "actionNBRGB_Combination")
AN_COMBINATIONS_PAGE_FITS(NBColorMapper, "actionNBColorMapper")
AN_COMBINATIONS_PAGE_APPLIES(NBColorMapper, "actionNBColorMapper")
AN_COMBINATIONS_PAGE_FITS(ImageCompare, "actionImage_Compare")

// Image Compare applies nothing: it compares. On its page as in its window the second image chosen in its column is the
// canvas's alternate, and a split chosen in the column lays the second image over the canvas; on the page the toggles
// row's Compare shows the second image in the first's place and names the image it shows. Its window's only button,
// Close, is the page's footer.
TEST(PageLayout, ImageCompareComparesAsInAWindow)
{
    const CombinationsProcess* process = familyProcess("actionImage_Compare");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets scene = openScene(*context, *process);
    ASSERT_NE(scene.target, nullptr);
    context->addViewControl(scene.target.get());
    context->setActiveImageViewControl(scene.target.get());
    ImageViewControl* const second = scene.side("second");
    ASSERT_NE(second, nullptr);
    for (const bool asPage : {false, true})
    {
        SCOPED_TRACE(asPage ? "in a page" : "in a window");
        PagePresenter presenter;
        presenter.adopting = asPage;
        context->presenter = &presenter;
        auto* const compare =
            dynamic_cast<ImageCompareProcess*>(shared::launchWindowOf(process->descriptorId, *scene.target));
        ASSERT_NE(compare, nullptr);
        EXPECT_EQ(compare->hasPageLayout(), asPage);
        selectView(*compare, "secondImageSelector", "second");
        ASSERT_TRUE(settleComparison(*compare));
        ImageViewControl* const preview = compare->previewView();
        ASSERT_NE(preview, nullptr) << "the comparison's preview is always on";
        ASSERT_NE(preview->getViewWidget(), nullptr);
        EXPECT_EQ(preview->alternateAsset(), second->document().asset());

        if (asPage)
        {
            an::ui::ProcessPageLayout* const page = compare->pageLayout();
            ASSERT_NE(page, nullptr);
            QAbstractButton* const close = page->footerButton(QDialogButtonBox::Close);
            ASSERT_NE(close, nullptr) << "the window's Close is the footer's";
            EXPECT_EQ(page->footerButton(QDialogButtonBox::Apply), nullptr) << "a comparison applies nothing";
            QAbstractButton* const shown = page->compareToggle();
            ASSERT_NE(shown, nullptr);
            ASSERT_TRUE(shown->isVisibleTo(compare));
            EXPECT_EQ(shown->text(), QStringLiteral("Image 1"));
            shown->click();
            pumpQuietly();
            EXPECT_TRUE(preview->getViewWidget()->isAlternateImageRenderEnabled()) << "the canvas shows the second";
            EXPECT_EQ(shown->text(), QStringLiteral("Image 2"));
            shown->click();
            pumpQuietly();
            EXPECT_FALSE(preview->getViewWidget()->isAlternateImageRenderEnabled());
        }

        auto* const split = widgetOf<QToolButton>(*compare, "horizontalSplitButton");
        ASSERT_NE(split, nullptr);
        if (asPage)
        {
            EXPECT_TRUE(compare->pageLayout()->bodyContent()->isAncestorOf(split)) << "the modes are in the column";
        }
        split->click();
        ASSERT_TRUE(settleComparison(*compare));
        ASSERT_NE(compare->splitModule(), nullptr);
        EXPECT_TRUE(compare->splitModule()->isDisplayModuleEnabled());
        EXPECT_TRUE(compare->splitModule()->hasOverlayImage()) << "the second image lies over the canvas";
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    dropTargets(*context, scene);
}

// NB Color Mapper scrolls its controls in a scroll area of its own beside its preview; on a page the body scrolls them,
// so they leave that area, and its groups are the page's sections, in order.
TEST(PageLayout, NBColorMapperScrollsItsControlsWithTheBody)
{
    const CombinationsProcess* process = familyProcess("actionNBColorMapper");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets scene = openScene(*context, *process);
    ASSERT_NE(scene.target, nullptr);
    context->addViewControl(scene.target.get());
    context->setActiveImageViewControl(scene.target.get());
    {
        PagePresenter presenter;
        context->presenter = &presenter;
        TopLevelProcessBaseControl* const page = shared::launchedPage(process->descriptorId, *scene.target);
        ASSERT_NE(page, nullptr);
        pumpQuietly();
        an::ui::ProcessPageLayout* const layout = page->pageLayout();
        QStringList sections;
        for (const an::ui::AnCollapsibleSection* section : layout->sections())
        {
            sections.append(section->title());
        }
        EXPECT_EQ(sections,
                  (QStringList{QStringLiteral("Histogram Controls"), QStringLiteral("Layers"),
                               QStringLiteral("Layer Settings"), QStringLiteral("Color Controls"),
                               QStringLiteral("Modification Controls"), QStringLiteral("Background Neutralization"),
                               QStringLiteral("Output Options")}));
        auto* const table = page->findChild<QTableWidget*>(QStringLiteral("layersTable"));
        ASSERT_NE(table, nullptr);
        for (const QScrollArea* area : layout->bodyContent()->findChildren<QScrollArea*>())
        {
            EXPECT_FALSE(area->isAncestorOf(table) && area->isVisibleTo(page))
                << area->objectName().toStdString() << " scrolls the controls inside the body";
        }
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    dropTargets(*context, scene);
}
