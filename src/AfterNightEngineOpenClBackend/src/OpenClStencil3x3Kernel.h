// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the direct three-by-three stencil on the device: the border-,
// halo- and scratch-aware proof kernel.
//
// The operation `Algorithms/Kernel/StencilKernel.h` defines, over one Float32 plane whose rows may be
// padded, with a clamped or constant border. Each work item resolves its nine neighbours - through the
// border rule where they fall outside the image - into its own nine samples of device-owned scratch,
// reads them back, and accumulates them in Float64 in the scalar reference's row-major order with
// contraction switched off, so the result matches the reference bit for bit. The scratch is declared
// volatile so that round trip through device memory stays in the kernel; it is there to prove the
// scratch contract, not to make the stencil fast. Whether a request is one this kernel may take is
// decided by the engine's own `checkStencil3x3KernelRequest`.

#pragma once

#include "OpenClBackendCore.h"
#include "OpenClHeader.h"
#include "OpenClKernel.h"

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace an::opencl
{

/// The direct three-by-three stencil as this backend prepared it.
class Stencil3x3Kernel final : public OpenClKernel
{
public:
    /// Takes ownership of a created kernel object.
    ///
    /// @param core the backend the kernel's program belongs to, kept alive by the kernel.
    /// @param handle the kernel object, released when this is destroyed.
    /// @param plan the plan it was prepared for.
    /// @param input the layout it reads.
    /// @param output the layout it writes.
    /// @param parameters the coefficients and border behavior copied at preparation.
    Stencil3x3Kernel(std::shared_ptr<BackendCore> core, cl_kernel handle, const KernelPlanKey& plan,
                     const ImageLayout& input, const ImageLayout& output,
                     const Stencil3x3KernelParameters& parameters) noexcept;

    /// Releases the kernel object.
    ~Stencil3x3Kernel() override;

    Stencil3x3Kernel(const Stencil3x3Kernel&) = delete;
    Stencil3x3Kernel& operator=(const Stencil3x3Kernel&) = delete;

    /// The plan this kernel was prepared for.
    ///
    /// @return the plan the preparing request named.
    [[nodiscard]] KernelPlanKey plan() const noexcept override;

    /// The source plane, read, and the destination plane, written, each needing its layout's total
    /// bytes at Float32 alignment.
    ///
    /// @return the two requirements, in that order, valid for the kernel's life.
    [[nodiscard]] std::span<const DeviceBindingRequirement> bindings() const noexcept override;

    /// The halo every output's neighbourhood is resolved into: nine Float32 samples per output
    /// sample, which the device allocates for each run and accounts for.
    ///
    /// @return `width * height * 9 * sizeof(float)` for the prepared extent.
    [[nodiscard]] std::uint64_t scratchBytes() const noexcept override;

    /// The weights and the border travel as the kernel's arguments and the halo lives in per-run scratch, so
    /// the kernel keeps nothing on the device between runs.
    ///
    /// @return zero.
    [[nodiscard]] std::uint64_t residentBytes() const noexcept override;

    /// Runs the kernel over the source, destination and device-owned halo scratch.
    ///
    /// @param bindings the source and destination buffers, in binding order.
    /// @param scratch the allocation of `scratchBytes()` bytes the device created for this run.
    /// @return the nanoseconds the device ran the kernel; `ErrorCode::Internal` when the two bindings
    ///         and the scratch are not all buffers this device holds at the required size; or
    ///         `ErrorCode::ExternalFailure` when the driver refuses an argument or the launch.
    [[nodiscard]] Result<std::uint64_t> run(std::span<const DeviceBindingStep> bindings,
                                            std::optional<DeviceAllocation> scratch) const override;

private:
    std::shared_ptr<BackendCore> m_core;
    cl_kernel m_handle;
    KernelPlanKey m_plan;
    std::array<DeviceBindingRequirement, 2u> m_bindings;
    Stencil3x3KernelParameters m_parameters;
    std::uint64_t m_inputFirstRow;
    std::uint64_t m_inputRowStride;
    std::uint64_t m_outputFirstRow;
    std::uint64_t m_outputRowStride;
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint64_t m_scratchBytes;
};

/// Prepares the direct stencil for one checked request, building its one program on first use.
///
/// @param core the backend preparing it.
/// @param request the plan, two layouts and stencil parameters.
/// @return the kernel; the failure `checkStencil3x3KernelRequest` reports; or
///         `ErrorCode::ExternalFailure` when the driver refuses to build or create it.
[[nodiscard]] Result<std::shared_ptr<const DeviceKernel>>
prepareStencil3x3Kernel(const std::shared_ptr<BackendCore>& core, const DeviceKernelRequest& request);

} // namespace an::opencl
