// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/NoiseGeneratorProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

#include <cstdint>

class ImageViewControl;

struct NoiseGeneratorParams
{
    int noiseTypeSelector = 0;

    double mean = 0.0;
    double var = 0.0;
    double amount = 0.0;
    double saltVsPepper = 0.0;

    /// What the draw is keyed on, so a preview and the Apply after it draw the same noise.
    std::uint64_t seed = 0u;
};

class AFTERNIGHTPROCESSES_API NoiseGeneratorProcess : public an::ui::TypedProcessControl<NoiseGeneratorParams>
{
public:
    NoiseGeneratorProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~NoiseGeneratorProcess();

    void refreshTheme() override;

private:
    void on_meanChanged(double value);
    void on_varChanged(double value);
    void on_amountChanged(double value);
    void on_saltVsPepperChanged(double value);

    void on_noiseTypeSelector_currentTextChanged(const QString& str);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const NoiseGeneratorParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const NoiseGeneratorParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const NoiseGeneratorParams& params) const override;
    void executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    void updateEnabledActions(const QString& str);

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] NoiseGeneratorParams getParams() const override;

    QSliderDoubleSpinBoxUPtr meanControl;
    QSliderDoubleSpinBoxUPtr varControl;
    QSliderDoubleSpinBoxUPtr amountControl;
    QSliderDoubleSpinBoxUPtr saltVsPepperControl;

    an::ui::processes::NoiseGeneratorProcessUiElements m_ui;

    /// The seed the next preview and Apply draw from; drawn anew after every Apply.
    std::uint64_t m_seed = 0u;
};
