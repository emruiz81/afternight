// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file FrameStatisticsDashboard.h
 * @brief Widget displaying frame statistics with a donut chart visualization.
 *
 * Shows included/excluded frame ratio, group count, master count, and
 * frame type-specific metrics (e.g., total exposure for lights).
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <memory>
#include <verdigris/wobjectdefs.h>

#include "processes/ImageIntegration/model/FrameType.h"

class IntegrationDataModel;

namespace ImageIntegration
{

/**
 * @class FrameStatisticsDashboard
 * @brief Displays a visual summary of frame statistics for a single frame type.
 *
 * Features a donut chart showing included vs excluded frames, along with
 * key metrics like total count, group count, and masters.
 */
class AFTERNIGHTPROCESSES_API FrameStatisticsDashboard : public QWidget
{
    W_OBJECT(FrameStatisticsDashboard)

public:
    explicit FrameStatisticsDashboard(FrameType frameType, IntegrationDataModel* model, QWidget* parent = nullptr);
    ~FrameStatisticsDashboard() override;

    void setFrameType(FrameType type);
    [[nodiscard]] FrameType frameType() const;

public slots:
    /**
     * @brief Refreshes the statistics from the model.
     */
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
