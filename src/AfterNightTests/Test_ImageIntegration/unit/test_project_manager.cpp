// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/persistence/ProjectManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QIODevice>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

namespace
{
QString readRepoSourceFile(const QString& relativePath)
{
    QStringList seeds = {QDir::currentPath()};
    if (QCoreApplication::instance())
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }
    seeds.append(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());

    for (const QString& seed : seeds)
    {
        QDir dir(seed);
        while (true)
        {
            const QString candidate = dir.filePath(relativePath);
            if (QFileInfo::exists(candidate))
            {
                QFile file(candidate);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    return QString::fromUtf8(file.readAll());
                }
            }

            if (!dir.cdUp())
            {
                break;
            }
        }
    }

    return {};
}
} // namespace

TEST(ProjectManager, CanNameProject)
{
    ProjectManager pm;
    pm.setProjectName(QStringLiteral("M42 Winter 2025"));
    EXPECT_EQ(pm.projectName(), QStringLiteral("M42 Winter 2025"));
}

TEST(ProjectManager, TracksRecentProjects)
{
    ProjectManager pm;
    pm.openProject(QStringLiteral("project1.json"));
    pm.openProject(QStringLiteral("project2.json"));
    pm.openProject(QStringLiteral("project3.json"));

    ASSERT_EQ(pm.recentProjects().size(), 3);
    EXPECT_TRUE(pm.recentProjects().front().contains("project3"));
}

TEST(ProjectManager, RecentProjectsLimitedTo10)
{
    ProjectManager pm;
    for (int i = 0; i < 15; ++i)
    {
        pm.openProject(QStringLiteral("project%1.json").arg(i));
    }

    EXPECT_EQ(pm.recentProjects().size(), 10);
}

TEST(ProjectManager, RemoveRecentProjectPrunesList)
{
    ProjectManager pm;
    pm.openProject(QStringLiteral("project1.json"));
    pm.openProject(QStringLiteral("project2.json"));
    pm.openProject(QStringLiteral("project3.json"));

    pm.removeRecentProject(QStringLiteral("project2.json"));
    EXPECT_FALSE(pm.recentProjects().contains(QStringLiteral("project2.json")));
    EXPECT_EQ(pm.recentProjects().size(), 2);
}

TEST(ProjectManager, CanPersistRecentProjectsToSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString iniPath = dir.filePath(QStringLiteral("settings.ini"));
    {
        QSettings settings(iniPath, QSettings::IniFormat);
        ProjectManager pm;
        pm.openProject(QStringLiteral("A.json"));
        pm.openProject(QStringLiteral("B.json"));
        pm.saveToSettings(settings);
    }

    {
        QSettings settings(iniPath, QSettings::IniFormat);
        ProjectManager loaded;
        loaded.loadFromSettings(settings);

        ASSERT_EQ(loaded.recentProjects().size(), 2);
        EXPECT_EQ(loaded.recentProjects().front(), QStringLiteral("B.json"));
        EXPECT_EQ(loaded.recentProjects().back(), QStringLiteral("A.json"));
    }
}

TEST(ProjectManager, LoadFromSettingsNormalizesAndLimits)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString iniPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("ImageIntegration"));
    settings.setValue(QStringLiteral("recentProjects"),
                      QStringList{QStringLiteral("  A.json  "), QStringLiteral("B.json"), QStringLiteral("A.json"),
                                  QStringLiteral(""), QStringLiteral("C.json")});
    settings.endGroup();

    ProjectManager pm;
    pm.loadFromSettings(settings);

    ASSERT_EQ(pm.recentProjects().size(), 3);
    EXPECT_EQ(pm.recentProjects()[0], QStringLiteral("A.json"));
    EXPECT_EQ(pm.recentProjects()[1], QStringLiteral("B.json"));
    EXPECT_EQ(pm.recentProjects()[2], QStringLiteral("C.json"));
}

TEST(ProjectManager, SaveAsCreatesNewProject)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    ProjectManager pm;
    pm.openProject(dir.filePath(QStringLiteral("original.json")));

    QJsonObject json;
    json.insert(QStringLiteral("version"), 2);

    const QString copyPath = dir.filePath(QStringLiteral("copy.json"));
    QString err;
    ASSERT_TRUE(pm.saveAs(json, copyPath, &err)) << err.toStdString();

    EXPECT_EQ(pm.currentProjectPath(), copyPath);
    EXPECT_TRUE(QFile::exists(copyPath));
}

TEST(PersistenceSourceGuards, JsonPersistenceUsesAtomicSaveFiles)
{
    const QString projectManager = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/persistence/ProjectManager.cpp"));
    const QString presetManager = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/persistence/PresetManager.cpp"));
    const QString runStateStore = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowRunStateStore.cpp"));
    const QString archiver = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowArchiver.cpp"));
    const QString imageIntegrationProcess = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));

    ASSERT_FALSE(projectManager.isEmpty());
    ASSERT_FALSE(presetManager.isEmpty());
    ASSERT_FALSE(runStateStore.isEmpty());
    ASSERT_FALSE(archiver.isEmpty());
    ASSERT_FALSE(imageIntegrationProcess.isEmpty());

    EXPECT_TRUE(projectManager.contains(QStringLiteral("QSaveFile f(newProjectPath)")));
    EXPECT_FALSE(projectManager.contains(QStringLiteral("QFile f(newProjectPath)")));

    EXPECT_TRUE(presetManager.contains(QStringLiteral("QSaveFile file(*presetPath)")));
    EXPECT_FALSE(presetManager.contains(QStringLiteral("QDir(m_storageDir).filePath(name + \".json\")")));

    EXPECT_TRUE(
        runStateStore.contains(QStringLiteral("QSaveFile file(manifestPath(scope, manifest.executionScopeKey))")));
    EXPECT_TRUE(runStateStore.contains(QStringLiteral("QSaveFile file(metadataPath())")));

    EXPECT_TRUE(archiver.contains(QStringLiteral("writeAtomicFile")));
    EXPECT_FALSE(archiver.contains(QStringLiteral("QFile manifestFile")));
    EXPECT_FALSE(archiver.contains(QStringLiteral("QFile groupSummaryFile")));
    EXPECT_FALSE(archiver.contains(QStringLiteral("QFile reportFile")));
    EXPECT_FALSE(archiver.contains(QStringLiteral("QFile logFile")));

    EXPECT_TRUE(imageIntegrationProcess.contains(QStringLiteral("QSaveFile f(file)")));
}
