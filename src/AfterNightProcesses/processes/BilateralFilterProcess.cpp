// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BilateralFilterProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>

#include <cmath>

BilateralFilterProcess::BilateralFilterProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<BilateralFilterParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildBilateralFilterProcessUi(this);

    setProcessDescription(QStringLiteral("Apply a bilateral filter to smooth images while preserving edges."));

    QObject::connect(m_ui.linkSigmaControls, &QCheckBox::checkStateChanged, this,
                     &BilateralFilterProcess::on_linkSigmaControlsClicked);

    INIT_D_SPINBOX_CTRL(diameterControl, m_ui.diameterValue, m_ui.diameterSlider,
                        &BilateralFilterProcess::on_diameterChanged);
    INIT_D_SPINBOX_CTRL(sigmaSpaceControl, m_ui.sigmaSpaceValue, m_ui.sigmaSpaceSlider,
                        &BilateralFilterProcess::on_sigmaSpaceChanged);
    INIT_D_SPINBOX_CTRL(sigmaColorControl, m_ui.sigmaColorValue, m_ui.sigmaColorSlider,
                        &BilateralFilterProcess::on_sigmaColorChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &BilateralFilterProcess::on_amountChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(800, 800), "Preview: Bilateral Filter");
    settingsUpdated();

    refreshTheme();
}

void BilateralFilterProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string BilateralFilterProcess::getProcessNameStatic()
{
    return "Bilateral Filter";
}

bool BilateralFilterProcess::requiresExclusiveAccessStatic()
{
    return false;
}

BilateralFilterProcess::~BilateralFilterProcess() {}

void BilateralFilterProcess::on_diameterChanged(double value)
{
    settingsUpdated();
}

void BilateralFilterProcess::on_sigmaSpaceChanged(double value)
{
    if (!m_IgnoreLastSigmaEvent)
    {
        if (m_ui.linkSigmaControls->isChecked())
        {
            m_IgnoreLastSigmaEvent = true;
            sigmaColorControl->setValue(value);
            m_IgnoreLastSigmaEvent = false;
        }

        settingsUpdated();
    }
}

void BilateralFilterProcess::on_sigmaColorChanged(double value)
{
    if (!m_IgnoreLastSigmaEvent)
    {
        if (m_ui.linkSigmaControls->isChecked())
        {
            m_IgnoreLastSigmaEvent = true;
            sigmaSpaceControl->setValue(value);
            m_IgnoreLastSigmaEvent = false;
        }

        settingsUpdated();
    }
}

void BilateralFilterProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void BilateralFilterProcess::on_linkSigmaControlsClicked(Qt::CheckState state)
{
    if (sigmaColorControl->getValue() != sigmaSpaceControl->getValue())
    {
        m_IgnoreLastSigmaEvent = true;
        sigmaColorControl->setValue(sigmaSpaceControl->getValue());
        m_IgnoreLastSigmaEvent = false;
    }

    settingsUpdated();
}

bool BilateralFilterProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void BilateralFilterProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void BilateralFilterProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void BilateralFilterProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

void BilateralFilterProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_ui.linkSigmaControls->setEnabled(enabled);
    diameterControl->setEnabled(enabled);
    sigmaSpaceControl->setEnabled(enabled);
    sigmaColorControl->setEnabled(enabled);

    amountControl->setEnabled(enabled);
}

void BilateralFilterProcess::initialize()
{
    m_ui.linkSigmaControls->setChecked(false);
    diameterControl->setValue(5.0);
    sigmaSpaceControl->setValue(30.0);
    sigmaColorControl->setValue(30.0);

    amountControl->setValue(1.0);
}

ProgressDialogControl* BilateralFilterProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 100);
    progress->setValue(0);

    return progress;
}

namespace
{

/// The window radius the body hands the engine: a diameter of zero asks for the window the spatial sigma implies, which
/// the engine derives and holds at its widest window - zero hands the derivation to the engine; any other diameter is
/// the window's, truncated to whole pixels.
[[nodiscard]] std::uint32_t windowRadiusOf(const BilateralFilterParams& params)
{
    const int diameter = static_cast<int>(params.diameter);
    if (diameter <= 0)
    {
        const double derived = std::round(an::kBilateralSpatialTruncation * params.sigmaSpace);
        return derived > static_cast<double>(an::kMaxBilateralRadius) ? an::kMaxBilateralRadius : 0u;
    }
    return (std::max)(1u, static_cast<std::uint32_t>(diameter / 2));
}

} // namespace

an::Status BilateralFilterProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                const an::ChannelScopeOutput& output,
                                                                const an::ui::ProcessRun& run,
                                                                const BilateralFilterParams& params,
                                                                an::ui::ExecutionScope& scope)
{
    // A sigma the filter cannot weigh by leaves the image as it is.
    if (params.sigmaColor <= an::kMinimumBilateralSigma || params.sigmaSpace <= an::kMinimumBilateralSigma)
    {
        return an::Status::success();
    }
    // The colour sigma is a fraction of full scale; the engine weighs the stored values, so an integer plane's is
    // restated in its own units.
    const an::SampleType type = input.descriptor->sampleType();
    const an::Result<an::BilateralPlan> plan = an::planBilateralFilter(
        an::BilateralRequest{.spatialSigma = params.sigmaSpace,
                             .rangeSigma = params.sigmaColor * process_utils::unitScaleOf(type).span,
                             .radius = windowRadiusOf(params)});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const process_utils::PlaneSelection selection(input);
    return process_utils::statusOf(an::filterImageBilateral(scope.execution(), type, input.samples, *input.layout,
                                                            output.samples, *output.layout, *plan,
                                                            selection.selection()));
}

an::Status BilateralFilterProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                           an::ui::ProcessDestination& destination,
                                                           const an::ui::ProcessRun& run,
                                                           const BilateralFilterParams& params,
                                                           an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling BilateralFilterProcess::previewScaling() const
{
    // The window and its spatial weights are sizes in pixels, which no rule scales to a reduction: an interactive frame
    // runs at full scale over the part of the target the preview view shows.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t BilateralFilterProcess::typedPreviewRegionMargin(const BilateralFilterParams& params) const
{
    // The window the engine filters with, the one it derives included; a sigma it cannot weigh by filters nothing.
    if (params.sigmaColor <= an::kMinimumBilateralSigma || params.sigmaSpace <= an::kMinimumBilateralSigma)
    {
        return 0u;
    }
    const an::Result<an::BilateralPlan> plan = an::planBilateralFilter(an::BilateralRequest{
        .spatialSigma = params.sigmaSpace, .rangeSigma = params.sigmaColor, .radius = windowRadiusOf(params)});
    return plan.hasValue() ? plan->radius() : 0u;
}

an::ui::ProcessStep BilateralFilterProcess::describeTypedProcessStep(const BilateralFilterParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Diameter: {:.2f}, Sigma Space: {}, Sigma Color: {}, Amount: {:.2f}",
                                               params.diameter, params.sigmaSpace, params.sigmaColor, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

double BilateralFilterProcess::getTypedModulationFromParams(const BilateralFilterParams& params)
{
    return params.amount;
}

BilateralFilterParams BilateralFilterProcess::getParams() const
{
    BilateralFilterParams params;
    params.linkSigmaControls = m_ui.linkSigmaControls->isChecked();
    params.diameter = diameterControl->getValue();
    params.sigmaSpace = sigmaSpaceControl->getValue();
    params.sigmaColor = sigmaColorControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
