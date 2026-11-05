// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the preview gamma rows, beside the prepared application.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace an::bench
{

/// The parameter naming what a preview gamma row's clock holds; a row that does not state it times the
/// application of a table built before the clock.
inline constexpr const char* kPreviewGammaBoundaryParameter = "timedBoundary";
/// The table's application alone, over a table built before the clock.
inline constexpr const char* kPreviewGammaApplication = "application";
/// The table built and applied, which is what the product's preview does every time its gamma moves.
inline constexpr const char* kPreviewGammaTableAndApplication = "table-and-application";
/// The table's preparation alone; it is applied to the row's ramp after the clock, to score it.
inline constexpr const char* kPreviewGammaTable = "table";
/// The entries of the preview table, which the table row counts as its work.
inline constexpr std::uint32_t kPreviewGammaTableEntries = 1024u;

/// The preview gamma rows derived from the prepared application over the synthetic frame.
///
/// The product's real-time preview builds its table again whenever the gamma moves and then applies
/// it, so a preview costs both. These rows time the parts apart and together: the same application
/// over each capture, whose sixteen-bit codes are carried into the unit interval before the clock;
/// the table built and applied, over the synthetic frame and each capture; and the table built alone,
/// scored by applying it to a ramp of its own width after the clock. Each keeps the source row's
/// gamma, table and tolerance.
///
/// @param preview the prepared application row the runner declares over the synthetic frame.
/// @return the six derived rows, in a fixed order.
inline std::vector<ScenarioSpec> previewGammaScenarios(const ScenarioSpec& preview)
{
    std::vector<ScenarioSpec> result;
    const std::pair<const char*, const char*> captures[] = {{"eagle", "eagle.qhy533m.ha.light"},
                                                            {"carina", "carina.asi294mm.red.light"}};
    for (const char* boundary : {kPreviewGammaApplication, kPreviewGammaTableAndApplication})
    {
        const bool complete = std::string(boundary) == kPreviewGammaTableAndApplication;
        const std::string stem =
            complete ? "pointwise.gamma_transfer.preview.complete" : "pointwise.gamma_transfer.preview";
        if (complete)
        {
            ScenarioSpec synthetic = preview;
            synthetic.id = stem + ".f32";
            synthetic.parameters[kPreviewGammaBoundaryParameter] = boundary;
            result.push_back(std::move(synthetic));
        }
        for (const auto& [name, corpusId] : captures)
        {
            const bool wide = std::string(name) == "carina";
            ScenarioSpec captured = preview;
            captured.id = stem + "." + name + ".f32";
            captured.inputSource = InputSource::Corpus;
            captured.corpusId = corpusId;
            captured.input.width = wide ? 8288u : 3008u;
            captured.input.height = wide ? 5644u : 3004u;
            captured.input.sampleType = SampleType::UInt16;
            captured.parameters["samples"] = "codes-over-65535";
            if (complete)
            {
                captured.parameters[kPreviewGammaBoundaryParameter] = boundary;
            }
            captured.elementsProcessed = std::uint64_t{captured.input.width} * captured.input.height;
            captured.bytesProcessed = captured.elementsProcessed * sizeof(float);
            result.push_back(std::move(captured));
        }
    }
    ScenarioSpec table = preview;
    table.id = "pointwise.gamma_transfer.preview.table";
    table.corpusId = "synthetic.ramp";
    table.input.width = kPreviewGammaTableEntries;
    table.input.height = 1u;
    table.parameters[kPreviewGammaBoundaryParameter] = kPreviewGammaTable;
    table.elementsProcessed = kPreviewGammaTableEntries;
    table.bytesProcessed = table.input.totalBytes();
    table.workUnit = "table-entries";
    result.push_back(std::move(table));
    return result;
}

} // namespace an::bench
