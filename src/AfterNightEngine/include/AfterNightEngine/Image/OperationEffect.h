// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an operation declares it does to an image's meaning and validity.
///
/// Every operation in this engine takes views and descriptors and gives back samples and counts.
/// None of that says what the *result* is: whether it still means what its input meant, whether the
/// solution the input carried still describes it, whether the header cards that came with the file
/// still apply, or which of its samples mean anything. Those are five separate facts, an operation
/// knows all five before it runs a single pixel, and until they are written down each one is
/// rediscovered by whoever wired the operation into a chain.
///
/// This header is where they are written down. An operation publishes one `OperationEffect` beside
/// its entry point, an orchestrator reads it, and `Image/OperationTransition.h` turns it into the
/// `MetadataTransition` the result's image is given. The failure this prevents is specific and has
/// no symptom: a stage that changes what samples mean while its description says otherwise produces
/// an image that every later stage reads confidently and wrongly.
///
/// **The vocabulary is here and the transition is next door.** A `MetadataTransition` carries six
/// metadata objects and their strings and vectors; an operation header that declared its effect
/// would inherit all of that. So the declaration - which every operation header carries - is this
/// header, and the builder - which only an orchestrator calls - is `Image/OperationTransition.h`.
/// It is the same split, for the same reason, as an image's storage naming its engine context
/// without including it.
///
/// **The validity half is not re-invented here.** `Image/ValidityDescriptor.h` already owns the four
/// policies and the function that applies them, so an effect carries a `ValidityTransition` rather
/// than a fifth spelling of the same idea, and `operationValidity` is the one call that applies an
/// operation's *declared* policy rather than one the call site chose.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cstdint>
#include <span>

namespace an
{

/// Whether an operation writes the samples it is given, or only reads them.
///
/// The distinction an image's pixel generation is moved by, and the one thing on this record that
/// is about the samples rather than about what they mean.
enum class SampleEffect : std::uint8_t
{
    /// The operation produces a value from samples and writes none of them. Every reduction:
    /// statistics, histograms, mask counts.
    Reads,

    /// The operation writes its destination's samples. Every transform, whether or not the
    /// destination aliases the source.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Writes
};

/// What an operation does to the description of the image it produces.
///
/// A description is the extent, the representation, the channel schema, the colour basis, the
/// numeric domain, the filter array, the origin and the pixel aspect. An operation either leaves
/// every one of them alone or states a new description of its own.
enum class DescriptionEffect : std::uint8_t
{
    /// The result is described exactly as the input was.
    Preserves,

    /// The operation states the result's description. A caller takes it from what that operation
    /// planned - `colorTransformedDescriptor` of a `ColorConversionPlan` for a colour conversion -
    /// rather than assuming one.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Restates
};

/// What an operation does to the meaning its result carries.
///
/// The meaning is `ImageSemantics`: which transfer function the magnitudes have been through, which
/// pipeline steps they have been through, and which of them mean anything. One of the two objects
/// whose change moves an image's semantic generation.
enum class SemanticEffect : std::uint8_t
{
    /// The result means what the input meant. What arithmetic on already-calibrated samples does.
    Preserves,

    /// The operation states what the result means, and its outcome must carry it. What subtracting
    /// a dark frame, applying a transfer function or normalizing to a reference does.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Restates
};

/// What an operation does to an astrometric solution its input carried.
///
/// The second of the two objects whose change moves an image's semantic generation. No operation in
/// the core surface produces a solution, so there is no `Restates`: a solver states its own
/// transition rather than declaring one of these.
enum class SolutionEffect : std::uint8_t
{
    /// The solution still maps the result's pixels to the sky. True of everything that leaves the
    /// geometry alone, which is every core operation that is not a colour schema change.
    Preserves,

    /// The solution no longer describes the result, and the transition withdraws it. What a
    /// geometric change does, and what a channel count change does to a solution that named one.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Invalidates
};

/// What an operation does to the source header cards the image was decoded with.
///
/// Preserved cards describe the file the samples came from. They are carried so an output can
/// reproduce them, and they are dropped when the result is no longer the frame they describe.
enum class CodecMetadataEffect : std::uint8_t
{
    /// The cards still describe the result. True of anything that changes sample values without
    /// changing what the frame is.
    Preserves,

    /// The cards no longer describe the result, and the transition drops them. What a combination
    /// of many frames does, and what a change of representation or channel schema does to cards
    /// that stated the old one.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Drops
};

/// What an operation does to a decision about how the image is to be written.
///
/// An output intent is absent until something decides, so no operation in this engine states one.
/// An operation only ever leaves the decision alone or says it no longer describes the result.
enum class OutputIntentEffect : std::uint8_t
{
    /// The decision still describes the result.
    Preserves,

    /// The decision no longer describes the result, and the transition withdraws it. What a change
    /// of representation or channel count does to an intent that named the old one.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Withdraws
};

/// One operation's declared effect on the image it produces.
///
/// A plain aggregate of six independent decisions, stated once beside the operation's entry point
/// rather than at each call site. Every default is the conservative one: an operation that writes
/// samples and changes nothing else needs to state nothing.
///
/// The record is about the *result*, not about the call. Whether a destination aliases its source,
/// how many threads ran, and which backend was selected are all invisible here, because none of
/// them changes what the result is.
struct OperationEffect
{
    /// Whether the operation writes the samples of its destination, or only reads them.
    SampleEffect samples = SampleEffect::Writes;

    /// Whether the result is described as its input was, or by a description the operation states.
    DescriptionEffect description = DescriptionEffect::Preserves;

    /// Whether the result means what its input meant, or something the operation states.
    SemanticEffect semantics = SemanticEffect::Preserves;

    /// Whether an astrometric solution the input carried still describes the result.
    SolutionEffect solution = SolutionEffect::Preserves;

    /// Whether the preserved source header cards still describe the result.
    CodecMetadataEffect codecMetadata = CodecMetadataEffect::Preserves;

    /// Whether a decision about how the image is written still describes the result.
    OutputIntentEffect outputIntent = OutputIntentEffect::Preserves;

    /// What the operation does with its inputs' validity, and what it produces when it decides.
    ValidityTransition validity{};

    /// Whether two declarations state the same effect.
    ///
    /// @param left one declaration.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const OperationEffect& left,
                                                   const OperationEffect& right) noexcept = default;
};

/// Why a declared effect cannot describe an operation.
///
/// Each reason is a combination that says two things at once and cannot mean both. A declaration is
/// a compile-time constant in every operation header, so these are caught by the test that reads
/// the declaration rather than by a caller.
enum class OperationEffectError : std::uint8_t
{
    /// The declaration is consistent.
    None,

    /// One of the six decisions holds a value this build does not know.
    UnknownEffect,

    /// The declared validity policy is not one of the four.
    UnknownValidityPolicy,

    /// An operation that only reads samples claims to change the result's description, meaning,
    /// solution, cards or output decision. A reduction produces a number, and a number has no
    /// metadata to change.
    ReadingOperationChangesResult,

    /// An operation that only reads samples declares a validity policy other than `Reject` or
    /// `Preserve`. A reduction narrows a selection; it produces no image whose validity could be
    /// combined or regenerated.
    ReadingOperationRegeneratesValidity,

    /// A validity rule is stated under a policy that derives its answer from the inputs. Only
    /// `ValidityPolicy::Regenerate` reads what the transition produces.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ProducedValidityWithoutRegenerate
};

/// Whether a value is one of the sample effects this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSampleEffect(SampleEffect effect) noexcept
{
    return static_cast<std::uint8_t>(effect) <= static_cast<std::uint8_t>(SampleEffect::Writes);
}

/// Whether a value is one of the description effects this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isDescriptionEffect(DescriptionEffect effect) noexcept
{
    return static_cast<std::uint8_t>(effect) <= static_cast<std::uint8_t>(DescriptionEffect::Restates);
}

/// Whether a value is one of the semantic effects this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSemanticEffect(SemanticEffect effect) noexcept
{
    return static_cast<std::uint8_t>(effect) <= static_cast<std::uint8_t>(SemanticEffect::Restates);
}

/// Whether a value is one of the solution effects this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSolutionEffect(SolutionEffect effect) noexcept
{
    return static_cast<std::uint8_t>(effect) <= static_cast<std::uint8_t>(SolutionEffect::Invalidates);
}

/// Whether a value is one of the codec-metadata effects this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isCodecMetadataEffect(CodecMetadataEffect effect) noexcept
{
    return static_cast<std::uint8_t>(effect) <= static_cast<std::uint8_t>(CodecMetadataEffect::Drops);
}

/// Whether a value is one of the output-intent effects this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputIntentEffect(OutputIntentEffect effect) noexcept
{
    return static_cast<std::uint8_t>(effect) <= static_cast<std::uint8_t>(OutputIntentEffect::Withdraws);
}

/// Whether a declared effect describes something an operation can actually do.
///
/// The check every operation's own test runs over its published declaration, and the check
/// `operationTransition` runs before it builds anything. It reads the declaration alone: nothing
/// here depends on an image, a call or a context.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param effect the declaration to check.
/// @return `OperationEffectError::None`, or the first reason the combination cannot mean what it
///         says.
[[nodiscard]] constexpr OperationEffectError checkOperationEffect(const OperationEffect& effect) noexcept
{
    if (!isSampleEffect(effect.samples) || !isDescriptionEffect(effect.description) ||
        !isSemanticEffect(effect.semantics) || !isSolutionEffect(effect.solution) ||
        !isCodecMetadataEffect(effect.codecMetadata) || !isOutputIntentEffect(effect.outputIntent))
    {
        return OperationEffectError::UnknownEffect;
    }
    if (!isValidityPolicy(effect.validity.policy))
    {
        return OperationEffectError::UnknownValidityPolicy;
    }
    if (effect.samples == SampleEffect::Reads)
    {
        if (effect.description != DescriptionEffect::Preserves || effect.semantics != SemanticEffect::Preserves ||
            effect.solution != SolutionEffect::Preserves || effect.codecMetadata != CodecMetadataEffect::Preserves ||
            effect.outputIntent != OutputIntentEffect::Preserves)
        {
            return OperationEffectError::ReadingOperationChangesResult;
        }
        if (effect.validity.policy != ValidityPolicy::Preserve && effect.validity.policy != ValidityPolicy::Reject)
        {
            return OperationEffectError::ReadingOperationRegeneratesValidity;
        }
    }
    if (effect.validity.policy != ValidityPolicy::Regenerate && effect.validity.produced != ValidityDescriptor{})
    {
        return OperationEffectError::ProducedValidityWithoutRegenerate;
    }
    return OperationEffectError::None;
}

/// The sample effect's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param effect the value to name. A value outside the enumeration is named `"UnknownSampleEffect"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* sampleEffectName(SampleEffect effect) noexcept;

/// The description effect's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param effect the value to name. A value outside the enumeration is named
///        `"UnknownDescriptionEffect"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* descriptionEffectName(DescriptionEffect effect) noexcept;

/// The semantic effect's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param effect the value to name. A value outside the enumeration is named
///        `"UnknownSemanticEffect"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* semanticEffectName(SemanticEffect effect) noexcept;

/// The solution effect's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param effect the value to name. A value outside the enumeration is named
///        `"UnknownSolutionEffect"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* solutionEffectName(SolutionEffect effect) noexcept;

/// The codec-metadata effect's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param effect the value to name. A value outside the enumeration is named
///        `"UnknownCodecMetadataEffect"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* codecMetadataEffectName(CodecMetadataEffect effect) noexcept;

/// The output-intent effect's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param effect the value to name. A value outside the enumeration is named
///        `"UnknownOutputIntentEffect"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* outputIntentEffectName(OutputIntentEffect effect) noexcept;

/// The refusal's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownOperationEffectError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* operationEffectErrorName(OperationEffectError error) noexcept;

/// The validity an operation's result carries, under the policy that operation declared.
///
/// The one call that applies a *declared* policy. `applyValidityPolicy` takes whichever transition
/// its caller hands it, which is correct for the machinery and wrong for a call site: an
/// orchestrator that picks the policy has taken a decision that belongs to the operation. This
/// takes the operation's own declaration and cannot be given a different one.
///
/// Thread-safe. Allocates only a failure it has to describe.
///
/// @param effect the operation's published declaration.
/// @param inputs what each input image claims about its own samples, in the order the operation
///        takes them. Never empty except under `ValidityPolicy::Regenerate`, which needs no input
///        to answer.
/// @return the result's validity, or `ErrorCode::InvalidArgument` when the declaration is
///         inconsistent - naming the `OperationEffectError` in its message and `effect` as the
///         parameter at fault - or whatever `applyValidityPolicy` reports for these inputs under
///         that policy.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ValidityDescriptor>
operationValidity(const OperationEffect& effect, std::span<const ValidityDescriptor> inputs);

} // namespace an
