// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/TightenEdgesProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct TightenEdgesParams
{
    double value = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API TightenEdgesProcess : public an::ui::TypedProcessControl<TightenEdgesParams>
{
public:
    TightenEdgesProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~TightenEdgesProcess();

private:
    void on_valueChanged(double value);
    void on_amountChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const TightenEdgesParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const TightenEdgesParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const TightenEdgesParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const TightenEdgesParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;
    [[nodiscard]] TightenEdgesParams getParams() const override;
    double getTypedModulationFromParams(const TightenEdgesParams& params) override;

    QSliderDoubleSpinBoxUPtr valueControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::TightenEdgesProcessUiElements m_ui;
};
