// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether there is a G'MIC library here, and what went wrong.
///
/// The engine this adapter sits beside links no G'MIC library, so nothing about the engine's
/// capabilities changes when this library is present or absent. What a caller needs instead is a way
/// to ask this library what it can do, and the answer has four states rather than two: the library
/// may be missing from the build entirely, present and startable, in use because this adapter opened
/// a session against it, or refused when this adapter tried. Collapsing those into a boolean is what
/// makes an absent optional subsystem look like a broken one.
///
/// `Absent` is not a hypothetical. A build regenerated with AFTERNIGHT_GMIC=0 contains this library
/// with no G'MIC in it: this report answers, and opening a session refuses by name rather than
/// failing to load or crashing at the call site. That configuration exists so the refusal path is
/// built and tested rather than reasoned about, which is the whole of what "an adapter that is not
/// present is an ordinary outcome" has to mean in code.
///
/// The one difference from the Python adapter's report is worth stating rather than copying over.
/// CPython has one interpreter per process and reports its own version, so that adapter's report
/// carries a seat and two version triples. The C bridge this adapter uses - three upstream functions,
/// one of them a deallocator, and the patch level of this repository's changes to it - exposes no
/// version of the library and keeps no session between calls, so there is no version to report and
/// no seat to take. What is process-wide here is the library's own global state *during* a call,
/// which this adapter serialises, and whether the library has ever answered a probe. Both of those
/// are reported instead, and neither is invented.
///
/// Nothing here names a G'MIC or CImg type, and this header includes no header of either. A consumer
/// compiles against it with no G'MIC toolchain installed - that is the property that makes the
/// dependency optional in a build rather than only in a document.

#pragma once

#include <AfterNightEngineGmicAdapter/Api.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>

namespace an
{

/// What state this adapter's G'MIC library is in.
///
/// The four answers are deliberately separate, because a caller does something different about each
/// one: an absent library is a build decision, an available one is something to open a session
/// against, a loaded one is something to use, and a refused one is something to report with the
/// reason beside it.
enum class GmicAdapterState : std::uint8_t
{
    /// This build of the adapter contains no G'MIC library. Nothing can open a session:
    /// `GmicRuntimeAdapter::create` refuses with `GmicAdapterReason::LibraryNotCompiledIn`, so every
    /// handle in this build is empty, and a call on an empty handle is refused with
    /// `AdapterShutDown`, as in any build.
    Absent,

    /// A G'MIC library is compiled in and this adapter holds no session against it. Either nothing
    /// has been opened yet, or everything opened has been closed again.
    Available,

    /// This adapter holds at least one open session. Unlike the Python adapter's equivalent state
    /// this is not a seat: several sessions may be open at once, because the library keeps nothing
    /// between calls that a session could own exclusively.
    Loaded,

    /// Opening a session was attempted and refused after the library was asked to answer. The reason
    /// that refused it is reported beside this state rather than remembered by the caller.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    FailedToLoad
};

/// Why this adapter refused, in its own vocabulary.
///
/// An `ErrorCode` says what kind of failure it was and this says which of the adapter's own rules
/// produced it, so a caller branches on a value rather than on the text of a message. It travels on
/// a failure as a `ContextKey::AdapterReason` field, which is why it is an enumeration the engine
/// stores as a count and never interprets.
///
/// A G'MIC error's own text is not in here on purpose, for the reason a Python exception's class is
/// not in the other adapter's: the engine's vocabulary is closed and an interpreter's is not, so the
/// library's sentence is carried in the failure's message - for a person - while the branchable fact
/// is that the command failed at all.
enum class GmicAdapterReason : std::uint16_t
{
    /// No adapter-specific reason. The value a failure carries when it came from somewhere else.
    None,

    /// This build of the adapter contains no G'MIC library.
    LibraryNotCompiledIn,

    /// The library is compiled in and did not answer the probe this adapter runs before it trusts
    /// it, or its bridge predates the changes to it this adapter was built against. What it said is
    /// carried in the failure's message.
    LibraryProbeFailed,

    /// A stated value is not one the adapter accepts - a cancellation policy or a sharing decision
    /// outside its enumeration, a custom command block with an embedded null, a result intent whose
    /// schema, basis, domain or origin is not an enumerator, a pixel aspect that is not positive and
    /// finite. Refused before anything is asked of the library, because the caller's repair is the
    /// same whatever the library is doing.
    OptionsRejected,

    /// The call was made on a session that has been shut down or moved from.
    AdapterShutDown,

    /// Shutting down was refused because a call into the library is still running.
    CallInFlight,

    /// The command is empty. The library treats an empty command as a no-op that still pays for an
    /// interpreter, and a caller that meant to run something has a bug this names.
    CommandEmpty,

    /// The library reported that the command failed or threw before it could report. Its own
    /// sentence is in the failure's message or cause.
    CommandFailed,

    /// The command was not started, or stopped, because the caller asked for it or its deadline
    /// passed. Reported with the execution's own code: `ErrorCode::Cancelled` for a requested stop,
    /// `ErrorCode::DeadlineExceeded` for a deadline.
    Interrupted,

    /// A buffer could not be handed to the library as it stands. Which rule refused is a
    /// `BridgeRefusal` from `GmicImageBridge.h`, named in the failure's message.
    BufferBridgeRefused,

    /// `GmicSourceSharing::Share` was asked for over samples the library may not write through.
    /// Sharing is how the library works in place, so it needs a writable view and a contract that
    /// says so; its own reason rather than a `BridgeRefusal`, because the bridge's alphabet
    /// describes what two memory models can express and this is a rule of this call.
    SourceNotShareable,

    /// The command reported success and left no image behind. Every call this adapter makes hands
    /// the library one image and expects one back.
    ResultMissing,

    /// The command left more than one image behind. One image in and one image out is this
    /// adapter's contract, and a command that splits or appends is refused rather than truncated -
    /// with every buffer the library allocated released first.
    ResultImageCountUnexpected,

    /// What came back cannot be described as an engine frame: a depth above one, an extent or a
    /// channel count outside the limits in force, a geometry of zero, or a channel count that
    /// disagrees with the schema the call states for the result.
    ResultGeometryRejected,

    /// What came back is not what this adapter asked the library to produce - an interleaved buffer
    /// or a byte representation where float planes were requested. Refused rather than reinterpreted.
    ResultFormatUnexpected,

    /// The command named its result longer than the field the library's own structure declares for
    /// a name. This adapter asks the bridge to refuse such a name rather than copy it, and the bridge
    /// then returns no image. Should a bridge copy one anyway, the write lands in room this adapter
    /// reserves after the field and is detected there; either way the call is refused rather than
    /// reported as a success.
    ResultNameOverflowed,

    /// The thread a call into the library runs on could not be started. The library's interpreter
    /// needs more stack than the thread that called this adapter can be assumed to have, so every
    /// call runs on a thread this adapter starts with that stack stated, and the platform refused to
    /// start one. Reported as `ErrorCode::AllocationFailed`; nothing was asked of the library.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CallThreadNotStarted
};

/// What this adapter can do, in this build and in this process, right now.
///
/// Cheap to ask and safe to ask at any time, including before anything has been constructed: that
/// is what a discovery report is for. One member is a property of the build and does not change;
/// the rest describe the process and do.
struct GmicAdapterCapabilities
{
    /// Which of the four states this adapter is in.
    GmicAdapterState state = GmicAdapterState::Absent;

    /// Whether a G'MIC library is compiled into this build of the adapter at all.
    ///
    /// A property of the binary rather than of the process, so it answers the question a deployment
    /// asks - "is this the enabled build?" - without a caller inferring it from a state that also
    /// depends on what is running.
    bool libraryCompiledIn = false;

    /// Whether this process has run the library probe, successfully or not.
    ///
    /// The probe runs once, when the first session is opened, and is what tells a library that is
    /// linked apart from a library that works. Before it has run, `state` is `Available` and says
    /// nothing about whether the library would answer.
    bool probed = false;

    /// How many sessions this adapter has open in this process.
    ///
    /// A number rather than a flag because there is no seat to take: several sessions coexist, each
    /// holding its own engine context open, and what they share is one lock around the library.
    std::uint64_t openSessions = 0u;

    /// How many calls into the library are in progress in this process right now: at most one
    /// running, because this adapter serialises them, and the rest waiting for the lock that does so.
    ///
    /// A count rather than a flag, so that a caller reading a hang sees the queue as well as the call.
    std::uint64_t callsInFlight = 0u;

    /// The code of the last refusal after the library was asked to answer, or `ErrorCode::Ok` when
    /// there has been none. Set only in `GmicAdapterState::FailedToLoad`.
    ErrorCode loadFailure = ErrorCode::Ok;

    /// Which of this adapter's rules refused the last session, or `GmicAdapterReason::None`.
    GmicAdapterReason loadReason = GmicAdapterReason::None;
};

/// What this adapter can do, asked of the library rather than of an object.
///
/// The entry point a consumer calls before deciding whether to ask for an adapter at all. It starts
/// nothing, loads nothing, runs no probe and cannot fail: a build with no G'MIC library answers
/// `Absent`, and a build that has one answers `Available` until a session is opened or refused.
///
/// Thread-safe and allocation-free.
///
/// @return the state, the build fact, the session and call counts, and the last refusal in this
///         process.
[[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API GmicAdapterCapabilities gmicAdapterCapabilities() noexcept;

/// The state's own name, for a log line, a diagnostic field or a test failure message.
///
/// The spelling of the enumerator: `GmicAdapterState::FailedToLoad` is `"FailedToLoad"`.
///
/// Thread-safe and allocation-free.
///
/// @param state the state to name. A value outside the enumeration is named
///        `"UnknownGmicAdapterState"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API const char* gmicAdapterStateName(GmicAdapterState state) noexcept;

/// The reason's own name, which is what a person reads to find out which rule refused.
///
/// Thread-safe and allocation-free.
///
/// @param reason the reason to name. A value outside the enumeration is named
///        `"UnknownGmicAdapterReason"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API const char* gmicAdapterReasonName(GmicAdapterReason reason) noexcept;

/// Which of this adapter's rules a failure came from.
///
/// Reads the typed context field the adapter attaches, so a caller branches on a value instead of
/// parsing a message. A failure from anywhere else - the engine, another adapter - carries none and
/// is reported as `GmicAdapterReason::None`.
///
/// Thread-safe and allocation-free.
///
/// @param status the failure to read. A successful status carries no reason.
/// @return the reason, or `GmicAdapterReason::None` when the failure does not carry one - which is
///         also the answer for a value outside this build's enumeration, because a reason this build
///         cannot name is not one it can branch on.
[[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API GmicAdapterReason gmicAdapterReason(const Status& status) noexcept;

/// Whether a value is one of the states this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param state the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isGmicAdapterState(GmicAdapterState state) noexcept
{
    return static_cast<std::uint8_t>(state) <= static_cast<std::uint8_t>(GmicAdapterState::FailedToLoad);
}

/// Whether a value is one of the reasons this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param reason the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isGmicAdapterReason(GmicAdapterReason reason) noexcept
{
    return static_cast<std::uint16_t>(reason) <= static_cast<std::uint16_t>(GmicAdapterReason::CallThreadNotStarted);
}

} // namespace an
