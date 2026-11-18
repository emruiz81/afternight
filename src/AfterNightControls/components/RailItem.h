// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// An item of the rail: a place, a task or an item of its foot, as the rail's owner hands it over.

#include "AfterNightControlsExport.h"

#include <QImage>
#include <QString>

#include <optional>

namespace an::ui
{

/// Where an item stands in the rail: the places at the top, the tasks under them, the foot at the bottom.
enum class RailSection
{
    Place,
    Task,
    Foot
};

/// Where a task's run stands.
enum class RailRunPhase
{
    None,
    Running,
    Succeeded,
    Failed
};

/// An item of the rail. The rail shows what it is given and knows nothing of what an item stands for.
struct RailItem
{
    QString id; ///< What the rail tells the item by; unique in a rail.
    RailSection section = RailSection::Place;
    QString iconKey; ///< A place's or a foot item's icon, by the name of its file in the icons ("home").
    QString text; ///< Its name: "Workspace 2 - Masters"; a task's "process - target".
    QString badgeText; ///< A place's digit, a foot item's count of what is unseen; none when empty.
    bool selected = false;
    bool enabled = true;
    QImage thumbnail; ///< A task's target, small; none for a task with no target image.
    QString glyphKey; ///< A task's process, by the name of its glyph's file in the icons.
    RailRunPhase runPhase = RailRunPhase::None;
    std::optional<int> progress; ///< How far a run has come, 0 to 100; none while it says nothing of it.
    QString runText; ///< What a run on its way does, when it says: "Step 5/9: Registration"; "applying" when empty.
    bool unseenResult = false; ///< A run that ended while the task was not shown: its result's dot is drawn.
    bool floating = false; ///< A task in a window of its own, or a workspace in one.
};

/// Whether two items show the same; a thumbnail is the same picture when it is the same image (its cache key).
[[nodiscard]] AFTERNIGHTCONTROLS_API bool sameRailItem(const RailItem& a, const RailItem& b);

/// What an item is called - its tooltip, and its name to assistive technology: its text, and for a task where its run
/// stands ("applying 42 %", or what the run says it does: "Step 5/9: Registration 58 %"; "applied", "failed") and
/// whether it is in a window of its own.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString railItemDescription(const RailItem& item);

} // namespace an::ui
