// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the vector coordinate warp, compiled once per instruction set.
//
// A warp reads a window of the source for every sample it writes, and the window's position is a
// different one per sample, so the lanes carry one sample's window rather than several samples. The
// four columns of a cubic window are four adjacent source samples, which is one load, and the four
// rows are four of those - so a whole window is four loads and no gather. That is what decides the
// lane count here: a fixed four, not the widest vector a target has, because a wider vector would
// have to gather a second sample's window out of a different part of the plane to fill itself.
//
// The taps use the reference's polynomials, with one tap per lane. What differs is the order the
// sixteen products are summed in: the reader carries one running total, and this carries one per
// source row and combines them at the end, which is the reassociation a vector accumulation is. So
// a sample agrees with the reference reader's to within the kernel conformance bound rather than
// bit for bit, and the row stops at the first column whose window needs the border reader.
//
// A Lanczos-4 window is eight samples a side, two loads of four a source row, and its weights are a
// sine product per tap. The reference evaluates two sines for each of the sixteen taps of a sample;
// this evaluates one sine and one cosine of four angles for the whole window, because the taps of one
// axis sit at whole-sample steps from each other and every sine they need is a sine or a cosine of a
// quarter of the fraction, or of a quarter of one less the fraction. The weights are the same function
// of the same distances; they are rounded differently, so the row is held to the conformance bound
// as the cubic row is.

#include "backends/cpu/WarpCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY
// The shared vcpkg headers otherwise mark even inline debug assertions as DLL imports.
// This translation unit uses only header definitions, including those assertions.
#define HWY_DLLEXPORT

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/WarpCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <hwy/contrib/math/math-inl.h>

#include "kernels/reference/ReferenceKernelInternal.h"

#include <cstdint>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace warp_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// One axis's four cubic taps, resolved into a vector of lanes.
///
/// The same placement and the same two polynomials as `samplingTaps`, evaluated a lane at a time
/// rather than a tap at a time: lane arithmetic is per-lane arithmetic, so each weight is the same
/// number the scalar resolver computes. The normalisation divides by the sum of the four, which a
/// vector reduces in its own order - that, and the accumulation order in the row below, are the
/// whole of the difference between this row and the reader it stands in for.
///
/// @tparam TagT the four-lane Float64 description.
/// @param window the lane description.
/// @param coordinate where to read, under the pixel-center convention.
/// @param first receives the source index of the first tap.
/// @return the four normalized weights.
template <class TagT>
[[nodiscard]] HWY_INLINE hn::Vec<TagT> cubicTapsOf(TagT window, double coordinate, std::int64_t& first) noexcept
{
    const hn::FixedTag<double, 1> scalar;
    const double floored = hn::GetLane(hn::Floor(hn::Set(scalar, coordinate)));
    first = static_cast<std::int64_t>(floored) - 1;
    const double fraction = coordinate - floored;
    // One polynomial per lane instead of four broadcast polynomials with three discarded lanes
    // each. The coefficients preserve the scalar expression's multiply/add order, including the
    // normalization; no lookup table or quantized fractional coordinate is introduced.
    const double distances[4] = {fraction + 1.0, fraction, 1.0 - fraction, 2.0 - fraction};
    constexpr double kShape = reference_detail::kCubicShape;
    const double cubic[4] = {kShape, kShape + 2.0, kShape + 2.0, kShape};
    const double quadratic[4] = {5.0 * kShape, kShape + 3.0, kShape + 3.0, 5.0 * kShape};
    const double linear[4] = {8.0 * kShape, 0.0, 0.0, 8.0 * kShape};
    const double constant[4] = {-4.0 * kShape, 1.0, 1.0, -4.0 * kShape};
    const auto distance = hn::LoadU(window, distances);
    const auto weights = hn::Add(
        hn::Mul(hn::Add(hn::Mul(hn::Sub(hn::Mul(hn::LoadU(window, cubic), distance), hn::LoadU(window, quadratic)),
                                distance),
                        hn::LoadU(window, linear)),
                distance),
        hn::LoadU(window, constant));
    return hn::Div(weights, hn::Set(window, hn::ReduceSum(window, weights)));
}

/// Writes the cubic interior and, when permitted, windows wholly in a constant-zero border.
///
/// @param parameters the source plane and the destination-to-source matrix.
/// @param y the destination row.
/// @param first the first destination column to write.
/// @param last one past the last destination column this call may write.
/// @param destination the destination row's first sample.
/// @return how many columns from `first` were written.
[[nodiscard]] std::uint32_t warpCubicRowOf(const WarpRowParameters& parameters, std::uint32_t y, std::uint32_t first,
                                           std::uint32_t last, float* destination) noexcept
{
    // Four lanes because a cubic window is four wide, on every target. See the note above.
    const hn::FixedTag<double, 4> window;
    const hn::Rebind<float, decltype(window)> samples;

    const double* const m = parameters.destinationToSource;
    const double rowNumeratorX = m[1] * static_cast<double>(y);
    const double rowNumeratorY = m[4] * static_cast<double>(y);
    const double rowDenominator = m[7] * static_cast<double>(y);
    const float* const source = parameters.source;
    const std::uint64_t stride = parameters.sourceRowStride;
    const std::int64_t lastColumn = parameters.sourceWidth - 4;
    const std::int64_t lastRow = parameters.sourceHeight - 4;

    std::uint32_t x = first;
    for (; x < last; ++x)
    {
        const double column = static_cast<double>(x);
        const double denominator = m[6] * column + rowDenominator + m[8];
        const double sourceX = (m[0] * column + rowNumeratorX + m[2]) / denominator;
        const double sourceY = (m[3] * column + rowNumeratorY + m[5]) / denominator;
        // A cubic window spans floor(coordinate)-1 through floor(coordinate)+2. If an entire
        // axis is outside the source, every contributor is the constant border. For zero that
        // weighted sum is exactly zero, with the same valid result as the reference reader.
        // Partially overlapping windows still stop this row and use the checked border reader.
        if (parameters.constantZeroBorder &&
            (sourceX < -2.0 || sourceY < -2.0 || sourceX >= static_cast<double>(parameters.sourceWidth) + 1.0 ||
             sourceY >= static_cast<double>(parameters.sourceHeight) + 1.0))
        {
            destination[x] = 0.0f;
            continue;
        }
        std::int64_t firstColumn = 0;
        std::int64_t firstRow = 0;
        const auto columnWeights = cubicTapsOf(window, sourceX, firstColumn);
        const auto rowWeights = cubicTapsOf(window, sourceY, firstRow);
        if (firstColumn < 0 || firstRow < 0 || firstColumn > lastColumn || firstRow > lastRow)
        {
            break;
        }
        const float* line =
            source + static_cast<std::uint64_t>(firstRow) * stride + static_cast<std::uint64_t>(firstColumn);
        // One column-weighted accumulation per source row: four unaligned loads of four adjacent
        // samples, which is what a window is, and no gather anywhere.
        auto total =
            hn::Mul(hn::PromoteTo(window, hn::LoadU(samples, line)), hn::Set(window, hn::ExtractLane(rowWeights, 0)));
        line += stride;
        total = hn::Add(total, hn::Mul(hn::PromoteTo(window, hn::LoadU(samples, line)),
                                       hn::Set(window, hn::ExtractLane(rowWeights, 1))));
        line += stride;
        total = hn::Add(total, hn::Mul(hn::PromoteTo(window, hn::LoadU(samples, line)),
                                       hn::Set(window, hn::ExtractLane(rowWeights, 2))));
        line += stride;
        total = hn::Add(total, hn::Mul(hn::PromoteTo(window, hn::LoadU(samples, line)),
                                       hn::Set(window, hn::ExtractLane(rowWeights, 3))));
        destination[x] = static_cast<float>(hn::ReduceSum(window, hn::Mul(total, columnWeights)));
    }
    return x - first;
}

/// Writes the Lanczos-4 interior and, when permitted, windows wholly in a constant-zero border.
///
/// The reference weighs the tap at distance `d` from the coordinate by
/// `sin(pi d) / (pi d) * sin(pi d / 4) / (pi d / 4)` and divides an axis's eight weights by their sum.
/// With `t` the coordinate's fraction and `u = 1 - t`, the eight distances of one axis are `3 + t`,
/// `2 + t`, `1 + t`, `t`, `u`, `1 + u`, `2 + u` and `3 + u`: every `sin(pi d)` is `sin(pi t)` up to a
/// sign that alternates from tap to tap, and every `sin(pi d / 4)` is the sine or the cosine of
/// `pi t / 4` or of `pi u / 4`. The common `sin(pi t)` and the constant `16 / pi^2` cancel in the
/// division by the sum, so a weight is a signed sine or cosine of a quarter angle over its squared
/// distance, and the two divisions by the axes' sums are one division of the window's total. Using
/// `u` where the reference would subtract two nearly equal cosines keeps each weight accurate to a few
/// units in its last place over the whole fraction, including next to either end.
///
/// A whole-sample coordinate on either axis is left to the reader, which gives the tap under it its
/// exact weight of one: there the sines this row divides are zero.
///
/// @param parameters the source plane and the destination-to-source matrix.
/// @param y the destination row.
/// @param first the first destination column to write.
/// @param last one past the last destination column this call may write.
/// @param destination the destination row's first sample.
/// @return how many columns from `first` were written.
[[nodiscard]] std::uint32_t warpLanczos4RowOf(const WarpRowParameters& parameters, std::uint32_t y, std::uint32_t first,
                                              std::uint32_t last, float* destination) noexcept
{
    // Four lanes, as the cubic row has, so that every target sums a window in the same order.
    const hn::FixedTag<double, 4> quad;
    const hn::FixedTag<double, 2> pair;
    const hn::FixedTag<double, 1> scalar;
    const hn::Rebind<float, decltype(quad)> samples;

    const double* const m = parameters.destinationToSource;
    const double rowNumeratorX = m[1] * static_cast<double>(y);
    const double rowNumeratorY = m[4] * static_cast<double>(y);
    const double rowDenominator = m[7] * static_cast<double>(y);
    const float* const source = parameters.source;
    const std::uint64_t stride = parameters.sourceRowStride;
    const std::int64_t lastColumn = parameters.sourceWidth - 8;
    const std::int64_t lastRow = parameters.sourceHeight - 8;
    const double rightOfWindow = static_cast<double>(parameters.sourceWidth) + 3.0;
    const double belowWindow = static_cast<double>(parameters.sourceHeight) + 3.0;

    // The first four taps sit 3, 2, 1 and 0 samples before the coordinate plus its fraction, the last
    // four 0, 1, 2 and 3 samples after it plus one less the fraction; the sine of the full angle
    // alternates in sign from one tap to the next.
    const double nearOffsets[4] = {3.0, 2.0, 1.0, 0.0};
    const double farOffsets[4] = {0.0, 1.0, 2.0, 3.0};
    const double nearSigns[4] = {-1.0, 1.0, -1.0, 1.0};
    const double farSigns[4] = {1.0, -1.0, 1.0, -1.0};
    const auto nearOffset = hn::LoadU(quad, nearOffsets);
    const auto farOffset = hn::LoadU(quad, farOffsets);
    const auto nearSign = hn::LoadU(quad, nearSigns);
    const auto farSign = hn::LoadU(quad, farSigns);
    const auto quarterPi = hn::Set(quad, 0.78539816339744830962);

    std::uint32_t x = first;
    for (; x < last; ++x)
    {
        const double column = static_cast<double>(x);
        const double denominator = m[6] * column + rowDenominator + m[8];
        const double sourceX = (m[0] * column + rowNumeratorX + m[2]) / denominator;
        const double sourceY = (m[3] * column + rowNumeratorY + m[5]) / denominator;
        // A Lanczos-4 window spans floor(coordinate)-3 through floor(coordinate)+4. If an entire axis
        // is outside the source, every contributor is the constant border, and for zero the weighted
        // sum is exactly zero with the same valid result as the reference reader.
        if (parameters.constantZeroBorder &&
            (sourceX < -4.0 || sourceY < -4.0 || sourceX >= rightOfWindow || sourceY >= belowWindow))
        {
            destination[x] = 0.0f;
            continue;
        }
        const double flooredX = hn::GetLane(hn::Floor(hn::Set(scalar, sourceX)));
        const double flooredY = hn::GetLane(hn::Floor(hn::Set(scalar, sourceY)));
        const std::int64_t firstColumn = static_cast<std::int64_t>(flooredX) - 3;
        const std::int64_t firstRow = static_cast<std::int64_t>(flooredY) - 3;
        // Exact: the floor's integer bits are the coordinate's own.
        const double fractionX = sourceX - flooredX;
        const double fractionY = sourceY - flooredY;
        if (firstColumn < 0 || firstRow < 0 || firstColumn > lastColumn || firstRow > lastRow || fractionX == 0.0 ||
            fractionY == 0.0)
        {
            break;
        }
        const double restX = 1.0 - fractionX;
        const double restY = 1.0 - fractionY;

        // One sine and one cosine of four quarter angles: [u_x, t_x, u_y, t_y] * pi / 4.
        const auto angles = hn::Mul(hn::Combine(quad, hn::Dup128VecFromValues(pair, restY, fractionY),
                                                hn::Dup128VecFromValues(pair, restX, fractionX)),
                                    quarterPi);
        hn::Vec<decltype(quad)> sines;
        hn::Vec<decltype(quad)> cosines;
        hn::SinCos(quad, angles, sines, cosines);
        // The quarter-angle sine of each tap's distance, the near four and the far four alike:
        // [sin(pi u / 4), cos(pi t / 4), cos(pi u / 4), sin(pi t / 4)] for each axis.
        const auto sinUCosT = hn::OddEven(cosines, sines);
        const auto cosUSinT = hn::OddEven(sines, cosines);
        const auto quarterSinesX = hn::ConcatLowerLower(quad, cosUSinT, sinUCosT);
        const auto quarterSinesY = hn::ConcatUpperUpper(quad, cosUSinT, sinUCosT);

        const auto tX = hn::Set(quad, fractionX);
        const auto uX = hn::Set(quad, restX);
        const auto tY = hn::Set(quad, fractionY);
        const auto uY = hn::Set(quad, restY);
        const auto nearX = hn::Add(nearOffset, tX);
        const auto farX = hn::Add(farOffset, uX);
        const auto nearY = hn::Add(nearOffset, tY);
        const auto farY = hn::Add(farOffset, uY);
        const auto nearWeightsX = hn::Div(hn::Mul(quarterSinesX, nearSign), hn::Mul(nearX, nearX));
        const auto farWeightsX = hn::Div(hn::Mul(quarterSinesX, farSign), hn::Mul(farX, farX));
        const auto nearWeightsY = hn::Div(hn::Mul(quarterSinesY, nearSign), hn::Mul(nearY, nearY));
        const auto farWeightsY = hn::Div(hn::Mul(quarterSinesY, farSign), hn::Mul(farY, farY));
        const double sumX = hn::ReduceSum(quad, hn::Add(nearWeightsX, farWeightsX));
        const double sumY = hn::ReduceSum(quad, hn::Add(nearWeightsY, farWeightsY));

        // One column-weighted accumulation per source row: two loads of four adjacent samples each.
        const float* line =
            source + static_cast<std::uint64_t>(firstRow) * stride + static_cast<std::uint64_t>(firstColumn);
        auto nearTotal = hn::Zero(quad);
        auto farTotal = hn::Zero(quad);
        const auto accumulate = [&](const hn::Vec<decltype(quad)> rowWeight) {
            nearTotal = hn::MulAdd(rowWeight, hn::PromoteTo(quad, hn::LoadU(samples, line)), nearTotal);
            farTotal = hn::MulAdd(rowWeight, hn::PromoteTo(quad, hn::LoadU(samples, line + 4)), farTotal);
            line += stride;
        };
        accumulate(hn::BroadcastLane<0>(nearWeightsY));
        accumulate(hn::BroadcastLane<1>(nearWeightsY));
        accumulate(hn::BroadcastLane<2>(nearWeightsY));
        accumulate(hn::BroadcastLane<3>(nearWeightsY));
        accumulate(hn::BroadcastLane<0>(farWeightsY));
        accumulate(hn::BroadcastLane<1>(farWeightsY));
        accumulate(hn::BroadcastLane<2>(farWeightsY));
        accumulate(hn::BroadcastLane<3>(farWeightsY));
        const double weighted =
            hn::ReduceSum(quad, hn::MulAdd(farTotal, farWeightsX, hn::Mul(nearTotal, nearWeightsX)));
        destination[x] = static_cast<float>(weighted / (sumX * sumY));
    }
    return x - first;
}

} // namespace HWY_NAMESPACE
} // namespace warp_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace warp_simd
{
namespace
{

constexpr WarpCpuRuns kAvx2Runs{.cubicFloat32 = HWY_CHOOSE_AVX2(warpCubicRowOf),
                                .lanczos4Float32 = HWY_CHOOSE_AVX2(warpLanczos4RowOf)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr WarpCpuRuns kAvx3Runs{.cubicFloat32 = HWY_CHOOSE_AVX3(warpCubicRowOf),
                                .lanczos4Float32 = HWY_CHOOSE_AVX3(warpLanczos4RowOf)};
#endif

} // namespace
} // namespace warp_simd

const WarpCpuRuns* simdWarpRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &warp_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &warp_simd::kAvx3Runs;
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

} // namespace an

#endif // HWY_ONCE
