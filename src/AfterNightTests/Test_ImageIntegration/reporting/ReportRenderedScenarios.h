// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the suites of the report's document share: each scenario's report, built once.
///
/// `ReportHtmlWriterTest` is the fixture of every case that reads a scenario's document back. It builds a
/// scenario, archives its run as the executor does, builds its model and writes its document in both forms the
/// first time a case asks, and keeps the result for the cases that follow, in whichever unit they are.

#include <gtest/gtest.h>

#include "ReportDocumentTestSupport.h"
#include "ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/ReportAssets.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace an::tests::integration::report
{
/// A scenario's report: the run archived as the executor archives it, its model, and its document in both forms.
struct Rendered
{
    ReportScenario scenario;
    ImageIntegration::Reporting::ReportModel model;
    ImageIntegration::Reporting::ReportDocument linked;
    ImageIntegration::Reporting::ReportDocument embedded;
    ParsedDocument linkedParsed;
    ParsedDocument embeddedParsed;
};

/// The scenarios a report is written of: every one but the two runs the executor archives nothing for.
[[nodiscard]] inline std::vector<ReportScenarioId> reportedScenarios()
{
    std::vector<ReportScenarioId> ids;
    for (const ReportScenarioId id : allReportScenarios())
    {
        if (id != ReportScenarioId::CancelledBeforeAnyMaster && id != ReportScenarioId::FailedBeforeAnyMaster)
        {
            ids.push_back(id);
        }
    }
    return ids;
}

/// The elements directly inside another.
[[nodiscard]] inline std::vector<const DocumentElement*> childrenOf(const ParsedDocument& parsed,
                                                                    const DocumentElement& parent)
{
    std::vector<const DocumentElement*> children;
    const int index = static_cast<int>(&parent - parsed.elements.data());
    for (const DocumentElement& element : parsed.elements)
    {
        if (element.parent == index)
        {
            children.push_back(&element);
        }
    }
    return children;
}

/// The first element of a name inside another, however deep; null when there is none.
[[nodiscard]] inline const DocumentElement* firstInside(const ParsedDocument& parsed, const DocumentElement& ancestor,
                                                        const char* name)
{
    for (const DocumentElement& element : parsed.elements)
    {
        if (element.name == QLatin1String(name) && parsed.isInside(element, ancestor))
        {
            return &element;
        }
    }
    return nullptr;
}

/// The elements of a name inside another, however deep, in document order.
[[nodiscard]] inline std::vector<const DocumentElement*> namedInside(const ParsedDocument& parsed,
                                                                     const DocumentElement& ancestor, const char* name)
{
    std::vector<const DocumentElement*> found;
    for (const DocumentElement& element : parsed.elements)
    {
        if (element.name == QLatin1String(name) && parsed.isInside(element, ancestor))
        {
            found.push_back(&element);
        }
    }
    return found;
}

/// The element that holds another; null for the root.
[[nodiscard]] inline const DocumentElement* parentOf(const ParsedDocument& parsed, const DocumentElement& element)
{
    return element.parent < 0 ? nullptr : &parsed.elements[static_cast<std::size_t>(element.parent)];
}

/// Whether a run stopped before its end.
[[nodiscard]] inline bool stopped(const ImageIntegration::Reporting::ReportModel& model)
{
    using ImageIntegration::Reporting::ReportOutcome;
    return model.run.outcome == ReportOutcome::Partial || model.run.outcome == ReportOutcome::Failed ||
           model.run.outcome == ReportOutcome::Cancelled;
}

/// The fixture of the cases that read a scenario's document back. The suite builds each scenario's report once,
/// when a case first asks for it.
class ReportHtmlWriterTest : public testing::Test
{
protected:
    static void TearDownTestSuite()
    {
        rendered().clear();
    }

    /// A scenario's report; null, with the case failed, when it could not be built.
    [[nodiscard]] static const Rendered* reportOf(const ReportScenarioId id)
    {
        using namespace ImageIntegration::Reporting;
        auto& cache = rendered();
        const auto known = cache.find(id);
        if (known != cache.end())
        {
            return known->second.get();
        }
        std::optional<ReportScenario> scenario = buildReportScenario(id);
        if (!scenario.has_value())
        {
            ADD_FAILURE() << "the scenario " << scenarioName(id) << " could not be built";
            return nullptr;
        }
        if (!archiveScenarioRun(*scenario))
        {
            ADD_FAILURE() << "the scenario " << scenarioName(id) << " could not be archived";
            return nullptr;
        }
        auto report = std::make_unique<Rendered>();
        report->model = buildScenarioModel(*scenario, scenario->runDirectory);
        report->linked =
            writeReportDocument(report->model, {.form = ReportForm::Linked, .runDirectory = scenario->runDirectory});
        report->embedded =
            writeReportDocument(report->model, {.form = ReportForm::Embedded, .runDirectory = scenario->runDirectory});
        report->linkedParsed = parseDocument(report->linked.html);
        report->embeddedParsed = parseDocument(report->embedded.html);
        report->scenario = std::move(*scenario);
        return cache.emplace(id, std::move(report)).first->second.get();
    }

private:
    [[nodiscard]] static std::map<ReportScenarioId, std::unique_ptr<Rendered>>& rendered()
    {
        static std::map<ReportScenarioId, std::unique_ptr<Rendered>> cache;
        return cache;
    }
};
} // namespace an::tests::integration::report
