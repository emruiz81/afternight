// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AdjustBrightnessAndContrastProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>

AdjustBrightnessAndContrastProcess::AdjustBrightnessAndContrastProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<AdjustBrightnessAndContrastParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildAdjustBrightnessAndContrastProcessUi(this);

    setProcessDescription(QStringLiteral("Adjust global brightness and contrast with live histogram feedback."));

    updateWindowTitle();

    INIT_SPINBOX_CTRL(brightnessControl, m_ui.brightnessValue, m_ui.brightnessSlider,
                      &AdjustBrightnessAndContrastProcess::on_brightnessChanged);
    INIT_SPINBOX_CTRL(contrastControl, m_ui.contrastValue, m_ui.contrastSlider,
                      &AdjustBrightnessAndContrastProcess::on_contrastChanged);

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, false);

    if (m_ui.previewLayout)
    {
        QMargins paneMargins;
        if (m_ui.brightnessValue && m_ui.brightnessValue->parentWidget() &&
            m_ui.brightnessValue->parentWidget()->layout())
        {
            paneMargins = m_ui.brightnessValue->parentWidget()->layout()->contentsMargins();
        }
        m_ui.previewLayout->setContentsMargins(paneMargins);
    }

    if (m_ui.horizontalLayoutMain)
    {
        m_ui.horizontalLayoutMain->setContentsMargins(0, 0, 0, 0);
    }

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);
    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);
    setupPreviewControl(false);

    refreshTheme();
}

void AdjustBrightnessAndContrastProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string AdjustBrightnessAndContrastProcess::getProcessNameStatic()
{
    return "Adjust Brightness And Contrast";
}

bool AdjustBrightnessAndContrastProcess::requiresExclusiveAccessStatic()
{
    return false;
}

AdjustBrightnessAndContrastProcess::~AdjustBrightnessAndContrastProcess() {}

void AdjustBrightnessAndContrastProcess::on_brightnessChanged(int value)
{
    settingsUpdated();
}

void AdjustBrightnessAndContrastProcess::on_contrastChanged(int value)
{
    settingsUpdated();
}

bool AdjustBrightnessAndContrastProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void AdjustBrightnessAndContrastProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status AdjustBrightnessAndContrastProcess::executeTypedProcessIteration(
    const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output, const an::ui::ProcessRun& run,
    const AdjustBrightnessAndContrastParams& params, an::ui::ExecutionScope& scope)
{
    // The two coefficients, computed in single precision: the brightness a fraction of 255, and the contrast factor of
    // the familiar 259 formula, which is one at a contrast of zero.
    const float brightness = static_cast<float>(params.brightness) / 255.0f;
    const float contrast = (259.0f * (static_cast<float>(params.contrast) + 255.0f)) /
                           (255.0f * (259.0f - static_cast<float>(params.contrast)));
    return process_utils::runNormalizedChain(
        scope, input, output,
        [brightness, contrast](const an::PointwisePlanShape& shape) -> an::Result<process_utils::PointwiseChain> {
            const an::ClampInterval unit{.low = 0.0, .high = 1.0};
            process_utils::PointwiseChain chain;
            an::Result<an::PointwiseProgram> brighten = an::buildBrightnessProgram(shape, brightness);
            if (!brighten.hasValue())
            {
                return brighten.status();
            }
            chain.push_back(std::move(*brighten));
            an::Result<an::PointwiseProgram> held = an::buildClampProgram(shape, unit);
            if (!held.hasValue())
            {
                return held.status();
            }
            chain.push_back(*held);
            if (contrast != 1.0f)
            {
                an::Result<an::PointwiseProgram> contrasted = an::buildContrastProgram(shape, contrast, 0.5);
                if (!contrasted.hasValue())
                {
                    return contrasted.status();
                }
                chain.push_back(std::move(*contrasted));
                chain.push_back(std::move(*held));
            }
            return chain;
        });
}

bool AdjustBrightnessAndContrastProcess::isTypedIdentity(const AdjustBrightnessAndContrastParams& params) const
{
    // A brightness and a contrast of zero leave every sample as it is.
    return params.brightness == 0 && params.contrast == 0;
}

an::ui::PreviewScaling AdjustBrightnessAndContrastProcess::previewScaling() const
{
    // Every pixel is mapped by the same curve wherever it lies: a frame over the target reduced shows what the whole
    // result shows at that reduction.
    return an::ui::PreviewScaling::Decimated;
}

bool AdjustBrightnessAndContrastProcess::mapsEachChannelAlone() const
{
    // Each stored channel's sample is mapped alone, by one curve of its own value.
    return true;
}

an::ui::ProcessBodyCoverage
AdjustBrightnessAndContrastProcess::typedProcessBodyCoverage(const AdjustBrightnessAndContrastParams& params) const
{
    // Every sample is the chain's, but at zero and zero, where the body leaves the destination as it started.
    return isTypedIdentity(params) ? an::ui::ProcessBodyCoverage::WritesPart
                                   : an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status AdjustBrightnessAndContrastProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                       an::ui::ProcessDestination& destination,
                                                                       const an::ui::ProcessRun& run,
                                                                       const AdjustBrightnessAndContrastParams& params,
                                                                       an::ui::ExecutionScope& scope)
{
    // At zero and zero the result is the source, which the destination already holds.
    if (isTypedIdentity(params))
    {
        return an::Status::success();
    }
    if (an::Status ran = executeTypedProcessUsingChannels(source, destination, run, params, scope); ran.failed())
    {
        return ran;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    return process_utils::recordNonlinearResult(
        destination, process_utils::NonlinearResultRecord{
                         .stateOperation = "brightness-contrast",
                         .engineOperation = "an::executePointwiseProgram",
                         .detail = fmt::format("brightness {} and contrast {}", params.brightness, params.contrast)});
}

an::ui::ProcessStep
AdjustBrightnessAndContrastProcess::describeTypedProcessStep(const AdjustBrightnessAndContrastParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Adjust brightness to {} and contrast to {}", params.brightness, params.contrast) +
                       process_utils::channelStepSuffix(m_image_view_control, selectedChannelScope())};
}

void AdjustBrightnessAndContrastProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    brightnessControl->setValue(0);
    contrastControl->setValue(0);

    if (process_utils::holdsColourImage(m_image_view_control))
    {
        m_ui.channelSelector->setCurrentText(an::ui::processChannelLabel(an::ChannelScope::Whole));
    }

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void AdjustBrightnessAndContrastProcess::settingsUpdatedImpl()
{
    if (brightnessControl->getValue() == 0 && contrastControl->getValue() == 0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void AdjustBrightnessAndContrastProcess::enableProcessControlsImpl(bool enabled)
{
    if (brightnessControl->getValue() == 0 && contrastControl->getValue() == 0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    brightnessControl->setEnabled(enabled);
    contrastControl->setEnabled(enabled);
}

AdjustBrightnessAndContrastParams AdjustBrightnessAndContrastProcess::getParams() const
{
    AdjustBrightnessAndContrastParams params;
    params.brightness = brightnessControl->getValue();
    params.contrast = contrastControl->getValue();

    return params;
}
