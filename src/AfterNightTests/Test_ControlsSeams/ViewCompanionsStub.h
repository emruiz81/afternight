// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What this suite's cases read of the stand-in companions: the history, the sequence player, the tool windows and the
/// overlays the viewer reaches through `controls/ImageViewCompanions.h`. The suite compiles the viewer's closure
/// rather than linking the Controls library, so the companions' own units are left out and these stand in for them -
/// the history's in `ViewHistoryStub.cpp`, the overlays' in `ViewOverlaysStub.cpp` and the sequence player's in
/// `ViewSequencePlayerStub.cpp`. The stand-ins record what the viewer asks and answer as the case steers.
#pragma once

#include "controls/ImageViewCompanions.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::viewer
{

/// Everything the stand-in companions were asked, and how the stand-in history answers.
struct CompanionRecord
{
    /// The initial steps the histories recorded.
    std::vector<std::string> initialSteps;

    /// The edits the histories recorded before the view showed them, by step name.
    std::vector<std::string> recordedEdits;

    /// The identity of each image an edit recorded, in order.
    std::vector<std::uint64_t> recordedEditAssets;

    /// Whether the histories refuse every edit, as one that cannot keep the step does.
    bool refuseEdits = false;

    /// How many edits asked the histories for their preparation.
    int editsPrepared = 0;

    /// How many times a history window was opened.
    int historyWindows = 0;

    /// How many undos and redos the histories were asked for.
    int undosAsked = 0;
    int redosAsked = 0;

    /// How many renames the histories recorded.
    int renames = 0;

    /// How many times a history inherited another's lineage.
    int lineagesInherited = 0;

    /// How many sequence players were made.
    int sequencePlayersMade = 0;

    /// How many times a sequence player's controls were disabled, and enabled.
    int playersDisabled = 0;
    int playersEnabled = 0;

    /// The tools opened, in order.
    std::vector<an::ui::ViewTool> toolsOpened;

    /// The overlays made, in order.
    std::vector<an::ui::AstrometryOverlay> overlaysMade;

    /// The stars handed to the detected-stars overlay, in order; a null entry cleared them.
    std::vector<std::shared_ptr<const an::ui::ViewOverlayStars>> overlayStars;

    /// The magnitude limits handed to the catalogue overlays.
    std::vector<double> magnitudeLimits;

    /// The layers handed to the catalogue overlay, in order: whether it draws the deep-sky objects, and the bright
    /// stars.
    std::vector<std::pair<bool, bool>> overlayLayers;

    /// The counts the lens histograms were handed.
    std::vector<an::ui::LensHistogram> lensCounts;
};

/// The record of the stand-ins, shared by every view of the case.
[[nodiscard]] CompanionRecord& companionRecord();

/// Forgets everything recorded and restores the stand-ins' answers.
void resetCompanionRecord();

} // namespace an::tests::viewer
