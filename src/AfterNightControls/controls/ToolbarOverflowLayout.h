// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <vector>

namespace an::ui::controls
{

/// Returns the first item index that must move to overflow.
/// Returns -1 when everything fits.
[[nodiscard]] inline int firstOverflowIndex(const std::vector<int>& itemWidths, const int availableWidth,
                                            const int overflowButtonWidth)
{
    if (itemWidths.empty())
    {
        return -1;
    }

    const int clampedAvailableWidth = (std::max)(0, availableWidth);
    const int clampedOverflowButtonWidth = (std::max)(0, overflowButtonWidth);

    int totalWidth = 0;
    for (const int rawWidth : itemWidths)
    {
        totalWidth += (std::max)(0, rawWidth);
    }

    if (totalWidth <= clampedAvailableWidth)
    {
        return -1;
    }

    const int maxMainWidth = (std::max)(0, clampedAvailableWidth - clampedOverflowButtonWidth);
    int usedWidth = 0;
    for (size_t i = 0; i < itemWidths.size(); ++i)
    {
        const int width = (std::max)(0, itemWidths[i]);
        if (usedWidth + width > maxMainWidth)
        {
            return static_cast<int>(i);
        }
        usedWidth += width;
    }

    return static_cast<int>(itemWidths.size() - 1);
}

} // namespace an::ui::controls
