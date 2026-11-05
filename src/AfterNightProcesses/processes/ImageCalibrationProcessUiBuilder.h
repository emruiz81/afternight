// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageCalibrationProcessUiBuilder.h
 * @brief Programmatic UI builder for ImageCalibrationProcess.
 */
#pragma once

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "theme/AnDesignTokens.h"
#include "widgets/TableWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

/**
 * @brief Read-only line edit that accepts a single dropped local file.
 */
class ImageCalibrationDropLineEdit : public QLineEdit
{
public:
    explicit ImageCalibrationDropLineEdit(QWidget* parent = nullptr) : QLineEdit(parent)
    {
        setReadOnly(true);
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event && event->mimeData() && event->mimeData()->hasUrls())
        {
            const auto urls = event->mimeData()->urls();
            if (urls.size() == 1 && urls.front().isLocalFile())
            {
                event->acceptProposedAction();
                return;
            }
        }

        QLineEdit::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if (event && event->mimeData() && event->mimeData()->hasUrls())
        {
            const auto urls = event->mimeData()->urls();
            if (urls.size() == 1 && urls.front().isLocalFile())
            {
                setText(urls.front().toLocalFile());
                event->acceptProposedAction();
                return;
            }
        }

        QLineEdit::dropEvent(event);
    }
};

struct ImageCalibrationProcessUiElements
{
    QVBoxLayout* verticalLayout_Main = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;
    QVBoxLayout* verticalLayout_3 = nullptr;
    QVBoxLayout* tableLayout = nullptr;
    QScrollArea* tableScrollArea = nullptr;

    QGroupBox* outputCard = nullptr;
    an::ui::AnCardWidget* frameListCard = nullptr;

    QWidget* imagePreviewWidget = nullptr;

    QGroupBox* biasGroupBox = nullptr;
    QLineEdit* biasFilePath = nullptr;
    QPushButton* biasFileButton = nullptr;

    QGroupBox* darkGroupBox = nullptr;
    QLineEdit* darkFilePath = nullptr;
    QPushButton* darkFileButton = nullptr;
    QCheckBox* darkOptimizationCheck = nullptr;
    QDoubleSpinBox* darkMultiplicationFactorValue = nullptr;
    an::ui::AnCollapsibleSection* darkAdvancedSection = nullptr;
    QGroupBox* cosmeticCorrectionGroupBox = nullptr;
    QCheckBox* coldSigmaCheck = nullptr;
    QDoubleSpinBox* coldSigmaValue = nullptr;
    QCheckBox* hotSigmaCheck = nullptr;
    QDoubleSpinBox* hotSigmaValue = nullptr;
    QPushButton* estimateCosmeticCorrectionButton = nullptr;

    QGroupBox* flatGroupBox = nullptr;
    QLineEdit* flatFilePath = nullptr;
    QPushButton* flatFileButton = nullptr;
    QCheckBox* normalizeFlatsCheck = nullptr;
    an::ui::AnCollapsibleSection* flatAdvancedSection = nullptr;

    QLineEdit* outputDirectory = nullptr;
    QPushButton* outputDirectoryButton = nullptr;
    QCheckBox* preserveOriginalNameCheck = nullptr;
    QLineEdit* filenameSuffixEdit = nullptr;
    QComboBox* outputFormatSelector = nullptr;
    QLabel* outputPrecisionLabel = nullptr;
    QComboBox* outputPrecisionSelector = nullptr;
    an::ui::AnCollapsibleSection* outputAdvancedSection = nullptr;
    QDoubleSpinBox* pedestalValue = nullptr;
    QPushButton* calibrateImagesButton = nullptr;

    TableWidget* tableWidget = nullptr;
    QPushButton* addFiles = nullptr;
    QPushButton* addFolder = nullptr;
    QPushButton* removeSelected = nullptr;
    QPushButton* moveUpButton = nullptr;
    QPushButton* moveDownButton = nullptr;
    QLabel* currentImageLabel = nullptr;
    QLabel* totalImagesLabel = nullptr;
    QPushButton* clearAll = nullptr;

    QDialogButtonBox* buttonBox = nullptr;
};

inline ImageCalibrationProcessUiElements buildImageCalibrationProcessUi(QWidget* parent)
{
    ImageCalibrationProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ImageCalibrationProcess"));
    parent->setWindowModality(Qt::NonModal);
    parent->resize(1398, 1042);
    parent->setMinimumSize(QSize(1200, 520));
    parent->setWindowTitle(QStringLiteral("Image Calibration"));

    ui.verticalLayout_Main = new QVBoxLayout(parent);
    ui.verticalLayout_Main->setObjectName(QStringLiteral("verticalLayout_Main"));
    ui.verticalLayout_Main->setSpacing(t.spacing.md);
    ui.verticalLayout_Main->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(t.spacing.md);
    ui.verticalLayout_Main->addLayout(topLayout, 2);

    auto* previewColumnLayout = new QVBoxLayout();
    previewColumnLayout->setSpacing(t.spacing.md);
    topLayout->addLayout(previewColumnLayout, 3);

    ui.verticalLayout_2 = new QVBoxLayout();
    ui.verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
    ui.verticalLayout_2->setSpacing(0);
    ui.verticalLayout_2->setContentsMargins(0, 0, 0, 0);

    ui.imagePreviewWidget = new QWidget(parent);
    ui.imagePreviewWidget->setObjectName(QStringLiteral("imagePreviewWidget"));
    ui.imagePreviewWidget->setMinimumSize(QSize(320, 220));
    ui.imagePreviewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui.verticalLayout_2->addWidget(ui.imagePreviewWidget, 1);
    previewColumnLayout->addLayout(ui.verticalLayout_2, 1);

    auto* controlsScrollArea = new QScrollArea(parent);
    controlsScrollArea->setObjectName(QStringLiteral("controlsScrollArea"));
    controlsScrollArea->setWidgetResizable(true);
    controlsScrollArea->setFrameShape(QFrame::NoFrame);
    controlsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* controlsContainer = new QWidget(controlsScrollArea);
    auto* controlsLayout = new QVBoxLayout(controlsContainer);
    controlsLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    controlsLayout->setSpacing(t.spacing.md);

    ui.biasGroupBox = new QGroupBox(QStringLiteral("Bias Master"), controlsContainer);
    ui.biasGroupBox->setObjectName(QStringLiteral("biasGroupBox"));
    ui.biasGroupBox->setCheckable(true);
    ui.biasGroupBox->setChecked(false);
    auto* biasLayout = new QVBoxLayout(ui.biasGroupBox);
    biasLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    biasLayout->setSpacing(t.spacing.sm);

    auto* biasPathLayout = new QHBoxLayout();
    ui.biasFilePath = new ImageCalibrationDropLineEdit(ui.biasGroupBox);
    ui.biasFilePath->setObjectName(QStringLiteral("biasFilePath"));
    ui.biasFilePath->setToolTip(
        QStringLiteral("Path to the selected master bias frame. You can also drop a single file here."));
    biasPathLayout->addWidget(ui.biasFilePath);

    ui.biasFileButton = new QPushButton(QStringLiteral("..."), ui.biasGroupBox);
    ui.biasFileButton->setObjectName(QStringLiteral("biasFileButton"));
    ui.biasFileButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.biasFileButton->setToolTip(QStringLiteral("Select a master bias file."));
    ui.biasFileButton->setMinimumSize(QSize(30, 0));
    ui.biasFileButton->setMaximumSize(QSize(30, QWIDGETSIZE_MAX));
    biasPathLayout->addWidget(ui.biasFileButton);
    biasLayout->addLayout(biasPathLayout);

    controlsLayout->addWidget(ui.biasGroupBox);

    ui.darkGroupBox = new QGroupBox(QStringLiteral("Dark Master"), controlsContainer);
    ui.darkGroupBox->setObjectName(QStringLiteral("darkGroupBox"));
    ui.darkGroupBox->setCheckable(true);
    ui.darkGroupBox->setChecked(false);
    ui.verticalLayout_3 = new QVBoxLayout(ui.darkGroupBox);
    ui.verticalLayout_3->setObjectName(QStringLiteral("verticalLayout_3"));
    ui.verticalLayout_3->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    ui.verticalLayout_3->setSpacing(t.spacing.sm);

    auto* darkPathLayout = new QHBoxLayout();
    ui.darkFilePath = new ImageCalibrationDropLineEdit(ui.darkGroupBox);
    ui.darkFilePath->setObjectName(QStringLiteral("darkFilePath"));
    ui.darkFilePath->setToolTip(
        QStringLiteral("Path to the selected master dark frame. You can also drop a single file here."));
    darkPathLayout->addWidget(ui.darkFilePath);

    ui.darkFileButton = new QPushButton(QStringLiteral("..."), ui.darkGroupBox);
    ui.darkFileButton->setObjectName(QStringLiteral("darkFileButton"));
    ui.darkFileButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.darkFileButton->setToolTip(QStringLiteral("Select a master dark file."));
    ui.darkFileButton->setMinimumSize(QSize(30, 0));
    ui.darkFileButton->setMaximumSize(QSize(30, QWIDGETSIZE_MAX));
    darkPathLayout->addWidget(ui.darkFileButton);
    ui.verticalLayout_3->addLayout(darkPathLayout);

    ui.darkAdvancedSection =
        new an::ui::AnCollapsibleSection(QStringLiteral("Advanced Dark Settings"), ui.darkGroupBox);
    ui.darkAdvancedSection->setObjectName(QStringLiteral("darkAdvancedSection"));
    ui.darkAdvancedSection->setExpanded(false);
    auto* darkAdvancedLayout = new QVBoxLayout();
    darkAdvancedLayout->setSpacing(t.spacing.sm);

    ui.darkOptimizationCheck = new QCheckBox(QStringLiteral("Enable dark optimization"), ui.darkAdvancedSection);
    ui.darkOptimizationCheck->setObjectName(QStringLiteral("darkOptimizationCheck"));
    ui.darkOptimizationCheck->setToolTip(
        QStringLiteral("Find for each grey light of at least 512 x 512 the dark scaling factor that leaves the least "
                       "variation over its central square. Other lights use the dark scaling below."));
    darkAdvancedLayout->addWidget(ui.darkOptimizationCheck);

    auto* darkFactorLayout = new QFormLayout();
    darkFactorLayout->setContentsMargins(0, 0, 0, 0);
    darkFactorLayout->setHorizontalSpacing(t.spacing.sm);
    darkFactorLayout->setVerticalSpacing(t.spacing.xs);

    ui.darkMultiplicationFactorValue = new QDoubleSpinBox(ui.darkAdvancedSection);
    ui.darkMultiplicationFactorValue->setObjectName(QStringLiteral("darkMultiplicationFactorValue"));
    ui.darkMultiplicationFactorValue->setRange(0.1, 10.0);
    ui.darkMultiplicationFactorValue->setSingleStep(0.1);
    ui.darkMultiplicationFactorValue->setDecimals(2);
    ui.darkMultiplicationFactorValue->setValue(1.0);
    ui.darkMultiplicationFactorValue->setToolTip(
        QStringLiteral("Dark scaling factor for every light when dark optimization is off, and for colour lights and "
                       "lights smaller than 512 x 512 when it is on."));
    darkFactorLayout->addRow(QStringLiteral("Dark scaling"), ui.darkMultiplicationFactorValue);
    darkAdvancedLayout->addLayout(darkFactorLayout);

    ui.cosmeticCorrectionGroupBox = new QGroupBox(QStringLiteral("Enable cosmetic correction"), ui.darkAdvancedSection);
    ui.cosmeticCorrectionGroupBox->setObjectName(QStringLiteral("cosmeticCorrectionGroupBox"));
    ui.cosmeticCorrectionGroupBox->setCheckable(true);
    ui.cosmeticCorrectionGroupBox->setChecked(false);
    auto* cosmeticLayout = new QVBoxLayout(ui.cosmeticCorrectionGroupBox);
    cosmeticLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    cosmeticLayout->setSpacing(t.spacing.sm);

    auto* coldLayout = new QHBoxLayout();
    ui.coldSigmaCheck = new QCheckBox(QStringLiteral("Cold sigma"), ui.cosmeticCorrectionGroupBox);
    ui.coldSigmaCheck->setObjectName(QStringLiteral("coldSigmaCheck"));
    ui.coldSigmaCheck->setToolTip(QStringLiteral("Enable correction of abnormally cold pixels."));
    coldLayout->addWidget(ui.coldSigmaCheck);
    ui.coldSigmaValue = new QDoubleSpinBox(ui.cosmeticCorrectionGroupBox);
    ui.coldSigmaValue->setObjectName(QStringLiteral("coldSigmaValue"));
    ui.coldSigmaValue->setDecimals(3);
    ui.coldSigmaValue->setSingleStep(0.1);
    ui.coldSigmaValue->setValue(3.0);
    ui.coldSigmaValue->setToolTip(QStringLiteral("Sigma threshold used to detect cold pixels."));
    coldLayout->addWidget(ui.coldSigmaValue);
    coldLayout->addStretch();
    cosmeticLayout->addLayout(coldLayout);

    auto* hotLayout = new QHBoxLayout();
    ui.hotSigmaCheck = new QCheckBox(QStringLiteral("Hot sigma"), ui.cosmeticCorrectionGroupBox);
    ui.hotSigmaCheck->setObjectName(QStringLiteral("hotSigmaCheck"));
    ui.hotSigmaCheck->setToolTip(QStringLiteral("Enable correction of abnormally hot pixels."));
    hotLayout->addWidget(ui.hotSigmaCheck);
    ui.hotSigmaValue = new QDoubleSpinBox(ui.cosmeticCorrectionGroupBox);
    ui.hotSigmaValue->setObjectName(QStringLiteral("hotSigmaValue"));
    ui.hotSigmaValue->setDecimals(3);
    ui.hotSigmaValue->setSingleStep(0.1);
    ui.hotSigmaValue->setValue(3.0);
    ui.hotSigmaValue->setToolTip(QStringLiteral("Sigma threshold used to detect hot pixels."));
    hotLayout->addWidget(ui.hotSigmaValue);
    hotLayout->addStretch();
    cosmeticLayout->addLayout(hotLayout);

    auto* estimateLayout = new QHBoxLayout();
    ui.estimateCosmeticCorrectionButton =
        new QPushButton(QStringLiteral("Auto-estimate"), ui.cosmeticCorrectionGroupBox);
    ui.estimateCosmeticCorrectionButton->setObjectName(QStringLiteral("estimateCosmeticCorrectionButton"));
    ui.estimateCosmeticCorrectionButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.estimateCosmeticCorrectionButton->setToolTip(
        QStringLiteral("Count the cold and hot pixels the current sigma thresholds find in the selected dark frame.\n"
                       "The thresholds themselves are not changed."));
    estimateLayout->addWidget(ui.estimateCosmeticCorrectionButton);
    estimateLayout->addStretch();
    cosmeticLayout->addLayout(estimateLayout);

    darkAdvancedLayout->addWidget(ui.cosmeticCorrectionGroupBox);
    ui.darkAdvancedSection->setContentLayout(darkAdvancedLayout);
    ui.verticalLayout_3->addWidget(ui.darkAdvancedSection);

    controlsLayout->addWidget(ui.darkGroupBox);

    ui.flatGroupBox = new QGroupBox(QStringLiteral("Flat Master"), controlsContainer);
    ui.flatGroupBox->setObjectName(QStringLiteral("flatGroupBox"));
    ui.flatGroupBox->setCheckable(true);
    ui.flatGroupBox->setChecked(false);
    auto* flatLayout = new QVBoxLayout(ui.flatGroupBox);
    flatLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    flatLayout->setSpacing(t.spacing.sm);

    auto* flatPathLayout = new QHBoxLayout();
    ui.flatFilePath = new ImageCalibrationDropLineEdit(ui.flatGroupBox);
    ui.flatFilePath->setObjectName(QStringLiteral("flatFilePath"));
    ui.flatFilePath->setToolTip(
        QStringLiteral("Path to the selected master flat frame. You can also drop a single file here."));
    flatPathLayout->addWidget(ui.flatFilePath);

    ui.flatFileButton = new QPushButton(QStringLiteral("..."), ui.flatGroupBox);
    ui.flatFileButton->setObjectName(QStringLiteral("flatFileButton"));
    ui.flatFileButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.flatFileButton->setToolTip(QStringLiteral("Select a master flat file."));
    ui.flatFileButton->setMinimumSize(QSize(30, 0));
    ui.flatFileButton->setMaximumSize(QSize(30, QWIDGETSIZE_MAX));
    flatPathLayout->addWidget(ui.flatFileButton);
    flatLayout->addLayout(flatPathLayout);

    ui.flatAdvancedSection =
        new an::ui::AnCollapsibleSection(QStringLiteral("Advanced Flat Settings"), ui.flatGroupBox);
    ui.flatAdvancedSection->setObjectName(QStringLiteral("flatAdvancedSection"));
    ui.flatAdvancedSection->setExpanded(false);
    auto* flatAdvancedLayout = new QVBoxLayout();
    flatAdvancedLayout->setSpacing(t.spacing.sm);

    ui.normalizeFlatsCheck =
        new QCheckBox(QStringLiteral("Normalize flat master automatically"), ui.flatAdvancedSection);
    ui.normalizeFlatsCheck->setObjectName(QStringLiteral("normalizeFlatsCheck"));
    ui.normalizeFlatsCheck->setToolTip(
        QStringLiteral("Normalize the master flat before applying it during calibration."));
    flatAdvancedLayout->addWidget(ui.normalizeFlatsCheck);

    ui.flatAdvancedSection->setContentLayout(flatAdvancedLayout);
    flatLayout->addWidget(ui.flatAdvancedSection);

    controlsLayout->addWidget(ui.flatGroupBox);

    ui.outputCard = new QGroupBox(QStringLiteral("Output & Execution"), controlsContainer);
    ui.outputCard->setObjectName(QStringLiteral("outputCard"));
    auto* outputCardLayout = new QVBoxLayout(ui.outputCard);
    outputCardLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    outputCardLayout->setSpacing(t.spacing.sm);

    auto* outputPathLayout = new QHBoxLayout();
    auto* outputLabel = new QLabel(QStringLiteral("Output directory"), ui.outputCard);
    outputPathLayout->addWidget(outputLabel);
    ui.outputDirectory = new QLineEdit(ui.outputCard);
    ui.outputDirectory->setObjectName(QStringLiteral("outputDirectory"));
    ui.outputDirectory->setReadOnly(true);
    ui.outputDirectory->setToolTip(QStringLiteral("Destination folder for calibrated output frames."));
    outputPathLayout->addWidget(ui.outputDirectory);

    ui.outputDirectoryButton = new QPushButton(QStringLiteral("..."), ui.outputCard);
    ui.outputDirectoryButton->setObjectName(QStringLiteral("outputDirectoryButton"));
    ui.outputDirectoryButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.outputDirectoryButton->setToolTip(QStringLiteral("Choose the output directory for calibrated images."));
    ui.outputDirectoryButton->setMinimumSize(QSize(30, 0));
    ui.outputDirectoryButton->setMaximumSize(QSize(30, QWIDGETSIZE_MAX));
    outputPathLayout->addWidget(ui.outputDirectoryButton);
    outputCardLayout->addLayout(outputPathLayout);

    auto* outputOptionsLayout = new QFormLayout();
    outputOptionsLayout->setContentsMargins(0, 0, 0, 0);
    outputOptionsLayout->setHorizontalSpacing(t.spacing.sm);
    outputOptionsLayout->setVerticalSpacing(t.spacing.sm);

    ui.preserveOriginalNameCheck = new QCheckBox(QStringLiteral("Use source filename"), ui.outputCard);
    ui.preserveOriginalNameCheck->setObjectName(QStringLiteral("preserveOriginalNameCheck"));
    ui.preserveOriginalNameCheck->setChecked(true);
    ui.preserveOriginalNameCheck->setToolTip(
        QStringLiteral("Preserve the original filename stem instead of using index-based names."));
    outputOptionsLayout->addRow(QStringLiteral("File naming"), ui.preserveOriginalNameCheck);

    ui.filenameSuffixEdit = new QLineEdit(ui.outputCard);
    ui.filenameSuffixEdit->setObjectName(QStringLiteral("filenameSuffixEdit"));
    ui.filenameSuffixEdit->setText(QStringLiteral("_cal"));
    ui.filenameSuffixEdit->setToolTip(QStringLiteral("Suffix appended before the output extension."));
    outputOptionsLayout->addRow(QStringLiteral("Suffix"), ui.filenameSuffixEdit);

    ui.outputFormatSelector = new QComboBox(ui.outputCard);
    ui.outputFormatSelector->setObjectName(QStringLiteral("outputFormatSelector"));
    ui.outputFormatSelector->addItem(QStringLiteral("FITS (Recommended)"), QStringLiteral("fits"));
    ui.outputFormatSelector->addItem(QStringLiteral("TIFF"), QStringLiteral("tiff"));
    ui.outputFormatSelector->addItem(QStringLiteral("Original input format"), QStringLiteral("original"));
    ui.outputFormatSelector->setToolTip(QStringLiteral("Choose the output container format for calibrated frames."));
    outputOptionsLayout->addRow(QStringLiteral("Format"), ui.outputFormatSelector);

    ui.outputPrecisionLabel = new QLabel(QStringLiteral("Precision"), ui.outputCard);
    ui.outputPrecisionLabel->setObjectName(QStringLiteral("outputPrecisionLabel"));
    ui.outputPrecisionSelector = new QComboBox(ui.outputCard);
    ui.outputPrecisionSelector->setObjectName(QStringLiteral("outputPrecisionSelector"));
    ui.outputPrecisionSelector->addItem(QStringLiteral("32-bit float"), QVariant(true));
    ui.outputPrecisionSelector->addItem(QStringLiteral("64-bit double"), QVariant(false));
    ui.outputPrecisionSelector->setToolTip(QStringLiteral(
        "Precision used when saving FITS or TIFF outputs. Ignored when preserving the original input format."));
    outputOptionsLayout->addRow(ui.outputPrecisionLabel, ui.outputPrecisionSelector);
    outputCardLayout->addLayout(outputOptionsLayout);

    ui.outputAdvancedSection =
        new an::ui::AnCollapsibleSection(QStringLiteral("Advanced Calibration Settings"), ui.outputCard);
    ui.outputAdvancedSection->setObjectName(QStringLiteral("outputAdvancedSection"));
    ui.outputAdvancedSection->setExpanded(false);
    auto* outputAdvancedLayout = new QFormLayout();
    outputAdvancedLayout->setContentsMargins(0, 0, 0, 0);
    outputAdvancedLayout->setHorizontalSpacing(t.spacing.sm);
    outputAdvancedLayout->setVerticalSpacing(t.spacing.sm);

    ui.pedestalValue = new QDoubleSpinBox(ui.outputAdvancedSection);
    ui.pedestalValue->setObjectName(QStringLiteral("pedestalValue"));
    ui.pedestalValue->setRange(0.0, 10000.0);
    ui.pedestalValue->setSingleStep(10.0);
    ui.pedestalValue->setDecimals(1);
    ui.pedestalValue->setValue(0.0);
    ui.pedestalValue->setToolTip(
        QStringLiteral("Add a fixed pedestal to every light to avoid clipping after subtraction.\n"
                       "The value is in 16-bit codes, where 65535 is full scale, whatever the light's own depth."));
    outputAdvancedLayout->addRow(QStringLiteral("Pedestal (16-bit codes)"), ui.pedestalValue);

    ui.outputAdvancedSection->setContentLayout(outputAdvancedLayout);
    outputCardLayout->addWidget(ui.outputAdvancedSection);

    controlsLayout->addWidget(ui.outputCard);
    controlsLayout->addStretch();

    controlsScrollArea->setWidget(controlsContainer);

    // ── Right column: scroll area + pinned Calibrate button ──
    auto* rightColumnLayout = new QVBoxLayout();
    rightColumnLayout->setSpacing(t.spacing.sm);
    rightColumnLayout->addWidget(controlsScrollArea, 1);

    ui.calibrateImagesButton = new QPushButton(QStringLiteral("Calibrate Images"), parent);
    ui.calibrateImagesButton->setObjectName(QStringLiteral("calibrateImagesButton"));
    ui.calibrateImagesButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    ui.calibrateImagesButton->setMinimumSize(QSize(0, 44));
    ui.calibrateImagesButton->setToolTip(QStringLiteral("Run calibration for all images listed in the table."));
    rightColumnLayout->addWidget(ui.calibrateImagesButton);

    topLayout->addLayout(rightColumnLayout, 2);

    ui.frameListCard = new an::ui::AnCardWidget(QStringLiteral("Light Frames"), parent);
    ui.frameListCard->setObjectName(QStringLiteral("frameListCard"));
    ui.tableLayout = new QVBoxLayout();
    ui.tableLayout->setObjectName(QStringLiteral("tableLayout"));

    ui.tableScrollArea = new QScrollArea(ui.frameListCard);
    ui.tableScrollArea->setObjectName(QStringLiteral("tableScrollArea"));
    ui.tableScrollArea->setWidgetResizable(true);
    ui.tableScrollArea->setMinimumHeight(0);
    ui.tableScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    ui.tableScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);

    auto* tableContainer = new QWidget(ui.tableScrollArea);
    tableContainer->setMinimumSize(QSize(0, 0));
    tableContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    auto* tableContainerLayout = new QVBoxLayout(tableContainer);
    tableContainerLayout->setContentsMargins(0, 0, 0, 0);
    tableContainerLayout->setSpacing(0);

    ui.tableWidget = new TableWidget(tableContainer);
    ui.tableWidget->setObjectName(QStringLiteral("tableWidget"));
    ui.tableWidget->setMinimumSize(QSize(0, 0));
    ui.tableWidget->setMinimumHeight(0);
    ui.tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    tableContainerLayout->addWidget(ui.tableWidget);
    ui.tableScrollArea->setWidget(tableContainer);
    ui.tableLayout->addWidget(ui.tableScrollArea);

    auto* tableButtonsLayout = new QHBoxLayout();
    ui.addFiles = new QPushButton(QStringLiteral("Add Files"), ui.frameListCard);
    ui.addFiles->setObjectName(QStringLiteral("addFiles"));
    ui.addFiles->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.addFiles->setToolTip(QStringLiteral("Add source image files to the calibration list."));
    tableButtonsLayout->addWidget(ui.addFiles);

    ui.addFolder = new QPushButton(QStringLiteral("Add Folder"), ui.frameListCard);
    ui.addFolder->setObjectName(QStringLiteral("addFolder"));
    ui.addFolder->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.addFolder->setToolTip(
        QStringLiteral("Add all image files from a folder with optional recursive scanning and metadata filtering."));
    tableButtonsLayout->addWidget(ui.addFolder);

    ui.removeSelected = new QPushButton(QStringLiteral("Remove Selected"), ui.frameListCard);
    ui.removeSelected->setObjectName(QStringLiteral("removeSelected"));
    ui.removeSelected->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.removeSelected->setToolTip(QStringLiteral("Remove selected frames from the calibration list."));
    tableButtonsLayout->addWidget(ui.removeSelected);

    ui.moveUpButton = new QPushButton(ui.frameListCard);
    ui.moveUpButton->setObjectName(QStringLiteral("moveUpButton"));
    ui.moveUpButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.moveUpButton->setToolTip(QStringLiteral("Move frame up"));
    tableButtonsLayout->addWidget(ui.moveUpButton);

    ui.moveDownButton = new QPushButton(ui.frameListCard);
    ui.moveDownButton->setObjectName(QStringLiteral("moveDownButton"));
    ui.moveDownButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    ui.moveDownButton->setToolTip(QStringLiteral("Move frame down"));
    tableButtonsLayout->addWidget(ui.moveDownButton);

    auto* currentLabel = new QLabel(QStringLiteral("Current Image:"), ui.frameListCard);
    tableButtonsLayout->addWidget(currentLabel);
    ui.currentImageLabel = new QLabel(ui.frameListCard);
    ui.currentImageLabel->setObjectName(QStringLiteral("currentImageLabel"));
    ui.currentImageLabel->setMinimumSize(QSize(45, 0));
    tableButtonsLayout->addWidget(ui.currentImageLabel);

    auto* totalLabel = new QLabel(QStringLiteral("Total Images:"), ui.frameListCard);
    tableButtonsLayout->addWidget(totalLabel);
    ui.totalImagesLabel = new QLabel(ui.frameListCard);
    ui.totalImagesLabel->setObjectName(QStringLiteral("totalImagesLabel"));
    ui.totalImagesLabel->setMinimumSize(QSize(45, 0));
    tableButtonsLayout->addWidget(ui.totalImagesLabel);

    tableButtonsLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));

    ui.clearAll = new QPushButton(QStringLiteral("Clear All"), ui.frameListCard);
    ui.clearAll->setObjectName(QStringLiteral("clearAll"));
    ui.clearAll->setProperty("anButtonVariant", QStringLiteral("danger"));
    ui.clearAll->setToolTip(QStringLiteral("Remove all frames from the calibration list."));
    tableButtonsLayout->addWidget(ui.clearAll);

    ui.tableLayout->addLayout(tableButtonsLayout);
    ui.frameListCard->setContentLayout(ui.tableLayout);
    ui.verticalLayout_Main->addWidget(ui.frameListCard, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setObjectName(QStringLiteral("horizontalLayout_7"));
    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
    {
        btn->setToolTip(QStringLiteral("Close this dialog without applying any changes."));
    }
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        btn->setToolTip(QStringLiteral("Reset calibration settings to their default values."));
    }
    buttonLayout->addWidget(ui.buttonBox);
    ui.verticalLayout_Main->addLayout(buttonLayout);

    return ui;
}

} // namespace an::ui::processes
