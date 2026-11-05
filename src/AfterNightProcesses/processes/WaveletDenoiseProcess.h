// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/WaveletDenoiseProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct WaveletDenoiseParams
{
    double threshold = 0.0;
    int scales = 0;
    int iterations = 0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API WaveletDenoiseProcess : public an::ui::TypedProcessControl<WaveletDenoiseParams>
{
public:
    WaveletDenoiseProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageUpdated(ImageViewControl* ctrl) override;

    ~WaveletDenoiseProcess();

private:
    void on_thresholdChanged(double value);
    void on_scalesChanged(int value);
    void on_iterationsChanged(int value);
    void on_amountChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const WaveletDenoiseParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const WaveletDenoiseParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const WaveletDenoiseParams& params) const override;

    void refreshTheme() override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] WaveletDenoiseParams getParams() const override;
    double getTypedModulationFromParams(const WaveletDenoiseParams& params) override;

    std::atomic_size_t m_CurrentTaskProgress = 0;

    QSliderDoubleSpinBoxUPtr thresholdControl;
    QSliderSpinBoxUPtr scalesControl;
    QSliderSpinBoxUPtr iterationsControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    std::string m_JobException;

    an::ui::processes::WaveletDenoiseProcessUiElements m_ui;
};
