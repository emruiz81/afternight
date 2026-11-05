// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/GammaCorrectionProcessUiBuilder.h"

class ImageViewControl;

struct GammaCorrectionParams
{
    double gamma = 0.0;
};

/// Gamma correction over the channel or colour component the dialog's selector names: every sample raised to the power
/// one over the gamma. Every run - an Apply, a drag's frame, the frame that settles it - raises its samples by the
/// engine's exact gamma program: as a table over the codes of an unsigned eight- or sixteen-bit plane, and over the
/// fractions of full scale of every other plane, where a sample below zero is answered a not-a-number, as the power
/// answers every negative base. A held slider's frames run over what the preview view paints of the target, and the
/// frame that settles the drag over the whole image.
class AFTERNIGHTPROCESSES_API GammaCorrectionProcess : public an::ui::TypedProcessControl<GammaCorrectionParams>
{
public:
    GammaCorrectionProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~GammaCorrectionProcess();

protected:
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const GammaCorrectionParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const GammaCorrectionParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const GammaCorrectionParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const GammaCorrectionParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const GammaCorrectionParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] bool mapsEachChannelAlone() const override;

private:
    void on_gammaChanged(double value);
    void refreshTheme() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;
    [[nodiscard]] GammaCorrectionParams getParams() const override;

    an::ui::processes::GammaCorrectionProcessUiElements m_ui;
    QSliderDoubleSpinBoxUPtr gammaControl;
};
