/// @file
/// Emission-line palette composition over prepared channels.
///
/// Input preparation is deliberately composition rather than another image-owning workflow. A caller
/// extracts a channel through ChannelTransfer, stretches it through Stretch and resizes it through
/// Geometry, then hands the three aligned monochrome views here. This surface owns the part that is
/// one operation: substitute the missing sulphur or hydrogen channel, apply the three intensities,
/// map a named palette and normalize the result without materializing intermediate images.
///
/// Every source sample is read into `[0, 1]` - a non-finite one as zero - before its intensity
/// multiplies it, and every mapped channel is clamped to `[0, 1]` before the optional normalization
/// divides the frame by its largest mapped value. One scheduled pass writes the mapped pixels while
/// reducing that maximum; normalization is a second pass.
///
/// **Derived from Seti Astro Suite Pro.** The palette set is a derived work of the narrowband
/// palette process of Seti Astro Suite Pro (SASPro) by Franklin Marek
/// (https://github.com/setiastro/setiastrosuitepro), distributed under the GNU General Public
/// License, version 3 (GPL-3.0). The palettes as SASPro composes them are its - their names, order
/// and equations, Realistic1, Realistic2 and Foraxx among them - and so are the substitution of a
/// missing H-alpha or SII channel, the clamp and the normalization by the frame maximum. What is
/// this engine's is the typed row passes, the reduction that finds the maximum while the pixels are
/// written and the validity rule; no Python, PyQt or SASPro asset is carried. This header and its
/// implementation are distributed under the same licence, and the credit for the set as composed
/// there is its author's;
/// `docs/dev/NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md` records the snapshot that was reviewed.

// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What has been done to an image. Defined in `Image/ImageSemantics.h`.
class ImageSemantics;

/// Metadata produced by an image transition. Defined in `Image/ImageMetadata.h`.
struct MetadataTransition;

/// The twelve stable emission-line mappings, in the palette set's own order.
enum class NarrowbandPalette : std::uint8_t
{
    /// Sulphur-II, hydrogen-alpha, oxygen-III.
    Sho = 0u,

    /// Hydrogen-alpha, oxygen-III, oxygen-III.
    Hoo,

    /// Hydrogen-alpha, sulphur-II, oxygen-III.
    Hso,

    /// Hydrogen-alpha, oxygen-III, sulphur-II.
    Hos,

    /// Oxygen-III, sulphur-II, sulphur-II.
    Oss,

    /// Oxygen-III, hydrogen-alpha, hydrogen-alpha.
    Ohh,

    /// Oxygen-III, sulphur-II, hydrogen-alpha.
    Osh,

    /// Oxygen-III, hydrogen-alpha, sulphur-II.
    Ohs,

    /// Hydrogen-alpha, sulphur-II, sulphur-II.
    Hss,

    /// First weighted natural-colour mapping: red `(Ha + SII) / 2`, green `0.3 Ha + 0.7 OIII`, blue
    /// `0.9 OIII + 0.1 Ha`.
    Realistic1,

    /// Second weighted natural-colour mapping: red `0.7 Ha + 0.3 SII`, green `0.3 SII + 0.7 OIII`,
    /// blue `OIII`.
    Realistic2,

    /// Nonlinear Foraxx mapping. With `p = Ha * OIII` and `b = p^(1 - p)`, green is
    /// `b Ha + (1 - b) OIII` and blue is `OIII`; red is `s SII + (1 - s) Ha` with `s = c^(1 - c)` for
    /// `c` the OIII value clamped to `[1e-6, 1]`, or `Ha` itself when SII is absent and substituted.
    ///
    /// Deliberately the last enumerator, so membership needs no table.
    Foraxx
};

/// Whether a value names a palette this build implements.
///
/// @param palette the value to inspect.
/// @return true for all twelve declared palettes.
[[nodiscard]] constexpr bool isNarrowbandPalette(NarrowbandPalette palette) noexcept
{
    return static_cast<std::uint8_t>(palette) <= static_cast<std::uint8_t>(NarrowbandPalette::Foraxx);
}

/// One stable palette catalogue entry.
struct NarrowbandPaletteDescriptor
{
    /// The palette selected by this row.
    NarrowbandPalette palette = NarrowbandPalette::Sho;

    /// Short display name. A string literal that outlives every caller.
    const char* name = "SHO";

    /// Human-readable mapping description. A string literal that outlives every caller.
    const char* description = "Sulfur, hydrogen-alpha, and oxygen mapped to RGB.";
};

/// The immutable palette catalogue in enumeration order.
///
/// @return twelve entries backed by process-lifetime read-only storage.
[[nodiscard]] AFTERNIGHTENGINE_API std::span<const NarrowbandPaletteDescriptor> narrowbandPalettes() noexcept;

/// A palette's short display name.
///
/// @param palette the value to name.
/// @return the catalogue name, or `"UnknownNarrowbandPalette"` for another value.
[[nodiscard]] AFTERNIGHTENGINE_API const char* narrowbandPaletteName(NarrowbandPalette palette) noexcept;

/// Converts a catalogue index to a palette.
///
/// An out-of-range index is clamped to the final entry, so a saved index always names a palette.
///
/// @param index zero-based catalogue index.
/// @return the matching palette, or `NarrowbandPalette::Foraxx` when the index is too large.
[[nodiscard]] AFTERNIGHTENGINE_API NarrowbandPalette narrowbandPaletteFromIndex(std::size_t index) noexcept;

/// Converts a palette to its catalogue index.
///
/// @param palette the palette.
/// @return its zero-based index, or the final index for an unknown value.
[[nodiscard]] AFTERNIGHTENGINE_API std::size_t narrowbandPaletteIndex(NarrowbandPalette palette) noexcept;

/// One borrowed, prepared monochrome channel.
struct NarrowbandPaletteChannel
{
    /// First readable byte, or null when this optional channel is absent.
    const void* samples = nullptr;

    /// Geometry and strides, borrowed with `samples`; null exactly when the channel is absent.
    const ImageLayout* layout = nullptr;
};

/// The aligned channels a palette reads.
struct NarrowbandPaletteChannels
{
    /// H-alpha, optional when sulphur-II is present because the latter substitutes for it.
    NarrowbandPaletteChannel hydrogenAlpha{};

    /// OIII, always required.
    NarrowbandPaletteChannel oxygenIII{};

    /// SII, optional when H-alpha is present because the latter substitutes for it.
    NarrowbandPaletteChannel sulfurII{};
};

/// Per-line multipliers applied before a palette equation, to each source sample already clamped to
/// `[0, 1]`.
struct NarrowbandPaletteIntensities
{
    /// Finite, nonnegative H-alpha multiplier.
    double hydrogenAlpha = 1.0;

    /// Finite, nonnegative OIII multiplier.
    double oxygenIII = 1.0;

    /// Finite, nonnegative SII multiplier.
    double sulfurII = 1.0;
};

/// Every choice for one palette output.
struct NarrowbandPaletteRequest
{
    /// Which channel equation to evaluate.
    NarrowbandPalette palette = NarrowbandPalette::Sho;

    /// Per-line intensity multipliers.
    NarrowbandPaletteIntensities intensities{};

    /// Whether to divide all output channels by the frame's largest mapped value when it is
    /// positive. Mapping values are clamped first.
    bool normalize = true;
};

/// One destination in a palette-grid request.
struct NarrowbandPaletteDestination
{
    /// Which palette this destination receives.
    NarrowbandPalette palette = NarrowbandPalette::Sho;

    /// First writable byte of the RGB destination.
    void* samples = nullptr;

    /// Geometry and strides of `samples`, borrowed for the call.
    const ImageLayout* layout = nullptr;
};

/// What one palette composition did.
struct NarrowbandPaletteResult
{
    /// Pixels considered and admitted by the shared validity rule, and RGB samples written.
    KernelRunResult run{};

    /// Largest mapped value before optional frame normalization.
    double maximumBeforeNormalization = 0.0;

    /// Non-finite source samples read as zero, counted once per role, so a substituted channel's are
    /// counted twice.
    std::uint64_t nonFiniteSamples = 0u;

    /// Whether SII substituted for an absent H-alpha channel.
    bool substitutedHydrogenAlpha = false;

    /// Whether H-alpha substituted for an absent SII channel.
    bool substitutedSulfurII = false;

    /// Workers the scheduled pass could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Describes the RGB result of a palette composition.
///
/// The extent, representation, numeric domain, coordinate origin and pixel aspect come from the
/// prepared reference channel. The schema becomes RGB, the basis becomes `NarrowbandMapping`, and
/// any CFA is cleared. Only Float32 and Float64 normalized or encoded inputs are accepted.
///
/// @param reference one prepared monochrome channel's description.
/// @return the output description; `ErrorCode::Unsupported` for a reference that is not Float32 or
///         Float64, is not mono, is still a mosaic, or has a numeric domain other than normalized
///         linear or encoded nonlinear; `ErrorCode::LimitExceeded` for an RGB extent beyond the
///         engine limits; or `ErrorCode::IncompatibleArguments` when the resulting description is
///         inconsistent.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor>
narrowbandPaletteOutputDescriptor(const ImageDescriptor& reference);

/// Maps three prepared monochrome channels into one RGB palette in a fused pointwise pass, followed
/// by a normalization pass when normalizing changes anything.
///
/// All present channels and the destination have the same extent and floating representation;
/// layouts may otherwise be planar, interleaved or padded. OIII and at least one of H-alpha or SII
/// are required. A missing one of that pair is substituted by the other. Selected non-finite values
/// are transparent zeroes and counted. A pixel is admitted only when all three channels, after
/// substitution, pass the one shared validity view; the others are left untouched. Every source and
/// the optional packed mask must be disjoint from the destination. Cancellation may leave earlier
/// destination rows written, or every row mapped but not yet normalized.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the Float32 or Float64 representation shared by all images.
/// @param channels prepared, aligned input channels.
/// @param destination first writable byte of a three-channel image.
/// @param destinationLayout destination geometry and strides.
/// @param request palette, intensities and normalization choice.
/// @param validity the shared value/mask rule for all inputs and the output.
/// @return counts and substitution facts; `ErrorCode::NotInitialized` when `execution` holds
///         nothing; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was
///         stopped; `ErrorCode::UnsupportedBackend` when the operation is constrained to a backend
///         this build lacks; `ErrorCode::Unsupported` for a sample type other than Float32 or
///         Float64; `ErrorCode::InvalidArgument` for a null or misaligned pointer, a layout whose
///         element size is not the sample type's, a channel with samples but no layout or the reverse,
///         an unknown palette, an intensity that is not finite and nonnegative, a mask the validity
///         rule does not use or a missing one it needs, or a mask overlapping the destination;
///         `ErrorCode::IncompatibleArguments` for a destination without three channels, no OIII or
///         neither H-alpha nor SII, a channel that is not monochrome or not the destination's width
///         and height, a channel overlapping the destination, or a mask that does not cover the
///         channels; or `ErrorCode::EmptySelection` when the validity view admits no pixel.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandPaletteResult>
composeNarrowbandPalette(const ExecutionContext& execution, SampleType sampleType,
                         const NarrowbandPaletteChannels& channels, void* destination,
                         const ImageLayout& destinationLayout, const NarrowbandPaletteRequest& request = {},
                         const KernelValidityView& validity = {});

/// Builds several palettes from the same prepared channels.
///
/// Every destination is validated before the first one is written, destinations are pairwise
/// disjoint, and each entry is evaluated with the same intensities, normalization and shared
/// validity. The destination span defines the requested order and may name a subset or repeat a
/// palette. A failure after execution starts may leave earlier destinations complete.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the Float32 or Float64 representation shared by every image.
/// @param channels prepared, aligned input channels.
/// @param destinations one or more palette/destination bindings.
/// @param intensities per-line multipliers shared by the grid.
/// @param results exactly one result slot per destination, written in destination order.
/// @param normalize whether each palette is normalized by its own maximum.
/// @param validity the shared value/mask rule.
/// @return success after every result slot is written; `ErrorCode::InvalidArgument` for no
///         destination, a result span of another length, or a destination without a layout;
///         `ErrorCode::IncompatibleArguments` when two destinations overlap; or any failure
///         `composeNarrowbandPalette` reports for an entry, checked for every entry before the first
///         is written. A failure never changes an unwritten result slot.
[[nodiscard]] AFTERNIGHTENGINE_API Status composeNarrowbandPaletteGrid(
    const ExecutionContext& execution, SampleType sampleType, const NarrowbandPaletteChannels& channels,
    std::span<const NarrowbandPaletteDestination> destinations, const NarrowbandPaletteIntensities& intensities,
    std::span<NarrowbandPaletteResult> results, bool normalize = true, const KernelValidityView& validity = {});

/// Builds the metadata transition for a palette result.
///
/// The reference channel's meaning is restated unchanged, so its transfer function, processing flags
/// and shared validity rule are retained, and the lineage entry names the palette. The astrometric
/// solution remains valid because the geometry is unchanged, while codec cards and an earlier output
/// intent are withdrawn for the changed schema. The new RGB description, whose basis is a narrowband
/// mapping, is `narrowbandPaletteOutputDescriptor`'s and travels beside this transition.
///
/// @param source meaning of the prepared reference channel.
/// @param palette the palette that was composed.
/// @return the transition; or `ErrorCode::InvalidArgument` for an unknown palette or a transition
///         `operationTransition` refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition> narrowbandPaletteTransition(const ImageSemantics& source,
                                                                                          NarrowbandPalette palette);

/// What palette composition does to its destination image.
inline constexpr OperationEffect kNarrowbandPaletteEffect{.samples = SampleEffect::Writes,
                                                          .description = DescriptionEffect::Restates,
                                                          .semantics = SemanticEffect::Restates,
                                                          .solution = SolutionEffect::Preserves,
                                                          .codecMetadata = CodecMetadataEffect::Drops,
                                                          .outputIntent = OutputIntentEffect::Withdraws,
                                                          .validity = {.policy = ValidityPolicy::Preserve}};

} // namespace an
