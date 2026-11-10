// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// How a file becomes a frame of the integration model: its header probed through the engine in an operation the
/// session admits, its kind read from its cards before its folder (`FrameDetection.h`), and the figures grouping and
/// calibration matching read taken from its acquisition record and its cards.

#include "processes/ImageIntegration/core/FrameDetection.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/FrameType.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QString>

#include <optional>
#include <vector>

namespace an
{
class ImageMetadata;
class RawFrameDescriptor;
} // namespace an

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

class QFileInfo;

namespace ImageIntegration
{

enum class ImportIssueSeverity
{
    Warning,
    Error
};

struct ImportIssue
{
    ImportIssueSeverity severity = ImportIssueSeverity::Error;
    QString path;
    QString message;
};

struct ImportResult
{
    int importedCount = 0;
    int skippedCount = 0;
    std::vector<ImportIssue> issues;

    [[nodiscard]] bool hasIssues() const
    {
        return !issues.empty();
    }
    [[nodiscard]] bool hasErrors() const;
    [[nodiscard]] QString summaryMessage() const;
    [[nodiscard]] QString detailedText(int maxEntries = 200) const;
};

struct DetectedFrameInfo
{
    FrameType type;
    FrameInfo info;
    /// What decided the frame's kind.
    FrameTypeEvidence typeEvidence = FrameTypeEvidence::Fallback;
};

struct BuildDetectedFrameInfoResult
{
    std::optional<DetectedFrameInfo> detected;
    std::optional<ImportIssue> issue;
};

/// How an import asks the session for the thread its probe runs on.
enum class ImportAdmission
{
    /// Waits in the session's queue: for a worker thread, which an import's own pool is.
    Wait,

    /// Takes a thread only when one is free now, and otherwise answers as busy: for the GUI thread, which never waits.
    NoWait
};

/**
 * @brief The operation a probe asks the session for: batch work on one thread.
 *
 * Reading a header is one thread's input and output, and an import probes many files at once, so each probe asks for
 * one thread rather than for every thread its purpose may have, which would run the probes one after another.
 *
 * @return the request.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API an::consumer::OperationRequest importProbeRequest();

/**
 * @brief Reads one file as a frame of the model.
 *
 * The file is probed in the operation `importProbeRequest` asks for - its header only, no sample decoded. A path that
 * is empty, names no file, or names a format the session's codecs do not read is refused with an error and no frame. A
 * file that cannot be probed, or a probe the session does not admit, is still imported with a warning: its kind from
 * its folders or `defaultType`, its size zero and its capture time the file's modification time.
 *
 * The figures come from the acquisition record a decoder states and, where it states none, from the cards
 * `an::projectAcquisition` reads: the exposure (`EXPOSURE`, `EXPTIME`, Exif), the sensor temperature (`CCD-TEMP`), the
 * ISO speed or else the camera's gain setting (`ISOSPEED`, `GAIN`), the camera's offset (`OFFSET`), the filter, the
 * target and the camera (`INSTRUME`, Exif model), the latter three with the sentinels `-` and `N/A` read as nothing.
 * The capture time is the exposure start (`DATE-OBS`, Exif original time) in UTC, else the time the file states it was
 * made (`DATE`, `DATE-LOC`), else the file's modification time.
 *
 * Thread-safe.
 *
 * @param session the session whose codecs read the file.
 * @param filePath the file.
 * @param defaultType the kind a frame is imported as when neither its cards nor its folders state one.
 * @param admission whether the probe may wait for a thread.
 * @return the frame, an issue, or both.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API BuildDetectedFrameInfoResult
buildDetectedFrameInfoWithIssue(const an::consumer::EngineSession& session, const QString& filePath,
                                FrameType defaultType, ImportAdmission admission = ImportAdmission::Wait);

/**
 * @brief The model's figures for a frame from the metadata a probe read.
 *
 * @param metadata what the probe read.
 * @param type the frame's kind.
 * @param width the image's width in pixels.
 * @param height the image's height in pixels.
 * @param fileInfo the file, whose modification time is the capture time when the metadata states none.
 * @param sampleLayout what each pixel holds, from the probe's descriptor (`sampleLayoutOf`).
 * @return the figures, as `buildDetectedFrameInfoWithIssue` describes them.
 */
[[nodiscard]] FrameMetadata frameMetadataFromProbe(const an::ImageMetadata& metadata, FrameType type, int width,
                                                   int height, const QFileInfo& fileInfo,
                                                   std::optional<FrameSampleLayout> sampleLayout = std::nullopt);

/**
 * @brief What each pixel of a probed image holds.
 *
 * @param descriptor the probe's description of the image.
 * @return its channel count, and whether that one channel is a colour-filter mosaic.
 */
[[nodiscard]] FrameSampleLayout sampleLayoutOf(const an::RawFrameDescriptor& descriptor);

AFTERNIGHTPROCESSES_API void sortDetectedFramesForImport(std::vector<DetectedFrameInfo>& detectedFrames);

// Heuristic: returns true if a file path looks like a pre-generated master calibration frame.
// Used to auto-mark imported Bias/Dark/Flat masters as MasterSource::AutoDetected.
[[nodiscard]] bool isProbablyMasterFilePath(const QString& filePath);

} // namespace ImageIntegration
