// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A file read as a frame of the integration model through the engine's probe.

#include "stdafx.hpp"
#include "FileImport.h"

#include "processes/ImageIntegration/core/FrameDetection.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <AfterNightPlatform/Logging.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace ImageIntegration
{
namespace
{
[[nodiscard]] bool containsMasterToken(const QString& value)
{
    const QString lower = value.toLower();
    return lower.contains(QStringLiteral("master"));
}

/// A header's text with the space around it removed, and nothing for the sentinels capture programs write for a
/// value they do not have.
[[nodiscard]] QString metadataText(const std::string& value)
{
    const QString text = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).trimmed();
    const QString upper = text.toUpper();
    if (upper == QStringLiteral("-") || upper == QStringLiteral("N/A"))
    {
        return {};
    }
    return text;
}

/// The decoder's typed text, or the card's where the decoder stated none.
[[nodiscard]] QString eitherText(const std::string& typed, const std::string& carded)
{
    const QString text = metadataText(typed);
    return text.isEmpty() ? metadataText(carded) : text;
}

/// A setting the model files as an integer: a value that is not negative and fits one.
[[nodiscard]] std::optional<int> settingOf(const std::optional<std::int64_t> value)
{
    if (!value.has_value() || *value < 0 || *value > static_cast<std::int64_t>((std::numeric_limits<int>::max)()))
    {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

[[nodiscard]] QDateTime utcTimeOf(const an::UtcInstant instant)
{
    return QDateTime::fromMSecsSinceEpoch(instant.nanosecondsSinceEpoch / 1000000, QTimeZone(QTimeZone::UTC));
}

[[nodiscard]] FrameMetadata createMetadataFallback(FrameType type, const QFileInfo& fileInfo)
{
    return FrameMetadata(type, QSize(0, 0), 0.0, std::nullopt, std::nullopt, std::nullopt, QString(), QString(),
                         fileInfo.lastModified().toUTC(), QString());
}

[[nodiscard]] QString basenameForIssue(const QString& path)
{
    const QFileInfo fi(path);
    return fi.fileName().isEmpty() ? path : fi.fileName();
}

/// Whether a probe's failure says the session's codecs do not read the file's format at all.
[[nodiscard]] bool isUnreadableFormat(const an::Status& status)
{
    const std::optional<an::CodecFailure> reason = an::codecFailureReason(status);
    return reason.has_value() &&
           (*reason == an::CodecFailure::UnknownFormat || *reason == an::CodecFailure::Unavailable);
}

[[nodiscard]] int clampedDimension(const std::uint32_t value)
{
    return value > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ? 0 : static_cast<int>(value);
}

} // namespace

bool isProbablyMasterFilePath(const QString& filePath)
{
    if (filePath.trimmed().isEmpty())
    {
        return false;
    }

    const QFileInfo info(filePath);
    const QString base = info.completeBaseName();
    if (containsMasterToken(base) || containsMasterToken(info.fileName()))
    {
        return true;
    }

    // Also treat a couple of parent directories containing "master" as a strong signal.
    QDir dir = info.dir();
    for (int i = 0; i < 3; ++i)
    {
        const QString name = dir.dirName();
        if (!name.isEmpty() && containsMasterToken(name))
        {
            return true;
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return false;
}

bool ImportResult::hasErrors() const
{
    for (const auto& issue : issues)
    {
        if (issue.severity == ImportIssueSeverity::Error)
        {
            return true;
        }
    }
    return false;
}

QString ImportResult::summaryMessage() const
{
    if (!hasIssues())
    {
        return QString();
    }

    const int warnings = static_cast<int>(std::count_if(
        issues.begin(), issues.end(), [](const ImportIssue& i) { return i.severity == ImportIssueSeverity::Warning; }));
    const int errors = static_cast<int>(issues.size()) - warnings;

    const QString imported = QStringLiteral("Imported %1 file(s)").arg(importedCount);
    const QString skipped = (skippedCount > 0) ? QStringLiteral(", skipped %1").arg(skippedCount) : QString();

    if (errors > 0 && warnings > 0)
    {
        return QStringLiteral("%1%2 with %3 error(s) and %4 warning(s).")
            .arg(imported, skipped)
            .arg(errors)
            .arg(warnings);
    }
    if (errors > 0)
    {
        return QStringLiteral("%1%2 with %3 error(s).").arg(imported, skipped).arg(errors);
    }

    return QStringLiteral("%1%2 with %3 warning(s).").arg(imported, skipped).arg(warnings);
}

QString ImportResult::detailedText(int maxEntries) const
{
    QStringList lines;
    lines.reserve(static_cast<int>((std::min)(issues.size(), static_cast<size_t>(maxEntries))));

    int count = 0;
    for (const auto& issue : issues)
    {
        if (count++ >= maxEntries)
        {
            lines << QStringLiteral("... (%1 more)").arg(static_cast<int>(issues.size()) - maxEntries);
            break;
        }

        const QString sev =
            (issue.severity == ImportIssueSeverity::Error) ? QStringLiteral("ERROR") : QStringLiteral("WARN");
        lines << QStringLiteral("[%1] %2: %3").arg(sev, basenameForIssue(issue.path), issue.message);
    }

    return lines.join(QChar('\n'));
}

void sortDetectedFramesForImport(std::vector<DetectedFrameInfo>& detectedFrames)
{
    auto sortKey = [](const DetectedFrameInfo& detected) {
        const QString canonicalPath = detected.info.id.canonicalPath();
        if (!canonicalPath.isEmpty())
        {
            return canonicalPath;
        }

        const QFileInfo info(detected.info.displayPath);
        return info.absoluteFilePath();
    };

    std::stable_sort(detectedFrames.begin(), detectedFrames.end(),
                     [&](const DetectedFrameInfo& lhs, const DetectedFrameInfo& rhs) {
                         return QString::compare(sortKey(lhs), sortKey(rhs), Qt::CaseInsensitive) < 0;
                     });
}

FrameMetadata frameMetadataFromProbe(const an::ImageMetadata& metadata, const FrameType type, const int width,
                                     const int height, const QFileInfo& fileInfo)
{
    // A camera RAW probe states the typed record and no card; FITS, XISF and TIFF state cards. A figure is read
    // from the record the decoder stated, and from the cards where the record says nothing.
    const an::AcquisitionMetadata& stated = metadata.acquisition();
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());
    const an::AcquisitionMetadataRequest& carded = projected.acquisition;

    const std::optional<double> exposure =
        stated.exposureSeconds().has_value() ? stated.exposureSeconds() : carded.exposureSeconds;
    const double exposureSeconds = exposure.has_value() && *exposure >= 0.0 ? *exposure : 0.0;

    const std::optional<double> temperature = stated.sensorTemperatureCelsius().has_value()
                                                  ? stated.sensorTemperatureCelsius()
                                                  : carded.sensorTemperatureCelsius;

    // The ISO speed a camera reported, or else its gain setting: one figure the calibration match compares.
    std::optional<std::int64_t> isoOrGain = projected.isoSpeed;
    if (!isoOrGain.has_value() && stated.rawCamera().has_value() && stated.rawCamera()->isoSpeed > 0.0)
    {
        isoOrGain = static_cast<std::int64_t>(stated.rawCamera()->isoSpeed);
    }
    if (!isoOrGain.has_value())
    {
        isoOrGain = projected.gainSetting;
    }

    std::optional<an::UtcInstant> captured =
        stated.exposureStart().has_value() ? stated.exposureStart() : carded.exposureStart;
    if (!captured.has_value())
    {
        captured = projected.creationTime;
    }
    const QDateTime capturedAtUtc = captured.has_value() ? utcTimeOf(*captured) : fileInfo.lastModified().toUTC();

    return FrameMetadata(type, QSize(width, height), exposureSeconds, temperature, settingOf(isoOrGain),
                         settingOf(projected.cameraOffset), eitherText(stated.filterName(), carded.filterName),
                         eitherText(stated.objectName(), carded.objectName), capturedAtUtc,
                         eitherText(stated.instrumentName(), carded.instrumentName));
}

an::consumer::OperationRequest importProbeRequest()
{
    return an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u};
}

BuildDetectedFrameInfoResult buildDetectedFrameInfoWithIssue(const an::consumer::EngineSession& session,
                                                             const QString& filePath, const FrameType defaultType,
                                                             const ImportAdmission admission)
{
    BuildDetectedFrameInfoResult out;

    if (filePath.trimmed().isEmpty())
    {
        out.issue = ImportIssue{ImportIssueSeverity::Error, filePath, QStringLiteral("Empty path")};
        return out;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        out.issue = ImportIssue{ImportIssueSeverity::Error, filePath, QStringLiteral("File does not exist")};
        return out;
    }

    const an::consumer::OperationRequest request = importProbeRequest();
    an::Result<an::consumer::OperationScope> scope =
        admission == ImportAdmission::Wait ? session.admit(request) : session.tryAdmit(request);

    FrameTypeDetection detection;
    FrameMetadata metadata = createMetadataFallback(defaultType, fileInfo);
    an::Status failure;
    if (scope.hasValue())
    {
        const std::string path = filePath.toUtf8().toStdString();
        const an::Result<an::CodecImageInfo> probed = an::probeImage(
            scope->engine(), scope->execution(), an::CodecReadRequest{.path = path, .validatePayload = false});
        if (probed.hasValue())
        {
            detection = FrameDetection::detect(filePath, &probed->metadata.codec(), defaultType);
            metadata = frameMetadataFromProbe(probed->metadata, detection.type,
                                              clampedDimension(probed->descriptor.extent().width()),
                                              clampedDimension(probed->descriptor.extent().height()), fileInfo);
        }
        else if (isUnreadableFormat(probed.status()))
        {
            out.issue = ImportIssue{ImportIssueSeverity::Error, filePath, QStringLiteral("Unsupported file format")};
            return out;
        }
        else
        {
            failure = probed.status();
        }
    }
    else
    {
        failure = scope.status();
    }

    if (failure.failed())
    {
        LOG_WARNING("Failed to read metadata for file: {} ({})", filePath.toStdString(), failure.message());
        out.issue = ImportIssue{ImportIssueSeverity::Warning, filePath,
                                QStringLiteral("Metadata read failed: %1").arg(QString::fromUtf8(failure.message()))};
        // Without cards the folders and the caller's kind are all there is to go by.
        detection = FrameDetection::detect(filePath, nullptr, defaultType);
        metadata = createMetadataFallback(detection.type, fileInfo);
    }

    out.detected =
        DetectedFrameInfo{detection.type, FrameInfo(FrameId(filePath), filePath, metadata), detection.evidence};
    return out;
}

} // namespace ImageIntegration
