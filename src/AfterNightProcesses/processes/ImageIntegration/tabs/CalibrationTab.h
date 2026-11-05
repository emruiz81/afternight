// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file CalibrationTab.h
 * @brief Calibration workflow tab for Image Integration.
 *
 * Provides UI for configuring calibration frame matching (bias, dark, flat)
 * and reviewing the automatic matching results before integration.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>

#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;
class QEvent;
class QUndoStack;

/**
 * @class CalibrationTab
 * @brief Tab widget for calibration frame configuration and matching.
 *
 * Displays master frame pools (bias, dark, flat) and allows users to:
 * - Review automatically matched calibration frames
 * - Override automatic matching with manual selections
 * - Configure matching tolerance parameters
 * - View calibration coverage status per light group
 *
 * Uses pimpl idiom for ABI stability and fast compilation.
 */
class AFTERNIGHTPROCESSES_API CalibrationTab : public QWidget
{
    W_OBJECT(CalibrationTab)

public:
    explicit CalibrationTab(IntegrationDataModel* model, QWidget* parent = nullptr, QUndoStack* undoStack = nullptr);
    ~CalibrationTab() override;

public slots:
    void refreshAll();
    void showGroupInTable(FrameType type, const GroupKey& key);

signals:
    void goToGroupDefinitionRequested(FrameType type, const GroupKey& key)
        W_SIGNAL(goToGroupDefinitionRequested, type, key);

private:
    void changeEvent(QEvent* event) override;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
