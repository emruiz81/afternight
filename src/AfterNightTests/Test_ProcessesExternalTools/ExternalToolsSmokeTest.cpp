// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The real programs, where they are installed: StarNet2 run through its window over a synthetic star field, its
/// starless image darker where the stars were and its star mask and star layer brighter there; DeepSNR run through
/// its window over a noisy gradient of its smallest size, its answer quieter than the input. Each case is skipped where
/// the program is not installed, unless `AFTERNIGHT_TEST_REQUIRE_EXTERNAL_TOOLS=1` makes that a failure.

#include "Test_ProcessesExternalTools/ExternalToolsTestSupport.h"

#include "processes/DeepSNRProcess.h"
#include "processes/StarnetProcess.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::externaltools;

class ExternalToolsSmokeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// Points the product at the installed program: true when one is; false - a failure when the real programs are
    /// required - without one, which the case then skips.
    [[nodiscard]] bool pointAt(const ExternalTool& tool)
    {
        const QString path = realToolPath(tool);
        if (path.isEmpty())
        {
            if (realToolsRequired())
            {
                ADD_FAILURE() << tool.programName << " is required and not installed";
            }
            return false;
        }
        return context().upsert(QString::fromLatin1(tool.settingsKey), path);
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// A star field: a sky of 0.05 with Gaussian stars of a few pixels at fixed places, in three channels.
[[nodiscard]] double starField(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    double value = 0.05 + 0.002 * channel;
    const std::vector<std::pair<double, double>> stars{{64.0, 64.0},   {200.0, 96.0},  {300.0, 300.0},
                                                       {420.0, 180.0}, {150.0, 400.0}, {480.0, 460.0}};
    for (const auto& [sx, sy] : stars)
    {
        const double d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy);
        value += 0.8 * std::exp(-d2 / (2.0 * 2.5 * 2.5));
    }
    return (std::min)(value, 1.0);
}

/// The mean of a channel over a rectangle, as fractions of full scale.
[[nodiscard]] double meanOver(const fixtures::FixtureSamples& samples, std::uint32_t x0, std::uint32_t y0,
                              std::uint32_t size, std::uint32_t channel)
{
    double sum = 0.0;
    for (std::uint32_t y = y0; y < y0 + size; ++y)
    {
        for (std::uint32_t x = x0; x < x0 + size; ++x)
        {
            sum += fractionOf(samples, x, y, channel);
        }
    }
    return sum / (static_cast<double>(size) * size);
}

/// The standard deviation of a channel's samples about a known gradient.
[[nodiscard]] double noiseAbout(const fixtures::FixtureSamples& samples, std::uint32_t channel)
{
    double sum = 0.0;
    double sumSquares = 0.0;
    std::uint64_t count = 0u;
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            const double expected = 0.2 + 0.4 * (static_cast<double>(x) / samples.width());
            const double residual = fractionOf(samples, x, y, channel) - expected;
            sum += residual;
            sumSquares += residual * residual;
            ++count;
        }
    }
    const double mean = sum / static_cast<double>(count);
    return std::sqrt((std::max)(0.0, sumSquares / static_cast<double>(count) - mean * mean));
}

} // namespace

// StarNet2 through its window: the starless image is the sky where the stars were, the star mask and the star layer
// are bright there and dark elsewhere, and the program's own progress reached the run.
TEST_F(ExternalToolsSmokeTest, StarNetRemovesTheStarsOfASyntheticField)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    if (!pointAt(starnet))
    {
        GTEST_SKIP() << "StarNet2 is not installed";
    }
    std::optional<an::consumer::ImageAsset> field =
        madeAsset(session(), 512u, 512u, 3u, an::SampleType::UInt16, starField);
    ASSERT_TRUE(field.has_value());
    ViewHandle target = assetView(std::move(*field), "field");
    activate(context(), *target);
    const std::size_t viewsBefore = context().madeViews.size();
    WindowHandle window = starnet.open(target.get());
    starnet.configure(*window, "stars");
    ASSERT_TRUE(runInstalledAndSettle(context(), *window, starnet));
    EXPECT_TRUE(context().errorTexts.isEmpty())
        << (context().errorTexts.isEmpty() ? QString() : context().errorTexts.front()).toStdString();
    const std::vector<ImageViewControl*> outputs(context().madeViews.begin() + static_cast<std::ptrdiff_t>(viewsBefore),
                                                 context().madeViews.end());
    ASSERT_EQ(outputs.size(), 3u);
    const std::optional<fixtures::FixtureSamples> starless = documentSamples(outputs[0]->document());
    const std::optional<fixtures::FixtureSamples> mask = documentSamples(outputs[1]->document());
    const std::optional<fixtures::FixtureSamples> stars = documentSamples(outputs[2]->document());
    ASSERT_TRUE(starless.has_value() && mask.has_value() && stars.has_value());
    EXPECT_EQ(starless->sampleType(), fixtures::FixtureSampleType::UInt16);
    EXPECT_EQ(starless->width(), 512u);
    EXPECT_EQ(starless->channelCount(), 3u);
    // At the star at (300, 300) the field is near one; the starless image is near the sky, the mask and the layer
    // bright; far from any star all three are what they should be.
    EXPECT_LT(meanOver(*starless, 298u, 298u, 5u, 1u), 0.3);
    EXPECT_NEAR(meanOver(*starless, 20u, 20u, 9u, 1u), 0.052, 0.03);
    EXPECT_GT(meanOver(*mask, 298u, 298u, 5u, 1u), 0.3);
    EXPECT_LT(meanOver(*mask, 20u, 20u, 9u, 1u), 0.1);
    EXPECT_GT(meanOver(*stars, 298u, 298u, 5u, 1u), 0.3);
    EXPECT_LT(meanOver(*stars, 20u, 20u, 9u, 1u), 0.1);
    window.reset();
    dropView(context(), target);
}

// DeepSNR through its window: a noisy gradient of the program's smallest size comes back quieter, in place, as one
// history step.
TEST_F(ExternalToolsSmokeTest, DeepSnrQuietensANoisyGradient)
{
    const ExternalTool& deepSnr = *toolNamed("DeepSnr");
    if (!pointAt(deepSnr))
    {
        GTEST_SKIP() << "DeepSNR is not installed";
    }
    std::mt19937 generator(7u);
    std::normal_distribution<double> noise(0.0, 0.02);
    std::vector<double> drawn(512u * 512u);
    for (double& value : drawn)
    {
        value = noise(generator);
    }
    std::optional<an::consumer::ImageAsset> gradient = madeAsset(
        session(), 512u, 512u, 1u, an::SampleType::UInt16, [&drawn](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return std::clamp(0.2 + 0.4 * (x / 512.0) + drawn[y * 512u + x], 0.0, 1.0);
        });
    ASSERT_TRUE(gradient.has_value());
    ViewHandle target = assetView(std::move(*gradient), "gradient");
    activate(context(), *target);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
    ASSERT_TRUE(before.has_value());
    WindowHandle window(new DeepSNRProcess(target.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(runInstalledAndSettle(context(), *window, deepSnr));
    EXPECT_TRUE(context().errorTexts.isEmpty())
        << (context().errorTexts.isEmpty() ? QString() : context().errorTexts.front()).toStdString();
    const std::optional<fixtures::FixtureSamples> after = documentSamples(target->document());
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->sampleType(), fixtures::FixtureSampleType::UInt16);
    const double noisy = noiseAbout(*before, 0u);
    const double quiet = noiseAbout(*after, 0u);
    EXPECT_LT(quiet, 0.7 * noisy) << "before " << noisy << ", after " << quiet;
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_TRUE(target->viewHistory()->hasUndo());
    EXPECT_EQ(target->viewHistory()->previousStepName(), "DeepSNR");
    window.reset();
    dropView(context(), target);
}
