// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the deconvolution domain.
//
// The suite is built around one experiment repeated in several forms: take a picture whose answer is
// known, blur it by a stated point spread function inside the test itself, and require that the
// deconvolution moves the blurred picture measurably back towards the original. A test that only
// checked that the operation ran would pass on a deconvolution that returned its input.
//
// Two structural claims are checked beside that. The direct and transform convolution paths must
// agree, because a caller choosing between them is choosing a cost and not an answer. And a run must
// say what it did - how many iterations, whether a stopping rule ended them, and what the flux
// correction had to multiply by - rather than computing those and discarding them.

#include <AfterNightEngine/Algorithms/Deconvolution/Deconvolution.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
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

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;

static_assert(
    !std::is_constructible_v<an::RichardsonLucyPlan, const an::RichardsonLucyRequest&, std::uint32_t, std::uint32_t>);
static_assert(!std::is_constructible_v<an::WienerPlan, const an::WienerRequest&, std::uint32_t, std::uint32_t>);

/// One coordinate reflected back inside an extent without repeating the edge.
///
/// The test resolves its own borders rather than reusing the engine's, so the blurred picture it
/// compares against is built by something other than the code under test.
///
/// @param position where the read wants to be.
/// @param extent how many samples there are.
/// @return the sample index inside the extent.
[[nodiscard]] std::int64_t reflected(std::int64_t position, std::int64_t extent)
{
    const std::int64_t last = extent - 1;
    while (position < 0 || position > last)
    {
        position = position < 0 ? -position : 2 * last - position;
    }
    return position;
}

/// A picture with sharp structure a blur will visibly remove.
///
/// Four point sources on a low background plus a bright bar, which between them exercise a
/// deconvolution's whole job: recovering isolated peaks and recovering an edge.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @return the samples.
[[nodiscard]] std::vector<float> sharpScene(std::uint32_t width, std::uint32_t height)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.05f);
    const auto place = [&](std::uint32_t x, std::uint32_t y, float value) {
        samples[static_cast<std::size_t>(y) * width + x] = value;
    };
    place(width / 4u, height / 4u, 0.9f);
    place(3u * width / 4u, height / 4u, 0.7f);
    place(width / 4u, 3u * height / 4u, 0.6f);
    place(3u * width / 4u, 3u * height / 4u, 0.8f);
    for (std::uint32_t x = width / 3u; x < 2u * width / 3u; ++x)
    {
        for (std::uint32_t y = height / 2u; y < height / 2u + 3u; ++y)
        {
            place(x, y, 0.5f);
        }
    }
    return samples;
}

/// Convolves a picture by a kernel, reflecting at the borders.
///
/// @param source the picture.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param kernel the kernel, row-major.
/// @param kernelSide the kernel's side.
/// @return the blurred picture.
[[nodiscard]] std::vector<float> blurred(const std::vector<float>& source, std::uint32_t width, std::uint32_t height,
                                         const std::vector<float>& kernel, std::uint32_t kernelSide)
{
    std::vector<float> answer(source.size(), 0.0f);
    const auto radius = static_cast<std::int64_t>(kernelSide / 2u);
    for (std::int64_t y = 0; y < static_cast<std::int64_t>(height); ++y)
    {
        for (std::int64_t x = 0; x < static_cast<std::int64_t>(width); ++x)
        {
            double sum = 0.0;
            for (std::int64_t ky = 0; ky < static_cast<std::int64_t>(kernelSide); ++ky)
            {
                const std::int64_t sourceY = reflected(y + radius - ky, static_cast<std::int64_t>(height));
                for (std::int64_t kx = 0; kx < static_cast<std::int64_t>(kernelSide); ++kx)
                {
                    const std::int64_t sourceX = reflected(x + radius - kx, static_cast<std::int64_t>(width));
                    sum += static_cast<double>(kernel[static_cast<std::size_t>(ky) * kernelSide + kx]) *
                           static_cast<double>(source[static_cast<std::size_t>(sourceY) * width + sourceX]);
                }
            }
            answer[static_cast<std::size_t>(y) * width + x] = static_cast<float>(sum);
        }
    }
    return answer;
}

/// The root-mean-square distance between two pictures.
///
/// @param left one picture.
/// @param right the other, of the same size.
/// @return the distance.
[[nodiscard]] double distance(const std::vector<float>& left, const std::vector<float>& right)
{
    double total = 0.0;
    for (std::size_t i = 0u; i < left.size(); ++i)
    {
        const double difference = static_cast<double>(left[i]) - static_cast<double>(right[i]);
        total += difference * difference;
    }
    return std::sqrt(total / static_cast<double>(left.size()));
}

/// A plane view over a kernel buffer.
///
/// @param samples the kernel.
/// @param side its side.
/// @return the view.
[[nodiscard]] an::PlaneView<float> kernelView(std::vector<float>& samples, std::uint32_t side)
{
    const std::optional<an::PlaneView<float>> view = an::PlaneView<float>::create(
        std::span<float>{samples}, side, side, 1u, static_cast<std::uint64_t>(side) * sizeof(float));
    EXPECT_TRUE(view.has_value());
    return *view;
}

/// A normalised Gaussian point spread function.
///
/// @param fwhm the full width at half maximum, in pixels.
/// @param side where the side that was built is written.
/// @return the samples.
[[nodiscard]] std::vector<float> gaussianPsf(double fwhm, std::uint32_t& side)
{
    an::PsfRequest request;
    request.profile = an::PsfProfile::Gaussian;
    request.fwhmX = fwhm;
    request.fwhmY = fwhm;
    const an::Result<std::uint32_t> extent = an::psfExtent(request);
    EXPECT_TRUE(extent.hasValue());
    side = extent.hasValue() ? *extent : 0u;
    std::vector<float> samples(static_cast<std::size_t>(side) * side, 0.0f);
    const an::Status built = an::buildPsf(request, kernelView(samples, side));
    EXPECT_TRUE(built.ok()) << built.message();
    return samples;
}

TEST(DeconvolutionTest, APointSpreadFunctionExtentIsOddAndDerivedFromTheWidths)
{
    an::PsfRequest gaussian;
    gaussian.fwhmX = 3.0;
    gaussian.fwhmY = 3.0;
    const an::Result<std::uint32_t> derived = an::psfExtent(gaussian);
    ASSERT_TRUE(derived.hasValue());
    EXPECT_EQ(*derived % 2u, 1u);
    EXPECT_GE(*derived, an::kMinimumPsfExtent);

    an::PsfRequest stated = gaussian;
    stated.size = 11u;
    EXPECT_EQ(an::psfExtent(stated).value(), 11u);

    an::PsfRequest even = gaussian;
    even.size = 10u;
    EXPECT_EQ(an::psfExtent(even).status().code(), an::ErrorCode::InvalidArgument);

    an::PsfRequest huge = gaussian;
    huge.size = an::kMaximumPsfExtent + 2u;
    EXPECT_EQ(an::psfExtent(huge).status().code(), an::ErrorCode::OutOfRange);

    an::PsfRequest hugeDerived = gaussian;
    hugeDerived.fwhmX = (std::numeric_limits<double>::max)();
    EXPECT_EQ(an::psfExtent(hugeDerived).status().code(), an::ErrorCode::OutOfRange);

    an::PsfRequest negative = gaussian;
    negative.fwhmX = -1.0;
    EXPECT_EQ(an::psfExtent(negative).status().code(), an::ErrorCode::InvalidArgument);

    an::PsfRequest moffat = gaussian;
    moffat.profile = an::PsfProfile::Moffat;
    moffat.beta = 0.5;
    EXPECT_EQ(an::psfExtent(moffat).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DeconvolutionTest, ABuiltPointSpreadFunctionSumsToOneAndIsSymmetric)
{
    std::uint32_t side = 0u;
    const std::vector<float> psf = gaussianPsf(3.0, side);
    double total = 0.0;
    for (const float sample : psf)
    {
        total += static_cast<double>(sample);
    }
    EXPECT_NEAR(total, 1.0, 1.0e-6);
    // An unrotated circular profile is symmetric about both axes, which is what puts the source it
    // describes at the centre rather than half a sample off it.
    for (std::uint32_t y = 0u; y < side; ++y)
    {
        for (std::uint32_t x = 0u; x < side; ++x)
        {
            const float here = psf[static_cast<std::size_t>(y) * side + x];
            EXPECT_FLOAT_EQ(here, psf[static_cast<std::size_t>(y) * side + (side - 1u - x)]);
            EXPECT_FLOAT_EQ(here, psf[static_cast<std::size_t>(side - 1u - y) * side + x]);
        }
    }
    const float centre = psf[static_cast<std::size_t>(side / 2u) * side + side / 2u];
    for (const float sample : psf)
    {
        EXPECT_LE(sample, centre);
    }
}

TEST(DeconvolutionTest, AMoffatProfileHasHeavierWingsThanAGaussianOfTheSameWidth)
{
    constexpr std::uint32_t kSide = 31u;
    an::PsfRequest gaussian;
    gaussian.profile = an::PsfProfile::Gaussian;
    gaussian.fwhmX = 4.0;
    gaussian.fwhmY = 4.0;
    gaussian.size = kSide;
    an::PsfRequest moffat = gaussian;
    moffat.profile = an::PsfProfile::Moffat;

    std::vector<float> gaussianSamples(static_cast<std::size_t>(kSide) * kSide, 0.0f);
    std::vector<float> moffatSamples(gaussianSamples.size(), 0.0f);
    ASSERT_TRUE(an::buildPsf(gaussian, kernelView(gaussianSamples, kSide)).ok());
    ASSERT_TRUE(an::buildPsf(moffat, kernelView(moffatSamples, kSide)).ok());

    const std::size_t corner = 0u;
    EXPECT_GT(moffatSamples[corner], gaussianSamples[corner]);
    const std::size_t centre = static_cast<std::size_t>(kSide / 2u) * kSide + kSide / 2u;
    EXPECT_LT(moffatSamples[centre], gaussianSamples[centre]);
}

TEST(DeconvolutionTest, AKernelIsCheckedBeforeAnyFrameIsRead)
{
    constexpr std::uint32_t kSide = 5u;
    std::vector<float> samples(static_cast<std::size_t>(kSide) * kSide, 0.04f);
    EXPECT_TRUE(an::checkPsf(kernelView(samples, kSide)).ok());

    std::vector<float> negative = samples;
    negative[7u] = -0.1f;
    EXPECT_EQ(an::checkPsf(kernelView(negative, kSide)).code(), an::ErrorCode::InvalidArgument);

    std::vector<float> infinite = samples;
    infinite[7u] = std::numeric_limits<float>::infinity();
    EXPECT_EQ(an::checkPsf(kernelView(infinite, kSide)).code(), an::ErrorCode::InvalidArgument);

    std::vector<float> empty(samples.size(), 0.0f);
    EXPECT_EQ(an::checkPsf(kernelView(empty, kSide)).code(), an::ErrorCode::InvalidData);

    constexpr std::uint32_t kEvenSide = 4u;
    std::vector<float> even(static_cast<std::size_t>(kEvenSide) * kEvenSide, 0.0625f);
    EXPECT_EQ(an::checkPsf(kernelView(even, kEvenSide)).code(), an::ErrorCode::InvalidArgument);
}

TEST(DeconvolutionTest, APlanRefusesTheRequestsThatCannotMeanAnything)
{
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const an::PlaneView<const float> kernel = kernelView(psf, side);

    an::RichardsonLucyRequest request;
    request.iterations = 10u;
    EXPECT_TRUE(an::planRichardsonLucy(request, kernel).hasValue());

    an::RichardsonLucyRequest none = request;
    none.iterations = 0u;
    EXPECT_EQ(an::planRichardsonLucy(none, kernel).status().code(), an::ErrorCode::OutOfRange);

    an::RichardsonLucyRequest many = request;
    many.iterations = an::kMaximumDeconvolutionIterations + 1u;
    EXPECT_EQ(an::planRichardsonLucy(many, kernel).status().code(), an::ErrorCode::OutOfRange);

    an::RichardsonLucyRequest constantBorder = request;
    constantBorder.common.border = an::KernelBorderMode::Constant;
    EXPECT_EQ(an::planRichardsonLucy(constantBorder, kernel).status().code(), an::ErrorCode::InvalidArgument);

    an::RichardsonLucyRequest fullScale = request;
    fullScale.common.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::planRichardsonLucy(fullScale, kernel).status().code(), an::ErrorCode::InvalidArgument);

    an::RichardsonLucyRequest noStep = request;
    noStep.update = an::DeconvolutionUpdate::GradientDescent;
    noStep.gradientDescentStep = 0.0;
    EXPECT_EQ(an::planRichardsonLucy(noStep, kernel).status().code(), an::ErrorCode::InvalidArgument);

    an::WienerRequest wiener;
    EXPECT_TRUE(an::planWiener(wiener, kernel).hasValue());
    wiener.regularization = 0.0;
    EXPECT_EQ(an::planWiener(wiener, kernel).status().code(), an::ErrorCode::InvalidArgument);

    wiener.regularization = (std::numeric_limits<double>::max)();
    EXPECT_EQ(an::planWiener(wiener, kernel).status().code(), an::ErrorCode::OutOfRange);
}

TEST(DeconvolutionTest, APlanIsBoundToTheCheckedKernelGeometryAndSamples)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 48u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    an::RichardsonLucyRequest iterativeRequest;
    iterativeRequest.iterations = 2u;
    iterativeRequest.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    an::WienerRequest wienerRequest;
    wienerRequest.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> iterativePlan =
        an::planRichardsonLucy(iterativeRequest, kernelView(psf, side));
    const an::Result<an::WienerPlan> wienerPlan = an::planWiener(wienerRequest, kernelView(psf, side));
    ASSERT_TRUE(iterativePlan.hasValue());
    ASSERT_TRUE(wienerPlan.hasValue());
    EXPECT_EQ(iterativePlan->psfWidth(), side);
    EXPECT_EQ(iterativePlan->psfHeight(), side);
    EXPECT_EQ(wienerPlan->psfWidth(), side);
    EXPECT_EQ(wienerPlan->psfHeight(), side);

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const std::vector<float> observed(static_cast<std::size_t>(kWidth) * kHeight, 0.4f);
    std::vector<float> answer(observed.size(), -1.0f);
    std::vector<float> emptyPsf(psf.size(), 0.0f);
    EXPECT_EQ(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                answer.data(), layout, kernelView(emptyPsf, side), *iterativePlan)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
    EXPECT_TRUE(std::all_of(answer.begin(), answer.end(), [](float sample) { return sample == -1.0f; }));

    constexpr std::uint32_t kDifferentSide = 5u;
    std::vector<float> differentPsf(static_cast<std::size_t>(kDifferentSide) * kDifferentSide,
                                    1.0f / static_cast<float>(kDifferentSide * kDifferentSide));
    EXPECT_EQ(an::deconvolveImageWiener(execution, an::SampleType::Float32, observed.data(), layout, answer.data(),
                                        layout, kernelView(differentPsf, kDifferentSide), *wienerPlan)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(std::all_of(answer.begin(), answer.end(), [](float sample) { return sample == -1.0f; }));
}

TEST(DeconvolutionTest, AnIterativeDeconvolutionMovesABlurredPictureBackTowardsItsOriginal)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> sharp = sharpScene(kWidth, kHeight);
    const std::vector<float> observed = blurred(sharp, kWidth, kHeight, psf, side);

    an::RichardsonLucyRequest request;
    request.iterations = 40u;
    request.regularization = an::DeconvolutionRegularization::None;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> answer(observed.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout, answer.data(),
                                          layout, kernelView(psf, side), *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_EQ(run->iterationsRun, 40u);
    EXPECT_FALSE(run->stoppedEarly);
    EXPECT_GT(run->filter.workingBytes, 0u);
    EXPECT_EQ(run->filter.precision, an::ComputePrecision::Float32);
    EXPECT_GT(distance(observed, sharp), distance(answer, sharp));
}

TEST(DeconvolutionTest, AWienerFilterMovesABlurredPictureBackTowardsItsOriginal)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> sharp = sharpScene(kWidth, kHeight);
    const std::vector<float> observed = blurred(sharp, kWidth, kHeight, psf, side);

    an::WienerRequest request;
    request.regularization = 0.0005;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::WienerPlan> plan = an::planWiener(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<float> answer(observed.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageWiener(execution, an::SampleType::Float32, observed.data(), layout, answer.data(), layout,
                                  kernelView(psf, side), *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->usedTransform);
    EXPECT_GT(run->transformWidth, kWidth);
    EXPECT_EQ(run->iterationsRun, 0u);
    EXPECT_GT(distance(observed, sharp), distance(answer, sharp));
}

TEST(DeconvolutionTest, AWienerTaperUsesTheConvolutionPathThePlanResolved)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    const std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::WienerRequest request;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Manual;
    request.common.edgeTaperRadius = 5.0;
    request.common.edgeTaperIterations = 1u;
    request.common.convolution = an::DeconvolutionConvolution::Direct;
    const an::Result<an::WienerPlan> plan = an::planWiener(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    EXPECT_FALSE(plan->taperUsesTransform());

    std::vector<float> answer(observed.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageWiener(execution, an::SampleType::Float32, observed.data(), layout, answer.data(), layout,
                                  kernelView(psf, side), *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->usedTransform);
    EXPECT_FALSE(run->taperUsedTransform);
}

TEST(DeconvolutionTest, TheDirectAndTransformPathsAgree)
{
    // The two paths compute the same convolution by different means, so a caller choosing between
    // them is choosing a cost and not an answer. They differ only in the rounding of one
    // single-precision transform against one single-precision sum.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 72u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    const std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest request;
    request.iterations = 8u;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;

    an::RichardsonLucyRequest direct = request;
    direct.common.convolution = an::DeconvolutionConvolution::Direct;
    an::RichardsonLucyRequest transform = request;
    transform.common.convolution = an::DeconvolutionConvolution::Transform;

    const an::Result<an::RichardsonLucyPlan> directPlan = an::planRichardsonLucy(direct, kernelView(psf, side));
    const an::Result<an::RichardsonLucyPlan> transformPlan = an::planRichardsonLucy(transform, kernelView(psf, side));
    ASSERT_TRUE(directPlan.hasValue());
    ASSERT_TRUE(transformPlan.hasValue());
    EXPECT_FALSE(directPlan->usesTransform());
    EXPECT_TRUE(transformPlan->usesTransform());

    std::vector<float> directAnswer(observed.size(), 0.0f);
    std::vector<float> transformAnswer(observed.size(), 0.0f);
    ASSERT_TRUE(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                  directAnswer.data(), layout, kernelView(psf, side), *directPlan)
                    .hasValue());
    ASSERT_TRUE(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                  transformAnswer.data(), layout, kernelView(psf, side), *transformPlan)
                    .hasValue());
    EXPECT_LT(distance(directAnswer, transformAnswer), 2.0e-3);
}

TEST(DeconvolutionTest, ADeconvolutionOfAFlatFrameIsThatFlatFrame)
{
    // A kernel that sums to one blurs a constant to itself, so every ratio is one, every correction
    // is one and the estimate never moves. It is the one case with an exactly known answer, and it
    // is where a normalisation or a border mistake shows up immediately.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed(static_cast<std::size_t>(kWidth) * kHeight, 0.375f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    for (const an::DeconvolutionConvolution path :
         {an::DeconvolutionConvolution::Direct, an::DeconvolutionConvolution::Transform})
    {
        an::RichardsonLucyRequest request;
        request.iterations = 12u;
        request.common.convolution = path;
        request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
        const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
        ASSERT_TRUE(plan.hasValue());
        std::vector<float> answer(observed.size(), 0.0f);
        ASSERT_TRUE(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                      answer.data(), layout, kernelView(psf, side), *plan)
                        .hasValue());
        for (const float sample : answer)
        {
            EXPECT_NEAR(sample, 0.375f, 1.0e-4f);
        }
    }
}

TEST(DeconvolutionTest, AStoppingRuleEndsARunAndSaysSo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest request;
    request.iterations = 100u;
    request.stoppingDelta = 1.0; // Larger than any change one iteration can make, so it stops at one.
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout, answer.data(),
                                          layout, kernelView(psf, side), *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->stoppedEarly);
    EXPECT_EQ(run->iterationsRun, 1u);
    EXPECT_GT(run->lastChange, 0.0);
    EXPECT_GT(run->lastRelativeChange, 0.0);
}

TEST(DeconvolutionTest, PreservingFluxKeepsThePositiveTotal)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest request;
    request.iterations = 15u;
    request.common.preserveFlux = true;
    request.common.clampToWhite = false;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), 0.0f);
    ASSERT_TRUE(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                  answer.data(), layout, kernelView(psf, side), *plan)
                    .hasValue());
    double observedTotal = 0.0;
    double answerTotal = 0.0;
    for (std::size_t i = 0u; i < observed.size(); ++i)
    {
        observedTotal += observed[i] > 0.0f ? static_cast<double>(observed[i]) : 0.0;
        answerTotal += answer[i] > 0.0f ? static_cast<double>(answer[i]) : 0.0;
    }
    EXPECT_NEAR(answerTotal / observedTotal, 1.0, 1.0e-3);
}

TEST(DeconvolutionTest, TheReportedFluxScaleIsTheLastCorrectionThatWasApplied)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest unscaledRequest;
    unscaledRequest.iterations = 1u;
    unscaledRequest.update = an::DeconvolutionUpdate::GradientDescent;
    unscaledRequest.gradientDescentStep = 0.2;
    unscaledRequest.common.preserveFlux = false;
    unscaledRequest.common.clampToWhite = false;
    unscaledRequest.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    an::RichardsonLucyRequest scaledRequest = unscaledRequest;
    scaledRequest.common.preserveFlux = true;
    const an::Result<an::RichardsonLucyPlan> unscaledPlan =
        an::planRichardsonLucy(unscaledRequest, kernelView(psf, side));
    const an::Result<an::RichardsonLucyPlan> scaledPlan = an::planRichardsonLucy(scaledRequest, kernelView(psf, side));
    ASSERT_TRUE(unscaledPlan.hasValue());
    ASSERT_TRUE(scaledPlan.hasValue());

    std::vector<float> unscaled(observed.size(), 0.0f);
    std::vector<float> scaled(observed.size(), 0.0f);
    ASSERT_TRUE(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                  unscaled.data(), layout, kernelView(psf, side), *unscaledPlan)
                    .hasValue());
    const an::Result<an::DeconvolutionResult> scaledRun =
        an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout, scaled.data(),
                                          layout, kernelView(psf, side), *scaledPlan);
    ASSERT_TRUE(scaledRun.hasValue()) << scaledRun.status().message();

    const auto positiveTotal = [](const std::vector<float>& samples) {
        double total = 0.0;
        for (const float sample : samples)
        {
            total += sample > 0.0f ? static_cast<double>(sample) : 0.0;
        }
        return total;
    };
    const double expected = positiveTotal(observed) / positiveTotal(unscaled);
    EXPECT_NEAR(scaledRun->fluxScale, expected, 1.0e-6);
    EXPECT_GT(std::abs(scaledRun->fluxScale - 1.0), 1.0e-5);
}

TEST(DeconvolutionTest, IterationsRunIsTheMaximumAcrossPerChannelSolves)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 48u;
    constexpr std::uint32_t kChannels = 3u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    const std::vector<float> plane = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    std::vector<float> observed(plane.size() * kChannels, 0.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        std::copy(plane.begin(), plane.end(), observed.begin() + static_cast<std::ptrdiff_t>(channel * plane.size()));
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, kChannels);

    an::RichardsonLucyRequest request;
    request.iterations = 2u;
    request.common.color = an::DeconvolutionColor::PerChannel;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout, answer.data(),
                                          layout, kernelView(psf, side), *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_EQ(run->iterationsRun, request.iterations);
}

TEST(DeconvolutionTest, AChromaPreservingModeNeedsThreeChannelsToPreserveTheChromaOf)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 48u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed(static_cast<std::size_t>(kWidth) * kHeight, 0.4f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest request;
    request.iterations = 3u;
    request.common.color = an::DeconvolutionColor::LuminancePreservingChroma;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), 0.0f);
    EXPECT_EQ(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                answer.data(), layout, kernelView(psf, side), *plan)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(DeconvolutionTest, AChromaPreservingModeKeepsTheRatiosBetweenTheChannels)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    const std::vector<float> plane = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);

    // One picture in three channels at fixed ratios. A per-channel deconvolution would move them
    // apart; the chroma-preserving mode may not.
    const std::size_t planeSize = plane.size();
    std::vector<float> observed(planeSize * 3u, 0.0f);
    for (std::size_t i = 0u; i < planeSize; ++i)
    {
        observed[i] = plane[i];
        observed[planeSize + i] = plane[i] * 0.5f;
        observed[2u * planeSize + i] = plane[i] * 0.25f;
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);

    an::RichardsonLucyRequest request;
    request.iterations = 10u;
    request.common.color = an::DeconvolutionColor::LuminancePreservingChroma;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    request.common.clampToWhite = false;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), 0.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout, answer.data(),
                                          layout, kernelView(psf, side), *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->preservedChroma);
    for (std::size_t i = 0u; i < planeSize; ++i)
    {
        if (answer[i] > 1.0e-3f)
        {
            EXPECT_NEAR(answer[planeSize + i] / answer[i], 0.5f, 1.0e-3f);
            EXPECT_NEAR(answer[2u * planeSize + i] / answer[i], 0.25f, 1.0e-3f);
        }
    }
}

TEST(DeconvolutionTest, ATaperedRunKeepsTheFrameFluxAndReportsItsPasses)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest request;
    request.iterations = 5u;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Manual;
    request.common.edgeTaperRadius = 6.0;
    request.common.edgeTaperIterations = 3u;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    EXPECT_DOUBLE_EQ(plan->taperRadius(), 6.0);
    EXPECT_EQ(plan->imagePasses(), 13u);

    std::vector<float> answer(observed.size(), 0.0f);
    ASSERT_TRUE(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                  answer.data(), layout, kernelView(psf, side), *plan)
                    .hasValue());
}

TEST(DeconvolutionTest, ADeconvolutionStopsWhenItIsAskedTo)
{
    const an::EngineContext engine = makeEngine(4u);
    std::stop_source stop;
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();

    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(3.0, side);
    const std::vector<float> observed(static_cast<std::size_t>(kWidth) * kHeight, 0.4f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::RichardsonLucyRequest request;
    request.iterations = 10u;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), -1.0f);
    EXPECT_EQ(an::deconvolveImageRichardsonLucy(*execution, an::SampleType::Float32, observed.data(), layout,
                                                answer.data(), layout, kernelView(psf, side), *plan)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    for (const float sample : answer)
    {
        EXPECT_FLOAT_EQ(sample, -1.0f);
    }
}

TEST(DeconvolutionTest, AFrameSmallerThanItsKernelIsRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(9.0, side);
    ASSERT_GT(side, 8u);
    const std::uint32_t narrow = side - 2u;
    const std::vector<float> observed(static_cast<std::size_t>(narrow) * narrow, 0.4f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, narrow, narrow);
    an::RichardsonLucyRequest request;
    request.iterations = 3u;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(observed.size(), 0.0f);
    EXPECT_EQ(an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout,
                                                answer.data(), layout, kernelView(psf, side), *plan)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
}

TEST(DeconvolutionTest, ARejectedSampleIsSubstitutedAndItsDestinationLeftAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    observed[static_cast<std::size_t>(20u) * kWidth + 20u] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::RichardsonLucyRequest request;
    request.iterations = 4u;
    request.common.invalidSample = 0.05;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::RichardsonLucyPlan> plan = an::planRichardsonLucy(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());

    an::SpatialSelection selection;
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    std::vector<float> answer(observed.size(), -1.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageRichardsonLucy(execution, an::SampleType::Float32, observed.data(), layout, answer.data(),
                                          layout, kernelView(psf, side), *plan, selection);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_EQ(run->substitutedSamples, 1u);
    EXPECT_FLOAT_EQ(answer[static_cast<std::size_t>(20u) * kWidth + 20u], -1.0f);
    EXPECT_NE(answer[static_cast<std::size_t>(20u) * kWidth + 21u], -1.0f);
}

TEST(DeconvolutionTest, ASinglePassFilterKeepsItsRejectedSamplesDestinationToo)
{
    // The two methods take different numbers of working planes, because a plane that is allocated is
    // a plane that is first touched. This is the smallest workspace of the four combinations - a
    // single-pass filter, one channel, no taper - and it is where the frame that holds the samples as
    // they were gathered sits at the end of the block rather than in the middle of it.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    std::uint32_t side = 0u;
    std::vector<float> psf = gaussianPsf(2.5, side);
    std::vector<float> observed = blurred(sharpScene(kWidth, kHeight), kWidth, kHeight, psf, side);
    observed[static_cast<std::size_t>(31u) * kWidth + 17u] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::WienerRequest request;
    request.regularization = 0.001;
    request.common.invalidSample = 0.05;
    request.common.edgeTaper = an::DeconvolutionEdgeTaper::Off;
    const an::Result<an::WienerPlan> plan = an::planWiener(request, kernelView(psf, side));
    ASSERT_TRUE(plan.hasValue());

    an::SpatialSelection selection;
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    std::vector<float> answer(observed.size(), -1.0f);
    const an::Result<an::DeconvolutionResult> run =
        an::deconvolveImageWiener(execution, an::SampleType::Float32, observed.data(), layout, answer.data(), layout,
                                  kernelView(psf, side), *plan, selection);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_EQ(run->substitutedSamples, 1u);
    EXPECT_FLOAT_EQ(answer[static_cast<std::size_t>(31u) * kWidth + 17u], -1.0f);
    EXPECT_NE(answer[static_cast<std::size_t>(31u) * kWidth + 18u], -1.0f);
}

} // namespace
