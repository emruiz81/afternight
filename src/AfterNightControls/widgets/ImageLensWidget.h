// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QPixmap>
#include <QtWidgets/QWidget>

#include <memory>
#include <vector>

class ImageViewWidget;

namespace an::ui
{
class ILensHistogram;
struct LensHistogram;
} // namespace an::ui

/// The quick-zoom lens: a magnified region of what a view shows, its histogram, and the values under the cursor.
///
/// What it shows is computed by its owner from the view's presentation; the lens paints it. Its histogram is the one
/// the histogram widgets make for it, reached through the view's companion seam.
class AFTERNIGHTCONTROLS_API ImageLensWidget : public QWidget
{
public:
    ImageLensWidget(QWidget* parent);

    ~ImageLensWidget();

    void attachToOwner(QWidget* owner);
    void detachFromOwner();

    [[nodiscard]] QColor getBorderColor() const;

    /// Shows a region's counts in the lens's histogram.
    void setHistogram(const an::ui::LensHistogram& counts);

    QPixmap m_CurrentPixmap;
    std::vector<QPixmap> m_CurrentMaskPixmaps;
    double m_CurrentX = 0.0;
    double m_CurrentY = 0.0;

    bool isRGB = true;
    double m_CurrentChannel0 = 0.0;
    double m_CurrentChannel1 = 0.0;
    double m_CurrentChannel2 = 0.0;

    bool isMasked = false;
    std::vector<double> m_CurrentMaskValues;

private:
    void paintEvent(QPaintEvent* evt) override;
    void changeEvent(QEvent* ev) override;

    void drawBackground(QPainter& painter);
    void drawText(QPainter& painter);
    void drawPixmap(QPainter& painter);

    std::unique_ptr<an::ui::ILensHistogram> m_Histogram;
};
