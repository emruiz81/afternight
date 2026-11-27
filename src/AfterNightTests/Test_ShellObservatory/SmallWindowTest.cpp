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
///
/// The full pages that hold a list of frames - Image Registration, Image List Analyzer, Image Integration's lights -
/// are measured with their frames listed too, under the long names a capture program gives them: what their lists,
/// their figures and their summaries show depends on the frames as much as on the window.

#include "ObservatoryPages.h"

#include "chrome/PaletteField.h"
#include "chrome/TopBar.h"
#include "fixtures/ClippedControls.h"
#include "fixtures/ObservatoryLook.h"
#include "fixtures/PaintRecorder.h"

#include "components/AnReadoutCard.h"
#include "components/AnReadoutColumn.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/widgets/FrameStatisticsDashboard.h"
#include "processes/ImageIntegration/widgets/StageNavigator.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/ImageListAnalyzerProcessUiBuilder.h"
#include "processes/ImageRegistrationProcess.h"
#include "processes/ImageRegistrationProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "widgets/TableWidget.h"

#include <QAbstractScrollArea>
#include <QAbstractTextDocumentLayout>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QList>
#include <QRectF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTableWidgetItem>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
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

/// The window the guide's pictures of a full page are taken in, at the compatible width.
constexpr QSize kGuideWindowOfAFullPage{1280, 900};

/// The windows a full page holding its frames is measured in: the guide's, the three displays' maximised windows.
const std::vector<QSize> kFullPageWindows{kGuideWindowOfAFullPage, kWindowOf1280, kWindowOf1536, kWindowOf1920};

/// A window's size as a failure names it.
[[nodiscard]] std::string sizeName(const QSize size)
{
    return std::to_string(size.width()) + " x " + std::to_string(size.height());
}

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

/// The combo boxes of a window narrower than their size hint whose text field still holds every item whole, its icon
/// and its text: a box sized to its items, whose hint asks besides for the characters a least contents length names,
/// cuts nothing. Each is named as `clippedControls` names a box too narrow for its longest item.
[[nodiscard]] QStringList combosHoldingEveryItem(const QWidget& window)
{
    QStringList holding;
    const an::ui::DesignTokens& t = an::ui::tokens();
    for (const QComboBox* combo : window.findChildren<QComboBox*>())
    {
        if (!combo->isVisibleTo(&window) || combo->isEditable() || combo->count() == 0 ||
            combo->width() >= combo->sizeHint().width())
        {
            continue;
        }
        bool holds = true;
        for (int index = 0; index < combo->count() && holds; ++index)
        {
            QStyleOptionComboBox option;
            option.initFrom(combo);
            option.rect = combo->rect();
            option.currentText = combo->itemText(index);
            option.currentIcon = combo->itemIcon(index);
            option.iconSize = combo->iconSize();
            const QRect field =
                combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
            // The text follows the icon by the label's gap, as the style paints it.
            const int icon =
                combo->itemIcon(index).isNull() ? 0 : combo->iconSize().width() + t.spacing.xs + t.spacing.xs / 2 + 1;
            holds = field.width() >=
                    icon + static_cast<int>(
                               std::ceil(QFontMetricsF(combo->font()).horizontalAdvance(combo->itemText(index))));
        }
        if (holds)
        {
            holding.append(nameOf(combo) + QStringLiteral(" is too narrow for its longest item (%1 < %2)")
                                               .arg(combo->width())
                                               .arg(combo->sizeHint().width()));
        }
    }
    return holding;
}

/// What is clipped of the window as it is shown now: what any font leaves, and what measured text needs where the
/// platform measures it, but a line that elides readably and a combo box that holds every item.
[[nodiscard]] QStringList clippedIn(const QWidget& window)
{
    QStringList clipped = outsideTheirParents(window);
    if (textIsMeasured())
    {
        const QStringList elided = readablyElided(window);
        const QStringList holding = combosHoldingEveryItem(window);
        for (const QString& found : an::tests::look::clippedControls(window))
        {
            const bool readable = std::any_of(elided.begin(), elided.end(),
                                              [&found](const QString& line) { return found.startsWith(line); });
            if (!readable && !holding.contains(found))
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

/// The widgets a layout places in the widget it lays out, its inner layouts' among them.
void collectPlaced(const QLayout& layout, const QWidget& owner, std::vector<const QWidget*>& placed)
{
    for (int index = 0; index < layout.count(); ++index)
    {
        QLayoutItem* const item = layout.itemAt(index);
        if (item == nullptr)
        {
            continue;
        }
        if (const QWidget* const widget = item->widget(); widget != nullptr && widget->parentWidget() == &owner)
        {
            placed.push_back(widget);
        }
        else if (const QLayout* const inner = item->layout(); inner != nullptr)
        {
            collectPlaced(*inner, owner, placed);
        }
    }
}

/// Every two controls that one widget's layout places over each other: a row given less room than its controls take
/// lays the later ones over the earlier, whatever the font. Only what a layout places is compared - a part laid over
/// another by design, a badge or a floating toolbar, is placed by hand - and a stacked layout shows one at a time.
[[nodiscard]] QStringList overlappingControls(const QWidget& root)
{
    QStringList overlapping;
    std::vector<const QWidget*> owners{&root};
    for (const QWidget* widget : root.findChildren<QWidget*>())
    {
        owners.push_back(widget);
    }
    for (const QWidget* owner : owners)
    {
        if (owner->layout() == nullptr || qobject_cast<const QStackedLayout*>(owner->layout()) != nullptr ||
            (owner != &root && !owner->isVisibleTo(&root)))
        {
            continue;
        }
        std::vector<const QWidget*> placed;
        collectPlaced(*owner->layout(), *owner, placed);
        std::erase_if(placed, [&root](const QWidget* widget) {
            return !widget->isVisibleTo(&root) || widget->width() <= 0 || widget->height() <= 0;
        });
        for (std::size_t first = 0; first < placed.size(); ++first)
        {
            for (std::size_t second = first + 1; second < placed.size(); ++second)
            {
                if (placed[first]->geometry().intersects(placed[second]->geometry()))
                {
                    overlapping.append(nameOf(placed[first]) + QStringLiteral(" and ") + nameOf(placed[second]) +
                                       QStringLiteral(" overlap in ") + nameOf(owner));
                }
            }
        }
    }
    return overlapping;
}

/// The labels the tables and trees of a widget cut short: each header painted as it is shown, a label drawn with an
/// ellipsis. A header that paints no label at all is named too, for a case to tell that it looked.
[[nodiscard]] QStringList elidedHeaderLabels(const QWidget& root)
{
    QStringList elided;
    for (QHeaderView* header : root.findChildren<QHeaderView*>())
    {
        if (!header->isVisibleTo(&root) || header->width() <= 0 || header->height() <= 0 || header->count() == 0)
        {
            continue;
        }
        an::ui::testing::PaintRecorder recorder(header->size());
        header->render(&recorder);
        if (recorder.texts().empty())
        {
            elided.append(nameOf(header) + QStringLiteral(" painted no label"));
        }
        for (const an::ui::testing::RecordedText& text : recorder.texts())
        {
            if (text.text.contains(QChar(0x2026)))
            {
                elided.append(nameOf(header) + QStringLiteral(" cuts a label to \"%1\"").arg(text.text));
            }
        }
    }
    return elided;
}

/// The places a text edit breaks a line inside a word - a line that ends on a letter, a digit or a point, and a next
/// line that goes on with one - each named with the text around the break.
[[nodiscard]] QStringList wordsBrokenIn(const QTextEdit& edit)
{
    QStringList broken;
    // The whole document laid out at the edit's width.
    (void)edit.document()->documentLayout()->documentSize();
    const auto inWord = [](const QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('.'); };
    for (QTextBlock block = edit.document()->begin(); block.isValid(); block = block.next())
    {
        const QTextLayout* const layout = block.layout();
        if (layout == nullptr)
        {
            continue;
        }
        const QString text = block.text();
        for (int line = 0; line + 1 < layout->lineCount(); ++line)
        {
            const QTextLine at = layout->lineAt(line);
            const int end = at.textStart() + at.textLength();
            if (end <= 0 || end >= text.size())
            {
                continue;
            }
            if (inWord(text.at(end - 1)) && inWord(text.at(end)))
            {
                broken.append(QStringLiteral("\"%1|%2\" breaks inside a word at %3 px")
                                  .arg(text.mid((std::max)(0, end - 12), (std::min)(end, 12)), text.mid(end, 12))
                                  .arg(edit.viewport()->width()));
            }
        }
    }
    return broken;
}

/// The figures of a frame statistics dashboard it draws narrower than their text, or past its side.
[[nodiscard]] QStringList cutFigures(const ImageIntegration::FrameStatisticsDashboard& dashboard)
{
    QStringList cut;
    const QFontMetricsF metrics(dashboard.font());
    for (const ImageIntegration::FrameStatisticsDashboard::FigureLine& line : dashboard.figureLines())
    {
        const bool labelWhole = line.labelRect.width() >= std::ceil(metrics.horizontalAdvance(line.label));
        const bool valueWhole = line.valueRect.width() >= std::ceil(metrics.horizontalAdvance(line.value));
        if (!labelWhole || !valueWhole || !dashboard.rect().contains(line.valueRect))
        {
            cut.append(QStringLiteral("\"%1 %2\" is drawn in %3 + %4 px of a %5 px dashboard")
                           .arg(line.label, line.value)
                           .arg(line.labelRect.width())
                           .arg(line.valueRect.width())
                           .arg(dashboard.width()));
        }
    }
    return cut;
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

/// A window of its own: the folds a case opens stay with the column for the window's life.
class EveryReadoutCardOpen : public SizesCase
{
};

class DisplaySizes : public SizesCase
{
};

class Scaling : public SizesCase
{
};

/// The full pages that hold a list of frames, measured with their frames listed.
class FullPages : public SizesCase
{
protected:
    /// Light frames written in a folder of the case's own under the names a capture program gives them: long, and with
    /// no place where the rules of text let a line break.
    [[nodiscard]] static QStringList writeLights(const QString& folder, const int count)
    {
        QStringList files;
        for (int index = 0; index < count; ++index)
        {
            files.append(writeImage(QStringLiteral("%1/2023-10-10_00-%2-34__21.00_180.00s_%3.fits")
                                        .arg(folder)
                                        .arg(19 + index, 2, 10, QLatin1Char('0'))
                                        .arg(index, 4, 10, QLatin1Char('0'))));
        }
        return files;
    }

    /// The page of the task shown.
    [[nodiscard]] static const QWidget* shownPage()
    {
        const TopLevelProcessBaseControl* const process = shownProcess();
        return process != nullptr && process->pageLayout() != nullptr ? process->pageLayout()->page() : nullptr;
    }

    /// What is clipped of the window, what overlaps on the page and the labels its lists cut short, at a size.
    [[nodiscard]] static QStringList faultsAt(const QSize size)
    {
        resizeTo(size);
        QStringList faults = clippedIn(window());
        // A toolbar with no room for every entry puts the rest behind its extension button, which reaches them and
        // which the toolbar sizes by the style's own measure: no clipping.
        faults.removeIf([](const QString& fault) {
            return fault.startsWith(QStringLiteral("QToolBarExtension 'qt_toolbar_ext_button'"));
        });
        if (const QWidget* const page = shownPage(); page != nullptr)
        {
            faults.append(overlappingControls(*page));
            faults.append(elidedHeaderLabels(*page));
        }
        else
        {
            faults.append(QStringLiteral("no task's page is shown"));
        }
        // At the friendly sizes nothing scrolls but a task's body.
        if (size == kWindowOf1536 || size == kWindowOf1920)
        {
            faults.append(scrollingAreas(false));
        }
        return faults;
    }
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

// Every card of the readout opened, as a person may open them in a short window where the Image and Astrometry cards
// start folded: at 1024 x 640 the cards scroll in their column and nothing of the workspace is clipped, in either
// density.
TEST_F(EveryReadoutCardOpen, NothingIsClippedAt1024x640)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        SCOPED_TRACE(densityName(density));
        ASSERT_NE(open(Page::Workspace), nullptr);
        window().readoutColumn()->setFoldedCards(QString());
        resizeTo(kLeastWindow);
        ASSERT_TRUE(window().readoutColumn()->foldedCards().isEmpty());
        const QStringList clipped = clippedIn(window());
        EXPECT_TRUE(clipped.isEmpty()) << "a workspace with every card open at 1024 x 640, " << densityName(density)
                                       << ":\n  " << clipped.join(QStringLiteral("\n  ")).toStdString();
        leave();
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

// Image Registration's page with its frames listed, in the guide's window of a full page and in the three displays'
// maximised windows, in either density: nothing is clipped and no control lies over another - its settings beside the
// preview take the room they need - and the frame list's labels read whole, the reference's number and mark among
// them.
TEST_F(FullPages, ImageRegistrationHoldsItsSettingsAndFrameNumbersWhole)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    const QStringList frames = writeLights(QStringLiteral("registration-sizes"), 3);
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        auto* const tool =
            dynamic_cast<ImageRegistrationProcess*>(launch("multi_image_stacking.actionImage_Registration"));
        ASSERT_NE(tool, nullptr);
        tool->addItemsNonBlocking(frames);
        TableWidget* const table = tool->ui().tableWidget;
        ASSERT_TRUE(an::tests::app::processEventsUntil(
            [&]() { return tool->isIdle() && table->rowCount() == frames.size(); }, kProjectOpenMs))
            << "the frames were not listed";
        settle();
        const QTableWidgetItem* const reference = table->verticalHeaderItem(0);
        ASSERT_NE(reference, nullptr);
        ASSERT_GT(reference->text().size(), 1) << "the reference's number has its mark";
        for (const QSize size : kFullPageWindows)
        {
            const QStringList faults = faultsAt(size);
            EXPECT_TRUE(faults.isEmpty())
                << "Image Registration in a " << sizeName(size) << " window, " << densityName(density) << ":\n  "
                << faults.join(QStringLiteral("\n  ")).toStdString();
        }
        // The list a tool keeps for its next opening is left empty.
        tool->ui().clearAll->click();
        settle();
        leave();
    }
}

// Image List Analyzer's page with its images listed, at the same sizes and densities: nothing is clipped, no control
// lies over another, the list's labels read whole, and the summary of the image chosen breaks its name and its path
// between their parts, never inside one - a capture program's name has no place a line may break by the rules of text.
TEST_F(FullPages, ImageListAnalyzerHoldsItsControlsAndBreaksNamesBetweenTheirParts)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    const QStringList frames = writeLights(QStringLiteral("analyzer-sizes"), 3);
    const QString firstName = QFileInfo(frames.front()).fileName();
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        auto* const tool =
            dynamic_cast<ImageListAnalyzerProcess*>(launch("multi_image_stacking.actionImage_List_Analyzer"));
        ASSERT_NE(tool, nullptr);
        tool->addItemsNonBlocking(frames);
        QTextEdit* const summary = tool->ui().imageSummary;
        ASSERT_TRUE(an::tests::app::processEventsUntil(
            [&]() {
                return tool->isIdle() && tool->ui().tableWidget->rowCount() == frames.size() &&
                       summary->toPlainText().remove(QChar(0x200B)).contains(firstName);
            },
            kProjectOpenMs))
            << "the images were not listed and summed up";
        settle();
        for (const QSize size : kFullPageWindows)
        {
            QStringList faults = faultsAt(size);
            faults.append(wordsBrokenIn(*summary));
            EXPECT_TRUE(faults.isEmpty())
                << "Image List Analyzer in a " << sizeName(size) << " window, " << densityName(density) << ":\n  "
                << faults.join(QStringLiteral("\n  ")).toStdString();
        }
        tool->ui().clearAll->click();
        settle();
        leave();
    }
}

// Image Integration's lights page with a night's lights listed, at the same sizes and densities: the frame statistics
// draw every figure whole - the total exposure and the size among them - and the frame list's column headers read
// whole, ISO/Gain and Exposure among them; nothing is clipped and no control lies over another.
TEST_F(FullPages, ImageIntegrationShowsItsLightsFiguresAndColumnsWhole)
{
    if (!textIsMeasured())
    {
        GTEST_SKIP() << "measured natively (QT_QPA_PLATFORM=windows): the suites' box font overflows what the "
                        "interface's does not";
    }
    // Twelve lights of 525 s: an hour and three quarters, the size of the files they are.
    const QStringList files = writeLights(QStringLiteral("integration-sizes"), 12);
    // The job is saved as the page closes, and the next page would open on it.
    const auto forgetTheJob = []() {
        IntegrationDatabase database(ImageIntegration::applicationSessionDatabasePath());
        if (database.initialize())
        {
            (void)database.deleteSession(QStringLiteral("last_session"));
        }
    };
    for (const an::ui::ThemeDensity density : kDensities)
    {
        setDensity(density);
        forgetTheJob();
        auto* const process =
            dynamic_cast<ImageIntegrationProcess*>(launch(an::ui::observatory::IntegrationHubPage::kImageIntegration));
        ASSERT_NE(process, nullptr);
        std::vector<FrameInfo> lights;
        for (const QString& file : files)
        {
            lights.emplace_back(FrameId(file), file,
                                FrameMetadata(FrameType::Light, QSize(5544, 3684), 525.0, -10.0, 11, 30,
                                              QStringLiteral("L"), QStringLiteral("NGC 253"),
                                              QDateTime::currentDateTimeUtc(), QString()));
        }
        process->model()->addFrames(FrameType::Light, lights);
        settle();
        ASSERT_NE(process->stageNavigator(), nullptr);
        process->stageNavigator()->setCurrentPage(ImageIntegration::StagePage::Lights);
        settle();
        const ImageIntegration::FrameStatisticsDashboard* dashboard = nullptr;
        for (const auto* found : process->findChildren<ImageIntegration::FrameStatisticsDashboard*>())
        {
            if (found->frameType() == FrameType::Light && found->isVisibleTo(process))
            {
                dashboard = found;
            }
        }
        ASSERT_NE(dashboard, nullptr) << "the lights page shows its frame statistics";
        for (const QSize size : kFullPageWindows)
        {
            QStringList faults = faultsAt(size);
            ASSERT_FALSE(dashboard->figureLines().empty()) << "the statistics hold the lights";
            faults.append(cutFigures(*dashboard));
            EXPECT_TRUE(faults.isEmpty())
                << "Image Integration's lights in a " << sizeName(size) << " window, " << densityName(density)
                << ":\n  " << faults.join(QStringLiteral("\n  ")).toStdString();
        }
        leave();
        forgetTheJob();
    }
}

} // namespace
} // namespace an::tests::observatory
