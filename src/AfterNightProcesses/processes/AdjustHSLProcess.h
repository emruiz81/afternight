// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/AdjustHSLProcessUiBuilder.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>

class ImageViewControl;

struct AdjustHSLParams
{
    double hue = 0.0;
    double saturation = 0.0;
    double lightness = 0.0;
};

/// Hue, saturation and lightness of a colour image, adjusted in the HSL of its stored samples: the hue turned by a
/// fraction of a turn, the saturation multiplied - by one plus a twenty-fifth of a positive setting or a hundredth of a
/// negative one - and the lightness moved by a hundredth of its setting, each held to the unit interval and the result
/// too. The preview runs the adjustment the Apply does.
class AFTERNIGHTPROCESSES_API AdjustHSLProcess : public an::ui::TypedProcessControl<AdjustHSLParams>
{
public:
    AdjustHSLProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~AdjustHSLProcess();

    /// The engine's adjustment for the dialog's settings.
    ///
    /// @param params the settings.
    /// @return the hue offset in turns, the saturation scale and the lightness offset.
    [[nodiscard]] static an::HslAdjustment hslAdjustment(const AdjustHSLParams& params) noexcept;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const AdjustHSLParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage typedProcessBodyCoverage(const AdjustHSLParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const AdjustHSLParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const AdjustHSLParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;

private:
    void on_hueChanged(int value);
    void on_saturationChanged(int value);
    void on_lightnessChanged(int value);
    void refreshTheme() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    [[nodiscard]] AdjustHSLParams getParams() const override;

    QSliderSpinBoxUPtr hueControl;
    QSliderSpinBoxUPtr saturationControl;
    QSliderSpinBoxUPtr lightnessControl;

    an::ui::processes::AdjustHSLProcessUiElements m_ui;
};
