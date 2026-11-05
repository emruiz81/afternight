// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageRegistrationProcessUiBuilder.h
 * @brief Programmatic UI builder for the standalone
 * ImageRegistrationProcess layout.
 */
#pragma once

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "theme/AnDesignTokens.h"
#include "widgets/TableWidget.h"

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSpacerItem>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct ImageRegistrationProcessUiElements
{
    QVBoxLayout* verticalLayout_Main = nullptr;
    QVBoxLayout* verticalLayoutTab1_Main = nullptr;
    QVBoxLayout* verticalLayoutTab2_Main = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;
    QVBoxLayout* tableLayout = nullptr;
    QScrollArea* tableScrollArea = nullptr;

    QWidget* tab1 = nullptr;
    QWidget* tab2 = nullptr;

    an::ui::AnCardWidget* frameListCard = nullptr;
    an::ui::AnCardWidget* registrationGroupBox = nullptr;
    an::ui::AnCardWidget* alignmentGroupBox = nullptr;
    an::ui::AnCardWidget* outputDirectoryCard = nullptr;
    an::ui::AnCardWidget* advancedCard = nullptr;

    QWidget* imagePreviewWidget = nullptr;

    TableWidget* tableWidget = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    QPushButton* addFiles = nullptr;
    QPushButton* addFolder = nullptr;
    QPushButton* removeSelected = nullptr;
    QPushButton* removeFailedImagesButton = nullptr;
    QPushButton* moveUpButton = nullptr;
    QPushButton* moveDownButton = nullptr;
    QPushButton* nextFailedButton = nullptr;
    QPushButton* blinkCompareButton = nullptr;
    QPushButton* clearAll = nullptr;

    QLabel* currentImageLabel = nullptr;
    QLabel* registeredImagesLabel = nullptr;
    QLabel* referenceImageLabel = nullptr;
    QLabel* showInfoLabel = nullptr;

    QComboBox* registrationMethodSelector = nullptr;
    QSpinBox* minStarParirsValue = nullptr;
    QSpinBox* maxStarParirsValue = nullptr;
    QComboBox* referenceModeSelector = nullptr;
    QSpinBox* referenceImageValue = nullptr;
    QComboBox* transformationSelector = nullptr;
    QPushButton* advancedSettingsButton = nullptr;
    QCheckBox* autoSolveUnsolvedFramesCheck = nullptr;
    QCheckBox* enrichWithGaiaCatalogCheck = nullptr;
    QCheckBox* chromaticAberrationCorrectionCheck = nullptr;
    QCheckBox* reprocessAlreadyRegisteredmages = nullptr;
    QPushButton* clearRegisteredDataButton = nullptr;
    QPushButton* registerButton = nullptr;

    QRadioButton* alignModeStandard = nullptr;
    QRadioButton* alignModeMosaic = nullptr;
    QRadioButton* alignModeIntersection = nullptr;
    QComboBox* interpolationSelector = nullptr;
    QLineEdit* outputDirectory = nullptr;
    QPushButton* outputDirectoryButton = nullptr;
    QPushButton* alignButton = nullptr;

    an::ui::AnCollapsibleSection* qualityThresholdSection = nullptr;
    QSpinBox* qualityMinStarsValue = nullptr;
    QDoubleSpinBox* qualityMaxFwhmValue = nullptr;
    QDoubleSpinBox* qualityMaxEccentricityValue = nullptr;
    QPushButton* applyThresholdsButton = nullptr;
    QPushButton* resetThresholdsButton = nullptr;
};

inline ImageRegistrationProcessUiElements buildImageRegistrationProcessUi(QWidget* parent)
{
    const auto& t = an::ui::tokens();
    ImageRegistrationProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ImageRegistrationProcess"));
    parent->setWindowModality(Qt::NonModal);
    parent->resize(1460, 1080);
    parent->setMinimumSize(QSize(1200, 520));
    parent->setWindowTitle(QStringLiteral("Image Registration"));

    auto applySecondaryButton = [](QPushButton* button) {
        if (button)
        {
            button->setProperty("anButtonVariant", QStringLiteral("secondary"));
        }
    };

    ui.verticalLayout_Main = new QVBoxLayout(parent);
    ui.verticalLayout_Main->setObjectName(QStringLiteral("verticalLayout_Main"));
    ui.verticalLayout_Main->setSpacing(t.spacing.md);
    ui.verticalLayout_Main->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    // --- Fixed top section (not scrollable) ---
    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(t.spacing.md);
    ui.verticalLayout_Main->addLayout(topLayout, 3);

    ui.verticalLayout_2 = new QVBoxLayout();
    ui.verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
    ui.verticalLayout_2->setSpacing(0);
    ui.verticalLayout_2->setContentsMargins(0, 0, 0, 0);
    topLayout->addLayout(ui.verticalLayout_2, 3);
    ui.imagePreviewWidget = new QWidget(parent);
    ui.imagePreviewWidget->setObjectName(QStringLiteral("imagePreviewWidget"));
    ui.imagePreviewWidget->setMinimumSize(QSize(320, 220));
    ui.imagePreviewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui.verticalLayout_2->addWidget(ui.imagePreviewWidget, 1);

    auto* rightColumnScrollArea = new QScrollArea(parent);
    rightColumnScrollArea->setObjectName(QStringLiteral("settingsScrollArea"));
    rightColumnScrollArea->setWidgetResizable(true);
    rightColumnScrollArea->setFrameShape(QFrame::NoFrame);
    rightColumnScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightColumnScrollArea->setMinimumWidth(360);
    rightColumnScrollArea->setMinimumHeight(0);
    rightColumnScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightColumnScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    auto* rightColumnWidget = new QWidget(rightColumnScrollArea);
    rightColumnWidget->setObjectName(QStringLiteral("settingsScrollAreaContents"));
    rightColumnWidget->setMinimumWidth(0);
    rightColumnWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* rightColumn = new QVBoxLayout(rightColumnWidget);
    rightColumn->setSpacing(t.spacing.md);
    rightColumn->setContentsMargins(t.spacing.xs, 0, t.spacing.xs, 0);
    rightColumnScrollArea->setWidget(rightColumnWidget);
    topLayout->addWidget(rightColumnScrollArea, 2);

    ui.registrationGroupBox = new an::ui::AnCardWidget(QStringLiteral("Registration"), parent);
    rightColumn->addWidget(ui.registrationGroupBox);
    auto* registrationLayout = new QVBoxLayout();
    registrationLayout->setSpacing(t.spacing.sm);
    registrationLayout->setContentsMargins(0, 0, 0, 0);
    ui.registrationGroupBox->setContentLayout(registrationLayout);

    auto* registrationGrid = new QGridLayout();
    registrationGrid->setHorizontalSpacing(t.spacing.sm);
    registrationGrid->setVerticalSpacing(t.spacing.sm);
    registrationLayout->addLayout(registrationGrid);

    registrationGrid->addWidget(new QLabel(QStringLiteral("Method"), ui.registrationGroupBox), 0, 0);
    ui.registrationMethodSelector = new QComboBox(ui.registrationGroupBox);
    ui.registrationMethodSelector->setObjectName(QStringLiteral("registrationMethodSelector"));
    ui.registrationMethodSelector->setToolTip(
        QStringLiteral("Algorithm used to detect and match features between frames"));
    registrationGrid->addWidget(ui.registrationMethodSelector, 0, 1);

    registrationGrid->addWidget(new QLabel(QStringLiteral("Transformation"), ui.registrationGroupBox), 0, 2);
    ui.transformationSelector = new QComboBox(ui.registrationGroupBox);
    ui.transformationSelector->setObjectName(QStringLiteral("transformationSelector"));
    ui.transformationSelector->setToolTip(
        QStringLiteral("Geometric model used to align frames.\n"
                       "Linear transforms (Similarity, Affine, Homography) handle rotation, scale, and perspective.\n"
                       "Polynomial orders correct optical distortion with a two-pass refinement.\n"
                       "Auto selects the best polynomial order automatically."));
    registrationGrid->addWidget(ui.transformationSelector, 0, 3);

    registrationGrid->addWidget(new QLabel(QStringLiteral("Reference"), ui.registrationGroupBox), 1, 0);
    auto* referenceLayout = new QHBoxLayout();
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->setSpacing(t.spacing.sm);
    ui.referenceModeSelector = new QComboBox(ui.registrationGroupBox);
    ui.referenceModeSelector->setObjectName(QStringLiteral("referenceModeSelector"));
    ui.referenceModeSelector->setToolTip(
        QStringLiteral("Manual: use the frame number you specify.\n"
                       "Auto: automatically pick the best frame as reference based on quality."));
    ui.referenceModeSelector->setMinimumWidth(112);
    referenceLayout->addWidget(ui.referenceModeSelector);
    ui.referenceImageValue = new QSpinBox(ui.registrationGroupBox);
    ui.referenceImageValue->setObjectName(QStringLiteral("referenceImageValue"));
    ui.referenceImageValue->setToolTip(
        QStringLiteral("Frame number to use as the reference image (only used in manual mode)"));
    ui.referenceImageValue->setMinimumWidth(88);
    ui.referenceImageValue->setMinimum(1);
    ui.referenceImageValue->setMaximum(100000);
    referenceLayout->addWidget(ui.referenceImageValue);
    registrationGrid->addLayout(referenceLayout, 1, 1);

    registrationGrid->addWidget(new QLabel(QStringLiteral("Min Star Pairs"), ui.registrationGroupBox), 1, 2);
    ui.minStarParirsValue = new QSpinBox(ui.registrationGroupBox);
    ui.minStarParirsValue->setObjectName(QStringLiteral("minStarParirsValue"));
    ui.minStarParirsValue->setToolTip(
        QStringLiteral("Minimum number of matched star pairs required for a successful registration.\n"
                       "Frames with fewer matches will be marked as failed."));
    ui.minStarParirsValue->setMinimum(5);
    ui.minStarParirsValue->setMaximum(10000);
    ui.minStarParirsValue->setValue(10);
    registrationGrid->addWidget(ui.minStarParirsValue, 1, 3);

    registrationGrid->addWidget(new QLabel(QStringLiteral("Max Stars per Frame"), ui.registrationGroupBox), 2, 2);
    ui.maxStarParirsValue = new QSpinBox(ui.registrationGroupBox);
    ui.maxStarParirsValue->setObjectName(QStringLiteral("maxStarParirsValue"));
    ui.maxStarParirsValue->setToolTip(
        QStringLiteral("Maximum number of stars detected in each frame and passed to the matcher as candidates.\n"
                       "Higher values help crowded fields but take longer to process."));
    ui.maxStarParirsValue->setMinimum(10);
    ui.maxStarParirsValue->setMaximum(20000);
    ui.maxStarParirsValue->setValue(2000);
    registrationGrid->addWidget(ui.maxStarParirsValue, 2, 3);

    auto* registrationToggleRow = new QHBoxLayout();
    registrationToggleRow->setSpacing(t.spacing.sm);
    ui.autoSolveUnsolvedFramesCheck =
        new QCheckBox(QStringLiteral("Auto-solve unsolved frames"), ui.registrationGroupBox);
    ui.autoSolveUnsolvedFramesCheck->setObjectName(QStringLiteral("autoSolveUnsolvedFramesCheck"));
    ui.autoSolveUnsolvedFramesCheck->setToolTip(
        QStringLiteral("Automatically plate-solve frames that lack WCS astrometric metadata before registration"));
    registrationToggleRow->addWidget(ui.autoSolveUnsolvedFramesCheck);
    ui.enrichWithGaiaCatalogCheck = new QCheckBox(QStringLiteral("Enrich with Gaia catalog"), ui.registrationGroupBox);
    ui.enrichWithGaiaCatalogCheck->setObjectName(QStringLiteral("enrichWithGaiaCatalogCheck"));
    ui.enrichWithGaiaCatalogCheck->setToolTip(
        QStringLiteral("After registration, match detected stars against the Gaia catalog to compute\n"
                       "photometric zero point, limiting magnitude, and catalog-matched star count.\n"
                       "Requires plate-solved frames (WCS metadata) and a configured Gaia catalog."));
    ui.enrichWithGaiaCatalogCheck->setChecked(true);
    registrationToggleRow->addWidget(ui.enrichWithGaiaCatalogCheck);
    ui.chromaticAberrationCorrectionCheck =
        new QCheckBox(QStringLiteral("Chromatic aberration correction"), ui.registrationGroupBox);
    ui.chromaticAberrationCorrectionCheck->setObjectName(QStringLiteral("chromaticAberrationCorrectionCheck"));
    ui.chromaticAberrationCorrectionCheck->setToolTip(
        QStringLiteral("Correct color fringing caused by the lens not focusing all wavelengths to the same point"));
    registrationToggleRow->addWidget(ui.chromaticAberrationCorrectionCheck);
    registrationToggleRow->addStretch(1);
    registrationLayout->addLayout(registrationToggleRow);

    auto* registrationToggleRow2 = new QHBoxLayout();
    registrationToggleRow2->setSpacing(t.spacing.sm);
    ui.reprocessAlreadyRegisteredmages =
        new QCheckBox(QStringLiteral("Skip already-registered frames"), ui.registrationGroupBox);
    ui.reprocessAlreadyRegisteredmages->setObjectName(QStringLiteral("reprocessAlreadyRegisteredmages"));
    ui.reprocessAlreadyRegisteredmages->setToolTip(
        QStringLiteral("When enabled, frames that were successfully registered previously will not be re-processed"));
    registrationToggleRow2->addWidget(ui.reprocessAlreadyRegisteredmages);
    registrationToggleRow2->addStretch(1);
    ui.clearRegisteredDataButton = new QPushButton(QStringLiteral("Clear Registered Data"), ui.registrationGroupBox);
    ui.clearRegisteredDataButton->setObjectName(QStringLiteral("clearRegisteredDataButton"));
    ui.clearRegisteredDataButton->setProperty("anButtonVariant", QStringLiteral("danger"));
    ui.clearRegisteredDataButton->setToolTip(QStringLiteral("Discard all registration results and start fresh"));
    registrationToggleRow2->addWidget(ui.clearRegisteredDataButton);
    registrationLayout->addLayout(registrationToggleRow2);

    ui.registerButton = new QPushButton(QStringLiteral("Register"), ui.registrationGroupBox);
    ui.registerButton->setObjectName(QStringLiteral("registerButton"));
    ui.registerButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    ui.registerButton->setMinimumHeight(50);
    registrationLayout->addWidget(ui.registerButton);

    ui.alignmentGroupBox = new an::ui::AnCardWidget(QStringLiteral("Alignment"), parent);
    rightColumn->addWidget(ui.alignmentGroupBox);
    auto* alignmentLayout = new QVBoxLayout();
    alignmentLayout->setSpacing(t.spacing.sm);
    alignmentLayout->setContentsMargins(0, 0, 0, 0);
    ui.alignmentGroupBox->setContentLayout(alignmentLayout);

    auto* modeLayout = new QHBoxLayout();
    modeLayout->setSpacing(t.spacing.sm);
    ui.alignModeStandard = new QRadioButton(QStringLiteral("Standard"), ui.alignmentGroupBox);
    ui.alignModeStandard->setObjectName(QStringLiteral("alignModeStandard"));
    ui.alignModeStandard->setChecked(true);
    ui.alignModeStandard->setToolTip(QStringLiteral("Align all frames to the reference frame's field of view"));
    ui.alignModeMosaic = new QRadioButton(QStringLiteral("Mosaic"), ui.alignmentGroupBox);
    ui.alignModeMosaic->setObjectName(QStringLiteral("alignModeMosaic"));
    ui.alignModeMosaic->setToolTip(
        QStringLiteral("Combine frames into a wider mosaic, expanding the canvas to include all fields of view"));
    ui.alignModeIntersection = new QRadioButton(QStringLiteral("Intersection"), ui.alignmentGroupBox);
    ui.alignModeIntersection->setObjectName(QStringLiteral("alignModeIntersection"));
    ui.alignModeIntersection->setToolTip(QStringLiteral("Crop the output to only the area covered by all frames"));
    modeLayout->addWidget(ui.alignModeStandard);
    modeLayout->addWidget(ui.alignModeMosaic);
    modeLayout->addWidget(ui.alignModeIntersection);
    modeLayout->addStretch(1);
    alignmentLayout->addLayout(modeLayout);

    auto* interpolationLayout = new QHBoxLayout();
    interpolationLayout->setSpacing(t.spacing.sm);
    interpolationLayout->addWidget(new QLabel(QStringLiteral("Interpolation"), ui.alignmentGroupBox));
    ui.interpolationSelector = new QComboBox(ui.alignmentGroupBox);
    ui.interpolationSelector->setObjectName(QStringLiteral("interpolationSelector"));
    ui.interpolationSelector->setToolTip(
        QStringLiteral("Pixel interpolation method used when transforming frames during alignment.\n"
                       "Lanczos produces the sharpest results; Bilinear is faster but slightly softer."));
    interpolationLayout->addWidget(ui.interpolationSelector);
    interpolationLayout->addStretch(1);
    alignmentLayout->addLayout(interpolationLayout);

    ui.alignButton = new QPushButton(QStringLiteral("Align"), ui.alignmentGroupBox);
    ui.alignButton->setObjectName(QStringLiteral("alignButton"));
    ui.alignButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    ui.alignButton->setMinimumHeight(50);
    alignmentLayout->addWidget(ui.alignButton);

    // --- Extended Settings (shared between Registration and Alignment) ---
    ui.advancedSettingsButton = new QPushButton(QStringLiteral("Extended Settings..."), parent);
    ui.advancedSettingsButton->setObjectName(QStringLiteral("advancedSettingsButton"));
    ui.advancedSettingsButton->setToolTip(
        QStringLiteral("Open the full settings dialog for star detection, debayer, CFA pattern, "
                       "comet tracking, and other advanced registration and alignment options"));
    applySecondaryButton(ui.advancedSettingsButton);
    rightColumn->addWidget(ui.advancedSettingsButton);

    // --- Output Directory (shared between Registration and Alignment) ---
    ui.outputDirectoryCard = new an::ui::AnCardWidget(QStringLiteral("Output Directory"), parent);
    rightColumn->addWidget(ui.outputDirectoryCard);
    auto* outputLayout = new QHBoxLayout();
    outputLayout->setSpacing(t.spacing.sm);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    ui.outputDirectory = new QLineEdit(ui.outputDirectoryCard->contentWidget());
    ui.outputDirectory->setObjectName(QStringLiteral("outputDirectory"));
    ui.outputDirectory->setReadOnly(true);
    outputLayout->addWidget(ui.outputDirectory, 1);
    ui.outputDirectoryButton = new QPushButton(QStringLiteral("Browse"), ui.outputDirectoryCard->contentWidget());
    ui.outputDirectoryButton->setObjectName(QStringLiteral("outputDirectoryButton"));
    applySecondaryButton(ui.outputDirectoryButton);
    outputLayout->addWidget(ui.outputDirectoryButton);
    ui.outputDirectoryCard->setContentLayout(outputLayout);

    ui.showInfoLabel = new QLabel(parent);
    ui.showInfoLabel->setObjectName(QStringLiteral("showInfoLabel"));
    ui.showInfoLabel->setAlignment(Qt::AlignCenter);
    ui.showInfoLabel->setWordWrap(true);
    ui.showInfoLabel->setTextFormat(Qt::PlainText);
    rightColumn->addWidget(ui.showInfoLabel);
    rightColumn->addStretch(1);

    // --- Scrollable bottom section (Frame List, Advanced) ---
    auto* scrollArea = new QScrollArea(parent);
    scrollArea->setObjectName(QStringLiteral("frameListScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumHeight(0);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    ui.verticalLayout_Main->addWidget(scrollArea, 2);

    ui.tab1 = new QWidget(scrollArea);
    ui.tab1->setObjectName(QStringLiteral("tab1"));
    scrollArea->setWidget(ui.tab1);

    ui.verticalLayoutTab1_Main = new QVBoxLayout(ui.tab1);
    ui.verticalLayoutTab1_Main->setObjectName(QStringLiteral("verticalLayoutTab1_Main"));
    ui.verticalLayoutTab1_Main->setSpacing(t.spacing.md);
    ui.verticalLayoutTab1_Main->setContentsMargins(0, 0, 0, 0);

    ui.frameListCard = new an::ui::AnCardWidget(QStringLiteral("Frame List"), ui.tab1);
    ui.verticalLayoutTab1_Main->addWidget(ui.frameListCard, 1);
    ui.tableLayout = new QVBoxLayout();
    ui.tableLayout->setObjectName(QStringLiteral("tableLayout"));
    ui.tableLayout->setSpacing(t.spacing.sm);
    ui.tableLayout->setContentsMargins(0, 0, 0, 0);
    ui.frameListCard->setContentLayout(ui.tableLayout);

    ui.tableScrollArea = new QScrollArea(ui.frameListCard->contentWidget());
    ui.tableScrollArea->setObjectName(QStringLiteral("tableScrollArea"));
    ui.tableScrollArea->setWidgetResizable(true);
    ui.tableScrollArea->setMinimumHeight(0);
    auto* tableContainer = new QWidget(ui.tableScrollArea);
    auto* tableContainerLayout = new QVBoxLayout(tableContainer);
    tableContainerLayout->setContentsMargins(0, 0, 0, 0);
    tableContainerLayout->setSpacing(0);
    ui.tableWidget = new TableWidget(tableContainer);
    ui.tableWidget->setObjectName(QStringLiteral("tableWidget"));
    ui.tableWidget->setMinimumHeight(0);
    // The blink comparator and Remove Selected act on every frame selected, so several rows can be.
    ui.tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableContainerLayout->addWidget(ui.tableWidget);
    ui.tableScrollArea->setWidget(tableContainer);
    ui.tableLayout->addWidget(ui.tableScrollArea);

    auto* frameButtonRow = new QHBoxLayout();
    frameButtonRow->setSpacing(t.spacing.sm);
    ui.addFiles = new QPushButton(QStringLiteral("Add"), ui.frameListCard);
    ui.addFiles->setObjectName(QStringLiteral("addFiles"));
    applySecondaryButton(ui.addFiles);
    frameButtonRow->addWidget(ui.addFiles);
    ui.addFolder = new QPushButton(QStringLiteral("Add Folder"), ui.frameListCard);
    ui.addFolder->setObjectName(QStringLiteral("addFolder"));
    applySecondaryButton(ui.addFolder);
    frameButtonRow->addWidget(ui.addFolder);
    ui.removeSelected = new QPushButton(QStringLiteral("Remove"), ui.frameListCard);
    ui.removeSelected->setObjectName(QStringLiteral("removeSelected"));
    applySecondaryButton(ui.removeSelected);
    frameButtonRow->addWidget(ui.removeSelected);
    ui.removeFailedImagesButton = new QPushButton(QStringLiteral("Remove Failed"), ui.frameListCard);
    ui.removeFailedImagesButton->setObjectName(QStringLiteral("removeFailedImagesButton"));
    ui.removeFailedImagesButton->setProperty("anButtonVariant", QStringLiteral("danger"));
    frameButtonRow->addWidget(ui.removeFailedImagesButton);
    ui.moveUpButton = new QPushButton(ui.frameListCard);
    ui.moveUpButton->setObjectName(QStringLiteral("moveUpButton"));
    applySecondaryButton(ui.moveUpButton);
    frameButtonRow->addWidget(ui.moveUpButton);
    ui.moveDownButton = new QPushButton(ui.frameListCard);
    ui.moveDownButton->setObjectName(QStringLiteral("moveDownButton"));
    applySecondaryButton(ui.moveDownButton);
    frameButtonRow->addWidget(ui.moveDownButton);
    ui.nextFailedButton = new QPushButton(QStringLiteral("Next Failed"), ui.frameListCard);
    ui.nextFailedButton->setObjectName(QStringLiteral("nextFailedButton"));
    applySecondaryButton(ui.nextFailedButton);
    frameButtonRow->addWidget(ui.nextFailedButton);
    ui.blinkCompareButton = new QPushButton(QStringLiteral("Blink Compare..."), ui.frameListCard);
    ui.blinkCompareButton->setObjectName(QStringLiteral("blinkCompareButton"));
    applySecondaryButton(ui.blinkCompareButton);
    frameButtonRow->addWidget(ui.blinkCompareButton);
    frameButtonRow->addStretch(1);
    frameButtonRow->addWidget(new QLabel(QStringLiteral("Current"), ui.frameListCard));
    ui.currentImageLabel = new QLabel(ui.frameListCard);
    ui.currentImageLabel->setObjectName(QStringLiteral("currentImageLabel"));
    frameButtonRow->addWidget(ui.currentImageLabel);
    frameButtonRow->addWidget(new QLabel(QStringLiteral("Registered"), ui.frameListCard));
    ui.registeredImagesLabel = new QLabel(ui.frameListCard);
    ui.registeredImagesLabel->setObjectName(QStringLiteral("registeredImagesLabel"));
    frameButtonRow->addWidget(ui.registeredImagesLabel);
    frameButtonRow->addWidget(new QLabel(QStringLiteral("Reference"), ui.frameListCard));
    ui.referenceImageLabel = new QLabel(ui.frameListCard);
    ui.referenceImageLabel->setObjectName(QStringLiteral("referenceImageLabel"));
    frameButtonRow->addWidget(ui.referenceImageLabel);
    ui.clearAll = new QPushButton(QStringLiteral("Clear All"), ui.frameListCard);
    ui.clearAll->setObjectName(QStringLiteral("clearAll"));
    ui.clearAll->setProperty("anButtonVariant", QStringLiteral("danger"));
    frameButtonRow->addWidget(ui.clearAll);
    ui.tableLayout->addLayout(frameButtonRow);

    ui.advancedCard = new an::ui::AnCardWidget(QStringLiteral("Advanced"), ui.tab1);
    ui.verticalLayoutTab1_Main->addWidget(ui.advancedCard);
    ui.verticalLayoutTab2_Main = new QVBoxLayout();
    ui.verticalLayoutTab2_Main->setObjectName(QStringLiteral("verticalLayoutTab2_Main"));
    ui.verticalLayoutTab2_Main->setSpacing(t.spacing.md);
    ui.verticalLayoutTab2_Main->setContentsMargins(0, 0, 0, 0);
    ui.advancedCard->setContentLayout(ui.verticalLayoutTab2_Main);
    ui.tab2 = ui.advancedCard->contentWidget();

    ui.qualityThresholdSection =
        new an::ui::AnCollapsibleSection(QStringLiteral("Quality Thresholds"), ui.advancedCard->contentWidget());
    ui.qualityThresholdSection->setExpanded(false);
    auto* thresholdLayout = new QFormLayout();
    thresholdLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    thresholdLayout->setHorizontalSpacing(t.spacing.md);
    thresholdLayout->setVerticalSpacing(t.spacing.sm);
    ui.qualityMinStarsValue = new QSpinBox(ui.qualityThresholdSection->contentWidget());
    ui.qualityMinStarsValue->setToolTip(
        QStringLiteral("Exclude frames with fewer detected stars than this value (0 = no minimum)"));
    ui.qualityMinStarsValue->setMinimum(0);
    ui.qualityMinStarsValue->setMaximum(100000);
    ui.qualityMaxFwhmValue = new QDoubleSpinBox(ui.qualityThresholdSection->contentWidget());
    ui.qualityMaxFwhmValue->setToolTip(
        QStringLiteral("Exclude frames where the average star FWHM exceeds this value (0 = no limit).\n"
                       "Lower FWHM means tighter, better-focused stars."));
    ui.qualityMaxFwhmValue->setDecimals(3);
    ui.qualityMaxFwhmValue->setMinimum(0.0);
    ui.qualityMaxFwhmValue->setMaximum(999.0);
    ui.qualityMaxFwhmValue->setSingleStep(0.1);
    ui.qualityMaxEccentricityValue = new QDoubleSpinBox(ui.qualityThresholdSection->contentWidget());
    ui.qualityMaxEccentricityValue->setToolTip(
        QStringLiteral("Exclude frames where average star eccentricity exceeds this value (0 = no limit).\n"
                       "Values closer to 0 mean rounder stars; closer to 1 means elongated (e.g. trailing)."));
    ui.qualityMaxEccentricityValue->setDecimals(3);
    ui.qualityMaxEccentricityValue->setMinimum(0.0);
    ui.qualityMaxEccentricityValue->setMaximum(1.0);
    ui.qualityMaxEccentricityValue->setSingleStep(0.01);
    thresholdLayout->addRow(QStringLiteral("Min Stars"), ui.qualityMinStarsValue);
    thresholdLayout->addRow(QStringLiteral("Max FWHM"), ui.qualityMaxFwhmValue);
    thresholdLayout->addRow(QStringLiteral("Max Eccentricity"), ui.qualityMaxEccentricityValue);
    auto* thresholdButtons = new QHBoxLayout();
    ui.applyThresholdsButton =
        new QPushButton(QStringLiteral("Apply Thresholds"), ui.qualityThresholdSection->contentWidget());
    applySecondaryButton(ui.applyThresholdsButton);
    ui.resetThresholdsButton = new QPushButton(QStringLiteral("Reset"), ui.qualityThresholdSection->contentWidget());
    applySecondaryButton(ui.resetThresholdsButton);
    thresholdButtons->addWidget(ui.applyThresholdsButton);
    thresholdButtons->addWidget(ui.resetThresholdsButton);
    thresholdButtons->addStretch(1);
    thresholdLayout->addRow(thresholdButtons);
    ui.qualityThresholdSection->setContentLayout(thresholdLayout);
    ui.verticalLayoutTab2_Main->addWidget(ui.qualityThresholdSection);

    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    ui.verticalLayout_Main->addWidget(ui.buttonBox);

    return ui;
}

} // namespace an::ui::processes
