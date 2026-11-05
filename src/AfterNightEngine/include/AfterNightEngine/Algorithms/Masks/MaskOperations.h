// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What is done to a packed validity mask, and what a mask does to an image.
///
/// `Image/ValidityDescriptor.h` says which of an image's samples mean something and
/// `Image/ValidityMask.h` is the packed mask a rule may need. Both are part of the image model, so
/// neither takes an operation: the model's `combineValidity` and `invertValidity` walk one mask on
/// the calling thread with no dispatch, which is the right shape for a value type and the wrong shape
/// for three megabytes of bits per frame.
///
/// This is the operation half. Everything here takes an `ExecutionContext`, runs on the threads that
/// operation was admitted and checks cancellation. The combine, the inversion and the count also reach
/// the instruction set the operation dispatches to: a mask combine is a byte-wise `and` or `or` over a
/// few hundred kilobytes and a mask count is a population count over the same - both are exactly what
/// a vector unit is for, and both take their scalar body when the backend is forced to the reference
/// so that the two can be compared. The threshold, marking, apply and binarize bodies are scalar on
/// every backend.
///
/// Besides the failures each operation lists, every one of them answers
/// `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build has no
/// dispatch table for, and passes on the scheduler's own `ErrorCode::AllocationFailed` or
/// `ErrorCode::ExternalFailure` as `parallelFor` states them.
///
/// **A set bit means the sample means something**, everywhere, as the mask view states. Nothing here
/// changes that convention or offers one of its own.
///
/// **The bits past the last pixel of a row stay zero.** Every write here masks the final byte of each
/// row, so a mask this file produced is countable and byte-for-byte comparable with a hand-written
/// one. That is why a combine is not simply an `and` over the spanned bytes: the padding *between*
/// rows is not part of any row and is left exactly as it was found.
///
/// **A threshold is a comparison and never arithmetic.** The bound is compared against the sample in
/// double, which is exact for all six integer representations, so a threshold means the same thing
/// over every representation rather than meaning full scale over one and a code value over another.
/// What a value that is not a number does is stated by the request rather than falling out of the
/// comparison's own answer, which for a not-a-number is false whichever way the bound is written.
///
/// **Writing a value into an image is a conversion, not a cast.** The two values a binarize writes and
/// the value an apply substitutes are converted once, before the loop, through
/// `Image/SampleConversion.h` under a policy the request carries. So writing one into a UInt8 image
/// means what the stated domain mapping says it means, and nothing here has an opinion about whether
/// one is white or is the code value one.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, for the reason `Image/ValidityMask.h` names the engine context:
/// a header that describes a mask operation should not put the runtime's whole include closure into
/// every translation unit that reads it.
class ExecutionContext;

/// Which samples a threshold selects.
///
/// Six relations rather than one with a negation flag, because "not below" and "at or above" differ
/// for a value that is not a number and the difference is exactly the thing a caller has to be able
/// to state.
enum class MaskPredicate : std::uint8_t
{
    /// The sample is strictly below the lower bound.
    Below = 0u,

    /// The sample is at or below the lower bound.
    AtOrBelow = 1u,

    /// The sample is at or above the lower bound.
    AtOrAbove = 2u,

    /// The sample is strictly above the lower bound.
    Above = 3u,

    /// The sample is inside the closed interval between the two bounds.
    InsideClosedRange = 4u,

    /// The sample is outside the closed interval between the two bounds.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    OutsideClosedRange = 5u
};

/// Whether a value names a relation this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param predicate the value to inspect.
/// @return true for every implemented relation.
[[nodiscard]] constexpr bool isMaskPredicate(MaskPredicate predicate) noexcept
{
    return static_cast<std::uint8_t>(predicate) <= static_cast<std::uint8_t>(MaskPredicate::OutsideClosedRange);
}

/// Whether a relation reads a second bound.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param predicate the relation to inspect.
/// @return true for the two range relations alone.
[[nodiscard]] constexpr bool maskPredicateNeedsUpperBound(MaskPredicate predicate) noexcept
{
    return predicate == MaskPredicate::InsideClosedRange || predicate == MaskPredicate::OutsideClosedRange;
}

/// The relation's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param predicate the relation to name. A value outside the enumeration is named
///        `"UnknownMaskPredicate"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* maskPredicateName(MaskPredicate predicate) noexcept;

/// How the channels of one pixel combine into one shared answer.
///
/// Read only when a mask with one bit-plane is written from an image with more than one channel,
/// because that is the only case in which several answers have to become one.
enum class MaskChannelRule : std::uint8_t
{
    /// The pixel is selected only where every channel's sample is. The conservative direction, and
    /// what a footprint wants.
    EveryChannel = 0u,

    /// The pixel is selected where any channel's sample is.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AnyChannel = 1u
};

/// Whether a value names a channel rule this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rule the value to inspect.
/// @return true for both rules.
[[nodiscard]] constexpr bool isMaskChannelRule(MaskChannelRule rule) noexcept
{
    return rule == MaskChannelRule::EveryChannel || rule == MaskChannelRule::AnyChannel;
}

/// What one sample has to be for a threshold to select it.
///
/// A plain aggregate whose second bound is present exactly for the relations that read one, in the
/// shape `Image/ValidityDescriptor.h`'s request uses. Both bounds are compared in double, which is
/// exact for every representation this engine stores.
struct SampleThreshold
{
    /// Which relation decides.
    MaskPredicate predicate = MaskPredicate::AtOrAbove;

    /// The bound the four single-bound relations compare against, and the start of the interval the
    /// two range relations use. Must be a finite number.
    double lowerBound = 0.0;

    /// The end of the interval. Required by the two range relations, refused by the other four, and
    /// must be finite and not below `lowerBound`.
    std::optional<double> upperBound{};

    /// Whether a sample that is not a finite number is selected. Stated rather than derived, because
    /// every comparison against a not-a-number is false and the caller's intent is not.
    bool nonFiniteSelected = false;
};

/// A threshold and how a shared mask resolves a pixel's several channels.
struct MaskThresholdRequest
{
    /// What one sample has to be.
    SampleThreshold threshold{};

    /// How several channels become one bit. Required when a mask with one bit-plane is written from
    /// an image with more than one channel; refused otherwise, because a mask with one plane per
    /// channel has nothing to combine and a one-channel image has nothing to combine either.
    std::optional<MaskChannelRule> channelRule{};
};

/// A threshold and the two values it writes.
struct BinarizeRequest
{
    /// What one sample has to be.
    SampleThreshold threshold{};

    /// What a selected sample becomes. Must be a finite number.
    double selectedValue = 1.0;

    /// What an unselected sample becomes. Must be a finite number.
    double unselectedValue = 0.0;

    /// How the two values become samples of the destination representation. They are converted once,
    /// before the loop, so this decides what writing one into a narrow integer image means.
    ConversionPolicy conversion{};
};

/// What one binarize wrote.
struct BinarizeResult
{
    /// How many samples the relation selected.
    std::uint64_t selected = 0u;

    /// How many samples were considered.
    std::uint64_t total = 0u;

    /// How many of the two written values could not be represented exactly and were saturated. Zero,
    /// one or two, because the conversion happens once for each of them rather than once per sample.
    std::uint32_t saturatedValues = 0u;

    /// Two results are equal when all three numbers are equal.
    friend constexpr bool operator==(const BinarizeResult& left, const BinarizeResult& right) noexcept = default;
};

/// What one apply changed.
struct MaskApplyResult
{
    /// How many samples the mask said mean nothing, and which were therefore written.
    std::uint64_t replaced = 0u;

    /// How many samples were considered.
    std::uint64_t total = 0u;

    /// Whether the substituted value could not be represented exactly and was saturated.
    bool saturatedValue = false;

    /// Two results are equal when both numbers and the flag are equal.
    friend constexpr bool operator==(const MaskApplyResult& left, const MaskApplyResult& right) noexcept = default;
};

/// How many inputs one validity transition may carry.
///
/// A transition is over one operation's arguments, and no operation in this engine takes eight
/// images whose validity rules all differ. The limit is here so that materialising a combined mask
/// allocates nothing; a caller with more inputs than this combines them in stages.
inline constexpr std::uint32_t kMaxValidityTransitionInputs = 8u;

/// One input to a validity transition, and how to read what it claims.
///
/// A rule that a value can answer needs the samples; the mask rule needs the mask; `AllValid` needs
/// neither. What a given rule needs is checked rather than assumed, so an input that declares a mask
/// rule and hands over no mask is refused instead of being read as fully valid.
struct MaskedValidityInput
{
    /// What this input claims about its samples.
    ValidityDescriptor validity{};

    /// What the samples are. Read only when the rule is decided by value.
    SampleType sampleType = SampleType::Float32;

    /// Where the samples start, borrowed for the call. Required when the rule is decided by value
    /// and by nothing else; may be null otherwise.
    const void* samples = nullptr;

    /// Where the samples sit, borrowed for the call. Required alongside `samples`, and held by
    /// address rather than by value because a layout has no default state to fill a struct with.
    const ImageLayout* layout = nullptr;

    /// This input's own packed mask. Required exactly when the rule is `ValidityRule::ExplicitMask`.
    std::optional<ValidityMaskView<const std::uint8_t>> mask{};
};

/// Writes into a mask which of an image's samples a threshold selects.
///
/// The mask covers the image in the sense `checkMaskCovers` states: the same width and height, and
/// either one bit-plane that every channel shares or one per channel. A shared mask over a
/// multi-channel image needs the request's channel rule; a per-channel mask decides each channel on
/// its own and refuses one.
///
/// Every bit of the mask is written, so a mask handed in with anything in it comes back holding this
/// operation's answer and nothing else, with the bits past the last pixel of each row at zero.
///
/// Thread-safe with respect to `execution` and the image, both of which are only read. Runs on the
/// threads the operation was admitted. Cancellation may leave part of the mask written: this
/// operation is not failure-atomic.
///
/// @param execution the operation this threshold belongs to. Must hold something.
/// @param sampleType what the samples are. All eight resident representations are accepted, because
///        a comparison needs no arithmetic.
/// @param source where the image's samples start. Must not be null.
/// @param sourceLayout where they sit. Its element size must be that representation's.
/// @param request the relation, its bounds, what a non-finite sample does, and the channel rule.
/// @param mask where the answers go. Must cover the image, and must not share memory with it.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, an unknown relation or channel rule, a bound
///         that is not finite, a missing or forbidden second bound, or a missing or forbidden
///         channel rule; `ErrorCode::IncompatibleArguments` when the mask does not cover the image
///         or shares memory with it; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when
///         the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status buildImageMask(const ExecutionContext& execution, SampleType sampleType,
                                                         const void* source, const ImageLayout& sourceLayout,
                                                         const MaskThresholdRequest& request,
                                                         const ValidityMaskView<std::uint8_t>& mask);

/// Writes into a mask which of an image's samples mean something under a stated value rule.
///
/// The operation form of `markValidity`: the same rule, the same shared-mask meaning - a pixel means
/// something only where every channel's sample does - on the operation's threads and with its
/// cancellation. `ValidityRule::ExplicitMask` is refused here as it is there, because a mask cannot
/// be built from a rule that says a mask decides.
///
/// Thread-safe with respect to `execution` and the image, both of which are only read. Runs on the
/// threads the operation was admitted. Cancellation may leave part of the mask written.
///
/// @param execution the operation this marking belongs to. Must hold something.
/// @param sampleType what the samples are.
/// @param source where the image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param validity the rule to judge the samples by.
/// @param mask where the answers go. Must cover the image, and must not share memory with it.
/// @return success; or the same failures as `buildImageMask`, with `ErrorCode::InvalidArgument` for
///         a rule that is not one a value can answer.
[[nodiscard]] AFTERNIGHTENGINE_API Status markImageValidity(const ExecutionContext& execution, SampleType sampleType,
                                                            const void* source, const ImageLayout& sourceLayout,
                                                            const ValidityDescriptor& validity,
                                                            const ValidityMaskView<std::uint8_t>& mask);

/// Combines two masks into a third on the operation's threads.
///
/// The dispatched form of `combineValidity`. The destination may be one of the sources exactly - the
/// accumulation over many frames that makes a footprint one allocation rather than one per frame -
/// and may not partially overlap one.
///
/// Only each row's own bytes are written, so whatever padding lies between rows is left as it was.
///
/// Thread-safe with respect to `execution` and the sources. Runs on the threads the operation was
/// admitted. Cancellation may leave part of the destination written.
///
/// @param execution the operation this combination belongs to. Must hold something.
/// @param destination where the combined bits go.
/// @param left one source.
/// @param right the other.
/// @param combination whether a sample means something where both say so or where either does.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a combination outside the enumeration or a destination
///         that partially overlaps a source; `ErrorCode::IncompatibleArguments` when the three masks
///         are not the same shape; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when
///         the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status combineImageMasks(const ExecutionContext& execution,
                                                            const ValidityMaskView<std::uint8_t>& destination,
                                                            const ValidityMaskView<const std::uint8_t>& left,
                                                            const ValidityMaskView<const std::uint8_t>& right,
                                                            ValidityCombination combination);

/// Turns every meaningful sample of a mask into a meaningless one and back, on the operation's
/// threads.
///
/// The dispatched form of `invertValidity`, and unlike it a copy as well as an inversion: the
/// destination may be the source exactly, which is the in-place spelling, or a mask of its own.
///
/// The bits past the last pixel of each row stay zero, so an inverted mask is still countable and
/// still comparable byte for byte.
///
/// Thread-safe with respect to `execution` and the source. Runs on the threads the operation was
/// admitted. Cancellation may leave part of the destination written.
///
/// @param execution the operation this inversion belongs to. Must hold something.
/// @param destination where the inverted bits go.
/// @param source the mask to invert.
/// @return success; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a destination that partially overlaps the source;
///         `ErrorCode::IncompatibleArguments` when the two masks are not the same shape; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Status invertImageMask(const ExecutionContext& execution,
                                                          const ValidityMaskView<std::uint8_t>& destination,
                                                          const ValidityMaskView<const std::uint8_t>& source);

/// Counts how many of a mask's pixels mean something, on the operation's threads.
///
/// The dispatched form of `countValidity`. The bits past the last pixel of a row are excluded
/// whatever they hold, and the per-row counts are whole numbers whose addition is exact, so the
/// answer does not depend on how many workers the operation was granted or on the order they
/// finished in.
///
/// Thread-safe with respect to `execution` and the mask, both of which are only read.
///
/// @param execution the operation this count belongs to. Must hold something.
/// @param mask the mask to count.
/// @return how many bits are set and how many there are; `ErrorCode::NotInitialized` when
///         `execution` holds nothing; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`
///         when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ValidityCount>
countImageMask(const ExecutionContext& execution, const ValidityMaskView<const std::uint8_t>& mask);

/// Writes a stated value into every sample a mask says means nothing.
///
/// The apply half of the mask family, in place. A mask with one bit-plane answers for every channel
/// of the pixel and one with a plane per channel answers for its own, which is the same pairing rule
/// every other operation here uses.
///
/// The substituted value is converted to the image's representation once, before the loop, under the
/// stated policy, so a value the representation cannot hold is saturated once and reported once
/// rather than being decided per sample.
///
/// Thread-safe with respect to `execution` and the mask. Runs on the threads the operation was
/// admitted. Cancellation may leave part of the image written: this operation is not failure-atomic.
///
/// @param execution the operation this application belongs to. Must hold something.
/// @param sampleType what the samples are.
/// @param samples where the image's samples start, and where the substitutions are written. Must not
///        be null.
/// @param layout where they sit.
/// @param mask which samples mean something. Must cover the image, and must not share memory with
///        it.
/// @param invalidValue what a meaningless sample becomes. Must be a finite number.
/// @param conversion how that value becomes a sample of this representation.
/// @return what was replaced; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a null buffer, a representation this build does not
///         know, a layout that does not describe it, a value that is not finite, or a conversion
///         policy field outside its enumeration; `ErrorCode::IncompatibleArguments` when the mask
///         does not cover the image or shares memory with it; or `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MaskApplyResult>
applyImageMask(const ExecutionContext& execution, SampleType sampleType, void* samples, const ImageLayout& layout,
               const ValidityMaskView<const std::uint8_t>& mask, double invalidValue,
               const ConversionPolicy& conversion = ConversionPolicy{});

/// Writes one of two stated values into every sample, according to a threshold.
///
/// The samples half of the same relation `buildImageMask` writes into bits. Each channel is
/// thresholded on its own - there is no channel rule, because there is one destination sample per
/// source sample and nothing to combine.
///
/// The destination may be the source exactly, which is the in-place spelling; every other overlap is
/// refused.
///
/// Thread-safe with respect to `execution` and the source. Runs on the threads the operation was
/// admitted. Cancellation may leave part of the destination written.
///
/// @param execution the operation this binarize belongs to. Must hold something.
/// @param sampleType what the samples are, on both sides.
/// @param source where the image's samples start. Must not be null.
/// @param sourceLayout where they sit.
/// @param request the relation, its bounds, the two values and the conversion policy.
/// @param destination where the two values go. Must not be null.
/// @param destinationLayout where they sit. The same picture as the source's.
/// @return how many samples were selected; `ErrorCode::NotInitialized` when `execution` holds
///         nothing; `ErrorCode::InvalidArgument` for a null buffer, a representation this build does
///         not know, a layout that does not describe it, an unknown relation, a bound or written
///         value that is not finite, a missing or forbidden second bound, or a conversion policy
///         field outside its enumeration; `ErrorCode::IncompatibleArguments` when the two layouts
///         are not the same picture or partially overlap; or `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BinarizeResult>
binarizeImage(const ExecutionContext& execution, SampleType sampleType, const void* source,
              const ImageLayout& sourceLayout, const BinarizeRequest& request, void* destination,
              const ImageLayout& destinationLayout);

/// Works out what an operation's output claims about its samples, and materialises the mask that
/// claim needs.
///
/// `applyValidityPolicy` answers the first half: given what an operation declared and what its
/// inputs claim, what does the output claim. Its `Combine` policy answers `ValidityRule::ExplicitMask`
/// whenever two inputs disagree, because a mask is the only description that can carry two different
/// rules sample by sample - and then somebody has to actually build that mask. This is that somebody.
///
/// The mask is the intersection of what every input says, evaluated sample by sample: a value rule is
/// evaluated against that input's own samples and a mask rule against that input's own mask. A shared
/// destination mask over a multi-channel input means something only where every channel does, which
/// is `markValidity`'s rule and the conservative direction.
///
/// A destination mask is required exactly when the answer's rule needs one, and refused when it does
/// not, so a caller cannot hand over a mask that nothing will fill in.
///
/// Thread-safe with respect to `execution` and every input, all of which are only read. Runs on the
/// threads the operation was admitted.
///
/// @param execution the operation this transition belongs to. Must hold something.
/// @param transition what the operation declared it does with its inputs' validity.
/// @param inputs what each input claims and how to read it, in the operation's own argument order.
/// @param destinationMask where the materialised answer goes. Required when the resulting rule is
///        `ValidityRule::ExplicitMask`, refused otherwise, and must cover every input that supplies
///        samples or a mask.
/// @return the output's validity; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         whatever `applyValidityPolicy` refused; `ErrorCode::LimitExceeded` for more than
///         `kMaxValidityTransitionInputs` inputs; `ErrorCode::InvalidArgument` when the destination
///         mask is missing or was supplied where none is needed, or - checked only when a mask is
///         materialised - when an input does not carry what its rule needs or its layout does not
///         describe its representation; `ErrorCode::IncompatibleArguments` when the destination
///         mask does not cover an input's image or the inputs are not all the same picture; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the operation was stopped.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ValidityDescriptor>
resolveImageValidity(const ExecutionContext& execution, const ValidityTransition& transition,
                     std::span<const MaskedValidityInput> inputs,
                     std::optional<ValidityMaskView<std::uint8_t>> destinationMask);

/// What `buildImageMask`, `combineImageMasks`, `invertImageMask` and `countImageMask` do to the
/// image they read.
///
/// All four produce a mask or a count and write no image sample, so none of them changes the image
/// they were given. Whether the mask they produced becomes that image's validity claim is a separate
/// act, taken by whatever holds the image and recorded in its own transition - which is why building
/// a mask does not declare `ValidityPolicy::Regenerate` here.
inline constexpr OperationEffect kMaskInspectionEffect{.samples = SampleEffect::Reads};

/// What `applyImageMask` does to the image it writes.
///
/// Substituting a stated value for every sample the mask says means nothing changes what the image
/// claims about itself: the samples that carried no meaning now carry a finite number that the
/// operation put there. The result therefore regenerates its validity as all-valid, and states the
/// meaning that carries it - an image whose semantics still named the old rule would be claiming
/// that samples the caller has just replaced are still absent.
inline constexpr OperationEffect kApplyImageMaskEffect{
    .semantics = SemanticEffect::Restates, .validity = ValidityTransition{.policy = ValidityPolicy::Regenerate}};

/// What `binarizeImage` does to the image it writes.
///
/// A binarized image is a selection map rather than a picture of anything, so what its samples mean
/// is stated by the operation and not inherited. Both written values are finite, so every sample of
/// the result means something whatever the source claimed.
inline constexpr OperationEffect kBinarizeImageEffect{
    .semantics = SemanticEffect::Restates, .validity = ValidityTransition{.policy = ValidityPolicy::Regenerate}};
} // namespace an
