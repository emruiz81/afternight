// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StackingSettingsDialog.h"
#include "LocalNormalizationSettingsDialog.h"
#include "StackingRecommendationDialog.h"
#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/FixedDialogUiSizing.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/core/TooltipFormatUtils.h"

#include <AfterNightPlatform/Logging.h>

#include <cmath>
#include <verdigris/wobjectimpl.h>
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

W_OBJECT_IMPL(StackingSettingsDialog)

namespace
{
constexpr auto kPhotometricWeightingText = "Photometric";
constexpr auto kPhotometricWeightingDisabledText = "Photometric (requires photometric zero-point data)";
/// The data of the Normalization choice's item that shows the global match local normalization runs.
constexpr auto kSetByLocalNormalizationKey = "setByLocalNormalization";
/// The property a value control keeps the stored value it was set to under.
constexpr auto kStoredValueProperty = "anStoredValue";

/// Sets a value control to a value the settings store, and keeps the value as stored. A control shows a value to its
/// decimals; one the settings hold more finely - a file edited by hand - would otherwise be rounded each time the
/// dialog is accepted, changing the settings and their fingerprints without anyone changing them.
void setStoredValue(QDoubleSpinBox& control, const double value)
{
    control.setValue(value);
    control.setProperty(kStoredValueProperty, value);
}

/// The value a control gives: the stored value it was set to while it still shows that value, otherwise the one it
/// shows - one the person set, or a stored one held to the control's range.
[[nodiscard]] double storedValueWhileShown(const QDoubleSpinBox& control)
{
    const QVariant stored = control.property(kStoredValueProperty);
    if (stored.isValid())
    {
        const double value = stored.toDouble();
        // The control rounds a value to its decimals as it shows it.
        if (std::isfinite(value) && QString::number(value, 'f', control.decimals()).toDouble() == control.value())
        {
            return value;
        }
    }
    return control.value();
}

/// The global method a Normalization choice names; None, and an item that names no method, name none.
[[nodiscard]] std::optional<GlobalNormalizationMethod> globalNormalizationMethodNamed(const QString& text)
{
    if (text.compare(QStringLiteral("Additive"), Qt::CaseInsensitive) == 0)
    {
        return GlobalNormalizationMethod::Additive;
    }
    if (text.compare(QStringLiteral("Additive With Scaling"), Qt::CaseInsensitive) == 0)
    {
        return GlobalNormalizationMethod::AdditiveScaling;
    }
    if (text.compare(QStringLiteral("Multiplicative"), Qt::CaseInsensitive) == 0)
    {
        return GlobalNormalizationMethod::Multiplicative;
    }
    if (text.compare(QStringLiteral("Multiplicative With Scaling"), Qt::CaseInsensitive) == 0)
    {
        return GlobalNormalizationMethod::MultiplicativeScaling;
    }
    return std::nullopt;
}

/// Whether the stack raises a weighting's frames to the group's floor: the star-count and width weights the engine
/// floors, and the photometric model's raw weight, whose floor is the group's. A noise or image-count weight is a ratio
/// that keeps every frame and takes none.
[[nodiscard]] bool weightingTakesFloor(const QString& weighting)
{
    const ImageIntegration::PersistedChoiceValue value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::Weighting, weighting);
    const std::optional<an::StackWeighting> source =
        value.known ? ImageIntegration::stackWeightingForKey(value.key) : std::nullopt;
    return source.has_value() &&
           (an::stackWeightingUsesMinimumWeight(*source) || *source == an::StackWeighting::Photometric);
}
} // namespace

StackingSettingsDialog::StackingSettingsDialog(FrameType frameType, const QString& groupLabel, QWidget* parent)
    : QDialog(parent), m_frameType(frameType), m_groupLabel(groupLabel)
{
    m_ui = an::ui::imageintegration::buildStackingSettingsDialogUi(this);

    // Set the group label text
    this->m_ui.groupLabel->setText(m_groupLabel);

    // Set window title based on frame type
    switch (frameType)
    {
    case FrameType::BIAS:
        setWindowTitle(tr("BIAS Stacking Settings"));
        break;
    case FrameType::DARK:
        setWindowTitle(tr("DARK Stacking Settings"));
        break;
    case FrameType::FLAT:
        setWindowTitle(tr("FLAT Stacking Settings"));
        break;
    case FrameType::LIGHT:
        setWindowTitle(tr("LIGHT Integration Stacking Settings"));
        break;
    }

    // Connect signals
    QObject::connect(m_ui.stackMethodSelector, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::on_stackMethodSelector_currentTextChanged);
    QObject::connect(m_ui.rejectionMethodSelector, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::on_rejectionMethodSelector_currentTextChanged);
    QObject::connect(m_ui.createRejectionMaps, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateRejectionControlsState);
    QObject::connect(m_ui.spatialRejectionClustering, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateRejectionControlsState);
    QObject::connect(m_ui.restackExcludingFlaggedFrames, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateRejectionControlsState);

    // Connect weighting signals (LIGHT frames only)
    QObject::connect(m_ui.weightingSelector, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::on_weightingSelector_currentTextChanged);
    QObject::connect(m_ui.starCountScalingSelector, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::on_starCountScaling_currentTextChanged);
    QObject::connect(m_ui.localWeightingEnabled, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateWeightingControlsState);
    QObject::connect(m_ui.weightingOptionsButton, &QPushButton::clicked, this,
                     &StackingSettingsDialog::openWeightingOptionsDialog);
    QObject::connect(m_ui.hdrExposureMergeEnabled, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::on_hdrExposureMergeEnabled_toggled);

    // Connect local normalization signals (LIGHT frames only)
    QObject::connect(m_ui.localNormalizationEnabled, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateLocalNormalizationControlsState);
    QObject::connect(m_ui.localNormalizationConfigureButton, &QPushButton::clicked, this,
                     &StackingSettingsDialog::openLocalNormalizationDialog);

    QObject::connect(m_ui.normalizationReferenceMode, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::updateNormalizationReferenceControlsState);
    QObject::connect(m_ui.normalizationReferenceMode, &QComboBox::currentTextChanged, this,
                     [this](const QString& mode) {
                         if (mode.compare(QStringLiteral("Manual"), Qt::CaseInsensitive) != 0)
                         {
                             automaticNormalizationReferenceDisplayRequested();
                         }
                     });
    QObject::connect(m_ui.normalizationMode, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::updateNormalizationReferenceControlsState);
    QObject::connect(m_ui.normalizationMode, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::updateNormalizationOptionsState);
    QObject::connect(m_ui.overlapNormalizationEnabled, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateNormalizationOptionsState);
    QObject::connect(m_ui.normalizationReferenceBestButton, &QPushButton::clicked, this,
                     [this]() { bestNormalizationReferenceRequested(); });

    // Connect buttons
    QObject::connect(m_ui.resetButton, &QPushButton::clicked, this, &StackingSettingsDialog::on_resetButton_clicked);
    QObject::connect(m_ui.applyToAllButton, &QPushButton::clicked, this,
                     &StackingSettingsDialog::on_applyToAllButton_clicked);
    QObject::connect(m_ui.recommendedButton, &QPushButton::clicked, this,
                     &StackingSettingsDialog::showRecommendedSettingsPopup);

    for (auto* section : {m_ui.stackSettingsGroupBox, m_ui.rejectionSettingsGroupBox, m_ui.drizzleSettingsGroupBox})
    {
        if (section)
        {
            an::ui::processes::fixed_dialog_ui::connectSectionResizeBehavior(section->toggleButton(), this,
                                                                             section->animationDuration());
        }
    }

    initialize();

    // Recommended button starts disabled until setFrameCount provides a valid count
    m_ui.recommendedButton->setEnabled(false);

    an::ui::processes::fixed_dialog_ui::scheduleDialogResize(this);
}

StackingSettingsDialog::~StackingSettingsDialog() {}

void StackingSettingsDialog::initialize()
{
    // Populate Stack Method combo box
    m_ui.stackMethodSelector->addItem("Average");
    m_ui.stackMethodSelector->addItem("Sum");
    m_ui.stackMethodSelector->addItem("Maximum");
    m_ui.stackMethodSelector->addItem("Minimum");
    m_ui.stackMethodSelector->addItem("Median");
    m_ui.stackMethodSelector->addItem("Auto-Adaptive Weighted Average");
    m_ui.stackMethodSelector->setItemData(m_ui.stackMethodSelector->count() - 1,
                                          tr("Automatic robust mean; no rejection thresholds needed."),
                                          Qt::ToolTipRole);

    // Populate Normalization combo box
    m_ui.normalizationMode->addItem("None");
    m_ui.normalizationMode->addItem("Additive");
    m_ui.normalizationMode->addItem("Additive With Scaling");
    m_ui.normalizationMode->addItem("Multiplicative");
    m_ui.normalizationMode->addItem("Multiplicative With Scaling");

    m_ui.scaleEstimatorSelector->addItem("IKSS");
    m_ui.scaleEstimatorSelector->addItem("MAD");

    m_ui.normalizationReferenceMode->addItem("Automatic (best quality)");
    m_ui.normalizationReferenceMode->addItem("Manual");
    m_ui.normalizationReferenceMode->setToolTip(
        tr("Select how the normalization reference is chosen for this stacking group."));
    m_ui.normalizationReferenceFrame->setToolTip(tr("Reference frame used for normalization when Manual is selected."));

    // Populate Rejection Method combo box
    m_ui.rejectionMethodSelector->addItem("None");
    m_ui.rejectionMethodSelector->addItem("Sigma Clipping");
    m_ui.rejectionMethodSelector->addItem("MAD Clipping");
    m_ui.rejectionMethodSelector->addItem("Median Sigma");
    m_ui.rejectionMethodSelector->addItem("Winsorized Sigma Clipping");
    // Additional rejection algorithms
    m_ui.rejectionMethodSelector->addItem("Percentile");
    m_ui.rejectionMethodSelector->addItem("GESDT");
    m_ui.rejectionMethodSelector->addItem("Linear Fit");

    // Populate Weighting combo box
    m_ui.weightingSelector->addItem("None");
    m_ui.weightingSelector->addItem("Number of stars");
    m_ui.weightingSelector->addItem("Weighted FWHM");
    m_ui.weightingSelector->addItem("Noise");
    m_ui.weightingSelector->addItem("Number of images");
    m_ui.weightingSelector->addItem(QString::fromLatin1(kPhotometricWeightingText));
    refreshPhotometricWeightingItem();

    // Populate Star Count Scaling combo box (for weighting options)
    m_ui.starCountScalingSelector->addItem("Linear");
    m_ui.starCountScalingSelector->addItem("Quadratic");
    m_ui.starCountScalingSelector->addItem("Exponential");
    m_ui.starCountScalingSelector->addItem("Roundness Weighted");

    // Show/hide weighting based on frame type (only for LIGHT)
    bool isLight = (m_frameType == FrameType::LIGHT);
    m_ui.weightingLabel->setVisible(isLight);
    m_ui.weightingSelector->setVisible(isLight);
    m_ui.weightingOptionsButton->setVisible(isLight);
    m_ui.weightingOptionsGroupBox->setVisible(false);
    m_ui.createQualityMaps->setVisible(isLight);
    m_ui.discardRegisteredOutputsAfterStacking->setVisible(isLight);
    m_ui.solveFinalMasterIfNeeded->setVisible(isLight);
    m_ui.spatialRejectionClustering->setVisible(isLight);
    m_ui.restackExcludingFlaggedFrames->setVisible(isLight);
    m_ui.hdrExposureMergeEnabled->setVisible(isLight);
    m_ui.hdrExposureMergeInfoLabel->setVisible(isLight);
    m_ui.localWeightingEnabled->setVisible(isLight);
    m_ui.localWeightingWindowSizeLabel->setVisible(isLight);
    m_ui.localWeightingWindowSizeValue->setVisible(isLight);
    m_ui.localWeightingGridSpacingLabel->setVisible(isLight);
    m_ui.localWeightingGridSpacingValue->setVisible(isLight);
    m_ui.localWeightingSnrExponentLabel->setVisible(isLight);
    m_ui.localWeightingSnrExponentValue->setVisible(isLight);

    // Show/hide local normalization settings (only for LIGHT frames)
    m_ui.localNormalizationEnabled->setVisible(isLight);
    m_ui.localNormalizationConfigureButton->setVisible(isLight);
    m_ui.localNormalizationPresetLabel->setVisible(isLight);

    m_ui.normalizationReferenceMode->setVisible(isLight);
    m_ui.normalizationReferenceFrame->setVisible(isLight);
    m_ui.normalizationReferenceBestButton->setVisible(isLight);
    m_ui.label_normReferenceMode->setVisible(isLight);
    m_ui.label_normReferenceFrame->setVisible(isLight);

    // Overlap normalization matches the lights over the areas they share; no master stack reads it.
    m_ui.overlapNormalizationEnabled->setVisible(isLight);
    m_ui.overlapMinimumPixelsLabel->setVisible(isLight);
    m_ui.overlapMinimumPixelsValue->setVisible(isLight);
    m_ui.overlapMinimumFractionLabel->setVisible(isLight);
    m_ui.overlapMinimumFractionValue->setVisible(isLight);

    // Show/hide drizzle settings (only for LIGHT frames)
    m_ui.drizzleSettingsGroupBox->setVisible(isLight);

    // Populate drizzle scale combo box
    m_ui.drizzleScaleSelector->addItem("1.0× (no upscaling)");
    m_ui.drizzleScaleSelector->addItem("1.5×");
    m_ui.drizzleScaleSelector->addItem("2.0× (recommended)");
    m_ui.drizzleScaleSelector->addItem("3.0×");

    // Populate drizzle kernel combo box
    m_ui.drizzleKernelSelector->addItem("Square (recommended)");
    m_ui.drizzleKernelSelector->addItem("Point");
    m_ui.drizzleKernelSelector->addItem("Turbo");
    m_ui.drizzleKernelSelector->addItem("Gaussian");
    m_ui.drizzleKernelSelector->addItem("Lanczos2");
    m_ui.drizzleKernelSelector->addItem("Lanczos3");

    // Connect drizzle signals
    QObject::connect(m_ui.chkEnableDrizzle, &QCheckBox::toggled, this, &StackingSettingsDialog::updateDrizzleInfoLabel);
    QObject::connect(m_ui.chkEnableDrizzle, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateWeightingControlsState);
    QObject::connect(m_ui.chkEnableDrizzle, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateRejectionControlsState);
    QObject::connect(m_ui.chkEnableDrizzle, &QCheckBox::toggled, this,
                     &StackingSettingsDialog::updateLocalNormalizationControlsState);
    QObject::connect(m_ui.drizzleScaleSelector, &QComboBox::currentTextChanged, this,
                     &StackingSettingsDialog::updateDrizzleInfoLabel);

    // Set default values for sigma spinboxes
    m_ui.sigmaLowValue->setValue(3.0);
    m_ui.sigmaHighValue->setValue(3.0);

    // Set defaults based on frame type
    resetToDefaults();

    // Initial state update
    updateRejectionControlsState();
    updateRejectionLabels();
    updateWeightingControlsState();
    updateHdrControlsState();
    updateLocalNormalizationControlsState();
    updateNormalizationReferenceControlsState();
    updateNormalizationOptionsState();

    applyTooltips();
}

void StackingSettingsDialog::applyTooltips()
{
    using an::ui::imageintegration::formatTooltip;
    using an::ui::imageintegration::setSharedToolTip;
    using an::ui::imageintegration::tooltipBullets;

    // --- Stack Method ---
    const QString stackMethodTip = formatTooltip(
        tr("Stack method"),
        {tr("What it does:\nSelects the algorithm used to combine multiple frames into a single master."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Average: arithmetic mean of all pixels \u2014 best SNR, works with rejection."),
                  tr("Sum: adds pixel values without dividing \u2014 preserves relative flux."),
                  tr("Maximum: keeps the brightest value at each pixel \u2014 useful for meteor trails or comet "
                     "tails."),
                  tr("Minimum: keeps the darkest value at each pixel \u2014 useful for finding persistent hot pixels."),
                  tr("Median: takes the middle value \u2014 robust to outliers but lower SNR than clipped average."),
                  tr("Auto-Adaptive Weighted Average: robust mean with automatic outlier handling \u2014 no rejection "
                     "thresholds needed.")})),
         tr("Good to know: rejection methods are only available with Average stacking.")});
    setSharedToolTip(stackMethodTip, m_ui.label_stackMethod, m_ui.stackMethodSelector);

    // --- Normalization Mode ---
    const QString normTip = formatTooltip(
        tr("Normalization"),
        {tr("What it does:\nEqualizes brightness and/or scale across frames before stacking so that small exposure or "
            "transparency differences do not degrade the result."),
         tr("Options:\n%1")
             .arg(tooltipBullets({tr("None: no brightness adjustment \u2014 use for bias and dark frames, or when all "
                                     "lights are already matched."),
                                  tr("Additive: shifts each frame by a constant so the backgrounds match \u2014 "
                                     "corrects for sky glow or offset changes."),
                                  tr("Additive With Scaling: shifts and scales each frame \u2014 also compensates for "
                                     "transparency or exposure differences."),
                                  tr("Multiplicative: scales each frame so the means match \u2014 good when the "
                                     "background is already consistent."),
                                  tr("Multiplicative With Scaling: extends multiplicative with a scale factor \u2014 "
                                     "handles both gain and mean differences.")})),
         tr("Typical choices:\n%1")
             .arg(tooltipBullets({tr("Lights: Additive With Scaling (default) or Multiplicative With Scaling."),
                                  tr("Flats: Multiplicative."), tr("Bias / Darks: None.")}))});
    const bool normalizationFixedAtNone = m_frameType == FrameType::BIAS || m_frameType == FrameType::DARK;
    setSharedToolTip(normalizationFixedAtNone
                         ? formatTooltip(tr("Normalization"),
                                         {tr("Fixed at None: a bias or dark master is combined without normalization, "
                                             "so the master holds its frames' own levels.")})
                         : normTip,
                     m_ui.label_normalization, m_ui.normalizationMode);

    // --- Scale Estimator ---
    const QString scaleEstimatorTip = formatTooltip(
        tr("Scale estimator"),
        {tr("What it does:\nChooses the dispersion each frame's scale is measured by when the normalization scales: "
            "Additive With Scaling, Multiplicative With Scaling, and the global match local normalization runs."),
         tr("Options:\n%1")
             .arg(tooltipBullets({tr("IKSS: the iterative k-sigma estimate of location and spread \u2014 robust and "
                                     "efficient (default)."),
                                  tr("MAD: the median absolute deviation \u2014 robust, and cheaper to measure.")})),
         tr("Good to know: Additive and Multiplicative measure no scale and do not read it.")});
    setSharedToolTip(scaleEstimatorTip, m_ui.label_scaleEstimator, m_ui.scaleEstimatorSelector);

    // --- Overlap Normalization ---
    const QString overlapTip = formatTooltip(
        tr("Overlap normalization"),
        {tr("What it does:\nMatches the lights over the areas they share and solves every match together, so the "
            "panels of a mosaic agree where they meet, instead of matching each light to the reference alone."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets({tr("Your lights are mosaic panels, or are offset so far that they share little of "
                                     "the reference's area.")})),
         tr("Good to know: it needs global normalization and registered lights. When the lights' overlaps do not "
            "connect, the run falls back to standard global normalization and says so.")});
    m_ui.overlapNormalizationEnabled->setToolTip(overlapTip);
    setSharedToolTip(formatTooltip(tr("Minimum overlap pixels"),
                                   {tr("What it does:\nLeaves out a pair of lights whose shared rectangle holds fewer "
                                       "pixels than this."),
                                    tr("Default: 1000")}),
                     m_ui.overlapMinimumPixelsLabel, m_ui.overlapMinimumPixelsValue);
    setSharedToolTip(formatTooltip(tr("Minimum overlap fraction"),
                                   {tr("What it does:\nLeaves out a pair of lights whose shared rectangle is smaller "
                                       "than this fraction of the smaller light's area."),
                                    tr("Default: 0.01")}),
                     m_ui.overlapMinimumFractionLabel, m_ui.overlapMinimumFractionValue);

    // --- Normalization Reference Mode ---
    const QString normRefModeTip = formatTooltip(
        tr("Normalization reference selection"),
        {tr("What it does:\nDetermines which frame is used as the reference for normalization \u2014 all other frames "
            "are adjusted to match it."),
         tr("Options:\n%1")
             .arg(tooltipBullets({tr("Automatic (best quality): AfterNight picks the frame with the best quality score "
                                     "as the normalization target."),
                                  tr("Manual: lets you choose a specific frame as the normalization reference.")}))});
    m_ui.normalizationReferenceMode->setToolTip(normRefModeTip);

    // --- Normalization Reference Frame ---
    const QString normRefFrameTip = formatTooltip(
        tr("Normalization reference frame"),
        {tr("What it does:\nThe specific frame all other frames will be normalized against when Manual reference "
            "selection is active."),
         tr("Pick a frame that is:\n%1")
             .arg(tooltipBullets({tr("Representative of the group's typical exposure and sky conditions."),
                                  tr("Free of unusual gradients, clouds, or satellite trails.")}))});
    m_ui.normalizationReferenceFrame->setToolTip(normRefFrameTip);

    // --- HDR Exposure Merge ---
    const QString hdrTip = formatTooltip(
        tr("HDR Exposure Merge"),
        {tr("What it does:\nGroups frames by exposure time, stacks each bucket separately, then blends highlights from "
            "shorter exposures into the long-exposure master for extended dynamic range."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets(
                 {tr("You have lights taken at different exposure lengths (e.g. 60s and 300s) for the same target."),
                  tr("Bright stars or galaxy cores are clipped in your long exposures.")})),
         tr("Good to know: this mode disables normalization, drizzle, and rejection-map outputs for the group.")});
    m_ui.hdrExposureMergeEnabled->setToolTip(hdrTip);

    // --- Local Normalization ---
    const QString localNormTip = formatTooltip(
        tr("Local normalization"),
        {tr("What it does:\nDivides each frame into a grid and equalizes brightness locally, compensating for "
            "spatially varying sky gradients, light pollution, and vignetting that global normalization cannot "
            "handle."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets(
                 {tr("Your frames have uneven sky brightness (heavy light pollution, moon glow, gradients)."),
                  tr("Standard normalization leaves visible gradient seams in mosaic panels."),
                  tr("You want the cleanest possible stacking with minimal background artifacts.")})),
         tr("Good to know: overrides the global normalization mode when enabled. Use the Configure button to tune grid "
            "size, reference strategy, and other advanced parameters.")});
    m_ui.localNormalizationEnabled->setToolTip(localNormTip);

    const QString localNormConfigTip = formatTooltip(
        tr("Configure local normalization"),
        {tr("What it does:\nOpens a dialog with detailed local normalization settings \u2014 grid size, reference "
            "selection strategy, background estimation, scale evaluation, and clipping parameters."),
         tr("Presets are provided for common scenarios; choose Custom for full manual control.")});
    m_ui.localNormalizationConfigureButton->setToolTip(localNormConfigTip);

    // --- Rejection Method ---
    const QString rejectionTip = formatTooltip(
        tr("Rejection method"),
        {tr("What it does:\nIdentifies and discards outlier pixels (satellites, cosmic rays, hot pixels) before "
            "averaging so they do not contaminate the final result."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("None: no outlier removal \u2014 only for very small sets or when you trust every frame."),
                  tr("Sigma Clipping: rejects pixels beyond N sigma from the mean \u2014 simple and effective for 8+ "
                     "frames."),
                  tr("MAD Clipping: uses the Median Absolute Deviation, more robust than sigma against skewed "
                     "distributions."),
                  tr("Median Sigma: like sigma clipping but measures deviation from the median \u2014 better with "
                     "fewer frames."),
                  tr("Winsorized Sigma Clipping: replaces outliers with boundary values before re-estimating sigma "
                     "\u2014 robust for 15+ frames."),
                  tr("Percentile: rejects a sample further below the median than the low fraction of the median, "
                     "or further above it than the high fraction \u2014 robust for small sets of frames."),
                  tr("GESDT (Generalized ESD Test): statistical test for multiple outliers \u2014 optimal for 40+ "
                     "frames."),
                  tr("Linear Fit: fits a linear model to the pixel stack and rejects deviations \u2014 excellent for "
                     "satellite trails with 25+ frames.")})),
         tr("Good to know: rejection is only available when the stack method is Average.")});
    setSharedToolTip(rejectionTip, m_ui.label_rejectionMethod, m_ui.rejectionMethodSelector);

    // --- Sigma Low / High ---
    const QString sigmaLowTip = formatTooltip(
        tr("Sigma Low"),
        {tr("What it does:\nSets the low rejection threshold. Pixels falling more than this many sigma below the stack "
            "median/mean are rejected."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets(
                 {tr("Lower it to be more aggressive about removing dark outliers (cold pixels, sensor defects)."),
                  tr("Raise it to keep more data and avoid rejecting faint signal.")})),
         tr("Default: 3.0 (adapts to the selected rejection method)")});
    setSharedToolTip(sigmaLowTip, m_ui.sigmaLowLabel, m_ui.sigmaLowValue);

    const QString sigmaHighTip = formatTooltip(
        tr("Sigma High"),
        {tr("What it does:\nSets the high rejection threshold. Pixels exceeding the stack median/mean by more than "
            "this many sigma are rejected."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets(
                 {tr("Lower it to more aggressively remove bright outliers (satellites, cosmic rays, hot pixels)."),
                  tr("Raise it if you see dark holes where bright nebula cores or stars should be.")})),
         tr("Default: 3.0 (adapts to the selected rejection method)")});
    setSharedToolTip(sigmaHighTip, m_ui.sigmaHighLabel, m_ui.sigmaHighValue);

    // --- Create Rejection Maps ---
    const QString rejMapTip = formatTooltip(
        tr("Create Rejection Maps"),
        {tr("What it does:\nSaves a per-pixel map showing how many frames were rejected at each position. Useful for "
            "diagnosing whether rejection thresholds are too aggressive or too lenient."),
         tr("Good to know: the maps are saved alongside the master and can be loaded as images for visual "
            "inspection.")});
    m_ui.createRejectionMaps->setToolTip(rejMapTip);

    const QString mergeRejMapTip = formatTooltip(
        tr("Merge Rejection Maps"), {tr("What it does:\nCombines the low and high rejection maps into a single image, "
                                        "making it easier to review all rejected pixels at once.")});
    m_ui.mergeRejectionMaps->setToolTip(mergeRejMapTip);

    // --- Spatial Rejection Clustering ---
    const QString spatialClusterTip = formatTooltip(
        tr("Analyze Spatial Rejection Clusters"),
        {tr("What it does:\nAfter stacking, scans the rejection mask for connected clusters of rejected pixels (e.g. "
            "satellite trails, airplane streaks) and includes a spatial analysis in the workflow report."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets(
                 {tr("You want to verify that satellite trails and similar artifacts were properly rejected."),
                  tr("You want to identify frames that contributed disproportionately to rejections.")}))});
    m_ui.spatialRejectionClustering->setToolTip(spatialClusterTip);

    // --- Re-Stack Excluding Flagged Frames ---
    const QString restackTip = formatTooltip(
        tr("Re-Stack Excluding Flagged Frames"),
        {tr("What it does:\nAfter spatial cluster analysis, automatically re-stacks without frames whose per-pixel "
            "rejection rates are statistical outliers. This removes entire frames that are dominated by artifacts."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets(
                 {tr("You have some severely affected frames (heavy cloud, large satellite cluster) and want fully "
                     "automatic cleanup."),
                  tr("You prefer a hands-off workflow that removes problematic frames automatically.")})),
         tr("Good to know: disabled when drizzle or HDR exposure merge is active. Requires Analyze Spatial Rejection "
            "Clusters to be enabled.")});
    m_ui.restackExcludingFlaggedFrames->setToolTip(restackTip);

    // --- Create Quality Maps ---
    const QString qualMapTip = formatTooltip(
        tr("Create Quality Maps"),
        {
            tr("What it does:\nGenerates a per-pixel quality map showing the local signal-to-noise ratio across the "
               "final stacked image. Useful for assessing integration depth across the field."),
        });
    m_ui.createQualityMaps->setToolTip(qualMapTip);

    const QString registeredOutputTip = formatTooltip(
        tr("Allow Registered Images to Be Discarded"),
        {tr("What it does:\nLets eligible Average integrations apply registration transforms while reading the "
            "source images, avoiding registered-image files that are not needed after the stack finishes."),
         tr("Good to know: keep this disabled when you want registered images available for inspection or recovery. "
            "Unsupported configurations continue to retain registered images automatically.")});
    m_ui.discardRegisteredOutputsAfterStacking->setToolTip(registeredOutputTip);

    // --- Solve Final Master If Needed ---
    const QString solveTip =
        formatTooltip(tr("Plate-Solve Final Master If Needed"),
                      {tr("What it does:\nIf the stacked master does not have valid astrometric (WCS) metadata after "
                          "integration \u2014 for example because registration did not propagate coordinates \u2014 "
                          "AfterNight will automatically plate-solve the master and write the solution to its header."),
                       tr("When to enable:\n%1")
                           .arg(tooltipBullets({tr("You want every master to carry sky coordinates for downstream "
                                                   "tools (annotation, mosaics, astrometry)."),
                                                tr("Your registration pipeline does not always propagate WCS.")})),
                       tr("Good to know: requires a plate-solving engine to be configured in Settings.")});
    m_ui.solveFinalMasterIfNeeded->setToolTip(solveTip);

    // --- Weighting ---
    const QString weightingTip = formatTooltip(
        tr("Frame weighting"),
        {tr("What it does:\nAssigns each frame a weight during stacking so that higher-quality frames contribute more "
            "to the final result."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("None: all frames contribute equally."),
                  tr("Number of stars: frames with more detected stars get higher weight."),
                  tr("Weighted FWHM: frames with tighter stars (smaller FWHM) get higher weight. A frame's width "
                     "is first penalized for the stars it lacks against the reference frame, so a hazy frame whose "
                     "few stars measure narrow is not favoured."),
                  tr("Noise: frames with lower background noise get higher weight. This is the mode that responds "
                     "to transparency; it needs the per-frame noise recorded during registration."),
                  tr("Number of images: weight by the number of sub-exposures in each frame \u2014 useful when "
                     "stacking pre-stacked masters."),
                  tr("Photometric: weight by atmospheric transparency (photometric zero point), penalized by "
                     "measurement scatter and astrometric alignment residual. Available whenever the current "
                     "registration results contain usable photometric zero-point data.")})),
         tr("Good to know: only available when the stack method is Average and HDR Exposure Merge is off.")});
    setSharedToolTip(weightingTip, m_ui.weightingLabel, m_ui.weightingSelector);
    m_ui.weightingLabel->setProperty("anDefaultToolTip", weightingTip);
    m_ui.weightingSelector->setProperty("anDefaultToolTip", weightingTip);

    // --- Weighting Options Button ---
    // (Dynamic tooltip set in updateWeightingControlsState \u2014 keep only defaults here)
    const QString weightOptBtnTip =
        formatTooltip(tr("Weighting options"),
                      {tr("What it does:\nOpens a panel with detailed weighting controls for the selected weighting "
                          "mode \u2014 the star-count scaling, the weight floor and per-pixel weighting.")});
    m_ui.weightingOptionsButton->setToolTip(weightOptBtnTip);

    // --- Star Count Scaling ---
    const QString starScaleTip = formatTooltip(
        tr("Star count scaling"),
        {tr("What it does:\nChooses how detected star counts are converted to stacking weights when \"Number of "
            "stars\" weighting is selected."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Linear: weight proportional to star count \u2014 simple and predictable."),
                  tr("Quadratic: weight proportional to count\u00B2 \u2014 more aggressive separation between good and "
                     "poor frames."),
                  tr("Exponential: smooth exponential falloff controlled by the Alpha parameter."),
                  tr("Roundness Weighted: adjusts weight by star roundness, favoring frames with rounder stars.")}))});
    setSharedToolTip(starScaleTip, m_ui.starCountScalingLabel, m_ui.starCountScalingSelector);

    // --- Exponential Alpha ---
    const QString alphaTip = formatTooltip(
        tr("Exponential alpha"),
        {tr("What it does:\nControls the steepness of the exponential star-count-to-weight curve. Higher values make "
            "the weight drop off faster for frames with fewer stars."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets({tr("Raise it to penalize low-star-count frames more strongly."),
                                  tr("Lower it for a gentler curve that keeps more frames contributing.")})),
         tr("Default: 2.0")});
    setSharedToolTip(alphaTip, m_ui.exponentialAlphaLabel, m_ui.exponentialAlphaValue);

    // --- Min Weight Floor ---
    const QString floorTip = formatTooltip(
        tr("Minimum weight floor"),
        {tr("What it does:\nRaises the weakest frames to this fraction of the best frame's weight, so a poor frame is "
            "down-weighted rather than left out of the stack."),
         tr("Applies to:\nNumber of stars and Weighted FWHM, and the Photometric model's raw weight. Noise and Number "
            "of images weights are ratios that keep every frame, so the floor does not apply to them."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets({tr("Raise it (e.g. 0.1) to ensure every frame contributes at least a little."),
                                  tr("Lower it (e.g. 0.001) to allow nearly complete suppression of poor frames.")})),
         tr("Range: 0.0 \u2013 1.0. Default: 0.01")});
    setSharedToolTip(floorTip, m_ui.minWeightFloorLabel, m_ui.minWeightFloorValue);

    // --- Per-Channel FWHM Weighting ---
    const QString perChTip = formatTooltip(
        tr("Per-channel FWHM weighting"),
        {tr("Unavailable: the registration measures one star width per frame, not one per colour channel, so each "
            "channel can only be weighed by its frame's width."),
         tr("Good to know: a choice an earlier session stored is let go of when these settings are accepted; until "
            "then the run weighs each channel by its frame's width and says so.")});
    m_ui.perChannelFwhmWeighting->setToolTip(perChTip);
    m_ui.perChannelFwhmWeighting->setChecked(false);
    m_ui.perChannelFwhmWeighting->setEnabled(false);

    // --- Local (Per-Pixel) Weighting ---
    const QString localWtTip = formatTooltip(
        tr("Per-pixel weighting"),
        {tr("What it does:\nEstimates a downsampled quality grid for each frame and applies spatially varying weights "
            "during stacking. Regions with higher local SNR contribute more."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets(
                 {tr("Your frames have localized defects (edge artifacts, coma, corner vignetting) not fully corrected "
                     "by flats."),
                  tr("You want the stacker to automatically down-weight bad regions of individual frames.")}))});
    m_ui.localWeightingEnabled->setToolTip(localWtTip);

    // --- Local Weighting Window Size ---
    const QString winSizeTip =
        formatTooltip(tr("Local weighting window size"),
                      {tr("What it does:\nSets the side length (in pixels) of the sliding window used to estimate "
                          "local signal and noise for per-pixel weighting. Must be an odd number."),
                       tr("Adjust it when:\n%1")
                           .arg(tooltipBullets({tr("Increase it for smoother quality maps (less noise in the weight "
                                                   "grid, but reacts more slowly to small defects)."),
                                                tr("Decrease it to detect finer-scale quality variations.")})),
                       tr("Default: 5")});
    setSharedToolTip(winSizeTip, m_ui.localWeightingWindowSizeLabel, m_ui.localWeightingWindowSizeValue);

    // --- Local Weighting Grid Spacing ---
    const QString gridSpaceTip =
        formatTooltip(tr("Local weighting grid spacing"),
                      {tr("What it does:\nSets the distance in pixels between sample points in the local-weight "
                          "quality grid. The grid is then interpolated to full resolution."),
                       tr("Adjust it when:\n%1")
                           .arg(tooltipBullets({tr("Increase it to reduce memory usage and processing time (but lower "
                                                   "spatial resolution of the weight map)."),
                                                tr("Decrease it for finer-grained quality adaptation \u2014 useful if "
                                                   "defects are small and localized.")})),
                       tr("Default: 64")});
    setSharedToolTip(gridSpaceTip, m_ui.localWeightingGridSpacingLabel, m_ui.localWeightingGridSpacingValue);

    // --- Local Weighting SNR Exponent ---
    const QString snrExpTip = formatTooltip(
        tr("SNR exponent"), {tr("What it does:\nSets the power applied to the local SNR proxy before it is combined "
                                "with temporal consistency to produce the final per-pixel weight. Higher values "
                                "increase the contrast between high-SNR and low-SNR regions."),
                             tr("Adjust it when:\n%1")
                                 .arg(tooltipBullets({tr("Raise it to more aggressively favor high-SNR regions."),
                                                      tr("Lower it for a more uniform weighting across the field.")})),
                             tr("Default: 2.0")});
    setSharedToolTip(snrExpTip, m_ui.localWeightingSnrExponentLabel, m_ui.localWeightingSnrExponentValue);

    // --- Drizzle Section ---
    const QString drizzleSectionTip = formatTooltip(
        tr("Drizzle integration"),
        {tr("What it does:\nDrizzle (Variable-Pixel Linear Reconstruction) is a technique that recovers sub-pixel "
            "detail by mapping each input pixel onto a finer output grid, using the known sub-pixel offsets from "
            "dithering."),
         tr("When to enable:\n%1")
             .arg(tooltipBullets({tr("You have 10+ well-dithered frames with sub-pixel offsets."),
                                  tr("Your imaging scale is undersampled (pixel size larger than the seeing disk)."),
                                  tr("You want to increase the effective resolution of the final image.")})),
         tr("Good to know: output images will be larger by the chosen scale factor. Drizzle needs registration data "
            "\u2014 run registration first.")});
    m_ui.drizzleSettingsGroupBox->setToolTip(drizzleSectionTip);

    // --- Drizzle Scale ---
    const QString drizzleScaleTip = formatTooltip(
        tr("Drizzle scale"),
        {tr("What it does:\nSets the output upscaling factor. A scale of 2.0\u00D7 produces an image with twice the "
            "linear resolution (4\u00D7 the pixel count)."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("1.0\u00D7: no upscaling \u2014 drizzle still provides sub-pixel accuracy via kernel resampling."),
                  tr("1.5\u00D7: moderate upscale \u2014 good balance of resolution gain and file size."),
                  tr("2.0\u00D7 (recommended): standard choice for undersampled data."),
                  tr("3.0\u00D7: highest resolution \u2014 requires many well-dithered frames; large output "
                     "files.")}))});
    setSharedToolTip(drizzleScaleTip, m_ui.drizzleScaleLabel, m_ui.drizzleScaleSelector);

    // --- Drizzle Kernel ---
    const QString drizzleKernelTip = formatTooltip(
        tr("Drizzle kernel"),
        {tr("What it does:\nChooses how each input pixel's flux is distributed onto the output grid."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Square (recommended): accurate polygon-overlap calculation \u2014 best flux conservation."),
                  tr("Point: all flux assigned to the nearest output pixel \u2014 fastest, but introduces aliasing."),
                  tr("Turbo: fast axis-aligned approximation of Square."),
                  tr("Gaussian: spreads flux with a smooth 2D Gaussian \u2014 softer output."),
                  tr("Lanczos2: high-quality windowed sinc interpolation."),
                  tr("Lanczos3: highest-quality windowed sinc \u2014 sharpest output but may introduce ringing near "
                     "bright edges.")}))});
    setSharedToolTip(drizzleKernelTip, m_ui.drizzleKernelLabel, m_ui.drizzleKernelSelector);

    // --- Drizzle Pixfrac ---
    const QString drizzlePixfracTip = formatTooltip(
        tr("Pixel fraction (pixfrac)"),
        {tr("What it does:\nShrinks each input pixel before drizzling. A pixfrac of 1.0 uses the full pixel footprint "
            "(shift-and-add); smaller values sharpen the result by reducing correlated noise between neighboring "
            "output pixels."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets(
                 {tr("Start at 0.9 for a conservative improvement with minimal artifacts."),
                  tr("Decrease toward 0.5 if you have many well-dithered frames \u2014 this gives sharper results but "
                     "requires good sub-pixel coverage."),
                  tr("Values below 0.5 are rarely useful and can produce a checkerboard pattern with sparse data.")})),
         tr("Range: 0.1 \u2013 1.0. Default: 0.9")});
    setSharedToolTip(drizzlePixfracTip, m_ui.drizzlePixfracLabel, m_ui.drizzlePixfracValue);

    // --- Tool Buttons ---
    m_ui.recommendedButton->setToolTip(formatTooltip(
        tr("Recommended settings"),
        {tr("What it does:\nAnalyzes your frame count, frame type, and dithering data, then suggests optimal stacking, "
            "rejection, and normalization settings based on astrophotography best practices.")}));

    m_ui.resetButton->setToolTip(tr("Reset all stacking settings to their default values."));

    m_ui.applyToAllButton->setToolTip(formatTooltip(
        tr("Apply to all groups"), {tr("What it does:\nCopies these stacking settings to every group of the same frame "
                                       "type, overwriting any per-group customizations.")}));
}

void StackingSettingsDialog::on_stackMethodSelector_currentTextChanged(const QString& text)
{
    updateRejectionControlsState();
    updateWeightingControlsState();
    updateHdrControlsState();
}

void StackingSettingsDialog::on_rejectionMethodSelector_currentTextChanged(const QString& text)
{
    updateRejectionControlsState();
    updateRejectionLabels();
}

void StackingSettingsDialog::on_weightingSelector_currentTextChanged(const QString& text)
{
    updateWeightingControlsState();
}

void StackingSettingsDialog::on_starCountScaling_currentTextChanged(const QString& text)
{
    updateWeightingControlsState();
}

void StackingSettingsDialog::on_hdrExposureMergeEnabled_toggled(bool checked)
{
    Q_UNUSED(checked);
    updateHdrControlsState();
}

void StackingSettingsDialog::updateRejectionControlsState()
{
    QString stackMethod = m_ui.stackMethodSelector->currentText();
    QString rejectionMethod = m_ui.rejectionMethodSelector->currentText();
    const bool hdrEnabled = (m_frameType == FrameType::LIGHT) && m_ui.hdrExposureMergeEnabled->isChecked();
    const bool drizzleEnabled = (m_frameType == FrameType::LIGHT) && m_ui.chkEnableDrizzle->isChecked();

    // Disable entire rejection group if stack method is not Average
    bool isAverage = (stackMethod == "Average");
    m_ui.rejectionSettingsGroupBox->setEnabled(isAverage);

    // Disable weighting controls if stack method is not Average (only for LIGHT frames)
    /*if (m_frameType == FrameType::LIGHT)
    {
        m_ui.weightingLabel->setEnabled(isAverage);
        m_ui.weightingSelector->setEnabled(isAverage);
    }*/

    // If enabled, check rejection method
    if (isAverage)
    {
        bool isNone = (rejectionMethod == "None");
        m_ui.sigmaLowLabel->setEnabled(!isNone);
        m_ui.sigmaLowValue->setEnabled(!isNone);
        m_ui.sigmaHighLabel->setEnabled(!isNone);
        m_ui.sigmaHighValue->setEnabled(!isNone);
        m_ui.createRejectionMaps->setEnabled(!isNone && !hdrEnabled);
        m_ui.mergeRejectionMaps->setEnabled(!isNone && !hdrEnabled && m_ui.createRejectionMaps->isChecked());

        const bool allowSpatialAnalysis = !isNone && !hdrEnabled;
        if (m_ui.restackExcludingFlaggedFrames->isChecked() && !m_ui.spatialRejectionClustering->isChecked())
        {
            m_ui.spatialRejectionClustering->setChecked(true);
        }
        m_ui.spatialRejectionClustering->setEnabled(allowSpatialAnalysis);
        m_ui.restackExcludingFlaggedFrames->setEnabled(allowSpatialAnalysis && !drizzleEnabled);
        if (!allowSpatialAnalysis)
        {
            m_ui.spatialRejectionClustering->setChecked(false);
        }
        if (!allowSpatialAnalysis || drizzleEnabled || !m_ui.spatialRejectionClustering->isChecked())
        {
            m_ui.restackExcludingFlaggedFrames->setChecked(false);
        }
    }
}

void StackingSettingsDialog::updateRejectionLabels()
{
    QString rejectionMethod = m_ui.rejectionMethodSelector->currentText();

    if (rejectionMethod == "Linear Fit")
    {
        m_ui.sigmaLowLabel->setText(tr("Linear Low:"));
        m_ui.sigmaHighLabel->setText(tr("Linear High:"));
        m_ui.sigmaLowValue->setValue(5.0);
        m_ui.sigmaHighValue->setValue(4.0);
    }
    else if (rejectionMethod == "GESDT")
    {
        m_ui.sigmaLowLabel->setText(tr("ESD Outliers:"));
        m_ui.sigmaHighLabel->setText(tr("ESD Significance:"));
        m_ui.sigmaLowValue->setValue(0.30);
        m_ui.sigmaHighValue->setValue(0.05);
    }
    else if (rejectionMethod == "Percentile")
    {
        // Fractions of the median: a sample further below it than the low fraction, or above it than the high, goes.
        m_ui.sigmaLowLabel->setText(tr("Low Fraction of Median:"));
        m_ui.sigmaHighLabel->setText(tr("High Fraction of Median:"));
        m_ui.sigmaLowValue->setValue(an::kDefaultLowMedianClipFraction);
        m_ui.sigmaHighValue->setValue(an::kDefaultHighMedianClipFraction);
    }
    else
    {
        m_ui.sigmaLowLabel->setText(tr("Sigma Low:"));
        m_ui.sigmaHighLabel->setText(tr("Sigma High:"));
        m_ui.sigmaLowValue->setValue(3.0);
        m_ui.sigmaHighValue->setValue(3.0);
    }
}

void StackingSettingsDialog::updateWeightingControlsState()
{
    // Only relevant for LIGHT frames
    if (m_frameType != FrameType::LIGHT)
    {
        return;
    }

    QString weighting = m_ui.weightingSelector->currentText();
    const bool hdrEnabled = m_ui.hdrExposureMergeEnabled->isChecked();
    const bool cometStackingEnabled = ImageIntegration::usesCometStackingMode(m_cometStackingMode);
    if (cometStackingEnabled)
    {
        if (weighting != QStringLiteral("None"))
        {
            const QSignalBlocker blocker(m_ui.weightingSelector);
            m_ui.weightingSelector->setCurrentText(QStringLiteral("None"));
            weighting = m_ui.weightingSelector->currentText();
        }
        if (m_ui.perChannelFwhmWeighting->isChecked())
        {
            const QSignalBlocker blocker(m_ui.perChannelFwhmWeighting);
            m_ui.perChannelFwhmWeighting->setChecked(false);
        }
        if (m_ui.localWeightingEnabled->isChecked())
        {
            const QSignalBlocker blocker(m_ui.localWeightingEnabled);
            m_ui.localWeightingEnabled->setChecked(false);
        }
    }
    const bool stackMethodAllowsWeighting =
        (m_ui.stackMethodSelector->currentText() == "Average") && !hdrEnabled && !cometStackingEnabled;
    const bool weightingEnabled = stackMethodAllowsWeighting && (weighting != "None");
    const bool localWeightingEnabled = weightingEnabled && m_ui.localWeightingEnabled->isChecked();

    m_ui.weightingLabel->setEnabled(stackMethodAllowsWeighting);
    m_ui.weightingSelector->setEnabled(stackMethodAllowsWeighting);

    if (cometStackingEnabled)
    {
        const QString cometWeightingTooltip =
            tr("Weighting is disabled for comet stacking so every included frame contributes equally.");
        m_ui.weightingLabel->setToolTip(cometWeightingTooltip);
        m_ui.weightingSelector->setToolTip(cometWeightingTooltip);
    }
    else
    {
        const QString weightingTip = m_ui.weightingSelector->property("anDefaultToolTip").toString();
        m_ui.weightingLabel->setToolTip(weightingTip);
        m_ui.weightingSelector->setToolTip(weightingTip);
    }

    QString weightingOptionsTooltip = tr("Open the detailed weighting controls for the selected weighting mode.");
    if (cometStackingEnabled)
    {
        weightingOptionsTooltip =
            tr("Weighting is disabled for comet stacking so every included frame contributes equally.");
    }
    else if (hdrEnabled)
    {
        weightingOptionsTooltip = tr("Weighting options are unavailable while HDR Exposure Merge is enabled.");
    }
    else if (!stackMethodAllowsWeighting)
    {
        weightingOptionsTooltip = tr("Weighting options are only available when Stack Method is Average.");
    }
    else if (!weightingEnabled)
    {
        weightingOptionsTooltip = tr("Select a weighting mode other than None to configure weighting options.");
    }
    m_ui.weightingOptionsButton->setEnabled(weightingEnabled);
    m_ui.weightingOptionsButton->setToolTip(weightingOptionsTooltip);

    // Enable/disable the entire weighting options group
    m_ui.weightingOptionsGroupBox->setEnabled(weightingEnabled);

    // Star count scaling is only relevant for "Number of stars" weighting
    bool isStarCount = weightingEnabled && (weighting == "Number of stars");
    m_ui.starCountScalingLabel->setEnabled(isStarCount);
    m_ui.starCountScalingSelector->setEnabled(isStarCount);

    // Exponential alpha is only enabled when star count scaling is "Exponential"
    QString scaling = m_ui.starCountScalingSelector->currentText();
    bool isExponential = isStarCount && (scaling == "Exponential");
    m_ui.exponentialAlphaLabel->setEnabled(isExponential);
    m_ui.exponentialAlphaValue->setEnabled(isExponential);

    // The floor is offered where the stack raises a weight to it; the stored value is kept under the others.
    const bool floorApplies = weightingEnabled && weightingTakesFloor(weighting);
    m_ui.minWeightFloorLabel->setEnabled(floorApplies);
    m_ui.minWeightFloorValue->setEnabled(floorApplies);

    // The registration measures one star width per frame, so no channel can be weighed by a width of its own.
    m_ui.perChannelFwhmWeighting->setEnabled(false);
    m_ui.localWeightingEnabled->setEnabled(weightingEnabled);
    m_ui.localWeightingWindowSizeLabel->setEnabled(localWeightingEnabled);
    m_ui.localWeightingWindowSizeValue->setEnabled(localWeightingEnabled);
    m_ui.localWeightingGridSpacingLabel->setEnabled(localWeightingEnabled);
    m_ui.localWeightingGridSpacingValue->setEnabled(localWeightingEnabled);
    m_ui.localWeightingSnrExponentLabel->setEnabled(localWeightingEnabled);
    m_ui.localWeightingSnrExponentValue->setEnabled(localWeightingEnabled);
}

void StackingSettingsDialog::updateHdrControlsState()
{
    if (m_frameType != FrameType::LIGHT)
    {
        return;
    }

    const bool hdrEnabled = m_ui.hdrExposureMergeEnabled->isChecked();
    if (hdrEnabled)
    {
        m_ui.createRejectionMaps->setChecked(false);
        m_ui.mergeRejectionMaps->setChecked(false);
        m_ui.spatialRejectionClustering->setChecked(false);
        m_ui.restackExcludingFlaggedFrames->setChecked(false);
        m_ui.createQualityMaps->setChecked(false);
        m_ui.chkEnableDrizzle->setChecked(false);
        m_ui.localWeightingEnabled->setChecked(false);
        m_ui.localNormalizationEnabled->setChecked(false);
        m_ui.normalizationMode->setCurrentText(QStringLiteral("None"));
    }

    m_ui.drizzleSettingsGroupBox->setEnabled(!hdrEnabled);
    m_ui.createQualityMaps->setEnabled(!hdrEnabled);
    m_ui.label_normalization->setEnabled(!hdrEnabled);
    m_ui.normalizationMode->setEnabled(!hdrEnabled);
    m_ui.localNormalizationEnabled->setEnabled(!hdrEnabled);
    m_ui.localNormalizationConfigureButton->setEnabled(!hdrEnabled && m_ui.localNormalizationEnabled->isChecked());
    m_ui.normalizationReferenceMode->setEnabled(!hdrEnabled);
    m_ui.normalizationReferenceFrame->setEnabled(!hdrEnabled);
    m_ui.normalizationReferenceBestButton->setEnabled(!hdrEnabled);
    m_ui.label_normReferenceMode->setEnabled(!hdrEnabled);
    m_ui.label_normReferenceFrame->setEnabled(!hdrEnabled);

    updateRejectionControlsState();
    updateWeightingControlsState();
    updateLocalNormalizationControlsState();
    updateNormalizationReferenceControlsState();
    updateDrizzleInfoLabel();
}

void StackingSettingsDialog::updateLocalNormalizationControlsState()
{
    if (m_frameType != FrameType::LIGHT)
    {
        // A bias or dark master is combined without normalization, so its choice is None and no other is offered; a
        // flat's is its own.
        const bool fixedAtNone = m_frameType == FrameType::BIAS || m_frameType == FrameType::DARK;
        if (fixedAtNone)
        {
            m_ui.normalizationMode->setCurrentText(QStringLiteral("None"));
        }
        m_ui.normalizationMode->setEnabled(!fixedAtNone);
        m_ui.label_normalization->setEnabled(!fixedAtNone);
        updateNormalizationOptionsState();
        return;
    }

    const bool hdrEnabled = m_ui.hdrExposureMergeEnabled->isChecked();
    const bool lnEnabled = m_ui.localNormalizationEnabled->isChecked();
    const bool localExplicitReference =
        lnEnabled && ImageIntegration::isExplicitLocalNormReferenceStrategy(m_localNormSettings.referenceStrategy);
    if (localExplicitReference &&
        m_ui.normalizationReferenceMode->currentText().compare(QStringLiteral("Manual"), Qt::CaseInsensitive) != 0)
    {
        m_ui.normalizationReferenceMode->setCurrentText(QStringLiteral("Manual"));
    }

    showGlobalMethodSetByLocalNormalization(lnEnabled);
    m_ui.normalizationMode->setEnabled(!hdrEnabled && !lnEnabled);
    m_ui.label_normalization->setEnabled(!hdrEnabled && !lnEnabled);
    m_ui.localNormalizationConfigureButton->setEnabled(!hdrEnabled && lnEnabled);
    m_ui.localNormalizationPresetLabel->setEnabled(!hdrEnabled && lnEnabled);

    // Update preset label text
    static const QStringList presetNames = {tr("Quick"), tr("Standard"), tr("Fine"), tr("Custom")};
    m_localNormSettings.preset =
        static_cast<int>(LocalNormalizationSettingsDialog::presetForSettings(currentLocalNormalizationSettings()));
    if (m_localNormSettings.preset >= 0 && m_localNormSettings.preset < presetNames.size())
    {
        // A drizzled group's surfaces are fitted by its stack, not measured by the normalization stage.
        const QString& preset = presetNames[m_localNormSettings.preset];
        m_ui.localNormalizationPresetLabel->setText(m_ui.chkEnableDrizzle->isChecked()
                                                        ? tr("(%1 preset, fitted by the drizzled stack)").arg(preset)
                                                        : QStringLiteral("(%1 preset)").arg(preset));
    }

    updateNormalizationReferenceControlsState();
    updateNormalizationOptionsState();
}

void StackingSettingsDialog::updateNormalizationOptionsState()
{
    const bool light = m_frameType == FrameType::LIGHT;
    const bool hdrEnabled = light && m_ui.hdrExposureMergeEnabled->isChecked();
    const bool localEnabled = light && m_ui.localNormalizationEnabled->isChecked();
    const std::optional<GlobalNormalizationMethod> method =
        globalNormalizationMethodNamed(m_ui.normalizationMode->currentText());

    // Local normalization's global match is Additive With Scaling, which measures a scale as well.
    const bool scales =
        !hdrEnabled &&
        (localEnabled || (method.has_value() && ImageIntegration::normalizationUsesScaleEstimator(*method)));
    m_ui.label_scaleEstimator->setEnabled(scales);
    m_ui.scaleEstimatorSelector->setEnabled(scales);

    // Overlap normalization is a global normalization of the lights; its floors are read only when it runs.
    const bool overlapOffered = light && !hdrEnabled && !localEnabled && method.has_value();
    const bool floorsOffered = overlapOffered && m_ui.overlapNormalizationEnabled->isChecked();
    m_ui.overlapNormalizationEnabled->setEnabled(overlapOffered);
    m_ui.overlapMinimumPixelsLabel->setEnabled(floorsOffered);
    m_ui.overlapMinimumPixelsValue->setEnabled(floorsOffered);
    m_ui.overlapMinimumFractionLabel->setEnabled(floorsOffered);
    m_ui.overlapMinimumFractionValue->setEnabled(floorsOffered);
}

bool StackingSettingsDialog::globalMethodSetByLocalNormalizationShown() const
{
    return m_ui.normalizationMode->currentData().toString() == QLatin1String(kSetByLocalNormalizationKey);
}

void StackingSettingsDialog::showGlobalMethodSetByLocalNormalization(bool shown)
{
    QComboBox* const choice = m_ui.normalizationMode;
    const int index = choice->findData(QString::fromLatin1(kSetByLocalNormalizationKey));
    if (shown == (index >= 0))
    {
        return;
    }
    if (shown)
    {
        m_globalNormalizationChoice = choice->currentText();
        choice->addItem(tr("Additive With Scaling (set by local normalization)"),
                        QString::fromLatin1(kSetByLocalNormalizationKey));
        choice->setCurrentIndex(choice->count() - 1);
        return;
    }
    choice->removeItem(index);
    choice->setCurrentText(m_globalNormalizationChoice);
}

void StackingSettingsDialog::updateNormalizationReferenceControlsState()
{
    if (m_frameType != FrameType::LIGHT)
    {
        m_ui.normalizationReferenceMode->setEnabled(false);
        m_ui.label_normReferenceMode->setEnabled(false);
        m_ui.normalizationReferenceFrame->setEnabled(false);
        m_ui.normalizationReferenceBestButton->setEnabled(false);
        m_ui.label_normReferenceFrame->setEnabled(false);
        return;
    }

    const bool hdrEnabled = m_ui.hdrExposureMergeEnabled->isChecked();
    const bool lnEnabled = m_ui.localNormalizationEnabled->isChecked();
    const bool localExplicitReference =
        lnEnabled && ImageIntegration::isExplicitLocalNormReferenceStrategy(m_localNormSettings.referenceStrategy);
    const bool normalizationNone =
        m_ui.normalizationMode->currentText().compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0;
    const bool globalReferenceEnabled = !lnEnabled && !normalizationNone;
    const bool referenceEnabled = !hdrEnabled && (globalReferenceEnabled || localExplicitReference);

    m_ui.normalizationReferenceMode->setEnabled(referenceEnabled);
    m_ui.label_normReferenceMode->setEnabled(referenceEnabled);

    const QString mode = m_ui.normalizationReferenceMode->currentText();
    const bool manual = referenceEnabled && mode.compare(QStringLiteral("Manual"), Qt::CaseInsensitive) == 0;
    m_ui.normalizationReferenceFrame->setEnabled(manual);
    m_ui.normalizationReferenceBestButton->setEnabled(manual);
    m_ui.label_normReferenceFrame->setEnabled(manual);
}

LocalNormalizationSettings StackingSettingsDialog::currentLocalNormalizationSettings() const
{
    LocalNormalizationSettings settings;
    settings.enabled = getLocalNormalizationEnabled();
    settings.referenceStrategy = m_localNormSettings.referenceStrategy;
    settings.maxIntegratedFrames = m_localNormSettings.maxIntegratedFrames;
    settings.evaluationCriteria = m_localNormSettings.evaluationCriteria;
    settings.gridSize = m_localNormSettings.gridSize;
    settings.gridInterpolation = m_localNormSettings.gridInterpolation;
    settings.backgroundEstimator = m_localNormSettings.backgroundEstimator;
    settings.backgroundPercentile = m_localNormSettings.backgroundPercentile;
    settings.sigmaClipK = m_localNormSettings.sigmaClipK;
    settings.sigmaClipIterations = m_localNormSettings.sigmaClipIterations;
    settings.backgroundSmoothingRadius = m_localNormSettings.backgroundSmoothingRadius;
    settings.scaleEvaluation = m_localNormSettings.scaleEvaluation;
    settings.scaleMode = m_localNormSettings.scaleMode;
    settings.psfType = m_localNormSettings.psfType;
    settings.moffatBeta = m_localNormSettings.moffatBeta;
    settings.maxStars = m_localNormSettings.maxStars;
    settings.minDetectionSnr = m_localNormSettings.minDetectionSnr;
    settings.allowClusteredSources = m_localNormSettings.allowClusteredSources;
    settings.lowClip = m_localNormSettings.lowClip;
    settings.highClip = m_localNormSettings.highClip;
    settings.hotPixelMedianRadius = m_localNormSettings.hotPixelMedianRadius;
    settings.noiseReductionRadius = m_localNormSettings.noiseReductionRadius;
    settings.localScaleEnabled = m_localNormSettings.localScaleEnabled;
    settings.localScaleSmoothingRadius = m_localNormSettings.localScaleSmoothingRadius;
    return settings;
}

void StackingSettingsDialog::applyLocalNormalizationSettings(const LocalNormalizationSettings& settings)
{
    m_localNormSettings.referenceStrategy = settings.referenceStrategy;
    m_localNormSettings.maxIntegratedFrames = settings.maxIntegratedFrames;
    m_localNormSettings.evaluationCriteria = settings.evaluationCriteria;
    m_localNormSettings.gridSize = settings.gridSize;
    m_localNormSettings.gridInterpolation = settings.gridInterpolation;
    m_localNormSettings.backgroundEstimator = settings.backgroundEstimator;
    m_localNormSettings.backgroundPercentile = settings.backgroundPercentile;
    m_localNormSettings.sigmaClipK = settings.sigmaClipK;
    m_localNormSettings.sigmaClipIterations = settings.sigmaClipIterations;
    m_localNormSettings.backgroundSmoothingRadius = settings.backgroundSmoothingRadius;
    m_localNormSettings.scaleEvaluation = settings.scaleEvaluation;
    m_localNormSettings.scaleMode = settings.scaleMode;
    m_localNormSettings.psfType = settings.psfType;
    m_localNormSettings.moffatBeta = settings.moffatBeta;
    m_localNormSettings.maxStars = settings.maxStars;
    m_localNormSettings.minDetectionSnr = settings.minDetectionSnr;
    m_localNormSettings.allowClusteredSources = settings.allowClusteredSources;
    m_localNormSettings.lowClip = settings.lowClip;
    m_localNormSettings.highClip = settings.highClip;
    m_localNormSettings.hotPixelMedianRadius = settings.hotPixelMedianRadius;
    m_localNormSettings.noiseReductionRadius = settings.noiseReductionRadius;
    m_localNormSettings.localScaleEnabled = settings.localScaleEnabled;
    m_localNormSettings.localScaleSmoothingRadius = settings.localScaleSmoothingRadius;
    m_localNormSettings.preset = static_cast<int>(LocalNormalizationSettingsDialog::presetForSettings(settings));
}

void StackingSettingsDialog::openLocalNormalizationDialog()
{
    auto* dialog = new LocalNormalizationSettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    // A drizzled group has no normalization stage: its stack fits the surfaces on the drizzled lights itself and reads
    // none of the stage's analysis.
    dialog->setReferenceAnalysisOffered(
        !getDrizzleEnabled(),
        tr("With drizzle on, the drizzled stack fits each light's local surfaces and scale against the group's "
           "reference as it reads the light: the reference strategy, the evaluation, the PSF flux scale and the "
           "hot-pixel and noise filters do not apply, so they are not offered."));

    // Populate dialog with current settings
    dialog->setSettings(currentLocalNormalizationSettings());

    const QPointer<StackingSettingsDialog> guard(this);
    connect(dialog, &QDialog::accepted, this, [guard, dialog]() {
        if (!guard)
        {
            return;
        }
        // Store settings from dialog
        guard->applyLocalNormalizationSettings(dialog->getSettings());

        guard->updateLocalNormalizationControlsState();
    });
    dialog->open();
}

void StackingSettingsDialog::ensureWeightingOptionsDialog()
{
    if (m_weightingOptionsDialog || !m_ui.weightingOptionsGroupBox)
    {
        return;
    }

    const auto& t = an::ui::tokens();

    m_weightingOptionsDialog = new QDialog(this);
    m_weightingOptionsDialog->setObjectName(QStringLiteral("stackingWeightingOptionsDialog"));
    m_weightingOptionsDialog->setWindowTitle(tr("Weighting Options"));
    m_weightingOptionsDialog->setWindowModality(Qt::WindowModal);
    m_weightingOptionsDialog->setModal(true);
    m_weightingOptionsDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto* layout = new QVBoxLayout(m_weightingOptionsDialog);
    layout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
    layout->setSpacing(t.spacing.lg);

    m_ui.weightingOptionsGroupBox->setParent(m_weightingOptionsDialog);
    m_ui.weightingOptionsGroupBox->setVisible(true);
    layout->addWidget(m_ui.weightingOptionsGroupBox);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, m_weightingOptionsDialog);
    buttonBox->setObjectName(QStringLiteral("weightingOptionsButtonBox"));
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, m_weightingOptionsDialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, m_weightingOptionsDialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    an::ui::processes::fixed_dialog_ui::applyStandardControlHeights(m_weightingOptionsDialog);

    const int initialWidth = (std::max)(width(), (std::max)(m_weightingOptionsDialog->sizeHint().width(),
                                                            m_weightingOptionsDialog->minimumSizeHint().width()));
    const int initialHeight =
        (std::max)(m_weightingOptionsDialog->sizeHint().height(), m_weightingOptionsDialog->minimumSizeHint().height());
    m_weightingOptionsDialog->resize(initialWidth, initialHeight);

    QObject::connect(m_weightingOptionsDialog, &QDialog::finished, this, [this](int result) {
        if (result != QDialog::Accepted && m_weightingOptionsSnapshot.has_value())
        {
            const WeightingOptionsSnapshot& snapshot = *m_weightingOptionsSnapshot;
            m_ui.starCountScalingSelector->setCurrentText(snapshot.starCountScaling);
            m_ui.exponentialAlphaValue->setValue(snapshot.exponentialAlpha);
            m_ui.minWeightFloorValue->setValue(snapshot.minWeightFloor);
            m_ui.perChannelFwhmWeighting->setChecked(snapshot.perChannelFwhmWeighting);
            m_ui.localWeightingEnabled->setChecked(snapshot.localWeightingEnabled);
            m_ui.localWeightingWindowSizeValue->setValue(snapshot.localWeightingWindowSize);
            m_ui.localWeightingGridSpacingValue->setValue(snapshot.localWeightingGridSpacing);
            m_ui.localWeightingSnrExponentValue->setValue(snapshot.localWeightingSnrWeightExponent);
        }

        m_weightingOptionsSnapshot.reset();
        updateWeightingControlsState();
    });

    an::ui::processes::fixed_dialog_ui::connectSectionResizeBehavior(
        m_ui.weightingOptionsGroupBox->toggleButton(), m_weightingOptionsDialog,
        m_ui.weightingOptionsGroupBox->animationDuration());

    an::ui::processes::fixed_dialog_ui::scheduleDialogResize(m_weightingOptionsDialog);
}

void StackingSettingsDialog::openWeightingOptionsDialog()
{
    if (!isWeightingOptionsAvailable())
    {
        return;
    }

    ensureWeightingOptionsDialog();
    if (!m_weightingOptionsDialog)
    {
        return;
    }

    if (m_weightingOptionsDialog->isVisible())
    {
        m_weightingOptionsDialog->raise();
        m_weightingOptionsDialog->activateWindow();
        return;
    }

    m_weightingOptionsSnapshot = WeightingOptionsSnapshot{
        .starCountScaling = m_ui.starCountScalingSelector->currentText(),
        .exponentialAlpha = m_ui.exponentialAlphaValue->value(),
        .minWeightFloor = m_ui.minWeightFloorValue->value(),
        .perChannelFwhmWeighting = m_ui.perChannelFwhmWeighting->isChecked(),
        .localWeightingEnabled = m_ui.localWeightingEnabled->isChecked(),
        .localWeightingWindowSize = m_ui.localWeightingWindowSizeValue->value(),
        .localWeightingGridSpacing = m_ui.localWeightingGridSpacingValue->value(),
        .localWeightingSnrWeightExponent = m_ui.localWeightingSnrExponentValue->value(),
    };

    an::ui::processes::fixed_dialog_ui::scheduleDialogResize(m_weightingOptionsDialog);
    m_weightingOptionsDialog->open();
}

bool StackingSettingsDialog::isWeightingOptionsAvailable() const
{
    if (m_frameType != FrameType::LIGHT)
    {
        return false;
    }

    if (m_ui.stackMethodSelector->currentText() != QStringLiteral("Average"))
    {
        return false;
    }

    if (ImageIntegration::usesCometStackingMode(m_cometStackingMode))
    {
        return false;
    }

    if (m_ui.hdrExposureMergeEnabled->isChecked())
    {
        return false;
    }

    return m_ui.weightingSelector->currentText() != QStringLiteral("None");
}

// Getters
QString StackingSettingsDialog::getStackMethod() const
{
    return m_ui.stackMethodSelector->currentText();
}

QString StackingSettingsDialog::getNormalizationMode() const
{
    // While local normalization sets the global match, the method the group stores is the one kept for it.
    return globalMethodSetByLocalNormalizationShown() ? m_globalNormalizationChoice
                                                      : m_ui.normalizationMode->currentText();
}

ScaleEstimatorMethod StackingSettingsDialog::getScaleEstimator() const
{
    return m_ui.scaleEstimatorSelector->currentText() == QStringLiteral("MAD") ? ScaleEstimatorMethod::MAD
                                                                               : ScaleEstimatorMethod::IKSS;
}

bool StackingSettingsDialog::getOverlapNormalizationEnabled() const
{
    return m_ui.overlapNormalizationEnabled->isChecked();
}

int StackingSettingsDialog::getOverlapMinimumPixels() const
{
    return m_ui.overlapMinimumPixelsValue->value();
}

double StackingSettingsDialog::getOverlapMinimumFraction() const
{
    return storedValueWhileShown(*m_ui.overlapMinimumFractionValue);
}

QString StackingSettingsDialog::getNormalizationReferenceMode() const
{
    return m_ui.normalizationReferenceMode->currentText();
}

QString StackingSettingsDialog::getManualNormalizationReference() const
{
    return m_ui.normalizationReferenceFrame->currentData().toString();
}

QString StackingSettingsDialog::getRejectionMethod() const
{
    return m_ui.rejectionMethodSelector->currentText();
}

bool StackingSettingsDialog::getCreateRejectionMaps() const
{
    return m_ui.createRejectionMaps->isChecked();
}

bool StackingSettingsDialog::getMergeRejectionMaps() const
{
    return m_ui.mergeRejectionMaps->isChecked();
}

bool StackingSettingsDialog::getSpatialRejectionClustering() const
{
    return m_ui.spatialRejectionClustering->isChecked();
}

bool StackingSettingsDialog::getRestackExcludingFlaggedFrames() const
{
    return m_ui.restackExcludingFlaggedFrames->isChecked();
}

bool StackingSettingsDialog::getCreateQualityMaps() const
{
    return m_ui.createQualityMaps->isChecked();
}

RegisteredOutputPolicy StackingSettingsDialog::getRegisteredOutputPolicy() const
{
    return m_ui.discardRegisteredOutputsAfterStacking->isChecked() ? RegisteredOutputPolicy::Discardable
                                                                   : RegisteredOutputPolicy::Retained;
}

double StackingSettingsDialog::getSigmaLow() const
{
    return storedValueWhileShown(*m_ui.sigmaLowValue);
}

double StackingSettingsDialog::getSigmaHigh() const
{
    return storedValueWhileShown(*m_ui.sigmaHighValue);
}

QString StackingSettingsDialog::getWeighting() const
{
    if (ImageIntegration::usesCometStackingMode(m_cometStackingMode))
    {
        return QStringLiteral("None");
    }

    return m_ui.weightingSelector->currentText();
}

QString StackingSettingsDialog::getStarCountScaling() const
{
    return m_ui.starCountScalingSelector->currentText();
}

double StackingSettingsDialog::getExponentialAlpha() const
{
    return storedValueWhileShown(*m_ui.exponentialAlphaValue);
}

double StackingSettingsDialog::getMinWeightFloor() const
{
    return storedValueWhileShown(*m_ui.minWeightFloorValue);
}

bool StackingSettingsDialog::getPerChannelFwhmWeighting() const
{
    return isWeightingOptionsAvailable() &&
           (m_ui.weightingSelector->currentText() == QStringLiteral("Weighted FWHM")) &&
           m_ui.perChannelFwhmWeighting->isChecked();
}

bool StackingSettingsDialog::getLocalWeightingEnabled() const
{
    return isWeightingOptionsAvailable() && m_ui.localWeightingEnabled->isChecked();
}

int StackingSettingsDialog::getLocalWeightingWindowSize() const
{
    return m_ui.localWeightingWindowSizeValue->value();
}

int StackingSettingsDialog::getLocalWeightingGridSpacing() const
{
    return m_ui.localWeightingGridSpacingValue->value();
}

double StackingSettingsDialog::getLocalWeightingSnrWeightExponent() const
{
    return storedValueWhileShown(*m_ui.localWeightingSnrExponentValue);
}

// Drizzle getters
bool StackingSettingsDialog::getDrizzleEnabled() const
{
    return m_ui.chkEnableDrizzle->isChecked();
}

bool StackingSettingsDialog::getHdrExposureMergeEnabled() const
{
    return m_ui.hdrExposureMergeEnabled->isChecked();
}

bool StackingSettingsDialog::getSolveFinalMasterIfNeeded() const
{
    return m_ui.solveFinalMasterIfNeeded->isChecked();
}

double StackingSettingsDialog::getDrizzleScale() const
{
    QString text = m_ui.drizzleScaleSelector->currentText();
    // Extract the numeric value from strings like "2.0× (recommended)"
    if (text.startsWith("1.0"))
        return 1.0;
    else if (text.startsWith("1.5"))
        return 1.5;
    else if (text.startsWith("2.0"))
        return 2.0;
    else if (text.startsWith("3.0"))
        return 3.0;
    return 2.0; // Default
}

QString StackingSettingsDialog::getDrizzleKernel() const
{
    QString text = m_ui.drizzleKernelSelector->currentText();
    // Extract the kernel name from strings like "Square (recommended)"
    if (text.startsWith("Square"))
        return QStringLiteral("Square");
    else if (text.startsWith("Point"))
        return QStringLiteral("Point");
    else if (text.startsWith("Turbo"))
        return QStringLiteral("Turbo");
    else if (text.startsWith("Gaussian"))
        return QStringLiteral("Gaussian");
    else if (text.startsWith("Lanczos2"))
        return QStringLiteral("Lanczos2");
    else if (text.startsWith("Lanczos3"))
        return QStringLiteral("Lanczos3");
    return QStringLiteral("Square"); // Default
}

double StackingSettingsDialog::getDrizzlePixfrac() const
{
    return storedValueWhileShown(*m_ui.drizzlePixfracValue);
}

// Local normalization getters
bool StackingSettingsDialog::getLocalNormalizationEnabled() const
{
    return m_ui.localNormalizationEnabled->isChecked();
}

QString StackingSettingsDialog::getLocalNormalizationReferenceStrategy() const
{
    return m_localNormSettings.referenceStrategy;
}

int StackingSettingsDialog::getLocalNormalizationMaxIntegratedFrames() const
{
    return m_localNormSettings.maxIntegratedFrames;
}

QString StackingSettingsDialog::getLocalNormalizationEvaluationCriteria() const
{
    return m_localNormSettings.evaluationCriteria;
}

int StackingSettingsDialog::getLocalNormalizationGridSize() const
{
    return m_localNormSettings.gridSize;
}

QString StackingSettingsDialog::getLocalNormalizationGridInterpolation() const
{
    return m_localNormSettings.gridInterpolation;
}

QString StackingSettingsDialog::getLocalNormalizationBackgroundEstimator() const
{
    return m_localNormSettings.backgroundEstimator;
}

double StackingSettingsDialog::getLocalNormalizationBackgroundPercentile() const
{
    return m_localNormSettings.backgroundPercentile;
}

double StackingSettingsDialog::getLocalNormalizationSigmaClipK() const
{
    return m_localNormSettings.sigmaClipK;
}

int StackingSettingsDialog::getLocalNormalizationSigmaClipIterations() const
{
    return m_localNormSettings.sigmaClipIterations;
}

int StackingSettingsDialog::getLocalNormalizationBackgroundSmoothingRadius() const
{
    return m_localNormSettings.backgroundSmoothingRadius;
}

QString StackingSettingsDialog::getLocalNormalizationScaleEvaluation() const
{
    return m_localNormSettings.scaleEvaluation;
}

QString StackingSettingsDialog::getLocalNormalizationScaleMode() const
{
    return m_localNormSettings.scaleMode;
}

QString StackingSettingsDialog::getLocalNormalizationPsfType() const
{
    return m_localNormSettings.psfType;
}

double StackingSettingsDialog::getLocalNormalizationMoffatBeta() const
{
    return m_localNormSettings.moffatBeta;
}

int StackingSettingsDialog::getLocalNormalizationMaxStars() const
{
    return m_localNormSettings.maxStars;
}

double StackingSettingsDialog::getLocalNormalizationMinDetectionSnr() const
{
    return m_localNormSettings.minDetectionSnr;
}

bool StackingSettingsDialog::getLocalNormalizationAllowClusteredSources() const
{
    return m_localNormSettings.allowClusteredSources;
}

double StackingSettingsDialog::getLocalNormalizationLowClip() const
{
    return m_localNormSettings.lowClip;
}

double StackingSettingsDialog::getLocalNormalizationHighClip() const
{
    return m_localNormSettings.highClip;
}

int StackingSettingsDialog::getLocalNormalizationHotPixelMedianRadius() const
{
    return m_localNormSettings.hotPixelMedianRadius;
}

int StackingSettingsDialog::getLocalNormalizationNoiseReductionRadius() const
{
    return m_localNormSettings.noiseReductionRadius;
}

bool StackingSettingsDialog::getLocalNormalizationLocalScaleEnabled() const
{
    return m_localNormSettings.localScaleEnabled;
}

int StackingSettingsDialog::getLocalNormalizationLocalScaleSmoothingRadius() const
{
    return m_localNormSettings.localScaleSmoothingRadius;
}

// Setters
void StackingSettingsDialog::setStackMethod(const QString& method)
{
    m_ui.stackMethodSelector->setCurrentText(method);
}

void StackingSettingsDialog::setNormalizationMode(const QString& mode)
{
    if (globalMethodSetByLocalNormalizationShown())
    {
        // Local normalization sets the global match; the method is kept for when it is turned off.
        if (mode != m_ui.normalizationMode->currentText() && m_ui.normalizationMode->findText(mode) >= 0)
        {
            m_globalNormalizationChoice = mode;
        }
        return;
    }
    m_ui.normalizationMode->setCurrentText(mode);
    if (m_frameType != FrameType::LIGHT)
    {
        updateLocalNormalizationControlsState();
    }
}

void StackingSettingsDialog::setScaleEstimator(ScaleEstimatorMethod method)
{
    m_ui.scaleEstimatorSelector->setCurrentText(method == ScaleEstimatorMethod::MAD ? QStringLiteral("MAD")
                                                                                    : QStringLiteral("IKSS"));
}

void StackingSettingsDialog::setOverlapNormalizationEnabled(bool enabled)
{
    m_ui.overlapNormalizationEnabled->setChecked(enabled);
    updateNormalizationOptionsState();
}

void StackingSettingsDialog::setOverlapMinimumPixels(int value)
{
    m_ui.overlapMinimumPixelsValue->setValue(value);
}

void StackingSettingsDialog::setOverlapMinimumFraction(double value)
{
    setStoredValue(*m_ui.overlapMinimumFractionValue, value);
}

void StackingSettingsDialog::setRejectionMethod(const QString& method)
{
    m_ui.rejectionMethodSelector->setCurrentText(method);
}

void StackingSettingsDialog::setCreateRejectionMaps(bool value)
{
    m_ui.createRejectionMaps->setChecked(value);
    updateRejectionControlsState();
}

void StackingSettingsDialog::setMergeRejectionMaps(bool value)
{
    m_ui.mergeRejectionMaps->setChecked(value);
    updateRejectionControlsState();
}

void StackingSettingsDialog::setSpatialRejectionClustering(bool value)
{
    m_ui.spatialRejectionClustering->setChecked(value);
    updateRejectionControlsState();
}

void StackingSettingsDialog::setRestackExcludingFlaggedFrames(bool value)
{
    m_ui.restackExcludingFlaggedFrames->setChecked(value);
    updateRejectionControlsState();
}

void StackingSettingsDialog::setCreateQualityMaps(bool value)
{
    m_ui.createQualityMaps->setChecked(value);
}

void StackingSettingsDialog::setRegisteredOutputPolicy(RegisteredOutputPolicy policy)
{
    m_ui.discardRegisteredOutputsAfterStacking->setChecked(policy == RegisteredOutputPolicy::Discardable);
}

void StackingSettingsDialog::setSigmaLow(double value)
{
    setStoredValue(*m_ui.sigmaLowValue, value);
}

void StackingSettingsDialog::setSigmaHigh(double value)
{
    setStoredValue(*m_ui.sigmaHighValue, value);
}

void StackingSettingsDialog::setWeighting(const QString& weighting)
{
    m_ui.weightingSelector->setCurrentText(weighting);
}

void StackingSettingsDialog::setPhotometricWeightingAvailable(const bool available)
{
    const QString currentWeighting = m_ui.weightingSelector->currentText();
    const bool selectedPhotometric =
        currentWeighting.compare(QString::fromLatin1(kPhotometricWeightingText), Qt::CaseInsensitive) == 0 ||
        currentWeighting.compare(QString::fromLatin1(kPhotometricWeightingDisabledText), Qt::CaseInsensitive) == 0;

    m_photometricWeightingAvailable = available;
    refreshPhotometricWeightingItem();

    if (!available && selectedPhotometric)
    {
        LOG_WARNING(
            "StackingSettingsDialog: Photometric weighting is unavailable for the current group; falling back to None");
        m_ui.weightingSelector->setCurrentText(QStringLiteral("None"));
    }

    updateWeightingControlsState();
}

void StackingSettingsDialog::setStarCountScaling(const QString& scaling)
{
    m_ui.starCountScalingSelector->setCurrentText(scaling);
    updateWeightingControlsState();
}

void StackingSettingsDialog::setExponentialAlpha(double value)
{
    setStoredValue(*m_ui.exponentialAlphaValue, value);
}

void StackingSettingsDialog::setMinWeightFloor(double value)
{
    setStoredValue(*m_ui.minWeightFloorValue, value);
}

void StackingSettingsDialog::setPerChannelFwhmWeighting(bool)
{
    // The registration measures one star width per frame, so the control stays off and a stored choice is let go of.
    m_ui.perChannelFwhmWeighting->setChecked(false);
}

void StackingSettingsDialog::setLocalWeightingEnabled(bool value)
{
    m_ui.localWeightingEnabled->setChecked(value);
    updateWeightingControlsState();
}

void StackingSettingsDialog::setLocalWeightingWindowSize(int value)
{
    m_ui.localWeightingWindowSizeValue->setValue(value);
}

void StackingSettingsDialog::setLocalWeightingGridSpacing(int value)
{
    m_ui.localWeightingGridSpacingValue->setValue(value);
}

void StackingSettingsDialog::setLocalWeightingSnrWeightExponent(double value)
{
    setStoredValue(*m_ui.localWeightingSnrExponentValue, value);
}

// Drizzle setters
void StackingSettingsDialog::setDrizzleEnabled(bool enabled)
{
    m_ui.chkEnableDrizzle->setChecked(enabled);
    updateDrizzleInfoLabel();
}

void StackingSettingsDialog::setHdrExposureMergeEnabled(bool enabled)
{
    m_ui.hdrExposureMergeEnabled->setChecked(enabled);
    updateHdrControlsState();
}

void StackingSettingsDialog::setCometStackingMode(const QString& mode)
{
    const QString trimmed = mode.trimmed();
    const QString requested = trimmed.isEmpty() ? QStringLiteral("Stars") : trimmed;
    m_cometStackingMode = ImageIntegration::normalizeCometStackingMode(requested);
    updateWeightingControlsState();
}

void StackingSettingsDialog::setSolveFinalMasterIfNeeded(bool enabled)
{
    m_ui.solveFinalMasterIfNeeded->setChecked(enabled);
}

void StackingSettingsDialog::setDrizzleScale(double scale)
{
    // Find and select the matching item
    QString scaleStr = QString::number(scale, 'f', 1);
    for (int i = 0; i < m_ui.drizzleScaleSelector->count(); ++i)
    {
        if (m_ui.drizzleScaleSelector->itemText(i).startsWith(scaleStr))
        {
            m_ui.drizzleScaleSelector->setCurrentIndex(i);
            break;
        }
    }
    updateDrizzleInfoLabel();
}

void StackingSettingsDialog::setDrizzleKernel(const QString& kernel)
{
    // A kernel is matched by its meaning: the model holds the dialog's "Lanczos3" or a stored "lanczos3" alike.
    using ImageIntegration::PersistedChoice;
    using ImageIntegration::persistedChoiceFromText;
    const ImageIntegration::PersistedChoiceValue wanted =
        persistedChoiceFromText(PersistedChoice::DrizzleKernel, kernel);
    if (!wanted.known)
    {
        return;
    }
    for (int i = 0; i < m_ui.drizzleKernelSelector->count(); ++i)
    {
        const QString name = m_ui.drizzleKernelSelector->itemText(i).section(QLatin1Char(' '), 0, 0);
        if (persistedChoiceFromText(PersistedChoice::DrizzleKernel, name).key == wanted.key)
        {
            m_ui.drizzleKernelSelector->setCurrentIndex(i);
            break;
        }
    }
}

void StackingSettingsDialog::setDrizzlePixfrac(double value)
{
    setStoredValue(*m_ui.drizzlePixfracValue, value);
}

void StackingSettingsDialog::setNormalizationReferenceMode(const QString& mode)
{
    const int idx = m_ui.normalizationReferenceMode->findText(mode);
    m_ui.normalizationReferenceMode->setCurrentIndex(idx >= 0 ? idx : 0);
    updateNormalizationReferenceControlsState();
}

void StackingSettingsDialog::setManualNormalizationReference(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        if (m_ui.normalizationReferenceFrame->count() > 0)
        {
            m_ui.normalizationReferenceFrame->setCurrentIndex(0);
        }
        return;
    }

    const int idx = m_ui.normalizationReferenceFrame->findData(path);
    if (idx >= 0)
    {
        m_ui.normalizationReferenceFrame->setCurrentIndex(idx);
        return;
    }

    const QString name = QFileInfo(path).fileName().trimmed();
    m_ui.normalizationReferenceFrame->addItem(name.isEmpty() ? path : name, path);
    m_ui.normalizationReferenceFrame->setCurrentIndex(m_ui.normalizationReferenceFrame->count() - 1);
}

void StackingSettingsDialog::setNormalizationReferenceCandidates(const QStringList& candidates)
{
    m_ui.normalizationReferenceFrame->clear();
    for (const auto& path : candidates)
    {
        const QString name = QFileInfo(path).fileName().trimmed();
        m_ui.normalizationReferenceFrame->addItem(name.isEmpty() ? path : name, path);
        m_ui.normalizationReferenceFrame->setItemData(m_ui.normalizationReferenceFrame->count() - 1, path,
                                                      Qt::ToolTipRole);
    }
}

// Local normalization setters
void StackingSettingsDialog::setLocalNormalizationEnabled(bool enabled)
{
    m_ui.localNormalizationEnabled->setChecked(enabled);
    updateLocalNormalizationControlsState();
}

void StackingSettingsDialog::setLocalNormalizationReferenceStrategy(const QString& strategy)
{
    m_localNormSettings.referenceStrategy = strategy;
    if (m_frameType == FrameType::LIGHT && ImageIntegration::isExplicitLocalNormReferenceStrategy(strategy))
    {
        setNormalizationReferenceMode(QStringLiteral("Manual"));
    }
    updateLocalNormalizationControlsState();
}

void StackingSettingsDialog::setLocalNormalizationMaxIntegratedFrames(int value)
{
    m_localNormSettings.maxIntegratedFrames = value;
}

void StackingSettingsDialog::setLocalNormalizationEvaluationCriteria(const QString& criteria)
{
    m_localNormSettings.evaluationCriteria = criteria;
}

void StackingSettingsDialog::setLocalNormalizationGridSize(int value)
{
    m_localNormSettings.gridSize = value;
}

void StackingSettingsDialog::setLocalNormalizationGridInterpolation(const QString& interpolation)
{
    m_localNormSettings.gridInterpolation = interpolation;
}

void StackingSettingsDialog::setLocalNormalizationBackgroundEstimator(const QString& estimator)
{
    m_localNormSettings.backgroundEstimator = estimator;
}

void StackingSettingsDialog::setLocalNormalizationBackgroundPercentile(double value)
{
    m_localNormSettings.backgroundPercentile = value;
}

void StackingSettingsDialog::setLocalNormalizationSigmaClipK(double value)
{
    m_localNormSettings.sigmaClipK = value;
}

void StackingSettingsDialog::setLocalNormalizationSigmaClipIterations(int value)
{
    m_localNormSettings.sigmaClipIterations = value;
}

void StackingSettingsDialog::setLocalNormalizationBackgroundSmoothingRadius(int value)
{
    m_localNormSettings.backgroundSmoothingRadius = value;
}

void StackingSettingsDialog::setLocalNormalizationScaleEvaluation(const QString& method)
{
    m_localNormSettings.scaleEvaluation = method;
    updateLocalNormalizationControlsState();
}

void StackingSettingsDialog::setLocalNormalizationScaleMode(const QString& mode)
{
    m_localNormSettings.scaleMode = mode;
}

void StackingSettingsDialog::setLocalNormalizationPsfType(const QString& psfType)
{
    m_localNormSettings.psfType = psfType;
}

void StackingSettingsDialog::setLocalNormalizationMoffatBeta(double value)
{
    m_localNormSettings.moffatBeta = value;
}

void StackingSettingsDialog::setLocalNormalizationMaxStars(int value)
{
    m_localNormSettings.maxStars = value;
}

void StackingSettingsDialog::setLocalNormalizationMinDetectionSnr(double value)
{
    m_localNormSettings.minDetectionSnr = value;
}

void StackingSettingsDialog::setLocalNormalizationAllowClusteredSources(bool value)
{
    m_localNormSettings.allowClusteredSources = value;
}

void StackingSettingsDialog::setLocalNormalizationLowClip(double value)
{
    m_localNormSettings.lowClip = value;
}

void StackingSettingsDialog::setLocalNormalizationHighClip(double value)
{
    m_localNormSettings.highClip = value;
}

void StackingSettingsDialog::setLocalNormalizationHotPixelMedianRadius(int value)
{
    m_localNormSettings.hotPixelMedianRadius = value;
}

void StackingSettingsDialog::setLocalNormalizationNoiseReductionRadius(int value)
{
    m_localNormSettings.noiseReductionRadius = value;
}

void StackingSettingsDialog::setLocalNormalizationLocalScaleEnabled(bool value)
{
    m_localNormSettings.localScaleEnabled = value;
    updateLocalNormalizationControlsState();
}

void StackingSettingsDialog::setLocalNormalizationLocalScaleSmoothingRadius(int value)
{
    m_localNormSettings.localScaleSmoothingRadius = value;
}

void StackingSettingsDialog::resetToDefaults()
{
    // Common defaults for all frame types
    m_ui.stackMethodSelector->setCurrentText("Average");
    setCometStackingMode(QStringLiteral("Stars"));
    m_ui.sigmaLowValue->setValue(3.0);
    m_ui.sigmaHighValue->setValue(3.0);
    m_ui.weightingSelector->setCurrentText("None");
    m_ui.createRejectionMaps->setChecked(false);
    m_ui.mergeRejectionMaps->setChecked(false);
    m_ui.spatialRejectionClustering->setChecked(false);
    m_ui.restackExcludingFlaggedFrames->setChecked(false);
    m_ui.createQualityMaps->setChecked(false);
    m_ui.discardRegisteredOutputsAfterStacking->setChecked(false);

    // Weighting options defaults (LIGHT frames only)
    m_ui.starCountScalingSelector->setCurrentText("Quadratic");
    m_ui.exponentialAlphaValue->setValue(2.0);
    m_ui.minWeightFloorValue->setValue(0.01);
    m_ui.perChannelFwhmWeighting->setChecked(false);
    m_ui.localWeightingEnabled->setChecked(false);
    m_ui.localWeightingWindowSizeValue->setValue(5);
    m_ui.localWeightingGridSpacingValue->setValue(64);
    m_ui.localWeightingSnrExponentValue->setValue(2.0);

    // Drizzle defaults (LIGHT frames only)
    m_ui.hdrExposureMergeEnabled->setChecked(false);
    m_ui.chkEnableDrizzle->setChecked(false); // Disabled by default
    m_ui.drizzleScaleSelector->setCurrentIndex(2); // 2.0× (recommended)
    m_ui.drizzleKernelSelector->setCurrentIndex(0); // Square (recommended)
    m_ui.drizzlePixfracValue->setValue(0.9);
    m_ui.solveFinalMasterIfNeeded->setChecked(false);

    // Local normalization defaults (LIGHT frames only)
    m_ui.localNormalizationEnabled->setChecked(false);
    m_localNormSettings = LocalNormSettings{}; // Reset to default values
    m_localNormSettings.preset = 1; // Standard

    m_ui.normalizationReferenceMode->setCurrentIndex(0);
    setScaleEstimator(ScaleEstimatorMethod::IKSS);
    m_ui.overlapNormalizationEnabled->setChecked(false);
    m_ui.overlapMinimumPixelsValue->setValue(1000);
    m_ui.overlapMinimumFractionValue->setValue(0.01);

    // Frame-type-specific defaults
    if (m_frameType == FrameType::FLAT)
    {
        m_ui.normalizationMode->setCurrentText("Multiplicative");
        m_ui.rejectionMethodSelector->setCurrentText("Sigma Clipping");
    }
    else if (m_frameType == FrameType::LIGHT)
    {
        m_ui.normalizationMode->setCurrentText("Additive With Scaling");
        m_ui.rejectionMethodSelector->setCurrentText("Winsorized Sigma Clipping");
    }
    else // BIAS or DARK
    {
        m_ui.normalizationMode->setCurrentText("None");
        m_ui.rejectionMethodSelector->setCurrentText("Sigma Clipping");
    }

    updateLocalNormalizationControlsState();
    updateNormalizationReferenceControlsState();
    updateHdrControlsState();
    updateWeightingControlsState();
}

int StackingSettingsDialog::photometricWeightingIndex() const
{
    int index = m_ui.weightingSelector->findText(QString::fromLatin1(kPhotometricWeightingText));
    if (index >= 0)
    {
        return index;
    }

    return m_ui.weightingSelector->findText(QString::fromLatin1(kPhotometricWeightingDisabledText));
}

void StackingSettingsDialog::refreshPhotometricWeightingItem()
{
    const int index = photometricWeightingIndex();
    if (index < 0)
    {
        return;
    }

    const QString itemText = m_photometricWeightingAvailable ? QString::fromLatin1(kPhotometricWeightingText)
                                                             : QString::fromLatin1(kPhotometricWeightingDisabledText);
    m_ui.weightingSelector->setItemText(index, itemText);

    if (auto* model = m_ui.weightingSelector->model())
    {
        const QModelIndex itemIndex = model->index(index, 0);
        Qt::ItemFlags flags = model->flags(itemIndex);
        if (m_photometricWeightingAvailable)
        {
            flags |= Qt::ItemIsEnabled;
            flags |= Qt::ItemIsSelectable;
        }
        else
        {
            flags &= ~Qt::ItemIsEnabled;
            flags &= ~Qt::ItemIsSelectable;
        }
        model->setData(itemIndex, static_cast<int>(flags), Qt::UserRole - 1);
    }
}

void StackingSettingsDialog::on_resetButton_clicked()
{
    resetToDefaults();
}

void StackingSettingsDialog::on_applyToAllButton_clicked()
{
    // Emit signal to notify parent that user wants to apply to all
    applyToAllRequested();
}

void StackingSettingsDialog::updateDrizzleInfoLabel()
{
    if (m_frameType != FrameType::LIGHT)
    {
        return;
    }

    if (m_ui.hdrExposureMergeEnabled->isChecked())
    {
        m_ui.drizzleInfoLabel->setText(tr("HDR exposure merge disables drizzle for this group."));
        return;
    }

    if (!m_ui.chkEnableDrizzle->isChecked())
    {
        m_ui.drizzleInfoLabel->setText(tr("ℹ️ Drizzle is disabled. Enable to increase output resolution."));
        return;
    }

    double scale = getDrizzleScale();

    QString sizeInfo;
    if (scale > 1.0)
    {
        int scaleFactor = static_cast<int>(scale * scale);
        sizeInfo = tr("Output will be %1× larger (%2× pixels).").arg(scale, 0, 'f', 1).arg(scaleFactor);
    }
    else
    {
        sizeInfo = tr("Output will be same size as input.");
    }

    m_ui.drizzleInfoLabel->setText(tr("ℹ️ Drizzle enabled. %1 Works best with 10+ dithered frames.").arg(sizeInfo));
}

void StackingSettingsDialog::setFrameCount(int count)
{
    m_frameCount = count;
    m_ui.recommendedButton->setEnabled(m_frameCount > 0);
}

void StackingSettingsDialog::setDrizzleAnalysis(const DrizzleAnalysis& analysis)
{
    m_drizzleAnalysis = analysis;
}

StackingSettings StackingSettingsDialog::getCurrentSettings() const
{
    // Note: limitMemory and maxMemoryMb are left at their struct defaults
    // since they are not exposed in this dialog's widgets.
    StackingSettings settings;
    settings.stackMethod = getStackMethod();
    settings.rejectionMethod = getRejectionMethod();
    settings.sigmaLow = getSigmaLow();
    settings.sigmaHigh = getSigmaHigh();
    settings.weighting = getWeighting();
    settings.createRejectionMaps = getCreateRejectionMaps();
    settings.mergeRejectionMaps = getMergeRejectionMaps();
    settings.spatialRejectionClustering = getSpatialRejectionClustering();
    settings.restackExcludingFlaggedFrames = getRestackExcludingFlaggedFrames();
    settings.createQualityMaps = getCreateQualityMaps();
    settings.registeredOutputPolicy = getRegisteredOutputPolicy();
    settings.starCountScaling = getStarCountScaling();
    settings.exponentialAlpha = getExponentialAlpha();
    settings.minWeightFloor = getMinWeightFloor();
    settings.perChannelFwhmWeighting = getPerChannelFwhmWeighting();
    settings.localWeighting.enabled = getLocalWeightingEnabled();
    settings.localWeighting.windowSize = getLocalWeightingWindowSize();
    settings.localWeighting.gridSpacing = getLocalWeightingGridSpacing();
    settings.localWeighting.snrWeightExponent = getLocalWeightingSnrWeightExponent();

    settings.drizzle.enabled = getDrizzleEnabled();
    settings.drizzle.scale = getDrizzleScale();
    settings.drizzle.kernel = getDrizzleKernel();
    settings.drizzle.pixelFraction = getDrizzlePixfrac();
    settings.solveFinalMasterIfNeeded = getSolveFinalMasterIfNeeded();
    settings.hdr.enabled = getHdrExposureMergeEnabled();
    settings.cometStackingMode = m_cometStackingMode;

    const QString refMode = getNormalizationReferenceMode();
    settings.normalizationReferenceMode = (refMode.compare(QStringLiteral("Manual"), Qt::CaseInsensitive) == 0)
                                              ? NormalizationReferenceMode::Manual
                                              : NormalizationReferenceMode::Automatic;
    settings.manualNormalizationReference = settings.normalizationReferenceMode == NormalizationReferenceMode::Manual
                                                ? getManualNormalizationReference()
                                                : QString();
    settings.scaleEstimator = getScaleEstimator();
    settings.overlapNormalization.enabled = getOverlapNormalizationEnabled();
    settings.overlapNormalization.minOverlapPixels = getOverlapMinimumPixels();
    settings.overlapNormalization.minOverlapFraction = getOverlapMinimumFraction();

    settings.localNormalization.enabled = getLocalNormalizationEnabled();
    settings.localNormalization.referenceStrategy = getLocalNormalizationReferenceStrategy();
    settings.localNormalization.maxIntegratedFrames = getLocalNormalizationMaxIntegratedFrames();
    settings.localNormalization.evaluationCriteria = getLocalNormalizationEvaluationCriteria();
    settings.localNormalization.gridSize = getLocalNormalizationGridSize();
    settings.localNormalization.gridInterpolation = getLocalNormalizationGridInterpolation();
    settings.localNormalization.backgroundEstimator = getLocalNormalizationBackgroundEstimator();
    settings.localNormalization.backgroundPercentile = getLocalNormalizationBackgroundPercentile();
    settings.localNormalization.sigmaClipK = getLocalNormalizationSigmaClipK();
    settings.localNormalization.sigmaClipIterations = getLocalNormalizationSigmaClipIterations();
    settings.localNormalization.backgroundSmoothingRadius = getLocalNormalizationBackgroundSmoothingRadius();
    settings.localNormalization.scaleEvaluation = getLocalNormalizationScaleEvaluation();
    settings.localNormalization.scaleMode = getLocalNormalizationScaleMode();
    settings.localNormalization.psfType = getLocalNormalizationPsfType();
    settings.localNormalization.moffatBeta = getLocalNormalizationMoffatBeta();
    settings.localNormalization.maxStars = getLocalNormalizationMaxStars();
    settings.localNormalization.minDetectionSnr = getLocalNormalizationMinDetectionSnr();
    settings.localNormalization.allowClusteredSources = getLocalNormalizationAllowClusteredSources();
    settings.localNormalization.lowClip = getLocalNormalizationLowClip();
    settings.localNormalization.highClip = getLocalNormalizationHighClip();
    settings.localNormalization.hotPixelMedianRadius = getLocalNormalizationHotPixelMedianRadius();
    settings.localNormalization.noiseReductionRadius = getLocalNormalizationNoiseReductionRadius();
    settings.localNormalization.localScaleEnabled = getLocalNormalizationLocalScaleEnabled();
    settings.localNormalization.localScaleSmoothingRadius = getLocalNormalizationLocalScaleSmoothingRadius();

    // The method the group stores, which local normalization keeps for when it is turned off.
    const QString modeText = getNormalizationMode();
    if (const std::optional<GlobalNormalizationMethod> method = globalNormalizationMethodNamed(modeText))
    {
        settings.globalNormalizationMethod = *method;
    }
    if (settings.localNormalization.enabled)
    {
        settings.normalizationMode = NormalizationMode::Local;
    }
    else if (modeText.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0)
    {
        settings.normalizationMode = NormalizationMode::None;
    }
    else
    {
        settings.normalizationMode = NormalizationMode::Global;
    }

    return ImageIntegration::sanitizeStackingSettings(std::move(settings));
}

// ---------------------------------------------------------------------------
// computeOptimalPixfrac – data-driven drizzle pixel fraction
// ---------------------------------------------------------------------------
double computeOptimalPixfrac(const DrizzleAnalysis& analysis, double scale, double medianFwhmPx)
{
    if (!analysis.available || analysis.framesWithTransforms < 2 || scale < 1.01)
        return 1.0; // No benefit; fall back to standard resampling

    // --- Base pixfrac from coverage ---
    // With perfect sub-pixel coverage (1.0) at 2× scale, the ideal drop
    // size equals 1/scale (0.5 for 2×). Poor coverage stays near 1.0.
    const double idealPixfrac = 1.0 / scale;
    double pixfrac = 1.0 - analysis.subPixelCoverage * (1.0 - idealPixfrac);

    // --- Frame-count adjustment ---
    // Fewer frames means less redundant coverage of each output pixel, so
    // the drop must be larger to avoid holes.  Below ~8 frames, nudge up.
    constexpr double kFrameCountThreshold = 8.0;
    constexpr double kFrameCountPenaltyMax = 0.15;
    const double n = static_cast<double>(analysis.framesWithTransforms);
    if (n < kFrameCountThreshold)
    {
        const double framePenalty = 1.0 - (n / kFrameCountThreshold);
        pixfrac += framePenalty * kFrameCountPenaltyMax;
    }

    // --- Seeing / FWHM constraint ---
    // The smallest output feature carrying independent information is roughly
    // FWHM/2.  In output-pixel units (output pixel = native_pixel / scale)
    // the corresponding minimum useful pixfrac is (FWHM/2) / scale.
    // Going below this produces interpolation artifacts, not extra detail.
    if (medianFwhmPx > 0.0)
    {
        const double seeingLimitPixfrac = (medianFwhmPx * 0.5) / scale;
        pixfrac = (std::max)(pixfrac, seeingLimitPixfrac);
    }

    // --- Clamp to valid range ---
    constexpr double kMinPixfrac = 0.1;
    constexpr double kMaxPixfrac = 1.0;

    // Round to one decimal place for clean UI display (e.g. 0.7, not 0.6837)
    pixfrac = std::round(pixfrac * 10.0) / 10.0;

    return std::clamp(pixfrac, kMinPixfrac, kMaxPixfrac);
}

// ---------------------------------------------------------------------------
// Recommendation engine constants
// ---------------------------------------------------------------------------
namespace
{
// Frame count thresholds — minimum frames before a rejection algorithm becomes
// statistically meaningful.  Below kMinFramesForRejection we disable rejection
// entirely; between that and kMinFramesForAdvancedRejection we use simple
// sigma clipping; above it we switch to a more powerful method.
constexpr int kMinFramesForRejection = 8;
constexpr int kMinFramesForAdvancedRejection = 20;

// LIGHT-specific tier boundaries (frame counts)
constexpr int kLightMinFramesForWeighting = 8;
constexpr int kLightMinFramesForWinsorized = 15;
constexpr int kLightMinFramesForLinearFit = 25;
constexpr int kLightMinFramesForGesdt = 40;

// Sigma / threshold defaults for each rejection method
constexpr double kPercentileLow = an::kDefaultLowMedianClipFraction;
constexpr double kPercentileHigh = an::kDefaultHighMedianClipFraction;
constexpr double kGesdtMaxOutlierFraction = 0.3; // 30 %
constexpr double kGesdtSignificanceLevel = 0.05; //  5 %

// Drizzle analysis thresholds
constexpr int kDrizzleMinFrames = 10;
constexpr double kDrizzleGoodCoverage = 0.25; // >=4 of 16 grid bins
constexpr double kDrizzleExcellentCoverage = 0.50; // >=8 of 16 grid bins
constexpr double kDrizzleRecommendedScale = 2.0;
constexpr double kDrizzleLargeSensorMp = 50.0; // MP above which we warn about output size
constexpr int kDrizzle2xSizeMultiplier = 4; // 2× scale → 4× pixel count
} // anonymous namespace

StackingRecommendation StackingSettingsDialog::getRecommendedSettings(FrameType frameType, int frameCount,
                                                                      const DrizzleAnalysis& drizzle)
{
    StackingRecommendation rec;
    rec.stackMethod = QStringLiteral("Average");

    // Dynamic sigma scaling: tighter thresholds for more frames
    // 10 frames -> ~3.0, 100 frames -> ~2.5
    auto computeDynamicSigma = [](int count, double base = 3.5) {
        if (count < 10)
            return 3.0;
        double sigma = base - 0.5 * std::log10(static_cast<double>(count));
        return std::clamp(std::round(sigma * 10.0) / 10.0, 2.0, 3.5);
    };

    switch (frameType)
    {
    case FrameType::BIAS: {
        rec.normalizationMode = QStringLiteral("None");
        if (frameCount < kMinFramesForRejection)
        {
            rec.rejectionMethod = QStringLiteral("None");
            rec.reason = QObject::tr("With only %1 BIAS frames, rejection is unreliable. "
                                     "Rejection is disabled to prevent noise injection.")
                             .arg(frameCount);
        }
        else
        {
            rec.rejectionMethod = (frameCount < kMinFramesForAdvancedRejection)
                                      ? QStringLiteral("Sigma Clipping")
                                      : QStringLiteral("Winsorized Sigma Clipping");
            rec.sigmaLow = computeDynamicSigma(frameCount);
            rec.sigmaHigh = rec.sigmaLow;
            rec.reason = QObject::tr("%1 BIAS frames: %2 removes hot pixels and cosmic rays efficiently.")
                             .arg(frameCount)
                             .arg(rec.rejectionMethod);
        }
        break;
    }

    case FrameType::DARK: {
        rec.normalizationMode = QStringLiteral("None");
        if (frameCount < kMinFramesForRejection)
        {
            rec.rejectionMethod = QStringLiteral("None");
            rec.reason = QObject::tr("With only %1 DARK frames, rejection is unreliable. "
                                     "Simple averaging used to maximize SNR.")
                             .arg(frameCount);
        }
        else
        {
            rec.rejectionMethod = (frameCount < kMinFramesForAdvancedRejection)
                                      ? QStringLiteral("Sigma Clipping")
                                      : QStringLiteral("Winsorized Sigma Clipping");
            rec.sigmaLow = computeDynamicSigma(frameCount);
            rec.sigmaHigh = rec.sigmaLow;
            rec.reason = QObject::tr("%1 DARK frames: %2 effectively removes cosmic rays and hot pixels.")
                             .arg(frameCount)
                             .arg(rec.rejectionMethod);
        }
        break;
    }

    case FrameType::FLAT: {
        rec.normalizationMode = QStringLiteral("Multiplicative");
        if (frameCount < kMinFramesForRejection)
        {
            rec.rejectionMethod = QStringLiteral("None");
            rec.reason = QObject::tr("With only %1 FLAT frames, rejection is unreliable.").arg(frameCount);
        }
        else
        {
            rec.rejectionMethod = QStringLiteral("Sigma Clipping");
            rec.sigmaLow = computeDynamicSigma(frameCount, 3.8); // Slightly more conservative for flats
            rec.sigmaHigh = rec.sigmaLow;
            rec.reason =
                QObject::tr("%1 FLAT frames: Sigma Clipping preserves vignetting gradients while removing dust.")
                    .arg(frameCount);

            // Secondary for Flats: Winsorized Sigma for large stacks
            if (frameCount >= 25)
            {
                rec.secondary.available = true;
                rec.secondary.label = QObject::tr("Aggressive Rejection");
                rec.secondary.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
                rec.secondary.sigmaLow = rec.sigmaLow - 0.2;
                rec.secondary.sigmaHigh = rec.secondary.sigmaLow;
                rec.secondary.reason = QObject::tr(
                    "Winsorized Sigma with tighter thresholds provides a cleaner master for large FLAT datasets.");
            }
        }
        break;
    }

    case FrameType::LIGHT: {
        rec.normalizationMode = QStringLiteral("Additive With Scaling");

        // Local Normalization Trigger: 15+ frames
        if (frameCount >= 15)
        {
            rec.localNormalization = true;
        }

        if (frameCount >= kLightMinFramesForWeighting)
        {
            rec.weightingMethod = QStringLiteral("Weighted FWHM");
        }

        if (frameCount < kMinFramesForRejection)
        {
            rec.rejectionMethod = QStringLiteral("Percentile");
            rec.sigmaLow = kPercentileLow;
            rec.sigmaHigh = kPercentileHigh;
            rec.reason = QObject::tr("Small dataset (%1 frames): Percentile clipping used as a robust, "
                                     "distribution-free outlier filter.")
                             .arg(frameCount);
        }
        else if (frameCount < kLightMinFramesForWinsorized)
        {
            rec.rejectionMethod = QStringLiteral("Sigma Clipping");
            rec.sigmaLow = computeDynamicSigma(frameCount);
            rec.sigmaHigh = rec.sigmaLow;
            rec.reason =
                QObject::tr(
                    "%1 LIGHT frames: Sigma Clipping provides reliable cosmic ray and satellite trail rejection.")
                    .arg(frameCount);
        }
        else if (frameCount < kLightMinFramesForLinearFit)
        {
            rec.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
            rec.sigmaLow = computeDynamicSigma(frameCount);
            rec.sigmaHigh = rec.sigmaLow;
            rec.reason =
                QObject::tr("%1 LIGHT frames: Winsorized Sigma provides robust outlier replacement, maximizing SNR.")
                    .arg(frameCount);

            // Secondary: Linear Fit (better for satellite trails)
            rec.secondary.available = true;
            rec.secondary.label = QObject::tr("Satellite Trail Removal");
            rec.secondary.rejectionMethod = QStringLiteral("Linear Fit");
            rec.secondary.sigmaLow = 5.0;
            rec.secondary.sigmaHigh = 4.0;
            rec.secondary.reason = QObject::tr("Linear Fit is superior at removing satellite trails and airplanes by "
                                               "fitting a linear model to each pixel.");
        }
        else if (frameCount < kLightMinFramesForGesdt)
        {
            rec.rejectionMethod = QStringLiteral("Linear Fit");
            rec.sigmaLow = 5.0;
            rec.sigmaHigh = 4.0;
            rec.reason =
                QObject::tr("%1 LIGHT frames: Linear Fit excels at satellite trail removal in moderate datasets.")
                    .arg(frameCount);

            // Secondary: Winsorized Sigma (better for SNR)
            rec.secondary.available = true;
            rec.secondary.label = QObject::tr("Maximum SNR");
            rec.secondary.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
            rec.secondary.sigmaLow = computeDynamicSigma(frameCount);
            rec.secondary.sigmaHigh = rec.secondary.sigmaLow;
            rec.secondary.reason = QObject::tr("Winsorized Sigma often yields slightly better SNR than Linear Fit when "
                                               "satellite trails are not a concern.");
        }
        else
        {
            // High frame count: GESDT is optimal
            rec.rejectionMethod = QStringLiteral("GESDT");
            rec.sigmaLow = kGesdtMaxOutlierFraction;
            rec.sigmaHigh = kGesdtSignificanceLevel;
            rec.reason =
                QObject::tr(
                    "%1 LIGHT frames: GESDT provides statistically optimal outlier detection for large datasets.")
                    .arg(frameCount);

            // Secondary: Linear Fit
            rec.secondary.available = true;
            rec.secondary.label = QObject::tr("Linear Fit Rejection");
            rec.secondary.rejectionMethod = QStringLiteral("Linear Fit");
            rec.secondary.sigmaLow = 4.0;
            rec.secondary.sigmaHigh = 3.5;
            rec.secondary.reason = QObject::tr(
                "Linear Fit remains a robust alternative for very large stacks with high satellite density.");
        }

        // --- Drizzle verdict ---
        // Drizzle needs: (a) registration transforms available, (b) enough frames,
        // (c) sub-pixel dithering with good coverage of the fractional-pixel space.
        // Scale recommendation: 2x is the standard choice.  3x is rarely justified
        // and massively increases memory.  For very large sensors (>50 MP),
        // drizzle at 2x may be impractical (output >200 MP) so we note the cost.
        if (drizzle.available)
        {
            const bool allFramesHaveTransforms = drizzle.framesWithTransforms == drizzle.totalFrames;
            const bool enoughFrames = drizzle.framesWithTransforms >= kDrizzleMinFrames;
            const bool goodCoverage = drizzle.subPixelCoverage >= kDrizzleGoodCoverage;
            const bool excellentCoverage = drizzle.subPixelCoverage >= kDrizzleExcellentCoverage;

            if (!allFramesHaveTransforms)
            {
                rec.drizzleVerdict = StackingRecommendation::DrizzleVerdict::NotRecommended;
                rec.drizzleReason = QObject::tr("Only %1 of %2 frames have usable registration transforms. "
                                                "Drizzle requires supported transforms for all frames. "
                                                "Run registration first to enable drizzle analysis.")
                                        .arg(drizzle.framesWithTransforms)
                                        .arg(drizzle.totalFrames);
            }
            else if (!enoughFrames)
            {
                rec.drizzleVerdict = StackingRecommendation::DrizzleVerdict::NotRecommended;
                rec.drizzleReason = QObject::tr("Only %1 registered frames. Drizzle needs at least %2 well-dithered "
                                                "frames to produce meaningful resolution improvement.")
                                        .arg(drizzle.framesWithTransforms)
                                        .arg(kDrizzleMinFrames);
            }
            else if (!goodCoverage)
            {
                rec.drizzleVerdict = StackingRecommendation::DrizzleVerdict::NotRecommended;
                rec.drizzleReason = QObject::tr("Sub-pixel coverage is very low (%1%). Your frames appear to have "
                                                "near-integer pixel shifts, indicating no dithering. Drizzle would "
                                                "only upscale the image without adding real detail. Consider enabling "
                                                "dithering in your mount/guider software for future sessions.")
                                        .arg(static_cast<int>(drizzle.subPixelCoverage * 100));
            }
            else if (!excellentCoverage)
            {
                rec.drizzleVerdict = StackingRecommendation::DrizzleVerdict::Possible;
                rec.drizzleScale = kDrizzleRecommendedScale;
                rec.drizzleReason = QObject::tr("Moderate sub-pixel dithering detected (%1% coverage). "
                                                "Drizzle at 2\u00d7 may provide some resolution improvement, but "
                                                "coverage is not ideal. Results will vary.")
                                        .arg(static_cast<int>(drizzle.subPixelCoverage * 100));
            }
            else
            {
                rec.drizzleVerdict = StackingRecommendation::DrizzleVerdict::Recommended;
                rec.drizzleScale = kDrizzleRecommendedScale;
                QString sizeNote;
                if (drizzle.imageMegapixels > kDrizzleLargeSensorMp)
                {
                    sizeNote = QObject::tr(" Note: at %1 MP, a 2\u00d7 drizzle output will be ~%2 MP \u2014 "
                                           "ensure you have sufficient RAM and disk space.")
                                   .arg(drizzle.imageMegapixels)
                                   .arg(drizzle.imageMegapixels * kDrizzle2xSizeMultiplier);
                }
                rec.drizzleReason =
                    QObject::tr("Excellent sub-pixel dithering detected (%1% coverage, %2 frames). "
                                "Drizzle at 2\u00d7 is recommended \u2014 it will recover real detail beyond "
                                "your native sensor resolution.%3")
                        .arg(static_cast<int>(drizzle.subPixelCoverage * 100))
                        .arg(drizzle.framesWithTransforms)
                        .arg(sizeNote);
            }

            // Compute optimal pixel fraction when drizzle is viable
            if (rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Recommended ||
                rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Possible)
            {
                rec.drizzlePixfrac = computeOptimalPixfrac(drizzle, rec.drizzleScale, drizzle.medianFwhmPx);
            }
        }

        break;
    }
    }

    return rec;
}

void StackingSettingsDialog::showRecommendedSettingsPopup()
{
    if (m_frameCount <= 0)
    {
        return;
    }

    const auto baseRec = getRecommendedSettings(m_frameType, m_frameCount, m_drizzleAnalysis);
    const auto current = getCurrentSettings();

    auto* reportDialog = new StackingRecommendationDialog(m_frameType, m_groupLabel, m_frameCount, baseRec,
                                                          m_drizzleAnalysis, current, this);
    reportDialog->setAttribute(Qt::WA_DeleteOnClose);
    reportDialog->setWindowModality(Qt::WindowModal);
    const QPointer<StackingSettingsDialog> guard(this);
    connect(reportDialog, &QDialog::accepted, this, [guard, reportDialog]() {
        if (guard && reportDialog->isApplyRequested())
        {
            guard->applyRecommendation(reportDialog->getRecommendation());
        }
    });
    reportDialog->open();
}

void StackingSettingsDialog::applyRecommendation(const StackingRecommendation& rec)
{
    m_ui.stackMethodSelector->setCurrentText(rec.stackMethod);
    m_ui.rejectionMethodSelector->setCurrentText(rec.rejectionMethod);
    m_ui.sigmaLowValue->setValue(rec.sigmaLow);
    m_ui.sigmaHighValue->setValue(rec.sigmaHigh);

    if (!rec.weightingMethod.isEmpty())
    {
        m_ui.weightingSelector->setCurrentText(rec.weightingMethod);
    }

    setLocalNormalizationEnabled(rec.localNormalization);

    // Only set the global normalization combo when local normalization is
    // not being enabled — local normalization supersedes the combo value.
    if (!rec.localNormalization)
    {
        setNormalizationMode(rec.normalizationMode);
    }

    if (rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Recommended ||
        rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Possible)
    {
        setDrizzleEnabled(true);
        setDrizzleScale(rec.drizzleScale);
        setDrizzlePixfrac(rec.drizzlePixfrac);
    }
    else if (rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::NotRecommended)
    {
        setDrizzleEnabled(false);
    }
}
