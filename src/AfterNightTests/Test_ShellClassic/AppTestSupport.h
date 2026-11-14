// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the application suite's cases share: a small image of a session, as an asset or a document, and the guard of a
/// case that needs the full extension host deployed beside the suite.
#pragma once

#include "session/ImageDocument.h"

#include <QtGlobal>

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QString>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace an::tests::app
{

/// The variable that makes a full extension host missing beside the suite a failure rather than a skip.
inline constexpr const char* kRequireFullExtensionHostVariable = "AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST";

/// Whether the run asked for the full extension host to be present.
[[nodiscard]] inline bool fullExtensionHostRequired()
{
    return qEnvironmentVariableIntValue(kRequireFullExtensionHostVariable) == 1;
}

/// What a case that installs through the full extension host says when the host is not deployed beside the suite.
inline constexpr const char* kFullExtensionHostAbsent =
    "the full extension host is not deployed beside this suite; set AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1 to make "
    "its absence a failure";

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

/// A new asset of a session holding a single-channel Float32 image filled with one value, or nothing when it cannot be
/// made.
[[nodiscard]] inline std::optional<an::consumer::ImageAsset> filledAsset(const an::consumer::EngineSession& session,
                                                                         float value, std::uint32_t width = 8u,
                                                                         std::uint32_t height = 4u)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor = monoFloatDescriptor(hold->engine(), width, height);
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, value);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// A new document around a filled asset of a session, shown under `name`, or nothing when it cannot be made.
[[nodiscard]] inline std::optional<an::ui::ImageDocument> filledDocument(const an::consumer::EngineSession& session,
                                                                         const QString& name, float value = 0.25f)
{
    std::optional<an::consumer::ImageAsset> asset = filledAsset(session, value);
    if (!asset.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document =
        an::ui::ImageDocument::create(std::move(*asset), an::ui::DocumentNames{.displayName = name, .sourcePath = {}});
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

} // namespace an::tests::app

/// Ends a case that needs the full extension host when the host is not deployed beside the suite: as a skip, or as a
/// failure when the run asked for the host. `host_path` is the host the application directory names, empty when none.
#define AN_APP_REQUIRE_FULL_EXTENSION_HOST(host_path)                   \
    do                                                                  \
    {                                                                   \
        if ((host_path).empty() || !std::filesystem::exists(host_path)) \
        {                                                               \
            if (::an::tests::app::fullExtensionHostRequired())          \
            {                                                           \
                FAIL() << ::an::tests::app::kFullExtensionHostAbsent;   \
            }                                                           \
            GTEST_SKIP() << ::an::tests::app::kFullExtensionHostAbsent; \
        }                                                               \
    } while (false)
