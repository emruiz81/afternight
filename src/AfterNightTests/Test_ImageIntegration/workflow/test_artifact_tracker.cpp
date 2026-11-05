// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Test_ImageIntegration/ImportSession.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

namespace
{

using an::tests::fixtures::AcquisitionFacts;
using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureCardKind;
using an::tests::fixtures::FixtureImage;

/// The run identity every stamped artifact here states beside its artifact identity.
constexpr const char* kRunIdentity = "20260819_112623_481";

/**
 * @brief Covers the reuse contract for artifacts AfterNight writes itself.
 *
 * The tracker must decide whether a generated file can be reused without ever reading its
 * pixel payload. Several tests assert that through the byte counter: any content hashing
 * would show up as a non-zero bytesHashed(). The header stamp is read through the engine
 * session's codecs, which is how the workflow's tracker reads it.
 */
class ArtifactTrackerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(dir_.isValid());
        output_ = dir_.path();

        an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(1u);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        session_ = std::make_unique<an::consumer::EngineSession>(std::move(*opened));
    }

    /// The reader that reads a file's `ANARTID` card through this case's session.
    [[nodiscard]] ArtifactStampReader stampReader() const
    {
        return sessionArtifactStampReader(*session_);
    }

    /// Write a small FITS file through the neutral writer, carrying the given cards beside the frame's own.
    [[nodiscard]] QString writeFrame(const QString& name, std::vector<FixtureCard> extraCards,
                                     const std::uint32_t width) const
    {
        FixtureImage frame =
            an::tests::fixtures::acquisitionFrame(name.toStdString(), AcquisitionFacts{}, width, width);
        for (FixtureCard& card : extraCards)
        {
            frame.cards.push_back(std::move(card));
        }

        const QString path = output_ + QStringLiteral("/") + name;
        std::string error;
        EXPECT_TRUE(
            an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    /// Write a small FITS artifact carrying @p artifactId in its header.
    [[nodiscard]] QString writeArtifact(const QString& name, const QString& artifactId) const
    {
        return writeFrame(name,
                          {FixtureCard{"ANARTID", artifactId.toStdString(), "artifact identity", FixtureCardKind::Text},
                           FixtureCard{"ANRUNID", kRunIdentity, "run identity", FixtureCardKind::Text}},
                          16u);
    }

    /// Write a small FITS file that carries no artifact identity, as files written before stamping do.
    [[nodiscard]] QString writeUnstampedArtifact(const QString& name) const
    {
        return writeFrame(name, {}, 8u);
    }

    /// Publish a small grey image as a stage publishes its output, stamped with @p identity, in the format the
    /// name's extension selects.
    [[nodiscard]] QString publishArtifact(const QString& name, const an::ArtifactIdentity& identity) const
    {
        const QString path = output_ + QStringLiteral("/") + name;
        // The scope ends with this function, so the session can admit the stamp read that follows.
        an::Result<an::consumer::OperationScope> scope = session_->admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        if (!scope.hasValue())
        {
            ADD_FAILURE() << scope.status().message();
            return path;
        }
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(16u, 16u, 1u, scope->engine().limits());
        if (!extent.has_value())
        {
            ADD_FAILURE() << "the engine refused a 16 by 16 extent";
            return path;
        }
        const std::optional<an::ImageDescriptor> descriptor =
            an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                   .sampleType = an::SampleType::Float32,
                                                                   .channelSchema = an::ChannelSchema::Mono,
                                                                   .colorSpace = an::ColorSpace::Unspecified,
                                                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                   .cfa = an::CfaDescriptor{},
                                                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                   .pixelAspectRatio = 1.0});
        if (!descriptor.has_value())
        {
            ADD_FAILURE() << "the engine refused a grey Float32 description";
            return path;
        }
        an::Result<an::Image<float>> image =
            an::Image<float>::createFilled(scope->engine(), *descriptor, an::StorageArrangement::Planar, 0.25f);
        if (!image.hasValue())
        {
            ADD_FAILURE() << image.status().message();
            return path;
        }
        const an::Status published = ImageIntegration::publishWorkflowImage(*scope, *image, path, identity);
        EXPECT_FALSE(published.failed()) << published.message();
        return path;
    }

    /// Put the same bytes back under a fresh file identity with the modification time restored, as a backup restore
    /// or a sync client does, so only the stamp the file carries can say it is the same artifact.
    static void restoreUnderAFreshIdentity(const QString& path)
    {
        const QDateTime modified = QFileInfo(path).lastModified();
        const QString copy = path + QStringLiteral(".copy");
        ASSERT_TRUE(QFile::copy(path, copy));
        ASSERT_TRUE(QFile::remove(path));
        ASSERT_TRUE(QFile::copy(copy, path));
        ASSERT_TRUE(QFile::remove(copy));
        ASSERT_TRUE(restoreModificationTime(path, modified));
    }

    /// Restore a file's modification timestamp so only its identity token can reveal a rewrite.
    [[nodiscard]] static bool restoreModificationTime(const QString& path, const QDateTime& when)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadWrite))
        {
            return false;
        }
        const bool applied = file.setFileTime(when, QFileDevice::FileModificationTime);
        file.close();
        return applied;
    }

    QTemporaryDir dir_;
    QString output_;
    std::unique_ptr<an::consumer::EngineSession> session_;
};

TEST_F(ArtifactTrackerTest, UnrecordedArtifactIsNotValid)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("unrecorded.fits"), QStringLiteral("aaaa1111"));

    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, RecordAndValidateNeverReadTheArtifactPayload)
{
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(output_, &counters, stampReader());

    const QString path = writeArtifact(QStringLiteral("cheap.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));
    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));

    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u)
        << "reuse validation read the artifact payload; it must decide from metadata and the header stamp";
}

TEST_F(ArtifactTrackerTest, ManifestHashChangeInvalidatesArtifact)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("rehashed.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("different-manifest-hash")));
}

TEST_F(ArtifactTrackerTest, DeletedArtifactIsNotValid)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("removed.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    ASSERT_TRUE(QFile::remove(path));
    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, SizeChangeInvalidatesArtifact)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("resized.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::Append));
    file.write("trailing", 8);
    file.close();

    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, RewriteThatDestroysTheHeaderStampIsDetected)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("tampered.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));
    ASSERT_EQ(tracker.artifactRecords().size(), 1u);
    ASSERT_EQ(tracker.artifactRecords().front().artifactStamp, QStringLiteral("aaaa1111"));

    const QDateTime original_modified = QFileInfo(path).lastModified();
    const qint64 original_size = QFileInfo(path).size();

    // A foreign tool that rewrites the file does not reproduce AfterNight's private cards.
    // Blanking the header region models that at identical size, with the modification time
    // restored - the case a plain path+mtime check cannot see.
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.seek(0));
        file.write(QByteArray(2880, ' '));
        file.close();
    }
    ASSERT_TRUE(restoreModificationTime(path, original_modified));

    ASSERT_EQ(QFileInfo(path).size(), original_size);
    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")))
        << "a rewrite that stripped the artifact identity was still accepted as reusable";
}

TEST_F(ArtifactTrackerTest, StampPreservingPayloadRewriteIsAcceptedByDesign)
{
    // Documents a deliberate limit, so it is not "fixed" by reintroducing content hashing.
    //
    // An in-place rewrite that keeps the size, the modification time and the header stamp is
    // indistinguishable from a byte-identical restore of the same artifact. Only re-reading
    // every byte could tell them apart, which is exactly the cost this design removes. The
    // realistic failure it replaces - a truncated or partial payload - cannot occur, because
    // outputs are renamed into place only once complete.
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("rewritten.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    const QDateTime original_modified = QFileInfo(path).lastModified();
    const qint64 original_size = QFileInfo(path).size();

    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.seek(original_size - 16));
        file.write(QByteArray(16, '\x7f'));
        file.close();
    }
    ASSERT_TRUE(restoreModificationTime(path, original_modified));

    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, ArtifactCarryingADifferentStampIsInvalid)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("swapped.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    const QDateTime original_modified = QFileInfo(path).lastModified();

    // Replace the file with a different artifact of the same shape. The identity token changes,
    // so validation falls back to the stamp the file carries - which no longer matches. The
    // modification time is restored so only the stamp can tell the two apart.
    ASSERT_TRUE(QFile::remove(path));
    const QString replacement = writeArtifact(QStringLiteral("swapped.fits"), QStringLiteral("bbbb2222"));
    ASSERT_EQ(replacement, path);
    ASSERT_TRUE(restoreModificationTime(path, original_modified));

    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, RelocatedArtifactWithMatchingStampStaysReusable)
{
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(output_, &counters, stampReader());

    const QString path = writeArtifact(QStringLiteral("moved.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    const QDateTime original_modified = QFileInfo(path).lastModified();

    // A backup restore or sync client reproduces the bytes and the timestamp under a fresh
    // file identity. The stamp inside the file is what proves it is still the same artifact.
    const QString sidecar = output_ + QStringLiteral("/moved.copy");
    ASSERT_TRUE(QFile::copy(path, sidecar));
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(QFile::copy(sidecar, path));
    ASSERT_TRUE(QFile::remove(sidecar));
    ASSERT_TRUE(restoreModificationTime(path, original_modified));

    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")))
        << "a byte-identical restore of the same artifact was needlessly invalidated";
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u) << "the stamp fallback read the payload instead of the header";
}

TEST_F(ArtifactTrackerTest, WithoutAStampReaderARelocatedArtifactIsRecomputed)
{
    // A tracker given no reader neither records the header stamp nor consults it, so a restore
    // under a fresh file identity has nothing to be checked against and is regenerated.
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(output_, &counters);

    const QString path = writeArtifact(QStringLiteral("unread.fits"), QStringLiteral("aaaa1111"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));
    ASSERT_EQ(tracker.artifactRecords().size(), 1u);
    EXPECT_TRUE(tracker.artifactRecords().front().artifactStamp.isEmpty());
    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));

    const QDateTime original_modified = QFileInfo(path).lastModified();
    const QString sidecar = output_ + QStringLiteral("/unread.copy");
    ASSERT_TRUE(QFile::copy(path, sidecar));
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(QFile::copy(sidecar, path));
    ASSERT_TRUE(QFile::remove(sidecar));
    ASSERT_TRUE(restoreModificationTime(path, original_modified));

    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(ArtifactTrackerTest, UnstampedArtifactValidatesWhileUntouched)
{
    WorkflowExecutionCounterAccumulator counters;
    ArtifactTracker tracker(output_, &counters, stampReader());

    // An artifact that carries no identity stamp is reused by the metadata fast path while
    // nothing changes it: its recorded size and time decide, and no byte is hashed.
    const QString path = writeUnstampedArtifact(QStringLiteral("unstamped.fits"));

    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));
    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
    EXPECT_EQ(counters.snapshot().bytesHashed(), 0u);
}

TEST_F(ArtifactTrackerTest, UnstampedArtifactIsInvalidatedOnceItIsTouched)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());

    const QString path = writeUnstampedArtifact(QStringLiteral("unstamped_touched.fits"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    const QDateTime original_modified = QFileInfo(path).lastModified();
    const qint64 original_size = QFileInfo(path).size();

    // Without a stamp there is nothing to fall back to, so a changed file identity must
    // invalidate rather than be trusted on metadata alone.
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.seek(original_size - 8));
        file.write(QByteArray(8, '\x5a'));
        file.close();
    }
    ASSERT_TRUE(restoreModificationTime(path, original_modified));

    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, RecordedStampMatchesTheHeaderTheFileCarries)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("introspect.fits"), QStringLiteral("abcd9876"));
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));

    const auto records = tracker.artifactRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records.front().artifactStamp, QStringLiteral("abcd9876"));
    EXPECT_FALSE(records.front().identityToken.isEmpty());
}

TEST_F(ArtifactTrackerTest, RecordingAMissingArtifactFails)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    EXPECT_FALSE(tracker.recordArtifact(output_ + QStringLiteral("/never_written.fits"), QStringLiteral("hash")));
}

TEST_F(ArtifactTrackerTest, RecordingIsSkippedWhenCancellationIsRequested)
{
    ArtifactTracker tracker(output_, nullptr, stampReader());
    const QString path = writeArtifact(QStringLiteral("cancelled.fits"), QStringLiteral("aaaa1111"));

    EXPECT_FALSE(tracker.recordArtifact(path, QStringLiteral("manifest-hash"), []() { return true; }));
    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
}

TEST_F(ArtifactTrackerTest, TheIdentityItsWriterStatedIsRecordedWithoutReadingTheFile)
{
    // An antivirus filter scans a file as its writer closes it, and a read that comes before the scan ends waits for
    // it, so a stage records the identity it has just stamped rather than reading it back.
    int stampReads = 0;
    ArtifactTracker tracker(output_, nullptr, [&stampReads, reader = stampReader()](const QString& path) {
        ++stampReads;
        return reader(path);
    });
    const an::Result<an::ArtifactIdentity> identity = an::ArtifactIdentity::create("aaaa1111", kRunIdentity);
    ASSERT_TRUE(identity.hasValue()) << identity.status().message();
    const QString path = publishArtifact(QStringLiteral("stated.fits"), *identity);

    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash"), *identity));
    EXPECT_EQ(stampReads, 0) << "the record read back an identity its writer had stated";
    const auto records = tracker.artifactRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records.front().artifactStamp, QStringLiteral("aaaa1111"));
    EXPECT_FALSE(records.front().identityToken.isEmpty());
    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));

    // The stated identity is the one the header carries, so a restore is still decided by it.
    restoreUnderAFreshIdentity(path);
    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
    EXPECT_EQ(stampReads, 1) << "a restore is decided by one read of the stamp the file carries";
}

TEST_F(ArtifactTrackerTest, AnXisfArtifactsStampIsReadFromItsOwnProperties)
{
    // XISF states the identity as properties of its own rather than FITS keywords, so a reader that looked only for
    // FITS cards found nothing and every restored XISF artifact was computed again.
    const an::Result<an::ArtifactIdentity> identity = an::ArtifactIdentity::create("cccc3333", kRunIdentity);
    ASSERT_TRUE(identity.hasValue()) << identity.status().message();
    const QString path = publishArtifact(QStringLiteral("restored.xisf"), *identity);
    EXPECT_EQ(stampReader()(path), QStringLiteral("cccc3333"));

    ArtifactTracker tracker(output_, nullptr, stampReader());
    ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash"), *identity));
    restoreUnderAFreshIdentity(path);
    EXPECT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")))
        << "a byte-identical restore of an XISF artifact was computed again";
}

#ifdef _WIN32
TEST_F(ArtifactTrackerTest, ARevisionIsCapturedWithoutAskingToReadTheFile)
{
    // An open that asks to read a file its writer has just closed waits for the antivirus scan the close started; one
    // that asks for the file's attributes alone does not. A handle that refuses to share reading tells the two apart,
    // because only an open that asks to read is refused by it.
    const QString path = writeArtifact(QStringLiteral("held.fits"), QStringLiteral("aaaa1111"));
    const HANDLE held = CreateFileW(path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(held, INVALID_HANDLE_VALUE);
    QFile reading(path);
    const bool readingOpened = reading.open(QIODevice::ReadOnly);
    reading.close();
    const std::optional<FileRevisionStamp> revision = ArtifactTracker::captureFileRevision(path);
    CloseHandle(held);

    ASSERT_FALSE(readingOpened) << "the held handle let a reading open through, so it proves nothing";
    ASSERT_TRUE(revision.has_value()) << "the revision was taken through an open that asks to read the file";
    EXPECT_FALSE(revision->identityToken.isEmpty());
    const std::optional<FileRevisionStamp> released = ArtifactTracker::captureFileRevision(path);
    ASSERT_TRUE(released.has_value());
    EXPECT_EQ(revision->identityToken, released->identityToken);
    EXPECT_EQ(revision->sizeBytes, released->sizeBytes);
    EXPECT_EQ(revision->modifiedAtMs, released->modifiedAtMs);
}
#endif

// The artifact records are read only at the format their writer writes. Records of another format are never read or
// reused: the store is set aside beside itself, under a name that states its format, and a new one is made, so every
// artifact it recorded is computed again.
TEST_F(ArtifactTrackerTest, ArtifactRecordsOfAnotherFormatAreSetAsideAndNotReused)
{
    const QString path = writeArtifact(QStringLiteral("master.fits"), QStringLiteral("abcd1234"));
    {
        ArtifactTracker tracker(output_, nullptr, stampReader());
        ASSERT_TRUE(tracker.recordArtifact(path, QStringLiteral("manifest-hash")));
        ASSERT_TRUE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
    }
    const QString records = QDir(output_).filePath(QStringLiteral(".afternight/image_integration/artifacts.db"));
    {
        core::SqliteConnection connection(records, QStringLiteral("ArtifactTrackerTestFormat"));
        ASSERT_TRUE(connection.open());
        QSqlQuery query(connection.db());
        ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA user_version = 1")));
    }
    QFile original(records);
    ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    original.close();

    ArtifactTracker tracker(output_, nullptr, stampReader());
    EXPECT_FALSE(tracker.isArtifactValid(path, QStringLiteral("manifest-hash")));
    EXPECT_TRUE(tracker.artifactRecords().empty());

    QFile setAside(QDir(output_).filePath(QStringLiteral(".afternight/image_integration/artifacts.format1.db")));
    ASSERT_TRUE(setAside.open(QIODevice::ReadOnly));
    EXPECT_EQ(setAside.readAll(), originalBytes);
}

} // namespace
