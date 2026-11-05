// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star filter the star-profiler windows share: which of a detected star list a person keeps, by ranges over the
/// stars' fitted figures, a random share of them, and the neighbours of the ones kept.
///
/// There is one detector and one record, `an::DetectedStar`; the two filter windows differ only in the three figures
/// they offer. The filter is a pure function of the list and the request - the random share is drawn from a stated seed
/// - so the same request keeps the same stars on every run and every platform.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace an::ui
{

/// A detected star list as the star windows hand it to one another.
using StarList = std::vector<an::DetectedStar>;

/// A star list shared without being copied; whoever changes one makes a new list.
using StarListPtr = std::shared_ptr<const StarList>;

/// A figure of a detected star a range can hold.
enum class StarFilterField : std::uint8_t
{
    /// The fitted peak above the background, in the frame's numeric domain.
    Amplitude,

    /// The brightness-weighted roundness figure: the amplitude over one tenth plus the two widths' difference.
    Circularity,

    /// Half the mean of the two full widths, in pixels.
    Radius,

    /// The instrumental magnitude; a star without one is held by no magnitude range.
    Magnitude,

    /// The major full width, in pixels.
    FullWidthX,

    /// The minor full width, in pixels.
    FullWidthY
};

/// A star's figure.
///
/// @param star the star.
/// @param field which figure.
/// @return the figure; nothing for a magnitude the star has none of.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<double> starFilterValue(const an::DetectedStar& star,
                                                                           StarFilterField field) noexcept;

/// The smallest and largest of a figure over a list.
struct StarFilterLimits
{
    /// The smallest.
    double minimum = 0.0;

    /// The largest.
    double maximum = 0.0;
};

/// The limits of a figure over a list.
///
/// @param stars the list.
/// @param field which figure.
/// @return the limits over the stars that have the figure; nothing when none has it.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<StarFilterLimits>
starFilterLimits(std::span<const an::DetectedStar> stars, StarFilterField field) noexcept;

/// A closed range one figure must lie in.
struct StarFilterRange
{
    /// Which figure.
    StarFilterField field = StarFilterField::Amplitude;

    /// The smallest value kept.
    double lowest = 0.0;

    /// The largest value kept.
    double highest = 0.0;
};

/// What a filter keeps.
struct StarFilterRequest
{
    /// The ranges a star must lie in, all of them; none keeps every star.
    std::vector<StarFilterRange> ranges;

    /// The share of the stars the ranges keep that is kept, in percent: chosen at random from `seed`; a hundred keeps
    /// them all.
    double keepPercent = 100.0;

    /// The random choice's seed.
    std::uint64_t seed = 0u;

    /// Whether a star the ranges or the random choice left out is kept when its box overlaps a kept star's box, and so
    /// on from the stars kept that way.
    bool includeNeighbours = false;

    /// The scale of the boxes: a box reaches two full widths times this from the star's centre along each axis.
    double scaleFactor = 1.0;
};

/// Whether a request changes anything: a range or a share below a hundred.
///
/// @param request the request.
/// @return true when the filter may leave a star out.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool starFilterApplies(const StarFilterRequest& request) noexcept;

/// Filters a list.
///
/// @param stars the list.
/// @param request what is kept.
/// @return the indices of the stars kept, in the list's order.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<std::size_t> filterStars(std::span<const an::DetectedStar> stars,
                                                                          const StarFilterRequest& request);

/// The stars a filter kept, as a new list.
///
/// @param stars the list.
/// @param request what is kept.
/// @return the stars kept, in the list's order; the list itself when the request changes nothing.
[[nodiscard]] AFTERNIGHTCONTROLS_API StarListPtr filteredStarList(const StarListPtr& stars,
                                                                  const StarFilterRequest& request);

} // namespace an::ui
