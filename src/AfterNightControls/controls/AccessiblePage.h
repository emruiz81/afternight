// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What makes a page reachable from the keyboard and readable by assistive technology whatever its controls' builder
/// did: the focus chain in the page's reading order, every control named from what a person reads beside it, and the
/// keyboard's Tab left to move between controls.

#include "AfterNightControlsExport.h"

#include <QString>

class QWidget;

namespace an::ui
{

/// Puts the focus chain of the widgets under `root` - `root`'s own window's widgets, not those of a window of their
/// own - in the order a person reads them, which the layouts state: a box's items in its direction, a grid's and a
/// form's row by row, a stack's pages in order. A child no layout places is read with its siblings by where it lies -
/// top, then left - and one that floats over a sibling (a toolbar over a canvas) is read after the deepest widget it
/// floats over. Only widgets that take the focus from Tab move; the order of the others is left. Cheap enough to run
/// before each Tab.
AFTERNIGHTCONTROLS_API void orderFocusChain(QWidget& root);

/// The name a person reads for a control that assistive technology finds none for; empty when it has a name, or
/// nothing beside it says one. In order: the label of its form row; in a grid, the label at the left of its row and the
/// heading over its column ("Width, Target"); in a row of a box, the label - or the check box - before it, and in a
/// column, the label over it; the label of the row or the cell a widget it is the first control of lies in; a field's
/// placeholder; a button's tooltip, for one that shows an icon alone; and, for a list, a table or a text area, the
/// title of the card, the section or the group it fills.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString readableNameOf(const QWidget& control);

/// Names every control under `root` that assistive technology finds no name for with `readableNameOf`, and leaves
/// alone the ones it names already.
AFTERNIGHTCONTROLS_API void nameUnnamedControls(QWidget& root);

/// What the keyboard needs of the widgets under `root`: a scroll area that holds a widget of controls is no stop of its
/// own - the controls are, and the area scrolls to the one the focus is on; a text area and a table pass Tab on, the
/// arrows moving inside them; and every unnamed control is named (`nameUnnamedControls`).
AFTERNIGHTCONTROLS_API void prepareForKeyboard(QWidget& root);

} // namespace an::ui
