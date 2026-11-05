// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnStyledTable.h"

#include "theme/AnDesignTokens.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QPalette>

namespace an::ui
{

namespace
{

QColor resolvedSelectionTextColor(const QPalette& palette, const DesignTokens& tokenSet)
{
    QColor selectionFill = tokenSet.colors.accentPrimary;
    selectionFill.setAlpha(an::ui::isDarkTheme() ? 116 : 84);

    const QColor baseColor =
        palette.color(QPalette::Base).isValid() ? palette.color(QPalette::Base) : palette.color(QPalette::Window);
    const QColor effectiveSelectionBg = an::ui::compositeOver(selectionFill, baseColor);

    return an::ui::bestContrastTextColor(effectiveSelectionBg, tokenSet.colors.textOnAccent, QColor(255, 255, 255),
                                         QColor(0, 0, 0), 4.5);
}

} // namespace

AnStyledTable::AnStyledTable(QWidget* parent) : QTableWidget(parent)
{
    setObjectName(QStringLiteral("anStyledTable"));
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true);
    horizontalHeader()->setStretchLastSection(true);
    verticalHeader()->setVisible(false);

    refreshTheme();
}

void AnStyledTable::refreshTheme()
{
    refreshTokens();
    const auto& tokenSet = tokens();

    // Table colors via palette
    QPalette pal = palette();
    pal.setColor(QPalette::Base, tokenSet.colors.bgInput);
    pal.setColor(QPalette::AlternateBase, tokenSet.colors.bgSurface);
    pal.setColor(QPalette::Text, tokenSet.colors.textPrimary);
    QColor selectionFill = tokenSet.colors.accentPrimary;
    selectionFill.setAlpha(an::ui::isDarkTheme() ? 116 : 84);
    pal.setColor(QPalette::Highlight, selectionFill);

    const QColor selectedTextColor = resolvedSelectionTextColor(pal, tokenSet);
    pal.setColor(QPalette::HighlightedText, selectedTextColor);
    pal.setColor(QPalette::Active, QPalette::HighlightedText, selectedTextColor);
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, selectedTextColor);
    setPalette(pal);

    // Header styling via palette + font
    QHeaderView* hHeader = horizontalHeader();
    if (hHeader)
    {
        QPalette headerPal = hHeader->palette();
        headerPal.setColor(QPalette::Window, tokenSet.colors.bgCardPrimary);
        headerPal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        hHeader->setPalette(headerPal);

        QFont headerFont = hHeader->font();
        headerFont.setWeight(QFont::DemiBold);
        hHeader->setFont(headerFont);
    }

    // Re-polish so the proxy style paints properly
    style()->unpolish(this);
    style()->polish(this);
    update();
}

} // namespace an::ui
