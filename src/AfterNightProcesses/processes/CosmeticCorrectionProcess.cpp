// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CosmeticCorrectionProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpatialProcessSupport.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>

CosmeticCorrectionProcess::CosmeticCorrectionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<CosmeticCorrectionParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildCosmeticCorrectionProcessUi(this);

    setProcessDescription(QStringLiteral("Remove hot and cold pixels using a cosmetic correction filter."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector,
                              /*supportPerChannel=*/true,
                              /*showWaitingSpinner=*/true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    QObject::connect(m_ui.kernelSize, &QComboBox::currentTextChanged, this,
                     &CosmeticCorrectionProcess::on_kernelSize_currentTextChanged);

    QObject::connect(m_ui.hotPixelsGroupBox, &QGroupBox::clicked, this,
                     &CosmeticCorrectionProcess::on_hotPixelsGroupBoxClicked);
    QObject::connect(m_ui.coldPixelsGroupBox, &QGroupBox::clicked, this,
                     &CosmeticCorrectionProcess::on_coldPixelsGroupBoxClicked);

    INIT_D_SPINBOX_CTRL(hotPixelsThresholdControl, m_ui.hotPixelsThresholdValue, m_ui.hotPixelsThresholdSlider,
                        &CosmeticCorrectionProcess::on_hotPixelsThresholdChanged);
    INIT_D_SPINBOX_CTRL(coldPixelsThresholdControl, m_ui.coldPixelsThresholdValue, m_ui.coldPixelsThresholdSlider,
                        &CosmeticCorrectionProcess::on_coldPixelsThresholdChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider,
                        &CosmeticCorrectionProcess::on_amountChanged);

    updateWindowTitle();

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(600, 600), "Preview: Cosmetic Correction Filter");

    refreshTheme();
}

void CosmeticCorrectionProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string CosmeticCorrectionProcess::getProcessNameStatic()
{
    return "Cosmetic Correction";
}

bool CosmeticCorrectionProcess::requiresExclusiveAccessStatic()
{
    return false;
}

CosmeticCorrectionProcess::~CosmeticCorrectionProcess() {}

void CosmeticCorrectionProcess::on_kernelSize_currentTextChanged(const QString& text)
{
    settingsUpdated();
}

void CosmeticCorrectionProcess::on_hotPixelsThresholdChanged(double value)
{
    settingsUpdated();
}

void CosmeticCorrectionProcess::on_coldPixelsThresholdChanged(double value)
{
    settingsUpdated();
}

void CosmeticCorrectionProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void CosmeticCorrectionProcess::on_hotPixelsGroupBoxClicked(bool on)
{
    settingsUpdated();
}

void CosmeticCorrectionProcess::on_coldPixelsGroupBoxClicked(bool on)
{
    settingsUpdated();
}

bool CosmeticCorrectionProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void CosmeticCorrectionProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void CosmeticCorrectionProcess::initialize()
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

ProgressDialogControl* CosmeticCorrectionProcess::getProgressDialogControl(const bool forSequence)
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
    else if (selection == 6) // "15x15"
    {
        return 15;
    }

    return 3;
}

an::Status CosmeticCorrectionProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                   const an::ChannelScopeOutput& output,
                                                                   const an::ui::ProcessRun& run,
                                                                   const CosmeticCorrectionParams& params,
                                                                   an::ui::ExecutionScope& scope)
{
    // A sample departing from its window's median by more than the threshold, as a fraction of itself (a hot one) or
    // of the median (a cold one), takes the median, over a window the image's edge truncates. A threshold of one asks
    // for a departure no sample of an image with no negative samples makes, so that side is left off; the engine
    // takes thresholds below one.
    const bool hot = params.hotPixelsGroupBox && params.hotPixelsThreshold < 1.0;
    const bool cold = params.coldPixelsGroupBox && params.coldPixelsThreshold < 1.0;
    if (!hot && !cold)
    {
        return an::Status::success();
    }
    const auto radius = static_cast<std::uint32_t>((std::max)(1, (params.kernelSize - 1) / 2));
    const an::ImpulseNoiseRequest request{.radius = radius,
                                          .rejectHigh = hot,
                                          .highThreshold = hot ? params.hotPixelsThreshold : 0.5,
                                          .rejectLow = cold,
                                          .lowThreshold = cold ? params.coldPixelsThreshold : 0.5,
                                          .borders = {.border = an::KernelBorderMode::NotApplicable}};
    const process_utils::PlaneSelection selection(input, process_utils::NonFiniteSamples::MeanNothing);
    return process_utils::statusOf(an::rejectImageImpulseNoise(scope.execution(), input.descriptor->sampleType(),
                                                               input.samples, *input.layout, output.samples,
                                                               *output.layout, request, selection.selection()));
}

an::Status CosmeticCorrectionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                              an::ui::ProcessDestination& destination,
                                                              const an::ui::ProcessRun& run,
                                                              const CosmeticCorrectionParams& params,
                                                              an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling CosmeticCorrectionProcess::previewScaling() const
{
    // A sample is held to its window's median, a window of a size in pixels that no rule scales to a reduction: an
    // interactive frame runs at full scale over the part of the target the preview view shows.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t CosmeticCorrectionProcess::typedPreviewRegionMargin(const CosmeticCorrectionParams& params) const
{
    // One window around each pixel.
    return static_cast<std::uint32_t>((std::max)(1, (params.kernelSize - 1) / 2));
}

an::ui::ProcessStep CosmeticCorrectionProcess::describeTypedProcessStep(const CosmeticCorrectionParams& params) const
{
    std::string description;
    if (params.hotPixelsGroupBox)
    {
        description += fmt::format("Hot Pixels Threshold {}, ", params.hotPixelsThreshold);
    }
    if (params.coldPixelsGroupBox)
    {
        description += fmt::format("Cold Pixels Threshold {}, ", params.coldPixelsThreshold);
    }
    description +=
        fmt::format("Kernel size {}, Amount {}", m_ui.kernelSize->currentText().toUtf8().toStdString(), params.amount);
    description += process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector);
    return an::ui::ProcessStep{.name = getProcessName(), .description = std::move(description)};
}

void CosmeticCorrectionProcess::resetSettings()
{
    amountControl->setValue(1.0);
    coldPixelsThresholdControl->setValue(0.5);
    hotPixelsThresholdControl->setValue(0.5);
    m_ui.kernelSize->setCurrentText("3x3");

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void CosmeticCorrectionProcess::settingsUpdatedImpl()
{
    if (m_ui.hotPixelsGroupBox->isChecked() || m_ui.coldPixelsGroupBox->isChecked())
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }
}

void CosmeticCorrectionProcess::enableProcessControlsImpl(bool enabled)
{
    if (m_ui.hotPixelsGroupBox->isChecked() || m_ui.coldPixelsGroupBox->isChecked())
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    }
    else
    {
        m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    amountControl->setEnabled(enabled);
    coldPixelsThresholdControl->setEnabled(enabled);
    hotPixelsThresholdControl->setEnabled(enabled);
    m_ui.kernelSize->setEnabled(enabled);
}

double CosmeticCorrectionProcess::getTypedModulationFromParams(const CosmeticCorrectionParams& params)
{
    return params.amount;
}

CosmeticCorrectionParams CosmeticCorrectionProcess::getParams() const
{
    CosmeticCorrectionParams params;
    params.kernelSize = get_kernel_size_from_selection(m_ui.kernelSize->currentIndex());

    params.hotPixelsGroupBox = m_ui.hotPixelsGroupBox->isChecked();
    params.coldPixelsGroupBox = m_ui.coldPixelsGroupBox->isChecked();

    params.hotPixelsThreshold = hotPixelsThresholdControl->getValue();
    params.coldPixelsThreshold = coldPixelsThresholdControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
