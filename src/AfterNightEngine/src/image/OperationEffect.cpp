// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/OperationEffect.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <span>
#include <string>

// The names of an operation's declared effect, and the one call that applies a declared validity
// policy.
//
// The whole of the reasoning behind this file is in the header. What is worth saying beside the
// code is why `operationValidity` exists at all when `applyValidityPolicy` is right there and does
// the work: the difference is which object supplies the policy. `applyValidityPolicy` takes a
// transition its caller constructed, which is the correct shape for the machinery and the wrong
// shape for a call site, because a caller free to choose the policy has taken a decision that
// belongs to the operation. This entry point takes the operation's published constant and forwards
// the transition inside it, so the only way to get a different answer is to change the operation's
// declaration - where it is read by that operation's own test.

namespace an
{
namespace
{

/// The failure reported for a declaration that cannot describe an operation.
///
/// @param error the reason `checkOperationEffect` gave.
/// @return `ErrorCode::InvalidArgument`, naming the reason in its message and `effect` as the
///         parameter at fault.
[[nodiscard]] Status inconsistentDeclaration(OperationEffectError error)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = std::string("the operation's declared effect is inconsistent: ") +
                                                   operationEffectErrorName(error),
                                        .operation = "an::operationValidity",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, "effect")}});
}

} // namespace

const char* sampleEffectName(SampleEffect effect) noexcept
{
    switch (effect)
    {
    case SampleEffect::Reads:
        return "Reads";
    case SampleEffect::Writes:
        return "Writes";
    }
    return "UnknownSampleEffect";
}

const char* descriptionEffectName(DescriptionEffect effect) noexcept
{
    switch (effect)
    {
    case DescriptionEffect::Preserves:
        return "Preserves";
    case DescriptionEffect::Restates:
        return "Restates";
    }
    return "UnknownDescriptionEffect";
}

const char* semanticEffectName(SemanticEffect effect) noexcept
{
    switch (effect)
    {
    case SemanticEffect::Preserves:
        return "Preserves";
    case SemanticEffect::Restates:
        return "Restates";
    }
    return "UnknownSemanticEffect";
}

const char* solutionEffectName(SolutionEffect effect) noexcept
{
    switch (effect)
    {
    case SolutionEffect::Preserves:
        return "Preserves";
    case SolutionEffect::Invalidates:
        return "Invalidates";
    }
    return "UnknownSolutionEffect";
}

const char* codecMetadataEffectName(CodecMetadataEffect effect) noexcept
{
    switch (effect)
    {
    case CodecMetadataEffect::Preserves:
        return "Preserves";
    case CodecMetadataEffect::Drops:
        return "Drops";
    }
    return "UnknownCodecMetadataEffect";
}

const char* outputIntentEffectName(OutputIntentEffect effect) noexcept
{
    switch (effect)
    {
    case OutputIntentEffect::Preserves:
        return "Preserves";
    case OutputIntentEffect::Withdraws:
        return "Withdraws";
    }
    return "UnknownOutputIntentEffect";
}

const char* operationEffectErrorName(OperationEffectError error) noexcept
{
    switch (error)
    {
    case OperationEffectError::None:
        return "None";
    case OperationEffectError::UnknownEffect:
        return "UnknownEffect";
    case OperationEffectError::UnknownValidityPolicy:
        return "UnknownValidityPolicy";
    case OperationEffectError::ReadingOperationChangesResult:
        return "ReadingOperationChangesResult";
    case OperationEffectError::ReadingOperationRegeneratesValidity:
        return "ReadingOperationRegeneratesValidity";
    case OperationEffectError::ProducedValidityWithoutRegenerate:
        return "ProducedValidityWithoutRegenerate";
    }
    return "UnknownOperationEffectError";
}

Result<ValidityDescriptor> operationValidity(const OperationEffect& effect, std::span<const ValidityDescriptor> inputs)
{
    const OperationEffectError declaration = checkOperationEffect(effect);
    if (declaration != OperationEffectError::None)
    {
        return inconsistentDeclaration(declaration);
    }
    return applyValidityPolicy(effect.validity, inputs);
}

} // namespace an
