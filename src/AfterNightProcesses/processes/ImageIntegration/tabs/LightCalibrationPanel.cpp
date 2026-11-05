// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LightCalibrationPanel.h"

#include "BaseCalibrationPanel.h"
#include "CalibrationGroupTable.h"
#include "CalibrationPanelUtils.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/controls/LightCalibrationSettingsDialog.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "app/FileDialogUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "theme/AnDesignTokens.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTableWidgetItem>
#include <QFileInfo>
#include <QBrush>
#include <QColor>
#include <QDir>
#include <QSignalBlocker>
#include <QUndoStack>

#include <algorithm>
#include <functional>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(LightCalibrationPanel)

namespace
{
std::optional<std::pair<GroupKey, QString>> selectedGroupFromTable(const QTableWidget* table)
{
    if (!table)
    {
        return std::nullopt;
    }

    const auto selected = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty())
    {
        return std::nullopt;
    }

    const int row = selected.front().row();
    const QTableWidgetItem* item = table->item(row, 0);
    if (!item)
    {
        return std::nullopt;
    }

    const auto groupKey = GroupKey::fromString(item->data(Qt::UserRole).toString());
    if (!groupKey)
    {
        return std::nullopt;
    }

    const QString label = item->data(Qt::UserRole + 3).toString().trimmed().isEmpty()
                              ? item->text()
                              : item->data(Qt::UserRole + 3).toString();
    return std::make_pair(*groupKey, label);
}

QString yesNo(bool value)
{
    return value ? LightCalibrationPanel::tr("Yes") : LightCalibrationPanel::tr("No");
}

QString overrideTypeDisplayName(CalibrationOverrideType type)
{
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        return LightCalibrationPanel::tr("Bias");
    case CalibrationOverrideType::Dark:
        return LightCalibrationPanel::tr("Dark");
    case CalibrationOverrideType::Flat:
        return LightCalibrationPanel::tr("Flat");
    }
    return LightCalibrationPanel::tr("Calibration");
}

CalibrationOverrideType overrideTypeForMatchColumn(int column)
{
    // Column indices: 0 = group, 1 = Bias, 2 = Dark, 3 = Flat
    if (column == 2)
    {
        return CalibrationOverrideType::Dark;
    }
    if (column == 3)
    {
        return CalibrationOverrideType::Flat;
    }
    return CalibrationOverrideType::Bias;
}

QString overrideDialogTitle(CalibrationOverrideType type)
{
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        return LightCalibrationPanel::tr("Select Bias Master");
    case CalibrationOverrideType::Dark:
        return LightCalibrationPanel::tr("Select Dark Master");
    case CalibrationOverrideType::Flat:
        return LightCalibrationPanel::tr("Select Flat Master");
    }
    return LightCalibrationPanel::tr("Select Calibration Master");
}

bool isCandidateCompatible(const CalibrationGroupKey& lightCalibrationKey, CalibrationOverrideType type,
                           const GroupedFrameSet& candidateGroup, const GroupingOptions& options)
{
    if (candidateGroup.frames.empty())
    {
        return false;
    }

    switch (type)
    {
    case CalibrationOverrideType::Bias:
        return lightCalibrationKey.matchesBias(BiasGroupKey::fromFrame(candidateGroup.frames.front()), options);
    case CalibrationOverrideType::Dark:
        return lightCalibrationKey.matchesDark(DarkGroupKey::fromFrame(candidateGroup.frames.front()), options);
    case CalibrationOverrideType::Flat:
        return lightCalibrationKey.matchesFlat(FlatGroupKey::fromFrame(candidateGroup.frames.front()), options);
    }
    return false;
}

std::vector<std::pair<QString, QString>>
collectOverrideCandidates(IntegrationDataModel* model, const LightGroupKey& lightKey, CalibrationOverrideType type)
{
    std::vector<std::pair<QString, QString>> candidates;
    if (!model)
    {
        return candidates;
    }

    const GroupedFrameView* groups = nullptr;
    GroupingOptions displayOptions;

    switch (type)
    {
    case CalibrationOverrideType::Bias:
        groups = &model->biasGroups();
        displayOptions = model->groupingOptions(FrameType::Bias);
        break;
    case CalibrationOverrideType::Dark:
        groups = &model->darkGroups();
        displayOptions = model->groupingOptions(FrameType::Dark);
        break;
    case CalibrationOverrideType::Flat:
        groups = &model->flatGroups();
        displayOptions = model->groupingOptions(FrameType::Flat);
        break;
    }

    if (!groups)
    {
        return candidates;
    }

    // For override candidates, use RESOLUTION-ONLY matching.
    // This allows users to manually select ANY calibration group with the same resolution,
    // not just the ones that would auto-match based on filter/session/etc.
    const QString targetResolution = lightKey.resolution;

    FrameType groupFrameType = FrameType::Light;
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        groupFrameType = FrameType::Bias;
        break;
    case CalibrationOverrideType::Dark:
        groupFrameType = FrameType::Dark;
        break;
    case CalibrationOverrideType::Flat:
        groupFrameType = FrameType::Flat;
        break;
    }

    for (const auto& group : *groups)
    {
        if (group.frames.empty())
        {
            continue;
        }

        // Resolution-only check
        const auto& firstFrame = group.frames.front();
        const QString groupResolution = firstFrame.metadata.resolutionString();

        if (groupResolution != targetResolution)
        {
            continue;
        }

        // List each compatible group once using its canonical master path.
        // Groups are listed by their output name.
        const QString groupLabel = group.key.toDisplayString(displayOptions, groupFrameType);
        const QString masterPath = canonicalMasterPathForGroup(model, type, group);

        if (masterPath.trimmed().isEmpty())
        {
            continue;
        }

        candidates.emplace_back(groupLabel, masterPath);
    }

    // Deterministic ordering.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.first == b.first)
        {
            return a.second < b.second;
        }
        return a.first < b.first;
    });

    return candidates;
}

CalibrationMatch currentEffectiveMatch(IntegrationDataModel* model, const LightGroupKey& lightKey,
                                       CalibrationOverrideType type, std::optional<double> lightTempC = std::nullopt)
{
    if (!model)
    {
        return {};
    }

    CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                               model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
    matcher.setOverrides(model->calibrationOverrides());

    // Build matcher groups with stable/realistic master paths.
    matcher.setBiasGroups(::collectBiasGroups(model));
    matcher.setDarkGroups(::collectDarkGroups(model));
    matcher.setFlatGroups(::collectFlatGroups(model));

    switch (type)
    {
    case CalibrationOverrideType::Bias:
        return matcher.findBias(lightKey);
    case CalibrationOverrideType::Dark:
        return matcher.findDark(lightKey, lightTempC);
    case CalibrationOverrideType::Flat:
        return matcher.findFlat(lightKey);
    }

    return {};
}

void applyMatchCell(QTableWidget* table, int row, int column, const CalibrationMatch& match, bool warnOnFallback)
{
    const QString hint =
        LightCalibrationPanel::tr("Right-click to select an override (Auto-detect / Browse / Candidates).");

    auto* item = new QTableWidgetItem(calibrationMatchDisplayText(match, QStringLiteral("-")));
    item->setIcon(calibrationMatchIcon(match, warnOnFallback));

    const QString tip =
        calibrationMatchTooltip(match, LightCalibrationPanel::tr("No matching master found."), warnOnFallback);
    item->setToolTip(tip.isEmpty() ? hint : (tip + QStringLiteral("\n\n") + hint));

    // Store structured data for Smart Match.
    item->setData(Qt::UserRole + 1, static_cast<int>(match.type));
    item->setData(Qt::UserRole + 2, match.needsUserAttention);
    item->setData(Qt::UserRole + 3, match.masterPath);

    table->setItem(row, column, item);
}

void applyNotApplicableCell(QTableWidget* table, int row, int column, const QString& tooltip)
{
    if (!table)
    {
        return;
    }

    auto* item = new QTableWidgetItem(QStringLiteral("-"));
    item->setToolTip(tooltip);
    table->setItem(row, column, item);
}

void applyMissingDarkButBiasUsedCell(QTableWidget* table, int row, int column, const CalibrationMatch& biasMatch)
{
    if (!table)
    {
        return;
    }

    const QString hint =
        LightCalibrationPanel::tr("Right-click to select an override (Auto-detect / Browse / Candidates).");

    CalibrationMatch warning;
    warning.type = CalibrationMatch::MatchType::ClosestExposure; // warning icon

    auto* item = new QTableWidgetItem(QStringLiteral("-"));
    item->setIcon(calibrationMatchIcon(warning, true));
    item->setToolTip(LightCalibrationPanel::tr("No matching Dark master found. Using Bias instead.\n\nBias: %1")
                         .arg(calibrationMatchDisplayText(biasMatch, QStringLiteral("-"))) +
                     QStringLiteral("\n\n") + hint);
    item->setData(Qt::UserRole + 1, static_cast<int>(CalibrationMatch::MatchType::None));
    item->setData(Qt::UserRole + 2, true);
    table->setItem(row, column, item);
}

/// Asks for a calibration master without a nested event loop; `chosen` receives the file the person picks.
void openMasterDialog(QWidget* parent, const QString& title, std::function<void(const QString&)> chosen)
{
    const QString filter = LightCalibrationPanel::tr("Calibration Masters (*.fit *.fits);;All Files (*.*)");
    openCalibrationMasterFileDialog(parent, title, filter, std::move(chosen));
}

/// Asks for a whole number without a nested event loop; `accepted` receives it once the person accepts.
void askIntWithoutBlocking(QWidget* parent, const QString& title, const QString& label, const int value,
                           const int minimum, const int maximum, std::function<void(int)> accepted)
{
    auto* dialog = new QInputDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setInputMode(QInputDialog::IntInput);
    dialog->setIntRange(minimum, maximum);
    dialog->setIntStep(1);
    dialog->setIntValue(value);
    QObject::connect(dialog, &QInputDialog::intValueSelected, dialog,
                     [accepted = std::move(accepted)](const int selected) {
                         if (accepted)
                         {
                             accepted(selected);
                         }
                     });
    dialog->open();
}

QString existingOverridePath(IntegrationDataModel* model, const LightGroupKey& key, CalibrationOverrideType type)
{
    if (!model)
    {
        return {};
    }

    for (const auto& overrideEntry : model->calibrationOverrides())
    {
        const auto& lhs = overrideEntry.lightKey;
        const bool sameKey = lhs.resolution == key.resolution && lhs.isoGain == key.isoGain &&
                             lhs.offset == key.offset && lhs.exposureSeconds == key.exposureSeconds &&
                             lhs.filter == key.filter && lhs.objectName == key.objectName &&
                             lhs.session == key.session && lhs.customTag == key.customTag;
        if (overrideEntry.type == type && sameKey)
        {
            return overrideEntry.masterPath;
        }
    }
    return {};
}

void setCalibrationOverrideWithUndo(IntegrationDataModel* model, QUndoStack* undoStack, const LightGroupKey& key,
                                    CalibrationOverrideType type, const QString& newPath)
{
    if (!model)
    {
        return;
    }

    const QString oldPath = existingOverridePath(model, key, type);
    if (undoStack)
    {
        undoStack->push(new SetCalibrationOverrideCommand(model, key, type, oldPath, newPath));
    }
    else if (newPath.isEmpty())
    {
        model->clearCalibrationOverride(key, type);
    }
    else
    {
        model->setCalibrationOverride(key, type, newPath);
    }
}

void setPolicyWithUndo(IntegrationDataModel* model, QUndoStack* undoStack, const CalibrationMatchingPolicy& newPolicy)
{
    if (!model)
    {
        return;
    }

    const CalibrationMatchingPolicy oldPolicy = model->calibrationMatchingPolicy();
    const bool policyUnchanged = oldPolicy.allowFlatSessionFallback == newPolicy.allowFlatSessionFallback &&
                                 oldPolicy.maxFlatSessionDaysApart == newPolicy.maxFlatSessionDaysApart &&
                                 oldPolicy.preferNewerFlat == newPolicy.preferNewerFlat &&
                                 oldPolicy.allowDarkExposureFallback == newPolicy.allowDarkExposureFallback &&
                                 oldPolicy.darkExposureTolerancePercent == newPolicy.darkExposureTolerancePercent &&
                                 oldPolicy.warnOnFallback == newPolicy.warnOnFallback &&
                                 oldPolicy.subtractBiasFromDark == newPolicy.subtractBiasFromDark;
    if (policyUnchanged)
    {
        return;
    }
    if (undoStack)
    {
        undoStack->push(new SetCalibrationMatchingPolicyCommand(model, oldPolicy, newPolicy));
    }
    else
    {
        model->setCalibrationMatchingPolicy(newPolicy);
    }
}

LightCalibrationSettings settingsForGroupOrDefault(IntegrationDataModel* model, const GroupKey& key)
{
    if (!model)
    {
        return {};
    }
    if (const auto it = model->lightCalibrationSettings().find(key); it != model->lightCalibrationSettings().end())
    {
        return it->second;
    }
    return {};
}

void setLightCalibrationSettingsWithUndo(IntegrationDataModel* model, QUndoStack* undoStack, const GroupKey& key,
                                         const LightCalibrationSettings& oldSettings,
                                         const LightCalibrationSettings& newSettings)
{
    if (!model)
    {
        return;
    }

    if (undoStack)
    {
        undoStack->push(new SetLightCalibrationSettingsCommand(model, key, oldSettings, newSettings));
    }
    else
    {
        model->setLightCalibrationSettings(key, newSettings);
    }
}
} // namespace

LightCalibrationPanel::LightCalibrationPanel(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : BaseCalibrationPanel(model, parent, undoStack)
{
    initializePanelChrome(QStringLiteral("lightCalibrationPanel"), tr("Light Frame Calibration"));
    setToolTip(
        tr("Check/uncheck to show or hide this panel. Calibration settings remain active regardless of visibility."));

    setupUi();
    updateTable();
}

void LightCalibrationPanel::setupUi()
{
    m_content = contentWidget();
    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    setContentLayout(contentLayout);

    auto* footerLayout = new QHBoxLayout();

    m_matchingButton = new QToolButton(m_content);
    m_matchingButton->setObjectName(QStringLiteral("btnCalibrationMatching"));
    m_matchingButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_matchingButton->setText(tr("Matching"));
    m_matchingButton->setToolTip(tr("Configure how calibration masters are matched to each light group.\n"
                                    "These settings affect automatic fallback behavior and warnings."));
    m_matchingButton->setPopupMode(QToolButton::InstantPopup);

    auto* matchingMenu = new QMenu(m_matchingButton);

    auto* allowDarkExposureFallbackAction = matchingMenu->addAction(tr("Allow dark exposure fallback"));
    allowDarkExposureFallbackAction->setCheckable(true);
    allowDarkExposureFallbackAction->setToolTip(
        tr("If enabled, AfterNight can select the closest matching dark exposure\n"
           "when an exact match does not exist (within the configured tolerance)."));
    QObject::connect(allowDarkExposureFallbackAction, &QAction::toggled, this, [this](bool enabled) {
        if (!m_model)
        {
            return;
        }
        CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
        policy.allowDarkExposureFallback = enabled;
        setPolicyWithUndo(m_model, m_undoStack, policy);
    });

    auto* darkToleranceAction = matchingMenu->addAction(tr("Dark exposure tolerance..."));
    darkToleranceAction->setToolTip(tr("Sets the maximum allowed exposure mismatch for dark fallback matching.\n"
                                       "Tolerance is expressed as a percent of the light exposure time."));
    QObject::connect(darkToleranceAction, &QAction::triggered, this, [this]() {
        if (!m_model)
        {
            return;
        }

        askIntWithoutBlocking(this, tr("Dark Exposure Tolerance"), tr("Allowed exposure mismatch (%):"),
                              m_model->calibrationMatchingPolicy().darkExposureTolerancePercent, 0, 100,
                              [this](const int value) {
                                  if (!m_model)
                                  {
                                      return;
                                  }
                                  CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
                                  policy.darkExposureTolerancePercent = value;
                                  setPolicyWithUndo(m_model, m_undoStack, policy);
                              });
    });

    matchingMenu->addSeparator();

    auto* subtractBiasFromDarkAction = matchingMenu->addAction(tr("Advanced: Subtract bias from darks (CCD sensors)"));
    subtractBiasFromDarkAction->setCheckable(true);
    subtractBiasFromDarkAction->setToolTip(
        tr("Enable this if your dark frames contain bias signal (common for CCD, rare for CMOS). "
           "If enabled, both Dark and Bias masters will be used."));
    QObject::connect(subtractBiasFromDarkAction, &QAction::toggled, this, [this](bool enabled) {
        if (!m_model)
            return;
        CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
        policy.subtractBiasFromDark = enabled;
        setPolicyWithUndo(m_model, m_undoStack, policy);
    });

    matchingMenu->addSeparator();

    auto* allowFlatSessionFallbackAction = matchingMenu->addAction(tr("Allow flat session fallback"));
    allowFlatSessionFallbackAction->setCheckable(true);
    allowFlatSessionFallbackAction->setToolTip(
        tr("If enabled, AfterNight can reuse flats from a nearby session\n"
           "when an exact session match does not exist (within the configured limit)."));
    QObject::connect(allowFlatSessionFallbackAction, &QAction::toggled, this, [this](bool enabled) {
        if (!m_model)
        {
            return;
        }
        CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
        policy.allowFlatSessionFallback = enabled;
        setPolicyWithUndo(m_model, m_undoStack, policy);
    });

    auto* maxFlatDaysAction = matchingMenu->addAction(tr("Max flat session days apart..."));
    maxFlatDaysAction->setToolTip(tr("Sets the maximum number of days allowed between the light session\n"
                                     "and a fallback flat session."));
    QObject::connect(maxFlatDaysAction, &QAction::triggered, this, [this]() {
        if (!m_model)
        {
            return;
        }

        askIntWithoutBlocking(this, tr("Flat Session Fallback"), tr("Maximum days apart:"),
                              m_model->calibrationMatchingPolicy().maxFlatSessionDaysApart, 0, 365,
                              [this](const int value) {
                                  if (!m_model)
                                  {
                                      return;
                                  }
                                  CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
                                  policy.maxFlatSessionDaysApart = value;
                                  setPolicyWithUndo(m_model, m_undoStack, policy);
                              });
    });

    auto* preferNewerFlatAction = matchingMenu->addAction(tr("Prefer newer flats"));
    preferNewerFlatAction->setCheckable(true);
    preferNewerFlatAction->setToolTip(tr("When multiple fallback flat sessions are equally close,\n"
                                         "prefer selecting the newer session."));
    QObject::connect(preferNewerFlatAction, &QAction::toggled, this, [this](bool enabled) {
        if (!m_model)
        {
            return;
        }
        CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
        policy.preferNewerFlat = enabled;
        setPolicyWithUndo(m_model, m_undoStack, policy);
    });

    matchingMenu->addSeparator();

    auto* warnOnFallbackAction = matchingMenu->addAction(tr("Warn on fallback matches"));
    warnOnFallbackAction->setCheckable(true);
    warnOnFallbackAction->setToolTip(
        tr("If enabled, fallback matches are shown as warnings (yellow icon/badge) and listed in Issues.\n\n"
           "If disabled, fallback matches are still used, but they are presented as OK."));
    QObject::connect(warnOnFallbackAction, &QAction::toggled, this, [this](bool enabled) {
        if (!m_model)
        {
            return;
        }
        CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
        policy.warnOnFallback = enabled;
        setPolicyWithUndo(m_model, m_undoStack, policy);
    });

    matchingMenu->addSeparator();

    auto* resetPolicyAction = matchingMenu->addAction(tr("Reset matching policy to defaults"));
    resetPolicyAction->setToolTip(
        tr("Resets all matching-policy settings to their default values.\n"
           "This also clears all manual calibration overrides (including disabled calibrations)."));
    QObject::connect(resetPolicyAction, &QAction::triggered, this, [this]() {
        if (!m_model)
        {
            return;
        }

        ImageIntegration::detail::openNonBlockingQuestionBox(
            this, tr("Reset Matching Policy"),
            tr("Reset matching policy settings to defaults?\n\n"
               "This will also clear all manual calibration overrides."),
            [this](const QMessageBox::StandardButton result, bool) {
                if (result != QMessageBox::Yes || !m_model)
                {
                    return;
                }

                if (m_undoStack)
                {
                    m_undoStack->beginMacro(tr("Reset calibration matching policy"));
                    setPolicyWithUndo(m_model, m_undoStack, CalibrationMatchingPolicy{});
                    m_undoStack->push(
                        new ClearAllCalibrationOverridesCommand(m_model, m_model->calibrationOverrides()));
                    m_undoStack->endMacro();
                }
                else
                {
                    setPolicyWithUndo(m_model, nullptr, CalibrationMatchingPolicy{});
                    m_model->clearAllCalibrationOverrides();
                }
            });
    });

    auto* clearOverridesAction = matchingMenu->addAction(tr("Clear all calibration overrides"));
    clearOverridesAction->setToolTip(tr("Clears all manual Bias/Dark/Flat overrides set on light groups."));
    QObject::connect(clearOverridesAction, &QAction::triggered, this, [this]() {
        if (!m_model)
        {
            return;
        }

        ImageIntegration::detail::openNonBlockingQuestionBox(
            this, tr("Clear Overrides"), tr("Clear all manual calibration overrides for light groups?"),
            [this](const QMessageBox::StandardButton result, bool) {
                if (result != QMessageBox::Yes || !m_model)
                {
                    return;
                }

                if (m_undoStack)
                {
                    m_undoStack->push(
                        new ClearAllCalibrationOverridesCommand(m_model, m_model->calibrationOverrides()));
                }
                else
                {
                    m_model->clearAllCalibrationOverrides();
                }
            });
    });

    m_matchingButton->setMenu(matchingMenu);

    QObject::connect(
        matchingMenu, &QMenu::aboutToShow, this,
        [this, allowDarkExposureFallbackAction, darkToleranceAction, subtractBiasFromDarkAction,
         allowFlatSessionFallbackAction, maxFlatDaysAction, preferNewerFlatAction, warnOnFallbackAction]() {
            if (!m_model)
            {
                return;
            }

            const GroupingOptions darkOptions = m_model->groupingOptions(FrameType::Dark);
            const bool darkExposureApplicable = darkOptions.byExposure;
            allowDarkExposureFallbackAction->setEnabled(darkExposureApplicable);
            darkToleranceAction->setEnabled(darkExposureApplicable);
            if (!darkExposureApplicable)
            {
                allowDarkExposureFallbackAction->setToolTip(
                    tr("Not applicable: Dark grouping is not using exposure, so exposure fallback is never used.\n"
                       "Enable \"Group by exposure\" in Dark grouping options to use this."));
                darkToleranceAction->setToolTip(
                    tr("Not applicable: Dark grouping is not using exposure, so exposure fallback is never used.\n"
                       "Enable \"Group by exposure\" in Dark grouping options to use this."));
            }

            const GroupingOptions flatOptions = m_model->groupingOptions(FrameType::Flat);
            const bool flatSessionApplicable = flatOptions.bySession;
            allowFlatSessionFallbackAction->setEnabled(flatSessionApplicable);
            maxFlatDaysAction->setEnabled(flatSessionApplicable);
            preferNewerFlatAction->setEnabled(flatSessionApplicable);
            if (!flatSessionApplicable)
            {
                allowFlatSessionFallbackAction->setToolTip(
                    tr("Not applicable: Flat grouping is not using session, so session fallback is never used.\n"
                       "Enable \"Group by session\" in Flat grouping options to use this."));
                maxFlatDaysAction->setToolTip(
                    tr("Not applicable: Flat grouping is not using session, so session fallback is never used.\n"
                       "Enable \"Group by session\" in Flat grouping options to use this."));
                preferNewerFlatAction->setToolTip(
                    tr("Not applicable: Flat grouping is not using session, so session fallback is never used.\n"
                       "Enable \"Group by session\" in Flat grouping options to use this."));
            }

            const CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
            darkToleranceAction->setText(
                tr("Dark exposure tolerance... (%1%)").arg(policy.darkExposureTolerancePercent));
            maxFlatDaysAction->setText(tr("Max flat session days apart... (%1)").arg(policy.maxFlatSessionDaysApart));
            {
                const QSignalBlocker blocker(allowDarkExposureFallbackAction);
                allowDarkExposureFallbackAction->setChecked(policy.allowDarkExposureFallback);
            }
            {
                const QSignalBlocker blocker(subtractBiasFromDarkAction);
                subtractBiasFromDarkAction->setChecked(policy.subtractBiasFromDark);
            }
            {
                const QSignalBlocker blocker(allowFlatSessionFallbackAction);
                allowFlatSessionFallbackAction->setChecked(policy.allowFlatSessionFallback);
            }
            {
                const QSignalBlocker blocker(preferNewerFlatAction);
                preferNewerFlatAction->setChecked(policy.preferNewerFlat);
            }
            {
                const QSignalBlocker blocker(warnOnFallbackAction);
                warnOnFallbackAction->setChecked(policy.warnOnFallback);
            }
        });

    footerLayout->addWidget(m_matchingButton);

    m_smartMatchButton = new QPushButton(tr("Smart Match"), m_content);
    m_smartMatchButton->setObjectName(QStringLiteral("btnSmartMatch"));
    m_smartMatchButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    m_smartMatchButton->setToolTip(tr("Analyze calibration matches and highlight ambiguous/missing results"));
    QObject::connect(m_smartMatchButton, &QPushButton::clicked, this, &LightCalibrationPanel::onSmartMatchClicked);
    footerLayout->addWidget(m_smartMatchButton);

    m_calibrationSettingsButton = new QPushButton(tr("Calibration Settings..."), m_content);
    m_calibrationSettingsButton->setObjectName(QStringLiteral("btnLightCalibrationSettings"));
    m_calibrationSettingsButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_calibrationSettingsButton->setToolTip(tr("Edit calibration settings for the selected light group"));
    m_calibrationSettingsButton->setEnabled(false);
    QObject::connect(m_calibrationSettingsButton, &QPushButton::clicked, this, [this]() {
        if (!m_model || !m_table)
        {
            return;
        }

        const auto selected = selectedGroupFromTable(m_table);
        if (!selected)
        {
            return;
        }

        const auto& [groupKey, groupLabel] = *selected;
        auto settingsIt = m_model->lightCalibrationSettings().find(groupKey);
        LightCalibrationSettings settings =
            (settingsIt != m_model->lightCalibrationSettings().end()) ? settingsIt->second : LightCalibrationSettings{};

        openCalibrationSettingsDialog(groupKey, groupLabel, settings);
    });
    footerLayout->addWidget(m_calibrationSettingsButton);
    footerLayout->addStretch();

    m_table = new CalibrationGroupTable(m_content);
    m_table->setObjectName(QStringLiteral("tblLightCalibration"));
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({tr("Light Group"), tr("Bias"), tr("Dark"), tr("Flat"), tr("Cosmetic"),
                                        tr("Dark Opt"), tr("Flat Norm"), tr("Pedestal"), tr("Pre-Calibrated")});
    contentLayout->addWidget(m_table);

    m_legendLabel =
        new QLabel(tr("Tip: Hover cells for details. Right-click for overrides, settings, and navigation."), m_content);
    {
        QPalette p = m_legendLabel->palette();
        // Use PlaceholderText color for secondary text style
        p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
        m_legendLabel->setPalette(p);
    }
    contentLayout->addWidget(m_legendLabel);
    contentLayout->addLayout(footerLayout);

    bindContentVisibility();

    // Handle pre-calibrated checkbox changes
    QObject::connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!item || !m_model || m_ignoreSelectionChange)
        {
            return;
        }

        // Only handle pre-calibrated column (column 8)
        if (item->column() != 8)
        {
            return;
        }

        // Get group key from the item
        const QString groupKeyStr = item->data(Qt::UserRole).toString();
        if (groupKeyStr.isEmpty())
        {
            return;
        }

        const auto groupKey = GroupKey::fromString(groupKeyStr);
        if (!groupKey)
        {
            return;
        }

        const bool isPreCalibrated = (item->checkState() == Qt::Checked);

        // Get current settings or create default
        auto settingsIt = m_model->lightCalibrationSettings().find(*groupKey);
        LightCalibrationSettings settings =
            (settingsIt != m_model->lightCalibrationSettings().end()) ? settingsIt->second : LightCalibrationSettings{};

        // Update pre-calibrated flag
        if (settings.preCalibrated != isPreCalibrated)
        {
            const LightCalibrationSettings oldSettings = settings;
            settings.preCalibrated = isPreCalibrated;
            setLightCalibrationSettingsWithUndo(m_model, m_undoStack, *groupKey, oldSettings, settings);
            // Table will be refreshed via model signal
        }
    });

    QObject::connect(m_model, &IntegrationDataModel::selectionChanged, this,
                     &LightCalibrationPanel::onModelSelectionChanged);
    QObject::connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_ignoreSelectionChange)
            return;

        if (m_calibrationSettingsButton)
        {
            const bool hasSelection = selectedGroupFromTable(m_table).has_value();
            m_calibrationSettingsButton->setEnabled(hasSelection);
            m_calibrationSettingsButton->setToolTip(hasSelection
                                                        ? tr("Edit calibration settings for the selected light group")
                                                        : tr("Select a light group to edit calibration settings."));
        }

        auto selected = m_table->selectionModel()->selectedRows();
        if (!selected.isEmpty())
        {
            int row = selected.front().row();
            QTableWidgetItem* item = m_table->item(row, 0);
            if (item)
            {
                QString keyStr = item->data(Qt::UserRole).toString();
                if (auto key = GroupKey::fromString(keyStr))
                {
                    m_model->setSelectedGroupKeys({*key});
                    emit selectionChanged(*key);
                    return;
                }
            }
        }
        m_model->setSelectedGroupKeys({});
        emit selectionChanged(GroupKey{});
    });

    QObject::connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (!m_model || !m_table)
        {
            return;
        }

        QTableWidgetItem* groupItem = m_table->item(row, 0);
        if (!groupItem)
        {
            return;
        }

        const QString keyStr = groupItem->data(Qt::UserRole).toString();
        const auto groupKey = GroupKey::fromString(keyStr);
        if (!groupKey)
        {
            return;
        }

        // Match cells (Bias/Dark/Flat).
        if (column >= 1 && column <= 3)
        {
            const auto lightKey = lightKeyForGroup(m_model, *groupKey);
            if (!lightKey)
            {
                return;
            }

            const CalibrationOverrideType type = overrideTypeForMatchColumn(column);
            const QString title = overrideDialogTitle(type);

            openMasterDialog(this, title, [this, key = *lightKey, type](const QString& path) {
                if (m_model)
                {
                    setCalibrationOverrideWithUndo(m_model, m_undoStack, key, type, path);
                }
            });
            return;
        }
    });

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_model || !m_table)
        {
            return;
        }

        QTableWidgetItem* item = m_table->itemAt(pos);
        if (!item)
        {
            return;
        }

        const int row = item->row();
        const int column = item->column();

        QTableWidgetItem* groupItem = m_table->item(row, 0);
        if (!groupItem)
        {
            return;
        }

        const auto groupKey = GroupKey::fromString(groupItem->data(Qt::UserRole).toString());
        if (!groupKey)
        {
            return;
        }

        const QString groupLabel = groupItem->data(Qt::UserRole + 3).toString().trimmed().isEmpty()
                                       ? groupItem->text()
                                       : groupItem->data(Qt::UserRole + 3).toString();

        QMenu menu(this);

        auto* calibrationSettingsAction = menu.addAction(tr("Calibration Settings..."));
        QObject::connect(calibrationSettingsAction, &QAction::triggered, this, [this, key = *groupKey, groupLabel]() {
            auto settingsIt = m_model->lightCalibrationSettings().find(key);
            LightCalibrationSettings settings = (settingsIt != m_model->lightCalibrationSettings().end())
                                                    ? settingsIt->second
                                                    : LightCalibrationSettings{};

            openCalibrationSettingsDialog(key, groupLabel, settings);
        });

        auto* goToGroupAction = menu.addAction(tr("Go to Group Definition"));
        QObject::connect(goToGroupAction, &QAction::triggered, this,
                         [this, key = *groupKey]() { emit goToGroupDefinitionRequested(key); });

        menu.addSeparator();

        if (column >= 1 && column <= 3)
        {
            const auto lightKeyOpt = lightKeyForGroup(m_model, *groupKey);
            if (lightKeyOpt)
            {
                const LightGroupKey lightKey = *lightKeyOpt;
                const CalibrationOverrideType type = overrideTypeForMatchColumn(column);
                const QString title = overrideTypeDisplayName(type);

                auto* header = menu.addAction(tr("%1 override").arg(title));
                header->setEnabled(false);

                std::optional<double> lightTempC;
                for (const auto& g : m_model->lightGroups())
                {
                    if (g.key == *groupKey)
                    {
                        lightTempC = averageTemperatureC(g.frames);
                        break;
                    }
                }

                const CalibrationMatch current = currentEffectiveMatch(m_model, lightKey, type, lightTempC);
                if (!current.masterPath.trimmed().isEmpty())
                {
                    auto* currentAction =
                        menu.addAction(tr("Current: %1").arg(QFileInfo(current.masterPath).fileName()));
                    currentAction->setEnabled(false);
                }

                menu.addSeparator();

                auto* autoDetect = menu.addAction(tr("Auto-detect"));
                QObject::connect(autoDetect, &QAction::triggered, this, [this, lightKey, type]() {
                    setCalibrationOverrideWithUndo(m_model, m_undoStack, lightKey, type, QString());
                    updateTable();
                });

                auto* browse = menu.addAction(tr("Browse file..."));
                QObject::connect(browse, &QAction::triggered, this, [this, lightKey, type]() {
                    openMasterDialog(this, overrideDialogTitle(type), [this, lightKey, type](const QString& path) {
                        if (m_model)
                        {
                            setCalibrationOverrideWithUndo(m_model, m_undoStack, lightKey, type, path);
                            updateTable();
                        }
                    });
                });

                auto* doNotUse = menu.addAction(tr("Do not use"));
                doNotUse->setToolTip(tr("Explicitly disable this calibration type for this group.\n"
                                        "No calibration master will be applied."));
                const bool isCurrentlyUnassigned = (current.type == CalibrationMatch::MatchType::ManuallyUnassigned);
                doNotUse->setCheckable(true);
                doNotUse->setChecked(isCurrentlyUnassigned);
                QObject::connect(doNotUse, &QAction::triggered, this, [this, lightKey, type]() {
                    setCalibrationOverrideWithUndo(m_model, m_undoStack, lightKey, type,
                                                   QString::fromLatin1(kUnassignedCalibrationMarker));
                    updateTable();
                });

                menu.addSeparator();

                const auto candidates = collectOverrideCandidates(m_model, lightKey, type);
                if (candidates.empty())
                {
                    auto* none = menu.addAction(tr("(No compatible masters found)"));
                    none->setEnabled(false);
                }
                else
                {
                    for (const auto& [label, masterPath] : candidates)
                    {
                        const QString display = tr("%1 - %2").arg(label, QFileInfo(masterPath).fileName());
                        auto* action = menu.addAction(display);
                        action->setCheckable(true);
                        action->setChecked(!current.masterPath.isEmpty() && current.masterPath == masterPath);
                        QObject::connect(action, &QAction::triggered, this, [this, lightKey, type, masterPath]() {
                            setCalibrationOverrideWithUndo(m_model, m_undoStack, lightKey, type, masterPath);
                            updateTable();
                        });
                    }
                }

                menu.addSeparator();
                QMenu* batchMenu = menu.addMenu(tr("Batch Actions"));

                if (!current.masterPath.isEmpty())
                {
                    const QString currentName = QFileInfo(current.masterPath).fileName();
                    auto* assignAll = batchMenu->addAction(tr("Assign '%1' to all groups").arg(currentName));
                    QObject::connect(assignAll, &QAction::triggered, this, [this, type, path = current.masterPath]() {
                        if (m_undoStack)
                        {
                            m_undoStack->beginMacro(tr("Assign calibration override to all groups"));
                        }
                        else
                        {
                            m_model->beginBatchUpdate();
                        }
                        for (const auto& g : m_model->lightGroups())
                        {
                            if (!g.frames.empty())
                            {
                                setCalibrationOverrideWithUndo(m_model, m_undoStack,
                                                               LightGroupKey::fromFrame(g.frames.front()), type, path);
                            }
                        }
                        if (m_undoStack)
                        {
                            m_undoStack->endMacro();
                        }
                        else
                        {
                            m_model->endBatchUpdate();
                        }
                    });

                    if (!lightKey.filter.isEmpty())
                    {
                        auto* assignFilter =
                            batchMenu->addAction(tr("Assign to all groups with filter '%1'").arg(lightKey.filter));
                        QObject::connect(assignFilter, &QAction::triggered, this,
                                         [this, type, path = current.masterPath, filter = lightKey.filter]() {
                                             if (m_undoStack)
                                             {
                                                 m_undoStack->beginMacro(tr("Assign calibration override by filter"));
                                             }
                                             else
                                             {
                                                 m_model->beginBatchUpdate();
                                             }
                                             for (const auto& g : m_model->lightGroups())
                                             {
                                                 if (!g.frames.empty())
                                                 {
                                                     auto key = LightGroupKey::fromFrame(g.frames.front());
                                                     if (key.filter == filter)
                                                     {
                                                         setCalibrationOverrideWithUndo(m_model, m_undoStack, key, type,
                                                                                        path);
                                                     }
                                                 }
                                             }
                                             if (m_undoStack)
                                             {
                                                 m_undoStack->endMacro();
                                             }
                                             else
                                             {
                                                 m_model->endBatchUpdate();
                                             }
                                         });
                    }
                }

                auto* clearAll =
                    batchMenu->addAction(tr("Clear %1 overrides for all groups").arg(overrideTypeDisplayName(type)));
                QObject::connect(clearAll, &QAction::triggered, this, [this, type]() {
                    if (m_undoStack)
                    {
                        m_undoStack->beginMacro(tr("Clear calibration overrides for all groups"));
                    }
                    else
                    {
                        m_model->beginBatchUpdate();
                    }
                    for (const auto& g : m_model->lightGroups())
                    {
                        if (!g.frames.empty())
                        {
                            setCalibrationOverrideWithUndo(m_model, m_undoStack,
                                                           LightGroupKey::fromFrame(g.frames.front()), type, QString());
                        }
                    }
                    if (m_undoStack)
                    {
                        m_undoStack->endMacro();
                    }
                    else
                    {
                        m_model->endBatchUpdate();
                    }
                });
            }
        }

        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
}

void LightCalibrationPanel::refresh()
{
    updateTable();
}

void LightCalibrationPanel::setHighlightedGroupKeys(const QSet<GroupKey>& keys)
{
    if (!m_table)
    {
        return;
    }

    QSet<QString> keyStrings;
    for (const auto& key : keys)
    {
        keyStrings.insert(key.toString());
    }
    m_table->setHighlightedKeys(keyStrings);
}

void LightCalibrationPanel::onModelSelectionChanged()
{
    const auto& keys = m_model->selectedGroupKeys();
    if (keys.empty())
    {
        if (m_table)
        {
            m_ignoreSelectionChange = true;
            m_table->clearSelection();
            m_ignoreSelectionChange = false;
        }
        return;
    }

    GroupKey targetKey = keys.front();
    if (m_table)
    {
        m_ignoreSelectionChange = true;
        for (int i = 0; i < m_table->rowCount(); ++i)
        {
            if (m_table->item(i, 0)->data(Qt::UserRole).toString() == targetKey.toString())
            {
                m_table->selectRow(i);
                m_table->scrollToItem(m_table->item(i, 0));
                break;
            }
        }
        m_ignoreSelectionChange = false;
    }
}

void LightCalibrationPanel::updateTable()
{
    if (!m_model || !m_table)
    {
        return;
    }

    m_table->clearEmptyMessage();

    const auto& lightGroups = m_model->lightGroups();
    int rows = 0;
    for (const auto& group : lightGroups)
    {
        if (!group.frames.empty())
        {
            ++rows;
        }
    }

    if (rows == 0)
    {
        m_table->setEmptyMessage(tr("No light groups available."));
        return;
    }

    CalibrationMatcher matcher(m_model->groupingOptions(FrameType::Bias), m_model->groupingOptions(FrameType::Dark),
                               m_model->groupingOptions(FrameType::Flat), m_model->calibrationMatchingPolicy());
    matcher.setBiasGroups(::collectBiasGroups(m_model));
    matcher.setDarkGroups(::collectDarkGroups(m_model));
    matcher.setFlatGroups(::collectFlatGroups(m_model));
    matcher.setOverrides(m_model->calibrationOverrides());

    const auto& calibrationSettingsMap = m_model->lightCalibrationSettings();

    m_table->setRowCount(rows);

    int row = 0;
    for (const auto& group : lightGroups)
    {
        if (group.frames.empty())
        {
            continue;
        }

        const LightGroupKey lightKey = LightGroupKey::fromFrame(group.frames.front());
        const std::optional<double> lightTempC = averageTemperatureC(group.frames);
        const GroupKey groupKey = group.key;
        const auto& groupingOptions = m_model->groupingOptions(FrameType::Light);
        const QString groupLabel = formatGroupLabel(group.key, tr("[Ungrouped]"), groupingOptions, FrameType::Light);

        auto* lightItem = new QTableWidgetItem(QStringLiteral("%1 (%2)").arg(groupLabel).arg(group.frames.size()));
        lightItem->setData(Qt::UserRole, groupKey.toString());
        lightItem->setData(Qt::UserRole + 3, groupLabel);
        {
            QStringList tip;
            tip << groupTooltip(FrameType::Light, groupKey, groupingOptions, group.frames);
            tip << QString();
            tip << framesCountTooltip(m_model, FrameType::Light, group.frames);
            lightItem->setToolTip(tip.join(QStringLiteral("\n")));
        }
        m_table->setItem(row, 0, lightItem);

        const CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();

        // Get calibration settings early to check pre-calibrated status
        const auto calIt = calibrationSettingsMap.find(groupKey);
        const LightCalibrationSettings calSettings =
            (calIt != calibrationSettingsMap.end()) ? calIt->second : LightCalibrationSettings{};
        const bool isPreCalibrated = calSettings.preCalibrated;

        // Pre-Calibrated tooltip suffix for all affected columns
        const QString preCalibratedNotice =
            isPreCalibrated ? tr("\n\n⚠ This group is marked as pre-calibrated. Calibration settings are disabled.")
                            : QString();

        // Helper lambda to create disabled cell for pre-calibrated groups
        auto setDisabledMatchCell = [&](int col, const QString& name) {
            auto* item = new QTableWidgetItem(QStringLiteral("-"));
            item->setForeground(QBrush(an::ui::tokens().colors.textTertiary));
            item->setToolTip(tr("Pre-calibrated: %1 assignment is disabled.").arg(name) + preCalibratedNotice);
            m_table->setItem(row, col, item);
        };

        if (isPreCalibrated)
        {
            // Show disabled cells for all calibration columns
            setDisabledMatchCell(1, tr("Bias"));
            setDisabledMatchCell(2, tr("Dark"));
            setDisabledMatchCell(3, tr("Flat"));
        }
        else
        {
            // Normal calibration matching logic
            const CalibrationMatch darkMatch = matcher.findDark(lightKey, lightTempC);
            const CalibrationMatch flatMatch = matcher.findFlat(lightKey);

            CalibrationMatch biasMatch;
            const bool useBias = lightTakesBias(darkMatch, policy.subtractBiasFromDark);
            if (useBias)
            {
                biasMatch = matcher.findBias(lightKey);
            }

            if (!useBias)
            {
                applyNotApplicableCell(m_table, row, 1,
                                       tr("Bias is not used when a Dark master is available (standard mode). Enable "
                                          "CCD mode to use both."));
            }
            else
            {
                applyMatchCell(m_table, row, 1, biasMatch, policy.warnOnFallback);
            }

            if (!policy.subtractBiasFromDark && darkMatch.type == CalibrationMatch::MatchType::None &&
                biasMatch.type != CalibrationMatch::MatchType::None)
            {
                applyMissingDarkButBiasUsedCell(m_table, row, 2, biasMatch);
            }
            else
            {
                applyMatchCell(m_table, row, 2, darkMatch, policy.warnOnFallback);
            }

            applyMatchCell(m_table, row, 3, flatMatch, policy.warnOnFallback);
        }

        QStringList calTip;
        calTip << tr("Light calibration settings (selected group)");
        calTip << QString();
        calTip << lightCalibrationSettingsReport(calSettings);
        calTip << QString();
        calTip << tr("Tip: Right-click to edit calibration settings.");
        if (isPreCalibrated)
        {
            calTip << QString();
            calTip << tr("⚠ This group is marked as pre-calibrated - no calibration will be applied.");
        }
        const QString calTooltip = calTip.join(QStringLiteral("\n"));

        auto* cosmeticItem =
            new QTableWidgetItem(isPreCalibrated ? QStringLiteral("-") : yesNo(calSettings.cosmeticCorrection));
        cosmeticItem->setToolTip(calTooltip);
        if (isPreCalibrated)
            cosmeticItem->setForeground(QBrush(an::ui::tokens().colors.textTertiary));
        m_table->setItem(row, 4, cosmeticItem);

        auto* darkOptItem =
            new QTableWidgetItem(isPreCalibrated ? QStringLiteral("-") : yesNo(calSettings.darkOptimization));
        darkOptItem->setToolTip(calTooltip);
        if (isPreCalibrated)
            darkOptItem->setForeground(QBrush(an::ui::tokens().colors.textTertiary));
        m_table->setItem(row, 5, darkOptItem);

        auto* flatNormItem =
            new QTableWidgetItem(isPreCalibrated ? QStringLiteral("-") : yesNo(calSettings.flatAutoLevel));
        flatNormItem->setToolTip(calTooltip);
        if (isPreCalibrated)
            flatNormItem->setForeground(QBrush(an::ui::tokens().colors.textTertiary));
        m_table->setItem(row, 6, flatNormItem);

        auto* pedestalItem =
            new QTableWidgetItem(isPreCalibrated ? QStringLiteral("-") : QString::number(calSettings.pedestal, 'f', 1));
        pedestalItem->setToolTip(calTooltip);
        if (isPreCalibrated)
            pedestalItem->setForeground(QBrush(an::ui::tokens().colors.textTertiary));
        m_table->setItem(row, 7, pedestalItem);

        // Pre-Calibrated checkbox column
        static const QString preCalibratedTooltip =
            tr("Pre-Calibrated\n\n"
               "Check this option if your light frames are already fully calibrated "
               "(e.g., from a previous processing session or another software).\n\n"
               "When enabled for this group:\n"
               "• No calibration masters (Bias, Dark, Flat) will be applied\n"
               "• No cosmetic correction, dark optimization, or flat normalization\n"
               "• No pedestal will be added\n"
               "• No calibration warnings will be shown for this group\n"
               "• \"Save calibrated frames to disk\" will skip this group\n"
               "• Disk space estimates will exclude this group's calibration output\n\n"
               "Use this when your lights have already been calibrated externally "
               "and you only want to register and stack them.");
        auto* preCalibratedItem = new QTableWidgetItem();
        preCalibratedItem->setFlags(preCalibratedItem->flags() | Qt::ItemIsUserCheckable);
        preCalibratedItem->setCheckState(isPreCalibrated ? Qt::Checked : Qt::Unchecked);
        preCalibratedItem->setToolTip(preCalibratedTooltip);
        preCalibratedItem->setData(Qt::UserRole, groupKey.toString()); // Store group key for lookup
        m_table->setItem(row, 8, preCalibratedItem);

        ++row;
    }

    m_table->resizeColumnsToContents();
}

void LightCalibrationPanel::onSmartMatchClicked()
{
    if (!m_model || !m_table)
    {
        return;
    }

    updateTable();

    int okCount = 0;
    int warnCount = 0;
    int checkCount = 0;
    int missingCount = 0;
    int overrideCount = 0;

    const bool warnOnFallback = m_model ? m_model->calibrationMatchingPolicy().warnOnFallback : true;

    int firstProblemRow = -1;

    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        bool rowHasProblem = false;
        for (int col = 1; col <= 3; ++col)
        {
            const auto* item = m_table->item(row, col);
            if (!item)
            {
                continue;
            }

            const auto type = static_cast<CalibrationMatch::MatchType>(item->data(Qt::UserRole + 1).toInt());
            const bool needsAttention = item->data(Qt::UserRole + 2).toBool();

            if (type == CalibrationMatch::MatchType::None)
            {
                ++missingCount;
                rowHasProblem = true;
            }
            else if (needsAttention)
            {
                ++checkCount;
                rowHasProblem = true;
            }
            else if (type == CalibrationMatch::MatchType::ClosestDate ||
                     type == CalibrationMatch::MatchType::ClosestExposure)
            {
                if (warnOnFallback)
                {
                    ++warnCount;
                }
                else
                {
                    ++okCount;
                }
            }
            else if (type == CalibrationMatch::MatchType::ManualOverride)
            {
                ++overrideCount;
            }
            else if (type == CalibrationMatch::MatchType::Exact)
            {
                ++okCount;
            }
        }

        if (rowHasProblem && firstProblemRow < 0)
        {
            firstProblemRow = row;
        }
    }

    if (firstProblemRow >= 0)
    {
        m_ignoreSelectionChange = true;
        m_table->selectRow(firstProblemRow);
        m_table->scrollToItem(m_table->item(firstProblemRow, 0));
        m_ignoreSelectionChange = false;
    }

    const QString summary = tr("Smart Match summary:\n"
                               "OK: %1\n"
                               "Warn (fallback): %2\n"
                               "Check (ambiguous): %3\n"
                               "Missing: %4\n"
                               "Override: %5")
                                .arg(okCount)
                                .arg(warnCount)
                                .arg(checkCount)
                                .arg(missingCount)
                                .arg(overrideCount);

    if (missingCount > 0 || checkCount > 0)
    {
        ImageIntegration::detail::openNonBlockingMessageBox(
            this, QMessageBox::Warning, tr("Smart Match"),
            summary + tr("\n\nReview rows marked 'Check' or 'Missing' and set overrides if needed."));
    }
    else
    {
        ImageIntegration::detail::openNonBlockingMessageBox(this, QMessageBox::Information, tr("Smart Match"),
                                                            summary +
                                                                tr("\n\nNo ambiguous or missing matches detected."));
    }
}

void LightCalibrationPanel::openCalibrationSettingsDialog(const GroupKey& groupKey, const QString& groupLabel,
                                                          const LightCalibrationSettings& settings)
{
    // Shown without a nested event loop: the settings are saved when the dialog is accepted.
    auto* dialog = new LightCalibrationSettingsDialog(settings, groupLabel, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog, &QDialog::accepted, this, [this, dialog, groupKey]() {
        if (!m_model)
        {
            return;
        }

        LightCalibrationSettings updated = dialog->getSettings();
        if (dialog->applyToAllGroups())
        {
            if (m_undoStack)
            {
                m_undoStack->beginMacro(tr("Update light calibration settings"));
            }
            for (const auto& group : m_model->lightGroups())
            {
                if (!group.frames.empty())
                {
                    // Preserve existing preCalibrated flag for each group
                    LightCalibrationSettings oldSettings = settingsForGroupOrDefault(m_model, group.key);
                    LightCalibrationSettings groupSettings = updated;
                    groupSettings.preCalibrated = oldSettings.preCalibrated;
                    setLightCalibrationSettingsWithUndo(m_model, m_undoStack, group.key, oldSettings, groupSettings);
                }
            }
            if (m_undoStack)
            {
                m_undoStack->endMacro();
            }
        }
        else
        {
            // Preserve existing preCalibrated flag for this group
            LightCalibrationSettings oldSettings = settingsForGroupOrDefault(m_model, groupKey);
            updated.preCalibrated = oldSettings.preCalibrated;
            setLightCalibrationSettingsWithUndo(m_model, m_undoStack, groupKey, oldSettings, updated);
        }

        updateTable();
    });
    dialog->open();
}

// Global settings button removed: use "Apply to all" in per-group settings dialog.
