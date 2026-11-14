// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Controls library's windows and dialogs as their builders lay them out, each over a window of the case's own -
/// the star filters, the profiled star mask, the image tools, the selectors, the export, log viewer, autostretch and
/// rename dialogs, the system information dialog, the new sequence window and the main window's menus: margins and
/// spacing are the design tokens' steps, headings and code the tokens' faces, no widget sets a font of its own size,
/// and a window that resizes lays out everything it shows.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"
#include "Test_ProcessFramework/WindowLayoutChecks.h"

#include "app/AfterNightAppUiBuilder.h"
#include "fixtures/RecordedActions.h"
#include "controls/AutostretchSettingsControlUiBuilder.h"
#include "controls/ExportSequenceControlUiBuilder.h"
#include "controls/ImageEXIFViewerControlUiBuilder.h"
#include "controls/ImageFITSHeaderViewerControlUiBuilder.h"
#include "controls/ImageHistogramControlUiBuilder.h"
#include "controls/ImageHistoryControlUiBuilder.h"
#include "controls/ImageMaskSelectorControlUiBuilder.h"
#include "controls/ImageRenameControlUiBuilder.h"
#include "controls/ImageSelectorControlUiBuilder.h"
#include "controls/ImageStatisticsControlUiBuilder.h"
#include "controls/ImageSummaryViewerControlUiBuilder.h"
#include "controls/LogViewerFindTextControlUiBuilder.h"
#include "controls/LogViewerSettingsControlUiBuilder.h"
#include "controls/NewSequenceControl.h"
#include "controls/ProfiledStarMaskCreatorControlUiBuilder.h"
#include "controls/ProgressDialogControlUiBuilder.h"
#include "controls/StarsFilterControlV1UiBuilder.h"
#include "controls/StarsFilterControlV2UiBuilder.h"
#include "controls/SystemInformationDialogUiBuilder.h"
#include "theme/AnDesignTokens.h"

#include <QCoreApplication>
#include <QLayout>
#include <QMainWindow>
#include <QMargins>
#include <QMenuBar>
#include <QSize>
#include <QSizePolicy>
#include <QStringList>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QWidget>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

namespace
{

using an::tests::processes::pumpUntil;
using an::tests::processes::framework::fontsOutsideTheTokens;
using an::tests::processes::framework::widgetsOutsideTheLayout;

namespace controls = an::ui::controls;

/// A window the case owns with a builder's interface built over it, and whether the window resizes with the person.
struct BuiltWindow
{
    const char* name;
    std::function<void(QWidget&)> build;
    bool resizable;
};

/// Every Controls window and dialog laid out by a builder of its own.
[[nodiscard]] std::vector<BuiltWindow> everyControlsWindow()
{
    return {
        {"StarsFilterV1", [](QWidget& window) { (void)controls::buildStarsFilterControlV1Ui(&window); }, true},
        {"StarsFilterV2", [](QWidget& window) { (void)controls::buildStarsFilterControlV2Ui(&window); }, true},
        {"ProfiledStarMaskCreator",
         [](QWidget& window) { (void)controls::buildProfiledStarMaskCreatorControlUi(&window); }, true},
        {"ImageHistogram", [](QWidget& window) { (void)controls::buildImageHistogramControlUi(&window); }, false},
        {"ImageSummaryViewer", [](QWidget& window) { (void)controls::buildImageSummaryViewerControlUi(&window); },
         false},
        {"ImageHistory", [](QWidget& window) { (void)controls::buildImageHistoryControlUi(&window); }, false},
        {"ImageFITSHeaderViewer", [](QWidget& window) { (void)controls::buildImageFITSHeaderViewerControlUi(&window); },
         false},
        {"ImageEXIFViewer", [](QWidget& window) { (void)controls::buildImageEXIFViewerControlUi(&window); }, false},
        {"ImageStatistics", [](QWidget& window) { (void)controls::buildImageStatisticsControlUi(&window); }, false},
        {"ImageMaskSelector", [](QWidget& window) { (void)controls::buildImageMaskSelectorControlUi(&window); }, false},
        {"ProgressDialog", [](QWidget& window) { (void)controls::buildProgressDialogControlUi(&window); }, false},
        {"ImageSelector", [](QWidget& window) { (void)controls::buildImageSelectorControlUi(&window); }, true},
        {"ExportSequence", [](QWidget& window) { (void)controls::buildExportSequenceControlUi(&window); }, true},
        {"LogViewerSettings", [](QWidget& window) { (void)controls::buildLogViewerSettingsControlUi(&window); }, true},
        {"AutostretchSettings", [](QWidget& window) { (void)controls::buildAutostretchSettingsControlUi(&window); },
         true},
        {"LogViewerFindText", [](QWidget& window) { (void)controls::buildLogViewerFindTextControlUi(&window); }, true},
        {"ImageRename", [](QWidget& window) { (void)controls::buildImageRenameControlUi(&window); }, true},
        {"SystemInformationDialog", [](QWidget& window) { (void)controls::buildSystemInformationDialogUi(&window); },
         true},
    };
}

/// A new sequence window, deleted with the case once what it started has landed.
struct NewSequenceDeleter
{
    void operator()(NewSequenceControl* window) const
    {
        if (window == nullptr)
        {
            return;
        }
        (void)pumpUntil([window]() { return window->isIdle(); });
        delete window;
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    }
};

} // namespace

// No Controls window sets a font of its own size: every widget that chooses its font takes one of the design tokens'
// faces, in points.
TEST(ControlsWindowLayoutTest, EveryControlsWindowTakesItsFontsFromTheTokens)
{
    for (const BuiltWindow& built : everyControlsWindow())
    {
        QWidget window;
        built.build(window);
        const QStringList outside = fontsOutsideTheTokens(window);
        EXPECT_TRUE(outside.isEmpty()) << built.name << ": " << outside.join(QStringLiteral(", ")).toStdString();
    }

    QMainWindow mainWindow;
    const an::tests::fixtures::RecordedActions actions(mainWindow);
    (void)an::ui::app::buildAfterNightAppUi(&mainWindow, actions.lookup());
    const QStringList outside = fontsOutsideTheTokens(mainWindow);
    EXPECT_TRUE(outside.isEmpty()) << "main window: " << outside.join(QStringLiteral(", ")).toStdString();
}

// A Controls window that resizes lays out everything it shows: no widget sits at a fixed geometry outside the
// window's layout.
TEST(ControlsWindowLayoutTest, EveryResizableControlsWindowLaysOutWhatItShows)
{
    int resizable = 0;
    for (const BuiltWindow& built : everyControlsWindow())
    {
        if (!built.resizable)
        {
            continue;
        }
        ++resizable;
        QWidget window;
        built.build(window);
        ASSERT_NE(window.layout(), nullptr) << built.name;
        const QStringList outside = widgetsOutsideTheLayout(window);
        EXPECT_TRUE(outside.isEmpty()) << built.name << ": " << outside.join(QStringLiteral(", ")).toStdString();
    }
    EXPECT_EQ(resizable, 10);
}

// The star filter windows space their grids in the tokens' steps.
TEST(ControlsWindowLayoutTest, StarsFilterWindowsUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget firstHost;
    const auto firstUi = controls::buildStarsFilterControlV1Ui(&firstHost);
    ASSERT_NE(firstUi.gridLayout1, nullptr);
    ASSERT_NE(firstUi.gridLayout1_2, nullptr);
    ASSERT_NE(firstUi.gridLayout1_3, nullptr);
    ASSERT_NE(firstUi.groupBox, nullptr);
    ASSERT_NE(firstUi.groupBox_2, nullptr);
    ASSERT_NE(firstHost.layout(), nullptr);
    EXPECT_EQ(firstUi.gridLayout1->spacing(), t.spacing.md);
    EXPECT_EQ(firstUi.gridLayout1->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(firstUi.gridLayout1_2->spacing(), t.spacing.md);
    EXPECT_EQ(firstUi.gridLayout1_3->spacing(), t.spacing.md);

    QWidget secondHost;
    const auto secondUi = controls::buildStarsFilterControlV2Ui(&secondHost);
    ASSERT_NE(secondUi.gridLayout1, nullptr);
    ASSERT_NE(secondUi.gridLayout1_2, nullptr);
    ASSERT_NE(secondUi.gridLayout1_3, nullptr);
    ASSERT_NE(secondUi.groupBox, nullptr);
    ASSERT_NE(secondUi.groupBox_2, nullptr);
    ASSERT_NE(secondHost.layout(), nullptr);
    EXPECT_EQ(secondUi.gridLayout1->spacing(), t.spacing.md);
    EXPECT_EQ(secondUi.gridLayout1->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(secondUi.gridLayout1_2->spacing(), t.spacing.md);
    EXPECT_EQ(secondUi.gridLayout1_3->spacing(), t.spacing.md);
}

// The profiled star mask window spaces its settings in the tokens' steps.
TEST(ControlsWindowLayoutTest, ProfiledStarMaskCreatorUsesTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = controls::buildProfiledStarMaskCreatorControlUi(&host);
    ASSERT_NE(host.layout(), nullptr);
    ASSERT_NE(ui.gridLayout, nullptr);
    ASSERT_NE(ui.gridLayout_2, nullptr);
    EXPECT_EQ(host.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(host.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.gridLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(ui.gridLayout_2->verticalSpacing(), t.spacing.sm);
}

// The histogram and summary tools set their margins and spacing in the tokens' steps.
TEST(ControlsWindowLayoutTest, HistogramAndSummaryUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget histogramHost;
    const auto histogramUi = controls::buildImageHistogramControlUi(&histogramHost);
    ASSERT_NE(histogramHost.layout(), nullptr);
    ASSERT_NE(histogramUi.verticalLayout, nullptr);
    EXPECT_EQ(histogramHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(histogramHost.layout()->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(histogramUi.verticalLayout->spacing(), t.spacing.md);

    QWidget summaryHost;
    const auto summaryUi = controls::buildImageSummaryViewerControlUi(&summaryHost);
    ASSERT_NE(summaryUi.verticalLayout, nullptr);
    ASSERT_NE(summaryUi.summaryContentLayout, nullptr);
    EXPECT_EQ(summaryUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(summaryUi.verticalLayout->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(summaryUi.summaryContentLayout->spacing(), t.spacing.md);
    EXPECT_EQ(summaryUi.summaryContentLayout->contentsMargins().left(), t.spacing.sm);
}

// The history, FITS header, Exif and statistics tools, the mask selector and the progress dialog set their outer
// margins and spacing in the tokens' steps.
TEST(ControlsWindowLayoutTest, ImageToolsAndProgressUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget historyHost;
    const auto historyUi = controls::buildImageHistoryControlUi(&historyHost);
    ASSERT_NE(historyUi.verticalLayout, nullptr);
    ASSERT_NE(historyUi.contentLayout, nullptr);
    EXPECT_EQ(historyUi.verticalLayout->contentsMargins(),
              QMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.md));
    EXPECT_EQ(historyUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(historyUi.contentLayout->spacing(), t.spacing.md);

    QWidget headerHost;
    const auto headerUi = controls::buildImageFITSHeaderViewerControlUi(&headerHost);
    ASSERT_NE(headerUi.verticalLayout_2, nullptr);
    ASSERT_NE(headerUi.verticalLayout, nullptr);
    EXPECT_EQ(headerUi.verticalLayout_2->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(headerUi.verticalLayout_2->spacing(), t.spacing.md);
    EXPECT_EQ(headerUi.verticalLayout->spacing(), t.spacing.md);

    QWidget exifHost;
    const auto exifUi = controls::buildImageEXIFViewerControlUi(&exifHost);
    ASSERT_NE(exifUi.verticalLayout_2, nullptr);
    ASSERT_NE(exifUi.verticalLayout, nullptr);
    EXPECT_EQ(exifUi.verticalLayout_2->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(exifUi.verticalLayout_2->spacing(), t.spacing.md);
    EXPECT_EQ(exifUi.verticalLayout->spacing(), t.spacing.md);

    QWidget statisticsHost;
    const auto statisticsUi = controls::buildImageStatisticsControlUi(&statisticsHost);
    ASSERT_NE(statisticsUi.mainLayout, nullptr);
    EXPECT_EQ(statisticsUi.mainLayout->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(statisticsUi.mainLayout->spacing(), t.spacing.md);

    QWidget maskHost;
    const auto maskUi = controls::buildImageMaskSelectorControlUi(&maskHost);
    ASSERT_NE(maskUi.verticalLayout, nullptr);
    ASSERT_NE(maskUi.formLayout, nullptr);
    EXPECT_EQ(maskUi.verticalLayout->contentsMargins(),
              QMargins(t.spacing.md, t.spacing.xs, t.spacing.md, t.spacing.sm));
    EXPECT_EQ(maskUi.verticalLayout->spacing(), t.spacing.xs);
    EXPECT_EQ(maskUi.formLayout->horizontalSpacing(), t.spacing.md);

    QWidget progressHost;
    const auto progressUi = controls::buildProgressDialogControlUi(&progressHost);
    ASSERT_NE(progressUi.verticalLayout, nullptr);
    EXPECT_EQ(progressUi.verticalLayout->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(progressUi.verticalLayout->spacing(), t.spacing.md);
}

// The image selector lays its content in the tokens' margins and is not capped in size.
TEST(ControlsWindowLayoutTest, ImageSelectorUsesTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = controls::buildImageSelectorControlUi(&host);
    ASSERT_NE(host.layout(), nullptr);
    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.horizontalLayout, nullptr);
    EXPECT_EQ(host.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(host.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(ui.verticalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.verticalLayout->contentsMargins().top(), t.spacing.sm);
    EXPECT_EQ(ui.horizontalLayout->contentsMargins().top(), t.spacing.xs);
    EXPECT_NE(host.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    EXPECT_NE(host.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    EXPECT_GT(host.maximumSize().width(), 10000);
    EXPECT_GT(host.maximumSize().height(), 10000);
}

// The export and log viewer settings dialogs lay their content in the tokens' margins and resize; the export starts
// with the filter array's debayering off. The autostretch settings dialog keeps its fixed size.
TEST(ControlsWindowLayoutTest, ExportLogViewerAndAutostretchUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget exportHost;
    const auto exportUi = controls::buildExportSequenceControlUi(&exportHost);
    ASSERT_NE(exportHost.layout(), nullptr);
    ASSERT_NE(exportUi.verticalLayout, nullptr);
    ASSERT_NE(exportUi.SettignsLayout, nullptr);
    ASSERT_NE(exportUi.debayerCfaCheckBox, nullptr);
    EXPECT_FALSE(exportUi.debayerCfaCheckBox->isChecked());
    EXPECT_EQ(exportHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(exportHost.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(exportUi.verticalLayout->contentsMargins().left(), t.spacing.xs);
    EXPECT_EQ(exportUi.SettignsLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_NE(exportHost.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    EXPECT_NE(exportHost.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    EXPECT_GT(exportHost.maximumSize().width(), 10000);
    EXPECT_GT(exportHost.maximumSize().height(), 10000);

    QWidget logViewerHost;
    const auto logViewerUi = controls::buildLogViewerSettingsControlUi(&logViewerHost);
    ASSERT_NE(logViewerHost.layout(), nullptr);
    ASSERT_NE(logViewerUi.verticalLayout, nullptr);
    EXPECT_EQ(logViewerHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(logViewerUi.verticalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(logViewerUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_GT(logViewerHost.maximumSize().width(), 10000);
    EXPECT_GT(logViewerHost.maximumSize().height(), 10000);

    QWidget autostretchHost;
    const auto autostretchUi = controls::buildAutostretchSettingsControlUi(&autostretchHost);
    ASSERT_NE(autostretchHost.layout(), nullptr);
    ASSERT_NE(autostretchUi.verticalLayout, nullptr);
    ASSERT_NE(autostretchUi.gridLayout, nullptr);
    ASSERT_NE(autostretchUi.gridLayout_2, nullptr);
    EXPECT_EQ(autostretchHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(autostretchUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(autostretchUi.verticalLayout->contentsMargins().left(), 0);
    EXPECT_EQ(autostretchUi.gridLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(autostretchUi.gridLayout_2->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(autostretchHost.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    EXPECT_EQ(autostretchHost.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    EXPECT_EQ(autostretchHost.maximumSize(), QSize(500, 550));
    autostretchHost.resize(920, 680);
    EXPECT_EQ(autostretchHost.size(), QSize(500, 550));
}

// The log viewer's find dialog and the rename dialog lay their content in the tokens' margins and resize.
TEST(ControlsWindowLayoutTest, LogViewerFindAndImageRenameUseResponsiveLayouts)
{
    const auto& t = an::ui::tokens();

    QWidget findHost;
    const auto findUi = controls::buildLogViewerFindTextControlUi(&findHost);
    ASSERT_NE(findHost.layout(), nullptr);
    ASSERT_NE(findUi.horizontalLayout, nullptr);
    ASSERT_NE(findUi.horizontalLayout_2, nullptr);
    ASSERT_NE(findUi.horizontalLayout_3, nullptr);
    EXPECT_EQ(findHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(findHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(findUi.horizontalLayout_2->spacing(), t.spacing.md);
    EXPECT_EQ(findUi.horizontalLayout_3->spacing(), t.spacing.sm);
    EXPECT_GT(findHost.maximumSize().width(), 10000);
    EXPECT_GT(findHost.maximumSize().height(), 10000);

    QWidget renameHost;
    const auto renameUi = controls::buildImageRenameControlUi(&renameHost);
    ASSERT_NE(renameHost.layout(), nullptr);
    ASSERT_NE(renameUi.buttonBox, nullptr);
    ASSERT_NE(renameUi.newImageName, nullptr);
    EXPECT_EQ(renameHost.layout()->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(renameHost.layout()->spacing(), t.spacing.md);
    EXPECT_GT(renameHost.maximumSize().width(), 10000);
    EXPECT_GT(renameHost.maximumSize().height(), 10000);
}

// The system information dialog heads itself in the second heading face as plain text, its sections in the third,
// shows its dependency and display reports in the monospace face, and resizes around its scrolled content.
TEST(ControlsWindowLayoutTest, SystemInformationDialogUsesTokenTypography)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = controls::buildSystemInformationDialogUi(&host);
    ASSERT_NE(host.layout(), nullptr);
    ASSERT_NE(ui.titleLabel, nullptr);
    ASSERT_NE(ui.osNameLabel, nullptr);
    ASSERT_NE(ui.appVersionLabel, nullptr);
    ASSERT_NE(ui.processorLabel, nullptr);
    ASSERT_NE(ui.depsTextEdit, nullptr);
    ASSERT_NE(ui.displayTextEdit, nullptr);
    ASSERT_NE(ui.scrollAreaWidgetContents, nullptr);
    EXPECT_EQ(ui.titleLabel->font(), t.typography.heading2);
    EXPECT_EQ(ui.titleLabel->textFormat(), Qt::PlainText);
    EXPECT_EQ(ui.osNameLabel->font(), t.typography.heading3);
    EXPECT_EQ(ui.appVersionLabel->font(), t.typography.heading3);
    EXPECT_EQ(ui.processorLabel->font(), t.typography.heading3);
    EXPECT_EQ(ui.depsTextEdit->font(), t.typography.monospace);
    EXPECT_EQ(ui.displayTextEdit->font(), t.typography.monospace);
    EXPECT_EQ(ui.scrollAreaWidgetContents->minimumSize(), QSize(780, 620));
    EXPECT_NE(host.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    EXPECT_NE(host.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    host.resize(980, 760);
    EXPECT_GE(host.width(), 980);
    EXPECT_GE(host.height(), 760);
}

// The new sequence window spaces its file list and its settings in the tokens' steps.
TEST(ControlsWindowLayoutTest, NewSequenceWindowUsesTokenSpacing)
{
    const auto& t = an::ui::tokens();

    const std::unique_ptr<NewSequenceControl, NewSequenceDeleter> window(new NewSequenceControl());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    const auto* files = window->findChild<QVBoxLayout*>(QStringLiteral("verticalLayout_Tab1"));
    const auto* settings = window->findChild<QVBoxLayout*>(QStringLiteral("tab2Layout"));
    const auto* input = window->findChild<QVBoxLayout*>(QStringLiteral("inputSettingsVerticalLayout"));
    const auto* output = window->findChild<QVBoxLayout*>(QStringLiteral("outputSettingsVerticalLayout"));
    ASSERT_NE(files, nullptr);
    ASSERT_NE(settings, nullptr);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(files->spacing(), t.spacing.md);
    EXPECT_EQ(files->contentsMargins(), QMargins(t.spacing.md, t.spacing.md, t.spacing.sm, t.spacing.md));
    EXPECT_EQ(settings->contentsMargins(), QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
    EXPECT_EQ(input->contentsMargins(), QMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm));
    EXPECT_EQ(output->contentsMargins(), QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
    const QStringList outside = fontsOutsideTheTokens(*window);
    EXPECT_TRUE(outside.isEmpty()) << outside.join(QStringLiteral(", ")).toStdString();
}

// The main window is built with its menu bar, tool bar, status bar and central stack in place, the menu bar holding
// the application's menus.
TEST(ControlsWindowLayoutTest, TheMainWindowsMenusAreBuilt)
{
    QMainWindow host;
    const an::tests::fixtures::RecordedActions actions(host);
    const auto ui = an::ui::app::buildAfterNightAppUi(&host, actions.lookup());
    ASSERT_NE(ui.centralWidget, nullptr);
    ASSERT_NE(ui.menuBar, nullptr);
    ASSERT_NE(ui.mainToolBar, nullptr);
    ASSERT_NE(ui.statusBar, nullptr);
    EXPECT_EQ(host.menuBar(), ui.menuBar);
    EXPECT_EQ(host.centralWidget(), ui.centralWidget);
    EXPECT_GT(ui.menuBar->actions().size(), 0);
}
