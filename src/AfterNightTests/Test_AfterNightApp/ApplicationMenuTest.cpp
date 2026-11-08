// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's menus, invoked on the application's own main window as a person invokes them:
// the file menu's open and saves through the application's file dialog, the image menu's closes, rename, history,
// zoom and Show in File Browser over the active view and in the image's context menu, its tool windows, the mask menu
// over a view with a mask, and the application menu's windows. Each case triggers the action the menu holds and
// observes what it did - a view made or gone, a file written, a window shown, a view's state changed - and answers the
// dialogs the action opens the way a person would.

#include "ApplicationDriving.h"
#include "app/ProcessRegistry.h"
#include "controls/AboutDialog.h"
#include "controls/IAppContext.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageHistoryControl.h"
#include "controls/ImageRenameControl.h"
#include "controls/ImageStatisticsControl.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageFITSHeaderViewerControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/NewSequenceControl.h"
#include "controls/PlateSolveControl.h"
#include "controls/SettingsDialog.h"
#include "controls/SystemInformationDialog.h"
#include "controls/settings/ExtensionManagerPage.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"
#include "widgets/MaskRenderer.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

using an::tests::app::Answered;
using an::tests::app::answerEveryMessageBox;
using an::tests::app::answerTheNextMessageBox;
using an::tests::app::answerTheNextModalDialog;
using an::tests::app::answerWhenShown;
using an::tests::app::chooseInTheNextFileDialog;
using an::tests::app::isA;
using an::tests::app::isNamed;
using an::tests::app::processEventsUntil;
using an::tests::app::viewSample;
using an::tests::app::visibleChildWindows;
using an::tests::app::visibleWidget;

class ApplicationMenu : public an::tests::app::RealWindowCase
{
};

// ---- The file menu.

TEST_F(ApplicationMenu, OpenShowsTheChosenFileInAViewOfItsOwn)
{
    const QString path = filePath(QStringLiteral("opened.fits"));
    writeFile(path, an::CodecFormat::Fits, 0.375f);
    const std::size_t before = views().size();

    Answered dialog;
    chooseInTheNextFileDialog({path}, &dialog);
    invoke("actionOpen");

    EXPECT_TRUE(dialog.seen);
    EXPECT_FALSE(dialog.refused) << dialog.refusal.toStdString();
    ASSERT_TRUE(processEventsUntil([&]() { return views().size() == before + 1u; }));
    ImageViewControl* opened = nullptr;
    for (ImageViewControl* view : views())
    {
        if (QDir::cleanPath(view->document().names().sourcePath) == QDir::cleanPath(path))
        {
            opened = view;
        }
    }
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(opened->document().names().displayName, QStringLiteral("opened"));
    EXPECT_NEAR(viewSample(*opened), 0.375, 1e-6);
}

TEST_F(ApplicationMenu, SaveAsWritesTheActiveViewToTheChosenFileAndItReopensTheSame)
{
    ImageViewControl* const view = makeView(QStringLiteral("saved"), 0.625f);
    ASSERT_NE(view, nullptr);
    const QString path = filePath(QStringLiteral("saved-as.fits"));

    Answered dialog;
    chooseInTheNextFileDialog({path}, &dialog);
    invoke("actionSave_as");

    EXPECT_TRUE(dialog.seen);
    EXPECT_FALSE(dialog.refused) << dialog.refusal.toStdString();
    ASSERT_TRUE(processEventsUntil([&]() { return QFileInfo::exists(path); }));
    EXPECT_NEAR(fileSample(path), 0.625, 1e-6);
}

TEST_F(ApplicationMenu, SaveWritesTheActiveViewThroughTheSaveDialogToo)
{
    ImageViewControl* const view = makeView(QStringLiteral("saved"), 0.125f);
    ASSERT_NE(view, nullptr);
    const QString path = filePath(QStringLiteral("saved.xisf"));

    Answered dialog;
    chooseInTheNextFileDialog({path}, &dialog);
    invoke("actionSave_File");

    EXPECT_TRUE(dialog.seen);
    EXPECT_FALSE(dialog.refused) << dialog.refusal.toStdString();
    ASSERT_TRUE(processEventsUntil([&]() { return QFileInfo::exists(path); }));
    EXPECT_NEAR(fileSample(path), 0.125, 1e-6);
}

// ---- The image menu: closing, renaming, the history.

TEST_F(ApplicationMenu, CloseClosesTheActiveViewOnly)
{
    ImageViewControl* const kept = makeView(QStringLiteral("kept"));
    ImageViewControl* const closed = makeView(QStringLiteral("closed"));
    ASSERT_NE(kept, nullptr);
    ASSERT_NE(closed, nullptr);
    ASSERT_EQ(activeView(), closed);
    const QPointer<ImageViewControl> watched(closed);

    answerEveryMessageBox(std::make_shared<Answered>());
    invoke("actionClose");

    ASSERT_TRUE(processEventsUntil([&]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return watched.isNull();
    }));
    const std::vector<ImageViewControl*> left = views();
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left.front(), kept);
}

TEST_F(ApplicationMenu, TheSecondCloseEntryClosesTheActiveViewToo)
{
    ImageViewControl* const kept = makeView(QStringLiteral("kept"));
    ImageViewControl* const closed = makeView(QStringLiteral("closed"));
    ASSERT_NE(kept, nullptr);
    ASSERT_NE(closed, nullptr);
    const QPointer<ImageViewControl> watched(closed);

    answerEveryMessageBox(std::make_shared<Answered>());
    invoke("actionClose_2");

    ASSERT_TRUE(processEventsUntil([&]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return watched.isNull();
    }));
    ASSERT_EQ(views().size(), 1u);
    EXPECT_EQ(views().front(), kept);
}

TEST_F(ApplicationMenu, CloseAllClosesEveryView)
{
    ASSERT_NE(makeView(QStringLiteral("first")), nullptr);
    ASSERT_NE(makeView(QStringLiteral("second")), nullptr);
    ASSERT_NE(makeView(QStringLiteral("third")), nullptr);

    answerEveryMessageBox(std::make_shared<Answered>());
    invoke("actionClose_All");

    EXPECT_TRUE(processEventsUntil([&]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return views().empty();
    }));
}

TEST_F(ApplicationMenu, CloseAllOthersKeepsOnlyTheActiveView)
{
    ASSERT_NE(makeView(QStringLiteral("first")), nullptr);
    ASSERT_NE(makeView(QStringLiteral("second")), nullptr);
    ImageViewControl* const kept = makeView(QStringLiteral("kept"));
    ASSERT_NE(kept, nullptr);

    answerEveryMessageBox(std::make_shared<Answered>());
    invoke("actionClose_All_Others");

    ASSERT_TRUE(processEventsUntil([&]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return views().size() == 1u;
    }));
    EXPECT_EQ(views().front(), kept);
}

TEST_F(ApplicationMenu, RenameOpensTheRenameWindowOverTheActiveViewAndItRenamesTheView)
{
    ImageViewControl* const view = makeView(QStringLiteral("before"));
    ASSERT_NE(view, nullptr);

    invoke("actionRename");

    QWidget* window = nullptr;
    ASSERT_TRUE(processEventsUntil([&]() {
        for (QWidget* child : visibleChildWindows(*view))
        {
            if (isA<ImageRenameControl>(child))
            {
                window = child;
            }
        }
        return window != nullptr;
    }));
    EXPECT_TRUE(window->windowTitle().contains(QString::fromStdString(view->getUniqueName())));
    auto* name = window->findChild<QLineEdit*>();
    auto* buttons = window->findChild<QDialogButtonBox*>();
    ASSERT_NE(name, nullptr);
    ASSERT_NE(buttons, nullptr);
    name->setText(QStringLiteral("renamed by the menu"));
    QAbstractButton* const ok = buttons->button(QDialogButtonBox::Ok) != nullptr
                                    ? static_cast<QAbstractButton*>(buttons->button(QDialogButtonBox::Ok))
                                    : static_cast<QAbstractButton*>(buttons->button(QDialogButtonBox::Apply));
    ASSERT_NE(ok, nullptr);
    ok->click();

    EXPECT_TRUE(processEventsUntil([&]() { return view->getUniqueName() == "renamed by the menu"; }))
        << view->getUniqueName();
}

TEST_F(ApplicationMenu, RenameWithNoImageSaysSoAndOpensNothing)
{
    ASSERT_TRUE(views().empty());

    Answered refusal;
    answerTheNextMessageBox(&refusal);
    invoke("actionRename");

    EXPECT_TRUE(refusal.seen);
    EXPECT_EQ(refusal.title, QStringLiteral("Image rename error"));
    EXPECT_EQ(refusal.text, QStringLiteral("No selected image to rename"));
    EXPECT_EQ(visibleWidget([](QWidget* widget) { return isA<ImageRenameControl>(widget); }), nullptr);
}

TEST_F(ApplicationMenu, UndoReturnsTheActiveViewToTheStateBeforeItsLastEdit)
{
    ImageViewControl* const view = makeView(QStringLiteral("edited"), 0.25f);
    ASSERT_NE(view, nullptr);
    invoke("actionNegative_Transformation");
    ASSERT_TRUE(processEventsUntil([&]() { return std::abs(viewSample(*view) - 0.75) < 1e-6; })) << viewSample(*view);

    invoke("actionUndo");

    EXPECT_TRUE(processEventsUntil([&]() { return std::abs(viewSample(*view) - 0.25) < 1e-6; })) << viewSample(*view);
}

TEST_F(ApplicationMenu, RedoReappliesTheEditUndone)
{
    ImageViewControl* const view = makeView(QStringLiteral("edited"), 0.25f);
    ASSERT_NE(view, nullptr);
    invoke("actionNegative_Transformation");
    ASSERT_TRUE(processEventsUntil([&]() { return std::abs(viewSample(*view) - 0.75) < 1e-6; })) << viewSample(*view);
    invoke("actionUndo");
    ASSERT_TRUE(processEventsUntil([&]() { return std::abs(viewSample(*view) - 0.25) < 1e-6; })) << viewSample(*view);

    invoke("actionRedo");

    EXPECT_TRUE(processEventsUntil([&]() { return std::abs(viewSample(*view) - 0.75) < 1e-6; })) << viewSample(*view);
}

TEST_F(ApplicationMenu, ImageHistoryShowsTheActiveViewsHistoryWindow)
{
    ImageViewControl* const view = makeView(QStringLiteral("historied"));
    ASSERT_NE(view, nullptr);

    invoke("actionImage_History");

    EXPECT_TRUE(processEventsUntil(
        []() { return visibleWidget([](QWidget* widget) { return isA<ImageHistoryControl>(widget); }) != nullptr; }));
}

// With no image there is no history to show: the entry is offered disabled, and invoking it - a shortcut, say - shows
// nothing, neither a window nor a message.
TEST_F(ApplicationMenu, ImageHistoryWithNoImageIsOfferedDisabledAndShowsNothing)
{
    ASSERT_TRUE(views().empty());
    QAction* const history = menuAction("actionImage_History");
    ASSERT_NE(history, nullptr);
    EXPECT_FALSE(history->isEnabled());

    Answered refusal;
    answerTheNextMessageBox(&refusal);
    history->trigger();
    processEventsUntil([]() { return false; }, 200);

    EXPECT_FALSE(refusal.seen) << refusal.title.toStdString() << ": " << refusal.text.toStdString();
    EXPECT_EQ(visibleWidget([](QWidget* widget) { return isA<ImageHistoryControl>(widget); }), nullptr);
}

// ---- The image menu: the view's zoom and layout.

TEST_F(ApplicationMenu, ZoomInEnlargesTheActiveView)
{
    ImageViewControl* const view = makeView(QStringLiteral("zoomed"));
    ASSERT_NE(view, nullptr);
    view->zoom100();
    const float before = view->getCurrentZoom();

    invoke("actionZoom_In");

    EXPECT_GT(view->getCurrentZoom(), before);
}

TEST_F(ApplicationMenu, ZoomOutReducesTheActiveView)
{
    ImageViewControl* const view = makeView(QStringLiteral("zoomed"));
    ASSERT_NE(view, nullptr);
    view->zoom100();
    const float before = view->getCurrentZoom();

    invoke("actionZoom_Out");

    EXPECT_LT(view->getCurrentZoom(), before);
}

TEST_F(ApplicationMenu, ZoomOneHundredShowsTheActiveViewAtItsPixelScale)
{
    ImageViewControl* const view = makeView(QStringLiteral("zoomed"));
    ASSERT_NE(view, nullptr);
    view->zoomIn();
    view->zoomIn();
    // The view states its zoom in percent.
    ASSERT_NE(view->getCurrentZoom(), 100.0f);

    invoke("actionZoom_100");

    EXPECT_FLOAT_EQ(view->getCurrentZoom(), 100.0f);
}

TEST_F(ApplicationMenu, FitToWindowScalesTheActiveViewToItsWindow)
{
    ImageViewControl* const view = makeView(QStringLiteral("fitted"));
    ASSERT_NE(view, nullptr);
    view->fitToWindow();
    const float fitted = view->getCurrentZoom();
    view->zoom100();
    ASSERT_NE(view->getCurrentZoom(), fitted);

    invoke("actionFit_To_Window");

    EXPECT_FLOAT_EQ(view->getCurrentZoom(), fitted);
}

TEST_F(ApplicationMenu, MaximizeViewMaximizesTheActiveViewAndASecondInvocationRestoresIt)
{
    ImageViewControl* const view = makeView(QStringLiteral("maximized"));
    ASSERT_NE(view, nullptr);
    ASSERT_FALSE(view->isMaximized());

    invoke("actionMaximize_View");
    EXPECT_TRUE(view->isMaximized());

    invoke("actionMaximize_View");
    EXPECT_FALSE(view->isMaximized());
}

TEST_F(ApplicationMenu, FitImagesToWindowFitsEveryView)
{
    ImageViewControl* const first = makeView(QStringLiteral("first"));
    ImageViewControl* const second = makeView(QStringLiteral("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->fitToWindow();
    second->fitToWindow();
    const float firstFitted = first->getCurrentZoom();
    const float secondFitted = second->getCurrentZoom();
    first->zoom100();
    second->zoom100();

    invoke("actionFit_Images_To_Window");

    EXPECT_FLOAT_EQ(first->getCurrentZoom(), firstFitted);
    EXPECT_FLOAT_EQ(second->getCurrentZoom(), secondFitted);
}

// ---- The image menu: the tool windows over the active view.

TEST_F(ApplicationMenu, StatisticsOpensTheStatisticsWindowOverTheActiveView)
{
    EXPECT_TRUE(
        opensToolWindow("actionStatistics", [](QWidget* window) { return isA<ImageStatisticsControl>(window); }));
}

TEST_F(ApplicationMenu, HistogramOpensTheHistogramWindowOverTheActiveView)
{
    EXPECT_TRUE(opensToolWindow("actionHistogram", [](QWidget* window) { return isA<ImageHistogramControl>(window); }));
}

// The header window is offered for an image whose file states a FITS header: one opened from a FITS file.
TEST_F(ApplicationMenu, FitsHeaderOpensTheHeaderWindowOverAViewOfAFitsFile)
{
    const QString path = filePath(QStringLiteral("header.fits"));
    writeFile(path, an::CodecFormat::Fits, 0.5f);
    ImageViewControl* const view = openThroughTheFileMenu(path);
    ASSERT_NE(view, nullptr);
    s_window->setActiveImageViewControl(view);
    ASSERT_TRUE(menuAction("actionFITSHeader")->isEnabled());

    invoke("actionFITSHeader");

    EXPECT_TRUE(processEventsUntil([view]() {
        for (QWidget* window : visibleChildWindows(*view))
        {
            if (isA<ImageFITSHeaderViewerControl>(window))
            {
                return true;
            }
        }
        return false;
    }));
}

// An image that states no header - one a process made - is offered no header window.
TEST_F(ApplicationMenu, FitsHeaderIsOfferedDisabledForAnImageThatStatesNoHeader)
{
    ImageViewControl* const view = makeView(QStringLiteral("made"));
    ASSERT_NE(view, nullptr);
    EXPECT_FALSE(menuAction("actionFITSHeader")->isEnabled());
}

TEST_F(ApplicationMenu, AstrometryInfoOpensTheSolutionWindowOverTheActiveView)
{
    EXPECT_TRUE(opensToolWindow("actionAstrometryInfo",
                                [](QWidget* window) { return isNamed(window, "ImageAstrometryInfoControl"); }));
}

// The Exif window is offered for an image whose file states Exif fields: a TIFF file, whose tags - the product's own
// Software tag among them - are read as its Exif record.
TEST_F(ApplicationMenu, ExifViewerOpensTheExifWindowOverAViewOfATiffFile)
{
    const QString path = filePath(QStringLiteral("tagged.tiff"));
    writeFile(path, an::CodecFormat::Tiff, 0.5f);
    ImageViewControl* const view = openThroughTheFileMenu(path);
    ASSERT_NE(view, nullptr);
    s_window->setActiveImageViewControl(view);
    ASSERT_TRUE(menuAction("actionImage_EXIFViewer")->isEnabled());

    invoke("actionImage_EXIFViewer");

    EXPECT_TRUE(processEventsUntil([view]() {
        for (QWidget* window : visibleChildWindows(*view))
        {
            if (isNamed(window, "ImageEXIFViewerControl"))
            {
                return true;
            }
        }
        return false;
    }));
}

TEST_F(ApplicationMenu, PlateSolveOpensThePlateSolveWindowOverTheActiveView)
{
    EXPECT_TRUE(
        opensToolWindow("actionPlateSolve", [](QWidget* window) { return isA<an::ui::PlateSolveControl>(window); }));
}

TEST_F(ApplicationMenu, SummaryOpensTheSummaryWindowOverTheActiveView)
{
    EXPECT_TRUE(
        opensToolWindow("actionSummary", [](QWidget* window) { return isNamed(window, "ImageSummaryViewerControl"); }));
}

// ---- The image menu: Show in File Browser, offered while the active view's image is a file.

TEST_F(ApplicationMenu, ShowInFileBrowserIsOfferedDisabledWithNoView)
{
    ASSERT_TRUE(views().empty());
    QAction* const show = menuAction("actionShow_In_File_Browser");
    ASSERT_NE(show, nullptr);
    EXPECT_FALSE(show->isEnabled());
}

TEST_F(ApplicationMenu, ShowInFileBrowserIsOfferedDisabledForAnImageThatHasNeverBeenAFile)
{
    ImageViewControl* const view = makeView(QStringLiteral("made"));
    ASSERT_NE(view, nullptr);
    EXPECT_FALSE(menuAction("actionShow_In_File_Browser")->isEnabled());
}

TEST_F(ApplicationMenu, ShowInFileBrowserIsOfferedForAViewOfAFile)
{
    const QString path = filePath(QStringLiteral("shown.fits"));
    writeFile(path, an::CodecFormat::Fits, 0.5f);
    ImageViewControl* const view = openThroughTheFileMenu(path);
    ASSERT_NE(view, nullptr);
    s_window->setActiveImageViewControl(view);
    EXPECT_TRUE(menuAction("actionShow_In_File_Browser")->isEnabled());
}

// Saving an image that has never been a file makes it one: the entry is offered as soon as the save lands.
TEST_F(ApplicationMenu, ShowInFileBrowserIsOfferedOnceAMadeImageIsSaved)
{
    ImageViewControl* const view = makeView(QStringLiteral("made then saved"), 0.375f);
    ASSERT_NE(view, nullptr);
    ASSERT_FALSE(menuAction("actionShow_In_File_Browser")->isEnabled());
    const QString path = filePath(QStringLiteral("made-then-saved.fits"));

    Answered dialog;
    chooseInTheNextFileDialog({path}, &dialog);
    invoke("actionSave_as");

    EXPECT_TRUE(dialog.seen);
    ASSERT_TRUE(processEventsUntil([&]() { return QFileInfo::exists(path); }));
    EXPECT_TRUE(processEventsUntil([]() { return menuAction("actionShow_In_File_Browser")->isEnabled(); }));
}

// A file deleted since it was opened is not handed to the file manager: the entry says the file is not on disk, as the
// dashboard's recent files do.
TEST_F(ApplicationMenu, ShowInFileBrowserSaysSoWhenTheFileIsNoLongerOnDisk)
{
    const QString path = filePath(QStringLiteral("deleted.fits"));
    writeFile(path, an::CodecFormat::Fits, 0.5f);
    ImageViewControl* const view = openThroughTheFileMenu(path);
    ASSERT_NE(view, nullptr);
    s_window->setActiveImageViewControl(view);
    ASSERT_TRUE(QFile::remove(path));

    Answered warning;
    answerTheNextMessageBox(&warning);
    invoke("actionShow_In_File_Browser");

    EXPECT_TRUE(processEventsUntil([&]() { return warning.seen; }));
    EXPECT_EQ(warning.title, QStringLiteral("File Not Found"));
    EXPECT_TRUE(warning.text.contains(QDir::cleanPath(path))) << warning.text.toStdString();
}

// The image's own context menu ends with the same entry, offered as the Image menu offers it.
class ApplicationMenuContextMenu : public ApplicationMenu
{
protected:
    // The last entry of a menu, as it was when the menu was shown.
    struct Entry
    {
        bool shown = false;
        QString objectName;
        QString text;
        bool enabled = false;
    };

    // Right-clicks a view as a person does and answers the last entry of the menu it shows, closing the menu.
    static Entry lastContextMenuEntry(ImageViewControl& view)
    {
        auto last = std::make_shared<Entry>();
        answerWhenShown([last](QWidget* widget) {
            auto* menu = qobject_cast<QMenu*>(widget);
            if (menu == nullptr)
            {
                return false;
            }
            last->shown = true;
            const QList<QAction*> entries = menu->actions();
            if (!entries.isEmpty())
            {
                last->objectName = entries.back()->objectName();
                last->text = entries.back()->text();
                last->enabled = entries.back()->isEnabled();
            }
            menu->close();
            return true;
        });
        const QPoint at(4, 4);
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, view.mapToGlobal(at));
        QCoreApplication::sendEvent(&view, &event);
        return *last;
    }
};

TEST_F(ApplicationMenuContextMenu, TheImagesContextMenuEndsWithShowInFileBrowserForAViewOfAFile)
{
    const QString path = filePath(QStringLiteral("context.fits"));
    writeFile(path, an::CodecFormat::Fits, 0.5f);
    ImageViewControl* const view = openThroughTheFileMenu(path);
    ASSERT_NE(view, nullptr);
    s_window->setActiveImageViewControl(view);

    const Entry last = lastContextMenuEntry(*view);

    ASSERT_TRUE(last.shown) << "the view showed no context menu";
    EXPECT_EQ(last.objectName, QStringLiteral("actionShow_In_File_Browser")) << last.text.toStdString();
    EXPECT_TRUE(last.enabled);
}

TEST_F(ApplicationMenuContextMenu, TheImagesContextMenuOffersShowInFileBrowserDisabledForAnImageThatHasNeverBeenAFile)
{
    ImageViewControl* const view = makeView(QStringLiteral("made"));
    ASSERT_NE(view, nullptr);

    const Entry last = lastContextMenuEntry(*view);

    ASSERT_TRUE(last.shown) << "the view showed no context menu";
    EXPECT_EQ(last.objectName, QStringLiteral("actionShow_In_File_Browser")) << last.text.toStdString();
    EXPECT_FALSE(last.enabled);
}

// ---- The mask menu, over a view with a mask.

TEST_F(ApplicationMenu, InvertMaskInvertsTheActiveViewsMask)
{
    ImageViewControl* const view = makeView(QStringLiteral("masked"));
    ASSERT_NE(view, nullptr);
    giveAMask(view);
    ASSERT_FALSE(view->isMaskInverted());

    clickCheckable("actionInvert_Mask", false);
    EXPECT_TRUE(view->isMaskInverted());

    clickCheckable("actionInvert_Mask", true);
    EXPECT_FALSE(view->isMaskInverted());
}

TEST_F(ApplicationMenu, EnableMaskTurnsTheActiveViewsMaskOnAndOff)
{
    ImageViewControl* const view = makeView(QStringLiteral("masked"));
    ASSERT_NE(view, nullptr);
    giveAMask(view);

    clickCheckable("actionEnable_Mask", true);
    EXPECT_FALSE(view->isMaskEnabled());

    clickCheckable("actionEnable_Mask", false);
    EXPECT_TRUE(view->isMaskEnabled());
}

TEST_F(ApplicationMenu, ShowMaskShowsAndHidesTheActiveViewsMask)
{
    ImageViewControl* const view = makeView(QStringLiteral("masked"));
    ASSERT_NE(view, nullptr);
    giveAMask(view);

    clickCheckable("actionShow_Mask", false);
    EXPECT_TRUE(view->isShowMaskEnabled());

    clickCheckable("actionShow_Mask", true);
    EXPECT_FALSE(view->isShowMaskEnabled());
}

// Each overlay entry sets the active view's mask rendering and leaves the other entries unchecked.
class ApplicationMenuOverlay : public ApplicationMenu
{
protected:
    void selects(const char* name, MaskRenderingMode mode)
    {
        ImageViewControl* const view = makeView(QStringLiteral("masked"));
        ASSERT_NE(view, nullptr);
        giveAMask(view);
        // Start from a mode no entry here selects last.
        view->setMaskRenderingMode(mode == MaskRenderingMode::red_mode ? MaskRenderingMode::blue_mode
                                                                       : MaskRenderingMode::red_mode);

        clickCheckable(name, false);

        EXPECT_EQ(view->getMaskRenderingMode(), mode);
        QAction* const selected = menuAction(name);
        ASSERT_NE(selected, nullptr);
        EXPECT_TRUE(selected->isChecked());
        for (const char* other :
             {"actionOverlay_Red", "actionOverlay_Green", "actionOverlay_Blue", "actionOverlay_Yellow",
              "actionOverlay_Cyan", "actionOverlay_Magenta", "actionMultiply", "actionReplace"})
        {
            if (std::string(other) != name)
            {
                QAction* const action = menuAction(other);
                ASSERT_NE(action, nullptr);
                EXPECT_FALSE(action->isChecked()) << other << " stayed checked beside " << name;
            }
        }
    }
};

TEST_F(ApplicationMenuOverlay, OverlayRedTintsTheMaskRed)
{
    selects("actionOverlay_Red", MaskRenderingMode::red_mode);
}

TEST_F(ApplicationMenuOverlay, OverlayGreenTintsTheMaskGreen)
{
    selects("actionOverlay_Green", MaskRenderingMode::green_mode);
}

TEST_F(ApplicationMenuOverlay, OverlayBlueTintsTheMaskBlue)
{
    selects("actionOverlay_Blue", MaskRenderingMode::blue_mode);
}

TEST_F(ApplicationMenuOverlay, OverlayYellowTintsTheMaskYellow)
{
    selects("actionOverlay_Yellow", MaskRenderingMode::yellow_mode);
}

TEST_F(ApplicationMenuOverlay, OverlayCyanTintsTheMaskCyan)
{
    selects("actionOverlay_Cyan", MaskRenderingMode::cyan_mode);
}

TEST_F(ApplicationMenuOverlay, OverlayMagentaTintsTheMaskMagenta)
{
    selects("actionOverlay_Magenta", MaskRenderingMode::magenta_mode);
}

TEST_F(ApplicationMenuOverlay, MultiplyShowsTheImageTimesTheMask)
{
    selects("actionMultiply", MaskRenderingMode::multiply_mode);
}

TEST_F(ApplicationMenuOverlay, ReplaceShowsTheMaskInPlaceOfTheImage)
{
    selects("actionReplace", MaskRenderingMode::replace_mode);
}

// ---- The application menu's windows.

TEST_F(ApplicationMenu, NewSequenceOpensTheNewSequenceWindow)
{
    invoke("actionNew_Sequence");

    EXPECT_TRUE(processEventsUntil(
        []() { return visibleWidget([](QWidget* widget) { return isA<NewSequenceControl>(widget); }) != nullptr; }));
}

TEST_F(ApplicationMenu, AboutShowsTheAboutWindowNamingTheProductsVersion)
{
    // The version the application states; the suite's process states none of its own.
    const QString previous = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QStringLiteral("6.5.0-menu"));
    Answered shown;
    bool about = false;
    QStringList texts;
    answerTheNextModalDialog(
        [&about, &texts](QDialog& dialog) {
            about = isA<AboutDialog>(&dialog);
            for (QLabel* label : dialog.findChildren<QLabel*>())
            {
                texts.append(label->text());
            }
        },
        &shown);

    invoke("actionAbout_AfterNight");

    QCoreApplication::setApplicationVersion(previous);
    ASSERT_TRUE(shown.seen);
    EXPECT_TRUE(about) << shown.text.toStdString();
    EXPECT_EQ(shown.title, QStringLiteral("About AfterNight"));
    EXPECT_TRUE(texts.join(QLatin1Char('\n')).contains(QStringLiteral("Version 6.5.0-menu")))
        << texts.join(QLatin1Char('\n')).toStdString();
}

TEST_F(ApplicationMenu, SystemInformationShowsTheSystemInformationWindow)
{
    Answered shown;
    bool information = false;
    answerTheNextModalDialog([&information](QDialog& dialog) { information = isA<SystemInformationDialog>(&dialog); },
                             &shown);

    invoke("actionSystem_Information");

    ASSERT_TRUE(shown.seen);
    EXPECT_TRUE(information) << shown.text.toStdString();
}

TEST_F(ApplicationMenu, SettingsShowsTheSettingsWindow)
{
    Answered shown;
    bool settings = false;
    answerTheNextModalDialog([&settings](QDialog& dialog) { settings = isA<SettingsDialog>(&dialog); }, &shown);

    invoke("actionSettings");

    ASSERT_TRUE(shown.seen);
    EXPECT_TRUE(settings) << shown.text.toStdString();
    EXPECT_TRUE(shown.title.startsWith(QStringLiteral("Settings"))) << shown.title.toStdString();
}

TEST_F(ApplicationMenu, ManageExtensionsShowsTheExtensionsWindowAtTheExtensionManager)
{
    Answered shown;
    bool extensions = false;
    bool atManager = false;
    answerTheNextModalDialog(
        [&extensions, &atManager](QDialog& dialog) {
            extensions = isNamed(&dialog, "ExtensionsDialog");
            for (QWidget* child : dialog.findChildren<QWidget*>())
            {
                if (isA<an::ui::ExtensionManagerPage>(child) && child->isVisible())
                {
                    atManager = true;
                }
            }
        },
        &shown);

    invoke("actionManage_Extensions");

    ASSERT_TRUE(shown.seen);
    EXPECT_TRUE(extensions) << shown.text.toStdString();
    EXPECT_TRUE(atManager);
}

// Exit closes the window through its close, which asks before it closes a window whose views hold changes nobody saved;
// answered No, the window and its views stay as they were.
TEST_F(ApplicationMenu, ExitAsksAboutUnsavedChangesAndAnsweredNoKeepsTheWindowAndItsViews)
{
    ImageViewControl* const view = makeView(QStringLiteral("unsaved"), 0.25f);
    ASSERT_NE(view, nullptr);
    invoke("actionNegative_Transformation");
    ASSERT_TRUE(processEventsUntil([&]() { return std::abs(viewSample(*view) - 0.75) < 1e-6; })) << viewSample(*view);
    ASSERT_TRUE(view->hasPendingChanges());
    const bool shown = s_window->isVisible();

    Answered question;
    answerWhenShown([&question](QWidget* widget) {
        auto* const box = qobject_cast<QMessageBox*>(widget);
        QAbstractButton* const no = box == nullptr ? nullptr : box->button(QMessageBox::No);
        if (no == nullptr)
        {
            return false;
        }
        question.seen = true;
        question.title = box->windowTitle();
        question.text = box->text();
        no->click();
        return true;
    });
    invoke("actionExit");

    EXPECT_TRUE(question.seen);
    EXPECT_EQ(question.title, QStringLiteral("Close AfterNight"));
    EXPECT_TRUE(question.text.contains(QStringLiteral("pending changes"))) << question.text.toStdString();
    EXPECT_EQ(s_window->isVisible(), shown);
    ASSERT_EQ(views().size(), 1u);
    EXPECT_EQ(views().front(), view);
    EXPECT_TRUE(view->hasPendingChanges());
}

// ---- What the menus hold.

namespace
{

// Every action the menu bar reaches through its submenus, each once, in menu order.
void collectMenuEntries(const QList<QAction*>& actions, std::vector<QAction*>& entries)
{
    for (QAction* action : actions)
    {
        if (action == nullptr || action->isSeparator() ||
            std::find(entries.begin(), entries.end(), action) != entries.end())
        {
            continue;
        }
        entries.push_back(action);
        if (QMenu* const submenu = action->menu())
        {
            collectMenuEntries(submenu->actions(), entries);
        }
    }
}

} // namespace

// Every process the registry holds is an entry of the window's own menus, reached from its menu bar, and no two entries
// share a name, so the name a descriptor binds reaches one entry. The names are recorded beside the result, in menu
// order, so a coverage sweep reads the menus the window builds rather than the source that wires them.
TEST_F(ApplicationMenu, EveryRegisteredProcessIsAnEntryOfTheWindowsMenus)
{
    std::vector<QAction*> entries;
    collectMenuEntries(s_window->menuBar()->actions(), entries);
    std::vector<std::string> names;
    for (const QAction* action : entries)
    {
        if (!action->objectName().isEmpty())
        {
            names.push_back(action->objectName().toStdString());
        }
    }
    ASSERT_FALSE(names.empty());
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    const auto shared = std::adjacent_find(sorted.begin(), sorted.end());
    EXPECT_EQ(shared, sorted.end()) << "two entries are named " << (shared == sorted.end() ? "" : *shared);
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        if (!descriptor.is_extension_process)
        {
            EXPECT_TRUE(std::binary_search(sorted.begin(), sorted.end(), descriptor.action_object_name))
                << descriptor.id << " is no entry of the menus";
        }
    }
    std::string recorded;
    for (const std::string& name : names)
    {
        recorded += (recorded.empty() ? "" : " ") + name;
    }
    RecordProperty("menuEntries", recorded);
}
