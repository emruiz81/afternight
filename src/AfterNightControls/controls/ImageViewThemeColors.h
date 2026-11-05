// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>

namespace an::ui
{
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor imageViewScrollAreaWindowColor(bool darkMode);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor imageViewCardSurfaceTintColor(bool darkMode);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor imageViewOverlayAutostretchColor(bool darkMode);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor imageViewOverlayOriginalColor(bool darkMode);
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor imageViewOverlayBackdropColor(bool darkMode);
} // namespace an::ui
