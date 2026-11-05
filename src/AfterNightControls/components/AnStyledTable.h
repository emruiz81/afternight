// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QTableWidget>

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnStyledTable : public QTableWidget
{
public:
    explicit AnStyledTable(QWidget* parent = nullptr);
    ~AnStyledTable() override = default;

    void refreshTheme();
};

} // namespace an::ui
