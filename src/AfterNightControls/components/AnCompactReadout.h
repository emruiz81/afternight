// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The compact readout a task pins above its process: the preview's histogram and five statistics, each with its change
/// against the original.

#include "AfterNightControlsExport.h"
#include "controls/ImageStatisticsFigures.h"
#include "widgets/HistogramDisplayData.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>

#include <verdigris/wobjectdefs.h>

class HistogramWidget;
class QGridLayout;
class QPaintEvent;
class QResizeEvent;

namespace an::ui
{

class AnIconButton;
class AnReadoutGrid;
class AnReadoutText;
class AnReadoutWell;
class CompactAstrometryLine;

/// The compact readout: `surface2`, padding 6 12 8. A 20 px header - the caps "Readout · preview", the target's
/// astrometry in one line ("Solved", "0.52″/px · SIP 4/4"; nothing when it is not solved) and a chevron that asks for
/// the full readout. A body of the preview's histogram in a 92 px well tagged "Preview", beside an 84 px column of the
/// mode ("RGB · 8-bit"), Clip low and Clip high; and five cells - Min, Max, Mean, Median, StdDev - each with the change
/// against the original under its value ("Δ +0.0123", "Δ ±0"). Each cell reads whole: under 480 px wide the cells take
/// the room under the histogram, five to a row where five fit and else in rows as even as fit; wider, they sit beside
/// it in a row of five or two rows of three, where those read whole and leave the histogram 120 px, and else under it.
///
/// The figures are the preview's: a colour image's are the mean of its three channels' figures. While the preview is
/// measured again - or an interactive frame is shown, whose pixels are approximated - the values are drawn at 55 %.
class AFTERNIGHTCONTROLS_API AnCompactReadout final : public QWidget
{
    W_OBJECT(AnCompactReadout)

public:
    explicit AnCompactReadout(QWidget* parent = nullptr);
    ~AnCompactReadout() override;

    /// The target's astrometry in one line - "0.52″/px · SIP 4/4" - or empty when it is not solved.
    void setAstrometry(const QString& line);
    [[nodiscard]] QString astrometry() const;

    /// The preview's counts; null clears the plot and the clipping.
    void setCounts(std::shared_ptr<const HistogramDisplayData> counts);

    /// The mode the counts are drawn in: the channels and the resolution, "RGB · 8-bit".
    void setMode(std::size_t channels, HistogramResolution resolution);

    /// The preview's figures and the original's; with no original, no change is written.
    void setFigures(std::optional<ImageStatisticsReport> preview, std::optional<ImageStatisticsReport> original);

    /// Whether the values are drawn at 55 %.
    void setStale(bool stale);
    [[nodiscard]] bool isStale() const noexcept;

    /// Whether the cells sit beside the histogram.
    [[nodiscard]] bool isWide() const noexcept;

    [[nodiscard]] QString modeText() const;
    [[nodiscard]] AnReadoutGrid* cells() const noexcept;
    [[nodiscard]] AnReadoutGrid* clipping() const noexcept;
    [[nodiscard]] AnReadoutWell* well() const noexcept;
    [[nodiscard]] HistogramWidget* histogramWidget() const noexcept;
    [[nodiscard]] AnIconButton* expandButton() const noexcept;
    [[nodiscard]] QWidget* astrometryLine() const noexcept;

    /// Whether the full readout is open over the column, which the chevron shows checked. Set by the owner that opens
    /// and closes it, which is told nothing.
    void setFullReadoutShown(bool shown);
    [[nodiscard]] bool isFullReadoutShown() const noexcept;

    /// Told when the person opens the full readout with the chevron, or closes it with it.
void fullReadoutToggled(bool shown) W_SIGNAL(fullReadoutToggled, shown)

    protected : bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Where the cells go at a width: beside the histogram or under it, how many to a row, and how wide they are beside
    /// it.
    struct Arrangement
    {
        bool wide = false;
        int columns = 1;
        int width = 0;

        [[nodiscard]] bool operator==(const Arrangement&) const = default;
    };

    void applyMetrics();
    [[nodiscard]] Arrangement arrangementAt(int width) const;
    void arrange(const Arrangement& arrangement);
    /// Arranges the cells for the readout's width, when that moves them.
    void rearrange();
    void refillCells();

    CompactAstrometryLine* m_astrometry = nullptr;
    AnIconButton* m_expand = nullptr;
    AnReadoutWell* m_well = nullptr;
    QPointer<HistogramWidget> m_plot;
    AnReadoutText* m_mode = nullptr;
    AnReadoutGrid* m_clipping = nullptr;
    AnReadoutGrid* m_cells = nullptr;
    QWidget* m_side = nullptr;
    QGridLayout* m_body = nullptr;
    std::optional<ImageStatisticsReport> m_preview;
    std::optional<ImageStatisticsReport> m_original;
    Arrangement m_arrangement;
    bool m_stale = false;
};

} // namespace an::ui
