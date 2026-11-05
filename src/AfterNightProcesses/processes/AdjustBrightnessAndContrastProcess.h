// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/AdjustBrightnessAndContrastProcessUiBuilder.h"

class ImageViewControl;

struct AdjustBrightnessAndContrastParams
{
    int brightness = 0;
    int contrast = 0;
};

/// Brightness and contrast over the channel or colour component the dialog's selector names: the brightness added as a
/// fraction of 255 and the result held to the unit interval, then the contrast about the middle of it and the result
/// held again - one fused pointwise program, which the preview runs as the Apply does.
class AFTERNIGHTPROCESSES_API AdjustBrightnessAndContrastProcess
    : public an::ui::TypedProcessControl<AdjustBrightnessAndContrastParams>
{
public:
    AdjustBrightnessAndContrastProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~AdjustBrightnessAndContrastProcess();

protected:
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run,
                                            const AdjustBrightnessAndContrastParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const AdjustBrightnessAndContrastParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const AdjustBrightnessAndContrastParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const AdjustBrightnessAndContrastParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const AdjustBrightnessAndContrastParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] bool mapsEachChannelAlone() const override;

private:
    void on_brightnessChanged(int value);
    void on_contrastChanged(int value);
    void refreshTheme() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    [[nodiscard]] AdjustBrightnessAndContrastParams getParams() const override;

    QSliderSpinBoxUPtr brightnessControl;
    QSliderSpinBoxUPtr contrastControl;

    an::ui::processes::AdjustBrightnessAndContrastProcessUiElements m_ui;
};
