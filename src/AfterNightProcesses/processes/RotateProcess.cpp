// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RotateProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "processes/GeometryProcessSupport.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>

#include <fmt/format.h>

#include <tuple>

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

/// The canvas a rotation of the image a view shows makes: the engine's plan of it, which is what the run writes, or the
/// image's own extent when the plan cannot be made.
[[nodiscard]] std::pair<int, int> rotatedCanvasOf(const ImageViewControl* view, double degrees, bool expandCanvas)
{
    const std::optional<an::ImageDescriptor> described = view != nullptr ? view->imageDescriptor() : std::nullopt;
    if (!described.has_value())
    {
        return {0, 0};
    }
    // The interpolation decides the samples and not the canvas.
    const an::Result<an::GeometryPlan> plan = an::ui::processes::planGeometryStep(
        *described, an::ui::processes::rotationRequest(degrees, expandCanvas, an::KernelInterpolation::Linear));
    if (!plan.hasValue())
    {
        return extentOf(view);
    }
    const an::ImageExtent& canvas = plan->destinationDescriptor().extent();
    return {static_cast<int>(canvas.width()), static_cast<int>(canvas.height())};
}

/// How many images a view's run reads: its sequence's frames, or one.
[[nodiscard]] double imageCountOf(ImageViewControl* view)
{
    return view->isSequence() ? static_cast<double>(view->getSequenceNumOfFrames()) : 1.0;
}

/// The bytes one pixel of the image a view shows holds.
[[nodiscard]] double pixelBytesOf(const ImageViewControl* view)
{
    const std::optional<an::ImageDescriptor> described = view->imageDescriptor();
    return described.has_value() ? static_cast<double>(described->extent().channelCount()) *
                                       an::sampleTypeSizeBytes(described->sampleType())
                                 : 0.0;
}

} // namespace

RotateProcess::RotateProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);

    m_ui = an::ui::processes::buildRotateProcessUi(this);
    setProcessDescription(QStringLiteral("Rotate the image and optionally crop or resize the resulting frame."));
    m_ui.groupBoxRot->setFlat(true);
    m_ui.groupBox->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &RotateProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.groupBox, &QGroupBox::toggled, this, &RotateProcess::on_CropChanged);
    QObject::connect(m_ui.resizeImage, &QCheckBox::checkStateChanged, this, &RotateProcess::on_ResizeImageChanged);
    QObject::connect(m_ui.targetPosX, &QSpinBox::valueChanged, this, &RotateProcess::on_targetPosXChanged);
    QObject::connect(m_ui.targetPosY, &QSpinBox::valueChanged, this, &RotateProcess::on_targetPosYChanged);
    QObject::connect(m_ui.targetWidth, &QSpinBox::valueChanged, this, &RotateProcess::on_targetWidthChanged);
    QObject::connect(m_ui.targetHeight, &QSpinBox::valueChanged, this, &RotateProcess::on_targetHeightChanged);

    INIT_D_SPINBOX_CTRL(rotationAngleControl, m_ui.rotationAngle, m_ui.rotationAngleSlider,
                        &RotateProcess::on_rotationAngleChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    m_CropController = std::make_unique<ImageCropController>(control->getScrollArea(), this);
    m_CropController->setListenerEnabled(false);

    an::ui::processes::fillInterpolationChoices(m_ui.interpolation, an::ui::processes::rotationAutoInterpolationText());

    m_image_view_control->getViewWidget()->enableRotationPreview(true);
    m_image_view_control->getViewWidget()->setRotationPreviewAngle(0.0);
    m_image_view_control->getViewWidget()->setRotationPreviewResizeArea(m_ui.resizeImage->isChecked());

    initialize();
    setCropEnabled(false);
    refreshTheme();
}

std::string RotateProcess::getProcessNameStatic()
{
    return "Rotate";
}

bool RotateProcess::requiresExclusiveAccessStatic()
{
    return true;
}

void RotateProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    refreshSourceImageData();
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void RotateProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    oldCtrl->getViewWidget()->setRotationPreviewAngle(0.0);
    oldCtrl->getViewWidget()->setRotationPreviewResizeArea(false);
    oldCtrl->getViewWidget()->enableRotationPreview(false);

    initialize();
    setCropEnabled(false);

    m_CropController->destroy();
    m_CropController = std::make_unique<ImageCropController>(newCtrl->getScrollArea(), this);
    m_CropController->setListenerEnabled(false);

    newCtrl->getViewWidget()->enableRotationPreview(true);
    newCtrl->getViewWidget()->setRotationPreviewAngle(0.0);
    newCtrl->getViewWidget()->setRotationPreviewResizeArea(m_ui.resizeImage->isChecked());

    refreshSourceImageData();
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

RotateProcess::Settings RotateProcess::settings() const
{
    Settings asked;
    asked.degrees = rotationAngleControl->getValue();
    asked.expandCanvas = m_ui.resizeImage->isChecked();
    asked.interpolation = an::ui::processes::resolveRotationInterpolation(
        an::ui::processes::selectedInterpolationName(m_ui.interpolation));
    if (m_ui.groupBox->isChecked())
    {
        // The rectangle is drawn over the preview of the turned canvas; the part of it the turned image holds is kept.
        asked.crop = an::CropRequest{.x = static_cast<std::uint32_t>(m_ui.targetPosX->value()),
                                     .y = static_cast<std::uint32_t>(m_ui.targetPosY->value()),
                                     .width = static_cast<std::uint32_t>(m_ui.targetWidth->value()),
                                     .height = static_cast<std::uint32_t>(m_ui.targetHeight->value()),
                                     .bounds = an::CropBoundsPolicy::ClipToSource};
    }
    return asked;
}

std::string RotateProcess::stepDescription(const Settings& settings)
{
    if (settings.crop.has_value())
    {
        return fmt::format("Rotated {} degrees - Cropped area : X: {} Y: {} W:{} H:{} - Interpolation: {}",
                           settings.degrees, settings.crop->x, settings.crop->y, settings.crop->width,
                           settings.crop->height, settings.interpolation.label.toStdString());
    }
    return fmt::format("Rotated {} degrees - Interpolation: {}", settings.degrees,
                       settings.interpolation.label.toStdString());
}

void RotateProcess::selectionAreaUpdated()
{
    m_IgnoreLastMoveEvent = true;
    auto selArea = m_image_view_control->getViewWidget()->getCurrentSelectionArea();
    m_ui.targetPosX->setValue(selArea.x());
    m_ui.targetPosY->setValue(selArea.y());
    m_ui.targetWidth->setValue(selArea.width());
    m_ui.targetHeight->setValue(selArea.height());

    m_IgnoreLastMoveEvent = false;
}

void RotateProcess::selectionAreaChanged() {}

void RotateProcess::newSelectionFinished() {}

void RotateProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

RotateProcess::~RotateProcess() {}

void RotateProcess::on_rotationAngleChanged(double value)
{
    m_image_view_control->getViewWidget()->setRotationPreviewAngle(value);
    refreshSourceImageData();
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void RotateProcess::on_CropChanged(bool checked)
{
    setCropEnabled(checked);
}

void RotateProcess::on_ResizeImageChanged(Qt::CheckState state)
{
    m_image_view_control->getViewWidget()->setRotationPreviewResizeArea(state == Qt::Checked);

    refreshSourceImageData();
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void RotateProcess::on_targetPosXChanged(int value)
{
    recalculateNewSize();
    checkChangesPending();

    if (m_ui.resizeImage->isChecked() && this->rotationAngleControl->getValue() != 0.0)
    {
        m_ui.targetWidth->setMaximum(
            rotatedCanvasOf(m_image_view_control, this->rotationAngleControl->getValue(), true).first - value);
    }
    else
    {
        m_ui.targetWidth->setMaximum(extentOf(m_image_view_control).first - value);
    }

    updateSelectionArea();
}

void RotateProcess::on_targetPosYChanged(int value)
{
    recalculateNewSize();
    checkChangesPending();

    if (m_ui.resizeImage->isChecked() && this->rotationAngleControl->getValue() != 0.0)
    {
        m_ui.targetHeight->setMaximum(
            rotatedCanvasOf(m_image_view_control, this->rotationAngleControl->getValue(), true).second - value);
    }
    else
    {
        m_ui.targetHeight->setMaximum(extentOf(m_image_view_control).second - value);
    }

    updateSelectionArea();
}

void RotateProcess::on_targetWidthChanged(int)
{
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void RotateProcess::on_targetHeightChanged(int)
{
    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void RotateProcess::initialize()
{
    rotationAngleControl->setValue(0.0);
    refreshSourceImageData();

    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.targetPosX->setValue(0);
    m_ui.targetPosY->setValue(0);
    m_ui.targetWidth->setValue(width);
    m_ui.targetHeight->setValue(height);

    an::ui::processes::selectInterpolationName(m_ui.interpolation, process_utils::INTERP_AUTO);

    m_ui.resizeImage->setChecked(false);

    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
}

void RotateProcess::setCropEnabled(bool enabled)
{
    m_ui.groupBox->blockSignals(true);
    m_ui.groupBox->setChecked(enabled);
    m_ui.groupBox->blockSignals(false);

    recalculateNewSize();
    checkChangesPending();
    updateSelectionArea();
    m_CropController->setListenerEnabled(enabled);
    m_image_view_control->getViewWidget()->setImageViewSelectionStyle(ImageViewSelectionStyle::CROP_STYLE);
}

void RotateProcess::refreshSourceImageData()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double bytes = described.has_value() ? static_cast<double>(an::ui::processes::imageBytesOf(*described)) : 0.0;
    m_ui.currentSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));

    auto [canvas_width, canvas_height] = extentOf(m_image_view_control);
    if (m_ui.resizeImage->isChecked() && this->rotationAngleControl->getValue() != 0.0)
    {
        std::tie(canvas_width, canvas_height) =
            rotatedCanvasOf(m_image_view_control, this->rotationAngleControl->getValue(), true);
    }

    m_ui.targetPosX->setMaximum(canvas_width);
    m_ui.targetPosY->setMaximum(canvas_height);
    m_ui.targetWidth->setMaximum(canvas_width - m_ui.targetPosX->value());
    m_ui.targetHeight->setMaximum(canvas_height - m_ui.targetPosY->value());
}

void RotateProcess::recalculateNewSize()
{
    const double pixelSize = pixelBytesOf(m_image_view_control);
    double size = 0.0;
    if (m_ui.groupBox->isChecked())
    {
        size = m_ui.targetWidth->value() * m_ui.targetHeight->value() * pixelSize;
    }
    else
    {
        if (m_ui.resizeImage->isChecked() && this->rotationAngleControl->getValue() != 0.0 &&
            this->rotationAngleControl->getValue() != 180.0 && this->rotationAngleControl->getValue() != -180.0)
        {
            const auto [width, height] =
                rotatedCanvasOf(m_image_view_control, this->rotationAngleControl->getValue(), true);
            size = static_cast<double>(width) * height * pixelSize;
        }
        else
        {
            const auto [width, height] = extentOf(m_image_view_control);
            size = width * height * pixelSize;
        }
    }
    m_ui.newSize->setText(an::ui::processes::sizeText(size * imageCountOf(m_image_view_control)));
}

void RotateProcess::checkChangesPending()
{
    auto* okBtn = m_ui.buttonBox->button(QDialogButtonBox::Ok);
    const auto [width, height] = extentOf(m_image_view_control);

    bool rotationChanges = false;
    if (m_ui.groupBox->isChecked())
    {
        rotationChanges = (width != m_ui.targetWidth->value() || height != m_ui.targetHeight->value() ||
                           m_ui.targetPosX->value() != 0 || m_ui.targetPosY->value() != 0);
    }

    if (m_ExecutionInProgress)
    {
        okBtn->setEnabled(false);
    }
    else if (rotationAngleControl->getValue() != 0.0 || rotationChanges)
    {
        okBtn->setEnabled(true);
        okBtn->setToolTip(tr("Apply the rotation to the target image."));
    }
    else
    {
        okBtn->setEnabled(false);
        okBtn->setToolTip(tr("Set a non-zero rotation angle or adjust the crop region to enable."));
    }
}

void RotateProcess::updateSelectionArea()
{
    if (!m_IgnoreLastMoveEvent)
        m_CropController->updateSelectionArea(QRect(m_ui.targetPosX->value(), m_ui.targetPosY->value(),
                                                    m_ui.targetWidth->value(), m_ui.targetHeight->value()));
}

void RotateProcess::executeImageRotate()
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
            this, "Rotate CFA",
            "The file appears to have a bayer matrix. Running this process in a CFA matrix will produce undesired "
            "results. Please consider running a debayer process first, or do a fast rotate operation instead (rotate "
            "90 or 180 degrees).\nDo you want to run this process anyway?",
            true, &ret);

        if (ret == QMessageBox::No)
        {
            return;
        }
        mosaicAsGrey = true;
    }

    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    std::vector<an::ui::processes::GeometryStep> steps;
    steps.emplace_back(
        an::ui::processes::rotationRequest(asked.degrees, asked.expandCanvas, asked.interpolation.interpolation));
    if (asked.crop.has_value())
    {
        steps.emplace_back(*asked.crop);
    }

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Rotate"),
        .progressText = QString::fromStdString(
            fmt::format("Executing rotate to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running rotate"),
        .errorLead = "An error occurred when executing the rotate process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [steps, mosaicAsGrey](const an::ui::ExecutionScope& scope,
                                              const an::ui::ProcessSource& source) {
        return an::ui::processes::transformImage(scope, source, steps, "Rotate", mosaicAsGrey);
    };
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<RotateProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void RotateProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
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

void RotateProcess::closeEvent(QCloseEvent* event)
{
    m_image_view_control->getViewWidget()->enableRotationPreview(false);
    m_image_view_control->getViewWidget()->setRotationPreviewAngle(0.0);
    m_image_view_control->getViewWidget()->setRotationPreviewResizeArea(false);
    m_image_view_control->getViewWidget()->update();
    m_CropController->destroy();
    ImageProcessControl::closeEvent(event);
}

void RotateProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeImageRotate();
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
