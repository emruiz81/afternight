// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shapes an owner - an extension's session - draws over a view, as a display module of the view fed with data
/// (`controls/ViewShapes.h`): each polygon or line where its points lie in the image, following the view's zoom and
/// scroll, a filled polygon filled, a label beside its shape in the size the view's other overlays label at. The line
/// is the view's other overlays' line. The colours are the owner's, drawn as Night shows a colour, so under Night the
/// shapes hold no green and no blue. The overlay takes no click: the view's tools and its menu work through it.
#pragma once

#include "AfterNightControlsExport.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <QStaticText>

#include <cstddef>
#include <memory>
#include <vector>

namespace an::ui
{

struct ViewShapes;

class AFTERNIGHTCONTROLS_API ShapeOverlayDisplayModule final : public ImageViewWidgetDisplayModule
{
public:
    /// The width of a shape's line on the screen, in device-independent pixels: the detected stars' line.
    static constexpr double kLineWidth = 1.0;

    /// Draws these shapes from now on; null draws none.
    void setShapes(std::shared_ptr<const ViewShapes> shapes);

    /// How many shapes the overlay draws.
    [[nodiscard]] std::size_t shapeCount() const;

    /// How many shapes the last paint drew: those whose part of the image, their label beside it, the paint reached.
    [[nodiscard]] std::size_t shapesDrawnByLastPaint() const noexcept;

    void onDraw(QPainter& painter) override;

private:
    /// Each shape's label laid out at the size labels are drawn at, and its width: made once for the shapes and the
    /// size, not at every paint, since a pan paints again what it uncovers.
    struct LaidOutLabels
    {
        int pixelSize = -1;
        std::vector<QStaticText> texts;
        std::vector<double> widths;
    };

    std::shared_ptr<const ViewShapes> m_Shapes;
    LaidOutLabels m_Labels;
    std::size_t m_DrawnByLastPaint = 0u;
};

} // namespace an::ui
