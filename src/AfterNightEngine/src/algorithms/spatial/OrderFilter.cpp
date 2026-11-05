// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/SpatialPlane.h"
#include "algorithms/spatial/MedianNetworks.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// The image half of the order-statistic family, and the one place in this engine where two
// implementations of the same answer are kept on purpose.
//
// The general one is `selectWindowOrderStatistic`: it takes any window, any rank, any validity rule,
// and resolves each output by partitioning a window buffer it holds one of per chunk of rows. The
// other is a hand-pruned comparator network: it takes four window sizes, the median, and a
// rule that rejects nothing, and resolves each output with a fixed branch-free sequence of min/max
// operations.
//
// Keeping both is what makes the choice between them a measurement. They are the same answer - the tests
// compare them sample for sample over random data - so the only thing that separates them is what
// they cost, and a caller may force either so a benchmark cannot accidentally measure the other one.
// The one mixed case is explicit: a truncated border has fewer inputs than a fixed network,
// so Automatic runs the network in the complete-window interior and a direct partial sort of the
// shrunken window at the edge, counted with the general selection's outputs.

namespace an
{
namespace
{

using spatial_detail::filterEveryChannel;
using spatial_detail::spatialFailure;
using spatial_detail::SpatialGeometry;
using spatial_detail::spatialLimitFailure;

constexpr const char* kOperation = "an::filterImageOrderStatistic";
constexpr const char* kFootprintOperation = "an::filterImageFootprintOrderStatistic";

/// The median of one complete square window, from the comparator network for its size.
///
/// The window is passed in row-major order and the network reorders it in place. Every sample must
/// be a number, because a minimum or maximum involving a NaN depends on argument order and the
/// network would then return an order-dependent answer; the callers check the plane first.
template <std::uint32_t RadiusV, std::size_t InputsV>
[[nodiscard]]
#if defined(__clang__)
__attribute__((always_inline))
#endif
inline float networkMedian(std::array<float, InputsV>& values) noexcept
{
    static_assert(InputsV == (2u * RadiusV + 1u) * (2u * RadiusV + 1u));
    if constexpr (RadiusV == 1u)
    {
        return ported_detail::median9f(values[0], values[1], values[2], values[3], values[4], values[5], values[6],
                                       values[7], values[8]);
    }
    else if constexpr (RadiusV == 2u)
    {
        return ported_detail::median5x5(values.data());
    }
    else if constexpr (RadiusV == 3u)
    {
        return ported_detail::median7x7(values.data());
    }
    else
    {
        static_assert(RadiusV == 4u);
        return ported_detail::median9x9(values.data());
    }
}

/// What one windowed row costs, for the grain policy.
///
/// The loop walks rows, so the item is a row and the cost has to be one too: a window gathered and
/// ordered for every one of its samples. Stating it per sample would put a whole picture in one
/// chunk and leave the filter on a single thread.
///
/// @param width how many pixels one row holds.
/// @param windowSamples how many samples one window holds.
/// @return the cost of one row.
[[nodiscard]] TaskCost windowRowCost(std::uint32_t width, std::uint32_t windowSamples) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * windowSamples * sizeof(float),
                    .operationsPerItem = samples * windowSamples * 4u};
}

/// What one scanned row costs, for the grain policy of the finiteness pass.
///
/// @param width how many pixels one row holds.
/// @return the cost of one row.
[[nodiscard]] TaskCost scanRowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * sizeof(float), .operationsPerItem = samples * 2u};
}

/// What the two implementations produced, across every pass and every channel.
struct OrderCounters
{
    /// Outputs the comparator network produced.
    std::atomic<std::uint64_t> networkOutputs{0u};

    /// Outputs the general selection and the truncated-edge partial sort produced.
    std::atomic<std::uint64_t> selectionOutputs{0u};
};

/// Whether every sample of a plane is a number.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the samples to scan.
/// @return success; `ErrorCode::InvalidData` for a sample that is not finite; or an execution or
///         cancellation failure.
[[nodiscard]] Status checkPlaneIsFinite(const ExecutionContext& execution, PlaneView<const float> plane)
{
    const std::uint32_t width = plane.width();
    return parallelFor(execution, plane.height(), scanRowCost(width), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const std::span<const float> row = *plane.row(static_cast<std::uint32_t>(index));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!sampleIsFinite(row[x]))
                {
                    return spatialFailure(ErrorCode::InvalidData, kOperation,
                                          "an order statistic has no defined rank over a sample that is not a "
                                          "number; state a validity rule that rejects it");
                }
            }
        }
        return Status::success();
    });
}

/// Produces the median of every window of a plane with a comparator network.
///
/// Every window is full: a border rule answers a coordinate outside the plane with a sample or a
/// constant, so the network always has exactly its input count and the border needs no second
/// implementation - only a slower gather.
///
/// @tparam RadiusV half the square window, which fixes the window size at compile time.
/// @param execution the operation policy and engine-context lease.
/// @param input the plane to read.
/// @param output the plane to write, of the same extent and disjoint from the input.
/// @param border how a coordinate outside the plane is resolved.
/// @param constantBorder what such a coordinate reads under a constant border.
/// @param written how many outputs were produced, added to.
/// @return success; `ErrorCode::InvalidData` for a sample that is not finite; or an execution or
///         cancellation failure.
template <std::uint32_t RadiusV>
[[nodiscard]] Status medianNetworkPlane(const ExecutionContext& execution, PlaneView<const float> input,
                                        PlaneView<float> output, KernelBorderMode border, float constantBorder,
                                        std::atomic<std::uint64_t>& written)
{
    // The window is square and its radius is a template argument, so the gather has compile-time
    // extents and its destination slots are compile-time indices. That is what lets the window live
    // in registers rather than on the stack: a gather whose write index is a running counter is one
    // the compiler has to keep in memory, and a network reading it from memory is a slower spelling
    // of the loop it replaced.
    constexpr std::uint32_t kAcross = 2u * RadiusV + 1u;
    constexpr std::uint32_t kInputs = kAcross * kAcross;
    // Every sample participates under the rule this path is chosen for, so "is any selected sample
    // not a number" is a question about the whole plane rather than about a window. Asking it once
    // costs one pass and leaves the window gather with no test in it at all; asking it per window
    // sample would cost the window size times as many.
    const Status finite = checkPlaneIsFinite(execution, input);
    if (finite.failed())
    {
        return finite;
    }

    const std::int64_t width = static_cast<std::int64_t>(input.width());
    const std::int64_t height = static_cast<std::int64_t>(input.height());
    constexpr std::int64_t kReach = static_cast<std::int64_t>(RadiusV);
    // Where the whole window lies inside the row, the gather is a contiguous read at a known offset
    // and the border rule is never consulted. Splitting the row here rather than testing every
    // sample is what leaves the network itself as the cost of the filter.
    const std::int64_t interiorFirst = (std::min)(kReach, width);
    const std::int64_t interiorLast = (std::max)(interiorFirst, width - kReach);
    return parallelFor(execution, input.height(), windowRowCost(input.width(), kInputs), [&](TaskRange range) {
        std::array<const float*, kAcross> windowRows{};
        std::uint64_t produced = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::int64_t>(index);
            const std::span<float> answer = *output.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t offset = 0u; offset < kAcross; ++offset)
            {
                const std::int64_t sourceRow = y + static_cast<std::int64_t>(offset) - kReach;
                const std::int64_t resolved = kernelBorderIndex(sourceRow, height, border);
                windowRows[offset] =
                    resolved == height ? nullptr : input.row(static_cast<std::uint32_t>(resolved))->data();
            }

            const auto atBorder = [&](std::int64_t x) {
                std::array<float, kInputs> values;
                for (std::uint32_t row = 0u; row < kAcross; ++row)
                {
                    const float* const samples = windowRows[row];
                    for (std::uint32_t offset = 0u; offset < kAcross; ++offset)
                    {
                        float sample = constantBorder;
                        if (samples != nullptr)
                        {
                            const std::int64_t column =
                                kernelBorderIndex(x + static_cast<std::int64_t>(offset) - kReach, width, border);
                            sample = column == width ? constantBorder : samples[column];
                        }
                        values[row * kAcross + offset] = sample;
                    }
                }
                return networkMedian<RadiusV>(values);
            };

            for (std::int64_t x = 0; x < interiorFirst; ++x)
            {
                answer[static_cast<std::size_t>(x)] = atBorder(x);
            }
            for (std::int64_t x = interiorFirst; x < interiorLast; ++x)
            {
                std::array<float, kInputs> values;
                for (std::uint32_t row = 0u; row < kAcross; ++row)
                {
                    const float* const samples = windowRows[row];
                    if (samples == nullptr)
                    {
                        for (std::uint32_t offset = 0u; offset < kAcross; ++offset)
                        {
                            values[row * kAcross + offset] = constantBorder;
                        }
                        continue;
                    }
                    const float* const start = samples + (x - kReach);
                    for (std::uint32_t offset = 0u; offset < kAcross; ++offset)
                    {
                        values[row * kAcross + offset] = start[offset];
                    }
                }
                answer[static_cast<std::size_t>(x)] = networkMedian<RadiusV>(values);
            }
            for (std::int64_t x = interiorLast; x < width; ++x)
            {
                answer[static_cast<std::size_t>(x)] = atBorder(x);
            }
            produced += static_cast<std::uint64_t>(width);
        }
        written.fetch_add(produced, std::memory_order_relaxed);
        return Status::success();
    });
}

/// Produces a truncated-border median with networks in the full-window interior.
///
/// The changing border population cannot enter a fixed-input comparator network. Automatic keeps
/// the network where the complete square lies inside the image and partially sorts only the edge
/// windows; this keeps the truncated border's meaning without throwing away the measured network
/// speed over almost the whole frame. An edge window with an even count answers the mean of its two
/// middle samples, taken in Float64 and narrowed once, which is the linear interpolation the
/// request names.
///
/// @tparam RadiusV half the square window.
/// @param execution the operation policy and engine-context lease.
/// @param input the finite all-valid plane to read.
/// @param output the disjoint plane to write.
/// @param networkWritten complete interior outputs, added to.
/// @param selectionWritten truncated edge outputs, added to.
/// @return success, or a finiteness, execution or cancellation failure.
template <std::uint32_t RadiusV>
[[nodiscard]] Status medianTruncatedPlane(const ExecutionContext& execution, PlaneView<const float> input,
                                          PlaneView<float> output, std::atomic<std::uint64_t>& networkWritten,
                                          std::atomic<std::uint64_t>& selectionWritten)
{
    constexpr std::uint32_t kAcross = 2u * RadiusV + 1u;
    constexpr std::uint32_t kInputs = kAcross * kAcross;
    const Status finite = checkPlaneIsFinite(execution, input);
    if (finite.failed())
    {
        return finite;
    }

    const std::uint32_t width = input.width();
    const std::uint32_t height = input.height();
    const std::uint32_t interiorFirstX = (std::min)(RadiusV, width);
    const std::uint32_t interiorLastX = (std::max)(interiorFirstX, width - (std::min)(RadiusV, width));
    const std::uint32_t interiorFirstY = (std::min)(RadiusV, height);
    const std::uint32_t interiorLastY = (std::max)(interiorFirstY, height - (std::min)(RadiusV, height));
    return parallelFor(execution, height, windowRowCost(width, kInputs), [&](TaskRange range) {
        std::uint64_t networkProduced = 0u;
        std::uint64_t selectionProduced = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<float> answer = *output.row(y);
            const bool interiorRow = y >= interiorFirstY && y < interiorLastY;
            std::array<const float*, kAcross> interiorRows{};
            if (interiorRow)
            {
                for (std::uint32_t row = 0u; row < kAcross; ++row)
                {
                    interiorRows[row] = input.row(y - RadiusV + row)->data();
                }
            }
            const auto atTruncatedBorder = [&](std::uint32_t x) {
                std::array<float, kInputs> values;
                std::uint32_t held = 0u;
                const std::uint32_t firstY = y > RadiusV ? y - RadiusV : 0u;
                const std::uint32_t lastY = y + (std::min)(RadiusV, height - 1u - y);
                const std::uint32_t firstX = x > RadiusV ? x - RadiusV : 0u;
                const std::uint32_t lastX = x + (std::min)(RadiusV, width - 1u - x);
                for (std::uint32_t sourceY = firstY; sourceY <= lastY; ++sourceY)
                {
                    const std::span<const float> row = *input.row(sourceY);
                    for (std::uint32_t sourceX = firstX; sourceX <= lastX; ++sourceX)
                    {
                        values[held] = row[sourceX];
                        ++held;
                    }
                }
                const std::uint32_t upper = held / 2u;
                std::nth_element(values.begin(), values.begin() + upper, values.begin() + held);
                if ((held & 1u) != 0u)
                {
                    return values[upper];
                }
                const float lower = *std::max_element(values.begin(), values.begin() + upper);
                return static_cast<float>((static_cast<double>(lower) + values[upper]) * 0.5);
            };

            if (!interiorRow)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    answer[x] = atTruncatedBorder(x);
                }
                selectionProduced += width;
                continue;
            }
            for (std::uint32_t x = 0u; x < interiorFirstX; ++x)
            {
                answer[x] = atTruncatedBorder(x);
            }
            selectionProduced += interiorFirstX;
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety) interleave(enable)
#endif
            for (std::uint32_t x = interiorFirstX; x < interiorLastX; ++x)
            {
                // Every slot is assigned below. Value-initializing this wide array first adds a
                // full redundant store pass to every output window.
                std::array<float, kInputs> values;
                for (std::uint32_t row = 0u; row < kAcross; ++row)
                {
                    const float* const start = interiorRows[row] + x - RadiusV;
                    for (std::uint32_t offset = 0u; offset < kAcross; ++offset)
                    {
                        values[row * kAcross + offset] = start[offset];
                    }
                }
                answer[x] = networkMedian<RadiusV>(values);
            }
            networkProduced += interiorLastX - interiorFirstX;
            for (std::uint32_t x = interiorLastX; x < width; ++x)
            {
                answer[x] = atTruncatedBorder(x);
            }
            selectionProduced += width - interiorLastX;
        }
        networkWritten.fetch_add(networkProduced, std::memory_order_relaxed);
        selectionWritten.fetch_add(selectionProduced, std::memory_order_relaxed);
        return Status::success();
    });
}

/// Runs one pass of the comparator network, choosing the instantiation the window radius names.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the plane to read.
/// @param output the plane to write.
/// @param request the window and border contract.
/// @param written how many outputs were produced, added to.
/// @return success, or whatever `medianNetworkPlane` reported.
[[nodiscard]] Status runNetworkPass(const ExecutionContext& execution, PlaneView<const float> input,
                                    PlaneView<float> output, const OrderFilterRequest& request,
                                    std::atomic<std::uint64_t>& written)
{
    switch (request.radiusX)
    {
    case 1u:
        return medianNetworkPlane<1u>(execution, input, output, request.border, request.constantBorder, written);
    case 2u:
        return medianNetworkPlane<2u>(execution, input, output, request.border, request.constantBorder, written);
    case 3u:
        return medianNetworkPlane<3u>(execution, input, output, request.border, request.constantBorder, written);
    case 4u:
        return medianNetworkPlane<4u>(execution, input, output, request.border, request.constantBorder, written);
    default:
        break;
    }
    return spatialFailure(ErrorCode::Unsupported, kOperation, "no network is generated for this window");
}

/// Runs the automatic network/interior and truncated-selection/edge combination.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the plane to read.
/// @param output the plane to write.
/// @param request the square window.
/// @param networkWritten interior outputs, added to.
/// @param selectionWritten edge outputs, added to.
/// @return success, or whatever the selected instantiation reported.
[[nodiscard]] Status runTruncatedPass(const ExecutionContext& execution, PlaneView<const float> input,
                                      PlaneView<float> output, const OrderFilterRequest& request,
                                      std::atomic<std::uint64_t>& networkWritten,
                                      std::atomic<std::uint64_t>& selectionWritten)
{
    switch (request.radiusX)
    {
    case 1u:
        return medianTruncatedPlane<1u>(execution, input, output, networkWritten, selectionWritten);
    case 2u:
        return medianTruncatedPlane<2u>(execution, input, output, networkWritten, selectionWritten);
    case 3u:
        return medianTruncatedPlane<3u>(execution, input, output, networkWritten, selectionWritten);
    case 4u:
        return medianTruncatedPlane<4u>(execution, input, output, networkWritten, selectionWritten);
    default:
        break;
    }
    return spatialFailure(ErrorCode::Unsupported, kOperation, "no network is generated for this window");
}

/// How many minimum and maximum operations the comparator network for a window of this size applies
/// per output, counted from the networks in `MedianNetworks.h`.
///
/// @param radius half the square window.
/// @return the operation count, or zero when no network serves it.
[[nodiscard]] std::uint32_t networkComparatorCount(std::uint32_t radius) noexcept
{
    switch (radius)
    {
    case 1u:
        return 30u;
    case 2u:
        return 174u;
    case 3u:
        return 578u;
    case 4u:
        return 1242u;
    default:
        break;
    }
    return 0u;
}

/// Whether a request has the shape and validity a comparator network can serve.
///
/// These conditions are shared by a whole-plane network and an automatic truncated-border hybrid:
/// a window with a comparator network, a linearly interpolated middle rank, and a rule under which
/// every in-bounds sample participates.
///
/// @param request the window, rank and method.
/// @param selection which samples mean something.
/// @return true when the network may run over at least the complete-window interior.
[[nodiscard]] bool networkShapeApplies(const OrderFilterRequest& request, const SpatialSelection& selection) noexcept
{
    return hasMedianNetwork(request.radiusX, request.radiusY) && request.quantile == 0.5 &&
           request.interpolation == QuantileInterpolation::Linear && selection.mask == nullptr &&
           selection.validity.rule() == ValidityRule::AllValid;
}

} // namespace

const char* orderFilterMethodName(OrderFilterMethod method) noexcept
{
    switch (method)
    {
    case OrderFilterMethod::Automatic:
        return "Automatic";
    case OrderFilterMethod::SelectionKernel:
        return "SelectionKernel";
    case OrderFilterMethod::SortingNetwork:
        return "SortingNetwork";
    }
    return "UnknownOrderFilterMethod";
}

Result<OrderFilterResult> filterImageOrderStatistic(const ExecutionContext& execution, SampleType sampleType,
                                                    const void* source, const ImageLayout& sourceLayout,
                                                    void* destination, const ImageLayout& destinationLayout,
                                                    const OrderFilterRequest& request,
                                                    const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (!isOrderFilterMethod(request.method))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kOperation,
                              "the order-filter method is not one this build implements");
    }
    if (!isQuantileInterpolation(request.interpolation))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kOperation,
                              "the interpolation rule is not one this build implements");
    }
    if (!isKernelBorderMode(request.border))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kOperation,
                              "the order-filter border is not one this build implements");
    }
    if (!std::isfinite(request.constantBorder))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kOperation,
                              "a border value that is not a number has no rank among the samples around it");
    }
    if (!std::isfinite(request.amount) || request.amount < 0.0 || request.amount > 1.0)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kOperation,
                              "a blend is a finite fraction of the filtered value, within zero and one");
    }
    if (!std::isfinite(request.quantile) || request.quantile < 0.0 || request.quantile > 1.0)
    {
        return spatialFailure(ErrorCode::OutOfRange, kOperation, "a quantile is a fraction within zero and one");
    }
    if (request.iterations == 0u || request.iterations > kMaxOrderFilterIterations)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kOperation,
                                   "an order filter runs at least once and at most the stated number of times",
                                   request.iterations, kMaxOrderFilterIterations);
    }
    if (request.radiusX > kMaxStencilRadius || request.radiusY > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kOperation, "a window reaches further than one filter may",
                                   (std::max)(request.radiusX, request.radiusY), kMaxStencilRadius);
    }
    const std::uint64_t windowSamples = (2u * static_cast<std::uint64_t>(request.radiusX) + 1u) *
                                        (2u * static_cast<std::uint64_t>(request.radiusY) + 1u);
    if (windowSamples > kMaxOrderWindowSamples)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kOperation,
                                   "the window holds more samples than one order filter may rank", windowSamples,
                                   kMaxOrderWindowSamples);
    }

    const auto window = static_cast<std::uint32_t>(windowSamples);
    const bool networkShape = networkShapeApplies(request, selection);
    const bool fullNetwork = networkShape && request.border != KernelBorderMode::NotApplicable;
    if (request.method == OrderFilterMethod::SortingNetwork && !fullNetwork)
    {
        return spatialFailure(ErrorCode::Unsupported, kOperation,
                              "no network is generated for this window, rank and validity rule; the general "
                              "selection serves them and is what Automatic would have chosen");
    }
    const bool automaticTruncated = request.method == OrderFilterMethod::Automatic && networkShape &&
                                    request.border == KernelBorderMode::NotApplicable;
    const OrderFilterMethod method = automaticTruncated
                                         ? OrderFilterMethod::Automatic
                                         : ((request.method == OrderFilterMethod::SelectionKernel || !fullNetwork)
                                                ? OrderFilterMethod::SelectionKernel
                                                : OrderFilterMethod::SortingNetwork);

    OrderFilterResult answer{};
    answer.method = method;
    answer.windowSamples = window;
    answer.iterations = request.iterations;
    answer.networkComparators =
        method != OrderFilterMethod::SelectionKernel ? networkComparatorCount(request.radiusX) : 0u;

    // A pass that leaves a rejected centre untouched cannot use the destination as half of its
    // ping-pong, because an earlier pass would have written every position of it. Where nothing is
    // rejected, the destination is the second buffer and the operation holds one working plane
    // instead of two.
    const bool gatesWrites = selection.mask != nullptr || selection.validity.rule() != ValidityRule::AllValid;
    spatial_detail::SpatialPlaneBuffer evenBuffer;
    spatial_detail::SpatialPlaneBuffer oddBuffer;
    if (request.iterations > 1u)
    {
        Result<spatial_detail::SpatialPlaneBuffer> even =
            spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kOperation);
        if (!even.hasValue())
        {
            return even.status();
        }
        evenBuffer = std::move(even).value();
        answer.filter.workingBytes += evenBuffer.bytes();
        if (gatesWrites)
        {
            Result<spatial_detail::SpatialPlaneBuffer> odd =
                spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kOperation);
            if (!odd.hasValue())
            {
                return odd.status();
            }
            oddBuffer = std::move(odd).value();
            answer.filter.workingBytes += oddBuffer.bytes();
        }
    }

    OrderCounters counters{};
    std::uint64_t kernelScratchBytes = 0u;
    const OrderWindowRequest windowRequest{.radiusX = request.radiusX,
                                           .radiusY = request.radiusY,
                                           .quantile = request.quantile,
                                           .interpolation = request.interpolation,
                                           .border = request.border,
                                           .constantBorder = request.constantBorder};
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kOperation, answer.filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const PlaneView<float> oddPlane = gatesWrites && request.iterations > 1u ? oddBuffer.view() : output;
            PlaneView<const float> current = input;
            for (std::uint32_t pass = 0u; pass < request.iterations; ++pass)
            {
                const bool last = pass + 1u == request.iterations;
                const PlaneView<float> target =
                    last ? output : (((request.iterations - 1u - pass) % 2u == 0u) ? oddPlane : evenBuffer.view());
                if (!last && gatesWrites)
                {
                    // What the pass leaves untouched has to be what it read, not what the buffer
                    // happened to hold, so that a rejected centre carries its value forward.
                    const Status seeded = spatial_detail::copyPlane(execution, current, target, kOperation);
                    if (seeded.failed())
                    {
                        return seeded;
                    }
                }

                if (method == OrderFilterMethod::SortingNetwork)
                {
                    const Status ran = runNetworkPass(execution, current, target, request, counters.networkOutputs);
                    if (ran.failed())
                    {
                        return ran;
                    }
                    answer.filter.plannedWorkers =
                        (std::max)(answer.filter.plannedWorkers,
                                   plannedWorkerCount(execution, input.height(), windowRowCost(input.width(), window)));
                }
                else if (method == OrderFilterMethod::Automatic)
                {
                    const Status ran = runTruncatedPass(execution, current, target, request, counters.networkOutputs,
                                                        counters.selectionOutputs);
                    if (ran.failed())
                    {
                        return ran;
                    }
                    answer.filter.plannedWorkers =
                        (std::max)(answer.filter.plannedWorkers,
                                   plannedWorkerCount(execution, input.height(), windowRowCost(input.width(), window)));
                }
                else
                {
                    const Result<OrderWindowResult> ran =
                        selectWindowOrderStatistic(execution, current, target, windowRequest, validity);
                    if (!ran.hasValue())
                    {
                        return ran.status();
                    }
                    counters.selectionOutputs.fetch_add(ran.value().run.outputsWritten, std::memory_order_relaxed);
                    answer.filter.plannedWorkers = (std::max)(answer.filter.plannedWorkers, ran.value().plannedWorkers);
                    kernelScratchBytes = (std::max)(kernelScratchBytes, ran.value().scratchBytes);
                }

                if (request.amount != 1.0)
                {
                    const Status blended =
                        spatial_detail::blendPlane(execution, current, target, validity, request.amount, kOperation);
                    if (blended.failed())
                    {
                        return blended;
                    }
                }
                current = target;
            }
            answer.filter.imagePasses += request.iterations;
            answer.filter.horizontalRadius = request.radiusX;
            answer.filter.verticalRadius = request.radiusY;
            answer.filter.run.selection.total += static_cast<std::uint64_t>(input.width()) * input.height();
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }

    answer.networkOutputs = counters.networkOutputs.load(std::memory_order_relaxed);
    answer.selectionOutputs = counters.selectionOutputs.load(std::memory_order_relaxed);
    answer.filter.workingBytes += kernelScratchBytes;
    answer.filter.precision = ComputePrecision::Float32;
    // Only the last pass writes the destination, so what the operation wrote is one pass of it.
    const std::uint64_t perPass = answer.networkOutputs + answer.selectionOutputs;
    answer.filter.run.outputsWritten = request.iterations == 0u ? 0u : perPass / request.iterations;
    answer.filter.run.selection.valid = answer.filter.run.outputsWritten;
    return answer;
}

Result<OrderFilterResult> filterImageFootprintOrderStatistic(const ExecutionContext& execution, SampleType sampleType,
                                                             const void* source, const ImageLayout& sourceLayout,
                                                             void* destination, const ImageLayout& destinationLayout,
                                                             const FootprintOrderFilterRequest& request,
                                                             const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           request.output, selection, kFootprintOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (!isQuantileInterpolation(request.interpolation))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kFootprintOperation,
                              "the interpolation rule is not one this build implements");
    }
    if (!isKernelBorderMode(request.border))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kFootprintOperation,
                              "the order-filter border is not one this build implements");
    }
    if (!std::isfinite(request.constantBorder))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kFootprintOperation,
                              "a border value that is not a number has no rank among the samples around it");
    }
    if (!std::isfinite(request.amount) || request.amount < 0.0 || request.amount > 1.0)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kFootprintOperation,
                              "a blend is a finite fraction of the filtered value, within zero and one");
    }
    if (!std::isfinite(request.quantile) || request.quantile < 0.0 || request.quantile > 1.0)
    {
        return spatialFailure(ErrorCode::OutOfRange, kFootprintOperation,
                              "a quantile is a fraction within zero and one");
    }
    if (request.iterations == 0u || request.iterations > kMaxOrderFilterIterations)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kFootprintOperation,
                                   "an order filter runs at least once and at most the stated number of times",
                                   request.iterations, kMaxOrderFilterIterations);
    }
    if (request.radiusX > kMaxStencilRadius || request.radiusY > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kFootprintOperation,
                                   "a structuring element reaches further than one filter may",
                                   (std::max)(request.radiusX, request.radiusY), kMaxStencilRadius);
    }
    const std::uint64_t windowCells = (2u * static_cast<std::uint64_t>(request.radiusX) + 1u) *
                                      (2u * static_cast<std::uint64_t>(request.radiusY) + 1u);
    if (windowCells > kMaxOrderWindowSamples)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kFootprintOperation,
                                   "the structuring element covers more cells than one order filter may rank",
                                   windowCells, kMaxOrderWindowSamples);
    }
    if (request.footprint.size() != windowCells)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kFootprintOperation,
                              "a structuring element holds one byte for every cell of the window it reaches across");
    }
    const auto members = static_cast<std::uint32_t>(std::count_if(request.footprint.begin(), request.footprint.end(),
                                                                  [](std::uint8_t cell) { return cell != 0u; }));
    if (members == 0u)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kFootprintOperation,
                              "a structuring element that marks no cell has no window to rank");
    }

    OrderFilterResult answer{};
    answer.method = OrderFilterMethod::SelectionKernel;
    answer.windowSamples = members;
    answer.iterations = request.iterations;

    // A pass that leaves a center untouched - a rejected one, or one whose element holds no sample -
    // cannot use the destination as half of its ping-pong, because an earlier pass would have written
    // every position of it. An element that marks its center and a rule that rejects nothing leave no
    // such center, and the destination is then the second buffer.
    const bool marksCenter = request.footprint[static_cast<std::size_t>(windowCells / 2u)] != 0u;
    const bool gatesWrites =
        selection.mask != nullptr || selection.validity.rule() != ValidityRule::AllValid || !marksCenter;
    spatial_detail::SpatialPlaneBuffer evenBuffer;
    spatial_detail::SpatialPlaneBuffer oddBuffer;
    if (request.iterations > 1u)
    {
        Result<spatial_detail::SpatialPlaneBuffer> even =
            spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kFootprintOperation);
        if (!even.hasValue())
        {
            return even.status();
        }
        evenBuffer = std::move(even).value();
        answer.filter.workingBytes += evenBuffer.bytes();
        if (gatesWrites)
        {
            Result<spatial_detail::SpatialPlaneBuffer> odd =
                spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kFootprintOperation);
            if (!odd.hasValue())
            {
                return odd.status();
            }
            oddBuffer = std::move(odd).value();
            answer.filter.workingBytes += oddBuffer.bytes();
        }
    }

    std::atomic<std::uint64_t> outputs{0u};
    std::uint64_t kernelScratchBytes = 0u;
    const OrderWindowRequest windowRequest{.radiusX = request.radiusX,
                                           .radiusY = request.radiusY,
                                           .quantile = request.quantile,
                                           .interpolation = request.interpolation,
                                           .border = request.border,
                                           .constantBorder = request.constantBorder};
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kFootprintOperation, answer.filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const PlaneView<float> oddPlane = gatesWrites && request.iterations > 1u ? oddBuffer.view() : output;
            PlaneView<const float> current = input;
            for (std::uint32_t pass = 0u; pass < request.iterations; ++pass)
            {
                const bool last = pass + 1u == request.iterations;
                const PlaneView<float> target =
                    last ? output : (((request.iterations - 1u - pass) % 2u == 0u) ? oddPlane : evenBuffer.view());
                if (!last && gatesWrites)
                {
                    // What the pass leaves untouched has to be what it read, not what the buffer
                    // happened to hold, so that an unranked center carries its value forward.
                    const Status seeded = spatial_detail::copyPlane(execution, current, target, kFootprintOperation);
                    if (seeded.failed())
                    {
                        return seeded;
                    }
                }

                const Result<OrderWindowResult> ran = selectFootprintOrderStatistic(
                    execution, current, target, windowRequest, request.footprint, validity);
                if (!ran.hasValue())
                {
                    return ran.status();
                }
                outputs.fetch_add(ran.value().run.outputsWritten, std::memory_order_relaxed);
                answer.filter.plannedWorkers = (std::max)(answer.filter.plannedWorkers, ran.value().plannedWorkers);
                kernelScratchBytes = (std::max)(kernelScratchBytes, ran.value().scratchBytes);

                if (request.amount != 1.0)
                {
                    const Status blended = spatial_detail::blendPlane(execution, current, target, validity,
                                                                      request.amount, kFootprintOperation);
                    if (blended.failed())
                    {
                        return blended;
                    }
                }
                current = target;
            }
            answer.filter.imagePasses += request.iterations;
            answer.filter.horizontalRadius = request.radiusX;
            answer.filter.verticalRadius = request.radiusY;
            answer.filter.run.selection.total += static_cast<std::uint64_t>(input.width()) * input.height();
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }

    answer.selectionOutputs = outputs.load(std::memory_order_relaxed);
    answer.filter.workingBytes += kernelScratchBytes;
    answer.filter.precision = ComputePrecision::Float32;
    // Only the last pass writes the destination, so what the operation wrote is one pass of it.
    answer.filter.run.outputsWritten = answer.selectionOutputs / request.iterations;
    answer.filter.run.selection.valid = answer.filter.run.outputsWritten;
    return answer;
}

} // namespace an
