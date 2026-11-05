// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ConversionPolicy.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

// The five decisions are checked one at a time and the first bad one is named.
//
// A policy has no combination rules - every field is independent of the other four - so the only
// thing that can be wrong with one is a value from a build that knows something this one does not.
// Naming the field rather than reporting "invalid policy" is what makes the failure actionable at
// the call site that filled it in.

namespace an
{
namespace
{

/// The failure a field outside its own enumeration produces.
///
/// @param field the member's name, as a call site spells it.
/// @param operation the engine operation to name.
/// @return `ErrorCode::InvalidArgument`, naming the field.
[[nodiscard]] Status unknownField(const char* field, const char* operation)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = "this conversion decision is not one this engine knows",
                                        .operation = operation != nullptr ? operation : "",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, field)}});
}

} // namespace

const char* roundingModeName(RoundingMode rounding) noexcept
{
    switch (rounding)
    {
    case RoundingMode::NearestEven:
        return "NearestEven";
    case RoundingMode::NearestAwayFromZero:
        return "NearestAwayFromZero";
    case RoundingMode::TowardZero:
        return "TowardZero";
    case RoundingMode::Floor:
        return "Floor";
    case RoundingMode::Ceiling:
        return "Ceiling";
    }
    return "UnknownRoundingMode";
}

const char* rangePolicyName(RangePolicy range) noexcept
{
    switch (range)
    {
    case RangePolicy::Saturate:
        return "Saturate";
    case RangePolicy::Reject:
        return "Reject";
    }
    return "UnknownRangePolicy";
}

const char* nonFinitePolicyName(NonFinitePolicy nonFinite) noexcept
{
    switch (nonFinite)
    {
    case NonFinitePolicy::MapToRange:
        return "MapToRange";
    case NonFinitePolicy::Reject:
        return "Reject";
    }
    return "UnknownNonFinitePolicy";
}

const char* domainMappingName(DomainMapping domain) noexcept
{
    switch (domain)
    {
    case DomainMapping::FullScale:
        return "FullScale";
    case DomainMapping::CodeValue:
        return "CodeValue";
    }
    return "UnknownDomainMapping";
}

const char* signedIntegerConventionName(SignedIntegerConvention convention) noexcept
{
    switch (convention)
    {
    case SignedIntegerConvention::TwosComplement:
        return "TwosComplement";
    case SignedIntegerConvention::OffsetBinary:
        return "OffsetBinary";
    }
    return "UnknownSignedIntegerConvention";
}

Status checkConversionPolicy(const ConversionPolicy& policy, const char* operation)
{
    if (!isRoundingMode(policy.rounding))
    {
        return unknownField("policy.rounding", operation);
    }
    if (!isRangePolicy(policy.range))
    {
        return unknownField("policy.range", operation);
    }
    if (!isNonFinitePolicy(policy.nonFinite))
    {
        return unknownField("policy.nonFinite", operation);
    }
    if (!isDomainMapping(policy.domain))
    {
        return unknownField("policy.domain", operation);
    }
    if (!isSignedIntegerConvention(policy.signedConvention))
    {
        return unknownField("policy.signedConvention", operation);
    }
    return Status::success();
}

} // namespace an
