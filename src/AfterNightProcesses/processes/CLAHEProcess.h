// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/CLAHEProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct CLAHEParams
{
    int gridSize = 0;
    double contrastLimit = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API CLAHEProcess : public an::ui::TypedProcessControl<CLAHEParams>
{
public:
    CLAHEProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~CLAHEProcess();

private:
    void on_gridSizeChanged(int value);
    void on_contrastLimitChanged(double value);
    void on_amountChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const CLAHEParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const CLAHEParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const CLAHEParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;
    [[nodiscard]] CLAHEParams getParams() const override;
    double getTypedModulationFromParams(const CLAHEParams& params) override;

    QSliderSpinBoxUPtr gridSizeControl;
    QSliderDoubleSpinBoxUPtr contrastLimitControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::CLAHEProcessUiElements m_ui;
};
