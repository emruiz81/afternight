// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "NoiseGeneratorProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Synthesis/NoiseGeneration.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <chrono>
#include <cstdint>
#include <random>

namespace
{

/// A seed no earlier dialog drew: the platform's entropy source mixed with the clock.
[[nodiscard]] std::uint64_t drawNoiseSeed()
{
    std::random_device entropy;
    const auto clock = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return (static_cast<std::uint64_t>(entropy()) << 32u) ^ static_cast<std::uint64_t>(entropy()) ^ clock;
}

} // namespace

NoiseGeneratorProcess::NoiseGeneratorProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<NoiseGeneratorParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildNoiseGeneratorProcessUi(this);
    m_seed = drawNoiseSeed();

    setProcessDescription(QStringLiteral(
        "Generate and add various types of noise (Gaussian, Poisson, Salt & Pepper, etc.) to the image."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    QObject::connect(m_ui.noiseTypeSelector, &QComboBox::currentTextChanged, this,
                     &NoiseGeneratorProcess::on_noiseTypeSelector_currentTextChanged);

    INIT_D_SPINBOX_CTRL(meanControl, m_ui.meanValue, m_ui.meanSlider, &NoiseGeneratorProcess::on_meanChanged);
    INIT_D_SPINBOX_CTRL(varControl, m_ui.varValue, m_ui.varSlider, &NoiseGeneratorProcess::on_varChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &NoiseGeneratorProcess::on_amountChanged);
    INIT_D_SPINBOX_CTRL(saltVsPepperControl, m_ui.saltVsPepperValue, m_ui.saltVsPepperSlider,
                        &NoiseGeneratorProcess::on_saltVsPepperChanged);

    updateWindowTitle();

    m_ui.noiseTypeSelector->addItem("Gaussian");
    m_ui.noiseTypeSelector->addItem("LocalVar");
    m_ui.noiseTypeSelector->addItem("Poisson");
    m_ui.noiseTypeSelector->addItem("Salt");
    m_ui.noiseTypeSelector->addItem("Pepper");
    m_ui.noiseTypeSelector->addItem("Salt & Pepper");
    m_ui.noiseTypeSelector->addItem("Speckle");

    m_ui.noiseTypeSelector->setCurrentText("Gaussian");

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(1200, 1200), "Preview: Noise Generator");

    refreshTheme();
}

void NoiseGeneratorProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string NoiseGeneratorProcess::getProcessNameStatic()
{
    return "Noise Generator";
}

bool NoiseGeneratorProcess::requiresExclusiveAccessStatic()
{
    return false;
}

NoiseGeneratorProcess::~NoiseGeneratorProcess() {}

void NoiseGeneratorProcess::on_meanChanged(double value)
{

    settingsUpdated();
}

void NoiseGeneratorProcess::on_varChanged(double value)
{
    settingsUpdated();
}

void NoiseGeneratorProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void NoiseGeneratorProcess::on_saltVsPepperChanged(double value)
{
    settingsUpdated();
}

void NoiseGeneratorProcess::on_noiseTypeSelector_currentTextChanged(const QString& str)
{
    updateEnabledActions(str);

    settingsUpdated();
}

an::Status NoiseGeneratorProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                               const an::ChannelScopeOutput& output,
                                                               const an::ui::ProcessRun& run,
                                                               const NoiseGeneratorParams& params,
                                                               an::ui::ExecutionScope& scope)
{
    // The dialog's quantities are fractions of full scale; the engine draws in the stored units, so an integer plane's
    // are restated in its own: a mean by the span, a variance by its square, and the clip, the salt and the pepper at
    // the representation's extremes.
    const an::SampleType type = input.descriptor->sampleType();
    const process_utils::UnitScale unit = process_utils::unitScaleOf(type);
    an::NoiseGenerationRequest request;
    request.clip = true;
    request.lowValue = unit.offset;
    request.highValue = unit.offset + unit.span;
    // Every frame of a sequence draws its own noise; frame zero, and a still image, draw the dialog's.
    request.seed = params.seed + 0x9E3779B97F4A7C15ull * run.frame;
    switch (params.noiseTypeSelector)
    {
    case 0: // Gaussian
        request.kind = an::NoiseKind::Gaussian;
        request.mean = params.mean * unit.span;
        request.variance = params.var * unit.span * unit.span;
        break;
    case 1: // LocalVar: no variance map is supplied, so the draw has a constant variance of 0.01.
        request.kind = an::NoiseKind::Gaussian;
        request.mean = 0.0;
        request.variance = 0.01 * unit.span * unit.span;
        break;
    case 2: // Poisson: the rate is the sample counted on a sixteen-bit scale, an eight-bit sample on its own codes.
    {
        const bool narrowInteger = an::isIntegerSampleType(type) && an::sampleTypeSizeBytes(type) <= 2u;
        const double counts = narrowInteger ? static_cast<double>(1u << (8u * an::sampleTypeSizeBytes(type))) : 65536.0;
        request.kind = an::NoiseKind::Poisson;
        request.poissonScale = counts / unit.span;
        break;
    }
    case 3:
        request.kind = an::NoiseKind::Salt;
        request.amount = params.amount;
        break;
    case 4:
        request.kind = an::NoiseKind::Pepper;
        request.amount = params.amount;
        break;
    case 5:
        request.kind = an::NoiseKind::SaltAndPepper;
        request.amount = params.amount;
        request.saltFraction = params.saltVsPepper;
        break;
    default: // Speckle multiplies the sample, so its quantities are already relative.
        request.kind = an::NoiseKind::Speckle;
        request.mean = params.mean;
        request.variance = params.var;
        break;
    }
    const an::Result<an::NoiseGenerationPlan> plan = an::planNoiseGeneration(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const process_utils::PlaneSelection selection(input);
    return process_utils::statusOf(an::generateImageNoise(scope.execution(), type, input.samples, *input.layout,
                                                          output.samples, *output.layout, *plan,
                                                          selection.selection()));
}

an::Status NoiseGeneratorProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                          an::ui::ProcessDestination& destination,
                                                          const an::ui::ProcessRun& run,
                                                          const NoiseGeneratorParams& params,
                                                          an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep NoiseGeneratorProcess::describeTypedProcessStep(const NoiseGeneratorParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Noise Type {} - ", m_ui.noiseTypeSelector->currentText().toUtf8().toStdString()) +
                       process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

void NoiseGeneratorProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    // Noise applied again is new noise: the next preview and Apply draw from a stream of their own.
    if (previewMode == RTPreviewMode::NoPreview)
    {
        m_seed = drawNoiseSeed();
        if (m_ui.preview->isChecked())
        {
            schedulePreviewProcessExecution();
        }
    }
}

bool NoiseGeneratorProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void NoiseGeneratorProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void NoiseGeneratorProcess::resetSettings()
{
    meanControl->setValue(0.0);
    varControl->setValue(0.002);
    amountControl->setValue(0.02);
    saltVsPepperControl->setValue(0.5);

    meanControl->setEnabled(true);
    varControl->setEnabled(true);
    amountControl->setEnabled(false);
    saltVsPepperControl->setEnabled(false);

    m_ui.noiseTypeSelector->setCurrentText("Gaussian");

    process_utils::selectWholeImageChannel(m_image_view_control, m_ui.channelSelector);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void NoiseGeneratorProcess::updateEnabledActions(const QString& str)
{
    if (str == "Gaussian")
    {
        meanControl->setEnabled(true);
        varControl->setEnabled(true);
        amountControl->setEnabled(false);
        saltVsPepperControl->setEnabled(false);
    }
    else if (str == "LocalVar")
    {
        meanControl->setEnabled(false);
        varControl->setEnabled(false);
        amountControl->setEnabled(false);
        saltVsPepperControl->setEnabled(false);
    }
    else if (str == "Poisson")
    {
        meanControl->setEnabled(false);
        varControl->setEnabled(false);
        amountControl->setEnabled(false);
        saltVsPepperControl->setEnabled(false);
    }
    else if (str == "Salt")
    {
        amountControl->setEnabled(true);
        saltVsPepperControl->setEnabled(false);
    }
    else if (str == "Pepper")
    {
        amountControl->setEnabled(true);
        saltVsPepperControl->setEnabled(false);
    }
    else if (str == "Salt & Pepper")
    {
        amountControl->setEnabled(true);
        saltVsPepperControl->setEnabled(true);
    }
    else if (str == "Speckle")
    {
        meanControl->setEnabled(true);
        varControl->setEnabled(true);
        amountControl->setEnabled(false);
    }
}

ProgressDialogControl* NoiseGeneratorProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 0);
    progress->setValue(0);

    return progress;
}

void NoiseGeneratorProcess::settingsUpdatedImpl()
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

void NoiseGeneratorProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    /*if (enabled)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(sigmaValue->value() != 0.0);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    */

    if (enabled)
    {
        updateEnabledActions(m_ui.noiseTypeSelector->currentText());
    }
    else
    {
        meanControl->setEnabled(false);
        varControl->setEnabled(false);
        amountControl->setEnabled(false);
        saltVsPepperControl->setEnabled(false);
    }

    m_ui.noiseTypeSelector->setEnabled(enabled);
}

NoiseGeneratorParams NoiseGeneratorProcess::getParams() const
{
    NoiseGeneratorParams params;
    params.noiseTypeSelector = m_ui.noiseTypeSelector->currentIndex();

    params.mean = meanControl->getValue();
    params.var = varControl->getValue();
    params.amount = amountControl->getValue();
    params.saltVsPepper = saltVsPepperControl->getValue();
    params.seed = m_seed;

    return params;
}
