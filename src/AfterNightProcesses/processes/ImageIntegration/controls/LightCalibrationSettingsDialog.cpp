// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LightCalibrationSettingsDialog.h"
#include "processes/FixedDialogUiSizing.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/core/TooltipFormatUtils.h"
#include "theme/AnDesignTokens.h"
#include "components/AnCollapsibleSection.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFrame>
#include <QMessageBox>
#include <QPointer>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(LightCalibrationSettingsDialog);

LightCalibrationSettingsDialog::LightCalibrationSettingsDialog(const LightCalibrationSettings& settings,
                                                               const QString& groupLabel, QWidget* parent)
    : QDialog(parent), m_GroupLabel(groupLabel), m_InitialSettings(settings)
{
    setupUi();

    // Set initial values from settings
    m_CosmeticCorrectionCheck->setChecked(settings.cosmeticCorrection);
    m_SigmaColdSpin->setValue(settings.sigmaCold);
    m_SigmaHotSpin->setValue(settings.sigmaHot);

    m_DarkOptimizationCheck->setChecked(settings.darkOptimization);
    m_DarkMultFactorSpin->setValue(settings.darkMultiplicationFactor);

    m_FlatAutoLevelCheck->setChecked(settings.flatAutoLevel);

    m_PedestalSpin->setValue(static_cast<int>(settings.pedestal));

    // Update UI state based on checkboxes
    onCosmeticCorrectionToggled(settings.cosmeticCorrection);
}

void LightCalibrationSettingsDialog::setupUi()
{
    setWindowTitle(tr("Calibration Settings"));
    setWindowModality(Qt::WindowModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumWidth(500);

    const auto& spacing = an::ui::tokens().spacing;
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(spacing.md);
    mainLayout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);

    // Title label
    auto* titleLabel = new QLabel(tr("<b>Calibration Settings for:</b>"));
    titleLabel->setFont(an::ui::tokens().typography.heading3);
    mainLayout->addWidget(titleLabel);

    auto* groupLabel = new QLabel(m_GroupLabel);
    groupLabel->setWordWrap(true);
    groupLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    mainLayout->addWidget(groupLabel);

    // Separator
    auto* separator1 = new QFrame();
    separator1->setFrameShape(QFrame::HLine);
    separator1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator1);

    // Cosmetic Correction Group
    m_CosmeticGroup = new an::ui::AnCollapsibleSection(tr("Cosmetic Correction"));
    auto* cosmeticLayout = new QGridLayout();
    m_CosmeticGroup->setContentLayout(cosmeticLayout);
    cosmeticLayout->setSpacing(8);

    m_CosmeticCorrectionCheck = new QCheckBox(tr("Enable cosmetic correction"));
    m_CosmeticCorrectionCheck->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Cosmetic correction"),
        {tr("What it does:\nFinds hot and cold pixels in the master dark - samples further above or below their "
            "channel's median than the sigma thresholds allow, the median and standard deviation taken over the "
            "whole channel - and replaces those pixels in each calibrated light with the median of the pixels around "
            "them, of the same colour in a colour-filter mosaic. A light with no dark has nothing corrected."),
         tr("When to enable:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Your sensor has known hot or cold pixels."),
                  tr("You see single-pixel bright dots or dark spots in your calibrated frames."),
                  tr("You do not have a cosmetic-defect map from a separate tool.")}))}));
    cosmeticLayout->addWidget(m_CosmeticCorrectionCheck, 0, 0, 1, 3);

    auto* sigmaColdLabel = new QLabel(tr("Cold Sigma (σ):"));
    sigmaColdLabel->setIndent(20);
    cosmeticLayout->addWidget(sigmaColdLabel, 1, 0);

    m_SigmaColdSpin = new QDoubleSpinBox();
    m_SigmaColdSpin->setRange(0.1, 10.0);
    m_SigmaColdSpin->setSingleStep(0.1);
    m_SigmaColdSpin->setDecimals(1);
    m_SigmaColdSpin->setValue(3.0);
    m_SigmaColdSpin->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Cold pixel sigma threshold"),
        {tr("What it does:\nA pixel of the master dark this many standard deviations or more below its channel's "
            "median is flagged as cold and replaced in each light. The median and the deviation are the whole "
            "channel's, not a neighbourhood's; only the replacement is local."),
         tr("Adjust it when:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Lower it (e.g. 2.0) to catch more cold pixels — may clip faint real detail."),
                  tr("Raise it (e.g. 5.0) to be more conservative and only fix the most obvious defects.")})),
         tr("Default: 3.0")}));
    cosmeticLayout->addWidget(m_SigmaColdSpin, 1, 1);

    auto* sigmaHotLabel = new QLabel(tr("Hot Sigma (σ):"));
    sigmaHotLabel->setIndent(20);
    cosmeticLayout->addWidget(sigmaHotLabel, 2, 0);

    m_SigmaHotSpin = new QDoubleSpinBox();
    m_SigmaHotSpin->setRange(0.1, 10.0);
    m_SigmaHotSpin->setSingleStep(0.1);
    m_SigmaHotSpin->setDecimals(1);
    m_SigmaHotSpin->setValue(3.0);
    m_SigmaHotSpin->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Hot pixel sigma threshold"),
        {tr("What it does:\nA pixel of the master dark this many standard deviations or more above its channel's "
            "median is flagged as hot and replaced in each light. The median and the deviation are the whole "
            "channel's, not a neighbourhood's; only the replacement is local."),
         tr("Adjust it when:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Lower it (e.g. 2.0) to catch more hot pixels — may clip bright stars."),
                  tr("Raise it (e.g. 5.0) to preserve bright features and only fix the strongest hot pixels.")})),
         tr("Default: 3.0")}));
    cosmeticLayout->addWidget(m_SigmaHotSpin, 2, 1);

    cosmeticLayout->setColumnStretch(2, 1);

    mainLayout->addWidget(m_CosmeticGroup);

    // Dark Optimization Group
    m_DarkOptGroup = new an::ui::AnCollapsibleSection(tr("Dark Optimization"));
    auto* darkOptLayout = new QGridLayout();
    m_DarkOptGroup->setContentLayout(darkOptLayout);
    darkOptLayout->setSpacing(8);

    m_DarkOptimizationCheck = new QCheckBox(tr("Enable dark optimization"));
    m_DarkOptimizationCheck->setObjectName(QStringLiteral("darkOptimization"));
    m_DarkOptimizationCheck->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Dark optimization"),
        {tr("What it does:\nChooses for each light the factor, from 0 to 2, that the master dark is scaled by before "
            "it "
            "is subtracted: the one that leaves the least variation over the light's central 512 by 512 square. This "
            "compensates for differences in sensor temperature or exposure time between the dark and light captures. A "
            "colour light, or one smaller than that square, is scaled by the multiplication factor instead."),
         tr("When to enable:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Your darks were taken at a different temperature than the lights."),
                  tr("You want to fine-tune how aggressively dark current is removed."),
                  tr("You see residual dark-current patterns after standard calibration.")}))}));
    darkOptLayout->addWidget(m_DarkOptimizationCheck, 0, 0, 1, 3);

    auto* multFactorLabel = new QLabel(tr("Multiplication Factor:"));
    multFactorLabel->setIndent(20);
    darkOptLayout->addWidget(multFactorLabel, 1, 0);

    m_DarkMultFactorSpin = new QDoubleSpinBox();
    m_DarkMultFactorSpin->setObjectName(QStringLiteral("darkMultiplicationFactor"));
    m_DarkMultFactorSpin->setRange(0.1, 10.0);
    m_DarkMultFactorSpin->setSingleStep(0.1);
    m_DarkMultFactorSpin->setDecimals(2);
    m_DarkMultFactorSpin->setValue(1.0);
    m_DarkMultFactorSpin->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Dark multiplication factor"),
        {tr("What it does:\nMultiplies the master dark frame by this factor before subtracting it from each light. A "
            "value of 1.0 subtracts the dark as-is. With dark optimization on, it scales only the lights the "
            "optimization cannot measure: colour lights and lights smaller than 512 by 512."),
         tr("Adjust it when:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Values > 1.0 subtract more dark signal (useful if darks are cooler than lights)."),
                  tr("Values < 1.0 subtract less dark signal (useful if darks are warmer than lights).")})),
         tr("Default: 1.00")}));
    darkOptLayout->addWidget(m_DarkMultFactorSpin, 1, 1);

    darkOptLayout->setColumnStretch(2, 1);

    mainLayout->addWidget(m_DarkOptGroup);

    // Flat Auto-Level
    auto* flatGroup = new an::ui::AnCollapsibleSection(tr("Flat Field Correction"));
    auto* flatLayout = new QVBoxLayout();
    flatGroup->setContentLayout(flatLayout);

    m_FlatAutoLevelCheck = new QCheckBox(tr("Enable flat auto-level"));
    m_FlatAutoLevelCheck->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Flat auto-level"),
        {tr("What it does:\nDivides each channel of the master flat - each colour of a colour-filter mosaic - by its "
            "mean over the central third of the frame, so the correction factor is 1.0 there and the light keeps its "
            "brightness, and its colour balance, after flat-field division."),
         tr("When to enable:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Almost always — keep this enabled unless your master flat is already normalized."),
                  tr("Disable only if your flat was pre-processed to a mean of 1.0 by another tool.")}))}));
    flatLayout->addWidget(m_FlatAutoLevelCheck);

    mainLayout->addWidget(flatGroup);

    // Pedestal
    auto* pedestalGroup = new an::ui::AnCollapsibleSection(tr("Pedestal"));
    auto* pedestalLayout = new QHBoxLayout();
    pedestalGroup->setContentLayout(pedestalLayout);

    auto* pedestalLabel = new QLabel(tr("Pedestal Value:"));
    pedestalLayout->addWidget(pedestalLabel);

    m_PedestalSpin = new QSpinBox();
    m_PedestalSpin->setObjectName(QStringLiteral("pedestal"));
    m_PedestalSpin->setRange(0, 10000);
    m_PedestalSpin->setSingleStep(1);
    m_PedestalSpin->setValue(0);
    m_PedestalSpin->setToolTip(an::ui::imageintegration::formatTooltip(
        tr("Pedestal value"),
        {tr("What it does:\nAdds a fixed constant to every pixel of the calibrated light, in 16-bit codes: the value "
            "is "
            "read as a fraction of 65,535 of full scale. It lifts pixel values that would go negative after dark "
            "subtraction."),
         tr("When to adjust:\n%1")
             .arg(an::ui::imageintegration::tooltipBullets(
                 {tr("Set to 0 for most workflows. Nothing is clamped: a negative result is kept as it is."),
                  tr("Increase it if your camera has a very low bias level and you want to avoid clipping faint signal "
                     "after calibration.")})),
         tr("Default: 0")}));
    pedestalLayout->addWidget(m_PedestalSpin);

    pedestalLayout->addStretch();

    mainLayout->addWidget(pedestalGroup);

    // Expand all cards by default
    m_CosmeticGroup->setExpanded(true);
    m_DarkOptGroup->setExpanded(true);
    flatGroup->setExpanded(true);
    pedestalGroup->setExpanded(true);

    for (auto* section : {m_CosmeticGroup, m_DarkOptGroup, flatGroup, pedestalGroup})
    {
        if (section)
        {
            an::ui::processes::fixed_dialog_ui::connectSectionResizeBehavior(section->toggleButton(), this,
                                                                             section->animationDuration());
        }
    }

    mainLayout->addStretch();

    // Separator
    auto* separator2 = new QFrame();
    separator2->setFrameShape(QFrame::HLine);
    separator2->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator2);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_ResetButton = new QPushButton(tr("Reset to Defaults"));
    m_ResetButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_ResetButton->setToolTip(tr("Reset all calibration settings to their default values."));
    buttonLayout->addWidget(m_ResetButton);

    m_ApplyToAllButton = new QPushButton(tr("Apply to All Groups"));
    m_ApplyToAllButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_ApplyToAllButton->setToolTip(tr("Apply these calibration settings to every LIGHT calibration group, overwriting "
                                      "any per-group customizations."));
    buttonLayout->addWidget(m_ApplyToAllButton);

    buttonLayout->addStretch();

    m_CancelButton = new QPushButton(tr("Cancel"));
    buttonLayout->addWidget(m_CancelButton);

    m_OkButton = new QPushButton(tr("OK"));
    m_OkButton->setDefault(true);
    m_OkButton->setProperty("buttonVariant", QStringLiteral("primary"));
    buttonLayout->addWidget(m_OkButton);

    mainLayout->addLayout(buttonLayout);

    // Connect signals
    QObject::connect(m_CosmeticCorrectionCheck, &QCheckBox::toggled, this,
                     &LightCalibrationSettingsDialog::onCosmeticCorrectionToggled);

    QObject::connect(m_OkButton, &QPushButton::clicked, this, &QDialog::accept);
    QObject::connect(m_CancelButton, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(m_ApplyToAllButton, &QPushButton::clicked, this,
                     &LightCalibrationSettingsDialog::onApplyToAllClicked);

    QObject::connect(m_ResetButton, &QPushButton::clicked, this, [this]() {
        // Reset to default values
        m_CosmeticCorrectionCheck->setChecked(true);
        m_SigmaColdSpin->setValue(3.0);
        m_SigmaHotSpin->setValue(3.0);
        m_DarkOptimizationCheck->setChecked(false);
        m_DarkMultFactorSpin->setValue(1.0);
        m_FlatAutoLevelCheck->setChecked(true);
        m_PedestalSpin->setValue(0);
    });

    an::ui::processes::fixed_dialog_ui::scheduleDialogResize(this);
}

void LightCalibrationSettingsDialog::onCosmeticCorrectionToggled(bool checked)
{
    m_SigmaColdSpin->setEnabled(checked);
    m_SigmaHotSpin->setEnabled(checked);
}

LightCalibrationSettings LightCalibrationSettingsDialog::getSettings() const
{
    LightCalibrationSettings settings;
    settings.cosmeticCorrection = m_CosmeticCorrectionCheck->isChecked();
    settings.sigmaCold = m_SigmaColdSpin->value();
    settings.sigmaHot = m_SigmaHotSpin->value();
    settings.darkOptimization = m_DarkOptimizationCheck->isChecked();
    settings.darkMultiplicationFactor = m_DarkMultFactorSpin->value();
    settings.flatAutoLevel = m_FlatAutoLevelCheck->isChecked();
    settings.pedestal = static_cast<double>(m_PedestalSpin->value());
    return settings;
}

void LightCalibrationSettingsDialog::onApplyToAllClicked()
{
    const QPointer<LightCalibrationSettingsDialog> guard(this);
    ImageIntegration::detail::openNonBlockingQuestionBox(
        this, tr("Apply to All Groups"),
        tr("This will apply the current calibration settings to ALL LIGHT groups, "
           "overwriting any existing settings.\n\nDo you want to continue?"),
        [guard](const QMessageBox::StandardButton button, bool) {
            if (!guard || button != QMessageBox::Yes)
            {
                return;
            }
            guard->m_ApplyToAll = true;
            guard->accept(); // Close the dialog with Accepted status
        });
}
