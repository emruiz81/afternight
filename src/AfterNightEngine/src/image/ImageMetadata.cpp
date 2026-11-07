// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <optional>
#include <utility>

// The order the transition is applied in is a decision, and it is: check everything, then build.
//
// A transition that is refused halfway would leave the caller holding an aggregate that is neither
// what it had nor what it asked for, and the caller has no way to tell which fields survived. So
// every rule is evaluated against the request before the first member is copied, and the failure
// paths all return before anything is constructed.
//
// The lineage is appended last for a related reason: it is the only step that can fail for a reason
// the transition itself is not responsible for - a lineage that is already full - and having it
// last means that failure cannot be confused with a rule about the change being made.

namespace an
{

ImageMetadata ImageMetadata::create(AcquisitionMetadata acquisition, ImageSemantics semantics,
                                    AstrometricSolution astrometry, Provenance provenance, CodecMetadata codec,
                                    std::optional<OutputIntent> outputIntent)
{
    ImageMetadata metadata;
    metadata.m_acquisition = std::move(acquisition);
    metadata.m_semantics = semantics;
    metadata.m_astrometry = astrometry;
    metadata.m_provenance = std::move(provenance);
    metadata.m_codec = std::move(codec);
    metadata.m_outputIntent = std::move(outputIntent);
    return metadata;
}

const AcquisitionMetadata& ImageMetadata::acquisition() const noexcept
{
    return m_acquisition;
}

const ImageSemantics& ImageMetadata::semantics() const noexcept
{
    return m_semantics;
}

const AstrometricSolution& ImageMetadata::astrometry() const noexcept
{
    return m_astrometry;
}

const Provenance& ImageMetadata::provenance() const noexcept
{
    return m_provenance;
}

const CodecMetadata& ImageMetadata::codec() const noexcept
{
    return m_codec;
}

const std::optional<OutputIntent>& ImageMetadata::outputIntent() const noexcept
{
    return m_outputIntent;
}

bool changesMeaning(const MetadataTransition& transition) noexcept
{
    return transition.semantics.has_value() || transition.astrometry.has_value();
}

bool changesAnything(const MetadataTransition& transition) noexcept
{
    return changesMeaning(transition) || transition.outputIntent.has_value() || transition.clearOutputIntent ||
           transition.clearCodecMetadata || transition.codec.has_value();
}

Result<ImageMetadata> applyMetadataTransition(const ImageMetadata& metadata, const MetadataTransition& transition)
{
    if (transition.outputIntent.has_value() && transition.clearOutputIntent)
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the transition both states an output intent and withdraws one",
                         .operation = "applyMetadataTransition",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "outputIntent")}});
    }
    if (transition.codec.has_value() && transition.clearCodecMetadata)
    {
        return Status::failure(ErrorCode::IncompatibleArguments,
                               StatusDetail{.message = "the transition both states the image's cards and drops them",
                                            .operation = "applyMetadataTransition",
                                            .domain = ErrorDomain::Image,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "codec")}});
    }
    if (changesAnything(transition) && !transition.record.has_value())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "a transition that changes an image records what changed it; a lineage with a "
                                    "gap in it cannot answer the question it exists for",
                         .operation = "applyMetadataTransition",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "record")}});
    }

    Provenance provenance = metadata.provenance();
    if (transition.record.has_value())
    {
        Result<Provenance> appended = provenance.appended(*transition.record);
        if (!appended.hasValue())
        {
            return appended.status();
        }
        provenance = std::move(*appended);
    }

    std::optional<OutputIntent> outputIntent = metadata.outputIntent();
    if (transition.outputIntent.has_value())
    {
        outputIntent = transition.outputIntent;
    }
    else if (transition.clearOutputIntent)
    {
        outputIntent.reset();
    }

    CodecMetadata codec = transition.codec.has_value()    ? *transition.codec
                          : transition.clearCodecMetadata ? CodecMetadata{}
                                                          : metadata.codec();

    return ImageMetadata::create(metadata.acquisition(),
                                 transition.semantics.has_value() ? *transition.semantics : metadata.semantics(),
                                 transition.astrometry.has_value() ? *transition.astrometry : metadata.astrometry(),
                                 std::move(provenance), std::move(codec), std::move(outputIntent));
}

} // namespace an
