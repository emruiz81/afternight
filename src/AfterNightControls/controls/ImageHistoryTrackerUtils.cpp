// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageHistoryTrackerUtils.h"
#include "ImageHistoryTracker.h"
#include "ImageHistoryLineage.h"

namespace an::ui::history
{

std::string formatHistoryLog(const ImageHistoryTracker& tracker)
{
    std::string log;

    const auto& lineage = tracker.lineage();
    if (!lineage.empty())
    {
        log += "==============================================================================\n";
        log += " INHERITED PROCESSING HISTORY (from parent images, read-only)\n";
        log += "==============================================================================\n\n";
        log += lineage.formatAsText();
        log += "\n";
    }

    log += "==============================================================================\n";
    log += " CURRENT IMAGE PROCESSING HISTORY\n";
    log += "==============================================================================\n\n";

    const auto initialStep = tracker.getInitialStep();
    if (initialStep)
    {
        log +=
            fmt::format("[0] {} - {} ({})\n", initialStep->m_CreationTime.toString("yyyy-MM-dd hh:mm:ss").toStdString(),
                        initialStep->getStepName(), initialStep->getStepDesc());
    }

    const auto steps = tracker.getImageHistorySteps();
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const auto& s = steps[i];
        const bool current = (i + 1 == tracker.getCurrentStep());
        log += fmt::format("[{}]{} {} - {} ({})\n", i + 1, current ? " *" : "",
                           s->m_CreationTime.toString("yyyy-MM-dd hh:mm:ss").toStdString(), s->getStepName(),
                           s->getStepDesc());
    }

    return log;
}

ImageHistoryLineage inheritedLineage(const ImageHistoryTracker& source)
{
    // The source's own lineage, then every step it recorded, in order.
    ImageHistoryLineage lineage;
    lineage.appendFrom(source.lineage());

    const auto append = [&lineage](const ImageHistoryStepSPtr& step) {
        LineageEntry entry;
        entry.step_name = step->getStepName();
        entry.description = step->getStepDesc();
        entry.image_name = step->m_ImageName;
        entry.timestamp = step->m_CreationTime;
        lineage.appendEntry(entry);
    };

    if (const auto initialStep = source.getInitialStep())
    {
        append(initialStep);
    }
    for (const auto& step : source.getImageHistorySteps())
    {
        append(step);
    }

    return lineage;
}

} // namespace an::ui::history
