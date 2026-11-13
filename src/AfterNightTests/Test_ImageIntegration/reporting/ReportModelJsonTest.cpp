// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The report model as the document a run's archive keeps: every field survives being written and read, a value that
/// is not stated stays not stated, a key the reader does not know is ignored, and a document of a newer format is
/// refused by name.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportModelJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <limits>
#include <optional>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
/// A document as it comes back from the file the archive writes: serialized to text and parsed again, so a number
/// is only what its text keeps.
QJsonObject throughText(const QJsonObject& document)
{
    return QJsonDocument::fromJson(QJsonDocument(document).toJson(QJsonDocument::Compact)).object();
}

/// A model in which every kind of value the model holds is stated at least once: text, counts past 32 bits, figures
/// that are not finite, dates, times in a zone, every optional, nested lists.
ReportModel modelWithEveryKindOfValue()
{
    ReportModel model;
    ReportRun& run = model.run;
    run.outcome = ReportOutcome::Partial;
    run.verdict = QStringLiteral("The run made 1 of its 2 masters.");
    run.title = QStringLiteral("AfterNight Integration Report");
    run.targets = {QStringLiteral("M 20"), QStringLiteral("NGC 253")};
    run.incremental = true;
    run.changedGroups = {QStringLiteral("Luminance \u2014 M 20")};
    run.groups = ReportGroupCounts{.total = 2, .made = 1, .reused = 0, .failed = 1, .notReached = 0};
    run.frames = 174;
    run.framesUsed = 168;
    run.framesWithExposure = 172;
    run.capturedMs = 35'400'000;
    run.integratedMs = 34'000'000;
    run.processingMs = 5'000'000'000LL;
    run.preparation = ReportPreparationTiming{.manifestBuildMs = 12, .planAndValidationMs = 34};
    run.executor =
        ReportExecutorTiming{.stageExecutionMs = 1000, .stateCommitMs = 20, .archiveMs = 300, .reportMs = 127};
    run.counters = {ReportCounter{
        .key = QStringLiteral("bytesHashed"), .name = QStringLiteral("Bytes hashed"), .value = 9'000'000'000'000ULL}};
    run.cacheDiagnostics = {ReportCounter{.key = QStringLiteral("hits"), .name = QStringLiteral("Hits"), .value = 7}};
    run.engineMemory = ReportEngineMemory{.peakBytes = 6'000'000'000ULL,
                                          .peakBytesBeforeRun = 1,
                                          .liveBytesAtEnd = 2,
                                          .allocations = 3,
                                          .bytesAllocated = 4,
                                          .deniedAllocations = 5};
    run.disk = {ReportDiskUse{.kind = QStringLiteral("Masters"), .bytes = 104'438'989, .files = 6}};
    run.sessionNotes = QStringLiteral("Thin cloud after 02:00 <b>&\n\"second line\"");
    run.sessionNotesChangedAt =
        QDateTime(QDate(2026, 3, 24), QTime(23, 15, 7, 250), QTimeZone::fromSecondsAheadOfUtc(-10800));
    run.nights = {ReportNight{
        .date = QDate(2026, 3, 24),
        .frames = 87,
        .integrationMs = 5'000'000,
        .filters = {ReportNightFilter{.filter = QStringLiteral("Luminance"), .frames = 30, .integrationMs = 1}}}};
    run.acquisition = {
        ReportAcquisitionRow{.filter = QStringLiteral("Luminance"),
                             .exposure = ReportExposure{.frames = 60,
                                                        .framesWithExposure = 59,
                                                        .integrationMs = 10'620'000,
                                                        .bins = {ReportExposureBin{.seconds = 180.0, .frames = 59}}},
                             .share = 0.3}};
    run.equipment = {ReportEquipmentRow{.masters = {QStringLiteral("Luminance")},
                                        .target = QStringLiteral("M 20"),
                                        .camera = QStringLiteral("ASI2600MM"),
                                        .captureResolution = QStringLiteral("6248 \u00D7 4176"),
                                        .outputResolution = QStringLiteral("6200 \u00D7 4100"),
                                        .gain = QStringLiteral("Gain 100"),
                                        .offset = QStringLiteral("Offset 50"),
                                        .telescope = QStringLiteral("FSQ-106"),
                                        .site = QStringLiteral("-34.6, -58.4")}};
    run.headers = ReportHeaderCompleteness{
        .lights = 174, .fields = {ReportHeaderField{.name = QStringLiteral("Exposure"), .present = 174}}};
    run.lowestTemperature = -10.5;
    run.highestTemperature = -9.5;
    run.archivedAt = QDateTime(QDate(2026, 11, 10), QTime(11, 15, 0, 1), QTimeZone::fromSecondsAheadOfUtc(-10800));
    run.timeZoneId = QStringLiteral("America/Argentina/Buenos_Aires");
    run.utcOffsetSeconds = -10800;
    run.archiveFormat = 3;
    run.manifestFormat = 1;
    run.fingerprint = QStringLiteral("abc");
    run.settings = {ReportSettingsBlock{.title = QStringLiteral("Execution"),
                                        .rows = {ReportSettingsRow{.name = QStringLiteral("Parallel CPU Threads"),
                                                                   .value = QStringLiteral("All available (16)"),
                                                                   .requested = QStringLiteral("Auto"),
                                                                   .reason = QStringLiteral("because")}}}};
    run.log = QStringLiteral("[2026-11-10T11:15:00.000] [INFO] \u2550\u2550\u2550 STAGE \u2550\u2550\u2550\nline 2");

    model.issues = {ReportIssue{.kind = ReportIssueKind::MasterWithoutAstrometry,
                                .severity = ReportIssueSeverity::Note,
                                .title = QStringLiteral("title"),
                                .detail = QStringLiteral("detail"),
                                .action = QStringLiteral("action"),
                                .groupKey = QStringLiteral("key"),
                                .anchor = QStringLiteral("file-luminance")}};
    model.targets = {ReportTarget{.name = QStringLiteral("M 20"),
                                  .frames = 174,
                                  .framesWithExposure = 172,
                                  .integrationMs = 35'400'000,
                                  .filters = {QStringLiteral("Ha"), QStringLiteral("Luminance")},
                                  .masters = {QStringLiteral("l.fits")},
                                  .snr = 61.5,
                                  .overlapRetained = 0.97}};

    ReportMaster master;
    master.key = QStringLiteral("6248x4176|-|-|-|Luminance|M 20|-");
    master.slug = QStringLiteral("luminance-m-20");
    master.previewFolder = QStringLiteral("6248x4176_Luminance_M_20");
    master.name = QStringLiteral("Luminance \u2014 M 20 (6248 \u00D7 4176)");
    master.kind = ReportMasterKind::CombinedRgb;
    master.state = ReportMasterState::NotReached;
    master.bytes = qint64{0};
    master.retention = ReportRetention::NotRetainedByArchiveFailure;
    master.lightsCombined = 60;
    master.exposure = run.acquisition.front().exposure;
    master.overlapRetained = 0.97;
    master.calibration = ReportMasterCalibration{
        .bias = {QStringLiteral("bias.fits")}, .dark = {QStringLiteral("dark.fits")}, .flat = {}};
    master.astrometry = ReportAstrometryOutcome::Inherited;
    master.durationMs = qint64{123'456};
    master.finalization = {
        ReportPhase{.name = QStringLiteral("Master write"), .status = QStringLiteral("completed"), .durationMs = 9}};
    ReportChannelStatistics channel;
    channel.samples = 26'091'648;
    channel.unclippedSamples = 26'000'000;
    channel.mean = 0.01742656553;
    channel.backgroundNoise = 6.749e-05;
    channel.ikssLocation = 0.017;
    master.statistics = ReportImageStatistics{.width = 6248, .height = 4176, .channels = 1, .channelStats = {channel}};
    master.distribution = {
        ReportSampleDistribution{.lowest = 0.001, .highest = 1.0, .counts = {1, 2, 3'000'000'000ULL}}};
    master.snapshot = ReportMasterSnapshot{
        .mean = 0.0174, .backgroundNoise = 6.7e-5, .snr = std::numeric_limits<double>::infinity(), .dynamicRange = 0.9};
    ReportStack stack;
    stack.inputFrames = 60;
    stack.effectiveFrames = 57.25;
    stack.rejection = ReportStackRejection{
        .global = 0.012, .lowestFrame = 0.001, .meanFrame = 0.012, .medianFrame = 0.011, .highestFrame = 0.09};
    stack.weights = ReportStackWeights{.lowest = 0.4, .highest = 1.6, .deviation = 0.2, .efficiency = 0.95};
    stack.localWeights = ReportLocalWeights{.windowSize = 64,
                                            .gridSpacing = 32,
                                            .snrExponent = 2.0,
                                            .grids = 60,
                                            .lowest = 0.1,
                                            .highest = 2.0,
                                            .mean = 1.0,
                                            .deviation = 0.3};
    stack.clusters = ReportStackClusters{.count = 1,
                                         .pixels = 40,
                                         .fraction = 1e-6,
                                         .largestPixels = 40,
                                         .largestFraction = 1e-6,
                                         .rejectedElements = 400,
                                         .rejectedElementFraction = 1e-5,
                                         .clusters = {ReportRejectionCluster{.map = ReportClusterMap::High,
                                                                             .channel = 0,
                                                                             .pixels = 40,
                                                                             .minX = 1,
                                                                             .minY = 2,
                                                                             .maxX = 3,
                                                                             .maxY = 4,
                                                                             .centroidX = 2.5,
                                                                             .centroidY = 3.5}}};
    stack.restack = ReportRestack{.applied = true,
                                  .flaggedFrameThreshold = 0.08,
                                  .initialClusters = 3,
                                  .excluded = {ReportRestackExclusion{
                                      .index = 4, .fileName = QStringLiteral("l_005.fits"), .rejectedFraction = 0.09}}};
    master.stack = stack;
    master.maps = {ReportMap{.kind = ReportMapKind::Quality,
                             .title = QStringLiteral("Weight Sum Map"),
                             .file = QStringLiteral("l_weights.fits"),
                             .bytes = qint64{4096},
                             .preview = QStringLiteral("report_previews/x/quality_map_1.jpg"),
                             .clusters = 2,
                             .weightSum = true}};
    master.rejectionMapsExpected = 2;
    master.settings = run.settings;
    master.weighting = ReportMasterWeighting{.mode = QStringLiteral("Photometric"),
                                             .minimumWeight = 0.05,
                                             .floorApplies = true,
                                             .framesWithMetrics = 60,
                                             .framesRegistered = 58,
                                             .framesAtFloor = 2,
                                             .weightedIntegrationMs = 10'000'000,
                                             .medianWeight = 0.98,
                                             .medianQualityScore = 71.2,
                                             .medianZeroPoint = 21.3,
                                             .medianAstrometricRmsArcsec = 0.21,
                                             .medianCatalogMatches = 412.0};
    ReportFrame frame;
    frame.fileName = QStringLiteral("l_001.fits");
    frame.path = QStringLiteral("D:/lights/l_001.fits");
    frame.state = ReportFrameState::ExcludedOnRestack;
    frame.stateDetail = QStringLiteral("flagged");
    frame.night = QDate(2026, 3, 24);
    frame.capturedAt = QDateTime(QDate(2026, 3, 25), QTime(1, 2, 3), QTimeZone::UTC);
    frame.exposureSeconds = 180.0;
    frame.registered = false;
    frame.stars = 0;
    frame.fwhm = 2.31;
    frame.shiftX = -3.5;
    frame.shiftY = 0.0;
    frame.transformKind = QStringLiteral("homography");
    frame.normalizationScale = {1.01, 0.99, 1.0};
    frame.normalizationOffset = {-0.001};
    frame.weight = 0.0;
    frame.stackShare = 0.0;
    frame.rejectedFraction = 0.09;
    frame.gain = QStringLiteral("Gain 100");
    frame.temperature = -10.0;
    master.frames = {frame, ReportFrame{}};
    master.baseline = ReportBaseline{.runId = QStringLiteral("20261109_101500_000"),
                                     .archivedAt = run.archivedAt,
                                     .made = true,
                                     .frames = 87,
                                     .framesTaken = 90,
                                     .integrationMs = qint64{5'000'000},
                                     .backgroundNoise = 9.1e-5,
                                     .snr = 44.0,
                                     .fwhm = 2.4,
                                     .effectiveFrames = 80.0,
                                     .rejectedFraction = 0.02,
                                     .sideBySide = true,
                                     .preview = QStringLiteral("report_previews/x/previous_master.jpg"),
                                     .detail = {}};
    model.masters = {master, ReportMaster{}};

    ReportCalibrationMaster calibration;
    calibration.kind = ReportCalibrationKind::Flat;
    calibration.state = ReportCalibrationState::Supplied;
    calibration.frames = 30;
    calibration.inputFiles = {QStringLiteral("f1.fits"), QStringLiteral("f2.fits")};
    calibration.level = {0.31, 0.3, 0.29};
    calibration.noise = {0.001};
    calibration.usedBy = {master.key};
    model.calibrationMasters = {calibration};
    model.lightGroups = {ReportLightGroup{.key = QStringLiteral("k"),
                                          .name = QStringLiteral("n"),
                                          .lights = 12,
                                          .preCalibrated = true,
                                          .biasMaster = {},
                                          .darkMaster = QStringLiteral("dark.fits"),
                                          .flatMaster = {},
                                          .settings = {}}};

    ReportRegistrationGroup registration;
    registration.key = QStringLiteral("6248x4176|-|-|-|-|M 20|-");
    registration.lights = 150;
    registration.registered = 147;
    registration.referenceChosenAutomatically = true;
    registration.transformsUsed = {ReportTransformUse{.kind = QStringLiteral("homography"), .frames = 140},
                                   ReportTransformUse{.kind = QStringLiteral("polynomial3"), .frames = std::nullopt}};
    registration.aligned = 147;
    registration.distortionCorrected = 0;
    registration.overlap = 0.96;
    ReportCometRun comet;
    comet.status = QStringLiteral("Ready");
    comet.rmsPixels = 0.4;
    comet.inputsAccepted = 20;
    comet.pointsManual = 2;
    comet.transformsSucceeded = 20;
    registration.cometRuns = {comet};
    registration.catalogHandoff = ReportCatalogHandoff{.settingsVersion = 3,
                                                       .frames = 10,
                                                       .references = 100,
                                                       .sources = 80,
                                                       .satisfied = 90,
                                                       .missingSource = 1,
                                                       .missingChunkOrCache = 2,
                                                       .manifestMismatch = 3,
                                                       .staleMatch = 4,
                                                       .rejectedQualityFields = 0,
                                                       .untested = 0,
                                                       .astrometricCatalog = QStringLiteral("gaia_dr3_g14"),
                                                       .photometricCatalog = QStringLiteral("gaia_xp"),
                                                       .medianResidualPixels = 0.3,
                                                       .medianSeparationArcsec = std::nullopt};
    registration.masters = {master.key};
    model.registrationGroups = {registration};

    model.stages = {ReportStage{.key = QStringLiteral("LightsStacking"),
                                .name = QStringLiteral("Lights Stacking"),
                                .state = ReportStageState::NotReached,
                                .status = QStringLiteral("unknown"),
                                .durationMs = std::nullopt,
                                .groups = 5,
                                .note = QStringLiteral("note")}};
    model.decisions = {ReportDecision{.area = QStringLiteral("Registration: Transform Model"),
                                      .groupKey = registration.key,
                                      .group = QStringLiteral("M 20"),
                                      .requested = QStringLiteral("Auto"),
                                      .resolved = QStringLiteral("Homography"),
                                      .reason = QStringLiteral("why")}};
    ReportComparison comparison;
    comparison.baselineRunId = QStringLiteral("20261109_101500_000");
    comparison.baselineArchivedAt = run.archivedAt;
    comparison.current = ReportRunTotals{.frames = 174,
                                         .integrationMs = 9,
                                         .framesUsed = 168,
                                         .integratedMs = qint64{7},
                                         .masters = 6,
                                         .outputBytes = 8,
                                         .snr = 61.0,
                                         .overlapRetained = 0.9};
    comparison.previous = ReportRunTotals{.frames = 87,
                                          .integrationMs = 4,
                                          .framesUsed = std::nullopt,
                                          .integratedMs = std::nullopt,
                                          .masters = 0,
                                          .outputBytes = 3,
                                          .snr = std::nullopt,
                                          .overlapRetained = std::nullopt};
    comparison.settingsChanged = {ReportSettingChange{.stage = QStringLiteral("Stacking"),
                                                      .groupKey = master.key,
                                                      .group = master.name,
                                                      .block = QStringLiteral("Rejection"),
                                                      .name = QStringLiteral("Sigma High"),
                                                      .before = QStringLiteral("3"),
                                                      .after = QStringLiteral("2.5")},
                                  ReportSettingChange{.stage = QStringLiteral("The run"),
                                                      .groupKey = {},
                                                      .group = {},
                                                      .block = QStringLiteral("Execution"),
                                                      .name = QStringLiteral("Threads"),
                                                      .before = {},
                                                      .after = QStringLiteral("16")}};
    comparison.settingsCompared = 64;
    comparison.groupsChanged = {ReportGroupChange{.stage = QStringLiteral("Registration"),
                                                  .groupKey = registration.key,
                                                  .group = QStringLiteral("M 20"),
                                                  .added = true},
                                ReportGroupChange{.stage = QStringLiteral("Calibration masters"),
                                                  .groupKey = QStringLiteral("dark|600"),
                                                  .group = QStringLiteral("Dark 600 s"),
                                                  .added = false}};
    comparison.inputsChanged = {ReportInputChange{.stage = QStringLiteral("Stacking"),
                                                  .groupKey = master.key,
                                                  .group = master.name,
                                                  .added = {QStringLiteral("l_061.fits")},
                                                  .removed = {}}};
    model.comparison = comparison;
    model.history = {ReportHistoryEntry{.runId = QStringLiteral("20261108_093000_000"),
                                        .refusal = QStringLiteral("The archive keeps no stored report."),
                                        .archivedAt = std::nullopt,
                                        .outcome = std::nullopt,
                                        .framesUsed = std::nullopt,
                                        .frames = std::nullopt,
                                        .integrationMs = std::nullopt,
                                        .masters = std::nullopt,
                                        .backgroundNoise = std::nullopt,
                                        .report = {}},
                     ReportHistoryEntry{.runId = QStringLiteral("20261109_101500_000"),
                                        .refusal = {},
                                        .archivedAt = run.archivedAt,
                                        .outcome = ReportOutcome::Cancelled,
                                        .framesUsed = 80,
                                        .frames = 87,
                                        .integrationMs = qint64{4},
                                        .masters = 5,
                                        .backgroundNoise = 9e-5,
                                        .report = QStringLiteral("../20261109_101500_000/workflow_report.html")}};
    model.notStated = {ReportNotStated{.what = QStringLiteral("The masters' statistics"),
                                       .why = QStringLiteral("The report was built without a session.")}};
    return model;
}
} // namespace

TEST(ReportModelJson, EveryScenariosModelSurvivesItsDocument)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const ReportModel model = buildScenarioModel(*scenario);

        const QJsonObject document = ReportModelJson::toJson(model);
        EXPECT_EQ(document.value(QStringLiteral("reportSchema")).toInt(), kReportSchema);

        const ReportModelRead read = ReportModelJson::fromJson(throughText(document));
        ASSERT_TRUE(read.model.has_value());
        EXPECT_FALSE(read.refusal.has_value());
        EXPECT_TRUE(*read.model == model) << "the model read back is not the model written";
        EXPECT_EQ(ReportModelJson::toJson(*read.model), document);
    }
}

TEST(ReportModelJson, AModelWithEveryKindOfValueSurvivesItsDocument)
{
    const ReportModel model = modelWithEveryKindOfValue();

    const ReportModelRead read = ReportModelJson::fromJson(throughText(ReportModelJson::toJson(model)));

    ASSERT_TRUE(read.model.has_value());
    EXPECT_TRUE(read.model->run == model.run);
    EXPECT_TRUE(read.model->issues == model.issues);
    EXPECT_TRUE(read.model->targets == model.targets);
    ASSERT_EQ(read.model->masters.size(), model.masters.size());
    EXPECT_TRUE(read.model->masters.front().statistics == model.masters.front().statistics);
    EXPECT_TRUE(read.model->masters.front().stack == model.masters.front().stack);
    EXPECT_TRUE(read.model->masters.front().frames == model.masters.front().frames);
    EXPECT_TRUE(read.model->masters.front().baseline == model.masters.front().baseline);
    EXPECT_TRUE(read.model->masters == model.masters);
    EXPECT_TRUE(read.model->calibrationMasters == model.calibrationMasters);
    EXPECT_TRUE(read.model->lightGroups == model.lightGroups);
    EXPECT_TRUE(read.model->registrationGroups == model.registrationGroups);
    EXPECT_TRUE(read.model->stages == model.stages);
    EXPECT_TRUE(read.model->decisions == model.decisions);
    EXPECT_TRUE(read.model->comparison == model.comparison);
    EXPECT_TRUE(read.model->history == model.history);
    EXPECT_TRUE(read.model->notStated == model.notStated);
    EXPECT_TRUE(*read.model == model);

    // A count past 32 bits, a figure that is not finite and a time in its zone are the ones written.
    EXPECT_EQ(read.model->run.counters.front().value, 9'000'000'000'000ULL);
    EXPECT_EQ(read.model->masters.front().distribution.front().counts.back(), 3'000'000'000ULL);
    EXPECT_TRUE(std::isinf(*read.model->masters.front().snapshot.snr));
    EXPECT_EQ(read.model->run.archivedAt->offsetFromUtc(), -10800);
    EXPECT_EQ(read.model->run.archivedAt->time().msec(), 1);
}

TEST(ReportModelJson, EveryNameOfAnOutcomeAStateAndAnIssueKindReadsBackAsItself)
{
    ReportModel model;
    for (int kind = 0; kind <= static_cast<int>(ReportIssueKind::RunMessage); ++kind)
    {
        model.issues.push_back(ReportIssue{.kind = static_cast<ReportIssueKind>(kind),
                                           .severity = static_cast<ReportIssueSeverity>(kind % 3),
                                           .title = {},
                                           .detail = {},
                                           .action = {},
                                           .groupKey = {},
                                           .anchor = {}});
    }
    for (int state = 0; state <= static_cast<int>(ReportMasterState::Cancelled); ++state)
    {
        ReportMaster master;
        master.state = static_cast<ReportMasterState>(state);
        master.retention = static_cast<ReportRetention>(state % 4);
        master.astrometry = static_cast<ReportAstrometryOutcome>(state % 4);
        for (int frameState = 0; frameState <= static_cast<int>(ReportFrameState::NotStacked); ++frameState)
        {
            ReportFrame frame;
            frame.state = static_cast<ReportFrameState>(frameState);
            master.frames.push_back(frame);
        }
        model.masters.push_back(master);
    }
    for (int state = 0; state <= static_cast<int>(ReportStageState::Skipped); ++state)
    {
        ReportStage stage;
        stage.state = static_cast<ReportStageState>(state);
        model.stages.push_back(stage);
    }
    for (int outcome = 0; outcome <= static_cast<int>(ReportOutcome::Cancelled); ++outcome)
    {
        ReportHistoryEntry entry;
        entry.outcome = static_cast<ReportOutcome>(outcome);
        model.history.push_back(entry);
    }
    for (int state = 0; state <= static_cast<int>(ReportCalibrationState::Failed); ++state)
    {
        ReportCalibrationMaster master;
        master.state = static_cast<ReportCalibrationState>(state);
        master.kind = static_cast<ReportCalibrationKind>(state % 3);
        model.calibrationMasters.push_back(master);
    }

    const QJsonObject document = ReportModelJson::toJson(model);
    const ReportModelRead read = ReportModelJson::fromJson(throughText(document));

    ASSERT_TRUE(read.model.has_value());
    EXPECT_TRUE(*read.model == model);
    // No value is written as an empty name, which would read back as the first enumerator.
    const QByteArray text = QJsonDocument(document).toJson(QJsonDocument::Compact);
    EXPECT_FALSE(text.contains("\"kind\":\"\""));
    EXPECT_FALSE(text.contains("\"state\":\"\""));
    EXPECT_FALSE(text.contains("\"outcome\":\"\""));
    EXPECT_FALSE(text.contains("\"severity\":\"\""));
}

TEST(ReportModelJson, ADocumentOfANewerFormatIsRefusedByName)
{
    QJsonObject document = ReportModelJson::toJson(modelWithEveryKindOfValue());
    document.insert(QStringLiteral("reportSchema"), kReportSchema + 1);

    const ReportModelRead read = ReportModelJson::fromJson(document);

    EXPECT_FALSE(read.model.has_value());
    ASSERT_TRUE(read.refusal.has_value());
    EXPECT_FALSE(read.refusal->what.isEmpty());
    EXPECT_TRUE(read.refusal->why.contains(QString::number(kReportSchema + 1))) << read.refusal->why.toStdString();
    EXPECT_TRUE(read.refusal->why.contains(QString::number(kReportSchema))) << read.refusal->why.toStdString();
}

TEST(ReportModelJson, ADocumentOfAnEarlierFormatIsRefusedByNameToo)
{
    // No earlier format is read: a document is this version's or it is refused.
    QJsonObject document = ReportModelJson::toJson(modelWithEveryKindOfValue());
    document.insert(QStringLiteral("reportSchema"), kReportSchema + 1);
    const QString newer = ReportModelJson::fromJson(document).refusal->why;
    document.insert(QStringLiteral("reportSchema"), 0);
    EXPECT_FALSE(ReportModelJson::fromJson(document).model.has_value());
    EXPECT_FALSE(newer.contains(QStringLiteral("newer"))) << "a refusal names the two formats, not their order";
}

TEST(ReportModelJson, ADocumentThatStatesNoFormatIsRefused)
{
    QJsonObject document = ReportModelJson::toJson(ReportModel{});
    document.remove(QStringLiteral("reportSchema"));

    EXPECT_FALSE(ReportModelJson::fromJson(document).model.has_value());
    EXPECT_TRUE(ReportModelJson::fromJson(document).refusal.has_value());
    EXPECT_FALSE(ReportModelJson::fromJson(QJsonObject{}).model.has_value());

    document.insert(QStringLiteral("reportSchema"), QStringLiteral("1"));
    EXPECT_FALSE(ReportModelJson::fromJson(document).model.has_value()) << "a format is a number";
}

TEST(ReportModelJson, AKeyTheReaderDoesNotKnowIsIgnored)
{
    const ReportModel model = modelWithEveryKindOfValue();
    QJsonObject document = ReportModelJson::toJson(model);
    document.insert(QStringLiteral("addedLater"), QStringLiteral("a field of a later version"));
    QJsonObject run = document.value(QStringLiteral("run")).toObject();
    run.insert(QStringLiteral("addedLater"), QJsonObject{{QStringLiteral("nested"), 1}});
    document.insert(QStringLiteral("run"), run);
    QJsonArray masters = document.value(QStringLiteral("masters")).toArray();
    QJsonObject first = masters.first().toObject();
    first.insert(QStringLiteral("addedLater"), QJsonArray{1, 2, 3});
    masters.replace(0, first);
    document.insert(QStringLiteral("masters"), masters);

    const ReportModelRead read = ReportModelJson::fromJson(throughText(document));

    ASSERT_TRUE(read.model.has_value());
    EXPECT_TRUE(*read.model == model);
}

TEST(ReportModelJson, AValueThatIsNotStatedStaysNotStatedAndAStatedZeroStaysAZero)
{
    ReportModel model;
    ReportMaster measured;
    measured.bytes = qint64{0};
    measured.lightsCombined = 0;
    measured.overlapRetained = 0.0;
    ReportMaster unmeasured;
    model.masters = {measured, unmeasured};

    const QJsonObject document = ReportModelJson::toJson(model);
    const QJsonArray masters = document.value(QStringLiteral("masters")).toArray();
    ASSERT_EQ(masters.size(), 2);
    EXPECT_TRUE(masters.at(0).toObject().contains(QStringLiteral("bytes")));
    EXPECT_FALSE(masters.at(1).toObject().contains(QStringLiteral("bytes")));
    EXPECT_FALSE(masters.at(1).toObject().contains(QStringLiteral("statistics")));
    EXPECT_FALSE(masters.at(1).toObject().contains(QStringLiteral("stack")));
    EXPECT_FALSE(document.contains(QStringLiteral("comparison")));

    const ReportModelRead read = ReportModelJson::fromJson(throughText(document));
    ASSERT_TRUE(read.model.has_value());
    ASSERT_EQ(read.model->masters.size(), 2u);
    ASSERT_TRUE(read.model->masters[0].bytes.has_value());
    EXPECT_EQ(*read.model->masters[0].bytes, 0);
    ASSERT_TRUE(read.model->masters[0].overlapRetained.has_value());
    EXPECT_EQ(*read.model->masters[0].overlapRetained, 0.0);
    EXPECT_FALSE(read.model->masters[1].bytes.has_value());
    EXPECT_FALSE(read.model->masters[1].lightsCombined.has_value());
    EXPECT_FALSE(read.model->masters[1].overlapRetained.has_value());
    EXPECT_FALSE(read.model->masters[1].statistics.has_value());
    EXPECT_FALSE(read.model->masters[1].stack.has_value());
    EXPECT_FALSE(read.model->comparison.has_value());
    EXPECT_FALSE(read.model->run.framesUsed.has_value());

    // A null where a value may be stated reads as not stated, as a document edited by hand may hold one.
    QJsonObject edited = document;
    QJsonArray editedMasters = masters;
    QJsonObject nulled = editedMasters.at(0).toObject();
    nulled.insert(QStringLiteral("bytes"), QJsonValue());
    editedMasters.replace(0, nulled);
    edited.insert(QStringLiteral("masters"), editedMasters);
    const ReportModelRead readEdited = ReportModelJson::fromJson(edited);
    ASSERT_TRUE(readEdited.model.has_value());
    EXPECT_FALSE(readEdited.model->masters[0].bytes.has_value());
}
