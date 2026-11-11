// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The report model: everything a run's report states, as plain structures.
///
/// A report is written from one model. `buildReportModel` fills it once from the run's manifest, its result and the
/// records the run left behind; a document is then written from it, the archive keeps it as `workflow_report.json`,
/// and a suite asserts on it. Nothing here computes: a structure holds what was found, and the words that name a
/// group or a setting, already as a reader meets them.
///
/// A value that may not be stated is a `std::optional` or an empty list, never a zero: a master that was not
/// measured has no statistics, a run that collected no counters lists none. Durations are milliseconds, sizes are
/// bytes, fractions are fractions of one, and a path is stored as the run recorded it.
///
/// Settings belong to groups, as the run's manifest holds them: each calibration master, each registration group and
/// each master carries its own, and only the execution's are stated once for the run.

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ImageIntegration::Reporting
{
/// The format `workflow_report.json` is written in. A reader takes a document of this number and refuses any other
/// by name: no earlier format is read.
inline constexpr int kReportSchema = 1;

/// How a run ended.
enum class ReportOutcome
{
    /// Every master was made or was still valid, and nothing needs attention.
    Completed,

    /// The run completed and stated at least one warning.
    CompletedWithWarnings,

    /// Some masters were made and at least one was not.
    Partial,

    /// The run failed and made no master.
    Failed,

    /// The user stopped the run.
    Cancelled
};

/// How serious an issue is.
enum class ReportIssueSeverity
{
    Critical,
    Warning,
    Note
};

/// What an issue is about. Each kind is computed from the run's records; `RunMessage` carries a warning or an error
/// of the run in the run's own words.
enum class ReportIssueKind
{
    RunFailed,
    RunCancelled,
    MasterFailed,
    MasterNotReached,
    OutputNotRetained,
    LightsWithoutFlat,
    LightsWithoutDark,
    NoFlats,
    NoDarks,
    FlatMasterUncalibrated,
    CosmeticCorrectionWithoutDark,
    RegistrationFailures,
    MasterWithoutAstrometry,
    LowFrameUse,
    RegistrationSuccessLow,
    LightsLeftOutByStack,
    ExposureMetadataIncomplete,
    StackedTwice,
    MasterReused,
    CalibrationMasterReused,
    PreCalibratedLights,
    OutputNotRetainedByPolicy,
    RunMessage
};

/// What a master is made of.
enum class ReportMasterKind
{
    /// The stack of one group of lights.
    Stack,

    /// The colour image combined from a registration group's red, green and blue stacks.
    CombinedRgb
};

/// What became of a master the run planned.
enum class ReportMasterState
{
    /// The run made it.
    Made,

    /// Its output was still valid, so the run made none: the run's record of the groups it reused names it.
    Reused,

    /// The run tried and failed.
    Failed,

    /// The run failed before it reached the master.
    NotReached,

    /// The run was cancelled before it reached the master.
    Cancelled
};

/// Whether a master's file has a copy in the run's archive.
enum class ReportRetention
{
    /// The archive does not say.
    Unknown,

    /// The archive holds a copy.
    Retained,

    /// The history's limits left no room for a copy.
    NotRetainedByPolicy,

    /// The copy could not be written.
    NotRetainedByArchiveFailure
};

/// Where a master's astrometric solution came from.
enum class ReportAstrometryOutcome
{
    /// The run recorded nothing about it.
    NotRecorded,

    /// Carried from the registration reference; the master itself was not plate-solved.
    Inherited,

    /// The master was plate-solved after stacking.
    Solved,

    /// The master was written without a solution.
    None
};

/// What became of one light.
enum class ReportFrameState
{
    Used,
    ExcludedByUser,
    StackExcludedByUser,
    RegistrationFailed,
    LeftOutByStack,
    ExcludedOnRestack,
    NotStacked
};

/// The kind of a calibration master.
enum class ReportCalibrationKind
{
    Bias,
    Dark,
    Flat
};

/// What became of a calibration master.
enum class ReportCalibrationState
{
    /// The run stacked it.
    Made,

    /// It was still valid, so the run stacked none.
    Reused,

    /// The user supplied the file.
    Supplied,

    /// The run tried and failed.
    Failed
};

/// What became of a stage.
enum class ReportStageState
{
    /// The stage ran to its end.
    Ran,

    /// The stage ran and some of its work failed.
    Partial,

    /// Everything the stage makes was still valid.
    Reused,

    /// The stage failed.
    Failed,

    /// The run was cancelled inside the stage.
    Cancelled,

    /// The run stopped before the stage.
    NotReached,

    /// The run had no work for the stage.
    Skipped
};

/// What a map of a master shows.
enum class ReportMapKind
{
    /// The samples the stack rejected.
    Rejection,

    /// A figure of the stack per pixel: coverage, variance, gain, the sum of the weights.
    Quality
};

/// Which rejection map a cluster of rejected samples was found in.
enum class ReportClusterMap
{
    Merged,
    Low,
    High
};

/// One row of a settings block: a setting, its value as a reader meets it and, for a setting left to the
/// application, what was asked for and why it resolved so.
struct ReportSettingsRow
{
    /// The setting's name.
    QString name;

    /// Its value in effect.
    QString value;

    /// What was asked for, when the value was left to the application; empty otherwise.
    QString requested;

    /// Why the value resolved so; empty when the setting was stated.
    QString reason;

    friend bool operator==(const ReportSettingsRow&, const ReportSettingsRow&) = default;
};

/// The settings of one step of one group.
struct ReportSettingsBlock
{
    /// What the settings are of: "Stacking", "Drizzle", "Registration".
    QString title;

    std::vector<ReportSettingsRow> rows;

    friend bool operator==(const ReportSettingsBlock&, const ReportSettingsBlock&) = default;
};

/// A setting the application decided, listed once for the run.
struct ReportDecision
{
    /// What was decided: "Transform Model", "Stacking Rejection".
    QString area;

    /// The key of the group it was decided for.
    QString groupKey;

    /// That group's name.
    QString group;

    /// What was asked for.
    QString requested;

    /// What it resolved to.
    QString resolved;

    /// Why.
    QString reason;

    friend bool operator==(const ReportDecision&, const ReportDecision&) = default;
};

/// The lights of a group that share an exposure.
struct ReportExposureBin
{
    /// The exposure in seconds.
    double seconds = 0.0;

    /// How many lights have it.
    int frames = 0;

    friend bool operator==(const ReportExposureBin&, const ReportExposureBin&) = default;
};

/// The exposures of a set of lights.
struct ReportExposure
{
    /// How many lights the set holds.
    int frames = 0;

    /// How many of them state an exposure.
    int framesWithExposure = 0;

    /// The sum of the stated exposures.
    qint64 integrationMs = 0;

    /// The stated exposures, shortest first.
    std::vector<ReportExposureBin> bins;

    friend bool operator==(const ReportExposure&, const ReportExposure&) = default;
};

/// One channel's figures of an image, each a fraction of the representation's display range, over the samples the
/// image's validity admits that lie strictly inside the range.
struct ReportChannelStatistics
{
    /// How many samples the channel has: one per pixel.
    std::uint64_t samples = 0;

    /// How many samples the figures are over: those the image's validity admits, less the clipped ones.
    std::uint64_t unclippedSamples = 0;

    /// The smallest sample.
    double minimum = 0.0;

    /// The largest sample.
    double maximum = 0.0;

    /// The arithmetic mean.
    double mean = 0.0;

    /// The median.
    double median = 0.0;

    /// The population standard deviation.
    double standardDeviation = 0.0;

    /// The mean absolute distance from the mean.
    double averageDeviation = 0.0;

    /// The population variance.
    double variance = 0.0;

    /// The median absolute deviation from the median, unscaled.
    double medianAbsoluteDeviation = 0.0;

    /// The background noise taken from each row's first differences.
    double backgroundNoise = 0.0;

    /// The biweight midvariance about the median.
    double biweightMidvariance = 0.0;

    /// The mean of the squared samples: the variance plus the square of the mean.
    double meanOfSquares = 0.0;

    /// The sum of the squared samples: the mean of squares times the samples the figures are over.
    double sumOfSquares = 0.0;

    /// The iterative k-sigma location, or nothing when the measurement could not state it.
    std::optional<double> ikssLocation;

    /// The iterative k-sigma scale, or nothing when the measurement could not state it.
    std::optional<double> ikssScale;

    friend bool operator==(const ReportChannelStatistics&, const ReportChannelStatistics&) = default;
};

/// What one read of an image measured.
struct ReportImageStatistics
{
    /// The image's width in pixels.
    std::size_t width = 0;

    /// The image's height in pixels.
    std::size_t height = 0;

    /// How many channels the image has.
    std::size_t channels = 0;

    /// Each channel's figures, in the image's channel order.
    std::vector<ReportChannelStatistics> channelStats;

    friend bool operator==(const ReportImageStatistics&, const ReportImageStatistics&) = default;
};

/// How the samples of one channel are spread: counts over bins that divide the display range on a logarithmic axis.
struct ReportSampleDistribution
{
    /// The lower edge of the first bin, as a fraction of the display range.
    double lowest = 0.0;

    /// The upper edge of the last bin.
    double highest = 0.0;

    /// The samples in each bin, the samples under `lowest` counted in the first. The bins are of equal width on a
    /// logarithmic axis: bin `i` of `n` starts at `lowest * (highest / lowest)^(i / n)`.
    std::vector<std::uint64_t> counts;

    /// The samples above `highest` or not a number, which are in no bin. The bins and these add up to the samples the
    /// image's validity admits.
    std::uint64_t outside = 0;

    friend bool operator==(const ReportSampleDistribution&, const ReportSampleDistribution&) = default;
};

/// A phase of a master's finalization, or of the run's own work, with its time.
struct ReportPhase
{
    /// The phase's name.
    QString name;

    /// How it ended, as the run wrote it: `completed`, `skipped`, `failed`.
    QString status;

    qint64 durationMs = 0;

    friend bool operator==(const ReportPhase&, const ReportPhase&) = default;
};

/// A cluster of rejected samples a stack's spatial analysis found.
struct ReportRejectionCluster
{
    ReportClusterMap map = ReportClusterMap::Merged;
    std::uint64_t channel = 0;
    std::uint64_t pixels = 0;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    double centroidX = 0.0;
    double centroidY = 0.0;

    friend bool operator==(const ReportRejectionCluster&, const ReportRejectionCluster&) = default;
};

/// What a stack rejected, over the whole master and per light.
struct ReportStackRejection
{
    /// The share of all samples that were rejected.
    double global = 0.0;

    /// The smallest, mean, median and largest share over the lights the stack combined.
    double lowestFrame = 0.0;
    double meanFrame = 0.0;
    double medianFrame = 0.0;
    double highestFrame = 0.0;

    friend bool operator==(const ReportStackRejection&, const ReportStackRejection&) = default;
};

/// How a stack's weights were spread over its lights, at a mean of one.
struct ReportStackWeights
{
    double lowest = 0.0;
    double highest = 0.0;
    double deviation = 0.0;

    /// The effective share of the lights the weights leave: one when every light weighs the same.
    double efficiency = 0.0;

    friend bool operator==(const ReportStackWeights&, const ReportStackWeights&) = default;
};

/// The weights a stack gave each pixel of each light.
struct ReportLocalWeights
{
    int windowSize = 0;
    int gridSpacing = 0;
    double snrExponent = 0.0;

    /// How many lights have a grid.
    std::uint64_t grids = 0;

    double lowest = 0.0;
    double highest = 0.0;
    double mean = 0.0;
    double deviation = 0.0;

    friend bool operator==(const ReportLocalWeights&, const ReportLocalWeights&) = default;
};

/// The clusters of rejected samples a stack found.
struct ReportStackClusters
{
    std::uint64_t count = 0;

    /// The pixels inside a cluster, and their share of the master.
    std::uint64_t pixels = 0;
    double fraction = 0.0;

    /// The largest cluster's pixels and their share.
    std::uint64_t largestPixels = 0;
    double largestFraction = 0.0;

    /// The elements either rejection map reached at all, clustered or not, and their share.
    std::uint64_t rejectedElements = 0;
    double rejectedElementFraction = 0.0;

    /// The clusters, largest first.
    std::vector<ReportRejectionCluster> clusters;

    friend bool operator==(const ReportStackClusters&, const ReportStackClusters&) = default;
};

/// A light a stack flagged and combined its master without.
struct ReportRestackExclusion
{
    /// The light's place among the group's lights.
    std::uint64_t index = 0;

    /// Its file name; empty when the group has no light at that place.
    QString fileName;

    /// The share of its samples the first stack rejected, when recorded.
    std::optional<double> rejectedFraction;

    friend bool operator==(const ReportRestackExclusion&, const ReportRestackExclusion&) = default;
};

/// A second stack made without the lights the first one flagged.
struct ReportRestack
{
    /// Whether the second stack was made.
    bool applied = false;

    /// The share of rejected samples from which a light is flagged.
    double flaggedFrameThreshold = 0.0;

    /// How many clusters the first stack found.
    std::uint64_t initialClusters = 0;

    std::vector<ReportRestackExclusion> excluded;

    friend bool operator==(const ReportRestack&, const ReportRestack&) = default;
};

/// The lights that reached a pixel of a stack: the fewest, the median, the most, and their deviation.
struct ReportStackCoverage
{
    double lowest = 0.0;
    double median = 0.0;
    double highest = 0.0;
    double deviation = 0.0;

    friend bool operator==(const ReportStackCoverage&, const ReportStackCoverage&) = default;
};

/// What a stack recorded of itself.
struct ReportStack
{
    /// The lights the stack combined.
    std::uint64_t inputFrames = 0;

    /// How many lights of equal weight and no rejection would give the same noise. Nothing when the stack kept
    /// nothing to derive it from: no gain map, no weights and no count of the lights that reached a pixel.
    std::optional<double> effectiveFrames;

    /// The gain in signal to noise the stack measured, stated with the effective frames it is the root of, and the
    /// gain its light count promises.
    std::optional<double> measuredSnrGain;
    double theoreticalSnrGain = 0.0;

    /// The measured gain over the theoretical one; stated with the measured gain.
    std::optional<double> efficiency;

    /// The lights that reached a pixel; nothing when the stack counted none.
    std::optional<ReportStackCoverage> coverage;

    /// What brought the lights to the stack's reference; empty when the stack normalized nothing.
    QString normalization;

    std::optional<ReportStackRejection> rejection;
    std::optional<ReportStackWeights> weights;
    std::optional<ReportLocalWeights> localWeights;
    std::optional<ReportStackClusters> clusters;
    std::optional<ReportRestack> restack;

    friend bool operator==(const ReportStack&, const ReportStack&) = default;
};

/// A map written beside a master.
struct ReportMap
{
    ReportMapKind kind = ReportMapKind::Rejection;

    /// The map's name: "Rejection Map (Low)", "Coverage Map".
    QString title;

    /// The map's file, as the run recorded it.
    QString file;

    /// Its size, when the file is there.
    std::optional<qint64> bytes;

    /// The picture of the map, relative to the run's folder; empty when there is none.
    QString preview;

    /// How many clusters of rejected samples are marked on the picture.
    int clusters = 0;

    /// Whether the map is the sum of the weights of a stack weighted per pixel, which is shown beside the weights.
    bool weightSum = false;

    friend bool operator==(const ReportMap&, const ReportMap&) = default;
};

/// The headline figures of a master, taken over its channels.
struct ReportMasterSnapshot
{
    /// The mean level.
    std::optional<double> mean;

    /// The background noise.
    std::optional<double> backgroundNoise;

    /// The mean over the background noise.
    std::optional<double> snr;

    /// The largest sample less the smallest.
    std::optional<double> dynamicRange;

    friend bool operator==(const ReportMasterSnapshot&, const ReportMasterSnapshot&) = default;
};

/// How a master's lights were weighed, over the lights whose registration recorded metrics.
struct ReportMasterWeighting
{
    /// The weighting the stack applied: "None", "Noise", "Photometric".
    QString mode;

    /// The least weight the group's settings allow, relative to the heaviest light.
    double minimumWeight = 0.0;

    /// Whether the weights are shown against that floor.
    bool floorApplies = false;

    /// The lights with a registration record, and those whose registration succeeded.
    int framesWithMetrics = 0;
    int framesRegistered = 0;

    /// The registered lights that weigh no more than the floor.
    int framesAtFloor = 0;

    /// The sum over the registered lights of exposure times weight.
    qint64 weightedIntegrationMs = 0;

    std::optional<double> medianWeight;
    std::optional<double> medianQualityScore;
    std::optional<double> medianZeroPoint;
    std::optional<double> medianAstrometricRmsArcsec;
    std::optional<double> medianCatalogMatches;

    friend bool operator==(const ReportMasterWeighting&, const ReportMasterWeighting&) = default;
};

/// One light of a master.
struct ReportFrame
{
    /// The light's file name and its path as the run recorded it.
    QString fileName;
    QString path;

    /// What became of the light, and the reason where the state has one.
    std::optional<ReportFrameState> state;
    QString stateDetail;

    /// The night the light was taken on, and when.
    std::optional<QDate> night;
    std::optional<QDateTime> capturedAt;

    /// The exposure in seconds.
    std::optional<double> exposureSeconds;

    /// Whether the light's registration succeeded, when the run has a record of it.
    std::optional<bool> registered;

    std::optional<int> stars;
    std::optional<double> fwhm;
    std::optional<double> eccentricity;
    std::optional<double> snr;
    std::optional<double> backgroundNoise;
    std::optional<double> qualityScore;
    std::optional<double> zeroPoint;
    std::optional<double> zeroPointScatter;
    std::optional<double> astrometricRmsPixels;
    std::optional<double> astrometricRmsArcsec;
    std::optional<double> limitingMagnitude;
    std::optional<int> catalogMatches;

    /// How far the light sat from the reference, in pixels: where the transform that brought it there puts the
    /// frame's centre, less the centre. Not the transform's own translation, which is where the frame's corner
    /// goes and is about the frame's size for a light turned half a turn, whatever its dither.
    std::optional<double> shiftX;
    std::optional<double> shiftY;

    /// How far the light is turned against the reference, in degrees between -180 and 180. A light taken after a
    /// meridian flip is turned half a turn.
    std::optional<double> rotationDegrees;

    /// The kind of the transform.
    QString transformKind;

    /// The normalization's scale and offset, per channel.
    std::vector<double> normalizationScale;
    std::vector<double> normalizationOffset;

    /// The light's weight, relative to the heaviest light; a photometric weight is at a mean of one.
    std::optional<double> weight;

    /// The light's share of the stack: its exposure times its weight over the group's.
    std::optional<double> stackShare;

    /// The share of the light's samples the stack rejected.
    std::optional<double> rejectedFraction;

    /// What the light's header states.
    QString gain;
    QString offset;
    std::optional<double> temperature;

    friend bool operator==(const ReportFrame&, const ReportFrame&) = default;
};

/// A master as an earlier run made it: the figures a comparison sets beside this run's, as that run's stored report
/// states them. A master no earlier run of the folder made is new, and states none.
struct ReportBaseline
{
    /// The latest earlier run that made the master, and when that run was archived; empty for a master that is new.
    QString runId;
    std::optional<QDateTime> archivedAt;

    /// Whether an earlier run made this master at all.
    bool made = false;

    /// The lights the stack combined and the lights the group took.
    std::optional<int> frames;
    std::optional<int> framesTaken;

    /// The exposure of the lights the stack combined.
    std::optional<qint64> integrationMs;

    std::optional<double> backgroundNoise;
    std::optional<double> snr;

    /// The median star width of the lights the stack combined, in pixels.
    std::optional<double> fwhm;

    std::optional<double> effectiveFrames;

    /// The share of all samples the stack rejected.
    std::optional<double> rejectedFraction;

    /// Whether the earlier master's frame is of another shape than this run's - their aspect ratios differ by more
    /// than one percent - so that the two pictures stand side by side and not under one divider.
    bool sideBySide = false;

    /// The earlier run's pictures, whole and at full scale, as copies in this run's folder; empty where that run's
    /// archive holds none.
    QString preview;
    QString detail;

    friend bool operator==(const ReportBaseline&, const ReportBaseline&) = default;
};

/// What a master's astrometric solution states of the frame, read from the master's header.
struct ReportAstrometrySolution
{
    /// Where the centre of the frame points, in degrees.
    double centreRightAscension = 0.0;
    double centreDeclination = 0.0;

    /// The mean scale in arcseconds per pixel.
    double pixelScale = 0.0;

    /// The field's width and height in arcminutes.
    double fieldWidth = 0.0;
    double fieldHeight = 0.0;

    /// The solution's orientation in degrees, as the engine's solution states it.
    double orientation = 0.0;

    /// Whether the frame is mirrored against the sky.
    bool mirrored = false;

    friend bool operator==(const ReportAstrometrySolution&, const ReportAstrometrySolution&) = default;
};

/// What the registration recorded of the photometry of the lights a master combines.
struct ReportMasterPhotometry
{
    /// The combined lights that state a zero point.
    int frames = 0;

    std::optional<double> medianZeroPoint;
    std::optional<double> medianZeroPointScatter;
    std::optional<double> medianLimitingMagnitude;
    std::optional<double> medianCatalogMatches;

    friend bool operator==(const ReportMasterPhotometry&, const ReportMasterPhotometry&) = default;
};

/// The calibration masters a master's lights were calibrated with, by file name.
struct ReportMasterCalibration
{
    QStringList bias;
    QStringList dark;
    QStringList flat;

    friend bool operator==(const ReportMasterCalibration&, const ReportMasterCalibration&) = default;
};

/// One master the run planned.
struct ReportMaster
{
    /// The group's key as the manifest states it, the anchor name made from it, and the name of its folder under
    /// `report_previews/`.
    QString key;
    QString slug;
    QString previewFolder;

    /// The group's name, its target and its filter.
    QString name;
    QString target;
    QString filter;

    ReportMasterKind kind = ReportMasterKind::Stack;
    ReportMasterState state = ReportMasterState::Made;

    /// Why the master was not made; empty for one that was.
    QString failure;

    /// For a reused master, the run that made it; empty when the history does not say.
    QString madeByRun;

    /// The file's name, its path as the run recorded it and its size.
    QString fileName;
    QString file;
    std::optional<qint64> bytes;

    ReportRetention retention = ReportRetention::Unknown;
    QString retentionReason;

    /// The lights the stack combined, as the run counted them.
    std::optional<int> lightsCombined;

    /// The group's lights and their exposures.
    ReportExposure exposure;

    /// The resolution the lights were taken at, as the manifest states it.
    QString captureResolution;

    /// The share of the capture's area the master covers, when both sizes are known.
    std::optional<double> overlapRetained;

    ReportMasterCalibration calibration;

    /// The keys of the groups of lights the master is stacked from, each sharing one calibration.
    QStringList lightGroups;

    /// The key of the registration group the master's lights were registered in, and how the stack took its inputs.
    QString registrationGroupKey;
    QString registrationInputPolicy;

    ReportAstrometryOutcome astrometry = ReportAstrometryOutcome::NotRecorded;
    QString astrometryDetail;

    /// What the solution in the master's header states of the frame, when the header holds one that reads.
    std::optional<ReportAstrometrySolution> solution;

    /// The photometry of the lights the master combines, when the registration recorded any.
    std::optional<ReportMasterPhotometry> photometry;

    /// The stacking and the drizzle in one line each.
    QString stackingSummary;
    QString drizzleSummary;

    /// The time the master took, and the phases of its finalization when they were timed.
    std::optional<qint64> durationMs;
    std::vector<ReportPhase> finalization;

    /// What one read of the master measured, per channel, and how its samples are spread.
    std::optional<ReportImageStatistics> statistics;
    std::vector<ReportSampleDistribution> distribution;
    ReportMasterSnapshot snapshot;

    std::optional<ReportStack> stack;

    /// The master's picture, whole and at full scale, relative to the run's folder; empty where there is none.
    QString preview;
    QString detail;

    std::vector<ReportMap> maps;

    /// How many rejection maps the group's settings ask for.
    std::optional<int> rejectionMapsExpected;

    /// The master's own settings: stacking, normalization, drizzle.
    std::vector<ReportSettingsBlock> settings;

    std::optional<ReportMasterWeighting> weighting;

    /// The group's lights in the manifest's order, then the lights the user left out of the group.
    std::vector<ReportFrame> frames;

    std::optional<ReportBaseline> baseline;

    friend bool operator==(const ReportMaster&, const ReportMaster&) = default;
};

/// A group of lights the run planned no master of, because the user left every one of its lights out.
struct ReportGroupNotStacked
{
    /// The key of the integration group the lights would have joined, and its name.
    QString key;
    QString name;

    /// The lights, each in the state the exclusion that left it out gives it.
    std::vector<ReportFrame> frames;

    friend bool operator==(const ReportGroupNotStacked&, const ReportGroupNotStacked&) = default;
};

/// A calibration master.
struct ReportCalibrationMaster
{
    ReportCalibrationKind kind = ReportCalibrationKind::Bias;

    QString key;
    QString slug;
    QString name;

    /// The master's file.
    QString fileName;
    QString file;

    ReportCalibrationState state = ReportCalibrationState::Made;

    /// The frames it is stacked from.
    int frames = 0;
    QStringList inputFiles;

    /// The exposure its header states, in seconds.
    std::optional<double> exposureSeconds;

    /// The file's size and when it was last changed, when it is there: what tells a later run the file is the same.
    std::optional<qint64> bytes;
    std::optional<QDateTime> modifiedAt;

    /// The masters a flat's frames are calibrated with, by file name, and the anchor of each where the run states it.
    QString biasMaster;
    QString darkMaster;
    QString biasMasterSlug;
    QString darkMasterSlug;

    /// Its stacking in one line, and row by row.
    QString stackingSummary;
    std::vector<ReportSettingsBlock> settings;

    /// The mean level and the noise per channel, when measured.
    std::vector<double> level;
    std::vector<double> noise;

    /// The share of the frame's area the level and the noise were measured over: one for the whole frame, less for
    /// a large master, which is measured over a rectangle at its centre so that a run of many masters stays quick.
    std::optional<double> measuredFraction;

    /// The earlier run whose stored report the level and the noise were taken from; empty when this run measured
    /// them.
    QString measuredByRun;

    /// Its picture, relative to the run's folder; empty where there is none.
    QString preview;

    /// The keys of the masters whose lights it calibrates.
    QStringList usedBy;

    friend bool operator==(const ReportCalibrationMaster&, const ReportCalibrationMaster&) = default;
};

/// A group of lights that share a calibration: the lights of one master that take the same masters under the same
/// settings.
struct ReportLightGroup
{
    /// The group's own key, and its name.
    QString key;
    QString name;

    /// The key of the master the lights are stacked into.
    QString masterKey;

    /// How many lights the group holds.
    int lights = 0;

    /// Whether the user marked the lights as calibrated already, so no master is applied.
    bool preCalibrated = false;

    /// The masters the lights take, by file name, and the anchor of each where the run states it.
    QString biasMaster;
    QString darkMaster;
    QString flatMaster;
    QString biasMasterSlug;
    QString darkMasterSlug;
    QString flatMasterSlug;

    std::vector<ReportSettingsBlock> settings;

    friend bool operator==(const ReportLightGroup&, const ReportLightGroup&) = default;
};

/// One fit of a comet's path through a registration group's lights.
struct ReportCometRun
{
    QString status;
    QString mode;
    QString fitModel;

    std::optional<double> rmsPixels;
    std::optional<double> largestResidualPixels;
    QString warnings;

    /// The lights the fit took, refused and only predicted.
    std::optional<int> inputsAccepted;
    std::optional<int> inputsRejected;
    std::optional<int> inputsPredictedOnly;

    /// The comet's positions by where they came from and how the fit took them.
    int pointsManual = 0;
    int pointsAutomatic = 0;
    int pointsPredictedOnly = 0;
    int pointsOutliers = 0;

    /// Whether the comet-aligned frames are ready to stack, and the transforms behind that.
    QString readiness;
    std::optional<int> transformsSucceeded;
    std::optional<int> transformsFailed;
    std::optional<int> transformsMissing;
    QString readinessFailure;

    /// The plane the comet-aligned frames are written on.
    QString outputPlane;

    friend bool operator==(const ReportCometRun&, const ReportCometRun&) = default;
};

/// The catalog sources a registration group matched and keeps for the colour calibration.
struct ReportCatalogHandoff
{
    int settingsVersion = 0;
    int frames = 0;
    int references = 0;
    int sources = 0;
    int satisfied = 0;
    int missingSource = 0;
    int missingChunkOrCache = 0;
    int manifestMismatch = 0;
    int staleMatch = 0;
    int rejectedQualityFields = 0;
    int untested = 0;
    QString astrometricCatalog;
    QString photometricCatalog;
    std::optional<double> medianResidualPixels;
    std::optional<double> medianSeparationArcsec;

    friend bool operator==(const ReportCatalogHandoff&, const ReportCatalogHandoff&) = default;
};

/// A kind of transform and the lights registered with it.
struct ReportTransformUse
{
    QString kind;

    /// How many lights, when counted.
    std::optional<int> frames;

    friend bool operator==(const ReportTransformUse&, const ReportTransformUse&) = default;
};

/// A registration group.
struct ReportRegistrationGroup
{
    QString key;
    QString slug;
    QString name;
    QString target;

    /// The group's lights, and those whose registration succeeded and failed when the run has a record.
    int lights = 0;
    std::optional<int> registered;
    std::optional<int> failed;

    /// The reference light, and whether the application chose it.
    QString reference;
    bool referenceChosenAutomatically = false;

    /// The frame the lights are aligned to, and the transform model as a reader meets it.
    QString alignmentMode;
    QString transform;

    /// The kinds of transform the registration recorded.
    std::vector<ReportTransformUse> transformsUsed;

    /// The lights aligned, and those corrected for distortion and for chromatic aberration, when the alignment
    /// recorded them.
    std::optional<int> aligned;
    std::optional<int> distortionCorrected;
    std::optional<int> chromaticAberrationCorrected;
    std::optional<int> chromaticAberrationPassedThrough;

    /// The share of the capture's area the group's output plane keeps: the smallest its masters state.
    std::optional<double> overlap;

    std::optional<double> medianAstrometricRmsArcsec;
    std::optional<double> medianCatalogMatches;

    std::vector<ReportCometRun> cometRuns;
    std::optional<ReportCatalogHandoff> catalogHandoff;

    std::vector<ReportSettingsBlock> settings;

    /// The keys of the masters whose lights the group registers.
    QStringList masters;

    friend bool operator==(const ReportRegistrationGroup&, const ReportRegistrationGroup&) = default;
};

/// A stage of the run.
struct ReportStage
{
    /// The stage's name as the run wrote it, and as a reader meets it.
    QString key;
    QString name;

    ReportStageState state = ReportStageState::Ran;

    /// How it ended, as the run wrote it.
    QString status;

    std::optional<qint64> durationMs;

    /// The groups the stage works on.
    int groups = 0;

    QString note;

    friend bool operator==(const ReportStage&, const ReportStage&) = default;
};

/// An issue of the run.
struct ReportIssue
{
    ReportIssueKind kind = ReportIssueKind::RunMessage;
    ReportIssueSeverity severity = ReportIssueSeverity::Warning;
    QString title;
    QString detail;

    /// What to do about it; empty when there is nothing true to say.
    QString action;

    /// The key of the group it concerns, and the anchor of the place that shows it; empty for the run.
    QString groupKey;
    QString anchor;

    friend bool operator==(const ReportIssue&, const ReportIssue&) = default;
};

/// What the report could not state, and why.
struct ReportNotStated
{
    QString what;
    QString why;

    friend bool operator==(const ReportNotStated&, const ReportNotStated&) = default;
};

/// The masters of the run by what became of them.
struct ReportGroupCounts
{
    /// The masters the run planned.
    int total = 0;

    int made = 0;
    int reused = 0;
    int failed = 0;
    int notReached = 0;

    friend bool operator==(const ReportGroupCounts&, const ReportGroupCounts&) = default;
};

/// The time the run's preparation took, before the executor started.
struct ReportPreparationTiming
{
    qint64 manifestBuildMs = 0;
    qint64 planAndValidationMs = 0;

    friend bool operator==(const ReportPreparationTiming&, const ReportPreparationTiming&) = default;
};

/// The executor's own phases, which do not overlap and add up to less than its whole interval.
struct ReportExecutorTiming
{
    qint64 stageExecutionMs = 0;
    qint64 stateCommitMs = 0;
    qint64 archiveMs = 0;
    qint64 reportMs = 0;

    friend bool operator==(const ReportExecutorTiming&, const ReportExecutorTiming&) = default;
};

/// One counter of the run's diagnostics.
struct ReportCounter
{
    /// The counter's name in the stored document, and as a reader meets it.
    QString key;
    QString name;

    std::uint64_t value = 0;

    friend bool operator==(const ReportCounter&, const ReportCounter&) = default;
};

/// What the engine counted of its memory over the run.
struct ReportEngineMemory
{
    std::uint64_t peakBytes = 0;
    std::uint64_t peakBytesBeforeRun = 0;
    std::uint64_t liveBytesAtEnd = 0;
    std::uint64_t allocations = 0;
    std::uint64_t bytesAllocated = 0;
    std::uint64_t deniedAllocations = 0;

    friend bool operator==(const ReportEngineMemory&, const ReportEngineMemory&) = default;
};

/// What the run wrote to disk, of one kind.
struct ReportDiskUse
{
    /// The kind: "Masters", "Registered lights", "The archive".
    QString kind;

    qint64 bytes = 0;
    int files = 0;

    friend bool operator==(const ReportDiskUse&, const ReportDiskUse&) = default;
};

/// One filter's lights of one night.
struct ReportNightFilter
{
    QString filter;
    int frames = 0;
    qint64 integrationMs = 0;

    friend bool operator==(const ReportNightFilter&, const ReportNightFilter&) = default;
};

/// One night of the run's lights.
struct ReportNight
{
    QDate date;
    int frames = 0;
    qint64 integrationMs = 0;
    std::vector<ReportNightFilter> filters;

    friend bool operator==(const ReportNight&, const ReportNight&) = default;
};

/// The lights of one group, as they were taken.
struct ReportAcquisitionRow
{
    /// The key of the group's master, and the group's name, target and filter.
    QString groupKey;
    QString group;
    QString target;
    QString filter;

    ReportExposure exposure;

    /// The group's share of the run's integration time.
    double share = 0.0;

    /// The sensor's temperature over the group's lights, when stated.
    std::optional<double> lowestTemperature;
    std::optional<double> highestTemperature;

    /// The gain and the offset the lights' headers state, each distinct value once.
    QStringList gain;
    QStringList offset;

    friend bool operator==(const ReportAcquisitionRow&, const ReportAcquisitionRow&) = default;
};

/// One set of equipment, as the masters' headers state it, and the masters taken with it.
struct ReportEquipmentRow
{
    /// The names of the masters.
    QStringList masters;

    QString target;
    QString camera;
    QString captureResolution;
    QString outputResolution;
    QString gain;
    QString offset;
    QString telescope;
    QString site;

    friend bool operator==(const ReportEquipmentRow&, const ReportEquipmentRow&) = default;
};

/// How many lights' headers state one field.
struct ReportHeaderField
{
    QString name;
    int present = 0;

    friend bool operator==(const ReportHeaderField&, const ReportHeaderField&) = default;
};

/// What the lights' headers state.
struct ReportHeaderCompleteness
{
    /// The lights counted.
    int lights = 0;

    std::vector<ReportHeaderField> fields;

    friend bool operator==(const ReportHeaderCompleteness&, const ReportHeaderCompleteness&) = default;
};

/// One target of the run.
struct ReportTarget
{
    QString name;
    int frames = 0;
    qint64 integrationMs = 0;
    QStringList filters;

    /// The file names of the target's masters.
    QStringList masters;

    /// The mean over the target's masters of the signal to noise and of the overlap retained.
    std::optional<double> snr;
    std::optional<double> overlapRetained;

    friend bool operator==(const ReportTarget&, const ReportTarget&) = default;
};

/// How many of the run's lights met one fate.
struct ReportFrameStateCount
{
    ReportFrameState state = ReportFrameState::Used;
    int frames = 0;

    friend bool operator==(const ReportFrameStateCount&, const ReportFrameStateCount&) = default;
};

/// The run as a whole.
struct ReportRun
{
    ReportOutcome outcome = ReportOutcome::Completed;

    /// The outcome in one sentence.
    QString verdict;

    /// The report's title, the process that ran and the profile it ran under.
    QString title;
    QString process;
    QString profile;
    QString profileId;

    /// The targets by name.
    QStringList targets;

    /// The mode - "Full", "Incremental", "Registration Only" - the scope and, for a run of one group, that group.
    QString mode;
    QString scope;
    QString scopeKey;
    QString scopeName;

    /// Whether the run was incremental, and the names of the groups it found changed.
    bool incremental = false;
    QStringList changedGroups;

    ReportGroupCounts groups;

    /// The lights the run took - the ones of its groups and the ones the user left out - and those the masters
    /// combine.
    int frames = 0;
    std::optional<int> framesUsed;

    /// The lights by what became of them, the fates no light met left out; empty when no light's fate is stated.
    std::vector<ReportFrameStateCount> frameStates;

    /// The exposure of every light, and of the lights the masters combine.
    qint64 capturedMs = 0;
    std::optional<qint64> integratedMs;

    /// The warnings and the errors the run counted.
    int warnings = 0;
    int errors = 0;

    /// The executor's whole interval, and the phases inside and before it when they were timed.
    qint64 processingMs = 0;
    std::optional<ReportPreparationTiming> preparation;
    std::optional<ReportExecutorTiming> executor;

    /// The run's diagnostics, when collected.
    std::vector<ReportCounter> counters;
    std::vector<ReportCounter> cacheDiagnostics;
    std::optional<ReportEngineMemory> engineMemory;

    std::vector<ReportDiskUse> disk;

    /// The session's notes, and when they were last changed.
    QString sessionNotes;
    std::optional<QDateTime> sessionNotesChangedAt;

    std::vector<ReportNight> nights;
    std::vector<ReportAcquisitionRow> acquisition;
    std::vector<ReportEquipmentRow> equipment;
    std::optional<ReportHeaderCompleteness> headers;

    /// The sensor's temperature over the lights, when stated.
    std::optional<double> lowestTemperature;
    std::optional<double> highestTemperature;

    /// The resolution the first group's lights were taken at.
    QString captureResolution;

    /// The application's version, the run's id, when and where it was archived, and the host.
    QString applicationVersion;
    QString executionId;
    std::optional<QDateTime> archivedAt;
    QString timeZoneId;
    int utcOffsetSeconds = 0;
    QString platform;

    /// The formats of the archive and of the manifest inside it.
    int archiveFormat = 0;
    int manifestFormat = 0;

    /// The fingerprint of the run, and the two it is made of: the settings' and the inputs'.
    QString fingerprint;
    QString settingsFingerprint;
    QString inputsFingerprint;

    /// The run's output folder.
    QString outputFolder;

    /// Settings stated once for the run: the execution, the plate solve, the colour combination.
    std::vector<ReportSettingsBlock> settings;

    /// The run's log, as the run wrote it.
    QString log;

    friend bool operator==(const ReportRun&, const ReportRun&) = default;
};

/// A run's totals, as a comparison of two runs states them.
struct ReportRunTotals
{
    /// The lights the run took and their exposure.
    int frames = 0;
    qint64 integrationMs = 0;

    /// The lights the masters combine and their exposure, when the run recorded the lights' fates.
    std::optional<int> framesUsed;
    std::optional<qint64> integratedMs;

    /// The masters that are there - made or still valid - and the sum of their sizes.
    int masters = 0;
    qint64 outputBytes = 0;

    /// The mean over those masters of the signal to noise and of the overlap retained, when measured.
    std::optional<double> snr;
    std::optional<double> overlapRetained;

    friend bool operator==(const ReportRunTotals&, const ReportRunTotals&) = default;
};

/// A setting that differs between this run and the earlier one, in a group both runs have.
struct ReportSettingChange
{
    /// The step the group belongs to: "Calibration masters", "Lights calibration", "Registration", "Stacking",
    /// "The run".
    QString stage;

    /// The group's key and its name; both empty for a setting stated once for the run.
    QString groupKey;
    QString group;

    /// The settings block the setting stands in, and the setting's name.
    QString block;
    QString name;

    /// Its value in the earlier run and in this one; empty where that run does not state the setting.
    QString before;
    QString after;

    friend bool operator==(const ReportSettingChange&, const ReportSettingChange&) = default;
};

/// A group only one of the two runs has.
struct ReportGroupChange
{
    /// The step the group belongs to, as a setting's change names it.
    QString stage;

    QString groupKey;
    QString group;

    /// Whether the group is new in this run; otherwise the earlier run had it and this run does not.
    bool added = false;

    friend bool operator==(const ReportGroupChange&, const ReportGroupChange&) = default;
};

/// The frames a group both runs have gained and lost since the earlier run, by file.
struct ReportInputChange
{
    /// The step the group belongs to: "Stacking" for a master's lights, "Calibration masters" for a calibration
    /// master's frames.
    QString stage;

    QString groupKey;
    QString group;

    /// The file names of the frames this run has and the earlier one had not, and the reverse.
    QStringList added;
    QStringList removed;

    friend bool operator==(const ReportInputChange&, const ReportInputChange&) = default;
};

/// This run against the most recent earlier run of the folder that completed and whose archive keeps its report.
/// Each master has a baseline of its own, since an incremental run remakes only some: `ReportMaster::baseline`.
struct ReportComparison
{
    /// The earlier run, and when it was archived.
    QString baselineRunId;
    std::optional<QDateTime> baselineArchivedAt;

    ReportRunTotals current;
    ReportRunTotals previous;

    /// The settings that differ in the groups both runs have, and how many settings of those groups were compared.
    std::vector<ReportSettingChange> settingsChanged;
    int settingsCompared = 0;

    /// The groups only one of the two runs has.
    std::vector<ReportGroupChange> groupsChanged;

    std::vector<ReportInputChange> inputsChanged;

    /// The application's version and the three fingerprints of the earlier run.
    QString previousApplicationVersion;
    QString previousFingerprint;
    QString previousSettingsFingerprint;
    QString previousInputsFingerprint;

    friend bool operator==(const ReportComparison&, const ReportComparison&) = default;
};

/// One earlier run of the output folder's history, as its stored report states it. A run whose archive keeps no
/// report this version reads is listed by its id, with why.
struct ReportHistoryEntry
{
    QString runId;

    /// Why the run states no figure here: its archive keeps no stored report, or one that is not read. Empty for a
    /// run whose report was read.
    QString refusal;

    std::optional<QDateTime> archivedAt;
    std::optional<ReportOutcome> outcome;

    /// The lights the masters combined, and the lights the run took.
    std::optional<int> framesUsed;
    std::optional<int> frames;

    /// The exposure of the lights the masters combined.
    std::optional<qint64> integrationMs;

    /// The masters that were there when the run ended: made or still valid.
    std::optional<int> masters;

    /// The background noise that run stated of the master this run lists first among the ones that are there, so
    /// the figure is of one master over the runs.
    std::optional<double> backgroundNoise;

    /// The run's report, relative to this run's folder; empty where it has none.
    QString report;

    friend bool operator==(const ReportHistoryEntry&, const ReportHistoryEntry&) = default;
};

/// Everything one run's report states.
struct ReportModel
{
    /// The format the model was written in.
    int reportSchema = kReportSchema;

    ReportRun run;
    std::vector<ReportIssue> issues;
    std::vector<ReportTarget> targets;
    std::vector<ReportMaster> masters;

    /// The groups whose every light the user left out: the run planned no master of them.
    std::vector<ReportGroupNotStacked> groupsNotStacked;

    std::vector<ReportCalibrationMaster> calibrationMasters;
    std::vector<ReportLightGroup> lightGroups;
    std::vector<ReportRegistrationGroup> registrationGroups;
    std::vector<ReportStage> stages;
    std::vector<ReportDecision> decisions;
    std::optional<ReportComparison> comparison;

    /// The earlier runs of the output folder, oldest first.
    std::vector<ReportHistoryEntry> history;

    std::vector<ReportNotStated> notStated;

    friend bool operator==(const ReportModel&, const ReportModel&) = default;
};
} // namespace ImageIntegration::Reporting
