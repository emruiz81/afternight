// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AdjustHSLProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

AdjustHSLProcess::AdjustHSLProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<AdjustHSLParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildAdjustHSLProcessUi(this);

    setProcessDescription(QStringLiteral("Adjust hue, saturation, and lightness in HSL color space."));

    updateWindowTitle();

    INIT_SPINBOX_CTRL(hueControl, m_ui.hueValue, m_ui.hueSlider, &AdjustHSLProcess::on_hueChanged);
    INIT_SPINBOX_CTRL(saturationControl, m_ui.saturationValue, m_ui.saturationSlider,
                      &AdjustHSLProcess::on_saturationChanged);
    INIT_SPINBOX_CTRL(lightnessControl, m_ui.lightnessValue, m_ui.lightnessSlider,
                      &AdjustHSLProcess::on_lightnessChanged);

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, false);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setupPreviewControl(false);

    refreshTheme();
}

void AdjustHSLProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string AdjustHSLProcess::getProcessNameStatic()
{
    return "Adjust Hue/Saturation/Lightness";
}

bool AdjustHSLProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void AdjustHSLProcess::onImageLoaded(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control && !isViewControlSupported(ctrl))
    {
        close();
    }
    else
    {
        ImageRTPreviewControl::onImageLoaded(ctrl);
    }
}

void AdjustHSLProcess::onImageUpdated(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control && !isViewControlSupported(ctrl))
    {
        close();
    }
    else
    {
        ImageRTPreviewControl::onImageUpdated(ctrl);
    }
}

void AdjustHSLProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
    }
    else
    {
        ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);

        settingsUpdated();
    }
}

AdjustHSLProcess::~AdjustHSLProcess() {}

void AdjustHSLProcess::on_hueChanged(int value)
{
    settingsUpdated();
}

void AdjustHSLProcess::on_saturationChanged(int value)
{
    settingsUpdated();
}

void AdjustHSLProcess::on_lightnessChanged(int value)
{
    settingsUpdated();
}

bool AdjustHSLProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void AdjustHSLProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::HslAdjustment AdjustHSLProcess::hslAdjustment(const AdjustHSLParams& params) noexcept
{
    double saturation = 1.0;
    if (params.saturation > 0.0)
    {
        saturation += params.saturation / 25.0;
    }
    else if (params.saturation < 0.0)
    {
        saturation += params.saturation / 100.0;
    }
    return an::HslAdjustment{.hueOffsetTurns = params.hue / 360.0,
                             .saturationScale = saturation,
                             .lightnessOffset = params.lightness / 100.0};
}

bool AdjustHSLProcess::isTypedIdentity(const AdjustHSLParams& params) const
{
    // Every setting at zero leaves every sample as it is.
    return params.hue == 0.0 && params.saturation == 0.0 && params.lightness == 0.0;
}

an::ui::PreviewScaling AdjustHSLProcess::previewScaling() const
{
    // Every pixel is mapped by the same shift of its hue, saturation and lightness wherever it lies: a frame over the
    // target reduced shows what the whole result shows at that reduction.
    return an::ui::PreviewScaling::Decimated;
}

an::ui::ProcessBodyCoverage AdjustHSLProcess::typedProcessBodyCoverage(const AdjustHSLParams& params) const
{
    // The adjustment and the narrowing write every sample, but with every setting at zero, where the body leaves the
    // destination as it started.
    return isTypedIdentity(params) ? an::ui::ProcessBodyCoverage::WritesPart
                                   : an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status AdjustHSLProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                     an::ui::ProcessDestination& destination,
                                                     const an::ui::ProcessRun& run, const AdjustHSLParams& params,
                                                     an::ui::ExecutionScope& scope)
{
    // Every setting at zero is the source, which the destination already holds.
    if (isTypedIdentity(params))
    {
        return an::Status::success();
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (described->channelSchema() != an::ChannelSchema::Rgb)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "the HSL adjustment needs an image of red, green and blue");
    }
    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    // The HSL of the samples as they are stored: one encoding on both sides, so nothing is converted on the way.
    const an::ColorEncoding encoding =
        an::ui::processChannelScopeRequest(an::ChannelScope::Whole, *described).sourceEncoding;
    const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope.execution());
    if (!skipping.hasValue())
    {
        return skipping.status();
    }
    const an::Result<an::ColorAdjustmentResult> adjusted =
        an::adjustImageHsl(scope.execution(), samples->sampleType(), samples->samples(), samples->layout(),
                           samples->samples(), samples->layout(), encoding, encoding, hslAdjustment(params), *skipping);
    if (!adjusted.hasValue())
    {
        return adjusted.status();
    }
    if (an::Status committed = samples->commit(scope.execution()); committed.failed())
    {
        return committed;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    return process_utils::recordNonlinearResult(
        destination,
        process_utils::NonlinearResultRecord{.stateOperation = "hsl-adjustment",
                                             .engineOperation = "an::adjustImageHsl",
                                             .detail = fmt::format("hue {}, saturation {} and lightness {}", params.hue,
                                                                   params.saturation, params.lightness)});
}

an::ui::ProcessStep AdjustHSLProcess::describeTypedProcessStep(const AdjustHSLParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description = fmt::format("Adjust hue to {} saturation to {} and lightness to {}",
                                                          params.hue, params.saturation, params.lightness)};
}

bool AdjustHSLProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return process_utils::holdsColourImage(ctrl);
}

void AdjustHSLProcess::settingsUpdatedImpl()
{
    if (hueControl->getValue() == 0 && saturationControl->getValue() == 0 && lightnessControl->getValue() == 0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void AdjustHSLProcess::enableProcessControlsImpl(bool enabled)
{
    if (hueControl->getValue() == 0 && saturationControl->getValue() == 0 && lightnessControl->getValue() == 0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    hueControl->setEnabled(enabled);
    saturationControl->setEnabled(enabled);
    lightnessControl->setEnabled(enabled);
}

void AdjustHSLProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    hueControl->setValue(0);
    saturationControl->setValue(0);
    lightnessControl->setValue(0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

AdjustHSLParams AdjustHSLProcess::getParams() const
{
    AdjustHSLParams params;
    params.hue = hueControl->getValue();
    params.saturation = saturationControl->getValue();
    params.lightness = lightnessControl->getValue();

    return params;
}
