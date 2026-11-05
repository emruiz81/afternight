// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowReportSectionBuilders.h"

namespace ImageIntegration::Reporting
{
WorkflowReportSummarySectionBuilder::WorkflowReportSummarySectionBuilder(Sections sections)
    : m_sections(std::move(sections))
{
}

void WorkflowReportSummarySectionBuilder::appendAll(QString& markdown) const
{
    if (m_sections.executiveSummary)
        m_sections.executiveSummary(markdown);
    if (m_sections.summary)
        m_sections.summary(markdown);
    if (m_sections.acquisitionSummary)
        m_sections.acquisitionSummary(markdown);
    if (m_sections.equipmentUsed)
        m_sections.equipmentUsed(markdown);
    if (m_sections.runFocus)
        m_sections.runFocus(markdown);
    if (m_sections.workflowTiming)
        m_sections.workflowTiming(markdown);
    if (m_sections.workflowCounters)
        m_sections.workflowCounters(markdown);
    if (m_sections.pipelineStages)
        m_sections.pipelineStages(markdown);
    if (m_sections.stackedMasters)
        m_sections.stackedMasters(markdown);
    if (m_sections.calibrationSummary)
        m_sections.calibrationSummary(markdown);
    if (m_sections.registrationSummary)
        m_sections.registrationSummary(markdown);
    if (m_sections.fullProcessingParameters)
        m_sections.fullProcessingParameters(markdown);
}

WorkflowReportAnalysisSectionBuilder::WorkflowReportAnalysisSectionBuilder(Sections sections)
    : m_sections(std::move(sections))
{
}

void WorkflowReportAnalysisSectionBuilder::appendAll(QString& markdown) const
{
    if (m_sections.qualityGate)
        m_sections.qualityGate(markdown);
    if (m_sections.frameQualityWeighting)
        m_sections.frameQualityWeighting(markdown);
    if (m_sections.targetSummaries)
        m_sections.targetSummaries(markdown);
    if (m_sections.comparedToPreviousRunDeltas)
        m_sections.comparedToPreviousRunDeltas(markdown);
    if (m_sections.metadataCompletenessScore)
        m_sections.metadataCompletenessScore(markdown);
    if (m_sections.reproducibilityFingerprint)
        m_sections.reproducibilityFingerprint(markdown);
    if (m_sections.failureDiagnostics)
        m_sections.failureDiagnostics(markdown);
    if (m_sections.inputFiles)
        m_sections.inputFiles(markdown);
    if (m_sections.warnings)
        m_sections.warnings(markdown);
    if (m_sections.technicalAppendix)
        m_sections.technicalAppendix(markdown);
}
} // namespace ImageIntegration::Reporting
