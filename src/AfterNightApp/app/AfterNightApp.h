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

#include "core/IShell.h"

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
class WindowLayoutCoordinator;
class AppLifecycleCoordinator;
class PersistenceCoordinator;
} // namespace an::ui

namespace an::ui::shell
{
class ApplicationCore;
}

/// The classic window: the main window of menus, a toolbar and a status bar over the dashboard, the dock area and the
/// page of a maximized view. It is the classic shell - what only a window can answer for the application core's
/// services: where a view goes, what the status line shows, whether the Session Console is on screen.
class AfterNightApp : public QMainWindow,
                      public an::ui::shell::IShell,
                      public an::ui::shell::IViewHost,
                      public an::ui::shell::IStatusDisplay
{
public:
    /// Makes the main window over the application core `main` made, which outlives it; called once.
    [[nodiscard]] static AfterNightApp* createInstance(an::ui::shell::ApplicationCore& core);

    /// The main window `createInstance` made; null before.
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

    void notifyViewControlUpdated(const ImageViewControl* ctrl);

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
    void showWithSavedGeometry() override;

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

    [[nodiscard]] bool isMaximizedViewActive() const override;

    // ---- The shell

    /// The window is built when it is made: nothing is left to build.
    void build() override;
    [[nodiscard]] QWidget* mainWindow() override;
    [[nodiscard]] an::ui::shell::IViewHost& viewHost() override;
    [[nodiscard]] an::ui::shell::IStatusDisplay& statusDisplay() override;
    /// Null: every process is a window of its own.
    [[nodiscard]] an::ui::IProcessPresenter* processPresenter() override;
    void setSessionConsoleVisible(bool visible) override;
    [[nodiscard]] bool isSessionConsoleVisible() const override;
    void refreshTheme() override;
    void saveGeometry() override;

    // ---- Where the views are

    /// Docks a new view beside its neighbour, or on the right of the dock area; a view maximized on its page goes back
    /// to the dock area first, and the dock area is shown in place of the dashboard.
    void place(ImageViewControl& view, const an::ui::shell::ViewPlacement& placement) override;
    /// Selects the view in the image selector and brings its dock forward.
    void activate(ImageViewControl& view) override;
    /// Lists the view in the image selector.
    void viewAdded(ImageViewControl& view, an::ui::shell::ViewOrigin origin) override;
    void viewRemoved(ImageViewControl& view) override;
    void viewRenamed(ImageViewControl& view, const std::string& previousName) override;
    void maximize(ImageViewControl& view) override;
    void restore(ImageViewControl& view) override;
    /// The one dock area the window has.
    [[nodiscard]] an::ui::shell::DockTarget dockTarget(const ImageViewControl* view = nullptr) override;
    /// Shows the dashboard, unless the Session Console holds the dock area.
    void showHome() override;
    /// Shows the dock area.
    void showViews() override;

    // ---- The status line

    void showImageInfo(ImageViewControl* view) override;
    void showZoom(ImageViewControl* view) override;
    void showCelestialCoordinates(double raDegrees, double decDegrees) override;
    void clearCelestialCoordinates() override;
    void clearImageInfo() override;

    [[nodiscard]] an::ui::ProcessLauncher* processLauncher() const;
    [[nodiscard]] an::ui::ChannelExtractor* channelExtractor() const;
    [[nodiscard]] an::ui::ImageOperationsCoordinator* imageOperations() const;

    ~AfterNightApp();

private:
    friend class an::ui::ThemeManager;
    friend class an::ui::FileManager;

    [[nodiscard]] an::ui::FileManager* fileManager() const;
    [[nodiscard]] an::ui::MaskOverlayCoordinator* maskOverlay() const;
    [[nodiscard]] an::ui::WindowLayoutCoordinator* windowLayout() const;
    [[nodiscard]] an::ui::DialogOrchestrationCoordinator* dialogOrchestration() const;

    void on_imageViewControlListComboBox_currentTextChanged(const QString& text);

    void on_LogViewerClicked(bool checked);

private:
    explicit AfterNightApp(an::ui::shell::ApplicationCore& core);

    static AfterNightApp* m_Instance;

    void closeEvent(QCloseEvent* closeEvent) override;

    void contextMenuEvent(QContextMenuEvent* contextMenuEvent) override;

    void dragEnterEvent(QDragEnterEvent* event) override;

    void dropEvent(QDropEvent* event) override;

    void connectActions();

    void setActionIcons();

    bool tryLaunchRegistryProcessById(const std::string& processId);

    void initializePersistence();

    void persistLastOpenDir();

    void persistRecentFiles();
    void updateRecentProcessesMenuEntries();
    void clearRecentProcessesList();
    void updateFavoriteProcessesMenuEntries();
    /// The extensions' processes in the category menus and their families in the Extensions menu, as the core's process
    /// menus hold them.
    void updateExtensionMenuEntries();
    void openManageFavoriteProcessesDialog();
    void clearFavoriteProcessesList();

    void saveWindowGeometry();
    void stopExtensionHostSessions();

    void loadSettings();

    void lockLayout();
    void unlockLayout();
    void toogleLayout();

    void setEnableImageActions(bool enable);

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
