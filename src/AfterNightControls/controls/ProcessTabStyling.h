// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QTabWidget>

namespace an::ui::processes
{

inline void applyStandardProcessTabBarSizing(QTabWidget* tabWidget)
{
    if (!tabWidget)
    {
        return;
    }

    if (auto* tabBar = tabWidget->tabBar())
    {
        tabWidget->setDocumentMode(true);
        tabBar->setExpanding(false);
        tabBar->setElideMode(Qt::ElideNone);
        tabBar->setUsesScrollButtons(false);
        tabBar->setDrawBase(false);
        tabBar->setProperty("anTabCompact", true);
        tabBar->setProperty("anTabHeightScale", 0.85);
        tabBar->setProperty("anTabMinWidth", 170);
        tabBar->setProperty("anTabMinHeight", 18);
        tabBar->setProperty("anTabAccentLinePosition", QStringLiteral("top"));
    }
}

} // namespace an::ui::processes
