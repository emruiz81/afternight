// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BackgroundNeutralizationProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "processes/ProcessCommonUtils.h"

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

} // namespace

BackgroundNeutralizationProcess::BackgroundNeutralizationProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    setWindowSizeFitMultipliers(1.3, 1.0);

    m_ui = an::ui::processes::buildBackgroundNeutralizationProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Neutralize background color casts using sampled background references and optional ROI selection."));
    m_ui.backgroundRoiGroupBox->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &BackgroundNeutralizationProcess::on_dialog_box_button_clicked);

    QObject::connect(m_ui.backgroundReferenceImageSelector, &QComboBox::currentTextChanged, this,
                     &BackgroundNeutralizationProcess::on_backgroundReferenceImageSelector_currentTextChanged);
    QObject::connect(m_ui.backgroundRoiGroupBox, &QGroupBox::clicked, this,
                     &BackgroundNeutralizationProcess::on_backgroundRoiGroupBoxClicked);
    QObject::connect(m_ui.backgroundRoiSelectionButton, &QPushButton::clicked, this,
                     &BackgroundNeutralizationProcess::on_backgroundRoiSelectionButtonClicked);

    QObject::connect(m_ui.backgroundRoiLeftValue, &QSpinBox::valueChanged, this,
                     &BackgroundNeutralizationProcess::on_backgroundRoiLeftValueChanged);
    QObject::connect(m_ui.backgroundRoiTopValue, &QSpinBox::valueChanged, this,
                     &BackgroundNeutralizationProcess::on_backgroundRoiTopValueChanged);
    QObject::connect(m_ui.backgroundRoiWidthValue, &QSpinBox::valueChanged, this,
                     &BackgroundNeutralizationProcess::on_backgroundRoiWidthValueChanged);
    QObject::connect(m_ui.backgroundRoiHeightValue, &QSpinBox::valueChanged, this,
                     &BackgroundNeutralizationProcess::on_backgroundRoiHeightValueChanged);

    INIT_D_SPINBOX_CTRL(backgroundLowerLimitControl, m_ui.backgroundLowerLimitValue, m_ui.backgroundLowerLimitSlider,
                        &BackgroundNeutralizationProcess::on_backgroundLowerLimitChanged);
    INIT_D_SPINBOX_CTRL(backgroundUpperLimitControl, m_ui.backgroundUpperLimitValue, m_ui.backgroundUpperLimitSlider,
                        &BackgroundNeutralizationProcess::on_backgroundUpperLimitChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();

    refreshTheme();
}

std::string BackgroundNeutralizationProcess::getProcessNameStatic()
{
    return "Background Neutralization";
}

bool BackgroundNeutralizationProcess::requiresExclusiveAccessStatic()
{
    return true;
}

void BackgroundNeutralizationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void BackgroundNeutralizationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);
    updateSelectionArea();
}

void BackgroundNeutralizationProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    destroyCropController();
    initialize();
}

void BackgroundNeutralizationProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlCreated(ctrl);
    if (an::ui::processes::showsRgbImage(ctrl))
    {
        m_ui.backgroundReferenceImageSelector->addItem(QString::fromUtf8(ctrl->getUniqueName()));
        m_ui.backgroundReferenceImageSelector->setItemData(m_ui.backgroundReferenceImageSelector->count() - 1,
                                                           an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
    }
}

void BackgroundNeutralizationProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlDestroyed(ctrl);
    auto imageName = QString::fromUtf8(ctrl->getUniqueName());
    int idx = m_ui.backgroundReferenceImageSelector->findText(imageName);
    if (idx != -1)
    {
        if (m_ui.backgroundReferenceImageSelector->currentText() == imageName)
        {
            m_ui.backgroundReferenceImageSelector->setCurrentText(
                QString::fromUtf8(an::ui::processes::kTargetImageReference));
        }

        m_ui.backgroundReferenceImageSelector->removeItem(idx);
    }
}

void BackgroundNeutralizationProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                           const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlRenamed(newName, oldName, ctrl);
    auto newImageName = QString::fromUtf8(newName);
    auto oldImageName = QString::fromUtf8(oldName);

    int idx = m_ui.backgroundReferenceImageSelector->findText(oldImageName);
    if (idx != -1)
    {
        m_ui.backgroundReferenceImageSelector->setItemText(idx, newImageName);
        m_ui.backgroundReferenceImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl),
                                                           Qt::ToolTipRole);
    }
}

void BackgroundNeutralizationProcess::selectionAreaUpdated()
{
    m_IgnoreLastCropMoveEvent = true;
    if (curentCropStatus == 2)
    {
        auto targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText());
        if (!targetControl)
        {
            m_IgnoreLastCropMoveEvent = false;
            return;
        }
        auto selArea = targetControl->getViewWidget()->getCurrentSelectionArea();
        m_ui.backgroundRoiLeftValue->setValue(selArea.x());
        m_ui.backgroundRoiTopValue->setValue(selArea.y());
        m_ui.backgroundRoiWidthValue->setValue(selArea.width());
        m_ui.backgroundRoiHeightValue->setValue(selArea.height());
    }

    m_IgnoreLastCropMoveEvent = false;
}

void BackgroundNeutralizationProcess::selectionAreaChanged() {}

void BackgroundNeutralizationProcess::newSelectionFinished() {}

BackgroundNeutralizationProcess::~BackgroundNeutralizationProcess()
{
    destroyCropController();
}

void BackgroundNeutralizationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    if (!m_ui.buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = m_ui.buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        executeBackgroundNeutralization();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        this->initialize();
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

void BackgroundNeutralizationProcess::on_backgroundReferenceImageSelector_currentTextChanged(const QString&)
{
    if (m_ui.backgroundRoiGroupBox->isChecked())
    {
        updateBackgroundReferenceImageData();
        if (m_ui.backgroundRoiSelectionButton->isChecked())
        {
            destroyCropController();
            curentCropStatus = 0;
            m_ui.backgroundRoiSelectionButton->setChecked(false);
        }
    }
}

void BackgroundNeutralizationProcess::on_backgroundLowerLimitChanged(double) {}

void BackgroundNeutralizationProcess::on_backgroundUpperLimitChanged(double) {}

void BackgroundNeutralizationProcess::on_backgroundRoiGroupBoxClicked(bool on)
{
    if (on)
    {
        updateBackgroundReferenceImageData();
        if (m_ui.backgroundRoiSelectionButton->isChecked())
        {
            destroyCropController();
            curentCropStatus = 0;
            m_ui.backgroundRoiSelectionButton->setChecked(false);
        }
    }
    else
    {
        destroyCropController();
        curentCropStatus = 0;
        m_ui.backgroundRoiSelectionButton->setChecked(false);
        resetBackgroundRoiValues();
    }
}

void BackgroundNeutralizationProcess::on_backgroundRoiSelectionButtonClicked(bool checked)
{
    if (checked)
    {
        curentCropStatus = 2;

        auto targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText());
        if (!targetControl)
        {
            curentCropStatus = 0;
            m_ui.backgroundRoiSelectionButton->setChecked(false);
            return;
        }

        destroyCropController();

        m_CropController = std::make_unique<ImageCropController>(targetControl->getScrollArea(), this);
        m_CropController->setListenerEnabled(true);
        m_CropController->enableOneClickMove(true);

        resetBackgroundRoiValues();

        m_CropController->startNewSelection();

        targetControl->getViewWidget()->setImageViewSelectionStyle(ImageViewSelectionStyle::AREA_SELECTION_STYLE);

        updateSelectionArea();
    }
    else
    {
        destroyCropController();

        curentCropStatus = 0;

        if (m_ui.backgroundRoiWidthValue->value() == 0 || m_ui.backgroundRoiHeightValue->value() == 0)
        {
            updateBackgroundReferenceImageData();
        }
    }
}

void BackgroundNeutralizationProcess::on_backgroundRoiLeftValueChanged(int value)
{
    auto targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText());
    if (!targetControl)
    {
        return;
    }

    m_ui.backgroundRoiWidthValue->setMaximum(extentOf(targetControl).first - value);
    updateSelectionArea();
}

void BackgroundNeutralizationProcess::on_backgroundRoiTopValueChanged(int value)
{
    auto targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText());
    if (!targetControl)
    {
        return;
    }

    m_ui.backgroundRoiHeightValue->setMaximum(extentOf(targetControl).second - value);
    updateSelectionArea();
}

void BackgroundNeutralizationProcess::on_backgroundRoiWidthValueChanged(int)
{
    updateSelectionArea();
}

void BackgroundNeutralizationProcess::on_backgroundRoiHeightValueChanged(int)
{
    updateSelectionArea();
}

void BackgroundNeutralizationProcess::initialize()
{
    destroyCropController();
    curentCropStatus = 0;

    const QString target = QString::fromUtf8(an::ui::processes::kTargetImageReference);
    m_ui.backgroundReferenceImageSelector->clear();
    m_ui.backgroundReferenceImageSelector->addItem(target);
    m_ui.backgroundReferenceImageSelector->setItemData(m_ui.backgroundReferenceImageSelector->count() - 1,
                                                       an::ui::buildImageSelectorTooltip(m_image_view_control),
                                                       Qt::ToolTipRole);

    // The target is offered once, as the target entry.
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl != m_image_view_control && an::ui::processes::showsRgbImage(viewControl))
        {
            m_ui.backgroundReferenceImageSelector->addItem(QString::fromUtf8(viewControl->getUniqueName()));
            m_ui.backgroundReferenceImageSelector->setItemData(m_ui.backgroundReferenceImageSelector->count() - 1,
                                                               an::ui::buildImageSelectorTooltip(viewControl),
                                                               Qt::ToolTipRole);
        }
    }

    m_ui.backgroundReferenceImageSelector->setCurrentText(target);

    m_ui.backgroundRoiSelectionButton->setChecked(false);
    m_ui.backgroundRoiGroupBox->setChecked(false);
    resetBackgroundRoiValues();

    backgroundLowerLimitControl->setValue(0.0);
    backgroundUpperLimitControl->setValue(0.1);
}

BackgroundNeutralizationProcess::Settings BackgroundNeutralizationProcess::settings() const
{
    Settings asked;
    asked.referenceName = m_ui.backgroundReferenceImageSelector->currentText();
    asked.regionChecked = m_ui.backgroundRoiGroupBox->isChecked();
    asked.region = QRect(m_ui.backgroundRoiLeftValue->value(), m_ui.backgroundRoiTopValue->value(),
                         m_ui.backgroundRoiWidthValue->value(), m_ui.backgroundRoiHeightValue->value());
    asked.range = an::ui::processes::FractionRange{.lowest = backgroundLowerLimitControl->getValue(),
                                                   .highest = backgroundUpperLimitControl->getValue()};
    return asked;
}

std::string BackgroundNeutralizationProcess::stepDescription(const std::string& referenceName, const QRect& region)
{
    return fmt::format("Reference image '{}' (ROI X:{}, Y:{}, W:{}, H:{})", referenceName, region.x(), region.y(),
                       region.width(), region.height());
}

void BackgroundNeutralizationProcess::executeBackgroundNeutralization()
{
    if (m_ExecutionInProgress || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog, the reference's image and the view's mask are read here, on the GUI thread; the run reads only these.
    const Settings asked = settings();
    ImageViewControl* referenceView = getViewControlFromSelectedImage(asked.referenceName);
    const std::optional<an::ImageDescriptor> referenceDescribed =
        referenceView != nullptr ? referenceView->imageDescriptor() : std::nullopt;
    if (!referenceDescribed.has_value())
    {
        notifyErrorMessage(QStringLiteral("Background Neutralization"),
                           tr("The selected background reference image is no longer available."));
        return;
    }
    QRect region(0, 0, static_cast<int>(referenceDescribed->extent().width()),
                 static_cast<int>(referenceDescribed->extent().height()));
    if (asked.regionChecked)
    {
        if (!an::ui::processes::rectangleInside(asked.region, *referenceDescribed))
        {
            notifyErrorMessage(
                QStringLiteral("Background Neutralization"),
                tr("The background ROI must be fully inside the reference image and have a non-zero size."));
            return;
        }
        region = asked.region;
    }
    const std::string targetName = m_image_view_control->getUniqueName();
    const bool referenceIsTarget = referenceView == m_image_view_control;
    // The image a still target's run changes is its own reference; a sequence's is the frame its view shows.
    const an::ui::processes::ToolReference reference{
        .asset = referenceIsTarget && !m_image_view_control->isSequence() ? nullptr : referenceView->document().asset(),
        .region = an::ui::processes::regionOf(region, *referenceDescribed)};
    const an::ui::processes::ViewMaskSnapshot mask = an::ui::processes::viewMaskOf(*m_image_view_control);

    m_ExecutionInProgress = true;
    enableButtons(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", targetName);

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Background Neutralization"),
        .progressText =
            QString::fromStdString(fmt::format("Executing background neutralization to sequence {}", targetName)),
        .errorTitle = QStringLiteral("Error running background neutralization"),
        .errorLead = "An error occurred when executing the background neutralization process"};
    request.stepName = getProcessName();
    request.stepDescription =
        stepDescription(referenceIsTarget ? targetName : asked.referenceName.toStdString(), region);
    auto measured = std::make_shared<an::ui::processes::MeasuredOnce<an::ui::processes::NeutralizationMeasurement>>();
    request.operation = [reference, range = asked.range, mask,
                         measured](const an::ui::ExecutionScope& scope,
                                   const an::ui::ProcessSource& source) -> an::Result<an::consumer::ImageAsset> {
        // The reference does not change while a sequence's frames are read, so it is measured once.
        an::Result<an::ui::processes::NeutralizationMeasurement> measurement =
            measured->get([&]() -> an::Result<an::ui::processes::NeutralizationMeasurement> {
                an::Result<an::ui::processes::ReferenceRead> read =
                    an::ui::processes::ReferenceRead::read(scope, reference, source);
                if (!read.hasValue())
                {
                    return read.status();
                }
                return an::ui::processes::measureNeutralization(scope, read->source(), reference.region, range);
            });
        if (!measurement.hasValue())
        {
            return measurement.status();
        }
        return an::ui::processes::applyChannelGains(
            scope, source,
            an::ui::processes::ChannelGainsApply{
                .gains = {1.0, 1.0, 1.0}, .offsets = measurement->offsets, .lineage = "background neutralization"},
            mask);
    };
    // The reference and the mask are other views' images, leased before the run is admitted.
    request.reads = {reference.asset, mask.asset};
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<BackgroundNeutralizationProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void BackgroundNeutralizationProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if (outcome.published || m_ProcessCloseRequested)
    {
        clearProcessMessage();
        close();
        return;
    }
    enableButtons(true);
}

void BackgroundNeutralizationProcess::enableButtons(const bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
}

ImageViewControl* BackgroundNeutralizationProcess::getViewControlFromSelectedImage(const QString& name) const
{
    if (m_image_view_control == nullptr || name == QString::fromUtf8(an::ui::processes::kTargetImageReference) ||
        name == QString::fromStdString(m_image_view_control->getUniqueName()))
    {
        return m_image_view_control;
    }
    return process_utils::findViewControlByName(name);
}

void BackgroundNeutralizationProcess::updateBackgroundReferenceImageData()
{
    auto targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText());
    if (!targetControl)
    {
        return;
    }

    const auto [width, height] = extentOf(targetControl);
    m_ui.backgroundRoiLeftValue->setMaximum((std::max)(0, width - 1));
    m_ui.backgroundRoiLeftValue->setValue(0);
    m_ui.backgroundRoiTopValue->setMaximum((std::max)(0, height - 1));
    m_ui.backgroundRoiTopValue->setValue(0);
    m_ui.backgroundRoiWidthValue->setMaximum(width);
    m_ui.backgroundRoiWidthValue->setValue(width);
    m_ui.backgroundRoiHeightValue->setMaximum(height);
    m_ui.backgroundRoiHeightValue->setValue(height);
}

void BackgroundNeutralizationProcess::updateSelectionArea()
{
    if (!m_IgnoreLastCropMoveEvent && m_CropController && curentCropStatus == 2)
    {
        m_CropController->updateSelectionArea(
            QRect(m_ui.backgroundRoiLeftValue->value(), m_ui.backgroundRoiTopValue->value(),
                  m_ui.backgroundRoiWidthValue->value(), m_ui.backgroundRoiHeightValue->value()));
    }
}

void BackgroundNeutralizationProcess::destroyCropController()
{
    if (m_CropController)
    {
        m_CropController->destroy();
        m_CropController.reset();
    }
}

void BackgroundNeutralizationProcess::resetBackgroundRoiValues()
{
    m_ui.backgroundRoiLeftValue->setValue(0);
    m_ui.backgroundRoiTopValue->setValue(0);
    m_ui.backgroundRoiWidthValue->setValue(0);
    m_ui.backgroundRoiHeightValue->setValue(0);
}

void BackgroundNeutralizationProcess::closeEvent(QCloseEvent* event)
{
    destroyCropController();
    ImageProcessControl::closeEvent(event);
}

bool BackgroundNeutralizationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return an::ui::processes::showsRgbImage(ctrl);
}
