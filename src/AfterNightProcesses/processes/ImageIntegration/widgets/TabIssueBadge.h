// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/ValidationResult.h"

#include <QColor>
#include <QLabel>

class QPaintEvent;
class QKeyEvent;
class QMouseEvent;

class AFTERNIGHTPROCESSES_API TabIssueBadge final : public QLabel
{
public:
    enum class Status
    {
        Warning,
        Error
    };

    explicit TabIssueBadge(QWidget* parent = nullptr);

    void setStatus(Status status);
    void refreshTheme();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void activateOwningTab();

    Status m_status = Status::Warning;
    QColor m_fillColor;
    QColor m_borderColor;
    QColor m_textColor;
    int m_radius = 4;
};

void applyBadgeSeverityStyle(QLabel* badge, ValidationIssue::Severity severity);
