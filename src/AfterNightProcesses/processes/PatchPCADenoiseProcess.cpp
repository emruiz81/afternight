// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PatchPCADenoiseProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/GmicProcessSupport.h"

PatchPCADenoiseProcess::PatchPCADenoiseProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<PatchPCADenoiseParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildPatchPCADenoiseProcessUi(this);
    setProcessDescription(
        QStringLiteral("Denoise by PCA analysis of similar patches with tunable search and sampling."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    INIT_D_SPINBOX_CTRL(strenghtControl, m_ui.strenghtValue, m_ui.strenghtSlider,
                        &PatchPCADenoiseProcess::on_strenghtChanged);
    INIT_SPINBOX_CTRL(patchSizeControl, m_ui.patchSizeValue, m_ui.patchSizeSlider,
                      &PatchPCADenoiseProcess::on_patchSizeChanged);
    INIT_SPINBOX_CTRL(lookupSizeControl, m_ui.lookupSizeValue, m_ui.lookupSizeSlider,
                      &PatchPCADenoiseProcess::on_lookupSizeChanged);
    INIT_SPINBOX_CTRL(spatialSamplingControl, m_ui.spatialSamplingValue, m_ui.spatialSamplingSlider,
                      &PatchPCADenoiseProcess::on_spatialSamplingChanged);

    updateWindowTitle();

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(350, 350), "Preview: Patch PCA Denoise Filter");

    refreshTheme();

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

std::string PatchPCADenoiseProcess::getProcessNameStatic()
{
    return "Patch PCA Denoise";
}

bool PatchPCADenoiseProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void PatchPCADenoiseProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
}

PatchPCADenoiseProcess::~PatchPCADenoiseProcess() {}

void PatchPCADenoiseProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void PatchPCADenoiseProcess::on_strenghtChanged(double value)
{
    settingsUpdated();
}

void PatchPCADenoiseProcess::on_patchSizeChanged(int value)
{
    settingsUpdated();
}

void PatchPCADenoiseProcess::on_lookupSizeChanged(int value)
{
    settingsUpdated();
}

void PatchPCADenoiseProcess::on_spatialSamplingChanged(int value)
{
    settingsUpdated();
}

bool PatchPCADenoiseProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void PatchPCADenoiseProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void PatchPCADenoiseProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void PatchPCADenoiseProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void PatchPCADenoiseProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);

    strenghtControl->setEnabled(enabled);
    patchSizeControl->setEnabled(enabled);
    lookupSizeControl->setEnabled(enabled);
    spatialSamplingControl->setEnabled(enabled);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

ProgressDialogControl* PatchPCADenoiseProcess::getProgressDialogControl(const bool forSequence)
{
    return process_utils::gmicProgressDialog(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
}

void PatchPCADenoiseProcess::initialize()
{
    strenghtControl->setValue(4);
    patchSizeControl->setValue(7);
    lookupSizeControl->setValue(11);
    spatialSamplingControl->setValue(7);
}

an::Status PatchPCADenoiseProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                const an::ChannelScopeOutput& output,
                                                                const an::ui::ProcessRun& run,
                                                                const PatchPCADenoiseParams& params,
                                                                an::ui::ExecutionScope& scope)
{
    // G'MIC's patch-based principal-component denoiser.
    const std::string command = fmt::format("denoise_patchpca {},{},{},{}", params.strenght, params.patchSize,
                                            params.lookupSize, params.spatialSampling);
    return process_utils::runGmicCommand(scope, input, output, process_utils::GmicCommand{.text = command});
}

an::Status PatchPCADenoiseProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                           an::ui::ProcessDestination& destination,
                                                           const an::ui::ProcessRun& run,
                                                           const PatchPCADenoiseParams& params,
                                                           an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep PatchPCADenoiseProcess::describeTypedProcessStep(const PatchPCADenoiseParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Strength: {:.2f}, Patch Size: {}, Lookup Size: {}, Spatial Sampling: {}",
                                   params.strenght, params.patchSize, params.lookupSize, params.spatialSampling) +
                       process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

PatchPCADenoiseParams PatchPCADenoiseProcess::getParams() const
{
    PatchPCADenoiseParams params;
    params.strenght = strenghtControl->getValue();
    params.patchSize = patchSizeControl->getValue();
    params.lookupSize = lookupSizeControl->getValue();
    params.spatialSampling = spatialSamplingControl->getValue();

    return params;
}
