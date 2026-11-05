// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePlanner.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>

#include <array>
#include <chrono>
#include <limits>
#include <stop_token>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace
{

an::ImageDescriptor drizzleDescriptor(std::uint32_t width = 17u, std::uint32_t height = 13u)
{
    const auto extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    return *an::ImageDescriptor::create({.extent = *extent,
                                         .sampleType = an::SampleType::Float32,
                                         .channelSchema = an::ChannelSchema::Mono,
                                         .colorSpace = an::ColorSpace::Unspecified,
                                         .numericDomain = an::NumericDomain::NormalizedLinear,
                                         .cfa = {},
                                         .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                         .pixelAspectRatio = 1.0});
}

TEST(DrizzlePlanTest, DefaultPlanAccountsForScaledOutputAndDeterministicAccumulation)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    ASSERT_TRUE(engine.hasValue());
    auto execution = an::ExecutionContext::create(*engine, {});
    ASSERT_TRUE(execution.hasValue());
    const auto model = an::DrizzleModel::create({.input = drizzleDescriptor(), .frameCount = 5u});
    ASSERT_TRUE(model.hasValue()) << model.status().message();
    const auto plan = an::planDrizzle(*execution, *model, {});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(model->outputDescriptor().extent().width(), 34u);
    EXPECT_EQ(model->outputDescriptor().extent().height(), 26u);
    EXPECT_EQ(plan->memory.outputImageBytes, 34u * 26u * sizeof(float));
    EXPECT_EQ(plan->memory.accumulatorBytes, 34u * 26u * 2u * sizeof(double));
    EXPECT_EQ(plan->destinationRows, 26u);
    EXPECT_EQ(plan->destinationTileCount, 1u);
    EXPECT_EQ(plan->workers, 2u);
    EXPECT_EQ(plan->io.inputPasses, 1u);
    EXPECT_EQ(plan->io.logicalInputBytes, 17u * 13u * 5u * sizeof(float));
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
}

an::DrizzleRequest drizzleRequest()
{
    return {.input = drizzleDescriptor(), .frameCount = 5u};
}

std::optional<std::uint64_t> countField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() != nullptr)
        for (const auto& field : status.detail()->context)
            if (field.key == key && field.kind == an::ContextValueKind::Count)
                return field.count;
    return std::nullopt;
}

class DrizzleKernelPlanTest : public testing::TestWithParam<an::DrizzleKernel>
{
};

TEST_P(DrizzleKernelPlanTest, RetainsChoiceAndPlansOnlyItsHaloAndTable)
{
    auto request = drizzleRequest();
    request.parameters.kernel = GetParam();
    request.input = drizzleDescriptor(19u, 101u);
    const auto model = an::DrizzleModel::create(request);
    ASSERT_TRUE(model.hasValue());
    EXPECT_EQ(model->request().parameters.kernel, GetParam());
    EXPECT_STRNE(an::drizzleKernelName(GetParam()), "UnknownDrizzleKernel");
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto plan = an::planDrizzle(*execution, *model, {});
    ASSERT_TRUE(plan.hasValue());
    const bool square = GetParam() == an::DrizzleKernel::Square;
    EXPECT_EQ(plan->mapHaloRows, square ? 1u : 0u);
    EXPECT_EQ(plan->mapRows, square ? 34u : 32u);
    EXPECT_EQ(plan->memory.coordinateMapBytes, (19u + 1u) * (square ? 34u : 32u) * 8u);
    const bool lanczos = GetParam() == an::DrizzleKernel::Lanczos2 || GetParam() == an::DrizzleKernel::Lanczos3;
    EXPECT_EQ(plan->memory.kernelTableBytes, lanczos ? sizeof(an::DrizzleKernelTable) : 0u);
}

INSTANTIATE_TEST_SUITE_P(AllKernels, DrizzleKernelPlanTest,
                         testing::Values(an::DrizzleKernel::Square, an::DrizzleKernel::Point, an::DrizzleKernel::Turbo,
                                         an::DrizzleKernel::Gaussian, an::DrizzleKernel::Lanczos2,
                                         an::DrizzleKernel::Lanczos3));

TEST(DrizzleModelTest, RejectsUnknownModesAndFrameCounts)
{
    auto request = drizzleRequest();
    request.parameters.kernel = static_cast<an::DrizzleKernel>(255u);
    EXPECT_FALSE(an::isDrizzleKernel(request.parameters.kernel));
    EXPECT_STREQ(an::drizzleKernelName(request.parameters.kernel), "UnknownDrizzleKernel");
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    request = drizzleRequest();
    request.parameters.nonFinite = static_cast<an::DrizzleNonFinitePolicy>(255u);
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    for (const auto count : {0u, an::kMaxDrizzleFrames + 1u})
    {
        request = drizzleRequest();
        request.frameCount = count;
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    }
    request = drizzleRequest();
    request.frameCount = an::kMaxDrizzleFrames;
    EXPECT_TRUE(an::DrizzleModel::create(request).hasValue());
}

TEST(DrizzleModelTest, RefusesNonFiniteAndOutOfRangeScaleDropAndWeights)
{
    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    const double infinity = (std::numeric_limits<double>::infinity)();
    for (const double scale : {0.0, 0.999, 4.001, nan, infinity, -infinity})
    {
        auto request = drizzleRequest();
        request.parameters.scale = scale;
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    }
    for (const double drop : {0.0, -0.1, 1.001, nan, infinity})
    {
        auto request = drizzleRequest();
        request.parameters.pixelFraction = drop;
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    }
    for (const double weight : {0.0, -1.0, nan, infinity})
    {
        auto request = drizzleRequest();
        request.parameters.weightScale = weight;
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
        request = drizzleRequest();
        request.parameters.channelWeights[0] = weight;
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    }
}

TEST(DrizzleModelTest, RejectsOverflowingAndUnderflowingCombinedWeights)
{
    auto request = drizzleRequest();
    request.parameters.weightScale = (std::numeric_limits<double>::max)();
    request.parameters.channelWeights[0] = 2.0;
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    request.parameters.weightScale = (std::numeric_limits<double>::min)();
    request.parameters.channelWeights[0] = (std::numeric_limits<double>::min)();
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DrizzleModelTest, UnusedChannelWeightsCannotBeSilentlyIgnored)
{
    auto request = drizzleRequest();
    request.parameters.channelWeights[2] = 2.0;
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DrizzleModelTest, FractionalScaleRoundsCanvasUpAndExplicitCanvasIsAuthoritative)
{
    auto request = drizzleRequest();
    request.parameters.scale = 1.5;
    request.parameters.pixelFraction = 1.0;
    const auto scaled = an::DrizzleModel::create(request);
    ASSERT_TRUE(scaled.hasValue());
    EXPECT_EQ(scaled->outputDescriptor().extent().width(), 26u);
    EXPECT_EQ(scaled->outputDescriptor().extent().height(), 20u);
    request.output.extent = an::ImageExtent::create(3u, 7u, 1u, an::defaultEngineLimits());
    const auto cropped = an::DrizzleModel::create(request);
    ASSERT_TRUE(cropped.hasValue());
    EXPECT_EQ(cropped->outputDescriptor().extent(), *request.output.extent);
    request.output.extent = an::ImageExtent::create(3u, 7u, 3u, an::defaultEngineLimits());
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DrizzleModelTest, SupportsAllResidentSampleTypesAndBothFloatingOutputs)
{
    for (const auto type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        auto description = drizzleDescriptor().toRequest();
        description.sampleType = type;
        auto request = drizzleRequest();
        request.input = *an::ImageDescriptor::create(description);
        for (const auto output : {an::SampleType::Float32, an::SampleType::Float64})
        {
            request.output.sampleType = output;
            const auto model = an::DrizzleModel::create(request);
            ASSERT_TRUE(model.hasValue());
            EXPECT_EQ(model->outputDescriptor().sampleType(), output);
            EXPECT_EQ(model->outputDescriptor().numericDomain(), description.numericDomain);
        }
    }
    for (const auto type : {an::SampleType::UInt8, an::SampleType::Int16, static_cast<an::SampleType>(255u)})
    {
        auto request = drizzleRequest();
        request.output.sampleType = type;
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    }
}

TEST(DrizzleModelTest, AllBayerPatternsAndCropPhasesSurviveIntoTheModel)
{
    for (const auto pattern : {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 2u; ++x)
            {
                auto description = drizzleDescriptor().toRequest();
                description.cfa = *an::CfaDescriptor::create(pattern, x, y);
                description.numericDomain = an::NumericDomain::RawSensorCode;
                auto request = drizzleRequest();
                request.input = *an::ImageDescriptor::create(description);
                const auto model = an::DrizzleModel::create(request);
                ASSERT_TRUE(model.hasValue());
                EXPECT_TRUE(model->usesCfa());
                EXPECT_EQ(model->request().input.cfa(), description.cfa);
                EXPECT_EQ(model->outputDescriptor().extent().channelCount(), 3u);
                EXPECT_EQ(model->outputDescriptor().channelSchema(), an::ChannelSchema::Rgb);
                EXPECT_EQ(model->outputDescriptor().cfa().pattern(), an::CfaPattern::None);
                EXPECT_EQ(model->outputDescriptor().numericDomain(), an::NumericDomain::RawSensorCode);
                EXPECT_EQ(model->outputDescriptor().colorSpace(), an::ColorSpace::Unspecified);
            }
}

TEST(DrizzleModelTest, AllValidityRulesRequireExactlyTheirDeclaredMask)
{
    for (const auto rule : {an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly, an::ValidityRule::ZeroIsNoData,
                            an::ValidityRule::ExplicitMask, an::ValidityRule::DomainRange})
    {
        auto request = drizzleRequest();
        an::ValidityDescriptorRequest validity{.rule = rule};
        if (rule == an::ValidityRule::DomainRange)
        {
            validity.lowestValid = -1.0;
            validity.highestValid = 2.0;
        }
        request.validity = *an::ValidityDescriptor::create(validity);
        if (rule == an::ValidityRule::ExplicitMask)
            request.maskExtent = request.input.extent();
        ASSERT_TRUE(an::DrizzleModel::create(request).hasValue());
        if (request.maskExtent.has_value())
            request.maskExtent.reset();
        else
            request.maskExtent = request.input.extent();
        EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
    }
    auto request = drizzleRequest();
    request.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    request.maskExtent = an::ImageExtent::create(16u, 13u, 1u, an::defaultEngineLimits());
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DrizzleModelTest, AuxiliaryWeightsHaveExplicitCompatibleMeaningAndGeometry)
{
    auto request = drizzleRequest();
    request.pixelWeights = drizzleDescriptor();
    request.flatWeights = drizzleDescriptor();
    EXPECT_TRUE(an::DrizzleModel::create(request).hasValue());
    request.pixelWeights = drizzleDescriptor(18u, 13u);
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
    request.pixelWeights.reset();
    auto wrong = drizzleDescriptor().toRequest();
    wrong.sampleType = an::SampleType::UInt16;
    request.flatWeights = *an::ImageDescriptor::create(wrong);
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
    wrong = drizzleDescriptor().toRequest();
    wrong.numericDomain = an::NumericDomain::RawSensorCode;
    request.flatWeights = *an::ImageDescriptor::create(wrong);
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DrizzleModelTest, RgbWeightsMayBroadcastOrNameEachChannelAndRgbaKeepsAlpha)
{
    auto description = drizzleDescriptor().toRequest();
    description.extent = *an::ImageExtent::create(17u, 13u, 3u, an::defaultEngineLimits());
    description.channelSchema = an::ChannelSchema::Rgb;
    description.colorSpace = an::ColorSpace::LinearRgb;
    auto request = drizzleRequest();
    request.input = *an::ImageDescriptor::create(description);
    request.parameters.channelWeights = {0.5, 1.5, 2.0};
    request.parameters.nonFinite = an::DrizzleNonFinitePolicy::Exclude;
    request.output.sampleType = an::SampleType::Float64;
    request.pixelWeights = drizzleDescriptor();
    auto model = an::DrizzleModel::create(request);
    ASSERT_TRUE(model.hasValue());
    EXPECT_FALSE(model->usesCfa());
    EXPECT_EQ(model->outputDescriptor().colorSpace(), an::ColorSpace::LinearRgb);
    description.colorSpace = an::ColorSpace::Unspecified;
    request.pixelWeights = *an::ImageDescriptor::create(description);
    EXPECT_TRUE(an::DrizzleModel::create(request).hasValue());
    description.extent = *an::ImageExtent::create(17u, 13u, 4u, an::defaultEngineLimits());
    description.channelSchema = an::ChannelSchema::Rgba;
    request.input = *an::ImageDescriptor::create(description);
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
    request.pixelWeights = drizzleDescriptor();
    model = an::DrizzleModel::create(request);
    ASSERT_TRUE(model.hasValue());
    EXPECT_EQ(model->outputDescriptor().extent().channelCount(), 4u);
    EXPECT_EQ(model->outputDescriptor().channelSchema(), an::ChannelSchema::Rgba);
}

TEST(DrizzlePlanTest, CfaSourceMaskAndOutputMaskAccountTheirDifferentPlaneCounts)
{
    auto description = drizzleDescriptor(1u, 1u).toRequest();
    description.cfa = *an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 1u);
    an::DrizzleRequest request{.input = *an::ImageDescriptor::create(description)};
    request.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    request.maskExtent = request.input.extent();
    request.output.sampleType = an::SampleType::Float64;
    const auto model = an::DrizzleModel::create(request);
    ASSERT_TRUE(model.hasValue());
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto plan = an::planDrizzle(*execution, *model, {});
    ASSERT_TRUE(plan.hasValue());
    EXPECT_EQ(plan->memory.sourceBufferBytes, 5u);
    EXPECT_EQ(plan->memory.outputMaskBytes, 6u);
    EXPECT_EQ(plan->memory.outputImageBytes, 2u * 2u * 3u * 8u);
    EXPECT_EQ(plan->memory.accumulatorBytes, 2u * 2u * 3u * 16u);
    EXPECT_EQ(plan->mapRows, 1u);
    EXPECT_EQ(plan->memory.coordinateMapBytes, 16u);
}

TEST(DrizzleModelTest, RegeneratesValidityAndWithdrawsSourceGeometryMetadata)
{
    const auto effect = an::drizzleOperationEffect();
    EXPECT_EQ(effect.validity.policy, an::ValidityPolicy::Regenerate);
    EXPECT_EQ(effect.validity.produced.rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(effect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(effect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(effect.solution, an::SolutionEffect::Invalidates);
    EXPECT_EQ(effect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(effect.outputIntent, an::OutputIntentEffect::Withdraws);
}

TEST(DrizzleModelTest, HonorsDimensionAndByteLimitsBeforeAnyAllocation)
{
    const auto request = drizzleRequest();
    auto limits = an::defaultEngineLimits();
    limits.maxImageWidth = 33u;
    EXPECT_EQ(an::DrizzleModel::create(request, limits).status().code(), an::ErrorCode::LimitExceeded);
    limits = an::defaultEngineLimits();
    limits.maxImageBytes = 34u * 26u * sizeof(float) - 1u;
    EXPECT_EQ(an::DrizzleModel::create(request, limits).status().code(), an::ErrorCode::LimitExceeded);
    ++limits.maxImageBytes;
    EXPECT_TRUE(an::DrizzleModel::create(request, limits).hasValue());
}

TEST(DrizzleMapDescriptorTest, OwnsPolynomialAndSipAndNamesSamplingCoordinates)
{
    auto request = drizzleRequest();
    request.parameters.scale = 1.5;
    const auto model = an::DrizzleModel::create(request);
    an::RegistrationTransformRequest transform;
    transform.polynomial.order = 3u;
    transform.polynomial.x[1] = 1.0;
    transform.polynomial.y[2] = 1.0;
    transform.polynomial.x[3] = 0.001;
    auto made = an::RegistrationTransform::create(transform);
    ASSERT_TRUE(made.hasValue());
    an::SipDistortionRequest sip;
    sip.referencePixel = {8.5, 6.5};
    sip.forwardX.order = 2u;
    sip.forwardX.coefficients[2][0] = 1.0e-5;
    auto distortion = an::SipDistortion::create(sip);
    ASSERT_TRUE(distortion.hasValue());
    an::DrizzleMappingRequest mapping{
        .transform = *made, .postTranslation = {-3.25, 4.5}, .sourceDistortion = *distortion};
    const auto descriptor = an::DrizzleMapDescriptor::create(*model, mapping);
    ASSERT_TRUE(descriptor.hasValue());
    mapping = {};
    EXPECT_EQ(descriptor->mapping().transform.kind(), an::RegistrationTransformKind::Polynomial3);
    EXPECT_EQ(descriptor->mapping().transform.polynomial(), transform.polynomial);
    EXPECT_EQ(descriptor->mapping().sourceDistortion, *distortion);
    EXPECT_EQ(descriptor->mapping().postTranslation, (an::PixelCoordinate{-3.25, 4.5}));
    EXPECT_EQ(descriptor->sourceExtent(), model->request().input.extent());
    EXPECT_EQ(descriptor->outputExtent(), model->outputDescriptor().extent());
    EXPECT_DOUBLE_EQ(descriptor->scale(), 1.5);
    EXPECT_EQ(descriptor->coordinateConvention(), an::DrizzleCoordinateConvention::IntegerPixelCenters);
    EXPECT_EQ(descriptor->invalidCoordinate(), an::DrizzleInvalidCoordinate::PairedNonFinite);
}

TEST(DrizzleMapDescriptorTest, RefusesNonFiniteTranslationAndInverseOnlySip)
{
    const auto model = an::DrizzleModel::create(drizzleRequest());
    for (const double value : {(std::numeric_limits<double>::infinity)(), (std::numeric_limits<double>::quiet_NaN)()})
    {
        EXPECT_EQ(an::DrizzleMapDescriptor::create(*model, {.postTranslation = {value, 0.0}}).status().code(),
                  an::ErrorCode::InvalidArgument);
        EXPECT_EQ(an::DrizzleMapDescriptor::create(*model, {.postTranslation = {0.0, value}}).status().code(),
                  an::ErrorCode::InvalidArgument);
    }
    an::SipDistortionRequest sip;
    sip.inverseX.order = 2u;
    const auto inverseOnly = an::SipDistortion::create(sip);
    ASSERT_TRUE(inverseOnly.hasValue());
    EXPECT_EQ(an::DrizzleMapDescriptor::create(*model, {.sourceDistortion = *inverseOnly}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(an::DrizzleMapDescriptor::create(*model, {}).hasValue());
}

TEST(DrizzlePlanTest, DenseAndBoundedMapsHaveExplicitDifferentCosts)
{
    auto request = drizzleRequest();
    request.input = drizzleDescriptor(19u, 101u);
    const auto model = an::DrizzleModel::create(request);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto bounded = an::planDrizzle(*execution, *model, {});
    auto dense = an::planDrizzle(*execution, *model, {.mapStorage = an::DrizzleMapStorage::Dense});
    ASSERT_TRUE(bounded.hasValue());
    ASSERT_TRUE(dense.hasValue());
    EXPECT_EQ(dense->memory.coordinateMapBytes, (19u + 1u) * 101u * 8u);
    EXPECT_EQ(bounded->memory.coordinateMapBytes, (19u + 1u) * 34u * 8u);
    EXPECT_EQ(dense->sourceRows, bounded->sourceRows);
    EXPECT_EQ(dense->memory.peakBytes - bounded->memory.peakBytes, (19u + 1u) * (101u - 34u) * 8u);
}

TEST(DrizzlePlanTest, DestinationBandsExposeRepeatedReadsAndRegionRequests)
{
    const auto model = an::DrizzleModel::create(drizzleRequest());
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto full = an::planDrizzle(*execution, *model, {.maximumSourceRows = 5u, .maximumDestinationRows = 4u});
    const auto region = an::planDrizzle(
        *execution, *model,
        {.access = an::DrizzleSourceAccess::Region, .maximumSourceRows = 5u, .maximumDestinationRows = 4u});
    ASSERT_TRUE(full.hasValue());
    ASSERT_TRUE(region.hasValue());
    EXPECT_EQ(full->destinationTileCount, 7u);
    EXPECT_EQ(full->sourceBlockCount, 3u);
    EXPECT_EQ(full->tileReason, an::DrizzleTileReason::CallerRowCap);
    EXPECT_EQ(full->io.readRequests, 7u * 5u);
    EXPECT_EQ(region->io.readRequests, 7u * 5u * 3u);
    EXPECT_EQ(full->io.logicalInputBytes, 7u * 5u * 17u * 13u * 4u);
    EXPECT_EQ(region->io.logicalInputBytes, full->io.logicalInputBytes);
    EXPECT_EQ(region->memory.sourceBufferBytes, 17u * 5u * 4u);
    EXPECT_EQ(full->memory.sourceBufferBytes, 17u * 13u * 4u);
}

TEST(DrizzlePlanTest, AccountsPackedMasksAuxiliariesOptionalMapsAndEveryPeakTerm)
{
    auto request = drizzleRequest();
    request.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    request.maskExtent = request.input.extent();
    request.pixelWeights = drizzleDescriptor();
    request.flatWeights = drizzleDescriptor();
    request.output.weightMap = true;
    request.output.coverageMap = true;
    request.parameters.collectMaximumWeight = true;
    const auto model = an::DrizzleModel::create(request);
    auto engine = an::EngineContext::create({.maxConcurrency = 3u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto plan = an::planDrizzle(*execution, *model, {.concurrentFrameReads = 2u, .sourceWorkspaceBytes = 123u});
    ASSERT_TRUE(plan.hasValue());
    const auto& memory = plan->memory;
    EXPECT_EQ(memory.outputMaskBytes, 5u * 26u);
    EXPECT_EQ(memory.outputMapBytes, 34u * 26u * 8u);
    EXPECT_EQ(memory.coverageStateBytes, 34u * 26u * 8u);
    EXPECT_EQ(memory.sourceBufferBytes, (17u * 12u + 3u) * 13u * 2u);
    EXPECT_EQ(memory.sourceWorkspaceBytes, 246u);
    EXPECT_EQ(plan->io.frameBytes, (17u * 12u + 3u) * 13u);
    EXPECT_EQ(memory.scratchBytesPerWorker % 64u, 0u);
    EXPECT_EQ(memory.scratchBytes, memory.scratchBytesPerWorker * plan->workers);
    EXPECT_EQ(memory.persistentBytes, memory.outputImageBytes + memory.outputMaskBytes + memory.outputMapBytes +
                                          memory.frameStateBytes + memory.kernelTableBytes + memory.preparedStateBytes);
    EXPECT_EQ(memory.peakBytes, memory.persistentBytes + memory.accumulatorBytes + memory.coverageStateBytes +
                                    memory.sourceBufferBytes + memory.sourceWorkspaceBytes + memory.coordinateMapBytes +
                                    memory.scratchBytes);
    EXPECT_TRUE(plan->collectMaximumWeight);
    EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, 0u);
}

TEST(DrizzlePlanTest, ExactMinimumFitsAndOneByteLessReturnsActionableBudgetFailure)
{
    const auto model = an::DrizzleModel::create(drizzleRequest());
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    an::DrizzlePlanningRequest request{
        .access = an::DrizzleSourceAccess::Region, .maximumSourceRows = 1u, .maximumDestinationRows = 1u};
    const auto minimum = an::planDrizzle(*execution, *model, request);
    ASSERT_TRUE(minimum.hasValue());
    const auto bytes = minimum->memory.peakBytes;
    request.workingSetLimitBytes = bytes;
    ASSERT_TRUE(an::planDrizzle(*execution, *model, request).hasValue());
    request.workingSetLimitBytes = bytes - 1u;
    const auto refused = an::planDrizzle(*execution, *model, request);
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), bytes);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Available), bytes - 1u);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Limit), bytes - 1u);
    EXPECT_EQ(engine->memoryResource()->counters().deniedAllocations, 0u);
}

TEST(DrizzlePlanTest, BudgetNarrowsSourceBlocksAndDestinationBands)
{
    const auto model = an::DrizzleModel::create(drizzleRequest());
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto small = an::planDrizzle(
        *execution, *model,
        {.access = an::DrizzleSourceAccess::Region, .maximumSourceRows = 2u, .maximumDestinationRows = 1u});
    ASSERT_TRUE(small.hasValue());
    const auto fitted =
        an::planDrizzle(*execution, *model,
                        {.access = an::DrizzleSourceAccess::Region, .workingSetLimitBytes = small->memory.peakBytes});
    ASSERT_TRUE(fitted.hasValue());
    EXPECT_EQ(fitted->sourceRows, 2u);
    EXPECT_EQ(fitted->destinationRows, 1u);
    EXPECT_EQ(fitted->tileReason, an::DrizzleTileReason::BudgetBound);
    EXPECT_EQ(fitted->memory.peakBytes, small->memory.peakBytes);
}

TEST(DrizzlePlanTest, PreparedContextMemoryIsCountedOnceAndUnrelatedMemoryReducesBudget)
{
    auto limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = 100000u;
    auto engine = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto model = an::DrizzleModel::create(drizzleRequest());
    auto allocation = engine->memoryResource()->allocate(2000u, 64u);
    ASSERT_TRUE(allocation.hasValue());
    const auto prepared =
        an::planDrizzle(*execution, *model, {.preparedStateBytes = 1500u, .preparedContextBytes = 1000u});
    const auto external = an::planDrizzle(*execution, *model, {.preparedStateBytes = 1500u});
    engine->memoryResource()->deallocate(*allocation, 2000u, 64u);
    ASSERT_TRUE(prepared.hasValue());
    ASSERT_TRUE(external.hasValue());
    EXPECT_EQ(prepared->memory.availableBytes, 99000u);
    EXPECT_EQ(external->memory.availableBytes, 98000u);
    EXPECT_EQ(prepared->memory.peakBytes, external->memory.peakBytes);
    EXPECT_EQ(prepared->memory.preparedStateBytes, 1500u);
    EXPECT_EQ(an::planDrizzle(*execution, *model, {.preparedContextBytes = 1u}).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(
        an::planDrizzle(*execution, *model, {.preparedStateBytes = 1u, .preparedContextBytes = 1u}).status().code(),
        an::ErrorCode::IncompatibleArguments);
}

TEST(DrizzlePlanTest, CallerLimitCannotWidenContextBudget)
{
    auto limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = 1u;
    auto engine = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto model = an::DrizzleModel::create(drizzleRequest());
    const auto plan = an::planDrizzle(*execution, *model, {.workingSetLimitBytes = 1000000u});
    EXPECT_EQ(plan.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(countField(plan.status(), an::ContextKey::Available), 1u);
    EXPECT_EQ(countField(plan.status(), an::ContextKey::Limit), 1u);
}

TEST(DrizzlePlanTest, ChecksExecutionGeometryLimitsAndOverflowingWorkspace)
{
    auto limits = an::defaultEngineLimits();
    limits.maxImageWidth = 33u;
    auto narrowEngine = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    auto narrowExecution = an::ExecutionContext::create(*narrowEngine, {});
    const auto model = an::DrizzleModel::create(drizzleRequest());
    EXPECT_EQ(an::planDrizzle(*narrowExecution, *model, {}).status().code(), an::ErrorCode::LimitExceeded);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    EXPECT_EQ(an::planDrizzle(*execution, *model, {.preparedStateBytes = maximum}).status().code(),
              an::ErrorCode::LimitExceeded);
    EXPECT_EQ(an::planDrizzle(*execution, *model, {.concurrentFrameReads = 2u, .sourceWorkspaceBytes = maximum})
                  .status()
                  .code(),
              an::ErrorCode::LimitExceeded);
}

TEST(DrizzlePlanTest, InvalidPlanningRequestsAreRefused)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto model = an::DrizzleModel::create(drizzleRequest());
    for (const auto request :
         {an::DrizzlePlanningRequest{.access = static_cast<an::DrizzleSourceAccess>(255u)},
          an::DrizzlePlanningRequest{.mapStorage = static_cast<an::DrizzleMapStorage>(255u)},
          an::DrizzlePlanningRequest{.maximumSourceRows = 0u}, an::DrizzlePlanningRequest{.maximumSourceRows = 257u},
          an::DrizzlePlanningRequest{.concurrentFrameReads = 0u},
          an::DrizzlePlanningRequest{.concurrentFrameReads = 6u}})
        EXPECT_EQ(an::planDrizzle(*execution, *model, request).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planDrizzle({}, *model, {}).status().code(), an::ErrorCode::NotInitialized);
}

TEST(DrizzlePlanTest, UsesAdmittedConcurrencyAndRealContextBackendSelection)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 3u});
    auto first = an::ExecutionContext::create(*engine, {.maxConcurrency = 2u});
    auto second = an::ExecutionContext::create(*engine, {.backend = an::ComputeBackend::Reference});
    ASSERT_TRUE(second.hasValue());
    const auto model = an::DrizzleModel::create(drizzleRequest());
    const auto plan = an::planDrizzle(*second, *model, {});
    ASSERT_TRUE(plan.hasValue());
    EXPECT_EQ(plan->workers, 1u);
    EXPECT_EQ(plan->workers, second->admittedConcurrency());
    EXPECT_EQ(plan->backend.backend, an::ComputeBackend::Reference);
    EXPECT_EQ(plan->backend.instructionSet, an::InstructionSet::Scalar);
    EXPECT_EQ(plan->memory.scratchBytes, plan->memory.scratchBytesPerWorker);
    EXPECT_EQ(plan->accumulation, an::ComputePrecision::Float64);
    EXPECT_EQ(plan->accumulationReason, an::Float64Justification::StackingAccumulation);
}

TEST(DrizzlePlanTest, CancellationAndDeadlineReturnBeforeReportingOrAllocating)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    std::stop_source stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(*engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();
    const auto model = an::DrizzleModel::create(drizzleRequest());
    EXPECT_EQ(an::planDrizzle(*execution, *model, {}).status().code(), an::ErrorCode::Cancelled);
    *execution = {};
    options.stopToken = {};
    options.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto expired = an::ExecutionContext::create(*engine, options);
    ASSERT_TRUE(expired.hasValue());
    EXPECT_EQ(an::planDrizzle(*expired, *model, {}).status().code(), an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, 0u);
}

class DrizzleDiagnosticSink final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        if (std::string_view(event.operation) != "an::planDrizzle")
            return;
        severity = event.severity;
        operationId = event.operationId;
        ++count;
        if (stop != nullptr)
            stop->request_stop();
    }
    an::DiagnosticSeverity severity = an::DiagnosticSeverity::Info;
    std::uint64_t operationId = 0u;
    unsigned count = 0u;
    std::stop_source* stop = nullptr;
};

TEST(DrizzlePlanTest, WarnsAboutRepeatedInputAndObservesCancellationFromTheSink)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    DrizzleDiagnosticSink sink;
    std::stop_source stop;
    an::ExecutionOptions options;
    options.diagnosticSink = &sink;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(*engine, options);
    const auto model = an::DrizzleModel::create(drizzleRequest());
    const unsigned before = sink.count;
    const auto plan = an::planDrizzle(*execution, *model, {.maximumDestinationRows = 1u});
    ASSERT_TRUE(plan.hasValue());
    EXPECT_EQ(sink.severity, an::DiagnosticSeverity::Warning);
    EXPECT_EQ(sink.operationId, execution->operationId());
    EXPECT_EQ(sink.count, before + 1u);
    sink.stop = &stop;
    EXPECT_EQ(an::planDrizzle(*execution, *model, {}).status().code(), an::ErrorCode::Cancelled);
}

} // namespace
