// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QSet>

#include "BaseCalibrationPanel.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include <verdigris/wobjectdefs.h>

class CalibrationGroupTable;
class IntegrationDataModel;
class QPushButton;
class QUndoStack;
class QWidget;

class FlatCalibrationPanel : public BaseCalibrationPanel
{
    W_OBJECT(FlatCalibrationPanel)

public:
    explicit FlatCalibrationPanel(IntegrationDataModel* model, QWidget* parent = nullptr,
                                  QUndoStack* undoStack = nullptr);
    ~FlatCalibrationPanel() override = default;

    void refresh();
    void setHighlightedGroupKeys(const QSet<GroupKey>& keys);

signals:
    void selectionChanged(const GroupKey& key) W_SIGNAL(selectionChanged, key);
    void goToGroupDefinitionRequested(const GroupKey& key) W_SIGNAL(goToGroupDefinitionRequested, key);

private:
    void setupUi();
    void updateTable();
    QPushButton* m_addButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
};
