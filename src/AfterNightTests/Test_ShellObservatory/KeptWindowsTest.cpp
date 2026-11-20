// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The windows that stay windows, each opened from the Observatory shell the way a person opens it: the tool windows
/// of an image from the readout's cards, the history from the history strip, the Autostretch settings from the
/// floating toolbar, a sequence's windows from the palette and the catalogue's action, the catalogue's download from
/// the Settings, the favourites from the menu tree, and About and System Information from the top bar. Each one opened
/// has the heading and the footer of a window that stays a window and lies on a dialog's surface.

#include "Test_ShellObservatory/KeptWindowLook.h"
#include "Test_ShellObservatory/ObservatoryWindowCase.h"

#include "chrome/MenuBarOnAlt.h"
#include "chrome/TopBar.h"
#include "pages/WorkspacePage.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnCommandPalette.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnHistogramCard.h"
#include "components/AnHistoryStrip.h"
#include "components/AnIconButton.h"
#include "components/AnImageFactsCard.h"
#include "components/AnReadoutCard.h"
#include "components/AnReadoutColumn.h"
#include "components/AnStatisticsCard.h"
#include "components/AnWindowHeading.h"
#include "controls/AboutDialog.h"
#include "controls/ExportSequenceControl.h"
#include "controls/ImageFITSHeaderViewerControl.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageHistoryControl.h"
#include "controls/ImageStatisticsControl.h"
#include "controls/NewSequenceControl.h"
#include "controls/PlateSolveControl.h"
#include "controls/SettingsDialog.h"
#include "controls/SystemInformationDialog.h"
#include "controls/ViewChrome.h"
#include "controls/annotation/AnnotateImageControl.h"
#include "dialogs/CatalogDownloadDialog.h"
#include "dialogs/CatalogSearchDialog.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::observatory
{

namespace
{

using an::tests::app::Answered;
using an::tests::app::answerTheNextModalDialog;
using an::tests::app::answerWhenShown;
using an::tests::app::isA;
using an::tests::app::isNamed;
using an::tests::app::processEventsUntil;
using an::tests::app::visibleChildWindows;

constexpr int kWaitMs = 10000;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The shell's window with the helpers the cases share: a view of an image that states cards, the readout following a
/// view, and the windows a view has opened.
class KeptWindowCase : public ObservatoryWindowCase
{
protected:
    /// A card of a namespace.
    static void add(std::vector<an::CodecCard>& cards, const an::CodecNamespace origin, const char* keyword,
                    const char* value, const char* comment = "")
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(origin, keyword, value, comment);
        EXPECT_TRUE(card.hasValue()) << keyword;
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }

    /// The cards of a solution: a tenth of a degree a pixel about (120, 10).
    [[nodiscard]] static std::vector<an::CodecCard> solutionCards()
    {
        std::vector<an::CodecCard> cards;
        const auto fits = [&cards](const char* keyword, const char* value) {
            add(cards, an::CodecNamespace::Fits, keyword, value);
        };
        fits("CTYPE1", "'RA---TAN'");
        fits("CTYPE2", "'DEC--TAN'");
        fits("CUNIT1", "'deg'");
        fits("CUNIT2", "'deg'");
        fits("CRPIX1", "4.5");
        fits("CRPIX2", "2.5");
        fits("CRVAL1", "120");
        fits("CRVAL2", "10");
        fits("CD1_1", "-0.1");
        fits("CD1_2", "0");
        fits("CD2_1", "0");
        fits("CD2_2", "0.1");
        fits("RADESYS", "'ICRS'");
        return cards;
    }

    /// Opens a view of a zeroed 8 x 4 image that states cards, as a person's file does, the active view.
    static ImageViewControl* openViewOf(const QString& name, std::vector<an::CodecCard> cards)
    {
        an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
        an::Result<an::consumer::SessionHold> hold = session().hold();
        EXPECT_TRUE(codec.hasValue() && hold.hasValue());
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
        ImageViewControl* const view = core().viewService().userViews().createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            core().viewService().setActiveImageViewControl(view);
            QCoreApplication::processEvents();
        }
        return view;
    }

    /// The readout column, once it follows a view and shows its cards whole.
    static an::ui::AnReadoutColumn* readoutOf(const ImageViewControl& view)
    {
        an::ui::AnReadoutColumn* const column = window().readoutColumn();
        EXPECT_NE(column, nullptr);
        if (column == nullptr)
        {
            return nullptr;
        }
        column->setCollapsed(false);
        const QString name = QString::fromUtf8(view.getUniqueName());
        EXPECT_TRUE(processEventsUntil([column, &name]() { return column->viewName() == name; }, kWaitMs))
            << "the readout follows " << column->viewName().toStdString();
        for (an::ui::AnReadoutCard* card :
             {column->card(an::ui::ReadoutCardId::Histogram), column->card(an::ui::ReadoutCardId::Statistics),
              column->card(an::ui::ReadoutCardId::Astrometry), column->card(an::ui::ReadoutCardId::Image)})
        {
            if (card != nullptr)
            {
                card->setFolded(false);
            }
        }
        settle();
        return column;
    }

    /// A button of the Astrometry card by the text it shows. The card's own header and a view's cannot be read
    /// together - each names an overlay type of its own by the same name - so the button is found as a person
    /// finds it.
    static QAbstractButton* astrometryButton(const an::ui::AnReadoutColumn& column, const QString& text)
    {
        const an::ui::AnReadoutCard* const card = column.card(an::ui::ReadoutCardId::Astrometry);
        if (card == nullptr)
        {
            return nullptr;
        }
        for (QPushButton* button : card->findChildren<QPushButton*>())
        {
            if (button->text() == text)
            {
                return button;
            }
        }
        return nullptr;
    }

    /// Presses a button of the readout once it is offered.
    static void press(QAbstractButton* button, const char* what)
    {
        ASSERT_NE(button, nullptr) << what;
        ASSERT_TRUE(processEventsUntil([button]() { return button->isEnabled() && button->isVisible(); }, kWaitMs))
            << what << " is not offered";
        button->click();
    }

    /// The window of a view that a check takes, once it is shown; null when none comes.
    static QWidget* windowOf(const ImageViewControl& view, const std::function<bool(QWidget*)>& isIt)
    {
        QWidget* found = nullptr;
        (void)processEventsUntil(
            [&]() {
                for (QWidget* candidate : visibleChildWindows(view))
                {
                    if (isIt(candidate))
                    {
                        found = candidate;
                    }
                }
                return found != nullptr;
            },
            kWaitMs);
        settle();
        return found;
    }

    /// Runs a command of the palette by the words a person types and the row they choose.
    static void runFromThePalette(const QString& typed, const QString& rowId)
    {
        window().openPalette();
        an::ui::AnCommandPalette& palette = *window().palette();
        ASSERT_TRUE(processEventsUntil([&palette]() { return palette.isVisible(); }, 3000));
        QTest::keyClicks(palette.queryField(), typed);
        settle();
        ASSERT_TRUE(palette.shownRowIds().contains(rowId))
            << palette.shownRowIds().join(QStringLiteral(", ")).toStdString();
        palette.setSelectedId(rowId);
        QTest::keyClick(palette.queryField(), Qt::Key_Return);
        settle();
    }
};

class StatisticsWindow : public KeptWindowCase
{
};
class HistogramWindow : public KeptWindowCase
{
};
class FitsHeaderWindow : public KeptWindowCase
{
};
class ExifWindow : public KeptWindowCase
{
};
class SummaryWindow : public KeptWindowCase
{
};
class AstrometryInfoWindow : public KeptWindowCase
{
};
class ImageHistoryWindow : public KeptWindowCase
{
};
class PlateSolveWindow : public KeptWindowCase
{
};
class FindObjectWindow : public KeptWindowCase
{
};
class AnnotateImageWindow : public KeptWindowCase
{
};
class NewSequenceWindow : public KeptWindowCase
{
};
class ExportSequenceWindow : public KeptWindowCase
{
};
class SaveAsWindow : public KeptWindowCase
{
};
class AutostretchSettingsWindow : public KeptWindowCase
{
};
class CatalogDownloadWindow : public KeptWindowCase
{
};
class ManageFavoritesWindow : public KeptWindowCase
{
};
class AboutWindow : public KeptWindowCase
{
};
class SystemInformationWindow : public KeptWindowCase
{
};

} // namespace

// The readout's Statistics card opens the Statistics window over the image the readout follows.
TEST_F(StatisticsWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("measured"));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(readout->statisticsCard()->windowButton(), "the Statistics card's window button");
    QWidget* const opened = windowOf(*view, [](QWidget* candidate) { return isA<ImageStatisticsControl>(candidate); });
    ASSERT_NE(opened, nullptr) << "the card opened no Statistics window";
    EXPECT_EQ(opened->windowTitle(), QStringLiteral("Statistics <measured>"));
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The readout's Histogram card opens the Histogram window over the image the readout follows.
TEST_F(HistogramWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("counted"));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(readout->histogramCard()->windowButton(), "the Histogram card's window button");
    QWidget* const opened = windowOf(*view, [](QWidget* candidate) { return isA<ImageHistogramControl>(candidate); });
    ASSERT_NE(opened, nullptr) << "the card opened no Histogram window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The Image card's header button opens the FITS header of an image whose file states one.
TEST_F(FitsHeaderWindow, OpensFromTheObservatory)
{
    std::vector<an::CodecCard> cards;
    add(cards, an::CodecNamespace::Fits, "OBJECT", "'NGC 7000'", "the target");
    add(cards, an::CodecNamespace::Fits, "EXPTIME", "300.0", "seconds");
    ImageViewControl* const view = openViewOf(QStringLiteral("with-a-header"), std::move(cards));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(readout->imageCard()->fitsHeaderButton(), "the Image card's FITS header button");
    QWidget* const opened =
        windowOf(*view, [](QWidget* candidate) { return isA<ImageFITSHeaderViewerControl>(candidate); });
    ASSERT_NE(opened, nullptr) << "the card opened no FITS header window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The Image card's Exif button opens the Exif fields of an image whose file states some.
TEST_F(ExifWindow, OpensFromTheObservatory)
{
    std::vector<an::CodecCard> cards;
    add(cards, an::CodecNamespace::Tiff, "Exif.Image.Model", "ZWO ASI2600MM Pro", "Ascii");
    ImageViewControl* const view = openViewOf(QStringLiteral("with-exif"), std::move(cards));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(readout->imageCard()->exifButton(), "the Image card's Exif button");
    QWidget* const opened =
        windowOf(*view, [](QWidget* candidate) { return isNamed(candidate, "ImageEXIFViewerControl"); });
    ASSERT_NE(opened, nullptr) << "the card opened no Exif window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The Image card's summary button opens the image's summary.
TEST_F(SummaryWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("summarized"));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(readout->imageCard()->summaryButton(), "the Image card's summary button");
    QWidget* const opened =
        windowOf(*view, [](QWidget* candidate) { return isNamed(candidate, "ImageSummaryViewerControl"); });
    ASSERT_NE(opened, nullptr) << "the card opened no summary window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The Astrometry card's Info button, shown for a solved image, opens the solution's window.
TEST_F(AstrometryInfoWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openViewOf(QStringLiteral("solved-info"), solutionCards());
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(astrometryButton(*readout, QStringLiteral("Info")), "the Astrometry card's Info button");
    QWidget* const opened =
        windowOf(*view, [](QWidget* candidate) { return isNamed(candidate, "ImageAstrometryInfoControl"); });
    ASSERT_NE(opened, nullptr) << "the card opened no astrometry window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The history strip's button opens the Image History window of the image the strip follows.
TEST_F(ImageHistoryWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("historied"));
    ASSERT_NE(view, nullptr);
    an::ui::AnHistoryStrip* const strip = window().workspacePage()->historyStrip();
    ASSERT_NE(strip, nullptr);
    ASSERT_TRUE(processEventsUntil([strip]() { return strip->isVisible(); }, kWaitMs));
    press(strip->historyButton(), "the history strip's button");
    QWidget* const opened = windowOf(*view, [](QWidget* candidate) { return isA<ImageHistoryControl>(candidate); });
    ASSERT_NE(opened, nullptr) << "the strip opened no history window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// The Astrometry card's Plate Solve button opens the solve's window, whose primary button is the footer's rightmost.
TEST_F(PlateSolveWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("to-solve"));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(astrometryButton(*readout, QStringLiteral("Plate Solve")), "the Astrometry card's Plate Solve button");
    QWidget* const opened =
        windowOf(*view, [](QWidget* candidate) { return isA<an::ui::PlateSolveControl>(candidate); });
    ASSERT_NE(opened, nullptr) << "the card opened no Plate Solve window";
    expectTheLookOfAKeptWindow(*opened);
    const QList<const QPushButton*> footer = an::tests::look::footerButtons(*opened);
    ASSERT_FALSE(footer.isEmpty());
    EXPECT_EQ(footer.back()->objectName(), QStringLiteral("plateSolveSolve"));
    opened->close();
}

// Find Object opens from the solve's hints, where a person looks an object up by its name.
TEST_F(FindObjectWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("to-find"));
    ASSERT_NE(view, nullptr);
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(astrometryButton(*readout, QStringLiteral("Plate Solve")), "the Astrometry card's Plate Solve button");
    QWidget* const solve =
        windowOf(*view, [](QWidget* candidate) { return isA<an::ui::PlateSolveControl>(candidate); });
    ASSERT_NE(solve, nullptr);
    // The hints are a section a person opens.
    for (QWidget* part : solve->findChildren<QWidget*>())
    {
        if (auto* const section = dynamic_cast<an::ui::AnCollapsibleSection*>(part))
        {
            section->setExpanded(true);
        }
    }
    settle();
    QPushButton* find = nullptr;
    for (QPushButton* button : solve->findChildren<QPushButton*>())
    {
        if (button->text().startsWith(QStringLiteral("Find object")))
        {
            find = button;
        }
    }
    ASSERT_NE(find, nullptr) << "the hints offer no Find object";
    ASSERT_TRUE(find->isVisibleTo(solve));

    Answered shown;
    bool found = false;
    answerTheNextModalDialog(
        [&found](QDialog& dialog) {
            found = isA<an::ui::CatalogSearchDialog>(&dialog);
            if (found)
            {
                expectTheLookOfAKeptWindow(dialog);
            }
        },
        &shown);
    find->click();
    ASSERT_TRUE(shown.seen) << "Find object opened no dialog";
    EXPECT_TRUE(found) << shown.text.toStdString();
    EXPECT_EQ(shown.title, QStringLiteral("Find Object"));
    settle();
    solve->close();
}

// The Astrometry card's Annotate button, shown for a solved image, opens the annotation's window, whose primary button
// - Export - is the footer's rightmost.
TEST_F(AnnotateImageWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openViewOf(QStringLiteral("solved-annotate"), solutionCards());
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->canAnnotateCurrentImage());
    an::ui::AnReadoutColumn* const readout = readoutOf(*view);
    ASSERT_NE(readout, nullptr);
    press(astrometryButton(*readout, QStringLiteral("Annotate...")), "the Astrometry card's Annotate button");
    auto* const opened = dynamic_cast<an::ui::AnnotateImageControl*>(windowOf(
        *view, [](QWidget* candidate) { return dynamic_cast<an::ui::AnnotateImageControl*>(candidate) != nullptr; }));
    ASSERT_NE(opened, nullptr) << "the card opened no annotation window";
    EXPECT_TRUE(processEventsUntil([opened]() { return !opened->isWorking(); }, kWaitMs));
    expectTheLookOfAKeptWindow(*opened);
    const QList<const QPushButton*> footer = an::tests::look::footerButtons(*opened);
    ASSERT_FALSE(footer.isEmpty());
    EXPECT_EQ(footer.back()->objectName(), QStringLiteral("annotateExport"));
    opened->close();
}

// New Sequence opens from the palette, a window of its own whose Apply is the footer's rightmost button.
TEST_F(NewSequenceWindow, OpensFromTheObservatory)
{
    const QString id = QStringLiteral("action:") + QString::fromLatin1(an::ui::shell::ids::kNewSequence.data(),
                                                                       an::ui::shell::ids::kNewSequence.size());
    runFromThePalette(QStringLiteral("New Sequence"), id);
    NewSequenceControl* opened = nullptr;
    ASSERT_TRUE(processEventsUntil(
        [&opened]() {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (auto* const control = dynamic_cast<NewSequenceControl*>(widget);
                    control != nullptr && control->isVisible())
                {
                    opened = control;
                }
            }
            return opened != nullptr;
        },
        kWaitMs))
        << "the palette opened no New Sequence window";
    // The window fits its content again a moment after it is shown.
    (void)processEventsUntil([]() { return false; }, 80);
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// Export opens the Export Sequence window over a sequence, from the catalogue's action the view's menu holds.
TEST_F(ExportSequenceWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openSequence(QStringLiteral("to-export"), 3u);
    ASSERT_NE(view, nullptr);
    QAction* const exportSequence = core().actions().action(an::ui::shell::ids::kExport);
    ASSERT_NE(exportSequence, nullptr);
    ASSERT_TRUE(processEventsUntil([exportSequence]() { return exportSequence->isEnabled(); }, kWaitMs))
        << "Export is not offered for a sequence";
    exportSequence->trigger();
    QWidget* const opened = windowOf(*view, [](QWidget* candidate) { return isA<ExportSequenceControl>(candidate); });
    ASSERT_NE(opened, nullptr) << "Export opened no window";
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// Save as opens the application's file dialog, where the format's options are the file type's: on a dialog's surface.
TEST_F(SaveAsWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("to-save"));
    ASSERT_NE(view, nullptr);
    QAction* const saveAs = core().actions().action(an::ui::shell::ids::kSaveAs);
    ASSERT_NE(saveAs, nullptr);
    ASSERT_TRUE(processEventsUntil([saveAs]() { return saveAs->isEnabled(); }, kWaitMs));
    bool shown = false;
    bool onADialogsSurface = false;
    const std::shared_ptr<an::tests::app::WindowPoll> poll =
        answerWhenShown([&shown, &onADialogsSurface](QWidget* widget) {
            auto* const dialog = qobject_cast<QFileDialog*>(widget);
            if (dialog == nullptr)
            {
                return false;
            }
            shown = true;
            onADialogsSurface = dialog->palette().color(QPalette::Window) == an::ui::tokens().layers.surface1;
            static_cast<QDialog*>(dialog)->reject();
            return true;
        });
    saveAs->trigger();
    EXPECT_TRUE(processEventsUntil([&shown]() { return shown; }, kWaitMs)) << "Save as opened no file dialog";
    poll->ended = true;
    EXPECT_TRUE(onADialogsSurface);
    settle();
}

// The floating toolbar's settings button opens the Autostretch settings of the view it floats over, while the view
// shows its image stretched.
TEST_F(AutostretchSettingsWindow, OpensFromTheObservatory)
{
    ImageViewControl* const view = openView(QStringLiteral("stretched"));
    ASSERT_NE(view, nullptr);
    an::ui::AnFloatingToolbar* const toolbar = window().workspacePage()->toolbar();
    ASSERT_NE(toolbar, nullptr);
    QComboBox* const mode = view->chromeActions().displayMode;
    ASSERT_NE(mode, nullptr);
    if (mode->currentIndex() != 1)
    {
        QAbstractButton* const autostretch = toolbar->displayModeSegment(1);
        ASSERT_NE(autostretch, nullptr);
        autostretch->click();
        settle();
    }
    ASSERT_NE(view->chromeActions().autostretchSettings, nullptr);
    press(toolbar->buttonFor(view->chromeActions().autostretchSettings), "the toolbar's Autostretch settings button");
    QWidget* const opened =
        windowOf(*view, [](QWidget* candidate) { return isNamed(candidate, "AutostretchSettingsControl"); });
    ASSERT_NE(opened, nullptr) << "the toolbar opened no Autostretch settings";
    EXPECT_EQ(opened->windowTitle(), QStringLiteral("Autostretch Settings <stretched>"));
    expectTheLookOfAKeptWindow(*opened);
    opened->close();
}

// A catalogue's download opens from the Settings' Catalogs & Data page.
TEST_F(CatalogDownloadWindow, OpensFromTheObservatory)
{
    bool settingsShown = false;
    bool downloadShown = false;
    const std::shared_ptr<an::tests::app::WindowPoll> settingsPoll =
        answerWhenShown([&settingsShown, &downloadShown](QWidget* widget) {
            auto* const settings = qobject_cast<SettingsDialog*>(widget);
            if (settings == nullptr)
            {
                return false;
            }
            settingsShown = true;
            settings->setCurrentPage(QStringLiteral("Catalogs & Data"));
            settle();
            auto* const download = settings->findChild<QPushButton*>(QStringLiteral("gaiaCatalogDownloadButton"));
            EXPECT_NE(download, nullptr);
            if (download != nullptr)
            {
                // The download runs in a loop of its own, from which it is looked at and dismissed.
                const std::shared_ptr<an::tests::app::WindowPoll> downloadPoll =
                    answerWhenShown([&downloadShown](QWidget* inner) {
                        auto* const dialog = qobject_cast<an::ui::CatalogDownloadDialog*>(inner);
                        if (dialog == nullptr)
                        {
                            return false;
                        }
                        downloadShown = true;
                        expectTheLookOfAKeptWindow(*dialog);
                        dialog->reject();
                        return true;
                    });
                download->click();
                downloadPoll->ended = true;
            }
            settings->reject();
            return true;
        });
    QAction* const settings = core().actions().action(an::ui::shell::ids::kSettings);
    ASSERT_NE(settings, nullptr);
    settings->trigger();
    EXPECT_TRUE(processEventsUntil([&settingsShown]() { return settingsShown; }, kWaitMs));
    settingsPoll->ended = true;
    EXPECT_TRUE(downloadShown) << "the page's Download Catalog... opened no download";
    settle();
}

// Manage Favorites... opens from the Favorite Processes menu of the menu tree.
TEST_F(ManageFavoritesWindow, OpensFromTheObservatory)
{
    QMenu* const favourites = window().menuBarOnAlt().menu("menuFavorite_Processes");
    ASSERT_NE(favourites, nullptr);
    QAction* manage = nullptr;
    for (QAction* entry : favourites->actions())
    {
        if (entry->text() == QStringLiteral("Manage Favorites..."))
        {
            manage = entry;
        }
    }
    ASSERT_NE(manage, nullptr) << "the menu offers no Manage Favorites...";

    Answered shown;
    QString name;
    answerTheNextModalDialog(
        [&name](QDialog& dialog) {
            name = dialog.objectName();
            expectTheLookOfAKeptWindow(dialog);
        },
        &shown);
    manage->trigger();
    ASSERT_TRUE(shown.seen) << "Manage Favorites... opened no dialog";
    EXPECT_EQ(name, QStringLiteral("manageFavoriteProcessesDialog"));
    settle();
}

// About AfterNight opens from the top bar's Information menu.
TEST_F(AboutWindow, OpensFromTheObservatory)
{
    auto* const information = window().topBar()->findChild<QToolButton*>(QStringLiteral("topBarInformationButton"));
    ASSERT_TRUE(information != nullptr && information->menu() != nullptr);
    QAction* const about = core().actions().action(an::ui::shell::ids::kAboutAfterNight);
    ASSERT_TRUE(information->menu()->actions().contains(about));

    Answered shown;
    bool isAbout = false;
    answerTheNextModalDialog(
        [&isAbout](QDialog& dialog) {
            isAbout = isA<::AboutDialog>(&dialog);
            if (isAbout)
            {
                expectTheLookOfAKeptWindow(dialog);
            }
        },
        &shown);
    about->trigger();
    ASSERT_TRUE(shown.seen) << "About AfterNight opened no dialog";
    EXPECT_TRUE(isAbout) << shown.text.toStdString();
    settle();
}

// System Information opens from the top bar's Information menu.
TEST_F(SystemInformationWindow, OpensFromTheObservatory)
{
    auto* const information = window().topBar()->findChild<QToolButton*>(QStringLiteral("topBarInformationButton"));
    ASSERT_TRUE(information != nullptr && information->menu() != nullptr);
    QAction* const system = core().actions().action(an::ui::shell::ids::kSystemInformation);
    ASSERT_TRUE(information->menu()->actions().contains(system));

    Answered shown;
    bool isSystem = false;
    answerTheNextModalDialog(
        [&isSystem](QDialog& dialog) {
            isSystem = isA<::SystemInformationDialog>(&dialog);
            if (isSystem)
            {
                expectTheLookOfAKeptWindow(dialog);
            }
        },
        &shown);
    system->trigger();
    ASSERT_TRUE(shown.seen) << "System Information opened no dialog";
    EXPECT_TRUE(isSystem) << shown.text.toStdString();
    settle();
}

} // namespace an::tests::observatory
