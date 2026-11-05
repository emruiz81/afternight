// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - private window binding for the stable reduction primitive.
//
// A rectangle of a plane is reduced where it lies, as a layout whose origin is the rectangle's, so
// its packed mask - which still answers for the whole plane - has to be read at an offset. This
// header carries that offset, and the optional value interval that narrows the selection, to the
// shared implementation.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>

#include <cstdint>

namespace an::detail
{

/// How a selected source window maps into a packed mask.
struct ReductionWindow
{
    /// First mask column corresponding to source column zero.
    std::uint32_t maskOriginX = 0u;

    /// First mask row corresponding to source row zero.
    std::uint32_t maskOriginY = 0u;

    /// Whether the mask must have exactly the source extent rather than merely contain the window.
    bool requireExactMaskExtent = true;

    /// Whether a closed value interval narrows the selection beyond its validity rule.
    bool hasValueWindow = false;

    /// The interval's inclusive lower endpoint, read only when one is present.
    double lowestValue = 0.0;

    /// The interval's inclusive upper endpoint, read only when one is present.
    double highestValue = 0.0;

    /// Operation name attached to failures raised below the public entry point. Never null.
    const char* operation = "an::reducePlaneStatistics";
};

/// Computes the reduction primitive with a source window mapped into a containing packed mask.
///
/// The one implementation behind both public entry points. `reducePlaneStatistics` calls it with
/// the default window and an exact mask extent; `reducePlaneStatisticsOver` describes its
/// rectangle as an already-windowed layout over the caller's samples, with no copy, and passes the
/// rectangle's origin in the mask and its value interval here.
///
/// @param execution the operation policy and context lease.
/// @param source the selected source component and its already-windowed layout.
/// @param validity the value rule and optional containing mask.
/// @param window the source-to-mask mapping and failure identity.
/// @return the stable primitive result or the same failures as the public kernel entry point.
[[nodiscard]] Result<ReductionStatistics> reducePlaneStatisticsWindow(const ExecutionContext& execution,
                                                                      const KernelPlaneSource& source,
                                                                      const KernelValidityView& validity,
                                                                      const ReductionWindow& window);

} // namespace an::detail
