// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stop_token>
#include <utility>

namespace registration_test
{

[[nodiscard]] inline an::EngineContext makeEngine(std::uint32_t concurrency = 8u,
                                                  std::uint64_t memoryLimit = 256u * 1024u * 1024u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryLimit;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] inline an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 4u,
                                                        std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] inline an::ImageDescriptor descriptorOf(std::uint32_t width, std::uint32_t height,
                                                      std::uint32_t channels = 1u,
                                                      an::SampleType sampleType = an::SampleType::Float32,
                                                      an::NumericDomain domain = an::NumericDomain::EncodedNonlinear)
{
    const an::ChannelSchema schema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = sampleType,
                                             .channelSchema = schema,
                                             .colorSpace = schema == an::ChannelSchema::Rgb
                                                               ? (domain == an::NumericDomain::EncodedNonlinear
                                                                      ? an::ColorSpace::DisplayRgb
                                                                      : an::ColorSpace::LinearRgb)
                                                               : an::ColorSpace::Unspecified,
                                             .numericDomain = domain,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] inline an::ImageLayout layoutOf(const an::ImageDescriptor& descriptor,
                                              an::StorageArrangement arrangement = an::StorageArrangement::Planar)
{
    const std::optional<an::ImageLayoutRequest> request = an::layoutRequestFor(descriptor, arrangement, 1u);
    EXPECT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

} // namespace registration_test
