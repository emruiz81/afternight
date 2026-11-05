// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/CoveragePlane.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace
{

/// Whether a rectangle holds a pixel and lies inside a plane of a size.
[[nodiscard]] bool regionFits(const QRect& rect, const std::uint32_t width, const std::uint32_t height) noexcept
{
    return !rect.isEmpty() && rect.left() >= 0 && rect.top() >= 0 && static_cast<std::uint32_t>(rect.right()) < width &&
           static_cast<std::uint32_t>(rect.bottom()) < height;
}

} // namespace

namespace an::ui::drawing
{

an::Result<an::Image<float>> createCoverageImage(const an::EngineContext& engine, const std::uint32_t width,
                                                 const std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, engine.limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the drawing's size cannot hold a coverage plane");
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "a coverage plane of the drawing's size cannot be "
                                                                   "described");
    }
    return an::Image<float>::create(engine, *descriptor, an::StorageArrangement::Planar,
                                    an::StorageInitialization::Zeroed);
}

std::optional<an::PlaneView<float>> planeRegion(const an::PlaneView<float> plane, const QRect& rect)
{
    if (!regionFits(rect, plane.width(), plane.height()))
    {
        return std::nullopt;
    }
    return plane.subplane(static_cast<std::uint32_t>(rect.left()), static_cast<std::uint32_t>(rect.top()),
                          static_cast<std::uint32_t>(rect.width()), static_cast<std::uint32_t>(rect.height()));
}

std::optional<an::PlaneView<const float>> planeRegion(const an::PlaneView<const float> plane, const QRect& rect)
{
    if (!regionFits(rect, plane.width(), plane.height()))
    {
        return std::nullopt;
    }
    return plane.subplane(static_cast<std::uint32_t>(rect.left()), static_cast<std::uint32_t>(rect.top()),
                          static_cast<std::uint32_t>(rect.width()), static_cast<std::uint32_t>(rect.height()));
}

void copyPlane(const an::PlaneView<const float> source, const an::PlaneView<float> destination) noexcept
{
    if (source.width() != destination.width() || source.height() != destination.height())
    {
        return;
    }
    for (std::uint32_t y = 0u; y < source.height(); ++y)
    {
        const std::optional<std::span<const float>> from = source.row(y);
        const std::optional<std::span<float>> to = destination.row(y);
        if (from.has_value() && to.has_value())
        {
            std::copy(from->begin(), from->end(), to->begin());
        }
    }
}

void fillPlane(const an::PlaneView<float> destination, const float value) noexcept
{
    for (std::uint32_t y = 0u; y < destination.height(); ++y)
    {
        if (const std::optional<std::span<float>> row = destination.row(y); row.has_value())
        {
            std::fill(row->begin(), row->end(), value);
        }
    }
}

std::vector<float> copyPlaneRegion(const an::PlaneView<const float> plane, const QRect& rect)
{
    const std::optional<an::PlaneView<const float>> region = planeRegion(plane, rect);
    if (!region.has_value())
    {
        return {};
    }
    std::vector<float> samples(static_cast<std::size_t>(rect.width()) * static_cast<std::size_t>(rect.height()));
    const std::optional<an::PlaneView<float>> packed =
        packedPlane(samples, static_cast<std::uint32_t>(rect.width()), static_cast<std::uint32_t>(rect.height()));
    if (packed.has_value())
    {
        copyPlane(*region, *packed);
    }
    return samples;
}

void writePlaneRegion(const std::vector<float>& samples, const an::PlaneView<float> plane, const QRect& rect) noexcept
{
    const std::optional<an::PlaneView<float>> region = planeRegion(plane, rect);
    const std::optional<an::PlaneView<const float>> packed =
        packedPlane(samples, static_cast<std::uint32_t>(rect.width()), static_cast<std::uint32_t>(rect.height()));
    if (region.has_value() && packed.has_value())
    {
        copyPlane(*packed, *region);
    }
}

std::optional<an::PlaneView<float>> packedPlane(std::vector<float>& samples, const std::uint32_t width,
                                                const std::uint32_t height) noexcept
{
    if (samples.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        return std::nullopt;
    }
    return an::PlaneView<float>::create(samples, width, height, 1u, static_cast<std::uint64_t>(width) * sizeof(float));
}

std::optional<an::PlaneView<const float>> packedPlane(const std::vector<float>& samples, const std::uint32_t width,
                                                      const std::uint32_t height) noexcept
{
    if (samples.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        return std::nullopt;
    }
    return an::PlaneView<const float>::create(samples, width, height, 1u,
                                              static_cast<std::uint64_t>(width) * sizeof(float));
}

an::Result<CoveragePlane> CoveragePlane::create(const an::EngineContext& engine, const std::uint32_t width,
                                                const std::uint32_t height)
{
    an::Result<an::Image<float>> image = createCoverageImage(engine, width, height);
    if (!image.hasValue())
    {
        return image.status();
    }
    return CoveragePlane(std::move(*image));
}

CoveragePlane::CoveragePlane(an::Image<float>&& image) noexcept : m_image(std::move(image)) {}

bool CoveragePlane::valid() const noexcept
{
    return m_image.valid();
}

int CoveragePlane::width() const noexcept
{
    const std::optional<an::ImageView<const float>> samples = m_image.view();
    return samples.has_value() ? static_cast<int>(samples->width()) : 0;
}

int CoveragePlane::height() const noexcept
{
    const std::optional<an::ImageView<const float>> samples = m_image.view();
    return samples.has_value() ? static_cast<int>(samples->height()) : 0;
}

QRect CoveragePlane::rect() const noexcept
{
    return QRect(0, 0, width(), height());
}

an::PlaneView<float> CoveragePlane::view() noexcept
{
    return *m_image.write()->view().plane(0u);
}

an::PlaneView<const float> CoveragePlane::view() const noexcept
{
    return *m_image.view()->plane(0u);
}

float CoveragePlane::at(const int x, const int y) const noexcept
{
    return *view().sampleAt(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
}

} // namespace an::ui::drawing
