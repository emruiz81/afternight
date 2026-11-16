// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process applied in a page runs with no progress window of its own: over a sequence the page shows the run's two
/// bars, Overall and Processing, in the banner's place, and its Cancel stops the run with nothing published; over an
/// image it shows the one bar. Median Filter is such a process: in a window of its own it opens a progress window for
/// every run.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"

#include "app/FileCompanions.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ProcessPresentation.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace an::tests::processes::shared
{
namespace
{

using Phase = an::ui::ProcessRunState::Phase;

/// A process whose window opens a progress window of its own for every run.
constexpr char kMedianFilter[] = "filters.actionMedian_Filter";

/// A sequence view of a three-frame grey SER written below a directory, made by the context's view factory as the
/// application makes one.
[[nodiscard]] ImageViewControl* sequenceView(ProcessTestContext& context, const QString& path)
{
    an::tests::viewer::writeSerFile(
        context.engineSession(), path, 24u, 16u, 3u, [](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
            return (0.05f + 0.01f * static_cast<float>(x + y)) * (1.0f + 0.5f * static_cast<float>(frame));
        });
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context.engineSession(), path, an::SequenceFormat::Ser, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    ImageViewControl* view = context.createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, false, "frames", ViewControlMode::standard, nullptr, false, false);
    EXPECT_NE(view, nullptr);
    if (view != nullptr)
    {
        EXPECT_TRUE(settleView(*view));
    }
    return view;
}

/// The process a presenter took into a page, launched over a target as the menu launches it; null when none was.
[[nodiscard]] ImageRTPreviewControl* launchPage(const std::string& descriptorId, ImageViewControl& target,
                                                const PagePresenter& presenter)
{
    const an::ui::process_registry::ProcessDescriptor* descriptor = registered(descriptorId);
    EXPECT_NE(descriptor, nullptr) << descriptorId;
    if (descriptor == nullptr || !descriptor->launch_single_image)
    {
        return nullptr;
    }
    descriptor->launch_single_image(&target);
    pumpQuietly();
    for (const QPointer<QWidget>& host : presenter.hosts)
    {
        if (host.isNull())
        {
            continue;
        }
        for (QWidget* widget : host->findChildren<QWidget*>())
        {
            if (auto* process = dynamic_cast<ImageRTPreviewControl*>(widget); process != nullptr)
            {
                return process;
            }
        }
    }
    return nullptr;
}

} // namespace

// Applied to a sequence in a page, the run shows two bars in the banner's place - Overall and Processing - and Cancel,
// and no progress window opens; Cancel stops the run, and the sequence the view shows is the one it was opened on.
TEST(SequenceApply, PageShowsTwoBarsAndCancels)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString recording = directory.filePath(QStringLiteral("frames.ser"));
    ImageViewControl* const target = sequenceView(*context, recording);
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->isSequence());
    context->setActiveImageViewControl(target);
    PagePresenter presenter;
    context->presenter = &presenter;
    const QPointer<ImageRTPreviewControl> process = launchPage(kMedianFilter, *target, presenter);
    ASSERT_NE(process, nullptr) << "Median Filter is a page";
    an::ui::ProcessPageLayout* const page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(pumpUntil([&process]() { return process->isProcessIdle(); }));
    EXPECT_EQ(page->runBars(), 0);

    const ProgressWindowsShown windows;
    {
        PoolHold hold;
        ASSERT_TRUE(hold.held());
        process->applyProcess();
        pumpQuietly();
        EXPECT_TRUE(process->runState().running());
        EXPECT_TRUE(process->runState().sequence);
        EXPECT_TRUE(page->runBanner()->isVisible()) << "the run's row in the banner's place";
        EXPECT_EQ(page->runBars(), 2) << "Overall and Processing";
        ASSERT_TRUE(page->cancelButton()->isVisible());
        ASSERT_TRUE(page->cancelButton()->isEnabled());
        page->cancelButton()->click();
    }
    ASSERT_TRUE(pumpUntil([&process]() { return process->isProcessIdle(); }));
    ASSERT_TRUE(settleView(*target));

    EXPECT_EQ(windows.count(), 0) << "no progress window in a page";
    EXPECT_EQ(process->runState().phase, Phase::Cancelled);
    EXPECT_EQ(page->runBars(), 0);
    ASSERT_TRUE(target->document().sequence().has_value());
    EXPECT_EQ(QFileInfo(target->document().sequence()->sourcePath).canonicalFilePath(),
              QFileInfo(recording).canonicalFilePath())
        << "nothing published";
    EXPECT_EQ(context->errorCount(), 0);

    process->close();
    pumpQuietly();
    context->presenter = nullptr;
}

// Applied to an image in a page, the run shows its one bar and opens no progress window either, and the image takes
// the step.
TEST(SequenceApply, APageAppliedToAnImageOpensNoProgressWindowEither)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 64u, 48u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("image", std::move(*document));
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());
    PagePresenter presenter;
    context->presenter = &presenter;
    const QPointer<ImageRTPreviewControl> process = launchPage(kMedianFilter, *target, presenter);
    ASSERT_NE(process, nullptr);
    an::ui::ProcessPageLayout* const page = process->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(pumpUntil([&process]() { return process->isProcessIdle(); }));
    const std::size_t steps = target->viewHistory() != nullptr ? target->viewHistory()->steps().current : 0u;

    const ProgressWindowsShown windows;
    {
        PoolHold hold;
        ASSERT_TRUE(hold.held());
        process->applyProcess();
        pumpQuietly();
        EXPECT_TRUE(process->runState().running());
        EXPECT_FALSE(process->runState().sequence);
        EXPECT_EQ(page->runBars(), 1);
    }
    ASSERT_TRUE(pumpUntil([&process]() { return process.isNull() || process->isProcessIdle(); }));
    ASSERT_TRUE(settleView(*target));

    EXPECT_EQ(windows.count(), 0) << "no progress window in a page";
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_EQ(target->viewHistory()->steps().current, steps + 1u) << "the image took the step";

    if (!process.isNull())
    {
        process->close();
    }
    pumpQuietly();
    context->presenter = nullptr;
    context->removeViewControl(target.get(), false);
}

} // namespace an::tests::processes::shared
