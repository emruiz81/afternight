// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application without a window: its stores, its lists and registries, and the work threads hand the GUI thread.

#include "AfterNightShellCoreExport.h"

#include "controls/ViewControlMode.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

class AfterNightEvent;
class FolderTrackerPersistence;
class IconManager;
class GlobalSettingsPersistence;
class ImageViewControl;
class PersistenceDbExecutor;
class ProcessSettingsPersistence;
class QEvent;
class QWidget;
class ThumbnailPersistence;

namespace an
{
template <typename ValueT> class Result;
class ImageDescriptor;
class OutputIntent;
class Status;
enum class CodecFormat : std::uint8_t;
} // namespace an

namespace an::consumer
{
class ImageAsset;
}

namespace an::ui
{
class IEngineSessionProvider;
class IGlobalSettingsProvider;
class ImageDocument;
class PersistenceCoordinator;
class RawOpenPolicy;
class RuntimeStorageCoordinator;
struct RawOpenSettings;
} // namespace an::ui

namespace an::ui::shell
{

class ActionBindings;
class ActionCatalogue;
class ActionStateCoordinator;
class AppEventBus;
class DialogService;
class DockLayoutOperations;
class ExtensionService;
class FavoriteProcessesModel;
class FileDialogService;
class FileDropHandler;
class IApplicationEventTarget;
class IShell;
class OpenProcessRegistry;
class ProcessLaunchService;
class ProcessMenuModel;
class RecentFilesModel;
class RecentProcessesModel;
class ViewService;

/// What the application is without a window: what a shell needs and none owns.
///
/// It holds the persistence stores - the settings database and the stores kept in it - and opens them before any
/// widget exists, so a shell can be chosen from a setting before the style it asks for is set; the folder last opened;
/// the temporary and thumbnail folders; the application's listeners; the process windows that are open; the files and
/// processes used last and the favourite processes. It receives the work a thread hands the GUI thread and runs it
/// there against the target it was given.
///
/// It holds the services the application context answers with - the messages, the file dialogs, the views, the launch
/// of processes, the installed extensions - which answer what only a window can through the shell they are given
/// (`setShell`); and the application's actions, what they do, which of them are offered and the process menus' data.
///
/// `main` makes it on the GUI thread before the window and hands it to the window, which wires its own units to what
/// the core holds; the core outlives the window. It makes no widget.
class AFTERNIGHTSHELLCORE_API ApplicationCore : public QObject
{
public:
    ApplicationCore();
    ~ApplicationCore() override;

    ApplicationCore(const ApplicationCore&) = delete;
    ApplicationCore& operator=(const ApplicationCore&) = delete;
    ApplicationCore(ApplicationCore&&) = delete;
    ApplicationCore& operator=(ApplicationCore&&) = delete;

    // ---- The stores

    /// Opens the settings database under the application's data folder, and the stores kept in it, makes no widget,
    /// and then reads what lives there: the folder last opened, the RAW policy, the recent files; and makes the
    /// temporary and thumbnail folders. A database of another format is set aside first (`settingsStoreNotice`).
    ///
    /// @throws std::runtime_error when the database cannot be opened or made, as the application reports it.
    void openStores();

    /// Closes the stores, then the database's executor; the lists keep what they hold. Closing closed stores does
    /// nothing.
    void closeStores() noexcept;

    [[nodiscard]] bool storesOpen() const noexcept;

    /// What a person should be told about the database the stores opened: that one of another format was set aside,
    /// and where; empty when there is nothing to tell.
    [[nodiscard]] const QString& settingsStoreNotice() const noexcept;

    /// The application's data folder, once the stores were opened.
    [[nodiscard]] const QString& localDataPath() const noexcept;

    /// The stores; null while they are closed.
    [[nodiscard]] ThumbnailPersistence* thumbnailStore() const noexcept;
    [[nodiscard]] FolderTrackerPersistence* folderStore() const noexcept;
    [[nodiscard]] ProcessSettingsPersistence* processSettingsStore() const noexcept;
    [[nodiscard]] GlobalSettingsPersistence* globalSettingsStore() const noexcept;

    /// The global settings store as the settings interface answers it: never null; while the store is closed a read
    /// answers the default and a write is refused.
    [[nodiscard]] an::ui::IGlobalSettingsProvider* globalSettings() noexcept;

    /// The RAW policy every read of a camera file is submitted under: the one published last, or null while the stores
    /// are closed or when no policy could be built.
    [[nodiscard]] std::shared_ptr<const an::ui::RawOpenPolicy> rawOpenPolicy() const;

    /// Stores the RAW settings and publishes their policy for the reads asked for after it.
    /// @return success, or why no policy could be built - the settings are stored either way - or that the stores are
    ///         not open, in which case nothing is stored.
    an::Status setRawOpenSettings(const an::ui::RawOpenSettings& settings);

    /// The output intent the export settings give for writing an image described by `source` into `container`.
    /// @return the intent, or why none can be built - also that the stores are not open.
    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const;

    // ---- The folder last opened

    [[nodiscard]] const std::string& lastOpenDirectory() const noexcept;

    /// The folder last opened, for the file workflows, which set it in place and then store it.
    [[nodiscard]] std::string& lastOpenDirectoryForEdit() noexcept;

    /// Makes `path` the folder last opened and stores it - a folder on another machine is kept for the run, never
    /// stored.
    void setLastOpenDirectory(const QString& path);

    /// Stores the folder last opened, unless it is on another machine.
    void persistLastOpenDirectory();

    // ---- The temporary and thumbnail folders

    [[nodiscard]] an::ui::RuntimeStorageCoordinator& runtimeStorage() noexcept;

    /// Whether `fileName` lies in this run's temporary folder.
    [[nodiscard]] bool isTempFile(const std::string& fileName) const;

    // ---- The lists and registries

    [[nodiscard]] AppEventBus& eventBus() noexcept;
    [[nodiscard]] OpenProcessRegistry& openProcesses() noexcept;
    [[nodiscard]] RecentFilesModel& recentFiles() noexcept;
    [[nodiscard]] RecentProcessesModel& recentProcesses() noexcept;
    [[nodiscard]] FavoriteProcessesModel& favoriteProcesses() noexcept;

    // ---- The services

    /// The messages, and where the settings are opened.
    [[nodiscard]] DialogService& dialogService() noexcept;

    /// The file dialogs and the folders they remember.
    [[nodiscard]] FileDialogService& fileDialogService() noexcept;

    /// The views, and what the shell is told of them.
    [[nodiscard]] ViewService& viewService() noexcept;

    /// The launch of the registry's processes, the extensions' included.
    [[nodiscard]] ProcessLaunchService& processLaunch() noexcept;

    /// The installed extensions and the host sessions their processes run in.
    [[nodiscard]] ExtensionService& extensions() noexcept;

    /// The icons the application draws with, in the theme's colours.
    [[nodiscard]] const std::shared_ptr<IconManager>& iconManager() const noexcept;

    // ---- The actions

    /// The application's actions, children of the core unless a shell's window hosts them.
    [[nodiscard]] ActionCatalogue& actions() noexcept;

    /// What the actions do: they are connected once the shell's targets are given (`ActionBindings::setTargets`).
    [[nodiscard]] ActionBindings& actionBindings() noexcept;

    /// Which actions are offered, from the image the image actions act on.
    [[nodiscard]] ActionStateCoordinator& actionState() noexcept;

    /// The process menus as data: the favourite and recent processes, and what the extensions add.
    [[nodiscard]] ProcessMenuModel& processMenus() noexcept;

    // ---- The arrangement of the views

    /// Group, split and fit the views, and lock their arrangement, on a dock area the shell names. The dock library is
    /// configured, once for the process, as the core is made (`DockHooks`): before any shell makes a main window.
    [[nodiscard]] DockLayoutOperations& dockLayout() noexcept;

    /// What a drop of files on the shell's window opens.
    [[nodiscard]] FileDropHandler& fileDrops() noexcept;

    // ---- The session

    /// The engine session the application opened, which the core lends the context; null until one is open. The
    /// session is the caller's and must outlive what it lent, or be taken back first.
    void setEngineSessionProvider(an::ui::IEngineSessionProvider* provider) noexcept;
    [[nodiscard]] an::ui::IEngineSessionProvider* engineSessionProvider() const noexcept;

    // ---- The shell

    /// The shell the services answer through - where a view goes, what a dialog belongs to, what the status line
    /// shows; null before one is made and after it goes. The shell must outlive what it was given, or be taken away
    /// before it goes; the preparations of extension launches, whose windows belong to it, go when it changes.
    void setShell(IShell* shell);
    [[nodiscard]] IShell* shell() const noexcept;

    // ---- The GUI thread and the work handed to it

    /// The thread the core was made on: the GUI thread.
    [[nodiscard]] std::thread::id guiThreadId() const noexcept;
    [[nodiscard]] bool isGuiThread() const noexcept;

    /// What the marshalled events run against; null, each asks nothing. The target must outlive what it was given,
    /// or be taken away - with `discardPendingEvents` - before it goes.
    void setEventTarget(IApplicationEventTarget* target) noexcept;
    [[nodiscard]] IApplicationEventTarget* eventTarget() const noexcept;

    /// Hands an event to the GUI thread, from any thread; the core owns it from here. It runs once there, unless the
    /// core goes first or its pending events are discarded.
    void postEvent(AfterNightEvent* event, int priority = 0);

    /// Lets go of every event posted that has not run yet, unrun.
    void discardPendingEvents();

    // Each of these may be called from any thread and never waits for the GUI thread: it posts one event and returns.
    // `finished`, where given, is set once the event has run.

    void createNewImageViewControlFromImageAsync(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                                 QWidget* parent, const std::string& historyDesc, ViewControlMode mode,
                                                 ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                 bool pendingChanges, std::atomic_bool* finished);
    void createNewImageViewControlFromSequenceAsync(ImageViewControl* customViewControl,
                                                    an::ui::ImageDocument&& document, QWidget* parent, bool autoDebayer,
                                                    const std::string& historyDesc, ViewControlMode mode,
                                                    ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                    bool pendingChanges, std::atomic_bool* finished);
    void addImageViewControlAsync(ImageViewControl* view);
    void removeImageViewControlAsync(ImageViewControl* view, bool deleteObject);
    void updateImageViewControlListAsync(std::atomic_bool* finished);
    void updateActiveImageActionsAsync();
    void addRecentFileAsync(const std::string& fileName, std::shared_ptr<const an::consumer::ImageAsset> image,
                            std::atomic_bool* finished);
    void removeRecentFileAsync(const std::string& fileName, std::atomic_bool* finished);
    void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text);
    void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text);
    void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text);
    /// `finished` is cleared before the question is posted, and set once it was answered.
    void messageBoxYesNoAsync(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret,
                              std::atomic_bool* finished);
    void setActiveImageViewControlAsync(ImageViewControl* view, std::atomic_bool* finished);
    /// Runs `func` on the GUI thread; it needs no target.
    void runCustomCodeAsync(const std::function<void()>& func);

protected:
    /// Runs an application event that reached the GUI thread; one that throws is reported to the target and logged.
    void customEvent(QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
