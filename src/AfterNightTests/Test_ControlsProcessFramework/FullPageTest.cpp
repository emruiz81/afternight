// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process shown as a full page - the process whole, with no canvas and no column - over a window with a menu bar of
/// its own: the page holds it in a scroll area that scrolls only when the page is smaller than the least the process
/// takes, its menu bar is hidden and its menus offered, each entry acting as in the bar; at its head it says the
/// process's message and shows a run of the process's own with Cancel, unless the process shows its run in itself; and
/// a process made outside a launch stays the window it is. And what the canvas of a page with a preview says while
/// images are still to choose.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "app/ProcessRegistry.h"
#include "components/AnInfoBanner.h"
#include "components/AnTaskHeader.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QAbstractButton>
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
    /// @param showsItsRun whether the process shows a run in itself, as an integration does.
    explicit MenuBarProcess(const bool showsItsRun = false)
        : TopLevelProcessBaseControl("Full Page Test Process"), m_showsItsRun(showsItsRun)
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

    /// A page's Cancel asks the process to stop its run: counted, and answered as stopped.
    bool cancelRun() override
    {
        ++cancels;
        return true;
    }

    QMenuBar* menuBar = nullptr;
    QWidget* body = nullptr;
    QAction* addFrames = nullptr;
    QAction* execute = nullptr;
    int framesAdded = 0;
    int executed = 0;
    int cancels = 0;

protected:
    [[nodiscard]] bool showsRunInItself() const override
    {
        return m_showsItsRun;
    }

private:
    bool m_showsItsRun = false;
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
    MenuBarProcess& open(const bool launched = true, const bool showsItsRun = false)
    {
        if (launched)
        {
            const an::ui::ProcessLaunchScope launch(fullPageDescriptor());
            process = new MenuBarProcess(showsItsRun);
        }
        else
        {
            process = new MenuBarProcess(showsItsRun);
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

// A full page says its process's message at its head, over the area that holds the process, and nothing while the
// process says none.
TEST(FullPage, TheProcesssMessageIsSaidAtItsHead)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open();
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    an::ui::AnInfoBanner* const banner = page->banner();
    ASSERT_NE(banner, nullptr) << "a full page has a place for its process's message";
    EXPECT_FALSE(banner->isVisibleTo(&process)) << "nothing is said while the process says nothing";

    process.showProcessMessage(QStringLiteral("Image Stacking"), QStringLiteral("Stacking completed with warnings."),
                               TopLevelProcessBaseControl::ProcessMessageTone::Warning);
    pumpQuietly();
    EXPECT_TRUE(banner->isVisibleTo(&process));
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_EQ(page->message(), QStringLiteral("Image Stacking: Stacking completed with warnings."));
    EXPECT_EQ(process.processMessage(), page->message());
    EXPECT_LE(banner->geometry().bottom(), page->fullArea()->geometry().top()) << "over the area, not in it";
    EXPECT_FALSE(page->fullArea()->isAncestorOf(banner));

    process.clearProcessMessage();
    pumpQuietly();
    EXPECT_FALSE(banner->isVisibleTo(&process));
    EXPECT_TRUE(page->message().isEmpty());
}

// A run of the process's own is shown at the page's head - what it does and how far it has come, in the message's
// place - with Cancel at its end, which asks the process to stop it. What the process laid out stays as the process
// leaves it, as it does behind a progress window.
TEST(FullPage, ARunIsShownAtItsHeadWithACancelThatAsksTheProcess)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open();
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_NE(page->runBanner(), nullptr);
    QAbstractButton* const cancel = page->cancelButton();
    ASSERT_NE(cancel, nullptr) << "a full page has no footer: its Cancel is at its head";
    EXPECT_EQ(page->runBars(), 0);
    EXPECT_FALSE(page->runBanner()->isVisibleTo(&process));
    EXPECT_FALSE(cancel->isVisibleTo(&process));

    process.showProcessMessage(QStringLiteral("Image Stacking"), QStringLiteral("The last run warned."));
    process.setRunState(an::ui::runningState(QStringLiteral("stacking"), 0.25));
    pumpQuietly();
    EXPECT_EQ(page->runBars(), 1);
    EXPECT_TRUE(page->runBanner()->isVisibleTo(&process));
    EXPECT_TRUE(cancel->isVisibleTo(&process));
    EXPECT_TRUE(cancel->isEnabled());
    EXPECT_FALSE(page->banner()->isVisibleTo(&process)) << "the run's row takes the message's place";
    EXPECT_LE(page->runBanner()->mapTo(page->page(), QPoint(0, page->runBanner()->height())).y(),
              page->fullArea()->geometry().top() + 1)
        << "over the area";
    EXPECT_TRUE(process.body->isEnabled()) << "the process disables what it must itself";

    cancel->click();
    EXPECT_EQ(process.cancels, 1);

    process.setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
    pumpQuietly();
    EXPECT_EQ(page->runBars(), 0);
    EXPECT_FALSE(page->runBanner()->isVisibleTo(&process));
    EXPECT_FALSE(cancel->isVisibleTo(&process));
    EXPECT_TRUE(page->banner()->isVisibleTo(&process)) << "the message is back once the run has ended";
}

// A process that shows its run in itself - a run's view with its own Cancel - has no row and no Cancel at its page's
// head; its message is still said there.
TEST(FullPage, AProcessThatShowsItsRunInItselfHasNoRowAtItsHead)
{
    FullPageCase paged;
    ASSERT_NE(paged.context, nullptr);
    MenuBarProcess& process = paged.open(true, true);
    ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->runBanner(), nullptr);
    EXPECT_EQ(page->cancelButton(), nullptr);
    ASSERT_NE(page->banner(), nullptr);

    process.setRunState(an::ui::runningState(QStringLiteral("running"), 0.5));
    pumpQuietly();
    EXPECT_EQ(page->runBars(), 0);
    EXPECT_EQ(process.cancels, 0);

    // No row takes the message's place: what the process says during its run is said, and stays once the run ended.
    process.showProcessMessage(QStringLiteral("Importing"), QStringLiteral("Reading frame metadata."));
    pumpQuietly();
    EXPECT_TRUE(page->banner()->isVisibleTo(&process));
    process.setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Succeeded});
    pumpQuietly();
    EXPECT_TRUE(page->banner()->isVisibleTo(&process));
    process.clearProcessMessage();
    pumpQuietly();
    EXPECT_FALSE(page->banner()->isVisibleTo(&process));
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
