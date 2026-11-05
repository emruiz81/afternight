// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "components/AnCollapsibleSection.h"
#include "components/AnCardWidget.h"
#include "components/AnInfoBanner.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace an::ui::processes
{

struct ImageIntegrationLiteProcessUiElements
{
    QVBoxLayout* mainLayout = nullptr;
    AnInfoBanner* statusBanner = nullptr;
    QLabel* frameSummaryLabel = nullptr;
    QLabel* filterSummaryLabel = nullptr;
    QLabel* objectSummaryLabel = nullptr;
    AnCollapsibleSection* calibrationSection = nullptr;
    AnCollapsibleSection* registrationSection = nullptr;
    AnCollapsibleSection* normalizationSection = nullptr;
    AnCollapsibleSection* stackingSection = nullptr;
    QComboBox* stackMethodCombo = nullptr;
    QComboBox* rejectionMethodCombo = nullptr;
    QLabel* rejectionLowLabel = nullptr;
    QDoubleSpinBox* rejectionLowSpin = nullptr;
    QLabel* rejectionHighLabel = nullptr;
    QDoubleSpinBox* rejectionHighSpin = nullptr;
    QComboBox* weightingCombo = nullptr;
    QComboBox* transformationCombo = nullptr;
    QCheckBox* autoReferenceCheck = nullptr;
    QCheckBox* autoCropCheck = nullptr;
    QComboBox* normalizationCombo = nullptr;
    QComboBox* calibrationCombo = nullptr;
    QCheckBox* darkOptimizationCheck = nullptr;
    QCheckBox* flatAutoLevelCheck = nullptr;
    QLabel* calibrationNormalizationLabel = nullptr;
    QCheckBox* solveFinalMasterCheck = nullptr;
    QCheckBox* drizzleEnabledCheck = nullptr;
    QComboBox* drizzleScaleCombo = nullptr;
    QComboBox* drizzlePixelFractionCombo = nullptr;
    QCheckBox* reprocessExistingCheck = nullptr;
    QCheckBox* openResultsCheck = nullptr;
    QCheckBox* limitMemoryCheck = nullptr;
    QSpinBox* maxMemorySpin = nullptr;
    QLabel* maxParallelThreadsLabel = nullptr;
    QSpinBox* maxParallelThreadsSpin = nullptr;
    QLineEdit* outputDirectoryEdit = nullptr;
    QPushButton* browseOutputButton = nullptr;
    QComboBox* importTypeCombo = nullptr;
    QPushButton* addFilesButton = nullptr;
    QPushButton* addFolderButton = nullptr;
    QPushButton* clearAllButton = nullptr;
    QPushButton* changeFrameTypeButton = nullptr;
    QTableWidget* frameTable = nullptr;
    QPushButton* runButton = nullptr;
    QPushButton* openFullProcessButton = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    AnCardWidget* summaryCard = nullptr;
    AnCardWidget* setupCard = nullptr;
    AnCardWidget* settingsCard = nullptr;
    AnCardWidget* framesCard = nullptr;
};

inline void configureImageIntegrationLiteCardChrome(AnCardWidget* card)
{
    if (!card)
    {
        return;
    }

    card->setProperty("anCardBackdropColor", an::ui::tokens().colors.bgSurface);
    card->setProperty("anCardFlat", true);
    card->update();
}

inline void configureImageIntegrationLiteSectionChrome(const ImageIntegrationLiteProcessUiElements& ui)
{
    configureImageIntegrationLiteCardChrome(ui.summaryCard);
    configureImageIntegrationLiteCardChrome(ui.setupCard);
    configureImageIntegrationLiteCardChrome(ui.settingsCard);
    configureImageIntegrationLiteCardChrome(ui.framesCard);
}

inline void setLiteControlTooltip(QWidget* widget, const QString& tooltip)
{
    if (!widget)
    {
        return;
    }

    widget->setToolTip(tooltip);
    widget->setStatusTip(tooltip);
}

inline void addLiteComboItem(QComboBox* combo, const QString& text, const QVariant& userData, const QString& tooltip)
{
    combo->addItem(text, userData);
    combo->setItemData(combo->count() - 1, tooltip, Qt::ToolTipRole);
}

inline void addLiteComboTextItem(QComboBox* combo, const QString& text, const QString& tooltip)
{
    combo->addItem(text);
    combo->setItemData(combo->count() - 1, tooltip, Qt::ToolTipRole);
}

[[nodiscard]] inline int liteComboWidthForUnclippedItems(const QComboBox* combo)
{
    if (!combo)
    {
        return 0;
    }

    const QFontMetrics metrics(combo->font());
    int longest_text_width = 0;
    for (int index = 0; index < combo->count(); ++index)
    {
        longest_text_width = (std::max)(longest_text_width, metrics.horizontalAdvance(combo->itemText(index)));
    }

    QStyleOptionComboBox option;
    option.initFrom(combo);
    option.editable = combo->isEditable();
    option.frame = combo->hasFrame();
    const QSize hint = combo->sizeHint();
    option.rect = QRect(0, 0, hint.width(), hint.height());

    const QRect edit_rect =
        combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
    const int non_text_width = (std::max)(0, option.rect.width() - edit_rect.width());
    return (std::max)(hint.width(), longest_text_width + non_text_width + an::ui::tokens().spacing.sm);
}

[[nodiscard]] inline ImageIntegrationLiteProcessUiElements buildImageIntegrationLiteProcessUi(QWidget* parent)
{
    ImageIntegrationLiteProcessUiElements ui;
    const auto& spacing = an::ui::tokens().spacing;

    parent->resize(1120, 780);
    parent->setMinimumSize(QSize(860, 600));

    ui.mainLayout = new QVBoxLayout(parent);
    ui.mainLayout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);
    ui.mainLayout->setSpacing(spacing.md);

    ui.statusBanner = new AnInfoBanner(QString(), AnInfoBanner::Tone::Info, parent);
    ui.statusBanner->setObjectName(QStringLiteral("liteStatusBanner"));
    ui.mainLayout->addWidget(ui.statusBanner);

    ui.summaryCard = new AnCardWidget(QObject::tr("Dataset"), parent);
    ui.summaryCard->setObjectName(QStringLiteral("liteSummaryCard"));
    auto* summaryLayout = new QFormLayout();
    summaryLayout->setContentsMargins(spacing.md, spacing.md, spacing.md, spacing.md);
    summaryLayout->setSpacing(spacing.sm);
    ui.frameSummaryLabel = new QLabel(QObject::tr("No frames"), ui.summaryCard);
    ui.filterSummaryLabel = new QLabel(QObject::tr("-"), ui.summaryCard);
    ui.objectSummaryLabel = new QLabel(QObject::tr("-"), ui.summaryCard);
    ui.frameSummaryLabel->setObjectName(QStringLiteral("liteFrameSummaryLabel"));
    ui.filterSummaryLabel->setObjectName(QStringLiteral("liteFilterSummaryLabel"));
    ui.objectSummaryLabel->setObjectName(QStringLiteral("liteObjectSummaryLabel"));
    summaryLayout->addRow(QObject::tr("Frames"), ui.frameSummaryLabel);
    summaryLayout->addRow(QObject::tr("Filters"), ui.filterSummaryLabel);
    summaryLayout->addRow(QObject::tr("Object"), ui.objectSummaryLabel);
    ui.summaryCard->setContentLayout(summaryLayout);
    ui.mainLayout->addWidget(ui.summaryCard);

    ui.setupCard = new AnCardWidget(QObject::tr("Setup"), parent);
    ui.setupCard->setObjectName(QStringLiteral("liteSetupCard"));
    auto* setupLayout = new QFormLayout();
    setupLayout->setContentsMargins(spacing.md, spacing.md, spacing.md, spacing.md);
    setupLayout->setSpacing(spacing.sm);

    auto* outputRow = new QWidget(ui.setupCard);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(spacing.sm);
    ui.outputDirectoryEdit = new QLineEdit(outputRow);
    ui.outputDirectoryEdit->setObjectName(QStringLiteral("liteOutputDirectoryEdit"));
    ui.outputDirectoryEdit->setPlaceholderText(QObject::tr("Output directory"));
    setLiteControlTooltip(ui.outputDirectoryEdit,
                          QObject::tr("Folder where masters, registered images, reports, and final integrations will "
                                      "be written."));
    ui.browseOutputButton = new QPushButton(QObject::tr("Browse"), outputRow);
    ui.browseOutputButton->setObjectName(QStringLiteral("liteBrowseOutputButton"));
    setLiteControlTooltip(ui.browseOutputButton, QObject::tr("Choose the folder that will receive this run's output."));
    outputLayout->addWidget(ui.outputDirectoryEdit, 1);
    outputLayout->addWidget(ui.browseOutputButton);
    setupLayout->addRow(QObject::tr("Output"), outputRow);

    ui.reprocessExistingCheck = new QCheckBox(QObject::tr("Reprocess existing outputs"), ui.setupCard);
    ui.reprocessExistingCheck->setObjectName(QStringLiteral("liteReprocessExistingCheck"));
    setLiteControlTooltip(ui.reprocessExistingCheck,
                          QObject::tr("Run every required stage again, even when matching output files already exist. "
                                      "Useful after changing settings or replacing source frames."));
    setupLayout->addRow(QString(), ui.reprocessExistingCheck);

    ui.openResultsCheck = new QCheckBox(QObject::tr("Open results when complete"), ui.setupCard);
    ui.openResultsCheck->setObjectName(QStringLiteral("liteOpenResultsCheck"));
    ui.openResultsCheck->setChecked(true);
    setLiteControlTooltip(ui.openResultsCheck,
                          QObject::tr("Open the final integrated image or images in AfterNight when the workflow "
                                      "finishes successfully."));
    setupLayout->addRow(QString(), ui.openResultsCheck);

    auto* resourcesGroup = new QWidget(ui.setupCard);
    auto* resourcesLayout = new QFormLayout(resourcesGroup);
    resourcesLayout->setContentsMargins(0, 0, 0, 0);
    resourcesLayout->setSpacing(spacing.sm);
    auto* memoryRow = new QWidget(resourcesGroup);
    auto* memoryLayout = new QHBoxLayout(memoryRow);
    memoryLayout->setContentsMargins(0, 0, 0, 0);
    memoryLayout->setSpacing(spacing.sm);
    ui.limitMemoryCheck = new QCheckBox(QObject::tr("Limit memory usage"), memoryRow);
    ui.limitMemoryCheck->setObjectName(QStringLiteral("liteLimitMemoryCheck"));
    setLiteControlTooltip(ui.limitMemoryCheck,
                          QObject::tr("Limit the maximum amount of RAM used during stacking operations. Enabling this "
                                      "uses tiled processing to respect the limit."));
    ui.maxMemorySpin = new QSpinBox(memoryRow);
    ui.maxMemorySpin->setObjectName(QStringLiteral("liteMaxMemorySpin"));
    ui.maxMemorySpin->setEnabled(false);
    ui.maxMemorySpin->setRange(512, 1048576);
    ui.maxMemorySpin->setValue(2048);
    ui.maxMemorySpin->setSuffix(QObject::tr(" MB"));
    ui.maxMemorySpin->setMinimumWidth(ui.maxMemorySpin->sizeHint().width());
    setLiteControlTooltip(ui.maxMemorySpin, QObject::tr("Maximum memory usage in megabytes (MB)."));
    memoryLayout->addWidget(ui.limitMemoryCheck);
    memoryLayout->addWidget(ui.maxMemorySpin);
    memoryLayout->addStretch(1);
    ui.maxParallelThreadsLabel = new QLabel(QObject::tr("CPU Threads"), resourcesGroup);
    ui.maxParallelThreadsLabel->setObjectName(QStringLiteral("liteMaxParallelThreadsLabel"));
    setLiteControlTooltip(ui.maxParallelThreadsLabel,
                          QObject::tr("Maximum CPU threads used by Image Integration Lite parallel processing."));
    ui.maxParallelThreadsSpin = new QSpinBox(resourcesGroup);
    ui.maxParallelThreadsSpin->setObjectName(QStringLiteral("liteMaxParallelThreadsSpin"));
    const int detectedParallelThreads = ImageIntegration::core::detectedParallelCoreCount();
    ui.maxParallelThreadsSpin->setRange(1, detectedParallelThreads);
    ui.maxParallelThreadsSpin->setValue(detectedParallelThreads);
    ui.maxParallelThreadsSpin->setMinimumWidth(ui.maxParallelThreadsSpin->sizeHint().width());
    setLiteControlTooltip(ui.maxParallelThreadsSpin,
                          QObject::tr("Maximum CPU threads used by Image Integration Lite parallel processing."));
    resourcesLayout->addRow(QObject::tr("Memory"), memoryRow);
    resourcesLayout->addRow(ui.maxParallelThreadsLabel, ui.maxParallelThreadsSpin);
    setupLayout->addRow(QObject::tr("Resource limits"), resourcesGroup);

    ui.setupCard->setContentLayout(setupLayout);
    ui.mainLayout->addWidget(ui.setupCard);

    ui.settingsCard = new AnCardWidget(QObject::tr("Processing Settings"), parent);
    ui.settingsCard->setObjectName(QStringLiteral("liteSettingsCard"));
    auto* settingsLayout = new QVBoxLayout();
    settingsLayout->setContentsMargins(spacing.md, spacing.md, spacing.md, spacing.md);
    settingsLayout->setSpacing(spacing.sm);

    auto addSection = [&](const QString& title, const QString& objectName,
                          AnCollapsibleSection*& section) -> QFormLayout* {
        section = new AnCollapsibleSection(title, ui.settingsCard);
        section->setObjectName(objectName);
        auto* form = new QFormLayout();
        form->setContentsMargins(spacing.md, spacing.sm, spacing.md, spacing.md);
        form->setSpacing(spacing.sm);
        section->setContentLayout(form);
        settingsLayout->addWidget(section);
        return form;
    };

    auto* calibrationLayout = addSection(QObject::tr("Calibration: automatic matching"),
                                         QStringLiteral("liteCalibrationSection"), ui.calibrationSection);
    setLiteControlTooltip(ui.calibrationSection,
                          QObject::tr("Controls how Lite chooses bias, dark, flat, and Dark Flat frames without "
                                      "showing the full calibration workflow."));
    ui.calibrationCombo = new QComboBox(ui.calibrationSection->contentWidget());
    ui.calibrationCombo->setObjectName(QStringLiteral("liteCalibrationCombo"));
    setLiteControlTooltip(ui.calibrationCombo,
                          QObject::tr("Choose how strictly calibration frames must match the lights they correct."));
    addLiteComboItem(ui.calibrationCombo, QObject::tr("Automatic"), 0,
                     QObject::tr("Use metadata when available and fall back to practical matching for common light, "
                                 "flat, dark, and bias sets."));
    addLiteComboItem(ui.calibrationCombo, QObject::tr("Strict metadata match"), 1,
                     QObject::tr("Only use calibration frames that closely match the light metadata, such as filter, "
                                 "exposure, gain, and binning."));
    addLiteComboItem(ui.calibrationCombo, QObject::tr("Adaptive"), 2,
                     QObject::tr("Prefer metadata matches, but allow reasonable fallbacks when headers are incomplete "
                                 "or not fully reliable."));
    ui.darkOptimizationCheck =
        new QCheckBox(QObject::tr("Optimize dark frames"), ui.calibrationSection->contentWidget());
    ui.darkOptimizationCheck->setObjectName(QStringLiteral("liteDarkOptimizationCheck"));
    setLiteControlTooltip(ui.darkOptimizationCheck,
                          QObject::tr("Scale dark frames to better match each light's thermal signal. Use it when dark "
                                      "exposures are close but not perfectly identical."));
    ui.flatAutoLevelCheck = new QCheckBox(QObject::tr("Normalize master flat"), ui.calibrationSection->contentWidget());
    ui.flatAutoLevelCheck->setObjectName(QStringLiteral("liteFlatAutoLevelCheck"));
    ui.flatAutoLevelCheck->setChecked(true);
    setLiteControlTooltip(ui.flatAutoLevelCheck,
                          QObject::tr("Normalize the master flat before calibrating lights so flat correction removes "
                                      "dust and vignetting without changing the overall brightness."));
    calibrationLayout->addRow(QObject::tr("Matching"), ui.calibrationCombo);
    calibrationLayout->addRow(QString(), ui.darkOptimizationCheck);
    calibrationLayout->addRow(QString(), ui.flatAutoLevelCheck);

    auto* registrationLayout = addSection(QObject::tr("Registration: star matching, homography"),
                                          QStringLiteral("liteRegistrationSection"), ui.registrationSection);
    setLiteControlTooltip(ui.registrationSection,
                          QObject::tr("Controls the star-matching transform model used to align lights before "
                                      "stacking."));
    ui.transformationCombo = new QComboBox(ui.registrationSection->contentWidget());
    ui.transformationCombo->setObjectName(QStringLiteral("liteTransformationCombo"));
    setLiteControlTooltip(
        ui.transformationCombo,
        QObject::tr("Choose how flexible the geometric correction can be during star registration.\n\n"
                    "Modes:\n"
                    "- Similarity: keeps the field shape rigid; use it for clean guided data that "
                    "only needs shift, rotation, and scale.\n"
                    "- Affine: also allows mild shear when frames look slightly stretched.\n"
                    "- Homography: handles stronger perspective-like changes and is a good general "
                    "model for star matching.\n"
                    "- Polynomial 2/3/4: bends the correction for optical distortion or field "
                    "curvature; higher orders need more matched stars.\n"
                    "- Auto starts with Homography and only tries polynomial refinement when it "
                    "clearly improves the star fit."));
    addLiteComboItem(ui.transformationCombo, QObject::tr("Similarity"), QStringLiteral("Similarity"),
                     QObject::tr("Allow shift, rotation, and scale. Good for clean guided sequences with little "
                                 "distortion."));
    addLiteComboItem(ui.transformationCombo, QObject::tr("Affine"), QStringLiteral("Affine"),
                     QObject::tr("Allow shift, rotation, scale, and mild shear. Useful when the field changes shape "
                                 "slightly across the set."));
    addLiteComboItem(ui.transformationCombo, QObject::tr("Homography"), QStringLiteral("Homography"),
                     QObject::tr("Allow a projective transform for wider fields or stronger perspective-like "
                                 "differences."));
    addLiteComboItem(ui.transformationCombo, QObject::tr("Polynomial 2"), QStringLiteral("polynomial2"),
                     QObject::tr("Refine the homography with a second-order polynomial for mild optical distortion."));
    addLiteComboItem(
        ui.transformationCombo, QObject::tr("Polynomial 3"), QStringLiteral("polynomial3"),
        QObject::tr("Use a third-order polynomial refinement for stronger field curvature or distortion."));
    addLiteComboItem(ui.transformationCombo, QObject::tr("Polynomial 4"), QStringLiteral("polynomial4"),
                     QObject::tr("Use the most flexible polynomial refinement. Best reserved for difficult wide-field "
                                 "data with enough matched stars."));
    addLiteComboItem(ui.transformationCombo, QObject::tr("Auto"), QStringLiteral("auto"),
                     QObject::tr("Start with Homography and only promote to polynomial refinement when the star fit "
                                 "clearly improves."));
    ui.autoReferenceCheck =
        new QCheckBox(QObject::tr("Auto-select reference"), ui.registrationSection->contentWidget());
    ui.autoReferenceCheck->setObjectName(QStringLiteral("liteAutoReferenceCheck"));
    ui.autoReferenceCheck->setChecked(true);
    setLiteControlTooltip(ui.autoReferenceCheck,
                          QObject::tr("Pick a strong reference frame automatically so all lights align to a stable "
                                      "anchor."));
    ui.autoCropCheck = new QCheckBox(QObject::tr("Auto crop to common area"), ui.registrationSection->contentWidget());
    ui.autoCropCheck->setObjectName(QStringLiteral("liteAutoCropCheck"));
    ui.autoCropCheck->setChecked(false);
    setLiteControlTooltip(ui.autoCropCheck,
                          QObject::tr("Crop registered outputs to the area shared by all light frames. Internally this "
                                      "uses intersection alignment."));
    registrationLayout->addRow(QObject::tr("Transform"), ui.transformationCombo);
    registrationLayout->addRow(QString(), ui.autoReferenceCheck);
    registrationLayout->addRow(QString(), ui.autoCropCheck);

    auto* normalizationLayout = addSection(QObject::tr("Normalization: global additive scaling"),
                                           QStringLiteral("liteNormalizationSection"), ui.normalizationSection);
    setLiteControlTooltip(ui.normalizationSection,
                          QObject::tr("Controls how light frames are brightness-matched before stacking."));
    ui.normalizationCombo = new QComboBox(ui.normalizationSection->contentWidget());
    ui.normalizationCombo->setObjectName(QStringLiteral("liteNormalizationCombo"));
    setLiteControlTooltip(ui.normalizationCombo,
                          QObject::tr("Choose how lights are normalized so background and signal levels stack "
                                      "consistently."));
    addLiteComboItem(ui.normalizationCombo, QObject::tr("Global Additive Scaling"), 2,
                     QObject::tr("Match background offsets and scale the image globally. This is the recommended "
                                 "default for most light frames."));
    addLiteComboItem(ui.normalizationCombo, QObject::tr("Global Additive"), 1,
                     QObject::tr("Match background offsets globally without changing the signal scale."));
    addLiteComboItem(ui.normalizationCombo, QObject::tr("Global Multiplicative Scaling"), 4,
                     QObject::tr("Match signal scale globally while also allowing a scale-aware correction."));
    addLiteComboItem(ui.normalizationCombo, QObject::tr("Global Multiplicative"), 3,
                     QObject::tr("Match signal scale globally without an additive background correction."));
    addLiteComboItem(ui.normalizationCombo, QObject::tr("Local Normalization"), 5,
                     QObject::tr("Build a local correction model for gradients or uneven transparency. More flexible, "
                                 "but slower and easier to overuse."));
    addLiteComboItem(ui.normalizationCombo, QObject::tr("None"), 0,
                     QObject::tr("Do not brightness-match lights before stacking. Use only for already consistent "
                                 "data or diagnostic runs."));
    ui.calibrationNormalizationLabel = new QLabel(QObject::tr("Bias/Dark: none, Flat: multiplicative scaling"),
                                                  ui.normalizationSection->contentWidget());
    ui.calibrationNormalizationLabel->setObjectName(QStringLiteral("liteCalibrationNormalizationLabel"));
    setLiteControlTooltip(ui.calibrationNormalizationLabel,
                          QObject::tr("Calibration masters use fixed Lite defaults: bias and dark frames are not "
                                      "normalized, while flats use multiplicative scaling."));
    normalizationLayout->addRow(QObject::tr("Lights"), ui.normalizationCombo);
    normalizationLayout->addRow(QObject::tr("Calibration"), ui.calibrationNormalizationLabel);

    auto* stackingLayout = addSection(QObject::tr("Stacking: average, winsorized rejection, drizzle off"),
                                      QStringLiteral("liteStackingSection"), ui.stackingSection);
    setLiteControlTooltip(ui.stackingSection,
                          QObject::tr("Controls how registered lights are combined into the final integrated image."));
    ui.stackMethodCombo = new QComboBox(ui.stackingSection->contentWidget());
    ui.stackMethodCombo->setObjectName(QStringLiteral("liteStackMethodCombo"));
    setLiteControlTooltip(ui.stackMethodCombo, QObject::tr("Choose how the registered light frames are combined."));
    addLiteComboTextItem(ui.stackMethodCombo, QObject::tr("Average"),
                         QObject::tr("Average all frames. Best signal-to-noise for normal, well-rejected stacks."));
    addLiteComboTextItem(ui.stackMethodCombo, QObject::tr("Sum"),
                         QObject::tr("Add all accepted frames together. Useful for diagnostic or specialized linear "
                                     "workflows."));
    addLiteComboTextItem(ui.stackMethodCombo, QObject::tr("Maximum"),
                         QObject::tr("Keep the brightest accepted pixel value from the stack. Mostly useful for "
                                     "diagnostic or special-purpose masters."));
    addLiteComboTextItem(ui.stackMethodCombo, QObject::tr("Minimum"),
                         QObject::tr("Keep the darkest accepted pixel value from the stack. Mostly useful for "
                                     "diagnostic or special-purpose masters."));
    addLiteComboTextItem(ui.stackMethodCombo, QObject::tr("Median"),
                         QObject::tr("Use the median value at each pixel. Robust, but usually noisier than average."));
    addLiteComboTextItem(ui.stackMethodCombo, QObject::tr("Auto-Adaptive Weighted Average"),
                         QObject::tr("Blend frames with adaptive weights so better frames contribute more strongly."));
    ui.rejectionMethodCombo = new QComboBox(ui.stackingSection->contentWidget());
    ui.rejectionMethodCombo->setObjectName(QStringLiteral("liteRejectionMethodCombo"));
    setLiteControlTooltip(ui.rejectionMethodCombo,
                          QObject::tr("Choose how outlier pixels, such as satellites, hot pixels, and cosmic rays, "
                                      "are rejected while stacking."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("Auto"),
                         QObject::tr("Choose a rejection method from the frame count, matching the full Image "
                                     "Integration recommendation tiers."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("None"),
                         QObject::tr("Do not reject outlier pixels. Useful only for tests or very small stacks."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("Sigma Clipping"),
                         QObject::tr("Reject pixels that deviate strongly from the stack average."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("MAD Clipping"),
                         QObject::tr("Reject outliers using median absolute deviation, which is robust for uneven "
                                     "data."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("Median Sigma"),
                         QObject::tr("Reject outliers by measuring deviation from the median. Useful when the average "
                                     "is pulled by strong artifacts."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("Winsorized Sigma Clipping"),
                         QObject::tr("A robust sigma-clipping method that works well for common deep-sky stacks."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("Percentile"),
                         QObject::tr("Reject a pixel further below the median than the low fraction of the median, or "
                                     "further above it than the high fraction. Useful for small stacks and "
                                     "calibration masters."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("GESDT"),
                         QObject::tr("Use a generalized outlier test. Best suited to large stacks with many frames."));
    addLiteComboTextItem(ui.rejectionMethodCombo, QObject::tr("Linear Fit"),
                         QObject::tr("Fit a linear model to each pixel stack and reject deviations. Good for "
                                     "satellite trails and airplane trails."));
    ui.rejectionLowLabel = new QLabel(QObject::tr("Sigma Low"), ui.stackingSection->contentWidget());
    ui.rejectionLowLabel->setObjectName(QStringLiteral("liteRejectionLowLabel"));
    ui.rejectionLowSpin = new QDoubleSpinBox(ui.stackingSection->contentWidget());
    ui.rejectionLowSpin->setObjectName(QStringLiteral("liteRejectionLowSpin"));
    ui.rejectionLowSpin->setDecimals(2);
    ui.rejectionLowSpin->setRange(0.01, 10.0);
    ui.rejectionLowSpin->setSingleStep(0.1);
    ui.rejectionLowSpin->setValue(3.0);
    ui.rejectionLowSpin->setMinimumWidth(ui.rejectionLowSpin->sizeHint().width());
    ui.rejectionLowLabel->setVisible(false);
    ui.rejectionLowSpin->setVisible(false);
    ui.rejectionLowLabel->setEnabled(false);
    ui.rejectionLowSpin->setEnabled(false);
    setLiteControlTooltip(ui.rejectionLowLabel,
                          QObject::tr("Lower rejection threshold for the selected rejection method."));
    setLiteControlTooltip(ui.rejectionLowSpin,
                          QObject::tr("Lower rejection threshold for the selected rejection method."));
    ui.rejectionHighLabel = new QLabel(QObject::tr("Sigma High"), ui.stackingSection->contentWidget());
    ui.rejectionHighLabel->setObjectName(QStringLiteral("liteRejectionHighLabel"));
    ui.rejectionHighSpin = new QDoubleSpinBox(ui.stackingSection->contentWidget());
    ui.rejectionHighSpin->setObjectName(QStringLiteral("liteRejectionHighSpin"));
    ui.rejectionHighSpin->setDecimals(2);
    ui.rejectionHighSpin->setRange(0.01, 10.0);
    ui.rejectionHighSpin->setSingleStep(0.1);
    ui.rejectionHighSpin->setValue(3.0);
    ui.rejectionHighSpin->setMinimumWidth(ui.rejectionHighSpin->sizeHint().width());
    ui.rejectionHighLabel->setVisible(false);
    ui.rejectionHighSpin->setVisible(false);
    ui.rejectionHighLabel->setEnabled(false);
    ui.rejectionHighSpin->setEnabled(false);
    setLiteControlTooltip(ui.rejectionHighLabel,
                          QObject::tr("Upper rejection threshold for the selected rejection method."));
    setLiteControlTooltip(ui.rejectionHighSpin,
                          QObject::tr("Upper rejection threshold for the selected rejection method."));
    ui.weightingCombo = new QComboBox(ui.stackingSection->contentWidget());
    ui.weightingCombo->setObjectName(QStringLiteral("liteWeightingCombo"));
    setLiteControlTooltip(ui.weightingCombo,
                          QObject::tr("Choose how much influence each light frame has in the final stack."));
    addLiteComboTextItem(ui.weightingCombo, QObject::tr("None"),
                         QObject::tr("Give every accepted light frame the same weight."));
    addLiteComboTextItem(ui.weightingCombo, QObject::tr("Number of stars"),
                         QObject::tr("Favor frames with more detected stars, often a sign of clearer data."));
    addLiteComboTextItem(ui.weightingCombo, QObject::tr("Weighted FWHM"),
                         QObject::tr("Favor frames with tighter stars, which usually means better focus and seeing."));
    addLiteComboTextItem(ui.weightingCombo, QObject::tr("Noise"),
                         QObject::tr("Favor frames with lower measured noise."));
    ui.solveFinalMasterCheck =
        new QCheckBox(QObject::tr("Plate solve final masters"), ui.stackingSection->contentWidget());
    ui.solveFinalMasterCheck->setObjectName(QStringLiteral("liteSolveFinalMasterCheck"));
    ui.solveFinalMasterCheck->setChecked(false);
    setLiteControlTooltip(ui.solveFinalMasterCheck,
                          QObject::tr("After stacking, solve the final integrated image if WCS metadata could not be "
                                      "carried forward from registration. This only runs on final outputs, not every "
                                      "input frame."));
    ui.drizzleEnabledCheck = new QCheckBox(QObject::tr("Enable drizzle"), ui.stackingSection->contentWidget());
    ui.drizzleEnabledCheck->setObjectName(QStringLiteral("liteDrizzleEnabledCheck"));
    setLiteControlTooltip(ui.drizzleEnabledCheck,
                          QObject::tr("Reconstruct a higher-resolution stack from well dithered frames. Leave this off "
                                      "unless your lights are dithered and registration is reliable."));
    ui.drizzleScaleCombo = new QComboBox(ui.stackingSection->contentWidget());
    ui.drizzleScaleCombo->setObjectName(QStringLiteral("liteDrizzleScaleCombo"));
    setLiteControlTooltip(ui.drizzleScaleCombo,
                          QObject::tr("Choose the output magnification for drizzle. 2x is the common choice; 3x uses "
                                      "more memory and time."));
    addLiteComboItem(ui.drizzleScaleCombo, QObject::tr("2x"), 2.0,
                     QObject::tr("Double the output resolution. This is the most common drizzle scale."));
    addLiteComboItem(ui.drizzleScaleCombo, QObject::tr("3x"), 3.0,
                     QObject::tr("Triple the output resolution. Use only with strong data and enough memory."));
    ui.drizzlePixelFractionCombo = new QComboBox(ui.stackingSection->contentWidget());
    ui.drizzlePixelFractionCombo->setObjectName(QStringLiteral("liteDrizzlePixelFractionCombo"));
    setLiteControlTooltip(ui.drizzlePixelFractionCombo,
                          QObject::tr("Choose the size of each input pixel footprint. Lower values can sharpen, but "
                                      "need good dithering to avoid gaps."));
    addLiteComboItem(ui.drizzlePixelFractionCombo, QObject::tr("0.9"), 0.9,
                     QObject::tr("Slightly shrink each pixel footprint. A balanced default for most drizzle stacks."));
    addLiteComboItem(ui.drizzlePixelFractionCombo, QObject::tr("0.8"), 0.8,
                     QObject::tr("Use a smaller footprint for a sharper result when dithering coverage is strong."));
    addLiteComboItem(ui.drizzlePixelFractionCombo, QObject::tr("1.0"), 1.0,
                     QObject::tr("Use the full pixel footprint for smoother coverage and lower risk of gaps."));
    ui.drizzleScaleCombo->setEnabled(false);
    ui.drizzlePixelFractionCombo->setEnabled(false);
    stackingLayout->addRow(QObject::tr("Method"), ui.stackMethodCombo);
    stackingLayout->addRow(QObject::tr("Rejection"), ui.rejectionMethodCombo);
    stackingLayout->addRow(ui.rejectionLowLabel, ui.rejectionLowSpin);
    stackingLayout->addRow(ui.rejectionHighLabel, ui.rejectionHighSpin);
    stackingLayout->addRow(QObject::tr("Weighting"), ui.weightingCombo);
    stackingLayout->addRow(QString(), ui.solveFinalMasterCheck);
    stackingLayout->addRow(QString(), ui.drizzleEnabledCheck);
    stackingLayout->addRow(QObject::tr("Drizzle scale"), ui.drizzleScaleCombo);
    stackingLayout->addRow(QObject::tr("Drizzle pixfrac"), ui.drizzlePixelFractionCombo);

    ui.settingsCard->setContentLayout(settingsLayout);
    ui.mainLayout->addWidget(ui.settingsCard);

    ui.framesCard = new AnCardWidget(QObject::tr("Frames"), parent);
    ui.framesCard->setObjectName(QStringLiteral("liteFramesCard"));
    auto* framesLayout = new QVBoxLayout();
    framesLayout->setContentsMargins(spacing.md, spacing.md, spacing.md, spacing.md);
    framesLayout->setSpacing(spacing.sm);

    auto* frameButtonRow = new QWidget(ui.framesCard);
    auto* frameButtonLayout = new QHBoxLayout(frameButtonRow);
    frameButtonLayout->setContentsMargins(0, 0, 0, 0);
    frameButtonLayout->setSpacing(spacing.sm);
    auto* importTypeLabel = new QLabel(QObject::tr("Add as"), frameButtonRow);
    setLiteControlTooltip(importTypeLabel, QObject::tr("Classification to apply when adding new frames."));
    ui.importTypeCombo = new QComboBox(frameButtonRow);
    ui.importTypeCombo->setObjectName(QStringLiteral("liteImportTypeCombo"));
    setLiteControlTooltip(ui.importTypeCombo,
                          QObject::tr("Choose how newly added files are classified. Auto-detect reads the frame type "
                                      "from the file's metadata, then from its folder's name; use a fixed type when "
                                      "both are missing or wrong."));
    addLiteComboItem(ui.importTypeCombo, QObject::tr("Auto-detect"), -1,
                     QObject::tr("Read the frame type from the file's header, then from a folder on its path named "
                                 "for a frame type (Flats, Darks, Bias or Lights); add any other file as a light "
                                 "frame."));
    addLiteComboItem(ui.importTypeCombo, QObject::tr("Light"), 3,
                     QObject::tr("Add files as light frames, the science exposures that will be registered and "
                                 "stacked."));
    addLiteComboItem(ui.importTypeCombo, QObject::tr("Flat"), 2,
                     QObject::tr("Add files as flat frames used to correct dust shadows and vignetting."));
    addLiteComboItem(ui.importTypeCombo, QObject::tr("Dark"), 1,
                     QObject::tr("Add files as dark frames used to remove thermal signal from lights."));
    addLiteComboItem(ui.importTypeCombo, QObject::tr("Dark Flat"), 4,
                     QObject::tr("Add files as darks for flats. Lite applies these in the background while building "
                                 "master flats."));
    addLiteComboItem(ui.importTypeCombo, QObject::tr("Bias"), 0,
                     QObject::tr("Add files as bias frames used to remove camera readout offset."));
    ui.importTypeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    ui.importTypeCombo->setMinimumWidth(liteComboWidthForUnclippedItems(ui.importTypeCombo));
    ui.importTypeCombo->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    ui.addFilesButton = new QPushButton(QObject::tr("Add Files"), frameButtonRow);
    ui.addFolderButton = new QPushButton(QObject::tr("Add Folder"), frameButtonRow);
    ui.changeFrameTypeButton = new QPushButton(QObject::tr("Change Type"), frameButtonRow);
    ui.clearAllButton = new QPushButton(QObject::tr("Clear All"), frameButtonRow);
    ui.addFilesButton->setObjectName(QStringLiteral("liteAddFilesButton"));
    ui.addFolderButton->setObjectName(QStringLiteral("liteAddFolderButton"));
    ui.changeFrameTypeButton->setObjectName(QStringLiteral("liteChangeFrameTypeButton"));
    ui.clearAllButton->setObjectName(QStringLiteral("liteClearAllButton"));
    setLiteControlTooltip(ui.addFilesButton, QObject::tr("Select one or more image files to add to the frame list."));
    setLiteControlTooltip(ui.addFolderButton,
                          QObject::tr("Add all supported image files from a folder and its subfolders."));
    setLiteControlTooltip(ui.changeFrameTypeButton,
                          QObject::tr("Change the type of the selected frame rows when metadata or auto-detection is "
                                      "wrong."));
    setLiteControlTooltip(ui.clearAllButton, QObject::tr("Remove all loaded frames from this Lite process."));
    frameButtonLayout->addWidget(importTypeLabel);
    frameButtonLayout->addWidget(ui.importTypeCombo);
    frameButtonLayout->addWidget(ui.addFilesButton);
    frameButtonLayout->addWidget(ui.addFolderButton);
    frameButtonLayout->addWidget(ui.changeFrameTypeButton);
    frameButtonLayout->addStretch(1);
    frameButtonLayout->addWidget(ui.clearAllButton);
    framesLayout->addWidget(frameButtonRow);

    ui.frameTable = new QTableWidget(ui.framesCard);
    ui.frameTable->setObjectName(QStringLiteral("liteFrameTable"));
    ui.frameTable->setColumnCount(6);
    ui.frameTable->setHorizontalHeaderLabels({QObject::tr("Type"), QObject::tr("File"), QObject::tr("Filter"),
                                              QObject::tr("Object"), QObject::tr("Exposure"),
                                              QObject::tr("Calibration")});
    ui.frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.frameTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui.frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui.frameTable->setAlternatingRowColors(true);
    setLiteControlTooltip(ui.frameTable,
                          QObject::tr("Review loaded frames. The Calibration column shows which flat, dark, bias, or "
                                      "Dark Flat masters each frame will use."));
    ui.frameTable->verticalHeader()->setVisible(false);
    ui.frameTable->horizontalHeader()->setStretchLastSection(false);
    ui.frameTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui.frameTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    framesLayout->addWidget(ui.frameTable, 1);

    ui.framesCard->setContentLayout(framesLayout);
    ui.mainLayout->addWidget(ui.framesCard, 1);

    ui.buttonBox = new QDialogButtonBox(parent);
    ui.buttonBox->setObjectName(QStringLiteral("liteButtonBox"));
    ui.runButton = ui.buttonBox->addButton(QObject::tr("Run Integration"), QDialogButtonBox::AcceptRole);
    ui.runButton->setObjectName(QStringLiteral("liteRunIntegrationButton"));
    setLiteControlTooltip(ui.runButton,
                          QObject::tr("Start the Lite integration workflow with the current frames and settings."));
    ui.openFullProcessButton = ui.buttonBox->addButton(QObject::tr("Open Full Process"), QDialogButtonBox::ActionRole);
    ui.openFullProcessButton->setObjectName(QStringLiteral("liteOpenFullProcessButton"));
    setLiteControlTooltip(ui.openFullProcessButton,
                          QObject::tr("Open the full Image Integration process for advanced controls and manual "
                                      "workflow tuning."));
    auto* closeButton = ui.buttonBox->addButton(QDialogButtonBox::Close);
    setLiteControlTooltip(closeButton, QObject::tr("Close the Lite process window without starting a new run."));
    ui.mainLayout->addWidget(ui.buttonBox);

    configureImageIntegrationLiteSectionChrome(ui);

    parent->setLayout(ui.mainLayout);
    return ui;
}

} // namespace an::ui::processes
