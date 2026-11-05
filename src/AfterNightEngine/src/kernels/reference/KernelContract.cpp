// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

// The immutable scalar registry and the capability comparison behind it.
//
// There is one registration per reference operation. A variant for another backend or instruction
// set repeats the operation ID under its own backend and instruction set, so uniqueness belongs to
// the complete dispatch key rather than to the operation alone. The reference scenario strings are
// stable machine-readable identities, not prose and not test names.

namespace an
{
namespace
{

/// Common Float32 single-plane capabilities of the scalar skeletons.
///
/// @return row-strided, mask-aware, exact scalar capabilities with no border or scratch.
[[nodiscard]] constexpr KernelCapabilities baseCapabilities() noexcept
{
    return KernelCapabilities{.inputSample = SampleType::Float32,
                              .outputSample = SampleType::Float32,
                              .computeSample = SampleType::Float32,
                              .inputDomain = NumericDomain::NormalizedLinear,
                              .outputDomain = NumericDomain::NormalizedLinear,
                              .inputArrangement = StorageArrangement::Planar,
                              .outputArrangement = StorageArrangement::Planar,
                              .channels = KernelChannelPolicy::SingleChannel,
                              .strides = KernelStridePolicy::RowStrided,
                              .minimumAlignmentBytes = static_cast<std::uint32_t>(alignof(float)),
                              .alias = KernelAliasPolicy::Disjoint,
                              .validity = KernelValidityCapability::ValueRulesAndMask,
                              .borders = KernelBorderCapability::None,
                              .haloRadius = 0u,
                              .coordinates = KernelCoordinateConvention::None,
                              .scratch = KernelScratchPolicy::None,
                              .parallelism = KernelParallelPolicy::Serial,
                              .cancellation = KernelCancellationPolicy::PerRowOrChunk,
                              .accuracy = KernelAccuracyContract{},
                              .tails = true,
                              .persistentPlan = false};
}

/// Capabilities for one operation, differing from the common shape only where the operation does.
///
/// @param operation the operation to describe.
/// @return its scalar capabilities.
[[nodiscard]] constexpr KernelCapabilities capabilitiesFor(KernelOperation operation) noexcept
{
    KernelCapabilities capabilities = baseCapabilities();
    switch (operation)
    {
    case KernelOperation::TransferCopy:
    case KernelOperation::PointwiseAffine:
        capabilities.alias = KernelAliasPolicy::ExactInPlace;
        break;
    case KernelOperation::ReductionSum:
        capabilities.outputSample = SampleType::Float64;
        capabilities.computeSample = SampleType::Float64;
        break;
    case KernelOperation::OrderStatistic:
        capabilities.scratch = KernelScratchPolicy::CallerProvided;
        break;
    case KernelOperation::Stencil3x3:
        capabilities.computeSample = SampleType::Float64;
        capabilities.borders = KernelBorderCapability::ClampAndConstant;
        capabilities.haloRadius = 1u;
        break;
    case KernelOperation::ResampleNearest:
    case KernelOperation::Gather:
        capabilities.borders = KernelBorderCapability::ClampAndConstant;
        capabilities.coordinates = KernelCoordinateConvention::PixelCenters;
        break;
    case KernelOperation::Scatter:
        capabilities.coordinates = KernelCoordinateConvention::PixelCenters;
        break;
    case KernelOperation::DemosaicBilinear:
    case KernelOperation::DemosaicFast:
    case KernelOperation::DemosaicRcd:
    case KernelOperation::DemosaicAmaze:
    case KernelOperation::DemosaicXTransFast:
    case KernelOperation::DemosaicMarkesteijn:
    case KernelOperation::DemosaicBorder:
    case KernelOperation::CfaSuperPixel:
    case KernelOperation::CfaBlockPlanes:
    case KernelOperation::DemosaicLmmse:
    case KernelOperation::DemosaicVng4:
    case KernelOperation::DemosaicLuminanceAhd:
    case KernelOperation::PointwiseProgram:
    case KernelOperation::Histogram:
    case KernelOperation::Unknown:
        break;
    }
    return capabilities;
}

/// One scalar registration.
///
/// @param operation the version-one operation.
/// @param scenarioId its stable oracle scenario.
/// @return the reference/scalar registration.
[[nodiscard]] constexpr KernelRegistration referenceRegistration(KernelOperation operation,
                                                                 const char* scenarioId) noexcept
{
    return KernelRegistration{.contract = KernelContract{.id = KernelId{.operation = operation, .version = 1u},
                                                         .capabilities = capabilitiesFor(operation),
                                                         .referenceScenarioId = scenarioId},
                              .backend = ComputeBackend::Reference,
                              .instructionSet = InstructionSet::Scalar};
}

/// Every authoritative scalar implementation, in stable operation order.
constexpr std::array kRegistry{
    referenceRegistration(KernelOperation::TransferCopy, "kernel.transfer.copy.float32.v1"),
    referenceRegistration(KernelOperation::PointwiseAffine, "kernel.pointwise.affine.float32.v1"),
    referenceRegistration(KernelOperation::ReductionSum, "kernel.reduction.sum.float32-float64.v1"),
    referenceRegistration(KernelOperation::Histogram, "kernel.histogram.float32.v1"),
    referenceRegistration(KernelOperation::OrderStatistic, "kernel.order-statistic.float32.v1"),
    referenceRegistration(KernelOperation::Stencil3x3, "kernel.stencil.3x3.float32.v1"),
    referenceRegistration(KernelOperation::ResampleNearest, "kernel.resample.nearest.float32.v1"),
    referenceRegistration(KernelOperation::Gather, "kernel.gather.nearest.float32.v1"),
    referenceRegistration(KernelOperation::Scatter, "kernel.scatter.nearest.float32.v1")};

/// Whether the plan's stride is no wider than the implementation's.
///
/// @param capability what the implementation accepts.
/// @param requested what the plan may use.
/// @return true when every requested stride is supported.
[[nodiscard]] constexpr bool supportsStride(KernelStridePolicy capability, KernelStridePolicy requested) noexcept
{
    if (capability == KernelStridePolicy::RowStrided)
    {
        return requested == KernelStridePolicy::ContiguousOnly || requested == KernelStridePolicy::RowStrided;
    }
    return capability == KernelStridePolicy::ContiguousOnly && requested == KernelStridePolicy::ContiguousOnly;
}

/// Whether the plan's overlap is no wider than the implementation's.
///
/// @param capability what the implementation accepts.
/// @param requested what the plan may use.
/// @return true when every requested overlap is supported.
[[nodiscard]] constexpr bool supportsAlias(KernelAliasPolicy capability, KernelAliasPolicy requested) noexcept
{
    if (capability == KernelAliasPolicy::ExactInPlace)
    {
        return requested == KernelAliasPolicy::Disjoint || requested == KernelAliasPolicy::ExactInPlace;
    }
    return capability == KernelAliasPolicy::Disjoint && requested == KernelAliasPolicy::Disjoint;
}

/// Whether the plan's channel constraint is no wider than the implementation's.
///
/// @param capability what the implementation accepts.
/// @param requested what the plan may contain.
/// @return true when every requested channel shape is supported.
[[nodiscard]] constexpr bool supportsChannels(KernelChannelPolicy capability, KernelChannelPolicy requested) noexcept
{
    switch (capability)
    {
    case KernelChannelPolicy::SingleChannel:
        return requested == KernelChannelPolicy::SingleChannel;
    case KernelChannelPolicy::MatchingChannels:
        return requested == KernelChannelPolicy::SingleChannel || requested == KernelChannelPolicy::MatchingChannels;
    case KernelChannelPolicy::Any:
        return requested == KernelChannelPolicy::SingleChannel || requested == KernelChannelPolicy::MatchingChannels ||
               requested == KernelChannelPolicy::Any;
    }
    return false;
}

/// Whether the plan's validity rule is one the implementation can read.
///
/// @param capability the widest validity mechanism implemented.
/// @param requested the input's rule.
/// @return true when the implementation can decide every sample under that rule.
[[nodiscard]] constexpr bool supportsValidity(KernelValidityCapability capability, ValidityRule requested) noexcept
{
    if (!isValidityRule(requested))
    {
        return false;
    }
    switch (capability)
    {
    case KernelValidityCapability::AllValidOnly:
        return requested == ValidityRule::AllValid;
    case KernelValidityCapability::ValueRules:
        return requested != ValidityRule::ExplicitMask;
    case KernelValidityCapability::ValueRulesAndMask:
        return true;
    }
    return false;
}

/// Whether the selected border is provided by the operation.
///
/// @param capability which choices exist.
/// @param requested which one the plan selected.
/// @return true when the pair agrees.
[[nodiscard]] constexpr bool supportsBorder(KernelBorderCapability capability, KernelBorderMode requested) noexcept
{
    switch (capability)
    {
    case KernelBorderCapability::None:
        return requested == KernelBorderMode::NotApplicable;
    case KernelBorderCapability::ClampAndConstant:
        return requested == KernelBorderMode::Clamp || requested == KernelBorderMode::Constant;
    case KernelBorderCapability::EveryBorderMode:
        return requested != KernelBorderMode::NotApplicable && isKernelBorderMode(requested);
    }
    return false;
}

/// Whether a plan guarantees at least the implementation's required alignment.
///
/// @param required the implementation requirement.
/// @param guaranteed what the plan guarantees.
/// @return true when both are powers of two and the guarantee is at least the requirement.
[[nodiscard]] constexpr bool supportsAlignment(std::uint32_t required, std::uint32_t guaranteed) noexcept
{
    return std::has_single_bit(required) && std::has_single_bit(guaranteed) && guaranteed >= required;
}

/// Whether an implementation's measured error fits the plan's accuracy requirement.
///
/// @param capability the implementation's worst permitted error.
/// @param requested the plan's largest accepted error.
/// @return true when the metrics agree and the implementation is no less accurate.
[[nodiscard]] bool supportsAccuracy(const KernelAccuracyContract& capability,
                                    const KernelAccuracyContract& requested) noexcept
{
    const auto isMetric = [](KernelAccuracyMetric metric) noexcept {
        switch (metric)
        {
        case KernelAccuracyMetric::Exact:
        case KernelAccuracyMetric::Ulp:
        case KernelAccuracyMetric::Absolute:
        case KernelAccuracyMetric::Relative:
            return true;
        }
        return false;
    };
    if (!isMetric(capability.metric) || !isMetric(requested.metric) || capability.metric != requested.metric ||
        !std::isfinite(capability.maximumError) || !std::isfinite(requested.maximumError) ||
        capability.maximumError < 0.0 || requested.maximumError < 0.0)
    {
        return false;
    }
    if (capability.metric == KernelAccuracyMetric::Exact)
    {
        return capability.maximumError == 0.0 && requested.maximumError == 0.0;
    }
    return capability.maximumError <= requested.maximumError;
}

} // namespace

const char* kernelFamilyName(KernelFamily family) noexcept
{
    switch (family)
    {
    case KernelFamily::Transfer:
        return "Transfer";
    case KernelFamily::Pointwise:
        return "Pointwise";
    case KernelFamily::Reduction:
        return "Reduction";
    case KernelFamily::HistogramOrder:
        return "HistogramOrder";
    case KernelFamily::Stencil:
        return "Stencil";
    case KernelFamily::Resample:
        return "Resample";
    case KernelFamily::GatherScatter:
        return "GatherScatter";
    case KernelFamily::Transform:
        return "Transform";
    case KernelFamily::StackStream:
        return "StackStream";
    case KernelFamily::Unknown:
        return "UnknownKernelFamily";
    }
    return "UnknownKernelFamily";
}

const char* kernelOperationName(KernelOperation operation) noexcept
{
    switch (operation)
    {
    case KernelOperation::TransferCopy:
        return "TransferCopy";
    case KernelOperation::PointwiseAffine:
        return "PointwiseAffine";
    case KernelOperation::ReductionSum:
        return "ReductionSum";
    case KernelOperation::Histogram:
        return "Histogram";
    case KernelOperation::OrderStatistic:
        return "OrderStatistic";
    case KernelOperation::Stencil3x3:
        return "Stencil3x3";
    case KernelOperation::ResampleNearest:
        return "ResampleNearest";
    case KernelOperation::Gather:
        return "Gather";
    case KernelOperation::Scatter:
        return "Scatter";
    case KernelOperation::DemosaicBilinear:
        return "DemosaicBilinear";
    case KernelOperation::DemosaicFast:
        return "DemosaicFast";
    case KernelOperation::DemosaicRcd:
        return "DemosaicRcd";
    case KernelOperation::DemosaicAmaze:
        return "DemosaicAmaze";
    case KernelOperation::DemosaicXTransFast:
        return "DemosaicXTransFast";
    case KernelOperation::DemosaicMarkesteijn:
        return "DemosaicMarkesteijn";
    case KernelOperation::DemosaicBorder:
        return "DemosaicBorder";
    case KernelOperation::CfaSuperPixel:
        return "CfaSuperPixel";
    case KernelOperation::CfaBlockPlanes:
        return "CfaBlockPlanes";
    case KernelOperation::DemosaicLmmse:
        return "DemosaicLmmse";
    case KernelOperation::DemosaicVng4:
        return "DemosaicVng4";
    case KernelOperation::DemosaicLuminanceAhd:
        return "DemosaicLuminanceAhd";
    case KernelOperation::PointwiseProgram:
        return "PointwiseProgram";
    case KernelOperation::Unknown:
        return "UnknownKernelOperation";
    }
    return "UnknownKernelOperation";
}

bool kernelContractSupports(const KernelContract& contract, const KernelPlanKey& plan) noexcept
{
    const KernelCapabilities& capabilities = contract.capabilities;
    return isKernelId(contract.id) && plan.id == contract.id && plan.inputSample == capabilities.inputSample &&
           plan.outputSample == capabilities.outputSample && plan.computeSample == capabilities.computeSample &&
           plan.inputDomain == capabilities.inputDomain && plan.outputDomain == capabilities.outputDomain &&
           plan.inputArrangement == capabilities.inputArrangement &&
           plan.outputArrangement == capabilities.outputArrangement &&
           supportsChannels(capabilities.channels, plan.channels) &&
           supportsStride(capabilities.strides, plan.stride) &&
           supportsAlignment(capabilities.minimumAlignmentBytes, plan.bufferAlignmentBytes) &&
           supportsAlias(capabilities.alias, plan.alias) && supportsValidity(capabilities.validity, plan.validity) &&
           supportsBorder(capabilities.borders, plan.border) && plan.haloRadius == capabilities.haloRadius &&
           plan.coordinates == capabilities.coordinates && plan.scratch == capabilities.scratch &&
           plan.persistentPlan == capabilities.persistentPlan && supportsAccuracy(capabilities.accuracy, plan.accuracy);
}

std::span<const KernelRegistration> kernelRegistry() noexcept
{
    return kRegistry;
}

const KernelRegistration* findKernelRegistration(const KernelDispatchKey& key) noexcept
{
    for (const KernelRegistration& registration : kRegistry)
    {
        if (registration.backend == key.backend && registration.instructionSet == key.instructionSet &&
            kernelContractSupports(registration.contract, key.plan))
        {
            return &registration;
        }
    }
    return nullptr;
}

} // namespace an
