// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the background-model domain.
//
// The suite is built around one experiment repeated in several forms: take a picture whose
// background is known because the test put it there, and require that what comes back is the
// background rather than the picture. A test that only checked that the operation ran would pass on
// a model that returned zero everywhere, so every case here compares against a surface the test
// itself computed, or measures how much flatter the frame became.
//
// The four stages are tested separately and then together, which is the point of splitting them: a
// fit that is wrong shows up in `fitBackgroundModel` rather than three calls later in a corrected
// frame, and a correction that is wrong cannot hide behind a fit that was.

#include <AfterNightEngine/Algorithms/Background/AutoBackground.h>
#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::randomPlane;

static_assert(!std::is_constructible_v<an::AutoBackgroundPlan, const an::AutoBackgroundRequest&, std::uint32_t,
                                       std::uint32_t, std::uint32_t>);

/// A polynomial surface the test can evaluate for itself.
///
/// The same normalisation the engine uses, written out here so that a fit is compared against an
/// independent evaluation rather than against the code that produced it.
///
/// @param x the column.
/// @param y the row.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param constant the constant term.
/// @param slopeX the coefficient of the normalised column.
/// @param slopeY the coefficient of the normalised row.
/// @param curvature the coefficient of the normalised column squared.
/// @return the surface's value.
[[nodiscard]] double surfaceAt(double x, double y, std::uint32_t width, std::uint32_t height, double constant,
                               double slopeX, double slopeY, double curvature)
{
    const double nx = width > 1u ? x / static_cast<double>(width - 1u) * 2.0 - 1.0 : 0.0;
    const double ny = height > 1u ? y / static_cast<double>(height - 1u) * 2.0 - 1.0 : 0.0;
    return constant + slopeX * nx + slopeY * ny + curvature * nx * nx;
}

/// A frame holding one polynomial gradient and nothing else.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param constant the constant term.
/// @param slopeX the coefficient of the normalised column.
/// @param slopeY the coefficient of the normalised row.
/// @param curvature the coefficient of the normalised column squared.
/// @return the samples, row-major.
[[nodiscard]] std::vector<float> gradientFrame(std::uint32_t width, std::uint32_t height, double constant,
                                               double slopeX, double slopeY, double curvature)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            samples[static_cast<std::size_t>(y) * width + x] =
                static_cast<float>(surfaceAt(x, y, width, height, constant, slopeX, slopeY, curvature));
        }
    }
    return samples;
}

/// Adds a bright disc to a frame, which is what a background model has to refuse to follow.
///
/// @param samples the frame.
/// @param width how many pixels one row holds.
/// @param centreX the disc's centre column.
/// @param centreY its centre row.
/// @param radius its radius, in pixels.
/// @param amplitude how far above the frame its centre sits.
void addDisc(std::vector<float>& samples, std::uint32_t width, std::uint32_t centreX, std::uint32_t centreY,
             double radius, double amplitude)
{
    const auto height = static_cast<std::uint32_t>(samples.size() / width);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = static_cast<double>(x) - static_cast<double>(centreX);
            const double dy = static_cast<double>(y) - static_cast<double>(centreY);
            if (dx * dx + dy * dy > radius * radius)
            {
                continue;
            }
            samples[static_cast<std::size_t>(y) * width + x] += static_cast<float>(amplitude);
        }
    }
}

/// The standard deviation of a frame, which says how flat it is.
///
/// @param samples the frame.
/// @return the deviation.
[[nodiscard]] double deviationOf(const std::vector<float>& samples)
{
    double sum = 0.0;
    for (const float sample : samples)
    {
        sum += static_cast<double>(sample);
    }
    const double mean = sum / static_cast<double>(samples.size());
    double squares = 0.0;
    for (const float sample : samples)
    {
        const double difference = static_cast<double>(sample) - mean;
        squares += difference * difference;
    }
    return std::sqrt(squares / static_cast<double>(samples.size()));
}

/// The standard deviation of the samples a frame calls background, judged by another frame.
///
/// The stars a background estimate had to ignore are still in the corrected frame and carry more
/// deviation than the gradient did, so a flatness measurement over every sample would say a correct
/// removal made the frame worse.
///
/// @param samples the frame to measure.
/// @param reference the frame that decides which samples are background.
/// @param ceiling the value above which a reference sample is an object rather than sky.
/// @return the deviation over the background samples.
[[nodiscard]] double backgroundDeviation(const std::vector<float>& samples, const std::vector<float>& reference,
                                         float ceiling)
{
    double sum = 0.0;
    std::size_t counted = 0u;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        if (reference[index] >= ceiling)
        {
            continue;
        }
        sum += static_cast<double>(samples[index]);
        ++counted;
    }
    if (counted == 0u)
    {
        return 0.0;
    }
    const double mean = sum / static_cast<double>(counted);
    double squares = 0.0;
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        if (reference[index] >= ceiling)
        {
            continue;
        }
        const double difference = static_cast<double>(samples[index]) - mean;
        squares += difference * difference;
    }
    return std::sqrt(squares / static_cast<double>(counted));
}

/// Sites measured over one frame, so a case about fitting does not spell the sampling twice.
///
/// @param execution the operation policy and engine-context lease.
/// @param samples the frame.
/// @param layout where it sits.
/// @param request what to ask the sampling for.
/// @param sites where the sites go.
/// @return the sampling result.
[[nodiscard]] an::BackgroundSampleResult measureSites(const an::ExecutionContext& execution,
                                                      const std::vector<float>& samples, const an::ImageLayout& layout,
                                                      const an::BackgroundSampleRequest& request,
                                                      std::vector<an::BackgroundSample>& sites)
{
    const an::Result<an::BackgroundSampleGrid> grid =
        an::planBackgroundSampleGrid(request, layout.extent().width(), layout.extent().height());
    EXPECT_TRUE(grid.hasValue()) << grid.status().message();
    sites.assign(grid.hasValue() ? grid->siteCount() : 0u, an::BackgroundSample{});
    const an::Result<an::BackgroundSampleResult> measured =
        an::sampleImageBackground(execution, an::SampleType::Float32, samples.data(), layout, request, sites);
    EXPECT_TRUE(measured.hasValue()) << measured.status().message();
    if (measured.hasValue())
    {
        sites.resize(measured->acceptedCount);
        return *measured;
    }
    sites.clear();
    return an::BackgroundSampleResult{};
}

TEST(BackgroundModelTest, AGridIsCentredAndEveryBoxLiesInsideThePicture)
{
    constexpr std::uint32_t kWidth = 401u;
    constexpr std::uint32_t kHeight = 257u;
    an::BackgroundSampleRequest request{};
    request.extent = 25u;
    request.samplesPerLine = 8u;

    const an::Result<an::BackgroundSampleGrid> grid = an::planBackgroundSampleGrid(request, kWidth, kHeight);
    ASSERT_TRUE(grid.hasValue()) << grid.status().message();
    EXPECT_EQ(grid->columns, 8u);
    EXPECT_GE(grid->rows, 1u);
    EXPECT_EQ(grid->extent, 25u);
    EXPECT_EQ(grid->siteCount(), grid->columns * grid->rows);
    EXPECT_GT(grid->columnSpacing, static_cast<double>(request.extent));

    const std::uint32_t radius = request.extent / 2u;
    const auto lastX = static_cast<std::uint32_t>(
        grid->originX + std::llround(static_cast<double>(grid->columns - 1u) * grid->columnSpacing));
    const auto lastY = static_cast<std::uint32_t>(
        grid->originY + std::llround(static_cast<double>(grid->rows - 1u) * grid->rowSpacing));
    EXPECT_GE(grid->originX, radius);
    EXPECT_GE(grid->originY, radius);
    EXPECT_LT(lastX + radius, kWidth);
    EXPECT_LT(lastY + radius, kHeight);

    // Centred: what is left over is split between the two margins to within a pixel of rounding.
    const std::uint32_t leftMargin = grid->originX - radius;
    const std::uint32_t rightMargin = kWidth - 1u - (lastX + radius);
    EXPECT_LE(leftMargin > rightMargin ? leftMargin - rightMargin : rightMargin - leftMargin, 1u);
}

TEST(BackgroundModelTest, AGridRefusesAPictureItCannotCoverAndADensityItCannotFit)
{
    an::BackgroundSampleRequest request{};
    request.extent = 25u;
    request.samplesPerLine = 8u;
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 20u, 400u).status().code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 100u, 400u).status().code(), an::ErrorCode::OutOfRange);

    request.extent = 24u;
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 400u, 400u).status().code(), an::ErrorCode::InvalidArgument);
    request.extent = 3u;
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 400u, 400u).status().code(), an::ErrorCode::OutOfRange);
    request.extent = 25u;
    request.samplesPerLine = 1u;
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 400u, 400u).status().code(), an::ErrorCode::InvalidArgument);
    request.samplesPerLine = 8u;
    request.tolerance = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 400u, 400u).status().code(), an::ErrorCode::InvalidArgument);
    request.tolerance = 1.0;
    request.luminance = static_cast<an::BackgroundLuminance>(7u);
    EXPECT_EQ(an::planBackgroundSampleGrid(request, 400u, 400u).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(BackgroundModelTest, ASiteCarriesTheMedianOfItsBoxAndTheLuminanceItWasAcceptedOn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 96u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        const float value = 0.1f * static_cast<float>(channel + 1u);
        std::fill(samples.begin() + static_cast<std::ptrdiff_t>(channel) * kWidth * kHeight,
                  samples.begin() + static_cast<std::ptrdiff_t>(channel + 1u) * kWidth * kHeight, value);
    }

    an::BackgroundSampleRequest request{};
    request.extent = 11u;
    const an::Result<an::BackgroundSample> site =
        an::measureImageBackgroundSample(execution, an::SampleType::Float32, samples.data(), layout, request, 40u, 30u);
    ASSERT_TRUE(site.hasValue()) << site.status().message();
    EXPECT_EQ(site->channelCount, kChannels);
    EXPECT_EQ(site->extent, 11u);
    EXPECT_EQ(site->sampleCount, 121u);
    EXPECT_DOUBLE_EQ(site->x, 40.0);
    EXPECT_DOUBLE_EQ(site->y, 30.0);
    EXPECT_NEAR(site->median[0], 0.1, 1.0e-6);
    EXPECT_NEAR(site->median[1], 0.2, 1.0e-6);
    EXPECT_NEAR(site->median[2], 0.30000001192092896, 1.0e-6);
    EXPECT_NEAR(site->luminance, 0.2, 1.0e-6);

    request.luminance = an::BackgroundLuminance::Rec709;
    const an::Result<an::BackgroundSample> weighted =
        an::measureImageBackgroundSample(execution, an::SampleType::Float32, samples.data(), layout, request, 40u, 30u);
    ASSERT_TRUE(weighted.hasValue()) << weighted.status().message();
    EXPECT_NEAR(weighted->luminance,
                an::kBackgroundLuminanceRed * 0.1 + an::kBackgroundLuminanceGreen * 0.2 +
                    an::kBackgroundLuminanceBlue * 0.3,
                1.0e-6);

    request.luminance = an::BackgroundLuminance::FirstChannel;
    const an::Result<an::BackgroundSample> first =
        an::measureImageBackgroundSample(execution, an::SampleType::Float32, samples.data(), layout, request, 40u, 30u);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_NEAR(first->luminance, 0.1, 1.0e-6);

    EXPECT_EQ(
        an::measureImageBackgroundSample(execution, an::SampleType::Float32, samples.data(), layout, request, 2u, 30u)
            .status()
            .code(),
        an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::measureImageBackgroundSample(execution, an::SampleType::Float32, samples.data(), layout, request,
                                               std::numeric_limits<std::uint32_t>::max(), 30u)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::measureImageBackgroundSample(execution, an::SampleType::Float32, samples.data(), layout, request, 40u,
                                               std::numeric_limits<std::uint32_t>::max())
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
}

TEST(BackgroundModelTest, ASamplingKeepsTheBackgroundAndDropsTheSitesAnObjectSitsOn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 192u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.2f);

    an::BackgroundSampleRequest request{};
    request.extent = 15u;
    request.samplesPerLine = 6u;
    request.tolerance = 1.0;

    std::vector<an::BackgroundSample> clean;
    const an::BackgroundSampleResult flat = measureSites(execution, samples, layout, request, clean);
    EXPECT_EQ(flat.rejectedCount, 0u);
    EXPECT_EQ(flat.acceptedCount, flat.grid.siteCount());
    EXPECT_NEAR(flat.frameLevel, 0.2, 1.0e-6);
    EXPECT_NEAR(flat.frameDeviation, 0.0, 1.0e-9);

    const an::Result<an::BackgroundSampleGrid> grid = an::planBackgroundSampleGrid(request, kWidth, kHeight);
    ASSERT_TRUE(grid.hasValue()) << grid.status().message();
    const auto centreX = static_cast<std::uint32_t>(grid->originX + std::llround(2.0 * grid->columnSpacing));
    const auto centreY = static_cast<std::uint32_t>(grid->originY + std::llround(1.0 * grid->rowSpacing));
    addDisc(samples, kWidth, centreX, centreY, 10.0, 0.6);

    std::vector<an::BackgroundSample> withObject;
    const an::BackgroundSampleResult contaminated = measureSites(execution, samples, layout, request, withObject);
    EXPECT_EQ(contaminated.rejectedCount, 1u);
    EXPECT_EQ(contaminated.acceptedCount, contaminated.grid.siteCount() - 1u);
    // The threshold is the level plus a tolerance times the sites' own deviation, and a frame whose
    // background sites all agree has no deviation - so one contaminated site is rejected by being
    // above the level rather than by a scale it could not shift.
    EXPECT_GE(contaminated.threshold, contaminated.frameLevel);
    for (const an::BackgroundSample& site : withObject)
    {
        EXPECT_LE(site.luminance, contaminated.threshold);
    }

    const an::Result<std::uint32_t> nearest = an::nearestBackgroundSample(
        withObject, static_cast<double>(withObject[3].x) + 1.0, static_cast<double>(withObject[3].y) - 1.0);
    ASSERT_TRUE(nearest.hasValue()) << nearest.status().message();
    EXPECT_EQ(*nearest, 3u);
    EXPECT_EQ(an::nearestBackgroundSample({}, 0.0, 0.0).status().code(), an::ErrorCode::EmptySelection);
}

TEST(BackgroundModelTest, ASamplingRefusesAnImageWithMoreChannelsThanASampleCarries)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    constexpr std::uint32_t kChannels = an::kMaxBackgroundChannels + 1u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0.1f);
    an::BackgroundSampleRequest request{};
    request.extent = 9u;
    request.samplesPerLine = 3u;
    std::vector<an::BackgroundSample> sites(64u, an::BackgroundSample{});
    EXPECT_EQ(an::sampleImageBackground(execution, an::SampleType::Float32, samples.data(), layout, request, sites)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(BackgroundModelTest, ACorrectionRefusesAnImageWithMoreChannelsThanItsResultCarriesLevelsFor)
{
    // The result reports the level it put back per channel in an array of kMaxBackgroundChannels. A
    // correction given one valid model per channel of a wider image has to refuse it rather than
    // write a level past the end of that array.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    constexpr std::uint32_t kChannels = an::kMaxBackgroundChannels + 1u;
    const an::ImageLayout planeLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> plane = gradientFrame(kWidth, kHeight, 0.25, 0.08, -0.05, 0.0);

    an::BackgroundSampleRequest sampling{};
    sampling.extent = 9u;
    sampling.samplesPerLine = 4u;
    sampling.tolerance = 6.0;
    std::vector<an::BackgroundSample> sites;
    ASSERT_GE(measureSites(execution, plane, planeLayout, sampling, sites).acceptedCount, 3u);
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());
    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    const std::vector<an::BackgroundModelView> models(kChannels, model);

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    std::vector<float> samples;
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        samples.insert(samples.end(), plane.begin(), plane.end());
    }
    std::vector<float> corrected(samples.size(), 0.0f);
    an::BackgroundApplyRequest apply{};
    apply.correction = an::BackgroundCorrection::Subtract;
    apply.level = an::BackgroundLevel::ModelMedian;
    EXPECT_EQ(an::applyImageBackground(execution, an::SampleType::Float32, samples.data(), layout, corrected.data(),
                                       layout, apply, models)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(BackgroundModelTest, ATermCountIsTheTriangularNumberOfItsDegreeAndARefusalIsNamed)
{
    an::BackgroundFitRequest request{};
    for (std::uint32_t degree = 1u; degree <= an::kMaxBackgroundPolynomialDegree; ++degree)
    {
        request.degree = degree;
        const an::Result<std::uint32_t> terms = an::backgroundModelTermCount(request, 100u);
        ASSERT_TRUE(terms.hasValue()) << terms.status().message();
        EXPECT_EQ(*terms, (degree + 1u) * (degree + 2u) / 2u);
    }
    request.degree = 0u;
    EXPECT_EQ(an::backgroundModelTermCount(request, 100u).status().code(), an::ErrorCode::InvalidArgument);
    request.degree = an::kMaxBackgroundPolynomialDegree + 1u;
    EXPECT_EQ(an::backgroundModelTermCount(request, 100u).status().code(), an::ErrorCode::OutOfRange);

    request.degree = 2u;
    request.kind = an::BackgroundModelKind::RadialBasis;
    const an::Result<std::uint32_t> spline = an::backgroundModelTermCount(request, 40u);
    ASSERT_TRUE(spline.hasValue()) << spline.status().message();
    EXPECT_EQ(*spline, 43u);
    EXPECT_EQ(an::backgroundModelTermCount(request, an::kMaxRadialBasisSites + 1u).status().code(),
              an::ErrorCode::OutOfRange);
    request.smoothing = 1.5;
    EXPECT_EQ(an::backgroundModelTermCount(request, 40u).status().code(), an::ErrorCode::InvalidArgument);

    EXPECT_STREQ(an::backgroundModelKindName(an::BackgroundModelKind::Polynomial), "polynomial");
    EXPECT_STREQ(an::backgroundModelKindName(an::BackgroundModelKind::RadialBasis), "radial-basis");
    EXPECT_STREQ(an::backgroundModelKindName(static_cast<an::BackgroundModelKind>(9u)), "unknown");
    EXPECT_STREQ(an::backgroundLevelName(an::BackgroundLevel::ModelMedian), "model-median");
    EXPECT_STREQ(an::backgroundLuminanceName(an::BackgroundLuminance::ChannelMean), "channel-mean");
}

TEST(BackgroundModelTest, APolynomialFitReproducesThePolynomialItWasGiven)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 320u;
    constexpr std::uint32_t kHeight = 240u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    // A plane rather than a curved surface, so that the median over each sample box is the surface
    // at the box centre exactly and the fit can be held to rounding. A curved surface offsets every
    // box median by its own curvature over the box, which is a property of sampling rather than of
    // fitting and belongs in its own case.
    const std::vector<float> samples = gradientFrame(kWidth, kHeight, 0.2, 0.05, -0.03, 0.0);

    an::BackgroundSampleRequest sampling{};
    sampling.extent = 15u;
    sampling.samplesPerLine = 8u;
    sampling.tolerance = 6.0;
    std::vector<an::BackgroundSample> sites;
    const an::BackgroundSampleResult measured = measureSites(execution, samples, layout, sampling, sites);
    ASSERT_GE(measured.acceptedCount, 20u);

    an::BackgroundFitRequest fit{};
    fit.degree = 2u;
    const an::Result<std::uint32_t> terms = an::backgroundModelTermCount(fit, static_cast<std::uint32_t>(sites.size()));
    ASSERT_TRUE(terms.hasValue()) << terms.status().message();
    std::vector<double> coefficients(*terms, 0.0);
    const an::Result<an::BackgroundFitResult> fitted =
        an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->termCount, 6u);
    EXPECT_EQ(fitted->droppedTerms, 0u);
    EXPECT_LT(fitted->residualRootMeanSquare, 1.0e-6);
    EXPECT_LT(fitted->largestResidual, 1.0e-5);
    EXPECT_GT(fitted->conditionEstimate, 1.0);
    // A degree-two basis fitted to a plane finds no curvature: the three second-order terms come
    // back at zero rather than at whatever the rounding of the accumulation suggested.
    EXPECT_NEAR(coefficients[3], 0.0, 1.0e-6);
    EXPECT_NEAR(coefficients[4], 0.0, 1.0e-6);
    EXPECT_NEAR(coefficients[5], 0.0, 1.0e-6);

    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;

    std::vector<float> surface(samples.size(), 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(surface, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(plane.has_value());
    const an::Result<an::BackgroundEvaluationResult> drawn = an::evaluateBackgroundModel(execution, model, *plane);
    ASSERT_TRUE(drawn.hasValue()) << drawn.status().message();
    EXPECT_EQ(drawn->evaluationStride, 1u);
    EXPECT_EQ(drawn->samplesWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_EQ(drawn->precision, an::ComputePrecision::Float64);

    for (std::uint32_t y = 0u; y < kHeight; y += 17u)
    {
        for (std::uint32_t x = 0u; x < kWidth; x += 19u)
        {
            const double expected = surfaceAt(x, y, kWidth, kHeight, 0.2, 0.05, -0.03, 0.0);
            ASSERT_NEAR(surface[static_cast<std::size_t>(y) * kWidth + x], expected, 1.0e-5)
                << "at (" << x << ", " << y << ")";
        }
    }
    EXPECT_NEAR(drawn->lowest, 0.2 - 0.05 - 0.03, 5.0e-3);
    EXPECT_NEAR(drawn->highest, 0.2 + 0.05 + 0.03, 5.0e-3);
}

TEST(BackgroundModelTest, AFitDropsTheTermsCollinearSitesDoNotDetermine)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 200u;
    constexpr std::uint32_t kHeight = 200u;

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t index = 0u; index < 12u; ++index)
    {
        an::BackgroundSample site{};
        site.x = 10.0 + 15.0 * static_cast<double>(index);
        site.y = 100.0;
        site.channelCount = 1u;
        site.extent = 5u;
        site.sampleCount = 25u;
        site.median[0] = 0.1 + 0.0002 * site.x;
        site.luminance = site.median[0];
        sites.push_back(site);
    }

    an::BackgroundFitRequest fit{};
    fit.degree = 2u;
    std::vector<double> coefficients(6u, 0.0);
    const an::Result<an::BackgroundFitResult> fitted =
        an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    // A row of sites determines the constant, the horizontal slope and the horizontal curvature and
    // nothing that varies down the frame; the three terms it does not determine are dropped rather
    // than solved for out of rounding noise.
    EXPECT_EQ(fitted->droppedTerms, 3u);
    EXPECT_LT(fitted->residualRootMeanSquare, 1.0e-9);
    for (const an::BackgroundSample& site : sites)
    {
        EXPECT_NEAR(coefficients[0] + coefficients[1] * (site.x / (kWidth - 1.0) * 2.0 - 1.0), site.median[0], 1.0e-9);
    }
}

TEST(BackgroundModelTest, AFitRefusesWhatItCannotSolve)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::BackgroundSample site{};
    site.x = 10.0;
    site.y = 10.0;
    site.channelCount = 1u;
    site.median[0] = 0.2;
    const std::array<an::BackgroundSample, 1u> one{site};
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);

    EXPECT_EQ(an::fitBackgroundModel(execution, fit, {}, 0u, 100u, 100u, coefficients).status().code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::fitBackgroundModel(execution, fit, one, 0u, 0u, 100u, coefficients).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::fitBackgroundModel(execution, fit, one, 1u, 100u, 100u, coefficients).status().code(),
              an::ErrorCode::OutOfRange);
    std::vector<double> wrongLength(4u, 0.0);
    EXPECT_EQ(an::fitBackgroundModel(execution, fit, one, 0u, 100u, 100u, wrongLength).status().code(),
              an::ErrorCode::InvalidArgument);

    an::BackgroundSample broken = site;
    broken.median[0] = std::numeric_limits<double>::infinity();
    const std::array<an::BackgroundSample, 1u> bad{broken};
    EXPECT_EQ(an::fitBackgroundModel(execution, fit, bad, 0u, 100u, 100u, coefficients).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(BackgroundModelTest, AFailedFitLeavesTheCallersCoefficientStorageUntouched)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr double kSentinel = -917.25;
    const std::array<an::BackgroundSample, 4u> sites{{
        {.x = 10.0, .y = 10.0, .median = {std::numeric_limits<double>::max()}, .channelCount = 1u},
        {.x = 90.0, .y = 10.0, .median = {std::numeric_limits<double>::max()}, .channelCount = 1u},
        {.x = 10.0, .y = 90.0, .median = {std::numeric_limits<double>::max()}, .channelCount = 1u},
        {.x = 90.0, .y = 90.0, .median = {std::numeric_limits<double>::max()}, .channelCount = 1u},
    }};

    an::BackgroundFitRequest polynomial{};
    polynomial.degree = 1u;
    std::vector<double> polynomialCoefficients(3u, kSentinel);
    EXPECT_EQ(
        an::fitBackgroundModel(execution, polynomial, sites, 0u, 100u, 100u, polynomialCoefficients).status().code(),
        an::ErrorCode::InvalidData);
    EXPECT_EQ(polynomialCoefficients, std::vector<double>(3u, kSentinel));

    an::BackgroundFitRequest radial{};
    radial.kind = an::BackgroundModelKind::RadialBasis;
    radial.smoothing = 0.5;
    std::vector<double> radialCoefficients(sites.size() + 3u, kSentinel);
    EXPECT_EQ(an::fitBackgroundModel(execution, radial, sites, 0u, 100u, 100u, radialCoefficients).status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(radialCoefficients, std::vector<double>(sites.size() + 3u, kSentinel));
}

TEST(BackgroundModelTest, ARadialFitHonoursADeadlineDuringItsDenseWork)
{
    const an::EngineContext engine = makeEngine();
    std::vector<an::BackgroundSample> sites;
    sites.reserve(512u);
    for (std::uint32_t index = 0u; index < 512u; ++index)
    {
        an::BackgroundSample site{};
        site.x = 4.0 + 7.0 * static_cast<double>(index % 32u);
        site.y = 3.0 + 9.0 * static_cast<double>(index / 32u);
        site.channelCount = 1u;
        site.median[0] = 0.2 + 0.0001 * site.x - 0.0002 * site.y;
        sites.push_back(site);
    }

    an::ExecutionOptions options{};
    options.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1};
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    an::BackgroundFitRequest fit{};
    fit.kind = an::BackgroundModelKind::RadialBasis;
    fit.smoothing = 0.5;
    std::vector<double> coefficients(sites.size() + 3u, 0.0);
    EXPECT_EQ(an::fitBackgroundModel(*execution, fit, sites, 0u, 256u, 128u, coefficients).status().code(),
              an::ErrorCode::DeadlineExceeded);
}

TEST(BackgroundModelTest, AThinPlateSplineInterpolatesItsSitesAndRefusesASingularSystem)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 200u;
    constexpr std::uint32_t kHeight = 160u;

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t row = 0u; row < 4u; ++row)
    {
        for (std::uint32_t column = 0u; column < 4u; ++column)
        {
            an::BackgroundSample site{};
            site.x = 20.0 + 50.0 * static_cast<double>(column);
            site.y = 20.0 + 40.0 * static_cast<double>(row);
            site.channelCount = 1u;
            site.extent = 5u;
            site.sampleCount = 25u;
            site.median[0] = 0.1 + 0.05 * std::sin(0.03 * site.x) + 0.04 * std::cos(0.02 * site.y);
            site.luminance = site.median[0];
            sites.push_back(site);
        }
    }

    an::BackgroundFitRequest fit{};
    fit.kind = an::BackgroundModelKind::RadialBasis;
    fit.smoothing = 0.0;
    const an::Result<std::uint32_t> terms = an::backgroundModelTermCount(fit, static_cast<std::uint32_t>(sites.size()));
    ASSERT_TRUE(terms.hasValue()) << terms.status().message();
    EXPECT_EQ(*terms, sites.size() + 3u);
    std::vector<double> coefficients(*terms, 0.0);
    const an::Result<an::BackgroundFitResult> fitted =
        an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    // Interpolation rather than approximation: at the lowest smoothing the surface passes through
    // every site to within the ridge that smoothing still adds, which is exactly what a polynomial
    // cannot promise at all.
    EXPECT_LT(fitted->largestResidual, 1.0e-4);

    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    std::vector<float> surface(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(surface, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(plane.has_value());
    ASSERT_TRUE(an::evaluateBackgroundModel(execution, model, *plane).hasValue());
    for (const an::BackgroundSample& site : sites)
    {
        const auto x = static_cast<std::uint32_t>(site.x);
        const auto y = static_cast<std::uint32_t>(site.y);
        ASSERT_NEAR(surface[static_cast<std::size_t>(y) * kWidth + x], site.median[0], 1.0e-4);
    }

    std::vector<an::BackgroundSample> stacked(4u, sites[0]);
    std::vector<double> singularCoefficients(stacked.size() + 3u, 0.0);
    EXPECT_EQ(
        an::fitBackgroundModel(execution, fit, stacked, 0u, kWidth, kHeight, singularCoefficients).status().code(),
        an::ErrorCode::InvalidData);
}

TEST(BackgroundModelTest, AnInterpolatedEvaluationAgreesWithAnExactOneForAPlanarSurface)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 197u;
    constexpr std::uint32_t kHeight = 131u;

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t row = 0u; row < 3u; ++row)
    {
        for (std::uint32_t column = 0u; column < 3u; ++column)
        {
            an::BackgroundSample site{};
            site.x = 20.0 + 70.0 * static_cast<double>(column);
            site.y = 20.0 + 45.0 * static_cast<double>(row);
            site.channelCount = 1u;
            site.median[0] = surfaceAt(site.x, site.y, kWidth, kHeight, 0.3, 0.04, 0.02, 0.0);
            sites.push_back(site);
        }
    }
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());

    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;

    std::vector<float> exact(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    std::vector<float> interpolated(exact.size(), 0.0f);
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(kWidth) * sizeof(float);
    const std::optional<an::PlaneView<float>> exactPlane =
        an::PlaneView<float>::create(exact, kWidth, kHeight, 1u, rowBytes);
    const std::optional<an::PlaneView<float>> interpolatedPlane =
        an::PlaneView<float>::create(interpolated, kWidth, kHeight, 1u, rowBytes);
    ASSERT_TRUE(exactPlane.has_value());
    ASSERT_TRUE(interpolatedPlane.has_value());
    ASSERT_TRUE(an::evaluateBackgroundModel(execution, model, *exactPlane, 1u).hasValue());
    const an::Result<an::BackgroundEvaluationResult> coarse =
        an::evaluateBackgroundModel(execution, model, *interpolatedPlane, 16u);
    ASSERT_TRUE(coarse.hasValue()) << coarse.status().message();
    EXPECT_EQ(coarse->evaluationStride, 16u);
    EXPECT_GT(coarse->workingBytes, 0u);

    // A plane is exactly what bilinear interpolation reproduces, so a coarse lattice costs nothing
    // but rounding here; the stride is a cost decision and not a second answer.
    for (std::size_t index = 0u; index < exact.size(); ++index)
    {
        ASSERT_NEAR(interpolated[index], exact[index], 1.0e-6f) << "at " << index;
    }

    EXPECT_EQ(an::evaluateBackgroundModel(execution, model, *exactPlane, 0u).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::evaluateBackgroundModel(execution, model, *exactPlane, an::kMaxBackgroundEvaluationStride + 1u)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    model.width = kWidth + 1u;
    EXPECT_EQ(an::evaluateBackgroundModel(execution, model, *exactPlane).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(BackgroundModelTest, SubtractingAFittedGradientLeavesAFlatFrameAtTheLevelItPutBack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 192u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> samples = gradientFrame(kWidth, kHeight, 0.25, 0.08, -0.05, 0.0);

    an::BackgroundSampleRequest sampling{};
    sampling.extent = 15u;
    sampling.samplesPerLine = 8u;
    sampling.tolerance = 6.0;
    std::vector<an::BackgroundSample> sites;
    const an::BackgroundSampleResult measured = measureSites(execution, samples, layout, sampling, sites);
    ASSERT_GE(measured.acceptedCount, 16u);

    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());
    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    const std::array<an::BackgroundModelView, 1u> models{model};

    std::vector<float> corrected(samples.size(), 0.0f);
    an::BackgroundApplyRequest apply{};
    apply.correction = an::BackgroundCorrection::Subtract;
    apply.level = an::BackgroundLevel::ModelMedian;
    const an::Result<an::BackgroundApplyResult> removed = an::applyImageBackground(
        execution, an::SampleType::Float32, samples.data(), layout, corrected.data(), layout, apply, models);
    ASSERT_TRUE(removed.hasValue()) << removed.status().message();
    EXPECT_EQ(removed->channelCount, 1u);
    EXPECT_EQ(removed->imagePasses, 1u);
    EXPECT_EQ(removed->guardedSamples, 0u);
    EXPECT_EQ(removed->clampedSamples, 0u);
    EXPECT_EQ(removed->run.selection.total, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_EQ(removed->precision, an::ComputePrecision::Float32);
    EXPECT_NEAR(removed->appliedLevel[0], 0.25, 5.0e-3);
    EXPECT_EQ(removed->workingBytes, static_cast<std::uint64_t>(sites.size()) * 2u * sizeof(double) +
                                         static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(double));

    EXPECT_GT(deviationOf(samples), 0.03);
    EXPECT_LT(deviationOf(corrected), deviationOf(samples) / 500.0);
    for (const float sample : corrected)
    {
        ASSERT_NEAR(sample, removed->appliedLevel[0], 1.0e-4);
    }
}

TEST(BackgroundModelTest, DivisionRestoresTheLevelAndCountsWhatTheGuardAnswered)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 96u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    // A vignette: the frame is a constant sky times a surface that falls towards one side and
    // crosses zero before the far edge, so the guard has something to answer.
    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t row = 0u; row < 3u; ++row)
    {
        for (std::uint32_t column = 0u; column < 3u; ++column)
        {
            an::BackgroundSample site{};
            site.x = 10.0 + 50.0 * static_cast<double>(column);
            site.y = 10.0 + 38.0 * static_cast<double>(row);
            site.channelCount = 1u;
            site.median[0] = surfaceAt(site.x, site.y, kWidth, kHeight, 0.3, 0.45, 0.0, 0.0);
            sites.push_back(site);
        }
    }
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());
    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    const std::array<an::BackgroundModelView, 1u> models{model};

    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            samples[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<float>(surfaceAt(x, y, kWidth, kHeight, 0.3, 0.45, 0.0, 0.0));
        }
    }
    std::vector<float> corrected(samples.size(), 0.0f);

    an::BackgroundApplyRequest apply{};
    apply.correction = an::BackgroundCorrection::Divide;
    apply.level = an::BackgroundLevel::Stated;
    apply.statedLevel = 0.5;
    apply.guard = 1.0e-3;
    apply.clamp = an::KernelValueWindow{.lowest = 0.0, .highest = 0.6};
    const an::Result<an::BackgroundApplyResult> divided = an::applyImageBackground(
        execution, an::SampleType::Float32, samples.data(), layout, corrected.data(), layout, apply, models);
    ASSERT_TRUE(divided.hasValue()) << divided.status().message();
    EXPECT_DOUBLE_EQ(divided->appliedLevel[0], 0.5);
    EXPECT_GT(divided->guardedSamples, 0u);
    EXPECT_GT(divided->clampedSamples, 0u);

    // Where the model is well away from the guard the division restores the level exactly.
    for (std::uint32_t y = 0u; y < kHeight; y += 13u)
    {
        const std::size_t index = static_cast<std::size_t>(y) * kWidth + kWidth - 1u;
        ASSERT_NEAR(corrected[index], 0.5f, 1.0e-3f);
    }
}

TEST(BackgroundModelTest, ADitherIsBoundedByItsWidthAndDoesNotDependOnTheThreadCount)
{
    const an::EngineContext engine = makeEngine(8u);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t index = 0u; index < 5u; ++index)
    {
        an::BackgroundSample site{};
        site.x = 8.0 + 11.0 * static_cast<double>(index);
        site.y = 8.0 + 7.0 * static_cast<double>(index);
        site.channelCount = 1u;
        site.median[0] = 0.25;
        sites.push_back(site);
    }
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    {
        const an::ExecutionContext execution = makeExecution(engine);
        ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());
    }
    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    const std::array<an::BackgroundModelView, 1u> models{model};

    an::BackgroundApplyRequest apply{};
    apply.level = an::BackgroundLevel::Stated;
    apply.statedLevel = 0.0;
    constexpr double kWidthOfDither = 1.0e-3;
    apply.dither = kWidthOfDither;

    std::vector<float> narrow(samples.size(), 0.0f);
    std::vector<float> wide(samples.size(), 0.0f);
    {
        const an::ExecutionContext oneThread = makeExecution(engine, 1u);
        ASSERT_TRUE(an::applyImageBackground(oneThread, an::SampleType::Float32, samples.data(), layout, narrow.data(),
                                             layout, apply, models)
                        .hasValue());
    }
    {
        const an::ExecutionContext manyThreads = makeExecution(engine, 8u);
        ASSERT_TRUE(an::applyImageBackground(manyThreads, an::SampleType::Float32, samples.data(), layout, wide.data(),
                                             layout, apply, models)
                        .hasValue());
    }

    // The dither is a function of the sample position, so a one-thread run and an eight-thread run
    // produce the same frame bit for bit - which a generator shared between workers could not.
    bool anyDithered = false;
    bool anyNegative = false;
    double mean = 0.0;
    for (std::size_t index = 0u; index < narrow.size(); ++index)
    {
        ASSERT_EQ(narrow[index], wide[index]) << "at " << index;
        ASSERT_GE(narrow[index], -static_cast<float>(kWidthOfDither) * 0.501f);
        ASSERT_LT(narrow[index], static_cast<float>(kWidthOfDither) * 0.501f);
        anyDithered = anyDithered || narrow[index] > 0.0f;
        anyNegative = anyNegative || narrow[index] < 0.0f;
        mean += narrow[index];
    }
    EXPECT_TRUE(anyDithered);
    EXPECT_TRUE(anyNegative);
    EXPECT_NEAR(mean / static_cast<double>(narrow.size()), 0.0, kWidthOfDither * 0.03);

    apply.dither = 0.0;
    std::vector<float> plain(samples.size(), 1.0f);
    {
        const an::ExecutionContext execution = makeExecution(engine);
        ASSERT_TRUE(an::applyImageBackground(execution, an::SampleType::Float32, samples.data(), layout, plain.data(),
                                             layout, apply, models)
                        .hasValue());
    }
    for (const float sample : plain)
    {
        ASSERT_NEAR(sample, 0.0f, 1.0e-6f);
    }

    apply.dither = -1.0;
    {
        const an::ExecutionContext execution = makeExecution(engine);
        EXPECT_EQ(an::applyImageBackground(execution, an::SampleType::Float32, samples.data(), layout, plain.data(),
                                           layout, apply, models)
                      .status()
                      .code(),
                  an::ErrorCode::InvalidArgument);
    }
}

TEST(BackgroundModelTest, ACorrectionMayWriteWhereItReadsButNotSomewhereThatOverlaps)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples = gradientFrame(kWidth, kHeight, 0.4, 0.1, 0.0, 0.0);
    const std::vector<float> original = samples;

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t index = 0u; index < 6u; ++index)
    {
        an::BackgroundSample site{};
        site.x = 8.0 + 9.0 * static_cast<double>(index);
        site.y = 10.0 + 5.0 * static_cast<double>(index);
        site.channelCount = 1u;
        site.median[0] = surfaceAt(site.x, site.y, kWidth, kHeight, 0.4, 0.1, 0.0, 0.0);
        sites.push_back(site);
    }
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());
    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    const std::array<an::BackgroundModelView, 1u> models{model};

    an::BackgroundApplyRequest apply{};
    apply.level = an::BackgroundLevel::Stated;
    apply.statedLevel = 0.0;
    const an::Result<an::BackgroundApplyResult> inPlace = an::applyImageBackground(
        execution, an::SampleType::Float32, samples.data(), layout, samples.data(), layout, apply, models);
    ASSERT_TRUE(inPlace.hasValue()) << inPlace.status().message();
    EXPECT_EQ(inPlace->samplesChanged, static_cast<std::uint64_t>(kWidth) * kHeight);
    for (const float sample : samples)
    {
        ASSERT_NEAR(sample, 0.0f, 1.0e-4f);
    }

    std::vector<float> overlapping = original;
    EXPECT_EQ(an::applyImageBackground(execution, an::SampleType::Float32, overlapping.data(), layout,
                                       overlapping.data() + 1u, layout, apply, models)
                  .status()
                  .code(),
              an::ErrorCode::Aliasing);

    const std::array<an::BackgroundModelView, 2u> tooMany{model, model};
    EXPECT_EQ(an::applyImageBackground(execution, an::SampleType::Float32, original.data(), layout, samples.data(),
                                       layout, apply, tooMany)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    an::BackgroundApplyRequest broken = apply;
    broken.guard = 0.0;
    EXPECT_EQ(an::applyImageBackground(execution, an::SampleType::Float32, original.data(), layout, samples.data(),
                                       layout, broken, models)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    broken = apply;
    broken.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::applyImageBackground(execution, an::SampleType::Float32, original.data(), layout, samples.data(),
                                       layout, broken, models)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(BackgroundModelTest, ACorrectionLeavesARejectedSampleUntouchedAndOutOfEveryLevel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 32u;
    constexpr std::uint32_t kHeight = 24u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    // One sample the validity rule rejects, sitting where a mean would notice it and must not.
    samples[static_cast<std::size_t>(5u) * kWidth + 7u] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> corrected(samples.size(), -1.0f);

    an::SpatialSelection selection{};
    selection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t index = 0u; index < 4u; ++index)
    {
        an::BackgroundSample site{};
        site.x = 5.0 + 6.0 * static_cast<double>(index);
        site.y = 4.0 + 4.0 * static_cast<double>(index);
        site.channelCount = 1u;
        site.median[0] = 0.5;
        sites.push_back(site);
    }
    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::vector<double> coefficients(3u, 0.0);
    ASSERT_TRUE(an::fitBackgroundModel(execution, fit, sites, 0u, kWidth, kHeight, coefficients).hasValue());
    an::BackgroundModelView model{};
    model.fit = fit;
    model.sites = sites;
    model.coefficients = coefficients;
    model.width = kWidth;
    model.height = kHeight;
    const std::array<an::BackgroundModelView, 1u> models{model};

    an::BackgroundApplyRequest apply{};
    apply.level = an::BackgroundLevel::SourceMean;
    const an::Result<an::BackgroundApplyResult> removed = an::applyImageBackground(
        execution, an::SampleType::Float32, samples.data(), layout, corrected.data(), layout, apply, models, selection);
    ASSERT_TRUE(removed.hasValue()) << removed.status().message();
    // The level a masked sample never reached: every other sample is one half exactly.
    EXPECT_NEAR(removed->appliedLevel[0], 0.5, 1.0e-6);
    EXPECT_EQ(removed->imagePasses, 2u);
    EXPECT_EQ(removed->run.selection.valid, static_cast<std::uint64_t>(kWidth) * kHeight - 1u);
    EXPECT_EQ(corrected[static_cast<std::size_t>(5u) * kWidth + 7u], -1.0f);
}

TEST(BackgroundModelTest, AnInterleavedFrameIsCorrectedLikeAPlanarOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 40u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout planar = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);
    const an::ImageLayout interleaved = interleavedLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);

    std::vector<float> planarSamples(static_cast<std::size_t>(kWidth) * kHeight * kChannels, 0.0f);
    std::vector<float> interleavedSamples(planarSamples.size(), 0.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const auto value = static_cast<float>(
                    surfaceAt(x, y, kWidth, kHeight, 0.2 + 0.05 * static_cast<double>(channel), 0.06, -0.04, 0.0));
                planarSamples[(static_cast<std::size_t>(channel) * kHeight + y) * kWidth + x] = value;
                interleavedSamples[(static_cast<std::size_t>(y) * kWidth + x) * kChannels + channel] = value;
            }
        }
    }

    std::vector<an::BackgroundSample> sites;
    for (std::uint32_t row = 0u; row < 3u; ++row)
    {
        for (std::uint32_t column = 0u; column < 3u; ++column)
        {
            an::BackgroundSample site{};
            site.x = 6.0 + 17.0 * static_cast<double>(column);
            site.y = 6.0 + 13.0 * static_cast<double>(row);
            site.channelCount = kChannels;
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                site.median[channel] = surfaceAt(site.x, site.y, kWidth, kHeight,
                                                 0.2 + 0.05 * static_cast<double>(channel), 0.06, -0.04, 0.0);
            }
            sites.push_back(site);
        }
    }

    an::BackgroundFitRequest fit{};
    fit.degree = 1u;
    std::array<std::vector<double>, kChannels> coefficients{};
    std::vector<an::BackgroundModelView> models;
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        coefficients[channel].assign(3u, 0.0);
        ASSERT_TRUE(
            an::fitBackgroundModel(execution, fit, sites, channel, kWidth, kHeight, coefficients[channel]).hasValue());
        an::BackgroundModelView model{};
        model.fit = fit;
        model.sites = sites;
        model.coefficients = coefficients[channel];
        model.width = kWidth;
        model.height = kHeight;
        models.push_back(model);
    }

    an::BackgroundApplyRequest apply{};
    apply.level = an::BackgroundLevel::SampleMean;
    std::vector<float> planarCorrected(planarSamples.size(), 0.0f);
    std::vector<float> interleavedCorrected(planarSamples.size(), 0.0f);
    const an::Result<an::BackgroundApplyResult> fromPlanar =
        an::applyImageBackground(execution, an::SampleType::Float32, planarSamples.data(), planar,
                                 planarCorrected.data(), planar, apply, models);
    ASSERT_TRUE(fromPlanar.hasValue()) << fromPlanar.status().message();
    const an::Result<an::BackgroundApplyResult> fromInterleaved =
        an::applyImageBackground(execution, an::SampleType::Float32, interleavedSamples.data(), interleaved,
                                 interleavedCorrected.data(), interleaved, apply, models);
    ASSERT_TRUE(fromInterleaved.hasValue()) << fromInterleaved.status().message();

    // The level a sample mean produces is shared by every channel, which is what stops a correction
    // from moving a colour.
    for (std::uint32_t channel = 1u; channel < kChannels; ++channel)
    {
        EXPECT_DOUBLE_EQ(fromPlanar->appliedLevel[channel], fromPlanar->appliedLevel[0]);
    }
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const float fromOne = planarCorrected[(static_cast<std::size_t>(channel) * kHeight + y) * kWidth + x];
                const float fromOther =
                    interleavedCorrected[(static_cast<std::size_t>(y) * kWidth + x) * kChannels + channel];
                ASSERT_NEAR(fromOne, fromOther, 1.0e-6f) << "at channel " << channel << " (" << x << ", " << y << ")";
            }
        }
    }
}

TEST(BackgroundModelTest, AnAutomaticEstimateRecoversAGradientAndFlattensTheFrame)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 192u;
    constexpr std::uint32_t kHeight = 144u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples = gradientFrame(kWidth, kHeight, 0.3, 0.08, -0.05, 0.0);
    const std::vector<float> gradient = samples;
    // Stars: small, bright, and exactly what the rejection has to remove from the estimate.
    for (std::uint32_t index = 0u; index < 40u; ++index)
    {
        const std::uint32_t x = 7u + (index * 37u) % (kWidth - 14u);
        const std::uint32_t y = 5u + (index * 53u) % (kHeight - 10u);
        addDisc(samples, kWidth, x, y, 2.0, 0.5);
    }

    an::AutoBackgroundRequest request{};
    request.model = an::AutoBackgroundModel::Polynomial;
    request.polynomialDegree = 2u;
    request.downsample = 4u;
    request.protectStructures = false;
    request.apply.level = an::BackgroundLevel::ModelMedian;
    const an::Result<an::AutoBackgroundPlan> plan = an::planAutoBackground(request, kWidth, kHeight);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->downsample(), 4u);
    EXPECT_EQ(plan->workingWidth(), kWidth / 4u);
    EXPECT_EQ(plan->workingHeight(), kHeight / 4u);
    EXPECT_EQ(plan->termCount(), 6u);
    EXPECT_GT(plan->workingBytes(), 0u);
    EXPECT_GT(plan->workingPasses(), 0u);

    std::vector<float> surface(samples.size(), 0.0f);
    const std::optional<an::PlaneView<float>> plane =
        an::PlaneView<float>::create(surface, kWidth, kHeight, 1u, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    ASSERT_TRUE(plane.has_value());
    const std::array<an::PlaneView<float>, 1u> models{*plane};
    const an::Result<an::AutoBackgroundResult> estimated =
        an::estimateImageAutoBackground(execution, an::SampleType::Float32, samples.data(), layout, *plan, models);
    ASSERT_TRUE(estimated.hasValue()) << estimated.status().message();
    EXPECT_GE(estimated->iterationsRun, 2u);
    EXPECT_LE(estimated->iterationsRun, request.maxIterations);
    EXPECT_GT(estimated->acceptedSamples, 0u);
    EXPECT_LT(estimated->acceptedSamples, estimated->consideredSamples);
    EXPECT_EQ(estimated->protectedSamples, 0u);
    EXPECT_EQ(estimated->workingWidth, kWidth / 4u);

    // The estimate is the gradient the test put there, to well inside the amplitude of the stars it
    // had to ignore.
    for (std::uint32_t y = 8u; y + 8u < kHeight; y += 11u)
    {
        for (std::uint32_t x = 8u; x + 8u < kWidth; x += 13u)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            ASSERT_NEAR(surface[index], gradient[index], 0.02) << "at (" << x << ", " << y << ")";
        }
    }

    std::vector<float> corrected(samples.size(), 0.0f);
    const an::Result<an::AutoBackgroundResult> removed = an::removeImageAutoBackground(
        execution, an::SampleType::Float32, samples.data(), layout, corrected.data(), layout, *plan);
    ASSERT_TRUE(removed.hasValue()) << removed.status().message();
    EXPECT_GT(removed->samplesChanged, 0u);
    EXPECT_NEAR(removed->appliedLevel[0], 0.3, 0.02);
    // Measured over the sky alone: the stars are still there, and they carry more deviation than the
    // gradient did, so a measurement over every sample would say the frame got worse.
    const double before = backgroundDeviation(samples, samples, 0.45f);
    const double after = backgroundDeviation(corrected, samples, 0.45f);
    EXPECT_GT(before, 0.03);
    EXPECT_LT(after, before / 10.0);
}

TEST(BackgroundModelTest, StructureProtectionExcludesABrightExtendedRegionAndReportsIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 160u;
    constexpr std::uint32_t kHeight = 128u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.2f);
    addDisc(samples, kWidth, kWidth / 2u, kHeight / 2u, 24.0, 0.4);

    an::AutoBackgroundRequest request{};
    request.model = an::AutoBackgroundModel::InpaintedLowpass;
    request.downsample = 4u;
    request.scale = 6.0;
    request.smoothness = 0.0;
    request.maxIterations = 4u;
    request.protectStructures = true;
    request.protectionThreshold = 0.05;
    request.protectionAmount = 0.5;
    const an::Result<an::AutoBackgroundPlan> plan = an::planAutoBackground(request, kWidth, kHeight);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    std::vector<float> surface(samples.size(), 0.0f);
    std::vector<float> protection(samples.size(), -1.0f);
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(kWidth) * sizeof(float);
    const std::optional<an::PlaneView<float>> surfacePlane =
        an::PlaneView<float>::create(surface, kWidth, kHeight, 1u, rowBytes);
    const std::optional<an::PlaneView<float>> protectionPlane =
        an::PlaneView<float>::create(protection, kWidth, kHeight, 1u, rowBytes);
    ASSERT_TRUE(surfacePlane.has_value());
    ASSERT_TRUE(protectionPlane.has_value());
    const std::array<an::PlaneView<float>, 1u> models{*surfacePlane};
    const std::array<an::PlaneView<float>, 1u> masks{*protectionPlane};

    const an::Result<an::AutoBackgroundResult> estimated = an::estimateImageAutoBackground(
        execution, an::SampleType::Float32, samples.data(), layout, *plan, models, masks);
    ASSERT_TRUE(estimated.hasValue()) << estimated.status().message();
    EXPECT_GT(estimated->protectedSamples, 0u);
    EXPECT_GT(protection[static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u], 0.5f);
    EXPECT_LT(protection[0u], 0.5f);

    // The object it protected is the object it did not follow: the surface stays near the sky rather
    // than rising into the disc.
    EXPECT_NEAR(surface[static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u], 0.2, 0.08);

    std::uint64_t protectedWorkingSamples = 0u;
    for (std::uint32_t y = 0u; y < plan->workingHeight(); ++y)
    {
        for (std::uint32_t x = 0u; x < plan->workingWidth(); ++x)
        {
            protectedWorkingSamples +=
                protection[static_cast<std::size_t>(y * plan->downsample()) * kWidth + x * plan->downsample()] > 0.5f
                    ? 1u
                    : 0u;
        }
    }
    EXPECT_EQ(estimated->protectedSamples, protectedWorkingSamples);

    std::vector<float> corrected(samples.size(), -1.0f);
    const an::Result<an::AutoBackgroundResult> removed = an::removeImageAutoBackground(
        execution, an::SampleType::Float32, samples.data(), layout, corrected.data(), layout, *plan);
    ASSERT_TRUE(removed.hasValue()) << removed.status().message();
    EXPECT_EQ(removed->protectedSamples, estimated->protectedSamples);

    an::AutoBackgroundRequest unprotected = request;
    unprotected.protectStructures = false;
    const an::Result<an::AutoBackgroundPlan> plainPlan = an::planAutoBackground(unprotected, kWidth, kHeight);
    ASSERT_TRUE(plainPlan.hasValue()) << plainPlan.status().message();
    const an::Result<an::AutoBackgroundResult> plain =
        an::estimateImageAutoBackground(execution, an::SampleType::Float32, samples.data(), layout, *plainPlan, models);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_EQ(plain->protectedSamples, 0u);
}

TEST(BackgroundModelTest, AnAutomaticPlanRefusesTheRequestsThatCannotMeanAnything)
{
    an::AutoBackgroundRequest request{};
    EXPECT_EQ(an::planAutoBackground(request, 0u, 100u).status().code(), an::ErrorCode::InvalidArgument);

    an::AutoBackgroundRequest broken = request;
    broken.downsample = 3u;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.scale = 0.5;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.scale = -1.0;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.smoothness = 5.0;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.smoothness = -1.0;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.maxIterations = 0u;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.protectionAmount = 2.0;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);
    broken = request;
    broken.lowRejection = 0.0;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.convergence = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::InvalidArgument);
    // An automatic estimate places no sites, so the level rule that averages them cannot apply.
    broken = request;
    broken.apply.level = an::BackgroundLevel::SampleMean;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::InvalidArgument);
    broken = request;
    broken.model = an::AutoBackgroundModel::Polynomial;
    broken.polynomialDegree = an::kMaxBackgroundPolynomialDegree + 1u;
    EXPECT_EQ(an::planAutoBackground(broken, 100u, 100u).status().code(), an::ErrorCode::OutOfRange);

    EXPECT_STREQ(an::autoBackgroundModelName(an::AutoBackgroundModel::InpaintedLowpass), "inpainted-lowpass");
    EXPECT_STREQ(an::autoBackgroundModelName(static_cast<an::AutoBackgroundModel>(6u)), "unknown");
}

TEST(BackgroundModelTest, AnUnvalidatedPlanAndACancelledRunAreBothRefused)
{
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.3f);
    std::vector<float> destination(samples.size(), -1.0f);

    const an::ExecutionContext execution = makeExecution(engine);
    EXPECT_EQ(an::removeImageAutoBackground(execution, an::SampleType::Float32, samples.data(), layout,
                                            destination.data(), layout, an::AutoBackgroundPlan{})
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);

    an::AutoBackgroundRequest request{};
    request.downsample = 2u;
    request.maxIterations = 2u;
    const an::Result<an::AutoBackgroundPlan> plan = an::planAutoBackground(request, kWidth, kHeight);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    std::stop_source stopped;
    an::ExecutionOptions options;
    options.stopToken = stopped.get_token();
    an::Result<an::ExecutionContext> cancellable = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(cancellable.hasValue()) << cancellable.status().message();
    stopped.request_stop();
    EXPECT_EQ(an::removeImageAutoBackground(*cancellable, an::SampleType::Float32, samples.data(), layout,
                                            destination.data(), layout, *plan)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    for (const float sample : destination)
    {
        ASSERT_EQ(sample, -1.0f);
    }
}

TEST(BackgroundModelTest, FusedRemovalFitsInsideTheWorkingPictureBudget)
{
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 192u;
    an::AutoBackgroundRequest request{};
    request.model = an::AutoBackgroundModel::Polynomial;
    request.polynomialDegree = 2u;
    request.downsample = 4u;
    request.maxIterations = 3u;
    request.protectStructures = true;
    request.protectionThreshold = 0.05;
    const an::Result<an::AutoBackgroundPlan> plan = an::planAutoBackground(request, kWidth, kHeight);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    an::EngineContextOptions engineOptions{};
    engineOptions.maxConcurrency = 4u;
    engineOptions.limits.memoryBudgetBytes = plan->workingBytes();
    an::Result<an::EngineContext> madeEngine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(madeEngine.hasValue()) << madeEngine.status().message();
    an::EngineContext engine = std::move(*madeEngine);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> samples = gradientFrame(kWidth, kHeight, 0.3, 0.04, -0.02, 0.0);
    addDisc(samples, kWidth, kWidth / 2u, kHeight / 2u, 18.0, 0.4);
    std::vector<float> corrected(samples.size(), -1.0f);

    const an::Result<an::AutoBackgroundResult> removed = an::removeImageAutoBackground(
        execution, an::SampleType::Float32, samples.data(), layout, corrected.data(), layout, *plan);
    ASSERT_TRUE(removed.hasValue()) << removed.status().message();
    EXPECT_EQ(removed->workingBytes, plan->workingBytes());
    ASSERT_NE(engine.memoryResource(), nullptr);
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, plan->workingBytes());
}

TEST(BackgroundModelTest, TheDeclaredEffectsSayWhatEachStageDoes)
{
    EXPECT_EQ(an::kBackgroundSamplingEffect.samples, an::SampleEffect::Reads);
    EXPECT_EQ(an::checkOperationEffect(an::kBackgroundSamplingEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kBackgroundCorrectionEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kBackgroundCorrectionEffect.description, an::DescriptionEffect::Preserves);
    EXPECT_EQ(an::kBackgroundCorrectionEffect.validity.policy, an::ValidityPolicy::Preserve);
    EXPECT_EQ(an::checkOperationEffect(an::kBackgroundCorrectionEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kAutoBackgroundEstimateEffect.samples, an::SampleEffect::Reads);
    EXPECT_EQ(an::checkOperationEffect(an::kAutoBackgroundEstimateEffect), an::OperationEffectError::None);
}

} // namespace

TEST(BackgroundModelTest, AutomaticRemovalDitherIsCentredAndReproducible)
{
    const auto engine = makeEngine(8u);
    constexpr std::uint32_t width = 64u, height = 48u;
    const auto layout = planarLayout(an::SampleType::Float32, width, height);
    const std::vector<float> source(width * height, 0.25f);
    an::AutoBackgroundRequest request;
    request.model = an::AutoBackgroundModel::Polynomial;
    request.polynomialDegree = 1u;
    request.downsample = 2u;
    request.protectStructures = false;
    request.apply.level = an::BackgroundLevel::Stated;
    request.apply.statedLevel = 0.0;
    request.apply.dither = 0.01;
    const auto plan = an::planAutoBackground(request, width, height);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    std::vector<float> one(source.size()), many(source.size());
    for (const auto threads : {1u, 8u})
    {
        const auto execution = makeExecution(engine, threads);
        auto& output = threads == 1u ? one : many;
        const auto result = an::removeImageAutoBackground(execution, an::SampleType::Float32, source.data(), layout,
                                                          output.data(), layout, *plan);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
    }
    EXPECT_EQ(one, many);
    double sum = 0.0;
    for (const float value : one)
    {
        ASSERT_GE(value, -0.00501f);
        ASSERT_LE(value, 0.00501f);
        sum += value;
    }
    EXPECT_NEAR(sum / static_cast<double>(one.size()), 0.0, 0.0003);
}
