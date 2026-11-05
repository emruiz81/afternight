// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ImageSemantics.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <limits>
#include <optional>
#include <string>

// The exponent rule is the only decision here, and it goes both ways on purpose.
//
// The validity description beside it needs no rule of its own: it was checked when it was built and
// there is nothing here it can contradict. It is carried rather than validated, which is what a
// value type that cannot be built wrong buys.
//
// A power law with no exponent cannot be applied or removed, so it is refused. An exponent stated
// beside sRGB is refused too, which is the half that is easy to leave out: sRGB is not a power law,
// so an exponent carried beside it is a number that will one day be read by something that assumes
// it means what it would mean beside `PureGamma`. Refusing it is how the object keeps its promise
// that a stated exponent is always the exponent in force.

namespace an
{

const char* transferFunctionName(TransferFunction transfer) noexcept
{
    switch (transfer)
    {
    case TransferFunction::Unspecified:
        return "Unspecified";
    case TransferFunction::Linear:
        return "Linear";
    case TransferFunction::Srgb:
        return "Srgb";
    case TransferFunction::Rec709:
        return "Rec709";
    case TransferFunction::PureGamma:
        return "PureGamma";
    }
    return "UnknownTransferFunction";
}

Result<ImageSemantics> ImageSemantics::create(const ImageSemanticsRequest& request)
{
    if (!isTransferFunction(request.transferFunction))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the transfer function is not one this engine knows",
                         .operation = "ImageSemantics::create",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "transferFunction")}});
    }
    if (request.transferFunction == TransferFunction::PureGamma)
    {
        if (!request.gammaExponent.has_value())
        {
            return Status::failure(
                ErrorCode::InvalidArgument,
                StatusDetail{.message = "a power law needs its exponent, or nothing can apply or remove it",
                             .operation = "ImageSemantics::create",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "gammaExponent")}});
        }
        if (!(*request.gammaExponent > 0.0 && *request.gammaExponent < std::numeric_limits<double>::infinity()))
        {
            return Status::failure(
                ErrorCode::InvalidArgument,
                StatusDetail{.message = "a gamma exponent is a positive finite number",
                             .operation = "ImageSemantics::create",
                             .domain = ErrorDomain::Image,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "gammaExponent")}});
        }
    }
    else if (request.gammaExponent.has_value())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = std::string("an exponent was stated beside ") +
                                    transferFunctionName(request.transferFunction) +
                                    ", which is not a power law; a number carried there would be read as one",
                         .operation = "ImageSemantics::create",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "gammaExponent")}});
    }
    return ImageSemantics{request};
}

TransferFunction ImageSemantics::transferFunction() const noexcept
{
    return m_transferFunction;
}

std::optional<double> ImageSemantics::gammaExponent() const noexcept
{
    return m_gammaExponent;
}

const ProcessingState& ImageSemantics::processingState() const noexcept
{
    return m_processingState;
}

const ValidityDescriptor& ImageSemantics::validity() const noexcept
{
    return m_validity;
}

bool ImageSemantics::isLinear() const noexcept
{
    return m_transferFunction == TransferFunction::Linear;
}

ImageSemanticsRequest ImageSemantics::toRequest() const noexcept
{
    return ImageSemanticsRequest{.transferFunction = m_transferFunction,
                                 .gammaExponent = m_gammaExponent,
                                 .processingState = m_processingState,
                                 .validity = m_validity};
}

} // namespace an
