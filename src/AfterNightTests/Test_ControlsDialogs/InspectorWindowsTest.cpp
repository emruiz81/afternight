// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The FITS header, Exif and summary windows over a view: each reads the view's document on the GUI thread, fills again
/// when the view shows another image, keeps what it shows while a writer holds the image, and holds its action off
/// while it is open.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/ImageEXIFViewerControl.h"
#include "controls/ImageFITSHeaderViewerControl.h"
#include "controls/ImageSummaryViewerControl.h"
#include "controls/ImageViewCompanions.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QTableWidget>

#include <gtest/gtest.h>

namespace
{

using an::tests::dialogs::card;
using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::makeCardDocument;
using an::tests::dialogs::makeView;
using an::tests::dialogs::settle;

/// The rows a window's table shows, each as its three cells.
std::vector<std::array<QString, 3>> tableRows(QWidget& window)
{
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    EXPECT_NE(table, nullptr);
    std::vector<std::array<QString, 3>> rows;
    if (table == nullptr)
    {
        return rows;
    }
    for (int row = 0; row < table->rowCount(); ++row)
    {
        std::array<QString, 3> cells;
        for (int column = 0; column < 3; ++column)
        {
            const QTableWidgetItem* item = table->item(row, column);
            cells[static_cast<std::size_t>(column)] = item ? item->text() : QString();
        }
        rows.push_back(cells);
    }
    return rows;
}

class InspectorWindowsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
        view = makeView("inspected");
    }

    void TearDown() override
    {
        view.reset();
        installed.reset();
        context.reset();
    }

    /// Shows a document of cards in the view.
    void show(std::vector<an::CodecCard> cards)
    {
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(context->engineSession(), std::move(cards), {.displayName = "inspected"});
        ASSERT_TRUE(document.has_value());
        view->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*view));
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    an::tests::dialogs::ViewHandle view;
};

TEST_F(InspectorWindowsTest, TheFitsHeaderWindowShowsTheViewsCardsAndFollowsTheImageItShows)
{
    show({card(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "target"),
          card(an::CodecNamespace::Fits, "EXPTIME", "120.0", "seconds")});

    QWidget* window = an::ui::openViewTool(an::ui::ViewTool::FitsHeader, *view);
    ASSERT_NE(window, nullptr);
    std::vector<std::array<QString, 3>> rows = tableRows(*window);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0][0], QStringLiteral("OBJECT"));
    EXPECT_EQ(rows[0][1], QStringLiteral("'M 42'"));
    EXPECT_EQ(rows[1][2], QStringLiteral("seconds"));
    EXPECT_TRUE(window->windowTitle().contains(QStringLiteral("inspected")));

    show({card(an::CodecNamespace::Fits, "OBJECT", "'M 31'"), card(an::CodecNamespace::Fits, "FILTER", "'Ha'"),
          card(an::CodecNamespace::Fits, "GAIN", "100")});
    rows = tableRows(*window);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[1][1], QStringLiteral("'Ha'"));

    // An image that states no card empties the table rather than keeping the rows of the image before.
    show({});
    EXPECT_TRUE(tableRows(*window).empty());
    window->close();
}

TEST_F(InspectorWindowsTest, TheExifWindowShowsTheViewsExifFieldsAndNothingElse)
{
    show({card(an::CodecNamespace::Fits, "OBJECT", "'M 42'"),
          card(an::CodecNamespace::Tiff, "Exif.Image.Model", "EOS 6D", "Ascii"),
          card(an::CodecNamespace::Tiff, "Exif.Photo.MakerNote", "opaque", "Undefined")});

    QWidget* window = an::ui::openViewTool(an::ui::ViewTool::Exif, *view);
    ASSERT_NE(window, nullptr);
    const std::vector<std::array<QString, 3>> rows = tableRows(*window);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][0], QStringLiteral("Model"));
    EXPECT_EQ(rows[0][1], QStringLiteral("EOS 6D"));
    EXPECT_EQ(rows[0][2], QStringLiteral("Exif.Image.Model"));
    window->close();
}

TEST_F(InspectorWindowsTest, TheSummaryWindowShowsTheViewsSummaryAndCopiesIt)
{
    show({card(an::CodecNamespace::Fits, "OBJECT", "'M 42'")});

    QWidget* opened = an::ui::openViewTool(an::ui::ViewTool::Summary, *view);
    auto* window = dynamic_cast<ImageSummaryViewerControl*>(opened);
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->reportPlainText().contains(QStringLiteral("inspected")))
        << window->reportPlainText().toStdString();
    EXPECT_TRUE(window->reportPlainText().contains(QStringLiteral("16x12"))) << window->reportPlainText().toStdString();

    QPushButton* copy = nullptr;
    for (QPushButton* button : window->findChildren<QPushButton*>())
    {
        if (button->text().contains(QStringLiteral("Copy"), Qt::CaseInsensitive))
        {
            copy = button;
        }
    }
    ASSERT_NE(copy, nullptr);
    copy->click();
    EXPECT_EQ(QApplication::clipboard()->text(), window->reportPlainText());

    // A rename rebuilds the report, so its name line follows the view.
    view->setUniqueName("renamed");
    EXPECT_TRUE(window->windowTitle().contains(QStringLiteral("renamed")));
    EXPECT_TRUE(window->reportPlainText().contains(QStringLiteral("renamed")))
        << window->reportPlainText().toStdString();
    window->close();
}

TEST_F(InspectorWindowsTest, AWindowKeepsWhatItShowsWhileAWriterHoldsTheImageAndNeverWaitsForIt)
{
    show({card(an::CodecNamespace::Fits, "OBJECT", "'M 42'")});
    QWidget* opened = an::ui::openViewTool(an::ui::ViewTool::FitsHeader, *view);
    auto* window = dynamic_cast<ImageFITSHeaderViewerControl*>(opened);
    ASSERT_NE(window, nullptr);
    ASSERT_EQ(tableRows(*window).size(), 1u);

    {
        an::Result<an::ui::ImageDocumentWriter> writer = view->document().write();
        ASSERT_TRUE(writer.hasValue()) << writer.status().message();
        // Asked to fill again while the image is written, the window returns at once and keeps its rows.
        window->recalculate();
        EXPECT_EQ(tableRows(*window).size(), 1u);
    }
    window->recalculate();
    EXPECT_EQ(tableRows(*window).size(), 1u);
    window->close();
}

TEST_F(InspectorWindowsTest, AnInspectorHoldsItsActionOffWhileItIsOpen)
{
    show({card(an::CodecNamespace::Fits, "OBJECT", "'M 42'"),
          card(an::CodecNamespace::Tiff, "Exif.Image.Model", "EOS 6D", "Ascii")});
    const auto enabled = [this](const GlobalViewControlActions action) {
        const auto found = context->actionsEnabled.find(action);
        return found == context->actionsEnabled.end() ? std::optional<bool>() : std::optional<bool>(found->second);
    };

    QWidget* fits = an::ui::openViewTool(an::ui::ViewTool::FitsHeader, *view);
    QWidget* exif = an::ui::openViewTool(an::ui::ViewTool::Exif, *view);
    QWidget* summary = an::ui::openViewTool(an::ui::ViewTool::Summary, *view);
    ASSERT_NE(fits, nullptr);
    ASSERT_NE(exif, nullptr);
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(enabled(GlobalViewControlActions::FITS_HEADER_ACTION), false);
    EXPECT_EQ(enabled(GlobalViewControlActions::EXIF_ACTION), false);
    EXPECT_EQ(enabled(GlobalViewControlActions::SUMMARY_ACTION), false);

    fits->close();
    exif->close();
    summary->close();
    EXPECT_EQ(enabled(GlobalViewControlActions::FITS_HEADER_ACTION), true);
    EXPECT_EQ(enabled(GlobalViewControlActions::EXIF_ACTION), true);
    EXPECT_EQ(enabled(GlobalViewControlActions::SUMMARY_ACTION), true);
}

} // namespace
