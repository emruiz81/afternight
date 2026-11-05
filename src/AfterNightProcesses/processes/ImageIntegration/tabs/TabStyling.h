// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "theme/AnDesignTokens.h"

#include <QFrame>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QFont>
#include <QPalette>
#include <QBrush>

#include <algorithm>

namespace ImageIntegration
{
namespace Style
{

enum class BannerTone
{
    Info,
    Warning,
    Error
};

inline QColor tint(const QColor& base, int alpha)
{
    QColor out = base;
    out.setAlpha(std::clamp(alpha, 0, 255));
    return out;
}

inline QColor mutedTextColor()
{
    return an::ui::tokens().colors.textTertiary;
}

inline QColor referenceRowColor()
{
    const auto& c = an::ui::tokens().colors;
    return tint(c.accentPrimary, 56);
}

inline QColor excludedRowColor()
{
    const auto& c = an::ui::tokens().colors;
    return tint(c.statusError, 52);
}

inline QColor failedRowColor()
{
    const auto& c = an::ui::tokens().colors;
    return tint(c.bgCardSecondary, 230);
}

inline QColor flashRowColor()
{
    const auto& c = an::ui::tokens().colors;
    return tint(c.statusWarning, 96);
}

inline void applyBannerStyle(QLabel* label, BannerTone tone, const int radius = 4, const int margin = 4)
{
    if (!label)
    {
        return;
    }

    const auto& c = an::ui::tokens().colors;
    QColor bg = tint(c.statusInfo, 64);
    QColor fg = c.textPrimary;
    QColor border = tint(c.statusInfo, 170);

    if (tone == BannerTone::Warning)
    {
        bg = tint(c.statusWarning, 72);
        border = tint(c.statusWarning, 180);
    }
    else if (tone == BannerTone::Error)
    {
        bg = tint(c.statusError, 72);
        border = tint(c.statusError, 180);
    }

    label->setFrameShape(QFrame::StyledPanel);
    QPalette p = label->palette();
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Mid, border);
    label->setPalette(p);
    label->setContentsMargins(margin, margin, margin, margin);
    label->setProperty("anFrameRadius", radius);
}

inline void applyTableStyle(QTableWidget* table)
{
    if (!table)
        return;

    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);

    // Header styling
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setStretchLastSection(true);

    // Make row height more compact but still DPI/font-safe.
    const int row_height = std::clamp(table->fontMetrics().height() + 8, 20, 26);
    table->verticalHeader()->setDefaultSectionSize(row_height);
    table->verticalHeader()->setMinimumSectionSize(18);

    // Theme-aware palette colours from design tokens.
    const auto& tokenSet = an::ui::tokens();
    const auto& c = tokenSet.colors;
    const QColor gridColor = c.borderSubtle;
    const QColor headerBg = c.bgCardPrimary;

    // Table grid/border via palette Dark role
    QPalette tablePal = table->palette();
    tablePal.setColor(QPalette::Base, c.bgInput);
    tablePal.setColor(QPalette::AlternateBase, c.bgCardSecondary);
    tablePal.setColor(QPalette::Text, c.textPrimary);
    tablePal.setColor(QPalette::WindowText, c.textPrimary);
    tablePal.setColor(QPalette::Highlight, c.accentPrimary);
    tablePal.setColor(QPalette::HighlightedText, c.textOnAccent);
    tablePal.setColor(QPalette::Dark, gridColor);
    table->setPalette(tablePal);

    // Header palette + bold font
    auto* hHeader = table->horizontalHeader();
    QPalette headerPal = hHeader->palette();
    headerPal.setColor(QPalette::Button, headerBg);
    headerPal.setColor(QPalette::ButtonText, c.textPrimary);
    headerPal.setColor(QPalette::WindowText, c.textPrimary);
    headerPal.setColor(QPalette::Dark, gridColor);
    hHeader->setPalette(headerPal);
    hHeader->setFont(tokenSet.typography.heading3);
}

} // namespace Style
} // namespace ImageIntegration
