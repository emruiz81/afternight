// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/AstrometryStarDisplayModule.h"

#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"
#include "widgets/ImageViewWidget.h"

#include <QPainter>

#include <utility>

namespace an::ui
{

void AstrometryStarDisplayModule::setStars(std::shared_ptr<const ViewOverlayStars> stars)
{
    m_Stars = std::move(stars);
}

void AstrometryStarDisplayModule::clear()
{
    m_Stars.reset();
}

std::size_t AstrometryStarDisplayModule::starCount() const
{
    return m_Stars ? m_Stars->detection.detection.stars.size() : 0u;
}

void AstrometryStarDisplayModule::onDraw(QPainter& painter)
{
    if (!isDisplayModuleEnabled() || !m_Stars || !m_ViewWidget)
    {
        return;
    }

    const double current_scale = m_ViewWidget->getCurrentScaleFactor();
    if (!std::isfinite(current_scale) || current_scale <= 0.0)
    {
        return;
    }

    const auto& token_set = an::ui::tokens();
    QColor normal_color = token_set.colors.accentPrimary;
    normal_color.setAlpha(204);

    QColor saturated_color = token_set.colors.statusError;
    saturated_color.setAlpha(224);

    QPen pen = painter.pen();
    pen.setWidth(1);
    pen.setCosmetic(true);
    painter.setBrush(Qt::NoBrush);

    for (const an::DetectedStar& star : m_Stars->detection.detection.stars)
    {
        const auto ellipse = an::ui::detail::projectAstrometryStarOverlayEllipse(star, current_scale);
        if (!ellipse.isValid())
        {
            continue;
        }

        pen.setColor(star.psf.saturated ? saturated_color : normal_color);
        painter.setPen(pen);

        an::ui::PainterGuard guard(&painter);
        painter.translate(ellipse.center);
        painter.rotate(ellipse.rotationDegrees);
        painter.scale(ellipse.axisRatio, 1.0);
        painter.drawEllipse(QPointF(0.0, 0.0), ellipse.semiMajorRadius, ellipse.semiMajorRadius);
    }
}

} // namespace an::ui
