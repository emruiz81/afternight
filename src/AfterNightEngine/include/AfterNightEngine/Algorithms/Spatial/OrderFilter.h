// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Replacing every sample by a rank of the window around it.
///
/// The median filter and its family. It is the neighbourhood filter that is not a convolution: no
/// weighting of a window produces its answer, and the thing that makes it useful - a value that is
/// unmoved by a neighbour a thousand times too bright - is exactly the thing that makes it
/// expensive.
///
/// **The window is stated and checked before a sample is read.** The window is `2 * radiusX + 1` by
/// `2 * radiusY + 1` - a seventeen by seventeen window, an ordinary ask for a cosmetic pass over a
/// wide-field frame, is two hundred and eighty-nine samples - it is compared against
/// `kMaxOrderWindowSamples` before anything is allocated or read, and a window above it is a returned
/// refusal carrying the window and the limit.
///
/// **The rank is exact and is taken in one processing domain.** Every representation is converted
/// once to Float32 on the way into the working plane, so the eight resident types share one
/// definition of "the sample an ascending sort would place at this rank" rather than one per type.
/// The conversion rounds Float64, UInt32 and Int32, which the family's precision rule in
/// `Algorithms/Spatial/SpatialFilter.h` admits or refuses. An invalid window sample is excluded from
/// the window rather than substituted, because an order statistic has no substitute value that does
/// not move the answer; a sample outside the image follows the border rule.
/// `NotApplicable` omits that sample and ranks the smaller border window.
///
/// **Two implementations produce the same picture, and which one ran is reported.** The median of a
/// three-by-three, five-by-five, seven-by-seven or nine-by-nine window over an image in which every
/// sample means something is resolved by hand-pruned comparator networks, kept as private copied
/// code because they measured faster than a generated network: branch-free sequences of minimum and
/// maximum operations over a window held in registers. Everything else is resolved by the general
/// bounded-scratch selection in `Algorithms/Kernel/HistogramOrderKernel.h`. A caller may force
/// either, which is what makes the two comparable rather than merely both present, and
/// `OrderFilterResult` reports how many outputs each produced. Under the truncated border,
/// `Automatic` uses the network over the complete-window interior and a direct partial sort of the
/// shrunken window at the edge, and reports both output counts.
///
/// **A structuring element narrows the window.** `filterImageFootprintOrderStatistic` ranks the cells
/// an element marks instead of the whole rectangle - the morphological selection, of which erosion,
/// dilation and the element's median are three ranks - under every other rule here, through the
/// general selection alone.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <span>

namespace an
{

/// Which implementation resolves a window's rank.
enum class OrderFilterMethod : std::uint8_t
{
    /// The comparator network where one exists and the request admits it, and the general
    /// selection everywhere else. What a caller that has no reason to care should ask for.
    Automatic = 0u,

    /// The general bounded-scratch selection, for every pixel.
    ///
    /// Always available, whatever the window, the rank or the validity rule.
    SelectionKernel = 1u,

    /// The comparator network, refused when the complete request does not admit one.
    ///
    /// A request admits it when its window is one `hasMedianNetwork` names, its quantile is
    /// one half under `QuantileInterpolation::Linear`, and its selection is the all-valid rule with
    /// no mask.
    ///
    /// Asking for it by name is how a test compares the two implementations and how a measurement
    /// isolates one of them. A variable-size truncated border cannot be served entirely by a fixed
    /// network and is therefore refused when forced; `Automatic` may still use the network in its
    /// full-window interior. A request the network cannot serve is a returned refusal rather than a
    /// silent fall back, so a benchmark cannot accidentally measure the other path.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SortingNetwork = 2u
};

/// Whether a value names a method this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the value to inspect.
/// @return true for every implemented method.
[[nodiscard]] constexpr bool isOrderFilterMethod(OrderFilterMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(OrderFilterMethod::SortingNetwork);
}

/// The method's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param method the method to name. A value outside the enumeration is named
///        `"UnknownOrderFilterMethod"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* orderFilterMethodName(OrderFilterMethod method) noexcept;

/// The most times one call may apply the filter to its own output.
///
/// A median filter is idempotent after a few passes and a caller asking for hundreds has made an
/// arithmetic mistake rather than an aesthetic decision. Sixteen is well beyond where the picture
/// stops changing and is checked before the first pass rather than discovered by a run that does not
/// end.
inline constexpr std::uint32_t kMaxOrderFilterIterations = 16u;

/// Whether a comparator network exists for a window of these radii.
///
/// The four square windows the hand-pruned networks cover: three by three, five by five, seven by
/// seven and nine by nine. They are private copied code, chosen because a generated Batcher network
/// lost the measured performance comparison at nine by nine. Square because the window extent is a
/// compile-time argument of the network code, so each shape that is served is one more instantiation,
/// and a non-square window of the same sample count is a different shape rather than the same one
/// rearranged.
///
/// It stops at eighty-one samples. The comparator count grows faster than the window and the window
/// stops fitting in registers, so the margin over the general selection narrows with every step;
/// wider windows use the general selection.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param radiusX half the window width.
/// @param radiusY half the window height.
/// @return true for the four square windows with a comparator network.
[[nodiscard]] constexpr bool hasMedianNetwork(std::uint32_t radiusX, std::uint32_t radiusY) noexcept
{
    return radiusX == radiusY && radiusX >= 1u && radiusX <= 4u;
}

/// What a caller asks an order filter for.
struct OrderFilterRequest
{
    /// Half the window width. The window is `2 * radiusX + 1` samples across.
    std::uint32_t radiusX = 1u;

    /// Half the window height. The window is `2 * radiusY + 1` samples down.
    std::uint32_t radiusY = 1u;

    /// Which rank of the window to produce, as a fraction. One half is the median.
    ///
    /// Must be within `[0, 1]`. Zero is the window's smallest sample, which makes this an erosion,
    /// and one is its largest, which makes it a dilation.
    double quantile = 0.5;

    /// Which rule resolves a quantile falling between two ranks of the window.
    QuantileInterpolation interpolation = QuantileInterpolation::Linear;

    /// How a window sample outside the image is supplied.
    ///
    /// The default reflects about the edge sample rather than repeating it, for the reason a blur
    /// does: repeating the edge sample gives it the weight of a whole half-window in the rank.
    /// `KernelBorderMode::NotApplicable` instead omits every outside sample: the truncated-window
    /// convention.
    KernelBorderMode border = KernelBorderMode::ReflectExcludingEdge;

    /// The window sample used outside the image under `KernelBorderMode::Constant`, in the image's
    /// own units. Must be finite.
    float constantBorder = 0.0f;

    /// How much of the filtered value replaces the source sample.
    ///
    /// One writes the rank; zero writes the source sample back; a value between them writes
    /// `amount * filtered + (1 - amount) * source`, which is how a caller applies a median softly
    /// enough to leave the noise floor alone. Must be finite and within `[0, 1]`.
    double amount = 1.0;

    /// How many times the filter is applied, each pass reading the one before it.
    ///
    /// Must be at least one and at most `kMaxOrderFilterIterations`. The blend is applied on every
    /// pass, so two passes at half strength are not one pass at full strength.
    ///
    /// The validity rule is applied to each pass's own input rather than to the source alone, which
    /// is the only reading that is defined: a value rule describes samples, and after one pass the
    /// samples are different ones. A centre a pass rejects carries that pass's input value into the
    /// pass that follows, and the destination is written where the last pass accepted.
    std::uint32_t iterations = 1u;

    /// Which implementation resolves the rank.
    OrderFilterMethod method = OrderFilterMethod::Automatic;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// What one order filter did, and which implementation did it.
struct OrderFilterResult
{
    /// The counts and the plan every neighbourhood filter reports.
    SpatialFilterResult filter{};

    /// Which implementation the call resolved to. `Automatic` is reported only when a truncated
    /// border used a network in the interior and the general rank at the edge.
    OrderFilterMethod method = OrderFilterMethod::SelectionKernel;

    /// How many samples the window holds, never above `kMaxOrderWindowSamples`.
    std::uint32_t windowSamples = 0u;

    /// How many passes ran, which is the request's iteration count.
    std::uint32_t iterations = 0u;

    /// How many min/max comparison primitives the comparator network applies per network output, or
    /// zero when none ran.
    ///
    /// The number the network's disposition turns on: it is what the branch-free path costs per
    /// pixel, against a selection whose cost is a partition of the same window.
    std::uint32_t networkComparators = 0u;

    /// How many outputs across every pass the comparator network produced.
    std::uint64_t networkOutputs = 0u;

    /// How many outputs across every pass the non-network rank path produced.
    ///
    /// A forced method makes one of the two output counts zero. `Automatic` with a truncated border
    /// reports network outputs for the complete-window interior and selection outputs for the edge,
    /// whose window population changes with its distance from the image boundary.
    std::uint64_t selectionOutputs = 0u;
};

/// Replaces every sample of every channel by a rank of the window around it.
///
/// The untyped entry point, and the one the exported library carries. The arrangement, working
/// planes, precision, border and write-back rules are the family's, stated in
/// `Algorithms/Spatial/SpatialFilter.h`.
///
/// A window sample the validity rule rejects is left out of the window rather than replaced, so a
/// window of nine holding two rejected samples is ranked as a window of seven. A centre the rule
/// rejects leaves its destination sample untouched, which also means it is never a member of a
/// window it would have been the centre of. A window that is entirely rejected cannot happen for a
/// centre that was accepted, because the centre is always in its own window.
///
/// Working memory is one window buffer per chunk of rows plus the working planes, taken once from
/// the engine context, so nothing here allocates per output pixel. An iteration count above one
/// adds one working plane - two when the validity rule can reject a centre - and alternates the
/// passes between them and the destination, so the memory does not grow with the iteration count.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at row-chunk
/// boundaries, and is not failure-atomic: a cancelled run may leave part of the destination written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param destination where the filtered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the window, rank, border, blend, iteration count and method.
/// @param selection which samples mean something.
/// @return the counts and which implementation ran; `ErrorCode::NotInitialized` when `execution`
///         holds nothing; `ErrorCode::InvalidArgument` for a null buffer, a representation this
///         build does not know, a layout that does not describe it, an unknown interpolation,
///         method or border, a non-finite border value or blend, a blend outside `[0, 1]`, or a
///         write-back policy naming `DomainMapping::FullScale`;
///         `ErrorCode::IncompatibleArguments` when the two images are not the same picture, when
///         they overlap, or when the promise cannot be kept in Float32; `ErrorCode::OutOfRange` for
///         a quantile outside `[0, 1]`, a window above `kMaxOrderWindowSamples`, a radius above
///         `kMaxStencilRadius`, or an iteration count of zero or above
///         `kMaxOrderFilterIterations`; `ErrorCode::Unsupported` when
///         `OrderFilterMethod::SortingNetwork` was asked for and the request does not admit a
///         comparator network, a `KernelBorderMode::NotApplicable` border included;
///         `ErrorCode::InvalidData` for a selected non-finite sample; or a memory-budget, mask or
///         cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OrderFilterResult>
filterImageOrderStatistic(const ExecutionContext& execution, SampleType sampleType, const void* source,
                          const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                          const OrderFilterRequest& request, const SpatialSelection& selection = {});

/// Replaces every sample of a checked typed image view by a rank of the window around it.
///
/// The typed spelling of `filterImageOrderStatistic`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param request the window, rank, border, blend, iteration count and method.
/// @param selection which samples mean something.
/// @return whatever `filterImageOrderStatistic` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<OrderFilterResult>
filterOrderStatistic(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                     const ImageView<SampleT>& destination, const OrderFilterRequest& request,
                     const SpatialSelection& selection = {})
{
    return filterImageOrderStatistic(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                     destination.data(), destination.layout(), request, selection);
}

/// What a caller asks a structuring-element order filter for.
///
/// An order filter whose window is the cells a structuring element marks rather than the whole
/// rectangle: the rank a morphological selection takes. At a quantile of zero it is an erosion by the
/// element, at one a dilation and at one half the element's median. Every rule but the window's
/// membership is `OrderFilterRequest`'s, and there is no method to choose: the comparator
/// networks serve whole squares only, so every window is ranked by the general selection.
struct FootprintOrderFilterRequest
{
    /// Half the element's width. The element is `2 * radiusX + 1` cells across.
    std::uint32_t radiusX = 1u;

    /// Half the element's height. The element is `2 * radiusY + 1` cells down.
    std::uint32_t radiusY = 1u;

    /// The element: one byte per cell in row-major order, its top row first, a nonzero byte marking a
    /// member of the window.
    ///
    /// Holds exactly `(2 * radiusX + 1) * (2 * radiusY + 1)` bytes and marks at least one. Borrowed for
    /// the call. The element need not mark its center, so the center need not be a member of its own
    /// window.
    std::span<const std::uint8_t> footprint{};

    /// Which rank of the window to produce, as a fraction. Must be within `[0, 1]`.
    double quantile = 0.5;

    /// Which rule resolves a quantile falling between two ranks of the window.
    QuantileInterpolation interpolation = QuantileInterpolation::Linear;

    /// How a window sample outside the image is supplied, as `OrderFilterRequest::border` states it.
    ///
    /// `KernelBorderMode::NotApplicable` omits it, which is the neutral border a morphological erosion
    /// and dilation take: the rank is of the element's cells that lie inside the image.
    KernelBorderMode border = KernelBorderMode::ReflectExcludingEdge;

    /// The window sample used outside the image under `KernelBorderMode::Constant`, in the image's
    /// own units. Must be finite.
    float constantBorder = 0.0f;

    /// How much of the filtered value replaces the source sample, as `OrderFilterRequest::amount`
    /// states it. Must be finite and within `[0, 1]`.
    double amount = 1.0;

    /// How many times the filter is applied, each pass reading the one before it. Must be at least one
    /// and at most `kMaxOrderFilterIterations`.
    std::uint32_t iterations = 1u;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// Replaces every sample of every channel by a rank of the samples a structuring element covers
/// around it.
///
/// `filterImageOrderStatistic` over the element's cells: the arrangement, working planes, precision,
/// border, validity, blend, iteration and write-back rules are that function's, and the general
/// selection ranks every window. A window that holds no sample - every marked cell outside the image
/// under `KernelBorderMode::NotApplicable`, or rejected by the validity rule, which only an element
/// that leaves its center out allows - is treated as a rejected center is: its value is carried from
/// one pass to the next and its destination sample is left untouched.
///
/// Thread-safe with respect to `execution`, the source, the element and the selection, all of which
/// are only read. Runs on the threads the operation was admitted, checks cancellation at row-chunk
/// boundaries, and is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param destination where the filtered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the element, rank, border, blend and iteration count.
/// @param selection which samples mean something.
/// @return the counts, with `OrderFilterMethod::SelectionKernel` and the element's member count as
///         the window; `ErrorCode::IncompatibleArguments` for an element whose length is not its
///         window's; `ErrorCode::InvalidArgument` for an element that marks no cell; or every other
///         failure `filterImageOrderStatistic` documents but the method's.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OrderFilterResult>
filterImageFootprintOrderStatistic(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                   const ImageLayout& sourceLayout, void* destination,
                                   const ImageLayout& destinationLayout, const FootprintOrderFilterRequest& request,
                                   const SpatialSelection& selection = {});

/// Replaces every sample of a checked typed image view by a rank of the samples a structuring
/// element covers around it.
///
/// The typed spelling of `filterImageFootprintOrderStatistic`, which owns every check and every
/// computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param request the element, rank, border, blend and iteration count.
/// @param selection which samples mean something.
/// @return whatever `filterImageFootprintOrderStatistic` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<OrderFilterResult>
filterFootprintOrderStatistic(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                              const ImageView<SampleT>& destination, const FootprintOrderFilterRequest& request,
                              const SpatialSelection& selection = {})
{
    return filterImageFootprintOrderStatistic(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                              destination.data(), destination.layout(), request, selection);
}

} // namespace an
