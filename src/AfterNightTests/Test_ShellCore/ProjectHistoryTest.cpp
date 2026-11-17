// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A project keeps the undo history of its images, over views of a real session: an image opened from a file while a
// project is open has its steps in the project's folder; an image no file holds joins at its first save, and its
// steps go in; saving says which step the file equals and follows the file to another name; opened again, the image
// shows its file with its history around it, and is told when the file changed meanwhile; closing the project leaves
// its history where it is; the history is compacted to what it says; and the project's storage is measured off the
// GUI thread.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "projects/ProjectHistoryBinder.h"
#include "projects/ProjectService.h"
#include "views/ViewService.h"

#include "app/RuntimeStorageCoordinator.h"
#include "controls/IAppContext.h"
#include "controls/ImageHistoryIndex.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::pumpUntil;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::history::FileIdentity;
using an::ui::history::HistoryCompaction;
using an::ui::history::HistoryCompactionResult;
using an::ui::history::HistoryHome;
using an::ui::history::HistoryIndex;
using an::ui::history::historyPlacementOf;
using an::ui::history::IHistoryPlacement;
using an::ui::history::readHistoryIndex;
using an::ui::history::StepsMove;
using an::ui::history::waitForClosingHistories;
using an::ui::shell::ApplicationCore;
using an::ui::shell::ProjectCreateRequest;
using an::ui::shell::ProjectHistoryBinder;
using an::ui::shell::ProjectImage;
using an::ui::shell::ProjectImageKind;
using an::ui::shell::ProjectService;
using an::ui::shell::ProjectStorageReport;

/// What the person is told when a file changed while its image was closed.
const QString kChangedOutside =
    QStringLiteral("This file changed outside AfterNight; its earlier history is kept and can be opened as images or "
                   "deleted");

/// The step files directly in a directory: the files of either kind a history's codec writes.
QStringList stepFilesIn(const QString& directory)
{
    return QDir(directory).entryList(QStringList{QStringLiteral("*.xisf"), QStringLiteral("*.ansnap")}, QDir::Files,
                                     QDir::Name);
}

class ProjectHistoryFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_widgetsBefore = an::tests::shellcore::widgetsNow();
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_files.isValid());
        m_session = an::tests::shellcore::openSession();
        ASSERT_NE(m_session, nullptr);
        m_core.openStores();
        m_core.setShell(&m_shell);
        m_core.setEngineSessionProvider(m_session.get());
        m_target = std::make_unique<an::ui::shell::ServiceEventTarget>(
            m_core, an::ui::shell::ServiceEventTarget::Dependencies{});
        m_core.setEventTarget(m_target.get());
        m_context = std::make_unique<an::ui::shell::ApplicationContext>(m_core, m_shell);
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());
        m_core.projectHistory().setNotice([this](const QString& text) { m_notices.push_back(text); });
    }

    void TearDown() override
    {
        closeViews();
        (void)m_core.projects().close();
        an::tests::shellcore::settle();
        EXPECT_TRUE(waitForClosingHistories({}, 30000));
        an::tests::shellcore::settle();
        m_core.projectHistory().setNotice({});
        m_core.discardPendingEvents();
        m_core.setEventTarget(nullptr);
        an::ui::clearAppContext();
        m_core.setEngineSessionProvider(nullptr);
        m_core.setShell(nullptr);
        m_core.closeStores();
        const QStringList left = an::tests::shellcore::widgetsLeftSince(m_widgetsBefore);
        EXPECT_TRUE(left.isEmpty()) << "the case left widgets behind: "
                                    << left.join(QStringLiteral(", ")).toStdString();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_session->engineSession();
    }

    [[nodiscard]] ProjectService& projects()
    {
        return m_core.projects();
    }

    [[nodiscard]] ProjectHistoryBinder& binder()
    {
        return m_core.projectHistory();
    }

    /// Makes and opens a project under the case's own folder.
    void openProject(const QString& name = QStringLiteral("Carina"))
    {
        ProjectCreateRequest request;
        request.name = name;
        request.location = QDir(m_files.path()).filePath(QStringLiteral("projects"));
        const an::ui::shell::ProjectOutcome made = projects().create(request);
        ASSERT_TRUE(made.ok()) << made.why.toStdString();
    }

    /// A file of the case's own that stands for an image's file on disk: its bytes are what its identity is read
    /// from.
    [[nodiscard]] QString fileOf(const QString& name, const QByteArray& bytes = QByteArray(4096, 'a')) const
    {
        const QString path = QDir::cleanPath(QDir(m_files.path()).filePath(name));
        EXPECT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        EXPECT_EQ(file.write(bytes), bytes.size());
        return path;
    }

    /// A view of an image named `name` that shows the file `file` - or no file, when it is empty - as a view the
    /// application opens does: made by the views' factory, with nothing to save, and told of to the projects' history.
    ImageViewControl* viewOf(const QString& file, const QString& name)
    {
        std::optional<an::ui::ImageDocument> document = an::tests::shellcore::documentNamed(session(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        an::ui::DocumentNames names = document->names();
        names.sourcePath = file;
        document->setNames(std::move(names));
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* view =
            factory.createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, "opened",
                                                       ViewControlMode::standard, nullptr, false, file.isEmpty());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            m_views.emplace_back(view);
            m_docks.emplace_back(view->getDockWidget());
            EXPECT_TRUE(pumpUntil([view]() { return view->isWorkIdle(); }));
            binder().watch(*view);
        }
        return view;
    }

    /// Closes a view as the application does, and waits for what its history finishes behind it.
    void closeView(ImageViewControl* view)
    {
        const QPointer<ImageViewControl> closing(view);
        const QPointer<QWidget> dock(view != nullptr ? view->getDockWidget() : nullptr);
        m_core.viewService().manager().unregisterImageViewControl(view);
        delete dock.data();
        delete closing.data();
        an::tests::shellcore::settle();
        EXPECT_TRUE(waitForClosingHistories({}, 30000));
        an::tests::shellcore::settle();
    }

    void closeViews()
    {
        m_core.viewService().manager().clear();
        for (const QPointer<QWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        for (const QPointer<ImageViewControl>& view : m_views)
        {
            delete view.data();
        }
        m_docks.clear();
        m_views.clear();
    }

    /// Edits a view's image into another, recorded in its history as `name`; waits until it is shown.
    void edit(ImageViewControl& view, const std::string& name)
    {
        std::optional<an::ui::ImageDocument> next = an::tests::shellcore::documentNamed(session(), QStringLiteral("x"));
        ASSERT_TRUE(next.has_value());
        an::Result<an::consumer::ImageAsset> copy = next->asset()->snapshot();
        ASSERT_TRUE(copy.hasValue()) << copy.status().message();
        auto held = std::make_shared<an::consumer::ImageAsset>(std::move(*copy));
        std::optional<bool> shown;
        view.editImage(
            name, "Edited by the case",
            [held](const an::consumer::EngineSession&, const an::consumer::AssetWriteLease&, std::stop_token) {
                return an::Result<an::consumer::ImageAsset>(std::move(*held));
            },
            [&shown](const bool done) { shown = done; });
        ASSERT_TRUE(pumpUntil([&shown, &view]() { return shown.has_value() && view.isWorkIdle(); }));
        ASSERT_TRUE(*shown) << "the edit " << name << " was not shown";
    }

    /// What a save does to a view once its file is written: the view takes the file's path and is told it is saved.
    void saveAs(ImageViewControl& view, const QString& file, const QByteArray& bytes)
    {
        (void)fileOf(QDir(m_files.path()).relativeFilePath(file), bytes);
        an::ui::DocumentNames names = view.document().names();
        names.sourcePath = file;
        view.document().setNames(std::move(names));
        view.notifyImageSaved();
    }

    /// The home of a view's history; nothing outside one.
    [[nodiscard]] static std::optional<HistoryHome> homeOf(ImageViewControl& view)
    {
        IHistoryPlacement* const placement = historyPlacementOf(view);
        return placement != nullptr ? placement->home() : std::nullopt;
    }

    /// Turns the event loop until a view's history is kept in a home.
    [[nodiscard]] static bool entered(ImageViewControl& view)
    {
        return pumpUntil([&view]() { return homeOf(view).has_value(); });
    }

    /// Turns the event loop until the index of a directory says what `said` asks of it.
    [[nodiscard]] static bool indexSays(const QString& directory, const std::function<bool(const HistoryIndex&)>& said)
    {
        return pumpUntil([&directory, &said]() {
            const std::optional<HistoryIndex> index = readHistoryIndex(directory);
            return index.has_value() && said(*index);
        });
    }

    /// The id of the step a view shows, as its history's index names it.
    [[nodiscard]] static QString currentStepOf(const QString& directory)
    {
        const std::optional<HistoryIndex> index = readHistoryIndex(directory);
        return index.has_value() ? index->current : QString();
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    QTemporaryDir m_files;
    std::unique_ptr<SessionProvider> m_session;
    ApplicationCore m_core;
    RecordingShell m_shell;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
    QStringList m_notices;
    QSet<QWidget*> m_widgetsBefore;
};

class ProjectHistory : public ProjectHistoryFixture
{
};

} // namespace

// An image opened from a file while a project is open is the project's, and its steps are files of the project's
// folder for it, listed by the index beside them; a view no file holds, and one a temporary file holds, are not.
TEST_F(ProjectHistory, AnImageOpenedInAProjectKeepsItsHistoryInTheProjectsFolder)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* const view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));

    const ProjectImage* const image = projects().imageFor(file);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->kind, ProjectImageKind::Image);
    const QString directory = projects().historyDirectoryFor(image->id);
    EXPECT_EQ(binder().imageOf(*view), image->id);
    const std::optional<HistoryHome> home = homeOf(*view);
    ASSERT_TRUE(home.has_value());
    EXPECT_EQ(QDir::cleanPath(home->directory), QDir::cleanPath(directory));
    EXPECT_EQ(home->compression, projects().current()->historyCompression);
    EXPECT_EQ(home->file, file);

    // The state it was opened with is the one the file holds, and the index says so with the file's identity.
    const std::optional<FileIdentity> identity = an::ui::history::readFileIdentity(file);
    ASSERT_TRUE(identity.has_value());
    ASSERT_TRUE(indexSays(directory, [&identity](const HistoryIndex& index) {
        return index.steps.size() == 1u && index.saved == index.steps.front().id && index.identity == identity;
    }));
    edit(*view, "Curves");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) {
        return index.steps.size() == 2u && index.current == index.steps[1].id && index.saved == index.steps[0].id;
    }));
    EXPECT_EQ(stepFilesIn(directory).size(), 2);

    // A view no file holds is no project image, and neither is one a temporary file holds.
    ImageViewControl* const made = viewOf(QString(), QStringLiteral("made by a process"));
    ASSERT_NE(made, nullptr);
    const QString temporary = QDir(m_core.runtimeStorage().getTempDirPath()).filePath(QStringLiteral("scratch.fits"));
    ImageViewControl* const scratch = viewOf(temporary, QStringLiteral("scratch"));
    ASSERT_NE(scratch, nullptr);
    an::tests::shellcore::settle();
    EXPECT_FALSE(homeOf(*made).has_value());
    EXPECT_FALSE(homeOf(*scratch).has_value());
    EXPECT_EQ(projects().current()->images.size(), 1u);
    EXPECT_TRUE(m_notices.isEmpty()) << m_notices.join(QLatin1Char('|')).toStdString();
}

// With no project open nothing joins anything: the history is the session's, as it always was.
TEST_F(ProjectHistory, WithoutAProjectTheHistoryIsTheSessions)
{
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* const view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    edit(*view, "Curves");
    an::tests::shellcore::settle();
    EXPECT_FALSE(homeOf(*view).has_value());
    EXPECT_TRUE(binder().imageOf(*view).isEmpty());
    EXPECT_FALSE(QFileInfo::exists(QDir(m_files.path()).filePath(QStringLiteral("projects"))));

    // A project opened later does not take a view that was open before it.
    openProject();
    an::tests::shellcore::settle();
    EXPECT_FALSE(homeOf(*view).has_value());
    EXPECT_TRUE(projects().current()->images.empty());
}

// An image no file holds keeps its history as outside a project until its first save, which adds it to the project
// and moves its steps in.
TEST_F(ProjectHistory, AnImageWithNoFileJoinsAtItsFirstSave)
{
    openProject();
    ImageViewControl* const view = viewOf(QString(), QStringLiteral("stacked"));
    ASSERT_NE(view, nullptr);
    edit(*view, "Stretch");
    an::tests::shellcore::settle();
    ASSERT_FALSE(homeOf(*view).has_value());
    ASSERT_TRUE(projects().current()->images.empty());
    IHistoryPlacement* const placement = historyPlacementOf(*view);
    ASSERT_NE(placement, nullptr);
    ASSERT_TRUE(pumpUntil([placement]() { return placement->stepFileBytes() > 0u; }));

    // Saved for the first time: the file is the project's image, and both steps are in its folder.
    const QString file = QDir::cleanPath(QDir(m_files.path()).filePath(QStringLiteral("sky/stacked.fits")));
    saveAs(*view, file, QByteArray(2048, 's'));
    ASSERT_TRUE(entered(*view));
    const ProjectImage* const image = projects().imageFor(file);
    ASSERT_NE(image, nullptr);
    const QString directory = projects().historyDirectoryFor(image->id);
    const std::optional<FileIdentity> identity = an::ui::history::readFileIdentity(file);
    ASSERT_TRUE(indexSays(directory, [&identity](const HistoryIndex& index) {
        // The step it shows is the one the file was written from.
        return index.steps.size() == 2u && index.saved == index.steps[1].id && index.current == index.saved &&
               index.identity == identity;
    }));
    EXPECT_EQ(stepFilesIn(directory).size(), 2);
    EXPECT_EQ(readHistoryIndex(directory)->file, file);
    // Undo still reaches the state before the save, from the project's folder.
    ASSERT_TRUE(view->viewHistory()->undo());
    ASSERT_TRUE(pumpUntil([view]() { return !view->viewHistory()->hasUndo() && view->isWorkIdle(); }));
}

// Saving the image over its own file makes the step shown the saved one and records the file's new identity; saving it
// under another name takes the project's record of it, and its history, to the new file.
TEST_F(ProjectHistory, SavingMovesTheSavedStepAndRecordsTheIdentity)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"), QByteArray(4096, 'a'));
    const QString other = fileOf(QStringLiteral("sky/M31.fits"), QByteArray(4096, 'm'));
    ImageViewControl* const view = viewOf(file, QStringLiteral("M42"));
    ImageViewControl* const neighbour = viewOf(other, QStringLiteral("M31"));
    ASSERT_NE(view, nullptr);
    ASSERT_NE(neighbour, nullptr);
    ASSERT_TRUE(entered(*view));
    ASSERT_TRUE(entered(*neighbour));
    const QString id = binder().imageOf(*view);
    const QString directory = projects().historyDirectoryFor(id);
    ASSERT_EQ(projects().current()->images.size(), 2u);

    edit(*view, "Curves");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) {
        return index.steps.size() == 2u && index.saved == index.steps[0].id;
    }));

    // Saved over its own file.
    saveAs(*view, file, QByteArray(8192, 'b'));
    const std::optional<FileIdentity> saved = an::ui::history::readFileIdentity(file);
    ASSERT_TRUE(indexSays(directory, [&saved](const HistoryIndex& index) {
        return index.saved == index.steps[1].id && index.identity == saved;
    }));
    EXPECT_EQ(projects().current()->images.size(), 2u);

    // Saved under another name: the view takes the new path, so the project's record follows it and the history
    // stays with the image.
    const QString renamed = QDir::cleanPath(QDir(m_files.path()).filePath(QStringLiteral("sky/M42-final.fits")));
    saveAs(*view, renamed, QByteArray(1024, 'c'));
    const std::optional<FileIdentity> moved = an::ui::history::readFileIdentity(renamed);
    ASSERT_TRUE(indexSays(directory,
                          [&](const HistoryIndex& index) { return index.file == renamed && index.identity == moved; }));
    EXPECT_EQ(binder().imageOf(*view), id);
    ASSERT_NE(projects().image(id), nullptr);
    EXPECT_EQ(projects().image(id)->path, renamed);
    EXPECT_EQ(projects().imageFor(file), nullptr) << "the project still holds the name the image left";
    EXPECT_EQ(QDir::cleanPath(homeOf(*view)->directory), QDir::cleanPath(directory));

    // Saved over another image of the project: that image's record gives way to this one.
    saveAs(*view, other, QByteArray(512, 'd'));
    ASSERT_TRUE(pumpUntil([&]() { return projects().image(id) != nullptr && projects().image(id)->path == other; }));
    EXPECT_EQ(projects().current()->images.size(), 1u);
    EXPECT_EQ(projects().imageFor(other)->id, id);
}

// A project image opened again shows its file: the kept history is around it, the saved step the one shown, the
// earlier steps behind it and the ones left unsaved ahead, and the view is not marked as changed.
TEST_F(ProjectHistory, ReopenShowsTheFileWithTheSavedStepCurrent)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    const QString directory = projects().historyDirectoryFor(binder().imageOf(*view));
    edit(*view, "Curves");
    edit(*view, "Stretch");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 3u; }));
    const std::optional<HistoryIndex> kept = readHistoryIndex(directory);
    ASSERT_TRUE(kept.has_value());

    // Closed without saving, as closing asks: the steps stay in the project.
    closeView(view);
    ASSERT_EQ(stepFilesIn(directory).size(), 3);

    view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    ASSERT_TRUE(pumpUntil([view]() { return view->viewHistory()->hasRedo(); }));
    EXPECT_FALSE(view->hasPendingChanges()) << "an image opened again was marked as changed";
    EXPECT_FALSE(view->viewHistory()->hasUndo());
    EXPECT_EQ(view->viewHistory()->nextStepName(), "Curves");
    const an::ui::ViewHistorySteps steps = view->viewHistory()->steps();
    EXPECT_EQ(steps.steps.size(), 3u);
    EXPECT_EQ(steps.current, 0u);
    EXPECT_EQ(stepFilesIn(directory).size(), 3);
    ASSERT_TRUE(indexSays(directory, [&kept](const HistoryIndex& index) {
        return index.steps.size() == 3u && index.current == kept->steps[0].id && index.saved == kept->steps[0].id;
    }));
    EXPECT_TRUE(m_notices.isEmpty()) << m_notices.join(QLatin1Char('|')).toStdString();

    // Redo reaches what was left unsaved.
    ASSERT_TRUE(view->viewHistory()->redo());
    ASSERT_TRUE(pumpUntil([view]() { return view->viewHistory()->hasUndo() && view->isWorkIdle(); }));
    EXPECT_EQ(view->viewHistory()->previousStepName(), "Curves");
    EXPECT_EQ(view->viewHistory()->nextStepName(), "Stretch");
}

// When the file changed outside the application the kept history is set aside, the person is told once, and a new
// history starts.
TEST_F(ProjectHistory, ChangedFileDetachesItsHistory)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"), QByteArray(4096, 'a'));
    ImageViewControl* view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    const QString directory = projects().historyDirectoryFor(binder().imageOf(*view));
    edit(*view, "Curves");
    ASSERT_TRUE(indexSays(
        directory, [](const HistoryIndex& index) { return index.steps.size() == 2u && index.identity.has_value(); }));
    closeView(view);

    (void)fileOf(QStringLiteral("sky/M42.fits"), QByteArray(4096, 'z'));
    view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    ASSERT_TRUE(pumpUntil([this]() { return !m_notices.isEmpty(); }));
    ASSERT_EQ(m_notices.size(), 1);
    EXPECT_TRUE(m_notices.front().contains(kChangedOutside)) << m_notices.front().toStdString();
    EXPECT_TRUE(m_notices.front().contains(QStringLiteral("M42.fits")));
    EXPECT_FALSE(view->viewHistory()->hasUndo());
    EXPECT_FALSE(view->viewHistory()->hasRedo());
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 1u; }));
    const QDir aside(QDir(directory).filePath(QString::fromLatin1(an::ui::history::kDetachedHistoryFolder)));
    ASSERT_EQ(aside.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
}

// A step of the kept history whose file is gone is left out and said once; the others stand.
TEST_F(ProjectHistory, AMissingStepFileIsLeftOutAndReported)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    const QString directory = projects().historyDirectoryFor(binder().imageOf(*view));
    edit(*view, "Curves");
    edit(*view, "Stretch");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 3u; }));
    const QString gone = readHistoryIndex(directory)->steps[1].file;
    closeView(view);
    ASSERT_TRUE(QFile::remove(QDir(directory).filePath(gone)));

    view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    ASSERT_TRUE(pumpUntil([this]() { return !m_notices.isEmpty(); }));
    ASSERT_EQ(m_notices.size(), 1);
    EXPECT_TRUE(m_notices.front().contains(QStringLiteral("M42.fits"))) << m_notices.front().toStdString();
    EXPECT_TRUE(m_notices.front().contains(QStringLiteral("1 step"))) << m_notices.front().toStdString();
    EXPECT_EQ(view->viewHistory()->steps().steps.size(), 2u);
    EXPECT_EQ(view->viewHistory()->nextStepName(), "Stretch");
}

// Closing the project leaves the history it kept in its folder; the view goes on with a history of the session's.
TEST_F(ProjectHistory, ClosingTheProjectLeavesItsHistoryInItsFolder)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* const view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    const QString directory = projects().historyDirectoryFor(binder().imageOf(*view));
    edit(*view, "Curves");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 2u; }));

    ASSERT_TRUE(projects().close());
    EXPECT_FALSE(homeOf(*view).has_value());
    EXPECT_TRUE(binder().imageOf(*view).isEmpty());
    edit(*view, "Stretch");
    an::tests::shellcore::settle();
    ASSERT_TRUE(waitForClosingHistories(directory, 30000));
    EXPECT_EQ(stepFilesIn(directory).size(), 2) << "a step recorded after the project closed landed in it";
    EXPECT_EQ(readHistoryIndex(directory)->steps.size(), 2u);
    // The steps the project keeps are still what undo restores.
    ASSERT_TRUE(view->viewHistory()->undo());
    ASSERT_TRUE(pumpUntil([view]() { return view->viewHistory()->steps().current == 1u && view->isWorkIdle(); }));
}

// The open images join a new project with their history when it is moved, and without the steps they had when it is
// not: the size the move is offered with is what the steps' files take.
TEST_F(ProjectHistory, MoveInKeepsEveryStep)
{
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    const QString other = fileOf(QStringLiteral("sky/M31.fits"));
    ImageViewControl* const moved = viewOf(file, QStringLiteral("M42"));
    ImageViewControl* const left = viewOf(other, QStringLiteral("M31"));
    ASSERT_NE(moved, nullptr);
    ASSERT_NE(left, nullptr);
    edit(*moved, "Curves");
    edit(*left, "Curves");
    IHistoryPlacement* const placement = historyPlacementOf(*moved);
    ASSERT_NE(placement, nullptr);
    ASSERT_TRUE(pumpUntil([&]() {
        return placement->stepFileBytes() > 0u && historyPlacementOf(*left)->stepFileBytes() > 0u &&
               session().memory().counters(an::consumer::MemoryCategory::PendingSnapshots).entries == 0u;
    }));
    const std::uint64_t offered = ProjectHistoryBinder::historyBytesOf({moved, left});
    EXPECT_EQ(offered, placement->stepFileBytes() + historyPlacementOf(*left)->stepFileBytes());

    openProject();
    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;
    const QString movedId =
        binder().join(*moved, StepsMove::Move, [&progress](const std::uint64_t done, const std::uint64_t total) {
            progress.emplace_back(done, total);
        });
    const QString leftId = binder().join(*left, StepsMove::Leave);
    ASSERT_FALSE(movedId.isEmpty());
    ASSERT_FALSE(leftId.isEmpty());
    ASSERT_TRUE(entered(*moved));
    ASSERT_TRUE(entered(*left));

    // Moved: both steps are the project's, and what moved is what was offered for this image.
    const QString movedDirectory = projects().historyDirectoryFor(movedId);
    ASSERT_TRUE(indexSays(movedDirectory, [](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    EXPECT_EQ(stepFilesIn(movedDirectory).size(), 2);
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back().first, placement->stepFileBytes());
    EXPECT_EQ(progress.back().first, progress.back().second);

    // Left: the steps it had stay with the session, and the ones recorded from now on are the project's.
    const QString leftDirectory = projects().historyDirectoryFor(leftId);
    EXPECT_TRUE(stepFilesIn(leftDirectory).isEmpty());
    edit(*left, "Stretch");
    ASSERT_TRUE(indexSays(leftDirectory, [](const HistoryIndex& index) { return index.steps.size() == 1u; }));
    EXPECT_EQ(readHistoryIndex(leftDirectory)->steps.front().name, QStringLiteral("Stretch"));
}

// A history is compacted to what it says it frees, whether a view has it open or not, and what it keeps is the state
// the image was left at.
TEST_F(ProjectHistory, CompactKeepsTheCurrentStateAndFreesWhatItSays)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* view = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(entered(*view));
    const QString id = binder().imageOf(*view);
    const QString directory = projects().historyDirectoryFor(id);
    for (const char* name : {"First", "Second", "Third"})
    {
        edit(*view, name);
    }
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 4u; }));
    const auto stepBytes = [&directory]() {
        std::uint64_t bytes = 0u;
        for (const QString& name : stepFilesIn(directory))
        {
            bytes += static_cast<std::uint64_t>(QFileInfo(QDir(directory).filePath(name)).size());
        }
        return bytes;
    };

    // With the image open: to its last two steps.
    HistoryCompaction lastTwo;
    lastTwo.keep = HistoryCompaction::Keep::LastSteps;
    lastTwo.steps = 2u;
    std::optional<HistoryCompactionResult> planned;
    std::optional<HistoryCompactionResult> done;
    const std::uint64_t before = stepBytes();
    binder().planCompaction(id, lastTwo, [&planned](const HistoryCompactionResult& result) { planned = result; });
    ASSERT_TRUE(pumpUntil([&planned]() { return planned.has_value(); }));
    ASSERT_TRUE(planned->ok);
    EXPECT_EQ(planned->stepsRemoved, 2u);
    EXPECT_EQ(stepBytes(), before);
    binder().compact(id, lastTwo, [&done](const HistoryCompactionResult& result) { done = result; });
    ASSERT_TRUE(pumpUntil([&done]() { return done.has_value(); }));
    EXPECT_EQ(*done, *planned);
    ASSERT_TRUE(pumpUntil([&]() { return stepFilesIn(directory).size() == 2; }));
    EXPECT_EQ(before - stepBytes(), planned->bytesFreed);
    const an::ui::ViewHistorySteps left = view->viewHistory()->steps();
    ASSERT_EQ(left.steps.size(), 2u);
    EXPECT_EQ(left.steps[0].name, "Second");
    EXPECT_EQ(left.steps[1].name, "Third");
    EXPECT_EQ(left.current, 1u) << "the state the view shows is not the one the history kept";
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 2u; }));

    // With the image closed: to its current state alone, measured and removed off the GUI thread.
    closeView(view);
    HistoryCompaction current;
    current.keep = HistoryCompaction::Keep::CurrentState;
    planned.reset();
    done.reset();
    const std::uint64_t closedBefore = stepBytes();
    binder().planCompaction(id, current, [&planned](const HistoryCompactionResult& result) { planned = result; });
    EXPECT_FALSE(planned.has_value()) << "a closed image's history was measured on the thread that asked";
    ASSERT_TRUE(pumpUntil([&planned]() { return planned.has_value(); }));
    ASSERT_TRUE(planned->ok);
    EXPECT_EQ(planned->stepsRemoved, 1u);
    binder().compact(id, current, [&done](const HistoryCompactionResult& result) { done = result; });
    ASSERT_TRUE(pumpUntil([&done]() { return done.has_value(); }));
    EXPECT_EQ(*done, *planned);
    EXPECT_EQ(closedBefore - stepBytes(), planned->bytesFreed);
    const std::optional<HistoryIndex> index = readHistoryIndex(directory);
    ASSERT_TRUE(index.has_value());
    ASSERT_EQ(index->steps.size(), 1u);
    EXPECT_EQ(index->steps.front().name, QStringLiteral("Third"));

    // An image the project does not hold has no history to compact.
    done.reset();
    binder().compact(QStringLiteral("no-such-image"), current,
                     [&done](const HistoryCompactionResult& result) { done = result; });
    ASSERT_TRUE(pumpUntil([&done]() { return done.has_value(); }));
    EXPECT_FALSE(done->ok);
}

// What the project takes on disk is measured on a worker and told on the GUI thread: each image's history with its
// steps, the histories set aside, and the rest of the folder.
TEST_F(ProjectHistory, TheStorageReportStatesEachImagesHistory)
{
    // With no project open there is nothing to report.
    std::optional<ProjectStorageReport> report;
    projects().storageReport([&report](const ProjectStorageReport& measured) { report = measured; });
    ASSERT_TRUE(pumpUntil([&report]() { return report.has_value(); }));
    EXPECT_TRUE(report->folder.isEmpty());
    EXPECT_TRUE(report->images.empty());

    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    const QString other = fileOf(QStringLiteral("sky/M31.fits"));
    ImageViewControl* const view = viewOf(file, QStringLiteral("M42"));
    ImageViewControl* const second = viewOf(other, QStringLiteral("M31"));
    ASSERT_NE(view, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(entered(*view));
    ASSERT_TRUE(entered(*second));
    const QString id = binder().imageOf(*view);
    const QString directory = projects().historyDirectoryFor(id);
    edit(*view, "Curves");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    ASSERT_TRUE(indexSays(projects().historyDirectoryFor(binder().imageOf(*second)),
                          [](const HistoryIndex& index) { return index.steps.size() == 1u; }));
    // Something of the project that is no history, and a history set aside.
    {
        QFile notes(QDir(projects().currentFolder()).filePath(QStringLiteral("notes.md")));
        ASSERT_TRUE(notes.open(QIODevice::WriteOnly));
        notes.write(QByteArray(1000, 'n'));
    }
    const QString aside = QDir(directory).filePath(QStringLiteral("detached/20261101-000000-000"));
    ASSERT_TRUE(QDir().mkpath(aside));
    {
        QFile old(QDir(aside).filePath(QStringLiteral("old.xisf")));
        ASSERT_TRUE(old.open(QIODevice::WriteOnly));
        old.write(QByteArray(500, 'o'));
    }

    report.reset();
    const std::thread::id asking = std::this_thread::get_id();
    std::thread::id told;
    projects().storageReport([&report, &told](const ProjectStorageReport& measured) {
        told = std::this_thread::get_id();
        report = measured;
    });
    EXPECT_FALSE(report.has_value()) << "the storage was measured on the thread that asked";
    ASSERT_TRUE(pumpUntil([&report]() { return report.has_value(); }));
    EXPECT_EQ(told, asking) << "the report was told off the GUI thread";
    EXPECT_EQ(QDir::cleanPath(report->folder), QDir::cleanPath(projects().currentFolder()));
    ASSERT_EQ(report->images.size(), 2u);
    const auto measured = std::find_if(report->images.begin(), report->images.end(),
                                       [&id](const auto& image) { return image.imageId == id; });
    ASSERT_NE(measured, report->images.end());
    EXPECT_EQ(measured->path, file);
    EXPECT_EQ(measured->steps, 2u);
    EXPECT_EQ(measured->detachedBytes, 500u);
    const an::ui::history::StoredHistorySize size = an::ui::history::measureStoredHistory(directory);
    EXPECT_EQ(measured->historyBytes, size.bytes);
    EXPECT_GT(measured->historyBytes, 0u);
    std::uint64_t histories = 0u;
    for (const auto& image : report->images)
    {
        histories += image.historyBytes + image.detachedBytes;
    }
    EXPECT_EQ(report->historyBytes, histories);
    EXPECT_GE(report->otherBytes, 1000u);
    EXPECT_EQ(report->integrationBytes, 0u);
    EXPECT_GT(report->freeBytes, 0);
}

// Two views of one file are one image of the project, and its history is kept for the view that was there first: the
// other's stays the session's, and nothing of the first is touched.
TEST_F(ProjectHistory, TwoViewsOfOneFileDoNotShareAHistory)
{
    openProject();
    const QString file = fileOf(QStringLiteral("sky/M42.fits"));
    ImageViewControl* const first = viewOf(file, QStringLiteral("M42"));
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(entered(*first));
    const QString id = binder().imageOf(*first);
    const QString directory = projects().historyDirectoryFor(id);
    edit(*first, "Curves");
    ASSERT_TRUE(indexSays(directory, [](const HistoryIndex& index) { return index.steps.size() == 2u; }));
    const QStringList kept = stepFilesIn(directory);

    ImageViewControl* const second = viewOf(file, QStringLiteral("M42 again"));
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(pumpUntil([&]() { return binder().imageOf(*second) == id; }));
    edit(*second, "Its own");
    an::tests::shellcore::settle();
    EXPECT_FALSE(homeOf(*second).has_value());
    EXPECT_EQ(projects().current()->images.size(), 1u);
    EXPECT_EQ(stepFilesIn(directory), kept);
    EXPECT_EQ(readHistoryIndex(directory)->steps.size(), 2u);
    EXPECT_TRUE(m_notices.isEmpty()) << m_notices.join(QLatin1Char('|')).toStdString();
    ASSERT_TRUE(first->viewHistory()->undo());
    ASSERT_TRUE(pumpUntil([first]() { return !first->viewHistory()->hasUndo() && first->isWorkIdle(); }));
}
