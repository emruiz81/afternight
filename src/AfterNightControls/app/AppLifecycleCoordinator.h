// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <cstddef>
#include <functional>

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtGlobal>

class GlobalSettingsPersistence;
class ImageViewControlManager;
class QMainWindow;
class QWidget;

namespace an::ui
{

inline constexpr int kMainWindowMinimumWidth = 1200;
inline constexpr int kMainWindowMinimumHeight = 720;

inline bool shouldPromptOnUnsavedChanges(const bool confirmCloseUnsaved, const bool hasPendingChanges)
{
    return confirmCloseUnsaved && hasPendingChanges;
}

inline bool shouldPromptOnOpenProcesses(const bool confirmCloseUnsaved, const bool hasPendingChanges,
                                        const size_t openProcessCount)
{
    return confirmCloseUnsaved && !hasPendingChanges && openProcessCount > 0;
}

inline int computeBatchThreadBudget(const unsigned int hardwareConcurrency)
{
    int threads = static_cast<int>(hardwareConcurrency);
    if (threads > 8)
    {
        threads -= 2;
    }
    else if (threads > 2)
    {
        --threads;
    }

    return threads;
}

inline int computePreviewThreadBudget(const unsigned int hardwareConcurrency)
{
    int threads = static_cast<int>(hardwareConcurrency);
    if (threads > 16)
    {
        threads -= 4;
    }
    else if (threads > 8)
    {
        threads -= 2;
    }
    else if (threads > 2)
    {
        --threads;
    }

    return threads;
}

inline QSize clampRestoredWindowSize(const QSize& size)
{
    return QSize(qMax(size.width(), kMainWindowMinimumWidth), qMax(size.height(), kMainWindowMinimumHeight));
}

inline bool intersectsAnyScreen(const QRect& windowRect, const QList<QRect>& screenGeometries)
{
    for (const QRect& screenGeometry : screenGeometries)
    {
        if (screenGeometry.intersects(windowRect))
        {
            return true;
        }
    }

    return false;
}

class AFTERNIGHTCONTROLS_API AppLifecycleCoordinator
{
public:
    struct Dependencies
    {
        QMainWindow* mainWindow = nullptr;
        ImageViewControlManager* imageViewControlManager = nullptr;
        bool* isTerminating = nullptr;
        int* maxThreadsForBatchProcess = nullptr;
        int* maxThreadsForPreviewProcess = nullptr;
        std::function<GlobalSettingsPersistence*()> globalSettingsPersistence;

        std::function<void()> logModernizationFeatureFlags;
        std::function<void()> initializePersistence;
        std::function<void()> cleanTemporaryFiles;
        std::function<void()> cleanTemporaryFilesAsync;
        std::function<void()> cleanStaleScratchRootsAsync;
        std::function<void()> preloadLocalCatalogsAsync;
        std::function<void(const QString&)> applyTheme;
        std::function<void()> loadIcons;
        std::function<void()> setActionIcons;
        std::function<void()> refreshProxyStyleTokens;
        std::function<void()> createAuxToolBars;
        std::function<void(bool)> setEnableImageActions;
        std::function<void()> updateRecentFilesMenuEntries;
        std::function<void()> initializeMainDashboard;
        std::function<void()> initializeDockManager;
        std::function<void()> initializeMaximizedView;
        std::function<void()> lockLayout;
        std::function<void()> cancelBackgroundWork;
        std::function<void()> notifyApplicationClose;
        std::function<size_t()> getNumOfOpenProcesses;
        std::function<void()> closeOpenProcesses;
        std::function<void(QWidget*, const QString&, const QString&, bool, int*)> messageBoxYesNo;
        std::function<QList<QRect>()> availableScreenGeometries;
    };

    explicit AppLifecycleCoordinator(const Dependencies& dependencies);

    void initialize() const;
    bool prepareForClose() const;

    void loadSettings() const;
    void showWithSavedGeometry() const;
    void saveWindowGeometry() const;

private:
    bool shouldCleanTempFilesOnStartup() const;
    bool shouldCleanTempFilesOnExit() const;
    bool isCloseConfirmationEnabled() const;

    Dependencies m_dependencies;
};

} // namespace an::ui
