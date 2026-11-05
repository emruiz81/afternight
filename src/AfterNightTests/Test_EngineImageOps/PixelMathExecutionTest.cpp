// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for compiling an expression, evaluating it, lowering the fusable ones into the
// pointwise program, and running both over whole images.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathEvaluator.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathLowering.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 9u;
constexpr std::uint32_t kHeight = 5u;

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 3u,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return *extent;
}

/// One planar Float32 image the tests own, with a little row padding so a walk that assumes
/// contiguity is caught.
struct OwnedImage
{
    std::vector<float> samples;
    an::ImageLayout layout;
    an::ImageExtent extent;

    [[nodiscard]] float& at(std::uint32_t channel, std::uint32_t x, std::uint32_t y)
    {
        const std::uint64_t offset = static_cast<std::uint64_t>(channel) * layout.planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                     static_cast<std::uint64_t>(x) * sizeof(float);
        return *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(samples.data()) + offset);
    }

    [[nodiscard]] float read(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        const std::uint64_t offset = static_cast<std::uint64_t>(channel) * layout.planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
                                     static_cast<std::uint64_t>(x) * sizeof(float);
        return *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(samples.data()) + offset);
    }
};

[[nodiscard]] OwnedImage makeImage(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                   std::uint32_t rowPaddingSamples = 2u)
{
    const an::ImageExtent extent = extentOf(width, height, channels);
    const an::ImageLayoutRequest request{.extent = extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width + rowPaddingSamples) * sizeof(float);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    OwnedImage image{std::vector<float>(layout->totalBytes() / sizeof(float) + 1u, 0.0f), *layout, extent};
    return image;
}

/// A ramp that is different in every channel, so a channel mix-up shows up as a wrong number.
void fillRamp(OwnedImage& image, float scale = 1.0f)
{
    for (std::uint32_t channel = 0u; channel < image.extent.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < image.extent.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < image.extent.width(); ++x)
            {
                image.at(channel, x, y) = scale * (static_cast<float>(x) + 10.0f * static_cast<float>(y) +
                                                   100.0f * static_cast<float>(channel));
            }
        }
    }
}

[[nodiscard]] an::PixelMathImageView viewOf(const OwnedImage& image)
{
    return an::PixelMathImageView{an::SampleType::Float32, image.samples.data(), &image.layout};
}

[[nodiscard]] an::PixelMathMutableImageView mutableViewOf(OwnedImage& image)
{
    return an::PixelMathMutableImageView{an::SampleType::Float32, image.samples.data(), &image.layout};
}

[[nodiscard]] an::PixelMathImageDeclaration
declarationOf(const OwnedImage& image, an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    return an::PixelMathImageDeclaration{image.extent, an::SampleType::Float32, domain};
}

/// Parse, resolve and compile in one step, failing the case with the reason when any of them refuse.
[[nodiscard]] an::PixelMathProgram compileOrFail(std::string_view source, const an::PixelMathBindings& bindings)
{
    const an::PixelMathParse parsed = an::parsePixelMath(source);
    EXPECT_TRUE(parsed.succeeded()) << source << ": "
                                    << (parsed.succeeded() ? std::string{}
                                                           : an::describePixelMathDiagnostic(parsed.diagnostics()[0]));
    if (!parsed.succeeded())
    {
        return {};
    }
    const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings);
    EXPECT_TRUE(resolved.succeeded()) << source << ": "
                                      << (resolved.succeeded()
                                              ? std::string{}
                                              : an::describePixelMathDiagnostic(resolved.diagnostics()[0]));
    if (!resolved.succeeded())
    {
        return {};
    }
    an::Result<an::PixelMathProgram> compiled = an::compilePixelMath(resolved);
    EXPECT_TRUE(compiled.hasValue()) << source << ": " << compiled.status().message();
    return compiled.hasValue() ? std::move(*compiled) : an::PixelMathProgram{};
}

/// The value one compiled expression computes at one site, with no images bound but the target.
[[nodiscard]] double evaluateOnce(const an::PixelMathProgram& program, std::span<const an::PixelMathImageView> images,
                                  std::span<const double> statistics, std::uint32_t x = 0u, std::uint32_t y = 0u,
                                  std::uint32_t channel = 0u, std::uint64_t seed = 0u)
{
    std::vector<double> stack((std::max)(program.stackDepth(), std::uint32_t{1}), 0.0);
    std::vector<double> variables((std::max)(program.variableInitialValues().size(), std::size_t{1}), 0.0);
    an::PixelMathEvaluationScratch scratch;
    scratch.stack = std::span<double>(stack);
    scratch.variables = std::span<double>(variables);
    an::PixelMathEvaluationSite site{x, y, channel, seed};
    const an::Result<an::PixelMathColorPlans> plans = an::planPixelMathColors(program);
    EXPECT_TRUE(plans.hasValue()) << plans.status().message();
    const an::Result<double> value = an::evaluatePixelMathPixel(
        program, images, statistics, plans.hasValue() ? *plans : an::PixelMathColorPlans{}, site, scratch);
    EXPECT_TRUE(value.hasValue()) << value.status().message();
    return value.hasValue() ? *value : std::numeric_limits<double>::quiet_NaN();
}

/// A one-pixel target so an arithmetic case does not have to build an image to say what `1 + 1` is.
struct ScalarHarness
{
    OwnedImage target = makeImage(1u, 1u, 1u, 0u);
    an::PixelMathBindings bindings{};

    ScalarHarness()
    {
        EXPECT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
        EXPECT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    }

    [[nodiscard]] double value(std::string_view source)
    {
        const an::PixelMathProgram program = compileOrFail(source, bindings);
        if (program.instructions().empty())
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
        return evaluateOnce(program, images, {});
    }
};

} // namespace

TEST(PixelMathProgramTest, ConstantPoolingPreservesBothSignsOfZero)
{
    ScalarHarness harness;
    ASSERT_TRUE(harness.bindings.declareConstant("negativeZero", -0.0).ok());
    ASSERT_TRUE(harness.bindings.declareVariable("v", 0.0).ok());
    for (const auto source : {"v = 0; 1 / negativeZero", "v = negativeZero; 1 / 0"})
    {
        const double value = harness.value(source);
        EXPECT_TRUE(std::isinf(value));
        EXPECT_EQ(std::signbit(value), std::string_view(source).starts_with("v = 0;"));
    }
}

TEST(PixelMathProgramTest, ArithmeticMeansWhatItSays)
{
    ScalarHarness harness;
    EXPECT_DOUBLE_EQ(harness.value("1 + 2 * 3"), 7.0);
    EXPECT_DOUBLE_EQ(harness.value("(1 + 2) * 3"), 9.0);
    EXPECT_DOUBLE_EQ(harness.value("2 * 3 ^ 2"), 18.0) << "power binds tighter than multiplication";
    EXPECT_DOUBLE_EQ(harness.value("2 ^ 3 ^ 2"), 512.0) << "and groups to the right";
    EXPECT_DOUBLE_EQ(harness.value("-2 ^ 2"), -4.0);
    EXPECT_DOUBLE_EQ(harness.value("2 ^ -1"), 0.5);
    EXPECT_DOUBLE_EQ(harness.value("7 % 3"), 1.0);
    EXPECT_DOUBLE_EQ(harness.value("3--5"), 2.0);
    EXPECT_DOUBLE_EQ(harness.value("~0.25"), 0.75);
    EXPECT_DOUBLE_EQ(harness.value("!0"), 1.0);
    EXPECT_DOUBLE_EQ(harness.value("1 < 2"), 1.0);
    EXPECT_DOUBLE_EQ(harness.value("1 > 2"), 0.0);
    EXPECT_DOUBLE_EQ(harness.value("1 && 0"), 0.0);
    EXPECT_DOUBLE_EQ(harness.value("1 || 0"), 1.0);
}

TEST(PixelMathProgramTest, TheScalarFunctionsAreTheOnesTheirNamesPromise)
{
    ScalarHarness harness;
    EXPECT_DOUBLE_EQ(harness.value("abs(-3)"), 3.0);
    EXPECT_DOUBLE_EQ(harness.value("sqrt(9)"), 3.0);
    EXPECT_DOUBLE_EQ(harness.value("floor(1.7)"), 1.0);
    EXPECT_DOUBLE_EQ(harness.value("ceil(1.2)"), 2.0);
    EXPECT_DOUBLE_EQ(harness.value("trunc(-1.7)"), -1.0);
    EXPECT_DOUBLE_EQ(harness.value("sign(-2)"), -1.0);
    EXPECT_DOUBLE_EQ(harness.value("log10(1000)"), 3.0);
    EXPECT_DOUBLE_EQ(harness.value("log2(8)"), 3.0);
    EXPECT_NEAR(harness.value("ln(e())"), 1.0, 1e-12);
    EXPECT_NEAR(harness.value("pi()"), 3.14159265358979323846, 1e-15);
    EXPECT_DOUBLE_EQ(harness.value("clamp(5, 0, 1)"), 1.0);
    EXPECT_DOUBLE_EQ(harness.value("range(-5, 0, 1)"), 0.0);
    EXPECT_DOUBLE_EQ(harness.value("fac(5)"), 120.0);
    EXPECT_DOUBLE_EQ(harness.value("ncr(5, 2)"), 10.0);
    EXPECT_DOUBLE_EQ(harness.value("npr(5, 2)"), 20.0);
    EXPECT_NEAR(harness.value("atan2(1, 1)"), 3.14159265358979323846 / 4.0, 1e-12);
    // The midtone transfer function takes its midtone to one half.
    EXPECT_NEAR(harness.value("mtf(0.25, 0.25)"), 0.5, 1e-12);
}

TEST(PixelMathProgramTest, TheArgumentListStatisticsAreExact)
{
    ScalarHarness harness;
    EXPECT_DOUBLE_EQ(harness.value("min(4, 2, 9)"), 2.0);
    EXPECT_DOUBLE_EQ(harness.value("max(4, 2, 9)"), 9.0);
    EXPECT_DOUBLE_EQ(harness.value("sum(1, 2, 3, 4)"), 10.0);
    EXPECT_DOUBLE_EQ(harness.value("mean(1, 2, 3, 4)"), 2.5);
    EXPECT_DOUBLE_EQ(harness.value("med(1, 2, 3)"), 2.0);
    EXPECT_DOUBLE_EQ(harness.value("med(1, 2, 3, 4)"), 2.5) << "an even count averages the two middle ranks";
    EXPECT_DOUBLE_EQ(harness.value("var(2, 4, 4, 4, 5, 5, 7, 9)"), 4.0);
    EXPECT_DOUBLE_EQ(harness.value("sdev(2, 4, 4, 4, 5, 5, 7, 9)"), 2.0);
    EXPECT_DOUBLE_EQ(harness.value("mdev(1, 2, 3, 100)"), 1.0);
    EXPECT_DOUBLE_EQ(harness.value("mad(1, 2, 3, 100)"), 1.0);
    // asqr and ssqr are squares, as their names say, not square roots.
    EXPECT_DOUBLE_EQ(harness.value("asqr(1, 2, 3)"), 14.0 / 3.0);
    EXPECT_DOUBLE_EQ(harness.value("ssqr(1, 2, 3)"), 14.0);
    EXPECT_DOUBLE_EQ(harness.value("bwmv(5, 5, 5, 5)"), 0.0) << "no spread, no midvariance";
    EXPECT_GT(harness.value("bwmv(1, 2, 3, 4, 100)"), 0.0);
}

TEST(PixelMathProgramTest, OnlyTheChosenBranchOfAConditionalIsEvaluated)
{
    ScalarHarness harness;
    EXPECT_DOUBLE_EQ(harness.value("iif(1, 10, 20)"), 10.0);
    EXPECT_DOUBLE_EQ(harness.value("iif(0, 10, 20)"), 20.0);
    // If the unchosen branch ran, this would be a division by zero rather than five.
    EXPECT_DOUBLE_EQ(harness.value("iif(0, 1/0, 5)"), 5.0);
    EXPECT_DOUBLE_EQ(harness.value("iif(iif(1, 0, 1), 1, 2)"), 2.0);
}

TEST(PixelMathProgramTest, AVariableIsSetBeforeEveryPixelAndAnAssignmentIsAValue)
{
    OwnedImage target = makeImage(1u, 1u, 1u, 0u);
    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    ASSERT_TRUE(bindings.declareVariable("v", 7.0).ok());

    const an::PixelMathProgram program = compileOrFail("v", bindings);
    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    EXPECT_DOUBLE_EQ(evaluateOnce(program, images, {}), 7.0);

    const an::PixelMathProgram assigned = compileOrFail("v = 3; v * 2", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(assigned, images, {}), 6.0);

    const an::PixelMathProgram compound = compileOrFail("v += 3; v", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(compound, images, {}), 10.0);

    const an::PixelMathProgram valued = compileOrFail("v = 4", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(valued, images, {}), 4.0) << "an assignment is worth what it stored";
}

TEST(PixelMathProgramTest, GeometryIsAConstantAndPositionIsNot)
{
    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};

    const an::PixelMathProgram geometry = compileOrFail("w() * 1000 + h() * 10 + n()", bindings);
    for (const an::PixelMathInstruction& instruction : geometry.instructions())
    {
        EXPECT_TRUE(instruction.opcode == an::PixelMathOpcode::PushConstant ||
                    instruction.opcode == an::PixelMathOpcode::ApplyBinary)
            << "a bound image's geometry is known while compiling, so nothing reads it per pixel";
    }
    EXPECT_DOUBLE_EQ(evaluateOnce(geometry, images, {}), kWidth * 1000.0 + kHeight * 10.0 + 3.0);
    EXPECT_DOUBLE_EQ(evaluateOnce(compileOrFail("iscolor()", bindings), images, {}), 1.0);

    const an::PixelMathProgram position = compileOrFail("x() * 100 + y() * 10 + c()", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(position, images, {}, 3u, 2u, 1u), 321.0);

    const an::PixelMathProgram normalized = compileOrFail("X()", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(normalized, images, {}, kWidth - 1u, 0u, 0u), 1.0);
    EXPECT_DOUBLE_EQ(evaluateOnce(normalized, images, {}, 0u, 0u, 0u), 0.0);
}

// The two region tests answer at the pixel being written: a rectangle whose truncated corner and size hold both of its
// edges, and an ellipse by its equation. Through the scalar evaluator and the image walk.
TEST(PixelMathProgramTest, TheRegionTestsAnswerAtThePixelBeingWritten)
{
    OwnedImage target = makeImage(kWidth, kHeight, 1u);
    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};

    const an::PixelMathProgram rectangle = compileOrFail("inrect(1.9, 1, 2.7, 0)", bindings);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 5u; ++x)
        {
            const double inside = (x >= 1u && x <= 3u && y == 1u) ? 1.0 : 0.0;
            EXPECT_DOUBLE_EQ(evaluateOnce(rectangle, images, {}, x, y), inside) << "at " << x << ", " << y;
        }
    }
    EXPECT_DOUBLE_EQ(evaluateOnce(compileOrFail("inrect(0, 0, 1 / 0, 1)", bindings), images, {}, 0u, 0u), 0.0)
        << "a size that is not finite holds no pixel";

    const an::PixelMathProgram ellipse = compileOrFail("inellipse(2, 1, 2, 1)", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(ellipse, images, {}, 2u, 1u), 1.0);
    EXPECT_DOUBLE_EQ(evaluateOnce(ellipse, images, {}, 4u, 1u), 1.0) << "the ellipse holds its edge";
    EXPECT_DOUBLE_EQ(evaluateOnce(ellipse, images, {}, 2u, 2u), 1.0);
    EXPECT_DOUBLE_EQ(evaluateOnce(ellipse, images, {}, 4u, 2u), 0.0);
    EXPECT_DOUBLE_EQ(evaluateOnce(ellipse, images, {}, 5u, 1u), 0.0);

    // The walk evaluates a run of samples at a time and reaches the same answers.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    OwnedImage destination = makeImage(kWidth, kHeight, 1u, 1u);
    const an::PixelMathProgram both = compileOrFail("inrect(1, 0, 2, 1) + 2 * inellipse(0, 0, 1.5, 1.5)", bindings);
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, both, request);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const double inRectangle = (x >= 1u && x <= 3u && y <= 1u) ? 1.0 : 0.0;
            const double inEllipse = (x * x + y * y) / 2.25 <= 1.0 ? 1.0 : 0.0;
            EXPECT_FLOAT_EQ(destination.read(0u, x, y), static_cast<float>(inRectangle + 2.0 * inEllipse))
                << "at " << x << ", " << y;
        }
    }
}

TEST(PixelMathProgramTest, ASampleOutsideAnImageIsZeroRatherThanAFault)
{
    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    fillRamp(target);
    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};

    const an::PixelMathProgram inside = compileOrFail("pixel($T, 2, 1, 2)", bindings);
    EXPECT_DOUBLE_EQ(evaluateOnce(inside, images, {}), 212.0);
    for (const std::string_view source :
         {"pixel($T, 1000, 0, 0)", "pixel($T, 0, 1000, 0)", "pixel($T, 0, 0, 7)", "pixel($T, -1, 0, 0)"})
    {
        const an::PixelMathProgram outside = compileOrFail(source, bindings);
        EXPECT_DOUBLE_EQ(evaluateOnce(outside, images, {}), 0.0) << source;
    }
}

TEST(PixelMathProgramTest, ADeepExpressionIsRefusedRatherThanCompiled)
{
    ScalarHarness harness;
    an::PixelMathParseLimits limits = an::defaultPixelMathParseLimits();
    limits.maxDepth = an::kMaxPixelMathCompileDepth + 8u;
    std::string source(an::kMaxPixelMathCompileDepth + 4u, '(');
    source += "1";
    source.append(an::kMaxPixelMathCompileDepth + 4u, ')');
    const an::PixelMathParse parsed = an::parsePixelMath(source, limits);
    ASSERT_TRUE(parsed.succeeded()) << an::describePixelMathDiagnostic(parsed.diagnostics()[0]);
    const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), harness.bindings);
    ASSERT_TRUE(resolved.succeeded());
    const an::Result<an::PixelMathProgram> compiled = an::compilePixelMath(resolved);
    ASSERT_FALSE(compiled.hasValue());
    EXPECT_EQ(compiled.status().code(), an::ErrorCode::LimitExceeded);
}

TEST(PixelMathExecutionTest, AChainIsWrittenIntoEveryChannelOfADestination)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    OwnedImage other = makeImage(kWidth, kHeight, 3u);
    OwnedImage destination = makeImage(kWidth, kHeight, 3u, 1u);
    fillRamp(target, 0.001f);
    fillRamp(other, 0.0005f);

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindImage("B", declarationOf(other)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T * 2 + B", bindings);

    const std::array<an::PixelMathImageView, 2u> images{viewOf(target), viewOf(other)};
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, program, request);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->run.selection.total, static_cast<std::uint64_t>(kWidth) * kHeight * 3u);
    EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight * 3u);
    EXPECT_EQ(ran->statisticPasses, 0u);

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const double expected = static_cast<double>(target.read(channel, x, y)) * 2.0 +
                                        static_cast<double>(other.read(channel, x, y));
                EXPECT_FLOAT_EQ(destination.read(channel, x, y), static_cast<float>(expected));
            }
        }
    }
}

TEST(PixelMathExecutionTest, OneChannelAtATimeIsAnOrdinaryRequestRatherThanAMode)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    OwnedImage destination = makeImage(kWidth, kHeight, 3u);
    fillRamp(target, 0.001f);

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T + 1", bindings);
    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};

    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    request.destinationChannel = 1u;
    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, program, request);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_FLOAT_EQ(destination.read(1u, 3u, 2u), target.read(1u, 3u, 2u) + 1.0f);
    EXPECT_FLOAT_EQ(destination.read(0u, 3u, 2u), 0.0f) << "the other channels were not written";

    request.destinationChannel = 3u;
    EXPECT_EQ(an::executePixelMath(execution, program, request).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(PixelMathExecutionTest, AWholeImageStatisticIsComputedOnceAndUsedEverywhere)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(4u, 4u, 1u);
    OwnedImage destination = makeImage(4u, 4u, 1u);
    float value = 0.0f;
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            target.at(0u, x, y) = value;
            value += 1.0f;
        }
    }

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T - med($T)", bindings);
    ASSERT_EQ(program.statistics().size(), 1u);

    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, program, request);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_GT(ran->statisticPasses, 0u);

    // Sixteen values from zero to fifteen: the median of the two middle ranks is seven and a half.
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_FLOAT_EQ(destination.read(0u, x, y), target.read(0u, x, y) - 7.5f);
        }
    }
}

TEST(PixelMathExecutionTest, EveryAdvancedWholeImageStatisticExecutesThroughTheSharedOperation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    OwnedImage target = makeImage(4u, 2u, 1u, 0u);
    OwnedImage destination = makeImage(4u, 2u, 1u, 0u);
    const std::array<float, 8u> values{0.1f, 0.2f, 0.4f, 0.8f, 0.15f, 0.3f, 0.6f, 0.9f};
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            target.at(0u, x, y) = values[static_cast<std::size_t>(y) * 4u + x];
        }
    }

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program =
        compileOrFail("adev($T) + asqr($T) + ssqr($T) + bwmv($T) + noise($T)", bindings);
    ASSERT_EQ(program.statistics().size(), 5u);

    const an::Result<an::AdvancedStatistics> expected = an::computeImageAdvancedStatistics(
        execution, an::SampleType::Float32, target.samples.data(), target.layout, 0u);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    const double expectedValue = expected->averageAbsoluteDeviation + expected->meanOfSquares + expected->sumOfSquares +
                                 expected->biweightMidvariance + expected->backgroundNoise;

    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, program, request);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_GT(ran->statisticPasses, 0u);
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_NEAR(destination.read(0u, x, y), expectedValue, 1.0e-5);
        }
    }
}

TEST(PixelMathExecutionTest, ARandomExpressionGivesTheSameImageAtEveryThreadCount)
{
    const an::EngineContext engine = makeEngine();
    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    OwnedImage one = makeImage(kWidth, kHeight, 3u);
    OwnedImage many = makeImage(kWidth, kHeight, 3u);
    OwnedImage reference = makeImage(kWidth, kHeight, 3u);
    fillRamp(target, 0.001f);

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T + random() * 0.1 + random() * 0.01", bindings);
    EXPECT_TRUE(program.usesRandom());

    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    for (OwnedImage* destination : {&one, &many})
    {
        const an::ExecutionContext execution = makeExecution(engine, destination == &one ? 1u : 4u);
        an::PixelMathExecutionRequest request;
        request.images = images;
        request.destination = mutableViewOf(*destination);
        request.randomSeed = 0x1234'5678'9ABC'DEF0ull;
        ASSERT_TRUE(an::executePixelMath(execution, program, request).hasValue());
    }

    {
        const an::ExecutionContext execution = makeExecution(engine, 1u, an::ComputeBackend::Reference);
        an::PixelMathExecutionRequest request;
        request.images = images;
        request.destination = mutableViewOf(reference);
        request.randomSeed = 0x1234'5678'9ABC'DEF0ull;
        ASSERT_TRUE(an::executePixelMath(execution, program, request).hasValue());
    }

    bool sawSomethingRandom = false;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_FLOAT_EQ(one.read(channel, x, y), many.read(channel, x, y));
                EXPECT_FLOAT_EQ(one.read(channel, x, y), reference.read(channel, x, y));
                sawSomethingRandom = sawSomethingRandom || one.read(channel, x, y) != target.read(channel, x, y);
            }
        }
    }
    EXPECT_TRUE(sawSomethingRandom);
}

TEST(PixelMathExecutionTest, InPlaceIsAllowedExactlyWhenAWriteCannotChangeALaterRead)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    fillRamp(target, 0.001f);
    const OwnedImage before = target;

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());

    const an::PixelMathProgram currentChannel = compileOrFail("$T * 2", bindings);
    EXPECT_TRUE(currentChannel.supportsInPlace());
    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(target);
    ASSERT_TRUE(an::executePixelMath(execution, currentChannel, request).hasValue());
    EXPECT_FLOAT_EQ(target.read(2u, 4u, 3u), before.read(2u, 4u, 3u) * 2.0f);

    const an::PixelMathProgram statedChannel = compileOrFail("$T[0] * 2", bindings);
    EXPECT_FALSE(statedChannel.supportsInPlace());
    EXPECT_EQ(an::executePixelMath(execution, statedChannel, request).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(PixelMathExecutionTest, AResultAboveOneSurvivesRatherThanBeingClamped)
{
    // Clamping every expression result to the unit interval whatever the image says its numbers
    // mean would silently destroy a raw-code or physically scaled frame. This engine narrows to the
    // destination representation and leaves the value alone.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(4u, 4u, 1u);
    OwnedImage destination = makeImage(4u, 4u, 1u);
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            target.at(0u, x, y) = 0.75f;
        }
    }

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T * 4 - 1", bindings);

    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    ASSERT_TRUE(an::executePixelMath(execution, program, request).hasValue());
    EXPECT_FLOAT_EQ(destination.read(0u, 1u, 1u), 2.0f);
}

TEST(PixelMathExecutionTest, AnInvalidPixelLeavesItsDestinationSamplesAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(kWidth, kHeight, 1u);
    OwnedImage destination = makeImage(kWidth, kHeight, 1u);
    fillRamp(target, 0.001f);
    target.at(0u, 2u, 1u) = std::numeric_limits<float>::quiet_NaN();
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            destination.at(0u, x, y) = -1.0f;
        }
    }

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T + 1", bindings);

    const std::array<an::PixelMathImageView, 1u> images{viewOf(target)};
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = mutableViewOf(destination);
    const an::ValidityDescriptorRequest finiteOnly{.rule = an::ValidityRule::FiniteOnly};
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(finiteOnly);
    ASSERT_TRUE(descriptor.has_value());
    request.validity.descriptor = *descriptor;

    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, program, request);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->run.selection.valid, static_cast<std::uint64_t>(kWidth) * kHeight - 1u);
    EXPECT_FLOAT_EQ(destination.read(0u, 2u, 1u), -1.0f);
    EXPECT_FLOAT_EQ(destination.read(0u, 3u, 1u), target.read(0u, 3u, 1u) + 1.0f);
}

TEST(PixelMathLoweringTest, TheChainsBecomeAFusedProgramAndTheRestSayWhyNot)
{
    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    OwnedImage other = makeImage(kWidth, kHeight, 3u);
    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindImage("B", declarationOf(other)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    ASSERT_TRUE(bindings.declareVariable("v", 0.0).ok());

    const auto lowerOf = [&](std::string_view source) {
        const an::PixelMathProgram program = compileOrFail(source, bindings);
        an::Result<an::PixelMathLowering> lowered = an::lowerPixelMathToPointwise(program);
        EXPECT_TRUE(lowered.hasValue()) << source << ": " << lowered.status().message();
        return lowered.hasValue() ? std::move(*lowered) : an::PixelMathLowering{};
    };

    for (const std::string_view source : {"$T", "$T * 2", "$T + 1", "$T - 0.5", "$T / 4", "$T ^ 2", "-$T", "~$T",
                                          "$T * 2 + B", "clamp($T * 2, 0, 1)", "min($T, B)", "max($T, B)", "$T > B"})
    {
        const an::PixelMathLowering lowered = lowerOf(source);
        EXPECT_TRUE(lowered.lowered()) << source << ": " << an::pixelMathLoweringBlockerName(lowered.blocker());
        if (lowered.lowered())
        {
            ASSERT_TRUE(lowered.program().has_value());
            EXPECT_FALSE(lowered.program()->steps().empty()) << source;
            EXPECT_EQ(lowered.sourceImages()[0], 0u) << source;
        }
    }

    const std::array<std::pair<std::string_view, an::PixelMathLoweringBlocker>, 12u> blocked{{
        {"$T + x()", an::PixelMathLoweringBlocker::UsesPixelPosition},
        {"$T + random()", an::PixelMathLoweringBlocker::UsesRandom},
        {"v = $T; v", an::PixelMathLoweringBlocker::UsesVariable},
        {"iif($T > 0, 1, 0)", an::PixelMathLoweringBlocker::UsesBranch},
        {"pixel($T, 0, 0)", an::PixelMathLoweringBlocker::UsesImageSample},
        {"CIEL($T)", an::PixelMathLoweringBlocker::UsesColorComponent},
        {"$T - med($T)", an::PixelMathLoweringBlocker::UsesImageStatistic},
        {"$T[1] * 2", an::PixelMathLoweringBlocker::ChannelSelectionNotFusable},
        {"$T % 2", an::PixelMathLoweringBlocker::OperationNotFusable},
        {"1 - $T", an::PixelMathLoweringBlocker::NotAChain},
        {"$T + B * 1", an::PixelMathLoweringBlocker::NotAChain},
        {"$T + (B - 1)", an::PixelMathLoweringBlocker::NotAChain},
    }};
    for (const auto& [source, blocker] : blocked)
    {
        const an::PixelMathLowering lowered = lowerOf(source);
        EXPECT_FALSE(lowered.lowered()) << source;
        EXPECT_EQ(lowered.blocker(), blocker)
            << source << " reported " << an::pixelMathLoweringBlockerName(lowered.blocker());
        EXPECT_FALSE(lowered.program().has_value()) << source;
    }

    // Dividing by three has no exact reciprocal, so multiplying by one third would not be the same
    // number the interpreter computes. Dividing by four does.
    EXPECT_FALSE(lowerOf("$T / 3").lowered());
    EXPECT_TRUE(lowerOf("$T / 4").lowered());
}

TEST(PixelMathLoweringTest, TheFusedChainComputesWhatTheInterpreterComputes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(kWidth, kHeight, 3u, 0u);
    OwnedImage other = makeImage(kWidth, kHeight, 3u, 0u);
    fillRamp(target, 0.001f);
    fillRamp(other, 0.0005f);

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindImage("B", declarationOf(other)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());

    for (const std::string_view source :
         {"$T * 2 + B", "clamp($T - B, 0, 1)", "~$T", "$T / 4 + 0.25", "min($T, B) * 8", "$T * 2 * B"})
    {
        const an::PixelMathProgram program = compileOrFail(source, bindings);
        an::Result<an::PixelMathLowering> lowered = an::lowerPixelMathToPointwise(program);
        ASSERT_TRUE(lowered.hasValue()) << source << ": " << lowered.status().message();
        ASSERT_TRUE(lowered->lowered()) << source << ": " << an::pixelMathLoweringBlockerName(lowered->blocker());

        OwnedImage interpreted = makeImage(kWidth, kHeight, 3u, 0u);
        OwnedImage fused = makeImage(kWidth, kHeight, 3u, 0u);
        const std::array<an::PixelMathImageView, 2u> everything{viewOf(target), viewOf(other)};
        // An expression that names one image binds one: the program's own count is what an
        // execution has to match, and the target is always its first.
        const std::span<const an::PixelMathImageView> images(everything.data(), program.images().size());

        an::PixelMathExecutionRequest request;
        request.images = images;
        request.destination = mutableViewOf(interpreted);
        ASSERT_TRUE(an::executePixelMath(execution, program, request).hasValue()) << source;

        std::vector<an::PointwiseImageView> sources;
        for (const std::uint32_t image : lowered->sourceImages())
        {
            sources.push_back(an::PointwiseImageView{images[image].samples, images[image].layout});
        }
        const an::PointwiseMutableImageView destination{fused.samples.data(), &fused.layout};
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            execution, *lowered->program(), std::span<const an::PointwiseImageView>(sources), destination,
            an::KernelValidityView{});
        ASSERT_TRUE(ran.hasValue()) << source << ": " << ran.status().message();

        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    EXPECT_FLOAT_EQ(fused.read(channel, x, y), interpreted.read(channel, x, y))
                        << source << " at " << x << "," << y << "," << channel;
                }
            }
        }
    }
}

TEST(PixelMathExecutionTest, TheBoundImagesHaveToBeWhatTheExpressionWasResolvedAgainst)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    OwnedImage target = makeImage(kWidth, kHeight, 3u);
    OwnedImage destination = makeImage(kWidth, kHeight, 3u);
    OwnedImage wrongShape = makeImage(kWidth, kHeight + 1u, 3u);

    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const an::PixelMathProgram program = compileOrFail("$T + 1", bindings);

    const std::array<an::PixelMathImageView, 1u> wrong{viewOf(wrongShape)};
    an::PixelMathExecutionRequest request;
    request.images = wrong;
    request.destination = mutableViewOf(destination);
    EXPECT_EQ(an::executePixelMath(execution, program, request).status().code(), an::ErrorCode::InvalidArgument);

    const std::array<an::PixelMathImageView, 1u> right{viewOf(target)};
    request.images = right;
    request.destination = mutableViewOf(wrongShape);
    EXPECT_EQ(an::executePixelMath(execution, program, request).status().code(), an::ErrorCode::IncompatibleArguments);

    const std::array<an::PixelMathImageView, 2u> tooMany{viewOf(target), viewOf(target)};
    request.images = tooMany;
    request.destination = mutableViewOf(destination);
    EXPECT_EQ(an::executePixelMath(execution, program, request).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(PixelMathLoweringTest, NonTonePowerRemainsAvailableToTheInterpreter)
{
    auto target = makeImage(kWidth, kHeight, 1u);
    an::PixelMathBindings bindings;
    ASSERT_TRUE(bindings.bindImage("T", declarationOf(target, an::NumericDomain::PhysicalScaled)).ok());
    ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    const auto program = compileOrFail("$T ^ 2", bindings);
    const auto lowered = an::lowerPixelMathToPointwise(program);
    ASSERT_TRUE(lowered.hasValue()) << lowered.status().message();
    EXPECT_FALSE(lowered->lowered());
    EXPECT_EQ(lowered->blocker(), an::PixelMathLoweringBlocker::OperationNotFusable);
}
