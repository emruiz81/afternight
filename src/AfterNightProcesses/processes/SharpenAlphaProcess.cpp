// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SharpenAlphaProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/GmicProcessSupport.h"

SharpenAlphaProcess::SharpenAlphaProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<SharpenAlphaParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildSharpenAlphaProcessUi(this);
    setProcessDescription(
        QStringLiteral("Apply multiscale alpha sharpening with amplitude, scales, and anisotropy tuning."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    INIT_D_SPINBOX_CTRL(amplitudeControl, m_ui.amplitudeValue, m_ui.amplitudeSlider,
                        &SharpenAlphaProcess::on_amplitudeChanged);
    INIT_SPINBOX_CTRL(numOfScalesControl, m_ui.numOfScalesValue, m_ui.numOfScalesSlider,
                      &SharpenAlphaProcess::on_numOfScalesChanged);
    INIT_D_SPINBOX_CTRL(anisotropyControl, m_ui.anisotropyValue, m_ui.anisotropySlider,
                        &SharpenAlphaProcess::on_anisotropyChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &SharpenAlphaProcess::on_amountChanged);

    updateWindowTitle();

    settingsUpdated();

    setupPreviewControl(true, QSize(600, 600), "Preview: Multiscale Sharpen Alpha");

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

std::string SharpenAlphaProcess::getProcessNameStatic()
{
    return "Multiscale Sharpen Alpha";
}

bool SharpenAlphaProcess::requiresExclusiveAccessStatic()
{
    return false;
}

SharpenAlphaProcess::~SharpenAlphaProcess() {}

void SharpenAlphaProcess::on_amplitudeChanged(double value)
{
    settingsUpdated();
}

void SharpenAlphaProcess::on_numOfScalesChanged(int value)
{
    settingsUpdated();
}

void SharpenAlphaProcess::on_anisotropyChanged(double value)
{
    settingsUpdated();
}

void SharpenAlphaProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void SharpenAlphaProcess::on_reduceHalosClicked(int state)
{
    settingsUpdated();
}

an::Status SharpenAlphaProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                             const an::ChannelScopeOutput& output,
                                                             const an::ui::ProcessRun& run,
                                                             const SharpenAlphaParams& params,
                                                             an::ui::ExecutionScope& scope)
{
    // G'MIC's multiscale alpha sharpening.
    const std::string command =
        fmt::format("sharpen_alpha {},{},{}", params.amplitude, params.numOfScales, params.anisotropy);
    return process_utils::runGmicCommand(scope, input, output, process_utils::GmicCommand{.text = command});
}

an::Status SharpenAlphaProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                        an::ui::ProcessDestination& destination,
                                                        const an::ui::ProcessRun& run, const SharpenAlphaParams& params,
                                                        an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep SharpenAlphaProcess::describeTypedProcessStep(const SharpenAlphaParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Amplitude: {:.2f}, Number of Scales: {}, Anisotropy: {:.2f}, Amount: {:.2f}",
                                   params.amplitude, params.numOfScales, params.anisotropy, params.amount) +
                       process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool SharpenAlphaProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void SharpenAlphaProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void SharpenAlphaProcess::resetSettings()
{
    amplitudeControl->setValue(1.0);
    numOfScalesControl->setValue(4);
    anisotropyControl->setValue(0.5);
    amountControl->setValue(1.0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* SharpenAlphaProcess::getProgressDialogControl(const bool forSequence)
{
    return process_utils::gmicProgressDialog(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
}

void SharpenAlphaProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void SharpenAlphaProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    amplitudeControl->setEnabled(enabled);
    numOfScalesControl->setEnabled(enabled);
    anisotropyControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

double SharpenAlphaProcess::getTypedModulationFromParams(const SharpenAlphaParams& params)
{
    return params.amount;
}

SharpenAlphaParams SharpenAlphaProcess::getParams() const
{
    SharpenAlphaParams params;
    params.amplitude = amplitudeControl->getValue();
    params.numOfScales = numOfScalesControl->getValue();
    params.anisotropy = anisotropyControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
