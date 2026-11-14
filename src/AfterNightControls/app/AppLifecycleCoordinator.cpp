// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/AppLifecycleCoordinator.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "app/ThemeManager.h"
#include "theme/AnimationUtils.h"

#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>

#include <thread>

namespace an::ui
{

AppLifecycleCoordinator::AppLifecycleCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void AppLifecycleCoordinator::initialize() const
{
    if (m_dependencies.logModernizationFeatureFlags)
    {
        m_dependencies.logModernizationFeatureFlags();
    }

    if (m_dependencies.initializePersistence)
    {
        m_dependencies.initializePersistence();
    }

    GlobalSettingsPersistence* globalSettings =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    if (shouldCleanTempFilesOnStartup() && m_dependencies.cleanTemporaryFilesAsync)
    {
        m_dependencies.cleanTemporaryFilesAsync();
    }

    // Scratch stranded outside the managed temp directory by an earlier crash or force-quit is
    // never needed again, so it is reclaimed regardless of the clear-temp-on-exit preference.
    if (m_dependencies.cleanStaleScratchRootsAsync)
    {
        m_dependencies.cleanStaleScratchRootsAsync();
    }

    if (m_dependencies.preloadLocalCatalogsAsync)
    {
        m_dependencies.preloadLocalCatalogsAsync();
    }

    loadSettings();

    if (globalSettings && m_dependencies.applyTheme)
    {
        const QString backdropMode =
            globalSettings->select(SettingsKeys::INTERFACE_MATERIAL_BACKDROP_MODE,
                                   QString(SettingsDefaults::INTERFACE_MATERIAL_BACKDROP_MODE));
        setMaterialBackdropMode(resolveMaterialBackdropMode(backdropMode));

        const QString contrastProfile =
            globalSettings->select(SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE,
                                   QString(SettingsDefaults::INTERFACE_THEME_CONTRAST_PROFILE));
        setThemeContrastProfile(resolveThemeContrastProfile(contrastProfile));

        const QString accentFamily = globalSettings->select(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY,
                                                            QString(SettingsDefaults::INTERFACE_THEME_ACCENT_FAMILY));
        setThemeAccentFamily(resolveThemeAccentFamily(accentFamily));

        const QString surfaceTintFamily =
            globalSettings->select(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY,
                                   QString(SettingsDefaults::INTERFACE_THEME_SURFACE_TINT_FAMILY));
        setThemeSurfaceTintFamily(resolveThemeSurfaceTintFamily(surfaceTintFamily));

        const QString tintStrength = globalSettings->select(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH,
                                                            QString(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH));
        setThemeTintStrength(resolveThemeTintStrength(tintStrength));

        // A preset stored puts its three parts in force; with none - an earlier version's settings, or a hand-made
        // combination - the parts stored stay as they are, and the preset is the one they make.
        if (const std::optional<QString> preset = globalSettings->selectOptional(SettingsKeys::INTERFACE_THEME_PRESET);
            preset.has_value())
        {
            applyThemePreset(resolveThemePreset(*preset));
        }

        // Night is off at every start unless the person asked to start in it.
        const QString startInNight = globalSettings->select(SettingsKeys::INTERFACE_START_IN_NIGHT,
                                                            SettingsDefaults::INTERFACE_START_IN_NIGHT ? "1" : "0");
        setNightMode(startInNight == QStringLiteral("1"));

        const QString density =
            globalSettings->select(SettingsKeys::INTERFACE_DENSITY, QString(SettingsDefaults::INTERFACE_DENSITY));
        setThemeDensity(resolveThemeDensity(density));

        const QString theme =
            globalSettings->select(SettingsKeys::INTERFACE_THEME, QString(SettingsDefaults::INTERFACE_THEME));
        m_dependencies.applyTheme(theme);

        const QString animationsStr = globalSettings->select(
            SettingsKeys::INTERFACE_ANIMATIONS_ENABLED, SettingsDefaults::INTERFACE_ANIMATIONS_ENABLED ? "1" : "0");
        an::ui::AnimationUtils::setAnimationsEnabled(animationsStr == "1");
    }
    else
    {
        if (m_dependencies.loadIcons)
        {
            m_dependencies.loadIcons();
        }
        if (m_dependencies.setActionIcons)
        {
            m_dependencies.setActionIcons();
        }
        if (m_dependencies.refreshProxyStyleTokens)
        {
            m_dependencies.refreshProxyStyleTokens();
        }
    }

    if (m_dependencies.createAuxToolBars)
    {
        m_dependencies.createAuxToolBars();
    }

    if (m_dependencies.setEnableImageActions)
    {
        m_dependencies.setEnableImageActions(false);
    }

    if (m_dependencies.updateRecentFilesMenuEntries)
    {
        m_dependencies.updateRecentFilesMenuEntries();
    }

    if (m_dependencies.initializeMainDashboard)
    {
        m_dependencies.initializeMainDashboard();
    }
    if (m_dependencies.initializeDockManager)
    {
        m_dependencies.initializeDockManager();
    }
    if (m_dependencies.initializeMaximizedView)
    {
        m_dependencies.initializeMaximizedView();
    }

    if (globalSettings && m_dependencies.lockLayout)
    {
        const QString lockLayoutStr =
            globalSettings->select(SettingsKeys::INTERFACE_LOCK_LAYOUT_BY_DEFAULT,
                                   SettingsDefaults::INTERFACE_LOCK_LAYOUT_BY_DEFAULT ? "1" : "0");
        if (lockLayoutStr == "1")
        {
            m_dependencies.lockLayout();
        }
    }
}

bool AppLifecycleCoordinator::prepareForClose() const
{
    GlobalSettingsPersistence* globalSettings =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    if (!m_dependencies.imageViewControlManager)
    {
        if (m_dependencies.cancelBackgroundWork)
        {
            m_dependencies.cancelBackgroundWork();
        }
        return true;
    }

    const bool confirmCloseUnsaved = isCloseConfirmationEnabled();
    const auto viewControls = m_dependencies.imageViewControlManager->getListOfImageViewControls();
    const size_t openProcessCount = m_dependencies.getNumOfOpenProcesses ? m_dependencies.getNumOfOpenProcesses() : 0;

    bool pendingChanges = false;
    for (auto* vc : viewControls)
    {
        if (vc && vc->hasPendingChanges())
        {
            pendingChanges = true;
            break;
        }
    }

    if (shouldPromptOnUnsavedChanges(confirmCloseUnsaved, pendingChanges))
    {
        int ret = QMessageBox::No;
        if (m_dependencies.messageBoxYesNo)
        {
            m_dependencies.messageBoxYesNo(
                m_dependencies.mainWindow, QStringLiteral("Close AfterNight"),
                QStringLiteral(
                    "There are some loaded images with pending changes, if you close the application without saving "
                    "them all changes will be lost.\n\nAre you sure you want to close the application?"),
                true, &ret);
        }

        if (ret == QMessageBox::No)
        {
            return false;
        }
    }
    else if (shouldPromptOnOpenProcesses(confirmCloseUnsaved, pendingChanges, openProcessCount))
    {
        int ret = QMessageBox::No;
        if (m_dependencies.messageBoxYesNo)
        {
            m_dependencies.messageBoxYesNo(
                m_dependencies.mainWindow, QStringLiteral("Close AfterNight"),
                QStringLiteral("There are some processes still open, if you close the application without saving them "
                               "all changes will be lost.\n\nAre you sure you want to close the application?"),
                true, &ret);
        }

        if (ret == QMessageBox::No)
        {
            return false;
        }
    }

    if (m_dependencies.cancelBackgroundWork)
    {
        m_dependencies.cancelBackgroundWork();
    }

    if (globalSettings)
    {
        const QString rememberSizeStr = globalSettings->select(
            SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE, SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE ? "1" : "0");
        if (rememberSizeStr == "1")
        {
            saveWindowGeometry();
        }
    }

    if (m_dependencies.isTerminating)
    {
        *m_dependencies.isTerminating = true;
    }

    if (m_dependencies.notifyApplicationClose)
    {
        m_dependencies.notifyApplicationClose();
    }

    if (openProcessCount > 0 && m_dependencies.closeOpenProcesses)
    {
        m_dependencies.closeOpenProcesses();
    }

    for (auto* vc : viewControls)
    {
        if (vc)
        {
            vc->deepClose();
        }
    }

    if (shouldCleanTempFilesOnExit() && m_dependencies.cleanTemporaryFiles)
    {
        m_dependencies.cleanTemporaryFiles();
    }

    return true;
}

void AppLifecycleCoordinator::loadSettings() const
{
    const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();

    if (m_dependencies.maxThreadsForBatchProcess)
    {
        *m_dependencies.maxThreadsForBatchProcess = computeBatchThreadBudget(hardwareConcurrency);
    }

    if (m_dependencies.maxThreadsForPreviewProcess)
    {
        *m_dependencies.maxThreadsForPreviewProcess = computePreviewThreadBudget(hardwareConcurrency);
    }
}

void AppLifecycleCoordinator::showWithSavedGeometry() const
{
    if (!m_dependencies.mainWindow)
    {
        return;
    }

    // Where the window was is the shell core's WindowGeometryStore: at its place when that place is on a screen there
    // is now, maximized otherwise.
    if (m_dependencies.showWindowWhereItWas)
    {
        m_dependencies.showWindowWhereItWas(*m_dependencies.mainWindow);
    }
    else
    {
        m_dependencies.mainWindow->showMaximized();
    }

    GlobalSettingsPersistence* globalSettings =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;
    if (!globalSettings || !m_dependencies.applyTheme)
    {
        return;
    }

    const QString persistedTheme =
        globalSettings->select(SettingsKeys::INTERFACE_THEME, QString(SettingsDefaults::INTERFACE_THEME));
    const auto reapplyTheme = [applyTheme = m_dependencies.applyTheme, persistedTheme]() {
        applyTheme(persistedTheme);
    };

    // Apply once after entering the event loop so widgets created during
    // startup pick up the persisted theme without a later visible reapply.
    QTimer::singleShot(0, m_dependencies.mainWindow, reapplyTheme);
}

void AppLifecycleCoordinator::saveWindowGeometry() const
{
    if (m_dependencies.mainWindow && m_dependencies.keepWindowPlace)
    {
        m_dependencies.keepWindowPlace(*m_dependencies.mainWindow);
    }
}

bool AppLifecycleCoordinator::shouldCleanTempFilesOnStartup() const
{
    GlobalSettingsPersistence* globalSettings =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    if (!globalSettings)
    {
        return true;
    }

    const QString clearTempStr = globalSettings->select(SettingsKeys::GENERAL_CLEAR_TEMP_ON_EXIT, "1");
    return clearTempStr == "1";
}

bool AppLifecycleCoordinator::shouldCleanTempFilesOnExit() const
{
    GlobalSettingsPersistence* globalSettings =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    if (!globalSettings)
    {
        return false;
    }

    const QString clearTempStr = globalSettings->select(SettingsKeys::GENERAL_CLEAR_TEMP_ON_EXIT, "1");
    return clearTempStr == "1";
}

bool AppLifecycleCoordinator::isCloseConfirmationEnabled() const
{
    GlobalSettingsPersistence* globalSettings =
        m_dependencies.globalSettingsPersistence ? m_dependencies.globalSettingsPersistence() : nullptr;

    if (!globalSettings)
    {
        return true;
    }

    const QString confirmStr = globalSettings->select(SettingsKeys::GENERAL_CONFIRM_CLOSE_UNSAVED, "1");
    return confirmStr == "1";
}

} // namespace an::ui
