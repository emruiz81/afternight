// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A typed process's parameters on their way to its body: an Apply runs with the value the window held when Apply was
/// pressed, whatever the window holds by the time the body reads it, and its result is that value's.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"

#include "controls/TypedProcessControl.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

namespace
{

using namespace an::tests::processes;
using framework::greySuiteView;
using framework::SuiteView;

} // namespace

// The test process is a typed process over its own parameter type, as every process with execution parameters is.
static_assert(std::is_base_of_v<an::ui::TypedProcessControl<TestParams>, TestProcess>);

// An Apply's body sees the parameters the window held when Apply was pressed: parameters changed while the run waits
// reach no body of that run, and the result the history records is that Apply's.
TEST(TypedProcessControlTest, AnApplyRunsWithTheParametersHeldWhenItWasPressed)
{
    SuiteView target = greySuiteView("typed target", 8u, 6u, 0.25f);
    ASSERT_NE(target, nullptr);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->setParams(TestParams{.body = TestBody::Scale, .factor = 3.0, .modulation = 1.0, .generation = 1});
    process->gate.close();

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    process->setParams(TestParams{.body = TestBody::Scale, .factor = 5.0, .modulation = 1.0, .generation = 2});
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));

    const std::vector<TestRun> runs = process->runs();
    ASSERT_GE(runs.size(), 1u);
    EXPECT_EQ(runs.front().kind, an::ui::ProcessRunKind::Apply);
    EXPECT_EQ(runs.front().params.generation, 1);
    EXPECT_DOUBLE_EQ(runs.front().params.factor, 3.0);
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Apply), 1);
    EXPECT_NEAR(documentSample(target->document(), 2u, 3u), 0.75, 1e-6);

    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->previousStepName(), "Test Scale");
}
