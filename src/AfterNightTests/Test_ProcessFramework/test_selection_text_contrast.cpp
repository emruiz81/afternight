// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The text the process windows' tables and item views draw over a highlight or a selection: it is chosen against the
/// fill it sits on, composited over the view's base, so it stays readable in the dark and the light theme - a table
/// row highlighted in any colour, the styled table's selection, and an item view selection the application's style
/// draws.

#include "components/AnStyledTable.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "widgets/TableWidget.h"

#include <QBrush>
#include <QColor>
#include <QCommonStyle>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionViewItem>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <optional>

namespace
{

/// The contrast a text needs over its fill to be read.
constexpr double kReadableContrast = 4.5;

/// Puts a theme preference in force for a case and the one before it back when the case ends, passing or failing.
class ScopedTheme
{
public:
    explicit ScopedTheme(an::ui::ThemePreference preference) : m_before(an::ui::themePreference())
    {
        an::ui::setThemePreference(preference);
        an::ui::refreshTokens();
    }
    ~ScopedTheme()
    {
        an::ui::setThemePreference(m_before);
        an::ui::refreshTokens();
    }
    ScopedTheme(const ScopedTheme&) = delete;
    ScopedTheme& operator=(const ScopedTheme&) = delete;

private:
    an::ui::ThemePreference m_before;
};

/// A style that draws nothing and keeps the palette it was asked to draw an item view item with.
class RecordingStyle final : public QCommonStyle
{
public:
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override
    {
        if (element == CE_ItemViewItem && option != nullptr)
        {
            *m_drawnWith = option->palette;
            return;
        }
        QCommonStyle::drawControl(element, option, painter, widget);
    }

    /// Where the palette is kept: the proxy owns this style, so the case reads it through a slot of its own.
    std::optional<QPalette>* m_drawnWith = nullptr;
};

/// The palette the application's style hands its base style for an item view item drawn over a base colour, selected
/// or not; empty when it drew nothing.
[[nodiscard]] std::optional<QPalette> itemDrawnWith(const QColor& base, bool selected, const QColor& sentinel)
{
    std::optional<QPalette> drawnWith;
    auto* recording = new RecordingStyle();
    recording->m_drawnWith = &drawnWith;
    an::ui::AnProxyStyle style(recording);

    QStyleOptionViewItem option;
    option.state = QStyle::State_Enabled | (selected ? QStyle::State_Selected : QStyle::State_None);
    option.rect = QRect(0, 0, 40, 12);
    option.palette.setColor(QPalette::Base, base);
    option.palette.setColor(QPalette::HighlightedText, sentinel);
    option.palette.setColor(QPalette::Text, sentinel);
    QImage canvas(40, 12, QImage::Format_ARGB32);
    QPainter painter(&canvas);
    style.drawControl(QStyle::CE_ItemViewItem, &option, &painter, nullptr);
    return drawnWith;
}

/// The selection fill the application's style and the styled table draw: the accent, translucent over the base.
[[nodiscard]] QColor selectionFillOver(const QColor& base)
{
    QColor fill = an::ui::tokens().colors.accentPrimary;
    fill.setAlpha(an::ui::isDarkTheme() ? 116 : 84);
    return an::ui::compositeOver(fill, base);
}

} // namespace

// A highlighted table row's text is chosen against the highlight composited over the table's base, readable on a pale
// and on a deep highlight, and clearing the highlight puts back the row's own colours.
TEST(SelectionTextContrastTest, AHighlightedTableRowsTextReadsOverItsHighlight)
{
    TableWidget table(2, 3);
    table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("frame")));
    const QBrush ownForeground = table.item(0, 0)->foreground();
    const QBrush ownBackground = table.item(0, 0)->background();
    const QColor base = table.palette().color(QPalette::Base);

    for (const QColor& highlight : {QColor(255, 240, 180), QColor(20, 30, 80), QColor(220, 60, 60, 120)})
    {
        table.highlightRow(0, highlight);
        for (int column = 0; column < table.columnCount(); ++column)
        {
            const QTableWidgetItem* item = table.item(0, column);
            ASSERT_NE(item, nullptr) << column;
            EXPECT_EQ(item->background().color(), highlight) << column;
            const QColor text = item->foreground().color();
            EXPECT_GE(an::ui::contrastRatio(text, an::ui::compositeOver(highlight, base)), kReadableContrast)
                << highlight.name(QColor::HexArgb).toStdString() << " column " << column;
        }
        table.clearHighlightRow(0);
        EXPECT_EQ(table.item(0, 0)->foreground(), ownForeground);
        EXPECT_EQ(table.item(0, 0)->background(), ownBackground);
    }
}

// The styled table's selected text is chosen against its selection composited over its base, in the dark theme and in
// the light one, and its base and text are the theme's.
TEST(SelectionTextContrastTest, TheStyledTablesSelectionReadsInBothThemes)
{
    for (const an::ui::ThemePreference preference : {an::ui::ThemePreference::Dark, an::ui::ThemePreference::Light})
    {
        const ScopedTheme theme(preference);
        an::ui::AnStyledTable table;
        const QPalette palette = table.palette();
        EXPECT_EQ(palette.color(QPalette::Base), an::ui::tokens().colors.bgInput);
        EXPECT_EQ(palette.color(QPalette::Text), an::ui::tokens().colors.textPrimary);
        const QColor fill = selectionFillOver(palette.color(QPalette::Base));
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive})
        {
            EXPECT_GE(an::ui::contrastRatio(palette.color(group, QPalette::HighlightedText), fill), kReadableContrast)
                << (preference == an::ui::ThemePreference::Dark ? "dark" : "light");
        }
    }
}

// The application's style draws a selected item's text in a colour chosen against the selection composited over the
// view's base - on a dark base and on a light one - and leaves an unselected item's colours as the view gave them.
TEST(SelectionTextContrastTest, TheStylesSelectedItemTextReadsOverTheSelection)
{
    const QColor sentinel(1, 2, 3);
    for (const an::ui::ThemePreference preference : {an::ui::ThemePreference::Dark, an::ui::ThemePreference::Light})
    {
        const ScopedTheme theme(preference);
        for (const QColor& base : {QColor(20, 20, 24), QColor(250, 250, 250)})
        {
            const std::optional<QPalette> selected = itemDrawnWith(base, true, sentinel);
            ASSERT_TRUE(selected.has_value()) << "the style handed the item to its base style";
            const QColor text = selected->color(QPalette::HighlightedText);
            EXPECT_NE(text, sentinel);
            EXPECT_GE(an::ui::contrastRatio(text, selectionFillOver(base)), kReadableContrast)
                << base.name().toStdString();
            EXPECT_EQ(selected->color(QPalette::Text), text);
            EXPECT_EQ(selected->color(QPalette::WindowText), text);

            const std::optional<QPalette> unselected = itemDrawnWith(base, false, sentinel);
            ASSERT_TRUE(unselected.has_value());
            EXPECT_EQ(unselected->color(QPalette::HighlightedText), sentinel);
            EXPECT_EQ(unselected->color(QPalette::Text), sentinel);
        }
    }
}
