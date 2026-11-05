// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/CosmeticCorrectionProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct CosmeticCorrectionParams
{
    int kernelSize = 0;

    bool hotPixelsGroupBox = false;
    bool coldPixelsGroupBox = false;

    double hotPixelsThreshold = 0.0;
    double coldPixelsThreshold = 0.0;
    double amount = 1.0;
};

class AFTERNIGHTPROCESSES_API CosmeticCorrectionProcess : public an::ui::TypedProcessControl<CosmeticCorrectionParams>
{
public:
    CosmeticCorrectionProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~CosmeticCorrectionProcess();

    void refreshTheme() override;

private:
    void on_kernelSize_currentTextChanged(const QString& text);

    void on_hotPixelsThresholdChanged(double value);
    void on_coldPixelsThresholdChanged(double value);
    void on_amountChanged(double value);

    void on_hotPixelsGroupBoxClicked(bool on);
    void on_coldPixelsGroupBoxClicked(bool on);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const CosmeticCorrectionParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const CosmeticCorrectionParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const CosmeticCorrectionParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const CosmeticCorrectionParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] CosmeticCorrectionParams getParams() const override;
    double getTypedModulationFromParams(const CosmeticCorrectionParams& params) override;

    an::ui::processes::CosmeticCorrectionProcessUiElements m_ui;

    QSliderDoubleSpinBoxUPtr hotPixelsThresholdControl;
    QSliderDoubleSpinBoxUPtr coldPixelsThresholdControl;
    QSliderDoubleSpinBoxUPtr amountControl;
};
