// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <string>
#include <vector>
#include <map>
#include <optional>

/**
 * @brief Pipeline manifest for tracking staged pipeline execution
 *
 * The manifest is a JSON file (pipeline_manifest.json) that persists:
 * - Pipeline version and run identification
 * - Per-stage parameters hashes for change detection
 * - Canonical ordered list of images with stable indices
 * - Mapping of paths: raw → calibrated → aligned
 * - Per-image status (succeeded/failed/skipped) and error strings
 *
 * This enables:
 * - Resuming from intermediate stages
 * - Detecting parameter changes (invalidating cached outputs)
 * - Correlating logs with outputs via stable indices
 * - Weight vectors matching stack input order
 */
class PipelineManifest
{
public:
    /// Current manifest version - increment when format or semantics change
    static constexpr int CURRENT_VERSION = 1;

    /// Pipeline stage identifiers
    enum class Stage
    {
        Calibration,
        StarDetection,
        Registration,
        Alignment,
        Stacking
    };

    /// Stage status
    enum class StageStatus
    {
        NotStarted,
        InProgress,
        Completed,
        Failed,
        Cancelled,
        Skipped,
        Implicit ///< Stage performed implicitly as part of another stage (e.g., calibration in combined mode)
    };

    /// Per-image status
    enum class ImageStatus
    {
        Pending,
        Succeeded,
        Failed,
        Skipped
    };

    /// Stage information stored in manifest
    struct StageInfo
    {
        StageStatus status = StageStatus::NotStarted;
        QString paramsHash; ///< SHA256 hash of stage parameters
        QString outputDir; ///< Output directory for this stage (relative to manifest dir)
        QString outputFile; ///< Optional output file for this stage (relative to manifest dir)
        QDateTime startTime; ///< When stage started
        QDateTime endTime; ///< When stage completed
        int durationMs = 0; ///< Duration in milliseconds

        // Calibration-specific info
        bool biasProvided = false;
        bool darkProvided = false;
        bool flatProvided = false;

        /// Convert to JSON object
        [[nodiscard]] QJsonObject toJson() const;

        /// Parse from JSON object
        static StageInfo fromJson(const QJsonObject& json);
    };

    /// Per-image tracking information
    struct ImageEntry
    {
        int index = 0; ///< Stable index in canonical order
        QString rawPath; ///< Original raw image path
        QString calibratedPath; ///< Path to calibrated output (relative)
        QString alignedPath; ///< Path to aligned output (relative)
        // Overall status for the pipeline (can be used as a quick "included/excluded" flag).
        ImageStatus status = ImageStatus::Pending;
        QString errorMessage; ///< Most recent error message (if any)

        // Per-stage per-image statuses for traceability and correct resume.
        ImageStatus calibrationStatus = ImageStatus::Pending;
        ImageStatus starDetectionStatus = ImageStatus::Pending;
        ImageStatus registrationStatus = ImageStatus::Pending;
        ImageStatus alignmentStatus = ImageStatus::Pending;
        ImageStatus stackingStatus = ImageStatus::Pending;

        // Per-stage short error strings (empty if none).
        QString calibrationError;
        QString starDetectionError;
        QString registrationError;
        QString alignmentError;
        QString stackingError;

        // Registration data (optional)
        int starCount = 0;
        double fwhm = 0.0;
        double quality = 0.0;

        /// Convert to JSON object
        [[nodiscard]] QJsonObject toJson() const;

        /// Parse from JSON object
        static ImageEntry fromJson(const QJsonObject& json);
    };

    /// Timing statistics for each stage
    struct TimingInfo
    {
        QDateTime startUtc;
        QDateTime endUtc;
        int durationMs = 0;

        [[nodiscard]] QJsonObject toJson() const;
        static TimingInfo fromJson(const QJsonObject& json);
    };

public:
    PipelineManifest() = default;
    ~PipelineManifest() = default;

    // Prevent copying, allow moving
    PipelineManifest(const PipelineManifest&) = delete;
    PipelineManifest& operator=(const PipelineManifest&) = delete;
    PipelineManifest(PipelineManifest&&) = default;
    PipelineManifest& operator=(PipelineManifest&&) = default;

    /// Initialize a new manifest for a fresh run
    void initializeNewRun(const QString& outputDir, const std::vector<QString>& imagePaths);

    /// Load manifest from disk
    [[nodiscard]] bool loadFromFile(const QString& manifestPath);

    /// Save manifest to disk
    [[nodiscard]] bool saveToFile() const;

    /// Get the manifest file path
    [[nodiscard]] QString getManifestPath() const
    {
        return m_ManifestPath;
    }

    /// Get output directory
    [[nodiscard]] QString getOutputDir() const
    {
        return m_OutputDir;
    }

    // ========== Run Information ==========

    [[nodiscard]] QString getRunId() const
    {
        return m_RunId;
    }
    [[nodiscard]] int getPipelineVersion() const
    {
        return m_PipelineVersion;
    }
    [[nodiscard]] QDateTime getCreatedTime() const
    {
        return m_CreatedUtc;
    }

    // ========== Stage Management ==========

    /// Get stage info
    [[nodiscard]] const StageInfo& getStageInfo(Stage stage) const;

    /// Update stage status
    void setStageStatus(Stage stage, StageStatus status);

    /// Set stage parameters hash for change detection
    void setStageParamsHash(Stage stage, const QString& hash);

    /// Check if stage parameters have changed (hash mismatch)
    [[nodiscard]] bool stageParamsChanged(Stage stage, const QString& currentHash) const;

    /// Set stage output directory
    void setStageOutputDir(Stage stage, const QString& dir);

    /// Set stage output file (relative to manifest dir)
    void setStageOutputFile(Stage stage, const QString& file);

    /// Set calibration-specific info
    void setCalibrationFrameInfo(bool hasBias, bool hasDark, bool hasFlat);

    /// Mark stage as started
    void markStageStarted(Stage stage);

    /// Mark stage as completed
    void markStageCompleted(Stage stage);

    /// Mark stage as failed
    void markStageFailed(Stage stage);

    /// Mark stage as cancelled
    void markStageCancelled(Stage stage);

    /// Mark stage as implicit (performed as part of another stage)
    /// Used for in-memory calibration mode where calibration is merged with star detection.
    void markStageImplicit(Stage stage);

    /// Check if a stage is completed successfully
    [[nodiscard]] bool isStageCompleted(Stage stage) const;

    /// Check if a stage was performed implicitly as part of another stage
    [[nodiscard]] bool isStageImplicit(Stage stage) const;

    /// Check if all prerequisites for a stage are completed
    [[nodiscard]] bool canResumeStage(Stage stage) const;

    // ========== Image Management ==========

    /// Get all image entries
    [[nodiscard]] const std::vector<ImageEntry>& getImageEntries() const
    {
        return m_Images;
    }

    /// Get image entry by index
    [[nodiscard]] const ImageEntry& getImageEntry(int index) const;

    /// Get image entry by raw path
    [[nodiscard]] std::optional<std::reference_wrapper<const ImageEntry>>
    findImageByRawPath(const QString& rawPath) const;

    /// Update image calibrated path
    void setImageCalibratedPath(int index, const QString& calibratedPath);

    /// Update image aligned path
    void setImageAlignedPath(int index, const QString& alignedPath);

    /// Update image status
    void setImageStatus(int index, ImageStatus status, const QString& errorMessage = QString());

    /// Update image registration data
    void setImageRegistrationData(int index, int starCount, double fwhm, double quality);

    /// Get raw image paths in canonical order (as stored in the manifest)
    [[nodiscard]] std::vector<QString> getRawImagePaths() const;

    /// Get images in canonical order for stacking
    [[nodiscard]] std::vector<QString> getCalibratedImagePaths() const;

    /// Get aligned images in canonical order for stacking
    [[nodiscard]] std::vector<QString> getAlignedImagePaths() const;

    /// Get calibrated images in canonical order along with manifest indices
    [[nodiscard]] std::vector<std::pair<int, QString>> getCalibratedImagePathsWithIndex() const;

    /// Get aligned images in canonical order along with manifest indices
    [[nodiscard]] std::vector<std::pair<int, QString>> getAlignedImagePathsWithIndex() const;

    /// Get count of successfully processed images for a stage
    [[nodiscard]] int getSucceededCount() const;
    [[nodiscard]] int getFailedCount() const;
    [[nodiscard]] int getSkippedCount() const;

    /// Get per-stage status counts (based on per-image per-stage statuses)
    [[nodiscard]] int getStageSucceededCount(Stage stage) const;
    [[nodiscard]] int getStageFailedCount(Stage stage) const;
    [[nodiscard]] int getStageSkippedCount(Stage stage) const;

    /// Update per-stage image status and error
    void setImageStageStatus(int index, Stage stage, ImageStatus status, const QString& errorMessage = QString());

    // ========== Timing ==========

    /// Set timing info for a stage
    void setStageTiming(Stage stage, const TimingInfo& timing);

    /// Get timing info for a stage
    [[nodiscard]] std::optional<TimingInfo> getStageTiming(Stage stage) const;

    // ========== Validation ==========

    /// Validate that all calibrated files exist on disk
    [[nodiscard]] bool validateCalibratedFiles() const;

    /// Validate that all aligned files exist on disk
    [[nodiscard]] bool validateAlignedFiles() const;

    /// Check if manifest exists at given output directory
    [[nodiscard]] static bool manifestExists(const QString& outputDir);

    /// Get manifest file name
    [[nodiscard]] static QString getManifestFileName()
    {
        return QStringLiteral("pipeline_manifest.json");
    }

    // ========== Utility ==========

    /// Generate a unique run ID
    [[nodiscard]] static QString generateRunId();

    /// Compute SHA256 hash of a string (for parameter change detection)
    [[nodiscard]] static QString computeHash(const QString& data);

    /// Convert stage enum to string
    [[nodiscard]] static QString stageToString(Stage stage);

    /// Convert string to stage enum
    [[nodiscard]] static Stage stringToStage(const QString& str);

    /// Convert status enum to string
    [[nodiscard]] static QString statusToString(StageStatus status);

    /// Convert string to status enum
    [[nodiscard]] static StageStatus stringToStatus(const QString& str);

    /// Convert image status enum to string
    [[nodiscard]] static QString imageStatusToString(ImageStatus status);

    /// Convert string to image status enum
    [[nodiscard]] static ImageStatus stringToImageStatus(const QString& str);

private:
    /// Convert entire manifest to JSON
    [[nodiscard]] QJsonObject toJson() const;

    /// Parse entire manifest from JSON
    bool fromJson(const QJsonObject& json);

private:
    QString m_ManifestPath; ///< Full path to manifest file
    QString m_OutputDir; ///< Base output directory
    QString m_RunId; ///< Unique run identifier
    int m_PipelineVersion = CURRENT_VERSION;
    QDateTime m_CreatedUtc;
    QDateTime m_LastModifiedUtc;

    /// Per-stage information
    std::map<Stage, StageInfo> m_Stages;

    /// Per-stage timing
    std::map<Stage, TimingInfo> m_Timing;

    /// Canonical ordered list of images
    std::vector<ImageEntry> m_Images;

    /// Map from raw path to vector position for quick lookup (NOT the stable image index).
    std::map<QString, int> m_RawPathToVectorPos;
};
