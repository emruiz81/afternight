// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file FrameQuickActionsWidget.h
 * @brief Widget providing quick action buttons for frame operations.
 *
 * Provides one-click access to common batch operations like
 * include all, exclude all, select all, and bulk tag operations.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <memory>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"

class IntegrationDataModel;
class QUndoStack;

namespace ImageIntegration
{

/**
 * @class FrameQuickActionsWidget
 * @brief Provides quick action buttons for frame management.
 *
 * Similar to CalibrationQuickActionsWidget but focused on frame operations:
 * - Include All / Exclude All
 * - Clear Custom Tags
 */
class AFTERNIGHTPROCESSES_API FrameQuickActionsWidget : public QWidget
{
    W_OBJECT(FrameQuickActionsWidget)

public:
    explicit FrameQuickActionsWidget(FrameType frameType, IntegrationDataModel* model, QWidget* parent = nullptr,
                                     QUndoStack* undoStack = nullptr);
    ~FrameQuickActionsWidget() override;

    void setFrameType(FrameType type);
    [[nodiscard]] FrameType frameType() const;

    // Signals for actions that require external handling
    void selectAllRequested() W_SIGNAL(selectAllRequested);
    void selectNoneRequested() W_SIGNAL(selectNoneRequested);
    void invertSelectionRequested() W_SIGNAL(invertSelectionRequested);

public slots:
    /**
     * @brief Refreshes button enabled states based on model state.
     */
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
