// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory window driven through itself: its regions and their sizes in each density, the place it shows with
// and without an image, a process opened as a window of its own, its end in the application's exit order, and what
// each close says in the log of who asked and what came of it.

#include "ObservatoryWindowCase.h"

#include "chrome/StatusLine.h"
#include "chrome/TopBar.h"
#include "pages/HomePage.h"
#include "pages/IntegrationHubPage.h"
#include "pages/ProjectHomePage.h"
#include "pages/WorkspacePage.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "app/ProcessRegistry.h"
#include "app/RuntimeStorageCoordinator.h"
#include "components/AnIconRail.h"
#include "controls/ImageViewControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "services/OpenProcessRegistry.h"
#include "services/ProcessLaunchService.h"
#include "theme/AnDesignTokens.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <AfterNightPlatform/Logging.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLayout>
#include <QMessageBox>
#include <QPointer>
#include <QStackedWidget>
#include <QStringList>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using ObservatoryWindow = ObservatoryWindowCase;

/// The processes the cases launch: one over the active image, shown as a page; one of the integration workflow, which
/// stays a window of its own.
constexpr std::string_view kBrightnessContrast = "tone_brightness.actionAdjustBrightness_Contrast";

/// A widget's place inside the window's central widget.
[[nodiscard]] QRect placeIn(const QWidget& widget, const QWidget& ancestor)
{
    return QRect(widget.mapTo(&ancestor, QPoint(0, 0)), widget.size());
}

/// Lays the window out now, after a change of what sizes it.
void layOut(an::ui::observatory::ObservatoryWindow& window)
{
    window.refreshTheme();
    QCoreApplication::processEvents();
    if (QLayout* const layout = window.centralWidget()->layout())
    {
        layout->invalidate();
        layout->activate();
    }
    QCoreApplication::processEvents();
}

// The window's regions are sized from the metrics of the density in force: the top bar across the window, the rail
// under it at the left, the status line at the bottom right of the rail, the centre between the rail and the column;
// the centre holds Home and the workspace, and the window takes no less than the smallest window the design keeps.
TEST_F(ObservatoryWindow, Regions)
{
    an::ui::observatory::ObservatoryWindow& shown = window();
    EXPECT_EQ(shown.minimumSize(), QSize(an::ui::observatory::ObservatoryWindow::kMinimumWidth,
                                         an::ui::observatory::ObservatoryWindow::kMinimumHeight));
    EXPECT_EQ(shown.minimumSize(), QSize(1024, 640));

    // The centre's pages: Home, the workspaces' place, a stack of the workspaces' pages, the Integration hub, and
    // Home while a project is open.
    ASSERT_NE(shown.centre()->indexOf(shown.homePage()), -1);
    ASSERT_NE(shown.centre()->indexOf(shown.workspaces().stack()), -1);
    EXPECT_NE(shown.workspaces().stack()->indexOf(shown.workspacePage()), -1);
    ASSERT_NE(shown.centre()->indexOf(shown.integrationHubPage()), -1);
    ASSERT_NE(shown.centre()->indexOf(shown.projectHomePage()), -1);
    EXPECT_EQ(shown.centre()->count(), 4);

    // The suite draws the shell in the profile it names, whose two densities size the bars differently.
    ASSERT_EQ(an::ui::styleProfile(), an::ui::StyleProfile::Observatory);
    const an::ui::ThemeDensity before = an::ui::themeDensity();
    std::vector<int> barHeights;
    for (const an::ui::ThemeDensity density : {an::ui::ThemeDensity::Compact, an::ui::ThemeDensity::Comfortable})
    {
        an::ui::setThemeDensity(density);
        layOut(shown);
        const an::ui::MetricTokens& metrics = an::ui::tokens().metrics;
        const QWidget& central = *shown.centralWidget();
        const QRect bar = placeIn(*shown.topBar(), central);
        const QRect rail = placeIn(*shown.rail(), central);
        const QRect status = placeIn(*shown.statusLine(), central);
        const QRect centre = placeIn(*shown.centre(), central);
        const int densityIndex = density == an::ui::ThemeDensity::Compact ? 0 : 1;

        EXPECT_EQ(bar.height(), metrics.barTop) << "density " << densityIndex;
        barHeights.push_back(bar.height());
        EXPECT_EQ(bar.topLeft(), QPoint(0, 0));
        EXPECT_EQ(bar.width(), central.width());

        EXPECT_EQ(rail.width(), metrics.rail) << "density " << densityIndex;
        EXPECT_EQ(rail.top(), bar.bottom() + 1);
        EXPECT_EQ(rail.left(), 0);
        EXPECT_EQ(rail.bottom(), central.height() - 1);

        EXPECT_EQ(status.height(), metrics.statusLine) << "density " << densityIndex;
        EXPECT_EQ(status.left(), rail.right() + 1);
        EXPECT_EQ(status.right(), central.width() - 1);
        EXPECT_EQ(status.bottom(), central.height() - 1);

        EXPECT_EQ(centre.left(), rail.right() + 1);
        EXPECT_EQ(centre.top(), bar.bottom() + 1);
        EXPECT_EQ(centre.bottom(), status.top() - 1);
    }
    an::ui::setThemeDensity(before);
    layOut(shown);
    ASSERT_EQ(barHeights.size(), 2u);
    EXPECT_LT(barHeights[0], barHeights[1]) << "the Comfortable density's top bar is not taller than the Compact one's";
}

// With no image open the window shows Home; an image opened shows the workspace that holds it, and closing the last
// one shows Home again.
TEST_F(ObservatoryWindow, NoViewsShowsHome)
{
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_FALSE(window().isWorkspaceShown());

    ImageViewControl* const view = openView(QStringLiteral("no-views-shows-home"));
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(window().isWorkspaceShown());

    core().viewService().removeViewControl(view, true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    EXPECT_TRUE(views().empty());
    EXPECT_TRUE(window().isHomeShown());
}

// A process that states a window opens as a window of its own: a top-level window outside the window's regions,
// registered among the open processes. No process of the application states one, so the process is the suite's.
TEST_F(ObservatoryWindow, ProcessOpensAsAWindow)
{
    ImageViewControl* const view = openView(QStringLiteral("process-opens-as-a-window"));
    ASSERT_NE(view, nullptr);

    const std::size_t openBefore = core().openProcesses().size();
    const QPointer<TopLevelProcessBaseControl> process(new WindowOfItsOwn());
    process->show();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return !process.isNull() && process->isVisible(); }));
    EXPECT_TRUE(process->isWindow());
    EXPECT_FALSE(window().isAncestorOf(process));
    EXPECT_EQ(core().openProcesses().size(), openBefore + 1);
    // The shell lends its presenter, which leaves a process of a window one.
    ASSERT_NE(shell().processPresenter(), nullptr);
    EXPECT_FALSE(shell().processPresenter()->adopt(*process));
}

// The window is active again at once after another window had the activation and went: what every case of the suite
// starts from. The suites' platform on Windows hands the activation to no window when the one that had it closes and
// delivers a request for it only as a window is shown or hidden, so asking and waiting cost each case that followed
// a dialog the whole of the wait.
TEST_F(ObservatoryWindow, IsActiveAgainAtOnceAfterAnotherWindowCameAndWent)
{
    if (!s_platformActivatesWindows)
    {
        GTEST_SKIP() << "the platform activates no window";
    }
    ASSERT_TRUE(window().isActiveWindow());
    {
        QDialog other(&window());
        other.show();
        other.activateWindow();
        ASSERT_TRUE(an::tests::app::processEventsUntil([&other]() { return other.isActiveWindow(); }, 2000));
        EXPECT_FALSE(window().isActiveWindow());
    }
    QCoreApplication::processEvents();
    if (QGuiApplication::platformName() == QLatin1String("minimal"))
    {
        ASSERT_FALSE(window().isActiveWindow()) << "the platform handed the activation back by itself";
        // Asked as a widget asks, the request stays queued: this is what the wait was waiting for.
        window().activateWindow();
        QCoreApplication::processEvents();
        EXPECT_FALSE(window().isActiveWindow());
    }

    QElapsedTimer taken;
    taken.start();
    EXPECT_TRUE(an::tests::app::activateWindow(window()));
    EXPECT_LT(taken.elapsed(), 1000) << "making the window active again waited";
    EXPECT_TRUE(window().isActiveWindow());
    EXPECT_EQ(QApplication::activeWindow(), &window());
    // Asked again of a window that is active, it changes nothing.
    EXPECT_TRUE(an::tests::app::activateWindow(window()));
}

/// The window's exit, each case over a start of its own: its end is the application's.
class ObservatoryWindowExit : public ::testing::Test
{
};

// Closing the window with an image and a process open follows the application's exit order: the question about the
// open processes is asked and answered, the process window is destroyed before the session ends, the session shuts
// down cleanly, the context is gone, and the run's temporary folder holds no file of the run.
TEST_F(ObservatoryWindowExit, ClosesInTheExitOrder)
{
    std::unique_ptr<RunningObservatory> running = startObservatory();
    ASSERT_NE(running, nullptr);
    an::ui::observatory::ObservatoryWindow* const window = running->shell->window();
    ASSERT_NE(window, nullptr);
    ASSERT_EQ(an::ui::appContext(), running->core->context());

    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(running->session->engineSession(), QStringLiteral("closes-in-the-exit-order"));
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const view = running->core->viewService().userViews().createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "closes-in-the-exit-order");
    ASSERT_NE(view, nullptr);
    running->core->viewService().setActiveImageViewControl(view);
    ASSERT_TRUE(running->core->processLaunch().launch(std::string(kBrightnessContrast)));
    // The process is a page of the window: it is found among the open processes.
    QPointer<TopLevelProcessBaseControl> process;
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        for (TopLevelProcessBaseControl* candidate : running->core->openProcesses().snapshot())
        {
            if (candidate != nullptr && candidate->isVisible())
            {
                process = candidate;
            }
        }
        return !process.isNull();
    }));
    EXPECT_TRUE(process->isPresentedAsPage());
    const QString temporary = running->core->runtimeStorage().getTempDirPath();

    // The window asks before it closes over unsaved images and open processes; every question is answered Yes.
    auto answered = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerEveryMessageBox(answered);
    EXPECT_TRUE(window->close());
    ++an::tests::app::g_caseGeneration;
    EXPECT_TRUE(answered->seen) << "the window closed over an open process without asking";

    const an::Status shut = stopObservatory(std::move(running));
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_TRUE(process.isNull()) << "the process window outlived the application";
    EXPECT_EQ(an::ui::appContext(), nullptr);

    QStringList left;
    QDirIterator files(temporary, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        left.append(files.next());
    }
    EXPECT_TRUE(left.isEmpty()) << left.join(QLatin1Char('\n')).toStdString();
}

/// The records the logger kept after the record that reads `mark`, oldest first.
[[nodiscard]] QStringList loggedAfter(const std::string& mark)
{
    QStringList after;
    bool marked = false;
    for (const spdlog::details::log_msg_buffer& record : an::platform::recentLogMessages(0u))
    {
        const std::string text(record.payload.data(), record.payload.size());
        if (marked)
        {
            after.append(QString::fromStdString(text));
        }
        else if (text == mark)
        {
            marked = true;
        }
    }
    return after;
}

/// The place of the first record that holds `fragment`, or -1.
[[nodiscard]] qsizetype placeOf(const QStringList& records, const QString& fragment)
{
    for (qsizetype at = 0; at < records.size(); ++at)
    {
        if (records[at].contains(fragment))
        {
            return at;
        }
    }
    return -1;
}

// Each close of the window says in the log who asked and what came of it, so an application that ended says why: Exit
// chosen from the menu is the application's own request, after a line that names Exit; a close the person declines
// leaves the window open and says so, and one agreed to says the application quits.
TEST_F(ObservatoryWindowExit, SaysInTheLogWhoClosedItAndWhatCameOfIt)
{
    std::unique_ptr<RunningObservatory> running = startObservatory();
    ASSERT_NE(running, nullptr);
    an::ui::observatory::ObservatoryWindow* const window = running->shell->window();
    ASSERT_NE(window, nullptr);
    // An open process makes the window ask before it closes.
    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(running->session->engineSession(), QStringLiteral("says-who-closed-it"));
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const view = running->core->viewService().userViews().createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "says-who-closed-it");
    ASSERT_NE(view, nullptr);
    running->core->viewService().setActiveImageViewControl(view);
    ASSERT_TRUE(running->core->processLaunch().launch(std::string(kBrightnessContrast)));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return running->core->openProcesses().size() > 0u; }));
    QAction* const exit = running->core->actions().action(an::ui::shell::ids::kExit);
    ASSERT_NE(exit, nullptr);

    // Exit, declined.
    const std::string declinedMark = "case mark: Exit is chosen and declined";
    LOG_INFO("{}", declinedMark);
    auto declined = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerWhenShown([declined](QWidget* widget) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box == nullptr || box->button(QMessageBox::No) == nullptr)
        {
            return false;
        }
        declined->seen = true;
        box->button(QMessageBox::No)->click();
        return true;
    });
    exit->trigger();
    ++an::tests::app::g_caseGeneration;
    ASSERT_TRUE(declined->seen) << "the window closed over an open process without asking";
    EXPECT_TRUE(window->isVisible());
    const QStringList afterDeclined = loggedAfter(declinedMark);
    const qsizetype chosen = placeOf(afterDeclined, QStringLiteral("Exit was chosen"));
    const qsizetype asked = placeOf(afterDeclined, QStringLiteral("asked to close by the application"));
    const qsizetype stays = placeOf(afterDeclined, QStringLiteral("stays open"));
    EXPECT_GE(chosen, 0) << afterDeclined.join(QLatin1Char('\n')).toStdString();
    EXPECT_GT(asked, chosen) << afterDeclined.join(QLatin1Char('\n')).toStdString();
    EXPECT_GT(stays, asked) << afterDeclined.join(QLatin1Char('\n')).toStdString();
    EXPECT_EQ(placeOf(afterDeclined, QStringLiteral("quits")), -1)
        << afterDeclined.join(QLatin1Char('\n')).toStdString();

    // Exit, agreed to.
    const std::string agreedMark = "case mark: Exit is chosen and agreed to";
    LOG_INFO("{}", agreedMark);
    auto agreed = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerTheNextMessageBox(agreed.get());
    exit->trigger();
    ++an::tests::app::g_caseGeneration;
    EXPECT_TRUE(agreed->seen);
    EXPECT_FALSE(window->isVisible());
    const QStringList afterAgreed = loggedAfter(agreedMark);
    const qsizetype chosenAgain = placeOf(afterAgreed, QStringLiteral("Exit was chosen"));
    const qsizetype askedAgain = placeOf(afterAgreed, QStringLiteral("asked to close by the application"));
    const qsizetype quits = placeOf(afterAgreed, QStringLiteral("The application quits"));
    EXPECT_GE(chosenAgain, 0) << afterAgreed.join(QLatin1Char('\n')).toStdString();
    EXPECT_GT(askedAgain, chosenAgain) << afterAgreed.join(QLatin1Char('\n')).toStdString();
    EXPECT_GT(quits, askedAgain) << afterAgreed.join(QLatin1Char('\n')).toStdString();

    const an::Status shut = stopObservatory(std::move(running));
    EXPECT_TRUE(shut.ok()) << shut.message();
}

} // namespace
} // namespace an::tests::observatory
