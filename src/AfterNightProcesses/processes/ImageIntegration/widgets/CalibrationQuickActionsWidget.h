// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>

#include <memory>
#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;
class QUndoStack;

namespace ImageIntegration
{

class AFTERNIGHTPROCESSES_API CalibrationQuickActionsWidget : public QWidget
{
    W_OBJECT(CalibrationQuickActionsWidget)

public:
    explicit CalibrationQuickActionsWidget(IntegrationDataModel* model, QWidget* parent = nullptr,
                                           QUndoStack* undoStack = nullptr);
    ~CalibrationQuickActionsWidget() override;

public slots:
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
