// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the full host tells an extension about an image it shows, in the protocol's words.
///
/// Every `pixel_type` the host hands Python - a shared buffer's, a snapshot's, the current view's - names the sample
/// type in the protocol's vocabulary (`uint8`, `uint16`, `float32`, `float64`). The current view's description falls
/// back to the image the native source view shows when no buffer describes it, and that fallback names it the same way:
/// an image of a representation the protocol does not carry is described with an empty `pixel_type`, never with a
/// display string of another vocabulary.
#pragma once

#include "app/SharedBufferManager.h"
#include "controls/ImageViewControl.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <optional>
#include <string>

namespace an::ui::extensions
{

/// The protocol's name for a sample type, or an empty string for one the protocol does not carry.
///
/// @param type the representation.
/// @return the name, as `sharedBufferPixelTypeName` spells it.
[[nodiscard]] inline std::string protocolPixelTypeName(const an::SampleType type)
{
    const an::Result<SharedBufferPixelType> carried = sharedBufferPixelTypeOf(type);
    return carried.hasValue() ? std::string(sharedBufferPixelTypeName(*carried)) : std::string();
}

/// What an image is, as the host describes it to an extension.
struct HostImageDimensions
{
    int width = 0;
    int height = 0;
    int channels = 0;

    /// The protocol's name for the sample type; empty for a representation it does not carry.
    std::string pixelType;
};

/// The description of the image a view shows, read without a lease; nothing when it shows none.
///
/// @param view the view; may be null.
/// @return the dimensions and the protocol's name for the sample type.
[[nodiscard]] inline std::optional<HostImageDimensions> viewImageDimensions(const ImageViewControl* view)
{
    const std::optional<an::ImageDescriptor> described = view != nullptr ? view->imageDescriptor() : std::nullopt;
    if (!described.has_value())
    {
        return std::nullopt;
    }
    HostImageDimensions dimensions;
    dimensions.width = static_cast<int>(described->extent().width());
    dimensions.height = static_cast<int>(described->extent().height());
    dimensions.channels = static_cast<int>(described->extent().channelCount());
    dimensions.pixelType = protocolPixelTypeName(described->sampleType());
    return dimensions;
}

} // namespace an::ui::extensions
