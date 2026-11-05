// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "components/AnInfoBanner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct PhotometricColorCalibrationUiElements
{
    QDoubleSpinBox* magnitudeLimitSpin = nullptr;
    QDoubleSpinBox* matchRadiusSpin = nullptr;
    QCheckBox* backgroundNeutralizationCheck = nullptr;
    QGroupBox* backgroundRoiGroupBox = nullptr;
    QPushButton* backgroundRoiSelectionButton = nullptr;
    QSpinBox* backgroundRoiLeftValue = nullptr;
    QSpinBox* backgroundRoiTopValue = nullptr;
    QSpinBox* backgroundRoiWidthValue = nullptr;
    QSpinBox* backgroundRoiHeightValue = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QCheckBox* showMatchedStars = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    an::ui::AnInfoBanner* warningBanner = nullptr;
    QPushButton* actionButton = nullptr;
    QLabel* statusLabel = nullptr;
};

inline PhotometricColorCalibrationUiElements buildPhotometricColorCalibrationProcessUi(QWidget* parent)
{
    PhotometricColorCalibrationUiElements ui;
    if (!parent)
    {
        return ui;
    }

    const auto& t = an::ui::tokens();

    parent->setObjectName(QStringLiteral("PhotometricColorCalibrationProcess"));
    parent->resize(500, 500);
    parent->setMinimumSize(QSize(460, 470));
    parent->setMaximumSize(QSize(500, 500));
    parent->setWindowTitle(
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Photometric Color Calibration", nullptr));

    auto* rootLayout = new QVBoxLayout(parent);
    rootLayout->setContentsMargins(t.spacing.lg, t.spacing.md, t.spacing.lg, t.spacing.md);
    rootLayout->setSpacing(t.spacing.md);

    auto* introLabel =
        new QLabel(QCoreApplication::translate(
                       "PhotometricColorCalibrationProcess",
                       "Calibrate RGB channel balance from Gaia DR3 star colors on a plate-solved image.", nullptr),
                   parent);
    introLabel->setWordWrap(true);
    rootLayout->addWidget(introLabel);

    // Warning banner (hidden by default, shown for missing catalog / not plate-solved)
    ui.warningBanner = new an::ui::AnInfoBanner(QString(), an::ui::AnInfoBanner::Tone::Warning, parent);
    ui.warningBanner->setVisible(false);
    rootLayout->addWidget(ui.warningBanner);

    // Action button (hidden by default, shown alongside warning banner)
    ui.actionButton = new QPushButton(parent);
    ui.actionButton->setVisible(false);
    rootLayout->addWidget(ui.actionButton);

    auto* formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, t.spacing.sm, 0, 0);
    formLayout->setHorizontalSpacing(t.spacing.sm);
    formLayout->setVerticalSpacing(t.spacing.sm);

    ui.magnitudeLimitSpin = new QDoubleSpinBox(parent);
    ui.magnitudeLimitSpin->setRange(6.0, 20.0);
    ui.magnitudeLimitSpin->setDecimals(1);
    ui.magnitudeLimitSpin->setSingleStep(0.5);
    ui.magnitudeLimitSpin->setValue(14.0);
    ui.magnitudeLimitSpin->setToolTip(
        QCoreApplication::translate("PhotometricColorCalibrationProcess",
                                    "Maximum Gaia G magnitude of catalog stars to use for calibration. "
                                    "Higher values include fainter stars but may increase noise.",
                                    nullptr));
    formLayout->addRow(QCoreApplication::translate("PhotometricColorCalibrationProcess", "Magnitude limit:", nullptr),
                       ui.magnitudeLimitSpin);

    ui.matchRadiusSpin = new QDoubleSpinBox(parent);
    ui.matchRadiusSpin->setRange(0.5, 20.0);
    ui.matchRadiusSpin->setDecimals(1);
    ui.matchRadiusSpin->setSingleStep(0.5);
    ui.matchRadiusSpin->setValue(5.0);
    ui.matchRadiusSpin->setToolTip(
        QCoreApplication::translate("PhotometricColorCalibrationProcess",
                                    "Maximum angular separation in arcseconds to match detected stars "
                                    "with Gaia catalog entries. Increase for images with lower resolution.",
                                    nullptr));
    formLayout->addRow(
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Match radius (arcsec):", nullptr),
        ui.matchRadiusSpin);

    ui.backgroundNeutralizationCheck = new QCheckBox(
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Apply background neutralization", nullptr),
        parent);
    ui.backgroundNeutralizationCheck->setChecked(true);
    ui.backgroundNeutralizationCheck->setToolTip(QCoreApplication::translate(
        "PhotometricColorCalibrationProcess",
        "Automatically estimate the image background with sigma-clipped statistics before neutralizing it.", nullptr));
    formLayout->addRow(QString(), ui.backgroundNeutralizationCheck);

    rootLayout->addLayout(formLayout);

    ui.backgroundRoiGroupBox = new QGroupBox(
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Background ROI Override", nullptr), parent);
    ui.backgroundRoiGroupBox->setCheckable(true);
    ui.backgroundRoiGroupBox->setChecked(false);
    ui.backgroundRoiGroupBox->setToolTip(QCoreApplication::translate(
        "PhotometricColorCalibrationProcess",
        "Restrict PCC background neutralization to a specific area of the target image.", nullptr));

    auto* roiLayout = new QVBoxLayout(ui.backgroundRoiGroupBox);
    roiLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    roiLayout->setSpacing(t.spacing.sm);

    auto* roiGrid = new QGridLayout();
    roiGrid->setHorizontalSpacing(t.spacing.md);
    roiGrid->setVerticalSpacing(t.spacing.sm);

    auto* leftLabel = new QLabel(QCoreApplication::translate("PhotometricColorCalibrationProcess", "Left:", nullptr),
                                 ui.backgroundRoiGroupBox);
    auto* topLabel = new QLabel(QCoreApplication::translate("PhotometricColorCalibrationProcess", "Top:", nullptr),
                                ui.backgroundRoiGroupBox);
    auto* widthLabel = new QLabel(QCoreApplication::translate("PhotometricColorCalibrationProcess", "Width:", nullptr),
                                  ui.backgroundRoiGroupBox);
    auto* heightLabel =
        new QLabel(QCoreApplication::translate("PhotometricColorCalibrationProcess", "Height:", nullptr),
                   ui.backgroundRoiGroupBox);

    ui.backgroundRoiLeftValue = new QSpinBox(ui.backgroundRoiGroupBox);
    ui.backgroundRoiTopValue = new QSpinBox(ui.backgroundRoiGroupBox);
    ui.backgroundRoiWidthValue = new QSpinBox(ui.backgroundRoiGroupBox);
    ui.backgroundRoiHeightValue = new QSpinBox(ui.backgroundRoiGroupBox);

    for (auto* spin :
         {ui.backgroundRoiLeftValue, ui.backgroundRoiTopValue, ui.backgroundRoiWidthValue, ui.backgroundRoiHeightValue})
    {
        spin->setRange(0, 999999);
        spin->setMinimumWidth(88);
        spin->setMaximumWidth(88);
    }

    roiGrid->addWidget(leftLabel, 0, 0);
    roiGrid->addWidget(ui.backgroundRoiLeftValue, 0, 1);
    roiGrid->addItem(new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum), 0, 2);
    roiGrid->addWidget(topLabel, 0, 3);
    roiGrid->addWidget(ui.backgroundRoiTopValue, 0, 4);
    roiGrid->addItem(new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum), 0, 5);
    roiGrid->addWidget(widthLabel, 1, 0);
    roiGrid->addWidget(ui.backgroundRoiWidthValue, 1, 1);
    roiGrid->addItem(new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum), 1, 2);
    roiGrid->addWidget(heightLabel, 1, 3);
    roiGrid->addWidget(ui.backgroundRoiHeightValue, 1, 4);
    roiGrid->addItem(new QSpacerItem(24, 20, QSizePolicy::Expanding, QSizePolicy::Minimum), 1, 5);
    roiLayout->addLayout(roiGrid);

    ui.backgroundRoiSelectionButton =
        new QPushButton(QCoreApplication::translate("PhotometricColorCalibrationProcess", "From Selection", nullptr),
                        ui.backgroundRoiGroupBox);
    ui.backgroundRoiSelectionButton->setCheckable(true);
    ui.backgroundRoiSelectionButton->setToolTip(
        QCoreApplication::translate("PhotometricColorCalibrationProcess",
                                    "Use an interactive image selection as the PCC background ROI.", nullptr));

    auto* roiButtonLayout = new QHBoxLayout();
    roiButtonLayout->setContentsMargins(0, 0, 0, 0);
    roiButtonLayout->addStretch(1);
    roiButtonLayout->addWidget(ui.backgroundRoiSelectionButton);
    roiLayout->addLayout(roiButtonLayout);

    rootLayout->addWidget(ui.backgroundRoiGroupBox);

    ui.statusLabel = new QLabel(parent);
    ui.statusLabel->setWordWrap(true);
    rootLayout->addWidget(ui.statusLabel);

    rootLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* selectorLayout = new QFormLayout();
    selectorLayout->setHorizontalSpacing(t.spacing.sm);
    selectorLayout->setVerticalSpacing(t.spacing.sm);
    ui.targetImageSelector = new QComboBox(parent);
    selectorLayout->addRow(QCoreApplication::translate("PhotometricColorCalibrationProcess", "Target image:", nullptr),
                           ui.targetImageSelector);
    rootLayout->addLayout(selectorLayout);

    ui.showMatchedStars = new QCheckBox(
        QCoreApplication::translate("PhotometricColorCalibrationProcess", "Show matched stars", nullptr), parent);
    ui.showMatchedStars->setChecked(true);
    rootLayout->addWidget(ui.showMatchedStars);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    rootLayout->addWidget(ui.buttonBox);

    ui.magnitudeLimitSpin->setObjectName(QStringLiteral("magnitudeLimitSpin"));
    ui.matchRadiusSpin->setObjectName(QStringLiteral("matchRadiusSpin"));
    ui.backgroundNeutralizationCheck->setObjectName(QStringLiteral("backgroundNeutralizationCheck"));
    ui.backgroundRoiGroupBox->setObjectName(QStringLiteral("backgroundRoiGroupBox"));
    ui.backgroundRoiSelectionButton->setObjectName(QStringLiteral("backgroundRoiSelectionButton"));
    ui.backgroundRoiLeftValue->setObjectName(QStringLiteral("backgroundRoiLeftValue"));
    ui.backgroundRoiTopValue->setObjectName(QStringLiteral("backgroundRoiTopValue"));
    ui.backgroundRoiWidthValue->setObjectName(QStringLiteral("backgroundRoiWidthValue"));
    ui.backgroundRoiHeightValue->setObjectName(QStringLiteral("backgroundRoiHeightValue"));
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.showMatchedStars->setObjectName(QStringLiteral("showMatchedStars"));
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    ui.warningBanner->setObjectName(QStringLiteral("warningBanner"));
    ui.actionButton->setObjectName(QStringLiteral("actionButton"));
    ui.statusLabel->setObjectName(QStringLiteral("statusLabel"));

    return ui;
}

} // namespace an::ui::processes
