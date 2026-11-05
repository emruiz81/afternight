// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What a workflow run lends each of its stages: the session they run their engine work in, how many of its threads
/// the run may use, the RAW treatment and codec registry the run reads under, and the run's stop.
///
/// A run neither caps the thread pool of the whole process for its length - the viewer and every dialog included - nor
/// rewrites the engine's process-wide RAW options. Neither is possible on the engine and neither is needed: a stage
/// admits each of its operations through the session with the run's parallel-cores setting as the operation's width, so
/// the run uses no more threads than the person allowed and everything else keeps the rest, and the RAW treatment is a
/// snapshot the run took when it started.

#include "processes/ImageIntegration/workflow/WorkflowRawLoadPolicy.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <memory>
#include <stop_token>

namespace an
{
class CodecRegistry;
class DiagnosticSink;
class ProgressSink;
} // namespace an

namespace ImageIntegration
{

/// The services one run lends its stages. Borrowed by every stage for the run and released when it ends.
class AFTERNIGHTPROCESSES_API WorkflowStageServices
{
public:
    /// The services of one run.
    ///
    /// @param session the session the stages run their engine work in; it outlives the run.
    /// @param parallelCores the most threads one stage operation may use; zero for as many as its purpose may have.
    /// @param rawPolicy the RAW treatment the run took when it started.
    /// @param codecRegistry the registry built from it, which every read of the run names.
    /// @param stop the run's stop.
    WorkflowStageServices(const an::consumer::EngineSession& session, std::uint32_t parallelCores,
                          WorkflowRawPolicy rawPolicy, std::shared_ptr<const an::CodecRegistry> codecRegistry,
                          std::stop_token stop) noexcept;

    /// The session the stages run their engine work in.
    [[nodiscard]] const an::consumer::EngineSession& session() const noexcept;

    /// The most threads one stage operation may use; zero for as many as its purpose may have.
    [[nodiscard]] std::uint32_t parallelCores() const noexcept;

    /// The threads the run may use at once: the parallel-cores setting, or the session's width when it states none.
    /// A stage that overlaps several operations of its own divides this between them.
    [[nodiscard]] std::uint32_t runWidth() const noexcept;

    /// The RAW treatment the run reads under.
    [[nodiscard]] const WorkflowRawPolicy& rawPolicy() const noexcept;

    /// The registry the run reads through, built from its RAW treatment.
    [[nodiscard]] const std::shared_ptr<const an::CodecRegistry>& codecRegistry() const noexcept;

    /// The run's stop.
    [[nodiscard]] std::stop_token stopToken() const noexcept;

    /// The request a stage operation is admitted with: purpose `WorkflowStage`, the run's width, the run's stop.
    ///
    /// @param progress the operation's progress sink, borrowed.
    /// @param diagnostics the operation's diagnostic sink, borrowed.
    /// @return the request.
    [[nodiscard]] an::consumer::OperationRequest stageOperationRequest(an::ProgressSink* progress = nullptr,
                                                                       an::DiagnosticSink* diagnostics = nullptr) const;

    /// Admits one stage operation, waiting for its threads; a stage calls this on its worker, never on the GUI thread.
    ///
    /// @param progress the operation's progress sink, borrowed.
    /// @param diagnostics the operation's diagnostic sink, borrowed.
    /// @return the admitted operation, or the session's refusal - a stop the run asked for among them.
    [[nodiscard]] an::Result<an::consumer::OperationScope>
    admitStageOperation(an::ProgressSink* progress = nullptr, an::DiagnosticSink* diagnostics = nullptr) const;

    /// Admits one stage operation at a stated share of the run's width, waiting for its threads; for the operations a
    /// stage overlaps, whose shares together are the run's width.
    ///
    /// @param width the most threads the operation may use; at least one, and no more than the run's width.
    /// @param progress the operation's progress sink, borrowed.
    /// @param diagnostics the operation's diagnostic sink, borrowed.
    /// @return the admitted operation, or the session's refusal - a stop the run asked for among them.
    [[nodiscard]] an::Result<an::consumer::OperationScope>
    admitStageOperationAtWidth(std::uint32_t width, an::ProgressSink* progress = nullptr,
                               an::DiagnosticSink* diagnostics = nullptr) const;

private:
    const an::consumer::EngineSession* m_session;
    std::uint32_t m_parallelCores;
    WorkflowRawPolicy m_rawPolicy;
    std::shared_ptr<const an::CodecRegistry> m_codecRegistry;
    std::stop_token m_stop;
};

} // namespace ImageIntegration
