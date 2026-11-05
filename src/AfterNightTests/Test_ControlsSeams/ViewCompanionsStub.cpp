// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stand-in for the tool windows the viewer reaches through its companion seam: it records which window the viewer
/// asks for and opens none; the opener itself is defined beside the windows in `controls/ViewTools.cpp`, in the
/// library. The history's stand-in is `ViewHistoryStub.cpp`, the astrometry overlays' `ViewOverlaysStub.cpp` and the
/// sequence player's `ViewSequencePlayerStub.cpp`; the record they all write is `ViewCompanionRecord.cpp`.

#include "Test_ControlsSeams/ViewCompanionsStub.h"

#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QWidget>

namespace an::ui
{

QWidget* openViewTool(ViewTool tool, ::ImageViewControl&)
{
    an::tests::viewer::companionRecord().toolsOpened.push_back(tool);
    return nullptr;
}

} // namespace an::ui
