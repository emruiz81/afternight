// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "NarrowbandPaletteComposerPreviewGridWidget.h"

#include "theme/AnDesignTokens.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{

constexpr double kMinimumZoom = 1.0;
constexpr double kMaximumZoom = 32.0;
constexpr double kWheelZoomStep = 1.15;
constexpr int kMinimumTileWidth = 260;
constexpr int kMinimumTileHeight = 192;
constexpr int kMaximumColumns = 4;
constexpr int kSinglePreviewMinimumWidth = 480;
constexpr int kSinglePreviewMinimumHeight = 420;
constexpr int kGalleryHeight = 132;
constexpr int kGalleryMaximumItemWidth = 144;

[[nodiscard]] double clampUnit(const double value)
{
    if (!std::isfinite(value))
    {
        return 0.5;
    }
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] QRect centeredAspectRect(const QRect& bounds, const QSize& imageSize)
{
    if (bounds.isEmpty() || imageSize.isEmpty())
    {
        return {};
    }

    const double sx = static_cast<double>(bounds.width()) / static_cast<double>(imageSize.width());
    const double sy = static_cast<double>(bounds.height()) / static_cast<double>(imageSize.height());
    const double scale = (std::min)(sx, sy);
    const int width = (std::max)(1, static_cast<int>(std::floor(imageSize.width() * scale)));
    const int height = (std::max)(1, static_cast<int>(std::floor(imageSize.height() * scale)));
    return QRect(bounds.left() + (bounds.width() - width) / 2, bounds.top() + (bounds.height() - height) / 2, width,
                 height);
}

[[nodiscard]] bool shouldSmoothImageTransform(const QRect& destinationRect, const QRect& sourceRect)
{
    if (destinationRect.isEmpty() || sourceRect.isEmpty())
    {
        return false;
    }

    const double scaleX = static_cast<double>(destinationRect.width()) / static_cast<double>(sourceRect.width());
    const double scaleY = static_cast<double>(destinationRect.height()) / static_cast<double>(sourceRect.height());
    return scaleX < 1.0 || scaleY < 1.0;
}

} // namespace

namespace an::ui::processes
{

NarrowbandPaletteComposerPreviewGridWidget::NarrowbandPaletteComposerPreviewGridWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("paletteGrid"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(minimumSizeHint());
}

void NarrowbandPaletteComposerPreviewGridWidget::setPaletteDescriptors(
    const std::vector<an::NarrowbandPaletteDescriptor>& descriptors)
{
    m_descriptors = descriptors;
    if (!paletteExists(m_selectedPalette) && !m_descriptors.empty())
    {
        m_selectedPalette = m_descriptors.front().palette;
    }
    update();
}

void NarrowbandPaletteComposerPreviewGridWidget::setPreviewImages(
    std::vector<NarrowbandPaletteComposerPreviewFrame> frames)
{
    m_frames = std::move(frames);
    if (!paletteExists(m_selectedPalette) && !m_frames.empty())
    {
        m_selectedPalette = m_frames.front().palette;
    }
    resetView();
    updateMinimumGridHeight();
    update();
}

void NarrowbandPaletteComposerPreviewGridWidget::clearPreviewImages()
{
    m_frames.clear();
    resetView();
    updateMinimumGridHeight();
    update();
}

bool NarrowbandPaletteComposerPreviewGridWidget::hasPreviewImages() const
{
    return !m_frames.empty();
}

QString NarrowbandPaletteComposerPreviewGridWidget::placeholderText() const
{
    return QStringLiteral("Select sources to update the palette grid");
}

an::NarrowbandPalette NarrowbandPaletteComposerPreviewGridWidget::selectedPalette() const
{
    return m_selectedPalette;
}

void NarrowbandPaletteComposerPreviewGridWidget::setSelectedPalette(const an::NarrowbandPalette palette)
{
    if (m_selectedPalette == palette || !paletteExists(palette))
    {
        return;
    }

    m_selectedPalette = palette;
    notifySelectionChanged();
    update();
}

void NarrowbandPaletteComposerPreviewGridWidget::setSelectionChangedHandler(
    std::function<void(an::NarrowbandPalette)> handler)
{
    m_selectionChangedHandler = std::move(handler);
}

QSize NarrowbandPaletteComposerPreviewGridWidget::sizeHint() const
{
    return QSize(720, heightForWidth(720));
}

QSize NarrowbandPaletteComposerPreviewGridWidget::minimumSizeHint() const
{
    return QSize(kMinimumTileWidth, kMinimumTileHeight * 2);
}

bool NarrowbandPaletteComposerPreviewGridWidget::hasHeightForWidth() const
{
    return true;
}

int NarrowbandPaletteComposerPreviewGridWidget::heightForWidth(const int width) const
{
    const auto tiles = tileGeometries((std::max)(kMinimumTileWidth, width));
    if (tiles.empty())
    {
        return minimumSizeHint().height();
    }

    const auto& t = an::ui::tokens();
    return tiles.back().cardRect.bottom() + t.spacing.md;
}

void NarrowbandPaletteComposerPreviewGridWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& t = an::ui::tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), t.colors.bgSurface);

    if (m_frames.empty())
    {
        const QRect bannerRect = rect().adjusted(t.spacing.xl, t.spacing.xl, -t.spacing.xl, -t.spacing.xl);
        painter.setPen(QPen(t.colors.borderSubtle, 1));
        painter.setBrush(t.colors.bgCard);
        painter.drawRoundedRect(bannerRect, t.radius.md, t.radius.md);
        painter.setFont(t.typography.heading3);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(bannerRect.adjusted(t.spacing.lg, t.spacing.lg, -t.spacing.lg, -t.spacing.lg),
                         Qt::AlignCenter | Qt::TextWordWrap, placeholderText());
        return;
    }

    painter.setFont(t.typography.body);
    for (const auto& tile : tileGeometries(width()))
    {
        if (tile.index < 0 || tile.index >= static_cast<int>(m_frames.size()))
        {
            continue;
        }

        const auto& frame = m_frames[static_cast<size_t>(tile.index)];
        const bool selected = frame.palette == m_selectedPalette;
        painter.setPen(QPen(selected ? t.colors.accentPrimary : t.colors.borderSubtle, selected ? 2 : 1));
        painter.setBrush(selected ? t.colors.bgHover : t.colors.bgCard);
        painter.drawRoundedRect(tile.cardRect, t.radius.md, t.radius.md);

        painter.setFont(t.typography.heading3);
        painter.setPen(selected ? t.colors.textAccent : t.colors.textPrimary);
        painter.drawText(tile.labelRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, frame.name);

        painter.setPen(QPen(t.colors.borderSubtle, 1));
        painter.setBrush(t.colors.bgInput);
        painter.drawRoundedRect(tile.imageRect, t.radius.sm, t.radius.sm);
        if (!frame.image.isNull())
        {
            const QRect drawRect = imageDrawRect(frame, tile.imageRect);
            if (!drawRect.isEmpty())
            {
                const QRect sourceRect = sourcePixelRectFor(frame.image);
                painter.save();
                painter.setClipRect(tile.imageRect);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                      shouldSmoothImageTransform(drawRect, sourceRect));
                painter.drawImage(drawRect, frame.image, sourceRect);
                painter.restore();
            }
        }
    }
}

void NarrowbandPaletteComposerPreviewGridWidget::mousePressEvent(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton || m_frames.empty())
    {
        QWidget::mousePressEvent(event);
        return;
    }

    const int index = tileIndexAt(event->pos());
    if (index < 0)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    setSelectedPalette(m_frames[static_cast<size_t>(index)].palette);
    m_activePanTile = index;
    m_lastPanPosition = event->pos();
    m_panning = true;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void NarrowbandPaletteComposerPreviewGridWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!event)
    {
        return;
    }

    updateTileToolTip(event->pos());
    if (!m_panning)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }

    panBy(event->pos() - m_lastPanPosition, m_activePanTile);
    m_lastPanPosition = event->pos();
    event->accept();
}

void NarrowbandPaletteComposerPreviewGridWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton && m_panning)
    {
        m_panning = false;
        m_activePanTile = -1;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void NarrowbandPaletteComposerPreviewGridWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton)
    {
        resetView();
        update();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void NarrowbandPaletteComposerPreviewGridWidget::wheelEvent(QWheelEvent* event)
{
    if (!event || m_frames.empty())
    {
        QWidget::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        QWidget::wheelEvent(event);
        return;
    }

    const double steps = static_cast<double>(delta) / 120.0;
    adjustZoom(std::pow(kWheelZoomStep, steps));
    event->accept();
}

void NarrowbandPaletteComposerPreviewGridWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateMinimumGridHeight();
}

std::vector<NarrowbandPaletteComposerPreviewGridWidget::TileGeometry>
NarrowbandPaletteComposerPreviewGridWidget::tileGeometries(const int width) const
{
    const auto& t = an::ui::tokens();
    const int frameCount = static_cast<int>(m_frames.size());
    if (frameCount <= 0)
    {
        return {};
    }

    const int availableWidth = (std::max)(kMinimumTileWidth, width - (t.spacing.md * 2));
    const int columnsByWidth = (std::max)(1, availableWidth / (kMinimumTileWidth + t.spacing.md));
    const int columns = (std::min)(kMaximumColumns, (std::min)(frameCount, columnsByWidth));
    const int cellWidth = (availableWidth - (columns - 1) * t.spacing.md) / columns;
    const int imageHeight = (std::max)(kMinimumTileHeight, cellWidth * 2 / 3);
    const int labelHeight = QFontMetrics(t.typography.heading3).height() + t.spacing.sm;
    const int cellHeight = imageHeight + labelHeight + t.spacing.lg * 2;

    std::vector<TileGeometry> tiles;
    tiles.reserve(static_cast<size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i)
    {
        const int row = i / columns;
        const int column = i % columns;
        const int left = t.spacing.md + column * (cellWidth + t.spacing.md);
        const int top = t.spacing.md + row * (cellHeight + t.spacing.md);
        const QRect cardRect(left, top, cellWidth, cellHeight);
        const QRect labelRect =
            cardRect.adjusted(t.spacing.md, t.spacing.sm, -t.spacing.md, -(cellHeight - labelHeight - t.spacing.sm));
        const QRect imageRect(cardRect.left() + t.spacing.md, labelRect.bottom() + t.spacing.sm,
                              cardRect.width() - t.spacing.md * 2,
                              cardRect.bottom() - labelRect.bottom() - t.spacing.md - t.spacing.sm);
        tiles.push_back({i, cardRect, labelRect, imageRect});
    }
    return tiles;
}

int NarrowbandPaletteComposerPreviewGridWidget::tileIndexAt(const QPoint& position) const
{
    for (const auto& tile : tileGeometries(width()))
    {
        if (tile.cardRect.contains(position))
        {
            return tile.index;
        }
    }
    return -1;
}

QRect NarrowbandPaletteComposerPreviewGridWidget::imageDrawRect(const NarrowbandPaletteComposerPreviewFrame& frame,
                                                                const QRect& imageRect) const
{
    return centeredAspectRect(imageRect.adjusted(1, 1, -1, -1), frame.image.size());
}

QRect NarrowbandPaletteComposerPreviewGridWidget::sourcePixelRectFor(const QImage& image) const
{
    if (image.isNull())
    {
        return {};
    }

    const double zoom = std::clamp(m_zoom, kMinimumZoom, kMaximumZoom);
    const int sourceWidth =
        std::clamp(static_cast<int>(std::round(static_cast<double>(image.width()) / zoom)), 1, image.width());
    const int sourceHeight =
        std::clamp(static_cast<int>(std::round(static_cast<double>(image.height()) / zoom)), 1, image.height());
    const int centerX =
        static_cast<int>(std::round(clampUnit(m_viewCenter.x()) * static_cast<double>(image.width() - 1)));
    const int centerY =
        static_cast<int>(std::round(clampUnit(m_viewCenter.y()) * static_cast<double>(image.height() - 1)));
    const int left = std::clamp(centerX - sourceWidth / 2, 0, image.width() - sourceWidth);
    const int top = std::clamp(centerY - sourceHeight / 2, 0, image.height() - sourceHeight);
    return QRect(left, top, sourceWidth, sourceHeight);
}

void NarrowbandPaletteComposerPreviewGridWidget::updateMinimumGridHeight()
{
    setMinimumHeight(heightForWidth(width() > 0 ? width() : sizeHint().width()));
    updateGeometry();
}

void NarrowbandPaletteComposerPreviewGridWidget::resetView()
{
    m_zoom = kMinimumZoom;
    m_viewCenter = QPointF(0.5, 0.5);
    m_panning = false;
    m_activePanTile = -1;
    unsetCursor();
}

void NarrowbandPaletteComposerPreviewGridWidget::adjustZoom(const double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
    {
        return;
    }

    m_zoom = std::clamp(m_zoom * factor, kMinimumZoom, kMaximumZoom);
    clampViewCenter();
    update();
}

void NarrowbandPaletteComposerPreviewGridWidget::panBy(const QPoint& delta, const int tileIndex)
{
    if (tileIndex < 0 || tileIndex >= static_cast<int>(m_frames.size()) || m_zoom <= kMinimumZoom)
    {
        return;
    }

    const auto& frame = m_frames[static_cast<size_t>(tileIndex)];
    if (frame.image.isNull())
    {
        return;
    }

    const auto tiles = tileGeometries(width());
    const auto tileIt = std::find_if(tiles.begin(), tiles.end(),
                                     [tileIndex](const TileGeometry& tile) { return tile.index == tileIndex; });
    if (tileIt == tiles.end())
    {
        return;
    }

    const QRect drawRect = imageDrawRect(frame, tileIt->imageRect);
    if (drawRect.isEmpty())
    {
        return;
    }

    const QRect source = sourcePixelRectFor(frame.image);
    const double sourceDx = -static_cast<double>(delta.x()) * source.width() / static_cast<double>(drawRect.width());
    const double sourceDy = -static_cast<double>(delta.y()) * source.height() / static_cast<double>(drawRect.height());
    m_viewCenter.setX(m_viewCenter.x() + sourceDx / static_cast<double>(frame.image.width()));
    m_viewCenter.setY(m_viewCenter.y() + sourceDy / static_cast<double>(frame.image.height()));
    clampViewCenter();
    update();
}

void NarrowbandPaletteComposerPreviewGridWidget::clampViewCenter()
{
    if (m_frames.empty() || m_zoom <= kMinimumZoom)
    {
        m_viewCenter = QPointF(0.5, 0.5);
        return;
    }

    const double halfSpan = 0.5 / std::clamp(m_zoom, kMinimumZoom, kMaximumZoom);
    m_viewCenter.setX(std::clamp(m_viewCenter.x(), halfSpan, 1.0 - halfSpan));
    m_viewCenter.setY(std::clamp(m_viewCenter.y(), halfSpan, 1.0 - halfSpan));
}

void NarrowbandPaletteComposerPreviewGridWidget::updateTileToolTip(const QPoint& position)
{
    const int index = tileIndexAt(position);
    if (index < 0 || index >= static_cast<int>(m_frames.size()))
    {
        setToolTip(QString());
        return;
    }

    setToolTip(m_frames[static_cast<size_t>(index)].description);
}

bool NarrowbandPaletteComposerPreviewGridWidget::paletteExists(const an::NarrowbandPalette palette) const
{
    for (const auto& frame : m_frames)
    {
        if (frame.palette == palette)
        {
            return true;
        }
    }
    for (const auto& descriptor : m_descriptors)
    {
        if (descriptor.palette == palette)
        {
            return true;
        }
    }
    return false;
}

void NarrowbandPaletteComposerPreviewGridWidget::notifySelectionChanged()
{
    if (m_selectionChangedHandler)
    {
        m_selectionChangedHandler(m_selectedPalette);
    }
}

NarrowbandPaletteComposerSinglePreviewWidget::NarrowbandPaletteComposerSinglePreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("singlePreview"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(minimumSizeHint());
}

void NarrowbandPaletteComposerSinglePreviewWidget::setPaletteDescriptors(
    const std::vector<an::NarrowbandPaletteDescriptor>& descriptors)
{
    m_descriptors = descriptors;
    if (!paletteExists(m_selectedPalette) && !m_descriptors.empty())
    {
        m_selectedPalette = m_descriptors.front().palette;
    }
    update();
}

void NarrowbandPaletteComposerSinglePreviewWidget::setPreviewImages(
    std::vector<NarrowbandPaletteComposerPreviewFrame> frames)
{
    m_frames = std::move(frames);
    if (!paletteExists(m_selectedPalette) && !m_frames.empty())
    {
        m_selectedPalette = m_frames.front().palette;
    }
    resetView();
    update();
}

void NarrowbandPaletteComposerSinglePreviewWidget::clearPreviewImages()
{
    m_frames.clear();
    resetView();
    update();
}

bool NarrowbandPaletteComposerSinglePreviewWidget::hasPreviewImages() const
{
    return !m_frames.empty();
}

QString NarrowbandPaletteComposerSinglePreviewWidget::placeholderText() const
{
    return QStringLiteral("Select sources to update the palette grid");
}

an::NarrowbandPalette NarrowbandPaletteComposerSinglePreviewWidget::selectedPalette() const
{
    return m_selectedPalette;
}

void NarrowbandPaletteComposerSinglePreviewWidget::setSelectedPalette(const an::NarrowbandPalette palette)
{
    if (m_selectedPalette == palette || !paletteExists(palette))
    {
        return;
    }

    m_selectedPalette = palette;
    notifySelectionChanged();
    update();
}

void NarrowbandPaletteComposerSinglePreviewWidget::setSelectionChangedHandler(
    std::function<void(an::NarrowbandPalette)> handler)
{
    m_selectionChangedHandler = std::move(handler);
}

QSize NarrowbandPaletteComposerSinglePreviewWidget::sizeHint() const
{
    return QSize(760, 620);
}

QSize NarrowbandPaletteComposerSinglePreviewWidget::minimumSizeHint() const
{
    return QSize(kSinglePreviewMinimumWidth, kSinglePreviewMinimumHeight);
}

void NarrowbandPaletteComposerSinglePreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& t = an::ui::tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), t.colors.bgSurface);

    if (m_frames.empty())
    {
        const QRect bannerRect = rect().adjusted(t.spacing.xl, t.spacing.xl, -t.spacing.xl, -t.spacing.xl);
        painter.setPen(QPen(t.colors.borderSubtle, 1));
        painter.setBrush(t.colors.bgCard);
        painter.drawRoundedRect(bannerRect, t.radius.md, t.radius.md);
        painter.setFont(t.typography.heading3);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(bannerRect.adjusted(t.spacing.lg, t.spacing.lg, -t.spacing.lg, -t.spacing.lg),
                         Qt::AlignCenter | Qt::TextWordWrap, placeholderText());
        return;
    }

    const auto* frame = selectedFrame();
    if (!frame)
    {
        return;
    }

    const QRect previewRect = previewCardRect();
    painter.setPen(QPen(t.colors.borderSubtle, 1));
    painter.setBrush(t.colors.bgCard);
    painter.drawRoundedRect(previewRect, t.radius.md, t.radius.md);

    const int labelHeight = QFontMetrics(t.typography.heading3).height() + t.spacing.md;
    const QRect labelRect =
        previewRect.adjusted(t.spacing.md, t.spacing.sm, -t.spacing.md, -(previewRect.height() - labelHeight));
    painter.setFont(t.typography.heading3);
    painter.setPen(t.colors.textPrimary);
    painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, frame->name);

    const QRect largeImageRect = previewImageRect();
    painter.setPen(QPen(t.colors.borderSubtle, 1));
    painter.setBrush(t.colors.bgInput);
    painter.drawRoundedRect(largeImageRect, t.radius.sm, t.radius.sm);
    if (!frame->image.isNull())
    {
        const QRect drawRect = imageDrawRect(frame->image, largeImageRect);
        if (!drawRect.isEmpty())
        {
            const QRect sourceRect = sourcePixelRectFor(frame->image);
            painter.save();
            painter.setClipRect(largeImageRect);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, shouldSmoothImageTransform(drawRect, sourceRect));
            painter.drawImage(drawRect, frame->image, sourceRect);
            painter.restore();
        }
    }

    const QRect ribbonRect = galleryRect();
    painter.setPen(QPen(t.colors.borderSubtle, 1));
    painter.setBrush(t.colors.bgCard);
    painter.drawRoundedRect(ribbonRect, t.radius.md, t.radius.md);

    for (const auto& item : galleryGeometries())
    {
        if (item.index < 0 || item.index >= static_cast<int>(m_frames.size()))
        {
            continue;
        }

        const auto& galleryFrame = m_frames[static_cast<size_t>(item.index)];
        const bool selected = galleryFrame.palette == m_selectedPalette;
        painter.setPen(QPen(selected ? t.colors.accentPrimary : t.colors.borderSubtle, selected ? 2 : 1));
        painter.setBrush(selected ? t.colors.bgHover : t.colors.bgInput);
        painter.drawRoundedRect(item.cardRect, t.radius.sm, t.radius.sm);

        if (!galleryFrame.image.isNull())
        {
            const QRect thumbnailRect =
                centeredAspectRect(item.imageRect.adjusted(1, 1, -1, -1), galleryFrame.image.size());
            if (!thumbnailRect.isEmpty())
            {
                const QRect fullSourceRect(QPoint(0, 0), galleryFrame.image.size());
                painter.save();
                painter.setClipRect(item.imageRect);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                      shouldSmoothImageTransform(thumbnailRect, fullSourceRect));
                painter.drawImage(thumbnailRect, galleryFrame.image);
                painter.restore();
            }
        }

        painter.setFont(t.typography.caption);
        painter.setPen(selected ? t.colors.textAccent : t.colors.textSecondary);
        painter.drawText(item.labelRect, Qt::AlignCenter | Qt::TextSingleLine, galleryFrame.name);
    }
}

void NarrowbandPaletteComposerSinglePreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton || m_frames.empty())
    {
        QWidget::mousePressEvent(event);
        return;
    }

    const int galleryIndex = galleryIndexAt(event->pos());
    if (galleryIndex >= 0)
    {
        setSelectedPalette(m_frames[static_cast<size_t>(galleryIndex)].palette);
        event->accept();
        return;
    }

    if (!previewImageRect().contains(event->pos()))
    {
        QWidget::mousePressEvent(event);
        return;
    }

    m_lastPanPosition = event->pos();
    m_panning = true;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void NarrowbandPaletteComposerSinglePreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!event)
    {
        return;
    }

    updateGalleryToolTip(event->pos());
    if (!m_panning)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }

    panBy(event->pos() - m_lastPanPosition);
    m_lastPanPosition = event->pos();
    event->accept();
}

void NarrowbandPaletteComposerSinglePreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton && m_panning)
    {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void NarrowbandPaletteComposerSinglePreviewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton && previewImageRect().contains(event->pos()))
    {
        resetView();
        update();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void NarrowbandPaletteComposerSinglePreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (!event || m_frames.empty() || !previewImageRect().contains(event->position().toPoint()))
    {
        QWidget::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        QWidget::wheelEvent(event);
        return;
    }

    const double steps = static_cast<double>(delta) / 120.0;
    adjustZoom(std::pow(kWheelZoomStep, steps));
    event->accept();
}

const NarrowbandPaletteComposerPreviewFrame* NarrowbandPaletteComposerSinglePreviewWidget::selectedFrame() const
{
    for (const auto& frame : m_frames)
    {
        if (frame.palette == m_selectedPalette)
        {
            return &frame;
        }
    }
    return m_frames.empty() ? nullptr : &m_frames.front();
}

QRect NarrowbandPaletteComposerSinglePreviewWidget::previewCardRect() const
{
    const auto& t = an::ui::tokens();
    const QRect available = rect().adjusted(t.spacing.md, t.spacing.md, -t.spacing.md, -t.spacing.md);
    return QRect(available.left(), available.top(), available.width(),
                 (std::max)(1, available.height() - kGalleryHeight - t.spacing.md));
}

QRect NarrowbandPaletteComposerSinglePreviewWidget::previewImageRect() const
{
    const auto& t = an::ui::tokens();
    const QRect previewRect = previewCardRect();
    const int labelHeight = QFontMetrics(t.typography.heading3).height() + t.spacing.md;
    return QRect(previewRect.left() + t.spacing.md, previewRect.top() + labelHeight + t.spacing.sm,
                 (std::max)(1, previewRect.width() - t.spacing.md * 2),
                 (std::max)(1, previewRect.height() - labelHeight - t.spacing.md - t.spacing.sm));
}

QRect NarrowbandPaletteComposerSinglePreviewWidget::galleryRect() const
{
    const auto& t = an::ui::tokens();
    const QRect available = rect().adjusted(t.spacing.md, t.spacing.md, -t.spacing.md, -t.spacing.md);
    return QRect(available.left(), available.bottom() - kGalleryHeight + 1, available.width(), kGalleryHeight);
}

QRect NarrowbandPaletteComposerSinglePreviewWidget::imageDrawRect(const QImage& image, const QRect& imageRect) const
{
    return centeredAspectRect(imageRect.adjusted(1, 1, -1, -1), image.size());
}

QRect NarrowbandPaletteComposerSinglePreviewWidget::sourcePixelRectFor(const QImage& image) const
{
    if (image.isNull())
    {
        return {};
    }

    const double zoom = std::clamp(m_zoom, kMinimumZoom, kMaximumZoom);
    const int sourceWidth =
        std::clamp(static_cast<int>(std::round(static_cast<double>(image.width()) / zoom)), 1, image.width());
    const int sourceHeight =
        std::clamp(static_cast<int>(std::round(static_cast<double>(image.height()) / zoom)), 1, image.height());
    const int centerX =
        static_cast<int>(std::round(clampUnit(m_viewCenter.x()) * static_cast<double>(image.width() - 1)));
    const int centerY =
        static_cast<int>(std::round(clampUnit(m_viewCenter.y()) * static_cast<double>(image.height() - 1)));
    const int left = std::clamp(centerX - sourceWidth / 2, 0, image.width() - sourceWidth);
    const int top = std::clamp(centerY - sourceHeight / 2, 0, image.height() - sourceHeight);
    return QRect(left, top, sourceWidth, sourceHeight);
}

std::vector<NarrowbandPaletteComposerSinglePreviewWidget::GalleryGeometry>
NarrowbandPaletteComposerSinglePreviewWidget::galleryGeometries() const
{
    const auto& t = an::ui::tokens();
    const int frameCount = static_cast<int>(m_frames.size());
    if (frameCount <= 0)
    {
        return {};
    }

    const QRect ribbonRect = galleryRect().adjusted(t.spacing.md, t.spacing.sm, -t.spacing.md, -t.spacing.sm);
    const int gap = t.spacing.sm;
    const int availableWidth = (std::max)(1, ribbonRect.width() - gap * (frameCount - 1));
    const int itemWidth = (std::max)(1, (std::min)(availableWidth / frameCount, kGalleryMaximumItemWidth));
    const int totalWidth = itemWidth * frameCount + gap * (frameCount - 1);
    const int left = ribbonRect.left() + (std::max)(0, (ribbonRect.width() - totalWidth) / 2);
    const int labelHeight = QFontMetrics(t.typography.caption).height() + t.spacing.xs;

    std::vector<GalleryGeometry> items;
    items.reserve(static_cast<size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i)
    {
        const QRect cardRect(left + i * (itemWidth + gap), ribbonRect.top(), itemWidth, ribbonRect.height());
        const QRect labelRect(cardRect.left() + t.spacing.xs, cardRect.bottom() - labelHeight - t.spacing.xs + 1,
                              cardRect.width() - t.spacing.xs * 2, labelHeight);
        const QRect imageRect(cardRect.left() + t.spacing.xs, cardRect.top() + t.spacing.xs,
                              cardRect.width() - t.spacing.xs * 2,
                              (std::max)(1, labelRect.top() - cardRect.top() - t.spacing.xs * 2));
        items.push_back({i, cardRect, imageRect, labelRect});
    }
    return items;
}

int NarrowbandPaletteComposerSinglePreviewWidget::galleryIndexAt(const QPoint& position) const
{
    for (const auto& item : galleryGeometries())
    {
        if (item.cardRect.contains(position))
        {
            return item.index;
        }
    }
    return -1;
}

void NarrowbandPaletteComposerSinglePreviewWidget::resetView()
{
    m_zoom = kMinimumZoom;
    m_viewCenter = QPointF(0.5, 0.5);
    m_panning = false;
    unsetCursor();
}

void NarrowbandPaletteComposerSinglePreviewWidget::adjustZoom(const double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
    {
        return;
    }

    m_zoom = std::clamp(m_zoom * factor, kMinimumZoom, kMaximumZoom);
    clampViewCenter();
    update();
}

void NarrowbandPaletteComposerSinglePreviewWidget::panBy(const QPoint& delta)
{
    const auto* frame = selectedFrame();
    if (!frame || frame->image.isNull() || m_zoom <= kMinimumZoom)
    {
        return;
    }

    const QRect drawRect = imageDrawRect(frame->image, previewImageRect());
    if (drawRect.isEmpty())
    {
        return;
    }

    const QRect source = sourcePixelRectFor(frame->image);
    const double sourceDx = -static_cast<double>(delta.x()) * source.width() / static_cast<double>(drawRect.width());
    const double sourceDy = -static_cast<double>(delta.y()) * source.height() / static_cast<double>(drawRect.height());
    m_viewCenter.setX(m_viewCenter.x() + sourceDx / static_cast<double>(frame->image.width()));
    m_viewCenter.setY(m_viewCenter.y() + sourceDy / static_cast<double>(frame->image.height()));
    clampViewCenter();
    update();
}

void NarrowbandPaletteComposerSinglePreviewWidget::clampViewCenter()
{
    if (m_frames.empty() || m_zoom <= kMinimumZoom)
    {
        m_viewCenter = QPointF(0.5, 0.5);
        return;
    }

    const double halfSpan = 0.5 / std::clamp(m_zoom, kMinimumZoom, kMaximumZoom);
    m_viewCenter.setX(std::clamp(m_viewCenter.x(), halfSpan, 1.0 - halfSpan));
    m_viewCenter.setY(std::clamp(m_viewCenter.y(), halfSpan, 1.0 - halfSpan));
}

void NarrowbandPaletteComposerSinglePreviewWidget::updateGalleryToolTip(const QPoint& position)
{
    const int index = galleryIndexAt(position);
    if (index < 0 || index >= static_cast<int>(m_frames.size()))
    {
        const auto* frame = selectedFrame();
        setToolTip(frame ? frame->description : QString());
        return;
    }

    setToolTip(m_frames[static_cast<size_t>(index)].description);
}

bool NarrowbandPaletteComposerSinglePreviewWidget::paletteExists(const an::NarrowbandPalette palette) const
{
    for (const auto& frame : m_frames)
    {
        if (frame.palette == palette)
        {
            return true;
        }
    }
    for (const auto& descriptor : m_descriptors)
    {
        if (descriptor.palette == palette)
        {
            return true;
        }
    }
    return false;
}

void NarrowbandPaletteComposerSinglePreviewWidget::notifySelectionChanged()
{
    if (m_selectionChangedHandler)
    {
        m_selectionChangedHandler(m_selectedPalette);
    }
}

} // namespace an::ui::processes
