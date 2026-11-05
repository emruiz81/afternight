// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include <array>
#include <cstdint>
#include <optional>

#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

namespace
{
void writeArtifactBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(bytes), bytes.size());
}

QString artifactDigest(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

std::optional<FinalCommittedArtifact> committedSnapshot(const QString& path, const CurrentRunCommittedDigest& digest)
{
    auto artifact = FinalCommittedArtifact::fromFile(path);
    if (!artifact.has_value() || artifact->digest().sha256() != digest.sha256())
    {
        return std::nullopt;
    }
    return artifact;
}

QString artifactsDatabasePath(const QString& outputDirectory)
{
    return QDir(outputDirectory).filePath(QStringLiteral(".afternight/image_integration/artifacts.db"));
}

std::optional<QString> storedArtifactProvenance(const QString& outputDirectory, const QString& path)
{
    const QString connection_name =
        QStringLiteral("ArtifactSchemaProbe-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    std::optional<QString> provenance;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
        db.setDatabaseName(artifactsDatabasePath(outputDirectory));
        if (db.open())
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT digest_provenance FROM artifacts WHERE path = :path"));
            query.bindValue(QStringLiteral(":path"), path);
            if (query.exec() && query.next() && !query.value(0).isNull())
            {
                provenance = query.value(0).toString();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name);
    return provenance;
}

bool executeArtifactSql(const QString& outputDirectory, const QString& sql)
{
    const QString connection_name =
        QStringLiteral("ArtifactSql-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool succeeded = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
        db.setDatabaseName(artifactsDatabasePath(outputDirectory));
        if (db.open())
        {
            QSqlQuery query(db);
            succeeded = query.exec(sql);
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name);
    return succeeded;
}

} // namespace

/// A fresh output directory per case, removed when the case ends.
class SmartSkippingFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tempDir.isValid()) << "Failed to create temporary directory";
    }

    QString tempPath() const
    {
        return m_tempDir.path();
    }

private:
    QTemporaryDir m_tempDir;
};

TEST_F(SmartSkippingFixture, ArtifactTrackerInvalidatesWhenRecordedFileIsTruncated)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("tracked_artifact.fit"));
    const QString manifestHash = QStringLiteral("same-manifest-hash");

    writeArtifactBytes(outputPath, QByteArray("complete artifact payload"));

    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, manifestHash);
    ASSERT_TRUE(tracker.isArtifactValid(outputPath, manifestHash));

    QFile truncated(outputPath);
    ASSERT_TRUE(truncated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    truncated.close();

    EXPECT_FALSE(tracker.isArtifactValid(outputPath, manifestHash));
}

TEST_F(SmartSkippingFixture, ArtifactTrackerInvalidatesWhenRecordedFileIsReplacedAtSamePath)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("replaced_artifact.fit"));
    const QString manifestHash = QStringLiteral("same-manifest-hash");
    const QByteArray original("original artifact payload");
    QByteArray replacement = original;
    replacement[0] = 'r';

    writeArtifactBytes(outputPath, original);
    const QDateTime recordedModified = QFileInfo(outputPath).lastModified();

    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, manifestHash);
    ASSERT_TRUE(tracker.isArtifactValid(outputPath, manifestHash));

    QFile replaced(outputPath);
    ASSERT_TRUE(replaced.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(replaced.write(replacement), replacement.size());
    replaced.close();

    QFile replacedMetadata(outputPath);
    ASSERT_TRUE(replacedMetadata.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacedMetadata.setFileTime(recordedModified.addSecs(60), QFileDevice::FileModificationTime));
    replacedMetadata.close();

    const QFileInfo replacedInfo(outputPath);
    ASSERT_EQ(replacedInfo.size(), original.size());
    ASSERT_NE(replacedInfo.lastModified().toMSecsSinceEpoch(), recordedModified.toMSecsSinceEpoch());
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, manifestHash));
}

TEST_F(SmartSkippingFixture, ArtifactTrackerInvalidatesWhenRecordedFileContentChangesWithSameSizeAndMtime)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("digest_tracked_artifact.fit"));
    const QString manifestHash = QStringLiteral("same-manifest-hash");
    const QByteArray original("original artifact payload");
    QByteArray replacement = original;
    replacement[replacement.size() - 1] = '!';

    writeArtifactBytes(outputPath, original);
    const QDateTime recordedModified = QFileInfo(outputPath).lastModified();

    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, manifestHash);
    ASSERT_TRUE(tracker.isArtifactValid(outputPath, manifestHash));

    writeArtifactBytes(outputPath, replacement);

    QFile replaced(outputPath);
    ASSERT_TRUE(replaced.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replaced.setFileTime(recordedModified, QFileDevice::FileModificationTime));
    replaced.close();

    const QFileInfo replacedInfo(outputPath);
    ASSERT_EQ(replacedInfo.size(), original.size());
    ASSERT_EQ(replacedInfo.lastModified().toMSecsSinceEpoch(), recordedModified.toMSecsSinceEpoch());
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, manifestHash));
}

TEST_F(SmartSkippingFixture, ArtifactHashCountersTrackExactBytesAndRepeatedReads)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("counter_tracked_artifact.fit"));
    const QByteArray payload(128 * 1024 + 17, 'x');
    writeArtifactBytes(outputPath, payload);

    const QString disabledDigest = ArtifactTracker::contentDigestForFile(outputPath);
    EXPECT_FALSE(disabledDigest.isEmpty());

    WorkflowExecutionCounterAccumulator counters;
    const QString instrumentedDigest = ArtifactTracker::contentDigestForFile(outputPath, &counters);
    EXPECT_EQ(instrumentedDigest, disabledDigest);
    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(payload.size()));

    // Recording and reusing an artifact must not add to that total: neither reads the payload.
    ArtifactTracker tracker(tempPath(), &counters);
    tracker.recordArtifact(outputPath, QStringLiteral("counter-manifest"));

    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(payload.size()));
    ASSERT_TRUE(tracker.isArtifactValid(outputPath, QStringLiteral("counter-manifest")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(payload.size()));
}

TEST_F(SmartSkippingFixture, RecordArtifactStoresProvenanceWithoutReadingThePayload)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("readback_verified.fit"));
    const QByteArray payload("existing recordArtifact payload");
    writeArtifactBytes(outputPath, payload);

    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);
    ASSERT_TRUE(tracker.recordArtifact(outputPath, QStringLiteral("readback-manifest")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);

    const auto provenance = storedArtifactProvenance(tempPath(), outputPath);
    ASSERT_TRUE(provenance.has_value());
    EXPECT_EQ(*provenance, QStringLiteral("readback_verified"));

    ASSERT_TRUE(tracker.isArtifactValid(outputPath, QStringLiteral("readback-manifest")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(SmartSkippingFixture, CurrentRunCommittedDigestRecordsWithoutRehashing)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("trusted_record.fit"));
    const QByteArray payload("committed current-run payload");
    writeArtifactBytes(outputPath, payload);

    const auto digest = CurrentRunCommittedDigest::fromSha256(artifactDigest(payload).toUpper(),
                                                              ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(digest->sha256(), artifactDigest(payload));

    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);
    const auto artifact = committedSnapshot(outputPath, *digest);
    ASSERT_TRUE(artifact.has_value());
    ASSERT_TRUE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("trusted-manifest")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);

    const auto provenance = storedArtifactProvenance(tempPath(), outputPath);
    ASSERT_TRUE(provenance.has_value());
    EXPECT_EQ(*provenance, QStringLiteral("current_run_committed"));
}

TEST_F(SmartSkippingFixture, CurrentRunCommittedDigestRejectsMalformedValues)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("malformed_digest.fit"));
    writeArtifactBytes(outputPath, QByteArray("malformed digest payload"));
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);

    const std::array malformed = {QString(), QString(63, QLatin1Char('a')), QString(65, QLatin1Char('a')),
                                  QString(64, QLatin1Char('g'))};
    for (const auto& value : malformed)
    {
        EXPECT_FALSE(
            CurrentRunCommittedDigest::fromSha256(value, ArtifactDigestProvenance::CurrentRunCommitted).has_value());
    }
    EXPECT_FALSE(tracker.hasArtifactRecord(outputPath));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);

    const QString valid(64, QLatin1Char('a'));
    EXPECT_FALSE(CurrentRunCommittedDigest::fromSha256(valid, ArtifactDigestProvenance::ReadbackVerified).has_value());
    EXPECT_FALSE(CurrentRunCommittedDigest::fromSha256(valid, ArtifactDigestProvenance::Unknown).has_value());
    EXPECT_FALSE(CurrentRunCommittedDigest::fromSha256(valid, static_cast<ArtifactDigestProvenance>(999)).has_value());

    const auto digest = CurrentRunCommittedDigest::fromSha256(valid, ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    const QString missingPath = QDir(tempPath()).filePath(QStringLiteral("missing.fit"));
    const auto missing_artifact = FinalCommittedArtifact::fromFile(missingPath);
    EXPECT_FALSE(missing_artifact.has_value());
    EXPECT_FALSE(tracker.hasArtifactRecord(missingPath));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(SmartSkippingFixture, CancelledTrustedDigestRecordDoesNotPublishOrReplace)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("cancelled_trusted_record.fit"));
    const QByteArray payload("unchanged committed payload");
    writeArtifactBytes(outputPath, payload);

    ArtifactTracker tracker(tempPath());
    ASSERT_TRUE(tracker.recordArtifact(outputPath, QStringLiteral("original-manifest")));
    const auto digest =
        CurrentRunCommittedDigest::fromSha256(artifactDigest(payload), ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    const auto artifact = committedSnapshot(outputPath, *digest);
    ASSERT_TRUE(artifact.has_value());

    int cancellationChecks = 0;
    EXPECT_FALSE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("replacement-manifest"),
                                                           [&cancellationChecks]() {
                                                               ++cancellationChecks;
                                                               return true;
                                                           }));
    EXPECT_GT(cancellationChecks, 0);
    EXPECT_TRUE(tracker.isArtifactValid(outputPath, QStringLiteral("original-manifest")));
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, QStringLiteral("replacement-manifest")));
}

TEST_F(SmartSkippingFixture, StaleCommittedSnapshotDoesNotReplaceExistingArtifactRecord)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("stale_committed_snapshot.fit"));
    const QByteArray payload("stable committed payload");
    writeArtifactBytes(outputPath, payload);

    ArtifactTracker tracker(tempPath());
    ASSERT_TRUE(tracker.recordArtifact(outputPath, QStringLiteral("original-manifest")));
    const QDateTime original_modified = QFileInfo(outputPath).lastModified();
    const auto digest =
        CurrentRunCommittedDigest::fromSha256(artifactDigest(payload), ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    const auto artifact = committedSnapshot(outputPath, *digest);
    ASSERT_TRUE(artifact.has_value());

    QFile file(outputPath);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    const QDateTime changed_modified = original_modified.addSecs(10);
    ASSERT_TRUE(file.setFileTime(changed_modified, QFileDevice::FileModificationTime));
    file.close();
    ASSERT_NE(QFileInfo(outputPath).lastModified().toMSecsSinceEpoch(), original_modified.toMSecsSinceEpoch());

    EXPECT_FALSE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("replacement-manifest")));

    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.setFileTime(original_modified, QFileDevice::FileModificationTime));
    file.close();

    // The original record is still the one on file: the rejected snapshot did not replace it.
    EXPECT_TRUE(tracker.hasArtifactRecord(outputPath));
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, QStringLiteral("replacement-manifest")));

    // Reuse is refused even under the original manifest hash: setting a timestamp moves the
    // platform file identity, and this payload is not a stamped AfterNight artifact, so there is
    // nothing left to prove it is unchanged. Regenerating is the safe answer.
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, QStringLiteral("original-manifest")));
}

TEST_F(SmartSkippingFixture, CurrentRunCommittedRecordIsReusedWithoutRehashing)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("trusted_reuse.fit"));
    const QByteArray payload("strict reuse payload");
    writeArtifactBytes(outputPath, payload);

    const auto digest =
        CurrentRunCommittedDigest::fromSha256(artifactDigest(payload), ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);
    const auto artifact = committedSnapshot(outputPath, *digest);
    ASSERT_TRUE(artifact.has_value());
    ASSERT_TRUE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("trusted-reuse-manifest")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);

    // Nothing has touched the file since it was committed, so its recorded platform identity
    // still matches and the reuse decision costs one stat rather than a full re-read.
    EXPECT_TRUE(tracker.isArtifactValid(outputPath, QStringLiteral("trusted-reuse-manifest")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(SmartSkippingFixture, CurrentRunCommittedRecordDetectsSameSizeSameMtimeReplacement)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("trusted_replaced.fit"));
    const QByteArray original("original trusted payload");
    QByteArray replacement = original;
    replacement[0] = 'r';
    writeArtifactBytes(outputPath, original);
    const QDateTime recordedModified = QFileInfo(outputPath).lastModified();

    const auto digest =
        CurrentRunCommittedDigest::fromSha256(artifactDigest(original), ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);
    const auto artifact = committedSnapshot(outputPath, *digest);
    ASSERT_TRUE(artifact.has_value());
    ASSERT_TRUE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("trusted-replaced")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);

    writeArtifactBytes(outputPath, replacement);
    QFile replaced(outputPath);
    ASSERT_TRUE(replaced.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replaced.setFileTime(recordedModified, QFileDevice::FileModificationTime));
    replaced.close();
    ASSERT_EQ(QFileInfo(outputPath).size(), original.size());
    ASSERT_EQ(QFileInfo(outputPath).lastModified().toMSecsSinceEpoch(), recordedModified.toMSecsSinceEpoch());

    // The rewrite moved the platform identity and the row holds no stamp, so nothing is left to reuse it by: the
    // recorded digest is provenance, and comparing it would read the payload.
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, QStringLiteral("trusted-replaced")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u) << "the replaced artifact's payload was read to rehash it";
}

TEST_F(SmartSkippingFixture, CurrentRunCommittedRecordWithoutAStampIsRecomputedOnceItsFileIdentityMoves)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("trusted_restored.fit"));
    const QByteArray payload("restored trusted payload");
    writeArtifactBytes(outputPath, payload);
    const QDateTime recordedModified = QFileInfo(outputPath).lastModified();

    const auto digest =
        CurrentRunCommittedDigest::fromSha256(artifactDigest(payload), ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);
    const auto artifact = committedSnapshot(outputPath, *digest);
    ASSERT_TRUE(artifact.has_value());
    ASSERT_TRUE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("trusted-restored")));

    // The same bytes restored under a fresh file identity, the modification time put back. The row holds the digest
    // and no stamp: the digest could say the bytes are the same, but deciding by it reads the whole payload, so the
    // artifact is recomputed.
    const QString copy = QDir(tempPath()).filePath(QStringLiteral("trusted_restored.copy"));
    ASSERT_TRUE(QFile::copy(outputPath, copy));
    ASSERT_TRUE(QFile::remove(outputPath));
    ASSERT_TRUE(QFile::rename(copy, outputPath));
    QFile restored(outputPath);
    ASSERT_TRUE(restored.open(QIODevice::ReadWrite));
    ASSERT_TRUE(restored.setFileTime(recordedModified, QFileDevice::FileModificationTime));
    restored.close();
    ASSERT_EQ(QFileInfo(outputPath).lastModified().toMSecsSinceEpoch(), recordedModified.toMSecsSinceEpoch());

    EXPECT_FALSE(tracker.isArtifactValid(outputPath, QStringLiteral("trusted-restored")))
        << "a moved artifact was reused by its recorded digest";
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(SmartSkippingFixture, TrustedDigestSqlFailureDoesNotPublishOrReplace)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("sql_failure.fit"));
    const QString newOutputPath = QDir(tempPath()).filePath(QStringLiteral("sql_failure_new.fit"));
    const QByteArray payload("sql failure payload");
    writeArtifactBytes(outputPath, payload);
    writeArtifactBytes(newOutputPath, payload);

    ArtifactTracker tracker(tempPath());
    ASSERT_TRUE(tracker.recordArtifact(outputPath, QStringLiteral("original-manifest")));
    ASSERT_TRUE(executeArtifactSql(tempPath(), QStringLiteral("CREATE TRIGGER reject_artifact_publish "
                                                              "BEFORE INSERT ON artifacts BEGIN "
                                                              "SELECT RAISE(FAIL, 'forced publication failure'); "
                                                              "END")));
    const auto digest =
        CurrentRunCommittedDigest::fromSha256(artifactDigest(payload), ArtifactDigestProvenance::CurrentRunCommitted);
    ASSERT_TRUE(digest.has_value());
    const auto artifact = committedSnapshot(outputPath, *digest);
    const auto newArtifact = committedSnapshot(newOutputPath, *digest);
    ASSERT_TRUE(artifact.has_value());
    ASSERT_TRUE(newArtifact.has_value());

    EXPECT_FALSE(tracker.recordCurrentRunCommittedArtifact(*artifact, QStringLiteral("replacement-manifest")));
    EXPECT_FALSE(tracker.recordCurrentRunCommittedArtifact(*newArtifact, QStringLiteral("new-manifest")));
    EXPECT_FALSE(tracker.hasArtifactRecord(newOutputPath));
    ASSERT_TRUE(executeArtifactSql(tempPath(), QStringLiteral("DROP TRIGGER reject_artifact_publish")));
    EXPECT_TRUE(tracker.isArtifactValid(outputPath, QStringLiteral("original-manifest")));
    EXPECT_FALSE(tracker.isArtifactValid(outputPath, QStringLiteral("replacement-manifest")));
}

TEST_F(SmartSkippingFixture, ArtifactHashCountersRetainConsumedBytesOnCancellationAndIgnoreOpenFailure)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("cancelled_hash.fit"));
    const QByteArray payload(256 * 1024, 'y');
    writeArtifactBytes(outputPath, payload);

    WorkflowExecutionCounterAccumulator counters;
    const QString digest = ArtifactTracker::contentDigestForFile(
        outputPath, &counters, [&counters]() { return counters.snapshot().bytesHashed() > 0; });

    EXPECT_TRUE(digest.isEmpty());
    EXPECT_GT(counters.snapshot().bytesHashed(), 0u);
    EXPECT_LT(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(payload.size()));

    const std::uint64_t beforeFailure = counters.snapshot().bytesHashed();
    EXPECT_TRUE(
        ArtifactTracker::contentDigestForFile(QDir(tempPath()).filePath(QStringLiteral("missing.fit")), &counters)
            .isEmpty());
    EXPECT_EQ(counters.snapshot().bytesHashed(), beforeFailure);
}

TEST_F(SmartSkippingFixture, CancelledArtifactRecordingNeverPublishesARecord)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("cancelled_record.fit"));
    const QByteArray payload(256 * 1024, 'z');
    writeArtifactBytes(outputPath, payload);

    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(tempPath(), &counters);
    int cancellation_checks = 0;
    const bool recorded =
        tracker.recordArtifact(outputPath, QStringLiteral("cancelled-record"), [&cancellation_checks]() {
            ++cancellation_checks;
            return true;
        });

    EXPECT_FALSE(recorded);
    EXPECT_FALSE(tracker.hasArtifactRecord(outputPath));
    EXPECT_GT(cancellation_checks, 0) << "recording never consulted the cancellation callback";
    // Recording no longer reads the payload, so there is no partial hashing left to observe.
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(SmartSkippingFixture, SkipsWhenOutputsExistAndSettingsMatch)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::CalibrationStage bias;
    bias.frameType = "BIAS";
    WorkflowManifest::CalibrationStage::Group group;
    group.outputFilename = "master_bias.fit";
    group.stacking.stackMethod = "Average"; // Default
    bias.groups.push_back(group);
    manifest.biasStage = bias;

    // Create dummy output file
    QString outputPath = QDir(tempPath()).filePath("master_bias.fit");
    QFile f(outputPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("dummy data");
    f.close();

    // Record artifact with matching hash
    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, computeCalibrationGroupHash(manifest, group));

    BiasStackingStage stage;
    EXPECT_TRUE(stage.shouldSkip(manifest));
}

TEST_F(SmartSkippingFixture, ReprocessesWhenSettingsChange)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::CalibrationStage bias;
    bias.frameType = "BIAS";
    WorkflowManifest::CalibrationStage::Group group;
    group.outputFilename = "master_bias.fit";
    group.stacking.stackMethod = "Average";
    bias.groups.push_back(group);
    manifest.biasStage = bias;

    // Create dummy output file
    QString outputPath = QDir(tempPath()).filePath("master_bias.fit");
    QFile f(outputPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("dummy data");
    f.close();

    // Record artifact with DIFFERENT settings
    WorkflowManifest::CalibrationStage::Group old_group = group;
    old_group.stacking.stackMethod = "Median";
    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, computeCalibrationGroupHash(manifest, old_group));

    BiasStackingStage stage;
    EXPECT_FALSE(stage.shouldSkip(manifest));
}

TEST_F(SmartSkippingFixture, ReprocessesWhenMemorySettingsChange)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::CalibrationStage bias;
    bias.frameType = "BIAS";
    WorkflowManifest::CalibrationStage::Group group;
    group.outputFilename = "master_bias.fit";
    group.stacking.stackMethod = "Average";
    group.stacking.limitMemory = false;
    group.stacking.maxMemoryMb = 2048;
    bias.groups.push_back(group);
    manifest.biasStage = bias;

    // Create dummy output file
    const QString outputPath = QDir(tempPath()).filePath("master_bias.fit");
    QFile f(outputPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("dummy data");
    f.close();

    // Record artifact with DIFFERENT memory settings
    WorkflowManifest::CalibrationStage::Group old_group = group;
    old_group.stacking.limitMemory = true;
    old_group.stacking.maxMemoryMb = 512;
    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, computeCalibrationGroupHash(manifest, old_group));

    BiasStackingStage stage;
    EXPECT_FALSE(stage.shouldSkip(manifest));
}

TEST_F(SmartSkippingFixture, ReprocessesWhenFileMissing)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::CalibrationStage bias;
    bias.frameType = "BIAS";
    WorkflowManifest::CalibrationStage::Group group;
    group.outputFilename = "master_bias.fit";
    bias.groups.push_back(group);
    manifest.biasStage = bias;

    // Record artifact but DO NOT create file
    QString outputPath = QDir(tempPath()).filePath("master_bias.fit");
    ArtifactTracker tracker(tempPath());
    tracker.recordArtifact(outputPath, computeCalibrationGroupHash(manifest, group));

    BiasStackingStage stage;
    EXPECT_FALSE(stage.shouldSkip(manifest));
}
