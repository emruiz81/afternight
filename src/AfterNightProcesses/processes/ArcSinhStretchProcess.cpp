// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ArcSinhStretchProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

#include <array>

ArcSinhStretchProcess::ArcSinhStretchProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<ArcSinhStretchParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildArcSinhStretchProcessUi(this);

    setProcessDescription(
        QStringLiteral("Apply ArcSinh stretch to boost faint signal while preserving stars and highlights."));

    QObject::connect(m_ui.humanWeightedLuminance, &QCheckBox::checkStateChanged, this,
                     &ArcSinhStretchProcess::on_HumanWeightedLuminanceClicked);

    INIT_D_SPINBOX_CTRL(stretchFactorControl, m_ui.stretchFactor, m_ui.stretchFactorSlider,
                        &ArcSinhStretchProcess::on_stretchFactorChanged);
    INIT_D_SPINBOX_CTRL(blackPointControl, m_ui.blackPoint, m_ui.blackPointSlider,
                        &ArcSinhStretchProcess::on_blackPointChanged);

    updateWindowTitle();

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

void ArcSinhStretchProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string ArcSinhStretchProcess::getProcessNameStatic()
{
    return "ArcSinhStretch";
}

bool ArcSinhStretchProcess::requiresExclusiveAccessStatic()
{
    return false;
}

ArcSinhStretchProcess::~ArcSinhStretchProcess() {}

void ArcSinhStretchProcess::on_stretchFactorChanged(double value)
{
    settingsUpdated();
}

void ArcSinhStretchProcess::on_blackPointChanged(double value)
{
    settingsUpdated();
}

void ArcSinhStretchProcess::on_HumanWeightedLuminanceClicked(Qt::CheckState state)
{
    settingsUpdated();
}

an::ui::ProcessBodyCoverage ArcSinhStretchProcess::typedProcessBodyCoverage(const ArcSinhStretchParams& params) const
{
    // The stretch writes every sample of the samples it opens, and the narrowing every sample of the destination.
    return an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status ArcSinhStretchProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                          an::ui::ProcessDestination& destination,
                                                          const an::ui::ProcessRun& run,
                                                          const ArcSinhStretchParams& params,
                                                          an::ui::ExecutionScope& scope)
{
    const an::ExecutionContext& execution = scope.execution();
    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    const std::uint32_t channels = samples->layout().extent().channelCount();
    // The stretch refuses a sample that holds no number, so a pixel holding one is left as it was, under every rule.
    const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(execution);
    if (!skipping.hasValue())
    {
        return skipping.status();
    }
    const an::KernelValidityView validity = *skipping;

    // The black point: every sample moved off it and rescaled so that one stays one, nothing below zero.
    if (params.blackPoint != 0.0)
    {
        const double width = 1.0 - params.blackPoint;
        const an::Result<an::PointwiseProgram> moved =
            an::buildScaleBiasProgram(process_utils::normalizedPlanShape(samples->sampleType(), channels), 1.0 / width,
                                      -params.blackPoint / width, an::ClampInterval{.low = 0.0, .high = 1.0});
        if (!moved.hasValue())
        {
            return moved.status();
        }
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = samples->samples(), .layout = &samples->layout()}};
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            execution, *moved, sources,
            an::PointwiseMutableImageView{.samples = samples->samples(), .layout = &samples->layout()}, validity);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }

    // The curve over the unit interval: the inverse hyperbolic sine of the stretch times the value, over that of the
    // stretch, which no protection point bends; a colour image's channels scaled together by their luminance's ratio.
    // The even luminance is the mean of the three channels.
    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::Asinh;
    request.stretch.amount = params.stretchFactor;
    request.stretch.lowProtection = 0.0;
    request.stretch.symmetryPoint = 0.0;
    request.stretch.highProtection = 1.0;
    if (channels == 3u)
    {
        request.colorMode = an::StretchColorMode::Color;
        request.overflow = an::StretchOverflowPolicy::Clip;
        request.luminanceCoefficients = params.humanWeightedLuminance ? std::array<double, 3u>{0.2126, 0.7152, 0.0722}
                                                                      : std::array<double, 3u>{1.0, 1.0, 1.0};
    }
    const an::Result<an::StretchImageResult> stretched =
        an::stretchImage(execution, samples->sampleType(), samples->samples(), samples->layout(), samples->samples(),
                         samples->layout(), request, validity);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    if (an::Status committed = samples->commit(execution); committed.failed())
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
            .stateOperation = "arcsinh-stretch",
            .engineOperation = "an::stretchImage",
            .detail = fmt::format("stretch {}, black point {}, {} luminance", params.stretchFactor, params.blackPoint,
                                  params.humanWeightedLuminance ? "human-weighted" : "even")});
}

an::ui::ProcessStep ArcSinhStretchProcess::describeTypedProcessStep(const ArcSinhStretchParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Stretch {}, Black Point {}, Human Weight {}", params.stretchFactor,
                                               params.blackPoint, params.humanWeightedLuminance)};
}

bool ArcSinhStretchProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void ArcSinhStretchProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void ArcSinhStretchProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    stretchFactorControl->setValue(0.0);
    blackPointControl->setValue(0.0);
    m_ui.humanWeightedLuminance->setChecked(true);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void ArcSinhStretchProcess::settingsUpdatedImpl()
{
    if (stretchFactorControl->getValue() == 0.0 && blackPointControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void ArcSinhStretchProcess::enableProcessControlsImpl(bool enabled)
{
    if (stretchFactorControl->getValue() == 0.0 && blackPointControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    stretchFactorControl->setEnabled(enabled);
    blackPointControl->setEnabled(enabled);
    m_ui.humanWeightedLuminance->setEnabled(enabled);
}

void ArcSinhStretchProcess::previewControlStarted() {}

void ArcSinhStretchProcess::previewControlClosed() {}

ArcSinhStretchParams ArcSinhStretchProcess::getParams() const
{
    ArcSinhStretchParams params;
    params.stretchFactor = stretchFactorControl->getValue();
    params.blackPoint = blackPointControl->getValue();
    params.humanWeightedLuminance = m_ui.humanWeightedLuminance->isChecked();

    return params;
}
