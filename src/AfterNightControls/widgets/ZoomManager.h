// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <cmath>

namespace an::ui
{

class ZoomManager
{
public:
    static constexpr double MIN_SCALE = 0.01;
    static constexpr double MAX_SCALE = 64.0;

    [[nodiscard]] static double clampScale(double requestedScale)
    {
        if (!std::isfinite(requestedScale))
        {
            return 1.0;
        }

        return std::clamp(requestedScale, MIN_SCALE, MAX_SCALE);
    }

    [[nodiscard]] static double applyRelativeZoom(double currentScale, double factor)
    {
        if (!std::isfinite(currentScale) || currentScale <= 0.0)
        {
            currentScale = 1.0;
        }

        if (!std::isfinite(factor) || factor <= 0.0)
        {
            return clampScale(currentScale);
        }

        return clampScale(currentScale * factor);
    }

    [[nodiscard]] static double effectiveRelativeZoomFactor(double currentScale, double factor)
    {
        if (!std::isfinite(currentScale) || currentScale <= 0.0)
        {
            currentScale = 1.0;
        }

        return applyRelativeZoom(currentScale, factor) / currentScale;
    }
};

} // namespace an::ui
