// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "theme/AnDesignTokens.h"

#include <QGridLayout>
#include <QLayout>
#include <QMargins>
#include <QWidget>

namespace an::ui
{

/// Recursively reparent all widgets in a layout hierarchy to the given parent.
/// Useful when assigning a layout built outside a widget to that widget's content area.
inline void reparentLayoutWidgets(QLayout* layout, QWidget* parent)
{
    if (!layout || !parent)
    {
        return;
    }

    for (int i = 0; i < layout->count(); ++i)
    {
        QLayoutItem* item = layout->itemAt(i);
        if (!item)
        {
            continue;
        }

        if (QWidget* widget = item->widget())
        {
            if (widget->parentWidget() != parent)
            {
                widget->setParent(parent);
            }
        }

        if (QLayout* childLayout = item->layout())
        {
            reparentLayoutWidgets(childLayout, parent);
        }
    }
}

/// Maps common hardcoded spacing values to the nearest design token value.
/// Sizes a layout keeps whatever the spacing tokens are: under the Observatory profile, the density's gap and a panel's
/// padding, which a widget sized from the metric tokens sets. -1 keeps nothing.
struct KeptSpacings
{
    int gap = -1;
    int pad = -1;
};

[[nodiscard]] inline int normalizeSpacingValueToTokens(const int value, const SpacingTokens& spacing,
                                                       const KeptSpacings kept = {})
{
    if (value >= 0 && (value == kept.gap || value == kept.pad))
    {
        return value;
    }
    if (value <= 0)
    {
        return value;
    }

    switch (value)
    {
    case 4:
        return spacing.xs;
    case 6:
    case 7:
    case 8:
    case 9:
        return spacing.sm;
    case 10:
    case 11:
    case 12:
    case 13:
        return spacing.md;
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
        return spacing.lg;
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
        return spacing.xl;
    default:
        return value;
    }
}

[[nodiscard]] inline QMargins normalizeMarginsToTokens(const QMargins& margins, const SpacingTokens& spacing,
                                                       const KeptSpacings kept = {})
{
    return QMargins(normalizeSpacingValueToTokens(margins.left(), spacing, kept),
                    normalizeSpacingValueToTokens(margins.top(), spacing, kept),
                    normalizeSpacingValueToTokens(margins.right(), spacing, kept),
                    normalizeSpacingValueToTokens(margins.bottom(), spacing, kept));
}

/// Recursively normalizes layout spacing/margins to design token spacing values.
inline void normalizeLayoutSpacingToTokens(QLayout* layout, const SpacingTokens& spacing, const KeptSpacings kept = {})
{
    if (!layout)
    {
        return;
    }

    const bool keepSpacing = layout->property("anKeepLayoutSpacing").toBool();
    const bool keepMargins =
        layout->property("anKeepLayoutMargins").toBool() || layout->property("anKeepPreviewLayoutMargins").toBool();

    if (!keepSpacing)
    {
        if (const int currentSpacing = layout->spacing(); currentSpacing >= 0)
        {
            const int normalizedSpacing = normalizeSpacingValueToTokens(currentSpacing, spacing, kept);
            if (normalizedSpacing != currentSpacing)
            {
                layout->setSpacing(normalizedSpacing);
            }
        }

        if (auto* gridLayout = qobject_cast<QGridLayout*>(layout))
        {
            if (const int currentHorizontal = gridLayout->horizontalSpacing(); currentHorizontal >= 0)
            {
                const int normalizedHorizontal = normalizeSpacingValueToTokens(currentHorizontal, spacing, kept);
                if (normalizedHorizontal != currentHorizontal)
                {
                    gridLayout->setHorizontalSpacing(normalizedHorizontal);
                }
            }

            if (const int currentVertical = gridLayout->verticalSpacing(); currentVertical >= 0)
            {
                const int normalizedVertical = normalizeSpacingValueToTokens(currentVertical, spacing, kept);
                if (normalizedVertical != currentVertical)
                {
                    gridLayout->setVerticalSpacing(normalizedVertical);
                }
            }
        }
    }

    if (!keepMargins)
    {
        const QMargins currentMargins = layout->contentsMargins();
        const QMargins normalizedMargins = normalizeMarginsToTokens(currentMargins, spacing, kept);
        if (normalizedMargins != currentMargins)
        {
            layout->setContentsMargins(normalizedMargins);
        }
    }

    for (int i = 0; i < layout->count(); ++i)
    {
        if (QLayoutItem* item = layout->itemAt(i))
        {
            if (QLayout* childLayout = item->layout())
            {
                normalizeLayoutSpacingToTokens(childLayout, spacing, kept);
            }
        }
    }
}

} // namespace an::ui
