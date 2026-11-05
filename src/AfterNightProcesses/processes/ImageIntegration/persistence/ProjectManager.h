// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

class QSettings;

/// What reading a project file gave: the model's JSON, or why the file was refused.
struct ProjectReadResult
{
    /// The model's JSON, or nothing when the file was refused.
    std::optional<QJsonObject> model;

    /// Why the file was refused, naming it; empty when it was read.
    QString refusal;
};

class AFTERNIGHTPROCESSES_API ProjectManager
{
public:
    static constexpr int kMaxRecentProjects = 10;

    /// Reads a project file, as Import Project and a recent project open it: a JSON project file at the format the
    /// project writer writes. Any other file - another format's project, one that states none, a file that is not a
    /// JSON project - is refused with a message naming it, and is not changed.
    ///
    /// @param projectPath the file.
    /// @return the model's JSON, or the refusal.
    [[nodiscard]] static ProjectReadResult readProject(const QString& projectPath);

    void setProjectName(const QString& name);
    [[nodiscard]] const QString& projectName() const noexcept;

    void openProject(const QString& projectPath);
    [[nodiscard]] const QString& currentProjectPath() const noexcept;
    [[nodiscard]] const QStringList& recentProjects() const noexcept;

    void clearRecentProjects();
    void removeRecentProject(const QString& projectPath);
    void loadFromSettings(QSettings& settings);
    void saveToSettings(QSettings& settings) const;

    /// Writes the model's JSON as a format 3 session document (`SessionDocument.h`) to a project file, replacing it
    /// whole; a value no table knows refuses the write with the field named.
    bool saveAs(const QJsonObject& modelJson, const QString& newProjectPath, QString* errorMessage = nullptr);

private:
    void touchRecentProject(const QString& projectPath);

    QString m_projectName;
    QString m_currentProjectPath;
    QStringList m_recentProjects;
};
