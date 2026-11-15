// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where a process's run stands, as the framework states it for every single-image process and the shell's presenter
/// is told: an Apply runs and succeeds, a Cancel ends it cancelled, a failing body ends it failed with its message, an
/// Apply refused for a busy view starts nothing, and every state is told on the GUI thread. The presenter is told the
/// image the process targets as it is made and when it targets another.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::processes;
using Phase = an::ui::ProcessRunState::Phase;

/// A grey 8 by 6 Float32 target whose samples are `value`.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session, float value,
                                    const std::string& name = "target")
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    return makeTargetView(name, std::move(*document));
}

/// A context with a recording presenter lent, installed for the case.
struct PresentedContext
{
    PresentedContext() : context(openProcessContext())
    {
        if (context != nullptr)
        {
            context->presenter = &presenter;
            installed = std::make_unique<InstalledContext>(*context);
        }
    }
    ~PresentedContext()
    {
        installed.reset();
        if (context != nullptr)
        {
            context->presenter = nullptr;
        }
    }

    RecordingPresenter presenter;
    std::unique_ptr<ProcessTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

} // namespace

TEST(RunState, AnApplyGoesFromIdleToRunningToSucceeded)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    EXPECT_EQ(process->runState().phase, Phase::Idle) << "no run has been made";

    process->gate.close();
    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    EXPECT_EQ(process->runState().phase, Phase::Running);
    EXPECT_EQ(process->runState().text, an::ui::applyingRunText());
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_EQ(process->runState().phase, Phase::Succeeded);
    EXPECT_EQ(presented.presenter.phasesOf(process.get()), (std::vector<Phase>{Phase::Running, Phase::Succeeded}));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
}

TEST(RunState, ACancelledRunEndsCancelled)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.progressDialog = true});
    process->setParams(TestParams{.body = TestBody::WaitForStop});

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 1; }));
    ASSERT_NE(process->lastProgress, nullptr);
    process->lastProgress->requestCancel();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_EQ(process->runState().phase, Phase::Cancelled);
    EXPECT_EQ(presented.presenter.phasesOf(process.get()), (std::vector<Phase>{Phase::Running, Phase::Cancelled}));
}

TEST(RunState, AFailingBodyEndsFailedWithItsMessage)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->setParams(TestParams{.body = TestBody::Fail});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    EXPECT_EQ(process->runState().phase, Phase::Failed);
    EXPECT_TRUE(process->runState().text.contains(QStringLiteral("the test body failed on purpose")))
        << process->runState().text.toStdString();
    EXPECT_EQ(presented.presenter.phasesOf(process.get()), (std::vector<Phase>{Phase::Running, Phase::Failed}));
}

TEST(RunState, AnApplyRefusedForABusyViewStartsNothing)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(target->tryRequestLockForProcessing());

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    target->releaseLockForProcessing();

    EXPECT_EQ(process->runState().phase, Phase::Idle);
    EXPECT_TRUE(presented.presenter.phasesOf(process.get()).empty());
}

TEST(RunState, TheStateChangesOnTheGuiThread)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.progressDialog = true});

    process->applyProcess();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    ASSERT_FALSE(presented.presenter.states.empty());
    EXPECT_TRUE(presented.presenter.everythingToldOnTheGuiThread());

    // A state stated on a worker is handed to the GUI thread before anything is told of it.
    const std::size_t before = presented.presenter.states.size();
    std::thread worker([&process]() { process->setRunState(an::ui::runningState(QStringLiteral("from a worker"))); });
    worker.join();
    ASSERT_TRUE(pumpUntil([&]() { return presented.presenter.states.size() > before; }));
    EXPECT_EQ(process->runState().text, QStringLiteral("from a worker"));
    EXPECT_TRUE(presented.presenter.everythingToldOnTheGuiThread());
}

TEST(RunState, AReportOfProgressMovesOnlyARunOnItsWay)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});

    process->reportRunProgress(0.5);
    EXPECT_EQ(process->runState().phase, Phase::Idle) << "no run is on its way";
    EXPECT_FALSE(process->runState().progress.has_value());

    process->setRunState(an::ui::runningState());
    process->reportRunProgress(0.25, QStringLiteral("Frame 1 of 4"));
    EXPECT_EQ(process->runState().progress, std::optional<double>(0.25));
    EXPECT_EQ(process->runState().text, QStringLiteral("Frame 1 of 4"));
    process->reportRunProgress(0.5);
    EXPECT_EQ(process->runState().progress, std::optional<double>(0.5));
    EXPECT_EQ(process->runState().text, QStringLiteral("Frame 1 of 4")) << "an empty text keeps the last one";

    process->setRunState(an::ui::ProcessRunState{.phase = Phase::Succeeded});
    process->reportRunProgress(0.75);
    EXPECT_EQ(process->runState().phase, Phase::Succeeded) << "a report that lands late changes nothing";
    EXPECT_EQ(presented.presenter.statesOf(process.get()).size(), 4u) << "each change told once, the late one not";
}

TEST(ProcessTarget, ThePresenterIsToldTheTargetAsTheProcessIsMadeAndWhenItChanges)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle first = flatTarget(presented.context->engineSession(), 0.25f, "first");
    ViewHandle second = flatTarget(presented.context->engineSession(), 0.5f, "second");
    ProcessHandle process = openProcess(*first, TestProcessOptions{});
    EXPECT_EQ(process->processTarget(), first.get());
    ASSERT_FALSE(presented.presenter.targets.empty());
    EXPECT_EQ(presented.presenter.targets.back().target.data(), first.get());

    process->changeTargetViewControl(second.get());
    EXPECT_EQ(process->processTarget(), second.get());
    EXPECT_EQ(presented.presenter.targets.back().target.data(), second.get());
    EXPECT_TRUE(presented.presenter.everythingToldOnTheGuiThread());
}

TEST(ProcessTarget, AProcessMadeOutsideALaunchIsAWindowOfNoDescriptor)
{
    PresentedContext presented;
    ASSERT_NE(presented.context, nullptr);
    ViewHandle target = flatTarget(presented.context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});

    EXPECT_TRUE(process->descriptorId().empty());
    EXPECT_EQ(process->processSurface(), an::ui::ProcessSurface::Window);
    EXPECT_EQ(process->processColumnWidth(), an::ui::ProcessColumnWidth::Standard);
    EXPECT_EQ(process->processName(), "Test Scale");
    EXPECT_EQ(presented.presenter.adoptions, 0) << "no process is offered to a page yet";
}
