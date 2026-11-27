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

#include <QRect>
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>
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
    /// A line of the figures: its label and its value as the dashboard says them, and where it draws each.
    struct FigureLine
    {
        QString label;
        QString value;
        QRect labelRect;
        QRect valueRect;
    };

    explicit FrameStatisticsDashboard(FrameType frameType, IntegrationDataModel* model, QWidget* parent = nullptr);
    ~FrameStatisticsDashboard() override;

    void setFrameType(FrameType type);
    [[nodiscard]] FrameType frameType() const;

    /// The figures as the dashboard draws them at its size, in order - the totals, the groups and, as its type has
    /// them, the masters, the exposure and the size; none while it holds no frame.
    [[nodiscard]] std::vector<FigureLine> figureLines() const;

    /// The height the dashboard takes at a width: the pie beside its figures where they fit there whole, and over them
    /// where they do not. The dashboard holds that height for its own width.
    [[nodiscard]] int heightForWidth(int width) const override;

public slots:
    /**
     * @brief Refreshes the statistics from the model.
     */
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    /// Holds the height the dashboard takes at its width.
    void keepHeightForWidth();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
