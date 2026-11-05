// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The services one workflow run lends its stages.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"

#include <AfterNightEngine/IO/CodecRegistry.h>

#include <algorithm>
#include <utility>

namespace ImageIntegration
{

WorkflowStageServices::WorkflowStageServices(const an::consumer::EngineSession& session,
                                             const std::uint32_t parallelCores, WorkflowRawPolicy rawPolicy,
                                             std::shared_ptr<const an::CodecRegistry> codecRegistry,
                                             std::stop_token stop) noexcept
    : m_session(&session), m_parallelCores(parallelCores), m_rawPolicy(rawPolicy),
      m_codecRegistry(std::move(codecRegistry)), m_stop(std::move(stop))
{
}

const an::consumer::EngineSession& WorkflowStageServices::session() const noexcept
{
    return *m_session;
}

std::uint32_t WorkflowStageServices::parallelCores() const noexcept
{
    return m_parallelCores;
}

std::uint32_t WorkflowStageServices::runWidth() const noexcept
{
    const std::uint32_t sessionWidth = m_session->width();
    if (m_parallelCores == 0u)
    {
        return sessionWidth;
    }
    return sessionWidth == 0u ? m_parallelCores : (std::min)(m_parallelCores, sessionWidth);
}

const WorkflowRawPolicy& WorkflowStageServices::rawPolicy() const noexcept
{
    return m_rawPolicy;
}

const std::shared_ptr<const an::CodecRegistry>& WorkflowStageServices::codecRegistry() const noexcept
{
    return m_codecRegistry;
}

std::stop_token WorkflowStageServices::stopToken() const noexcept
{
    return m_stop;
}

an::consumer::OperationRequest WorkflowStageServices::stageOperationRequest(an::ProgressSink* progress,
                                                                            an::DiagnosticSink* diagnostics) const
{
    an::consumer::OperationRequest request;
    request.purpose = an::consumer::OperationPurpose::WorkflowStage;
    request.maxConcurrency = m_parallelCores;
    request.stopToken = m_stop;
    request.progressSink = progress;
    request.diagnosticSink = diagnostics;
    return request;
}

an::Result<an::consumer::OperationScope>
WorkflowStageServices::admitStageOperation(an::ProgressSink* progress, an::DiagnosticSink* diagnostics) const
{
    return m_session->admit(stageOperationRequest(progress, diagnostics));
}

an::Result<an::consumer::OperationScope>
WorkflowStageServices::admitStageOperationAtWidth(const std::uint32_t width, an::ProgressSink* progress,
                                                  an::DiagnosticSink* diagnostics) const
{
    an::consumer::OperationRequest request = stageOperationRequest(progress, diagnostics);
    request.maxConcurrency = (std::max)(1u, width);
    return m_session->admit(request);
}

} // namespace ImageIntegration
