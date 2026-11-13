// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a history step keeps and where: every metadata object and the validity mask restored with the samples, a
/// snapshot read only for the identity it was written for, an earlier session's swap dump never read and never removed,
/// only the history's own directory reclaimed, a copy the engine or the session cannot hold refusing the edit before it
/// is shown, a slow writer waited for on the edit's worker, and a restore read on a worker and shown on the GUI thread.

#include "Test_ImageViewControl/history_test_support.h"

#include "controls/ImageHistoryTrackerUtils.h"

#include "fixtures/ScopedTempPath.h"

#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QCoreApplication>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace
{

using namespace an::tests::history;
using an::ui::history::SnapshotState;

/// A name of the shape an earlier session's history gave its swap files.
constexpr const char* kSwapDumpName = "IMG_SWAP{6f1c2a7e-3b9d-4e85-a1f2-0c9d7b3e5a41}.dmp";

QString qtPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path pathOf(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/// Writes a swap dump of the shape an earlier session's history left one - a forty-byte header of five little-endian
/// 64-bit words (a type code, the width, the height, the channel count and the payload's size) and then 16 x 12 x 3
/// float samples - and returns its path, or an empty path when it cannot be written.
std::filesystem::path writeSwapDump(const std::filesystem::path& path)
{
    constexpr std::uint64_t kWidth = 16u;
    constexpr std::uint64_t kHeight = 12u;
    constexpr std::uint64_t kChannels = 3u;
    constexpr std::uint64_t kSamples = kWidth * kHeight * kChannels;
    std::string bytes;
    const auto word = [&bytes](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8)
        {
            bytes.push_back(static_cast<char>((value >> shift) & 0xffu));
        }
    };
    word(0x20700u);
    word(kWidth);
    word(kHeight);
    word(kChannels);
    word(kSamples * sizeof(float));
    for (std::uint64_t sample = 0u; sample < kSamples; ++sample)
    {
        const float value = static_cast<float>(sample) / 1024.0f;
        char raw[sizeof(float)];
        std::memcpy(raw, &value, sizeof(float));
        bytes.append(raw, sizeof(float));
    }
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return stream.good() ? path : std::filesystem::path();
}

std::string bytesOf(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

an::CodecCard card(an::CodecNamespace origin, std::string keyword, std::string value)
{
    return *an::CodecCard::create(origin, std::move(keyword), std::move(value), {});
}

/// Metadata in which every object states something other than its default, the validity a mask.
an::ImageMetadata everyObject()
{
    const an::AcquisitionMetadata acquisition = *an::AcquisitionMetadata::create({.instrumentName = "Camera Q",
                                                                                  .telescopeName = "Refractor",
                                                                                  .objectName = "NGC 7000",
                                                                                  .filterName = "Ha 3nm",
                                                                                  .exposureSeconds = 300.25,
                                                                                  .sensorTemperatureCelsius = -10.5});
    const an::ImageSemantics semantics = *an::ImageSemantics::create(
        {.processingState = {.biasSubtracted = true, .flatFielded = true},
         .validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})});
    const an::AstrometricSolution solution = *an::AstrometricSolution::create(
        {.referencePoint = {.rightAscensionDegrees = 312.75, .declinationDegrees = 44.33},
         .referencePixelX = 3.5,
         .referencePixelY = 2.25,
         .pixelScaleArcsecPerPixel = 1.87,
         .orientationDegrees = 180.5});
    an::Provenance provenance = *an::Provenance::create(an::ProvenanceSourceKind::Derived, "stack of 50");
    provenance = *provenance.appended(*an::ProvenanceEntry::create("decodeImage", "light-001.fits"));
    const an::CodecMetadata codec = *an::CodecMetadata::create(
        {}, {card(an::CodecNamespace::Fits, "OBJECT", "'NGC 7000'"), card(an::CodecNamespace::Fits, "GAIN", "100")});
    const an::OutputIntent intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    return an::ImageMetadata::create(acquisition, semantics, solution, std::move(provenance), codec, intent);
}

/// A Float32 colour image carrying `everyObject` and a mask that leaves one pixel of one channel meaningless.
std::shared_ptr<an::consumer::ImageAsset> describedImage(const an::consumer::EngineSession& session)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        ADD_FAILURE() << hold.status().message();
        return nullptr;
    }
    const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        hold->engine(), an::SampleType::Float32, 5u, 4u, an::tests::viewer::Channels::Rgb);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, everyObject());
    if (!image.hasValue())
    {
        ADD_FAILURE() << image.status().message();
        return nullptr;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 4u; ++y)
            {
                for (std::uint32_t x = 0u; x < 5u; ++x)
                {
                    (*access->view().row(c, y))[x] = 0.01f * static_cast<float>(x + 5u * y + 20u * c);
                }
            }
        }
    }
    an::Result<an::ValidityMask> mask =
        an::ValidityMask::createAllValid(hold->engine(), *an::ImageExtent::create(5u, 4u, 3u, hold->engine().limits()));
    if (!mask.hasValue())
    {
        ADD_FAILURE() << mask.status().message();
        return nullptr;
    }
    (void)mask->view()->setValid(1u, 2u, 3u, false);
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)}, std::move(*mask));
    if (!asset.hasValue())
    {
        ADD_FAILURE() << asset.status().message();
        return nullptr;
    }
    return std::make_shared<an::consumer::ImageAsset>(std::move(*asset));
}

an::ImageMetadata metadataOf(const an::consumer::ImageAsset& image)
{
    an::ImageMetadata metadata;
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (lease.hasValue())
    {
        lease->visit([&metadata](const auto& typed) { metadata = typed.metadata(); });
    }
    return metadata;
}

/// Every bit of an asset's validity mask, plane by plane; empty when it has none.
std::vector<bool> validityOf(const an::consumer::ImageAsset& image)
{
    std::vector<bool> bits;
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (!lease.hasValue() || lease->validity() == nullptr)
    {
        return bits;
    }
    const auto view = lease->validity()->view();
    for (std::uint32_t plane = 0u; plane < view->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < view->width(); ++x)
            {
                bits.push_back(view->test(plane, x, y).value_or(false));
            }
        }
    }
    return bits;
}

class HistorySnapshotTest : public ::testing::Test
{
protected:
    void open(const an::consumer::MemoryAllowances& memory = {}, std::uint64_t engineBudget = 0u)
    {
        m_Provider = openHistorySession(memory, engineBudget);
        ASSERT_TRUE(m_Provider.has_value());
        m_Directory = an::tests::fixtures::makeScopedTempDir("afternight-history-snapshots");
    }

    void SetUp() override
    {
        open();
    }

    void TearDown() override
    {
        pumpQuietly();
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

    std::optional<TestSessionProvider> m_Provider;
    an::tests::fixtures::ScopedTempPath m_Directory;
};

TEST_F(HistorySnapshotTest, AStepRestoresEveryMetadataObjectAndTheValidityMask)
{
    FakeHistoryView view;
    view.image = describedImage(session());
    ASSERT_TRUE(view.image);
    const an::ImageMetadata metadata = metadataOf(*view.image);
    const std::vector<bool> validity = validityOf(*view.image);
    const std::vector<double> samples = samplesOf(*view.image);
    ASSERT_EQ(std::count(validity.begin(), validity.end(), false), 1);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");

    view.image = makeImage(session(), 5, 4, 3, 7.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();
    const std::uint64_t shown = view.image->identity();

    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_NE(view.image->identity(), shown);
    EXPECT_EQ(samplesOf(*view.image), samples);
    EXPECT_TRUE(metadataOf(*view.image) == metadata) << "every metadata object comes back from the snapshot";
    EXPECT_EQ(validityOf(*view.image), validity) << "the mask comes back bit for bit";
}

TEST_F(HistorySnapshotTest, ASnapshotIsReadOnlyForTheIdentityItWasWrittenFor)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    ASSERT_EQ(tracker->getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);
    const auto& snapshot = *tracker->getInitialStep()->m_Snapshot;

    an::Result<an::consumer::ImageAsset> own =
        an::ui::history::readHistorySnapshot(session(), snapshot.path(), snapshot.identity(), {});
    ASSERT_TRUE(own.hasValue()) << own.status().message();
    EXPECT_EQ(samplesOf(*own), samplesOf(*view.image));

    const an::ArtifactIdentity other = *an::ArtifactIdentity::create(snapshot.identity().artifactId(), "another-run");
    an::Result<an::consumer::ImageAsset> foreign =
        an::ui::history::readHistorySnapshot(session(), snapshot.path(), other, {});
    ASSERT_FALSE(foreign.hasValue()) << "a file stating another identity is not this history's step";
    EXPECT_EQ(foreign.status().code(), an::ErrorCode::InvalidData);
}

TEST_F(HistorySnapshotTest, AnEarlierSessionsSwapDumpIsNeverReadAsASnapshot)
{
    const std::filesystem::path dump = writeSwapDump(m_Directory.filePath(kSwapDumpName));
    ASSERT_FALSE(dump.empty()) << "the swap dump could not be written";

    const an::ArtifactIdentity anyIdentity = *an::ArtifactIdentity::create("any", "any");
    an::Result<an::consumer::ImageAsset> read =
        an::ui::history::readHistorySnapshot(session(), qtPath(dump).toUtf8().toStdString(), anyIdentity, {});

    ASSERT_FALSE(read.hasValue());
    EXPECT_EQ(read.status().code(), an::ErrorCode::InvalidData) << read.status().message();
}

TEST_F(HistorySnapshotTest, TheHistoryReclaimsItsOwnDirectoryAndNothingElse)
{
    // What an earlier session left in the temporary directory: its swap dump, another history's directory, and a file
    // of the user's that happens to live there.
    const std::filesystem::path dump = writeSwapDump(m_Directory.filePath(kSwapDumpName));
    ASSERT_FALSE(dump.empty()) << "the swap dump could not be written";
    const std::filesystem::path& leftDump = dump;
    const std::filesystem::path otherHistory = m_Directory.filePath("AN_HISTORY_0123456789abcdef0123456789abcdef");
    std::filesystem::create_directories(otherHistory);
    std::filesystem::copy_file(dump, otherHistory / "fedcba9876543210fedcba9876543210.ansnap");
    const std::filesystem::path userFile = m_Directory.filePath("starless.tif");
    std::filesystem::copy_file(dump, userFile);
    const std::string dumpBytes = bytesOf(dump);

    {
        FakeHistoryView view;
        view.image = makeImage(session(), 8, 6, 1, 1.0f);
        const auto opened = samplesOf(*view.image);
        auto tracker = track(view);
        tracker->registerInitialStep("opened");
        view.image = makeImage(session(), 8, 6, 1, 500.0f);
        ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
        tracker->waitAllAsyncJobsDone();
        ASSERT_TRUE(tracker->undoStep());
        ASSERT_TRUE(settleRestore(*tracker));
        EXPECT_EQ(samplesOf(*view.image), opened) << "the history restores its own snapshot, never a dump";
        EXPECT_TRUE(std::filesystem::exists(pathOf(tracker->snapshotDirectory())));
    }

    EXPECT_EQ(bytesOf(leftDump), dumpBytes) << "an earlier session's swap dump is neither read nor removed";
    EXPECT_EQ(bytesOf(otherHistory / "fedcba9876543210fedcba9876543210.ansnap"), dumpBytes)
        << "another history's directory is not this history's to reclaim";
    EXPECT_EQ(bytesOf(userFile), dumpBytes) << "no file of the user's is taken for a swap file";
    std::size_t ownDirectories = 0u;
    for (const auto& entry : std::filesystem::directory_iterator(m_Directory.path()))
    {
        const std::string name = entry.path().filename().string();
        ownDirectories += name.rfind("AN_HISTORY_", 0) == 0 ? 1u : 0u;
    }
    EXPECT_EQ(ownDirectories, 1u) << "the history's own directory went with it, and only that";
}

TEST_F(HistorySnapshotTest, AnEditWhoseCopyTheEngineRefusesIsRefusedBeforeItIsShown)
{
    // Room for the shown image, its initial copy and an edit's result four times its size, and not for the result's
    // copy as well.
    constexpr std::uint64_t kShown = 256u * 256u * 4u;
    open({}, 6u * kShown);
    FakeHistoryView view;
    view.image = makeImage(session(), 256, 256, 1, 1.0f);
    ASSERT_TRUE(view.image);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    ASSERT_EQ(tracker->getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);

    const auto edited = makeImage(session(), 512, 512, 1, 5.0f);
    ASSERT_TRUE(edited);
    EXPECT_FALSE(recordEdit(*tracker, *view.image, *edited, "Resample"));
    EXPECT_EQ(tracker->getTotalStepCount(), 1u) << "nothing was recorded, so the edit may not be shown";
    EXPECT_EQ(session().memory().counters(an::consumer::MemoryCategory::PendingSnapshots).entries, 0u)
        << "the refused copy's admission was given back";
}

TEST_F(HistorySnapshotTest, AnEditWhoseCopyTheAllowanceCanNeverHoldIsRefusedBeforeItIsShown)
{
    an::consumer::MemoryAllowances memory;
    memory.pendingSnapshots.bytes = 64u;
    memory.pendingSnapshots.admitsOneOversized = false;
    open(memory);
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    // The initial state was already shown, so its copy is admitted beyond the allowance and counted.
    EXPECT_EQ(session().memory().counters(an::consumer::MemoryCategory::PendingSnapshots).overrunAdmissions, 1u);
    ASSERT_EQ(tracker->getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);

    EXPECT_FALSE(recordEdit(*tracker, *view.image, *makeImage(session(), 8, 6, 1, 7.0f), "Stretch"));
    EXPECT_EQ(tracker->getTotalStepCount(), 1u);
}

TEST_F(HistorySnapshotTest, AnEditsPreparationWaitsForASlowWriterOnItsWorkerNotTheGuiThread)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);
    tracker->registerInitialStep("opened");
    ASSERT_TRUE(gate.waitForStarted(1));

    const auto edited = makeImage(session(), 8, 6, 1, 700.0f);
    ImageHistoryTracker::EditPreparation prepare = tracker->editPreparation("Stretch");
    an::Result<an::consumer::AssetWriteLease> writer = view.image->write();
    ASSERT_TRUE(writer.hasValue());
    std::atomic_bool prepared = false;
    an::Result<std::shared_ptr<void>> preparation = an::Status::failure(an::ErrorCode::Internal, "not prepared");
    std::thread worker([&]() {
        preparation = prepare(*writer, *edited, {});
        prepared = true;
    });

    // The GUI thread turns while the worker waits for the state the edit replaces to be written.
    std::atomic_int ticks = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
    ticker.start(5);
    pumpQuietly();
    EXPECT_FALSE(prepared.load()) << "the edit went on before what it replaces was written";
    EXPECT_GT(ticks.load(), 0) << "the GUI thread's event loop turned meanwhile";

    gate.open();
    worker.join();
    ticker.stop();
    ASSERT_TRUE(preparation.hasValue()) << preparation.status().message();
    writer->release();
    EXPECT_TRUE(tracker->registerEditBeforePublishing("Stretch", "", *edited));
    EXPECT_EQ(tracker->getTotalStepCount(), 2u);
}

TEST_F(HistorySnapshotTest, ARestoreIsReadOnAWorkerAndTheCurrentStepMovesWhenItIsShown)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    const auto opened = samplesOf(*view.image);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);
    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));

    // The initial state is still being written: undo begins at once and waits for it on its worker.
    ASSERT_TRUE(tracker->undoStep());
    EXPECT_TRUE(tracker->isRestoring());
    pumpQuietly();
    EXPECT_EQ(tracker->getCurrentStep(), 1u);
    EXPECT_EQ(view.imageRestores, 0u);
    EXPECT_FALSE(tracker->undoStep()) << "one restore runs at a time";
    EXPECT_FALSE(tracker->registerEditBeforePublishing("Stretch", "", *makeImage(session(), 8, 6, 1, 9.0f)))
        << "nothing is recorded while a restore is on its way";

    gate.open();
    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(tracker->getCurrentStep(), 0u);
    EXPECT_EQ(view.imageRestores, 1u);
    EXPECT_EQ(samplesOf(*view.image), opened);
}

TEST_F(HistorySnapshotTest, NothingIsRecordedWhileARestoreIsOnItsWay)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();

    // The restore lands only when the GUI thread's event loop next turns.
    ASSERT_TRUE(tracker->undoStep());
    ASSERT_TRUE(tracker->isRestoring());
    EXPECT_FALSE(tracker->registerEditBeforePublishing("Mirror", "", *makeImage(session(), 8, 6, 1, 9.0f)))
        << "an edit computed from the state being left is not recorded";
    EXPECT_EQ(tracker->getTotalStepCount(), 2u);

    ASSERT_TRUE(settleRestore(*tracker));
    EXPECT_EQ(tracker->getCurrentStep(), 0u);
}

TEST_F(HistorySnapshotTest, AnEditPreparedBeforeAnotherStepWasRecordedIsRefused)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    tracker->waitAllAsyncJobsDone();

    const auto edited = makeImage(session(), 8, 6, 1, 700.0f);
    ImageHistoryTracker::EditPreparation prepare = tracker->editPreparation("Stretch");
    an::Result<an::consumer::AssetWriteLease> writer = view.image->write();
    ASSERT_TRUE(writer.hasValue());
    an::Result<std::shared_ptr<void>> prepared = prepare(*writer, *edited, {});
    writer->release();
    ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();

    // A process shows and records another state before the prepared edit reaches the GUI thread.
    view.image = makeImage(session(), 8, 6, 1, 50.0f);
    ASSERT_TRUE(tracker->registerShownState("Mirror", ""));

    EXPECT_FALSE(tracker->registerEditBeforePublishing("Stretch", "", *edited))
        << "the edit was prepared against a state the history no longer has as its current one";
    EXPECT_EQ(tracker->getTotalStepCount(), 2u);
}

TEST_F(HistorySnapshotTest, ARestoreIsNotShownOnADocumentTheViewTookMeanwhile)
{
    WriterGate gate;
    gate.close();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view, gate.codec());
    GateOpener opener(gate);
    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    ASSERT_TRUE(tracker->undoStep());

    // The view shows another document before the step has been read.
    view.document = 2u;
    const auto shown = view.image;
    gate.open();
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_EQ(view.imageRestores, 0u);
    EXPECT_EQ(view.image.get(), shown.get());
    EXPECT_EQ(tracker->getCurrentStep(), 1u);
}

TEST_F(HistorySnapshotTest, RemovingTheCurrentStepRestoresTheOneBeforeAndThenRemovesIt)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    const auto opened = samplesOf(*view.image);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ASSERT_TRUE(tracker->registerShownState("Stretch", ""));
    tracker->waitAllAsyncJobsDone();
    const std::string removedPath = tracker->getImageHistorySteps().at(0)->m_Snapshot->path();

    ASSERT_TRUE(tracker->removeStep(1));
    EXPECT_EQ(tracker->getTotalStepCount(), 2u) << "the step goes when the state before it is shown";
    ASSERT_TRUE(settleRestore(*tracker));

    EXPECT_EQ(tracker->getTotalStepCount(), 1u);
    EXPECT_EQ(tracker->getCurrentStep(), 0u);
    EXPECT_EQ(samplesOf(*view.image), opened);
    EXPECT_FALSE(std::filesystem::exists(pathOf(removedPath))) << "the removed step's snapshot is removed with it";
}

TEST_F(HistorySnapshotTest, TheLogAndTheInheritedLineageListEveryStep)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 1.0f);
    auto tracker = track(view);
    tracker->registerInitialStep("opened");
    ASSERT_TRUE(tracker->registerShownState("Stretch", "midtones"));
    ASSERT_TRUE(tracker->registerShownState("Mirror", ""));

    const ImageHistoryLineage lineage = an::ui::history::inheritedLineage(*tracker);
    ASSERT_EQ(lineage.entries().size(), 3u);
    EXPECT_EQ(lineage.entries()[0].step_name, "Initial state");
    EXPECT_EQ(lineage.entries()[1].step_name, "Stretch");
    EXPECT_EQ(lineage.entries()[1].description, "midtones");
    EXPECT_EQ(lineage.entries()[2].image_name, "M42");

    tracker->setLineage(lineage);
    const std::string log = an::ui::history::formatHistoryLog(*tracker);
    EXPECT_NE(log.find("INHERITED PROCESSING HISTORY"), std::string::npos);
    EXPECT_NE(log.find("[2] *"), std::string::npos) << "the current step is marked";
    tracker->waitAllAsyncJobsDone();
}

} // namespace
