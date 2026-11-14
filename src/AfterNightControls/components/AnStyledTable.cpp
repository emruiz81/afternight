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

/// How much of the state colour an alternate row takes under the Observatory profile.
constexpr float kAlternateShare = 0.04f;
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

    if (styleProfile() == StyleProfile::Observatory)
    {
        // The rows on the first surface, every other one a breath lighter; the selection the accent's soft share under
        // the primary text; the header the third surface in the secondary text, in the style's small capitals.
        QPalette pal = palette();
        pal.setColor(QPalette::Base, tokenSet.layers.surface1);
        QColor alternate = tokenSet.colors.stateLayer;
        alternate.setAlphaF(kAlternateShare);
        pal.setColor(QPalette::AlternateBase, compositeOver(alternate, tokenSet.layers.surface1));
        pal.setColor(QPalette::Text, tokenSet.colors.textPrimary);
        pal.setColor(QPalette::Highlight, tokenSet.colors.accentSoft);
        pal.setColor(QPalette::HighlightedText, tokenSet.colors.textPrimary);
        setPalette(pal);
        if (QHeaderView* header = horizontalHeader())
        {
            QPalette headerPal = header->palette();
            headerPal.setColor(QPalette::Window, tokenSet.layers.surface3);
            headerPal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
            header->setPalette(headerPal);
        }
        style()->unpolish(this);
        style()->polish(this);
        update();
        return;
    }

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
