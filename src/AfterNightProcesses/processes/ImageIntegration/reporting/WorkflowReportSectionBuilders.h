// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>

#include <functional>

namespace ImageIntegration::Reporting
{
class WorkflowReportSummarySectionBuilder
{
public:
    using AppendFn = std::function<void(QString&)>;

    struct Sections
    {
        AppendFn executiveSummary;
        AppendFn summary;
        AppendFn acquisitionSummary;
        AppendFn equipmentUsed;
        AppendFn runFocus;
        AppendFn workflowTiming;
        AppendFn workflowCounters;
        AppendFn pipelineStages;
        AppendFn stackedMasters;
        AppendFn calibrationSummary;
        AppendFn registrationSummary;
        AppendFn fullProcessingParameters;
    };

    explicit WorkflowReportSummarySectionBuilder(Sections sections);
    void appendAll(QString& markdown) const;

private:
    Sections m_sections;
};

class WorkflowReportAnalysisSectionBuilder
{
public:
    using AppendFn = std::function<void(QString&)>;

    struct Sections
    {
        AppendFn qualityGate;
        AppendFn frameQualityWeighting;
        AppendFn targetSummaries;
        AppendFn comparedToPreviousRunDeltas;
        AppendFn metadataCompletenessScore;
        AppendFn reproducibilityFingerprint;
        AppendFn failureDiagnostics;
        AppendFn inputFiles;
        AppendFn warnings;
        AppendFn technicalAppendix;
    };

    explicit WorkflowReportAnalysisSectionBuilder(Sections sections);
    void appendAll(QString& markdown) const;

private:
    Sections m_sections;
};
} // namespace ImageIntegration::Reporting
