// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the affine pointwise kernel, on the device.
//
// The operation `Algorithms/Kernel/PointwiseKernel.h` defines, `Float32(Float64(input) * scale + bias)`,
// over one Float32 plane whose rows may be padded, compiled from OpenCL C when a kernel is first prepared.
// Each sample is widened, multiplied and offset in separate statements with contraction switched off, so
// no rounding can move and the result is the scalar reference's bit for bit - which the suite checks
// rather than assumes. The kernel runs over all-valid input only. A fused chain of pointwise steps is
// `OpenClPointwiseProgramKernel.h`'s.

#pragma once

#include "OpenClBackendCore.h"
#include "OpenClHeader.h"
#include "OpenClKernel.h"

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace an::opencl
{

/// Where one plane's samples sit in its buffer, counted in Float32 elements.
struct PlanePlacement
{
    /// The first row's first sample.
    std::uint64_t firstRow = 0u;

    /// How many elements one row spans, padding included.
    std::uint64_t rowStride = 0u;
};

/// The affine kernel as this backend prepared it.
class AffineKernel final : public OpenClKernel
{
public:
    /// Takes ownership of a created kernel object.
    ///
    /// @param core the backend the kernel's program belongs to, kept alive by the kernel.
    /// @param handle the kernel object, released when this is destroyed.
    /// @param plan the plan it was prepared for.
    /// @param input the layout it reads.
    /// @param output the layout it writes.
    /// @param parameters its coefficients.
    AffineKernel(std::shared_ptr<BackendCore> core, cl_kernel handle, const KernelPlanKey& plan,
                 const ImageLayout& input, const ImageLayout& output,
                 const AffineKernelParameters& parameters) noexcept;

    /// Releases the kernel object.
    ~AffineKernel() override;

    AffineKernel(const AffineKernel&) = delete;
    AffineKernel& operator=(const AffineKernel&) = delete;

    /// The plan this kernel was prepared for.
    ///
    /// @return the plan the preparing request named.
    [[nodiscard]] KernelPlanKey plan() const noexcept override;

    /// The input plane, read, and the output plane, written, each needing its layout's total bytes at
    /// Float32 alignment.
    ///
    /// @return the two requirements, in that order, valid for the kernel's life.
    [[nodiscard]] std::span<const DeviceBindingRequirement> bindings() const noexcept override;

    /// Each work item reads one sample and writes one, so a run needs no scratch.
    ///
    /// @return zero.
    [[nodiscard]] std::uint64_t scratchBytes() const noexcept override;

    /// The coefficients travel as the kernel's arguments on every run, so the kernel keeps nothing on the device.
    ///
    /// @return zero.
    [[nodiscard]] std::uint64_t residentBytes() const noexcept override;

    /// Runs the kernel over two bound buffers and reports how long the device took.
    ///
    /// Called on the backend's worker thread only. That is what makes setting arguments on one kernel
    /// object safe while the kernel is otherwise immutable: one worker runs one piece of work at a time.
    ///
    /// @param bindings the input and output buffers, in binding order.
    /// @param scratch no allocation for this kernel.
    /// @return the nanoseconds the device ran the kernel, or the failure it ended with.
    [[nodiscard]] Result<std::uint64_t> run(std::span<const DeviceBindingStep> bindings,
                                            std::optional<DeviceAllocation> scratch) const override;

private:
    std::shared_ptr<BackendCore> m_core;
    cl_kernel m_handle;
    KernelPlanKey m_plan;
    std::array<DeviceBindingRequirement, 2u> m_bindings;
    AffineKernelParameters m_parameters;
    PlanePlacement m_input;
    PlanePlacement m_output;
    std::uint32_t m_width;
    std::uint32_t m_height;
};

/// Prepares the affine kernel for one request, building the program the first time.
///
/// @param core the backend preparing it.
/// @param request the plan, the two layouts and the coefficients.
/// @return the kernel; `ErrorCode::UnsupportedBackend` for a plan the kernel's contract does not cover;
///         `ErrorCode::InvalidArgument` for layouts or coefficients it cannot honour; or
///         `ErrorCode::ExternalFailure` when the driver refuses to build or create it.
[[nodiscard]] Result<std::shared_ptr<const DeviceKernel>> prepareAffineKernel(const std::shared_ptr<BackendCore>& core,
                                                                              const DeviceKernelRequest& request);

} // namespace an::opencl
