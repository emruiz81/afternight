// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Small pictures of what views show, made on the thread pool: what a task's rail item shows of its target.

#include "AfterNightShellCoreExport.h"

#include <QImage>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

class ImageViewControl;

namespace an::ui::shell
{

class ApplicationCore;

/// What the service came to, for the cases that hold it to its rules.
struct ViewThumbnailTelemetry
{
    /// Renderings started on the thread pool.
    std::uint64_t started = 0u;

    /// Renderings published.
    std::uint64_t published = 0u;

    /// Renderings that landed with a ticket that no longer committed - the image changed, the view closed, a newer
    /// rendering was asked for - and were dropped.
    std::uint64_t discarded = 0u;

    /// Renderings the session would not admit at once; each is asked for again a little later.
    std::uint64_t refused = 0u;

    /// Renderings the engine refused, or that found no session.
    std::uint64_t failed = 0u;

    /// Calls the renderings made of the engine and the session, and how many of them on the GUI thread, which is never
    /// one.
    std::uint64_t engineCalls = 0u;
    std::uint64_t engineCallsOnGuiThread = 0u;
};

/// Renders the views it follows into thumbnails `kSide` pixels square: the whole image, as the view presents it - its
/// stretch and its channel - scaled to cover the square and centred.
///
/// A rendering is one operation on the thread pool: the image's read lease, then an admission as `Preview` through
/// `tryAdmit` - so a thumbnail never queues in front of a run - then the image reduced for display and presented
/// through the analysis the view keeps of it, or one measured when it keeps none. It is published only when its commit
/// ticket still commits. A rendering is made when a view is followed and again each time its image is loaded or
/// updated; one refused admission is asked for again after `kRetry`. A view that goes is followed no more and its
/// thumbnail is dropped, its listeners told with a null image. The GUI thread makes no call of the engine.
///
/// Night is not applied here: whatever shows a thumbnail shows it in Night's form when Night is on. Used on the GUI
/// thread only.
class AFTERNIGHTSHELLCORE_API ViewThumbnailService final
{
public:
    /// The side of a thumbnail, in pixels: twice the rail's tile, for a display at twice the scale.
    static constexpr int kSide = 60;

    /// How long a refused rendering waits before it is asked for again.
    static constexpr std::chrono::milliseconds kRetry{400};

    /// Told on the GUI thread each time a view's thumbnail is published, or dropped with its view.
    using Listener = std::function<void(ImageViewControl* view, const QImage& thumbnail)>;

    explicit ViewThumbnailService(ApplicationCore& core);
    ~ViewThumbnailService();

    ViewThumbnailService(const ViewThumbnailService&) = delete;
    ViewThumbnailService& operator=(const ViewThumbnailService&) = delete;
    ViewThumbnailService(ViewThumbnailService&&) = delete;
    ViewThumbnailService& operator=(ViewThumbnailService&&) = delete;

    /// Follows a view: renders it now and each time its image changes. A view followed already, or null, changes
    /// nothing.
    void follow(ImageViewControl* view);

    /// Follows a view no more and drops its thumbnail; nothing for one it does not follow.
    void forget(ImageViewControl* view);

    /// Renders a followed view again: what it presents changed - its stretch, its channel - while its image did not.
    void refresh(ImageViewControl* view);

    [[nodiscard]] bool follows(const ImageViewControl* view) const;

    /// The view's last published thumbnail; null before the first, and for a view it does not follow.
    [[nodiscard]] QImage thumbnail(const ImageViewControl* view) const;

    /// Adds a listener, told of every thumbnail from now on; the token takes it out again.
    std::size_t subscribe(Listener listener);
    void unsubscribe(std::size_t token);

    /// Whether no rendering is asked for or on its way.
    [[nodiscard]] bool isIdle() const;

    [[nodiscard]] ViewThumbnailTelemetry telemetry() const;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
