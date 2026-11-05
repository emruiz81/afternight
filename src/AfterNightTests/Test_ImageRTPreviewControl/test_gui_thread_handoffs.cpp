// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What crosses to the GUI thread around a real-time preview, run against the Controls library: a channel chosen on the
/// preview's selector reaches the view's listeners on the GUI thread as the mode its item carries, whatever the item is
/// labelled; and a refresh of the preview view asked for on a worker returns at once and reaches the view's listeners
/// on the GUI thread when its event loop turns.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/ImageViewControlListener.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QThread>
#include <QVariant>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace
{

using namespace an::tests::processes;

/// The channel selector on a view's toolbar.
[[nodiscard]] QComboBox* channelSelectorOf(const ImageViewControl& view)
{
    for (QComboBox* box : view.findChildren<QComboBox*>())
    {
        if (box->accessibleName() == QStringLiteral("View Channel"))
        {
            return box;
        }
    }
    return nullptr;
}

/// Keeps what a view tells its listeners - the channels chosen and the updates - and the thread each was told on. The
/// listener subscribes to the view as it is made and unsubscribes as it goes.
class ToldOnThread final : public ImageViewControlListener
{
public:
    explicit ToldOnThread(ImageViewControl* view) : ImageViewControlListener(view) {}

    ToldOnThread(const ToldOnThread&) = delete;
    ToldOnThread& operator=(const ToldOnThread&) = delete;

    void onImageDisplayChannelChanged(ImageViewControl*, int mode) override
    {
        const std::lock_guard lock(m_mutex);
        m_channels.push_back(mode);
        m_threads.push_back(QThread::currentThread());
    }

    void onImageUpdated(ImageViewControl*) override
    {
        const std::lock_guard lock(m_mutex);
        ++m_updates;
        m_threads.push_back(QThread::currentThread());
    }

    /// The channels chosen, as the modes the view told.
    [[nodiscard]] std::vector<int> channels()
    {
        const std::lock_guard lock(m_mutex);
        return m_channels;
    }

    /// How many updates the view told.
    [[nodiscard]] int updates()
    {
        const std::lock_guard lock(m_mutex);
        return m_updates;
    }

    /// Whether everything was told on a thread.
    [[nodiscard]] bool allToldOn(const QThread* thread)
    {
        const std::lock_guard lock(m_mutex);
        for (const QThread* told : m_threads)
        {
            if (told != thread)
            {
                return false;
            }
        }
        return true;
    }

private:
    std::mutex m_mutex;
    std::vector<int> m_channels;
    int m_updates = 0;
    std::vector<const QThread*> m_threads;
};

} // namespace

// A channel chosen on the preview's selector reaches its listeners on the GUI thread as its item's mode, not its label.
TEST(ImageRTPreviewControlGuiThreadHandoffs, AChannelChosenOnThePreviewsSelectorReachesItsListenersAsTheItemsMode)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context->engineSession(), 8u, 6u, Channels::Rgb,
        [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.1f * static_cast<float>(c + 1u); });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("colour", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    QComboBox* selector = channelSelectorOf(*preview);
    ASSERT_NE(selector, nullptr);
    const int red = selector->findData(QVariant(static_cast<int>(ImageCompositionMode::r_mode)));
    ASSERT_GE(red, 0);
    ASSERT_NE(selector->currentIndex(), red);

    ToldOnThread listener(preview);
    selector->setItemText(red, QStringLiteral("renamed"));
    selector->setCurrentIndex(red);
    EXPECT_EQ(listener.channels(), std::vector<int>{static_cast<int>(ImageCompositionMode::r_mode)});
    EXPECT_TRUE(listener.allToldOn(QCoreApplication::instance()->thread()));
    EXPECT_EQ(preview->displayedComposition(), ImageCompositionMode::r_mode);
    ASSERT_TRUE(settleProcess(*process));
}

// The preview view's refresh asked for on a worker returns at once and reaches its listeners on the GUI thread later.
TEST(ImageRTPreviewControlGuiThreadHandoffs, APreviewRefreshAskedForOnAWorkerIsQueuedToTheGuiThreadAndNeverWaitedFor)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);

    ToldOnThread listener(preview);
    std::future<void> asked = std::async(std::launch::async, [preview]() { preview->notifyImageUpdated(true); });
    EXPECT_EQ(asked.wait_for(kPatience), std::future_status::ready) << "the worker never waits for the GUI thread";
    EXPECT_EQ(listener.updates(), 0) << "the listeners hear of it only when the GUI thread takes it";
    EXPECT_TRUE(pumpUntil([&asked, &listener]() {
        return asked.wait_for(std::chrono::seconds(0)) == std::future_status::ready && listener.updates() == 1;
    }));
    EXPECT_TRUE(listener.allToldOn(QCoreApplication::instance()->thread()));
    ASSERT_TRUE(settleProcess(*process));
}
