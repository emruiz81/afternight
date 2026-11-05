// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/StatisticalStretchUiBuilder.h"
#include "processes/StretchProcessSupport.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Pointwise/StatisticalStretch.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QRect>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class ImageViewControl;

/// What the statistical stretch is decided from for one image and one linking choice: the image's working copy, its
/// packed mask, and the engine's analysis of it. Measured once per revision of the image and shared by the dialog's
/// clipping figures and its runs.
struct StatisticalStretchMeasurement
{
    /// The image's working copy, its samples fractions of full scale.
    std::shared_ptr<const an::consumer::AnyImage> working;

    /// The image's packed validity mask, or null when its rule reads none.
    std::shared_ptr<const an::ValidityMask> mask;

    /// The engine's analysis of the working copy.
    an::StatisticalStretchAnalysis analysis{};
};

/// The dialog's measurements, one cache per linking choice, shared with its runs.
struct StatisticalStretchMeasurements
{
    an::ui::processes::StretchStatisticsCache<StatisticalStretchMeasurement> linked;
    an::ui::processes::StretchStatisticsCache<StatisticalStretchMeasurement> unlinked;

    /// The cache of a linking choice.
    [[nodiscard]] an::ui::processes::StretchStatisticsCache<StatisticalStretchMeasurement>& of(bool linkedChannels)
    {
        return linkedChannels ? linked : unlinked;
    }
};

struct StatisticalStretchParams
{
    double targetMedian = 0.25;
    double blackPointSigma = 5.0;
    double curvesBoost = 0.2;

    bool linked = false;
    bool normalize = false;
    bool applyCurves = false;
    bool noBlackClip = false;

    /// The image the dialog targeted when the run was issued: a preview area is that image's stretch, cropped.
    std::shared_ptr<const an::consumer::ImageAsset> target;

    /// The preview area shown when the run was issued; empty for the whole image.
    QRect area;

    /// The dialog's measurements.
    std::shared_ptr<StatisticalStretchMeasurements> measurements;
};

/// The engine's statistical stretch parameters a dialog's parameters ask for.
///
/// @param params the dialog's parameters.
/// @param channels the image's channel count: a grey image has one population, whatever the dialog says.
/// @return the engine's parameters.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StatisticalStretchParameters
statisticalStretchParametersOf(const StatisticalStretchParams& params, std::uint32_t channels) noexcept;

/// The measurement a statistical stretch of an image takes: the image's working copy and the engine's analysis of it,
/// under the image's validity.
///
/// @param engine the engine context of the execution.
/// @param execution the admitted execution the measurement runs under.
/// @param image the image.
/// @param mask its packed mask, or null.
/// @param linked whether a colour image's channels form one population.
/// @return the measurement; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<StatisticalStretchMeasurement>
measureStatisticalStretch(const an::EngineContext& engine, const an::ExecutionContext& execution,
                          const an::consumer::AnyImage& image, const an::ValidityMask* mask, bool linked);

class AFTERNIGHTPROCESSES_API StatisticalStretch : public an::ui::TypedProcessControl<StatisticalStretchParams>
{
public:
    StatisticalStretch(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~StatisticalStretch() override;

private:
    void on_targetMedianChanged(double value);
    void on_blackPointSigmaChanged(double value);
    void on_curvesBoostChanged(double value);
    void refreshTheme() override;

    void on_linkedStretchCheck_clicked(Qt::CheckState state);
    void on_normalizeImageCheck_clicked(Qt::CheckState state);
    void on_applyCurvesCheck_clicked(Qt::CheckState state);
    void on_noBlackClipCheck_clicked(Qt::CheckState state);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const StatisticalStretchParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const StatisticalStretchParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const StatisticalStretchParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;
    [[nodiscard]] StatisticalStretchParams getParams() const override;

    void sourceImageChanged();
    void updateDependentControls();
    void updateClipStats();
    void showClipStats(const an::Result<an::StatisticalStretchClipStatistics>& clipping, std::uint32_t channels,
                       bool linked, bool noBlackClip);

    QSliderDoubleSpinBoxUPtr targetMedianControl;
    QSliderDoubleSpinBoxUPtr blackPointSigmaControl;
    QSliderDoubleSpinBoxUPtr curvesBoostControl;

    std::shared_ptr<StatisticalStretchMeasurements> m_Measurements = std::make_shared<StatisticalStretchMeasurements>();

    // The clipping figures are measured on a worker, one measurement at a time; a request while one runs is taken up,
    // with the dialog's values then, when it lands.
    bool m_ClipStatsRunning = false;
    bool m_ClipStatsPending = false;

    an::ui::processes::StatisticalStretchUiElements m_ui;
};
