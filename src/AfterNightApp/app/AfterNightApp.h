// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QMainWindow>
#include <QApplication>
#include <QFileDialog>
#include <QMetaObject>
#include <QMutex>
#include <QRecursiveMutex>
#include <QThread>

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/ProgressDialogControl.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"

#include "app/AfterNightEvent.h"
#include "app/FilePolicies.h"
#include "app/GuiThreadInvoke.h"
#include "app/IconManager.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/DockWidget.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

namespace an
{
class ImageDescriptor;
class OutputIntent;
enum class CodecFormat : std::uint8_t;
enum class SequenceFormat : std::uint8_t;
} // namespace an

namespace an::consumer
{
class ImageAsset;
} // namespace an::consumer

namespace an::ui::app
{
class AfterNightAppUiElements;
}

namespace an::ui::extensions
{
struct ExtensionLaunchPreparationOutcome;
}

class AfterNightAppListener;
class ImageInfoStatusBarWidget;
class ImageViewControl;
class LogViewerControl;
class MainDashboard;
class QComboBox;
class QCloseEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QScrollArea;
class QSqlDatabase;
class QTimer;
class TopLevelProcessBaseControl;

#include "controls/GlobalViewControlActions.h"

namespace an::ui
{
class ApplicationSession;
class IEngineSessionProvider;
class ImageDocument;
class ProcessLauncher;
class ChannelExtractor;
class FileManager;
class MenuActionManager;
class ThemeManager;
class ViewControlCoordinator;
class ViewCreationCoordinator;
class ImageOperationsCoordinator;
class MaskOverlayCoordinator;
class DialogOrchestrationCoordinator;
class RuntimeStorageCoordinator;
class UiBootstrapCoordinator;
class ActionStateCoordinator;
class WindowLayoutCoordinator;
class AppLifecycleCoordinator;
class PersistenceCoordinator;
} // namespace an::ui

namespace an::ui::extensions
{
class ExtensionHostLaunchCoordinator;
}

class AfterNightApp : public QMainWindow
{
public:
    [[nodiscard]] static AfterNightApp* getInstance();

    [[nodiscard]] static AfterNightApp* instanceOrNull() noexcept;

    static void destroyInstance();

    /// Reads the settings store, opens the session the application owns from the processing limits it states, and
    /// readies everything else the window needs. The session is the caller's: it outlives this window, and the caller
    /// shuts it down once the window is gone.
    void initialize(an::ui::ApplicationSession& session);

    /// The session Controls runs in, once `initialize` opened it; null before and after a failed open.
    [[nodiscard]] an::ui::IEngineSessionProvider* engineSessionProvider() const noexcept;

    void setActiveImageViewControl(ImageViewControl* view);
    void setActiveImageViewControlAsync(ImageViewControl* view, std::atomic_bool* finished = nullptr);

    void setActiveImageViewControlByName(const std::string& name);
    void setActiveImageViewControlByNameAsync(const std::string& name, std::atomic_bool* finished = nullptr);

    void addImageViewControl(ImageViewControl* view);
    void addImageViewControlAsync(ImageViewControl* view);

    void removeImageViewControl(ImageViewControl* view, bool deleteObject);
    void removeImageViewControlAsync(ImageViewControl* view, bool deleteObject);

    void renameImageViewControl(ImageViewControl* view);

    void updateImageViewControlList();
    void updateImageViewControlListAsync(std::atomic_bool* finished = nullptr);

    [[nodiscard]] ImageViewControlManager* getImageViewControlManager();

    void createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event);

    // Called only in the main thread
    [[nodiscard]] ImageViewControl* createNewImageViewControl(const std::string& name, QWidget* parent = nullptr,
                                                              const ViewControlMode mode = ViewControlMode::standard,
                                                              const bool pendingChanges = true);
    // The view factory, on the GUI thread: a view made takes the document, and a document no view is made for is
    // left with the caller as it was handed in.
    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(
        ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent = nullptr,
        const std::string& historyDesc = "", const ViewControlMode mode = ViewControlMode::standard,
        ImageViewControl* neighbour = nullptr, bool keepFocusedNeighbour = false, const bool pendingChanges = true);
    [[nodiscard]] ImageViewControl* createNewImageViewControlFromSequence(
        ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent = nullptr,
        const bool autoDebayer = false, const std::string& historyDesc = "",
        const ViewControlMode mode = ViewControlMode::standard, ImageViewControl* neighbour = nullptr,
        bool keepFocusedNeighbour = false, const bool pendingChanges = true);

    // The view factory from any thread, which never waits for the GUI thread: the document travels in the event, and
    // the event closes it when no view is made for it. `finished` is set once the event has run.
    void createNewImageViewControlFromImageAsync(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                                 QWidget* parent = nullptr, const std::string& historyDesc = "",
                                                 const ViewControlMode mode = ViewControlMode::standard,
                                                 ImageViewControl* neighbour = nullptr,
                                                 bool keepFocusedNeighbour = false, const bool pendingChanges = true,
                                                 std::atomic_bool* finished = nullptr);
    void createNewImageViewControlFromSequenceAsync(ImageViewControl* customViewControl,
                                                    an::ui::ImageDocument&& document, QWidget* parent = nullptr,
                                                    const bool autoDebayer = false, const std::string& historyDesc = "",
                                                    const ViewControlMode mode = ViewControlMode::standard,
                                                    ImageViewControl* neighbour = nullptr,
                                                    bool keepFocusedNeighbour = false, const bool pendingChanges = true,
                                                    std::atomic_bool* finished = nullptr);

    void updateActiveImageHistory();

    void setCustomImageActionEnabled(ImageViewControl* view, const GlobalViewControlActions action, bool enable);

    void updateImageInfoInStatusBar();
    void updateImageZoomInfoInStatusBar();
    void updateImageCelestialCoordsInStatusBar(double ra_deg, double dec_deg);
    void clearImageCelestialCoordsInStatusBar();

    void updateActiveImageActions();
    void updateActiveImageActionsAsync();

    void subscribeListener(AfterNightAppListener* listener);
    void unsubscribeListener(AfterNightAppListener* listener);

    void notifyViewControlCreated(const ImageViewControl* ctrl);
    void notifyActiveViewControlChanged(ImageViewControl* ctrl);

    void notifyViewControlDestroyed(const ImageViewControl* ctrl);

    void notifyViewControlUpdated(const ImageViewControl* ctrl);

    void notifyViewControlRenamed(const std::string& newName, const std::string& oldName, const ImageViewControl* ctrl);

    void notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl);

    void notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl);

    void notifyApplicationClose();

    void setLastOpenDirectoryAndPersist(const QString& path);

    QStringList selectImageFilesDlg(QWidget* parent, const QString& title, const QString& folderContext,
                                    bool supportMultipleFiles = true);

    QStringList selectFilesDlg(QWidget* parent, const QString& title, const QString& folderContext,
                               const QStringList& fileExtensions);

    // defaultDir takes priority over folderContext. If defaultDir doesn't exists, or is empty, then look at
    // folderContext (if any). folderContext is updated if provided, even if defaultDir was provided
    QStringList saveFileDlg(QWidget* parent, const QString& title, const QString& defaultDir,
                            const QString& folderContext, const QStringList& fileExtensions);

    QString getExistingDirectory(QWidget* parent, const QString& title = QString(), const QString& dir = QString(),
                                 QFileDialog::Options options = QFileDialog::ShowDirsOnly);

    void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text);
    void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text);

    void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text);
    void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text);

    void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text);
    void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text);

    void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret);
    void messageBoxYesNoAsync(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret,
                              std::atomic_bool* finished);

    void runCustomCode(const std::function<void()>& func);
    void runCustomCodeAsync(const std::function<void()>& func);

    void cleanTemporaryFiles();

    void cleanTemporaryFilesAsync();
    void cleanStaleScratchRootsAsync();

    void ensureTempDirCreated();

    [[nodiscard]] QString getTempDirPath() const;

    void ensureThumbnailsDirCreated();

    [[nodiscard]] QString getThumbnailsDirPath() const;

    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const;

    [[nodiscard]] bool isDarkMode() const;

    /// Apply the specified theme to the application
    /// @param themeId The theme identifier: "system", "light", or "dark"
    void applyTheme(const QString& themeId);

    /// Refresh cached zoom settings for all image view controls
    void refreshAllZoomSettings();

    /// Show the main window, restoring saved geometry if the setting is enabled
    void showWithSavedGeometry();

    void maximizeViewControl(ImageViewControl* ctrl);

    void restoreViewControl(ImageViewControl* ctrl);

    [[nodiscard]] IconManagerSptr getIconManager();

    void initializeMainDashboard();
    void initializeDockManager();
    void initializeMaximizedView();

    void switchToMainDashboard();
    void switchToDockManager();

    [[nodiscard]] std::deque<std::string> getRecentFilesList() const;

    [[nodiscard]] size_t getRecentFilesLimit() const;
    void openRecentSingleFile(const QString& filePath);

    void openDroppedFiles(const QStringList& files);

    void newSequence();

    void openSettings();

    /// Open the settings dialog and navigate to a specific page
    /// @param pageName The title of the page to navigate to (e.g., "External Tools")
    void openSettingsAt(const QString& pageName);

    void about();
    void openHelp();

    void systemInformation();

    void postAfterNightEvent(AfterNightEvent* evt, int priority = 0);

    void addRecentFile(const std::string& fileName, const std::shared_ptr<const an::consumer::ImageAsset>& image);
    void addRecentFileAsync(const std::string& fileName, std::shared_ptr<const an::consumer::ImageAsset> image,
                            std::atomic_bool* finished = nullptr);

    void removeRecentFile(const std::string& fileName);
    void removeRecentFileAsync(const std::string& fileName, std::atomic_bool* finished = nullptr);

    void refreshStoredProcessMenus();

    void setLogViewerChecked(bool checked);

    void onLogViewerClosed();

    [[nodiscard]] std::thread::id getMainThreadId() const;

    [[nodiscard]] bool isMainThread() const;

    [[nodiscard]] bool isTempFile(const std::string& fileName);

    void registerProcessInstance(TopLevelProcessBaseControl* process);
    void unregisterProcessInstance(TopLevelProcessBaseControl* process);

    [[nodiscard]] size_t getNumOfOpenProcesses();

    [[nodiscard]] bool layoutIsLocked() const;

    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* getDockMainWindow();

    [[nodiscard]] ThumbnailPersistence* getThumbnailPersistence();
    [[nodiscard]] FolderTrackerPersistence* getFolderTrackerPersistence();
    [[nodiscard]] ProcessSettingsPersistence* getProcessSettingsPersistence();
    [[nodiscard]] GlobalSettingsPersistence* getGlobalSettingsPersistence();

    /// The RAW policy every read of a camera file is submitted under: the one published last, or null before the
    /// settings store is read or when no policy could be built.
    [[nodiscard]] std::shared_ptr<const an::ui::RawOpenPolicy> rawOpenPolicy() const;

    /// Stores the RAW settings and publishes their policy for the reads asked for after it.
    ///
    /// @return success, or why no policy could be built - the settings are stored either way - or that the settings
    ///         store is not open yet, in which case nothing is stored.
    an::Status setRawOpenSettings(const an::ui::RawOpenSettings& settings);

    /// The output intent the export settings give for writing an image described by `source` into `container`.
    ///
    /// @return the intent, or why none can be built - also that the settings store is not open yet.
    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const;

    [[nodiscard]] bool isMaximizedViewActive() const;

    [[nodiscard]] an::ui::ProcessLauncher* processLauncher() const;
    [[nodiscard]] an::ui::ChannelExtractor* channelExtractor() const;
    [[nodiscard]] an::ui::ImageOperationsCoordinator* imageOperations() const;

    ~AfterNightApp();

private:
    friend class an::ui::ThemeManager;
    friend class an::ui::MenuActionManager;
    friend class an::ui::FileManager;

    [[nodiscard]] an::ui::FileManager* fileManager() const;
    [[nodiscard]] an::ui::MaskOverlayCoordinator* maskOverlay() const;
    [[nodiscard]] an::ui::WindowLayoutCoordinator* windowLayout() const;
    [[nodiscard]] an::ui::DialogOrchestrationCoordinator* dialogOrchestration() const;
    [[nodiscard]] an::ui::ActionStateCoordinator* actionState() const;

    void on_imageViewControlListComboBox_currentTextChanged(const QString& text);

    void on_LogViewerClicked(bool checked);

private:
    AfterNightApp();

    static AfterNightApp* m_Instance;

    void closeEvent(QCloseEvent* closeEvent) override;

    void contextMenuEvent(QContextMenuEvent* contextMenuEvent) override;

    void dragEnterEvent(QDragEnterEvent* event) override;

    void dropEvent(QDropEvent* event) override;

    void customEvent(QEvent* event) override;

    void connectActions();

    void setActionIcons();

    bool tryLaunchRegistryProcessById(const std::string& processId);

    /// Completes the launch of a process whose extension's environment a launch prepared: the registry, the launches
    /// and the menus take what the packages are now, and the process is launched when its environment was made ready,
    /// or what still stands in the way reported once, with where to deal with it.
    ///
    /// @param processId the descriptor that was launched.
    /// @param extensionId the extension the descriptor belongs to.
    /// @param outcome what the preparation came to.
    void completePreparedExtensionLaunch(const std::string& processId, const std::string& extensionId,
                                         const an::ui::extensions::ExtensionLaunchPreparationOutcome& outcome);

    void initializePersistence();

    void persistLastOpenDir();

    void persistRecentFiles();
    [[nodiscard]] size_t getRecentProcessesLimit() const;
    void enforceRecentProcessesLimit();
    void loadRecentProcessesFromSettings();
    void persistRecentProcessesToSettings();
    void recordRecentProcess(const std::string& processId, const std::string& displayName);
    void updateRecentProcessesMenuEntries();
    void clearRecentProcessesList();
    void loadFavoriteProcessesFromSettings();
    void persistFavoriteProcessesToSettings();
    void updateFavoriteProcessesMenuEntries();
    void openManageFavoriteProcessesDialog();
    void clearFavoriteProcessesList();

    void saveWindowGeometry();
    void stopExtensionHostSessions();

    void loadSettings();

    void lockLayout();
    void unlockLayout();
    void toogleLayout();

    void setEnableImageActions(bool enable);

    void refreshTheme();

    void loadIcons();

    void createAuxToolBars();

    void splitAllImages(ImageViewControl* currentViewControl);

    void groupAllImages(ImageViewControl* currentViewControl);

    void fitAllImagesToWindow(ImageViewControl* currentViewControl);

    void splitAllImages();

    void groupAllImages();

    bool viewControlInAMaximizedContainer(ImageViewControl* control);

    ImageViewControl* getviewControlInAMaximizedContainer(ImageViewControl* control);

    // Only for ImageViewControl objects, not DockableControl (not including LoggerViewControl for example)
    std::map<KDDockWidgets::QtWidgets::DockWidget*, ImageViewControl*> getViewControlsByDockWidgetMap();
    std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*> getDockWidgetsByViewControlMap();

    std::list<ImageViewControl*> getListOfMaximizedViewControls();
    std::list<ImageViewControl*> getListOfViewControls();

    std::list<ImageViewControl*> getListOfViewControlsSharingTheSameWindow(ImageViewControl* currentViewControl);

    std::set<std::string> getListOfTemporaryDirs();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
