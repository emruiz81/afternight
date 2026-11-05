// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef Q_OS_LINUX
#include <sys/stat.h>
#include <time.h>
#endif

#include "processes/ImageIntegration/workflow/ArchiveFileTransport.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"

namespace
{
using ImageIntegration::ArchiveCloneAttemptResult;
using ImageIntegration::ArchiveCloneAttemptStatus;
using ImageIntegration::ArchiveCopyAttemptResult;
using ImageIntegration::ArchiveCopyAttemptStatus;
using ImageIntegration::ArchiveCopySourceValidation;
using ImageIntegration::ArchiveFallbackReason;
using ImageIntegration::ArchiveFileTransport;
using ImageIntegration::ArchiveFileTransportOutcome;
using ImageIntegration::ArchiveFileTransportRequest;
using ImageIntegration::ArchiveSelectedTransport;
using ImageIntegration::ArchiveTransportOperations;

void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(bytes), bytes.size());
    file.close();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll();
}

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
/// The file's last change as the file system stamped it, in the units of `changeClockNow`.
[[nodiscard]] std::optional<std::int64_t> lastChangeStamp(const QString& path)
{
#ifdef Q_OS_WIN
    const std::wstring nativePath = path.toStdWString();
    const HANDLE handle =
        CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }
    FILE_BASIC_INFO basicInfo{};
    const bool read = GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) != 0;
    CloseHandle(handle);
    return read ? std::optional<std::int64_t>(basicInfo.ChangeTime.QuadPart) : std::nullopt;
#else
    struct stat status{};
    if (::stat(QFile::encodeName(path).constData(), &status) != 0)
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(status.st_ctim.tv_sec) * 1000000000 + status.st_ctim.tv_nsec;
#endif
}

/// The coarse clock the file system stamps a change from, in the units of `lastChangeStamp`.
[[nodiscard]] std::int64_t changeClockNow()
{
#ifdef Q_OS_WIN
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(now.dwHighDateTime) << 32U) | now.dwLowDateTime);
#else
    timespec now{};
    clock_gettime(CLOCK_REALTIME_COARSE, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
#endif
}
#endif

/// Waits until a change made to the file from now on is stamped later than its last one.
///
/// A file's change time is part of its committed identity, and the file system stamps it from a coarse clock: on
/// Windows that clock moves once a clock interrupt - every fifteen and a half milliseconds on a host where no process
/// asks for a finer timer, as on an idle build machine - so a change made within the interrupt of the file's last one
/// carries the same change time, and with the same size and modification time no snapshot can tell the two apart.
[[nodiscard]] bool waitForTheChangeClockToPass(const QString& path)
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    const std::optional<std::int64_t> lastChange = lastChangeStamp(path);
    if (!lastChange.has_value())
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (changeClockNow() <= *lastChange)
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    Q_UNUSED(path);
#endif
    return true;
}

struct OpenPathReplacementAttempt
{
    bool replaced = false;
    bool deniedByOpenHandle = false;
    unsigned long errorCode = 0;
};

OpenPathReplacementAttempt replacePathWhileOpen(const QString& source, const QString& displaced,
                                                const QString& replacement)
{
    OpenPathReplacementAttempt result;
#ifdef Q_OS_WIN
    const std::wstring sourcePath = source.toStdWString();
    const std::wstring displacedPath = displaced.toStdWString();
    const std::wstring replacementPath = replacement.toStdWString();
    if (MoveFileExW(sourcePath.c_str(), displacedPath.c_str(), MOVEFILE_WRITE_THROUGH) == 0)
    {
        result.errorCode = GetLastError();
        result.deniedByOpenHandle = result.errorCode == ERROR_SHARING_VIOLATION ||
                                    result.errorCode == ERROR_ACCESS_DENIED || result.errorCode == ERROR_LOCK_VIOLATION;
        return result;
    }
    if (MoveFileExW(replacementPath.c_str(), sourcePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        0)
    {
        result.errorCode = GetLastError();
        (void)MoveFileExW(displacedPath.c_str(), sourcePath.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        return result;
    }
    result.replaced = true;
#else
    if (!QFile::rename(source, displaced))
    {
        result.errorCode = 1;
        return result;
    }
    if (!QFile::rename(replacement, source))
    {
        result.errorCode = 2;
        (void)QFile::rename(displaced, source);
        return result;
    }
    result.replaced = true;
#endif
    return result;
}

QStringList archiveTransportTemporaries(const QString& root)
{
    QStringList paths;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString path = it.next();
        if (QFileInfo(path).fileName().contains(QStringLiteral(".afternight-archive-")))
        {
            paths.push_back(path);
        }
    }
    return paths;
}

ArchiveCloneAttemptResult copyAsSuccessfulClone(const ImageIntegration::ArchiveCloneRequest& request)
{
    if (!QFile::copy(request.immutableSource().path(), request.temporaryDestinationPath()))
    {
        return {ArchiveCloneAttemptStatus::Failed, QStringLiteral("injected clone copy failed")};
    }
    return {ArchiveCloneAttemptStatus::Succeeded, {}};
}

ArchiveCopyAttemptResult testCopyResult(const ImageIntegration::ArchiveCopyRequest& request, const bool copied,
                                        QString failureDetail = QStringLiteral("injected copy failed"))
{
    if (!copied)
    {
        return {ArchiveCopyAttemptStatus::Failed, std::move(failureDetail)};
    }

    ArchiveCopySourceValidation validation = ArchiveCopySourceValidation::NotRequested;
    if (request.immutableSource() != nullptr)
    {
        validation = request.immutableSource()->matchesFileSnapshot(request.sourcePath())
                         ? ArchiveCopySourceValidation::StableImmutableSource
                         : ArchiveCopySourceValidation::ImmutableSourceChanged;
    }
    return {ArchiveCopyAttemptStatus::Succeeded,
            {},
            static_cast<std::uint64_t>(QFileInfo(request.temporaryDestinationPath()).size()),
            validation};
}

WorkflowManifest createP117Manifest(const QString& outputDir, const QString& executionId)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = outputDir;
    manifest.executionId = executionId;
    manifest.executionMode = ExecutionMode::Full;
    return manifest;
}

WorkflowExecutor::Result createP117Result(const QString& outputDir, const QByteArray& payload,
                                          WorkflowExecutionCounterAccumulator* counters)
{
    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult group;
    group.groupKey = QStringLiteral("P117Group");
    group.success = true;
    group.outputPath = QDir(outputDir).filePath(QStringLiteral("p117_master.bin"));
    group.outputFilename = QStringLiteral("p117_master.bin");
    group.outputBytes = payload.size();
    writeBytes(group.outputPath, payload);

    auto committed_artifact = FinalCommittedArtifact::fromFile(group.outputPath, counters);
    EXPECT_TRUE(committed_artifact.has_value());
    if (committed_artifact.has_value())
    {
        group.committedArtifact = std::move(*committed_artifact);
    }
    result.integrationGroupResults.push_back(std::move(group));
    return result;
}

TEST(P117ArchiveTransport, OrdinaryCopyBackendParity)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload("ordinary-copy-parity\0payload", 28);
    writeBytes(source, payload);

    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport = ArchiveFileTransport::ordinaryCopyOnly();
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::NonImmutableSource);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 0u);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, OrdinaryCopyOnlyReusesCommittedDigestWithoutRehashing)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(8193, 'p');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport = ArchiveFileTransport::ordinaryCopyOnly();
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::OrdinaryCopySucceeded);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 0u);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
    EXPECT_EQ(readBytes(destination), payload);
}

TEST(P117ArchiveTransport, ImmutableCopyBackendMustAttestStableOpenedSourceIdentity)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(8193, 'i');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.copy = [](const ImageIntegration::ArchiveCopyRequest& request) {
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        return ArchiveCopyAttemptResult{copied ? ArchiveCopyAttemptStatus::Succeeded : ArchiveCopyAttemptStatus::Failed,
                                        copied ? QString{} : QStringLiteral("copy failed")};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CompleteFailure);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, CloneSuccessUsesImmutableAuthority)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(8193, 'c');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = copyAsSuccessfulClone;
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::WindowsBlockCloneSucceeded);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::WindowsBlockClone);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 1u);
    EXPECT_EQ(result.diagnostics.logicalBytesCloned, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, UnsupportedCloneFallsBackToFreshOrdinaryCopy)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(4103, 'u');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest&) {
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Unsupported,
                                         QStringLiteral("injected unsupported filesystem")};
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::CloneUnsupported);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 1u);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 0u);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, CrossVolumeCloneClassificationFallsBack)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(5001, 'v');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest&) {
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::CrossVolume,
                                         QStringLiteral("injected cross-volume destination")};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::CrossVolume);
    EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 1u);
    EXPECT_EQ(readBytes(destination), payload);
}

TEST(P117ArchiveTransport, PermissionAndCapabilityFailuresFallBack)
{
    const std::array cases{
        std::pair{ArchiveCloneAttemptStatus::PermissionDenied, ArchiveFallbackReason::PermissionDenied},
        std::pair{ArchiveCloneAttemptStatus::UnsupportedExtentAlignment,
                  ArchiveFallbackReason::UnsupportedExtentAlignment},
        std::pair{ArchiveCloneAttemptStatus::CapabilityFailure, ArchiveFallbackReason::PlatformCapabilityFailure}};

    for (const auto& [cloneStatus, expectedReason] : cases)
    {
        QTemporaryDir temporaryDirectory;
        ASSERT_TRUE(temporaryDirectory.isValid());
        const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
        const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
        const QByteArray payload(6151, static_cast<char>('a' + static_cast<int>(cloneStatus)));
        writeBytes(source, payload);
        auto committed_artifact = FinalCommittedArtifact::fromFile(source);
        ASSERT_TRUE(committed_artifact.has_value());

        ArchiveTransportOperations operations;
        operations.clone = [cloneStatus](const ImageIntegration::ArchiveCloneRequest&) {
            return ArchiveCloneAttemptResult{cloneStatus, QStringLiteral("injected expected clone failure")};
        };
        const ArchiveFileTransport transport(std::move(operations));
        const auto result =
            transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

        EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded);
        EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
        EXPECT_EQ(result.diagnostics.fallbackReason, expectedReason);
        EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 1u);
        EXPECT_EQ(readBytes(destination), payload);
        EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
    }
}

TEST(P117ArchiveTransport, PartialCloneIsCleanedBeforeFallback)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(32779, 'p');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    QString partialPath;
    ArchiveTransportOperations operations;
    operations.clone = [&partialPath](const ImageIntegration::ArchiveCloneRequest& request) {
        partialPath = request.temporaryDestinationPath();
        writeBytes(partialPath, QByteArray("partial"));
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Failed, QStringLiteral("injected partial clone")};
    };
    operations.copy = [&partialPath](const ImageIntegration::ArchiveCopyRequest& request) {
        EXPECT_FALSE(partialPath.isEmpty());
        EXPECT_FALSE(QFileInfo::exists(partialPath));
        EXPECT_NE(request.temporaryDestinationPath(), partialPath);
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        return testCopyResult(request, copied);
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneFailedCopySucceeded);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 1u);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::CloneAttemptFailed);
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_FALSE(QFileInfo::exists(partialPath));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, CloneAndCopyFailurePublishesNothing)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray originalDestination("trusted-existing-archive");
    writeBytes(source, QByteArray(9001, 'f'));
    writeBytes(destination, originalDestination);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest& request) {
        writeBytes(request.temporaryDestinationPath(), QByteArray("partial"));
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Failed, QStringLiteral("clone failed")};
    };
    operations.copy = [](const ImageIntegration::ArchiveCopyRequest&) {
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Failed, QStringLiteral("copy failed")};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CompleteFailure);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::None);
    EXPECT_EQ(readBytes(destination), originalDestination);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, ZeroLengthAndNonAlignedFilesCloneExactly)
{
    for (const QByteArray payload : {QByteArray{}, QByteArray(4096 + 37, 'n')})
    {
        QTemporaryDir temporaryDirectory;
        ASSERT_TRUE(temporaryDirectory.isValid());
        const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
        const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
        writeBytes(source, payload);
        auto committed_artifact = FinalCommittedArtifact::fromFile(source);
        ASSERT_TRUE(committed_artifact.has_value());

        ArchiveTransportOperations operations;
        operations.clone = copyAsSuccessfulClone;
        const ArchiveFileTransport transport(std::move(operations));
        const auto result =
            transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

        EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::WindowsBlockCloneSucceeded);
        EXPECT_EQ(QFileInfo(destination).size(), payload.size());
        EXPECT_EQ(readBytes(destination), payload);
        EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
    }
}

TEST(P117ArchiveTransport, LargeLogicalClonePlanUsesBoundedCheckedChunks)
{
    constexpr qint64 kAlignment = 4096;
    constexpr qint64 kChunk = 64LL * 1024LL * 1024LL;
    constexpr qint64 kLogicalSize = 5LL * 1024LL * 1024LL * 1024LL + 123;
    const auto plan = ImageIntegration::planArchiveCloneExtents(kLogicalSize, kAlignment, kChunk);

    ASSERT_GT(plan.size(), 1u);
    qint64 expectedOffset = 0;
    for (const auto& extent : plan)
    {
        EXPECT_EQ(extent.sourceOffset, expectedOffset);
        EXPECT_EQ(extent.destinationOffset, expectedOffset);
        EXPECT_GT(extent.byteCount, 0);
        EXPECT_LE(extent.byteCount, kChunk);
        EXPECT_EQ(extent.byteCount % kAlignment, 0);
        expectedOffset += extent.byteCount;
    }
    EXPECT_EQ(expectedOffset, kLogicalSize - (kLogicalSize % kAlignment));
    EXPECT_TRUE(ImageIntegration::planArchiveCloneExtents(-1, kAlignment, kChunk).empty());
    EXPECT_TRUE(ImageIntegration::planArchiveCloneExtents(kLogicalSize, 0, kChunk).empty());
    EXPECT_TRUE(ImageIntegration::planArchiveCloneExtents(kLogicalSize, kAlignment, kAlignment - 1).empty());
}

TEST(P117ArchiveTransport, ExistingDestinationRemainsUntilAtomicPublication)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(7001, 'r');
    const QByteArray originalDestination("old-trusted-archive");
    writeBytes(source, payload);
    writeBytes(destination, originalDestination);

    ArchiveTransportOperations operations;
    operations.copy = [&destination, &originalDestination](const ImageIntegration::ArchiveCopyRequest& request) {
        EXPECT_EQ(readBytes(destination), originalDestination);
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        return testCopyResult(request, copied);
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination));

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, PublicationFailurePreservesDestinationAndCleansCandidate)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray originalDestination("old-trusted-archive");
    writeBytes(source, QByteArray(7001, 'd'));
    writeBytes(destination, originalDestination);

    ArchiveTransportOperations operations;
    operations.publish = [](const ImageIntegration::ArchivePublishRequest&) {
        return ImageIntegration::ArchivePublishAttemptResult{ImageIntegration::ArchivePublishAttemptStatus::Failed,
                                                             QStringLiteral("injected publication failure")};
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::DestinationPublicationFailure);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, 7001u);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(readBytes(destination), originalDestination);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, ClonePublicationFailureDoesNotCountCopiedTailBytes)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(7001, 't');
    const QByteArray originalDestination("old-trusted-archive");
    writeBytes(source, payload);
    writeBytes(destination, originalDestination);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = [&payload](const ImageIntegration::ArchiveCloneRequest& request) {
        const bool copied = QFile::copy(request.immutableSource().path(), request.temporaryDestinationPath());
        return ArchiveCloneAttemptResult{copied ? ArchiveCloneAttemptStatus::Succeeded
                                                : ArchiveCloneAttemptStatus::Failed,
                                         copied ? QString{} : QStringLiteral("injected clone copy failed"), 0u,
                                         copied ? static_cast<std::uint64_t>(payload.size()) : 0u};
    };
    operations.publish = [](const ImageIntegration::ArchivePublishRequest&) {
        return ImageIntegration::ArchivePublishAttemptResult{ImageIntegration::ArchivePublishAttemptStatus::Failed,
                                                             QStringLiteral("injected publication failure")};
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::DestinationPublicationFailure);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(readBytes(destination), originalDestination);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, PublicationExceptionPreservesDestinationAndCleansCandidate)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray originalDestination("old-trusted-archive");
    writeBytes(source, QByteArray(7001, 'x'));
    writeBytes(destination, originalDestination);

    ArchiveTransportOperations operations;
    operations.publish =
        [](const ImageIntegration::ArchivePublishRequest&) -> ImageIntegration::ArchivePublishAttemptResult {
        throw std::runtime_error("injected publish exception");
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::DestinationPublicationFailure);
    EXPECT_EQ(readBytes(destination), originalDestination);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, StaleCommittedSourceStopsBeforeTransfer)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    writeBytes(source, QByteArray(4097, 's'));
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());
    writeBytes(source, QByteArray(4098, 's'));

    int cloneAttempts = 0;
    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&cloneAttempts](const ImageIntegration::ArchiveCloneRequest&) {
        ++cloneAttempts;
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Succeeded, {}};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Succeeded, {}};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedBeforeTransfer);
    EXPECT_EQ(cloneAttempts, 0);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, SameSizeSameTimestampReplacementNeverPublishesUnderCommittedDigest)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray original(4096, 'a');
    const QByteArray replacement(4096, 'b');
    writeBytes(source, original);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ASSERT_TRUE(waitForTheChangeClockToPass(source));
    writeBytes(source, replacement);
    QFile sourceFile(source);
    ASSERT_TRUE(sourceFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(sourceFile.setFileTime(committed_artifact->modificationTimestamp(), QFileDevice::FileModificationTime));
    sourceFile.close();
    ASSERT_FALSE(committed_artifact->matchesFileSnapshot(source));

    const ArchiveFileTransport transport = ArchiveFileTransport::ordinaryCopyOnly();
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedBeforeTransfer);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, FileBackedArtifactRejectsOrPinsPathReplacementDuringDigest)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString displaced = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source-original.bin"));
    const QString replacement = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source-replacement.bin"));
    constexpr int kPayloadBytes = 3 * 64 * 1024;
    writeBytes(source, QByteArray(kPayloadBytes, 'a'));
    writeBytes(replacement, QByteArray(kPayloadBytes, 'b'));

    const QDateTime committedTimestamp = QFileInfo(source).lastModified();
    QFile replacementFile(replacement);
    ASSERT_TRUE(replacementFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacementFile.setFileTime(committedTimestamp, QFileDevice::FileModificationTime));
    replacementFile.close();

    int cancellationChecks = 0;
    OpenPathReplacementAttempt replacementAttempt;
    const auto artifact = FinalCommittedArtifact::fromFile(source, nullptr, [&]() {
        ++cancellationChecks;
        if (cancellationChecks == 3)
        {
            replacementAttempt = replacePathWhileOpen(source, displaced, replacement);
        }
        return false;
    });

    ASSERT_TRUE(replacementAttempt.replaced || replacementAttempt.deniedByOpenHandle)
        << "replacement error " << replacementAttempt.errorCode;
    if (replacementAttempt.replaced)
    {
        EXPECT_FALSE(artifact.has_value());
    }
    else
    {
        ASSERT_TRUE(artifact.has_value());
        EXPECT_EQ(artifact->digest().sha256(),
                  QString::fromLatin1(
                      QCryptographicHash::hash(QByteArray(kPayloadBytes, 'a'), QCryptographicHash::Sha256).toHex()));
    }
}

TEST(P117ArchiveTransport, GenericStrictDigestRejectsOrPinsPathReplacementDuringRead)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString displaced = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source-original.bin"));
    const QString replacement = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source-replacement.bin"));
    constexpr int kPayloadBytes = 3 * 64 * 1024;
    const QByteArray original(kPayloadBytes, 'a');
    writeBytes(source, original);
    writeBytes(replacement, QByteArray(kPayloadBytes, 'b'));

    const QDateTime committedTimestamp = QFileInfo(source).lastModified();
    QFile replacementFile(replacement);
    ASSERT_TRUE(replacementFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacementFile.setFileTime(committedTimestamp, QFileDevice::FileModificationTime));
    replacementFile.close();

    WorkflowExecutionCounterAccumulator counters;
    int cancellationChecks = 0;
    OpenPathReplacementAttempt replacementAttempt;
    const QString digest = ArtifactTracker::contentDigestForFile(source, &counters, [&]() {
        ++cancellationChecks;
        if (cancellationChecks == 3)
        {
            replacementAttempt = replacePathWhileOpen(source, displaced, replacement);
        }
        return false;
    });

    ASSERT_TRUE(replacementAttempt.replaced || replacementAttempt.deniedByOpenHandle)
        << "replacement error " << replacementAttempt.errorCode;
    if (replacementAttempt.replaced)
    {
        EXPECT_TRUE(digest.isEmpty());
    }
    else
    {
        EXPECT_EQ(digest, QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex()));
    }
    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(kPayloadBytes));
}

TEST(P117ArchiveTransport, RemovesOldCrashOrphanBeforeStartingNewTransfer)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QString orphan =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral(".archive.bin.afternight-archive-crash.tmp"));
    writeBytes(source, QByteArray("current"));
    writeBytes(orphan, QByteArray("orphan"));
    QFile orphanFile(orphan);
    ASSERT_TRUE(orphanFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(orphanFile.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime));
    orphanFile.close();

    const ArchiveFileTransport transport = ArchiveFileTransport::ordinaryCopyOnly();
    const auto result = transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination));

    ASSERT_TRUE(result.succeeded());
    EXPECT_FALSE(QFileInfo::exists(orphan));
}

TEST(P117ArchiveTransport, CommittedSourceChangedDuringTransferIsNeverRetried)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    writeBytes(source, QByteArray(8193, 'b'));
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&source](const ImageIntegration::ArchiveCloneRequest& request) {
        if (!QFile::copy(source, request.temporaryDestinationPath()))
        {
            return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Failed, QStringLiteral("copy failed")};
        }
        QFile changed(source);
        EXPECT_TRUE(changed.open(QIODevice::Append));
        EXPECT_EQ(changed.write("changed"), 7);
        changed.close();
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Succeeded, {}};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Succeeded, {}};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedDuringTransfer);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, CommittedSourceChangedDuringOrdinaryCopyIsNotPublished)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(12289, 'o');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.copy = [&source](const ImageIntegration::ArchiveCopyRequest& request) {
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        QFile changed(source);
        EXPECT_TRUE(changed.open(QIODevice::Append));
        EXPECT_EQ(changed.write("changed"), 7);
        changed.close();
        return testCopyResult(request, copied);
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedDuringTransfer);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, BackendExceptionsUseFallbackAndCleanEveryCandidate)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(9217, 'e');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest& request) -> ArchiveCloneAttemptResult {
        writeBytes(request.temporaryDestinationPath(), QByteArray("partial-before-exception"));
        throw std::runtime_error("injected clone exception");
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneFailedCopySucceeded);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 1u);
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, NonImmutableSourceNeverAttemptsClone)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("mutable.sqlite"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("snapshot.sqlite"));
    const QByteArray payload("mutable-snapshot");
    writeBytes(source, payload);

    int cloneAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&cloneAttempts](const ImageIntegration::ArchiveCloneRequest&) {
        ++cloneAttempts;
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Succeeded, {}};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded);
    EXPECT_EQ(cloneAttempts, 0);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::NonImmutableSource);
    EXPECT_EQ(readBytes(destination), payload);
}

TEST(P117ArchiveTransport, MutableSourceUsesOrdinaryCopyWithoutImmutablePostValidation)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("mutable.sqlite"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("snapshot.sqlite"));
    const QByteArray payload("mutable-snapshot-before-change");
    writeBytes(source, payload);

    ArchiveTransportOperations operations;
    operations.copy = [&source](const ImageIntegration::ArchiveCopyRequest& request) {
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        QFile changed(source);
        EXPECT_TRUE(changed.open(QIODevice::Append));
        EXPECT_GT(changed.write("changed"), 0);
        changed.close();
        return testCopyResult(request, copied);
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::NonImmutableSource);
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P117ArchiveTransport, CloneProducesIndependentArchiveIdentity)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(16397, 'i');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = copyAsSuccessfulClone;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));
    ASSERT_TRUE(result.succeeded());

    QFile changedSource(source);
    ASSERT_TRUE(changedSource.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(changedSource.write("changed-source"), 14);
    changedSource.close();
    EXPECT_EQ(readBytes(destination), payload);

    writeBytes(destination, QByteArray("changed-archive"));
    EXPECT_EQ(readBytes(source), QByteArray("changed-source"));
    ASSERT_TRUE(QFile::remove(source));
    EXPECT_TRUE(QFileInfo::exists(destination));
}

#ifdef Q_OS_WIN
TEST(P117ArchiveTransport, HardLinkCloneCandidateChangesCommittedIdentityAndIsNotPublished)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(16397, 'h');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());
    ASSERT_TRUE(waitForTheChangeClockToPass(source));

    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest& request) {
        std::error_code error;
        std::filesystem::create_hard_link(std::filesystem::path(request.immutableSource().path().toStdWString()),
                                          std::filesystem::path(request.temporaryDestinationPath().toStdWString()),
                                          error);
        const bool linked = !error;
        return ArchiveCloneAttemptResult{linked ? ArchiveCloneAttemptStatus::Succeeded
                                                : ArchiveCloneAttemptStatus::Failed,
                                         linked ? QString{} : QStringLiteral("hard-link fixture failed")};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Failed, QStringLiteral("copy must not run")};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    // Creating the hard link mutates NTFS ChangeTime, which is part of the
    // committed revision identity, once the clock has passed the commit's.
    // Source-change handling therefore takes precedence over fallback and must
    // not publish bytes under the old digest.
    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedDuringTransfer);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::None);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 0u);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_EQ(readBytes(source), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}
#endif

TEST(P117WorkflowArchiver, CloneTransportPersistsDigestAndCountsArchiveOnce)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QByteArray payload(12347, 'a');
    WorkflowExecutionCounterAccumulator counters;
    WorkflowManifest manifest = createP117Manifest(temporaryDirectory.path(), QStringLiteral("P117_CLONE_ARCHIVE"));
    WorkflowExecutor::Result workflowResult = createP117Result(temporaryDirectory.path(), payload, &counters);
    ASSERT_TRUE(workflowResult.integrationGroupResults.front().committedArtifact.has_value());
    const QString digest = workflowResult.integrationGroupResults.front().committedArtifact->digest().sha256();
    const std::uint64_t bytesHashedBeforeArchive = counters.snapshot().bytesHashed();

    ArchiveTransportOperations operations;
    operations.clone = copyAsSuccessfulClone;
    WorkflowArchiver archiver(temporaryDirectory.path(), &counters, ArchiveFileTransport(std::move(operations)));
    const auto result = archiver.commit(manifest, workflowResult);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.itemsArchived, 1);
    EXPECT_EQ(result.transportDiagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.transportDiagnostics.cloneSuccesses, 1u);
    EXPECT_EQ(result.transportDiagnostics.logicalBytesCloned, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(result.transportDiagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(counters.snapshot().bytesHashed(), bytesHashedBeforeArchive);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(workflowResult.integrationGroupResults.front().outputRetentionStatus,
              OutputRetentionStatus::RetainedInHistory);

    QFile manifestFile(result.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonArray archivedFiles = root.value(QStringLiteral("integrationGroupResults"))
                                         .toArray()
                                         .first()
                                         .toObject()
                                         .value(QStringLiteral("archivedFiles"))
                                         .toArray();
    ASSERT_EQ(archivedFiles.size(), 1);
    EXPECT_EQ(archivedFiles.first().toObject().value(QStringLiteral("sha256")).toString(), digest);
    EXPECT_EQ(archivedFiles.first().toObject().value(QStringLiteral("digestProvenance")).toString(),
              QString::fromLatin1(ImageIntegration::schema::kArtifactDigestProvenanceCurrentRunCommitted));
    EXPECT_EQ(readBytes(QDir(result.archivePath).filePath(QStringLiteral("groups/P117Group/p117_master.bin"))),
              payload);
}

TEST(P117WorkflowArchiver, RetentionDeclinePerformsNoPayloadTransport)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QByteArray payload(4097, 'q');
    WorkflowExecutionCounterAccumulator counters;
    WorkflowManifest manifest = createP117Manifest(temporaryDirectory.path(), QStringLiteral("P117_DECLINED"));
    WorkflowExecutor::Result workflowResult = createP117Result(temporaryDirectory.path(), payload, &counters);

    int cloneAttempts = 0;
    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&cloneAttempts](const ImageIntegration::ArchiveCloneRequest&) {
        ++cloneAttempts;
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Succeeded, {}};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Succeeded, {}};
    };
    WorkflowArchiver archiver(temporaryDirectory.path(), 10, 128, &counters,
                              ArchiveFileTransport(std::move(operations)));
    const auto result = archiver.commit(manifest, workflowResult);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.itemsArchived, 0);
    EXPECT_EQ(cloneAttempts, 0);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_EQ(result.transportDiagnostics.cloneAttempts, 0u);
    EXPECT_EQ(result.transportDiagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(workflowResult.integrationGroupResults.front().outputRetentionStatus,
              OutputRetentionStatus::NotRetainedByPolicy);
}

TEST(P117WorkflowArchiver, UnsupportedCloneFallbackCountsRetainedPayloadExactlyOnce)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QByteArray payload(10301, 'l');
    WorkflowExecutionCounterAccumulator counters;
    WorkflowManifest manifest = createP117Manifest(temporaryDirectory.path(), QStringLiteral("P117_FALLBACK"));
    WorkflowExecutor::Result workflowResult = createP117Result(temporaryDirectory.path(), payload, &counters);

    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest&) {
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Unsupported,
                                         QStringLiteral("injected unsupported filesystem")};
    };
    WorkflowArchiver archiver(temporaryDirectory.path(), &counters, ArchiveFileTransport(std::move(operations)));
    const auto result = archiver.commit(manifest, workflowResult);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.itemsArchived, 1);
    EXPECT_EQ(result.transportDiagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.transportDiagnostics.cloneSuccesses, 0u);
    EXPECT_EQ(result.transportDiagnostics.unsupportedFallbacks, 1u);
    EXPECT_EQ(result.transportDiagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(workflowResult.integrationGroupResults.front().outputRetentionStatus,
              OutputRetentionStatus::RetainedInHistory);
}

TEST(P117WorkflowArchiver, ChangedDuringCloneKeepsScientificSuccessAndPublishesNoDigest)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QByteArray payload(8193, 't');
    WorkflowExecutionCounterAccumulator counters;
    WorkflowManifest manifest = createP117Manifest(temporaryDirectory.path(), QStringLiteral("P117_CHANGED"));
    WorkflowExecutor::Result workflowResult = createP117Result(temporaryDirectory.path(), payload, &counters);
    const QString source = workflowResult.integrationGroupResults.front().outputPath;

    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&source](const ImageIntegration::ArchiveCloneRequest& request) {
        if (!QFile::copy(source, request.temporaryDestinationPath()))
        {
            return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Failed, QStringLiteral("copy failed")};
        }
        QFile changed(source);
        EXPECT_TRUE(changed.open(QIODevice::Append));
        EXPECT_EQ(changed.write("changed"), 7);
        changed.close();
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Succeeded, {}};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Succeeded, {}};
    };
    WorkflowArchiver archiver(temporaryDirectory.path(), &counters, ArchiveFileTransport(std::move(operations)));
    const auto result = archiver.commit(manifest, workflowResult);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(workflowResult.success);
    EXPECT_EQ(result.itemsArchived, 0);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_EQ(workflowResult.integrationGroupResults.front().outputRetentionStatus,
              OutputRetentionStatus::NotRetainedByArchiveFailure);
    EXPECT_TRUE(workflowResult.integrationGroupResults.front().outputRetentionReason.contains(QStringLiteral("changed"),
                                                                                              Qt::CaseInsensitive));

    QFile manifestFile(result.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonArray archivedFiles = root.value(QStringLiteral("integrationGroupResults"))
                                         .toArray()
                                         .first()
                                         .toObject()
                                         .value(QStringLiteral("archivedFiles"))
                                         .toArray();
    EXPECT_TRUE(archivedFiles.isEmpty());
    EXPECT_FALSE(
        QFileInfo::exists(QDir(result.archivePath).filePath(QStringLiteral("groups/P117Group/p117_master.bin"))));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

#ifdef Q_OS_LINUX
TEST(P118LinuxArchiveTransport, NativeReflinkOrVerifiedFallbackProducesIndependentArchive)
{
    QTemporaryDir temporaryDirectory(QDir::current().filePath(QStringLiteral(".p118-linux-reflink-capability-XXXXXX")));
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    QByteArray payload(256 * 1024 + 37, Qt::Uninitialized);
    for (qsizetype index = 0; index < payload.size(); ++index)
    {
        payload[index] = static_cast<char>((index * 31) & 0xff);
    }
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport;
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    ::testing::Test::RecordProperty("realLinuxReflinkSupported",
                                    result.outcome == ArchiveFileTransportOutcome::LinuxReflinkSucceeded ? "true"
                                                                                                         : "false");
    ::testing::Test::RecordProperty("selectedTransport",
                                    result.diagnostics.selectedTransport == ArchiveSelectedTransport::LinuxReflink
                                        ? "linux_reflink"
                                        : "ordinary_copy_fallback");
    ::testing::Test::RecordProperty("fallbackReason",
                                    std::to_string(static_cast<int>(result.diagnostics.fallbackReason)));
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(QFileInfo(destination).size(), payload.size());
    EXPECT_EQ(readBytes(destination), payload);

    std::error_code identityError;
    EXPECT_FALSE(std::filesystem::equivalent(std::filesystem::path(source.toStdString()),
                                             std::filesystem::path(destination.toStdString()), identityError));
    EXPECT_FALSE(identityError);

    if (result.outcome == ArchiveFileTransportOutcome::LinuxReflinkSucceeded)
    {
        EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::LinuxReflink);
        EXPECT_EQ(result.diagnostics.cloneSuccesses, 1u);
        EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 0u);
        EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 0u);
        EXPECT_EQ(result.diagnostics.logicalBytesCloned, static_cast<std::uint64_t>(payload.size()));
        EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, 0u);
        EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    }
    else
    {
        EXPECT_TRUE(result.outcome == ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded ||
                    result.outcome == ArchiveFileTransportOutcome::CloneFailedCopySucceeded);
        EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
        EXPECT_NE(result.diagnostics.fallbackReason, ArchiveFallbackReason::None);
        EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
        EXPECT_EQ(result.diagnostics.unsupportedFallbacks + result.diagnostics.failedAttemptFallbacks, 1u);
        EXPECT_EQ(result.diagnostics.logicalBytesCloned, 0u);
        EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
        EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    }

    writeBytes(source, QByteArray("changed-source"));
    EXPECT_EQ(readBytes(destination), payload);
    writeBytes(destination, QByteArray("changed-archive"));
    EXPECT_EQ(readBytes(source), QByteArray("changed-source"));
    ASSERT_TRUE(QFile::remove(source));
    EXPECT_TRUE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118ArchiveTransport, InjectedLinuxReflinkSuccessReportsExactDiagnostics)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(12347, 'r');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    ArchiveTransportOperations operations;
    operations.clone = [&payload](const ImageIntegration::ArchiveCloneRequest& request) {
        const bool copied = QFile::copy(request.immutableSource().path(), request.temporaryDestinationPath());
        return ArchiveCloneAttemptResult{
            copied ? ArchiveCloneAttemptStatus::Succeeded : ArchiveCloneAttemptStatus::Failed,
            copied ? QString{} : QStringLiteral("injected reflink failed"),
            copied ? static_cast<std::uint64_t>(payload.size()) : 0u, 0u, ArchiveSelectedTransport::LinuxReflink};
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::LinuxReflinkSucceeded);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::LinuxReflink);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::None);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 1u);
    EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 0u);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 0u);
    EXPECT_EQ(result.diagnostics.logicalBytesCloned, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(QFileInfo(destination).size(), payload.size());
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118ArchiveTransport, LinuxFallbackClassificationsCopyExactlyOnce)
{
    const std::array cases{
        std::pair{ArchiveCloneAttemptStatus::Unsupported, ArchiveFallbackReason::CloneUnsupported},
        std::pair{ArchiveCloneAttemptStatus::CrossVolume, ArchiveFallbackReason::CrossVolume},
        std::pair{ArchiveCloneAttemptStatus::PermissionDenied, ArchiveFallbackReason::PermissionDenied},
        std::pair{ArchiveCloneAttemptStatus::UnsupportedExtentAlignment,
                  ArchiveFallbackReason::UnsupportedExtentAlignment},
        std::pair{ArchiveCloneAttemptStatus::CapabilityFailure, ArchiveFallbackReason::PlatformCapabilityFailure}};

    for (const auto& [cloneStatus, expectedReason] : cases)
    {
        QTemporaryDir temporaryDirectory;
        ASSERT_TRUE(temporaryDirectory.isValid());
        const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
        const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
        const QByteArray payload(6151, static_cast<char>('a' + static_cast<int>(cloneStatus)));
        writeBytes(source, payload);
        auto committed_artifact = FinalCommittedArtifact::fromFile(source);
        ASSERT_TRUE(committed_artifact.has_value());

        ArchiveTransportOperations operations;
        operations.clone = [cloneStatus](const ImageIntegration::ArchiveCloneRequest&) {
            return ArchiveCloneAttemptResult{cloneStatus, QStringLiteral("injected Linux reflink failure"), 0u, 0u,
                                             ArchiveSelectedTransport::LinuxReflink};
        };
        WorkflowExecutionCounterAccumulator counters;
        const ArchiveFileTransport transport(std::move(operations));
        const auto result = transport.transport(
            ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

        EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded);
        EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
        EXPECT_EQ(result.diagnostics.fallbackReason, expectedReason);
        EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
        EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
        EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 1u);
        EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 0u);
        EXPECT_EQ(result.diagnostics.logicalBytesCloned, 0u);
        EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
        EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
        EXPECT_EQ(readBytes(destination), payload);
        EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
    }
}

TEST(P118ArchiveTransport, LinuxReflinkRequiresImmutableAuthority)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("mutable.sqlite"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("snapshot.sqlite"));
    const QByteArray payload("mutable-snapshot");
    writeBytes(source, payload);

    int cloneAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&cloneAttempts](const ImageIntegration::ArchiveCloneRequest&) {
        ++cloneAttempts;
        return ArchiveCloneAttemptResult{
            ArchiveCloneAttemptStatus::Succeeded, {}, 0u, 0u, ArchiveSelectedTransport::LinuxReflink};
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forOrdinaryCopy(source, destination, &counters));

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::NonImmutableSource);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 0u);
    EXPECT_EQ(cloneAttempts, 0);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(readBytes(destination), payload);
}

TEST(P118ArchiveTransport, FailedLinuxCandidateIsRemovedBeforeAtomicCopyFallback)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(8209, 'p');
    const QByteArray previousDestination("previous-archive");
    writeBytes(source, payload);
    writeBytes(destination, previousDestination);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    QString failedCandidate;
    ArchiveTransportOperations operations;
    operations.clone = [&failedCandidate](const ImageIntegration::ArchiveCloneRequest& request) {
        failedCandidate = request.temporaryDestinationPath();
        writeBytes(failedCandidate, QByteArray("partial-reflink-candidate"));
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Failed, QStringLiteral("partial reflink"), 0u, 0u,
                                         ArchiveSelectedTransport::LinuxReflink};
    };
    operations.copy = [&failedCandidate, &destination,
                       &previousDestination](const ImageIntegration::ArchiveCopyRequest& request) {
        EXPECT_FALSE(QFileInfo::exists(failedCandidate));
        EXPECT_NE(request.temporaryDestinationPath(), failedCandidate);
        EXPECT_EQ(readBytes(destination), previousDestination);
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        return testCopyResult(request, copied, QStringLiteral("copy fallback failed"));
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::CloneFailedCopySucceeded);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::CloneAttemptFailed);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
    EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 0u);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 1u);
    EXPECT_EQ(result.diagnostics.logicalBytesCloned, 0u);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(readBytes(destination), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118ArchiveTransport, SourceChangeDuringLinuxReflinkNeverFallsBackOrPublishes)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(12289, 's');
    const QByteArray previousDestination("previous-archive");
    writeBytes(source, payload);
    writeBytes(destination, previousDestination);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&source, &payload](const ImageIntegration::ArchiveCloneRequest& request) {
        const bool copied = QFile::copy(source, request.temporaryDestinationPath());
        QFile changed(source);
        EXPECT_TRUE(changed.open(QIODevice::Append));
        EXPECT_EQ(changed.write("changed"), 7);
        changed.close();
        return ArchiveCloneAttemptResult{
            copied ? ArchiveCloneAttemptStatus::Succeeded : ArchiveCloneAttemptStatus::Failed,
            copied ? QString{} : QStringLiteral("injected reflink failed"),
            copied ? static_cast<std::uint64_t>(payload.size()) : 0u, 0u, ArchiveSelectedTransport::LinuxReflink};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Succeeded, {}};
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedDuringTransfer);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
    EXPECT_EQ(result.diagnostics.logicalBytesCloned, 0u);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_EQ(readBytes(destination), previousDestination);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118ArchiveTransport, SourceChangeDuringLinuxCopyFallbackIsNeverPublished)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(13313, 'f');
    const QByteArray previousDestination("previous-archive");
    writeBytes(source, payload);
    writeBytes(destination, previousDestination);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest&) {
        return ArchiveCloneAttemptResult{ArchiveCloneAttemptStatus::Unsupported,
                                         QStringLiteral("injected unsupported filesystem"), 0u, 0u,
                                         ArchiveSelectedTransport::LinuxReflink};
    };
    operations.copy = [&source, &copyAttempts](const ImageIntegration::ArchiveCopyRequest& request) {
        ++copyAttempts;
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        QFile changed(source);
        EXPECT_TRUE(changed.open(QIODevice::Append));
        EXPECT_EQ(changed.write("changed"), 7);
        changed.close();
        return testCopyResult(request, copied, QStringLiteral("copy fallback failed"));
    };
    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport(std::move(operations));
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedDuringTransfer);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::None);
    EXPECT_EQ(result.diagnostics.fallbackReason, ArchiveFallbackReason::CloneUnsupported);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
    EXPECT_EQ(result.diagnostics.unsupportedFallbacks, 1u);
    EXPECT_EQ(result.diagnostics.logicalBytesCloned, 0u);
    EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(copyAttempts, 1);
    EXPECT_EQ(readBytes(destination), previousDestination);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118ArchiveTransport, HardLinkCandidateChangesCommittedIdentityAndIsNotPublished)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    const QByteArray payload(16397, 'h');
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());
    ASSERT_TRUE(waitForTheChangeClockToPass(source));

    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [](const ImageIntegration::ArchiveCloneRequest& request) {
        std::error_code error;
        std::filesystem::create_hard_link(std::filesystem::path(request.immutableSource().path().toStdString()),
                                          std::filesystem::path(request.temporaryDestinationPath().toStdString()),
                                          error);
        const bool linked = !error;
        return ArchiveCloneAttemptResult{linked ? ArchiveCloneAttemptStatus::Succeeded
                                                : ArchiveCloneAttemptStatus::Failed,
                                         linked ? QString{} : QStringLiteral("hard-link fixture failed"), 0u, 0u,
                                         ArchiveSelectedTransport::LinuxReflink};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest&) {
        ++copyAttempts;
        return ArchiveCopyAttemptResult{ArchiveCopyAttemptStatus::Failed, QStringLiteral("copy must not run")};
    };
    const ArchiveFileTransport transport(std::move(operations));
    const auto result =
        transport.transport(ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination));

    // Creating the hard link mutates POSIX ctime, which is part of the
    // committed revision identity, once the clock has passed the commit's.
    // Source-change handling therefore takes precedence over fallback and must
    // not publish bytes under the old digest.
    EXPECT_EQ(result.outcome, ArchiveFileTransportOutcome::SourceChangedDuringTransfer);
    EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::None);
    EXPECT_EQ(result.diagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.diagnostics.cloneSuccesses, 0u);
    EXPECT_EQ(result.diagnostics.failedAttemptFallbacks, 0u);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_FALSE(QFileInfo::exists(destination));
    EXPECT_EQ(readBytes(source), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118WorkflowArchiver, LinuxReflinkDiagnosticsCountWorkflowPayloadOnce)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QByteArray payload(17303, 'w');
    WorkflowExecutionCounterAccumulator counters;
    WorkflowManifest manifest = createP117Manifest(temporaryDirectory.path(), QStringLiteral("P118_REFLINK_ARCHIVE"));
    WorkflowExecutor::Result workflowResult = createP117Result(temporaryDirectory.path(), payload, &counters);

    ArchiveTransportOperations operations;
    operations.clone = [&payload](const ImageIntegration::ArchiveCloneRequest& request) {
        const bool copied = QFile::copy(request.immutableSource().path(), request.temporaryDestinationPath());
        return ArchiveCloneAttemptResult{
            copied ? ArchiveCloneAttemptStatus::Succeeded : ArchiveCloneAttemptStatus::Failed,
            copied ? QString{} : QStringLiteral("injected reflink failed"),
            copied ? static_cast<std::uint64_t>(payload.size()) : 0u, 0u, ArchiveSelectedTransport::LinuxReflink};
    };
    const std::uint64_t bytesHashedBeforeArchive = counters.snapshot().bytesHashed();
    WorkflowArchiver archiver(temporaryDirectory.path(), &counters, ArchiveFileTransport(std::move(operations)));
    const auto result = archiver.commit(manifest, workflowResult);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.payloadTransports.size(), 1u);
    EXPECT_EQ(result.itemsArchived, 1);
    EXPECT_EQ(result.payloadTransports.front().outcome, ArchiveFileTransportOutcome::LinuxReflinkSucceeded);
    EXPECT_EQ(result.transportDiagnostics.selectedTransport, ArchiveSelectedTransport::LinuxReflink);
    EXPECT_EQ(result.transportDiagnostics.fallbackReason, ArchiveFallbackReason::None);
    EXPECT_EQ(result.transportDiagnostics.cloneAttempts, 1u);
    EXPECT_EQ(result.transportDiagnostics.cloneSuccesses, 1u);
    EXPECT_EQ(result.transportDiagnostics.unsupportedFallbacks, 0u);
    EXPECT_EQ(result.transportDiagnostics.failedAttemptFallbacks, 0u);
    EXPECT_EQ(result.transportDiagnostics.logicalBytesCloned, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(result.transportDiagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(counters.snapshot().bytesHashed(), bytesHashedBeforeArchive);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(workflowResult.integrationGroupResults.front().outputRetentionStatus,
              OutputRetentionStatus::RetainedInHistory);
    EXPECT_EQ(readBytes(QDir(result.archivePath).filePath(QStringLiteral("groups/P117Group/p117_master.bin"))),
              payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}

TEST(P118WorkflowArchiver, RegistrationMetricsSnapshotUsesValidatedSqliteCopyWithoutCloneBackend)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString payload = QStringLiteral("mutable-registration-metrics");
    const QString metricsPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("registration_metrics.db"));
    core::SqliteConnection live(metricsPath, QStringLiteral("P118MutableRegistrationMetrics"));
    QString error;
    ASSERT_TRUE(live.open(&error)) << error.toStdString();
    QSqlQuery query(live.db());
    ASSERT_TRUE(query.exec(QStringLiteral("CREATE TABLE snapshot_rows (value TEXT NOT NULL)")))
        << query.lastError().text().toStdString();
    query.prepare(QStringLiteral("INSERT INTO snapshot_rows (value) VALUES (:value)"));
    query.bindValue(QStringLiteral(":value"), payload);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();

    int cloneAttempts = 0;
    int copyAttempts = 0;
    ArchiveTransportOperations operations;
    operations.clone = [&cloneAttempts](const ImageIntegration::ArchiveCloneRequest&) {
        ++cloneAttempts;
        return ArchiveCloneAttemptResult{
            ArchiveCloneAttemptStatus::Succeeded, {}, 0u, 0u, ArchiveSelectedTransport::LinuxReflink};
    };
    operations.copy = [&copyAttempts](const ImageIntegration::ArchiveCopyRequest& request) {
        ++copyAttempts;
        const bool copied = QFile::copy(request.sourcePath(), request.temporaryDestinationPath());
        return testCopyResult(request, copied, QStringLiteral("metrics copy failed"));
    };
    WorkflowExecutionCounterAccumulator counters;
    WorkflowManifest manifest = createP117Manifest(temporaryDirectory.path(), QStringLiteral("P118_METRICS"));
    WorkflowExecutor::Result workflowResult;
    workflowResult.success = true;
    WorkflowArchiver archiver(temporaryDirectory.path(), &counters, ArchiveFileTransport(std::move(operations)));
    const auto result = archiver.commit(manifest, workflowResult);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.payloadTransports.size(), 1u);
    EXPECT_EQ(cloneAttempts, 0);
    EXPECT_EQ(copyAttempts, 0);
    EXPECT_EQ(result.payloadTransports.front().outcome, ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded);
    EXPECT_EQ(result.transportDiagnostics.cloneAttempts, 0u);
    EXPECT_EQ(result.transportDiagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
    EXPECT_EQ(result.transportDiagnostics.fallbackReason, ArchiveFallbackReason::NonImmutableSource);
    const QString archivedPath = QDir(result.archivePath).filePath(QStringLiteral("registration_metrics.db"));
    const std::uint64_t snapshotBytes = static_cast<std::uint64_t>(QFileInfo(archivedPath).size());
    EXPECT_GT(snapshotBytes, 0u);
    EXPECT_EQ(result.transportDiagnostics.ordinaryCopyBytes, snapshotBytes);
    EXPECT_EQ(counters.snapshot().bytesCopied(), snapshotBytes);

    core::SqliteConnection archived(archivedPath, QStringLiteral("P118ArchivedRegistrationMetrics"));
    ASSERT_TRUE(archived.openReadOnly(&error)) << error.toStdString();
    QSqlQuery archivedQuery(archived.db());
    ASSERT_TRUE(archivedQuery.exec(QStringLiteral("SELECT value FROM snapshot_rows")))
        << archivedQuery.lastError().text().toStdString();
    ASSERT_TRUE(archivedQuery.next());
    EXPECT_EQ(archivedQuery.value(0).toString(), payload);
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}
#endif

#ifdef Q_OS_WIN
TEST(P117WindowsArchiveTransport, RealCapabilityOrFallbackProducesIndependentArchive)
{
    QTemporaryDir temporaryDirectory;
    ASSERT_TRUE(temporaryDirectory.isValid());
    const QString source = QDir(temporaryDirectory.path()).filePath(QStringLiteral("source.bin"));
    const QString destination = QDir(temporaryDirectory.path()).filePath(QStringLiteral("archive.bin"));
    QByteArray payload(256 * 1024 + 37, Qt::Uninitialized);
    for (qsizetype index = 0; index < payload.size(); ++index)
    {
        payload[index] = static_cast<char>((index * 31) & 0xff);
    }
    writeBytes(source, payload);
    auto committed_artifact = FinalCommittedArtifact::fromFile(source);
    ASSERT_TRUE(committed_artifact.has_value());

    WorkflowExecutionCounterAccumulator counters;
    const ArchiveFileTransport transport;
    const auto result = transport.transport(
        ArchiveFileTransportRequest::forImmutableArtifact(*committed_artifact, destination, &counters));

    ::testing::Test::RecordProperty(
        "realBlockCloneSupported",
        result.outcome == ArchiveFileTransportOutcome::WindowsBlockCloneSucceeded ? "true" : "false");
    ::testing::Test::RecordProperty("selectedTransport",
                                    result.diagnostics.selectedTransport == ArchiveSelectedTransport::WindowsBlockClone
                                        ? "windows_block_clone"
                                        : "ordinary_copy_fallback");
    ::testing::Test::RecordProperty("fallbackReason",
                                    std::to_string(static_cast<int>(result.diagnostics.fallbackReason)));
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(QFileInfo(destination).size(), payload.size());
    EXPECT_EQ(readBytes(destination), payload);
    if (result.outcome == ArchiveFileTransportOutcome::WindowsBlockCloneSucceeded)
    {
        EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::WindowsBlockClone);
        EXPECT_EQ(result.diagnostics.cloneSuccesses, 1u);
        EXPECT_EQ(result.diagnostics.logicalBytesCloned + result.diagnostics.ordinaryCopyBytes,
                  static_cast<std::uint64_t>(payload.size()));
        EXPECT_EQ(counters.snapshot().bytesCopied(), result.diagnostics.ordinaryCopyBytes);
    }
    else
    {
        EXPECT_TRUE(result.outcome == ArchiveFileTransportOutcome::CloneUnsupportedCopySucceeded ||
                    result.outcome == ArchiveFileTransportOutcome::CloneFailedCopySucceeded);
        EXPECT_EQ(result.diagnostics.selectedTransport, ArchiveSelectedTransport::OrdinaryCopy);
        EXPECT_EQ(result.diagnostics.ordinaryCopyBytes, static_cast<std::uint64_t>(payload.size()));
        EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(payload.size()));
    }

    writeBytes(source, QByteArray("changed-source"));
    EXPECT_EQ(readBytes(destination), payload);
    writeBytes(destination, QByteArray("changed-archive"));
    EXPECT_EQ(readBytes(source), QByteArray("changed-source"));
    ASSERT_TRUE(QFile::remove(source));
    EXPECT_TRUE(QFileInfo::exists(destination));
    EXPECT_TRUE(archiveTransportTemporaries(temporaryDirectory.path()).isEmpty());
}
#endif

} // namespace
