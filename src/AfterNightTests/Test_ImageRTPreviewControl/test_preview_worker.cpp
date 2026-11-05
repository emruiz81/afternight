// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where a real-time preview runs, against the Controls library: its body on a thread of the application's pool while
/// the GUI thread stays free, and its result published only when the GUI thread takes it; and the window's type
/// contracts, held at compile time.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/ImageRTPreviewControl.h"

#include <QThreadPool>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>

namespace
{

using namespace an::tests::processes;

/// How long, in milliseconds, a case waits for the application's pool to finish what it was given.
[[nodiscard]] int patienceMilliseconds()
{
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(kPatience).count());
}

} // namespace

TEST(ImageRTPreviewControlPreviewWorker, TypeContractsHold)
{
    static_assert(std::is_base_of_v<ImageProcessControl, ImageRTPreviewControl>,
                  "ImageRTPreviewControl should inherit ImageProcessControl");
    static_assert(std::is_abstract_v<ImageRTPreviewControl>, "ImageRTPreviewControl is expected to remain abstract");

    SUCCEED();
}

// A preview's body holds one of the application's pool threads while the GUI thread runs on, and lands only when taken.
TEST(ImageRTPreviewControlPreviewWorker, APreviewRunsOnTheApplicationsPoolAndIsPublishedOnTheGuiThread)
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
    ASSERT_TRUE(QThreadPool::globalInstance()->waitForDone(patienceMilliseconds())) << "nothing else runs on the pool";
    process->gate.close();

    process->previewCheckbox->setChecked(true);
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    // The GUI thread turns its event loop while the body waits at its gate.
    ASSERT_TRUE(pumpUntil([&process, preview, &target]() {
        return process->gate.arrived() == 1 && preview->isWorkIdle() && target->isWorkIdle();
    }));
    EXPECT_FALSE(QThreadPool::globalInstance()->waitForDone(50)) << "the body holds a thread of the application's pool";

    process->gate.open();
    ASSERT_TRUE(QThreadPool::globalInstance()->waitForDone(patienceMilliseconds()));
    EXPECT_EQ(process->runTelemetry().previewsCommitted, 0u) << "a finished preview waits for the GUI thread";
    EXPECT_EQ(process->ended, 0);
    EXPECT_NEAR(documentSample(preview->document(), 0u, 0u), 0.25, 1e-6);

    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runTelemetry().previewsCommitted, 1u);
    EXPECT_EQ(process->ended, 1);
    EXPECT_NEAR(documentSample(preview->document(), 0u, 0u), 0.5, 1e-6);
}
