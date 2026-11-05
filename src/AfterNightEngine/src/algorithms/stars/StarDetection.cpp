// AfterNightEngine - finding the stars in one frame and measuring each of them.
//
// Four stages, in this order: the detection channel becomes a Float32 working plane; that plane is
// measured and smoothed; the smoothed plane is scanned for local maxima that dominate a search box
// and are backed by enough bright neighbours in the unsmoothed plane; and every surviving candidate
// is fitted by `fitStarPsf` and either accepted or counted under a rejection reason.
//
// **The answer does not depend on the thread count.** The scan runs in bands and the bands are
// merged in scan order before duplicate rejection is applied, so the candidate list is the one a
// single-threaded scan would produce. The fits are stored by candidate index rather than by
// completion order. The accepted stars are ordered by magnitude with the candidate index breaking
// ties, so two stars of exactly equal brightness keep a stable order.
//
// **A sample the frame's validity rule rejects is replaced by the measured sky before the plane is
// smoothed.** Without it a no-data border would become a step in the smoothed surface and a single
// not-a-number would poison every candidate within two kernel widths of it. Substituting the sky is
// what makes the detector's answer near a border the same answer it
// would give on a frame that had been cropped there instead.
//
// **Derived from Siril's star finder.** The candidate scan and its rejections are adapted from the star
// finder of Siril (https://siril.org, `siril/src/algos/star_finder.c`), whose file states
// `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)` and
// `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. Siril's are the maximum over the search box and its tie rule, the
// bright-neighbour support read from the unsmoothed plane, the saturated-plateau edge walk, the
// second-derivative zero crossings that size the box and estimate the amplitude, the energy-density
// cut-off, the saturation fractions, the duplicate radius and the symmetry and residual rejections. The
// banded scan and its order, the sky substituted for a sample the validity rule rejects, the counted
// rejection reasons and the typed failures are this engine's. The provenance note is
// `docs/dev/STAR_DETECTION_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "algorithms/stars/StarDetectionSky.h"
#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace an
{

namespace
{

using stars_detail::failure;
using stars_detail::parameterFailure;

constexpr const char* kDetectOperation = "an::detectStars";

/// The trace category every stage of a detection reports under.
constexpr const char* kTraceCategory = "stars.detect";
constexpr const char* kFullWidthOperation = "an::measureStarFieldFullWidth";

/// How many rows one band of the candidate scan covers.
///
/// Thirty-two: large enough that the per-band bookkeeping disappears against the work, small enough
/// that a frame of any realistic height has far more bands than threads.
constexpr std::uint32_t kScanBandRows = 32u;

/// The fraction of a candidate's box radius within which a second candidate is the same star.
constexpr double kDuplicateRadiusFraction = 0.2;

/// The energy density at which a point-spread function's useful radius is cut off.
constexpr double kDensityCutoff = 0.001;

/// The largest ratio between the two halves of a width or amplitude estimate a candidate may have.
///
/// Two: a star's smoothed profile is symmetric about its own centre, so an estimate that is twice
/// as wide on one side as on the other is a blend of two sources or an edge.
constexpr double kMaxCandidateAsymmetry = 2.0;

/// The largest fraction of the fitted amplitude the residual may reach for an unsaturated star.
constexpr double kMaxResidualFraction = 0.2;

/// The square root of the base of natural logarithms, which the amplitude estimate scales by.
constexpr double kSqrtE = 1.6487212707;

/// One candidate the scan produced, before any fitting.
struct StarCandidate
{
    /// Column of the candidate's centre in the frame.
    std::int32_t x = 0;

    /// Row of the candidate's centre in the frame.
    std::int32_t y = 0;

    /// The mean of the bright neighbours around it, which ranks candidates for a star limit.
    double brightness = 0.0;

    /// The level at or above which the fit withholds a sample of this candidate's box.
    double saturationLevel = 0.0;

    /// Half-width estimate along a row, from the smoothed second derivative.
    double widthX = 0.0;

    /// Half-width estimate down a column.
    double widthY = 0.0;

    /// The box radius the fit will read.
    std::uint32_t radius = 0u;

    /// Whether the scan found a saturated plateau at this candidate.
    bool saturated = false;
};

/// Everything the candidate scan needs that does not change between rows.
struct ScanConstants
{
    /// The smoothed plane the local maxima are found in.
    const float* smoothed = nullptr;

    /// The unsmoothed working plane the bright-neighbour support is read from.
    const float* plane = nullptr;

    /// Both planes' width, which is also their row stride in samples.
    std::int32_t width = 0;

    /// Both planes' height.
    std::int32_t height = 0;

    /// The searched rectangle's first column.
    std::int32_t regionX0 = 0;

    /// The searched rectangle's first row.
    std::int32_t regionY0 = 0;

    /// One past the searched rectangle's last column.
    std::int32_t regionX1 = 0;

    /// One past the searched rectangle's last row.
    std::int32_t regionY1 = 0;

    /// The search radius a local maximum has to dominate.
    std::int32_t radius = 0;

    /// The measured sky plus the requested multiple of the background noise.
    double threshold = 0.0;

    /// That multiple of the noise alone, which an amplitude estimate is held to.
    double localThreshold = 0.0;

    /// The measured sky.
    double background = 0.0;

    /// The request's full-scale sample value.
    double fullScale = 0.0;

    /// The share of the dynamic range a bright neighbourhood's mean must reach to be a plateau.
    double minimumSaturationLevel = 0.0;

    /// The share of the dynamic range within which a plateau counts as flat.
    double saturationSpread = 0.0;

    /// The factor turning a half-width estimate into a box radius, from the energy cutoff.
    double densityFactor = 0.0;

    /// The smoothing the width estimates are measured under.
    double smoothingSigma = 0.0;

    /// The plan's cap on a derived box radius.
    std::uint32_t maximumBoxRadius = 0u;

    /// Whether the frame has more than one channel.
    bool colour = false;

    /// Whether the request relaxed the scan's symmetry and amplitude checks.
    bool relaxed = false;
};

/// Reads one sample of a tight single-channel plane.
[[nodiscard]] inline float at(const float* plane, std::int32_t width, std::int32_t x, std::int32_t y) noexcept
{
    return plane[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

/// Whether a candidate already listed claims the neighbourhood of a new one.
///
/// The list is walked backwards and the walk stops as soon as the listed candidate is further up
/// the frame than one box radius, which is what makes this linear in the candidates near a row
/// rather than in every candidate found so far.
[[nodiscard]] bool candidateIsDuplicate(std::int32_t x, std::int32_t y, std::uint32_t radius,
                                        const std::vector<StarCandidate>& listed) noexcept
{
    const auto matchRadius =
        (std::max)(std::int32_t{1}, static_cast<std::int32_t>(static_cast<double>(radius) * kDuplicateRadiusFraction));
    for (std::size_t index = listed.size(); index-- > 0u;)
    {
        const StarCandidate& candidate = listed[index];
        if (std::abs(x - candidate.x) + std::abs(y - candidate.y) <= matchRadius)
        {
            return true;
        }
        if (y - candidate.y > static_cast<std::int32_t>(radius))
        {
            break;
        }
    }
    return false;
}

/// The zero crossing of the smoothed second derivative walking outwards along one axis.
///
/// Returns both the half width the crossing implies and the amplitude estimate the first derivative
/// at that crossing implies. `step` is the direction, `along` selects the row or the column axis.
struct ProfileArm
{
    /// The signed half width, in pixels.
    double width = 0.0;

    /// The amplitude the first derivative at the crossing implies.
    double amplitude = 0.0;

    /// False when the walk ran into the frame's own edge before it crossed.
    bool usable = false;
};

/// Walks one arm of the profile outwards until the second derivative crosses zero upwards.
///
/// @param scan the scan's constants.
/// @param originX the centre column.
/// @param originY the centre row.
/// @param stepX one step along the walked axis.
/// @param stepY one step down the walked axis.
/// @param zeroCrossing the sub-pixel offset of the first derivative's own zero, subtracted from the
///        measured width so both arms are measured from the same origin.
/// @param saturationLevel the plateau level to walk past first, or a negative number for none.
/// @return the arm, or an unusable arm when the walk reached the frame edge.
[[nodiscard]] ProfileArm walkProfileArm(const ScanConstants& scan, std::int32_t originX, std::int32_t originY,
                                        std::int32_t stepX, std::int32_t stepY, double zeroCrossing,
                                        double saturationLevel) noexcept
{
    const auto sample = [&](std::int32_t offset) {
        return static_cast<double>(at(scan.smoothed, scan.width, originX + stepX * offset, originY + stepY * offset));
    };
    const std::int32_t limitX = stepX > 0 ? scan.width : (stepX < 0 ? -1 : 0);
    const std::int32_t limitY = stepY > 0 ? scan.height : (stepY < 0 ? -1 : 0);
    std::int32_t offset = 0;
    if (saturationLevel >= 0.0)
    {
        while (true)
        {
            const std::int32_t nextX = originX + stepX * (offset + 1);
            const std::int32_t nextY = originY + stepY * (offset + 1);
            if ((stepX > 0 && nextX >= limitX) || (stepX < 0 && nextX <= limitX) || (stepY > 0 && nextY >= limitY) ||
                (stepY < 0 && nextY <= limitY))
            {
                break;
            }
            if (!(sample(offset) > saturationLevel))
            {
                break;
            }
            ++offset;
        }
    }
    // Two samples of headroom past the crossing: the walk reads one beyond the candidate crossing
    // to know whether it is still falling.
    const std::int32_t reachX = originX + stepX * (offset + 2);
    const std::int32_t reachY = originY + stepY * (offset + 2);
    if (reachX < 1 || reachX >= scan.width - 1 || reachY < 1 || reachY >= scan.height - 1)
    {
        return ProfileArm{};
    }
    double curvature = sample(offset + 1) + sample(offset - 1) - 2.0 * sample(offset);
    double nextCurvature = sample(offset + 2) + sample(offset) - 2.0 * sample(offset + 1);
    const auto insideRegion = [&](std::int32_t steps) {
        const std::int32_t x = originX + stepX * steps;
        const std::int32_t y = originY + stepY * steps;
        return x > scan.regionX0 && x < scan.regionX1 - 1 && y > scan.regionY0 && y < scan.regionY1 - 1;
    };
    while (nextCurvature < 0.0 && insideRegion(offset + 2))
    {
        ++offset;
        curvature = nextCurvature;
        const std::int32_t aheadX = originX + stepX * (offset + 2);
        const std::int32_t aheadY = originY + stepY * (offset + 2);
        if (aheadX < 0 || aheadX >= scan.width || aheadY < 0 || aheadY >= scan.height)
        {
            break;
        }
        nextCurvature = sample(offset + 2) + sample(offset) - 2.0 * sample(offset + 1);
    }
    const double denominator = nextCurvature - curvature;
    const double crossing =
        denominator != 0.0 ? static_cast<double>(offset) - curvature / denominator : static_cast<double>(offset);
    const double slope = sample(offset) - sample(offset - 1);
    ProfileArm arm{};
    arm.width = crossing - zeroCrossing;
    arm.amplitude = -slope * arm.width * kSqrtE;
    arm.usable = true;
    return arm;
}

/// Scans one row of the smoothed plane and appends whatever candidates it holds.
///
/// The row scan, with its bounds tests: the local maximum over the search box with the plateau
/// tie-break that keeps the first in scan order, the bright-neighbour support count read from the
/// unsmoothed plane, the saturated-plateau edge walk that re-centres the candidate, the four
/// second-derivative arms that size the box, and the symmetry checks a relaxed request skips.
void scanRow(const ScanConstants& scan, std::int32_t y, std::vector<StarCandidate>& out)
{
    const std::int32_t radius = scan.radius;
    const std::int32_t boxSamples = (2 * radius + 1) * (2 * radius + 1);
    for (std::int32_t x = radius + scan.regionX0; x < scan.regionX1 - radius; ++x)
    {
        const double centre = static_cast<double>(at(scan.smoothed, scan.width, x, y));
        if (!(centre > scan.threshold))
        {
            continue;
        }
        bool accepted = true;
        std::int32_t counted = 0;
        for (std::int32_t neighbourY = y - radius; neighbourY <= y + radius && accepted; ++neighbourY)
        {
            for (std::int32_t neighbourX = x - radius; neighbourX <= x + radius; ++neighbourX)
            {
                if (neighbourX == x && neighbourY == y)
                {
                    continue;
                }
                const double neighbour = static_cast<double>(at(scan.smoothed, scan.width, neighbourX, neighbourY));
                if (neighbour > centre)
                {
                    accepted = false;
                    break;
                }
                if (neighbour == centre && ((neighbourX <= x && neighbourY <= y) || (neighbourX > x && neighbourY < y)))
                {
                    accepted = false;
                    break;
                }
                ++counted;
            }
        }
        if (!accepted || counted < boxSamples - 1)
        {
            continue;
        }

        // The centre dominates its box, so nothing within the next radius of columns can.
        const std::int32_t centreX = x;
        x += radius;

        double brightSum = 0.0;
        double brightMinimum = (std::numeric_limits<double>::max)();
        double unsmoothedCentre = 0.0;
        std::int32_t brightCount = 0;
        for (std::int32_t neighbourY = y - 1; neighbourY <= y + 1; ++neighbourY)
        {
            for (std::int32_t neighbourX = centreX - 1; neighbourX <= centreX + 1; ++neighbourX)
            {
                if (neighbourX == centreX && neighbourY == y)
                {
                    unsmoothedCentre = static_cast<double>(at(scan.plane, scan.width, neighbourX, neighbourY));
                    continue;
                }
                const double neighbour = static_cast<double>(at(scan.plane, scan.width, neighbourX, neighbourY));
                if (neighbour >= scan.threshold)
                {
                    brightMinimum = (std::min)(brightMinimum, neighbour);
                    brightSum += neighbour;
                    ++brightCount;
                }
            }
        }
        // A monochrome frame cannot leak between channels, so four adjacent bright samples are a
        // star; a demosaiced colour frame interpolates, so it takes a full block of nine.
        const std::int32_t requiredSupport = scan.colour ? 8 : 3;
        if (brightCount == 0 || brightCount < requiredSupport)
        {
            continue;
        }
        const double brightMean = brightSum / static_cast<double>(brightCount);

        std::int32_t peakX = centreX;
        std::int32_t peakY = y;
        double zeroCrossingX = -0.5;
        double zeroCrossingY = -0.5;
        double saturationLevel = scan.fullScale;
        bool saturated = false;
        if (peakX - 2 < scan.regionX0 + 1 || peakX + 2 > scan.regionX1 - 1 || peakY - 2 < scan.regionY0 + 1 ||
            peakY + 2 > scan.regionY1 - 1)
        {
            continue;
        }

        if (brightMean - scan.background < scan.minimumSaturationLevel ||
            unsmoothedCentre - brightMinimum > scan.saturationSpread)
        {
            const double left = centre - static_cast<double>(at(scan.smoothed, scan.width, peakX - 1, peakY));
            const double right = static_cast<double>(at(scan.smoothed, scan.width, peakX + 1, peakY)) - centre;
            const double up = centre - static_cast<double>(at(scan.smoothed, scan.width, peakX, peakY - 1));
            const double down = static_cast<double>(at(scan.smoothed, scan.width, peakX, peakY + 1)) - centre;
            zeroCrossingX = right != left ? -0.5 - left / (right - left) : -0.5;
            zeroCrossingY = down != up ? -0.5 - up / (down - up) : -0.5;
        }
        else
        {
            // A saturated core has no usable derivative at its centre, so the plateau's own outline
            // is walked instead and the candidate is re-centred on the middle of it.
            saturated = true;
            saturationLevel = (std::min)(unsmoothedCentre, scan.fullScale) - scan.saturationSpread;
            std::int32_t offsetX = 0;
            std::int32_t offsetY = 0;
            const auto plateau = [&](std::int32_t dx, std::int32_t dy) {
                return static_cast<double>(at(scan.smoothed, scan.width, peakX + dx, peakY + dy)) > saturationLevel;
            };
            while (peakX + offsetX < scan.regionX1 - 1 && plateau(offsetX, 0))
            {
                ++offsetX;
            }
            const auto walkSouthEast = [&]() {
                while (peakX + offsetX < scan.regionX1 - 1 && peakY + offsetY < scan.regionY1 - 1 &&
                       (plateau(offsetX + 1, offsetY + 1) || plateau(offsetX, offsetY + 1)))
                {
                    if (plateau(offsetX + 1, offsetY + 1))
                    {
                        ++offsetX;
                    }
                    ++offsetY;
                }
            };
            walkSouthEast();
            const std::int32_t east = offsetX;
            while (peakX + offsetX > scan.regionX0 + 1 && peakY + offsetY < scan.regionY1 - 1 &&
                   (plateau(offsetX - 1, offsetY + 1) || plateau(offsetX - 1, offsetY)))
            {
                if (plateau(offsetX - 1, offsetY + 1))
                {
                    ++offsetY;
                }
                --offsetX;
            }
            const std::int32_t south = offsetY;
            while (peakX + offsetX > scan.regionX0 + 1 && peakY + offsetY > scan.regionY0 + 1 &&
                   (plateau(offsetX - 1, offsetY - 1) || plateau(offsetX, offsetY - 1)))
            {
                if (plateau(offsetX - 1, offsetY - 1))
                {
                    --offsetX;
                }
                --offsetY;
            }
            const std::int32_t west = offsetX;
            while (peakX + offsetX < scan.regionX1 - 1 && peakY + offsetY > scan.regionY0 + 1 &&
                   (plateau(offsetX + 1, offsetY - 1) || plateau(offsetX + 1, offsetY)))
            {
                if (plateau(offsetX + 1, offsetY - 1))
                {
                    --offsetY;
                }
                ++offsetX;
            }
            const std::int32_t north = offsetY;
            walkSouthEast();
            const std::int32_t widest = (std::max)(offsetX, east);
            peakX += (widest + west) / 2;
            peakY += (north + south) / 2;
            x += widest;
            if (peakX - 3 < scan.regionX0 || peakX + 3 >= scan.regionX1 || peakY - 3 < scan.regionY0 ||
                peakY + 3 >= scan.regionY1)
            {
                continue;
            }
        }

        const double plateau = saturated ? saturationLevel : -1.0;
        const ProfileArm right = walkProfileArm(scan, peakX, peakY, 1, 0, zeroCrossingX, plateau);
        const ProfileArm left = walkProfileArm(scan, peakX, peakY, -1, 0, zeroCrossingX, plateau);
        const ProfileArm down = walkProfileArm(scan, peakX, peakY, 0, 1, zeroCrossingY, plateau);
        const ProfileArm up = walkProfileArm(scan, peakX, peakY, 0, -1, zeroCrossingY, plateau);
        if (!right.usable || !left.usable || !down.usable || !up.usable)
        {
            continue;
        }

        const double halfWidthX = 0.5 * (right.width + left.width);
        const double halfWidthY = 0.5 * (down.width + up.width);
        const double amplitudeX = 0.5 * (right.amplitude + left.amplitude);
        const double amplitudeY = 0.5 * (down.amplitude + up.amplitude);
        if (!std::isfinite(halfWidthX) || !std::isfinite(halfWidthY) || !(halfWidthX > 0.0) || !(halfWidthY > 0.0))
        {
            continue;
        }

        const auto boxFrom = [&](double half) {
            return static_cast<std::int32_t>(std::ceil(scan.densityFactor * half));
        };
        std::int32_t boxRadius = (std::max)(boxFrom(halfWidthX), boxFrom(halfWidthY));
        boxRadius = (std::min)(boxRadius, static_cast<std::int32_t>(scan.maximumBoxRadius));
        boxRadius = (std::max)(boxRadius, radius);
        boxRadius = (std::min)(boxRadius, peakX);
        boxRadius = (std::min)(boxRadius, scan.width - peakX - 1);
        boxRadius = (std::min)(boxRadius, peakY);
        boxRadius = (std::min)(boxRadius, scan.height - peakY - 1);
        if (boxRadius < 1)
        {
            continue;
        }

        if (!scan.relaxed)
        {
            const double amplitudeRatio = (std::max)(amplitudeX, amplitudeY) / (std::min)(amplitudeX, amplitudeY);
            const double widthRatioX = (std::max)(-left.width, right.width) / (std::min)(-left.width, right.width);
            const double widthRatioY = (std::max)(-up.width, down.width) / (std::min)(-up.width, down.width);
            if (!(amplitudeRatio <= kMaxCandidateAsymmetry) || !(widthRatioX <= kMaxCandidateAsymmetry) ||
                !(widthRatioY <= kMaxCandidateAsymmetry) || (std::max)(amplitudeX, amplitudeY) < scan.localThreshold)
            {
                continue;
            }
        }

        out.push_back(StarCandidate{.x = peakX,
                                    .y = peakY,
                                    .brightness = brightMean,
                                    .saturationLevel = saturated ? saturationLevel : scan.fullScale,
                                    .widthX = halfWidthX,
                                    .widthY = halfWidthY,
                                    .radius = static_cast<std::uint32_t>(boxRadius),
                                    .saturated = saturated});
    }
}

/// Decides whether one fitted candidate is a star, and says why when it is not.
///
/// @param fit what the fit produced.
/// @param request the detection's own thresholds.
/// @param candidate what the scan said about it.
/// @param dynamicRange full scale above the measured sky.
/// @param smoothingSigma the sigma the scan's width estimate was measured against.
/// @param colour whether the frame has more than one channel.
/// @param reason receives the reason when one is returned.
/// @return true when the candidate is a star.
[[nodiscard]] bool acceptStar(const PsfFitResult& fit, const StarDetectionRequest& request,
                              const StarCandidate& candidate, double dynamicRange, double smoothingSigma, bool colour,
                              StarRejectionReason& reason) noexcept
{
    const StarPsf& psf = fit.psf;
    if (!std::isfinite(psf.fullWidthX) || !std::isfinite(psf.fullWidthY))
    {
        reason = StarRejectionReason::NonFiniteWidth;
        return false;
    }
    if (!std::isfinite(psf.centreX) || !std::isfinite(psf.centreY))
    {
        reason = StarRejectionReason::NonFinitePosition;
        return false;
    }
    if (!fit.instrumentalMagnitude.has_value())
    {
        reason = StarRejectionReason::NoMagnitude;
        return false;
    }
    if (colour && (psf.fullWidthX <= 1.0 || psf.fullWidthY <= 1.0))
    {
        reason = StarRejectionReason::WidthBelowColourFloor;
        return false;
    }
    if (psf.fullWidthX <= 0.0 || psf.fullWidthY <= 0.0)
    {
        reason = StarRejectionReason::NonPositiveWidth;
        return false;
    }
    const double ratio = psf.fullWidthY / psf.fullWidthX;
    if (ratio < request.minimumRoundness || ratio > request.maximumRoundness)
    {
        reason = StarRejectionReason::RoundnessOutsideWindow;
        return false;
    }
    const double estimated = (std::max)(candidate.widthX, candidate.widthY);
    // The allowance widens with the estimate: a star much wider than the smoothing kernel is
    // measured by the scan less precisely, so holding it to the same fraction would reject the
    // brightest stars of every frame.
    const double allowance =
        estimated * kGaussianFullWidthPerSigma * (1.0 + 0.5 * std::log(estimated / smoothingSigma));
    if (!request.relaxedChecks && psf.fullWidthX > allowance)
    {
        reason = StarRejectionReason::WidthAboveEstimate;
        return false;
    }
    if (!request.relaxedChecks && psf.amplitude != 0.0 &&
        (psf.residualRootMeanSquare * request.noiseSigma / psf.amplitude) > kMaxResidualFraction &&
        !(psf.amplitude > dynamicRange))
    {
        // A saturated star keeps its place whatever its residual: it is wanted for alignment, and
        // the residual of a clipped core measures the clipping.
        reason = StarRejectionReason::ResidualTooLarge;
        return false;
    }
    if ((request.minimumAmplitude > 0.0 || request.maximumAmplitude > 0.0) &&
        (psf.amplitude < request.minimumAmplitude || psf.amplitude > request.maximumAmplitude))
    {
        reason = StarRejectionReason::AmplitudeOutsideWindow;
        return false;
    }
    if (request.model != PsfModel::Gaussian && psf.beta.has_value() && *psf.beta < request.minimumBeta)
    {
        reason = StarRejectionReason::BetaBelowMinimum;
        return false;
    }
    return true;
}

/// Rounds a byte count up to the engine's buffer alignment.
[[nodiscard]] std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) & ~(kDefaultBufferAlignmentBytes - 1u);
}

/// A block of engine-context memory released on every path out of the detection.
class ContextBlock
{
public:
    ContextBlock() noexcept = default;

    ContextBlock(const ContextBlock&) = delete;
    ContextBlock& operator=(const ContextBlock&) = delete;
    ContextBlock(ContextBlock&&) = delete;
    ContextBlock& operator=(ContextBlock&&) = delete;

    ~ContextBlock()
    {
        if (m_address != nullptr && m_memory != nullptr)
        {
            m_memory->deallocate(m_address, m_bytes, kDefaultBufferAlignmentBytes);
        }
    }

    /// Takes `bytes` from `memory`, or reports why it could not.
    [[nodiscard]] Status take(MemoryResource* memory, std::uint64_t bytes)
    {
        Result<void*> taken = memory->allocate(bytes, kDefaultBufferAlignmentBytes);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        m_memory = memory;
        m_address = *taken;
        m_bytes = bytes;
        return Status::success();
    }

    /// Where the block starts.
    [[nodiscard]] std::byte* bytes() const noexcept
    {
        return static_cast<std::byte*>(m_address);
    }

    /// How large it is.
    [[nodiscard]] std::uint64_t size() const noexcept
    {
        return m_bytes;
    }

private:
    /// Where the block came from.
    MemoryResource* m_memory = nullptr;

    /// The block, or null.
    void* m_address = nullptr;

    /// Its size.
    std::uint64_t m_bytes = 0u;
};

} // namespace

namespace
{

/// The detection, with its channel measured here or taken from the caller's measurement.
[[nodiscard]] Status detectStarsMeasuring(const ExecutionContext& execution, const StarFrameView& frame,
                                          const StarDetectionPlan& plan, const StarChannelStatistics* given,
                                          StarDetectionResult& out)
try
{
    if (!execution.valid())
    {
        return failure(kDetectOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!frame.valid())
    {
        return failure(kDetectOperation, ErrorCode::InvalidArgument, "the frame view holds nothing");
    }
    if (!plan.valid())
    {
        return failure(kDetectOperation, ErrorCode::InvalidArgument, "the plan was never built from a request");
    }
    const ImageExtent& extent = frame.descriptor().extent();
    if (plan.channelIndex() >= extent.channelCount() || plan.region().originX + plan.region().width > extent.width() ||
        plan.region().originY + plan.region().height > extent.height())
    {
        return failure(kDetectOperation, ErrorCode::IncompatibleArguments,
                       "the plan was built for a frame of another shape");
    }
    if (given != nullptr && given->channelIndex != plan.channelIndex())
    {
        return failure(kDetectOperation, ErrorCode::IncompatibleArguments,
                       "the channel measurement is of another channel than the plan's");
    }
    Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    execution.reportTrace(TraceEventKind::SpanBegin, kTraceCategory, "workingPlane");

    const StarDetectionRequest& request = plan.request();
    const std::uint32_t width = extent.width();
    const std::uint32_t height = extent.height();
    const std::uint64_t planeSamples = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t planeBytes = alignedBytes(planeSamples * sizeof(float));

    // A validity rule that rejects samples means the plane is substituted into, so it is copied
    // whatever its representation: nothing here writes to the caller's frame.
    const bool copyPlane = plan.copiesDetectionPlane() || !frame.validity().descriptor.isAllValid();
    const std::uint64_t planeTotalBytes = (copyPlane ? planeBytes : 0u) + planeBytes;

    MemoryResource* const memory = execution.memoryResource();
    ContextBlock planes;
    const Status takenPlanes = planes.take(memory, planeTotalBytes);
    if (takenPlanes.failed())
    {
        return takenPlanes;
    }
    std::byte* cursor = planes.bytes();
    float* workingPlane = nullptr;
    if (copyPlane)
    {
        workingPlane = reinterpret_cast<float*>(cursor);
        cursor += planeBytes;
    }
    auto* const smoothedPlane = reinterpret_cast<float*>(cursor);

    // The tight single-channel Float32 plane both the scan and the fit read.
    const std::optional<ImageExtent> planeExtent = ImageExtent::create(width, height, 1u, execution.limits());
    if (!planeExtent.has_value())
    {
        return failure(kDetectOperation, ErrorCode::Internal, "the working plane could not be described");
    }
    const std::optional<ImageLayout> planeLayout =
        ImageLayout::create(ImageLayoutRequest{.extent = *planeExtent,
                                               .elementSizeBytes = static_cast<std::uint32_t>(sizeof(float)),
                                               .samplesPerPixel = 1u,
                                               .planeCount = 1u,
                                               .rowAlignmentBytes = 1u},
                            execution.limits());
    if (!planeLayout.has_value())
    {
        return failure(kDetectOperation, ErrorCode::Internal, "the working plane could not be described");
    }

    const float* detectionPlane = nullptr;
    if (!copyPlane)
    {
        const std::uint64_t planeOffset = frame.layout().planeStrideBytes() * plan.channelIndex();
        detectionPlane = reinterpret_cast<const float*>(static_cast<const std::byte*>(frame.samples()) + planeOffset);
        if (frame.layout().rowStrideBytes() != planeLayout->rowStrideBytes())
        {
            // A padded row stride is the one shape a borrowed plane cannot serve, so it is copied
            // like every other representation rather than read with a second addressing rule.
            return failure(kDetectOperation, ErrorCode::Internal,
                           "a padded Float32 plane reached the borrowed-plane path");
        }
    }
    else
    {
        // One channel of the frame, converted into the working plane. A planar frame's channel is
        // already a single-channel image where it lies, so it converts in one pass; an interleaved
        // one is gathered first.
        const bool planar = frame.layout().samplesPerPixel() == 1u;
        if (planar)
        {
            const std::optional<ImageLayout> sourceLayout = ImageLayout::createWithStrides(
                ImageLayoutRequest{.extent = *planeExtent,
                                   .elementSizeBytes = frame.layout().elementSizeBytes(),
                                   .samplesPerPixel = 1u,
                                   .planeCount = 1u,
                                   .rowAlignmentBytes = 1u},
                frame.layout().rowStrideBytes(), frame.layout().planeStrideBytes(), execution.limits());
            if (!sourceLayout.has_value())
            {
                return failure(kDetectOperation, ErrorCode::Internal, "the detection channel could not be described");
            }
            const std::uint64_t planeOffset = frame.layout().planeStrideBytes() * plan.channelIndex();
            ConversionCounters counters;
            const Status converted = convertImageSamples(
                execution, frame.descriptor().sampleType(),
                static_cast<const std::byte*>(frame.samples()) + planeOffset, *sourceLayout, SampleType::Float32,
                workingPlane, *planeLayout, ConversionPolicy{.domain = DomainMapping::CodeValue}, counters);
            if (converted.failed())
            {
                return converted;
            }
        }
        else
        {
            // Gather the channel into the working plane's own bytes as the source representation,
            // then convert it in place through a second pass. The gathered bytes never exceed the
            // Float32 plane's, because no representation this surface reads is wider than four
            // bytes.
            const std::uint32_t elementSize = frame.layout().elementSizeBytes();
            if (elementSize > sizeof(float))
            {
                return failure(kDetectOperation, ErrorCode::Unsupported,
                               "an interleaved frame of this representation is not one this detector gathers");
            }
            const std::optional<ImageLayout> gatheredLayout =
                ImageLayout::create(ImageLayoutRequest{.extent = *planeExtent,
                                                       .elementSizeBytes = elementSize,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                    execution.limits());
            if (!gatheredLayout.has_value())
            {
                return failure(kDetectOperation, ErrorCode::Internal, "the gathered channel could not be described");
            }
            const Status extracted =
                extractImageChannel(execution, frame.descriptor().sampleType(), frame.samples(), frame.layout(),
                                    plan.channelIndex(), workingPlane, *gatheredLayout);
            if (extracted.failed())
            {
                return extracted;
            }
            if (frame.descriptor().sampleType() != SampleType::Float32)
            {
                // Convert backwards through the plane so a narrower source never overwrites a
                // sample it has not read yet.
                ConversionCounters counters;
                const Status converted = convertImageSamples(
                    execution, frame.descriptor().sampleType(), workingPlane, *gatheredLayout, SampleType::Float32,
                    smoothedPlane, *planeLayout, ConversionPolicy{.domain = DomainMapping::CodeValue}, counters);
                if (converted.failed())
                {
                    return converted;
                }
                std::memcpy(workingPlane, smoothedPlane, static_cast<std::size_t>(planeSamples * sizeof(float)));
            }
        }
        detectionPlane = workingPlane;
    }

    running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    execution.reportProgress(1u, 4u, "star detection: working plane");
    execution.reportTrace(TraceEventKind::SpanEnd, kTraceCategory, "workingPlane");

    // The three figures a detection reads, measured on the working plane in three parallel passes.
    // The complete channel measurement is a separate call and is taken only when the request asks
    // for the deviation and the robust scale as well, because those two are most of its cost and no
    // part of a detection reads either - or it is the caller's, taken beforehand.
    execution.reportTrace(TraceEventKind::SpanBegin, kTraceCategory, "statistics");
    StarChannelStatistics measured{};
    measured.channelIndex = plan.channelIndex();
    if (given != nullptr)
    {
        measured = *given;
    }
    else if (request.measureRobustScale)
    {
        const Result<StarChannelStatistics> complete =
            measureStarChannel(execution, frame, plan.channelIndex(), plan.region());
        if (!complete.hasValue())
        {
            return complete.status();
        }
        measured = *complete;
    }
    else
    {
        KernelValidityView channelValidity = frame.validity();
        if (channelValidity.mask != nullptr && !channelValidity.mask->isShared())
        {
            channelValidity.maskPlane = plan.channelIndex();
        }
        const Result<stars_detail::DetectionSky> sky =
            stars_detail::measureDetectionSky(execution, detectionPlane, width, height, plan.region(), channelValidity);
        if (!sky.hasValue())
        {
            return sky.status();
        }
        measured.background = sky->median;
        measured.backgroundNoise = sky->backgroundNoise;
        measured.maximum = sky->maximum;
        measured.admittedSamples = sky->admittedSamples;
    }
    const StarChannelStatistics* const channel = &measured;
    if (!std::isfinite(channel->background) || !std::isfinite(channel->backgroundNoise) ||
        !(channel->backgroundNoise >= 0.0))
    {
        return failure(kDetectOperation, ErrorCode::InvalidData,
                       "the detection channel's measured sky is not a number a threshold can be built from");
    }

    // Samples the frame's validity rule rejects stand in at the measured sky, so neither a no-data
    // border nor a single not-a-number reaches the smoothing.
    if (!frame.validity().descriptor.isAllValid())
    {
        const auto sky = static_cast<float>(channel->background);
        const ValidityDescriptor& rule = frame.validity().descriptor;
        const ValidityMaskView<const std::uint8_t>* const mask = frame.validity().mask;
        const std::uint32_t maskPlane = mask != nullptr && !mask->isShared() ? plan.channelIndex() : 0u;
        const Status substituted = parallelFor(
            execution, height, TaskCost{.bytesPerItem = width * sizeof(float), .operationsPerItem = width},
            [&](TaskRange rows) {
                for (std::uint64_t row = rows.begin; row < rows.end; ++row)
                {
                    float* const line = workingPlane + row * width;
                    for (std::uint32_t column = 0u; column < width; ++column)
                    {
                        const bool valid =
                            mask != nullptr
                                ? mask->test(maskPlane, column, static_cast<std::uint32_t>(row)).value_or(false)
                                : sampleIsValidByValue(rule, line[column]);
                        if (!valid)
                        {
                            line[column] = sky;
                        }
                    }
                }
                return Status::success();
            });
        if (substituted.failed())
        {
            return substituted;
        }
    }

    execution.reportTrace(TraceEventKind::SpanEnd, kTraceCategory, "statistics");

    // The smoothed surface the candidate scan runs against.
    execution.reportTrace(TraceEventKind::SpanBegin, kTraceCategory, "smoothing");
    const SpatialSelection smoothing{};
    Result<GaussianPlan> blurPlan =
        request.smoothing == StarDetectionSmoothing::Gaussian
            ? planGaussianBlur(GaussianRequest{.sigmaX = request.smoothingSigma, .sigmaY = request.smoothingSigma})
            : planRealtimePreviewGaussianBlur(
                  RealtimePreviewGaussianRequest{.sigmaX = request.smoothingSigma, .sigmaY = request.smoothingSigma});
    if (!blurPlan.hasValue())
    {
        return blurPlan.status();
    }
    const Result<SpatialFilterResult> blurred =
        request.smoothing == StarDetectionSmoothing::Gaussian
            ? blurImageGaussian(execution, SampleType::Float32, detectionPlane, *planeLayout, smoothedPlane,
                                *planeLayout, *blurPlan, smoothing)
            : blurImageRealtimePreviewGaussian(execution, SampleType::Float32, detectionPlane, *planeLayout,
                                               smoothedPlane, *planeLayout, *blurPlan, smoothing);
    if (!blurred.hasValue())
    {
        return blurred.status();
    }
    execution.reportProgress(2u, 4u, "star detection: smoothed surface");
    execution.reportTrace(TraceEventKind::SpanEnd, kTraceCategory, "smoothing");

    execution.reportTrace(TraceEventKind::SpanBegin, kTraceCategory, "candidateScan");
    ScanConstants scan{};
    scan.smoothed = smoothedPlane;
    scan.plane = detectionPlane;
    scan.width = static_cast<std::int32_t>(width);
    scan.height = static_cast<std::int32_t>(height);
    scan.regionX0 = static_cast<std::int32_t>(plan.region().originX);
    scan.regionY0 = static_cast<std::int32_t>(plan.region().originY);
    scan.regionX1 = static_cast<std::int32_t>(plan.region().originX + plan.region().width);
    scan.regionY1 = static_cast<std::int32_t>(plan.region().originY + plan.region().height);
    scan.radius = static_cast<std::int32_t>(request.searchRadius);
    const double thresholdSigmas = kStarDetectionThresholdSigmas * request.noiseSigma;
    scan.threshold = channel->background + thresholdSigmas * channel->backgroundNoise;
    scan.localThreshold = thresholdSigmas * channel->backgroundNoise;
    scan.background = channel->background;
    scan.fullScale = request.fullScale;
    const double dynamicRange = (std::min)(channel->maximum, request.fullScale) - channel->background;
    scan.minimumSaturationLevel = dynamicRange * kDefaultStarSaturationFraction;
    scan.saturationSpread = dynamicRange * kDefaultStarSaturationPlateauFraction;
    scan.densityFactor = std::sqrt(-2.0 * std::log(kDensityCutoff));
    scan.smoothingSigma = request.smoothingSigma;
    scan.maximumBoxRadius = plan.maximumBoxRadius();
    scan.colour = extent.channelCount() > 1u;
    scan.relaxed = request.relaxedChecks;

    const std::int32_t firstRow = scan.regionY0 + scan.radius;
    const std::int32_t lastRow = scan.regionY1 - scan.radius;
    const std::uint64_t bandCount =
        lastRow > firstRow ? (static_cast<std::uint64_t>(lastRow - firstRow) + kScanBandRows - 1u) / kScanBandRows : 0u;
    std::vector<std::vector<StarCandidate>> bands(static_cast<std::size_t>(bandCount));
    const Status scanned =
        parallelFor(execution, bandCount,
                    TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * kScanBandRows * sizeof(float) * 2u,
                             .operationsPerItem = static_cast<std::uint64_t>(width) * kScanBandRows *
                                                  (2u * request.searchRadius + 1u) * (2u * request.searchRadius + 1u)},
                    [&](TaskRange range) {
                        for (std::uint64_t band = range.begin; band < range.end; ++band)
                        {
                            const Status stillRunning = execution.checkCancellation();
                            if (stillRunning.failed())
                            {
                                return stillRunning;
                            }
                            const std::int32_t begin = firstRow + static_cast<std::int32_t>(band * kScanBandRows);
                            const std::int32_t end =
                                (std::min)(lastRow, begin + static_cast<std::int32_t>(kScanBandRows));
                            std::vector<StarCandidate>& out = bands[static_cast<std::size_t>(band)];
                            for (std::int32_t row = begin; row < end; ++row)
                            {
                                scanRow(scan, row, out);
                            }
                        }
                        return Status::success();
                    });
    if (scanned.failed())
    {
        return scanned;
    }

    // Merged in scan order, so duplicate rejection and the frame ceiling see the sequence a
    // single-threaded scan would have produced.
    std::vector<StarCandidate> candidates;
    std::uint64_t rawCandidates = 0u;
    std::uint64_t duplicates = 0u;
    bool ceilingReached = false;
    for (const std::vector<StarCandidate>& band : bands)
    {
        rawCandidates += band.size();
        for (const StarCandidate& candidate : band)
        {
            if (candidates.size() >= kMaxStarsPerFrame)
            {
                ceilingReached = true;
                break;
            }
            if (!candidates.empty() && candidateIsDuplicate(candidate.x, candidate.y, candidate.radius, candidates))
            {
                ++duplicates;
                continue;
            }
            candidates.push_back(candidate);
        }
        if (ceilingReached)
        {
            break;
        }
    }
    bands.clear();
    bands.shrink_to_fit();
    execution.reportProgress(3u, 4u, "star detection: candidates");
    execution.reportTrace(TraceEventKind::SpanEnd, kTraceCategory, "candidateScan");

    StarDetectionResult produced{};
    produced.report.rawCandidates = rawCandidates;
    produced.report.duplicateCandidates = duplicates;
    produced.report.candidates = candidates.size();
    produced.report.candidateCeilingReached = ceilingReached;

    // The fit scratch is taken after the scan and sized by the largest box the frame actually
    // produced, rather than by the ceiling the plan allowed for: a frame whose widest star is nine
    // pixels across must not reserve the four hundred-sample box a saturated one would have needed.
    std::uint32_t widestCandidate = 1u;
    for (const StarCandidate& candidate : candidates)
    {
        widestCandidate = (std::max)(widestCandidate, candidate.radius);
    }
    const std::uint64_t boxSide = 2ull * widestCandidate + 1ull;
    const std::uint64_t scratchDoubles = psfFitScratchLength(widestCandidate) + boxSide * boxSide;
    const std::uint64_t perWorkerBytes = alignedBytes(scratchDoubles * sizeof(double));
    const std::uint32_t workers = (std::max)(execution.admittedConcurrency(), 1u);
    ContextBlock fitScratch;
    if (!candidates.empty())
    {
        const Status takenScratch = fitScratch.take(memory, perWorkerBytes * workers);
        if (takenScratch.failed())
        {
            return takenScratch;
        }
    }
    auto* const workerScratch = reinterpret_cast<double*>(fitScratch.bytes());
    produced.report.peakWorkingBytes = planes.size() + fitScratch.size();

    // The fitted candidates, kept by candidate index so that the completion order of the workers
    // cannot reach the answer.
    execution.reportTrace(TraceEventKind::SpanBegin, kTraceCategory, "psfFit");
    std::vector<std::optional<DetectedStar>> fitted(candidates.size());
    std::vector<std::uint32_t> ranked(candidates.size());
    std::iota(ranked.begin(), ranked.end(), 0u);
    const auto brighterFirst = [&candidates](std::uint32_t left, std::uint32_t right) {
        const double a = candidates[left].brightness;
        const double b = candidates[right].brightness;
        return a != b ? a > b : left < right;
    };
    const std::uint32_t starLimit = request.maximumStars.value_or(0u);
    const bool limited = starLimit > 0u;
    std::size_t rankedReady = 0u;
    if (!limited)
    {
        std::sort(ranked.begin(), ranked.end(), brighterFirst);
        rankedReady = ranked.size();
    }

    std::atomic<std::uint64_t> fitFailures{0u};
    std::atomic<std::uint64_t> unconverged{0u};
    std::atomic<std::uint32_t> largestBox{0u};
    std::array<std::atomic<std::uint64_t>, kStarRejectionReasonCount> rejections{};
    const bool colourFrame = scan.colour;
    const std::size_t perRound = limited ? static_cast<std::size_t>(starLimit) + starLimit / 4u : candidates.size();
    std::size_t accepted = 0u;
    std::size_t round = 0u;
    std::size_t upper = 0u;
    std::vector<std::uint32_t> orderedAccepted;
    do
    {
        const std::size_t lower = round * perRound;
        upper = (std::min)(candidates.size(), (round + 1u) * perRound);
        if (lower >= upper)
        {
            break;
        }
        if (limited && upper > rankedReady)
        {
            if (upper == candidates.size())
            {
                std::sort(ranked.begin(), ranked.end(), brighterFirst);
            }
            else
            {
                std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(upper), ranked.end(),
                                  brighterFirst);
            }
            rankedReady = upper;
        }

        // Largest boxes first inside a round: a saturated star costs orders of magnitude more than
        // a typical candidate, and starting those early keeps the workers balanced. The results are
        // stored by candidate index, so the order changes nothing.
        std::vector<std::uint32_t> order(upper - lower);
        std::iota(order.begin(), order.end(), static_cast<std::uint32_t>(lower));
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t left, std::uint32_t right) {
            return candidates[ranked[left]].radius > candidates[ranked[right]].radius;
        });

        const Status fittedRound = parallelForWorkers(
            execution, order.size(),
            TaskCost{.bytesPerItem = boxSide * boxSide * sizeof(double), .operationsPerItem = boxSide * boxSide * 64u},
            [&](WorkerTask task) {
                double* const scratch =
                    workerScratch + static_cast<std::uint64_t>(task.workerSlot) * (perWorkerBytes / sizeof(double));
                double* const box = scratch + psfFitScratchLength(widestCandidate);
                for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
                {
                    const Status stillRunning = execution.checkCancellation();
                    if (stillRunning.failed())
                    {
                        return stillRunning;
                    }
                    const std::uint32_t candidateIndex = ranked[order[static_cast<std::size_t>(index)]];
                    const StarCandidate& candidate = candidates[candidateIndex];
                    const std::uint32_t radius = candidate.radius;
                    const std::uint64_t side = 2ull * radius + 1ull;
                    for (std::uint64_t row = 0u; row < side; ++row)
                    {
                        const float* const source =
                            detectionPlane +
                            (static_cast<std::uint64_t>(candidate.y - static_cast<std::int32_t>(radius)) + row) *
                                width +
                            static_cast<std::uint64_t>(candidate.x - static_cast<std::int32_t>(radius));
                        double* const destination = box + row * side;
                        for (std::uint64_t column = 0u; column < side; ++column)
                        {
                            destination[column] = static_cast<double>(source[column]);
                        }
                    }
                    const PsfFitRequest fitRequest{.model = request.model,
                                                   .fixedBeta = request.minimumBeta,
                                                   .background = channel->background,
                                                   .saturationLevel = candidate.saturationLevel,
                                                   .convergence = request.convergence,
                                                   .parameterTolerance = kDefaultPsfFitTolerance,
                                                   .gradientTolerance = kDefaultPsfFitTolerance,
                                                   .arcsecondsPerPixel = request.arcsecondsPerPixel};
                    const Result<PsfFitResult> fit = fitStarPsf(
                        execution, std::span<const double>{box, static_cast<std::size_t>(side * side)}, radius,
                        fitRequest, std::span<double>{scratch, psfFitScratchLength(widestCandidate)});
                    if (!fit.hasValue())
                    {
                        if (fit.status().code() == ErrorCode::Cancelled ||
                            fit.status().code() == ErrorCode::DeadlineExceeded)
                        {
                            return fit.status();
                        }
                        fitFailures.fetch_add(1u, std::memory_order_relaxed);
                        continue;
                    }
                    if (fit->report.outcome != PsfFitOutcome::Converged)
                    {
                        // The solver stopped without a convergence test passing, so the star is
                        // discarded: a star measured by a fit that never settled has a width nobody
                        // should rank a frame by.
                        unconverged.fetch_add(1u, std::memory_order_relaxed);
                        continue;
                    }
                    StarRejectionReason reason = StarRejectionReason::NonFiniteWidth;
                    if (!acceptStar(*fit, request, candidate, dynamicRange, request.smoothingSigma, colourFrame,
                                    reason))
                    {
                        rejections[static_cast<std::size_t>(reason)].fetch_add(1u, std::memory_order_relaxed);
                        continue;
                    }

                    DetectedStar star{};
                    star.psf = fit->psf;
                    star.uncertainty = fit->uncertainty;
                    star.metrics = fit->metrics;
                    star.instrumentalMagnitude = fit->instrumentalMagnitude;
                    star.channelIndex = plan.channelIndex();
                    star.psf.saturationLevel = candidate.saturationLevel;
                    star.psf.boxRadius = radius;
                    star.psf.saturated = fit->psf.amplitude > dynamicRange;
                    star.positionX =
                        static_cast<double>(candidate.x - static_cast<std::int32_t>(radius)) + fit->psf.centreX;
                    star.positionY =
                        static_cast<double>(candidate.y - static_cast<std::int32_t>(radius)) + fit->psf.centreY;
                    if (channel->backgroundNoise > 0.0 && fit->psf.amplitude > 0.0)
                    {
                        star.signalToNoise = fit->psf.amplitude / channel->backgroundNoise;
                    }
                    fitted[candidateIndex] = star;
                    std::uint32_t previous = largestBox.load(std::memory_order_relaxed);
                    while (previous < radius &&
                           !largestBox.compare_exchange_weak(previous, radius, std::memory_order_relaxed))
                    {
                    }
                }
                return Status::success();
            });
        if (fittedRound.failed())
        {
            return fittedRound;
        }
        produced.report.fittedCandidates += order.size();

        for (std::size_t index = lower; index < upper; ++index)
        {
            if (limited && accepted >= starLimit)
            {
                break;
            }
            const std::uint32_t candidateIndex = ranked[index];
            if (fitted[candidateIndex].has_value())
            {
                orderedAccepted.push_back(candidateIndex);
                ++accepted;
            }
        }
        ++round;
    } while (limited && accepted < starLimit && upper < candidates.size());

    // Brightest first, with the candidate index breaking a tie, so the list is the same list on
    // every run and at every admitted width.
    std::stable_sort(orderedAccepted.begin(), orderedAccepted.end(),
                     [&fitted](std::uint32_t left, std::uint32_t right) {
                         const std::optional<double>& a = fitted[left]->instrumentalMagnitude;
                         const std::optional<double>& b = fitted[right]->instrumentalMagnitude;
                         if (a.has_value() && b.has_value() && *a != *b)
                         {
                             return *a < *b;
                         }
                         if (a.has_value() != b.has_value())
                         {
                             return a.has_value();
                         }
                         return left < right;
                     });
    produced.stars.reserve(orderedAccepted.size());
    for (const std::uint32_t candidateIndex : orderedAccepted)
    {
        produced.stars.push_back(*fitted[candidateIndex]);
    }

    produced.report.fitFailures = fitFailures.load(std::memory_order_relaxed);
    produced.report.unconvergedFits = unconverged.load(std::memory_order_relaxed);
    produced.report.largestBoxRadius = largestBox.load(std::memory_order_relaxed);
    for (std::size_t reason = 0u; reason < kStarRejectionReasonCount; ++reason)
    {
        produced.report.rejections[reason] = rejections[reason].load(std::memory_order_relaxed);
    }

    execution.reportTrace(TraceEventKind::SpanEnd, kTraceCategory, "psfFit");
    Result<StarFieldSummary> summary = summarizeStarField(execution, produced.stars, measured);
    if (!summary.hasValue())
    {
        return summary.status();
    }
    produced.summary = *summary;

    running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    execution.reportProgress(4u, 4u, "star detection: measured stars");
    out = std::move(produced);
    return Status::success();
}
catch (const std::bad_alloc&)
{
    return Status::failure(ErrorCode::AllocationFailed);
}

} // namespace

Status detectStars(const ExecutionContext& execution, const StarFrameView& frame, const StarDetectionPlan& plan,
                   StarDetectionResult& out)
{
    return detectStarsMeasuring(execution, frame, plan, nullptr, out);
}

Status detectStars(const ExecutionContext& execution, const StarFrameView& frame, const StarDetectionPlan& plan,
                   const StarChannelStatistics& measured, StarDetectionResult& out)
{
    return detectStarsMeasuring(execution, frame, plan, &measured, out);
}

Result<double> measureStarFieldFullWidth(const ExecutionContext& execution, const StarFrameView& frame,
                                         const StarDetectionRequest& request, std::optional<std::uint32_t> channel)
{
    if (!frame.valid())
    {
        return failure(kFullWidthOperation, ErrorCode::InvalidArgument, "the frame view holds nothing");
    }
    StarDetectionRequest seeing = request;
    if (!seeing.maximumStars.has_value())
    {
        seeing.maximumStars = kDefaultFullWidthStarLimit;
    }
    const Result<StarDetectionPlan> plan = planStarDetection(seeing, frame.descriptor(), frame.layout(), channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    StarDetectionResult detected;
    const Status ran = detectStars(execution, frame, *plan, detected);
    if (ran.failed())
    {
        return ran;
    }
    if (!detected.summary.medianFullWidth.has_value())
    {
        return failure(kFullWidthOperation, ErrorCode::EmptySelection,
                       "the frame yielded no star with a width the seeing can be read from");
    }
    return *detected.summary.medianFullWidth;
}

} // namespace an
