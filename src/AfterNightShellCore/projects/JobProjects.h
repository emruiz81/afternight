// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the project service is to a job that runs in the application - an integration: the project that is open, a
/// project for a job that runs with none, and what a run made, recorded in it.

#include "AfterNightShellCoreExport.h"

#include "app/JobProjects.h"
#include "projects/ProjectService.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace an::ui
{
class IGlobalSettingsProvider;
} // namespace an::ui

namespace an::ui::shell
{

/// Answers a job's questions about projects (`an::ui::IJobProjects`) from the project service.
///
/// **A project for a job that runs with none open.** When the settings ask for it (`projects/createForIntegrationJobs`,
/// on unless they say otherwise), a job about to run into a folder gets a project there: the folder's own, opened,
/// when it is a project already - so a folder gets one project, however many runs go into it - and otherwise a new one
/// made of the folder (`ProjectService::createInFolder`), named as the folder and stating that an integration job
/// made it. Nothing of what the job keeps there is moved. With the setting off, or with a project open already, none
/// is made; a project that cannot be made or opened is told (`Hooks::refused`) and the job runs outside any project.
///
/// **The object of a project made for a job** is the one its frames' OBJECT cards agree on, stated as read from those
/// cards. A card is read with its blanks trimmed and the blanks inside it made one, and two cards agree when they are
/// the same text without regard to case; the object is named as the first frame that states it writes it. A frame that
/// states none says nothing either way. Frames that name different objects give the project none - it is about
/// several - and so do frames none of which states one. A project that was there keeps the object it states.
///
/// **What a shell adds.** A project that was there and is opened for a job is opened as any project is: the shell is
/// told (`Hooks::opened`) and puts back what the project keeps - its workspaces, its images.
///
/// **What a run made.** A job is recorded in the open project by the name of its session and the folder it writes to
/// (`recordJob`), and the files its run made are recorded as the project's images (`addJobImages`), each once.
///
/// The service and the settings are the caller's and outlive it. It shows nothing. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API JobProjects final : public an::ui::IJobProjects
{
public:
    /// What a shell does about a project a job asked for.
    struct Hooks
    {
        /// A project that was there already was opened for a job.
        std::function<void()> opened;

        /// A project was made for a job.
        std::function<void()> made;

        /// A project could not be made or opened for a job: what the service answered.
        std::function<void(const ProjectOutcome& outcome)> refused;
    };

    explicit JobProjects(ProjectService& projects);
    ~JobProjects();

    JobProjects(const JobProjects&) = delete;
    JobProjects& operator=(const JobProjects&) = delete;
    JobProjects(JobProjects&&) = delete;
    JobProjects& operator=(JobProjects&&) = delete;

    /// Where the setting is read; null reads it as its default.
    void setSettings(an::ui::IGlobalSettingsProvider* settings) noexcept;

    void setHooks(Hooks hooks);

    /// The name a project made of a folder takes: the folder's own; "Integration" for a folder with none - a drive.
    [[nodiscard]] static QString projectNameFor(const QString& folder);

    [[nodiscard]] std::optional<an::ui::JobProject> openProject() const override;
    [[nodiscard]] std::optional<an::ui::JobProject> projectForJob(const QString& outputFolder,
                                                                  const QStringList& objectCards) override;
    void recordJob(const QString& name, const QString& outputFolder) override;
    void addJobImages(const QStringList& files) override;

private:
    ProjectService& m_projects;
    an::ui::IGlobalSettingsProvider* m_settings = nullptr;
    Hooks m_hooks;
};

} // namespace an::ui::shell
