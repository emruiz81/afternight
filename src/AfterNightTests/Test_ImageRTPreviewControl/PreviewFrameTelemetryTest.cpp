// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a real-time preview window's telemetry says of its frames, against the Controls library: a preview a change
/// asks for with no slider held is counted as the settled kind of frame and none as the interactive kind, and the time
/// from a preview's start to its result being handed to the preview view is kept in all and at the longest, for
/// previews shown only.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/ImageRTPreviewControl.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

namespace
{

using namespace an::tests::processes;
using namespace std::chrono_literals;

/// How long a case holds a preview's body at its gate.
constexpr std::chrono::milliseconds kHeld{40};

} // namespace

TEST(PreviewFrameTelemetry, APreviewOfAChangeWithNoSliderHeldIsASettledFrameAndHowLongItTookIsKept)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = greyDocument(
        context->engineSession(), 16u, 12u, [](std::uint32_t x, std::uint32_t) { return 0.1f + 0.01f * x; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ASSERT_TRUE(settleView(*target));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    const ProcessRunTelemetry before = process->runTelemetry();
    EXPECT_EQ(before.settledPreviewsCommitted, 0u);
    EXPECT_EQ(before.previewTime, std::chrono::nanoseconds::zero());
    EXPECT_EQ(before.longestPreview, std::chrono::nanoseconds::zero());

    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleProcess(*process));
    const ProcessRunTelemetry opened = process->runTelemetry();
    ASSERT_GE(opened.previewsCommitted, 1u);
    EXPECT_EQ(opened.settledPreviewsCommitted, opened.previewsCommitted);
    EXPECT_GT(opened.longestPreview, std::chrono::nanoseconds::zero());

    // A preview whose body is held at its gate takes at least as long as it was held.
    process->gate.close();
    const int arrived = process->gate.arrived();
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(pumpUntil([&process, arrived]() { return process->gate.arrived() > arrived; }));
    std::this_thread::sleep_for(kHeld);
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));

    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_GT(telemetry.previewsCommitted, opened.previewsCommitted);
    EXPECT_EQ(telemetry.settledPreviewsCommitted, telemetry.previewsCommitted)
        << "every change came with no slider held";
    EXPECT_EQ(telemetry.interactivePreviewsCommitted, 0u) << "no slider was held";
    EXPECT_GE(telemetry.longestPreview, kHeld) << "the held preview is the longest";
    EXPECT_GE(telemetry.previewTime, telemetry.longestPreview + opened.previewTime)
        << "the time in all holds every preview shown";
}

// A preview that comes back no longer current is not shown, and its time is not a shown frame's.
TEST(PreviewFrameTelemetry, APreviewDiscardedUnshownAddsNoFrameTime)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ASSERT_TRUE(settleView(*target));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleProcess(*process));
    const ProcessRunTelemetry shown = process->runTelemetry();

    // Turning the preview off while a preview waits at its gate refuses it when it lands.
    process->gate.close();
    const int arrived = process->gate.arrived();
    process->setParams(TestParams{.factor = 4.0});
    ASSERT_TRUE(pumpUntil([&process, arrived]() { return process->gate.arrived() > arrived; }));
    std::this_thread::sleep_for(kHeld);
    process->previewCheckbox->setChecked(false);
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));

    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_GT(telemetry.previewsDiscarded, shown.previewsDiscarded);
    EXPECT_EQ(telemetry.settledPreviewsCommitted, shown.settledPreviewsCommitted);
    EXPECT_EQ(telemetry.previewTime, shown.previewTime);
    EXPECT_EQ(telemetry.longestPreview, shown.longestPreview);
}
