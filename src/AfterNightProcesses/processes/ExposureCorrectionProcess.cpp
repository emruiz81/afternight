// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ExposureCorrectionProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>

ExposureCorrectionProcess::ExposureCorrectionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<ExposureCorrectionParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildExposureCorrectionProcessUi(this);

    setProcessDescription(
        QStringLiteral("Adjust image exposure by a scale factor for global brightening or darkening."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector,
                              /*supportPerChannel=*/true,
                              /*showWaitingSpinner=*/false);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    updateWindowTitle();

    INIT_D_SPINBOX_CTRL(exposureControl, m_ui.exposureValue, m_ui.exposureSlider,
                        &ExposureCorrectionProcess::on_exposureChanged);

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);

    refreshTheme();
}

void ExposureCorrectionProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string ExposureCorrectionProcess::getProcessNameStatic()
{
    return "Exposure Correction";
}

bool ExposureCorrectionProcess::requiresExclusiveAccessStatic()
{
    return false;
}

ExposureCorrectionProcess::~ExposureCorrectionProcess() {}

void ExposureCorrectionProcess::on_exposureChanged(double value)
{
    settingsUpdated();
}

float ExposureCorrectionProcess::exposureFactor(double exposure) noexcept
{
    float factor = 1.0f;
    if (exposure < 0.0)
    {
        factor += .02f + static_cast<float>(exposure) / 5.0f;
    }
    else if (exposure > 0.0)
    {
        factor += static_cast<float>(exposure) / 5.0f;
    }
    return factor;
}

an::Status ExposureCorrectionProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                   const an::ChannelScopeOutput& output,
                                                                   const an::ui::ProcessRun& run,
                                                                   const ExposureCorrectionParams& params,
                                                                   an::ui::ExecutionScope& scope)
{
    const float factor = exposureFactor(params.exposure);
    return process_utils::runNormalizedChain(
        scope, input, output,
        [factor](const an::PointwisePlanShape& shape) -> an::Result<process_utils::PointwiseChain> {
            an::Result<an::PointwiseProgram> scaled =
                an::buildScaleBiasProgram(shape, factor, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
            if (!scaled.hasValue())
            {
                return scaled.status();
            }
            process_utils::PointwiseChain chain;
            chain.push_back(std::move(*scaled));
            return chain;
        });
}

bool ExposureCorrectionProcess::isTypedIdentity(const ExposureCorrectionParams& params) const
{
    // A setting of zero leaves every sample as it is.
    return params.exposure == 0.0;
}

an::ui::PreviewScaling ExposureCorrectionProcess::previewScaling() const
{
    // Every pixel is mapped by the same factor wherever it lies: a frame over the target reduced shows what the whole
    // result shows at that reduction.
    return an::ui::PreviewScaling::Decimated;
}

bool ExposureCorrectionProcess::mapsEachChannelAlone() const
{
    // Each stored channel's sample is mapped alone, by one curve of its own value.
    return true;
}

an::ui::ProcessBodyCoverage
ExposureCorrectionProcess::typedProcessBodyCoverage(const ExposureCorrectionParams& params) const
{
    // Every sample is the chain's, but at zero, where the body leaves the destination as it started.
    return isTypedIdentity(params) ? an::ui::ProcessBodyCoverage::WritesPart
                                   : an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status ExposureCorrectionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                              an::ui::ProcessDestination& destination,
                                                              const an::ui::ProcessRun& run,
                                                              const ExposureCorrectionParams& params,
                                                              an::ui::ExecutionScope& scope)
{
    // A setting of zero is the source, which the destination already holds.
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
        scope, destination,
        process_utils::NonlinearResultRecord{
            .stateOperation = "exposure-correction",
            .engineOperation = "an::executePointwiseProgram",
            .detail = fmt::format("exposure {}, a factor of {}", params.exposure, exposureFactor(params.exposure))});
}

an::ui::ProcessStep ExposureCorrectionProcess::describeTypedProcessStep(const ExposureCorrectionParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Adjust exposure to {}", params.exposure) +
                                   process_utils::channelStepSuffix(m_image_view_control, selectedChannelScope())};
}

bool ExposureCorrectionProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void ExposureCorrectionProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void ExposureCorrectionProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    exposureControl->setValue(0.0);

    if (process_utils::holdsColourImage(m_image_view_control))
    {
        m_ui.channelSelector->setCurrentText(an::ui::processChannelLabel(an::ChannelScope::Whole));
    }

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void ExposureCorrectionProcess::settingsUpdatedImpl()
{
    if (exposureControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void ExposureCorrectionProcess::enableProcessControlsImpl(bool enabled)
{
    if (exposureControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    exposureControl->setEnabled(enabled);
}

ExposureCorrectionParams ExposureCorrectionProcess::getParams() const
{
    ExposureCorrectionParams params;
    params.exposure = exposureControl->getValue();

    return params;
}
