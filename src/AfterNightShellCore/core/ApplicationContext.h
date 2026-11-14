// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application's side of the context Controls and the processes reach it through.

#include "AfterNightShellCoreExport.h"

#include "controls/IAppContext.h"

#include <functional>

namespace an::ui::shell
{

class ApplicationCore;
class IShell;

/// The application context over the application core and the shell made over it: each service is answered by the
/// part of the application that holds it, and nothing here names a window.
///
/// | Interface | Answered by |
/// |---|---|
/// | the settings, the process settings, the temporary files, the folders remembered, the thumbnails, the output
/// intents, the RAW policy, the session | the core | | the messages, the file dialogs | the core's `DialogService`,
/// `FileDialogService` | | the listeners, the recent files, the open process windows | the core's `AppEventBus`,
/// `RecentFilesModel`, `OpenProcessRegistry` | | the view factory, the views' registry and their manager | the core's
/// `ViewService`, which asks the shell where a view goes | | the status line | the shell's `IStatusDisplay` | | the
/// window, the process presenter | the shell |
///
/// What is still the window's - the theme, the recent files opened and the files dropped, the image actions' own
/// state - is answered through the `Dependencies` it is handed.
class AFTERNIGHTSHELLCORE_API ApplicationContext final : public an::ui::IAppContext,
                                                         public an::ui::IGlobalSettingsProvider,
                                                         public an::ui::IInterfaceSettingsRuntimeService,
                                                         public an::ui::IProcessSettingsProvider,
                                                         public an::ui::ITempFileService,
                                                         public an::ui::IPathMemoryService,
                                                         public an::ui::IAppEventBus,
                                                         public an::ui::IProcessRegistry,
                                                         public an::ui::IOutputIntentProvider,
                                                         public an::ui::IRawOpenPolicyService,
                                                         public an::ui::IRecentFilesService,
                                                         public an::ui::IThumbnailPathProvider,
                                                         public an::ui::IStatusBarService,
                                                         public an::ui::IActionStateManager
{
public:
    /// What the context asks of the window that holds it, and of no service yet: the theme it applies, the files it
    /// opens, the state of an image action it shows. An empty one does nothing.
    struct Dependencies
    {
        std::function<void(const QString& themeId)> applyTheme;
        std::function<void(const QString& filePath)> openRecentSingleFile;
        std::function<void(const QStringList& files)> openDroppedFiles;
        std::function<void(ImageViewControl* view, GlobalViewControlActions action, bool enable)>
            setCustomImageActionEnabled;
    };

    /// @param core the application core; it outlives the context.
    /// @param shell the shell the context is made over; it outlives the context.
    ApplicationContext(ApplicationCore& core, IShell& shell, Dependencies dependencies = {});

    // ---- IAppContext
    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override;
    [[nodiscard]] an::ui::IDialogService* getDialogService() override;
    [[nodiscard]] an::ui::IGlobalSettingsProvider* getGlobalSettings() override;
    [[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override;
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override;
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override;
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override;
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override;
    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override;
    [[nodiscard]] an::ui::IViewRegistry* getViewRegistry() override;
    [[nodiscard]] an::ui::IAppEventBus* getAppEventBus() override;
    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override;
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override;
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override;
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override;
    [[nodiscard]] an::ui::IViewControlManager* getViewControlManager() override;
    [[nodiscard]] an::ui::IStatusBarService* getStatusBarService() override;
    [[nodiscard]] an::ui::IActionStateManager* getActionStateManager() override;
    [[nodiscard]] an::ui::IEngineSessionProvider* getEngineSessionProvider() override;
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override;
    [[nodiscard]] QWidget* getMainWindow() override;
    [[nodiscard]] an::ui::IProcessPresenter* getProcessPresenter() override;

    // ---- IGlobalSettingsProvider
    bool upsert(const QString& key, const QString& value) override;
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override;
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override;

    // ---- IInterfaceSettingsRuntimeService
    void applyTheme(const QString& themeId) override;
    void refreshAllZoomSettings() override;

    // ---- IProcessSettingsProvider
    bool upsert(const QString& processName, int windowState, int width, int height,
                const QString& jsonSettings) override;
    [[nodiscard]] bool select(const QString& processName, int& windowState, int& width, int& height,
                              QString& jsonSettings) override;

    // ---- ITempFileService
    [[nodiscard]] bool isTempFile(const std::string& fileName) override;
    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override;
    [[nodiscard]] QString getTempDirPath() const override;

    // ---- IPathMemoryService
    [[nodiscard]] QString selectPath(const QString& contextKey, const QString& defaultValue = {}) override;
    bool upsertPath(const QString& contextKey, const QString& path) override;

    // ---- IAppEventBus
    void subscribeListener(AfterNightAppListener* listener) override;
    void unsubscribeListener(AfterNightAppListener* listener) override;
    void postAfterNightEvent(AfterNightEvent* event, int priority) override;
    void notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl) override;
    void notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl) override;

    // ---- IProcessRegistry
    void registerProcessInstance(TopLevelProcessBaseControl* process) override;
    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override;

    // ---- IOutputIntentProvider
    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const override;

    // ---- IRawOpenPolicyService
    [[nodiscard]] std::shared_ptr<const an::ui::RawOpenPolicy> rawOpenPolicy() const override;
    an::Status setRawOpenSettings(const an::ui::RawOpenSettings& settings) override;

    // ---- IRecentFilesService
    [[nodiscard]] std::deque<std::string> getRecentFilesList() const override;
    void openRecentSingleFile(const QString& filePath) override;
    void openDroppedFiles(const QStringList& files) override;

    // ---- IThumbnailPathProvider
    [[nodiscard]] QString selectThumbnailPath(const QString& filePath) const override;

    // ---- IStatusBarService
    void updateImageInfoInStatusBar() override;
    void updateImageZoomInfoInStatusBar() override;
    void updateImageCelestialCoordsInStatusBar(double raDegrees, double decDegrees) override;
    void clearImageCelestialCoordsInStatusBar() override;

    // ---- IActionStateManager
    void setCustomImageActionEnabled(ImageViewControl* view, GlobalViewControlActions action, bool enable) override;
    void updateActiveImageActions() override;
    void updateActiveImageHistory() override;

private:
    ApplicationCore& m_core;
    IShell& m_shell;
    Dependencies m_dependencies;
};

} // namespace an::ui::shell
