// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/UnsharpMaskProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct UnsharpMaskParams
{
    double radius = 0.0;
    double amount = 0.0;
};

class AFTERNIGHTPROCESSES_API UnsharpMaskProcess : public an::ui::TypedProcessControl<UnsharpMaskParams>
{
public:
    UnsharpMaskProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~UnsharpMaskProcess();

private:
    void on_radiusChanged(double value);
    void on_amountChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const UnsharpMaskParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const UnsharpMaskParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const UnsharpMaskParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const UnsharpMaskParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] UnsharpMaskParams getParams() const override;

    QSliderDoubleSpinBoxUPtr radiusControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::UnsharpMaskProcessUiElements m_ui;
};
