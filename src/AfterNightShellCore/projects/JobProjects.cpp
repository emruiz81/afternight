// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/JobProjects.h"

#include "controls/IAppContext.h"
#include "controls/settings/ProjectSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <utility>

namespace an::ui::shell
{

namespace
{

[[nodiscard]] an::ui::JobProject seenByAJob(const ProjectService& projects)
{
    const ProjectDocument* const open = projects.current();
    an::ui::JobProject project;
    project.folder = projects.currentFolder();
    project.name = open != nullptr ? open->name : QString();
    project.jobsFolder = QDir(project.folder).filePath(QString::fromLatin1(kProjectIntegrationFolder));
    return project;
}

/// The object frames' OBJECT cards agree on, named as the first frame that states it writes it; none when they name
/// different objects or none states one. A card is read with its blanks trimmed and the ones inside it made one, and
/// two cards agree without regard to case.
[[nodiscard]] std::optional<ProjectObject> objectOfCards(const QStringList& objectCards)
{
    QString agreed;
    for (const QString& card : objectCards)
    {
        const QString object = card.simplified();
        if (object.isEmpty())
        {
            continue;
        }
        if (agreed.isEmpty())
        {
            agreed = object;
        }
        else if (object.compare(agreed, Qt::CaseInsensitive) != 0)
        {
            return std::nullopt;
        }
    }
    if (agreed.isEmpty())
    {
        return std::nullopt;
    }
    return ProjectObject{.name = agreed, .source = ProjectObjectSource::ObjectCard};
}

} // namespace

JobProjects::JobProjects(ProjectService& projects) : m_projects(projects) {}

JobProjects::~JobProjects() = default;

void JobProjects::setSettings(an::ui::IGlobalSettingsProvider* const settings) noexcept
{
    m_settings = settings;
}

void JobProjects::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
}

QString JobProjects::projectNameFor(const QString& folder)
{
    const QString name = QDir(QDir::cleanPath(folder)).dirName().trimmed();
    return name.isEmpty() ? QCoreApplication::translate("JobProjects", "Integration") : name;
}

std::optional<an::ui::JobProject> JobProjects::openProject() const
{
    if (!m_projects.isOpen())
    {
        return std::nullopt;
    }
    return seenByAJob(m_projects);
}

std::optional<an::ui::JobProject> JobProjects::projectForJob(const QString& outputFolder,
                                                             const QStringList& objectCards)
{
    // A job that runs inside a project asks for none; one that writes nowhere has no folder to make one of.
    if (m_projects.isOpen() || outputFolder.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    an::ui::IGlobalSettingsProvider* const settings = m_settings;
    const an::ui::ProjectSettings stored =
        an::ui::readProjectSettings([settings](const QString& key, const QString& fallback) {
            return settings != nullptr ? settings->select(key, fallback) : fallback;
        });
    if (!stored.createForIntegrationJobs)
    {
        return std::nullopt;
    }
    const QString folder = QDir::cleanPath(outputFolder);
    // A folder gets one project: the one it is already is opened, as any project is.
    if (ProjectService::isProjectFolder(folder))
    {
        const ProjectOutcome opened = m_projects.open(folder);
        if (!opened.ok())
        {
            if (m_hooks.refused)
            {
                m_hooks.refused(opened);
            }
            return std::nullopt;
        }
        if (m_hooks.opened)
        {
            m_hooks.opened();
        }
        return seenByAJob(m_projects);
    }
    ProjectCreateRequest request;
    request.name = projectNameFor(folder);
    request.compression = stored.historyCompression;
    request.origin = ProjectOrigin::IntegrationJob;
    request.object = objectOfCards(objectCards);
    const ProjectOutcome made = m_projects.createInFolder(folder, request);
    if (!made.ok())
    {
        if (m_hooks.refused)
        {
            m_hooks.refused(made);
        }
        return std::nullopt;
    }
    if (m_hooks.made)
    {
        m_hooks.made();
    }
    return seenByAJob(m_projects);
}

void JobProjects::recordJob(const QString& name, const QString& outputFolder)
{
    if (m_projects.isOpen())
    {
        (void)m_projects.addIntegration(name, outputFolder);
    }
}

void JobProjects::addJobImages(const QStringList& files)
{
    if (!m_projects.isOpen())
    {
        return;
    }
    for (const QString& file : files)
    {
        // A file the run names and did not leave is no image of the project.
        if (!file.isEmpty() && QFileInfo(file).isFile())
        {
            (void)m_projects.addImage(file, ProjectImageKind::Image);
        }
    }
}

} // namespace an::ui::shell
