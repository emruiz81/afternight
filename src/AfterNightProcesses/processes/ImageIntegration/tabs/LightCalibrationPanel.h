// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QSet>

#include "BaseCalibrationPanel.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <verdigris/wobjectdefs.h>

class CalibrationGroupTable;
class IntegrationDataModel;
class QLabel;
class QPushButton;
class QToolButton;
class QUndoStack;
class QWidget;
struct LightCalibrationSettings;

class AFTERNIGHTPROCESSES_API LightCalibrationPanel : public BaseCalibrationPanel
{
    W_OBJECT(LightCalibrationPanel)

public:
    explicit LightCalibrationPanel(IntegrationDataModel* model, QWidget* parent = nullptr,
                                   QUndoStack* undoStack = nullptr);
    ~LightCalibrationPanel() override = default;

    void refresh();
    void setHighlightedGroupKeys(const QSet<GroupKey>& keys);

signals:
    void selectionChanged(const GroupKey& key) W_SIGNAL(selectionChanged, key);
    void goToGroupDefinitionRequested(const GroupKey& key) W_SIGNAL(goToGroupDefinitionRequested, key);

private:
    void setupUi();
    void updateTable();
    void onSmartMatchClicked();
    void onModelSelectionChanged();
    /// Opens a light group's calibration settings without a nested event loop, and saves them once accepted.
    void openCalibrationSettingsDialog(const GroupKey& groupKey, const QString& groupLabel,
                                       const LightCalibrationSettings& settings);
    QLabel* m_legendLabel = nullptr;
    QPushButton* m_calibrationSettingsButton = nullptr;
    QPushButton* m_smartMatchButton = nullptr;
    QToolButton* m_matchingButton = nullptr;
    bool m_ignoreSelectionChange = false;
};
