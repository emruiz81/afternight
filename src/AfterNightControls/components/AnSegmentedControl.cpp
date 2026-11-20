// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnSegmentedControl.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/PainterGuard.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSizePolicy>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnSegmentedControl)

namespace
{

/// A segment: a checkable button that takes no focus and paints nothing - the control is the keyboard's one stop and
/// paints every segment - so that the pointer and assistive technology meet a radio button for each.
class SegmentButton final : public QAbstractButton
{
public:
    SegmentButton(const QString& text, an::ui::AnSegmentedControl* control)
        : QAbstractButton(control), m_control(control)
    {
        setText(text);
        setCheckable(true);
        setAutoExclusive(true);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    /// The width every segment takes, inside the control's outline.
    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(m_control->segmentWidth(), m_control->sizeHint().height() - 2);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {}

private:
    an::ui::AnSegmentedControl* m_control;
};

using ControlHeight = an::ui::AnSegmentedControl::ControlHeight;

int heightOf(const an::ui::DesignTokens& t, const ControlHeight height)
{
    switch (height)
    {
    case ControlHeight::Small:
        return t.metrics.controlSmall;
    case ControlHeight::ExtraSmall:
        return t.metrics.controlXSmall;
    case ControlHeight::Regular:
    default:
        return t.metrics.control;
    }
}

QFont segmentFont(const an::ui::DesignTokens& t, const ControlHeight height)
{
    return an::ui::parts::sizedFont(t.typography.body,
                                    height == ControlHeight::ExtraSmall ? t.metrics.fontCaption : t.metrics.fontSmall,
                                    QFont::DemiBold);
}

/// The outer corners: the controls' radius, the small parts' in the smallest form.
qreal radiusOf(const an::ui::DesignTokens& t, const ControlHeight height)
{
    return height == ControlHeight::ExtraSmall ? t.radius.sm : t.radius.md;
}

} // namespace

namespace an::ui
{

AnSegmentedControl::AnSegmentedControl(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // The segments side by side inside the outline, a hairline between two.
    // Its margins and spacing are the outline and the hairlines, which the style's polish leaves as they are.
    auto* const row = new QHBoxLayout(this);
    row->setProperty("anKeepLayoutMargins", true);
    row->setProperty("anKeepLayoutSpacing", true);
    row->setContentsMargins(1, 1, 1, 1);
    row->setSpacing(1);
}

AnSegmentedControl::AnSegmentedControl(const QStringList& segments, QWidget* parent) : AnSegmentedControl(parent)
{
    setSegments(segments);
}

AnSegmentedControl::~AnSegmentedControl() = default;

int AnSegmentedControl::addSegment(const QString& text)
{
    auto* const segment = new SegmentButton(text, this);
    const int index = static_cast<int>(m_segments.size());
    segment->installEventFilter(this);
    connect(segment, &QAbstractButton::clicked, this, [this, index] { choose(index); });
    m_segments.push_back(segment);
    layout()->addWidget(segment);
    refreshSegments();
    update();
    return index;
}

void AnSegmentedControl::setSegments(const QStringList& segments)
{
    for (QAbstractButton* const segment : m_segments)
    {
        delete segment;
    }
    m_segments.clear();
    const bool hadChoice = m_current >= 0;
    m_current = -1;
    m_focused = 0;
    for (const QString& text : segments)
    {
        addSegment(text);
    }
    if (hadChoice)
    {
        currentIndexChanged(-1);
    }
}

int AnSegmentedControl::count() const
{
    return static_cast<int>(m_segments.size());
}

QString AnSegmentedControl::segmentText(const int index) const
{
    return index >= 0 && index < count() ? m_segments[static_cast<std::size_t>(index)]->text() : QString();
}

void AnSegmentedControl::setCurrentIndex(const int index)
{
    const int chosen = index >= 0 && index < count() ? index : -1;
    if (chosen == m_current)
    {
        return;
    }
    if (chosen < 0)
    {
        // A button of an exclusive set cannot be unchecked while it is the checked one.
        QAbstractButton* const checked = m_segments[static_cast<std::size_t>(m_current)];
        checked->setAutoExclusive(false);
        checked->setChecked(false);
        checked->setAutoExclusive(true);
    }
    else
    {
        m_segments[static_cast<std::size_t>(chosen)]->setChecked(true);
        m_focused = chosen;
    }
    m_current = chosen;
    update();
    currentIndexChanged(m_current);
}

int AnSegmentedControl::currentIndex() const noexcept
{
    return m_current;
}

int AnSegmentedControl::focusedSegment() const noexcept
{
    return m_focused;
}

void AnSegmentedControl::setControlHeight(const ControlHeight height)
{
    if (m_height == height)
    {
        return;
    }
    m_height = height;
    refreshSegments();
    update();
}

AnSegmentedControl::ControlHeight AnSegmentedControl::controlHeight() const noexcept
{
    return m_height;
}

QAbstractButton* AnSegmentedControl::segmentButton(const int index) const
{
    return index >= 0 && index < count() ? m_segments[static_cast<std::size_t>(index)] : nullptr;
}

int AnSegmentedControl::segmentWidth() const
{
    const DesignTokens& t = tokens();
    const QFont font = segmentFont(t, m_height);
    int widest = 0;
    for (const QAbstractButton* const segment : m_segments)
    {
        widest = (std::max)(widest, parts::textWidth(font, segment->text()));
    }
    if (m_height == ControlHeight::ExtraSmall)
    {
        return 2 * parts::kSegmentPaddingSmall + widest;
    }
    return 2 * parts::kSegmentPadding + parts::kSegmentCheck + parts::kSegmentCheckGap + widest;
}

QSize AnSegmentedControl::sizeHint() const
{
    const DesignTokens& t = tokens();
    const int segments = count();
    // The segments, a hairline between two, and the outline at each side.
    const int width = segments > 0 ? segments * segmentWidth() + (segments - 1) + 2 : 2;
    return QSize(width, heightOf(t, m_height));
}

QSize AnSegmentedControl::minimumSizeHint() const
{
    return sizeHint();
}

void AnSegmentedControl::refreshSegments()
{
    // Each segment asks the control for its size: the layout is told it may have changed.
    for (QAbstractButton* const segment : m_segments)
    {
        segment->updateGeometry();
    }
    layout()->invalidate();
    updateGeometry();
}

bool AnSegmentedControl::event(QEvent* event)
{
    if (event->type() == QEvent::StyleChange)
    {
        refreshSegments();
    }
    return QWidget::event(event);
}

void AnSegmentedControl::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }
    const QRectF bounds(rect());
    const qreal radius = radiusOf(t, m_height);
    QPainterPath shape;
    shape.addRoundedRect(bounds, radius, radius);

    const QFont font = segmentFont(t, m_height);
    const bool withCheck = m_height != ControlHeight::ExtraSmall;
    for (std::size_t index = 0; index < m_segments.size(); ++index)
    {
        const QAbstractButton* const segment = m_segments[index];
        const QRectF area =
            QRectF(segment->geometry()).adjusted(index == 0 ? -1 : 0, -1, index + 1 == m_segments.size() ? 1 : 0, 1);
        const bool chosen = static_cast<int>(index) == m_current;
        {
            const PainterGuard guard(&painter);
            painter.setClipPath(shape);
            if (chosen)
            {
                painter.fillRect(area, t.colors.accentContainer);
            }
            if (isEnabled() && segment->isEnabled() && (segment->underMouse() || segment->isDown()))
            {
                painter.fillRect(area, parts::atShare(t.colors.stateLayer,
                                                      segment->isDown() ? parts::kPressedLayer : parts::kHoverLayer));
            }
        }

        const QColor text = chosen ? t.colors.textOnAccentContainer : t.colors.textPrimary;
        // A segment that cannot be chosen now is drawn as a disabled control is.
        const PainterGuard dimmed(&painter);
        if (!segment->isEnabled())
        {
            painter.setOpacity(painter.opacity() * parts::kDisabledOpacity);
        }
        const int textWidth = parts::textWidth(font, segment->text());
        const int content = textWidth + (withCheck && chosen ? parts::kSegmentCheck + parts::kSegmentCheckGap : 0);
        qreal x = area.left() + (area.width() - content) / 2.0;
        if (withCheck && chosen)
        {
            const qreal top = area.center().y() - parts::kSegmentCheck / 2.0;
            parts::paintCheckMark(painter, QRectF(x, top, parts::kSegmentCheck, parts::kSegmentCheck), text,
                                  parts::kGlyphStroke);
            x += parts::kSegmentCheck + parts::kSegmentCheckGap;
        }
        painter.setPen(text);
        painter.setFont(font);
        painter.drawText(QRectF(x, area.top(), textWidth, area.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         segment->text());
    }

    // The outline around them all and a hairline between two.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(t.colors.borderPrimary, 1.0));
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    for (std::size_t index = 1; index < m_segments.size(); ++index)
    {
        const qreal x = m_segments[index]->geometry().left() - 0.5;
        painter.drawLine(QPointF(x, 1.0), QPointF(x, height() - 1.0));
    }

    if (m_keyboardFocus && isEnabled() && m_focused >= 0 && m_focused < count())
    {
        const QRectF focused =
            QRectF(m_segments[static_cast<std::size_t>(m_focused)]->geometry()).adjusted(-1, -1, 1, 1);
        const PainterGuard guard(&painter);
        painter.setClipPath(shape);
        parts::paintFocusRing(painter, focused, radius);
    }
}

void AnSegmentedControl::keyPressEvent(QKeyEvent* event)
{
    const int last = count() - 1;
    switch (event->key())
    {
    case Qt::Key_Left:
        m_focused = enabledSegmentFrom(m_focused - 1, -1, m_focused);
        break;
    case Qt::Key_Right:
        m_focused = enabledSegmentFrom(m_focused + 1, 1, m_focused);
        break;
    case Qt::Key_Home:
        m_focused = enabledSegmentFrom(0, 1, m_focused);
        break;
    case Qt::Key_End:
        m_focused = enabledSegmentFrom(last, -1, m_focused);
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!event->isAutoRepeat())
        {
            choose(m_focused);
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
    update();
}

void AnSegmentedControl::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    if (m_current >= 0)
    {
        m_focused = m_current;
    }
    QWidget::focusInEvent(event);
    update();
}

void AnSegmentedControl::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QWidget::focusOutEvent(event);
    update();
}

bool AnSegmentedControl::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        update();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

int AnSegmentedControl::enabledSegmentFrom(const int from, const int step, const int otherwise) const
{
    for (int index = from; index >= 0 && index < count(); index += step)
    {
        if (m_segments[static_cast<std::size_t>(index)]->isEnabled())
        {
            return index;
        }
    }
    return otherwise;
}

void AnSegmentedControl::choose(const int index)
{
    if (index < 0 || index >= count() || !m_segments[static_cast<std::size_t>(index)]->isEnabled())
    {
        return;
    }
    m_focused = index;
    setCurrentIndex(index);
}

} // namespace an::ui
