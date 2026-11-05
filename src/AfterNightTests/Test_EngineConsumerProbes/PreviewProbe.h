// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// An asynchronous real-time preview, written against the engine's public headers alone.
//
// The third consumer shape. It is modelled on the preview machinery the product's process dialogs
// share: one worker per dialog, a request posted every time a control moves, the newest request
// winning over any still waiting, the engine run on a region of the image the user is looking at,
// progress shown while it runs, and a result handed to the view on a thread that is not the view's.
// Machinery of that shape goes wrong in known ways: a running computation that finishes after the
// parameters have moved on shows a stale frame; preview calls that pass no progress callback leave
// a spinner where progress should be; and a dialog closed while a preview runs races the worker for
// the preview image.
//
// This probe does the same job the way a consumer of the engine would, and is asked the questions
// a person dragging a slider asks: does the preview I am looking at belong to the parameters I last
// chose; does the bar move, and does it belong to what is running; does closing the dialog stop the
// work and let everything go; and does a preview leave the rest of the machine to the batch that is
// already running.
//
// It includes no consumer header, compiles no consumer source, and links no consumer target.
//
// **What one request does, in engine terms.** Crops a square region from the centre of the source
// into an image of its own, plans a patch denoise of that region at the request's noise level, and
// runs it into a third image under an execution context carrying the request's own stop token, a
// progress sink tagged with the request's number and a concurrency no wider than the session was
// allowed. The result goes to the view only when no newer request exists.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace probe::preview
{

/// What one preview is asked to compute.
struct PreviewParameters
{
    /// The noise level to remove, in the image's own sample units.
    double sigma = 0.02;

    /// How many strengthen-and-subtract iterations the denoise runs. More is slower.
    std::uint32_t iterations = 1u;
};

/// The half of a dialog that shows what the preview produced. Called on the session's worker thread.
///
/// Every call names the request it is about, because by the time it arrives the dialog may have asked
/// for something else; a view that draws a report without checking the number draws the wrong thing.
class PreviewView
{
public:
    /// Nothing to release.
    virtual ~PreviewView() = default;

    /// A running request's operation reported progress.
    ///
    /// @param request the request it belongs to.
    /// @param completed how many of the operation's units are finished.
    /// @param total how many it has.
    virtual void onPreviewProgress(std::uint64_t request, std::uint64_t completed, std::uint64_t total) noexcept = 0;

    /// A request finished and is still the newest.
    ///
    /// @param request the request.
    /// @param result the denoised region, which the view now shares ownership of.
    virtual void onPreviewReady(std::uint64_t request, std::shared_ptr<const an::Image<float>> result) noexcept = 0;

    /// A request ended without a result the view should show.
    ///
    /// @param request the request.
    /// @param code why: `ErrorCode::Cancelled` for a request a newer one or a close stopped or made
    ///        stale, otherwise the engine's refusal.
    virtual void onPreviewAbandoned(std::uint64_t request, an::ErrorCode code) noexcept = 0;
};

/// How a session runs its previews.
struct PreviewOptions
{
    /// The most threads a preview may use, or zero for the engine context's whole width.
    std::uint32_t maxConcurrency = 1u;

    /// The side of the square region previewed, in pixels, clipped to the source.
    std::uint32_t regionSize = 64u;
};

/// One dialog's preview worker.
///
/// Created with the image the dialog previews and the view it reports to; both must outlive the
/// session's `close`, and the engine context must outlive the session. Not copyable or movable: the
/// worker thread holds a pointer to it.
class PreviewSession
{
public:
    /// Starts a session's worker.
    ///
    /// @param engine the context previews run in.
    /// @param source the image being previewed, shared with whoever else holds it.
    /// @param view where results and progress go.
    /// @param options how previews run.
    /// @return the session, or `ErrorCode::InvalidArgument` for an empty source.
    [[nodiscard]] static an::Result<std::unique_ptr<PreviewSession>>
    create(const an::EngineContext& engine, std::shared_ptr<const an::Image<float>> source, PreviewView& view,
           const PreviewOptions& options);

    /// Stops whatever is running and joins the worker.
    ~PreviewSession();

    /// Copying is deleted.
    PreviewSession(const PreviewSession&) = delete;

    /// Copying is deleted.
    PreviewSession& operator=(const PreviewSession&) = delete;

    /// Asks for a preview of these parameters.
    ///
    /// A request still waiting is replaced and reported abandoned. A request already running is
    /// stopped, and its result, if it finishes anyway, is never shown.
    ///
    /// @param parameters what to compute.
    /// @return the request's number, increasing with every call.
    std::uint64_t request(const PreviewParameters& parameters);

    /// Previews a different image from the next request on. A running request keeps the image it
    /// started with until it ends, and then lets it go.
    ///
    /// @param source the new image.
    void replaceSource(std::shared_ptr<const an::Image<float>> source);

    /// The request currently running, if any.
    ///
    /// @return its number, or an empty optional when the worker is idle.
    [[nodiscard]] std::optional<std::uint64_t> running() const;

    /// Stops the running request, drops a waiting one and joins the worker. Idempotent. Afterwards the
    /// session holds no image and no execution context.
    void close() noexcept;

private:
    /// One request, as the worker takes it.
    struct Job
    {
        std::uint64_t number = 0u;
        PreviewParameters parameters{};
        std::shared_ptr<const an::Image<float>> source{};
        std::stop_source stop{};
    };

    /// Builds a session; `create` starts its worker.
    PreviewSession(const an::EngineContext& engine, std::shared_ptr<const an::Image<float>> source, PreviewView& view,
                   const PreviewOptions& options);

    /// The worker's loop: wait for a job, run it, repeat until closed.
    void work();

    /// Runs one job to its end and reports how it ended.
    void runJob(Job& job);

    /// Whether a request is still the newest.
    [[nodiscard]] bool newest(std::uint64_t number) const;

    /// The context previews run in. Borrowed.
    const an::EngineContext& m_engine;

    /// Where results go. Borrowed.
    PreviewView& m_view;

    /// How previews run.
    PreviewOptions m_options;

    /// Guards everything below.
    mutable std::mutex m_mutex;

    /// Wakes the worker.
    std::condition_variable m_wake;

    /// The image the next request previews.
    std::shared_ptr<const an::Image<float>> m_source{};

    /// A request waiting to run.
    std::optional<Job> m_pending{};

    /// The number of the running request, if any.
    std::optional<std::uint64_t> m_running{};

    /// The running request's stop source.
    std::stop_source m_runningStop{};

    /// The newest request's number.
    std::uint64_t m_latest = 0u;

    /// Whether the session is closing.
    bool m_closing = false;

    /// The worker.
    std::thread m_worker{};
};

} // namespace probe::preview
