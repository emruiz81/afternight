// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ColorCalibrationProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"

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

/// A reference a run reads, resolved on the GUI thread: what it is, the rectangle measured, and its name.
struct ResolvedReference
{
    an::ui::processes::ToolReference reference;
    QRect region;
    std::string name;
};

} // namespace

ColorCalibrationProcess::ColorCalibrationProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);

    m_ui = an::ui::processes::buildColorCalibrationProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Calibrate channel balance using white/background references and optional manual white balance."));
    m_ui.mwbGroupBox->setFlat(true);
    m_ui.whiteRoiGroupBox->setFlat(true);
    m_ui.backgroundRoiGroupBox->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ColorCalibrationProcess::on_dialog_box_button_clicked);

    QObject::connect(m_ui.whiteReferenceImageSelector, &QComboBox::currentTextChanged, this,
                     &ColorCalibrationProcess::on_whiteReferenceImageSelector_currentTextChanged);
    QObject::connect(m_ui.backgroundReferenceImageSelector, &QComboBox::currentTextChanged, this,
                     &ColorCalibrationProcess::on_backgroundReferenceImageSelector_currentTextChanged);
    QObject::connect(m_ui.mwbGroupBox, &QGroupBox::clicked, this, &ColorCalibrationProcess::on_mwbGroupBoxClicked);
    QObject::connect(m_ui.whiteRoiGroupBox, &QGroupBox::clicked, this,
                     &ColorCalibrationProcess::on_whiteRoiGroupBoxClicked);
    QObject::connect(m_ui.backgroundRoiGroupBox, &QGroupBox::clicked, this,
                     &ColorCalibrationProcess::on_backgroundRoiGroupBoxClicked);
    QObject::connect(m_ui.whiteRoiSelectionButton, &QPushButton::clicked, this,
                     &ColorCalibrationProcess::on_whiteRoiSelectionButtonClicked);
    QObject::connect(m_ui.backgroundRoiSelectionButton, &QPushButton::clicked, this,
                     &ColorCalibrationProcess::on_backgroundRoiSelectionButtonClicked);

    QObject::connect(m_ui.whiteRoiLeftValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_whiteRoiLeftValueChanged);
    QObject::connect(m_ui.whiteRoiTopValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_whiteRoiTopValueChanged);
    QObject::connect(m_ui.whiteRoiWidthValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_roiSizeChanged);
    QObject::connect(m_ui.whiteRoiHeightValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_roiSizeChanged);

    QObject::connect(m_ui.backgroundRoiLeftValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_backgroundRoiLeftValueChanged);
    QObject::connect(m_ui.backgroundRoiTopValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_backgroundRoiTopValueChanged);
    QObject::connect(m_ui.backgroundRoiWidthValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_roiSizeChanged);
    QObject::connect(m_ui.backgroundRoiHeightValue, &QSpinBox::valueChanged, this,
                     &ColorCalibrationProcess::on_roiSizeChanged);

    INIT_D_SPINBOX_CTRL(whiteLowerLimitControl, m_ui.whiteLowerLimitValue, m_ui.whiteLowerLimitSlider,
                        &ColorCalibrationProcess::on_limitChanged);
    INIT_D_SPINBOX_CTRL(whiteUpperLimitControl, m_ui.whiteUpperLimitValue, m_ui.whiteUpperLimitSlider,
                        &ColorCalibrationProcess::on_limitChanged);
    INIT_D_SPINBOX_CTRL(backgroundLowerLimitControl, m_ui.backgroundLowerLimitValue, m_ui.backgroundLowerLimitSlider,
                        &ColorCalibrationProcess::on_limitChanged);
    INIT_D_SPINBOX_CTRL(backgroundUpperLimitControl, m_ui.backgroundUpperLimitValue, m_ui.backgroundUpperLimitSlider,
                        &ColorCalibrationProcess::on_limitChanged);
    INIT_D_SPINBOX_CTRL(mwbRedControl, m_ui.mwbRedValue, m_ui.mwbRedSlider, &ColorCalibrationProcess::on_limitChanged);
    INIT_D_SPINBOX_CTRL(mwbGreenControl, m_ui.mwbGreenValue, m_ui.mwbGreenSlider,
                        &ColorCalibrationProcess::on_limitChanged);
    INIT_D_SPINBOX_CTRL(mwbBlueControl, m_ui.mwbBlueValue, m_ui.mwbBlueSlider,
                        &ColorCalibrationProcess::on_limitChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();

    refreshTheme();
}

std::string ColorCalibrationProcess::getProcessNameStatic()
{
    return "ColorCalibration";
}

bool ColorCalibrationProcess::requiresExclusiveAccessStatic()
{
    return true;
}

void ColorCalibrationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void ColorCalibrationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);
    updateSelectionArea();
}

void ColorCalibrationProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    destroyCropController();
    initialize();
}

void ColorCalibrationProcess::addReferenceItem(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    const QString tooltip = an::ui::buildImageSelectorTooltip(ctrl);
    m_ui.whiteReferenceImageSelector->addItem(imageName);
    m_ui.whiteReferenceImageSelector->setItemData(m_ui.whiteReferenceImageSelector->count() - 1, tooltip,
                                                  Qt::ToolTipRole);
    m_ui.backgroundReferenceImageSelector->addItem(imageName);
    m_ui.backgroundReferenceImageSelector->setItemData(m_ui.backgroundReferenceImageSelector->count() - 1, tooltip,
                                                       Qt::ToolTipRole);
}

void ColorCalibrationProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlCreated(ctrl);
    if (an::ui::processes::showsRgbImage(ctrl))
    {
        addReferenceItem(ctrl);
    }
}

void ColorCalibrationProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlDestroyed(ctrl);
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    const QString target = QString::fromUtf8(an::ui::processes::kTargetImageReference);
    for (QComboBox* selector : {m_ui.whiteReferenceImageSelector, m_ui.backgroundReferenceImageSelector})
    {
        const int idx = selector->findText(imageName);
        if (idx != -1)
        {
            if (selector->currentText() == imageName)
            {
                selector->setCurrentText(target);
            }
            selector->removeItem(idx);
        }
    }
}

void ColorCalibrationProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                   const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlRenamed(newName, oldName, ctrl);
    const QString newImageName = QString::fromUtf8(newName);
    const QString oldImageName = QString::fromUtf8(oldName);
    for (QComboBox* selector : {m_ui.whiteReferenceImageSelector, m_ui.backgroundReferenceImageSelector})
    {
        const int idx = selector->findText(oldImageName);
        if (idx != -1)
        {
            selector->setItemText(idx, newImageName);
            selector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
}

void ColorCalibrationProcess::selectionAreaUpdated()
{
    m_IgnoreLastCropMoveEvent = true;
    if (curentCropStatus == 1)
    {
        if (auto* targetControl = getViewControlFromSelectedImage(m_ui.whiteReferenceImageSelector->currentText()))
        {
            const QRect selArea = targetControl->getViewWidget()->getCurrentSelectionArea();
            m_ui.whiteRoiLeftValue->setValue(selArea.x());
            m_ui.whiteRoiTopValue->setValue(selArea.y());
            m_ui.whiteRoiWidthValue->setValue(selArea.width());
            m_ui.whiteRoiHeightValue->setValue(selArea.height());
        }
    }
    else if (curentCropStatus == 2)
    {
        if (auto* targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText()))
        {
            const QRect selArea = targetControl->getViewWidget()->getCurrentSelectionArea();
            m_ui.backgroundRoiLeftValue->setValue(selArea.x());
            m_ui.backgroundRoiTopValue->setValue(selArea.y());
            m_ui.backgroundRoiWidthValue->setValue(selArea.width());
            m_ui.backgroundRoiHeightValue->setValue(selArea.height());
        }
    }

    m_IgnoreLastCropMoveEvent = false;
}

void ColorCalibrationProcess::selectionAreaChanged() {}

void ColorCalibrationProcess::newSelectionFinished() {}

ColorCalibrationProcess::~ColorCalibrationProcess()
{
    destroyCropController();
}

void ColorCalibrationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    if (!m_ui.buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = m_ui.buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply)
    {
        executeColorCalibration();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        this->initialize();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}

void ColorCalibrationProcess::on_whiteReferenceImageSelector_currentTextChanged(const QString&)
{
    if (m_ui.whiteRoiGroupBox->isChecked())
    {
        updateWhiteReferenceImageData();
        if (m_ui.whiteRoiSelectionButton->isChecked())
        {
            destroyCropController();
            curentCropStatus = 0;
            m_ui.whiteRoiSelectionButton->setChecked(false);
        }
    }
}

void ColorCalibrationProcess::on_backgroundReferenceImageSelector_currentTextChanged(const QString&)
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

void ColorCalibrationProcess::on_limitChanged(double) {}

void ColorCalibrationProcess::on_mwbGroupBoxClicked(bool on)
{
    if (on && curentCropStatus == 1)
    {
        destroyCropController();
        curentCropStatus = 0;
        m_ui.whiteRoiSelectionButton->setChecked(false);
    }

    updateManualWhiteBalanceAvailability();
}

void ColorCalibrationProcess::on_whiteRoiGroupBoxClicked(bool on)
{
    if (on)
    {
        updateWhiteReferenceImageData();
        if (m_ui.whiteRoiSelectionButton->isChecked())
        {
            destroyCropController();
            curentCropStatus = 0;
            m_ui.whiteRoiSelectionButton->setChecked(false);
        }
    }
    else
    {
        destroyCropController();
        curentCropStatus = 0;
        m_ui.whiteRoiSelectionButton->setChecked(false);
        resetWhiteRoiValues();
    }

    updateManualWhiteBalanceAvailability();
}

void ColorCalibrationProcess::on_backgroundRoiGroupBoxClicked(bool on)
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

void ColorCalibrationProcess::on_whiteRoiSelectionButtonClicked(bool checked)
{
    if (checked)
    {
        curentCropStatus = 1;
        m_ui.backgroundRoiSelectionButton->setChecked(false);

        auto targetControl = getViewControlFromSelectedImage(m_ui.whiteReferenceImageSelector->currentText());
        if (!targetControl)
        {
            curentCropStatus = 0;
            m_ui.whiteRoiSelectionButton->setChecked(false);
            return;
        }

        destroyCropController();

        m_CropController = std::make_unique<ImageCropController>(targetControl->getScrollArea(), this);
        m_CropController->setListenerEnabled(true);
        m_CropController->enableOneClickMove(true);

        resetWhiteRoiValues();

        m_CropController->startNewSelection();

        targetControl->getViewWidget()->setImageViewSelectionStyle(ImageViewSelectionStyle::AREA_SELECTION_STYLE);

        updateSelectionArea();
    }
    else
    {
        destroyCropController();

        if (m_ui.whiteRoiWidthValue->value() == 0 || m_ui.whiteRoiHeightValue->value() == 0)
        {
            updateWhiteReferenceImageData();
        }

        curentCropStatus = 0;
    }
}

void ColorCalibrationProcess::on_backgroundRoiSelectionButtonClicked(bool checked)
{
    if (checked)
    {
        curentCropStatus = 2;
        m_ui.whiteRoiSelectionButton->setChecked(false);

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

void ColorCalibrationProcess::on_whiteRoiLeftValueChanged(int value)
{
    if (auto* targetControl = getViewControlFromSelectedImage(m_ui.whiteReferenceImageSelector->currentText()))
    {
        m_ui.whiteRoiWidthValue->setMaximum(extentOf(targetControl).first - value);
        updateSelectionArea();
    }
}

void ColorCalibrationProcess::on_whiteRoiTopValueChanged(int value)
{
    if (auto* targetControl = getViewControlFromSelectedImage(m_ui.whiteReferenceImageSelector->currentText()))
    {
        m_ui.whiteRoiHeightValue->setMaximum(extentOf(targetControl).second - value);
        updateSelectionArea();
    }
}

void ColorCalibrationProcess::on_backgroundRoiLeftValueChanged(int value)
{
    if (auto* targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText()))
    {
        m_ui.backgroundRoiWidthValue->setMaximum(extentOf(targetControl).first - value);
        updateSelectionArea();
    }
}

void ColorCalibrationProcess::on_backgroundRoiTopValueChanged(int value)
{
    if (auto* targetControl = getViewControlFromSelectedImage(m_ui.backgroundReferenceImageSelector->currentText()))
    {
        m_ui.backgroundRoiHeightValue->setMaximum(extentOf(targetControl).second - value);
        updateSelectionArea();
    }
}

void ColorCalibrationProcess::on_roiSizeChanged(int)
{
    updateSelectionArea();
}

void ColorCalibrationProcess::initialize()
{
    destroyCropController();
    curentCropStatus = 0;

    const QString target = QString::fromUtf8(an::ui::processes::kTargetImageReference);
    m_ui.whiteReferenceImageSelector->clear();
    m_ui.backgroundReferenceImageSelector->clear();

    m_ui.whiteReferenceImageSelector->addItem(target);
    m_ui.whiteReferenceImageSelector->setItemData(m_ui.whiteReferenceImageSelector->count() - 1,
                                                  an::ui::buildImageSelectorTooltip(m_image_view_control),
                                                  Qt::ToolTipRole);
    m_ui.backgroundReferenceImageSelector->addItem(target);
    m_ui.backgroundReferenceImageSelector->setItemData(m_ui.backgroundReferenceImageSelector->count() - 1,
                                                       an::ui::buildImageSelectorTooltip(m_image_view_control),
                                                       Qt::ToolTipRole);

    // The target is offered once, as the target entry.
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl != m_image_view_control && an::ui::processes::showsRgbImage(viewControl))
        {
            addReferenceItem(viewControl);
        }
    }

    m_ui.whiteReferenceImageSelector->setCurrentText(target);
    m_ui.backgroundReferenceImageSelector->setCurrentText(target);

    m_ui.whiteRoiSelectionButton->setChecked(false);
    m_ui.backgroundRoiSelectionButton->setChecked(false);
    m_ui.whiteRoiGroupBox->setChecked(false);
    m_ui.backgroundRoiGroupBox->setChecked(false);
    resetWhiteRoiValues();
    resetBackgroundRoiValues();

    whiteLowerLimitControl->setValue(0.0);
    whiteUpperLimitControl->setValue(0.9);
    backgroundLowerLimitControl->setValue(0.0);
    backgroundUpperLimitControl->setValue(0.1);

    mwbRedControl->setValue(1.0);
    mwbGreenControl->setValue(1.0);
    mwbBlueControl->setValue(1.0);

    m_ui.mwbGroupBox->setChecked(false);

    m_ui.outputWhiteReferenceMask->setChecked(false);
    m_ui.outputBackgroundReferenceMask->setChecked(false);
    updateManualWhiteBalanceAvailability();
}

ColorCalibrationProcess::Settings ColorCalibrationProcess::settings() const
{
    Settings asked;
    asked.whiteName = m_ui.whiteReferenceImageSelector->currentText();
    asked.whiteRegionChecked = m_ui.whiteRoiGroupBox->isChecked();
    asked.whiteRegion = QRect(m_ui.whiteRoiLeftValue->value(), m_ui.whiteRoiTopValue->value(),
                              m_ui.whiteRoiWidthValue->value(), m_ui.whiteRoiHeightValue->value());
    asked.whiteRange = an::ui::processes::FractionRange{.lowest = whiteLowerLimitControl->getValue(),
                                                        .highest = whiteUpperLimitControl->getValue()};
    asked.manual = m_ui.mwbGroupBox->isChecked();
    asked.manualWhite = {mwbRedControl->getValue(), mwbGreenControl->getValue(), mwbBlueControl->getValue()};
    asked.backgroundName = m_ui.backgroundReferenceImageSelector->currentText();
    asked.backgroundRegionChecked = m_ui.backgroundRoiGroupBox->isChecked();
    asked.backgroundRegion = QRect(m_ui.backgroundRoiLeftValue->value(), m_ui.backgroundRoiTopValue->value(),
                                   m_ui.backgroundRoiWidthValue->value(), m_ui.backgroundRoiHeightValue->value());
    asked.backgroundRange = an::ui::processes::FractionRange{.lowest = backgroundLowerLimitControl->getValue(),
                                                             .highest = backgroundUpperLimitControl->getValue()};
    asked.whiteMask = m_ui.outputWhiteReferenceMask->isChecked() && !asked.manual;
    asked.backgroundMask = m_ui.outputBackgroundReferenceMask->isChecked();
    return asked;
}

std::string ColorCalibrationProcess::stepDescription(const Settings& settings, const std::string& whiteName,
                                                     const QRect& whiteRegion, const std::string& backgroundName,
                                                     const QRect& backgroundRegion)
{
    const std::string whiteParam =
        settings.manual
            ? fmt::format("Using manual white balance values R:{:.6f}, G:{:.6f}, B:{:.6f}", settings.manualWhite[0],
                          settings.manualWhite[1], settings.manualWhite[2])
            : fmt::format("White reference image '{}' (ROI X:{}, Y:{}, W:{}, H:{}) (MIN:{:.6f}, MAX:{:.6f})", whiteName,
                          whiteRegion.x(), whiteRegion.y(), whiteRegion.width(), whiteRegion.height(),
                          settings.whiteRange.lowest, settings.whiteRange.highest);
    const std::string backgroundParam =
        fmt::format("Background reference image '{}' (ROI X:{}, Y:{}, W:{}, H:{}) (MIN:{:.6f}, MAX:{:.6f})",
                    backgroundName, backgroundRegion.x(), backgroundRegion.y(), backgroundRegion.width(),
                    backgroundRegion.height(), settings.backgroundRange.lowest, settings.backgroundRange.highest);
    return whiteParam + " - " + backgroundParam;
}

void ColorCalibrationProcess::executeColorCalibration()
{
    if (m_ExecutionInProgress || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog, the references' images and the view's mask are read here, on the GUI thread; the run reads only
    // these.
    const Settings asked = settings();
    const std::string targetName = m_image_view_control->getUniqueName();
    const QString title = QStringLiteral("Color Calibration");

    // A reference resolved to what the run reads: the image a still target's run changes is its own reference, and a
    // sequence's is the frame its view shows.
    const auto resolve = [&](const QString& name, const bool regionChecked, const QRect& rectangle,
                             const QString& unavailable, const QString& outside) -> std::optional<ResolvedReference> {
        ImageViewControl* view = getViewControlFromSelectedImage(name);
        const std::optional<an::ImageDescriptor> described = view != nullptr ? view->imageDescriptor() : std::nullopt;
        if (!described.has_value())
        {
            notifyErrorMessage(title, unavailable);
            return std::nullopt;
        }
        QRect region(0, 0, static_cast<int>(described->extent().width()),
                     static_cast<int>(described->extent().height()));
        if (regionChecked)
        {
            if (!an::ui::processes::rectangleInside(rectangle, *described))
            {
                notifyErrorMessage(title, outside);
                return std::nullopt;
            }
            region = rectangle;
        }
        const bool isTarget = view == m_image_view_control;
        return ResolvedReference{
            .reference = {.asset = isTarget && !m_image_view_control->isSequence() ? nullptr : view->document().asset(),
                          .region = an::ui::processes::regionOf(region, *described)},
            .region = region,
            .name = isTarget ? targetName : name.toStdString()};
    };

    std::optional<ResolvedReference> white;
    if (!asked.manual)
    {
        white =
            resolve(asked.whiteName, asked.whiteRegionChecked, asked.whiteRegion,
                    tr("The selected white reference image is no longer available."),
                    tr("The white reference ROI must be fully inside the reference image and have a non-zero size."));
        if (!white.has_value())
        {
            return;
        }
    }
    const std::optional<ResolvedReference> background =
        resolve(asked.backgroundName, asked.backgroundRegionChecked, asked.backgroundRegion,
                tr("The selected background reference image is no longer available."),
                tr("The background reference ROI must be fully inside the reference image and have a non-zero size."));
    if (!background.has_value())
    {
        return;
    }
    const an::ui::processes::ViewMaskSnapshot mask = an::ui::processes::viewMaskOf(*m_image_view_control);

    an::ui::processes::ColorCalibrationRequest calibration;
    if (asked.manual)
    {
        calibration.manualWhite =
            an::ColorChannelGains{asked.manualWhite[0], asked.manualWhite[1], asked.manualWhite[2]};
    }
    else
    {
        calibration.whiteRegion = white->reference.region;
        calibration.whiteRange = asked.whiteRange;
    }
    calibration.backgroundRegion = background->reference.region;

    m_ExecutionInProgress = true;
    enableButtons(false);
    m_StagedMasks->clear();

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", targetName);

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = title,
        .progressText = QString::fromStdString(fmt::format("Executing color calibration to sequence {}", targetName)),
        .errorTitle = QStringLiteral("Error running color calibration"),
        .errorLead = "An error occurred when executing the color calibration process"};
    request.stepName = getProcessName();
    request.stepDescription =
        stepDescription(asked, white.has_value() ? white->name : std::string(),
                        white.has_value() ? white->region : QRect(), background->name, background->region);
    auto measured = std::make_shared<an::ui::processes::MeasuredOnce<an::ui::processes::ColorCalibrationMeasurement>>();
    std::optional<an::ui::processes::ToolReference> whiteReference;
    if (white.has_value())
    {
        whiteReference = white->reference;
    }
    request.operation = [whiteReference, backgroundReference = background->reference, calibration,
                         whiteRange = asked.whiteRange, backgroundRange = asked.backgroundRange,
                         whiteMask = asked.whiteMask, backgroundMask = asked.backgroundMask, mask, measured,
                         staged = m_StagedMasks,
                         targetName](const an::ui::ExecutionScope& scope,
                                     const an::ui::ProcessSource& source) -> an::Result<an::consumer::ImageAsset> {
        // The references do not change while a sequence's frames are read, so they are measured - and their masks
        // made - once.
        an::Result<an::ui::processes::ColorCalibrationMeasurement> measurement =
            measured->get([&]() -> an::Result<an::ui::processes::ColorCalibrationMeasurement> {
                std::optional<an::ui::processes::ReferenceRead> whiteRead;
                if (whiteReference.has_value())
                {
                    an::Result<an::ui::processes::ReferenceRead> read =
                        an::ui::processes::ReferenceRead::read(scope, *whiteReference, source);
                    if (!read.hasValue())
                    {
                        return read.status();
                    }
                    whiteRead.emplace(std::move(*read));
                }
                an::Result<an::ui::processes::ReferenceRead> backgroundRead =
                    an::ui::processes::ReferenceRead::read(scope, backgroundReference, source);
                if (!backgroundRead.hasValue())
                {
                    return backgroundRead.status();
                }
                const std::optional<an::ui::ProcessSource> whiteSource =
                    whiteRead.has_value() ? std::optional<an::ui::ProcessSource>(whiteRead->source()) : std::nullopt;
                an::Result<an::ui::processes::ColorCalibrationMeasurement> made =
                    an::ui::processes::measureColorCalibration(scope, whiteSource.has_value() ? &*whiteSource : nullptr,
                                                               backgroundRead->source(), calibration);
                if (!made.hasValue())
                {
                    return made.status();
                }
                if (whiteMask && whiteSource.has_value())
                {
                    an::Result<an::consumer::ImageAsset> selected =
                        an::ui::processes::selectedSamplesMask(scope, *whiteSource, whiteReference->region, whiteRange);
                    if (!selected.hasValue())
                    {
                        return selected.status();
                    }
                    staged->stage(process_utils::StagedOutput{
                        .asset = std::move(*selected),
                        .name = QString::fromStdString(targetName + " - WHITE MASK"),
                        .history =
                            fmt::format("ColorCalibration: White reference mask image created from '{}'", targetName)});
                }
                if (backgroundMask)
                {
                    an::Result<an::consumer::ImageAsset> selected = an::ui::processes::selectedSamplesMask(
                        scope, backgroundRead->source(), backgroundReference.region, backgroundRange);
                    if (!selected.hasValue())
                    {
                        return selected.status();
                    }
                    staged->stage(process_utils::StagedOutput{
                        .asset = std::move(*selected),
                        .name = QString::fromStdString(targetName + " - BACKGROUND MASK"),
                        .history = fmt::format("ColorCalibration: Background reference mask image created from '{}'",
                                               targetName)});
                }
                return made;
            });
        if (!measurement.hasValue())
        {
            return measurement.status();
        }
        return an::ui::processes::applyChannelGains(
            scope, source,
            an::ui::processes::ChannelGainsApply{
                .gains = measurement->gains, .offsets = measurement->offsets, .lineage = "color calibration"},
            mask);
    };
    // The references and the mask are other views' images, leased before the run is admitted - one lease for a view
    // that is both references.
    request.reads = {whiteReference.has_value() ? whiteReference->asset : nullptr, background->reference.asset,
                     mask.asset};
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<ColorCalibrationProcess> guard(this);
    QPointer<ImageViewControl> target(m_image_view_control);
    an::ui::processes::runInPlace(
        *m_image_view_control, std::move(request),
        [guard, target, staged = m_StagedMasks, targetName](const an::ui::processes::GeometryRunOutcome& outcome) {
            // The masks open beside the target once its result is shown, even when the window is gone, as long as the
            // target is there to show them beside.
            if (outcome.published && target)
            {
                (void)process_utils::openStagedOutputs(staged->take(), target.data());
            }
            else
            {
                staged->clear();
            }
            if (guard)
            {
                guard->finishRun(outcome, targetName);
            }
        });
}

void ColorCalibrationProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome,
                                        const std::string& targetName)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(), targetName);
    }
    if (outcome.published || m_ProcessCloseRequested)
    {
        clearProcessMessage();
        close();
        return;
    }
    enableButtons(true);
    updateManualWhiteBalanceAvailability();
}

void ColorCalibrationProcess::enableButtons(const bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
}

ImageViewControl* ColorCalibrationProcess::getViewControlFromSelectedImage(const QString& name) const
{
    if (m_image_view_control == nullptr || name == QString::fromUtf8(an::ui::processes::kTargetImageReference) ||
        name == QString::fromStdString(m_image_view_control->getUniqueName()))
    {
        return m_image_view_control;
    }
    return process_utils::findViewControlByName(name);
}

void ColorCalibrationProcess::updateWhiteReferenceImageData()
{
    auto targetControl = getViewControlFromSelectedImage(m_ui.whiteReferenceImageSelector->currentText());
    if (!targetControl)
    {
        return;
    }

    const auto [width, height] = extentOf(targetControl);
    m_ui.whiteRoiLeftValue->setMaximum((std::max)(0, width - 1));
    m_ui.whiteRoiLeftValue->setValue(0);
    m_ui.whiteRoiTopValue->setMaximum((std::max)(0, height - 1));
    m_ui.whiteRoiTopValue->setValue(0);
    m_ui.whiteRoiWidthValue->setMaximum(width);
    m_ui.whiteRoiWidthValue->setValue(width);
    m_ui.whiteRoiHeightValue->setMaximum(height);
    m_ui.whiteRoiHeightValue->setValue(height);
}

void ColorCalibrationProcess::updateBackgroundReferenceImageData()
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

void ColorCalibrationProcess::updateSelectionArea()
{
    if (!m_IgnoreLastCropMoveEvent && m_CropController)
    {
        if (curentCropStatus == 1)
        {
            m_CropController->updateSelectionArea(QRect(m_ui.whiteRoiLeftValue->value(), m_ui.whiteRoiTopValue->value(),
                                                        m_ui.whiteRoiWidthValue->value(),
                                                        m_ui.whiteRoiHeightValue->value()));
        }
        else if (curentCropStatus == 2)
        {
            m_CropController->updateSelectionArea(
                QRect(m_ui.backgroundRoiLeftValue->value(), m_ui.backgroundRoiTopValue->value(),
                      m_ui.backgroundRoiWidthValue->value(), m_ui.backgroundRoiHeightValue->value()));
        }
    }
}

void ColorCalibrationProcess::updateManualWhiteBalanceAvailability()
{
    const bool manualWhiteBalance = m_ui.mwbGroupBox->isChecked();

    m_ui.whiteReferenceImageSelector->setEnabled(!manualWhiteBalance);
    whiteLowerLimitControl->setEnabled(!manualWhiteBalance);
    whiteUpperLimitControl->setEnabled(!manualWhiteBalance);
    m_ui.outputWhiteReferenceMask->setEnabled(!manualWhiteBalance);
    m_ui.whiteRoiGroupBox->setEnabled(!manualWhiteBalance);

    const bool whiteRoiEnabled = !manualWhiteBalance && m_ui.whiteRoiGroupBox->isChecked();
    m_ui.whiteRoiSelectionButton->setEnabled(whiteRoiEnabled);
    m_ui.whiteRoiLeftValue->setEnabled(whiteRoiEnabled);
    m_ui.whiteRoiTopValue->setEnabled(whiteRoiEnabled);
    m_ui.whiteRoiWidthValue->setEnabled(whiteRoiEnabled);
    m_ui.whiteRoiHeightValue->setEnabled(whiteRoiEnabled);
}

void ColorCalibrationProcess::destroyCropController()
{
    if (m_CropController)
    {
        m_CropController->destroy();
        m_CropController.reset();
    }
}

void ColorCalibrationProcess::resetWhiteRoiValues()
{
    m_ui.whiteRoiLeftValue->setValue(0);
    m_ui.whiteRoiTopValue->setValue(0);
    m_ui.whiteRoiWidthValue->setValue(0);
    m_ui.whiteRoiHeightValue->setValue(0);
}

void ColorCalibrationProcess::resetBackgroundRoiValues()
{
    m_ui.backgroundRoiLeftValue->setValue(0);
    m_ui.backgroundRoiTopValue->setValue(0);
    m_ui.backgroundRoiWidthValue->setValue(0);
    m_ui.backgroundRoiHeightValue->setValue(0);
}

void ColorCalibrationProcess::closeEvent(QCloseEvent* event)
{
    destroyCropController();
    ImageProcessControl::closeEvent(event);
}

bool ColorCalibrationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return an::ui::processes::showsRgbImage(ctrl);
}
