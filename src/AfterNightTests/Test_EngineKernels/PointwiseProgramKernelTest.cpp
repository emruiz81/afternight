// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for Algorithms/Pointwise/PointwiseProgramKernel.h.
//
// A fused program reaches a device as one kernel whose arithmetic travels as its arguments, so a caller and
// a backend have to agree on values that neither of them runs: the plan read off a program, the contract a
// backend advertises for one shape, the program carried by address, and the check every backend applies to
// a request before it lowers a chain. These cases prove those values without a device;
// DevicePointwiseProgramTest submits such a kernel through the checked facade.

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::ImageLayout planarLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t channels, std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width + rowPaddingSamples) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::PointwiseProgram built(const an::PointwiseProgramBuilder& builder)
{
    an::Result<an::PointwiseProgram> program = builder.build();
    EXPECT_TRUE(program.hasValue()) << program.status().message();
    return std::move(*program);
}

/// Brightness, a clamp, a gamma encode and a mask, over three channels and two sources.
[[nodiscard]] an::PointwiseProgram gammaChain()
{
    an::PointwiseProgramBuilder builder;
    builder.channels(3u)
        .sources(2u)
        .readingDomain(an::NumericDomain::NormalizedLinear)
        .writingDomain(an::NumericDomain::EncodedNonlinear)
        .append(an::pointwiseScale(1.1))
        .append(an::pointwiseBias(-0.0125))
        .append(an::pointwiseClamp(0.0, 1.0))
        .append(an::pointwiseGamma(1.0 / 2.2))
        .append(an::pointwiseMask(1u));
    return built(builder);
}

/// A single-channel chain with no power step, over Float64 storage computing in Float64.
[[nodiscard]] an::PointwiseProgram wideCurveChain()
{
    constexpr std::array<double, 3u> kCurve{0.0, 0.25, 1.0};
    an::PointwiseProgramBuilder builder;
    const std::uint32_t table = builder.addTable(kCurve);
    builder.readingSamples(an::SampleType::Float64)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64)
        .append(an::pointwiseScale(0.5))
        .append(an::pointwiseSampledCurve(table, 0.0, 1.0));
    return built(builder);
}

/// An exact lookup over sixteen-bit storage.
[[nodiscard]] an::PointwiseProgram integerLookup()
{
    std::vector<double> entries(65536u);
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        entries[index] = static_cast<double>(65535u - index);
    }
    an::PointwiseProgramBuilder builder;
    builder.readingSamples(an::SampleType::UInt16);
    const std::uint32_t table = builder.addTable(entries);
    builder.append(an::pointwiseLut(table));
    return built(builder);
}

[[nodiscard]] std::string parameterOf(const an::Status& status)
{
    const an::StatusContextField* const field = status.findContext(an::ContextKey::Parameter);
    return field != nullptr ? field->text : std::string{};
}

/// A request for one program over some layouts, which owns the bytes its arguments span.
struct ProgramRequest
{
    an::PointwiseProgramKernelArguments arguments{};
    std::vector<an::ImageLayout> layouts{};
    an::KernelPlanKey plan{};

    [[nodiscard]] an::DeviceKernelRequest request() const
    {
        return an::DeviceKernelRequest{
            .plan = plan, .bindingLayouts = layouts, .arguments = an::deviceKernelArguments(arguments)};
    }
};

TEST(PointwiseProgramKernelTest, ThePlanCarriesTheProgramsNumericContractAndTheWidestBufferChoices)
{
    const an::PointwiseProgram gamma = gammaChain();
    const an::KernelPlanKey plan = an::pointwiseProgramKernelPlan(gamma);
    EXPECT_EQ(plan.id, an::kPointwiseProgramKernelId);
    EXPECT_EQ(an::kernelFamilyOf(plan.id.operation), an::KernelFamily::Pointwise);
    EXPECT_EQ(plan.inputSample, an::SampleType::Float32);
    EXPECT_EQ(plan.outputSample, an::SampleType::Float32);
    EXPECT_EQ(plan.computeSample, an::SampleType::Float32);
    EXPECT_EQ(plan.inputDomain, an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(plan.outputDomain, an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(plan.inputArrangement, an::StorageArrangement::Planar);
    EXPECT_EQ(plan.outputArrangement, an::StorageArrangement::Planar);
    EXPECT_EQ(plan.channels, an::KernelChannelPolicy::MatchingChannels);
    EXPECT_EQ(plan.stride, an::KernelStridePolicy::RowStrided);
    EXPECT_EQ(plan.bufferAlignmentBytes, alignof(float));
    EXPECT_EQ(plan.alias, an::KernelAliasPolicy::Disjoint);
    EXPECT_EQ(plan.validity, an::ValidityRule::AllValid);
    EXPECT_EQ(plan.border, an::KernelBorderMode::NotApplicable);
    EXPECT_EQ(plan.coordinates, an::KernelCoordinateConvention::None);
    EXPECT_EQ(plan.scratch, an::KernelScratchPolicy::None);
    EXPECT_FALSE(plan.persistentPlan);
    // The power step is the one no instruction computes, so the program carries the tier every
    // implementation - the compiled host path included - is already permitted at that step.
    EXPECT_EQ(plan.accuracy, an::mathFunctionAccuracy(an::MathFunction::Pow));
    EXPECT_EQ(an::pointwiseProgramAccuracy(gamma), plan.accuracy);

    const an::KernelPlanKey widePlan = an::pointwiseProgramKernelPlan(wideCurveChain());
    EXPECT_EQ(widePlan.inputSample, an::SampleType::Float64);
    EXPECT_EQ(widePlan.outputSample, an::SampleType::Float64);
    EXPECT_EQ(widePlan.computeSample, an::SampleType::Float64);
    EXPECT_EQ(widePlan.outputDomain, an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(widePlan.channels, an::KernelChannelPolicy::SingleChannel);
    EXPECT_EQ(widePlan.bufferAlignmentBytes, alignof(double));
    EXPECT_EQ(widePlan.accuracy, an::KernelAccuracyContract{});

    const an::PointwiseProgram lookup = integerLookup();
    const an::KernelPlanKey lookupPlan = an::pointwiseProgramKernelPlan(lookup);
    EXPECT_EQ(lookupPlan.inputSample, an::SampleType::UInt16);
    EXPECT_EQ(lookupPlan.outputSample, an::SampleType::UInt16);
    EXPECT_EQ(lookupPlan.computeSample, an::SampleType::Float32);
    EXPECT_EQ(lookupPlan.bufferAlignmentBytes, alignof(std::uint16_t));
    EXPECT_EQ(an::pointwiseProgramAccuracy(lookup), an::KernelAccuracyContract{});
}

TEST(PointwiseProgramKernelTest, AContractSupportsThePlansOfItsOwnShapeAndNoOther)
{
    const an::KernelPlanKey plan = an::pointwiseProgramKernelPlan(gammaChain());
    const an::KernelContract contract = an::pointwiseProgramKernelContract(
        an::PointwiseProgramKernelShape{.inputDomain = an::NumericDomain::NormalizedLinear,
                                        .outputDomain = an::NumericDomain::EncodedNonlinear,
                                        .accuracy = an::mathFunctionAccuracy(an::MathFunction::Pow)});
    EXPECT_EQ(contract.id, an::kPointwiseProgramKernelId);
    EXPECT_STREQ(contract.referenceScenarioId, an::kPointwiseProgramReferenceScenario);
    EXPECT_EQ(contract.capabilities.inputSample, an::SampleType::Float32);
    EXPECT_EQ(contract.capabilities.computeSample, an::SampleType::Float32);
    EXPECT_EQ(contract.capabilities.channels, an::KernelChannelPolicy::MatchingChannels);
    EXPECT_EQ(contract.capabilities.strides, an::KernelStridePolicy::RowStrided);
    EXPECT_EQ(contract.capabilities.minimumAlignmentBytes, alignof(float));
    EXPECT_EQ(contract.capabilities.alias, an::KernelAliasPolicy::Disjoint);
    EXPECT_EQ(contract.capabilities.validity, an::KernelValidityCapability::AllValidOnly);
    EXPECT_EQ(contract.capabilities.borders, an::KernelBorderCapability::None);
    EXPECT_EQ(contract.capabilities.cancellation, an::KernelCancellationPolicy::None);
    EXPECT_TRUE(contract.capabilities.tails);
    EXPECT_FALSE(contract.capabilities.persistentPlan);
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    EXPECT_TRUE(an::kernelContractSupports(contract, an::kernelPlanFor(contract)));

    an::KernelPlanKey narrowed = plan;
    narrowed.stride = an::KernelStridePolicy::ContiguousOnly;
    narrowed.channels = an::KernelChannelPolicy::SingleChannel;
    EXPECT_TRUE(an::kernelContractSupports(contract, narrowed));

    // One domain pair per contract: the same arithmetic writing another domain is another contract.
    an::KernelPlanKey otherDomain = plan;
    otherDomain.outputDomain = an::NumericDomain::NormalizedLinear;
    EXPECT_FALSE(an::kernelContractSupports(contract, otherDomain));

    // One accuracy per contract: the power tier does not serve a plan that asks for exactness, and an exact
    // contract does not serve a plan that accepts the tier.
    an::KernelPlanKey exactPlan = plan;
    exactPlan.accuracy = an::KernelAccuracyContract{};
    EXPECT_FALSE(an::kernelContractSupports(contract, exactPlan));
    an::KernelContract exactContract = contract;
    exactContract.capabilities.accuracy = an::KernelAccuracyContract{};
    EXPECT_FALSE(an::kernelContractSupports(exactContract, plan));
    EXPECT_TRUE(an::kernelContractSupports(exactContract, exactPlan));

    an::KernelPlanKey finite = plan;
    finite.validity = an::ValidityRule::FiniteOnly;
    EXPECT_FALSE(an::kernelContractSupports(contract, finite));
    const an::KernelContract valueRules = an::pointwiseProgramKernelContract(
        an::PointwiseProgramKernelShape{.inputDomain = an::NumericDomain::NormalizedLinear,
                                        .outputDomain = an::NumericDomain::EncodedNonlinear,
                                        .validity = an::KernelValidityCapability::ValueRules,
                                        .accuracy = an::mathFunctionAccuracy(an::MathFunction::Pow)});
    EXPECT_TRUE(an::kernelContractSupports(valueRules, finite));

    EXPECT_FALSE(an::kernelContractSupports(contract, an::pointwiseProgramKernelPlan(wideCurveChain())));
    an::KernelPlanKey affine = plan;
    affine.id.operation = an::KernelOperation::PointwiseAffine;
    EXPECT_FALSE(an::kernelContractSupports(contract, affine));

    const an::KernelContract wide = an::pointwiseProgramKernelContract(an::PointwiseProgramKernelShape{
        .sampleType = an::SampleType::Float64, .computeSample = an::SampleType::Float64});
    EXPECT_EQ(wide.capabilities.minimumAlignmentBytes, alignof(double));
    EXPECT_TRUE(an::kernelContractSupports(wide, an::pointwiseProgramKernelPlan(wideCurveChain())));
}

TEST(PointwiseProgramKernelTest, TheArgumentsCarryTheProgramByAddressAndNothingElse)
{
    const an::PointwiseProgram program = gammaChain();
    const an::PointwiseProgramKernelArguments arguments{.program = &program};
    const std::span<const std::byte> bytes = an::deviceKernelArguments(arguments);
    EXPECT_EQ(bytes.size(), sizeof(an::PointwiseProgramKernelArguments));
    EXPECT_EQ(an::pointwiseProgramOf(bytes), &program);

    EXPECT_EQ(an::pointwiseProgramOf(bytes.first(bytes.size() - 1u)), nullptr);
    EXPECT_EQ(an::pointwiseProgramOf(std::span<const std::byte>{}), nullptr);
    const an::PointwiseProgramKernelArguments empty{};
    EXPECT_EQ(an::pointwiseProgramOf(an::deviceKernelArguments(empty)), nullptr);
    // A valid structure followed by more bytes is still not one structure, and must not be read as one.
    std::array<std::byte, 2u * sizeof(an::PointwiseProgramKernelArguments)> longer{};
    std::memcpy(longer.data(), bytes.data(), bytes.size());
    EXPECT_EQ(an::pointwiseProgramOf(longer), nullptr);
}

TEST(PointwiseProgramKernelTest, ARequestIsCheckedAgainstTheProgramItCarriesAndTheFirstReasonWins)
{
    const an::PointwiseProgram program = gammaChain();
    const ProgramRequest valid{.arguments = {.program = &program},
                               .layouts = {planarLayout(an::SampleType::Float32, 13u, 5u, 3u, 3u),
                                           planarLayout(an::SampleType::Float32, 13u, 5u, 3u, 1u),
                                           planarLayout(an::SampleType::Float32, 13u, 5u, 3u)},
                               .plan = an::pointwiseProgramKernelPlan(program)};
    const an::Status accepted = an::checkPointwiseProgramKernelRequest(valid.request());
    EXPECT_TRUE(accepted.ok()) << accepted.message();

    const auto expectRefused = [](const ProgramRequest& candidate, an::ErrorCode code, const char* parameter) {
        const an::Status status = an::checkPointwiseProgramKernelRequest(candidate.request());
        EXPECT_EQ(status.code(), code) << status.message();
        EXPECT_EQ(parameterOf(status), parameter) << status.message();
    };
    const auto expectAccepted = [](const ProgramRequest& candidate) {
        const an::Status status = an::checkPointwiseProgramKernelRequest(candidate.request());
        EXPECT_TRUE(status.ok()) << status.message();
    };

    ProgramRequest otherOperation = valid;
    otherOperation.plan.id.operation = an::KernelOperation::PointwiseAffine;
    expectRefused(otherOperation, an::ErrorCode::InvalidArgument, "plan");

    ProgramRequest noProgram = valid;
    noProgram.arguments.program = nullptr;
    expectRefused(noProgram, an::ErrorCode::InvalidArgument, "arguments");

    // Both at once: the operation is checked before the arguments.
    ProgramRequest both = otherOperation;
    both.arguments.program = nullptr;
    expectRefused(both, an::ErrorCode::InvalidArgument, "plan");

    ProgramRequest otherDomain = valid;
    otherDomain.plan.outputDomain = an::NumericDomain::NormalizedLinear;
    expectRefused(otherDomain, an::ErrorCode::IncompatibleArguments, "plan");
    ProgramRequest otherWidth = valid;
    otherWidth.plan.computeSample = an::SampleType::Float64;
    expectRefused(otherWidth, an::ErrorCode::IncompatibleArguments, "plan");
    ProgramRequest otherRepresentation = valid;
    otherRepresentation.plan.inputSample = an::SampleType::UInt16;
    expectRefused(otherRepresentation, an::ErrorCode::IncompatibleArguments, "plan");
    ProgramRequest oneChannel = valid;
    oneChannel.plan.channels = an::KernelChannelPolicy::SingleChannel;
    expectRefused(oneChannel, an::ErrorCode::IncompatibleArguments, "plan");

    // No binding carries a packed mask. A value rule is a plan the program can be placed with; whether a
    // kernel honours it is its contract's question, which the device answers before the backend is asked.
    ProgramRequest masked = valid;
    masked.plan.validity = an::ValidityRule::ExplicitMask;
    expectRefused(masked, an::ErrorCode::InvalidArgument, "validity");
    ProgramRequest finite = valid;
    finite.plan.validity = an::ValidityRule::FiniteOnly;
    expectAccepted(finite);

    // A power step cannot be promised exactly, nor tighter than its tier, nor in another metric - and the
    // code is the one a caller that may run on the host branches on.
    ProgramRequest exact = valid;
    exact.plan.accuracy = an::KernelAccuracyContract{};
    expectRefused(exact, an::ErrorCode::UnsupportedBackend, "accuracy");
    ProgramRequest tighter = valid;
    tighter.plan.accuracy = {.metric = an::KernelAccuracyMetric::Ulp, .maximumError = an::kVectorMathUlpTier - 1.0};
    expectRefused(tighter, an::ErrorCode::UnsupportedBackend, "accuracy");
    ProgramRequest absolute = valid;
    absolute.plan.accuracy = {.metric = an::KernelAccuracyMetric::Absolute, .maximumError = 1.0};
    expectRefused(absolute, an::ErrorCode::UnsupportedBackend, "accuracy");
    ProgramRequest looser = valid;
    looser.plan.accuracy = {.metric = an::KernelAccuracyMetric::Ulp, .maximumError = 2.0 * an::kVectorMathUlpTier};
    expectAccepted(looser);

    // An exact program fits every plan, whatever the plan accepts.
    const an::PointwiseProgram wide = wideCurveChain();
    ProgramRequest wideRequest{.arguments = {.program = &wide},
                               .layouts = {planarLayout(an::SampleType::Float64, 7u, 3u, 1u, 2u),
                                           planarLayout(an::SampleType::Float64, 7u, 3u, 1u)},
                               .plan = an::pointwiseProgramKernelPlan(wide)};
    wideRequest.plan.accuracy = an::mathFunctionAccuracy(an::MathFunction::Pow);
    expectAccepted(wideRequest);

    ProgramRequest missingDestination = valid;
    missingDestination.layouts.pop_back();
    expectRefused(missingDestination, an::ErrorCode::InvalidArgument, "bindingLayouts");
    ProgramRequest twoPlanes = valid;
    twoPlanes.layouts[1] = planarLayout(an::SampleType::Float32, 13u, 5u, 2u);
    expectRefused(twoPlanes, an::ErrorCode::IncompatibleArguments, "bindingLayouts");
    ProgramRequest wider = valid;
    wider.layouts[2] = planarLayout(an::SampleType::Float32, 14u, 5u, 3u);
    expectRefused(wider, an::ErrorCode::IncompatibleArguments, "bindingLayouts");
    ProgramRequest doubles = valid;
    doubles.layouts[0] = planarLayout(an::SampleType::Float64, 13u, 5u, 3u);
    expectRefused(doubles, an::ErrorCode::IncompatibleArguments, "bindingLayouts");

    // A plan that allows no row stride describes contiguous layouts only.
    ProgramRequest contiguous = valid;
    contiguous.plan.stride = an::KernelStridePolicy::ContiguousOnly;
    expectRefused(contiguous, an::ErrorCode::IncompatibleArguments, "bindingLayouts");
    contiguous.layouts = {planarLayout(an::SampleType::Float32, 13u, 5u, 3u),
                          planarLayout(an::SampleType::Float32, 13u, 5u, 3u),
                          planarLayout(an::SampleType::Float32, 13u, 5u, 3u)};
    expectAccepted(contiguous);
}

} // namespace
