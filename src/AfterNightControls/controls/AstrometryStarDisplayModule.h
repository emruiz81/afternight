// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The detected-stars overlay: the stars the view detected on a solved image after a plate solve, each drawn as an
/// ellipse of its fitted widths, a saturated one in the error colour.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/AstrometryStarOverlayUtils.h"
#include "controls/ImageViewCompanions.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <cstddef>
#include <memory>

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AstrometryStarDisplayModule final : public ImageViewWidgetDisplayModule
{
public:
    /// Draws these stars from now on; null draws none.
    void setStars(std::shared_ptr<const ViewOverlayStars> stars);

    /// Draws no stars.
    void clear();

    /// How many stars the overlay draws.
    [[nodiscard]] std::size_t starCount() const;

    void onDraw(QPainter& painter) override;

private:
    std::shared_ptr<const ViewOverlayStars> m_Stars;
};

} // namespace an::ui
