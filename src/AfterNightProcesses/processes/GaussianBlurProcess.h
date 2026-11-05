// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"
#include "processes/GaussianBlurProcessUiBuilder.h"

class ImageViewControl;

struct GaussianBlurParams
{
    double sigma = 0.0;
};

/// Blurs the image, or the channel the selector names, with a Gaussian of the dialog's sigma.
///
/// Every run - an Apply, a new image, a snapshot, a drag's frame and the frame that settles it - runs the exact
/// Gaussian; the engine's three-box approximation is not taken. A held slider's frame over the preview view's reduction
/// of the target blurs it by the sigma over the reduction's factor, since a Gaussian commutes with the block averaging,
/// and a frame at full scale over the part the view shows reads that part grown by the blur's reach. A sigma the
/// engine builds no kernel for leaves the image as it is.
class AFTERNIGHTPROCESSES_API GaussianBlurProcess : public an::ui::TypedProcessControl<GaussianBlurParams>
{
public:
    GaussianBlurProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~GaussianBlurProcess();

    void refreshTheme() override;

private:
    void on_sigmaChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const GaussianBlurParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const GaussianBlurParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const GaussianBlurParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const GaussianBlurParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const GaussianBlurParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;
    [[nodiscard]] GaussianBlurParams getParams() const override;

    an::ui::processes::GaussianBlurProcessUiElements m_ui;
    QSliderDoubleSpinBoxUPtr sigmaControl;
};
