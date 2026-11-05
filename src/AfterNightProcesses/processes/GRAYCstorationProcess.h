// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/GRAYCstorationProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct GRAYCstorationParams
{
    int interpolation = 0;
    bool fastApproximation = false;

    int iterations = 0;
    int amplitude = 0;

    double sharpness = 0.0;
    double anisotropy = 0.0;
    double noiseScale = 0.0;
    double regularity = 0.0;
    double spatialStepSize = 0.0;
    int angularStepSize = 0;
    double precision = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API GRAYCstorationProcess : public an::ui::TypedProcessControl<GRAYCstorationParams>
{
public:
    GRAYCstorationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageUpdated(ImageViewControl* ctrl) override;

    ~GRAYCstorationProcess();

private:
    void on_iterationsChanged(int value);
    void on_amplitudeChanged(int value);
    void on_sharpnessChanged(double value);
    void on_anisotropyChanged(double value);
    void on_noiseScaleChanged(double value);
    void on_regularityChanged(double value);
    void on_spatialStepSizeChanged(double value);
    void on_angularStepSizeChanged(int value);
    void on_precisionChanged(double value);
    void on_amountChanged(double value);

    void on_interpolationSelector_currentTextChanged(const QString& text);

    void on_fastApproximationClicked(Qt::CheckState state);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const GRAYCstorationParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const GRAYCstorationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const GRAYCstorationParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] GRAYCstorationParams getParams() const override;
    double getTypedModulationFromParams(const GRAYCstorationParams& params) override;

    std::atomic_size_t m_CurrentTaskProgress = 0;

    an::ui::processes::GRAYCstorationProcessUiElements m_ui;

    QSliderSpinBoxUPtr iterationsControl;
    QSliderSpinBoxUPtr amplitudeControl;
    QSliderDoubleSpinBoxUPtr sharpnessControl;
    QSliderDoubleSpinBoxUPtr anisotropyControl;
    QSliderDoubleSpinBoxUPtr noiseScaleControl;
    QSliderDoubleSpinBoxUPtr regularityControl;
    QSliderDoubleSpinBoxUPtr spatialStepSizeControl;
    QSliderSpinBoxUPtr angularStepSizeControl;
    QSliderDoubleSpinBoxUPtr precisionControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    std::string m_JobException;
};
