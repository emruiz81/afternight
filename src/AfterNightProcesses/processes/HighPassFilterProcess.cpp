// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "HighPassFilterProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <algorithm>

namespace
{

/// The filter the dialog asks for at a radius, in a frame of a kind: an interactive frame blurs a radius beyond one
/// kernel's reach by the real-time preview's boxes, and every other run by the exact Gaussian.
[[nodiscard]] process_utils::DetailRequest highPassRequest(double radius, an::ui::PreviewFrameKind kind)
{
    return process_utils::DetailRequest{.method = an::DetailFilterMethod::HighPass,
                                        .sigma = radius,
                                        .offset = 0.5,
                                        .blur = kind == an::ui::PreviewFrameKind::Interactive
                                                    ? process_utils::DetailBlur::InteractivePreview
                                                    : process_utils::DetailBlur::Exact};
}

} // namespace

HighPassFilterProcess::HighPassFilterProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<HighPassFilterProcessParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildHighPassFilterProcessUi(this);
    setProcessDescription(
        QStringLiteral("Extract and emphasize high-frequency detail using an adjustable high-pass radius."));

    updateWindowTitle();

    INIT_D_SPINBOX_CTRL(radiusControl, m_ui.radiusValue, m_ui.radiusSlider, &HighPassFilterProcess::on_radiusChanged);

    m_FixedLayout = false;
    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, false);
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setupPreviewControl(false);
    settingsUpdated();
}

std::string HighPassFilterProcess::getProcessNameStatic()
{
    return "High Pass Filter";
}

bool HighPassFilterProcess::requiresExclusiveAccessStatic()
{
    return false;
}

HighPassFilterProcess::~HighPassFilterProcess() {}

void HighPassFilterProcess::on_radiusChanged(double value)
{
    settingsUpdated();
}

bool HighPassFilterProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void HighPassFilterProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status HighPassFilterProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                               const an::ChannelScopeOutput& output,
                                                               const an::ui::ProcessRun& run,
                                                               const HighPassFilterProcessParams& params,
                                                               an::ui::ExecutionScope& scope)
{
    // The source less its Gaussian blur of the dialog's radius, raised to mid-grey and held to full scale. Over the
    // target reduced by the preview view's factor, the blur's radius is reduced with it.
    const double radius = params.radius / static_cast<double>((std::max)(1u, run.proxyFactor));
    return process_utils::filterDetail(scope.engine(), scope.execution(), input, output,
                                       highPassRequest(radius, run.frameKind));
}

an::Status HighPassFilterProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                          an::ui::ProcessDestination& destination,
                                                          const an::ui::ProcessRun& run,
                                                          const HighPassFilterProcessParams& params,
                                                          an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling HighPassFilterProcess::previewScaling() const
{
    // An interactive frame runs over what the preview view paints. Over the target reduced by the view's factor the
    // body blurs by the radius reduced with it; the clamp to full scale keeps the result from commuting with the
    // reduction, so such a frame comes close to the reduction of the whole result rather than being it. Over the part
    // of the target the view shows at full scale it blurs by the dialog's radius. The settled frame runs over the whole
    // image as an Apply does.
    return an::ui::PreviewScaling::Decimated;
}

std::uint32_t HighPassFilterProcess::typedPreviewRegionMargin(const HighPassFilterProcessParams& params) const
{
    // The reach of the blur an interactive frame at full scale takes away: one kernel's, or the boxes' beyond it.
    return process_utils::detailReach(highPassRequest(params.radius, an::ui::PreviewFrameKind::Interactive));
}

an::ui::ProcessStep HighPassFilterProcess::describeTypedProcessStep(const HighPassFilterProcessParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description = fmt::format("Apply high pass filter with radius {}", params.radius)};
}

void HighPassFilterProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    radiusControl->setValue(10.0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void HighPassFilterProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }
}

void HighPassFilterProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    radiusControl->setEnabled(enabled);
}

HighPassFilterProcessParams HighPassFilterProcess::getParams() const
{
    HighPassFilterProcessParams params;
    params.radius = radiusControl->getValue();

    return params;
}
