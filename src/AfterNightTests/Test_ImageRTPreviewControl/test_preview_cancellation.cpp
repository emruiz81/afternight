// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a real-time preview runs beside the window's other runs, against the Controls library: a preview is admitted as
/// the session's preview work and stopped when the preview goes off, and an Apply as ordinary work outside the share
/// the preview reserves.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace
{

using namespace an::tests::processes;

/// A grey 8 by 6 Float32 target, every sample a quarter; null when it cannot be made.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("target", std::move(*document));
}

} // namespace

// A preview runs as the session's preview work until it is turned off, and an Apply as work outside the preview share.
TEST(ImageRTPreviewControlPreviewCancellation, APreviewRunsInThePreviewShareAndAnApplyOutsideIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = flatTarget(session);
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(settleView(*target));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->gate.close();

    process->previewCheckbox->setChecked(true);
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    // The views' own presentations are preview work too: the preview's body is held until they have landed.
    ASSERT_TRUE(pumpUntil([&process, preview, &target]() {
        return process->gate.arrived() == 1 && preview->isWorkIdle() && target->isWorkIdle();
    }));
    EXPECT_GT(session.admission().previewThreads, 0u) << "the preview's body runs in the preview share";

    process->previewCheckbox->setChecked(false);
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runTelemetry().previewsDiscarded, 1u) << "the preview turned off saw its stop";
    EXPECT_EQ(session.admission().previewThreads, 0u);

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process, &target]() {
        return process->runsOf(an::ui::ProcessRunKind::Apply) == 1 && target->getViewWidget()->isPresentationIdle();
    }));
    const an::consumer::AdmissionSnapshot applying = session.admission();
    EXPECT_GT(applying.otherThreads, 0u);
    EXPECT_EQ(applying.previewThreads, 0u) << "an Apply takes nothing of the preview share";

    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
}
