// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How a unit of Controls is handed the application's actions without naming who holds them: an action by the object
/// name it carries (`actionOpen`, `actionSave_as`), as the application's action catalogue answers it. A window's
/// builder takes its actions this way, and so do the coordinators that set an action's state, so that none of them
/// includes the library the catalogue lives in.

#include <functional>
#include <string_view>

class QAction;

namespace an::ui
{

/// An action of the application's catalogue by its object name; null when the catalogue holds none of that name.
using ActionLookup = std::function<QAction*(std::string_view objectName)>;

} // namespace an::ui
