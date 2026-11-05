// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "NLBayesProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>

NLBayesProcess::NLBayesProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<NLBayesParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildNLBayesProcessUi(this);
    // The engine runs at most this many strengthen-and-subtract passes; the dialog offers no more.
    m_ui.sosIterations->setMaximum(static_cast<int>(an::kMaximumPatchDenoiseIterations));
    setProcessDescription(
        QStringLiteral("Run non-local Bayes denoising with optional homogeneous-area and DA3D refinements."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    QObject::connect(m_ui.useArea1, &QCheckBox::checkStateChanged, this, &NLBayesProcess::on_useArea1Clicked);
    QObject::connect(m_ui.useArea2, &QCheckBox::checkStateChanged, this, &NLBayesProcess::on_useArea2Clicked);
    QObject::connect(m_ui.da3d, &QCheckBox::checkStateChanged, this, &NLBayesProcess::on_da3dClicked);
    QObject::connect(m_ui.anscombe, &QCheckBox::checkStateChanged, this, &NLBayesProcess::on_anscombeClicked);
    QObject::connect(m_ui.autoSigma, &QCheckBox::checkStateChanged, this, &NLBayesProcess::on_autoSigmaClicked);
    QObject::connect(m_ui.sosIterations, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &NLBayesProcess::on_sosIterationsChanged);

    INIT_D_SPINBOX_CTRL(sigmaControl, m_ui.sigmaValue, m_ui.sigmaSlider, &NLBayesProcess::on_sigmaChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &NLBayesProcess::on_amountChanged);
    INIT_D_SPINBOX_CTRL(sosRhoControl, m_ui.sosRhoValue, m_ui.sosRhoSlider, &NLBayesProcess::on_sosRhoChanged);

    updateWindowTitle();

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(500, 500), "Preview: Non-Local Bayes Filter");
    settingsUpdated();

    refreshTheme();
}

std::string NLBayesProcess::getProcessNameStatic()
{
    return "Non-Local Bayes";
}

bool NLBayesProcess::requiresExclusiveAccessStatic()
{
    return false;
}

NLBayesProcess::~NLBayesProcess() {}

void NLBayesProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void NLBayesProcess::on_sigmaChanged(double value)
{
    settingsUpdated();
}

void NLBayesProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void NLBayesProcess::on_useArea1Clicked(Qt::CheckState state)
{
    settingsUpdated();
}

void NLBayesProcess::on_useArea2Clicked(Qt::CheckState state)
{
    settingsUpdated();
}

void NLBayesProcess::on_da3dClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void NLBayesProcess::on_anscombeClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void NLBayesProcess::on_autoSigmaClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void NLBayesProcess::on_sosIterationsChanged(int value)
{
    settingsUpdated();
}

void NLBayesProcess::on_sosRhoChanged(double value)
{
    settingsUpdated();
}

bool NLBayesProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void NLBayesProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void NLBayesProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void NLBayesProcess::settingsUpdatedImpl()
{
    syncDependentControls(true);
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

void NLBayesProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);

    amountControl->setEnabled(enabled);

    m_ui.useArea1->setEnabled(enabled);
    m_ui.useArea2->setEnabled(enabled);
    m_ui.anscombe->setEnabled(enabled);
    m_ui.autoSigma->setEnabled(enabled);
    syncDependentControls(enabled);
}

void NLBayesProcess::initialize()
{
    sigmaControl->setValue(5.0);
    amountControl->setValue(1.0);
    sosRhoControl->setValue(0.8);
    m_ui.sosIterations->setValue(1);
    m_ui.autoSigma->setChecked(true);
    syncDependentControls(true);
}

void NLBayesProcess::syncDependentControls(bool enabled)
{
    const bool anscombeActive = m_ui.anscombe->isChecked();
    const bool autoSigmaActive = m_ui.autoSigma->isChecked();
    const bool allowDa3d = enabled && !anscombeActive;
    const bool allowSos = enabled && !anscombeActive;
    const bool allowSigma = enabled && !autoSigmaActive;

    sigmaControl->setEnabled(allowSigma);
    if (m_ui.label_5)
    {
        m_ui.label_5->setEnabled(allowSigma);
    }
    m_ui.da3d->setEnabled(allowDa3d);
    m_ui.sosIterations->setEnabled(allowSos);
    sosRhoControl->setEnabled(allowSos);

    if (m_ui.label_sos)
    {
        m_ui.label_sos->setEnabled(allowSos);
    }

    if (m_ui.label_rho)
    {
        m_ui.label_rho->setEnabled(allowSos);
    }
}

ProgressDialogControl* NLBayesProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 100);
    progress->setValue(0);

    return progress;
}

an::Status NLBayesProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                        const an::ChannelScopeOutput& output,
                                                        const an::ui::ProcessRun& run, const NLBayesParams& params,
                                                        an::ui::ExecutionScope& scope)
{
    // The dialog's sigma is on a tuning scale where full scale is 255; the engine takes it in the plane's own units,
    // whose full scale is one for a floating plane and a component of another encoding and the representation's span
    // for an integer one. The finishing pass runs at the dialog's sigma.
    const NLBayesParams effective = sanitizeParamsForExecution(params);
    const an::SampleType type = input.descriptor->sampleType();
    const double white = process_utils::unitScaleOf(type).span;
    const double toUnits = white / an::kPatchDenoiseTuningScale;
    const an::PatchDenoiseRequest request{.sigma = effective.sigma * toUnits,
                                          .measureNoise = effective.autoSigma,
                                          .whiteLevel = white,
                                          .homogeneousAreaFirstPass = effective.useArea1,
                                          .homogeneousAreaSecondPass = effective.useArea2,
                                          .iterations = effective.sosIterations,
                                          .reinjection = effective.sosRho,
                                          .varianceStabilisation = effective.anscombe,
                                          .finishing = effective.da3d ? an::PatchDenoiseFinishing::GuidedDualDomain
                                                                      : an::PatchDenoiseFinishing::None,
                                          .finishingSigma = effective.da3d ? effective.sigma * toUnits : 0.0};
    const an::Result<an::PatchDenoisePlan> plan =
        an::planPatchDenoise(request, input.descriptor->extent().channelCount());
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const process_utils::PlaneSelection selection(input);
    return process_utils::statusOf(an::denoiseImagePatches(scope.execution(), type, input.samples, *input.layout,
                                                           output.samples, *output.layout, *plan,
                                                           selection.selection()));
}

an::Status NLBayesProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                   an::ui::ProcessDestination& destination,
                                                   const an::ui::ProcessRun& run, const NLBayesParams& params,
                                                   an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, sanitizeParamsForExecution(params), scope);
}

an::ui::ProcessStep NLBayesProcess::describeTypedProcessStep(const NLBayesParams& params) const
{
    const NLBayesParams effective = sanitizeParamsForExecution(params);
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Sigma: {}, Step 1 Homogeneous Area: {}, Step 2 Homogeneous Area: {}, DA3D: {}, "
                                   "Anscombe: {}, SOS: {} (rho={:.2f}), Amount: {:.2f}",
                                   effective.autoSigma ? "auto" : fmt::format("{:.4f}", effective.sigma),
                                   effective.useArea1, effective.useArea2, effective.da3d, effective.anscombe,
                                   effective.sosIterations, effective.sosRho, effective.amount) +
                       process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

double NLBayesProcess::getTypedModulationFromParams(const NLBayesParams& params)
{
    return params.amount;
}

NLBayesParams NLBayesProcess::getParams() const
{
    NLBayesParams params;
    params.useArea1 = m_ui.useArea1->isChecked();
    params.useArea2 = m_ui.useArea2->isChecked();
    params.da3d = m_ui.da3d->isChecked();
    params.anscombe = m_ui.anscombe->isChecked();
    params.autoSigma = m_ui.autoSigma->isChecked();

    params.sigma = sigmaControl->getValue();
    params.amount = amountControl->getValue();
    params.sosIterations = static_cast<unsigned>(m_ui.sosIterations->value());
    params.sosRho = sosRhoControl->getValue();

    return params;
}
