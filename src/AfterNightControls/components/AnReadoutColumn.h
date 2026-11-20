// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The readout column: the figures of the active view in four cards.

#include "AfterNightControlsExport.h"

#include <QSize>
#include <QString>
#include <QWidget>

#include <cstdint>

#include <verdigris/wobjectdefs.h>

class QPaintEvent;

namespace an::ui
{

class AnAstrometryCard;
class AnElidedLabel;
class AnHistogramCard;
class AnIconButton;
class AnImageFactsCard;
class AnReadoutCard;
class AnReadoutText;
class AnStatisticsCard;

/// A card of the column, in its order.
enum class ReadoutCardId : std::uint8_t
{
    Histogram,
    Statistics,
    Astrometry,
    Image
};

/// The readout column: `surface1` with a hairline at its left, 360 px wide - 320 in a window under 1200 px wide. A
/// header row - the caps "Readout", the view's full name elided in the middle, a button that collapses the column -
/// then the Histogram, Statistics, Astrometry and Image cards a card gap apart, the histogram taking the height the
/// others leave up to a card 248 px high (`parts::kReadoutHistogramCardMaximum`); in a taller column the room under the
/// last card is left empty, so the plot is not stretched.
///
/// **What is remembered.** Which cards are folded is a line of their names (`foldedCards`, "statistics,image"), and
/// the column's being collapsed a flag: whoever keeps them hands them back (`setFoldedCards`, `setCollapsed`) and is
/// told when the person changes them (`foldsChanged`, `collapsedChanged`). Until it is handed a remembered line, the
/// Image card starts folded in a window under 820 px high, and the Astrometry card too under 720 px.
class AFTERNIGHTCONTROLS_API AnReadoutColumn final : public QWidget
{
    W_OBJECT(AnReadoutColumn)

public:
    explicit AnReadoutColumn(QWidget* parent = nullptr);
    ~AnReadoutColumn() override;

    /// The full name of the view the column reads out; empty for none.
    void setViewName(const QString& name);
    [[nodiscard]] QString viewName() const;

    [[nodiscard]] AnHistogramCard* histogramCard() const noexcept;
    [[nodiscard]] AnStatisticsCard* statisticsCard() const noexcept;
    [[nodiscard]] AnAstrometryCard* astrometryCard() const noexcept;
    [[nodiscard]] AnImageFactsCard* imageCard() const noexcept;
    [[nodiscard]] AnReadoutCard* card(ReadoutCardId id) const;

    /// A card's name in the remembered line.
    [[nodiscard]] static QString cardName(ReadoutCardId id);

    /// The folded cards' names, in the column's order, comma separated.
    [[nodiscard]] QString foldedCards() const;

    /// Folds the cards a remembered line names and opens the others; a name it does not know is ignored.
    void setFoldedCards(const QString& names);

    /// Whether the column is collapsed to its button.
    [[nodiscard]] bool isCollapsed() const noexcept;
    void setCollapsed(bool collapsed);

    /// The size of the window the column sits in, which decides its width and, until a remembered line arrives, whether
    /// the Image card starts folded.
    void setWindowSize(const QSize& size);

    /// The width the column takes for its window: 360, or 320 in a window under 1200 px wide.
    [[nodiscard]] int columnWidth() const noexcept;

    [[nodiscard]] AnElidedLabel* nameLabel() const noexcept;
    [[nodiscard]] AnIconButton* collapseButton() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;

    /// Told when the person folds or opens a card.
void foldsChanged() W_SIGNAL(foldsChanged)

    /// Told when the column is collapsed or opened.
    void collapsedChanged(bool collapsed) W_SIGNAL(collapsedChanged, collapsed)

        protected : bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void applyMetrics();
    void applyCollapsed();

    AnReadoutText* m_eyebrow = nullptr;
    AnElidedLabel* m_name = nullptr;
    AnIconButton* m_collapse = nullptr;
    QWidget* m_cards = nullptr;
    AnHistogramCard* m_histogram = nullptr;
    AnStatisticsCard* m_statistics = nullptr;
    AnAstrometryCard* m_astrometry = nullptr;
    AnImageFactsCard* m_image = nullptr;
    int m_width = 0;
    bool m_collapsed = false;
    bool m_foldsRemembered = false;
    bool m_restoring = false;
};

} // namespace an::ui
