// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's process menus as the classic window's builder makes them, over the actions the record of the
/// interface holds: which menu each process action sits in, and under which name. What an action launches when it is
/// triggered, the icon it carries and the state it follows are the shell core's action bindings and action state,
/// held by the shell core's suite.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/AfterNightAppUiBuilder.h"
#include "fixtures/RecordedActions.h"

#include <QAction>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QString>

#include <gtest/gtest.h>

namespace
{

/// The classic window's menus, built over the actions the record holds.
class BuiltMenus
{
public:
    BuiltMenus() : m_actions(window), ui(an::ui::app::buildAfterNightAppUi(&window, m_actions.lookup())) {}

    QMainWindow window;

private:
    an::tests::fixtures::RecordedActions m_actions;

public:
    an::ui::app::AfterNightAppUiElements ui;
};

} // namespace

// The Color Calibration menu offers the manual, the photometric and the spectrophotometric calibration side by side
// under their own names - the photometric entry was added beside the manual one, not in its place.
TEST(ProcessMenuWiringTest, TheColourCalibrationMenuOffersTheThreeCalibrationsSideBySide)
{
    const BuiltMenus menus;

    const QList<QAction*> entries = menus.ui.menuColor_Calibration->actions();
    EXPECT_EQ(menus.ui.menuColor_Calibration->title(), QStringLiteral("Color Calibration"));
    EXPECT_TRUE(entries.contains(menus.ui.actionColor_Calibration_2));
    EXPECT_TRUE(entries.contains(menus.ui.actionPhotometric_Color_Calibration));
    EXPECT_TRUE(entries.contains(menus.ui.actionSpectrophotometric_Color_Calibration));
    EXPECT_EQ(menus.ui.actionColor_Calibration_2->text(), QStringLiteral("Color Calibration"));
    EXPECT_EQ(menus.ui.actionPhotometric_Color_Calibration->text(), QStringLiteral("Photometric Color Calibration"));
    EXPECT_EQ(menus.ui.actionSpectrophotometric_Color_Calibration->text(),
              QStringLiteral("Spectrophotometric Color Calibration"));
}

// Image Integration Lite sits in the Preprocess menu under its own name.
TEST(ProcessMenuWiringTest, ImageIntegrationLiteIsOfferedInThePreprocessMenu)
{
    const BuiltMenus menus;

    EXPECT_EQ(menus.ui.menuPreprocess->title(), QStringLiteral("Preprocess"));
    EXPECT_TRUE(menus.ui.menuPreprocess->actions().contains(menus.ui.actionImage_Integration_Lite));
    EXPECT_EQ(menus.ui.actionImage_Integration_Lite->text(), QStringLiteral("Image Integration Lite"));
}

// DeepSNR sits in the Noise Reduction menu.
TEST(ProcessMenuWiringTest, DeepSnrIsOfferedInTheNoiseReductionMenu)
{
    const BuiltMenus menus;

    EXPECT_EQ(menus.ui.menuNoise_Reduction->title(), QStringLiteral("Noise Reduction"));
    EXPECT_TRUE(menus.ui.menuNoise_Reduction->actions().contains(menus.ui.actionDeepSNR));
}

// The Process menu holds a Filters menu with the Gaussian, median, bilateral, banding and cosmetic filters, none of
// which the Noise Reduction menu repeats, and SCNR sits in the Color Calibration menu rather than Noise Reduction.
TEST(ProcessMenuWiringTest, TheFiltersAndScnrSitInTheirOwnMenus)
{
    const BuiltMenus menus;
    const an::ui::app::AfterNightAppUiElements& ui = menus.ui;

    EXPECT_EQ(ui.menuFilters->title(), QStringLiteral("Filters"));
    EXPECT_TRUE(ui.menuProcess->actions().contains(ui.menuFilters->menuAction()));
    const QList<QAction*> filters = ui.menuFilters->actions();
    const QList<QAction*> noiseReduction = ui.menuNoise_Reduction->actions();
    for (QAction* filter : {ui.actionGaussian_Blur, ui.actionMedian_Filter, ui.actionBilateral_Filter,
                            ui.actionBanding_Reduction_Filter, ui.actionCosmetic_Correction_Filter})
    {
        EXPECT_TRUE(filters.contains(filter)) << filter->objectName().toStdString();
        EXPECT_FALSE(noiseReduction.contains(filter)) << filter->objectName().toStdString();
    }
    EXPECT_TRUE(ui.menuColor_Calibration->actions().contains(ui.actionSCNR));
    EXPECT_FALSE(noiseReduction.contains(ui.actionSCNR));
}
