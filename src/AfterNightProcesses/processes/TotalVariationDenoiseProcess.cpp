// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TotalVariationDenoiseProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/TotalVariationDenoise.h>

TotalVariationDenoiseProcess::TotalVariationDenoiseProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<TotalVariationDenoiseParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildTotalVariationDenoiseProcessUi(this);
    setProcessDescription(
        QStringLiteral("Suppress noise using total-variation regularization with selectable optimization algorithms."));

    QObject::connect(m_ui.algorithmSelector, &QComboBox::currentTextChanged, this,
                     &TotalVariationDenoiseProcess::on_algorithmSelector_currentTextChanged);

    INIT_D_SPINBOX_CTRL(weightControl, m_ui.weightValue, m_ui.weightSlider,
                        &TotalVariationDenoiseProcess::on_weightChanged);
    INIT_D_SPINBOX_CTRL(epsControl, m_ui.epsValue, m_ui.epsSlider, &TotalVariationDenoiseProcess::on_epsChanged);
    INIT_SPINBOX_CTRL(maxIterationsControl, m_ui.maxIterationsValue, m_ui.maxIterationsSlider,
                      &TotalVariationDenoiseProcess::on_maxIterationsChanged);

    updateWindowTitle();

    m_ui.algorithmSelector->addItem("Bregman");
    m_ui.algorithmSelector->addItem("Chambolle");

    m_ui.algorithmSelector->setCurrentText("Bregman");

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(800, 800), "Preview: Total Variation Denoise");
    settingsUpdated();

    refreshTheme();
}

std::string TotalVariationDenoiseProcess::getProcessNameStatic()
{
    return "Total Variation Denoise";
}

bool TotalVariationDenoiseProcess::requiresExclusiveAccessStatic()
{
    return false;
}

TotalVariationDenoiseProcess::~TotalVariationDenoiseProcess() {}

void TotalVariationDenoiseProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void TotalVariationDenoiseProcess::on_weightChanged(double value)
{
    settingsUpdated();
}

void TotalVariationDenoiseProcess::on_epsChanged(double value)
{
    settingsUpdated();
}

void TotalVariationDenoiseProcess::on_maxIterationsChanged(int value)
{
    settingsUpdated();
}

void TotalVariationDenoiseProcess::on_algorithmSelector_currentTextChanged(const QString& str)
{
    if (str == "Bregman")
    {
        maxIterationsControl->setValue(100);
        epsControl->setValue(0.001);
        m_ui.isotropic->setChecked(true);
        m_ui.isotropic->setEnabled(true);
    }
    else if (str == "Chambolle")
    {
        maxIterationsControl->setValue(200);
        epsControl->setValue(0.0002);
        m_ui.isotropic->setEnabled(false);
    }

    settingsUpdated();
}

an::Status TotalVariationDenoiseProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                      const an::ChannelScopeOutput& output,
                                                                      const an::ui::ProcessRun& run,
                                                                      const TotalVariationDenoiseParams& params,
                                                                      an::ui::ExecutionScope& scope)
{
    // The dialog's weight is a slider between zero and one; each method reads it through a mapping of its own. The
    // weight and tolerance are fractions of full scale and the engine solves over the stored values, so an integer
    // plane's are restated in its own units: the functional is the same, scaled by the span.
    const an::SampleType type = input.descriptor->sampleType();
    const double span = process_utils::unitScaleOf(type).span;
    an::TotalVariationRequest request;
    request.maximumIterations = static_cast<std::uint32_t>((std::max)(1, params.maxIterations));
    if (params.algorithmSelector == 0)
    {
        double weight = 120.0 * (1.0 - params.weight);
        if (weight == 0.0)
        {
            weight = 1.0;
        }
        request.method = an::TotalVariationMethod::SplitBregman;
        request.weight = weight / span;
        request.tolerance = params.eps * span;
        request.isotropic = params.isotropic;
    }
    else
    {
        double weight = params.weight * 0.1;
        if (weight == 0.0)
        {
            weight = 0.0001;
        }
        request.method = an::TotalVariationMethod::Chambolle;
        request.weight = weight * span;
        request.tolerance = params.eps;
    }
    const an::Result<an::TotalVariationPlan> plan = an::planTotalVariationDenoise(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const process_utils::PlaneSelection selection(input);
    return process_utils::statusOf(an::denoiseImageTotalVariation(scope.execution(), type, input.samples, *input.layout,
                                                                  output.samples, *output.layout, *plan,
                                                                  selection.selection()));
}

an::Status TotalVariationDenoiseProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                 an::ui::ProcessDestination& destination,
                                                                 const an::ui::ProcessRun& run,
                                                                 const TotalVariationDenoiseParams& params,
                                                                 an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep
TotalVariationDenoiseProcess::describeTypedProcessStep(const TotalVariationDenoiseParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Algorithm: {}, weight: {}, eps: {}, max iterations: {} - ",
                                               m_ui.algorithmSelector->currentText().toUtf8().toStdString(),
                                               params.weight, params.eps, params.maxIterations) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool TotalVariationDenoiseProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void TotalVariationDenoiseProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void TotalVariationDenoiseProcess::resetSettings()
{
    weightControl->setValue(0.5);
    epsControl->setValue(0.001);
    maxIterationsControl->setValue(100);

    m_ui.isotropic->setChecked(true);

    m_ui.algorithmSelector->setCurrentText("Bregman");

    process_utils::selectWholeImageChannel(m_image_view_control, m_ui.channelSelector);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* TotalVariationDenoiseProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 100);
    progress->setValue(0);

    return progress;
}

void TotalVariationDenoiseProcess::settingsUpdatedImpl()
{
    /*if (sigmaValue->value() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else*/
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void TotalVariationDenoiseProcess::enableProcessControlsImpl(bool enabled)
{
    const bool bregman_selected = m_ui.algorithmSelector->currentText() == QStringLiteral("Bregman");

    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    weightControl->setEnabled(enabled);
    epsControl->setEnabled(enabled);
    maxIterationsControl->setEnabled(enabled);
    m_ui.isotropic->setEnabled(enabled && bregman_selected);
    m_ui.algorithmSelector->setEnabled(enabled);
    m_ui.channelSelector->setEnabled(enabled);
}

TotalVariationDenoiseParams TotalVariationDenoiseProcess::getParams() const
{
    TotalVariationDenoiseParams params;

    params.algorithmSelector = m_ui.algorithmSelector->currentIndex();
    params.weight = weightControl->getValue();
    params.eps = epsControl->getValue();
    params.maxIterations = maxIterationsControl->getValue();

    params.isotropic = m_ui.isotropic->isChecked();

    return params;
}
