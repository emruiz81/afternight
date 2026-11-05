// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>

// The rules a validity policy is honoured by, in one place.
//
// Each policy answers a different question and each of them can fail for a reason the caller acts
// on differently, which is why they are four values rather than a boolean and a comment. The whole
// of the reasoning is that an operation that has not decided what it does with validity cannot get
// past this function without writing one of them down.

namespace an
{
namespace
{

/// Whether every description in a list says the same thing.
///
/// @param inputs the descriptions to compare. An empty list agrees with itself.
/// @return true when the list holds fewer than two distinct descriptions.
[[nodiscard]] bool allAgree(std::span<const ValidityDescriptor> inputs) noexcept
{
    for (std::size_t index = 1u; index < inputs.size(); ++index)
    {
        if (!(inputs[index] == inputs[0]))
        {
            return false;
        }
    }
    return true;
}

/// The failure a policy that needs inputs reports when it is given none.
///
/// @param policy the policy that was declared.
/// @return `ErrorCode::InvalidArgument`, naming the policy.
[[nodiscard]] Status noInputs(ValidityPolicy policy)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = std::string("a ") + validityPolicyName(policy) +
                                                   " policy derives its answer from its inputs, and none were given",
                                        .operation = "an::applyValidityPolicy",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, "inputs")}});
}

} // namespace

const char* validityRuleName(ValidityRule rule) noexcept
{
    switch (rule)
    {
    case ValidityRule::AllValid:
        return "AllValid";
    case ValidityRule::FiniteOnly:
        return "FiniteOnly";
    case ValidityRule::ZeroIsNoData:
        return "ZeroIsNoData";
    case ValidityRule::ExplicitMask:
        return "ExplicitMask";
    case ValidityRule::DomainRange:
        return "DomainRange";
    }
    return "UnknownValidityRule";
}

const char* validityPolicyName(ValidityPolicy policy) noexcept
{
    switch (policy)
    {
    case ValidityPolicy::Preserve:
        return "Preserve";
    case ValidityPolicy::Combine:
        return "Combine";
    case ValidityPolicy::Regenerate:
        return "Regenerate";
    case ValidityPolicy::Reject:
        return "Reject";
    }
    return "UnknownValidityPolicy";
}

Result<ValidityDescriptor> applyValidityPolicy(const ValidityTransition& transition,
                                               std::span<const ValidityDescriptor> inputs)
{
    if (!isValidityPolicy(transition.policy))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the validity policy is not one this engine knows",
                         .operation = "an::applyValidityPolicy",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "transition.policy")}});
    }

    switch (transition.policy)
    {
    case ValidityPolicy::Preserve:
        if (inputs.empty())
        {
            return noInputs(transition.policy);
        }
        if (!allAgree(inputs))
        {
            // Preserving two different claims is not a thing that can be done, and picking one of
            // them would be this function deciding on the operation's behalf. An operation whose
            // inputs really may disagree declares Combine.
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                StatusDetail{.message = "a preserving operation was given inputs that claim different things about "
                                        "their samples; there is no one claim to preserve",
                             .operation = "an::applyValidityPolicy",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofCount(ContextKey::Total, inputs.size())}});
        }
        return inputs[0];

    case ValidityPolicy::Combine: {
        if (inputs.empty())
        {
            return noInputs(transition.policy);
        }
        if (allAgree(inputs))
        {
            return inputs[0];
        }
        // Two inputs claiming different things have no shared rule, and the union of two rules is
        // a per-sample fact. A mask is the only description that carries one, so the answer is the
        // mask rule and materialising the mask is the operation's own work.
        const std::optional<ValidityDescriptor> masked =
            ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::ExplicitMask});
        if (!masked.has_value())
        {
            return Status::failure(ErrorCode::Internal,
                                   StatusDetail{.message = "the mask rule did not build, which it always does",
                                                .operation = "an::applyValidityPolicy",
                                                .domain = ErrorDomain::Image});
        }
        return *masked;
    }

    case ValidityPolicy::Regenerate:
        // The operation has just worked out what its output claims, so nothing about the inputs is
        // consulted at all - including whether there are any.
        return transition.produced;

    case ValidityPolicy::Reject:
        if (inputs.empty())
        {
            return noInputs(transition.policy);
        }
        for (std::size_t index = 0u; index < inputs.size(); ++index)
        {
            if (!inputs[index].isAllValid())
            {
                return Status::failure(
                    ErrorCode::IncompatibleArguments,
                    StatusDetail{.message = std::string("this operation has no meaning on incomplete data, and an "
                                                        "input claims ") +
                                            validityRuleName(inputs[index].rule()),
                                 .operation = "an::applyValidityPolicy",
                                 .domain = ErrorDomain::Image,
                                 .context = {StatusContextField::ofText(ContextKey::Parameter, "inputs"),
                                             StatusContextField::ofCount(ContextKey::Completed, index)}});
            }
        }
        return ValidityDescriptor::allValid();
    }

    return Status::failure(ErrorCode::Internal, StatusDetail{.message = "the validity policy fell through every case",
                                                             .operation = "an::applyValidityPolicy",
                                                             .domain = ErrorDomain::Image});
}

Status requireSelection(const ValidityCount& count, const char* operation)
{
    if (!count.empty())
    {
        return Status::success();
    }
    return Status::failure(
        ErrorCode::EmptySelection,
        StatusDetail{.message = "no sample of this selection means anything, so there is nothing to reduce",
                     .operation = operation != nullptr ? operation : "",
                     .domain = ErrorDomain::Image,
                     .context = {StatusContextField::ofCount(ContextKey::Completed, count.valid),
                                 StatusContextField::ofCount(ContextKey::Total, count.total)}});
}

} // namespace an
