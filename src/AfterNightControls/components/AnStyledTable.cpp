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

/// How much of the state colour an alternate row takes.
constexpr float kAlternateShare = 0.04f;

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

    // The rows on the first surface, every other one a breath lighter; the selection the accent's soft share under the
    // primary text; the header the third surface in the secondary text, in the style's small capitals.
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
}

} // namespace an::ui
