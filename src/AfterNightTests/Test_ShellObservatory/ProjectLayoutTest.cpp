// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What a project keeps of the Observatory window: closed and opened again, it gives back its workspaces with their
// names and locks, each image in its group, the tabs in their order, the splitters where they were, a floating image
// where it was and the workspace used last; an image whose file is gone is said once, with Locate... and Remove from
// the project, and the others open; the images open on workers and the window waits for none of it; nothing is
// written with no project; a task is never kept; a project closes with its images, and one the person keeps open
// keeps it; an image that is none of the project's stays; and what the application is left with at exit is what the
// next start restores.

#include "ProjectCase.h"

#include "ShellActions.h"
#include "pages/WorkspacePage.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"
#include "workspaces/WorkspaceHost.h"

#include "Test_ShellClassic/AppTestSupport.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/PathPickerWidget.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "dialogs/NewProjectDialog.h"
#include "projects/ProjectDocument.h"
#include "projects/ProjectLayoutStore.h"
#include "projects/ProjectService.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"
#include "views/WorkspaceModel.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Group.h>

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::observatory::ProjectFlows;
using an::ui::shell::ProjectDocument;
using an::ui::shell::ProjectLayoutStore;
using an::ui::shell::ProjectService;
using an::ui::shell::ProjectWorkspace;
using Window = an::ui::observatory::ObservatoryWindow;
namespace ids = an::ui::shell::ids;
namespace shellIds = an::ui::observatory::ids;

/// How far a splitter may be from where it was, in pixels.
constexpr int kSplitterTolerance = 2;

/// A process launched on an image, a page over a view of it.
constexpr std::string_view kTargetProcess = "denoising.actionDeepSNR";

/// Where a view's dock widget is in the dock area of its workspace; a null place for one in none.
[[nodiscard]] QRect placeOf(ImageViewControl& view)
{
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    KDDockWidgets::QtWidgets::MainWindow* const area = an::ui::shell::DockHooks::mainWindowHolding(dock);
    if (dock == nullptr || area == nullptr || dock->group() == nullptr)
    {
        return {};
    }
    // The group's place: what the splitters bound, whichever of its tabs is shown.
    return dock->group()->geometry();
}

[[nodiscard]] qint64 areaOf(const QRect& place)
{
    return static_cast<qint64>(place.width()) * place.height();
}

[[nodiscard]] bool near(const QRect& left, const QRect& right)
{
    return std::abs(left.x() - right.x()) <= kSplitterTolerance &&
           std::abs(left.y() - right.y()) <= kSplitterTolerance &&
           std::abs(left.width() - right.width()) <= kSplitterTolerance &&
           std::abs(left.height() - right.height()) <= kSplitterTolerance;
}

/// The files anywhere under a folder that are named as a project's layout is.
[[nodiscard]] QStringList layoutFilesUnder(const QString& folder)
{
    QStringList found;
    QDirIterator files(folder, QStringList{QString::fromLatin1(an::ui::shell::kProjectLayoutFileName)}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        found.append(files.next());
    }
    return found;
}

/// The cases of a project's arrangement: each ends with no project, one workspace, free, and no view.
class ProjectLayout : public ProjectCase
{
protected:
    void SetUp() override
    {
        ProjectCase::SetUp();
        forgetProjects();
    }

    void TearDown() override
    {
        flows().setFolderChooser({});
        flows().setFileChooser({});
        if (QMessageBox* const said = flows().missingImagesMessage())
        {
            said->close();
        }
        waitForTheRestore();
        (void)projects().close();
        forgetProjects();
        for (const QString& id : host().ids())
        {
            core().dockLayout().unlock(host().target(id));
            (void)host().model().setLocked(id, false);
        }
        ProjectCase::TearDown();
        settleProjects();
        EXPECT_FALSE(projects().isOpen());
        EXPECT_EQ(host().model().count(), 1u) << "a case left more than one workspace";
    }

    [[nodiscard]] static an::ui::observatory::WorkspaceHost& host()
    {
        return window().workspaces();
    }

    [[nodiscard]] static ProjectLayoutStore& layout()
    {
        return window().projectLayout();
    }

    [[nodiscard]] static QAction* shellAction(const std::string_view id)
    {
        return window().shellActions().action(id);
    }

    /// Makes a project under the case's root, and opens it: the window's workspaces stay as they are.
    static QString makeProject(const QString& name)
    {
        an::ui::shell::ProjectCreateRequest request;
        request.name = name;
        request.location = root();
        const an::ui::shell::ProjectOutcome made = projects().create(request);
        EXPECT_TRUE(made.ok()) << made.why.toStdString();
        return made.folder;
    }

    /// Writes an image under a folder of the case and opens it in the open project, which it joins.
    static ImageViewControl* openInProject(const QString& name, const float value = 0.4f)
    {
        const QString path = writeImage(name, value);
        EXPECT_FALSE(path.isEmpty());
        const std::size_t before = projects().current() != nullptr ? projects().current()->images.size() : 0u;
        openFile(path);
        EXPECT_TRUE(an::tests::app::processEventsUntil(
            [&]() { return projects().imageFor(path) != nullptr && projects().current()->images.size() > before; },
            5000))
            << name.toStdString() << " did not join the project";
        ImageViewControl* const view = viewOf(path);
        EXPECT_NE(view, nullptr);
        // The project's record of the image is the view's once the history has its place.
        EXPECT_TRUE(an::tests::app::processEventsUntil(
            [&]() { return view != nullptr && !core().projectHistory().imageOf(*view).isEmpty(); }, 5000));
        return view;
    }

    /// Closes the open project as Close Project does.
    static void closeProject()
    {
        ASSERT_TRUE(shellAction(shellIds::kCloseProject)->isEnabled());
        shellAction(shellIds::kCloseProject)->trigger();
        settleProjects();
    }

    /// Opens a project's folder as the switcher's menu does, and waits until its arrangement is back.
    static void openProject(const QString& folder)
    {
        flows().openProjectFolder(folder);
        waitForTheRestore();
    }

    static void waitForTheRestore()
    {
        EXPECT_TRUE(an::tests::app::processEventsUntil([]() { return !layout().isRestoring(); }, kProjectOpenMs))
            << "the project's arrangement was not restored";
        settleProjects();
    }

    /// Locks a workspace's layout as its menu's Lock Layout does.
    static void lockByTheMenu(const QString& workspaceId)
    {
        QMenu menu;
        window().buildWorkspaceMenu(menu, workspaceId);
        QAction* lock = nullptr;
        for (QAction* entry : menu.actions())
        {
            if (entry->objectName() == QLatin1String("workspaceMenuLock"))
            {
                lock = entry;
            }
        }
        ASSERT_NE(lock, nullptr);
        lock->trigger();
        settleProjects();
    }

    [[nodiscard]] static std::vector<ImageViewControl*> dockedIn(const QString& id)
    {
        return host().dockedViews(id, core().viewService().getListOfViewControls());
    }

    [[nodiscard]] static KDDockWidgets::Core::Group* groupOf(ImageViewControl* view)
    {
        return view != nullptr && view->getDockWidget() != nullptr ? view->getDockWidget()->group() : nullptr;
    }

    /// What the project's document says, read from its file.
    [[nodiscard]] static ProjectDocument documentOf(const QString& folder)
    {
        const an::ui::shell::ProjectDocumentRead read =
            an::ui::shell::readProjectDocument(QDir(folder).filePath(QStringLiteral("project.anproj")));
        EXPECT_TRUE(read.isRead()) << read.why.toStdString();
        return read.isRead() ? *read.document : ProjectDocument();
    }
};

// Two workspaces - one split two by two with a splitter moved, locked; one of two tabs, renamed, the one used last -
// and a floating image. The project closed: its images close with it, the document lists each workspace with its
// name, its lock and its images, and the layout is a file of the project. Opened again: every image is back in its
// group, the tabs in their order with the same one shown, the splitters within two pixels, the floating image where
// it was, the lock, the names and the current workspace.
TEST_F(ProjectLayout, RoundTrip)
{
    const QString folder = makeProject(QStringLiteral("Round trip"));
    ASSERT_TRUE(projects().isOpen());
    const QString first = host().ids().front();

    // The first workspace: four images, split two by two, one splitter moved.
    std::vector<ImageViewControl*> grid;
    for (const char* name : {"rt-a.fits", "rt-b.fits", "rt-c.fits", "rt-d.fits"})
    {
        grid.push_back(openInProject(QString::fromLatin1(name)));
        ASSERT_NE(grid.back(), nullptr);
    }
    // The second workspace: two images as tabs, the first of them shown; renamed. It is made once its images are
    // open: an empty workspace the person is not in closes.
    ImageViewControl* const tabOne = openInProject(QStringLiteral("rt-e.fits"));
    ImageViewControl* const tabTwo = openInProject(QStringLiteral("rt-f.fits"));
    ASSERT_NE(tabOne, nullptr);
    ASSERT_NE(tabTwo, nullptr);
    const QString second = host().create();
    ASSERT_FALSE(second.isEmpty());
    ASSERT_TRUE(host().model().rename(second, QStringLiteral("Colour")));
    host().mainWindow(second)->addDockWidget(tabOne->getDockWidget(), KDDockWidgets::Location_OnLeft);
    tabOne->getDockWidget()->addDockWidgetAsTab(tabTwo->getDockWidget());
    tabOne->getDockWidget()->setAsCurrentTab();
    settleProjects();
    ASSERT_EQ(groupOf(tabOne), groupOf(tabTwo));

    // An image floating in a window of its own.
    ImageViewControl* const floating = openInProject(QStringLiteral("rt-g.fits"));
    ASSERT_NE(floating, nullptr);
    floating->getDockWidget()->setFloating(true);
    settleProjects();
    ASSERT_TRUE(floating->isInFloatingWindow());
    const QRect asked(180, 140, 420, 330);
    floating->getDockWidget()->QWidget::window()->setGeometry(asked);
    settleProjects();
    const QRect floatingPlace = floating->getDockWidget()->QWidget::window()->geometry();

    // The four left in the first workspace are split two by two, and a splitter of each direction is moved.
    core().dockLayout().splitAllEqually(host().target(first));
    settleProjects();
    ASSERT_EQ(dockedIn(first).size(), 4u);
    for (std::size_t left = 0; left < grid.size(); ++left)
    {
        for (std::size_t right = left + 1; right < grid.size(); ++right)
        {
            ASSERT_NE(groupOf(grid[left]), groupOf(grid[right])) << "the split left two images in one group";
        }
    }
    // Which cell of the grid the image lands in is the split's to choose: it grows towards its neighbours, whichever
    // sides of the area it lies against.
    const QRect evenly = placeOf(*grid.front());
    const bool againstLeft = evenly.x() == 0;
    const bool againstTop = evenly.y() == 0;
    grid.front()->getDockWidget()->dockWidget()->resizeInLayout(againstLeft ? 0 : 60, againstTop ? 0 : 40,
                                                                againstLeft ? 60 : 0, againstTop ? 40 : 0);
    settleProjects();
    ASSERT_FALSE(near(placeOf(*grid.front()), evenly)) << "no splitter moved";

    // The first workspace locked, the second the one the person is in.
    lockByTheMenu(first);
    ASSERT_TRUE(core().dockLayout().isLocked(host().target(first)));
    window().workspaces().setCurrent(second);
    window().showViews();
    settleProjects();
    ASSERT_EQ(host().current(), second);

    // What is on screen, by each image's file.
    struct Seen
    {
        QString file;
        QString workspace;
        QRect place;
    };
    std::vector<Seen> seen;
    for (ImageViewControl* const view : grid)
    {
        seen.push_back(
            Seen{QDir::cleanPath(view->document().names().sourcePath), host().workspaceOf(*view), placeOf(*view)});
        ASSERT_EQ(seen.back().workspace, first);
        ASSERT_TRUE(seen.back().place.isValid());
    }
    const QString tabOneFile = QDir::cleanPath(tabOne->document().names().sourcePath);
    const QString tabTwoFile = QDir::cleanPath(tabTwo->document().names().sourcePath);
    const QString floatingFile = QDir::cleanPath(floating->document().names().sourcePath);
    ASSERT_EQ(groupOf(tabOne)->indexOfDockWidget(tabOne->getDockWidget()->dockWidget()), 0);
    ASSERT_EQ(groupOf(tabOne)->indexOfDockWidget(tabTwo->getDockWidget()->dockWidget()), 1);
    ASSERT_TRUE(tabOne->getDockWidget()->dockWidget()->isCurrentTab());

    // Closed: the images go with the project, which keeps what it was left with.
    closeProject();
    ASSERT_FALSE(projects().isOpen());
    EXPECT_TRUE(views().empty()) << "a project's images stayed open when it closed";
    EXPECT_TRUE(QFileInfo(ProjectLayoutStore::layoutFilePath(folder)).isFile());
    EXPECT_EQ(ProjectLayoutStore::layoutFilePath(folder), folder + QStringLiteral("/workspaces/layout.json"));
    const ProjectDocument kept = documentOf(folder);
    ASSERT_EQ(kept.workspaces.size(), 2u);
    EXPECT_EQ(kept.workspaces[0].id, first);
    EXPECT_EQ(kept.workspaces[0].views.size(), 4u);
    EXPECT_TRUE(kept.workspaces[0].locked);
    EXPECT_FALSE(kept.workspaces[0].last);
    EXPECT_EQ(kept.workspaces[1].id, second);
    EXPECT_EQ(kept.workspaces[1].name, QStringLiteral("Colour"));
    EXPECT_EQ(kept.workspaces[1].views.size(), 2u);
    EXPECT_FALSE(kept.workspaces[1].locked);
    EXPECT_TRUE(kept.workspaces[1].last);
    ASSERT_EQ(kept.floatingViews.size(), 1u);
    for (const ProjectWorkspace& workspace : kept.workspaces)
    {
        for (const an::ui::shell::ProjectWorkspaceView& view : workspace.views)
        {
            EXPECT_TRUE(view.dockWidget.startsWith(QStringLiteral("an.view."))) << view.dockWidget.toStdString();
        }
    }

    // Opened again.
    openProject(folder);
    ASSERT_TRUE(projects().isOpen());
    ASSERT_EQ(views().size(), 7u) << "the project did not open every image it had open";
    ASSERT_EQ(host().model().count(), 2u);
    EXPECT_EQ(host().ids(), (QStringList{first, second}));
    EXPECT_EQ(host().workspaceName(second), QStringLiteral("Colour"));
    EXPECT_TRUE(core().dockLayout().isLocked(host().target(first))) << "the first workspace's lock did not come back";
    EXPECT_TRUE(host().model().find(first)->locked);
    EXPECT_FALSE(core().dockLayout().isLocked(host().target(second)));
    EXPECT_EQ(host().current(), second) << "the workspace used last is not the current one";
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_EQ(host().shown(), second);

    // Each image of the grid in its workspace, in a group of its own, where it was.
    std::vector<ImageViewControl*> again;
    for (const Seen& was : seen)
    {
        ImageViewControl* const view = viewOf(was.file);
        ASSERT_NE(view, nullptr) << was.file.toStdString();
        again.push_back(view);
        EXPECT_EQ(host().workspaceOf(*view), was.workspace) << was.file.toStdString();
        const QRect now = placeOf(*view);
        EXPECT_TRUE(near(now, was.place))
            << was.file.toStdString() << " was at " << was.place.x() << "," << was.place.y() << " " << was.place.width()
            << "x" << was.place.height() << " and is at " << now.x() << "," << now.y() << " " << now.width() << "x"
            << now.height();
    }
    for (std::size_t left = 0; left < again.size(); ++left)
    {
        for (std::size_t right = left + 1; right < again.size(); ++right)
        {
            EXPECT_NE(groupOf(again[left]), groupOf(again[right]));
        }
    }

    // The tabs in their group, in their order, the same one shown.
    ImageViewControl* const tabOneAgain = viewOf(tabOneFile);
    ImageViewControl* const tabTwoAgain = viewOf(tabTwoFile);
    ASSERT_NE(tabOneAgain, nullptr);
    ASSERT_NE(tabTwoAgain, nullptr);
    EXPECT_EQ(host().workspaceOf(*tabOneAgain), second);
    ASSERT_EQ(groupOf(tabOneAgain), groupOf(tabTwoAgain)) << "the tabbed images are no longer tabs of one group";
    EXPECT_EQ(groupOf(tabOneAgain)->indexOfDockWidget(tabOneAgain->getDockWidget()->dockWidget()), 0);
    EXPECT_EQ(groupOf(tabOneAgain)->indexOfDockWidget(tabTwoAgain->getDockWidget()->dockWidget()), 1);
    EXPECT_TRUE(tabOneAgain->getDockWidget()->dockWidget()->isCurrentTab());

    // The floating image in a window of its own, where it was.
    ImageViewControl* const floatingAgain = viewOf(floatingFile);
    ASSERT_NE(floatingAgain, nullptr);
    ASSERT_TRUE(floatingAgain->isInFloatingWindow());
    EXPECT_TRUE(host().workspaceOf(*floatingAgain).isEmpty());
    EXPECT_EQ(floatingAgain->getDockWidget()->QWidget::window()->geometry(), floatingPlace);

    // Each is the project's image again, its history in the project's folder.
    for (ImageViewControl* const view : views())
    {
        EXPECT_FALSE(core().projectHistory().imageOf(*view).isEmpty())
            << view->getUniqueName() << " came back as no image of the project";
    }
}

// An image whose file is gone is left out and said once - one message, with Locate... and Remove from the project -
// and the others open where they were. Locate... takes the project's record to the file's new place, and the image
// opens in the workspace it was in.
TEST_F(ProjectLayout, MissingImageIsReportedAndLeftOut)
{
    const QString folder = makeProject(QStringLiteral("Missing"));
    const QString first = host().ids().front();
    ImageViewControl* const left = openInProject(QStringLiteral("missing/left.fits"));
    ImageViewControl* const lost = openInProject(QStringLiteral("missing/lost.fits"));
    ImageViewControl* const right = openInProject(QStringLiteral("missing/right.fits"));
    ASSERT_NE(left, nullptr);
    ASSERT_NE(lost, nullptr);
    ASSERT_NE(right, nullptr);
    core().dockLayout().splitAllEqually(host().target(first));
    settleProjects();
    const QString leftFile = QDir::cleanPath(left->document().names().sourcePath);
    const QString lostFile = QDir::cleanPath(lost->document().names().sourcePath);
    const QString rightFile = QDir::cleanPath(right->document().names().sourcePath);
    const QString lostId = projects().imageFor(lostFile)->id;

    closeProject();
    ASSERT_TRUE(views().empty());
    // The file moves where neither its path nor its path from the project's folder finds it.
    const QString moved = QDir(root()).filePath(QStringLiteral("elsewhere/found.fits"));
    ASSERT_TRUE(QDir().mkpath(QFileInfo(moved).absolutePath()));
    ASSERT_TRUE(QFile::rename(lostFile, moved));

    openProject(folder);
    ASSERT_TRUE(projects().isOpen());
    ASSERT_EQ(views().size(), 2u) << "the images whose files are there did not open";
    ImageViewControl* const leftAgain = viewOf(leftFile);
    ImageViewControl* const rightAgain = viewOf(rightFile);
    ASSERT_NE(leftAgain, nullptr);
    ASSERT_NE(rightAgain, nullptr);
    EXPECT_EQ(host().workspaceOf(*leftAgain), first);
    EXPECT_EQ(host().workspaceOf(*rightAgain), first);
    EXPECT_FALSE(leftAgain->isInFloatingWindow());
    EXPECT_FALSE(rightAgain->isInFloatingWindow());
    // The place of the image left out is closed up: the two fill the dock area between them.
    const qint64 dockArea = areaOf(host().mainWindow(first)->rect());
    EXPECT_GE(areaOf(placeOf(*leftAgain)) + areaOf(placeOf(*rightAgain)), dockArea * 9 / 10)
        << "the place of the image that was left out stayed empty";

    // Said once, in one message over the window, with what can be done about it.
    QMessageBox* const said = flows().missingImagesMessage();
    ASSERT_NE(said, nullptr) << "the image that could not be opened was not said";
    EXPECT_EQ(said->parentWidget(), &window());
    EXPECT_EQ(said->windowModality(), Qt::WindowModal);
    EXPECT_TRUE(said->text().contains(QStringLiteral("was not found"))) << said->text().toStdString();
    EXPECT_TRUE(said->informativeText().contains(QDir::toNativeSeparators(lostFile)))
        << said->informativeText().toStdString();
    auto* const locate = said->findChild<QPushButton*>(QStringLiteral("projectLocateImages"));
    auto* const remove = said->findChild<QPushButton*>(QStringLiteral("projectRemoveImages"));
    ASSERT_NE(locate, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_EQ(locate->text(), QStringLiteral("Locate..."));
    EXPECT_EQ(remove->text(), QStringLiteral("Remove from the project"));
    int messages = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        messages += widget->isVisible() && qobject_cast<QMessageBox*>(widget) != nullptr ? 1 : 0;
    }
    EXPECT_EQ(messages, 1) << "the image that could not be opened was said more than once";
    ASSERT_NE(projects().image(lostId), nullptr) << "the image left out is still the project's";

    // Locate...: asked where the file is now, from the nearest folder that is still there.
    QString askedFrom;
    QString askedAs;
    flows().setFileChooser([&](QWidget* parent, const QString& caption, const QString& start) {
        EXPECT_EQ(parent, &window());
        askedAs = caption;
        askedFrom = start;
        return QDir::toNativeSeparators(moved);
    });
    locate->click();
    EXPECT_TRUE(askedAs.contains(QStringLiteral("lost.fits"))) << askedAs.toStdString();
    EXPECT_EQ(QDir::cleanPath(askedFrom), QFileInfo(lostFile).absolutePath());
    EXPECT_EQ(projects().image(lostId)->path, QDir::cleanPath(moved)) << "the project's record did not follow the file";
    EXPECT_TRUE(
        an::tests::app::processEventsUntil([&]() { return viewOf(QDir::cleanPath(moved)) != nullptr; }, kProjectOpenMs))
        << "the image that was found did not open";
    settleProjects();
    ImageViewControl* const found = viewOf(QDir::cleanPath(moved));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(host().workspaceOf(*found), first);
    EXPECT_TRUE(
        an::tests::app::processEventsUntil([&]() { return core().projectHistory().imageOf(*found) == lostId; }, 5000))
        << "the image that was found is not the project's image it was";
    EXPECT_EQ(flows().missingImagesMessage(), nullptr);
}

// Remove from the project forgets the images that were not found: the project lists them no more, their history's
// folder stays, and opening the project again says nothing.
TEST_F(ProjectLayout, RemoveForgetsAnImageThatIsGone)
{
    const QString folder = makeProject(QStringLiteral("Forgets"));
    ImageViewControl* const stays = openInProject(QStringLiteral("forgets/stays.fits"));
    ImageViewControl* const gone = openInProject(QStringLiteral("forgets/gone.fits"));
    ASSERT_NE(stays, nullptr);
    ASSERT_NE(gone, nullptr);
    const QString goneFile = QDir::cleanPath(gone->document().names().sourcePath);
    const QString goneId = projects().imageFor(goneFile)->id;
    closeProject();
    ASSERT_TRUE(QFile::remove(goneFile));

    openProject(folder);
    ASSERT_EQ(views().size(), 1u);
    QMessageBox* const said = flows().missingImagesMessage();
    ASSERT_NE(said, nullptr);
    auto* const remove = said->findChild<QPushButton*>(QStringLiteral("projectRemoveImages"));
    ASSERT_NE(remove, nullptr);
    remove->click();
    settleProjects();
    EXPECT_EQ(projects().image(goneId), nullptr);
    EXPECT_EQ(projects().current()->images.size(), 1u);
    EXPECT_EQ(flows().missingImagesMessage(), nullptr);

    closeProject();
    openProject(folder);
    EXPECT_EQ(views().size(), 1u);
    EXPECT_EQ(flows().missingImagesMessage(), nullptr) << "an image the project forgot was said again";
}

// Opening a project returns before any of its images is open: they are read on workers, the window's own work goes
// on meanwhile, and the arrangement is applied once they are open.
TEST_F(ProjectLayout, RestoreDoesNotBlockTheGuiThread)
{
    const QString folder = makeProject(QStringLiteral("Workers"));
    for (const char* name : {"workers-a.fits", "workers-b.fits", "workers-c.fits"})
    {
        ASSERT_NE(openInProject(QString::fromLatin1(name)), nullptr);
    }
    closeProject();
    ASSERT_TRUE(views().empty());

    // The window's own work, while the project opens: a timer of the GUI thread.
    int ticks = 0;
    QTimer heartbeat;
    heartbeat.setInterval(0);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&ticks]() { ++ticks; });
    heartbeat.start();

    QElapsedTimer asked;
    asked.start();
    flows().openProjectFolder(folder);
    const qint64 returnedAfter = asked.elapsed();
    ASSERT_TRUE(projects().isOpen());
    EXPECT_TRUE(layout().isRestoring()) << "opening the project waited for its arrangement";
    EXPECT_TRUE(views().empty()) << "an image was open when the call that opens the project returned";
    EXPECT_EQ(ticks, 0);
    EXPECT_LT(returnedAfter, 2000) << "opening the project took as long as reading its images";

    // While the images open, the arrangement half restored is not kept, and the project is not left.
    EXPECT_FALSE(layout().save());
    EXPECT_FALSE(projects().close()) << "a project whose images are still opening was closed";
    EXPECT_TRUE(projects().isOpen());

    waitForTheRestore();
    heartbeat.stop();
    EXPECT_EQ(views().size(), 3u);
    EXPECT_GT(ticks, 0) << "the GUI thread ran nothing of its own while the project's images opened";
    for (ImageViewControl* const view : views())
    {
        EXPECT_FALSE(host().workspaceOf(*view).isEmpty()) << view->getUniqueName() << " was left undocked";
    }
}

// With no project open nothing is kept: the store writes nothing, closing nothing writes nothing, and no layout file
// appears anywhere the application or the case writes.
TEST_F(ProjectLayout, NothingIsWrittenWithoutAProject)
{
    ASSERT_FALSE(projects().isOpen());
    const QStringList before = layoutFilesUnder(root());
    const QString storage = qEnvironmentVariable("AFTERNIGHT_TEST_STORAGE_ROOT");
    ASSERT_FALSE(storage.isEmpty());
    const QStringList storedBefore = layoutFilesUnder(storage);

    ImageViewControl* const first = openView(QStringLiteral("no-project-first"));
    ImageViewControl* const second = openView(QStringLiteral("no-project-second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const QString file = writeImage(QStringLiteral("no-project.fits"));
    openFile(file);
    core().dockLayout().splitAllEqually(host().target(host().ids().front()));
    settleProjects();

    EXPECT_FALSE(layout().save()) << "an arrangement was written with no project open";
    EXPECT_FALSE(layout().restore());
    EXPECT_FALSE(layout().isRestoring());
    EXPECT_FALSE(projects().close());
    EXPECT_TRUE(layout().lastWorkspace().isEmpty());
    EXPECT_FALSE(window().continueProject());
    EXPECT_TRUE(window().continueLabel().isEmpty());
    EXPECT_EQ(views().size(), 3u) << "an image closed though no project did";
    EXPECT_EQ(layoutFilesUnder(root()), before);
    EXPECT_EQ(layoutFilesUnder(storage), storedBefore);
}

// A task is never kept: the page of a process and a task floating in a window of its own are in no layout and in no
// record of the project, and the project opened again opens no process.
TEST_F(ProjectLayout, TasksAreNotSaved)
{
    const QString folder = makeProject(QStringLiteral("Tasks"));
    ImageViewControl* const image = openInProject(QStringLiteral("tasks-image.fits"));
    ASSERT_NE(image, nullptr);
    // Two tasks over an image that is none of the project's, so the project closes without a question: one a page of
    // the centre, one floating in a window of its own.
    ImageViewControl* const target = openView(QStringLiteral("tasks-target"));
    ASSERT_NE(target, nullptr);
    an::ui::observatory::TaskPresenter* const presenter = window().taskPresenter();
    ASSERT_NE(presenter, nullptr);
    core().viewService().setActiveImageViewControl(target);
    ASSERT_TRUE(core().processLaunch().launch(std::string(kTargetProcess)));
    settleProjects();
    ASSERT_EQ(presenter->model().tasks().size(), 1u);
    const an::ui::shell::TaskId floatingTask = presenter->model().tasks().back().id;
    ASSERT_TRUE(window().floatTask(floatingTask));
    settleProjects();
    an::ui::observatory::TaskWindow* const taskWindow = window().taskWindow(floatingTask);
    ASSERT_NE(taskWindow, nullptr);
    const QString taskDock = an::ui::observatory::TaskWindow::dockName(floatingTask);

    ASSERT_TRUE(layout().save());
    QFile file(ProjectLayoutStore::layoutFilePath(folder));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray written = file.readAll();
    file.close();
    ASSERT_FALSE(written.isEmpty());
    EXPECT_FALSE(written.contains(taskDock.toUtf8())) << "a task's window is in the project's layout";
    EXPECT_TRUE(written.contains(image->getDockWidget()->uniqueName().toUtf8()))
        << "the project's image is not in its layout";
    const ProjectDocument kept = documentOf(folder);
    ASSERT_EQ(kept.workspaces.size(), 1u);
    ASSERT_EQ(kept.workspaces.front().views.size(), 1u) << "the project lists a view that is no image of its own";
    EXPECT_TRUE(kept.floatingViews.empty());
    for (const an::ui::shell::ProjectWorkspaceView& view : kept.workspaces.front().views)
    {
        EXPECT_NE(view.dockWidget, taskDock);
    }

    // Closed and opened again: the task is as it was - it is the window's, not the project's - and no other is made.
    const QRect taskPlace = taskWindow->window()->geometry();
    closeProject();
    ASSERT_FALSE(projects().isOpen());
    EXPECT_EQ(presenter->model().tasks().size(), 1u) << "a task closed with a project that does not hold its image";
    openProject(folder);
    ASSERT_TRUE(projects().isOpen());
    EXPECT_EQ(presenter->model().tasks().size(), 1u) << "opening a project opened a process";
    ASSERT_NE(window().taskWindow(floatingTask), nullptr);
    EXPECT_EQ(window().taskWindow(floatingTask)->window()->geometry(), taskPlace) << "the layout moved a task's window";
    EXPECT_EQ(views().size(), 2u);
}

// A project closes with its images, each asked about as Close asks: an image with unsaved changes the person keeps
// keeps the project open, and so does it keep another from taking its place. Answered Yes, the project closes. An
// image that is none of the project's stays open throughout.
TEST_F(ProjectLayout, AProjectClosesWithItsImagesAndOneKeptKeepsItOpen)
{
    const QString folder = makeProject(QStringLiteral("Kept open"));
    ImageViewControl* const plain = openInProject(QStringLiteral("kept-plain.fits"));
    ImageViewControl* const edited = openInProject(QStringLiteral("kept-edited.fits"), 0.25f);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(edited, nullptr);
    ImageViewControl* const other = openView(QStringLiteral("kept-none-of-its-own"));
    ASSERT_NE(other, nullptr);
    core().viewService().setActiveImageViewControl(edited);
    core().actions().action(ids::kNegativeTransformation)->trigger();
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return std::abs(an::tests::app::viewSample(*edited) - 0.75) < 1e-3; }));
    ASSERT_TRUE(edited->hasPendingChanges());
    const QPointer<ImageViewControl> editedGuard(edited);
    const QPointer<ImageViewControl> otherGuard(other);

    // A second project to switch to.
    QTemporaryDir elsewhere;
    ASSERT_TRUE(elsewhere.isValid());
    QString second;
    {
        ProjectService maker;
        an::ui::shell::ProjectCreateRequest request;
        request.name = QStringLiteral("Other project");
        request.location = QDir::cleanPath(elsewhere.path());
        const an::ui::shell::ProjectOutcome made = maker.create(request);
        ASSERT_TRUE(made.ok()) << made.why.toStdString();
        second = made.folder;
        ASSERT_TRUE(maker.close());
    }

    const auto answerNo = [](const std::shared_ptr<an::tests::app::Answered>& answered) {
        return an::tests::app::answerWhenShown([answered](QWidget* shown) {
            auto* const box = qobject_cast<QMessageBox*>(shown);
            if (box == nullptr || box->button(QMessageBox::No) == nullptr)
            {
                return false;
            }
            answered->seen = true;
            answered->title = box->windowTitle();
            box->button(QMessageBox::No)->click();
            return true;
        });
    };

    // Close Project, the question answered No: the image and the project stay.
    auto refused = std::make_shared<an::tests::app::Answered>();
    std::shared_ptr<an::tests::app::WindowPoll> poll = answerNo(refused);
    closeProject();
    poll->ended = true;
    EXPECT_TRUE(refused->seen) << "closing the project asked nothing about its unsaved image";
    EXPECT_EQ(refused->title, QStringLiteral("Close image with unsaved changes"));
    EXPECT_TRUE(projects().isOpen()) << "a project closed though one of its images stays open";
    EXPECT_EQ(projects().currentFolder(), folder);
    ASSERT_FALSE(editedGuard.isNull());
    EXPECT_EQ(views().size(), 2u) << "the image with nothing to ask about closed, the other two stay";

    // Another project asked for, answered No again: the open project stays, the other is not opened and not locked,
    // and nothing is said over the window.
    refused = std::make_shared<an::tests::app::Answered>();
    poll = answerNo(refused);
    flows().openProjectFolder(second);
    settleProjects();
    poll->ended = true;
    EXPECT_TRUE(refused->seen);
    EXPECT_EQ(projects().currentFolder(), folder);
    EXPECT_FALSE(QFileInfo::exists(second + QStringLiteral("/.lock")));
    EXPECT_EQ(flows().refusalMessage(), nullptr) << "a project the person kept open was said as a refusal";

    // Answered Yes: the project closes with its image; the one that is none of its own stays.
    auto answered = std::make_shared<an::tests::app::Answered>();
    poll = an::tests::app::answerWhenShown(
        [answered](QWidget* shown) { return an::tests::app::pressYesOrOk(shown, answered.get()); });
    closeProject();
    poll->ended = true;
    EXPECT_TRUE(answered->seen);
    EXPECT_FALSE(projects().isOpen());
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
        settleProjects();
        return editedGuard.isNull();
    }));
    ASSERT_FALSE(otherGuard.isNull()) << "an image that is none of the project's closed with it";
    EXPECT_EQ(views().size(), 1u);
}

// An image that was open before a project is opened is none of the project's: it stays open, floating over the
// project's workspaces, and the project that closes leaves it open. A workspace the project does not list goes.
TEST_F(ProjectLayout, AnImageOpenBeforeTheProjectStaysOpenAndFloats)
{
    const QString folder = makeProject(QStringLiteral("Before"));
    const QString first = host().ids().front();
    ImageViewControl* const own = openInProject(QStringLiteral("before-own.fits"));
    ASSERT_NE(own, nullptr);
    const QString ownFile = QDir::cleanPath(own->document().names().sourcePath);
    closeProject();
    ASSERT_TRUE(views().empty());

    // With no project: an image in the workspace the project lists, and one in a workspace it does not.
    ImageViewControl* const before = openView(QStringLiteral("before-the-project"));
    ASSERT_NE(before, nullptr);
    const QString extra = host().create();
    host().setCurrent(extra);
    ImageViewControl* const inExtra = openView(QStringLiteral("before-in-another-workspace"));
    ASSERT_NE(inExtra, nullptr);
    settleProjects();
    ASSERT_EQ(host().workspaceOf(*inExtra), extra);
    const QPointer<ImageViewControl> beforeGuard(before);
    const QPointer<ImageViewControl> extraGuard(inExtra);

    openProject(folder);
    ASSERT_TRUE(projects().isOpen());
    ASSERT_FALSE(beforeGuard.isNull());
    ASSERT_FALSE(extraGuard.isNull());
    EXPECT_EQ(views().size(), 3u);
    EXPECT_EQ(host().ids(), QStringList{first}) << "a workspace the project does not list stayed";
    EXPECT_TRUE(before->isInFloatingWindow()) << "an image that is none of the project's is docked in its workspace";
    EXPECT_TRUE(inExtra->isInFloatingWindow());
    ImageViewControl* const ownAgain = viewOf(ownFile);
    ASSERT_NE(ownAgain, nullptr);
    EXPECT_EQ(host().workspaceOf(*ownAgain), first);
    EXPECT_TRUE(core().projectHistory().imageOf(*before).isEmpty());

    closeProject();
    EXPECT_FALSE(projects().isOpen());
    ASSERT_FALSE(beforeGuard.isNull()) << "an image that is none of the project's closed with it";
    ASSERT_FALSE(extraGuard.isNull());
    EXPECT_EQ(views().size(), 2u);
}

// Continue shows the workspace of the project that was used last, under its name: the one the project recorded once
// it is opened again, and from then on the one the person is in.
TEST_F(ProjectLayout, ContinueShowsTheWorkspaceUsedLast)
{
    const QString folder = makeProject(QStringLiteral("Continue"));
    const QString first = host().ids().front();
    ASSERT_NE(openInProject(QStringLiteral("continue-a.fits")), nullptr);
    ImageViewControl* const inSecond = openInProject(QStringLiteral("continue-b.fits"));
    ASSERT_NE(inSecond, nullptr);
    const QString second = host().create(QStringLiteral("Narrowband"));
    host().mainWindow(second)->addDockWidget(inSecond->getDockWidget(), KDDockWidgets::Location_OnLeft);
    host().setCurrent(second);
    window().showViews();
    settleProjects();
    EXPECT_EQ(window().continueLabel(), QString::fromUtf8("Continue \xC2\xB7 Narrowband"));

    closeProject();
    openProject(folder);
    ASSERT_EQ(host().model().count(), 2u);
    EXPECT_EQ(layout().lastWorkspace(), second);
    EXPECT_EQ(window().continueLabel(), QString::fromUtf8("Continue \xC2\xB7 Narrowband"));

    // From Home, Continue is back in that workspace.
    window().showHome();
    settleProjects();
    ASSERT_TRUE(window().isHomeShown());
    ASSERT_TRUE(window().continueProject());
    settleProjects();
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_EQ(host().shown(), second);

    // The person moves to the other workspace: that is the one used last from then on.
    host().setCurrent(first);
    window().showViews();
    settleProjects();
    EXPECT_EQ(layout().lastWorkspace(), first);
    EXPECT_EQ(window().continueLabel(), QString::fromUtf8("Continue \xC2\xB7 ") + host().workspaceName(first));
}

// A view's dock widget has a name of its own for good, never its view's title: an image renamed and another opened
// under the name it had are two dock widgets the dock library tells apart. Two of one name is a layout the library
// refuses to write, and the project would keep no arrangement at all.
TEST_F(ProjectLayout, AnImageRenamedAndAnotherOfItsOldNameAreBothKept)
{
    const QString folder = makeProject(QStringLiteral("Renamed"));
    ImageViewControl* const first = openInProject(QStringLiteral("renamed/twin.fits"));
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->getUniqueName(), "twin");
    first->setUniqueName("the-first-twin");
    settleProjects();
    ImageViewControl* const second = openInProject(QStringLiteral("renamed/other/twin.fits"));
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->getUniqueName(), "twin") << "the second image did not take the name the first one had";

    const QString firstDock = first->getDockWidget()->uniqueName();
    const QString secondDock = second->getDockWidget()->uniqueName();
    EXPECT_NE(firstDock, secondDock);
    EXPECT_TRUE(firstDock.startsWith(QStringLiteral("an.view."))) << firstDock.toStdString();
    EXPECT_NE(firstDock, QStringLiteral("twin")) << "a dock widget is named by its view's title";
    EXPECT_EQ(first->getDockWidget()->title(), QStringLiteral("the-first-twin"));
    EXPECT_EQ(second->getDockWidget()->title(), QStringLiteral("twin"));
    EXPECT_FALSE(an::ui::shell::DockHooks::saveLayout().isEmpty())
        << "the dock library refused to write a layout: two dock widgets have one name";

    closeProject();
    EXPECT_TRUE(QFileInfo(ProjectLayoutStore::layoutFilePath(folder)).isFile());
    EXPECT_EQ(documentOf(folder).workspaces.front().views.size(), 2u);
    openProject(folder);
    EXPECT_EQ(views().size(), 2u);
}

// A new project made while one is open: the open project is left first - its arrangement kept, its images closed -
// and an image of it is not one the new project is offered; an image that is none of its own is, and joins the new
// project.
TEST_F(ProjectLayout, ANewProjectTakesTheImagesThatAreNoneOfTheOpenProjects)
{
    const QString first = makeProject(QStringLiteral("Left for a new one"));
    ImageViewControl* const own = openInProject(QStringLiteral("newproject-own.fits"));
    ASSERT_NE(own, nullptr);
    const QString ownFile = QDir::cleanPath(own->document().names().sourcePath);
    closeProject();
    // An image opened with no project, then the project again: the image is none of its own.
    const QString strayFile = writeImage(QStringLiteral("newproject-stray.fits"));
    openFile(strayFile);
    openProject(first);
    ASSERT_EQ(views().size(), 2u);
    const QPointer<ImageViewControl> ownGuard(viewOf(ownFile));
    const QPointer<ImageViewControl> strayGuard(viewOf(strayFile));
    ASSERT_FALSE(ownGuard.isNull());
    ASSERT_FALSE(strayGuard.isNull());

    shellAction(shellIds::kNewProject)->trigger();
    an::ui::NewProjectDialog* const dialog = flows().newProjectDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_FALSE(dialog->addOpenImagesCheck()->isHidden());
    EXPECT_EQ(dialog->addOpenImagesCheck()->text(), QStringLiteral("Add the open image to the project"))
        << "an image of the project that closes is offered to the new one";
    dialog->locationPicker()->setPath(QDir::toNativeSeparators(root()));
    dialog->nameField()->setText(QStringLiteral("The new one"));
    ASSERT_TRUE(dialog->createButton()->isEnabled()) << dialog->problem().toStdString();
    dialog->createButton()->click();
    settleProjects();

    ASSERT_TRUE(projects().isOpen());
    EXPECT_EQ(projects().currentFolder(), root() + QStringLiteral("/The new one"));
    EXPECT_TRUE(ownGuard.isNull()) << "an image of the project that was left stayed open";
    ASSERT_FALSE(strayGuard.isNull());
    ASSERT_EQ(projects().current()->images.size(), 1u);
    EXPECT_EQ(projects().current()->images.front().path, strayFile);
    // The project that was left kept its image for the next time.
    const ProjectDocument left = documentOf(first);
    ASSERT_EQ(left.workspaces.size(), 1u);
    ASSERT_EQ(left.workspaces.front().views.size(), 1u);
    EXPECT_EQ(left.workspaces.front().views.front().image, left.images.front().id);
}

// A project with no layout - its file gone, or bytes that are none - still opens its images, each docked in the
// workspace the project lists it in, as a new view is.
TEST_F(ProjectLayout, AProjectWhoseLayoutIsGoneOpensItsImagesInTheirWorkspaces)
{
    const QString folder = makeProject(QStringLiteral("No layout"));
    const QString first = host().ids().front();
    ImageViewControl* const one = openInProject(QStringLiteral("nolayout-a.fits"));
    ASSERT_NE(one, nullptr);
    ImageViewControl* const two = openInProject(QStringLiteral("nolayout-b.fits"));
    ASSERT_NE(two, nullptr);
    const QString second = host().create();
    host().mainWindow(second)->addDockWidget(two->getDockWidget(), KDDockWidgets::Location_OnLeft);
    settleProjects();
    const QString oneFile = QDir::cleanPath(one->document().names().sourcePath);
    const QString twoFile = QDir::cleanPath(two->document().names().sourcePath);
    closeProject();

    for (const QByteArray& bytes : {QByteArray(), QByteArrayLiteral("{ this is no layout")})
    {
        const QString layoutFile = ProjectLayoutStore::layoutFilePath(folder);
        ASSERT_TRUE(QFile::remove(layoutFile) || !QFileInfo::exists(layoutFile));
        if (!bytes.isEmpty())
        {
            QFile file(layoutFile);
            ASSERT_TRUE(file.open(QIODevice::WriteOnly));
            file.write(bytes);
        }
        openProject(folder);
        ASSERT_EQ(views().size(), 2u);
        ASSERT_NE(viewOf(oneFile), nullptr);
        ASSERT_NE(viewOf(twoFile), nullptr);
        EXPECT_EQ(host().workspaceOf(*viewOf(oneFile)), first);
        EXPECT_EQ(host().workspaceOf(*viewOf(twoFile)), second);
        closeProject();
        ASSERT_TRUE(views().empty());
    }
}

/// The exit and the next start, over shells of the case's own.
class ProjectLayoutAtExit : public ::testing::Test
{
protected:
    /// Starts the shell over settings that name a project as the one used last, to be opened at the start.
    [[nodiscard]] static std::unique_ptr<RunningObservatory> startWith(const QString& last)
    {
        return startObservatory([last](an::ui::shell::ApplicationCore& core) {
            an::ui::IGlobalSettingsProvider* const settings = core.globalSettings();
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), last);
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QString());
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP),
                                   last.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
            core.projects().setSettings(settings);
        });
    }

    /// Ends the application as a person does: the window closed, then the shell and the session.
    static void exitApplication(std::unique_ptr<RunningObservatory> running)
    {
        an::ui::IGlobalSettingsProvider* const settings = running->core->globalSettings();
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QString());
        (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("0"));
        running->shell->window()->close();
        const an::Status shut = stopObservatory(std::move(running));
        EXPECT_TRUE(shut.ok()) << shut.message();
    }

    [[nodiscard]] static ImageViewControl* viewOf(RunningObservatory& running, const QString& path)
    {
        for (ImageViewControl* view : running.core->viewService().getListOfViewControls())
        {
            if (view != nullptr && view->hasImage() && QDir::cleanPath(view->document().names().sourcePath) == path)
            {
                return view;
            }
        }
        return nullptr;
    }

    [[nodiscard]] QString writeImage(RunningObservatory& running, const QString& name) const
    {
        const an::consumer::EngineSession& session = running.session->engineSession();
        const QString path = QDir(QDir::cleanPath(m_root.path())).filePath(name);
        std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session, 0.4f);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return {};
        }
        const an::Result<an::OutputIntent> intent = an::ui::appContext()->getOutputIntentProvider()->buildOutputIntent(
            *asset->descriptor(), an::CodecFormat::Fits);
        EXPECT_TRUE(intent.hasValue() && an::ui::writeViewImage(session, *asset, path, *intent, {}).ok());
        return QDir::cleanPath(path);
    }

    QTemporaryDir m_root;
};

// The application is ended with a project open - two images side by side, one of them moved to the left - and no
// question asked of its images: the project keeps the arrangement. Started again with the project opened at the
// start, the two images are back as they were.
TEST_F(ProjectLayoutAtExit, TheArrangementIsKeptAtExitAndIsBackAtTheNextStart)
{
    ASSERT_TRUE(m_root.isValid());
    QString folder;
    QString leftFile;
    QString rightFile;
    QRect leftPlace;
    QRect rightPlace;
    {
        std::unique_ptr<RunningObservatory> running = startWith(QString());
        ASSERT_NE(running, nullptr);
        an::ui::observatory::ObservatoryWindow& window = *running->shell->window();
        an::ui::shell::ProjectCreateRequest request;
        request.name = QStringLiteral("At exit");
        request.location = QDir::cleanPath(m_root.path());
        const an::ui::shell::ProjectOutcome made = running->core->projects().create(request);
        ASSERT_TRUE(made.ok()) << made.why.toStdString();
        folder = made.folder;
        leftFile = writeImage(*running, QStringLiteral("exit-left.fits"));
        rightFile = writeImage(*running, QStringLiteral("exit-right.fits"));
        for (const QString& file : {leftFile, rightFile})
        {
            an::ui::appContext()->getRecentFilesService()->openDroppedFiles({file});
            ASSERT_TRUE(an::tests::app::processEventsUntil(
                [&]() {
                    ImageViewControl* const view = viewOf(*running, file);
                    return view != nullptr && !running->core->projectHistory().imageOf(*view).isEmpty();
                },
                kProjectOpenMs))
                << file.toStdString() << " did not open in the project";
        }
        ImageViewControl* const left = viewOf(*running, leftFile);
        ImageViewControl* const right = viewOf(*running, rightFile);
        const QString workspace = window.workspaces().ids().front();
        window.workspaces().mainWindow(workspace)->addDockWidget(left->getDockWidget(), KDDockWidgets::Location_OnLeft);
        settleProjects();
        left->getDockWidget()->dockWidget()->resizeInLayout(0, 0, -80, 0);
        settleProjects();
        ASSERT_NE(left->getDockWidget()->group(), right->getDockWidget()->group());
        leftPlace = placeOf(*left);
        rightPlace = placeOf(*right);
        ASSERT_LT(leftPlace.x(), rightPlace.x());
        ASSERT_FALSE(QFileInfo::exists(ProjectLayoutStore::layoutFilePath(folder)))
            << "the arrangement was written before the project was left";
        exitApplication(std::move(running));
    }
    ASSERT_TRUE(QFileInfo(ProjectLayoutStore::layoutFilePath(folder)).isFile())
        << "the application ended and the project did not keep its arrangement";
    {
        const an::ui::shell::ProjectDocumentRead kept =
            an::ui::shell::readProjectDocument(QDir(folder).filePath(QStringLiteral("project.anproj")));
        ASSERT_TRUE(kept.isRead()) << kept.why.toStdString();
        ASSERT_EQ(kept.document->workspaces.size(), 1u);
        EXPECT_EQ(kept.document->workspaces.front().views.size(), 2u)
            << "the project does not list the images that were open as the application ended";
        EXPECT_TRUE(kept.document->workspaces.front().last);
    }

    std::unique_ptr<RunningObservatory> again = startWith(folder);
    ASSERT_NE(again, nullptr);
    ASSERT_TRUE(again->core->projects().isOpen());
    an::ui::observatory::ObservatoryWindow& window = *again->shell->window();
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&]() {
            return !window.projectLayout().isRestoring() && viewOf(*again, leftFile) != nullptr &&
                   viewOf(*again, rightFile) != nullptr;
        },
        kProjectOpenMs))
        << "the project opened at the start did not get its images back";
    settleProjects();
    ImageViewControl* const left = viewOf(*again, leftFile);
    ImageViewControl* const right = viewOf(*again, rightFile);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_TRUE(window.isWorkspaceShown());
    EXPECT_NE(left->getDockWidget()->group(), right->getDockWidget()->group());
    EXPECT_TRUE(near(placeOf(*left), leftPlace))
        << "the left image is " << placeOf(*left).width() << " px wide and was " << leftPlace.width();
    EXPECT_TRUE(near(placeOf(*right), rightPlace));
    exitApplication(std::move(again));
}

// With no project open, ending the application writes no layout anywhere, and the next start opens nothing.
TEST_F(ProjectLayoutAtExit, WithNoProjectTheExitWritesNothingAndTheStartOpensNothing)
{
    ASSERT_TRUE(m_root.isValid());
    const QString storage = qEnvironmentVariable("AFTERNIGHT_TEST_STORAGE_ROOT");
    ASSERT_FALSE(storage.isEmpty());
    const QStringList storedBefore = layoutFilesUnder(storage);
    {
        std::unique_ptr<RunningObservatory> running = startWith(QString());
        ASSERT_NE(running, nullptr);
        const QString file = writeImage(*running, QStringLiteral("exit-no-project.fits"));
        an::ui::appContext()->getRecentFilesService()->openDroppedFiles({file});
        ASSERT_TRUE(
            an::tests::app::processEventsUntil([&]() { return viewOf(*running, file) != nullptr; }, kProjectOpenMs));
        exitApplication(std::move(running));
    }
    EXPECT_TRUE(layoutFilesUnder(QDir::cleanPath(m_root.path())).isEmpty());
    EXPECT_EQ(layoutFilesUnder(storage), storedBefore);

    std::unique_ptr<RunningObservatory> again = startWith(QString());
    ASSERT_NE(again, nullptr);
    settleProjects();
    EXPECT_FALSE(again->core->projects().isOpen());
    EXPECT_TRUE(again->core->viewService().getListOfViewControls().empty()) << "the start opened an image";
    EXPECT_TRUE(again->shell->window()->isHomeShown());
    exitApplication(std::move(again));
}

} // namespace
} // namespace an::tests::observatory
