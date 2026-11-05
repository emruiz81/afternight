// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the affine pointwise kernel, on the device. See OpenClAffineKernel.h.

#include "OpenClAffineKernel.h"

#include "OpenClRuntime.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace an::opencl
{

namespace
{

/// The kernel's source. Each step of the reference's arithmetic is its own statement, and contraction is
/// switched off as well, so the device can no more fuse the multiply into the add than the reference
/// does.
constexpr const char* kAffineSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#pragma OPENCL FP_CONTRACT OFF

kernel void pointwiseAffine(global const float* source, global float* destination,
                            const ulong sourceFirstRow, const ulong sourceRowStride,
                            const ulong destinationFirstRow, const ulong destinationRowStride,
                            const double scale, const double bias)
{
    const ulong x = get_global_id(0);
    const ulong y = get_global_id(1);
    const double widened = (double)source[sourceFirstRow + y * sourceRowStride + x];
    const double scaled = widened * scale;
    const double shifted = scaled + bias;
    destination[destinationFirstRow + y * destinationRowStride + x] = (float)shifted;
}
)CLC";

/// Where a single-plane Float32 layout's samples sit, or no value for a layout the kernel cannot address.
[[nodiscard]] std::optional<PlanePlacement> placementOf(const ImageLayout& layout) noexcept
{
    if (layout.elementSizeBytes() != sizeof(float) || layout.samplesPerPixel() != 1u || layout.planeCount() != 1u)
    {
        return std::nullopt;
    }
    const std::optional<std::uint64_t> firstRow = layout.rowByteOffset(0u, 0u);
    if (!firstRow.has_value() || *firstRow % sizeof(float) != 0u || layout.rowStrideBytes() % sizeof(float) != 0u)
    {
        return std::nullopt;
    }
    return PlanePlacement{.firstRow = *firstRow / sizeof(float), .rowStride = layout.rowStrideBytes() / sizeof(float)};
}

/// Builds the affine program once per backend.
[[nodiscard]] Status ensureAffineProgram(BackendCore& core)
{
    const std::lock_guard<std::mutex> held(core.programMutex);
    if (core.affineProgram != nullptr)
    {
        return Status::success();
    }
    cl_int status = CL_SUCCESS;
    const char* source = kAffineSource;
    const std::size_t length = std::strlen(kAffineSource);
    const cl_program program = clCreateProgramWithSource(core.context, 1u, &source, &length, &status);
    if (status != CL_SUCCESS || program == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateProgramWithSource", status);
    }
    status = clBuildProgram(program, 1u, &core.device, "-cl-std=CL1.2", nullptr, nullptr);
    if (status != CL_SUCCESS)
    {
        std::string log = programBuildLog(program, core.device);
        (void)clReleaseProgram(program);
        return backendFailure(ErrorCode::ExternalFailure, "the affine kernel's program did not build: " + log,
                              {StatusContextField::ofNumber(ContextKey::LibraryCode, status)});
    }
    core.affineProgram = program;
    return Status::success();
}

[[nodiscard]] Status setArgument(cl_kernel kernel, cl_uint index, std::size_t size, const void* value)
{
    const cl_int status = clSetKernelArg(kernel, index, size, value);
    return status == CL_SUCCESS ? Status::success() : callFailure(ErrorCode::ExternalFailure, "clSetKernelArg", status);
}

} // namespace

AffineKernel::AffineKernel(std::shared_ptr<BackendCore> core, cl_kernel handle, const KernelPlanKey& plan,
                           const ImageLayout& input, const ImageLayout& output,
                           const AffineKernelParameters& parameters) noexcept
    : m_core(std::move(core)), m_handle(handle), m_plan(plan),
      m_bindings{DeviceBindingRequirement{.access = DeviceAccess::Read,
                                          .minimumBytes = input.totalBytes(),
                                          .alignment = alignof(float),
                                          .layout = input},
                 DeviceBindingRequirement{.access = DeviceAccess::Write,
                                          .minimumBytes = output.totalBytes(),
                                          .alignment = alignof(float),
                                          .layout = output}},
      m_parameters(parameters), m_input(placementOf(input).value_or(PlanePlacement{})),
      m_output(placementOf(output).value_or(PlanePlacement{})), m_width(input.extent().width()),
      m_height(input.extent().height())
{
}

AffineKernel::~AffineKernel()
{
    if (m_handle != nullptr)
    {
        (void)clReleaseKernel(m_handle);
    }
}

KernelPlanKey AffineKernel::plan() const noexcept
{
    return m_plan;
}

std::span<const DeviceBindingRequirement> AffineKernel::bindings() const noexcept
{
    return m_bindings;
}

std::uint64_t AffineKernel::scratchBytes() const noexcept
{
    return 0u;
}

std::uint64_t AffineKernel::residentBytes() const noexcept
{
    return 0u;
}

Result<std::uint64_t> AffineKernel::run(std::span<const DeviceBindingStep> bindings,
                                        std::optional<DeviceAllocation> scratch) const
{
    (void)scratch;
    if (bindings.size() != m_bindings.size())
    {
        return backendFailure(ErrorCode::Internal, "the affine kernel was handed a binding count it did not declare");
    }
    const std::optional<Allocation> source = m_core->findAllocation(bindings[0].allocation);
    const std::optional<Allocation> destination = m_core->findAllocation(bindings[1].allocation);
    if (!source.has_value() || !destination.has_value() || source->buffer == nullptr || destination->buffer == nullptr)
    {
        return backendFailure(ErrorCode::Internal,
                              "the affine kernel was bound to memory this device holds no buffer for");
    }

    const cl_ulong sourceFirstRow = m_input.firstRow;
    const cl_ulong sourceRowStride = m_input.rowStride;
    const cl_ulong destinationFirstRow = m_output.firstRow;
    const cl_ulong destinationRowStride = m_output.rowStride;
    const cl_double scale = m_parameters.scale;
    const cl_double bias = m_parameters.bias;
    for (Status status :
         {setArgument(m_handle, 0u, sizeof(cl_mem), &source->buffer),
          setArgument(m_handle, 1u, sizeof(cl_mem), &destination->buffer),
          setArgument(m_handle, 2u, sizeof(cl_ulong), &sourceFirstRow),
          setArgument(m_handle, 3u, sizeof(cl_ulong), &sourceRowStride),
          setArgument(m_handle, 4u, sizeof(cl_ulong), &destinationFirstRow),
          setArgument(m_handle, 5u, sizeof(cl_ulong), &destinationRowStride),
          setArgument(m_handle, 6u, sizeof(cl_double), &scale), setArgument(m_handle, 7u, sizeof(cl_double), &bias)})
    {
        if (status.failed())
        {
            return status;
        }
    }

    const Clock::time_point started = Clock::now();
    const std::array<std::size_t, 2u> global{m_width, m_height};
    cl_event event = nullptr;
    const cl_int status =
        clEnqueueNDRangeKernel(m_core->workQueue, m_handle, 2u, nullptr, global.data(), nullptr, 0u, nullptr, &event);
    if (status != CL_SUCCESS)
    {
        return callFailure(ErrorCode::ExternalFailure, "clEnqueueNDRangeKernel", status);
    }
    return finishCommand(event, "clEnqueueNDRangeKernel", started);
}

Result<std::shared_ptr<const DeviceKernel>> prepareAffineKernel(const std::shared_ptr<BackendCore>& core,
                                                                const DeviceKernelRequest& request)
{
    if (!core->affineContract.has_value() || !kernelContractSupports(*core->affineContract, request.plan))
    {
        return backendFailure(ErrorCode::UnsupportedBackend,
                              "this OpenCL device runs the affine pointwise kernel only, over all-valid input, in "
                              "double precision");
    }
    if (request.bindingLayouts.size() != 2u)
    {
        return backendFailure(ErrorCode::InvalidArgument,
                              "the affine kernel binds one input plane and one output plane");
    }
    const ImageLayout& input = request.bindingLayouts[0];
    const ImageLayout& output = request.bindingLayouts[1];
    if (!placementOf(input).has_value() || !placementOf(output).has_value() ||
        input.extent().width() != output.extent().width() || input.extent().height() != output.extent().height())
    {
        return backendFailure(ErrorCode::InvalidArgument,
                              "the affine kernel maps one single-plane Float32 layout onto another of the same "
                              "extent, with rows that begin on a sample boundary");
    }
    if (request.arguments.size() != sizeof(AffineKernelParameters))
    {
        return backendFailure(ErrorCode::InvalidArgument, "the affine kernel's arguments are its two coefficients");
    }
    AffineKernelParameters parameters;
    std::memcpy(&parameters, request.arguments.data(), sizeof(parameters));
    if (!std::isfinite(parameters.scale) || !std::isfinite(parameters.bias))
    {
        return backendFailure(ErrorCode::InvalidArgument, "the affine coefficients must be finite");
    }

    const Status built = ensureAffineProgram(*core);
    if (built.failed())
    {
        return built;
    }
    cl_int status = CL_SUCCESS;
    const cl_kernel handle = clCreateKernel(core->affineProgram, "pointwiseAffine", &status);
    if (status != CL_SUCCESS || handle == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateKernel", status);
    }
    try
    {
        return std::shared_ptr<const DeviceKernel>(
            std::make_shared<const AffineKernel>(core, handle, request.plan, input, output, parameters));
    }
    catch (...)
    {
        (void)clReleaseKernel(handle);
        throw;
    }
}

} // namespace an::opencl
