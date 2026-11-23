// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The look an extension host is given: the theme state the application sends a host (`theme.describe`,
/// `theme.changed`) - the choices in force and the tokens they resolve to - and the style profile a full host is
/// started under. A host applies the state to its own tokens, so a window it shows is drawn as the application's are,
/// and answers it whole to an extension that paints itself (`afternight.ui_protocol.tokens()`).

#include "AfterNightControlsExport.h"

#include "app/ExtensionSdkService.h"
#include "theme/StyleProfile.h"

#include <QCborMap>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace an::ui::extensions
{

/// The names of the theme state's entries, in the order the state states them: the choices in force - whether the
/// theme is dark, the style profile, the contrast profile, the accent, the surface tint, its strength, the preset they
/// make, the density and Night - and the tokens they resolve to - the colours, the surface ladder, the sizes, the
/// spacing, the radii and the type.
[[nodiscard]] AFTERNIGHTCONTROLS_API const std::vector<QString>& extensionThemeStateNames();

/// The theme state of the choices in force: every entry `extensionThemeStateNames()` names. A choice is its
/// enumerator's number, as the settings resolve it; the style profile is its name (`styleProfileName`).
[[nodiscard]] AFTERNIGHTCONTROLS_API QCborMap currentExtensionThemeTokens();

/// The theme state of the choices in force, under the theme's name ("AfterNight Dark", "AfterNight Light").
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionThemeSnapshot currentExtensionThemeSnapshot();

/// A style profile's name, as a host is told it: "observatory".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString styleProfileName(StyleProfile profile);

/// The style profile a name names, ignoring case and surrounding space; nothing for another name.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<StyleProfile> styleProfileNamed(const QString& name);

/// The arguments a full host is started under so its look is the application's: `--style-profile` and the profile's
/// name. The host puts the profile in force before its first widget, as the application does.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<std::string> hostLookArguments(StyleProfile profile);

} // namespace an::ui::extensions
