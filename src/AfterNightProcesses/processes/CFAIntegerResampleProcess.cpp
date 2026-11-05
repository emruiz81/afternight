// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CFAIntegerResampleProcess.h"
#include "processes/CfaProcessSupport.h"
#include "ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"

#include <fmt/format.h>

namespace
{

/// The width and height of the image a view shows; zero for none.
[[nodiscard]] std::pair<int, int> extentOf(const ImageViewControl* view)
{
    const std::optional<an::ImageDescriptor> described = view != nullptr ? view->imageDescriptor() : std::nullopt;
    if (!described.has_value())
    {
        return {0, 0};
    }
    return {static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height())};
}

/// How many images a view's run reads: its sequence's frames, or one.
[[nodiscard]] double imageCountOf(ImageViewControl* view)
{
    return view->isSequence() ? static_cast<double>(view->getSequenceNumOfFrames()) : 1.0;
}

} // namespace

CFAIntegerResampleProcess::CFAIntegerResampleProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);

    m_ui = an::ui::processes::buildCFAIntegerResampleProcessUi(this);
    setProcessDescription(QStringLiteral("Resample CFA frames by integer factors while preserving the CFA pattern."));
    m_ui.groupBox_2->setFlat(true);
    m_ui.groupBox->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &CFAIntegerResampleProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.resampleFactor, &QSpinBox::valueChanged, this,
                     &CFAIntegerResampleProcess::on_resampleFactorChanged);

    QObject::connect(m_ui.downSampleRadioButton, &QRadioButton::clicked, this,
                     &CFAIntegerResampleProcess::on_downsampleRadioButtonClicked);
    QObject::connect(m_ui.UpsampleRadioButton, &QRadioButton::clicked, this,
                     &CFAIntegerResampleProcess::on_upsampleRadioButtonClicked);

    for (const QString& label : an::ui::processes::integerReductionLabels())
    {
        m_ui.downsampleMode->addItem(label);
    }

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();
    refreshTheme();
}

std::string CFAIntegerResampleProcess::getProcessNameStatic()
{
    return "CFA Integer Resample";
}

bool CFAIntegerResampleProcess::requiresExclusiveAccessStatic()
{
    return false;
}

CFAIntegerResampleProcess::Settings CFAIntegerResampleProcess::settings() const
{
    return Settings{.factor = static_cast<std::uint32_t>(m_ui.resampleFactor->value()),
                    .enlarge = m_ui.UpsampleRadioButton->isChecked(),
                    .reductionLabel = m_ui.downsampleMode->currentText(),
                    .width = static_cast<std::uint32_t>(m_ui.targetWidth->value()),
                    .height = static_cast<std::uint32_t>(m_ui.targetHeight->value())};
}

std::string CFAIntegerResampleProcess::stepDescription(const Settings& settings)
{
    if (settings.enlarge)
    {
        return fmt::format("Resample factor: {}X - New size: {}x{}", settings.factor, settings.width, settings.height);
    }
    return fmt::format("Binning factor: {}X - New size: {}x{} - Binning method: {}", settings.factor, settings.width,
                       settings.height, settings.reductionLabel.toStdString());
}

void CFAIntegerResampleProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    // The readouts, the target size, the estimates and Ok follow the image the view now shows.
    refreshSourceImageData();
    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();
}

void CFAIntegerResampleProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    initialize();
}

void CFAIntegerResampleProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

CFAIntegerResampleProcess::~CFAIntegerResampleProcess() {}

void CFAIntegerResampleProcess::on_resampleFactorChanged(int)
{
    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();
}

void CFAIntegerResampleProcess::on_downsampleRadioButtonClicked(bool checked)
{
    m_ui.UpsampleRadioButton->setChecked(!checked);
    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();

    if (checked)
    {
        m_ui.downsampleMode->setEnabled(true);
    }
}

void CFAIntegerResampleProcess::on_upsampleRadioButtonClicked(bool checked)
{
    m_ui.downSampleRadioButton->setChecked(!checked);
    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();

    if (checked)
    {
        m_ui.downsampleMode->setEnabled(false);
    }
}

void CFAIntegerResampleProcess::initialize()
{
    refreshSourceImageData();

    m_ui.resampleFactor->setValue(2);
    m_ui.UpsampleRadioButton->setChecked(false);
    m_ui.downSampleRadioButton->setChecked(true);
    m_ui.downsampleMode->setEnabled(true);

    m_ui.downsampleMode->setCurrentText(QStringLiteral("Average"));

    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();
}

void CFAIntegerResampleProcess::updateResampleFactor()
{
    int newWidth = 0;
    int newHeight = 0;

    if (m_ui.downSampleRadioButton->isChecked())
    {
        newWidth = m_ui.originalWidth->value() / m_ui.resampleFactor->value();
        newHeight = m_ui.originalHeight->value() / m_ui.resampleFactor->value();
    }
    else
    {
        newWidth = m_ui.originalWidth->value() * m_ui.resampleFactor->value();
        newHeight = m_ui.originalHeight->value() * m_ui.resampleFactor->value();
    }

    m_ui.targetWidth->setValue(newWidth);
    m_ui.targetHeight->setValue(newHeight);
}

void CFAIntegerResampleProcess::refreshSourceImageData()
{
    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.originalWidth->setValue(width);
    m_ui.originalHeight->setValue(height);

    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double bytes = described.has_value() ? static_cast<double>(an::ui::processes::imageBytesOf(*described)) : 0.0;
    m_ui.currentSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void CFAIntegerResampleProcess::recalculateNewSize()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double pixelBytes = described.has_value() ? static_cast<double>(described->extent().channelCount()) *
                                                          an::sampleTypeSizeBytes(described->sampleType())
                                                    : 0.0;
    const double bytes = static_cast<double>(m_ui.targetWidth->value()) * m_ui.targetHeight->value() * pixelBytes;
    m_ui.newSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void CFAIntegerResampleProcess::checkChangesPending()
{
    auto* okBtn = m_ui.buttonBox->button(QDialogButtonBox::Ok);
    const auto [width, height] = extentOf(m_image_view_control);
    if (m_ExecutionInProgress)
    {
        okBtn->setEnabled(false);
    }
    else if (width != m_ui.targetWidth->value() || height != m_ui.targetHeight->value())
    {
        okBtn->setEnabled(true);
        okBtn->setToolTip(tr("Apply integer resampling to the target image."));
    }
    else
    {
        okBtn->setEnabled(false);
        okBtn->setToolTip(tr("Target dimensions match the current image. Change the resample factor to enable."));
    }
}

void CFAIntegerResampleProcess::executeImageResample()
{
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    const an::ui::processes::GeometryRunWords words{
        .title = QStringLiteral("CFA Integer Resample"),
        .progressText = QString::fromStdString(
            fmt::format("Executing CFA integer resample to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running integer resample"),
        .errorLead = "An error occurred when executing the integer resample process"};
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    if (an::Status mosaic = an::ui::processes::checkBayerMosaic(described); mosaic.failed())
    {
        an::ui::processes::reportRunFailure(this, words, mosaic);
        return;
    }

    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    std::vector<an::ui::processes::GeometryStep> steps;
    const std::uint32_t width = described->extent().width();
    const std::uint32_t height = described->extent().height();
    if (width % 2u != 0u || height % 2u != 0u)
    {
        steps.emplace_back(
            an::CropRequest{.x = 0u, .y = 0u, .width = width - width % 2u, .height = height - height % 2u});
    }
    steps.emplace_back(an::ui::processes::CfaIntegerResampleStep{
        .request = an::ui::processes::integerResampleRequest(
            asked.factor, asked.enlarge, an::ui::processes::integerReductionFromLabel(asked.reductionLabel))});

    m_ExecutionInProgress = true;
    // The run's finish reads the view the selector names, so the target is held until the run lands.
    enableProcessControls(false);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = words;
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [steps](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
        return an::ui::processes::transformImage(scope, source, steps, "CFA Integer Resample");
    };
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<CFAIntegerResampleProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void CFAIntegerResampleProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if (outcome.published || m_ProcessCloseRequested)
    {
        close();
        return;
    }
    enableProcessControls(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
    checkChangesPending();
}

bool CFAIntegerResampleProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return ctrl->isCFA() && ctrl->isMono();
}

void CFAIntegerResampleProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeImageResample();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        initialize();
    }
}
