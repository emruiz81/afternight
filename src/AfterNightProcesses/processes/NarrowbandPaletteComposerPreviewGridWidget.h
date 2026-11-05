// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QSize>
#include <QWidget>

#include <functional>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace an::ui::processes
{

struct NarrowbandPaletteComposerPreviewFrame
{
    an::NarrowbandPalette palette = an::NarrowbandPalette::Sho;
    QString name;
    QString description;
    QImage image;
};

class AFTERNIGHTPROCESSES_API NarrowbandPaletteComposerPreviewGridWidget final : public QWidget
{
public:
    explicit NarrowbandPaletteComposerPreviewGridWidget(QWidget* parent = nullptr);

    void setPaletteDescriptors(const std::vector<an::NarrowbandPaletteDescriptor>& descriptors);
    void setPreviewImages(std::vector<NarrowbandPaletteComposerPreviewFrame> frames);
    void clearPreviewImages();

    [[nodiscard]] bool hasPreviewImages() const;
    [[nodiscard]] QString placeholderText() const;
    [[nodiscard]] an::NarrowbandPalette selectedPalette() const;
    void setSelectedPalette(an::NarrowbandPalette palette);
    void setSelectionChangedHandler(std::function<void(an::NarrowbandPalette)> handler);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct TileGeometry
    {
        int index = -1;
        QRect cardRect;
        QRect labelRect;
        QRect imageRect;
    };

    [[nodiscard]] std::vector<TileGeometry> tileGeometries(int width) const;
    [[nodiscard]] int tileIndexAt(const QPoint& position) const;
    [[nodiscard]] QRect imageDrawRect(const NarrowbandPaletteComposerPreviewFrame& frame, const QRect& imageRect) const;
    [[nodiscard]] QRect sourcePixelRectFor(const QImage& image) const;
    void updateMinimumGridHeight();
    void resetView();
    void adjustZoom(double factor);
    void panBy(const QPoint& delta, int tileIndex);
    void clampViewCenter();
    void updateTileToolTip(const QPoint& position);
    [[nodiscard]] bool paletteExists(an::NarrowbandPalette palette) const;
    void notifySelectionChanged();

    std::vector<NarrowbandPaletteComposerPreviewFrame> m_frames;
    std::vector<an::NarrowbandPaletteDescriptor> m_descriptors;
    std::function<void(an::NarrowbandPalette)> m_selectionChangedHandler;
    an::NarrowbandPalette m_selectedPalette = an::NarrowbandPalette::Sho;
    QPointF m_viewCenter = QPointF(0.5, 0.5);
    QPoint m_lastPanPosition;
    int m_activePanTile = -1;
    double m_zoom = 1.0;
    bool m_panning = false;
};

class AFTERNIGHTPROCESSES_API NarrowbandPaletteComposerSinglePreviewWidget final : public QWidget
{
public:
    explicit NarrowbandPaletteComposerSinglePreviewWidget(QWidget* parent = nullptr);

    void setPaletteDescriptors(const std::vector<an::NarrowbandPaletteDescriptor>& descriptors);
    void setPreviewImages(std::vector<NarrowbandPaletteComposerPreviewFrame> frames);
    void clearPreviewImages();

    [[nodiscard]] bool hasPreviewImages() const;
    [[nodiscard]] QString placeholderText() const;
    [[nodiscard]] an::NarrowbandPalette selectedPalette() const;
    void setSelectedPalette(an::NarrowbandPalette palette);
    void setSelectionChangedHandler(std::function<void(an::NarrowbandPalette)> handler);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct GalleryGeometry
    {
        int index = -1;
        QRect cardRect;
        QRect imageRect;
        QRect labelRect;
    };

    [[nodiscard]] const NarrowbandPaletteComposerPreviewFrame* selectedFrame() const;
    [[nodiscard]] QRect previewCardRect() const;
    [[nodiscard]] QRect previewImageRect() const;
    [[nodiscard]] QRect galleryRect() const;
    [[nodiscard]] QRect imageDrawRect(const QImage& image, const QRect& imageRect) const;
    [[nodiscard]] QRect sourcePixelRectFor(const QImage& image) const;
    [[nodiscard]] std::vector<GalleryGeometry> galleryGeometries() const;
    [[nodiscard]] int galleryIndexAt(const QPoint& position) const;
    void resetView();
    void adjustZoom(double factor);
    void panBy(const QPoint& delta);
    void clampViewCenter();
    void updateGalleryToolTip(const QPoint& position);
    [[nodiscard]] bool paletteExists(an::NarrowbandPalette palette) const;
    void notifySelectionChanged();

    std::vector<NarrowbandPaletteComposerPreviewFrame> m_frames;
    std::vector<an::NarrowbandPaletteDescriptor> m_descriptors;
    std::function<void(an::NarrowbandPalette)> m_selectionChangedHandler;
    an::NarrowbandPalette m_selectedPalette = an::NarrowbandPalette::Sho;
    QPointF m_viewCenter = QPointF(0.5, 0.5);
    QPoint m_lastPanPosition;
    double m_zoom = 1.0;
    bool m_panning = false;
};

} // namespace an::ui::processes
