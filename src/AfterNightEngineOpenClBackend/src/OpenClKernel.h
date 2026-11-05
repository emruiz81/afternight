// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - what every kernel this backend prepares does on the worker.
//
// The seam hands a backend back only the kernels it prepared, so the worker knows each one is an
// `OpenClKernel` and asks it to run over the bindings a piece of work resolved. A kernel sets its own
// arguments and enqueues its own range; the worker only orders the steps around it.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an::opencl
{

/// A kernel this backend prepared.
class OpenClKernel : public DeviceKernel
{
public:
    /// Virtual, so a kernel held by base pointer is destroyed as what it is.
    ~OpenClKernel() override = default;

    /// Runs the kernel over its bound buffers and reports how long the device took.
    ///
    /// Called on the backend's worker thread only. That is what makes setting arguments on one kernel object
    /// safe while the kernel is otherwise immutable: one worker runs one piece of work at a time.
    ///
    /// @param bindings the bound buffers, in the kernel's binding order.
    /// @param scratch the device-owned temporary allocation the prepared kernel declared, or no
    ///        allocation when it declared zero bytes.
    /// @return the nanoseconds the device ran the kernel, or the failure it ended with.
    [[nodiscard]] virtual Result<std::uint64_t> run(std::span<const DeviceBindingStep> bindings,
                                                    std::optional<DeviceAllocation> scratch) const = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    OpenClKernel() = default;

    /// Protected so a kernel is never copied through a base pointer and sliced.
    OpenClKernel(const OpenClKernel&) = default;

    /// Protected for the same reason as the copy constructor.
    OpenClKernel& operator=(const OpenClKernel&) = default;
};

} // namespace an::opencl
