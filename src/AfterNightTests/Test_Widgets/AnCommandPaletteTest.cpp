// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command palette: a popup 640 px wide on the third surface, a 48 px query row, a body of groups under caps
// headers with rows the palette's row height, and a 36 px footer on the second surface. It shows what a command source
// answers the query, six rows a group until the group's header is clicked; the keyboard moves the selection - arrows,
// paging, Home and End, Tab to the next group - Enter runs the selected command and closes, Esc or a click outside
// closes. The selected row is the accent's container in its text colour; a dimmed command does not run; the star of a
// favoritable row toggles it. Assistive technology reads a combo box with a list. The palette knows no command.

#include "RenderTestSupport.h"

#include "components/AnCommandPalette.h"
#include "components/ICommandSource.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAccessibleInterface>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include <cstdlib>
#include <functional>
#include <vector>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;

using an::tests::widgets::recordedText;
using an::tests::widgets::RenderedPicture;
using an::ui::AnCommandPalette;
using an::ui::CommandRow;
using an::ui::CommandRowGroup;
using an::ui::DesignTokens;
using namespace an::ui::parts;

CommandRow row(const QString& id, const QString& title, const QString& subtitle = {}, const bool enabled = true)
{
    CommandRow made;
    made.id = id;
    made.title = title;
    made.subtitle = subtitle;
    made.iconKey = QStringLiteral("tune");
    made.enabled = enabled;
    return made;
}

CommandRowGroup group(const QString& id, const QString& title, std::vector<CommandRow> rows, const int limit = 0)
{
    return CommandRowGroup{id, title, std::move(rows), limit};
}

/// A source answering every query with the same groups, recording what it is asked.
class StubSource final : public an::ui::ICommandSource
{
public:
    void refresh() override
    {
        ++refreshes;
    }

    [[nodiscard]] std::vector<CommandRowGroup> query(const QString& text) const override
    {
        queries.append(text);
        return answer;
    }

    bool run(const QString& id) override
    {
        runs.append(id);
        for (const CommandRowGroup& shown : answer)
        {
            for (const CommandRow& command : shown.rows)
            {
                if (command.id == id)
                {
                    return command.enabled;
                }
            }
        }
        return false;
    }

    void toggleFavorite(const QString& id) override
    {
        toggled.append(id);
        for (CommandRowGroup& shown : answer)
        {
            for (CommandRow& command : shown.rows)
            {
                if (command.id == id && command.favoritable)
                {
                    command.favorite = !command.favorite;
                }
            }
        }
    }

    std::vector<CommandRowGroup> answer;
    mutable QStringList queries;
    QStringList runs;
    QStringList toggled;
    int refreshes = 0;
};

/// Processes (eight, the third dimmed, the first a favoritable one), images (two) and settings (one).
std::vector<CommandRowGroup> ordinaryAnswer()
{
    std::vector<CommandRow> processes;
    for (int number = 0; number < 8; ++number)
    {
        processes.push_back(row(QStringLiteral("p%1").arg(number), QStringLiteral("Process %1").arg(number),
                                QStringLiteral("Category"), number != 2));
    }
    processes[2].subtitle = QStringLiteral("No selected image to process");
    processes[0].favoritable = true;
    processes[0].shortcut = QKeySequence(QStringLiteral("Ctrl+H"));
    return {group(QStringLiteral("processes"), QStringLiteral("Processes"), processes, 6),
            group(QStringLiteral("images"), QStringLiteral("Images"),
                  {row(QStringLiteral("i0"), QStringLiteral("M20_FINAL"), QStringLiteral("D:\\Astro\\M20.fits")),
                   row(QStringLiteral("i1"), QStringLiteral("Open file..."), QStringLiteral("File"))},
                  6),
            group(QStringLiteral("settings"), QStringLiteral("Settings"),
                  {row(QStringLiteral("s0"), QStringLiteral("Interface > Theme"), QStringLiteral("Appearance"))}, 6)};
}

/// The window height the palette is opened for in most cases.
constexpr int kWindowHeight = 900;

/// A palette over a stub source, opened under a point.
struct OpenPalette
{
    explicit OpenPalette(std::vector<CommandRowGroup> answer = ordinaryAnswer(), const int windowHeight = kWindowHeight)
    {
        source.answer = std::move(answer);
        palette.setSource(&source);
        palette.setAttribute(Qt::WA_DontShowOnScreen, true);
        palette.open(kAnchor, windowHeight);
    }

    void press(const int key, const Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::keyClick(palette.queryField(), static_cast<Qt::Key>(key), modifiers);
    }

    static constexpr QPoint kAnchor{700, 60};
    StubSource source;
    AnCommandPalette palette;
};

// The surface is 640 px wide, centred under the point it opens from and 12 px below it; the query row is 48 px, the
// footer 36 px, a group's header and its rows lie between, the rows the palette's row height in each density. A body
// longer than 60 % of the window's height scrolls.
TEST(CommandPalette, Geometry)
{
    forEachLook([](const DesignTokens& t) {
        OpenPalette shown;
        const QRect surface = shown.palette.surfaceRect();
        EXPECT_EQ(surface.width(), kPaletteWidth);
        EXPECT_EQ(kPaletteWidth, 640);
        const QPoint topCentre = shown.palette.mapToGlobal(QPoint(surface.center().x() + 1, surface.top()));
        EXPECT_LE(std::abs(topCentre.x() - OpenPalette::kAnchor.x()), 1);
        EXPECT_EQ(topCentre.y(), OpenPalette::kAnchor.y() + kPaletteOffset);
        EXPECT_EQ(kPaletteOffset, 12);

        EXPECT_EQ(shown.palette.queryRowRect().height(), kPaletteQueryRow);
        EXPECT_EQ(kPaletteQueryRow, 48);
        EXPECT_EQ(shown.palette.queryRowRect().top(), surface.top());
        EXPECT_EQ(shown.palette.footerRect().height(), kPaletteFooter);
        EXPECT_EQ(kPaletteFooter, 36);
        EXPECT_EQ(shown.palette.footerRect().bottom(), surface.bottom());

        // The query takes the row between the search icon and the key hint.
        const QRect field = shown.palette.queryField()->geometry();
        EXPECT_EQ(field.left(), surface.left() + kPaletteQueryPaddingStart + kPaletteQueryIcon + kPaletteQueryGap);
        EXPECT_GT(field.width(), kPaletteWidth / 2) << field.width();
        EXPECT_TRUE(shown.palette.queryRowRect().contains(field));

        const QRect first = shown.palette.rowRect(QStringLiteral("p0"));
        ASSERT_FALSE(first.isEmpty());
        EXPECT_EQ(first.height(), t.metrics.rowPalette);
        EXPECT_EQ(first.width(), kPaletteWidth - 2 * kPaletteBodyPadding) << "a row spans the body";
        const QRect header = shown.palette.headerRect(QStringLiteral("processes"));
        ASSERT_FALSE(header.isEmpty());
        EXPECT_EQ(header.height(), kPaletteHeaderPaddingTop + kPaletteHeaderLine + kPaletteHeaderPaddingBottom);
        EXPECT_LE(header.bottom(), first.top());
        EXPECT_GE(header.top(), shown.palette.queryRowRect().bottom());
        EXPECT_EQ(first.left() - surface.left(), kPaletteBodyPadding);
        EXPECT_LE(shown.palette.bodyRect().height(), kWindowHeight * kPaletteBodyShare);
        EXPECT_FALSE(shown.palette.list()->verticalScrollBar()->isVisible()) << "a short body does not scroll";
    });

    std::vector<CommandRow> many;
    for (int number = 0; number < 60; ++number)
    {
        many.push_back(row(QStringLiteral("r%1").arg(number), QStringLiteral("Row %1").arg(number)));
    }
    OpenPalette tall({group(QStringLiteral("processes"), QStringLiteral("Processes"), many)}, 500);
    EXPECT_LE(tall.palette.bodyRect().height(), 500 * kPaletteBodyShare);
    EXPECT_GT(tall.palette.bodyRect().height(),
              500 * kPaletteBodyShare - tall.palette.rowRect(QStringLiteral("r0")).height() - 1);
    EXPECT_TRUE(tall.palette.list()->verticalScrollBar()->isVisible()) << "a long body scrolls";
}

// It opens with what the source answers an empty query, read again first, the first row selected; typing asks the
// source again with each keystroke's text, and the first row of the answer is selected.
TEST(CommandPalette, ShowsWhatTheSourceAnswers)
{
    OpenPalette shown;
    EXPECT_EQ(shown.source.refreshes, 1);
    ASSERT_FALSE(shown.source.queries.isEmpty());
    EXPECT_EQ(shown.source.queries.front(), QString());
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0"));
    EXPECT_EQ(shown.palette.shownRowIds(),
              (QStringList{QStringLiteral("p0"), QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("p3"),
                           QStringLiteral("p4"), QStringLiteral("p5"), QStringLiteral("i0"), QStringLiteral("i1"),
                           QStringLiteral("s0")}))
        << "six rows of a group of eight";
    EXPECT_EQ(shown.palette.queryField()->placeholderText(), QStringLiteral("Type a command, process or file..."));

    shown.palette.setSelectedId(QStringLiteral("i1"));
    QTest::keyClicks(shown.palette.queryField(), QStringLiteral("hi"));
    EXPECT_EQ(shown.source.queries.mid(shown.source.queries.size() - 2),
              (QStringList{QStringLiteral("h"), QStringLiteral("hi")}));
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0")) << "a new answer selects its first row";

    shown.source.answer = {};
    QTest::keyClick(shown.palette.queryField(), Qt::Key_X);
    EXPECT_TRUE(shown.palette.shownRowIds().isEmpty());
    EXPECT_TRUE(shown.palette.selectedId().isEmpty());
    EXPECT_GT(shown.palette.bodyRect().height(), 0) << "an answer of nothing says so";
}

// Up and Down move the selection from row to row across the groups, past their headers, and stop at the ends; Page Up
// and Page Down move a page; Home and End go to the first and the last row.
TEST(CommandPalette, ArrowsPagingHomeAndEndMoveTheSelection)
{
    OpenPalette shown;
    shown.press(Qt::Key_Up);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0")) << "the first row stays";
    for (int step = 0; step < 6; ++step)
    {
        shown.press(Qt::Key_Down);
    }
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("i0")) << "past the header of Images";
    shown.press(Qt::Key_Up);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p5"));
    shown.press(Qt::Key_End);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("s0"));
    shown.press(Qt::Key_Down);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("s0")) << "the last row stays";
    shown.press(Qt::Key_Home);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0"));
    shown.press(Qt::Key_PageDown);
    const QString paged = shown.palette.selectedId();
    EXPECT_NE(paged, QStringLiteral("p0"));
    EXPECT_NE(paged, QStringLiteral("p1")) << "a page is more than one row";
    shown.press(Qt::Key_PageUp);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0"));
    EXPECT_TRUE(shown.palette.queryField()->text().isEmpty()) << "the keys move the selection, not the text";
}

// Tab goes to the first row of the next group, from the last group to the first; Shift+Tab to the previous group's.
TEST(CommandPalette, TabMovesToTheNextGroup)
{
    OpenPalette shown;
    shown.press(Qt::Key_Down);
    shown.press(Qt::Key_Tab);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("i0"));
    shown.press(Qt::Key_Tab);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("s0"));
    shown.press(Qt::Key_Tab);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0"));
    shown.press(Qt::Key_Backtab, Qt::ShiftModifier);
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("s0"));
    EXPECT_TRUE(shown.palette.isVisible());
    EXPECT_EQ(shown.palette.focusWidget(), shown.palette.queryField()) << "Tab leaves the focus in the field";
}

// Enter runs the selected command and closes, saying what ran; Esc closes and runs nothing.
TEST(CommandPalette, EnterRunsAndEscCloses)
{
    {
        OpenPalette shown;
        QSignalSpy ran(&shown.palette, &AnCommandPalette::commandRun);
        QSignalSpy closed(&shown.palette, &AnCommandPalette::closed);
        shown.press(Qt::Key_Down);
        shown.press(Qt::Key_Return);
        EXPECT_EQ(shown.source.runs, QStringList{QStringLiteral("p1")});
        ASSERT_EQ(ran.count(), 1);
        EXPECT_EQ(ran.front().front().toString(), QStringLiteral("p1"));
        EXPECT_FALSE(shown.palette.isVisible());
        EXPECT_EQ(closed.count(), 1);
    }
    {
        OpenPalette shown;
        QSignalSpy closed(&shown.palette, &AnCommandPalette::closed);
        shown.press(Qt::Key_Escape);
        EXPECT_FALSE(shown.palette.isVisible());
        EXPECT_EQ(closed.count(), 1);
        EXPECT_TRUE(shown.source.runs.isEmpty());
    }
    {
        OpenPalette shown;
        QTest::mouseClick(shown.palette.list()->viewport(), Qt::LeftButton, {},
                          shown.palette.list()->viewport()->mapFrom(
                              &shown.palette, shown.palette.rowRect(QStringLiteral("i0")).center()));
        EXPECT_EQ(shown.source.runs, QStringList{QStringLiteral("i0")}) << "a click runs the row";
        EXPECT_FALSE(shown.palette.isVisible());
    }
}

// A dimmed command, which cannot run now, is selected and read but not run, by Enter or by a click.
TEST(CommandPalette, ADimmedCommandDoesNotRun)
{
    OpenPalette shown;
    shown.palette.setSelectedId(QStringLiteral("p2"));
    shown.press(Qt::Key_Enter);
    QTest::mouseClick(shown.palette.list()->viewport(), Qt::LeftButton, {},
                      shown.palette.list()->viewport()->mapFrom(&shown.palette,
                                                                shown.palette.rowRect(QStringLiteral("p2")).center()));
    EXPECT_TRUE(shown.source.runs.isEmpty());
    EXPECT_TRUE(shown.palette.isVisible());
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p2"));
    EXPECT_NEAR(recordedText(shown.palette, QStringLiteral("Process 2")).opacity, kDisabledOpacity, 0.01)
        << "drawn dimmed";
    // The reason may be elided in the middle; it starts as it does.
    EXPECT_FALSE(recordedText(shown.palette, QStringLiteral("No sel")).text.isEmpty())
        << "its reason is its subtitle; " << an::tests::widgets::describedTexts(shown.palette).toStdString();
}

// A click outside the surface closes the palette - on its shadow, or on another window, which Qt hands the open popup
// as a press outside it - and runs nothing.
TEST(CommandPalette, AClickOutsideCloses)
{
    {
        OpenPalette shown;
        const QRect surface = shown.palette.surfaceRect();
        ASSERT_GT(surface.top(), 0) << "the shadow lies around the surface";
        QTest::mouseClick(&shown.palette, Qt::LeftButton, {}, QPoint(surface.center().x(), surface.top() / 2));
        EXPECT_FALSE(shown.palette.isVisible());
    }
    {
        OpenPalette shown;
        ASSERT_TRUE(shown.palette.isVisible());
        QTest::mouseClick(&shown.palette, Qt::LeftButton, {}, QPoint(-40, shown.palette.height() + 40));
        EXPECT_FALSE(shown.palette.isVisible());
        EXPECT_TRUE(shown.source.runs.isEmpty());
    }
    {
        OpenPalette shown;
        QTest::mouseClick(&shown.palette, Qt::LeftButton, {}, shown.palette.queryRowRect().center());
        EXPECT_TRUE(shown.palette.isVisible()) << "a click inside keeps it";
    }
}

// The selected row is the accent's container, its title and subtitle in the container's text; another row lies on the
// surface in the primary text.
TEST(CommandPalette, TheSelectedRowIsTheAccentsContainer)
{
    forEachLook([](const DesignTokens& t) {
        OpenPalette shown;
        shown.palette.setSelectedId(QStringLiteral("i0"));
        const QRect selected = shown.palette.rowRect(QStringLiteral("i0"));
        const QRect other = shown.palette.rowRect(QStringLiteral("p1"));
        forEachRatio(shown.palette, Qt::transparent, [&](const RenderedPicture& picture) {
            // The row's middle, right of the selection bar and left of the icon.
            expectColour(picture.at(selected.left() + kPaletteSelectedBar + 3, selected.center().y()),
                         t.colors.accentContainer, "the selected row");
            expectColour(picture.at(other.left() + kPaletteSelectedBar + 3, other.center().y()), t.layers.surface3,
                         "another row");
            expectColour(picture.at(selected.left() + 1, selected.center().y()), t.colors.accentPrimary,
                         "the selection bar");
        });
        expectColour(recordedText(shown.palette, QStringLiteral("M20_FINAL")).colour, t.colors.textOnAccentContainer,
                     "the selected title");
        expectColour(recordedText(shown.palette, QStringLiteral("M20.fits")).colour, t.colors.textOnAccentContainer,
                     "the selected subtitle");
        expectColour(recordedText(shown.palette, QStringLiteral("Process 1")).colour, t.colors.textPrimary,
                     "another title");
        expectColour(recordedText(shown.palette, QStringLiteral("Category")).colour, t.colors.textSecondary,
                     "another subtitle");
        const QFont title = recordedText(shown.palette, QStringLiteral("Process 1")).font;
        EXPECT_EQ(an::tests::widgets::logicalPixels(title), t.metrics.fontBody);
        const QFont header = recordedText(shown.palette, QStringLiteral("Processes")).font;
        EXPECT_EQ(an::tests::widgets::logicalPixels(header), t.metrics.fontCaption);
        EXPECT_EQ(header.capitalization(), QFont::AllUppercase);
        expectColour(recordedText(shown.palette, QStringLiteral("Processes")).colour, t.colors.textTertiary,
                     "a header");
    });
}

// The surface is the third surface with the query row's hairline under it, the footer the second surface; the footer
// says how to move, run and close, the three prefixes, and offers the menus.
TEST(CommandPalette, RendersItsSurfaceQueryRowAndFooter)
{
    forEachLook([](const DesignTokens& t) {
        OpenPalette shown;
        const QRect query = shown.palette.queryRowRect();
        const QRect footer = shown.palette.footerRect();
        const QRect body = shown.palette.bodyRect();
        forEachRatio(shown.palette, Qt::transparent, [&](const RenderedPicture& picture) {
            expectColour(picture.at(query.right() - 4, query.top() + 4), t.layers.surface3, "the query row");
            expectColour(picture.at(body.center().x(), body.bottom() - 1), t.layers.surface3, "the body's padding");
            expectColour(picture.at(footer.center().x(), footer.bottom() - 4), t.layers.surface2, "the footer");
        });
        for (const char* text : {"navigate", "run", "close", "processes", "images", "settings", "all menus"})
        {
            EXPECT_FALSE(recordedText(shown.palette, QString::fromLatin1(text)).text.isEmpty()) << text;
        }
        QSignalSpy menus(&shown.palette, &AnCommandPalette::allMenusRequested);
        ASSERT_NE(shown.palette.allMenusButton(), nullptr);
        shown.palette.allMenusButton()->click();
        EXPECT_EQ(menus.count(), 1);
        EXPECT_FALSE(shown.palette.isVisible());
    });
}

// The star of a favoritable row shows when the row is hovered or selected - filled for a favourite, always - and a
// click on it toggles the favourite without running the row; a row that is no process has none.
TEST(CommandPalette, TheStarTogglesAFavourite)
{
    OpenPalette shown;
    EXPECT_FALSE(shown.palette.starRect(QStringLiteral("p0")).isEmpty()) << "the selected favoritable row";
    EXPECT_TRUE(shown.palette.starRect(QStringLiteral("p1")).isEmpty()) << "a row that is no process";
    QTest::mouseClick(shown.palette.list()->viewport(), Qt::LeftButton, {},
                      shown.palette.list()->viewport()->mapFrom(&shown.palette,
                                                                shown.palette.starRect(QStringLiteral("p0")).center()));
    EXPECT_EQ(shown.source.toggled, QStringList{QStringLiteral("p0")});
    EXPECT_TRUE(shown.source.runs.isEmpty());
    EXPECT_TRUE(shown.palette.isVisible());
    EXPECT_EQ(shown.palette.selectedId(), QStringLiteral("p0"));

    // A favourite shows its star when it is neither hovered nor selected.
    shown.palette.setSelectedId(QStringLiteral("i0"));
    EXPECT_FALSE(shown.palette.starRect(QStringLiteral("p0")).isEmpty());
}

// A group shows six rows until its header is clicked, which shows them all.
TEST(CommandPalette, AGroupShowsSixRowsUntilItsHeaderIsClicked)
{
    OpenPalette shown;
    EXPECT_TRUE(shown.palette.rowRect(QStringLiteral("p6")).isEmpty());
    const QRect header = shown.palette.headerRect(QStringLiteral("processes"));
    QTest::mouseClick(shown.palette.list()->viewport(), Qt::LeftButton, {},
                      shown.palette.list()->viewport()->mapFrom(&shown.palette, header.center()));
    EXPECT_FALSE(shown.palette.rowRect(QStringLiteral("p6")).isEmpty());
    EXPECT_FALSE(shown.palette.rowRect(QStringLiteral("p7")).isEmpty());
    EXPECT_TRUE(shown.source.runs.isEmpty());
    EXPECT_FALSE(recordedText(shown.palette, QStringLiteral("Process 7")).text.isEmpty());
}

// Assistive technology reads a combo box named for the palette, holding the editable query and the list of its rows,
// each row named by its title and subtitle; a dimmed row says it is unavailable, and why.
TEST(CommandPalette, IsAComboBoxWithAList)
{
    OpenPalette shown;
    QAccessibleInterface* const palette = QAccessible::queryAccessibleInterface(&shown.palette);
    ASSERT_NE(palette, nullptr);
    EXPECT_EQ(palette->role(), QAccessible::ComboBox);
    EXPECT_EQ(palette->text(QAccessible::Name), QStringLiteral("Command palette"));

    QAccessibleInterface* field = nullptr;
    QAccessibleInterface* list = nullptr;
    for (int index = 0; index < palette->childCount(); ++index)
    {
        QAccessibleInterface* const child = palette->child(index);
        ASSERT_NE(child, nullptr);
        if (child->role() == QAccessible::EditableText)
        {
            field = child;
        }
        if (child->role() == QAccessible::List)
        {
            list = child;
        }
    }
    ASSERT_NE(field, nullptr) << "the query";
    ASSERT_NE(list, nullptr) << "the rows";
    EXPECT_EQ(field->object(), shown.palette.queryField());

    QStringList names;
    for (int index = 0; index < list->childCount(); ++index)
    {
        QAccessibleInterface* const item = list->child(index);
        ASSERT_NE(item, nullptr);
        names.append(item->text(QAccessible::Name));
    }
    EXPECT_TRUE(names.contains(QStringLiteral("Process 1, Category")))
        << names.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(names.contains(QStringLiteral("M20_FINAL, D:\\Astro\\M20.fits")));
    EXPECT_TRUE(names.contains(QStringLiteral("Process 2, unavailable: No selected image to process")));
    EXPECT_TRUE(names.contains(QStringLiteral("Processes, 2 more"))) << "a header says what it hides";
}

} // namespace
