// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The readout's histogram: the counts of the image in a well, and the histogram window's tools under it.

#include "AfterNightControlsExport.h"
#include "components/AnReadoutCard.h"
#include "components/AnReadoutGrid.h"
#include "widgets/HistogramDisplayData.h"

#include <QPointer>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <memory>

#include <verdigris/wobjectdefs.h>

class HistogramWidget;
class QComboBox;
class QGraphicsOpacityEffect;
class QHBoxLayout;
class QResizeEvent;
class QScrollArea;

namespace an::ui
{

class AnIconButton;

/// A tool of the histogram card, in its order under the plot.
enum class HistogramCardTool : std::uint8_t
{
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Grid,
    ClippingInfo,
    AbsoluteScales,
    Areas
};

/// The Histogram card: the counts it is handed, drawn by the histogram window's widget in a well at least 100 px high
/// that grows with the room it is given (the readout column gives it up to `parts::kReadoutHistogramCardMaximum`);
/// folded, it is its header alone and grows no more. Under the well the window's tools, which act on the widget as the
/// window's do - Zoom in, Zoom out, Zoom reset; Show grid, Show clipping info, Absolute scales, Draw histogram areas,
/// with the window's defaults - and its resolution. A change of resolution is told, and the counts are made again by
/// whoever counts them. The header says "RGB" or "Grayscale" and its tool opens the window.
class AFTERNIGHTCONTROLS_API AnHistogramCard final : public AnReadoutCard
{
    W_OBJECT(AnHistogramCard)

public:
    explicit AnHistogramCard(QWidget* parent = nullptr);
    ~AnHistogramCard() override;

    /// Shows the counts of an image; null clears the plot.
    void setCounts(std::shared_ptr<const HistogramDisplayData> counts);

    /// How many channels the image has: the header says "RGB" for three or more, "Grayscale" otherwise, and nothing
    /// for none.
    void setChannelCount(std::size_t channels);

    /// Which colour channels' curves are drawn, as the view shows its channels.
    void setShownChannels(bool red, bool green, bool blue);

    /// The resolution the counts are asked at.
    [[nodiscard]] HistogramResolution resolution() const;

    [[nodiscard]] HistogramWidget* histogramWidget() const noexcept;
    [[nodiscard]] AnIconButton* tool(HistogramCardTool tool) const;
    [[nodiscard]] QComboBox* resolutionPicker() const noexcept;
    [[nodiscard]] AnIconButton* windowButton() const noexcept;
    [[nodiscard]] AnReadoutWell* well() const noexcept;

    /// Told when the person picks another resolution.
void resolutionChanged() W_SIGNAL(resolutionChanged)

    /// Told when the person asks for the histogram window.
    void histogramWindowRequested() W_SIGNAL(histogramWindowRequested)

        protected : void staleChanged() override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// The resolution at the end of the tools' row, or on a row of its own under it where the card is too narrow for
    /// the two side by side - a narrow column's.
    void placeResolution();

    QPointer<HistogramWidget> m_plot;
    QScrollArea* m_scroll = nullptr;
    AnReadoutWell* m_well = nullptr;
    AnIconButton* m_tools[7] = {};
    QComboBox* m_resolution = nullptr;
    QHBoxLayout* m_toolRow = nullptr;
    QHBoxLayout* m_resolutionRow = nullptr;
    QWidget* m_resolutionSeparator = nullptr;
    AnIconButton* m_window = nullptr;
    QGraphicsOpacityEffect* m_staleEffect = nullptr;
};

} // namespace an::ui
