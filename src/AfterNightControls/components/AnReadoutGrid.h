// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The readout's small parts: read-outs in a grid - a caps label over a value in the monospace, with an optional unit
/// after it and a note under it - a line of text in one of the readout's styles, and the well a plot is drawn in.

#include "AfterNightControlsExport.h"

#include <QFont>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class QPaintEvent;
class QStackedLayout;

namespace an::ui
{

/// One read-out: what it is, its value, its unit and a note - the change against the original - under the value.
struct ReadoutCell
{
    QString label;
    QString value;
    QString unit;
    QString note;
    /// What a tooltip says of it beyond its label.
    QString detail;

    [[nodiscard]] bool operator==(const ReadoutCell&) const = default;
};

/// Paints its read-outs in rows of `columns` equal cells, a cell gap apart: the label in the caption's caps in the
/// tertiary text, the value in the body's monospace in the primary text, a unit in the caption in the secondary text,
/// and a note in the caption's monospace in the secondary text. While stale, the values, units and notes are drawn at
/// 55 %; the labels stay. Assistive technology reads every cell as "label value unit".
class AFTERNIGHTCONTROLS_API AnReadoutGrid final : public QWidget
{
public:
    explicit AnReadoutGrid(int columns, QWidget* parent = nullptr);

    /// Shows other read-outs.
    void setCells(std::vector<ReadoutCell> cells);
    [[nodiscard]] const std::vector<ReadoutCell>& cells() const noexcept;

    [[nodiscard]] int columns() const noexcept;
    void setColumns(int columns);

    /// Whether the values are drawn at 55 %.
    void setStale(bool stale);
    [[nodiscard]] bool isStale() const noexcept;

    /// The rectangle of a cell, in the grid's coordinates.
    [[nodiscard]] QRect cellRect(int index) const;

    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] int cellHeight() const;
    [[nodiscard]] int rows() const;
    void describe();

    std::vector<ReadoutCell> m_cells;
    int m_columns = 1;
    bool m_stale = false;
};

/// A line of text in one of the readout's styles, elided at its end when there is no room.
class AFTERNIGHTCONTROLS_API AnReadoutText final : public QWidget
{
public:
    /// How the text is drawn.
    enum class Style
    {
        /// The caption's caps in the tertiary text: "READOUT".
        Eyebrow,

        /// The caption in the secondary text: "RGB - 8-bit".
        Caption,

        /// The caption's monospace in the secondary text: a figure beside an icon.
        Figure
    };

    explicit AnReadoutText(Style style, const QString& text = QString(), QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] QFont shownFont() const;

    Style m_style;
    QString m_text;
};

/// The well a plot is drawn in: `bgWell` with an 8 px radius, its plot inset so that no square corner leaves it, and
/// an optional tag laid over the plot's upper left - "Preview".
class AFTERNIGHTCONTROLS_API AnReadoutWell final : public QWidget
{
public:
    explicit AnReadoutWell(QWidget* parent = nullptr);

    /// The plot the well holds, which it takes as its child.
    void setPlot(QWidget* plot);

    /// A tag laid over the plot; empty shows none.
    void setTag(const QString& tag);
    [[nodiscard]] QString tag() const;

    /// The tag's widget, null while there is none.
    [[nodiscard]] QWidget* tagWidget() const noexcept;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QStackedLayout* m_stack = nullptr;
    QWidget* m_overlay = nullptr;
    QWidget* m_tag = nullptr;
};

} // namespace an::ui
