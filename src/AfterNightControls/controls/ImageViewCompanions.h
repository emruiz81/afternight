// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the image view reaches of the components built around it that other parts of Controls own: its undo history,
/// the processes attached to it, the inspector and tool windows it opens, the astrometry overlays it draws, the player
/// of a sequence and the lens's histogram.
///
/// The view creates and drives each of these, and none of them is the view's: the history is a tracker of undo steps,
/// a process is a dialog, an inspector is a window over the view's document. The view reaches each through an interface
/// declared here, made by a function declared here and defined where the component lives - the history's beside the
/// tracker, a tool's beside its window - so the view compiles, and is tested, with none of them, and a component
/// changes what it is without the view changing.
///
/// Every member is called on the GUI thread unless it says otherwise.
#pragma once

#include "AfterNightControlsExport.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

class ImageViewControl;
class ImageViewWidgetDisplayModule;
class QWidget;

namespace an::ui
{

/// The undo history of one view. Made by `createViewHistory`, which the history defines.
class IViewHistory
{
public:
    /// Lets the history's steps and their files go.
    virtual ~IViewHistory() = default;

    /// What an edit's worker runs once the edit's result is computed and before the result reaches the GUI thread: it
    /// secures what recording the result needs - the state the edit replaces kept, and room for the result's copy -
    /// waiting for the history's writer as long as it must, so the GUI thread never does.
    ///
    /// Called on the worker, holding the document's writer; `replaced` is the image that writer holds and `edited` the
    /// result nothing else holds yet. The answer is held until the edit is recorded or let go; a failure - which the
    /// history has reported - means the edit must not be shown. It is safe to call after the history is gone, when it
    /// refuses.
    using EditPreparation = std::function<an::Result<std::shared_ptr<void>>(
        const an::consumer::AssetWriteLease& replaced, const an::consumer::ImageAsset& edited, std::stop_token stop)>;

    /// Records the state the view was created with.
    ///
    /// @param description why the view exists: read from a file, made by a process.
    virtual void registerInitialStep(const std::string& description) = 0;

    /// What the worker of the edit about to start runs before its result is recorded. Called when the edit starts, so
    /// the step it replaces is the one the history records now.
    ///
    /// @param name the edit's step name.
    /// @return the preparation; an empty one prepares nothing.
    [[nodiscard]] virtual EditPreparation editPreparation(const std::string& name) = 0;

    /// Records an edit's result before the view shows it, from what its preparation secured. Never waits: the view
    /// calls it on the GUI thread holding the document's writer.
    ///
    /// @param name the step's name.
    /// @param description what the edit did.
    /// @param edited the image the edit is about to show; its copy was taken by the preparation, or is taken now under
    ///        a read lease when there was none, and the image is never kept.
    /// @return false, having recorded nothing and reported why, when the state the edit replaces cannot be restored or
    ///         `edited` cannot be kept: the caller must then not show the edit.
    [[nodiscard]] virtual bool registerEditBeforePublishing(const std::string& name, const std::string& description,
                                                            const an::consumer::ImageAsset& edited) = 0;

    /// Records the state the view shows now, after an edit that has already shown it.
    ///
    /// @param name the step's name.
    /// @param description what the edit did.
    /// @return false when the step could not be recorded.
    virtual bool registerShownState(const std::string& name, const std::string& description) = 0;

    /// Records that the view was renamed.
    virtual void registerRename() = 0;

    /// Starts restoring the previous step: it is read on a worker and shown when it lands.
    ///
    /// @return false, with the view unchanged, when it cannot start; a restore that fails later changes nothing and is
    ///         reported.
    virtual bool undo() = 0;

    /// Starts restoring the next step: it is read on a worker and shown when it lands.
    ///
    /// @return false, with the view unchanged, when it cannot start; a restore that fails later changes nothing and is
    ///         reported.
    virtual bool redo() = 0;

    /// Whether a previous step exists.
    [[nodiscard]] virtual bool hasUndo() const = 0;

    /// Whether a next step exists.
    [[nodiscard]] virtual bool hasRedo() const = 0;

    /// The previous step's name, or empty.
    [[nodiscard]] virtual std::string previousStepName() const = 0;

    /// The next step's name, or empty.
    [[nodiscard]] virtual std::string nextStepName() const = 0;

    /// Opens the history's window over the view.
    virtual void showHistory() = 0;

    /// Continues the lineage of the view this view's image was made from.
    ///
    /// @param source the other view's history.
    virtual void inheritLineage(const IViewHistory& source) = 0;
};

/// Makes the history of a view. Defined beside the history tracker.
///
/// @param view the view whose states it records and restores.
/// @return the history.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::unique_ptr<IViewHistory> createViewHistory(::ImageViewControl& view);

/// A process attached to a view: what the view asks of it while it is attached and when the view closes.
class IViewAttachment
{
public:
    /// An attachment is not owned by the view; it detaches itself before it goes.
    virtual ~IViewAttachment() = default;

    /// The process's name, which is unique among the processes attached to one view.
    [[nodiscard]] virtual std::string attachmentName() const = 0;

    /// Whether the process needs the view to itself: no other process may run on it.
    [[nodiscard]] virtual bool requiresExclusiveAccess() const = 0;

    /// Whether the process shows a real-time preview of the view.
    [[nodiscard]] virtual bool showsRealtimePreview() const = 0;

    /// Whether the process's preview needs the view to itself among previews.
    [[nodiscard]] virtual bool requiresExclusivePreviewAccess() const = 0;

    /// Turns the process's real-time preview off.
    virtual void disableRealtimePreview() = 0;

    /// Detaches from the view and closes: the view is closing.
    virtual void detachAndClose() = 0;
};

/// The player of a sequence view: the frame shown, playback, and the sequence's source. Made by
/// `createViewSequencePlayer`, which the sequence player defines.
class IViewSequencePlayer
{
public:
    /// Stops playback and lets the sequence's source go.
    virtual ~IViewSequencePlayer() = default;

    /// The player's controls, which the view lays out under the image.
    [[nodiscard]] virtual QWidget* widget() = 0;

    /// Whether frames are being played.
    [[nodiscard]] virtual bool isPlaying() const = 0;

    /// Pauses playback.
    virtual void pause() = 0;

    /// Resumes playback.
    virtual void resume() = 0;

    /// Stops playback.
    virtual void stop() = 0;

    /// The zero-based index of the frame shown.
    [[nodiscard]] virtual std::uint64_t currentFrame() const = 0;

    /// Shows a frame.
    ///
    /// @param frame the zero-based index.
    /// @param shown runs on the GUI thread once the frame is the view's image; may be empty.
    virtual void setCurrentFrame(std::uint64_t frame, std::function<void()> shown) = 0;

    /// Enables or disables the controls, while a process runs over the sequence.
    ///
    /// @param enabled whether the controls answer.
    virtual void setEnabled(bool enabled) = 0;

    /// Stops playback and lets the sequence's source go: the view is closing.
    virtual void shutdown() = 0;

    /// Writes the sequence to a file through its publication.
    ///
    /// @param path the file.
    /// @return success, or why the sequence was not written; nothing is left at `path` on a failure.
    [[nodiscard]] virtual an::Status saveTo(const QString& path) = 0;
};

/// Makes the player of a sequence view. Defined beside the sequence player.
///
/// @param view the view whose document states the sequence.
/// @return the player.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::unique_ptr<IViewSequencePlayer>
createViewSequencePlayer(::ImageViewControl& view);

/// A window a view opens over its document.
enum class ViewTool : std::uint8_t
{
    /// The statistics of the image.
    Statistics,

    /// The histogram of the image.
    Histogram,

    /// The header cards a FITS file stated.
    FitsHeader,

    /// The EXIF record a camera file stated.
    Exif,

    /// The summary of the image and its file.
    Summary,

    /// The astrometric solution.
    AstrometryInfo,

    /// The plate solve.
    PlateSolve
};

/// Opens a tool window over a view. Defined beside the tools.
///
/// @param tool which window.
/// @param view the view it reads.
/// @return the window, shown; owned by Qt's parentage and deleted when closed.
[[nodiscard]] AFTERNIGHTCONTROLS_API QWidget* openViewTool(ViewTool tool, ::ImageViewControl& view);

/// An astrometry overlay a view draws over a solved image.
enum class AstrometryOverlay : std::uint8_t
{
    /// The stars detected after a plate solve.
    DetectedStars,

    /// The equatorial grid.
    CoordinateGrid,

    /// The SIP distortion map.
    DistortionMap,

    /// The deep-sky catalogue's annotations.
    CatalogAnnotations,

    /// The bright stars' labels.
    BrightStarLabels
};

/// The stars a view detected for its overlay, with the solution that places them on the sky.
struct ViewOverlayStars
{
    /// The detection the channel policy kept.
    an::StarDetectionChannelResult detection{};

    /// The solution of the frame the stars were detected on.
    std::optional<an::WcsSolution> solution{};
};

/// Makes an astrometry overlay of a view. Defined beside the overlays.
///
/// @param overlay which overlay.
/// @param view the view whose document it reads.
/// @return the overlay, not yet attached to the view's widget.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::unique_ptr<::ImageViewWidgetDisplayModule>
createAstrometryOverlay(AstrometryOverlay overlay, ::ImageViewControl& view);

/// Sets the faintest magnitude a catalogue overlay labels; an overlay that labels no catalogue ignores it. Defined
/// beside the overlays.
///
/// @param overlay an overlay `createAstrometryOverlay` made.
/// @param limit the magnitude.
AFTERNIGHTCONTROLS_API void setAstrometryOverlayMagnitudeLimit(::ImageViewWidgetDisplayModule& overlay, double limit);

/// Hands the stars a view detected to the overlay that draws them; an overlay that draws no stars ignores it. Defined
/// beside the overlays.
///
/// @param overlay an overlay `createAstrometryOverlay` made.
/// @param stars the stars, or null to draw none.
AFTERNIGHTCONTROLS_API void setAstrometryOverlayStars(::ImageViewWidgetDisplayModule& overlay,
                                                      std::shared_ptr<const ViewOverlayStars> stars);

/// The histogram the lens shows under its magnified region. Made by `createLensHistogram`, which the histogram
/// widgets define.
class ILensHistogram
{
public:
    /// The widget is a child of the lens and goes with it.
    virtual ~ILensHistogram() = default;

    /// The widget the lens lays out.
    [[nodiscard]] virtual QWidget* widget() = 0;

    /// Shows a region's counts.
    ///
    /// @param counts the counts, one to three channels of `kLensHistogramBins` bins.
    virtual void setCounts(const LensHistogram& counts) = 0;
};

/// Makes the lens's histogram. Defined beside the histogram widgets.
///
/// @param parent the lens.
/// @return the histogram.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::unique_ptr<ILensHistogram> createLensHistogram(QWidget* parent);

} // namespace an::ui
