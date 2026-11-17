// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A history over each step codec: its steps are files of the codec's kind in the history's own directory, an edit is
/// recorded through it and an undo restores the state it kept whole; and the one codec every history is made with -
/// what a history given no codec writes through, in a project's folder or out of one, and what it falls back to where
/// a step cannot be an XISF image.

#include "Test_ControlsHistory/StepCodecTestSupport.h"

#include "controls/ImageHistoryIndex.h"

#include "fixtures/ScopedTempPath.h"

#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace
{

using namespace an::tests::history;
using an::tests::viewer::Channels;
using an::ui::history::SnapshotState;

QString qtPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

/// The extensions of the files below a directory, at any depth.
std::set<std::string> extensionsBelow(const std::filesystem::path& directory)
{
    std::set<std::string> extensions;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(directory, error), end; !error && entry != end;
         entry.increment(error))
    {
        if (entry->is_regular_file(error))
        {
            extensions.insert(entry->path().extension().string());
        }
    }
    return extensions;
}

class HistoryOverAStepCodec : public ::testing::TestWithParam<CodecCase>
{
protected:
    void SetUp() override
    {
        m_Provider = openStepCodecSession();
        ASSERT_TRUE(m_Provider.has_value());
        m_Directory = an::tests::fixtures::makeScopedTempDir("afternight-history-codec");
    }

    void TearDown() override
    {
        pumpQuietly();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Provider->engineSession();
    }

    std::optional<TestSessionProvider> m_Provider;
    an::tests::fixtures::ScopedTempPath m_Directory;
};

TEST_P(HistoryOverAStepCodec, ItsStepsAreFilesOfTheCodecAndAnUndoRestoresTheStateWhole)
{
    const an::ui::history::StepCodecPtr codec = GetParam().make();
    ASSERT_NE(codec, nullptr);

    // The state the history keeps first: a colour image with a mask for its channels under the explicit rule.
    StepImageRequest request;
    request.type = an::SampleType::Float32;
    request.channels = Channels::Rgb;
    request.maskPlanes = 1u;
    request.metadata = an::ImageMetadata::create(
        *an::AcquisitionMetadata::create({.objectName = "NGC 7000", .exposureSeconds = 120.5}),
        *an::ImageSemantics::create(
            {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})}),
        {}, {}, {}, std::nullopt);
    FakeHistoryView view;
    view.image = makeStepImage(session(), request);
    ASSERT_NE(view.image, nullptr);
    const StepState opened = stepStateOf(*view.image);

    ImageHistoryTracker tracker(view, session(), codec, qtPath(m_Directory.path()));
    tracker.registerInitialStep("opened");
    ASSERT_EQ(tracker.getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);
    EXPECT_TRUE(tracker.getInitialStep()->m_Snapshot->path().ends_with(codec->fileExtension()));

    // An edit, recorded before it is shown as the view records one.
    StepImageRequest editedRequest = request;
    editedRequest.samples = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return mixedBits(x, y, c, 9u); };
    const std::shared_ptr<an::consumer::ImageAsset> edited = makeStepImage(session(), editedRequest);
    ASSERT_NE(edited, nullptr);
    const StepState afterEdit = stepStateOf(*edited);
    ASSERT_TRUE(recordEdit(tracker, *view.image, *edited, "Curves"));
    view.image = edited;
    tracker.waitAllAsyncJobsDone();
    EXPECT_EQ(extensionsBelow(m_Directory.path()), std::set<std::string>{std::string(codec->fileExtension())})
        << "the history's directory holds its steps and nothing else";

    ASSERT_TRUE(tracker.undoStep());
    ASSERT_TRUE(settleRestore(tracker));
    ASSERT_NE(view.image, nullptr);
    expectSameStep(opened, stepStateOf(*view.image));

    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(afterEdit, stepStateOf(*view.image));
}

INSTANTIATE_TEST_SUITE_P(EveryCodec, HistoryOverAStepCodec, ::testing::ValuesIn(everyStepCodec()),
                         [](const ::testing::TestParamInfo<CodecCase>& info) { return std::string(info.param.name); });

/// The head of a file, where an XISF step states how its samples are compressed.
QByteArray headOf(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.read(64 * 1024);
}

TEST(HistoryStepCodec, AHistoryGivenNoCodecWritesThroughTheOneEveryHistoryIsMadeWith)
{
    std::optional<TestSessionProvider> provider = openStepCodecSession();
    ASSERT_TRUE(provider.has_value());
    const an::tests::fixtures::ScopedTempPath directory =
        an::tests::fixtures::makeScopedTempDir("afternight-history-codec");
    FakeHistoryView view;
    view.image = makeImage(provider->engineSession(), 64, 64, 1, 1.0f);
    {
        ImageHistoryTracker tracker(view, provider->engineSession(), nullptr, qtPath(directory.path()));
        tracker.registerInitialStep("opened");
        ASSERT_EQ(tracker.getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);
        EXPECT_EQ(tracker.backupQueue().codec()->name(), "history");
        // An XISF image, compressed as the codec's default says.
        EXPECT_EQ(extensionsBelow(directory.path()), std::set<std::string>{".xisf"});
        EXPECT_TRUE(headOf(tracker.getInitialStep()->m_Snapshot->path()).contains("lz4"));
    }
    pumpQuietly();
}

TEST(HistoryStepCodec, AHistoryOutsideAProjectWritesTheSameStepsAsOneInside)
{
    using an::ui::history::HistoryEntry;
    using an::ui::history::HistoryHome;
    using an::ui::history::StepCompression;
    std::optional<TestSessionProvider> provider = openStepCodecSession();
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const an::tests::fixtures::ScopedTempPath temporary =
        an::tests::fixtures::makeScopedTempDir("afternight-history-outside");
    const an::tests::fixtures::ScopedTempPath project =
        an::tests::fixtures::makeScopedTempDir("afternight-history-inside");
    const QString homeDirectory = QDir(qtPath(project.path())).filePath(QStringLiteral("history/image-1"));

    // The same image and the same edit, recorded by a history in the session's temporary directory and by one in a
    // project's folder, each under LZ4.
    const auto record = [&session](ImageHistoryTracker& tracker, FakeHistoryView& view) {
        const std::shared_ptr<an::consumer::ImageAsset> edited = makeImage(session, 64, 64, 3, 40.0f, "NGC 7000");
        ASSERT_NE(edited, nullptr);
        ASSERT_TRUE(recordEdit(tracker, *view.image, *edited, "Curves"));
        view.image = edited;
        tracker.waitAllAsyncJobsDone();
    };
    FakeHistoryView outsideView;
    outsideView.image = makeImage(session, 64, 64, 3, 1.0f, "NGC 7000");
    FakeHistoryView insideView;
    insideView.image = makeImage(session, 64, 64, 3, 1.0f, "NGC 7000");
    {
        ImageHistoryTracker outside(outsideView, session, an::ui::history::historyStepCodec(StepCompression::Lz4),
                                    qtPath(temporary.path()));
        outside.registerInitialStep("opened");
        record(outside, outsideView);

        ImageHistoryTracker inside(insideView, session, nullptr, qtPath(temporary.path()));
        inside.registerInitialStep("opened");
        std::optional<HistoryEntry> entry;
        HistoryHome home;
        home.directory = homeDirectory;
        home.compression = StepCompression::Lz4;
        inside.enterHome(home, an::ui::history::StepsMove::Move,
                         [&entry](const HistoryEntry& landed) { entry = landed; });
        ASSERT_TRUE(pumpUntil([&entry]() { return entry.has_value(); }));
        ASSERT_TRUE(entry->entered) << entry->problem.toStdString();
        record(inside, insideView);

        // One codec writes both: the same kind of file, the same compression, and steps that restore the same state.
        EXPECT_EQ(outside.backupQueue().codec()->name(), "history");
        EXPECT_EQ(inside.backupQueue().codec()->name(), "history");
        const std::string outsideFile = outside.getImageHistorySteps().front()->m_Snapshot->path();
        const std::string insideFile = inside.getImageHistorySteps().front()->m_Snapshot->path();
        EXPECT_TRUE(outsideFile.ends_with(".xisf"));
        EXPECT_TRUE(insideFile.ends_with(".xisf"));
        EXPECT_TRUE(headOf(outsideFile).contains("lz4"));
        EXPECT_TRUE(headOf(insideFile).contains("lz4"));
        EXPECT_EQ(std::filesystem::path(insideFile).parent_path().filename().string(), "image-1");
        for (std::size_t step = 0u; step < 2u; ++step)
        {
            an::Result<an::consumer::ImageAsset> fromOutside = outside.stepReader(step)({});
            an::Result<an::consumer::ImageAsset> fromInside = inside.stepReader(step)({});
            ASSERT_TRUE(fromOutside.hasValue()) << fromOutside.status().message();
            ASSERT_TRUE(fromInside.hasValue()) << fromInside.status().message();
            expectSameStep(stepStateOf(*fromOutside), stepStateOf(*fromInside));
        }
    }
    EXPECT_TRUE(an::ui::history::waitForClosingHistories({}, 30000));
    pumpQuietly();
}

// A step is written wherever its own file fits: what the codec stages beside it while it writes and reads - the
// step's record - goes under a name shorter than the step's, so a history deep in a project's folder is not refused
// for a file nobody keeps.
TEST(HistoryStepCodec, AStepIsWrittenWhereverItsOwnFileFits)
{
    std::optional<TestSessionProvider> provider = openStepCodecSession();
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const an::tests::fixtures::ScopedTempPath root = an::tests::fixtures::makeScopedTempDir("afternight-deep-history");

    // A directory 190 characters long: with a step's name of 37 and the name its payload is written under, that is
    // within the 259 a path may be where paths are bounded, with little to spare.
    std::filesystem::path directory = root.path();
    constexpr std::size_t kDirectoryLength = 190u;
    // Whole segments of ten while one more leaves room for a last of its own, a separator and a character at least -
    // whatever the length of the temporary folder - then the last, which makes up the length.
    ASSERT_LT(directory.native().size() + 2u, kDirectoryLength);
    while (kDirectoryLength - directory.native().size() > 12u)
    {
        directory /= "0123456789";
    }
    directory /= std::string(kDirectoryLength - directory.native().size() - 1u, 'x');
    ASSERT_EQ(directory.native().size(), kDirectoryLength);
    std::error_code made;
    std::filesystem::create_directories(directory, made);
    ASSERT_FALSE(made) << made.message();

    const std::shared_ptr<an::consumer::ImageAsset> image = makeImage(session, 8, 6, 1, 1.0f, "M 42");
    ASSERT_NE(image, nullptr);
    const StepState original = stepStateOf(*image);
    const an::ui::history::StepCodecPtr codec = an::ui::history::historyStepCodec();
    const std::string path = utf8Of(directory / "0123456789abcdef0123456789abcdef.xisf");
    const an::ArtifactIdentity identity = stepIdentityOf("0123456789abcdef0123456789abcdef");
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue());
        const an::Result<std::string> written = codec->write(session, *lease, path, identity, {});
        ASSERT_TRUE(written.hasValue()) << written.status().message();
        EXPECT_EQ(*written, path);
    }
    an::Result<an::consumer::ImageAsset> read = codec->read(session, path, identity, {});
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    expectSameStep(original, stepStateOf(*read));
    EXPECT_EQ(entriesOf(directory), std::vector<std::string>{"0123456789abcdef0123456789abcdef.xisf"});
}

TEST(HistoryStepCodec, ASessionWhoseEngineWritesNoXisfImageKeepsItsStepsAsSnapshots)
{
    // A session with no codec adapter at all, as a suite that asks for none has.
    std::optional<TestSessionProvider> provider = openHistorySession();
    ASSERT_TRUE(provider.has_value());
    const an::tests::fixtures::ScopedTempPath directory =
        an::tests::fixtures::makeScopedTempDir("afternight-history-codec");
    FakeHistoryView view;
    view.image = makeImage(provider->engineSession(), 8, 6, 1, 1.0f);
    const StepState opened = stepStateOf(*view.image);
    {
        ImageHistoryTracker tracker(view, provider->engineSession(), nullptr, qtPath(directory.path()));
        tracker.registerInitialStep("opened");
        ASSERT_EQ(tracker.getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written)
            << tracker.getInitialStep()->m_Snapshot->failure();
        // The step is kept, not refused for the format, and the file says by its extension which codec reads it.
        EXPECT_EQ(extensionsBelow(directory.path()), std::set<std::string>{".ansnap"});
        EXPECT_TRUE(tracker.getInitialStep()->m_Snapshot->path().ends_with(".ansnap"));
        an::Result<an::consumer::ImageAsset> read = tracker.stepReader(0u)({});
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        expectSameStep(opened, stepStateOf(*read));
    }
    pumpQuietly();
}

} // namespace
