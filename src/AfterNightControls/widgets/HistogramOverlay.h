// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtCore/QEvent>
#include <QtCore/QMetaObject>
#include <QtCore/QThread>

#include "theme/AnDesignTokens.h"

class HistogramOverlay : public QWidget
{
public:
    explicit HistogramOverlay(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        // Child overlay, no window manager involvement
        setWindowFlag(Qt::FramelessWindowHint, true);
        setAttribute(Qt::WA_DeleteOnClose);
        hide();
    }

    void setText(const QString& text)
    {
        if (m_Text == text)
            return;
        m_Text = text;

        if (QThread::currentThread() == thread())
        {
            updateGeometryFromText();
            update();
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this]() {
                updateGeometryFromText();
                update();
            },
            Qt::QueuedConnection);
    }

    const QString& text() const
    {
        return m_Text;
    }

    void setAnchor(const QPoint& localPos)
    {
        // Position the overlay with a small offset from the anchor
        const QPoint offset(16, -16);
        move(localPos + offset);
    }

    QSize sizeHint() const override
    {
        QFontMetrics fm(font());
        const int pad = 8;
        if (m_Text.isEmpty())
            return QSize(pad * 2, pad * 2);

        // Compute multi-line bounding size: widest line and total height
        const QStringList lines = m_Text.split('\n');
        int maxWidth = 0;
        for (const QString& line : lines)
            maxWidth = (std::max)(maxWidth, fm.horizontalAdvance(line));
        const int totalHeight = fm.height() * lines.size();

        return QSize(maxWidth + pad * 2, totalHeight + pad * 2);
    }

    void setBackgroundColor(const QColor& color)
    {
        m_BackgroundColor = color;
        update();
    }

    void setFontColor(const QColor& color)
    {
        m_FontColor = color;
        update();
    }

    void setRoundedCorners(bool enabled)
    {
        m_RoundedCorners = enabled;
        update();
    }

    QColor getBackgroundColor() const
    {
        return resolvedBackgroundColor();
    }

    QColor getFontColor() const
    {
        return resolvedFontColor();
    }

    bool getRoundedCorners() const
    {
        return m_RoundedCorners;
    }

    qreal cornerRadius() const
    {
        return resolvedCornerRadius();
    }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        Q_UNUSED(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int pad = 6;

        const QRect r = rect();

        p.setBrush(resolvedBackgroundColor());
        p.setPen(Qt::NoPen);
        if (m_RoundedCorners)
        {
            const qreal radius = resolvedCornerRadius();
            p.drawRoundedRect(r, radius, radius);
        }
        else
        {
            p.drawRect(r);
        }

        p.setPen(resolvedFontColor());
        // Draw text from top-left within padding; newlines are handled automatically
        p.drawText(r.adjusted(pad, pad, -pad, -pad), Qt::AlignLeft | Qt::AlignTop, m_Text);
    }

    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
            event->type() == QEvent::ThemeChange)
            update();

        QWidget::changeEvent(event);
    }

private:
    void updateGeometryFromText()
    {
        resize(sizeHint());
    }

    [[nodiscard]] QColor resolvedBackgroundColor() const
    {
        return m_BackgroundColor.isValid() ? m_BackgroundColor : an::ui::tokens().colors.bgSurface;
    }

    [[nodiscard]] QColor resolvedFontColor() const
    {
        return m_FontColor.isValid() ? m_FontColor : an::ui::tokens().colors.textPrimary;
    }

    [[nodiscard]] qreal resolvedCornerRadius() const
    {
        return static_cast<qreal>(an::ui::tokens().radius.md);
    }

    QColor m_BackgroundColor;
    QColor m_FontColor;
    bool m_RoundedCorners = true;

    QString m_Text;
};
