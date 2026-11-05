// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - the engine layout of an image the library produced.
//
// The one part of the image bridge compiled into the adapter rather than written inline in its
// header. It holds no G'MIC type: the description it reads is the adapter's own, so it builds the
// same in both configurations.

#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <cstdint>
#include <optional>

namespace an
{

std::optional<ImageLayout> gmicResultLayout(const GmicImageDescription& description,
                                            const EngineLimits& limits) noexcept
{
    if (checkGmicImageDescription(description) != BridgeRefusal::None)
    {
        return std::nullopt;
    }
    // The library's own image is one plane per channel with nothing between a row and the next one
    // and nothing between a plane and the next one, so the strides are not read from anywhere - they
    // are what "tightly packed" means, and stating them here is what stops a caller deriving them
    // again at each use.
    const std::optional<ImageExtent> extent =
        ImageExtent::create(description.width, description.height, description.spectrum, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const ImageLayoutRequest request{.extent = *extent,
                                     .elementSizeBytes = sampleTypeSizeBytes(kGmicSampleType),
                                     .samplesPerPixel = 1u,
                                     .planeCount = description.spectrum,
                                     .rowAlignmentBytes = 1u};
    const auto rowStride = static_cast<std::uint64_t>(description.width) * request.elementSizeBytes;
    return ImageLayout::createWithStrides(request, rowStride, rowStride * description.height, limits);
}

} // namespace an
