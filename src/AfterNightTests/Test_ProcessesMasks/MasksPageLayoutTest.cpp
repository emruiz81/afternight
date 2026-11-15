// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The mask, star and background processes as pages: each in its column at 600 px of height, in both densities, with
/// every control in the column and reached by Tab and nothing clipped sideways where text is measured; and each giving
/// through its page the result its window gives for the same parameters - in the target, or in the new image it makes.

#include "Test_ProcessesMasks/MasksTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"

#include "components/AnCollapsibleSection.h"

#include <QLabel>
#include <QStringList>
#include <QTreeWidget>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::masks;

/// The process of an action; fails the case when the family has none.
[[nodiscard]] const MasksProcess* familyProcess(std::string_view action)
{
    const MasksProcess* process = processOfAction(action);
    EXPECT_NE(process, nullptr) << "no process of " << action;
    return process;
}

/// The starless image a process that reads one finds beside its target, listed among the context's views; none for a
/// process that reads none.
[[nodiscard]] ViewHandle starlessBeside(ProcessTestContext& context, const MasksProcess& process)
{
    if (!process.needsStarless)
    {
        return nullptr;
    }
    ViewHandle starless = sceneView(context.engineSession(), std::string(process.scene) + "-starless", "starless");
    EXPECT_NE(starless, nullptr);
    if (starless != nullptr)
    {
        context.addViewControl(starless.get());
    }
    return starless;
}

/// The page of a process held to its column; reported skipped where text is not measured as the interface measures it.
void fitsItsColumn(std::string_view action)
{
    const MasksProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle starless = starlessBeside(*context, *process);
    ViewHandle target = sceneView(context->engineSession(), process->scene, "page target");
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());
    const bool measured = shared::expectPageFitsItsColumn(process->descriptorId, *target, *context);
    dropView(*context, target);
    dropView(*context, starless);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// The page of a process gives the window's result, its parameters set to the mode the family's window cases set.
void appliesAsInAWindow(std::string_view action)
{
    const MasksProcess* process = familyProcess(action);
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle starless = starlessBeside(*context, *process);
    shared::PageApply how;
    how.configure = [process](ImageRTPreviewControl& window) { process->configure(window, process->changingMode); };
    how.resultIsANewImage = process->landing == ResultLanding::NewView;
    shared::expectPageAppliesAsTheWindow(
        process->descriptorId, *context,
        [&context, process]() { return sceneView(context->engineSession(), process->scene, "applied target"); }, how);
    dropView(*context, starless);
}

} // namespace

#define AN_MASKS_PAGE_CASES(Name, Action)      \
    TEST(PageLayout, Name##FitsItsColumn)      \
    {                                          \
        fitsItsColumn(Action);                 \
    }                                          \
    TEST(PageLayout, Name##AppliesAsInAWindow) \
    {                                          \
        appliesAsInAWindow(Action);            \
    }

AN_MASKS_PAGE_CASES(RangeSelection, "actionRange_Selection")
AN_MASKS_PAGE_CASES(Binarize, "actionBinarize")
AN_MASKS_PAGE_CASES(StarMask, "actionStar_Mask")
AN_MASKS_PAGE_CASES(StarReduction, "actionStar_Reduction")
AN_MASKS_PAGE_CASES(MultiscaleLinearTransform, "actionMultiscale_Linear_Transform")
AN_MASKS_PAGE_CASES(AutoGradientRemoval, "actionAuto_Gradient_Removal")
AN_MASKS_PAGE_CASES(BackgroundExtraction, "actionBackground_Extraction")
AN_MASKS_PAGE_CASES(MaskDrawing, "actionMask_Drawing")
AN_MASKS_PAGE_CASES(DarkStructureEnhance, "actionDark_Structure_Enhance")

// Multiscale Linear Transform keeps the selected layer's controls, the target and what the preview shows beside its
// preview in a window; on a page they are in the column - the selected layer's section right after the layers it is
// chosen from - and the canvas holds the preview alone.
TEST(PageLayout, MultiscaleLinearTransformKeepsItsLayerControlsBesideItsLayers)
{
    const MasksProcess* process = familyProcess("actionMultiscale_Linear_Transform");
    ASSERT_NE(process, nullptr);
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = sceneView(context->engineSession(), process->scene, "layers target");
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());
    {
        PagePresenter presenter;
        context->presenter = &presenter;
        TopLevelProcessBaseControl* const page = shared::launchedPage(process->descriptorId, *target);
        ASSERT_NE(page, nullptr);
        pumpQuietly();
        an::ui::ProcessPageLayout* const layout = page->pageLayout();
        QStringList sections;
        for (const an::ui::AnCollapsibleSection* section : layout->sections())
        {
            sections.append(section->title());
        }
        EXPECT_EQ(sections, (QStringList{QStringLiteral("Transform Settings"), QStringLiteral("Layers"),
                                         QStringLiteral("Selected Layer"), QStringLiteral("Global Settings")}));
        for (const char* name :
             {"layerInspectorGroup", "previewModeComboBox", "targetImageSelector", "channelSelector"})
        {
            auto* const widget = page->findChild<QWidget*>(QString::fromLatin1(name));
            ASSERT_NE(widget, nullptr) << name;
            EXPECT_TRUE(layout->bodyContent()->isAncestorOf(widget)) << name << " is in the column's body";
        }

        // A layer chosen in the column is the one its section edits.
        auto* const layers = page->findChild<QTreeWidget*>(QStringLiteral("layerTreeWidget"));
        auto* const chosen = page->findChild<QLabel*>(QStringLiteral("layerNameLabel"));
        ASSERT_TRUE(layers != nullptr && chosen != nullptr);
        ASSERT_GT(layers->topLevelItemCount(), 1);
        layers->setCurrentItem(layers->topLevelItem(1));
        pumpQuietly();
        EXPECT_TRUE(chosen->text().contains(layers->topLevelItem(1)->text(1))) << chosen->text().toStdString();
        shared::closeOpenedProcessWindows();
        context->presenter = nullptr;
    }
    dropView(*context, target);
}
