// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"
#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"

#include <QColor>
#include <QIcon>
#include <QString>

#include <functional>
#include <optional>

class QWidget;
class QUndoStack;

AFTERNIGHTPROCESSES_API void applyStackingSettingsToDialog(const StackingSettings& settings,
                                                           StackingSettingsDialog& dialog);
AFTERNIGHTPROCESSES_API void applyStackingSettingsToDialog(const StackingSettings& settings,
                                                           StackingSettingsDialog& dialog,
                                                           const QString& effectiveCometStackingMode);
[[nodiscard]] AFTERNIGHTPROCESSES_API StackingSettings stackingSettingsFromDialog(const StackingSettingsDialog& dialog,
                                                                                  const StackingSettings& base);

/**
 * @brief Opens a stacking settings dialog for a calibration group and saves changes to the model.
 *
 * This helper
 * reduces code duplication across Bias/Dark/Flat calibration panels.
 * It retrieves current settings from the model,
 * displays the dialog, and saves
 * accepted changes back to the model.
 *
 * @param model The data model to read/write
 * settings from/to.
 * @param type The frame type (Bias, Dark, or Flat).
 * @param groupKey The group key to edit
 * settings for.
 * @param groupLabel Display label for the dialog title.
 * @param parent Parent widget for the
 * dialog.
 * @param undoStack The stack the change is pushed onto, or null to change the model directly.
 * @param finished Called once the dialog closes, with true when it was accepted and the settings were saved. The
 * dialog is opened without a nested event loop, so this returns before the person answers.
 */
void openStackingSettingsDialog(IntegrationDataModel* model, FrameType type, const GroupKey& groupKey,
                                const QString& groupLabel, QWidget* parent, QUndoStack* undoStack = nullptr,
                                std::function<void(bool)> finished = {});

// --- Calibration match UI helpers ---

[[nodiscard]] QString calibrationMatchDisplayText(const CalibrationMatch& match,
                                                  const QString& missingText = QStringLiteral("-"));

[[nodiscard]] QString calibrationMatchLabel(const CalibrationMatch& match, const QString& okPrefix,
                                            const QString& warnPrefix, const QString& overridePrefix,
                                            const QString& missingText, const QString& checkPrefix = QString());

[[nodiscard]] QString calibrationMatchTooltip(const CalibrationMatch& match, const QString& missingMessage,
                                              bool warnOnFallback = true);

[[nodiscard]] QColor calibrationMatchColor(const CalibrationMatch& match, bool warnOnFallback = true);

[[nodiscard]] QIcon calibrationMatchIcon(const CalibrationMatch& match, bool warnOnFallback = true);

// --- General UI helpers ---

[[nodiscard]] QIcon getAppIcon(const QString& key);

// --- Master UI helpers ---

[[nodiscard]] QString masterSourceLabel(MasterSource source);

[[nodiscard]] QString calibrationMasterOutputPath(IntegrationDataModel* model, CalibrationOverrideType type,
                                                  const GroupKey& groupKey, const GroupingOptions& options,
                                                  FrameType frameType);

/// The master a calibration group's candidate in a panel names, which an override assigned from that candidate holds:
/// the group's own supplied master, else the one the run writes for it in the output directory.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString canonicalMasterPathForGroup(IntegrationDataModel* model,
                                                                          CalibrationOverrideType type,
                                                                          const GroupedFrameSet& group);

[[nodiscard]] QString formatGroupLabel(const GroupKey& key, const QString& fallback, const GroupingOptions& options,
                                       FrameType frameType);

// --- Tooltip helpers ---

[[nodiscard]] QString groupTooltip(FrameType frameType, const GroupKey& groupKey, const GroupingOptions& options,
                                   const std::vector<FrameInfo>& frames);

[[nodiscard]] QString framesCountTooltip(IntegrationDataModel* model, FrameType frameType,
                                         const std::vector<FrameInfo>& frames);

[[nodiscard]] QString masterSourceTooltip(MasterSource source);

[[nodiscard]] AFTERNIGHTPROCESSES_API QString stackingSettingsReport(const StackingSettings& settings);

[[nodiscard]] QString stackingSettingsTooltip(IntegrationDataModel* model, FrameType frameType,
                                              const GroupKey& groupKey, const StackingSettingsMap& settings_map);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<FrameId>
bestNormalizationReferenceCandidate(IntegrationDataModel* model, const GroupKey& groupKey);

/// The frames of one light integration group, as the integration groups the session's lights.
///
/// @param model the session's model.
/// @param groupKey the group's key.
/// @return a copy of each of the group's frames; none when no light group has that key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<FrameInfo>
lightIntegrationGroupFrames(const IntegrationDataModel& model, const GroupKey& groupKey);

/// Asks for a calibration master file without a nested event loop, starting in the folder the last master was picked
/// from. `chosen` receives the file once the person picks one, after that folder is remembered; it is not called when
/// they cancel.
///
/// @param parent the dialog's parent; the dialog closes with it.
/// @param title the dialog's title.
/// @param filter the dialog's name filters.
/// @param chosen receives the picked file.
void openCalibrationMasterFileDialog(QWidget* parent, const QString& title, const QString& filter,
                                     std::function<void(const QString&)> chosen);

/// A master the user picked for a calibration tab, read as a frame on the GUI thread: its header probed in the
/// application's session only when a thread is free now, so the GUI never waits. Nothing when the file cannot be read
/// as a frame.
[[nodiscard]] std::optional<FrameInfo> readExternalMaster(const QString& path, FrameType type);

[[nodiscard]] AFTERNIGHTPROCESSES_API DrizzleAnalysis computeDrizzleAnalysis(const QString& outputDir,
                                                                             const std::vector<FrameInfo>& frames);

// --- Display formatting helpers (Calibration tables) ---

// The stacking column:
// - If stack method != "Average" OR rejection == "None": show stack method.
// - Otherwise show rejection method with parameters "(low|high)" when applicable.
[[nodiscard]] QString stackingMethodColumnText(const StackingSettings& settings);

// The normalization column:
// - If local normalization enabled: "Local Normalization"
// - Else show global normalization method (or "None")
[[nodiscard]] QString normalizationColumnText(const StackingSettings& settings);

[[nodiscard]] QString lightCalibrationSettingsReport(const LightCalibrationSettings& settings);

// --- Matcher helpers ---

[[nodiscard]] CalibrationMatcher buildConfiguredCalibrationMatcher(IntegrationDataModel* model);
std::vector<BiasCalibrationGroup> collectBiasGroups(IntegrationDataModel* model);
std::vector<DarkCalibrationGroup> collectDarkGroups(IntegrationDataModel* model);
std::vector<FlatCalibrationGroup> collectFlatGroups(IntegrationDataModel* model);

[[nodiscard]] std::optional<LightGroupKey> lightKeyForGroup(IntegrationDataModel* model, const GroupKey& groupKey);
[[nodiscard]] std::optional<LightGroupKey> lightKeyForFlatGroup(IntegrationDataModel* model, const GroupKey& groupKey);
