// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The support unit the window runs, held directly over assets: which way each expression runs and why, the binding
/// policy for size, format and domain, the symbols, the seeded random stream, the unclamped result and its narrowing,
/// the rescale, the validity a run keeps, the result's metadata, and the dialog's settings file and symbol lists.

#include "Test_ProcessesPixelMath/PixelMathTestSupport.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::pixelmath;
using an::ui::processes::PixelMathColorFormat;
using an::ui::processes::PixelMathColorSpace;
using an::ui::processes::PixelMathExpressionTab;
using an::ui::processes::PixelMathImageSet;
using an::ui::processes::PixelMathNamedImage;
using an::ui::processes::PixelMathOutputMode;
using an::ui::processes::PixelMathSettings;

class PixelMathRunTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A Float32 image whose sample is `value(x, y, channel)`.
    [[nodiscard]] an::consumer::ImageAsset
    floats(std::uint32_t channels, const std::function<double(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
           std::uint32_t width = 8u, std::uint32_t height = 6u,
           an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
    {
        std::optional<an::consumer::ImageAsset> made =
            filledAsset(session(), width, height, channels, an::SampleType::Float32, domain, value);
        EXPECT_TRUE(made.has_value());
        return made.has_value() ? std::move(*made) : an::consumer::ImageAsset{};
    }

    /// The samples a run made; fails the case when it made none.
    [[nodiscard]] fixtures::FixtureSamples samplesOf(const DirectRun& run)
    {
        EXPECT_TRUE(run.status.ok()) << run.status.message();
        EXPECT_TRUE(run.result.has_value());
        if (!run.result.has_value())
        {
            return fixtures::FixtureSamples();
        }
        std::optional<fixtures::FixtureSamples> samples = assetSamples(*run.result);
        EXPECT_TRUE(samples.has_value());
        return samples.has_value() ? std::move(*samples) : fixtures::FixtureSamples();
    }

    std::unique_ptr<ProcessTestContext> m_context;
};

[[nodiscard]] double gradient(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    return 0.05 + 0.1 * x + 0.03 * y + 0.01 * channel;
}

[[nodiscard]] const an::ImageMetadata& metadataOf(const an::consumer::AssetReadLease& lease)
{
    return std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease.image());
}

// ---- Which way an expression runs.

// A chain runs as the engine's fused pointwise program, and computes what the expression says.
TEST_F(PixelMathRunTest, AChainTakesTheFusedPath)
{
    const an::consumer::ImageAsset target = floats(3u, gradient);
    const DirectRun run = runDirect(session(), target, expressionSettings("$T * 0.5 + 0.25"));
    const fixtures::FixtureSamples made = samplesOf(run);
    ASSERT_EQ(run.report.runs.size(), 1u);
    EXPECT_TRUE(run.report.runs[0].fused);
    EXPECT_EQ(run.report.runs[0].blocker, an::PixelMathLoweringBlocker::None);
    EXPECT_EQ(run.report.runs[0].channels, (std::vector<std::uint32_t>{0u, 1u, 2u}));
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 8u; ++x)
            {
                const double expected = static_cast<float>(gradient(x, y, channel)) * 0.5 + 0.25;
                EXPECT_NEAR(made.value(x, y, channel), expected, 1e-6);
            }
        }
    }
    // A chain over another image of the same shape fuses as well.
    an::consumer::ImageAsset other =
        floats(3u, [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.1 * (c + 1); });
    PixelMathImageSet images;
    images.others.push_back(PixelMathNamedImage{
        .name = "other", .asset = std::make_shared<const an::consumer::ImageAsset>(std::move(other))});
    const DirectRun two = runDirect(session(), target, expressionSettings("$T - other"), images);
    const fixtures::FixtureSamples difference = samplesOf(two);
    ASSERT_EQ(two.report.runs.size(), 1u);
    EXPECT_TRUE(two.report.runs[0].fused);
    EXPECT_EQ(two.report.boundImages, (std::vector<std::string>{"$T", "other"}));
    EXPECT_NEAR(difference.value(3u, 2u, 1u), static_cast<float>(gradient(3u, 2u, 1u)) - static_cast<float>(0.2), 1e-6);
}

// An expression that is not a chain runs through the engine's evaluator, and the run says what stopped it.
TEST_F(PixelMathRunTest, AnExpressionThatIsNotAChainTakesTheEvaluatorAndSaysWhy)
{
    const an::consumer::ImageAsset target = floats(1u, gradient);
    struct Case
    {
        const char* expression;
        an::PixelMathLoweringBlocker blocker;
    };
    const Case cases[] = {{"iif($T > 0.5, 1, 0)", an::PixelMathLoweringBlocker::UsesBranch},
                          {"$T - med($T)", an::PixelMathLoweringBlocker::UsesImageStatistic},
                          {"$T * x()", an::PixelMathLoweringBlocker::UsesPixelPosition},
                          {"$T + random() * 0", an::PixelMathLoweringBlocker::UsesRandom}};
    for (const Case& tried : cases)
    {
        SCOPED_TRACE(tried.expression);
        const DirectRun run = runDirect(session(), target, expressionSettings(tried.expression));
        ASSERT_TRUE(run.status.ok()) << run.status.message();
        ASSERT_EQ(run.report.runs.size(), 1u);
        EXPECT_FALSE(run.report.runs[0].fused);
        EXPECT_EQ(run.report.runs[0].blocker, tried.blocker);
    }
    const fixtures::FixtureSamples branched =
        samplesOf(runDirect(session(), target, expressionSettings("iif($T > 0.5, 1, 0)")));
    EXPECT_EQ(branched.value(0u, 0u, 0u), 0.0);
    EXPECT_EQ(branched.value(7u, 5u, 0u), 1.0);
}

// With one expression per colour channel, a chain still runs fused, one plane at a time; a channel whose expression is
// blank keeps the target's samples.
TEST_F(PixelMathRunTest, PerChannelChainsRunFusedPlaneByPlaneAndABlankChannelKeepsTheTarget)
{
    const an::consumer::ImageAsset target = floats(3u, gradient);
    PixelMathSettings settings;
    settings.singleExpression = false;
    settings.expressions = {"$T * 2", "$T * 0.5", "  ", ""};
    const DirectRun run = runDirect(session(), target, settings);
    const fixtures::FixtureSamples made = samplesOf(run);
    ASSERT_EQ(run.report.runs.size(), 2u);
    EXPECT_TRUE(run.report.runs[0].fused);
    EXPECT_EQ(run.report.runs[0].channels, std::vector<std::uint32_t>{0u});
    EXPECT_TRUE(run.report.runs[1].fused);
    EXPECT_EQ(run.report.runs[1].channels, std::vector<std::uint32_t>{1u});
    EXPECT_NEAR(made.value(2u, 3u, 0u), static_cast<float>(gradient(2u, 3u, 0u)) * 2.0, 1e-6);
    EXPECT_NEAR(made.value(2u, 3u, 1u), static_cast<float>(gradient(2u, 3u, 1u)) * 0.5, 1e-6);
    EXPECT_FLOAT_EQ(made.value(2u, 3u, 2u), static_cast<float>(gradient(2u, 3u, 2u)));
    // A per-channel expression that reads another channel is the evaluator's.
    settings.expressions = {"$T[1]", "", "", ""};
    const DirectRun selected = runDirect(session(), target, settings);
    ASSERT_TRUE(selected.status.ok()) << selected.status.message();
    ASSERT_EQ(selected.report.runs.size(), 1u);
    EXPECT_FALSE(selected.report.runs[0].fused);
    EXPECT_FLOAT_EQ(samplesOf(selected).value(4u, 1u, 0u), static_cast<float>(gradient(4u, 1u, 1u)));
}

// An alpha channel takes the alpha tab's expression; the colour channels the first tab's.
TEST_F(PixelMathRunTest, TheAlphaTabWritesTheAlphaChannel)
{
    const an::consumer::ImageAsset target = floats(4u, gradient);
    PixelMathSettings settings = expressionSettings("$T * 0.5");
    settings.expressions[3] = "1";
    const DirectRun run = runDirect(session(), target, settings);
    ASSERT_TRUE(run.result.has_value()) << run.status.message();
    const std::optional<StoredSamples> made = storedSamples(*run.result);
    ASSERT_TRUE(made.has_value());
    ASSERT_EQ(made->channelCount, 4u);
    EXPECT_NEAR(made->value(1u, 1u, 2u), static_cast<float>(gradient(1u, 1u, 2u)) * 0.5, 1e-6);
    EXPECT_EQ(made->value(1u, 1u, 3u), 1.0);
    ASSERT_EQ(run.report.runs.size(), 2u);
    EXPECT_EQ(run.report.runs[1].tab, PixelMathExpressionTab::Alpha);
    EXPECT_EQ(run.report.runs[1].channels, std::vector<std::uint32_t>{3u});
}

// ---- What is written.

// `$T * 2` produces what it says: no clamp to the unit interval, in a floating image; an integer image saturates at its
// largest code, its samples read and written as fractions of full scale.
TEST_F(PixelMathRunTest, TheResultIsNotClampedAndAnIntegerIsWorkedInFractions)
{
    const an::consumer::ImageAsset target =
        floats(1u, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.2 * x; });
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), target, expressionSettings("$T * 2")));
    EXPECT_NEAR(made.value(7u, 0u, 0u), 2.8, 1e-6);
    EXPECT_NEAR(made.value(1u, 0u, 0u), 0.4, 1e-6);

    std::optional<an::consumer::ImageAsset> codes =
        filledAsset(session(), 4u, 2u, 1u, an::SampleType::UInt16, an::NumericDomain::NormalizedLinear,
                    [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 20000.0 * x; });
    ASSERT_TRUE(codes.has_value());
    const fixtures::FixtureSamples half =
        samplesOf(runDirect(session(), *codes, expressionSettings("$T * 0.5 + 0.25")));
    EXPECT_EQ(half.sampleType(), fixtures::FixtureSampleType::UInt16);
    EXPECT_EQ(half.value(2u, 0u, 0u), std::nearbyint((40000.0 / 65535.0 * 0.5 + 0.25) * 65535.0));
    const fixtures::FixtureSamples twice = samplesOf(runDirect(session(), *codes, expressionSettings("$T * 2")));
    EXPECT_EQ(twice.value(3u, 0u, 0u), 65535.0) << "an integer saturates at its largest code";

    std::optional<an::consumer::ImageAsset> signedCodes =
        filledAsset(session(), 3u, 1u, 1u, an::SampleType::Int16, an::NumericDomain::NormalizedLinear,
                    [](std::uint32_t x, std::uint32_t, std::uint32_t) { return -32768.0 + 32767.5 * x; });
    ASSERT_TRUE(signedCodes.has_value());
    // A signed sample is a fraction counted from the smallest value: -32768 is zero, 32767 one.
    const fixtures::FixtureSamples negated =
        samplesOf(runDirect(session(), *signedCodes, expressionSettings("1 - $T")));
    EXPECT_EQ(negated.value(0u, 0u, 0u), 32767.0);
    EXPECT_EQ(negated.value(2u, 0u, 0u), -32768.0);
}

// "Rescale result" maps the smallest and the largest finite value of every channel to the dialog's bounds.
TEST_F(PixelMathRunTest, RescaleMapsTheResultsRangeToTheBounds)
{
    const an::consumer::ImageAsset target = floats(3u, gradient);
    PixelMathSettings settings = expressionSettings("$T * 3");
    settings.rescale = true;
    settings.rescaleLow = 0.1;
    settings.rescaleHigh = 0.9;
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), target, settings));
    const double smallest = static_cast<float>(gradient(0u, 0u, 0u)) * 3.0;
    const double largest = static_cast<float>(gradient(7u, 5u, 2u)) * 3.0;
    EXPECT_NEAR(made.value(0u, 0u, 0u), 0.1, 1e-6);
    EXPECT_NEAR(made.value(7u, 5u, 2u), 0.9, 1e-6);
    const double middle = static_cast<float>(gradient(3u, 2u, 1u)) * 3.0;
    EXPECT_NEAR(made.value(3u, 2u, 1u), 0.1 + 0.8 * (middle - smallest) / (largest - smallest), 1e-5);
}

// A result with no range - one value everywhere - has nothing to map, and Rescale leaves it as it is.
TEST_F(PixelMathRunTest, RescaleLeavesAResultWithNoRangeAsItIs)
{
    const an::consumer::ImageAsset target = floats(3u, gradient);
    PixelMathSettings settings = expressionSettings("0.25");
    settings.rescale = true;
    settings.rescaleLow = 0.1;
    settings.rescaleHigh = 0.9;
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), target, settings));
    for (std::uint32_t channel = 0u; channel < made.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < made.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < made.width(); ++x)
            {
                EXPECT_EQ(made.value(x, y, channel), 0.25) << "at " << x << ", " << y << ", channel " << channel;
            }
        }
    }
}

// A pixel the target's validity rejects keeps its samples; an integer's value rule is judged on its stored samples.
TEST_F(PixelMathRunTest, APixelTheTargetsValidityRejectsKeepsItsSamples)
{
    std::optional<an::consumer::ImageAsset> codes = filledAsset(
        session(), 4u, 2u, 1u, an::SampleType::UInt16, an::NumericDomain::NormalizedLinear,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return (x == 1u && y == 1u) ? 0.0 : 1000.0; });
    ASSERT_TRUE(codes.has_value());
    an::ImageSemanticsRequest meaning;
    meaning.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue());
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt);
    std::optional<an::consumer::ImageAsset> stated = withMetadata(session(), *codes, metadata);
    ASSERT_TRUE(stated.has_value());
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), *stated, expressionSettings("$T + 0.5")));
    EXPECT_EQ(made.value(1u, 1u, 0u), 0.0) << "the rejected pixel was written";
    EXPECT_EQ(made.value(0u, 0u, 0u), std::nearbyint((1000.0 / 65535.0 + 0.5) * 65535.0));
}

/// A target of sample codes whose zero code sits at one pixel, stated to treat exact zero as no data.
[[nodiscard]] an::ImageMetadata zeroIsNoDataMetadata()
{
    an::ImageSemanticsRequest meaning;
    meaning.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    EXPECT_TRUE(semantics.hasValue());
    return an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                     an::CodecMetadata{}, std::nullopt);
}

// A signed integer's zero code is not a zero fraction: the value rule is judged on the stored samples, so the pixel
// holding the code zero keeps its samples.
TEST_F(PixelMathRunTest, ASignedIntegersZeroRuleIsJudgedOnItsStoredCodes)
{
    std::optional<an::consumer::ImageAsset> codes = filledAsset(
        session(), 4u, 2u, 1u, an::SampleType::Int16, an::NumericDomain::NormalizedLinear,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return (x == 1u && y == 1u) ? 0.0 : 1000.0; });
    ASSERT_TRUE(codes.has_value());
    std::optional<an::consumer::ImageAsset> stated = withMetadata(session(), *codes, zeroIsNoDataMetadata());
    ASSERT_TRUE(stated.has_value());
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), *stated, expressionSettings("$T + 0.25")));
    EXPECT_EQ(made.value(1u, 1u, 0u), 0.0) << "the code zero was written";
    EXPECT_EQ(made.value(0u, 0u, 0u), std::nearbyint(((1000.0 + 32768.0) / 65535.0 + 0.25) * 65535.0) - 32768.0);
}

// An integer target every pixel of which is valid is worked on without a mask and has every pixel written: a mask
// judged of every pixel would only send the engine down its pixel-by-pixel path. A rule its value decides is still
// judged into one.
TEST_F(PixelMathRunTest, AnIntegerTargetWhoseEveryPixelIsValidIsWorkedOnWithoutAMask)
{
    for (const auto& [type, fullScale] :
         {std::pair{an::SampleType::UInt8, 255.0}, std::pair{an::SampleType::UInt16, 65535.0}})
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        std::optional<an::consumer::ImageAsset> codes = filledAsset(
            session(), 4u, 2u, 1u, type, an::NumericDomain::NormalizedLinear,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return (x == 1u && y == 1u) ? 0.0 : 100.0; });
        ASSERT_TRUE(codes.has_value());
        {
            const an::Result<an::consumer::AssetReadLease> lease = codes->read();
            ASSERT_TRUE(lease.hasValue());
            ASSERT_EQ(metadataOf(*lease).semantics().validity().rule(), an::ValidityRule::AllValid);
        }
        const DirectRun every = runDirect(session(), *codes, expressionSettings("$T + 0.25"));
        const fixtures::FixtureSamples written = samplesOf(every);
        EXPECT_EQ(every.report.selectedBy, an::ValidityRule::AllValid);
        EXPECT_EQ(written.value(1u, 1u, 0u), std::nearbyint(0.25 * fullScale)) << "the zero code was not written";

        std::optional<an::consumer::ImageAsset> stated = withMetadata(session(), *codes, zeroIsNoDataMetadata());
        ASSERT_TRUE(stated.has_value());
        const DirectRun ruled = runDirect(session(), *stated, expressionSettings("$T + 0.25"));
        const fixtures::FixtureSamples kept = samplesOf(ruled);
        EXPECT_EQ(ruled.report.selectedBy, an::ValidityRule::ExplicitMask);
        EXPECT_EQ(kept.value(1u, 1u, 0u), 0.0) << "the rejected pixel was written";
    }
}

// The target's validity selects pixels only where the output is the target's own size: a new image of another size has
// every pixel written.
TEST_F(PixelMathRunTest, ANewImageOfAnotherSizeIgnoresTheTargetsValidity)
{
    std::optional<an::consumer::ImageAsset> codes = filledAsset(
        session(), 4u, 2u, 1u, an::SampleType::UInt16, an::NumericDomain::NormalizedLinear,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return (x == 1u && y == 1u) ? 0.0 : 1000.0; });
    ASSERT_TRUE(codes.has_value());
    std::optional<an::consumer::ImageAsset> stated = withMetadata(session(), *codes, zeroIsNoDataMetadata());
    ASSERT_TRUE(stated.has_value());
    PixelMathSettings settings = expressionSettings("0.5");
    settings.outputMode = PixelMathOutputMode::CreateNewImage;
    settings.width = 8u;
    settings.height = 4u;
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), *stated, settings));
    ASSERT_EQ(made.width(), 8u);
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            EXPECT_EQ(made.value(x, y, 0u), std::nearbyint(0.5 * 65535.0)) << "at " << x << ", " << y;
        }
    }
}

// An explicit mask selects the pixels written, every channel of one together, and travels beside the result.
TEST_F(PixelMathRunTest, AnExplicitMaskSelectsPixelsAndTravelsBesideTheResult)
{
    const an::consumer::ImageAsset plain = floats(3u, gradient);
    std::optional<an::ValidityMask> mask = explicitMask(
        session(), 8u, 6u, 1u, [](std::uint32_t, std::uint32_t x, std::uint32_t y) { return !(x == 2u && y == 1u); });
    ASSERT_TRUE(mask.has_value());
    std::optional<an::consumer::ImageAsset> target =
        withMetadata(session(), plain, explicitMaskMetadata(), std::move(mask));
    ASSERT_TRUE(target.has_value());
    const DirectRun run = runDirect(session(), *target, expressionSettings("$T + 0.5"));
    const fixtures::FixtureSamples made = samplesOf(run);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_FLOAT_EQ(made.value(2u, 1u, channel), static_cast<float>(gradient(2u, 1u, channel)))
            << "the rejected pixel was written in channel " << channel;
    }
    EXPECT_NEAR(made.value(3u, 1u, 1u), static_cast<float>(gradient(3u, 1u, 1u)) + 0.5, 1e-6);
    ASSERT_TRUE(run.result.has_value());
    an::Result<an::consumer::AssetReadLease> lease = run.result->read();
    ASSERT_TRUE(lease.hasValue());
    const an::ValidityMask* carried = lease->validity();
    ASSERT_NE(carried, nullptr) << "the mask did not travel with the result";
    const std::optional<an::ValidityMaskView<const std::uint8_t>> bits = carried->view();
    ASSERT_TRUE(bits.has_value());
    EXPECT_EQ(bits->test(0u, 2u, 1u), std::optional<bool>(false));
    EXPECT_EQ(bits->test(0u, 3u, 1u), std::optional<bool>(true));
}

// ---- The symbols.

// A local symbol is a variable every pixel starts from its stated value; a global one a constant, which an expression
// cannot assign.
TEST_F(PixelMathRunTest, LocalSymbolsAreVariablesAndGlobalSymbolsAreConstants)
{
    const an::consumer::ImageAsset target = floats(1u, gradient);
    PixelMathSettings settings = expressionSettings("a = a + $T; a * k");
    settings.localSymbols = "a = 0.25";
    settings.globalSymbols = "k = 2";
    const fixtures::FixtureSamples made = samplesOf(runDirect(session(), target, settings));
    for (std::uint32_t x = 0u; x < 8u; ++x)
    {
        EXPECT_NEAR(made.value(x, 2u, 0u), (0.25 + static_cast<float>(gradient(x, 2u, 0u))) * 2.0, 1e-6)
            << "a pixel did not start from the variable's stated value";
    }
    settings.expressions[0] = "k = 1; k";
    const DirectRun refused = runDirect(session(), target, settings);
    EXPECT_EQ(refused.status.code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(refused.status.message()).find("RGB/K expression"), std::string::npos)
        << refused.status.message();
}

// ---- The refusals.

// Every expression the engine refuses is said, under the tab it was written in, before any image is read.
TEST_F(PixelMathRunTest, EveryRefusedExpressionIsSaidUnderItsTab)
{
    const an::consumer::ImageAsset target = floats(3u, gradient);
    PixelMathSettings settings;
    settings.singleExpression = false;
    settings.expressions = {"$T +", "$T * (", "$T", ""};
    const DirectRun run = runDirect(session(), target, settings);
    EXPECT_EQ(run.status.code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(run.status.message()).find("R expression"), std::string::npos) << run.status.message();
    EXPECT_NE(std::string(run.status.message()).find("G expression"), std::string::npos) << run.status.message();
    EXPECT_EQ(std::string(run.status.message()).find("B expression"), std::string::npos) << run.status.message();

    const DirectRun unknown = runDirect(session(), target, expressionSettings("$T * nowhere"));
    EXPECT_EQ(unknown.status.code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(unknown.status.message()).find("nowhere"), std::string::npos) << unknown.status.message();

    PixelMathSettings empty;
    empty.expressions = {" ", "", "\n", ""};
    const DirectRun nothing = runDirect(session(), target, empty);
    EXPECT_EQ(nothing.status.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(std::string(nothing.status.message()), "The pixel math expression is empty");

    const DirectRun noMask = runDirect(session(), target, expressionSettings("$T * $M"));
    EXPECT_EQ(noMask.status.code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(noMask.status.message()).find("$M"), std::string::npos) << noMask.status.message();
}

// ---- The binding policy.

// An image of another size is resampled to the output's with Lanczos-4 before it is read; only the images an
// expression names are read.
TEST_F(PixelMathRunTest, AnImageOfAnotherSizeIsResampledToTheOutputAndOnlyNamedImagesAreRead)
{
    const an::consumer::ImageAsset target = floats(1u, gradient);
    PixelMathImageSet images;
    images.others.push_back(
        PixelMathNamedImage{.name = "large",
                            .asset = std::make_shared<const an::consumer::ImageAsset>(floats(
                                1u, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.3; }, 16u, 12u))});
    images.others.push_back(PixelMathNamedImage{
        .name = "unread", .asset = std::make_shared<const an::consumer::ImageAsset>(floats(3u, gradient, 5u, 5u))});
    const DirectRun run = runDirect(session(), target, expressionSettings("large + $T * 0"), images);
    const fixtures::FixtureSamples made = samplesOf(run);
    EXPECT_EQ(run.report.boundImages, (std::vector<std::string>{"$T", "large"}));
    EXPECT_EQ(made.width(), 8u);
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            EXPECT_NEAR(made.value(x, y, 0u), 0.3, 1e-5);
        }
    }
}

// Resampling is Lanczos-4: a ramp read at half its size lands between its samples, where a nearest sample cannot.
TEST_F(PixelMathRunTest, AnImageResampledToTheOutputIsInterpolatedNotPicked)
{
    // A ramp is reproduced exactly by any symmetric kernel, so the named image is a cosine of eight samples a period,
    // doubled to the target's size: Lanczos-4 lands on the curve where a bilinear sample falls short of its crest.
    constexpr double kPeriod = 8.0;
    const auto wave = [](double x) { return 0.5 + 0.4 * std::cos(2.0 * std::numbers::pi * (x - 16.0) / kPeriod); };
    const an::consumer::ImageAsset target = floats(1u, gradient, 64u, 24u);
    PixelMathImageSet images;
    images.others.push_back(PixelMathNamedImage{
        .name = "wave",
        .asset = std::make_shared<const an::consumer::ImageAsset>(floats(
            1u, [&wave](std::uint32_t x, std::uint32_t, std::uint32_t) { return wave(static_cast<double>(x)); }, 32u,
            12u))});
    const fixtures::FixtureSamples made =
        samplesOf(runDirect(session(), target, expressionSettings("wave + $T * 0"), images));
    // Around the crest at source column 16, where the dark-ringing guard keeps the kernel's answer.
    for (std::uint32_t x = 31u; x < 35u; ++x)
    {
        const double source = (static_cast<double>(x) + 0.5) / 2.0 - 0.5;
        const double below = std::floor(source);
        const double bilinear = wave(below) + (source - below) * (wave(below + 1.0) - wave(below));
        ASSERT_GT(std::abs(bilinear - wave(source)), 0.015) << "the wave tells the kernels apart at column " << x;
        EXPECT_NEAR(made.value(x, 6u, 0u), wave(source), 0.006) << "at column " << x;
    }
}

// Fractions of full scale are read together whatever their encoding; an image of physical quantities beside them is
// refused by the engine by name, not read as a fraction of something it is not.
TEST_F(PixelMathRunTest, ImagesOfAnotherKindOfNumberAreRefusedByName)
{
    const an::consumer::ImageAsset target = floats(1u, gradient);
    PixelMathImageSet images;
    images.others.push_back(PixelMathNamedImage{.name = "encoded",
                                                .asset = std::make_shared<const an::consumer::ImageAsset>(floats(
                                                    1u, gradient, 8u, 6u, an::NumericDomain::EncodedNonlinear))});
    images.others.push_back(PixelMathNamedImage{.name = "physical",
                                                .asset = std::make_shared<const an::consumer::ImageAsset>(
                                                    floats(1u, gradient, 8u, 6u, an::NumericDomain::PhysicalScaled))});
    const DirectRun together = runDirect(session(), target, expressionSettings("$T - encoded"), images);
    ASSERT_TRUE(together.status.ok()) << together.status.message();
    EXPECT_NEAR(samplesOf(together).value(5u, 5u, 0u), 0.0, 1e-7);
    const DirectRun refused = runDirect(session(), target, expressionSettings("$T - physical"), images);
    EXPECT_EQ(refused.status.code(), an::ErrorCode::InvalidArgument) << refused.status.message();
}

// The target's own view name reads the image being written, `$A` the active view's image or the target's when the
// target is active, and `$M` the target's mask.
TEST_F(PixelMathRunTest, TheTargetsNameTheActiveImageAndTheMaskReadWhatTheyName)
{
    const an::consumer::ImageAsset target = floats(1u, gradient);
    PixelMathImageSet images;
    images.targetName = "scene";
    const DirectRun named = runDirect(session(), target, expressionSettings("scene * 2"), images);
    EXPECT_NEAR(samplesOf(named).value(4u, 3u, 0u), static_cast<float>(gradient(4u, 3u, 0u)) * 2.0, 1e-6);
    const DirectRun activeIsTarget = runDirect(session(), target, expressionSettings("$A * 2"), images);
    EXPECT_NEAR(samplesOf(activeIsTarget).value(4u, 3u, 0u), static_cast<float>(gradient(4u, 3u, 0u)) * 2.0, 1e-6);
    images.active = std::make_shared<const an::consumer::ImageAsset>(
        floats(1u, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.125; }));
    images.mask = std::make_shared<const an::consumer::ImageAsset>(
        floats(1u, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 4u ? 0.0 : 1.0; }));
    const DirectRun both = runDirect(session(), target, expressionSettings("$A + $M"), images);
    const fixtures::FixtureSamples made = samplesOf(both);
    EXPECT_EQ(both.report.boundImages, (std::vector<std::string>{"$T", "$A", "$M"}));
    EXPECT_NEAR(made.value(1u, 1u, 0u), 0.125, 1e-7);
    EXPECT_NEAR(made.value(6u, 1u, 0u), 1.125, 1e-7);
}

// A new image takes the dialog's size, channels and representation: grey from colour by the weights 0.2989, 0.5870
// and 0.1140, colour from grey by repeating it, an alpha channel added at zero.
TEST_F(PixelMathRunTest, ANewImageTakesTheDialogsSizeChannelsAndRepresentation)
{
    const an::consumer::ImageAsset colour =
        floats(3u, [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.2 * (c + 1); });
    PixelMathSettings settings = expressionSettings("$T");
    settings.outputMode = PixelMathOutputMode::CreateNewImage;
    settings.colorSpace = PixelMathColorSpace::Grayscale;
    settings.colorFormat = PixelMathColorFormat::UInt8;
    settings.width = 4u;
    settings.height = 3u;
    const fixtures::FixtureSamples grey = samplesOf(runDirect(session(), colour, settings));
    EXPECT_EQ(grey.width(), 4u);
    EXPECT_EQ(grey.height(), 3u);
    EXPECT_EQ(grey.channelCount(), 1u);
    EXPECT_EQ(grey.sampleType(), fixtures::FixtureSampleType::UInt8);
    const double luminance =
        0.2989 * static_cast<float>(0.2) + 0.5870 * static_cast<float>(0.4) + 0.1140 * static_cast<float>(0.6);
    EXPECT_NEAR(grey.value(2u, 1u, 0u), std::nearbyint(luminance * 255.0), 1.0);

    settings.colorSpace = PixelMathColorSpace::Rgba;
    settings.colorFormat = PixelMathColorFormat::Float32;
    settings.width = 0u;
    settings.height = 0u;
    const DirectRun alphaRun = runDirect(session(), colour, settings);
    ASSERT_TRUE(alphaRun.result.has_value()) << alphaRun.status.message();
    const std::optional<StoredSamples> withAlpha = storedSamples(*alphaRun.result);
    ASSERT_TRUE(withAlpha.has_value());
    ASSERT_EQ(withAlpha->channelCount, 4u);
    ASSERT_TRUE(alphaRun.result->descriptor().has_value());
    EXPECT_EQ(alphaRun.result->descriptor()->sampleType(), an::SampleType::Float32) << "the dialog's representation";
    EXPECT_EQ(alphaRun.report.working, an::SampleType::Float32);
    EXPECT_NEAR(withAlpha->value(0u, 0u, 1u), 0.4, 1e-6);
    EXPECT_EQ(withAlpha->value(0u, 0u, 3u), 0.0);

    // A double output is worked in double even when the target and everything read are single precision.
    settings.colorFormat = PixelMathColorFormat::Float64;
    const DirectRun wide = runDirect(session(), colour, settings);
    ASSERT_TRUE(wide.result.has_value()) << wide.status.message();
    EXPECT_EQ(wide.report.working, an::SampleType::Float64);
    ASSERT_TRUE(wide.result->descriptor().has_value());
    EXPECT_EQ(wide.result->descriptor()->sampleType(), an::SampleType::Float64);
    settings.colorFormat = PixelMathColorFormat::Float32;

    const an::consumer::ImageAsset mono = floats(1u, gradient);
    settings.colorSpace = PixelMathColorSpace::Rgb;
    const fixtures::FixtureSamples repeated = samplesOf(runDirect(session(), mono, settings));
    ASSERT_EQ(repeated.channelCount(), 3u);
    EXPECT_EQ(repeated.value(5u, 2u, 0u), repeated.value(5u, 2u, 2u));
}

// A result that replaces its target keeps its description, its cards and its solution and is recorded nonlinear; a
// new image of another size keeps the acquisition record, no solution and no card but the processing state.
TEST_F(PixelMathRunTest, TheResultsMetadataFollowsWhereItGoes)
{
    const an::consumer::ImageAsset plain = floats(3u, gradient);
    std::optional<an::consumer::ImageAsset> target = withMetadata(session(), plain, describedMetadata());
    ASSERT_TRUE(target.has_value());

    const DirectRun replaced = runDirect(session(), *target, expressionSettings("$T * 0.5"));
    ASSERT_TRUE(replaced.result.has_value()) << replaced.status.message();
    {
        an::Result<an::consumer::AssetReadLease> lease = replaced.result->read();
        ASSERT_TRUE(lease.hasValue());
        const an::ImageMetadata& metadata = metadataOf(*lease);
        EXPECT_TRUE(metadata.astrometry().present());
        EXPECT_NE(metadata.codec().find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
        const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata.codec());
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.operation, "pixelmath");
        EXPECT_NE(metadata.semantics().transferFunction(), an::TransferFunction::Linear);
        ASSERT_FALSE(metadata.provenance().entries().empty());
        EXPECT_EQ(metadata.provenance().entries().back().operation(), "an::executePointwiseProgram");
        EXPECT_NE(metadata.provenance().entries().back().detail().find("seed 7"), std::string::npos);
    }

    PixelMathSettings settings = expressionSettings("$T * 0.5");
    settings.outputMode = PixelMathOutputMode::CreateNewImage;
    settings.width = 4u;
    const DirectRun created = runDirect(session(), *target, settings);
    ASSERT_TRUE(created.result.has_value()) << created.status.message();
    an::Result<an::consumer::AssetReadLease> lease = created.result->read();
    ASSERT_TRUE(lease.hasValue());
    const an::ImageMetadata& metadata = metadataOf(*lease);
    EXPECT_FALSE(metadata.astrometry().present()) << "a solution of another size was kept";
    EXPECT_EQ(metadata.codec().find(an::CodecNamespace::Fits, "OBJECT"), nullptr);
    EXPECT_EQ(an::readProcessingStateCards(metadata.codec()).operation, "pixelmath");
    EXPECT_EQ(metadata.acquisition(), describedMetadata().acquisition());
}

// The user guide's coordinate example: a closed rectangle, a stated sample of another image and the normalized column
// - the region tests evaluated as the guide reads them.
TEST_F(PixelMathRunTest, TheRegionTestsAndTheStatedSampleAnswerAsTheGuidesExampleSays)
{
    const an::consumer::ImageAsset target =
        floats(1u, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.0; }, 3u, 2u);
    PixelMathImageSet images;
    images.others.push_back(PixelMathNamedImage{
        .name = "SRC",
        .asset = std::make_shared<const an::consumer::ImageAsset>(floats(
            1u, [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return 0.1 + 0.2 * x + 0.4 * y; }, 3u, 2u))});
    const DirectRun run =
        runDirect(session(), target, expressionSettings("iif(inrect(0, 0, 1, 0), pixel(SRC, x(), y()), X())"), images);
    const fixtures::FixtureSamples made = samplesOf(run);
    EXPECT_NEAR(made.value(0u, 0u, 0u), 0.1, 1e-6);
    EXPECT_NEAR(made.value(1u, 0u, 0u), 0.3, 1e-6) << "the rectangle holds both of its edges";
    EXPECT_NEAR(made.value(2u, 0u, 0u), 1.0, 1e-6);
    EXPECT_NEAR(made.value(1u, 1u, 0u), 0.5, 1e-6);
    ASSERT_EQ(run.report.runs.size(), 1u);
    EXPECT_FALSE(run.report.runs[0].fused);
    const fixtures::FixtureSamples ellipse =
        samplesOf(runDirect(session(), target, expressionSettings("inellipse(1, 0, 1, 1)")));
    EXPECT_EQ(ellipse.value(0u, 0u, 0u), 1.0);
    EXPECT_EQ(ellipse.value(1u, 1u, 0u), 1.0);
    EXPECT_EQ(ellipse.value(2u, 1u, 0u), 0.0);
}

// ---- The random stream.

// `random()` draws from the run's seed: the same seed makes the same image at any number of threads, another seed
// another image, and every frame of a sequence its own stream.
TEST_F(PixelMathRunTest, TheRandomStreamIsSeeded)
{
    const an::consumer::ImageAsset target = floats(1u, gradient, 32u, 24u);
    const fixtures::FixtureSamples first =
        samplesOf(runDirect(session(), target, expressionSettings("random()"), {}, 7u));
    const fixtures::FixtureSamples again =
        samplesOf(runDirect(session(), target, expressionSettings("random()"), {}, 7u));
    const fixtures::FixtureSamples other =
        samplesOf(runDirect(session(), target, expressionSettings("random()"), {}, 8u));
    EXPECT_EQ(first.digest(), again.digest());
    EXPECT_NE(first.digest(), other.digest());
    for (std::uint32_t y = 0u; y < 24u; ++y)
    {
        for (std::uint32_t x = 0u; x < 32u; ++x)
        {
            EXPECT_GE(first.value(x, y, 0u), 0.0);
            EXPECT_LE(first.value(x, y, 0u), 1.0);
        }
    }
    EXPECT_EQ(an::ui::processes::pixelMathFrameSeed(7u, 0u), 7u);
    EXPECT_NE(an::ui::processes::pixelMathFrameSeed(7u, 1u), an::ui::processes::pixelMathFrameSeed(7u, 2u));
    EXPECT_EQ(an::ui::processes::pixelMathFrameSeed(7u, 3u), an::ui::processes::pixelMathFrameSeed(7u, 3u));
}

// ---- The dialog's values.

// The channel each tab writes, and the description of a new image or a replaced target.
TEST(PixelMathDialogValuesTest, EachChannelTakesItsTabsExpressionAndTheOutputItsDescription)
{
    PixelMathSettings settings;
    settings.expressions = {" r ", "g", "b", "a"};
    settings.singleExpression = true;
    const auto single = an::ui::processes::pixelMathChannelExpressions(settings, 4u);
    ASSERT_EQ(single.size(), 4u);
    EXPECT_EQ(single[0].second, "r");
    EXPECT_EQ(single[2].second, "r");
    EXPECT_EQ(single[3].first, PixelMathExpressionTab::Alpha);
    settings.singleExpression = false;
    const auto separate = an::ui::processes::pixelMathChannelExpressions(settings, 3u);
    EXPECT_EQ(separate[1].second, "g");
    EXPECT_EQ(separate[2].second, "b");
    const auto grey = an::ui::processes::pixelMathChannelExpressions(settings, 1u);
    ASSERT_EQ(grey.size(), 1u);
    EXPECT_EQ(grey[0].second, "r");
    EXPECT_EQ(an::ui::processes::pixelMathTabText(PixelMathExpressionTab::RedOrAll, true), "RGB/K");
    EXPECT_EQ(an::ui::processes::pixelMathTabText(PixelMathExpressionTab::RedOrAll, false), "R");
}

// The symbol lists: each name once, with its last value, in name order; a definition with no name is dropped.
TEST(PixelMathDialogValuesTest, SymbolListsKeepEachNameOnceInOrder)
{
    const std::vector<an::ui::processes::PixelMathSymbol> symbols =
        an::ui::processes::parsePixelMathSymbols(" b = 2.5x, a=1 , c, =3, a = 4,d=1=2,e = +0.5, f = x, , ,=1=2");
    ASSERT_EQ(symbols.size(), 6u);
    EXPECT_EQ(symbols[0].name, "a");
    EXPECT_EQ(symbols[0].value, 4.0);
    EXPECT_EQ(symbols[1].name, "b");
    EXPECT_EQ(symbols[1].value, 2.5);
    EXPECT_EQ(symbols[2].name, "c");
    EXPECT_EQ(symbols[2].value, 0.0);
    EXPECT_EQ(symbols[3].name, "d");
    EXPECT_EQ(symbols[3].value, 0.0);
    EXPECT_EQ(symbols[4].name, "e");
    EXPECT_EQ(symbols[4].value, 0.5);
    EXPECT_EQ(symbols[5].name, "f");
    EXPECT_EQ(symbols[5].value, 0.0) << "a value that starts with no number is zero";
    EXPECT_TRUE(an::ui::processes::parsePixelMathSymbols("").empty());
}

// The settings file is version 1 of the PixelMath format: what this window writes it reads back, a file laid out by
// another writer is read as it was written, and another version is refused.
TEST(PixelMathSettingsFileTest, TheSettingsFileIsVersionOneOfTheFormat)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    PixelMathSettings written;
    written.expressions = {"$T * 2", "", "$T <= 0.5", "1"};
    written.singleExpression = false;
    written.localSymbols = "a = 1, b";
    written.globalSymbols = "k=0.5";
    written.rescale = true;
    written.rescaleLow = 0.125;
    written.rescaleHigh = 0.875;
    written.outputMode = PixelMathOutputMode::CreateNewImage;
    written.newImageId = "RESULT";
    written.width = 640u;
    written.height = 480u;
    written.colorSpace = PixelMathColorSpace::Rgba;
    written.colorFormat = PixelMathColorFormat::UInt16;
    written.singleThreaded = true;
    written.codeOptimization = false;
    const QString path = directory.filePath(QStringLiteral("expression.anpm"));
    ASSERT_TRUE(an::ui::processes::writePixelMathSettingsFile(path, written).ok());
    const an::Result<PixelMathSettings> read = an::ui::processes::readPixelMathSettingsFile(path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(read->expressions, written.expressions);
    EXPECT_EQ(read->singleExpression, written.singleExpression);
    EXPECT_EQ(read->localSymbols, written.localSymbols);
    EXPECT_EQ(read->globalSymbols, written.globalSymbols);
    EXPECT_EQ(read->rescale, written.rescale);
    EXPECT_EQ(read->rescaleLow, written.rescaleLow);
    EXPECT_EQ(read->rescaleHigh, written.rescaleHigh);
    EXPECT_EQ(read->outputMode, written.outputMode);
    EXPECT_EQ(read->newImageId, written.newImageId);
    EXPECT_EQ(read->width, written.width);
    EXPECT_EQ(read->height, written.height);
    EXPECT_EQ(read->colorSpace, written.colorSpace);
    EXPECT_EQ(read->colorFormat, written.colorFormat);
    EXPECT_EQ(read->singleThreaded, written.singleThreaded);
    EXPECT_EQ(read->codeOptimization, written.codeOptimization);

    // As an XML library lays one out: a declaration, tab indents and escaped operators.
    const QString indented = directory.filePath(QStringLiteral("indented.anpm"));
    {
        QFile file(indented);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("<?xml version=\"1.0\"?>\n<pixel_math version=\"1\">\n"
                   "\t<global_settings single_expr=\"true\" single_threaded=\"false\" process_64_bits=\"true\" "
                   "optimize=\"true\" rescale=\"false\" rescale_low=\"0\" rescale_high=\"1\" />\n"
                   "\t<output_settings output_mode=\"0\" new_image_id=\"PIXEL_MATH_IMAGE\" width=\"0\" height=\"0\" "
                   "color_space=\"0\" color_format=\"4\" />\n"
                   "\t<expression0>iif($T &gt; 0.4, $T - 0.1, 0)</expression0>\n\t<expression1 />\n"
                   "\t<expression2 />\n\t<expression3 />\n\t<local_symbols>x = 2</local_symbols>\n"
                   "\t<global_symbols />\n</pixel_math>\n");
    }
    const an::Result<PixelMathSettings> laidOut = an::ui::processes::readPixelMathSettingsFile(indented);
    ASSERT_TRUE(laidOut.hasValue()) << laidOut.status().message();
    EXPECT_EQ(laidOut->expressions[0], "iif($T > 0.4, $T - 0.1, 0)");
    EXPECT_TRUE(laidOut->singleExpression);
    EXPECT_FALSE(laidOut->singleThreaded);
    EXPECT_FALSE(laidOut->rescale);
    EXPECT_EQ(laidOut->localSymbols, "x = 2");
    EXPECT_EQ(laidOut->colorFormat, PixelMathColorFormat::Float32);

    // A file that is not version 1 of the format, or lacks its settings, is refused naming what is wrong.
    const QString wrong = directory.filePath(QStringLiteral("wrong.anpm"));
    {
        QFile file(wrong);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("<pixel_math version=\"2\"><global_settings/><output_settings/></pixel_math>");
    }
    const an::Result<PixelMathSettings> refused = an::ui::processes::readPixelMathSettingsFile(wrong);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
    EXPECT_NE(std::string(refused.status().message()).find("Version is not compatible"), std::string::npos)
        << refused.status().message();
    {
        QFile file(wrong);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("<pixel_math version=\"1\"><global_settings/></pixel_math>");
    }
    const an::Result<PixelMathSettings> partial = an::ui::processes::readPixelMathSettingsFile(wrong);
    ASSERT_FALSE(partial.hasValue());
    EXPECT_NE(std::string(partial.status().message()).find("No output_settings node found"), std::string::npos);
    EXPECT_FALSE(
        an::ui::processes::readPixelMathSettingsFile(directory.filePath(QStringLiteral("none.anpm"))).hasValue());
}

// A settings file that leaves an attribute out reads it as the window's default, as a window just opened shows it.
// Rescale and Single threaded read as on, which a window never starts with.
TEST(PixelMathSettingsFileTest, AnAttributeLeftOutReadsAsTheWindowsDefault)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sparse.anpm"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("<pixel_math version=\"1\"><global_settings/><output_settings/>"
                   "<expression0>$T</expression0></pixel_math>");
    }
    const an::Result<PixelMathSettings> read = an::ui::processes::readPixelMathSettingsFile(path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    const PixelMathSettings defaults;
    EXPECT_EQ(read->singleExpression, defaults.singleExpression);
    EXPECT_EQ(read->rescale, defaults.rescale);
    EXPECT_EQ(read->rescaleLow, defaults.rescaleLow);
    EXPECT_EQ(read->rescaleHigh, defaults.rescaleHigh);
    EXPECT_EQ(read->outputMode, defaults.outputMode);
    EXPECT_EQ(read->newImageId, defaults.newImageId);
    EXPECT_EQ(read->width, defaults.width);
    EXPECT_EQ(read->height, defaults.height);
    EXPECT_EQ(read->colorSpace, defaults.colorSpace);
    EXPECT_EQ(read->colorFormat, defaults.colorFormat);
    EXPECT_EQ(read->singleThreaded, defaults.singleThreaded);
    EXPECT_EQ(read->codeOptimization, defaults.codeOptimization);
}

} // namespace
