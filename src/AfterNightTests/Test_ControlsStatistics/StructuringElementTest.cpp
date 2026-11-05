// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The structuring element a morphology window edits: the widget-owned grid - its cells, filling, inverting and
/// resizing about the centre - and the widget that shows it and turns over the cell a click lands on.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "widgets/StructuredElementWidget.h"
#include "widgets/StructuringElementGrid.h"

#include <QCoreApplication>
#include <QMouseEvent>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace
{

/// The grid as rows of '#' and '.'.
[[nodiscard]] std::string pictureOf(const an::ui::StructuringElementGrid& grid)
{
    std::string picture;
    for (std::size_t row = 0u; row < grid.size(); ++row)
    {
        for (std::size_t column = 0u; column < grid.size(); ++column)
        {
            picture += grid.at(row, column) ? '#' : '.';
        }
        picture += '/';
    }
    return picture;
}

/// Counts what the widget reports.
class GridListener : public StructuredElementWidgetListener
{
public:
    void onGridUpdated() override
    {
        ++updates;
    }

    int updates = 0;
};

void click(QWidget& widget, const QPoint& position)
{
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(position), widget.mapToGlobal(QPointF(position)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

} // namespace

TEST(StructuringElementGridTest, ANewGridHasEveryCellOff)
{
    const an::ui::StructuringElementGrid grid(3u);
    EXPECT_EQ(grid.size(), 3u);
    EXPECT_EQ(grid.onCount(), 0u);
    EXPECT_EQ(grid.cells().size(), 9u);
    EXPECT_EQ(an::ui::StructuringElementGrid().size(), 0u);
}

TEST(StructuringElementGridTest, CellsAreSetToggledFilledAndInverted)
{
    an::ui::StructuringElementGrid grid(3u);
    grid.set(0u, 2u, true);
    grid.toggle(1u, 1u);
    EXPECT_EQ(pictureOf(grid), "..#/.#./.../");
    grid.toggle(1u, 1u);
    EXPECT_FALSE(grid.at(1u, 1u));
    grid.invert();
    EXPECT_EQ(pictureOf(grid), "##./###/###/");
    grid.fill(false);
    EXPECT_EQ(grid.onCount(), 0u);
    grid.fill(true);
    EXPECT_EQ(grid.onCount(), 9u);
}

TEST(StructuringElementGridTest, ACellOutsideTheGridIsIgnored)
{
    an::ui::StructuringElementGrid grid(2u);
    grid.set(2u, 0u, true);
    grid.toggle(0u, 5u);
    EXPECT_EQ(grid.onCount(), 0u);
    EXPECT_FALSE(grid.at(9u, 9u));
}

// Resizing keeps the cells about the centre: a larger grid surrounds them with cells that are off, a smaller one keeps
// the middle - for an odd difference and an even one alike.
TEST(StructuringElementGridTest, ResizingKeepsTheCellsAboutTheCentre)
{
    an::ui::StructuringElementGrid grid(3u);
    grid.set(0u, 0u, true);
    grid.set(1u, 1u, true);
    grid.set(2u, 1u, true);

    an::ui::StructuringElementGrid larger = grid;
    larger.resize(5u);
    EXPECT_EQ(pictureOf(larger), "...../.#.../..#../..#../...../");

    an::ui::StructuringElementGrid uneven = grid;
    uneven.resize(4u);
    EXPECT_EQ(pictureOf(uneven), "#.../.#../.#../..../");
    EXPECT_EQ(uneven.onCount(), grid.onCount());

    larger.resize(3u);
    EXPECT_EQ(larger, grid);

    an::ui::StructuringElementGrid smaller = grid;
    smaller.resize(1u);
    EXPECT_EQ(pictureOf(smaller), "#/");
}

TEST(StructuredElementWidgetTest, AClickTurnsOverTheCellUnderItAndTellsTheListener)
{
    StructuredElementWidget widget(nullptr);
    widget.resize(90, 90);
    GridListener listener;
    widget.setListener(&listener);
    widget.setStructuringElement(an::ui::StructuringElementGrid(3u));

    click(widget, QPoint(75, 15));
    EXPECT_EQ(pictureOf(widget.structuringElement()), "..#/.../.../");
    click(widget, QPoint(45, 45));
    click(widget, QPoint(75, 15));
    EXPECT_EQ(pictureOf(widget.structuringElement()), ".../.#./.../");
    EXPECT_EQ(listener.updates, 3);
    EXPECT_FALSE(widget.grab().toImage().isNull());
    widget.setListener(nullptr);
}

// A widget nobody gave a grid ignores a click rather than reading a cell that does not exist.
TEST(StructuredElementWidgetTest, AWidgetWithoutAGridIgnoresAClick)
{
    StructuredElementWidget widget(nullptr);
    widget.resize(90, 90);
    GridListener listener;
    widget.setListener(&listener);

    click(widget, QPoint(10, 10));
    EXPECT_EQ(listener.updates, 0);
    EXPECT_EQ(widget.structuringElement().size(), 0u);
    EXPECT_FALSE(widget.grab().toImage().isNull());
    widget.setListener(nullptr);
}
