// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - a fused pointwise program, lowered into one OpenCL kernel.
//
// The engine's `PointwiseProgram` is a list of steps, and this is the one place it becomes device code. When
// a kernel is prepared the chain is written out as OpenCL C - one statement per rounding the scalar evaluator
// performs, in its order, at the program's Float32 width, with contraction switched off - and compiled by the
// driver, so preparing is the expensive step and a run afterwards only binds buffers. Coefficients and tables
// are written into the source as exact hexadecimal Float32 constants, the values the evaluator narrows them
// to, and so are each binding's row and plane strides, which is what makes a prepared kernel belong to its
// layouts. The one operation not written as itself is a sampled curve's division: the RTX 5080's OpenCL
// division is not the nearest quotient, so the quotient is chosen from a double-precision estimate by exact
// residuals, and the kernel needs the device's double precision for it.
//
// The device therefore reproduces bit for bit everything the evaluator does with basic arithmetic: scale,
// bias, clamp, mask, blend, and a sampled curve read nearest or linearly and clamped or extrapolated. The one
// step it cannot is the power, which calls the device's own `pow`; that step is held to the engine's
// transcendental tier, which is the accuracy the kernel plan of any program with a power step states. A
// not-a-number result is written as the one quiet not-a-number the host executor narrows every not-a-number
// to, so a result compares by its bits.
//
// Everything else is refused when the kernel is prepared, by name, with `ErrorCode::UnsupportedBackend`: a
// program that is not Float32 throughout, a lookup table, a channel matrix, the two-source arithmetic and
// comparison steps, a contracted scale and bias, a curve that rejects values outside its domain or whose
// domain has no positive finite width in Float32, and the preview-only lower-neighbour curve. None of them is
// beyond a device. A refusal names the step, so a caller that may run the program on the host does so.

#pragma once

#include "OpenClBackendCore.h"
#include "OpenClHeader.h"
#include "OpenClKernel.h"

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace an::opencl
{

/// Every contract the pointwise program kernel is advertised under.
///
/// The kernel's arithmetic does not depend on what the values mean, and the capability vocabulary states one
/// domain pair and one accuracy per contract: so it is advertised exactly over every pair of the four
/// domains, and at the power tier over the pairs whose input a power step can be built on.
///
/// @return the contracts, Float32 and all-valid input throughout.
[[nodiscard]] std::vector<KernelContract> pointwiseProgramContracts();

/// A pointwise program as this backend prepared it: its own compiled program and kernel object.
class PointwiseProgramKernel final : public OpenClKernel
{
public:
    /// Takes ownership of a built program and its kernel object.
    ///
    /// @param core the backend the program was built in, kept alive by the kernel.
    /// @param program the built program, released when this is destroyed.
    /// @param handle its kernel object, released when this is destroyed.
    /// @param plan the plan it was prepared for.
    /// @param bindings one requirement per source, then the destination's.
    /// @param width the images' width.
    /// @param height the images' height.
    /// @param channels the program's channel count.
    /// @param tableBytes the device memory the program's constant tables occupy: one Float32 per entry.
    PointwiseProgramKernel(std::shared_ptr<BackendCore> core, cl_program program, cl_kernel handle,
                           const KernelPlanKey& plan, std::vector<DeviceBindingRequirement> bindings,
                           std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                           std::uint64_t tableBytes) noexcept;

    /// Releases the kernel object and the program.
    ~PointwiseProgramKernel() override;

    PointwiseProgramKernel(const PointwiseProgramKernel&) = delete;
    PointwiseProgramKernel& operator=(const PointwiseProgramKernel&) = delete;

    /// The plan this kernel was prepared for.
    ///
    /// @return the plan the preparing request named.
    [[nodiscard]] KernelPlanKey plan() const noexcept override;

    /// One read requirement per source, then the destination's write requirement, each needing its
    /// layout's total bytes at Float32 alignment.
    ///
    /// @return the requirements, in that order, valid for the kernel's life.
    [[nodiscard]] std::span<const DeviceBindingRequirement> bindings() const noexcept override;

    /// Each work item evaluates every step on its own sample's values, so a run needs no scratch.
    ///
    /// @return zero.
    [[nodiscard]] std::uint64_t scratchBytes() const noexcept override;

    /// The program's tables, which the kernel's source declares as constant arrays and the device therefore
    /// keeps for the kernel's whole life.
    ///
    /// @return the table bytes it was prepared with.
    [[nodiscard]] std::uint64_t residentBytes() const noexcept override;

    /// Runs the kernel over one work item per sample of every channel and reports how long the device
    /// took.
    ///
    /// @param bindings the source buffers, then the destination, in binding order.
    /// @param scratch no allocation for this kernel.
    /// @return the nanoseconds the device ran the kernel; `ErrorCode::Internal` when the binding count
    ///         is not the declared one or a binding is not a buffer this device holds; or
    ///         `ErrorCode::ExternalFailure` when the driver refuses an argument or the launch.
    [[nodiscard]] Result<std::uint64_t> run(std::span<const DeviceBindingStep> bindings,
                                            std::optional<DeviceAllocation> scratch) const override;

private:
    std::shared_ptr<BackendCore> m_core;
    cl_program m_program;
    cl_kernel m_handle;
    KernelPlanKey m_plan;
    std::vector<DeviceBindingRequirement> m_bindings;
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_channels;
    std::uint64_t m_tableBytes;
};

/// Lowers the program a request carries into OpenCL C and builds it.
///
/// @param core the backend preparing it.
/// @param request the plan, the binding layouts and the program.
/// @return the kernel; the failure `checkPointwiseProgramKernelRequest` reports;
///         `ErrorCode::UnsupportedBackend` naming a step or a property this kernel does not lower;
///         `ErrorCode::InvalidArgument` for a layout that does not start at its buffer's first byte or
///         whose strides are not whole samples; or `ErrorCode::ExternalFailure` when the driver refuses
///         to build or create it.
[[nodiscard]] Result<std::shared_ptr<const DeviceKernel>>
preparePointwiseProgramKernel(const std::shared_ptr<BackendCore>& core, const DeviceKernelRequest& request);

} // namespace an::opencl
