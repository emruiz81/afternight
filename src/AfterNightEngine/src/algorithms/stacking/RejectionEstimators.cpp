// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the rejection estimators: seven adopted rules, each run on one column at the
// tile width.
//
// Every family is its adopted rule kept to its answers, roundings included. Statistics are taken at
// the tile's floating width: the sigma families sum in Float64 in frame order, the linear fit keeps
// running means at the tile width in rank order, and the generalized extreme Studentized deviate
// (GESD) test takes its statistics in Float32 whatever the tile width. The rules are
// Siril-compatible and the deviate test is Rosner's, and the histogram search the median absolute
// deviation follows is that of the `findMinMaxPercentile` routine, whose source carries a 2017
// copyright line and is attributed there to Ingo Weyrich.
//
// Two of those rules read as defects and are kept because Siril has them too: the sigma families'
// removal counter is not reset between iterations, and the linear fit's rank statistics are taken
// over every frame of the stack rather than over the survivors.
//
// Nothing here allocates: every array is the worker's slice, cut by `rejectionWorkspace`. Every
// order is fixed - gathers and sums run in frame order, rankings break ties by frame, and a median
// is an exact selection whose even-count answer is the mean of the two middle values - so a
// verdict does not depend on the worker that reached it.

#include "algorithms/stacking/RejectionEstimators.h"

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include "interop/gsl/StudentDistribution.h"

namespace an
{
namespace
{

/// The exchanges of one sorting network, up to the nineteen eight samples need.
struct SmallMedianNetwork
{
    /// Each exchange's two positions, the one that receives the smaller value first.
    std::array<std::array<std::uint32_t, 2u>, 19u> exchanges{};
    /// How many leading exchanges the network has.
    std::uint32_t count = 0u;
};

/// The same odd-even merge network as the column backend, specialized for short histories.
///
/// Batcher's odd-even merge sort for `Count` samples, generated at compile time: every exchange
/// orders one pair of positions, and after the last one the samples are sorted.
template <std::uint32_t Count> consteval SmallMedianNetwork smallMedianNetwork()
{
    SmallMedianNetwork network;
    for (std::uint32_t p = 1u; p < Count; p *= 2u)
        for (std::uint32_t k = p; k >= 1u; k /= 2u)
            for (std::uint32_t j = k % p; j + k < Count; j += 2u * k)
                for (std::uint32_t i = 0u; i < k && i + j + k < Count; ++i)
                    if ((i + j) / (2u * p) == (i + j + k) / (2u * p))
                        network.exchanges[network.count++] = {i + j, i + j + k};
    return network;
}

/// One exchange of the network: the pair in order, the smaller first.
///
/// Written as two selects on one comparison rather than a conditional swap, so the compiler emits
/// no branch: the network's comparisons follow the samples, which a branch predictor cannot learn,
/// and a mispredicted exchange cost more than the whole network otherwise does. The selects keep
/// the conditional swap's result exactly, a tie and a signed zero included.
template <typename T> void exchangeInOrder(T& left, T& right) noexcept
{
    const T first = left;
    const T second = right;
    const bool exchange = first > second;
    left = exchange ? second : first;
    right = exchange ? first : second;
}

/// Sorts a short history by running every exchange of its network, unrolled.
template <typename T, std::uint32_t Count, std::size_t... Exchanges>
void sortSmallHistory(std::array<T, Count>& values, std::index_sequence<Exchanges...>) noexcept
{
    constexpr auto kNetwork = smallMedianNetwork<Count>();
    (exchangeInOrder(values[kNetwork.exchanges[Exchanges][0]], values[kNetwork.exchanges[Exchanges][1]]), ...);
}

/// The median of a short history, sorted in registers and written back in order.
template <typename T, std::uint32_t Count> T medianOfSmall(T* values) noexcept
{
    std::array<T, Count> sorted;
    std::copy_n(values, Count, sorted.begin());
    sortSmallHistory<T, Count>(sorted, std::make_index_sequence<smallMedianNetwork<Count>().count>{});
    std::copy_n(sorted.begin(), Count, values);
    constexpr std::uint32_t kMiddle = Count / 2u;
    if constexpr (Count % 2u != 0u)
        return sorted[kMiddle];
    else
        return static_cast<T>((sorted[kMiddle] + sorted[kMiddle - 1u]) / 2.0);
}

/// The exact median of the survivors gathered at the tile width, reordering them.
///
/// The middle value of an odd count; of an even one the two middle values added in the tile type
/// and halved. Two to eight values are sorted by the network and written back in order; a longer
/// column is partitioned to its middle.
template <typename T> T medianOf(T* values, std::uint32_t count) noexcept
{
    const auto middle = count / 2u;
    // Short histories pay only a fixed sequence of comparisons, with no partition machinery.
    switch (count)
    {
    case 2u:
        return medianOfSmall<T, 2u>(values);
    case 3u:
        return medianOfSmall<T, 3u>(values);
    case 4u:
        return medianOfSmall<T, 4u>(values);
    case 5u:
        return medianOfSmall<T, 5u>(values);
    case 6u:
        return medianOfSmall<T, 6u>(values);
    case 7u:
        return medianOfSmall<T, 7u>(values);
    case 8u:
        return medianOfSmall<T, 8u>(values);
    default:
        break;
    }
    std::nth_element(values, values + middle, values + count);
    if (count % 2u != 0u)
        return values[middle];
    // The general median widens the middle pair before addition, so two finite Float32 values
    // cannot overflow before the division by two.
    const T lower = *std::max_element(values, values + middle);
    return static_cast<T>((static_cast<double>(values[middle]) + static_cast<double>(lower)) / 2.0);
}

/// Copies the column's survivors, in frame order and narrowed to the tile width, into the working
/// copy, and returns how many there are.
template <typename T> std::uint32_t gather(const RejectionWorkspace& workspace, std::uint32_t frames) noexcept
{
    auto* values = reinterpret_cast<T*>(workspace.work);
    std::uint32_t count = 0u;
    for (std::uint32_t i = 0u; i < frames; ++i)
        if (rejectionSurvivor(workspace.flags[i]))
            values[count++] = static_cast<T>(workspace.column[i]);
    return count;
}

/// The unbiased sample standard deviation of `count` values around a stated mean.
///
/// Each difference and its square are taken in the tile type and the squares summed in Float64 in
/// frame order; the sum over `count - 1` is narrowed to the tile type before its square root. A
/// non-zero `Count` fixes the count at compile time for the short histories.
template <typename T, std::uint32_t Count = 0u> T deviationAround(const T* values, std::uint32_t count, T mean) noexcept
{
    if constexpr (Count != 0u)
        count = Count;
    double sum = 0.0;
    std::uint32_t i = 0u;
    // Expose independent sample arithmetic while keeping every addition in frame order.
    for (; count - i >= 4u; i += 4u)
    {
        const T a = values[i] - mean;
        const T b = values[i + 1u] - mean;
        const T c = values[i + 2u] - mean;
        const T d = values[i + 3u] - mean;
        sum += static_cast<double>(a * a);
        sum += static_cast<double>(b * b);
        sum += static_cast<double>(c * c);
        sum += static_cast<double>(d * d);
    }
    for (; i < count; ++i)
    {
        const T difference = values[i] - mean;
        sum += static_cast<double>(difference * difference);
    }
    return std::sqrt(static_cast<T>(sum / (count - 1u)));
}

/// The unbiased sample standard deviation of the gathered survivors around their mean, the mean
/// summed in Float64 in frame order and narrowed to the tile type.
template <typename T, std::uint32_t Count = 0u> T deviationOfSize(const T* values, std::uint32_t count) noexcept
{
    if constexpr (Count != 0u)
        count = Count;
    double sum = 0.0;
    std::uint32_t i = 0u;
    for (; count - i >= 4u; i += 4u)
    {
        sum += values[i];
        sum += values[i + 1u];
        sum += values[i + 2u];
        sum += values[i + 3u];
    }
    for (; i < count; ++i)
        sum += values[i];
    return deviationAround<T, Count>(values, count, static_cast<T>(sum / count));
}

/// Clamps the gathered values into `[low, high]` in place and returns their unbiased standard
/// deviation around their own mean; a later pass therefore clamps the values an earlier pass
/// already clamped, as the adopted winsorization does.
template <typename T, std::uint32_t Count = 0u>
T winsorizedDeviationOfSize(T* values, std::uint32_t count, T low, T high) noexcept
{
    if constexpr (Count != 0u)
        count = Count;
    // Clamping and summing can share a traversal without changing the frame order or
    // the tile-type rounding of either the clamped samples or their mean.
    double sum = 0.0;
    std::uint32_t i = 0u;
    for (; count - i >= 4u; i += 4u)
    {
        values[i] = std::clamp(values[i], low, high);
        values[i + 1u] = std::clamp(values[i + 1u], low, high);
        values[i + 2u] = std::clamp(values[i + 2u], low, high);
        values[i + 3u] = std::clamp(values[i + 3u], low, high);
        sum += values[i];
        sum += values[i + 1u];
        sum += values[i + 2u];
        sum += values[i + 3u];
    }
    for (; i < count; ++i)
    {
        values[i] = std::clamp(values[i], low, high);
        sum += values[i];
    }
    return deviationAround<T, Count>(values, count, static_cast<T>(sum / count));
}

/// Calls `function` instantiated for a count of two to eight, so a short history's loops have a
/// compile-time bound, and instantiated for zero - the count read at run time - otherwise.
template <typename FunctionT> auto forSmallHistory(std::uint32_t count, FunctionT&& function) noexcept
{
    switch (count)
    {
    case 2u:
        return function.template operator()<2u>();
    case 3u:
        return function.template operator()<3u>();
    case 4u:
        return function.template operator()<4u>();
    case 5u:
        return function.template operator()<5u>();
    case 6u:
        return function.template operator()<6u>();
    case 7u:
        return function.template operator()<7u>();
    case 8u:
        return function.template operator()<8u>();
    default:
        return function.template operator()<0u>();
    }
}

/// `deviationOfSize` over the gathered survivors, with the count fixed at compile time when short.
template <typename T> T deviationOf(const T* values, std::uint32_t count) noexcept
{
    return forSmallHistory(count, [&]<std::uint32_t Count>() { return deviationOfSize<T, Count>(values, count); });
}

/// `winsorizedDeviationOfSize` over the gathered survivors, with the count fixed at compile time
/// when short.
template <typename T> T winsorizedDeviationOf(T* values, std::uint32_t count, T low, T high) noexcept
{
    return forSmallHistory(
        count, [&]<std::uint32_t Count>() { return winsorizedDeviationOfSize<T, Count>(values, count, low, high); });
}

/// The median absolute deviation of the survivors around `center`, by the adopted histogram search
/// rather than by selection.
///
/// The compatibility MAD is a histogram interpolation, not a normal-consistent MAD: it is not
/// scaled by 1.4826. The absolute deviations are taken in Float32, binned between their extremes
/// into at most 65,536 bins, and the bin where the cumulative count reaches half the survivors is
/// interpolated linearly and clamped to the extremes. Equal extremes return that deviation; a
/// range the bins cannot scale finitely returns a quiet NaN, which the caller refuses. Deviations
/// and bins share the already reserved working copy: four bytes each per frame, no heap or TBB.
template <typename T> T histogramDeviation(const RejectionWorkspace& workspace, std::uint32_t frames, T center) noexcept
{
    auto* values = reinterpret_cast<float*>(workspace.work);
    auto* bins = reinterpret_cast<std::uint32_t*>(workspace.work + frames * sizeof(float));
    std::uint32_t count = 0u;
    for (std::uint32_t i = 0u; i < frames; ++i)
        if (rejectionSurvivor(workspace.flags[i]))
            values[count++] = std::fabs(static_cast<float>(static_cast<T>(workspace.column[i]) - center));
    const auto extremes = std::minmax_element(values, values + count);
    const float low = *extremes.first;
    const float high = *extremes.second;
    if (low == high)
        return static_cast<T>(low);
    const std::uint32_t binCount = (std::min)(65536u, count);
    const float scale = static_cast<float>(binCount - 1u) / (high - low);
    if (!std::isfinite(scale) || scale <= 0.0f || !std::isfinite(high))
        return std::numeric_limits<T>::quiet_NaN();
    std::fill_n(bins, binCount, 0u);
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const float position = scale * (values[i] - low);
        const auto bin = static_cast<std::uint32_t>((std::min)(position, static_cast<float>(binCount - 1u)));
        ++bins[bin];
    }
    const float threshold = 0.5f * static_cast<float>(count);
    std::uint32_t bin = 0u;
    std::uint32_t accumulated = 0u;
    while (static_cast<float>(accumulated) < threshold)
        accumulated += bins[bin++];
    const float above = static_cast<float>(accumulated) - threshold;
    const float below = threshold - static_cast<float>(accumulated - bins[bin - 1u]);
    float result = (below * static_cast<float>(bin) + above * static_cast<float>(bin - 1u)) / (above + below);
    result /= scale;
    result += low;
    return static_cast<T>(std::clamp(result, low, high));
}

/// Which side a sample falls out on: -1 when it lies strictly further below the centre than the
/// low limit, otherwise 1 when strictly further above than the high limit, otherwise 0. The low
/// side is tested first, as the adopted rule tests it.
template <typename T> int sideOf(T lowDistance, T highDistance, T lowLimit, T highLimit) noexcept
{
    return lowDistance > lowLimit ? -1 : (highDistance > highLimit ? 1 : 0);
}

/// Records one verdict: counts the event on its side and, for a removal, flags the frame `Low` or
/// `High`. A replacement flags a `Kept` frame `ReplacedLow` or `ReplacedHigh` and leaves an
/// already-replaced frame on the side of its first replacement, while still counting the event.
void record(RejectionWorkspace const& workspace, std::uint32_t frame, int side, bool replace,
            RejectionCounts& counts) noexcept
{
    if (side == 0)
        return;
    if (side < 0)
        ++counts.low;
    else
        ++counts.high;
    if (!replace)
        workspace.flags[frame] = side < 0 ? RejectionFlag::Low : RejectionFlag::High;
    else if (workspace.flags[frame] == RejectionFlag::Kept)
        workspace.flags[frame] = side < 0 ? RejectionFlag::ReplacedLow : RejectionFlag::ReplacedHigh;
}

/// The four sigma families: sigma clipping, MAD sigma clipping, median-sigma replacement and
/// winsorized sigma clipping, iterated until an iteration changes nothing.
///
/// Each iteration gathers the survivors, estimates a dispersion and a centre - the survivors'
/// median - and judges every survivor in frame order: a sample more than `sigma.low` dispersions
/// below the centre falls out low, one more than `sigma.high` above falls out high. The dispersion
/// is the unbiased standard deviation, except under MAD clipping, where it is the unscaled
/// histogram deviation around the previous iteration's centre, the median being refreshed only
/// after it; and under winsorized clipping, where the standard deviation is refined by clamping
/// the survivors to 1.5 dispersions about the centre and multiplying their deviation by 1.134
/// until a pass moves it by no more than 0.0005 of its previous value.
///
/// The removing families stop judging once the survivors counted at the iteration's start, less
/// every removal made so far in this iteration or an earlier one, are four or fewer. The counter
/// is not reset between iterations, so an earlier removal counts twice and removal stops sooner
/// than the guard reads; Siril does the same. The replacing family has no
/// guard: it writes the centre over each sample it judges out and keeps the frame among the
/// survivors.
///
/// @return success, cancellation, or a bare `ErrorCode::InvalidData` for a centre or dispersion
///         that is not finite, or a winsorization that has not settled after 8,192 passes.
template <typename T>
Status sigmaFamily(const ExecutionContext& execution, const StackModel& model, const RejectionWorkspace& workspace,
                   RejectionCounts& counts)
{
    const auto& request = model.request();
    const auto method = request.rejection.method;
    const auto frames = request.input.frameCount;
    auto* values = reinterpret_cast<T*>(workspace.work);
    auto count = gather<T>(workspace, frames);
    if (count < 2u)
        return Status::success();
    const bool mad = method == StackRejection::MadSigmaClipping;
    // Only MAD needs a center before estimating the deviation. The other estimators can
    // share the same gathered samples for the deviation and the subsequent median.
    T center = mad ? medianOf(values, count) : T{0};
    std::uint32_t cumulative = 0u;
    bool first = true;
    for (;;)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
            return checkpoint;
        if (!first || mad)
            count = gather<T>(workspace, frames);
        T sigma = mad ? histogramDeviation<T>(workspace, frames, center) : deviationOf(values, count);
        // Refresh after estimating MAD: the previous center is part of the adopted rule.
        if (!mad || !first)
        {
            if (mad)
                gather<T>(workspace, frames);
            center = medianOf(values, count);
        }
        first = false;
        if (method == StackRejection::WinsorizedSigmaClipping)
        {
            gather<T>(workspace, frames);
            for (std::uint32_t iteration = 0u;; ++iteration)
            {
                const Status stopped = execution.checkCancellation();
                if (stopped.failed())
                    return stopped;
                const T low = center - 1.5f * sigma;
                const T high = center + 1.5f * sigma;
                const T previous = sigma;
                sigma = 1.134f * winsorizedDeviationOf(values, count, low, high);
                if (!std::isfinite(sigma) || !std::isfinite(low) || !std::isfinite(high))
                    return Status::failure(ErrorCode::InvalidData);
                if (!(std::fabs(sigma - previous) > previous * 0.0005))
                    break;
                // Refuse failure to converge instead of hanging on an adversarial column.
                if (iteration == 8191u)
                    return Status::failure(ErrorCode::InvalidData);
            }
        }
        if (!std::isfinite(center) || !std::isfinite(sigma))
            return Status::failure(ErrorCode::InvalidData);
        bool changed = false;
        const bool replace = method == StackRejection::MedianSigmaReplacement;
        const T lowLimit = sigma * static_cast<T>(request.rejection.sigma.low);
        const T highLimit = sigma * static_cast<T>(request.rejection.sigma.high);
        for (std::uint32_t i = 0u; i < frames; ++i)
        {
            if (!rejectionSurvivor(workspace.flags[i]))
                continue;
            if (!replace && (count <= cumulative || count - cumulative <= 4u))
                break;
            const T value = static_cast<T>(workspace.column[i]);
            const int side =
                sideOf(static_cast<T>(center - value), static_cast<T>(value - center), lowLimit, highLimit);
            if (side != 0)
            {
                record(workspace, i, side, replace, counts);
                if (replace)
                    workspace.column[i] = center;
                if (!replace)
                    ++cumulative;
                changed = true;
            }
        }
        if (!changed)
            return Status::success();
    }
}

/// Median-relative clipping, in one pass with no iteration and no survivor guard.
///
/// The centre is the survivors' median at the tile width; a survivor more than
/// `medianFractions.low` of the median below it falls out low, one more than `medianFractions.high`
/// of it above falls out high, the distances and limits taken in Float64. A median of exactly zero
/// rejects nothing, and a negative median makes both limits negative, as the adopted rule does.
/// Unlike the iterating families this one can reject every survivor of a column.
///
/// @return success, or a bare `ErrorCode::InvalidData` for a median that is not finite.
template <typename T>
Status percentile(const StackModel& model, const RejectionWorkspace& workspace, RejectionCounts& counts)
{
    const auto frames = model.request().input.frameCount;
    const auto count = gather<T>(workspace, frames);
    if (count < 2u)
        return Status::success();
    const T center = medianOf(reinterpret_cast<T*>(workspace.work), count);
    if (!std::isfinite(center))
        return Status::failure(ErrorCode::InvalidData);
    if (center == T{0})
        return Status::success();
    const auto& fractions = model.request().rejection.medianFractions;
    for (std::uint32_t i = 0u; i < frames; ++i)
        if (rejectionSurvivor(workspace.flags[i]))
            record(workspace, i,
                   sideOf(static_cast<double>(center) - workspace.column[i], workspace.column[i] - center,
                          center * fractions.low, center * fractions.high),
                   false, counts);
    return Status::success();
}

/// Writes the survivors' frame indices into the ranking, ordered by their Float64 value and then by
/// frame, and returns how many there are. The order is total, so it does not depend on how the
/// standard library's sort permutes equal keys.
std::uint32_t sortOrder(const RejectionWorkspace& workspace, std::uint32_t frames) noexcept
{
    std::uint32_t count = 0u;
    for (std::uint32_t i = 0u; i < frames; ++i)
        if (rejectionSurvivor(workspace.flags[i]))
            workspace.order[count++] = i;
    std::sort(workspace.order, workspace.order + count, [&](std::uint32_t left, std::uint32_t right) {
        return workspace.column[left] < workspace.column[right] ||
               (workspace.column[left] == workspace.column[right] && left < right);
    });
    return count;
}

/// Linear-fit clipping, iterated until an iteration changes nothing.
///
/// The survivors are ranked by value, and a line is fitted to value against rank `i`: the mean
/// value and the covariance as running means weighted by the prepared reciprocals, the slope as
/// that covariance times the prepared inverse rank variance, and the intercept through the mean
/// rank. The ranks' mean and variance are the prepared ones over every frame of the stack, not
/// over the survivors, as in Siril. The dispersion is the mean absolute
/// residual; a survivor more than `sigma.low` of it below the line falls out low, one more than
/// `sigma.high` above falls out high, judged in rank order under the sigma families' cumulative
/// four-survivor guard.
///
/// @return success, cancellation, or a bare `ErrorCode::InvalidData` for a fit or dispersion that
///         is not finite.
template <typename T>
Status linearFit(const ExecutionContext& execution, const StackModel& model, const RejectionWorkspace& workspace,
                 RejectionCounts& counts)
{
    const auto frames = model.request().input.frameCount;
    std::uint32_t cumulative = 0u;
    // The fit never changes a sample, so the survivors of a later iteration are the ranked
    // survivors of the one before with the rejected taken out, already in rank order: the ranking
    // is sorted once and filtered after, which is the order a fresh sort of the survivors gives.
    std::uint32_t count = sortOrder(workspace, frames);
    for (bool first = true;; first = false)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
            return checkpoint;
        if (!first)
        {
            std::uint32_t kept = 0u;
            for (std::uint32_t i = 0u; i < count; ++i)
                if (rejectionSurvivor(workspace.flags[workspace.order[i]]))
                    workspace.order[kept++] = workspace.order[i];
            count = kept;
        }
        if (count < 2u)
            return Status::success();
        const T meanRank = static_cast<T>(workspace.meanRank);
        T mean = static_cast<T>(workspace.column[workspace.order[0]]);
        for (std::uint32_t i = 1u; i < count; ++i)
            mean += (static_cast<T>(workspace.column[workspace.order[i]]) - mean) *
                    static_cast<T>(workspace.reciprocals[i]);
        T covariance = T{0};
        T dx = -meanRank;
        for (std::uint32_t i = 0u; i < count; ++i, dx += T{1})
        {
            const T dy = static_cast<T>(workspace.column[workspace.order[i]]) - mean;
            covariance += (dx * dy - covariance) * static_cast<T>(workspace.reciprocals[i]);
        }
        const T slope = covariance * static_cast<T>(workspace.inverseRankVariance);
        const T intercept = mean - meanRank * slope;
        T sigma = T{0};
        for (std::uint32_t i = 0u; i < count; ++i)
            sigma += std::fabs(static_cast<T>(workspace.column[workspace.order[i]]) -
                               (slope * static_cast<T>(i) + intercept));
        sigma /= static_cast<T>(count);
        if (!std::isfinite(sigma) || !std::isfinite(slope) || !std::isfinite(intercept))
            return Status::failure(ErrorCode::InvalidData);
        bool changed = false;
        for (std::uint32_t i = 0u; i < count && count > cumulative && count - cumulative > 4u; ++i)
        {
            const auto frame = workspace.order[i];
            const T value = static_cast<T>(workspace.column[frame]);
            const int side = sideOf(static_cast<T>(slope * static_cast<T>(i) + intercept - value),
                                    static_cast<T>(value - slope * static_cast<T>(i) - intercept),
                                    static_cast<T>(sigma * static_cast<T>(model.request().rejection.sigma.low)),
                                    static_cast<T>(sigma * static_cast<T>(model.request().rejection.sigma.high)));
            if (side != 0)
            {
                record(workspace, frame, side, false, counts);
                ++cumulative;
                changed = true;
            }
        }
        if (!changed)
            return Status::success();
    }
}

/// The running moments of the deviate test's active interval, downdated as each candidate leaves.
///
/// Moment removal is constant-time, but only used when its error bound proves the same rounded
/// mean and the same critical-value decision as the two-pass Float32 compatibility statistic.
/// Near a rounding midpoint, a degenerate variance or a decision boundary the active interval is
/// rescanned. The reference backend always rescans, so differential tests exercise both paths.
struct GesdMoments
{
    /// The Float64 sum of the active samples.
    double sum = 0.0;

    /// The Float64 sum of their squares.
    double squares = 0.0;

    /// The sum of squares over every survivor, before any candidate left, which bounds the
    /// rounding error the downdates can have accumulated.
    double initialSquares = 0.0;

    /// The sum of every survivor's magnitude, which bounds the error of the downdated sum.
    double absoluteSum = 0.0;

    /// How many survivors the interval started with.
    std::uint32_t initialCount = 0u;
};

/// One step's mean and deviation of the active interval, and whether they can be trusted.
struct GesdStatistics
{
    /// The mean, narrowed to Float32 as the compatibility statistic narrows it.
    float mean = 0.0f;

    /// The unbiased standard deviation around that mean, in Float32.
    float sigma = 0.0f;

    /// A bound on the relative error of the statistic formed from these moments.
    double uncertainty = 0.0;

    /// Whether the mean and the step's decision are proven to be the rescan's; a rescan is
    /// reliable by construction.
    bool reliable = false;
};

/// How far a Float64 value lies from the edge of the interval that rounds to `rounded`: the
/// smaller distance to the midpoints between `rounded` and its two Float32 neighbours, negative
/// when the value does not round to it, and zero when either is not finite.
double roundingMargin(double value, float rounded) noexcept
{
    if (!std::isfinite(value) || !std::isfinite(rounded))
        return 0.0;
    const float previous = std::nextafter(rounded, -std::numeric_limits<float>::infinity());
    const float next = std::nextafter(rounded, std::numeric_limits<float>::infinity());
    return (std::min)(value - (static_cast<double>(previous) + rounded) * 0.5,
                      (static_cast<double>(next) + rounded) * 0.5 - value);
}

/// The active interval's statistics from its running moments, marked reliable only when the error
/// bounds prove the rounded mean is the rescan's and the variance is clear of zero.
///
/// The mean is the moments' sum over the count narrowed to Float32; the centred sum of squares
/// about that Float32 mean is expanded from the moments, which is the rescan's second pass in exact
/// arithmetic; the deviation is the Float32 square root of that sum over `count - 1`. Each bound
/// scales with the operations the downdates have performed.
GesdStatistics momentStatistics(const GesdMoments& moments, std::uint32_t count) noexcept
{
    GesdStatistics result;
    const double unroundedMean = moments.sum / count;
    result.mean = static_cast<float>(unroundedMean);
    constexpr double kDoubleErrorFactor = 64.0 * std::numeric_limits<double>::epsilon();
    constexpr double kFloatErrorFactor = 32.0 * std::numeric_limits<float>::epsilon();
    const double operations = static_cast<double>(moments.initialCount) + count + 1.0;
    const double sumError = kDoubleErrorFactor * operations * (2.0 * moments.absoluteSum + 1.0);
    if (!(roundingMargin(unroundedMean, result.mean) > sumError / count))
        return result;
    const double mean = result.mean;
    const double centered = moments.squares - 2.0 * mean * moments.sum + count * mean * mean;
    const double scale = std::fabs(moments.squares) + 2.0 * std::fabs(mean * moments.sum) + count * mean * mean;
    const double squareError = kDoubleErrorFactor * operations * (2.0 * moments.initialSquares + 1.0);
    const double varianceError = squareError + 2.0 * std::fabs(mean) * sumError +
                                 kDoubleErrorFactor * operations * (scale + 1.0) +
                                 kFloatErrorFactor * std::fabs(centered);
    if (!std::isfinite(centered) || !(centered > varianceError))
        return result;
    result.sigma = std::sqrt(static_cast<float>(centered / (count - 1u)));
    if (!(result.sigma > 0.0f) || !std::isfinite(result.sigma))
        return result;
    result.uncertainty = kFloatErrorFactor * count + 0.5 * varianceError / centered;
    result.reliable = true;
    return result;
}

/// The active interval's statistics by the adopted two passes: the Float64 sum over the
/// count narrowed to a Float32 mean, then the Float64 sum of squared distances from that mean over
/// `count - 1`, narrowed to Float32 before its square root.
GesdStatistics rescanStatistics(const float* sorted, std::uint32_t low, std::uint32_t high) noexcept
{
    const auto remaining = high - low;
    double sum = 0.0;
    for (auto i = low; i < high; ++i)
        sum += sorted[i];
    const float mean = static_cast<float>(sum / remaining);
    sum = 0.0;
    for (auto i = low; i < high; ++i)
    {
        const double difference = sorted[i] - mean;
        sum += difference * difference;
    }
    return {.mean = mean, .sigma = std::sqrt(static_cast<float>(sum / (remaining - 1u))), .reliable = true};
}

/// The generalized extreme Studentized deviate test, Rosner's procedure in its adopted form.
///
/// The survivors are ranked, narrowed to Float32 and held as a sorted active interval. Each step,
/// up to `stackGesdtMaximumOutliers` of the survivor count and while more than two samples remain,
/// takes the interval's mean and deviation, removes the endpoint farther from the mean - the low
/// one on a tie - as a candidate, and marks it significant when its distance over the deviation
/// strictly exceeds the critical value for the interval's size. A deviation of zero ends the test.
/// Every candidate up to and including the last significant one is then confirmed and rejected,
/// on the high side when its value is at or above the survivors' original median and on the low
/// side otherwise. Fewer than three survivors reject nothing.
///
/// @return success, cancellation, or a bare `ErrorCode::InvalidData` for a survivor, median or
///         statistic that is not finite in Float32.
Status gesd(const ExecutionContext& execution, const StackModel& model, const RejectionWorkspace& workspace,
            RejectionCounts& counts)
{
    const auto count = sortOrder(workspace, model.request().input.frameCount);
    if (count < 3u)
        return Status::success();
    auto* sorted = reinterpret_cast<float*>(workspace.work);
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        sorted[i] = static_cast<float>(workspace.column[workspace.order[i]]);
        if (!std::isfinite(sorted[i]))
            return Status::failure(ErrorCode::InvalidData);
    }
    const float median = count % 2u != 0u ? sorted[count / 2u] : (sorted[count / 2u - 1u] + sorted[count / 2u]) * 0.5f;
    if (!std::isfinite(median))
        return Status::failure(ErrorCode::InvalidData);
    const auto maximum = stackGesdtMaximumOutliers(count, model.request().rejection.gesdt.maximumOutlierFraction);
    std::uint32_t low = 0u;
    std::uint32_t high = count;
    std::uint32_t confirmed = 0u;
    GesdMoments moments;
    moments.initialCount = count;
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const double value = sorted[i];
        moments.sum += value;
        moments.squares += value * value;
        moments.absoluteSum += std::fabs(value);
    }
    moments.initialSquares = moments.squares;
    for (std::uint32_t iteration = 0u; iteration < maximum && high - low > 2u; ++iteration)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
            return checkpoint;
        const auto remaining = high - low;
        auto statistics =
            execution.backend() == ComputeBackend::Reference ? GesdStatistics{} : momentStatistics(moments, remaining);
        const float critical = static_cast<float>(workspace.critical[remaining - 1u]);
        if (statistics.reliable)
        {
            const float distance = (std::max)(statistics.mean - sorted[low], sorted[high - 1u] - statistics.mean);
            const float statistic = distance / statistics.sigma;
            const double scale =
                (std::max)({1.0, std::fabs(static_cast<double>(statistic)), std::fabs(static_cast<double>(critical))});
            statistics.reliable = std::fabs(static_cast<double>(statistic) - critical) > statistics.uncertainty * scale;
        }
        if (!statistics.reliable)
            statistics = rescanStatistics(sorted, low, high);
        if (!std::isfinite(statistics.mean) || !std::isfinite(statistics.sigma))
            return Status::failure(ErrorCode::InvalidData);
        if (statistics.sigma <= 0.0f)
            break;
        const float lowDistance = statistics.mean - sorted[low];
        const float highDistance = sorted[high - 1u] - statistics.mean;
        const bool removeHigh = highDistance > lowDistance;
        const auto candidate = removeHigh ? --high : low++;
        const float statistic = (removeHigh ? highDistance : lowDistance) / statistics.sigma;
        workspace.candidates[iteration] = {.frameIndex = workspace.order[candidate],
                                           .side =
                                               removeHigh ? StackGesdCandidateSide::High : StackGesdCandidateSide::Low,
                                           .significant = statistic > critical};
        counts.candidates = iteration + 1u;
        if (statistic > critical)
            confirmed = iteration + 1u;
        const double removed = sorted[candidate];
        moments.sum -= removed;
        moments.squares -= removed * removed;
    }
    for (std::uint32_t i = 0u; i < confirmed; ++i)
    {
        auto& candidate = workspace.candidates[i];
        candidate.confirmed = true;
        record(workspace, candidate.frameIndex,
               static_cast<float>(workspace.column[candidate.frameIndex]) >= median ? 1 : -1, false, counts);
    }
    return Status::success();
}

/// Prepares the linear fit's rank statistics over every frame of the model: the reciprocals
/// `1 / (i + 1)` formed in Float32, the mean rank `(frames - 1) / 2` formed in Float32, and the
/// reciprocal of the ranks' population variance accumulated as a running mean at the tile width.
template <typename T> void prepareRanks(const StackModel& model, RejectionWorkspace& workspace) noexcept
{
    const auto frames = model.request().input.frameCount;
    const T mean = static_cast<T>(static_cast<float>(frames - 1u) * 0.5f);
    T variance = T{0};
    for (std::uint32_t i = 0u; i < frames; ++i)
    {
        const T reciprocal = static_cast<T>(1.0f / static_cast<float>(i + 1u));
        workspace.reciprocals[i] = reciprocal;
        const T dx = static_cast<T>(i) - mean;
        variance += (dx * dx - variance) * reciprocal;
    }
    workspace.meanRank = mean;
    workspace.inverseRankVariance = T{1} / variance;
}

/// Runs the model's family at the tile width `T`.
template <typename T>
Status estimateAs(const ExecutionContext& execution, const StackModel& model, const RejectionWorkspace& workspace,
                  RejectionCounts& counts)
{
    switch (model.request().rejection.method)
    {
    case StackRejection::PercentileClipping:
        return percentile<T>(model, workspace, counts);
    case StackRejection::LinearFitClipping:
        return linearFit<T>(execution, model, workspace, counts);
    case StackRejection::GeneralizedEsdTest:
        return gesd(execution, model, workspace, counts);
    case StackRejection::SigmaClipping:
        return sigmaFamily<T>(execution, model, workspace, counts);
    case StackRejection::MadSigmaClipping:
        return sigmaFamily<T>(execution, model, workspace, counts);
    case StackRejection::MedianSigmaReplacement:
        return sigmaFamily<T>(execution, model, workspace, counts);
    case StackRejection::WinsorizedSigmaClipping:
        return sigmaFamily<T>(execution, model, workspace, counts);
    case StackRejection::None:
        return Status::success();
    default:
        return Status::failure(ErrorCode::InvalidArgument);
    }
}

} // namespace

RejectionWorkspace rejectionWorkspace(const StackModel& model, std::span<std::byte> bytes) noexcept
{
    const auto frames = model.request().input.frameCount;
    auto* cursor = bytes.data();
    const auto take = [&](std::uint64_t size) {
        auto* result = cursor;
        cursor += alignedStackScratchTerm(size);
        return result;
    };
    RejectionWorkspace workspace;
    workspace.column = reinterpret_cast<double*>(take(frames * sizeof(double)));
    workspace.frameRows = reinterpret_cast<StackColumnFrame*>(take(frames * kStackRejectionRowRecordBytes));
    workspace.flags = reinterpret_cast<RejectionFlag*>(take(frames));
    workspace.work = take(frames * sizeof(double));
    if (model.request().rejection.method == StackRejection::LinearFitClipping)
    {
        auto* ranks = take(2u * frames * sizeof(double));
        workspace.reciprocals = reinterpret_cast<double*>(ranks);
        workspace.order = reinterpret_cast<std::uint32_t*>(ranks + frames * sizeof(double));
    }
    if (model.request().rejection.method == StackRejection::GeneralizedEsdTest)
    {
        workspace.critical = reinterpret_cast<double*>(take(frames * sizeof(double)));
        workspace.order = reinterpret_cast<std::uint32_t*>(take(2u * frames * sizeof(std::uint32_t)));
        static_assert(sizeof(StackGesdCandidate) <= kStackGesdtCandidateBytes);
        static_assert(kStackScratchAlignmentBytes % alignof(StackGesdCandidate) == 0u);
        workspace.candidates = reinterpret_cast<StackGesdCandidate*>(
            take(stackGesdtMaximumOutliers(frames, model.request().rejection.gesdt.maximumOutlierFraction) *
                 kStackGesdtCandidateBytes));
    }
    if (model.usesFrameWeights() || model.usesLocalWeights())
        workspace.weights = reinterpret_cast<double*>(take(frames * sizeof(double)));
    return workspace;
}

Status prepareRejectionWorkspace(const ExecutionContext& execution, const StackModel& model,
                                 RejectionWorkspace& workspace)
{
    if (workspace.reciprocals != nullptr)
    {
        if (stackTileSampleTypeOf(model.request().input.descriptor.sampleType()) == SampleType::Float64)
            prepareRanks<double>(model, workspace);
        else
            prepareRanks<float>(model, workspace);
    }
    if (workspace.critical != nullptr)
        for (std::uint32_t count = 3u; count <= model.request().input.frameCount; ++count)
        {
            const Status checkpoint = execution.checkCancellation();
            if (checkpoint.failed())
                return checkpoint;
            const auto critical = studentizedDeviateCritical(count, model.request().rejection.gesdt.significance);
            if (!critical.hasValue())
                return critical.status();
            workspace.critical[count - 1u] = *critical;
        }
    return Status::success();
}

Status estimateRejection(const ExecutionContext& execution, const StackModel& model,
                         const RejectionWorkspace& workspace, RejectionCounts& counts)
{
    return stackTileSampleTypeOf(model.request().input.descriptor.sampleType()) == SampleType::Float64
               ? estimateAs<double>(execution, model, workspace, counts)
               : estimateAs<float>(execution, model, workspace, counts);
}

namespace
{
/// The one-column operations behind `rejectStackSampleColumn` and `evaluateStackGesdColumn`.
///
/// Every argument is checked before the scratch is touched, the column is copied into the
/// workspace with every frame past its end `Missing`, and the samples, the dispositions and the
/// candidate records are written only once the estimator has succeeded, so a failure leaves them
/// as they were. `candidatesWritten` is null for a plain judgement and names where the deviate
/// test's candidate count goes otherwise. Its own refusals name the public caller and the rejected parameter.
template <typename T>
Result<StackColumnRejectionOutcome>
rejectColumn(const ExecutionContext& execution, const StackModel& model, std::span<T> samples,
             std::span<StackSampleDisposition> dispositions, std::span<std::byte> scratch,
             std::span<StackGesdCandidate> candidates = {}, std::uint32_t* candidatesWritten = nullptr)
{
    const char* operation =
        candidatesWritten == nullptr ? "an::rejectStackSampleColumn" : "an::evaluateStackGesdColumn";
    const auto refuse = [operation](ErrorCode code, const char* parameter, const char* message) {
        return Status::failure(code, {.message = message,
                                      .operation = operation,
                                      .domain = ErrorDomain::Algorithms,
                                      .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
    };
    if (!execution.valid())
        return refuse(ErrorCode::NotInitialized, "execution", "the execution holds nothing");
    const auto checkpoint = execution.checkCancellation();
    if (checkpoint.failed())
        return checkpoint;
    const auto selected = selectBackend(execution);
    if (!selected.hasValue())
        return selected.status();
    const auto type = sizeof(T) == sizeof(float) ? SampleType::Float32 : SampleType::Float64;
    if (!model.usesRejection() || type != stackTileSampleTypeOf(model.request().input.descriptor.sampleType()))
        return refuse(ErrorCode::IncompatibleArguments, "model",
                      "the model does not reject columns of this representation");
    if (samples.size() > model.request().input.frameCount)
        return refuse(ErrorCode::IncompatibleArguments, "samples", "the column exceeds the model's frame count");
    if (samples.size() != dispositions.size())
        return refuse(ErrorCode::IncompatibleArguments, "dispositions", "each sample needs one disposition");
    if (scratch.size() < stackScratchBytesPerWorker(model))
        return refuse(ErrorCode::IncompatibleArguments, "scratch", "the scratch is smaller than the model requires");
    if (candidatesWritten != nullptr && model.request().rejection.method != StackRejection::GeneralizedEsdTest)
        return refuse(ErrorCode::IncompatibleArguments, "model", "candidate evaluation needs a generalized ESD model");
    if (candidatesWritten != nullptr &&
        candidates.size() < stackGesdtMaximumOutliers(static_cast<std::uint32_t>(samples.size()),
                                                      model.request().rejection.gesdt.maximumOutlierFraction))
        return refuse(ErrorCode::IncompatibleArguments, "candidates", "the candidate span is too small");
    const auto overlaps = [](std::span<const std::byte> left, std::span<const std::byte> right) {
        if (left.empty() || right.empty())
            return false;
        const auto a = reinterpret_cast<std::uintptr_t>(left.data());
        const auto b = reinterpret_cast<std::uintptr_t>(right.data());
        return a <= b ? b - a < left.size() : a - b < right.size();
    };
    if (reinterpret_cast<std::uintptr_t>(scratch.data()) % kStackScratchAlignmentBytes != 0u ||
        overlaps(std::as_bytes(samples), std::as_bytes(dispositions)) || overlaps(std::as_bytes(samples), scratch) ||
        overlaps(std::as_bytes(dispositions), scratch) ||
        reinterpret_cast<std::uintptr_t>(candidates.data()) % alignof(StackGesdCandidate) != 0u ||
        overlaps(std::as_bytes(candidates), scratch) || overlaps(std::as_bytes(candidates), std::as_bytes(samples)) ||
        overlaps(std::as_bytes(candidates), std::as_bytes(dispositions)))
        return refuse(ErrorCode::InvalidArgument, "buffers", "column buffers must be aligned and disjoint");
    StackColumnRejectionOutcome outcome;
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        if (dispositions[i] != RejectionFlag::Kept && dispositions[i] != RejectionFlag::Missing)
            return refuse(ErrorCode::InvalidArgument, "dispositions", "an input disposition must be Kept or Missing");
        if (dispositions[i] == RejectionFlag::Kept)
        {
            if (!std::isfinite(samples[i]))
                return refuse(ErrorCode::InvalidData, "samples", "a kept sample must be finite");
            ++outcome.admitted;
        }
    }
    auto workspace = rejectionWorkspace(model, scratch);
    const auto prepared = prepareRejectionWorkspace(execution, model, workspace);
    if (prepared.failed())
        return prepared;
    std::fill_n(workspace.flags, model.request().input.frameCount, RejectionFlag::Missing);
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        workspace.column[i] = samples[i];
        workspace.flags[i] = dispositions[i];
    }
    RejectionCounts counts;
    const auto estimated = estimateRejection(execution, model, workspace, counts);
    if (estimated.failed())
        return estimated;
    if (candidatesWritten != nullptr)
    {
        std::copy_n(workspace.candidates, counts.candidates, candidates.begin());
        *candidatesWritten = counts.candidates;
    }
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        const auto flag = workspace.flags[i];
        if (flag == RejectionFlag::ReplacedLow || flag == RejectionFlag::ReplacedHigh)
            samples[i] = static_cast<T>(workspace.column[i]);
        dispositions[i] = flag;
        outcome.surviving += rejectionSurvivor(flag) ? 1u : 0u;
        outcome.mappedLow += flag == RejectionFlag::Low || flag == RejectionFlag::ReplacedLow ? 1u : 0u;
        outcome.mappedHigh += flag == RejectionFlag::High || flag == RejectionFlag::ReplacedHigh ? 1u : 0u;
    }
    outcome.rejectedLow = counts.low;
    outcome.rejectedHigh = counts.high;
    return outcome;
}

template <typename T>
Result<StackGesdColumnOutcome> evaluateGesdColumn(const ExecutionContext& execution, const StackModel& model,
                                                  std::span<T> samples, std::span<StackSampleDisposition> dispositions,
                                                  std::span<std::byte> scratch,
                                                  std::span<StackGesdCandidate> candidates)
{
    std::uint32_t written = 0u;
    const auto rejected = rejectColumn(execution, model, samples, dispositions, scratch, candidates, &written);
    if (!rejected.hasValue())
        return rejected.status();
    return StackGesdColumnOutcome{.rejection = *rejected, .candidatesWritten = written};
}
} // namespace

Result<StackColumnRejectionOutcome> rejectStackSampleColumn(const ExecutionContext& execution, const StackModel& model,
                                                            std::span<float> samples,
                                                            std::span<StackSampleDisposition> dispositions,
                                                            std::span<std::byte> scratch)
{
    return rejectColumn(execution, model, samples, dispositions, scratch);
}

Result<StackColumnRejectionOutcome> rejectStackSampleColumn(const ExecutionContext& execution, const StackModel& model,
                                                            std::span<double> samples,
                                                            std::span<StackSampleDisposition> dispositions,
                                                            std::span<std::byte> scratch)
{
    return rejectColumn(execution, model, samples, dispositions, scratch);
}

Result<StackGesdColumnOutcome> evaluateStackGesdColumn(const ExecutionContext& execution, const StackModel& model,
                                                       std::span<float> samples,
                                                       std::span<StackSampleDisposition> dispositions,
                                                       std::span<std::byte> scratch,
                                                       std::span<StackGesdCandidate> candidates)
{
    return evaluateGesdColumn(execution, model, samples, dispositions, scratch, candidates);
}

Result<StackGesdColumnOutcome> evaluateStackGesdColumn(const ExecutionContext& execution, const StackModel& model,
                                                       std::span<double> samples,
                                                       std::span<StackSampleDisposition> dispositions,
                                                       std::span<std::byte> scratch,
                                                       std::span<StackGesdCandidate> candidates)
{
    return evaluateGesdColumn(execution, model, samples, dispositions, scratch, candidates);
}

} // namespace an
