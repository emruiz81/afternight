// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "UnsharpMaskProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

UnsharpMaskProcess::UnsharpMaskProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<UnsharpMaskParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildUnsharpMaskProcessUi(this);
    setProcessDescription(QStringLiteral("Sharpen image detail using unsharp mask radius and amount controls."));

    INIT_D_SPINBOX_CTRL(radiusControl, m_ui.radiusValue, m_ui.radiusSlider, &UnsharpMaskProcess::on_radiusChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &UnsharpMaskProcess::on_amountChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(1200, 1200), "Preview: Unsharp Mask");
    settingsUpdated();
}

std::string UnsharpMaskProcess::getProcessNameStatic()
{
    return "Unsharp Mask";
}

bool UnsharpMaskProcess::requiresExclusiveAccessStatic()
{
    return false;
}

UnsharpMaskProcess::~UnsharpMaskProcess() {}

void UnsharpMaskProcess::on_radiusChanged(double value)
{
    settingsUpdated();
}

void UnsharpMaskProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

an::Status UnsharpMaskProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                            const an::ChannelScopeOutput& output,
                                                            const an::ui::ProcessRun& run,
                                                            const UnsharpMaskParams& params,
                                                            an::ui::ExecutionScope& scope)
{
    // A radius too narrow to build a kernel for is no blur at all, and the plane stays as it starts.
    if (!(params.radius > an::kMinimumGaussianSigma))
    {
        return an::Status::success();
    }
    // The source plus the amount times its difference from its Gaussian blur, held to full scale, the blur reflecting
    // at the edge.
    return process_utils::filterDetail(scope.engine(), scope.execution(), input, output,
                                       process_utils::DetailRequest{.method = an::DetailFilterMethod::UnsharpMask,
                                                                    .sigma = params.radius,
                                                                    .amount = params.amount,
                                                                    .border = an::KernelBorderMode::Reflect});
}

an::Status UnsharpMaskProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                       an::ui::ProcessDestination& destination,
                                                       const an::ui::ProcessRun& run, const UnsharpMaskParams& params,
                                                       an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling UnsharpMaskProcess::previewScaling() const
{
    // The blur the mask is taken from reaches a radius in pixels, and the clamp to full scale keeps the result from
    // commuting with a reduction: an interactive frame runs at full scale over the part of the target the preview view
    // shows.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t UnsharpMaskProcess::typedPreviewRegionMargin(const UnsharpMaskParams& params) const
{
    // The reach of the blur the body takes the mask from; a radius too narrow for a kernel blurs nothing.
    if (!(params.radius > an::kMinimumGaussianSigma))
    {
        return 0u;
    }
    return process_utils::detailReach(process_utils::DetailRequest{.method = an::DetailFilterMethod::UnsharpMask,
                                                                   .sigma = params.radius,
                                                                   .amount = params.amount,
                                                                   .border = an::KernelBorderMode::Reflect});
}

an::ui::ProcessStep UnsharpMaskProcess::describeTypedProcessStep(const UnsharpMaskParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Radius: {}, amount: {}", params.radius, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool UnsharpMaskProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void UnsharpMaskProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void UnsharpMaskProcess::resetSettings()
{
    radiusControl->setValue(1.0);
    amountControl->setValue(1.0);

    process_utils::selectWholeImageChannel(m_image_view_control, m_ui.channelSelector);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* UnsharpMaskProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 0);
    progress->setValue(0);

    return progress;
}

void UnsharpMaskProcess::settingsUpdatedImpl()
{
    /*if (m_ui.radiusValue->value() == 1.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else*/
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void UnsharpMaskProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    radiusControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);
}

UnsharpMaskParams UnsharpMaskProcess::getParams() const
{
    UnsharpMaskParams params;
    params.radius = radiusControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
