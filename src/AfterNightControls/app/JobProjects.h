// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the application's projects are to a job that runs in it - an integration - and how the job reaches them.
///
/// A job knows nothing of how projects are kept: a process asks this interface which project is open and where that
/// project keeps its jobs, asks for a project when it is about to run with none open, and says what a run made. An
/// application that offers projects answers it through its context (`IAppContext::getJobProjects`); in one that
/// offers none there is nothing to ask (`jobProjects()` is null) and a job runs as it does outside any project.

#include "AfterNightControlsExport.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace an::ui
{

/// A project, as a job that runs in it sees it.
struct JobProject
{
    /// The project's folder.
    QString folder;

    /// The project's name.
    QString name;

    /// The folder the project keeps its jobs in: the database of its sessions, and the folders of the jobs made in
    /// it.
    QString jobsFolder;

    [[nodiscard]] bool operator==(const JobProject&) const = default;
};

/// What a job asks of the application's projects. Used on the GUI thread.
class AFTERNIGHTCONTROLS_API IJobProjects
{
public:
    /// The project that is open; none with none open.
    [[nodiscard]] virtual std::optional<JobProject> openProject() const = 0;

    /// A job is about to run into a folder with no project open. When the settings ask for it, the folder becomes a
    /// project - or, when it is one already, that project is opened - and is the open project from then on. The job's
    /// own files in the folder are not touched. A project made for the job is about the object its frames' OBJECT
    /// cards agree on; one that was there keeps the object it states.
    ///
    /// @param outputFolder the folder the job's runs write to.
    /// @param objectCards the OBJECT card of each of the job's light frames, an empty text for a frame that states
    ///        none.
    /// @return the project the job runs in from now on; none when the settings do not ask for one, when a project
    ///         is open already, or when the folder's project could not be made or opened - which is said to the
    ///         person, and the job runs outside any project.
    [[nodiscard]] virtual std::optional<JobProject> projectForJob(const QString& outputFolder,
                                                                  const QStringList& objectCards) = 0;

    /// Records a job as one of the open project's, once: by the name its session has and the folder its runs write
    /// to. Nothing with no project open.
    virtual void recordJob(const QString& name, const QString& outputFolder) = 0;

    /// Records files a run of a job made - its masters, its stacked images - as images of the open project, each
    /// once. Nothing with no project open.
    virtual void addJobImages(const QStringList& files) = 0;

protected:
    ~IJobProjects() = default;
};

/// The application's projects, as a job reaches them: what the application's context answers
/// (`IAppContext::getJobProjects`). Null in an application that offers no projects, and with no context. GUI thread.
[[nodiscard]] AFTERNIGHTCONTROLS_API IJobProjects* jobProjects();

} // namespace an::ui
