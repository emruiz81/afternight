// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A row of controls that stands them side by side where its width holds them, and one under another where it does not.

#include "AfterNightControlsExport.h"

#include <QBoxLayout>
#include <QRect>
#include <QSize>

class QWidget;

namespace an::ui
{

/// Lays its items out as a `QHBoxLayout` does - their stretch, their spacing, their alignment - where the width it is
/// given holds them side by side, and one under another, as a `QVBoxLayout` does, where it does not. It states the
/// least width of its widest item rather than of the whole row, so a column narrower than the row gives it the width it
/// has, and it is laid out again there, one item under another and taller.
///
/// What it is for: a row of a few controls - buttons, a check box beside a label and its field - that the interface's
/// font can draw wider than the column that holds it. Where the row fits it keeps the look it was given; where it does
/// not it gives way whole, rather than squeezing its controls under their text.
class AFTERNIGHTCONTROLS_API AnSideBySideLayout final : public QBoxLayout
{
public:
    explicit AnSideBySideLayout(QWidget* parent = nullptr);
    ~AnSideBySideLayout() override;

    AnSideBySideLayout(const AnSideBySideLayout&) = delete;
    AnSideBySideLayout& operator=(const AnSideBySideLayout&) = delete;
    AnSideBySideLayout(AnSideBySideLayout&&) = delete;
    AnSideBySideLayout& operator=(AnSideBySideLayout&&) = delete;

    /// Whether the items stand side by side now: the last width the row was given held them.
    [[nodiscard]] bool sideBySide() const;

    /// The width the items need side by side, as a `QHBoxLayout` sums it: the items' least widths, the room stated
    /// between them, the spacing between those shown and the margins. The same whichever way the items stand.
    [[nodiscard]] int sideBySideWidth() const;

    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
};

/// Stands the items of a row a builder made side by side where its width holds them and one under another where it
/// does not: they move, each with its stretch and its alignment, into an `AnSideBySideLayout` that becomes the row's
/// only item, and the row's margins go with them. The row keeps its place in its parent and every pointer to it holds;
/// the new layout takes the spacing the row has.
///
/// @param row a row of widgets, layouts and spacers.
/// @return the layout its items stand in now.
AFTERNIGHTCONTROLS_API AnSideBySideLayout* standSideBySideWhereItFits(QBoxLayout& row);

/// The same for the row that holds a widget itself - a row a builder made and kept no pointer to - searched in the
/// layout of the widget it lies in.
///
/// @param member a widget of the row.
/// @return the layout the row's items stand in now; null where no box layout holds the widget.
AFTERNIGHTCONTROLS_API AnSideBySideLayout* standSideBySideWhereItFits(QWidget& member);

} // namespace an::ui
