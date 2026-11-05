// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>
#include <QTableWidgetItem>
#include <QVariant>
#include <Qt>

/// Table item that sorts by the numeric value stored in Qt::UserRole rather than
/// by its display text. Shared by the ImageIntegration frame-quality tables
/// (FrameQualityDashboard, RegistrationTab).
class NumericSortItem final : public QTableWidgetItem
{
public:
    NumericSortItem(const QString& text, const QVariant& sortValue) : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, sortValue);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toDouble() < other.data(Qt::UserRole).toDouble();
    }
};
