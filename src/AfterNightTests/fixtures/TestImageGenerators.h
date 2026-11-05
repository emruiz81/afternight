// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Synthetic frames for the consumer suites, described in the engine-neutral fixture layer and made through the engine
/// fixture adapter: an image of constant levels, grey or colour, and a colour gradient, each an asset of the caller's
/// session. A description is the record of what a case asked for; the asset is what the case hands the product.
#pragma once

#include "fixtures/common/FixtureImage.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::fixtures
{

/// A Float32 image of a constant level per channel: one level a grey image, three a colour one.
///
/// @param name the description's name.
/// @param width the width.
/// @param height the height.
/// @param levels the channels' levels, fractions of full scale.
/// @return the description.
[[nodiscard]] inline FixtureImage constantFloatImage(std::string name, std::uint32_t width, std::uint32_t height,
                                                     const std::vector<double>& levels)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.channels = levels.size() == 3u ? FixtureChannels::Rgb : FixtureChannels::Mono;
    image.sampleType = FixtureSampleType::Float32;
    image.domain = FixtureDomain::NormalizedLinear;
    image.pattern = FixturePattern::Constant;
    for (const double level : levels)
    {
        image.levels.push_back(FixtureChannelLevels{.base = level});
    }
    return image;
}

/// A Float32 colour gradient: red rises from zero at the first column to one at the last, green from zero at the top
/// row to one at the bottom, and blue is a half everywhere.
///
/// @param name the description's name.
/// @param width the width, at least two.
/// @param height the height, at least two.
/// @return the description.
[[nodiscard]] inline FixtureImage rgbGradientImage(std::string name, std::uint32_t width, std::uint32_t height)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.channels = FixtureChannels::Rgb;
    image.sampleType = FixtureSampleType::Float32;
    image.domain = FixtureDomain::NormalizedLinear;
    image.pattern = FixturePattern::Gradient;
    const double perColumn = width > 1u ? 1.0 / static_cast<double>(width - 1u) : 0.0;
    const double perRow = height > 1u ? 1.0 / static_cast<double>(height - 1u) : 0.0;
    image.levels = {FixtureChannelLevels{.perColumn = perColumn}, FixtureChannelLevels{.perRow = perRow},
                    FixtureChannelLevels{.base = 0.5}};
    return image;
}

/// The asset a description stands for, in a session.
///
/// @param session the session the asset belongs to.
/// @param image the description.
/// @return the asset; `ErrorCode::InvalidArgument` naming the reason when the fixture layer refuses the description,
///         or the adapter's failure.
[[nodiscard]] inline an::Result<an::consumer::ImageAsset> makeGeneratedAsset(const an::consumer::EngineSession& session,
                                                                             const FixtureImage& image)
{
    std::string error;
    const std::optional<FixtureSamples> samples = materializeFixture(image, error);
    if (!samples.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, error);
    }
    return engine::makeImageAsset(session, image, *samples);
}

/// A colour Float32 image of constant levels, as an asset of the session.
[[nodiscard]] inline an::Result<an::consumer::ImageAsset>
createRgbFloatAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, double red,
                    double green, double blue)
{
    return makeGeneratedAsset(session, constantFloatImage("rgb-float", width, height, {red, green, blue}));
}

/// A grey Float32 image of one level, as an asset of the session.
[[nodiscard]] inline an::Result<an::consumer::ImageAsset>
createGreyFloatAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
                     double level)
{
    return makeGeneratedAsset(session, constantFloatImage("grey-float", width, height, {level}));
}

/// The colour gradient of `rgbGradientImage`, as an asset of the session.
[[nodiscard]] inline an::Result<an::consumer::ImageAsset>
createRgbGradientAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height)
{
    return makeGeneratedAsset(session, rgbGradientImage("rgb-gradient", width, height));
}

} // namespace an::tests::fixtures
