// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/SpectrophotometricColorCalibration/SpccProcessReadinessPanel.h"
#include "components/AnCollapsibleSection.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>

namespace an::ui::processes
{

inline constexpr double kSpccProcessDefaultMagnitudeLimit = 17.6;
inline constexpr double kSpccProcessDefaultApertureFwhmScale = 2.0;
inline constexpr bool kSpccProcessAtmosphericCorrectionControlsEnabled = false;
inline constexpr bool kSpccProcessNarrowbandControlsEnabled = false;

struct SpectrophotometricColorCalibrationUiElements
{
    QScrollArea* optionsScrollArea = nullptr;
    QWidget* optionsScrollContent = nullptr;
    SpccProcessReadinessPanel* readinessPanel = nullptr;
    an::ui::AnCollapsibleSection* catalogWcsPreflightSection = nullptr;
    an::ui::AnCollapsibleSection* observingConditionsSection = nullptr;
    an::ui::AnCollapsibleSection* narrowbandSection = nullptr;
    an::ui::AnCollapsibleSection* photometryFitSection = nullptr;
    QCheckBox* autoMagnitudeLimitCheck = nullptr;
    QDoubleSpinBox* magnitudeLimitSpin = nullptr;
    QDoubleSpinBox* matchRadiusSpin = nullptr;
    QSpinBox* maxSourcesSpin = nullptr;
    QDoubleSpinBox* sourceSnrSpin = nullptr;
    QDoubleSpinBox* residualPxSpin = nullptr;
    QDoubleSpinBox* residualArcsecSpin = nullptr;
    QCheckBox* atmosphereCorrectionCheck = nullptr;
    QDoubleSpinBox* observerHeightSpin = nullptr;
    QDoubleSpinBox* pressureSpin = nullptr;
    QCheckBox* pressureSeaLevelCheck = nullptr;
    QDoubleSpinBox* airmassSpin = nullptr;
    QCheckBox* narrowbandModeCheck = nullptr;
    QDoubleSpinBox* narrowbandRedCenterSpin = nullptr;
    QDoubleSpinBox* narrowbandRedBandwidthSpin = nullptr;
    QDoubleSpinBox* narrowbandGreenCenterSpin = nullptr;
    QDoubleSpinBox* narrowbandGreenBandwidthSpin = nullptr;
    QDoubleSpinBox* narrowbandBlueCenterSpin = nullptr;
    QDoubleSpinBox* narrowbandBlueBandwidthSpin = nullptr;
    QSpinBox* minimumSamplesSpin = nullptr;
    QDoubleSpinBox* apertureScaleSpin = nullptr;
    QDoubleSpinBox* annulusSigmaClipSpin = nullptr;
    QComboBox* fitOutlierPolicyCombo = nullptr;
    QDoubleSpinBox* outlierSigmaSpin = nullptr;
    QSpinBox* fitIterationsSpin = nullptr;
    QCheckBox* backgroundNeutralizationCheck = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QLabel* statusLabel = nullptr;
    QProgressBar* progressBar = nullptr;
    QPlainTextEdit* diagnosticsText = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline QDoubleSpinBox* makeSpccDoubleSpinBox(QWidget* parent, const double minimum, const double maximum,
                                             const double value, const double step, const int decimals)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    return spin;
}

inline QSpinBox* makeSpccSpinBox(QWidget* parent, const int minimum, const int maximum, const int value, const int step)
{
    auto* spin = new QSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setSingleStep(step);
    return spin;
}

inline SpectrophotometricColorCalibrationUiElements buildSpectrophotometricColorCalibrationProcessUi(QWidget* parent)
{
    SpectrophotometricColorCalibrationUiElements ui;
    if (!parent)
    {
        return ui;
    }

    const auto& t = an::ui::tokens();

    parent->setObjectName(QStringLiteral("SpectrophotometricColorCalibrationProcess"));
    parent->resize(560, 720);
    parent->setMinimumSize(QSize(520, 560));
    parent->setMaximumSize(QSize(620, 820));
    parent->setWindowTitle(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                       "Spectrophotometric Color Calibration", nullptr));

    auto* rootLayout = new QVBoxLayout(parent);
    rootLayout->setContentsMargins(t.spacing.lg, t.spacing.md, t.spacing.lg, t.spacing.md);
    rootLayout->setSpacing(t.spacing.md);

    ui.statusLabel = new QLabel(parent);
    ui.statusLabel->setObjectName(QStringLiteral("spccProcessStatusLabel"));
    ui.statusLabel->setWordWrap(true);
    ui.statusLabel->setMinimumWidth(0);
    ui.statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rootLayout->addWidget(ui.statusLabel);

    ui.progressBar = new QProgressBar(parent);
    ui.progressBar->setObjectName(QStringLiteral("spccProcessProgressBar"));
    ui.progressBar->setRange(0, 100);
    ui.progressBar->setValue(0);
    ui.progressBar->setVisible(false);
    rootLayout->addWidget(ui.progressBar);

    ui.optionsScrollArea = new QScrollArea(parent);
    ui.optionsScrollArea->setObjectName(QStringLiteral("spccProcessOptionsScrollArea"));
    ui.optionsScrollArea->setFrameShape(QFrame::NoFrame);
    ui.optionsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.optionsScrollArea->setMinimumWidth(0);
    ui.optionsScrollArea->setWidgetResizable(true);
    ui.optionsScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui.optionsScrollContent = new QWidget(ui.optionsScrollArea);
    ui.optionsScrollContent->setObjectName(QStringLiteral("spccProcessOptionsScrollContent"));
    ui.optionsScrollContent->setMinimumWidth(0);
    ui.optionsScrollContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* optionsLayout = new QVBoxLayout(ui.optionsScrollContent);
    optionsLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    optionsLayout->setSpacing(t.spacing.md);
    ui.optionsScrollArea->setWidget(ui.optionsScrollContent);
    rootLayout->addWidget(ui.optionsScrollArea, 1);

    ui.readinessPanel = new SpccProcessReadinessPanel(ui.optionsScrollContent);
    optionsLayout->addWidget(ui.readinessPanel);

    ui.catalogWcsPreflightSection = new an::ui::AnCollapsibleSection(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Catalog and WCS Preflight", nullptr),
        ui.optionsScrollContent);
    ui.catalogWcsPreflightSection->setObjectName(QStringLiteral("spccCatalogWcsPreflightSection"));
    auto* wcsContent = ui.catalogWcsPreflightSection->contentWidget();
    auto* wcsForm = new QFormLayout();
    wcsForm->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    wcsForm->setHorizontalSpacing(t.spacing.sm);
    wcsForm->setVerticalSpacing(t.spacing.sm);

    ui.magnitudeLimitSpin = makeSpccDoubleSpinBox(wcsContent, 6.0, 20.0, kSpccProcessDefaultMagnitudeLimit, 0.1, 1);
    ui.magnitudeLimitSpin->setToolTip(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                    "Maximum Gaia G magnitude to query from the local SPCC catalog.", nullptr));
    ui.autoMagnitudeLimitCheck =
        new QCheckBox(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                  "Choose magnitude limit automatically", nullptr),
                      wcsContent);
    ui.autoMagnitudeLimitCheck->setChecked(true);
    ui.autoMagnitudeLimitCheck->setToolTip(QCoreApplication::translate(
        "SpectrophotometricColorCalibrationProcess",
        "Select a Gaia G limit from the WCS footprint and detected-star density, capped by the magnitude value below.",
        nullptr));
    wcsForm->addRow(QString(), ui.autoMagnitudeLimitCheck);
    wcsForm->addRow(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Magnitude cap:", nullptr),
                    ui.magnitudeLimitSpin);
    ui.magnitudeLimitSpin->setEnabled(false);

    ui.matchRadiusSpin = makeSpccDoubleSpinBox(wcsContent, 0.5, 30.0, 5.0, 0.5, 1);
    ui.matchRadiusSpin->setToolTip(QCoreApplication::translate(
        "SpectrophotometricColorCalibrationProcess",
        "Maximum angular separation in arcseconds for SPCC WCS preflight rematching.", nullptr));
    wcsForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Match radius (arcsec):", nullptr),
        ui.matchRadiusSpin);

    ui.maxSourcesSpin = makeSpccSpinBox(wcsContent, 64, 20000, 4096, 256);
    ui.maxSourcesSpin->setToolTip(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                    "Maximum detected image stars to pass into SPCC source matching.", nullptr));
    wcsForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Max detected stars:", nullptr),
        ui.maxSourcesSpin);

    ui.sourceSnrSpin = makeSpccDoubleSpinBox(wcsContent, 1.0, 100.0, 5.0, 0.5, 1);
    ui.sourceSnrSpin->setToolTip(QCoreApplication::translate(
        "SpectrophotometricColorCalibrationProcess",
        "Minimum signal-to-noise ratio for stars detected before SPCC preflight.", nullptr));
    wcsForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Detected star SNR:", nullptr),
        ui.sourceSnrSpin);

    ui.residualPxSpin = makeSpccDoubleSpinBox(wcsContent, 0.1, 20.0, 3.0, 0.1, 1);
    wcsForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Max residual (px):", nullptr),
        ui.residualPxSpin);

    ui.residualArcsecSpin = makeSpccDoubleSpinBox(wcsContent, 0.1, 30.0, 3.0, 0.1, 1);
    wcsForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Max residual (arcsec):", nullptr),
        ui.residualArcsecSpin);

    ui.catalogWcsPreflightSection->setContentLayout(wcsForm);
    ui.catalogWcsPreflightSection->setExpanded(false);
    optionsLayout->addWidget(ui.catalogWcsPreflightSection);

    if constexpr (kSpccProcessAtmosphericCorrectionControlsEnabled)
    {
        ui.observingConditionsSection = new an::ui::AnCollapsibleSection(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Observing Conditions", nullptr),
            ui.optionsScrollContent);
        auto* observingContent = ui.observingConditionsSection->contentWidget();
        auto* observingForm = new QFormLayout();
        observingForm->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        observingForm->setHorizontalSpacing(t.spacing.sm);
        observingForm->setVerticalSpacing(t.spacing.sm);

        ui.atmosphereCorrectionCheck =
            new QCheckBox(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                      "Apply atmospheric correction", nullptr),
                          observingContent);
        observingForm->addRow(QString(), ui.atmosphereCorrectionCheck);

        ui.observerHeightSpin = makeSpccDoubleSpinBox(observingContent, -500.0, 9000.0, 10.0, 10.0, 0);
        observingForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Observer height (m):", nullptr),
            ui.observerHeightSpin);

        ui.pressureSpin = makeSpccDoubleSpinBox(observingContent, 500.0, 1100.0, 1013.25, 1.0, 2);
        observingForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Pressure (hPa):", nullptr),
            ui.pressureSpin);

        ui.pressureSeaLevelCheck = new QCheckBox(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Pressure is sea-level", nullptr),
            observingContent);
        ui.pressureSeaLevelCheck->setChecked(true);
        observingForm->addRow(QString(), ui.pressureSeaLevelCheck);

        ui.airmassSpin = makeSpccDoubleSpinBox(observingContent, 0.0, 10.0, 0.0, 0.1, 2);
        ui.airmassSpin->setSpecialValueText(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Auto", nullptr));
        observingForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Airmass:", nullptr),
            ui.airmassSpin);

        ui.observingConditionsSection->setContentLayout(observingForm);
        ui.observingConditionsSection->setExpanded(false);
        optionsLayout->addWidget(ui.observingConditionsSection);
    }

    if constexpr (kSpccProcessNarrowbandControlsEnabled)
    {
        ui.narrowbandSection = new an::ui::AnCollapsibleSection(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Narrowband", nullptr),
            ui.optionsScrollContent);
        auto* narrowbandContent = ui.narrowbandSection->contentWidget();
        auto* narrowbandForm = new QFormLayout();
        narrowbandForm->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        narrowbandForm->setHorizontalSpacing(t.spacing.sm);
        narrowbandForm->setVerticalSpacing(t.spacing.sm);

        ui.narrowbandModeCheck = new QCheckBox(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                                           "Use narrowband passbands", nullptr),
                                               narrowbandContent);
        narrowbandForm->addRow(QString(), ui.narrowbandModeCheck);

        ui.narrowbandRedCenterSpin = makeSpccDoubleSpinBox(narrowbandContent, 337.0, 1019.0, 656.3, 0.1, 1);
        narrowbandForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "R center (nm):", nullptr),
            ui.narrowbandRedCenterSpin);
        ui.narrowbandRedBandwidthSpin = makeSpccDoubleSpinBox(narrowbandContent, 1.0, 200.0, 7.0, 0.1, 1);
        narrowbandForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "R width (nm):", nullptr),
            ui.narrowbandRedBandwidthSpin);
        ui.narrowbandGreenCenterSpin = makeSpccDoubleSpinBox(narrowbandContent, 337.0, 1019.0, 500.7, 0.1, 1);
        narrowbandForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "G center (nm):", nullptr),
            ui.narrowbandGreenCenterSpin);
        ui.narrowbandGreenBandwidthSpin = makeSpccDoubleSpinBox(narrowbandContent, 1.0, 200.0, 7.0, 0.1, 1);
        narrowbandForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "G width (nm):", nullptr),
            ui.narrowbandGreenBandwidthSpin);
        ui.narrowbandBlueCenterSpin = makeSpccDoubleSpinBox(narrowbandContent, 337.0, 1019.0, 486.1, 0.1, 1);
        narrowbandForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "B center (nm):", nullptr),
            ui.narrowbandBlueCenterSpin);
        ui.narrowbandBlueBandwidthSpin = makeSpccDoubleSpinBox(narrowbandContent, 1.0, 200.0, 7.0, 0.1, 1);
        narrowbandForm->addRow(
            QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "B width (nm):", nullptr),
            ui.narrowbandBlueBandwidthSpin);

        ui.narrowbandSection->setContentLayout(narrowbandForm);
        ui.narrowbandSection->setExpanded(false);
        optionsLayout->addWidget(ui.narrowbandSection);
    }

    ui.photometryFitSection = new an::ui::AnCollapsibleSection(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Photometry and Fit", nullptr),
        ui.optionsScrollContent);
    ui.photometryFitSection->setObjectName(QStringLiteral("spccPhotometryFitSection"));
    auto* photometryContent = ui.photometryFitSection->contentWidget();
    auto* photometryForm = new QFormLayout();
    photometryForm->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    photometryForm->setHorizontalSpacing(t.spacing.sm);
    photometryForm->setVerticalSpacing(t.spacing.sm);

    ui.minimumSamplesSpin =
        makeSpccSpinBox(photometryContent, 5, 500, static_cast<int>(an::kDefaultSpectrophotometricMinimumSamples), 1);
    photometryForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Minimum samples:", nullptr),
        ui.minimumSamplesSpin);

    ui.apertureScaleSpin =
        makeSpccDoubleSpinBox(photometryContent, 0.5, 5.0, kSpccProcessDefaultApertureFwhmScale, 0.1, 1);
    photometryForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "FWHM aperture scale:", nullptr),
        ui.apertureScaleSpin);

    ui.annulusSigmaClipSpin = makeSpccDoubleSpinBox(photometryContent, 1.0, 8.0, 3.0, 0.1, 1);
    photometryForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Annulus sigma clip:", nullptr),
        ui.annulusSigmaClipSpin);

    ui.fitOutlierPolicyCombo = new QComboBox(photometryContent);
    ui.fitOutlierPolicyCombo->setObjectName(QStringLiteral("spccFitOutlierPolicyCombo"));
    ui.fitOutlierPolicyCombo->addItem(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "AfterNight iterative", nullptr),
        static_cast<int>(an::SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit));
    ui.fitOutlierPolicyCombo->addItem(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Single-pass", nullptr),
        static_cast<int>(an::SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask));
    photometryForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Outlier policy:", nullptr),
        ui.fitOutlierPolicyCombo);

    ui.outlierSigmaSpin =
        makeSpccDoubleSpinBox(photometryContent, 1.0, 8.0, an::kDefaultSpectrophotometricClipSigmas, 0.1, 1);
    photometryForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Fit outlier sigma:", nullptr),
        ui.outlierSigmaSpin);

    ui.fitIterationsSpin =
        makeSpccSpinBox(photometryContent, 1, 20, static_cast<int>(an::kDefaultSpectrophotometricMaximumIterations), 1);
    photometryForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Fit iterations:", nullptr),
        ui.fitIterationsSpin);

    ui.backgroundNeutralizationCheck =
        new QCheckBox(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess",
                                                  "Apply background neutralization", nullptr),
                      photometryContent);
    ui.backgroundNeutralizationCheck->setChecked(true);
    photometryForm->addRow(QString(), ui.backgroundNeutralizationCheck);

    ui.photometryFitSection->setContentLayout(photometryForm);
    ui.photometryFitSection->setExpanded(false);
    optionsLayout->addWidget(ui.photometryFitSection);

    auto* diagnosticsGroup =
        new QGroupBox(QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Diagnostics", nullptr),
                      ui.optionsScrollContent);
    diagnosticsGroup->setObjectName(QStringLiteral("spccDiagnosticsGroup"));
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsGroup);
    diagnosticsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    diagnosticsLayout->setSpacing(t.spacing.sm);
    ui.diagnosticsText = new QPlainTextEdit(diagnosticsGroup);
    ui.diagnosticsText->setObjectName(QStringLiteral("spccDiagnosticsText"));
    ui.diagnosticsText->setReadOnly(true);
    ui.diagnosticsText->setMaximumHeight(t.spacing.xl * 4);
    diagnosticsLayout->addWidget(ui.diagnosticsText);
    optionsLayout->addWidget(diagnosticsGroup);

    auto* targetForm = new QFormLayout();
    targetForm->setHorizontalSpacing(t.spacing.sm);
    targetForm->setVerticalSpacing(t.spacing.sm);
    ui.targetImageSelector = new QComboBox(parent);
    targetForm->addRow(
        QCoreApplication::translate("SpectrophotometricColorCalibrationProcess", "Target image:", nullptr),
        ui.targetImageSelector);
    rootLayout->addLayout(targetForm);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    rootLayout->addWidget(ui.buttonBox);

    ui.autoMagnitudeLimitCheck->setObjectName(QStringLiteral("spccAutoMagnitudeLimitCheck"));
    ui.magnitudeLimitSpin->setObjectName(QStringLiteral("spccMagnitudeLimitSpin"));
    ui.matchRadiusSpin->setObjectName(QStringLiteral("spccMatchRadiusSpin"));
    ui.maxSourcesSpin->setObjectName(QStringLiteral("spccMaxSourcesSpin"));
    ui.sourceSnrSpin->setObjectName(QStringLiteral("spccSourceSnrSpin"));
    ui.residualPxSpin->setObjectName(QStringLiteral("spccResidualPxSpin"));
    ui.residualArcsecSpin->setObjectName(QStringLiteral("spccResidualArcsecSpin"));
    ui.minimumSamplesSpin->setObjectName(QStringLiteral("spccMinimumSamplesSpin"));
    ui.apertureScaleSpin->setObjectName(QStringLiteral("spccApertureScaleSpin"));
    ui.annulusSigmaClipSpin->setObjectName(QStringLiteral("spccAnnulusSigmaClipSpin"));
    ui.outlierSigmaSpin->setObjectName(QStringLiteral("spccOutlierSigmaSpin"));
    ui.fitIterationsSpin->setObjectName(QStringLiteral("spccFitIterationsSpin"));
    ui.backgroundNeutralizationCheck->setObjectName(QStringLiteral("spccBackgroundNeutralizationCheck"));
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));

    return ui;
}

} // namespace an::ui::processes
