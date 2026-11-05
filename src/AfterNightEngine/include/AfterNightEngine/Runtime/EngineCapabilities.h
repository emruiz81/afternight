// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the loaded build offers, answerable before anything is constructed.
///
/// A caller decides how to configure an engine context from what the build and the machine can do,
/// so needing a context in order to ask would be a cycle. Everything here is therefore a free
/// function over compile-time constants and one query of the host, and a context reports the same
/// values afterwards for the caller that already holds one.
///
/// The report is deliberately coarse. It says which build this is and what the machine offers, not
/// which instruction sets the processor supports: that finer question belongs to the dispatch table
/// that acts on it, along with the ability to force a backend, and answering it in two places is how
/// two answers start to disagree.
///
/// The header is kept apart from the context for a measured reason. It carries the version header
/// and nothing else, so a caller that only wants to know what it is talking to does not pay for the
/// result and failure machinery a context's interface needs.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Version.h>

#include <cstdint>

namespace an
{

/// What the loaded engine build offers, independently of any context built from it.
///
/// Immutable for the life of the process: every member is either compiled into the binary or a
/// property of the machine it is running on, so two calls in one run report the same values.
///
/// Every member carries a default, so a caller may build one with designated initialisers - in a
/// test double, or in a report - without naming the members it does not set.
struct EngineCapabilities
{
    /// The version of the loaded library, as the headers it was built from declared it. Compare it
    /// against `engineHeaderVersion()` to find a caller compiled against a different revision;
    /// `engineMatchesHeaders()` is that comparison written out.
    EngineVersion version{kEngineVersionMajor, kEngineVersionMinor, kEngineVersionPatch};

    /// The binary-interface generation of the loaded library. See `kEngineAbiGeneration`.
    int abiGeneration = kEngineAbiGeneration;

    /// How many threads the host reports it can run at once.
    ///
    /// Never zero: the platform query this comes from is allowed to answer zero when it cannot
    /// tell, and one is reported instead, because a caller that resolved a concurrency setting
    /// from a zero would configure an engine to run nothing. It is a fact about the machine, so it
    /// is reported unclamped and may exceed `kMaxConfigurableConcurrency` on a very large host.
    std::uint32_t hostConcurrency = 1u;

    /// Whether implicit GPU dispatch is available. Always false: `ComputeBackend::Gpu` is reserved.
    /// Explicit device backends reached through `Algorithms/Kernel/Device.h` are independent and
    /// report their capabilities on the device instance.
    bool gpuBackend = false;
};

/// What the loaded build offers.
///
/// Thread-safe, reentrant, and allocation-free; it cannot fail. Callable before any engine object
/// exists, which is the point of it - the values are what a caller configures a context from.
///
/// @return the version, binary-interface generation, host concurrency, and optional-module
///         presence of the binary that is running.
[[nodiscard]] AFTERNIGHTENGINE_API EngineCapabilities engineCapabilities() noexcept;

} // namespace an
