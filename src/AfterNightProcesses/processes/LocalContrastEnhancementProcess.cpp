// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LocalContrastEnhancementProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/GmicProcessSupport.h"

LocalContrastEnhancementProcess::LocalContrastEnhancementProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<LocalContrastEnhancementParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildLocalContrastEnhancementProcessUi(this);
    setProcessDescription(
        QStringLiteral("Boost fine local contrast with spatial radius, tone weighting, and modulation controls."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    INIT_D_SPINBOX_CTRL(spatialRadiusControl, m_ui.spatialRadiusValue, m_ui.spatialRadiusSlider,
                        &LocalContrastEnhancementProcess::on_spatialRadiusChanged);
    INIT_D_SPINBOX_CTRL(weightControl, m_ui.weightValue, m_ui.weightSlider,
                        &LocalContrastEnhancementProcess::on_weightChanged);
    INIT_D_SPINBOX_CTRL(darknessLevelControl, m_ui.darknessLevelValue, m_ui.darknessLevelSlider,
                        &LocalContrastEnhancementProcess::on_darknessLevelChanged);
    INIT_D_SPINBOX_CTRL(lightnessLevelControl, m_ui.lightnessLevelValue, m_ui.lightnessLevelSlider,
                        &LocalContrastEnhancementProcess::on_lightnessLevelChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider,
                        &LocalContrastEnhancementProcess::on_amountChanged);

    updateWindowTitle();

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    settingsUpdated();

    setupPreviewControl(true, QSize(600, 600), "Preview: Local Contrast Enhancement");

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

std::string LocalContrastEnhancementProcess::getProcessNameStatic()
{
    return "Local Contrast Enhancement";
}

bool LocalContrastEnhancementProcess::requiresExclusiveAccessStatic()
{
    return false;
}

LocalContrastEnhancementProcess::~LocalContrastEnhancementProcess() {}

void LocalContrastEnhancementProcess::on_spatialRadiusChanged(double value)
{
    settingsUpdated();
}

void LocalContrastEnhancementProcess::on_weightChanged(double value)
{
    settingsUpdated();
}

void LocalContrastEnhancementProcess::on_darknessLevelChanged(double value)
{
    settingsUpdated();
}

void LocalContrastEnhancementProcess::on_lightnessLevelChanged(double value)
{
    settingsUpdated();
}

void LocalContrastEnhancementProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

an::Status LocalContrastEnhancementProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                         const an::ChannelScopeOutput& output,
                                                                         const an::ui::ProcessRun& run,
                                                                         const LocalContrastEnhancementParams& params,
                                                                         an::ui::ExecutionScope& scope)
{
    // Arto Huotari's local contrast enhancement: the dialog's weight is the filter's amount, and its
    // definitions travel with the command rather than being read from a file beside the program.
    const std::string command = fmt::format("fx_LCE {},{},{},{},0,0", params.spatialRadius, params.weight,
                                            params.darknessLevel, params.lightnessLevel);
    return process_utils::runGmicCommand(
        scope, input, output,
        process_utils::GmicCommand{.text = command,
                                   .definitions = process_utils::kLocalContrastEnhancementDefinitions});
}

an::Status LocalContrastEnhancementProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                    an::ui::ProcessDestination& destination,
                                                                    const an::ui::ProcessRun& run,
                                                                    const LocalContrastEnhancementParams& params,
                                                                    an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep
LocalContrastEnhancementProcess::describeTypedProcessStep(const LocalContrastEnhancementParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Spatial Radius: {:.2f}, Weight: {:.2f}, Darkness Level: {:.2f}, "
                                               "Lightness Level: {:.2f}, Amount: {:.2f}",
                                               params.spatialRadius, params.weight, params.darknessLevel,
                                               params.lightnessLevel, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool LocalContrastEnhancementProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void LocalContrastEnhancementProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void LocalContrastEnhancementProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();

    spatialRadiusControl->setValue(80.0);
    weightControl->setValue(0.5);
    darknessLevelControl->setValue(1.0);
    lightnessLevelControl->setValue(1.0);
    amountControl->setValue(1.0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* LocalContrastEnhancementProcess::getProgressDialogControl(const bool forSequence)
{
    return process_utils::gmicProgressDialog(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
}

void LocalContrastEnhancementProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void LocalContrastEnhancementProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    spatialRadiusControl->setEnabled(enabled);
    weightControl->setEnabled(enabled);
    darknessLevelControl->setEnabled(enabled);
    lightnessLevelControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

double LocalContrastEnhancementProcess::getTypedModulationFromParams(const LocalContrastEnhancementParams& params)
{
    return params.amount;
}

LocalContrastEnhancementParams LocalContrastEnhancementProcess::getParams() const
{
    LocalContrastEnhancementParams params;
    params.spatialRadius = spatialRadiusControl->getValue();
    params.weight = weightControl->getValue();
    params.darknessLevel = darknessLevelControl->getValue();
    params.lightnessLevel = lightnessLevelControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
