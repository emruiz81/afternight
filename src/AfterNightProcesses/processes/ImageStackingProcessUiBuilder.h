// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageStackingProcessUiBuilder.h
 * @brief Programmatic UI builder for the standalone ImageStackingProcess.
 */
#pragma once

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "theme/AnDesignTokens.h"
#include "widgets/TableWidget.h"

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct ImageStackingProcessUiElements
{
    void setupUi(QWidget* parent);

    QVBoxLayout* verticalLayout_Main = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;
    QVBoxLayout* verticalLayout_5 = nullptr;
    QVBoxLayout* frameTableLayout = nullptr;
    QVBoxLayout* groupTableLayout = nullptr;
    QScrollArea* tableScrollArea = nullptr;
    QScrollArea* groupTableScrollArea = nullptr;

    an::ui::AnCardWidget* stackingSettingsCard = nullptr;
    an::ui::AnCardWidget* outputCard = nullptr;
    an::ui::AnCollapsibleSection* frameGroupsCard = nullptr;
    an::ui::AnCardWidget* frameListCard = nullptr;
    an::ui::AnCollapsibleSection* advancedCard = nullptr;

    QWidget* imagePreviewWidget = nullptr;

    TableWidget* tableWidget = nullptr;
    TableWidget* groupTable = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    QPushButton* addFiles = nullptr;
    QPushButton* addFolder = nullptr;
    QPushButton* removeSelected = nullptr;
    QPushButton* moveUpButton = nullptr;
    QPushButton* moveDownButton = nullptr;
    QPushButton* clearAll = nullptr;
    QPushButton* stackImagesButton = nullptr;
    QPushButton* outputDirectoryButton = nullptr;

    QLabel* currentImageLabel = nullptr;
    QLabel* totalImagesLabel = nullptr;
    QLabel* sigmaLowLabel = nullptr;
    QLabel* sigmaHighLabel = nullptr;
    QLabel* rejectionMapsPathLabel = nullptr;

    QComboBox* stackMethodSelector = nullptr;
    QComboBox* cometStackingModeSelector = nullptr;
    QComboBox* weightingSelector = nullptr;
    QComboBox* normalizationMode = nullptr;
    QComboBox* scaleEstimatorSelector = nullptr;
    QComboBox* rejectionMethodSelector = nullptr;
    QComboBox* cfaPattern = nullptr;
    QComboBox* debayerMethod = nullptr;

    QDoubleSpinBox* sigmaLowValue = nullptr;
    QDoubleSpinBox* sigmaHighValue = nullptr;
    QDoubleSpinBox* exposureTolerancePercentValue = nullptr;

    QCheckBox* groupByFilter = nullptr;
    QCheckBox* groupBySession = nullptr;
    QCheckBox* groupByExposure = nullptr;
    QCheckBox* openAfterStackCheck = nullptr;
    QCheckBox* sumExposureTimesCheckBox = nullptr;
    QCheckBox* process64BitsImageCheckbox = nullptr;
    QCheckBox* createRejectionMapsCheck = nullptr;
    QCheckBox* mergeRejectionMapsCheck = nullptr;

    QLineEdit* outputDirectoryEdit = nullptr;
    QLineEdit* outputImageNameEdit = nullptr;

    an::ui::AnInfoBanner* weightingBanner = nullptr;
    an::ui::AnInfoBanner* localNormalizationBanner = nullptr;
    QPushButton* localNormalizationConfigureButton = nullptr;
    an::ui::AnCollapsibleSection* rejectionSection = nullptr;

    QGroupBox* limitMemoryUsageGroupBox = nullptr;
    QGroupBox* debayerCFAGroupBox = nullptr;

    QSpinBox* maxMemoryMBValue = nullptr;
};

inline ImageStackingProcessUiElements buildImageStackingProcessUi(QWidget* parent)
{
    ImageStackingProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    auto applySecondaryButton = [](QPushButton* button) {
        if (button)
        {
            button->setProperty("anButtonVariant", QStringLiteral("secondary"));
        }
    };

    parent->setObjectName(QStringLiteral("ImageStackingProcess"));
    parent->setWindowModality(Qt::NonModal);
    parent->resize(1460, 1080);
    parent->setMinimumSize(QSize(1200, 520));
    parent->setWindowTitle(QStringLiteral("Image Stacking"));

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

    ui.stackingSettingsCard = new an::ui::AnCardWidget(QStringLiteral("Stacking Settings"), parent);
    rightColumn->addWidget(ui.stackingSettingsCard);
    auto* stackingLayout = new QVBoxLayout();
    stackingLayout->setSpacing(t.spacing.sm);
    stackingLayout->setContentsMargins(0, 0, 0, 0);
    ui.stackingSettingsCard->setContentLayout(stackingLayout);

    auto* settingsGrid = new QGridLayout();
    settingsGrid->setHorizontalSpacing(t.spacing.sm);
    settingsGrid->setVerticalSpacing(t.spacing.sm);
    stackingLayout->addLayout(settingsGrid);

    settingsGrid->addWidget(new QLabel(QStringLiteral("Method"), ui.stackingSettingsCard), 0, 0);
    ui.stackMethodSelector = new QComboBox(ui.stackingSettingsCard);
    ui.stackMethodSelector->setToolTip(
        QStringLiteral("Pixel combination method.\n"
                       "Average: mean of all pixels (best SNR with rejection).\n"
                       "Sum: additive combination (preserves relative flux).\n"
                       "Median: robust to outliers but lower SNR.\n"
                       "Min / Max: keep extreme pixel values.\n"
                       "Auto-Adaptive Weighted Average: automatic per-pixel weighting."));
    settingsGrid->addWidget(ui.stackMethodSelector, 0, 1);

    settingsGrid->addWidget(new QLabel(QStringLiteral("Normalization"), ui.stackingSettingsCard), 0, 2);
    ui.normalizationMode = new QComboBox(ui.stackingSettingsCard);
    ui.normalizationMode->setToolTip(QStringLiteral("How frames are matched before stacking.\n"
                                                    "None: no normalization.\n"
                                                    "Additive / Multiplicative: match global statistics.\n"
                                                    "With Scaling: also matches dispersion (recommended).\n"
                                                    "Local: spatially-varying normalization for uneven backgrounds."));
    settingsGrid->addWidget(ui.normalizationMode, 0, 3);

    settingsGrid->addWidget(new QLabel(QStringLiteral("Weighting"), ui.stackingSettingsCard), 1, 0);
    ui.weightingSelector = new QComboBox(ui.stackingSettingsCard);
    ui.weightingSelector->setToolTip(QStringLiteral("Per-frame quality weighting for the stacking combination.\n"
                                                    "None (Equal): all frames contribute equally.\n"
                                                    "FWHM / Stars / SNR: weight by quality metric\n"
                                                    "(requires registration metrics database).\n"
                                                    "Noise: weight by each frame's measured background noise.\n"
                                                    "Exposure: weight proportional to exposure time."));
    settingsGrid->addWidget(ui.weightingSelector, 1, 1);

    settingsGrid->addWidget(new QLabel(QStringLiteral("Scale Estimator"), ui.stackingSettingsCard), 1, 2);
    ui.scaleEstimatorSelector = new QComboBox(ui.stackingSettingsCard);
    ui.scaleEstimatorSelector->setToolTip(
        QStringLiteral("Method used to estimate pixel dispersion for normalization scaling.\n"
                       "IKSS: Iterative K-Sigma-Sigma (robust, recommended).\n"
                       "MAD: Median Absolute Deviation (faster, slightly less robust)."));
    settingsGrid->addWidget(ui.scaleEstimatorSelector, 1, 3);

    settingsGrid->addWidget(new QLabel(QStringLiteral("Comet Mode"), ui.stackingSettingsCard), 2, 0);
    ui.cometStackingModeSelector = new QComboBox(ui.stackingSettingsCard);
    ui.cometStackingModeSelector->setToolTip(
        QStringLiteral("Input role used for stacking.\n"
                       "Stars: stack the selected frames directly.\n"
                       "Comet: consume active comet_aligned registration products."));
    settingsGrid->addWidget(ui.cometStackingModeSelector, 2, 1, 1, 3);

    ui.weightingBanner = new an::ui::AnInfoBanner(
        QStringLiteral("Registration metrics are not available for the selected weighting mode."),
        an::ui::AnInfoBanner::Tone::Warning, ui.stackingSettingsCard->contentWidget());
    ui.weightingBanner->hide();
    stackingLayout->addWidget(ui.weightingBanner);

    ui.localNormalizationBanner =
        new an::ui::AnInfoBanner(QStringLiteral("Local normalization works best with aligned/registered frames."),
                                 an::ui::AnInfoBanner::Tone::Info, ui.stackingSettingsCard->contentWidget());
    ui.localNormalizationBanner->hide();
    stackingLayout->addWidget(ui.localNormalizationBanner);

    ui.localNormalizationConfigureButton =
        new QPushButton(QStringLiteral("Configure Local Normalization..."), ui.stackingSettingsCard);
    applySecondaryButton(ui.localNormalizationConfigureButton);
    ui.localNormalizationConfigureButton->hide();
    stackingLayout->addWidget(ui.localNormalizationConfigureButton);

    ui.rejectionSection = new an::ui::AnCollapsibleSection(QStringLiteral("Rejection Settings"),
                                                           ui.stackingSettingsCard->contentWidget());
    ui.rejectionSection->setExpanded(false);
    auto* rejectionGrid = new QGridLayout();
    rejectionGrid->setHorizontalSpacing(t.spacing.sm);
    rejectionGrid->setVerticalSpacing(t.spacing.sm);
    ui.sigmaLowLabel = new QLabel(QStringLiteral("Sigma Low"), ui.rejectionSection->contentWidget());
    ui.sigmaHighLabel = new QLabel(QStringLiteral("Sigma High"), ui.rejectionSection->contentWidget());
    rejectionGrid->addWidget(new QLabel(QStringLiteral("Rejection Method"), ui.rejectionSection->contentWidget()), 0,
                             0);
    ui.rejectionMethodSelector = new QComboBox(ui.rejectionSection->contentWidget());
    ui.rejectionMethodSelector->setToolTip(
        QStringLiteral("Algorithm for detecting and rejecting outlier pixels.\n"
                       "Sigma Clipping: classic sigma-based rejection.\n"
                       "MAD Clipping: uses Median Absolute Deviation (more robust).\n"
                       "Winsorized Sigma: iterative Winsorization before sigma test.\n"
                       "Percentile: rejects a sample further below the median than the low\n"
                       "fraction of the median, or further above it than the high fraction.\n"
                       "GESDT: Generalized ESD Test for multiple outliers.\n"
                       "Linear Fit: fits a line and rejects deviations."));
    rejectionGrid->addWidget(ui.rejectionMethodSelector, 0, 1, 1, 3);
    rejectionGrid->addWidget(ui.sigmaLowLabel, 1, 0);
    ui.sigmaLowValue = new QDoubleSpinBox(ui.rejectionSection->contentWidget());
    ui.sigmaLowValue->setToolTip(QStringLiteral("Lower rejection threshold. Pixels below this many standard\n"
                                                "deviations from the median are rejected. Lower values reject more."));
    ui.sigmaLowValue->setDecimals(3);
    ui.sigmaLowValue->setSingleStep(0.1);
    // Every rejection reads a positive threshold; the window narrows the maximum for one that reads fractions.
    ui.sigmaLowValue->setMinimum(0.001);
    ui.sigmaLowValue->setMaximum(1000.0);
    rejectionGrid->addWidget(ui.sigmaLowValue, 1, 1);
    rejectionGrid->addWidget(ui.sigmaHighLabel, 1, 2);
    ui.sigmaHighValue = new QDoubleSpinBox(ui.rejectionSection->contentWidget());
    ui.sigmaHighValue->setToolTip(QStringLiteral("Upper rejection threshold. Pixels above this many standard\n"
                                                 "deviations from the median are rejected. Lower values reject more."));
    ui.sigmaHighValue->setDecimals(3);
    ui.sigmaHighValue->setSingleStep(0.1);
    ui.sigmaHighValue->setMinimum(0.001);
    ui.sigmaHighValue->setMaximum(1000.0);
    rejectionGrid->addWidget(ui.sigmaHighValue, 1, 3);
    ui.createRejectionMapsCheck =
        new QCheckBox(QStringLiteral("Save rejection maps"), ui.rejectionSection->contentWidget());
    ui.createRejectionMapsCheck->setToolTip(
        QStringLiteral("Save images showing which pixels were rejected at each position."));
    ui.mergeRejectionMapsCheck =
        new QCheckBox(QStringLiteral("Merge rejection maps"), ui.rejectionSection->contentWidget());
    ui.mergeRejectionMapsCheck->setToolTip(QStringLiteral("Combine low and high rejection maps into a single image "
                                                          "instead of separate files."));
    rejectionGrid->addWidget(ui.createRejectionMapsCheck, 2, 0, 1, 2);
    rejectionGrid->addWidget(ui.mergeRejectionMapsCheck, 2, 2, 1, 2);
    ui.rejectionMapsPathLabel =
        new QLabel(QStringLiteral("Rejection maps will be written next to the stacked outputs."),
                   ui.rejectionSection->contentWidget());
    ui.rejectionMapsPathLabel->setWordWrap(true);
    ui.rejectionMapsPathLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    rejectionGrid->addWidget(ui.rejectionMapsPathLabel, 3, 0, 1, 4);
    ui.rejectionSection->setContentLayout(rejectionGrid);
    stackingLayout->addWidget(ui.rejectionSection);

    ui.outputCard = new an::ui::AnCardWidget(QStringLiteral("Output"), parent);
    rightColumn->addWidget(ui.outputCard);
    auto* outputLayout = new QVBoxLayout();
    outputLayout->setSpacing(t.spacing.sm);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    ui.outputCard->setContentLayout(outputLayout);

    auto* outputNameLayout = new QHBoxLayout();
    outputNameLayout->setSpacing(t.spacing.sm);
    outputNameLayout->addWidget(new QLabel(QStringLiteral("Name Template"), ui.outputCard));
    ui.outputImageNameEdit = new QLineEdit(ui.outputCard);
    ui.outputImageNameEdit->setToolTip(
        QStringLiteral("Output file name template. Available variables:\n"
                       "  $filter   — filter name (e.g. L, R, Ha). Blank for broadband.\n"
                       "  $session  — session date (e.g. 2024-03-15).\n"
                       "  $exposure — representative exposure time (e.g. 30s, 120s).\n"
                       "  $group    — combined group token (filter_session_exposure).\n\n"
                       "Example: stacked_$filter_$session.fit"));
    outputNameLayout->addWidget(ui.outputImageNameEdit, 1);
    outputLayout->addLayout(outputNameLayout);

    auto* outputDirectoryLayout = new QHBoxLayout();
    outputDirectoryLayout->setSpacing(t.spacing.sm);
    outputDirectoryLayout->addWidget(new QLabel(QStringLiteral("Directory"), ui.outputCard));
    ui.outputDirectoryEdit = new QLineEdit(ui.outputCard);
    ui.outputDirectoryEdit->setReadOnly(true);
    outputDirectoryLayout->addWidget(ui.outputDirectoryEdit, 1);
    ui.outputDirectoryButton = new QPushButton(QStringLiteral("Browse"), ui.outputCard);
    applySecondaryButton(ui.outputDirectoryButton);
    outputDirectoryLayout->addWidget(ui.outputDirectoryButton);
    outputLayout->addLayout(outputDirectoryLayout);

    auto* outputOptionsLayout = new QHBoxLayout();
    outputOptionsLayout->setSpacing(t.spacing.sm);
    ui.process64BitsImageCheckbox = new QCheckBox(QStringLiteral("64-bit"), ui.outputCard);
    ui.process64BitsImageCheckbox->setToolTip(QStringLiteral("Output 64-bit floating point instead of 32-bit.\n"
                                                             "Higher precision but doubles file size."));
    ui.openAfterStackCheck = new QCheckBox(QStringLiteral("Open after stacking"), ui.outputCard);
    ui.openAfterStackCheck->setToolTip(QStringLiteral("Automatically open the stacked result in the main viewer."));
    ui.openAfterStackCheck->setChecked(true);
    ui.sumExposureTimesCheckBox = new QCheckBox(QStringLiteral("Sum exposure times"), ui.outputCard);
    ui.sumExposureTimesCheckBox->setToolTip(QStringLiteral("Write the total accumulated exposure time into the\n"
                                                           "output FITS header instead of a single frame's exposure."));
    outputOptionsLayout->addWidget(ui.process64BitsImageCheckbox);
    outputOptionsLayout->addWidget(ui.openAfterStackCheck);
    outputOptionsLayout->addWidget(ui.sumExposureTimesCheckBox);
    outputOptionsLayout->addStretch(1);
    outputLayout->addLayout(outputOptionsLayout);

    ui.stackImagesButton = new QPushButton(QStringLiteral("Stack Images"), ui.outputCard);
    ui.stackImagesButton->setToolTip(QStringLiteral("Start the stacking process for all frame groups."));
    ui.stackImagesButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    ui.stackImagesButton->setMinimumHeight(50);
    outputLayout->addWidget(ui.stackImagesButton);
    rightColumn->addStretch(1);

    // --- Scrollable bottom section (Frames, Groups, Advanced) ---
    auto* scrollArea = new QScrollArea(parent);
    scrollArea->setObjectName(QStringLiteral("frameListScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumHeight(0);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    ui.verticalLayout_Main->addWidget(scrollArea, 2);

    auto* scrollWidget = new QWidget(scrollArea);
    scrollWidget->setObjectName(QStringLiteral("scrollWidget"));
    scrollArea->setWidget(scrollWidget);

    auto* scrollLayout = new QVBoxLayout(scrollWidget);
    scrollLayout->setSpacing(t.spacing.md);
    scrollLayout->setContentsMargins(0, 0, 0, 0);

    ui.frameListCard = new an::ui::AnCardWidget(QStringLiteral("Frames"), scrollWidget);
    scrollLayout->addWidget(ui.frameListCard, 1);
    ui.frameTableLayout = new QVBoxLayout();
    ui.frameTableLayout->setSpacing(t.spacing.sm);
    ui.frameTableLayout->setContentsMargins(0, 0, 0, 0);
    ui.frameListCard->setContentLayout(ui.frameTableLayout);

    ui.tableScrollArea = new QScrollArea(ui.frameListCard->contentWidget());
    ui.tableScrollArea->setWidgetResizable(true);
    ui.tableScrollArea->setMinimumHeight(0);
    ui.tableScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    ui.tableScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    auto* tableContainer = new QWidget(ui.tableScrollArea);
    auto* tableContainerLayout = new QVBoxLayout(tableContainer);
    tableContainerLayout->setContentsMargins(0, 0, 0, 0);
    tableContainerLayout->setSpacing(0);
    ui.tableWidget = new TableWidget(tableContainer);
    ui.tableWidget->setMinimumHeight(0);
    tableContainerLayout->addWidget(ui.tableWidget);
    ui.tableScrollArea->setWidget(tableContainer);
    ui.frameTableLayout->addWidget(ui.tableScrollArea);

    auto* frameButtons = new QHBoxLayout();
    frameButtons->setSpacing(t.spacing.sm);
    ui.addFiles = new QPushButton(QStringLiteral("Add Files"), ui.frameListCard);
    ui.addFolder = new QPushButton(QStringLiteral("Add Folder"), ui.frameListCard);
    ui.removeSelected = new QPushButton(QStringLiteral("Remove Selected"), ui.frameListCard);
    ui.moveUpButton = new QPushButton(ui.frameListCard);
    ui.moveDownButton = new QPushButton(ui.frameListCard);
    ui.moveUpButton->setToolTip(QStringLiteral("Move frame up"));
    ui.moveDownButton->setToolTip(QStringLiteral("Move frame down"));
    ui.clearAll = new QPushButton(QStringLiteral("Clear All"), ui.frameListCard);
    applySecondaryButton(ui.addFiles);
    applySecondaryButton(ui.addFolder);
    applySecondaryButton(ui.removeSelected);
    applySecondaryButton(ui.moveUpButton);
    applySecondaryButton(ui.moveDownButton);
    ui.clearAll->setProperty("anButtonVariant", QStringLiteral("danger"));
    frameButtons->addWidget(ui.addFiles);
    frameButtons->addWidget(ui.addFolder);
    frameButtons->addWidget(ui.removeSelected);
    frameButtons->addWidget(ui.moveUpButton);
    frameButtons->addWidget(ui.moveDownButton);
    frameButtons->addStretch(1);
    frameButtons->addWidget(new QLabel(QStringLiteral("Current Image"), ui.frameListCard));
    ui.currentImageLabel = new QLabel(ui.frameListCard);
    ui.currentImageLabel->setMinimumWidth(32);
    frameButtons->addWidget(ui.currentImageLabel);
    frameButtons->addWidget(new QLabel(QStringLiteral("Total Images"), ui.frameListCard));
    ui.totalImagesLabel = new QLabel(ui.frameListCard);
    ui.totalImagesLabel->setMinimumWidth(32);
    frameButtons->addWidget(ui.totalImagesLabel);
    frameButtons->addWidget(ui.clearAll);
    ui.frameTableLayout->addLayout(frameButtons);

    ui.frameGroupsCard = new an::ui::AnCollapsibleSection(QStringLiteral("Frame Groups"), scrollWidget);
    ui.frameGroupsCard->setExpanded(true);
    scrollLayout->addWidget(ui.frameGroupsCard);
    auto* groupsLayout = new QVBoxLayout();
    groupsLayout->setSpacing(t.spacing.sm);
    groupsLayout->setContentsMargins(0, 0, 0, 0);
    ui.frameGroupsCard->setContentLayout(groupsLayout);

    auto* groupByBox = new QGroupBox(QStringLiteral("Group by"), ui.frameGroupsCard);
    auto* groupByLayout = new QHBoxLayout(groupByBox);
    groupByLayout->setSpacing(t.spacing.sm);
    groupByLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    ui.groupByFilter = new QCheckBox(QStringLiteral("Filter"), groupByBox);
    ui.groupByFilter->setToolTip(QStringLiteral("Create separate stacking groups per filter (e.g. L, R, G, B, Ha)."));
    ui.groupByFilter->setChecked(true);
    ui.groupBySession = new QCheckBox(QStringLiteral("Session"), groupByBox);
    ui.groupBySession->setToolTip(QStringLiteral("Create separate stacking groups per imaging session/date."));
    ui.groupByExposure = new QCheckBox(QStringLiteral("Exposure"), groupByBox);
    ui.groupByExposure->setToolTip(
        QStringLiteral("Create separate stacking groups per exposure time.\n"
                       "Frames with exposure times within the tolerance are grouped together."));
    groupByLayout->addWidget(ui.groupByFilter);
    groupByLayout->addWidget(ui.groupBySession);
    groupByLayout->addWidget(ui.groupByExposure);
    groupByLayout->addSpacing(t.spacing.sm);
    groupByLayout->addWidget(new QLabel(QStringLiteral("Tolerance"), groupByBox));
    ui.exposureTolerancePercentValue = new QDoubleSpinBox(groupByBox);
    ui.exposureTolerancePercentValue->setToolTip(QStringLiteral("Percentage tolerance for exposure time grouping.\n"
                                                                "Frames whose exposure times differ by less than this\n"
                                                                "percentage are placed into the same group."));
    ui.exposureTolerancePercentValue->setDecimals(1);
    ui.exposureTolerancePercentValue->setMinimum(0.0);
    ui.exposureTolerancePercentValue->setMaximum(100.0);
    ui.exposureTolerancePercentValue->setValue(5.0);
    ui.exposureTolerancePercentValue->setSuffix(QStringLiteral("%"));
    groupByLayout->addWidget(ui.exposureTolerancePercentValue);
    groupByLayout->addStretch(1);
    groupsLayout->addWidget(groupByBox);

    ui.groupTableLayout = new QVBoxLayout();
    ui.groupTableLayout->setSpacing(t.spacing.sm);
    ui.groupTableLayout->setContentsMargins(0, 0, 0, 0);
    groupsLayout->addLayout(ui.groupTableLayout);
    ui.groupTableScrollArea = new QScrollArea(ui.frameGroupsCard);
    ui.groupTableScrollArea->setWidgetResizable(true);
    ui.groupTableScrollArea->setMinimumHeight(0);
    ui.groupTableScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    ui.groupTableScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    auto* groupTableContainer = new QWidget(ui.groupTableScrollArea);
    auto* groupTableContainerLayout = new QVBoxLayout(groupTableContainer);
    groupTableContainerLayout->setContentsMargins(0, 0, 0, 0);
    groupTableContainerLayout->setSpacing(0);
    ui.groupTable = new TableWidget(groupTableContainer);
    ui.groupTable->setMinimumHeight(0);
    groupTableContainerLayout->addWidget(ui.groupTable);
    ui.groupTableScrollArea->setWidget(groupTableContainer);
    ui.groupTableLayout->addWidget(ui.groupTableScrollArea);

    ui.advancedCard = new an::ui::AnCollapsibleSection(QStringLiteral("Advanced"), scrollWidget);
    ui.advancedCard->setExpanded(false);
    scrollLayout->addWidget(ui.advancedCard);
    ui.verticalLayout_5 = new QVBoxLayout();
    ui.verticalLayout_5->setObjectName(QStringLiteral("verticalLayout_5"));
    ui.verticalLayout_5->setSpacing(t.spacing.md);
    ui.verticalLayout_5->setContentsMargins(0, 0, 0, 0);
    ui.advancedCard->setContentLayout(ui.verticalLayout_5);

    ui.limitMemoryUsageGroupBox = new QGroupBox(QStringLiteral("Limit Memory Usage"), ui.advancedCard);
    ui.limitMemoryUsageGroupBox->setToolTip(
        QStringLiteral("Cap peak memory usage during stacking. When enabled, frames are\n"
                       "processed in batches that fit within the memory budget."));
    ui.limitMemoryUsageGroupBox->setCheckable(true);
    auto* memoryLayout = new QHBoxLayout(ui.limitMemoryUsageGroupBox);
    memoryLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    memoryLayout->addWidget(new QLabel(QStringLiteral("Max Memory"), ui.limitMemoryUsageGroupBox));
    ui.maxMemoryMBValue = new QSpinBox(ui.limitMemoryUsageGroupBox);
    ui.maxMemoryMBValue->setToolTip(QStringLiteral("Maximum memory (in MB) the stacking process is allowed to use."));
    ui.maxMemoryMBValue->setMinimum(256);
    ui.maxMemoryMBValue->setMaximum(8184);
    ui.maxMemoryMBValue->setMinimumWidth(120);
    ui.maxMemoryMBValue->setValue(1024);
    ui.maxMemoryMBValue->setSuffix(QStringLiteral("MB"));
    memoryLayout->addWidget(ui.maxMemoryMBValue);
    memoryLayout->addStretch(1);
    ui.verticalLayout_5->addWidget(ui.limitMemoryUsageGroupBox);

    ui.debayerCFAGroupBox = new QGroupBox(QStringLiteral("Debayer CFA"), ui.advancedCard);
    ui.debayerCFAGroupBox->setToolTip(QStringLiteral("Enable debayering (demosaicing) of Color Filter Array data.\n"
                                                     "Use this when stacking raw one-shot-colour camera frames\n"
                                                     "that haven't been debayered yet."));
    ui.debayerCFAGroupBox->setCheckable(true);
    ui.debayerCFAGroupBox->setChecked(false);
    auto* debayerLayout = new QGridLayout(ui.debayerCFAGroupBox);
    debayerLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    debayerLayout->setHorizontalSpacing(t.spacing.sm);
    debayerLayout->setVerticalSpacing(t.spacing.sm);
    debayerLayout->addWidget(new QLabel(QStringLiteral("Pattern"), ui.debayerCFAGroupBox), 0, 0);
    ui.cfaPattern = new QComboBox(ui.debayerCFAGroupBox);
    ui.cfaPattern->setToolTip(QStringLiteral("Bayer pattern of the sensor. Auto detects from file metadata."));
    ui.cfaPattern->setMinimumWidth(140);
    debayerLayout->addWidget(ui.cfaPattern, 0, 1);
    debayerLayout->addWidget(new QLabel(QStringLiteral("Method"), ui.debayerCFAGroupBox), 1, 0);
    ui.debayerMethod = new QComboBox(ui.debayerCFAGroupBox);
    ui.debayerMethod->setToolTip(QStringLiteral("Demosaicing algorithm. RCD offers good quality/speed balance.\n"
                                                "AMAZe and LMMSE provide the highest quality but are slower."));
    ui.debayerMethod->setMinimumWidth(140);
    debayerLayout->addWidget(ui.debayerMethod, 1, 1);
    debayerLayout->setColumnStretch(2, 1);
    ui.verticalLayout_5->addWidget(ui.debayerCFAGroupBox);

    ui.buttonBox = new QDialogButtonBox(parent);
    ui.buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Reset);
    ui.verticalLayout_Main->addWidget(ui.buttonBox);

    return ui;
}

inline void ImageStackingProcessUiElements::setupUi(QWidget* parent)
{
    *this = buildImageStackingProcessUi(parent);
}

} // namespace an::ui::processes
