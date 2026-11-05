// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file FrameGroupingIssuesWidget.h
 * @brief Widget displaying grouping-related issues for a frame type.
 *
 * Detects and displays problems like groups with insufficient frames,
 * mixed dimensions/bit depths, duplicate files, etc.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <memory>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"

class IntegrationDataModel;
struct GroupKey;

namespace ImageIntegration
{

/**
 * @class FrameGroupingIssuesWidget
 * @brief Displays detected issues with frame grouping.
 *
 * Similar to CalibrationIssuesWidget but focused on grouping-level problems:
 * - Groups with only 1 frame (insufficient for stacking)
 * - Mixed dimensions within a group
 * - High exclusion ratio warnings
 * - Duplicate file detection
 */
class AFTERNIGHTPROCESSES_API FrameGroupingIssuesWidget : public QWidget
{
    W_OBJECT(FrameGroupingIssuesWidget)

public:
    explicit FrameGroupingIssuesWidget(FrameType frameType, IntegrationDataModel* model, QWidget* parent = nullptr);
    ~FrameGroupingIssuesWidget() override;

    void setFrameType(FrameType type);
    [[nodiscard]] FrameType frameType() const;

    /**
     * @brief Signal emitted when a specific group should be shown in the tree.
     */
    void showGroupRequested(const GroupKey& key) W_SIGNAL(showGroupRequested, key);

public slots:
    /**
     * @brief Refreshes the issues from the model.
     */
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
