// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <QDateTime>
#include <QString>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

/// Reads the artifact identity a generated file carries in its own header - its `ANARTID` FITS card or XISF property -
/// without decoding its samples; empty when the file cannot be read or carries none.
using ArtifactStampReader = std::function<QString(const QString& path)>;

/// A stamp reader that probes each header through a session's codecs in a one-thread batch operation it waits to be
/// admitted to. For a worker thread. The session must outlive the reader.
///
/// @param session the session.
/// @return the reader.
[[nodiscard]] AFTERNIGHTPROCESSES_API ArtifactStampReader
sessionArtifactStampReader(const an::consumer::EngineSession& session);

enum class ArtifactDigestProvenance
{
    CurrentRunCommitted,
    ReadbackVerified,
    Unknown
};

/**
 * @brief Lightweight identity of one stable file revision without reading its contents.
 *
 * This token is suitable for binding metadata or derived caches to the exact file
 * revision that was observed. It is not a content digest and must not be used as
 * authoritative output provenance.
 */
struct FileRevisionStamp
{
    QString path;
    qint64 sizeBytes = -1;
    qint64 modifiedAtMs = -1;
    QString identityToken;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool sameRevision(const FileRevisionStamp& other) const noexcept;
    [[nodiscard]] bool matchesFile(const QString& expectedPath = {}) const;
};

/**
 * @brief A complete SHA-256 digest explicitly sourced from a committed current-run write.
 *
 * Construction validates and canonicalizes the digest. The digest becomes provenance only
 * when paired with a FinalCommittedArtifact bound to one stable opened file revision; it is
 * recorded for the run archive and never decides whether an artifact is reused.
 */
class AFTERNIGHTPROCESSES_API CurrentRunCommittedDigest
{
public:
    [[nodiscard]] static std::optional<CurrentRunCommittedDigest> fromSha256(QString digest,
                                                                             ArtifactDigestProvenance provenance);

    [[nodiscard]] const QString& sha256() const noexcept
    {
        return m_sha256;
    }

private:
    explicit CurrentRunCommittedDigest(QString digest) : m_sha256(std::move(digest)) {}

    QString m_sha256;
};

/**
 * @brief Immutable identity of final bytes committed by the current workflow run.
 *
 * The file-backed factory performs one complete post-commit read through one
 * opened file handle and rejects a file whose stable identity, path binding,
 * size, or modification timestamp changes during verification.
 * Streaming encoders must not publish an artifact through this type until the
 * producing handle or atomic rename identity can be attested.
 */
class AFTERNIGHTPROCESSES_API FinalCommittedArtifact
{
public:
    [[nodiscard]] static std::optional<FinalCommittedArtifact>
    fromFile(const QString& path, WorkflowExecutionCounterAccumulator* counters = nullptr,
             const std::function<bool()>& shouldCancel = std::function<bool()>());

    [[nodiscard]] const QString& path() const noexcept
    {
        return m_path;
    }

    [[nodiscard]] qint64 sizeBytes() const noexcept
    {
        return m_sizeBytes;
    }

    [[nodiscard]] const QDateTime& modificationTimestamp() const noexcept
    {
        return m_modificationTimestamp;
    }

    [[nodiscard]] const CurrentRunCommittedDigest& digest() const noexcept
    {
        return m_digest;
    }

    [[nodiscard]] ArtifactDigestProvenance digestProvenance() const noexcept
    {
        return ArtifactDigestProvenance::CurrentRunCommitted;
    }

    [[nodiscard]] FileRevisionStamp revisionStamp() const
    {
        return FileRevisionStamp{m_path, m_sizeBytes, m_modificationTimestamp.toMSecsSinceEpoch(), m_fileIdentityToken};
    }

    [[nodiscard]] bool matchesFileSnapshot(const QString& expectedPath = {}) const;
    /**
     * @brief Validate an already-open native file handle against this committed artifact.
     *
     * The handle must identify the same stable file revision and the artifact path must
     * still resolve to that handle. On Linux pass a file descriptor; on Windows pass a
     * HANDLE converted to qintptr.
     */
    [[nodiscard]] bool matchesOpenFileSnapshot(qintptr nativeHandle) const;

private:
    FinalCommittedArtifact(QString path, qint64 sizeBytes, QDateTime modificationTimestamp,
                           CurrentRunCommittedDigest digest, QString fileIdentityToken)
        : m_path(std::move(path)), m_sizeBytes(sizeBytes), m_modificationTimestamp(std::move(modificationTimestamp)),
          m_digest(std::move(digest)), m_fileIdentityToken(std::move(fileIdentityToken))
    {
    }

    QString m_path;
    qint64 m_sizeBytes = 0;
    QDateTime m_modificationTimestamp;
    CurrentRunCommittedDigest m_digest;
    QString m_fileIdentityToken;
};

/**
 * @struct WorkflowArtifact
 * @brief Represents a generated output file and the settings that produced it.
 */
struct WorkflowArtifact
{
    QString path;
    QString sourceManifestHash; ///< Hash of the relevant manifest section
    QDateTime createdAt;
    QDateTime modifiedAt;
    qint64 sizeBytes = 0;
};

/** @brief Immutable persisted artifact row used by execution preparation. */
struct ArtifactRecordSnapshot
{
    QString path;
    QString manifestHash;
    qint64 sizeBytes = 0;
    qint64 modifiedAtMs = 0;
    QString contentDigest;
    QString digestProvenance;
    /// Artifact identity read from the file's own header when the record was published.
    QString artifactStamp;
    /// Platform file identity (volume + file id + change time) observed when the record was published.
    QString identityToken;
};

/**
 * @class ArtifactTracker
 * @brief Tracks workflow outputs and their source configurations.
 *
 * Uses an internal registry (or SQLite in future phases) to check if
 * a file on disk is still valid or if its settings have changed.
 */
class AFTERNIGHTPROCESSES_API ArtifactTracker
{
public:
    /// A tracker over an output directory's artifact records. Without a stamp reader the header identity is neither
    /// recorded nor read, and a moved or restored artifact is recomputed.
    explicit ArtifactTracker(QString outputDirectory, WorkflowExecutionCounterAccumulator* counters = nullptr,
                             ArtifactStampReader stampReader = {});
    ~ArtifactTracker();

    /**
     * @brief Decide whether a generated artifact can be reused, without reading its payload.
     *
     * Reuse requires the recorded manifest hash, size and modification time to match the file on
     * disk. Beyond that the check resolves in one of three ways:
     *
     *  1. **Untouched** — the recorded platform file identity still matches, so nothing has
     *     modified the file since it was published. This is the common case and costs one stat.
     *  2. **Moved or restored** — the identity changed and the row holds a stamp, so the artifact
     *     identity the file carries in its own header (its `ANARTID` card) is read and compared
     *     with it. A byte-identical restore under a fresh file identity therefore stays reusable; a
     *     foreign rewrite that dropped the stamp does not. This costs a header read, not a payload
     *     read.
     *  3. **Nothing left to check against** — the identity changed and the row holds no stamp, as a
     *     row made before stamping or without a reader does: treated as stale and regenerated. The
     *     content digest a master's row holds is the run archive's provenance and is never compared.
     *
     * The payload is never hashed. See `docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md` for the
     * trade-off and the atomic-write invariant that makes it sound.
     */
    [[nodiscard]] bool isArtifactValid(const QString& path, const QString& expectedManifestHash) const;

    /**
     * @brief Return whether provenance has been recorded for an artifact path.
     */
    [[nodiscard]] bool hasArtifactRecord(const QString& path) const;

    /** @brief Read every persisted artifact row through this tracker's existing connection. */
    [[nodiscard]] std::vector<ArtifactRecordSnapshot> artifactRecords() const;

    [[nodiscard]] bool isOpen() const;

    /** Return the platform file identity for replacement detection, or an empty string if unavailable. */
    [[nodiscard]] static QString fileIdentityForPath(const QString& path);

    /**
     * Capture one stable, path-bound file revision without hashing its contents. On Windows the file is opened for its
     * attributes alone, so the capture never waits for an antivirus scan of a file its writer has just closed.
     */
    [[nodiscard]] static std::optional<FileRevisionStamp> captureFileRevision(const QString& path);

    /**
     * @brief Record a new or updated artifact.
     *
     * Captures the file's size, modification time, platform identity and the artifact identity
     * carried in its own header. The payload is never read: the caller has just written this file
     * through an atomic rename, so its completeness is structural rather than something to verify.
     * The row holds no content digest; its `digest_provenance` column is labelled
     * `readback_verified` all the same, the label the schema gives such rows.
     *
     * Returns false without publishing a record when the file is missing or the optional
     * cancellation callback requests cancellation.
     */
    bool recordArtifact(const QString& path, const QString& manifestSectionHash,
                        const std::function<bool()>& shouldCancel = std::function<bool()>());

    /**
     * @brief Record an artifact the caller has just written, under the identity it stamped into the header.
     *
     * As the overload above, except that the identity is recorded as the writer stated it instead of being read
     * back. An antivirus filter scans a file as its writer closes it, and an open that reads the file before that
     * scan ends waits for it - about half a second for a full-size XISF frame on Windows - while the scan finishes
     * unseen when the file is first read later. The stamp is still read from the file whenever reuse depends on it,
     * so an identity stated here and missing from the header makes a moved file be computed again, never reused.
     */
    bool recordArtifact(const QString& path, const QString& manifestSectionHash, const an::ArtifactIdentity& stamped,
                        const std::function<bool()>& shouldCancel = std::function<bool()>());

    /**
     * @brief Publish metadata for a committed current-run artifact using its precomputed digest.
     *
     * This path snapshots file metadata, including the header stamp, but never reads the file
     * contents. The digest is recorded as provenance for the run archive; reuse is decided as for
     * every other row, from the platform identity or the stamp. Returns false without inserting
     * or replacing a record if the file is missing, cancellation is requested before publication,
     * or SQLite rejects the publication.
     */
    bool recordCurrentRunCommittedArtifact(const FinalCommittedArtifact& artifact, const QString& manifestSectionHash,
                                           const std::function<bool()>& shouldCancel = std::function<bool()>());

    /**
     * @brief Compute the SHA-256 content digest while accounting for consumed bytes.
     *
     * The optional cancellation callback is checked between fixed-size reads.
     * An empty result indicates open/read failure or cancellation.
     *
     * @warning This is **not** how reuse is decided — see isArtifactValid(). Hashing an image
     * to answer "can I skip regenerating this" is what
     * `docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md` exists to prevent. Every workflow
     * caller hashes the normalization coefficient document, a bounded document whose digest its
     * sidecar records: the normalization stage when it checks the document before reading it, the
     * preparation snapshot when it pins it, and the planner when it decides the group's reuse. A
     * master's provenance digest is taken by FinalCommittedArtifact::fromFile() through its own
     * opened handle, not through this function.
     */
    [[nodiscard]] static QString
    contentDigestForFile(const QString& path, WorkflowExecutionCounterAccumulator* counters = nullptr,
                         const std::function<bool()>& shouldCancel = std::function<bool()>());

private:
    bool publishArtifactRecord(const QString& path, const QString& manifestSectionHash, const QString& digest,
                               const QString& digestProvenance, const std::function<bool()>& shouldCancel,
                               const FinalCommittedArtifact* committedArtifact = nullptr,
                               const an::ArtifactIdentity* stamped = nullptr);

    QString m_outputDirectory;
    QString m_connectionName;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
    ArtifactStampReader m_stampReader;
};
