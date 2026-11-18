// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process shown as a full page - the process whole, with no canvas and no column - over a window with a menu bar of
/// its own: the page holds it in a scroll area that scrolls only when the page is smaller than the least the process
/// takes, its menu bar is hidden and its menus offered, each entry acting as in the bar; and a process made outside a
/// launch stays the window it is. And what the canvas of a page with a preview says while images are still to choose.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "app/ProcessRegistry.h"
#include "components/AnTaskHeader.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <memory>

namespace
{

using namespace an::tests::processes;
using an::ui::ProcessPageLayout;

/// The least size of the process's body.
constexpr QSize kLeast{600, 400};

/// A window of a menu bar of its own over a body of a least size: what an integration workflow's window is.
class MenuBarProcess final : public TopLevelProcessBaseControl
{
public:
    MenuBarProcess() : TopLevelProcessBaseControl("Full Page Test Process")
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        auto* layout = new QVBoxLayout(this);
        menuBar = new QMenuBar(this);
        menuBar->setNativeMenuBar(false);
        QMenu* const frames = menuBar->addMenu(QStringLiteral("Frames"));
        addFrames = frames->addAction(QStringLiteral("Add Frames"));
        QMenu* const run = menuBar->addMenu(QStringLiteral("Run"));
        execute = run->addAction(QStringLiteral("Execute"));
        layout->addWidget(menuBar);
        body = new QWidget(this);
        body->setObjectName(QStringLiteral("fullPageBody"));
        body->setMinimumSize(kLeast);
        layout->addWidget(body, 1);
        QObject::connect(addFrames, &QAction::triggered, this, [this]() { ++framesAdded; });
        QObject::connect(execute, &QAction::triggered, this, [this]() { ++executed; });
    }

    QMenuBar* menuBar = nullptr;
    QWidget* body = nullptr;
    QAction* addFrames = nullptr;
    QAction* execute = nullptr;
    int framesAdded = 0;
    int executed = 0;
};

/// A descriptor whose launch states a full page.
[[nodiscard]] an::ui::process_registry::ProcessDescriptor fullPageDescriptor()
{
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "test.actionTest_FullPage";
    descriptor.surface = an::ui::ProcessSurface::FullPage;
    return descriptor;
}

/// A page presenter and a context it is lent through, for one case; the processes go before the hosts that hold them.
struct FullPageCase
{
    explicit FullPageCase(QSize centre = PagePresenter::kCentre) : presenter(centre), context(openProcessContext())
    {
        if (context != nullptr)
        {
            context->presenter = &presenter;
            installed = std::make_unique<InstalledContext>(*context);
        }
    }
    ~FullPageCase()
    {
        delete process.data();
        installed.reset();
        if (context != nullptr)
        {
            context->presenter = nullptr;
        }
    }

    /// Makes the process - inside a launch of a full page, or outside every launch - and shows it.
    MenuBarProcess& open(const bool launched = true)
    {
        if (launched)
        {
            const an::ui::ProcessLaunchScope launch(fullPageDescriptor());
            process = new MenuBarProcess();
        }
        else
        {
            process = new MenuBarProcess();
        }
        process->show();
        pumpQuietly();
        return *process;
    }

    PagePresenter presenter;
    std::unique_ptr<ProcessTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    QPointer<MenuBarProcess> process;
};

/// Whether a scroll bar can move: the content is larger than the area along it.
[[nodiscard]] bool scrolls(const QScrollBar& bar)
{
    return bar.maximum() > bar.minimum();
}

} // namespace

// A full page is the process whole in a scroll area that fills the page: no canvas and no column, the process's body
// under it, its menu bar hidden.
TEST(FullPage, TheProcessIsThePageWithNoColumn)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open();
    EXPECT_EQ(process.processSurface(), an::ui::ProcessSurface::FullPage);
    ASSERT_TRUE(process.isPresentedAsPage());
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->canvas(), nullptr);
    EXPECT_EQ(page->column(), nullptr);
    EXPECT_EQ(page->body(), nullptr);
    EXPECT_EQ(page->footer(), nullptr);
    ASSERT_NE(page->fullArea(), nullptr);
    EXPECT_TRUE(page->fullArea()->isAncestorOf(process.body)) << "the process's body is in the page's area";
    EXPECT_FALSE(process.menuBar->isVisibleTo(&process)) << "its menu bar is offered, not shown";
    QWidget* const host = paged.presenter.hostOf(process);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(page->page()->size(), host->size()) << "the page fills what holds it";
}

// The page offers the process's own menus, in its menu bar's order, each entry the process's own action.
TEST(FullPage, ItsMenusAreOfferedAndEachEntryActs)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open();
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    const QList<QMenu*> menus = page->menus();
    ASSERT_EQ(menus.size(), 2);
    EXPECT_EQ(menus[0]->title(), QStringLiteral("Frames"));
    EXPECT_EQ(menus[1]->title(), QStringLiteral("Run"));
    ASSERT_FALSE(menus[0]->actions().isEmpty());
    ASSERT_FALSE(menus[1]->actions().isEmpty());
    menus[0]->actions().front()->trigger();
    menus[1]->actions().front()->trigger();
    EXPECT_EQ(process.framesAdded, 1);
    EXPECT_EQ(process.executed, 1);
}

// A page larger than the least the process takes shows it whole with no scroll bar; a smaller one scrolls to reach the
// rest.
TEST(FullPage, ALargerPageScrollsAndASmallerOneDoesNot)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open();
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    QScrollArea* const area = page->fullArea();
    ASSERT_NE(area, nullptr);
    EXPECT_FALSE(scrolls(*area->horizontalScrollBar())) << "the centre holds the process whole";
    EXPECT_FALSE(scrolls(*area->verticalScrollBar()));
    EXPECT_GE(process.body->width(), kLeast.width());

    QWidget* const host = paged.presenter.hostOf(process);
    ASSERT_NE(host, nullptr);
    host->resize(kLeast.width() / 2, kLeast.height() / 2);
    pumpQuietly();
    EXPECT_TRUE(scrolls(*area->horizontalScrollBar())) << "a smaller centre scrolls sideways";
    EXPECT_TRUE(scrolls(*area->verticalScrollBar())) << "and up and down";
    EXPECT_EQ(process.body->width(), kLeast.width()) << "the process keeps its least size";
}

// A full page says the job its process holds once the process states one, and tells its listener each time what it
// says changes - and only then.
TEST(FullPage, TheJobItsProcessHoldsIsToldToItsListener)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open();
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->jobHeading(), nullptr) << "a process that states no job is named by its process";
    int told = 0;
    page->setJobHeadingListener([&told]() { ++told; });

    an::ui::JobHeading job;
    job.title = QStringLiteral("Carina Nebula LRGB");
    job.primary = process.execute;
    page->setJobHeading(job);
    ASSERT_NE(page->jobHeading(), nullptr);
    EXPECT_EQ(page->jobHeading()->title, QStringLiteral("Carina Nebula LRGB"));
    EXPECT_EQ(page->jobHeading()->primary.data(), process.execute);
    EXPECT_EQ(told, 1);

    page->setJobHeading(job);
    EXPECT_EQ(told, 1) << "the same job said again tells nothing";
    job.facts = QStringLiteral("3 frames");
    page->setJobHeading(job);
    EXPECT_EQ(told, 2);
    EXPECT_EQ(page->jobHeading()->facts, QStringLiteral("3 frames"));
}

// A process made outside every launch belongs to no descriptor: it stays the window it is, its menu bar its own.
TEST(FullPage, AProcessOfNoLaunchStaysAWindow)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open(false);
    EXPECT_EQ(process.processSurface(), an::ui::ProcessSurface::Window);
    EXPECT_EQ(process.pageLayout(), nullptr);
    EXPECT_TRUE(process.isWindow());
    EXPECT_TRUE(process.menuBar->isVisibleTo(&process));
}

// While images are still to choose, the canvas names them and hides what it holds; once every one is chosen it shows
// the preview again, and says nothing.
TEST(ProcessPageLayout, TheCanvasNamesTheImagesStillToChoose)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(paged.context->engineSession(), 64u, 48u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("page target", std::move(*document));
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "test.actionTest_Scale";
    descriptor.surface = an::ui::ProcessSurface::PreviewPage;
    ProcessHandle process;
    {
        const an::ui::ProcessLaunchScope launched(descriptor);
        process = openProcess(*target, TestProcessOptions{.preview = true});
    }
    process->show();
    pumpQuietly();
    ASSERT_TRUE(settleProcess(*process));
    ProcessPageLayout* const page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_TRUE(page->canvasMessage().isEmpty()) << "the canvas shows the preview";

    page->setMissingInputs({QStringLiteral("Star image"), QStringLiteral("Starless image")});
    const QString said = page->canvasMessage();
    EXPECT_TRUE(said.contains(QStringLiteral("Star image"))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("Starless image"))) << said.toStdString();
    EXPECT_FALSE(process->previewView()->isVisibleTo(page->canvas())) << "what the canvas holds is hidden";

    page->setMissingInputs({});
    EXPECT_TRUE(process->previewView()->isVisibleTo(page->canvas()));
    EXPECT_TRUE(page->canvasMessage().isEmpty());
    process.reset();
}
