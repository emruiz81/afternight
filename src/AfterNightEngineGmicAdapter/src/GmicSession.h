// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - the session a handle points at, and how a call enters the
// library.
//
// This is the implementation side of `GmicRuntimeAdapter`: the state a handle holds, the process-wide
// lock that makes "one call into the library at a time" a rule rather than a hope, and the two calls
// that cross into G'MIC. The library's own header arrives through "GmicHeader.h", which includes the
// C bridge and nothing else.
//
// What lives here rather than in the public header is everything a consumer must not need: a lease,
// the option strings the library reads as C strings, and the counters. What lives in the public
// header is a handle and a forward declaration, which is what lets a caller compile against this
// adapter without a G'MIC toolchain.

#pragma once

#include "GmicHeader.h"

#include "GmicProcessState.h"

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>
#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>
#include <AfterNightEngineGmicAdapter/GmicRuntimeAdapter.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace an
{

class ExecutionContext;

/// One open session: the lease that holds its engine context, the options every call reuses, and the
/// counters.
///
/// Reference-counted by the handles and the calls that are running, so a session destroyed while
/// another thread is inside a call leaves the state alive until that call returns. Nothing about this
/// type is copyable or movable: the counters are read from other threads and the lease is released by
/// its destructor.
class GmicSessionState
{
public:
    /// Opens a session, or reports why it cannot.
    ///
    /// The order is the contract, and it is the order the public header documents: the build fact
    /// first, then the options, then the engine context's lease, and only then the library's probe.
    /// Each step undoes what the previous one took when it fails, so a refused open leaves no lease
    /// behind.
    ///
    /// @param engine the engine context the session belongs to.
    /// @param options what the caller configured.
    /// @return the state, or the failure to report.
    [[nodiscard]] static Result<std::shared_ptr<GmicSessionState>> open(const EngineContext& engine,
                                                                        const GmicRuntimeOptions& options);

    /// Releases the lease and drops this process's open-session count.
    ~GmicSessionState();

    GmicSessionState(const GmicSessionState&) = delete;
    GmicSessionState& operator=(const GmicSessionState&) = delete;
    GmicSessionState(GmicSessionState&&) = delete;
    GmicSessionState& operator=(GmicSessionState&&) = delete;

    /// Runs a command with no image. See `GmicRuntimeAdapter::execute`.
    [[nodiscard]] Status execute(const ExecutionContext& execution, std::string_view command);

    /// Runs a command over one image. See `GmicRuntimeAdapter::run`.
    [[nodiscard]] Result<RawFrame> run(const ExecutionContext& execution, const GmicImageCall& call);

    /// Which engine context this session holds open.
    [[nodiscard]] std::uint64_t engineContextId() const noexcept
    {
        return m_lease.contextId();
    }

    /// How many calls this session has in progress right now, running or waiting for the library lock.
    [[nodiscard]] std::uint64_t callsInFlight() const noexcept
    {
        return m_callsInFlight.load(std::memory_order_relaxed);
    }

    /// What this session has done since it opened.
    [[nodiscard]] GmicCallCounters counters() const noexcept;

private:
    /// Takes the lease and the options a successful open has already validated.
    GmicSessionState(EngineContextLease lease, std::string customCommands, bool useStandardLibrary,
                     GmicCancellationPolicy cancellation) noexcept;

    /// Runs one body with the library lock held, the counters moved and cancellation applied as this
    /// session's policy states it.
    ///
    /// Every command a session runs goes through here, so there is one place where the boundary
    /// checks, the lock, the abort flag, the deadline watch and the error translation happen, rather
    /// than one per entry point; only the once-per-process probe, which belongs to no session, takes
    /// the lock and starts its thread itself. The body is handed the option structure it should fill
    /// in and returns the library's own result code.
    ///
    /// @tparam BodyT a callable taking `gmic_interface_options&` and returning `int`.
    /// @param execution the operation this runs under.
    /// @param operation the adapter operation, spelled as it is in the source.
    /// @param errorText filled with what the library said when the command failed.
    /// @param body what to run inside the lock.
    /// @return success, or the failure to report.
    template <typename BodyT>
    [[nodiscard]] Status withLibrary(const ExecutionContext& execution, const char* operation, std::string& errorText,
                                     BodyT&& body);

    /// The hold on the engine context, which is what stops it being shut down under a session.
    EngineContextLease m_lease;

    /// The command definitions every call parses, held because the library reads a C string that must
    /// outlive the call.
    std::string m_customCommands;

    /// Whether the library's own standard library of commands is available to a command.
    bool m_useStandardLibrary = true;

    /// When a cancellation request reaches a running command.
    GmicCancellationPolicy m_cancellation = GmicCancellationPolicy::DuringCall;

    /// How many calls this session has in progress, counted before the library lock is taken, so a
    /// shutdown can refuse.
    std::atomic<std::uint64_t> m_callsInFlight{0u};

    /// The counters, each moved once per call that reaches its case.
    std::atomic<std::uint64_t> m_calls{0u};
    std::atomic<std::uint64_t> m_sharedBytes{0u};
    std::atomic<std::uint64_t> m_copiedIntoLibraryBytes{0u};
    std::atomic<std::uint64_t> m_libraryResultBytes{0u};
    std::atomic<std::uint64_t> m_peakLibraryResultBytes{0u};
    std::atomic<std::uint64_t> m_bridgeRefusals{0u};
    std::atomic<std::uint64_t> m_cancellations{0u};
    std::atomic<std::uint64_t> m_commandFailures{0u};
};

/// A failure in this adapter's own vocabulary.
///
/// One place that attaches the typed reason field, so no call site spells the key and the kind
/// together and gets one of them wrong.
///
/// @param code what kind of failure it is.
/// @param reason which of this adapter's rules refused.
/// @param operation the adapter operation that failed, spelled as it is in the source.
/// @param message a sentence for a person.
/// @return the failure, in `ErrorDomain::Interop`, carrying the reason as typed context.
[[nodiscard]] Status gmicAdapterFailure(ErrorCode code, GmicAdapterReason reason, const char* operation,
                                        std::string message);

} // namespace an
