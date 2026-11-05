// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QDialog>
#include <optional>
#include "RegistrationSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <verdigris/wobjectdefs.h>

class QCheckBox;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;
class QWidget;

class AFTERNIGHTPROCESSES_API RegistrationSettingsDialog : public QDialog
{
    W_OBJECT(RegistrationSettingsDialog)

public:
    explicit RegistrationSettingsDialog(const QString& groupLabel, const QStringList& availableLightFrames,
                                        QWidget* parent = nullptr);
    ~RegistrationSettingsDialog();

    // Set the registration group context (needed for auto-assign best feature)
    void setRegistrationGroupContext(const QString& resolution, const QString& object);

    // Set the registration settings to use for star detection parameters
    void setRegistrationSettings(const RegistrationSettings& settings);
    [[nodiscard]] RegistrationSettings getRegistrationSettings() const;

    // Getters for the selected values
    int getMinStarPairs() const;
    int getMaxStarPairs() const;
    QString getReferenceImage() const;
    QString getRegistrationMethod() const;
    QString getTransformation() const;
    QString getTransformMode() const;
    int getTransformOrder() const;
    bool getTwoPassRefinementEnabled() const;
    QString getAlignmentMode() const;
    QString getInterpolation() const;
    bool getAutoReferenceDuringRegistration() const;
    bool getAutoSolveUnsolvedFrames() const;
    bool getEnrichWithGaiaCatalog() const;
    RegistrationSettings::ManualControlPointMap getManualControlPointsByFrame() const;
    std::optional<RegistrationSettings::CometTrackingSettings> getCometTrackingSettings() const;
    RegistrationSettings::ChromaticAberrationSettings getChromaticAberrationSettings() const;

    // Advanced settings getters - Star Detection
    int getRadius() const;
    double getThreshold() const;
    bool getAmplitudeRangeEnabled() const;
    double getThresholdMinimum() const;
    double getThresholdMaximum() const;
    double getRoundnessThreshold() const;
    bool getRoundnessRangeEnabled() const;
    double getRoundnessThresholdMaximum() const;
    int getConvergence() const;
    bool getRelaxPSFChecks() const;
    QString getProfileType() const;
    double getMinimumBeta() const;

    // Advanced settings getters - Miscellaneous
    QString getCFAPattern() const;
    QString getDebayerMethod() const;

    // Setters for the values
    void setMinStarPairs(int value);
    void setMaxStarPairs(int value);
    void setReferenceImage(const QString& image);
    void setRegistrationMethod(const QString& method);
    void setTransformation(const QString& transformation);
    /// Selects the transform a stored contract names, read by its mode as the star matcher reads it; two-pass
    /// refinement follows the mode and selects nothing.
    void setTransformContract(const QString& transformation, const QString& transformMode, int transformOrder);
    void setAlignmentMode(const QString& mode);
    void setInterpolation(const QString& interpolation);
    void setAutoReferenceDuringRegistration(bool enabled);
    void setAutoSolveUnsolvedFrames(bool enabled);
    void setEnrichWithGaiaCatalog(bool enabled);
    /// Whether the run these settings are for sends a mosaic's panels its method cannot register to the plate solver
    /// whatever Auto-solve says, as the integration's registration does, so that Mosaic shows Auto-solve on and fixed.
    /// On by default; the standalone registration, which solves a frame only when asked, turns it off.
    void setMosaicSolvesUnregisteredPanels(bool solves);
    void setManualControlPointsByFrame(const RegistrationSettings::ManualControlPointMap& points);
    void setCometTrackingSettings(const std::optional<RegistrationSettings::CometTrackingSettings>& settings);
    void setCometAutoTrackHandler(an::ui::CometTrackingAutoTrackHandler handler);
    void setCometBlindDetectHandler(an::ui::CometTrackingBlindDetectHandler handler);
    void setCometFramePreviewSizeProvider(an::ui::CometTrackingFramePreviewSizeProvider provider);
    void setChromaticAberrationSettings(const RegistrationSettings::ChromaticAberrationSettings& settings);

    // Advanced settings setters - Star Detection
    void setRadius(int value);
    void setThreshold(double value);
    void setAmplitudeRangeEnabled(bool enabled);
    void setThresholdMinimum(double value);
    void setThresholdMaximum(double value);
    void setRoundnessThreshold(double value);
    void setRoundnessRangeEnabled(bool enabled);
    void setRoundnessThresholdMaximum(double value);
    void setConvergence(int value);
    void setRelaxPSFChecks(bool enabled);
    void setProfileType(const QString& type);
    void setMinimumBeta(double value);

    // Advanced settings setters - Miscellaneous
    void setCFAPattern(const QString& pattern);
    void setDebayerMethod(const QString& method);

    // Remove a registration method from the method combo box by normalized key
    void removeRegistrationMethod(const QString& normalizedKey);

    // Reset to default values
    void resetToDefaults();

    // Signals
void applyToAllRequested() W_SIGNAL(applyToAllRequested)

    private : void initialize();
    [[nodiscard]] RegistrationSettings currentRegistrationSettings() const;
    [[nodiscard]] bool hasConfiguredPlateSolveSolver() const;
    void ensureAdvancedSettingsDialog();
    void applyTooltips();
    void applyMainControlTooltips();
    void applyAdvancedControlTooltips();
    void applyCometTrackingTooltips();
    void applyChromaticAberrationTooltips();
    void on_resetButton_clicked();
    void on_applyToAllButton_clicked();
    void on_editManualPointsButton_clicked();
    void on_trackCometButton_clicked();
    void on_advancedSettingsButton_clicked();

    void syncRadiusSliderAndSpinBox();
    void syncThresholdSliderAndSpinBox();
    void syncThresholdMinimumSliderAndSpinBox();
    void syncThresholdMaximumSliderAndSpinBox();
    void syncRoundnessThresholdSliderAndSpinBox();
    void syncRoundnessThresholdMaximumSliderAndSpinBox();
    void ensureAutoReferenceControl();
    void ensureAutoSolveControl();
    void ensureGaiaEnrichmentControl();
    void updateAutoReferenceUi(bool enabled);
    void updateAutoSolveUi();
    [[nodiscard]] QString mosaicModeToolTip() const;
    void updateRegistrationMethodUi();
    void updateTransformUi();
    void updateManualPointsSummary();
    void updateCometOutputModeUi();
    void updateCometTrackingSummary();
    void updateChromaticAberrationSummary();
    [[nodiscard]] QString currentCometOutputMode() const;
    void setCometOutputMode(const QString& mode);

    QString m_groupLabel;
    QStringList m_availableLightFrames;

    // Registration group context for auto-assign
    QString m_resolution;
    QString m_object;
    RegistrationSettings m_registrationSettings;

    bool m_autoReferenceDuringRegistration = true;
    QCheckBox* m_autoReferenceDuringRegistrationCheckBox = nullptr;
    bool m_autoSolveUnsolvedFrames = false;
    QCheckBox* m_autoSolveUnsolvedFramesCheckBox = nullptr;
    // Shown while Mosaic fixes Auto-solve on, saying why.
    QLabel* m_autoSolveMosaicNote = nullptr;
    bool m_mosaicSolvesUnregisteredPanels = true;
    bool m_enrichWithGaiaCatalog = true;
    QCheckBox* m_enrichWithGaiaCatalogCheckBox = nullptr;
    QDialog* m_advancedSettingsDialog = nullptr;
    std::optional<RegistrationSettings> m_advancedDialogSnapshot;
    QWidget* m_cometTrackingPanel = nullptr;
    QComboBox* m_cometOutputModeComboBox = nullptr;
    QLabel* m_cometOutputModeInfoLabel = nullptr;
    QPushButton* m_trackCometButton = nullptr;
    an::ui::CometTrackingDialog* m_cometTrackingDialog = nullptr;
    an::ui::CometTrackingAutoTrackHandler m_cometAutoTrackHandler;
    an::ui::CometTrackingBlindDetectHandler m_cometBlindDetectHandler;
    an::ui::CometTrackingFramePreviewSizeProvider m_cometFramePreviewSizeProvider;
    QLabel* m_cometTrackingSummaryLabel = nullptr;
    QWidget* m_chromaticAberrationPanel = nullptr;
    QCheckBox* m_chromaticAberrationEnabledCheckBox = nullptr;
    QDoubleSpinBox* m_chromaticAberrationEdgeFractionSpinBox = nullptr;
    QComboBox* m_chromaticAberrationTransformTypeComboBox = nullptr;
    QSpinBox* m_chromaticAberrationMinMatchedStarsSpinBox = nullptr;
    QComboBox* m_chromaticAberrationInterpolationComboBox = nullptr;
    QLabel* m_chromaticAberrationSummaryLabel = nullptr;

public:
    an::ui::imageintegration::RegistrationSettingsDialogUiElements m_ui;
};
