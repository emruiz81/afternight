// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/RangeSelectionProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct RangeSelectionProcessParams
{
    bool linkRangeLimits = false;
    bool screening = false;
    bool lightness = false;
    bool invert = false;

    double lowerLimit = 0.0;
    double upperLimit = 0.0;
    double fuzziness = 0.0;
    double smoothness = 0.0;
};

/// Range Selection: a new mask of the samples between two limits, with ramps across its edges, screened by the source,
/// smoothed and inverted as asked - of a colour image's CIE lightness, or of the image itself. The selection is the
/// engine's `rangeSelectImage` over the run's own source, the smoothing its exact Gaussian.
class AFTERNIGHTPROCESSES_API RangeSelectionProcess : public an::ui::TypedProcessControl<RangeSelectionProcessParams>
{
public:
    RangeSelectionProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~RangeSelectionProcess();

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const RangeSelectionProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;

private:
    void on_lowerLimitChanged(double value);
    void on_upperLimitChanged(double value);

    void on_linkRangeLimitsClicked(int state);

    void on_fuzzinessChanged(double value);
    void on_smoothnessChanged(double value);

    void on_screeningClicked(Qt::CheckState state);
    void on_lightnessClicked(Qt::CheckState state);
    void on_invertClicked(Qt::CheckState state);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const RangeSelectionProcessParams& params) const override;

    void refreshTheme() override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;

    void checkLinkRangeLimits();

    [[nodiscard]] RangeSelectionProcessParams getParams() const override;

    double m_CurrentLinkedLimitsOffset = 0.0;

    QSliderDoubleSpinBoxUPtr lowerLimitControl;
    QSliderDoubleSpinBoxUPtr upperLimitControl;
    QSliderDoubleSpinBoxUPtr fuzzinessControl;
    QSliderDoubleSpinBoxUPtr smoothnessControl;

    an::ui::processes::RangeSelectionProcessUiElements m_ui;
};
