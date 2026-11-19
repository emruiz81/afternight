// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An extension's process the application draws is a task like a built-in process's: a preview page, or a mirror page
/// for one that does not preview, its header naming it with the quiet "extension" tag, its column the panel of its
/// parameters, floated to a window of its own and attached again like any other.

#include "Test_ShellObservatory/ObservatoryWindowCase.h"

#include "app/ProcessRegistry.h"
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
#include <QSlider>

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

} // namespace

} // namespace an::tests::observatory
