// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <verdigris/wobjectdefs.h>
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

class QRadioButton;
class QButtonGroup;
class AFTERNIGHTPROCESSES_API TreeDisplayModeWidget : public QWidget
{
    W_OBJECT(TreeDisplayModeWidget)

public:
    explicit TreeDisplayModeWidget(QWidget* parent = nullptr);
    ~TreeDisplayModeWidget() override;

    void setModel(GroupTreeModel* model);

signals:
    void displayModeChanged(GroupTreeModel::DisplayMode mode) W_SIGNAL(displayModeChanged, mode);

private:
    void onToggled(int id, bool checked);
    void updateFromModel();

    GroupTreeModel* m_model = nullptr;
    QRadioButton* m_hierarchicalRadio = nullptr;
    QRadioButton* m_flatRadio = nullptr;
    QButtonGroup* m_group = nullptr;
};
