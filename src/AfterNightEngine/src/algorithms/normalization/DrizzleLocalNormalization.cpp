// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the grid of a local normalization measured on a drizzle canvas's analysis plane.
//
// The shape is the canvas's analysis extent with the caller's grid size and channels, checked by the
// same rule every other grid is; nothing here measures or allocates.

#include <AfterNightEngine/Algorithms/Normalization/DrizzleLocalNormalization.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Core/Status.h>

namespace an
{

Result<LocalNormalizationGridShape> drizzleLocalNormalizationShape(const DrizzleCanvas& canvas, std::uint32_t gridSize,
                                                                   std::uint32_t channelCount)
{
    const LocalNormalizationGridShape shape{.gridSize = gridSize,
                                            .width = canvas.analysisWidth,
                                            .height = canvas.analysisHeight,
                                            .channelCount = channelCount};
    const Status checked = checkLocalNormalizationGridShape(shape);
    if (checked.failed())
    {
        return checked;
    }
    return shape;
}

} // namespace an
