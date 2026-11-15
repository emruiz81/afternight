// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The readout's statistics: six figures of the image, at one of three ranges.

#include "AfterNightControlsExport.h"
#include "components/AnReadoutCard.h"
#include "components/ReadoutFigures.h"
#include "controls/ImageStatisticsFigures.h"

#include <QString>

#include <cstddef>
#include <optional>

#include <verdigris/wobjectdefs.h>

namespace an::ui
{

class AnIconButton;
class AnReadoutGrid;
class AnSegmentedControl;
class StatisticsTable;

/// The Statistics card. A grey image reads out Min, Max, Mean, Median, StdDev and MAD in three columns; a colour image
/// shows a table of the same six, a row each, and a column per colour channel headed by its dot. The figures are the
/// statistics window's (`readoutFigure`) at the range the header's three segments choose - Normalized, 8-bit, 16-bit
/// - and a change of range writes them again without measuring anything. The header's tool opens the window.
class AFTERNIGHTCONTROLS_API AnStatisticsCard final : public AnReadoutCard
{
    W_OBJECT(AnStatisticsCard)

public:
    explicit AnStatisticsCard(QWidget* parent = nullptr);
    ~AnStatisticsCard() override;

    /// Shows an image's figures; nothing clears them.
    void setReport(std::optional<ImageStatisticsReport> report);
    [[nodiscard]] const std::optional<ImageStatisticsReport>& report() const noexcept;

    /// The range the figures are shown at; Normalized until the person or `setScale` chooses another.
    [[nodiscard]] StatisticsScale scale() const noexcept;
    void setScale(StatisticsScale scale);

    /// A figure as shown: of a channel's column in the table, or of the one channel; NaN when there is none.
    [[nodiscard]] double figure(ReadoutStatistic statistic, std::size_t channel = 0u) const;

    /// A figure's text as shown.
    [[nodiscard]] QString figureText(ReadoutStatistic statistic, std::size_t channel = 0u) const;

    /// Whether it shows the colour table rather than the grey read-outs.
    [[nodiscard]] bool showsTable() const;

    [[nodiscard]] AnSegmentedControl* rangePicker() const noexcept;
    [[nodiscard]] AnIconButton* windowButton() const noexcept;
    [[nodiscard]] AnReadoutGrid* greyReadouts() const noexcept;
    [[nodiscard]] QWidget* colourTable() const noexcept;

    /// Told when the range changes.
void scaleChanged() W_SIGNAL(scaleChanged)

    /// Told when the person asks for the statistics window.
    void statisticsWindowRequested() W_SIGNAL(statisticsWindowRequested)

        protected : void staleChanged() override;

private:
    void refill();

    std::optional<ImageStatisticsReport> m_report;
    StatisticsScale m_scale = StatisticsScale::Normalized;
    AnSegmentedControl* m_range = nullptr;
    AnIconButton* m_window = nullptr;
    AnReadoutGrid* m_grey = nullptr;
    StatisticsTable* m_table = nullptr;
};

} // namespace an::ui
