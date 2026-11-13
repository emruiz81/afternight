// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What a history step holds and when it holds it: the snapshot a step keeps is the state it was registered with,
// whatever happens to the shown image afterwards; the backup writer's copies are admitted against the session's
// allowance and never waited for on the registering thread; a snapshot that could not be written is known to have
// failed; a restore never damages what the view shows; and a step on a sequence that changed only the shown frame keeps
// that frame.

#include "history_test_support.h"

#include "controls/ImageHistoryTracker.h"

#include "fixtures/ScopedTempPath.h"

#include <QString>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace
{

using an::tests::history::FakeHistoryView;
using an::tests::history::GateOpener;
using an::tests::history::historyFilesIn;
using an::tests::history::makeFrame;
using an::tests::history::makeImage;
using an::tests::history::objectNameOf;
using an::tests::history::openHistorySession;
using an::tests::history::overwriteSamples;
using an::tests::history::recordEdit;
using an::tests::history::samplesOf;
using an::tests::history::settleRestore;
using an::tests::history::waitUntil;
using an::tests::history::WriterGate;
using an::tests::history::writeSerFrames;
using an::ui::history::BackupQueue;
using an::ui::history::SnapshotState;

QString qtPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::string utf8Path(const std::filesystem::path& path)
{
    return qtPath(path).toUtf8().toStdString();
}

/// A copy of an asset, taken under a read lease, as a step takes it.
an::consumer::ImageAsset copyOf(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    EXPECT_TRUE(lease.hasValue());
    an::Result<an::consumer::ImageAsset> copy = an::ui::history::copyForHistory(session, *lease);
    EXPECT_TRUE(copy.hasValue()) << copy.status().message();
    return copy.hasValue() ? std::move(*copy) : an::consumer::ImageAsset();
}

an::ArtifactIdentity identityOf(const std::string& artifact)
{
    return *an::ArtifactIdentity::create(artifact, "history-test");
}

class HistoryCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Provider = openHistorySession(allowances());
        ASSERT_TRUE(m_Provider.has_value());
        m_Directory = an::tests::fixtures::makeScopedTempDir("afternight-history");
    }

    void TearDown() override
    {
        an::tests::viewer::pumpQuietly();
    }

    [[nodiscard]] virtual an::consumer::MemoryAllowances allowances() const
    {
        return {};
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Provider->engineSession();
    }

    [[nodiscard]] std::unique_ptr<ImageHistoryTracker> track(FakeHistoryView& view,
                                                             an::ui::history::StepCodecPtr codec = {})
    {
        return std::make_unique<ImageHistoryTracker>(view, session(), std::move(codec), qtPath(m_Directory.path()));
    }

    [[nodiscard]] an::consumer::MemoryCategoryCounters pending() const
    {
        return session().memory().counters(an::consumer::MemoryCategory::PendingSnapshots);
    }

    std::optional<an::tests::viewer::TestSessionProvider> m_Provider;
    an::tests::fixtures::ScopedTempPath m_Directory;
};

// ---------------------------------------------------------------------------------------------------------------
// The backup queue on its own
// ---------------------------------------------------------------------------------------------------------------

using ImageHistoryBackupQueue = HistoryCase;

TEST_F(ImageHistoryBackupQueue, TheWriterWritesTheImageAsItWasWhenItWasSubmitted)
{
    WriterGate gate;
    gate.close();
    BackupQueue queue(session(), gate.codec());
    GateOpener opener(gate);

    const auto image = makeImage(session(), 8, 6, 1, 1.0f);
    const auto submitted = samplesOf(*image);
    an::consumer::ImageAsset copy = copyOf(session(), *image);
    auto room = queue.tryReserve(48u * 4u);
    ASSERT_TRUE(room.hasValue());
    const auto snapshot = queue.submit(std::move(copy), std::move(*room), utf8Path(m_Directory.filePath("a.ansnap")),
                                       "a", identityOf("a"));
    overwriteSamples(*image, 99.0f);

    gate.open();
    ASSERT_EQ(snapshot->waitUntilSettled(), SnapshotState::Written);
    an::Result<an::consumer::ImageAsset> read =
        an::ui::history::readHistorySnapshot(session(), snapshot->path(), snapshot->identity(), {});
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(samplesOf(*read), submitted);
}

TEST_F(ImageHistoryBackupQueue, AReservationWaitsWhileTwoSnapshotsArePending)
{
    WriterGate gate;
    gate.close();
    BackupQueue queue(session(), gate.codec());
    GateOpener opener(gate);

    const auto image = makeImage(session(), 8, 6, 1, 1.0f);
    for (const char* name : {"a", "b"})
    {
        auto room = queue.tryReserve(192u);
        ASSERT_TRUE(room.hasValue()) << room.status().message();
        (void)queue.submit(copyOf(session(), *image), std::move(*room),
                           utf8Path(m_Directory.filePath(std::string(name) + ".ansnap")), name, identityOf(name));
    }
    ASSERT_TRUE(gate.waitForStarted(1));
    EXPECT_FALSE(queue.tryReserve(192u).hasValue()) << "a third copy was admitted at once while two were pending";

    std::atomic_bool admitted = false;
    std::thread submitter([&] {
        an::Result<an::consumer::MemoryReservation> room = queue.reserve(192u, {});
        admitted = room.hasValue();
    });

    EXPECT_TRUE(waitUntil([&] { return queue.admissionWaits() > 0 || admitted.load(); }));
    EXPECT_FALSE(admitted.load()) << "a third copy was admitted while two were pending";
    EXPECT_EQ(queue.admissionWaits(), 1u);

    gate.open();
    submitter.join();
    EXPECT_TRUE(admitted.load());
    EXPECT_EQ(queue.peakPendingSnapshots(), 2u);
}

class SmallAllowanceQueue : public HistoryCase
{
protected:
    [[nodiscard]] an::consumer::MemoryAllowances allowances() const override
    {
        an::consumer::MemoryAllowances memory;
        memory.pendingSnapshots.bytes = 100u;
        return memory;
    }
};

TEST_F(SmallAllowanceQueue, ASnapshotLargerThanTheByteAllowanceIsAdmittedAlone)
{
    WriterGate gate;
    gate.close();
    BackupQueue queue(session(), gate.codec());
    GateOpener opener(gate);

    const auto image = makeImage(session(), 8, 6, 1, 1.0f); // 192 bytes
    auto first = queue.tryReserve(192u);
    ASSERT_TRUE(first.hasValue()) << "one snapshot always fits";
    const auto written = queue.submit(copyOf(session(), *image), std::move(*first),
                                      utf8Path(m_Directory.filePath("a.ansnap")), "a", identityOf("a"));
    ASSERT_TRUE(gate.waitForStarted(1));

    std::atomic_bool admitted = false;
    std::thread submitter([&] {
        an::Result<an::consumer::MemoryReservation> room = queue.reserve(192u, {});
        admitted = room.hasValue();
    });

    EXPECT_TRUE(waitUntil([&] { return queue.admissionWaits() > 0 || admitted.load(); }));
    EXPECT_FALSE(admitted.load()) << "a second copy was admitted past the byte allowance";

    gate.open();
    submitter.join();
    EXPECT_EQ(written->waitUntilSettled(), SnapshotState::Written);
    EXPECT_TRUE(admitted.load());
    EXPECT_EQ(queue.peakPendingSnapshots(), 1u);
    EXPECT_EQ(pending().peakBytes, 192u);
}

class NoOversizedQueue : public HistoryCase
{
protected:
    [[nodiscard]] an::consumer::MemoryAllowances allowances() const override
    {
        an::consumer::MemoryAllowances memory;
        memory.pendingSnapshots.bytes = 100u;
        memory.pendingSnapshots.admitsOneOversized = false;
        return memory;
    }
};

TEST_F(NoOversizedQueue, AReservationThatCanNeverFitIsRefusedAtOnceRatherThanWaitedFor)
{
    BackupQueue queue(session());

    const an::Result<an::consumer::MemoryReservation> room = queue.reserve(192u, {});

    ASSERT_FALSE(room.hasValue());
    EXPECT_EQ(room.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(queue.admissionWaits(), 0u);
}

TEST_F(ImageHistoryBackupQueue, ASnapshotThatCannotBeWrittenSettlesAsFailedAndIsReported)
{
    std::atomic_int reported = 0;
    std::string reportedLabel;
    BackupQueue queue(session(), nullptr, [&](const an::ui::history::Snapshot& snapshot) {
        if (snapshot.state() == SnapshotState::Failed)
        {
            reportedLabel = snapshot.label();
            ++reported;
        }
    });

    const auto path = m_Directory.path() / "missing-directory" / "a.ansnap";
    auto room = queue.tryReserve(192u);
    ASSERT_TRUE(room.hasValue());
    const auto snapshot = queue.submit(copyOf(session(), *makeImage(session(), 8, 6, 1, 1.0f)), std::move(*room),
                                       utf8Path(path), "Mirror", identityOf("a"));

    EXPECT_EQ(snapshot->waitUntilSettled(), SnapshotState::Failed);
    EXPECT_FALSE(snapshot->failure().empty());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(waitUntil([&] { return reported.load() == 1; }));
    EXPECT_EQ(reportedLabel, "Mirror");
}

TEST_F(ImageHistoryBackupQueue, SubmittingNoImageFailsAtOnce)
{
    BackupQueue queue(session());
    auto room = queue.tryReserve(1u);
    ASSERT_TRUE(room.hasValue());

    const auto snapshot = queue.submit(an::consumer::ImageAsset(), std::move(*room),
                                       utf8Path(m_Directory.filePath("a.ansnap")), "a", identityOf("a"));

    EXPECT_EQ(snapshot->state(), SnapshotState::Failed);
    EXPECT_EQ(queue.pendingSnapshots(), 0u);
    EXPECT_EQ(pending().entries, 0u) << "a refused submission gives its admission back";
}

TEST_F(ImageHistoryBackupQueue, DiscardTakesAQueuedSnapshotOffWithoutWritingIt)
{
    WriterGate gate;
    gate.close();
    BackupQueue queue(session(), gate.codec());
    GateOpener opener(gate);

    const auto image = makeImage(session(), 8, 6, 1, 1.0f);
    auto firstRoom = queue.tryReserve(192u);
    const auto first = queue.submit(copyOf(session(), *image), std::move(*firstRoom),
                                    utf8Path(m_Directory.filePath("a.ansnap")), "a", identityOf("a"));
    ASSERT_TRUE(gate.waitForStarted(1));
    auto secondRoom = queue.tryReserve(192u);
    const auto second = queue.submit(copyOf(session(), *image), std::move(*secondRoom),
                                     utf8Path(m_Directory.filePath("b.ansnap")), "b", identityOf("b"));

    queue.discard(second);
    EXPECT_EQ(second->state(), SnapshotState::Discarded);
    EXPECT_EQ(queue.pendingSnapshots(), 1u);
    EXPECT_EQ(pending().entries, 1u) << "the discarded copy gives its admission back";

    gate.open();
    EXPECT_EQ(first->waitUntilSettled(), SnapshotState::Written);
    EXPECT_TRUE(waitUntil([&] { return queue.pendingSnapshots() == 0; }));
    EXPECT_FALSE(std::filesystem::exists(second->path()));
    EXPECT_EQ(gate.started(), 1u);
}

TEST_F(ImageHistoryBackupQueue, ClosingStopsTheWriteInProgressAndLeavesNoFile)
{
    WriterGate gate;
    gate.close();
    auto queue = std::make_unique<BackupQueue>(session(), gate.codec());
    GateOpener opener(gate);

    auto room = queue->tryReserve(192u);
    const auto snapshot = queue->submit(copyOf(session(), *makeImage(session(), 8, 6, 1, 1.0f)), std::move(*room),
                                        utf8Path(m_Directory.filePath("a.ansnap")), "a", identityOf("a"));
    ASSERT_TRUE(gate.waitForStarted(1));

    // The gate stays closed: only the stop the close requests lets the write go, and a stopped write publishes nothing.
    queue->close();

    EXPECT_NE(snapshot->state(), SnapshotState::Written);
    EXPECT_FALSE(std::filesystem::exists(snapshot->path()));
    EXPECT_EQ(pending().entries, 0u);
}

// ---------------------------------------------------------------------------------------------------------------
// Still images
// ---------------------------------------------------------------------------------------------------------------

using ImageHistorySnapshot = HistoryCase;

TEST_F(ImageHistorySnapshot, AStepKeepsTheStateItWasRegisteredWithWhenTheImageChangesBeforeItIsWritten)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    const auto opened = samplesOf(*view.image);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);

    tracker->registerInitialStep("opened");
    ASSERT_TRUE(gate.waitForStarted(1));

    // A writer changes the shown image in place and the change is recorded; another writer then changes it again.
    overwriteSamples(*view.image, 50.0f);
    const auto changed = samplesOf(*view.image);
    ASSERT_TRUE(tracker->registerShownState("Mirror", ""));
    overwriteSamples(*view.image, 99.0f);

    gate.open();
    tracker->waitAllAsyncJobsDone();

    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(samplesOf(*view.image), opened);
    ASSERT_TRUE(tracker->redoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(samplesOf(*view.image), changed);
}

TEST_F(ImageHistorySnapshot, AStepIsWrittenInTheHistorysOwnDirectory)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);

    tracker->registerInitialStep("opened");
    ASSERT_TRUE(tracker->registerShownState("Mirror", ""));
    tracker->waitAllAsyncJobsDone();

    const std::string ownText = tracker->snapshotDirectory();
    const std::filesystem::path own = std::filesystem::path(std::u8string(ownText.begin(), ownText.end()));
    EXPECT_EQ(own.parent_path(), m_Directory.path());
    for (const auto& step : {tracker->getInitialStep(), tracker->getImageHistorySteps().at(0)})
    {
        ASSERT_TRUE(step->m_Snapshot);
        const std::filesystem::path written =
            std::filesystem::path(std::u8string(step->m_Snapshot->path().begin(), step->m_Snapshot->path().end()));
        EXPECT_EQ(written.parent_path(), own);
        EXPECT_TRUE(std::filesystem::exists(written));
    }
}

TEST_F(ImageHistorySnapshot, UndoShowsANewImageAndLeavesTheShownOneUntouched)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 3, 1.0f);
    const auto opened = samplesOf(*view.image);
    auto tracker = track(view);

    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 3, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();

    const auto shown = view.image;
    const auto shownSamples = samplesOf(*shown);
    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_NE(view.image.get(), shown.get());
    EXPECT_NE(view.image->identity(), shown->identity()) << "a restored state is a new asset";
    EXPECT_EQ(samplesOf(*view.image), opened);
    EXPECT_EQ(samplesOf(*shown), shownSamples);
}

TEST_F(ImageHistorySnapshot, AWriteThatFailsLeavesTheStepFailed)
{
    WriterGate gate;
    gate.refuseNext(1);
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());

    tracker->registerInitialStep("opened");

    EXPECT_EQ(tracker->getInitialStep()->waitAsyncJobsDone(), SnapshotState::Failed);
    EXPECT_EQ(tracker->getInitialStep()->snapshotState(), SnapshotState::Failed);
}

TEST_F(ImageHistorySnapshot, UndoToAStepWhoseSnapshotFailedChangesNothing)
{
    WriterGate gate;
    gate.refuseNext(1);
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());

    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();

    const auto shown = view.image;
    const auto shownSamples = samplesOf(*shown);
    EXPECT_FALSE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_EQ(tracker->getCurrentStep(), 1u);
    EXPECT_EQ(view.image.get(), shown.get());
    EXPECT_EQ(samplesOf(*view.image), shownSamples);
}

TEST_F(ImageHistorySnapshot, JumpingToAStepWhoseSnapshotFailedChangesNothing)
{
    WriterGate gate;
    gate.refuseNext(1);
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());

    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();

    const auto shown = view.image;
    const auto shownSamples = samplesOf(*shown);
    EXPECT_FALSE(tracker->jumpToStep(0));
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_EQ(tracker->getCurrentStep(), 1u);
    EXPECT_EQ(view.image.get(), shown.get());
    EXPECT_EQ(samplesOf(*view.image), shownSamples);
}

TEST_F(ImageHistorySnapshot, ARestoreTheViewRefusesChangesNothing)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();

    view.refuseRestores = true;
    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_EQ(tracker->getCurrentStep(), 1u) << "the current step moves only once the restored state is shown";
    EXPECT_EQ(view.imageRestores, 0u);
}

TEST_F(ImageHistorySnapshot, AShownStateIsRecordedAtOnceEvenWhileTwoCopiesArePending)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);

    tracker->registerInitialStep("opened");
    ASSERT_TRUE(gate.waitForStarted(1));
    ASSERT_TRUE(tracker->registerShownState("First", ""));

    // Two copies hold the allowance; a state already shown is recorded without waiting, beyond it, and counted.
    ASSERT_TRUE(tracker->registerShownState("Second", ""));
    EXPECT_EQ(tracker->getTotalStepCount(), 3u);
    EXPECT_EQ(pending().overrunAdmissions, 0u) << "the shown states' copies fit the byte allowance";
    EXPECT_EQ(pending().entries, 3u);
    EXPECT_EQ(tracker->backupQueue().admissionWaits(), 0u) << "nothing waited on the registering thread";

    gate.open();
    tracker->waitAllAsyncJobsDone();
    EXPECT_EQ(tracker->getImageHistorySteps().at(1)->snapshotState(), SnapshotState::Written);
}

TEST_F(ImageHistorySnapshot, AnEditRecordedBeforeItIsShownIsWhatRedoRestores)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 3, 1.0f);
    const auto opened = samplesOf(*view.image);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");

    const auto edited = makeImage(session(), 8, 6, 3, 700.0f);
    const auto editedSamples = samplesOf(*edited);
    ASSERT_TRUE(recordEdit(*tracker, *view.image, *edited, "Autostretch"));
    view.image = edited;
    tracker->waitAllAsyncJobsDone();

    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(samplesOf(*view.image), opened);
    ASSERT_TRUE(tracker->redoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(samplesOf(*view.image), editedSamples);
}

TEST_F(ImageHistorySnapshot, AnEditIsRefusedWhenTheStateItReplacesCannotBeKept)
{
    WriterGate gate;
    gate.refuseAll(true);
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());
    tracker->registerInitialStep("opened");
    tracker->waitAllAsyncJobsDone();

    EXPECT_FALSE(recordEdit(*tracker, *view.image, *makeImage(session(), 8, 6, 1, 700.0f), "Autostretch"));
    EXPECT_FALSE(tracker->registerEditBeforePublishing("Autostretch", "", *makeImage(session(), 8, 6, 1, 700.0f)));
    EXPECT_EQ(tracker->getTotalStepCount(), 1u);
}

TEST_F(ImageHistorySnapshot, AnEditSecuresAgainTheStateItReplacesWhenItsFirstWriteFailed)
{
    WriterGate gate;
    gate.refuseNext(1);
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    const auto opened = samplesOf(*view.image);
    auto tracker = track(view, gate.codec());
    tracker->registerInitialStep("opened");
    ASSERT_EQ(tracker->getInitialStep()->waitAsyncJobsDone(), SnapshotState::Failed);

    const auto edited = makeImage(session(), 8, 6, 1, 700.0f);
    ASSERT_TRUE(recordEdit(*tracker, *view.image, *edited, "Autostretch"));
    view.image = edited;
    tracker->waitAllAsyncJobsDone();

    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(samplesOf(*view.image), opened);
}

TEST_F(ImageHistorySnapshot, AnEditNobodyPreparedWaitsForNothingAndIsRefusedWhileTheStateItReplacesIsPending)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);
    tracker->registerInitialStep("opened");
    ASSERT_TRUE(gate.waitForStarted(1));

    EXPECT_FALSE(tracker->registerEditBeforePublishing("Autostretch", "", *makeImage(session(), 8, 6, 1, 700.0f)));
    EXPECT_EQ(tracker->getTotalStepCount(), 1u);

    gate.open();
    tracker->waitAllAsyncJobsDone();
    EXPECT_TRUE(tracker->registerEditBeforePublishing("Autostretch", "", *makeImage(session(), 8, 6, 1, 700.0f)));
}

TEST_F(ImageHistorySnapshot, ARenameStepRestoresTheMetadataOfTheStateItShares)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f, "opened");
    ASSERT_EQ(objectNameOf(*view.image), "opened");
    auto tracker = track(view);
    tracker->registerInitialStep("opened");

    view.image = makeImage(session(), 8, 6, 1, 500.0f, "stretched");
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    view.name = "M42 stretched";
    tracker->registerNewImageRenameStep();
    tracker->waitAllAsyncJobsDone();
    ASSERT_EQ(tracker->getTotalStepCount(), 3u);

    ASSERT_TRUE(tracker->jumpToStep(0));
    ASSERT_TRUE(settleRestore(*tracker));
    ASSERT_EQ(tracker->getCurrentStep(), 0u);
    EXPECT_EQ(objectNameOf(*view.image), "opened");

    ASSERT_TRUE(tracker->jumpToStep(2));
    ASSERT_TRUE(settleRestore(*tracker));
    ASSERT_EQ(tracker->getCurrentStep(), 2u);
    EXPECT_EQ(view.name, "M42 stretched");
    EXPECT_EQ(objectNameOf(*view.image), "stretched");
    EXPECT_EQ(samplesOf(*view.image), samplesOf(*makeImage(session(), 8, 6, 1, 500.0f)));
}

TEST_F(ImageHistorySnapshot, ClosingAViewWithSnapshotsPendingLeavesNoFileBehind)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);

    tracker->registerInitialStep("opened");
    ASSERT_TRUE(gate.waitForStarted(1));
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    const std::string own = tracker->snapshotDirectory();

    tracker.reset();

    EXPECT_TRUE(historyFilesIn(m_Directory.path()).empty());
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(std::u8string(own.begin(), own.end()))));
    EXPECT_EQ(pending().entries, 0u) << "every copy gave its admission back";
}

// ---------------------------------------------------------------------------------------------------------------
// Sequences
// ---------------------------------------------------------------------------------------------------------------

class ImageHistorySequence : public HistoryCase
{
protected:
    void SetUp() override
    {
        HistoryCase::SetUp();
        // The sequence's frames, in a SER file its reader opens.
        std::vector<std::shared_ptr<an::consumer::ImageAsset>> frames;
        for (const std::uint16_t base : {std::uint16_t{100}, std::uint16_t{200}, std::uint16_t{300}})
        {
            m_Frames.push_back(base);
            frames.push_back(makeFrame(session(), 8, 6, base));
        }
        const QString capture = qtPath(m_Directory.path()) + QStringLiteral("/capture.ser");
        writeSerFrames(session(), capture, frames);
        an::SequenceDescriptor descriptor;
        descriptor.format = an::SequenceFormat::Ser;
        descriptor.frameCount = 3u;
        m_View.sequence = an::ui::DocumentSequence{.sourcePath = capture, .descriptor = descriptor, .currentFrame = 1u};
        m_View.image = makeFrame(session(), 8, 6, 200);
    }

    [[nodiscard]] std::vector<double> frameSamples(std::size_t index) const
    {
        return samplesOf(*makeFrame(session(), 8, 6, m_Frames.at(index)));
    }

    std::vector<std::uint16_t> m_Frames;
    FakeHistoryView m_View;
};

TEST_F(ImageHistorySequence, AnEditOfTheShownFrameIsWrittenAndUndone)
{
    auto tracker = track(m_View);
    tracker->registerInitialStep("opened");

    const auto edited = makeFrame(session(), 8, 6, 5000);
    ASSERT_TRUE(recordEdit(*tracker, *m_View.image, *edited, "Stretch"));
    m_View.image = edited;

    const auto step = tracker->getImageHistorySteps().at(0);
    ASSERT_TRUE(step->m_Snapshot);
    ASSERT_TRUE(step->m_FrameIndex.has_value());
    EXPECT_EQ(*step->m_FrameIndex, 1u);
    EXPECT_EQ(step->snapshotState(), SnapshotState::Written) << "the frame is written before the edit is recorded";

    m_View.sequence->currentFrame = 2;
    m_View.image = makeFrame(session(), 8, 6, 300);
    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(m_View.sequence->currentFrame, 1u);
    EXPECT_EQ(samplesOf(*m_View.image), frameSamples(1));

    ASSERT_TRUE(tracker->redoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(m_View.sequence->currentFrame, 1u);
    EXPECT_EQ(samplesOf(*m_View.image), samplesOf(*edited));
}

TEST_F(ImageHistorySequence, AFrameEditIsRefusedBeforeItIsShownWhenTheFrameCannotBeWritten)
{
    WriterGate gate;
    auto tracker = track(m_View, gate.codec());
    tracker->registerInitialStep("opened");
    const auto shown = m_View.image;

    gate.refuseAll(true);
    EXPECT_FALSE(recordEdit(*tracker, *m_View.image, *makeFrame(session(), 8, 6, 5000), "Stretch"));
    EXPECT_FALSE(tracker->registerEditBeforePublishing("Stretch", "", *makeFrame(session(), 8, 6, 5000)))
        << "a frame edit nobody prepared cannot be written before it is shown";

    EXPECT_EQ(tracker->getTotalStepCount(), 1u);
    EXPECT_EQ(m_View.image.get(), shown.get());
}

TEST_F(ImageHistorySequence, AFrameChangeAlreadyShownThatCannotBeWrittenIsRecordedAndCannotBeRestored)
{
    WriterGate gate;
    auto tracker = track(m_View, gate.codec());
    tracker->registerInitialStep("opened");

    gate.refuseAll(true);
    m_View.image = makeFrame(session(), 8, 6, 5000);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();

    EXPECT_EQ(tracker->getTotalStepCount(), 2u) << "the timeline keeps matching what the view shows";
    EXPECT_EQ(tracker->getImageHistorySteps().at(0)->snapshotState(), SnapshotState::Failed);
    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(samplesOf(*m_View.image), frameSamples(1));
    EXPECT_FALSE(tracker->redoStep()) << "the step says it cannot be restored";
}

TEST_F(ImageHistorySequence, TheChangedFrameIsWhatTheStepPreviews)
{
    auto tracker = track(m_View);
    tracker->registerInitialStep("opened");

    const auto edited = makeFrame(session(), 8, 6, 5000);
    ASSERT_TRUE(recordEdit(*tracker, *m_View.image, *edited, "Stretch"));

    const ImageHistoryTracker::StepReader initial = tracker->stepReader(0, 1u);
    ASSERT_TRUE(initial);
    an::Result<an::consumer::ImageAsset> unchanged = initial({});
    ASSERT_TRUE(unchanged.hasValue()) << unchanged.status().message();
    EXPECT_EQ(samplesOf(*unchanged), frameSamples(1)) << "a step that changed no frame previews its sequence's frame";

    const ImageHistoryTracker::StepReader changed = tracker->stepReader(1);
    ASSERT_TRUE(changed);
    an::Result<an::consumer::ImageAsset> preview = changed({});
    ASSERT_TRUE(preview.hasValue()) << preview.status().message();
    EXPECT_EQ(samplesOf(*preview), samplesOf(*edited));
}

TEST_F(ImageHistorySequence, ANewSequenceIsReferencedRatherThanWritten)
{
    auto tracker = track(m_View);
    tracker->registerInitialStep("opened");
    const QString original = m_View.sequence->sourcePath;

    m_View.sequence = an::ui::DocumentSequence{.sourcePath = QStringLiteral("processed.ser"), .currentFrame = 1u};
    m_View.image = makeFrame(session(), 8, 6, 2000);
    ASSERT_TRUE(tracker->registerShownState("Mirror", ""));

    const auto step = tracker->getImageHistorySteps().at(0);
    ASSERT_TRUE(step->m_Sequence.has_value());
    EXPECT_EQ(step->m_Sequence->sourcePath, QStringLiteral("processed.ser"));
    EXPECT_FALSE(step->m_FrameIndex.has_value());
    EXPECT_FALSE(step->m_Snapshot);

    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(m_View.sequence->sourcePath, original);
    EXPECT_EQ(m_View.sequenceRestores, 1u);
    EXPECT_EQ(samplesOf(*m_View.image), frameSamples(1));
}

} // namespace
