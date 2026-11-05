// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Neighbourhood filters over one Float32 plane: the three-by-three oracle and general convolution.
///
/// Three things live here. The direct three-by-three stencil is the one stencil a device backend
/// implements: `stencil3x3KernelPlan`, `stencil3x3KernelContract` and `checkStencil3x3KernelRequest`
/// state what a device agrees to, and `referenceStencil3x3Kernel` is its scalar oracle.
/// `convolveSeparablePlane` and `convolveDensePlane` are the general convolutions the engine's
/// filters are built on, run across the operation's threads with bounded scratch. A separable pass
/// takes a tap spacing so that the starlet transform's dilated layers run through the same
/// convolution, border rule and bounded scratch as every other filter.
///
/// Every filter accumulates in Float64 in the order of its coefficient array and narrows once to
/// Float32, keeps an outside neighbour (resolved by the border rule) apart from an invalid one
/// (replaced by a stated substitute), and leaves the output of an invalid centre untouched. Each
/// filter refuses with `ErrorCode::NotInitialized` an execution that holds nothing; with
/// `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` a stopped one; with
/// `ErrorCode::IncompatibleArguments` a plane with more than one sample per pixel, a destination
/// shaped unlike its source, or a packed mask that does not cover the source and its selected
/// bit-plane; and with `ErrorCode::InvalidArgument` overlapping buffers, or a packed mask supplied
/// under a rule that reads none or missing under `ValidityRule::ExplicitMask`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <array>
#include <cstdint>
#include <span>

namespace an
{

/// What a caller asks a device to prepare. Defined in `Algorithms/Kernel/DeviceBackend.h`.
///
/// Named here rather than included, because the check below only borrows one.
struct DeviceKernelRequest;

/// The operation and contract revision of the direct three-by-three stencil.
inline constexpr KernelId kStencil3x3KernelId{.operation = KernelOperation::Stencil3x3, .version = 1u};

/// Samples a three-by-three stencil reads beyond each side of an output tile.
inline constexpr std::uint32_t kStencil3x3HaloRadius = 1u;

/// The stable scalar oracle named by every device implementation of the three-by-three stencil.
inline constexpr const char* kStencil3x3ReferenceScenario = "kernel.stencil.3x3.float32.v1";

/// Coefficients and edge behavior of the direct three-by-three scalar stencil.
struct Stencil3x3KernelParameters
{
    /// Row-major coefficients from top-left through bottom-right. The default is identity.
    std::array<double, 9u> coefficients{0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0};

    /// How a neighbor outside the input footprint is supplied. Must be `Clamp` or `Constant`.
    KernelBorderMode border = KernelBorderMode::Clamp;

    /// Neighbor value outside the footprint under `KernelBorderMode::Constant`.
    float constantBorder = 0.0f;

    /// Neighbor value used when the coordinate is inside but the input sample is invalid.
    float invalidNeighbor = 0.0f;
};

/// The device plan for one direct three-by-three stencil.
///
/// The operation reads and writes row-strided planar Float32 single-channel images, accumulates in
/// Float64, requires disjoint buffers, and takes its clamp or constant border from `parameters`.
/// The fixed one-sample halo is part of `kStencil3x3KernelId`; a device implementation reports the
/// temporary storage it needs for that halo through `DeviceKernel::scratchBytes`, which the device
/// allocates and supplies under `KernelScratchPolicy::DeviceAllocated`.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param parameters the coefficients and border behavior of the operation.
/// @return the widest plan the device operation defines. Invalid parameters are retained for
///         `checkStencil3x3KernelRequest` to refuse with a typed status.
[[nodiscard]] AFTERNIGHTENGINE_API KernelPlanKey
stencil3x3KernelPlan(const Stencil3x3KernelParameters& parameters) noexcept;

/// The contract a device backend advertises for the direct three-by-three stencil.
///
/// Device execution currently accepts all-valid input only because the two bindings carry no
/// validity mask. The host reference remains the oracle for every validity rule. A caller that
/// needs one of those rules selects the host through the ordinary fallback path.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @return the Float32-input, Float64-accumulating, exact contract with clamp and constant borders.
[[nodiscard]] AFTERNIGHTENGINE_API KernelContract stencil3x3KernelContract() noexcept;

/// Whether a kernel request describes one direct three-by-three stencil.
///
/// Checks the operation, argument object, finite coefficients and substitute values, the plan
/// against the operation's device contract, agreement between the plan and argument border, then
/// the two single-plane Float32 layouts. Both layouts must have one extent; a contiguous plan
/// requires both to be contiguous.
///
/// Thread-safe and reentrant. Allocates only a failure it describes.
///
/// @param request the plan, source and destination layouts, and the bytes of one
///        `Stencil3x3KernelParameters`. Borrowed for the call.
/// @return success; `ErrorCode::InvalidArgument` for another operation, arguments that are not one
///         parameter object, non-finite values, a border other than `Clamp` or `Constant`, or a
///         binding count other than two; or
///         `ErrorCode::IncompatibleArguments` when the plan disagrees with the parameters or the
///         layouts do not describe the planned image pair; or `ErrorCode::OutOfRange` when the
///         device halo scratch byte count is not representable.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkStencil3x3KernelRequest(const DeviceKernelRequest& request);

/// Applies a direct three-by-three scalar stencil.
///
/// Source and destination must be disjoint. An invalid center leaves its output untouched; an
/// invalid in-footprint neighbor contributes `parameters.invalidNeighbor`. Arithmetic follows the
/// coefficient array in row-major order using a Float64 accumulator, then narrows once to Float32.
/// Cancellation is checked before output and once per row, so a mid-run stop may leave a prefix.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane.
/// @param parameters finite coefficients and substitute values, plus a `Clamp` or `Constant` border.
/// @param validity the source validity rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::InvalidArgument` for a border other than `Clamp` or `Constant`
///         or a non-finite coefficient or substitute; or a refusal the file comment names.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
referenceStencil3x3Kernel(const ExecutionContext& execution, PlaneView<const float> source,
                          PlaneView<float> destination, const Stencil3x3KernelParameters& parameters,
                          const KernelValidityView& validity);

/// The widest half-extent one separable pass may have.
///
/// A tap array is the operation's inner loop and its halo at once: a radius decides how much of a
/// neighbouring tile every tile has to read, so an unbounded radius is an unbounded halo. The
/// ceiling is checked before a sample is read rather than implied by the size of a buffer inside an
/// implementation.
inline constexpr std::uint32_t kMaxStencilRadius = 128u;

/// The most taps one separable pass may have, which is the widest radius on both sides of a center.
inline constexpr std::uint32_t kMaxStencilTaps = 2u * kMaxStencilRadius + 1u;

/// The widest spacing a dilated separable pass may put between two consecutive taps.
///
/// A dilation spreads a fixed number of taps over a footprint `dilation` times wider without
/// costing a single extra multiply, which is what an a-trous multiscale transform is built out of.
/// It is bounded separately from the radius because the two limit different things: the radius
/// bounds the arithmetic per output sample, and the dilation bounds how far apart the samples that
/// arithmetic reads may lie. Five hundred and twelve is past the point where every source row of a
/// production frame is inside one footprint, and a spacing above it is refused before a sample is
/// read.
inline constexpr std::uint32_t kMaxStencilDilation = 512u;

/// The most coefficients one dense two-dimensional kernel may have.
///
/// A dense kernel costs its whole area per output sample, so its ceiling is an area rather than a
/// radius: four thousand and ninety-six admits a sixty-three by sixty-three neighbourhood and
/// refuses the shapes that should have been expressed separably.
inline constexpr std::uint32_t kMaxStencilKernelSamples = 4096u;

/// The context memory one separable stencil may hold for its bands, padded rows and counters.
///
/// The separable path keeps one horizontally filtered band per concurrently running tile rather
/// than one intermediate per image, and this is what bounds that. A caller reading it can tell
/// what a convolution costs before running one.
inline constexpr std::uint64_t kMaxStencilScratchBytes = 8u * 1024u * 1024u;

/// How a stencil supplies a neighbour it cannot read, whether it is outside or invalid.
///
/// The two cases are separate on purpose. A coordinate outside the footprint has no sample at all,
/// and the border rule decides what stands in for it. A coordinate inside the footprint has a
/// sample the validity rule rejected, and substituting the border value there would make an
/// interior no-data pixel behave like an edge.
struct StencilBorderPolicy
{
    /// How a coordinate outside the input footprint is resolved.
    ///
    /// `Clamp` matches the rest of this family's defaults. A filtering caller usually wants
    /// `KernelBorderMode::ReflectExcludingEdge` instead, because clamping biases a convolution near
    /// an edge toward the edge sample; the choice is stated rather than assumed.
    KernelBorderMode border = KernelBorderMode::Clamp;

    /// The neighbour value outside the footprint under `KernelBorderMode::Constant`.
    float constantBorder = 0.0f;

    /// The neighbour value used when a coordinate is inside the footprint but its sample is invalid.
    float invalidNeighbor = 0.0f;
};

/// One separable convolution: a horizontal pass of taps and a vertical pass of taps.
///
/// Both tap arrays are borrowed for the call and must have an odd length, so each names a center
/// and an equal number of samples either side of it. A one-tap array is the identity pass and is
/// how a caller asks for a filter along one axis only.
struct SeparableStencilRequest
{
    /// Taps applied along a row, from the leftmost neighbour to the rightmost.
    std::span<const double> horizontalTaps;

    /// Taps applied down a column, from the topmost neighbour to the bottommost.
    std::span<const double> verticalTaps;

    /// How neighbours outside the footprint and invalid neighbours are supplied.
    StencilBorderPolicy borders{};

    /// How many samples apart two consecutive taps of either pass sit.
    ///
    /// One is an ordinary convolution and is the default. A larger spacing applies the same taps to
    /// a footprint that many times wider - the a-trous, or "with holes", form a multiscale
    /// transform decomposes an image with - at exactly the arithmetic cost of the undilated pass.
    /// Zero is refused rather than read as one, because a caller that computed a spacing and got
    /// zero has a defect rather than an ordinary convolution.
    std::uint32_t dilation = 1u;
};

/// One dense two-dimensional convolution, for a kernel that does not factor.
struct DenseStencilRequest
{
    /// Row-major coefficients, `width * height` of them, borrowed for the call.
    std::span<const double> coefficients;

    /// How many coefficients one row of the kernel holds. Must be odd.
    std::uint32_t width = 1u;

    /// How many rows the kernel holds. Must be odd.
    std::uint32_t height = 1u;

    /// How neighbours outside the footprint and invalid neighbours are supplied.
    StencilBorderPolicy borders{};
};

/// What one stencil execution did, and the tile and halo plan it did it with.
struct StencilExecutionResult
{
    /// Pixel selection and writes. An invalid center leaves its output untouched.
    KernelRunResult run{};

    /// Half the horizontal extent actually applied, counted in taps rather than in samples.
    ///
    /// The footprint it covers is this many samples either side of the centre under an ordinary
    /// pass, and `horizontalRadius * dilation` samples either side under a dilated one.
    std::uint32_t horizontalRadius = 0u;

    /// Half the vertical extent actually applied, counted in taps rather than in rows.
    ///
    /// The halo one tile reads is `verticalRadius * dilation` rows either side of its own, which is
    /// what `haloRows` reports.
    std::uint32_t verticalRadius = 0u;

    /// How many samples apart two consecutive taps sat, which is one for an ordinary pass.
    std::uint32_t dilation = 1u;

    /// Output rows one tile produces, halo excluded. A dilated pass's tile takes them `dilation`
    /// rows apart.
    std::uint32_t tileRows = 0u;

    /// How many tiles the destination was divided into.
    std::uint64_t tileCount = 0u;

    /// How many tiles may hold an intermediate band at once, which is what bounds the scratch.
    std::uint64_t concurrentTiles = 0u;

    /// Rows of halo one tile reads above and below its own output rows.
    std::uint64_t haloRows = 0u;

    /// Context memory held for the intermediate bands. Zero for the dense path, which needs none.
    std::uint64_t scratchBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Convolves a Float32 plane with a separable kernel, one tile band at a time.
///
/// The two passes are what make this cheaper than the dense path and what make its contract worth
/// stating. A horizontal pass filters a band of source rows - the tile's own rows plus a halo of
/// `verticalTaps.size() / 2` rows above and below - into a Float64 intermediate; the vertical pass
/// then filters down the intermediate into the destination and narrows once. Both accumulate tap by
/// tap in the array's own order, in Float64, so the optimized backends produce the same arithmetic
/// as the reference rather than a reassociated one.
///
/// The intermediate is a band rather than an image: only as many bands exist at once as there are
/// tiles running, so an admitted working set is bounded by `kMaxStencilScratchBytes`.
/// `StencilExecutionResult` reports the plan it chose; a geometry whose minimum one-band plan does
/// not fit is refused for a caller to answer with an outer tiling strategy.
///
/// A dilated pass reads the same number of taps `dilation` samples apart instead of one, and it
/// costs no more arithmetic than the ordinary pass does. It also costs no more memory: a tile is
/// `tileRows` consecutive rows of one residue class of the row index modulo `dilation`, so its band
/// holds those rows plus its halo rather than every row of the `dilation` times taller span they
/// cover, and the rows a band holds are the ones the taps actually read. Every source row inside a
/// tile's footprint is filtered horizontally exactly once whatever the spacing, and because no two
/// classes share a row, a pass with its taps far apart has as many tiles for its workers as one
/// with its taps adjacent.
///
/// A source coordinate outside the footprint follows the border rule. Under
/// `KernelBorderMode::Constant` a whole row outside the footprint contributes the constant, which
/// after the horizontal pass is the constant scaled by the sum of the horizontal taps - the same
/// answer a dense convolution over an infinite constant surround gives. An invalid source sample
/// contributes `StencilBorderPolicy::invalidNeighbor` to the horizontal pass; an invalid center
/// leaves its destination sample untouched and is not counted as an output.
///
/// The selected backend is the operation's dispatch table, and the interior of each pass runs on
/// that table's compiled code while the edges, where the border rule applies, run scalar.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane, disjoint from the source.
/// @param request the taps, the tap spacing and the border policy.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts and the tile plan; `ErrorCode::InvalidArgument` for an empty or even-length
///         tap array, a non-finite tap, a zero tap spacing, an unknown border or
///         `KernelBorderMode::NotApplicable`, or a non-finite constant or substitute;
///         `ErrorCode::OutOfRange` for a radius above `kMaxStencilRadius` or a spacing above
///         `kMaxStencilDilation`; `ErrorCode::LimitExceeded` when even one tile band exceeds
///         `kMaxStencilScratchBytes`; `ErrorCode::NotInitialized`, `ErrorCode::MemoryBudgetExceeded`
///         or `ErrorCode::AllocationFailed` when the context has no memory resource or refuses the
///         bands; or a refusal the file comment names. A cancelled run may leave a prefix of the
///         destination written.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StencilExecutionResult>
convolveSeparablePlane(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                       const SeparableStencilRequest& request, const KernelValidityView& validity);

/// Convolves a Float32 plane with a dense two-dimensional kernel.
///
/// The path for a kernel that does not factor into two passes. It needs no intermediate band - its
/// only working memory is one write counter per chunk - because each output sample accumulates its
/// whole neighbourhood in Float64, in row-major coefficient order, and narrows once. Rows are
/// divided across the operation's threads; the border and validity rules are the separable path's.
///
/// A separable kernel given here produces the same result to within Float64 rounding, which is what
/// makes the two paths checkable against one another rather than only against a scalar oracle.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane, disjoint from the source.
/// @param request the coefficients, their odd extents, and the border policy.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts and the row plan; `ErrorCode::InvalidArgument` for a zero or even extent, a
///         non-finite coefficient, an unknown border or `KernelBorderMode::NotApplicable`, or a
///         non-finite constant or substitute; `ErrorCode::IncompatibleArguments` when the
///         coefficient count is not the extents' product; `ErrorCode::OutOfRange` for more than
///         `kMaxStencilKernelSamples` coefficients or a radius above `kMaxStencilRadius`;
///         `ErrorCode::NotInitialized`, `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when the context has no memory resource or refuses the
///         per-chunk write counters; or a refusal the file comment names.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StencilExecutionResult>
convolveDensePlane(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                   const DenseStencilRequest& request, const KernelValidityView& validity);

} // namespace an
