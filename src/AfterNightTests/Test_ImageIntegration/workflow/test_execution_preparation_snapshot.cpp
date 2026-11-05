// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/core/RegistrationMetricsWriter.h"

namespace
{
void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(bytes), bytes.size());
}

/// A case's own temporary directory.
class TempDirFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tempDir.isValid()) << "Failed to create temporary directory";
    }

    [[nodiscard]] QString tempPath() const
    {
        return m_tempDir.path();
    }

    QTemporaryDir m_tempDir;
};

class P119ExecutionPreparationSnapshotTest : public TempDirFixture
{
};

class P120ExecutionPreparationSnapshotTest : public TempDirFixture
{
};
} // namespace

TEST_F(P120ExecutionPreparationSnapshotTest, RejectsOversizedNormalizationMetadataBeforeReadAll)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|oversized-sidecar")}};
    group.outputFilename = QStringLiteral("stacked.fit");
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.normalizationCoefficientsArtifactPath = QDir(tempPath()).filePath(QStringLiteral("coefficients.json"));
    manifest.integrationGroups.push_back(group);

    QFile metadata(*group.normalizationCoefficientsArtifactPath + QStringLiteral(".meta.json"));
    ASSERT_TRUE(metadata.open(QIODevice::WriteOnly));
    ASSERT_TRUE(metadata.resize(ImageIntegration::schema::kMetadataSidecarMaxBytes + 1));
    metadata.close();

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    const auto* parsed = snapshot->normalizationMetadata(group.groupKey);
    ASSERT_NE(parsed, nullptr);
    EXPECT_FALSE(parsed->parsed);
}

TEST_F(P120ExecutionPreparationSnapshotTest, CancellationStopsSnapshotCapture)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    EXPECT_THROW((void)ExecutionPreparationSnapshot::build(manifest, [] { return true; }), std::runtime_error);
}

TEST_F(P120ExecutionPreparationSnapshotTest, PinsNormalizationCoefficientDigestWithoutRetainingPayload)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|coefficient-residency")}};
    group.outputFilename = QStringLiteral("stacked.fit");
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.normalizationCoefficientsArtifactPath =
        QDir(tempPath()).filePath(QStringLiteral("resident_coefficients.json"));
    manifest.integrationGroups.push_back(group);

    // The snapshot pins the coefficient file by its content digest and never parses it, so any payload will do.
    const QByteArray coefficients = QByteArrayLiteral("{\"mode\":\"global\",\"frames\":[{\"frameId\":\"light.fit\","
                                                      "\"offset\":[0.0],\"scale\":[1.0]}],\"referenceMedian\":[0.5]}");
    writeBytes(*group.normalizationCoefficientsArtifactPath, coefficients);
    writeBytes(*group.normalizationCoefficientsArtifactPath + QStringLiteral(".meta.json"), QByteArrayLiteral("{}"));

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);

    const auto digest = snapshot->normalizationCoefficientDigest(group.groupKey);
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(*digest, ArtifactTracker::contentDigestForFile(*group.normalizationCoefficientsArtifactPath));
    // The document's bytes are the only ones the capture hashed, and its diagnostics say so.
    EXPECT_EQ(snapshot->diagnostics().bytesHashed, static_cast<std::uint64_t>(coefficients.size()));

    writeBytes(*group.normalizationCoefficientsArtifactPath, QByteArrayLiteral("{}"));
    EXPECT_EQ(snapshot->normalizationCoefficientDigest(group.groupKey), digest);
}

TEST_F(P120ExecutionPreparationSnapshotTest, StackedArtifactHashUsesOnlyGroupCalibrationAssignments)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.calibrateLightsInMemory = true;

    const FrameId first_light(QDir(tempPath()).filePath(QStringLiteral("first_light.fit")));
    const FrameId second_light(QDir(tempPath()).filePath(QStringLiteral("second_light.fit")));

    WorkflowManifest::IntegrationGroup first_group;
    first_group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|first")}};
    first_group.outputFilename = QStringLiteral("stacked_first.fit");
    first_group.lightFiles = {first_light};
    manifest.integrationGroups.push_back(first_group);

    WorkflowManifest::IntegrationGroup second_group;
    second_group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|second")}};
    second_group.outputFilename = QStringLiteral("stacked_second.fit");
    second_group.lightFiles = {second_light};
    manifest.integrationGroups.push_back(second_group);

    WorkflowManifest::LightsCalibrationParams first_assignment;
    first_assignment.lightPath = first_light;
    WorkflowManifest::LightsCalibrationParams second_assignment;
    second_assignment.lightPath = second_light;
    manifest.lightsCalibration = {first_assignment, second_assignment};

    const auto original_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    const QString original_hash = computeStackedLightArtifactHash(manifest, first_group, QStringLiteral("inputs"), {},
                                                                  {}, {}, original_snapshot.get());

    WorkflowManifest unrelated_changed = manifest;
    unrelated_changed.lightsCalibration.back().settings.pedestal = 100.0;
    const auto unrelated_snapshot = ExecutionPreparationSnapshot::build(unrelated_changed, manifest);
    EXPECT_EQ(original_hash, computeStackedLightArtifactHash(unrelated_changed, first_group, QStringLiteral("inputs"),
                                                             {}, {}, {}, unrelated_snapshot.get()));

    WorkflowManifest group_changed = manifest;
    group_changed.lightsCalibration.front().settings.pedestal = 100.0;
    const auto group_snapshot = ExecutionPreparationSnapshot::build(group_changed, manifest);
    EXPECT_NE(original_hash, computeStackedLightArtifactHash(group_changed, first_group, QStringLiteral("inputs"), {},
                                                             {}, {}, group_snapshot.get()));
}

TEST_F(P120ExecutionPreparationSnapshotTest, HashesPreindexedCalibrationAssignmentsWithoutScanningUnrelatedRows)
{
    const FrameId selectedLight(QDir(tempPath()).filePath(QStringLiteral("selected.fit")));
    WorkflowManifest::LightsCalibrationParams selected;
    selected.lightPath = selectedLight;
    selected.settings.pedestal = 42.0;

    std::vector<WorkflowManifest::LightsCalibrationParams> allAssignments;
    allAssignments.reserve(10'001);
    allAssignments.push_back(selected);
    for (int index = 0; index < 10'000; ++index)
    {
        WorkflowManifest::LightsCalibrationParams unrelated;
        unrelated.lightPath = FrameId(QDir(tempPath()).filePath(QStringLiteral("unrelated_%1.fit").arg(index)));
        unrelated.settings.pedestal = index;
        allAssignments.push_back(std::move(unrelated));
    }

    const std::vector<const WorkflowManifest::LightsCalibrationParams*> indexedAssignments{&allAssignments.front()};
    EXPECT_EQ(computeLightsCalibrationHashForAssignments(indexedAssignments),
              computeLightsCalibrationHashForSubset(allAssignments, {selectedLight}));

    allAssignments.back().settings.pedestal = -1.0;
    EXPECT_EQ(computeLightsCalibrationHashForAssignments(indexedAssignments),
              computeLightsCalibrationHashForSubset(allAssignments, {selectedLight}));
}

TEST_F(P120ExecutionPreparationSnapshotTest, CachesDeterministicSubsetAndSkipDecisionFacts)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    const FrameId first_light(QDir(tempPath()).filePath(QStringLiteral("fact_first.fit")));
    const FrameId second_light(QDir(tempPath()).filePath(QStringLiteral("fact_second.fit")));
    const FrameId unmatched_light(QDir(tempPath()).filePath(QStringLiteral("fact_unmatched.fit")));
    const FrameId bias(QDir(tempPath()).filePath(QStringLiteral("master_bias.fit")));
    const FrameId dark(QDir(tempPath()).filePath(QStringLiteral("master_dark.fit")));
    const FrameId flat(QDir(tempPath()).filePath(QStringLiteral("master_flat.fit")));

    WorkflowManifest::LightsCalibrationParams first_assignment;
    first_assignment.lightPath = first_light;
    first_assignment.biasPath = bias;
    first_assignment.darkPath = dark;
    WorkflowManifest::LightsCalibrationParams second_assignment;
    second_assignment.lightPath = second_light;
    second_assignment.flatPath = flat;
    manifest.lightsCalibration = {first_assignment, second_assignment};

    WorkflowManifest::IntegrationGroup matching_group;
    matching_group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|facts")}};
    matching_group.outputFilename = QStringLiteral("stacked_facts.fit");
    matching_group.lightFiles = {second_light, unmatched_light, first_light};
    manifest.integrationGroups.push_back(matching_group);

    WorkflowManifest::IntegrationGroup empty_group;
    empty_group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|empty")}};
    empty_group.outputFilename = QStringLiteral("stacked_empty.fit");
    empty_group.lightFiles = {unmatched_light};
    manifest.integrationGroups.push_back(empty_group);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    const auto* facts = snapshot->calibrationSubsetFacts(matching_group.lightFiles);
    ASSERT_NE(facts, nullptr);
    EXPECT_EQ(facts->matchingAssignmentCount, 2);
    EXPECT_EQ(facts->subsetHash,
              computeLightsCalibrationHashForSubset(manifest.lightsCalibration, matching_group.lightFiles));
    EXPECT_FALSE(facts->allCalibratedArtifactsReusable);
    EXPECT_TRUE(facts->dependencyPaths.contains(bias.canonicalPath()));
    EXPECT_TRUE(facts->dependencyPaths.contains(dark.canonicalPath()));
    EXPECT_TRUE(facts->dependencyPaths.contains(flat.canonicalPath()));

    std::vector<FrameId> reordered_lights = matching_group.lightFiles;
    std::reverse(reordered_lights.begin(), reordered_lights.end());
    EXPECT_EQ(snapshot->calibrationSubsetFacts(reordered_lights), facts);
    EXPECT_EQ(computeLightsCalibrationHashForSubset(manifest.lightsCalibration, reordered_lights, snapshot.get()),
              facts->subsetHash);

    const auto* empty_facts = snapshot->calibrationSubsetFacts(empty_group.lightFiles);
    ASSERT_NE(empty_facts, nullptr);
    EXPECT_EQ(empty_facts->matchingAssignmentCount, 0);
    EXPECT_TRUE(empty_facts->subsetHash.isEmpty());
    EXPECT_EQ(snapshot->diagnostics().calibrationAssignmentVisits, 4u);

    const ExecutionPreparationDiagnostics before = snapshot->diagnostics();
    ExecutionPlanner planner;
    const ExecutionPlan plan = planner.buildPlan(manifest, *snapshot);
    (void)buildExecutionPlanDecisions(manifest, plan, *snapshot);
    EXPECT_EQ(snapshot->diagnostics().fileStatOperations, before.fileStatOperations);
    EXPECT_EQ(snapshot->diagnostics().databaseOpenOperations, before.databaseOpenOperations);
    EXPECT_EQ(snapshot->diagnostics().parsedRecords, before.parsedRecords);
    EXPECT_EQ(snapshot->diagnostics().strictHeaderReads, before.strictHeaderReads);
    EXPECT_EQ(snapshot->diagnostics().bytesHashed, before.bytesHashed);
    EXPECT_EQ(snapshot->diagnostics().calibrationAssignmentVisits, before.calibrationAssignmentVisits);
}

TEST_F(P119ExecutionPreparationSnapshotTest, BuildsAnImmutableSnapshotWithDeterministicFileStats)
{
    const QString first_path = QDir(tempPath()).filePath(QStringLiteral("light_001.fit"));
    const QString second_path = QDir(tempPath()).filePath(QStringLiteral("light_002.fit"));
    writeBytes(first_path, QByteArray("first"));
    writeBytes(second_path, QByteArray("second"));

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|file-stats")}};
    group.outputFilename = QStringLiteral("stacked_file_stats.fit");
    group.lightFiles = {FrameId(first_path), FrameId(second_path)};
    group.stacking.normalizationMode = NormalizationMode::None;
    manifest.integrationGroups.push_back(group);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest);
    static_assert(std::is_same_v<decltype(snapshot), const std::shared_ptr<const ExecutionPreparationSnapshot>>);

    ASSERT_NE(snapshot, nullptr);
    ASSERT_NE(snapshot->fileStamp(first_path), nullptr);
    EXPECT_TRUE(snapshot->fileStamp(first_path)->exists);
    EXPECT_EQ(snapshot->fileStamp(first_path)->sizeBytes, 5);
    EXPECT_EQ(snapshot->diagnostics().fileStatOperations, 3u);
    EXPECT_EQ(snapshot->diagnostics().databaseOpenOperations, 0u);
    EXPECT_EQ(snapshot->diagnostics().parsedRecords, 0u);

    writeBytes(first_path, QByteArray("changed bytes"));
    EXPECT_EQ(snapshot->fileStamp(first_path)->sizeBytes, 5);
}

TEST_F(P119ExecutionPreparationSnapshotTest, DetectsThatCapturedFactsChangedBeforeExecutionLaunch)
{
    const QString input_path = QDir(tempPath()).filePath(QStringLiteral("bias_001.fit"));
    const QString output_path = QDir(tempPath()).filePath(QStringLiteral("master_bias.fit"));
    writeBytes(input_path, QByteArray("input"));
    writeBytes(output_path, QByteArray("first"));

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("Bias");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|captured-facts")};
    group.outputFilename = QStringLiteral("master_bias.fit");
    group.inputFiles = {FrameId(input_path)};
    bias_stage.groups.push_back(group);
    manifest.biasStage = bias_stage;

    const QString manifest_hash = computeCalibrationGroupHash(manifest, group);
    ArtifactTracker tracker(tempPath());
    ASSERT_TRUE(tracker.recordArtifact(output_path, manifest_hash));

    const QDateTime original_mtime = QFileInfo(output_path).lastModified();
    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    ASSERT_TRUE(snapshot->isArtifactValid(output_path, manifest_hash, PreparationArtifactValidation::StrictContent));
    EXPECT_EQ(snapshot->diagnostics().fileStatOperations, 2u);
    EXPECT_EQ(snapshot->diagnostics().databaseOpenOperations, 1u);
    EXPECT_EQ(snapshot->diagnostics().parsedRecords, 2u);
    // A master is decided from its platform file identity, or the stamp its header carries when
    // that identity moved; the snapshot reads no payload, and this master was not moved.
    EXPECT_EQ(snapshot->diagnostics().strictHeaderReads, 0u);
    EXPECT_EQ(snapshot->diagnostics().bytesHashed, 0u);

    ExecutionPlanner planner;
    const ExecutionPlan plan = planner.buildPlan(manifest, *snapshot);
    EXPECT_TRUE(plan.isEmpty());
    const auto decisions = buildExecutionPlanDecisions(manifest, plan, *snapshot);
    ASSERT_FALSE(decisions.empty());
    EXPECT_EQ(decisions.front().action, ExecutionPlanDecision::Action::Reuse);

    // The bias stage decides its skip over the same snapshot.
    BiasStackingStage stage;
    stage.setExecutionPreparationSnapshot(snapshot);
    EXPECT_TRUE(stage.shouldSkip(manifest));
    EXPECT_FALSE(stage.skipReason(manifest).isEmpty());

    writeBytes(output_path, QByteArray("other"));
    QFile output_file(output_path);
    ASSERT_TRUE(output_file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(output_file.setFileTime(original_mtime, QFileDevice::FileModificationTime));
    output_file.close();
    EXPECT_FALSE(tracker.isArtifactValid(output_path, manifest_hash));

    EXPECT_FALSE(snapshot->matchesCurrentFacts());
    EXPECT_TRUE(snapshot->isArtifactValid(output_path, manifest_hash, PreparationArtifactValidation::StrictContent));
    EXPECT_TRUE(planner.buildPlan(manifest, *snapshot).isEmpty());
    EXPECT_TRUE(stage.shouldSkip(manifest));
}

TEST_F(P119ExecutionPreparationSnapshotTest, DetectsSameMetadataFileReplacementBeforeExecutionLaunch)
{
    const QString input_path = QDir(tempPath()).filePath(QStringLiteral("identity_input.fit"));
    const QString replacement_path = QDir(tempPath()).filePath(QStringLiteral("identity_replacement.fit"));
    writeBytes(input_path, QByteArray("first"));

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("Bias");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|identity")};
    group.outputFilename = QStringLiteral("identity_master.fit");
    group.inputFiles = {FrameId(input_path)};
    bias_stage.groups.push_back(group);
    manifest.biasStage = bias_stage;

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    ASSERT_TRUE(snapshot->matchesCurrentFacts());
    const QDateTime original_mtime = QFileInfo(input_path).lastModified();

    writeBytes(replacement_path, QByteArray("other"));
    QFile replacement(replacement_path);
    ASSERT_TRUE(replacement.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacement.setFileTime(original_mtime, QFileDevice::FileModificationTime));
    replacement.close();
    ASSERT_TRUE(QFile::remove(input_path));
    ASSERT_TRUE(QFile::rename(replacement_path, input_path));
    ASSERT_EQ(QFileInfo(input_path).size(), 5);
    ASSERT_EQ(QFileInfo(input_path).lastModified().toMSecsSinceEpoch(), original_mtime.toMSecsSinceEpoch());

    EXPECT_FALSE(snapshot->matchesCurrentFacts());
}

TEST_F(P119ExecutionPreparationSnapshotTest, FailsClosedWhenFileIdentityBecomesUnavailable)
{
    const QString inputPath = QDir(tempPath()).filePath(QStringLiteral("identity_availability.fit"));
    writeBytes(inputPath, QByteArray("stable"));

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("Bias");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|identity-availability")};
    group.outputFilename = QStringLiteral("identity_availability_master.fit");
    group.inputFiles = {FrameId(inputPath)};
    biasStage.groups.push_back(group);
    manifest.biasStage = biasStage;

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    const auto* stamp = snapshot->fileStamp(inputPath);
    ASSERT_NE(stamp, nullptr);
    if (stamp->identityToken.isEmpty())
    {
        GTEST_SKIP() << "Native file identity is unavailable on this platform.";
    }

    EXPECT_FALSE(snapshot->matchesCurrentFacts({}, [](const QString&) { return QString(); }));
}

TEST_F(P119ExecutionPreparationSnapshotTest, StrictArtifactRevalidationHonorsCancellation)
{
    const QString input_path = QDir(tempPath()).filePath(QStringLiteral("bias_cancel_input.fit"));
    const QString output_path = QDir(tempPath()).filePath(QStringLiteral("master_bias_cancel.fit"));
    writeBytes(input_path, QByteArray("input"));
    writeBytes(output_path, QByteArray(256 * 1024, 'm'));

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("Bias");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|cancel")};
    group.outputFilename = QFileInfo(output_path).fileName();
    group.inputFiles = {FrameId(input_path)};
    bias_stage.groups.push_back(group);
    manifest.biasStage = bias_stage;

    ArtifactTracker tracker(tempPath());
    ASSERT_TRUE(tracker.recordArtifact(output_path, computeCalibrationGroupHash(manifest, group)));
    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    ASSERT_NE(snapshot, nullptr);
    // A master is decided from its platform file identity, or the stamp its header carries when
    // that identity moved; the check before launch is a stat per captured file.
    ASSERT_EQ(snapshot->diagnostics().strictHeaderReads, 0u);

    int cancellation_checks = 0;
    EXPECT_FALSE(snapshot->matchesCurrentFacts([&]() {
        ++cancellation_checks;
        return true;
    }));
    EXPECT_GT(cancellation_checks, 0);
}

TEST_F(P119ExecutionPreparationSnapshotTest, UsesFrameIdentityForWindowsPathIndices)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::LightsCalibrationParams params;
    params.lightPath = FrameId(QStringLiteral("C:/Images/LIGHT_001.fit"));
    params.biasPath = FrameId(QStringLiteral("C:/Calibration/MASTER_BIAS.fit"));
    manifest.lightsCalibration.push_back(params);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|identity")}};
    group.lightFiles.push_back(FrameId(QStringLiteral("c:/images/light_001.fit")));
    manifest.integrationGroups.push_back(group);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    ASSERT_NE(snapshot, nullptr);

    const auto* assignment = snapshot->calibrationAssignment(FrameId(QStringLiteral("c:\\IMAGES\\light_001.fit")));
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->biasPath, FrameId(QStringLiteral("c:/calibration/master_bias.fit")));

    const auto* subset = snapshot->calibrationSubsetFacts(group.lightFiles);
    ASSERT_NE(subset, nullptr);
    EXPECT_EQ(subset->matchingAssignmentCount, 1u);
    EXPECT_EQ(computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles), subset->subsetHash);
}

TEST_F(P119ExecutionPreparationSnapshotTest, TenThousandFrameNoOpAndIncrementalPreparationHaveExactWorkCounters)
{
    constexpr int frame_count = 10000;
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.calibrateLightsInMemory = true;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|ten-thousand")}};
    group.outputFilename = QStringLiteral("stacked_ten_thousand.fit");
    group.skipIfExists = true;
    group.stacking.normalizationMode = NormalizationMode::None;
    group.lightFiles.reserve(frame_count);
    manifest.lightsCalibration.reserve(frame_count);
    for (int index = 0; index < frame_count; ++index)
    {
        const FrameId frame(QDir(tempPath()).filePath(QStringLiteral("light_%1.fit").arg(index)));
        group.lightFiles.push_back(frame);
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = frame;
        manifest.lightsCalibration.push_back(params);
    }
    manifest.integrationGroups.push_back(group);

    const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
    writeBytes(output_path, QByteArray("stack"));
    std::vector<FrameId> stack_inputs = group.lightFiles;
    std::sort(stack_inputs.begin(), stack_inputs.end());
    const auto hashing_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    const QString inputs_fingerprint = computeFrameFileStampFingerprint(stack_inputs, hashing_snapshot.get());
    ArtifactTracker tracker(tempPath());
    ASSERT_TRUE(
        tracker.recordArtifact(output_path, computeStackedLightArtifactHash(manifest, group, inputs_fingerprint, {}, {},
                                                                            {}, hashing_snapshot.get())));

    const auto recorded_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    const QString captured_inputs_fingerprint = computeFrameFileStampFingerprint(stack_inputs, recorded_snapshot.get());
    ASSERT_TRUE(tracker.recordArtifact(output_path,
                                       computeStackedLightArtifactHash(manifest, group, captured_inputs_fingerprint, {},
                                                                       {}, {}, recorded_snapshot.get())));
    const auto no_op_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    const ExecutionPreparationDiagnostics expected{10001u, 1u, 2u, 0u, 10000u, 10000u};
    EXPECT_EQ(no_op_snapshot->diagnostics().fileStatOperations, expected.fileStatOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().databaseOpenOperations, expected.databaseOpenOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().parsedRecords, expected.parsedRecords);
    EXPECT_EQ(no_op_snapshot->diagnostics().strictHeaderReads, expected.strictHeaderReads);
    EXPECT_EQ(no_op_snapshot->diagnostics().calibrationAssignmentsIndexed, expected.calibrationAssignmentsIndexed);

    ExecutionPlanner planner;
    const ExecutionPlan no_op_plan = planner.buildPlan(manifest, *no_op_snapshot);
    EXPECT_TRUE(no_op_plan.isEmpty()) << no_op_plan.toSummaryText().toStdString();
    (void)buildExecutionPlanDecisions(manifest, no_op_plan, *no_op_snapshot);
    EXPECT_EQ(no_op_snapshot->diagnostics().fileStatOperations, expected.fileStatOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().databaseOpenOperations, expected.databaseOpenOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().parsedRecords, expected.parsedRecords);

    WorkflowManifest changed = manifest;
    changed.integrationGroups.front().stacking.sigmaHigh += 0.5;
    const auto incremental_snapshot = ExecutionPreparationSnapshot::build(changed, manifest);
    const ExecutionPlan incremental_plan = planner.buildPlan(changed, *incremental_snapshot);
    EXPECT_EQ(incremental_plan.mode, ExecutionMode::Incremental);
    ASSERT_EQ(incremental_plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(incremental_plan.integrationGroupsToProcess.front(), group.groupKey);
    EXPECT_EQ(incremental_snapshot->diagnostics().fileStatOperations, expected.fileStatOperations);
    EXPECT_EQ(incremental_snapshot->diagnostics().databaseOpenOperations, expected.databaseOpenOperations);
    EXPECT_EQ(incremental_snapshot->diagnostics().parsedRecords, expected.parsedRecords);
    EXPECT_EQ(incremental_snapshot->diagnostics().calibrationAssignmentsIndexed,
              expected.calibrationAssignmentsIndexed);
}

TEST_F(P120ExecutionPreparationSnapshotTest,
       TenThousandFrameMultiGroupPreparationVisitsEachMembershipOnceAcrossPlanAndDecisions)
{
    constexpr int group_count = 100;
    constexpr int frames_per_group = 100;
    constexpr int frame_count = group_count * frames_per_group;

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.calibrateLightsInMemory = true;
    manifest.stageSequence = {QStringLiteral("LightsCalibration"), QStringLiteral("LightsStacking")};
    manifest.integrationGroups.reserve(group_count);
    manifest.lightsCalibration.reserve(frame_count);

    for (int group_index = 0; group_index < group_count; ++group_index)
    {
        WorkflowManifest::IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|ten-thousand|%1").arg(group_index)}};
        group.outputFilename = QStringLiteral("stacked_ten_thousand_%1.fit").arg(group_index);
        group.skipIfExists = true;
        group.stacking.normalizationMode = NormalizationMode::None;
        group.lightFiles.reserve(frames_per_group);

        for (int frame_index = 0; frame_index < frames_per_group; ++frame_index)
        {
            const int index = group_index * frames_per_group + frame_index;
            const FrameId frame(QDir(tempPath()).filePath(QStringLiteral("light_%1.fit").arg(index)));
            group.lightFiles.push_back(frame);
            WorkflowManifest::LightsCalibrationParams params;
            params.lightPath = frame;
            manifest.lightsCalibration.push_back(params);
        }

        writeBytes(QDir(tempPath()).filePath(group.outputFilename), QByteArray("stack"));
        manifest.integrationGroups.push_back(std::move(group));
    }

    const auto hashing_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    ArtifactTracker tracker(tempPath());
    for (const auto& group : manifest.integrationGroups)
    {
        std::vector<FrameId> stack_inputs = group.lightFiles;
        std::sort(stack_inputs.begin(), stack_inputs.end());
        const QString inputs_fingerprint = computeFrameFileStampFingerprint(stack_inputs, hashing_snapshot.get());
        const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
        ASSERT_TRUE(
            tracker.recordArtifact(output_path, computeStackedLightArtifactHash(manifest, group, inputs_fingerprint, {},
                                                                                {}, {}, hashing_snapshot.get())));
    }

    const ExecutionPreparationDiagnostics expected{10100u, 1u, 101u, 0u, 10000u, 10000u};
    const auto no_op_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest);
    EXPECT_EQ(no_op_snapshot->diagnostics().fileStatOperations, expected.fileStatOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().databaseOpenOperations, expected.databaseOpenOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().parsedRecords, expected.parsedRecords);
    EXPECT_EQ(no_op_snapshot->diagnostics().strictHeaderReads, expected.strictHeaderReads);
    EXPECT_EQ(no_op_snapshot->diagnostics().calibrationAssignmentsIndexed, expected.calibrationAssignmentsIndexed);
    EXPECT_EQ(no_op_snapshot->diagnostics().calibrationAssignmentVisits, expected.calibrationAssignmentVisits);

    ExecutionPlanner planner;
    const ExecutionPlan no_op_plan = planner.buildPlan(manifest, *no_op_snapshot);
    EXPECT_TRUE(no_op_plan.isEmpty()) << no_op_plan.toSummaryText().toStdString();
    (void)buildExecutionPlanDecisions(manifest, no_op_plan, *no_op_snapshot);
    EXPECT_EQ(no_op_snapshot->diagnostics().fileStatOperations, expected.fileStatOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().databaseOpenOperations, expected.databaseOpenOperations);
    EXPECT_EQ(no_op_snapshot->diagnostics().parsedRecords, expected.parsedRecords);
    EXPECT_EQ(no_op_snapshot->diagnostics().strictHeaderReads, expected.strictHeaderReads);
    EXPECT_EQ(no_op_snapshot->diagnostics().calibrationAssignmentVisits, expected.calibrationAssignmentVisits);

    WorkflowManifest changed = manifest;
    changed.integrationGroups.at(42).stacking.sigmaHigh += 0.5;
    const auto incremental_snapshot = ExecutionPreparationSnapshot::build(changed, manifest);
    const ExecutionPlan incremental_plan = planner.buildPlan(changed, *incremental_snapshot);
    EXPECT_EQ(incremental_plan.mode, ExecutionMode::Incremental);
    ASSERT_EQ(incremental_plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(incremental_plan.integrationGroupsToProcess.front(), changed.integrationGroups.at(42).groupKey);
    (void)buildExecutionPlanDecisions(changed, incremental_plan, *incremental_snapshot);
    EXPECT_EQ(incremental_snapshot->diagnostics().fileStatOperations, expected.fileStatOperations);
    EXPECT_EQ(incremental_snapshot->diagnostics().databaseOpenOperations, expected.databaseOpenOperations);
    EXPECT_EQ(incremental_snapshot->diagnostics().parsedRecords, expected.parsedRecords);
    EXPECT_EQ(incremental_snapshot->diagnostics().strictHeaderReads, expected.strictHeaderReads);
    EXPECT_EQ(incremental_snapshot->diagnostics().calibrationAssignmentsIndexed,
              expected.calibrationAssignmentsIndexed);
    EXPECT_EQ(incremental_snapshot->diagnostics().calibrationAssignmentVisits, expected.calibrationAssignmentVisits);
}

TEST_F(P119ExecutionPreparationSnapshotTest, DetectsPreviousManifestReplacementBeforeLaunch)
{
    WorkflowManifest previous;
    previous.outputDirectory = tempPath();
    previous.processLabel = QStringLiteral("captured");

    WorkflowRunStateStore store(tempPath());
    ASSERT_TRUE(store.saveLastSuccessfulManifest(previous));

    WorkflowManifest current = previous;
    current.processLabel = QStringLiteral("current");
    const auto snapshot = ExecutionPreparationSnapshot::build(current);
    ASSERT_TRUE(snapshot->previousManifest().has_value());
    EXPECT_EQ(snapshot->previousManifest()->processLabel, QStringLiteral("captured"));
    ASSERT_TRUE(snapshot->matchesCurrentFacts());

    WorkflowManifest replacement = previous;
    replacement.processLabel = QStringLiteral("replacement with different serialized bytes");
    ASSERT_TRUE(store.saveLastSuccessfulManifest(replacement));

    EXPECT_FALSE(snapshot->matchesCurrentFacts());
}

TEST_F(P119ExecutionPreparationSnapshotTest, CapturesDrizzleSourceGeometryOnlyThroughAHeaderReader)
{
    const FrameId first_light(QDir(tempPath()).filePath(QStringLiteral("drizzle_first.fit")));
    const FrameId second_light(QDir(tempPath()).filePath(QStringLiteral("drizzle_second.fit")));

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|drizzle-geometry")}};
    group.outputFilename = QStringLiteral("stacked_drizzle_geometry.fit");
    group.lightFiles = {first_light, second_light};
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    manifest.integrationGroups.push_back(group);

    // Without a reader no header is read, so no geometry is known and no drizzle artifact can be reused.
    const auto unread_snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    const auto* unread = unread_snapshot->drizzleSourceGeometry(first_light);
    ASSERT_NE(unread, nullptr);
    EXPECT_FALSE(unread->metadataProbeSucceeded);
    EXPECT_EQ(unread->sourceWidth, 0u);
    EXPECT_EQ(unread->sourceHeight, 0u);

    auto stated_width = std::make_shared<std::uint32_t>(40u);
    auto reads = std::make_shared<int>(0);
    const ImageIntegration::FrameHeaderReader reader =
        [stated_width, reads](const QString&) -> std::optional<ImageIntegration::FrameHeaderFacts> {
        ++*reads;
        return ImageIntegration::FrameHeaderFacts{*stated_width, 24u, std::nullopt};
    };

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt, {}, reader);
    const auto* facts = snapshot->drizzleSourceGeometry(second_light);
    ASSERT_NE(facts, nullptr);
    EXPECT_TRUE(facts->metadataProbeSucceeded);
    EXPECT_EQ(facts->sourceWidth, 40u);
    EXPECT_EQ(facts->sourceHeight, 24u);
    EXPECT_FALSE(facts->inheritedSourceDistortion.has_value());
    EXPECT_EQ(facts->inheritedSourceDistortionFingerprint,
              stage_utils::fingerprintSourceDistortionHex(stage_utils::SourceDistortionPolicy::InheritMetadata,
                                                          std::nullopt));
    EXPECT_GE(*reads, 2);

    // The snapshot reads the headers again through the same reader when it is asked whether its facts still hold.
    ASSERT_TRUE(snapshot->matchesCurrentFacts());
    *stated_width = 48u;
    EXPECT_FALSE(snapshot->matchesCurrentFacts());
}

TEST_F(P119ExecutionPreparationSnapshotTest, LoadsOnlyRequestedRegistrationGroupsAndIndexesLatestVersions)
{
    const QString database_path = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    an::ui::RegistrationMetricsWriter writer(database_path, QStringLiteral("SnapshotRelevantGroupsWriter"));
    ASSERT_TRUE(writer.initialize());

    const auto add_version = [&](const QString& group_key, const int version, const QString& frame_path) {
        QString error;
        an::ui::RegistrationGroupState state;
        state.groupKey = group_key;
        state.settingsVersion = version;
        state.detectionHash = QStringLiteral("detect-%1").arg(version);
        state.registrationHash = QStringLiteral("register-%1").arg(version);
        state.referenceFrameId = QStringLiteral("");
        state.lightFilesHash = QStringLiteral("lights-%1").arg(version);
        state.calibrationHash = QStringLiteral("");
        state.outputWcsJson = QStringLiteral("{}");
        state.updatedAt = version;
        ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();

        an::ui::RegistrationMetricRecord metric;
        metric.groupKey = group_key;
        metric.settingsVersion = version;
        metric.frameId = FrameId(frame_path);
        metric.metrics.succeeded = true;
        metric.outputPath = QStringLiteral("");
        metric.transformJson = QStringLiteral("");
        metric.updatedAt = version;
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

        an::ui::RegistrationStarCacheEntry stars;
        stars.groupKey = group_key;
        stars.detectionHash = state.detectionHash;
        stars.frameId = metric.frameId;
        stars.inputPath = frame_path;
        stars.inputMtime = version;
        stars.starsBlob = QByteArray("");
        stars.updatedAt = version;
        ASSERT_TRUE(writer.upsertStarCache(stars, &error)) << error.toStdString();
    };

    const QString requested_group = QStringLiteral("reg|snapshot-requested");
    const QString requested_frame = QDir(tempPath()).filePath(QStringLiteral("requested.fit"));
    add_version(requested_group, 1, requested_frame);
    add_version(requested_group, 2, requested_frame);
    for (int index = 0; index < 32; ++index)
    {
        add_version(QStringLiteral("reg|unrelated-%1").arg(index), 1,
                    QDir(tempPath()).filePath(QStringLiteral("unrelated-%1.fit").arg(index)));
    }

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{requested_group};
    group.lightFiles = {FrameId(requested_frame)};
    manifest.registrationGroups.push_back(group);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    ASSERT_TRUE(snapshot->registrationDatabaseUsable());
    ASSERT_NE(snapshot->registrationGroupState(requested_group, 1), nullptr);
    ASSERT_NE(snapshot->registrationGroupState(requested_group, 2), nullptr);
    ASSERT_NE(snapshot->latestRegistrationGroupState(requested_group), nullptr);
    EXPECT_EQ(snapshot->latestRegistrationGroupState(requested_group)->settingsVersion, 2);
    EXPECT_EQ(snapshot->resolveRegistrationSettingsVersion(requested_group, 1), 1);
    EXPECT_EQ(snapshot->resolveRegistrationSettingsVersion(requested_group, 99), 2);
    EXPECT_EQ(snapshot->diagnostics().parsedRecords, 6u);
}

TEST_F(P119ExecutionPreparationSnapshotTest, StarCacheMtimeUsesNewestIdentityPayloadRegardlessOfSqlRowOrder)
{
    const QString database_path = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    const QString group_key = QStringLiteral("reg|snapshot-star-identity");
    const QString detection_hash = QStringLiteral("det-snapshot-star-identity");
    const FrameId current_frame(QStringLiteral("C:/Current/Snapshot/Light.fit"));
    const FrameId alias_frame(QStringLiteral("C:/current/snapshot/light.fit"));
    const QString alias_stored_frame = QStringLiteral("c:\\current\\snapshot\\light.fit");

    an::ui::RegistrationMetricsWriter writer(database_path, QStringLiteral("SnapshotStarIdentityWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationGroupState state;
    state.groupKey = group_key;
    state.settingsVersion = 1;
    state.detectionHash = detection_hash;
    state.registrationHash = QStringLiteral("registration-snapshot-star-identity");
    state.lightFilesHash = QStringLiteral("lights-snapshot-star-identity");
    state.updatedAt = 300;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();

    an::ui::RegistrationStarCacheEntry current;
    current.frameId = current_frame;
    current.groupKey = group_key;
    current.detectionHash = detection_hash;
    current.inputPath = current_frame.canonicalPath();
    current.inputMtime = 222;
    current.starsBlob = QByteArray("current");
    current.updatedAt = 200;
    ASSERT_TRUE(writer.upsertStarCache(current, &error)) << error.toStdString();

    an::ui::RegistrationStarCacheEntry alias = current;
    alias.frameId = alias_frame;
    alias.inputPath = alias_frame.canonicalPath();
    alias.inputMtime = 111;
    alias.starsBlob = QByteArray("alias");
    alias.updatedAt = 100;
    ASSERT_TRUE(writer.upsertStarCache(alias, &error)) << error.toStdString();

    an::ui::RegistrationMetricRecord current_metric;
    current_metric.frameId = current_frame;
    current_metric.groupKey = group_key;
    current_metric.settingsVersion = 1;
    current_metric.metrics.succeeded = true;
    current_metric.outputPath = QStringLiteral("current-registered.fit");
    current_metric.updatedAt = 200;
    ASSERT_TRUE(writer.upsertMetric(current_metric, &error)) << error.toStdString();
    an::ui::RegistrationMetricRecord alias_metric = current_metric;
    alias_metric.frameId = alias_frame;
    alias_metric.metrics.succeeded = false;
    alias_metric.outputPath = QStringLiteral("alias-registered.fit");
    alias_metric.updatedAt = 100;
    ASSERT_TRUE(writer.upsertMetric(alias_metric, &error)) << error.toStdString();

    core::SqliteConnection rewrite_connection(database_path, QStringLiteral("SnapshotStarIdentityRewriter"));
    ASSERT_TRUE(rewrite_connection.open(&error)) << error.toStdString();
    QSqlQuery rewrite(rewrite_connection.db());
    rewrite.prepare(QStringLiteral("UPDATE star_detection_cache SET frame_id = :stored_frame_id "
                                   "WHERE frame_id = :current_frame_id AND group_key = :group_key "
                                   "AND detection_hash = :detection_hash"));
    rewrite.bindValue(QStringLiteral(":stored_frame_id"), alias_stored_frame);
    rewrite.bindValue(QStringLiteral(":current_frame_id"), alias_frame.canonicalPath());
    rewrite.bindValue(QStringLiteral(":group_key"), group_key);
    rewrite.bindValue(QStringLiteral(":detection_hash"), detection_hash);
    ASSERT_TRUE(rewrite.exec()) << rewrite.lastError().text().toStdString();
    ASSERT_EQ(rewrite.numRowsAffected(), 1);
    rewrite.prepare(QStringLiteral("UPDATE registration_metrics SET frame_id = :stored_frame_id "
                                   "WHERE frame_id = :current_frame_id AND group_key = :group_key "
                                   "AND settings_version = 1"));
    rewrite.bindValue(QStringLiteral(":stored_frame_id"), alias_stored_frame);
    rewrite.bindValue(QStringLiteral(":current_frame_id"), alias_frame.canonicalPath());
    rewrite.bindValue(QStringLiteral(":group_key"), group_key);
    ASSERT_TRUE(rewrite.exec()) << rewrite.lastError().text().toStdString();
    ASSERT_EQ(rewrite.numRowsAffected(), 1);

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{group_key};
    group.lightFiles = {current_frame};
    manifest.registrationGroups.push_back(group);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    ASSERT_TRUE(snapshot->registrationDatabaseUsable());
    EXPECT_EQ(snapshot->starCacheMtime(group_key, detection_hash, current_frame), current.inputMtime);
    const auto* metrics = snapshot->registrationMetrics(group_key, 1);
    ASSERT_NE(metrics, nullptr);
    const auto selected_metric = metrics->constFind(current_frame);
    ASSERT_NE(selected_metric, metrics->cend());
    EXPECT_TRUE(selected_metric->succeeded);
    EXPECT_EQ(selected_metric->updatedAt, current_metric.updatedAt);
}
