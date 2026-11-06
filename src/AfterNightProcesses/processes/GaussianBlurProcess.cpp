// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GaussianBlurProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpatialProcessSupport.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

GaussianBlurProcess::GaussianBlurProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<GaussianBlurParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildGaussianBlurProcessUi(this);

    setProcessDescription(QStringLiteral("Apply a Gaussian blur to the image to reduce noise or smooth details."));

    INIT_D_SPINBOX_CTRL(sigmaControl, m_ui.sigmaValue, m_ui.sigmaSlider, &GaussianBlurProcess::on_sigmaChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);

    refreshTheme();
}

void GaussianBlurProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string GaussianBlurProcess::getProcessNameStatic()
{
    return "Gaussian Blur";
}

bool GaussianBlurProcess::requiresExclusiveAccessStatic()
{
    return false;
}

GaussianBlurProcess::~GaussianBlurProcess() {}

void GaussianBlurProcess::on_sigmaChanged(double value)
{
    settingsUpdated();
}

an::Status GaussianBlurProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                             const an::ChannelScopeOutput& output,
                                                             const an::ui::ProcessRun& run,
                                                             const GaussianBlurParams& params,
                                                             an::ui::ExecutionScope& scope)
{
    // The exact Gaussian in a settled frame as in an Apply. An interactive frame over the target reduced by the view's
    // factor blurs the reduction by the sigma reduced with it - a Gaussian commutes with the block averaging - so it
    // shows what the whole result's reduction shows; a frame over the region the view shows at full scale, and every
    // other run, blurs by the dialog's sigma. An interactive frame's sigma beyond the stencil family's widest kernel is
    // blurred by the real-time preview's boxes, whose cost does not grow with the sigma as the exact kernel's does.
    const double sigma = params.sigma / static_cast<double>((std::max)(1u, run.proxyFactor));
    // A sigma the engine cannot build a kernel for is no blur at all: the plane stays as it starts. Over a reduction
    // the sigma reduced with it can fall there while the dialog's own sigma, which the Apply blurs by, does not - a
    // dialog sigma above the engine's minimum but not above that minimum times the view's factor, under 0.004 pixels
    // at a four-fold reduction, whose blur no pixel of the reduction could show.
    if (!(sigma > an::kMinimumGaussianSigma))
    {
        return an::Status::success();
    }
    return process_utils::blurGaussian(
        scope.engine(), scope.execution(), input, output, sigma, an::KernelBorderMode::ReflectExcludingEdge,
        run.frameKind == an::ui::PreviewFrameKind::Interactive ? process_utils::BlurKind::InteractivePreview
                                                               : process_utils::BlurKind::Exact);
}

bool GaussianBlurProcess::isTypedIdentity(const GaussianBlurParams& params) const
{
    // A sigma the engine builds no kernel for leaves every sample as it is.
    return !(params.sigma > an::kMinimumGaussianSigma);
}

an::ui::PreviewScaling GaussianBlurProcess::previewScaling() const
{
    // The blur's reach scales with the image: over the target reduced the body blurs by the sigma reduced with it, and
    // over the region the view shows at full scale it reads the kernel's reach around the region.
    return an::ui::PreviewScaling::Decimated;
}

std::uint32_t GaussianBlurProcess::typedPreviewRegionMargin(const GaussianBlurParams& params) const
{
    // The reach of the blur an interactive frame at full scale runs: the kernel's, or the boxes' beyond the stencil
    // family's widest kernel.
    return process_utils::gaussianReach(params.sigma, an::KernelBorderMode::ReflectExcludingEdge,
                                        process_utils::BlurKind::InteractivePreview);
}

an::Status GaussianBlurProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                        an::ui::ProcessDestination& destination,
                                                        const an::ui::ProcessRun& run, const GaussianBlurParams& params,
                                                        an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep GaussianBlurProcess::describeTypedProcessStep(const GaussianBlurParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Adjust sigma to {}", params.sigma) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool GaussianBlurProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void GaussianBlurProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void GaussianBlurProcess::resetSettings()
{
    sigmaControl->setValue(0.0);

    process_utils::selectWholeImageChannel(m_image_view_control, m_ui.channelSelector);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void GaussianBlurProcess::settingsUpdatedImpl()
{
    if (sigmaControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
}

void GaussianBlurProcess::enableProcessControlsImpl(bool enabled)
{
    if (sigmaControl->getValue() == 0.0)
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    sigmaControl->setEnabled(enabled);
}

GaussianBlurParams GaussianBlurProcess::getParams() const
{
    GaussianBlurParams params;
    params.sigma = sigmaControl->getValue();

    return params;
}
