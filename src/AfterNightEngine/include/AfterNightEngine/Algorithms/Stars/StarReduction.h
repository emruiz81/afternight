// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Direct star reduction over two explicitly compatible images.
///
/// Three material decisions are visible rather than taken inside an image-owning class: a plan
/// accepts one representation and one fractional numeric domain, the caller performs any
/// representation conversion before planning, and the plan publishes both output descriptors and
/// the metadata transitions that go with them.
///
/// Reduction, residual extraction and the protection mask stay together because
/// they are three views of the same target/starless pair. They share validation and no one of them
/// owns an image. Every call writes caller storage through a checked layout, uses the execution's
/// scheduler, and returns expected failures.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cstdint>
#include <optional>

namespace an
{

class ExecutionContext;
class ImageSemantics;
struct MetadataTransition;

/// Which compatibility equation reduces a target sample against its starless counterpart.
enum class StarReductionMethod : std::uint8_t
{
    /// The transfer expression `1 - (1 - M(t)) / (1 - M(s)) * (1 - s)`, where `M` is the midtones
    /// transform at `1 - stretchFactor` and `t` and `s` are the target and starless values.
    Transfer = 0u,

    /// The halo expression `t - (1 + t) / 2 * (linear - stretched) * (1 - s / 2)`, where `linear`
    /// is `1 - (1 - t) / (1 - s)` and `stretched` is the same ratio taken after `M`.
    Halo,

    /// The iterative PixInsight-style star-reduction expression `t*t + (1-t)*s`.
    Star,

    /// The iterative blend weighted by the target pixel's CIE lightness for RGB, or by the target
    /// sample itself for monochrome.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Lightness
};

/// Whether a value names a reduction method this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param method the value to inspect.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isStarReductionMethod(StarReductionMethod method) noexcept
{
    return static_cast<std::uint8_t>(method) <= static_cast<std::uint8_t>(StarReductionMethod::Lightness);
}

/// The reduction method's own name.
///
/// @param method the value to name.
/// @return the enumerator spelling, or `"UnknownStarReductionMethod"` for anything else. The
///         returned literal outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starReductionMethodName(StarReductionMethod method) noexcept;

/// How strongly the iterative star and lightness methods retain their reduced value.
enum class StarReductionStrength : std::uint8_t
{
    /// Use the iterative result directly.
    Strong = 0u,

    /// Raise the result by the compatibility post-process when that is brighter.
    Moderate,

    /// Apply the moderate rule and blend it back toward the target.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Soft
};

/// Whether a value names a reduction strength this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param strength the value to inspect.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isStarReductionStrength(StarReductionStrength strength) noexcept
{
    return static_cast<std::uint8_t>(strength) <= static_cast<std::uint8_t>(StarReductionStrength::Soft);
}

/// The reduction strength's own name.
///
/// @param strength the value to name.
/// @return the enumerator spelling, or `"UnknownStarReductionStrength"` for anything else. The
///         returned literal outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starReductionStrengthName(StarReductionStrength strength) noexcept;

/// The explicit choices used to plan one star reduction.
struct StarReductionRequest
{
    /// Which compatibility equation to use.
    StarReductionMethod method = StarReductionMethod::Star;

    /// Post-processing strength for `Star` and `Lightness`; ignored by `Transfer` and `Halo`.
    StarReductionStrength strength = StarReductionStrength::Strong;

    /// Iteration count for `Star` and `Lightness`, in [1, 3].
    std::uint32_t iterations = 1u;

    /// Midtones stretch amount for `Transfer` and `Halo`, strictly between zero and one; checked
    /// for every method. Those two use the midtones transform
    /// `M(x) = (m - 1) x / ((2 m - 1) x - m)` at `m = 1 - stretchFactor`.
    double stretchFactor = 0.2;

    /// The RGB encoding from which `Lightness` obtains CIE L*. Its schema and numeric domain must
    /// agree with an RGB target. Ignored for a monochrome target and for other methods.
    ColorEncoding lightnessEncoding{.schema = ChannelSchema::Rgb,
                                    .primaries = RgbPrimaries::Srgb,
                                    .transfer = ColorTransferFunction::Srgb,
                                    .whitePoint = ChromaticWhitePoint::D65};

    /// Whether two requests state every choice identically.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarReductionRequest& left,
                                                   const StarReductionRequest& right) noexcept = default;
};

/// The choices used when extracting the star residual of a reduced image.
struct ReducedStarExtractionRequest
{
    /// Whether to apply and then undo a midtones stretch around the unscreen.
    bool reverseStretch = false;

    /// The forward midtones value when `reverseStretch` is true, strictly between zero and one;
    /// checked whether or not it is used.
    double stretchFactor = 0.2;

    /// Whether two requests state the same extraction.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const ReducedStarExtractionRequest& left,
                                                   const ReducedStarExtractionRequest& right) noexcept = default;
};

/// A checked target/starless pairing and the descriptions of all three possible outputs.
///
/// Default construction holds no plan. A successful `planStarReduction` is immutable and safe to
/// use concurrently; it owns only small value records and no image memory.
class StarReductionPlan
{
public:
    /// Builds an empty plan, useful as a destination before planning.
    StarReductionPlan() = default;

    /// Whether this object came from `planStarReduction`.
    ///
    /// @return true when all descriptors are present.
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return m_input.has_value() && m_output.has_value() && m_mask.has_value();
    }

    /// The one description both inputs must have.
    ///
    /// @return the checked input descriptor. The result is defined only when `valid()` is true.
    [[nodiscard]] constexpr const ImageDescriptor& inputDescriptor() const noexcept
    {
        return *m_input;
    }

    /// Description of a reduced or extracted image.
    ///
    /// It keeps the geometry, representation, channels, colour basis and origin, and states the
    /// numeric domain as `EncodedNonlinear` because the reduction formula is nonlinear.
    ///
    /// @return the checked output descriptor. The result is defined only when `valid()` is true.
    [[nodiscard]] constexpr const ImageDescriptor& outputDescriptor() const noexcept
    {
        return *m_output;
    }

    /// Description of the single-channel protection mask.
    ///
    /// @return the checked mask descriptor, in the input representation and encoded unit domain.
    ///         The result is defined only when `valid()` is true.
    [[nodiscard]] constexpr const ImageDescriptor& maskDescriptor() const noexcept
    {
        return *m_mask;
    }

    /// The choices this plan validated.
    ///
    /// @return the immutable request.
    [[nodiscard]] constexpr const StarReductionRequest& request() const noexcept
    {
        return m_request;
    }

private:
    /// Builds a plan after every value has been checked.
    ///
    /// @param input the common input description.
    /// @param output the reduced-image description.
    /// @param mask the protection-mask description.
    /// @param request the validated choices.
    constexpr StarReductionPlan(const ImageDescriptor& input, const ImageDescriptor& output,
                                const ImageDescriptor& mask, const StarReductionRequest& request) noexcept
        : m_input(input), m_output(output), m_mask(mask), m_request(request)
    {
    }

    /// The common input description, absent before planning.
    std::optional<ImageDescriptor> m_input{};

    /// The reduced-image description, absent before planning.
    std::optional<ImageDescriptor> m_output{};

    /// The mask description, absent before planning.
    std::optional<ImageDescriptor> m_mask{};

    /// The checked choices.
    StarReductionRequest m_request{};

    friend AFTERNIGHTENGINE_API Result<StarReductionPlan>
    planStarReduction(const ImageDescriptor&, const ImageDescriptor&, const StarReductionRequest&);
};

/// Counts and execution facts from one reduction, extraction or mask construction.
struct StarReductionResult
{
    /// Pixels visited, once each regardless of channel count.
    std::uint64_t pixelsProcessed = 0u;

    /// Channel samples written.
    std::uint64_t samplesWritten = 0u;

    /// Computed real values clipped to the output unit interval.
    std::uint64_t clampedOutputs = 0u;

    /// Context-owned temporary bytes held at once and released before return.
    std::uint64_t workingBytes = 0u;

    /// Workers the scheduler could use for the operation.
    std::uint32_t plannedWorkers = 0u;

    /// Full-frame passes performed. Reduction and extraction use one; the compatibility mask uses
    /// several named passes.
    std::uint32_t imagePasses = 0u;

    /// Whether two results report exactly the same execution facts.
    ///
    /// @param left one result.
    /// @param right the other.
    /// @return true when every counter is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarReductionResult& left,
                                                   const StarReductionResult& right) noexcept = default;
};

/// Builds the shared plan for reduction, residual extraction and protection-mask construction.
///
/// Both inputs must have the exact same descriptor. That includes representation: a caller that
/// has a differently represented starless image converts it explicitly through
/// `Image/ImageConversion.h`, so the full-frame cost cannot hide inside this operation. Mosaics and
/// raw or physically scaled domains are refused; these equations are defined over unit fractions.
///
/// @param target what the target samples mean.
/// @param starless what the starless samples mean.
/// @param request equation, strength, iteration and lightness choices.
/// @return the immutable plan; `ErrorCode::IncompatibleArguments` when the descriptions differ or
///         a lightness encoding does not describe the target's RGB samples;
///         `ErrorCode::Unsupported` for a mosaic, a channel schema other than monochrome, RGB, RGBA
///         or named channels, a domain that is not a normalized or encoded unit fraction, or a
///         `Lightness` reduction of anything but monochrome or RGB; `ErrorCode::InvalidArgument`
///         for an unknown method or strength, or an incomplete lightness encoding; or
///         `ErrorCode::OutOfRange` for an iteration count or stretch outside its range.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarReductionPlan>
planStarReduction(const ImageDescriptor& target, const ImageDescriptor& starless,
                  const StarReductionRequest& request = {});

/// Reduces each target sample against its starless counterpart.
///
/// Source and destination layouts may independently be planar, interleaved or semi-planar. Every
/// normalized input must be finite and in [0, 1]; invalid data returns a failure and may leave
/// earlier row chunks written. The destination must be disjoint from both sources. Expected
/// denominator zeros take the compatibility value zero before the enclosing formula is clamped.
///
/// Thread-safe with respect to the execution, plan and sources. Work is partitioned by rows on the
/// operation's admitted threads; cancellation is checked at row-chunk boundaries.
///
/// @param execution operation policy and engine-context lease.
/// @param target first byte of the target image.
/// @param targetLayout target geometry and strides.
/// @param starless first byte of the starless image.
/// @param starlessLayout starless geometry and strides.
/// @param destination first writable byte of the reduced image.
/// @param destinationLayout output geometry and strides, describing `plan.outputDescriptor()`.
/// @param plan checked plan shared by the three operations.
/// @param targetValidity target's validity claim, which must be all-valid.
/// @param starlessValidity starless image's validity claim, which must be all-valid.
/// @return counts; `ErrorCode::NotInitialized` without an execution; `ErrorCode::InvalidArgument`
///         for a default plan or a null or misaligned buffer; `ErrorCode::IncompatibleArguments`
///         when a layout does not describe the image the plan expects or a validity claim is not
///         all-valid; `ErrorCode::Aliasing` when the destination overlaps either source;
///         `ErrorCode::InvalidData` for an input outside the finite unit interval; or a
///         colour-conversion, scheduling or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarReductionResult>
reduceStars(const ExecutionContext& execution, const void* target, const ImageLayout& targetLayout,
            const void* starless, const ImageLayout& starlessLayout, void* destination,
            const ImageLayout& destinationLayout, const StarReductionPlan& plan,
            const ValidityDescriptor& targetValidity = ValidityDescriptor::allValid(),
            const ValidityDescriptor& starlessValidity = ValidityDescriptor::allValid());

/// Extracts the residual stars from a reduced/starless pair.
///
/// The ordinary path computes `(reduced-starless)/(1-starless)`. The reverse-stretch path applies
/// the requested midtones transform to both inputs, unscreens, then applies the complementary
/// midtones value. The same layout, validity, aliasing and unit-domain rules as `reduceStars` apply.
///
/// @param execution operation policy and engine-context lease.
/// @param reduced first byte of the reduced image.
/// @param reducedLayout reduced-image geometry and strides.
/// @param starless first byte of the starless image.
/// @param starlessLayout starless geometry and strides.
/// @param destination first writable byte of the residual image.
/// @param destinationLayout output geometry and strides, describing `plan.outputDescriptor()`.
/// @param plan checked common input plan.
/// @param request ordinary or reverse-stretch extraction choices.
/// @param reducedValidity reduced image's validity claim, which must be all-valid.
/// @param starlessValidity starless image's validity claim, which must be all-valid.
/// @return counts, or the same validation and execution failures as `reduceStars`, plus
///         `ErrorCode::OutOfRange` for a stretch factor outside its range. The reduced image is
///         checked against the plan's input description, like the starless one.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarReductionResult>
extractReducedStars(const ExecutionContext& execution, const void* reduced, const ImageLayout& reducedLayout,
                    const void* starless, const ImageLayout& starlessLayout, void* destination,
                    const ImageLayout& destinationLayout, const StarReductionPlan& plan,
                    const ReducedStarExtractionRequest& request = {},
                    const ValidityDescriptor& reducedValidity = ValidityDescriptor::allValid(),
                    const ValidityDescriptor& starlessValidity = ValidityDescriptor::allValid());

/// Builds a small-star protection mask.
///
/// Channels are averaged, each input is rescaled by its own extrema, the unscreen ratio
/// `(t - s) / (t (1 - s))` is separated from its eleven-pixel circular median, stretched by a
/// midtones transform, averaged with its three-pixel cross dilation, box-smoothed over seven by
/// seven pixels, and inverted. Neighbourhoods replicate the frame's edge. Every input must be a
/// finite unit-interval value, as for `reduceStars`. Three Float32 planes come from the
/// engine-context memory resource and are released on every exit. The output has
/// `plan.maskDescriptor()` and may use any arrangement its one channel admits.
///
/// @param execution operation policy and engine-context lease.
/// @param target first byte of the target image.
/// @param targetLayout target geometry and strides.
/// @param starless first byte of the starless image.
/// @param starlessLayout starless geometry and strides.
/// @param destination first writable byte of the one-channel mask.
/// @param destinationLayout mask geometry and strides, describing `plan.maskDescriptor()`.
/// @param plan checked common input plan.
/// @param targetValidity target's validity claim, which must be all-valid.
/// @param starlessValidity starless image's validity claim, which must be all-valid.
/// @return counts; the same validation failures as `reduceStars`; `ErrorCode::LimitExceeded` when
///         the three working planes are not representable; or a memory-budget, cancellation or
///         scheduling failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarReductionResult>
buildSmallStarProtectionMask(const ExecutionContext& execution, const void* target, const ImageLayout& targetLayout,
                             const void* starless, const ImageLayout& starlessLayout, void* destination,
                             const ImageLayout& destinationLayout, const StarReductionPlan& plan,
                             const ValidityDescriptor& targetValidity = ValidityDescriptor::allValid(),
                             const ValidityDescriptor& starlessValidity = ValidityDescriptor::allValid());

/// Builds the metadata transition for a reduced image.
///
/// Processing flags are preserved, the transfer function becomes unspecified because the
/// compatibility equation is not a named display transfer, and provenance records the method,
/// strength, iteration count and stretch. The plan's output descriptor is the other half of the
/// transition and is supplied to `Image::transition` by the caller.
///
/// @param plan the plan that produced the samples.
/// @param source the target image's current processing meaning, whose validity must be all-valid.
/// @return the checked transition; `ErrorCode::InvalidArgument` for a default plan;
///         `ErrorCode::IncompatibleArguments` when `source` is not all-valid; or the failure
///         building the derived semantics reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition> starReductionTransition(const StarReductionPlan& plan,
                                                                                      const ImageSemantics& source);

/// Builds the metadata transition for an extracted residual image.
///
/// @param plan the plan that described the reduced image.
/// @param source the reduced image's current processing meaning, whose processing flags are
///        preserved and whose validity must be all-valid.
/// @param request the extraction choices to record.
/// @return the checked transition; `ErrorCode::InvalidArgument` for a default plan;
///         `ErrorCode::OutOfRange` for a stretch factor outside its range;
///         `ErrorCode::IncompatibleArguments` when `source` is not all-valid; or the failure
///         building the derived semantics reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
reducedStarExtractionTransition(const StarReductionPlan& plan, const ImageSemantics& source,
                                const ReducedStarExtractionRequest& request = {});

/// Builds the metadata transition for a protection mask.
///
/// A mask is a new measurement rather than the target under another encoding: its processing state
/// is empty, astrometry is invalidated, source cards are dropped and any output intent is
/// withdrawn. The mask descriptor comes from the plan and is intentionally not repeated here.
///
/// @return the checked transition. Construction uses only fixed valid values; a failure therefore
///         reports an internal invariant or a provenance allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition> smallStarProtectionMaskTransition();

/// What direct star reduction does to the image it writes.
inline constexpr OperationEffect kStarReductionEffect{.description = DescriptionEffect::Restates,
                                                      .semantics = SemanticEffect::Restates,
                                                      .validity = ValidityTransition{.policy = ValidityPolicy::Reject}};

/// What residual extraction does to the image it writes.
inline constexpr OperationEffect kReducedStarExtractionEffect{.description = DescriptionEffect::Restates,
                                                              .semantics = SemanticEffect::Restates,
                                                              .validity =
                                                                  ValidityTransition{.policy = ValidityPolicy::Reject}};

/// What protection-mask construction does to the image it writes.
inline constexpr OperationEffect kSmallStarProtectionMaskEffect{
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .solution = SolutionEffect::Invalidates,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = ValidityTransition{.policy = ValidityPolicy::Reject}};

} // namespace an
