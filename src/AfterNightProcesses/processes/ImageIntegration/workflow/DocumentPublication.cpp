// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Publication and recovery of the integration workflow's non-image documents.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <cstring>
#include <limits>
#include <memory>

#ifdef Q_OS_WIN
#include <Windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ImageIntegration
{

namespace
{

constexpr qsizetype kMagicBytes = static_cast<qsizetype>(sizeof(kDocumentEnvelopeMagic));
constexpr qsizetype kFixedPrefixBytes = kMagicBytes + 2 * static_cast<qsizetype>(sizeof(std::uint32_t));

void appendUint32(QByteArray& bytes, const std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        bytes.append(static_cast<char>((value >> shift) & 0xFFu));
    }
}

[[nodiscard]] std::uint32_t readUint32(const char* bytes)
{
    std::uint32_t value = 0u;
    for (int index = 3; index >= 0; --index)
    {
        value = (value << 8) | static_cast<std::uint8_t>(bytes[index]);
    }
    return value;
}

[[nodiscard]] QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

[[nodiscard]] DocumentReadResult refused(const DocumentReadFailure failure, const QString& message)
{
    DocumentReadResult result;
    result.failure = failure;
    result.message = message;
    return result;
}

[[nodiscard]] DocumentPublicationResult failed(const DocumentPublicationFailure failure, const QString& message)
{
    DocumentPublicationResult result;
    result.failure = failure;
    result.message = message;
    return result;
}

[[nodiscard]] QString pendingSiblingFor(const QString& path)
{
    return path + QStringLiteral(".") + QUuid::createUuid().toString(QUuid::WithoutBraces) +
           QString::fromLatin1(kPendingDocumentSuffix);
}

#ifdef Q_OS_WIN
[[nodiscard]] QString windowsError(const char* what)
{
    return QStringLiteral("%1 failed with Windows error %2").arg(QString::fromLatin1(what)).arg(GetLastError());
}
#else
[[nodiscard]] QString posixError(const char* what)
{
    return QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(what), QString::fromLocal8Bit(std::strerror(errno)));
}
#endif

} // namespace

QByteArray encodeDocumentEnvelope(const DocumentEnvelope& envelope)
{
    QJsonObject header;
    header.insert(QStringLiteral("documentType"), envelope.documentType);
    header.insert(QStringLiteral("schemaVersion"), envelope.schemaVersion);
    header.insert(QStringLiteral("identity"), envelope.identity);
    header.insert(QStringLiteral("payloadLength"), QString::number(envelope.payload.size()));
    header.insert(QStringLiteral("payloadSha256"), sha256Hex(envelope.payload));
    const QByteArray headerBytes = QJsonDocument(header).toJson(QJsonDocument::Compact);
    if (headerBytes.size() > static_cast<qsizetype>(kMaxDocumentEnvelopeHeaderBytes))
    {
        return {};
    }

    QByteArray bytes;
    bytes.reserve(kFixedPrefixBytes + headerBytes.size() + envelope.payload.size());
    bytes.append(kDocumentEnvelopeMagic, kMagicBytes);
    appendUint32(bytes, kDocumentEnvelopeVersion);
    appendUint32(bytes, static_cast<std::uint32_t>(headerBytes.size()));
    bytes.append(headerBytes);
    bytes.append(envelope.payload);
    return bytes;
}

const char* documentReadFailureName(const DocumentReadFailure failure) noexcept
{
    switch (failure)
    {
    case DocumentReadFailure::None:
        return "None";
    case DocumentReadFailure::Missing:
        return "Missing";
    case DocumentReadFailure::Unreadable:
        return "Unreadable";
    case DocumentReadFailure::NotAnEnvelope:
        return "NotAnEnvelope";
    case DocumentReadFailure::UnknownEnvelopeVersion:
        return "UnknownEnvelopeVersion";
    case DocumentReadFailure::TooLarge:
        return "TooLarge";
    case DocumentReadFailure::WrongType:
        return "WrongType";
    case DocumentReadFailure::UnknownSchema:
        return "UnknownSchema";
    case DocumentReadFailure::LengthMismatch:
        return "LengthMismatch";
    case DocumentReadFailure::ChecksumMismatch:
        return "ChecksumMismatch";
    case DocumentReadFailure::IdentityMismatch:
        return "IdentityMismatch";
    }
    return "Unknown";
}

DocumentReadResult decodeDocumentEnvelope(const QByteArray& bytes, const DocumentReadRequest& request)
{
    if (bytes.size() < kFixedPrefixBytes || std::memcmp(bytes.constData(), kDocumentEnvelopeMagic, kMagicBytes) != 0)
    {
        return refused(DocumentReadFailure::NotAnEnvelope, QStringLiteral("The file is not a workflow document."));
    }
    const std::uint32_t envelopeVersion = readUint32(bytes.constData() + kMagicBytes);
    if (envelopeVersion != kDocumentEnvelopeVersion)
    {
        return refused(
            DocumentReadFailure::UnknownEnvelopeVersion,
            QStringLiteral("The document's envelope version %1 is not one this version reads.").arg(envelopeVersion));
    }
    const std::uint32_t headerLength = readUint32(bytes.constData() + kMagicBytes + sizeof(std::uint32_t));
    if (headerLength > kMaxDocumentEnvelopeHeaderBytes)
    {
        return refused(DocumentReadFailure::TooLarge, QStringLiteral("The document's header is too large."));
    }
    if (bytes.size() - kFixedPrefixBytes < static_cast<qsizetype>(headerLength))
    {
        return refused(DocumentReadFailure::LengthMismatch, QStringLiteral("The document's header is truncated."));
    }

    QJsonParseError parseError{};
    const QJsonDocument headerDocument =
        QJsonDocument::fromJson(bytes.mid(kFixedPrefixBytes, static_cast<qsizetype>(headerLength)), &parseError);
    if (parseError.error != QJsonParseError::NoError || !headerDocument.isObject())
    {
        return refused(DocumentReadFailure::NotAnEnvelope, QStringLiteral("The document's header is not readable."));
    }
    const QJsonObject header = headerDocument.object();
    const QJsonValue typeValue = header.value(QStringLiteral("documentType"));
    const QJsonValue schemaValue = header.value(QStringLiteral("schemaVersion"));
    const QJsonValue identityValue = header.value(QStringLiteral("identity"));
    const QJsonValue lengthValue = header.value(QStringLiteral("payloadLength"));
    const QJsonValue checksumValue = header.value(QStringLiteral("payloadSha256"));
    bool lengthParsed = false;
    const qulonglong statedLength = lengthValue.toString().toULongLong(&lengthParsed);
    if (!typeValue.isString() || !schemaValue.isDouble() || !identityValue.isString() || !lengthValue.isString() ||
        !lengthParsed || !checksumValue.isString())
    {
        return refused(DocumentReadFailure::NotAnEnvelope, QStringLiteral("The document's header is incomplete."));
    }

    DocumentEnvelope envelope;
    envelope.documentType = typeValue.toString();
    envelope.schemaVersion = schemaValue.toInt();
    envelope.identity = identityValue.toString();
    if (envelope.documentType != request.documentType)
    {
        return refused(
            DocumentReadFailure::WrongType,
            QStringLiteral("The document is a %1, not a %2.").arg(envelope.documentType, request.documentType));
    }
    if (envelope.schemaVersion > request.maximumSchemaVersion || envelope.schemaVersion <= 0)
    {
        return refused(
            DocumentReadFailure::UnknownSchema,
            QStringLiteral("The document's schema %1 is not one this version reads.").arg(envelope.schemaVersion));
    }
    if (statedLength > request.maxPayloadBytes)
    {
        return refused(DocumentReadFailure::TooLarge, QStringLiteral("The document's payload is too large."));
    }
    const qsizetype payloadOffset = kFixedPrefixBytes + static_cast<qsizetype>(headerLength);
    if (static_cast<qulonglong>(bytes.size() - payloadOffset) != statedLength)
    {
        return refused(DocumentReadFailure::LengthMismatch,
                       QStringLiteral("The document's payload is not the length its header states."));
    }
    envelope.payload = bytes.mid(payloadOffset);
    if (sha256Hex(envelope.payload) != checksumValue.toString())
    {
        return refused(DocumentReadFailure::ChecksumMismatch,
                       QStringLiteral("The document's payload is not the one its header states."));
    }
    if (request.expectedIdentity.has_value() && envelope.identity != *request.expectedIdentity)
    {
        return refused(DocumentReadFailure::IdentityMismatch,
                       QStringLiteral("The document satisfies another request than this one."));
    }

    DocumentReadResult result;
    result.envelope = std::move(envelope);
    return result;
}

DocumentReadResult readDocument(const QString& path, const DocumentReadRequest& request)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        return refused(DocumentReadFailure::Missing, QStringLiteral("No document is at %1.").arg(path));
    }
    const std::uint64_t bound =
        static_cast<std::uint64_t>(kFixedPrefixBytes) + kMaxDocumentEnvelopeHeaderBytes + request.maxPayloadBytes;
    if (static_cast<std::uint64_t>(info.size()) > bound)
    {
        return refused(DocumentReadFailure::TooLarge, QStringLiteral("The document at %1 is too large.").arg(path));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refused(DocumentReadFailure::Unreadable,
                       QStringLiteral("The document at %1 could not be opened: %2").arg(path, file.errorString()));
    }
    const QByteArray bytes = file.read(static_cast<qint64>(bound) + 1);
    if (file.error() != QFileDevice::NoError)
    {
        return refused(DocumentReadFailure::Unreadable,
                       QStringLiteral("The document at %1 could not be read: %2").arg(path, file.errorString()));
    }
    if (static_cast<std::uint64_t>(bytes.size()) > bound)
    {
        return refused(DocumentReadFailure::TooLarge, QStringLiteral("The document at %1 is too large.").arg(path));
    }
    return decodeDocumentEnvelope(bytes, request);
}

DocumentFileOperations systemDocumentFileOperations()
{
    DocumentFileOperations operations;
#ifdef Q_OS_WIN
    operations.create = [](const QString& path, QString& error) -> DocumentFileOperations::Handle {
        const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
        HANDLE handle =
            CreateFileW(native.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = windowsError("Creating the pending document");
            return nullptr;
        }
        return handle;
    };
    operations.write = [](const DocumentFileOperations::Handle handle, const QByteArray& bytes, bool& diskFull,
                          QString& error) {
        qsizetype written = 0;
        while (written < bytes.size())
        {
            const DWORD chunk = static_cast<DWORD>(
                (std::min)(bytes.size() - written, static_cast<qsizetype>((std::numeric_limits<DWORD>::max)() / 2)));
            DWORD done = 0;
            if (WriteFile(static_cast<HANDLE>(handle), bytes.constData() + written, chunk, &done, nullptr) == 0)
            {
                const DWORD code = GetLastError();
                diskFull = code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL;
                error = QStringLiteral("Writing the pending document failed with Windows error %1").arg(code);
                return false;
            }
            if (done == 0)
            {
                diskFull = true;
                error = QStringLiteral("Writing the pending document wrote nothing.");
                return false;
            }
            written += static_cast<qsizetype>(done);
        }
        return true;
    };
    operations.flush = [](const DocumentFileOperations::Handle handle, QString& error) {
        if (FlushFileBuffers(static_cast<HANDLE>(handle)) == 0)
        {
            error = windowsError("Flushing the pending document");
            return false;
        }
        return true;
    };
    operations.close = [](const DocumentFileOperations::Handle handle, QString& error) {
        if (CloseHandle(static_cast<HANDLE>(handle)) == 0)
        {
            error = windowsError("Closing the pending document");
            return false;
        }
        return true;
    };
    operations.replace = [](const QString& from, const QString& to, QString& error) {
        const std::wstring source = QDir::toNativeSeparators(from).toStdWString();
        const std::wstring destination = QDir::toNativeSeparators(to).toStdWString();
        if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        {
            error = windowsError("Publishing the document");
            return false;
        }
        return true;
    };
#else
    operations.create = [](const QString& path, QString& error) -> DocumentFileOperations::Handle {
        const int descriptor =
            ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (descriptor < 0)
        {
            error = posixError("Creating the pending document");
            return nullptr;
        }
        return reinterpret_cast<DocumentFileOperations::Handle>(static_cast<std::intptr_t>(descriptor) + 1);
    };
    const auto descriptorOf = [](const DocumentFileOperations::Handle handle) {
        return static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1);
    };
    operations.write = [descriptorOf](const DocumentFileOperations::Handle handle, const QByteArray& bytes,
                                      bool& diskFull, QString& error) {
        qsizetype written = 0;
        while (written < bytes.size())
        {
            const ssize_t done =
                ::write(descriptorOf(handle), bytes.constData() + written, static_cast<size_t>(bytes.size() - written));
            if (done < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                diskFull = errno == ENOSPC;
                error = posixError("Writing the pending document");
                return false;
            }
            if (done == 0)
            {
                diskFull = true;
                error = QStringLiteral("Writing the pending document wrote nothing.");
                return false;
            }
            written += static_cast<qsizetype>(done);
        }
        return true;
    };
    operations.flush = [descriptorOf](const DocumentFileOperations::Handle handle, QString& error) {
        if (::fsync(descriptorOf(handle)) != 0)
        {
            error = posixError("Flushing the pending document");
            return false;
        }
        return true;
    };
    operations.close = [descriptorOf](const DocumentFileOperations::Handle handle, QString& error) {
        if (::close(descriptorOf(handle)) != 0)
        {
            error = posixError("Closing the pending document");
            return false;
        }
        return true;
    };
    operations.replace = [](const QString& from, const QString& to, QString& error) {
        if (std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) != 0)
        {
            error = posixError("Publishing the document");
            return false;
        }
        return true;
    };
#endif
    operations.remove = [](const QString& path, QString& error) {
        QFile file(path);
        if (!file.exists() || file.remove())
        {
            return true;
        }
        error = file.errorString();
        return false;
    };
    return operations;
}

const char* documentPublicationFailureName(const DocumentPublicationFailure failure) noexcept
{
    switch (failure)
    {
    case DocumentPublicationFailure::None:
        return "None";
    case DocumentPublicationFailure::Cancelled:
        return "Cancelled";
    case DocumentPublicationFailure::InvalidRequest:
        return "InvalidRequest";
    case DocumentPublicationFailure::CreateFailed:
        return "CreateFailed";
    case DocumentPublicationFailure::WriteFailed:
        return "WriteFailed";
    case DocumentPublicationFailure::DiskFull:
        return "DiskFull";
    case DocumentPublicationFailure::FlushFailed:
        return "FlushFailed";
    case DocumentPublicationFailure::CloseFailed:
        return "CloseFailed";
    case DocumentPublicationFailure::ReplaceFailed:
        return "ReplaceFailed";
    }
    return "Unknown";
}

DocumentPublicationResult publishDocumentBytes(const QString& path, const QByteArray& bytes, const std::stop_token stop,
                                               const DocumentFileOperations& operations)
{
    if (path.trimmed().isEmpty() || bytes.isEmpty())
    {
        return failed(DocumentPublicationFailure::InvalidRequest, QStringLiteral("Nothing to publish, or nowhere."));
    }
    if (stop.stop_requested())
    {
        return failed(DocumentPublicationFailure::Cancelled, QStringLiteral("The publication was stopped."));
    }

    const QString pending = pendingSiblingFor(path);
    QString error;
    DocumentFileOperations::Handle handle = operations.create(pending, error);
    if (handle == nullptr)
    {
        return failed(DocumentPublicationFailure::CreateFailed, error);
    }

    bool open = true;
    const auto abandon = [&](const DocumentPublicationFailure failure, const QString& message) {
        DocumentPublicationResult result = failed(failure, message);
        QString ignored;
        if (open)
        {
            (void)operations.close(handle, ignored);
            open = false;
        }
        if (!operations.remove(pending, ignored))
        {
            result.pendingLeftBehind = true;
        }
        return result;
    };

    bool diskFull = false;
    if (!operations.write(handle, bytes, diskFull, error))
    {
        return abandon(diskFull ? DocumentPublicationFailure::DiskFull : DocumentPublicationFailure::WriteFailed,
                       error);
    }
    if (stop.stop_requested())
    {
        return abandon(DocumentPublicationFailure::Cancelled, QStringLiteral("The publication was stopped."));
    }
    if (!operations.flush(handle, error))
    {
        return abandon(DocumentPublicationFailure::FlushFailed, error);
    }
    open = false;
    if (!operations.close(handle, error))
    {
        return abandon(DocumentPublicationFailure::CloseFailed, error);
    }
    if (stop.stop_requested())
    {
        return abandon(DocumentPublicationFailure::Cancelled, QStringLiteral("The publication was stopped."));
    }
    if (!operations.replace(pending, path, error))
    {
        return abandon(DocumentPublicationFailure::ReplaceFailed, error);
    }
    return {};
}

DocumentPublicationResult publishDocument(const QString& path, const DocumentEnvelope& envelope,
                                          const std::stop_token stop, const DocumentFileOperations& operations)
{
    const QByteArray bytes = encodeDocumentEnvelope(envelope);
    if (bytes.isEmpty())
    {
        return failed(DocumentPublicationFailure::InvalidRequest,
                      QStringLiteral("The document's header is too large."));
    }
    return publishDocumentBytes(path, bytes, stop, operations);
}

DocumentPublicationResult publishDocumentPair(const QString& payloadPath, const DocumentEnvelope& payload,
                                              const QString& manifestPath, const DocumentEnvelope& manifest,
                                              const std::stop_token stop, const DocumentFileOperations& operations)
{
    DocumentPublicationResult payloadResult = publishDocument(payloadPath, payload, stop, operations);
    if (!payloadResult.ok())
    {
        return payloadResult;
    }
    return publishDocument(manifestPath, manifest, stop, operations);
}

bool isPendingDocumentPath(const QString& path)
{
    return path.endsWith(QString::fromLatin1(kPendingDocumentSuffix));
}

DocumentRecoveryReport recoverDocumentDirectory(const QString& directory,
                                                const std::function<bool(const QString&)>& isOrphan,
                                                const DocumentFileOperations& operations)
{
    DocumentRecoveryReport report;
    const QDir dir(directory);
    if (directory.trimmed().isEmpty() || !dir.exists())
    {
        return report;
    }
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries)
    {
        const QString name = entry.fileName();
        const bool owned = isPendingDocumentPath(name) || (isOrphan && isOrphan(name));
        if (!owned)
        {
            continue;
        }
        QString error;
        if (operations.remove(entry.absoluteFilePath(), error))
        {
            report.removed.append(name);
        }
        else
        {
            report.failed.append(name);
        }
    }
    return report;
}

} // namespace ImageIntegration
