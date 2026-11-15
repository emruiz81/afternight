// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a process window knows of how it is presented, and what it tells the shell's presenter: every descriptor is a
/// window with a standard column until it states otherwise; a window made inside a launch carries its descriptor's id,
/// guide page, surface and column width, one made outside carries none, and no launch leaves its scope behind; and the
/// runs of a process that runs through the geometry support and of a top-level process state where they stand, on the
/// GUI thread.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"
#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ProcessLauncher.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPresentation.h"
#include "processes/BackgroundNeutralizationProcess.h"
#include "processes/ChannelCompositionProcess.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;
using an::ui::ProcessColumnWidth;
using an::ui::ProcessLaunchScope;
using an::ui::ProcessSurface;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessRegistry;
using Phase = an::ui::ProcessRunState::Phase;

constexpr char kGaussianBlur[] = "filters.actionGaussian_Blur";

/// A dark sky whose channels sit at different levels, inside Background Neutralization's default range.
float sky(std::uint32_t x, std::uint32_t, std::uint32_t channel)
{
    return 0.02f * static_cast<float>(channel + 1u) + 0.0005f * static_cast<float>(x);
}

/// Lends the suite's context a recording presenter for the case, and takes it back as the case ends.
class LentPresenter
{
public:
    LentPresenter()
    {
        suiteContext().presenter = &presenter;
    }
    ~LentPresenter()
    {
        suiteContext().presenter = nullptr;
    }
    LentPresenter(const LentPresenter&) = delete;
    LentPresenter& operator=(const LentPresenter&) = delete;

    RecordingPresenter presenter;
};

/// The process windows of a class the cases opened and the launcher showed.
[[nodiscard]] TopLevelProcessBaseControl* onlyOpened(const std::string& className)
{
    const std::vector<shared::OpenedProcessWindow> opened = shared::openedProcessWindows(className);
    EXPECT_EQ(opened.size(), 1u) << className;
    return opened.size() == 1u ? opened.front().window : nullptr;
}

} // namespace

// Every process is a window until its descriptor says otherwise, and the processes library registers every one of its
// own as a window with the standard column.
TEST(ProcessDescriptor, SurfaceDefaultsToWindow)
{
    const ProcessDescriptor made;
    EXPECT_EQ(made.surface, ProcessSurface::Window);
    EXPECT_EQ(made.columnWidth, ProcessColumnWidth::Standard);
    ASSERT_FALSE(ProcessRegistry::instance().all().empty());
    for (const ProcessDescriptor& descriptor : ProcessRegistry::instance().all())
    {
        EXPECT_EQ(descriptor.surface, ProcessSurface::Window) << descriptor.id;
        EXPECT_EQ(descriptor.columnWidth, ProcessColumnWidth::Standard) << descriptor.id;
    }
}

// A window a launch makes carries what its descriptor states: the id it was launched as, its guide page, its surface
// and its column width.
TEST(ProcessLaunchScope, AnInstanceMadeInsideALaunchCarriesItsDescriptor)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closes;
    ProcessDescriptor* const descriptor = ProcessRegistry::instance().findMutable(kGaussianBlur);
    ASSERT_NE(descriptor, nullptr);
    descriptor->surface = ProcessSurface::PreviewPage;
    descriptor->columnWidth = ProcessColumnWidth::Wide;
    const std::unique_ptr<ContextView> view =
        floatView("target", 16u, 12u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.2f; });
    ASSERT_TRUE(view != nullptr && *view);

    an::ui::ProcessLauncher(kGaussianBlur).executeGaussianBlurFilter();
    TopLevelProcessBaseControl* const window = onlyOpened("GaussianBlurProcess");
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->descriptorId(), kGaussianBlur);
    EXPECT_EQ(window->helpPath(), QString::fromStdString(descriptor->helpPath));
    EXPECT_FALSE(window->helpPath().isEmpty());
    EXPECT_EQ(window->processSurface(), ProcessSurface::PreviewPage);
    EXPECT_EQ(window->processColumnWidth(), ProcessColumnWidth::Wide);
    EXPECT_EQ(window->processTarget(), view->get());
    EXPECT_EQ(ProcessLaunchScope::current(), nullptr) << "the launch's scope ended with it";
}

// A window made outside every launch - opened over a view as a case opens one, or by a launcher acting for no
// descriptor - belongs to none and is a window.
TEST(ProcessLaunchScope, AnInstanceMadeOutsideALaunchCarriesNone)
{
    const shared::ClosesOpenedWindows closes;
    const std::unique_ptr<ContextView> view = floatView("target", 16u, 12u, Channels::Rgb, sky);
    ASSERT_TRUE(view != nullptr && *view);

    WindowHandle<BackgroundNeutralizationProcess> made = openOver<BackgroundNeutralizationProcess>(**view);
    EXPECT_TRUE(made->descriptorId().empty());
    EXPECT_TRUE(made->helpPath().isEmpty());
    EXPECT_EQ(made->processSurface(), ProcessSurface::Window);
    EXPECT_EQ(made->processColumnWidth(), ProcessColumnWidth::Standard);

    an::ui::ProcessLauncher().executeImageRegistration();
    TopLevelProcessBaseControl* const launched = onlyOpened("ImageRegistrationProcess");
    ASSERT_NE(launched, nullptr);
    EXPECT_TRUE(launched->descriptorId().empty());
    EXPECT_EQ(launched->processSurface(), ProcessSurface::Window);
    EXPECT_EQ(launched->processTarget(), nullptr) << "a top-level process has no single target";
}

// A launch its guards refuse makes no window and leaves no scope open: a window made afterwards belongs to no
// descriptor. Scopes nest, the innermost naming the launch, and each ends with its block.
TEST(ProcessLaunchScope, ScopesDoNotLeakAfterALaunchAGuardRefused)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closes;
    ProcessTestContext& context = suiteContext();
    context.setActiveImageViewControl(nullptr);
    const int messages = static_cast<int>(context.messages.size());

    an::ui::ProcessLauncher(kGaussianBlur).executeGaussianBlurFilter();
    EXPECT_TRUE(shared::openedProcessWindows("GaussianBlurProcess").empty());
    EXPECT_GT(static_cast<int>(context.messages.size()), messages) << "the refusal was said";
    EXPECT_EQ(ProcessLaunchScope::current(), nullptr);

    const std::unique_ptr<ContextView> view = floatView("target", 16u, 12u, Channels::Rgb, sky);
    ASSERT_TRUE(view != nullptr && *view);
    WindowHandle<BackgroundNeutralizationProcess> made = openOver<BackgroundNeutralizationProcess>(**view);
    EXPECT_TRUE(made->descriptorId().empty());

    const ProcessDescriptor* const outer = shared::registered(kGaussianBlur);
    const ProcessDescriptor* const inner = shared::registered("multi_image_stacking.actionImage_Stacking");
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);
    {
        const ProcessLaunchScope outerScope(*outer);
        {
            const ProcessLaunchScope innerScope(*inner);
            ASSERT_NE(ProcessLaunchScope::current(), nullptr);
            EXPECT_EQ(ProcessLaunchScope::current()->descriptorId, inner->id);
        }
        ASSERT_NE(ProcessLaunchScope::current(), nullptr);
        EXPECT_EQ(ProcessLaunchScope::current()->descriptorId, outer->id);
    }
    EXPECT_EQ(ProcessLaunchScope::current(), nullptr);
}

// A process that runs through the geometry support states its run: running, then succeeded once its result is shown,
// each told on the GUI thread.
TEST(RunState, AGeometryRunGoesFromRunningToSucceeded)
{
    LentPresenter lent;
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    const an::ui::DocumentRevision before = (*target)->document().revision();

    WindowHandle<BackgroundNeutralizationProcess> window = openOver<BackgroundNeutralizationProcess>(**target);
    EXPECT_EQ(window->runState().phase, Phase::Idle);
    auto* box = widgetNamed<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(box, nullptr);
    box->button(QDialogButtonBox::Ok)->click();
    ASSERT_TRUE(settle(*window, {target->get()}));
    ASSERT_NE((*target)->document().revision(), before) << "the run's result was shown";

    EXPECT_EQ(window->runState().phase, Phase::Succeeded);
    EXPECT_EQ(lent.presenter.phasesOf(window.get()), (std::vector<Phase>{Phase::Running, Phase::Succeeded}));
    EXPECT_TRUE(lent.presenter.everythingToldOnTheGuiThread());
}

// A top-level process states its own run: Channel Composition composing three images runs, then succeeds.
TEST(RunState, ATopLevelProcessStatesItsOwnRun)
{
    LentPresenter lent;
    const auto plane = [](float level) {
        return [level](std::uint32_t x, std::uint32_t, std::uint32_t) { return level + 0.01f * static_cast<float>(x); };
    };
    const std::unique_ptr<ContextView> red = floatView("red", 10u, 6u, Channels::Grey, plane(0.1f));
    const std::unique_ptr<ContextView> green = floatView("green", 10u, 6u, Channels::Grey, plane(0.2f));
    const std::unique_ptr<ContextView> blue = floatView("blue", 10u, 6u, Channels::Grey, plane(0.3f));
    ASSERT_TRUE(red != nullptr && green != nullptr && blue != nullptr);
    const std::size_t made = suiteContext().madeViews.size();

    WindowHandle<ChannelCompositionProcess> window(new ChannelCompositionProcess(nullptr));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    EXPECT_EQ(window->processTarget(), nullptr);
    chooseText(*window, "redChannel", QStringLiteral("red"));
    chooseText(*window, "greenChannel", QStringLiteral("green"));
    chooseText(*window, "blueChannel", QStringLiteral("blue"));
    if (auto* linearMatch = widgetNamed<QCheckBox>(*window, "linearMatchCheckBox"))
    {
        linearMatch->setChecked(false);
    }
    chooseText(*window, "colorSpace", QStringLiteral("RGB"));

    window->executeChannelComposition();
    EXPECT_EQ(window->runState().phase, Phase::Running);
    ASSERT_TRUE(pumpUntil([&window]() { return !window->isRunning(); }));
    pumpQuietly();
    ASSERT_EQ(window->runsPublished(), 1u) << suiteContext().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(suiteContext().madeViews.size(), made + 1u);

    EXPECT_EQ(window->runState().phase, Phase::Succeeded);
    EXPECT_EQ(lent.presenter.phasesOf(window.get()), (std::vector<Phase>{Phase::Running, Phase::Succeeded}));
    EXPECT_TRUE(lent.presenter.everythingToldOnTheGuiThread());
}
