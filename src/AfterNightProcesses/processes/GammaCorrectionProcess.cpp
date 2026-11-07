// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GammaCorrectionProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <array>

GammaCorrectionProcess::GammaCorrectionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<GammaCorrectionParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildGammaCorrectionProcessUi(this);

    setProcessDescription(
        QStringLiteral("Adjust midtones using gamma correction while preserving highlights and shadows."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector,
                              /*supportPerChannel=*/true,
                              /*showWaitingSpinner=*/false);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    updateWindowTitle();

    INIT_D_SPINBOX_CTRL(gammaControl, m_ui.gammaValue, m_ui.gammaSlider, &GammaCorrectionProcess::on_gammaChanged);

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setupPreviewControl(false);

    refreshTheme();
}

void GammaCorrectionProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string GammaCorrectionProcess::getProcessNameStatic()
{
    return "Gamma Correction";
}

bool GammaCorrectionProcess::requiresExclusiveAccessStatic()
{
    return false;
}

GammaCorrectionProcess::~GammaCorrectionProcess() {}

void GammaCorrectionProcess::on_gammaChanged(double value)
{
    settingsUpdated();
}

an::Status GammaCorrectionProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                const an::ChannelScopeOutput& output,
                                                                const an::ui::ProcessRun& run,
                                                                const GammaCorrectionParams& params,
                                                                an::ui::ExecutionScope& scope)
{
    const an::SampleType stored = output.descriptor->sampleType();
    const std::uint32_t channels = output.layout->extent().channelCount();
    const an::ExecutionContext& execution = scope.execution();
    // The plane is read where the scope hands it over and the result written to the output, which need not hold it.
    const an::PointwiseImageView read = process_utils::pointwiseReadOf(input, output);

    // An unsigned eight- or sixteen-bit plane takes the program as an exact table over its codes, where it is, for the
    // preview as for an Apply.
    if (stored == an::SampleType::UInt8 || stored == an::SampleType::UInt16)
    {
        const an::Result<an::PowerProgramPlan> plan = an::buildGammaProgram(
            process_utils::normalizedToneShape(stored, channels), params.gamma, an::GammaDirection::Encode);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const std::array<an::PointwiseImageView, 1u> sources{read};
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            execution, plan->program(), sources,
            an::PointwiseMutableImageView{.samples = output.samples, .layout = output.layout}, input.validity);
        return ran.hasValue() ? an::Status::success() : ran.status();
    }

    an::Result<process_utils::NormalizedSamples> samples =
        process_utils::NormalizedSamples::open(scope.engine(), execution, *output.descriptor, read.samples,
                                               *read.layout, output.samples, *output.layout, input.validity);
    if (!samples.hasValue())
    {
        return samples.status();
    }
    const an::TonePlanShape shape = process_utils::normalizedToneShape(samples->sampleType(), channels);
    const an::PointwiseImageView source{.samples = samples->samples(), .layout = &samples->layout()};
    const an::PointwiseMutableImageView destination{.samples = samples->samples(), .layout = &samples->layout()};
    // Every frame raises the plane by the Apply's power, a drag's as a settled one: over the reduction an interactive
    // frame runs over, the exact power costs what a coarse preview table would, and such a table would quantize the
    // shadows the settled frame then snaps out of.
    const an::Result<an::PowerProgramPlan> plan =
        an::buildGammaProgram(shape, params.gamma, an::GammaDirection::Encode);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    // A floating sample below zero is answered a not-a-number, as the power answers every negative base, in a frame as
    // in an Apply - the answer the recorded expectations hold.
    const std::array<an::PointwiseImageView, 1u> sources{source};
    const an::Result<an::PointwiseExecutionResult> ran =
        an::executePointwiseProgram(execution, plan->program(), sources, destination, samples->validity());
    if (!ran.hasValue())
    {
        return ran.status();
    }
    return samples->commit(execution);
}

bool GammaCorrectionProcess::isTypedIdentity(const GammaCorrectionParams& params) const
{
    // A gamma of one leaves every sample as it is.
    return params.gamma == 1.0;
}

an::ui::PreviewScaling GammaCorrectionProcess::previewScaling() const
{
    // Every pixel is raised to the same power wherever it lies: a frame over the target reduced shows the power of each
    // block's mean, which a drag can follow, and the frame that settles it the power of every pixel, as the Apply does.
    return an::ui::PreviewScaling::Decimated;
}

bool GammaCorrectionProcess::mapsEachChannelAlone() const
{
    // Each stored channel's sample is mapped alone, by one curve of its own value.
    return true;
}

an::ui::ProcessBodyCoverage GammaCorrectionProcess::typedProcessBodyCoverage(const GammaCorrectionParams& params) const
{
    // Every sample is the program's, but at a gamma of one, where the body leaves the destination as it started.
    return isTypedIdentity(params) ? an::ui::ProcessBodyCoverage::WritesPart
                                   : an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status GammaCorrectionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                           an::ui::ProcessDestination& destination,
                                                           const an::ui::ProcessRun& run,
                                                           const GammaCorrectionParams& params,
                                                           an::ui::ExecutionScope& scope)
{
    // A gamma of one is the source, which the destination already holds.
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
        destination, process_utils::NonlinearResultRecord{.stateOperation = "gamma-correction",
                                                          .engineOperation = "an::buildGammaProgram",
                                                          .detail = fmt::format("gamma {}", params.gamma)});
}

an::ui::ProcessStep GammaCorrectionProcess::describeTypedProcessStep(const GammaCorrectionParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Adjust gamma to {}", params.gamma) +
                                   process_utils::channelStepSuffix(m_image_view_control, selectedChannelScope())};
}

bool GammaCorrectionProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void GammaCorrectionProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void GammaCorrectionProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();

    gammaControl->setValue(1.0);

    if (process_utils::holdsColourImage(m_image_view_control))
    {
        m_ui.channelSelector->setCurrentText(an::ui::processChannelLabel(an::ChannelScope::Whole));
    }

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void GammaCorrectionProcess::settingsUpdatedImpl()
{
    if (gammaControl->getValue() == 1.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void GammaCorrectionProcess::enableProcessControlsImpl(bool enabled)
{
    if (gammaControl->getValue() == 1.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    gammaControl->setEnabled(enabled);
}

GammaCorrectionParams GammaCorrectionProcess::getParams() const
{
    GammaCorrectionParams params;
    params.gamma = gammaControl->getValue();

    return params;
}
