// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageHistoryLineage.h"

void ImageHistoryLineage::appendEntry(const LineageEntry& entry)
{
    m_Entries.push_back(entry);
}

void ImageHistoryLineage::appendFrom(const ImageHistoryLineage& other)
{
    m_Entries.insert(m_Entries.end(), other.m_Entries.begin(), other.m_Entries.end());
}

const std::vector<LineageEntry>& ImageHistoryLineage::entries() const
{
    return m_Entries;
}

bool ImageHistoryLineage::empty() const
{
    return m_Entries.empty();
}

std::string ImageHistoryLineage::formatAsText() const
{
    if (m_Entries.empty())
    {
        return {};
    }

    std::string text;
    std::string prevImageName;

    for (size_t i = 0; i < m_Entries.size(); ++i)
    {
        const auto& e = m_Entries[i];

        // Insert image header when image name changes
        if (e.image_name != prevImageName)
        {
            prevImageName = e.image_name;
            if (!text.empty())
            {
                text += "\n";
            }

            if (!e.image_name.empty())
            {
                text += fmt::format("--- Inherited from \"{}\" ---\n", e.image_name);
            }
            else
            {
                text += "--- Inherited History ---\n";
            }
        }

        text += fmt::format("  [{}] {} - {}", i + 1, e.timestamp.toString("yyyy-MM-dd hh:mm:ss").toStdString(),
                            e.step_name);

        text += "\n";

        if (!e.description.empty())
        {
            text += fmt::format("       {}\n", e.description);
        }

        if (!e.source_file.empty())
        {
            text += fmt::format("       Source file: {}\n", e.source_file);
        }
    }

    return text;
}
