// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace ImageIntegration
{
inline constexpr qint64 kArchiveCloneChunkBytes = 64LL * 1024LL * 1024LL;

enum class ArchiveSelectedTransport
{
    None,
    OrdinaryCopy,
    WindowsBlockClone,
    LinuxReflink
};

struct ArchiveCloneExtent
{
    qint64 sourceOffset = 0;
    qint64 destinationOffset = 0;
    qint64 byteCount = 0;
};

/**
 * @brief Plan the aligned prefix of a clone as bounded, checked extents.
 *
 * Any final unaligned bytes are intentionally excluded and must be copied by
 * the platform backend according to its API contract.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<ArchiveCloneExtent>
planArchiveCloneExtents(qint64 logicalSizeBytes, qint64 alignmentBytes,
                        qint64 maximumChunkBytes = kArchiveCloneChunkBytes);

enum class ArchiveCloneAttemptStatus
{
    Succeeded,
    Unsupported,
    CrossVolume,
    PermissionDenied,
    UnsupportedExtentAlignment,
    CapabilityFailure,
    SourceChanged,
    Failed
};

class ArchiveCloneRequest
{
public:
    ArchiveCloneRequest(const FinalCommittedArtifact& immutableSource, QString temporaryDestinationPath)
        : m_immutableSource(immutableSource), m_temporaryDestinationPath(std::move(temporaryDestinationPath))
    {
    }

    [[nodiscard]] const FinalCommittedArtifact& immutableSource() const noexcept
    {
        return m_immutableSource.get();
    }

    [[nodiscard]] const QString& temporaryDestinationPath() const noexcept
    {
        return m_temporaryDestinationPath;
    }

private:
    std::reference_wrapper<const FinalCommittedArtifact> m_immutableSource;
    QString m_temporaryDestinationPath;
};

struct ArchiveCloneAttemptResult
{
    ArchiveCloneAttemptStatus status = ArchiveCloneAttemptStatus::Failed;
    QString detail;
    std::uint64_t logicalBytesCloned = 0;
    std::uint64_t ordinaryCopyBytes = 0;
    // Retains the pre-Linux injected-backend behavior when omitted.
    ArchiveSelectedTransport successfulTransport = ArchiveSelectedTransport::WindowsBlockClone;
};

enum class ArchiveCopyAttemptStatus
{
    Succeeded,
    Failed
};

enum class ArchiveCopySourceValidation
{
    NotRequested,
    StableImmutableSource,
    ImmutableSourceChanged
};

class ArchiveCopyRequest
{
public:
    ArchiveCopyRequest(QString sourcePath, QString temporaryDestinationPath,
                       const FinalCommittedArtifact* immutableSource = nullptr)
        : m_sourcePath(std::move(sourcePath)), m_temporaryDestinationPath(std::move(temporaryDestinationPath)),
          m_immutableSource(immutableSource)
    {
    }

    [[nodiscard]] const QString& sourcePath() const noexcept
    {
        return m_sourcePath;
    }

    [[nodiscard]] const QString& temporaryDestinationPath() const noexcept
    {
        return m_temporaryDestinationPath;
    }

    [[nodiscard]] const FinalCommittedArtifact* immutableSource() const noexcept
    {
        return m_immutableSource;
    }

private:
    QString m_sourcePath;
    QString m_temporaryDestinationPath;
    const FinalCommittedArtifact* m_immutableSource = nullptr;
};

struct ArchiveCopyAttemptResult
{
    ArchiveCopyAttemptStatus status = ArchiveCopyAttemptStatus::Failed;
    QString detail;
    std::uint64_t copiedBytes = 0;
    ArchiveCopySourceValidation sourceValidation = ArchiveCopySourceValidation::NotRequested;
};

enum class ArchivePublishAttemptStatus
{
    Succeeded,
    Failed
};

class ArchivePublishRequest
{
public:
    ArchivePublishRequest(QString candidatePath, QString destinationPath)
        : m_candidatePath(std::move(candidatePath)), m_destinationPath(std::move(destinationPath))
    {
    }

    [[nodiscard]] const QString& candidatePath() const noexcept
    {
        return m_candidatePath;
    }

    [[nodiscard]] const QString& destinationPath() const noexcept
    {
        return m_destinationPath;
    }

private:
    QString m_candidatePath;
    QString m_destinationPath;
};

struct ArchivePublishAttemptResult
{
    ArchivePublishAttemptStatus status = ArchivePublishAttemptStatus::Failed;
    QString detail;
};

struct ArchiveTransportOperations
{
    /**
     * Built-in clone backends bind one opened source handle to the committed artifact
     * before and after the kernel clone operation; the candidate is never re-read.
     */
    using CloneOperation = std::function<ArchiveCloneAttemptResult(const ArchiveCloneRequest&)>;
    /**
     * A successful immutable-source copy must return StableImmutableSource only after
     * validating one opened source handle before and after the copy. The handle must
     * remain bound to ArchiveCopyRequest::immutableSource() for the whole transfer.
     */
    using CopyOperation = std::function<ArchiveCopyAttemptResult(const ArchiveCopyRequest&)>;
    using PublishOperation = std::function<ArchivePublishAttemptResult(const ArchivePublishRequest&)>;

    CloneOperation clone;
    CopyOperation copy;
    PublishOperation publish;
};

/**
 * @brief Typed source authority and destination for one archive payload.
 *
 * Only forImmutableArtifact() authorizes clone optimization. The ordinary-copy
 * factory deliberately carries no path/size/timestamp data that can be
 * reinterpreted as immutable authority.
 */
class AFTERNIGHTPROCESSES_API ArchiveFileTransportRequest
{
public:
    [[nodiscard]] static ArchiveFileTransportRequest
    forImmutableArtifact(const FinalCommittedArtifact& artifact, QString destinationPath,
                         WorkflowExecutionCounterAccumulator* counters = nullptr);

    [[nodiscard]] static ArchiveFileTransportRequest
    forOrdinaryCopy(QString sourcePath, QString destinationPath,
                    WorkflowExecutionCounterAccumulator* counters = nullptr);

    [[nodiscard]] const QString& sourcePath() const noexcept
    {
        return m_sourcePath;
    }

    [[nodiscard]] const QString& destinationPath() const noexcept
    {
        return m_destinationPath;
    }

    [[nodiscard]] const FinalCommittedArtifact* immutableArtifact() const noexcept
    {
        return m_immutableArtifact ? &*m_immutableArtifact : nullptr;
    }

    [[nodiscard]] WorkflowExecutionCounterAccumulator* counters() const noexcept
    {
        return m_counters;
    }

private:
    ArchiveFileTransportRequest(QString sourcePath, QString destinationPath,
                                std::optional<FinalCommittedArtifact> immutableArtifact,
                                WorkflowExecutionCounterAccumulator* counters)
        : m_sourcePath(std::move(sourcePath)), m_destinationPath(std::move(destinationPath)),
          m_immutableArtifact(std::move(immutableArtifact)), m_counters(counters)
    {
    }

    QString m_sourcePath;
    QString m_destinationPath;
    std::optional<FinalCommittedArtifact> m_immutableArtifact;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
};

enum class ArchiveFallbackReason
{
    None,
    NonImmutableSource,
    CloneUnsupported,
    CrossVolume,
    PermissionDenied,
    UnsupportedExtentAlignment,
    PlatformCapabilityFailure,
    CloneAttemptFailed
};

enum class ArchiveFileTransportOutcome
{
    OrdinaryCopySucceeded,
    WindowsBlockCloneSucceeded,
    LinuxReflinkSucceeded,
    CloneUnsupportedCopySucceeded,
    CloneFailedCopySucceeded,
    NonImmutableSourceCopySucceeded,
    SourceChangedBeforeTransfer,
    SourceChangedDuringTransfer,
    DestinationPublicationFailure,
    CompleteFailure
};

struct ArchiveTransportDiagnostics
{
    std::uint64_t cloneAttempts = 0;
    std::uint64_t cloneSuccesses = 0;
    std::uint64_t unsupportedFallbacks = 0;
    std::uint64_t failedAttemptFallbacks = 0;
    std::uint64_t logicalBytesCloned = 0;
    std::uint64_t ordinaryCopyBytes = 0;
    ArchiveSelectedTransport selectedTransport = ArchiveSelectedTransport::None;
    ArchiveFallbackReason fallbackReason = ArchiveFallbackReason::None;

    void accumulate(const ArchiveTransportDiagnostics& other) noexcept;
};

struct AFTERNIGHTPROCESSES_API ArchiveFileTransportResult
{
    ArchiveFileTransportOutcome outcome = ArchiveFileTransportOutcome::CompleteFailure;
    ArchiveTransportDiagnostics diagnostics;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept;
};

/**
 * @brief Recoverable archive payload materialization and publication.
 *
 * Transfers always target a fresh sibling temporary file. The candidate is
 * atomically published only after transfer validation; every other path removes
 * it. Clone attempts are available only for a FinalCommittedArtifact authority.
 */
class AFTERNIGHTPROCESSES_API ArchiveFileTransport
{
public:
    ArchiveFileTransport();
    explicit ArchiveFileTransport(ArchiveTransportOperations operations);

    [[nodiscard]] static ArchiveFileTransport ordinaryCopyOnly();
    [[nodiscard]] ArchiveFileTransportResult transport(const ArchiveFileTransportRequest& request) const;

private:
    ArchiveTransportOperations m_operations;
};
} // namespace ImageIntegration
