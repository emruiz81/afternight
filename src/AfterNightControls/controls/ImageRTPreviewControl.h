// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QScrollArea>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ImageCropController.h"
#include "ImageProcessControl.h"
#include "ProgressDialogControl.h"
#include "controls/ProcessCompanions.h"
#include "controls/ProcessExecution.h"
#include "controls/QSliderSpinBox.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

class ImageViewControl;
class QCheckBox;
class QDialogButtonBox;
class WaitingSpinnerWidget;

// 0 No preview, 1: Sync preview, 2 : Async preview, 3 : Take snapshot from preview
enum class RTPreviewMode
{
    NoPreview = 0,
    SyncPreview = 1,
    AsyncPreview = 2,
    TakeSnapshot = 3
};

/// What a process window's runs came to so far.
struct ProcessRunTelemetry
{
    /// Previews asked for, by a parameter change, an image change or the preview's start.
    std::uint64_t previewsRequested = 0u;

    /// Previews that ran; a burst of requests while one runs starts one more, with the latest parameters.
    std::uint64_t previewsStarted = 0u;

    /// Previews whose result the preview view showed.
    std::uint64_t previewsCommitted = 0u;

    /// Previews that came back no longer current - cancelled, superseded, or over an image that changed - and were
    /// dropped with nothing shown.
    std::uint64_t previewsDiscarded = 0u;

    /// Previews the body or the engine refused.
    std::uint64_t previewsFailed = 0u;

    /// Previews shown that ran once the parameters had settled - a change made without a held control, or the frame
    /// that follows a drag - the settled kind of frame.
    std::uint64_t settledPreviewsCommitted = 0u;

    /// Previews shown that ran while a person dragged one of the window's sliders - the interactive kind of frame.
    std::uint64_t interactivePreviewsCommitted = 0u;

    /// Previews whose parameters change nothing: no body ran, and the preview view showed the target's own image.
    std::uint64_t identityPreviews = 0u;

    /// Times the frame rate was read from the settings store - off the GUI thread, when the preview is turned on, the
    /// window activated or a chain of frames begins - each read paced the chains that began after it landed.
    std::uint64_t frameRatesRead = 0u;

    /// Distinct causes of failed previews the window's banner reported; a preview that fails again for the cause the
    /// banner shows reports nothing more.
    std::uint64_t failuresReported = 0u;

    /// Times the waiting spinner was shown, each once a preview had run for the spinner's delay.
    std::uint64_t spinnersShown = 0u;

    /// Interactive frames shown that ran over what the preview view paints of the target - the target reduced by the
    /// view's factor, or a part of it at full scale - rather than over the whole of it.
    std::uint64_t viewFramesCommitted = 0u;

    /// Of those, the frames over the part of the target the view shows that were painted as a layer over what the view
    /// paints, rather than as its presentation of the whole region it presents.
    std::uint64_t viewFramesLayered = 0u;

    /// The factor the last frame shown over a reduction of the target ran at; one when none has.
    std::uint32_t viewFrameFactor = 1u;

    /// Frames whose presentation, made in the frame's own operation, the preview view painted as it came.
    std::uint64_t presentationsAdopted = 0u;

    /// Histograms frames handed the dialog, by how each was made: the count of the frame's whole result; the target's
    /// codes remapped through what the body made of every code; and the count of the body's result over a sample of the
    /// target taken one pixel per block.
    std::uint64_t histogramsCounted = 0u;
    std::uint64_t histogramsRemapped = 0u;
    std::uint64_t histogramsSampled = 0u;

    /// Analyses of the image the preview is stretched by that frames measured; each is measured once for a revision of
    /// that image and the view's presentation.
    std::uint64_t analysesMeasured = 0u;

    /// Conversions frames made of what they read, which the dialog keeps for the frames that follow - the forward half
    /// of a component of another encoding, and a mask or a source laid out a plane per channel in the result's
    /// representation - and the times a frame read one kept from before instead of making it.
    std::uint64_t scopesPrepared = 0u;
    std::uint64_t scopesReused = 0u;
    std::uint64_t planarConversionsMade = 0u;
    std::uint64_t planarConversionsReused = 0u;

    /// Crops of the target and of its mask under the preview area, or under the region the preview view shows, that
    /// frames made and the dialog keeps, and the times a frame ran over one kept from before.
    std::uint64_t cropsMade = 0u;
    std::uint64_t cropsReused = 0u;

    /// Bases of the preview area's composite that frames built: the target in its patches' description, which a patch
    /// of the target's own description over a target held a plane per channel does without.
    std::uint64_t compositeBasesBuilt = 0u;

    /// Interactive frames over the preview area shown as their patch painted over what the preview view paints, no
    /// composite of the whole image made, and their histograms, derived from the counts of the target, of its part
    /// under the area and of the patch.
    std::uint64_t patchFramesCommitted = 0u;
    std::uint64_t histogramsDerived = 0u;

    /// How long the frames shown spent in their admitted operation, in all - processing, measuring, counting and
    /// presenting - and the longest one did.
    std::chrono::nanoseconds frameWorkTime{0};
    std::chrono::nanoseconds longestFrameWork{0};

    /// How long the frames shown waited to be admitted, in all, and the longest any waited.
    std::chrono::nanoseconds admissionWaitTime{0};
    std::chrono::nanoseconds longestAdmissionWait{0};

    /// How long the previews shown took in all, each from its start on the GUI thread until the GUI thread handed its
    /// result to the preview view.
    std::chrono::nanoseconds previewTime{0};

    /// The longest any preview shown took, from its start until its result was handed to the preview view.
    std::chrono::nanoseconds longestPreview{0};

    /// Applies, new images, snapshots and sequence runs whose result was published.
    std::uint64_t runsCommitted = 0u;

    /// Runs that came back no longer current, or whose result the history or the view refused, with nothing published.
    std::uint64_t runsDiscarded = 0u;

    /// Runs stopped by Cancel or by closing the window, with nothing published.
    std::uint64_t runsCancelled = 0u;

    /// Runs the body or the engine refused, with nothing published.
    std::uint64_t runsFailed = 0u;
};

/// A process window with a real-time preview: the preview view beside the controls, the preview area within it, the
/// histogram, and the runs - the preview, the Apply, the new image, the snapshot and the sequence - that the framework
/// sends to workers and publishes on the GUI thread.
///
/// **A preview drains to the latest.** A parameter or image change asks for a preview; while one runs, or the frame
/// period the Processing settings state has not passed since the last one started, every further request collapses
/// into one that starts with the latest parameters, so a dragged slider never starves the preview of a finished frame
/// and nothing runs while nothing changes. Turning the preview off, an Apply, a closed window or a changed image
/// refuses the preview on its way: its result is dropped, never shown.
///
/// **A drag is shown in interactive frames, and a settled frame follows it.** While a person holds a slider the window
/// watches (`watchInteraction`, which the `INIT_*SPINBOX_CTRL` macros call), the previews its changes ask for are
/// interactive frames, run on the session's preview reservation; letting it go, or holding it still for the settle
/// delay, asks for one settled frame, which may borrow every thread a preview can. Every other request is settled.
///
/// **A preview that changes nothing runs nothing.** Parameters the process declares the identity (`isIdentity`) show
/// the target's own image in the preview view, shared rather than copied, as the view does until the first frame lands.
///
/// **A frame is one operation.** It processes, counts what the dialog's histogram shows and presents what the preview
/// view paints in one admitted operation, and the view and the histogram show what it made as it lands, asking for
/// nothing more. An interactive frame of a process that declares it may (`previewScaling`) runs over what the view
/// paints of the target - the target reduced by the view's factor, or the part the view shows at full scale, grown by
/// the reach the process states - and only its presentation and its histogram show it, the part painted as a layer
/// over what the view paints; the preview's document holds what the last settled frame made.
///
/// **A result is published only while its request is current.** Every run captures its target's revision on the GUI
/// thread; a preview, a new image, a snapshot or a sequence run commits only on its ticket's `Commit` verdict, and an
/// Apply is the view's edit, which holds the document's writer from its read to its publication and is recorded in the
/// history before the view shows it.
class AFTERNIGHTCONTROLS_API ImageRTPreviewControl : public ImageProcessControl,
                                                     public ImageAreaSelectorControl,
                                                     public ImageViewWidgetDisplayModule,
                                                     public an::ui::IPreviewInteractionWatcher
{
public:
    ImageRTPreviewControl(ImageViewControl* control, const std::string& name);

    void changeTargetViewControl(ImageViewControl* control) override;

    bool isRTPreviewProcess() const override;

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onImageRenamed(ImageViewControl* ctrl) override;

    void onMaskUpdated(ImageViewControl* ctrl) override;

    void onAlternateImageEnabled(ImageViewControl* ctrl) override;

    void onAlternateImageDisabled(ImageViewControl* ctrl) override;

    void onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    void onRTTakeSnapshot(ImageViewControl* ctrl) override;

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;

    void disableRTPreview();

    void refreshTheme() override;

    // op: 0: start, 1: stop
    void controlSpinner(int op);

    // op: 0: start, 1: stop
    void controlSpinnerAsync(int op);

    /// Whether the preview needs the target view to itself among previews; a real-time preview does unless it says
    /// otherwise.
    [[nodiscard]] bool requiresExclusivePreviewAccess() const override;

    /// Turns the preview off, as its checkbox does.
    void disableRealtimePreview() override;

    /// Applies the process to the target as the Apply button does: in place, to a new image or over a sequence, as the
    /// process's batch parameters say. A preview on its way is refused first, so the click is never lost to it.
    void applyProcess();

    /// The view the preview is shown in, or null while the preview is off.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// The area the preview runs over, in the target's pixels; empty when it runs over the whole image.
    [[nodiscard]] QRect previewArea() const;

    /// What the runs came to so far.
    [[nodiscard]] ProcessRunTelemetry runTelemetry() const;

    /// Whether no preview or run is on its way, none waits to start and the preview view's own work has landed.
    [[nodiscard]] bool isProcessIdle() const;

    /// Watches a slider a person drags: while it is held, the previews its changes ask for are interactive frames, and
    /// letting it go - or holding it still for the settle delay - asks for a settled frame. A control destroyed while
    /// held is let go of without one.
    ///
    /// @param control the control; it may be destroyed before the window, or with it as one of its children.
    void watchInteraction(QSliderSpinBox& control) override;

    /// `watchInteraction` for a control of real numbers.
    ///
    /// @param control the control; it may be destroyed before the window, or with it as one of its children.
    void watchInteraction(QSliderDoubleSpinBox& control) override;

    /// What an interactive frame may run the body over: `PreviewScaling::FullOnly`, the default, the whole target as
    /// every other run does; `PreviewScaling::Decimated` for a body that maps every pixel by one rule wherever it lies
    /// and whatever the image's size and reads no other image, which then runs over what the preview view paints of the
    /// target, is presented through the target's own stretch, and answers a change made without a held slider with an
    /// interactive frame before the settled one; `PreviewScaling::NativeRegion` for a body whose answer depends on the
    /// pixel grid and on nothing beyond the reach it states (`previewRegionMargin`), which then runs at full scale over
    /// the part of the target the view shows - every frame over the whole target, settled, while the view shows all of
    /// it - and also answers such a change with an interactive frame first. Read on the GUI thread when a frame is
    /// issued.
    [[nodiscard]] virtual an::ui::PreviewScaling previewScaling() const;

    /// Whether every sample the body writes depends on the sample of the same channel at the same place alone, for the
    /// stored channels: then a frame over a reduction of an eight- or sixteen-bit target shows, in the dialog's
    /// histogram, the target's own codes remapped through what the body makes of every code - exactly what a count of
    /// its whole result would show. False, the default, for a body that mixes channels; its frame's histogram is
    /// counted over a sample of the target instead. Read on the GUI thread when a frame is issued.
    [[nodiscard]] virtual bool mapsEachChannelAlone() const;

    /// Whether the body asks the engine for work of its own while it runs, as an extension's preview does through its
    /// SDK. That admission would wait for the threads the frame borrowed, which come back only when the frame ends, so
    /// every frame of such a body runs on what previews are sure of and borrows nothing, as an interactive frame does.
    /// False, the default, for a body that works on the frame's own execution. Read on the GUI thread when a frame is
    /// issued.
    [[nodiscard]] virtual bool bodyAdmitsWorkOfItsOwn() const;

    /// Stops every preview frame and run on its way - an Apply, a new image, a snapshot, a sequence - and waits for
    /// none: each ends at its next stop point and its result is refused.
    void onStopWorkInFlight() override;

    ~ImageRTPreviewControl() noexcept override;

protected:
    void on_rt_preview_clicked(Qt::CheckState state);

    void on_rt_dialog_box_button_clicked(QAbstractButton* button);

    void on_rt_plotResolution_currentTextChanged(const QString& str);

    void on_rt_gridClicked(bool checked);
    void on_rt_zoomInButton();
    void on_rt_zoomOutButton();
    void on_rt_zoomResetButton();
    void on_rt_localChannelNormalization(bool checked);
    void on_rt_showClippingInfo(bool checked);
    void on_rt_showHistogram(bool checked);
    void on_rt_showHistogramAreas(bool checked);
    void on_rt_preview_area_toggled(bool checked);

protected:
    void setExecutionAutostretchSupported(bool supported);
    bool isExecutionAutostretchSupported() const;

    virtual bool isPreviewEnabled() const = 0;

    virtual void enablePreview(bool enabled) = 0;

    void closeEvent(QCloseEvent* event) override;

    void onDraw(QPainter& painter) override;

    void onDisplayModeChanged(ImageViewMode mode) override;
    void onAutostretchContextChanged() override;

    void changeEvent(QEvent* e) override;

    void resetSettings() override;

    /// Asks for a preview with the parameters the window holds now - an interactive frame while a person holds one of
    /// the sliders the window watches, a settled one otherwise. While one runs or the frame period has not passed, the
    /// request waits and is collapsed with any that follow into one.
    void schedulePreviewProcessExecution();

    /// The process itself, on a worker: reads the source, writes the destination - which starts as a copy of the source
    /// - and returns whether it succeeded. It reads only what the run snapshotted, never a widget.
    ///
    /// @param source what the run reads: the target, the preview area's crop or a sequence frame.
    /// @param destination what the run writes.
    /// @param run what the run snapshotted: why it runs, its channel, its frame.
    /// @param params the parameters snapshotted when the run was issued.
    /// @param scope the run's admitted execution, whose progress sink the process reports to and whose stop it checks.
    /// @return success, or why the run produced nothing; a stopped run returns the engine's `ErrorCode::Cancelled`.
    virtual an::Status executeProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                          const an::ui::ProcessRun& run, const ProcessExecutionParams* params,
                                          an::ui::ExecutionScope& scope) = 0;

    /// The history step an Apply records, decided from its parameters on the GUI thread before it runs. The process's
    /// name and no description unless the process says otherwise.
    [[nodiscard]] virtual an::ui::ProcessStep describeProcessStep(const ProcessExecutionParams* params) const;

    /// The images a run of these parameters reads beside its target and the target's mask - another view's image the
    /// process's body reads - decided on the GUI thread when the run is issued. The run leases each before it asks to
    /// be admitted, and an Apply in place in one order with its target's writer, and the body reads them through
    /// `readBeside`; an image read and not named here would be waited for under the run's admission. None unless the
    /// process says otherwise.
    [[nodiscard]] virtual std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    imagesRead(const ProcessExecutionParams* params) const;

    /// What a run of these parameters writes of its destination, decided on the GUI thread when the run is issued.
    /// `ProcessBodyCoverage::WritesEverySample` for parameters under which the body writes every sample of every
    /// channel of its destination - or replaces it - and reads none of it first: the run is then handed a destination
    /// that starts unwritten wherever the source allows, and none of the source is copied into it. `WritesPart`, the
    /// default, for a body that may leave a sample alone, which it finds holding the source's.
    [[nodiscard]] virtual an::ui::ProcessBodyCoverage processBodyCoverage(const ProcessExecutionParams* params) const;

    /// Whether these parameters change nothing - the body's result is its source, sample for sample - decided on the
    /// GUI thread when a preview is issued. A preview of the identity runs nothing: the preview view shows the target's
    /// own image, which is what the mask and the modulation would blend back to. False, the default, for a process that
    /// declares none.
    [[nodiscard]] virtual bool isIdentity(const ProcessExecutionParams* params) const;

    /// How many pixels beyond the part of the target the preview view shows an interactive frame at full scale reads of
    /// the target around it, decided on the GUI thread when a frame is issued. A body whose answer at a pixel reads its
    /// neighbours - a blur's kernel, a median's window, an element's cells, each pass of an iterated filter in turn -
    /// states the farthest any output pixel reads, so the frame answers the part's edge as it is answered over the
    /// whole image and shows the part alone; zero, the default, for a body that reads each pixel alone.
    [[nodiscard]] virtual std::uint32_t previewRegionMargin(const ProcessExecutionParams* params) const;

    /// Called on the run's worker with the finished result, after the modulation and the mask and before the GUI thread
    /// publishes it, for a process that derives something from the result. Reads only.
    virtual void imageReadyToRender(const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) {}

    virtual void executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode) {};

    virtual void executeProcessEnded(const RTPreviewMode previewMode, const bool sequenceMode,
                                     const bool succeeded) final;

    // Runs on the main thread only after the process result has been committed successfully.
    virtual void executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode) {};

    // Runs in main thread, inherited from child processes
    virtual void executeProcessEndedImpl(const RTPreviewMode previewMode, const bool sequenceMode) {};

    void settingsUpdated();

    virtual void settingsUpdatedImpl() = 0;

    void updateNeeded() override;

    void createPreviewControl();

    void destroyPreviewControl(bool processEnding = false);

    /// The image the preview view shows when the person asks to see the original: the target's, unless the process
    /// shows another. Read on the GUI thread.
    [[nodiscard]] virtual std::shared_ptr<const an::consumer::ImageAsset> getAlternateAsset() const;

    /// The image whose statistics stretch the preview when the process supports a stretch of its own: the target's,
    /// so the preview is stretched as the applied result will be, unless the process names another. Null measures the
    /// preview's own result. Read on the GUI thread.
    [[nodiscard]] virtual std::shared_ptr<const an::consumer::ImageAsset> getExecutionAutostretchSource() const;

    void updateAlternateImage(bool notifyImageViewWidget);

    void recreatePreviewSubAreaController();
    void destroyPreviewSubAreaController();
    void resetPreviewSubAreaToDefault();
    void ensurePreviewSubAreaInitialized();
    void updatePreviewSubAreaOverlay();
    void clearPreviewSubAreaOverlay();

    [[nodiscard]] QRect normalizedPreviewSubArea(const QRect& area) const;

    void setupPreviewControl(const bool useSubArea, const QSize& subAreaSize = QSize(0, 0),
                             const QString& subAreaText = "");

    /// Options for initializeStandardProcess — prefer this overload for readability.
    struct ProcessInitOptions
    {
        bool supportPerChannel = false;
        bool showWaitingSpinner = false;
        bool previewAreaSelectionEnabled = true;
    };

    void initializeStandardProcess(QLayout* mainLayout, QLayout* previewLayout, QCheckBox* previewCheckbox,
                                   QDialogButtonBox* buttonBox, QComboBox* targetSelector, QComboBox* channelSelector,
                                   const ProcessInitOptions& options);

    /// The options as two flags — delegates to the ProcessInitOptions overload.
    void initializeStandardProcess(QLayout* mainLayout, QLayout* previewLayout, QCheckBox* previewCheckbox,
                                   QDialogButtonBox* buttonBox, QComboBox* targetSelector, QComboBox* channelSelector,
                                   bool supportPerChannel, bool showWaitingSpinner);

    void setupHistogramPreview(QScrollArea* _histogramPreviewScrollArea, QComboBox* _plotResolution,
                               QAbstractButton* zoomIn, QAbstractButton* zoomOut, QAbstractButton* zoomReset,
                               QAbstractButton* gridButton, QAbstractButton* localChannelNormalization,
                               QAbstractButton* showClippingInfo, QAbstractButton* showHistogram,
                               QAbstractButton* showHistogramAreas);

    virtual void previewControlStarted() {}
    virtual void previewControlClosed() {}

    void onContentLayoutReplaced(QBoxLayout* contentLayout) override;

    void enableProcessControls(bool enabled) override;

    virtual ProgressDialogControl* getProgressDialogControl(const bool forSequence)
    {
        return nullptr;
    }

    // Implemented in the process itself.
    virtual ProcessExecutionParamsPtr getProcessParamsForExecution()
    {
        return nullptr;
    }
    virtual double getModulationFromParams(const ProcessExecutionParams* params)
    {
        return 1.0;
    }

    /// How an Apply runs: whether it makes a new image and under what name, and whether the window closes after it.
    /// Every run goes to a worker, so `executeInBackground` is not read.
    virtual void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                             std::string& newImageName)
    {
        createNewImage = false;
        closeAtEnd = false;
        executeInBackground = true;
    }

    void updateSupportedPlotResolutions();
    void invalidateExecutionAutostretchStatistics();

    /// Refuses the preview on its way and every one waiting to start: nothing of them is shown, and no settled frame is
    /// owed for them.
    void cancelPreviews();

    /// Holds every preview request back while a reset changes the parameters, so the requests it makes collapse into
    /// the one `releasePreviewRequests` asks for; holds nest.
    void holdPreviewRequests();
    void releasePreviewRequests();

    /// A control a person drags was pressed or let go: while it is held, the previews its changes ask for are
    /// interactive frames, and letting the last held control go asks for a settled one. For a control that is no
    /// slider of the framework's - a curve's point, a slider the window links itself - which `watchInteraction` does
    /// not reach.
    ///
    /// @param control the control, by identity; it is never read.
    /// @param held whether it is held now.
    void controlHeld(const QObject* control, bool held);

    /// Forgets a control that is being destroyed, as if let go but asking for nothing.
    ///
    /// @param control the control, by identity.
    void forgetControl(const QObject* control);

    /// Whether the person may draw a preview area on the preview view; the area is put away when they may not. Read
    /// when the preview view is made and whenever the window's controls are enabled.
    ///
    /// @param enabled whether they may.
    void setPreviewAreaSelectionEnabled(bool enabled);

private:
    struct Impl;
    struct RunSnapshot;

    [[nodiscard]] std::shared_ptr<RunSnapshot> snapshotRun(an::ui::ProcessRunKind kind,
                                                           ProcessExecutionParamsPtr params);

    /// Asks for a frame of a kind, collapsed with the one waiting to start, if any; a settled one makes it settled.
    void requestPreview(an::ui::PreviewFrameKind kind);

    /// Whether an interactive frame asked for now would run over what the preview view paints of the target - a
    /// reduction of it, or a part of it at full scale - rather than over the whole of it or the preview area.
    [[nodiscard]] bool interactiveFrameRunsOverView() const;

    /// Starts the frame waiting to start, once no frame runs and the frame period has passed since the last one began;
    /// arms the pacing timer when the period has not.
    void pumpPreview();

    void startPreview(an::ui::PreviewFrameKind kind);

    /// Shows the target's own image in the preview view for a frame whose parameters change nothing.
    void showIdentityPreview();

    /// Fits the preview view, shown and laid out, to the target's image it shows as it opens, before any result, and
    /// again as its viewport is shown or resized until that result lands, unless the person zoomed since.
    void fitPreviewBeforeItsFirstResult();

    /// Fits the preview view to a result landing in it when a fit is owed: always when none was made as it opened,
    /// and when one was, only for a first result of another size than the image fitted then.
    void fitLandingPreview(ImageViewControl& preview);

    /// Shows a preview's failure in the window's banner, once for each cause.
    void reportPreviewFailure(const an::Status& status);

    /// Takes the reported failure out of the banner once a preview is shown, if the banner still shows it.
    void clearReportedPreviewFailure();

    /// Hides the waiting spinner and stops its delay once no frame runs.
    void settleSpinner();

    /// Lets go what frames converted of the target and its mask, and the counts kept of them, once the target or its
    /// mask has changed: their revisions are never read again, and the memory goes now rather than when it is evicted.
    void releaseFrameConversions();

    /// Lets go everything frames kept of the target and its mask - the conversions and counts above, the reductions,
    /// samples and crops, the codes and the preview area's composite base - once the target or its mask has changed.
    void releaseFrameCaches();

    /// Reads the frame rate from the settings store off the GUI thread, once at a time; what it answers paces the
    /// chains of frames that begin after the read lands.
    void refreshFramePeriod();

    void runApplyInPlace(std::shared_ptr<RunSnapshot> snapshot, ProgressDialogControl* progress, bool closeAtEnd);
    void runToNewImage(std::shared_ptr<RunSnapshot> snapshot, ProgressDialogControl* progress,
                       const std::string& newImageName, bool closeAtEnd);
    void runOverSequence(std::shared_ptr<RunSnapshot> snapshot, ProgressDialogControl* progress, bool createNewSequence,
                         const std::string& newName, bool closeAtEnd);
    void takeSnapshot(const std::string& newImageName);
    void finishRun(RTPreviewMode mode, bool sequenceMode, an::Status status, bool published, bool closeAtEnd);

    std::unique_ptr<Impl> m_impl;

protected:
    bool& m_ShowWaitingSpinner;

    std::atomic_bool& m_SuspendBackgroundUpdates;
    std::atomic_bool& m_IgnoreLastImageUpdatedEvent;

    ImageViewControl*& m_rt_preview_view_control;

    bool& m_rt_preview_use_sub_area;
    QRect& m_rt_preview_sub_area;

    QSize& m_InitiaSubAreaSize;
    QString& m_SubAreaText;

    QSize& m_DefaultSizePreview;
    QSize& m_DefaultSizeNoPreview;

    QLayout*& m_ImagePreviewLayout;
    QScrollArea*& m_HistogramPreviewScrollArea;
    QBoxLayout*& m_MainHorizontalLayout;

    QComboBox*& m_PlotResolution;

    QAbstractButton*& m_ZoomIn;
    QAbstractButton*& m_ZoomOut;
    QAbstractButton*& m_ZoomReset;
    QAbstractButton*& m_GridButton;
    QAbstractButton*& m_LocalChannelNormalization;
    QAbstractButton*& m_ShowClippingInfo;
    QAbstractButton*& m_ShowHistogramAreas;

    QAbstractButton*& m_ShowHistogram;

    std::unique_ptr<ImageCropController>& m_CropController;

    bool& m_SupportMasking;

    bool& m_FixedLayout;

    bool& m_CreatedViewControlsAreNeighbour;

    std::unique_ptr<WaitingSpinnerWidget>& m_ImagePreviewSpinner;

    bool& m_FitToWindowRequested;

    bool& m_UseHistogramPreviewWidget;
    std::unique_ptr<an::ui::IPreviewHistogram>& m_HistogramPreview;

    bool& m_SupportExecutionAutostretch;
};
