// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include "processes/CurvesTransformationProcessUiBuilder.h"
#include "processes/StretchProcessSupport.h"
#include "controls/TypedProcessControl.h"
#include "widgets/CurvesWidget.h"
#include "widgets/ToneCurveModel.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <memory>
#include <string>

class ImageViewControl;

namespace an::ui
{
class HistogramCountScheduler;
struct HistogramCountTelemetry;
} // namespace an::ui

struct CurvesTransformationParams
{
    /// What the curve runs on: the whole image, a stored channel or a component of another encoding.
    an::ChannelScope channel = an::ChannelScope::Whole;

    /// The channel selector's and the interpolation list's texts, for the history.
    std::string channelLabel = "RGB";
    std::string interpolationLabel = "Cubic";

    /// The curve the widget holds: its points and the rule the person chose.
    an::ui::ToneCurveModel curve;
};

/// The entries of the table a floating image's curve is sampled into, for a preview and an Apply alike.
///
/// The program reads its table in Float32, and over a curve across the unit interval - the dialog's - a power of two
/// of intervals puts a sample's position in the table exactly, where across the engine's largest table, 65,535
/// intervals, the position rounds by up to a five-hundredth of an interval. Read linearly, the table stands from the
/// curve by at most an eighth of an interval's width squared times the curve's curvature, under half a Float32 unit at
/// the top of the range for any curve whose curvature stays below thirty, so 8,192 intervals write outputs as close to
/// the curve as the largest table and closer on the curves the dialog draws. It is an eighth of the largest table to
/// build every time the curve moves, in memory the allocator keeps rather than half a mebibyte mapped afresh.
inline constexpr std::uint32_t kCurvesTableEntries = 8193u;

/// The engine's tone-curve plan for a curve over samples of one representation: an exact table over every code of an
/// eight- or sixteen-bit integer, and over floating samples a table of `kCurvesTableEntries`, read linearly - the same
/// plan for a preview and an Apply. A curve is joined by the rule the widget evaluates it with.
///
/// @param curve the curve.
/// @param sampleType the samples' representation: UInt8, UInt16, Float32 or Float64.
/// @param channels the channels per pixel.
/// @return the plan; or what the engine refused, `ErrorCode::Unsupported` for another representation.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::CurveProgramPlan>
curvesProgramFor(const an::ui::ToneCurveModel& curve, an::SampleType sampleType, std::uint32_t channels);

/// The same plan, built on a run's own workers, which is how a preview or an Apply builds it: a floating image's
/// table is measured against the curve at three points inside each of its intervals, every time the curve moves.
///
/// @param execution the run.
/// @param curve the curve.
/// @param sampleType the samples' representation: UInt8, UInt16, Float32 or Float64.
/// @param channels the channels per pixel.
/// @return what `curvesProgramFor(curve, sampleType, channels)` returns, or the run's stop.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::CurveProgramPlan>
curvesProgramFor(const an::ExecutionContext& execution, const an::ui::ToneCurveModel& curve, an::SampleType sampleType,
                 std::uint32_t channels);

class AFTERNIGHTPROCESSES_API CurvesTransformationProcess
    : public an::ui::TypedProcessControl<CurvesTransformationParams>,
      public CurvesWidgetListener
{
public:
    CurvesTransformationProcess(ImageViewControl* control);

    void refreshTheme() override;

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onAlternateImageEnabled(ImageViewControl* ctrl) override;

    void onAlternateImageDisabled(ImageViewControl* ctrl) override;

    void onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode) override;

    void onCurvesUpdated(const an::ui::ToneCurveModel& curve) override;

    /// A point of the curve held and dragged is a drag, as a held slider is: its changes are shown in interactive
    /// frames, and letting it go - or holding it still for the settle delay - asks for the settled frame.
    void onCurvePointHeld(bool held) override;

    /// The curve maps every pixel by one rule wherever it lies and reads no other image, so an interactive frame runs
    /// over what the preview view paints of the target.
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;

    /// What the counts of the curve editor's histogram came to so far: the editor counts the image the preview shows
    /// itself, apart from the frames - after each settled frame, never while a drag's interactive frames are shown.
    [[nodiscard]] an::ui::HistogramCountTelemetry histogramTelemetry() const;

    ~CurvesTransformationProcess() override;

private:
    void on_channelSelector2_currentTextChanged(const QString& str);
    void on_interpolationSelector_currentTextChanged(const QString& str);

    void on_gridClicked(bool checked);
    void on_guideLineClicked(bool checked);
    void on_HistogramClicked(bool state);
    void on_ZoomInButton();
    void on_ZoomOutButton();
    void on_ZoomResetButton();
    void on_showHistogramAreas(bool checked);

    void updateSupportedImageChannels();
    [[nodiscard]] bool isColourTarget() const;

    /// Counts the image the preview shows - or the target while the preview shows its original - into the curve's
    /// histogram, when the histogram is on.
    void recountHistogram();

    void resetSettings() override;

    void resetCurve();

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const CurvesTransformationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const CurvesTransformationParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const CurvesTransformationParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const CurvesTransformationParams& params) const override;

    [[nodiscard]] CurvesTransformationParams getParams() const override;

    void previewControlStarted() override;
    void previewControlClosed() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    an::ui::processes::CurvesTransformationProcessUiElements m_ui;

    std::unique_ptr<CurvesWidget> m_curves_widget;
    std::unique_ptr<an::ui::HistogramCountScheduler> m_HistogramCounts;

    bool m_ShowingAlternate = false;
};
