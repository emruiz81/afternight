// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What every consumer fault scenario shares: a session of the product's kind built over the sweep's injecting
/// resource, the codes a consumer path may answer an injected fault with, and the fixture files a scenario reads,
/// written before its sweep and outside it.
///
/// A consumer scenario is the engine's sweep over the product's own paths - the history's snapshots, the integration's
/// published images and documents, the standalone stack, a sequence's publication, a view's read and an extension's
/// buffer import. The engine's harness is reused as it is: it builds a context over an injecting resource and arms it,
/// and a consumer subject builds its session over the same resource through `EngineSessionSettings::memoryResource`,
/// so every block the session's context takes is one the sweep counts, refuses or stops at, and one the harness checks
/// came back once the subject has destroyed its session.

#pragma once

#include "Test_ConsumerFaults/FrameFiles.h"
#include "Test_EngineFaults/FaultHarness.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <QString>

#include <cstdint>
#include <filesystem>

namespace an::tests::consumerfaults
{

/// A session over a run's injecting resource, with the FITS and TIFF codecs the fixtures and the external hand-off
/// need. Everything the session's context allocates comes from that resource, so the sweep counts it.
///
/// @param run the run the sweep built.
/// @param width the session's concurrency.
/// @return the session, or what opening it refused - an injected refusal among them.
[[nodiscard]] an::Result<an::consumer::EngineSession> openSession(an::faults::RunContext& run,
                                                                  std::uint32_t width = 2u);

/// A session over the platform's memory, for what a scenario prepares before its sweep.
///
/// @return the session, or what opening it refused.
[[nodiscard]] an::Result<an::consumer::EngineSession> openSetupSession();

/// The codes a consumer path may answer a refused allocation with: the refusal itself, a budget refusal, and - where
/// a path reads what it allocated through a codec - the codec's report of a buffer it could not have.
///
/// @param sweep the sweep that accepts them.
void acceptRefusals(an::faults::Sweep& sweep);

/// The codes a consumer path may answer a stop with.
///
/// @param sweep the sweep that accepts them.
void acceptStops(an::faults::Sweep& sweep);

/// A path as Qt spells it.
///
/// @param path the path.
/// @return the same path, as a QString.
[[nodiscard]] QString qtPath(const std::filesystem::path& path);

} // namespace an::tests::consumerfaults
