// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Comet time, coordinate, track and registration-plan tests.

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{

using registration_test::descriptorOf;
using registration_test::layoutOf;
using registration_test::makeEngine;
using registration_test::makeExecution;

[[nodiscard]] constexpr an::UtcInstant atSecond(const std::int64_t second) noexcept
{
    return an::UtcInstant{.nanosecondsSinceEpoch = second * 1000000000LL};
}

[[nodiscard]] an::RegistrationTransform translated(const double x, const double y)
{
    const an::Result<an::RegistrationTransform> made =
        an::RegistrationTransform::create({.linearKind = an::RegistrationTransformKind::Similarity,
                                           .sourceToReference = {1.0, 0.0, x, 0.0, 1.0, y, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::RegistrationTransform{};
}

[[nodiscard]] an::ValidityMaskView<std::uint8_t> outputMask(an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask.view();
    EXPECT_TRUE(view.has_value());
    return *view;
}

} // namespace

TEST(CometContractTest, TrackingAndRegistrationDefaultsAreTheDocumentedPolicy)
{
    const an::CometTrackRequest track;
    EXPECT_EQ(track.modelPolicy, an::CometTrackModelPolicy::Automatic);
    EXPECT_DOUBLE_EQ(track.outlierSigma, 4.0);
    EXPECT_DOUBLE_EQ(track.minimumOutlierResidualPixels, 3.0);
    EXPECT_EQ(track.minimumQuadraticObservations, 6u);
    EXPECT_DOUBLE_EQ(track.minimumQuadraticImprovement, 0.3);
    EXPECT_DOUBLE_EQ(track.highResidualWarningPixels, 5.0);
    EXPECT_DOUBLE_EQ(track.minimumMotionWarningPixels, 1.0);
    EXPECT_FALSE(track.coefficientOrigin.has_value());

    const an::CometRegistrationRequest registration;
    EXPECT_FALSE(registration.retainBaseRegistration);
    EXPECT_TRUE(registration.allowPredictedPositions);

    const an::CometApertureProjectionRequest aperture;
    EXPECT_DOUBLE_EQ(aperture.sourceRadiusPixels, 0.0);
    EXPECT_EQ(aperture.sourceWidth, 0u);
    EXPECT_EQ(aperture.sourceHeight, 0u);
    EXPECT_EQ(aperture.outputWidth, 0u);
    EXPECT_EQ(aperture.outputHeight, 0u);
    EXPECT_EQ(aperture.boundarySamples, 32u);
}

TEST(CometTimeTest, MidpointUsesNanosecondsAndRefusesOverflow)
{
    const an::Result<an::UtcInstant> midpoint =
        an::computeCometObservationMidpoint({.nanosecondsSinceEpoch = 1700000000123456789LL}, 7.25);
    ASSERT_TRUE(midpoint.hasValue()) << midpoint.status().message();
    EXPECT_EQ(midpoint->nanosecondsSinceEpoch, 1700000003748456789LL);

    const an::Result<an::UtcInstant> negative = an::computeCometObservationMidpoint(atSecond(10), -1.0);
    ASSERT_FALSE(negative.hasValue());
    EXPECT_EQ(negative.status().code(), an::ErrorCode::InvalidArgument);
    const an::Result<an::UtcInstant> overflow = an::computeCometObservationMidpoint(
        {.nanosecondsSinceEpoch = (std::numeric_limits<std::int64_t>::max)() - 4LL}, 1.0);
    ASSERT_FALSE(overflow.hasValue());
    EXPECT_EQ(overflow.status().code(), an::ErrorCode::OutOfRange);
}

TEST(CometTrackTest, LargeAbsoluteEpochLinearFitRejectsOneOutlierDeterministically)
{
    const an::UtcInstant origin{.nanosecondsSinceEpoch = 1700000000123456789LL};
    std::vector<an::CometTrackObservation> observations;
    for (std::int64_t i = 0; i < 7; ++i)
    {
        observations.push_back({.frameId = static_cast<std::uint64_t>(i + 1),
                                .midpoint = {.nanosecondsSinceEpoch = origin.nanosecondsSinceEpoch + i * 11000000000LL},
                                .position = {.x = 100.25 + 0.37 * static_cast<double>(i * 11),
                                             .y = 80.75 - 0.19 * static_cast<double>(i * 11)},
                                .accepted = true,
                                .required = true});
    }
    observations[3].position.x += 14.0;
    observations[3].position.y -= 9.0;

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometTrackResult> fit =
        an::fitCometTrack(execution, observations,
                          {.modelPolicy = an::CometTrackModelPolicy::ForceLinear,
                           .outlierSigma = 2.0,
                           .minimumOutlierResidualPixels = 0.5});
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->model, an::CometTrackModel::Linear);
    EXPECT_EQ(fit->observationsUsed, 6u);
    EXPECT_EQ(fit->outliersRejected, 1u);
    EXPECT_LT(fit->rootMeanSquareResidualPixels, 1.0e-8);

    const an::Result<an::PixelCoordinate> evaluated =
        an::evaluateCometTrack(*fit, {.nanosecondsSinceEpoch = origin.nanosecondsSinceEpoch + 55000000000LL});
    ASSERT_TRUE(evaluated.hasValue()) << evaluated.status().message();
    EXPECT_NEAR(evaluated->x, 120.6, 1.0e-8);
    EXPECT_NEAR(evaluated->y, 70.3, 1.0e-8);
}

TEST(CometTrackTest, CallerCanStateAnExplicitCoefficientOrigin)
{
    const an::CometTrackObservation observations[]{
        {.frameId = 1u, .midpoint = atSecond(100), .position = {.x = 25.0, .y = -5.0}},
        {.frameId = 2u, .midpoint = atSecond(110), .position = {.x = 45.0, .y = 5.0}},
        {.frameId = 3u, .midpoint = atSecond(120), .position = {.x = 65.0, .y = 15.0}}};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::CometTrackResult> fit =
        an::fitCometTrack(execution, observations,
                          {.modelPolicy = an::CometTrackModelPolicy::ForceLinear, .coefficientOrigin = atSecond(90)});
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->origin, atSecond(90));
    EXPECT_NEAR(fit->x[0], 5.0, 1.0e-12);
    EXPECT_NEAR(fit->x[1], 2.0, 1.0e-12);
    EXPECT_NEAR(fit->y[0], -15.0, 1.0e-12);
    EXPECT_NEAR(fit->y[1], 1.0, 1.0e-12);
}

TEST(CometTrackTest, QuadraticTrajectoryIsIndependentOfTheRequestedCoefficientOrigin)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    std::vector<an::CometTrackObservation> observations;
    for (std::int64_t second = 0; second < 8; ++second)
    {
        const double time = static_cast<double>(second);
        observations.push_back({.frameId = static_cast<std::uint64_t>(second + 1),
                                .midpoint = atSecond(second),
                                .position = {100. + 7.2 * time + .5184 * time * time, 50. + 14.4 * time}});
    }
    an::CometTrackRequest request;
    request.modelPolicy = an::CometTrackModelPolicy::PreferQuadratic;
    request.minimumOutlierResidualPixels = 1.e6;
    const auto baseline = an::fitCometTrack(execution, observations, request);
    ASSERT_TRUE(baseline.hasValue());
    ASSERT_EQ(baseline->model, an::CometTrackModel::Quadratic);
    for (const std::int64_t origin : {-86400ll, 86400ll})
    {
        request.coefficientOrigin = atSecond(origin);
        const auto shifted = an::fitCometTrack(execution, observations, request);
        ASSERT_TRUE(shifted.hasValue());
        EXPECT_EQ(shifted->model, baseline->model);
        EXPECT_DOUBLE_EQ(shifted->rootMeanSquareResidualPixels, baseline->rootMeanSquareResidualPixels);
        for (const auto& observation : observations)
        {
            const auto predicted = an::evaluateCometTrack(*shifted, observation.midpoint);
            ASSERT_TRUE(predicted.hasValue());
            EXPECT_NEAR(predicted->x, observation.position.x, 2.e-6);
            EXPECT_NEAR(predicted->y, observation.position.y, 2.e-6);
        }
    }
}

TEST(CometTrackTest, QuadraticPolicyAndRequiredObservationsHaveExplicitFailures)
{
    std::vector<an::CometTrackObservation> observations;
    for (std::int64_t i = 0; i < 7; ++i)
    {
        const double t = static_cast<double>(i) * 10.0;
        observations.push_back({.frameId = static_cast<std::uint64_t>(i + 1),
                                .midpoint = atSecond(i * 10),
                                .position = {.x = 5.0 + 0.2 * t + 0.004 * t * t, .y = 9.0 - 0.1 * t + 0.002 * t * t},
                                .accepted = true,
                                .required = true});
    }
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometTrackResult> fit =
        an::fitCometTrack(execution, observations,
                          {.modelPolicy = an::CometTrackModelPolicy::PreferQuadratic,
                           .minimumQuadraticObservations = 6u,
                           .minimumQuadraticImprovement = 0.2});
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->model, an::CometTrackModel::Quadratic);
    EXPECT_NEAR(fit->x[2], 0.004, 1.0e-10);
    EXPECT_NEAR(fit->y[2], 0.002, 1.0e-10);

    const an::Result<an::CometTrackResult> automatic = an::fitCometTrack(execution, observations);
    ASSERT_TRUE(automatic.hasValue()) << automatic.status().message();
    EXPECT_EQ(automatic->model, an::CometTrackModel::Quadratic);
    const an::Result<an::CometTrackResult> forced =
        an::fitCometTrack(execution, observations, {.modelPolicy = an::CometTrackModelPolicy::ForceLinear});
    ASSERT_TRUE(forced.hasValue()) << forced.status().message();
    EXPECT_EQ(forced->model, an::CometTrackModel::Linear);

    observations[2].accepted = false;
    const an::Result<an::CometTrackResult> missing = an::fitCometTrack(execution, observations);
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::InvalidData);
}

TEST(CometTrackTest, AutomaticAndPreferredPoliciesKeepTheirDistinctMeanings)
{
    std::vector<an::CometTrackObservation> observations;
    for (std::int64_t index = 0; index < 6; ++index)
    {
        observations.push_back(
            {.frameId = static_cast<std::uint64_t>(index + 1),
             .midpoint = atSecond(index * 5),
             .position = {.x = 4.0 + static_cast<double>(index), .y = 9.0 - 0.5 * static_cast<double>(index)}});
    }
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::Result<an::CometTrackResult> automatic = an::fitCometTrack(execution, observations);
    ASSERT_TRUE(automatic.hasValue()) << automatic.status().message();
    EXPECT_EQ(automatic->model, an::CometTrackModel::Linear);

    const an::Result<an::CometTrackResult> preferred =
        an::fitCometTrack(execution, observations,
                          {.modelPolicy = an::CometTrackModelPolicy::PreferQuadratic,
                           .highResidualWarningPixels = 0.0,
                           .minimumMotionWarningPixels = 0.0});
    ASSERT_TRUE(preferred.hasValue()) << preferred.status().message();
    EXPECT_EQ(preferred->model, an::CometTrackModel::Quadratic);
    EXPECT_FALSE(preferred->diagnostics.highResidual);
    EXPECT_FALSE(preferred->diagnostics.motionBelowThreshold);

    EXPECT_STREQ(an::cometTrackModelName(an::CometTrackModel::None), "None");
    EXPECT_STREQ(an::cometTrackModelName(an::CometTrackModel::Linear), "Linear");
    EXPECT_STREQ(an::cometTrackModelName(an::CometTrackModel::Quadratic), "Quadratic");
    EXPECT_STREQ(an::cometTrackModelPolicyName(an::CometTrackModelPolicy::Automatic), "Automatic");
    EXPECT_STREQ(an::cometTrackModelPolicyName(an::CometTrackModelPolicy::ForceLinear), "ForceLinear");
    EXPECT_STREQ(an::cometTrackModelPolicyName(an::CometTrackModelPolicy::PreferQuadratic), "PreferQuadratic");
    EXPECT_FALSE(an::isCometTrackModel(static_cast<an::CometTrackModel>(99)));
    EXPECT_FALSE(an::isCometTrackModelPolicy(static_cast<an::CometTrackModelPolicy>(99)));

    const an::Result<an::PixelCoordinate> none = an::evaluateCometTrack({}, atSecond(0));
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CometTrackTest, QualityDiagnosticsReportNoRedundancyLowMotionAndHighResidual)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::CometTrackObservation shortTrack[]{
        {.frameId = 1u, .midpoint = atSecond(0), .position = {.x = 3.0, .y = 4.0}},
        {.frameId = 2u, .midpoint = atSecond(1), .position = {.x = 3.1, .y = 4.0}}};
    const an::Result<an::CometTrackResult> shortFit = an::fitCometTrack(execution, shortTrack);
    ASSERT_TRUE(shortFit.hasValue()) << shortFit.status().message();
    EXPECT_TRUE(shortFit->diagnostics.noRedundancy);
    EXPECT_TRUE(shortFit->diagnostics.motionBelowThreshold);

    const an::CometTrackObservation noisyTrack[]{
        {.frameId = 1u, .midpoint = atSecond(0), .position = {.x = 0.0, .y = 0.0}},
        {.frameId = 2u, .midpoint = atSecond(1), .position = {.x = 8.0, .y = 1.0}},
        {.frameId = 3u, .midpoint = atSecond(2), .position = {.x = 2.0, .y = 2.0}}};
    const an::Result<an::CometTrackResult> noisyFit =
        an::fitCometTrack(execution, noisyTrack,
                          {.modelPolicy = an::CometTrackModelPolicy::ForceLinear,
                           .outlierSigma = 1000.0,
                           .minimumOutlierResidualPixels = 1000.0,
                           .highResidualWarningPixels = 0.1});
    ASSERT_TRUE(noisyFit.hasValue()) << noisyFit.status().message();
    EXPECT_TRUE(noisyFit->diagnostics.highResidual);
}

TEST(CometGeometryTest, SipDiagnosisAndApertureProjectionExposeRoutesAndClipping)
{
    const an::Result<an::CometSipInverseDiagnostic> identity = an::diagnoseCometSipInverse({}, {.x = 3.5, .y = 4.5});
    ASSERT_TRUE(identity.hasValue()) << identity.status().message();
    EXPECT_EQ(identity->route, an::SipInverseRoute::Identity);

    an::SipDistortionRequest sipRequest;
    sipRequest.referencePixel = {.x = 10.5, .y = 10.5};
    sipRequest.forwardX.order = 2u;
    sipRequest.forwardY.order = 2u;
    sipRequest.forwardX.coefficients[2][0] = 0.001;
    sipRequest.forwardY.coefficients[0][2] = -0.0007;
    const an::Result<an::SipDistortion> sip = an::SipDistortion::create(sipRequest);
    ASSERT_TRUE(sip.hasValue()) << sip.status().message();

    const an::Result<an::CometSipInverseDiagnostic> diagnostic =
        an::diagnoseCometSipInverse(*sip, {.x = 18.0, .y = 7.0});
    ASSERT_TRUE(diagnostic.hasValue()) << diagnostic.status().message();
    EXPECT_EQ(diagnostic->route, an::SipInverseRoute::NewtonIteration);
    EXPECT_LT(diagnostic->residualPixels, an::kSipInverseTolerancePixels);

    sipRequest.inverseX.order = 2u;
    sipRequest.inverseY.order = 2u;
    sipRequest.inverseX.coefficients[2][0] = -0.001;
    sipRequest.inverseY.coefficients[0][2] = 0.0007;
    const an::Result<an::SipDistortion> writtenInverse = an::SipDistortion::create(sipRequest);
    ASSERT_TRUE(writtenInverse.hasValue()) << writtenInverse.status().message();
    const an::Result<an::CometSipInverseDiagnostic> coefficientRoute =
        an::diagnoseCometSipInverse(*writtenInverse, {.x = 18.0, .y = 7.0});
    ASSERT_TRUE(coefficientRoute.hasValue()) << coefficientRoute.status().message();
    EXPECT_EQ(coefficientRoute->route, an::SipInverseRoute::InverseCoefficients);

    const an::Result<an::CometSipInverseDiagnostic> nonFinite =
        an::diagnoseCometSipInverse(*writtenInverse, {.x = (std::numeric_limits<double>::quiet_NaN)(), .y = 7.0});
    ASSERT_FALSE(nonFinite.hasValue());
    EXPECT_EQ(nonFinite.status().code(), an::ErrorCode::InvalidArgument);

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometApertureProjection> projection =
        an::projectCometAperture(execution, {.sourceCenter = {.x = 1.0, .y = 1.0},
                                             .sourceRadiusPixels = 4.0,
                                             .sourceWidth = 20u,
                                             .sourceHeight = 18u,
                                             .outputWidth = 20u,
                                             .outputHeight = 18u,
                                             .boundarySamples = 24u,
                                             .sourceToReference = translated(2.0, -1.0),
                                             .sourceDistortion = *sip,
                                             .postTranslation = {.x = 0.5, .y = 2.0}});
    ASSERT_TRUE(projection.hasValue()) << projection.status().message();
    EXPECT_DOUBLE_EQ(projection->sourceCenter.x, 1.0);
    EXPECT_TRUE(std::isfinite(projection->outputCenter.x));
    EXPECT_TRUE(projection->sourceClipped);
    EXPECT_FALSE(projection->sourcePolygon.empty());
    EXPECT_EQ(projection->sourcePolygon.size(), projection->outputPolygon.size());
    for (const an::PixelCoordinate& point : projection->outputPolygon)
    {
        EXPECT_GE(point.x, 0.5);
        EXPECT_LE(point.x, 19.5);
        EXPECT_GE(point.y, 0.5);
        EXPECT_LE(point.y, 17.5);
    }

    const an::Result<an::CometApertureProjection> invalid = an::projectCometAperture(execution, {});
    ASSERT_FALSE(invalid.hasValue());
    EXPECT_EQ(invalid.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CometRegistrationTest, ThreeCoordinateSpacesProduceOneTrackAndPerFrameTranslations)
{
    const an::CometRegistrationFrame frames[]{
        {.frameId = 10u,
         .midpoint = atSecond(0),
         .width = 80u,
         .height = 60u,
         .sourceToReference = translated(5.0, -2.0),
         .observation = an::CometRegistrationObservation{.position = {.x = 15.0, .y = 32.0},
                                                         .space = an::CometCoordinateSpace::SourcePixels}},
        {.frameId = 11u,
         .midpoint = atSecond(10),
         .width = 80u,
         .height = 60u,
         .sourceToReference = translated(-3.0, 4.0),
         .observation =
             an::CometRegistrationObservation{.position = {.x = 25.0, .y = 27.0},
                                              .space = an::CometCoordinateSpace::DistortionCorrectedSourcePixels}},
        {.frameId = 12u,
         .midpoint = atSecond(20),
         .width = 80u,
         .height = 60u,
         .sourceToReference = translated(1.0, 1.0),
         .observation = an::CometRegistrationObservation{.position = {.x = 24.0, .y = 32.0},
                                                         .space = an::CometCoordinateSpace::ReferencePixels}},
        {.frameId = 13u,
         .midpoint = atSecond(30),
         .width = 80u,
         .height = 60u,
         .sourceToReference = translated(0.25, 0.0),
         .observation = std::nullopt}};

    const an::Result<an::CometRegistrationPlan> plan =
        an::planCometRegistration(frames, 11u,
                                  {.track = {.modelPolicy = an::CometTrackModelPolicy::ForceLinear},
                                   .retainBaseRegistration = true,
                                   .allowPredictedPositions = true});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->trackObservations.size(), 3u);
    ASSERT_TRUE(plan->request.track.coefficientOrigin.has_value());
    EXPECT_EQ(*plan->request.track.coefficientOrigin, atSecond(10));
    EXPECT_DOUBLE_EQ(plan->trackObservations[0].position.x, 20.0);
    EXPECT_DOUBLE_EQ(plan->trackObservations[1].position.x, 22.0);
    EXPECT_DOUBLE_EQ(plan->trackObservations[2].position.x, 24.0);

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometRegistrationResult> result = an::executeCometRegistration(execution, *plan);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->track.origin, atSecond(10));
    ASSERT_EQ(result->frames.size(), 4u);
    EXPECT_EQ(result->referenceFrameId, 11u);
    EXPECT_NEAR(result->referencePosition.x, 22.0, 1.0e-9);
    EXPECT_EQ(result->frames[0].width, 80u);
    EXPECT_EQ(result->frames[0].height, 60u);
    EXPECT_NEAR(result->frames[0].postTranslation.x, 2.0, 1.0e-9);
    EXPECT_NEAR(result->frames[0].postTranslation.y, 1.0, 1.0e-9);
    EXPECT_NEAR(result->frames[1].postTranslation.x, 0.0, 1.0e-9);
    EXPECT_NEAR(result->frames[2].postTranslation.x, -2.0, 1.0e-9);
    EXPECT_TRUE(result->frames[3].predictionOnly);
    EXPECT_NEAR(result->frames[3].postTranslation.x, -4.0, 1.0e-9);

    const an::Result<an::CometRegistrationPlan> cometOnly =
        an::planCometRegistration(frames, 11u,
                                  {.track = {.modelPolicy = an::CometTrackModelPolicy::ForceLinear},
                                   .retainBaseRegistration = false,
                                   .allowPredictedPositions = true});
    ASSERT_TRUE(cometOnly.hasValue()) << cometOnly.status().message();
    EXPECT_EQ(cometOnly->frames[0].sourceToReference.matrix(), an::RegistrationTransform{}.matrix());
}

TEST(CometRegistrationTest, EveryTransformUsesTheFittedTrajectoryAndReportsItsConstraint)
{
    const an::CometRegistrationFrame frames[]{
        {.frameId = 1u,
         .midpoint = atSecond(0),
         .width = 32u,
         .height = 24u,
         .observation = an::CometRegistrationObservation{.position = {.x = 10.0, .y = 7.0},
                                                         .space = an::CometCoordinateSpace::ReferencePixels}},
        {.frameId = 2u,
         .midpoint = atSecond(10),
         .width = 32u,
         .height = 24u,
         .observation = an::CometRegistrationObservation{.position = {.x = 12.8, .y = 8.0},
                                                         .space = an::CometCoordinateSpace::ReferencePixels}},
        {.frameId = 3u,
         .midpoint = atSecond(20),
         .width = 32u,
         .height = 24u,
         .observation = an::CometRegistrationObservation{.position = {.x = 14.0, .y = 9.0},
                                                         .space = an::CometCoordinateSpace::ReferencePixels}},
        {.frameId = 4u, .midpoint = atSecond(30), .width = 32u, .height = 24u, .observation = std::nullopt}};

    const an::Result<an::CometRegistrationPlan> plan =
        an::planCometRegistration(frames, 2u, {.track = {.modelPolicy = an::CometTrackModelPolicy::ForceLinear}});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::CometRegistrationResult> result = an::executeCometRegistration(execution, *plan);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_EQ(result->frames.size(), 4u);
    for (std::size_t index = 0u; index < result->frames.size(); ++index)
    {
        const an::Result<an::PixelCoordinate> fitted =
            an::evaluateCometTrack(result->track, result->frames[index].midpoint);
        ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
        EXPECT_DOUBLE_EQ(result->frames[index].trackPositionInReference.x, fitted->x);
        EXPECT_DOUBLE_EQ(result->frames[index].trackPositionInReference.y, fitted->y);
        EXPECT_EQ(result->frames[index].observationConstrained, index < 3u);
        EXPECT_EQ(result->frames[index].predictionOnly, index == 3u);
    }
    EXPECT_NE(result->frames[1].trackPositionInReference.x, frames[1].observation->position.x);
}

TEST(CometRegistrationTest, ACometFramePlansAndExecutesThroughTheGeometryWarp)
{
    const an::ImageDescriptor descriptor =
        descriptorOf(4u, 3u, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::ImageLayout layout = layoutOf(descriptor);
    const std::vector<float> source{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F};
    std::vector<float> destination(source.size(), -1.0F);
    const an::CometRegistrationFrameResult frame{.frameId = 4u,
                                                 .midpoint = atSecond(0),
                                                 .width = 4u,
                                                 .height = 3u,
                                                 .sourceToReference = translated(0.25, 0.0),
                                                 .sourceDistortion = {},
                                                 .trackPositionInReference = {.x = 2.0, .y = 1.5},
                                                 .postTranslation = {.x = 0.25, .y = 0.0},
                                                 .observationConstrained = true,
                                                 .predictionOnly = false};
    const an::Result<an::RegistrationWarpPlan> plan =
        an::planCometRegistrationWarp(descriptor, frame,
                                      {.width = 4u,
                                       .height = 3u,
                                       .canvasOrigin = {},
                                       .sampling = {.interpolation = an::KernelInterpolation::Nearest,
                                                    .border = an::KernelBorderMode::Constant,
                                                    .invalid = an::SamplingInvalidPolicy::RequireAll}});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->route(), an::RegistrationSamplingRoute::Warp);
    EXPECT_FALSE(plan->needsCoordinateMap());
    EXPECT_DOUBLE_EQ(plan->request().canvasOrigin.x, 0.25);

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    an::Result<an::ValidityMask> maskMade = an::ValidityMask::create(engine, descriptor.extent());
    ASSERT_TRUE(maskMade.hasValue()) << maskMade.status().message();
    an::ValidityMask mask = std::move(*maskMade);
    const an::Result<an::RegistrationWarpResult> warped =
        an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout,
                                *plan, {}, outputMask(mask));
    ASSERT_TRUE(warped.hasValue()) << warped.status().message();
    EXPECT_EQ(warped->route, an::RegistrationSamplingRoute::Warp);
    EXPECT_GT(warped->run.outputsWritten, 0u);

    const an::ImageDescriptor wrongDescriptor =
        descriptorOf(5u, 3u, 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear);
    const an::Result<an::RegistrationWarpPlan> wrongShape =
        an::planCometRegistrationWarp(wrongDescriptor, frame,
                                      {.width = 4u,
                                       .height = 3u,
                                       .sampling = {.interpolation = an::KernelInterpolation::Nearest,
                                                    .border = an::KernelBorderMode::Constant,
                                                    .invalid = an::SamplingInvalidPolicy::RequireAll}});
    ASSERT_FALSE(wrongShape.hasValue());
    EXPECT_EQ(wrongShape.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(CometRegistrationTest, RequiredMissingInputsAndCancellationAreReturned)
{
    const an::CometRegistrationFrame missing[]{{.frameId = 1u,
                                                .midpoint = std::nullopt,
                                                .width = 10u,
                                                .height = 10u,
                                                .observation = std::nullopt,
                                                .required = true}};
    const an::Result<an::CometRegistrationPlan> refused = an::planCometRegistration(missing, 1u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);

    const an::CometTrackObservation observations[]{
        {.frameId = 1u, .midpoint = atSecond(0), .position = {.x = 1.0, .y = 1.0}},
        {.frameId = 2u, .midpoint = atSecond(1), .position = {.x = 2.0, .y = 2.0}}};
    an::EngineContext engine = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext execution = makeExecution(engine, 2u, stop.get_token());
    const an::Result<an::CometTrackResult> cancelled = an::fitCometTrack(execution, observations);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);

    const an::Result<an::CometApertureProjection> aperture =
        an::projectCometAperture(execution, {.sourceCenter = {.x = 5.5, .y = 5.5},
                                             .sourceRadiusPixels = 2.0,
                                             .sourceWidth = 12u,
                                             .sourceHeight = 12u,
                                             .outputWidth = 12u,
                                             .outputHeight = 12u,
                                             .boundarySamples = 16u,
                                             .sourceToReference = translated(0.0, 0.0)});
    ASSERT_FALSE(aperture.hasValue());
    EXPECT_EQ(aperture.status().code(), an::ErrorCode::Cancelled);

    const an::CometRegistrationFrame validFrames[]{
        {.frameId = 1u,
         .midpoint = atSecond(0),
         .width = 10u,
         .height = 10u,
         .observation = an::CometRegistrationObservation{.position = {.x = 2.5, .y = 2.5},
                                                         .space = an::CometCoordinateSpace::ReferencePixels}},
        {.frameId = 2u,
         .midpoint = atSecond(1),
         .width = 10u,
         .height = 10u,
         .observation = an::CometRegistrationObservation{.position = {.x = 3.5, .y = 3.5},
                                                         .space = an::CometCoordinateSpace::ReferencePixels}}};
    const an::Result<an::CometRegistrationPlan> validPlan = an::planCometRegistration(validFrames, 1u);
    ASSERT_TRUE(validPlan.hasValue()) << validPlan.status().message();
    an::CometRegistrationPlan modified = *validPlan;
    modified.trackObservations.front().position.x += 1.0;
    an::ExecutionContext ready = makeExecution(engine);
    const an::Result<an::CometRegistrationResult> rejectedMutation = an::executeCometRegistration(ready, modified);
    ASSERT_FALSE(rejectedMutation.hasValue());
    EXPECT_EQ(rejectedMutation.status().code(), an::ErrorCode::InvalidArgument);

    modified = *validPlan;
    modified.frames.resize(static_cast<std::size_t>(an::kMaximumCometTrackObservations) + 1u);
    const an::Result<an::CometRegistrationResult> rejectedExpansion = an::executeCometRegistration(ready, modified);
    ASSERT_FALSE(rejectedExpansion.hasValue());
    EXPECT_EQ(rejectedExpansion.status().code(), an::ErrorCode::LimitExceeded);

    const an::Result<an::CometRegistrationResult> cancelledExecution =
        an::executeCometRegistration(execution, *validPlan);
    ASSERT_FALSE(cancelledExecution.hasValue());
    EXPECT_EQ(cancelledExecution.status().code(), an::ErrorCode::Cancelled);

    std::vector<an::CometTrackObservation> tooManyObservations(
        static_cast<std::size_t>(an::kMaximumCometTrackObservations) + 1u);
    const an::Result<an::CometTrackResult> observationLimit = an::fitCometTrack(ready, tooManyObservations);
    ASSERT_FALSE(observationLimit.hasValue());
    EXPECT_EQ(observationLimit.status().code(), an::ErrorCode::LimitExceeded);

    std::vector<an::CometRegistrationFrame> tooManyFrames(static_cast<std::size_t>(an::kMaximumCometTrackObservations) +
                                                          1u);
    const an::Result<an::CometRegistrationPlan> planLimit = an::planCometRegistration(tooManyFrames, 0u);
    ASSERT_FALSE(planLimit.hasValue());
    EXPECT_EQ(planLimit.status().code(), an::ErrorCode::LimitExceeded);

    const an::Result<an::CometApertureProjection> apertureLimit =
        an::projectCometAperture(ready, {.sourceCenter = {.x = 5.5, .y = 5.5},
                                         .sourceRadiusPixels = 2.0,
                                         .sourceWidth = 12u,
                                         .sourceHeight = 12u,
                                         .outputWidth = 12u,
                                         .outputHeight = 12u,
                                         .boundarySamples = an::kMaximumCometApertureBoundarySamples + 1u});
    ASSERT_FALSE(apertureLimit.hasValue());
    EXPECT_EQ(apertureLimit.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CometTrackTest, QuadraticMotionIsNotRejectedAsALinearOutlier)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const auto policy : {an::CometTrackModelPolicy::Automatic, an::CometTrackModelPolicy::PreferQuadratic})
    {
        for (const std::uint32_t count : {3u, 6u, 8u})
        {
            std::vector<an::CometTrackObservation> observations;
            for (std::uint32_t i = 0u; i < count; ++i)
            {
                const auto second = static_cast<std::int64_t>(i * i);
                const double t = static_cast<double>(second);
                observations.push_back(
                    {.frameId = i + 1u, .midpoint = atSecond(second), .position = {10.0 * t * t, 5.0 * t * t}});
            }
            const auto fit = an::fitCometTrack(execution, observations,
                                               {.modelPolicy = policy, .minimumQuadraticObservations = count});
            ASSERT_TRUE(fit.hasValue()) << fit.status().message();
            EXPECT_EQ(fit->model, an::CometTrackModel::Quadratic);
            EXPECT_EQ(fit->observationsUsed, count);
            EXPECT_EQ(fit->outliersRejected, 0u);
            const auto predicted = an::evaluateCometTrack(*fit, atSecond(3));
            ASSERT_TRUE(predicted.hasValue());
            EXPECT_NEAR(predicted->x, 90.0, 1.0e-7);
            EXPECT_NEAR(predicted->y, 45.0, 1.0e-7);
        }
    }
}

TEST(CometTrackTest, OutliersCanReduceTheSetBelowTheQuadraticSelectionThreshold)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const auto policy : {an::CometTrackModelPolicy::Automatic, an::CometTrackModelPolicy::PreferQuadratic})
    {
        std::vector<an::CometTrackObservation> observations;
        for (std::int64_t i = 0; i < 6; ++i)
        {
            observations.push_back({.frameId = static_cast<std::uint64_t>(i + 1),
                                    .midpoint = atSecond(i),
                                    .position = {10.0 + i * 2.0, 20.0 + i * 3.0}});
        }
        observations[2].position.x += 40.0;
        observations[2].position.y -= 25.0;
        const auto fit = an::fitCometTrack(execution, observations, {.modelPolicy = policy});
        ASSERT_TRUE(fit.hasValue()) << fit.status().message();
        EXPECT_EQ(fit->model, an::CometTrackModel::Linear);
        EXPECT_EQ(fit->observationsUsed, 5u);
        EXPECT_EQ(fit->outliersRejected, 1u);
        const auto predicted = an::evaluateCometTrack(*fit, atSecond(2));
        ASSERT_TRUE(predicted.hasValue());
        EXPECT_NEAR(predicted->x, 14.0, 1.0e-9);
        EXPECT_NEAR(predicted->y, 26.0, 1.0e-9);
    }
}
