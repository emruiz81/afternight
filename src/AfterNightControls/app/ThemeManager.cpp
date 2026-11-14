// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ThemeManager.h"

#include "app/ModernizationFeatureFlags.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/MainDashboard.h"
#include "controls/NewSequenceControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "theme/KddwProxyStyle.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/MainWindow.h>

#include <QApplication>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <AfterNightPlatform/Logging.h>

namespace an::ui
{

namespace
{

ThemePreference themePreferenceFromColorScheme(const Qt::ColorScheme scheme)
{
    if (scheme == Qt::ColorScheme::Dark)
    {
        return ThemePreference::Dark;
    }

    if (scheme == Qt::ColorScheme::Light)
    {
        return ThemePreference::Light;
    }

    return ThemePreference::System;
}

#if defined(Q_OS_WIN)
using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

constexpr DWORD kDwmWaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWaSystemBackdropType = 38;
constexpr int kDwmSystemBackdropNone = 1;
constexpr int kDwmSystemBackdropMainWindow = 2;

DwmSetWindowAttributeFn resolveDwmSetWindowAttribute()
{
    static const DwmSetWindowAttributeFn fn = []() -> DwmSetWindowAttributeFn {
        const HMODULE module = ::LoadLibraryW(L"dwmapi.dll");
        if (!module)
        {
            return nullptr;
        }

        const FARPROC proc = ::GetProcAddress(module, "DwmSetWindowAttribute");
        return reinterpret_cast<DwmSetWindowAttributeFn>(proc);
    }();
    return fn;
}

void applyNativeBackdropToWidget(QWidget* widget, const bool darkMode, const bool useNativeBackdrop)
{
    if (!widget)
    {
        return;
    }

    const auto dwmSetWindowAttribute = resolveDwmSetWindowAttribute();
    if (!dwmSetWindowAttribute)
    {
        return;
    }

    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
    {
        return;
    }

    const BOOL immersiveDarkMode = darkMode ? TRUE : FALSE;
    (void)dwmSetWindowAttribute(hwnd, kDwmWaUseImmersiveDarkMode, &immersiveDarkMode, sizeof(immersiveDarkMode));

    const int backdropType = useNativeBackdrop ? kDwmSystemBackdropMainWindow : kDwmSystemBackdropNone;
    (void)dwmSetWindowAttribute(hwnd, kDwmWaSystemBackdropType, &backdropType, sizeof(backdropType));
}
#endif

void refreshNativeBackdrops(QWidget* mainWindow, const bool darkMode)
{
#if defined(Q_OS_WIN)
    const bool useNativeBackdrop = an::ui::usingNativeBackdropMaterials() && an::ui::nativeBackdropSupported();

    for (QWidget* topLevel : QApplication::topLevelWidgets())
    {
        if (!topLevel || !topLevel->isWindow())
        {
            continue;
        }

        const Qt::WindowType windowType = topLevel->windowType();
        if (windowType == Qt::ToolTip || windowType == Qt::Popup || windowType == Qt::Tool ||
            windowType == Qt::SplashScreen)
        {
            continue;
        }

        applyNativeBackdropToWidget(topLevel, darkMode, useNativeBackdrop);
    }

    if (mainWindow)
    {
        applyNativeBackdropToWidget(mainWindow, darkMode, useNativeBackdrop);
    }
#else
    Q_UNUSED(mainWindow);
    Q_UNUSED(darkMode);
#endif
}

} // anonymous namespace

ThemeManager::ThemeManager(const Dependencies& deps) : m_deps(deps) {}

void ThemeManager::refreshTheme() const
{
    if (!m_deps.darkMode)
    {
        return;
    }

    an::ui::refreshTokens();
    *m_deps.darkMode = an::ui::isDarkTheme();
}

void ThemeManager::loadIcons() const
{
    if (!m_deps.reloadIcons)
    {
        return;
    }

    const bool darkMode = m_deps.darkMode ? *m_deps.darkMode : false;
    m_deps.reloadIcons(buildThemeIconRefreshRequest(darkMode));
}

void ThemeManager::applyTheme(const QString& themeId) const
{
    const QString normalizedThemeId = themeId.trimmed().toLower();
    LOG_DEBUG("Applying theme: {}", normalizedThemeId.toStdString());

    const Qt::ColorScheme targetScheme = resolveThemeColorScheme(normalizedThemeId);
    setThemePreference(themePreferenceFromColorScheme(targetScheme));

#if (QT_VERSION >= QT_VERSION_CHECK(6, 8, 0))
    QGuiApplication::styleHints()->setColorScheme(targetScheme);
#else
    Q_UNUSED(targetScheme);
    LOG_WARNING("Theme switching requires Qt 6.8.0 or later. Current Qt version: {}", QT_VERSION_STR);
#endif

    QCoreApplication::processEvents();
    refreshTheme();

    const bool darkMode = m_deps.darkMode ? *m_deps.darkMode : false;

    // Refresh the proxy style theme tokens (replaces QSS-based theming).
    // This re-polishes all widgets and triggers repaint with new token colors.
    if (auto* proxyStyle = installedAnProxyStyle())
    {
        proxyStyle->refreshThemeTokens();
    }

    // Refresh the KDDW proxy style so dock widgets pick up the new tokens.
    if (auto* kddwStyle = installedKddwProxyStyle())
    {
        kddwStyle->refreshThemeTokens();
    }

    // Clear the KDDW ViewFactory icon cache so title bar / corner buttons
    // regenerate with updated token colours on next paint.
    if (auto* viewFactory = dynamic_cast<AN_ViewFactory*>(KDDockWidgets::Config::self().viewFactory()))
    {
        viewFactory->clearIconCache();
    }

    loadIcons();

    if (m_deps.setActionIcons)
    {
        m_deps.setActionIcons();
    }

    refreshCascade();
    refreshNativeBackdrops(m_deps.mainWindow, darkMode);

    LOG_INFO("Theme applied: {} (dark mode: {})", normalizedThemeId.toStdString(), darkMode);
}

void ThemeManager::refreshCascade() const
{
    if (m_deps.mainDashboard && *m_deps.mainDashboard)
    {
        (*m_deps.mainDashboard)->refreshIcons();
    }

    if (m_deps.toolbarLockLayout && *m_deps.toolbarLockLayout && m_deps.iconProvider)
    {
        if (m_deps.isLayoutLocked && m_deps.isLayoutLocked())
        {
            (*m_deps.toolbarLockLayout)->setIcon(m_deps.iconProvider("unlock"));
        }
        else
        {
            (*m_deps.toolbarLockLayout)->setIcon(m_deps.iconProvider("lock"));
        }
    }

    if (m_deps.imageViewControlManager)
    {
        for (auto* viewControl : m_deps.imageViewControlManager->getListOfImageViewControls())
        {
            if (viewControl)
            {
                viewControl->refreshTheme();
            }
        }
    }

    if (m_deps.dockMainWindow && *m_deps.dockMainWindow)
    {
        for (auto* tabBar : (*m_deps.dockMainWindow)->findChildren<AN_TabBar*>())
        {
            tabBar->applyThemeColors();
        }

        for (auto* stack : (*m_deps.dockMainWindow)->findChildren<AN_Stack*>())
        {
            stack->applyThemeColors();
            stack->styleCornerButtons();
        }
    }

    if (m_deps.forEachOpenProcess)
    {
        m_deps.forEachOpenProcess([](TopLevelProcessBaseControl* process) { process->refreshTheme(); });
    }

    if (m_deps.mainWindow)
    {
        for (auto* histogramControl : m_deps.mainWindow->findChildren<ImageHistogramControl*>())
        {
            histogramControl->refreshIcons();
        }

        for (auto* newSeqControl : m_deps.mainWindow->findChildren<NewSequenceControl*>())
        {
            newSeqControl->refreshTheme();
        }
    }
}

} // namespace an::ui
