// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/OperationTransition.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <string>
#include <utility>

// Building the transition an operation's result takes.
//
// Everything here is a translation from two records into a third, and the only judgement in the
// file is that the lineage entry is unconditional. An operation that changed nothing still ran, and
// a caller that wants no record of it can simply not call this - whereas an operation that changed
// something and recorded nothing leaves an image whose history has a hole in it, which nothing
// downstream can detect.
//
// The astrometric case reads oddly on purpose. `SolutionEffect::Invalidates` states a
// *default-constructed* solution rather than clearing a field, because that is how
// `MetadataTransition` says "the old one no longer applies": an absent optional means the operation
// said nothing about the solution, so an empty one has to be stated explicitly.

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
                                        .operation = "an::operationTransition",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, "effect")}});
}

/// The failure reported when a call site reports something its operation did not declare.
///
/// @param error the reason `checkOperationOutcome` gave.
/// @return `ErrorCode::InvalidArgument`, naming the reason in its message and `outcome` as the
///         parameter at fault.
[[nodiscard]] Status outcomeDisagrees(OperationOutcomeError error)
{
    return Status::failure(
        ErrorCode::InvalidArgument,
        StatusDetail{.message = std::string("what the operation reported disagrees with what it declared: ") +
                                operationOutcomeErrorName(error),
                     .operation = "an::operationTransition",
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofText(ContextKey::Parameter, "outcome")}});
}

} // namespace

const char* operationOutcomeErrorName(OperationOutcomeError error) noexcept
{
    switch (error)
    {
    case OperationOutcomeError::None:
        return "None";
    case OperationOutcomeError::MissingSemantics:
        return "MissingSemantics";
    case OperationOutcomeError::UnexpectedSemantics:
        return "UnexpectedSemantics";
    }
    return "UnknownOperationOutcomeError";
}

OperationOutcomeError checkOperationOutcome(const OperationEffect& effect, const OperationOutcome& outcome) noexcept
{
    if (effect.semantics == SemanticEffect::Restates && !outcome.semantics.has_value())
    {
        return OperationOutcomeError::MissingSemantics;
    }
    if (effect.semantics != SemanticEffect::Restates && outcome.semantics.has_value())
    {
        return OperationOutcomeError::UnexpectedSemantics;
    }
    return OperationOutcomeError::None;
}

Result<MetadataTransition> operationTransition(const OperationEffect& effect, const OperationOutcome& outcome)
{
    const OperationEffectError declaration = checkOperationEffect(effect);
    if (declaration != OperationEffectError::None)
    {
        return inconsistentDeclaration(declaration);
    }
    const OperationOutcomeError disagreement = checkOperationOutcome(effect, outcome);
    if (disagreement != OperationOutcomeError::None)
    {
        return outcomeDisagrees(disagreement);
    }

    Result<ProvenanceEntry> entry = ProvenanceEntry::create(outcome.operation, outcome.detail);
    if (!entry.hasValue())
    {
        return entry.status();
    }

    MetadataTransition change;
    change.record = std::move(*entry);
    if (effect.semantics == SemanticEffect::Restates)
    {
        change.semantics = outcome.semantics;
    }
    if (effect.solution == SolutionEffect::Invalidates)
    {
        change.astrometry = AstrometricSolution{};
    }
    change.clearCodecMetadata = effect.codecMetadata == CodecMetadataEffect::Drops;
    change.clearOutputIntent = effect.outputIntent == OutputIntentEffect::Withdraws;
    return change;
}

} // namespace an
