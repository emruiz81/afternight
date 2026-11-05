// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QSet>
#include <QTableWidget>
#include <verdigris/wobjectdefs.h>

class QEvent;

/**
 * @brief Table widget with shared calibration table styling helpers.
 */
class AFTERNIGHTPROCESSES_API CalibrationGroupTable : public QTableWidget
{
    W_OBJECT(CalibrationGroupTable)

public:
    explicit CalibrationGroupTable(QWidget* parent = nullptr);

    void setEmptyMessage(const QString& message);
    void clearEmptyMessage();

    void setHighlightedKeys(const QSet<QString>& keys);

    [[nodiscard]] bool isKeyHighlighted(const QString& key) const;

private:
    void changeEvent(QEvent* event) override;

    QSet<QString> m_highlightedKeys;
};
