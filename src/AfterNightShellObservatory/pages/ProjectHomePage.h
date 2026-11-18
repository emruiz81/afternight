// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Project Home: the Home place while a project is open - what the project's folder holds, and what to do from there.

#include "AfterNightShellObservatoryExport.h"

#include "projects/ProjectService.h"
#include "workspaces/WorkspaceMiniature.h"

#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QAbstractButton;
class QPlainTextEdit;
class QShowEvent;

namespace ImageIntegration
{
class IntegrationJobsModel;
} // namespace ImageIntegration

namespace an::ui
{
class AnTag;
} // namespace an::ui

namespace an::ui::shell
{
class ApplicationCore;
} // namespace an::ui::shell

namespace an::ui::observatory
{

/// What a workspace's tile says: the window's workspace as it is now.
struct WorkspaceTileFacts
{
    QString id;
    QString name;

    /// The project's images docked in it.
    int images = 0;

    /// Whether it is the workspace used last, which Continue shows.
    bool last = false;

    MiniatureLayout layout;
};

/// The page of Home while a project is open, which scrolls when the window is smaller than it.
///
/// **The header**: the project's picture - its image opened last - its name as the heading, its object with where it
/// came from, its folder with Show in File Browser, when it was made and from what, and its size. **The actions**:
/// Continue · <the workspace used last>, Add images..., New integration..., Project settings... and Close project.
/// **The cards**: Workspaces - a tile each, a miniature of its arrangement, its name, its images and arrangement, the
/// one used last marked, Continue or Open and Rename...; Images and sequences - a row each with its picture, its name,
/// its type, its size, its history as its steps and their size, where it is open and when its file changed;
/// Integrations - the project's jobs, each with what it holds, its status and Open; Storage - a bar of what the folder
/// holds and its legend, with Compact history... and Clean intermediate files..., which is offered while the project's
/// jobs keep frames a run can make again; Notes - the project's notes.md, saved as they are written.
///
/// The project is the open one of the core's project service. The storage is measured on the thread pool and shown
/// when it lands; the images' pictures and sizes are read off the GUI thread as Home's tiles are. What the window
/// knows - its workspaces, where each image is open, the views' pictures - it tells the page; what the page asks for,
/// it asks through its hooks.
class AFTERNIGHTSHELLOBSERVATORY_API ProjectHomePage final : public QScrollArea
{
    W_OBJECT(ProjectHomePage)

public:
    /// What the page asks of the window.
    struct Hooks
    {
        std::function<QString()> continueLabel;
        std::function<void()> continueProject;
        std::function<void()> addImages;
        std::function<void()> newIntegration;
        std::function<void()> projectSettings;
        std::function<void()> closeProject;
        std::function<void()> compactHistory;
        std::function<void()> cleanIntermediate;
        std::function<void(const QString& workspaceId)> showWorkspace;
        std::function<void(const QString& workspaceId)> renameWorkspace;
        std::function<void(const QString& databasePath, const QString& session)> openJob;
    };

    /// A workspace's tile.
    class AFTERNIGHTSHELLOBSERVATORY_API WorkspaceTile final : public QWidget
    {
    public:
        WorkspaceTile(const WorkspaceTileFacts& facts, QWidget* parent);
        ~WorkspaceTile() override;

        [[nodiscard]] QString id() const;
        [[nodiscard]] QString name() const;
        /// "2 images · 2 × 1 split".
        [[nodiscard]] QString factsText() const;
        [[nodiscard]] bool isLast() const;
        [[nodiscard]] WorkspaceMiniature* miniature() const;
        /// Continue on the workspace used last, Open on the others.
        [[nodiscard]] QAbstractButton* openButton() const;
        [[nodiscard]] QAbstractButton* renameButton() const;

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    /// A row of the images' table, as it reads.
    struct ImageRow
    {
        QString imageId;
        QString name;
        QString type;
        QString dimensions;
        QString history;
        QString state;
        QString modified;
    };

    ProjectHomePage(an::ui::shell::ApplicationCore& core, ImageIntegration::IntegrationJobsModel& jobs, Hooks hooks,
                    QWidget* parent = nullptr);
    ~ProjectHomePage() override;

    /// Reads again what the open project holds: its header, its images, its integrations and its notes at once, its
    /// storage on the thread pool.
    void refresh();

    /// The window's workspaces, as they are now.
    void setWorkspaces(const std::vector<WorkspaceTileFacts>& workspaces);

    /// Where each image of the project is open: its id, and the workspace's name; one not listed is closed.
    void setImagesOpenIn(const std::map<QString, QString>& openIn);

    /// A view's picture, by its name, for the miniatures.
    void setViewPicture(const QString& view, const QImage& picture);

    /// What shows the folder in the system's file browser; `revealInFileManager` unless set.
    void setFileReveal(std::function<bool(const QString&)> reveal);

    /// Writes the notes into notes.md now; they are written as they change, a moment after the last keystroke.
    void saveNotes();

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    // ---- Its parts, for the window and the cases

    [[nodiscard]] QString headingText() const;
    [[nodiscard]] an::ui::AnTag* objectTag() const;
    [[nodiscard]] QString objectCaption() const;
    [[nodiscard]] QString folderText() const;
    [[nodiscard]] QString createdText() const;
    [[nodiscard]] QString sizeText() const;
    [[nodiscard]] QAbstractButton* showInFileBrowserButton() const;
    [[nodiscard]] QAbstractButton* continueButton() const;
    [[nodiscard]] QAbstractButton* addImagesButton() const;
    [[nodiscard]] QAbstractButton* newIntegrationButton() const;
    [[nodiscard]] QAbstractButton* projectSettingsButton() const;
    [[nodiscard]] QAbstractButton* closeProjectButton() const;
    [[nodiscard]] std::vector<WorkspaceTile*> workspaceTiles() const;
    [[nodiscard]] std::vector<ImageRow> imageRows() const;
    /// The project's jobs, each as "name · status".
    [[nodiscard]] QStringList integrationRows() const;
    /// The storage shown: none until the measurement lands.
    [[nodiscard]] const std::optional<an::ui::shell::ProjectStorageReport>& storage() const noexcept;
    /// The storage's legend, each part's name and bytes: the undo history, the masters and stacked images, the
    /// calibrated and registered frames, the reports, layouts and notes, and what is free on the drive.
    [[nodiscard]] std::vector<std::pair<QString, std::uint64_t>> storageLegend() const;
    [[nodiscard]] QAbstractButton* compactHistoryButton() const;
    /// Clean intermediate files...: enabled while the storage shown counts frames a run can make again.
    [[nodiscard]] QAbstractButton* cleanIntermediateButton() const;
    [[nodiscard]] QPlainTextEdit* notes() const;

    /// The storage of the open project was measured: what the window asks of it - a question about a heavy history.
    void storageMeasured(const an::ui::shell::ProjectStorageReport& report) W_SIGNAL(storageMeasured, report);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void rebuildHeader();
    void rebuildImages();
    void rebuildIntegrations();
    void rebuildWorkspaces();
    void showStorage();
    void loadNotes();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory

W_REGISTER_ARGTYPE(an::ui::shell::ProjectStorageReport)
