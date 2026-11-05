// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <QString>

#include <optional>

/**
 * @brief Frame tree filtering criteria for search and basic metadata filters.
 */
class AFTERNIGHTPROCESSES_API FrameTreeFilter
{
public:
    void setSearchText(const QString& text);
    [[nodiscard]] const QString& searchText() const noexcept;

    void setExposureRange(double minSeconds, double maxSeconds);
    void clearExposureRange();

    void setFilterName(const QString& filterName);
    [[nodiscard]] const QString& filterName() const noexcept;

    [[nodiscard]] bool matches(const FrameInfo& frame) const;
    [[nodiscard]] bool isEmpty() const noexcept;

private:
    QString m_searchText;
    std::optional<double> m_exposureMin;
    std::optional<double> m_exposureMax;
    QString m_filterName;
};
