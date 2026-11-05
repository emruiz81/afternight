// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "widgets/StructuringElementGrid.h"

#include <algorithm>
#include <utility>

namespace an::ui
{

StructuringElementGrid::StructuringElementGrid(const std::size_t size) : m_size(size), m_cells(size * size, 0u) {}

std::size_t StructuringElementGrid::size() const noexcept
{
    return m_size;
}

bool StructuringElementGrid::at(const std::size_t row, const std::size_t column) const noexcept
{
    return row < m_size && column < m_size && m_cells[row * m_size + column] != 0u;
}

void StructuringElementGrid::set(const std::size_t row, const std::size_t column, const bool on) noexcept
{
    if (row < m_size && column < m_size)
    {
        m_cells[row * m_size + column] = on ? 1u : 0u;
    }
}

void StructuringElementGrid::toggle(const std::size_t row, const std::size_t column) noexcept
{
    set(row, column, !at(row, column));
}

void StructuringElementGrid::fill(const bool on) noexcept
{
    std::fill(m_cells.begin(), m_cells.end(), static_cast<std::uint8_t>(on ? 1u : 0u));
}

void StructuringElementGrid::invert() noexcept
{
    for (std::uint8_t& cell : m_cells)
    {
        cell = cell != 0u ? 0u : 1u;
    }
}

void StructuringElementGrid::resize(const std::size_t size)
{
    if (size == m_size)
    {
        return;
    }
    StructuringElementGrid resized(size);
    if (size < m_size)
    {
        // The middle of this grid.
        const std::size_t offset = (m_size - size) / 2u;
        for (std::size_t row = 0u; row < size; ++row)
        {
            for (std::size_t column = 0u; column < size; ++column)
            {
                resized.set(row, column, at(row + offset, column + offset));
            }
        }
    }
    else
    {
        // This grid in the middle of a larger one.
        const std::size_t offset = (size - m_size) / 2u;
        for (std::size_t row = 0u; row < m_size; ++row)
        {
            for (std::size_t column = 0u; column < m_size; ++column)
            {
                resized.set(row + offset, column + offset, at(row, column));
            }
        }
    }
    *this = std::move(resized);
}

std::size_t StructuringElementGrid::onCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(m_cells.begin(), m_cells.end(), [](std::uint8_t cell) { return cell != 0u; }));
}

const std::vector<std::uint8_t>& StructuringElementGrid::cells() const noexcept
{
    return m_cells;
}

bool operator==(const StructuringElementGrid& left, const StructuringElementGrid& right) noexcept
{
    return left.m_size == right.m_size && left.m_cells == right.m_cells;
}

} // namespace an::ui
