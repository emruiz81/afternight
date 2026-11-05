// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stand-in for the image selector's tooltip, which the mask selector shows beside each view it offers: this suite
/// compiles the viewer's closure rather than linking the Controls library, and the tooltip is the process framework's.

#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"

namespace an::ui
{

QString buildImageSelectorTooltip(const ImageViewControl* view)
{
    return view != nullptr ? QString::fromUtf8(view->getUniqueName()) : QString();
}

} // namespace an::ui
