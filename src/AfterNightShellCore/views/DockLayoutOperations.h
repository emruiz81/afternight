// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a person does to the arrangement of the views: gather them into one tab group, split them into a grid of equal
/// shares, fit them to their windows, and lock the arrangement so no drag moves a view.
///
/// An operation acts on a dock area - a `DockTarget`, which the shell names - or on the window a view is in: the dock
/// area it is docked in, or the floating window it shares with others. A lock refuses, through `DockHooks`, a drag out
/// of the locked main window and an indicator into it, and nothing of another; the lock of a shell's only dock area
/// (a target with no workspace) is the whole layout's, as the layout lock always was: every drag and every indicator,
/// a floating window's included.

#include "AfterNightShellCoreExport.h"

#include "core/IShell.h"

#include <list>
#include <memory>
#include <vector>

class ImageViewControl;

namespace an::ui::shell
{

class ApplicationCore;

class AFTERNIGHTSHELLCORE_API DockLayoutOperations
{
public:
    /// Acts on the views the core lists, and sets the catalogue's Lock Layout and Unlock Layout as the lock is taken
    /// and lifted.
    explicit DockLayoutOperations(ApplicationCore& core);
    /// Lifts every lock the operations took.
    ~DockLayoutOperations();

    DockLayoutOperations(const DockLayoutOperations&) = delete;
    DockLayoutOperations& operator=(const DockLayoutOperations&) = delete;
    DockLayoutOperations(DockLayoutOperations&&) = delete;
    DockLayoutOperations& operator=(DockLayoutOperations&&) = delete;

    /// Gathers every listed view into the target's dock area as one tab group; the active view stays active.
    void groupAll(const DockTarget& target);
    /// Places every listed view in the target's dock area in a grid of equal shares; the active view stays active.
    void splitAllEqually(const DockTarget& target);

    /// Gathers the views that share the window `view` is in into one tab group there; `view` is made active.
    void groupAll(ImageViewControl& view);
    /// Places the views that share the window `view` is in in a grid of equal shares there; `view` is made active.
    void splitAllEqually(ImageViewControl& view);
    /// Fits each view that shares the window `view` is in to its window.
    void fitAllToWindow(ImageViewControl& view);

    /// Locks the target's arrangement, and offers Unlock Layout in place of Lock Layout. Nothing when it is locked.
    void lock(const DockTarget& target);
    /// Lifts the target's lock, and offers Lock Layout in place of Unlock Layout.
    void unlock(const DockTarget& target);
    [[nodiscard]] bool isLocked(const DockTarget& target) const;

    /// The views of `views` that share the window `view` is in: the floating window it is in, or, docked, every view
    /// docked in a dock area. `view` among them.
    [[nodiscard]] static std::list<ImageViewControl*>
    viewsSharingTheWindowOf(ImageViewControl& view, const std::vector<ImageViewControl*>& views);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
