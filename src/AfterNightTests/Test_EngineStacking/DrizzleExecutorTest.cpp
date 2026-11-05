// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapIdentity.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <vector>

#include <gtest/gtest.h>

namespace
{

an::DrizzleModel modelFor(an::DrizzleKernel kernel = an::DrizzleKernel::Square,
                          an::SampleType type = an::SampleType::Float32,
                          an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const auto extent = *an::ImageExtent::create(19u, 11u, 1u, an::defaultEngineLimits());
    const auto descriptor = *an::ImageDescriptor::create({.extent = extent,
                                                          .sampleType = type,
                                                          .channelSchema = an::ChannelSchema::Mono,
                                                          .colorSpace = an::ColorSpace::Unspecified,
                                                          .numericDomain = domain,
                                                          .cfa = {},
                                                          .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                          .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = descriptor, .frameCount = 3u};
    request.parameters.kernel = kernel;
    request.parameters.scale = 1.0;
    request.parameters.pixelFraction = 1.0;
    request.parameters.collectMaximumWeight = true;
    request.output.weightMap = true;
    request.output.coverageMap = true;
    return *an::DrizzleModel::create(request);
}

struct Fixture
{
    explicit Fixture(const an::DrizzleModel& model)
    {
        const auto extent = model.request().input.extent();
        const auto layout =
            *an::ImageLayout::create({extent, 4u, 1u, extent.channelCount(), 1u}, an::defaultEngineLimits());
        for (std::uint32_t frame = 0u; frame < model.request().frameCount; ++frame)
        {
            samples.emplace_back(extent.sampleCount());
            for (std::size_t i = 0u; i < samples.back().size(); ++i)
                samples.back()[i] = static_cast<float>((i * 13u + frame * 7u) % 31u) / 32.0f;
            data.push_back({{layout, std::as_bytes(std::span{samples.back()}), an::SampleType::Float32}});
            frames.push_back({*an::DrizzleMapDescriptor::create(model, {.postTranslation = {0.25, -0.25}})});
        }
    }
    std::vector<std::vector<float>> samples;
    std::vector<an::DrizzleFrameData> data;
    std::vector<an::DrizzleFrameParameters> frames;
};

class RecordingSource final : public an::DrizzleFrameSource
{
public:
    explicit RecordingSource(std::span<const an::DrizzleFrameData> frames) : data(frames) {}
    an::DrizzleSourceAccess access() const noexcept override
    {
        return an::DrizzleSourceAccess::Region;
    }
    an::Result<an::DrizzleFrameData> read(const an::ExecutionContext& execution, std::uint32_t frame,
                                          std::uint32_t first, std::uint32_t rows,
                                          const an::DrizzleReadBuffers& buffers) override
    {
        reads.push_back({frame, first, rows});
        if (reads.size() == failAt)
        {
            if (throws)
                throw std::runtime_error("decoder failure");
            return an::Status::failure(an::ErrorCode::InvalidData);
        }
        if (reads.size() == cancelAt && stop != nullptr)
            stop->request_stop();
        auto result = an::drizzleSourceRows(data[frame], first, rows);
        if (result.hasValue() && decode)
        {
            EXPECT_GE(buffers.pixels.size(), result->pixels.samples.size());
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffers.pixels.data()) % 64u, 0u);
            std::memcpy(buffers.pixels.data(), result->pixels.samples.data(), result->pixels.samples.size());
            result->pixels.samples = buffers.pixels.first(result->pixels.samples.size());
        }
        static_cast<void>(execution);
        if (malformed && result.hasValue())
            result->pixels.sampleType = an::SampleType::UInt16;
        return result;
    }
    std::span<const an::DrizzleFrameData> data;
    std::vector<std::array<std::uint32_t, 3u>> reads;
    std::size_t failAt = 0u, cancelAt = 0u;
    std::stop_source* stop = nullptr;
    bool throws = false, malformed = false, decode = false;
};

class Progress final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        reports.push_back(report);
        if (stop != nullptr && report.completed >= cancelAt)
            stop->request_stop();
    }
    std::vector<an::ProgressReport> reports;
    std::stop_source* stop = nullptr;
    std::uint64_t cancelAt = (std::numeric_limits<std::uint64_t>::max)();
};

void sameOutput(const an::DrizzleResult& left, const an::DrizzleResult& right, double tolerance)
{
    ASSERT_EQ(left.descriptor, right.descriptor);
    const auto count = left.descriptor.extent().sampleCount();
    const auto* a = static_cast<const float*>(left.image.data());
    const auto* b = static_cast<const float*>(right.image.data());
    const auto* aw = static_cast<const float*>(left.weightMap.data());
    const auto* bw = static_cast<const float*>(right.weightMap.data());
    for (std::size_t i = 0u; i < count; ++i)
    {
        EXPECT_NEAR(a[i], b[i], tolerance);
        EXPECT_NEAR(aw[i], bw[i], tolerance);
    }
    EXPECT_EQ(std::memcmp(left.mask.view()->data(), right.mask.view()->data(), left.mask.capacityBytes()), 0);
    EXPECT_EQ(std::memcmp(left.coverageMap.data(), right.coverageMap.data(), left.coverageMap.capacityBytes()), 0);
    EXPECT_EQ(left.report.selected, right.report.selected);
    EXPECT_EQ(left.report.excluded, right.report.excluded);
    EXPECT_EQ(left.report.contributions, right.report.contributions);
}

TEST(DrizzleMapIdentityTest, EveryMappingComponentAndNormalizationGridParticipates)
{
    const auto model = modelFor();
    an::SipDistortionRequest sip;
    sip.referencePixel = {7.25, 4.5};
    sip.forwardX.order = sip.forwardY.order = 6u;
    sip.forwardX.coefficients[6][0] = 1.0e-12;
    sip.forwardY.coefficients[0][6] = 2.0e-12;
    sip.inverseX.order = sip.inverseY.order = 6u;
    sip.inverseX.coefficients[0][6] = -1.0e-12;
    sip.inverseY.coefficients[6][0] = -2.0e-12;
    an::DrizzleMappingRequest mapping;
    mapping.postTranslation = {0.25, -0.5};
    mapping.sourceDistortion = *an::SipDistortion::create(sip);
    const auto descriptor = an::DrizzleMapDescriptor::create(model, mapping);
    ASSERT_TRUE(descriptor);
    auto identity = an::encodeDrizzleMapIdentity(*descriptor, an::DrizzleNormalizationSpace::SourcePixels);
    ASSERT_TRUE(identity);
    EXPECT_TRUE(
        an::validateDrizzleMapIdentity(*descriptor, an::DrizzleNormalizationSpace::SourcePixels, identity->bytes).ok());
    EXPECT_EQ(an::validateDrizzleMapIdentity(*descriptor, an::DrizzleNormalizationSpace::OutputPixels, identity->bytes)
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    for (unsigned field = 0u; field < 7u; ++field)
    {
        auto changed = mapping;
        auto distortion = sip;
        if (field == 0u)
            changed.postTranslation.x += 0.01;
        if (field == 1u)
            distortion.referencePixel.y += 0.01;
        if (field == 2u)
            distortion.forwardX.coefficients[6][0] += 1.0e-16;
        if (field == 3u)
            distortion.forwardY.coefficients[0][6] += 1.0e-16;
        if (field == 4u)
            distortion.inverseX.coefficients[0][6] += 1.0e-16;
        if (field == 5u)
            distortion.inverseY.coefficients[6][0] += 1.0e-16;
        changed.sourceDistortion = *an::SipDistortion::create(distortion);
        if (field == 6u)
        {
            auto transform = changed.transform.toRequest();
            transform.sourceToReference[2] += 0.01;
            changed.transform = *an::RegistrationTransform::create(transform);
        }
        auto other = an::DrizzleMapDescriptor::create(model, changed);
        ASSERT_TRUE(other);
        EXPECT_EQ(
            an::validateDrizzleMapIdentity(*other, an::DrizzleNormalizationSpace::SourcePixels, identity->bytes).code(),
            an::ErrorCode::IncompatibleArguments);
    }
    auto corrupt = identity->bytes;
    corrupt.back() = std::byte{1};
    EXPECT_EQ(an::validateDrizzleMapIdentity(*descriptor, an::DrizzleNormalizationSpace::SourcePixels, corrupt).code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::validateDrizzleMapIdentity(*descriptor, an::DrizzleNormalizationSpace::SourcePixels,
                                             std::span(corrupt).first(128u))
                  .code(),
              an::ErrorCode::InvalidData);
    const auto invalid = an::encodeDrizzleMapIdentity(*descriptor, static_cast<an::DrizzleNormalizationSpace>(99u));
    EXPECT_EQ(invalid.code(), an::ErrorCode::InvalidArgument);
    EXPECT_STREQ(invalid.status().operation(), "an::encodeDrizzleMapIdentity");
    ASSERT_NE(invalid.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(invalid.status().findContext(an::ContextKey::Parameter)->text, "normalization");
    const auto shortIdentity =
        an::validateDrizzleMapIdentity(*descriptor, an::DrizzleNormalizationSpace::SourcePixels, {});
    EXPECT_EQ(shortIdentity.code(), an::ErrorCode::InvalidData);
    EXPECT_STREQ(shortIdentity.operation(), "an::validateDrizzleMapIdentity");
    ASSERT_NE(shortIdentity.findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(shortIdentity.findContext(an::ContextKey::Parameter)->text, "stored");
}

TEST(DrizzleExecutorTest, NormalizedIntegerMeanIsPublishedInFloatingUnitsWithCompleteMaps)
{
    const auto extent = *an::ImageExtent::create(5u, 3u, 1u, an::defaultEngineLimits());
    const auto descriptor = *an::ImageDescriptor::create({.extent = extent,
                                                          .sampleType = an::SampleType::UInt16,
                                                          .channelSchema = an::ChannelSchema::Mono,
                                                          .colorSpace = an::ColorSpace::Unspecified,
                                                          .numericDomain = an::NumericDomain::NormalizedLinear,
                                                          .cfa = {},
                                                          .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                          .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = descriptor, .frameCount = 2u};
    request.parameters.kernel = an::DrizzleKernel::Point;
    request.parameters.scale = 1.0;
    request.parameters.collectMaximumWeight = true;
    request.output.weightMap = true;
    request.output.coverageMap = true;
    const auto model = *an::DrizzleModel::create(request);
    ASSERT_EQ(model.request().frameCount, 2u);
    const auto layout = *an::ImageLayout::create({extent, 2u, 1u, 1u, 1u}, an::defaultEngineLimits());
    std::vector<std::uint16_t> black(15u, 0u), white(15u, 65535u);
    const std::array<an::DrizzleFrameData, 2u> data{
        an::DrizzleFrameData{{layout, std::as_bytes(std::span{black}), an::SampleType::UInt16}},
        an::DrizzleFrameData{{layout, std::as_bytes(std::span{white}), an::SampleType::UInt16}}};
    auto source = an::ResidentDrizzleFrameSource::create(data, an::DrizzleSourceAccess::Region);
    ASSERT_TRUE(source.hasValue());
    const auto geometry = *an::DrizzleMapDescriptor::create(model, {});
    const std::array<an::DrizzleFrameParameters, 2u> frames{{{geometry}, {geometry}}};
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    {
        an::DrizzleExecutionRequest run;
        run.planning.maximumSourceRows = 1u;
        run.planning.maximumDestinationRows = 1u;
        run.artifact = {17u, 23u};
        auto result = an::executeDrizzle(*engine, *execution, model, *source, frames, run);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->artifact.runId, 17u);
        EXPECT_EQ(result->artifact.artifactId, 23u);
        EXPECT_EQ(result->report.readRequests, result->report.plan.io.readRequests);
        EXPECT_EQ(result->report.inputBytesRead, result->report.plan.io.logicalInputBytes);
        EXPECT_EQ(result->report.selected, 30u);
        ASSERT_TRUE(result->report.maximumWeight.has_value());
        EXPECT_DOUBLE_EQ(*result->report.maximumWeight, 2.0);
        const auto* pixels = static_cast<const float*>(result->image.data());
        const auto* weights = static_cast<const float*>(result->weightMap.data());
        const auto* coverage = static_cast<const std::uint32_t*>(result->coverageMap.data());
        for (std::uint32_t i = 0u; i < 15u; ++i)
        {
            EXPECT_FLOAT_EQ(pixels[i], 0.5f);
            EXPECT_FLOAT_EQ(weights[i], 2.0f);
            EXPECT_EQ(coverage[i], 2u);
        }
        EXPECT_EQ(an::countValidity(*result->mask.view()).valid, 15u);
        EXPECT_LE(engine->memoryResource()->counters().peakBytes, result->report.plan.memory.peakBytes);
    }
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
}

TEST(DrizzleExecutorTest, AllKernelsMatchDenseReferenceAcrossReaderModesTilesAndWorkers)
{
    for (auto kernel : {an::DrizzleKernel::Point, an::DrizzleKernel::Square, an::DrizzleKernel::Turbo,
                        an::DrizzleKernel::Gaussian, an::DrizzleKernel::Lanczos2, an::DrizzleKernel::Lanczos3})
    {
        SCOPED_TRACE(an::drizzleKernelName(kernel));
        auto model = modelFor(kernel);
        Fixture fixture(model);
        auto referenceEngine = an::EngineContext::create({.maxConcurrency = 1u});
        auto referenceExecution =
            an::ExecutionContext::create(*referenceEngine, {.backend = an::ComputeBackend::Reference});
        auto source = an::ResidentDrizzleFrameSource::create(fixture.data);
        an::DrizzleExecutionRequest request;
        request.planning.mapStorage = an::DrizzleMapStorage::Dense;
        auto reference =
            an::executeDrizzle(*referenceEngine, *referenceExecution, model, *source, fixture.frames, request);
        ASSERT_TRUE(reference.hasValue()) << reference.status().message();
        for (auto workers : {1u, 3u})
        {
            auto engine = an::EngineContext::create({.maxConcurrency = workers});
            auto execution = an::ExecutionContext::create(*engine, {});
            auto whole = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames);
            ASSERT_TRUE(whole.hasValue()) << whole.status().message();
            sameOutput(*reference, *whole, 2e-6);
            for (auto rows : {1u, 4u})
            {
                RecordingSource reader(fixture.data);
                reader.decode = true;
                request.planning.mapStorage = an::DrizzleMapStorage::BoundedRows;
                request.planning.maximumSourceRows = rows;
                request.planning.maximumDestinationRows = rows;
                auto tiled = an::executeDrizzle(*engine, *execution, model, reader, fixture.frames, request);
                ASSERT_TRUE(tiled.hasValue()) << tiled.status().message();
                sameOutput(*whole, *tiled, 0.0);
                EXPECT_EQ(tiled->report.readRequests, tiled->report.plan.io.readRequests);
                EXPECT_EQ(tiled->report.inputBytesRead, tiled->report.plan.io.logicalInputBytes);
                EXPECT_EQ(tiled->report.implementation, an::DrizzleAccumulationImplementation::OwnedRows);
                EXPECT_EQ(tiled->report.plan.workers, workers);
                std::size_t index = 0u;
                for (std::uint32_t tile = 0u; tile < tiled->report.plan.destinationTileCount; ++tile)
                    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
                        for (std::uint32_t row = 0u; row < 11u; row += rows)
                        {
                            ASSERT_LT(index, reader.reads.size());
                            EXPECT_EQ(reader.reads[index++],
                                      (std::array<std::uint32_t, 3u>{frame, row, (std::min)(rows, 11u - row)}));
                        }
            }
        }
    }
}

TEST(DrizzleExecutorTest, ReadFailureExceptionAndMalformedBandsReleaseEverythingWithoutFallback)
{
    const auto model = modelFor();
    Fixture fixture(model);
    for (int mode = 0; mode < 3; ++mode)
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 2u});
        auto execution = an::ExecutionContext::create(*engine, {});
        RecordingSource reader(fixture.data);
        reader.failAt = mode < 2 ? 3u : 0u;
        reader.throws = mode == 1;
        reader.malformed = mode == 2;
        an::DrizzleExecutionRequest request;
        request.planning.maximumSourceRows = 2u;
        auto result = an::executeDrizzle(*engine, *execution, model, reader, fixture.frames, request);
        EXPECT_FALSE(result.hasValue());
        EXPECT_EQ(result.status().code(), mode == 0   ? an::ErrorCode::InvalidData
                                          : mode == 1 ? an::ErrorCode::ExternalFailure
                                                      : an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(reader.reads.size(), mode == 2 ? 1u : 3u);
        EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
    }
}

TEST(DrizzleExecutorTest, CancellationAtReadProgressAndPublicationReturnsNoArtifact)
{
    const auto model = modelFor();
    Fixture fixture(model);
    for (int point = 0; point < 3; ++point)
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 2u});
        std::stop_source stop;
        Progress progress;
        if (point != 0)
            progress.stop = &stop;
        progress.cancelAt = point == 1 ? 2u : 11u;
        auto execution =
            an::ExecutionContext::create(*engine, {.stopToken = stop.get_token(), .progressSink = &progress});
        RecordingSource reader(fixture.data);
        reader.stop = &stop;
        reader.cancelAt = point == 0 ? 2u : 0u;
        an::DrizzleExecutionRequest request;
        request.planning.maximumSourceRows = 4u;
        auto result = an::executeDrizzle(*engine, *execution, model, reader, fixture.frames, request);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
        for (std::size_t i = 1u; i < progress.reports.size(); ++i)
        {
            EXPECT_GE(progress.reports[i].completed, progress.reports[i - 1u].completed);
            EXPECT_EQ(progress.reports[i].total, progress.reports[0].total);
        }
        EXPECT_EQ(execution->operationCounters().discardedProgressReports, 0u);
    }
}

TEST(DrizzleExecutorTest, ExactMinimumBudgetWorksAndOneByteLessRefusesBeforeAllocationOrReading)
{
    const auto model = modelFor(an::DrizzleKernel::Lanczos3);
    Fixture fixture(model);
    auto planningEngine = an::EngineContext::create({.maxConcurrency = 1u});
    auto planningExecution = an::ExecutionContext::create(*planningEngine, {});
    const an::DrizzlePlanningRequest planning{
        .access = an::DrizzleSourceAccess::Region, .maximumSourceRows = 1u, .maximumDestinationRows = 1u};
    auto plan = an::planDrizzle(*planningExecution, model, planning);
    ASSERT_TRUE(plan.hasValue());
    for (auto budget : {plan->memory.peakBytes - 1u, plan->memory.peakBytes})
    {
        auto limits = an::defaultEngineLimits();
        limits.memoryBudgetBytes = budget;
        auto engine = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
        auto execution = an::ExecutionContext::create(*engine, {});
        RecordingSource source(fixture.data);
        {
            an::DrizzleExecutionRequest request{.planning = planning};
            auto result = an::executeDrizzle(*engine, *execution, model, source, fixture.frames, request);
            if (budget < plan->memory.peakBytes)
            {
                EXPECT_EQ(result.status().code(), an::ErrorCode::MemoryBudgetExceeded);
                EXPECT_TRUE(source.reads.empty());
                EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, 0u);
            }
            else
            {
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_LE(engine->memoryResource()->counters().peakBytes, budget);
                EXPECT_EQ(engine->memoryResource()->counters().liveBytes, result->report.outputBytes);
            }
        }
        EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
    }
}

TEST(DrizzleExecutorTest, EmptySupportAndUnrequestedDiagnosticsNeverAcquirePreviousValues)
{
    auto request = modelFor(an::DrizzleKernel::Point).request();
    request.parameters.collectMaximumWeight = false;
    request.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    const auto model = *an::DrizzleModel::create(request);
    Fixture fixture(model);
    for (auto& samples : fixture.samples)
        std::fill(samples.begin(), samples.end(), 0.0f);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data);
    auto result = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_FALSE(result->report.maximumWeight.has_value());
    EXPECT_EQ(result->report.validOutputSamples, 0u);
    EXPECT_EQ(result->report.invalidOutputSamples, 209u);
    EXPECT_EQ(result->report.excluded, 627u);
    EXPECT_EQ(an::countValidity(*result->mask.view()).valid, 0u);
}

TEST(DrizzleExecutorTest, TransitionPreservesAcquisitionAndWithdrawsSourceGeometryAndOutputIntent)
{
    const auto model = modelFor();
    an::ImageMetadata source;
    auto transition = an::drizzleTransition(model, source.semantics());
    ASSERT_TRUE(transition.hasValue());
    EXPECT_TRUE(transition->clearCodecMetadata);
    EXPECT_TRUE(transition->clearOutputIntent);
    EXPECT_TRUE(transition->astrometry.has_value());
    EXPECT_TRUE(transition->record.has_value());
    auto applied = an::applyMetadataTransition(source, *transition);
    ASSERT_TRUE(applied.hasValue());
    EXPECT_EQ(applied->acquisition(), source.acquisition());
    EXPECT_TRUE(applied->semantics().processingState().registered);
    EXPECT_EQ(applied->semantics().validity().rule(), an::ValidityRule::ExplicitMask);
}

template <typename T> void checkFloatingOutput(an::SampleType type)
{
    for (auto domain : {an::NumericDomain::NormalizedLinear, an::NumericDomain::RawSensorCode})
    {
        auto request = modelFor(an::DrizzleKernel::Point, type, domain).request();
        request.frameCount = 2u;
        request.output.sampleType = an::SampleType::Float64;
        const auto model = *an::DrizzleModel::create(request);
        const auto extent = request.input.extent();
        const auto layout = *an::ImageLayout::create({extent, sizeof(T), 1u, 1u, 1u}, an::defaultEngineLimits());
        const T high = std::is_integral_v<T> ? (std::numeric_limits<T>::max)() : static_cast<T>(0.75);
        std::vector<T> zero(extent.sampleCount(), T{0}), values(extent.sampleCount(), high);
        if constexpr (std::is_signed_v<T>)
            values[0] = std::is_integral_v<T> ? (std::numeric_limits<T>::lowest)() : static_cast<T>(-0.5);
        const std::array<an::DrizzleFrameData, 2u> data{
            an::DrizzleFrameData{{layout, std::as_bytes(std::span{zero}), type}},
            an::DrizzleFrameData{{layout, std::as_bytes(std::span{values}), type}}};
        const auto geometry = *an::DrizzleMapDescriptor::create(model, {});
        const std::array<an::DrizzleFrameParameters, 2u> frames{{{geometry}, {geometry}}};
        auto source = an::ResidentDrizzleFrameSource::create(data, an::DrizzleSourceAccess::Region);
        ASSERT_TRUE(source.hasValue());
        auto engine = an::EngineContext::create({.maxConcurrency = 1u});
        auto execution = an::ExecutionContext::create(*engine, {});
        auto result = an::executeDrizzle(*engine, *execution, model, *source, frames);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        const double divisor =
            domain == an::NumericDomain::NormalizedLinear && std::is_integral_v<T> ? static_cast<double>(high) : 1.0;
        const auto* output = static_cast<const double*>(result->image.data());
        for (std::size_t i = 0u; i < values.size(); ++i)
            EXPECT_DOUBLE_EQ(output[i], (static_cast<double>(values[i]) / 2.0) / divisor);
        EXPECT_EQ(result->descriptor.numericDomain(), domain);
    }
}

TEST(DrizzleExecutorTest, EveryResidentRepresentationKeepsRawUnitsAndExplicitNormalizedFullScale)
{
    checkFloatingOutput<std::uint8_t>(an::SampleType::UInt8);
    checkFloatingOutput<std::int8_t>(an::SampleType::Int8);
    checkFloatingOutput<std::uint16_t>(an::SampleType::UInt16);
    checkFloatingOutput<std::int16_t>(an::SampleType::Int16);
    checkFloatingOutput<std::uint32_t>(an::SampleType::UInt32);
    checkFloatingOutput<std::int32_t>(an::SampleType::Int32);
    checkFloatingOutput<float>(an::SampleType::Float32);
    checkFloatingOutput<double>(an::SampleType::Float64);
}

TEST(DrizzleExecutorTest, PerFrameSourceComposesEveryStackCombinationAndRejectionWithoutRetainedFrameHistory)
{
    const auto drizzle = modelFor(an::DrizzleKernel::Point);
    Fixture fixture(drizzle);
    for (auto& frame : fixture.frames)
        frame.geometry = *an::DrizzleMapDescriptor::create(drizzle, {});
    std::vector<an::StackSourceBand> residentFrames;
    for (std::uint32_t i = 0u; i < fixture.data.size(); ++i)
        residentFrames.push_back({.sampleType = an::SampleType::Float32,
                                  .samples = fixture.samples[i].data(),
                                  .layout = &fixture.data[i].pixels.layout,
                                  .frameIndex = i});
    for (std::uint32_t option = 0u; option < 13u; ++option)
    {
        SCOPED_TRACE(option);
        an::StackRequest request{.input = {3u, drizzle.outputDescriptor()}};
        if (option < 6u)
            request.combination = static_cast<an::StackCombination>(option);
        else
            request.rejection.method = static_cast<an::StackRejection>(option - 5u);
        request.output.coverageMap = true;
        request.output.varianceMap = true;
        auto model = an::makeStackModel(request);
        ASSERT_TRUE(model.hasValue()) << model.status().message();
        auto engine = an::EngineContext::create({.maxConcurrency = 2u});
        Progress progress;
        auto execution = an::ExecutionContext::create(*engine, {.progressSink = &progress});
        auto resident = an::ResidentStackFrameSource::create(residentFrames);
        ASSERT_TRUE(resident.hasValue());
        an::StackExecutionRequest run;
        run.maximumTileRows = 3u;
        run.sumDomain = an::StackSumDomain::Raw;
        auto expected = an::executeStack(*engine, *execution, *model, *resident, {}, run, {});
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        *execution = an::ExecutionContext{};
        auto newExecution = an::ExecutionContext::create(*engine, {.maxConcurrency = 1u});
        auto input = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
        auto source = an::DrizzledStackFrameSource::create(*engine, drizzle, *input, fixture.frames);
        ASSERT_TRUE(source.hasValue());
        {
            auto actual = an::executeStack(*engine, *newExecution, *model, *source, {}, run, {});
            ASSERT_TRUE(actual.hasValue()) << actual.status().message();
            EXPECT_EQ(std::memcmp(expected->image.data(), actual->image.data(), expected->image.logicalBytes()), 0);
            EXPECT_EQ(
                std::memcmp(expected->mask.view()->data(), actual->mask.view()->data(), expected->mask.capacityBytes()),
                0);
            for (std::size_t map = 0u; map < an::kStackOutputMapCount; ++map)
                if (expected->maps[map].valid())
                    EXPECT_EQ(std::memcmp(expected->maps[map].data(), actual->maps[map].data(),
                                          expected->maps[map].logicalBytes()),
                              0);
            EXPECT_EQ(source->report().renderedFrames, actual->report.frameReads);
            EXPECT_EQ(newExecution->operationCounters().discardedProgressReports, 0u);
            EXPECT_GT(source->report().readRequests, 0u);
            EXPECT_EQ(engine->memoryResource()->counters().liveBytes,
                      expected->image.capacityBytes() + expected->mask.capacityBytes() + actual->image.capacityBytes() +
                          actual->mask.capacityBytes() +
                          expected->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)].capacityBytes() +
                          expected->maps[static_cast<std::size_t>(an::StackOutputMap::Variance)].capacityBytes() +
                          actual->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)].capacityBytes() +
                          actual->maps[static_cast<std::size_t>(an::StackOutputMap::Variance)].capacityBytes());
        }
    }
}

TEST(DrizzleExecutorTest, ScaledCfaBandsCarryWeightsMasksCoverageAndGlobalCropPhase)
{
    for (double scale : {1.0, 2.0, 4.0})
    {
        auto request = modelFor(an::DrizzleKernel::Point).request();
        auto description = request.input.toRequest();
        description.numericDomain = an::NumericDomain::RawSensorCode;
        description.cfa = *an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 1u);
        request.input = *an::ImageDescriptor::create(description);
        request.frameCount = 1u;
        request.parameters.scale = scale;
        request.parameters.channelWeights = {2.0, 3.0, 5.0};
        request.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        request.maskExtent = request.input.extent();
        auto auxiliary = description;
        auxiliary.cfa = {};
        auxiliary.numericDomain = an::NumericDomain::NormalizedLinear;
        request.pixelWeights = *an::ImageDescriptor::create(auxiliary);
        request.flatWeights = request.pixelWeights;
        const auto model = *an::DrizzleModel::create(request);
        auto engine = an::EngineContext::create({.maxConcurrency = 3u});
        auto execution = an::ExecutionContext::create(*engine, {});
        const auto extent = request.input.extent();
        auto mask = an::ValidityMask::createAllValid(*engine, extent);
        ASSERT_TRUE(mask.hasValue());
        ASSERT_TRUE(mask->view()->setValid(0u, 1u, 1u, false));
        std::vector<float> samples(extent.sampleCount(), 16.0f), weights(samples.size(), 0.5f),
            flats(samples.size(), 2.0f);
        samples[0] = 0.0f;
        const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, execution->limits());
        const an::DrizzleFrameData data{
            {layout, std::as_bytes(std::span{samples}), an::SampleType::Float32},
            mask->view()->constView(),
            an::DrizzleSampleBand{layout, std::as_bytes(std::span{weights}), an::SampleType::Float32},
            an::DrizzleSampleBand{layout, std::as_bytes(std::span{flats}), an::SampleType::Float32}};
        const an::DrizzleFrameParameters frame{*an::DrizzleMapDescriptor::create(model, {}), {7.0, 11.0, 13.0, 1.0}};
        auto source = an::ResidentDrizzleFrameSource::create({&data, 1u}, an::DrizzleSourceAccess::Region);
        ASSERT_TRUE(source.hasValue());
        an::DrizzleExecutionRequest run;
        run.planning.maximumSourceRows = 1u;
        run.planning.maximumDestinationRows = 3u;
        auto result = an::executeDrizzle(*engine, *execution, model, *source, {&frame, 1u}, run);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        const auto& output = result->descriptor.extent();
        EXPECT_EQ(result->descriptor.cfa().pattern(), an::CfaPattern::None);
        EXPECT_EQ(result->report.validOutputSamples, extent.sampleCount() - 1u);
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                const auto channel = static_cast<std::uint32_t>(description.cfa.colorAt(x, y));
                const auto ox = static_cast<std::uint32_t>(std::round((x + 0.5) * scale - 0.5));
                const auto oy = static_cast<std::uint32_t>(std::round((y + 0.5) * scale - 0.5));
                if (ox >= output.width() || oy >= output.height())
                    continue;
                const auto offset = channel * output.pixelCount() + oy * output.width() + ox;
                const bool valid = x != 1u || y != 1u;
                EXPECT_FLOAT_EQ(static_cast<const float*>(result->image.data())[offset],
                                valid ? samples[y * extent.width() + x] / static_cast<float>(scale * scale) : 0.0f);
                EXPECT_FLOAT_EQ(
                    static_cast<const float*>(result->weightMap.data())[offset],
                    valid ? static_cast<float>(request.parameters.channelWeights[channel] * frame.weights[channel])
                          : 0.0f);
                EXPECT_EQ(static_cast<const std::uint32_t*>(result->coverageMap.data())[offset], valid ? 1u : 0u);
            }
    }
}

TEST(DrizzleExecutorTest, NonFiniteAndOverflowFailuresPreserveAnExistingArtifact)
{
    const auto model = modelFor(an::DrizzleKernel::Point);
    Fixture fixture(model);
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data);
    auto first = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames);
    ASSERT_TRUE(first.hasValue());
    const auto live = engine->memoryResource()->counters().liveBytes;
    std::vector<std::byte> snapshot(first->image.logicalBytes());
    std::memcpy(snapshot.data(), first->image.data(), snapshot.size());
    fixture.samples.back().back() = std::numeric_limits<float>::quiet_NaN();
    auto failed = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames);
    EXPECT_EQ(failed.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(std::memcmp(first->image.data(), snapshot.data(), snapshot.size()), 0);
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, live);
    auto excludedRequest = model.request();
    excludedRequest.parameters.nonFinite = an::DrizzleNonFinitePolicy::Exclude;
    auto excludedModel = *an::DrizzleModel::create(excludedRequest);
    {
        auto result = an::executeDrizzle(*engine, *execution, excludedModel, *source, fixture.frames);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->report.nonFinite, 1u);
        EXPECT_EQ(result->report.excluded, 1u);
    }
    fixture.samples.back().back() = 0.5f;
    for (auto& frame : fixture.frames)
        frame.weights[0] = 1e100;
    failed = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames);
    EXPECT_EQ(failed.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, live);
}

TEST(DrizzleExecutorTest, CurvedProjectiveSipGeometryRetainsFullResultUnderBandTiling)
{
    auto model = modelFor();
    Fixture fixture(model);
    an::RegistrationTransformRequest transform;
    transform.linearKind = an::RegistrationTransformKind::Projective;
    transform.sourceToReference = {1.01, -0.03, 0.25, 0.02, 0.98, -0.25, 0.0001, -0.0002, 1.0};
    an::SipDistortionRequest sip;
    sip.referencePixel = {9.5, 5.5};
    sip.forwardX.order = sip.forwardY.order = 2u;
    sip.forwardX.coefficients[2][0] = 0.0001;
    sip.forwardY.coefficients[1][1] = -0.0002;
    const an::DrizzleMappingRequest mapping{.transform = *an::RegistrationTransform::create(transform),
                                            .sourceDistortion = *an::SipDistortion::create(sip)};
    for (auto& frame : fixture.frames)
        frame.geometry = *an::DrizzleMapDescriptor::create(model, mapping);
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    an::DrizzleExecutionRequest request;
    request.planning.mapStorage = an::DrizzleMapStorage::Dense;
    auto dense = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames, request);
    ASSERT_TRUE(dense.hasValue());
    request.planning.mapStorage = an::DrizzleMapStorage::BoundedRows;
    request.planning.maximumSourceRows = 1u;
    request.planning.maximumDestinationRows = 2u;
    auto bounded = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames, request);
    ASSERT_TRUE(bounded.hasValue());
    sameOutput(*dense, *bounded, 0.0);
    EXPECT_GT(bounded->report.geometry.sipEvaluations, dense->report.geometry.sipEvaluations);
}

TEST(DrizzleExecutorTest, SourceCalibrationAndPreparedLocalGlobalNormalizationPrecedeMapping)
{
    auto request = modelFor(an::DrizzleKernel::Point).request();
    request.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    const auto model = *an::DrizzleModel::create(request);
    Fixture fixture(model);
    for (auto& frame : fixture.frames)
        frame.geometry = *an::DrizzleMapDescriptor::create(model, {.postTranslation = {1.0, 2.0}});
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto& descriptor = request.input;
    const auto& extent = descriptor.extent();
    const auto& layout = fixture.data[0].pixels.layout;
    std::vector<float> bias(extent.sampleCount());
    for (std::size_t i = 0u; i < bias.size(); ++i)
        bias[i] = static_cast<float>(i) / 1024.0f;
    const auto master = an::CalibrationFrameView::create(bias.data(), bias.size() * sizeof(float), layout, descriptor);
    ASSERT_TRUE(master.hasValue());
    const an::CalibrationMasters masters{.bias = *master};
    auto calibration = an::prepareImageCalibration(*execution, descriptor, masters, {});
    ASSERT_TRUE(calibration.hasValue()) << calibration.status().message();
    an::StackRequest sourceRequest{.input = {3u, descriptor}};
    sourceRequest.normalization.scope = an::StackNormalizationScope::LocalThenGlobal;
    sourceRequest.normalization.globalMethod = an::NormalizationMethod::Additive;
    const auto sourceModel = an::makeStackModel(sourceRequest);
    ASSERT_TRUE(sourceModel.hasValue());
    an::LocalNormalizationDocument document;
    document.request.gridSize = 2u;
    document.shape = {.gridSize = 2u, .width = extent.width(), .height = extent.height(), .channelCount = 1u};
    document.referenceIndex = 0u;
    document.validity = an::ValidityRule::FiniteOnly;
    document.referenceBackground.assign(document.shape.valueCount(), 0.0);
    std::array<double, 6u> table{};
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        an::LocalNormalizationFrameCoefficients record;
        record.frameIndex = frame;
        record.background.assign(document.shape.valueCount(), 0.1 * frame);
        an::FrameNormalization match;
        match.method = an::NormalizationMethod::Additive;
        match.estimator = sourceRequest.normalization.scaleEstimator;
        match.channelCount = 1u;
        match.channels[0] = {.scale = 1.0, .offset = 0.03 * frame};
        record.global = match;
        document.frames.push_back(std::move(record));
        ASSERT_TRUE(an::storeStackFrameNormalization(*sourceModel, frame, match, table).ok());
    }
    const an::StackSourceContract contract{.state = an::StackFrameState::Raw,
                                           .calibration = {&*calibration, 1u},
                                           .masters = &masters,
                                           .localNormalization = &document};
    ASSERT_TRUE(an::checkStackSourceContract(*sourceModel, contract).hasValue());
    class PreparedSource final : public an::DrizzleFrameSource
    {
    public:
        PreparedSource(const Fixture& fixture, const an::StackModel& model, const an::StackSourceContract& contract,
                       std::span<const double> table)
            : fixture(fixture), model(model), contract(contract), table(table)
        {
        }
        an::DrizzleSourceAccess access() const noexcept override
        {
            return an::DrizzleSourceAccess::Region;
        }
        an::Result<an::DrizzleFrameData> read(const an::ExecutionContext& execution, std::uint32_t frame,
                                              std::uint32_t first, std::uint32_t rows,
                                              const an::DrizzleReadBuffers& buffers) override
        {
            auto band = an::drizzleSourceRows(fixture.data[frame], first, rows);
            if (!band.hasValue())
                return band.status();
            const an::StackSourceBand stored{
                .samples = band->pixels.samples.data(),
                .layout = &band->pixels.layout,
                .frameIndex = frame,
                .firstRow = first,
                .validity = {.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})}};
            const std::array<std::span<std::byte>, 1u> scratch{buffers.workspace};
            const auto converted =
                an::convertStackSourceBand(execution, model, contract, stored, table, scratch,
                                           {.samples = buffers.pixels.data(), .layout = &band->pixels.layout});
            if (!converted.hasValue())
                return converted.status();
            EXPECT_TRUE(converted->stages.calibrate && converted->stages.normalizeLocally &&
                        converted->stages.normalizeGlobally);
            band->pixels.samples = buffers.pixels.first(band->pixels.layout.totalBytes());
            ++reads;
            return band;
        }
        const Fixture& fixture;
        const an::StackModel& model;
        const an::StackSourceContract& contract;
        std::span<const double> table;
        std::uint32_t reads = 0u;
    } source(fixture, *sourceModel, contract, table);
    an::DrizzleExecutionRequest run;
    run.planning.maximumSourceRows = 2u;
    run.planning.maximumDestinationRows = 3u;
    run.planning.sourceWorkspaceBytes = an::stackScratchBytesPerWorker(*sourceModel);
    run.planning.preparedStateBytes = bias.size() * sizeof(float) + sizeof(document) +
                                      document.frames.capacity() * sizeof(an::LocalNormalizationFrameCoefficients) +
                                      (document.frames.size() + 1u) * document.shape.valueCount() * sizeof(double) +
                                      3u * layout.totalBytes() + sizeof(table) + sizeof(an::CalibrationPlan);
    auto result = an::executeDrizzle(*engine, *execution, model, source, fixture.frames, run);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(source.reads, result->report.plan.io.readRequests);
    EXPECT_LE(engine->memoryResource()->counters().peakBytes, result->report.plan.memory.peakBytes);
    const auto* values = static_cast<const float*>(result->image.data());
    for (std::uint32_t y = 0u; y < extent.height(); ++y)
        for (std::uint32_t x = 0u; x < extent.width(); ++x)
        {
            double expected = 0.0;
            if (x >= 1u && y >= 2u)
            {
                const auto origin = static_cast<std::size_t>(y - 2u) * extent.width() + x - 1u;
                for (std::uint32_t frame = 0u; frame < 3u; ++frame)
                {
                    const float calibrated = fixture.samples[frame][origin] - bias[origin];
                    const float local = static_cast<float>(calibrated - 0.1 * frame);
                    expected += static_cast<float>(local + 0.03 * frame);
                }
                expected /= 3.0;
            }
            EXPECT_NEAR(values[y * extent.width() + x], expected, 1e-7);
        }
}

TEST(DrizzleExecutorTest, PlanarRgbBorrowKeepsFullPlaneStridesAcrossShortBands)
{
    auto request = modelFor(an::DrizzleKernel::Point).request();
    auto description = request.input.toRequest();
    description.extent = *an::ImageExtent::create(19u, 11u, 3u, an::defaultEngineLimits());
    description.channelSchema = an::ChannelSchema::Rgb;
    request.input = *an::ImageDescriptor::create(description);
    request.frameCount = 2u;
    request.parameters.channelWeights = {2.0, 3.0, 5.0};
    const auto model = *an::DrizzleModel::create(request);
    Fixture fixture(model);
    for (auto& frame : fixture.frames)
        frame.geometry = *an::DrizzleMapDescriptor::create(model, {});
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
    ASSERT_TRUE(source.hasValue());
    auto engine = an::EngineContext::create({.maxConcurrency = 3u});
    auto execution = an::ExecutionContext::create(*engine, {});
    an::DrizzleExecutionRequest run;
    run.planning.maximumSourceRows = 2u;
    run.planning.maximumDestinationRows = 3u;
    auto result = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames, run);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const auto* pixels = static_cast<const float*>(result->image.data());
    const auto* weights = static_cast<const float*>(result->weightMap.data());
    const auto* coverage = static_cast<const std::uint32_t*>(result->coverageMap.data());
    for (std::size_t i = 0u; i < description.extent.sampleCount(); ++i)
    {
        EXPECT_FLOAT_EQ(pixels[i], (fixture.samples[0][i] + fixture.samples[1][i]) / 2.0f);
        EXPECT_FLOAT_EQ(weights[i], static_cast<float>(
                                        2.0 * request.parameters.channelWeights[i / description.extent.pixelCount()]));
        EXPECT_EQ(coverage[i], 2u);
    }
}

TEST(DrizzleExecutorTest, SourceWindowsKeepTheLimitsAlreadyProvedByTheirLayout)
{
    auto limits = an::defaultEngineLimits();
    limits.maxImageWidth += 1u;
    const auto extent = *an::ImageExtent::create(limits.maxImageWidth, 2u, 1u, limits);
    const auto layout = *an::ImageLayout::create({extent, 1u, 1u, 1u, 1u}, limits);
    std::vector<std::byte> pixels(layout.totalBytes());
    const an::DrizzleFrameData frame{{layout, pixels, an::SampleType::UInt8}};
    auto source = an::ResidentDrizzleFrameSource::create({&frame, 1u}, an::DrizzleSourceAccess::Region);
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    const auto cropped = an::drizzleSourceRows(frame, 1u, 1u);
    ASSERT_TRUE(cropped.hasValue()) << cropped.status().message();
    EXPECT_EQ(cropped->pixels.samples.data(), pixels.data() + layout.rowStrideBytes());
    EXPECT_EQ(cropped->pixels.layout.extent().width(), limits.maxImageWidth);
    EXPECT_EQ(cropped->pixels.layout.extent().height(), 1u);
}

TEST(DrizzleExecutorTest, PeakSumUsesJointSupportAndOnePeakAcrossDestinationBands)
{
    auto request = modelFor(an::DrizzleKernel::Point).request();
    auto description = request.input.toRequest();
    description.extent = *an::ImageExtent::create(19u, 11u, 3u, an::defaultEngineLimits());
    description.channelSchema = an::ChannelSchema::Rgb;
    request.input = *an::ImageDescriptor::create(description);
    request.output.combination = an::DrizzleCombination::PeakNormalizedSum;
    const auto model = *an::DrizzleModel::create(request);
    Fixture fixture(model);
    for (std::size_t frame = 0u; frame < fixture.frames.size(); ++frame)
    {
        fixture.frames[frame].geometry =
            *an::DrizzleMapDescriptor::create(model, {.postTranslation = {static_cast<double>(frame), 0.0}});
        fixture.frames[frame].weights[0] = static_cast<double>(frame + 1u);
    }
    // The maximum lives in the tail of a four-sample scan and in the last channel/band.
    fixture.samples[0].back() = 8.0f;
    auto engine = an::EngineContext::create({.maxConcurrency = 4u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
    an::DrizzleExecutionRequest run;
    run.planning.maximumDestinationRows = 1u;
    run.planning.maximumSourceRows = 2u;
    auto result = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames, run);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    std::array<double, 19u * 11u * 3u> sums{};
    std::array<double, 19u * 11u * 3u> weights{};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
        for (std::size_t y = 0u; y < 11u; ++y)
            for (std::size_t x = 0u; x < 19u; ++x)
                for (std::size_t frame = 0u; frame < 3u; ++frame)
                    if (x >= frame)
                    {
                        const auto index = channel * 19u * 11u + y * 19u + x;
                        sums[index] += fixture.samples[frame][index - frame] * fixture.frames[frame].weights[channel];
                        weights[index] += fixture.frames[frame].weights[channel];
                    }
    const double peak = *std::max_element(sums.begin(), sums.end());
    for (std::size_t i = 0u; i < sums.size(); ++i)
    {
        EXPECT_FLOAT_EQ(static_cast<const float*>(result->image.data())[i], static_cast<float>(sums[i] / peak));
        EXPECT_FLOAT_EQ(static_cast<const float*>(result->weightMap.data())[i], static_cast<float>(weights[i]));
    }
    EXPECT_EQ(result->report.plan.memory.finalizationBytes, sums.size() * sizeof(double));
    EXPECT_LE(execution->memoryResource()->counters().peakBytes, result->report.plan.memory.peakBytes);
    EXPECT_EQ(an::DrizzledStackFrameSource::create(*engine, model, *source, fixture.frames).status().code(),
              an::ErrorCode::IncompatibleArguments);
    an::ImageMetadata inputMetadata;
    const auto transition = an::drizzleTransition(model, inputMetadata.semantics());
    ASSERT_TRUE(transition.hasValue());
    const auto metadata = an::applyMetadataTransition(inputMetadata, *transition);
    ASSERT_TRUE(metadata.hasValue());
    EXPECT_TRUE(metadata->semantics().processingState().normalized);
    EXPECT_TRUE(metadata->semantics().processingState().registered);
}

TEST(DrizzleExecutorTest, PeakSumNormalizesBeforeNarrowingAndRejectsUnknownCombination)
{
    auto request = modelFor(an::DrizzleKernel::Point).request();
    request.output.combination = static_cast<an::DrizzleCombination>(255u);
    EXPECT_EQ(an::DrizzleModel::create(request).status().code(), an::ErrorCode::InvalidArgument);
    request.output.combination = an::DrizzleCombination::PeakNormalizedSum;
    request.output.weightMap = false;
    const auto model = *an::DrizzleModel::create(request);
    Fixture fixture(model);
    for (std::size_t frame = 0u; frame < fixture.frames.size(); ++frame)
    {
        fixture.frames[frame].geometry = *an::DrizzleMapDescriptor::create(model, {});
        fixture.frames[frame].weights[0] = 1.0e100;
        std::fill(fixture.samples[frame].begin(), fixture.samples[frame].end(), 0.5f);
        fixture.samples[frame][0] = -0.5f;
    }
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
    auto result = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const auto* values = static_cast<const float*>(result->image.data());
    EXPECT_FLOAT_EQ(values[0], 0.0f);
    for (std::size_t i = 1u; i < fixture.samples[0].size(); ++i)
        EXPECT_FLOAT_EQ(values[i], 1.0f);
}

TEST(DrizzleExecutorTest, FailureRetainsCurrentReadAndGeometryDiagnosticsWithoutAnArtifact)
{
    const auto model = modelFor(an::DrizzleKernel::Point);
    Fixture fixture(model);
    RecordingSource source(fixture.data);
    source.failAt = 2u;
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    an::DrizzleExecutionReport diagnostics;
    diagnostics.readRequests = 999u;
    an::DrizzleExecutionRequest run;
    run.planning.maximumSourceRows = 2u;
    run.diagnostics = &diagnostics;
    const auto result = an::executeDrizzle(*engine, *execution, model, source, fixture.frames, run);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(diagnostics.readRequests, 1u);
    EXPECT_GT(diagnostics.geometry.mapped, 0u);
    EXPECT_EQ(diagnostics.outputBytes, 0u);
    EXPECT_EQ(execution->memoryResource()->counters().liveBytes, 0u);
    run.artifact = {1u, 0u};
    EXPECT_FALSE(an::executeDrizzle(*engine, *execution, model, source, fixture.frames, run).hasValue());
    EXPECT_EQ(diagnostics.readRequests, 0u);
    EXPECT_EQ(diagnostics.geometry.mapped, 0u);
}

TEST(DrizzleExecutorTest, PeakSumKeepsNonpositiveValuesAndReusesDoubleOutputWithinTheExactBudget)
{
    for (const auto type : {an::SampleType::Float32, an::SampleType::Float64})
    {
        auto request =
            modelFor(an::DrizzleKernel::Point, an::SampleType::Float32, an::NumericDomain::RawSensorCode).request();
        request.output.combination = an::DrizzleCombination::PeakNormalizedSum;
        request.output.sampleType = type;
        const auto model = *an::DrizzleModel::create(request);
        EXPECT_EQ(model.outputDescriptor().numericDomain(), an::NumericDomain::NormalizedLinear);
        Fixture fixture(model);
        for (std::size_t frame = 0u; frame < fixture.frames.size(); ++frame)
        {
            fixture.frames[frame].geometry = *an::DrizzleMapDescriptor::create(model, {});
            std::fill(fixture.samples[frame].begin(), fixture.samples[frame].end(), -2.0f);
            fixture.samples[frame][0] = 0.0f;
        }
        auto source = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
        auto planningEngine = an::EngineContext::create({.maxConcurrency = 2u});
        auto planningExecution = an::ExecutionContext::create(*planningEngine, {});
        an::DrizzleExecutionRequest run;
        run.planning.access = an::DrizzleSourceAccess::Region;
        run.planning.maximumSourceRows = run.planning.maximumDestinationRows = 2u;
        const auto plan = an::planDrizzle(*planningExecution, model, run.planning);
        ASSERT_TRUE(plan.hasValue());
        an::EngineContextOptions options;
        options.maxConcurrency = 2u;
        options.limits.memoryBudgetBytes = plan->memory.peakBytes;
        auto engine = an::EngineContext::create(options);
        auto execution = an::ExecutionContext::create(*engine, {});
        auto result = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames, run);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(result->report.plan.memory.finalizationBytes,
                  type == an::SampleType::Float64 ? 0u : request.input.extent().sampleCount() * sizeof(double));
        const auto value = [&](std::size_t index) {
            return type == an::SampleType::Float64 ? static_cast<const double*>(result->image.data())[index]
                                                   : static_cast<const float*>(result->image.data())[index];
        };
        EXPECT_EQ(value(0u), 0.0);
        EXPECT_EQ(value(1u), -6.0);
        EXPECT_EQ(result->report.validOutputSamples, request.input.extent().sampleCount());
        EXPECT_LE(execution->memoryResource()->counters().peakBytes, options.limits.memoryBudgetBytes);
        run.planning.workingSetLimitBytes = plan->memory.peakBytes - 1u;
        run.planning.maximumSourceRows = run.planning.maximumDestinationRows = 1u;
        // The smallest plan, not a larger preferred tile, defines the actual refusal boundary.
        const auto minimum = an::planDrizzle(*planningExecution, model, run.planning);
        ASSERT_TRUE(minimum.hasValue());
        run.planning.workingSetLimitBytes = minimum->memory.peakBytes - 1u;
        EXPECT_EQ(an::planDrizzle(*planningExecution, model, run.planning).status().code(),
                  an::ErrorCode::MemoryBudgetExceeded);
    }
}

TEST(DrizzleExecutorTest, CancellationAtSumFinalizationRetainsDiagnosticsAndPublishesNothing)
{
    auto request = modelFor(an::DrizzleKernel::Point).request();
    request.output.combination = an::DrizzleCombination::PeakNormalizedSum;
    const auto model = *an::DrizzleModel::create(request);
    Fixture fixture(model);
    auto source = an::ResidentDrizzleFrameSource::create(fixture.data, an::DrizzleSourceAccess::Region);
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    std::stop_source stop;
    Progress progress;
    progress.stop = &stop;
    progress.cancelAt = 4u; // Three source reads and the completed destination band.
    auto execution = an::ExecutionContext::create(*engine, {.stopToken = stop.get_token(), .progressSink = &progress});
    an::DrizzleExecutionReport diagnostics;
    an::DrizzleExecutionRequest run;
    run.diagnostics = &diagnostics;
    const auto result = an::executeDrizzle(*engine, *execution, model, *source, fixture.frames, run);
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(diagnostics.readRequests, 3u);
    EXPECT_GT(diagnostics.geometry.mapped, 0u);
    EXPECT_EQ(diagnostics.outputBytes, 0u);
    EXPECT_EQ(execution->memoryResource()->counters().liveBytes, 0u);
}

} // namespace
