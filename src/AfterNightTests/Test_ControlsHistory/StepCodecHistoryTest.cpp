// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A history over each step codec: its steps are files of the codec's kind in the history's own directory, an edit is
/// recorded through it and an undo restores the state it kept whole, and a history given no codec keeps its steps as
/// snapshots.

#include "Test_ControlsHistory/StepCodecTestSupport.h"

#include "fixtures/ScopedTempPath.h"

#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QString>

#include <filesystem>
#include <set>
#include <string>

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

TEST(HistoryStepCodec, AHistoryGivenNoCodecKeepsItsStepsAsSnapshots)
{
    std::optional<TestSessionProvider> provider = openStepCodecSession();
    ASSERT_TRUE(provider.has_value());
    const an::tests::fixtures::ScopedTempPath directory =
        an::tests::fixtures::makeScopedTempDir("afternight-history-codec");
    FakeHistoryView view;
    view.image = makeImage(provider->engineSession(), 8, 6, 1, 1.0f);
    {
        ImageHistoryTracker tracker(view, provider->engineSession(), nullptr, qtPath(directory.path()));
        tracker.registerInitialStep("opened");
        ASSERT_EQ(tracker.getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);
        EXPECT_EQ(tracker.backupQueue().codec().name(), "snapshot");
        EXPECT_EQ(extensionsBelow(directory.path()), std::set<std::string>{".ansnap"});
    }
    pumpQuietly();
}

} // namespace
