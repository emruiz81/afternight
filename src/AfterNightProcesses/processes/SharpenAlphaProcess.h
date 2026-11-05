// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/SharpenAlphaProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct SharpenAlphaParams
{
    double amplitude = 0.0;
    int numOfScales = 0;
    double anisotropy = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API SharpenAlphaProcess : public an::ui::TypedProcessControl<SharpenAlphaParams>
{
public:
    SharpenAlphaProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~SharpenAlphaProcess();

private:
    void on_amplitudeChanged(double value);
    void on_numOfScalesChanged(int value);
    void on_anisotropyChanged(double value);
    void on_amountChanged(double value);

    void on_reduceHalosClicked(int state);

    // void on_modeComboBox_currentTextChanged(const QString& str);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const SharpenAlphaParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const SharpenAlphaParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const SharpenAlphaParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] SharpenAlphaParams getParams() const override;
    double getTypedModulationFromParams(const SharpenAlphaParams& params) override;

    QSliderDoubleSpinBoxUPtr amplitudeControl;
    QSliderSpinBoxUPtr numOfScalesControl;
    QSliderDoubleSpinBoxUPtr anisotropyControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::SharpenAlphaProcessUiElements m_ui;
};
