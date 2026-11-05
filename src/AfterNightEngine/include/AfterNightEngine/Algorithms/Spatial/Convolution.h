// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Convolving an image with a kernel, as one dense pass or as two separable
/// ones.
///
/// `Algorithms/Kernel/StencilKernel.h` convolves a Float32 plane; this convolves an image. What it
/// adds is everything the plane does not know about: which of the eight resident representations the
/// samples are in, how the channels are arranged, which samples mean something, what the output
/// promises, and how a computed value becomes a stored one. `Algorithms/Spatial/SpatialFilter.h`
/// states the parts of that every neighbourhood filter shares.
///
/// **Separable and dense are two entry points, not a mode.** Which one a caller reaches is a
/// property of the kernel rather than a preference, and a kernel that factors costs `w + h`
/// multiply-accumulates per output where the dense form costs `w * h`. The two are separate calls so
/// that a caller cannot ask for the cheap one and silently get the expensive one; a caller holding a
/// dense kernel and wanting to know which it has asks `separateConvolutionKernel`, which either
/// produces the two tap arrays or says by how much the kernel misses being a product.
///
/// **The taps are the caller's memory.** A request borrows its coefficients for the call, exactly as
/// `SeparableStencilRequest` does, so there is no plan object that owns two hundred and fifty-seven
/// doubles and no allocation between building a filter and running it. A caller that keeps a filter
/// keeps the array it built.
///
/// **Nothing here normalizes a kernel.** A tap array that sums to something other than one changes
/// the image's brightness, and that is a legitimate filter - a gradient sums to zero, an unsharp
/// mask sums to more than one. The coefficients are applied as given, and scaling is left to whoever
/// built them. `convolutionTapSum` is there so a caller can check its own kernel rather than
/// discover the answer in a picture.

#pragma once

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

/// One convolution expressed as a horizontal pass of taps and a vertical pass of taps.
///
/// Both arrays are borrowed for the call and must have an odd length, so each names a centre and an
/// equal number of samples either side of it. A one-tap array is the identity along that axis and
/// is how a caller asks for a filter along one axis only.
struct SeparableConvolutionRequest
{
    /// Taps applied along a row, from the leftmost neighbour to the rightmost.
    std::span<const double> horizontalTaps;

    /// Taps applied down a column, from the topmost neighbour to the bottommost.
    std::span<const double> verticalTaps;

    /// How a neighbour outside the image and a neighbour the validity rule rejected are supplied.
    StencilBorderPolicy borders{};

    /// How many samples apart two consecutive taps of either pass sit.
    ///
    /// One is an ordinary convolution and is the default. A larger spacing applies the same taps
    /// over a footprint that many times wider without a single extra multiply, which is the
    /// a-trous form the multiscale transforms are built out of; a caller reaching for it directly
    /// is asking for one scale of such a transform. It costs no more working memory than the
    /// ordinary pass, because the tile is walked one residue class of rows at a time.
    ///
    /// Zero is refused rather than read as one, and a spacing above `kMaxStencilDilation` is
    /// refused by name.
    std::uint32_t dilation = 1u;

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// One convolution expressed as a two-dimensional kernel that does not factor.
struct DenseConvolutionRequest
{
    /// Row-major coefficients, `width * height` of them, borrowed for the call.
    std::span<const double> coefficients;

    /// How many coefficients one row of the kernel holds. Must be odd.
    std::uint32_t width = 1u;

    /// How many rows the kernel holds. Must be odd.
    std::uint32_t height = 1u;

    /// How a neighbour outside the image and a neighbour the validity rule rejected are supplied.
    StencilBorderPolicy borders{};

    /// What the result promises, and how a computed value becomes a stored sample.
    SpatialOutputPolicy output{};
};

/// Whether a dense kernel is the outer product of two one-dimensional passes, and how nearly.
///
/// The result of asking a kernel a question about itself rather than of running it. A kernel of
/// rank one is exactly a product and the two arrays reproduce it; a kernel of higher rank is not,
/// and `residual` says how far the best rank-one approximation of it lies from the kernel that was
/// offered, so a caller can decide whether the difference matters for its picture rather than being
/// told only yes or no.
struct ConvolutionSeparation
{
    /// Whether the residual is within the stated tolerance, and the two tap arrays therefore
    /// reproduce the kernel.
    bool separable = false;

    /// How many taps the horizontal pass has, which is the kernel's width.
    std::uint32_t horizontalTapCount = 0u;

    /// How many taps the vertical pass has, which is the kernel's height.
    std::uint32_t verticalTapCount = 0u;

    /// The largest absolute difference between a kernel coefficient and the product of the two
    /// taps that would stand in for it.
    double residual = 0.0;

    /// The sum of the kernel's coefficients, which is what the filter does to a flat surface.
    double coefficientSum = 0.0;
};

/// The sum of a tap array, which is what the pass does to a flat surface.
///
/// Accumulated in Float64 in the array's own order, so it is the same number the kernel's own
/// accumulator would reach over a constant row.
///
/// Thread-safe and allocation-free.
///
/// @param taps the coefficients to sum. An empty array sums to zero.
/// @return the sum.
[[nodiscard]] AFTERNIGHTENGINE_API double convolutionTapSum(std::span<const double> taps) noexcept;

/// Factors a dense kernel into a horizontal and a vertical pass, when it factors.
///
/// The rank-one test, evaluated rather than assumed: the largest-magnitude coefficient fixes a row
/// and a column of the kernel, their outer product scaled by that coefficient is the best rank-one
/// approximation, and the largest absolute difference from the kernel is the residual. A kernel
/// whose coefficients are all zero separates trivially into two zero passes.
///
/// The two arrays are written whether or not the residual is within tolerance, so a caller may
/// inspect the approximation it was offered. Only `separable` says whether running them reproduces
/// the kernel.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution, because it is a
/// question about numbers.
///
/// @param coefficients the row-major kernel, exactly `width * height` of them.
/// @param width how many coefficients one kernel row holds. Must be odd and non-zero.
/// @param height how many rows the kernel holds. Must be odd and non-zero.
/// @param tolerance the largest residual that still counts as separable. Must be finite and not
///        negative.
/// @param horizontalTaps where the horizontal pass goes, exactly `width` long.
/// @param verticalTaps where the vertical pass goes, exactly `height` long.
/// @return the verdict and the residual; `ErrorCode::InvalidArgument` for an even or zero extent, a
///         non-finite coefficient or tolerance, or a negative tolerance;
///         `ErrorCode::IncompatibleArguments` when the coefficient count is not the extents' product
///         or an output array is not as long as its extent; or `ErrorCode::OutOfRange` for more than
///         `kMaxStencilKernelSamples` coefficients or an extent wider than `kMaxStencilTaps`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ConvolutionSeparation>
separateConvolutionKernel(std::span<const double> coefficients, std::uint32_t width, std::uint32_t height,
                          double tolerance, std::span<double> horizontalTaps, std::span<double> verticalTaps);

/// Convolves every channel of an image with a separable kernel.
///
/// The untyped entry point, and the one the exported library carries. The source may hold its
/// channels any way a layout can describe - one plane per channel, one plane holding every channel,
/// or a mixture - and each channel is addressed through that arrangement rather than through an
/// assumption about it. The destination holds the same representation, the same extent and the same
/// channel count; its strides and its arrangement are its own.
///
/// A Float32 channel that is a plane of its own is read and written where it lies. Every other
/// combination is gathered into a Float32 working plane taken from the engine context's memory
/// resource and scattered back afterwards, which `SpatialFilterResult` reports both as a byte count
/// and as two flags. The working memory is two planes at most, whatever the channel count, because
/// the channels are filtered one after another; a budget that cannot hold them is a returned refusal
/// carrying the requested, available and limit byte counts.
///
/// Both passes accumulate in Float64 tap by tap in the array's own order, and the result narrows
/// once to Float32 before the write back, so the answer does not depend on how many threads ran it.
///
/// Thread-safe with respect to `execution`, the source and the selection, all of which are only
/// read. Runs on the threads the operation was admitted, checks cancellation at row and tile
/// boundaries, and is not failure-atomic: a cancelled run may leave part of the destination written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param destination where the filtered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the two tap arrays, the tap spacing, the border policy and the output policy.
/// @param selection which samples mean something.
/// @return the counts and the plan; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, an empty or even-length tap array, a non-finite
///         tap, a zero tap spacing, an unknown or inapplicable border, a non-finite border constant
///         or substitute, a selection whose mask does not match its rule, an output contract or
///         conversion policy field outside its enumeration, or a write-back policy naming
///         `DomainMapping::FullScale`;
///         `ErrorCode::IncompatibleArguments` when the two images are not the same picture, when
///         they overlap, or when the promise cannot be kept in Float32; `ErrorCode::OutOfRange` for
///         a radius above `kMaxStencilRadius` or a spacing above `kMaxStencilDilation`;
///         `ErrorCode::LimitExceeded` when one tile band exceeds `kMaxStencilScratchBytes`; or a
///         memory-budget, mask or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
convolveImageSeparable(const ExecutionContext& execution, SampleType sampleType, const void* source,
                       const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                       const SeparableConvolutionRequest& request, const SpatialSelection& selection = {});

/// Convolves every channel of an image with a dense two-dimensional kernel.
///
/// The path for a kernel that does not factor. It needs no intermediate band and therefore no
/// kernel scratch: each output sample accumulates its whole neighbourhood in Float64 in row-major
/// coefficient order and narrows once. Everything else - the arrangement, the working planes, the
/// border and validity rules, the write back and the failures - is
/// `convolveImageSeparable`'s.
///
/// A separable kernel handed to this entry point produces the same picture the two passes do, to
/// within Float64 rounding, which is what makes the two paths checkable against one another rather
/// than only against a scalar oracle.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType what the samples are, on both sides.
/// @param source where the source image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param destination where the filtered samples go. Must not be null, and must not overlap the
///        source.
/// @param destinationLayout where they sit. Its extent must be the source's.
/// @param request the coefficients, their odd extents, the border policy and the output policy.
/// @param selection which samples mean something.
/// @return the counts and the plan; the failures `convolveImageSeparable` documents, and
///         `ErrorCode::IncompatibleArguments` when the coefficient count is not the extents'
///         product, and `ErrorCode::OutOfRange` for more than `kMaxStencilKernelSamples`
///         coefficients.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpatialFilterResult>
convolveImageDense(const ExecutionContext& execution, SampleType sampleType, const void* source,
                   const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout,
                   const DenseConvolutionRequest& request, const SpatialSelection& selection = {});

/// Convolves a checked typed image view with a separable kernel.
///
/// The typed spelling of `convolveImageSeparable`. It adds no numeric, border or validity rule: the
/// representation, the pointers and the layouts are read from the views and the untyped entry point
/// owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param request the two tap arrays, the tap spacing, the border policy and the output policy.
/// @param selection which samples mean something.
/// @return whatever `convolveImageSeparable` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
convolveSeparable(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                  const ImageView<SampleT>& destination, const SeparableConvolutionRequest& request,
                  const SpatialSelection& selection = {})
{
    return convolveImageSeparable(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                                  destination.layout(), request, selection);
}

/// Convolves a checked typed image view with a dense kernel.
///
/// The typed spelling of `convolveImageDense`, which owns every check and every computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param destination the checked writable image view.
/// @param request the coefficients, their odd extents, the border policy and the output policy.
/// @param selection which samples mean something.
/// @return whatever `convolveImageDense` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<SpatialFilterResult>
convolveDense(const ExecutionContext& execution, const ImageView<const SampleT>& source,
              const ImageView<SampleT>& destination, const DenseConvolutionRequest& request,
              const SpatialSelection& selection = {})
{
    return convolveImageDense(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), destination.data(),
                              destination.layout(), request, selection);
}

} // namespace an
