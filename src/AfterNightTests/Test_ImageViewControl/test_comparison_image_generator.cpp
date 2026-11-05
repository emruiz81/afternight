// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The comparison view's difference image over the engine's image difference: per channel or as the grey mean, Float32
/// in the images' common domain, refused across shapes, and zero for two equal signed images.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/ComparisonImageGenerator.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace
{

using namespace an::tests::viewer;

class ComparisonImageGenerator : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::optional<TestSessionProvider> provider = openProvider(2u);
        ASSERT_TRUE(provider.has_value());
        m_provider.emplace(std::move(*provider));
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_provider->engineSession();
    }

    std::optional<TestSessionProvider> m_provider;
};

TEST_F(ComparisonImageGenerator, BuildsDeterministicMonoDifferenceImage)
{
    std::optional<an::consumer::ImageAsset> left =
        makeAsset<float>(session(), 2u, 1u, Channels::Grey,
                         [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x == 0u ? 0.2f : 0.8f; });
    std::optional<an::consumer::ImageAsset> right =
        makeAsset<float>(session(), 2u, 1u, Channels::Grey,
                         [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x == 0u ? 0.5f : 0.1f; });
    ASSERT_TRUE(left.has_value() && right.has_value());

    an::Result<an::consumer::ImageAsset> difference = an::ui::buildDifferenceImage(session(), *left, *right, {}, {});
    ASSERT_TRUE(difference.hasValue()) << difference.status().message();
    ASSERT_TRUE(difference->descriptor().has_value());
    EXPECT_EQ(difference->descriptor()->extent().width(), 2u);
    EXPECT_EQ(difference->descriptor()->extent().height(), 1u);
    EXPECT_EQ(difference->descriptor()->extent().channelCount(), 1u);
    EXPECT_EQ(difference->descriptor()->sampleType(), an::SampleType::Float32);
    EXPECT_NEAR(*sampleOf<float>(*difference, 0u, 0u), 0.3f, 1.0e-6f);
    EXPECT_NEAR(*sampleOf<float>(*difference, 1u, 0u), 0.7f, 1.0e-6f);
}

TEST_F(ComparisonImageGenerator, SupportsGrayscaleDifferenceReduction)
{
    std::optional<an::consumer::ImageAsset> left =
        makeAsset<float>(session(), 1u, 1u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return c == 0u ? 0.1f : (c == 1u ? 0.4f : 0.9f);
        });
    std::optional<an::consumer::ImageAsset> right =
        makeAsset<float>(session(), 1u, 1u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return c == 0u ? 0.6f : (c == 1u ? 0.2f : 0.3f);
        });
    ASSERT_TRUE(left.has_value() && right.has_value());

    an::Result<an::consumer::ImageAsset> difference = an::ui::buildDifferenceImage(
        session(), *left, *right, an::ui::ComparisonDifferenceOptions{.grayscale = true}, {});
    ASSERT_TRUE(difference.hasValue()) << difference.status().message();
    EXPECT_EQ(difference->descriptor()->extent().channelCount(), 1u);
    EXPECT_NEAR(*sampleOf<float>(*difference, 0u, 0u), (0.5f + 0.2f + 0.6f) / 3.0f, 1.0e-6f);
}

TEST_F(ComparisonImageGenerator, RejectsIncompatibleImageGeometry)
{
    const auto zero = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.0f; };
    std::optional<an::consumer::ImageAsset> left = makeAsset<float>(session(), 2u, 1u, Channels::Grey, zero);
    std::optional<an::consumer::ImageAsset> right = makeAsset<float>(session(), 3u, 1u, Channels::Grey, zero);
    ASSERT_TRUE(left.has_value() && right.has_value());

    const an::Result<an::consumer::ImageAsset> difference =
        an::ui::buildDifferenceImage(session(), *left, *right, {}, {});
    EXPECT_FALSE(difference.hasValue());
}

// Two equal signed images differ by zero everywhere, not by the lowest code an offset reading would give.
TEST_F(ComparisonImageGenerator, TwoEqualSignedImagesDifferByNothing)
{
    const auto signedRamp = [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<std::int16_t>(-20000 + 1000 * static_cast<int>(x) - 500 * static_cast<int>(y));
    };
    std::optional<an::consumer::ImageAsset> left =
        makeAsset<std::int16_t>(session(), 6u, 4u, Channels::Grey, signedRamp);
    std::optional<an::consumer::ImageAsset> right =
        makeAsset<std::int16_t>(session(), 6u, 4u, Channels::Grey, signedRamp);
    ASSERT_TRUE(left.has_value() && right.has_value());

    an::Result<an::consumer::ImageAsset> difference = an::ui::buildDifferenceImage(session(), *left, *right, {}, {});
    ASSERT_TRUE(difference.hasValue()) << difference.status().message();
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 6u; ++x)
        {
            const std::optional<float> sample = sampleOf<float>(*difference, x, y);
            ASSERT_TRUE(sample.has_value());
            EXPECT_EQ(*sample, 0.0f) << x << "," << y;
        }
    }
}

} // namespace
