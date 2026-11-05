// AfterNightEngine - the compiled AMaZE estimate, one per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsAmaze.cpp`, derived from RawTherapee's
// AMaZE as librtprocess carries it (`demosaic/amaze.cc`), which states
// `copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>` and
// `optimized for speed by Ingo Weyrich`, and is distributed under the GNU General Public License,
// version 3 or any later version; this file is part of that derived work and is distributed under the
// same licence. What is here is AfterNight's: the lane-wise spelling of the reference's passes in its
// order of evaluation. The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// The passes of the AMaZE authority whose decisions can be taken lane by lane are compiled here for
// every packaged target, each in the reference's exact order of evaluation: the normalization, the
// first gradient field, the colour differences, the vertical and the horizontal bounds on them, and the
// direction weights. A data-dependent decision is taken as a selection between both of its results,
// and a recurrence along a row runs one row per lane. Later passes consume those identical values.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/DemosaicAmazeCpu.h"

#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicAmazeCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace demosaic_amaze_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Which rung of the ladder this compilation of the file belongs to.
///
/// @return the instruction set this body was compiled for.
[[nodiscard]] InstructionSet isaOfCompiledAmaze() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// The `DemosaicAmazeVectorOps::initialize` pass: the padded tile divided by the white level, without
/// a clamp, into the mosaic plane and the green working plane.
void initializeAmaze(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                     std::uint64_t columns, float whiteLevel, float* cfa, float* rgbGreen) noexcept
{
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto scale = hn::Set(lanes, whiteLevel);
    for (std::uint64_t row = 0u; row < rows; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const float* const source = tile.source + static_cast<std::uint64_t>(y) * tile.sourceStride + padBeginX;
        const std::uint64_t rowBase = row * columns;
        std::uint64_t column = 0u;
        for (; column + width <= columns; column += width)
        {
            const auto measured = hn::Div(hn::LoadU(lanes, source + column), scale);
            hn::StoreU(measured, lanes, cfa + rowBase + column);
            hn::StoreU(measured, lanes, rgbGreen + rowBase + column);
        }
        for (; column < columns; ++column)
        {
            const float measured = source[column] / whiteLevel;
            cfa[rowBase + column] = measured;
            rgbGreen[rowBase + column] = measured;
        }
    }
}

/// The `DemosaicAmazeVectorOps::gradients` pass: the vertical and horizontal gradient weights and
/// the summed squared spans, two samples in from every edge, in the reference's order of addition.
void gradientsAmaze(const float* cfa, std::uint64_t rows, std::uint64_t columns, float* gradientSum,
                    float* verticalWeights, float* horizontalWeights) noexcept
{
    constexpr float kEpsilon = 1e-5f;
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto epsilon = hn::Set(lanes, kEpsilon);
    const std::uint64_t v1 = columns;
    const std::uint64_t v2 = 2u * columns;
    for (std::uint64_t row = 2u; row < rows - 2u; ++row)
    {
        std::uint64_t column = 2u;
        for (; column + width <= columns - 2u; column += width)
        {
            const std::uint64_t index = row * columns + column;
            const auto centre = hn::LoadU(lanes, cfa + index);
            const auto horizontalSpan =
                hn::Abs(hn::Sub(hn::LoadU(lanes, cfa + index + 1u), hn::LoadU(lanes, cfa + index - 1u)));
            const auto verticalSpan =
                hn::Abs(hn::Sub(hn::LoadU(lanes, cfa + index + v1), hn::LoadU(lanes, cfa + index - v1)));
            const auto vertical =
                hn::Add(hn::Add(hn::Add(epsilon, hn::Abs(hn::Sub(hn::LoadU(lanes, cfa + index + v2), centre))),
                                hn::Abs(hn::Sub(centre, hn::LoadU(lanes, cfa + index - v2)))),
                        verticalSpan);
            const auto horizontal =
                hn::Add(hn::Add(hn::Add(epsilon, hn::Abs(hn::Sub(hn::LoadU(lanes, cfa + index + 2u), centre))),
                                hn::Abs(hn::Sub(centre, hn::LoadU(lanes, cfa + index - 2u)))),
                        horizontalSpan);
            hn::StoreU(vertical, lanes, verticalWeights + index);
            hn::StoreU(horizontal, lanes, horizontalWeights + index);
            hn::StoreU(hn::Add(hn::Mul(horizontalSpan, horizontalSpan), hn::Mul(verticalSpan, verticalSpan)), lanes,
                       gradientSum + index);
        }
        for (; column < columns - 2u; ++column)
        {
            const std::uint64_t index = row * columns + column;
            const float horizontalSpan = std::fabs(cfa[index + 1u] - cfa[index - 1u]);
            const float verticalSpan = std::fabs(cfa[index + v1] - cfa[index - v1]);
            verticalWeights[index] = kEpsilon + std::fabs(cfa[index + v2] - cfa[index]) +
                                     std::fabs(cfa[index] - cfa[index - v2]) + verticalSpan;
            horizontalWeights[index] = kEpsilon + std::fabs(cfa[index + 2u] - cfa[index]) +
                                       std::fabs(cfa[index] - cfa[index - 2u]) + horizontalSpan;
            gradientSum[index] = horizontalSpan * horizontalSpan + verticalSpan * verticalSpan;
        }
    }
}

/// The `DemosaicAmazeVectorOps::colourDifferences` pass, lane by lane in the reference's order of
/// evaluation. The reference's `std::min` keeps its first argument unless the second is smaller, and so
/// does the selection here; the last vector of a row stores only the samples the reference writes,
/// because the passes after this one read the zeros beyond them.
void colourDifferencesAmaze(const std::uint8_t* rowStartsGreen, std::uint64_t rows, std::uint64_t columns,
                            float highlightPoint, const float* cfa, const float* verticalWeights,
                            const float* horizontalWeights, float* verticalDifference, float* horizontalDifference,
                            float* verticalAlternate, float* horizontalAlternate, float* verticalSpread,
                            float* horizontalSpread) noexcept
{
    const hn::ScalableTag<float> lanes;
    const hn::RebindToSigned<decltype(lanes)> integers;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto epsilon = hn::Set(lanes, 1e-5f);
    const auto one = hn::Set(lanes, 1.0f);
    const auto half = hn::Set(lanes, 0.5f);
    const auto threshold = hn::Set(lanes, 0.75f);
    const auto highlight = hn::Set(lanes, highlightPoint);
    // Every vector starts on an even column, so a lane is a green site exactly when its parity matches
    // the row's first site.
    const auto oddLane =
        hn::RebindMask(lanes, hn::Eq(hn::And(hn::Iota(integers, 0), hn::Set(integers, 1)), hn::Set(integers, 1)));
    const std::uint64_t v1 = columns;
    const std::uint64_t v2 = 2u * columns;
    const auto ratio = [&](const float* weights, std::uint64_t index, std::int64_t near, std::int64_t far,
                           decltype(one) centre) {
        const auto farWeight = hn::LoadU(lanes, weights + (static_cast<std::int64_t>(index) + far));
        const auto weight = hn::LoadU(lanes, weights + index);
        const auto numerator =
            hn::Mul(hn::LoadU(lanes, cfa + (static_cast<std::int64_t>(index) + near)), hn::Add(farWeight, weight));
        const auto denominator = hn::Add(
            hn::Mul(farWeight, hn::Add(epsilon, centre)),
            hn::Mul(weight, hn::Add(epsilon, hn::LoadU(lanes, cfa + (static_cast<std::int64_t>(index) + far)))));
        return hn::Div(numerator, denominator);
    };
    const auto adams = [&](std::uint64_t index, std::int64_t near, std::int64_t far, decltype(one) centre) {
        return hn::Add(
            hn::LoadU(lanes, cfa + (static_cast<std::int64_t>(index) + near)),
            hn::Mul(half, hn::Sub(centre, hn::LoadU(lanes, cfa + (static_cast<std::int64_t>(index) + far)))));
    };
    const auto estimate = [&](decltype(one) value, decltype(one) centre, decltype(one) fallback) {
        return hn::IfThenElse(hn::Lt(hn::Abs(hn::Sub(one, value)), threshold), hn::Mul(centre, value), fallback);
    };
    const auto smaller = [](decltype(one) first, decltype(one) second) {
        return hn::IfThenElse(hn::Lt(second, first), second, first);
    };
    const auto rowV1 = static_cast<std::int64_t>(v1);
    const auto rowV2 = static_cast<std::int64_t>(v2);
    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const auto green = rowStartsGreen[row] != 0u ? hn::Not(oddLane) : oddLane;
        for (std::uint64_t column = 4u; column < columns - 4u; column += width)
        {
            const std::uint64_t index = row * columns + column;
            const auto centre = hn::LoadU(lanes, cfa + index);
            const auto upRatio = ratio(verticalWeights, index, -rowV1, -rowV2, centre);
            const auto downRatio = ratio(verticalWeights, index, rowV1, rowV2, centre);
            const auto leftRatio = ratio(horizontalWeights, index, -1, -2, centre);
            const auto rightRatio = ratio(horizontalWeights, index, 1, 2, centre);

            const auto upAdams = adams(index, -rowV1, -rowV2, centre);
            const auto downAdams = adams(index, rowV1, rowV2, centre);
            const auto leftAdams = adams(index, -1, -2, centre);
            const auto rightAdams = adams(index, 1, 2, centre);

            auto upRatioEstimate = estimate(upRatio, centre, upAdams);
            auto downRatioEstimate = estimate(downRatio, centre, downAdams);
            auto leftRatioEstimate = estimate(leftRatio, centre, leftAdams);
            auto rightRatioEstimate = estimate(rightRatio, centre, rightAdams);

            const auto leftWeight = hn::LoadU(lanes, horizontalWeights + index - 1u);
            const auto horizontalWeight =
                hn::Div(leftWeight, hn::Add(leftWeight, hn::LoadU(lanes, horizontalWeights + index + 1u)));
            const auto upWeight = hn::LoadU(lanes, verticalWeights + index - v1);
            const auto verticalWeight =
                hn::Div(upWeight, hn::Add(hn::LoadU(lanes, verticalWeights + index + v1), upWeight));

            const auto verticalAdams =
                hn::Add(hn::Mul(verticalWeight, downAdams), hn::Mul(hn::Sub(one, verticalWeight), upAdams));
            const auto horizontalAdams =
                hn::Add(hn::Mul(horizontalWeight, rightAdams), hn::Mul(hn::Sub(one, horizontalWeight), leftAdams));

            const auto verticalEstimate = hn::Add(hn::Mul(verticalWeight, downRatioEstimate),
                                                  hn::Mul(hn::Sub(one, verticalWeight), upRatioEstimate));
            const auto horizontalEstimate = hn::Add(hn::Mul(horizontalWeight, rightRatioEstimate),
                                                    hn::Mul(hn::Sub(one, horizontalWeight), leftRatioEstimate));
            const auto verticalAlternateValue =
                hn::IfThenElse(green, hn::Sub(centre, verticalAdams), hn::Sub(verticalAdams, centre));
            const auto horizontalAlternateValue =
                hn::IfThenElse(green, hn::Sub(centre, horizontalAdams), hn::Sub(horizontalAdams, centre));
            auto verticalDifferenceValue =
                hn::IfThenElse(green, hn::Sub(centre, verticalEstimate), hn::Sub(verticalEstimate, centre));
            auto horizontalDifferenceValue =
                hn::IfThenElse(green, hn::Sub(centre, horizontalEstimate), hn::Sub(horizontalEstimate, centre));

            const auto bright = hn::Or(hn::Or(hn::Gt(centre, highlight), hn::Gt(verticalAdams, highlight)),
                                       hn::Gt(horizontalAdams, highlight));
            upRatioEstimate = hn::IfThenElse(bright, upAdams, upRatioEstimate);
            downRatioEstimate = hn::IfThenElse(bright, downAdams, downRatioEstimate);
            leftRatioEstimate = hn::IfThenElse(bright, leftAdams, leftRatioEstimate);
            rightRatioEstimate = hn::IfThenElse(bright, rightAdams, rightRatioEstimate);
            verticalDifferenceValue = hn::IfThenElse(bright, verticalAlternateValue, verticalDifferenceValue);
            horizontalDifferenceValue = hn::IfThenElse(bright, horizontalAlternateValue, horizontalDifferenceValue);

            const auto verticalAdamsSpread = hn::Sub(upAdams, downAdams);
            const auto verticalRatioSpread = hn::Sub(upRatioEstimate, downRatioEstimate);
            const auto horizontalAdamsSpread = hn::Sub(leftAdams, rightAdams);
            const auto horizontalRatioSpread = hn::Sub(leftRatioEstimate, rightRatioEstimate);
            const auto verticalSpreadValue = smaller(hn::Mul(verticalAdamsSpread, verticalAdamsSpread),
                                                     hn::Mul(verticalRatioSpread, verticalRatioSpread));
            const auto horizontalSpreadValue = smaller(hn::Mul(horizontalAdamsSpread, horizontalAdamsSpread),
                                                       hn::Mul(horizontalRatioSpread, horizontalRatioSpread));

            const std::uint64_t remaining = (std::min)(width, columns - 4u - column);
            const auto written = hn::FirstN(lanes, static_cast<std::size_t>(remaining));
            hn::BlendedStore(verticalDifferenceValue, written, lanes, verticalDifference + index);
            hn::BlendedStore(horizontalDifferenceValue, written, lanes, horizontalDifference + index);
            hn::BlendedStore(verticalAlternateValue, written, lanes, verticalAlternate + index);
            hn::BlendedStore(horizontalAlternateValue, written, lanes, horizontalAlternate + index);
            hn::BlendedStore(verticalSpreadValue, written, lanes, verticalSpread + index);
            hn::BlendedStore(horizontalSpreadValue, written, lanes, horizontalSpread + index);
        }
    }
}

/// The `DemosaicAmazeVectorOps::boundVertical` pass for one row, lane by lane in the reference's order
/// of evaluation, with both sites' arithmetic taken and the site's chosen. The reference's `std::min`
/// and `std::max` keep their first argument on a tie, and so do the selections here: the instructions
/// keep the second, which would turn a positive zero into a negative one.
void boundVerticalAmaze(std::uint64_t row, std::uint64_t columns, bool rowStartsGreen, float clipPoint,
                        const float* cfa, const float* verticalAlternate, float* verticalDifference) noexcept
{
    const hn::ScalableTag<float> lanes;
    const hn::RebindToSigned<decltype(lanes)> integers;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto epsilon = hn::Set(lanes, 1e-5f);
    const auto one = hn::Set(lanes, 1.0f);
    const auto three = hn::Set(lanes, 3.0f);
    const auto zero = hn::Zero(lanes);
    const auto clip = hn::Set(lanes, clipPoint);
    const auto oddLane =
        hn::RebindMask(lanes, hn::Eq(hn::And(hn::Iota(integers, 0), hn::Set(integers, 1)), hn::Set(integers, 1)));
    const auto green = rowStartsGreen ? hn::Not(oddLane) : oddLane;
    const std::uint64_t v1 = columns;
    const std::uint64_t v2 = 2u * columns;
    using Vector = decltype(one);
    const auto smaller = [](Vector first, Vector second) {
        return hn::IfThenElse(hn::Lt(second, first), second, first);
    };
    const auto larger = [](Vector first, Vector second) {
        return hn::IfThenElse(hn::Lt(first, second), second, first);
    };
    const auto median = [&](Vector first, Vector second, Vector third) {
        return larger(smaller(first, second), smaller(third, larger(first, second)));
    };
    const auto variance = [&](const float* plane, std::uint64_t index) {
        const auto above = hn::LoadU(lanes, plane + index - v2);
        const auto here = hn::LoadU(lanes, plane + index);
        const auto below = hn::LoadU(lanes, plane + index + v2);
        const auto sum = hn::Add(hn::Add(above, here), below);
        return hn::Sub(
            hn::Mul(three, hn::Add(hn::Add(hn::Mul(above, above), hn::Mul(here, here)), hn::Mul(below, below))),
            hn::Mul(sum, sum));
    };
    for (std::uint64_t column = 4u; column < columns - 4u; column += width)
    {
        const std::uint64_t index = row * columns + column;
        auto difference = hn::LoadU(lanes, verticalDifference + index);
        difference = hn::IfThenElse(hn::Lt(variance(verticalAlternate, index), variance(verticalDifference, index)),
                                    hn::LoadU(lanes, verticalAlternate + index), difference);
        const auto centre = hn::LoadU(lanes, cfa + index);
        const auto up = hn::LoadU(lanes, cfa + index - v1);
        const auto down = hn::LoadU(lanes, cfa + index + v1);
        const auto triple = hn::Mul(three, difference);

        const auto greenGreen = hn::Add(hn::Neg(difference), centre);
        const auto greenBounded = hn::Add(hn::Neg(median(greenGreen, up, down)), centre);
        const auto greenWeight = hn::Sub(one, hn::Div(triple, hn::Add(hn::Add(epsilon, greenGreen), centre)));
        const auto greenBlended =
            hn::Add(hn::Mul(greenWeight, difference), hn::Mul(hn::Sub(one, greenWeight), greenBounded));
        auto greenResult = hn::IfThenElse(
            hn::Gt(difference, zero),
            hn::IfThenElse(hn::Gt(triple, hn::Add(greenGreen, centre)), greenBounded, greenBlended), difference);
        greenResult = hn::IfThenElse(hn::Gt(greenGreen, clip), greenBounded, greenResult);

        const auto otherGreen = hn::Add(difference, centre);
        const auto otherBounded = hn::Sub(median(otherGreen, up, down), centre);
        const auto otherWeight = hn::Add(one, hn::Div(triple, hn::Add(hn::Add(epsilon, otherGreen), centre)));
        const auto otherBlended =
            hn::Add(hn::Mul(otherWeight, difference), hn::Mul(hn::Sub(one, otherWeight), otherBounded));
        auto otherResult = hn::IfThenElse(
            hn::Lt(difference, zero),
            hn::IfThenElse(hn::Lt(triple, hn::Neg(hn::Add(otherGreen, centre))), otherBounded, otherBlended),
            difference);
        otherResult = hn::IfThenElse(hn::Gt(otherGreen, clip), otherBounded, otherResult);

        const std::uint64_t remaining = (std::min)(width, columns - 4u - column);
        hn::BlendedStore(hn::IfThenElse(green, greenResult, otherResult),
                         hn::FirstN(lanes, static_cast<std::size_t>(remaining)), lanes, verticalDifference + index);
    }
}

/// The `DemosaicAmazeVectorOps::directionWeights` pass, lane by lane in the reference's order of
/// evaluation. Every column of two vectors is weighed, and the red and blue sites among them - every
/// other lane - are gathered into one vector and stored at consecutive half-resolution indices, which
/// is where consecutive sites of a row sit when the row width is even.
void directionWeightsAmaze(const std::uint8_t* rowStartsGreen, std::uint64_t rows, std::uint64_t columns,
                           const float* verticalDifference, const float* horizontalDifference,
                           const float* verticalWeights, const float* horizontalWeights, const float* verticalSpread,
                           const float* horizontalSpread, float* directionWeight) noexcept
{
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto one = hn::Set(lanes, 1.0f);
    const auto half = hn::Set(lanes, 0.5f);
    const auto zero = hn::Zero(lanes);
    const auto squaredEpsilon = hn::Set(lanes, 1e-10f);
    const std::uint64_t v1 = columns;
    const std::uint64_t v2 = 2u * columns;
    const std::uint64_t v3 = 3u * columns;
    using Vector = decltype(one);
    const auto squaredFrom = [](Vector value, Vector mean) {
        const auto difference = hn::Sub(value, mean);
        return hn::Mul(difference, difference);
    };
    const auto spread = [&](const float* plane, std::uint64_t index, std::uint64_t first, std::uint64_t second,
                            std::uint64_t third, bool after) {
        const auto at = [&](std::uint64_t offset) {
            return hn::LoadU(lanes, after ? plane + index + offset : plane + index - offset);
        };
        const auto centre = hn::LoadU(lanes, plane + index);
        const auto near = at(first);
        const auto middle = at(second);
        const auto far = at(third);
        const auto mean = hn::Add(hn::Add(hn::Add(centre, near), middle), far);
        return hn::Add(hn::Add(hn::Add(squaredFrom(centre, mean), squaredFrom(near, mean)), squaredFrom(middle, mean)),
                       squaredFrom(far, mean));
    };
    const auto sum3 = [&](const float* plane, std::uint64_t index, std::uint64_t first, std::uint64_t second,
                          bool after) {
        const auto centre = hn::LoadU(lanes, plane + index);
        const auto near = hn::LoadU(lanes, after ? plane + index + first : plane + index - first);
        const auto far = hn::LoadU(lanes, after ? plane + index + second : plane + index - second);
        return hn::Add(hn::Add(centre, near), far);
    };
    const auto weigh = [&](std::uint64_t index) {
        const auto upVariance = spread(verticalDifference, index, v1, v2, v3, false);
        const auto downVariance = spread(verticalDifference, index, v1, v2, v3, true);
        const auto leftVariance = spread(horizontalDifference, index, 1u, 2u, 3u, false);
        const auto rightVariance = spread(horizontalDifference, index, 1u, 2u, 3u, true);

        const auto leftWeight = hn::LoadU(lanes, horizontalWeights + index - 1u);
        const auto horizontalWeight =
            hn::Div(leftWeight, hn::Add(leftWeight, hn::LoadU(lanes, horizontalWeights + index + 1u)));
        const auto upWeight = hn::LoadU(lanes, verticalWeights + index - v1);
        const auto verticalWeight =
            hn::Div(upWeight, hn::Add(hn::LoadU(lanes, verticalWeights + index + v1), upWeight));

        const auto verticalVariance = hn::Add(hn::Add(squaredEpsilon, hn::Mul(verticalWeight, downVariance)),
                                              hn::Mul(hn::Sub(one, verticalWeight), upVariance));
        const auto horizontalVariance = hn::Add(hn::Add(squaredEpsilon, hn::Mul(horizontalWeight, rightVariance)),
                                                hn::Mul(hn::Sub(one, horizontalWeight), leftVariance));

        const auto upSpread = sum3(verticalSpread, index, v1, v2, false);
        const auto downSpread = sum3(verticalSpread, index, v1, v2, true);
        const auto leftSpread = sum3(horizontalSpread, index, 1u, 2u, false);
        const auto rightSpread = sum3(horizontalSpread, index, 1u, 2u, true);
        const auto verticalSpreadVariance = hn::Add(hn::Add(squaredEpsilon, hn::Mul(verticalWeight, downSpread)),
                                                    hn::Mul(hn::Sub(one, verticalWeight), upSpread));
        const auto horizontalSpreadVariance = hn::Add(hn::Add(squaredEpsilon, hn::Mul(horizontalWeight, rightSpread)),
                                                      hn::Mul(hn::Sub(one, horizontalWeight), leftSpread));

        const auto varianceWeight = hn::Div(horizontalVariance, hn::Add(verticalVariance, horizontalVariance));
        const auto spreadWeight =
            hn::Div(horizontalSpreadVariance, hn::Add(verticalSpreadVariance, horizontalSpreadVariance));
        const auto agree =
            hn::And(hn::Gt(hn::Mul(hn::Sub(half, varianceWeight), hn::Sub(half, spreadWeight)), zero),
                    hn::Lt(hn::Abs(hn::Sub(half, spreadWeight)), hn::Abs(hn::Sub(half, varianceWeight))));
        return hn::IfThenElse(agree, varianceWeight, spreadWeight);
    };
    for (std::uint64_t row = 6u; row < rows - 6u; ++row)
    {
        // A row whose first site is green has its red or blue sites on the odd columns.
        const bool oddSites = rowStartsGreen[row] != 0u;
        for (std::uint64_t column = 6u; column < columns - 6u; column += 2u * width)
        {
            const std::uint64_t index = row * columns + column;
            const auto low = weigh(index);
            const auto high = weigh(index + width);
            const auto sites = oddSites ? hn::ConcatOdd(lanes, high, low) : hn::ConcatEven(lanes, high, low);
            const std::uint64_t firstSite = column + (oddSites ? 1u : 0u);
            if (firstSite >= columns - 6u)
            {
                break;
            }
            const std::uint64_t count = (std::min)(width, (columns - 6u - firstSite + 1u) / 2u);
            hn::BlendedStore(sites, hn::FirstN(lanes, static_cast<std::size_t>(count)), lanes,
                             directionWeight + ((row * columns + firstSite) >> 1u));
        }
    }
}

/// The `DemosaicAmazeVectorOps::boundHorizontal` pass: every row's horizontal recurrence in the
/// reference's order of evaluation, a block of rows at a time with one row in each lane.
///
/// Each column of a block is taken in turn, both kinds of site computed and each lane's chosen, and
/// the selections keep the reference's `std::min` and `std::max` ties, as the vertical pass's do. A
/// column's choice reads the final difference two columns to its left, which is still held from the
/// column that produced it, and the original ones at and two columns to its right; each column
/// gathers only what enters its windows and scatters its result back, which no later column gathers.
/// A block shorter than the vector repeats its first row in its spare lanes and scatters only its own
/// rows. The squared differences at the red and blue sites follow, row by row.
void boundHorizontalAmaze(const std::uint8_t* rowStartsGreen, std::uint64_t rows, std::uint64_t columns,
                          float clipPoint, const float* cfa, const float* horizontalAlternate,
                          const float* verticalDifference, float* horizontalDifference,
                          float* differenceSquared) noexcept
{
    const hn::ScalableTag<float> lanes;
    const hn::RebindToSigned<decltype(lanes)> integers;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto epsilon = hn::Set(lanes, 1e-5f);
    const auto one = hn::Set(lanes, 1.0f);
    const auto three = hn::Set(lanes, 3.0f);
    const auto zero = hn::Zero(lanes);
    const auto clip = hn::Set(lanes, clipPoint);
    using Vector = hn::Vec<decltype(lanes)>;
    using Mask = hn::Mask<decltype(lanes)>;
    const auto smaller = [](Vector first, Vector second) {
        return hn::IfThenElse(hn::Lt(second, first), second, first);
    };
    const auto larger = [](Vector first, Vector second) {
        return hn::IfThenElse(hn::Lt(first, second), second, first);
    };
    const auto median = [&](Vector first, Vector second, Vector third) {
        return larger(smaller(first, second), smaller(third, larger(first, second)));
    };
    const auto variance = [&](const Vector before, const Vector here, const Vector after) {
        const auto sum = hn::Add(hn::Add(before, here), after);
        return hn::Sub(
            hn::Mul(three, hn::Add(hn::Add(hn::Mul(before, before), hn::Mul(here, here)), hn::Mul(after, after))),
            hn::Mul(sum, sum));
    };
    // One column of one block: the final difference of every lane.
    const auto bound = [&](Mask green, Vector before, Vector here, Vector after, Vector alternateBefore,
                           Vector alternateHere, Vector alternateAfter, Vector left, Vector centre, Vector right) {
        const auto chosen = hn::IfThenElse(
            hn::Lt(variance(alternateBefore, alternateHere, alternateAfter), variance(before, here, after)),
            alternateHere, here);
        const auto triple = hn::Mul(three, chosen);

        const auto greenGreen = hn::Add(hn::Neg(chosen), centre);
        const auto greenBounded = hn::Add(hn::Neg(median(greenGreen, left, right)), centre);
        const auto greenWeight = hn::Sub(one, hn::Div(triple, hn::Add(hn::Add(epsilon, greenGreen), centre)));
        const auto greenBlended =
            hn::Add(hn::Mul(greenWeight, chosen), hn::Mul(hn::Sub(one, greenWeight), greenBounded));
        auto greenResult = hn::IfThenElse(
            hn::Gt(chosen, zero),
            hn::IfThenElse(hn::Gt(triple, hn::Add(greenGreen, centre)), greenBounded, greenBlended), chosen);
        greenResult = hn::IfThenElse(hn::Gt(greenGreen, clip), greenBounded, greenResult);

        const auto otherGreen = hn::Add(chosen, centre);
        const auto otherBounded = hn::Sub(median(otherGreen, left, right), centre);
        const auto otherWeight = hn::Add(one, hn::Div(triple, hn::Add(hn::Add(epsilon, otherGreen), centre)));
        const auto otherBlended =
            hn::Add(hn::Mul(otherWeight, chosen), hn::Mul(hn::Sub(one, otherWeight), otherBounded));
        auto otherResult = hn::IfThenElse(
            hn::Lt(chosen, zero),
            hn::IfThenElse(hn::Lt(triple, hn::Neg(hn::Add(otherGreen, centre))), otherBounded, otherBlended), chosen);
        otherResult = hn::IfThenElse(hn::Gt(otherGreen, clip), otherBounded, otherResult);
        return hn::IfThenElse(green, greenResult, otherResult);
    };
    alignas(64) std::int32_t offsets[hn::MaxLanes(hn::ScalableTag<float>())];
    alignas(64) std::int32_t startsGreen[hn::MaxLanes(hn::ScalableTag<float>())];
    const std::uint64_t end = columns - 4u;
    for (std::uint64_t first = 4u; first < rows - 4u; first += width)
    {
        const std::uint64_t count = (std::min)(width, rows - 4u - first);
        for (std::uint64_t lane = 0u; lane < width; ++lane)
        {
            const std::uint64_t row = lane < count ? lane : 0u;
            offsets[lane] = static_cast<std::int32_t>(row * columns);
            startsGreen[lane] = rowStartsGreen[first + row] != 0u ? 1 : 0;
        }
        const auto rowOffsets = hn::Load(integers, offsets);
        const auto owned = hn::FirstN(lanes, static_cast<std::size_t>(count));
        const auto greenAtEven = hn::RebindMask(lanes, hn::Ne(hn::Load(integers, startsGreen), hn::Zero(integers)));
        const auto greenAtOdd = hn::Not(greenAtEven);
        float* const differenceBase = horizontalDifference + first * columns;
        const float* const alternateBase = horizontalAlternate + first * columns;
        const float* const mosaicBase = cfa + first * columns;
        const auto gather = [&](const float* base, std::uint64_t column) {
            return hn::GatherIndex(lanes, base + column, rowOffsets);
        };
        // Per chain of one column parity: the final difference two columns back and the original here.
        Vector evenBefore = gather(differenceBase, 2u);
        Vector evenHere = gather(differenceBase, 4u);
        Vector oddBefore = gather(differenceBase, 3u);
        Vector oddHere = gather(differenceBase, 5u);
        Vector evenAlternateBefore = gather(alternateBase, 2u);
        Vector evenAlternateHere = gather(alternateBase, 4u);
        Vector oddAlternateBefore = gather(alternateBase, 3u);
        Vector oddAlternateHere = gather(alternateBase, 5u);
        Vector left = gather(mosaicBase, 3u);
        Vector centre = gather(mosaicBase, 4u);
        for (std::uint64_t column = 4u; column < end; column += 2u)
        {
            {
                const Vector after = gather(differenceBase, column + 2u);
                const Vector alternateAfter = gather(alternateBase, column + 2u);
                const Vector right = gather(mosaicBase, column + 1u);
                const Vector result = bound(greenAtEven, evenBefore, evenHere, after, evenAlternateBefore,
                                            evenAlternateHere, alternateAfter, left, centre, right);
                hn::MaskedScatterIndex(result, owned, lanes, differenceBase + column, rowOffsets);
                evenBefore = result;
                evenHere = after;
                evenAlternateBefore = evenAlternateHere;
                evenAlternateHere = alternateAfter;
                left = centre;
                centre = right;
            }
            if (column + 1u < end)
            {
                const Vector after = gather(differenceBase, column + 3u);
                const Vector alternateAfter = gather(alternateBase, column + 3u);
                const Vector right = gather(mosaicBase, column + 2u);
                const Vector result = bound(greenAtOdd, oddBefore, oddHere, after, oddAlternateBefore, oddAlternateHere,
                                            alternateAfter, left, centre, right);
                hn::MaskedScatterIndex(result, owned, lanes, differenceBase + column + 1u, rowOffsets);
                oddBefore = result;
                oddHere = after;
                oddAlternateBefore = oddAlternateHere;
                oddAlternateHere = alternateAfter;
                left = centre;
                centre = right;
            }
        }
    }
    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const std::uint64_t base = row * columns;
        const std::uint64_t firstColour = rowStartsGreen[row] != 0u ? 5u : 4u;
        for (std::uint64_t column = firstColour; column < end; column += 2u)
        {
            const float spread = verticalDifference[base + column] - horizontalDifference[base + column];
            differenceSquared[base + column] = spread * spread;
        }
    }
}

/// Estimates one tile through the shared AMaZE authority, with the passes above substituted.
void amazeRun(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    const DemosaicAmazeVectorOps ops{.initialize = &initializeAmaze,
                                     .gradients = &gradientsAmaze,
                                     .colourDifferences = &colourDifferencesAmaze,
                                     .boundVertical = &boundVerticalAmaze,
                                     .boundHorizontal = &boundHorizontalAmaze,
                                     .directionWeights = &directionWeightsAmaze};
    demosaicAmazeBody(tile, scratch, scratchFloats, &ops);
}

} // namespace HWY_NAMESPACE
} // namespace demosaic_amaze_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_amaze_simd
{
namespace
{

constexpr DemosaicRunFn kAmazeAvx2 = HWY_CHOOSE_AVX2(amazeRun);
constexpr InstructionSet (*kAmazeAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledAmaze);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr DemosaicRunFn kAmazeAvx3 = HWY_CHOOSE_AVX3(amazeRun);
constexpr InstructionSet (*kAmazeAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledAmaze);
#endif

} // namespace
} // namespace demosaic_amaze_simd

DemosaicRunFn simdAmazeRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_amaze_simd::kAmazeAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_amaze_simd::kAmazeAvx3;
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

InstructionSet simdAmazeIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_amaze_simd::kAmazeAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_amaze_simd::kAmazeAvx3Isa();
#else
        return InstructionSet::Automatic;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return InstructionSet::Automatic;
    }
    return InstructionSet::Automatic;
}

} // namespace an

#endif // HWY_ONCE
