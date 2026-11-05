// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/BandingReductionProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct BandingReductionParams
{
    bool protectFromHighlights = false;
    bool verticalBanding = false;
    double amount = 0.0;
    double sigma = 0.0;
};

class AFTERNIGHTPROCESSES_API BandingReductionProcess : public an::ui::TypedProcessControl<BandingReductionParams>
{
public:
    BandingReductionProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~BandingReductionProcess();

    void refreshTheme() override;

private:
    void on_kernelSize_currentTextChanged(const QString& text);

    void on_amountChanged(double value);
    void on_sigmaChanged(double value);

    void on_protectFromHighlights(Qt::CheckState state);
    void on_verticalBanding(Qt::CheckState state);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const BandingReductionParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const BandingReductionParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const BandingReductionParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] BandingReductionParams getParams() const override;

    QSliderDoubleSpinBoxUPtr amountControl;
    QSliderDoubleSpinBoxUPtr sigmaControl;

    an::ui::processes::BandingReductionProcessUiElements m_ui;
};
