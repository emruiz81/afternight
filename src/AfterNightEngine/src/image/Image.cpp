// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/Image.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <cstdint>
#include <optional>
#include <string>

// Only the two things that are not templates live here, and both are here for the same reason: they
// say the same sentence for all eight representations, so compiling them once is compiling them
// eight times fewer.
//
// The re-description is the more interesting of the two. It looks like a field assignment and it is
// not: changing what a sample is can make a description impossible - a physically scaled image has
// no integer form, because a descriptor carries no scale factor for an integer to be read through -
// so it goes back through the descriptor's own rules and reports the one that refused. That is what
// makes a working image of the wrong precision a refusal at the point somebody asked for it rather
// than a conversion that produces plausible numbers.

namespace an
{

Result<ImageDescriptor> describeSamplesAs(const ImageDescriptor& descriptor, SampleType sampleType)
{
    if (!isSampleType(sampleType))
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "the representation is not one of the eight this engine stores",
                         .operation = "describeSamplesAs",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "sampleType")}});
    }

    ImageDescriptorRequest request = descriptor.toRequest();
    request.sampleType = sampleType;
    const DescriptorError refused = ImageDescriptor::check(request);
    if (refused != DescriptorError::None)
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = std::string("this image cannot be described with ") + sampleTypeName(sampleType) +
                                    " samples: " + descriptorErrorName(refused),
                         .operation = "describeSamplesAs",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "sampleType")}});
    }

    const std::optional<ImageDescriptor> described = ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return Status::failure(ErrorCode::Internal,
                               StatusDetail{.message = "the description passed its own rules and then refused to be "
                                                       "built",
                                            .operation = "describeSamplesAs",
                                            .domain = ErrorDomain::Image});
    }
    return *described;
}

Status checkImageDescribesLayout(const ImageDescriptor& descriptor, const ImageLayout& layout, const char* operation)
{
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(descriptor.sampleType()))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = std::string("the memory holds samples of a different size than ") +
                                    sampleTypeName(descriptor.sampleType()),
                         .operation = operation,
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "descriptor"),
                                     StatusContextField::ofCount(ContextKey::Requested,
                                                                 sampleTypeSizeBytes(descriptor.sampleType())),
                                     StatusContextField::ofCount(ContextKey::Available, layout.elementSizeBytes())}});
    }
    if (!(layout.extent() == descriptor.extent()))
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the memory and the description are about different numbers of pixels",
                         .operation = operation,
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "descriptor")}});
    }
    return Status::success();
}

} // namespace an
