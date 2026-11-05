// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - binding one frame's mapping to a checked drizzle model. The only refusals
// are a post-translation that is not finite and a source SIP with no forward model, since a
// source-to-destination map evaluates the forward direction.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cmath>

namespace an
{

Result<DrizzleMapDescriptor> DrizzleMapDescriptor::create(const DrizzleModel& model,
                                                          const DrizzleMappingRequest& request)
{
    if (!std::isfinite(request.postTranslation.x) || !std::isfinite(request.postTranslation.y))
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "post-translation must be finite in reference pixels",
                         .operation = "an::DrizzleMapDescriptor::create",
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "postTranslation")}});
    if (request.sourceDistortion.has_value() && request.sourceDistortion->hasInverseModel() &&
        !request.sourceDistortion->hasForwardModel())
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "a source-to-destination mapping needs the source SIP forward model",
                         .operation = "an::DrizzleMapDescriptor::create",
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "sourceDistortion")}});
    return DrizzleMapDescriptor{model, request};
}

} // namespace an
