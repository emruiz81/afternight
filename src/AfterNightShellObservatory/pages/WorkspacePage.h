// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A workspace's page: its dock area, or the one view it shows maximized.

#include "AfterNightShellObservatoryExport.h"

#include <QWidget>

class ImageViewControl;
class QStackedWidget;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui::observatory
{

/// The page of a workspace in the window's centre: the workspace's dock area, where its views split, stack as tabs and
/// float as they always did; or one of them maximized over the whole page, its dock keeping its place in the area
/// until it is restored.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspacePage final : public QWidget
{
public:
    /// Shows the dock area `dockArea`, which the shell's window made and owns from here as the page's child.
    WorkspacePage(KDDockWidgets::QtWidgets::MainWindow& dockArea, QWidget* parent = nullptr);

    /// The dock area the page shows.
    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* dockArea() const noexcept;

    /// Shows `view` over the whole page; its dock keeps a stand-in until it is restored. A view maximized already is
    /// restored first.
    void maximize(ImageViewControl& view);
    /// Puts the maximized view back in its dock and shows the dock area; nothing when `view` is not the one maximized.
    void restore(ImageViewControl& view);

    /// The view shown maximized; null while the dock area is shown.
    [[nodiscard]] ImageViewControl* maximizedView() const noexcept;

private:
    QStackedWidget* m_stack = nullptr;
    KDDockWidgets::QtWidgets::MainWindow* m_dockArea = nullptr;
    QWidget* m_maximizedPage = nullptr;
    ImageViewControl* m_maximized = nullptr;
};

} // namespace an::ui::observatory
