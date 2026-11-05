// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "components/AnCollapsibleSection.h"

class CalibrationGroupTable;
class IntegrationDataModel;
class QUndoStack;
class QWidget;

class AFTERNIGHTPROCESSES_API BaseCalibrationPanel : public an::ui::AnCollapsibleSection
{
public:
    BaseCalibrationPanel(IntegrationDataModel* model, QWidget* parent = nullptr, QUndoStack* undoStack = nullptr);

protected:
    void initializePanelChrome(const QString& objectName, const QString& title);
    void bindContentVisibility();

    IntegrationDataModel* m_model = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QWidget* m_content = nullptr;
    CalibrationGroupTable* m_table = nullptr;
};
