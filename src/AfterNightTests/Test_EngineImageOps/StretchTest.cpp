// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the retained generalized-stretch and screen-transfer families.

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <chrono>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, {});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                       bool interleaved = false)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = sizeof(float),
                                                       .samplesPerPixel = interleaved ? channels : 1u,
                                                       .planeCount = interleaved ? 1u : channels,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] float& sampleAt(std::vector<float>& samples, const an::ImageLayout& layout, std::uint32_t channel,
                              std::uint32_t x, std::uint32_t y)
{
    const std::uint64_t offset =
        *layout.sampleByteOffset(channel / layout.samplesPerPixel(), y, x, channel % layout.samplesPerPixel());
    return *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(samples.data()) + offset);
}

[[nodiscard]] an::GeneralizedStretchParameters typical(an::StretchFunction function)
{
    an::GeneralizedStretchParameters parameters;
    parameters.function = function;
    parameters.amount = 2.5;
    parameters.lowProtection = 0.1;
    parameters.symmetryPoint = 0.35;
    parameters.highProtection = 0.9;
    return parameters;
}

[[nodiscard]] double referenceLogGhs(double x, const an::GeneralizedStretchParameters& p)
{
    const double qlp = -std::log1p(p.amount * (p.symmetryPoint - p.lowProtection));
    const double q0 = qlp - p.amount * p.lowProtection / (1.0 + p.amount * (p.symmetryPoint - p.lowProtection));
    const double qwp = std::log1p(p.amount * (p.highProtection - p.symmetryPoint));
    const double q1 =
        qwp + p.amount * (1.0 - p.highProtection) / (1.0 + p.amount * (p.highProtection - p.symmetryPoint));
    const double q = 1.0 / (q1 - q0);
    if (x < p.lowProtection)
    {
        return p.amount / (1.0 + p.amount * (p.symmetryPoint - p.lowProtection)) * q * x;
    }
    if (x < p.symmetryPoint)
    {
        return -q0 * q - q * std::log(1.0 + p.amount * p.symmetryPoint - p.amount * x);
    }
    if (x < p.highProtection)
    {
        return -q0 * q + q * std::log(1.0 - p.amount * p.symmetryPoint + p.amount * x);
    }
    return (qwp - q0 - p.amount * p.highProtection / (1.0 + p.amount * (p.highProtection - p.symmetryPoint))) * q +
           q * p.amount / (1.0 + p.amount * (p.highProtection - p.symmetryPoint)) * x;
}

[[nodiscard]] double referenceHistogram(double x, const an::GeneralizedStretchParameters& p)
{
    const double m = 1.0 / (2.0 * (p.amount + 1.0));
    const double lowDenominator = (1.0 - 2.0 * m) * (p.lowProtection - p.symmetryPoint) - m;
    const double highDenominator = (2.0 * m - 1.0) * (p.highProtection - p.symmetryPoint) - m;
    const double qlp = (m - 1.0) * (p.lowProtection - p.symmetryPoint) / lowDenominator;
    const double q0 = qlp + p.lowProtection * (m - 1.0) * m * std::pow(lowDenominator, -2.0);
    const double qwp = (m - 1.0) * (p.highProtection - p.symmetryPoint) / highDenominator;
    const double q1 = qwp + (p.highProtection - 1.0) * (m - 1.0) * m * std::pow(highDenominator, -2.0);
    const double q = 1.0 / (q1 - q0);
    const double b1 = m * (1.0 - m) * std::pow(lowDenominator, -2.0) * q;
    const double a2 = -q0 * q;
    const double b2 = (m - 1.0) * q;
    const double c2 = -b2 * p.symmetryPoint;
    const double d2 = 1.0 - 2.0 * m;
    const double e2 = -d2 * p.symmetryPoint - m;
    const double a3 = -q0 * q;
    const double b3 = (m - 1.0) * q;
    const double c3 = -b3 * p.symmetryPoint;
    const double d3 = 2.0 * m - 1.0;
    const double e3 = -d3 * p.symmetryPoint - m;
    const double a4 = (qwp - p.highProtection * (1.0 - m) * m * std::pow(highDenominator, -2.0) - q0) * q;
    const double b4 = -m * (m - 1.0) * std::pow(highDenominator, -2.0) * q;
    if (x < p.lowProtection)
    {
        return b1 * x;
    }
    if (x < p.symmetryPoint)
    {
        return a2 + (b2 * x + c2) / (d2 * x + e2);
    }
    if (x < p.highProtection)
    {
        return a3 + (b3 * x + c3) / (d3 * x + e3);
    }
    return a4 + b4 * x;
}

[[nodiscard]] std::array<double, 3u> toLab(const std::array<double, 3u>& rgb)
{
    an::ColorEncoding source;
    source.transfer = an::ColorTransferFunction::Srgb;
    an::ColorEncoding destination;
    destination.schema = an::ChannelSchema::Lab;
    destination.perceptual = an::PerceptualEncoding::UnitPacked;
    const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(source, destination);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    std::array<double, 3u> lab{};
    EXPECT_TRUE(an::convertColorPixel(*plan, rgb, lab).ok());
    return lab;
}

} // namespace

namespace
{
// Integrate the positive derivative over the four original pieces. This oracle neither
// imports planned coefficients nor solves the rational inverse used by the implementation.
[[nodiscard]] long double histogramIntegral(long double x, const an::GeneralizedStretchParameters& p)
{
    const long double low = p.lowProtection;
    const long double center = p.symmetryPoint;
    const long double high = p.highProtection;
    if (p.amount > 1.0)
    {
        // Integrate after dividing the derivative by a common reciprocal scale. The
        // scale cancels in normalization and keeps Double subnormal protection tails
        // visible even on platforms whose long double has no wider exponent range.
        const long double h = 0.5L / p.amount;
        long double sum = ((std::min)(x, low) / (h + (center - low))) * (h / (h + (center - low)));
        if (x > low)
        {
            const long double end = (std::min)(x, center);
            sum += ((end - low) / (h + (center - low))) * (h / (h + (center - end)));
        }
        if (x > center)
        {
            const long double distance = (std::min)(x, high) - center;
            sum += distance / (h + distance);
        }
        if (x > high)
            sum += ((x - high) / (h + (high - center))) * (h / (h + (high - center)));
        return sum;
    }
    const long double k = 2.0L * p.amount;
    const auto slope = [&](long double t) { return 1.0L / (1.0L + k * std::abs(t - center)); };
    long double sum = (std::min)(x, low) * slope(low) * slope(low);
    if (x > low)
    {
        const long double end = (std::min)(x, center);
        sum += (end - low) * slope(low) * slope(end);
    }
    if (x > center)
    {
        const long double end = (std::min)(x, high);
        sum += (end - center) * slope(end);
    }
    if (x > high)
    {
        sum += (x - high) * slope(high) * slope(high);
    }
    return sum;
}

[[nodiscard]] double histogramInverseOracle(double y, const an::GeneralizedStretchParameters& p)
{
    if (y <= 0.0 || y >= 1.0)
    {
        return std::clamp(y, 0.0, 1.0);
    }
    const long double target = y * histogramIntegral(1.0L, p);
    long double low = 0.0L;
    long double high = 1.0L;
    for (unsigned iteration = 0; iteration < 100; ++iteration)
    {
        const long double middle = (low + high) * 0.5L;
        if (histogramIntegral(middle, p) < target)
            low = middle;
        else
            high = middle;
    }
    return static_cast<double>((low + high) * 0.5L);
}
} // namespace

TEST(StretchTest, InverseHistogramIdentityKeepsRepresentableEndpoints)
{
    auto parameters = typical(an::StretchFunction::InverseHistogram);
    parameters.amount = 0.0;
    for (double x : {0.0, std::numeric_limits<double>::denorm_min(), 1.0e-30, std::nextafter(1.0, 0.0), 1.0})
    {
        const auto result = an::evaluateGeneralizedStretch(x, parameters);
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(*result, x);
    }
    for (double amount : {0.0, 2.5})
    {
        parameters.amount = amount;
        const auto result = an::evaluateGeneralizedStretch(-0.0, parameters);
        ASSERT_TRUE(result.hasValue());
        EXPECT_FALSE(std::signbit(*result));
        const auto engine = makeEngine();
        const auto execution = makeExecution(engine);
        const auto layout = layoutOf(1u, 1u, 1u);
        float input = -0.0f;
        float output = -1.0f;
        an::StretchImageRequest request;
        request.stretch = parameters;
        ASSERT_TRUE(
            an::stretchImage(execution, an::SampleType::Float32, &input, layout, &output, layout, request).hasValue());
        EXPECT_EQ(output, 0.0f);
        EXPECT_FALSE(std::signbit(output));
    }
}

TEST(StretchTest, InverseHistogramSeededParametersAndCompleteSamplesMatchIndependentIntegral)
{
    std::mt19937_64 random(0x13a9d582u);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (unsigned trial = 0; trial < 512; ++trial)
    {
        auto parameters = typical(an::StretchFunction::InverseHistogram);
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        parameters.lowProtection = trial % 4 == 0 ? 0.0 : bounds[0];
        parameters.symmetryPoint = bounds[1];
        parameters.highProtection = trial % 4 == 0 ? 1.0 : bounds[2];
        parameters.amount = std::pow(10.0, -14.0 + 20.0 * unit());
        if (trial % 11 == 0)
            parameters.symmetryPoint = parameters.lowProtection;
        if (trial % 13 == 0)
            parameters.highProtection = parameters.symmetryPoint;
        if (trial % 17 == 0)
            parameters.amount = 0.0;
        SCOPED_TRACE(::testing::Message() << "seed=0x13a9d582 trial=" << trial << " amount=" << parameters.amount
                                          << " low=" << parameters.lowProtection << " center="
                                          << parameters.symmetryPoint << " high=" << parameters.highProtection);
        std::vector<float> input;
        for (const double boundary :
             {0.0, parameters.lowProtection, parameters.symmetryPoint, parameters.highProtection, 1.0})
        {
            const float y =
                static_cast<float>(histogramIntegral(boundary, parameters) / histogramIntegral(1.0L, parameters));
            input.push_back(std::nextafter(y, -1.0f));
            input.push_back(y);
            input.push_back(std::nextafter(y, 2.0f));
        }
        input.push_back(-10.0f);
        input.push_back(10.0f);
        input.push_back(-0.0f);
        while (input.size() < 257)
            input.push_back(static_cast<float>(unit()));
        std::sort(input.begin(), input.end());
        const auto layout = layoutOf(static_cast<std::uint32_t>(input.size()), 1u, 1u);
        std::vector<float> output(input.size(), -1.0f);
        an::StretchImageRequest request;
        request.stretch = parameters;
        const auto result =
            an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->run.outputsWritten, input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const double expected = histogramInverseOracle(input[i], parameters);
            EXPECT_NEAR(output[i], expected, 1.0e-6) << "sample=" << input[i];
            EXPECT_GE(output[i], 0.0f);
            EXPECT_LE(output[i], 1.0f);
            if (i)
                EXPECT_LE(output[i - 1], output[i]);
            const auto scalar = an::evaluateGeneralizedStretch(input[i], parameters);
            ASSERT_TRUE(scalar.hasValue());
            EXPECT_NEAR(*scalar, expected, 1.0e-9) << "sample=" << input[i];
        }
        request.stretch.function = an::StretchFunction::Histogram;
        ASSERT_TRUE(
            an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request)
                .hasValue());
        an::KernelValidityView finiteOnly;
        finiteOnly.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        std::vector<float> generalOutput(input.size());
        ASSERT_TRUE(an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, generalOutput.data(),
                                     layout, request, finiteOnly)
                        .hasValue());
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(output[i]), std::bit_cast<std::uint32_t>(generalOutput[i]));
            const auto expected =
                histogramIntegral(std::clamp(input[i], 0.0f, 1.0f), parameters) / histogramIntegral(1.0L, parameters);
            EXPECT_NEAR(output[i], static_cast<double>(expected), 1.0e-6);
        }
    }
}

TEST(StretchTest, InverseHistogramLargeAmountsHaveStableEndpointDenominators)
{
    an::GeneralizedStretchParameters parameters;
    parameters.function = an::StretchFunction::InverseHistogram;
    for (double amount : {1.0e-300, 1.0e-20, 1.0, 1.0e6, 1.0e12, 1.0e100})
    {
        parameters.amount = amount;
        for (double y :
             {0.0, std::numeric_limits<double>::denorm_min(), 0x1p-52, 0.1, 0.5, 0.9, std::nextafter(1.0, 0.0), 1.0})
        {
            // With symmetry at black and no protection this is the elementary MTF inverse.
            const long double expected = y / (1.0L + 2.0L * amount * (1.0L - y));
            const auto result = an::evaluateGeneralizedStretch(y, parameters);
            ASSERT_TRUE(result.hasValue()) << amount;
            EXPECT_NEAR(*result, static_cast<double>(expected), 3.0e-15) << amount << " " << y;
            EXPECT_GE(*result, 0.0);
            EXPECT_LE(*result, 1.0);
        }
    }
    parameters.amount = (std::numeric_limits<double>::max)();
    const auto rejected = an::evaluateGeneralizedStretch(0.5, parameters);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(StretchTest, InverseHistogramAdmittedExtremeAmountsNeverOverflowPlanning)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto layout = layoutOf(257u, 1u, 1u);
    std::vector<float> input(257), output(257);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i) / 256.0f;
    input[1] = std::numeric_limits<float>::denorm_min();
    input[255] = std::nextafter(1.0f, 0.0f);
    std::mt19937_64 random(0x65e9a21u);
    for (unsigned trial = 0; trial < 64; ++trial)
    {
        an::StretchImageRequest request;
        auto& p = request.stretch;
        p = typical(an::StretchFunction::InverseHistogram);
        p.amount = trial < 2 ? (std::numeric_limits<double>::max)() / (1.0 + trial * 0.25)
                             : std::pow(10.0, 100.0 + 208.0 * static_cast<double>(random() >> 11u) * 0x1p-53);
        p.symmetryPoint = 0.2 + 0.6 * static_cast<double>(random() >> 11u) * 0x1p-53;
        p.lowProtection = p.symmetryPoint - 0.1;
        p.highProtection = p.symmetryPoint + 0.1;
        const auto run =
            an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request);
        ASSERT_TRUE(run.hasValue()) << trial << " " << run.status().message();
        // These amounts put every representable interior Float32 target within much less than
        // one Double ulp of symmetry; both endpoint choices remain exact.
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const double expected = i == 0 ? 0.0 : (i == 256 ? 1.0 : p.symmetryPoint);
            EXPECT_NEAR(output[i], expected, 1.0e-6) << trial << " " << i;
            const auto scalar = an::evaluateGeneralizedStretch(input[i], p);
            ASSERT_TRUE(scalar.hasValue()) << trial << " " << scalar.status().message();
            EXPECT_NEAR(*scalar, expected, 2.0e-13);
        }
    }
}

TEST(StretchTest, InverseHistogramExtremeProtectionTailsRemainRepresentable)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    std::mt19937_64 random(0x8319a26u);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    for (unsigned trial = 0; trial < 64; ++trial)
    {
        const std::array<double, 3> bounds =
            trial == 0
                ? std::array<double, 3>{0.25, 0.5, 0.75}
                : (trial == 1 ? std::array<double, 3>{0.02, 0.93, 0.98}
                              : std::array<double, 3>{0.01 + 0.09 * unit(), 0.2 + 0.7 * unit(), 0.91 + 0.08 * unit()});
        an::StretchImageRequest request;
        auto& p = request.stretch;
        p = typical(an::StretchFunction::InverseHistogram);
        p.amount = trial < 2 ? (std::numeric_limits<double>::max)() : std::pow(10.0, 100.0 + 208.0 * unit());
        p.lowProtection = bounds[0];
        p.symmetryPoint = bounds[1];
        p.highProtection = bounds[2];
        const double boundary = static_cast<double>(histogramIntegral(p.lowProtection, p) / histogramIntegral(1.0L, p));
        ASSERT_GT(boundary, 0.0);
        const std::array<double, 9> input{0.0,
                                          std::numeric_limits<double>::denorm_min(),
                                          boundary * 0.25,
                                          std::nextafter(boundary, 0.0),
                                          boundary,
                                          std::nextafter(boundary, 1.0),
                                          0.5,
                                          std::nextafter(1.0, 0.0),
                                          1.0};
        std::array<double, 9> output{};
        const auto extent = an::ImageExtent::create(9u, 1u, 1u, an::defaultEngineLimits());
        const auto layout = an::ImageLayout::create({.extent = *extent,
                                                     .elementSizeBytes = sizeof(double),
                                                     .samplesPerPixel = 1u,
                                                     .planeCount = 1u,
                                                     .rowAlignmentBytes = 1u},
                                                    an::defaultEngineLimits());
        ASSERT_TRUE(layout.has_value());
        const auto run = an::stretchImage(execution, an::SampleType::Float64, input.data(), *layout, output.data(),
                                          *layout, request);
        ASSERT_TRUE(run.hasValue()) << run.status().message();
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const double expected = histogramInverseOracle(input[i], p);
            EXPECT_NEAR(output[i], expected, 2.0e-13) << trial << " " << i;
            const auto scalar = an::evaluateGeneralizedStretch(input[i], p);
            ASSERT_TRUE(scalar.hasValue());
            EXPECT_NEAR(*scalar, expected, 2.0e-13) << trial << " " << i;
        }
        if (p.symmetryPoint == 0.5)
        {
            // This symmetric tail's inverse slope is exactly 1 + amount/4.
            const double expected = input[1] + (input[1] * p.amount) * 0.25;
            EXPECT_NEAR(output[1], expected, 1.0e-30);
        }
    }
}

namespace
{
// The asinh family written independently of the engine: the primitive asinh(a t) / a about the
// symmetry point, extended by its tangent lines below the low protection point and above the high
// one, normalized over the unit interval. Dividing out the amount keeps the zero-amount limit and
// tiny amounts nonsingular, and the tangent slope is a hypotenuse so no amount squares out of
// range. No planned coefficient or evaluator of the engine is used.
class AsinhReference
{
public:
    explicit AsinhReference(const an::GeneralizedStretchParameters& p)
        : m_amount(p.amount), m_center(p.symmetryPoint), m_low(p.lowProtection), m_high(p.highProtection),
          m_origin(primitive(0.0L)), m_extent(primitive(1.0L) - m_origin)
    {
    }

    [[nodiscard]] long double forward(long double x) const
    {
        return std::clamp((primitive(std::clamp(x, 0.0L, 1.0L)) - m_origin) / m_extent, 0.0L, 1.0L);
    }

    [[nodiscard]] long double inverse(long double y) const
    {
        if (y <= 0.0L || y >= 1.0L)
            return y <= 0.0L ? 0.0L : 1.0L;
        const long double target = m_origin + y * m_extent;
        long double x;
        if (target < primitive(m_low))
            x = m_low + (target - primitive(m_low)) / slope(m_low);
        else if (target > primitive(m_high))
            x = m_high + (target - primitive(m_high)) / slope(m_high);
        else if (m_amount == 0.0L)
            x = m_center + target;
        else
        {
            // sinh(u) / a, with u beyond double sinh's range written as the exponential it equals
            // to far below one part in 1e300.
            const long double u = m_amount * target;
            const long double offset = std::abs(u) > 700.0L
                                           ? std::exp(std::abs(u) - std::log(m_amount) - std::log(2.0L))
                                           : std::sinh(std::abs(u)) / m_amount;
            x = m_center + (u < 0.0L ? -offset : offset);
        }
        return std::clamp(x, 0.0L, 1.0L);
    }

    [[nodiscard]] long double maximumSlope() const
    {
        return 1.0L / m_extent;
    }

    // The forward's least slope over the unit interval, reached at the protection point further from
    // the symmetry point and kept by the tangent line beyond it.
    [[nodiscard]] long double minimumSlope() const
    {
        return (std::min)(slope(m_low), slope(m_high)) / m_extent;
    }

    // How far one Float64 rounding of the primitive moves an inverse at `x`. This reference, and the
    // engine on the curve, form the primitive as the origin plus a share of the extent, which rounds
    // relative to the larger of the two; where a steep tangent line at the symmetry point takes
    // nearly all of the extent, that rounding is a long way along the rest of the curve.
    [[nodiscard]] long double primitiveRounding(long double x) const
    {
        const long double magnitude = (std::max)(std::abs(m_origin), std::abs(primitive(x) - m_origin));
        return magnitude * 0x1p-52L / slope(std::clamp(x, m_low, m_high));
    }

private:
    [[nodiscard]] long double slope(long double z) const
    {
        return 1.0L / std::hypot(1.0L, m_amount * (z - m_center));
    }

    [[nodiscard]] long double primitive(long double x) const
    {
        const long double z = std::clamp(x, m_low, m_high);
        const long double t = z - m_center;
        const long double curved = m_amount == 0.0L ? t : std::asinh(m_amount * t) / m_amount;
        return curved + (x - z) * slope(z);
    }

    long double m_amount;
    long double m_center;
    long double m_low;
    long double m_high;
    long double m_origin;
    long double m_extent;
};

[[nodiscard]] double asinhInverseReference(double y, const an::GeneralizedStretchParameters& p)
{
    return static_cast<double>(AsinhReference(p).inverse(y));
}

// Runs one Float32 row through `stretchImage` under one backend.
[[nodiscard]] std::vector<float> stretchRow(const an::EngineContext& engine, an::ComputeBackend backend,
                                            const std::vector<float>& input,
                                            const an::GeneralizedStretchParameters& parameters)
{
    an::ExecutionOptions options;
    options.backend = backend;
    auto execution = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(execution.hasValue());
    const auto layout = layoutOf(static_cast<std::uint32_t>(input.size()), 1u, 1u);
    std::vector<float> output(input.size(), -1.0f);
    an::StretchImageRequest request;
    request.stretch = parameters;
    const auto run =
        an::stretchImage(*execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request);
    EXPECT_TRUE(run.hasValue()) << run.status().message();
    if (run.hasValue())
        EXPECT_EQ(run->run.outputsWritten, input.size());
    return output;
}

// Float32 neighbours of the curve's image of each landmark, as the benchmark fixtures build them.
void appendAsinhLandmarks(const an::GeneralizedStretchParameters& p, std::vector<float>& input)
{
    const AsinhReference reference(p);
    for (const double landmark : {0.0, p.lowProtection, p.symmetryPoint, p.highProtection, 1.0})
    {
        const float y = static_cast<float>(reference.forward(landmark));
        for (const float value : {std::nextafter(y, -1.0f), y, y, std::nextafter(y, 2.0f)})
            input.push_back(value);
    }
    for (const float value :
         {-10.0f, 10.0f, -0.0f, 0.0f, 1.0f, std::numeric_limits<float>::denorm_min(), std::nextafter(1.0f, 0.0f)})
        input.push_back(value);
}

// Every property the benchmark oracle holds a full image to, for one sorted row and both backends.
void checkAsinhInverseRow(const an::EngineContext& engine, const an::GeneralizedStretchParameters& p,
                          const std::vector<float>& input, double scalarTolerance)
{
    const AsinhReference reference(p);
    const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, p);
    const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, p);
    ASSERT_EQ(compiled.size(), input.size());
    ASSERT_EQ(scalar.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        SCOPED_TRACE(::testing::Message() << "sample " << i << " = " << input[i]);
        const long double expected = reference.inverse(input[i]);
        for (const auto* output : {&compiled, &scalar})
        {
            const float value = (*output)[i];
            EXPECT_NEAR(value, static_cast<double>(expected), 1.0e-6);
            EXPECT_GE(value, 0.0f);
            EXPECT_LE(value, 1.0f);
            if (input[i] <= 0.0f)
                EXPECT_EQ(std::bit_cast<std::uint32_t>(value), 0u);
            if (input[i] >= 1.0f)
                EXPECT_EQ(value, 1.0f);
            const long double roundTrip = std::abs(reference.forward(value) - std::clamp(input[i], 0.0f, 1.0f));
            EXPECT_LE(roundTrip, reference.maximumSlope() * 1.0e-6L + 1.0e-14L);
            if (i != 0u)
            {
                EXPECT_LE((*output)[i - 1], value);
                if (input[i] == input[i - 1])
                    EXPECT_EQ(std::bit_cast<std::uint32_t>((*output)[i - 1]), std::bit_cast<std::uint32_t>(value));
            }
        }
        // The compiled row evaluates the same Float64 formula through the vector library's
        // hyperbolic sine, so after narrowing the two differ by at most one Float32 step, plus a few
        // Float64 roundings of the unit-sized terms where the result cancels to near zero.
        EXPECT_LE(std::abs(compiled[i] - scalar[i]), std::abs(std::nextafter(scalar[i], 2.0f) - scalar[i]) + 1.0e-14f);
        const auto evaluated = an::evaluateGeneralizedStretch(input[i], p);
        ASSERT_TRUE(evaluated.hasValue()) << evaluated.status().message();
        EXPECT_NEAR(*evaluated, static_cast<double>(expected),
                    scalarTolerance + 4.0 * static_cast<double>(reference.primitiveRounding(expected)));
    }
}
} // namespace

TEST(StretchTest, InverseAsinhIdentityKeepsRepresentableEndpoints)
{
    auto parameters = typical(an::StretchFunction::InverseAsinh);
    parameters.amount = 0.0;
    for (double x : {0.0, std::numeric_limits<double>::denorm_min(), 1.0e-30, std::nextafter(1.0, 0.0), 1.0})
    {
        const auto result = an::evaluateGeneralizedStretch(x, parameters);
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(*result, x);
    }
    const auto engine = makeEngine();
    for (double amount : {0.0, 2.5})
    {
        parameters.amount = amount;
        const auto result = an::evaluateGeneralizedStretch(-0.0, parameters);
        ASSERT_TRUE(result.hasValue());
        EXPECT_FALSE(std::signbit(*result));
        for (auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
        {
            const auto output = stretchRow(engine, backend, {-0.0f, 0.0f, 1.0f, 2.0f, -3.0f}, parameters);
            for (float value : {output[0], output[1], output[4]})
            {
                EXPECT_EQ(value, 0.0f);
                EXPECT_FALSE(std::signbit(value));
            }
            EXPECT_EQ(output[2], 1.0f);
            EXPECT_EQ(output[3], 1.0f);
        }
    }
}

TEST(StretchTest, InverseAsinhSeededParametersAndCompleteSamplesMatchIndependentPrimitive)
{
    std::mt19937_64 random(0x5a17c3e9u);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    const auto engine = makeEngine();
    for (unsigned trial = 0; trial < 512; ++trial)
    {
        auto parameters = typical(an::StretchFunction::InverseAsinh);
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        parameters.lowProtection = trial % 4 == 0 ? 0.0 : bounds[0];
        parameters.symmetryPoint = bounds[1];
        parameters.highProtection = trial % 4 == 0 ? 1.0 : bounds[2];
        parameters.amount = std::pow(10.0, -14.0 + 22.0 * unit());
        if (trial % 11 == 0)
            parameters.symmetryPoint = parameters.lowProtection;
        if (trial % 13 == 0)
            parameters.highProtection = parameters.symmetryPoint;
        if (trial % 17 == 0)
            parameters.amount = 0.0;
        if (trial % 19 == 0)
            parameters.symmetryPoint = parameters.lowProtection = 0.0;
        if (trial % 23 == 0)
            parameters.symmetryPoint = parameters.highProtection = 1.0;
        SCOPED_TRACE(::testing::Message() << "seed=0x5a17c3e9 trial=" << trial << " amount=" << parameters.amount
                                          << " low=" << parameters.lowProtection << " center="
                                          << parameters.symmetryPoint << " high=" << parameters.highProtection);
        std::vector<float> input;
        appendAsinhLandmarks(parameters, input);
        // An odd width puts samples in the compiled row's partial final vector as well as its body.
        while (input.size() < 263)
            input.push_back(static_cast<float>(unit()));
        std::sort(input.begin(), input.end());
        checkAsinhInverseRow(engine, parameters, input, 1.0e-9);
        if (::testing::Test::HasFailure())
            return;
    }
}

TEST(StretchTest, InverseAsinhExtremeAmountsNeverOverflowAndInvertTheCurve)
{
    const auto engine = makeEngine();
    std::mt19937_64 random(0x2c8e4b17u);
    for (const double amount : {1.0e-300, 1.0e-100, 1.0e-20, 1.0e-6, 1.0e6, 1.0e12, 1.0e100, 1.0e150, 1.0e160, 1.0e200,
                                1.0e300, static_cast<double>((std::numeric_limits<float>::max)()),
                                (std::numeric_limits<double>::max)() / 2.0, (std::numeric_limits<double>::max)()})
    {
        // The last three put a tangent line at the symmetry point, whose slope is then the amount itself;
        // at a large amount that line takes nearly all of the normalized range.
        for (const auto bounds : {std::array<double, 3>{0.4, 0.5, 0.6}, std::array<double, 3>{0.0, 0.25, 1.0},
                                  std::array<double, 3>{0.02, 0.93, 0.98}, std::array<double, 3>{0.5, 0.5, 0.5},
                                  std::array<double, 3>{0.3, 0.7, 0.7}, std::array<double, 3>{0.3, 0.3, 0.7}})
        {
            auto parameters = typical(an::StretchFunction::InverseAsinh);
            parameters.amount = amount;
            parameters.lowProtection = bounds[0];
            parameters.symmetryPoint = bounds[1];
            parameters.highProtection = bounds[2];
            SCOPED_TRACE(::testing::Message() << "amount=" << amount << " low=" << bounds[0] << " center=" << bounds[1]
                                              << " high=" << bounds[2]);
            std::vector<float> input;
            appendAsinhLandmarks(parameters, input);
            while (input.size() < 131)
                input.push_back(static_cast<float>(static_cast<double>(random() >> 11u) * 0x1p-53));
            std::sort(input.begin(), input.end());
            checkAsinhInverseRow(engine, parameters, input, 1.0e-12);

            const AsinhReference reference(parameters);

            // The Float64 image path keeps the scalar reference's precision.
            std::vector<double> wide(input.begin(), input.end());
            std::vector<double> output(wide.size(), -1.0);
            const auto extent =
                an::ImageExtent::create(static_cast<std::uint32_t>(wide.size()), 1u, 1u, an::defaultEngineLimits());
            const auto layout = an::ImageLayout::create({.extent = *extent,
                                                         .elementSizeBytes = sizeof(double),
                                                         .samplesPerPixel = 1u,
                                                         .planeCount = 1u,
                                                         .rowAlignmentBytes = 1u},
                                                        an::defaultEngineLimits());
            ASSERT_TRUE(layout.has_value());
            an::StretchImageRequest request;
            request.stretch = parameters;
            const auto execution = makeExecution(engine);
            const auto run = an::stretchImage(execution, an::SampleType::Float64, wide.data(), *layout, output.data(),
                                              *layout, request);
            ASSERT_TRUE(run.hasValue()) << run.status().message();
            for (std::size_t i = 0; i < wide.size(); ++i)
            {
                const long double expected = reference.inverse(wide[i]);
                EXPECT_NEAR(output[i], static_cast<double>(expected),
                            1.0e-12 + 4.0 * static_cast<double>(reference.primitiveRounding(expected)))
                    << wide[i];
            }
            if (::testing::Test::HasFailure())
                return;
        }
    }
}

TEST(StretchTest, InverseAsinhRoundTripsItsForwardWhereTheForwardIsConditioned)
{
    std::mt19937_64 random(0x71d04a3bu);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    for (unsigned trial = 0; trial < 256; ++trial)
    {
        auto forward = typical(an::StretchFunction::Asinh);
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        forward.lowProtection = bounds[0];
        forward.symmetryPoint = bounds[1];
        forward.highProtection = bounds[2];
        forward.amount = std::pow(10.0, -10.0 + 30.0 * unit());
        auto inverse = forward;
        inverse.function = an::StretchFunction::InverseAsinh;
        for (unsigned sample = 0; sample < 32; ++sample)
        {
            const double x = unit();
            const double y = *an::evaluateGeneralizedStretch(x, forward);
            const auto back = an::evaluateGeneralizedStretch(y, inverse);
            ASSERT_TRUE(back.hasValue());
            // One Double rounding of the forward value, divided by the curve's smallest slope.
            EXPECT_NEAR(*back, x, 1.0e-9) << "trial " << trial << " amount " << forward.amount << " x " << x;
        }
    }
}

TEST(StretchTest, InverseAsinhCompiledRowEqualsItsScalarAuthorityOnEveryQuantizedCode)
{
    // Every UInt16 capture code as the benchmark derives it, in a row whose width leaves a partial
    // final vector, for the audit and interactive profiles.
    std::vector<float> input(65536u + 3u);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i % 65536u) / 65535.0f;
    const auto engine = makeEngine();
    for (const auto [amount, center] : {std::pair{2.0, 0.25}, std::pair{3.0, 0.05}})
    {
        auto parameters = typical(an::StretchFunction::InverseAsinh);
        parameters.amount = amount;
        parameters.symmetryPoint = center;
        parameters.lowProtection = 0.0;
        parameters.highProtection = 1.0;
        const AsinhReference reference(parameters);
        const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, parameters);
        const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, parameters);
        std::size_t differing = 0u;
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            differing += compiled[i] != scalar[i] ? 1u : 0u;
            EXPECT_NEAR(compiled[i], static_cast<double>(reference.inverse(input[i])), 1.0e-6) << input[i];
            if (i >= 65536u)
                EXPECT_EQ(std::bit_cast<std::uint32_t>(compiled[i]),
                          std::bit_cast<std::uint32_t>(compiled[i - 65536u]));
        }
        // Distinct Float64 hyperbolic sines rarely round to different Float32 values; a count near
        // the sample count would mean the compiled row computes something else.
        EXPECT_LE(differing, input.size() / 1000u) << amount;
    }
}

namespace
{
// The generalized-hyperbolic family written from its definition, independently of the engine. The
// curve is G(t), its rise a distance t from the symmetry point, with unit initial slope at amount a
// and bias b: log1p(a t) / a at b = -1, (1 - (1 - b a t)^((1 + b) / b)) / ((1 + b) a) elsewhere below
// zero, (1 - exp(-a t)) / a at zero and (1 - (1 + b a t)^(-1 / b)) / a above it. Tangent lines
// continue it past the protection points and the whole is normalized over the unit interval, each
// piece written from its own end of it. A saturating curve is inverted from its remaining rise
// 1 - c a G where that is below one half, since a difference from the supremum loses what the rise
// keeps. The inverse measures rises in a unit, a power of two that brings the extent near one: at the
// largest amounts the extent is near the least positive double, and a small value's share of it is
// below that, which a long double as wide as a double - MSVC's - cannot hold. The exchange is exact,
// so a wider long double computes what it computed without it. No planned coefficient or evaluator
// of the engine is used; the benchmark's oracle is the same construction, pinned to 90-digit values.
// Its formulas cannot take a subnormal bias or amount, so those are tested against the neighbouring
// regime and the identity instead.
class GhsReference
{
public:
    explicit GhsReference(const an::GeneralizedStretchParameters& p)
        : m_amount(p.amount), m_bias(p.bias), m_center(p.symmetryPoint), m_low(p.lowProtection),
          m_high(p.highProtection), m_lowCurve(curve(m_center - m_low)), m_highCurve(curve(m_high - m_center)),
          m_lowSlope(slope(m_center - m_low)), m_highSlope(slope(m_high - m_center)), m_lowTail(tail(m_center - m_low)),
          m_highTail(tail(m_high - m_center)), m_lowRise(m_low * m_lowSlope + m_lowCurve),
          m_extent(m_lowRise + (1.0L - m_high) * m_highSlope + m_highCurve),
          m_unit(m_extent > 0.0L && std::isfinite(m_extent)
                     ? std::ldexp(1.0L,
                                  std::clamp(-std::ilogb(m_extent), 0, std::numeric_limits<double>::max_exponent - 1))
                     : 1.0L)
    {
    }

    [[nodiscard]] long double forward(long double value) const
    {
        const long double x = std::clamp(value, 0.0L, 1.0L);
        if (m_amount == 0.0L)
            return x;
        long double result;
        if (x < m_low)
            result = x * m_lowSlope / m_extent;
        else if (x < m_center)
            result = (m_low * m_lowSlope + (m_lowCurve - curve(m_center - x))) / m_extent;
        else if (x <= m_high)
            result = 1.0L - ((1.0L - m_high) * m_highSlope + (m_highCurve - curve(x - m_center))) / m_extent;
        else
            result = 1.0L - (1.0L - x) * m_highSlope / m_extent;
        return std::clamp(result, 0.0L, 1.0L);
    }

    [[nodiscard]] long double inverse(long double y) const
    {
        if (y <= 0.0L || y >= 1.0L)
            return y <= 0.0L ? 0.0L : 1.0L;
        if (m_amount == 0.0L)
            return y;
        // Rises in units of m_unit; a distance along the input is not one.
        const long double below = y * (m_extent * m_unit);
        const long double above = (1.0L - y) * (m_extent * m_unit);
        const long double lowLine = m_low * m_lowSlope * m_unit;
        const long double highLine = (1.0L - m_high) * m_highSlope * m_unit;
        long double x;
        if (below < lowLine)
            x = below / m_lowSlope / m_unit;
        else if (above < highLine)
            x = 1.0L - above / m_highSlope / m_unit;
        else if (below < m_lowRise * m_unit)
            x = m_center - distance(below - lowLine, m_center - m_low, m_lowCurve * m_unit, m_lowTail);
        else
            x = m_center + distance(above - highLine, m_high - m_center, m_highCurve * m_unit, m_highTail);
        return std::clamp(x, 0.0L, 1.0L);
    }

    [[nodiscard]] long double maximumSlope() const
    {
        return 1.0L / m_extent;
    }

private:
    [[nodiscard]] long double curve(long double t) const
    {
        const long double a = m_amount;
        const long double b = m_bias;
        if (a == 0.0L || t == 0.0L)
            return t;
        if (b == -1.0L)
            return std::log1p(a * t) / a;
        if (b < 0.0L)
            return -std::expm1((1.0L + b) / b * std::log1p(-b * a * t)) / ((1.0L + b) * a);
        if (b == 0.0L)
            return -std::expm1(-a * t) / a;
        return -std::expm1(-std::log1p(b * a * t) / b) / a;
    }

    [[nodiscard]] long double slope(long double t) const
    {
        const long double a = m_amount;
        const long double b = m_bias;
        if (a == 0.0L)
            return 1.0L;
        if (b == -1.0L)
            return 1.0L / (1.0L + a * t);
        if (b < 0.0L)
            return std::exp(std::log1p(-b * a * t) / b);
        if (b == 0.0L)
            return std::exp(-a * t);
        return std::exp(-(1.0L + b) / b * std::log1p(b * a * t));
    }

    // What the curve still has to rise at t, 1 - c a G(t), with c = 1 + b below zero and one
    // otherwise; one everywhere for the logarithm, which never saturates.
    [[nodiscard]] long double tail(long double t) const
    {
        const long double a = m_amount;
        const long double b = m_bias;
        if (b == 0.0L)
            return std::exp(-a * t);
        if (b > 0.0L)
            return std::exp(-std::log1p(b * a * t) / b);
        return std::exp((1.0L + b) / b * std::log1p(-b * a * t));
    }

    // Where within `limit` of the symmetry point the curve has `drop` less than its value there; the
    // drop and the curve's value at the limit are in units of m_unit.
    [[nodiscard]] long double distance(long double drop, long double limit, long double limitCurve,
                                       long double limitTail) const
    {
        const long double a = m_amount;
        const long double b = m_bias;
        // The amount over the unit, so that it times a rise in units is the amount times the rise.
        const long double unitAmount = m_amount / m_unit;
        const long double r = (std::max)(drop, 0.0L);
        const long double t = limitTail + (b < 0.0L ? 1.0L + b : 1.0L) * unitAmount * r;
        const long double s = (std::max)(limitCurve - r, 0.0L);
        long double u;
        if (s == 0.0L)
            u = 0.0L;
        else if (t < 0.5L)
            u = b == 0.0L  ? -std::log(t) / a
                : b > 0.0L ? std::expm1(-b * std::log(t)) / (b * a)
                           : std::expm1(b / (1.0L + b) * std::log(t)) / (-b * a);
        else if (b == -1.0L)
            u = std::expm1(unitAmount * s) / a;
        else if (b < 0.0L)
            u = std::expm1(b / (1.0L + b) * std::log1p(-(std::min)((1.0L + b) * unitAmount * s, 1.0L))) / (-b * a);
        else if (b == 0.0L)
            u = -std::log1p(-(std::min)(unitAmount * s, 1.0L)) / a;
        else
            u = std::expm1(-b * std::log1p(-(std::min)(unitAmount * s, 1.0L))) / (b * a);
        return std::isnan(u) ? limit : (std::min)(u, limit);
    }

    long double m_amount;
    long double m_bias;
    long double m_center;
    long double m_low;
    long double m_high;
    long double m_lowCurve;
    long double m_highCurve;
    long double m_lowSlope;
    long double m_highSlope;
    long double m_lowTail;
    long double m_highTail;
    long double m_lowRise;
    long double m_extent;
    long double m_unit;
};

[[nodiscard]] double ghsReference(double value, const an::GeneralizedStretchParameters& p)
{
    const GhsReference reference(p);
    return static_cast<double>(p.function == an::StretchFunction::InverseGeneralizedHyperbolic
                                   ? reference.inverse(value)
                                   : reference.forward(value));
}

[[nodiscard]] an::GeneralizedStretchParameters ghs(bool inverse, double amount, double bias, double symmetry,
                                                   double low, double high)
{
    an::GeneralizedStretchParameters p;
    p.function =
        inverse ? an::StretchFunction::InverseGeneralizedHyperbolic : an::StretchFunction::GeneralizedHyperbolic;
    p.amount = amount;
    p.bias = bias;
    p.symmetryPoint = symmetry;
    p.lowProtection = low;
    p.highProtection = high;
    return p;
}

// Runs one Float32 row as a Float64 image under the default backend.
[[nodiscard]] std::vector<double> stretchWideRow(const an::EngineContext& engine, const std::vector<float>& input,
                                                 const an::GeneralizedStretchParameters& parameters)
{
    std::vector<double> wide(input.begin(), input.end());
    std::vector<double> output(wide.size(), -1.0);
    const auto extent =
        an::ImageExtent::create(static_cast<std::uint32_t>(wide.size()), 1u, 1u, an::defaultEngineLimits());
    const auto layout = an::ImageLayout::create({.extent = *extent,
                                                 .elementSizeBytes = sizeof(double),
                                                 .samplesPerPixel = 1u,
                                                 .planeCount = 1u,
                                                 .rowAlignmentBytes = 1u},
                                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    an::StretchImageRequest request;
    request.stretch = parameters;
    const auto execution = makeExecution(engine);
    const auto run =
        an::stretchImage(execution, an::SampleType::Float64, wide.data(), *layout, output.data(), *layout, request);
    EXPECT_TRUE(run.hasValue()) << run.status().message();
    return output;
}

// Float32 neighbours of every landmark and of its image under the curve, the extremes a caller can
// pass, and uniform samples up to an odd width, sorted; so an inverse and a forward row both meet
// every piece boundary from each side, and a compiled row's partial final vector is reached.
[[nodiscard]] std::vector<float> ghsRow(const an::GeneralizedStretchParameters& p, std::mt19937_64& random,
                                        std::size_t width)
{
    const GhsReference reference(p);
    std::vector<float> input;
    for (const double landmark : {0.0, p.lowProtection, p.symmetryPoint, p.highProtection, 1.0})
        for (const float center :
             {static_cast<float>(landmark), static_cast<float>(reference.forward(static_cast<long double>(landmark)))})
            for (const float value : {std::nextafter(center, -1.0f), center, center, std::nextafter(center, 2.0f)})
                input.push_back(value);
    for (const float value :
         {-10.0f, 10.0f, -0.0f, 0.0f, 1.0f, std::numeric_limits<float>::denorm_min(), std::nextafter(1.0f, 0.0f)})
        input.push_back(value);
    while (input.size() < width)
        input.push_back(static_cast<float>(static_cast<double>(random() >> 11u) * 0x1p-53));
    std::sort(input.begin(), input.end());
    return input;
}

// Every property the benchmark oracle holds a complete output to, for one sorted Float32 row under
// both backends, and the Float64 image path and scalar calls within 1e-12 of the reference. Reports
// the first failure and how many checks failed, so one line names each failing curve.
[[nodiscard]] ::testing::AssertionResult ghsRowMatchesReference(const an::EngineContext& engine,
                                                                const an::GeneralizedStretchParameters& p,
                                                                const std::vector<float>& input)
{
    const bool inverse = p.function == an::StretchFunction::InverseGeneralizedHyperbolic;
    const GhsReference reference(p);
    const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, p);
    const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, p);
    const auto wide = stretchWideRow(engine, input, p);
    std::size_t failures = 0u;
    std::ostringstream first;
    const auto fail = [&](std::size_t i, const char* what, double value, double expected) {
        if (failures++ == 0u)
            first << what << " at sample " << i << " = " << input[i] << ": " << value << " against " << expected;
    };
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const long double expected = inverse ? reference.inverse(input[i]) : reference.forward(input[i]);
        const double target = static_cast<double>(expected);
        for (const auto* output : {&compiled, &scalar})
        {
            const float value = (*output)[i];
            if (!(std::abs(value - target) <= 1.0e-6))
                fail(i, "error", value, target);
            if (!(value >= 0.0f && value <= 1.0f))
                fail(i, "range", value, target);
            if (input[i] <= 0.0f && std::bit_cast<std::uint32_t>(value) != 0u)
                fail(i, "positive zero", value, 0.0);
            if (input[i] >= 1.0f && value != 1.0f)
                fail(i, "one", value, 1.0);
            if (inverse)
            {
                const long double roundTrip =
                    std::abs(reference.forward(value) - std::clamp(static_cast<long double>(input[i]), 0.0L, 1.0L));
                if (!(roundTrip <= reference.maximumSlope() * 1.0e-6L + 1.0e-14L))
                    fail(i, "round trip", static_cast<double>(roundTrip), 0.0);
            }
            if (i != 0u && (*output)[i - 1] > value)
                fail(i, "ordering", value, (*output)[i - 1]);
            if (i != 0u && input[i] == input[i - 1] &&
                std::bit_cast<std::uint32_t>((*output)[i - 1]) != std::bit_cast<std::uint32_t>(value))
                fail(i, "equal inputs", value, (*output)[i - 1]);
        }
        // A compiled row evaluates the same Float64 formula through the vector library, so after
        // narrowing it differs from the scalar authority by at most one Float32 step - or, where the
        // result is so near zero that a Float32 step is finer than the Float64 tier both are held to
        // below, by that tier: a distance recovered from an exponent in the hundreds carries one
        // rounding of its logarithm, which two correct libraries need not share.
        const float step = std::abs(std::nextafter(scalar[i], 2.0f) - scalar[i]) + 1.0e-14f;
        if (!(std::abs(compiled[i] - scalar[i]) <= (std::max)(step, 1.0e-12f)))
            fail(i, "backends", compiled[i], scalar[i]);
        if (!(std::abs(wide[i] - target) <= 1.0e-12))
            fail(i, "Float64 image", wide[i], target);
        const auto evaluated = an::evaluateGeneralizedStretch(input[i], p);
        if (!evaluated.hasValue() || !(std::abs(*evaluated - target) <= 1.0e-12))
            fail(i, "scalar call", evaluated.hasValue() ? *evaluated : -1.0, target);
    }
    if (failures == 0u)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << failures << " failed checks; first: " << first.str();
}

[[nodiscard]] std::string describe(const an::GeneralizedStretchParameters& p)
{
    std::ostringstream text;
    text.precision(17);
    text << (p.function == an::StretchFunction::InverseGeneralizedHyperbolic ? "inverse" : "forward")
         << " amount=" << p.amount << " bias=" << p.bias << " low=" << p.lowProtection
         << " symmetry=" << p.symmetryPoint << " high=" << p.highProtection;
    return text.str();
}
} // namespace

TEST(StretchTest, GeneralizedHyperbolicKnownValuesComeFromTheFamilysDefinition)
{
    // Amount 2, symmetry 1/2 and protection at 1/4 and 3/4, so each sample lies on a different
    // piece: the low tangent, both sides of the symmetry point and the high tangent. Evaluated
    // from the family's definition in 90-digit decimal arithmetic.
    struct Known
    {
        float bias;
        double input;
        double output;
    };
    constexpr std::array<Known, 20> known{{
        {-1.0f, 0.125, 0.1127957622254027766877977649423713654416},
        {-1.0f, 0.375, 0.3489821182629825986258473299225299355348},
        {-1.0f, 0.625, 0.6510178817370174013741526700774700644652},
        {-1.0f, 0.875, 0.8872042377745972233122022350576286345584},
        {-0.5f, 0.125, 0.1111111111111111111111111111111111111111},
        {-0.5f, 0.375, 0.3456790123456790123456790123456790123457},
        {-0.5f, 0.625, 0.6543209876543209876543209876543209876543},
        {-0.5f, 0.875, 0.8888888888888888888888888888888888888889},
        {0.0f, 0.125, 0.1088166495983959631655432429888300040208},
        {0.0f, 0.375, 0.3412600761757854081611357676222076182740},
        {0.0f, 0.625, 0.6587399238242145918388642323777923817260},
        {0.0f, 0.875, 0.8911833504016040368344567570111699959792},
        {0.5f, 0.125, 0.1038961038961038961038961038961038961039},
        {0.5f, 0.375, 0.3296456629789963123296456629789963123296},
        {0.5f, 0.625, 0.6703543370210036876703543370210036876704},
        {0.5f, 0.875, 0.8961038961038961038961038961038961038961},
        {1.0f, 0.125, 0.1},
        {1.0f, 0.375, 0.32},
        {1.0f, 0.625, 0.68},
        {1.0f, 0.875, 0.9},
    }};
    for (const Known& value : known)
    {
        const auto forward = an::evaluateGeneralizedStretch(value.input, ghs(false, 2.0, value.bias, 0.5, 0.25, 0.75));
        ASSERT_TRUE(forward.hasValue()) << forward.status().message();
        EXPECT_NEAR(*forward, value.output, 2.0e-15) << "bias " << value.bias << " at " << value.input;
        const auto inverse = an::evaluateGeneralizedStretch(value.output, ghs(true, 2.0, value.bias, 0.5, 0.25, 0.75));
        ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
        EXPECT_NEAR(*inverse, value.input, 1.0e-14) << "bias " << value.bias << " at " << value.output;
    }

    // Protection at 1/8 and 3/4 around 3/8. At a large amount the curve lies within e^-250 of its
    // supremum over most of each half, so a value just above zero is the supremum less a rise far
    // below its rounding; the inverse must still find where the curve reaches it.
    struct Saturated
    {
        float amount;
        float bias;
        float input;
        double output;
    };
    const std::array<Saturated, 7> saturated{{
        {1000.0f, 0.0f, 1.0e-20f, 0.329641445288944553664415651256},
        {1000.0f, 0.0f, 0.5f, 0.375},
        {1000.0f, 0.0f, 0.99999994f, 0.390942385152878742116596338794},
        {1000.0f, 0.001f, 1.0e-20f, 0.328597014576371627796898782043},
        {1000.0f, -0.5f, 1.0e-20f, 6.33202537090e-19},
        {100.0f, 0.0f, 1.0e-10f, 0.135650486319342905728993645423},
        {100.0f, -0.999999f, 0.999f, 0.996905068547346546947425391626},
    }};
    for (const Saturated& value : saturated)
    {
        const auto inverse =
            an::evaluateGeneralizedStretch(value.input, ghs(true, value.amount, value.bias, 0.375, 0.125, 0.75));
        ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
        EXPECT_NEAR(*inverse, value.output, (std::max)(1.0e-13, value.output * 1.0e-12))
            << value.amount << ' ' << value.bias << ' ' << value.input;
    }
    // At the largest Float32 amount the interior of the range is reached within 1e-15 of the
    // symmetry point: the curve is a step there, not flat on either side of it.
    for (const float bias : {-1.0f, -0.999999f, -0.5f, 0.0f, 1.0e-6f, 0.5f, 1.0f})
        for (const double input : {0.25, 0.75, 1.0 - std::numeric_limits<float>::epsilon()})
        {
            const auto inverse = an::evaluateGeneralizedStretch(
                input, ghs(true, (std::numeric_limits<float>::max)(), bias, 0.375, 0.125, 0.75));
            ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
            // The logarithm never saturates, so it keeps a measurable distance from the point.
            if (bias > -0.9f)
                EXPECT_NEAR(*inverse, 0.375, 1.0e-15) << bias << ' ' << input;
            else
                EXPECT_NEAR(
                    *inverse,
                    ghsReference(input, ghs(true, (std::numeric_limits<float>::max)(), bias, 0.375, 0.125, 0.75)),
                    1.0e-12)
                    << bias << ' ' << input;
        }
}

TEST(StretchTest, GeneralizedHyperbolicZeroAmountIsAnExactIdentity)
{
    const auto engine = makeEngine();
    for (const bool inverse : {false, true})
        for (const double bias : {-1.0, -0.5, 0.0, 1.0})
        {
            const auto parameters = ghs(inverse, 0.0, bias, 0.375, 0.125, 0.75);
            for (const double x :
                 {0.0, std::numeric_limits<double>::denorm_min(), 1.0e-30, 0.3, std::nextafter(1.0, 0.0), 1.0})
            {
                const auto result = an::evaluateGeneralizedStretch(x, parameters);
                ASSERT_TRUE(result.hasValue());
                EXPECT_EQ(*result, x) << describe(parameters);
            }
            const auto zero = an::evaluateGeneralizedStretch(-0.0, parameters);
            ASSERT_TRUE(zero.hasValue());
            EXPECT_FALSE(std::signbit(*zero)) << describe(parameters);
            for (auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
            {
                const auto output = stretchRow(
                    engine, backend, {-0.0f, 0.0f, std::numeric_limits<float>::denorm_min(), 0.3f, 1.0f, 2.0f, -3.0f},
                    parameters);
                for (float value : {output[0], output[1], output[6]})
                {
                    EXPECT_EQ(value, 0.0f);
                    EXPECT_FALSE(std::signbit(value)) << describe(parameters);
                }
                EXPECT_EQ(output[2], std::numeric_limits<float>::denorm_min()) << describe(parameters);
                EXPECT_EQ(output[3], 0.3f);
                EXPECT_EQ(output[4], 1.0f);
                EXPECT_EQ(output[5], 1.0f);
            }
        }
}

TEST(StretchTest, GeneralizedHyperbolicBoundaryProfilesMatchTheFamilysDefinition)
{
    // The benchmark's boundary profiles, parameters rounded through Float32 as its scenarios state
    // them: every regime and a bias a millionth from each regime boundary, at tiny, ordinary, large
    // and the largest Float32 amounts, with symmetry at either end; zero amount and coincident
    // protection.
    struct Profile
    {
        float amount;
        float bias;
        float symmetry;
        float low;
        float high;
    };
    std::vector<Profile> profiles{{0.0f, -0.5f, 0.375f, 0.125f, 0.75f}, {3.0f, -0.5f, 0.375f, 0.375f, 0.375f}};
    for (const float bias : {-1.0f, -0.999999f, -0.5f, -0.000001f, 0.0f, 0.000001f, 1.0f})
    {
        for (const float amount : {0.000001f, 3.0f, 100.0f, (std::numeric_limits<float>::max)()})
            profiles.push_back({amount, bias, 0.375f, 0.125f, 0.75f});
        profiles.push_back({3.0f, bias, 0.0f, 0.0f, 0.75f});
        profiles.push_back({3.0f, bias, 1.0f, 0.125f, 1.0f});
    }
    const auto engine = makeEngine();
    std::mt19937_64 random(0x6b2e90d1u);
    for (const bool inverse : {false, true})
        for (const Profile& profile : profiles)
        {
            const auto parameters =
                ghs(inverse, profile.amount, profile.bias, profile.symmetry, profile.low, profile.high);
            EXPECT_TRUE(ghsRowMatchesReference(engine, parameters, ghsRow(parameters, random, 263u)))
                << describe(parameters);
        }
}

TEST(StretchTest, GeneralizedHyperbolicBiasesBeyondUnitMatchTheFamilysDefinition)
{
    // The local intensities the stretch dialog offers beyond one either way: below minus one
    // the integral power grows without bound, so its inverse is solved from the rise itself, never
    // from a remaining rise below one half; above one it is the hyperbolic power saturating faster.
    // Each a millionth either side of the regime boundaries and at the range's ends, at tiny,
    // ordinary, large and the largest Float32 amounts, with symmetry at either end; and seeded
    // curves over the whole range.
    struct Profile
    {
        float amount;
        float bias;
        float symmetry;
        float low;
        float high;
    };
    std::vector<Profile> profiles;
    for (const float bias : {-5.0f, -3.0f, -1.5f, -1.000001f, 1.000001f, 1.5f, 2.0f, 5.0f, 15.0f})
    {
        for (const float amount : {0.000001f, 3.0f, 100.0f, 1.0e6f, (std::numeric_limits<float>::max)()})
            profiles.push_back({amount, bias, 0.375f, 0.125f, 0.75f});
        profiles.push_back({3.0f, bias, 0.0f, 0.0f, 0.75f});
        profiles.push_back({3.0f, bias, 1.0f, 0.125f, 1.0f});
        profiles.push_back({3.0f, bias, 0.375f, 0.375f, 0.375f});
    }
    const auto engine = makeEngine();
    std::mt19937_64 random(0x52a1c4e9u);
    for (const bool inverse : {false, true})
        for (const Profile& profile : profiles)
        {
            const auto parameters =
                ghs(inverse, profile.amount, profile.bias, profile.symmetry, profile.low, profile.high);
            EXPECT_TRUE(ghsRowMatchesReference(engine, parameters, ghsRow(parameters, random, 263u)))
                << describe(parameters);
        }
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    for (unsigned trial = 0; trial < 192; ++trial)
    {
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        const double bias = an::kMinimumGeneralizedHyperbolicBias +
                            (an::kMaximumGeneralizedHyperbolicBias - an::kMinimumGeneralizedHyperbolicBias) * unit();
        const auto parameters = ghs(trial % 2 == 1, std::pow(10.0, -12.0 + 40.0 * unit()), bias, bounds[1],
                                    trial % 4 == 0 ? 0.0 : bounds[0], trial % 4 == 0 ? 1.0 : bounds[2]);
        EXPECT_TRUE(ghsRowMatchesReference(engine, parameters, ghsRow(parameters, random, 263u)))
            << "seed=0x52a1c4e9 trial=" << trial << ' ' << describe(parameters);
    }
}

TEST(StretchTest, AGeneralizedHyperbolicBiasIsAcceptedOverTheDialogsRangeAndRefusedBeyondIt)
{
    for (const double bias : {an::kMinimumGeneralizedHyperbolicBias, an::kMaximumGeneralizedHyperbolicBias})
    {
        const auto accepted = an::evaluateGeneralizedStretch(0.25, ghs(false, 3.0, bias, 0.375, 0.125, 0.75));
        ASSERT_TRUE(accepted.hasValue()) << bias << ": " << accepted.status().message();
        EXPECT_NEAR(*accepted,
                    static_cast<double>(GhsReference(ghs(false, 3.0, bias, 0.375, 0.125, 0.75)).forward(0.25)), 1.0e-12)
            << bias;
        const auto inverted = an::evaluateGeneralizedStretch(*accepted, ghs(true, 3.0, bias, 0.375, 0.125, 0.75));
        ASSERT_TRUE(inverted.hasValue()) << bias;
        EXPECT_NEAR(*inverted, 0.25, 1.0e-12) << bias;
    }
    for (const double bias : {std::nextafter(an::kMinimumGeneralizedHyperbolicBias, -10.0),
                              std::nextafter(an::kMaximumGeneralizedHyperbolicBias, 20.0)})
    {
        const auto refused = an::evaluateGeneralizedStretch(0.25, ghs(false, 3.0, bias, 0.375, 0.125, 0.75));
        ASSERT_FALSE(refused.hasValue()) << bias;
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument) << bias;
    }
}

TEST(StretchTest, GeneralizedHyperbolicSeededParametersMatchTheFamilysDefinition)
{
    std::mt19937_64 random(0x3e61a9d5u);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    const auto engine = makeEngine();
    const std::array<double, 9> biases{-1.0, std::nextafter(-1.0, 0.0), -0.999999, -1.0e-12, 0.0, 1.0e-12, 0.5, -0.5,
                                       1.0};
    for (unsigned trial = 0; trial < 384; ++trial)
    {
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        auto parameters = ghs(trial % 2 == 1, std::pow(10.0, -12.0 + 50.0 * unit()),
                              trial % 3 == 0 ? biases[(trial / 3) % biases.size()] : -1.0 + 2.0 * unit(), bounds[1],
                              trial % 4 == 0 ? 0.0 : bounds[0], trial % 4 == 0 ? 1.0 : bounds[2]);
        if (trial % 11 == 0)
            parameters.symmetryPoint = parameters.lowProtection;
        if (trial % 13 == 0)
            parameters.highProtection = parameters.symmetryPoint;
        if (trial % 17 == 0)
            parameters.amount = 0.0;
        if (trial % 19 == 0)
            parameters.symmetryPoint = parameters.lowProtection = 0.0;
        if (trial % 23 == 0)
            parameters.symmetryPoint = parameters.highProtection = 1.0;
        EXPECT_TRUE(ghsRowMatchesReference(engine, parameters, ghsRow(parameters, random, 263u)))
            << "seed=0x3e61a9d5 trial=" << trial << ' ' << describe(parameters);
    }
}

TEST(StretchTest, GeneralizedHyperbolicExtremeAmountsStayFiniteAndMatchTheFamilysDefinition)
{
    const auto engine = makeEngine();
    std::mt19937_64 random(0x1f9c27e5u);
    for (const double amount :
         {1.0e20, 1.0e100, 1.0e300, (std::numeric_limits<double>::max)() / 2.0, (std::numeric_limits<double>::max)()})
        for (const double bias : {-1.0, -0.999999, -0.5, -1.0e-6, 0.0, 1.0e-6, 0.5, 1.0})
            for (const auto bounds : {std::array<double, 3>{0.125, 0.375, 0.75}, std::array<double, 3>{0.0, 0.0, 0.75},
                                      std::array<double, 3>{0.125, 1.0, 1.0}, std::array<double, 3>{0.0, 0.5, 1.0}})
                for (const bool inverse : {false, true})
                {
                    const auto parameters = ghs(inverse, amount, bias, bounds[1], bounds[0], bounds[2]);
                    EXPECT_TRUE(ghsRowMatchesReference(engine, parameters, ghsRow(parameters, random, 131u)))
                        << describe(parameters);
                }
}

TEST(StretchTest, GeneralizedHyperbolicInverseKeepsTheLeastPositiveSamplesShareOfTheLargestAmountsExtent)
{
    // At an amount of 1e300 the curve's whole rise is about 2e-300, and the least positive Float32's
    // share of it is below the least positive Float64. The curve is flat below the symmetry point to
    // within that share, so the value it reaches lies within about 1e-250 of the symmetry point - the
    // symmetry point itself in Float64 - and never at the low protection point. Written without an
    // extended-precision reference, so a platform whose long double is Float64 checks it too.
    const auto engine = makeEngine();
    const std::vector<float> input{std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::min(),
                                   1.0e-30f};
    for (const double amount :
         {1.0e300, (std::numeric_limits<double>::max)() / 2.0, (std::numeric_limits<double>::max)()})
        for (const double bias : {-0.5, -1.0e-6, 0.0, 1.0e-6, 0.5, 1.0})
            for (const auto bounds : {std::array<double, 3>{0.125, 0.375, 0.75}, std::array<double, 3>{0.0, 0.5, 1.0},
                                      std::array<double, 3>{0.125, 1.0, 1.0}})
            {
                const auto parameters = ghs(true, amount, bias, bounds[1], bounds[0], bounds[2]);
                const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, parameters);
                const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, parameters);
                const auto wide = stretchWideRow(engine, input, parameters);
                for (std::size_t i = 0; i < input.size(); ++i)
                {
                    EXPECT_NEAR(compiled[i], bounds[1], 1.0e-12) << describe(parameters) << ' ' << input[i];
                    EXPECT_NEAR(scalar[i], bounds[1], 1.0e-12) << describe(parameters) << ' ' << input[i];
                    EXPECT_NEAR(wide[i], bounds[1], 1.0e-12) << describe(parameters) << ' ' << input[i];
                    const auto evaluated = an::evaluateGeneralizedStretch(input[i], parameters);
                    ASSERT_TRUE(evaluated.hasValue());
                    EXPECT_NEAR(*evaluated, bounds[1], 1.0e-12) << describe(parameters) << ' ' << input[i];
                }
            }
}

TEST(StretchTest, GeneralizedHyperbolicCompiledRowsEqualTheirScalarAuthorityOnEveryQuantizedCode)
{
    // Every UInt16 capture code as the benchmark derives it, in a row whose width leaves a partial
    // final vector, for the audit and interactive profiles in both directions and one profile per
    // other member of the family.
    std::vector<float> input(65536u + 3u);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i % 65536u) / 65535.0f;
    const auto engine = makeEngine();
    for (const auto& [amount, bias, center] : std::vector<std::tuple<double, double, double>>{
             {2.0, -0.5, 0.25}, {3.0, -0.5, 0.05}, {5.0, -1.0, 0.1}, {5.0, 0.0, 0.1}, {5.0, 0.5, 0.1}})
        for (const bool inverse : {false, true})
        {
            const auto parameters = ghs(inverse, amount, bias, center, 0.0, 1.0);
            const GhsReference reference(parameters);
            const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, parameters);
            const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, parameters);
            std::size_t differing = 0u;
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                differing += compiled[i] != scalar[i] ? 1u : 0u;
                const long double expected = inverse ? reference.inverse(input[i]) : reference.forward(input[i]);
                EXPECT_NEAR(compiled[i], static_cast<double>(expected), 1.0e-6)
                    << describe(parameters) << ' ' << input[i];
                if (i >= 65536u)
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(compiled[i]),
                              std::bit_cast<std::uint32_t>(compiled[i - 65536u]));
            }
            // Distinct Float64 logarithms and exponentials rarely round to different Float32 values;
            // a count near the sample count would mean the compiled row computes something else.
            EXPECT_LE(differing, input.size() / 1000u) << describe(parameters);
            if (::testing::Test::HasFailure())
                return;
        }
}

TEST(StretchTest, GeneralizedHyperbolicMeetsTheNeighbouringRegimeAndTheIdentityContinuously)
{
    // Minus one and zero are where one member's formula becomes another's, and a vanishing amount
    // is where every member becomes the identity. Approaching either has to approach that curve,
    // including from a subnormal bias or amount no formula of the family can evaluate directly.
    const auto at = [](bool inverse, double amount, double bias, double x) {
        const auto result = an::evaluateGeneralizedStretch(x, ghs(inverse, amount, bias, 0.375, 0.125, 0.75));
        EXPECT_TRUE(result.hasValue()) << result.status().message();
        return result.hasValue() ? *result : -1.0;
    };
    const double tiny = std::numeric_limits<double>::denorm_min();
    for (const auto& [limit, near] : std::vector<std::pair<double, double>>{{-1.0, std::nextafter(-1.0, 0.0)},
                                                                            {0.0, -1.0e-30},
                                                                            {0.0, 1.0e-30},
                                                                            {0.0, -1.0e-300},
                                                                            {0.0, 1.0e-300},
                                                                            {0.0, -tiny},
                                                                            {0.0, tiny}})
        for (const double amount : {1.0e-6, 3.0, 100.0, 1.0e20})
            for (const bool inverse : {false, true})
                for (int i = 0; i <= 1024; ++i)
                {
                    const double x = static_cast<double>(i) / 1024.0;
                    EXPECT_NEAR(at(inverse, amount, near, x), at(inverse, amount, limit, x), 1.0e-12)
                        << "bias " << near << " amount " << amount << (inverse ? " inverse" : " forward") << " at "
                        << x;
                }
    for (const double amount : {tiny, 1.0e-300, 1.0e-100, 1.0e-30, 1.0e-18, 1.0e-15, 1.0e-12})
        for (const double bias : {-1.0, -0.999999, -0.5, -1.0e-6, 0.0, 1.0e-6, 0.5, 1.0})
            for (const bool inverse : {false, true})
                for (int i = 0; i <= 1024; ++i)
                {
                    const double x = static_cast<double>(i) / 1024.0;
                    EXPECT_NEAR(at(inverse, amount, bias, x), x, 4.0 * amount + 1.0e-15)
                        << "bias " << bias << " amount " << amount << (inverse ? " inverse" : " forward") << " at "
                        << x;
                }
}

namespace
{
[[nodiscard]] double asinhForwardReference(double x, const an::GeneralizedStretchParameters& p)
{
    return static_cast<double>(AsinhReference(p).forward(x));
}

[[nodiscard]] an::GeneralizedStretchParameters asinhStretch(bool inverse, double amount, double low, double symmetry,
                                                            double high)
{
    an::GeneralizedStretchParameters p;
    p.function = inverse ? an::StretchFunction::InverseAsinh : an::StretchFunction::Asinh;
    p.amount = amount;
    p.lowProtection = low;
    p.symmetryPoint = symmetry;
    p.highProtection = high;
    return p;
}

// Float32 neighbours of every landmark, the extremes a caller can pass, and uniform samples up to an
// odd width, sorted; so a forward row meets every piece boundary from each side and a compiled row's
// partial final vector is reached.
[[nodiscard]] std::vector<float> asinhForwardRow(const an::GeneralizedStretchParameters& p, std::mt19937_64& random,
                                                 std::size_t width)
{
    std::vector<float> input;
    for (const double landmark : {0.0, p.lowProtection, p.symmetryPoint, p.highProtection, 1.0})
    {
        const float center = static_cast<float>(landmark);
        for (const float value : {std::nextafter(center, -1.0f), center, center, std::nextafter(center, 2.0f)})
            input.push_back(value);
    }
    for (const float value :
         {-10.0f, 10.0f, -0.0f, 0.0f, 1.0f, std::numeric_limits<float>::denorm_min(), std::nextafter(1.0f, 0.0f)})
        input.push_back(value);
    while (input.size() < width)
        input.push_back(static_cast<float>(static_cast<double>(random() >> 11u) * 0x1p-53));
    std::sort(input.begin(), input.end());
    return input;
}

// Every property the benchmark oracle holds a complete forward output to, for one sorted Float32 row
// under both backends, and the Float64 image path and scalar calls within 1e-12 of the reference with
// the same endpoints. Reports the first failure and how many checks failed.
[[nodiscard]] ::testing::AssertionResult asinhForwardRowMatchesReference(const an::EngineContext& engine,
                                                                         const an::GeneralizedStretchParameters& p,
                                                                         const std::vector<float>& input)
{
    const AsinhReference reference(p);
    const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, p);
    const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, p);
    const auto wide = stretchWideRow(engine, input, p);
    std::size_t failures = 0u;
    std::ostringstream first;
    first.precision(17);
    const auto fail = [&](std::size_t i, const char* what, double value, double expected) {
        if (failures++ == 0u)
            first << what << " at sample " << i << " = " << input[i] << ": " << value << " against " << expected;
    };
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const double target = static_cast<double>(reference.forward(input[i]));
        for (const auto* output : {&compiled, &scalar})
        {
            const float value = (*output)[i];
            if (!(std::abs(value - target) <= 1.0e-6))
                fail(i, "error", value, target);
            if (!(value >= 0.0f && value <= 1.0f))
                fail(i, "range", value, target);
            if (input[i] <= 0.0f && std::bit_cast<std::uint32_t>(value) != 0u)
                fail(i, "positive zero", value, 0.0);
            if (input[i] >= 1.0f && value != 1.0f)
                fail(i, "one", value, 1.0);
            if (i != 0u && (*output)[i - 1] > value)
                fail(i, "ordering", value, (*output)[i - 1]);
            if (i != 0u && input[i] == input[i - 1] &&
                std::bit_cast<std::uint32_t>((*output)[i - 1]) != std::bit_cast<std::uint32_t>(value))
                fail(i, "equal inputs", value, (*output)[i - 1]);
        }
        // The compiled row evaluates the same Float64 formula through the vector library's inverse
        // hyperbolic sine, so after narrowing it differs from the scalar authority by at most one
        // Float32 step, plus a few Float64 roundings where the result cancels to near zero.
        if (!(std::abs(compiled[i] - scalar[i]) <= std::abs(std::nextafter(scalar[i], 2.0f) - scalar[i]) + 1.0e-14f))
            fail(i, "backends", compiled[i], scalar[i]);
        if (!(std::abs(wide[i] - target) <= 1.0e-12 && wide[i] >= 0.0 && wide[i] <= 1.0))
            fail(i, "Float64 image", wide[i], target);
        if (input[i] <= 0.0f && std::bit_cast<std::uint64_t>(wide[i]) != 0u)
            fail(i, "Float64 positive zero", wide[i], 0.0);
        if (input[i] >= 1.0f && wide[i] != 1.0)
            fail(i, "Float64 one", wide[i], 1.0);
        const auto evaluated = an::evaluateGeneralizedStretch(input[i], p);
        if (!evaluated.hasValue() || !(std::abs(*evaluated - target) <= 1.0e-12))
            fail(i, "scalar call", evaluated.hasValue() ? *evaluated : -1.0, target);
    }
    if (failures == 0u)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << failures << " failed checks; first: " << first.str();
}

// Each sample through the forward row and back through the inverse row under one backend, against
// what two Float32 roundings allow: the forward's carried back through the curve's least slope, and
// the inverse's own.
[[nodiscard]] ::testing::AssertionResult asinhRowRoundTrips(const an::EngineContext& engine, an::ComputeBackend backend,
                                                            const an::GeneralizedStretchParameters& forward,
                                                            const std::vector<float>& input)
{
    auto inverse = forward;
    inverse.function = an::StretchFunction::InverseAsinh;
    const auto stretched = stretchRow(engine, backend, input, forward);
    const auto back = stretchRow(engine, backend, stretched, inverse);
    const long double bound = 0x1p-23L * (1.0L / AsinhReference(forward).minimumSlope() + 1.0L);
    std::size_t failures = 0u;
    std::ostringstream first;
    first.precision(9);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const long double residual = std::abs(static_cast<long double>(back[i]) - std::clamp(input[i], 0.0f, 1.0f));
        if (!(residual <= bound) && failures++ == 0u)
            first << "sample " << i << " = " << input[i] << " returns as " << back[i] << " through " << stretched[i]
                  << ", against a bound of " << static_cast<double>(bound);
    }
    if (failures == 0u)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << failures << " samples did not round-trip; first: " << first.str();
}
} // namespace

TEST(StretchTest, ForwardAsinhMapsZeroAndBelowToPositiveZeroAndOneAndAboveToOne)
{
    const auto engine = makeEngine();
    for (const double amount : {0.0, 2.5, 1.0e160})
        for (const auto bounds : {std::array<double, 3>{0.1, 0.35, 0.9}, std::array<double, 3>{0.0, 0.35, 1.0}})
        {
            const auto p = asinhStretch(false, amount, bounds[0], bounds[1], bounds[2]);
            SCOPED_TRACE(describe(p));
            for (const double x : {-0.0, -3.0, 0.0})
            {
                const auto result = an::evaluateGeneralizedStretch(x, p);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_EQ(std::bit_cast<std::uint64_t>(*result), 0u) << x;
            }
            for (const double x : {1.0, 2.0})
                EXPECT_EQ(*an::evaluateGeneralizedStretch(x, p), 1.0) << x;
            EXPECT_LE(*an::evaluateGeneralizedStretch(std::nextafter(1.0, 0.0), p), 1.0);
            const std::vector<float> input{-0.0f, 0.0f, 1.0f, 2.0f, -3.0f, std::nextafter(1.0f, 0.0f)};
            for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
            {
                const auto output = stretchRow(engine, backend, input, p);
                for (const float value : {output[0], output[1], output[4]})
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(value), 0u);
                EXPECT_EQ(output[2], 1.0f);
                EXPECT_EQ(output[3], 1.0f);
                EXPECT_LE(output[5], 1.0f);
            }
            const auto wide = stretchWideRow(engine, input, p);
            for (const double value : {wide[0], wide[1], wide[4]})
                EXPECT_EQ(std::bit_cast<std::uint64_t>(value), 0u);
            EXPECT_EQ(wide[2], 1.0);
            EXPECT_EQ(wide[3], 1.0);
            EXPECT_LE(wide[5], 1.0);
        }
    // A zero amount is the identity on every representable value of the unit interval.
    const auto identity = asinhStretch(false, 0.0, 0.1, 0.35, 0.9);
    for (const double x : {std::numeric_limits<double>::denorm_min(), 1.0e-30, 0.25, std::nextafter(1.0, 0.0)})
        EXPECT_EQ(*an::evaluateGeneralizedStretch(x, identity), x);
}

TEST(StretchTest, AsinhAmountsTooSmallToBendTheCurveAreTheIdentityInBothDirections)
{
    // Below 2^-60 every piece of the normalized curve differs from its input by less than 2^-120
    // relative, and a subnormal amount's reciprocal is not finite; such an amount is the identity.
    const auto engine = makeEngine();
    const std::vector<float> input{-1.0f,    -0.0f,  0.0f,  std::numeric_limits<float>::denorm_min(),
                                   1.0e-30f, 0.125f, 0.35f, std::nextafter(1.0f, 0.0f),
                                   1.0f,     2.0f};
    for (const double amount : {std::numeric_limits<double>::denorm_min(), 1.0e-310, 1.0e-300, 0x1p-61})
        for (const bool inverse : {false, true})
        {
            const auto p = asinhStretch(inverse, amount, 0.1, 0.35, 0.9);
            SCOPED_TRACE(::testing::Message() << (inverse ? "inverse" : "forward") << " amount=" << amount);
            for (const float x : input)
            {
                const double expected = std::clamp(static_cast<double>(x), 0.0, 1.0) + 0.0;
                const auto result = an::evaluateGeneralizedStretch(x, p);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_EQ(std::bit_cast<std::uint64_t>(*result), std::bit_cast<std::uint64_t>(expected)) << x;
            }
            for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
            {
                const auto output = stretchRow(engine, backend, input, p);
                for (std::size_t i = 0; i < input.size(); ++i)
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(output[i]),
                              std::bit_cast<std::uint32_t>(std::clamp(input[i], 0.0f, 1.0f) + 0.0f))
                        << input[i];
            }
        }
}

TEST(StretchTest, ForwardAsinhSeededParametersAndCompleteSamplesMatchIndependentPrimitive)
{
    std::mt19937_64 random(0x3e61a9d5u);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    const auto engine = makeEngine();
    for (unsigned trial = 0; trial < 512; ++trial)
    {
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        auto p = asinhStretch(false, std::pow(10.0, -14.0 + 22.0 * unit()), trial % 4 == 0 ? 0.0 : bounds[0], bounds[1],
                              trial % 4 == 0 ? 1.0 : bounds[2]);
        if (trial % 11 == 0)
            p.symmetryPoint = p.lowProtection;
        if (trial % 13 == 0)
            p.highProtection = p.symmetryPoint;
        if (trial % 17 == 0)
            p.amount = 0.0;
        if (trial % 19 == 0)
            p.symmetryPoint = p.lowProtection = 0.0;
        if (trial % 23 == 0)
            p.symmetryPoint = p.highProtection = 1.0;
        ASSERT_TRUE(asinhForwardRowMatchesReference(engine, p, asinhForwardRow(p, random, 263u)))
            << "seed=0x3e61a9d5 trial=" << trial << " " << describe(p);
    }
}

TEST(StretchTest, ForwardAsinhExtremeAmountsNeverOverflowAndFollowTheCurve)
{
    // Above about 1.34e154 the square of the amount is not finite; the tangent slopes must not need it.
    const auto engine = makeEngine();
    std::mt19937_64 random(0x49b2e07cu);
    for (const double amount : {1.0e-300, 1.0e-100, 1.0e-20, 1.0e-6, 1.0e6, 1.0e12, 1.0e100, 1.0e150, 1.0e160, 1.0e200,
                                1.0e300, static_cast<double>((std::numeric_limits<float>::max)()),
                                (std::numeric_limits<double>::max)() / 2.0, (std::numeric_limits<double>::max)()})
        for (const auto bounds : {std::array<double, 3>{0.4, 0.5, 0.6}, std::array<double, 3>{0.0, 0.25, 1.0},
                                  std::array<double, 3>{0.02, 0.93, 0.98}, std::array<double, 3>{0.5, 0.5, 0.5},
                                  std::array<double, 3>{0.3, 0.7, 0.7}, std::array<double, 3>{0.3, 0.3, 0.7}})
        {
            const auto p = asinhStretch(false, amount, bounds[0], bounds[1], bounds[2]);
            ASSERT_TRUE(asinhForwardRowMatchesReference(engine, p, asinhForwardRow(p, random, 131u))) << describe(p);
        }
}

TEST(StretchTest, ForwardAsinhCompiledRowEqualsItsScalarAuthorityOnEveryQuantizedCode)
{
    // Every UInt16 capture code as the benchmark derives it, in a row whose width leaves a partial
    // final vector, for the audit and interactive profiles.
    std::vector<float> input(65536u + 3u);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i % 65536u) / 65535.0f;
    const auto engine = makeEngine();
    for (const auto [amount, center] : {std::pair{2.0, 0.25}, std::pair{3.0, 0.05}})
    {
        const auto p = asinhStretch(false, amount, 0.0, center, 1.0);
        const AsinhReference reference(p);
        const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, p);
        const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, p);
        std::size_t differing = 0u;
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            differing += compiled[i] != scalar[i] ? 1u : 0u;
            EXPECT_NEAR(compiled[i], static_cast<double>(reference.forward(input[i])), 1.0e-6) << input[i];
            if (i >= 65536u)
                EXPECT_EQ(std::bit_cast<std::uint32_t>(compiled[i]),
                          std::bit_cast<std::uint32_t>(compiled[i - 65536u]));
        }
        // Distinct Float64 inverse hyperbolic sines rarely round to different Float32 values; a count
        // near the sample count would mean the compiled row computes something else.
        EXPECT_LE(differing, input.size() / 1000u) << amount;
    }
}

TEST(StretchTest, AsinhRoundTripsThroughBothRowsOnCaptureCodesAndGeneratedSamples)
{
    const auto engine = makeEngine();
    std::vector<float> codes(65536u + 3u);
    for (std::size_t i = 0; i < codes.size(); ++i)
        codes[i] = static_cast<float>(i % 65536u) / 65535.0f;
    for (const auto [amount, center] : {std::pair{2.0, 0.25}, std::pair{3.0, 0.05}})
        for (const auto backend : {an::ComputeBackend::Cpu, an::ComputeBackend::Reference})
            EXPECT_TRUE(asinhRowRoundTrips(engine, backend, asinhStretch(false, amount, 0.0, center, 1.0), codes))
                << amount;
    std::mt19937_64 random(0x0d5c7f21u);
    const auto unit = [&]() { return static_cast<double>(random() >> 11u) * 0x1p-53; };
    for (unsigned trial = 0; trial < 160; ++trial)
    {
        std::array<double, 3> bounds{unit(), unit(), unit()};
        std::sort(bounds.begin(), bounds.end());
        // Mostly the working range of amounts, and every eighth one from the admitted extremes.
        const double amount =
            trial % 8 == 7 ? std::pow(10.0, 150.0 + 158.0 * unit()) : std::pow(10.0, -8.0 + 12.0 * unit());
        const auto p = asinhStretch(false, (std::min)(amount, (std::numeric_limits<double>::max)()), bounds[0],
                                    bounds[1], bounds[2]);
        const auto input = asinhForwardRow(p, random, 263u);
        for (const auto backend : {an::ComputeBackend::Cpu, an::ComputeBackend::Reference})
            ASSERT_TRUE(asinhRowRoundTrips(engine, backend, p, input))
                << "seed=0x0d5c7f21 trial=" << trial << " " << describe(p);
    }
}

namespace
{
template <typename T>
void checkInverseLayouts(an::SampleType type, an::StretchFunction function,
                         double (*oracle)(double, const an::GeneralizedStretchParameters&))
{
    const auto engine = makeEngine();
    for (auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
    {
        an::ExecutionOptions options;
        options.backend = backend;
        auto execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue());
        for (unsigned width : {1u, 3u, 17u, 257u})
            for (unsigned channels : {1u, 3u, 4u})
                for (bool interleaved : {false, true})
                {
                    const auto extent = an::ImageExtent::create(width, 3u, channels, an::defaultEngineLimits());
                    const auto layout = an::ImageLayout::create({.extent = *extent,
                                                                 .elementSizeBytes = sizeof(T),
                                                                 .samplesPerPixel = interleaved ? channels : 1u,
                                                                 .planeCount = interleaved ? 1u : channels,
                                                                 .rowAlignmentBytes = 64u},
                                                                an::defaultEngineLimits());
                    ASSERT_TRUE(layout.has_value());
                    std::vector<T> input(layout->totalBytes() / sizeof(T), T{-5});
                    auto expected = input;
                    an::StretchImageRequest request;
                    request.stretch = typical(function);
                    request.channelEnabled[1] = false;
                    for (unsigned channel = 0; channel < channels; ++channel)
                        for (unsigned y = 0; y < 3; ++y)
                            for (unsigned x = 0; x < width; ++x)
                            {
                                const auto index = *layout->sampleByteOffset(channel / layout->samplesPerPixel(), y, x,
                                                                             channel % layout->samplesPerPixel()) /
                                                   sizeof(T);
                                input[index] =
                                    static_cast<T>((x + channel + y) / static_cast<double>(width + channels + 1));
                                expected[index] = !request.channelEnabled[channel]
                                                      ? input[index]
                                                      : static_cast<T>(oracle(input[index], request.stretch));
                            }
                    const auto before = engine.memoryResource()->counters();
                    auto output = input;
                    const auto run =
                        an::stretchImage(*execution, type, input.data(), *layout, output.data(), *layout, request);
                    ASSERT_TRUE(run.hasValue()) << run.status().message();
                    EXPECT_EQ(run->run.outputsWritten, width * 3u * channels);
                    ASSERT_TRUE(
                        an::stretchImage(*execution, type, input.data(), *layout, input.data(), *layout, request)
                            .hasValue());
                    EXPECT_EQ(input, output);
                    for (std::size_t i = 0; i < output.size(); ++i)
                        EXPECT_NEAR(output[i], expected[i], sizeof(T) == 4 ? 1.0e-6 : 2.0e-13);
                    const auto after = engine.memoryResource()->counters();
                    EXPECT_EQ(after.liveBytes, before.liveBytes);
                    EXPECT_EQ(after.totalAllocations, before.totalAllocations);
                }
    }
}
} // namespace

TEST(StretchTest, InverseHistogramPaddedPlanarInterleavedAndAliasedLayouts)
{
    checkInverseLayouts<float>(an::SampleType::Float32, an::StretchFunction::InverseHistogram, histogramInverseOracle);
    checkInverseLayouts<double>(an::SampleType::Float64, an::StretchFunction::InverseHistogram, histogramInverseOracle);
}

TEST(StretchTest, InverseAsinhPaddedPlanarInterleavedAndAliasedLayouts)
{
    checkInverseLayouts<float>(an::SampleType::Float32, an::StretchFunction::InverseAsinh, asinhInverseReference);
    checkInverseLayouts<double>(an::SampleType::Float64, an::StretchFunction::InverseAsinh, asinhInverseReference);
}

TEST(StretchTest, ForwardAsinhPaddedPlanarInterleavedAndAliasedLayouts)
{
    checkInverseLayouts<float>(an::SampleType::Float32, an::StretchFunction::Asinh, asinhForwardReference);
    checkInverseLayouts<double>(an::SampleType::Float64, an::StretchFunction::Asinh, asinhForwardReference);
}

TEST(StretchTest, GeneralizedHyperbolicPaddedPlanarInterleavedAndAliasedLayouts)
{
    for (const auto function :
         {an::StretchFunction::GeneralizedHyperbolic, an::StretchFunction::InverseGeneralizedHyperbolic})
    {
        checkInverseLayouts<float>(an::SampleType::Float32, function, ghsReference);
        checkInverseLayouts<double>(an::SampleType::Float64, function, ghsReference);
    }
}

TEST(StretchTest, NonlinearStretchValidityMasksAndPrecancelledCallsKeepDestinations)
{
    for (const auto function : {an::StretchFunction::InverseHistogram, an::StretchFunction::Asinh,
                                an::StretchFunction::InverseAsinh, an::StretchFunction::GeneralizedHyperbolic,
                                an::StretchFunction::InverseGeneralizedHyperbolic, an::StretchFunction::ScreenTransfer})
    {
        SCOPED_TRACE(static_cast<int>(function));
        const auto engine = makeEngine();
        const auto execution = makeExecution(engine);
        const auto layout = layoutOf(17u, 1u, 1u);
        std::vector<float> input(17, 0.3f), output(17, -5.0f);
        input[3] = std::numeric_limits<float>::quiet_NaN();
        input[4] = std::numeric_limits<float>::infinity();
        an::StretchImageRequest request;
        request.stretch = typical(function);
        auto run =
            an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request);
        ASSERT_FALSE(run.hasValue());
        EXPECT_EQ(run.status().code(), an::ErrorCode::InvalidData);
        an::KernelValidityView validity;
        validity.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        run = an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request,
                               validity);
        ASSERT_TRUE(run.hasValue());
        EXPECT_EQ(run->run.selection.valid, 15u);
        EXPECT_EQ(output[3], -5.0f);
        EXPECT_EQ(output[4], -5.0f);
        std::array<std::uint8_t, 3> maskBytes{0xe7u, 0xffu, 0x01u};
        const auto mask = an::ValidityMaskView<const std::uint8_t>::createTight(maskBytes, layout.extent());
        ASSERT_TRUE(mask.has_value());
        validity.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        validity.mask = &*mask;
        const auto masked = an::stretchImage(execution, an::SampleType::Float32, input.data(), layout, output.data(),
                                             layout, request, validity);
        ASSERT_TRUE(masked.hasValue());
        EXPECT_EQ(masked->run.selection.valid, 15u);
        EXPECT_EQ(output[3], -5.0f);
        EXPECT_EQ(output[4], -5.0f);
        for (bool deadline : {false, true})
        {
            const auto otherEngine = makeEngine();
            std::stop_source stop;
            an::ExecutionOptions options;
            if (deadline)
                options.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
            else
                options.stopToken = stop.get_token();
            auto stopped = an::ExecutionContext::create(otherEngine, options);
            ASSERT_TRUE(stopped.hasValue());
            if (deadline)
                std::this_thread::sleep_until(*options.deadline);
            else
                stop.request_stop();
            const auto saved = output;
            run = an::stretchImage(*stopped, an::SampleType::Float32, input.data(), layout, output.data(), layout,
                                   request);
            ASSERT_FALSE(run.hasValue());
            EXPECT_EQ(run.status().code(), deadline ? an::ErrorCode::DeadlineExceeded : an::ErrorCode::Cancelled);
            EXPECT_EQ(output, saved);
        }
    }
}

TEST(StretchTest, GeneralizedAndHistogramForwardCurvesMatchRecordedReferences)
{
    an::GeneralizedStretchParameters logarithmic = typical(an::StretchFunction::GeneralizedHyperbolic);
    logarithmic.bias = -1.0;
    for (const double x : {0.04, 0.2, 0.5, 0.95})
    {
        const an::Result<double> value = an::evaluateGeneralizedStretch(x, logarithmic);
        ASSERT_TRUE(value.hasValue()) << value.status().message();
        EXPECT_NEAR(*value, referenceLogGhs(x, logarithmic), 1.0e-12);
    }

    const an::GeneralizedStretchParameters histogram = typical(an::StretchFunction::Histogram);
    for (const double x : {0.05, 0.2, 0.5, 0.95})
    {
        const an::Result<double> value = an::evaluateGeneralizedStretch(x, histogram);
        ASSERT_TRUE(value.hasValue()) << value.status().message();
        EXPECT_NEAR(*value, referenceHistogram(x, histogram), 1.0e-12);
    }
}

TEST(StretchTest, EveryNonlinearInverseRoundTripsItsForwardFamily)
{
    for (const double bias : {-0.5, 0.0, 0.5})
    {
        an::GeneralizedStretchParameters forward = typical(an::StretchFunction::GeneralizedHyperbolic);
        forward.bias = bias;
        an::GeneralizedStretchParameters inverse = forward;
        inverse.function = an::StretchFunction::InverseGeneralizedHyperbolic;
        for (const double x : {0.02, 0.18, 0.42, 0.82})
        {
            const an::Result<double> y = an::evaluateGeneralizedStretch(x, forward);
            ASSERT_TRUE(y.hasValue());
            const an::Result<double> roundTrip = an::evaluateGeneralizedStretch(*y, inverse);
            ASSERT_TRUE(roundTrip.hasValue());
            EXPECT_NEAR(*roundTrip, x, 2.0e-13) << bias;
        }
    }
    for (const auto [forwardFunction, inverseFunction] :
         {std::pair{an::StretchFunction::Histogram, an::StretchFunction::InverseHistogram},
          std::pair{an::StretchFunction::Asinh, an::StretchFunction::InverseAsinh}})
    {
        an::GeneralizedStretchParameters forward = typical(forwardFunction);
        an::GeneralizedStretchParameters inverse = forward;
        inverse.function = inverseFunction;
        for (const double x : {0.02, 0.18, 0.42, 0.82})
        {
            const double y = *an::evaluateGeneralizedStretch(x, forward);
            EXPECT_NEAR(*an::evaluateGeneralizedStretch(y, inverse), x, 2.0e-13);
        }
    }
}

TEST(StretchTest, LinearInversionAndMidtonesKeepTheirEndpointRules)
{
    an::GeneralizedStretchParameters linear = typical(an::StretchFunction::Linear);
    linear.blackPoint = 0.2;
    linear.whitePoint = 0.8;
    EXPECT_DOUBLE_EQ(*an::evaluateGeneralizedStretch(0.1, linear), 0.0);
    EXPECT_NEAR(*an::evaluateGeneralizedStretch(0.5, linear), 0.5, 1.0e-15);
    EXPECT_DOUBLE_EQ(*an::evaluateGeneralizedStretch(0.9, linear), 1.0);

    an::GeneralizedStretchParameters inversion = typical(an::StretchFunction::Invert);
    EXPECT_DOUBLE_EQ(*an::evaluateGeneralizedStretch(0.2, inversion), 0.8);

    const an::MidtonesTransferParameters mtf{.midtones = 0.23, .shadows = 0.1, .highlights = 0.9};
    EXPECT_DOUBLE_EQ(*an::evaluateMidtonesTransfer(0.05, mtf), 0.0);
    EXPECT_DOUBLE_EQ(*an::evaluateMidtonesTransfer(0.95, mtf), 1.0);
    for (const double x : {0.1, 0.25, 0.5, 0.75, 0.9})
    {
        const double y = *an::evaluateMidtonesTransfer(x, mtf);
        EXPECT_NEAR(*an::evaluateInverseMidtonesTransfer(y, mtf), x, 2.0e-15);
    }
}

TEST(StretchTest, LinkedAndUnlinkedAutomaticScreenTransfersFollowTheirRules)
{
    constexpr std::array statistics{an::StretchChannelStatistics{.median = 0.2, .medianAbsoluteDeviation = 0.03},
                                    an::StretchChannelStatistics{.median = 0.3, .medianAbsoluteDeviation = 0.04},
                                    an::StretchChannelStatistics{.median = 0.4, .medianAbsoluteDeviation = 0.05}};
    const an::Result<an::MidtonesTransferParameters> linked = an::deriveLinkedAutoStretch(statistics, -2.8, 0.25);
    ASSERT_TRUE(linked.hasValue()) << linked.status().message();
    EXPECT_GE(linked->shadows, 0.0);
    EXPECT_EQ(linked->highlights, 1.0);
    EXPECT_GT(linked->midtones, 0.0);
    EXPECT_LT(linked->midtones, 1.0);

    std::array<an::MidtonesTransferParameters, 3u> unlinked{};
    ASSERT_TRUE(an::deriveUnlinkedAutoStretch(statistics, -2.8, 0.25, unlinked).ok());
    EXPECT_NE(unlinked[0], unlinked[1]);
    EXPECT_NE(unlinked[1], unlinked[2]);
}

TEST(StretchTest, IndependentImageStretchSupportsInterleavingSelectionAndExactAliasing)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(1u, 1u, 3u, true);
    std::vector<float> pixel(layout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(pixel, layout, 0u, 0u, 0u) = 0.2f;
    sampleAt(pixel, layout, 1u, 0u, 0u) = 0.4f;
    sampleAt(pixel, layout, 2u, 0u, 0u) = 0.8f;
    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::Invert;
    request.channelEnabled = {true, false, true, false};

    const an::Result<an::StretchImageResult> result =
        an::stretchImage(execution, an::SampleType::Float32, pixel.data(), layout, pixel.data(), layout, request);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->run.outputsWritten, 3u);
    EXPECT_FLOAT_EQ(sampleAt(pixel, layout, 0u, 0u, 0u), 0.8f);
    EXPECT_FLOAT_EQ(sampleAt(pixel, layout, 1u, 0u, 0u), 0.4f);
    EXPECT_FLOAT_EQ(sampleAt(pixel, layout, 2u, 0u, 0u), 0.2f);
}

TEST(StretchTest, LightnessSaturationAndColorModesKeepTheirMeaning)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf(1u, 1u, 3u);
    std::vector<float> source(layout.totalBytes() / sizeof(float), 0.0f);
    std::vector<float> output(layout.totalBytes() / sizeof(float), 0.0f);
    sampleAt(source, layout, 0u, 0u, 0u) = 0.2f;
    sampleAt(source, layout, 1u, 0u, 0u) = 0.6f;
    sampleAt(source, layout, 2u, 0u, 0u) = 0.4f;
    const std::array<double, 3u> beforeRgb{0.2, 0.6, 0.4};
    const std::array<double, 3u> beforeLab = toLab(beforeRgb);

    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::Invert;
    request.colorMode = an::StretchColorMode::Lightness;
    request.colorEncoding.transfer = an::ColorTransferFunction::Srgb;
    ASSERT_TRUE(
        an::stretchImage(execution, an::SampleType::Float32, source.data(), layout, output.data(), layout, request)
            .hasValue());
    std::array<double, 3u> afterRgb{sampleAt(output, layout, 0u, 0u, 0u), sampleAt(output, layout, 1u, 0u, 0u),
                                    sampleAt(output, layout, 2u, 0u, 0u)};
    EXPECT_NEAR(toLab(afterRgb)[0], 1.0 - beforeLab[0], 2.0e-3);

    request.colorMode = an::StretchColorMode::Saturation;
    ASSERT_TRUE(
        an::stretchImage(execution, an::SampleType::Float32, source.data(), layout, output.data(), layout, request)
            .hasValue());
    afterRgb = {sampleAt(output, layout, 0u, 0u, 0u), sampleAt(output, layout, 1u, 0u, 0u),
                sampleAt(output, layout, 2u, 0u, 0u)};
    EXPECT_NEAR(toLab(afterRgb)[0], beforeLab[0], 2.0e-4);

    sampleAt(source, layout, 0u, 0u, 0u) = 0.05f;
    sampleAt(source, layout, 1u, 0u, 0u) = 0.05f;
    sampleAt(source, layout, 2u, 0u, 0u) = 0.2f;
    request.colorMode = an::StretchColorMode::Color;
    request.overflow = an::StretchOverflowPolicy::Clip;
    ASSERT_TRUE(
        an::stretchImage(execution, an::SampleType::Float32, source.data(), layout, output.data(), layout, request)
            .hasValue());
    EXPECT_NEAR(sampleAt(output, layout, 0u, 0u, 0u), 0.45, 1.0e-6);
    EXPECT_NEAR(sampleAt(output, layout, 1u, 0u, 0u), 0.45, 1.0e-6);
    EXPECT_NEAR(sampleAt(output, layout, 2u, 0u, 0u), 1.0, 1.0e-6);

    request.overflow = an::StretchOverflowPolicy::Rescale;
    ASSERT_TRUE(
        an::stretchImage(execution, an::SampleType::Float32, source.data(), layout, output.data(), layout, request)
            .hasValue());
    EXPECT_NEAR(sampleAt(output, layout, 0u, 0u, 0u), 0.25, 1.0e-6);
    EXPECT_NEAR(sampleAt(output, layout, 1u, 0u, 0u), 0.25, 1.0e-6);
    EXPECT_NEAR(sampleAt(output, layout, 2u, 0u, 0u), 1.0, 1.0e-6);
}

TEST(StretchTest, HistogramRemappingConservesCountsAndUsesTheSameCurve)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<std::uint64_t, 5u> source{2u, 3u, 5u, 7u, 11u};
    std::array<std::uint64_t, 5u> destination{};
    an::GeneralizedStretchParameters parameters;
    parameters.function = an::StretchFunction::Invert;
    ASSERT_TRUE(an::remapStretchHistogram(execution, source, destination, parameters).ok());
    EXPECT_EQ(destination, (std::array<std::uint64_t, 5u>{11u, 7u, 5u, 3u, 2u}));
    EXPECT_EQ(std::accumulate(destination.begin(), destination.end(), std::uint64_t{0}), 28u);
    std::mt19937_64 random(0x986d3b1u);
    std::vector<std::uint64_t> counts(257), mapped(257), expected(257);
    for (auto& count : counts)
        count = random() % 101u;
    parameters = typical(an::StretchFunction::InverseHistogram);
    for (double amount : {0.0, 0.1, 2.5, 1000.0})
    {
        parameters.amount = amount;
        std::fill(expected.begin(), expected.end(), 0u);
        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            const double value = histogramInverseOracle(static_cast<double>(i) / 256.0, parameters);
            expected[static_cast<std::size_t>(std::llround(value * 256.0))] += counts[i];
        }
        ASSERT_TRUE(an::remapStretchHistogram(execution, counts, mapped, parameters).ok());
        EXPECT_EQ(mapped, expected) << amount;
    }
}

namespace
{
// A screen transfer with the same midtones transfer on every channel.
[[nodiscard]] an::GeneralizedStretchParameters screenTransfer(double midtones, double shadows, double highlights)
{
    an::GeneralizedStretchParameters parameters;
    parameters.function = an::StretchFunction::ScreenTransfer;
    const an::MidtonesTransferParameters transfer{.midtones = midtones, .shadows = shadows, .highlights = highlights};
    parameters.screenTransfer = {transfer, transfer, transfer};
    return parameters;
}

// The midtones transfer from its definition, `(m - 1) x / ((2m - 1) x - m)` over the rescaled input,
// in long double; the engine evaluates the same fraction multiplied through by the endpoint range.
[[nodiscard]] long double midtonesReference(long double value, const an::MidtonesTransferParameters& p)
{
    if (value <= p.shadows)
        return 0.0L;
    if (value >= p.highlights)
        return 1.0L;
    const long double x = (value - p.shadows) / (static_cast<long double>(p.highlights) - p.shadows);
    const long double m = p.midtones;
    return (m - 1.0L) * x / ((2.0L * m - 1.0L) * x - m);
}

// Every profile the benchmark's boundary rows name, the two capture profiles, and equal endpoints at
// one, where clamping an input above one decides which endpoint rule applies.
[[nodiscard]] std::vector<an::MidtonesTransferParameters> midtonesProfiles()
{
    const auto f = [](double value) { return static_cast<double>(static_cast<float>(value)); };
    std::vector<an::MidtonesTransferParameters> profiles;
    for (const auto& [m, s, h] : std::vector<std::tuple<double, double, double>>{{0.25, 0.0, 1.0},
                                                                                 {0.03, 0.0, 1.0},
                                                                                 {0.5, 0.0, 1.0},
                                                                                 {0.0, 0.0, 1.0},
                                                                                 {0.000001, 0.0, 1.0},
                                                                                 {0.999999, 0.0, 1.0},
                                                                                 {1.0, 0.0, 1.0},
                                                                                 {0.03, 0.02, 1.0},
                                                                                 {0.25, 0.0, 0.75},
                                                                                 {0.25, 0.125, 0.75},
                                                                                 {0.000001, 0.125, 0.75},
                                                                                 {0.999999, 0.125, 0.75},
                                                                                 {0.25, 0.5, 0.50000095367431640625},
                                                                                 {0.25, 0.375, 0.375},
                                                                                 {0.25, 0.0, 0.0},
                                                                                 {0.25, 1.0, 1.0}})
        profiles.push_back({.midtones = f(m), .shadows = f(s), .highlights = f(h)});
    return profiles;
}

// The distance between two Float32 values in steps, through their order-preserving integer images,
// so that the two zeros are one value and a step across zero counts once.
[[nodiscard]] std::uint64_t float32Steps(float a, float b)
{
    const auto ordered = [](float value) {
        const std::int64_t bits = std::bit_cast<std::int32_t>(value);
        return bits < 0 ? -(bits & 0x7fffffff) : bits;
    };
    const std::int64_t distance = ordered(a) - ordered(b);
    return static_cast<std::uint64_t>(distance < 0 ? -distance : distance);
}

// How far the compiled Float32 midtones transfer may be from its scalar authority, in Float32 steps.
constexpr std::uint64_t kMidtonesTransferFloat32Steps = 12u;

// Whether a transfer is too extreme for the compiled row to evaluate in Float32: a balance, a
// complement or an endpoint range below `2^-60`, which it evaluates in Float64 instead.
[[nodiscard]] bool midtonesTransferIsEvaluatedInFloat64(const an::MidtonesTransferParameters& p)
{
    constexpr double floor = 0x1p-60;
    return !(p.midtones >= floor && 1.0 - p.midtones >= floor && p.highlights - p.shadows >= floor);
}

// Holds a compiled midtones transfer row to its scalar authority: every sample the authority selects
// as zero or one is that value exactly, every other sample is in the unit interval, is not negative
// zero and is within the stated number of Float32 steps - or bit-identical, for a transfer the row
// evaluates in Float64 - and every sample is within the benchmark oracle's tier of the definition.
[[nodiscard]] ::testing::AssertionResult midtonesRowWithinTier(const std::vector<float>& input,
                                                               const std::vector<float>& compiled,
                                                               const std::vector<float>& scalar,
                                                               const an::MidtonesTransferParameters& p)
{
    const bool exact = midtonesTransferIsEvaluatedInFloat64(p);
    std::uint64_t failures = 0u;
    std::uint64_t widest = 0u;
    std::ostringstream first;
    first.precision(9);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const double clamped = std::clamp(static_cast<double>(input[i]), 0.0, 1.0);
        const bool selected = clamped <= p.shadows || clamped >= p.highlights;
        const std::uint64_t steps = float32Steps(compiled[i], scalar[i]);
        widest = (std::max)(widest, steps);
        const bool bitIdentical = std::bit_cast<std::uint32_t>(compiled[i]) == std::bit_cast<std::uint32_t>(scalar[i]);
        const bool ok =
            (selected || exact ? bitIdentical : steps <= kMidtonesTransferFloat32Steps) && compiled[i] >= 0.0f &&
            compiled[i] <= 1.0f && !std::signbit(compiled[i]) &&
            std::abs(static_cast<double>(compiled[i]) - static_cast<double>(midtonesReference(clamped, p))) <= 1.0e-6;
        if (!ok && failures++ == 0u)
            first << "input " << input[i] << " compiled " << compiled[i] << " scalar " << scalar[i] << " at " << steps
                  << " steps";
    }
    if (failures == 0u)
        return ::testing::AssertionSuccess() << "widest " << widest << " steps";
    return ::testing::AssertionFailure() << failures << " samples outside the tier; first: " << first.str();
}

// The Float32 values beside each endpoint and the first values above zero, with random samples of
// the unit interval and the values beyond it a caller can pass.
[[nodiscard]] std::vector<float> midtonesProbeInputs(const an::MidtonesTransferParameters& p, std::uint64_t seed)
{
    std::vector<float> input;
    for (const double endpoint : {p.shadows, p.highlights})
    {
        const float nearest = static_cast<float>(endpoint);
        float up = nearest;
        float down = nearest;
        for (int step = 0; step < 4096; ++step)
        {
            input.push_back(up);
            input.push_back(down);
            up = std::nextafter(up, 2.0f);
            down = std::nextafter(down, -1.0f);
        }
    }
    float tiny = std::numeric_limits<float>::denorm_min();
    for (int step = 0; step < 4096; ++step, tiny = std::nextafter(tiny, 1.0f))
        input.push_back(tiny);
    for (float normal = std::numeric_limits<float>::min(); normal < 1.0f; normal *= 1.0625f)
        input.push_back(normal);
    std::mt19937_64 random(seed);
    for (int sample = 0; sample < 65536; ++sample)
        input.push_back(std::bit_cast<float>(static_cast<std::uint32_t>(random() % 0x3f800001u)));
    input.insert(input.end(), {-10.0f, -0.0f, 0.0f, 1.0f, 10.0f, std::numeric_limits<float>::max()});
    return input;
}
} // namespace

TEST(StretchTest, MidtonesTransferKnownValuesComeFromTheDefinition)
{
    // Float32 parameters and inputs on each piece and at each balance class, evaluated from the
    // definition in exact rational arithmetic, independently of the engine.
    struct Known
    {
        float midtones;
        float shadows;
        float highlights;
        double input;
        double output;
    };
    constexpr std::array<Known, 18> known{{
        {0.25f, 0.0f, 1.0f, 1.25000000000000000e-01, 0.3},
        {0.25f, 0.0f, 1.0f, 7.50000000000000000e-01, 0.9},
        {0.03f, 0.02f, 1.0f, 2.50000003725290298e-02, 0.1422287618581031296175445629528049869792},
        {0.03f, 0.02f, 1.0f, 5.00000007450580597e-02, 0.5052083492225424830444877615373235477338},
        {0.03f, 0.02f, 1.0f, 9.90000009536743164e-01, 0.9996812582830520411542135454708138304866},
        {0.25f, 0.125f, 0.75f, 1.00000001490116119e-01, 0.0},
        {0.25f, 0.125f, 0.75f, 2.50000000000000000e-01, 0.4285714285714285714285714285714285714286},
        {0.25f, 0.125f, 0.75f, 5.00000000000000000e-01, 0.8181818181818181818181818181818181818182},
        {0.25f, 0.125f, 0.75f, 8.00000011920928955e-01, 1.0},
        {0.999999f, 0.0f, 1.0f, 5.00000000000000000e-01, 0.000001013278961181640625},
        {0.999999f, 0.0f, 1.0f, 9.99998986721038818e-01, 0.5},
        {0.999999f, 0.0f, 1.0f, 9.99999940395355225e-01, 0.9444444944829172969499829620103727060976},
        {0.000001f, 0.0f, 1.0f, 4.99999998737621354e-07, 0.3333332222221484286277274511843824613901},
        {0.000001f, 0.0f, 1.0f, 1.00000004749745131e-03, 0.9990019960579039759767008596865587058160},
        {0.5f, 0.2f, 0.6f, 3.00000011920928955e-01, 0.2500000093132252604322372151489518132607},
        {0.0f, 0.0f, 1.0f, 3.00000011920928955e-01, 1.0},
        {1.0f, 0.0f, 1.0f, 3.00000011920928955e-01, 0.0},
        {1.0f, 0.0f, 1.0f, 9.99999940395355225e-01, 0.0},
    }};
    const auto engine = makeEngine();
    for (const Known& value : known)
    {
        const an::MidtonesTransferParameters transfer{
            .midtones = value.midtones, .shadows = value.shadows, .highlights = value.highlights};
        SCOPED_TRACE(::testing::Message()
                     << value.midtones << ' ' << value.shadows << ' ' << value.highlights << " at " << value.input);
        const an::Result<double> scalar = an::evaluateMidtonesTransfer(value.input, transfer);
        ASSERT_TRUE(scalar.hasValue()) << scalar.status().message();
        EXPECT_NEAR(*scalar, value.output, 6.0e-16);
        const auto parameters = screenTransfer(value.midtones, value.shadows, value.highlights);
        EXPECT_NEAR(*an::evaluateGeneralizedStretch(value.input, parameters), value.output, 6.0e-16);
        EXPECT_NEAR(stretchWideRow(engine, {static_cast<float>(value.input)}, parameters)[0], value.output, 6.0e-16);
        for (const auto backend : {an::ComputeBackend::Cpu, an::ComputeBackend::Reference})
            EXPECT_NEAR(stretchRow(engine, backend, {static_cast<float>(value.input)}, parameters)[0], value.output,
                        6.0e-8);
    }
}

TEST(StretchTest, MidtonesTransferAtABalanceOfOneIsPositiveZeroBelowTheHighlightPoint)
{
    // A balance of one maps everything below the highlight point to zero; a zero result is positive
    // zero on every path, as it is below the shadow point.
    const an::MidtonesTransferParameters transfer{.midtones = 1.0, .shadows = 0.125, .highlights = 0.75};
    for (const double x : {0.0, 0.125, 0.2, 0.5, std::nextafter(0.75, 0.0)})
    {
        const an::Result<double> value = an::evaluateMidtonesTransfer(x, transfer);
        ASSERT_TRUE(value.hasValue()) << value.status().message();
        EXPECT_EQ(std::bit_cast<std::uint64_t>(*value), 0u) << x;
    }
    const auto engine = makeEngine();
    const auto parameters = screenTransfer(1.0, 0.125, 0.75);
    const std::vector<float> input{0.0f, 0.2f, 0.5f, 0.74f, std::nextafter(0.75f, 0.0f), 0.75f, 0.9f};
    for (const auto backend : {an::ComputeBackend::Cpu, an::ComputeBackend::Reference})
    {
        const auto output = stretchRow(engine, backend, input, parameters);
        for (std::size_t i = 0; i + 2u < input.size(); ++i)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(output[i]), 0u) << input[i];
        EXPECT_EQ(output[5], 1.0f);
        EXPECT_EQ(output[6], 1.0f);
    }
    for (const double value : stretchWideRow(engine, input, parameters))
        EXPECT_FALSE(std::signbit(value)) << value;
}

TEST(StretchTest, MidtonesTransferStaysInTheUnitIntervalBesideEndpointsItCannotSeparate)
{
    // In Float64 an input one step below the highlight point and the highlight point itself can round
    // to the same distance from the shadow point. The curve is still below the highlight point there,
    // so it must answer a value in the unit interval - zero at a balance of one - rather than a
    // quotient of two zeros.
    const double shadows = 3.0 * 0x1p-54;
    const double below = std::nextafter(1.0, 0.0);
    ASSERT_EQ(below - shadows, 1.0 - shadows) << "the two distances no longer round together";
    const auto engine = makeEngine();
    for (const double midtones : {1.0, std::nextafter(1.0, 0.0), 0.5, 0.25, 0.0})
    {
        SCOPED_TRACE(::testing::Message() << "midtones " << midtones);
        const an::MidtonesTransferParameters transfer{.midtones = midtones, .shadows = shadows, .highlights = 1.0};
        const an::Result<double> value = an::evaluateMidtonesTransfer(below, transfer);
        ASSERT_TRUE(value.hasValue()) << value.status().message();
        EXPECT_TRUE(*value >= 0.0 && *value <= 1.0) << *value;
        if (midtones == 1.0)
            EXPECT_EQ(std::bit_cast<std::uint64_t>(*value), 0u);
        std::vector<double> samples{shadows, std::nextafter(shadows, 1.0), 0.5, below, 1.0};
        std::vector<double> output(samples.size(), -1.0);
        const auto extent =
            an::ImageExtent::create(static_cast<std::uint32_t>(samples.size()), 1u, 1u, an::defaultEngineLimits());
        const auto layout = an::ImageLayout::create({.extent = *extent,
                                                     .elementSizeBytes = sizeof(double),
                                                     .samplesPerPixel = 1u,
                                                     .planeCount = 1u,
                                                     .rowAlignmentBytes = 1u},
                                                    an::defaultEngineLimits());
        an::StretchImageRequest request;
        request.stretch = screenTransfer(midtones, shadows, 1.0);
        for (const auto backend : {an::ComputeBackend::Cpu, an::ComputeBackend::Reference})
        {
            an::ExecutionOptions options;
            options.backend = backend;
            const auto execution = an::ExecutionContext::create(engine, options);
            const auto run = an::stretchImage(*execution, an::SampleType::Float64, samples.data(), *layout,
                                              output.data(), *layout, request);
            ASSERT_TRUE(run.hasValue()) << run.status().message();
            double previous = 0.0;
            for (const double y : output)
            {
                EXPECT_TRUE(y >= previous && y <= 1.0 && !std::signbit(y)) << y;
                previous = y;
            }
        }
    }
}

TEST(StretchTest, MidtonesTransferCompiledRowIsWithinItsTierOfItsScalarAuthorityOnEveryQuantizedCode)
{
    // Every UInt16 capture code as the benchmark derives it, in a row whose width leaves a partial
    // final vector, and the values beyond the unit interval a caller can pass. The compiled row
    // evaluates the fraction in Float32, so a sample between the endpoints may be a few Float32 steps
    // from the authority's; the profiles at a balance of zero or one and with equal endpoints are
    // evaluated in Float64, bit for bit.
    std::vector<float> input(65536u + 3u);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i % 65536u) / 65535.0f;
    input.insert(input.end(),
                 {-10.0f, -0.0f, std::numeric_limits<float>::denorm_min(), std::nextafter(1.0f, 0.0f), 10.0f});
    const auto engine = makeEngine();
    for (const an::MidtonesTransferParameters& transfer : midtonesProfiles())
    {
        SCOPED_TRACE(::testing::Message()
                     << transfer.midtones << ' ' << transfer.shadows << ' ' << transfer.highlights);
        const auto parameters = screenTransfer(transfer.midtones, transfer.shadows, transfer.highlights);
        const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, parameters);
        const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, parameters);
        EXPECT_TRUE(midtonesRowWithinTier(input, compiled, scalar, transfer));
        if (::testing::Test::HasFailure())
            return;
    }
}

TEST(StretchTest, MidtonesTransferCompiledRowStaysInItsTierBesideFloat64EndpointsAndBelowTheNormalRange)
{
    // Endpoints a Float32 value cannot hold, where the distance of the next Float32 sample from the
    // shadow point is a few Float64 steps; shadow points at zero, where a sample and its distance are
    // below the Float32 normal range; and balances near each end of their range.
    const std::vector<an::MidtonesTransferParameters> profiles{
        {.midtones = 0.0015, .shadows = 0.1, .highlights = 1.0},
        {.midtones = 0.03, .shadows = 3.0 * 0x1p-54, .highlights = 0.9},
        {.midtones = 0.25, .shadows = 0.1234567890123456, .highlights = 0.8765432109876543},
        {.midtones = 0.0008, .shadows = 0.0931, .highlights = 0.97},
        {.midtones = 0.000001, .shadows = 0.2, .highlights = 0.200001},
        {.midtones = 0.03, .shadows = 0.0, .highlights = 1.0},
        {.midtones = 0.000001, .shadows = 0.0, .highlights = 1.0},
        {.midtones = 0x1p-60, .shadows = 0.1, .highlights = 0.9},
        {.midtones = 1.0 - 0x1p-52, .shadows = 0.0, .highlights = 0.3},
        {.midtones = 0.4, .shadows = 0x1p-140, .highlights = 0.5}};
    const auto engine = makeEngine();
    std::uint64_t seed = 0x3d7u;
    for (const an::MidtonesTransferParameters& transfer : profiles)
    {
        SCOPED_TRACE(::testing::Message() << std::setprecision(17) << transfer.midtones << ' ' << transfer.shadows
                                          << ' ' << transfer.highlights);
        ASSERT_FALSE(midtonesTransferIsEvaluatedInFloat64(transfer));
        const std::vector<float> input = midtonesProbeInputs(transfer, ++seed);
        const auto parameters = screenTransfer(transfer.midtones, transfer.shadows, transfer.highlights);
        const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, parameters);
        const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, parameters);
        EXPECT_TRUE(midtonesRowWithinTier(input, compiled, scalar, transfer));
    }
}

TEST(StretchTest, MidtonesTransferCompiledRowEvaluatesATransferTooExtremeForFloat32BitForBit)
{
    // A balance below `2^-60` - one of them below the Float32 normal range, where a Float32 balance
    // would be a few bits and the samples just above zero lie on the steep part of the curve - a
    // balance of one, whose complement is zero, and endpoint ranges below `2^-60`, including none at
    // all: the row evaluates each in Float64, as the authority.
    const std::vector<an::MidtonesTransferParameters> profiles{
        {.midtones = 0x1p-61, .shadows = 0.1, .highlights = 0.9},
        {.midtones = 1.0e-40, .shadows = 0.0, .highlights = 1.0},
        {.midtones = 0x1p-1000, .shadows = 0.0, .highlights = 1.0},
        {.midtones = 0.0, .shadows = 0.25, .highlights = 0.75},
        {.midtones = 1.0, .shadows = 0.25, .highlights = 0.75},
        {.midtones = 0.25, .shadows = 0x1p-70, .highlights = 0x1p-70 + 0x1p-80},
        {.midtones = 0.25, .shadows = 0.375, .highlights = 0.375}};
    const auto engine = makeEngine();
    std::uint64_t seed = 0x61u;
    for (const an::MidtonesTransferParameters& transfer : profiles)
    {
        SCOPED_TRACE(::testing::Message() << std::setprecision(17) << transfer.midtones << ' ' << transfer.shadows
                                          << ' ' << transfer.highlights);
        ASSERT_TRUE(midtonesTransferIsEvaluatedInFloat64(transfer));
        const std::vector<float> input = midtonesProbeInputs(transfer, ++seed);
        const auto parameters = screenTransfer(transfer.midtones, transfer.shadows, transfer.highlights);
        const auto compiled = stretchRow(engine, an::ComputeBackend::Cpu, input, parameters);
        const auto scalar = stretchRow(engine, an::ComputeBackend::Reference, input, parameters);
        EXPECT_TRUE(midtonesRowWithinTier(input, compiled, scalar, transfer));
    }
}

TEST(StretchTest, MidtonesTransferCompiledRowIsTheSameOnEveryInstructionSet)
{
    // Every operation of the Float32 row rounds once and none is contracted, so each instruction set
    // this host runs writes the same bits.
    const an::MidtonesTransferParameters transfer{.midtones = 0.0015, .shadows = 0.1, .highlights = 0.97};
    const std::vector<float> input = midtonesProbeInputs(transfer, 0x155u);
    const auto parameters = screenTransfer(transfer.midtones, transfer.shadows, transfer.highlights);
    const auto engine = makeEngine();
    const auto layout = layoutOf(static_cast<std::uint32_t>(input.size()), 1u, 1u);
    an::StretchImageRequest request;
    request.stretch = parameters;
    std::vector<std::vector<float>> outputs;
    for (const an::InstructionSet set : {an::InstructionSet::Avx2, an::InstructionSet::Avx512Skylake})
    {
        if (!an::instructionSetIsCompiledIn(set) || !an::supportsInstructionSet(an::cpuCapabilities(), set))
            continue;
        an::ExecutionOptions options;
        options.backend = an::ComputeBackend::Cpu;
        options.instructionSet = set;
        options.backendFallback = an::BackendFallback::Forbidden;
        const auto execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();
        auto& output = outputs.emplace_back(input.size(), -1.0f);
        ASSERT_TRUE(
            an::stretchImage(*execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request)
                .hasValue());
    }
    if (outputs.size() < 2u)
        GTEST_SKIP() << "this host runs " << outputs.size() << " of the instruction sets with a compiled row";
    EXPECT_TRUE(outputs[0] == outputs[1]);
}

TEST(StretchTest, MidtonesTransferLargeImagesMatchTheScalarAuthorityWithUnalignedRowsAndInPlace)
{
    // An image whose destination is too large to stay cached is written with streaming stores, which
    // need an aligned address: rows one sample wider than a multiple of every vector start at every
    // alignment. Neither that path nor an exactly aliased call may change a sample: the streaming
    // rows write what a row small enough for ordinary stores writes, within the tier of the authority.
    constexpr std::uint32_t width = 4099u;
    constexpr std::uint32_t height = 2100u;
    const auto extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    const auto layout = an::ImageLayout::create({.extent = *extent,
                                                 .elementSizeBytes = sizeof(float),
                                                 .samplesPerPixel = 1u,
                                                 .planeCount = 1u,
                                                 .rowAlignmentBytes = 1u},
                                                an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    ASSERT_GT(layout->totalBytes(), 32u << 20u);
    std::vector<float> input(layout->totalBytes() / sizeof(float));
    std::mt19937_64 random(0x5d1e7u);
    for (float& value : input)
        value = static_cast<float>(static_cast<double>(random() >> 11u) * 0x1p-53);
    const auto engine = makeEngine();
    for (const auto& transfer : {an::MidtonesTransferParameters{.midtones = 0.25, .shadows = 0.0, .highlights = 1.0},
                                 an::MidtonesTransferParameters{.midtones = 0.03, .shadows = 0.02, .highlights = 0.9}})
    {
        an::StretchImageRequest request;
        request.stretch = screenTransfer(transfer.midtones, transfer.shadows, transfer.highlights);
        std::vector<std::vector<float>> outputs;
        for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
        {
            an::ExecutionOptions options;
            options.backend = backend;
            const auto execution = an::ExecutionContext::create(engine, options);
            auto& output = outputs.emplace_back(input.size(), -1.0f);
            ASSERT_TRUE(an::stretchImage(*execution, an::SampleType::Float32, input.data(), *layout, output.data(),
                                         *layout, request)
                            .hasValue());
            auto inPlace = input;
            ASSERT_TRUE(an::stretchImage(*execution, an::SampleType::Float32, inPlace.data(), *layout, inPlace.data(),
                                         *layout, request)
                            .hasValue());
            EXPECT_TRUE(inPlace == output);
        }
        EXPECT_TRUE(midtonesRowWithinTier(input, outputs[1], outputs[0], transfer));
        for (std::uint32_t y = 0; y < height; y += 7u)
        {
            const std::vector<float> row(input.begin() + std::ptrdiff_t{y} * width,
                                         input.begin() + std::ptrdiff_t{y + 1u} * width);
            const auto ordinary = stretchRow(engine, an::ComputeBackend::Cpu, row, request.stretch);
            ASSERT_TRUE(std::equal(ordinary.begin(), ordinary.end(), outputs[1].begin() + std::ptrdiff_t{y} * width))
                << "row " << y;
        }
    }
}

namespace
{
template <typename T> void checkMidtonesLayouts(an::SampleType type)
{
    // Each RGB channel keeps its own transfer and a fourth reuses the third's; a disabled channel is
    // copied as its clamped value.
    const auto engine = makeEngine();
    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::ScreenTransfer;
    request.stretch.screenTransfer = {
        an::MidtonesTransferParameters{.midtones = 0.25, .shadows = 0.0, .highlights = 1.0},
        an::MidtonesTransferParameters{.midtones = 0.6, .shadows = 0.1, .highlights = 0.8},
        an::MidtonesTransferParameters{.midtones = 0.05, .shadows = 0.02, .highlights = 1.0}};
    for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
    {
        an::ExecutionOptions options;
        options.backend = backend;
        auto execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue());
        for (const std::array<bool, 4u> enabled :
             {std::array<bool, 4u>{true, true, true, false}, std::array<bool, 4u>{true, false, true, true}})
            for (unsigned width : {1u, 3u, 17u, 257u})
                for (unsigned channels : {1u, 3u, 4u})
                    for (bool interleaved : {false, true})
                    {
                        SCOPED_TRACE(::testing::Message() << "width " << width << " channels " << channels
                                                          << (interleaved ? " interleaved" : " planar"));
                        request.channelEnabled = enabled;
                        const auto extent = an::ImageExtent::create(width, 3u, channels, an::defaultEngineLimits());
                        const auto layout = an::ImageLayout::create({.extent = *extent,
                                                                     .elementSizeBytes = sizeof(T),
                                                                     .samplesPerPixel = interleaved ? channels : 1u,
                                                                     .planeCount = interleaved ? 1u : channels,
                                                                     .rowAlignmentBytes = 64u},
                                                                    an::defaultEngineLimits());
                        ASSERT_TRUE(layout.has_value());
                        std::vector<T> input(layout->totalBytes() / sizeof(T), T{-5});
                        auto expected = input;
                        for (unsigned channel = 0; channel < channels; ++channel)
                            for (unsigned y = 0; y < 3; ++y)
                                for (unsigned x = 0; x < width; ++x)
                                {
                                    const auto index =
                                        *layout->sampleByteOffset(channel / layout->samplesPerPixel(), y, x,
                                                                  channel % layout->samplesPerPixel()) /
                                        sizeof(T);
                                    input[index] = static_cast<T>(
                                        -0.25 + 1.5 * (x + channel + y) / static_cast<double>(width + channels + 1));
                                    const T clamped = std::clamp(input[index], T{0}, T{1});
                                    const bool stretched = channels == 1u || request.channelEnabled[channel];
                                    expected[index] =
                                        stretched
                                            ? static_cast<T>(midtonesReference(
                                                  clamped, request.stretch.screenTransfer[(std::min)(channel, 2u)]))
                                            : clamped;
                                }
                        const auto before = engine.memoryResource()->counters();
                        auto output = input;
                        const auto run =
                            an::stretchImage(*execution, type, input.data(), *layout, output.data(), *layout, request);
                        ASSERT_TRUE(run.hasValue()) << run.status().message();
                        EXPECT_EQ(run->run.outputsWritten, width * 3u * channels);
                        EXPECT_EQ(run->run.selection.valid, width * 3u);
                        ASSERT_TRUE(
                            an::stretchImage(*execution, type, input.data(), *layout, input.data(), *layout, request)
                                .hasValue());
                        EXPECT_EQ(input, output);
                        for (std::size_t i = 0; i < output.size(); ++i)
                            EXPECT_NEAR(output[i], expected[i], sizeof(T) == 4 ? 1.0e-6 : 2.0e-13);
                        const auto after = engine.memoryResource()->counters();
                        EXPECT_EQ(after.liveBytes, before.liveBytes);
                        EXPECT_EQ(after.totalAllocations, before.totalAllocations);
                        if (::testing::Test::HasFailure())
                            return;
                    }
    }
}
} // namespace

TEST(StretchTest, MidtonesTransferPaddedPlanarInterleavedAndAliasedLayoutsKeepEachChannelsTransfer)
{
    checkMidtonesLayouts<float>(an::SampleType::Float32);
    checkMidtonesLayouts<double>(an::SampleType::Float64);
}

TEST(StretchTest, MidtonesTransferRefusesANonFiniteSampleInALaterRowBeforeWritingThatRow)
{
    // The compiled row checks the next row while it writes, so a sample that is not finite anywhere in
    // a later row - in a partial first vector, a full one or a partial last one, on the ordinary and
    // the streaming paths - must still refuse that row before any of it is written. On one worker
    // every earlier row has been written by then. A streaming row's partial first vector depends on
    // where its destination row starts; a row is three samples longer than a multiple of every vector,
    // so of three consecutive rows at least two start unaligned, wherever the allocation begins.
    const auto engine = makeEngine();
    an::StretchImageRequest request;
    request.stretch = screenTransfer(0.25, 0.0, 1.0);
    for (const std::uint32_t height : {48u, 2100u})
    {
        constexpr std::uint32_t width = 4099u;
        const auto extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
        const auto layout = an::ImageLayout::create({.extent = *extent,
                                                     .elementSizeBytes = sizeof(float),
                                                     .samplesPerPixel = 1u,
                                                     .planeCount = 1u,
                                                     .rowAlignmentBytes = 1u},
                                                    an::defaultEngineLimits());
        ASSERT_TRUE(layout.has_value());
        std::vector<float> input(layout->totalBytes() / sizeof(float), 0.3f);
        for (const std::uint32_t bad : {37u, 38u, 39u})
            for (const std::uint32_t x : {0u, 1u, 15u, 16u, 2048u, width - 1u})
                for (const float value :
                     {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()})
                    for (const std::uint32_t workers : {1u, an::kUseContextConcurrency})
                    {
                        SCOPED_TRACE(::testing::Message() << "height " << height << " row " << bad << " x " << x
                                                          << " value " << value << " workers " << workers);
                        input[std::size_t{bad} * width + x] = value;
                        an::ExecutionOptions options;
                        options.backend = an::ComputeBackend::Cpu;
                        options.maxConcurrency = workers;
                        const auto execution = an::ExecutionContext::create(engine, options);
                        ASSERT_TRUE(execution.hasValue());
                        std::vector<float> output(input.size(), -5.0f);
                        const auto run = an::stretchImage(*execution, an::SampleType::Float32, input.data(), *layout,
                                                          output.data(), *layout, request);
                        input[std::size_t{bad} * width + x] = 0.3f;
                        ASSERT_FALSE(run.hasValue());
                        EXPECT_EQ(run.status().code(), an::ErrorCode::InvalidData);
                        EXPECT_TRUE(std::all_of(output.begin() + std::ptrdiff_t{bad} * width,
                                                output.begin() + std::ptrdiff_t{bad + 1u} * width,
                                                [](float sample) { return sample == -5.0f; }));
                        if (workers == 1u)
                            EXPECT_TRUE(std::none_of(output.begin(), output.begin() + std::ptrdiff_t{bad} * width,
                                                     [](float sample) { return sample == -5.0f; }));
                    }
    }
}

TEST(StretchTest, MidtonesTransferRefusesANonFiniteSampleInAnyPlaneBeforeWritingItsRow)
{
    // A planar image is checked across every plane of a row, enabled or not, before that row is
    // written; a rule that admits only finite samples leaves such a pixel untouched instead.
    const auto engine = makeEngine();
    const auto layout = layoutOf(17u, 2u, 3u);
    std::vector<float> input(layout.totalBytes() / sizeof(float), 0.3f);
    sampleAt(input, layout, 1u, 5u, 1u) = std::numeric_limits<float>::quiet_NaN();
    an::StretchImageRequest request;
    request.stretch = screenTransfer(0.25, 0.0, 1.0);
    request.channelEnabled[1] = false;
    for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
    {
        an::ExecutionOptions options;
        options.backend = backend;
        const auto execution = an::ExecutionContext::create(engine, options);
        std::vector<float> output(input.size(), -5.0f);
        auto run =
            an::stretchImage(*execution, an::SampleType::Float32, input.data(), layout, output.data(), layout, request);
        ASSERT_FALSE(run.hasValue());
        EXPECT_EQ(run.status().code(), an::ErrorCode::InvalidData);
        for (std::uint32_t channel = 0; channel < 3u; ++channel)
            for (std::uint32_t x = 0; x < 17u; ++x)
                EXPECT_EQ(sampleAt(output, layout, channel, x, 1u), -5.0f) << channel << ' ' << x;
        an::KernelValidityView validity;
        validity.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        run = an::stretchImage(*execution, an::SampleType::Float32, input.data(), layout, output.data(), layout,
                               request, validity);
        ASSERT_TRUE(run.hasValue()) << run.status().message();
        EXPECT_EQ(run->run.selection.valid, 33u);
        EXPECT_EQ(sampleAt(output, layout, 0u, 5u, 1u), -5.0f);
        EXPECT_NEAR(sampleAt(output, layout, 0u, 6u, 1u), 0.5625f, 1.0e-7);
    }
}
