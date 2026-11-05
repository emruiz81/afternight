// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Design token tests (MainDashboard & DashboardOnboardingPanel).
/// Tests verifying that dashboard controls use design tokens for typography
/// and respond to theme changes without crashes.
#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>

#include "Test_MainDashboard/DashboardApplication.h"
#include "controls/DashboardOnboardingPanel.h"
#include "controls/MainDashboard.h"
#include "theme/AnDesignTokens.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

void sendPaletteChange(QWidget* w)
{
    QEvent ev(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(w, &ev);
}

void sendThemeChange(QWidget* w)
{
    QEvent ev(QEvent::ThemeChange);
    QApplication::sendEvent(w, &ev);
}

} // namespace

// ---------------------------------------------------------------------------
// DashboardOnboardingPanel — token integration
// ---------------------------------------------------------------------------

TEST(DashboardThemeTokens, DashboardOnboardingPanel_ConstructionDoesNotCrash)
{
    an::ui::DashboardOnboardingPanel panel(1.0);
    panel.resize(800, 600);
    SUCCEED();
}

TEST(DashboardThemeTokens, DashboardOnboardingPanel_ThemeChangeDoesNotCrash)
{
    an::ui::DashboardOnboardingPanel panel(1.0);
    panel.resize(800, 600);

    sendPaletteChange(&panel);
    sendThemeChange(&panel);
    SUCCEED();
}

TEST(DashboardThemeTokens, DashboardOnboardingPanel_TitleFontUsesTokenHeading1)
{
    an::ui::DashboardOnboardingPanel panel(1.0);
    panel.refreshTheme();

    auto* title = panel.findChild<QLabel*>(QStringLiteral("dashboardOnboardingTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading1);
}

TEST(DashboardThemeTokens, DashboardOnboardingPanel_SubtitleFontUsesTokenBody)
{
    an::ui::DashboardOnboardingPanel panel(1.0);

    auto* subtitle = panel.findChild<QLabel*>(QStringLiteral("dashboardOnboardingSubtitle"));
    ASSERT_NE(subtitle, nullptr);
    EXPECT_EQ(subtitle->font(), an::ui::tokens().typography.body);
}

TEST(DashboardThemeTokens, DashboardOnboardingPanel_RefreshThemeUpdatesLabelFonts)
{
    an::ui::DashboardOnboardingPanel panel(1.0);
    panel.refreshTheme();

    auto* title = panel.findChild<QLabel*>(QStringLiteral("dashboardOnboardingTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->font(), an::ui::tokens().typography.heading1);
}

// ---------------------------------------------------------------------------
// MainDashboard — theme change resilience + font token verification
// ---------------------------------------------------------------------------

TEST(DashboardThemeTokens, MainDashboard_ThemeChangeDoesNotCrash)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);

    sendPaletteChange(&dashboard);
    sendThemeChange(&dashboard);
    SUCCEED();
}

TEST(DashboardThemeTokens, MainDashboard_RecentSearchInputFontUsesTokenBody)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());

    auto* searchInput = dashboard.findChild<QLineEdit*>(QStringLiteral("dashboardRecentSearchInput"));
    ASSERT_NE(searchInput, nullptr);
    EXPECT_EQ(searchInput->font(), an::ui::tokens().typography.body);
}

TEST(DashboardThemeTokens, MainDashboard_RecentSortComboFontUsesTokenBody)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());

    auto* sortCombo = dashboard.findChild<QComboBox*>(QStringLiteral("dashboardRecentSortCombo"));
    ASSERT_NE(sortCombo, nullptr);
    EXPECT_EQ(sortCombo->font(), an::ui::tokens().typography.body);
}
