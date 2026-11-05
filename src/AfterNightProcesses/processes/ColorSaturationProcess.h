// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/ColorSaturationProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/Result.h>

class ImageViewControl;

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

/// The backgrounds a saturation dialog measured of its target, by revision, shared by the dialog and its runs.
struct SaturationBackgrounds;

struct ColorSaturationParams
{
    double amount = 0.0;
    double bgFactor = 0.0;

    int hueSelector = 0;

    /// The image the dialog targeted when the run was issued: a run over a view of it - the preview area, or what an
    /// interactive frame runs over - takes the background of the whole of it.
    std::shared_ptr<const an::consumer::ImageAsset> target;

    /// The preview area shown when the run was issued; empty for the whole image.
    QRect area;

    /// The dialog's measurements of its target's background.
    std::shared_ptr<SaturationBackgrounds> backgrounds;
};

/// The hues of one range a colour selector entry names, in degrees on a six-sextant wheel; a range whose first hue is
/// above its last crosses zero.
struct SaturationHueRange
{
    double minimumDegrees = 0.0;
    double maximumDegrees = 360.0;
};

/// Selective saturation of a colour image: the HSL saturation of every pixel above a background, whose hue lies in the
/// range the selector names, multiplied by one plus the amount and held to the unit interval. The background is the
/// green channel's median plus its standard deviation times the background factor, and a pixel at or below it is left
/// as it was.
class AFTERNIGHTPROCESSES_API ColorSaturationProcess : public an::ui::TypedProcessControl<ColorSaturationParams>
{
public:
    ColorSaturationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~ColorSaturationProcess();

    /// The hues the selector's entry at an index names.
    ///
    /// @param selector the entry's index; an index the selector does not have names every hue.
    /// @return the range.
    [[nodiscard]] static SaturationHueRange hueRange(int selector) noexcept;

    /// The background at a factor of one of a whole image, which a run over a view of it takes: the green channel's
    /// median plus its standard deviation, in the fractions of full scale the body compares lightness in, over the
    /// samples the body itself would measure - the image's rule with every sample holding no number left out.
    ///
    /// @param engine the run's engine context.
    /// @param execution the run's admitted execution.
    /// @param image the image, read; red, green and blue.
    /// @return the background; or what the measurement refused.
    [[nodiscard]] static an::Result<double> measureBackgroundAtFactorOne(const an::EngineContext& engine,
                                                                         const an::ExecutionContext& execution,
                                                                         const an::consumer::AssetReadLease& image);

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ColorSaturationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const ColorSaturationParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const ColorSaturationParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;

private:
    void on_amountChanged(double value);
    void on_bgFactorChanged(double value);

    void on_hueSelector_currentTextChanged(const QString& str);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void refreshTheme() override;

    bool isViewControlSupported(const ImageViewControl* ctrl) override;
    [[nodiscard]] ColorSaturationParams getParams() const override;

    an::ui::processes::ColorSaturationProcessUiElements m_ui;
    QSliderDoubleSpinBoxUPtr amountControl;
    QSliderDoubleSpinBoxUPtr bgFactorControl;
    std::shared_ptr<SaturationBackgrounds> m_backgrounds;
};
