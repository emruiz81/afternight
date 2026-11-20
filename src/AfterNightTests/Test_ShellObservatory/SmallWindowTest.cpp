// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Observatory window at the sizes a person works at, on every page: at 1024 x 640, its least size, nothing is
/// clipped in either density; under 1200 px of width the columns narrow and under 820 px of height the readout's cards
/// start folded; at the three display sizes - 1920 x 1080, 1536 x 864 and 1280 x 720, a maximised window being the
/// display less the taskbar and its title bar - nothing scrolls but a task's body and the image keeps its floor at the
/// first two, and nothing is clipped at the third; and each page renders at 125, 150 and 200 %.
///
/// What a size leaves of a page depends on how wide its font draws text, and the suites' own platform draws a box font
/// about twice the interface's width, which overflows what the interface's font does not: the cases of the least window
/// and of the display sizes are measured natively (`QT_QPA_PLATFORM=windows`), as the gate measures them, and reported
/// skipped under the suites' platform; the columns' widths, the readout's folds and the renders hold everywhere.

#include "ObservatoryPages.h"

#include "chrome/PaletteField.h"
#include "chrome/TopBar.h"
#include "fixtures/ClippedControls.h"
#include "fixtures/ObservatoryLook.h"

#include "components/AnReadoutCard.h"
#include "components/AnReadoutColumn.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractScrollArea>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QRectF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <string>
#include <tuple>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::tests::look::nameOf;
using an::tests::look::textIsMeasured;

/// The window a display leaves a maximised window: the display less the taskbar and the title bar.
constexpr QSize kWindowOf1920{1920, 1001};
constexpr QSize kWindowOf1536{1536, 785};
constexpr QSize kWindowOf1280{1280, 641};
constexpr QSize kLeastWindow{1024, 640};

/// The floors of the image beside a task's column, at the floor display (4.7).
constexpr int kImageFloorBesideStandard = 1130;
constexpr int kImageFloorBesideWide = 970;

/// The densities a case runs in.
const std::vector<an::ui::ThemeDensity> kDensities{an::ui::ThemeDensity::Compact, an::ui::ThemeDensity::Comfortable};

[[nodiscard]] std::string densityName(const an::ui::ThemeDensity density)
{
    return density == an::ui::ThemeDensity::Comfortable ? "comfortable" : "compact";
}

/// Every widget of a window shown that lies outside its parent - but the content of a scroll area, which may be taller
/// than its viewport and scrolls, and wider only where its area scrolls sideways - named with where it is. A size
/// leaves these whatever the font; where the platform measures text, what each control needs of it is held too
/// (`clippedControls`).
[[nodiscard]] QStringList outsideTheirParents(const QWidget& root)
{
    QStringList outside;
    for (const QWidget* widget : root.findChildren<QWidget*>())
    {
        if (widget->isWindow() || !widget->isVisibleTo(&root))
        {
            continue;
        }
        const QWidget* const parent = widget->parentWidget();
        if (parent == nullptr || an::tests::look::scrollAreaOfViewport(parent) != nullptr)
        {
            continue;
        }
        if (!parent->rect().contains(widget->geometry()))
        {
            outside.append(nameOf(widget) +
                           QStringLiteral(" (%1,%2 %3x%4) lies outside ")
                               .arg(widget->x())
                               .arg(widget->y())
                               .arg(widget->width())
                               .arg(widget->height()) +
                           nameOf(parent) + QStringLiteral(" (%1x%2)").arg(parent->width()).arg(parent->height()));
        }
    }
    return outside;
}

/// The labels of a window that elide by design - a line whose least width is none, which a part cuts with an ellipsis
/// where its place is short (4.18: "compatible") - and that show at least a character and the ellipsis: narrower than
/// their text, they are not clipped.
[[nodiscard]] QStringList readablyElided(const QWidget& window)
{
    QStringList elided;
    for (const QLabel* label : window.findChildren<QLabel*>())
    {
        if (!label->isVisibleTo(&window) || label->wordWrap() || label->text().isEmpty() ||
            label->minimumSizeHint().width() > 0)
        {
            continue;
        }
        const QString shortest = label->text().left(1) + QChar(0x2026);
        if (label->contentsRect().width() >= label->fontMetrics().horizontalAdvance(shortest))
        {
            elided.append(nameOf(label) + QStringLiteral(" is too small for its text"));
        }
    }
    return elided;
}

/// What is clipped of the window as it is shown now: what any font leaves, and what measured text needs where the
/// platform measures it, but a line that elides readably.
[[nodiscard]] QStringList clippedIn(const QWidget& window)
{
    QStringList clipped = outsideTheirParents(window);
    if (textIsMeasured())
    {
        const QStringList elided = readablyElided(window);
        for (const QString& found : an::tests::look::clippedControls(window))
        {
            const bool readable = std::any_of(elided.begin(), elided.end(),
                                              [&found](const QString& line) { return found.startsWith(line); });
            if (!readable)
            {
                clipped.append(found);
            }
        }
        clipped.append(an::tests::look::unsizedScrollContents(window));
        clipped.removeDuplicates();
    }
    return clipped;
}

/// Whether a scroll area shows a bar.
[[nodiscard]] bool scrolls(const QAbstractScrollArea& area, const QWidget& window, const Qt::Orientation orientation)
{
    const QScrollBar* const bar = orientation == Qt::Vertical ? area.verticalScrollBar() : area.horizontalScrollBar();
    return bar != nullptr && bar->isVisibleTo(&window) && bar->maximum() > bar->minimum();
}

class SizesCase : public PagesCase
{
protected:
    void TearDown() override
    {
        setDensity(an::ui::ThemeDensity::Compact);
        resizeTo(QSize(1536, 864));
        PagesCase::TearDown();
    }

    /// The scroll areas of the window that show a bar, but an image's own and a plot's - which scroll what they show by
    /// its zoom - and a task's body, which scrolls by design: the areas of the page that scroll.
    [[nodiscard]] static QStringList scrollingAreas(const bool homeMayScroll)
    {
        QStringList scrolling;
        const TopLevelProcessBaseControl* const process = shownProcess();
        const an::ui::ProcessPageLayout* const page = process != nullptr ? process->pageLayout() : nullptr;
        for (const QScrollArea* area : window().findChildren<QScrollArea*>())
        {
            if (!area->isVisibleTo(&window()) || area->window() != &window())
            {
                continue;
            }
            const bool image = qobject_cast<const ImageViewControl*>(area->parentWidget()) != nullptr;
            const bool plot = area->objectName() == QStringLiteral("readoutHistogramScrollArea");
            // A task's body: its column's, and what a full page's process scrolls inside itself.
            const bool body =
                page != nullptr && (area == page->body() || (page->fullArea() != nullptr && area != page->fullArea() &&
                                                             page->fullArea()->isAncestorOf(area)));
            if (image || plot || body)
            {
                continue;
            }
            const bool down = scrolls(*area, window(), Qt::Vertical);
            const bool sideways = scrolls(*area, window(), Qt::Horizontal);
            if (homeMayScroll && area == window().homePage() && !sideways)
            {
                continue;
            }
            if (down || sideways)
            {
                scrolling.append(nameOf(area) + (down ? QStringLiteral(" scrolls down") : QString()) +
                                 (sideways ? QStringLiteral(" scrolls sideways") : QString()));
            }
        }
        return scrolling;
    }
};

class SmallWindow : public SizesCase
{
};

class DisplaySizes : public SizesCase
{
};

class Scaling : public SizesCase
{
};

// At its least size, 1024 x 640, nothing of any page is clipped, in either density.
TEST_F(SmallWindow, NothingIsClippedAt1024x640)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        for (const Page page : everyPage())
        {
            SCOPED_TRACE(pageName(page) + ", " + densityName(density));
            ASSERT_NE(open(page), nullptr);
            resizeTo(kLeastWindow);
            if (textIsMeasured())
            {
                EXPECT_EQ(window().size(), kLeastWindow) << "the window's least size is larger than 1024 x 640";
            }
            const QStringList clipped = clippedIn(window());
            EXPECT_TRUE(clipped.isEmpty()) << pageName(page) << " at 1024 x 640, " << densityName(density) << ":\n  "
                                           << clipped.join(QStringLiteral("\n  ")).toStdString();
            leave();
        }
    }
}

// Under 1200 px of width the readout column is 320 px, a task's standard column 320 and a wide one 440, and the
// palette's field 240 px - an icon under 1100; at 1200 they are 360, 360, 520 and 340.
TEST_F(SmallWindow, ColumnsNarrowUnder1200)
{
    ASSERT_NE(open(Page::Workspace), nullptr);
    resizeTo(QSize(1199, 800));
    EXPECT_EQ(window().readoutColumn()->width(), an::ui::parts::kReadoutColumnNarrowWidth);
    EXPECT_EQ(window().topBar()->paletteField()->width(), 240);
    resizeTo(QSize(1099, 800));
    EXPECT_EQ(an::ui::observatory::PaletteField::formFor(window().width()),
              an::ui::observatory::PaletteField::Form::Icon);
    resizeTo(QSize(1200, 800));
    EXPECT_EQ(window().readoutColumn()->width(), an::ui::parts::kReadoutColumnWidth);
    EXPECT_EQ(window().topBar()->paletteField()->width(), 340);
    leave();

    for (const auto& [descriptor, narrow, wide] :
         {std::tuple<std::string, int, int>{"filters.actionGaussian_Blur", an::ui::parts::kPageColumnStandardNarrow,
                                            an::ui::parts::kPageColumnStandard},
          std::tuple<std::string, int, int>{"tone_brightness.actionAdjustBrightness_Contrast",
                                            an::ui::parts::kPageColumnWideNarrow, an::ui::parts::kPageColumnWide}})
    {
        SCOPED_TRACE(descriptor);
        ASSERT_NE(openView(QStringLiteral("narrow-columns")), nullptr);
        ASSERT_NE(launch(descriptor), nullptr);
        const an::ui::ProcessPageLayout* const page = shownProcess()->pageLayout();
        ASSERT_NE(page, nullptr);
        resizeTo(QSize(1199, 800));
        EXPECT_EQ(page->column()->width(), narrow);
        resizeTo(QSize(1200, 800));
        EXPECT_EQ(page->column()->width(), wide);
        leave();
    }
}

// Under 820 px of height the readout's Image card starts folded, and in a window too short to hold the cards open the
// Astrometry card too; a taller window starts with both open. A person's own folds are kept whatever the height.
TEST_F(SmallWindow, ReadoutCardsStartFoldedInAShortWindow)
{
    an::ui::AnReadoutColumn column;
    column.setWindowSize(QSize(1536, 864));
    EXPECT_FALSE(column.card(an::ui::ReadoutCardId::Image)->isFolded());
    EXPECT_FALSE(column.card(an::ui::ReadoutCardId::Astrometry)->isFolded());
    column.setWindowSize(QSize(1536, 819));
    EXPECT_TRUE(column.card(an::ui::ReadoutCardId::Image)->isFolded());
    EXPECT_FALSE(column.card(an::ui::ReadoutCardId::Astrometry)->isFolded());
    column.setWindowSize(QSize(1024, an::ui::parts::kReadoutShorterWindow - 1));
    EXPECT_TRUE(column.card(an::ui::ReadoutCardId::Image)->isFolded());
    EXPECT_TRUE(column.card(an::ui::ReadoutCardId::Astrometry)->isFolded());
    column.setWindowSize(QSize(1536, 1001));
    EXPECT_FALSE(column.card(an::ui::ReadoutCardId::Image)->isFolded());
    EXPECT_FALSE(column.card(an::ui::ReadoutCardId::Astrometry)->isFolded());

    an::ui::AnReadoutColumn remembered;
    remembered.setFoldedCards(QStringLiteral("statistics"));
    remembered.setWindowSize(QSize(1024, 640));
    EXPECT_FALSE(remembered.card(an::ui::ReadoutCardId::Image)->isFolded());
    EXPECT_FALSE(remembered.card(an::ui::ReadoutCardId::Astrometry)->isFolded());
    EXPECT_TRUE(remembered.card(an::ui::ReadoutCardId::Statistics)->isFolded());
}

// At 1920 x 1080 and 1536 x 864 nothing scrolls but a task's body - Home may scroll down at 1536 x 864, at the
// maintainer's word - and the image keeps its floor beside a task's column; in either density.
TEST_F(DisplaySizes, NothingScrollsButATasksBodyAt1920x1080And1536x864)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        for (const QSize size : {kWindowOf1920, kWindowOf1536})
        {
            for (const Page page : everyPage())
            {
                SCOPED_TRACE(pageName(page) + ", " + densityName(density) + ", " + std::to_string(size.width()));
                ASSERT_NE(open(page), nullptr);
                resizeTo(size);
                const QStringList scrolling = scrollingAreas(size == kWindowOf1536 && page == Page::Home);
                EXPECT_TRUE(scrolling.isEmpty())
                    << pageName(page) << " in a " << size.width() << " x " << size.height() << " window, "
                    << densityName(density) << ":\n  " << scrolling.join(QStringLiteral("\n  ")).toStdString();
                leave();
            }
        }
    }
    // The image's floors beside the standard column and the wide one (Curves Transformation's), at the floor display.
    setDensity(an::ui::ThemeDensity::Compact);
    for (const auto& [descriptor, floor] :
         {std::pair<std::string, int>{"filters.actionGaussian_Blur", kImageFloorBesideStandard},
          std::pair<std::string, int>{"transforms.actionCurves_Transformation", kImageFloorBesideWide}})
    {
        SCOPED_TRACE(descriptor);
        ASSERT_NE(openView(QStringLiteral("floor")), nullptr);
        ASSERT_NE(launch(descriptor), nullptr);
        resizeTo(kWindowOf1536);
        EXPECT_GE(shownProcess()->pageLayout()->canvas()->width(), floor) << "the image beside the column";
        leave();
    }
}

// At 1280 x 720 nothing of any page is clipped, in either density.
TEST_F(DisplaySizes, NothingIsClippedAt1280x720)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        for (const Page page : everyPage())
        {
            SCOPED_TRACE(pageName(page) + ", " + densityName(density));
            ASSERT_NE(open(page), nullptr);
            resizeTo(kWindowOf1280);
            const QStringList clipped = clippedIn(window());
            EXPECT_TRUE(clipped.isEmpty()) << pageName(page) << " in a 1280 x 641 window, " << densityName(density)
                                           << ":\n  " << clipped.join(QStringLiteral("\n  ")).toStdString();
            leave();
        }
    }
}

// Each page renders at 125, 150 and 200 % as at 100 %: the picture is the page's size at the ratio, every device pixel
// of it is painted - no seam lets through what lies under the page - and its corners are the colour they are at 100 %.
TEST_F(Scaling, PagesRenderAt125_150_200)
{
    // A colour no theme paints, under the page: a pixel of it left is one the page did not paint.
    const QColor under(255, 0, 255);
    for (const Page page : everyPage())
    {
        SCOPED_TRACE(pageName(page));
        QWidget* const shown = open(page);
        ASSERT_NE(shown, nullptr);
        const an::ui::testing::RenderedPicture plain = an::ui::testing::RenderedPicture::ofWidget(*shown, 1.0, under);
        const QSize logical = shown->size();
        for (const qreal ratio : {1.25, 1.5, 2.0})
        {
            const an::ui::testing::RenderedPicture picture =
                an::ui::testing::RenderedPicture::ofWidget(*shown, ratio, under);
            const QImage& image = picture.image();
            EXPECT_EQ(image.width(), static_cast<int>(std::ceil(logical.width() * ratio)));
            EXPECT_EQ(image.height(), static_cast<int>(std::ceil(logical.height() * ratio)));
            // A scroll bar is translucent over what lies behind its area, which the window paints, not the page.
            QList<QRect> bars;
            for (const QScrollBar* bar : shown->findChildren<QScrollBar*>())
            {
                if (bar->isVisibleTo(shown))
                {
                    bars.append(QRect(bar->mapTo(shown, QPoint(0, 0)), bar->size()));
                }
            }
            const auto underABar = [&bars, ratio](const int x, const int y) {
                const QPointF logicalPoint(x / ratio, y / ratio);
                return std::any_of(bars.begin(), bars.end(), [&logicalPoint](const QRect& bar) {
                    return QRectF(bar).adjusted(-1.0, -1.0, 1.0, 1.0).contains(logicalPoint);
                });
            };
            int unpainted = 0;
            QPoint first(-1, -1);
            // The last device column and row are past the page's logical edge at a fractional ratio.
            const int width = static_cast<int>(std::floor(logical.width() * ratio));
            const int height = static_cast<int>(std::floor(logical.height() * ratio));
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    if (!underABar(x, y) && an::ui::testing::sameColour(image.pixelColor(x, y), under, 0))
                    {
                        if (unpainted == 0)
                        {
                            first = QPoint(x, y);
                        }
                        ++unpainted;
                    }
                }
            }
            EXPECT_EQ(unpainted, 0) << unpainted << " device pixels unpainted at " << ratio << ", the first at "
                                    << first.x() << "," << first.y();
            for (const QPointF corner :
                 {QPointF(1.0, 1.0), QPointF(logical.width() - 2.0, 1.0), QPointF(1.0, logical.height() - 2.0),
                  QPointF(logical.width() - 2.0, logical.height() - 2.0)})
            {
                if (underABar(static_cast<int>(corner.x() * ratio), static_cast<int>(corner.y() * ratio)))
                {
                    continue;
                }
                EXPECT_TRUE(an::ui::testing::sameColour(picture.at(corner.x(), corner.y()),
                                                        plain.at(corner.x(), corner.y()), 3))
                    << "the corner at " << corner.x() << "," << corner.y() << " at " << ratio << " is "
                    << an::ui::testing::colourName(picture.at(corner.x(), corner.y())) << ", at 100 % "
                    << an::ui::testing::colourName(plain.at(corner.x(), corner.y()));
            }
        }
        leave();
    }
}

} // namespace
} // namespace an::tests::observatory
