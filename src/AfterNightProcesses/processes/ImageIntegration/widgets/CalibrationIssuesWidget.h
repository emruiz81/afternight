// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>

#include <memory>
#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;

namespace ImageIntegration
{

class AFTERNIGHTPROCESSES_API CalibrationIssuesWidget : public QWidget
{
    W_OBJECT(CalibrationIssuesWidget)

public:
    explicit CalibrationIssuesWidget(IntegrationDataModel* model, QWidget* parent = nullptr);
    ~CalibrationIssuesWidget() override;

public slots:
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
