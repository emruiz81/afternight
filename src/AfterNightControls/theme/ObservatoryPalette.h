// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory profile's colours: the tables they are made from - the neutral bases and the text of each theme and
/// contrast profile, the tint hues and how much of each is kept before it is blended, the tint strengths, the accent
/// families, the status and channel colours - the derivations that make a token set from them, and the contrast rules
/// every combination a user can choose is held to. `THEME_SYSTEM.md`, "Profiles" and "The surface ladder", says why
/// each is what it is.

#include "AfterNightControlsExport.h"
#include "theme/AnDesignTokens.h"

#include <QColor>
#include <QString>

#include <vector>

namespace an::ui
{

/// `mix(a, p, b)`: the fraction `p` of `a` and the rest of `b`, per channel in sRGB with premultiplied alpha, as CSS
/// `color-mix(in srgb, a p, b)` mixes them. Each component of the result is rounded to the nearest 8-bit value.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor mixColors(const QColor& a, double fractionOfA, const QColor& b);

/// Fills every colour of `tokens` - the colour roles, the surface layers and the material surfaces - as the
/// Observatory profile resolves `choices`, and leaves the spacing, the radii and the type scale as they are. The light
/// theme with the OLED contrast profile resolves as Balanced, as it does under the Classic profile.
/// resolveDesignTokens() is the way in; this is its Observatory half.
AFTERNIGHTCONTROLS_API void resolveObservatoryColors(DesignTokens& tokens, const ThemeChoices& choices);

/// A contrast rule a resolved token set misses: which pair, and by how much.
struct ContrastFailure
{
    /// The rule in words, with both colours: "textSecondary #AEB0BC on surface3 #34496F".
    QString pair;
    /// The ratio the pair has; 1 for two ladder steps that are equal.
    double ratio = 0.0;
    /// The ratio the rule asks for; 0 for two ladder steps that must differ.
    double minimum = 0.0;
};

/// Holds a token set resolved under the Observatory profile to the profile's rules. Primary and secondary text reach
/// 4.5 : 1 and tertiary text 3 : 1 against surfaces 0 to 3; the text on the accent reaches 4.5 : 1 on the accent, and
/// the text on the accent container on the container; text over an image reaches 4.5 : 1 on the scrim laid over black
/// and over white; the outline reaches 3 : 1 against surfaces 1 to 3 in the High contrast profile and 1.6 : 1 in the
/// others; two adjacent surfaces differ, except surfaces 0 and 1 under OLED, which are both black; and the status
/// colours reach 3 : 1 against surfaces 1 and 2. A translucent colour is laid over what it is drawn on first.
///
/// @param tokens  a token set resolveDesignTokens() made for `choices`.
/// @param choices the combination it was made for: its contrast profile chooses the outline's rule.
/// @return every rule the set misses; none when it meets them all.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ContrastFailure>
observatoryContrastFailures(const DesignTokens& tokens, const ThemeChoices& choices);

} // namespace an::ui
