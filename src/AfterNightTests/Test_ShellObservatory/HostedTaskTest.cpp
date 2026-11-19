// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An extension's process the application draws is a task like a built-in process's: a preview page, or a mirror page
/// for one that does not preview, its header naming it with the quiet "extension" tag, its column the panel of its
/// parameters, floated to a window of its own and attached again like any other. While its host starts its body says
/// so; a host that failed leaves the task failed, its banner offering Restart and Close.

#include "Test_ShellObservatory/ObservatoryWindowCase.h"

#include "app/ExtensionHostManager.h"
#include "app/ExtensionHostedSession.h"
#include "app/ExtensionViewRequests.h"
#include "app/ProcessRegistry.h"
#include "components/AnInfoBanner.h"
#include "components/AnTaskHeader.h"
#include "controls/ExtensionHostedProcess.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QStringList>

#include <gtest/gtest.h>

#include <string>

namespace an::tests::observatory
{

namespace
{

using an::ui::extensions::ExtensionHostedProcess;
using an::ui::shell::Task;
using an::ui::shell::TaskPresentation;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The parameter list an extension states for its window: one float.
[[nodiscard]] QJsonArray strengthOnly()
{
    return QCborArray{QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                               {QStringLiteral("type"), QStringLiteral("float")},
                               {QStringLiteral("label"), QStringLiteral("Strength")},
                               {QStringLiteral("default"), 0.5},
                               {QStringLiteral("min"), 0.0},
                               {QStringLiteral("max"), 1.0}}}
        .toJsonArray();
}

class HostedTasks : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    /// Opens an extension's process over a view, as the launch coordinator makes one for a session whose panel the
    /// application draws: under its descriptor, the parameters kept from an earlier start drawn at once.
    static ExtensionHostedProcess* openHosted(ImageViewControl* target, const bool previews)
    {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = "tests.hosted.extension";
        descriptor.display_name = "Hosted Demo";
        descriptor.process_name = "Hosted Demo";
        descriptor.category = "filters";
        descriptor.action_object_name = "actionNoSuchHostedExtension";
        descriptor.is_extension_process = true;
        descriptor.extension_id = "tests.hosted";
        // As the launch coordinator states it: where the process is shown is its manifest's.
        descriptor.surface = previews ? an::ui::ProcessSurface::PreviewPage : an::ui::ProcessSurface::MirrorPage;
        ExtensionHostedProcess::Setup setup;
        setup.displayName = "Hosted Demo";
        setup.previews = previews;
        setup.keptParams = strengthOnly();
        ExtensionHostedProcess* process = nullptr;
        {
            const an::ui::ProcessLaunchScope launch(descriptor);
            process = new ExtensionHostedProcess(target, std::move(setup));
        }
        process->show();
        settle();
        return process;
    }
};

TEST_F(HostedTasks, AHostedExtensionIsATaskPageWithItsTag)
{
    ImageViewControl* const view = openView(QStringLiteral("hosted-target"));
    ASSERT_NE(view, nullptr);
    QPointer<ExtensionHostedProcess> process = openHosted(view, true);
    ASSERT_NE(process, nullptr);

    ASSERT_TRUE(process->isPresentedAsPage());
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->presentation, TaskPresentation::Page);
    EXPECT_EQ(task->surface, an::ui::ProcessSurface::PreviewPage);
    EXPECT_EQ(task->descriptorId, "tests.hosted.extension");
    EXPECT_EQ(task->target.data(), view);
    an::ui::observatory::TaskPage* const page = window().taskPage(task->id);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->header()->processName(), QStringLiteral("Hosted Demo"));

    // The column's header carries the quiet tag, and the body the panel drawn from the extension's parameters.
    const an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->tag(), QStringLiteral("extension"));
    EXPECT_EQ(layout->tagToolTip(), QStringLiteral("Runs in its own process"));
    ASSERT_NE(layout->canvas(), nullptr);
    auto* slider = process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(slider));
    EXPECT_NE(layout->footerButton(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(layout->footerButton(QDialogButtonBox::Reset), nullptr);

    process->close();
    settle();
    EXPECT_TRUE(process.isNull());
}

TEST_F(HostedTasks, AnExtensionWithNoPreviewIsAMirrorPage)
{
    ImageViewControl* const view = openView(QStringLiteral("hosted-mirror"));
    ASSERT_NE(view, nullptr);
    QPointer<ExtensionHostedProcess> process = openHosted(view, false);
    ASSERT_NE(process, nullptr);

    ASSERT_TRUE(process->isPresentedAsPage());
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->surface, an::ui::ProcessSurface::MirrorPage);
    const an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    EXPECT_FALSE(layout->toggles()->isVisibleTo(layout->column())) << "a mirror page has no toggles";
    // The target's own view is never taken into the page: the canvas shows a view of its image.
    EXPECT_FALSE(layout->page()->isAncestorOf(view));
    EXPECT_NE(process->interactionView(), view);

    process->close();
    settle();
}

TEST_F(HostedTasks, AHostedTaskFloatsAndAttachesLikeAnyOther)
{
    ImageViewControl* const view = openView(QStringLiteral("hosted-floating"));
    ASSERT_NE(view, nullptr);
    QPointer<ExtensionHostedProcess> process = openHosted(view, true);
    ASSERT_NE(process, nullptr);
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const an::ui::shell::TaskId id = task->id;

    ASSERT_TRUE(window().floatTask(id));
    settle();
    task = tasks().model().task(id);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->presentation, TaskPresentation::Window);
    ASSERT_NE(window().taskWindow(id), nullptr) << "its page floats whole in a window of its own";
    EXPECT_TRUE(process->hasPageLayout());

    ASSERT_TRUE(window().attachTask(id));
    settle();
    task = tasks().model().task(id);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->presentation, TaskPresentation::Page);
    EXPECT_TRUE(process->isPresentedAsPage());

    process->close();
    settle();
}

// An extension that hides its process window puts its task away: its page leaves the centre for the place shown before,
// or the window it floats in is minimized, and the task stays in the rail, shown again from there. One that closes its
// process window ends its session as one closed should, and its task closes.
TEST_F(HostedTasks, AnExtensionHidingItsWindowPutsItsTaskAwayAndClosingItClosesIt)
{
    ImageViewControl* const view = openView(QStringLiteral("hosted-hidden"));
    ASSERT_NE(view, nullptr);
    QPointer<ExtensionHostedProcess> process = openHosted(view, true);
    ASSERT_NE(process, nullptr);
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const an::ui::shell::TaskId id = task->id;
    ASSERT_TRUE(tasks().show(id));
    settle();
    ASSERT_TRUE(window().isTaskPageShown());
    ASSERT_EQ(window().shownTask(), id);

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionSessionController controller;
    controller.running_session.prepared_session.session_id = "observatory-hosted-hide";
    controller.running_session.hosted_panel = true;
    process->hostedSession()->bind(manager, controller);
    const auto hide = [&]() {
        an::ui::extensions::PanelHideMessage hidden;
        hidden.session_id = "observatory-hosted-hide";
        controller.running_session.panel_inbox.push_back(an::ui::extensions::toProtocolMessage(hidden));
        process->hostedSession()->route();
        settle();
    };

    // Its page leaves the centre; the task stays, and the rail shows it again.
    hide();
    EXPECT_FALSE(window().isTaskPageShown());
    ASSERT_FALSE(process.isNull());
    ASSERT_NE(tasks().model().task(id), nullptr) << "the task stays";
    ASSERT_TRUE(tasks().show(id));
    settle();
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), id);

    // Floating, its window is minimized, and showing the task brings it back.
    ASSERT_TRUE(window().floatTask(id));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    ASSERT_NE(floating->window(), nullptr);
    hide();
    EXPECT_TRUE(floating->window()->isMinimized());
    ASSERT_NE(tasks().model().task(id), nullptr);
    ASSERT_TRUE(tasks().show(id));
    settle();
    EXPECT_FALSE(floating->window()->isMinimized());
    ASSERT_TRUE(window().attachTask(id));
    settle();

    // Closing its window ends its session, not failed, and the task closes.
    process->hostedSession()->unbind(
        an::ui::extensions::ExtensionSessionEnd{.sessionId = "observatory-hosted-hide", .failed = false, .reason = {}});
    settle();
    EXPECT_TRUE(process.isNull());
    EXPECT_EQ(tasks().model().task(id), nullptr);
}

TEST_F(HostedTasks, AHostThatFailedLeavesItsTaskFailedWithRestartAndClose)
{
    ImageViewControl* const view = openView(QStringLiteral("hosted-failing"));
    ASSERT_NE(view, nullptr);
    QPointer<ExtensionHostedProcess> process = openHosted(view, true);
    ASSERT_NE(process, nullptr);
    const an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);

    // Its host starts: the body says so over the parameters kept, which wait disabled.
    EXPECT_TRUE(process->isStarting());
    auto* const row = process->findChild<QWidget*>(QStringLiteral("hostedStartingRow"));
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(layout->bodyContent()->isAncestorOf(row));
    EXPECT_TRUE(row->isVisibleTo(layout->column()));
    auto* const slider = process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    EXPECT_FALSE(slider->isEnabled());

    int restarts = 0;
    ExtensionHostedProcess::Hooks hooks;
    hooks.restartSession = [&restarts](std::string* /*error*/) {
        ++restarts;
        return true;
    };
    process->setHooks(std::move(hooks));
    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionSessionController controller;
    controller.running_session.prepared_session.session_id = "observatory-hosted";
    process->hostedSession()->bind(manager, controller);
    process->hostedSession()->unbind(an::ui::extensions::ExtensionSessionEnd{
        .sessionId = "observatory-hosted", .failed = true, .reason = QStringLiteral("The host ended unexpectedly.")});
    settle();

    // The task stays, failed with why; the banner says so with Restart and Close; the controls stay disabled.
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->run.phase, an::ui::ProcessRunState::Phase::Failed);
    EXPECT_TRUE(layout->message().contains(QStringLiteral("stopped responding"))) << layout->message().toStdString();
    EXPECT_EQ(layout->messageActions(), (QStringList{QStringLiteral("Restart"), QStringLiteral("Close")}));
    EXPECT_FALSE(row->isVisibleTo(layout->column()));
    EXPECT_FALSE(slider->isEnabled());
    QPushButton* restart = nullptr;
    for (QPushButton* button : layout->banner()->findChildren<QPushButton*>(QStringLiteral("anMessageAction")))
    {
        if (button->text() == QStringLiteral("Restart"))
        {
            restart = button;
        }
    }
    ASSERT_NE(restart, nullptr);
    EXPECT_TRUE(restart->isVisibleTo(layout->column()));

    // Restart starts it again: the failure goes and the body says the host starts.
    restart->click();
    settle();
    EXPECT_EQ(restarts, 1);
    EXPECT_TRUE(process->isStarting());
    EXPECT_TRUE(layout->messageActions().isEmpty());
    task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_NE(task->run.phase, an::ui::ProcessRunState::Phase::Failed);

    process->close();
    settle();
}

} // namespace

namespace
{

/// An extension's process the application draws, asking things of its view.
class HostedProcess : public HostedTasks
{
};

// For an extension drawn in a page, a display it asks for moves the page's canvas, as the page's looking actions do,
// and leaves the target's own view as it was; the shapes it draws lie on the canvas too, and the selection area and the
// history are the target's.
} // namespace

TEST_F(HostedProcess, DisplayRequestMovesTheCanvas)
{
    ImageViewControl* const view = openView(QStringLiteral("hosted-display"));
    ASSERT_NE(view, nullptr);
    QPointer<ExtensionHostedProcess> process = openHosted(view, false);
    ASSERT_NE(process, nullptr);
    const an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);
    ImageViewControl* const canvas = layout->previewView();
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(canvas, view) << "a mirror page's canvas is a view of the target's image";

    an::ui::extensions::ExtensionSessionController session;
    session.running_session.prepared_session.session_id = "hosted-display-session";
    session.running_session.state = an::ui::extensions::ExtensionHostSessionState::Active;
    session.target_control = view;
    session.hosted_process = process.data();
    EXPECT_EQ(session.requestView(), canvas);

    const float targetZoom = view->getCurrentZoom();
    an::ui::extensions::ViewDisplayMessage display;
    display.zoom_percent = 300.0;
    QCborMap answer;
    std::string reason;
    ASSERT_TRUE(an::ui::extensions::answerViewRequest(session, display, &answer, &reason)) << reason;
    settle();
    EXPECT_NEAR(canvas->getCurrentZoom(), 300.0, 0.01) << "the display did not move the canvas";
    EXPECT_NEAR(view->getCurrentZoom(), targetZoom, 0.01) << "the display moved the target's own view";
    EXPECT_NEAR(answer.value(QStringLiteral("zoom")).toDouble(), 300.0, 0.01);

    an::ui::extensions::ViewOverlayMessage overlay;
    an::ui::extensions::ViewOverlayShape line;
    line.points = {{1.0, 1.0}, {5.0, 5.0}};
    overlay.add = {line};
    ASSERT_TRUE(an::ui::extensions::answerViewRequest(session, overlay, &answer, &reason)) << reason;
    EXPECT_NE(canvas->ownedShapes("hosted-display-session"), nullptr) << "the shapes are not on the canvas";
    EXPECT_EQ(view->ownedShapes("hosted-display-session"), nullptr);

    session.releaseViewRequests();
    EXPECT_EQ(canvas->ownedShapes("hosted-display-session"), nullptr);
    session.hosted_process.clear();
    process->close();
    settle();
}

} // namespace an::tests::observatory
