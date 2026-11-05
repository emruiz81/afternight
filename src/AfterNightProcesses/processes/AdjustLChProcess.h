// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/AdjustLChProcessUiBuilder.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>

class ImageViewControl;

struct AdjustLChParams
{
    double lightness = 0.0;
    double chroma = 0.0;
    double hue = 0.0;
};

/// Lightness, chroma and hue of a colour image, adjusted in CIE L*C*h of its red, green and blue read as the process
/// framework reads them: the lightness moved by its setting, the chroma multiplied - by one plus a twenty-fifth of a
/// positive setting or a hundredth of a negative one - and the hue turned by its setting in degrees, the lightness held
/// to a hundred, the chroma to two hundred and the result to the unit interval. The preview runs the adjustment the
/// Apply does.
class AFTERNIGHTPROCESSES_API AdjustLChProcess : public an::ui::TypedProcessControl<AdjustLChParams>
{
public:
    AdjustLChProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~AdjustLChProcess();

    /// The engine's adjustment for the dialog's settings.
    ///
    /// @param params the settings.
    /// @return the lightness offset, the chroma scale and the hue offset in degrees.
    [[nodiscard]] static an::LchAdjustment lchAdjustment(const AdjustLChParams& params) noexcept;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const AdjustLChParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage typedProcessBodyCoverage(const AdjustLChParams& params) const override;
    [[nodiscard]] bool isTypedIdentity(const AdjustLChParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const AdjustLChParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;

private:
    void on_lightnessChanged(int value);
    void on_chromaChanged(int value);
    void on_hueChanged(int value);
    void refreshTheme() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    void resetSettings() override;

    [[nodiscard]] AdjustLChParams getParams() const override;

    QSliderSpinBoxUPtr lightnessControl;
    QSliderSpinBoxUPtr chromaControl;
    QSliderSpinBoxUPtr hueControl;

    an::ui::processes::AdjustLChProcessUiElements m_ui;
};
