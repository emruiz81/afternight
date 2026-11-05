// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TightenEdgesProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>

TightenEdgesProcess::TightenEdgesProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<TightenEdgesParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildTightenEdgesProcessUi(this);
    setProcessDescription(
        QStringLiteral("Tighten edge transitions with value and modulation controls for selective sharpening."));

    INIT_D_SPINBOX_CTRL(valueControl, m_ui.valueValue, m_ui.valueSlider, &TightenEdgesProcess::on_valueChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &TightenEdgesProcess::on_amountChanged);

    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);
}

std::string TightenEdgesProcess::getProcessNameStatic()
{
    return "Tighten Edges";
}

bool TightenEdgesProcess::requiresExclusiveAccessStatic()
{
    return false;
}

TightenEdgesProcess::~TightenEdgesProcess() {}

void TightenEdgesProcess::on_valueChanged(double value)
{
    settingsUpdated();
}

void TightenEdgesProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

an::Status TightenEdgesProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                             const an::ChannelScopeOutput& output,
                                                             const an::ui::ProcessRun& run,
                                                             const TightenEdgesParams& params,
                                                             an::ui::ExecutionScope& scope)
{
    // The source less the dialog's value times a twentieth of its four-direction Sobel magnitude, held to full scale;
    // a neighbour past the image's edge reads as zero.
    const process_utils::UnitScale unit = process_utils::unitScaleOf(input.descriptor->sampleType());
    const an::Result<an::EdgeTightenPlan> plan =
        an::planEdgeTighten(an::EdgeTightenRequest{.amount = static_cast<double>(static_cast<float>(params.value)),
                                                   .offset = 0.0,
                                                   .lowest = unit.offset,
                                                   .highest = unit.offset + unit.span});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const process_utils::PlaneSelection selection(input);
    return process_utils::statusOf(an::filterImageEdgeTighten(scope.execution(), input.descriptor->sampleType(),
                                                              input.samples, *input.layout, output.samples,
                                                              *output.layout, *plan, selection.selection()));
}

an::Status TightenEdgesProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                        an::ui::ProcessDestination& destination,
                                                        const an::ui::ProcessRun& run, const TightenEdgesParams& params,
                                                        an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling TightenEdgesProcess::previewScaling() const
{
    // The Sobel stencils read a pixel's eight neighbours, which a reduction would move: an interactive frame runs at
    // full scale over the part of the target the preview view shows.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t TightenEdgesProcess::typedPreviewRegionMargin(const TightenEdgesParams& /*params*/) const
{
    // The stencils' three by three window.
    return 1u;
}

an::ui::ProcessStep TightenEdgesProcess::describeTypedProcessStep(const TightenEdgesParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Adjust value to {}, amount {}", params.value, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool TightenEdgesProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void TightenEdgesProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void TightenEdgesProcess::resetSettings()
{
    valueControl->setValue(0.0);
    amountControl->setValue(1.0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void TightenEdgesProcess::settingsUpdatedImpl()
{
    if (valueControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void TightenEdgesProcess::enableProcessControlsImpl(bool enabled)
{
    if (valueControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    valueControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);
}

double TightenEdgesProcess::getTypedModulationFromParams(const TightenEdgesParams& params)
{
    return params.amount;
}

TightenEdgesParams TightenEdgesProcess::getParams() const
{
    TightenEdgesParams params;
    params.value = valueControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
