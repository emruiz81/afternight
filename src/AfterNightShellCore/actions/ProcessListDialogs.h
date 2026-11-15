// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the Process menu's own entries ask a person: whether to forget the processes used last, which processes are
/// favourites, whether to forget the favourites.

#include "AfterNightShellCoreExport.h"

class QWidget;

namespace an::ui::shell
{

class ApplicationCore;

/// Asks whether to forget the processes used last, and forgets them on Yes.
/// @return whether the list is what the menus should show again: it was empty, or the person said Yes.
AFTERNIGHTSHELLCORE_API bool clearRecentProcesses(ApplicationCore& core);

/// Lets the person choose the favourite processes among every registered one, searchable by name, in a dialog over
/// `parent`; the choice is kept on OK.
/// @return whether the favourites changed hands: the person pressed OK.
AFTERNIGHTSHELLCORE_API bool manageFavoriteProcesses(ApplicationCore& core, QWidget* parent);

/// Asks whether to forget the favourite processes, and forgets them on Yes.
/// @return whether the list is what the menus should show again: it was empty, or the person said Yes.
AFTERNIGHTSHELLCORE_API bool clearFavoriteProcesses(ApplicationCore& core);

} // namespace an::ui::shell
