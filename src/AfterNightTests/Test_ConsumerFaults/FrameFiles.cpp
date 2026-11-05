// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The consumer fault scenarios' input frames, described and written through the neutral fixture layer alone.

#include "Test_ConsumerFaults/FrameFiles.h"

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace an::tests::consumerfaults
{

bool writeFrame(const std::filesystem::path& path, const std::uint32_t width, const std::uint32_t height,
                const std::uint64_t seed, const double base)
{
    an::tests::fixtures::FixtureImage image;
    image.name = path.stem().string();
    image.width = width;
    image.height = height;
    image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
    image.pattern = an::tests::fixtures::FixturePattern::GradientNoise;
    image.seed = seed;
    an::tests::fixtures::FixtureChannelLevels levels;
    levels.base = base;
    levels.perColumn = 0.001;
    levels.amplitude = 0.01;
    image.levels = {levels};
    std::string error;
    const std::optional<an::tests::fixtures::FixtureSamples> samples =
        an::tests::fixtures::materializeFixture(image, error);
    if (!samples.has_value())
    {
        ADD_FAILURE() << error;
        return false;
    }
    if (!an::tests::fixtures::writeFixtureFits(image, *samples, path, error))
    {
        ADD_FAILURE() << error;
        return false;
    }
    return true;
}

} // namespace an::tests::consumerfaults
