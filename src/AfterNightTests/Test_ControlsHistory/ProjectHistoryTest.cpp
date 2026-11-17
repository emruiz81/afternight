// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A history that outlives its view: given a home, its steps land there in the home's compression beside an index that
/// lists the ones written; a view that closes leaves the home, where the steps still waiting are written behind it;
/// the steps a history already has go into the home as the files they are; a history kept for a file is attached
/// around that file when the image is opened again, set aside when the file changed, and less the steps whose files
/// are gone; and none of it waits on the thread that asks.

#include "Test_ControlsHistory/StepCodecTestSupport.h"

#include "controls/ImageHistoryIndex.h"

#include "fixtures/ScopedTempPath.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace an::tests::history;
using an::ui::history::FileIdentity;
using an::ui::history::HistoryCompaction;
using an::ui::history::HistoryCompactionResult;
using an::ui::history::HistoryEntry;
using an::ui::history::HistoryHome;
using an::ui::history::HistoryIndex;
using an::ui::history::HistoryIndexStep;
using an::ui::history::readHistoryIndex;
using an::ui::history::SnapshotState;
using an::ui::history::StepCompression;
using an::ui::history::StepsMove;
using an::ui::history::waitForClosingHistories;

QString qtPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path fsPath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

/// The names of the regular files directly in a directory.
std::set<std::string> filesIn(const QString& directory)
{
    std::set<std::string> names;
    const QStringList entries = QDir(directory).entryList(QDir::Files | QDir::Hidden);
    for (const QString& entry : entries)
    {
        names.insert(entry.toStdString());
    }
    return names;
}

/// The step files directly in a directory: the files of either kind a history's codec writes.
std::set<std::string> stepFilesIn(const QString& directory)
{
    std::set<std::string> steps;
    for (const std::string& name : filesIn(directory))
    {
        if (name.ends_with(an::ui::history::kXisfStepExtension) ||
            name.ends_with(an::ui::history::kSnapshotStepExtension))
        {
            steps.insert(name);
        }
    }
    return steps;
}

/// The bytes the step files directly in a directory take.
std::uint64_t stepBytesIn(const QString& directory)
{
    std::uint64_t bytes = 0u;
    for (const std::string& name : stepFilesIn(directory))
    {
        bytes += static_cast<std::uint64_t>(QFileInfo(QDir(directory).filePath(QString::fromStdString(name))).size());
    }
    return bytes;
}

/// The head of a file, where an XISF step states how its samples are compressed.
QByteArray headOf(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.read(64 * 1024);
}

/// The base value of the image a history's state `step` is: the opened image, then each edit's.
float baseOf(const std::size_t step)
{
    return step == 0u ? 1.0f : 10.0f * static_cast<float>(step);
}

class ProjectHistory : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Provider = openStepCodecSession();
        ASSERT_TRUE(m_Provider.has_value());
        m_Temporary = an::tests::fixtures::makeScopedTempDir("afternight-history-session");
        m_Project = an::tests::fixtures::makeScopedTempDir("afternight-history-project");
    }

    void TearDown() override
    {
        // A history that closed in a home finishes its writes behind it, with the session and the gate this case
        // owns.
        m_Gate.open();
        EXPECT_TRUE(waitForClosingHistories({}, 30000));
        pumpQuietly();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Provider->engineSession();
    }

    /// Where a history outside a home makes its own directory.
    [[nodiscard]] QString temporary() const
    {
        return qtPath(m_Temporary.path());
    }

    /// The directory a project keeps the history of an image in.
    [[nodiscard]] QString homeDirectory(const char* image = "image-1") const
    {
        return QDir(qtPath(m_Project.path())).filePath(QStringLiteral("history/") + QString::fromLatin1(image));
    }

    /// A file that stands for the image's file on disk: its bytes are what its identity is read from.
    [[nodiscard]] QString imageFile(const QByteArray& bytes = QByteArray(4096, 'a'),
                                    const char* name = "M42.fits") const
    {
        const QString path = QDir(qtPath(m_Project.path())).filePath(QString::fromLatin1(name));
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        EXPECT_EQ(file.write(bytes), bytes.size());
        file.close();
        return path;
    }

    [[nodiscard]] HistoryHome home(const QString& file = {},
                                   const StepCompression compression = StepCompression::Lz4) const
    {
        HistoryHome made;
        made.directory = homeDirectory();
        made.compression = compression;
        made.file = file;
        return made;
    }

    /// Enters a home and turns the event loop until the entry has landed; what it came to.
    [[nodiscard]] static HistoryEntry enter(ImageHistoryTracker& tracker, const HistoryHome& where,
                                            const StepsMove move = StepsMove::Move)
    {
        std::optional<HistoryEntry> entry;
        tracker.enterHome(where, move, [&entry](const HistoryEntry& landed) { entry = landed; });
        EXPECT_TRUE(pumpUntil([&entry]() { return entry.has_value(); })) << "the entry never landed";
        return entry.value_or(HistoryEntry{});
    }

    /// Records an edit of the view into an image of a base value, as the view records one.
    [[nodiscard]] bool edit(ImageHistoryTracker& tracker, FakeHistoryView& view, const float base,
                            const std::string& name) const
    {
        const std::shared_ptr<an::consumer::ImageAsset> edited = makeImage(session(), 8, 6, 1, base);
        if (edited == nullptr || !recordEdit(tracker, *view.image, *edited, name))
        {
            return false;
        }
        view.image = edited;
        return true;
    }

    /// What a step restores, read through the history.
    [[nodiscard]] static StepState stateOfStep(const ImageHistoryTracker& tracker, const std::size_t step)
    {
        const ImageHistoryTracker::StepReader read = tracker.stepReader(step);
        if (!read)
        {
            ADD_FAILURE() << "the history has no step " << step;
            return {};
        }
        an::Result<an::consumer::ImageAsset> image = read({});
        if (!image.hasValue())
        {
            ADD_FAILURE() << "step " << step << " did not read: " << image.status().message();
            return {};
        }
        return stepStateOf(*image);
    }

    /// Turns the event loop until the home's index says what `said` asks of it.
    [[nodiscard]] bool indexSays(const std::function<bool(const HistoryIndex&)>& said) const
    {
        const QString directory = homeDirectory();
        return pumpUntil([&directory, &said]() {
            const std::optional<HistoryIndex> index = readHistoryIndex(directory);
            return index.has_value() && said(*index);
        });
    }

    /// What an earlier session left in the home.
    struct Kept
    {
        /// Each state the history kept, the opened image first.
        std::vector<StepState> states;

        /// Each step's id and the name of its file.
        std::vector<QString> ids;
        std::vector<QString> files;
    };

    /// A history of `edits` edits kept in the home for `file`, which was saved at state `savedAt`, by a view that then
    /// closed.
    [[nodiscard]] Kept keepHistory(const QString& file, const std::size_t edits, const std::size_t savedAt) const
    {
        Kept kept;
        FakeHistoryView view;
        view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
        {
            ImageHistoryTracker tracker(view, session(), nullptr, temporary());
            tracker.registerInitialStep("opened");
            kept.states.push_back(stepStateOf(*view.image));
            EXPECT_TRUE(enter(tracker, home(file)).entered);
            if (savedAt == 0u)
            {
                tracker.fileSaved(file);
            }
            for (std::size_t step = 1u; step <= edits; ++step)
            {
                EXPECT_TRUE(edit(tracker, view, baseOf(step), "Edit " + std::to_string(step)));
                kept.states.push_back(stepStateOf(*view.image));
                if (savedAt == step)
                {
                    tracker.fileSaved(file);
                }
            }
            tracker.waitAllAsyncJobsDone();
            EXPECT_TRUE(indexSays([edits](const HistoryIndex& index) {
                return index.identity.has_value() && index.steps.size() == edits + 1u && !index.saved.isEmpty();
            })) << "the index never said the steps and the file's identity";
        }
        EXPECT_TRUE(waitForClosingHistories(homeDirectory(), 30000));
        const std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());
        EXPECT_TRUE(index.has_value());
        if (index.has_value())
        {
            for (const HistoryIndexStep& step : index->steps)
            {
                kept.ids.push_back(step.id);
                kept.files.push_back(step.file);
            }
        }
        return kept;
    }

    std::optional<TestSessionProvider> m_Provider;
    an::tests::fixtures::ScopedTempPath m_Temporary;
    an::tests::fixtures::ScopedTempPath m_Project;

    /// What holds the writes of a home that was given its codec.
    WriterGate m_Gate;
};

// ---- The steps and their index.

TEST_F(ProjectHistory, StepsLandInTheProjectInItsFormat)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    // Outside a home the history writes uncompressed steps; the home asks for Zstandard.
    ImageHistoryTracker tracker(view, session(), an::ui::history::historyStepCodec(StepCompression::None), temporary());
    tracker.registerInitialStep("opened");
    ASSERT_EQ(tracker.getInitialStep()->waitAsyncJobsDone(), SnapshotState::Written);
    const std::string own = tracker.snapshotDirectory();
    EXPECT_FALSE(tracker.home().has_value());

    const HistoryEntry entry = enter(tracker, home({}, StepCompression::Zstd));
    ASSERT_TRUE(entry.entered) << entry.problem.toStdString();
    EXPECT_FALSE(entry.attached);
    ASSERT_TRUE(tracker.home().has_value());
    EXPECT_EQ(tracker.home()->directory, homeDirectory());
    EXPECT_EQ(fsPath(QString::fromStdString(tracker.snapshotDirectory())), fsPath(homeDirectory()));

    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "Curves"));
    tracker.waitAllAsyncJobsDone();
    const std::vector<ImageHistoryStepSPtr> steps = tracker.getImageHistorySteps();
    ASSERT_EQ(steps.size(), 1u);

    // Both steps are files of the home, XISF images, and the one recorded there is compressed as the home says.
    const QString initialFile = QString::fromStdString(tracker.getInitialStep()->m_Snapshot->path());
    const QString editFile = QString::fromStdString(steps.front()->m_Snapshot->path());
    EXPECT_EQ(fsPath(QFileInfo(initialFile).absolutePath()), fsPath(homeDirectory()));
    EXPECT_EQ(fsPath(QFileInfo(editFile).absolutePath()), fsPath(homeDirectory()));
    EXPECT_TRUE(initialFile.endsWith(QStringLiteral(".xisf")));
    EXPECT_TRUE(editFile.endsWith(QStringLiteral(".xisf")));
    EXPECT_TRUE(headOf(editFile).contains("zstd")) << "the step recorded in the home is not in its compression";
    EXPECT_FALSE(headOf(initialFile).contains("zstd")) << "the step that moved in was written again";
    EXPECT_EQ(stepFilesIn(homeDirectory()).size(), 2u);
    EXPECT_FALSE(std::filesystem::exists(fsPath(QString::fromStdString(own))))
        << "the history's own directory outlived the move of its steps";

    // The index beside them lists both, and the step the view shows.
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    const std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(index->steps[0].file, QFileInfo(initialFile).fileName());
    EXPECT_EQ(index->steps[1].file, QFileInfo(editFile).fileName());
    EXPECT_EQ(index->steps[1].name, QStringLiteral("Curves"));
    EXPECT_EQ(index->current, QString::fromStdString(steps.front()->getUuid()));
    EXPECT_GT(index->steps[1].bytes, 0u);
}

TEST_F(ProjectHistory, IndexRoundTrip)
{
    HistoryIndex index;
    index.steps.push_back(HistoryIndexStep{.id = QStringLiteral("inherited-0"),
                                           .name = QStringLiteral("Stretch"),
                                           .description = QStringLiteral("of the image it was made from"),
                                           .inherited = true});
    index.steps.push_back(HistoryIndexStep{.id = QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
                                           .name = QStringLiteral("Initial state"),
                                           .description = QStringLiteral("opened \"M 42\" \u2014 f\u00fcr Sie"),
                                           .file = QStringLiteral("0123456789abcdef0123456789abcdef.xisf"),
                                           .run = QStringLiteral("fedcba9876543210fedcba9876543210"),
                                           .image = QStringLiteral("M42"),
                                           .time = QDateTime::fromMSecsSinceEpoch(1'700'000'000'123, QTimeZone::UTC),
                                           .bytes = 173'000'000u});
    index.steps.push_back(HistoryIndexStep{.id = QStringLiteral("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"),
                                           .name = QStringLiteral("Curves"),
                                           .file = QStringLiteral("00000000000000000000000000000001.ansnap"),
                                           .run = QStringLiteral("fedcba9876543210fedcba9876543210"),
                                           .image = QStringLiteral("M42_curves"),
                                           .time = QDateTime::fromMSecsSinceEpoch(1'700'000'060'000, QTimeZone::UTC),
                                           .bytes = 12u});
    index.current = index.steps[2].id;
    index.saved = index.steps[1].id;
    index.file = QStringLiteral("E:/sky/M 42.fits");
    index.identity = FileIdentity{
        .size = 230'000'000u, .modifiedMs = 1'700'000'000'000, .headSha256 = QString(64, QLatin1Char('a'))};

    // As text, and as the file of a directory.
    const std::optional<HistoryIndex> parsed =
        an::ui::history::parseHistoryIndex(an::ui::history::formatHistoryIndex(index));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, index);
    ASSERT_TRUE(QDir().mkpath(homeDirectory()));
    ASSERT_TRUE(an::ui::history::writeHistoryIndex(homeDirectory(), index));
    const std::optional<HistoryIndex> read = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, index);
    EXPECT_EQ(filesIn(homeDirectory()), std::set<std::string>{an::ui::history::kHistoryIndexFileName})
        << "writing the index left something beside it";

    // What is no index is none: other text, a later version, and a step whose file is outside the directory.
    EXPECT_FALSE(an::ui::history::parseHistoryIndex("{}").has_value());
    EXPECT_FALSE(an::ui::history::parseHistoryIndex("not an index").has_value());
    QByteArray later = an::ui::history::formatHistoryIndex(index);
    later.replace("\"version\": 1", "\"version\": 2");
    EXPECT_FALSE(an::ui::history::parseHistoryIndex(later).has_value());
    HistoryIndex outside = index;
    outside.steps[1].file = QStringLiteral("../elsewhere.xisf");
    EXPECT_FALSE(an::ui::history::parseHistoryIndex(an::ui::history::formatHistoryIndex(outside)).has_value());
    EXPECT_FALSE(readHistoryIndex(homeDirectory("no-such-image")).has_value());
}

TEST_F(ProjectHistory, IndexListsOnlyWrittenSteps)
{
    WriterGate& gate = m_Gate;
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    HistoryHome where = home();
    where.codec = gate.codec();
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, where).entered);
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 1u; }));

    // An edit whose file the writer has not written yet: the index names it as the step shown and does not list it.
    gate.close();
    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "Curves"));
    const std::string editId = tracker.getImageHistorySteps().front()->getUuid();
    ASSERT_TRUE(gate.waitForStarted(1u));
    ASSERT_TRUE(
        indexSays([&editId](const HistoryIndex& index) { return index.current == QString::fromStdString(editId); }));
    std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(index.has_value());
    ASSERT_EQ(index->steps.size(), 1u) << "the index lists a step whose file is not written";
    EXPECT_EQ(index->steps.front().id, QString::fromStdString(tracker.getInitialStep()->getUuid()));

    // Once the file lands the index lists it.
    gate.open();
    tracker.waitAllAsyncJobsDone();
    ASSERT_TRUE(indexSays([](const HistoryIndex& listed) { return listed.steps.size() == 2u; }));
    index = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(index->steps[1].id, QString::fromStdString(editId));
    for (const HistoryIndexStep& step : index->steps)
    {
        EXPECT_TRUE(QFileInfo(QDir(homeDirectory()).filePath(step.file)).isFile()) << step.file.toStdString();
    }
}

TEST_F(ProjectHistory, AStepWithoutAnIndexEntryIsIgnoredAndCleaned)
{
    const QString file = imageFile();
    const Kept kept = keepHistory(file, 1u, 0u);
    ASSERT_EQ(kept.files.size(), 2u);

    // The process died between a step's file and the index: a step file the index does not list.
    const QString stray = QDir(homeDirectory()).filePath(QStringLiteral("ffffffffffffffffffffffffffffffff.xisf"));
    ASSERT_TRUE(QFile::copy(QDir(homeDirectory()).filePath(kept.files[1]), stray));
    ASSERT_EQ(stepFilesIn(homeDirectory()).size(), 3u);

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.entered) << entry.problem.toStdString();
    EXPECT_TRUE(entry.attached);
    EXPECT_EQ(tracker.getTotalStepCount(), 2u) << "the file no index lists became a step";
    EXPECT_FALSE(QFileInfo::exists(stray)) << "the file no index lists was left in the history";
    EXPECT_EQ(stepFilesIn(homeDirectory()),
              (std::set<std::string>{kept.files[0].toStdString(), kept.files[1].toStdString()}));
}

TEST_F(ProjectHistory, ALeftoverRecordFileIsSweptWhenTheHistoryOpens)
{
    ASSERT_TRUE(QDir().mkpath(homeDirectory()));
    const QString record = QDir(homeDirectory())
                               .filePath(QStringLiteral("0123456789abcdef0123456789abcdef.xisf."
                                                        "00112233445566778899aabbccddeeff.record"));
    {
        QFile left(record);
        ASSERT_TRUE(left.open(QIODevice::WriteOnly));
        left.write("what a killed write staged");
    }
    // A folder of the home - a history set aside - is not entered.
    const QString aside = QDir(homeDirectory()).filePath(QStringLiteral("detached/20261101-000000"));
    ASSERT_TRUE(QDir().mkpath(aside));
    const QString kept = QDir(aside).filePath(QStringLiteral("old.record"));
    {
        QFile left(kept);
        ASSERT_TRUE(left.open(QIODevice::WriteOnly));
    }

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, home()).entered);
    EXPECT_FALSE(QFileInfo::exists(record)) << "the record a killed write left is still in the history";
    EXPECT_TRUE(QFileInfo::exists(kept)) << "the sweep entered a folder of the home";
}

TEST_F(ProjectHistory, AStepBeyondWhatXisfCarriesIsKeptAsASnapshot)
{
    // 156 megapixels of mask with no pattern: more bits than the cards of one XISF image carry.
    std::optional<an::consumer::ImageAsset> beyond = patternlessMaskImage(session(), 13000u, 12000u);
    ASSERT_TRUE(beyond.has_value());
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, home()).entered);

    // The edit is recorded, not refused for the format: its step is a snapshot beside the XISF one.
    const auto edited = std::make_shared<an::consumer::ImageAsset>(std::move(*beyond));
    const StepState state = stepStateOf(*edited);
    ASSERT_TRUE(recordEdit(tracker, *view.image, *edited, "Mask"));
    view.image = edited;
    tracker.waitAllAsyncJobsDone();
    const ImageHistoryStepSPtr step = tracker.getImageHistorySteps().front();
    ASSERT_EQ(step->snapshotState(), SnapshotState::Written) << step->m_Snapshot->failure();
    EXPECT_TRUE(step->m_Snapshot->path().ends_with(an::ui::history::kSnapshotStepExtension));
    EXPECT_TRUE(tracker.getInitialStep()->m_Snapshot->path().ends_with(an::ui::history::kXisfStepExtension));

    // The index names each file with its extension, which says which codec reads it; and it reads back whole.
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    const std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(index.has_value());
    EXPECT_TRUE(index->steps[0].file.endsWith(QStringLiteral(".xisf")));
    EXPECT_TRUE(index->steps[1].file.endsWith(QStringLiteral(".ansnap")));
    expectSameStep(state, stateOfStep(tracker, 1u));
}

// ---- A view that closes, and the thread that asks.

TEST_F(ProjectHistory, ClosingAViewFinishesItsWritesAndKeepsItsDirectory)
{
    WriterGate& gate = m_Gate;
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    HistoryHome where = home();
    where.codec = gate.codec();
    QString editFile;
    std::string editId;
    {
        auto tracker = std::make_unique<ImageHistoryTracker>(view, session(), nullptr, temporary());
        tracker->registerInitialStep("opened");
        ASSERT_TRUE(enter(*tracker, where).entered);
        gate.close();
        ASSERT_TRUE(edit(*tracker, view, baseOf(1u), "Curves"));
        ASSERT_TRUE(gate.waitForStarted(1u));
        editFile = QString::fromStdString(tracker->getImageHistorySteps().front()->m_Snapshot->path());
        editId = tracker->getImageHistorySteps().front()->getUuid();

        // The view closes while the step's write is held: nothing here waits for it.
        QElapsedTimer closing;
        closing.start();
        tracker.reset();
        EXPECT_LT(closing.elapsed(), 5000) << "closing the history waited for its writer";
    }
    EXPECT_FALSE(QFileInfo::exists(editFile)) << "the gate did not hold the write";
    EXPECT_TRUE(QFileInfo(homeDirectory()).isDir());
    EXPECT_FALSE(waitForClosingHistories(homeDirectory(), 50)) << "the history is not finishing its writes";

    // The write finishes behind the view, and the index is closed after it lands.
    gate.open();
    ASSERT_TRUE(waitForClosingHistories(homeDirectory(), 30000));
    EXPECT_TRUE(QFileInfo::exists(editFile)) << "the step still waiting when the view closed was not written";
    const std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(index.has_value());
    ASSERT_EQ(index->steps.size(), 2u);
    EXPECT_EQ(index->steps[1].id, QString::fromStdString(editId));
    EXPECT_EQ(index->current, QString::fromStdString(editId));
    EXPECT_TRUE(QDir(temporary()).isEmpty()) << "the history's own directory was left in the temporary folder";
}

TEST_F(ProjectHistory, NeverWaitsOnTheGuiThread)
{
    WriterGate& gate = m_Gate;
    const QString file = imageFile();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    HistoryHome where = home(file);
    where.codec = gate.codec();
    auto tracker = std::make_unique<ImageHistoryTracker>(view, session(), gate.codec(), temporary());
    tracker->registerInitialStep("opened");
    ASSERT_TRUE(enter(*tracker, where).entered);
    tracker->waitAllAsyncJobsDone();

    // A write is held from here on: whatever returns below returned without the writer.
    const std::size_t written = gate.started();
    gate.close();
    ASSERT_TRUE(tracker->registerShownState("Shown", "a state already shown"));
    ASSERT_TRUE(gate.waitForStarted(written + 1u));
    QElapsedTimer asked;
    asked.start();
    ASSERT_TRUE(tracker->registerShownState("Shown again", "another, behind the one held"));
    tracker->fileSaved(file);
    EXPECT_GT(tracker->stepFileBytes(), 0u);
    EXPECT_TRUE(tracker->planCompaction(HistoryCompaction{}).ok);
    EXPECT_TRUE(tracker->compact(HistoryCompaction{}).ok);
    tracker->leaveHome();
    EXPECT_FALSE(tracker->home().has_value());
    ASSERT_TRUE(tracker->registerShownState("Shown outside", "recorded after the home was left"));
    tracker->enterHome(where, StepsMove::Move);
    EXPECT_TRUE(tracker->isEnteringHome());
    tracker.reset();
    EXPECT_LT(asked.elapsed(), 5000) << "a call on the thread that asks waited for the history's writer";
    // The write held when the calls began is the only one that started since.
    EXPECT_EQ(gate.started(), written + 1u);
    gate.open();
}

TEST_F(ProjectHistory, PendingSnapshotAllowanceStillHolds)
{
    // Room for one copy of the image waiting for the writer, and no more.
    std::uint64_t copyBytes = 0u;
    {
        const std::shared_ptr<an::consumer::ImageAsset> probe = makeImage(session(), 8, 6, 1, baseOf(0u));
        ASSERT_NE(probe, nullptr);
        an::Result<an::consumer::AssetReadLease> lease = probe->read();
        ASSERT_TRUE(lease.hasValue());
        copyBytes = an::ui::history::historyBytesOf(*lease);
    }
    an::consumer::MemoryAllowances memory;
    memory.pendingSnapshots.bytes = copyBytes + copyBytes / 2u;
    memory.pendingSnapshots.admitsOneOversized = false;
    m_Provider = openStepCodecSession(memory);
    ASSERT_TRUE(m_Provider.has_value());

    WriterGate& gate = m_Gate;
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    HistoryHome where = home();
    where.codec = gate.codec();
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, where).entered);
    tracker.waitAllAsyncJobsDone();
    an::consumer::ConsumerMemory& allowance = session().memory();
    ASSERT_EQ(allowance.counters(an::consumer::MemoryCategory::PendingSnapshots).entries, 0u);

    // One edit's copy waits at the gate of the home's writer, and holds the session's allowance as any other does.
    gate.close();
    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "First"));
    ASSERT_TRUE(gate.waitForStarted(1u));
    EXPECT_EQ(tracker.backupQueue().pendingSnapshots(), 1u);
    EXPECT_EQ(allowance.counters(an::consumer::MemoryCategory::PendingSnapshots).entries, 1u);
    EXPECT_FALSE(allowance.reserve(an::consumer::MemoryCategory::PendingSnapshots, copyBytes).hasValue())
        << "a second copy was admitted beyond the allowance";

    // A second edit nobody prepared is refused at once: nothing is shown over a state that is not kept, and nothing
    // waits.
    const std::shared_ptr<an::consumer::ImageAsset> second = makeImage(session(), 8, 6, 1, baseOf(2u));
    EXPECT_FALSE(tracker.registerEditBeforePublishing("Second", "", *second));
    EXPECT_EQ(tracker.getImageHistorySteps().size(), 1u);

    // With the writer let through the room comes back, and the same edit prepared on its worker is recorded.
    gate.open();
    tracker.waitAllAsyncJobsDone();
    ASSERT_TRUE(waitUntil(
        [&allowance]() { return allowance.counters(an::consumer::MemoryCategory::PendingSnapshots).entries == 0u; }));
    ASSERT_TRUE(edit(tracker, view, baseOf(2u), "Second"));
    tracker.waitAllAsyncJobsDone();
    EXPECT_EQ(tracker.getImageHistorySteps().size(), 2u);
    EXPECT_EQ(tracker.backupQueue().peakPendingSnapshots(), 1u);
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 3u; }));
}

// ---- The steps a history already has.

TEST_F(ProjectHistory, MoveInKeepsEveryStep)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "First"));
    ASSERT_TRUE(edit(tracker, view, baseOf(2u), "Second"));
    tracker.waitAllAsyncJobsDone();

    // Each step as it is before the move: what it restores, its file's name, and when the file was written.
    std::vector<StepState> before;
    std::map<std::string, std::filesystem::file_time_type> written;
    std::uint64_t bytes = 0u;
    for (std::size_t step = 0u; step < 3u; ++step)
    {
        before.push_back(stateOfStep(tracker, step));
    }
    const std::filesystem::path own = fsPath(QString::fromStdString(tracker.snapshotDirectory()));
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(own))
    {
        written[entry.path().filename().string()] = entry.last_write_time();
        bytes += entry.file_size();
    }
    ASSERT_EQ(written.size(), 3u);
    EXPECT_EQ(tracker.stepFileBytes(), bytes);
    // Long enough for a file written again to say so by its time.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;
    std::optional<HistoryEntry> entry;
    tracker.enterHome(
        home(), StepsMove::Move, [&entry](const HistoryEntry& landed) { entry = landed; },
        [&progress](const std::uint64_t moved, const std::uint64_t total) { progress.emplace_back(moved, total); });
    ASSERT_TRUE(pumpUntil([&entry]() { return entry.has_value(); }));
    ASSERT_TRUE(entry->entered) << entry->problem.toStdString();
    EXPECT_EQ(entry->stepsMoved, 3u);
    EXPECT_EQ(entry->stepsCopied, 0u) << "on one volume a step's file is renamed, not copied";
    EXPECT_EQ(entry->bytesMoved, bytes);
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back(), (std::pair<std::uint64_t, std::uint64_t>{bytes, bytes}));

    // The same files, under the same names, never written again; the originals are gone with their directory.
    const std::filesystem::path there = fsPath(homeDirectory());
    for (const auto& [name, time] : written)
    {
        ASSERT_TRUE(std::filesystem::exists(there / name)) << name;
        EXPECT_EQ(std::filesystem::last_write_time(there / name), time) << name << " was written again";
    }
    EXPECT_FALSE(std::filesystem::exists(own));

    // And every step is what it was, by the comparison a codec is held to.
    for (std::size_t step = 0u; step < 3u; ++step)
    {
        expectSameStep(before[step], stateOfStep(tracker, step));
    }
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 3u; }));

    // An undo after the move restores from the home.
    ASSERT_TRUE(tracker.undoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(before[1], stepStateOf(*view.image));
}

TEST_F(ProjectHistory, AcrossVolumesAStepIsCopiedAndTheOriginalRemovedAfter)
{
    const std::filesystem::path from = m_Temporary.path() / "step.xisf";
    const std::filesystem::path to = m_Project.path() / "step.xisf";
    QByteArray bytes(3 * 1024 * 1024 + 17, '\0');
    for (qsizetype index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<char>(mixedBits(static_cast<std::uint32_t>(index), 0u, 0u));
    }
    {
        QFile file(qtPath(from));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }

    // Where no rename can take it there, the file is copied whole and only then removed.
    const std::optional<an::ui::history::StepFileMove> moved = an::ui::history::moveStepFile(from, to, false);
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(*moved, an::ui::history::StepFileMove::Copied);
    EXPECT_FALSE(std::filesystem::exists(from));
    QFile copied(qtPath(to));
    ASSERT_TRUE(copied.open(QIODevice::ReadOnly));
    EXPECT_EQ(copied.readAll(), bytes);
    copied.close();

    // On one volume it is renamed; and a file that cannot go leaves both ends as they were.
    const std::optional<an::ui::history::StepFileMove> back = an::ui::history::moveStepFile(to, from);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, an::ui::history::StepFileMove::Renamed);
    EXPECT_FALSE(
        an::ui::history::moveStepFile(from, m_Project.path() / "no-such-folder" / "step.xisf", false).has_value());
    EXPECT_TRUE(std::filesystem::exists(from));
}

TEST_F(ProjectHistory, StepsLeftWhereTheyAreAreNotTheHomes)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    tracker.waitAllAsyncJobsDone();
    const std::string own = tracker.snapshotDirectory();

    // The steps the history has stay with the session; the ones recorded from now on are the home's.
    const HistoryEntry entry = enter(tracker, home(), StepsMove::Leave);
    ASSERT_TRUE(entry.entered);
    EXPECT_EQ(entry.stepsMoved, 0u);
    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "Curves"));
    tracker.waitAllAsyncJobsDone();
    EXPECT_EQ(historyFilesIn(fsPath(QString::fromStdString(own))).size(), 1u);
    EXPECT_EQ(stepFilesIn(homeDirectory()).size(), 1u);
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 1u; }));
    EXPECT_EQ(readHistoryIndex(homeDirectory())->steps.front().name, QStringLiteral("Curves"));

    // Undo still reaches the state that stayed behind.
    ASSERT_TRUE(tracker.undoStep());
    ASSERT_TRUE(settleRestore(tracker));
    EXPECT_EQ(samplesOf(view.image), samplesOf(makeImage(session(), 8, 6, 1, baseOf(0u))));
}

TEST_F(ProjectHistory, LeavingAHomeKeepsItsStepsAndWritesTheNextElsewhere)
{
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, home()).entered);
    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "Kept"));
    tracker.waitAllAsyncJobsDone();
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    const std::set<std::string> kept = stepFilesIn(homeDirectory());

    tracker.leaveHome();
    EXPECT_FALSE(tracker.home().has_value());
    ASSERT_TRUE(edit(tracker, view, baseOf(2u), "Elsewhere"));
    tracker.waitAllAsyncJobsDone();
    EXPECT_EQ(stepFilesIn(homeDirectory()), kept) << "a step recorded after leaving landed in the home";
    EXPECT_EQ(readHistoryIndex(homeDirectory())->steps.size(), 2u);

    // The steps the home holds are still what undo restores, and dropping them here removes nothing there.
    ASSERT_TRUE(tracker.undoStep());
    ASSERT_TRUE(settleRestore(tracker));
    ASSERT_TRUE(tracker.undoStep());
    ASSERT_TRUE(settleRestore(tracker));
    EXPECT_EQ(samplesOf(view.image), samplesOf(makeImage(session(), 8, 6, 1, baseOf(0u))));
    ASSERT_TRUE(edit(tracker, view, baseOf(3u), "A branch"));
    tracker.waitAllAsyncJobsDone();
    EXPECT_EQ(stepFilesIn(homeDirectory()), kept) << "a history that left its home removed a step from it";
}

TEST_F(ProjectHistory, OneHistoryKeepsAHomeAtATime)
{
    const QString file = imageFile();
    FakeHistoryView first;
    first.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    auto holder = std::make_unique<ImageHistoryTracker>(first, session(), nullptr, temporary());
    holder->registerInitialStep("opened");
    ASSERT_TRUE(enter(*holder, home(file)).entered);
    holder->fileSaved(file);
    ASSERT_TRUE(edit(*holder, first, baseOf(1u), "Curves"));
    holder->waitAllAsyncJobsDone();
    ASSERT_TRUE(
        indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u && !index.saved.isEmpty(); }));
    const std::set<std::string> kept = stepFilesIn(homeDirectory());
    const std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());

    // A second view of the same file: the home is the first view's, so the second is refused it - nothing there is
    // read as its history, swept or written - and goes on with a history of the session's.
    FakeHistoryView second;
    second.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker other(second, session(), nullptr, temporary());
    other.registerInitialStep("opened");
    const HistoryEntry refused = enter(other, home(file));
    EXPECT_FALSE(refused.entered);
    EXPECT_FALSE(refused.attached);
    EXPECT_FALSE(refused.problem.isEmpty());
    EXPECT_FALSE(other.home().has_value());
    EXPECT_EQ(other.getTotalStepCount(), 1u);
    ASSERT_TRUE(edit(other, second, 300.0f, "Its own"));
    other.waitAllAsyncJobsDone();
    EXPECT_EQ(stepFilesIn(homeDirectory()), kept) << "a second history wrote into, or swept, a home another keeps";
    EXPECT_EQ(readHistoryIndex(homeDirectory()), index);
    // The first view's history is whole.
    ASSERT_TRUE(holder->undoStep());
    ASSERT_TRUE(settleRestore(*holder));
    EXPECT_EQ(samplesOf(first.image), samplesOf(makeImage(session(), 8, 6, 1, baseOf(0u))));

    // Once the first view has closed and its history has finished there, the home can be entered.
    holder.reset();
    const HistoryEntry entered = enter(other, home(file));
    EXPECT_TRUE(entered.entered) << entered.problem.toStdString();
    EXPECT_TRUE(other.home().has_value());
}

// ---- Saving.

TEST_F(ProjectHistory, SavingMovesTheSavedStepAndRecordsTheIdentity)
{
    const QString file = imageFile(QByteArray(4096, 'a'));
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, home(file)).entered);

    // Read from the file: the first state is the one the file equals.
    tracker.fileSaved(file);
    const QString initial = QString::fromStdString(tracker.getInitialStep()->getUuid());
    EXPECT_EQ(tracker.savedStep(), initial.toStdString());
    const std::optional<FileIdentity> first = an::ui::history::readFileIdentity(file);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(
        indexSays([&](const HistoryIndex& index) { return index.saved == initial && index.identity == first; }));

    // An edit leaves the saved step where it was.
    ASSERT_TRUE(edit(tracker, view, baseOf(1u), "Curves"));
    const QString edited = QString::fromStdString(tracker.getImageHistorySteps().front()->getUuid());
    tracker.waitAllAsyncJobsDone();
    ASSERT_TRUE(indexSays([&](const HistoryIndex& index) { return index.current == edited; }));
    EXPECT_EQ(readHistoryIndex(homeDirectory())->saved, initial);

    // Saved over its file: the step shown is the saved one, and the index states the file's new identity.
    (void)imageFile(QByteArray(8192, 'b'));
    tracker.fileSaved(file);
    const std::optional<FileIdentity> second = an::ui::history::readFileIdentity(file);
    ASSERT_TRUE(second.has_value());
    ASSERT_NE(*first, *second);
    ASSERT_TRUE(
        indexSays([&](const HistoryIndex& index) { return index.saved == edited && index.identity == second; }));
    EXPECT_EQ(readHistoryIndex(homeDirectory())->file, file);

    // Saved under another name: the history stays with the image, and the index follows the file.
    const QString other = imageFile(QByteArray(1024, 'c'), "M42-copy.fits");
    tracker.fileSaved(other);
    const std::optional<FileIdentity> third = an::ui::history::readFileIdentity(other);
    ASSERT_TRUE(indexSays([&](const HistoryIndex& index) { return index.file == other && index.identity == third; }));
    ASSERT_TRUE(tracker.home().has_value());
    EXPECT_EQ(tracker.home()->file, other);
}

// ---- Compacting.

TEST_F(ProjectHistory, CompactKeepsTheCurrentStateAndFreesWhatItSays)
{
    const QString file = imageFile();
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    StepState shown;
    {
        ImageHistoryTracker tracker(view, session(), nullptr, temporary());
        tracker.registerInitialStep("opened");
        ASSERT_TRUE(enter(tracker, home(file)).entered);
        tracker.fileSaved(file);
        for (std::size_t step = 1u; step <= 4u; ++step)
        {
            ASSERT_TRUE(edit(tracker, view, baseOf(step), "Edit " + std::to_string(step)));
        }
        tracker.waitAllAsyncJobsDone();
        ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 5u; }));
        shown = stepStateOf(*view.image);

        // A history a view has open, to its last two steps: what it says it frees is what the step files lose.
        const std::uint64_t before = stepBytesIn(homeDirectory());
        HistoryCompaction lastTwo;
        lastTwo.keep = HistoryCompaction::Keep::LastSteps;
        lastTwo.steps = 2u;
        const HistoryCompactionResult planned = tracker.planCompaction(lastTwo);
        ASSERT_TRUE(planned.ok);
        EXPECT_EQ(planned.stepsRemoved, 3u);
        EXPECT_GT(planned.bytesFreed, 0u);
        EXPECT_EQ(stepBytesIn(homeDirectory()), before) << "a plan removed something";
        const HistoryCompactionResult done = tracker.compact(lastTwo);
        EXPECT_EQ(done, planned);
        ASSERT_TRUE(pumpUntil([&]() { return stepFilesIn(homeDirectory()).size() == 2u; }));
        EXPECT_EQ(before - stepBytesIn(homeDirectory()), planned.bytesFreed);
        EXPECT_EQ(tracker.getTotalStepCount(), 2u);
        EXPECT_EQ(tracker.getCurrentStep(), 1u);
        expectSameStep(shown, stepStateOf(*view.image));
        ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    }
    ASSERT_TRUE(waitForClosingHistories(homeDirectory(), 30000));

    // The same history with no view open, to its current state alone.
    const std::uint64_t before = stepBytesIn(homeDirectory());
    HistoryCompaction current;
    current.keep = HistoryCompaction::Keep::CurrentState;
    const HistoryCompactionResult planned = an::ui::history::planStoredHistoryCompaction(homeDirectory(), current);
    ASSERT_TRUE(planned.ok);
    EXPECT_EQ(planned.stepsRemoved, 1u);
    EXPECT_EQ(stepBytesIn(homeDirectory()), before);
    const HistoryCompactionResult done = an::ui::history::compactStoredHistory(homeDirectory(), current);
    EXPECT_EQ(done, planned);
    EXPECT_EQ(before - stepBytesIn(homeDirectory()), planned.bytesFreed);
    const std::optional<HistoryIndex> index = readHistoryIndex(homeDirectory());
    ASSERT_TRUE(index.has_value());
    ASSERT_EQ(index->steps.size(), 1u);
    EXPECT_EQ(index->steps.front().id, index->current);
    EXPECT_EQ(stepFilesIn(homeDirectory()), std::set<std::string>{index->steps.front().file.toStdString()});
    EXPECT_EQ(an::ui::history::measureStoredHistory(homeDirectory()).steps, 1u);

    // What is left is the state the view showed: opened again, redo reaches it bit for bit.
    FakeHistoryView again;
    again.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(again, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.attached);
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(shown, stepStateOf(*again.image));

    // A directory that holds no index is not compacted.
    EXPECT_FALSE(an::ui::history::compactStoredHistory(homeDirectory("no-such-image"), current).ok);
}

// ---- Opening the image again.

TEST_F(ProjectHistory, ReopenShowsTheFileWithTheSavedStepCurrent)
{
    const QString file = imageFile();
    const Kept kept = keepHistory(file, 2u, 0u);
    ASSERT_EQ(kept.ids.size(), 3u);

    // The image is opened again: the view shows its file, and its history starts as any other.
    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    const std::shared_ptr<an::consumer::ImageAsset> shown = view.image;
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const std::string own = tracker.snapshotDirectory();

    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.entered) << entry.problem.toStdString();
    EXPECT_TRUE(entry.attached);
    EXPECT_FALSE(entry.detached);
    EXPECT_EQ(entry.stepsLeftOut, 0u);

    // The kept history is around the file: the saved step is the one shown, and nothing was restored to say so.
    EXPECT_EQ(tracker.getTotalStepCount(), 3u);
    EXPECT_EQ(tracker.getCurrentStep(), 0u);
    EXPECT_EQ(tracker.savedStep(), kept.ids[0].toStdString());
    EXPECT_EQ(tracker.getInitialStep()->getUuid(), kept.ids[0].toStdString());
    EXPECT_EQ(view.image, shown) << "attaching the history replaced the image the view shows";
    EXPECT_EQ(view.imageRestores, 0u);
    EXPECT_FALSE(tracker.hasUndoSteps());
    EXPECT_TRUE(tracker.hasRedoSteps());
    EXPECT_EQ(tracker.getNextStepName(), "Edit 1");

    // The copy the new history took of the file is not kept twice.
    tracker.waitAllAsyncJobsDone();
    EXPECT_TRUE(pumpUntil([&]() { return stepFilesIn(homeDirectory()).size() == 3u; }));
    EXPECT_TRUE(pumpUntil([&]() { return historyFilesIn(fsPath(QString::fromStdString(own))).empty(); }));
    ASSERT_TRUE(indexSays([&](const HistoryIndex& index) {
        return index.steps.size() == 3u && index.current == kept.ids[0] && index.saved == kept.ids[0];
    }));
}

TEST_F(ProjectHistory, UnsavedStepsAreAheadAfterReopen)
{
    const QString file = imageFile();
    const Kept kept = keepHistory(file, 2u, 0u);

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    ASSERT_TRUE(enter(tracker, home(file)).attached);

    // Redo reaches each state the person left unsaved, bit for bit.
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[1], stepStateOf(*view.image));
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[2], stepStateOf(*view.image));
    EXPECT_FALSE(tracker.hasRedoSteps());
    EXPECT_EQ(tracker.getCurrentStep(), 2u);

    // An edit made from the file's state is an edit made after an undo: the steps ahead go, and their files.
    ASSERT_TRUE(tracker.jumpToStep(0u));
    ASSERT_TRUE(settleRestore(tracker));
    ASSERT_TRUE(edit(tracker, view, 77.0f, "A branch"));
    tracker.waitAllAsyncJobsDone();
    EXPECT_EQ(tracker.getTotalStepCount(), 2u);
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    EXPECT_TRUE(pumpUntil([&]() { return stepFilesIn(homeDirectory()).size() == 2u; }));
}

TEST_F(ProjectHistory, UndoAfterReopen)
{
    const QString file = imageFile();
    // Saved at its first edit, with one more left unsaved.
    const Kept kept = keepHistory(file, 2u, 1u);

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(1u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.attached);
    EXPECT_EQ(tracker.getCurrentStep(), 1u);
    EXPECT_EQ(tracker.savedStep(), kept.ids[1].toStdString());
    EXPECT_TRUE(tracker.hasUndoSteps());
    EXPECT_TRUE(tracker.hasRedoSteps());

    // Undo reaches the state before the one the file holds; redo, twice, the one left unsaved.
    ASSERT_TRUE(tracker.undoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[0], stepStateOf(*view.image));
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[1], stepStateOf(*view.image));
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[2], stepStateOf(*view.image));
}

TEST_F(ProjectHistory, ChangedFileDetachesItsHistory)
{
    const QString file = imageFile(QByteArray(4096, 'a'));
    const Kept kept = keepHistory(file, 2u, 0u);

    // The file changes outside the application.
    (void)imageFile(QByteArray(4096, 'z'));

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, 500.0f);
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.entered) << entry.problem.toStdString();
    EXPECT_TRUE(entry.detached);
    EXPECT_FALSE(entry.attached);

    // A new history starts: the image as opened, and nothing behind or ahead of it.
    EXPECT_EQ(tracker.getTotalStepCount(), 1u);
    EXPECT_FALSE(tracker.hasUndoSteps());
    EXPECT_FALSE(tracker.hasRedoSteps());
    tracker.waitAllAsyncJobsDone();
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 1u; }));
    EXPECT_EQ(stepFilesIn(homeDirectory()).size(), 1u);

    // The earlier history is kept aside, whole: its index and every step, which open as images.
    const QDir aside(QDir(homeDirectory()).filePath(QString::fromLatin1(an::ui::history::kDetachedHistoryFolder)));
    const QStringList folders = aside.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    ASSERT_EQ(folders.size(), 1);
    const QString detached = aside.filePath(folders.front());
    const std::optional<HistoryIndex> index = readHistoryIndex(detached);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(index->steps.size(), 3u);
    for (const QString& stepFile : kept.files)
    {
        EXPECT_TRUE(QFileInfo(QDir(detached).filePath(stepFile)).isFile()) << stepFile.toStdString();
    }
    EXPECT_GT(an::ui::history::measureStoredHistory(homeDirectory()).detachedBytes, 0u);
}

TEST_F(ProjectHistory, AMissingStepFileIsLeftOutAndReported)
{
    const QString file = imageFile();
    const Kept kept = keepHistory(file, 2u, 0u);
    ASSERT_TRUE(QFile::remove(QDir(homeDirectory()).filePath(kept.files[1])));

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.attached);
    EXPECT_EQ(entry.stepsLeftOut, 1u) << "the step whose file is gone was not reported";

    // The others stand: redo reaches the step after the one that is gone.
    EXPECT_EQ(tracker.getTotalStepCount(), 2u);
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[2], stepStateOf(*view.image));
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
}

TEST_F(ProjectHistory, AStepThatDoesNotReadIsLeftOutWhenItIsFirstRead)
{
    const QString file = imageFile();
    const Kept kept = keepHistory(file, 2u, 0u);
    // The step's file is there and is no step any more.
    {
        QFile damaged(QDir(homeDirectory()).filePath(kept.files[1]));
        ASSERT_TRUE(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
        damaged.write("not a step");
    }

    FakeHistoryView view;
    view.image = makeImage(session(), 8, 6, 1, baseOf(0u));
    const StepState opened = stepStateOf(*view.image);
    ImageHistoryTracker tracker(view, session(), nullptr, temporary());
    tracker.registerInitialStep("opened");
    const HistoryEntry entry = enter(tracker, home(file));
    ASSERT_TRUE(entry.attached);
    EXPECT_EQ(entry.stepsLeftOut, 0u);
    ASSERT_EQ(tracker.getTotalStepCount(), 3u);

    // The first read of it fails: the view stays as it is, and the step leaves the history.
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(opened, stepStateOf(*view.image));
    EXPECT_EQ(tracker.getCurrentStep(), 0u);
    EXPECT_EQ(tracker.getTotalStepCount(), 2u);
    ASSERT_TRUE(tracker.redoStep());
    ASSERT_TRUE(settleRestore(tracker));
    expectSameStep(kept.states[2], stepStateOf(*view.image));
    ASSERT_TRUE(indexSays([](const HistoryIndex& index) { return index.steps.size() == 2u; }));
}

// ---- The identity of a file.

TEST(FileIdentity, NoFileHasNone)
{
    EXPECT_FALSE(an::ui::history::readFileIdentity(QString()).has_value());
    EXPECT_FALSE(an::ui::history::readFileIdentity(QStringLiteral("Z:/no/such/file.fits")).has_value());
}

TEST(FileIdentity, TheSizeTheTimeAndTheHeadsHashEachMakeTwoFilesDiffer)
{
    const an::tests::fixtures::ScopedTempPath directory =
        an::tests::fixtures::makeScopedTempDir("afternight-file-identity");
    const auto fileOf = [&directory](const char* name, const QByteArray& bytes) {
        const QString path = QDir(qtPath(directory.path())).filePath(QString::fromLatin1(name));
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        EXPECT_EQ(file.write(bytes), bytes.size());
        return path;
    };
    const QByteArray bytes(200'000, 'x');
    const QString original = fileOf("original.fits", bytes);
    const std::optional<FileIdentity> identity = an::ui::history::readFileIdentity(original);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->size, 200'000u);
    EXPECT_EQ(identity->headSha256.size(), 64);

    // A copy written at the same instant is the same file to its identity.
    const QString same = fileOf("same.fits", bytes);
    std::filesystem::last_write_time(fsPath(same), std::filesystem::last_write_time(fsPath(original)));
    EXPECT_EQ(an::ui::history::readFileIdentity(same), identity);

    // The size alone: the same head, the same time, one byte more.
    const QString longer = fileOf("longer.fits", bytes + QByteArray(1, 'x'));
    std::filesystem::last_write_time(fsPath(longer), std::filesystem::last_write_time(fsPath(original)));
    const std::optional<FileIdentity> longerIdentity = an::ui::history::readFileIdentity(longer);
    ASSERT_TRUE(longerIdentity.has_value());
    EXPECT_NE(longerIdentity->size, identity->size);
    EXPECT_EQ(longerIdentity->modifiedMs, identity->modifiedMs);
    EXPECT_NE(*longerIdentity, *identity);

    // The time alone: the same bytes, written two seconds later.
    const QString later = fileOf("later.fits", bytes);
    std::filesystem::last_write_time(fsPath(later),
                                     std::filesystem::last_write_time(fsPath(original)) + std::chrono::seconds(2));
    const std::optional<FileIdentity> laterIdentity = an::ui::history::readFileIdentity(later);
    ASSERT_TRUE(laterIdentity.has_value());
    EXPECT_EQ(laterIdentity->size, identity->size);
    EXPECT_EQ(laterIdentity->headSha256, identity->headSha256);
    EXPECT_NE(laterIdentity->modifiedMs, identity->modifiedMs);
    EXPECT_NE(*laterIdentity, *identity);

    // The head's hash alone: the same size, the same time, one byte of the head another.
    QByteArray other = bytes;
    other[100'000] = 'y';
    const QString changed = fileOf("changed.fits", other);
    std::filesystem::last_write_time(fsPath(changed), std::filesystem::last_write_time(fsPath(original)));
    const std::optional<FileIdentity> changedIdentity = an::ui::history::readFileIdentity(changed);
    ASSERT_TRUE(changedIdentity.has_value());
    EXPECT_EQ(changedIdentity->size, identity->size);
    EXPECT_EQ(changedIdentity->modifiedMs, identity->modifiedMs);
    EXPECT_NE(changedIdentity->headSha256, identity->headSha256);
    EXPECT_NE(*changedIdentity, *identity);
}

TEST(FileIdentity, AnUntouchedFileIsItselfWhenItIsReadAgain)
{
    const an::tests::fixtures::ScopedTempPath directory =
        an::tests::fixtures::makeScopedTempDir("afternight-file-identity");
    // Longer than the head the identity digests, so the digest is of the head and not of the file.
    QByteArray bytes(static_cast<qsizetype>(an::ui::history::kFileIdentityHeadBytes) + 4096, 'q');
    const QString path = QDir(qtPath(directory.path())).filePath(QStringLiteral("frame.xisf"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
    const std::optional<FileIdentity> first = an::ui::history::readFileIdentity(path);
    ASSERT_TRUE(first.has_value());

    // Read again as a later session reads it - through another name for the same file, nothing kept in between.
    const std::optional<FileIdentity> again =
        an::ui::history::readFileIdentity(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *first);

    // What the identity is as text - the index's - is the same identity when it is read back.
    HistoryIndex index;
    index.identity = first;
    const std::optional<HistoryIndex> parsed =
        an::ui::history::parseHistoryIndex(an::ui::history::formatHistoryIndex(index));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->identity, first);

    // A byte past the head, the size and the time kept: beyond what the identity tells apart.
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(fsPath(path));
    bytes[bytes.size() - 1] = 'r';
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
    std::filesystem::last_write_time(fsPath(path), written);
    EXPECT_EQ(an::ui::history::readFileIdentity(path), first);
}

} // namespace
