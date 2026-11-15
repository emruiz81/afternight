// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnTaskThumbnail.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/PainterGuard.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>

namespace an::ui
{

void AnTaskThumbnail::setPicture(const QImage& picture)
{
    if (picture.isNull())
    {
        m_picture.clear();
        m_pictureKey = 0;
        return;
    }
    if (picture.cacheKey() == m_pictureKey)
    {
        return;
    }
    m_pictureKey = picture.cacheKey();
    m_picture.setPicture(picture);
}

bool AnTaskThumbnail::hasPicture() const
{
    return !m_picture.isNull();
}

QPixmap AnTaskThumbnail::shownPicture(const QObject* owner, const std::function<void()>& repaint) const
{
    return m_picture.shown(owner, repaint);
}

void AnTaskThumbnail::paint(QPainter& painter, const QPointF& topLeft, const RailItem& item, const Look& look) const
{
    using namespace parts;
    const DesignTokens& t = tokens();
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF box(topLeft, QSizeF(kTaskBox, kTaskBox));
    const qreal inset = (kTaskBox - kTaskPicture) / 2.0;
    const QRectF pictureRect = box.adjusted(inset, inset, -inset, -inset);
    const bool picture = hasPicture();

    if (picture)
    {
        const QPixmap shown = m_picture.shown(look.owner, look.repaint);
        if (!shown.isNull())
        {
            // The middle of the picture, as large a square as it holds, fills the place.
            const QSizeF size = shown.deviceIndependentSize();
            const qreal side = (std::min)(size.width(), size.height());
            const QRectF source((size.width() - side) / 2.0, (size.height() - side) / 2.0, side, side);
            const qreal ratio = shown.devicePixelRatio();
            const QRectF sourcePixels(source.topLeft() * ratio, source.size() * ratio);
            const PainterGuard clip(&painter);
            QPainterPath rounded;
            rounded.addRoundedRect(pictureRect, kTaskPictureRadius, kTaskPictureRadius);
            painter.setClipPath(rounded);
            painter.setOpacity(painter.opacity() * (item.floating ? kTaskFloatingOpacity : 1.0));
            painter.drawPixmap(pictureRect, shown, sourcePixels);
        }
    }
    else
    {
        // A task with no target image: the process's glyph on a tile.
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.layers.surface3);
        painter.drawRoundedRect(pictureRect, kTaskPictureRadius, kTaskPictureRadius);
        const QIcon& glyph = item.floating ? look.windowGlyph : look.glyph;
        const QPointF centre = pictureRect.center();
        paintIcon(painter, glyph,
                  QRectF(centre.x() - kTaskTileGlyph / 2.0, centre.y() - kTaskTileGlyph / 2.0, kTaskTileGlyph,
                         kTaskTileGlyph),
                  t.colors.textPrimary);
    }
    if (look.hovered && item.runPhase != RailRunPhase::Running)
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(atShare(t.colors.stateLayer, kPressedLayer), kTaskRing));
        const qreal out = kTaskRing / 2.0;
        painter.drawRoundedRect(pictureRect.adjusted(-out, -out, out, out), kTaskPictureRadius + out,
                                kTaskPictureRadius + out);
    }

    if (item.runPhase == RailRunPhase::Running)
    {
        // The ring: the accent's track, and the accent's arc clockwise from the top.
        const qreal half = kTaskRing / 2.0;
        const QRectF ring = box.adjusted(half, half, -half, -half);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(t.colors.accentTrack, kTaskRing));
        painter.drawEllipse(ring);
        QPen arc(t.colors.accentPrimary, kTaskRing);
        arc.setCapStyle(Qt::RoundCap);
        painter.setPen(arc);
        if (item.progress)
        {
            const int done = std::clamp(*item.progress, 0, 100);
            if (done >= 100)
            {
                painter.drawEllipse(ring);
            }
            else if (done > 0)
            {
                // Qt counts sixteenths of a degree, anticlockwise from three o'clock.
                painter.drawArc(ring, 90 * 16, -static_cast<int>(done * 360 * 16 / 100));
            }
        }
        else
        {
            const int start = 90 * 16 - static_cast<int>(look.turn * 360.0 * 16.0);
            painter.drawArc(ring, start, -90 * 16);
        }
    }

    if (picture || item.floating)
    {
        // The process's glyph - the window's, for a task in a window of its own - on a disc ringed by the rail.
        const QRectF disc(box.right() - kTaskGlyphDisc, box.bottom() - kTaskGlyphDisc, kTaskGlyphDisc, kTaskGlyphDisc);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.layers.surface1);
        painter.drawEllipse(disc.adjusted(-kRailBadgeRing, -kRailBadgeRing, kRailBadgeRing, kRailBadgeRing));
        painter.setBrush(t.layers.surface4);
        painter.drawEllipse(disc);
        const QPointF centre = disc.center();
        paintIcon(painter, item.floating ? look.windowGlyph : look.glyph,
                  QRectF(centre.x() - kTaskGlyph / 2.0, centre.y() - kTaskGlyph / 2.0, kTaskGlyph, kTaskGlyph),
                  t.colors.textPrimary);
    }

    const bool ended = item.runPhase == RailRunPhase::Succeeded || item.runPhase == RailRunPhase::Failed;
    if (item.unseenResult && ended)
    {
        const QRectF dot(box.right() + 1.0 - kTaskResultDot, box.top() - 1.0, kTaskResultDot, kTaskResultDot);
        const QRectF ringed = dot.adjusted(-kRailBadgeRing, -kRailBadgeRing, kRailBadgeRing, kRailBadgeRing);
        const QPointF centre = dot.center();
        const QRectF glyph(centre.x() - kTaskResultGlyph / 2.0, centre.y() - kTaskResultGlyph / 2.0, kTaskResultGlyph,
                           kTaskResultGlyph);
        painter.setPen(Qt::NoPen);
        if (item.runPhase == RailRunPhase::Succeeded)
        {
            painter.setBrush(t.layers.surface1);
            painter.drawEllipse(ringed);
            painter.setBrush(t.colors.statusSuccess);
            painter.drawEllipse(dot);
            paintCheckMark(painter, glyph, t.layers.surface0, kGlyphStroke);
        }
        else
        {
            painter.setBrush(t.layers.surface1);
            painter.drawRoundedRect(ringed, kTaskFailedRadius + kRailBadgeRing, kTaskFailedRadius + kRailBadgeRing);
            painter.setBrush(t.colors.statusError);
            painter.drawRoundedRect(dot, kTaskFailedRadius, kTaskFailedRadius);
            paintCrossMark(painter, glyph, t.layers.surface0, kGlyphStroke);
        }
    }
}

} // namespace an::ui
