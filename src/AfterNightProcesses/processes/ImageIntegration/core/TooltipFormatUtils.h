// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>
#include <QStringList>

#include <initializer_list>

namespace an::ui::imageintegration
{

/// Build a multi-section tooltip with an optional bold title and body
/// sections separated by blank lines.
inline QString formatTooltip(const QString& title, std::initializer_list<QString> sections)
{
    QStringList blocks;
    const QString trimmedTitle = title.trimmed();
    if (!trimmedTitle.isEmpty())
    {
        blocks << trimmedTitle;
    }

    for (const QString& section : sections)
    {
        const QString trimmedSection = section.trimmed();
        if (!trimmedSection.isEmpty())
        {
            blocks << trimmedSection;
        }
    }

    return blocks.join(QStringLiteral("\n\n"));
}

/// Format items as a bullet list ("- item\n- item\n…").
inline QString tooltipBullets(std::initializer_list<QString> items)
{
    QStringList lines;
    for (const QString& item : items)
    {
        const QString trimmedItem = item.trimmed();
        if (!trimmedItem.isEmpty())
        {
            lines << QStringLiteral("- %1").arg(trimmedItem);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

/// Format items as a numbered list ("1. item\n2. item\n…").
inline QString tooltipNumbered(std::initializer_list<QString> items)
{
    QStringList lines;
    int index = 1;
    for (const QString& item : items)
    {
        const QString trimmedItem = item.trimmed();
        if (!trimmedItem.isEmpty())
        {
            lines << QStringLiteral("%1. %2").arg(index++).arg(trimmedItem);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

/// Apply the same tooltip text to every widget in the pack.
template <typename... Widgets> void setSharedToolTip(const QString& tooltip, Widgets*... widgets)
{
    (
        [&]() {
            if (widgets)
            {
                widgets->setToolTip(tooltip);
            }
        }(),
        ...);
}

} // namespace an::ui::imageintegration
