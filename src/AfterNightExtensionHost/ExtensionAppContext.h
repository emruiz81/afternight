// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/IAppContext.h"

#include <QHash>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class IconManager;
using IconManagerSptr = std::shared_ptr<IconManager>;

namespace an::ui::extensions
{

class ExtensionAppContext final : public IAppContext,
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
                                  public IRecentFilesService,
                                  public IThumbnailPathProvider,
                                  public IViewControlManager,
                                  public IStatusBarService,
                                  public IActionStateManager
{
public:
    struct DialogCallbacks
    {
        std::function<void(const QString&)> open_settings_at;
        std::function<void(QWidget*, const QString&, const QString&, bool, int*)> message_box_yes_no;
        std::function<void(QWidget*, const QString&, const QString&)> notify_error;
        std::function<void(QWidget*, const QString&, const QString&)> notify_error_async;
        std::function<void(QWidget*, const QString&, const QString&)> notify_warn;
        std::function<void(QWidget*, const QString&, const QString&)> notify_warn_async;
        std::function<void(QWidget*, const QString&, const QString&)> notify_info;
        std::function<void(QWidget*, const QString&, const QString&)> notify_info_async;
    };

    /// Makes a host-local view of a document; a view made takes the document, and one not made leaves it with the
    /// caller, as `IViewFactory` says.
    using ViewFactoryCallback =
        std::function<ImageViewControl*(ImageViewControl*, ImageDocument&&, QWidget*, const std::string&,
                                        ViewControlMode, ImageViewControl*, bool, bool)>;

    ExtensionAppContext();
    ~ExtensionAppContext() override;

    void setMainWindow(QWidget* main_window) noexcept;
    void setSessionDirectories(const std::filesystem::path& temp_dir, const std::filesystem::path& artifacts_dir = {});
    void setDialogCallbacks(DialogCallbacks callbacks);
    void setViewFactoryCallback(ViewFactoryCallback callback);

    /// Lends the session the host borrowed from the runtime backend; null until it is borrowed and once it is let go.
    /// The provider outlives every native window made while it is lent.
    void setEngineSessionProvider(IEngineSessionProvider* provider) noexcept;

    [[nodiscard]] QString artifactsDirPath() const;
    [[nodiscard]] std::size_t listenerCount() const noexcept;
    [[nodiscard]] std::size_t trackedViewCount() const noexcept;
    [[nodiscard]] std::size_t registeredProcessCount() const noexcept;

    void notifyViewCreated(const ImageViewControl* ctrl);
    void notifyViewDestroyed(const ImageViewControl* ctrl);
    void notifyViewUpdated(const ImageViewControl* ctrl);
    void notifyActiveViewChanged(ImageViewControl* ctrl);
    void notifyViewRenamed(const std::string& new_name, const std::string& old_name, const ImageViewControl* ctrl);

    [[nodiscard]] IIconProviderPtr getIconProvider() override;
    void reloadIcons(bool dark_mode);
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

    /// The host publishes no RAW policy: its views show the images the application hands it, and read no camera file.
    [[nodiscard]] IRawOpenPolicyService* getRawOpenPolicyService() override;
    [[nodiscard]] QWidget* getMainWindow() override;

    void openSettingsAt(const QString& pageName) override;
    void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) override;
    void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text) override;

    bool upsert(const QString& key, const QString& value) override;
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override;
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override;

    void applyTheme(const QString& themeId) override;
    void refreshAllZoomSettings() override;

    bool upsert(const QString& processName, int win_state, int width, int height,
                const QString& json_settings) override;
    [[nodiscard]] bool select(const QString& processName, int& win_state, int& width, int& height,
                              QString& json_settings) override;

    [[nodiscard]] bool isTempFile(const std::string& fileName) override;
    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override;
    [[nodiscard]] QString getTempDirPath() const override;

    [[nodiscard]] QString selectPath(const QString& contextKey, const QString& defaultValue = {}) override;
    bool upsertPath(const QString& contextKey, const QString& path) override;

    [[nodiscard]] QStringList selectImageFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                               bool supportMultipleFiles) override;
    [[nodiscard]] QStringList selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                          const QStringList& fileExtensions) override;
    [[nodiscard]] QStringList saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                                        const QString& folderContext, const QStringList& fileExtensions) override;

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(
        ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const std::string& historyDesc,
        ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour, bool pendingChanges) override;

    /// The host makes no sequence view: the document stays with the caller.
    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl* customViewControl, ImageDocument&& document,
                                          QWidget* parent, bool autoDebayer, const std::string& historyDesc,
                                          ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                          bool pendingChanges) override;

    [[nodiscard]] ImageViewControl* getActiveImageViewControl() override;
    void setActiveImageViewControl(ImageViewControl* view) override;

    void subscribeListener(AfterNightAppListener* listener) override;
    void unsubscribeListener(AfterNightAppListener* listener) override;
    void postAfterNightEvent(AfterNightEvent* event, int priority) override;
    void notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl) override;
    void notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl) override;

    void registerProcessInstance(TopLevelProcessBaseControl* process) override;
    void unregisterProcessInstance(TopLevelProcessBaseControl* process) override;

    /// The intent the export settings stored in this context give, through the product's codecs, as the application's
    /// persistence builds it.
    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const override;

    [[nodiscard]] std::deque<std::string> getRecentFilesList() const override;
    void openRecentSingleFile(const QString& filePath) override;
    void openDroppedFiles(const QStringList& files) override;

    [[nodiscard]] QString selectThumbnailPath(const QString& filePath) const override;

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

    void updateImageInfoInStatusBar() override;
    void updateImageZoomInfoInStatusBar() override;
    void updateImageCelestialCoordsInStatusBar(double ra_deg, double dec_deg) override;
    void clearImageCelestialCoordsInStatusBar() override;

    void setCustomImageActionEnabled(ImageViewControl* view, GlobalViewControlActions action, bool enable) override;
    void updateActiveImageActions() override;
    void updateActiveImageHistory() override;

private:
    class EventDispatcher;

    struct ProcessSettingsRecord
    {
        int win_state = 0;
        int width = -1;
        int height = -1;
        QString json_settings;
    };

    void notifyListeners(const std::function<void(AfterNightAppListener*)>& notifier);

    QWidget* main_window_ = nullptr;
    IconManagerSptr icon_manager_;
    IIconProviderPtr icon_provider_;
    DialogCallbacks dialog_callbacks_;
    ViewFactoryCallback view_factory_callback_;
    IEngineSessionProvider* engine_session_provider_ = nullptr;
    QHash<QString, QString> global_settings_;
    QHash<QString, ProcessSettingsRecord> process_settings_;
    QHash<QString, QString> path_memory_;
    QString temp_dir_path_;
    QString artifacts_dir_path_;
    mutable int next_temp_sequence_id_ = 0;
    EventDispatcher* event_dispatcher_ = nullptr;
    std::vector<AfterNightAppListener*> listeners_;
    std::vector<ImageViewControl*> view_controls_;
    std::unordered_map<ImageViewControl*, std::string> view_names_by_address_;
    std::unordered_map<std::string, ImageViewControl*> view_controls_by_name_;
    std::vector<TopLevelProcessBaseControl*> processes_;
    ImageViewControl* active_view_control_ = nullptr;
};

} // namespace an::ui::extensions