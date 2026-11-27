// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A row that stands its controls side by side where its width holds them and one under another where it does not:
// where it fits it is laid out as a QHBoxLayout with the same controls is, and it turns at the width such a row needs;
// it states its widest control's least width; narrower, its controls stand one under another, none of them squeezed
// under its text; and wider again they stand side by side again.

#include "components/AnSideBySideLayout.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace
{

using an::ui::AnSideBySideLayout;

/// Three buttons in a row of a widget's own: two sharing the row's room, a stated spacing and a stretch, and a third.
template <typename Row> struct Holder
{
    QWidget widget;
    Row* row = nullptr;
    std::array<QPushButton*, 3u> buttons{};

    Holder()
    {
        row = new Row(&widget);
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(6);
        const std::array<const char*, 3u> texts{"Copy to All", "Reset Layer", "Disable"};
        for (std::size_t index = 0; index < buttons.size(); ++index)
        {
            buttons[index] = new QPushButton(QString::fromLatin1(texts[index]), &widget);
        }
        row->addWidget(buttons[0], 1);
        row->addWidget(buttons[1], 1);
        row->addSpacing(10);
        row->addStretch(1);
        row->addWidget(buttons[2]);
        widget.setAttribute(Qt::WA_DontShowOnScreen);
    }

    /// Lays the row out at a width and at the height it needs there: a turn asks for a layout again, at the height of
    /// the other arrangement.
    void layOutAt(const int width)
    {
        widget.show();
        for (int round = 0; round < 3; ++round)
        {
            widget.resize(width, widget.minimumSizeHint().height());
            QCoreApplication::sendPostedEvents();
            row->activate();
        }
    }

    [[nodiscard]] int widestLeast() const
    {
        int widest = 0;
        for (const QPushButton* button : buttons)
        {
            widest = (std::max)(widest, button->minimumSizeHint().width());
        }
        return widest;
    }
};

} // namespace

// Where the width holds the row, its controls are where a QHBoxLayout with the same controls puts them, and the row
// turns at the width such a row needs: its controls' least widths, the room stated between them, the spacing between
// them and its margins.
TEST(AnSideBySideLayout, WhereTheRowFitsItIsLaidOutAsARowIs)
{
    Holder<AnSideBySideLayout> adaptive;
    Holder<QHBoxLayout> plain;
    EXPECT_EQ(adaptive.row->sideBySideWidth(), plain.row->minimumSize().width());
    for (const int extra : {0, 40})
    {
        const int width = plain.row->minimumSize().width() + extra;
        adaptive.layOutAt(width);
        plain.layOutAt(width);
        EXPECT_TRUE(adaptive.row->sideBySide()) << width;
        for (std::size_t index = 0; index < adaptive.buttons.size(); ++index)
        {
            EXPECT_EQ(adaptive.buttons[index]->geometry(), plain.buttons[index]->geometry())
                << adaptive.buttons[index]->text().toStdString() << " at " << width;
        }
    }
}

// It states the least width of its widest control, so a narrower column can give it less than the row needs; there
// its controls stand one under another, each at least as wide as its text needs, and the row is as tall as they are.
TEST(AnSideBySideLayout, NarrowerThanTheRowItsControlsStandOneUnderAnother)
{
    Holder<AnSideBySideLayout> adaptive;
    EXPECT_EQ(adaptive.row->minimumSize().width(), adaptive.widestLeast() + 8);
    const int sideBySideHeight = adaptive.row->minimumSize().height();
    adaptive.layOutAt(adaptive.row->sideBySideWidth() - 1);
    EXPECT_FALSE(adaptive.row->sideBySide());
    int under = -1;
    for (const QPushButton* button : adaptive.buttons)
    {
        EXPECT_GE(button->width(), button->minimumSizeHint().width()) << button->text().toStdString();
        EXPECT_GT(button->y(), under) << button->text().toStdString() << " stands under the one before";
        under = button->geometry().bottom();
    }
    EXPECT_GE(adaptive.widget.height(), under + 1) << "the row is as tall as its controls one under another";
    EXPECT_GT(adaptive.row->minimumSize().height(), sideBySideHeight);
}

// Given the width again, the controls stand side by side again, where a row puts them.
TEST(AnSideBySideLayout, GivenTheWidthAgainItsControlsStandSideBySide)
{
    Holder<AnSideBySideLayout> adaptive;
    Holder<QHBoxLayout> plain;
    const int width = plain.row->minimumSize().width();
    adaptive.layOutAt(width - 30);
    ASSERT_FALSE(adaptive.row->sideBySide());
    adaptive.layOutAt(width);
    plain.layOutAt(width);
    EXPECT_TRUE(adaptive.row->sideBySide());
    for (std::size_t index = 0; index < adaptive.buttons.size(); ++index)
    {
        EXPECT_EQ(adaptive.buttons[index]->geometry(), plain.buttons[index]->geometry())
            << adaptive.buttons[index]->text().toStdString();
    }
}
