// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/ViewProcessingNotice.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>

W_OBJECT_IMPL(an::ui::ViewProcessingNotice)

namespace an::ui
{

namespace
{

[[nodiscard]] QFont noticeTextFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontSmall);
}

[[nodiscard]] QFont noticeShareFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

[[nodiscard]] QFont noticeButtonFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontSmall, QFont::DemiBold);
}

/// The 1 px edge under a raised part over an image: the part's shape moved a pixel down, less the part itself, as a
/// shadow is drawn only outside what casts it.
void paintEdgeUnder(QPainter& painter, const QRectF& part, const qreal radius, const QColor& edge)
{
    QPainterPath below;
    below.addRoundedRect(part.translated(0.0, 1.0), radius, radius);
    QPainterPath shape;
    shape.addRoundedRect(part, radius, radius);
    painter.fillPath(below.subtracted(shape), edge);
}

/// The pill's button: its label in the text over images, in semibold, in an outline of that text at 40 %.
class NoticeButton final : public QAbstractButton
{
public:
    explicit NoticeButton(QWidget* parent) : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(2 * parts::kNoticeButtonPadding + parts::textWidth(noticeButtonFont(), text()),
                     parts::kNoticeButtonHeight);
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = t.radius.sm;
        parts::paintStateLayer(painter, bounds, radius, m_hovered && isEnabled(), isDown());
        painter.setPen(QPen(parts::atShare(t.colors.textOnImage, parts::kNoticeButtonOutlineShare), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, radius, radius);
        if (m_keyboardFocus && isEnabled())
        {
            parts::paintFocusRing(painter, QRectF(rect()), radius);
        }
        painter.setFont(noticeButtonFont());
        painter.setPen(t.colors.textOnImage);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

    void focusInEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
        QAbstractButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = false;
        QAbstractButton::focusOutEvent(event);
        update();
    }

    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    bool m_keyboardFocus = false;
    bool m_hovered = false;
};

} // namespace

ViewProcessingNotice::ViewProcessingNotice(QWidget* covered) : QWidget(covered)
{
    setObjectName(QStringLiteral("viewProcessingNotice"));
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();
    if (covered != nullptr)
    {
        covered->installEventFilter(this);
        setGeometry(covered->rect());
    }

    m_button = new NoticeButton(this);
    m_button->setObjectName(QStringLiteral("viewProcessingNoticeAction"));
    m_button->hide();
    QObject::connect(m_button, &QAbstractButton::clicked, this, [this]() {
        // A copy: the action may set another notice, or clear this one.
        const std::function<void()> action = m_notice.action;
        if (action)
        {
            action();
        }
    });
}

ViewProcessingNotice::~ViewProcessingNotice() = default;

void ViewProcessingNotice::setNotice(const ProcessingNotice& notice)
{
    m_notice = notice;
    const bool offered = !notice.actionLabel.isEmpty() && static_cast<bool>(notice.action);
    m_button->setText(notice.actionLabel);
    m_button->setAccessibleName(notice.actionLabel);
    m_button->setVisible(offered);
    setAccessibleName(notice.text);
    layoutPill();
    update();
}

const ProcessingNotice& ViewProcessingNotice::notice() const noexcept
{
    return m_notice;
}

QString ViewProcessingNotice::progressText() const
{
    if (!m_notice.progress.has_value())
    {
        return {};
    }
    const double share = std::clamp(*m_notice.progress, 0.0, 1.0);
    return QStringLiteral("%1 %").arg(static_cast<int>(std::lround(share * 100.0)));
}

QSize ViewProcessingNotice::pillSize() const
{
    int width = parts::kNoticePillPaddingStart + parts::kNoticeLockIcon + parts::kNoticePillGap +
                parts::textWidth(noticeTextFont(), m_notice.text);
    if (m_notice.progress.has_value())
    {
        width += parts::kNoticePillGap + parts::textWidth(noticeShareFont(), progressText()) + parts::kNoticePillGap +
                 parts::kNoticeBarWidth;
    }
    if (m_button->isVisibleTo(this))
    {
        width += parts::kNoticePillGap + parts::kNoticeButtonGap + m_button->sizeHint().width() +
                 parts::kNoticePillPaddingEnd;
    }
    else
    {
        // With no button at its end the pill closes as it opens.
        width += parts::kNoticePillPaddingStart;
    }
    return QSize(width, parts::kNoticePillHeight);
}

QRect ViewProcessingNotice::pillRect() const
{
    const QSize wanted = pillSize();
    const int width = (std::min)(wanted.width(), (std::max)(0, this->width()));
    return QRect((this->width() - width) / 2, (height() - wanted.height()) / 2, width, wanted.height());
}

QAbstractButton* ViewProcessingNotice::actionButton() const
{
    return m_button->isVisibleTo(this) ? m_button : nullptr;
}

void ViewProcessingNotice::layoutPill()
{
    const QRect pill = pillRect();
    const QSize button = m_button->sizeHint();
    m_button->setGeometry(pill.right() + 1 - parts::kNoticePillPaddingEnd - button.width(),
                          pill.top() + (pill.height() - button.height()) / 2, button.width(), button.height());
}

bool ViewProcessingNotice::event(QEvent* event)
{
    switch (event->type())
    {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::FontChange:
        layoutPill();
        update();
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

bool ViewProcessingNotice::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
    {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void ViewProcessingNotice::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutPill();
}

void ViewProcessingNotice::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), t.colors.scrimSoft);

    const QRectF pill(pillRect());
    const qreal radius = parts::kNoticePillRadius;
    paintEdgeUnder(painter, pill, radius, t.colors.edge);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.colors.scrim);
    painter.drawRoundedRect(pill, radius, radius);

    painter.save();
    painter.setClipRect(pill);
    const qreal centre = pill.center().y();
    qreal x = pill.left() + parts::kNoticePillPaddingStart;
    const qreal lock = parts::kNoticeLockIcon;
    parts::paintIcon(painter, QIcon(QStringLiteral(":/AfterNight/icons/SVG/lock.svg")),
                     QRectF(x, centre - lock / 2.0, lock, lock), t.colors.accentOnImage);
    x += lock + parts::kNoticePillGap;

    // What runs takes what room the pill was given, elided when the view is narrower than the pill.
    const QFont textFont = noticeTextFont();
    qreal tail = 0.0;
    const QString share = progressText();
    if (!share.isEmpty())
    {
        tail += parts::kNoticePillGap + parts::textWidth(noticeShareFont(), share) + parts::kNoticePillGap +
                parts::kNoticeBarWidth;
    }
    const qreal end = m_button->isVisibleTo(this)
                          ? m_button->geometry().left() - parts::kNoticeButtonGap - parts::kNoticePillGap
                          : pill.right() + 1.0 - parts::kNoticePillPaddingStart;
    const qreal textRoom = (std::max)(0.0, end - tail - x);
    painter.setFont(textFont);
    painter.setPen(t.colors.textOnImage);
    const QString shown = QFontMetricsF(textFont).elidedText(m_notice.text, Qt::ElideRight, textRoom);
    painter.drawText(QRectF(x, pill.top(), textRoom, pill.height()), Qt::AlignLeft | Qt::AlignVCenter, shown);
    x += textRoom;

    if (!share.isEmpty())
    {
        x += parts::kNoticePillGap;
        const QFont shareFont = noticeShareFont();
        const qreal shareWidth = parts::textWidth(shareFont, share);
        painter.setFont(shareFont);
        painter.setPen(t.colors.textOnImageSecondary);
        painter.drawText(QRectF(x, pill.top(), shareWidth, pill.height()), Qt::AlignLeft | Qt::AlignVCenter, share);
        x += shareWidth + parts::kNoticePillGap;

        const QRectF track(x, centre - parts::kNoticeBarHeight / 2.0, parts::kNoticeBarWidth, parts::kNoticeBarHeight);
        const qreal barRadius = parts::kNoticeBarHeight / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(parts::atShare(t.colors.textOnImage, parts::kNoticeBarTrackShare));
        painter.drawRoundedRect(track, barRadius, barRadius);
        const double done = std::clamp(m_notice.progress.value_or(0.0), 0.0, 1.0);
        if (done > 0.0)
        {
            painter.setBrush(t.colors.accentOnImage);
            painter.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * done, track.height()), barRadius,
                                    barRadius);
        }
    }
    painter.restore();
}

void ViewProcessingNotice::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void ViewProcessingNotice::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void ViewProcessingNotice::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

void ViewProcessingNotice::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

} // namespace an::ui
