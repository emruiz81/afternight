// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ArtifactTracker.h"
#include "ArtifactSchema.h"
#include "app/ApplicationStorage.h"
#include "processes/ImageIntegration/core/DatabaseUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/Sha256.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <array>
#include <cstddef>
#include <limits>
#include <span>

#ifdef Q_OS_LINUX
#include <sys/stat.h>
#endif

#ifdef Q_OS_WIN
#include <Windows.h>
#include <io.h>
#endif

namespace
{
const char* kDbName = "artifacts.db";

QString serializedProvenance(const char* provenance)
{
    return QString::fromLatin1(provenance);
}

QString artifactsTableSchema()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS artifacts ("
                          "path TEXT PRIMARY KEY, "
                          "hash TEXT NOT NULL, "
                          "created_at INTEGER NOT NULL, "
                          "size_bytes INTEGER NOT NULL, "
                          "modified_at INTEGER NOT NULL, "
                          "content_digest TEXT NOT NULL, "
                          "digest_provenance TEXT NOT NULL DEFAULT '%1', "
                          "artifact_stamp TEXT NOT NULL DEFAULT '', "
                          "identity_token TEXT NOT NULL DEFAULT '')")
        .arg(serializedProvenance(ImageIntegration::schema::kArtifactDigestProvenanceUnknown));
}

/// The identity a stamp reader reads, or nothing when there is no reader.
QString artifactStampForFile(const ArtifactStampReader& reader, const QString& path)
{
    return reader ? reader(path) : QString();
}

/// Whether the artifacts table has every column its format's table has.
bool artifactsTableIsItsFormats(QSqlDatabase& db, WorkflowExecutionCounterAccumulator* counters)
{
    QSqlQuery q(db);
    if (!core::executeSqliteStatement(q, QStringLiteral("PRAGMA table_info(artifacts)"), counters))
        return false;

    QStringList columns;
    while (q.next())
    {
        columns << q.value(1).toString();
    }
    for (const char* column : {"path", "hash", "created_at", "size_bytes", "modified_at", "content_digest",
                               "digest_provenance", "artifact_stamp", "identity_token"})
    {
        if (!columns.contains(QString::fromLatin1(column)))
            return false;
    }
    return true;
}

/// What opening the artifact records' store found.
enum class ArtifactStore
{
    /// The store is at the format, or was made at it.
    Opened,
    /// The store states another format, or its table is not its format's: nothing was read or written.
    OtherFormat,
    Failed
};

/// Opens the store at the format, making a new one at it, and reports one of another format without reading or writing
/// it.
ArtifactStore openArtifactStore(QSqlDatabase& db, WorkflowExecutionCounterAccumulator* counters, int& foundFormat)
{
    QSqlQuery tables(db);
    if (!core::executeSqliteStatement(
            tables, QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' LIMIT 1"), counters))
        return ArtifactStore::Failed;
    const bool holdsTables = tables.next();

    QSqlQuery version(db);
    if (!core::executeSqliteStatement(version, QStringLiteral("PRAGMA user_version"), counters) || !version.next())
        return ArtifactStore::Failed;
    foundFormat = version.value(0).toInt();

    if (!holdsTables)
    {
        QSqlQuery create(db);
        QSqlQuery setVersion(db);
        return core::executeSqliteStatement(create, artifactsTableSchema(), counters) &&
                       core::executeSqliteStatement(setVersion,
                                                    QStringLiteral("PRAGMA user_version = %1")
                                                        .arg(ImageIntegration::schema::kArtifactTrackerSchemaVersion),
                                                    counters)
                   ? ArtifactStore::Opened
                   : ArtifactStore::Failed;
    }
    if (foundFormat != ImageIntegration::schema::kArtifactTrackerSchemaVersion ||
        !artifactsTableIsItsFormats(db, counters))
        return ArtifactStore::OtherFormat;
    return ArtifactStore::Opened;
}

qint64 modificationStamp(const QFileInfo& info)
{
    return info.lastModified().toMSecsSinceEpoch();
}

struct NativeFileSnapshot
{
    QString identityToken;
    qint64 sizeBytes = -1;
};

#ifdef Q_OS_LINUX
[[nodiscard]] QString linuxIdentityToken(const struct stat& status)
{
    if (!S_ISREG(status.st_mode))
    {
        return {};
    }
    return QStringLiteral("linux:%1:%2:%3:%4")
        .arg(static_cast<qulonglong>(status.st_dev))
        .arg(static_cast<qulonglong>(status.st_ino))
        .arg(static_cast<qlonglong>(status.st_ctim.tv_sec))
        .arg(static_cast<qlonglong>(status.st_ctim.tv_nsec));
}
#endif

#ifdef Q_OS_WIN
[[nodiscard]] std::optional<NativeFileSnapshot> windowsFileSnapshot(const HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION identity{};
    FILE_BASIC_INFO basicInfo{};
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr || GetFileInformationByHandle(handle, &identity) == 0 ||
        GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == 0 ||
        (identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return std::nullopt;
    }

    const std::uint64_t fileIndex = (static_cast<std::uint64_t>(identity.nFileIndexHigh) << 32U) |
                                    static_cast<std::uint64_t>(identity.nFileIndexLow);
    const std::uint64_t fileSize =
        (static_cast<std::uint64_t>(identity.nFileSizeHigh) << 32U) | static_cast<std::uint64_t>(identity.nFileSizeLow);
    if (fileSize > static_cast<std::uint64_t>((std::numeric_limits<qint64>::max)()))
    {
        return std::nullopt;
    }

    NativeFileSnapshot snapshot;
    snapshot.identityToken = QStringLiteral("windows:%1:%2:%3")
                                 .arg(static_cast<qulonglong>(identity.dwVolumeSerialNumber))
                                 .arg(static_cast<qulonglong>(fileIndex))
                                 .arg(static_cast<qlonglong>(basicInfo.ChangeTime.QuadPart));
    snapshot.sizeBytes = static_cast<qint64>(fileSize);
    return snapshot;
}
#endif

[[nodiscard]] std::optional<NativeFileSnapshot> nativeFileSnapshot(const qintptr nativeHandle)
{
#ifdef Q_OS_LINUX
    struct stat status{};
    if (nativeHandle < 0 || ::fstat(static_cast<int>(nativeHandle), &status) != 0)
    {
        return std::nullopt;
    }
    const QString identityToken = linuxIdentityToken(status);
    if (identityToken.isEmpty())
    {
        return std::nullopt;
    }
    return NativeFileSnapshot{identityToken, status.st_size};
#elif defined(Q_OS_WIN)
    return windowsFileSnapshot(reinterpret_cast<HANDLE>(nativeHandle));
#else
    Q_UNUSED(nativeHandle);
    return std::nullopt;
#endif
}

[[nodiscard]] std::optional<qintptr> nativeHandleForFile(const QFile& file)
{
    const int descriptor = file.handle();
    if (descriptor < 0)
    {
        return std::nullopt;
    }
#ifdef Q_OS_WIN
    const intptr_t nativeHandle = _get_osfhandle(descriptor);
    if (nativeHandle == -1)
    {
        return std::nullopt;
    }
    return static_cast<qintptr>(nativeHandle);
#else
    return static_cast<qintptr>(descriptor);
#endif
}

#ifdef Q_OS_WIN
/// Holds one file still while its revision is taken, opened for its attributes alone. An antivirus filter scans a
/// file as its writer closes it, and an open that asks to read the file waits for that scan to end - about half a
/// second for a full-size XISF frame - while one that asks for the attributes does not.
class RevisionHandle
{
public:
    explicit RevisionHandle(const QString& path)
        : m_handle(CreateFileW(path.toStdWString().c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr))
    {
    }
    ~RevisionHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    RevisionHandle(const RevisionHandle&) = delete;
    RevisionHandle& operator=(const RevisionHandle&) = delete;

    /// The platform handle, or nothing when the file could not be opened.
    [[nodiscard]] std::optional<qintptr> native() const
    {
        if (m_handle == INVALID_HANDLE_VALUE)
            return std::nullopt;
        return reinterpret_cast<qintptr>(m_handle);
    }

private:
    HANDLE m_handle;
};
#else
/// Holds one file still while its revision is taken, opened read-only; nothing of it is read.
class RevisionHandle
{
public:
    explicit RevisionHandle(const QString& path) : m_file(path)
    {
        (void)m_file.open(QIODevice::ReadOnly);
    }

    /// The platform handle, or nothing when the file could not be opened.
    [[nodiscard]] std::optional<qintptr> native() const
    {
        return m_file.isOpen() ? nativeHandleForFile(m_file) : std::nullopt;
    }

private:
    QFile m_file;
};
#endif

[[nodiscard]] QString fileIdentityToken(const QString& path)
{
#ifdef Q_OS_LINUX
    struct stat status{};
    const QByteArray encodedPath = QFile::encodeName(path);
    if (::stat(encodedPath.constData(), &status) != 0)
    {
        return {};
    }
    return linuxIdentityToken(status);
#elif defined(Q_OS_WIN)
    const std::wstring nativePath = path.toStdWString();
    const HANDLE handle =
        CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    const auto snapshot = windowsFileSnapshot(handle);
    CloseHandle(handle);
    return snapshot.has_value() ? snapshot->identityToken : QString{};
#else
    Q_UNUSED(path);
    return {};
#endif
}

[[nodiscard]] QString contentDigestForOpenFile(QFile& file, WorkflowExecutionCounterAccumulator* counters,
                                               const std::function<bool()>& shouldCancel)
{
    if (!file.isOpen() || !file.seek(0))
    {
        return {};
    }

    // The processor's SHA instructions digest a master several times faster than the portable rounds, and the
    // digest is the standard's either way.
    an::platform::Sha256 hash;
    std::array<char, 64 * 1024> buffer;
    while (!file.atEnd())
    {
        if (shouldCancel && shouldCancel())
        {
            return {};
        }

        const qint64 bytesRead = file.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (bytesRead <= 0)
        {
            return {};
        }
        if (counters != nullptr)
        {
            counters->addBytesHashed(static_cast<std::uint64_t>(bytesRead));
        }
        hash.update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(bytesRead))));
    }

    return QString::fromStdString(an::platform::sha256Hex(hash.finish()));
}

} // namespace

bool FileRevisionStamp::isValid() const noexcept
{
    return !path.isEmpty() && sizeBytes >= 0 && modifiedAtMs >= 0 && !identityToken.isEmpty();
}

bool FileRevisionStamp::sameRevision(const FileRevisionStamp& other) const noexcept
{
    return isValid() && other.isValid() && path == other.path && sizeBytes == other.sizeBytes &&
           modifiedAtMs == other.modifiedAtMs && identityToken == other.identityToken;
}

bool FileRevisionStamp::matchesFile(const QString& expectedPath) const
{
    if (!isValid())
        return false;

    const QString candidate_path = expectedPath.trimmed().isEmpty() ? path : expectedPath;
    const auto current = ArtifactTracker::captureFileRevision(candidate_path);
    return current.has_value() && sameRevision(*current);
}

std::optional<CurrentRunCommittedDigest>
CurrentRunCommittedDigest::fromSha256(QString digest, const ArtifactDigestProvenance provenance)
{
    if (provenance != ArtifactDigestProvenance::CurrentRunCommitted || digest.size() != 64)
        return std::nullopt;

    for (const QChar character : digest)
    {
        const bool decimal = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool lowercase = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        const bool uppercase = character >= QLatin1Char('A') && character <= QLatin1Char('F');
        if (!decimal && !lowercase && !uppercase)
            return std::nullopt;
    }

    return CurrentRunCommittedDigest(digest.toLower());
}

std::optional<FinalCommittedArtifact> FinalCommittedArtifact::fromFile(const QString& path,
                                                                       WorkflowExecutionCounterAccumulator* counters,
                                                                       const std::function<bool()>& shouldCancel)
{
    if (shouldCancel && shouldCancel())
        return std::nullopt;

    const QFileInfo initialInfo(path);
    if (!initialInfo.exists() || !initialInfo.isFile())
        return std::nullopt;

    const QString absolutePath = QDir::cleanPath(initialInfo.absoluteFilePath());
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    const auto nativeHandle = nativeHandleForFile(file);
    const auto openedBefore = nativeHandle.has_value() ? nativeFileSnapshot(*nativeHandle) : std::nullopt;
    const QFileInfo before(absolutePath);
    const QDateTime modificationTimestamp = before.lastModified();
    if (!openedBefore.has_value() || openedBefore->identityToken.isEmpty() ||
        fileIdentityToken(absolutePath) != openedBefore->identityToken || before.size() != openedBefore->sizeBytes ||
        !modificationTimestamp.isValid())
        return std::nullopt;

    const QString sha256 = contentDigestForOpenFile(file, counters, shouldCancel);
    if (sha256.isEmpty() || (shouldCancel && shouldCancel()))
        return std::nullopt;

    const auto openedAfter = nativeFileSnapshot(*nativeHandle);
    const QFileInfo after(absolutePath);
    if (!openedAfter.has_value() || openedBefore->identityToken != openedAfter->identityToken ||
        openedBefore->sizeBytes != openedAfter->sizeBytes ||
        fileIdentityToken(absolutePath) != openedAfter->identityToken || after.size() != openedAfter->sizeBytes ||
        after.lastModified().toMSecsSinceEpoch() != modificationTimestamp.toMSecsSinceEpoch())
        return std::nullopt;

    const auto digest = CurrentRunCommittedDigest::fromSha256(sha256, ArtifactDigestProvenance::CurrentRunCommitted);
    if (!digest.has_value())
        return std::nullopt;

    FinalCommittedArtifact result(absolutePath, openedBefore->sizeBytes, modificationTimestamp, std::move(*digest),
                                  openedBefore->identityToken);
    if (!result.matchesOpenFileSnapshot(*nativeHandle) || !result.matchesFileSnapshot(absolutePath))
        return std::nullopt;
    return result;
}

bool FinalCommittedArtifact::matchesFileSnapshot(const QString& expectedPath) const
{
    if (!expectedPath.trimmed().isEmpty() && QDir::cleanPath(QFileInfo(expectedPath).absoluteFilePath()) != m_path)
        return false;

    const QFileInfo info(m_path);
    return info.exists() && info.isFile() && QDir::cleanPath(info.absoluteFilePath()) == m_path &&
           info.size() == m_sizeBytes &&
           info.lastModified().toMSecsSinceEpoch() == m_modificationTimestamp.toMSecsSinceEpoch() &&
           (m_fileIdentityToken.isEmpty() || fileIdentityToken(m_path) == m_fileIdentityToken);
}

bool FinalCommittedArtifact::matchesOpenFileSnapshot(const qintptr nativeHandle) const
{
    const auto opened = nativeFileSnapshot(nativeHandle);
    return opened.has_value() && !m_fileIdentityToken.isEmpty() && opened->identityToken == m_fileIdentityToken &&
           opened->sizeBytes == m_sizeBytes && fileIdentityToken(m_path) == opened->identityToken;
}

ArtifactStampReader sessionArtifactStampReader(const an::consumer::EngineSession& session)
{
    return [&session](const QString& path) -> QString {
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        if (!scope.hasValue())
        {
            return {};
        }
        const std::string nativePath = path.toUtf8().toStdString();
        const an::Result<an::CodecImageInfo> probed = an::probeImage(
            scope->engine(), scope->execution(), an::CodecReadRequest{.path = nativePath, .validatePayload = false});
        if (!probed.hasValue())
        {
            LOG_DEBUG("ArtifactTracker: could not read artifact stamp from '{}': {}", path.toStdString(),
                      probed.status().message());
            return {};
        }
        // A file states its identity in its own format's vocabulary - a FITS keyword, an XISF property - and the
        // probe chose its codec by the same name.
        const auto identity =
            an::artifactIdentityOf(probed->metadata.codec(), an::codecNamespaceOf(an::codecFormatFromPath(nativePath)));
        if (!identity.hasValue() || !identity->has_value())
        {
            return {};
        }
        return QString::fromStdString((*identity)->artifactId());
    };
}

ArtifactTracker::ArtifactTracker(QString outputDirectory, WorkflowExecutionCounterAccumulator* counters,
                                 ArtifactStampReader stampReader)
    : m_outputDirectory(std::move(outputDirectory)),
      m_connectionName(core::nextConnectionName(QStringLiteral("ArtifactTracker"))), m_counters(counters),
      m_stampReader(std::move(stampReader))
{
    // Initialize DB
    QString dbPath = QDir(m_outputDirectory).filePath(QStringLiteral(".afternight/image_integration"));
    QDir().mkpath(dbPath);
    QString dbFile = QDir(dbPath).filePath(kDbName);

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbFile);
    if (!db.open())
    {
        return;
    }
    int foundFormat = 0;
    if (openArtifactStore(db, m_counters, foundFormat) != ArtifactStore::OtherFormat)
    {
        return;
    }
    // Records of another format are never read or reused: the store is set aside beside itself and a new one made, so
    // every artifact it recorded is computed again.
    db.close();
    const QString setAside = an::ui::setAsideStore(dbFile, foundFormat);
    if (setAside.isEmpty())
    {
        LOG_WARNING(
            "ArtifactTracker: the artifact records '{}' state format {}; this version of AfterNight reads format "
            "{}, and they could not be set aside, so no artifact is reused",
            dbFile.toStdString(), foundFormat, ImageIntegration::schema::kArtifactTrackerSchemaVersion);
        return;
    }
    LOG_WARNING(
        "ArtifactTracker: the artifact records '{}' state format {}; this version of AfterNight reads format {}. "
        "They were set aside as '{}', and every artifact they recorded is computed again.",
        dbFile.toStdString(), foundFormat, ImageIntegration::schema::kArtifactTrackerSchemaVersion,
        setAside.toStdString());
    if (db.open())
    {
        (void)openArtifactStore(db, m_counters, foundFormat);
    }
}

ArtifactTracker::~ArtifactTracker()
{
    {
        auto db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen())
        {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool ArtifactTracker::isArtifactValid(const QString& path, const QString& expectedManifestHash) const
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;

    // Check DB
    auto db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT hash, size_bytes, modified_at, artifact_stamp, identity_token "
                             "FROM artifacts WHERE path = :path"));
    q.bindValue(QStringLiteral(":path"), path);
    if (!core::executeSqliteStatement(q, m_counters) || !q.next())
        return false;

    if (q.value(0).toString() != expectedManifestHash)
        return false;

    if (q.value(1).toLongLong() != info.size())
        return false;

    if (q.value(2).isNull() || q.value(2).toLongLong() != modificationStamp(info))
        return false;

    const QString storedStamp = q.value(3).toString();
    const QString storedIdentity = q.value(4).toString();

    // Nothing has modified the file since it was published: the cheapest and most common answer.
    if (!storedIdentity.isEmpty() && storedIdentity == fileIdentityToken(path))
        return true;

    // The file was replaced, restored or relocated. The identity it carries in its own header
    // decides whether it is still the artifact this record describes, at the cost of a header
    // read rather than a payload read.
    if (!storedStamp.isEmpty())
        return artifactStampForFile(m_stampReader, path) == storedStamp;

    // Nothing left to check the file against: recompute it. A master's row also holds the digest
    // taken when it was committed, which is the run archive's provenance; it never decides reuse,
    // because comparing it would read the whole payload.
    return false;
}

bool ArtifactTracker::hasArtifactRecord(const QString& path) const
{
    auto db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM artifacts WHERE path = :path LIMIT 1"));
    q.bindValue(QStringLiteral(":path"), path);
    return core::executeSqliteStatement(q, m_counters) && q.next();
}

std::vector<ArtifactRecordSnapshot> ArtifactTracker::artifactRecords() const
{
    std::vector<ArtifactRecordSnapshot> records;
    auto db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen())
        return records;

    QSqlQuery query(db);
    if (!core::executeSqliteStatement(
            query,
            QStringLiteral("SELECT path, hash, size_bytes, modified_at, content_digest, digest_provenance, "
                           "artifact_stamp, identity_token "
                           "FROM artifacts ORDER BY path"),
            m_counters))
        return records;

    while (query.next())
    {
        ArtifactRecordSnapshot record;
        record.path = query.value(0).toString();
        record.manifestHash = query.value(1).toString();
        record.sizeBytes = query.value(2).toLongLong();
        record.modifiedAtMs = query.value(3).toLongLong();
        record.contentDigest = query.value(4).toString();
        record.digestProvenance = query.value(5).toString();
        record.artifactStamp = query.value(6).toString();
        record.identityToken = query.value(7).toString();
        records.push_back(std::move(record));
    }
    return records;
}

bool ArtifactTracker::isOpen() const
{
    return QSqlDatabase::database(m_connectionName).isOpen();
}

QString ArtifactTracker::fileIdentityForPath(const QString& path)
{
    return fileIdentityToken(path);
}

std::optional<FileRevisionStamp> ArtifactTracker::captureFileRevision(const QString& path)
{
    const QFileInfo initial_info(path);
    if (!initial_info.exists() || !initial_info.isFile())
        return std::nullopt;

    const QString absolute_path = QDir::cleanPath(initial_info.absoluteFilePath());
    const RevisionHandle handle(absolute_path);
    const auto native_handle = handle.native();
    if (!native_handle.has_value())
        return std::nullopt;

    const auto opened_before = nativeFileSnapshot(*native_handle);
    const QFileInfo before(absolute_path);
    const QDateTime modified_at = before.lastModified();
    if (!opened_before.has_value() || opened_before->identityToken.isEmpty() || !modified_at.isValid() ||
        fileIdentityToken(absolute_path) != opened_before->identityToken || before.size() != opened_before->sizeBytes)
    {
        return std::nullopt;
    }

    const auto opened_after = nativeFileSnapshot(*native_handle);
    const QFileInfo after(absolute_path);
    if (!opened_after.has_value() || opened_before->identityToken != opened_after->identityToken ||
        opened_before->sizeBytes != opened_after->sizeBytes ||
        fileIdentityToken(absolute_path) != opened_after->identityToken || after.size() != opened_after->sizeBytes ||
        after.lastModified().toMSecsSinceEpoch() != modified_at.toMSecsSinceEpoch())
    {
        return std::nullopt;
    }

    return FileRevisionStamp{absolute_path, opened_before->sizeBytes, modified_at.toMSecsSinceEpoch(),
                             opened_before->identityToken};
}

bool ArtifactTracker::recordArtifact(const QString& path, const QString& manifestSectionHash,
                                     const std::function<bool()>& shouldCancel)
{
    const auto db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen())
        return false;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;

    if (shouldCancel && shouldCancel())
        return false;

    // The caller published this file through an atomic rename, so it is complete by construction.
    // Recording its own header identity is what lets a later run reuse it without reading a byte
    // of payload; see docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md.
    return publishArtifactRecord(
        path, manifestSectionHash, QString(),
        serializedProvenance(ImageIntegration::schema::kArtifactDigestProvenanceReadbackVerified), shouldCancel);
}

bool ArtifactTracker::recordArtifact(const QString& path, const QString& manifestSectionHash,
                                     const an::ArtifactIdentity& stamped, const std::function<bool()>& shouldCancel)
{
    // The writer's word for what it stamped: reading the header back would be the file's first open since its writer
    // closed it, which waits for any antivirus scan the close started.
    return publishArtifactRecord(
        path, manifestSectionHash, QString(),
        serializedProvenance(ImageIntegration::schema::kArtifactDigestProvenanceReadbackVerified), shouldCancel,
        nullptr, &stamped);
}

bool ArtifactTracker::recordCurrentRunCommittedArtifact(const FinalCommittedArtifact& artifact,
                                                        const QString& manifestSectionHash,
                                                        const std::function<bool()>& shouldCancel)
{
    if (!artifact.matchesFileSnapshot())
        return false;

    return publishArtifactRecord(
        artifact.path(), manifestSectionHash, artifact.digest().sha256(),
        serializedProvenance(ImageIntegration::schema::kArtifactDigestProvenanceCurrentRunCommitted), shouldCancel,
        &artifact);
}

bool ArtifactTracker::publishArtifactRecord(const QString& path, const QString& manifestSectionHash,
                                            const QString& digest, const QString& digestProvenance,
                                            const std::function<bool()>& shouldCancel,
                                            const FinalCommittedArtifact* committedArtifact,
                                            const an::ArtifactIdentity* stamped)
{
    auto db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen())
        return false;
    if (shouldCancel && shouldCancel())
        return false;
    if (committedArtifact != nullptr && !committedArtifact->matchesFileSnapshot(path))
        return false;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;
    const qint64 created_at = info.birthTime().toSecsSinceEpoch();
    const qint64 size_bytes = info.size();
    const qint64 modified_at = modificationStamp(info);

    // The header identity survives a copy or restore, the platform identity makes the untouched
    // case answerable from a single stat. The header identity is the one its writer stated when
    // there is one, read from the file as published otherwise.
    // A default-constructed QString binds as SQL NULL, which these NOT NULL columns reject, so
    // every optional value is coerced to an empty string first.
    const auto notNull = [](const QString& value) { return value.isNull() ? QStringLiteral("") : value; };
    const QString artifact_stamp = notNull(stamped != nullptr ? QString::fromStdString(stamped->artifactId())
                                                              : artifactStampForFile(m_stampReader, path));
    const QString identity_token = notNull(fileIdentityToken(path));
    if (shouldCancel && shouldCancel())
        return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO artifacts "
                             "(path, hash, created_at, size_bytes, modified_at, content_digest, digest_provenance, "
                             "artifact_stamp, identity_token) "
                             "VALUES (:path, :hash, :created, :size, :modified, :digest, :provenance, :stamp, "
                             ":identity) "
                             "ON CONFLICT(path) DO UPDATE SET "
                             "hash = excluded.hash, "
                             "created_at = excluded.created_at, "
                             "size_bytes = excluded.size_bytes, "
                             "modified_at = excluded.modified_at, "
                             "content_digest = excluded.content_digest, "
                             "digest_provenance = excluded.digest_provenance, "
                             "artifact_stamp = excluded.artifact_stamp, "
                             "identity_token = excluded.identity_token"));
    q.bindValue(QStringLiteral(":path"), path);
    q.bindValue(QStringLiteral(":hash"), manifestSectionHash);
    q.bindValue(QStringLiteral(":created"), created_at);
    q.bindValue(QStringLiteral(":size"), size_bytes);
    q.bindValue(QStringLiteral(":modified"), modified_at);
    q.bindValue(QStringLiteral(":digest"), notNull(digest));
    q.bindValue(QStringLiteral(":provenance"), digestProvenance);
    q.bindValue(QStringLiteral(":stamp"), artifact_stamp);
    q.bindValue(QStringLiteral(":identity"), identity_token);
    if (shouldCancel && shouldCancel())
        return false;
    if (committedArtifact != nullptr && !committedArtifact->matchesFileSnapshot(path))
        return false;

    return core::executeSqliteStatement(q, m_counters);
}

QString ArtifactTracker::contentDigestForFile(const QString& path, WorkflowExecutionCounterAccumulator* counters,
                                              const std::function<bool()>& shouldCancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    const QString absolutePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto nativeHandle = nativeHandleForFile(file);
    const auto openedBefore = nativeHandle.has_value() ? nativeFileSnapshot(*nativeHandle) : std::nullopt;
    if (!openedBefore.has_value() || openedBefore->identityToken.isEmpty() ||
        fileIdentityToken(absolutePath) != openedBefore->identityToken)
    {
        return {};
    }

    const QString digest = contentDigestForOpenFile(file, counters, shouldCancel);
    if (digest.isEmpty())
    {
        return {};
    }

    const auto openedAfter = nativeFileSnapshot(*nativeHandle);
    if (!openedAfter.has_value() || openedAfter->identityToken != openedBefore->identityToken ||
        openedAfter->sizeBytes != openedBefore->sizeBytes ||
        fileIdentityToken(absolutePath) != openedAfter->identityToken)
    {
        return {};
    }
    return digest;
#else
    return contentDigestForOpenFile(file, counters, shouldCancel);
#endif
}
