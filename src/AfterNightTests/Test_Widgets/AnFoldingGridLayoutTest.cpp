// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A grid that stands its trailing columns under its leading ones where its width does not hold them: where it fits it
// is the grid it was built as, its cells where a QGridLayout built the same way puts them; it states the folded grid's
// least width; narrower, each row's cells from the fold column on stand in a row of their own under the row's others,
// from the first column, none squeezed under its text; and wider again it stands as built again.

#include "components/AnFoldingGridLayout.h"

#include <QCoreApplication>
#include <QGridLayout>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <type_traits>

namespace
{

using an::ui::AnFoldingGridLayout;

/// Two rows of a grid of a widget's own, folding at its third column: an option over two columns beside a label and its
/// field, then two options over two columns each.
template <typename Grid> struct Holder
{
    QWidget widget;
    Grid* grid = nullptr;
    std::array<QPushButton*, 5u> cells{};

    Holder()
    {
        if constexpr (std::is_same_v<Grid, AnFoldingGridLayout>)
        {
            grid = new AnFoldingGridLayout(2, &widget);
        }
        else
        {
            grid = new Grid(&widget);
        }
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(3);
        const std::array<const char*, 5u> texts{"Preview the stretch", "Target", "0.25",
                                                "Create a layer for each colour", "Stretch the combined image"};
        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            cells[index] = new QPushButton(QString::fromLatin1(texts[index]), &widget);
        }
        grid->addWidget(cells[0], 0, 0, 1, 2);
        grid->addWidget(cells[1], 0, 2);
        grid->addWidget(cells[2], 0, 3);
        grid->addWidget(cells[3], 1, 0, 1, 2);
        grid->addWidget(cells[4], 1, 2, 1, 2);
        widget.setAttribute(Qt::WA_DontShowOnScreen);
    }

    /// Lays the grid out at a width and at the height it needs there: a fold asks for a layout again, at the height of
    /// the other arrangement.
    void layOutAt(const int width)
    {
        widget.show();
        for (int round = 0; round < 3; ++round)
        {
            widget.resize(width, widget.minimumSizeHint().height());
            QCoreApplication::sendPostedEvents();
            grid->activate();
        }
    }
};

} // namespace

// Where the width holds the grid, its cells are where a QGridLayout built the same way puts them.
TEST(AnFoldingGridLayout, WhereTheGridFitsItIsTheGridItWasBuiltAs)
{
    Holder<AnFoldingGridLayout> folding;
    Holder<QGridLayout> plain;
    for (const int extra : {0, 40})
    {
        const int width = plain.grid->minimumSize().width() + extra;
        folding.layOutAt(width);
        plain.layOutAt(width);
        EXPECT_FALSE(folding.grid->folded()) << width;
        for (std::size_t index = 0; index < folding.cells.size(); ++index)
        {
            EXPECT_EQ(folding.cells[index]->geometry(), plain.cells[index]->geometry())
                << folding.cells[index]->text().toStdString() << " at " << width;
        }
    }
}

// It states less than the grid as built needs, so a narrower column can give it less; there each row's cells from the
// fold column on stand under the row's others, from the first column, each as wide as its text needs.
TEST(AnFoldingGridLayout, NarrowerThanTheGridItsTrailingCellsStandUnderTheLeadingOnes)
{
    Holder<AnFoldingGridLayout> folding;
    Holder<QGridLayout> plain;
    const int built = plain.grid->minimumSize().width();
    EXPECT_LT(folding.grid->minimumSize().width(), built);
    folding.layOutAt(built - 1);
    ASSERT_TRUE(folding.grid->folded());
    const auto& c = folding.cells;
    for (const QPushButton* cell : c)
    {
        EXPECT_GE(cell->width(), cell->minimumSizeHint().width()) << cell->text().toStdString();
    }
    EXPECT_GT(c[1]->y(), c[0]->geometry().bottom()) << "the label stands under its row's option";
    EXPECT_EQ(c[1]->x(), c[0]->x()) << "from the first column";
    EXPECT_EQ(c[2]->y(), c[1]->y()) << "the field beside its label";
    EXPECT_GT(c[2]->x(), c[1]->geometry().right());
    EXPECT_GT(c[3]->y(), c[2]->geometry().bottom()) << "the next row under the fold of the one before";
    EXPECT_GT(c[4]->y(), c[3]->geometry().bottom()) << "its trailing option under it";
    EXPECT_EQ(c[4]->x(), c[3]->x());
    EXPECT_LE(c[4]->geometry().right(), folding.widget.width()) << "the folded grid lies within its width";
}

// Given the width again, it stands as built again.
TEST(AnFoldingGridLayout, GivenTheWidthAgainItStandsAsBuilt)
{
    Holder<AnFoldingGridLayout> folding;
    Holder<QGridLayout> plain;
    const int width = plain.grid->minimumSize().width();
    folding.layOutAt(width - 30);
    ASSERT_TRUE(folding.grid->folded());
    folding.layOutAt(width);
    plain.layOutAt(width);
    EXPECT_FALSE(folding.grid->folded());
    for (std::size_t index = 0; index < folding.cells.size(); ++index)
    {
        EXPECT_EQ(folding.cells[index]->geometry(), plain.cells[index]->geometry())
            << folding.cells[index]->text().toStdString();
    }
}
