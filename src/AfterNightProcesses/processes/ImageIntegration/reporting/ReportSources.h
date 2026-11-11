// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the report model's builder reads through.
///
/// A report states what a run left behind, and most of that is outside the manifest and the result: the metrics the
/// registration stored, the headers of the lights and the masters, the masters' samples, files on disk, and the
/// time. The builder reaches each of them through one of the interfaces here, so a suite can stand in for any of
/// them, and so the caller - not the builder - decides which metrics database a model is built from: the output
/// folder's for the run being committed, the archive's own snapshot for a run read later.
///
/// `ProductionReportSources` holds the implementations the product uses: the metrics database opened once, a header
/// probed and an image measured in the engine session it is given, and the host's files and clock.

#include "AfterNightProcessesExport.h"

#include "ReportModel.h"

#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class WorkflowExecutionCounterAccumulator;

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration::Reporting
{
/// What a registration recorded of one light.
struct RegistrationMetricRow
{
    int stars = 0;
    double fwhm = 0.0;
    double snr = 0.0;
    double eccentricity = 0.0;
    double photometricZeroPoint = 0.0;
    double zeroPointScatter = 0.0;
    double astrometricRmsPixels = 0.0;
    double astrometricRmsArcsec = 0.0;
    double limitingMagnitude = 0.0;
    int catalogMatchedStars = 0;
    bool succeeded = false;
    QString transformKind;

    /// The translation of the transform that brought the light to the reference, for a kind whose record carries a
    /// matrix; nothing for a polynomial kind, whose matrix is not the whole transform.
    std::optional<double> shiftX;
    std::optional<double> shiftY;

    /// The background noise the detection measured, whether or not the registration succeeded; zero when the
    /// database states none.
    double backgroundNoise = 0.0;

    /// The registered image the alignment wrote and its size; empty and negative when the database states none.
    QString outputPath;
    qint64 outputSizeBytes = -1;
};

/// What one read of an image measured: its figures per channel and how each channel's samples are spread.
struct ReportImageMeasurement
{
    ReportImageStatistics statistics;

    /// One distribution per channel, in the image's channel order; empty for an image whose validity a range cannot
    /// state.
    std::vector<ReportSampleDistribution> distribution;
};

/// The level and the noise of an image, per channel, and how much of the frame they were measured over.
struct ReportImageLevels
{
    /// Each channel's mean and background noise, as fractions of the display range, in the image's channel order.
    std::vector<double> level;
    std::vector<double> noise;

    /// The share of the frame's area measured: one for the whole frame.
    double measuredFraction = 1.0;
};

/// What the report reads of a file's header: its size and its metadata, never its pixels.
struct ReportHeader
{
    /// The image's width in pixels.
    std::uint64_t width = 0;

    /// The image's height in pixels.
    std::uint64_t height = 0;

    /// The metadata the header states.
    an::ImageMetadata metadata{};
};

/// One fit of a comet's path, with what the registration stored beside it.
struct CometReportRun
{
    an::ui::CometTrackingRunRecord run;
    std::optional<an::ui::CometTrackingRunSummaryRecord> summary;
    std::vector<an::ui::CometTrackingPointRecord> points;
    std::optional<an::ui::CometTransformRoleProduct> cometAlignedProduct;
};

/// The catalog sources one registration group matched and keeps for the colour calibration.
struct SpccHandoffGroupSummary
{
    /// The group's key as the manifest states it, and its name.
    QString groupKey;
    QString groupName;

    int settingsVersion = -1;
    int frameCount = 0;
    int referenceCount = 0;
    int sourceCount = 0;
    int satisfiedCount = 0;
    int missingSourceCount = 0;
    int missingChunkOrCacheCount = 0;
    int manifestMismatchCount = 0;
    int staleMatchCount = 0;
    int rejectedQualityFieldsCount = 0;
    int untestedCount = 0;
    QString astrometricManifestId;
    QString spccManifestId;
    QList<double> matchSeparationsArcsec;
    QList<double> residualsPx;
};

/// The metrics a run's registration stored.
class IReportMetrics
{
public:
    virtual ~IReportMetrics() = default;

    /// Whether there is a metrics database to read at all.
    [[nodiscard]] virtual bool available() = 0;

    /// The latest settings version a registration group has metrics under; nothing when it has none.
    [[nodiscard]] virtual std::optional<int> settingsVersion(const QString& groupKey) = 0;

    /// A registration group's metrics under one settings version, by each light's canonical path.
    [[nodiscard]] virtual QHash<QString, RegistrationMetricRow> metricsByFrame(const QString& groupKey,
                                                                               int settingsVersion) = 0;

    /// The background noise and scales of the lights of a group whose registration succeeded, by canonical path.
    [[nodiscard]] virtual QHash<QString, FrameNoiseStatistics> noiseByFrame(const QString& groupKey,
                                                                            int settingsVersion) = 0;

    /// The comet fits stored for the manifest's registration groups, latest first within a group.
    [[nodiscard]] virtual QList<CometReportRun> cometRuns(const WorkflowManifest& manifest) = 0;

    /// The matched catalog sources stored for the manifest's registration groups; a group with none is left out.
    [[nodiscard]] virtual QList<SpccHandoffGroupSummary> catalogHandoff(const WorkflowManifest& manifest) = 0;

    /// The light a registration group's lights were brought to, by its canonical path, as the registration stored it
    /// under one settings version; empty when it stored none.
    [[nodiscard]] virtual QString referenceFrame(const QString& groupKey, int settingsVersion) = 0;
};

/// The headers of the run's files.
class IReportHeaders
{
public:
    virtual ~IReportHeaders() = default;

    /// A file's header, read without its pixels; nothing when it cannot be read.
    [[nodiscard]] virtual std::optional<ReportHeader> probe(const QString& path) = 0;
};

/// The samples of the run's images.
class IReportImages
{
public:
    virtual ~IReportImages() = default;

    /// One read of an image, measured per channel: its figures and the distribution of its samples, in one admitted
    /// operation.
    ///
    /// @param path the image.
    /// @param width the most threads the measurement may use.
    /// @return what was measured; nothing when the image cannot be read or measured.
    [[nodiscard]] virtual std::optional<ReportImageMeasurement> measure(const QString& path, std::uint32_t width) = 0;

    /// One read of an image, measured for its level and its noise alone, over a rectangle at the centre of the frame
    /// when the frame is large. What a calibration master is measured by: the figures of a master frame are a
    /// second apiece for a large one, and a run may have a dozen.
    ///
    /// @param path the image.
    /// @param width the most threads the measurement may use.
    /// @return what was measured; nothing when the image cannot be read or measured.
    [[nodiscard]] virtual std::optional<ReportImageLevels> measureLevels(const QString& path, std::uint32_t width) = 0;
};

/// The files of the run's folders.
class IReportFiles
{
public:
    virtual ~IReportFiles() = default;

    /// Whether a path names a file that is there.
    [[nodiscard]] virtual bool exists(const QString& path) = 0;

    /// A file's size; nothing when it is not there.
    [[nodiscard]] virtual std::optional<qint64> size(const QString& path) = 0;

    /// When a file was last changed; nothing when it is not there.
    [[nodiscard]] virtual std::optional<QDateTime> lastModified(const QString& path) = 0;

    /// A text file's contents; nothing when it cannot be read.
    [[nodiscard]] virtual std::optional<QString> readText(const QString& path) = 0;

    /// The names of the folders directly inside a folder, in name order; empty when it is not there.
    [[nodiscard]] virtual QStringList folders(const QString& path) = 0;
};

/// The host the report is built on.
class IReportHost
{
public:
    virtual ~IReportHost() = default;

    /// The time now.
    [[nodiscard]] virtual QDateTime now() = 0;

    /// The host's time zone, by its IANA name.
    [[nodiscard]] virtual QString timeZoneId() = 0;

    /// The host's operating system and processor, as one line.
    [[nodiscard]] virtual QString platform() = 0;
};

/// What a report model is built through. Every source is borrowed for the build.
struct ReportSources
{
    IReportMetrics* metrics = nullptr;
    IReportHeaders* headers = nullptr;
    IReportImages* images = nullptr;
    IReportFiles* files = nullptr;
    IReportHost* host = nullptr;

    /// The session the headers and the images are read in, or null: what tells a report built without one from a
    /// file that could not be read.
    const an::consumer::EngineSession* session = nullptr;
};

/// The metrics database a report is built from: the output folder's, as the run being committed left it. A model is
/// built of a run once, as it is committed; a run read later is read from the report its archive keeps.
///
/// @param manifest the run's manifest.
/// @return the path, whether or not a file is there; empty when the manifest names no output folder.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString committedRunMetricsDatabase(const WorkflowManifest& manifest);

/// The sources the product builds a report through.
class AFTERNIGHTPROCESSES_API ProductionReportSources
{
public:
    /// The product's sources.
    ///
    /// @param session the session headers are probed, images measured and weights derived in, borrowed for the
    ///        object's life; without one no header and no image is read.
    /// @param metricsDatabasePath the metrics database the report is built from; empty, or a file that is not
    ///        there, for a run with none.
    /// @param counters the run's diagnostics, which count the database's statements, or null.
    explicit ProductionReportSources(const an::consumer::EngineSession* session, QString metricsDatabasePath,
                                     WorkflowExecutionCounterAccumulator* counters = nullptr);
    ~ProductionReportSources();

    ProductionReportSources(const ProductionReportSources&) = delete;
    ProductionReportSources& operator=(const ProductionReportSources&) = delete;

    /// The sources, borrowed from this object.
    [[nodiscard]] ReportSources sources() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace ImageIntegration::Reporting
