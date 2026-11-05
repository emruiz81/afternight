// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stars a persisted star profile holds, as the engine's detected-star records: a second-generation profile's
/// fitted point-spread functions as they are, a first-generation profile's circles as stars of that radius.
///
/// The profiled recombination paints its mask from a profile a person chose, not from a detection of its own, so the
/// profile's stars have to become what the engine's mask painter reads. A second-generation profile carries the fit
/// the engine's detector would have produced - centre, widths, angle, amplitude, background - and maps field for
/// field. A first-generation profile carries a position and a radius, which its own mask painter drew as a circle of
/// that radius; the circle the engine paints has four full widths across, so the radius becomes half a full width.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/StarFilter.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <string_view>

namespace an
{
class ExecutionContext;
} // namespace an

namespace process_utils
{

/// The stars of a profile as detected-star records, in the profile's order.
///
/// @param profile the profile, of either generation.
/// @return the list; empty for a profile holding no star.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ui::StarList starsOfProfile(const an::StarProfileFile& profile);

/// Reads a star profile file and converts its stars.
///
/// @param execution the operation the read belongs to.
/// @param path the file's path.
/// @return the list; or what the read refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ui::StarList>
readProfileStars(const an::ExecutionContext& execution, std::string_view path);

} // namespace process_utils
