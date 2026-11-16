// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/HomeParts.h"

#include "controls/DashboardFileFacts.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEvent>
#include <QFontMetrics>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QThreadPool>
#include <verdigris/wobjectimpl.h>

#include <algorithm>

W_OBJECT_IMPL(an::ui::HomeText)

namespace an::ui
{

// ---- HomeText

HomeText::HomeText(const Role role, const QString& text, QWidget* parent) : QLabel(text, parent), m_role(role)
{
    setTextFormat(Qt::PlainText);
}

void HomeText::setRole(const Role role)
{
    if (m_role == role)
    {
        return;
    }
    m_role = role;
    applyRoleFont();
    updateGeometry();
    update();
}

HomeText::Role HomeText::role() const noexcept
{
    return m_role;
}

void HomeText::setElideMode(const Qt::TextElideMode mode)
{
    m_elide = mode;
    updateGeometry();
    update();
}

QFont HomeText::fontFor(const Role role)
{
    const DesignTokens& t = tokens();
    switch (role)
    {
    case Role::Display:
        return parts::sizedFont(t.typography.heading1, parts::kHomeDisplayFont, QFont::DemiBold);
    case Role::Heading:
        return parts::sizedFont(t.typography.heading1, parts::kHomeHeadingFont, QFont::DemiBold);
    case Role::Section:
        return parts::sizedFont(t.typography.heading2, parts::kHomeSectionFont, QFont::DemiBold);
    case Role::Strong:
        return parts::sizedFont(t.typography.body, t.metrics.fontBody, QFont::DemiBold);
    case Role::Body:
    case Role::Secondary:
        return parts::sizedFont(t.typography.body, t.metrics.fontBody);
    case Role::Caption:
        return parts::sizedFont(t.typography.body, t.metrics.fontSmall);
    case Role::Caps:
    case Role::CapsAccent:
        return parts::capsFont(t.typography.caption, t.metrics.fontCaption);
    case Role::Mono:
        return parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
    }
    return t.typography.body;
}

QColor HomeText::colourFor(const Role role)
{
    const ColorTokens& c = tokens().colors;
    switch (role)
    {
    case Role::Display:
    case Role::Heading:
    case Role::Section:
    case Role::Strong:
        return c.textPrimary;
    case Role::Body:
    case Role::Secondary:
    case Role::Mono:
        return c.textSecondary;
    case Role::Caption:
    case Role::Caps:
        return c.textTertiary;
    case Role::CapsAccent:
        return c.textAccent;
    }
    return c.textPrimary;
}

QSize HomeText::sizeHint() const
{
    QSize hint = QLabel::sizeHint();
    if (!wordWrap())
    {
        const QMargins margins = contentsMargins();
        hint.setHeight((std::max)(hint.height(), fontMetrics().height() + margins.top() + margins.bottom()));
    }
    return hint;
}

QSize HomeText::minimumSizeHint() const
{
    QSize hint = QLabel::minimumSizeHint();
    hint.setHeight(sizeHint().height());
    // A line that elides may be as narrow as its place gives it.
    if (!wordWrap() && m_elide != Qt::ElideNone)
    {
        hint.setWidth(0);
    }
    return hint;
}

bool HomeText::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::Polish)
    {
        applyRoleFont();
    }
    return QLabel::event(event);
}

void HomeText::changeEvent(QEvent* event)
{
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        applyRoleFont();
        updateGeometry();
        update();
    }
    QLabel::changeEvent(event);
}

void HomeText::paintEvent(QPaintEvent* /*event*/)
{
    if (text().isEmpty())
    {
        return;
    }
    QPainter painter(this);
    painter.setFont(font());
    painter.setPen(colourFor(m_role));
    const QRect place = contentsRect();
    if (wordWrap())
    {
        painter.drawText(place, static_cast<int>(alignment()) | Qt::TextWordWrap, text());
        return;
    }
    const QString shown =
        m_elide == Qt::ElideNone ? text() : QFontMetrics(font()).elidedText(text(), m_elide, place.width());
    painter.drawText(place, static_cast<int>(alignment()) | Qt::TextSingleLine, shown);
}

void HomeText::applyRoleFont()
{
    const QFont wanted = fontFor(m_role);
    if (font() != wanted)
    {
        setFont(wanted);
    }
}

// ---- A recent file's facts

HomeRecentFacts probeHomeRecentFile(const QString& path)
{
    HomeRecentFacts facts;
    const QFileInfo info(path);
    if (info.exists())
    {
        facts.modified = info.lastModified();
    }
    if (isDashboardSequence(path))
    {
        if (const std::optional<DashboardSequenceFacts> sequence = probeDashboardSequence(path))
        {
            facts.size = sequence->frameSize;
            facts.frameCount = sequence->frameCount;
        }
    }
    else if (const std::optional<DashboardFileFacts> image = probeDashboardFile(path))
    {
        facts.size = image->size;
    }
    return facts;
}

bool isHomeSequence(const QString& path)
{
    return isDashboardSequence(path);
}

QString homeTypeText(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("fit") || suffix == QLatin1String("fits") || suffix == QLatin1String("fts"))
    {
        return QStringLiteral("FITS");
    }
    if (suffix == QLatin1String("tif") || suffix == QLatin1String("tiff"))
    {
        return QStringLiteral("TIFF");
    }
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") || suffix == QLatin1String("jpe"))
    {
        return QStringLiteral("JPEG");
    }
    if (suffix == QLatin1String("anseq"))
    {
        return QObject::tr("LIST");
    }
    return suffix.toUpper();
}

QString homeFactsText(const HomeRecentFacts& facts)
{
    QStringList parts;
    if (facts.size.isValid() && !facts.size.isEmpty())
    {
        parts << QStringLiteral("%1x%2").arg(facts.size.width()).arg(facts.size.height());
    }
    if (facts.frameCount.has_value())
    {
        parts << (*facts.frameCount == 1u ? QObject::tr("1 frame")
                                          : QObject::tr("%1 frames").arg(static_cast<qulonglong>(*facts.frameCount)));
    }
    if (facts.modified.isValid())
    {
        parts << QLocale::c().toString(facts.modified, QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    return parts.join(QStringLiteral(" · "));
}

void paintHomePicture(QPainter& painter, const QRectF& place, const QPixmap& shown, const qreal radius)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath shape;
    shape.addRoundedRect(place, radius, radius);
    painter.fillPath(shape, tokens().colors.bgWell);
    if (!shown.isNull())
    {
        painter.setClipPath(shape);
        const QSizeF size = shown.deviceIndependentSize();
        const qreal scale = (std::max)(place.width() / size.width(), place.height() / size.height());
        const QSizeF covered = size * scale;
        const QRectF target(place.center().x() - covered.width() / 2.0, place.center().y() - covered.height() / 2.0,
                            covered.width(), covered.height());
        painter.drawPixmap(target, shown, QRectF(QPointF(0.0, 0.0), shown.size()));
    }
    painter.restore();
}

QString homeThumbnailPath(const QString& path)
{
    if (IAppContext* context = appContext())
    {
        if (IThumbnailPathProvider* thumbnails = context->getThumbnailPathProvider())
        {
            return thumbnails->selectThumbnailPath(path);
        }
    }
    return {};
}

void readHomeRecentFile(QObject* context, const QString& path, const QString& thumbnailPath, const QSize& pictureSize,
                        const HomeRecentProbe& probe,
                        const std::function<void(const QImage&, const HomeRecentFacts&)>& done)
{
    if (context == nullptr || !done)
    {
        return;
    }
    QPointer<QObject> receiver(context);
    QThreadPool::globalInstance()->start([receiver, path, thumbnailPath, pictureSize, probe, done]() {
        QImage picture;
        if (!thumbnailPath.isEmpty())
        {
            QImageReader reader(thumbnailPath);
            reader.setAutoTransform(true);
            picture = reader.read();
            if (!picture.isNull() && pictureSize.isValid() &&
                (picture.width() > pictureSize.width() || picture.height() > pictureSize.height()))
            {
                picture = picture.scaled(pictureSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            }
        }
        const HomeRecentFacts facts = probe ? probe(path) : HomeRecentFacts{};
        if (receiver.isNull())
        {
            return;
        }
        QMetaObject::invokeMethod(
            receiver.data(),
            [receiver, done, picture = std::move(picture), facts]() {
                if (!receiver.isNull())
                {
                    done(picture, facts);
                }
            },
            Qt::QueuedConnection);
    });
}

} // namespace an::ui
