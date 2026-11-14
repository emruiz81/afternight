// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnSnackbar.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/Elevation.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QTimer>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnSnackbar)

namespace
{

using std::chrono::milliseconds;

/// How long a running lifetime goes at most without reading its clock.
constexpr milliseconds kCheckInterval{250};

QFont textFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontBody);
}

QFont detailFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

QFont actionFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontBody, QFont::DemiBold);
}

/// The message's action: its text in the accent drawn over an image, a small control high, the state colour laid over
/// it when hovered or pressed, the focus ring when the keyboard brought the focus; clicked by the pointer, Space and
/// Enter.
class SnackbarAction final : public QAbstractButton
{
public:
    explicit SnackbarAction(QWidget* parent) : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        return QSize(2 * an::ui::parts::kSnackbarActionPadding + an::ui::parts::textWidth(actionFont(t), text()),
                     t.metrics.controlSmall);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds(rect());
        const qreal radius = t.radius.sm;
        an::ui::parts::paintStateLayer(painter, bounds, radius, underMouse(), isDown());
        if (m_keyboardFocus)
        {
            an::ui::parts::paintFocusRing(painter, bounds, radius);
        }
        painter.setPen(t.colors.accentOnImage);
        painter.setFont(actionFont(t));
        painter.drawText(bounds, Qt::AlignCenter, text());
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->isAutoRepeat())
        {
            click();
            event->accept();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void focusInEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = an::ui::parts::isKeyboardFocusReason(event->reason());
        QAbstractButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = false;
        QAbstractButton::focusOutEvent(event);
        update();
    }

private:
    bool m_keyboardFocus = false;
};

} // namespace

namespace an::ui
{

AnSnackbar::AnSnackbar(QWidget* parent)
    : QWidget(parent), m_action(new SnackbarAction(this)), m_timer(new QTimer(this)), m_clock([] {
          return std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
      }),
      m_content(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // The painted parts' room, then the action at the end. A layout adds no spacing beside a spacer item, so the room
    // holds the gap before the action; the margins and the room are sized from the tokens when the bar is polished.
    // Its margins and spacing are the bar's geometry, which the style's polish leaves as they are.
    auto* const row = new QHBoxLayout(this);
    row->setProperty("anKeepLayoutMargins", true);
    row->setProperty("anKeepLayoutSpacing", true);
    row->setSpacing(0);
    row->addItem(m_content);
    row->addWidget(m_action, 0, Qt::AlignVCenter);
    setFocusPolicy(Qt::StrongFocus);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { checkLifetime(); });
    connect(m_action, &QAbstractButton::clicked, this, [this] {
        actionTriggered();
        dismiss();
    });
    m_action->hide();
    hide();
}

AnSnackbar::~AnSnackbar() = default;

void AnSnackbar::setText(const QString& text)
{
    m_text = text;
    refreshAccessibleName();
    applyMetrics();
    update();
}

QString AnSnackbar::text() const
{
    return m_text;
}

void AnSnackbar::setDetail(const QString& detail)
{
    m_detail = detail;
    refreshAccessibleName();
    applyMetrics();
    update();
}

QString AnSnackbar::detail() const
{
    return m_detail;
}

void AnSnackbar::setIcon(const QIcon& icon)
{
    m_icon = icon;
    applyMetrics();
    update();
}

QIcon AnSnackbar::icon() const
{
    return m_icon;
}

void AnSnackbar::setActionText(const QString& text)
{
    m_action->setText(text);
    m_action->setVisible(!text.isEmpty());
    applyMetrics();
    update();
}

QString AnSnackbar::actionText() const
{
    return m_action->text();
}

void AnSnackbar::setLifetime(const milliseconds lifetime)
{
    m_lifetime = (std::max)(lifetime, milliseconds{0});
    if (!m_running && !m_paused)
    {
        m_remaining = m_lifetime;
    }
}

milliseconds AnSnackbar::lifetime() const noexcept
{
    return m_lifetime;
}

void AnSnackbar::setClock(Clock clock)
{
    if (clock)
    {
        m_clock = std::move(clock);
    }
}

milliseconds AnSnackbar::now() const
{
    return m_clock();
}

void AnSnackbar::present()
{
    m_remaining = m_lifetime;
    m_paused = false;
    m_running = true;
    m_runningSince = now();
    show();
    raise();
    schedule();
}

void AnSnackbar::dismiss()
{
    if (isHidden())
    {
        return;
    }
    m_timer->stop();
    m_running = false;
    m_paused = false;
    m_remaining = milliseconds{0};
    hide();
    dismissed();
}

bool AnSnackbar::isPaused() const noexcept
{
    return m_paused;
}

milliseconds AnSnackbar::remainingLifetime() const
{
    if (!m_running)
    {
        return m_remaining;
    }
    return (std::max)(milliseconds{0}, m_remaining - (now() - m_runningSince));
}

void AnSnackbar::schedule()
{
    if (!m_running)
    {
        return;
    }
    const milliseconds wait = (std::min)(remainingLifetime(), kCheckInterval);
    m_timer->start(static_cast<int>(wait.count()));
}

void AnSnackbar::checkLifetime()
{
    if (!m_running)
    {
        return;
    }
    if (remainingLifetime() <= milliseconds{0})
    {
        dismiss();
        return;
    }
    schedule();
}

QAbstractButton* AnSnackbar::actionButton() const noexcept
{
    return m_action;
}

QMargins AnSnackbar::shadowMargins() const
{
    return elevationWindowMargins();
}

QRect AnSnackbar::bodyRect() const
{
    return rect().marginsRemoved(shadowMargins());
}

QSize AnSnackbar::sizeHint() const
{
    ensurePolished();
    return QWidget::sizeHint();
}

QSize AnSnackbar::minimumSizeHint() const
{
    return sizeHint();
}

void AnSnackbar::applyMetrics()
{
    const DesignTokens& t = tokens();
    int content = parts::textWidth(textFont(t), m_text);
    if (!m_icon.isNull())
    {
        content += parts::kSnackbarIcon + parts::kSnackbarGap;
    }
    if (!m_detail.isEmpty())
    {
        content += parts::kSnackbarGap + parts::textWidth(detailFont(t), m_detail);
    }
    if (!m_action->text().isEmpty())
    {
        content += parts::kSnackbarGap;
    }
    m_content->changeSize(content, t.metrics.controlLarge, QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QMargins shadow = shadowMargins();
    layout()->setContentsMargins(shadow.left() + parts::kSnackbarPaddingStart, shadow.top(),
                                 shadow.right() + parts::kSnackbarPaddingEnd, shadow.bottom());
    m_action->updateGeometry();
    layout()->invalidate();
    updateGeometry();
}

void AnSnackbar::refreshAccessibleName()
{
    setAccessibleName(m_detail.isEmpty() ? m_text : m_text + QStringLiteral(", ") + m_detail);
}

bool AnSnackbar::event(QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    return QWidget::event(event);
}

void AnSnackbar::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF body(bodyRect());
    const int radius = t.radius.md;
    paintElevation(painter, body, radius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.colors.bgSnack);
    painter.drawRoundedRect(body, radius, radius);

    const QColor text = t.colors.textOnSnack;
    qreal x = body.left() + parts::kSnackbarPaddingStart;
    if (!m_icon.isNull())
    {
        const qreal top = body.center().y() - parts::kSnackbarIcon / 2.0;
        parts::paintIcon(painter, m_icon, QRectF(x, top, parts::kSnackbarIcon, parts::kSnackbarIcon), text);
        x += parts::kSnackbarIcon + parts::kSnackbarGap;
    }
    const QFont font = textFont(t);
    const int textWidth = parts::textWidth(font, m_text);
    painter.setPen(text);
    painter.setFont(font);
    painter.drawText(QRectF(x, body.top(), textWidth, body.height()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
    if (!m_detail.isEmpty())
    {
        x += textWidth + parts::kSnackbarGap;
        const QFont detail = detailFont(t);
        painter.setPen(parts::atShare(text, parts::kSnackbarDetailOpacity));
        painter.setFont(detail);
        painter.drawText(QRectF(x, body.top(), parts::textWidth(detail, m_detail), body.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, m_detail);
    }
}

void AnSnackbar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        dismiss();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AnSnackbar::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    if (m_running)
    {
        m_remaining = remainingLifetime();
        m_running = false;
        m_timer->stop();
    }
    m_paused = !isHidden();
}

void AnSnackbar::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (!m_paused)
    {
        return;
    }
    m_paused = false;
    if (!isHidden())
    {
        m_running = true;
        m_runningSince = now();
        schedule();
    }
}

} // namespace an::ui
