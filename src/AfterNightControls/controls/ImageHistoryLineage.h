// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QDateTime>

#include <string>
#include <vector>

struct LineageEntry
{
    std::string step_name;
    std::string description;
    std::string image_name;
    QDateTime timestamp;
    std::string source_file;
};

class AFTERNIGHTCONTROLS_API ImageHistoryLineage
{
public:
    void appendEntry(const LineageEntry& entry);

    void appendFrom(const ImageHistoryLineage& other);

    [[nodiscard]] const std::vector<LineageEntry>& entries() const;

    [[nodiscard]] bool empty() const;

    [[nodiscard]] std::string formatAsText() const;

private:
    std::vector<LineageEntry> m_Entries;
};
