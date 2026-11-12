// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QToolBar>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include "controls/CalibrationPreviewConfig.h"
#include "controls/ImageViewThemeColors.h"
#include "controls/ImageViewScrollArea.h"
#include "session/ImageDocument.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/MaskRenderer.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DockableControl.h"

class QAction;
class QCloseEvent;
class QComboBox;
class QContextMenuEvent;
class QFocusEvent;
class QKeyEvent;
class QActionEvent;
class QCheckBox;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QToolButton;
class QVBoxLayout;
class ImageViewControlListener;
class ImageViewControl;

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui
{
class ImageReads;
class IViewAttachment;
class IViewHistory;
class IViewSequencePlayer;
struct ViewImageStatistics;
struct ViewOverlayStars;
} // namespace an::ui

enum class ImageViewControlAction : uint8_t
{
    execute_statistics,
    execute_histogram,
    execute_fits_header,
    execute_astrometry_info,
    execute_exif,
    execute_summary
};

#include "controls/ViewControlMode.h"

enum class ImageUpdateKind : uint8_t
{
    PixelDataChanged = 0,
    MetadataOnly
};

const QString NO_IMAGE_STR = "<--NO IMAGE-->";

// Class for drawing text over the view control ignoring the scroll area
class AFTERNIGHTCONTROLS_API ViewControlTextOverlay : public QWidget
{
public:
    ViewControlTextOverlay(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

    void setText(const QString& text, const QColor& color);

private:
    void paintEvent(QPaintEvent* ev) override;

    QString m_Text;
    QColor m_Color;
};

class AFTERNIGHTCONTROLS_API CustomToolBar : public QToolBar
{
public:
    CustomToolBar(ImageViewControl* vControl);
    void setOverflowIconColors(const QColor& normalColor, const QColor& disabledColor);
    void refreshOverflow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void actionEvent(QActionEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void ensureOverflowActionIsLast();
    void queueOverflowRefresh();
    void updateOverflowLayout();
    void rebuildOverflowMenu();

    ImageViewControl* m_ImageViewControl = nullptr;
    QToolButton* m_OverflowButton = nullptr;
    QMenu* m_OverflowMenu = nullptr;
    QAction* m_OverflowAction = nullptr;
    QList<QAction*> m_OverflowedActions;
    bool m_OverflowRefreshPending = false;
    bool m_ReorderingOverflowAction = false;
    bool m_UpdatingOverflowLayout = false;
};

/// What the view's work in flight came to so far: file reads, edits and writes asked for, published, and refused as
/// stale.
struct ImageViewWorkTelemetry
{
    /// Reads asked for.
    std::uint64_t readsRequested = 0u;

    /// Reads whose image the view showed.
    std::uint64_t readsCommitted = 0u;

    /// Reads that came back for a request no longer current and were discarded.
    std::uint64_t readsDiscarded = 0u;

    /// Reads the engine refused.
    std::uint64_t readsFailed = 0u;

    /// Edits whose result the view showed.
    std::uint64_t editsCommitted = 0u;

    /// Edits discarded: the document changed or closed under them, or the history refused them.
    std::uint64_t editsDiscarded = 0u;

    /// Writes made.
    std::uint64_t writesCompleted = 0u;

    /// Writes refused.
    std::uint64_t writesFailed = 0u;
};

/// The window an image document is shown in: its toolbar, the presentation of its image, its mask, the companions
/// around it and, for a sequence, its player.
///
/// A view owns the document it shows. Reading a file, demosaicing, calibrating a static preview, editing and writing
/// run on workers over the process's engine session, and each publishes on the GUI thread only when the request it was
/// made for is still current; a view never waits on the GUI thread for its work, and closing it refuses every result
/// still on its way.
class AFTERNIGHTCONTROLS_API ImageViewControl : public DockableControl
{
    W_OBJECT(ImageViewControl)

public:
    ImageViewControl(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

    KDDockWidgets::QtWidgets::DockWidget* getDockWidget() override;

    // mode: 0 = standard view control, 1 = RT preview control, 2 = static Image preview control (for image lists with
    // preview)
    void initialize(const std::string& uniqueName, const ViewControlMode mode);

    void initializeNewImageHistory(const std::string& historyDesc);

    /// Reads an image file into the view. The read runs on a worker and the view shows the image when it lands, if no
    /// later read superseded it. A standard view offers to demosaic a mosaic, through the method the RAW demosaic
    /// setting names; a static preview reads through its calibration and its demosaic choice.
    void loadImageFromFile(const std::string& fileName);
    void clear();

    void setCalibrationPreviewConfig(const an::ui::CalibrationPreviewConfig& config);
    void setCalibrationPreviewVisible(bool visible);
    [[nodiscard]] bool isCalibrationPreviewEnabled() const;
    void clearCalibrationCaches();

    void setAutoDebayerCfaImagePreviews(bool enabled);
    [[nodiscard]] bool isAutoDebayerCfaImagePreviewsEnabled() const;

    /// Whether the static preview measures the statistics its overlay shows, for every image it shows - as it does
    /// unless told otherwise - or shows only those it is handed (`showPreviewStatistics`): a preview of a list of files
    /// whose owner keeps each file's statistics, so that a file shown again is not measured again.
    ///
    /// @param measures whether the preview measures them.
    void setMeasuresPreviewStatistics(bool measures);

    /// Whether the static preview's owner has it measure the statistics its overlay shows
    /// (`setMeasuresPreviewStatistics`). A preview whose presentation choices are not offered measures none
    /// whatever this says, and measures again as its owner set once they are offered.
    [[nodiscard]] bool measuresPreviewStatistics() const;

    /// Shows statistics in the static preview's overlay, measured elsewhere for the image it shows.
    ///
    /// @param statistics the statistics, or nothing to show none.
    void showPreviewStatistics(const std::optional<an::ui::ViewImageStatistics>& statistics);

    /// The statistics the static preview's overlay shows now, or nothing.
    [[nodiscard]] std::optional<an::ui::ViewImageStatistics> previewStatistics() const;

    /// Whether the static preview offers the choices of how its image is presented - the channel, the display mode,
    /// the stretch's settings and the statistics overlay - or its zoom alone, for a preview whose owner decides how the
    /// image is presented (`setViewMode`, `ImageViewWidget::setAutostretchSettings`). One that offers none measures no
    /// statistics, shows no autostretch banner, and opens no stretch settings by their shortcut.
    ///
    /// @param offered whether the choices are offered; they are unless told otherwise.
    void setPresentationChoicesOffered(bool offered);

    /// Writes the view's image to a file, in the container its extension names, as the export settings decide. The
    /// write runs on the calling thread under a read lease; the view's pending changes are cleared only when the
    /// document still holds what was written.
    ///
    /// @param fileName the file, UTF-8.
    /// @param async whether the view's listeners hear of the write through the event loop rather than at once.
    /// @return success, or why nothing was written; nothing is left at the path on a failure.
    an::Status saveImageToFile(const std::string& fileName, bool async = false);

    /// Writes the sequence the view plays to a file through its player.
    [[nodiscard]] bool saveSequenceToFile(const std::string& fileName, bool async = false);

    /// Shows a copy of another view's image, taken on a worker, as a new document of this view.
    void loadImageFromOtherViewControl(ImageViewControl* other);

    /// Shows a document: the view takes it, and the document it showed before is closed.
    void setDocument(an::ui::ImageDocument&& document);

    /// Shows a sequence document: the document states the sequence through `ImageDocument::sequence` and holds the
    /// frame shown. The view takes it, and the player the sequence companion makes is laid out under the image.
    ///
    /// @param document the document.
    /// @param autoDebayer whether the player demosaics a mosaic frame as it shows it.
    void setSequenceDocument(an::ui::ImageDocument&& document, bool autoDebayer);

    /// The document the view shows; an invalid one before the first image lands.
    [[nodiscard]] const an::ui::ImageDocument& document() const;

    /// The document the view shows, for writing.
    [[nodiscard]] an::ui::ImageDocument& document();

    /// Whether the view shows an image.
    [[nodiscard]] bool hasImage() const;

    /// The description of the image shown, read without a lease.
    [[nodiscard]] std::optional<an::ImageDescriptor> imageDescriptor() const;

    /// Makes another asset the document's image and shows it. The writer is taken at once when nothing reads the
    /// document and on a worker otherwise; `done` runs on the GUI thread with the outcome.
    ///
    /// @param asset the new image, from the process's session.
    /// @param disablePendingChangesCheck whether the change leaves the pending-changes mark as it was.
    /// @param done told the outcome; may be empty.
    void replaceImage(an::consumer::ImageAsset&& asset, bool disablePendingChangesCheck = false,
                      std::function<void(const an::Status&)> done = {});

    /// `replaceImage`, with the analysis the new image's presentation reuses rather than measures: statistics taken
    /// from another image still stretch it when the writer is taken on a worker.
    ///
    /// @param asset the new image, from the process's session.
    /// @param analysis what the new image's presentation reuses, or nothing to measure its own.
    /// @param disablePendingChangesCheck whether the change leaves the pending-changes mark as it was.
    /// @param done told the outcome; may be empty.
    void replaceImageWithPresentationAnalysis(an::consumer::ImageAsset&& asset,
                                              std::optional<an::DisplayPresentationAnalysis> analysis,
                                              bool disablePendingChangesCheck = false,
                                              std::function<void(const an::Status&)> done = {});

    /// How an edit computes the view's new image from the one the document's writer holds, on a worker.
    using EditCompute = std::function<an::Result<an::consumer::ImageAsset>(
        const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image, std::stop_token stop)>;

    /// Edits the view's image. Edits run one at a time in the order asked for; each runs on a worker holding the
    /// document's writer, is recorded in the history before the view shows it, and is shown only while its request is
    /// current - a document changed, closed or replaced under it discards it. An edit that reads other images names
    /// them in `reads`: the worker leases them with the document's writer, in one order of identity, before `compute`
    /// asks to be admitted, and lets them go when it returns; `compute` reads them through `readBeside` under an
    /// operation whose request states them.
    ///
    /// @param stepName the history step's name.
    /// @param stepDescription the history step's description.
    /// @param compute the edit.
    /// @param done runs on the GUI thread with whether the edit was shown; may be empty.
    /// @param reads the other images the edit reads; may be empty.
    void editImage(std::string stepName, std::string stepDescription, EditCompute compute,
                   std::function<void(bool shown)> done = {}, std::shared_ptr<an::ui::ImageReads> reads = {});

    void fitWindowToImage();

    void executeStatistics();

    void executeHistogram();

    void executeFITSHeaderViewer();

    void executeAstrometryInfoViewer();

    void executeEXIFViewer();

    void executeSummaryViewer();

    void executePlateSolve();

    /// Opens the Annotate Image window over the view's solved image, or raises the one open.
    void executeAnnotateImage();

    void adjustImageToWindow();

    void switchMaximized();

    bool isMaximized() const;

    void switchMaximizedExternal();

    void displayDefaultViewChannel();

    void displayViewChannel(const ImageCompositionMode channel);

    /// The channel or component the view shows.
    [[nodiscard]] ImageCompositionMode displayedComposition() const;

    void setUniqueName(const std::string& name);

    [[nodiscard]] std::string getUniqueName() const;

    [[nodiscard]] std::string getFileName() const;

    /// Detects the stars of a solved image on a worker and hands them to the detected-stars overlay; clears them for
    /// an image with no solution.
    void runPostPlateSolveStarDetection();

    /// The stars the detected-stars overlay draws, or null.
    [[nodiscard]] std::shared_ptr<const an::ui::ViewOverlayStars> overlayStars() const;

    /// The world-coordinate solution the image's own cards state, read when the image was last shown or updated;
    /// nothing for an image that states none. The astrometry overlays and windows read it on the GUI thread.
    [[nodiscard]] const std::optional<an::WcsSolution>& imageSolution() const;

    /// Shows another image in place of the document's when the widget's alternate rendering is on: a real-time
    /// preview's original.
    void setAlternateAsset(std::shared_ptr<const an::consumer::ImageAsset> asset, bool notifyImageViewWidget);
    void clearAlternateImage();
    bool hasAlternateImage() const;
    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> alternateAsset() const;

    void subscribeListener(ImageViewControlListener* listener);

    void unsubscribeListener(ImageViewControlListener* listener);

    void notifyImageLoaded();

    void notifyImageSaved();

    void notifyImageSavedAsync();

    void notifyImageUpdated(bool disablePendingChangesCheck = false,
                            ImageUpdateKind updateKind = ImageUpdateKind::PixelDataChanged);

    /// `notifyImageUpdated`, with the analysis the next presentation reuses rather than measures.
    ///
    /// @param analysis what a presentation of the updated image measured, or nothing.
    void notifyImageUpdatedWithPresentationAnalysis(std::optional<an::DisplayPresentationAnalysis> analysis,
                                                    bool disablePendingChangesCheck = false,
                                                    ImageUpdateKind updateKind = ImageUpdateKind::PixelDataChanged);

    /// `notifyImageUpdated` from any thread, through the event loop, never waited on. Nothing is notified, and
    /// `callback` does not run, once the view has closed.
    void notifyImageUpdatedAsync(bool disablePendingChangesCheck = false,
                                 const std::function<void()>& callback = nullptr,
                                 ImageUpdateKind updateKind = ImageUpdateKind::PixelDataChanged);

    void fitToWindowAsync();

    void notifyImageRepaintAsync();

    void notifyImageRenamed();

    void notifyImageViewScaled();

    void notifyMaskUpdated();

    void notifyAlternateImageEnabled();

    void notifyAlternateImageDisabled();

    void enableAction(ImageViewControlAction action, bool enable);

    /// The view's undo history, or null for a preview view.
    [[nodiscard]] an::ui::IViewHistory* viewHistory() const;

    void setActiveControl();

    /// Refresh icons and styles after theme change
    void refreshTheme();

    /// Refresh cached zoom settings from persistence
    void refreshZoomSettings();

    void horizontalMirror();
    void verticalMirror();

    void rotate180Degree();
    void rotate90DegreeRight();
    void rotate90DegreeLeft();

    void zoomIn();
    void zoomOut();
    void zoom100();
    void fitToWindow();
    void adjustWindow();

    void zoomAndCenterOn(int x, int y, float scaleFactor);

    float getCurrentZoom();
    [[nodiscard]] QPoint getViewportOffset() const;
    void setViewportOffset(const QPoint& offset);

    [[nodiscard]] ViewControlMode getViewControlMode() const;

    [[nodiscard]] ImageViewScrollArea* getScrollArea() const;

    [[nodiscard]] ImageViewWidget* getViewWidget() const;

    bool isMasked() const;

    /// Uses another view's image as this view's mask. A mask must have this view's width and height and one channel,
    /// or three over a colour image; one that has not is refused with a warning and the view stays as it was.
    void setCurrentMask(ImageViewControl* mask);

    [[nodiscard]] ImageViewControl* getCurrentMask() const;

    void removeCurrentMask();

    void setShowMask(const bool show);
    bool isShowMaskEnabled() const;

    void setEnableMask(const bool enable);
    bool isMaskEnabled() const;

    void setInvertMask(const bool invert);
    bool isMaskInverted() const;

    void setMaskRenderingMode(const MaskRenderingMode mode);

    [[nodiscard]] MaskRenderingMode getMaskRenderingMode() const;

    // Client callback called from the server mask
    void notifySourceMaskUpdated(const bool fromMaskProvider);

    bool hasProcessAttached(const std::string& processName) const;

    size_t numOfAttachedProcesses() const;

    bool hasProcessWithExclusiveAccessAttached() const;

    /// Attaches a process to the view; it detaches itself before it goes.
    void attachProcess(an::ui::IViewAttachment& process);

    void detachProcess(an::ui::IViewAttachment& process);

    /// Turns off every attached real-time preview that needs the view to itself among previews, except the
    /// requester's.
    void disableExclusiveRTPreviews(const an::ui::IViewAttachment* requester);

    void disableAllRTPreviews();

    void setRTPreviewShowOriginalLabel(bool val);

    void setRTAlternatePreviewLabels(const QString& label1, const QString& label2);

    void setRTPreviewLabelEnabled(bool enabled);

    void enableAutostretchBanner(bool enabled);

    void enableRTPreviewAutostretchControls(bool enabled);

    void setShowAutostretchBanner(bool show);
    bool isShowAutostretchBannerEnabled();

    // Closes this object and the dock container (removing it from the dock manager)
    void deepClose();

    void updateImageActions();
    void upcateImageHistory();

    bool isSequence() const;
    bool isSERSequence() const;
    bool isImageListSequence() const;
    bool isVideoSequence() const;
    bool isFITSSequence() const;

    bool isSequenceAutodebayerEnabled() const;

    bool isSequencePlaying() const;

    void pauseSequence();

    void resumeSequence();

    // Returns the 1-based frame number for display/UI usage.
    size_t getSequenceCurrentFrame();

    // Returns the 0-based frame index used by sequence/image APIs.
    size_t getSequenceCurrentFrameIndex();

    size_t getSequenceNumOfFrames();

    /// The sequence player, or null for a view of a single image.
    [[nodiscard]] an::ui::IViewSequencePlayer* sequencePlayer() const;

    // Checks both sequence or image
    bool isCFA() const;
    bool isMono() const;

    /// Takes the view for one process run: false when another run holds it. Exactly one of two concurrent callers
    /// succeeds.
    bool tryRequestLockForProcessing();

    bool isLockedForProcessing();

    void releaseLockForProcessing();

    void setRTPreviewTakeSnapshotActionEnabled(bool enabled);

    void setRTPreviewAreaActionEnabled(bool enabled);

    [[nodiscard]] QCheckBox* getRTPreviewAreaCheckBox() const;

    /// Tells the view whether its Autostretch Settings window is open. The window's button and shortcut are
    /// offered only while the view shows its image stretched and no such window is open, and the display mode
    /// stays as it is while one is.
    void setAutostretchSettingsOpen(bool open);

    void setViewMode(ImageViewMode mode);

    void forceLinearMode();

    void redrawImage();

    bool hasPendingChanges();
    bool hasProcessAttached();

    void markPendingChanges();

    void resetPendingChanges();

    bool checkPendingChangesAndAskUserBeforeClosing();
    bool checkProcessAttachedAndAskUserBeforeClosing();

    // Hook ADS close events
    void closeRequested();

    bool isCloseRequested();

    void showViewControlBusyWarning(const QString& title, bool async = true);

    bool isAutoStretchEnabled();

    AutostretchParams getAutostretchSettings();

    bool isInFloatingWindow();

    bool isStatisticsActionEnabled() const;
    bool isHistogramActionEnabled() const;

    /// Whether the view offers its FITS header window: the image carries header cards a FITS file stated.
    [[nodiscard]] bool isFitsHeaderActionEnabled() const;

    /// Whether the view offers its EXIF window: the image carries a camera's EXIF record.
    [[nodiscard]] bool isExifActionEnabled() const;

    bool canPlateSolveCurrentImage() const;

    /// Whether the view can annotate its image: a still image that states an astrometric solution.
    [[nodiscard]] bool canAnnotateCurrentImage() const;

    /// What the view's reads, edits and writes came to so far.
    [[nodiscard]] ImageViewWorkTelemetry workTelemetry() const;

    /// Whether no read, edit, write or detection of the view is on its way.
    [[nodiscard]] bool isWorkIdle() const;

    void celestialCoordsChanged(double ra_deg, double dec_deg) W_SIGNAL(celestialCoordsChanged, ra_deg, dec_deg);
    void celestialCoordsCleared() W_SIGNAL(celestialCoordsCleared);

    virtual ~ImageViewControl();

public:
    void undo();
    void redo();
    void history();
    void autostretchSettings();

private:
    void on_imageViewChannelComboBox_currentTextChanged(const QString& text);

    void on_imageViewModeComboBox_currentTextChanged(const QString& text);

    void on_rtpreview_switch_images_clicked();

    void on_rtpreview_take_snapshot_clicked();

    /// @brief Emitted when a dock widget becomes current or not in its tab group
    /// @param isCurrent true if it became current
    void isCurrentTabChanged(bool focused);
    void isFocusedChanged(bool focused);
    void isFloatingChanged(bool);
    void guestViewChanged();

private:
    friend class ImageViewWidget;

    struct ReadJob;
    struct EditJob;

    void updateSupportedImageChannels(bool forceUpdate = false);

    void updateAutostretchBannerText();
    void refreshCelestialCoordinateSystemCache();
    void updateCursorCelestialCoords();
    void updateHoveredCelestialCoordsForImagePixel(double pixel_x, double pixel_y);
    void clearHoveredCelestialCoords();
    void invalidateAstrometryStarDetection(bool clearStars);
    void restartStaticPreviewLoad();
    void setPreviewContentVisible(bool hasContent);

    void notifyImageUpdatedImpl(std::optional<an::DisplayPresentationAnalysis> analysis,
                                bool disablePendingChangesCheck, ImageUpdateKind updateKind);

    void requestRead(const QString& path);
    void requestPreviewStatistics();
    void commitRead(const ReadJob& job, an::Result<an::consumer::ImageAsset>&& read, bool mosaic);
    void offerDemosaic(an::consumer::ImageAsset&& mosaic, const QString& path);
    void showReadImage(an::consumer::ImageAsset&& asset, const QString& path);
    void failRead(const QString& path, const an::Status& status);

    void queueEdit(EditJob job);
    void startNextEdit();
    void orientImage(int orientation, const std::string& stepName, const std::string& stepDescription,
                     const char* removedSolutionNote);

    void showDocumentInternal(std::shared_ptr<an::ui::ImageDocument> document);

    void notifyImageDisplayChannelChangedListeners(ImageCompositionMode mode);
    void displayImageChannelFromSelection(int index);

    void closeEvent(QCloseEvent* closeEvent) override;
    void focusInEvent(QFocusEvent* focusInEvent) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* contextMenuEvent) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    void updateFloatingStatus();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
