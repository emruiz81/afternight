// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IntegerResampleProcess.h"
#include "ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <limits>

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

IntegerResampleProcess::IntegerResampleProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);

    m_ui = an::ui::processes::buildIntegerResampleProcessUi(this);
    setProcessDescription(QStringLiteral("Upsample or downsample by an integer factor with selectable binning mode."));
    m_ui.groupBox_2->setFlat(true);
    m_ui.groupBox->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &IntegerResampleProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.resampleFactor, &QSpinBox::valueChanged, this,
                     &IntegerResampleProcess::on_resampleFactorChanged);

    QObject::connect(m_ui.downSampleRadioButton, &QRadioButton::clicked, this,
                     &IntegerResampleProcess::on_downsampleRadioButtonClicked);
    QObject::connect(m_ui.UpsampleRadioButton, &QRadioButton::clicked, this,
                     &IntegerResampleProcess::on_upsampleRadioButtonClicked);

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

std::string IntegerResampleProcess::getProcessNameStatic()
{
    return "Integer Resample";
}

bool IntegerResampleProcess::requiresExclusiveAccessStatic()
{
    return false;
}

IntegerResampleProcess::Settings IntegerResampleProcess::settings() const
{
    return Settings{.factor = static_cast<std::uint32_t>(m_ui.resampleFactor->value()),
                    .enlarge = m_ui.UpsampleRadioButton->isChecked(),
                    .reductionLabel = m_ui.downsampleMode->currentText(),
                    .width = static_cast<std::uint32_t>(m_ui.targetWidth->value()),
                    .height = static_cast<std::uint32_t>(m_ui.targetHeight->value())};
}

std::string IntegerResampleProcess::stepDescription(const Settings& settings)
{
    if (settings.enlarge)
    {
        return fmt::format("Resample factor: {}X - New size: {}x{}", settings.factor, settings.width, settings.height);
    }
    return fmt::format("Binning factor: {}X - New size: {}x{} - Binning method: {}", settings.factor, settings.width,
                       settings.height, settings.reductionLabel.toStdString());
}

void IntegerResampleProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    // The readouts, the target size, the estimates and Ok follow the image the view now shows, so the step the history
    // records names the size the run makes.
    refreshSourceImageData();
    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();
}

void IntegerResampleProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    initialize();
}

void IntegerResampleProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

IntegerResampleProcess::~IntegerResampleProcess() {}

void IntegerResampleProcess::on_resampleFactorChanged(int)
{
    updateResampleFactor();
    recalculateNewSize();
    checkChangesPending();
}

void IntegerResampleProcess::on_downsampleRadioButtonClicked(bool checked)
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

void IntegerResampleProcess::on_upsampleRadioButtonClicked(bool checked)
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

void IntegerResampleProcess::initialize()
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

void IntegerResampleProcess::updateResampleFactor()
{
    const std::int64_t factor = m_ui.resampleFactor->value();
    std::int64_t newWidth = m_ui.originalWidth->value();
    std::int64_t newHeight = m_ui.originalHeight->value();

    if (m_ui.downSampleRadioButton->isChecked())
    {
        newWidth /= factor;
        newHeight /= factor;
    }
    else
    {
        newWidth *= factor;
        newHeight *= factor;
    }

    // An upsample past what a readout holds is shown at its largest; the engine refuses such an extent anyway.
    constexpr std::int64_t kLargest = std::numeric_limits<int>::max();
    m_ui.targetWidth->setValue(static_cast<int>((std::min)(newWidth, kLargest)));
    m_ui.targetHeight->setValue(static_cast<int>((std::min)(newHeight, kLargest)));
}

void IntegerResampleProcess::refreshSourceImageData()
{
    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.originalWidth->setValue(width);
    m_ui.originalHeight->setValue(height);

    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double bytes = described.has_value() ? static_cast<double>(an::ui::processes::imageBytesOf(*described)) : 0.0;
    m_ui.currentSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void IntegerResampleProcess::recalculateNewSize()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double pixelBytes = described.has_value() ? static_cast<double>(described->extent().channelCount()) *
                                                          an::sampleTypeSizeBytes(described->sampleType())
                                                    : 0.0;
    const double bytes = static_cast<double>(m_ui.targetWidth->value()) * m_ui.targetHeight->value() * pixelBytes;
    m_ui.newSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void IntegerResampleProcess::checkChangesPending()
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

void IntegerResampleProcess::executeImageResample()
{
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    bool mosaicAsGrey = false;
    if (m_image_view_control->isCFA())
    {
        int ret = QMessageBox::No;
        process_utils::messageBoxYesNo(
            this, "Integer Resample CFA",
            "The file appears to have a bayer matrix. Running this process in a CFA matrix will produce undesired "
            "results. Please consider running a debayer process first, or run the CFA Integer Resample process "
            "instead.\nDo you want to run this process anyway?",
            true, &ret);

        if (ret == QMessageBox::No)
        {
            return;
        }
        mosaicAsGrey = true;
    }

    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const an::IntegerResampleRequest resample = an::ui::processes::integerResampleRequest(
        asked.factor, asked.enlarge, an::ui::processes::integerReductionFromLabel(asked.reductionLabel));

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Integer Resample"),
        .progressText = QString::fromStdString(
            fmt::format("Executing integer resample to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running integer resample"),
        .errorLead = "An error occurred when executing the integer resample process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [resample, mosaicAsGrey](const an::ui::ExecutionScope& scope,
                                                 const an::ui::ProcessSource& source) {
        const std::array<an::ui::processes::GeometryStep, 1u> steps{resample};
        return an::ui::processes::transformImage(scope, source, steps, "Integer Resample", mosaicAsGrey);
    };
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<IntegerResampleProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void IntegerResampleProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
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
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
    checkChangesPending();
}

void IntegerResampleProcess::on_dialog_box_button_clicked(QAbstractButton* button)
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
