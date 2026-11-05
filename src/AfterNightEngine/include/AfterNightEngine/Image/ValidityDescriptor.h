// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which of an image's samples mean something, stated rather than inferred.
///
/// Some pixels mean nothing: outside a registered frame's footprint, divided by a zero flat,
/// rejected by a sigma clip, or simply never written. Answered contextually - one path treating exact
/// zero as absent, another NaN, a third neither - the answers spread across the algorithms rather than
/// travelling with the image, and two operations reading one frame disagree about how many pixels it
/// has, with neither answer written down anywhere a third could read. Here the image states it.
///
/// **A rule is always present; a mask is materialised only when a rule needs one.** Carrying this
/// descriptor is free - it is a byte and two numbers - and carrying a packed mask is not. The cost of
/// a mask is not its memory, which is about 3 MB against a 24-megapixel frame's 48 MB; the cost is
/// that every kernel then has to honour it, which doubles the branches in the inner loop and the
/// cases in a conformance run. That is why the mask is opt-in per image rather than universal, and
/// why `ValidityRule::ExplicitMask` is one rule among five rather than the only representation.
///
/// **The default is all-valid, and no rule is ever inferred from the pixels.** An image is fully
/// valid until something makes it otherwise. Calibration that divides by a zero flat sets
/// `FiniteOnly` because it has just produced NaN; registration that resamples into a larger canvas
/// sets `ExplicitMask` because it knows the footprint. Nothing looks at the samples and guesses,
/// because a black sky and an unwritten pixel are the same bits.
///
/// **Exact zero is a compatibility rule and never an unasked-for default.** Treating zero as absent
/// is a real convention in some data and some paths this engine has to keep working with, and it is
/// also a catastrophic default: a genuinely black pixel is data. `ZeroIsNoData` is available, it is
/// preserved where a source requires it, and no unrelated algorithm acquires it.
///
/// **Every operation declares what it does with validity.** `ValidityPolicy` is that declaration -
/// preserve, combine, regenerate, or reject - and `applyValidityPolicy` is where it is honoured.
/// Declared rather than implied, so an operation that has not thought about it says so at its own
/// call site instead of quietly producing an all-valid output from partly invalid input. Refusing is
/// a legitimate answer for an operation that has no meaning on incomplete data.
///
/// **A reduction over nothing says so.** `ValidityCount` and `requireSelection` are the other half of
/// the same idea: the mean of no samples is a distinguishable outcome that usually decides whether a
/// frame is used at all, and it reaches the caller as one rather than as a NaN nobody planned or a
/// division by zero.
///
/// This descriptor is part of what an image *means*, so it travels in `Image/ImageSemantics.h` and a
/// change to it moves the image's semantic generation. The mask a rule may need is memory rather
/// than meaning, so it does not.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace an
{

/// How an image says which of its samples mean something.
///
/// Five rules, and no sixth. Each of them is a convention this engine actually meets, and each is
/// cheap to evaluate on a sample except the one that is not, which is why the expensive one is
/// named separately rather than being how every image works.
enum class ValidityRule : std::uint8_t
{
    /// Every sample means something. The default, and the description of an image nothing has
    /// invalidated.
    AllValid,

    /// A sample that is not a finite number means nothing. What a calibration divide leaves behind
    /// when the flat holds a zero, and the rule an operation sets because it has just produced the
    /// non-finite values rather than because it inspected them.
    ///
    /// An integer sample is always finite, so this rule over an integer image is the same set of
    /// samples as `AllValid` - stated rather than refused, because an image keeps its rule across
    /// a conversion and refusing it here would make the rule depend on the representation.
    FiniteOnly,

    /// A sample that is exactly zero means nothing. The compatibility rule, present because the
    /// convention exists in real data and in paths this engine has to keep reproducing.
    ///
    /// Never a default and never acquired: an image has this rule only because something stated it.
    ZeroIsNoData,

    /// A packed mask beside the image decides, sample by sample. The general answer, and the only
    /// one that can express "invalid, and the value is 0.3".
    ///
    /// The mask is `Image/ValidityMask.h` and is carried by whatever owns the image rather than by
    /// the description, because it is memory: it is allocated, counted against a budget and
    /// released, none of which a value type does.
    ExplicitMask,

    /// A sample outside a stated closed range means nothing. What a sensor's saturation limit and a
    /// physically bounded quantity look like when the bound is a number rather than a mask.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    DomainRange
};

/// How many rules `ValidityRule` holds.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `DomainRange` stays last. It exists so a table indexed by rule - a counter per
/// rule in a report - can be sized without its author restating the enumeration.
inline constexpr std::size_t kValidityRuleCount = static_cast<std::size_t>(ValidityRule::DomainRange) + 1u;

/// Whether a value is one of the rules this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rule the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isValidityRule(ValidityRule rule) noexcept
{
    return static_cast<std::uint8_t>(rule) <= static_cast<std::uint8_t>(ValidityRule::DomainRange);
}

/// Whether this rule needs a packed mask beside the image.
///
/// The question an operation asks before it accepts an image: a rule that needs a mask and an image
/// handed over without one is an incomplete argument, not a fully valid image.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rule the rule to test.
/// @return true for `ValidityRule::ExplicitMask` alone.
[[nodiscard]] constexpr bool validityRuleNeedsMask(ValidityRule rule) noexcept
{
    return rule == ValidityRule::ExplicitMask;
}

/// Whether this rule needs a stated range.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rule the rule to test.
/// @return true for `ValidityRule::DomainRange` alone.
[[nodiscard]] constexpr bool validityRuleNeedsRange(ValidityRule rule) noexcept
{
    return rule == ValidityRule::DomainRange;
}

/// Whether this rule can be decided from one sample's value alone.
///
/// The three value rules can; the mask rule cannot, by definition, and `AllValid` needs no value at
/// all. It is worth asking because a kernel that can answer per sample needs no second buffer, and
/// one that cannot needs a mask argument it must be given.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rule the rule to test.
/// @return true for every rule but `ValidityRule::ExplicitMask`.
[[nodiscard]] constexpr bool validityRuleIsDecidedByValue(ValidityRule rule) noexcept
{
    return isValidityRule(rule) && rule != ValidityRule::ExplicitMask;
}

/// The rule's own name, for a log line, a diagnostic field or a failure message.
///
/// The spelling of the enumerator: `ValidityRule::ZeroIsNoData` is `"ZeroIsNoData"`. Stable, and
/// safe to write into a machine-read field; a caller still branches on the rule itself.
///
/// Thread-safe and allocation-free.
///
/// @param rule the rule to name. A value outside the enumeration is named `"UnknownValidityRule"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* validityRuleName(ValidityRule rule) noexcept;

/// What a caller states about which samples of an image mean something.
///
/// A plain aggregate with defaults, so a caller writes only what it is claiming. The defaults are
/// the all-valid rule and no range, which is the correct description of an image nothing has
/// invalidated.
struct ValidityDescriptorRequest
{
    /// How the image says which samples mean something.
    ValidityRule rule = ValidityRule::AllValid;

    /// The smallest value a sample may hold and still mean something. Required by
    /// `ValidityRule::DomainRange` and refused by every other rule. Must be a finite number.
    std::optional<double> lowestValid{};

    /// The largest value a sample may hold and still mean something. Required by
    /// `ValidityRule::DomainRange` and refused by every other rule. Must be a finite number and not
    /// below `lowestValid`.
    std::optional<double> highestValid{};
};

/// Which of an image's samples mean something.
///
/// There is no way to build one that contradicts itself: the constructor is private and the two
/// static entry points below are the only way in, so an object holding `DomainRange` always has an
/// ordered finite range and one holding anything else never has a range at all.
///
/// A default-constructed descriptor is the all-valid rule, which is what makes "every image carries
/// one" free rather than something every call site has to remember to state.
///
/// Trivially copyable and usable in a constant expression.
class ValidityDescriptor
{
public:
    /// The rule an image has until something invalidates part of it: every sample means something.
    constexpr ValidityDescriptor() noexcept = default;

    /// Whether a descriptor can be built from this request, and if not, why not.
    ///
    /// This is the entry point for a caller that has to report the reason - to a user, or as the
    /// context of a failure it returns. A caller that only needs the value uses `create`.
    ///
    /// The order is a contract: the rule is checked for membership of its own enumeration first, so
    /// a range rule is never applied to a value this build cannot interpret.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the rule and the range to build from.
    /// @return `DescriptorError::None` when a descriptor can be built; otherwise the first rule
    ///         that refused.
    [[nodiscard]] static constexpr DescriptorError check(const ValidityDescriptorRequest& request) noexcept
    {
        if (!isValidityRule(request.rule))
        {
            return DescriptorError::UnknownValidityRule;
        }
        if (validityRuleNeedsRange(request.rule))
        {
            if (!request.lowestValid.has_value() || !request.highestValid.has_value())
            {
                return DescriptorError::ValidityRangeMissing;
            }
            if (!isFiniteBound(*request.lowestValid) || !isFiniteBound(*request.highestValid))
            {
                return DescriptorError::ValidityRangeNotFinite;
            }
            if (*request.lowestValid > *request.highestValid)
            {
                return DescriptorError::ValidityRangeNotOrdered;
            }
            return DescriptorError::None;
        }
        if (request.lowestValid.has_value() || request.highestValid.has_value())
        {
            return DescriptorError::ValidityRangeNotAllowed;
        }
        return DescriptorError::None;
    }

    /// A descriptor for this request, where it describes one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the rule and the range to build from.
    /// @return the descriptor, or an empty optional exactly when `check` would name a reason.
    [[nodiscard]] static constexpr std::optional<ValidityDescriptor>
    create(const ValidityDescriptorRequest& request) noexcept
    {
        if (check(request) != DescriptorError::None)
        {
            return std::nullopt;
        }
        return ValidityDescriptor{request};
    }

    /// The rule every image has until something invalidates part of it.
    ///
    /// The named spelling of the default, for a call site where `ValidityDescriptor{}` would read
    /// as an unset field rather than as a claim.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @return a descriptor whose rule is `ValidityRule::AllValid`.
    [[nodiscard]] static constexpr ValidityDescriptor allValid() noexcept
    {
        return ValidityDescriptor{};
    }

    /// How the image says which samples mean something.
    ///
    /// @return the validated rule.
    [[nodiscard]] constexpr ValidityRule rule() const noexcept
    {
        return m_rule;
    }

    /// The smallest value a sample may hold and still mean something.
    ///
    /// @return the bound, or an empty optional for every rule but `ValidityRule::DomainRange`.
    [[nodiscard]] constexpr std::optional<double> lowestValid() const noexcept
    {
        return m_lowestValid;
    }

    /// The largest value a sample may hold and still mean something.
    ///
    /// @return the bound, or an empty optional for every rule but `ValidityRule::DomainRange`.
    [[nodiscard]] constexpr std::optional<double> highestValid() const noexcept
    {
        return m_highestValid;
    }

    /// Whether an image described this way needs a packed mask beside it.
    ///
    /// @return true exactly when the rule is `ValidityRule::ExplicitMask`.
    [[nodiscard]] constexpr bool needsMask() const noexcept
    {
        return validityRuleNeedsMask(m_rule);
    }

    /// Whether this description claims every sample means something.
    ///
    /// The question a kernel asks to decide whether it may skip validity entirely. `FiniteOnly`
    /// answers false even over an integer image, because the claim is about the rule the image
    /// carries and not about what this representation happens to make of it.
    ///
    /// @return true exactly when the rule is `ValidityRule::AllValid`.
    [[nodiscard]] constexpr bool isAllValid() const noexcept
    {
        return m_rule == ValidityRule::AllValid;
    }

    /// The request this descriptor was built from.
    ///
    /// The way a change is expressed: read the request, change what the operation changes, and
    /// build a new descriptor. There is no setter, for the reason the other descriptors have none.
    ///
    /// @return a request that `create` accepts and that produces an equal descriptor.
    [[nodiscard]] constexpr ValidityDescriptorRequest toRequest() const noexcept
    {
        return ValidityDescriptorRequest{.rule = m_rule, .lowestValid = m_lowestValid, .highestValid = m_highestValid};
    }

    /// Two descriptions are equal when the rule and both bounds are equal.
    ///
    /// The bounds participate as the floating values they are, so two ranges computed two different
    /// ways compare unequal. That is the intended answer: this comparison decides whether two
    /// images may be treated as claiming the same thing about their samples.
    friend constexpr bool operator==(const ValidityDescriptor& left,
                                     const ValidityDescriptor& right) noexcept = default;

private:
    /// Builds the value once the request has been checked. Private, so that every descriptor in
    /// existence has been through `check`.
    ///
    /// @param request the checked request.
    constexpr explicit ValidityDescriptor(const ValidityDescriptorRequest& request) noexcept
        : m_lowestValid(request.lowestValid), m_highestValid(request.highestValid), m_rule(request.rule)
    {
    }

    /// Whether a bound is a number a sample could be compared against, without leaving a constant
    /// expression.
    ///
    /// `std::isfinite` is not usable here, so the two comparisons do the work: not-a-number fails
    /// both because every comparison against it is false, and both infinities fail one of them.
    ///
    /// @param value the bound to test.
    /// @return true when the value is finite.
    [[nodiscard]] static constexpr bool isFiniteBound(double value) noexcept
    {
        return value >= -(std::numeric_limits<double>::max)() && value <= (std::numeric_limits<double>::max)();
    }

    /// The smallest value a sample may hold, present exactly for `ValidityRule::DomainRange`.
    std::optional<double> m_lowestValid{};

    /// The largest value a sample may hold, present exactly for `ValidityRule::DomainRange`.
    std::optional<double> m_highestValid{};

    /// How the image says which samples mean something.
    ValidityRule m_rule = ValidityRule::AllValid;
};

/// Whether one sample's own value satisfies a description's rule.
///
/// The per-sample half of the three value rules, written once so that a kernel, a mask builder and
/// a test all ask the same question. It is deliberately not the whole answer: under
/// `ValidityRule::ExplicitMask` a value says nothing at all about whether it means something, so
/// this reports true and the mask decides. A caller that must honour a mask asks the mask.
///
/// An integer sample is always finite and is compared against a range as the number it is, so the
/// same rule over an integer and a floating image is the same rule and not two.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @tparam SampleT one of the eight resident sample types, or a const-qualified one of them.
/// @param validity what the image claims about its samples.
/// @param value the sample.
/// @return true when the value alone does not make the sample meaningless.
template <ViewableSample SampleT>
[[nodiscard]] constexpr bool sampleIsValidByValue(const ValidityDescriptor& validity, SampleT value) noexcept
{
    using Bare = std::remove_const_t<SampleT>;
    switch (validity.rule())
    {
    case ValidityRule::AllValid:
    case ValidityRule::ExplicitMask:
        return true;
    case ValidityRule::FiniteOnly:
        if constexpr (std::is_floating_point_v<Bare>)
        {
            return value >= -(std::numeric_limits<Bare>::max)() && value <= (std::numeric_limits<Bare>::max)();
        }
        else
        {
            return true;
        }
    case ValidityRule::ZeroIsNoData:
        return value != static_cast<Bare>(0);
    case ValidityRule::DomainRange: {
        const double widened = static_cast<double>(value);
        const std::optional<double> lowest = validity.lowestValid();
        const std::optional<double> highest = validity.highestValid();
        if (!lowest.has_value() || !highest.has_value())
        {
            return false;
        }
        return widened >= *lowest && widened <= *highest;
    }
    }
    return false;
}

/// What an operation declares it does with its inputs' validity.
///
/// Declared rather than implied. An operation that has not decided cannot call
/// `applyValidityPolicy` without writing one of these down, which is the point: the failure this
/// prevents is an operation quietly producing an all-valid output from partly invalid input, and
/// that failure has no symptom until something downstream averages the meaningless samples.
enum class ValidityPolicy : std::uint8_t
{
    /// The output claims exactly what the input claimed. What a pointwise operation that cannot
    /// create or destroy meaninglessness does - a scale, a bias, a channel permutation.
    Preserve,

    /// The output claims what all the inputs claim together. What a two-input operation does, and
    /// the only policy whose answer can be a rule none of its inputs had: two inputs that disagree
    /// combine to a mask, because a mask is the only description that can express the union of two
    /// different rules sample by sample.
    Combine,

    /// The output's validity is decided by the operation, which has just worked it out. What
    /// registration does when it resamples into a canvas whose footprint it knows, and what
    /// calibration does when it has just produced the non-finite values.
    Regenerate,

    /// The operation has no meaning on incomplete data and refuses to run on any. A legitimate
    /// answer, and a better one than an average over samples that mean nothing.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Reject
};

/// Whether a value is one of the policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isValidityPolicy(ValidityPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(ValidityPolicy::Reject);
}

/// The policy's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param policy the policy to name. A value outside the enumeration is named
///        `"UnknownValidityPolicy"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* validityPolicyName(ValidityPolicy policy) noexcept;

/// What one operation declares about its output's validity.
///
/// A plain aggregate, because the second member is meaningful under exactly one policy and a
/// constructor that took both would have to refuse three quarters of its calls. An operation states
/// this once, beside its parameters, rather than deciding at each call site.
struct ValidityTransition
{
    /// What the operation does with its inputs' validity.
    ValidityPolicy policy = ValidityPolicy::Preserve;

    /// What the output claims, under `ValidityPolicy::Regenerate`. Ignored by every other policy,
    /// which derive their answer from the inputs.
    ValidityDescriptor produced{};

    /// Whether two declarations say the same thing.
    ///
    /// The stated rule participates whatever the policy is, so two declarations that differ only in
    /// a field one of them ignores compare unequal. That is deliberate: a declaration carrying a
    /// rule no policy reads is one an operation could not have made, and a comparison that hid the
    /// difference would make it harder to find rather than impossible to make.
    ///
    /// @param left one declaration.
    /// @param right the other.
    /// @return true when the policy and the stated rule are both equal.
    [[nodiscard]] friend constexpr bool operator==(const ValidityTransition& left,
                                                   const ValidityTransition& right) noexcept = default;
};

/// The validity an operation's output carries, given what it declared and what its inputs claim.
///
/// One function rather than a rule per operation, so that "what happens to validity here" has the
/// same answer everywhere and a new operation inherits it by declaring a policy.
///
/// The policies answer differently and each refusal is the caller's to act on:
///
/// * `Preserve` needs every input to claim the same thing, and refuses when they disagree -
///   preserving two different rules is not a thing that can be done.
/// * `Combine` answers with the inputs' shared claim when they agree, and with
///   `ValidityRule::ExplicitMask` when they do not, because a mask is the only description that can
///   carry the combination sample by sample. The mask itself is the operation's to materialise.
/// * `Regenerate` answers with what the transition states and ignores the inputs.
/// * `Reject` refuses unless every input is all-valid, and answers all-valid when they are.
///
/// Thread-safe. Allocates only a failure it has to describe.
///
/// @param transition what the operation declared.
/// @param inputs what each input claims, in the operation's own argument order. Never empty except
///        under `ValidityPolicy::Regenerate`, which needs no input to answer.
/// @return the output's validity; `ErrorCode::InvalidArgument` for a policy outside the
///         enumeration or for an empty input list under a policy that needs one; or
///         `ErrorCode::IncompatibleArguments` when `Preserve` is given inputs that disagree or
///         `Reject` is given an input that is not all-valid.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ValidityDescriptor>
applyValidityPolicy(const ValidityTransition& transition, std::span<const ValidityDescriptor> inputs);

/// How many samples a selection holds, and how many it was taken from.
///
/// The value a reduction reports beside its answer, and the value that makes an empty selection a
/// fact rather than an absence. Both numbers are counted rather than estimated: a caller deciding
/// whether to keep a frame wants to know that four samples of nine million survived, and "not many"
/// is not a decision it can act on.
struct ValidityCount
{
    /// How many samples mean something.
    std::uint64_t valid = 0u;

    /// How many samples were considered, meaningful or not.
    std::uint64_t total = 0u;

    /// Whether nothing at all was selected.
    ///
    /// Allocation-free and usable in a constant expression.
    ///
    /// @return true when no sample means anything.
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return valid == 0u;
    }

    /// Whether every sample considered means something.
    ///
    /// Allocation-free and usable in a constant expression.
    ///
    /// @return true when the two counts agree. A selection over nothing at all reports true, and
    ///         `empty` is what tells the two apart.
    [[nodiscard]] constexpr bool complete() const noexcept
    {
        return valid == total;
    }

    /// Two counts are equal when both numbers are equal.
    friend constexpr bool operator==(const ValidityCount& left, const ValidityCount& right) noexcept = default;
};

/// Whether a selection holds anything for an operation to work on.
///
/// The one place the empty case becomes a failure, so that every reduction in this engine reports it
/// with the same code and a caller needs one branch rather than one per operation. A mean of no
/// samples is not zero and is not a NaN that happened to fall out of a division; it is an outcome,
/// and this is the outcome.
///
/// Thread-safe. Allocates only the failure it describes.
///
/// @param count how many samples mean something, and how many were considered.
/// @param operation the engine operation to name in the failure; null names none.
/// @return success when at least one sample means something; otherwise
///         `ErrorCode::EmptySelection`, carrying both counts as context.
[[nodiscard]] AFTERNIGHTENGINE_API Status requireSelection(const ValidityCount& count, const char* operation);

} // namespace an
