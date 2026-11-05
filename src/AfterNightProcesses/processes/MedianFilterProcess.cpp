// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MedianFilterProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpatialProcessSupport.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>

MedianFilterProcess::MedianFilterProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<MedianFilterParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildMedianFilterProcessUi(this);

    setProcessDescription(QStringLiteral("Apply a median filter to remove noise while preserving edges."));

    QObject::connect(m_ui.kernelSize, &QComboBox::currentTextChanged, this,
                     &MedianFilterProcess::on_kernelSize_currentTextChanged);

    INIT_SPINBOX_CTRL(iterationsControl, m_ui.iterationsValue, m_ui.iterationsSlider,
                      &MedianFilterProcess::on_iterationsChanged);
    INIT_D_SPINBOX_CTRL(modulationControl, m_ui.modulationValue, m_ui.modulationSlider,
                        &MedianFilterProcess::on_modulationChanged);

    updateWindowTitle();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(600, 600), "Preview: Median Filter");

    refreshTheme();
}

void MedianFilterProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string MedianFilterProcess::getProcessNameStatic()
{
    return "Median Filter";
}

bool MedianFilterProcess::requiresExclusiveAccessStatic()
{
    return false;
}

MedianFilterProcess::~MedianFilterProcess() {}

void MedianFilterProcess::on_kernelSize_currentTextChanged(const QString& text)
{
    settingsUpdated();
}

void MedianFilterProcess::on_iterationsChanged(int value)
{
    settingsUpdated();
}

void MedianFilterProcess::on_modulationChanged(double value)
{
    settingsUpdated();
}

bool MedianFilterProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void MedianFilterProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void MedianFilterProcess::initialize()
{
    m_ui.kernelSize->addItem("3x3");
    m_ui.kernelSize->addItem("5x5");
    m_ui.kernelSize->addItem("7x7");
    m_ui.kernelSize->addItem("9x9");
    m_ui.kernelSize->addItem("11x11");
    m_ui.kernelSize->addItem("13x13");
    m_ui.kernelSize->addItem("15x15");

    m_ui.kernelSize->setCurrentText("3x3");
}

ProgressDialogControl* MedianFilterProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 100);
    progress->setValue(0);

    return progress;
}

static int get_kernel_size_from_selection(int selection)
{
    if (selection == 0) // "3x3"
    {
        return 3;
    }
    else if (selection == 1) // "5x5"
    {
        return 5;
    }
    else if (selection == 2) // "7x7"
    {
        return 7;
    }
    else if (selection == 3) // "9x9"
    {
        return 9;
    }
    else if (selection == 4) // "11x11"
    {
        return 11;
    }
    else if (selection == 5) // "13x13"
    {
        return 13;
    }
    else if (selection == 6) // 15x15"
    {
        return 15;
    }
    return 3;
}

an::Status MedianFilterProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                             const an::ChannelScopeOutput& output,
                                                             const an::ui::ProcessRun& run,
                                                             const MedianFilterParams& params,
                                                             an::ui::ExecutionScope& scope)
{
    // The median: a window truncated at the image's edge rather than reflected, the mean of the two middle samples
    // where the truncated window holds an even count, and the modulation blended on every pass.
    const auto radius = static_cast<std::uint32_t>((std::max)(0, (params.kernelSize - 1) / 2));
    const an::OrderFilterRequest request{.radiusX = radius,
                                         .radiusY = radius,
                                         .quantile = 0.5,
                                         .interpolation = an::QuantileInterpolation::Linear,
                                         .border = an::KernelBorderMode::NotApplicable,
                                         .amount = params.modulation,
                                         .iterations = static_cast<std::uint32_t>((std::max)(1, params.iterations))};
    const process_utils::PlaneSelection selection(input, process_utils::NonFiniteSamples::MeanNothing);
    return process_utils::statusOf(an::filterImageOrderStatistic(scope.execution(), input.descriptor->sampleType(),
                                                                 input.samples, *input.layout, output.samples,
                                                                 *output.layout, request, selection.selection()));
}

an::Status MedianFilterProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                        an::ui::ProcessDestination& destination,
                                                        const an::ui::ProcessRun& run, const MedianFilterParams& params,
                                                        an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling MedianFilterProcess::previewScaling() const
{
    // The window is a size in pixels, which no rule scales to a reduction, and a pass reads nothing beyond it: an
    // interactive frame runs at full scale over the part of the target the preview view shows.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t MedianFilterProcess::typedPreviewRegionMargin(const MedianFilterParams& params) const
{
    // Every pass reads its window around each pixel of the pass before.
    const auto radius = static_cast<std::uint32_t>((std::max)(0, (params.kernelSize - 1) / 2));
    return radius * static_cast<std::uint32_t>((std::max)(1, params.iterations));
}

an::ui::ProcessStep MedianFilterProcess::describeTypedProcessStep(const MedianFilterParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Kernel size {} iterations {} modulation {}",
                                               m_ui.kernelSize->currentText().toUtf8().toStdString(), params.iterations,
                                               static_cast<float>(params.modulation)) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

void MedianFilterProcess::resetSettings()
{
    iterationsControl->setValue(1);
    modulationControl->setValue(1.0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void MedianFilterProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

void MedianFilterProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    iterationsControl->setEnabled(enabled);
    modulationControl->setEnabled(enabled);

    m_ui.kernelSize->setEnabled(enabled);
}

MedianFilterParams MedianFilterProcess::getParams() const
{
    MedianFilterParams params;
    params.kernelSize = get_kernel_size_from_selection(m_ui.kernelSize->currentIndex());
    params.iterations = iterationsControl->getValue();
    params.modulation = modulationControl->getValue();

    return params;
}
