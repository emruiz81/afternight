// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the units that fill a report model share: the state of one build, the step each unit contributes, and the
/// functions that read a fact out of a run's records.
///
/// Private to the reporting units. Each `fill*` function states one area of the model - the masters, their lights,
/// the calibration, the registration, the settings, the run, the comparison with an earlier run - and is called once
/// by `buildReportModel`, in that order, since the run's rollups read what the masters measured. The functions below
/// them take a record and return the fact, and read nothing else.

#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/reporting/ReportModelBuilder.h"
#include "processes/ImageIntegration/reporting/ReportSources.h"

#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QCoreApplication>
#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ImageIntegration::Reporting
{
/// A sentence or a name of the report, in the report's translation context. No translation ships: the report reads
/// in English.
[[nodiscard]] inline QString reportText(const char* text)
{
    return QCoreApplication::translate("ImageIntegrationReport", text);
}

/// An earlier run of the output folder whose archive keeps its report model.
struct StoredRun
{
    /// The run's folder name under `history/`, which is its execution id made a file name.
    QString runId;

    ReportModel model;
};

/// An earlier run of the output folder, whether or not its archive keeps a report model this version reads.
struct EarlierArchive
{
    /// The run's folder name under `history/`.
    QString runId;

    /// The run's place among `ReportBuild::earlierRuns()`; nothing for an archive that gave no report model.
    std::optional<std::size_t> stored;

    /// Why the archive gave no report model, in a sentence; empty for one that gave it.
    QString refusal;
};

/// One build of a report model: the run, the sources, and what the build has read so far.
class ReportBuild
{
public:
    ReportBuild(const ReportModelInputs& inputs, const ReportSources& sources);

    [[nodiscard]] const WorkflowManifest& manifest() const noexcept
    {
        return m_inputs.manifest;
    }

    [[nodiscard]] const WorkflowExecutor::Result& result() const noexcept
    {
        return m_inputs.result;
    }

    [[nodiscard]] const ReportModelInputs& inputs() const noexcept
    {
        return m_inputs;
    }

    [[nodiscard]] const ReportSources& sources() const noexcept
    {
        return m_sources;
    }

    /// The anchor names of the run's groups.
    [[nodiscard]] ReportSlugs& slugs() noexcept
    {
        return m_slugs;
    }

    /// A path of the run as a file to read: as it is when absolute, else under the run's folder.
    [[nodiscard]] QString resolvePath(const QString& path) const;

    /// The file a result names: its path, or its file name under the output folder when it states no path.
    [[nodiscard]] QString outputFile(const IntegrationGroupResult& group) const;

    /// Whether a path, resolved as `resolvePath` resolves it, names a file that is there.
    [[nodiscard]] bool exists(const QString& path) const;

    /// A file's size, when it is there.
    [[nodiscard]] std::optional<qint64> fileSize(const QString& path) const;

    /// One read of an image, measured per channel with the spread of its samples; an image is read once however
    /// often it is asked for. An image that is there and was not measured is named among what the report could not
    /// state.
    ///
    /// @param path the image.
    /// @param what the image as a reader meets it: "the master Luminance", "the master dark 120 s".
    [[nodiscard]] std::optional<ReportImageMeasurement> measure(const QString& path, const QString& what);

    /// The figures of `measure`, without the spread.
    [[nodiscard]] std::optional<ReportImageStatistics> statistics(const QString& path, const QString& what);

    /// One read of an image, measured for its level and its noise alone; read once however often it is asked for,
    /// and named among what the report could not state when it is there and was not measured.
    [[nodiscard]] std::optional<ReportImageLevels> levels(const QString& path, const QString& what);

    /// A file's header, read once however often it is asked for; a file that is not there is not probed.
    [[nodiscard]] std::optional<ReportHeader> header(const QString& path);

    /// The offset from UTC of the zone the archive states its times in, in seconds: the archive's own, else the
    /// host's.
    [[nodiscard]] int utcOffsetSeconds() const;

    /// The light a registration group's lights were brought to, by its canonical path; empty when the registration
    /// stored none.
    [[nodiscard]] QString referenceFrame(const QString& registrationGroupKey);

    /// The earlier runs of the output folder whose archives keep their report models, latest first; empty for a run
    /// with no archive. An earlier run is a folder beside this run's whose name sorts before it: the run being
    /// written is never among them, and neither is a run that started after it.
    [[nodiscard]] const std::vector<StoredRun>& earlierRuns();

    /// Every earlier run of the output folder, latest first, each with its place among `earlierRuns()` or why its
    /// archive gave no report model. Read through the stored reports alone: no image, no manifest and no metrics
    /// database of an earlier run is opened.
    [[nodiscard]] const std::vector<EarlierArchive>& earlierArchives();

    /// The folder the runs of the output folder are archived in: this run's folder's parent. Empty for a run with
    /// no archive.
    [[nodiscard]] QString historyDirectory() const;

    /// States something the report could not state, and why; the same thing for the same reason is stated once.
    void notStated(const QString& what, const QString& why);

    /// What the build could not state, in the order it met each thing.
    [[nodiscard]] const std::vector<ReportNotStated>& notStatedEntries() const noexcept
    {
        return m_notStated;
    }

    /// Whether the run has registration metrics to read.
    [[nodiscard]] bool hasMetrics() const;

    /// The latest settings version a registration group has metrics under.
    [[nodiscard]] std::optional<int> settingsVersion(const QString& registrationGroupKey);

    /// A registration group's metrics at its latest settings version, by each light's canonical path; empty when the
    /// run recorded none.
    [[nodiscard]] const QHash<QString, RegistrationMetricRow>& metrics(const QString& registrationGroupKey);

private:
    /// Names an image that was not measured among what the report could not state, with the reason.
    void imageNotStated(const QString& subject, const QString& file, bool there);

    const ReportModelInputs& m_inputs;
    const ReportSources& m_sources;
    ReportSlugs m_slugs;
    QHash<QString, std::optional<ReportImageMeasurement>> m_measured;
    QHash<QString, std::optional<ReportImageLevels>> m_levels;
    QHash<QString, QString> m_references;
    std::optional<std::vector<StoredRun>> m_earlierRuns;
    std::vector<EarlierArchive> m_earlierArchives;
    std::vector<ReportNotStated> m_notStated;
    QHash<QString, std::optional<ReportHeader>> m_headers;
    QHash<QString, std::optional<int>> m_settingsVersions;
    QHash<QString, QHash<QString, RegistrationMetricRow>> m_metrics;
};

/// The masters the run planned: what became of each, its file, what one read of it measured, what its stack
/// recorded, its maps and its pictures.
void fillMasters(ReportBuild& build, ReportModel& model);

/// Each master's lights, with what the registration recorded of them and how the stack weighed them.
void fillFrames(ReportBuild& build, ReportModel& model);

/// The calibration masters, and the groups of lights the user marked as calibrated already.
void fillCalibration(ReportBuild& build, ReportModel& model);

/// The registration groups: the reference, the transform, the corrections, the comet fits, the catalog handoff.
void fillRegistration(ReportBuild& build, ReportModel& model);

/// The settings of every group, the ones stated once for the run, and what was left to the application.
void fillSettings(ReportBuild& build, ReportModel& model);

/// The run as a whole: how it ended, its totals, its stages and timings, its nights, its equipment, its provenance.
void fillRun(ReportBuild& build, ReportModel& model);

/// The run beside the earlier runs of its folder, as their stored reports state them: each master the run made
/// beside the latest earlier run that made it, the run's totals and what changed against the latest earlier run that
/// completed, and the runs of the folder. Reads the masters and the run the model already states.
void fillComparison(ReportBuild& build, ReportModel& model);

/// The groups of lights that share a calibration, and the lineage between a master, its groups of lights, the
/// calibration masters and the registration group. Reads the masters, the calibration masters and the registration
/// groups the model already states.
void fillLineage(ReportBuild& build, ReportModel& model);

/// The run's issues: the catalog's kinds computed from the model's facts, then every warning and error of the run in
/// the run's own words. Settles the run's outcome by the issues' severities. Reads the whole model.
void fillIssues(ReportBuild& build, ReportModel& model);

/// The sum of what the run wrote to disk, by kind. Reads the masters and the calibration masters.
void fillDisk(ReportBuild& build, ReportModel& model);

// ---- The run: exposures, outcomes, targets, inputs, provenance.

/// The lights of a group that share an exposure, as the exposure breakdown counts them.
struct ReportExposureBucket
{
    double exposureSeconds = 0.0;
    int frameCount = 0;
};

/// The exposures of a group's lights.
struct ExposureBreakdown
{
    int totalFrames = 0;
    int knownFrames = 0;
    int unknownFrames = 0;
    int totalMs = 0;
    QList<ReportExposureBucket> buckets;
};

/// The run's input files by frame type, each by its canonical path.
struct InputFileSets
{
    QSet<QString> biasFiles;
    QSet<QString> darkFiles;
    QSet<QString> flatFiles;
    QSet<QString> lightFiles;

    [[nodiscard]] bool isEmpty() const
    {
        return lightFiles.isEmpty() && biasFiles.isEmpty() && darkFiles.isEmpty() && flatFiles.isEmpty();
    }
};

[[nodiscard]] std::optional<double> extractSensorTemperature(const QString& filePath);
[[nodiscard]] std::optional<double> extractExposureFromFilename(const QString& filePath);
[[nodiscard]] std::optional<double> parseGroupExposure(const WorkflowManifest::IntegrationGroup& group);
void addExposureBucket(QList<ReportExposureBucket>& buckets, double exposureSeconds, int frameCount);
[[nodiscard]] ExposureBreakdown buildExposureBreakdown(const WorkflowManifest::IntegrationGroup& group);
[[nodiscard]] QString stageDisplayName(QString stageName);
[[nodiscard]] InputFileSets collectInputFileSets(const WorkflowManifest& manifest);
[[nodiscard]] QString sha256Hex(const QByteArray& bytes);
[[nodiscard]] QString resolveTargetName(const WorkflowManifest::IntegrationGroup& group);
[[nodiscard]] QString canonicalOrRawPath(const FrameId& id);
[[nodiscard]] QStringList collectTargets(const WorkflowManifest& manifest);
[[nodiscard]] QString normalizeMetadataText(const QString& rawText);
[[nodiscard]] QString normalizeMetadataText(const std::string& rawText);
[[nodiscard]] QString unquotedCardText(const std::string& value);
[[nodiscard]] QString readFirstFitsValue(const an::ImageMetadata& metadata, const QStringList& keys);
[[nodiscard]] QString metadataObjectName(const an::ImageMetadata& metadata);
[[nodiscard]] QString metadataCameraModel(const an::ImageMetadata& metadata);
[[nodiscard]] bool metadataStatesExposure(const an::ImageMetadata& metadata);
[[nodiscard]] QString metadataTelescope(const an::ImageMetadata& metadata);
[[nodiscard]] QString formatIsoGainLabel(const an::ImageMetadata& metadata);
[[nodiscard]] QString formatOffsetLabel(const an::ImageMetadata& metadata);
[[nodiscard]] QString formatLocationFromFits(const an::ImageMetadata& metadata);
[[nodiscard]] QString resolveAppVersion(const QJsonObject& context);
[[nodiscard]] QString resolveContextValue(const QJsonObject& context, const QString& key,
                                          const QString& fallback = QString());
[[nodiscard]] QString reportProcessLabel(const WorkflowManifest& manifest);
[[nodiscard]] QString reportTitle(const WorkflowManifest& manifest);

/// The fingerprint of a run: a hash of the application's version, of the manifest's settings and of its input
/// files, the same for two runs that would do the same work on the same files.
///
/// @param manifest the run's manifest.
/// @param context the archive's context, which states the application's version.
/// @param manifestSettingsFingerprint receives the settings' fingerprint, when not null.
/// @param inputFingerprint receives the inputs' fingerprint, when not null.
/// @return the composite fingerprint.
[[nodiscard]] QString computeReproducibilityFingerprint(const WorkflowManifest& manifest, const QJsonObject& context,
                                                        QString* manifestSettingsFingerprint,
                                                        QString* inputFingerprint);

/// An exposure breakdown as the model holds it.
[[nodiscard]] ReportExposure reportExposure(const ExposureBreakdown& breakdown);

/// A run's outcome in one sentence, from what became of its masters.
[[nodiscard]] QString reportVerdict(ReportOutcome outcome, const ReportGroupCounts& groups);

// ---- The masters.

/// The figures one measurement of an image states, under the names the units that read them have always used.
using RawImageStats = ReportImageStatistics;
using RawChannelStats = ReportChannelStatistics;

[[nodiscard]] RejectionClusterMapKind rejectionMapKindForTitle(const QString& title);
[[nodiscard]] std::optional<QPair<int, int>> parseResolutionDimensions(const QString& resolution);
[[nodiscard]] QString resolvePathAgainstBase(const QString& candidatePath, const QString& baseDir);

/// The mean over an image's channels of one figure, the channels that do not state a finite one left out.
template <typename ValueFn>
[[nodiscard]] std::optional<double> averageChannelStat(const std::vector<RawChannelStats>& stats, ValueFn&& valueFn)
{
    if (stats.empty())
    {
        return std::nullopt;
    }

    double sum = 0.0;
    int count = 0;
    for (const auto& channel : stats)
    {
        const double v = valueFn(channel);
        if (!std::isfinite(v))
        {
            continue;
        }
        sum += v;
        ++count;
    }

    if (count <= 0)
    {
        return std::nullopt;
    }
    return sum / static_cast<double>(count);
}

/// The share of a capture's area an output of the given size covers, as a percentage; nothing when the resolution
/// is not a size or either area is empty.
[[nodiscard]] std::optional<double> overlapRetainedPercent(const QString& captureResolution, std::size_t width,
                                                           std::size_t height);

// ---- The registration.

/// What a transform left on Auto resolved to, and why.
struct AutoTransformResolution
{
    QString detail;
    QString reason;
};

[[nodiscard]] bool isAutoToken(const QString& value);
[[nodiscard]] QString registrationBaseTransformDisplayName(const RegistrationSettings& settings);
[[nodiscard]] QString transformKindDisplayName(const QString& kind);
[[nodiscard]] bool isPolynomialTransformKind(const QString& kind);
[[nodiscard]] AutoTransformResolution resolveAutoTransformDetail(const RegistrationSettings& settings,
                                                                 const QSet<QString>& metricKinds);
[[nodiscard]] AutoTransformResolution
resolveAutoTransformDetail(const WorkflowManifest::RegistrationGroup& registrationGroup, const QString& metricsDbPath,
                           WorkflowExecutionCounterAccumulator* counters);
[[nodiscard]] QString registrationTransformDisplayName(const RegistrationSettings& settings,
                                                       const QString& resolvedAutoDetail = QString());
[[nodiscard]] QSet<QString> successfulTransformKinds(const QHash<QString, RegistrationMetricRow>& metricsByFrame);

// ---- The settings.

[[nodiscard]] QString interpolationDisplayText(const QString& interpolation);
[[nodiscard]] QString cfaPatternDisplayText(const QString& cfaPattern);
[[nodiscard]] QString debayerMethodDisplayText(const QString& debayerMethod);
[[nodiscard]] QString normalizationModeText(NormalizationMode mode);
[[nodiscard]] QString globalNormalizationMethodText(GlobalNormalizationMethod method);
[[nodiscard]] QString scaleEstimatorText(ScaleEstimatorMethod method);
[[nodiscard]] QString rejectionMethodText(const StackingSettings& settings);
[[nodiscard]] QString frameLabel(const FrameId& id);

/// The settings a group of lights is calibrated under, the ones of a master the group does not take left out.
///
/// @param settings the settings the group's lights share.
/// @param takesDark whether the lights take a dark master.
/// @param takesFlat whether the lights take a flat master.
[[nodiscard]] ReportSettingsBlock lightsCalibrationBlock(const WorkflowManifest::CalibrationSettings& settings,
                                                         bool takesDark, bool takesFlat);

// ---- The lights.

/// One registered light of a group, as its weight is derived.
struct QualityWeightSample
{
    QString frameId;
    double exposureSeconds = 0.0;
    FrameQualityMetrics metrics;
    /// The background noise and scales the registration recorded, read for a group weighted by noise.
    FrameNoiseStatistics noise;
};

[[nodiscard]] bool showsWeightFloor(const QString& weighting);
[[nodiscard]] std::optional<double> medianOf(QList<double> values);
[[nodiscard]] double resolveLightExposureSeconds(const WorkflowManifest::IntegrationGroup& group, int index);
[[nodiscard]] QList<double> computeWeightsForSamples(const an::consumer::EngineSession* session,
                                                     const QList<QualityWeightSample>& samples,
                                                     const WorkflowManifest::IntegrationGroup& group);
[[nodiscard]] QList<double> recordedWeightsForSamples(const QList<QualityWeightSample>& samples,
                                                      const StackQualityRecord& quality,
                                                      const WorkflowManifest::IntegrationGroup& group);

// ---- The rollups.

/// The mean of the values that are finite numbers; nothing when there is none.
[[nodiscard]] std::optional<double> averageFiniteValues(const QList<double>& values);

// ---- The sources' own readers, which the product's sources call.

[[nodiscard]] std::optional<QString> transformKindFromJson(const QString& json);
[[nodiscard]] std::optional<ReportImageMeasurement> measureReportImage(const an::consumer::EngineSession* session,
                                                                       const QString& path, std::uint32_t width);
[[nodiscard]] std::optional<ReportImageLevels> measureReportImageLevels(const an::consumer::EngineSession* session,
                                                                        const QString& path, std::uint32_t width);
[[nodiscard]] QString queryRegistrationReferenceFrame(QSqlDatabase& db, const QString& groupKey, int settingsVersion,
                                                      WorkflowExecutionCounterAccumulator* counters = nullptr);
[[nodiscard]] std::optional<ReportHeader> probeReportHeader(const an::consumer::EngineSession* session,
                                                            const QString& path);
[[nodiscard]] std::optional<int>
queryRegistrationSettingsVersion(QSqlDatabase& db, const QString& groupKey,
                                 WorkflowExecutionCounterAccumulator* counters = nullptr);
[[nodiscard]] QHash<QString, RegistrationMetricRow>
loadRegistrationMetricsByFrame(QSqlDatabase& db, const QString& groupKey, int settingsVersion,
                               WorkflowExecutionCounterAccumulator* counters = nullptr);
[[nodiscard]] QHash<QString, RegistrationMetricRow>
loadRegistrationMetricsForGroup(const QString& metricsDbPath, const QString& groupKey,
                                WorkflowExecutionCounterAccumulator* counters);
[[nodiscard]] QHash<QString, FrameNoiseStatistics>
loadRegistrationNoiseByFrame(QSqlDatabase& db, const QString& groupKey, int settingsVersion,
                             WorkflowExecutionCounterAccumulator* counters = nullptr);
[[nodiscard]] QList<CometReportRun> loadCometReportRuns(const QString& metricsDbPath, const WorkflowManifest& manifest,
                                                        WorkflowExecutionCounterAccumulator* counters);
[[nodiscard]] QList<SpccHandoffGroupSummary> loadSpccHandoffSummaries(QSqlDatabase& db,
                                                                      const WorkflowManifest& manifest,
                                                                      WorkflowExecutionCounterAccumulator* counters);
} // namespace ImageIntegration::Reporting
