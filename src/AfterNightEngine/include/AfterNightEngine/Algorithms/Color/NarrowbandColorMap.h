/// @file
/// Layered narrowband colour mapping in one scheduled image operation.
///
/// A layer's histogram transfer, optional modulation, HSV colourization and N-layer screen blend are
/// pointwise and therefore evaluated together. Background measurement and the optional output
/// midtones transfer remain explicit frame passes because each needs a rank from the completed
/// composition. The operation borrows every image and allocates only bounded, context-accounted rank
/// workspace and, when required for in-place post-processing, original admission.
///
/// A caller declares the layers, asks `narrowbandColorMapChannelCount` or
/// `narrowbandColorMapOutputDescriptor` what the result will be, allocates it, runs
/// `mapNarrowbandColor`, and records the change with `narrowbandColorMapTransition`. There is no
/// engine-owned intermediate: a caller that wants one colourized layer on its own maps a request that
/// includes only that layer.
///
/// **Derived from Cosmic Photons' NBColorMapper.** The layer mapping is a derived work of
/// NBColorMapper 2.2, a PixInsight script by Mike Cranfield of Cosmic Photons
/// (https://cosmicphotons.com/scripts/):
/// Copyright (C) 2023, 2024 Mike Cranfield, distributed under the GNU General Public License,
/// version 3 (GPLv3). The layer model is the script's - each layer's black point and midtones
/// transfer, the four modification modes with their inversion and median offset, HSV
/// colourization, the screen blend of every layer, the background neutralization's sigma window
/// and the output stretch - with its formulas and constants. What is this engine's is the fused row
/// pass, the bounded rank memory, the borrowed layers and the validity rule; no PixInsight, PJSR or
/// Pleiades code, interface or asset is carried. This header and its implementation are distributed
/// under the same licence, and the credit for the method is its author's;
/// `docs/dev/NBCOLORMAPPER_PROVENANCE.md` records the snapshot that was reviewed.

// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorChannels.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What has been done to an image. Defined in `Image/ImageSemantics.h`.
class ImageSemantics;

/// Metadata produced by an image transition. Defined in `Image/ImageMetadata.h`.
struct MetadataTransition;

/// Maximum layer count accepted by one map, bounding validation and per-layer scalar state.
inline constexpr std::uint32_t kMaximumNarrowbandColorLayers = 64u;

/// How a monochrome layer is modulated by another layer before colourization.
enum class NarrowbandModificationMode : std::uint8_t
{
    /// Do not read a modifier.
    None = 0u,

    /// Add `factor * modifier`.
    Add,

    /// Multiply by `factor * modifier`.
    Multiply,

    /// Multiply by `1 + factor * modifier`.
    ///
    /// Deliberately the last enumerator, so membership needs no table.
    Boost
};

/// Whether a value names a modification mode this build implements.
///
/// @param mode the value to inspect.
/// @return true for every declared mode.
[[nodiscard]] constexpr bool isNarrowbandModificationMode(NarrowbandModificationMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(NarrowbandModificationMode::Boost);
}

/// The mode's stable name.
///
/// @param mode the value to name.
/// @return its enumerator spelling, or `"UnknownNarrowbandModificationMode"` for another value.
[[nodiscard]] AFTERNIGHTENGINE_API const char* narrowbandModificationModeName(NarrowbandModificationMode mode) noexcept;

/// One borrowed monochrome or RGB input layer and its pointwise choices.
struct NarrowbandColorLayer
{
    /// First readable byte of the layer.
    const void* samples = nullptr;

    /// One- or three-channel geometry and strides, borrowed for the call.
    const ImageLayout* layout = nullptr;

    /// Whether this layer participates in the screen blend.
    bool included = true;

    /// HSV hue in degrees for a monochrome layer. Any finite value is wrapped modulo 360.
    double hueDegrees = 0.0;

    /// HSV saturation for a monochrome layer. Finite values are clamped to the unit interval.
    double saturation = 0.5;

    /// Midtones balance applied before modification. Finite values are clamped away from zero and
    /// one by a small epsilon.
    double midtones = 0.5;

    /// Input mapped to black before modification. Finite values are clamped to the unit interval.
    double blackPoint = 0.0;

    /// Modification applied to this layer. Read only for a monochrome layer that is not itself a
    /// modifier; on an RGB layer the choice is validated and ignored.
    NarrowbandModificationMode modification = NarrowbandModificationMode::None;

    /// Zero-based source layer used as the modifier. Required outside `None`, absent in `None`, and
    /// never the layer itself.
    std::optional<std::uint32_t> modificationSource{};

    /// Finite multiplier applied to the modifier.
    double modificationFactor = 1.0;

    /// Whether the modifier becomes `1 - value` before its factor is applied.
    bool invertModification = false;

    /// Whether the modifier's exact median, taken after any inversion over the modifier pixels the
    /// validity view admits, is subtracted and negative results are held at zero.
    bool subtractModificationMedian = false;
};

/// An optional rectangular background-measurement region.
struct NarrowbandColorRegion
{
    /// First included column.
    std::uint32_t x = 0u;

    /// First included row.
    std::uint32_t y = 0u;

    /// Number of columns; must be positive and remain inside the image.
    std::uint32_t width = 0u;

    /// Number of rows; must be positive and remain inside the image.
    std::uint32_t height = 0u;
};

/// Choices for background neutralization of an RGB composition.
struct NarrowbandBackgroundRequest
{
    /// Whether a background is measured and subtracted.
    bool enabled = false;

    /// Whether the high end of the shared RGB rescale uses the measured channel maxima rather than
    /// the nominal full-scale endpoint.
    bool rescaleHighlights = false;

    /// Finite lower robust-sigma bound, conventionally -2.8.
    double lowerSigmas = -2.8;

    /// Finite upper robust-sigma bound, strictly above `lowerSigmas`, conventionally 2.0.
    double upperSigmas = 2.0;

    /// Region to measure, or absent for the complete output.
    std::optional<NarrowbandColorRegion> region{};
};

/// Frame-level choices made after the fused layer pass.
struct NarrowbandColorMapRequest
{
    /// Optional background measurement, subtraction and shared rescale.
    NarrowbandBackgroundRequest background{};

    /// Whether to move the median of the output's luminance - the equal-weight mean of its channels -
    /// to `targetBackground` with one midtones transfer applied alike to every channel.
    bool applyOutputStretch = false;

    /// Desired luminance median for the optional output transfer, strictly between zero and one.
    /// Checked on every call, whether or not the stretch is requested.
    double targetBackground = 0.25;
};

/// What one layered colour map did.
struct NarrowbandColorMapResult
{
    /// Pixels considered and admitted, and destination samples written by the composition pass.
    KernelRunResult run{};

    /// Included layers, whether colour-producing or neutral.
    std::uint32_t includedLayers = 0u;

    /// Layers referenced as modifiers. Such a layer is not recursively modified or colourized.
    std::uint32_t modifierLayers = 0u;

    /// Non-finite source samples the composition pass read, each read as zero and therefore
    /// transparent to the screen blend. A sample read twice - as a layer and as another layer's
    /// modifier - is counted twice.
    std::uint64_t nonFiniteSamples = 0u;

    /// Per-channel background values that were subtracted. Zero for a skipped channel or disabled
    /// background processing.
    ColorChannelValues backgroundLevels{};

    /// Per-channel multipliers used by the background correction. Unity for a skipped channel or
    /// disabled background processing.
    ColorChannelGains backgroundGains{1.0, 1.0, 1.0};

    /// Per-channel addends used by the background correction. Zero for a skipped channel or
    /// disabled background processing.
    ColorChannelOffsets backgroundOffsets{};

    /// Whether the background pass used one shared rescale interval.
    bool backgroundRescaled = false;

    /// RGB channels with no admitted finite sample in the region, or whose sigma window retained
    /// none, and which were therefore left unchanged.
    std::uint32_t backgroundChannelsSkipped = 0u;

    /// Whether the requested output stretch had a usable interior median and was applied.
    bool outputStretchApplied = false;

    /// Workers the scheduler planned for the composition pass.
    std::uint32_t plannedWorkers = 0u;

    /// Peak bytes held from the engine context for rank workspace and retained input admission.
    std::uint64_t workingBytes = 0u;
};

/// Determines whether a map produces a monochrome or RGB destination.
///
/// A map is RGB when an included layer is already RGB or an included monochrome layer that is not
/// referenced as a modifier has nonzero saturation. Otherwise all included layers remain neutral
/// and the one-channel screen result is retained.
///
/// @param layers one to `kMaximumNarrowbandColorLayers` layer declarations.
/// @return one or three; `ErrorCode::InvalidArgument` for no layer or more than the maximum, a layer
///         without samples or a layout, a parameter that is not finite, a mode outside the
///         enumeration, a modifier named without a mode or a mode without a modifier, a modifier
///         that is the layer itself or out of range, or no included layer; or
///         `ErrorCode::IncompatibleArguments` for a layer that is neither one- nor three-channel, or
///         layers whose widths and heights differ.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t>
narrowbandColorMapChannelCount(std::span<const NarrowbandColorLayer> layers);

/// Describes a layered narrowband colour-map result.
///
/// Geometry, representation, domain, origin and pixel aspect come from `reference`. The schema is
/// Mono/Unspecified for a neutral map and RGB/NarrowbandMapping for a colour map; any CFA is cleared.
///
/// @param reference the first layer's description.
/// @param layers the exact layer declarations that will be executed.
/// @return the checked output description; the layer validation failures of
///         `narrowbandColorMapChannelCount`; `ErrorCode::Unsupported` for a reference that is not
///         Float32 or Float64, is still a mosaic, is neither mono nor RGB, has a numeric domain other
///         than normalized linear or encoded nonlinear, or differs from the layers in width or
///         height; `ErrorCode::LimitExceeded` for an output extent beyond the engine limits; or
///         `ErrorCode::IncompatibleArguments` when the resulting description is inconsistent.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor>
narrowbandColorMapOutputDescriptor(const ImageDescriptor& reference, std::span<const NarrowbandColorLayer> layers);

/// Applies every layer transform, modification, colourization and screen blend.
///
/// All layers and the destination share one Float32 or Float64 representation and extent; layouts
/// may differ and each layer may be monochrome or RGB. A pixel is admitted only when every channel of
/// every included layer, and of the modifier of each modified monochrome layer, passes the shared
/// validity view; a rejected pixel is left untouched by every pass. Every layer value is read through
/// the layer's black point and midtones transfer and clamped to `[0, 1]`, a non-finite sample being
/// read as zero. A modifier is read the same way through its own layer's transfer, using its only
/// channel or, for an RGB modifier, the Rec. 601 luma `0.2989 R + 0.5870 G + 0.1140 B`. A modified
/// value is clamped to `[0, 1]` before colourization. Exact source/destination aliasing is supported
/// by evaluating one pixel before writing it. For aliased post-processing with a validity rule,
/// one context-owned byte per pixel preserves admission from the original inputs. Every partial
/// overlap and every mask/destination overlap is refused. Background neutralization uses exact median and normalized
/// MAD ranks and the script's clamped sigma window, then one shared rescale when a corrected channel would
/// leave the unit interval. Cancellation may leave earlier scheduled rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the Float32 or Float64 representation shared by all images.
/// @param layers one to `kMaximumNarrowbandColorLayers` inputs and their choices.
/// @param destination first writable byte of the planned one- or three-channel result.
/// @param destinationLayout destination geometry and strides.
/// @param request optional background and output-transfer choices.
/// @param validity the shared value/mask rule for all inputs and the output.
/// @return counts and measured values; the layer validation failures of
///         `narrowbandColorMapChannelCount`; `ErrorCode::NotInitialized` when `execution` holds
///         nothing or has no memory resource; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped; `ErrorCode::UnsupportedBackend`
///         when the operation is constrained to a backend this build lacks;
///         `ErrorCode::Unsupported` for a sample type other than Float32 or Float64;
///         `ErrorCode::InvalidArgument` for a null or misaligned pointer, a layout whose element size
///         is not the sample type's, a mask the validity rule does not use or a missing one it needs,
///         a mask overlapping the destination, or sigma bounds that are not finite and ordered;
///         `ErrorCode::IncompatibleArguments` for a destination whose extent or channel count is not
///         the planned one, a partial overlap, or a mask that does not cover the layers;
///         `ErrorCode::OutOfRange` for a target background outside `(0, 1)` or an empty or
///         out-of-bounds region; `ErrorCode::LimitExceeded` when the workspace size overflows;
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when it is refused; or
///         `ErrorCode::EmptySelection` when a modifier median or the output stretch has no admitted
///         sample.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandColorMapResult>
mapNarrowbandColor(const ExecutionContext& execution, SampleType sampleType,
                   std::span<const NarrowbandColorLayer> layers, void* destination,
                   const ImageLayout& destinationLayout, const NarrowbandColorMapRequest& request = {},
                   const KernelValidityView& validity = {});

/// Builds the metadata transition for a layered narrowband colour map.
///
/// The reference layer's meaning is restated unchanged, so its transfer function, processing flags
/// and shared validity remain. Geometry is unchanged, so an astrometric solution remains valid; codec
/// cards are dropped and prior output intent is withdrawn. The lineage entry names the layer and
/// channel counts. The new description, whose RGB basis is a narrowband mapping, is
/// `narrowbandColorMapOutputDescriptor`'s and travels beside this transition.
///
/// @param source meaning of the reference layer.
/// @param layerCount number of layer declarations used, for the lineage record; at least one.
/// @param outputChannels one or three, as reported by `narrowbandColorMapChannelCount`.
/// @return the transition; or `ErrorCode::InvalidArgument` for a layer count outside
///         `[1, kMaximumNarrowbandColorLayers]`, a channel count other than one or three, or a
///         transition `operationTransition` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
narrowbandColorMapTransition(const ImageSemantics& source, std::uint32_t layerCount, std::uint32_t outputChannels);

/// What layered narrowband mapping does to its destination image.
inline constexpr OperationEffect kNarrowbandColorMapEffect{.samples = SampleEffect::Writes,
                                                           .description = DescriptionEffect::Restates,
                                                           .semantics = SemanticEffect::Restates,
                                                           .solution = SolutionEffect::Preserves,
                                                           .codecMetadata = CodecMetadataEffect::Drops,
                                                           .outputIntent = OutputIntentEffect::Withdraws,
                                                           .validity = {.policy = ValidityPolicy::Preserve}};

} // namespace an
