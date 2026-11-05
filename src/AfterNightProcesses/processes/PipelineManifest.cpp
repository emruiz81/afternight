// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PipelineManifest.h"

#include <AfterNightPlatform/Logging.h>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>

// ============================================================================
// StageInfo
// ============================================================================

QJsonObject PipelineManifest::StageInfo::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("status")] = PipelineManifest::statusToString(status);
    obj[QStringLiteral("paramsHash")] = paramsHash;
    if (!outputDir.isEmpty())
    {
        obj[QStringLiteral("outputDir")] = outputDir;
    }
    if (!outputFile.isEmpty())
    {
        obj[QStringLiteral("outputFile")] = outputFile;
    }
    if (startTime.isValid())
    {
        obj[QStringLiteral("startTime")] = startTime.toString(Qt::ISODate);
    }
    if (endTime.isValid())
    {
        obj[QStringLiteral("endTime")] = endTime.toString(Qt::ISODate);
    }
    if (durationMs > 0)
    {
        obj[QStringLiteral("durationMs")] = durationMs;
    }

    // Calibration-specific fields (only for calibration stage)
    obj[QStringLiteral("biasProvided")] = biasProvided;
    obj[QStringLiteral("darkProvided")] = darkProvided;
    obj[QStringLiteral("flatProvided")] = flatProvided;

    return obj;
}

PipelineManifest::StageInfo PipelineManifest::StageInfo::fromJson(const QJsonObject& json)
{
    StageInfo info;
    info.status = PipelineManifest::stringToStatus(json[QStringLiteral("status")].toString());
    info.paramsHash = json[QStringLiteral("paramsHash")].toString();
    info.outputDir = json[QStringLiteral("outputDir")].toString();
    info.outputFile = json[QStringLiteral("outputFile")].toString();

    const QString startTimeStr = json[QStringLiteral("startTime")].toString();
    if (!startTimeStr.isEmpty())
    {
        info.startTime = QDateTime::fromString(startTimeStr, Qt::ISODate);
    }

    const QString endTimeStr = json[QStringLiteral("endTime")].toString();
    if (!endTimeStr.isEmpty())
    {
        info.endTime = QDateTime::fromString(endTimeStr, Qt::ISODate);
    }

    info.durationMs = json[QStringLiteral("durationMs")].toInt(0);
    info.biasProvided = json[QStringLiteral("biasProvided")].toBool(false);
    info.darkProvided = json[QStringLiteral("darkProvided")].toBool(false);
    info.flatProvided = json[QStringLiteral("flatProvided")].toBool(false);

    return info;
}

// ============================================================================
// ImageEntry
// ============================================================================

QJsonObject PipelineManifest::ImageEntry::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("index")] = index;
    obj[QStringLiteral("rawPath")] = rawPath;

    if (!calibratedPath.isEmpty())
    {
        obj[QStringLiteral("calibratedPath")] = calibratedPath;
    }
    if (!alignedPath.isEmpty())
    {
        obj[QStringLiteral("alignedPath")] = alignedPath;
    }

    obj[QStringLiteral("status")] = PipelineManifest::imageStatusToString(status);

    if (!errorMessage.isEmpty())
    {
        obj[QStringLiteral("error")] = errorMessage;
    }

    // Per-stage per-image statuses
    QJsonObject stageStatusObj;
    stageStatusObj[QStringLiteral("calibration")] = PipelineManifest::imageStatusToString(calibrationStatus);
    stageStatusObj[QStringLiteral("starDetection")] = PipelineManifest::imageStatusToString(starDetectionStatus);
    stageStatusObj[QStringLiteral("registration")] = PipelineManifest::imageStatusToString(registrationStatus);
    stageStatusObj[QStringLiteral("alignment")] = PipelineManifest::imageStatusToString(alignmentStatus);
    stageStatusObj[QStringLiteral("stacking")] = PipelineManifest::imageStatusToString(stackingStatus);
    obj[QStringLiteral("stageStatus")] = stageStatusObj;

    // Per-stage error strings
    QJsonObject stageErrorsObj;
    if (!calibrationError.isEmpty())
        stageErrorsObj[QStringLiteral("calibration")] = calibrationError;
    if (!starDetectionError.isEmpty())
        stageErrorsObj[QStringLiteral("starDetection")] = starDetectionError;
    if (!registrationError.isEmpty())
        stageErrorsObj[QStringLiteral("registration")] = registrationError;
    if (!alignmentError.isEmpty())
        stageErrorsObj[QStringLiteral("alignment")] = alignmentError;
    if (!stackingError.isEmpty())
        stageErrorsObj[QStringLiteral("stacking")] = stackingError;
    if (!stageErrorsObj.isEmpty())
    {
        obj[QStringLiteral("stageErrors")] = stageErrorsObj;
    }

    // Registration data
    if (starCount > 0)
    {
        obj[QStringLiteral("starCount")] = starCount;
    }
    if (fwhm > 0.0)
    {
        obj[QStringLiteral("fwhm")] = fwhm;
    }
    if (quality > 0.0)
    {
        obj[QStringLiteral("quality")] = quality;
    }

    return obj;
}

PipelineManifest::ImageEntry PipelineManifest::ImageEntry::fromJson(const QJsonObject& json)
{
    ImageEntry entry;
    entry.index = json[QStringLiteral("index")].toInt(0);
    entry.rawPath = json[QStringLiteral("rawPath")].toString();
    entry.calibratedPath = json[QStringLiteral("calibratedPath")].toString();
    entry.alignedPath = json[QStringLiteral("alignedPath")].toString();
    entry.status = PipelineManifest::stringToImageStatus(json[QStringLiteral("status")].toString());
    entry.errorMessage = json[QStringLiteral("error")].toString();

    if (json.contains(QStringLiteral("stageStatus")))
    {
        const QJsonObject stageStatusObj = json[QStringLiteral("stageStatus")].toObject();
        entry.calibrationStatus =
            PipelineManifest::stringToImageStatus(stageStatusObj[QStringLiteral("calibration")].toString());
        entry.starDetectionStatus =
            PipelineManifest::stringToImageStatus(stageStatusObj[QStringLiteral("starDetection")].toString());
        entry.registrationStatus =
            PipelineManifest::stringToImageStatus(stageStatusObj[QStringLiteral("registration")].toString());
        entry.alignmentStatus =
            PipelineManifest::stringToImageStatus(stageStatusObj[QStringLiteral("alignment")].toString());
        entry.stackingStatus =
            PipelineManifest::stringToImageStatus(stageStatusObj[QStringLiteral("stacking")].toString());
    }

    if (json.contains(QStringLiteral("stageErrors")))
    {
        const QJsonObject stageErrorsObj = json[QStringLiteral("stageErrors")].toObject();
        entry.calibrationError = stageErrorsObj[QStringLiteral("calibration")].toString();
        entry.starDetectionError = stageErrorsObj[QStringLiteral("starDetection")].toString();
        entry.registrationError = stageErrorsObj[QStringLiteral("registration")].toString();
        entry.alignmentError = stageErrorsObj[QStringLiteral("alignment")].toString();
        entry.stackingError = stageErrorsObj[QStringLiteral("stacking")].toString();
    }
    entry.starCount = json[QStringLiteral("starCount")].toInt(0);
    entry.fwhm = json[QStringLiteral("fwhm")].toDouble(0.0);
    entry.quality = json[QStringLiteral("quality")].toDouble(0.0);

    return entry;
}

// ============================================================================
// TimingInfo
// ============================================================================

QJsonObject PipelineManifest::TimingInfo::toJson() const
{
    QJsonObject obj;
    if (startUtc.isValid())
    {
        obj[QStringLiteral("startUtc")] = startUtc.toString(Qt::ISODate);
    }
    if (endUtc.isValid())
    {
        obj[QStringLiteral("endUtc")] = endUtc.toString(Qt::ISODate);
    }
    obj[QStringLiteral("durationMs")] = durationMs;
    return obj;
}

PipelineManifest::TimingInfo PipelineManifest::TimingInfo::fromJson(const QJsonObject& json)
{
    TimingInfo info;

    const QString startStr = json[QStringLiteral("startUtc")].toString();
    if (!startStr.isEmpty())
    {
        info.startUtc = QDateTime::fromString(startStr, Qt::ISODate);
    }

    const QString endStr = json[QStringLiteral("endUtc")].toString();
    if (!endStr.isEmpty())
    {
        info.endUtc = QDateTime::fromString(endStr, Qt::ISODate);
    }

    info.durationMs = json[QStringLiteral("durationMs")].toInt(0);
    return info;
}

// ============================================================================
// PipelineManifest
// ============================================================================

void PipelineManifest::initializeNewRun(const QString& outputDir, const std::vector<QString>& imagePaths)
{
    m_OutputDir = outputDir;
    m_ManifestPath = QDir(outputDir).filePath(getManifestFileName());
    m_RunId = generateRunId();
    m_PipelineVersion = CURRENT_VERSION;
    m_CreatedUtc = QDateTime::currentDateTimeUtc();
    m_LastModifiedUtc = m_CreatedUtc;

    // Initialize stages
    m_Stages.clear();
    for (auto stage :
         {Stage::Calibration, Stage::StarDetection, Stage::Registration, Stage::Alignment, Stage::Stacking})
    {
        m_Stages[stage] = StageInfo{};
    }

    // Initialize images with canonical ordering
    m_Images.clear();
    m_RawPathToVectorPos.clear();
    m_Images.reserve(imagePaths.size());

    for (size_t i = 0; i < imagePaths.size(); ++i)
    {
        ImageEntry entry;
        entry.index = static_cast<int>(i);
        entry.rawPath = imagePaths[i];
        entry.status = ImageStatus::Pending;

        entry.calibrationStatus = ImageStatus::Pending;
        entry.starDetectionStatus = ImageStatus::Pending;
        entry.registrationStatus = ImageStatus::Pending;
        entry.alignmentStatus = ImageStatus::Pending;
        entry.stackingStatus = ImageStatus::Pending;

        m_Images.push_back(std::move(entry));
        m_RawPathToVectorPos[imagePaths[i]] = static_cast<int>(i);
    }

    LOG_INFO("Initialized new pipeline run: runId={}, outputDir={}, imageCount={}", m_RunId.toStdString(),
             m_OutputDir.toStdString(), imagePaths.size());
}

bool PipelineManifest::loadFromFile(const QString& manifestPath)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        LOG_ERROR("Failed to open manifest file for reading: {}", manifestPath.toStdString());
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        LOG_ERROR("Failed to parse manifest JSON: {} at offset {}", parseError.errorString().toStdString(),
                  parseError.offset);
        return false;
    }

    if (!doc.isObject())
    {
        LOG_ERROR("Manifest JSON is not an object");
        return false;
    }

    m_ManifestPath = manifestPath;
    m_OutputDir = QFileInfo(manifestPath).absolutePath();

    if (!fromJson(doc.object()))
    {
        LOG_ERROR("Failed to parse manifest content");
        return false;
    }

    LOG_INFO("Loaded pipeline manifest: runId={}, version={}, imageCount={}", m_RunId.toStdString(), m_PipelineVersion,
             m_Images.size());

    return true;
}

bool PipelineManifest::saveToFile() const
{
    if (m_ManifestPath.isEmpty())
    {
        LOG_ERROR("Cannot save manifest: path not set");
        return false;
    }

    // Ensure output directory exists
    const QDir outputDir(m_OutputDir);
    if (!outputDir.exists())
    {
        if (!outputDir.mkpath(QStringLiteral(".")))
        {
            LOG_ERROR("Failed to create output directory: {}", m_OutputDir.toStdString());
            return false;
        }
    }

    const QJsonDocument doc(toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    // Write to temp file first, then rename (atomic write)
    const QString tempPath = m_ManifestPath + QStringLiteral(".tmp");
    QFile tempFile(tempPath);

    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        LOG_ERROR("Failed to open temp manifest file for writing: {}", tempPath.toStdString());
        return false;
    }

    if (tempFile.write(data) != data.size())
    {
        LOG_ERROR("Failed to write manifest data to temp file");
        tempFile.close();
        QFile::remove(tempPath);
        return false;
    }

    tempFile.close();

    // Remove existing manifest if present
    if (QFile::exists(m_ManifestPath))
    {
        if (!QFile::remove(m_ManifestPath))
        {
            LOG_WARNING("Failed to remove existing manifest file");
        }
    }

    // Rename temp file to final name
    if (!QFile::rename(tempPath, m_ManifestPath))
    {
        LOG_ERROR("Failed to rename temp manifest file to final path");
        QFile::remove(tempPath);
        return false;
    }

    LOG_DEBUG("Saved pipeline manifest: {}", m_ManifestPath.toStdString());
    return true;
}

const PipelineManifest::StageInfo& PipelineManifest::getStageInfo(Stage stage) const
{
    static const StageInfo emptyInfo{};
    const auto it = m_Stages.find(stage);
    if (it == m_Stages.end())
    {
        return emptyInfo;
    }
    return it->second;
}

void PipelineManifest::setStageStatus(Stage stage, StageStatus status)
{
    m_Stages[stage].status = status;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setStageParamsHash(Stage stage, const QString& hash)
{
    m_Stages[stage].paramsHash = hash;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

bool PipelineManifest::stageParamsChanged(Stage stage, const QString& currentHash) const
{
    const auto& info = getStageInfo(stage);
    if (info.paramsHash.isEmpty())
    {
        return false; // No previous hash, assume no change
    }
    return info.paramsHash != currentHash;
}

void PipelineManifest::setStageOutputDir(Stage stage, const QString& dir)
{
    m_Stages[stage].outputDir = dir;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setStageOutputFile(Stage stage, const QString& file)
{
    m_Stages[stage].outputFile = file;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setCalibrationFrameInfo(bool hasBias, bool hasDark, bool hasFlat)
{
    auto& info = m_Stages[Stage::Calibration];
    info.biasProvided = hasBias;
    info.darkProvided = hasDark;
    info.flatProvided = hasFlat;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::markStageStarted(Stage stage)
{
    auto& info = m_Stages[stage];
    info.status = StageStatus::InProgress;
    info.startTime = QDateTime::currentDateTimeUtc();
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();

    // Keep timing map consistent with StageInfo
    {
        TimingInfo timing;
        timing.startUtc = info.startTime;
        timing.endUtc = QDateTime{};
        timing.durationMs = 0;
        m_Timing[stage] = timing;
    }

    LOG_INFO("[Pipeline] Stage {} started", stageToString(stage).toStdString());
}

void PipelineManifest::markStageCompleted(Stage stage)
{
    auto& info = m_Stages[stage];
    info.status = StageStatus::Completed;
    info.endTime = QDateTime::currentDateTimeUtc();

    if (info.startTime.isValid())
    {
        info.durationMs = static_cast<int>(info.startTime.msecsTo(info.endTime));
    }

    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();

    // Keep timing map consistent with StageInfo
    {
        TimingInfo timing;
        timing.startUtc = info.startTime;
        timing.endUtc = info.endTime;
        timing.durationMs = info.durationMs;
        m_Timing[stage] = timing;
    }

    LOG_INFO("[Pipeline] Stage {} completed (duration: {}ms)", stageToString(stage).toStdString(), info.durationMs);
}

void PipelineManifest::markStageFailed(Stage stage)
{
    auto& info = m_Stages[stage];
    info.status = StageStatus::Failed;
    info.endTime = QDateTime::currentDateTimeUtc();

    if (info.startTime.isValid())
    {
        info.durationMs = static_cast<int>(info.startTime.msecsTo(info.endTime));
    }

    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();

    // Keep timing map consistent with StageInfo
    {
        TimingInfo timing;
        timing.startUtc = info.startTime;
        timing.endUtc = info.endTime;
        timing.durationMs = info.durationMs;
        m_Timing[stage] = timing;
    }

    LOG_ERROR("[Pipeline] Stage {} failed", stageToString(stage).toStdString());
}

void PipelineManifest::markStageCancelled(Stage stage)
{
    auto& info = m_Stages[stage];
    info.status = StageStatus::Cancelled;
    info.endTime = QDateTime::currentDateTimeUtc();

    if (info.startTime.isValid())
    {
        info.durationMs = static_cast<int>(info.startTime.msecsTo(info.endTime));
    }

    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();

    // Keep timing map consistent with StageInfo
    {
        TimingInfo timing;
        timing.startUtc = info.startTime;
        timing.endUtc = info.endTime;
        timing.durationMs = info.durationMs;
        m_Timing[stage] = timing;
    }

    LOG_INFO("[Pipeline] Stage {} cancelled", stageToString(stage).toStdString());
}

void PipelineManifest::markStageImplicit(Stage stage)
{
    auto& info = m_Stages[stage];
    info.status = StageStatus::Implicit;
    info.endTime = QDateTime::currentDateTimeUtc();

    if (!info.startTime.isValid())
    {
        info.startTime = info.endTime;
    }

    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();

    LOG_INFO("[Pipeline] Stage {} marked as implicit (performed as part of another stage)",
             stageToString(stage).toStdString());
}

bool PipelineManifest::isStageCompleted(Stage stage) const
{
    return getStageInfo(stage).status == StageStatus::Completed;
}

bool PipelineManifest::isStageImplicit(Stage stage) const
{
    return getStageInfo(stage).status == StageStatus::Implicit;
}

bool PipelineManifest::canResumeStage(Stage stage) const
{
    // Helper lambda: checks if a stage is completed or implicit (both mean "done" for resume purposes)
    auto isStageCompletedOrImplicit = [this](Stage s) {
        const StageStatus status = getStageInfo(s).status;
        return status == StageStatus::Completed || status == StageStatus::Implicit;
    };

    // Check prerequisites for each stage
    switch (stage)
    {
    case Stage::Calibration:
        // Calibration has no prerequisites
        return true;

    case Stage::StarDetection:
        // Star detection requires calibration to be completed (or implicit in combined mode)
        return isStageCompletedOrImplicit(Stage::Calibration);

    case Stage::Registration:
        // Registration requires star detection
        return isStageCompletedOrImplicit(Stage::StarDetection);

    case Stage::Alignment:
        // Alignment requires registration
        return isStageCompletedOrImplicit(Stage::Registration);

    case Stage::Stacking:
        // Stacking requires alignment unless alignment was explicitly skipped (drizzle).
        return isStageCompletedOrImplicit(Stage::Alignment) ||
               getStageInfo(Stage::Alignment).status == StageStatus::Skipped;

    default:
        return false;
    }
}

const PipelineManifest::ImageEntry& PipelineManifest::getImageEntry(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_Images.size()))
    {
        static const ImageEntry emptyEntry{};
        LOG_ERROR("Invalid image index: {} (size={})", index, m_Images.size());
        return emptyEntry;
    }
    return m_Images[static_cast<size_t>(index)];
}

std::optional<std::reference_wrapper<const PipelineManifest::ImageEntry>>
PipelineManifest::findImageByRawPath(const QString& rawPath) const
{
    const auto it = m_RawPathToVectorPos.find(rawPath);
    if (it == m_RawPathToVectorPos.end())
    {
        return std::nullopt;
    }
    const int vectorPos = it->second;
    if (vectorPos < 0 || vectorPos >= static_cast<int>(m_Images.size()))
    {
        return std::nullopt;
    }
    return std::cref(m_Images[static_cast<size_t>(vectorPos)]);
}

void PipelineManifest::setImageCalibratedPath(int index, const QString& calibratedPath)
{
    if (index < 0 || index >= static_cast<int>(m_Images.size()))
    {
        LOG_ERROR("Invalid image index for setImageCalibratedPath: {}", index);
        return;
    }
    m_Images[static_cast<size_t>(index)].calibratedPath = calibratedPath;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setImageAlignedPath(int index, const QString& alignedPath)
{
    if (index < 0 || index >= static_cast<int>(m_Images.size()))
    {
        LOG_ERROR("Invalid image index for setImageAlignedPath: {}", index);
        return;
    }
    m_Images[static_cast<size_t>(index)].alignedPath = alignedPath;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setImageStatus(int index, ImageStatus status, const QString& errorMessage)
{
    if (index < 0 || index >= static_cast<int>(m_Images.size()))
    {
        LOG_ERROR("Invalid image index for setImageStatus: {}", index);
        return;
    }
    auto& entry = m_Images[static_cast<size_t>(index)];
    entry.status = status;
    entry.errorMessage = errorMessage;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setImageStageStatus(int index, Stage stage, ImageStatus status, const QString& errorMessage)
{
    if (index < 0 || index >= static_cast<int>(m_Images.size()))
    {
        LOG_ERROR("Invalid image index for setImageStageStatus: {}", index);
        return;
    }

    auto& entry = m_Images[static_cast<size_t>(index)];

    auto setStageFields = [&](ImageStatus& stageStatusField, QString& stageErrorField) {
        stageStatusField = status;
        stageErrorField = errorMessage;
    };

    switch (stage)
    {
    case Stage::Calibration:
        setStageFields(entry.calibrationStatus, entry.calibrationError);
        break;
    case Stage::StarDetection:
        setStageFields(entry.starDetectionStatus, entry.starDetectionError);
        break;
    case Stage::Registration:
        setStageFields(entry.registrationStatus, entry.registrationError);
        break;
    case Stage::Alignment:
        setStageFields(entry.alignmentStatus, entry.alignmentError);
        break;
    case Stage::Stacking:
        setStageFields(entry.stackingStatus, entry.stackingError);
        break;
    default:
        break;
    }

    // Keep a convenient overall error for quick inspection.
    if (!errorMessage.isEmpty())
    {
        entry.errorMessage = errorMessage;
    }

    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

void PipelineManifest::setImageRegistrationData(int index, int starCount, double fwhm, double quality)
{
    if (index < 0 || index >= static_cast<int>(m_Images.size()))
    {
        LOG_ERROR("Invalid image index for setImageRegistrationData: {}", index);
        return;
    }
    auto& entry = m_Images[static_cast<size_t>(index)];
    entry.starCount = starCount;
    entry.fwhm = fwhm;
    entry.quality = quality;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

std::vector<QString> PipelineManifest::getRawImagePaths() const
{
    std::vector<QString> paths;
    paths.reserve(m_Images.size());

    for (const auto& entry : m_Images)
    {
        paths.push_back(entry.rawPath);
    }

    return paths;
}

std::vector<QString> PipelineManifest::getCalibratedImagePaths() const
{
    std::vector<QString> paths;
    paths.reserve(m_Images.size());

    for (const auto& entry : m_Images)
    {
        if (entry.calibrationStatus == ImageStatus::Succeeded && !entry.calibratedPath.isEmpty())
        {
            // Convert relative path to absolute
            if (QDir::isRelativePath(entry.calibratedPath))
            {
                paths.push_back(QDir(m_OutputDir).filePath(entry.calibratedPath));
            }
            else
            {
                paths.push_back(entry.calibratedPath);
            }
        }
    }

    return paths;
}

std::vector<QString> PipelineManifest::getAlignedImagePaths() const
{
    std::vector<QString> paths;
    paths.reserve(m_Images.size());

    for (const auto& entry : m_Images)
    {
        if (entry.alignmentStatus == ImageStatus::Succeeded && !entry.alignedPath.isEmpty())
        {
            // Convert relative path to absolute
            if (QDir::isRelativePath(entry.alignedPath))
            {
                paths.push_back(QDir(m_OutputDir).filePath(entry.alignedPath));
            }
            else
            {
                paths.push_back(entry.alignedPath);
            }
        }
    }

    return paths;
}

std::vector<std::pair<int, QString>> PipelineManifest::getCalibratedImagePathsWithIndex() const
{
    std::vector<std::pair<int, QString>> result;
    result.reserve(m_Images.size());

    for (const auto& entry : m_Images)
    {
        if (entry.calibrationStatus == ImageStatus::Succeeded && !entry.calibratedPath.isEmpty())
        {
            QString fullPath = entry.calibratedPath;
            if (QDir::isRelativePath(fullPath))
            {
                fullPath = QDir(m_OutputDir).filePath(fullPath);
            }
            result.emplace_back(entry.index, fullPath);
        }
    }

    return result;
}

std::vector<std::pair<int, QString>> PipelineManifest::getAlignedImagePathsWithIndex() const
{
    std::vector<std::pair<int, QString>> result;
    result.reserve(m_Images.size());

    for (const auto& entry : m_Images)
    {
        if (entry.alignmentStatus == ImageStatus::Succeeded && !entry.alignedPath.isEmpty())
        {
            QString fullPath = entry.alignedPath;
            if (QDir::isRelativePath(fullPath))
            {
                fullPath = QDir(m_OutputDir).filePath(fullPath);
            }
            result.emplace_back(entry.index, fullPath);
        }
    }

    return result;
}

int PipelineManifest::getSucceededCount() const
{
    int count = 0;
    for (const auto& entry : m_Images)
    {
        if (entry.status == ImageStatus::Succeeded)
        {
            ++count;
        }
    }
    return count;
}

int PipelineManifest::getFailedCount() const
{
    int count = 0;
    for (const auto& entry : m_Images)
    {
        if (entry.status == ImageStatus::Failed)
        {
            ++count;
        }
    }
    return count;
}

int PipelineManifest::getSkippedCount() const
{
    int count = 0;
    for (const auto& entry : m_Images)
    {
        if (entry.status == ImageStatus::Skipped)
        {
            ++count;
        }
    }
    return count;
}

void PipelineManifest::setStageTiming(Stage stage, const TimingInfo& timing)
{
    m_Timing[stage] = timing;
    m_LastModifiedUtc = QDateTime::currentDateTimeUtc();
}

std::optional<PipelineManifest::TimingInfo> PipelineManifest::getStageTiming(Stage stage) const
{
    const auto it = m_Timing.find(stage);
    if (it == m_Timing.end())
    {
        return std::nullopt;
    }
    return it->second;
}

bool PipelineManifest::validateCalibratedFiles() const
{
    for (const auto& entry : m_Images)
    {
        if (entry.calibrationStatus == ImageStatus::Succeeded && !entry.calibratedPath.isEmpty())
        {
            QString fullPath = entry.calibratedPath;
            if (QDir::isRelativePath(fullPath))
            {
                fullPath = QDir(m_OutputDir).filePath(fullPath);
            }

            if (!QFile::exists(fullPath))
            {
                LOG_WARNING("Calibrated file missing: {}", fullPath.toStdString());
                return false;
            }
        }
    }
    return true;
}

bool PipelineManifest::validateAlignedFiles() const
{
    for (const auto& entry : m_Images)
    {
        if (entry.alignmentStatus == ImageStatus::Succeeded && !entry.alignedPath.isEmpty())
        {
            QString fullPath = entry.alignedPath;
            if (QDir::isRelativePath(fullPath))
            {
                fullPath = QDir(m_OutputDir).filePath(fullPath);
            }

            if (!QFile::exists(fullPath))
            {
                LOG_WARNING("Aligned file missing: {}", fullPath.toStdString());
                return false;
            }
        }
    }
    return true;
}

bool PipelineManifest::manifestExists(const QString& outputDir)
{
    return QFile::exists(QDir(outputDir).filePath(getManifestFileName()));
}

QString PipelineManifest::generateRunId()
{
    // Format: YYYY-MM-DDTHH-MM-SS_<uuid-suffix>
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString timestamp = now.toString(QStringLiteral("yyyy-MM-ddTHH-mm-ss"));
    const QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    return QStringLiteral("%1_%2").arg(timestamp, uuid);
}

QString PipelineManifest::computeHash(const QString& data)
{
    const QByteArray hash = QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("sha256:") + QString::fromLatin1(hash.toHex());
}

QString PipelineManifest::stageToString(Stage stage)
{
    switch (stage)
    {
    case Stage::Calibration:
        return QStringLiteral("calibration");
    case Stage::StarDetection:
        return QStringLiteral("starDetection");
    case Stage::Registration:
        return QStringLiteral("registration");
    case Stage::Alignment:
        return QStringLiteral("alignment");
    case Stage::Stacking:
        return QStringLiteral("stacking");
    default:
        return QStringLiteral("unknown");
    }
}

PipelineManifest::Stage PipelineManifest::stringToStage(const QString& str)
{
    if (str == QStringLiteral("calibration"))
        return Stage::Calibration;
    if (str == QStringLiteral("starDetection"))
        return Stage::StarDetection;
    if (str == QStringLiteral("registration"))
        return Stage::Registration;
    if (str == QStringLiteral("alignment"))
        return Stage::Alignment;
    if (str == QStringLiteral("stacking"))
        return Stage::Stacking;
    return Stage::Calibration; // Default fallback
}

QString PipelineManifest::statusToString(StageStatus status)
{
    switch (status)
    {
    case StageStatus::NotStarted:
        return QStringLiteral("not_started");
    case StageStatus::InProgress:
        return QStringLiteral("in_progress");
    case StageStatus::Completed:
        return QStringLiteral("completed");
    case StageStatus::Failed:
        return QStringLiteral("failed");
    case StageStatus::Cancelled:
        return QStringLiteral("cancelled");
    case StageStatus::Skipped:
        return QStringLiteral("skipped");
    case StageStatus::Implicit:
        return QStringLiteral("implicit");
    default:
        return QStringLiteral("unknown");
    }
}

PipelineManifest::StageStatus PipelineManifest::stringToStatus(const QString& str)
{
    if (str == QStringLiteral("not_started"))
        return StageStatus::NotStarted;
    if (str == QStringLiteral("in_progress"))
        return StageStatus::InProgress;
    if (str == QStringLiteral("completed"))
        return StageStatus::Completed;
    if (str == QStringLiteral("failed"))
        return StageStatus::Failed;
    if (str == QStringLiteral("cancelled"))
        return StageStatus::Cancelled;
    if (str == QStringLiteral("skipped"))
        return StageStatus::Skipped;
    if (str == QStringLiteral("implicit"))
        return StageStatus::Implicit;
    return StageStatus::NotStarted; // Default fallback
}

QString PipelineManifest::imageStatusToString(ImageStatus status)
{
    switch (status)
    {
    case ImageStatus::Pending:
        return QStringLiteral("pending");
    case ImageStatus::Succeeded:
        return QStringLiteral("succeeded");
    case ImageStatus::Failed:
        return QStringLiteral("failed");
    case ImageStatus::Skipped:
        return QStringLiteral("skipped");
    default:
        return QStringLiteral("unknown");
    }
}

PipelineManifest::ImageStatus PipelineManifest::stringToImageStatus(const QString& str)
{
    if (str == QStringLiteral("pending"))
        return ImageStatus::Pending;
    if (str == QStringLiteral("succeeded"))
        return ImageStatus::Succeeded;
    if (str == QStringLiteral("failed"))
        return ImageStatus::Failed;
    if (str == QStringLiteral("skipped"))
        return ImageStatus::Skipped;
    return ImageStatus::Pending; // Default fallback
}

QJsonObject PipelineManifest::toJson() const
{
    QJsonObject root;

    // Header info
    root[QStringLiteral("pipelineVersion")] = m_PipelineVersion;
    root[QStringLiteral("runId")] = m_RunId;
    root[QStringLiteral("outputDir")] = m_OutputDir;
    root[QStringLiteral("createdUtc")] = m_CreatedUtc.toString(Qt::ISODate);
    root[QStringLiteral("lastModifiedUtc")] = m_LastModifiedUtc.toString(Qt::ISODate);

    // Stages
    QJsonObject stagesObj;
    for (const auto& [stage, info] : m_Stages)
    {
        stagesObj[stageToString(stage)] = info.toJson();
    }
    root[QStringLiteral("stages")] = stagesObj;

    // Timing
    if (!m_Timing.empty())
    {
        QJsonObject timingObj;
        for (const auto& [stage, timing] : m_Timing)
        {
            timingObj[stageToString(stage)] = timing.toJson();
        }
        root[QStringLiteral("timing")] = timingObj;
    }

    // Images
    QJsonArray imagesArray;
    for (const auto& entry : m_Images)
    {
        imagesArray.append(entry.toJson());
    }
    root[QStringLiteral("images")] = imagesArray;

    return root;
}

bool PipelineManifest::fromJson(const QJsonObject& json)
{
    // Parse header
    m_PipelineVersion = json[QStringLiteral("pipelineVersion")].toInt(1);
    m_RunId = json[QStringLiteral("runId")].toString();
    // m_OutputDir is set by loadFromFile
    m_CreatedUtc = QDateTime::fromString(json[QStringLiteral("createdUtc")].toString(), Qt::ISODate);
    m_LastModifiedUtc = QDateTime::fromString(json[QStringLiteral("lastModifiedUtc")].toString(), Qt::ISODate);

    // Check version compatibility
    if (m_PipelineVersion > CURRENT_VERSION)
    {
        LOG_WARNING("Manifest version {} is newer than current version {}, some features may not work correctly",
                    m_PipelineVersion, CURRENT_VERSION);
    }

    // Parse stages
    m_Stages.clear();
    const QJsonObject stagesObj = json[QStringLiteral("stages")].toObject();
    for (auto it = stagesObj.begin(); it != stagesObj.end(); ++it)
    {
        const Stage stage = stringToStage(it.key());
        m_Stages[stage] = StageInfo::fromJson(it.value().toObject());
    }

    // Ensure all stages exist
    for (auto stage :
         {Stage::Calibration, Stage::StarDetection, Stage::Registration, Stage::Alignment, Stage::Stacking})
    {
        if (m_Stages.find(stage) == m_Stages.end())
        {
            m_Stages[stage] = StageInfo{};
        }
    }

    // Parse timing
    m_Timing.clear();
    if (json.contains(QStringLiteral("timing")))
    {
        const QJsonObject timingObj = json[QStringLiteral("timing")].toObject();
        for (auto it = timingObj.begin(); it != timingObj.end(); ++it)
        {
            const Stage stage = stringToStage(it.key());
            m_Timing[stage] = TimingInfo::fromJson(it.value().toObject());
        }
    }

    // Parse images
    m_Images.clear();
    m_RawPathToVectorPos.clear();
    const QJsonArray imagesArray = json[QStringLiteral("images")].toArray();
    m_Images.reserve(static_cast<size_t>(imagesArray.size()));

    int vectorPos = 0;
    for (const QJsonValue& value : imagesArray)
    {
        ImageEntry entry = ImageEntry::fromJson(value.toObject());
        m_RawPathToVectorPos[entry.rawPath] = vectorPos;
        m_Images.push_back(std::move(entry));
        ++vectorPos;
    }

    return true;
}

namespace
{
PipelineManifest::ImageStatus get_stage_status_from_entry(const PipelineManifest::ImageEntry& entry,
                                                          PipelineManifest::Stage stage)
{
    switch (stage)
    {
    case PipelineManifest::Stage::Calibration:
        return entry.calibrationStatus;
    case PipelineManifest::Stage::StarDetection:
        return entry.starDetectionStatus;
    case PipelineManifest::Stage::Registration:
        return entry.registrationStatus;
    case PipelineManifest::Stage::Alignment:
        return entry.alignmentStatus;
    case PipelineManifest::Stage::Stacking:
        return entry.stackingStatus;
    default:
        return entry.status;
    }
}
} // namespace

int PipelineManifest::getStageSucceededCount(Stage stage) const
{
    int count = 0;
    for (const auto& entry : m_Images)
    {
        if (get_stage_status_from_entry(entry, stage) == ImageStatus::Succeeded)
        {
            ++count;
        }
    }
    return count;
}

int PipelineManifest::getStageFailedCount(Stage stage) const
{
    int count = 0;
    for (const auto& entry : m_Images)
    {
        if (get_stage_status_from_entry(entry, stage) == ImageStatus::Failed)
        {
            ++count;
        }
    }
    return count;
}

int PipelineManifest::getStageSkippedCount(Stage stage) const
{
    int count = 0;
    for (const auto& entry : m_Images)
    {
        if (get_stage_status_from_entry(entry, stage) == ImageStatus::Skipped)
        {
            ++count;
        }
    }
    return count;
}
