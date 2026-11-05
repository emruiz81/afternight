// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the asynchronous preview probe is asked, and why each question is one a person dragging a
// slider asks.
//
// Five questions: is the preview on screen the one for the parameters last chosen; does the progress
// bar belong to what is running and only move forwards; does closing the dialog stop the work and let
// the engine go; does a preview leave the rest of the machine to a batch that is already running; and
// does an image the dialog stopped previewing live only as long as the preview that was using it.

#include "PreviewProbe.h"
#include "ProbeSupport.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;

/// How long a case waits for something the worker must do before it fails rather than hangs.
constexpr std::chrono::seconds kPatience{120};

/// What the view was told, in order.
struct ViewEvent
{
    enum class Kind : std::uint8_t
    {
        Progress,
        Ready,
        Abandoned
    };

    Kind kind = Kind::Progress;
    std::uint64_t request = 0u;
    std::uint64_t completed = 0u;
    std::uint64_t total = 0u;
    an::ErrorCode code = an::ErrorCode::Ok;
    std::uint32_t resultWidth = 0u;
};

/// A view that records every call, and lets a case wait for one.
class RecordingView final : public probe::preview::PreviewView
{
public:
    void onPreviewProgress(std::uint64_t request, std::uint64_t completed, std::uint64_t total) noexcept override
    {
        record(
            ViewEvent{.kind = ViewEvent::Kind::Progress, .request = request, .completed = completed, .total = total});
    }

    void onPreviewReady(std::uint64_t request, std::shared_ptr<const an::Image<float>> result) noexcept override
    {
        const std::optional<an::ImageDescriptor> descriptor = result->descriptor();
        record(ViewEvent{.kind = ViewEvent::Kind::Ready,
                         .request = request,
                         .resultWidth = descriptor.has_value() ? descriptor->extent().width() : 0u});
    }

    void onPreviewAbandoned(std::uint64_t request, an::ErrorCode code) noexcept override
    {
        record(ViewEvent{.kind = ViewEvent::Kind::Abandoned, .request = request, .code = code});
    }

    /// Waits until some request has ended, ready or abandoned.
    ///
    /// @param request the request.
    /// @return the ending event, or an empty optional when the patience ran out.
    [[nodiscard]] std::optional<ViewEvent> waitForEnd(std::uint64_t request)
    {
        std::unique_lock lock(m_mutex);
        std::optional<ViewEvent> ending;
        m_changed.wait_for(lock, kPatience, [&] {
            for (const ViewEvent& event : m_events)
            {
                if (event.request == request && event.kind != ViewEvent::Kind::Progress)
                {
                    ending = event;
                    return true;
                }
            }
            return false;
        });
        return ending;
    }

    /// Every event so far.
    [[nodiscard]] std::vector<ViewEvent> events() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_events;
    }

private:
    void record(const ViewEvent& event) noexcept
    {
        try
        {
            const std::scoped_lock lock(m_mutex);
            m_events.push_back(event);
        }
        catch (...)
        {
        }
        m_changed.notify_all();
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<ViewEvent> m_events;
};

/// A noisy normalized frame to preview.
///
/// @param engine the context its memory comes from.
/// @param side its width and height.
/// @return the frame, shared the way a dialog shares the image it previews.
[[nodiscard]] std::shared_ptr<const an::Image<float>> noisyFrame(const an::EngineContext& engine, std::uint32_t side)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(side, side, 1u, engine.limits());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    an::Result<an::Image<float>> image = an::Image<float>::create(engine, *descriptor, an::StorageArrangement::Planar,
                                                                  an::StorageInitialization::Zeroed);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    {
        const std::optional<an::ImageWriteAccess<float>> access = image->write();
        const std::optional<an::PlaneView<float>> plane = access->view().plane(0u);
        std::uint64_t state = 0x9E3779B97F4A7C15ull;
        for (std::uint32_t y = 0u; y < side; ++y)
        {
            const std::optional<std::span<float>> row = plane->row(y);
            for (std::uint32_t x = 0u; x < side; ++x)
            {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                const double noise = (static_cast<double>(state >> 11u) / static_cast<double>(1ull << 53u)) - 0.5;
                const double dx = static_cast<double>(x) - 0.5 * side;
                const double dy = static_cast<double>(y) - 0.5 * side;
                (*row)[x] = static_cast<float>(0.2 + 0.5 * std::exp(-(dx * dx + dy * dy) / 18.0) + 0.08 * noise);
            }
        }
    }
    return std::make_shared<const an::Image<float>>(std::move(*image));
}

/// Waits until the session reports a request running.
///
/// @param session the session.
/// @param request the request.
/// @return true when it was seen running before the patience ran out.
[[nodiscard]] bool waitUntilRunning(const probe::preview::PreviewSession& session, std::uint64_t request)
{
    const auto deadline = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (session.running() == request)
        {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

/// Parameters slow enough that a case can act while the preview is still running.
constexpr probe::preview::PreviewParameters kSlow{.sigma = 0.03, .iterations = 4u};

/// Parameters for a quick preview.
constexpr probe::preview::PreviewParameters kQuick{.sigma = 0.02, .iterations = 1u};

} // namespace

TEST(PreviewProbeTest, APreviewThatIsOvertakenIsStoppedAndNeverShown)
{
    an::Result<an::EngineContext> engine = probe::createProbeEngine(4u);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const std::shared_ptr<const an::Image<float>> source = noisyFrame(*engine, 192u);
    RecordingView view;
    an::Result<std::unique_ptr<probe::preview::PreviewSession>> session = probe::preview::PreviewSession::create(
        *engine, source, view, probe::preview::PreviewOptions{.maxConcurrency = 2u, .regionSize = 128u});
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    const std::uint64_t first = (*session)->request(kSlow);
    ASSERT_TRUE(waitUntilRunning(**session, first));
    const std::uint64_t second = (*session)->request(kQuick);

    const std::optional<ViewEvent> firstEnd = view.waitForEnd(first);
    const std::optional<ViewEvent> secondEnd = view.waitForEnd(second);
    ASSERT_TRUE(firstEnd.has_value());
    ASSERT_TRUE(secondEnd.has_value());

    // The overtaken preview ended without a result the view may show - stopped, or finished and
    // found stale - and the newest one was shown.
    EXPECT_EQ(firstEnd->kind, ViewEvent::Kind::Abandoned);
    EXPECT_EQ(firstEnd->code, an::ErrorCode::Cancelled);
    EXPECT_EQ(secondEnd->kind, ViewEvent::Kind::Ready);
    for (const ViewEvent& event : view.events())
    {
        EXPECT_FALSE(event.kind == ViewEvent::Kind::Ready && event.request == first);
    }
}

TEST(PreviewProbeTest, TheProgressBarBelongsToWhatIsRunningAndOnlyMovesForwards)
{
    an::Result<an::EngineContext> engine = probe::createProbeEngine(4u);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const std::shared_ptr<const an::Image<float>> source = noisyFrame(*engine, 128u);
    RecordingView view;
    an::Result<std::unique_ptr<probe::preview::PreviewSession>> session = probe::preview::PreviewSession::create(
        *engine, source, view, probe::preview::PreviewOptions{.maxConcurrency = 2u, .regionSize = 96u});
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    const std::uint64_t first = (*session)->request(kSlow);
    ASSERT_TRUE(waitUntilRunning(**session, first));
    const std::uint64_t second = (*session)->request(kQuick);
    ASSERT_TRUE(view.waitForEnd(first).has_value());
    ASSERT_TRUE(view.waitForEnd(second).has_value());

    // The preview reports its own progress: a denoise of a region is the slow half of a dialog, and a
    // spinner says nothing about how long it will take.
    const std::vector<ViewEvent> events = view.events();
    std::uint64_t secondReports = 0u;
    std::uint64_t lastCompleted = 0u;
    std::uint64_t lastTotal = 0u;
    std::size_t lastFirstEvent = 0u;
    std::size_t firstSecondEvent = events.size();
    for (std::size_t index = 0u; index < events.size(); ++index)
    {
        const ViewEvent& event = events[index];
        if (event.request == first)
        {
            lastFirstEvent = index;
        }
        if (event.request == second && firstSecondEvent == events.size())
        {
            firstSecondEvent = index;
        }
        if (event.kind != ViewEvent::Kind::Progress || event.request != second)
        {
            continue;
        }
        ++secondReports;
        EXPECT_GE(event.completed, lastCompleted);
        EXPECT_LE(event.completed, event.total);
        lastCompleted = event.completed;
        lastTotal = event.total;
    }
    EXPECT_GT(secondReports, 0u) << "the preview's operation reported no progress";
    EXPECT_EQ(lastCompleted, lastTotal);

    // Nothing the overtaken preview said arrives after the newest one has started saying things, so a
    // bar can never be dragged back by a request nobody is waiting for.
    EXPECT_LT(lastFirstEvent, firstSecondEvent);
}

TEST(PreviewProbeTest, ClosingWhileAPreviewRunsStopsItAndLetsTheEngineShutDown)
{
    an::Result<an::EngineContext> engine = probe::createProbeEngine(4u);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    std::shared_ptr<const an::Image<float>> source = noisyFrame(*engine, 192u);
    const std::weak_ptr<const an::Image<float>> watched = source;
    RecordingView view;
    {
        an::Result<std::unique_ptr<probe::preview::PreviewSession>> session = probe::preview::PreviewSession::create(
            *engine, source, view, probe::preview::PreviewOptions{.maxConcurrency = 2u, .regionSize = 128u});
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        const std::uint64_t running = (*session)->request(kSlow);
        ASSERT_TRUE(waitUntilRunning(**session, running));

        (*session)->close();
        const std::optional<ViewEvent> ending = view.waitForEnd(running);
        ASSERT_TRUE(ending.has_value());
        EXPECT_EQ(ending->kind, ViewEvent::Kind::Abandoned);
        EXPECT_EQ(ending->code, an::ErrorCode::Cancelled);
        EXPECT_FALSE((*session)->running().has_value());
    }

    // The session let go of the image, so once the dialog's owner does too, nothing holds engine memory
    // and the context shuts down rather than being released late.
    source.reset();
    EXPECT_TRUE(watched.expired());
    EXPECT_EQ(engine->outstandingLeases(), 0u);
    EXPECT_TRUE(engine->shutdown().ok());
}

TEST(PreviewProbeTest, APreviewThatNarrowsItselfRunsBesideABatchAndOneThatDoesNotIsRefused)
{
    an::Result<an::EngineContext> engine = probe::createProbeEngine(4u);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const std::shared_ptr<const an::Image<float>> source = noisyFrame(*engine, 96u);

    {
        // A batch that takes the whole machine leaves no thread for anything else, and the preview is
        // refused by name rather than queued behind it or allowed to oversubscribe.
        an::Result<an::ExecutionContext> batch = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
        ASSERT_TRUE(batch.hasValue());
        RecordingView view;
        an::Result<std::unique_ptr<probe::preview::PreviewSession>> session = probe::preview::PreviewSession::create(
            *engine, source, view, probe::preview::PreviewOptions{.maxConcurrency = 1u, .regionSize = 64u});
        ASSERT_TRUE(session.hasValue());
        const std::uint64_t refused = (*session)->request(kQuick);
        const std::optional<ViewEvent> ending = view.waitForEnd(refused);
        ASSERT_TRUE(ending.has_value());
        EXPECT_EQ(ending->kind, ViewEvent::Kind::Abandoned);
        EXPECT_EQ(ending->code, an::ErrorCode::Busy);
    }
    {
        // A batch that narrows itself leaves room, and a preview that narrows itself fits in it.
        an::ExecutionOptions narrowed;
        narrowed.maxConcurrency = 3u;
        an::Result<an::ExecutionContext> batch = an::ExecutionContext::create(*engine, narrowed);
        ASSERT_TRUE(batch.hasValue());
        RecordingView view;
        an::Result<std::unique_ptr<probe::preview::PreviewSession>> session = probe::preview::PreviewSession::create(
            *engine, source, view, probe::preview::PreviewOptions{.maxConcurrency = 1u, .regionSize = 64u});
        ASSERT_TRUE(session.hasValue());
        const std::uint64_t shown = (*session)->request(kQuick);
        const std::optional<ViewEvent> ending = view.waitForEnd(shown);
        ASSERT_TRUE(ending.has_value());
        EXPECT_EQ(ending->kind, ViewEvent::Kind::Ready) << an::errorCodeName(ending->code);
    }
}

TEST(PreviewProbeTest, AnImageNoLongerPreviewedLivesOnlyAsLongAsThePreviewThatWasUsingIt)
{
    an::Result<an::EngineContext> engine = probe::createProbeEngine(4u);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    std::shared_ptr<const an::Image<float>> original = noisyFrame(*engine, 128u);
    const std::weak_ptr<const an::Image<float>> watched = original;
    RecordingView view;
    an::Result<std::unique_ptr<probe::preview::PreviewSession>> session = probe::preview::PreviewSession::create(
        *engine, original, view, probe::preview::PreviewOptions{.maxConcurrency = 2u, .regionSize = 64u});
    ASSERT_TRUE(session.hasValue());

    const std::uint64_t first = (*session)->request(kQuick);
    (*session)->replaceSource(noisyFrame(*engine, 48u));
    original.reset();
    const std::optional<ViewEvent> firstEnd = view.waitForEnd(first);
    ASSERT_TRUE(firstEnd.has_value());
    EXPECT_EQ(firstEnd->kind, ViewEvent::Kind::Ready);
    EXPECT_EQ(firstEnd->resultWidth, 64u);

    // The request that started on the original has ended, so nothing holds the original any more.
    EXPECT_TRUE(watched.expired());

    // The next request previews the replacement, clipped to its size.
    const std::uint64_t second = (*session)->request(kQuick);
    const std::optional<ViewEvent> secondEnd = view.waitForEnd(second);
    ASSERT_TRUE(secondEnd.has_value());
    EXPECT_EQ(secondEnd->kind, ViewEvent::Kind::Ready);
    EXPECT_EQ(secondEnd->resultWidth, 48u);
}
