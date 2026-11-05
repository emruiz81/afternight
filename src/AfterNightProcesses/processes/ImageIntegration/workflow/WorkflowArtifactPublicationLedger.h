// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"

#include <QString>

#include <mutex>
#include <optional>
#include <unordered_map>

class WorkflowArtifactPublicationLedger final
{
public:
    [[nodiscard]] bool recordPublishedArtifact(const FinalCommittedArtifact& artifact)
    {
        if (!artifact.matchesFileSnapshot())
        {
            return false;
        }
        const FileRevisionStamp revision = artifact.revisionStamp();
        ExecutionPreparationSnapshot::FileStamp stamp{
            revision.path, true, true, revision.sizeBytes, revision.modifiedAtMs, revision.identityToken};
        std::lock_guard lock(m_mutex);
        m_stamps[FrameId(revision.path).canonicalPath()] = std::move(stamp);
        return true;
    }

    [[nodiscard]] bool recordPublishedFile(const QString& path)
    {
        const auto revision = ArtifactTracker::captureFileRevision(path);
        if (!revision.has_value())
        {
            return false;
        }
        ExecutionPreparationSnapshot::FileStamp stamp{
            revision->path, true, true, revision->sizeBytes, revision->modifiedAtMs, revision->identityToken};
        std::lock_guard lock(m_mutex);
        m_stamps[FrameId(path).canonicalPath()] = std::move(stamp);
        return true;
    }

    [[nodiscard]] std::optional<ExecutionPreparationSnapshot::FileStamp> publishedFileStamp(const QString& path) const
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_stamps.find(FrameId(path).canonicalPath());
        return found == m_stamps.cend() ? std::nullopt : std::optional(found->second);
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<QString, ExecutionPreparationSnapshot::FileStamp> m_stamps;
};
