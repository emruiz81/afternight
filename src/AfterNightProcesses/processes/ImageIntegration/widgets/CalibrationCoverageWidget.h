// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file CalibrationCoverageWidget.h
 * @brief Widget for displaying calibration coverage summary for Light frames.
 */
#pragma once

#include <QWidget>
#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;

namespace ImageIntegration
{

/**
 * @class CalibrationCoverageWidget
 * @brief Displays a visual summary of calibration coverage for light frame groups.
 *
 * Shows a segmented progress bar or pie chart representing the proportion of
 * light groups that have complete, partial, or missing calibration.
 */
class CalibrationCoverageWidget : public QWidget
{
    W_OBJECT(CalibrationCoverageWidget)

public:
    explicit CalibrationCoverageWidget(IntegrationDataModel* model, QWidget* parent = nullptr);
    ~CalibrationCoverageWidget() override;

public slots:
    /**
     * @brief Refreshes the coverage data from the model.
     */
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    QSize sizeHint() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ImageIntegration
