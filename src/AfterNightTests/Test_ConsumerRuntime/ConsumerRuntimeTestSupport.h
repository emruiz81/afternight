// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the consumer runtime's cases share: a session of a stated width, images built through the engine, and
/// bounded waits, so a case whose subject never answers fails instead of hanging the suite.
#pragma once

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <optional>
#include <thread>
#include <variant>

namespace an::tests::consumer
{

/// How long a case waits for something that should happen at once before calling it a failure.
inline constexpr std::chrono::seconds kPatience{10};

/// How long a case waits to be confident that something that should not happen has not.
inline constexpr std::chrono::milliseconds kQuietPeriod{100};

/// Settings for a session of a given width and nothing else stated.
[[nodiscard]] inline an::consumer::EngineSessionSettings settingsOfWidth(std::uint32_t width)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    return settings;
}

/// A session of a given width; fails the case when it cannot be opened.
[[nodiscard]] inline std::optional<an::consumer::EngineSession> openSession(std::uint32_t width)
{
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settingsOfWidth(width));
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*session);
}

/// The description of a single-channel Float32 image.
[[nodiscard]] inline std::optional<an::ImageDescriptor> monoFloatDescriptor(const an::EngineContext& engine,
                                                                            std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, engine.limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                  .sampleType = an::SampleType::Float32,
                                                                  .channelSchema = an::ChannelSchema::Mono,
                                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                                  .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                  .cfa = an::CfaDescriptor{},
                                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                  .pixelAspectRatio = 1.0});
}

/// A single-channel Float32 image holding one value everywhere.
[[nodiscard]] inline an::Result<an::Image<float>> filledImage(const an::EngineContext& engine, std::uint32_t width,
                                                              std::uint32_t height, float value)
{
    const std::optional<an::ImageDescriptor> descriptor = monoFloatDescriptor(engine, width, height);
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the test image cannot be described");
    }
    return an::Image<float>::createFilled(engine, *descriptor, an::StorageArrangement::Planar, value);
}

/// A new asset of a session holding a filled image; fails the case when it cannot be built.
[[nodiscard]] inline std::optional<an::consumer::ImageAsset> filledAsset(const an::consumer::EngineSession& session,
                                                                         float value, std::uint32_t width = 8u,
                                                                         std::uint32_t height = 4u)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image = filledImage(hold->engine(), width, height, value);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// The sample at a position of a single-channel Float32 image, or NaN.
[[nodiscard]] inline float sampleAt(const an::consumer::AnyImage& image, std::uint32_t x, std::uint32_t y)
{
    const an::Image<float>* typed = std::get_if<an::Image<float>>(&image);
    if (typed == nullptr || !typed->view().has_value())
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto row = typed->view()->row(0u, y);
    return row.has_value() && x < row->size() ? (*row)[x] : std::numeric_limits<float>::quiet_NaN();
}

/// Writes one value at a position of a single-channel Float32 asset through its write lease.
inline void writeSample(an::consumer::AssetWriteLease& lease, std::uint32_t x, std::uint32_t y, float value)
{
    std::optional<an::ImageWriteAccess<float>> access = lease.write<float>();
    ASSERT_TRUE(access.has_value());
    const auto row = access->view().row(0u, y);
    ASSERT_TRUE(row.has_value());
    (*row)[x] = value;
}

/// Polls a condition until it holds or the patience runs out.
[[nodiscard]] inline bool eventually(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < until)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

/// Whether a future is still not ready after the quiet period.
template <typename ValueT> [[nodiscard]] bool stillWaiting(const std::future<ValueT>& future)
{
    return future.wait_for(kQuietPeriod) == std::future_status::timeout;
}

/// Whether a future becomes ready within the patience.
template <typename ValueT> [[nodiscard]] bool answers(const std::future<ValueT>& future)
{
    return future.wait_for(kPatience) == std::future_status::ready;
}

} // namespace an::tests::consumer
