// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The runs the integration report's suites build, and the files they are made of.
///
/// A report is built from what a run leaves behind: its manifest, its result, the masters it wrote, the metrics its
/// registration stored, and the earlier runs archived in the same output folder. A scenario is one such run, written
/// whole under a temporary directory of its own - nothing touches the production storage tree - and described by the
/// same structures a real run fills, so a suite that builds a report from a scenario builds it as the product does.
/// Each scenario states one thing a report must handle: one master, several targets, a run that stopped, a group
/// reused from an earlier run, a light left out in each of the ways a light can be.
///
/// The first half of this unit is the file helpers the report suites share: the engine session a report reads its
/// images in, the writers of masters and lights through the neutral FITS writer, the metrics database's rows and an
/// archived run's manifest.

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::integration::report
{

/// How many times `needle` occurs in `haystack`, counting no overlap; zero for an empty needle.
[[nodiscard]] int countOccurrences(const QString& haystack, const QString& needle);

/// Why the registration of a scenario's failed lights failed, as a run records it.
inline constexpr char kScenarioRegistrationFailure[] =
    "Too few matching star pairs were found against the reference frame.";

/// A sample of a written image, a fraction of full scale, by position and channel.
using SampleRule = std::function<double(std::uint32_t, std::uint32_t, std::uint32_t)>;

/// A card whose value is text.
[[nodiscard]] an::tests::fixtures::FixtureCard textCard(const std::string& keyword, const QString& value);

/// Writes a Float32 image by the neutral FITS writer: every sample `level`, or what the rule states, and the cards.
/// Fails the running case when the file cannot be written.
///
/// @return whether the file was written.
bool writeFloatImage(const QString& path, int width, int height, an::tests::fixtures::FixtureChannels channels,
                     double level, const SampleRule& rule, std::vector<an::tests::fixtures::FixtureCard> cards);

/// The cards a capture program writes for a camera's ISO speed or gain setting, its offset and the target, followed by
/// the telescope and the site where they are given.
[[nodiscard]] std::vector<an::tests::fixtures::FixtureCard>
equipmentCards(const an::tests::fixtures::AcquisitionFacts& facts, const QString& telescope, const QString& siteLat,
               const QString& siteLong);

/// Writes a flat grey master whose header states the camera, the telescope, the ISO speed or gain, the offset, the
/// site and the target.
///
/// @return `path`.
QString writeMasterWithEquipmentMetadata(const QString& path, int width, int height, const QString& camera,
                                         const QString& telescope, bool isIso, int isoOrGain, int offset,
                                         const QString& siteLat, const QString& siteLong, const QString& objectName);

/// Writes a grey master of a small deterministic pattern around `baseLevel`, so its background noise is not zero.
///
/// @return `path`.
QString writeNoisyMaster(const QString& path, int width, int height, float baseLevel = 0.25f);

/// Writes a three-channel master whose channels sit at three levels.
///
/// @return `path`.
QString writeRgbMaster(const QString& path, int width, int height);

/// Writes a small grey light whose header states what is given and nothing else.
///
/// @return `path`.
QString writeLightFrameWithMetadata(const QString& path, const std::optional<double>& exposureSeconds,
                                    const std::optional<int>& isoOrGain, bool isoMode, const std::optional<int>& offset,
                                    const QString& objectName, const QString& telescope, const QString& siteLat,
                                    const QString& siteLong);

/// The engine session a case's report reads its images and headers in; fails the case when it cannot be opened.
[[nodiscard]] std::optional<an::consumer::EngineSession> openReportSession();

/// Writes registration metrics as rows of a bare `registration_metrics` table - the columns a report reads, without
/// the group state and the format number a registration writes. Each row is an object keyed by column name; a column
/// left out is zero. A scenario's database is written by the product's own writer instead.
///
/// @return whether every row was written.
[[nodiscard]] bool writeRegistrationMetricsDb(const QString& dbPath, const QString& groupKey, int settingsVersion,
                                              const QList<QJsonObject>& rows);

/// A matched catalog source as a registration records it for the spectrophotometric calibration.
[[nodiscard]] an::MatchedSourceReference makeSpccHandoffReference(std::uint64_t sourceId, double separationArcsec,
                                                                  double residualPx);

/// Writes the manifest of an archived run by hand: the document's root with its overall result, the run's manifest
/// and its integration group results, and nothing else an archive holds. For a case that needs an earlier run stated
/// field by field; `archiveScenarioRun` writes a whole archive through the product's archiver.
///
/// @param runDirectory the run's folder under `history/`, created when it is missing.
/// @param manifest the run's manifest.
/// @param overallResult the run's outcome as the archive spells it: `success`, `partial`, `failed`, `cancelled`.
/// @param integrationGroupResults the group results as the archive's JSON holds them.
/// @return whether the file was written.
[[nodiscard]] bool writeArchivedRunManifest(const QString& runDirectory, const WorkflowManifest& manifest,
                                            const QString& overallResult, const QJsonArray& integrationGroupResults);

/// The runs the report's suites build.
enum class ReportScenarioId
{
    /// One mono master from one night, with no calibration frames at all.
    OneMonoMaster,

    /// One colour master from a mosaic camera, over two nights, calibrated with bias, dark and flat: a clean run.
    OneColourMaster,

    /// Luminance, red, green, blue and hydrogen-alpha of one target - 60, 30, 30, 30 and 24 lights over two nights -
    /// in two registration groups, with a combined RGB output, a bias master reused from an earlier run and no flat
    /// for the hydrogen-alpha lights.
    FiveFilters,

    /// Two targets, two filters each, in one run.
    TwoTargets,

    /// A drizzled group beside a plain one.
    DrizzledGroup,

    /// A comet run: lights registered on the comet and stacked in the comet's frame.
    CometRun,

    /// An exposure merge of lights of three exposures in one group.
    HdrExposureMerge,

    /// A run that failed in stacking after three masters: one master failed, one was never reached.
    FailedInStacking,

    /// A run cancelled after two of its five masters were made.
    CancelledAfterTwoMasters,

    /// A run cancelled before any master was made, which the executor archives nothing for.
    CancelledBeforeAnyMaster,

    /// A run that failed before any master was made, which the executor archives nothing for.
    FailedBeforeAnyMaster,

    /// An incremental run over the five filters, archived after a full one: two groups regenerated, three reused.
    IncrementalRun,

    /// An incremental run in which every group was still valid: nothing regenerated.
    EveryGroupReused,

    /// Lights marked pre-calibrated beside lights calibrated with a master dark the user supplied.
    PreCalibratedAndSuppliedDark,

    /// The five filters with no engine session: nothing can be read from an image or a header.
    FiveFiltersWithoutSession,

    /// The five filters with no registration metrics database.
    FiveFiltersWithoutMetrics,

    /// The five filters after an earlier run of the same folder, of the first night alone, archived as the product
    /// archives a run.
    FiveFiltersAfterEarlierRun,

    /// The five filters after three earlier runs of the same folder: a full run of the first night's broadband
    /// lights under another weighting, with no hydrogen-alpha; an incremental run that made the luminance master
    /// again from more lights and found the red, green and blue ones still valid; and a run whose archive keeps no
    /// report, as an earlier version of the application left it. Each master of the last run has an earlier run of
    /// its own to be compared with, or none.
    FiveFiltersAfterTwoEarlierRuns,

    /// One mono master from the Lite front end.
    LiteFrontEnd,

    /// The five filters with a history too small to keep the run's masters: the archive holds metadata alone.
    FiveFiltersWithoutPayloads,

    /// One group of 2,000 lights.
    TwoThousandLights,

    /// Every way a light is left out of a master: excluded and stack-excluded by the user, a group whose every light
    /// is left out, lights excluded on a restack, registration failures, and a light with no registered output.
    EveryLightState,

    /// Photometric weighting with zero points and catalog matches in one group, local weighting in another.
    PhotometricAndLocalWeighting,

    /// Distortion and chromatic aberration corrections in one registration group, manual control points in another.
    DistortionCorrectionsAndManualPoints,
};

/// What a scenario was built to hold, counted as it was built: the figures a suite holds a report to.
struct ReportScenarioFacts
{
    /// The lights the run's groups took, the ones the user left out included.
    int lightsTaken = 0;

    /// The lights the user left out: excluded in the lights' list, or from one stack. No group of the manifest names
    /// them; its list of the lights left out does.
    int lightsLeftOutByUser = 0;

    /// The lights whose registration failed. They are still in their groups' lists.
    int registrationFailures = 0;

    /// The registered lights a stack dropped on its own, for want of a registered output: its group's result names
    /// each with the reason.
    int lightsDroppedByStack = 0;

    /// The lights a stack flagged and combined its master without.
    int lightsExcludedOnRestack = 0;

    /// The lights the masters made in this run combined.
    int lightsCombined = 0;

    /// The masters the run planned, a combined RGB output among them.
    int mastersPlanned = 0;

    /// The planned masters by what became of them. A reused master's output was still valid, so the run made none.
    int mastersMade = 0;
    int mastersFailed = 0;
    int mastersReused = 0;
    int mastersNotReached = 0;

    /// The calibration masters the run's lights are calibrated with, made, reused or supplied.
    int calibrationMasters = 0;

    /// The run's registration groups.
    int registrationGroups = 0;

    /// The nights the lights were taken on.
    int nights = 0;

    /// The integration groups of the folder whose every light the user left out, so the manifest holds no group.
    int groupsLeftOutWhole = 0;
};

/// A light the user left out of a run. The manifest names none of them, so the scenario does.
struct ReportScenarioLeftOutLight
{
    /// The light's file.
    QString path;

    /// The key of the integration group the light would have joined.
    QString integrationGroupKey;

    /// Whether the light was left out of its stack alone - it is still registered - or excluded from the run.
    bool fromStackOnly = false;
};

/// One run, written under a temporary directory the scenario owns and removes with its last copy.
struct ReportScenario
{
    ReportScenarioId id = ReportScenarioId::OneMonoMaster;

    /// The scenario's name, as `reportScenarioName` states it.
    QString name;

    /// The run's output folder: the masters, `registration_metrics.db`, `history/`.
    QString outputDirectory;

    /// The run's own folder under `history/`, where its archive is written. It does not exist until the run is
    /// archived.
    QString runDirectory;

    WorkflowManifest manifest;
    WorkflowExecutor::Result result;

    /// The archive's context: the application's version, the execution and the times and zone it states.
    QJsonObject context;

    /// The run's metrics database; empty when the run has none.
    QString metricsDatabasePath;

    /// The earlier runs archived in the output folder, oldest first. Each keeps its report, as a run of this
    /// version does, unless the scenario says otherwise.
    QStringList earlierRunDirectories;

    /// Whether the executor archives the run: it does not for a run that stopped before any master was made.
    bool archivedByExecutor = true;

    /// The largest history the run is archived into, in bytes; zero for the product's own limit. A scenario whose
    /// archive keeps no payload states a limit its masters do not fit.
    qint64 historyByteLimit = 0;

    ReportScenarioFacts facts;

    /// The lights the user left out, in the order the run met them.
    std::vector<ReportScenarioLeftOutLight> lightsLeftOutByUser;

    /// The session a report of this run reads its images and headers in; null for a run reported without one.
    [[nodiscard]] const an::consumer::EngineSession* session() const noexcept
    {
        return m_session.get();
    }

    /// The masters the run made, as the paths its results state.
    [[nodiscard]] QStringList madeMasterPaths() const;

private:
    friend class ReportScenarioBuilder;

    std::shared_ptr<QTemporaryDir> m_root;
    std::shared_ptr<an::consumer::EngineSession> m_session;
};

/// Every scenario, in the order the enumeration lists them.
[[nodiscard]] const std::vector<ReportScenarioId>& allReportScenarios();

/// A scenario's name, which a suite prints in a failure and a sample writer names a file by.
[[nodiscard]] QString reportScenarioName(ReportScenarioId id);

/// Builds a scenario: its folder, its masters, its lights, its metrics database and its earlier runs. Fails the
/// running case, and returns nothing, when a file cannot be written.
[[nodiscard]] std::optional<ReportScenario> buildReportScenario(ReportScenarioId id);

/// Archives a scenario's run as the executor does once the run ends: through the product's archiver, into
/// `runDirectory`, in the scenario's session and within its history limit, stating the time, the place and the
/// version the scenario's context states. The result is completed as a commit completes it - each master's retention
/// among it - and the archive holds the run's report as a run stores it: the model as `workflow_report.json`, the
/// linked document, and the pictures of the earlier runs its masters are compared with. Fails the running case when
/// the commit fails.
///
/// @return whether the run was archived.
[[nodiscard]] bool archiveScenarioRun(ReportScenario& scenario);

} // namespace an::tests::integration::report
