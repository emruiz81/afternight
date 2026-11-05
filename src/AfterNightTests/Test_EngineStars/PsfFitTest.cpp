// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract and accuracy tests for the point-spread-function fit.

#include <AfterNightEngine/Algorithms/Stars/PsfFit.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "StarFieldFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::uint32_t concurrency = 2u)
{
    an::ExecutionOptions options;
    options.backend = backend;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// One elliptical Gaussian rendered into a square box, in the fit's own coordinate convention.
[[nodiscard]] std::vector<double> renderBox(std::uint32_t radius, double background, double amplitude, double centreX,
                                            double centreY, double sigmaMajor, double sigmaMinor, double angle)
{
    const std::size_t side = 2u * radius + 1u;
    std::vector<double> box(side * side, 0.0);
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double inverseMajor = 1.0 / (2.0 * sigmaMajor * sigmaMajor);
    const double inverseMinor = 1.0 / (2.0 * sigmaMinor * sigmaMinor);
    for (std::size_t row = 0u; row < side; ++row)
    {
        for (std::size_t column = 0u; column < side; ++column)
        {
            const double offsetX = static_cast<double>(column) + 0.5 - centreX;
            const double offsetY = static_cast<double>(row) + 0.5 - centreY;
            const double alongMajor = cosine * offsetX - sine * offsetY;
            const double alongMinor = sine * offsetX + cosine * offsetY;
            box[row * side + column] = background + amplitude * std::exp(-(alongMajor * alongMajor * inverseMajor +
                                                                           alongMinor * alongMinor * inverseMinor));
        }
    }
    return box;
}

/// One Moffat rendered into a square box.
[[nodiscard]] std::vector<double> renderMoffatBox(std::uint32_t radius, double background, double amplitude,
                                                  double centre, double coreRadius, double beta)
{
    const std::size_t side = 2u * radius + 1u;
    std::vector<double> box(side * side, 0.0);
    const double inverseCore = 1.0 / (coreRadius * coreRadius);
    for (std::size_t row = 0u; row < side; ++row)
    {
        for (std::size_t column = 0u; column < side; ++column)
        {
            const double offsetX = static_cast<double>(column) + 0.5 - centre;
            const double offsetY = static_cast<double>(row) + 0.5 - centre;
            const double quadratic = (offsetX * offsetX + offsetY * offsetY) * inverseCore;
            box[row * side + column] = background + amplitude * std::pow(1.0 + quadratic, -beta);
        }
    }
    return box;
}

} // namespace

TEST(PsfFitTest, ScaleAndFullWidthAreInverses)
{
    const an::Result<double> scale = an::psfScaleFromFullWidth(4.0, 0.0, an::PsfModel::Gaussian);
    ASSERT_TRUE(scale.hasValue()) << scale.status().message();
    // The Gaussian's scale parameter is twice the variance, so the standard deviation is the square
    // root of half of it.
    const double sigma = std::sqrt(*scale * 0.5);
    const an::Result<double> width = an::psfFullWidthFromScale(sigma, 0.0, an::PsfModel::Gaussian);
    ASSERT_TRUE(width.hasValue()) << width.status().message();
    EXPECT_NEAR(*width, 4.0, 1e-9);

    const an::Result<double> moffatScale = an::psfScaleFromFullWidth(5.0, 2.5, an::PsfModel::MoffatFixedBeta);
    ASSERT_TRUE(moffatScale.hasValue()) << moffatScale.status().message();
    const an::Result<double> moffatWidth =
        an::psfFullWidthFromScale(std::sqrt(*moffatScale), 2.5, an::PsfModel::MoffatFixedBeta);
    ASSERT_TRUE(moffatWidth.hasValue()) << moffatWidth.status().message();
    EXPECT_NEAR(*moffatWidth, 5.0, 1e-9);
}

TEST(PsfFitTest, ScaleAndFullWidthRefuseUnusableArguments)
{
    EXPECT_EQ(an::psfScaleFromFullWidth(0.0, 0.0, an::PsfModel::Gaussian).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::psfScaleFromFullWidth(std::numeric_limits<double>::quiet_NaN(), 0.0, an::PsfModel::Gaussian)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::psfScaleFromFullWidth(4.0, 0.0, an::PsfModel::MoffatFixedBeta).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::psfFullWidthFromScale(-1.0, 0.0, an::PsfModel::Gaussian).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::psfFullWidthFromScale(1.0, 0.0, static_cast<an::PsfModel>(9)).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(PsfFitTest, RecoversAPlantedGaussian)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kRadius = 9u;
    const double sigmaMajor = 2.1;
    const double sigmaMinor = 1.6;
    const double angle = 0.4;
    const std::vector<double> box = renderBox(kRadius, 0.05, 0.7, 9.35, 9.72, sigmaMajor, sigmaMinor, angle);
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);

    const an::PsfFitRequest request{.background = 0.05, .saturationLevel = 10.0};
    const an::Result<an::PsfFitResult> fit = an::fitStarPsf(execution, box, kRadius, request, scratch);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->report.outcome, an::PsfFitOutcome::Converged);
    EXPECT_EQ(fit->report.withheldSamples, 0u);
    EXPECT_EQ(fit->report.fittedSamples, static_cast<std::uint64_t>(2u * kRadius + 1u) * (2u * kRadius + 1u));

    EXPECT_NEAR(fit->psf.centreX, 9.35, 0.01);
    EXPECT_NEAR(fit->psf.centreY, 9.72, 0.01);
    EXPECT_NEAR(fit->psf.background, 0.05, 1e-3);
    EXPECT_NEAR(fit->psf.amplitude, 0.7, 5e-3);
    EXPECT_NEAR(fit->psf.fullWidthX, sigmaMajor * an::kGaussianFullWidthPerSigma, 0.02);
    EXPECT_NEAR(fit->psf.fullWidthY, sigmaMinor * an::kGaussianFullWidthPerSigma, 0.02);
    EXPECT_NEAR(fit->metrics.roundness, sigmaMinor / sigmaMajor, 0.01);
    EXPECT_LT(fit->psf.residualRootMeanSquare, 1e-6);
    EXPECT_FALSE(fit->psf.beta.has_value());
    EXPECT_TRUE(fit->instrumentalMagnitude.has_value());
    EXPECT_TRUE(fit->report.uncertaintiesAvailable);
    EXPECT_TRUE(fit->uncertainty.amplitude.has_value());
}

TEST(PsfFitTest, StatesArcsecondWidthsOnlyWhenAPlateScaleIsGiven)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kRadius = 8u;
    const std::vector<double> box = renderBox(kRadius, 0.02, 0.5, 8.5, 8.5, 1.8, 1.8, 0.0);
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);

    an::PsfFitRequest request{.background = 0.02, .saturationLevel = 10.0};
    const an::Result<an::PsfFitResult> plain = an::fitStarPsf(execution, box, kRadius, request, scratch);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_FALSE(plain->psf.fullWidthXArcseconds.has_value());

    request.arcsecondsPerPixel = 1.25;
    const an::Result<an::PsfFitResult> scaled = an::fitStarPsf(execution, box, kRadius, request, scratch);
    ASSERT_TRUE(scaled.hasValue()) << scaled.status().message();
    ASSERT_TRUE(scaled->psf.fullWidthXArcseconds.has_value());
    EXPECT_NEAR(*scaled->psf.fullWidthXArcseconds, scaled->psf.fullWidthX * 1.25, 1e-9);
    EXPECT_NEAR(*scaled->psf.fullWidthYArcseconds, scaled->psf.fullWidthY * 1.25, 1e-9);
}

TEST(PsfFitTest, RecoversAPlantedMoffatWithBothExponentPolicies)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kRadius = 12u;
    const double core = 2.4;
    const double beta = 2.5;
    const std::vector<double> box = renderMoffatBox(kRadius, 0.03, 0.6, 12.5, core, beta);
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);

    const an::PsfFitRequest fixed{.model = an::PsfModel::MoffatFixedBeta,
                                  .fixedBeta = beta,
                                  .background = 0.03,
                                  .saturationLevel = 10.0,
                                  .convergence = 4u};
    const an::Result<an::PsfFitResult> heldFit = an::fitStarPsf(execution, box, kRadius, fixed, scratch);
    ASSERT_TRUE(heldFit.hasValue()) << heldFit.status().message();
    ASSERT_TRUE(heldFit->psf.beta.has_value());
    EXPECT_DOUBLE_EQ(*heldFit->psf.beta, beta);
    EXPECT_NEAR(heldFit->psf.scaleX, core, 0.02);
    EXPECT_NEAR(heldFit->psf.centreX, 12.5, 0.02);

    const an::PsfFitRequest free{
        .model = an::PsfModel::MoffatFreeBeta, .background = 0.03, .saturationLevel = 10.0, .convergence = 6u};
    const an::Result<an::PsfFitResult> freeFit = an::fitStarPsf(execution, box, kRadius, free, scratch);
    ASSERT_TRUE(freeFit.hasValue()) << freeFit.status().message();
    ASSERT_TRUE(freeFit->psf.beta.has_value());
    EXPECT_NEAR(*freeFit->psf.beta, beta, 0.25);
    EXPECT_NEAR(freeFit->psf.fullWidthX, heldFit->psf.fullWidthX, 0.1);
}

TEST(PsfFitTest, WithholdsSaturatedSamplesFromTheResidual)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kRadius = 10u;
    const double sigma = 2.2;
    std::vector<double> box = renderBox(kRadius, 0.04, 0.9, 10.5, 10.5, sigma, sigma, 0.0);
    // Clip the core, as a real saturated star is clipped.
    const double clip = 0.6;
    for (double& sample : box)
    {
        sample = (std::min)(sample, clip);
    }
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);

    const an::PsfFitRequest request{.background = 0.04, .saturationLevel = clip, .convergence = 4u};
    const an::Result<an::PsfFitResult> fit = an::fitStarPsf(execution, box, kRadius, request, scratch);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_GT(fit->report.withheldSamples, 0u);
    EXPECT_EQ(fit->report.fittedSamples + fit->report.withheldSamples,
              static_cast<std::uint64_t>(2u * kRadius + 1u) * (2u * kRadius + 1u));
    // The wings alone still recover the width the clipped core was drawn with.
    EXPECT_NEAR(fit->psf.fullWidthX, sigma * an::kGaussianFullWidthPerSigma, 0.1);
    EXPECT_NEAR(fit->psf.centreX, 10.5, 0.05);
}

TEST(PsfFitTest, RefusesArgumentsItCannotUse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kRadius = 6u;
    const std::vector<double> box = renderBox(kRadius, 0.01, 0.4, 6.5, 6.5, 1.7, 1.7, 0.0);
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);
    const an::PsfFitRequest good{.background = 0.01, .saturationLevel = 10.0};

    const an::ExecutionContext empty;
    EXPECT_EQ(an::fitStarPsf(empty, box, kRadius, good, scratch).status().code(), an::ErrorCode::NotInitialized);

    an::PsfFitRequest badModel = good;
    badModel.model = static_cast<an::PsfModel>(9);
    EXPECT_EQ(an::fitStarPsf(execution, box, kRadius, badModel, scratch).status().code(),
              an::ErrorCode::InvalidArgument);

    an::PsfFitRequest badConvergence = good;
    badConvergence.convergence = 0u;
    EXPECT_EQ(an::fitStarPsf(execution, box, kRadius, badConvergence, scratch).status().code(),
              an::ErrorCode::OutOfRange);

    an::PsfFitRequest badSaturation = good;
    badSaturation.saturationLevel = good.background;
    EXPECT_EQ(an::fitStarPsf(execution, box, kRadius, badSaturation, scratch).status().code(),
              an::ErrorCode::InvalidArgument);

    an::PsfFitRequest badTolerance = good;
    badTolerance.parameterTolerance = 0.0;
    EXPECT_EQ(an::fitStarPsf(execution, box, kRadius, badTolerance, scratch).status().code(),
              an::ErrorCode::InvalidArgument);

    EXPECT_EQ(an::fitStarPsf(execution, box, kRadius + 1u, good, scratch).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::fitStarPsf(execution, box, kRadius, good, std::span<double>{scratch.data(), scratch.size() - 1u})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::fitStarPsf(execution, box, 0u, good, scratch).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::fitStarPsf(execution, box, an::kMaxPsfBoxRadius + 1u, good, scratch).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST(PsfFitTest, RefusesABoxItCannotFit)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kRadius = 6u;
    std::vector<double> box = renderBox(kRadius, 0.01, 0.4, 6.5, 6.5, 1.7, 1.7, 0.0);
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);

    std::vector<double> poisoned = box;
    poisoned[10] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::fitStarPsf(execution, poisoned, kRadius,
                             an::PsfFitRequest{.background = 0.01, .saturationLevel = 10.0}, scratch)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);

    // A box whose every sample is withheld leaves fewer surviving samples than parameters.
    constexpr std::uint32_t kTinyRadius = 1u;
    const std::vector<double> tiny = renderBox(kTinyRadius, 0.01, 0.4, 1.5, 1.5, 1.7, 1.7, 0.0);
    std::vector<double> tinyScratch(an::psfFitScratchLength(kTinyRadius), 0.0);
    EXPECT_EQ(an::fitStarPsf(execution, tiny, kTinyRadius,
                             an::PsfFitRequest{.background = 0.01, .saturationLevel = 0.02}, tinyScratch)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

TEST(PsfFitTest, ReferenceAndCompiledBackendsAgreeOnEveryModel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
    const an::ExecutionContext compiled = makeExecution(engine, an::ComputeBackend::Cpu);
    // Without this the case is a tautology: a table with no compiled entry would run the scalar
    // authority on both sides and agree perfectly while proving nothing.
    ASSERT_EQ(an::instructionSetOfDispatchedCode(reference), an::InstructionSet::Scalar);
    const an::InstructionSet compiledSet = an::instructionSetOfDispatchedCode(compiled);
    ASSERT_NE(compiledSet, an::InstructionSet::Scalar)
        << "this host and build have no compiled kernel above the scalar floor";
    constexpr std::uint32_t kRadius = 11u;
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);

    std::size_t compared = 0u;
    double worstCentre = 0.0;
    double worstWidth = 0.0;
    for (std::uint32_t index = 0u; index < 12u; ++index)
    {
        const double sigmaMajor = 1.5 + 0.11 * index;
        const double sigmaMinor = sigmaMajor * (0.7 + 0.02 * index);
        const double angle = 0.13 * index;
        const std::vector<double> box =
            renderBox(kRadius, 0.02 + 0.001 * index, 0.3 + 0.04 * index, 11.2 + 0.05 * index, 11.7 - 0.04 * index,
                      sigmaMajor, sigmaMinor, angle);
        for (const an::PsfModel model :
             {an::PsfModel::Gaussian, an::PsfModel::MoffatFixedBeta, an::PsfModel::MoffatFreeBeta})
        {
            const an::PsfFitRequest request{.model = model,
                                            .fixedBeta = 2.0,
                                            .background = 0.02 + 0.001 * index,
                                            .saturationLevel = 10.0,
                                            .convergence = 4u};
            const an::Result<an::PsfFitResult> left = an::fitStarPsf(reference, box, kRadius, request, scratch);
            const an::Result<an::PsfFitResult> right = an::fitStarPsf(compiled, box, kRadius, request, scratch);
            ASSERT_EQ(left.hasValue(), right.hasValue()) << an::psfModelName(model);
            if (!left.hasValue())
            {
                continue;
            }
            ++compared;
            worstCentre = (std::max)(worstCentre, std::abs(left->psf.centreX - right->psf.centreX));
            worstCentre = (std::max)(worstCentre, std::abs(left->psf.centreY - right->psf.centreY));
            worstWidth = (std::max)(worstWidth, std::abs(left->psf.fullWidthX - right->psf.fullWidthX));
            worstWidth = (std::max)(worstWidth, std::abs(left->psf.fullWidthY - right->psf.fullWidthY));
        }
    }
    EXPECT_EQ(compared, 36u);
    // The compiled evaluation uses a vector exponential and the reference the platform's scalar
    // one, so this is a stated distance rather than an equality. A ten-thousandth of a pixel and a
    // ten-thousandth of a pixel of width are three orders of magnitude below anything a star is
    // measured to.
    EXPECT_LT(worstCentre, 1e-4);
    EXPECT_LT(worstWidth, 1e-4);
    std::cout << "parity psf reference-vs-compiled: compared " << compared << " fits on "
              << an::instructionSetName(compiledSet) << ", worst centre " << worstCentre << " px, worst full width "
              << worstWidth << " px\n";
}

TEST(PsfFitTest, MeasuresEveryPlantedStarOfAField)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = 384u;
    fieldRequest.height = 384u;
    fieldRequest.noise = 0.0;
    const an::test::StarField field = an::test::makeStarField(fieldRequest);
    ASSERT_GE(field.stars.size(), 30u);

    constexpr std::uint32_t kRadius = 10u;
    std::vector<double> scratch(an::psfFitScratchLength(kRadius), 0.0);
    std::vector<double> box(static_cast<std::size_t>(2u * kRadius + 1u) * (2u * kRadius + 1u), 0.0);
    double worstPosition = 0.0;
    double worstWidth = 0.0;
    for (const an::test::PlantedStar& planted : field.stars)
    {
        const auto centreX = static_cast<std::int32_t>(planted.x);
        const auto centreY = static_cast<std::int32_t>(planted.y);
        const std::size_t side = 2u * kRadius + 1u;
        for (std::size_t row = 0u; row < side; ++row)
        {
            for (std::size_t column = 0u; column < side; ++column)
            {
                const std::size_t sourceRow =
                    static_cast<std::size_t>(centreY - static_cast<std::int32_t>(kRadius)) + row;
                const std::size_t sourceColumn =
                    static_cast<std::size_t>(centreX - static_cast<std::int32_t>(kRadius)) + column;
                box[row * side + column] =
                    static_cast<double>(field.samples[sourceRow * fieldRequest.width + sourceColumn]);
            }
        }
        const an::PsfFitRequest request{.background = fieldRequest.background, .saturationLevel = 10.0};
        const an::Result<an::PsfFitResult> fit = an::fitStarPsf(execution, box, kRadius, request, scratch);
        ASSERT_TRUE(fit.hasValue()) << fit.status().message();
        const double fittedX = static_cast<double>(centreX - static_cast<std::int32_t>(kRadius)) + fit->psf.centreX;
        const double fittedY = static_cast<double>(centreY - static_cast<std::int32_t>(kRadius)) + fit->psf.centreY;
        worstPosition = (std::max)(worstPosition, std::hypot(fittedX - planted.x, fittedY - planted.y));
        worstWidth = (std::max)(worstWidth, std::abs(fit->metrics.meanFullWidth - planted.meanFullWidth()) /
                                                planted.meanFullWidth());
    }
    EXPECT_LT(worstPosition, 0.02);
    EXPECT_LT(worstWidth, 0.01);
    std::cout << "psf accuracy on a noiseless planted field: " << field.stars.size() << " stars, worst position "
              << worstPosition << " px, worst relative full width " << worstWidth << '\n';
}
