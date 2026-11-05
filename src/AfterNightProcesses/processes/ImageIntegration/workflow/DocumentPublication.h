// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// How the integration workflow writes a document that is not an image - a coefficient sidecar, a drizzle descriptor,
/// a dense map's payload - so that a reader finds the whole document it was given or the one before it, never part of
/// one.
///
/// An image goes through the engine's output transaction; nothing here touches one. A document is wrapped in a bounded,
/// versioned envelope that states what it is, the identity it satisfies, how long its payload is and the payload's
/// checksum, so a truncated or substituted payload is refused rather than read. It is written to a pending sibling the
/// workflow alone names, flushed, closed and moved over its destination in one atomic replace; a pair whose manifest
/// names its payload publishes the payload first, so an interruption can leave an orphaned payload but never a manifest
/// that advertises a payload that is missing or different. Recovery removes only what the workflow owns: its own
/// pending siblings, and payloads a caller says nothing references.
///
/// Every file operation goes through `DocumentFileOperations`, which a test replaces to fail a write, a flush, a
/// replace or a removal, to fill the disk, or to stop the publication between steps.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>

namespace ImageIntegration
{

/// The suffix of a pending sibling the workflow writes before publishing a document. A file ending in it is the
/// workflow's own and is never a published document.
inline constexpr char kPendingDocumentSuffix[] = ".anpending";

/// The envelope's magic bytes.
inline constexpr char kDocumentEnvelopeMagic[8] = {'A', 'N', 'D', 'O', 'C', 'E', 'N', 'V'};

/// The envelope layout this build writes and the only one it reads.
inline constexpr std::uint32_t kDocumentEnvelopeVersion = 1u;

/// The most bytes an envelope's header may hold.
inline constexpr std::uint32_t kMaxDocumentEnvelopeHeaderBytes = 64u * 1024u;

/// The most bytes a document's payload may hold unless its reader states a smaller bound.
inline constexpr std::uint64_t kDefaultMaxDocumentPayloadBytes = 64ull * 1024ull * 1024ull;

/// One document: what kind it is, the schema of its payload, the identity it satisfies and its payload.
struct DocumentEnvelope
{
    /// What the document is, for example "normalization-sidecar". A reader refuses any other kind.
    QString documentType;

    /// The payload's schema. A reader refuses a schema newer than it knows rather than guessing at it.
    int schemaVersion = 0;

    /// The artifact identity the document satisfies; empty when it satisfies none.
    QString identity;

    /// The payload, opaque to the envelope.
    QByteArray payload;
};

/// The envelope's bytes: the magic, the envelope version, the header's length, the header - the type, the schema, the
/// identity, the payload's length and its SHA-256 - and the payload.
///
/// @param envelope the document.
/// @return the bytes; empty when the header would exceed `kMaxDocumentEnvelopeHeaderBytes`.
[[nodiscard]] AFTERNIGHTPROCESSES_API QByteArray encodeDocumentEnvelope(const DocumentEnvelope& envelope);

/// Why a document was not read.
enum class DocumentReadFailure
{
    /// It was read.
    None,
    /// No file is at the path.
    Missing,
    /// The file could not be opened or read.
    Unreadable,
    /// The bytes do not begin with the envelope's magic, or the header is not one the envelope writes.
    NotAnEnvelope,
    /// The envelope's own layout is one this build does not read.
    UnknownEnvelopeVersion,
    /// The header or the payload is longer than the reader allows.
    TooLarge,
    /// The document is another kind than the one asked for.
    WrongType,
    /// The payload's schema is newer than the reader knows.
    UnknownSchema,
    /// The payload is shorter or longer than the header states.
    LengthMismatch,
    /// The payload's checksum is not the one the header states.
    ChecksumMismatch,
    /// The document satisfies another identity than the one asked for.
    IdentityMismatch
};

/// A failure's stable name, for a log line or a report.
///
/// @param failure the failure.
/// @return a static string; "Unknown" for a value outside the enumeration.
[[nodiscard]] const char* documentReadFailureName(DocumentReadFailure failure) noexcept;

/// What a reader asks of a document.
struct DocumentReadRequest
{
    /// The kind the document must be.
    QString documentType;

    /// The newest schema the reader knows; a newer one is refused.
    int maximumSchemaVersion = 0;

    /// The identity the document must satisfy; nothing asks for none in particular.
    std::optional<QString> expectedIdentity;

    /// The most payload bytes the reader accepts.
    std::uint64_t maxPayloadBytes = kDefaultMaxDocumentPayloadBytes;
};

/// The outcome of a read: the document, or why there is none, in words for a person beside the reason.
struct DocumentReadResult
{
    DocumentReadFailure failure = DocumentReadFailure::None;
    QString message;
    DocumentEnvelope envelope;

    [[nodiscard]] bool ok() const noexcept
    {
        return failure == DocumentReadFailure::None;
    }
};

/// A document read from bytes already in memory.
///
/// @param bytes the envelope's bytes.
/// @param request what the reader asks of it.
/// @return the document or the reason.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentReadResult decodeDocumentEnvelope(const QByteArray& bytes,
                                                                                const DocumentReadRequest& request);

/// A document read from a file, reading no more than the request's bound allows.
///
/// @param path the document's path.
/// @param request what the reader asks of it.
/// @return the document or the reason.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentReadResult readDocument(const QString& path,
                                                                      const DocumentReadRequest& request);

/// The file operations a publication makes, each replaceable so a test can fail it.
///
/// Each answers whether it succeeded and, when it did not, says why in `error`. `write` also reports a full disk
/// through `diskFull`, which the publication reports as its own reason.
struct DocumentFileOperations
{
    /// A handle the operations pass between them; opaque to the publication.
    using Handle = void*;

    std::function<Handle(const QString& path, QString& error)> create;
    std::function<bool(Handle handle, const QByteArray& bytes, bool& diskFull, QString& error)> write;
    std::function<bool(Handle handle, QString& error)> flush;
    std::function<bool(Handle handle, QString& error)> close;
    std::function<bool(const QString& from, const QString& to, QString& error)> replace;
    std::function<bool(const QString& path, QString& error)> remove;
};

/// The operations over the real file system: an exclusive create, a write, a flush to the device, a close, and an
/// atomic replace that never leaves the destination missing.
///
/// @return the operations.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentFileOperations systemDocumentFileOperations();

/// Why a publication did not publish.
enum class DocumentPublicationFailure
{
    /// It published.
    None,
    /// The stop was requested before the destination was replaced; nothing was published.
    Cancelled,
    /// The bytes are larger than the publication allows, or the destination's path is empty.
    InvalidRequest,
    /// The pending sibling could not be created.
    CreateFailed,
    /// Writing the pending sibling failed.
    WriteFailed,
    /// The disk filled while the pending sibling was written.
    DiskFull,
    /// The pending sibling could not be flushed to the device.
    FlushFailed,
    /// The pending sibling could not be closed.
    CloseFailed,
    /// The pending sibling could not replace the destination; the destination is as it was.
    ReplaceFailed
};

/// A failure's stable name, for a log line or a report.
///
/// @param failure the failure.
/// @return a static string; "Unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTPROCESSES_API const char*
documentPublicationFailureName(DocumentPublicationFailure failure) noexcept;

/// The outcome of a publication.
struct DocumentPublicationResult
{
    DocumentPublicationFailure failure = DocumentPublicationFailure::None;
    QString message;

    /// Whether a pending sibling was left behind because removing it failed too; recovery removes it.
    bool pendingLeftBehind = false;

    [[nodiscard]] bool ok() const noexcept
    {
        return failure == DocumentPublicationFailure::None;
    }
};

/// Writes bytes to a pending sibling of `path`, flushes and closes it, and replaces `path` with it.
///
/// A failure at any step removes the pending sibling and leaves `path` as it was. The stop is observed before each step
/// up to the replace and never after it: a document that replaced its destination is published.
///
/// @param path the destination.
/// @param bytes what it will hold - an encoded envelope, usually.
/// @param stop the stop the publication observes.
/// @param operations the file operations.
/// @return the outcome.
[[nodiscard]] DocumentPublicationResult
publishDocumentBytes(const QString& path, const QByteArray& bytes, std::stop_token stop = {},
                     const DocumentFileOperations& operations = systemDocumentFileOperations());

/// Publishes a document's envelope.
///
/// @param path the destination.
/// @param envelope the document.
/// @param stop the stop the publication observes.
/// @param operations the file operations.
/// @return the outcome.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentPublicationResult
publishDocument(const QString& path, const DocumentEnvelope& envelope, std::stop_token stop = {},
                const DocumentFileOperations& operations = systemDocumentFileOperations());

/// Publishes a payload and then the manifest that names it, so the manifest never advertises a payload that is not
/// there. A payload published before a failed manifest is left for recovery to remove as an orphan.
///
/// @param payloadPath the payload's destination.
/// @param payload the payload's envelope.
/// @param manifestPath the manifest's destination.
/// @param manifest the manifest's envelope.
/// @param stop the stop both publications observe.
/// @param operations the file operations.
/// @return the payload's failure when it did not publish, else the manifest's.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentPublicationResult
publishDocumentPair(const QString& payloadPath, const DocumentEnvelope& payload, const QString& manifestPath,
                    const DocumentEnvelope& manifest, std::stop_token stop = {},
                    const DocumentFileOperations& operations = systemDocumentFileOperations());

/// What recovery removed from one directory, and what it could not.
struct DocumentRecoveryReport
{
    QStringList removed;
    QStringList failed;
};

/// Whether a path is one of the workflow's pending siblings.
///
/// @param path a file's path or name.
/// @return true when it ends in `kPendingDocumentSuffix`.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool isPendingDocumentPath(const QString& path);

/// Removes the pending siblings in one directory - not in the directories below it - and every file there whose name
/// `isOrphan` says nothing references. A file that is neither is never touched.
///
/// Safe only while no publication into the directory is running.
///
/// @param directory the directory.
/// @param isOrphan answers for a published file's name whether it is an orphan the workflow owns; nothing removes no
///        published file.
/// @param operations the file operations.
/// @return what was removed and what could not be.
[[nodiscard]] AFTERNIGHTPROCESSES_API DocumentRecoveryReport
recoverDocumentDirectory(const QString& directory, const std::function<bool(const QString&)>& isOrphan = {},
                         const DocumentFileOperations& operations = systemDocumentFileOperations());

} // namespace ImageIntegration
