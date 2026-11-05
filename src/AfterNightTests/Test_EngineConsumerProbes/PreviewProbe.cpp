// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "PreviewProbe.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <algorithm>
#include <utility>

// The session's implementation, written the way a dialog's preview worker is written.
//
// Three habits in here are what the engine asks of an asynchronous caller.
//
// **A request owns its stop source.** The engine stops what the token it was given says to stop, so a
// session that wants to stop the running request without stopping the next one gives each request a
// source of its own. One shared source would either stop nothing or stop everything.
//
// **A result is checked for staleness after it is computed, not only before.** A stop is a request,
// not a guarantee: an operation past its last checkpoint finishes and succeeds. The session therefore
// asks, when the result arrives, whether a newer request exists, and a stale result goes to the view as
// abandoned. That is the one line that makes "a stale frame is never shown" true.
//
// **Every image a request uses is a local of the request.** The source is held by shared pointer for
// the run, the crop and the result are created by the run, and when the run ends everything it did
// not hand to the view is released - which is what lets the engine context shut down after `close`.

namespace probe::preview
{
namespace
{

/// Tags every progress report of one request with its number and hands it to the view.
class RequestProgress final : public an::ProgressSink
{
public:
    /// Builds a sink for one request.
    RequestProgress(PreviewView& view, std::uint64_t request) noexcept : m_view(view), m_request(request) {}

    /// Forwards a report.
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        m_view.onPreviewProgress(m_request, report.completed, report.total);
    }

private:
    PreviewView& m_view;
    std::uint64_t m_request;
};

} // namespace

an::Result<std::unique_ptr<PreviewSession>> PreviewSession::create(const an::EngineContext& engine,
                                                                   std::shared_ptr<const an::Image<float>> source,
                                                                   PreviewView& view, const PreviewOptions& options)
{
    if (source == nullptr || !source->valid())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "a preview needs an image to preview");
    }
    std::unique_ptr<PreviewSession> session(new PreviewSession(engine, std::move(source), view, options));
    session->m_worker = std::thread([raw = session.get()] { raw->work(); });
    return session;
}

PreviewSession::PreviewSession(const an::EngineContext& engine, std::shared_ptr<const an::Image<float>> source,
                               PreviewView& view, const PreviewOptions& options)
    : m_engine(engine), m_view(view), m_options(options), m_source(std::move(source))
{
}

PreviewSession::~PreviewSession()
{
    close();
}

std::uint64_t PreviewSession::request(const PreviewParameters& parameters)
{
    std::optional<std::uint64_t> dropped;
    std::uint64_t number = 0u;
    {
        const std::scoped_lock lock(m_mutex);
        number = ++m_latest;
        if (m_pending.has_value())
        {
            dropped = m_pending->number;
        }
        m_pending = Job{.number = number, .parameters = parameters, .source = m_source};
        if (m_running.has_value())
        {
            m_runningStop.request_stop();
        }
    }
    if (dropped.has_value())
    {
        m_view.onPreviewAbandoned(*dropped, an::ErrorCode::Cancelled);
    }
    m_wake.notify_one();
    return number;
}

void PreviewSession::replaceSource(std::shared_ptr<const an::Image<float>> source)
{
    const std::scoped_lock lock(m_mutex);
    m_source = std::move(source);
}

std::optional<std::uint64_t> PreviewSession::running() const
{
    const std::scoped_lock lock(m_mutex);
    return m_running;
}

void PreviewSession::close() noexcept
{
    std::optional<std::uint64_t> dropped;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_closing)
        {
            return;
        }
        m_closing = true;
        if (m_pending.has_value())
        {
            dropped = m_pending->number;
            m_pending.reset();
        }
        if (m_running.has_value())
        {
            m_runningStop.request_stop();
        }
    }
    if (dropped.has_value())
    {
        m_view.onPreviewAbandoned(*dropped, an::ErrorCode::Cancelled);
    }
    m_wake.notify_one();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    const std::scoped_lock lock(m_mutex);
    m_source.reset();
}

bool PreviewSession::newest(std::uint64_t number) const
{
    const std::scoped_lock lock(m_mutex);
    return !m_closing && number == m_latest;
}

void PreviewSession::work()
{
    while (true)
    {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_closing || m_pending.has_value(); });
            if (m_closing)
            {
                return;
            }
            job = std::move(*m_pending);
            m_pending.reset();
            m_running = job.number;
            m_runningStop = job.stop;
        }
        runJob(job);
        const std::scoped_lock lock(m_mutex);
        m_running.reset();
        m_runningStop = std::stop_source{};
    }
}

void PreviewSession::runJob(Job& job)
{
    // Everything the run creates lives in this scope; the report to the view is made after the scope
    // ends, so a view that holds the result holds the only reference to anything this run allocated.
    an::ErrorCode outcome = an::ErrorCode::Ok;
    std::shared_ptr<const an::Image<float>> delivered;
    {
        const std::optional<an::ImageDescriptor> descriptor = job.source->descriptor();
        const std::optional<an::ImageLayout> sourceLayout = job.source->layout();
        if (!descriptor.has_value() || !sourceLayout.has_value())
        {
            outcome = an::ErrorCode::NotInitialized;
        }
        else
        {
            RequestProgress progress(m_view, job.number);
            an::ExecutionOptions options;
            options.stopToken = job.stop.get_token();
            options.progressSink = &progress;
            options.maxConcurrency = m_options.maxConcurrency;
            an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
            if (!execution.hasValue())
            {
                outcome = execution.status().code();
            }
            else
            {
                outcome = [&]() -> an::ErrorCode {
                    // The region the dialog shows, centred and clipped to the image.
                    const std::uint32_t width = (std::min)(m_options.regionSize, descriptor->extent().width());
                    const std::uint32_t height = (std::min)(m_options.regionSize, descriptor->extent().height());
                    const an::Result<an::GeometryPlan> crop =
                        an::planCrop(*descriptor, an::CropRequest{.x = (descriptor->extent().width() - width) / 2u,
                                                                  .y = (descriptor->extent().height() - height) / 2u,
                                                                  .width = width,
                                                                  .height = height});
                    if (!crop.hasValue())
                    {
                        return crop.status().code();
                    }
                    an::Result<an::Image<float>> region = an::Image<float>::create(
                        m_engine, crop->destinationDescriptor(), an::StorageArrangement::Planar,
                        an::StorageInitialization::Uninitialized);
                    an::Result<an::ValidityMask> regionMask =
                        an::ValidityMask::create(m_engine, crop->destinationDescriptor().extent());
                    if (!region.hasValue() || !regionMask.hasValue())
                    {
                        return region.hasValue() ? regionMask.status().code() : region.status().code();
                    }
                    {
                        const std::optional<an::ImageWriteAccess<float>> access = region->write();
                        const std::optional<an::ValidityMaskView<std::uint8_t>> maskView = regionMask->view();
                        const an::Result<an::GeometryResult> cropped = an::transformImageGeometry(
                            *execution, an::SampleType::Float32, job.source->storage().data(), *sourceLayout,
                            access->view().data(), access->view().layout(), *crop, an::GeometrySelection{}, *maskView);
                        if (!cropped.hasValue())
                        {
                            return cropped.status().code();
                        }
                    }

                    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(
                        an::PatchDenoiseRequest{.sigma = job.parameters.sigma, .iterations = job.parameters.iterations},
                        descriptor->extent().channelCount());
                    if (!plan.hasValue())
                    {
                        return plan.status().code();
                    }
                    an::Result<an::Image<float>> result = an::Image<float>::create(
                        m_engine, crop->destinationDescriptor(), an::StorageArrangement::Planar,
                        an::StorageInitialization::Uninitialized);
                    if (!result.hasValue())
                    {
                        return result.status().code();
                    }
                    {
                        const std::optional<an::ImageWriteAccess<float>> access = result->write();
                        const an::ImageLayout regionLayout = *region->layout();
                        const an::Result<an::PatchDenoiseResult> denoised = an::denoiseImagePatches(
                            *execution, an::SampleType::Float32, region->storage().data(), regionLayout,
                            access->view().data(), access->view().layout(), *plan);
                        if (!denoised.hasValue())
                        {
                            return denoised.status().code();
                        }
                    }
                    delivered = std::make_shared<const an::Image<float>>(std::move(*result));
                    return an::ErrorCode::Ok;
                }();
            }
        }
        // The run's own reference to the source goes here, whether or not anything else still holds it.
        job.source.reset();
    }

    if (outcome == an::ErrorCode::Ok && newest(job.number))
    {
        m_view.onPreviewReady(job.number, std::move(delivered));
        return;
    }
    // A stale result is released before the view hears about it, so an abandoned request leaves
    // nothing of its own behind.
    delivered.reset();
    m_view.onPreviewAbandoned(job.number, outcome == an::ErrorCode::Ok ? an::ErrorCode::Cancelled : outcome);
}

} // namespace probe::preview
