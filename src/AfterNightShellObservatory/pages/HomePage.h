// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home: the place the Observatory window shows when no image is open, and that the rail reaches while images are.

#include "AfterNightShellObservatoryExport.h"

#include <QScrollArea>
#include <QString>
#include <verdigris/wobjectdefs.h>

#include <memory>
#include <vector>

class QShowEvent;

namespace an::ui
{
class HomeContinueRow;
class HomeProjectsRow;
class HomeQuickActions;
class HomeRecentGrid;
class HomeText;
class HomeWelcome;
} // namespace an::ui

namespace an::ui::shell
{
struct RecentProject;
} // namespace an::ui::shell

namespace an::ui::observatory
{

/// The page of the Home place, which scrolls when the window is smaller than it: the date and the heading with the
/// quick actions at its right - the dashboard's, then New project, then More; under them, with no recent file, the
/// welcome, whose line about projects offers New project... too, and with recent files or projects used last the
/// Continue row - the featured image, the last project and the last sequence - the Projects row and the recent images
/// and sequences. Its parts are Controls' Home parts, over the dashboard's settings, recent files and
/// thumbnails; what they ask for is asked by the page in the dashboard's signals, which are the window's to answer.
class AFTERNIGHTSHELLOBSERVATORY_API HomePage final : public QScrollArea
{
    W_OBJECT(HomePage)

public:
    /// The id of the quick action this page adds to the dashboard's: New project.
    static constexpr char kNewProjectAction[] = "new_project";

    explicit HomePage(QWidget* parent = nullptr);
    ~HomePage() override;

    /// Reads the recent files again - after one was opened, removed or the list was changed; the window calls it where
    /// the file manager says the list or a thumbnail changed. Showing the page reads nothing.
    void reloadRecentFiles();

    /// The recent files were cleared: the pins go with them, and the welcome is shown.
    void recentFilesCleared();

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    /// The projects used last, most recent first: what each holds is read from its folder on the thread pool, and the
    /// last project's card and the Projects row show them when it lands.
    void setRecentProjects(const std::vector<an::ui::shell::RecentProject>& recent);

    /// Whether the page shows the welcome: there is no recent file and no project used last.
    [[nodiscard]] bool showsWelcome() const;

    [[nodiscard]] an::ui::HomeQuickActions* quickActions() const noexcept;
    [[nodiscard]] an::ui::HomeContinueRow* continueRow() const noexcept;
    [[nodiscard]] an::ui::HomeRecentGrid* recentGrid() const noexcept;
    [[nodiscard]] an::ui::HomeWelcome* welcome() const noexcept;
    [[nodiscard]] an::ui::HomeProjectsRow* projectsRow() const noexcept;

    void openFileRequested() W_SIGNAL(openFileRequested);
    void newSequenceRequested() W_SIGNAL(newSequenceRequested);
    void imageIntegrationRequested() W_SIGNAL(imageIntegrationRequested);
    /// New project was asked for, by the quick action or by the welcome's line.
    void newProjectRequested() W_SIGNAL(newProjectRequested);
    void openExtensionManagerRequested() W_SIGNAL(openExtensionManagerRequested);
    void clearRecentFilesRequested() W_SIGNAL(clearRecentFilesRequested);
    void systemInformationRequested() W_SIGNAL(systemInformationRequested);
    void openHelpRequested() W_SIGNAL(openHelpRequested);
    void openRecentFileRequested(const QString& filePath) W_SIGNAL(openRecentFileRequested, filePath);
    void removeRecentFileRequested(const QString& filePath) W_SIGNAL(removeRecentFileRequested, filePath);
    /// A project used last was asked to be opened, by its card or the Continue row's; another, by the Open project...
    /// tile.
    void openProjectRequested(const QString& folder) W_SIGNAL(openProjectRequested, folder);
    void openOtherProjectRequested() W_SIGNAL(openOtherProjectRequested);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void showRows();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
