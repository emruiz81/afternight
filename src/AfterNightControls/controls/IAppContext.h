// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include "controls/GlobalViewControlActions.h"
#include "controls/ViewControlMode.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The engine's and the session bridge's types the interfaces name. They are declared rather than included, so a unit
// that includes the service interfaces reaches no engine header through them; a unit that calls or implements a member
// naming one includes the header that defines what it uses.
namespace an
{
template <typename ValueT> class Result;
class ImageDescriptor;
class OutputIntent;
class Status;
enum class CodecFormat : std::uint8_t;
enum class SequenceFormat : std::uint8_t;
} // namespace an

class QContextMenuEvent;
class QWidget;
class ImageViewControl;
class AfterNightAppListener;
class TopLevelProcessBaseControl;
class AfterNightEvent;

namespace an::ui
{

class IDialogService;
class IEngineSessionProvider;
class ImageDocument;
class IInterfaceSettingsRuntimeService;
class IPathMemoryService;
class IProcessPresenter;
class IFileDialogService;
class IViewRegistry;
class IAppEventBus;
class IRecentFilesService;
class IThumbnailPathProvider;
class RawOpenPolicy;
struct RawOpenSettings;

// ---------------------------------------------------------------------------
// IIconProvider — icon retrieval abstraction
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IIconProvider
{
public:
    virtual ~IIconProvider() = default;

    [[nodiscard]] virtual QIcon getIcon(const QString& icon) = 0;
    [[nodiscard]] virtual QPixmap loadSvgFileAsPixmap(const QString& fileName, QSize size, QColor color) = 0;
    [[nodiscard]] virtual QColor getColorNormal() const = 0;
    [[nodiscard]] virtual QColor getColorDisabled() const = 0;
};

using IIconProviderPtr = std::shared_ptr<IIconProvider>;

// ---------------------------------------------------------------------------
// IDialogService — message boxes and notification toasts
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IDialogService
{
public:
    virtual ~IDialogService() = default;

    virtual void openSettingsAt(const QString& pageName) = 0;
    virtual void openExtensionsAt(const QString& pageName)
    {
        openSettingsAt(pageName);
    }

    virtual void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) = 0;

    virtual void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text) = 0;

    virtual void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text) = 0;

    virtual void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text) = 0;
};

// ---------------------------------------------------------------------------
// IGlobalSettingsProvider — key-value settings access (read/write)
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IGlobalSettingsProvider
{
public:
    virtual ~IGlobalSettingsProvider() = default;

    virtual bool upsert(const QString& key, const QString& value) = 0;

    [[nodiscard]] virtual QString select(const QString& key, const QString& defaultValue = {}) = 0;

    [[nodiscard]] virtual std::optional<QString> selectOptional(const QString& key) = 0;
};

// ---------------------------------------------------------------------------
// IInterfaceSettingsRuntimeService - apply saved interface settings live
// ---------------------------------------------------------------------------

/// A shell a person can choose: its id, as the setting `interface/shell` stores it, and the name a person reads.
struct ShellOption
{
    QString id;
    QString displayName;
};

class AFTERNIGHTCONTROLS_API IInterfaceSettingsRuntimeService
{
public:
    virtual ~IInterfaceSettingsRuntimeService() = default;

    virtual void applyTheme(const QString& themeId) = 0;
    virtual void refreshAllZoomSettings() = 0;

    /// The shells the application can make, in the order it registered them; none when it offers no choice.
    [[nodiscard]] virtual std::vector<ShellOption> availableShells() const
    {
        return {};
    }
};

// ---------------------------------------------------------------------------
// IProcessSettingsProvider — per-process window state persistence
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IProcessSettingsProvider
{
public:
    virtual ~IProcessSettingsProvider() = default;

    virtual bool upsert(const QString& processName, int win_state, int width, int height,
                        const QString& json_settings) = 0;

    [[nodiscard]] virtual bool select(const QString& processName, int& win_state, int& width, int& height,
                                      QString& json_settings) = 0;
};

// ---------------------------------------------------------------------------
// ITempFileService — temp/scratch file management
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API ITempFileService
{
public:
    virtual ~ITempFileService() = default;

    [[nodiscard]] virtual bool isTempFile(const std::string& fileName) = 0;

    /// A new path in the temporary directory for a sequence written in the given container.
    [[nodiscard]] virtual std::string createTempFilePathForNewSequence(an::SequenceFormat format) const = 0;

    [[nodiscard]] virtual QString getTempDirPath() const = 0;
};

// ---------------------------------------------------------------------------
// IPathMemoryService - persisted folder-context lookup/update
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IPathMemoryService
{
public:
    virtual ~IPathMemoryService() = default;

    [[nodiscard]] virtual QString selectPath(const QString& contextKey, const QString& defaultValue = {}) = 0;
    virtual bool upsertPath(const QString& contextKey, const QString& path) = 0;
};

// ---------------------------------------------------------------------------
// IFileDialogService - app-managed file selection dialogs
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IFileDialogService
{
public:
    virtual ~IFileDialogService() = default;

    [[nodiscard]] virtual QStringList selectImageFiles(QWidget* parent, const QString& title,
                                                       const QString& folderContext,
                                                       bool supportMultipleFiles = true) = 0;

    [[nodiscard]] virtual QStringList selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                                  const QStringList& fileExtensions) = 0;

    [[nodiscard]] virtual QStringList saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                                                const QString& folderContext, const QStringList& fileExtensions) = 0;
};

// ---------------------------------------------------------------------------
// IViewFactory — creating new view controls
// ---------------------------------------------------------------------------

/// Makes the view a document is shown in. Called on the GUI thread.
///
/// A view owns the document it shows. Both entry points take the document as a value to move from: when a view is
/// made it has taken the document, and when none is made - the factory returns null - the document is left with the
/// caller as it was handed in, open and still the caller's to show or close.
class AFTERNIGHTCONTROLS_API IViewFactory
{
public:
    virtual ~IViewFactory() = default;

    /// Makes a view of an image document.
    [[nodiscard]] virtual ImageViewControl* createNewImageViewControlFromImage(
        ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent = nullptr,
        const std::string& historyDesc = "", ViewControlMode mode = ViewControlMode::standard,
        ImageViewControl* neighbour = nullptr, bool keepFocusedNeighbour = false, bool pendingChanges = true) = 0;

    /// Makes a view of a sequence: the document states the sequence it shows a frame of, through
    /// `ImageDocument::sequence`, and holds that frame. A document that states no sequence makes no view.
    [[nodiscard]] virtual ImageViewControl* createNewImageViewControlFromSequence(
        ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent = nullptr,
        bool autoDebayer = false, const std::string& historyDesc = "", ViewControlMode mode = ViewControlMode::standard,
        ImageViewControl* neighbour = nullptr, bool keepFocusedNeighbour = false, bool pendingChanges = true) = 0;
};

// ---------------------------------------------------------------------------
// IViewRegistry — active view tracking
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IViewRegistry
{
public:
    virtual ~IViewRegistry() = default;

    [[nodiscard]] virtual ImageViewControl* getActiveImageViewControl() = 0;
    virtual void setActiveImageViewControl(ImageViewControl* view) = 0;
};

// ---------------------------------------------------------------------------
// IAppEventBus — application-wide listener subscribe/unsubscribe
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IAppEventBus
{
public:
    virtual ~IAppEventBus() = default;

    virtual void subscribeListener(AfterNightAppListener* listener) = 0;
    virtual void unsubscribeListener(AfterNightAppListener* listener) = 0;
    virtual void postAfterNightEvent(AfterNightEvent* event, int priority) = 0;
    virtual void notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl) = 0;
    virtual void notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl) = 0;
};

// ---------------------------------------------------------------------------
// IProcessRegistry — process window instance tracking
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IProcessRegistry
{
public:
    virtual ~IProcessRegistry() = default;

    virtual void registerProcessInstance(TopLevelProcessBaseControl* process) = 0;
    virtual void unregisterProcessInstance(TopLevelProcessBaseControl* process) = 0;
};

// ---------------------------------------------------------------------------
// IOutputIntentProvider — how the export settings write an image
// ---------------------------------------------------------------------------

/// Decides, from the export settings, how an image is written.
class AFTERNIGHTCONTROLS_API IOutputIntentProvider
{
public:
    virtual ~IOutputIntentProvider() = default;

    /// The output intent the export settings give for writing an image described by `source` into a file of
    /// `container`: the representation written and its scaling, the compression the settings choose for that
    /// container, whether the source's cards are carried, and the cards the settings add. The artifact identity a
    /// workflow output carries is the output request's, not the intent's.
    ///
    /// @return the intent, or why none can be built - a container the settings cannot write, or an intent the engine
    ///         refuses.
    [[nodiscard]] virtual an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                         an::CodecFormat container) const = 0;
};

// ---------------------------------------------------------------------------
// IRawOpenPolicyService — the RAW settings a camera file is read under
// ---------------------------------------------------------------------------

/// Publishes the RAW policy every read of a camera file is submitted under, and stores the settings it is built from.
///
/// A read takes the policy published when it is asked for and keeps it, its registry included, until it lands; a
/// change publishes another for the reads asked for after it and never reaches one on its way. The settings pages
/// change the settings through it and the views read under it.
class AFTERNIGHTCONTROLS_API IRawOpenPolicyService
{
public:
    virtual ~IRawOpenPolicyService() = default;

    /// The policy published last, or null when none could be built, in which case a read decodes through the
    /// session's own registry and treats nothing. Thread-safe.
    [[nodiscard]] virtual std::shared_ptr<const RawOpenPolicy> rawOpenPolicy() const = 0;

    /// Stores the RAW settings and publishes their policy for the reads asked for after it. Called on the GUI thread.
    ///
    /// @return success, or why the policy could not be built - the settings are stored either way, and the policy
    ///         published before stays published.
    virtual an::Status setRawOpenSettings(const RawOpenSettings& settings) = 0;
};

// ---------------------------------------------------------------------------
// IRecentFilesService - recent-file enumeration and open handoff
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IRecentFilesService
{
public:
    virtual ~IRecentFilesService() = default;

    [[nodiscard]] virtual std::deque<std::string> getRecentFilesList() const = 0;
    virtual void openRecentSingleFile(const QString& filePath) = 0;
    virtual void openDroppedFiles(const QStringList& files) = 0;
};

// ---------------------------------------------------------------------------
// IThumbnailPathProvider - persisted thumbnail file lookup
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IThumbnailPathProvider
{
public:
    virtual ~IThumbnailPathProvider() = default;

    [[nodiscard]] virtual QString selectThumbnailPath(const QString& filePath) const = 0;
};

// ---------------------------------------------------------------------------
// IViewControlManager — view control lifecycle, naming, and queries
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IViewControlManager
{
public:
    virtual ~IViewControlManager() = default;

    virtual void addViewControl(ImageViewControl* view) = 0;
    virtual void removeViewControl(ImageViewControl* view, bool deleteObject) = 0;
    virtual void renameViewControl(ImageViewControl* view) = 0;
    virtual void maximizeViewControl(ImageViewControl* ctrl) = 0;
    virtual void restoreViewControl(ImageViewControl* ctrl) = 0;
    virtual void notifyViewControlUpdated(const ImageViewControl* ctrl) = 0;
    virtual void createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event) = 0;
    [[nodiscard]] virtual bool supportsDockWidgets() const = 0;

    [[nodiscard]] virtual ImageViewControl* getViewControlByName(const std::string& name) = 0;
    [[nodiscard]] virtual std::vector<ImageViewControl*> getListOfViewControls() = 0;
    [[nodiscard]] virtual std::string createViewControlUniqueName(const std::string& name) = 0;
};

// ---------------------------------------------------------------------------
// IStatusBarService — status bar updates (image info, zoom, celestial coords)
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IStatusBarService
{
public:
    virtual ~IStatusBarService() = default;

    virtual void updateImageInfoInStatusBar() = 0;
    virtual void updateImageZoomInfoInStatusBar() = 0;
    virtual void updateImageCelestialCoordsInStatusBar(double ra_deg, double dec_deg) = 0;
    virtual void clearImageCelestialCoordsInStatusBar() = 0;
};

// ---------------------------------------------------------------------------
// IActionStateManager — action enable/disable and history state
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IActionStateManager
{
public:
    virtual ~IActionStateManager() = default;

    virtual void setCustomImageActionEnabled(ImageViewControl* view, GlobalViewControlActions action, bool enable) = 0;
    virtual void updateActiveImageActions() = 0;
    virtual void updateActiveImageHistory() = 0;
};

// ---------------------------------------------------------------------------
// IAppContext — aggregate interface that exposes all sub-services
// ---------------------------------------------------------------------------

class AFTERNIGHTCONTROLS_API IAppContext
{
public:
    virtual ~IAppContext() = default;

    [[nodiscard]] virtual IIconProviderPtr getIconProvider() = 0;
    [[nodiscard]] virtual IDialogService* getDialogService() = 0;
    [[nodiscard]] virtual IGlobalSettingsProvider* getGlobalSettings() = 0;
    [[nodiscard]] virtual IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() = 0;
    [[nodiscard]] virtual IProcessSettingsProvider* getProcessSettings() = 0;
    [[nodiscard]] virtual ITempFileService* getTempFileService() = 0;
    [[nodiscard]] virtual IPathMemoryService* getPathMemoryService() = 0;
    [[nodiscard]] virtual IFileDialogService* getFileDialogService() = 0;
    [[nodiscard]] virtual IViewFactory* getViewFactory() = 0;
    [[nodiscard]] virtual IViewRegistry* getViewRegistry() = 0;
    [[nodiscard]] virtual IAppEventBus* getAppEventBus() = 0;
    [[nodiscard]] virtual IProcessRegistry* getProcessRegistry() = 0;
    [[nodiscard]] virtual IOutputIntentProvider* getOutputIntentProvider() = 0;
    [[nodiscard]] virtual IRecentFilesService* getRecentFilesService() = 0;
    [[nodiscard]] virtual IThumbnailPathProvider* getThumbnailPathProvider() = 0;
    [[nodiscard]] virtual IViewControlManager* getViewControlManager() = 0;
    [[nodiscard]] virtual IStatusBarService* getStatusBarService() = 0;
    [[nodiscard]] virtual IActionStateManager* getActionStateManager() = 0;

    /// The process's engine session, which every Controls operation runs in. Controls opens none of its own.
    [[nodiscard]] virtual IEngineSessionProvider* getEngineSessionProvider() = 0;

    /// The RAW policy reads of a camera file are submitted under; null where nothing publishes one.
    [[nodiscard]] virtual IRawOpenPolicyService* getRawOpenPolicyService() = 0;

    /// The top-level application widget, used as default parent for dialogs
    /// and process windows.
    [[nodiscard]] virtual QWidget* getMainWindow() = 0;

    /// What shows a process as a page of the main window rather than a window of its own; null where every process
    /// is a window, which is so unless a context says otherwise.
    [[nodiscard]] virtual IProcessPresenter* getProcessPresenter()
    {
        return nullptr;
    }
};

} // namespace an::ui

// ---------------------------------------------------------------------------
// Process-global accessor — set once per process at startup.
// The application sets it to the shell core's ApplicationContext; the
// extension host sets it to ExtensionAppContext.
// ---------------------------------------------------------------------------

namespace an::ui
{
/// Install the process-wide IAppContext.  Must be called exactly once
/// before any control is created.  Ownership is NOT transferred.
AFTERNIGHTCONTROLS_API void setAppContext(IAppContext* ctx);

/// Clear the process-wide context during controlled application or test teardown.
AFTERNIGHTCONTROLS_API void clearAppContext();

/// Retrieve the process-wide IAppContext.  Returns nullptr if
/// setAppContext() has not been called yet.
[[nodiscard]] AFTERNIGHTCONTROLS_API IAppContext* appContext();

[[nodiscard]] inline QString ensureTempDirPathOrSystemTemp()
{
    if (auto* context = appContext())
    {
        if (auto* temp_file_service = context->getTempFileService())
        {
            const QString temp_dir_path = temp_file_service->getTempDirPath().trimmed();
            if (!temp_dir_path.isEmpty())
            {
                QDir().mkpath(temp_dir_path);
                return temp_dir_path;
            }
        }
    }

    const QString fallback_path = QDir::tempPath();
    QDir().mkpath(fallback_path);
    return fallback_path;
}

} // namespace an::ui
