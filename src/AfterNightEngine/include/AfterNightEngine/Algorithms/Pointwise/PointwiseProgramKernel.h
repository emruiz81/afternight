// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A pointwise program as a kernel that a backend other than the host can run.
///
/// A fused chain is the pointwise family's unit of work, so it is also what a device is handed, rather
/// than one kernel per step. The kernel vocabulary identifies an operation and plans it in plain values;
/// this header adds the one operation whose arithmetic travels with it - `KernelOperation::PointwiseProgram`
/// - and states the four things a caller and a backend have to agree on for a program to cross the device
/// seam:
///
/// - **the plan, read off the program.** Representation, arithmetic width, numeric domains and channel
///   shape are the program's. So is the accuracy: a program with a power step carries the vector
///   transcendental tier that every implementation, the compiled host path included, is already permitted
///   at that step, and any other program is exact. A plan says nothing about which steps a program holds,
///   so a backend that covers only some of them refuses the rest when the kernel is prepared rather than
///   when it is selected, with `ErrorCode::UnsupportedBackend` - the code a caller that may run on the host
///   branches on;
/// - **the arguments.** The program travels by address inside `PointwiseProgramKernelArguments`, a
///   trivially copyable structure borrowed for the preparation, exactly as another operation's coefficients
///   travel. A backend that needs the chain afterwards copies or lowers it before the preparation returns,
///   so a variable-length chain needs no byte encoding of its own and the seam's argument rule is unchanged;
/// - **the bindings.** Every source in the program's order, then the destination: planar layouts of the
///   program's representation with one plane per channel, all of one extent. Their strides may differ; and
/// - **the contract** a backend advertises, built for one representation, domain pair, validity mechanism
///   and accuracy, so that two backends advertising the same shape advertise the same values.
///
/// Nothing here runs a program. `PointwiseExecution.h` is the host's executor, and a device kernel is
/// whatever a backend prepares from these values, held to producing what `PointwiseEvaluator.h` produces
/// within the plan's accuracy.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <span>

namespace an
{

/// A validated chain of element-wise steps. Defined in `Algorithms/Pointwise/PointwiseProgram.h`.
///
/// Named here rather than included, because every declaration below only borrows one.
class PointwiseProgram;

/// What a caller asks a device to prepare. Defined in `Algorithms/Kernel/DeviceBackend.h`.
///
/// Named here rather than included, because the check below only borrows one.
struct DeviceKernelRequest;

/// The operation and contract revision of a pointwise program kernel.
inline constexpr KernelId kPointwiseProgramKernelId{.operation = KernelOperation::PointwiseProgram, .version = 1u};

/// The stable name of the scalar oracle a pointwise program kernel is measured against, for
/// `KernelContract::referenceScenarioId`.
inline constexpr const char* kPointwiseProgramReferenceScenario = "kernel.pointwise.program.v1";

/// The arguments of a pointwise program kernel, for `DeviceKernelRequest::arguments`.
///
/// Trivially copyable, so `deviceKernelArguments` turns it into a request's arguments like any other
/// operation's. The program is borrowed for the call that prepares the kernel and no longer: a backend that
/// needs the chain after that call copies or lowers it before returning.
struct PointwiseProgramKernelArguments
{
    /// The validated program. Must not be null, and must outlive the preparation.
    const PointwiseProgram* program = nullptr;
};

/// The accuracy any implementation of a program is held to against the scalar evaluator.
///
/// A program with a power step carries `mathFunctionAccuracy(MathFunction::Pow)`: that is the one step no
/// instruction computes, and the compiled host path already evaluates it within that tier for a
/// non-negative base; `PointwiseOpKind::Gamma` states where the two differ outside it. Every other step
/// is arithmetic whose rounding the evaluator states, so a program without a power step is exact.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param program the validated program.
/// @return a `KernelAccuracyMetric::Ulp` contract at the accepted transcendental tier, or an exact one.
[[nodiscard]] AFTERNIGHTENGINE_API KernelAccuracyContract
pointwiseProgramAccuracy(const PointwiseProgram& program) noexcept;

/// The plan a program is placed with: what `Device::selectKernel` is asked, and what a request carries.
///
/// Every numeric choice is the program's - its primary representation for the input and the output, its
/// declared width for the arithmetic, its two domains, and `pointwiseProgramAccuracy`. The buffer choices
/// are the widest the operation defines: planar images, one channel or matching channels, row strides, the
/// representation's own alignment, disjoint buffers and all-valid input. A caller narrows a copy to describe
/// a particular invocation, as it would narrow `kernelPlanFor`.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param program the validated program.
/// @return the plan.
[[nodiscard]] AFTERNIGHTENGINE_API KernelPlanKey pointwiseProgramKernelPlan(const PointwiseProgram& program) noexcept;

/// What one backend's pointwise program kernel covers, for `pointwiseProgramKernelContract`.
struct PointwiseProgramKernelShape
{
    /// The resident representation of every binding.
    SampleType sampleType = SampleType::Float32;

    /// The width the kernel computes in: `SampleType::Float32` or `SampleType::Float64`.
    SampleType computeSample = SampleType::Float32;

    /// What the values the kernel reads mean.
    NumericDomain inputDomain = NumericDomain::NormalizedLinear;

    /// What the values the kernel writes mean.
    NumericDomain outputDomain = NumericDomain::NormalizedLinear;

    /// Which validity descriptions the kernel honours.
    KernelValidityCapability validity = KernelValidityCapability::AllValidOnly;

    /// How far the kernel may be from the scalar evaluator.
    KernelAccuracyContract accuracy{};
};

/// The contract a backend advertises for one shape of pointwise program kernel.
///
/// The capability vocabulary states one representation, one domain pair, one validity mechanism and one
/// accuracy per contract. A backend whose kernel computes the same arithmetic over several domains therefore
/// advertises one contract per domain pair, and one per accuracy it meets. Everything else is fixed by the
/// operation: planar arrangement, one channel or matching channels, row strides, the representation's
/// alignment, disjoint buffers, no border, coordinate, scratch or persistent plan, and a stop observed
/// between a piece of work's steps rather than inside the kernel.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param shape what the kernel covers.
/// @return the contract. `kernelPlanFor` of it is a plan it supports.
[[nodiscard]] AFTERNIGHTENGINE_API KernelContract
pointwiseProgramKernelContract(const PointwiseProgramKernelShape& shape) noexcept;

/// The program a request's arguments carry.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param arguments the bytes of one `PointwiseProgramKernelArguments`.
/// @return the program, or null when the bytes are not one argument structure or it names no program.
[[nodiscard]] AFTERNIGHTENGINE_API const PointwiseProgram*
pointwiseProgramOf(std::span<const std::byte> arguments) noexcept;

/// Whether a kernel request describes the program it carries, checked the way every backend has to.
///
/// A device checks a request against its backend's contracts before the backend is asked. This checks what
/// only the program can answer, in a fixed order, and the first reason wins:
///
/// 1. the plan names `kPointwiseProgramKernelId`, and the arguments carry a program;
/// 2. the plan agrees with the program's own plan in representation, arithmetic width, both domains and
///    arrangement, states more than one channel when the program has more than one, and does not ask for a
///    packed validity mask, which no binding carries;
/// 3. the program's accuracy fits the plan's: an exact program fits every plan, and a program with a power
///    step fits only a plan of the same metric whose limit is at least its tier; and
/// 4. there is one binding layout per source and one for the destination, each with one plane per channel
///    of the program's representation and all of one extent, and contiguous when the plan allows no row
///    stride.
///
/// Thread-safe and reentrant. Allocates only a failure it describes.
///
/// @param request the plan, the binding layouts and the arguments. Borrowed for the call.
/// @return success; `ErrorCode::InvalidArgument` for a plan naming another operation, arguments that carry
///         no program, a packed-mask plan or the wrong number of layouts; `ErrorCode::IncompatibleArguments`
///         for a plan or a layout that describes a different program; or `ErrorCode::UnsupportedBackend` for
///         a plan whose accuracy the program cannot meet. Each failure names what was wrong as
///         `ContextKey::Parameter`.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkPointwiseProgramKernelRequest(const DeviceKernelRequest& request);

} // namespace an
