// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/ApplicationContext.h"

#include "actions/ActionStateCoordinator.h"
#include "core/ApplicationCore.h"
#include "core/ShellRegistry.h"
#include "core/IShell.h"
#include "services/AppEventBus.h"
#include "services/DialogService.h"
#include "services/FileDialogService.h"
#include "services/OpenProcessRegistry.h"
#include "services/RecentFilesModel.h"
#include "views/ViewService.h"

#include "app/IconManager.h"
#include "app/RuntimeStorageCoordinator.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QDir>

#include <utility>

namespace an::ui::shell
{

namespace
{

/// The icons, as the context hands them out: the application's icon manager.
class IconManagerAdapter final : public an::ui::IIconProvider
{
public:
    explicit IconManagerAdapter(IconManagerSptr manager) : m_manager(std::move(manager)) {}

    [[nodiscard]] QIcon getIcon(const QString& icon) override
    {
        return m_manager ? m_manager->getIcon(icon) : QIcon();
    }

    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString& fileName, const QSize size, const QColor color) override
    {
        return m_manager ? m_manager->loadSvgFileAsPixmap(fileName, size, color) : QPixmap();
    }

    [[nodiscard]] QColor getColorNormal() const override
    {
        return m_manager ? m_manager->getColorNormal() : QColor();
    }

    [[nodiscard]] QColor getColorDisabled() const override
    {
        return m_manager ? m_manager->getColorDisabled() : QColor();
    }

private:
    IconManagerSptr m_manager;
};

/// The folder a context remembers, when it still exists; empty otherwise.
[[nodiscard]] QString trackedFolderPath(FolderTrackerPersistence* tracker, const QString& contextKey)
{
    if (tracker == nullptr || contextKey.trimmed().isEmpty())
    {
        return {};
    }

    const QString trackedPath = tracker->select(contextKey).trimmed();
    return (!trackedPath.isEmpty() && QDir(trackedPath).exists()) ? trackedPath : QString();
}

} // namespace

ApplicationContext::ApplicationContext(ApplicationCore& core, IShell& shell, Dependencies dependencies)
    : m_core(core), m_shell(shell), m_dependencies(std::move(dependencies))
{
}

// ---- IAppContext

an::ui::IIconProviderPtr ApplicationContext::getIconProvider()
{
    return std::make_shared<IconManagerAdapter>(m_core.iconManager());
}

an::ui::IDialogService* ApplicationContext::getDialogService()
{
    return &m_core.dialogService();
}

an::ui::IGlobalSettingsProvider* ApplicationContext::getGlobalSettings()
{
    return this;
}

an::ui::IInterfaceSettingsRuntimeService* ApplicationContext::getInterfaceSettingsRuntimeService()
{
    return this;
}

an::ui::IProcessSettingsProvider* ApplicationContext::getProcessSettings()
{
    return this;
}

an::ui::ITempFileService* ApplicationContext::getTempFileService()
{
    return this;
}

an::ui::IPathMemoryService* ApplicationContext::getPathMemoryService()
{
    return this;
}

an::ui::IFileDialogService* ApplicationContext::getFileDialogService()
{
    return &m_core.fileDialogService();
}

an::ui::IViewFactory* ApplicationContext::getViewFactory()
{
    return &m_core.viewService();
}

an::ui::IViewRegistry* ApplicationContext::getViewRegistry()
{
    return &m_core.viewService();
}

an::ui::IAppEventBus* ApplicationContext::getAppEventBus()
{
    return this;
}

an::ui::IProcessRegistry* ApplicationContext::getProcessRegistry()
{
    return this;
}

an::ui::IOutputIntentProvider* ApplicationContext::getOutputIntentProvider()
{
    return this;
}

an::ui::IRecentFilesService* ApplicationContext::getRecentFilesService()
{
    return this;
}

an::ui::IThumbnailPathProvider* ApplicationContext::getThumbnailPathProvider()
{
    return this;
}

an::ui::IViewControlManager* ApplicationContext::getViewControlManager()
{
    return &m_core.viewService();
}

an::ui::IStatusBarService* ApplicationContext::getStatusBarService()
{
    return this;
}

an::ui::IActionStateManager* ApplicationContext::getActionStateManager()
{
    return this;
}

an::ui::IEngineSessionProvider* ApplicationContext::getEngineSessionProvider()
{
    return m_core.engineSessionProvider();
}

an::ui::IRawOpenPolicyService* ApplicationContext::getRawOpenPolicyService()
{
    return this;
}

QWidget* ApplicationContext::getMainWindow()
{
    return m_shell.mainWindow();
}

an::ui::IProcessPresenter* ApplicationContext::getProcessPresenter()
{
    return m_shell.processPresenter();
}

// ---- IGlobalSettingsProvider

bool ApplicationContext::upsert(const QString& key, const QString& value)
{
    return m_core.globalSettings()->upsert(key, value);
}

QString ApplicationContext::select(const QString& key, const QString& defaultValue)
{
    return m_core.globalSettings()->select(key, defaultValue);
}

std::optional<QString> ApplicationContext::selectOptional(const QString& key)
{
    return m_core.globalSettings()->selectOptional(key);
}

// ---- IInterfaceSettingsRuntimeService

void ApplicationContext::applyTheme(const QString& themeId)
{
    if (m_dependencies.applyTheme)
    {
        m_dependencies.applyTheme(themeId);
    }
}

std::vector<an::ui::ShellOption> ApplicationContext::availableShells() const
{
    std::vector<an::ui::ShellOption> shells;
    if (const ShellRegistry* const registry = m_core.shellRegistry())
    {
        for (const ShellDescriptor* descriptor : registry->descriptors())
        {
            shells.push_back(an::ui::ShellOption{.id = descriptor->id, .displayName = descriptor->displayName});
        }
    }
    return shells;
}

void ApplicationContext::refreshAllZoomSettings()
{
    for (ImageViewControl* view : m_core.viewService().getListOfViewControls())
    {
        if (view)
        {
            view->refreshZoomSettings();
        }
    }
}

// ---- IProcessSettingsProvider

bool ApplicationContext::upsert(const QString& processName, const int windowState, const int width, const int height,
                                const QString& jsonSettings)
{
    ProcessSettingsPersistence* const store = m_core.processSettingsStore();
    return store != nullptr && store->upsert(processName, windowState, width, height, jsonSettings);
}

bool ApplicationContext::select(const QString& processName, int& windowState, int& width, int& height,
                                QString& jsonSettings)
{
    ProcessSettingsPersistence* const store = m_core.processSettingsStore();
    return store != nullptr && store->select(processName, windowState, width, height, jsonSettings);
}

// ---- ITempFileService

bool ApplicationContext::isTempFile(const std::string& fileName)
{
    return m_core.isTempFile(fileName);
}

std::string ApplicationContext::createTempFilePathForNewSequence(const an::SequenceFormat format) const
{
    return m_core.runtimeStorage().createTempFilePathForNewSequence(format);
}

QString ApplicationContext::getTempDirPath() const
{
    return m_core.runtimeStorage().getTempDirPath();
}

// ---- IPathMemoryService

QString ApplicationContext::selectPath(const QString& contextKey, const QString& defaultValue)
{
    const QString trackedPath = trackedFolderPath(m_core.folderStore(), contextKey);
    return trackedPath.isEmpty() ? defaultValue : trackedPath;
}

bool ApplicationContext::upsertPath(const QString& contextKey, const QString& path)
{
    if (contextKey.trimmed().isEmpty())
    {
        return false;
    }

    FolderTrackerPersistence* const tracker = m_core.folderStore();
    return tracker != nullptr && tracker->upsert(contextKey, path);
}

// ---- IAppEventBus

void ApplicationContext::subscribeListener(AfterNightAppListener* listener)
{
    m_core.eventBus().subscribe(listener);
}

void ApplicationContext::unsubscribeListener(AfterNightAppListener* listener)
{
    m_core.eventBus().unsubscribe(listener);
}

void ApplicationContext::postAfterNightEvent(AfterNightEvent* event, const int priority)
{
    m_core.postEvent(event, priority);
}

void ApplicationContext::notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_core.eventBus().notifyProcessOpened(processName, targetCtrl);
}

void ApplicationContext::notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_core.eventBus().notifyProcessExecuted(processName, targetCtrl);
}

// ---- IProcessRegistry

void ApplicationContext::registerProcessInstance(TopLevelProcessBaseControl* process)
{
    m_core.openProcesses().add(process);
}

void ApplicationContext::unregisterProcessInstance(TopLevelProcessBaseControl* process)
{
    m_core.openProcesses().remove(process);
}

// ---- IOutputIntentProvider

an::Result<an::OutputIntent> ApplicationContext::buildOutputIntent(const an::ImageDescriptor& source,
                                                                   const an::CodecFormat container) const
{
    return m_core.buildOutputIntent(source, container);
}

// ---- IRawOpenPolicyService

std::shared_ptr<const an::ui::RawOpenPolicy> ApplicationContext::rawOpenPolicy() const
{
    return m_core.rawOpenPolicy();
}

an::Status ApplicationContext::setRawOpenSettings(const an::ui::RawOpenSettings& settings)
{
    return m_core.setRawOpenSettings(settings);
}

// ---- IRecentFilesService

std::deque<std::string> ApplicationContext::getRecentFilesList() const
{
    return m_core.recentFiles().entries();
}

void ApplicationContext::openRecentSingleFile(const QString& filePath)
{
    if (m_dependencies.openRecentSingleFile)
    {
        m_dependencies.openRecentSingleFile(filePath);
    }
}

void ApplicationContext::openDroppedFiles(const QStringList& files)
{
    if (m_dependencies.openDroppedFiles)
    {
        m_dependencies.openDroppedFiles(files);
    }
}

// ---- IThumbnailPathProvider

QString ApplicationContext::selectThumbnailPath(const QString& filePath) const
{
    ThumbnailPersistence* const store = m_core.thumbnailStore();
    return store != nullptr ? store->select(filePath) : QString();
}

// ---- IStatusBarService

void ApplicationContext::updateImageInfoInStatusBar()
{
    m_shell.statusDisplay().showImageInfo(m_core.viewService().getActiveImageViewControl());
}

void ApplicationContext::updateImageZoomInfoInStatusBar()
{
    m_shell.statusDisplay().showZoom(m_core.viewService().getActiveImageViewControl());
}

void ApplicationContext::updateImageCelestialCoordsInStatusBar(const double raDegrees, const double decDegrees)
{
    m_shell.statusDisplay().showCelestialCoordinates(raDegrees, decDegrees);
}

void ApplicationContext::clearImageCelestialCoordsInStatusBar()
{
    m_shell.statusDisplay().clearCelestialCoordinates();
}

// ---- IActionStateManager

void ApplicationContext::setCustomImageActionEnabled(ImageViewControl* view, const GlobalViewControlActions action,
                                                     const bool enable)
{
    m_core.actionState().setCustomImageActionEnabled(view, action, enable);
}

void ApplicationContext::updateActiveImageActions()
{
    m_core.actionState().updateActiveImageActions();
}

void ApplicationContext::updateActiveImageHistory()
{
    m_core.actionState().updateActiveImageHistory();
}

} // namespace an::ui::shell
