// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime backend's module seat: the session the loaded module owns, opened by `seatInitialize`, lent to the
/// embedded Python module and to the host through the session capsule, and shut down only when nothing holds it.

#include "RuntimeSeat.h"

#include "RuntimeEngine.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <AfterNightPlatform/Logging.h>

#include <exception>
#include <mutex>
#include <string>
#include <utility>

namespace an::sdk
{

namespace
{

/// What the seat holds: the session, and the text the last C export left. The C interface carries no handle, so this
/// is the one place the loaded module's state lives; its lock orders the exports against each other, since a host may
/// probe, initialize and shut down from different threads.
struct SeatState
{
    std::mutex lock;
    std::shared_ptr<an::consumer::EngineSession> session;
    std::string lastError;
};

SeatState gRuntimeSeat;

/// Creates a one-by-one Float32 image in a context and lets it go, which is the probe's question: whether the engine
/// the backend linked can allocate.
[[nodiscard]] an::Status allocateProbeImage(const an::EngineContext& engine)
{
    an::Result<an::ImageDescriptor> descriptor = describeSdkImage(engine.limits(), 1u, 1u, 1u, an::SampleType::Float32);
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }
    an::Result<an::Image<float>> image =
        createCompactImage<float>(engine, *descriptor, an::StorageInitialization::Zeroed);
    return image.hasValue() ? an::Status::success() : image.status();
}

/// The settings of the backend's session: the host's threads, the engine's limits and the backend's codecs.
[[nodiscard]] an::consumer::EngineSessionSettings runtimeSessionSettings()
{
    an::consumer::EngineSessionSettings settings;
    settings.codecRegistry = [] { return createRuntimeCodecRegistry(); };
    return settings;
}

/// What is still held of a session, in words, or an empty string when nothing is.
[[nodiscard]] std::string outstandingText(const an::consumer::EngineSession& session, const long sharedOwners)
{
    const an::consumer::SessionOutstanding outstanding = session.outstanding();
    std::string text;
    const auto add = [&text](const std::uint64_t count, const char* what) {
        if (count != 0u)
        {
            text += (text.empty() ? "" : ", ") + std::to_string(count) + " " + what;
        }
    };
    add(sharedOwners > 1 ? static_cast<std::uint64_t>(sharedOwners - 1) : 0u, "session capsule or borrow(s)");
    add(outstanding.assets, "image asset(s), held by images or arrays the interpreter still holds");
    add(outstanding.operations, "admitted operation(s)");
    add(outstanding.queued, "queued request(s)");
    add(outstanding.holds, "hold(s)");
    add(outstanding.previewReservations, "preview reservation(s)");
    return text;
}

} // namespace

int seatProbe() noexcept
{
    std::lock_guard<std::mutex> guard(gRuntimeSeat.lock);
    gRuntimeSeat.lastError.clear();
    try
    {
        if (gRuntimeSeat.session)
        {
            an::Result<an::consumer::SessionHold> hold = gRuntimeSeat.session->hold();
            if (!hold.hasValue())
            {
                gRuntimeSeat.lastError = "Runtime backend probe: " + describeFailure(hold.status());
                return 0;
            }
            if (const an::Status allocated = allocateProbeImage(hold->engine()); allocated.failed())
            {
                gRuntimeSeat.lastError = "Runtime backend probe: " + describeFailure(allocated);
                return 0;
            }
            return 1;
        }

        // Before initialization the probe builds a context of its own, uses it and shuts it down, leaving nothing.
        an::EngineContextOptions options;
        options.maxConcurrency = 1u;
        an::Result<an::EngineContext> engine = an::EngineContext::create(options);
        if (!engine.hasValue())
        {
            gRuntimeSeat.lastError = "Runtime backend probe: " + describeFailure(engine.status());
            return 0;
        }
        const an::Status allocated = allocateProbeImage(*engine);
        const an::Status stopped = engine->shutdown();
        if (allocated.failed() || stopped.failed())
        {
            gRuntimeSeat.lastError =
                "Runtime backend probe: " + describeFailure(allocated.failed() ? allocated : stopped);
            return 0;
        }
        return 1;
    }
    catch (const std::exception& error)
    {
        gRuntimeSeat.lastError = error.what();
    }
    catch (...)
    {
        gRuntimeSeat.lastError = "Unknown runtime backend probe failure";
    }
    return 0;
}

int seatInitialize() noexcept
{
    std::lock_guard<std::mutex> guard(gRuntimeSeat.lock);
    gRuntimeSeat.lastError.clear();
    if (gRuntimeSeat.session)
    {
        return 1;
    }
    try
    {
        an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(runtimeSessionSettings());
        if (!session.hasValue())
        {
            gRuntimeSeat.lastError = "Runtime backend initialization: " + describeFailure(session.status());
            return 0;
        }
        gRuntimeSeat.session = std::make_shared<an::consumer::EngineSession>(std::move(*session));
        LOG_INFO("Runtime backend: opened its engine session, {} thread(s)", gRuntimeSeat.session->width());
        return 1;
    }
    catch (const std::exception& error)
    {
        gRuntimeSeat.lastError = error.what();
    }
    catch (...)
    {
        gRuntimeSeat.lastError = "Unknown runtime backend initialization failure";
    }
    return 0;
}

void seatShutdown() noexcept
{
    std::lock_guard<std::mutex> guard(gRuntimeSeat.lock);
    gRuntimeSeat.lastError.clear();
    if (!gRuntimeSeat.session)
    {
        return;
    }
    try
    {
        // Checked before admission is closed, so a refused shutdown leaves the session working for whatever still
        // holds it, and the host can finalize its interpreter and try again.
        const std::string held = outstandingText(*gRuntimeSeat.session, gRuntimeSeat.session.use_count());
        if (!held.empty())
        {
            gRuntimeSeat.lastError = "Runtime backend shutdown refused: the session is still held by " + held;
            LOG_WARNING("{}", gRuntimeSeat.lastError);
            return;
        }
        if (const an::Status stopped = gRuntimeSeat.session->shutdown(); stopped.failed())
        {
            gRuntimeSeat.lastError = "Runtime backend shutdown refused: " + describeFailure(stopped);
            LOG_WARNING("{}", gRuntimeSeat.lastError);
            return;
        }
        gRuntimeSeat.session.reset();
        LOG_INFO("Runtime backend: shut its engine session down");
    }
    catch (const std::exception& error)
    {
        gRuntimeSeat.lastError = error.what();
    }
    catch (...)
    {
        gRuntimeSeat.lastError = "Unknown runtime backend shutdown failure";
    }
}

const char* seatLastError() noexcept
{
    return gRuntimeSeat.lastError.c_str();
}

std::shared_ptr<an::consumer::EngineSession> seatSession() noexcept
{
    std::lock_guard<std::mutex> guard(gRuntimeSeat.lock);
    return gRuntimeSeat.session;
}

} // namespace an::sdk
