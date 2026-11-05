// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WaveletDenoiseProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/GmicProcessSupport.h"

WaveletDenoiseProcess::WaveletDenoiseProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<WaveletDenoiseParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildWaveletDenoiseProcessUi(this);
    setProcessDescription(
        QStringLiteral("Reduce noise with multi-scale wavelet thresholding and controlled blend amount."));

    INIT_D_SPINBOX_CTRL(thresholdControl, m_ui.thresholdValue, m_ui.thresholdSlider,
                        &WaveletDenoiseProcess::on_thresholdChanged);
    INIT_SPINBOX_CTRL(scalesControl, m_ui.scalesValue, m_ui.scalesSlider, &WaveletDenoiseProcess::on_scalesChanged);
    INIT_SPINBOX_CTRL(iterationsControl, m_ui.iterationsValue, m_ui.iterationsSlider,
                      &WaveletDenoiseProcess::on_iterationsChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &WaveletDenoiseProcess::on_amountChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(500, 500), "Preview: Wavelet Denoise Filter");
    settingsUpdated();

    refreshTheme();

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

std::string WaveletDenoiseProcess::getProcessNameStatic()
{
    return "Wavelet Denoise";
}

bool WaveletDenoiseProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void WaveletDenoiseProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
}

WaveletDenoiseProcess::~WaveletDenoiseProcess() {}

void WaveletDenoiseProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void WaveletDenoiseProcess::on_thresholdChanged(double value)
{
    settingsUpdated();
}

void WaveletDenoiseProcess::on_scalesChanged(int value)
{
    settingsUpdated();
}

void WaveletDenoiseProcess::on_iterationsChanged(int value)
{
    settingsUpdated();
}

void WaveletDenoiseProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

bool WaveletDenoiseProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void WaveletDenoiseProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void WaveletDenoiseProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void WaveletDenoiseProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void WaveletDenoiseProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);

    thresholdControl->setEnabled(enabled);
    iterationsControl->setEnabled(enabled);
    scalesControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void WaveletDenoiseProcess::initialize()
{
    thresholdControl->setValue(1.4);
    iterationsControl->setValue(10);
    scalesControl->setValue(6);
    amountControl->setValue(1.0);
}

ProgressDialogControl* WaveletDenoiseProcess::getProgressDialogControl(const bool forSequence)
{
    return process_utils::gmicProgressDialog(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
}

an::Status WaveletDenoiseProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                               const an::ChannelScopeOutput& output,
                                                               const an::ui::ProcessRun& run,
                                                               const WaveletDenoiseParams& params,
                                                               an::ui::ExecutionScope& scope)
{
    // G'MIC's Haar wavelet thresholding with cycle spinning; its shifts are drawn from a generator seeded
    // with a constant for the command and reseeded after it, so one input gives one answer.
    const std::string command =
        fmt::format("srand 0 denoise_haar {},{},{} srand", params.threshold, params.scales, params.iterations);
    return process_utils::runGmicCommand(scope, input, output, process_utils::GmicCommand{.text = command});
}

an::Status WaveletDenoiseProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                          an::ui::ProcessDestination& destination,
                                                          const an::ui::ProcessRun& run,
                                                          const WaveletDenoiseParams& params,
                                                          an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep WaveletDenoiseProcess::describeTypedProcessStep(const WaveletDenoiseParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Threshold: {:.2f}, Scales: {}, Iterations: {}, Amount: {:.2f}",
                                               params.threshold, params.scales, params.iterations, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

double WaveletDenoiseProcess::getTypedModulationFromParams(const WaveletDenoiseParams& params)
{
    return params.amount;
}

WaveletDenoiseParams WaveletDenoiseProcess::getParams() const
{
    WaveletDenoiseParams params;
    params.threshold = thresholdControl->getValue();
    params.scales = scalesControl->getValue();
    params.iterations = iterationsControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
