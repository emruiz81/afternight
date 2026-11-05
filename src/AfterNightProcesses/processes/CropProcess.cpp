// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CropProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
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

CropProcess::CropProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    m_ui = an::ui::processes::buildCropProcessUi(this);
    setProcessDescription(QStringLiteral("Trim the target image to a selected rectangular region."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &CropProcess::on_dialog_box_button_clicked);

    QObject::connect(m_ui.targetPosX, &QSpinBox::valueChanged, this, &CropProcess::on_targetPosXChanged);
    QObject::connect(m_ui.targetPosY, &QSpinBox::valueChanged, this, &CropProcess::on_targetPosYChanged);
    QObject::connect(m_ui.targetWidth, &QSpinBox::valueChanged, this, &CropProcess::on_targetWidthChanged);
    QObject::connect(m_ui.targetHeight, &QSpinBox::valueChanged, this, &CropProcess::on_targetHeightChanged);

    QObject::connect(m_ui.newSelectionButton, &QPushButton::clicked, this, &CropProcess::on_newSelectionClicked);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    m_CropController = std::make_unique<ImageCropController>(control->getScrollArea(), this);
    m_CropController->setListenerEnabled(true);
    m_CropController->enableOneClickMove(true);
    m_image_view_control->getViewWidget()->setImageViewSelectionStyle(ImageViewSelectionStyle::CROP_STYLE);

    initialize();
    refreshTheme();
}

std::string CropProcess::getProcessNameStatic()
{
    return "Crop";
}

bool CropProcess::requiresExclusiveAccessStatic()
{
    return true;
}

CropProcess::Settings CropProcess::settings() const
{
    return Settings{.x = static_cast<std::uint32_t>(m_ui.targetPosX->value()),
                    .y = static_cast<std::uint32_t>(m_ui.targetPosY->value()),
                    .width = static_cast<std::uint32_t>(m_ui.targetWidth->value()),
                    .height = static_cast<std::uint32_t>(m_ui.targetHeight->value())};
}

std::string CropProcess::stepDescription(const Settings& settings)
{
    return fmt::format("Cropped area : X: {} Y: {} W:{} H:{}", settings.x, settings.y, settings.width, settings.height);
}

void CropProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    refreshSourceImageData();
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void CropProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    m_CropController->destroy();
    m_CropController = std::make_unique<ImageCropController>(newCtrl->getScrollArea(), this);
    m_CropController->setListenerEnabled(true);
    m_CropController->enableOneClickMove(true);

    initialize();
}

void CropProcess::selectionAreaUpdated()
{
    m_IgnoreLastMoveEvent = true;
    auto selArea = m_image_view_control->getViewWidget()->getCurrentSelectionArea();
    m_ui.targetPosX->setValue(selArea.x());
    m_ui.targetPosY->setValue(selArea.y());
    m_ui.targetWidth->setValue(selArea.width());
    m_ui.targetHeight->setValue(selArea.height());

    m_IgnoreLastMoveEvent = false;
}

void CropProcess::selectionAreaChanged() {}

void CropProcess::newSelectionFinished()
{
    m_ui.newSelectionButton->setChecked(false);
}

void CropProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.newSelectionButton->setIcon(iconProvider->getIcon("crop"));
    }
}

CropProcess::~CropProcess() {}

void CropProcess::on_targetPosXChanged(int value)
{
    recalculateNewSize();
    checkChangesPending();

    m_ui.targetWidth->setMaximum(extentOf(m_image_view_control).first - value);
    updateSelectionArea();
}

void CropProcess::on_targetPosYChanged(int value)
{
    recalculateNewSize();
    checkChangesPending();

    m_ui.targetHeight->setMaximum(extentOf(m_image_view_control).second - value);
    updateSelectionArea();
}

void CropProcess::on_targetWidthChanged(int)
{
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void CropProcess::on_targetHeightChanged(int)
{
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void CropProcess::on_newSelectionClicked(bool checked)
{
    if (checked)
    {
        m_IgnoreLastMoveEvent = true;
        m_ui.targetPosX->setValue(0);
        m_ui.targetPosY->setValue(0);
        m_ui.targetWidth->setValue(0);
        m_ui.targetHeight->setValue(0);
        m_IgnoreLastMoveEvent = false;

        m_CropController->startNewSelection();
    }
    else
    {
        m_CropController->cancelNewSelection();
        initialize();
    }
}

void CropProcess::initialize()
{
    refreshSourceImageData();

    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.targetPosX->setValue(0);
    m_ui.targetPosY->setValue(0);
    m_ui.targetWidth->setValue(width);
    m_ui.targetHeight->setValue(height);

    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void CropProcess::refreshSourceImageData()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double bytes = described.has_value() ? static_cast<double>(an::ui::processes::imageBytesOf(*described)) : 0.0;
    m_ui.currentSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));

    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.targetPosX->setMaximum(width);
    m_ui.targetPosY->setMaximum(height);
    m_ui.targetWidth->setMaximum(width - m_ui.targetPosX->value());
    m_ui.targetHeight->setMaximum(height - m_ui.targetPosY->value());
}

void CropProcess::recalculateNewSize()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double pixelBytes = described.has_value() ? static_cast<double>(described->extent().channelCount()) *
                                                          an::sampleTypeSizeBytes(described->sampleType())
                                                    : 0.0;
    const double bytes = static_cast<double>(m_ui.targetWidth->value()) * m_ui.targetHeight->value() * pixelBytes;
    m_ui.newSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void CropProcess::checkChangesPending()
{
    auto* okBtn = m_ui.buttonBox->button(QDialogButtonBox::Ok);
    const auto [width, height] = extentOf(m_image_view_control);
    if (m_ExecutionInProgress)
    {
        okBtn->setEnabled(false);
    }
    else if (m_ui.targetWidth->value() == 0 || m_ui.targetHeight->value() == 0)
    {
        okBtn->setEnabled(false);
        okBtn->setToolTip(tr("Crop width and height must be greater than zero."));
    }
    else if (width != m_ui.targetWidth->value() || height != m_ui.targetHeight->value() ||
             m_ui.targetPosX->value() != 0 || m_ui.targetPosY->value() != 0)
    {
        okBtn->setEnabled(true);
        okBtn->setToolTip(tr("Apply the crop to the target image."));
    }
    else
    {
        okBtn->setEnabled(false);
        okBtn->setToolTip(tr("The crop region matches the full image. Adjust the region to enable."));
    }
}

void CropProcess::updateSelectionArea()
{
    if (!m_IgnoreLastMoveEvent)
        m_CropController->updateSelectionArea(QRect(m_ui.targetPosX->value(), m_ui.targetPosY->value(),
                                                    m_ui.targetWidth->value(), m_ui.targetHeight->value()));
}

void CropProcess::executeImageCrop()
{
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const an::CropRequest crop{.x = asked.x, .y = asked.y, .width = asked.width, .height = asked.height};

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Crop"),
        .progressText =
            QString::fromStdString(fmt::format("Executing crop to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running crop"),
        .errorLead = "An error occurred when executing the crop process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [crop](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
        const std::array<an::ui::processes::GeometryStep, 1u> steps{crop};
        return an::ui::processes::transformImage(scope, source, steps, "Crop");
    };
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<CropProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void CropProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
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

void CropProcess::closeEvent(QCloseEvent* event)
{
    m_CropController->destroy();
    ImageProcessControl::closeEvent(event);
}

void CropProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = m_ui.buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeImageCrop();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        if (m_ui.newSelectionButton->isChecked())
        {
            m_ui.newSelectionButton->setChecked(false);
            m_CropController->cancelNewSelection();
        }

        initialize();
    }
}
