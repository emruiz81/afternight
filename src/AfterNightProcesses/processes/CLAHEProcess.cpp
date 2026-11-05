// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CLAHEProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"

#include <AfterNightEngine/Algorithms/Color/AdaptiveHistogramColor.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>

CLAHEProcess::CLAHEProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<CLAHEParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildCLAHEProcessUi(this);
    setProcessDescription(
        QStringLiteral("Improve local contrast with CLAHE using configurable tile grid and contrast limit."));

    updateWindowTitle();

    INIT_SPINBOX_CTRL(gridSizeControl, m_ui.gridSizeValue, m_ui.gridSizeSlider, &CLAHEProcess::on_gridSizeChanged);
    INIT_D_SPINBOX_CTRL(contrastLimitControl, m_ui.contrastLimitValue, m_ui.contrastLimitSlider,
                        &CLAHEProcess::on_contrastLimitChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &CLAHEProcess::on_amountChanged);

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    setupPreviewControl(false);

    settingsUpdated();
}

std::string CLAHEProcess::getProcessNameStatic()
{
    return "CLAHE";
}

bool CLAHEProcess::requiresExclusiveAccessStatic()
{
    return false;
}

CLAHEProcess::~CLAHEProcess() {}

void CLAHEProcess::on_gridSizeChanged(int value)
{
    settingsUpdated();
}

void CLAHEProcess::on_contrastLimitChanged(double value)
{
    settingsUpdated();
}

void CLAHEProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

an::Status CLAHEProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                      const an::ChannelScopeOutput& output,
                                                      const an::ui::ProcessRun& run, const CLAHEParams& params,
                                                      an::ui::ExecutionScope& scope)
{
    // A one-byte image is equalized over its 256 codes and every other one over 65,536 levels, and a colour image's CIE
    // L* alone, its chroma left where it was.
    const an::ImageDescriptor& described = *input.descriptor;
    const an::SampleType type = described.sampleType();
    const auto grid = static_cast<std::uint32_t>((std::max)(1, params.gridSize));
    const std::uint32_t bins = an::sampleTypeSizeBytes(type) == 1u ? 256u : 65536u;
    const bool colour =
        described.channelSchema() == an::ChannelSchema::Rgb || described.channelSchema() == an::ChannelSchema::Rgba;
    if (!colour)
    {
        const process_utils::UnitScale unit = process_utils::unitScaleOf(type);
        const an::Result<an::AdaptiveHistogramPlan> plan =
            an::planAdaptiveHistogramEqualization(an::AdaptiveHistogramRequest{.tileColumns = grid,
                                                                               .tileRows = grid,
                                                                               .binCount = bins,
                                                                               .clipLimit = params.contrastLimit,
                                                                               .lowest = unit.offset,
                                                                               .highest = unit.offset + unit.span});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const process_utils::PlaneSelection selection(input);
        return process_utils::statusOf(an::equalizeImageAdaptiveHistogram(scope.execution(), type, input.samples,
                                                                          *input.layout, output.samples, *output.layout,
                                                                          *plan, selection.selection()));
    }

    // The whole of a colour image: its red, green and blue - read as the process framework reads them, linear when
    // the image says so and sRGB-encoded otherwise - equalized in L* over the destination, which starts as the
    // source's planar copy. An alpha channel is left as it is.
    const an::ColorEncoding encoding =
        an::ui::processChannelScopeRequest(an::ChannelScope::CieLightness, described).sourceEncoding;
    const an::Result<an::PerceptualAdaptiveHistogramPlan> plan = an::planPerceptualAdaptiveHistogramEqualization(
        an::PerceptualAdaptiveHistogramRequest{.rgbEncoding = encoding,
                                               .tileColumns = grid,
                                               .tileRows = grid,
                                               .binCount = bins,
                                               .clipLimit = params.contrastLimit});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const std::optional<an::ImageLayout> rgb =
        process_utils::leadingPlanesLayout(*output.layout, 3u, scope.engine().limits());
    if (!rgb.has_value())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "a colour image's red, green and blue are equalized from planes of their own");
    }
    const process_utils::PlaneSelection selection(input);
    const process_utils::LeadingPlanesSelection rgbSelection(selection.selection(), 3u, rgb->extent());
    return process_utils::runOverUnitFloat(
        scope.engine(), scope.execution(), type, output.samples, *rgb,
        [&](float* samples, const an::ImageLayout& layout) {
            return process_utils::statusOf(
                an::equalizeImagePerceptualAdaptiveHistogram(scope.execution(), an::SampleType::Float32, samples,
                                                             layout, samples, layout, *plan, rgbSelection.selection()));
        });
}

an::Status CLAHEProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                 an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                                 const CLAHEParams& params, an::ui::ExecutionScope& scope)
{
    if (an::Status equalized = executeTypedProcessUsingChannels(source, destination, run, params, scope);
        equalized.failed())
    {
        return equalized;
    }
    // An equalized image is no longer linear, and its processing state says so. A preview is shown and never kept, so
    // it is spared the copy the record costs.
    const an::consumer::EngineSession* session = engineSession();
    if (run.kind == an::ui::ProcessRunKind::Preview || session == nullptr)
    {
        return an::Status::success();
    }
    return process_utils::markProcessResultNonlinear(*session, scope.engine(), scope.execution(), destination, "clahe");
}

an::ui::ProcessStep CLAHEProcess::describeTypedProcessStep(const CLAHEParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Grid Size: {}, Contrast Limit: {:.2f}, Amount: {:.2f}", params.gridSize,
                                               params.contrastLimit, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

bool CLAHEProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void CLAHEProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void CLAHEProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    gridSizeControl->setValue(8);
    contrastLimitControl->setValue(2.0);
    amountControl->setValue(1.0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void CLAHEProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }
}

void CLAHEProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    gridSizeControl->setEnabled(enabled);
    contrastLimitControl->setEnabled(enabled);
    amountControl->setEnabled(enabled);
}

double CLAHEProcess::getTypedModulationFromParams(const CLAHEParams& params)
{
    return params.amount;
}

CLAHEParams CLAHEProcess::getParams() const
{
    CLAHEParams params;
    params.gridSize = gridSizeControl->getValue();
    params.contrastLimit = contrastLimitControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
