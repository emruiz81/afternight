// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProjectManager.h"

#include "SessionDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>

ProjectReadResult ProjectManager::readProject(const QString& projectPath)
{
    ProjectReadResult result;
    const QString shown = QDir::toNativeSeparators(projectPath);
    if (!projectPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
    {
        result.refusal = QObject::tr("'%1' is not a project file: Import Project reads a JSON project file, as Export "
                                     "Project writes it.")
                             .arg(shown);
        return result;
    }
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.refusal = QObject::tr("The project file '%1' could not be opened for reading.").arg(shown);
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isNull() || !document.isObject())
    {
        result.refusal = QObject::tr("'%1' is not a JSON project file.").arg(shown);
        return result;
    }
    // A project is read only at the format its writer writes: one of another format, or one that states none, is
    // refused naming the file, the format found and the one this version reads.
    const ImageIntegration::SessionDocumentResult read =
        ImageIntegration::readSessionDocument(document.object(), QStringLiteral("project '%1'").arg(shown));
    if (!read.ok())
    {
        result.refusal = read.refusal;
        return result;
    }
    result.model = *read.document;
    return result;
}

void ProjectManager::setProjectName(const QString& name)
{
    m_projectName = name.trimmed();
}

const QString& ProjectManager::projectName() const noexcept
{
    return m_projectName;
}

void ProjectManager::touchRecentProject(const QString& projectPath)
{
    const QString canonical = QFileInfo(projectPath).canonicalFilePath();
    const QString key = canonical.isEmpty() ? projectPath : canonical;

    m_recentProjects.removeAll(key);
    m_recentProjects.prepend(key);

    while (m_recentProjects.size() > kMaxRecentProjects)
    {
        m_recentProjects.removeLast();
    }
}

void ProjectManager::openProject(const QString& projectPath)
{
    m_currentProjectPath = projectPath;
    touchRecentProject(projectPath);

    if (m_projectName.isEmpty())
    {
        m_projectName = QFileInfo(projectPath).completeBaseName();
    }
}

const QString& ProjectManager::currentProjectPath() const noexcept
{
    return m_currentProjectPath;
}

const QStringList& ProjectManager::recentProjects() const noexcept
{
    return m_recentProjects;
}

void ProjectManager::clearRecentProjects()
{
    m_recentProjects.clear();
}

void ProjectManager::removeRecentProject(const QString& projectPath)
{
    const QString canonical = QFileInfo(projectPath).canonicalFilePath();
    const QString key = canonical.isEmpty() ? projectPath : canonical;
    m_recentProjects.removeAll(key);
}

void ProjectManager::loadFromSettings(QSettings& settings)
{
    settings.beginGroup(QStringLiteral("ImageIntegration"));
    const QStringList stored = settings.value(QStringLiteral("recentProjects")).toStringList();
    settings.endGroup();

    QStringList normalized;
    normalized.reserve(stored.size());
    for (const QString& path : stored)
    {
        const QString trimmed = path.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }
        if (normalized.contains(trimmed))
        {
            continue;
        }
        normalized.push_back(trimmed);
        if (normalized.size() >= kMaxRecentProjects)
        {
            break;
        }
    }

    m_recentProjects = normalized;
}

void ProjectManager::saveToSettings(QSettings& settings) const
{
    settings.beginGroup(QStringLiteral("ImageIntegration"));
    settings.setValue(QStringLiteral("recentProjects"), m_recentProjects);
    settings.endGroup();
}

bool ProjectManager::saveAs(const QJsonObject& modelJson, const QString& newProjectPath, QString* errorMessage)
{
    QFileInfo info(newProjectPath);
    if (info.absoluteDir().exists() == false)
    {
        if (!QDir().mkpath(info.absolutePath()))
        {
            if (errorMessage)
            {
                *errorMessage = QObject::tr("Failed to create directory: %1").arg(info.absolutePath());
            }
            return false;
        }
    }

    QSaveFile f(newProjectPath);
    if (!f.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Could not open file for writing.");
        }
        return false;
    }

    const ImageIntegration::SessionDocumentResult document = ImageIntegration::writeSessionDocument(
        modelJson, QStringLiteral("project '%1'").arg(QFileInfo(newProjectPath).completeBaseName()));
    if (!document.ok())
    {
        f.cancelWriting();
        if (errorMessage)
        {
            *errorMessage = document.refusal;
        }
        return false;
    }
    const QByteArray payload = QJsonDocument(*document.document).toJson();
    if (f.write(payload) != payload.size() || !f.commit())
    {
        f.cancelWriting();
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Could not commit file to disk.");
        }
        return false;
    }

    m_currentProjectPath = newProjectPath;
    touchRecentProject(newProjectPath);
    if (m_projectName.isEmpty())
    {
        m_projectName = QFileInfo(newProjectPath).completeBaseName();
    }
    return true;
}
