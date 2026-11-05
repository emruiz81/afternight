// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a pipelined stage divides the run's width between its reader, its own operation and its writers.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StageFramePipeline.h"

namespace stage_utils
{

PipelineWidths pipelineWidths(const std::uint32_t runWidth, const bool writes) noexcept
{
    PipelineWidths widths;
    if (runWidth < 4u)
    {
        widths.compute = (std::max)(1u, runWidth);
        return widths;
    }
    widths.overlapped = true;
    widths.reader = (std::min)(4u, (std::max)(1u, runWidth / 8u));
    widths.writers = writes ? (runWidth >= 16u ? 2u : 1u) : 0u;
    widths.compute = runWidth - widths.reader - widths.writers;
    return widths;
}

} // namespace stage_utils
