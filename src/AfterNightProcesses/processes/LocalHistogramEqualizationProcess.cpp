// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LocalHistogramEqualizationProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/GmicProcessSupport.h"

LocalHistogramEqualizationProcess::LocalHistogramEqualizationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<LocalHistogramEqualizationParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildLocalHistogramEqualizationProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Enhance local contrast through adaptive histogram equalization with radius and halo controls."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    QObject::connect(m_ui.reduceHalos, &QCheckBox::checkStateChanged, this,
                     &LocalHistogramEqualizationProcess::on_reduceHalosClicked);

    INIT_D_SPINBOX_CTRL(strengthControl, m_ui.strengthValue, m_ui.strengthSlider,
                        &LocalHistogramEqualizationProcess::on_strengthChanged);
    INIT_SPINBOX_CTRL(radiusControl, m_ui.radiusValue, m_ui.radiusSlider,
                      &LocalHistogramEqualizationProcess::on_radiusChanged);
    INIT_D_SPINBOX_CTRL(sigmaControl, m_ui.sigmaValue, m_ui.sigmaSlider,
                        &LocalHistogramEqualizationProcess::on_sigmaChanged);
    INIT_D_SPINBOX_CTRL(regularizationControl, m_ui.regularizationValue, m_ui.regularizationSlider,
                        &LocalHistogramEqualizationProcess::on_regularizationChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider,
                        &LocalHistogramEqualizationProcess::on_amountChanged);

    updateWindowTitle();

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    settingsUpdated();

    setupPreviewControl(true, QSize(600, 600), "Preview: Local Histogram Equalization");

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

std::string LocalHistogramEqualizationProcess::getProcessNameStatic()
{
    return "Local Histogram Equalization";
}

bool LocalHistogramEqualizationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

LocalHistogramEqualizationProcess::~LocalHistogramEqualizationProcess() {}

void LocalHistogramEqualizationProcess::on_strengthChanged(double value)
{
    settingsUpdated();
}

void LocalHistogramEqualizationProcess::on_radiusChanged(int value)
{
    settingsUpdated();
}

void LocalHistogramEqualizationProcess::on_sigmaChanged(double value)
{
    settingsUpdated();
}

void LocalHistogramEqualizationProcess::on_regularizationChanged(double value)
{
    settingsUpdated();
}

void LocalHistogramEqualizationProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void LocalHistogramEqualizationProcess::on_reduceHalosClicked(int state)
{
    settingsUpdated();
}
//
// void LocalHistogramEqualizationProcess::on_modeComboBox_currentTextChanged(const QString& str)
//{
//    settingsUpdated();
//}

an::Status LocalHistogramEqualizationProcess::executeTypedProcessIteration(
    const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output, const an::ui::ProcessRun& run,
    const LocalHistogramEqualizationParams& params, an::ui::ExecutionScope& scope)
{
    // G'MIC's local histogram equalization in its soft mode, whatever the image.
    const std::string command =
        fmt::format("fx_equalize_local_histograms {},{},{},{},{},{},16,0,50,50", params.strength, 2, params.radius,
                    params.sigma, params.regularization, static_cast<int>(params.reduceHalos));
    return process_utils::runGmicCommand(scope, input, output, process_utils::GmicCommand{.text = command});
}

an::Status LocalHistogramEqualizationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                      an::ui::ProcessDestination& destination,
                                                                      const an::ui::ProcessRun& run,
                                                                      const LocalHistogramEqualizationParams& params,
                                                                      an::ui::ExecutionScope& scope)
{
    if (an::Status equalized = executeTypedProcessUsingChannels(source, destination, run, params, scope);
        equalized.failed())
    {
        return equalized;
    }
    // An equalized image is no longer linear, and its processing state says so. A preview is shown and never kept,
    // so it is spared the copy the record costs.
    const an::consumer::EngineSession* session = engineSession();
    if (run.kind == an::ui::ProcessRunKind::Preview || session == nullptr)
    {
        return an::Status::success();
    }
    return process_utils::markProcessResultNonlinear(*session, scope.engine(), scope.execution(), destination,
                                                     "local-histogram-equalization");
}

an::ui::ProcessStep
LocalHistogramEqualizationProcess::describeTypedProcessStep(const LocalHistogramEqualizationParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Strength: {:.2f}, Radius: {}, Sigma: {:.2f}, Regularization: {:.2f}, "
                                               "Amount: {:.2f}, Reduce Halos: {}",
                                               params.strength, params.radius, params.sigma, params.regularization,
                                               params.amount, params.reduceHalos) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool LocalHistogramEqualizationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void LocalHistogramEqualizationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void LocalHistogramEqualizationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    strengthControl->setValue(75.0);
    radiusControl->setValue(4);
    sigmaControl->setValue(100.0);
    regularizationControl->setValue(8.0);
    m_ui.reduceHalos->setChecked(true);
    amountControl->setValue(1.0);

    // modeComboBox->setCurrentText("Soft");

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* LocalHistogramEqualizationProcess::getProgressDialogControl(const bool forSequence)
{
    return process_utils::gmicProgressDialog(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
}

double LocalHistogramEqualizationProcess::getTypedModulationFromParams(const LocalHistogramEqualizationParams& params)
{
    return params.amount;
}

void LocalHistogramEqualizationProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void LocalHistogramEqualizationProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    strengthControl->setEnabled(enabled);
    radiusControl->setEnabled(enabled);
    sigmaControl->setEnabled(enabled);
    regularizationControl->setEnabled(enabled);
    m_ui.reduceHalos->setEnabled(enabled);
    amountControl->setEnabled(enabled);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

LocalHistogramEqualizationParams LocalHistogramEqualizationProcess::getParams() const
{
    LocalHistogramEqualizationParams params;
    params.strength = strengthControl->getValue();
    params.radius = radiusControl->getValue();
    params.sigma = sigmaControl->getValue();
    params.regularization = regularizationControl->getValue();
    params.amount = amountControl->getValue();
    params.reduceHalos = m_ui.reduceHalos->isChecked();

    return params;
}
