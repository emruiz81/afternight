// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the plan, contract and request check that place a three-by-three stencil
// across the device seam.
//
// A device backend is handed its arguments as bytes, so the request check reads them back into a
// `Stencil3x3KernelParameters`, holds them and the plan to the one contract, and requires two
// equally shaped single-plane Float32 bindings. The backends that implement the stencil call it
// before they prepare the kernel, so none carries its own opinion of what a valid stencil is. The contract states
// device-allocated scratch because the kernel resolves each output's halo into nine Float32
// samples before it accumulates them, and the check refuses a picture whose scratch byte count
// would not be representable.

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cmath>
#include <cstring>

namespace an
{
namespace
{

constexpr const char* kCheckOperation = "an::checkStencil3x3KernelRequest";

[[nodiscard]] Status requestFailure(ErrorCode code, const char* parameter, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = kCheckOperation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool isFloatPlane(const ImageLayout& layout) noexcept
{
    return layout.elementSizeBytes() == sizeof(float) && layout.samplesPerPixel() == 1u && layout.planeCount() == 1u &&
           layout.extent().channelCount() == 1u;
}

} // namespace

KernelPlanKey stencil3x3KernelPlan(const Stencil3x3KernelParameters& parameters) noexcept
{
    return KernelPlanKey{.id = kStencil3x3KernelId,
                         .inputSample = SampleType::Float32,
                         .outputSample = SampleType::Float32,
                         .computeSample = SampleType::Float64,
                         .inputDomain = NumericDomain::NormalizedLinear,
                         .outputDomain = NumericDomain::NormalizedLinear,
                         .inputArrangement = StorageArrangement::Planar,
                         .outputArrangement = StorageArrangement::Planar,
                         .channels = KernelChannelPolicy::SingleChannel,
                         .stride = KernelStridePolicy::RowStrided,
                         .bufferAlignmentBytes = alignof(float),
                         .alias = KernelAliasPolicy::Disjoint,
                         .validity = ValidityRule::AllValid,
                         .border = parameters.border,
                         .haloRadius = kStencil3x3HaloRadius,
                         .coordinates = KernelCoordinateConvention::None,
                         .scratch = KernelScratchPolicy::DeviceAllocated,
                         .persistentPlan = false,
                         .accuracy = KernelAccuracyContract{}};
}

KernelContract stencil3x3KernelContract() noexcept
{
    return KernelContract{.id = kStencil3x3KernelId,
                          .capabilities = KernelCapabilities{.inputSample = SampleType::Float32,
                                                             .outputSample = SampleType::Float32,
                                                             .computeSample = SampleType::Float64,
                                                             .inputDomain = NumericDomain::NormalizedLinear,
                                                             .outputDomain = NumericDomain::NormalizedLinear,
                                                             .inputArrangement = StorageArrangement::Planar,
                                                             .outputArrangement = StorageArrangement::Planar,
                                                             .channels = KernelChannelPolicy::SingleChannel,
                                                             .strides = KernelStridePolicy::RowStrided,
                                                             .minimumAlignmentBytes = alignof(float),
                                                             .alias = KernelAliasPolicy::Disjoint,
                                                             .validity = KernelValidityCapability::AllValidOnly,
                                                             .borders = KernelBorderCapability::ClampAndConstant,
                                                             .haloRadius = kStencil3x3HaloRadius,
                                                             .coordinates = KernelCoordinateConvention::None,
                                                             .scratch = KernelScratchPolicy::DeviceAllocated,
                                                             .parallelism = KernelParallelPolicy::Serial,
                                                             .cancellation = KernelCancellationPolicy::None,
                                                             .accuracy = KernelAccuracyContract{},
                                                             .tails = true,
                                                             .persistentPlan = false},
                          .referenceScenarioId = kStencil3x3ReferenceScenario};
}

Status checkStencil3x3KernelRequest(const DeviceKernelRequest& request)
{
    if (!(request.plan.id == kStencil3x3KernelId))
    {
        return requestFailure(ErrorCode::InvalidArgument, "plan",
                              "the plan names an operation other than the direct three-by-three stencil");
    }
    if (request.arguments.size() != sizeof(Stencil3x3KernelParameters))
    {
        return requestFailure(ErrorCode::InvalidArgument, "arguments",
                              "the arguments are not one three-by-three stencil parameter object");
    }

    Stencil3x3KernelParameters parameters;
    std::memcpy(&parameters, request.arguments.data(), sizeof(parameters));
    if ((parameters.border != KernelBorderMode::Clamp && parameters.border != KernelBorderMode::Constant) ||
        !std::isfinite(parameters.constantBorder) || !std::isfinite(parameters.invalidNeighbor))
    {
        return requestFailure(ErrorCode::InvalidArgument, "arguments",
                              "the stencil border and substitute values must be known and finite");
    }
    for (const double coefficient : parameters.coefficients)
    {
        if (!std::isfinite(coefficient))
        {
            return requestFailure(ErrorCode::InvalidArgument, "arguments", "a stencil coefficient is not finite");
        }
    }
    if (!kernelContractSupports(stencil3x3KernelContract(), request.plan) || request.plan.border != parameters.border)
    {
        return requestFailure(ErrorCode::IncompatibleArguments, "plan",
                              "the plan does not describe the representation, width, border, validity or buffer "
                              "rules of the stencil arguments");
    }

    if (request.bindingLayouts.size() != 2u)
    {
        return requestFailure(ErrorCode::InvalidArgument, "bindingLayouts",
                              "a three-by-three stencil binds one source and one destination");
    }
    const ImageLayout& source = request.bindingLayouts[0];
    const ImageLayout& destination = request.bindingLayouts[1];
    if (!isFloatPlane(source) || !isFloatPlane(destination) ||
        source.extent().width() != destination.extent().width() ||
        source.extent().height() != destination.extent().height() ||
        (request.plan.stride == KernelStridePolicy::ContiguousOnly &&
         (!source.isContiguous() || !destination.isContiguous())))
    {
        return requestFailure(ErrorCode::IncompatibleArguments, "bindingLayouts",
                              "the bindings are not equally shaped single-plane Float32 images with the planned "
                              "row-stride policy");
    }
    const CheckedSize scratchBytes =
        CheckedSize{source.extent().pixelCount()} * CheckedSize{9u} * CheckedSize{sizeof(float)};
    if (!scratchBytes.isValid())
    {
        return requestFailure(ErrorCode::OutOfRange, "bindingLayouts",
                              "the device halo scratch byte count is not representable");
    }
    return Status::success();
}

} // namespace an
