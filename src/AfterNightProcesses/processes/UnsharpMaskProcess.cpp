// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "UnsharpMaskProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <algorithm>

namespace
{

/// The filter the dialog asks for at a radius and an amount, in a frame of a kind: the source plus the amount times
/// its difference from its Gaussian blur, the blur mirroring the edge sample as scikit-image's does; an interactive
/// frame blurs a radius beyond one kernel's reach by the real-time preview's boxes, and every other run by the exact
/// Gaussian.
[[nodiscard]] process_utils::DetailRequest unsharpMaskRequest(double radius, double amount,
                                                              an::ui::PreviewFrameKind kind)
{
    return process_utils::DetailRequest{.method = an::DetailFilterMethod::UnsharpMask,
                                        .sigma = radius,
                                        .amount = amount,
                                        .border = an::KernelBorderMode::Reflect,
                                        .blur = kind == an::ui::PreviewFrameKind::Interactive
                                                    ? process_utils::DetailBlur::InteractivePreview
                                                    : process_utils::DetailBlur::Exact};
}

} // namespace

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
    // at the edge. Over the target reduced by the preview view's factor, the blur's radius is reduced with it.
    const double radius = params.radius / static_cast<double>((std::max)(1u, run.proxyFactor));
    return process_utils::filterDetail(scope.engine(), scope.execution(), input, output,
                                       unsharpMaskRequest(radius, params.amount, run.frameKind));
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
    // An interactive frame runs over what the preview view paints. Over the target reduced by the view's factor the
    // body blurs by the radius reduced with it; the clamp to full scale keeps the result from commuting with the
    // reduction, so such a frame comes close to the reduction of the whole result rather than being it. Over the part
    // of the target the view shows at full scale it blurs by the dialog's radius. The settled frame runs over the whole
    // image as an Apply does.
    return an::ui::PreviewScaling::Decimated;
}

std::uint32_t UnsharpMaskProcess::typedPreviewRegionMargin(const UnsharpMaskParams& params) const
{
    // The reach of the blur an interactive frame at full scale takes the mask from: one kernel's, or the boxes' beyond
    // it; a radius too narrow for a kernel blurs nothing.
    if (!(params.radius > an::kMinimumGaussianSigma))
    {
        return 0u;
    }
    return process_utils::detailReach(
        unsharpMaskRequest(params.radius, params.amount, an::ui::PreviewFrameKind::Interactive));
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
