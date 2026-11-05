// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Measured narrowband channels from a Bayer mosaic.
///
/// A colour-filter array contains one measured red site, two measured green sites and one measured
/// blue site per Bayer block. Hydrogen-alpha is the measured red site; the green product is the mean
/// of the two measured green sites; and the compatibility H-alpha/OIII pair equalizes the two green
/// sites and the blue site before filling the red positions from their neighbours. None of those
/// choices is inferred from a sample type, and the phase carried by CfaDescriptor is always applied.
///
/// Only complete two-by-two blocks are read: an odd last column or row is cropped and the crop is
/// reported. The single-plane extractions accept every resident representation and copy or average
/// measured sites; the pair runs on a Float32 or Float64 normalized working mosaic and, when asked,
/// resamples one of its products by a factor of two with the separable Lanczos filter defined on
/// `NarrowbandExtractionScaling`.
///
/// The pair takes the red site from the descriptor in every phase, GRBG and GBRG included, and its
/// scaled products use this header's own Lanczos definition rather than an imaging library's.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <cstdint>
#include <optional>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What has been done to an image. Defined in `Image/ImageSemantics.h`.
class ImageSemantics;

/// Metadata produced by an image transition. Defined in `Image/ImageMetadata.h`.
struct MetadataTransition;

/// Which compatibility product is being recorded in a metadata transition.
enum class NarrowbandExtractionProduct : std::uint8_t
{
    /// The measured red photosite from each complete Bayer block.
    HydrogenAlpha = 0u,

    /// The arithmetic mean of the two measured green photosites in each complete Bayer block.
    Green,

    /// The measured hydrogen-alpha plane and the equalized/interpolated OIII plane together.
    ///
    /// Deliberately the last enumerator, so membership needs no table.
    HydrogenAlphaAndOxygenIII
};

/// Whether a value names an extraction product this build implements.
///
/// @param product the value to inspect.
/// @return true for every declared product.
[[nodiscard]] constexpr bool isNarrowbandExtractionProduct(NarrowbandExtractionProduct product) noexcept
{
    return static_cast<std::uint8_t>(product) <=
           static_cast<std::uint8_t>(NarrowbandExtractionProduct::HydrogenAlphaAndOxygenIII);
}

/// How the two products of a compatibility H-alpha/OIII extraction are sized.
///
/// Both resampling choices use one definition: the Lanczos kernel `sinc(d) sinc(d / 4)`, with
/// `sinc(t) = sin(pi t) / (pi t)`, for `|d| < 4` and zero beyond; eight taps normalized by their sum;
/// applied separably, rows into a Float64 working plane charged to the engine context and then
/// columns. Pixel centres sit at half-pixel positions, so output pixel `x` is centred on source
/// coordinate `(x + 0.5) * s - 0.5`, where `s` is the source size over the output size, and a tap
/// beyond the picture reads its nearest edge sample. The result is not clamped, so the kernel's
/// negative lobes can carry a value beside a sharp edge outside the input's range.
enum class NarrowbandExtractionScaling : std::uint8_t
{
    /// Keep H-alpha at one pixel per Bayer block and OIII at the mosaic's cropped full resolution.
    Native = 0u,

    /// Resample H-alpha to the cropped OIII extent with a four-lobe Lanczos filter.
    UpsampleHydrogenAlpha,

    /// Resample OIII to the H-alpha extent with a four-lobe Lanczos filter.
    ///
    /// Deliberately the last enumerator, so membership needs no table.
    DownsampleOxygenIII
};

/// Whether a value names an extraction scaling this build implements.
///
/// @param scaling the value to inspect.
/// @return true for every declared scaling.
[[nodiscard]] constexpr bool isNarrowbandExtractionScaling(NarrowbandExtractionScaling scaling) noexcept
{
    return static_cast<std::uint8_t>(scaling) <=
           static_cast<std::uint8_t>(NarrowbandExtractionScaling::DownsampleOxygenIII);
}

/// The scaling's stable name.
///
/// @param scaling the value to name.
/// @return its enumerator spelling, or `"UnknownNarrowbandExtractionScaling"` for another value.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
narrowbandExtractionScalingName(NarrowbandExtractionScaling scaling) noexcept;

/// The checked descriptions of one H-alpha/OIII extraction.
class NarrowbandExtractionPlan
{
public:
    /// Builds an empty plan, useful as a destination before planning.
    NarrowbandExtractionPlan() = default;

    /// Whether this value came from `planNarrowbandExtraction`.
    ///
    /// @return true when all three descriptions are present.
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return m_source.has_value() && m_hydrogenAlpha.has_value() && m_oxygenIII.has_value();
    }

    /// The source mosaic description against which the plan was built.
    ///
    /// @return the source descriptor. Defined only when `valid()` is true.
    [[nodiscard]] constexpr const ImageDescriptor& sourceDescriptor() const noexcept
    {
        return *m_source;
    }

    /// The H-alpha output description, always monochrome and with no unresolved CFA.
    ///
    /// @return the H-alpha descriptor. Defined only when `valid()` is true.
    [[nodiscard]] constexpr const ImageDescriptor& hydrogenAlphaDescriptor() const noexcept
    {
        return *m_hydrogenAlpha;
    }

    /// The OIII output description, always monochrome and with no unresolved CFA.
    ///
    /// @return the OIII descriptor. Defined only when `valid()` is true.
    [[nodiscard]] constexpr const ImageDescriptor& oxygenIIIDescriptor() const noexcept
    {
        return *m_oxygenIII;
    }

    /// Which output was resampled, if either.
    ///
    /// @return the validated scaling choice.
    [[nodiscard]] constexpr NarrowbandExtractionScaling scaling() const noexcept
    {
        return m_scaling;
    }

private:
    /// Builds a plan whose descriptions and scaling have been checked.
    ///
    /// @param source the source mosaic description.
    /// @param hydrogenAlpha the H-alpha output description.
    /// @param oxygenIII the OIII output description.
    /// @param scaling which output was resampled.
    constexpr NarrowbandExtractionPlan(const ImageDescriptor& source, const ImageDescriptor& hydrogenAlpha,
                                       const ImageDescriptor& oxygenIII, NarrowbandExtractionScaling scaling) noexcept
        : m_source(source), m_hydrogenAlpha(hydrogenAlpha), m_oxygenIII(oxygenIII), m_scaling(scaling)
    {
    }

    /// Source mosaic description, absent before planning.
    std::optional<ImageDescriptor> m_source{};

    /// H-alpha output description, absent before planning.
    std::optional<ImageDescriptor> m_hydrogenAlpha{};

    /// OIII output description, absent before planning.
    std::optional<ImageDescriptor> m_oxygenIII{};

    /// Validated scaling choice.
    NarrowbandExtractionScaling m_scaling = NarrowbandExtractionScaling::Native;

    friend AFTERNIGHTENGINE_API Result<NarrowbandExtractionPlan> planNarrowbandExtraction(const ImageDescriptor&,
                                                                                          NarrowbandExtractionScaling);
};

/// Counts and geometry reported by a measured-channel extraction.
struct NarrowbandExtractionResult
{
    /// Pixels considered and admitted, and samples written.
    KernelRunResult run{};

    /// Complete two-by-two Bayer blocks covered across the source.
    std::uint32_t blockColumns = 0u;

    /// Complete two-by-two Bayer blocks covered down the source.
    std::uint32_t blockRows = 0u;

    /// Source columns excluded because they do not complete a Bayer block.
    std::uint32_t trailingColumns = 0u;

    /// Source rows excluded because they do not complete a Bayer block.
    std::uint32_t trailingRows = 0u;

    /// Non-finite green and blue source samples, each left out of the level means and taken as zero
    /// by the OIII product. Zero for the single-plane extractions, which do not inspect values.
    std::uint64_t nonFiniteSamples = 0u;

    /// Workers the scheduled write could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// Peak bytes held from the engine context for a separable Lanczos working plane. Zero when no
    /// output was resampled.
    std::uint64_t workingBytes = 0u;
};

/// Plans the compatibility H-alpha/OIII extraction from one Bayer working image.
///
/// The source must be a one-channel mono Float32 or Float64 image in `NumericDomain::NormalizedLinear`
/// with one of the four named Bayer patterns. Odd right and bottom edges are cropped explicitly.
/// `Native` therefore produces a half-size H-alpha descriptor and an even full-size OIII descriptor;
/// the other choices make both products share one of those extents. The coordinate origin and pixel
/// aspect are retained, and the CFA is cleared because both products name resolved measurements.
///
/// @param source the mosaic description.
/// @param scaling which product, if either, is resampled.
/// @return the checked plan; `ErrorCode::InvalidArgument` for an unknown scaling;
///         `ErrorCode::Unsupported` for a non-floating, non-normalized or non-Bayer image;
///         `ErrorCode::IncompatibleArguments` when no complete Bayer block exists or an output
///         description is inconsistent; or `ErrorCode::LimitExceeded` for an output extent beyond the
///         engine limits.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandExtractionPlan>
planNarrowbandExtraction(const ImageDescriptor& source, NarrowbandExtractionScaling scaling);

/// Describes one measured H-alpha or green plane from a Bayer source.
///
/// @param source a one-channel image with one of the four named Bayer patterns.
/// @return a same-representation monochrome description whose width and height are halved, rounding
///         down, and whose CFA and colour space are cleared; or the descriptor and geometry failures
///         named by `planNarrowbandExtraction`, except that every resident representation and numeric
///         domain is accepted.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageDescriptor>
measuredBayerChannelDescriptor(const ImageDescriptor& source);

/// Extracts the measured red photosite from every complete Bayer block.
///
/// The source and destination may use any resident representation and any checked planar or
/// interleaved layout consistent with their one-channel descriptions. The destination must match
/// `measuredBayerChannelDescriptor(sourceDescriptor)` and must not overlap the source. No numeric
/// conversion occurs: every output value is the red site selected through the descriptor's phase,
/// carried through double, which holds every resident representation's values exactly. Values are
/// not inspected, so a non-finite red site is copied as it is. Cancellation may leave earlier rows
/// written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sourceDescriptor the source mosaic description.
/// @param source first byte of source storage.
/// @param sourceLayout source geometry and strides.
/// @param destination first writable byte of the H-alpha storage.
/// @param destinationLayout destination geometry and strides.
/// @return the extraction counts; `ErrorCode::Unsupported` for a source description that is not a
///         one-channel mono named Bayer mosaic; `ErrorCode::IncompatibleArguments` for a source
///         without one complete Bayer block, a layout whose extent is not its description's, or a
///         destination overlapping the source; `ErrorCode::LimitExceeded` for an output extent
///         beyond the engine limits; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to a backend this build
///         lacks; or `ErrorCode::InvalidArgument` for a null or misaligned pointer or a layout whose
///         element size is not the sample type's.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandExtractionResult>
extractBayerHydrogenAlpha(const ExecutionContext& execution, const ImageDescriptor& sourceDescriptor,
                          const void* source, const ImageLayout& sourceLayout, void* destination,
                          const ImageLayout& destinationLayout);

/// Extracts the mean of the two measured green photosites from every complete Bayer block.
///
/// Integer inputs are averaged in their normalized domain and written with the approved
/// nearest-even conversion, so the operation does not overflow at the representation's maximum.
/// Floating inputs use the same arithmetic in double. Values are not inspected, so in a floating
/// mosaic a non-finite green site makes its block's mean non-finite. The source and destination are
/// disjoint and cancellation may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param sourceDescriptor the source mosaic description.
/// @param source first byte of source storage.
/// @param sourceLayout source geometry and strides.
/// @param destination first writable byte of the green storage.
/// @param destinationLayout destination geometry and strides.
/// @return the extraction counts; or the same failures as `extractBayerHydrogenAlpha`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandExtractionResult>
extractBayerGreen(const ExecutionContext& execution, const ImageDescriptor& sourceDescriptor, const void* source,
                  const ImageLayout& sourceLayout, void* destination, const ImageLayout& destinationLayout);

/// Builds the compatibility H-alpha and OIII products described by a checked plan.
///
/// The three OIII photosites in each Bayer block are level-equalized: each position's samples are
/// multiplied by the mean of the three positions' finite global means over its own, a position whose
/// mean is within double epsilon of zero being left unscaled. The red positions are filled from the
/// equalized finite OIII measurements among their eight neighbours, axial ones weighted by the square
/// root of two and diagonal ones by one, in every Bayer phase. A non-finite source value contributes
/// neither to a mean nor to an interpolation and is taken as zero, as is a red position with no
/// finite neighbour. Optional resizing uses the Lanczos definition on `NarrowbandExtractionScaling`
/// and holds one context-accounted Float64 working plane for the duration of that resize, taken
/// before either product is written. Both destinations must be disjoint from the source and each
/// other. Cancellation may leave earlier rows written.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the plan returned by `planNarrowbandExtraction`.
/// @param source first byte of source storage.
/// @param sourceLayout source geometry and strides matching the plan.
/// @param hydrogenAlphaDestination first writable byte of the H-alpha product.
/// @param hydrogenAlphaLayout layout matching `plan.hydrogenAlphaDescriptor()`.
/// @param oxygenIIIDestination first writable byte of the OIII product.
/// @param oxygenIIILayout layout matching `plan.oxygenIIIDescriptor()`.
/// @return counts over both outputs; `ErrorCode::InvalidArgument` for a plan that is not
///         `valid()`; `ErrorCode::EmptySelection` when a green or blue position has no finite
///         sample; `ErrorCode::LimitExceeded` when the Lanczos working plane's size overflows;
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when it is refused;
///         the execution, descriptor, pointer and layout failures `extractBayerHydrogenAlpha`
///         documents; or `ErrorCode::IncompatibleArguments` when the source and the two destinations
///         are not pairwise disjoint.
[[nodiscard]] AFTERNIGHTENGINE_API Result<NarrowbandExtractionResult>
extractBayerHydrogenAlphaAndOxygenIII(const ExecutionContext& execution, const NarrowbandExtractionPlan& plan,
                                      const void* source, const ImageLayout& sourceLayout,
                                      void* hydrogenAlphaDestination, const ImageLayout& hydrogenAlphaLayout,
                                      void* oxygenIIIDestination, const ImageLayout& oxygenIIILayout);

/// Builds the metadata transition for a completed CFA narrowband extraction.
///
/// The transfer function, processing flags and all-valid claim are retained, except that
/// `ProcessingState::demosaiced` becomes true. The astrometric solution is invalidated because a
/// product's sampling differs from the sensor mosaic; codec cards are dropped and output intent is
/// withdrawn. The lineage names the product and scaling rather than relying on prose parsing.
///
/// @param source the source image's meaning.
/// @param product which product is being recorded.
/// @param scaling the scaling used; ignored for single-channel products but still validated.
/// @return the transition; `ErrorCode::InvalidArgument` for an unknown product or scaling;
///         `ErrorCode::IncompatibleArguments` when the source's validity is not all-valid, because
///         equalization and interpolation have no rule for a missing sample; or a failure building
///         the new meaning or the transition.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
narrowbandExtractionTransition(const ImageSemantics& source, NarrowbandExtractionProduct product,
                               NarrowbandExtractionScaling scaling = NarrowbandExtractionScaling::Native);

/// What a CFA narrowband extraction does to the image it writes.
///
/// The validity policy is `Reject`: a source with anything but all-valid data is refused, which
/// `narrowbandExtractionTransition` enforces.
inline constexpr OperationEffect kNarrowbandExtractionEffect{.samples = SampleEffect::Writes,
                                                             .description = DescriptionEffect::Restates,
                                                             .semantics = SemanticEffect::Restates,
                                                             .solution = SolutionEffect::Invalidates,
                                                             .codecMetadata = CodecMetadataEffect::Drops,
                                                             .outputIntent = OutputIntentEffect::Withdraws,
                                                             .validity = {.policy = ValidityPolicy::Reject}};

} // namespace an
