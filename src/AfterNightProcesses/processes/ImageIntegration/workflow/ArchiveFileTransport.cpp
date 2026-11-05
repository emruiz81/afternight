// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ArchiveFileTransport.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <filesystem>
#include <cwchar>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <Windows.h>
#include <winioctl.h>
#endif

namespace ImageIntegration
{
namespace
{
class CandidateFileGuard
{
public:
    explicit CandidateFileGuard(QString path) : m_path(std::move(path)) {}

    ~CandidateFileGuard()
    {
        remove();
    }

    CandidateFileGuard(const CandidateFileGuard&) = delete;
    CandidateFileGuard& operator=(const CandidateFileGuard&) = delete;

    void remove()
    {
        if (m_path.isEmpty())
        {
            return;
        }

        const QFileInfo info(m_path);
        if (info.isDir())
        {
            (void)QDir(m_path).removeRecursively();
        }
        else
        {
            (void)QFile::remove(m_path);
        }
        m_path.clear();
    }

    void release() noexcept
    {
        m_path.clear();
    }

private:
    QString m_path;
};

[[nodiscard]] std::uint64_t nonNegativeBytes(const qint64 bytes) noexcept
{
    return bytes > 0 ? static_cast<std::uint64_t>(bytes) : 0;
}

void saturatingAdd(std::uint64_t& destination, const std::uint64_t amount) noexcept
{
    constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();
    destination = amount > kMaximum - destination ? kMaximum : destination + amount;
}

[[nodiscard]] QString uniqueSiblingCandidatePath(const QString& destinationPath)
{
    const QFileInfo destinationInfo(destinationPath);
    const QString fileName =
        destinationInfo.fileName().isEmpty() ? QStringLiteral("archive-payload") : destinationInfo.fileName();
    return destinationInfo.dir().filePath(QStringLiteral(".%1.afternight-archive-%2.tmp")
                                              .arg(fileName, QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void cleanupOldSiblingCandidates(const QDir& destinationDirectory)
{
    constexpr qint64 kMinimumOrphanAgeSeconds = 24 * 60 * 60;
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-kMinimumOrphanAgeSeconds);
    const QStringList candidates = destinationDirectory.entryList(
        QStringList{QStringLiteral(".*.afternight-archive-*.tmp")}, QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    for (const auto& fileName : candidates)
    {
        const QFileInfo info(destinationDirectory.filePath(fileName));
        if (info.lastModified().toUTC() < cutoff)
        {
            (void)QFile::remove(info.absoluteFilePath());
        }
    }
}

[[nodiscard]] std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

[[nodiscard]] bool sameFileIdentity(const QString& leftPath, const QString& rightPath)
{
    if (!QFileInfo::exists(leftPath) || !QFileInfo::exists(rightPath))
    {
        return false;
    }

    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(filesystemPath(leftPath), filesystemPath(rightPath), error);
    return !error && equivalent;
}

[[nodiscard]] bool validCandidate(const QString& candidatePath, const QString& sourcePath, const qint64 expectedBytes,
                                  const bool requireExactSize)
{
    const QFileInfo candidate(candidatePath);
    return candidate.exists() && candidate.isFile() && (!requireExactSize || candidate.size() == expectedBytes) &&
           !sameFileIdentity(sourcePath, candidatePath);
}

[[nodiscard]] ArchivePublishAttemptResult
safelyPublish(const ArchiveTransportOperations::PublishOperation& publishOperation,
              const ArchivePublishRequest& request)
{
    try
    {
        return publishOperation(request);
    }
    catch (...)
    {
        return {ArchivePublishAttemptStatus::Failed,
                QStringLiteral("the archive publication backend threw an exception")};
    }
}

[[nodiscard]] ArchivePublishAttemptResult publishCandidate(const ArchivePublishRequest& request)
{
#ifdef Q_OS_WIN
    const std::wstring candidate = request.candidatePath().toStdWString();
    const std::wstring destination = request.destinationPath().toStdWString();
    if (MoveFileExW(candidate.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
    {
        return {ArchivePublishAttemptStatus::Succeeded, {}};
    }
    return {ArchivePublishAttemptStatus::Failed,
            QStringLiteral("atomic archive publication failed with Windows error %1").arg(GetLastError())};
#else
    std::error_code error;
    std::filesystem::rename(filesystemPath(request.candidatePath()), filesystemPath(request.destinationPath()), error);
    if (!error)
    {
        return {ArchivePublishAttemptStatus::Succeeded, {}};
    }
    return {ArchivePublishAttemptStatus::Failed,
            QStringLiteral("atomic archive publication failed: %1").arg(QString::fromStdString(error.message()))};
#endif
}

[[nodiscard]] ArchiveFallbackReason fallbackReasonFor(const ArchiveCloneAttemptStatus status)
{
    switch (status)
    {
    case ArchiveCloneAttemptStatus::Unsupported:
        return ArchiveFallbackReason::CloneUnsupported;
    case ArchiveCloneAttemptStatus::CrossVolume:
        return ArchiveFallbackReason::CrossVolume;
    case ArchiveCloneAttemptStatus::PermissionDenied:
        return ArchiveFallbackReason::PermissionDenied;
    case ArchiveCloneAttemptStatus::UnsupportedExtentAlignment:
        return ArchiveFallbackReason::UnsupportedExtentAlignment;
    case ArchiveCloneAttemptStatus::CapabilityFailure:
        return ArchiveFallbackReason::PlatformCapabilityFailure;
    case ArchiveCloneAttemptStatus::Succeeded:
    case ArchiveCloneAttemptStatus::SourceChanged:
    case ArchiveCloneAttemptStatus::Failed:
        return ArchiveFallbackReason::CloneAttemptFailed;
    }
    return ArchiveFallbackReason::CloneAttemptFailed;
}

[[nodiscard]] bool isUnsupportedFallback(const ArchiveCloneAttemptStatus status)
{
    return status == ArchiveCloneAttemptStatus::Unsupported || status == ArchiveCloneAttemptStatus::CrossVolume ||
           status == ArchiveCloneAttemptStatus::PermissionDenied ||
           status == ArchiveCloneAttemptStatus::UnsupportedExtentAlignment ||
           status == ArchiveCloneAttemptStatus::CapabilityFailure;
}

[[nodiscard]] bool isCloneTransport(const ArchiveSelectedTransport transport) noexcept
{
    return transport == ArchiveSelectedTransport::WindowsBlockClone ||
           transport == ArchiveSelectedTransport::LinuxReflink;
}

#ifdef Q_OS_LINUX
class LinuxFileDescriptor
{
public:
    explicit LinuxFileDescriptor(const int descriptor = -1) noexcept : m_descriptor(descriptor) {}

    ~LinuxFileDescriptor()
    {
        if (valid())
        {
            (void)::close(m_descriptor);
        }
    }

    LinuxFileDescriptor(const LinuxFileDescriptor&) = delete;
    LinuxFileDescriptor& operator=(const LinuxFileDescriptor&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return m_descriptor >= 0;
    }

    [[nodiscard]] int get() const noexcept
    {
        return m_descriptor;
    }

private:
    int m_descriptor = -1;
};

[[nodiscard]] bool sameLinuxFileIdentity(const struct stat& left, const struct stat& right) noexcept
{
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

[[nodiscard]] bool sameLinuxSourceSnapshot(const struct stat& left, const struct stat& right) noexcept
{
    return sameLinuxFileIdentity(left, right) && left.st_size == right.st_size &&
           left.st_mtim.tv_sec == right.st_mtim.tv_sec && left.st_mtim.tv_nsec == right.st_mtim.tv_nsec &&
           left.st_ctim.tv_sec == right.st_ctim.tv_sec && left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}

[[nodiscard]] qint64 linuxModificationTimeMs(const struct stat& fileStatus) noexcept
{
    return static_cast<qint64>(fileStatus.st_mtim.tv_sec) * 1000LL +
           static_cast<qint64>(fileStatus.st_mtim.tv_nsec / 1000000L);
}

[[nodiscard]] bool linuxDescriptorMatchesArtifact(const struct stat& fileStatus,
                                                  const FinalCommittedArtifact& artifact) noexcept
{
    return S_ISREG(fileStatus.st_mode) && fileStatus.st_size == artifact.sizeBytes() &&
           linuxModificationTimeMs(fileStatus) == artifact.modificationTimestamp().toMSecsSinceEpoch();
}

[[nodiscard]] bool linuxPathMatchesDescriptor(const QString& path, const struct stat& descriptorStatus)
{
    const QByteArray encodedPath = QFile::encodeName(path);
    struct stat pathStatus{};
    return ::stat(encodedPath.constData(), &pathStatus) == 0 && sameLinuxFileIdentity(pathStatus, descriptorStatus);
}

[[nodiscard]] bool linuxSourceRemainsStable(const int descriptor, const struct stat& initialStatus,
                                            const FinalCommittedArtifact& artifact)
{
    struct stat currentStatus{};
    return ::fstat(descriptor, &currentStatus) == 0 && sameLinuxSourceSnapshot(initialStatus, currentStatus) &&
           linuxDescriptorMatchesArtifact(currentStatus, artifact) &&
           linuxPathMatchesDescriptor(artifact.path(), currentStatus);
}

[[nodiscard]] ArchiveCloneAttemptResult linuxSourceChanged(const QString& detail)
{
    return {ArchiveCloneAttemptStatus::SourceChanged, detail, 0u, 0u, ArchiveSelectedTransport::LinuxReflink};
}

[[nodiscard]] ArchiveCloneAttemptStatus classifyLinuxReflinkError(const int error)
{
    switch (error)
    {
    case EOPNOTSUPP:
    case ENOTTY:
    case ENOSYS:
        return ArchiveCloneAttemptStatus::Unsupported;
    case EXDEV:
        return ArchiveCloneAttemptStatus::CrossVolume;
    case EACCES:
    case EPERM:
    case EROFS:
    case ETXTBSY:
        return ArchiveCloneAttemptStatus::PermissionDenied;
    case ENOENT:
    case ESTALE:
        return ArchiveCloneAttemptStatus::SourceChanged;
    case EINVAL:
        return ArchiveCloneAttemptStatus::CapabilityFailure;
    case EDQUOT:
    case EFBIG:
    case ENOSPC:
    case EIO:
        return ArchiveCloneAttemptStatus::Failed;
    default:
        return ArchiveCloneAttemptStatus::CapabilityFailure;
    }
}

[[nodiscard]] ArchiveCloneAttemptResult linuxReflinkFailure(const QString& operation, const int error)
{
    return {classifyLinuxReflinkError(error),
            QStringLiteral("%1 failed: %2 (errno %3)")
                .arg(operation, QString::fromStdString(std::generic_category().message(error)))
                .arg(error),
            0u, 0u, ArchiveSelectedTransport::LinuxReflink};
}

[[nodiscard]] ArchiveCloneAttemptResult linuxReflink(const ArchiveCloneRequest& request)
{
    const FinalCommittedArtifact& artifact = request.immutableSource();
    const QByteArray sourcePath = QFile::encodeName(artifact.path());
    const QByteArray destinationPath = QFile::encodeName(request.temporaryDestinationPath());

    LinuxFileDescriptor source(::open(sourcePath.constData(), O_RDONLY | O_CLOEXEC));
    if (!source.valid())
    {
        return linuxReflinkFailure(QStringLiteral("opening the immutable reflink source"), errno);
    }

    struct stat sourceBefore{};
    if (::fstat(source.get(), &sourceBefore) != 0)
    {
        return linuxReflinkFailure(QStringLiteral("querying the immutable reflink source"), errno);
    }
    if (!artifact.matchesOpenFileSnapshot(source.get()) || !linuxDescriptorMatchesArtifact(sourceBefore, artifact) ||
        !linuxPathMatchesDescriptor(artifact.path(), sourceBefore))
    {
        return linuxSourceChanged(QStringLiteral("the immutable source changed before the Linux reflink attempt"));
    }

    const mode_t candidateMode = sourceBefore.st_mode & static_cast<mode_t>(0777);
    LinuxFileDescriptor destination(
        ::open(destinationPath.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, candidateMode));
    if (!destination.valid())
    {
        const int operationError = errno;
        if (!linuxSourceRemainsStable(source.get(), sourceBefore, artifact))
        {
            return linuxSourceChanged(
                QStringLiteral("the immutable source changed while creating the Linux reflink candidate"));
        }
        return linuxReflinkFailure(QStringLiteral("creating the Linux reflink candidate"), operationError);
    }

    struct stat destinationBefore{};
    if (::fstat(destination.get(), &destinationBefore) != 0)
    {
        return linuxReflinkFailure(QStringLiteral("querying the Linux reflink candidate"), errno);
    }
    if (!S_ISREG(destinationBefore.st_mode))
    {
        return {ArchiveCloneAttemptStatus::CapabilityFailure,
                QStringLiteral("the Linux reflink candidate is not a regular file"), 0u, 0u,
                ArchiveSelectedTransport::LinuxReflink};
    }
    if (sameLinuxFileIdentity(sourceBefore, destinationBefore))
    {
        return {ArchiveCloneAttemptStatus::Failed,
                QStringLiteral("the Linux reflink candidate aliases the immutable source"), 0u, 0u,
                ArchiveSelectedTransport::LinuxReflink};
    }
    if (sourceBefore.st_dev != destinationBefore.st_dev)
    {
        return {ArchiveCloneAttemptStatus::CrossVolume,
                QStringLiteral("Linux reflinking requires source and destination on the same filesystem"), 0u, 0u,
                ArchiveSelectedTransport::LinuxReflink};
    }

    if (::ioctl(destination.get(), FICLONE, source.get()) != 0)
    {
        const int operationError = errno;
        if (!linuxSourceRemainsStable(source.get(), sourceBefore, artifact))
        {
            return linuxSourceChanged(QStringLiteral("the immutable source changed during the Linux reflink attempt"));
        }
        return linuxReflinkFailure(QStringLiteral("cloning the immutable archive payload"), operationError);
    }

    struct stat sourceAfter{};
    struct stat destinationAfter{};
    if (::fstat(source.get(), &sourceAfter) != 0 || !sameLinuxSourceSnapshot(sourceBefore, sourceAfter) ||
        !linuxDescriptorMatchesArtifact(sourceAfter, artifact) ||
        !linuxPathMatchesDescriptor(artifact.path(), sourceAfter))
    {
        return linuxSourceChanged(QStringLiteral("the immutable source changed during the Linux reflink attempt"));
    }
    if (::fstat(destination.get(), &destinationAfter) != 0)
    {
        return linuxReflinkFailure(QStringLiteral("validating the Linux reflink candidate"), errno);
    }
    if (!S_ISREG(destinationAfter.st_mode) || destinationAfter.st_size != artifact.sizeBytes() ||
        sameLinuxFileIdentity(sourceAfter, destinationAfter) ||
        !linuxPathMatchesDescriptor(request.temporaryDestinationPath(), destinationAfter))
    {
        return {ArchiveCloneAttemptStatus::Failed,
                QStringLiteral("the Linux reflink candidate failed size or identity validation"), 0u, 0u,
                ArchiveSelectedTransport::LinuxReflink};
    }
    if (::fsync(destination.get()) != 0)
    {
        const int operationError = errno;
        if (!linuxSourceRemainsStable(source.get(), sourceBefore, artifact))
        {
            return linuxSourceChanged(
                QStringLiteral("the immutable source changed while flushing the Linux reflink candidate"));
        }
        return linuxReflinkFailure(QStringLiteral("flushing the Linux reflink candidate"), operationError);
    }

    struct stat sourceFinal{};
    if (::fstat(source.get(), &sourceFinal) != 0 || !sameLinuxSourceSnapshot(sourceBefore, sourceFinal) ||
        !linuxPathMatchesDescriptor(artifact.path(), sourceFinal) || !artifact.matchesOpenFileSnapshot(source.get()))
    {
        return linuxSourceChanged(QStringLiteral("the immutable source changed while finalizing the Linux reflink"));
    }

    return {ArchiveCloneAttemptStatus::Succeeded,
            {},
            nonNegativeBytes(artifact.sizeBytes()),
            0u,
            ArchiveSelectedTransport::LinuxReflink};
}

[[nodiscard]] ArchiveCopyAttemptResult linuxOrdinaryCopy(const ArchiveCopyRequest& request)
{
    const QByteArray sourcePath = QFile::encodeName(request.sourcePath());
    const QByteArray destinationPath = QFile::encodeName(request.temporaryDestinationPath());
    LinuxFileDescriptor source(::open(sourcePath.constData(), O_RDONLY | O_CLOEXEC));
    if (!source.valid())
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy source could not be opened")};
    }

    struct stat sourceStatus{};
    if (::fstat(source.get(), &sourceStatus) != 0 || !S_ISREG(sourceStatus.st_mode))
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("the ordinary copy source is not an opened regular file")};
    }

    const FinalCommittedArtifact* immutableSource = request.immutableSource();
    if (immutableSource != nullptr && !immutableSource->matchesOpenFileSnapshot(source.get()))
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("the immutable ordinary copy source changed before transfer"), 0u,
                ArchiveCopySourceValidation::ImmutableSourceChanged};
    }

    const mode_t candidateMode = sourceStatus.st_mode & static_cast<mode_t>(0777);
    LinuxFileDescriptor destination(
        ::open(destinationPath.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, candidateMode));
    if (!destination.valid())
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy candidate could not be created")};
    }

    std::vector<char> buffer(1024 * 1024);
    std::uint64_t copiedBytes = 0;
    while (true)
    {
        ssize_t bytesRead = -1;
        do
        {
            bytesRead = ::read(source.get(), buffer.data(), buffer.size());
        } while (bytesRead < 0 && errno == EINTR);

        if (bytesRead < 0)
        {
            return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy source read failed")};
        }
        if (bytesRead == 0)
        {
            break;
        }

        ssize_t bytesWritten = 0;
        while (bytesWritten < bytesRead)
        {
            ssize_t written = -1;
            do
            {
                written = ::write(destination.get(), buffer.data() + bytesWritten,
                                  static_cast<size_t>(bytesRead - bytesWritten));
            } while (written < 0 && errno == EINTR);
            if (written <= 0)
            {
                return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy candidate write failed")};
            }
            bytesWritten += written;
        }
        saturatingAdd(copiedBytes, static_cast<std::uint64_t>(bytesRead));
    }

    if (::fchmod(destination.get(), candidateMode) != 0)
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("the ordinary copy candidate permissions could not be preserved")};
    }
    if (::fsync(destination.get()) != 0)
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy candidate flush failed")};
    }
    if (immutableSource != nullptr && !immutableSource->matchesOpenFileSnapshot(source.get()))
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("the immutable ordinary copy source changed during transfer"), copiedBytes,
                ArchiveCopySourceValidation::ImmutableSourceChanged};
    }

    return {ArchiveCopyAttemptStatus::Succeeded,
            {},
            copiedBytes,
            immutableSource != nullptr ? ArchiveCopySourceValidation::StableImmutableSource
                                       : ArchiveCopySourceValidation::NotRequested};
}
#endif

#ifdef Q_OS_WIN
class WindowsHandle
{
public:
    explicit WindowsHandle(const HANDLE handle = INVALID_HANDLE_VALUE) noexcept : m_handle(handle) {}

    ~WindowsHandle()
    {
        if (valid())
        {
            CloseHandle(m_handle);
        }
    }

    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr;
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return m_handle;
    }

    void close() noexcept
    {
        if (valid())
        {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

[[nodiscard]] ArchiveCloneAttemptStatus classifyWindowsCloneError(const DWORD error, const bool partialClone)
{
    if (partialClone)
    {
        return ArchiveCloneAttemptStatus::Failed;
    }

    switch (error)
    {
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
        return ArchiveCloneAttemptStatus::Unsupported;
    case ERROR_NOT_SAME_DEVICE:
        return ArchiveCloneAttemptStatus::CrossVolume;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_WRITE_PROTECT:
        return ArchiveCloneAttemptStatus::PermissionDenied;
    case ERROR_INVALID_PARAMETER:
        return ArchiveCloneAttemptStatus::UnsupportedExtentAlignment;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return ArchiveCloneAttemptStatus::Failed;
    default:
        return ArchiveCloneAttemptStatus::CapabilityFailure;
    }
}

[[nodiscard]] ArchiveCloneAttemptResult windowsCloneFailure(const QString& operation, const DWORD error,
                                                            const bool partialClone = false)
{
    return {classifyWindowsCloneError(error, partialClone),
            QStringLiteral("%1 failed with Windows error %2").arg(operation).arg(static_cast<qulonglong>(error))};
}

[[nodiscard]] bool setWindowsFileOffset(const HANDLE handle, const qint64 offset)
{
    LARGE_INTEGER position;
    position.QuadPart = offset;
    return SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) != 0;
}

[[nodiscard]] ArchiveCloneAttemptResult copyWindowsCloneTail(const HANDLE source, const HANDLE destination,
                                                             const qint64 offset, const qint64 byteCount)
{
    if (byteCount == 0)
    {
        return {ArchiveCloneAttemptStatus::Succeeded, {}};
    }
    if (byteCount < 0 || static_cast<std::uint64_t>(byteCount) > (std::numeric_limits<DWORD>::max)())
    {
        return {ArchiveCloneAttemptStatus::UnsupportedExtentAlignment,
                QStringLiteral("the final unaligned extent is too large for a bounded Windows write")};
    }
    if (!setWindowsFileOffset(source, offset) || !setWindowsFileOffset(destination, offset))
    {
        return windowsCloneFailure(QStringLiteral("positioning the unaligned archive extent"), GetLastError());
    }

    std::vector<char> buffer(static_cast<size_t>(byteCount));
    DWORD bytesRead = 0;
    const BOOL readSucceeded = ReadFile(source, buffer.data(), static_cast<DWORD>(byteCount), &bytesRead, nullptr);
    if (readSucceeded == 0 || bytesRead != static_cast<DWORD>(byteCount))
    {
        const DWORD error = readSucceeded == 0 ? GetLastError() : ERROR_HANDLE_EOF;
        return windowsCloneFailure(QStringLiteral("reading the unaligned archive extent"), error);
    }

    DWORD bytesWritten = 0;
    const BOOL writeSucceeded = WriteFile(destination, buffer.data(), bytesRead, &bytesWritten, nullptr);
    if (writeSucceeded == 0 || bytesWritten != bytesRead)
    {
        const DWORD error = writeSucceeded == 0 ? GetLastError() : ERROR_WRITE_FAULT;
        return windowsCloneFailure(QStringLiteral("writing the unaligned archive extent"), error);
    }
    return {ArchiveCloneAttemptStatus::Succeeded, {}};
}

[[nodiscard]] ArchiveCloneAttemptResult windowsBlockClone(const ArchiveCloneRequest& request)
{
    constexpr DWORD kSourceSharing = FILE_SHARE_READ;
    constexpr DWORD kDestinationSharing = FILE_SHARE_READ;
    const FinalCommittedArtifact& artifact = request.immutableSource();
    const std::wstring sourcePath = artifact.path().toStdWString();
    const std::wstring destinationPath = request.temporaryDestinationPath().toStdWString();

    WindowsHandle source(CreateFileW(sourcePath.c_str(), GENERIC_READ, kSourceSharing, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!source.valid())
    {
        return windowsCloneFailure(QStringLiteral("opening the immutable clone source"), GetLastError());
    }
    if (!artifact.matchesOpenFileSnapshot(reinterpret_cast<qintptr>(source.get())))
    {
        return {ArchiveCloneAttemptStatus::SourceChanged,
                QStringLiteral("the opened clone source no longer matches the committed artifact")};
    }

    WindowsHandle destination(CreateFileW(destinationPath.c_str(), GENERIC_READ | GENERIC_WRITE, kDestinationSharing,
                                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!destination.valid())
    {
        return windowsCloneFailure(QStringLiteral("creating the clone candidate"), GetLastError());
    }

    BY_HANDLE_FILE_INFORMATION sourceIdentity{};
    BY_HANDLE_FILE_INFORMATION destinationIdentity{};
    if (GetFileInformationByHandle(source.get(), &sourceIdentity) == 0 ||
        GetFileInformationByHandle(destination.get(), &destinationIdentity) == 0)
    {
        return windowsCloneFailure(QStringLiteral("querying clone file identity"), GetLastError());
    }
    if (sourceIdentity.dwVolumeSerialNumber != destinationIdentity.dwVolumeSerialNumber)
    {
        return {ArchiveCloneAttemptStatus::CrossVolume,
                QStringLiteral("Windows block cloning requires source and destination on the same volume")};
    }
    if (sourceIdentity.nFileIndexHigh == destinationIdentity.nFileIndexHigh &&
        sourceIdentity.nFileIndexLow == destinationIdentity.nFileIndexLow)
    {
        return {ArchiveCloneAttemptStatus::Failed,
                QStringLiteral("the clone candidate unexpectedly aliases the source file identity")};
    }

    LARGE_INTEGER sourceSize{};
    if (GetFileSizeEx(source.get(), &sourceSize) == 0)
    {
        return windowsCloneFailure(QStringLiteral("querying the immutable clone source size"), GetLastError());
    }
    if (sourceSize.QuadPart != artifact.sizeBytes())
    {
        return {ArchiveCloneAttemptStatus::SourceChanged,
                QStringLiteral("the immutable source size changed after its handle was opened")};
    }

    if (!setWindowsFileOffset(destination.get(), artifact.sizeBytes()) || SetEndOfFile(destination.get()) == 0)
    {
        return windowsCloneFailure(QStringLiteral("sizing the clone candidate"), GetLastError());
    }
    if (artifact.sizeBytes() == 0)
    {
        return {ArchiveCloneAttemptStatus::UnsupportedExtentAlignment,
                QStringLiteral("a zero-length file has no block-clone extent")};
    }

    std::wstring volumeRoot(32768, L'\0');
    if (GetVolumePathNameW(sourcePath.c_str(), volumeRoot.data(), static_cast<DWORD>(volumeRoot.size())) == 0)
    {
        return windowsCloneFailure(QStringLiteral("resolving the clone source volume"), GetLastError());
    }
    volumeRoot.resize(std::wcslen(volumeRoot.c_str()));

    DWORD sectorsPerCluster = 0;
    DWORD bytesPerSector = 0;
    DWORD freeClusters = 0;
    DWORD totalClusters = 0;
    if (GetDiskFreeSpaceW(volumeRoot.c_str(), &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters) == 0)
    {
        return windowsCloneFailure(QStringLiteral("querying the clone volume alignment"), GetLastError());
    }
    const std::uint64_t clusterBytes =
        static_cast<std::uint64_t>(sectorsPerCluster) * static_cast<std::uint64_t>(bytesPerSector);
    if (clusterBytes == 0 || clusterBytes > static_cast<std::uint64_t>((std::numeric_limits<qint64>::max)()))
    {
        return {ArchiveCloneAttemptStatus::CapabilityFailure,
                QStringLiteral("the clone volume returned an invalid cluster size")};
    }

    const qint64 alignmentBytes = static_cast<qint64>(clusterBytes);
    const std::vector<ArchiveCloneExtent> extents =
        planArchiveCloneExtents(artifact.sizeBytes(), alignmentBytes, kArchiveCloneChunkBytes);
    const qint64 alignedBytes = artifact.sizeBytes() - (artifact.sizeBytes() % alignmentBytes);
    if (alignedBytes > 0 && extents.empty())
    {
        return {ArchiveCloneAttemptStatus::UnsupportedExtentAlignment,
                QStringLiteral("the Windows block-clone extent plan is invalid")};
    }
    if (alignedBytes == 0)
    {
        return {ArchiveCloneAttemptStatus::UnsupportedExtentAlignment,
                QStringLiteral("the file has no complete cluster-aligned extent to clone")};
    }

    bool clonedExtent = false;
    for (const ArchiveCloneExtent& extent : extents)
    {
        DUPLICATE_EXTENTS_DATA duplicate{};
        duplicate.FileHandle = source.get();
        duplicate.SourceFileOffset.QuadPart = extent.sourceOffset;
        duplicate.TargetFileOffset.QuadPart = extent.destinationOffset;
        duplicate.ByteCount.QuadPart = extent.byteCount;
        DWORD bytesReturned = 0;
        if (DeviceIoControl(destination.get(), FSCTL_DUPLICATE_EXTENTS_TO_FILE, &duplicate, sizeof(duplicate), nullptr,
                            0, &bytesReturned, nullptr) == 0)
        {
            return windowsCloneFailure(QStringLiteral("duplicating an archive file extent"), GetLastError(),
                                       clonedExtent);
        }
        clonedExtent = true;
    }

    const qint64 tailBytes = artifact.sizeBytes() - alignedBytes;
    const ArchiveCloneAttemptResult tailResult =
        copyWindowsCloneTail(source.get(), destination.get(), alignedBytes, tailBytes);
    if (tailResult.status != ArchiveCloneAttemptStatus::Succeeded)
    {
        return {ArchiveCloneAttemptStatus::Failed, tailResult.detail};
    }
    if (FlushFileBuffers(destination.get()) == 0)
    {
        return windowsCloneFailure(QStringLiteral("flushing the clone candidate"), GetLastError(), clonedExtent);
    }
    if (!artifact.matchesOpenFileSnapshot(reinterpret_cast<qintptr>(source.get())))
    {
        return {ArchiveCloneAttemptStatus::SourceChanged,
                QStringLiteral("the immutable clone source changed during transfer")};
    }
    return {ArchiveCloneAttemptStatus::Succeeded, {}, nonNegativeBytes(alignedBytes), nonNegativeBytes(tailBytes)};
}

[[nodiscard]] ArchiveCopyAttemptResult windowsOrdinaryCopy(const ArchiveCopyRequest& request)
{
    constexpr DWORD kImmutableSourceSharing = FILE_SHARE_READ;
    constexpr DWORD kMutableSourceSharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    constexpr DWORD kDestinationSharing = FILE_SHARE_READ;
    const FinalCommittedArtifact* immutableSource = request.immutableSource();
    const DWORD sourceSharing = immutableSource != nullptr ? kImmutableSourceSharing : kMutableSourceSharing;
    const std::wstring sourcePath = request.sourcePath().toStdWString();
    const std::wstring destinationPath = request.temporaryDestinationPath().toStdWString();

    WindowsHandle source(CreateFileW(sourcePath.c_str(), GENERIC_READ, sourceSharing, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!source.valid())
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy source could not be opened")};
    }
    if (immutableSource != nullptr &&
        !immutableSource->matchesOpenFileSnapshot(reinterpret_cast<qintptr>(source.get())))
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("the immutable ordinary copy source changed before transfer"), 0u,
                ArchiveCopySourceValidation::ImmutableSourceChanged};
    }

    WindowsHandle destination(CreateFileW(destinationPath.c_str(), GENERIC_WRITE, kDestinationSharing, nullptr,
                                          CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!destination.valid())
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy candidate could not be created")};
    }

    std::vector<char> buffer(1024 * 1024);
    std::uint64_t copiedBytes = 0;
    while (true)
    {
        DWORD bytesRead = 0;
        if (ReadFile(source.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) == 0)
        {
            return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy source read failed")};
        }
        if (bytesRead == 0)
        {
            break;
        }

        DWORD bytesWritten = 0;
        while (bytesWritten < bytesRead)
        {
            DWORD written = 0;
            if (WriteFile(destination.get(), buffer.data() + bytesWritten, bytesRead - bytesWritten, &written,
                          nullptr) == 0 ||
                written == 0)
            {
                return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy candidate write failed")};
            }
            bytesWritten += written;
        }
        saturatingAdd(copiedBytes, static_cast<std::uint64_t>(bytesRead));
    }

    if (FlushFileBuffers(destination.get()) == 0)
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy candidate flush failed")};
    }
    if (immutableSource != nullptr &&
        !immutableSource->matchesOpenFileSnapshot(reinterpret_cast<qintptr>(source.get())))
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("the immutable ordinary copy source changed during transfer"), copiedBytes,
                ArchiveCopySourceValidation::ImmutableSourceChanged};
    }

    destination.close();
    (void)QFile::setPermissions(request.temporaryDestinationPath(), QFileInfo(request.sourcePath()).permissions());
    return {ArchiveCopyAttemptStatus::Succeeded,
            {},
            copiedBytes,
            immutableSource != nullptr ? ArchiveCopySourceValidation::StableImmutableSource
                                       : ArchiveCopySourceValidation::NotRequested};
}
#endif

[[nodiscard]] ArchiveCopyAttemptResult ordinaryCopy(const ArchiveCopyRequest& request)
{
#ifdef Q_OS_LINUX
    return linuxOrdinaryCopy(request);
#elif defined(Q_OS_WIN)
    return windowsOrdinaryCopy(request);
#else
    if (request.immutableSource() != nullptr)
    {
        return {ArchiveCopyAttemptStatus::Failed,
                QStringLiteral("stable immutable-source copying is unsupported on this platform")};
    }
    if (!QFile::copy(request.sourcePath(), request.temporaryDestinationPath()))
    {
        return {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary file copy failed")};
    }
    return {ArchiveCopyAttemptStatus::Succeeded,
            {},
            nonNegativeBytes(QFileInfo(request.temporaryDestinationPath()).size())};
#endif
}

[[nodiscard]] ArchiveTransportOperations platformNeutralOperations()
{
    ArchiveTransportOperations operations;
#ifdef Q_OS_LINUX
    operations.clone = linuxReflink;
#elif defined(Q_OS_WIN)
    operations.clone = windowsBlockClone;
#endif
    operations.copy = ordinaryCopy;
    operations.publish = publishCandidate;
    return operations;
}
} // namespace

std::vector<ArchiveCloneExtent> planArchiveCloneExtents(const qint64 logicalSizeBytes, const qint64 alignmentBytes,
                                                        const qint64 maximumChunkBytes)
{
    if (logicalSizeBytes < 0 || alignmentBytes <= 0 || maximumChunkBytes < alignmentBytes)
    {
        return {};
    }

    const qint64 boundedChunkBytes = maximumChunkBytes - (maximumChunkBytes % alignmentBytes);
    if (boundedChunkBytes <= 0)
    {
        return {};
    }

    const qint64 alignedBytes = logicalSizeBytes - (logicalSizeBytes % alignmentBytes);
    std::vector<ArchiveCloneExtent> extents;
    if (alignedBytes == 0)
    {
        return extents;
    }

    const auto extentCount = static_cast<std::uint64_t>(alignedBytes / boundedChunkBytes) +
                             (alignedBytes % boundedChunkBytes == 0 ? 0u : 1u);
    if (extentCount <= static_cast<std::uint64_t>((std::numeric_limits<size_t>::max)()))
    {
        extents.reserve(static_cast<size_t>(extentCount));
    }

    qint64 offset = 0;
    while (offset < alignedBytes)
    {
        const qint64 remaining = alignedBytes - offset;
        const qint64 byteCount = (std::min)(remaining, boundedChunkBytes);
        extents.push_back({offset, offset, byteCount});
        offset += byteCount;
    }
    return extents;
}

ArchiveFileTransportRequest
ArchiveFileTransportRequest::forImmutableArtifact(const FinalCommittedArtifact& artifact, QString destinationPath,
                                                  WorkflowExecutionCounterAccumulator* counters)
{
    return {artifact.path(), std::move(destinationPath), artifact, counters};
}

ArchiveFileTransportRequest ArchiveFileTransportRequest::forOrdinaryCopy(QString sourcePath, QString destinationPath,
                                                                         WorkflowExecutionCounterAccumulator* counters)
{
    return {std::move(sourcePath), std::move(destinationPath), std::nullopt, counters};
}

void ArchiveTransportDiagnostics::accumulate(const ArchiveTransportDiagnostics& other) noexcept
{
    saturatingAdd(cloneAttempts, other.cloneAttempts);
    saturatingAdd(cloneSuccesses, other.cloneSuccesses);
    saturatingAdd(unsupportedFallbacks, other.unsupportedFallbacks);
    saturatingAdd(failedAttemptFallbacks, other.failedAttemptFallbacks);
    saturatingAdd(logicalBytesCloned, other.logicalBytesCloned);
    saturatingAdd(ordinaryCopyBytes, other.ordinaryCopyBytes);
    if (other.selectedTransport != ArchiveSelectedTransport::None)
    {
        selectedTransport = other.selectedTransport;
    }
    if (other.fallbackReason != ArchiveFallbackReason::None)
    {
        fallbackReason = other.fallbackReason;
    }
}

bool ArchiveFileTransportResult::succeeded() const noexcept
{
    return outcome == ArchiveFileTransportOutcome::OrdinaryCopySucceeded ||
           outcome == ArchiveFileTransportOutcome::WindowsBlockCloneSucceeded ||
           outcome == ArchiveFileTransportOutcome::LinuxReflinkSucceeded ||
           outcome == ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded ||
           outcome == ArchiveFileTransportOutcome::CloneFailedCopySucceeded ||
           outcome == ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded;
}

ArchiveFileTransport::ArchiveFileTransport() : m_operations(platformNeutralOperations()) {}

ArchiveFileTransport::ArchiveFileTransport(ArchiveTransportOperations operations) : m_operations(std::move(operations))
{
    if (!m_operations.copy)
    {
        m_operations.copy = ordinaryCopy;
    }
    if (!m_operations.publish)
    {
        m_operations.publish = publishCandidate;
    }
}

ArchiveFileTransport ArchiveFileTransport::ordinaryCopyOnly()
{
    ArchiveTransportOperations operations = platformNeutralOperations();
    operations.clone = {};
    return ArchiveFileTransport(std::move(operations));
}

ArchiveFileTransportResult ArchiveFileTransport::transport(const ArchiveFileTransportRequest& request) const
{
    ArchiveFileTransportResult result;
    const QFileInfo sourceInfo(request.sourcePath());
    if (request.sourcePath().trimmed().isEmpty() || request.destinationPath().trimmed().isEmpty() ||
        !sourceInfo.exists() || !sourceInfo.isFile())
    {
        result.error = QStringLiteral("the archive source or destination is invalid");
        return result;
    }

    const QString sourcePath = QDir::cleanPath(sourceInfo.absoluteFilePath());
    const QString destinationPath = QDir::cleanPath(QFileInfo(request.destinationPath()).absoluteFilePath());
    if (sourcePath == destinationPath || sameFileIdentity(sourcePath, destinationPath))
    {
        result.outcome = ArchiveFileTransportOutcome::DestinationPublicationFailure;
        result.error = QStringLiteral("the archive destination aliases the source");
        return result;
    }

    const QDir destinationDirectory = QFileInfo(destinationPath).dir();
    if (!destinationDirectory.exists() && !QDir().mkpath(destinationDirectory.absolutePath()))
    {
        result.outcome = ArchiveFileTransportOutcome::DestinationPublicationFailure;
        result.error = QStringLiteral("the archive destination directory could not be created");
        return result;
    }
    cleanupOldSiblingCandidates(destinationDirectory);

    const FinalCommittedArtifact* immutableArtifact = request.immutableArtifact();
    if (immutableArtifact != nullptr && !immutableArtifact->matchesFileSnapshot(sourcePath))
    {
        result.outcome = ArchiveFileTransportOutcome::SourceChangedBeforeTransfer;
        result.error = QStringLiteral("the immutable archive source changed before transfer");
        return result;
    }

    const qint64 expectedBytes = immutableArtifact != nullptr ? immutableArtifact->sizeBytes() : sourceInfo.size();
    ArchiveCloneAttemptStatus cloneStatus = ArchiveCloneAttemptStatus::Succeeded;
    bool cloneWasAttempted = false;

    if (immutableArtifact != nullptr && m_operations.clone)
    {
        cloneWasAttempted = true;
        result.diagnostics.cloneAttempts = 1;
        const QString candidatePath = uniqueSiblingCandidatePath(destinationPath);
        CandidateFileGuard candidate(candidatePath);

        ArchiveCloneAttemptResult cloneResult;
        try
        {
            cloneResult = m_operations.clone(ArchiveCloneRequest(*immutableArtifact, candidatePath));
        }
        catch (...)
        {
            cloneResult = {ArchiveCloneAttemptStatus::Failed, QStringLiteral("the clone backend threw an exception")};
        }
        cloneStatus = cloneResult.status;

        if (cloneStatus == ArchiveCloneAttemptStatus::SourceChanged ||
            !immutableArtifact->matchesFileSnapshot(sourcePath))
        {
            result.outcome = ArchiveFileTransportOutcome::SourceChangedDuringTransfer;
            result.error = QStringLiteral("the immutable archive source changed during transfer");
            return result;
        }

        if (cloneStatus == ArchiveCloneAttemptStatus::Succeeded && isCloneTransport(cloneResult.successfulTransport) &&
            validCandidate(candidatePath, sourcePath, expectedBytes, true))
        {
            if (!immutableArtifact->matchesFileSnapshot(sourcePath))
            {
                result.outcome = ArchiveFileTransportOutcome::SourceChangedDuringTransfer;
                result.error = QStringLiteral("the immutable archive source changed before clone publication");
                return result;
            }
            const std::uint64_t expectedLogicalBytes = nonNegativeBytes(expectedBytes);
            if (cloneResult.logicalBytesCloned == 0 && cloneResult.ordinaryCopyBytes == 0 && expectedLogicalBytes > 0)
            {
                cloneResult.logicalBytesCloned = expectedLogicalBytes;
            }
            result.diagnostics.cloneSuccesses = 1;
            result.diagnostics.logicalBytesCloned = cloneResult.logicalBytesCloned;
            result.diagnostics.ordinaryCopyBytes = cloneResult.ordinaryCopyBytes;
            const ArchivePublishAttemptResult publishResult =
                safelyPublish(m_operations.publish, ArchivePublishRequest{candidatePath, destinationPath});
            if (publishResult.status != ArchivePublishAttemptStatus::Succeeded)
            {
                result.outcome = ArchiveFileTransportOutcome::DestinationPublicationFailure;
                result.error = publishResult.detail;
                return result;
            }

            candidate.release();
            if (request.counters() != nullptr)
            {
                request.counters()->addBytesCopied(cloneResult.ordinaryCopyBytes);
            }
            result.diagnostics.selectedTransport = cloneResult.successfulTransport;
            result.outcome = cloneResult.successfulTransport == ArchiveSelectedTransport::LinuxReflink
                                 ? ArchiveFileTransportOutcome::LinuxReflinkSucceeded
                                 : ArchiveFileTransportOutcome::WindowsBlockCloneSucceeded;
            return result;
        }

        if (cloneStatus == ArchiveCloneAttemptStatus::Succeeded)
        {
            cloneStatus = ArchiveCloneAttemptStatus::Failed;
            cloneResult.detail = QStringLiteral("the clone backend produced an invalid or aliased candidate");
        }
        candidate.remove();
        result.error = cloneResult.detail;

        if (!immutableArtifact->matchesFileSnapshot(sourcePath))
        {
            result.outcome = ArchiveFileTransportOutcome::SourceChangedDuringTransfer;
            result.error = QStringLiteral("the immutable archive source changed before copy fallback");
            return result;
        }

        result.diagnostics.fallbackReason = fallbackReasonFor(cloneStatus);
        if (isUnsupportedFallback(cloneStatus))
        {
            result.diagnostics.unsupportedFallbacks = 1;
        }
        else
        {
            result.diagnostics.failedAttemptFallbacks = 1;
        }
    }
    else if (immutableArtifact == nullptr)
    {
        result.diagnostics.fallbackReason = ArchiveFallbackReason::NonImmutableSource;
    }

    const QString copyCandidatePath = uniqueSiblingCandidatePath(destinationPath);
    CandidateFileGuard copyCandidate(copyCandidatePath);
    ArchiveCopyAttemptResult copyResult;
    try
    {
        copyResult = m_operations.copy(ArchiveCopyRequest{sourcePath, copyCandidatePath, immutableArtifact});
    }
    catch (...)
    {
        copyResult = {ArchiveCopyAttemptStatus::Failed, QStringLiteral("the ordinary copy backend threw an exception")};
    }

    const std::uint64_t copiedBytes =
        copyResult.copiedBytes == 0 && copyResult.status == ArchiveCopyAttemptStatus::Succeeded && expectedBytes > 0
            ? nonNegativeBytes(expectedBytes)
            : copyResult.copiedBytes;
    if (copyResult.status == ArchiveCopyAttemptStatus::Succeeded ||
        copyResult.sourceValidation == ArchiveCopySourceValidation::ImmutableSourceChanged)
    {
        result.diagnostics.ordinaryCopyBytes = copiedBytes;
    }

    if (immutableArtifact != nullptr &&
        copyResult.sourceValidation == ArchiveCopySourceValidation::ImmutableSourceChanged)
    {
        result.outcome = ArchiveFileTransportOutcome::SourceChangedDuringTransfer;
        result.error = copyResult.detail.isEmpty()
                           ? QStringLiteral("the immutable archive source changed during ordinary copy")
                           : copyResult.detail;
        return result;
    }
    if (immutableArtifact != nullptr && copyResult.status == ArchiveCopyAttemptStatus::Succeeded &&
        copyResult.sourceValidation != ArchiveCopySourceValidation::StableImmutableSource)
    {
        result.outcome = ArchiveFileTransportOutcome::CompleteFailure;
        result.error =
            QStringLiteral("the ordinary copy backend did not attest a stable opened immutable-source identity");
        return result;
    }

    if (copyResult.status != ArchiveCopyAttemptStatus::Succeeded ||
        !validCandidate(copyCandidatePath, sourcePath, expectedBytes, immutableArtifact != nullptr))
    {
        result.outcome = ArchiveFileTransportOutcome::CompleteFailure;
        result.error =
            copyResult.detail.isEmpty() ? QStringLiteral("the ordinary archive copy failed") : copyResult.detail;
        return result;
    }

    if (immutableArtifact != nullptr && !immutableArtifact->matchesFileSnapshot(sourcePath))
    {
        result.outcome = ArchiveFileTransportOutcome::SourceChangedDuringTransfer;
        result.error = QStringLiteral("the immutable archive source changed during ordinary copy");
        return result;
    }

    const ArchivePublishAttemptResult publishResult =
        safelyPublish(m_operations.publish, ArchivePublishRequest{copyCandidatePath, destinationPath});
    if (publishResult.status != ArchivePublishAttemptStatus::Succeeded)
    {
        result.outcome = ArchiveFileTransportOutcome::DestinationPublicationFailure;
        result.error = publishResult.detail;
        return result;
    }

    copyCandidate.release();
    if (request.counters() != nullptr)
    {
        request.counters()->addBytesCopied(copiedBytes);
    }
    result.error.clear();
    result.diagnostics.selectedTransport = ArchiveSelectedTransport::OrdinaryCopy;
    if (immutableArtifact == nullptr)
    {
        result.outcome = ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded;
    }
    else if (!cloneWasAttempted)
    {
        result.outcome = ArchiveFileTransportOutcome::OrdinaryCopySucceeded;
    }
    else if (isUnsupportedFallback(cloneStatus))
    {
        result.outcome = ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded;
    }
    else
    {
        result.outcome = ArchiveFileTransportOutcome::CloneFailedCopySucceeded;
    }
    return result;
}
} // namespace ImageIntegration
