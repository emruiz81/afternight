// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

class QDialogButtonBox;
class IconManager;

namespace an::ui
{
class IIconProvider;
}

namespace process_utils
{

/// Apply standard icons to all recognized buttons in a QDialogButtonBox.
///
/// Assigns themed icons for common dialog roles:
///   - Ok / Apply  → check mark (white normal, gray disabled)
///   - Close       → close icon
///   - Reset       → refresh icon
///
/// Call this from refreshTheme() after the base-class implementation
/// so that role properties are already in place.
AFTERNIGHTCONTROLS_API void applyDialogButtonBoxIcons(QDialogButtonBox* buttonBox, an::ui::IIconProvider* iconProvider);

/// Backward-compatible overload — delegates via IAppContext.
AFTERNIGHTCONTROLS_API void applyDialogButtonBoxIcons(QDialogButtonBox* buttonBox, IconManager* iconManager);

} // namespace process_utils
