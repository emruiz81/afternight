// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the full host's native suite shares: the run's engine session, borrowed by the host's own `HostEngineSession`
/// through a session capsule record as the host borrows the runtime backend's, the host's own application context
/// lending it, and the images the cases show and read.
///
/// The run has one session, opened by `main` before the first case and shut down, checked, after the last: the suite
/// stands where the runtime backend's module seat stands in the host, and owns the session the native windows borrow.
#pragma once

#include "ExtensionAppContext.h"
#include "ExtensionNativeProcessWindow.h"
#include "HostPreviewFrames.h"
#include "HostRuntimeSession.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "fixtures/TestImageGenerators.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <gtest/gtest.h>

#include <QCborMap>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRect>
#include <QSize>
#include <QTest>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <variant>
#include <vector>

namespace an::tests::host_native
{

/// The run's session as the native windows reach it: through the host's context.
[[nodiscard]] inline const an::consumer::EngineSession& suiteSession()
{
    return an::ui::appContext()->getEngineSessionProvider()->engineSession();
}

/// The host's application context the run installed.
[[nodiscard]] inline an::ui::extensions::ExtensionAppContext& suiteContext()
{
    return *dynamic_cast<an::ui::extensions::ExtensionAppContext*>(an::ui::appContext());
}

/// A grey Float32 asset of the run's session, every sample at one level.
[[nodiscard]] inline an::consumer::ImageAsset greyAsset(std::uint32_t width, std::uint32_t height, double level)
{
    an::Result<an::consumer::ImageAsset> asset =
        an::tests::fixtures::createGreyFloatAsset(suiteSession(), width, height, level);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::move(*asset) : an::consumer::ImageAsset{};
}

/// An RGB Float32 asset of the run's session, each channel at one level.
[[nodiscard]] inline an::consumer::ImageAsset rgbAsset(std::uint32_t width, std::uint32_t height, double red,
                                                       double green, double blue)
{
    an::Result<an::consumer::ImageAsset> asset =
        an::tests::fixtures::createRgbFloatAsset(suiteSession(), width, height, red, green, blue);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::move(*asset) : an::consumer::ImageAsset{};
}

/// Shows an asset in a view as a new document of the view, as the host's view factory does.
inline void showInView(ImageViewControl& view, an::consumer::ImageAsset&& asset)
{
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(asset));
    ASSERT_TRUE(document.hasValue()) << document.status().message();
    view.setDocument(std::move(*document));
}

/// Shows a grey image in a view.
inline void showGrey(ImageViewControl& view, std::uint32_t width, std::uint32_t height, double level)
{
    showInView(view, greyAsset(width, height, level));
}

/// The sample at a position and channel of a Float32 image, or NaN.
[[nodiscard]] inline double sampleAt(const an::consumer::AnyImage& image, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t channel = 0u)
{
    const an::Image<float>* typed = std::get_if<an::Image<float>>(&image);
    if (typed == nullptr || !typed->view().has_value())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto row = typed->view()->row(channel, y);
    return row.has_value() && x < row->size() ? static_cast<double>((*row)[x])
                                              : std::numeric_limits<double>::quiet_NaN();
}

/// The sample at a position and channel of an asset, read under a lease, or NaN.
[[nodiscard]] inline double assetSampleAt(const an::consumer::ImageAsset& asset, std::uint32_t x, std::uint32_t y,
                                          std::uint32_t channel = 0u)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    return lease.hasValue() ? sampleAt(lease->image(), x, y, channel) : std::numeric_limits<double>::quiet_NaN();
}

/// The sample at a position and channel of what a view shows, or NaN when it shows nothing.
[[nodiscard]] inline double viewSampleAt(const ImageViewControl& view, std::uint32_t x, std::uint32_t y,
                                         std::uint32_t channel = 0u)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = view.document().asset();
    return asset ? assetSampleAt(*asset, x, y, channel) : std::numeric_limits<double>::quiet_NaN();
}

/// Runs the event loop until a condition holds or a bound passes.
///
/// @return whether the condition held.
[[nodiscard]] inline bool waitUntil(const std::function<bool()>& condition, int boundMilliseconds = 5000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition())
    {
        if (elapsed.elapsed() > boundMilliseconds)
        {
            return condition();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }
    return true;
}

/// What a preview body standing for an extension's saw of the frames it ran, recorded on the frames' workers.
class PreviewFrameRecord
{
public:
    /// One frame as the body was handed it.
    struct Frame
    {
        /// The source's width, height and channels: what the extension reads and writes.
        int width = 0;
        int height = 0;
        int channels = 0;
        std::uint32_t factor = 1u;
        QRect part;
        QSize imageSize;
        bool interactive = false;
        bool masked = false;
        QCborMap params;

        /// The source's first sample, NaN for a source not Float32.
        double firstSample = 0.0;

        /// The manual mask's width and height, zero without one.
        int maskWidth = 0;
        int maskHeight = 0;

        /// When the body was handed the frame.
        std::chrono::steady_clock::time_point started;
    };

    /// @param level the level every sample of what the body answers is at.
    /// @param answeredChannels how many channels what the body answers has; zero for the source's.
    explicit PreviewFrameRecord(double level = 0.5, int answeredChannels = 0)
        : m_level(level), m_answeredChannels(answeredChannels)
    {
    }

    [[nodiscard]] double level() const noexcept
    {
        return m_level;
    }

    [[nodiscard]] int answeredChannels() const noexcept
    {
        return m_answeredChannels;
    }

    [[nodiscard]] int count() const
    {
        const std::lock_guard lock(m_mutex);
        return static_cast<int>(m_frames.size());
    }

    [[nodiscard]] std::vector<Frame> frames() const
    {
        const std::lock_guard lock(m_mutex);
        return m_frames;
    }

    [[nodiscard]] std::optional<Frame> last() const
    {
        const std::lock_guard lock(m_mutex);
        return m_frames.empty() ? std::nullopt : std::optional<Frame>(m_frames.back());
    }

    void clear()
    {
        const std::lock_guard lock(m_mutex);
        m_frames.clear();
    }

    void add(Frame frame)
    {
        const std::lock_guard lock(m_mutex);
        m_frames.push_back(std::move(frame));
    }

private:
    double m_level;
    int m_answeredChannels;
    mutable std::mutex m_mutex;
    std::vector<Frame> m_frames;
};

/// A preview body standing for an extension's: it records each frame it is handed and answers a new Float32 image of
/// the frame's source's width and height - with the source's channels unless the record names others - every sample at
/// the record's level; an image it cannot make, a failure.
[[nodiscard]] inline an::ui::extensions::ExtensionPreviewBody
recordingPreviewBody(const std::shared_ptr<PreviewFrameRecord>& record)
{
    const an::consumer::EngineSession* session = &suiteSession();
    return [record,
            session](const an::ui::extensions::ExtensionPreviewFrame& frame) -> an::Result<an::consumer::ImageAsset> {
        if (frame.source == nullptr)
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the frame has no source");
        }
        const std::optional<an::ImageDescriptor> described =
            std::visit([](const auto& typed) { return typed.descriptor(); }, *frame.source);
        if (!described.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the frame's source has no description");
        }
        PreviewFrameRecord::Frame seen;
        seen.width = static_cast<int>(described->extent().width());
        seen.height = static_cast<int>(described->extent().height());
        seen.channels = static_cast<int>(described->extent().channelCount());
        seen.factor = frame.factor;
        seen.part = frame.part;
        seen.imageSize = frame.imageSize;
        seen.interactive = frame.interactive;
        seen.masked = frame.mask != nullptr;
        seen.params = frame.params;
        seen.firstSample = sampleAt(*frame.source, 0u, 0u);
        seen.started = std::chrono::steady_clock::now();
        if (frame.mask != nullptr)
        {
            const std::optional<an::ImageDescriptor> mask =
                std::visit([](const auto& typed) { return typed.descriptor(); }, *frame.mask);
            if (mask.has_value())
            {
                seen.maskWidth = static_cast<int>(mask->extent().width());
                seen.maskHeight = static_cast<int>(mask->extent().height());
            }
        }
        record->add(seen);
        const double level = record->level();
        const int channels = record->answeredChannels() > 0 ? record->answeredChannels() : seen.channels;
        return channels >= 3
                   ? an::tests::fixtures::createRgbFloatAsset(*session, described->extent().width(),
                                                              described->extent().height(), level, level, level)
                   : an::tests::fixtures::createGreyFloatAsset(*session, described->extent().width(),
                                                               described->extent().height(), level);
    };
}

/// Holds the frames a preview body is handed until the case lets them through, so a frame can be caught on its way.
class PreviewFrameGate
{
public:
    /// Lets every frame waiting, and every later one, through.
    void open()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_open = true;
        }
        m_changed.notify_all();
    }

    /// Holds every later frame.
    void close()
    {
        const std::lock_guard lock(m_mutex);
        m_open = false;
    }

    /// How many frames reached the body.
    [[nodiscard]] int arrived() const
    {
        const std::lock_guard lock(m_mutex);
        return m_arrived;
    }

    /// How many frames wait at the gate now.
    [[nodiscard]] int waiting() const
    {
        const std::lock_guard lock(m_mutex);
        return m_waiting;
    }

    /// Waits until the gate is open or the frame is stopped.
    ///
    /// @return whether the frame may go on.
    [[nodiscard]] bool pass(std::stop_token stop)
    {
        std::unique_lock lock(m_mutex);
        ++m_arrived;
        ++m_waiting;
        const bool open = m_changed.wait(lock, stop, [this]() { return m_open; });
        --m_waiting;
        return open;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    bool m_open = true;
    int m_arrived = 0;
    int m_waiting = 0;
};

/// `recordingPreviewBody` behind a gate: each frame waits at the gate first, and one stopped there is refused as
/// stopped.
[[nodiscard]] inline an::ui::extensions::ExtensionPreviewBody
gatedPreviewBody(const std::shared_ptr<PreviewFrameGate>& gate, const std::shared_ptr<PreviewFrameRecord>& record)
{
    return [gate, body = recordingPreviewBody(record)](
               const an::ui::extensions::ExtensionPreviewFrame& frame) -> an::Result<an::consumer::ImageAsset> {
        if (!gate->pass(frame.stop))
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the frame was stopped at the gate");
        }
        return body(frame);
    };
}

/// Stops a native window's frames and waits for every one to land when it goes: declared after the window, it keeps a
/// frame's worker from calling into a window already gone however the case ends - as the host's teardown does.
class FramesLandFirst
{
public:
    explicit FramesLandFirst(an::ui::extensions::ExtensionNativeProcessWindow& window) : m_window(window) {}

    ~FramesLandFirst()
    {
        m_window.stopPreviews();
        EXPECT_TRUE(waitUntil([this]() { return m_window.isRunIdle(); }, 20000)) << "a frame never landed";
    }

    FramesLandFirst(const FramesLandFirst&) = delete;
    FramesLandFirst& operator=(const FramesLandFirst&) = delete;

private:
    an::ui::extensions::ExtensionNativeProcessWindow& m_window;
};

/// Counts what a view tells its listeners about its image.
class ViewEventCounter final : public ImageViewControlListener
{
public:
    explicit ViewEventCounter(ImageViewControl& view) : ImageViewControlListener(&view) {}

    ViewEventCounter(const ViewEventCounter&) = delete;
    ViewEventCounter& operator=(const ViewEventCounter&) = delete;

    void onImageLoaded(ImageViewControl*) override
    {
        ++loaded;
    }

    void onImageUpdated(ImageViewControl*) override
    {
        ++updated;
    }

    void reset() noexcept
    {
        loaded = 0;
        updated = 0;
    }

    int loaded = 0;
    int updated = 0;
};

} // namespace an::tests::host_native
