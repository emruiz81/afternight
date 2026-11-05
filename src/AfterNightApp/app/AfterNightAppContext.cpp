// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "AfterNightAppContext.h"

#include "app/AfterNightApp.h"
#include "app/FileDialogUtils.h"
#include "app/FilePolicies.h"
#include "app/IconManager.h"

#include "controls/ImageViewControlManager.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace an::ui
{

namespace
{

/// Thin adapter: delegates to the existing IconManager instance.
class IconManagerAdapter final : public IIconProvider
{
public:
    explicit IconManagerAdapter(IconManagerSptr mgr) : m_mgr(std::move(mgr)) {}

    [[nodiscard]] QIcon getIcon(const QString& icon) override
    {
        return m_mgr->getIcon(icon);
    }

    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString& fileName, QSize size, QColor color) override
    {
        return m_mgr->loadSvgFileAsPixmap(fileName, size, color);
    }

    [[nodiscard]] QColor getColorNormal() const override
    {
        return m_mgr->getColorNormal();
    }

    [[nodiscard]] QColor getColorDisabled() const override
    {
        return m_mgr->getColorDisabled();
    }

private:
    IconManagerSptr m_mgr;
};

QString trackedFolderPath(AfterNightApp* app, const QString& context_key)
{
    if (!app || context_key.trimmed().isEmpty())
    {
        return {};
    }

    auto* tracker = app->getFolderTrackerPersistence();
    if (!tracker)
    {
        return {};
    }

    const QString tracked_path = tracker->select(context_key).trimmed();
    return (!tracked_path.isEmpty() && QDir(tracked_path).exists()) ? tracked_path : QString();
}

QString closestExistingDirectory(QString path)
{
    path = path.trimmed();
    if (path.isEmpty())
    {
        return {};
    }

    QDir dir(path);
    while (!path.isEmpty() && !dir.exists())
    {
        const QString parent_path = QFileInfo(path).absolutePath();
        if (parent_path == path)
        {
            break;
        }
        path = parent_path;
        dir.setPath(path);
    }

    return dir.exists() ? path : QString();
}

QString joinedNameFilters(const QStringList& name_filters)
{
    return name_filters.join(QStringLiteral(";;"));
}

void persistTrackedFolder(AfterNightApp* app, const QString& context_key, const QString& selected_path)
{
    if (!app || context_key.trimmed().isEmpty() || selected_path.trimmed().isEmpty())
    {
        return;
    }

    if (auto* tracker = app->getFolderTrackerPersistence())
    {
        tracker->upsert(context_key, QFileInfo(selected_path).absolutePath());
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------

AfterNightAppContext::AfterNightAppContext(AfterNightApp* app) : m_app(app) {}

// -- IAppContext --

IIconProviderPtr AfterNightAppContext::getIconProvider()
{
    return std::make_shared<IconManagerAdapter>(m_app->getIconManager());
}

IDialogService* AfterNightAppContext::getDialogService()
{
    return this;
}
IGlobalSettingsProvider* AfterNightAppContext::getGlobalSettings()
{
    return this;
}
IInterfaceSettingsRuntimeService* AfterNightAppContext::getInterfaceSettingsRuntimeService()
{
    return this;
}
IProcessSettingsProvider* AfterNightAppContext::getProcessSettings()
{
    return this;
}
ITempFileService* AfterNightAppContext::getTempFileService()
{
    return this;
}
IPathMemoryService* AfterNightAppContext::getPathMemoryService()
{
    return this;
}
IFileDialogService* AfterNightAppContext::getFileDialogService()
{
    return this;
}
IViewFactory* AfterNightAppContext::getViewFactory()
{
    return this;
}
IViewRegistry* AfterNightAppContext::getViewRegistry()
{
    return this;
}
IAppEventBus* AfterNightAppContext::getAppEventBus()
{
    return this;
}
IProcessRegistry* AfterNightAppContext::getProcessRegistry()
{
    return this;
}
IOutputIntentProvider* AfterNightAppContext::getOutputIntentProvider()
{
    return this;
}
IRecentFilesService* AfterNightAppContext::getRecentFilesService()
{
    return this;
}
IThumbnailPathProvider* AfterNightAppContext::getThumbnailPathProvider()
{
    return this;
}
IViewControlManager* AfterNightAppContext::getViewControlManager()
{
    return this;
}
IStatusBarService* AfterNightAppContext::getStatusBarService()
{
    return this;
}
IActionStateManager* AfterNightAppContext::getActionStateManager()
{
    return this;
}
IEngineSessionProvider* AfterNightAppContext::getEngineSessionProvider()
{
    return m_app->engineSessionProvider();
}
IRawOpenPolicyService* AfterNightAppContext::getRawOpenPolicyService()
{
    return this;
}
QWidget* AfterNightAppContext::getMainWindow()
{
    return m_app;
}

// -- IDialogService --

void AfterNightAppContext::openSettingsAt(const QString& pageName)
{
    m_app->openSettingsAt(pageName);
}

void AfterNightAppContext::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn,
                                           int* ret)
{
    m_app->messageBoxYesNo(parent, title, text, warn, ret);
}

void AfterNightAppContext::notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_app->notifyErrorMessage(parent, title, text);
}

void AfterNightAppContext::notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_app->notifyErrorMessageAsync(parent, title, text);
}

void AfterNightAppContext::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_app->notifyWarnMessage(parent, title, text);
}

void AfterNightAppContext::notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_app->notifyWarnMessageAsync(parent, title, text);
}

void AfterNightAppContext::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_app->notifyInfoMessage(parent, title, text);
}

void AfterNightAppContext::notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_app->notifyInfoMessageAsync(parent, title, text);
}

// -- IGlobalSettingsProvider --

bool AfterNightAppContext::upsert(const QString& key, const QString& value)
{
    return m_app->getGlobalSettingsPersistence()->upsert(key, value);
}

QString AfterNightAppContext::select(const QString& key, const QString& defaultValue)
{
    return m_app->getGlobalSettingsPersistence()->select(key, defaultValue);
}

std::optional<QString> AfterNightAppContext::selectOptional(const QString& key)
{
    return m_app->getGlobalSettingsPersistence()->selectOptional(key);
}

// -- IInterfaceSettingsRuntimeService --

void AfterNightAppContext::applyTheme(const QString& themeId)
{
    m_app->applyTheme(themeId);
}

void AfterNightAppContext::refreshAllZoomSettings()
{
    m_app->refreshAllZoomSettings();
}

// -- IProcessSettingsProvider --

bool AfterNightAppContext::upsert(const QString& processName, int win_state, int width, int height,
                                  const QString& json_settings)
{
    return m_app->getProcessSettingsPersistence()->upsert(processName, win_state, width, height, json_settings);
}

bool AfterNightAppContext::select(const QString& processName, int& win_state, int& width, int& height,
                                  QString& json_settings)
{
    return m_app->getProcessSettingsPersistence()->select(processName, win_state, width, height, json_settings);
}

// -- ITempFileService --

bool AfterNightAppContext::isTempFile(const std::string& fileName)
{
    return m_app->isTempFile(fileName);
}

std::string AfterNightAppContext::createTempFilePathForNewSequence(an::SequenceFormat format) const
{
    return m_app->createTempFilePathForNewSequence(format);
}

QString AfterNightAppContext::getTempDirPath() const
{
    return m_app->getTempDirPath();
}

// -- IPathMemoryService --

QString AfterNightAppContext::selectPath(const QString& contextKey, const QString& defaultValue)
{
    const QString tracked_path = trackedFolderPath(m_app, contextKey);
    return tracked_path.isEmpty() ? defaultValue : tracked_path;
}

bool AfterNightAppContext::upsertPath(const QString& contextKey, const QString& path)
{
    if (contextKey.trimmed().isEmpty())
    {
        return false;
    }

    if (auto* tracker = m_app->getFolderTrackerPersistence())
    {
        return tracker->upsert(contextKey, path);
    }

    return false;
}

// -- IFileDialogService --

QStringList AfterNightAppContext::selectImageFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                                   const bool supportMultipleFiles)
{
    return m_app->selectImageFilesDlg(parent, title, folderContext, supportMultipleFiles);
}

QStringList AfterNightAppContext::selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                              const QStringList& fileExtensions)
{
    return m_app->selectFilesDlg(parent, title, folderContext, fileExtensions);
}

QStringList AfterNightAppContext::saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                                            const QString& folderContext, const QStringList& fileExtensions)
{
    return m_app->saveFileDlg(parent, title, defaultDir, folderContext, fileExtensions);
}

// -- IViewFactory --

ImageViewControl* AfterNightAppContext::createNewImageViewControlFromImage(
    ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const std::string& historyDesc,
    ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour, bool pendingChanges)
{
    return m_app->createNewImageViewControlFromImage(customViewControl, std::move(document), parent, historyDesc, mode,
                                                     neighbour, keepFocusedNeighbour, pendingChanges);
}

ImageViewControl* AfterNightAppContext::createNewImageViewControlFromSequence(
    ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const bool autoDebayer,
    const std::string& historyDesc, ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour,
    bool pendingChanges)
{
    return m_app->createNewImageViewControlFromSequence(customViewControl, std::move(document), parent, autoDebayer,
                                                        historyDesc, mode, neighbour, keepFocusedNeighbour,
                                                        pendingChanges);
}

// -- IViewRegistry --

ImageViewControl* AfterNightAppContext::getActiveImageViewControl()
{
    if (auto* manager = m_app->getImageViewControlManager())
    {
        return manager->getActiveImageViewControl();
    }

    return nullptr;
}

void AfterNightAppContext::setActiveImageViewControl(ImageViewControl* view)
{
    m_app->setActiveImageViewControl(view);
}

// -- IAppEventBus --

void AfterNightAppContext::subscribeListener(AfterNightAppListener* listener)
{
    m_app->subscribeListener(listener);
}

void AfterNightAppContext::unsubscribeListener(AfterNightAppListener* listener)
{
    m_app->unsubscribeListener(listener);
}

void AfterNightAppContext::postAfterNightEvent(AfterNightEvent* event, int priority)
{
    m_app->postAfterNightEvent(event, priority);
}

void AfterNightAppContext::notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_app->notifyProcessOpened(processName, targetCtrl);
}

void AfterNightAppContext::notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_app->notifyProcessExecuted(processName, targetCtrl);
}

// -- IProcessRegistry --

void AfterNightAppContext::registerProcessInstance(TopLevelProcessBaseControl* process)
{
    m_app->registerProcessInstance(process);
}

void AfterNightAppContext::unregisterProcessInstance(TopLevelProcessBaseControl* process)
{
    m_app->unregisterProcessInstance(process);
}

// -- IOutputIntentProvider --

an::Result<an::OutputIntent> AfterNightAppContext::buildOutputIntent(const an::ImageDescriptor& source,
                                                                     const an::CodecFormat container) const
{
    return m_app->buildOutputIntent(source, container);
}

// -- IRawOpenPolicyService --

std::shared_ptr<const RawOpenPolicy> AfterNightAppContext::rawOpenPolicy() const
{
    return m_app->rawOpenPolicy();
}

an::Status AfterNightAppContext::setRawOpenSettings(const RawOpenSettings& settings)
{
    return m_app->setRawOpenSettings(settings);
}

// -- IRecentFilesService --

std::deque<std::string> AfterNightAppContext::getRecentFilesList() const
{
    return m_app->getRecentFilesList();
}

void AfterNightAppContext::openRecentSingleFile(const QString& filePath)
{
    m_app->openRecentSingleFile(filePath);
}

void AfterNightAppContext::openDroppedFiles(const QStringList& files)
{
    m_app->openDroppedFiles(files);
}

// -- IThumbnailPathProvider --

QString AfterNightAppContext::selectThumbnailPath(const QString& filePath) const
{
    if (auto* persistence = m_app->getThumbnailPersistence())
    {
        return persistence->select(filePath);
    }

    return {};
}

// -- IViewControlManager --

void AfterNightAppContext::addViewControl(ImageViewControl* view)
{
    m_app->addImageViewControl(view);
}

void AfterNightAppContext::removeViewControl(ImageViewControl* view, bool deleteObject)
{
    m_app->removeImageViewControl(view, deleteObject);
}

void AfterNightAppContext::renameViewControl(ImageViewControl* view)
{
    m_app->renameImageViewControl(view);
}

void AfterNightAppContext::maximizeViewControl(ImageViewControl* ctrl)
{
    m_app->maximizeViewControl(ctrl);
}

void AfterNightAppContext::restoreViewControl(ImageViewControl* ctrl)
{
    m_app->restoreViewControl(ctrl);
}

void AfterNightAppContext::notifyViewControlUpdated(const ImageViewControl* ctrl)
{
    m_app->notifyViewControlUpdated(ctrl);
}

void AfterNightAppContext::createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event)
{
    m_app->createContextMenuForViewControl(view, event);
}

bool AfterNightAppContext::supportsDockWidgets() const
{
    return true;
}

ImageViewControl* AfterNightAppContext::getViewControlByName(const std::string& name)
{
    return m_app->getImageViewControlManager()->getImageViewControlByName(name);
}

std::vector<ImageViewControl*> AfterNightAppContext::getListOfViewControls()
{
    return m_app->getImageViewControlManager()->getListOfImageViewControls();
}

std::string AfterNightAppContext::createViewControlUniqueName(const std::string& name)
{
    return m_app->getImageViewControlManager()->createImageViewControlUniqueName(name);
}

// -- IStatusBarService --

void AfterNightAppContext::updateImageInfoInStatusBar()
{
    m_app->updateImageInfoInStatusBar();
}

void AfterNightAppContext::updateImageZoomInfoInStatusBar()
{
    m_app->updateImageZoomInfoInStatusBar();
}

void AfterNightAppContext::updateImageCelestialCoordsInStatusBar(double ra_deg, double dec_deg)
{
    m_app->updateImageCelestialCoordsInStatusBar(ra_deg, dec_deg);
}

void AfterNightAppContext::clearImageCelestialCoordsInStatusBar()
{
    m_app->clearImageCelestialCoordsInStatusBar();
}

// -- IActionStateManager --

void AfterNightAppContext::setCustomImageActionEnabled(ImageViewControl* view, GlobalViewControlActions action,
                                                       bool enable)
{
    m_app->setCustomImageActionEnabled(view, action, enable);
}

void AfterNightAppContext::updateActiveImageActions()
{
    m_app->updateActiveImageActions();
}

void AfterNightAppContext::updateActiveImageHistory()
{
    m_app->updateActiveImageHistory();
}

} // namespace an::ui
