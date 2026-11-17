// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A workspace's page: its dock area, the one view it shows maximized or the list of its floating images, with the
/// active view's floating toolbar over it and its history strip under it.

#include "AfterNightShellObservatoryExport.h"

#include <QWidget>

#include <memory>

class ImageViewControl;
class QResizeEvent;
class QStackedWidget;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui
{
class AnFloatingToolbar;
class AnHistoryStrip;
} // namespace an::ui

namespace an::ui::shell
{
class ViewChromeBinder;
} // namespace an::ui::shell

namespace an::ui::observatory
{

class FloatingImagesPage;
class ToolbarClearance;

/// The page of a workspace in the window's centre: the workspace's dock area, where its views split, stack as tabs and
/// float as they always did; one of them maximized over the whole page, its dock keeping a stand-in until it is
/// restored, the tab strips out of sight; or, when every image it holds floats, the list of them
/// (`FloatingImagesPage`). Over the page, centred 12 px above its bottom, the active view's floating toolbar - lifted
/// to 8 px above a sequence's player it would cover (`ToolbarClearance`); under it the active view's history strip;
/// both bound to the view by a `ViewChromeBinder` the page owns, and both kept through a maximize.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspacePage final : public QWidget
{
public:
    /// Shows the dock area `dockArea`, which the shell's window made and owns from here as the page's child, and the
    /// floating images' page `floatingImages`, which the page owns from here.
    WorkspacePage(KDDockWidgets::QtWidgets::MainWindow& dockArea, FloatingImagesPage& floatingImages,
                  QWidget* parent = nullptr);
    ~WorkspacePage() override;

    WorkspacePage(const WorkspacePage&) = delete;
    WorkspacePage& operator=(const WorkspacePage&) = delete;
    WorkspacePage(WorkspacePage&&) = delete;
    WorkspacePage& operator=(WorkspacePage&&) = delete;

    /// The dock area the page shows.
    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* dockArea() const noexcept;
    [[nodiscard]] FloatingImagesPage* floatingImages() const noexcept;

    /// Shows `view` over the whole page; its dock keeps a stand-in until it is restored. A view maximized already is
    /// restored first.
    void maximize(ImageViewControl& view);
    /// Puts the maximized view back in its dock and shows the dock area; nothing when `view` is not the one maximized.
    void restore(ImageViewControl& view);

    /// The view shown maximized; null while the dock area is shown.
    [[nodiscard]] ImageViewControl* maximizedView() const noexcept;

    /// Shows the list of floating images in place of the dock area, or the dock area again; nothing while a view is
    /// maximized.
    void showFloatingImages(bool shown);
    [[nodiscard]] bool isFloatingImagesShown() const;

    /// The active view's chrome: the toolbar over the page, the strip under it and the binder that shows the view in
    /// them.
    [[nodiscard]] an::ui::AnFloatingToolbar* toolbar() const noexcept;
    [[nodiscard]] an::ui::AnHistoryStrip* historyStrip() const noexcept;
    [[nodiscard]] an::ui::shell::ViewChromeBinder& binder() const noexcept;

    /// Shows `view` in the toolbar and the strip; null shows none and hides the toolbar.
    void bindView(ImageViewControl* view);

    /// The part of the page the toolbar floats over: the dock area, the maximized view or the floating images' list.
    [[nodiscard]] QRect canvasArea() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void placeToolbar();

    QStackedWidget* m_stack = nullptr;
    KDDockWidgets::QtWidgets::MainWindow* m_dockArea = nullptr;
    QWidget* m_maximizedPage = nullptr;
    FloatingImagesPage* m_floatingImages = nullptr;
    ImageViewControl* m_maximized = nullptr;
    an::ui::AnFloatingToolbar* m_toolbar = nullptr;
    an::ui::AnHistoryStrip* m_strip = nullptr;
    std::unique_ptr<an::ui::shell::ViewChromeBinder> m_binder;
    std::unique_ptr<ToolbarClearance> m_clearance;
};

} // namespace an::ui::observatory
