// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AdjustLChProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

AdjustLChProcess::AdjustLChProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<AdjustLChParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildAdjustLChProcessUi(this);

    setProcessDescription(QStringLiteral("Adjust perceptual lightness, chroma, and hue in CIE LCh space."));

    updateWindowTitle();

    INIT_SPINBOX_CTRL(lightnessControl, m_ui.lightnessValue, m_ui.lightnessSlider,
                      &AdjustLChProcess::on_lightnessChanged);
    INIT_SPINBOX_CTRL(chromaControl, m_ui.chromaValue, m_ui.chromaSlider, &AdjustLChProcess::on_chromaChanged);
    INIT_SPINBOX_CTRL(hueControl, m_ui.hueValue, m_ui.hueSlider, &AdjustLChProcess::on_hueChanged);

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, true);
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setupPreviewControl(false);

    refreshTheme();
}

void AdjustLChProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string AdjustLChProcess::getProcessNameStatic()
{
    return "Adjust Lightness/Chroma/Hue";
}

bool AdjustLChProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void AdjustLChProcess::onImageLoaded(ImageViewControl* ctrl)
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

void AdjustLChProcess::onImageUpdated(ImageViewControl* ctrl)
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

void AdjustLChProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
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

AdjustLChProcess::~AdjustLChProcess() {}

void AdjustLChProcess::on_lightnessChanged(int value)
{
    settingsUpdated();
}

void AdjustLChProcess::on_chromaChanged(int value)
{
    settingsUpdated();
}

void AdjustLChProcess::on_hueChanged(int value)
{
    settingsUpdated();
}

bool AdjustLChProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void AdjustLChProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::LchAdjustment AdjustLChProcess::lchAdjustment(const AdjustLChParams& params) noexcept
{
    double chroma = 1.0;
    if (params.chroma > 0.0)
    {
        chroma += params.chroma / 25.0;
    }
    else if (params.chroma < 0.0)
    {
        chroma += params.chroma / 100.0;
    }
    return an::LchAdjustment{
        .lightnessOffset = params.lightness, .chromaScale = chroma, .hueOffsetDegrees = params.hue};
}

bool AdjustLChProcess::isTypedIdentity(const AdjustLChParams& params) const
{
    // Every setting at zero leaves every sample as it is.
    return params.hue == 0.0 && params.chroma == 0.0 && params.lightness == 0.0;
}

an::ui::PreviewScaling AdjustLChProcess::previewScaling() const
{
    // Every pixel is mapped by the same shift of its lightness, chroma and hue wherever it lies: a frame over the
    // target reduced shows what the whole result shows at that reduction.
    return an::ui::PreviewScaling::Decimated;
}

an::ui::ProcessBodyCoverage AdjustLChProcess::typedProcessBodyCoverage(const AdjustLChParams& params) const
{
    // The adjustment and the narrowing write every sample, but with every setting at zero, where the body leaves the
    // destination as it started.
    return isTypedIdentity(params) ? an::ui::ProcessBodyCoverage::WritesPart
                                   : an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status AdjustLChProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                     an::ui::ProcessDestination& destination,
                                                     const an::ui::ProcessRun& run, const AdjustLChParams& params,
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
                                   "the LCh adjustment needs an image of red, green and blue");
    }
    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    // The red, green and blue are read as the framework reads them for a CIE component: linear for an image that says
    // it is linear, sRGB-encoded otherwise; the result is written back in the same encoding.
    const an::ColorEncoding encoding =
        an::ui::processChannelScopeRequest(an::ChannelScope::Whole, *described).sourceEncoding;
    const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope.execution());
    if (!skipping.hasValue())
    {
        return skipping.status();
    }
    const an::Result<an::ColorAdjustmentResult> adjusted =
        an::adjustImageLch(scope.execution(), samples->sampleType(), samples->samples(), samples->layout(),
                           samples->samples(), samples->layout(), encoding, encoding, lchAdjustment(params), *skipping);
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
        process_utils::NonlinearResultRecord{
            .stateOperation = "lch-adjustment",
            .engineOperation = "an::adjustImageLch",
            .detail = fmt::format("lightness {}, chroma {} and hue {}", params.lightness, params.chroma, params.hue)});
}

an::ui::ProcessStep AdjustLChProcess::describeTypedProcessStep(const AdjustLChParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description = fmt::format("Adjust lightness to {} chroma to {} and hue to {}",
                                                          params.lightness, params.chroma, params.hue)};
}

void AdjustLChProcess::settingsUpdatedImpl()
{
    if (hueControl->getValue() == 0 && chromaControl->getValue() == 0 && lightnessControl->getValue() == 0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void AdjustLChProcess::enableProcessControlsImpl(bool enabled)
{
    if (hueControl->getValue() == 0 && chromaControl->getValue() == 0 && lightnessControl->getValue() == 0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    lightnessControl->setEnabled(enabled);
    chromaControl->setEnabled(enabled);
    hueControl->setEnabled(enabled);
}

bool AdjustLChProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return process_utils::holdsColourImage(ctrl);
}

void AdjustLChProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    lightnessControl->setValue(0);
    chromaControl->setValue(0);
    hueControl->setValue(0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

AdjustLChParams AdjustLChProcess::getParams() const
{
    AdjustLChParams params;
    params.lightness = lightnessControl->getValue();
    params.chroma = chromaControl->getValue();
    params.hue = hueControl->getValue();

    return params;
}
