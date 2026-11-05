// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for Algorithms/Kernel/KernelContract.h.
//
// A registry is useful only when it is total enough to plan against and specific enough that two
// implementations cannot claim the same dispatch key. These cases therefore test the vocabulary,
// every registration, capability matching, and the exact lookup key rather than merely asking
// whether the registry is non-empty.

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>

#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>

namespace
{

TEST(KernelVocabularyTest, EveryFamilyHasAStableName)
{
    constexpr std::array families{
        an::KernelFamily::Transfer,       an::KernelFamily::Pointwise, an::KernelFamily::Reduction,
        an::KernelFamily::HistogramOrder, an::KernelFamily::Stencil,   an::KernelFamily::Resample,
        an::KernelFamily::GatherScatter,  an::KernelFamily::Transform, an::KernelFamily::StackStream};
    std::set<std::string> names;
    for (const an::KernelFamily family : families)
    {
        EXPECT_TRUE(an::isKernelFamily(family));
        names.emplace(an::kernelFamilyName(family));
    }
    EXPECT_EQ(names.size(), families.size());
    EXPECT_FALSE(an::isKernelFamily(an::KernelFamily::Unknown));
    EXPECT_STREQ(an::kernelFamilyName(static_cast<an::KernelFamily>(255)), "UnknownKernelFamily");

    for (std::size_t index = 0u; index < families.size(); ++index)
    {
        EXPECT_EQ(static_cast<std::uint8_t>(families[index]), index + 1u);
    }
}

TEST(KernelVocabularyTest, OperationsMapToTheirMemoryAccessFamilies)
{
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::TransferCopy), an::KernelFamily::Transfer);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::PointwiseAffine), an::KernelFamily::Pointwise);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::ReductionSum), an::KernelFamily::Reduction);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::Histogram), an::KernelFamily::HistogramOrder);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::OrderStatistic), an::KernelFamily::HistogramOrder);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::Stencil3x3), an::KernelFamily::Stencil);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::ResampleNearest), an::KernelFamily::Resample);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::Gather), an::KernelFamily::GatherScatter);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::Scatter), an::KernelFamily::GatherScatter);
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::PointwiseProgram), an::KernelFamily::Pointwise);
    EXPECT_TRUE(an::isKernelOperation(an::KernelOperation::PointwiseProgram));
    EXPECT_STREQ(an::kernelOperationName(an::KernelOperation::PointwiseProgram), "PointwiseProgram");
    EXPECT_EQ(static_cast<std::uint16_t>(an::KernelOperation::PointwiseProgram), 22u);
    EXPECT_FALSE(an::isKernelOperation(static_cast<an::KernelOperation>(23)));
    EXPECT_EQ(an::kernelFamilyOf(an::KernelOperation::Unknown), an::KernelFamily::Unknown);
    EXPECT_STREQ(an::kernelOperationName(static_cast<an::KernelOperation>(999)), "UnknownKernelOperation");

    constexpr std::array operations{
        an::KernelOperation::TransferCopy,    an::KernelOperation::PointwiseAffine, an::KernelOperation::ReductionSum,
        an::KernelOperation::Histogram,       an::KernelOperation::OrderStatistic,  an::KernelOperation::Stencil3x3,
        an::KernelOperation::ResampleNearest, an::KernelOperation::Gather,          an::KernelOperation::Scatter};
    for (std::size_t index = 0u; index < operations.size(); ++index)
    {
        EXPECT_EQ(static_cast<std::uint16_t>(operations[index]), index + 1u);
    }
}

TEST(KernelVocabularyTest, AnOperationIdNeedsAKnownOperationAndANonzeroVersion)
{
    EXPECT_TRUE(an::isKernelId(an::KernelId{.operation = an::KernelOperation::TransferCopy, .version = 1u}));
    EXPECT_FALSE(an::isKernelId(an::KernelId{}));
    EXPECT_FALSE(an::isKernelId(an::KernelId{.operation = an::KernelOperation::TransferCopy, .version = 0u}));
    EXPECT_FALSE(an::isKernelId(an::KernelId{.operation = static_cast<an::KernelOperation>(999), .version = 1u}));
}

TEST(KernelRegistryTest, TheScalarRegistryContainsOneVersionedOracleForEverySkeletonOperation)
{
    const std::span<const an::KernelRegistration> registry = an::kernelRegistry();
    ASSERT_EQ(registry.size(), 9u);

    std::set<std::tuple<std::uint16_t, std::uint16_t, std::uint8_t, std::uint8_t>> dispatches;
    std::set<an::KernelOperation> operations;
    for (const an::KernelRegistration& registration : registry)
    {
        EXPECT_TRUE(an::isKernelId(registration.contract.id));
        EXPECT_EQ(registration.contract.id.version, 1u);
        EXPECT_EQ(registration.backend, an::ComputeBackend::Reference);
        EXPECT_EQ(registration.instructionSet, an::InstructionSet::Scalar);
        EXPECT_EQ(registration.contract.capabilities.inputSample, an::SampleType::Float32);
        const bool computesInFloat64 = registration.contract.id.operation == an::KernelOperation::ReductionSum ||
                                       registration.contract.id.operation == an::KernelOperation::Stencil3x3;
        EXPECT_EQ(registration.contract.capabilities.computeSample,
                  computesInFloat64 ? an::SampleType::Float64 : an::SampleType::Float32);
        EXPECT_EQ(registration.contract.capabilities.inputDomain, an::NumericDomain::NormalizedLinear);
        EXPECT_EQ(registration.contract.capabilities.inputArrangement, an::StorageArrangement::Planar);
        EXPECT_EQ(registration.contract.capabilities.strides, an::KernelStridePolicy::RowStrided);
        EXPECT_EQ(registration.contract.capabilities.minimumAlignmentBytes, alignof(float));
        EXPECT_EQ(registration.contract.capabilities.validity, an::KernelValidityCapability::ValueRulesAndMask);
        EXPECT_EQ(registration.contract.capabilities.accuracy.metric, an::KernelAccuracyMetric::Exact);
        EXPECT_EQ(registration.contract.capabilities.accuracy.maximumError, 0.0);
        EXPECT_TRUE(registration.contract.capabilities.tails);
        EXPECT_NE(registration.contract.referenceScenarioId, nullptr);
        EXPECT_NE(registration.contract.referenceScenarioId[0], '\0');

        const auto dispatch = std::tuple{
            static_cast<std::uint16_t>(registration.contract.id.operation), registration.contract.id.version,
            static_cast<std::uint8_t>(registration.backend), static_cast<std::uint8_t>(registration.instructionSet)};
        EXPECT_TRUE(dispatches.insert(dispatch).second);
        EXPECT_TRUE(operations.insert(registration.contract.id.operation).second);
    }
    EXPECT_EQ(operations.size(), 9u);
}

TEST(KernelRegistryTest, ACanonicalPlanRoundTripsThroughItsExactDispatchKey)
{
    for (const an::KernelRegistration& registration : an::kernelRegistry())
    {
        const an::KernelPlanKey plan = an::kernelPlanFor(registration.contract);
        ASSERT_TRUE(an::kernelContractSupports(registration.contract, plan));
        EXPECT_EQ(plan.bufferAlignmentBytes, registration.contract.capabilities.minimumAlignmentBytes);
        EXPECT_EQ(plan.coordinates, registration.contract.capabilities.coordinates);
        EXPECT_EQ(plan.scratch, registration.contract.capabilities.scratch);
        EXPECT_EQ(plan.persistentPlan, registration.contract.capabilities.persistentPlan);
        EXPECT_EQ(plan.accuracy, registration.contract.capabilities.accuracy);

        const an::KernelDispatchKey dispatch{
            .plan = plan, .backend = registration.backend, .instructionSet = registration.instructionSet};
        const an::KernelRegistration* found = an::findKernelRegistration(dispatch);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->contract.id, registration.contract.id);
    }
}

TEST(KernelRegistryTest, CapabilityMatchingRejectsAPlanThatChangesItsNumericOrMemoryContract)
{
    const an::KernelRegistration& transfer = an::kernelRegistry().front();
    an::KernelPlanKey plan = an::kernelPlanFor(transfer.contract);

    plan.inputSample = an::SampleType::UInt16;
    EXPECT_FALSE(an::kernelContractSupports(transfer.contract, plan));
    plan = an::kernelPlanFor(transfer.contract);
    plan.outputDomain = an::NumericDomain::EncodedNonlinear;
    EXPECT_FALSE(an::kernelContractSupports(transfer.contract, plan));
    plan = an::kernelPlanFor(transfer.contract);
    plan.inputArrangement = an::StorageArrangement::Interleaved;
    EXPECT_FALSE(an::kernelContractSupports(transfer.contract, plan));
    plan = an::kernelPlanFor(transfer.contract);
    plan.id.version = 2u;
    EXPECT_FALSE(an::kernelContractSupports(transfer.contract, plan));
}

TEST(KernelRegistryTest, WiderChannelCapabilitiesAcceptOnlyNarrowerKnownPlans)
{
    an::KernelContract contract = an::kernelRegistry().front().contract;
    an::KernelPlanKey plan = an::kernelPlanFor(contract);

    contract.capabilities.channels = an::KernelChannelPolicy::Any;
    plan.channels = an::KernelChannelPolicy::SingleChannel;
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    plan.channels = an::KernelChannelPolicy::MatchingChannels;
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    plan.channels = an::KernelChannelPolicy::Any;
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    plan.channels = static_cast<an::KernelChannelPolicy>(255);
    EXPECT_FALSE(an::kernelContractSupports(contract, plan));

    contract.capabilities.channels = an::KernelChannelPolicy::MatchingChannels;
    plan.channels = an::KernelChannelPolicy::SingleChannel;
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    plan.channels = an::KernelChannelPolicy::Any;
    EXPECT_FALSE(an::kernelContractSupports(contract, plan));

    contract.capabilities.channels = an::KernelChannelPolicy::SingleChannel;
    plan.channels = an::KernelChannelPolicy::MatchingChannels;
    EXPECT_FALSE(an::kernelContractSupports(contract, plan));
}

TEST(KernelRegistryTest, StrideAlignmentAliasValidityBorderCoordinatesScratchAndAccuracyArePartOfThePlan)
{
    const auto registrationFor = [](an::KernelOperation operation) -> const an::KernelRegistration& {
        for (const an::KernelRegistration& registration : an::kernelRegistry())
        {
            if (registration.contract.id.operation == operation)
            {
                return registration;
            }
        }
        return an::kernelRegistry().front();
    };

    const an::KernelRegistration& affine = registrationFor(an::KernelOperation::PointwiseAffine);
    an::KernelPlanKey affinePlan = an::kernelPlanFor(affine.contract);
    affinePlan.stride = an::KernelStridePolicy::ContiguousOnly;
    affinePlan.alias = an::KernelAliasPolicy::ExactInPlace;
    affinePlan.validity = an::ValidityRule::ExplicitMask;
    affinePlan.bufferAlignmentBytes = 16u;
    EXPECT_TRUE(an::kernelContractSupports(affine.contract, affinePlan));
    affinePlan.bufferAlignmentBytes = 2u;
    EXPECT_FALSE(an::kernelContractSupports(affine.contract, affinePlan));
    affinePlan.bufferAlignmentBytes = 3u;
    EXPECT_FALSE(an::kernelContractSupports(affine.contract, affinePlan));

    const an::KernelRegistration& stencil = registrationFor(an::KernelOperation::Stencil3x3);
    an::KernelPlanKey stencilPlan = an::kernelPlanFor(stencil.contract);
    stencilPlan.alias = an::KernelAliasPolicy::ExactInPlace;
    EXPECT_FALSE(an::kernelContractSupports(stencil.contract, stencilPlan));
    stencilPlan = an::kernelPlanFor(stencil.contract);
    stencilPlan.border = an::KernelBorderMode::Constant;
    EXPECT_TRUE(an::kernelContractSupports(stencil.contract, stencilPlan));
    stencilPlan.coordinates = an::KernelCoordinateConvention::PixelCenters;
    EXPECT_FALSE(an::kernelContractSupports(stencil.contract, stencilPlan));
    stencilPlan = an::kernelPlanFor(stencil.contract);
    stencilPlan.haloRadius = 0u;
    EXPECT_FALSE(an::kernelContractSupports(stencil.contract, stencilPlan));
    stencilPlan = an::kernelPlanFor(stencil.contract);
    stencilPlan.scratch = an::KernelScratchPolicy::CallerProvided;
    EXPECT_FALSE(an::kernelContractSupports(stencil.contract, stencilPlan));
    stencilPlan = an::kernelPlanFor(stencil.contract);
    stencilPlan.accuracy = an::KernelAccuracyContract{.metric = an::KernelAccuracyMetric::Ulp, .maximumError = 1.0};
    EXPECT_FALSE(an::kernelContractSupports(stencil.contract, stencilPlan));
}

TEST(KernelRegistryTest, ABackendMayBeatButNeverExceedThePlansAccuracyLimit)
{
    an::KernelContract contract = an::kernelRegistry().front().contract;
    contract.capabilities.accuracy =
        an::KernelAccuracyContract{.metric = an::KernelAccuracyMetric::Ulp, .maximumError = 2.0};
    an::KernelPlanKey plan = an::kernelPlanFor(contract);
    plan.accuracy.maximumError = 4.0;
    EXPECT_TRUE(an::kernelContractSupports(contract, plan));
    plan.accuracy.maximumError = 1.0;
    EXPECT_FALSE(an::kernelContractSupports(contract, plan));
    plan.accuracy.maximumError = -1.0;
    EXPECT_FALSE(an::kernelContractSupports(contract, plan));
    plan.accuracy =
        an::KernelAccuracyContract{.metric = static_cast<an::KernelAccuracyMetric>(255), .maximumError = 0.0};
    contract.capabilities.accuracy = plan.accuracy;
    EXPECT_FALSE(an::kernelContractSupports(contract, plan));
}

TEST(KernelRegistryTest, ADispatchLookupDoesNotSilentlyFallBack)
{
    const an::KernelRegistration& registration = an::kernelRegistry().front();
    an::KernelDispatchKey key{.plan = an::kernelPlanFor(registration.contract),
                              .backend = an::ComputeBackend::Cpu,
                              .instructionSet = an::InstructionSet::Scalar};
    EXPECT_EQ(an::findKernelRegistration(key), nullptr);

    key.backend = an::ComputeBackend::Reference;
    key.instructionSet = an::InstructionSet::Avx2;
    EXPECT_EQ(an::findKernelRegistration(key), nullptr);

    key.instructionSet = an::InstructionSet::Scalar;
    key.plan.id.version = 99u;
    EXPECT_EQ(an::findKernelRegistration(key), nullptr);
}

static_assert(std::is_trivially_copyable_v<an::KernelId>);
static_assert(std::is_trivially_copyable_v<an::KernelPlanKey>);
static_assert(std::is_standard_layout_v<an::KernelContract>);
static_assert(std::is_standard_layout_v<an::KernelRegistration>);

} // namespace

TEST(KernelContractTest, EveryBorderCapabilityProducesASupportedDefault)
{
    for (const auto borders : {an::KernelBorderCapability::None, an::KernelBorderCapability::ClampAndConstant,
                               an::KernelBorderCapability::EveryBorderMode})
    {
        auto contract = an::kernelRegistry().front().contract;
        contract.capabilities.borders = borders;
        EXPECT_TRUE(an::kernelContractSupports(contract, an::kernelPlanFor(contract)));
    }
}
