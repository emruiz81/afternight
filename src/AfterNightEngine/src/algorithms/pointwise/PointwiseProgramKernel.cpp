// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a pointwise program as a kernel a backend can run. See PointwiseProgramKernel.h.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <algorithm>
#include <cstring>

namespace an
{
namespace
{

constexpr const char* kCheckOperation = "an::checkPointwiseProgramKernelRequest";

[[nodiscard]] Status requestFailure(ErrorCode code, const char* parameter, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = kCheckOperation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] SampleType computeSampleOf(const PointwiseProgram& program) noexcept
{
    return program.precision() == ComputePrecision::Float64 ? SampleType::Float64 : SampleType::Float32;
}

/// Whether an implementation held to the program's accuracy meets what a plan accepts. An exact result meets
/// every plan; any other has to be stated in the plan's own metric and within its limit.
[[nodiscard]] bool accuracyFits(const KernelAccuracyContract& program, const KernelAccuracyContract& plan) noexcept
{
    if (program.metric == KernelAccuracyMetric::Exact)
    {
        return true;
    }
    return program.metric == plan.metric && program.maximumError <= plan.maximumError;
}

/// Whether a layout holds one plane per channel of the program's primary representation.
[[nodiscard]] bool describesProgramImage(const ImageLayout& layout, const PointwiseProgram& program) noexcept
{
    const std::uint32_t channels = program.channelCount();
    return layout.elementSizeBytes() == sampleTypeSizeBytes(program.sourceSampleType()) &&
           layout.samplesPerPixel() == 1u && layout.planeCount() == channels &&
           layout.extent().channelCount() == channels;
}

} // namespace

KernelAccuracyContract pointwiseProgramAccuracy(const PointwiseProgram& program) noexcept
{
    const bool raisesToAPower = std::ranges::any_of(
        program.steps(), [](const PointwiseOp& step) noexcept { return step.kind == PointwiseOpKind::Gamma; });
    return raisesToAPower ? mathFunctionAccuracy(MathFunction::Pow) : KernelAccuracyContract{};
}

KernelPlanKey pointwiseProgramKernelPlan(const PointwiseProgram& program) noexcept
{
    const SampleType sample = program.sourceSampleType();
    return KernelPlanKey{.id = kPointwiseProgramKernelId,
                         .inputSample = sample,
                         .outputSample = sample,
                         .computeSample = computeSampleOf(program),
                         .inputDomain = program.inputDomain(),
                         .outputDomain = program.outputDomain(),
                         .inputArrangement = StorageArrangement::Planar,
                         .outputArrangement = StorageArrangement::Planar,
                         .channels = program.channelCount() == 1u ? KernelChannelPolicy::SingleChannel
                                                                  : KernelChannelPolicy::MatchingChannels,
                         .stride = KernelStridePolicy::RowStrided,
                         .bufferAlignmentBytes = sampleTypeSizeBytes(sample),
                         .alias = KernelAliasPolicy::Disjoint,
                         .validity = ValidityRule::AllValid,
                         .border = KernelBorderMode::NotApplicable,
                         .coordinates = KernelCoordinateConvention::None,
                         .scratch = KernelScratchPolicy::None,
                         .persistentPlan = false,
                         .accuracy = pointwiseProgramAccuracy(program)};
}

KernelContract pointwiseProgramKernelContract(const PointwiseProgramKernelShape& shape) noexcept
{
    // A kernel a backend runs uses none of the operation's threads, and the vocabulary has no word for work a
    // device schedules; `Serial` is its nearest statement, and no plan is matched against it.
    return KernelContract{.id = kPointwiseProgramKernelId,
                          .capabilities =
                              KernelCapabilities{.inputSample = shape.sampleType,
                                                 .outputSample = shape.sampleType,
                                                 .computeSample = shape.computeSample,
                                                 .inputDomain = shape.inputDomain,
                                                 .outputDomain = shape.outputDomain,
                                                 .inputArrangement = StorageArrangement::Planar,
                                                 .outputArrangement = StorageArrangement::Planar,
                                                 .channels = KernelChannelPolicy::MatchingChannels,
                                                 .strides = KernelStridePolicy::RowStrided,
                                                 .minimumAlignmentBytes = sampleTypeSizeBytes(shape.sampleType),
                                                 .alias = KernelAliasPolicy::Disjoint,
                                                 .validity = shape.validity,
                                                 .borders = KernelBorderCapability::None,
                                                 .coordinates = KernelCoordinateConvention::None,
                                                 .scratch = KernelScratchPolicy::None,
                                                 .parallelism = KernelParallelPolicy::Serial,
                                                 .cancellation = KernelCancellationPolicy::None,
                                                 .accuracy = shape.accuracy,
                                                 .tails = true,
                                                 .persistentPlan = false},
                          .referenceScenarioId = kPointwiseProgramReferenceScenario};
}

const PointwiseProgram* pointwiseProgramOf(std::span<const std::byte> arguments) noexcept
{
    if (arguments.size() != sizeof(PointwiseProgramKernelArguments))
    {
        return nullptr;
    }
    PointwiseProgramKernelArguments carried;
    std::memcpy(&carried, arguments.data(), sizeof(carried));
    return carried.program;
}

Status checkPointwiseProgramKernelRequest(const DeviceKernelRequest& request)
{
    const KernelPlanKey& plan = request.plan;
    if (!(plan.id == kPointwiseProgramKernelId))
    {
        return requestFailure(ErrorCode::InvalidArgument, "plan",
                              "the plan names an operation other than a pointwise program");
    }
    const PointwiseProgram* const carried = pointwiseProgramOf(request.arguments);
    if (carried == nullptr)
    {
        return requestFailure(ErrorCode::InvalidArgument, "arguments", "the arguments carry no pointwise program");
    }

    const PointwiseProgram& program = *carried;
    const KernelPlanKey own = pointwiseProgramKernelPlan(program);
    const bool channelsFit = program.channelCount() == 1u || plan.channels != KernelChannelPolicy::SingleChannel;
    if (plan.inputSample != own.inputSample || plan.outputSample != own.outputSample ||
        plan.computeSample != own.computeSample || plan.inputDomain != own.inputDomain ||
        plan.outputDomain != own.outputDomain || plan.inputArrangement != own.inputArrangement ||
        plan.outputArrangement != own.outputArrangement || !channelsFit)
    {
        return requestFailure(ErrorCode::IncompatibleArguments, "plan",
                              "the plan states a representation, width, domain, arrangement or channel shape the "
                              "program it carries does not have");
    }
    if (plan.validity == ValidityRule::ExplicitMask)
    {
        return requestFailure(ErrorCode::InvalidArgument, "validity",
                              "a pointwise program kernel binds no packed validity mask");
    }
    if (!accuracyFits(own.accuracy, plan.accuracy))
    {
        return requestFailure(ErrorCode::UnsupportedBackend, "accuracy",
                              "the program raises to a power, which no implementation promises closer than the "
                              "transcendental tier, and the plan asks for more");
    }

    const std::span<const ImageLayout> layouts = request.bindingLayouts;
    if (layouts.size() != std::size_t{program.sourceCount()} + 1u)
    {
        return requestFailure(ErrorCode::InvalidArgument, "bindingLayouts",
                              "a pointwise program kernel binds one layout per source and one for the destination");
    }
    for (const ImageLayout& layout : layouts)
    {
        if (!describesProgramImage(layout, program) || layout.extent().width() != layouts.front().extent().width() ||
            layout.extent().height() != layouts.front().extent().height() ||
            (plan.stride == KernelStridePolicy::ContiguousOnly && !layout.isContiguous()))
        {
            return requestFailure(ErrorCode::IncompatibleArguments, "bindingLayouts",
                                  "a binding layout is not a planar image of the program's representation and "
                                  "channels, of the others' extent, with the row stride the plan allows");
        }
    }
    return Status::success();
}

} // namespace an
