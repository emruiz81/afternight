// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A grid that stands its trailing columns under its leading ones where its width does not hold them side by side.

#include "AfterNightControlsExport.h"

#include <QGridLayout>
#include <QRect>
#include <QSize>

#include <vector>

class QLayoutItem;
class QWidget;

namespace an::ui
{

/// A `QGridLayout` that is the grid it was built as wherever its width holds it, and folds where it does not: each
/// row's cells from `foldColumn()` on stand in a row of their own under the row's other cells, starting at the first
/// column. It states the least width of the folded grid, so a column narrower than the grid gives it the width it has,
/// and it is laid out again there, folded and taller.
///
/// What it is for: a grid of a few controls - two options to a row, or a label beside fields under their headings -
/// that the interface's font can draw wider than the column that holds it. Where it fits it keeps the look it was
/// given, its columns aligned as the builder's grid aligns them; where it does not it gives way whole rather than
/// squeezing its controls under their text. Its cells span one row each; a cell that spans the fold column stands with
/// the leading ones.
class AFTERNIGHTCONTROLS_API AnFoldingGridLayout final : public QGridLayout
{
public:
    /// @param foldColumn the first of the columns that stand under the others where the grid does not fit.
    /// @param parent the widget the grid lays out, or none.
    explicit AnFoldingGridLayout(int foldColumn, QWidget* parent = nullptr);
    ~AnFoldingGridLayout() override;

    AnFoldingGridLayout(const AnFoldingGridLayout&) = delete;
    AnFoldingGridLayout& operator=(const AnFoldingGridLayout&) = delete;
    AnFoldingGridLayout(AnFoldingGridLayout&&) = delete;
    AnFoldingGridLayout& operator=(AnFoldingGridLayout&&) = delete;

    /// The first of the columns that stand under the others where the grid does not fit.
    [[nodiscard]] int foldColumn() const;

    /// Whether the grid stands folded now: the last width it was given did not hold it as built.
    [[nodiscard]] bool folded() const;

    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;

private:
    /// A cell as the builder placed it.
    struct Cell
    {
        QLayoutItem* item = nullptr;
        int row = 0;
        int column = 0;
        int columnSpan = 1;
    };

    /// The cells where they stand now; as built while the grid is not folded.
    [[nodiscard]] std::vector<Cell> cellsNow() const;

    /// The least width of the folded grid: the wider of each row's two parts, and the margins.
    [[nodiscard]] int foldedWidth() const;

    /// Places the cells as built, or folded.
    void place(bool fold);

    int m_foldColumn;
    bool m_folded = false;
    std::vector<Cell> m_built;
    int m_builtWidth = 0;
};

/// Makes a grid a builder made fold its columns from `foldColumn` on under the others where its width does not hold
/// them side by side: its items move, each to the cell it had, into an `AnFoldingGridLayout` that becomes the grid's
/// only item, and the grid's margins, spacing and columns' stretch go with them. The grid keeps its place and every
/// pointer to it holds.
///
/// @param grid a grid of widgets and layouts, each spanning one row.
/// @param foldColumn the first of the columns that stand under the others.
/// @return the grid its items stand in now.
AFTERNIGHTCONTROLS_API AnFoldingGridLayout* foldWhereItDoesNotFit(QGridLayout& grid, int foldColumn);

} // namespace an::ui
