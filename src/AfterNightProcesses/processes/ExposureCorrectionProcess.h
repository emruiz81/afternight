// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/ExposureCorrectionProcessUiBuilder.h"

class ImageViewControl;

struct ExposureCorrectionParams
{
    double exposure = 0.0;
};

/// Exposure correction over the channel or colour component the dialog's selector names: every sample multiplied by a
/// factor - one plus a fifth of a positive setting, or one plus 0.02 plus a fifth of a negative one - and held to the
/// unit interval, one fused pointwise program.
class AFTERNIGHTPROCESSES_API ExposureCorrectionProcess : public an::ui::TypedProcessControl<ExposureCorrectionParams>
{
public:
    ExposureCorrectionProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~ExposureCorrectionProcess();

    /// The factor for a setting, computed in single precision.
    ///
    /// @param exposure the dialog's setting.
    /// @return one for zero; one plus a fifth of a positive setting; one plus 0.02 plus a fifth of a negative one.
    [[nodiscard]] static float exposureFactor(double exposure) noexcept;

protected:
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const ExposureCorrectionParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ExposureCorrectionParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const ExposureCorrectionParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const ExposureCorrectionParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const ExposureCorrectionParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] bool mapsEachChannelAlone() const override;

private:
    void on_exposureChanged(double value);
    void refreshTheme() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;
    [[nodiscard]] ExposureCorrectionParams getParams() const override;

    an::ui::processes::ExposureCorrectionProcessUiElements m_ui;
    QSliderDoubleSpinBoxUPtr exposureControl;
};
