// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Continuum-corrected narrowband enhancement of RGB channels.
///
/// The broadband frame and up to three aligned emission-line frames remain distinct inputs. For
/// each enabled target channel this operation removes the broadband contribution implied by the two
/// filter widths, boosts the continuum-corrected residual about its median, fits the residual to the
/// boosted channel when enough samples vary, then keeps the brighter answer. Intermediate planes live
/// in one context-accounted block and no image is hidden behind the call.
///
/// Per enabled channel, with `r` the narrowband width over the broadband width: the residual is
/// `(narrowband - broadband * r) / (1 - r)`; the boosted channel is
/// `broadband + (residual - median(residual)) * scale`; the fit is the least-squares line from the
/// residual to the boosted value over the pixels whose residual and boosted value are nonzero and
/// whose boosted value lies in `[fitLow, fitHigh]`, used when there are at least three such pixels
/// and their residuals' standard deviation exceeds `1e-8`; and the result is the greater of the
/// boosted value and the fitted residual, or of the boosted value and the residual itself without a
/// fit. Every stage is clamped to `[0, 1]`, or first rescaled from its measured range when
/// `rescaleNonlinear` asks. The intermediate planes hold the operation's representation, so a
/// Float32 call narrows at every stage boundary; the fit's sums are double.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What has been done to an image. Defined in `Image/ImageSemantics.h`.
class ImageSemantics;

/// Metadata produced by an image transition. Defined in `Image/ImageMetadata.h`.
struct MetadataTransition;

/// One monochrome emission-line input targeting one RGB channel.
struct NarrowbandRgbChannel
{
    /// First readable byte of the monochrome input; ignored when disabled.
    const void* samples = nullptr;

    /// One-channel geometry and strides, borrowed for the call; ignored when disabled.
    const ImageLayout* layout = nullptr;

    /// Whether this input enhances its corresponding RGB channel.
    bool enabled = false;

    /// Emission-line filter full width in nanometres, finite and positive when enabled, and far
    /// enough below the broadband width that `1 - width / broadband` exceeds `1e-6`.
    double bandwidthNanometres = 7.0;

    /// Finite nonnegative enhancement factor.
    double scale = 1.2;
};

/// Every choice for one NBRGB combination.
struct NarrowbandRgbRequest
{
    /// Broadband RGB filter width in nanometres, finite and positive.
    double broadbandBandwidthNanometres = 100.0;

    /// Narrowband inputs targeting red, green and blue respectively.
    std::array<NarrowbandRgbChannel, 3u> channels{{
        {.bandwidthNanometres = 7.0, .scale = 1.2},
        {.bandwidthNanometres = 8.5, .scale = 1.2},
        {.bandwidthNanometres = 8.5, .scale = 1.2},
    }};

    /// Inclusive lower bound on the boosted value a pixel needs to enter the linear fit, in the unit
    /// interval.
    double fitLow = 0.0;

    /// Inclusive upper bound on the boosted value a pixel may have to enter the linear fit, at most
    /// one and strictly above `fitLow`.
    double fitHigh = 0.92;

    /// Whether each nonlinear intermediate is linearly rescaled from its measured minimum and
    /// maximum before the next stage instead of only being clamped.
    bool rescaleNonlinear = false;
};

/// What one NBRGB combination did.
struct NarrowbandRgbResult
{
    /// Pixels considered and admitted, and RGB samples written or copied.
    KernelRunResult run{};

    /// Whether red, green and blue had an enabled narrowband input.
    std::array<bool, 3u> channelCombined{};

    /// Whether each channel had enough samples and variation to use the fitted mapping.
    std::array<bool, 3u> fitApplied{};

    /// Selected non-finite broadband or narrowband samples mapped to zero.
    std::uint64_t nonFiniteSamples = 0u;

    /// Workers the scheduler planned for the broadband copy pass.
    std::uint32_t plannedWorkers = 0u;

    /// Peak bytes held from the engine context: two working planes plus one snapshot plane per
    /// narrowband input at the destination and three when the broadband source is the destination.
    std::uint64_t workingBytes = 0u;
};

/// Describes an NBRGB result.
///
/// Geometry, representation, numeric domain, origin and pixel aspect are retained from the RGB
/// source. The schema remains RGB, its basis becomes `NarrowbandMapping`, and any CFA is cleared.
/// Float32 and Float64 normalized or encoded working images are accepted; integer boundary images
/// are converted explicitly before this arithmetic rather than normalized implicitly by a cast.
///
/// @param broadband the three-channel source description.
/// @return the output description; `ErrorCode::Unsupported` for a source that is not Float32 or
///         Float64, is not RGB, is still a mosaic, or has a numeric domain other than normalized
///         linear or encoded nonlinear; or `ErrorCode::IncompatibleArguments` when the resulting
///         description is inconsistent.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor>
narrowbandRgbOutputDescriptor(const ImageDescriptor& broadband);

/// Enhances selected RGB channels with continuum-corrected narrowband inputs.
///
/// All images share one Float32 or Float64 representation and extent. The destination may exactly
/// alias the broadband source; partial overlap is refused. An enabled narrowband plane may start
/// at the destination base, in which case the operation snapshots that plane before writing;
/// every other overlap is refused. A pixel is admitted only when all three broadband channels and
/// every enabled narrowband input pass the one shared validity view; the others are copied from
/// broadband unchanged, as are the channels with no enabled input. At least one input must be
/// enabled. The per-channel stages run in one order on the calling thread and only the copy and the
/// final write are scheduled, so the median and the fit are the same at every thread count.
/// Cancellation may leave the destination partially written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the Float32 or Float64 representation shared by every image.
/// @param broadband first readable byte of the RGB source.
/// @param broadbandLayout source geometry and strides.
/// @param destination first writable byte of the RGB result.
/// @param destinationLayout destination geometry and strides.
/// @param request filter widths, narrowband bindings, fit interval and rescale choice.
/// @param validity the shared value/mask rule for every input and the output.
/// @return counts, fit decisions and workspace; `ErrorCode::NotInitialized` when `execution` holds
///         nothing or has no memory resource; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped; `ErrorCode::UnsupportedBackend`
///         when the operation is constrained to a backend this build lacks;
///         `ErrorCode::InvalidArgument` for a request that fails the width, scale or fit-interval
///         rules above or enables no channel, a null or misaligned pointer, a layout whose element
///         size is not the sample type's, an enabled input without samples or a layout, a mask the
///         validity rule does not use or a missing one it needs, or a mask overlapping the
///         destination; `ErrorCode::Unsupported` for a sample type other than Float32 or Float64;
///         `ErrorCode::IncompatibleArguments` for a source or destination without three channels,
///         differing widths or heights, an input that is not monochrome, a partial overlap, or a
///         mask that does not cover the source; `ErrorCode::LimitExceeded` when the working size
///         overflows; `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the
///         working memory is refused; or `ErrorCode::EmptySelection` when an enabled channel has no
///         admitted pixel.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandRgbResult>
combineNarrowbandRgb(const ExecutionContext& execution, SampleType sampleType, const void* broadband,
                     const ImageLayout& broadbandLayout, void* destination, const ImageLayout& destinationLayout,
                     const NarrowbandRgbRequest& request, const KernelValidityView& validity = {});

/// Builds the metadata transition for a completed NBRGB combination.
///
/// The broadband frame's meaning is restated unchanged, so its transfer function, processing flags
/// and shared validity remain. Geometry and channel count do not move, so an astrometric solution
/// remains valid. Source codec cards are dropped and an earlier output intent is withdrawn. The new
/// description, whose basis is a narrowband mapping, is `narrowbandRgbOutputDescriptor`'s and
/// travels beside this transition.
///
/// @param source meaning of the broadband RGB frame.
/// @param request the validated request, whose enabled channels are recorded in the lineage.
/// @return the transition; or `ErrorCode::InvalidArgument` for a request that fails the width,
///         scale or fit-interval rules or enables no channel, or a transition `operationTransition`
///         refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
narrowbandRgbTransition(const ImageSemantics& source, const NarrowbandRgbRequest& request);

/// What NBRGB combination does to its destination image.
inline constexpr OperationEffect kNarrowbandRgbEffect{.samples = SampleEffect::Writes,
                                                      .description = DescriptionEffect::Restates,
                                                      .semantics = SemanticEffect::Restates,
                                                      .solution = SolutionEffect::Preserves,
                                                      .codecMetadata = CodecMetadataEffect::Drops,
                                                      .outputIntent = OutputIntentEffect::Withdraws,
                                                      .validity = {.policy = ValidityPolicy::Preserve}};

} // namespace an
