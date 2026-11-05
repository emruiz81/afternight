// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/LocalHistogramEqualizationProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct LocalHistogramEqualizationParams
{
    double strength = 0.0;
    int radius = 0;
    double sigma = 0.0;
    double regularization = 0.0;
    double amount = 1.0;
    bool reduceHalos = false;
};

class AFTERNIGHTPROCESSES_API LocalHistogramEqualizationProcess
    : public an::ui::TypedProcessControl<LocalHistogramEqualizationParams>
{
public:
    LocalHistogramEqualizationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~LocalHistogramEqualizationProcess();

private:
    void on_strengthChanged(double value);
    void on_radiusChanged(int value);
    void on_sigmaChanged(double value);
    void on_regularizationChanged(double value);
    void on_amountChanged(double value);

    void on_reduceHalosClicked(int state);

    // void on_modeComboBox_currentTextChanged(const QString& str);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run,
                                            const LocalHistogramEqualizationParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const LocalHistogramEqualizationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const LocalHistogramEqualizationParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    double getTypedModulationFromParams(const LocalHistogramEqualizationParams& params) override;
    [[nodiscard]] LocalHistogramEqualizationParams getParams() const override;

    QSliderDoubleSpinBoxUPtr strengthControl;
    QSliderSpinBoxUPtr radiusControl;
    QSliderDoubleSpinBoxUPtr sigmaControl;
    QSliderDoubleSpinBoxUPtr regularizationControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::LocalHistogramEqualizationProcessUiElements m_ui;
};
