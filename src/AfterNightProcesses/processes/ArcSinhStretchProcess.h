// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/ArcSinhStretchProcessUiBuilder.h"

class ImageViewControl;

struct ArcSinhStretchParams
{
    double stretchFactor = 0.0;
    double blackPoint = 0.0;
    bool humanWeightedLuminance = false;
};

/// The arcsinh stretch over the whole image: each sample moved off the black point and rescaled to the unit interval,
/// then the engine's inverse-hyperbolic-sine curve applied to a colour image's luminance - the human-weighted Rec. 709
/// one or the even mean - and every channel scaled by the luminance's ratio, held to the unit interval, or to a grey
/// image's one channel.
class AFTERNIGHTPROCESSES_API ArcSinhStretchProcess : public an::ui::TypedProcessControl<ArcSinhStretchParams>
{
public:
    ArcSinhStretchProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~ArcSinhStretchProcess();

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ArcSinhStretchParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const ArcSinhStretchParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const ArcSinhStretchParams& params) const override;

private:
    void on_stretchFactorChanged(double value);
    void on_blackPointChanged(double value);
    void refreshTheme() override;

    void on_HumanWeightedLuminanceClicked(Qt::CheckState state);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    void previewControlStarted() override;
    void previewControlClosed() override;
    [[nodiscard]] ArcSinhStretchParams getParams() const override;

    QSliderDoubleSpinBoxUPtr stretchFactorControl;
    QSliderDoubleSpinBoxUPtr blackPointControl;

    an::ui::processes::ArcSinhStretchProcessUiElements m_ui;
};
