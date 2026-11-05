// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BandingReductionProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpatialProcessSupport.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>

BandingReductionProcess::BandingReductionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<BandingReductionParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildBandingReductionProcessUi(this);

    setProcessDescription(QStringLiteral("Reduce horizontal or vertical banding noise in the image."));

    QObject::connect(m_ui.protectFromHighlights, &QCheckBox::checkStateChanged, this,
                     &BandingReductionProcess::on_protectFromHighlights);
    QObject::connect(m_ui.verticalBanding, &QCheckBox::checkStateChanged, this,
                     &BandingReductionProcess::on_verticalBanding);

    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &BandingReductionProcess::on_amountChanged);
    INIT_D_SPINBOX_CTRL(sigmaControl, m_ui.sigmaValue, m_ui.sigmaSlider, &BandingReductionProcess::on_sigmaChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    resetSettings();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(700, 700), "Preview: Banding Reduction");
    settingsUpdated();

    refreshTheme();
}

void BandingReductionProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string BandingReductionProcess::getProcessNameStatic()
{
    return "Banding Reduction";
}

bool BandingReductionProcess::requiresExclusiveAccessStatic()
{
    return false;
}

BandingReductionProcess::~BandingReductionProcess() {}

void BandingReductionProcess::on_kernelSize_currentTextChanged(const QString& text)
{
    settingsUpdated();
}

void BandingReductionProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void BandingReductionProcess::on_sigmaChanged(double value)
{
    settingsUpdated();
}

void BandingReductionProcess::on_protectFromHighlights(Qt::CheckState state)
{
    sigmaControl->setEnabled(state == Qt::Checked);

    settingsUpdated();
}

void BandingReductionProcess::on_verticalBanding(Qt::CheckState state)
{
    settingsUpdated();
}

bool BandingReductionProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void BandingReductionProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status BandingReductionProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                 const an::ChannelScopeOutput& output,
                                                                 const an::ui::ProcessRun& run,
                                                                 const BandingReductionParams& params,
                                                                 an::ui::ExecutionScope& scope)
{
    // A protection sigma of zero puts the reject point at infinity: no sample is protected.
    const bool protect = params.protectFromHighlights && params.sigma > 0.0;
    const an::BandingRequest request{.amount = params.amount,
                                     .protectHighlights = protect,
                                     .highlightSigma = protect ? params.sigma : 1.0,
                                     .verticalBanding = params.verticalBanding};
    const process_utils::PlaneSelection selection(input, process_utils::NonFiniteSamples::MeanNothing);
    return process_utils::statusOf(an::suppressImageBanding(scope.execution(), input.descriptor->sampleType(),
                                                            input.samples, *input.layout, output.samples,
                                                            *output.layout, request, selection.selection()));
}

an::Status BandingReductionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                            an::ui::ProcessDestination& destination,
                                                            const an::ui::ProcessRun& run,
                                                            const BandingReductionParams& params,
                                                            an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep BandingReductionProcess::describeTypedProcessStep(const BandingReductionParams& params) const
{
    std::string description = fmt::format("Amount {}", params.amount);
    if (params.protectFromHighlights)
    {
        description += fmt::format(", Inv Sigma {}", params.sigma);
    }
    description += fmt::format(", Vertical Banding: {}", params.verticalBanding);
    description += process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector);
    return an::ui::ProcessStep{.name = getProcessName(), .description = std::move(description)};
}

void BandingReductionProcess::resetSettings()
{
    amountControl->setValue(1.0);
    sigmaControl->setValue(1.0);

    m_ui.protectFromHighlights->setChecked(false);
    m_ui.verticalBanding->setChecked(false);

    sigmaControl->setEnabled(false);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void BandingReductionProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

void BandingReductionProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    amountControl->setEnabled(enabled);
    sigmaControl->setEnabled(enabled);
    m_ui.protectFromHighlights->setEnabled(enabled);
    m_ui.verticalBanding->setEnabled(enabled);
    if (m_ui.protectFromHighlights->isChecked())
    {
        sigmaControl->setEnabled(enabled);
    }
    else
    {
        sigmaControl->setEnabled(false);
    }
}

ProgressDialogControl* BandingReductionProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 100);
    progress->setValue(0);

    return progress;
}

BandingReductionParams BandingReductionProcess::getParams() const
{
    BandingReductionParams params;
    params.protectFromHighlights = m_ui.protectFromHighlights->isChecked();
    params.verticalBanding = m_ui.verticalBanding->isChecked();
    params.amount = amountControl->getValue();
    params.sigma = sigmaControl->getValue();

    return params;
}
