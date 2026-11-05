// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QJsonObject>
#include <QDateTime>
#include <QStringList>
#include <vector>
#include <memory>

#include <verdigris/wobjectdefs.h>

class AFTERNIGHTPROCESSES_API WorkflowHistoryModel : public QAbstractItemModel
{
    W_OBJECT(WorkflowHistoryModel)

public:
    enum Roles
    {
        ExecutionIdRole = Qt::UserRole + 1,
        TimestampRole,
        ModeRole,
        StatusRole,
        DurationRole,
        GroupCountRole,
        MasterCountRole,
        ManifestPathRole,
        ReportPathRole,
        LogPathRole,
        RunDirPathRole,
        IsExpandedRole,
        CanExpandRole,

        // Group item roles
        GroupKeyRole,
        ThumbnailPathRole,
        OutputSizeRole,
        FrameCountRole,
        IsGroupItemRole,
        OutputPathRole,
        OutputFilenameRole,
        RawGroupKeyRole,
        ResolutionRole,
        ObjectRole,
        FilterRole,
        ExposureRole,
        RegistrationGroupKeyRole,
        ArchivedOutputPathsRole,
        MasterContextRole,
        CanCompareMasterRole
    };

    explicit WorkflowHistoryModel(QObject* parent = nullptr);
    ~WorkflowHistoryModel() override;

    void setHistoryDirectory(const QString& path);
    void refresh();

    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

private:
    struct GroupItem
    {
        QString groupKey;
        QString rawGroupKey;
        QString outputPath;
        QString outputFilename;
        qint64 outputBytes = 0;
        QString previewPath;
        int lightFrameCount = 0;
        int durationMs = 0;
        bool success = false;
        QString resolution;
        QString object;
        QString filter;
        QString exposure;
        QString registrationGroupKey;
        QStringList archivedOutputPaths;
        QString masterContext;

        // Parent pointer not strictly needed if we store hierarchy differently,
        // but useful for recursive structures.
    };

    struct WorkflowItem
    {
        QString executionId;
        QDateTime timestamp;
        QString mode;
        QString status; // success, partial, failed, cancelled
        int totalDurationMs = 0;
        int warningCount = 0;
        QString manifestPath;
        QString reportPath;
        QString logPath;
        QString runDirPath;
        bool expanded = false;

        std::vector<GroupItem> groups;
    };

    QString m_historyDir;
    std::vector<WorkflowItem> m_workflows;
    QHash<QString, bool> m_expandedStateByExecutionId;

    void loadWorkflows();
    [[nodiscard]] bool groupHasComparableMaster(int workflowRow, int groupRow) const;
    [[nodiscard]] static bool groupsCanCompare(const GroupItem& source, const GroupItem& candidate);
};
