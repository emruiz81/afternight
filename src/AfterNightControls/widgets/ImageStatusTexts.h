// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a status line writes of the active image: its facts, its zoom, and the sky position under the cursor - the
/// texts AfterNight 1.1's status bar wrote, for every shell's status line to write the same.

#include "AfterNightControlsExport.h"

#include <QString>

class ImageViewControl;

namespace an::ui
{

/// The image's facts as the status bar writes them: its size and mode, its sample type and its size in memory, and for
/// a sequence its container, its frame and its rate - "  [8x4 (Grayscale)] - [32 bit floating point] - [0.00 MB]  ".
/// Empty with no view, or a view whose image is not described yet.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString imageFactsStatusText(ImageViewControl* view);

/// The view's zoom as the status bar writes it: "  100.00 %  "; empty with no view or no image.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString zoomStatusText(ImageViewControl* view);

/// A right ascension under the cursor, sexagesimal: "RA 12h 34m 56.78s"; and as a tooltip, in degrees.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString rightAscensionStatusText(double raDegrees);
[[nodiscard]] AFTERNIGHTCONTROLS_API QString rightAscensionStatusToolTip(double raDegrees);

/// A declination under the cursor, sexagesimal: "Dec +12° 34' 56.7\""; and as a tooltip, in degrees.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString declinationStatusText(double decDegrees);
[[nodiscard]] AFTERNIGHTCONTROLS_API QString declinationStatusToolTip(double decDegrees);

} // namespace an::ui
