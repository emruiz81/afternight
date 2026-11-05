// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/LocalContrastEnhancementProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct LocalContrastEnhancementParams
{
    double spatialRadius = 0.0;
    double weight = 0.0;
    double darknessLevel = 0.0;
    double lightnessLevel = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API LocalContrastEnhancementProcess
    : public an::ui::TypedProcessControl<LocalContrastEnhancementParams>
{
public:
    LocalContrastEnhancementProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~LocalContrastEnhancementProcess();

private:
    void on_spatialRadiusChanged(double value);
    void on_weightChanged(double value);
    void on_darknessLevelChanged(double value);
    void on_lightnessLevelChanged(double value);
    void on_amountChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const LocalContrastEnhancementParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const LocalContrastEnhancementParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const LocalContrastEnhancementParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] LocalContrastEnhancementParams getParams() const override;
    double getTypedModulationFromParams(const LocalContrastEnhancementParams& params) override;

    QSliderDoubleSpinBoxUPtr spatialRadiusControl;
    QSliderDoubleSpinBoxUPtr weightControl;
    QSliderDoubleSpinBoxUPtr darknessLevelControl;
    QSliderDoubleSpinBoxUPtr lightnessLevelControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::LocalContrastEnhancementProcessUiElements m_ui;
};
