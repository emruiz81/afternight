// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The square grid of cells a structuring-element widget edits: which cells of a morphological operation's footprint
/// are on.
///
/// The engine applies a footprint and keeps no editable one, so the grid is the widget's: a value its owner copies in,
/// reads back and hands to the operation. A cell is on or off; resizing keeps the cells about the centre.
#pragma once

#include "AfterNightControlsExport.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace an::ui
{

/// A square grid of on and off cells.
class AFTERNIGHTCONTROLS_API StructuringElementGrid
{
public:
    /// An empty grid of no cells.
    StructuringElementGrid() = default;

    /// A grid of a side, every cell off.
    ///
    /// @param size how many cells along each side.
    explicit StructuringElementGrid(std::size_t size);

    /// How many cells along each side.
    [[nodiscard]] std::size_t size() const noexcept;

    /// Whether a cell is on.
    ///
    /// @param row the cell's row.
    /// @param column the cell's column.
    /// @return true when it is on; false outside the grid.
    [[nodiscard]] bool at(std::size_t row, std::size_t column) const noexcept;

    /// Turns a cell on or off; a cell outside the grid is ignored.
    ///
    /// @param row the cell's row.
    /// @param column the cell's column.
    /// @param on whether it is on.
    void set(std::size_t row, std::size_t column, bool on) noexcept;

    /// Turns a cell over; a cell outside the grid is ignored.
    ///
    /// @param row the cell's row.
    /// @param column the cell's column.
    void toggle(std::size_t row, std::size_t column) noexcept;

    /// Turns every cell on or off.
    ///
    /// @param on whether they are on.
    void fill(bool on) noexcept;

    /// Turns every cell over.
    void invert() noexcept;

    /// Changes the side, keeping the cells about the centre: a smaller grid keeps the middle of this one and a larger
    /// one surrounds it with cells that are off.
    ///
    /// @param size the new side.
    void resize(std::size_t size);

    /// How many cells are on.
    [[nodiscard]] std::size_t onCount() const noexcept;

    /// The cells, row by row, one byte each, one for on.
    [[nodiscard]] const std::vector<std::uint8_t>& cells() const noexcept;

    /// Grids compare by their side and their cells.
    friend AFTERNIGHTCONTROLS_API bool operator==(const StructuringElementGrid& left,
                                                  const StructuringElementGrid& right) noexcept;

private:
    std::size_t m_size = 0u;
    std::vector<std::uint8_t> m_cells;
};

} // namespace an::ui
