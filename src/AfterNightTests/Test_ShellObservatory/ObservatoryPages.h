// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The pages of the Observatory window a walk of the interface visits - Home, Project Home, the Integration hub, a
/// workspace, a task's page of each surface and the full pages - each opened as a person opens it, and left again.

#pragma once

#include "ProjectCase.h"

#include "ObservatoryWindow.h"
#include "pages/HomePage.h"
#include "pages/IntegrationHubPage.h"
#include "pages/ProjectHomePage.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"

#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "projects/ProjectService.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "theme/AnDesignTokens.h"

#include <kddockwidgets/DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QPointer>
#include <QStackedWidget>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace an::tests::observatory
{

/// A page of the window.
enum class Page
{
    Home,
    ProjectHome,
    IntegrationHub,
    Workspace,
    /// A process's page with its preview: Curves Transformation.
    PreviewTask,
    /// A process's page with no preview, its canvas a view of its target: Resize.
    MirrorTask,
    /// A process shown whole: Image Integration.
    ImageIntegration,
    ImageIntegrationLite,
    ImageListAnalyzer
};

/// Every page a walk visits, in the order it visits them.
[[nodiscard]] inline const std::vector<Page>& everyPage()
{
    static const std::vector<Page> kPages = {
        Page::Home,       Page::ProjectHome,      Page::IntegrationHub,       Page::Workspace,        Page::PreviewTask,
        Page::MirrorTask, Page::ImageIntegration, Page::ImageIntegrationLite, Page::ImageListAnalyzer};
    return kPages;
}

/// A page's name, as a failure names it.
[[nodiscard]] inline std::string pageName(const Page page)
{
    switch (page)
    {
    case Page::Home:
        return "Home";
    case Page::ProjectHome:
        return "Project Home";
    case Page::IntegrationHub:
        return "the Integration hub";
    case Page::Workspace:
        return "a workspace";
    case Page::PreviewTask:
        return "Curves Transformation's page";
    case Page::MirrorTask:
        return "Resize's page";
    case Page::ImageIntegration:
        return "Image Integration's page";
    case Page::ImageIntegrationLite:
        return "Image Integration Lite's page";
    case Page::ImageListAnalyzer:
        return "Image List Analyzer's page";
    }
    return "a page";
}

/// Whether a page is a task's.
[[nodiscard]] inline bool isTaskPage(const Page page)
{
    return page == Page::PreviewTask || page == Page::MirrorTask || page == Page::ImageIntegration ||
           page == Page::ImageIntegrationLite || page == Page::ImageListAnalyzer;
}

/// The Observatory window, each case starting with no project open and none among the ones used last, which opens the
/// pages of the walk and leaves each again.
class PagesCase : public ProjectCase
{
protected:
    void SetUp() override
    {
        ProjectCase::SetUp();
        forgetProjects();
        removeEmptyDocks();
    }

    void TearDown() override
    {
        leave();
        forgetProjects();
        ProjectCase::TearDown();
    }

    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        return *window().taskPresenter();
    }

    /// Lets the window settle: posted work delivered, the deletions done, layouts made.
    static void settle()
    {
        settleProjects();
        QCoreApplication::processEvents();
    }

    /// Opens a process as its menu entry does, and waits until it is shown.
    static TopLevelProcessBaseControl* launch(const std::string& descriptorId)
    {
        const std::size_t before = tasks().model().tasks().size();
        EXPECT_TRUE(core().processLaunch().launch(descriptorId)) << descriptorId;
        TopLevelProcessBaseControl* made = nullptr;
        EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
            const std::vector<an::ui::shell::Task>& now = tasks().model().tasks();
            if (now.size() > before && !now.back().process.isNull() && now.back().process->isVisible())
            {
                made = now.back().process.data();
            }
            return made != nullptr;
        })) << descriptorId
            << " opened no task";
        settle();
        return made;
    }

    /// The process of the task page shown; null on any other page.
    [[nodiscard]] static TopLevelProcessBaseControl* shownProcess()
    {
        const an::ui::observatory::TaskPage* const page =
            window().isTaskPageShown() ? window().taskPage(window().shownTask()) : nullptr;
        return page != nullptr ? page->process() : nullptr;
    }

    /// Opens a page as a person opens it: Home with a file among the recent ones, a project made and opened, the hub,
    /// an image's workspace, a process over that image, a tool on no image. Answers the widget the centre shows.
    static QWidget* open(const Page page)
    {
        switch (page)
        {
        case Page::Home: {
            // Home shows a recent file, as it does once a person has worked: one opened and closed again.
            const QString file = writeImage(QStringLiteral("walk-recent.fits"));
            openFile(file);
            closeViews();
            window().showHome();
            break;
        }
        case Page::ProjectHome: {
            // A project of its own each time: a folder made by an earlier case is a project already.
            static int made = 0;
            an::ui::shell::ProjectCreateRequest request;
            request.name = QStringLiteral("Walk %1").arg(++made);
            request.location = root();
            const an::ui::shell::ProjectOutcome outcome = core().projects().create(request);
            EXPECT_TRUE(outcome.ok()) << outcome.why.toStdString();
            settle();
            openFile(writeImage(QStringLiteral("walk-project.fits")));
            settle();
            window().showHome();
            break;
        }
        case Page::IntegrationHub:
            window().showIntegrationHub();
            break;
        case Page::Workspace:
            EXPECT_NE(openView(QStringLiteral("walk-image")), nullptr);
            window().showViews();
            break;
        case Page::PreviewTask:
            EXPECT_NE(openView(QStringLiteral("walk-curves")), nullptr);
            EXPECT_NE(launch("transforms.actionCurves_Transformation"), nullptr);
            break;
        case Page::MirrorTask:
            EXPECT_NE(openView(QStringLiteral("walk-resize")), nullptr);
            EXPECT_NE(launch("geometry.actionResize"), nullptr);
            break;
        case Page::ImageIntegration:
            EXPECT_NE(launch(an::ui::observatory::IntegrationHubPage::kImageIntegration), nullptr);
            break;
        case Page::ImageIntegrationLite:
            EXPECT_NE(launch(an::ui::observatory::IntegrationHubPage::kImageIntegrationLite), nullptr);
            break;
        case Page::ImageListAnalyzer:
            EXPECT_NE(launch(an::ui::observatory::IntegrationHubPage::kImageListAnalyzer), nullptr);
            break;
        }
        settle();
        EXPECT_EQ(isTaskPage(page), window().isTaskPageShown()) << pageName(page);
        // A file opened, or a window shown and gone, may have taken the activation away.
        (void)an::tests::app::activateWindow(window());
        return window().centre()->currentWidget();
    }

    /// Leaves whatever page is open: every task closed, every image closed, the project closed, Home shown.
    static void leave()
    {
        for (const an::ui::shell::Task& task : std::vector<an::ui::shell::Task>(tasks().model().tasks()))
        {
            if (!task.process.isNull())
            {
                (void)task.process->cancelRun();
                if (task.process->runState().running())
                {
                    task.process->setRunState(
                        an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
                }
                (void)task.process->close();
            }
        }
        settle();
        closeViews();
        (void)core().projects().close();
        settle();
        window().showHome();
        settle();
    }

    /// Closes every image unasked, its dock with it, as a closed image goes: deleting the view alone leaves its dock in
    /// the workspace.
    static void closeViews()
    {
        for (ImageViewControl* view : views())
        {
            const QPointer<KDDockWidgets::QtWidgets::DockWidget> dock = view->getDockWidget();
            core().viewService().removeViewControl(view, true);
            if (!dock.isNull())
            {
                dock->deleteLater();
            }
        }
        settle();
    }

    /// The docks no image lives in any more go: the suite's other cases delete their views and leave the docks, which
    /// a page of this case's workspace would show.
    static void removeEmptyDocks()
    {
        for (QWidget* const widget : QApplication::allWidgets())
        {
            auto* const dock = qobject_cast<KDDockWidgets::QtWidgets::DockWidget*>(widget);
            if (dock != nullptr && dock->widget() == nullptr)
            {
                dock->deleteLater();
            }
        }
        settle();
    }

    /// Sets the window's size, as a display of that size leaves it, and lays it out.
    static void resizeTo(const QSize& size)
    {
        window().showNormal();
        window().resize(size);
        settle();
        // A layout request posted by a page's own resize is taken in the next round.
        settle();
    }

    /// Lays the window out in a density, as the View menu's Density does.
    static void setDensity(const an::ui::ThemeDensity density)
    {
        const char* const id =
            density == an::ui::ThemeDensity::Comfortable ? "actionDensity_Comfortable" : "actionDensity_Compact";
        for (QAction* action : window().findChildren<QAction*>())
        {
            if (action->objectName() == QLatin1String(id))
            {
                action->trigger();
            }
        }
        settle();
    }
};

} // namespace an::tests::observatory
