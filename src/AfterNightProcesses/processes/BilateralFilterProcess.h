// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/BilateralFilterProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct BilateralFilterParams
{
    bool linkSigmaControls = false;

    double diameter = 0.0;
    double sigmaSpace = 0.0;
    double sigmaColor = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API BilateralFilterProcess : public an::ui::TypedProcessControl<BilateralFilterParams>
{
public:
    BilateralFilterProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~BilateralFilterProcess();

    void refreshTheme() override;

private:
    void on_diameterChanged(double value);
    void on_sigmaSpaceChanged(double value);
    void on_sigmaColorChanged(double value);
    void on_amountChanged(double value);

    void on_linkSigmaControlsClicked(Qt::CheckState state);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const BilateralFilterParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const BilateralFilterParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const BilateralFilterParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const BilateralFilterParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] BilateralFilterParams getParams() const override;
    double getTypedModulationFromParams(const BilateralFilterParams& params) override;

    QSliderDoubleSpinBoxUPtr diameterControl;
    QSliderDoubleSpinBoxUPtr sigmaSpaceControl;
    QSliderDoubleSpinBoxUPtr sigmaColorControl;
    QSliderDoubleSpinBoxUPtr amountControl;

    bool m_IgnoreLastSigmaEvent = false;

    an::ui::processes::BilateralFilterProcessUiElements m_ui;
};
