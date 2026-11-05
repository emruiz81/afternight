// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/NLBayesProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include "controls/QSliderSpinBox.h"

#include <algorithm>
#include <QSpinBox>

class ImageViewControl;

struct NLBayesParams
{
    bool useArea1 = false;
    bool useArea2 = false;
    bool da3d = false;
    bool anscombe = false;
    bool autoSigma = true;

    double sigma = 5.0;
    double amount = 1.0;
    unsigned sosIterations = 1;
    double sosRho = 0.8;
};

class AFTERNIGHTPROCESSES_API NLBayesProcess : public an::ui::TypedProcessControl<NLBayesParams>
{
public:
    NLBayesProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();
    [[nodiscard]] static NLBayesParams sanitizeParamsForExecution(const NLBayesParams& params)
    {
        NLBayesParams effective = params;
        effective.sosIterations =
            (std::min)((std::max)(effective.sosIterations, 1u), an::kMaximumPatchDenoiseIterations);

        if (effective.anscombe)
        {
            effective.da3d = false;
            effective.sosIterations = 1;
        }

        return effective;
    }

    ~NLBayesProcess();

private:
    void on_sigmaChanged(double value);
    void on_amountChanged(double value);

    void on_useArea1Clicked(Qt::CheckState state);
    void on_useArea2Clicked(Qt::CheckState state);
    void on_da3dClicked(Qt::CheckState state);
    void on_anscombeClicked(Qt::CheckState state);
    void on_autoSigmaClicked(Qt::CheckState state);
    void on_sosIterationsChanged(int value);
    void on_sosRhoChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const NLBayesParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const NLBayesParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const NLBayesParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void refreshTheme() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();
    void syncDependentControls(bool enabled);

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] NLBayesParams getParams() const override;
    double getTypedModulationFromParams(const NLBayesParams& params) override;

    QSliderDoubleSpinBoxUPtr sigmaControl;
    QSliderDoubleSpinBoxUPtr amountControl;
    QSliderDoubleSpinBoxUPtr sosRhoControl;

    an::ui::processes::NLBayesProcessUiElements m_ui;
};
