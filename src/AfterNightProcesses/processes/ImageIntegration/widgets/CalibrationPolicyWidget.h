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

class AFTERNIGHTPROCESSES_API CalibrationPolicyWidget : public QWidget
{
    W_OBJECT(CalibrationPolicyWidget)

public:
    explicit CalibrationPolicyWidget(IntegrationDataModel* model, QWidget* parent = nullptr,
                                     QUndoStack* undoStack = nullptr);
    ~CalibrationPolicyWidget() override;

public slots:
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
