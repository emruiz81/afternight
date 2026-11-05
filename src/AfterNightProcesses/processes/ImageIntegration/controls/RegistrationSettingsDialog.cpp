// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationSettingsDialog.h"

#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/core/TooltipFormatUtils.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialog.h"
#include "processes/ImageIntegration/dialogs/ManualRegistrationDialog.h"
#include "processes/FixedDialogUiSizing.h"
#include "app/DemosaicTextMaps.h"
#include "app/PlateSolveConfiguration.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <verdigris/wobjectimpl.h>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QPushButton>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStringList>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <algorithm>

namespace
{

using an::ui::processes::fixed_dialog_ui::connectSectionResizeBehavior;
using an::ui::processes::fixed_dialog_ui::scheduleDialogResize;

using an::ui::imageintegration::formatTooltip;
using an::ui::imageintegration::setSharedToolTip;
using an::ui::imageintegration::tooltipBullets;
using an::ui::imageintegration::tooltipNumbered;

QString resolveGlobalDemosaicKey()
{
    const QString fallback = QString(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
    return process_utils::selectGlobalSetting(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, fallback);
}

QString trRegistration(const char* text)
{
    return QCoreApplication::translate("RegistrationSettingsDialog", text);
}

QString normalizedTransformSelectionKey(const QString& value)
{
    return value.trimmed().toLower();
}

using ImageIntegration::RegistrationSettingsUtils::baseTransformForSelectionKey;
using ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod;
using ImageIntegration::RegistrationSettingsUtils::requiresFixedReference;
using ImageIntegration::RegistrationSettingsUtils::transformModeForSelectionKey;
using ImageIntegration::RegistrationSettingsUtils::transformOrderForSelectionKey;
using ImageIntegration::RegistrationSettingsUtils::twoPassForSelectionKey;
using ImageIntegration::RegistrationSettingsUtils::usesCometTracking;
using ImageIntegration::RegistrationSettingsUtils::usesManualControlPoints;
using ImageIntegration::RegistrationSettingsUtils::usesStarDetection;

QString transformSummaryTextForSelectionKey(const QString& selection_key)
{
    const QString normalized = normalizedTransformSelectionKey(selection_key);
    if (normalized == QStringLiteral("similarity"))
    {
        return trRegistration("Linear-only registration using a 4-parameter similarity transform.");
    }
    if (normalized == QStringLiteral("affine"))
    {
        return trRegistration("Linear-only registration using an affine transform.");
    }
    if (normalized == QStringLiteral("homography"))
    {
        return trRegistration("Linear-only registration using a full homography.");
    }
    if (normalized == QStringLiteral("polynomial2"))
    {
        return trRegistration("Two-pass registration: homography base alignment plus order-2 polynomial residual "
                              "refinement in a single remap.");
    }
    if (normalized == QStringLiteral("polynomial3"))
    {
        return trRegistration("Two-pass registration: homography base alignment plus order-3 polynomial residual "
                              "refinement in a single remap.");
    }
    if (normalized == QStringLiteral("polynomial4"))
    {
        return trRegistration("Two-pass registration: homography base alignment plus order-4 polynomial residual "
                              "refinement in a single remap.");
    }
    return trRegistration(
        "Auto mode starts from a homography, then keeps the linear model or upgrades to polynomial refinement when "
        "residual improvement justifies it. Falls back to linear if the fit is weak.");
}

QString registrationMethodSummaryText(const QString& method_key, const QString& transform_key)
{
    if (normalizeRegistrationMethod(method_key) == QStringLiteral("phase_correlation"))
    {
        return trRegistration("Shift-only registration using FFT phase correlation. The selected reference frame is "
                              "used directly and star-detection or transform-model settings are ignored.");
    }
    if (normalizeRegistrationMethod(method_key) == QStringLiteral("manual_points"))
    {
        return trRegistration("Manual N-point registration uses the selected reference frame and the control-point "
                              "pairs you define per target frame. "
                              "Choose a linear or polynomial transform model, then edit points for each frame that "
                              "needs manual alignment.");
    }
    if (normalizeRegistrationMethod(method_key) == QStringLiteral("comet_tracking"))
    {
        return trRegistration(
            "Comet Tracking aligns the moving object using timestamp-interpolated translation only. Mark the object "
            "in two frames; star-detection and transform-model settings are ignored.");
    }
    return transformSummaryTextForSelectionKey(transform_key);
}

} // namespace

W_OBJECT_IMPL(RegistrationSettingsDialog)

RegistrationSettingsDialog::RegistrationSettingsDialog(const QString& groupLabel,
                                                       const QStringList& availableLightFrames, QWidget* parent)
    : QDialog(parent), m_groupLabel(groupLabel), m_availableLightFrames(availableLightFrames)
{
    m_ui = an::ui::imageintegration::buildRegistrationSettingsDialogUi(this);

    // Hidden by default — callers that supply auto-assign logic show it.
    if (m_ui.autoAssignButton)
        m_ui.autoAssignButton->setVisible(false);

    // Set the group label text
    this->m_ui.groupLabel->setText(m_groupLabel);

    // Set window title
    setWindowTitle(tr("Registration Settings"));
    setWindowModality(Qt::WindowModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    // Connect buttons
    QObject::connect(m_ui.resetButton, &QPushButton::clicked, this,
                     &RegistrationSettingsDialog::on_resetButton_clicked);
    QObject::connect(m_ui.applyToAllButton, &QPushButton::clicked, this,
                     &RegistrationSettingsDialog::on_applyToAllButton_clicked);
    QObject::connect(m_ui.editManualPointsButton, &QPushButton::clicked, this,
                     &RegistrationSettingsDialog::on_editManualPointsButton_clicked);
    QObject::connect(m_ui.advancedSettingsButton, &QPushButton::clicked, this,
                     &RegistrationSettingsDialog::on_advancedSettingsButton_clicked);

    // Connect sliders and spinboxes for star detection parameters
    QObject::connect(m_ui.radiusSlider, &QSlider::valueChanged, m_ui.radiusSpinBox, &QSpinBox::setValue);
    QObject::connect(m_ui.radiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), m_ui.radiusSlider,
                     &QSlider::setValue);

    QObject::connect(m_ui.thresholdSlider, &QSlider::valueChanged, this,
                     [this](int value) { m_ui.thresholdSpinBox->setValue(value / 1000.0); });
    QObject::connect(m_ui.thresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     [this](double value) { m_ui.thresholdSlider->setValue(static_cast<int>(value * 1000)); });

    QObject::connect(m_ui.thresholdMinimumSlider, &QSlider::valueChanged, this,
                     [this](int value) { m_ui.thresholdMinimumSpinBox->setValue(value / 1000.0); });
    QObject::connect(m_ui.thresholdMinimumSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     [this](double value) { m_ui.thresholdMinimumSlider->setValue(static_cast<int>(value * 1000)); });

    QObject::connect(m_ui.thresholdMaximumSlider, &QSlider::valueChanged, this,
                     [this](int value) { m_ui.thresholdMaximumSpinBox->setValue(value / 1000.0); });
    QObject::connect(m_ui.thresholdMaximumSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     [this](double value) { m_ui.thresholdMaximumSlider->setValue(static_cast<int>(value * 1000)); });

    QObject::connect(m_ui.roundnessThresholdSlider, &QSlider::valueChanged, this,
                     [this](int value) { m_ui.roundnessThresholdSpinBox->setValue(value / 1000.0); });
    QObject::connect(m_ui.roundnessThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     [this](double value) { m_ui.roundnessThresholdSlider->setValue(static_cast<int>(value * 1000)); });

    QObject::connect(m_ui.roundnessThresholdMaximumSlider, &QSlider::valueChanged, this,
                     [this](int value) { m_ui.roundnessThresholdMaximumSpinBox->setValue(value / 1000.0); });
    QObject::connect(
        m_ui.roundnessThresholdMaximumSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) { m_ui.roundnessThresholdMaximumSlider->setValue(static_cast<int>(value * 1000)); });

    initialize();
    ensureAdvancedSettingsDialog();
    connectSectionResizeBehavior(m_ui.registrationGroupBox ? m_ui.registrationGroupBox->toggleButton() : nullptr, this,
                                 m_ui.registrationGroupBox ? m_ui.registrationGroupBox->animationDuration() : 0);
    connectSectionResizeBehavior(m_ui.alignmentGroupBox ? m_ui.alignmentGroupBox->toggleButton() : nullptr, this,
                                 m_ui.alignmentGroupBox ? m_ui.alignmentGroupBox->animationDuration() : 0);
    scheduleDialogResize(this);
}

RegistrationSettingsDialog::~RegistrationSettingsDialog() {}

void RegistrationSettingsDialog::setRegistrationGroupContext(const QString& resolution, const QString& object)
{
    m_resolution = resolution;
    m_object = object;
}

void RegistrationSettingsDialog::setRegistrationSettings(const RegistrationSettings& settings)
{
    m_registrationSettings = settings;

    setMinStarPairs(settings.minStarPairs);
    setMaxStarPairs(settings.maxStarPairs);
    setReferenceImage(settings.referenceImage);
    setCometTrackingSettings(settings.cometTracking);
    setChromaticAberrationSettings(settings.chromaticAberration);
    setTransformContract(settings.transformation, settings.transformMode, settings.transformOrder);
    setManualControlPointsByFrame(settings.manualControlPointsByFrame);
    setRegistrationMethod(settings.registrationMethod);
    setAutoReferenceDuringRegistration(settings.autoReferenceDuringRegistration);
    setAutoSolveUnsolvedFrames(settings.autoSolveUnsolvedFrames);
    setEnrichWithGaiaCatalog(settings.enrichWithGaiaCatalog);
    setAlignmentMode(settings.alignmentMode);
    setInterpolation(settings.interpolation);

    setRadius(settings.radius);
    setThreshold(settings.threshold);
    setAmplitudeRangeEnabled(settings.amplitudeRangeEnabled);
    setThresholdMinimum(settings.thresholdMinimum);
    setThresholdMaximum(settings.thresholdMaximum);
    setRoundnessThreshold(settings.roundnessThreshold);
    setRoundnessRangeEnabled(settings.roundnessRangeEnabled);
    setRoundnessThresholdMaximum(settings.roundnessThresholdMaximum);
    setConvergence(settings.convergence);
    setRelaxPSFChecks(settings.relaxPsfChecks);
    setProfileType(settings.profileType);
    setMinimumBeta(settings.minimumBeta);

    setCFAPattern(settings.cfaPattern);
    setDebayerMethod(settings.debayerMethod);
}

RegistrationSettings RegistrationSettingsDialog::getRegistrationSettings() const
{
    return currentRegistrationSettings();
}

RegistrationSettings RegistrationSettingsDialog::currentRegistrationSettings() const
{
    RegistrationSettings settings = m_registrationSettings;

    settings.minStarPairs = getMinStarPairs();
    settings.maxStarPairs = getMaxStarPairs();
    settings.referenceImage = getReferenceImage();
    settings.registrationMethod = getRegistrationMethod();
    settings.transformation = getTransformation();
    settings.transformMode = getTransformMode();
    settings.transformOrder = getTransformOrder();
    settings.twoPassRefinementEnabled = getTwoPassRefinementEnabled();
    settings.alignmentMode = getAlignmentMode();
    settings.interpolation = getInterpolation();
    settings.autoReferenceDuringRegistration = getAutoReferenceDuringRegistration();
    settings.autoSolveUnsolvedFrames = getAutoSolveUnsolvedFrames();
    settings.enrichWithGaiaCatalog = getEnrichWithGaiaCatalog();
    settings.manualControlPointsByFrame = getManualControlPointsByFrame();
    settings.cometTracking = getCometTrackingSettings();
    settings.chromaticAberration = getChromaticAberrationSettings();

    settings.radius = getRadius();
    settings.threshold = getThreshold();
    settings.amplitudeRangeEnabled = getAmplitudeRangeEnabled();
    settings.thresholdMinimum = getThresholdMinimum();
    settings.thresholdMaximum = getThresholdMaximum();
    settings.roundnessThreshold = getRoundnessThreshold();
    settings.roundnessRangeEnabled = getRoundnessRangeEnabled();
    settings.roundnessThresholdMaximum = getRoundnessThresholdMaximum();
    settings.convergence = getConvergence();
    settings.relaxPsfChecks = getRelaxPSFChecks();
    settings.profileType = getProfileType();
    settings.minimumBeta = getMinimumBeta();
    settings.cfaPattern = getCFAPattern();
    settings.debayerMethod = getDebayerMethod();

    return settings;
}

// Called on the UI thread during initialize()/updateAutoSolveUi(). The settings are the ones the workflow's manifest
// copies, and the answer is the one the registration and the solve service give: a ready Gaia catalog, an executable
// ASTAP or local astrometry.net, or an online key.
bool RegistrationSettingsDialog::hasConfiguredPlateSolveSolver() const
{
    auto* globalSettings = process_utils::currentGlobalSettings();
    if (!globalSettings)
    {
        return false;
    }

    an::ui::PlateSolveSettings settings;
    settings.defaultSolver =
        globalSettings->select(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER, SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER);
    settings.astapPath = globalSettings->select(SettingsKeys::TOOLS_ASTAP_PATH, SettingsDefaults::TOOLS_ASTAP_PATH);
    settings.astrometryLocalPath = globalSettings->select(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH,
                                                          SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH);
    settings.astrometryApiKey =
        globalSettings->select(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, SettingsDefaults::TOOLS_ASTROMETRY_API_KEY);
    settings.gaiaCatalogDirectory = globalSettings->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                                           SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY);
    settings.gaiaCatalogPack = globalSettings->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK,
                                                      SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
    return an::ui::hasAnyConfiguredPlateSolveSolver(settings);
}

void RegistrationSettingsDialog::initialize()
{
    // Populate reference image combo box with available light frames
    m_ui.referenceImageComboBox->clear();
    m_ui.referenceImageComboBox->addItems(m_availableLightFrames);

    // Setup autocomplete for reference image combo box
    QCompleter* completer = new QCompleter(m_availableLightFrames, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_ui.referenceImageComboBox->setCompleter(completer);
    QObject::connect(m_ui.referenceImageComboBox, &QComboBox::currentTextChanged, this,
                     [this](const QString&) { updateManualPointsSummary(); });

    m_ui.registrationMethodComboBox->addItem(tr("Star Matching"), QStringLiteral("star_matching"));
    m_ui.registrationMethodComboBox->addItem(tr("Phase Correlation"), QStringLiteral("phase_correlation"));
    m_ui.registrationMethodComboBox->addItem(tr("Manual Points"), QStringLiteral("manual_points"));
    m_ui.registrationMethodComboBox->addItem(tr("Comet Tracking"), QStringLiteral("comet_tracking"));
    QObject::connect(m_ui.registrationMethodComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) { updateRegistrationMethodUi(); });

    if (!m_cometTrackingPanel)
    {
        const auto& t = an::ui::tokens();
        m_cometTrackingPanel = new QWidget(m_ui.registrationGroupBox->contentWidget());
        m_cometTrackingPanel->setObjectName(QStringLiteral("cometTrackingPanel"));
        auto* comet_layout = new QVBoxLayout(m_cometTrackingPanel);
        comet_layout->setContentsMargins(0, 0, 0, 0);
        comet_layout->setSpacing(t.spacing.sm);

        auto* output_row = new QHBoxLayout();
        output_row->setObjectName(QStringLiteral("cometOutputModeRow"));
        auto* output_label = new QLabel(tr("Comet Output:"), m_cometTrackingPanel);
        output_label->setMinimumSize(QSize(150, 0));
        output_label->setObjectName(QStringLiteral("cometOutputModeLabel"));
        output_row->addWidget(output_label);

        m_cometOutputModeComboBox = new QComboBox(m_cometTrackingPanel);
        m_cometOutputModeComboBox->setObjectName(QStringLiteral("cometOutputModeSelector"));
        m_cometOutputModeComboBox->setMinimumSize(QSize(220, 0));
        m_cometOutputModeComboBox->addItem(tr("Comet only"), QStringLiteral("Comet"));
        // StarsPlusCometExperimental is intentionally hidden from the UI. The current composite/artifact pipeline is
        // not reliable enough for user-facing workflows and needs a first-class compositor redesign.
        output_row->addWidget(m_cometOutputModeComboBox);
        output_row->addStretch(1);
        comet_layout->addLayout(output_row);

        m_cometOutputModeInfoLabel = new QLabel(m_cometTrackingPanel);
        m_cometOutputModeInfoLabel->setObjectName(QStringLiteral("cometOutputModeInfoLabel"));
        m_cometOutputModeInfoLabel->setWordWrap(true);
        m_cometOutputModeInfoLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        m_cometOutputModeInfoLabel->setFont(t.typography.caption);
        comet_layout->addWidget(m_cometOutputModeInfoLabel);

        m_trackCometButton = new QPushButton(tr("Track Comet..."), m_cometTrackingPanel);
        m_trackCometButton->setObjectName(QStringLiteral("trackCometButton"));
        comet_layout->addWidget(m_trackCometButton);

        m_cometTrackingSummaryLabel = new QLabel(m_cometTrackingPanel);
        m_cometTrackingSummaryLabel->setObjectName(QStringLiteral("cometTrackingSummaryLabel"));
        m_cometTrackingSummaryLabel->setWordWrap(true);
        m_cometTrackingSummaryLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        comet_layout->addWidget(m_cometTrackingSummaryLabel);

        m_ui.verticalLayout_registration->addWidget(m_cometTrackingPanel);

        QObject::connect(
            m_cometOutputModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
                auto comet =
                    m_registrationSettings.cometTracking.value_or(RegistrationSettings::CometTrackingSettings{});
                comet.outputMode = currentCometOutputMode();
                m_registrationSettings.cometTracking = comet;
                updateCometOutputModeUi();
                updateCometTrackingSummary();
            });
        QObject::connect(m_trackCometButton, &QPushButton::clicked, this,
                         &RegistrationSettingsDialog::on_trackCometButton_clicked);
    }

    if (!m_chromaticAberrationPanel)
    {
        const auto& t = an::ui::tokens();
        m_chromaticAberrationPanel = new QWidget(m_ui.registrationGroupBox->contentWidget());
        auto* ca_layout = new QVBoxLayout(m_chromaticAberrationPanel);
        ca_layout->setContentsMargins(0, 0, 0, 0);
        ca_layout->setSpacing(t.spacing.sm);

        m_chromaticAberrationEnabledCheckBox =
            new QCheckBox(tr("Correct chromatic aberration before registration"), m_chromaticAberrationPanel);
        m_chromaticAberrationEnabledCheckBox->setObjectName(QStringLiteral("chromaticAberrationEnabledCheckBox"));
        ca_layout->addWidget(m_chromaticAberrationEnabledCheckBox);

        auto* edge_row = new QHBoxLayout();
        auto* edge_label = new QLabel(tr("Edge Fraction:"), m_chromaticAberrationPanel);
        edge_label->setMinimumSize(QSize(150, 0));
        m_chromaticAberrationEdgeFractionSpinBox = new QDoubleSpinBox(m_chromaticAberrationPanel);
        m_chromaticAberrationEdgeFractionSpinBox->setObjectName(
            QStringLiteral("chromaticAberrationEdgeFractionSpinBox"));
        m_chromaticAberrationEdgeFractionSpinBox->setRange(0.30, 0.80);
        m_chromaticAberrationEdgeFractionSpinBox->setDecimals(2);
        m_chromaticAberrationEdgeFractionSpinBox->setSingleStep(0.05);
        edge_row->addWidget(edge_label);
        edge_row->addWidget(m_chromaticAberrationEdgeFractionSpinBox);
        edge_row->addStretch(1);
        ca_layout->addLayout(edge_row);

        auto* transform_row = new QHBoxLayout();
        auto* transform_label = new QLabel(tr("CA Fit Type:"), m_chromaticAberrationPanel);
        transform_label->setMinimumSize(QSize(150, 0));
        m_chromaticAberrationTransformTypeComboBox = new QComboBox(m_chromaticAberrationPanel);
        m_chromaticAberrationTransformTypeComboBox->setObjectName(
            QStringLiteral("chromaticAberrationTransformTypeComboBox"));
        m_chromaticAberrationTransformTypeComboBox->setMinimumSize(QSize(130, 0));
        m_chromaticAberrationTransformTypeComboBox->setSizeAdjustPolicy(
            QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
        m_chromaticAberrationTransformTypeComboBox->setMinimumContentsLength(10);
        transform_row->addWidget(transform_label);
        transform_row->addWidget(m_chromaticAberrationTransformTypeComboBox);
        transform_row->addStretch(1);
        ca_layout->addLayout(transform_row);

        auto* stars_row = new QHBoxLayout();
        auto* stars_label = new QLabel(tr("Min Matched Stars:"), m_chromaticAberrationPanel);
        stars_label->setMinimumSize(QSize(150, 0));
        m_chromaticAberrationMinMatchedStarsSpinBox = new QSpinBox(m_chromaticAberrationPanel);
        m_chromaticAberrationMinMatchedStarsSpinBox->setObjectName(
            QStringLiteral("chromaticAberrationMinMatchedStarsSpinBox"));
        m_chromaticAberrationMinMatchedStarsSpinBox->setRange(3, 50);
        stars_row->addWidget(stars_label);
        stars_row->addWidget(m_chromaticAberrationMinMatchedStarsSpinBox);
        stars_row->addStretch(1);
        ca_layout->addLayout(stars_row);

        auto* interpolation_row = new QHBoxLayout();
        auto* interpolation_label = new QLabel(tr("CA Interpolation:"), m_chromaticAberrationPanel);
        interpolation_label->setMinimumSize(QSize(150, 0));
        m_chromaticAberrationInterpolationComboBox = new QComboBox(m_chromaticAberrationPanel);
        m_chromaticAberrationInterpolationComboBox->setObjectName(
            QStringLiteral("chromaticAberrationInterpolationComboBox"));
        m_chromaticAberrationInterpolationComboBox->setMinimumSize(QSize(170, 0));
        m_chromaticAberrationInterpolationComboBox->setSizeAdjustPolicy(
            QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
        m_chromaticAberrationInterpolationComboBox->setMinimumContentsLength(14);
        interpolation_row->addWidget(interpolation_label);
        interpolation_row->addWidget(m_chromaticAberrationInterpolationComboBox);
        interpolation_row->addStretch(1);
        ca_layout->addLayout(interpolation_row);

        m_chromaticAberrationSummaryLabel = new QLabel(m_chromaticAberrationPanel);
        m_chromaticAberrationSummaryLabel->setObjectName(QStringLiteral("chromaticAberrationSummaryLabel"));
        m_chromaticAberrationSummaryLabel->setWordWrap(true);
        m_chromaticAberrationSummaryLabel->setMargin(2);
        m_chromaticAberrationSummaryLabel->setMinimumHeight(m_chromaticAberrationSummaryLabel->fontMetrics().height() +
                                                            4);
        m_chromaticAberrationSummaryLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        m_chromaticAberrationSummaryLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        ca_layout->addWidget(m_chromaticAberrationSummaryLabel);

        m_ui.verticalLayout_registration->addWidget(m_chromaticAberrationPanel);

        const auto update_ca_ui = [this]() { updateChromaticAberrationSummary(); };
        QObject::connect(m_chromaticAberrationEnabledCheckBox, &QCheckBox::toggled, this,
                         [update_ca_ui](bool) { update_ca_ui(); });
        QObject::connect(m_chromaticAberrationEdgeFractionSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this, [update_ca_ui](double) { update_ca_ui(); });
        QObject::connect(m_chromaticAberrationTransformTypeComboBox, &QComboBox::currentTextChanged, this,
                         [update_ca_ui](const QString&) { update_ca_ui(); });
        QObject::connect(m_chromaticAberrationMinMatchedStarsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                         [update_ca_ui](int) { update_ca_ui(); });
        QObject::connect(m_chromaticAberrationInterpolationComboBox, &QComboBox::currentTextChanged, this,
                         [update_ca_ui](const QString&) { update_ca_ui(); });
    }

    // Populate transformation combo box
    m_ui.transformationComboBox->addItem(tr("Similarity"), QStringLiteral("similarity"));
    m_ui.transformationComboBox->addItem(tr("Affine"), QStringLiteral("affine"));
    m_ui.transformationComboBox->addItem(tr("Homography"), QStringLiteral("homography"));
    m_ui.transformationComboBox->addItem(tr("Polynomial 2"), QStringLiteral("polynomial2"));
    m_ui.transformationComboBox->addItem(tr("Polynomial 3"), QStringLiteral("polynomial3"));
    m_ui.transformationComboBox->addItem(tr("Polynomial 4"), QStringLiteral("polynomial4"));
    m_ui.transformationComboBox->addItem(tr("Auto"), QStringLiteral("auto"));
    QObject::connect(m_ui.transformationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) { updateTransformUi(); });

    // Populate interpolation combo box
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_AUTO);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_NEAREST_NEIGHBOR);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_BILINEAR);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_BICUBIC);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_AREA);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_LANCZOS4);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_BIT_EXACT_BILINEAR);
    m_ui.interpolationComboBox->addItem(process_utils::INTERP_BIT_EXACT_NEAREST_NEIGHBOR);
    if (m_chromaticAberrationTransformTypeComboBox)
    {
        m_chromaticAberrationTransformTypeComboBox->clear();
        m_chromaticAberrationTransformTypeComboBox->addItem(QStringLiteral("Similarity"));
        m_chromaticAberrationTransformTypeComboBox->addItem(QStringLiteral("Affine"));
    }
    if (m_chromaticAberrationInterpolationComboBox)
    {
        m_chromaticAberrationInterpolationComboBox->clear();
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_BILINEAR);
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_BICUBIC);
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_LANCZOS4);
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_AREA);
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_NEAREST_NEIGHBOR);
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_BIT_EXACT_BILINEAR);
        m_chromaticAberrationInterpolationComboBox->addItem(process_utils::INTERP_BIT_EXACT_NEAREST_NEIGHBOR);
    }

    // Populate profile type combo box (advanced settings)
    m_ui.profileTypeComboBox->addItem("Gaussian");
    m_ui.profileTypeComboBox->addItem("Moffat");

    // Populate CFA pattern combo box (advanced settings)
    m_ui.cfaPatternComboBox->addItem(an::ui::kBayerPatternFromImageText);
    m_ui.cfaPatternComboBox->addItem(an::ui::kBayerPatternRggbText);
    m_ui.cfaPatternComboBox->addItem(an::ui::kBayerPatternBggrText);
    m_ui.cfaPatternComboBox->addItem(an::ui::kBayerPatternGbrgText);
    m_ui.cfaPatternComboBox->addItem(an::ui::kBayerPatternGrbgText);

    // Populate debayer method combo box (advanced settings)
    const QString global_key = resolveGlobalDemosaicKey();
    const QString global_label = process_utils::demosaicLabelFromSettingsKey(global_key);
    m_ui.debayerMethodComboBox->addItem(tr("Default (%1)").arg(global_label), process_utils::DEMOSAIC_DEFAULT_KEY);
    m_ui.debayerMethodComboBox->addItem(process_utils::DEMOSAIC_BILINEAR, process_utils::DEMOSAIC_BILINEAR);
    m_ui.debayerMethodComboBox->addItem(process_utils::DEMOSAIC_VNG4, process_utils::DEMOSAIC_VNG4);
    m_ui.debayerMethodComboBox->addItem(process_utils::DEMOSAIC_AHD, process_utils::DEMOSAIC_AHD);
    m_ui.debayerMethodComboBox->addItem(process_utils::DEMOSAIC_AMAZE, process_utils::DEMOSAIC_AMAZE);
    m_ui.debayerMethodComboBox->addItem(process_utils::DEMOSAIC_LMMSE, process_utils::DEMOSAIC_LMMSE);
    m_ui.debayerMethodComboBox->addItem(process_utils::DEMOSAIC_RCD, process_utils::DEMOSAIC_RCD);

    // Set default values
    resetToDefaults();

    ensureAutoReferenceControl();
    ensureAutoSolveControl();
    ensureGaiaEnrichmentControl();
    applyTooltips();
    an::ui::processes::fixed_dialog_ui::applyStandardControlHeights(this);
    updateAutoReferenceUi(m_autoReferenceDuringRegistration);
    updateAutoSolveUi();
    updateRegistrationMethodUi();
    updateManualPointsSummary();
    updateCometTrackingSummary();
    updateChromaticAberrationSummary();
}

// Getters
int RegistrationSettingsDialog::getMinStarPairs() const
{
    return m_ui.minStarPairsSpinBox->value();
}

int RegistrationSettingsDialog::getMaxStarPairs() const
{
    return m_ui.maxStarPairsSpinBox->value();
}

QString RegistrationSettingsDialog::getReferenceImage() const
{
    return m_ui.referenceImageComboBox->currentText();
}

QString RegistrationSettingsDialog::getRegistrationMethod() const
{
    const QVariant data = m_ui.registrationMethodComboBox->currentData();
    return normalizeRegistrationMethod(data.isValid() ? data.toString()
                                                      : m_ui.registrationMethodComboBox->currentText());
}

QString RegistrationSettingsDialog::getTransformation() const
{
    const QVariant data = m_ui.transformationComboBox->currentData();
    const QString selection_key =
        data.isValid() ? data.toString() : normalizedTransformSelectionKey(m_ui.transformationComboBox->currentText());
    return baseTransformForSelectionKey(selection_key);
}

QString RegistrationSettingsDialog::getTransformMode() const
{
    const QVariant data = m_ui.transformationComboBox->currentData();
    const QString selection_key =
        data.isValid() ? data.toString() : normalizedTransformSelectionKey(m_ui.transformationComboBox->currentText());
    return transformModeForSelectionKey(selection_key);
}

int RegistrationSettingsDialog::getTransformOrder() const
{
    const QVariant data = m_ui.transformationComboBox->currentData();
    const QString selection_key =
        data.isValid() ? data.toString() : normalizedTransformSelectionKey(m_ui.transformationComboBox->currentText());
    return transformOrderForSelectionKey(selection_key);
}

bool RegistrationSettingsDialog::getTwoPassRefinementEnabled() const
{
    const QVariant data = m_ui.transformationComboBox->currentData();
    const QString selection_key =
        data.isValid() ? data.toString() : normalizedTransformSelectionKey(m_ui.transformationComboBox->currentText());
    return twoPassForSelectionKey(selection_key);
}

QString RegistrationSettingsDialog::getAlignmentMode() const
{
    if (m_ui.alignModeStandard->isChecked())
        return QStringLiteral("standard");
    else if (m_ui.alignModeMosaic->isChecked())
        return QStringLiteral("mosaic");
    else if (m_ui.alignModeIntersection->isChecked())
        return QStringLiteral("intersection");
    return QStringLiteral("standard");
}

QString RegistrationSettingsDialog::getInterpolation() const
{
    return m_ui.interpolationComboBox->currentText();
}

bool RegistrationSettingsDialog::getAutoReferenceDuringRegistration() const
{
    return m_autoReferenceDuringRegistration;
}

bool RegistrationSettingsDialog::getAutoSolveUnsolvedFrames() const
{
    return m_autoSolveUnsolvedFrames;
}

bool RegistrationSettingsDialog::getEnrichWithGaiaCatalog() const
{
    return m_enrichWithGaiaCatalog;
}

void RegistrationSettingsDialog::setEnrichWithGaiaCatalog(bool enabled)
{
    m_enrichWithGaiaCatalog = enabled;
    ensureGaiaEnrichmentControl();
    if (m_enrichWithGaiaCatalogCheckBox)
    {
        const QSignalBlocker blocker(m_enrichWithGaiaCatalogCheckBox);
        m_enrichWithGaiaCatalogCheckBox->setChecked(enabled);
    }
}

void RegistrationSettingsDialog::setMosaicSolvesUnregisteredPanels(bool solves)
{
    m_mosaicSolvesUnregisteredPanels = solves;
    setSharedToolTip(mosaicModeToolTip(), m_ui.alignModeMosaic);
    updateAutoSolveUi();
}

RegistrationSettings::ManualControlPointMap RegistrationSettingsDialog::getManualControlPointsByFrame() const
{
    return m_registrationSettings.manualControlPointsByFrame;
}

std::optional<RegistrationSettings::CometTrackingSettings> RegistrationSettingsDialog::getCometTrackingSettings() const
{
    if (!usesCometTracking(getRegistrationMethod()))
    {
        return m_registrationSettings.cometTracking;
    }

    RegistrationSettings::CometTrackingSettings settings =
        m_registrationSettings.cometTracking.value_or(RegistrationSettings::CometTrackingSettings{});
    settings.outputMode = currentCometOutputMode();
    return settings;
}

RegistrationSettings::ChromaticAberrationSettings RegistrationSettingsDialog::getChromaticAberrationSettings() const
{
    RegistrationSettings::ChromaticAberrationSettings settings = m_registrationSettings.chromaticAberration;
    if (!m_chromaticAberrationEnabledCheckBox || !m_chromaticAberrationEdgeFractionSpinBox ||
        !m_chromaticAberrationTransformTypeComboBox || !m_chromaticAberrationMinMatchedStarsSpinBox ||
        !m_chromaticAberrationInterpolationComboBox)
    {
        return settings;
    }

    settings.enabled = m_chromaticAberrationEnabledCheckBox->isChecked();
    settings.edgeFraction = m_chromaticAberrationEdgeFractionSpinBox->value();
    settings.transformType = m_chromaticAberrationTransformTypeComboBox->currentText();
    settings.minMatchedStars = m_chromaticAberrationMinMatchedStarsSpinBox->value();
    settings.interpolation = m_chromaticAberrationInterpolationComboBox->currentText();
    return settings;
}

// Setters
void RegistrationSettingsDialog::setMinStarPairs(int value)
{
    m_ui.minStarPairsSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setMaxStarPairs(int value)
{
    m_ui.maxStarPairsSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setReferenceImage(const QString& image)
{
    // Try to find the image in the combo box
    int index = m_ui.referenceImageComboBox->findText(image, Qt::MatchFixedString);
    if (index >= 0)
    {
        m_ui.referenceImageComboBox->setCurrentIndex(index);
    }
    else
    {
        // If not found (image was removed from the group), fall back to first image in list
        if (m_ui.referenceImageComboBox->count() > 0)
        {
            m_ui.referenceImageComboBox->setCurrentIndex(0);
        }
    }
    updateManualPointsSummary();
}

void RegistrationSettingsDialog::setRegistrationMethod(const QString& method)
{
    const QString method_key = normalizeRegistrationMethod(method);
    const int index = m_ui.registrationMethodComboBox->findData(method_key);
    if (index >= 0)
    {
        m_ui.registrationMethodComboBox->setCurrentIndex(index);
    }
    else
    {
        m_ui.registrationMethodComboBox->setCurrentText(method_key);
    }
    updateRegistrationMethodUi();
}

void RegistrationSettingsDialog::setTransformation(const QString& transformation)
{
    setTransformContract(transformation, QStringLiteral("linear"), 0);
}

void RegistrationSettingsDialog::setTransformContract(const QString& transformation, const QString& transformMode,
                                                      const int transformOrder)
{
    const QString selection_key = ImageIntegration::RegistrationSettingsUtils::transformSelectionKey(
        transformMode, transformOrder, transformation);
    const int index = m_ui.transformationComboBox->findData(selection_key);
    if (index >= 0)
    {
        m_ui.transformationComboBox->setCurrentIndex(index);
    }
    else
    {
        m_ui.transformationComboBox->setCurrentText(baseTransformForSelectionKey(selection_key));
    }
    updateTransformUi();
}

void RegistrationSettingsDialog::setAlignmentMode(const QString& mode)
{
    if (mode == QStringLiteral("standard"))
        m_ui.alignModeStandard->setChecked(true);
    else if (mode == QStringLiteral("mosaic"))
        m_ui.alignModeMosaic->setChecked(true);
    else if (mode == QStringLiteral("intersection"))
        m_ui.alignModeIntersection->setChecked(true);
    else
        m_ui.alignModeStandard->setChecked(true);
}

void RegistrationSettingsDialog::setInterpolation(const QString& interpolation)
{
    m_ui.interpolationComboBox->setCurrentText(interpolation);
}

void RegistrationSettingsDialog::setAutoReferenceDuringRegistration(bool enabled)
{
    if (requiresFixedReference(getRegistrationMethod()))
    {
        enabled = false;
    }
    m_autoReferenceDuringRegistration = enabled;
    ensureAutoReferenceControl();
    if (m_autoReferenceDuringRegistrationCheckBox)
    {
        const QSignalBlocker blocker(m_autoReferenceDuringRegistrationCheckBox);
        m_autoReferenceDuringRegistrationCheckBox->setChecked(enabled);
    }
    updateAutoReferenceUi(enabled);
    scheduleDialogResize(this);
}

void RegistrationSettingsDialog::setAutoSolveUnsolvedFrames(bool enabled)
{
    // Kept as the group states it: without a solver the control shows it off, and the run solves nothing.
    m_autoSolveUnsolvedFrames = enabled;
    ensureAutoSolveControl();
    if (m_autoSolveUnsolvedFramesCheckBox)
    {
        const QSignalBlocker blocker(m_autoSolveUnsolvedFramesCheckBox);
        m_autoSolveUnsolvedFramesCheckBox->setChecked(m_autoSolveUnsolvedFrames);
    }
    updateAutoSolveUi();
}

void RegistrationSettingsDialog::setManualControlPointsByFrame(
    const RegistrationSettings::ManualControlPointMap& points)
{
    m_registrationSettings.manualControlPointsByFrame = points;
    updateManualPointsSummary();
}

void RegistrationSettingsDialog::setCometTrackingSettings(
    const std::optional<RegistrationSettings::CometTrackingSettings>& settings)
{
    m_registrationSettings.cometTracking = settings;
    setCometOutputMode(settings.has_value() ? settings->outputMode : QStringLiteral("Comet"));
    updateCometTrackingSummary();
}

void RegistrationSettingsDialog::setCometAutoTrackHandler(an::ui::CometTrackingAutoTrackHandler handler)
{
    m_cometAutoTrackHandler = std::move(handler);
    if (m_cometTrackingDialog != nullptr)
    {
        m_cometTrackingDialog->setAutoTrackHandler(m_cometAutoTrackHandler);
    }
}

void RegistrationSettingsDialog::setCometBlindDetectHandler(an::ui::CometTrackingBlindDetectHandler handler)
{
    m_cometBlindDetectHandler = std::move(handler);
    if (m_cometTrackingDialog != nullptr)
    {
        m_cometTrackingDialog->setBlindDetectHandler(m_cometBlindDetectHandler);
    }
}

void RegistrationSettingsDialog::setCometFramePreviewSizeProvider(
    an::ui::CometTrackingFramePreviewSizeProvider provider)
{
    m_cometFramePreviewSizeProvider = std::move(provider);
    if (m_cometTrackingDialog != nullptr)
    {
        m_cometTrackingDialog->setFramePreviewSizeProvider(m_cometFramePreviewSizeProvider);
    }
}

void RegistrationSettingsDialog::setChromaticAberrationSettings(
    const RegistrationSettings::ChromaticAberrationSettings& settings)
{
    m_registrationSettings.chromaticAberration = settings;
    if (!m_chromaticAberrationEnabledCheckBox || !m_chromaticAberrationEdgeFractionSpinBox ||
        !m_chromaticAberrationTransformTypeComboBox || !m_chromaticAberrationMinMatchedStarsSpinBox ||
        !m_chromaticAberrationInterpolationComboBox)
    {
        return;
    }

    const QSignalBlocker block_enabled(m_chromaticAberrationEnabledCheckBox);
    const QSignalBlocker block_edge(m_chromaticAberrationEdgeFractionSpinBox);
    const QSignalBlocker block_transform(m_chromaticAberrationTransformTypeComboBox);
    const QSignalBlocker block_stars(m_chromaticAberrationMinMatchedStarsSpinBox);
    const QSignalBlocker block_interp(m_chromaticAberrationInterpolationComboBox);

    m_chromaticAberrationEnabledCheckBox->setChecked(settings.enabled);
    m_chromaticAberrationEdgeFractionSpinBox->setValue(settings.edgeFraction);
    const QString transform_type =
        settings.transformType.trimmed().compare(QStringLiteral("Affine"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("Affine")
            : QStringLiteral("Similarity");
    m_chromaticAberrationTransformTypeComboBox->setCurrentText(transform_type);
    m_chromaticAberrationMinMatchedStarsSpinBox->setValue(settings.minMatchedStars);
    if (m_chromaticAberrationInterpolationComboBox->findText(settings.interpolation, Qt::MatchFixedString) >= 0)
    {
        m_chromaticAberrationInterpolationComboBox->setCurrentText(settings.interpolation);
    }
    else
    {
        m_chromaticAberrationInterpolationComboBox->setCurrentText(QStringLiteral("Lanczos4"));
    }

    updateChromaticAberrationSummary();
}

void RegistrationSettingsDialog::ensureAdvancedSettingsDialog()
{
    if (m_advancedSettingsDialog || !m_ui.advancedSettingsPanel)
    {
        return;
    }

    if (QWidget* parent_widget = m_ui.advancedSettingsPanel->parentWidget();
        parent_widget != nullptr && parent_widget->layout() != nullptr)
    {
        parent_widget->layout()->removeWidget(m_ui.advancedSettingsPanel);
    }

    const auto& t = an::ui::tokens();

    m_advancedSettingsDialog = new QDialog(this);
    m_advancedSettingsDialog->setObjectName(QStringLiteral("registrationAdvancedSettingsDialog"));
    m_advancedSettingsDialog->setWindowTitle(tr("Advanced Registration Settings"));
    m_advancedSettingsDialog->setWindowModality(Qt::WindowModal);
    m_advancedSettingsDialog->setModal(true);
    m_advancedSettingsDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto* layout = new QVBoxLayout(m_advancedSettingsDialog);
    layout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
    layout->setSpacing(t.spacing.lg);

    m_ui.advancedSettingsPanel->setParent(m_advancedSettingsDialog);
    m_ui.advancedSettingsPanel->setVisible(true);
    layout->addWidget(m_ui.advancedSettingsPanel);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, m_advancedSettingsDialog);
    buttonBox->setObjectName(QStringLiteral("advancedSettingsButtonBox"));
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, m_advancedSettingsDialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, m_advancedSettingsDialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    an::ui::processes::fixed_dialog_ui::applyStandardControlHeights(m_advancedSettingsDialog);
    m_advancedSettingsDialog->resize((std::max)(width(), 560), m_advancedSettingsDialog->sizeHint().height());

    QObject::connect(m_advancedSettingsDialog, &QDialog::finished, this, [this](int result) {
        if (result != QDialog::Accepted && m_advancedDialogSnapshot.has_value())
        {
            setRegistrationSettings(*m_advancedDialogSnapshot);
        }
        m_advancedDialogSnapshot.reset();
        scheduleDialogResize(this);
    });

    connectSectionResizeBehavior(m_ui.starDetectionGroupBox ? m_ui.starDetectionGroupBox->toggleButton() : nullptr,
                                 m_advancedSettingsDialog,
                                 m_ui.starDetectionGroupBox ? m_ui.starDetectionGroupBox->animationDuration() : 0);
    connectSectionResizeBehavior(m_ui.amplitudeRangeGroupBox ? m_ui.amplitudeRangeGroupBox->toggleButton() : nullptr,
                                 m_advancedSettingsDialog,
                                 m_ui.amplitudeRangeGroupBox ? m_ui.amplitudeRangeGroupBox->animationDuration() : 0);
    connectSectionResizeBehavior(m_ui.roundnessRangeGroupBox ? m_ui.roundnessRangeGroupBox->toggleButton() : nullptr,
                                 m_advancedSettingsDialog,
                                 m_ui.roundnessRangeGroupBox ? m_ui.roundnessRangeGroupBox->animationDuration() : 0);
    connectSectionResizeBehavior(m_ui.miscGroupBox ? m_ui.miscGroupBox->toggleButton() : nullptr,
                                 m_advancedSettingsDialog,
                                 m_ui.miscGroupBox ? m_ui.miscGroupBox->animationDuration() : 0);

    scheduleDialogResize(m_advancedSettingsDialog);
}

void RegistrationSettingsDialog::applyTooltips()
{
    applyMainControlTooltips();
    applyAdvancedControlTooltips();
    applyCometTrackingTooltips();
    applyChromaticAberrationTooltips();
}

void RegistrationSettingsDialog::applyMainControlTooltips()
{
    const QString minPairsTip = formatTooltip(
        tr("Minimum matched stars"),
        {tr("What it does:\nSets the minimum number of matched stars required before a registration solution is "
            "accepted."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets(
                 {tr("Raise it to reject weak or ambiguous matches."),
                  tr("Lower it for sparse fields, narrow-band data, or noisy frames with fewer usable stars.")}))});
    setSharedToolTip(minPairsTip, m_ui.label_minStarPairs, m_ui.minStarPairsSpinBox);

    const QString maxPairsTip = formatTooltip(
        tr("Maximum stars per frame"),
        {tr("What it does:\nSets the upper limit on the stars detected in each frame and passed into the "
            "matcher."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets({tr("Raise it in crowded fields when more candidates improve robustness."),
                                  tr("Lower it to keep matching faster and focus on the strongest stars.")}))});
    setSharedToolTip(maxPairsTip, m_ui.label_maxStarPairs, m_ui.maxStarPairsSpinBox);

    const QString manualReferenceTip = formatTooltip(
        tr("Reference image"),
        {tr("What it does:\nThis is the manual reference frame used for registration when Auto-select reference during "
            "Registration is turned off."),
         tr("Pick a frame that is:\n%1")
             .arg(tooltipBullets({tr("Sharp and well guided."), tr("Free of saturated or distorted stars."),
                                  tr("Framed near the middle of the set.")}))});
    setSharedToolTip(manualReferenceTip, m_ui.label_referenceImage, m_ui.referenceImageComboBox);

    const QString registrationMethodTip = formatTooltip(
        tr("Registration method"),
        {tr("What it does:\nChooses the registration pipeline for this group."),
         tr("Options:\n%1")
             .arg(tooltipBullets({tr("Star Matching: detects and matches stars automatically."),
                                  tr("Phase Correlation: measures translation only."),
                                  tr("Manual Points: uses the control-point pairs you define."),
                                  tr("Comet Tracking: follows a moving object, with optional star alignment.")}))});
    setSharedToolTip(registrationMethodTip, m_ui.label_registrationMethod, m_ui.registrationMethodComboBox);

    const QString transformTip = formatTooltip(
        tr("Transformation model"),
        {tr("What it does:\nSelects how flexible the geometric correction can be during star registration."),
         tr("Modes:\n%1")
             .arg(tooltipBullets(
                 {tr("Similarity: keeps the field shape rigid; use it for clean guided data that only needs shift, "
                     "rotation, and scale."),
                  tr("Affine: also allows mild shear when frames look slightly stretched."),
                  tr("Homography: handles stronger perspective-like changes and is a good general model for star "
                     "matching."),
                  tr("Polynomial 2/3/4: bends the correction for optical distortion or field curvature; higher orders "
                     "need more matched stars."),
                  tr("Auto starts with Homography and only tries polynomial refinement when it clearly improves the "
                     "star fit.")}))});
    setSharedToolTip(transformTip, m_ui.label_transformation, m_ui.transformationComboBox, m_ui.transformSummaryLabel);

    const QString manualPointsTip =
        formatTooltip(tr("Manual points editor"),
                      {tr("What it does:\nOpens the manual point editor for the current registration group."),
                       tr("Before you start:\n%1")
                           .arg(tooltipBullets({tr("Choose a fixed reference frame."),
                                                tr("Define at least three reference-to-target point pairs for every "
                                                   "frame that needs manual alignment.")}))});
    setSharedToolTip(manualPointsTip, m_ui.editManualPointsButton, m_ui.manualPointsSummaryLabel);

    const QString standardModeTip = formatTooltip(
        tr("Standard alignment"),
        {tr("What it does:\nKeeps the output canvas tied to the selected reference frame size and placement.")});
    const QString mosaicModeTip = mosaicModeToolTip();
    const QString intersectionModeTip =
        formatTooltip(tr("Intersection alignment"),
                      {tr("What it does:\nCrops the result to the area shared by every registered frame.")});
    setSharedToolTip(standardModeTip, m_ui.alignModeStandard);
    setSharedToolTip(mosaicModeTip, m_ui.alignModeMosaic);
    setSharedToolTip(intersectionModeTip, m_ui.alignModeIntersection);

    const QString interpolationTip = formatTooltip(
        tr("Interpolation"),
        {tr("What it does:\nSelects the resampling method used when pixels are remapped onto the aligned output "
            "canvas."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Auto: lets AfterNight choose the default resampling mode."),
                  tr("Nearest Neighbor: fastest, but can look blocky or jagged."),
                  tr("Bilinear: simple and balanced for general use."),
                  tr("Bicubic: smoother than Bilinear and often a good visual compromise."),
                  tr("Pixel Area Relation: averaging-style resampling that can help when scale is reduced."),
                  tr("Lanczos4: preserves fine detail well, but may create halos or ringing around strong edges."),
                  tr("Bit Exact Bilinear / Bit Exact Nearest Neighbor: deterministic variants used when exact "
                     "reproducibility matters more than appearance.")}))});
    setSharedToolTip(interpolationTip, m_ui.label_interpolation, m_ui.interpolationComboBox);

    const QString advancedButtonTip =
        formatTooltip(tr("Advanced registration settings"),
                      {tr("What it does:\nOpens a secondary dialog with advanced star-detection, CFA, and debayer "
                          "tuning controls for registration.")});
    setSharedToolTip(advancedButtonTip, m_ui.advancedSettingsButton);
}

void RegistrationSettingsDialog::applyAdvancedControlTooltips()
{
    const QString starDetectionSectionTip = formatTooltip(
        tr("Star detection"), {tr("What it does:\nContains the advanced star-detection tuning used by registration, "
                                  "auto-reference analysis, and other star-based alignment paths.")});
    if (m_ui.starDetectionGroupBox && m_ui.starDetectionGroupBox->toggleButton())
    {
        setSharedToolTip(starDetectionSectionTip, m_ui.starDetectionGroupBox,
                         m_ui.starDetectionGroupBox->toggleButton());
    }

    const QString radiusTip = formatTooltip(
        tr("Star radius"),
        {tr("What it does:\nSets the approximate PSF radius, in pixels, used while fitting stars."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets(
                 {tr("Increase it, up to %1 pixels - the largest the detector takes - for bloated or undersampled "
                     "stars.")
                      .arg(ImageIntegration::RegistrationSettingsUtils::kLargestStarSearchRadius),
                  tr("Decrease it for tight stars so nearby detections do not merge.")}))});
    setSharedToolTip(radiusTip, m_ui.label_radius, m_ui.radiusSpinBox, m_ui.radiusSlider);

    const QString thresholdTip = formatTooltip(
        tr("Detection threshold"),
        {tr("What it does:\nSets the detection threshold in sigma above the local background."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets({tr("Raise it to reject faint or noisy detections."),
                                  tr("Lower it to find dimmer stars, but expect more noise and hot pixels.")}))});
    setSharedToolTip(thresholdTip, m_ui.label_threshold, m_ui.thresholdSpinBox, m_ui.thresholdSlider);

    const QString amplitudeRangeTip =
        formatTooltip(tr("Amplitude range filter"),
                      {tr("What it does:\nEnables a normalized fitted-amplitude filter."),
                       tr("Use this when:\n%1")
                           .arg(tooltipBullets({tr("You want to reject extremely faint detections."),
                                                tr("You want to reject saturated peaks by keeping stars inside the "
                                                   "minimum/maximum amplitude window only.")}))});
    if (m_ui.amplitudeRangeGroupBox && m_ui.amplitudeRangeGroupBox->toggleButton())
    {
        setSharedToolTip(amplitudeRangeTip, m_ui.amplitudeRangeGroupBox, m_ui.amplitudeRangeGroupBox->toggleButton(),
                         m_ui.chkEnableAmplitudeRange);
    }

    const QString amplitudeMinTip =
        formatTooltip(tr("Minimum amplitude"), {tr("What it does:\nSets the minimum normalized fitted amplitude "
                                                   "accepted when amplitude range filtering is enabled.")});
    setSharedToolTip(amplitudeMinTip, m_ui.label_thresholdMinimum, m_ui.thresholdMinimumSpinBox,
                     m_ui.thresholdMinimumSlider);

    const QString amplitudeMaxTip =
        formatTooltip(tr("Maximum amplitude"),
                      {tr("What it does:\nSets the maximum normalized fitted amplitude accepted when amplitude range "
                          "filtering is enabled."),
                       tr("Adjust it when:\n%1")
                           .arg(tooltipBullets({tr("Lower it to discard saturated or clipped stellar peaks.")}))});
    setSharedToolTip(amplitudeMaxTip, m_ui.label_thresholdMaximum, m_ui.thresholdMaximumSpinBox,
                     m_ui.thresholdMaximumSlider);

    const QString roundnessTip = formatTooltip(
        tr("Minimum roundness"),
        {tr("What it does:\nSets the minimum roundness accepted for a detected star."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets({tr("Raise it to reject elongated stars, trails, or distorted detections."),
                                  tr("Lower it if edge stars are naturally stretched by the optics.")}))});
    setSharedToolTip(roundnessTip, m_ui.label_roundnessThreshold, m_ui.roundnessThresholdSpinBox,
                     m_ui.roundnessThresholdSlider);

    const QString roundnessRangeTip = formatTooltip(
        tr("Roundness range"),
        {tr("What it does:\nApplies an optional upper roundness limit in addition to the main lower threshold.")});
    if (m_ui.roundnessRangeGroupBox && m_ui.roundnessRangeGroupBox->toggleButton())
    {
        setSharedToolTip(roundnessRangeTip, m_ui.roundnessRangeGroupBox, m_ui.roundnessRangeGroupBox->toggleButton(),
                         m_ui.chkEnableRoundnessRange);
    }

    const QString roundnessMaxTip = formatTooltip(
        tr("Maximum roundness"),
        {tr("What it does:\nSets the upper roundness limit used when roundness range filtering is enabled.")});
    setSharedToolTip(roundnessMaxTip, m_ui.label_roundnessMaximum, m_ui.roundnessThresholdMaximumSpinBox,
                     m_ui.roundnessThresholdMaximumSlider);

    const QString convergenceTip = formatTooltip(
        tr("Convergence passes"), {tr("What it does:\nSets the number of fitting convergence passes per star."),
                                   tr("Adjust it when:\n%1")
                                       .arg(tooltipBullets({tr("Raise it if noisy data needs more stable fits."),
                                                            tr("Keep it lower to reduce runtime.")}))});
    setSharedToolTip(convergenceTip, m_ui.label_convergence, m_ui.convergenceSpinBox);

    const QString relaxPsfTip = formatTooltip(
        tr("Relax PSF checks"), {tr("What it does:\nLoosens PSF validation rules for difficult data."),
                                 tr("Use this when:\n%1")
                                     .arg(tooltipBullets({tr("Stars are distorted or faint."),
                                                          tr("Strict profile checks reject too many detections.")}))});
    setSharedToolTip(relaxPsfTip, m_ui.relaxPSFChecksCheckBox);

    const QString profileTypeTip =
        formatTooltip(tr("Profile type"),
                      {tr("What it does:\nSelects the analytical profile used to fit detected stars."),
                       tr("Options:\n%1")
                           .arg(tooltipBullets({tr("Gaussian is fast and stable."),
                                                tr("Moffat models broader wings better on real stellar profiles.")}))});
    setSharedToolTip(profileTypeTip, m_ui.label_profileType, m_ui.profileTypeComboBox);

    const QString minimumBetaTip = formatTooltip(
        tr("Minimum beta"),
        {tr("What it does:\nSets the lower bound for the fitted Moffat beta parameter."),
         tr("Good to know:\nOnly used when the Moffat profile is selected to keep solutions physically plausible.")});
    setSharedToolTip(minimumBetaTip, m_ui.label_minimumBeta, m_ui.minimumBetaSpinBox);

    const QString miscSectionTip = formatTooltip(
        tr("CFA and debayer options"), {tr("What it does:\nContains the advanced CFA and debayer options used before "
                                           "star detection when raw Bayer data needs to be interpreted.")});
    if (m_ui.miscGroupBox && m_ui.miscGroupBox->toggleButton())
    {
        setSharedToolTip(miscSectionTip, m_ui.miscGroupBox, m_ui.miscGroupBox->toggleButton());
    }

    const QString cfaPatternTip = formatTooltip(
        tr("CFA pattern"), {tr("What it does:\nSets the Bayer pattern assumed for CFA/raw frames."),
                            tr("Use this when:\n%1")
                                .arg(tooltipBullets({tr("Leave it on Auto when metadata is present and correct."),
                                                     tr("Override it only when metadata is missing or wrong and star "
                                                        "colors or shapes look obviously incorrect.")}))});
    setSharedToolTip(cfaPatternTip, m_ui.label_cfaPattern, m_ui.cfaPatternComboBox);

    const QString debayerTip = formatTooltip(
        tr("Debayer method"),
        {tr("What it does:\nSelects the debayer algorithm used before star detection on CFA/raw frames."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Default follows the global Settings choice."), tr("Simpler methods are faster."),
                  tr("Higher-quality methods can produce cleaner star profiles on difficult raw data.")}))});
    setSharedToolTip(debayerTip, m_ui.label_debayerMethod, m_ui.debayerMethodComboBox);
}

void RegistrationSettingsDialog::applyCometTrackingTooltips()
{
    const QString cometOutputTip = formatTooltip(
        tr("Comet output"),
        {tr("What it does:\nChooses the comet-aware output mode for this registration group."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Comet only: downstream stacking uses comet-aligned products from this registration group.")}))});
    setSharedToolTip(cometOutputTip, m_cometOutputModeComboBox, m_cometOutputModeInfoLabel);

    const QString trackCometTip = formatTooltip(
        tr("Track Comet"),
        {tr("What it does:\nOpens the comet observation editor for manual marks, accepted candidates and "
            "diagnostics, and the tracking and fit options the registration runs with."),
         tr("Good to know:\n%1")
             .arg(tooltipBullets(
                 {tr("Guided auto-track is the default workflow once you mark or accept one observation."),
                  tr("Find Moving Object is optional and never accepts a track without confirmation."),
                  tr("At least two enabled observations with valid timestamps are normally required for production "
                     "tracking.")}))});
    setSharedToolTip(trackCometTip, m_trackCometButton);
}

void RegistrationSettingsDialog::applyChromaticAberrationTooltips()
{
    const QString chromaticEnabledTip =
        formatTooltip(tr("Chromatic aberration correction"),
                      {tr("What it does:\nCorrects red and blue channel offsets toward green before star detection and "
                          "registration."),
                       tr("Use this when:\n%1")
                           .arg(tooltipBullets({tr("Your optics leave visible chromatic fringes around stars."),
                                                tr("Blue and red stars look displaced relative to green.")}))});
    setSharedToolTip(chromaticEnabledTip, m_chromaticAberrationEnabledCheckBox);

    const QString chromaticEdgeTip = formatTooltip(
        tr("Chromatic edge fraction"),
        {tr("What it does:\nUses only stars beyond this fraction of the image radius for chromatic-aberration "
            "fitting."),
         tr("Adjust it when:\n%1")
             .arg(tooltipBullets(
                 {tr("Raise it to focus the fit on the outer field, where color fringing is often strongest."),
                  tr("Lower it if too few edge stars are available.")}))});
    setSharedToolTip(chromaticEdgeTip, m_chromaticAberrationEdgeFractionSpinBox);

    const QString chromaticTransformTip = formatTooltip(
        tr("CA fit type"),
        {tr("What it does:\nSelects the transform used to fit red and blue channel offsets relative to green."),
         tr("Options:\n%1")
             .arg(tooltipBullets(
                 {tr("Similarity handles translation, rotation, and scale."), tr("Affine also models shear.")}))});
    setSharedToolTip(chromaticTransformTip, m_chromaticAberrationTransformTypeComboBox);

    const QString chromaticStarsTip = formatTooltip(
        tr("Minimum matched stars per channel"), {tr("What it does:\nSets the minimum matched stars required per color "
                                                     "channel before chromatic-aberration correction is applied.")});
    setSharedToolTip(chromaticStarsTip, m_chromaticAberrationMinMatchedStarsSpinBox);

    const QString chromaticInterpolationTip = formatTooltip(
        tr("CA interpolation"),
        {tr("What it does:\nSelects the resampling method used when corrected red and blue channels are warped back "
            "into alignment."),
         tr("Options:\n%1")
             .arg(tooltipBullets({tr("Nearest Neighbor: fastest, but can look blocky."),
                                  tr("Bilinear: simple and balanced for general use."),
                                  tr("Bicubic: smoother than Bilinear."),
                                  tr("Lanczos4: preserves detail well, but may ring around bright edges."),
                                  tr("Pixel Area Relation: averaging-style resampling."),
                                  tr("Bit Exact Bilinear / Bit Exact Nearest Neighbor: deterministic variants for "
                                     "reproducible results.")}))});
    setSharedToolTip(chromaticInterpolationTip, m_chromaticAberrationInterpolationComboBox);
}

void RegistrationSettingsDialog::resetToDefaults()
{
    const RegistrationSettings defaults;

    // Default values for registration settings
    m_ui.minStarPairsSpinBox->setValue(defaults.minStarPairs);
    m_ui.maxStarPairsSpinBox->setValue(defaults.maxStarPairs);

    // Auto-assign reference image (first in list)
    if (!m_availableLightFrames.isEmpty())
    {
        m_ui.referenceImageComboBox->setCurrentIndex(0);
    }

    setRegistrationMethod(defaults.registrationMethod);
    setTransformContract(defaults.transformation, defaults.transformMode, defaults.transformOrder);
    setManualControlPointsByFrame({});
    setCometTrackingSettings(std::nullopt);
    setChromaticAberrationSettings(defaults.chromaticAberration);

    // Default alignment mode
    m_ui.alignModeStandard->setChecked(true);

    // Default interpolation
    m_ui.interpolationComboBox->setCurrentText(defaults.interpolation);

    // Advanced settings - Star Detection defaults
    m_ui.radiusSpinBox->setValue(5);
    m_ui.thresholdSpinBox->setValue(1.0);
    m_ui.chkEnableAmplitudeRange->setChecked(false);
    m_ui.thresholdMinimumSpinBox->setValue(0.0);
    m_ui.thresholdMaximumSpinBox->setValue(1.0);
    m_ui.roundnessThresholdSpinBox->setValue(0.5);
    m_ui.chkEnableRoundnessRange->setChecked(false);
    m_ui.roundnessThresholdMaximumSpinBox->setValue(1.0);
    m_ui.convergenceSpinBox->setValue(1);
    m_ui.relaxPSFChecksCheckBox->setChecked(false);
    m_ui.profileTypeComboBox->setCurrentText(QStringLiteral("Gaussian"));
    m_ui.minimumBetaSpinBox->setValue(1.5);

    // Advanced settings - Miscellaneous defaults
    m_ui.cfaPatternComboBox->setCurrentText(an::ui::kBayerPatternFromImageText);
    setDebayerMethod(process_utils::DEMOSAIC_DEFAULT_KEY);

    setAutoReferenceDuringRegistration(defaults.autoReferenceDuringRegistration);
    setAutoSolveUnsolvedFrames(defaults.autoSolveUnsolvedFrames);
    setEnrichWithGaiaCatalog(defaults.enrichWithGaiaCatalog);
}

void RegistrationSettingsDialog::ensureAutoReferenceControl()
{
    if (m_autoReferenceDuringRegistrationCheckBox)
    {
        return;
    }

    if (!m_ui.horizontalLayout_referenceImage || !m_ui.autoAssignButton)
    {
        return;
    }

    const int insertIndex = (std::max)(0, m_ui.horizontalLayout_referenceImage->indexOf(m_ui.autoAssignButton));
    auto* checkbox = new QCheckBox(tr("Auto-select"), m_ui.registrationGroupBox->contentWidget());
    checkbox->setObjectName(QStringLiteral("chkAutoReferenceDuringRegistration"));
    checkbox->setToolTip(formatTooltip(
        tr("Auto-select reference during Registration"),
        {tr("What it does:\nAfterNight chooses the registration reference frame automatically during execution instead "
            "of using a fixed manual frame."),
         tr("How it works:\n%1")
             .arg(tooltipNumbered({tr("Star detection"), tr("Initial registration"), tr("Frame quality lookup"),
                                   tr("Set best reference frame"), tr("Registration (final pass)")})),
         tr("Use manual selection when:\n%1")
             .arg(tooltipBullets({tr("You want the same reference every run."),
                                  tr("You are using Manual Points or Comet Tracking.")}))}));
    checkbox->setChecked(m_autoReferenceDuringRegistration);

    QObject::connect(checkbox, &QCheckBox::toggled, this,
                     [this](bool enabled) { setAutoReferenceDuringRegistration(enabled); });

    m_ui.horizontalLayout_referenceImage->insertWidget(insertIndex, checkbox);
    m_autoReferenceDuringRegistrationCheckBox = checkbox;
}

void RegistrationSettingsDialog::ensureAutoSolveControl()
{
    if (m_autoSolveUnsolvedFramesCheckBox)
    {
        return;
    }

    auto* parentLayout = qobject_cast<QVBoxLayout*>(m_ui.horizontalLayout_referenceImage->parent());
    if (!parentLayout)
    {
        return;
    }

    int insertIndex = parentLayout->count();
    if (m_autoReferenceDuringRegistrationCheckBox)
    {
        insertIndex = parentLayout->indexOf(m_autoReferenceDuringRegistrationCheckBox) + 1;
    }
    else
    {
        for (int i = 0; i < parentLayout->count(); ++i)
        {
            if (auto* item = parentLayout->itemAt(i); item && item->layout() == m_ui.horizontalLayout_referenceImage)
            {
                insertIndex = i;
                break;
            }
        }
    }

    auto* checkbox = new QCheckBox(tr("Auto-solve unsolved frames during Registration"), this);
    checkbox->setObjectName(QStringLiteral("chkAutoSolveUnsolvedFrames"));
    checkbox->setChecked(m_autoSolveUnsolvedFrames);

    QObject::connect(checkbox, &QCheckBox::toggled, this,
                     [this](bool enabled) { setAutoSolveUnsolvedFrames(enabled); });

    auto* note = new QLabel(this);
    note->setObjectName(QStringLiteral("autoSolveMosaicNote"));
    note->setWordWrap(true);
    note->setProperty("anStatusTone", QStringLiteral("secondary"));
    note->setFont(an::ui::tokens().typography.caption);
    note->setText(tr("Mosaic alignment sends every panel star matching cannot register to the configured plate solver, "
                     "so Auto-solve stays on while Mosaic is chosen."));
    note->setVisible(false);

    parentLayout->insertWidget(insertIndex, checkbox);
    parentLayout->insertWidget(insertIndex + 1, note);
    m_autoSolveUnsolvedFramesCheckBox = checkbox;
    m_autoSolveMosaicNote = note;
    // Toggled when Mosaic is chosen and when another canvas is.
    QObject::connect(m_ui.alignModeMosaic, &QAbstractButton::toggled, this, [this](bool) { updateAutoSolveUi(); });
    updateAutoSolveUi();
}

void RegistrationSettingsDialog::ensureGaiaEnrichmentControl()
{
    if (m_enrichWithGaiaCatalogCheckBox)
    {
        return;
    }

    ensureAutoSolveControl();
    auto* parentLayout = qobject_cast<QVBoxLayout*>(m_ui.horizontalLayout_referenceImage->parent());
    if (!parentLayout || !m_autoSolveMosaicNote)
    {
        return;
    }

    auto* checkbox = new QCheckBox(tr("Enrich with Gaia catalog"), this);
    checkbox->setObjectName(QStringLiteral("chkEnrichWithGaiaCatalog"));
    checkbox->setChecked(m_enrichWithGaiaCatalog);
    // Offered without a catalog too, so a group can turn off what the run cannot do there and says it cannot.
    checkbox->setToolTip(formatTooltip(
        tr("Enrich with Gaia catalog"),
        {tr("What it does:\nAfter registration, matches the stars detected in each light whose world coordinates are "
            "known against the Gaia catalog, for its photometric zero point, limiting magnitude and catalog-matched "
            "star count, which photometric weighting and spectrophotometric calibration read."),
         tr("Good to know:\n%1")
             .arg(tooltipBullets({tr("Needs a Gaia catalog installed in Settings > Plate Solving."),
                                  tr("Without one the run enriches nothing and says so once per group; turn this "
                                     "off when no catalog will be installed.")}))}));
    QObject::connect(checkbox, &QCheckBox::toggled, this, [this](bool enabled) { m_enrichWithGaiaCatalog = enabled; });

    parentLayout->insertWidget(parentLayout->indexOf(m_autoSolveMosaicNote) + 1, checkbox);
    m_enrichWithGaiaCatalogCheckBox = checkbox;
}

void RegistrationSettingsDialog::updateAutoReferenceUi(bool enabled)
{
    const QString method = normalizeRegistrationMethod(getRegistrationMethod());
    const bool phase_correlation = method == QStringLiteral("phase_correlation");
    const bool manual_points = method == QStringLiteral("manual_points");
    const bool comet_tracking = method == QStringLiteral("comet_tracking");
    const bool fixed_reference_required = phase_correlation || manual_points || comet_tracking;
    const bool auto_reference_effective = enabled && !fixed_reference_required;

    const QString autoReferenceTip = formatTooltip(
        tr("Auto-select reference during Registration"),
        {tr("What it does:\nAfterNight chooses the registration reference frame automatically during execution."),
         tr("How it works:\n%1")
             .arg(tooltipNumbered({tr("Star detection"), tr("Initial registration"), tr("Frame quality lookup"),
                                   tr("Set best reference frame"), tr("Registration (final pass)")})),
         tr("Use manual selection when:\n%1")
             .arg(tooltipBullets({tr("You need a fixed reference frame."),
                                  tr("You want the exact frame shown beside this checkbox to be used.")}))});
    const QString manualReferenceTip = formatTooltip(
        tr("Reference image"),
        {tr("What it does:\nThis is the fixed reference frame used when automatic reference selection is off."),
         tr("Pick a frame that is:\n%1")
             .arg(tooltipBullets({tr("Sharp and stable."), tr("Representative of the group framing."),
                                  tr("Free of obviously distorted stars.")}))});
    const QString autoAssignTip =
        formatTooltip(tr("Choose best manual reference"),
                      {tr("What it does:\nAnalyzes the current group and preselects the best manual reference frame "
                          "based on star quality metrics."),
                       tr("Use this when:\n%1")
                           .arg(tooltipBullets({tr("You want a fixed reference."),
                                                tr("You do not want to inspect every frame manually.")}))});

    QString disabledTip;
    if (manual_points)
    {
        disabledTip = formatTooltip(tr("Reference image"),
                                    {tr("Why it's unavailable:\nManual Points requires an explicit reference frame."),
                                     tr("What to do instead:\n%1")
                                         .arg(tooltipBullets({tr("Pick the reference manually."),
                                                              tr("Define control points for each target frame.")}))});
    }
    else if (comet_tracking)
    {
        disabledTip =
            formatTooltip(tr("Reference image"), {tr("Why it's unavailable:\nComet Tracking requires a fixed reference "
                                                     "frame so the moving-object shift can be computed against it.")});
    }
    else if (phase_correlation)
    {
        disabledTip = formatTooltip(tr("Reference image"),
                                    {tr("Why it's unavailable:\nPhase Correlation uses a fixed reference frame and "
                                        "does not support automatic reference selection during registration.")});
    }
    else
    {
        disabledTip =
            formatTooltip(tr("Auto-select reference during Registration"),
                          {tr("Why it's unavailable:\nThe Reference Image controls are disabled because Auto-select "
                              "reference during Registration is enabled."),
                           tr("To enable it:\n%1")
                               .arg(tooltipBullets({tr("Turn off Auto-select."),
                                                    tr("Then choose the registration reference frame manually.")}))});
    }

    if (m_autoReferenceDuringRegistrationCheckBox)
    {
        m_autoReferenceDuringRegistrationCheckBox->setEnabled(!fixed_reference_required);
        if (manual_points)
        {
            m_autoReferenceDuringRegistrationCheckBox->setToolTip(
                formatTooltip(tr("Auto-select reference during Registration"),
                              {tr("Why it's unavailable:\nManual Points uses a fixed reference frame and does not "
                                  "support automatic reference selection.")}));
        }
        else if (comet_tracking)
        {
            m_autoReferenceDuringRegistrationCheckBox->setToolTip(
                formatTooltip(tr("Auto-select reference during Registration"),
                              {tr("Why it's unavailable:\nComet Tracking uses a fixed reference frame and does not "
                                  "support automatic reference selection.")}));
        }
        else if (phase_correlation)
        {
            m_autoReferenceDuringRegistrationCheckBox->setToolTip(
                formatTooltip(tr("Auto-select reference during Registration"),
                              {tr("Why it's unavailable:\nPhase Correlation uses a fixed reference frame and does not "
                                  "support automatic reference selection.")}));
        }
        else
        {
            m_autoReferenceDuringRegistrationCheckBox->setToolTip(autoReferenceTip);
        }
    }

    if (m_ui.label_referenceImage)
    {
        m_ui.label_referenceImage->setEnabled(!auto_reference_effective);
        if (auto_reference_effective)
        {
            m_ui.label_referenceImage->setToolTip(disabledTip);
        }
        else if (manual_points)
        {
            m_ui.label_referenceImage->setToolTip(
                formatTooltip(tr("Reference image"), {tr("What it does:\nThis is the reference frame used for all "
                                                         "manual control-point pairs in this registration group.")}));
        }
        else if (comet_tracking)
        {
            m_ui.label_referenceImage->setToolTip(
                formatTooltip(tr("Reference image"),
                              {tr("What it does:\nThis is the fixed alignment target used for comet tracking.")}));
        }
        else if (phase_correlation)
        {
            m_ui.label_referenceImage->setToolTip(
                formatTooltip(tr("Reference image"), {tr("What it does:\nThis is the fixed reference frame used "
                                                         "directly by phase correlation registration.")}));
        }
        else
        {
            m_ui.label_referenceImage->setToolTip(manualReferenceTip);
        }
    }
    if (m_ui.referenceImageComboBox)
    {
        m_ui.referenceImageComboBox->setEnabled(!auto_reference_effective);
        if (auto_reference_effective)
        {
            m_ui.referenceImageComboBox->setToolTip(disabledTip);
        }
        else if (manual_points)
        {
            m_ui.referenceImageComboBox->setToolTip(
                formatTooltip(tr("Reference image"), {tr("What it does:\nThis is the reference frame used for all "
                                                         "manual control-point pairs in this registration group.")}));
        }
        else if (comet_tracking)
        {
            m_ui.referenceImageComboBox->setToolTip(
                formatTooltip(tr("Reference image"),
                              {tr("What it does:\nThis is the fixed alignment target used for comet tracking.")}));
        }
        else if (phase_correlation)
        {
            m_ui.referenceImageComboBox->setToolTip(
                formatTooltip(tr("Reference image"), {tr("What it does:\nThis is the fixed reference frame used "
                                                         "directly by phase correlation registration.")}));
        }
        else
        {
            m_ui.referenceImageComboBox->setToolTip(manualReferenceTip);
        }
    }
    if (m_ui.autoAssignButton)
    {
        const bool enable_auto_assign = !auto_reference_effective && !manual_points && !comet_tracking;
        m_ui.autoAssignButton->setEnabled(enable_auto_assign);
        if (!enable_auto_assign)
        {
            m_ui.autoAssignButton->setToolTip(disabledTip);
        }
        else
        {
            m_ui.autoAssignButton->setToolTip(autoAssignTip);
        }
    }
}

void RegistrationSettingsDialog::updateAutoSolveUi()
{
    if (!m_autoSolveUnsolvedFramesCheckBox)
    {
        return;
    }

    const bool solverConfigured = hasConfiguredPlateSolveSolver();
    // The integration's registration solves a mosaic's unregistered panels whatever the group's choice says, so Mosaic
    // shows it on and fixed; the choice the group stores is kept, and shown again on another canvas.
    const bool mosaicSolves = m_mosaicSolvesUnregisteredPanels && m_ui.alignModeMosaic->isChecked();
    const QString enabledTip =
        formatTooltip(tr("Auto-solve unsolved frames during Registration"),
                      {tr("What it does:\nAfterNight may invoke a configured plate solver during registration for "
                          "frames that do not already contain usable WCS or astrometric metadata."),
                       tr("Good to know:\n%1")
                           .arg(tooltipBullets({tr("Existing embedded WCS is reused automatically."),
                                                tr("This is useful when star matching needs astrometric fallback."),
                                                m_mosaicSolvesUnregisteredPanels
                                                    ? tr("Mosaic alignment always sends the panels star matching "
                                                         "cannot register to the configured plate solver.")
                                                    : QString()}))});
    const QString mosaicTip = formatTooltip(
        tr("Auto-solve unsolved frames during Registration"),
        {tr("Why it's fixed:\nMosaic alignment sends every panel star matching cannot register, and the reference it "
            "is placed against when its header holds no solution, to the configured plate solver."),
         tr("To change it:\nChoose Standard or Intersection alignment; the choice stored for this group comes back.")});
    const QString disabledTip =
        formatTooltip(tr("Auto-solve unsolved frames during Registration"),
                      {tr("Why it's unavailable:\nNo plate solver is configured in Settings, so registration solves "
                          "nothing. The choice stored for this group is kept for when one is."),
                       tr("To enable it:\n%1")
                           .arg(tooltipBullets({tr("Configure ASTAP in Settings > Plate Solving / External Tools."),
                                                tr("Or configure local or online astrometry.net."),
                                                tr("Or install a Gaia catalog for the internal solver.")}))});

    {
        const QSignalBlocker blocker(m_autoSolveUnsolvedFramesCheckBox);
        m_autoSolveUnsolvedFramesCheckBox->setChecked(
            ImageIntegration::RegistrationSettingsUtils::allowsPlateSolveDuringRegistration(
                m_autoSolveUnsolvedFrames, mosaicSolves, solverConfigured));
    }
    m_autoSolveUnsolvedFramesCheckBox->setEnabled(solverConfigured && !mosaicSolves);
    m_autoSolveUnsolvedFramesCheckBox->setToolTip(!solverConfigured ? disabledTip
                                                  : mosaicSolves    ? mosaicTip
                                                                    : enabledTip);
    if (m_autoSolveMosaicNote)
    {
        const bool noted = solverConfigured && mosaicSolves;
        if (m_autoSolveMosaicNote->isHidden() == noted)
        {
            m_autoSolveMosaicNote->setVisible(noted);
            scheduleDialogResize(this);
        }
    }
}

QString RegistrationSettingsDialog::mosaicModeToolTip() const
{
    const QString does = tr("What it does:\nExpands the output canvas to include the union of all registered frames.");
    if (m_mosaicSolvesUnregisteredPanels)
    {
        return formatTooltip(tr("Mosaic alignment"),
                             {does, tr("Good to know:\nA panel star matching cannot register is placed by its world "
                                       "coordinates: its own, or the configured plate solver's, which Mosaic calls "
                                       "for it whatever Auto-solve unsolved frames says.")});
    }
    return formatTooltip(tr("Mosaic alignment"),
                         {does, tr("Good to know:\nEvery frame is placed by the transform its registration found; a "
                                   "frame is plate-solved only when Auto-solve unsolved frames is on.")});
}

void RegistrationSettingsDialog::updateRegistrationMethodUi()
{
    const QString method = getRegistrationMethod();
    const bool uses_comet_tracking = usesCometTracking(method);
    const bool uses_phase_correlation = normalizeRegistrationMethod(method) == QStringLiteral("phase_correlation");
    const bool uses_star_detection = usesStarDetection(method);
    const bool uses_transform_contract = ImageIntegration::RegistrationSettingsUtils::usesTransformContract(method);
    const bool uses_manual_points = usesManualControlPoints(method);
    const bool fixed_reference_required = uses_phase_correlation || uses_manual_points || uses_comet_tracking;
    const bool show_star_pair_controls = uses_star_detection;
    const bool show_transform_controls = uses_transform_contract;

    if (fixed_reference_required && m_autoReferenceDuringRegistration)
    {
        setAutoReferenceDuringRegistration(false);
    }

    if (fixed_reference_required && m_ui.referenceImageComboBox &&
        m_ui.referenceImageComboBox->currentText().trimmed().isEmpty())
    {
        setReferenceImage(QString());
    }

    if (m_ui.label_minStarPairs)
    {
        m_ui.label_minStarPairs->setVisible(show_star_pair_controls);
        m_ui.label_minStarPairs->setEnabled(uses_star_detection);
    }
    if (m_ui.minStarPairsSpinBox)
    {
        m_ui.minStarPairsSpinBox->setVisible(show_star_pair_controls);
        m_ui.minStarPairsSpinBox->setEnabled(uses_star_detection);
    }
    if (m_ui.label_maxStarPairs)
    {
        m_ui.label_maxStarPairs->setVisible(show_star_pair_controls);
        m_ui.label_maxStarPairs->setEnabled(uses_star_detection);
    }
    if (m_ui.maxStarPairsSpinBox)
    {
        m_ui.maxStarPairsSpinBox->setVisible(show_star_pair_controls);
        m_ui.maxStarPairsSpinBox->setEnabled(uses_star_detection);
    }
    if (m_ui.label_transformation)
    {
        m_ui.label_transformation->setVisible(show_transform_controls);
        m_ui.label_transformation->setEnabled(uses_transform_contract);
    }
    if (m_ui.transformationComboBox)
    {
        m_ui.transformationComboBox->setVisible(show_transform_controls);
        m_ui.transformationComboBox->setEnabled(uses_transform_contract);
    }
    if (m_ui.transformSummaryLabel)
    {
        m_ui.transformSummaryLabel->setVisible(show_transform_controls);
    }
    if (m_ui.starDetectionGroupBox)
    {
        m_ui.starDetectionGroupBox->setEnabled(uses_star_detection);
    }
    if (m_ui.manualPointsSummaryLabel)
    {
        m_ui.manualPointsSummaryLabel->setVisible(uses_manual_points);
    }
    if (m_ui.editManualPointsButton)
    {
        m_ui.editManualPointsButton->setVisible(uses_manual_points);
        m_ui.editManualPointsButton->setEnabled(uses_manual_points);
    }
    if (m_cometTrackingPanel)
    {
        m_cometTrackingPanel->setVisible(uses_comet_tracking);
    }
    if (m_ui.applyToAllButton)
    {
        m_ui.applyToAllButton->setEnabled(!uses_comet_tracking);
        if (uses_comet_tracking)
        {
            m_ui.applyToAllButton->setToolTip(
                tr("Comet-tracking settings are frame-specific and cannot be applied to all groups."));
        }
    }

    updateAutoReferenceUi(m_autoReferenceDuringRegistration);
    updateAutoSolveUi();
    updateManualPointsSummary();
    updateCometOutputModeUi();
    updateCometTrackingSummary();
    updateTransformUi();
    updateChromaticAberrationSummary();
    an::ui::imageintegration::applyRegistrationSettingsDialogScrollAwareMinimumWidth(
        this, m_ui.verticalLayout_main, m_ui.verticalLayout_settingsContent, m_ui.settingsScrollArea,
        m_ui.registrationGroupBox, m_ui.alignmentGroupBox, m_ui.advancedSettingsButton);
    scheduleDialogResize(this);
}

void RegistrationSettingsDialog::updateTransformUi()
{
    if (!m_ui.transformSummaryLabel)
    {
        return;
    }

    const QVariant data = m_ui.transformationComboBox->currentData();
    const QString selection_key =
        data.isValid() ? data.toString() : normalizedTransformSelectionKey(m_ui.transformationComboBox->currentText());
    const QString summary = registrationMethodSummaryText(getRegistrationMethod(), selection_key);
    m_ui.transformSummaryLabel->setText(summary);
    m_ui.transformSummaryLabel->setToolTip(summary);
}

void RegistrationSettingsDialog::updateManualPointsSummary()
{
    if (!m_ui.manualPointsSummaryLabel)
    {
        return;
    }

    if (!usesManualControlPoints(getRegistrationMethod()))
    {
        m_ui.manualPointsSummaryLabel->setText(tr("Manual control points are not used for the current method."));
        m_ui.manualPointsSummaryLabel->setToolTip(QString());
        return;
    }

    const int frame_count = m_registrationSettings.manualControlPointsByFrame.size();
    int point_count = 0;
    for (auto it = m_registrationSettings.manualControlPointsByFrame.cbegin();
         it != m_registrationSettings.manualControlPointsByFrame.cend(); ++it)
    {
        point_count += it.value().size();
    }

    const QString reference = getReferenceImage().trimmed();
    const QString reference_label = reference.isEmpty() ? tr("(not selected)") : QFileInfo(reference).fileName();
    QString summary;
    if (point_count == 0)
    {
        summary = tr("Reference: %1. No manual control points defined yet. Click \"Edit Points...\" to create 3 or "
                     "more pairs for each target frame that needs manual alignment.")
                      .arg(reference_label);
    }
    else
    {
        summary = tr("Reference: %1. %2 control point pair(s) across %3 target frame(s). Changing the reference frame "
                     "requires rechecking the stored points.")
                      .arg(reference_label)
                      .arg(point_count)
                      .arg(frame_count);
    }

    m_ui.manualPointsSummaryLabel->setText(summary);
    m_ui.manualPointsSummaryLabel->setToolTip(summary);
}

QString RegistrationSettingsDialog::currentCometOutputMode() const
{
    if (m_cometOutputModeComboBox == nullptr)
    {
        return QStringLiteral("Comet");
    }

    const QVariant data = m_cometOutputModeComboBox->currentData();
    return ImageIntegration::normalizeCometStackingMode(data.isValid() ? data.toString() : QStringLiteral("Comet"));
}

void RegistrationSettingsDialog::setCometOutputMode(const QString& mode)
{
    Q_UNUSED(mode);
    if (m_cometOutputModeComboBox == nullptr)
    {
        return;
    }

    // The only user-facing comet output mode is Comet. Existing projects may still contain the hidden experimental
    // token; opening this dialog deliberately maps it back to the supported option.
    const QString normalized = QStringLiteral("Comet");
    const int index = m_cometOutputModeComboBox->findData(normalized);
    const QSignalBlocker blocker(m_cometOutputModeComboBox);
    m_cometOutputModeComboBox->setCurrentIndex(index >= 0 ? index : 0);
    updateCometOutputModeUi();
}

void RegistrationSettingsDialog::updateCometOutputModeUi()
{
    if (m_cometOutputModeInfoLabel == nullptr)
    {
        return;
    }

    const QString detail = tr("Downstream stacking groups sharing this registration group use comet-aligned products.");
    m_cometOutputModeInfoLabel->setText(detail);
    m_cometOutputModeInfoLabel->setToolTip(detail);
}

void RegistrationSettingsDialog::updateCometTrackingSummary()
{
    if (!m_cometTrackingSummaryLabel)
    {
        return;
    }

    if (!usesCometTracking(getRegistrationMethod()))
    {
        m_cometTrackingSummaryLabel->setText(tr("Comet/Asteroid tracking is not used for the current method."));
        m_cometTrackingSummaryLabel->setToolTip(QString());
        return;
    }

    const auto settings = getCometTrackingSettings();
    if (!settings.has_value())
    {
        const QString summary = ImageIntegration::RegistrationSettingsUtils::cometTrackingTemporarilyDisabledMessage();
        m_cometTrackingSummaryLabel->setText(summary);
        m_cometTrackingSummaryLabel->setToolTip(summary);
        return;
    }

    int enabled_count = 0;
    for (const auto& observation : settings->observations)
    {
        if (observation.enabled)
        {
            ++enabled_count;
        }
    }
    // The options as the tracking runs with them, which the Track Comet dialog sets.
    const RegistrationSettings::CometTrackingOptions& tracking = settings->trackingOptions;
    const RegistrationSettings::CometFitOptions& fit = settings->fitOptions;
    const QString model = ImageIntegration::RegistrationSettingsUtils::cometFitModelKey(fit.model);
    const QString model_text = model == QStringLiteral("linear")      ? tr("linear track")
                               : model == QStringLiteral("quadratic") ? tr("quadratic preferred")
                                                                      : tr("track model chosen by the fit");
    const QString rejection_text =
        fit.robustRejection
            ? tr("outliers beyond %1 sigma rejected")
                  .arg((std::max)(fit.outlierSigma,
                                  ImageIntegration::RegistrationSettingsUtils::kSmallestCometOutlierSigma))
            : tr("no outlier rejection");
    const QString options =
        tr("Tracking: search radius %1 px, minimum SNR %2, %3 bootstrap frames; %4, %5.")
            .arg(ImageIntegration::RegistrationSettingsUtils::effectiveCometSearchRadius(tracking.searchRadiusPixels))
            .arg(tracking.minimumSnr)
            .arg((std::max)(tracking.maxBootstrapFrames, 0))
            .arg(model_text, rejection_text);
    const QString summary =
        tr("Comet tracking: %1 observation(s), %2 enabled.").arg(settings->observations.size()).arg(enabled_count) +
        QLatin1Char('\n') + options;
    const QString output_label = tr("Comet only");
    const QString details =
        tr("Comet tracking is enabled. Output: %1. Guided auto-track is the default; Find Moving Object is optional "
           "and requires accepting a candidate track.")
            .arg(output_label);
    m_cometTrackingSummaryLabel->setText(summary);
    m_cometTrackingSummaryLabel->setToolTip(details);
}

void RegistrationSettingsDialog::updateChromaticAberrationSummary()
{
    if (!m_chromaticAberrationSummaryLabel)
    {
        return;
    }

    const auto settings = getChromaticAberrationSettings();
    const bool enabled = settings.enabled;

    if (m_chromaticAberrationEdgeFractionSpinBox)
    {
        m_chromaticAberrationEdgeFractionSpinBox->setEnabled(enabled);
    }
    if (m_chromaticAberrationTransformTypeComboBox)
    {
        m_chromaticAberrationTransformTypeComboBox->setEnabled(enabled);
    }
    if (m_chromaticAberrationMinMatchedStarsSpinBox)
    {
        m_chromaticAberrationMinMatchedStarsSpinBox->setEnabled(enabled);
    }
    if (m_chromaticAberrationInterpolationComboBox)
    {
        m_chromaticAberrationInterpolationComboBox->setEnabled(enabled);
    }

    QString summary;
    if (!enabled)
    {
        summary =
            tr("Chromatic aberration correction is disabled. RGB channels are left unchanged before registration.");
    }
    else
    {
        summary = tr("Align red and blue channels toward green before registration using %1 on edge stars beyond %2 of "
                     "the image radius, requiring at least %3 matches per channel and resampling with %4.")
                      .arg(settings.transformType, QString::number(settings.edgeFraction, 'f', 2),
                           QString::number(settings.minMatchedStars), settings.interpolation);
    }

    m_chromaticAberrationSummaryLabel->setText(summary);
    m_chromaticAberrationSummaryLabel->setToolTip(summary);
}

void RegistrationSettingsDialog::on_resetButton_clicked()
{
    resetToDefaults();
}

void RegistrationSettingsDialog::on_applyToAllButton_clicked()
{
    // Emit signal to notify parent that user wants to apply to all
    applyToAllRequested();
}

void RegistrationSettingsDialog::on_editManualPointsButton_clicked()
{
    if (m_availableLightFrames.size() < 2)
    {
        ImageIntegration::detail::openNonBlockingMessageBox(
            this, QMessageBox::Information, tr("Manual Registration"),
            tr("At least two light frames are required to define manual registration points."));
        return;
    }

    QString reference = getReferenceImage().trimmed();
    if (reference.isEmpty())
    {
        setReferenceImage(QString());
        reference = getReferenceImage().trimmed();
    }

    if (reference.isEmpty())
    {
        ImageIntegration::detail::openNonBlockingMessageBox(
            this, QMessageBox::Warning, tr("Manual Registration"),
            tr("Select a reference frame before editing manual control points."));
        return;
    }

    auto* dialog = new ManualRegistrationDialog(reference, m_availableLightFrames,
                                                m_registrationSettings.manualControlPointsByFrame, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    const QPointer<RegistrationSettingsDialog> guard(this);
    QObject::connect(dialog, &QDialog::accepted, this, [guard, dialog]() {
        if (guard)
        {
            guard->setManualControlPointsByFrame(dialog->controlPointsByFrame());
        }
    });
    dialog->open();
}

void RegistrationSettingsDialog::on_trackCometButton_clicked()
{
    if (m_cometTrackingDialog == nullptr)
    {
        m_cometTrackingDialog = new an::ui::CometTrackingDialog(m_availableLightFrames, this);
        m_cometTrackingDialog->setAutoTrackHandler(m_cometAutoTrackHandler);
        m_cometTrackingDialog->setBlindDetectHandler(m_cometBlindDetectHandler);
        m_cometTrackingDialog->setFramePreviewSizeProvider(m_cometFramePreviewSizeProvider);
        QObject::connect(m_cometTrackingDialog, &QDialog::accepted, this, [this]() {
            if (m_cometTrackingDialog == nullptr)
            {
                return;
            }
            setCometTrackingSettings(m_cometTrackingDialog->cometTrackingSettings());
            updateRegistrationMethodUi();
        });
    }
    else
    {
        m_cometTrackingDialog->setAutoTrackHandler(m_cometAutoTrackHandler);
        m_cometTrackingDialog->setBlindDetectHandler(m_cometBlindDetectHandler);
        m_cometTrackingDialog->setFramePreviewSizeProvider(m_cometFramePreviewSizeProvider);
    }

    const RegistrationSettings::CometTrackingSettings settings =
        getCometTrackingSettings().value_or(RegistrationSettings::CometTrackingSettings{});
    m_cometTrackingDialog->setCometTrackingSettings(settings);
    m_cometTrackingDialog->setWindowModality(Qt::WindowModal);
    m_cometTrackingDialog->open();
    m_cometTrackingDialog->raise();
    m_cometTrackingDialog->activateWindow();
}

void RegistrationSettingsDialog::on_advancedSettingsButton_clicked()
{
    ensureAdvancedSettingsDialog();
    if (!m_advancedSettingsDialog)
    {
        return;
    }

    if (m_advancedSettingsDialog->isVisible())
    {
        m_advancedSettingsDialog->raise();
        m_advancedSettingsDialog->activateWindow();
        return;
    }

    m_advancedDialogSnapshot = currentRegistrationSettings();
    scheduleDialogResize(m_advancedSettingsDialog);
    m_advancedSettingsDialog->open();
}

namespace
{
/// The stored value while its control shows it as the control holds it - brought into the control's range, which the
/// run clamps it to as well - and the control's value otherwise: a stored radius beyond the largest, or a roundness or
/// a Moffat exponent below the smallest, is not rewritten, and the detection and its fingerprint not changed, by
/// opening and accepting the dialog.
[[nodiscard]] int storedWhileShown(const QSpinBox& control, const int stored)
{
    return std::clamp(stored, control.minimum(), control.maximum()) == control.value() ? stored : control.value();
}

[[nodiscard]] double storedWhileShown(const QDoubleSpinBox& control, const double stored)
{
    if (!std::isfinite(stored))
    {
        return control.value();
    }
    const double shown = std::clamp(stored, control.minimum(), control.maximum());
    return QString::number(shown, 'f', control.decimals()).toDouble() == control.value() ? stored : control.value();
}
} // namespace

// Advanced settings getters - Star Detection
int RegistrationSettingsDialog::getRadius() const
{
    return storedWhileShown(*m_ui.radiusSpinBox, m_registrationSettings.radius);
}

double RegistrationSettingsDialog::getThreshold() const
{
    return m_ui.thresholdSpinBox->value();
}

bool RegistrationSettingsDialog::getAmplitudeRangeEnabled() const
{
    return m_ui.chkEnableAmplitudeRange->isChecked();
}

double RegistrationSettingsDialog::getThresholdMinimum() const
{
    return m_ui.thresholdMinimumSpinBox->value();
}

double RegistrationSettingsDialog::getThresholdMaximum() const
{
    return m_ui.thresholdMaximumSpinBox->value();
}

double RegistrationSettingsDialog::getRoundnessThreshold() const
{
    return storedWhileShown(*m_ui.roundnessThresholdSpinBox, m_registrationSettings.roundnessThreshold);
}

bool RegistrationSettingsDialog::getRoundnessRangeEnabled() const
{
    return m_ui.chkEnableRoundnessRange->isChecked();
}

double RegistrationSettingsDialog::getRoundnessThresholdMaximum() const
{
    return storedWhileShown(*m_ui.roundnessThresholdMaximumSpinBox, m_registrationSettings.roundnessThresholdMaximum);
}

int RegistrationSettingsDialog::getConvergence() const
{
    return m_ui.convergenceSpinBox->value();
}

bool RegistrationSettingsDialog::getRelaxPSFChecks() const
{
    return m_ui.relaxPSFChecksCheckBox->isChecked();
}

QString RegistrationSettingsDialog::getProfileType() const
{
    return m_ui.profileTypeComboBox->currentText();
}

double RegistrationSettingsDialog::getMinimumBeta() const
{
    return storedWhileShown(*m_ui.minimumBetaSpinBox, m_registrationSettings.minimumBeta);
}

// Advanced settings getters - Miscellaneous
QString RegistrationSettingsDialog::getCFAPattern() const
{
    return m_ui.cfaPatternComboBox->currentText();
}

QString RegistrationSettingsDialog::getDebayerMethod() const
{
    const QVariant data = m_ui.debayerMethodComboBox->currentData();
    if (data.isValid() && !data.toString().isEmpty())
    {
        return data.toString();
    }
    return m_ui.debayerMethodComboBox->currentText();
}

// Advanced settings setters - Star Detection
void RegistrationSettingsDialog::setRadius(int value)
{
    m_ui.radiusSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setThreshold(double value)
{
    m_ui.thresholdSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setAmplitudeRangeEnabled(bool enabled)
{
    m_ui.chkEnableAmplitudeRange->setChecked(enabled);
}

void RegistrationSettingsDialog::setThresholdMinimum(double value)
{
    m_ui.thresholdMinimumSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setThresholdMaximum(double value)
{
    m_ui.thresholdMaximumSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setRoundnessThreshold(double value)
{
    m_ui.roundnessThresholdSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setRoundnessRangeEnabled(bool enabled)
{
    m_ui.chkEnableRoundnessRange->setChecked(enabled);
}

void RegistrationSettingsDialog::setRoundnessThresholdMaximum(double value)
{
    m_ui.roundnessThresholdMaximumSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setConvergence(int value)
{
    m_ui.convergenceSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setRelaxPSFChecks(bool enabled)
{
    m_ui.relaxPSFChecksCheckBox->setChecked(enabled);
}

void RegistrationSettingsDialog::setProfileType(const QString& type)
{
    m_ui.profileTypeComboBox->setCurrentText(type);
}

void RegistrationSettingsDialog::setMinimumBeta(double value)
{
    m_ui.minimumBetaSpinBox->setValue(value);
}

void RegistrationSettingsDialog::setCFAPattern(const QString& pattern)
{
    m_ui.cfaPatternComboBox->setCurrentText(pattern);
}

void RegistrationSettingsDialog::setDebayerMethod(const QString& method)
{
    // A method the dialog does not offer selects the default, as any text it does not hold does.
    int index = m_ui.debayerMethodComboBox->findData(method);
    if (index >= 0)
    {
        m_ui.debayerMethodComboBox->setCurrentIndex(index);
        return;
    }

    index = m_ui.debayerMethodComboBox->findText(method);
    if (index >= 0)
    {
        m_ui.debayerMethodComboBox->setCurrentIndex(index);
        return;
    }

    m_ui.debayerMethodComboBox->setCurrentIndex(0);
}

void RegistrationSettingsDialog::removeRegistrationMethod(const QString& normalizedKey)
{
    if (!m_ui.registrationMethodComboBox)
        return;

    const int index = m_ui.registrationMethodComboBox->findData(normalizedKey);
    if (index >= 0)
    {
        m_ui.registrationMethodComboBox->removeItem(index);
    }
}
