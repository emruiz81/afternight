// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's menus, invoked on the application's own main window as a person invokes them:
// the file menu's open and saves through the application's file dialog, the image menu's closes, rename, history,
// zoom and Show in File Browser over the active view and in the image's context menu, its tool windows, the mask menu
// over a view with a mask, and the application menu's windows. Each case triggers the action the menu holds and
// observes what it did - a view made or gone, a file written, a window shown, a view's state changed - and answers the
// dialogs the action opens the way a person would.

#include "ApplicationDriving.h"
#include "InventoryRecords.h"
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
#include "controls/annotation/AnnotateImageControl.h"
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
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
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
protected:
    // A view the application's factory makes over a zeroed 8x4 image whose cards state a solution - a tenth of a degree
    // a pixel about (120, 10) - which becomes the active view.
    static ImageViewControl* makeSolvedView(const QString& name)
    {
        std::vector<an::CodecCard> cards;
        const auto add = [&cards](const char* keyword, const char* value) {
            an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, {});
            EXPECT_TRUE(card.hasValue());
            if (card.hasValue())
            {
                cards.push_back(std::move(*card));
            }
        };
        add("CTYPE1", "'RA---TAN'");
        add("CTYPE2", "'DEC--TAN'");
        add("CUNIT1", "'deg'");
        add("CUNIT2", "'deg'");
        add("CRPIX1", "4.5");
        add("CRPIX2", "2.5");
        add("CRVAL1", "120");
        add("CRVAL2", "10");
        add("CD1_1", "-0.1");
        add("CD1_2", "0");
        add("CD2_1", "0");
        add("CD2_2", "0.1");
        add("RADESYS", "'ICRS'");
        an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
        EXPECT_TRUE(codec.hasValue());
        an::Result<an::consumer::SessionHold> hold = session().hold();
        EXPECT_TRUE(hold.hasValue());
        if (!codec.hasValue() || !hold.hasValue())
        {
            return nullptr;
        }
        const std::optional<an::ImageDescriptor> descriptor =
            an::tests::app::monoFloatDescriptor(hold->engine(), 8u, 4u);
        EXPECT_TRUE(descriptor.has_value());
        if (!descriptor.has_value())
        {
            return nullptr;
        }
        an::Result<an::Image<float>> image = an::Image<float>::create(
            hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
            an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                      an::Provenance{}, std::move(*codec), std::nullopt));
        EXPECT_TRUE(image.hasValue());
        if (!image.hasValue())
        {
            return nullptr;
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
        EXPECT_TRUE(asset.hasValue());
        if (!asset.hasValue())
        {
            return nullptr;
        }
        an::Result<an::ui::ImageDocument> document =
            an::ui::ImageDocument::create(std::move(*asset), an::ui::DocumentNames{.displayName = name});
        EXPECT_TRUE(document.hasValue());
        if (!document.hasValue())
        {
            return nullptr;
        }
        ImageViewControl* const view = context().getViewFactory()->createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            s_window->setActiveImageViewControl(view);
        }
        return view;
    }
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

// Annotate Image is offered for a view whose image states a solution, and opens its window over that view.
TEST_F(ApplicationMenu, AnnotateImageOpensTheAnnotationWindowOverASolvedViewAndIsNotOfferedForAnotherView)
{
    ImageViewControl* const unsolved = makeView(QStringLiteral("unsolved"));
    ASSERT_NE(unsolved, nullptr);
    EXPECT_FALSE(menuAction("actionAnnotateImage")->isEnabled());

    ImageViewControl* const view = makeSolvedView(QStringLiteral("solved"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->canAnnotateCurrentImage());
    ASSERT_TRUE(processEventsUntil([]() { return menuAction("actionAnnotateImage")->isEnabled(); }));

    invoke("actionAnnotateImage");

    an::ui::AnnotateImageControl* opened = nullptr;
    EXPECT_TRUE(processEventsUntil([view, &opened]() {
        for (QWidget* window : visibleChildWindows(*view))
        {
            if (auto* annotate = dynamic_cast<an::ui::AnnotateImageControl*>(window); annotate != nullptr)
            {
                opened = annotate;
                return true;
            }
        }
        return false;
    }));
    if (opened != nullptr)
    {
        EXPECT_TRUE(processEventsUntil([opened]() { return !opened->isWorking(); }));
        opened->close();
    }
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

// The poll that answers a file dialog's refusal - a message box shown from inside the dialog's accept - ends when the
// accept returns. Left armed for its ten seconds, it answered the next message box of the case, whichever asked for
// it: a case that opened a file through the menu and then waited for a box of its own lost that box to the poll
// about every other run.
TEST_F(ApplicationMenu, AMessageBoxShownAfterAFileIsOpenedIsAnsweredByNoPollTheOpenLeftBehind)
{
    const QString path = filePath(QStringLiteral("opened-before-a-box.fits"));
    writeFile(path, an::CodecFormat::Fits, 0.5f);
    ASSERT_NE(openThroughTheFileMenu(path), nullptr);

    // A box this case asks no poll to answer: nothing presses its button.
    QMessageBox box(QMessageBox::Information, QStringLiteral("Later"),
                    QStringLiteral("A box no poll of this case was asked to answer."), QMessageBox::Ok, s_window);
    box.open();
    processEventsUntil([]() { return false; }, 300);

    EXPECT_TRUE(box.isVisible()) << "a poll the file dialog left armed answered a box that was not its own";
    box.close();
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

// The repository the suite was built from: the first folder at or above the working directory, the suite's own folder
// or this source file that holds both `src` and `build`. Empty when there is none.
QString repositoryRoot()
{
    const QStringList seeds = {QDir::currentPath(), QCoreApplication::applicationDirPath(),
                               QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()};
    for (const QString& seed : seeds)
    {
        QDir folder(seed);
        do
        {
            if (QFileInfo::exists(folder.filePath(QStringLiteral("src"))) &&
                QFileInfo::exists(folder.filePath(QStringLiteral("build"))))
            {
                return folder.absolutePath();
            }
        } while (folder.cdUp());
    }
    return {};
}

} // namespace

// Every process of the application states the page of the user guide that Help opens from its window, and the guide
// has that page: the built guide names a page as its source with `.html` for `.md`. An extension's process has no page
// of the guide.
TEST_F(ApplicationMenu, EveryRegisteredProcessStatesAPageTheUserGuideHas)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty()) << "the repository's root was not found";
    const QDir pages(QDir(root).filePath(QStringLiteral("docs/user-guide/docs/en")));
    ASSERT_TRUE(pages.exists()) << pages.absolutePath().toStdString();

    std::size_t stated = 0u;
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        if (descriptor.is_extension_process)
        {
            continue;
        }
        const QString helpPath = QString::fromStdString(descriptor.helpPath);
        ASSERT_TRUE(helpPath.endsWith(QStringLiteral(".html")))
            << descriptor.id << " states the page '" << descriptor.helpPath << "'";
        const QString source = helpPath.chopped(5) + QStringLiteral(".md");
        EXPECT_TRUE(QFileInfo(pages.filePath(source)).isFile())
            << descriptor.id << " states " << descriptor.helpPath << ", and the guide has no " << source.toStdString();
        ++stated;
    }
    EXPECT_GT(stated, 0u);
}

// Help answers F1 from every window of the application: a process window is a window of its own, where a shortcut of
// the main window's context is not heard. No other entry of the menus takes F1.
TEST_F(ApplicationMenu, UserGuideAnswersF1FromEveryWindowOfTheApplication)
{
    QAction* const guide = menuAction("actionUser_Guide");
    ASSERT_NE(guide, nullptr);
    EXPECT_EQ(guide->shortcut(), QKeySequence(Qt::Key_F1));
    EXPECT_EQ(guide->shortcutContext(), Qt::ApplicationShortcut);

    std::vector<QAction*> entries;
    collectMenuEntries(s_window->menuBar()->actions(), entries);
    for (const QAction* action : entries)
    {
        if (action != guide)
        {
            EXPECT_NE(action->shortcut(), QKeySequence(Qt::Key_F1)) << action->objectName().toStdString();
        }
    }
}

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

// ---- What the window is, held to the records beside this suite.

// Every action the window declares is the one the record holds, in the order the window declares them: its object
// name, text, tool tip, shortcut and the shortcut's context, the icon it shows, whether it can be checked, and the
// menus of the menu bar that hold it. An action renamed, reworded, given another key or icon, moved to another menu,
// added or removed fails here until the record's entry says so.
TEST_F(ApplicationMenu, ActionInventoryMatchesTheRecord)
{
    const an::ui::IIconProviderPtr icons = context().getIconProvider();
    ASSERT_NE(icons, nullptr);
    const an::tests::app::IconNames names(*icons);
    ASSERT_GT(names.size(), 0) << "the application loaded no icon, so no action's icon can be named";
    // A few actions follow the active image - Undo and Redo name the step they would take, Maximize View becomes
    // Restore View - so the inventory is read over one image that has never been edited or maximized: what the
    // actions are when a person has opened a first image.
    ASSERT_NE(makeView(QStringLiteral("inventory")), nullptr);
    an::ui::IActionStateManager* const state = context().getActionStateManager();
    ASSERT_NE(state, nullptr);
    state->updateActiveImageActions();
    state->updateActiveImageHistory();
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kActionInventoryPath),
                                               an::tests::app::actionInventoryPreamble());

    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kActionsSection),
                               an::tests::app::actionInventoryEntries(*s_window, names)));
}

// The menu bar is the tree the record holds: every menu, entry and separator, in the order a person reads them. The
// menus whose entries follow what a person did - the recent files, the favourite and the recent processes - are held
// by name alone, and of the Extensions menu only its own actions, so the tree is the same whatever was opened,
// launched or installed before it is read.
TEST_F(ApplicationMenu, MenuBarTreeMatchesTheRecord)
{
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kActionInventoryPath),
                                               an::tests::app::actionInventoryPreamble());

    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kMenuBarSection),
                               an::tests::app::menuBarTreeEntries(*s_window)));
}

// The menu a view shows on a right click is the tree the record holds, entry for entry: the menus it shares with the
// menu bar by name, and its own entries under them.
TEST_F(ApplicationMenu, ViewMenuTreeMatchesTheRecord)
{
    ImageViewControl* const view = makeView(QStringLiteral("made"));
    ASSERT_NE(view, nullptr);

    auto shown = std::make_shared<std::optional<QStringList>>();
    answerWhenShown([shown](QWidget* widget) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr)
        {
            return false;
        }
        *shown = an::tests::app::viewMenuTreeEntries(*menu, *s_window);
        menu->close();
        return true;
    });
    const QPoint at(4, 4);
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, view->mapToGlobal(at));
    QCoreApplication::sendEvent(view, &event);

    ASSERT_TRUE(shown->has_value()) << "the view showed no context menu";
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kViewMenuInventoryPath),
                                               an::tests::app::viewMenuInventoryPreamble());
    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kViewMenuSection), **shown));
}
