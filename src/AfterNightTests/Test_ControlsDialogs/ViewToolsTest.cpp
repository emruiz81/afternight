// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The opener of a view's tool windows, defined beside the windows: each tool opens its own window as a child of the
/// view, the astrometry and plate-solve windows once per view, and the view's own commands open them through it; the
/// inspectors a view offers follow the image it shows whatever room its toolbar has, the Exif window for a field it
/// shows from any raster container.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/ImageAstrometryInfoControl.h"
#include "controls/ImageEXIFViewerControl.h"
#include "controls/ImageFITSHeaderViewerControl.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageStatisticsControl.h"
#include "controls/ImageSummaryViewerControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/PlateSolveControl.h"

#include <QAction>
#include <QToolBar>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{

using an::tests::dialogs::card;
using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::makeCardDocument;
using an::tests::dialogs::makeView;
using an::tests::dialogs::settle;
using an::ui::ViewTool;

class ViewToolsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
        view = makeView("tools");
        std::optional<an::ui::ImageDocument> document = makeCardDocument(
            context->engineSession(), {card(an::CodecNamespace::Fits, "OBJECT", "'M 42'")}, {.displayName = "tools"});
        ASSERT_TRUE(document.has_value());
        view->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*view));
    }

    void TearDown() override
    {
        // Windows the cases opened are the view's children and go with it.
        an::tests::viewer::pumpQuietly();
        view.reset();
        installed.reset();
        context.reset();
    }

    /// How many open windows of a type the view has.
    template <typename WindowT> [[nodiscard]] int openWindows() const
    {
        int count = 0;
        for (QObject* child : view->children())
        {
            if (auto* window = dynamic_cast<WindowT*>(child); window != nullptr && window->isVisible())
            {
                ++count;
            }
        }
        return count;
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    an::tests::dialogs::ViewHandle view;
};

TEST_F(ViewToolsTest, EachToolOpensItsOwnWindowAsAChildOfTheView)
{
    const auto opens = [this](const ViewTool tool, auto* typed) {
        QWidget* window = an::ui::openViewTool(tool, *view);
        EXPECT_NE(window, nullptr);
        if (window == nullptr)
        {
            return;
        }
        EXPECT_NE(dynamic_cast<std::remove_pointer_t<decltype(typed)>*>(window), nullptr);
        EXPECT_EQ(window->parentWidget(), view.get());
        EXPECT_TRUE(window->isVisible());
        EXPECT_TRUE(window->testAttribute(Qt::WA_DeleteOnClose));
        window->close();
    };
    opens(ViewTool::Statistics, static_cast<ImageStatisticsControl*>(nullptr));
    opens(ViewTool::Histogram, static_cast<ImageHistogramControl*>(nullptr));
    opens(ViewTool::FitsHeader, static_cast<ImageFITSHeaderViewerControl*>(nullptr));
    opens(ViewTool::Exif, static_cast<ImageEXIFViewerControl*>(nullptr));
    opens(ViewTool::Summary, static_cast<ImageSummaryViewerControl*>(nullptr));
    opens(ViewTool::AstrometryInfo, static_cast<an::ui::ImageAstrometryInfoControl*>(nullptr));
    opens(ViewTool::PlateSolve, static_cast<an::ui::PlateSolveControl*>(nullptr));
}

TEST_F(ViewToolsTest, TheAstrometryAndPlateSolveWindowsOpenOncePerViewAndAreRaisedWhenAskedAgain)
{
    QWidget* astrometry = an::ui::openViewTool(ViewTool::AstrometryInfo, *view);
    QWidget* again = an::ui::openViewTool(ViewTool::AstrometryInfo, *view);
    EXPECT_EQ(again, astrometry);
    EXPECT_EQ(openWindows<an::ui::ImageAstrometryInfoControl>(), 1);

    QWidget* solve = an::ui::openViewTool(ViewTool::PlateSolve, *view);
    EXPECT_EQ(an::ui::openViewTool(ViewTool::PlateSolve, *view), solve);
    EXPECT_EQ(openWindows<an::ui::PlateSolveControl>(), 1);

    // A window closed is not the one open: asking again opens another.
    astrometry->close();
    QWidget* reopened = an::ui::openViewTool(ViewTool::AstrometryInfo, *view);
    ASSERT_NE(reopened, nullptr);
    EXPECT_TRUE(reopened->isVisible());
    EXPECT_EQ(openWindows<an::ui::ImageAstrometryInfoControl>(), 1);
    reopened->close();
    solve->close();
}

TEST_F(ViewToolsTest, TheOtherToolsOpenAWindowEachTimeAsTheyAlwaysDid)
{
    QWidget* first = an::ui::openViewTool(ViewTool::Summary, *view);
    QWidget* second = an::ui::openViewTool(ViewTool::Summary, *view);
    EXPECT_NE(first, second);
    EXPECT_EQ(openWindows<ImageSummaryViewerControl>(), 2);
    first->close();
    second->close();
}

TEST_F(ViewToolsTest, TheViewsOwnCommandsOpenTheirWindowsThroughTheOpener)
{
    view->executeFITSHeaderViewer();
    view->executeEXIFViewer();
    view->executeSummaryViewer();
    view->executeStatistics();
    view->executeHistogram();
    EXPECT_EQ(openWindows<ImageFITSHeaderViewerControl>(), 1);
    EXPECT_EQ(openWindows<ImageEXIFViewerControl>(), 1);
    EXPECT_EQ(openWindows<ImageSummaryViewerControl>(), 1);
    EXPECT_EQ(openWindows<ImageStatisticsControl>(), 1);
    EXPECT_EQ(openWindows<ImageHistogramControl>(), 1);
    for (QObject* child : view->children())
    {
        if (auto* dialog = dynamic_cast<QDialog*>(child); dialog != nullptr && dialog->isVisible())
        {
            dialog->close();
        }
    }
    ASSERT_TRUE(settle(*view));
}

// The inspectors a view offers follow the image it shows, not the room its toolbar has: the toolbar hides the buttons
// it has no room for, and a hidden action reads as disabled, so the menu's entries followed a narrow view's toolbar.
TEST_F(ViewToolsTest, TheInspectorsAViewOffersFollowItsImageAndNotItsToolbarsRoom)
{
    auto* toolbar = dynamic_cast<CustomToolBar*>(view->findChild<QToolBar*>(QStringLiteral("anImageViewToolbar")));
    ASSERT_NE(toolbar, nullptr);
    QAction* header = nullptr;
    for (QAction* entry : toolbar->actions())
    {
        header = entry->text() == QStringLiteral("FITS Header") ? entry : header;
    }
    ASSERT_NE(header, nullptr);

    // A toolbar with room for every button shows the header's, enabled for an image that states a header.
    toolbar->resize(8192, toolbar->height());
    toolbar->refreshOverflow();
    ASSERT_TRUE(an::tests::viewer::pumpUntil([header]() { return header->isVisible(); }))
        << "the toolbar has room for the header's button";
    ASSERT_TRUE(view->isFitsHeaderActionEnabled());

    // A toolbar with room for nothing: every button it shows goes to its overflow menu.
    toolbar->resize(1, toolbar->height());
    toolbar->refreshOverflow();
    ASSERT_TRUE(an::tests::viewer::pumpUntil([header]() { return !header->isVisible(); }))
        << "the toolbar has no room for the header's button";
    EXPECT_TRUE(view->isStatisticsActionEnabled());
    EXPECT_TRUE(view->isHistogramActionEnabled());
    EXPECT_TRUE(view->isFitsHeaderActionEnabled()) << "the image states a FITS header";
    EXPECT_FALSE(view->isExifActionEnabled()) << "and no Exif field";

    // An image the view shows while its toolbar has no room decides them as well, at once and after the toolbar has
    // laid its buttons out again.
    std::optional<an::ui::ImageDocument> tagged = makeCardDocument(
        context->engineSession(), {card(an::CodecNamespace::Tiff, "Exif.Image.Model", "EOS 6D", "Ascii")},
        {.displayName = "tagged"});
    ASSERT_TRUE(tagged.has_value());
    view->setDocument(std::move(*tagged));
    EXPECT_FALSE(view->isFitsHeaderActionEnabled()) << "the image shown states no FITS header";
    EXPECT_TRUE(view->isExifActionEnabled()) << "and an Exif field";
    ASSERT_TRUE(settle(*view));
    EXPECT_FALSE(header->isVisible()) << "the toolbar still has no room";
    EXPECT_FALSE(view->isFitsHeaderActionEnabled()) << "the image states no FITS header";
    EXPECT_TRUE(view->isExifActionEnabled()) << "and an Exif field";
}

// The Exif window is offered for an Exif field from any container that carries one - a PNG among them - and only for a
// field it shows: a container's own entries and a field the window hides offer nothing. The view offered it for any
// card of a JPEG, a TIFF or a camera RAW file and never for a PNG's, so a PNG's Exif was never reachable and a JPEG
// with a comment opened an empty window.
TEST_F(ViewToolsTest, TheExifWindowIsOfferedForAnExifFieldOfEveryRasterContainer)
{
    const auto shows = [this](std::vector<an::CodecCard> cards) {
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(context->engineSession(), std::move(cards), {.displayName = "exif"});
        ASSERT_TRUE(document.has_value());
        view->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*view));
    };
    shows({card(an::CodecNamespace::Png, "BitDepth", "16"),
           card(an::CodecNamespace::Png, "Exif.Image.Model", "EOS 6D", "Ascii")});
    EXPECT_TRUE(view->isExifActionEnabled()) << "a PNG's Exif field";

    shows({card(an::CodecNamespace::Png, "BitDepth", "16"), card(an::CodecNamespace::Png, "ColorType", "2")});
    EXPECT_FALSE(view->isExifActionEnabled()) << "a PNG's own entries are not Exif";

    shows({card(an::CodecNamespace::Jpeg, "Comment", "a JPEG comment, not an Exif field")});
    EXPECT_FALSE(view->isExifActionEnabled()) << "a JPEG comment is not Exif";

    shows({card(an::CodecNamespace::Tiff, "Exif.Photo.MakerNote", "hidden")});
    EXPECT_FALSE(view->isExifActionEnabled()) << "a field the window hides";

    shows({card(an::CodecNamespace::Fits, "Exif.Photo.FNumber", "a FITS card spelled like a field")});
    EXPECT_FALSE(view->isExifActionEnabled()) << "FITS carries no Exif";
    EXPECT_TRUE(view->isFitsHeaderActionEnabled()) << "it is a FITS header";

    shows({card(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime", "1/125", "Rational")});
    EXPECT_TRUE(view->isExifActionEnabled()) << "a camera file's Exif field";
}

} // namespace
