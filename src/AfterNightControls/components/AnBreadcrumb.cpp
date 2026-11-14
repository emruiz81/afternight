// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnBreadcrumb.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnBreadcrumb)

namespace
{

/// How few average characters a part is elided down to: the item, then every other part.
constexpr int kItemFloorCharacters = 12;
constexpr int kPlaceFloorCharacters = 4;

/// What separates the parts in the whole path, the tooltip and what assistive technology reads.
const char* const kSeparator = " > ";

QFont partFont(const an::ui::DesignTokens& t, const bool last)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontSmall, last ? QFont::DemiBold : QFont::Normal);
}

/// The room between two parts: the chevron with a gap at each side.
constexpr int separatorWidth()
{
    return 2 * an::ui::parts::kBreadcrumbGap + an::ui::parts::kBreadcrumbChevron;
}

int floorOf(const QFont& font, const QString& text, const int characters)
{
    const int floor = static_cast<int>(std::ceil(QFontMetricsF(font).averageCharWidth() * characters));
    return (std::min)(an::ui::parts::textWidth(font, text), floor);
}

} // namespace

namespace an::ui
{

AnBreadcrumb::AnBreadcrumb(QWidget* parent) : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void AnBreadcrumb::setParts(const QStringList& parts)
{
    m_parts = parts;
    setText(parts.join(QLatin1String(kSeparator)));
    updateGeometry();
    refreshToolTip();
    update();
}

QStringList AnBreadcrumb::parts() const
{
    return m_parts;
}

std::vector<AnBreadcrumb::ShownPart> AnBreadcrumb::layoutAt(const int width) const
{
    const DesignTokens& t = tokens();
    const int count = static_cast<int>(m_parts.size());
    std::vector<ShownPart> shown;
    if (count == 0)
    {
        return shown;
    }
    std::vector<int> full(static_cast<std::size_t>(count));
    std::vector<int> floors(static_cast<std::size_t>(count));
    int othersFull = 0;
    int othersFloor = 0;
    for (int index = 0; index < count; ++index)
    {
        const bool last = index + 1 == count;
        const QFont font = partFont(t, last);
        const auto at = static_cast<std::size_t>(index);
        full[at] = parts::textWidth(font, m_parts.at(index));
        floors[at] = floorOf(font, m_parts.at(index), last ? kItemFloorCharacters : kPlaceFloorCharacters);
        if (!last)
        {
            othersFull += full[at];
            othersFloor += floors[at];
        }
    }
    const auto lastAt = static_cast<std::size_t>(count - 1);
    const int available = width - (count - 1) * separatorWidth();
    std::vector<int> widths = full;
    if (othersFull + full[lastAt] > available)
    {
        // The item gives way first, down to its floor; then the others share what is still missing.
        widths[lastAt] = (std::max)(floors[lastAt], available - othersFull);
        const int missing = othersFull + widths[lastAt] - available;
        const int slack = othersFull - othersFloor;
        if (missing > 0 && slack > 0)
        {
            const int taken = (std::min)(missing, slack);
            int given = 0;
            for (std::size_t at = 0; at < lastAt; ++at)
            {
                const int share = static_cast<int>(
                    std::ceil(static_cast<double>(taken) * (full[at] - floors[at]) / static_cast<double>(slack)));
                const int cut = (std::min)(share, taken - given);
                widths[at] = full[at] - cut;
                given += cut;
            }
        }
    }

    qreal x = 0;
    for (int index = 0; index < count; ++index)
    {
        const auto at = static_cast<std::size_t>(index);
        const QFont font = partFont(t, index + 1 == count);
        const QString text = widths[at] >= full[at]
                                 ? m_parts.at(index)
                                 : QFontMetricsF(font).elidedText(m_parts.at(index), Qt::ElideMiddle, widths[at]);
        shown.push_back(ShownPart{QRectF(x, 0, widths[at], height()), text});
        x += widths[at] + separatorWidth();
    }
    return shown;
}

QStringList AnBreadcrumb::shownParts() const
{
    QStringList texts;
    for (const ShownPart& part : layoutAt(width()))
    {
        texts.append(part.text);
    }
    return texts;
}

bool AnBreadcrumb::isElided() const
{
    return shownParts() != m_parts;
}

QSize AnBreadcrumb::sizeHint() const
{
    const DesignTokens& t = tokens();
    int width = 0;
    for (int index = 0; index < m_parts.size(); ++index)
    {
        width += parts::textWidth(partFont(t, index + 1 == m_parts.size()), m_parts.at(index));
    }
    width += (std::max)(0, static_cast<int>(m_parts.size()) - 1) * separatorWidth();
    return QSize(width, t.metrics.controlXSmall);
}

QSize AnBreadcrumb::minimumSizeHint() const
{
    const DesignTokens& t = tokens();
    int width = 0;
    for (int index = 0; index < m_parts.size(); ++index)
    {
        const bool last = index + 1 == m_parts.size();
        width += floorOf(partFont(t, last), m_parts.at(index), last ? kItemFloorCharacters : kPlaceFloorCharacters);
    }
    width += (std::max)(0, static_cast<int>(m_parts.size()) - 1) * separatorWidth();
    return QSize(width, t.metrics.controlXSmall);
}

void AnBreadcrumb::refreshToolTip()
{
    setToolTip(isElided() ? text() : QString());
}

void AnBreadcrumb::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    refreshToolTip();
}

void AnBreadcrumb::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const std::vector<ShownPart> shown = layoutAt(width());
    for (std::size_t index = 0; index < shown.size(); ++index)
    {
        const bool last = index + 1 == shown.size();
        painter.setFont(partFont(t, last));
        painter.setPen(last ? t.colors.textPrimary : t.colors.textSecondary);
        painter.drawText(shown[index].rect, Qt::AlignLeft | Qt::AlignVCenter, shown[index].text);
        if (!last)
        {
            const qreal left = shown[index].rect.right() + parts::kBreadcrumbGap;
            const qreal top = (height() - parts::kBreadcrumbChevron) / 2.0;
            parts::paintChevronRight(painter, QRectF(left, top, parts::kBreadcrumbChevron, parts::kBreadcrumbChevron),
                                     t.colors.textTertiary, parts::kGlyphStroke);
        }
    }
}

int AnBreadcrumb::partAt(const QPointF& position) const
{
    const std::vector<ShownPart> shown = layoutAt(width());
    for (std::size_t index = 0; index < shown.size(); ++index)
    {
        if (shown[index].rect.contains(position))
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void AnBreadcrumb::mousePressEvent(QMouseEvent* event)
{
    m_pressed = event->button() == Qt::LeftButton ? partAt(event->position()) : -1;
    QLabel::mousePressEvent(event);
}

void AnBreadcrumb::mouseReleaseEvent(QMouseEvent* event)
{
    const int part = event->button() == Qt::LeftButton ? partAt(event->position()) : -1;
    const int pressed = m_pressed;
    m_pressed = -1;
    QLabel::mouseReleaseEvent(event);
    if (part >= 0 && part == pressed)
    {
        partActivated(part);
    }
}

} // namespace an::ui
