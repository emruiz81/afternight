// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home: the place the Observatory window shows when no image is open.

#include "AfterNightShellObservatoryExport.h"

#include <QScrollArea>

class MainDashboard;

namespace an::ui::observatory
{

/// The page of the Home place: the application's dashboard - the actions it starts with, the recent files - in a page
/// that scrolls when the window is smaller than it.
class AFTERNIGHTSHELLOBSERVATORY_API HomePage final : public QScrollArea
{
public:
    explicit HomePage(QWidget* parent = nullptr);

    /// The dashboard the page shows; its requests are the window's to answer.
    [[nodiscard]] MainDashboard* dashboard() const noexcept;

private:
    MainDashboard* m_dashboard = nullptr;
};

} // namespace an::ui::observatory
