// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What shows a view's chrome outside it: one floating toolbar and one history strip bound to a view.

#include "AfterNightShellCoreExport.h"

#include <functional>
#include <memory>

class ImageViewControl;
class QMenu;

namespace an::ui
{
class AnFloatingToolbar;
class AnHistoryStrip;
} // namespace an::ui

namespace an::ui::shell
{

/// Binds one floating toolbar and one history strip to a view: the toolbar shows that view's actions and choices
/// (`ImageViewControl::chromeActions`), its zoom and its name, and the strip its history's steps, Undo, Redo and Image
/// History; a click on a step moves the image there through the history. Told another view, it shows that one and
/// hears nothing more of the first; a view that goes is shown no more.
///
/// The toolbar and the strip may show different views - a task page's toolbar its canvas, its strip its target - and
/// either may be absent. A host makes the toolbar, the strip and their binder, and places the two; two binders never
/// share a toolbar or a strip, though they may show the same view. Used on the GUI thread only.
class AFTERNIGHTSHELLCORE_API ViewChromeBinder final
{
public:
    /// What fills the view's menu when the toolbar's More is clicked: a host's `ViewMenuBuilder::buildViewMenu`.
    using MenuFiller = std::function<void(QMenu& menu, ImageViewControl& view)>;

    /// A binder of a toolbar and a strip, either of which may be null; neither is owned, and both must outlive it.
    ViewChromeBinder(an::ui::AnFloatingToolbar* toolbar, an::ui::AnHistoryStrip* strip);
    ~ViewChromeBinder();

    ViewChromeBinder(const ViewChromeBinder&) = delete;
    ViewChromeBinder& operator=(const ViewChromeBinder&) = delete;

    /// Shows a view in the toolbar and its history in the strip; null shows none.
    void bind(ImageViewControl* view);

    /// Shows a view in the toolbar alone; null shows none.
    void bindToolbar(ImageViewControl* view);

    /// Shows a view's history in the strip alone; null shows none.
    void bindHistory(ImageViewControl* view);

    /// The view the toolbar shows, and the view whose history the strip shows; null for none.
    [[nodiscard]] ImageViewControl* toolbarView() const;
    [[nodiscard]] ImageViewControl* historyView() const;

    /// How many views the toolbar may be showing: it names the view with more than one.
    void setViewCount(int count);

    /// What fills the view's menu when More is clicked; with none, More opens nothing.
    void setMenuFiller(MenuFiller filler);

    /// The menu More opened last, while it is open; null otherwise.
    [[nodiscard]] QMenu* openMenu() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
