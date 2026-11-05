// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QString>

class ImageViewControl;

namespace an::ui
{

/// Builds a rich-text tooltip string summarising the image behind an ImageViewControl.
/// Designed to be used as the per-item tooltip inside every target-image QComboBox.
///
/// The tooltip shows:
///   • Bold image display name (without extension)
///   • Absolute file path when available
///   • "In-memory file" when there is no real path (or temporary .dmp backing)
///   • Dimensions (width × height)
///   • Number of channels and colour space
///   • Pixel type / bit depth
///   • Estimated memory size
///
/// Returns an empty QString when @p vc is null or has no loaded image.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString buildImageSelectorTooltip(const ImageViewControl* vc);

} // namespace an::ui
