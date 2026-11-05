// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/IAppContext.h"

#include <memory>

class AfterNightApp;

namespace an::ui
{

/// Adapter that bridges the existing AfterNightApp singleton to the
/// IAppContext interface hierarchy.  This is the application-side
/// implementation used by AfterNightApp; the extension host will provide
/// its own ExtensionAppContext.
///
/// It lends the session the application owns once the application opened it, the RAW policy and the output intents
/// the application's persistence coordinator publishes from the settings store, and makes views of the documents it
/// is handed through the application's view creation coordinator.
class AfterNightAppContext final : public IAppContext,
                                   public IDialogService,
                                   public IGlobalSettingsProvider,
                                   public IInterfaceSettingsRuntimeService,
                                   public IProcessSettingsProvider,
                                   public ITempFileService,
                                   public IPathMemoryService,
                                   public IFileDialogService,
                                   public IViewFactory,
                                   public IViewRegistry,
                                   public IAppEventBus,
                                   public IProcessRegistry,
                                   public IOutputIntentProvider,
                                   public IRawOpenPolicyService,
                                   public IRecentFilesService,
                                   public IThumbnailPathProvider,
                                   public IViewControlManager,
                                   public IStatusBarService,
                                   public IActionStateManager
{
public:
    explicit AfterNightAppContext(AfterNightApp* app);

    // -- IAppContext --
    [[nodiscard]] IIconProviderPtr getIconProvider() override;
    [[nodiscard]] IDialogService* getDialogService() override;
    [[nodiscard]] IGlobalSettingsProvider* getGlobalSettings() override;
    [[nodiscard]] IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override;
    [[nodiscard]] IProcessSettingsProvider* getProcessSettings() override;
    [[nodiscard]] ITempFileService* getTempFileService() override;
    [[nodiscard]] IPathMemoryService* getPathMemoryService() override;
    [[nodiscard]] IFileDialogService* getFileDialogService() override;
    [[nodiscard]] IViewFactory* getViewFactory() override;
    [[nodiscard]] IViewRegistry* getViewRegistry() override;
    [[nodiscard]] IAppEventBus* getAppEventBus() override;
    [[nodiscard]] IProcessRegistry* getProcessRegistry() override;
    [[nodiscard]] IOutputIntentProvider* getOutputIntentProvider() override;
    [[nodiscard]] IRecentFilesService* getRecentFilesService() override;
    [[nodiscard]] IThumbnailPathProvider* getThumbnailPathProvider() override;
    [[nodiscard]] IViewControlManager* getViewControlManager() override;
    [[nodiscard]] IStatusBarService* getStatusBarService() override;
    [[nodiscard]] IActionStateManager* getActionStateManager() override;
    [[nodiscard]] IEngineSessionProvider* getEngineSessionProvider() override;
    [[nodiscard]] IRawOpenPolicyService* getRawOpenPolicyService() override;
    [[nodiscard]] QWidget* getMainWindow() override;

    // -- IDialogService --
    void openSettingsAt(const QString& pageName) override;
    void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) override;
    void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text) override;

    // -- IGlobalSettingsProvider --
    bool upsert(const QString& key, const QString& value) override;
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override;
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override;

    // -- IInterfaceSettingsRuntimeService --
    void applyTheme(const QString& themeId) override;
    void refreshAllZoomSettings() override;

    // -- IProcessSettingsProvider --
    bool upsert(const QString& processName, int win_state, int width, int height,
                const QString& json_settings) override;
    [[nodiscard]] bool select(const QString& processName, int& win_state, int& width, int& height,
                              QString& json_settings) override;

    // -- ITempFileService --
    [[nodiscard]] bool isTempFile(const std::string& fileName) override;
    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override;
    [[nodiscard]] QString getTempDirPath() const override;

    // -- IPathMemoryService --
    [[nodiscard]] QString selectPath(const QString& contextKey, const QString& defaultValue = {}) override;
    bool upsertPath(const QString& contextKey, const QString& path) override;

    // -- IFileDialogService --
    [[nodiscard]] QStringList selectImageFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                               bool supportMultipleFiles) override;
    [[nodiscard]] QStringList selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                          const QStringList& fileExtensions) override;
    [[nodiscard]] QStringList saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                                        const QString& folderContext, const QStringList& fileExtensions) override;

    // -- IViewFactory --
    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(
        ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const std::string& historyDesc,
        ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour, bool pendingChanges) override;
    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl* customViewControl, ImageDocument&& document,
                                          QWidget* parent, bool autoDebayer, const std::string& historyDesc,
                                          ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                          bool pendingChanges) override;

    // -- IViewRegistry --
    [[nodiscard]] ImageViewControl* getActiveImageViewControl() override;
    void setActiveImageViewControl(ImageViewControl* view) override;

    // -- IAppEventBus --
    void subscribeListener(AfterNightAppListener* listener) override;
    void unsubscribeListener(AfterNightAppListener* listener) override;
    void postAfterNightEvent(AfterNightEvent* event, int priority) override;
    void notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl) override;
    void notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl) override;

    // -- IProcessRegistry --
    void registerProcessInstance(TopLevelProcessBaseControl* process) override;
    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override;

    // -- IOutputIntentProvider --
    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const override;

    // -- IRawOpenPolicyService --
    [[nodiscard]] std::shared_ptr<const RawOpenPolicy> rawOpenPolicy() const override;
    an::Status setRawOpenSettings(const RawOpenSettings& settings) override;

    // -- IRecentFilesService --
    [[nodiscard]] std::deque<std::string> getRecentFilesList() const override;
    void openRecentSingleFile(const QString& filePath) override;
    void openDroppedFiles(const QStringList& files) override;

    // -- IThumbnailPathProvider --
    [[nodiscard]] QString selectThumbnailPath(const QString& filePath) const override;

    // -- IViewControlManager --
    void addViewControl(ImageViewControl* view) override;
    void removeViewControl(ImageViewControl* view, bool deleteObject) override;
    void renameViewControl(ImageViewControl* view) override;
    void maximizeViewControl(ImageViewControl* ctrl) override;
    void restoreViewControl(ImageViewControl* ctrl) override;
    void notifyViewControlUpdated(const ImageViewControl* ctrl) override;
    void createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event) override;
    [[nodiscard]] bool supportsDockWidgets() const override;
    [[nodiscard]] ImageViewControl* getViewControlByName(const std::string& name) override;
    [[nodiscard]] std::vector<ImageViewControl*> getListOfViewControls() override;
    [[nodiscard]] std::string createViewControlUniqueName(const std::string& name) override;

    // -- IStatusBarService --
    void updateImageInfoInStatusBar() override;
    void updateImageZoomInfoInStatusBar() override;
    void updateImageCelestialCoordsInStatusBar(double ra_deg, double dec_deg) override;
    void clearImageCelestialCoordsInStatusBar() override;

    // -- IActionStateManager --
    void setCustomImageActionEnabled(ImageViewControl* view, GlobalViewControlActions action, bool enable) override;
    void updateActiveImageActions() override;
    void updateActiveImageHistory() override;

private:
    AfterNightApp* m_app;
};

} // namespace an::ui
