// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - one direct three-by-three stencil on the GPU. See
// OpenClStencil3x3Kernel.h.

#include "OpenClStencil3x3Kernel.h"

#include "OpenClRuntime.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace an::opencl
{
namespace
{

/// One work item materializes its resolved three-by-three halo into its own nine-sample scratch
/// run, then accumulates those samples in the scalar oracle's row-major order. Volatile scratch
/// makes that device-memory round trip part of the proof rather than an optimization the compiler
/// may erase.
constexpr const char* kStencilSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#pragma OPENCL FP_CONTRACT OFF

kernel void stencil3x3(global const float* source, global float* destination,
                       volatile global float* haloScratch,
                       const ulong sourceFirstRow, const ulong sourceRowStride,
                       const ulong destinationFirstRow, const ulong destinationRowStride,
                       const uint width, const uint height, const uint constantBorder,
                       const float outside,
                       const double c0, const double c1, const double c2,
                       const double c3, const double c4, const double c5,
                       const double c6, const double c7, const double c8)
{
    const uint x = get_global_id(0);
    const uint y = get_global_id(1);
    const long maximumX = (long)width - 1l;
    const long maximumY = (long)height - 1l;
    const ulong scratchBase = ((ulong)y * (ulong)width + (ulong)x) * 9u;
    uint at = 0u;
    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        for (int offsetX = -1; offsetX <= 1; ++offsetX, ++at)
        {
            const long candidateX = (long)x + (long)offsetX;
            const long candidateY = (long)y + (long)offsetY;
            float neighbor = outside;
            if (!constantBorder ||
                (candidateX >= 0l && candidateY >= 0l && candidateX <= maximumX && candidateY <= maximumY))
            {
                const ulong sourceX = (ulong)clamp(candidateX, 0l, maximumX);
                const ulong sourceY = (ulong)clamp(candidateY, 0l, maximumY);
                neighbor = source[sourceFirstRow + sourceY * sourceRowStride + sourceX];
            }
            haloScratch[scratchBase + at] = neighbor;
        }
    }

    const double s0 = (double)haloScratch[scratchBase + 0u] * c0;
    const double s1 = s0 + (double)haloScratch[scratchBase + 1u] * c1;
    const double s2 = s1 + (double)haloScratch[scratchBase + 2u] * c2;
    const double s3 = s2 + (double)haloScratch[scratchBase + 3u] * c3;
    const double s4 = s3 + (double)haloScratch[scratchBase + 4u] * c4;
    const double s5 = s4 + (double)haloScratch[scratchBase + 5u] * c5;
    const double s6 = s5 + (double)haloScratch[scratchBase + 6u] * c6;
    const double s7 = s6 + (double)haloScratch[scratchBase + 7u] * c7;
    const double s8 = s7 + (double)haloScratch[scratchBase + 8u] * c8;
    destination[destinationFirstRow + (ulong)y * destinationRowStride + (ulong)x] = (float)s8;
}
)CLC";

[[nodiscard]] Status setArgument(cl_kernel kernel, cl_uint index, std::size_t size, const void* value)
{
    const cl_int status = clSetKernelArg(kernel, index, size, value);
    return status == CL_SUCCESS ? Status::success() : callFailure(ErrorCode::ExternalFailure, "clSetKernelArg", status);
}

[[nodiscard]] Status ensureStencilProgram(BackendCore& core)
{
    const std::lock_guard<std::mutex> held(core.programMutex);
    if (core.stencilProgram != nullptr)
    {
        return Status::success();
    }
    cl_int status = CL_SUCCESS;
    const char* source = kStencilSource;
    const std::size_t length = std::strlen(kStencilSource);
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
        return backendFailure(ErrorCode::ExternalFailure, "the stencil kernel's program did not build: " + log,
                              {StatusContextField::ofNumber(ContextKey::LibraryCode, status)});
    }
    core.stencilProgram = program;
    return Status::success();
}

[[nodiscard]] std::uint64_t firstRowOf(const ImageLayout& layout) noexcept
{
    return *layout.rowByteOffset(0u, 0u) / sizeof(float);
}

} // namespace

Stencil3x3Kernel::Stencil3x3Kernel(std::shared_ptr<BackendCore> core, cl_kernel handle, const KernelPlanKey& plan,
                                   const ImageLayout& input, const ImageLayout& output,
                                   const Stencil3x3KernelParameters& parameters) noexcept
    : m_core(std::move(core)), m_handle(handle), m_plan(plan),
      m_bindings{DeviceBindingRequirement{.access = DeviceAccess::Read,
                                          .minimumBytes = input.totalBytes(),
                                          .alignment = alignof(float),
                                          .layout = input},
                 DeviceBindingRequirement{.access = DeviceAccess::Write,
                                          .minimumBytes = output.totalBytes(),
                                          .alignment = alignof(float),
                                          .layout = output}},
      m_parameters(parameters), m_inputFirstRow(firstRowOf(input)),
      m_inputRowStride(input.rowStrideBytes() / sizeof(float)), m_outputFirstRow(firstRowOf(output)),
      m_outputRowStride(output.rowStrideBytes() / sizeof(float)), m_width(input.extent().width()),
      m_height(input.extent().height()), m_scratchBytes(std::uint64_t{m_width} * m_height * 9u * sizeof(float))
{
}

Stencil3x3Kernel::~Stencil3x3Kernel()
{
    if (m_handle != nullptr)
    {
        (void)clReleaseKernel(m_handle);
    }
}

KernelPlanKey Stencil3x3Kernel::plan() const noexcept
{
    return m_plan;
}

std::span<const DeviceBindingRequirement> Stencil3x3Kernel::bindings() const noexcept
{
    return m_bindings;
}

std::uint64_t Stencil3x3Kernel::scratchBytes() const noexcept
{
    return m_scratchBytes;
}

std::uint64_t Stencil3x3Kernel::residentBytes() const noexcept
{
    return 0u;
}

Result<std::uint64_t> Stencil3x3Kernel::run(std::span<const DeviceBindingStep> bindings,
                                            std::optional<DeviceAllocation> scratch) const
{
    if (bindings.size() != m_bindings.size() || !scratch.has_value())
    {
        return backendFailure(ErrorCode::Internal,
                              "the stencil kernel was not handed its two bindings and halo scratch");
    }
    const std::optional<Allocation> source = m_core->findAllocation(bindings[0].allocation);
    const std::optional<Allocation> destination = m_core->findAllocation(bindings[1].allocation);
    const std::optional<Allocation> temporary = m_core->findAllocation(*scratch);
    if (!source.has_value() || !destination.has_value() || !temporary.has_value() || source->buffer == nullptr ||
        destination->buffer == nullptr || temporary->buffer == nullptr || temporary->bytes < m_scratchBytes)
    {
        return backendFailure(ErrorCode::Internal,
                              "the stencil kernel was bound to memory this device does not hold at the required size");
    }

    const cl_ulong inputFirstRow = m_inputFirstRow;
    const cl_ulong inputRowStride = m_inputRowStride;
    const cl_ulong outputFirstRow = m_outputFirstRow;
    const cl_ulong outputRowStride = m_outputRowStride;
    const cl_uint width = m_width;
    const cl_uint height = m_height;
    const cl_uint constantBorder = m_parameters.border == KernelBorderMode::Constant ? 1u : 0u;
    const cl_float outside = m_parameters.constantBorder;
    std::array<cl_double, 9u> coefficients{};
    for (std::size_t index = 0u; index < coefficients.size(); ++index)
    {
        coefficients[index] = m_parameters.coefficients[index];
    }

    std::array<Status, 20u> statuses{setArgument(m_handle, 0u, sizeof(cl_mem), &source->buffer),
                                     setArgument(m_handle, 1u, sizeof(cl_mem), &destination->buffer),
                                     setArgument(m_handle, 2u, sizeof(cl_mem), &temporary->buffer),
                                     setArgument(m_handle, 3u, sizeof(cl_ulong), &inputFirstRow),
                                     setArgument(m_handle, 4u, sizeof(cl_ulong), &inputRowStride),
                                     setArgument(m_handle, 5u, sizeof(cl_ulong), &outputFirstRow),
                                     setArgument(m_handle, 6u, sizeof(cl_ulong), &outputRowStride),
                                     setArgument(m_handle, 7u, sizeof(cl_uint), &width),
                                     setArgument(m_handle, 8u, sizeof(cl_uint), &height),
                                     setArgument(m_handle, 9u, sizeof(cl_uint), &constantBorder),
                                     setArgument(m_handle, 10u, sizeof(cl_float), &outside)};
    for (std::size_t index = 0u; index < coefficients.size(); ++index)
    {
        statuses[index + 11u] =
            setArgument(m_handle, static_cast<cl_uint>(index + 11u), sizeof(cl_double), &coefficients[index]);
    }
    for (const Status& status : statuses)
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

Result<std::shared_ptr<const DeviceKernel>> prepareStencil3x3Kernel(const std::shared_ptr<BackendCore>& core,
                                                                    const DeviceKernelRequest& request)
{
    const Status checked = checkStencil3x3KernelRequest(request);
    if (checked.failed())
    {
        return checked;
    }
    Stencil3x3KernelParameters parameters;
    std::memcpy(&parameters, request.arguments.data(), sizeof(parameters));

    const Status built = ensureStencilProgram(*core);
    if (built.failed())
    {
        return built;
    }
    cl_int status = CL_SUCCESS;
    const cl_kernel handle = clCreateKernel(core->stencilProgram, "stencil3x3", &status);
    if (status != CL_SUCCESS || handle == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateKernel", status);
    }
    try
    {
        return std::shared_ptr<const DeviceKernel>(std::make_shared<const Stencil3x3Kernel>(
            core, handle, request.plan, request.bindingLayouts[0], request.bindingLayouts[1], parameters));
    }
    catch (...)
    {
        (void)clReleaseKernel(handle);
        throw;
    }
}

} // namespace an::opencl
