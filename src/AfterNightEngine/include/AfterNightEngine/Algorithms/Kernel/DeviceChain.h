// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Two or more device operations planned together, so that what one produces and
/// the next consumes never goes back to the host.
///
/// `Device` makes every transfer explicit, which is what makes this possible and also what makes it
/// necessary: a caller that submits each operation on its own has to name the resources itself, and the
/// obvious way to do that is to bring each result home and send it out again. A chain of two operations
/// written that way costs four transfers where two are needed, and the cost is invisible - nothing is
/// wrong, the numbers are right, and the device spends its time on a copy nobody asked for.
///
/// So the decision is taken once, by a planner that sees the whole chain rather than one operation:
///
/// - a **buffer** is named by what the host wants of it. An input the host supplies, an output the host
///   needs back, or an intermediate that is neither - produced by one step, consumed by a later one, and
///   never reachable from the host at all;
/// - a **step** is one prepared kernel and the buffers it binds, in the kernel's binding order;
/// - the **plan** says where each buffer lives, which ones are uploaded, which are downloaded, which are
///   handed from one step to the next without a transfer, and what that saves against the chain that
///   round-tripped every one of them; and
/// - the **run** submits one piece of work for the uploads, one per step and one for the downloads, each
///   depending on the one before, and waits for none of them.
///
/// Residency is a decision, not a constant. On a device with memory of its own, a buffer a kernel binds
/// is device memory and an input or an output is staged through pinned host memory. On a device that
/// shares its memory with the host, the same chain allocates shared memory, the host maps it directly,
/// and the plan says there is nothing to transfer - which is the answer, not a special case.
///
/// What a chain does not do is hide a failure. Submission stops at the first refusal and says which
/// piece of work it was; work that was already accepted keeps running under the stop token it was
/// submitted with, and `awaitDeviceChain` waits for all of it and reports the first piece that did not
/// succeed. A stopped execution therefore ends a chain without leaving work nobody waits for, and
/// without an output the host could mistake for a finished one.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, because every member below only borrows one for the call.
class ExecutionContext;

/// What the host wants of one buffer of a chain, which is what decides whether it is transferred.
enum class DeviceChainBufferRole : std::uint8_t
{
    /// The host supplies its bytes. The chain uploads them before the first step that reads it, and the
    /// host reaches it through `DeviceChain::hostBuffer`.
    Input,

    /// The host neither supplies nor reads it: some step writes it, a later step reads it, and it stays
    /// where a kernel can bind it. `DeviceChain::hostBuffer` gives the host no door to one.
    Intermediate,

    /// The host needs its bytes. Some step must write it, the chain downloads it after the last step
    /// that does, and the host reaches it through `DeviceChain::hostBuffer`.
    ///
    /// Deliberately the last enumerator; `kDeviceChainBufferRoleCount` is derived from it.
    Output
};

/// How many roles `DeviceChainBufferRole` holds.
inline constexpr std::size_t kDeviceChainBufferRoleCount = static_cast<std::size_t>(DeviceChainBufferRole::Output) + 1u;

/// The role's own name, for a diagnostic or a report.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param role the role to name. A value outside the enumeration is named
///        `"UnknownDeviceChainBufferRole"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* deviceChainBufferRoleName(DeviceChainBufferRole role) noexcept;

/// One buffer a chain's steps bind.
///
/// The chain allocates it, so a caller states what it holds rather than where it lives; where it lives
/// is the plan's answer. There is no default for `layout`, because no layout means nothing.
struct DeviceChainBuffer
{
    /// What the host wants of it.
    DeviceChainBufferRole role = DeviceChainBufferRole::Intermediate;

    /// How the samples sit in its bytes. Its total is the resource's size, and a step whose kernel
    /// states a layout for the binding must bind a buffer that states the same one.
    ImageLayout layout;

    /// The boundary its first byte sits on, in bytes. Must be a power of two, at least the alignment
    /// every kernel binding it requires, and no more than the device accepts.
    std::uint64_t alignment = 64u;
};

/// One binding of one step, naming a buffer of the chain rather than a resource.
struct DeviceChainBinding
{
    /// The buffer's position in `DeviceChainRequest::buffers`.
    std::uint32_t buffer = 0u;

    /// What the step's kernel does with it. Must equal the kernel's requirement for this binding.
    DeviceAccess access = DeviceAccess::Read;
};

/// One operation of a chain.
struct DeviceChainStep
{
    /// The kernel to run, prepared on the chain's device. Must not be null and must hold a kernel.
    const PreparedDeviceKernel* kernel = nullptr;

    /// The buffers it binds, in the kernel's binding order. Borrowed for the `DeviceChain::create`
    /// call, which copies what it keeps.
    std::span<const DeviceChainBinding> bindings{};
};

/// Everything a chain is asked for.
///
/// A plain aggregate of borrowed spans, valid for the `DeviceChain::create` call only.
struct DeviceChainRequest
{
    /// The buffers the steps bind. At least one, and every one of them bound by at least one step.
    std::span<const DeviceChainBuffer> buffers{};

    /// The steps, in the order they run. At least one.
    std::span<const DeviceChainStep> steps{};
};

/// What the planner decided for one buffer.
struct DeviceChainBufferPlan
{
    /// What the host wanted of it, copied from the request.
    DeviceChainBufferRole role = DeviceChainBufferRole::Intermediate;

    /// Where the chain keeps it: device memory when the device has its own, shared memory otherwise.
    /// Either way a kernel can bind it.
    ResourceResidency residency = ResourceResidency::Device;

    /// Whether the chain uploads the host's bytes into it before the first step that reads it.
    bool uploaded = false;

    /// Whether the chain downloads its bytes for the host after the last step that writes it.
    bool downloaded = false;

    /// Whether it is written by one step and read by a later one, and therefore handed across without
    /// reaching the host.
    bool resident = false;

    /// The first step that binds it.
    std::uint32_t firstStep = 0u;

    /// The last step that binds it. Equal to `firstStep` when only one does.
    std::uint32_t lastStep = 0u;

    /// How many bytes it holds, which is `DeviceChainBuffer::layout`'s total.
    std::uint64_t bytes = 0u;
};

/// What a chain will transfer, what it will keep where it is, and what that is worth.
///
/// Every figure is a consequence of the plan and is known before any work is submitted, which is the
/// point: a planner that can only measure a transfer after making it cannot decide not to make it.
struct DeviceChainAccounting
{
    /// How many pieces of work the chain submits: one for the uploads when it has any, one per step,
    /// and one for the downloads when it has any.
    std::uint32_t submissions = 0u;

    /// How many transfers carry the host's bytes into memory a kernel binds.
    std::uint32_t uploads = 0u;

    /// How many bytes they carry.
    std::uint64_t uploadBytes = 0u;

    /// How many transfers carry a result back to the host.
    std::uint32_t downloads = 0u;

    /// How many bytes they carry.
    std::uint64_t downloadBytes = 0u;

    /// How many buffers are handed from one step to a later one without reaching the host.
    std::uint32_t residentHandovers = 0u;

    /// How many bytes those buffers hold between them.
    std::uint64_t residentBytes = 0u;

    /// How many transfers a chain that returned every handed-over buffer to the host between the steps
    /// would have added to this plan: a download and an upload each, and none at all on a device whose
    /// memory the host already shares.
    std::uint32_t avoidedTransfers = 0u;

    /// How many bytes those transfers would have carried.
    std::uint64_t avoidedBytes = 0u;

    /// How many bytes of memory a kernel can bind the chain holds for its whole life. The scratch a
    /// kernel declares is not part of it: the device allocates and returns that around each run.
    std::uint64_t deviceBytes = 0u;

    /// How many bytes of host-addressable staging the chain holds for its whole life. Zero on a device
    /// whose memory the host shares, because there is nothing to stage.
    std::uint64_t hostBytes = 0u;
};

/// The planner's whole answer for one chain.
struct DeviceChainPlan
{
    /// One entry per requested buffer, in the request's order.
    std::vector<DeviceChainBufferPlan> buffers{};

    /// What the chain transfers and what it keeps.
    DeviceChainAccounting accounting{};
};

/// One piece of work of a chain has no index, said of a report that names none.
inline constexpr std::uint32_t kDeviceChainNoWork = 0xFFFFFFFFu;

/// The work one run of a chain submitted.
///
/// The events are in submission order, which is the order `DeviceChainAccounting::submissions` counts:
/// the uploads, then one per step, then the downloads. There are fewer than that when submission
/// stopped early, and `submission` says why.
struct DeviceChainRun
{
    /// One event per accepted piece of work, in submission order.
    std::vector<DeviceEvent> events{};

    /// Success when every planned piece of work was accepted, or the refusal that stopped submission
    /// after at least one had been.
    Status submission{};
};

/// What a chain's work did, once all of it has finished.
struct DeviceChainReport
{
    /// Success, or the outcome of the first piece of work that did not succeed, or the failure that
    /// stopped the wait.
    Status outcome{};

    /// Which piece of work `outcome` came from, as a position in `DeviceChainRun::events`;
    /// `kDeviceChainNoWork` when every piece succeeded and submission was not refused. A wait the
    /// waiter's own execution stopped names the piece that had not finished, and a refused submission
    /// is one past the last event.
    std::uint32_t failedWork = kDeviceChainNoWork;

    /// How many pieces of work finished successfully.
    std::uint32_t succeededWork = 0u;

    /// How many of the run's declared transfers the backend recorded as performed.
    std::uint32_t transfersPerformed = 0u;

    /// How many of its kernels the backend recorded as run.
    std::uint32_t kernelsRun = 0u;

    /// What the backend recorded spending in those transfers, in nanoseconds on its own clock.
    std::uint64_t transferNanoseconds = 0u;

    /// What it recorded spending in those kernels, on the same clock.
    std::uint64_t kernelNanoseconds = 0u;
};

/// Waits for every piece of a run's work and reports what they did.
///
/// Waits for every one of them, in submission order, so that nothing of the chain is still running when
/// it returns, and reports the first that did not succeed - everything after that one depends on it and
/// reports it as its cause. A wait `execution` stops returns at that point and leaves the work alone,
/// exactly as `DeviceEvent::wait(const ExecutionContext&)` does.
///
/// Thread-safe. Allocation-free apart from the failure it may have to copy.
///
/// @param run what `DeviceChain::submit` returned. A run with no event reports its own `submission`.
/// @param execution the operation that is waiting, whose stop token and deadline bound the wait. Must
///        hold something.
/// @return the report. `execution` is consulted only for a piece of work that has not finished, so
///         one that holds nothing makes `outcome` `ErrorCode::NotInitialized`, naming the first such
///         piece, and goes unnoticed when every piece has already finished.
[[nodiscard]] AFTERNIGHTENGINE_API DeviceChainReport awaitDeviceChain(const DeviceChainRun& run,
                                                                      const ExecutionContext& execution) noexcept;

/// The engine's record of one chain, defined inside the engine.
class DeviceChainState;

/// Two or more device operations, planned and run as one.
///
/// Built by `create`, which validates the whole chain and allocates every buffer it needs before any
/// work exists - so a chain that cannot run is refused with nothing allocated and nothing started.
/// Move-only, because the buffers have one owner.
///
/// The members are safe to call from several threads at once, but two runs of one chain are not safe
/// to overlap: they write the same buffers. Running one chain again after its previous run has
/// finished is ordinary, and uploads the host's inputs again.
class DeviceChain
{
public:
    /// A chain that holds nothing. `valid()` is false.
    DeviceChain() noexcept = default;

    /// Releases the chain's buffers, under `DeviceResource`'s rule: memory accepted work still uses is
    /// returned when that work finishes.
    ~DeviceChain() = default;

    /// Takes over another chain, leaving it holding nothing.
    ///
    /// @param other the chain to take over. Left as a default-constructed one.
    DeviceChain(DeviceChain&& other) noexcept = default;

    /// Releases this chain and takes over another.
    ///
    /// @param other the chain to take over. Left as a default-constructed one.
    /// @return this chain.
    DeviceChain& operator=(DeviceChain&& other) noexcept = default;

    /// Copying is deleted: a copy would be a second owner of the buffers.
    DeviceChain(const DeviceChain&) = delete;

    /// Copying is deleted. See the copy constructor.
    DeviceChain& operator=(const DeviceChain&) = delete;

    /// Plans a chain and allocates its buffers.
    ///
    /// Every rule is checked before anything is allocated: the steps and their kernels belong to this
    /// device, each step's bindings match its kernel's requirements in count, access, size, alignment
    /// and layout and name no buffer twice, every buffer some step reads has been produced - written by
    /// an earlier step, or supplied by the host - every output is written by some step, and no buffer
    /// is left unbound.
    ///
    /// Thread-safe. Allocates the chain's record and its buffers.
    ///
    /// @param device the device the chain runs on, which must have prepared every step's kernel. Only
    ///        read; the chain holds its buffers, which keep the device alive.
    /// @param request the buffers and steps. Borrowed for the call.
    /// @return the chain; or a refusal: `ErrorCode::NotInitialized` with `DeviceFailure::DeviceShutDown`;
    ///         `ErrorCode::InvalidArgument` with `DeviceFailure::EmptySubmission` for a chain with no
    ///         step or no buffer, `DeviceFailure::InvalidKernel` for a step that names no kernel, or
    ///         `DeviceFailure::BindingMismatch` for bindings that disagree with a kernel;
    ///         `ErrorCode::OutOfRange` with `DeviceFailure::BindingMismatch` for a binding that names no
    ///         buffer of the chain; `ErrorCode::Aliasing` with `DeviceFailure::BindingAliased` for a step
    ///         that binds one buffer twice; `ErrorCode::IncompatibleArguments` with
    ///         `DeviceFailure::ForeignDevice` for a kernel of another device; `ErrorCode::InvalidState`
    ///         with `DeviceFailure::UnproducedBuffer` or `DeviceFailure::UnusedBuffer`;
    ///         `ErrorCode::Unsupported` with `DeviceFailure::ResidencyNotOffered` when the device stages
    ///         neither host nor pinned memory for an input or an output; or whatever
    ///         `Device::createResource` refused a buffer with.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DeviceChain> create(const Device& device,
                                                                         const DeviceChainRequest& request);

    /// Whether this chain holds a plan and its buffers.
    ///
    /// @return false for a default-constructed chain and one that was moved from.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// The identity of the device this chain was planned for.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the device's identity, or `0` for a chain that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t deviceId() const noexcept;

    /// What the planner decided.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the plan, valid while this chain holds it; null for a chain that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceChainPlan* plan() const noexcept;

    /// The resource a step's kernel binds for one buffer.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param index the buffer's position in the request.
    /// @return the resource, valid while this chain holds it; a handle that holds nothing for a chain
    ///         that holds nothing and for a position the chain has no buffer at.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceResource& buffer(std::uint32_t index) const noexcept;

    /// The resource the host writes an input into, or reads an output from.
    ///
    /// The same resource as `buffer` on a device whose memory the host shares, and the staging the
    /// chain uploads from or downloads into otherwise. Mapping it for writing an input or reading an
    /// output is refused while the chain's unfinished work reads or writes it, so a caller writes its
    /// inputs before submitting and reads its outputs after waiting.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param index the buffer's position in the request.
    /// @return the resource, valid while this chain holds it; a handle that holds nothing for an
    ///         intermediate, for a chain that holds nothing, and for a position the chain has no buffer
    ///         at.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceResource& hostBuffer(std::uint32_t index) const noexcept;

    /// Submits the chain's work and returns without waiting for any of it.
    ///
    /// One piece of work carries the uploads, one carries each step, and one carries the downloads.
    /// Each depends on the one before it, so the device's own rule that work declares what it waits for
    /// is satisfied through the chain rather than by the caller. A refusal stops submission there: the
    /// work already accepted keeps running under `execution`'s stop token, and the returned run holds
    /// its events and the refusal.
    ///
    /// Thread-safe with respect to other chains, and not with respect to another run of this one.
    /// Allocates the run's events.
    ///
    /// @param device the chain's own device. Only read.
    /// @param execution the operation the work belongs to. Only its policy is kept, never the reference.
    /// @return the run; or a refusal with nothing submitted: `ErrorCode::NotInitialized` for a chain,
    ///         device or execution that holds nothing, `ErrorCode::IncompatibleArguments` with
    ///         `DeviceFailure::ForeignDevice` for another device, `ErrorCode::Cancelled` or
    ///         `ErrorCode::DeadlineExceeded` for an execution already stopped, or whatever
    ///         `Device::submit` refused the first piece of work with.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DeviceChainRun> submit(const Device& device,
                                                                     const ExecutionContext& execution) const;

private:
    /// Takes the record the factory has already built.
    ///
    /// @param state the chain's record. Never null on this path.
    explicit DeviceChain(std::shared_ptr<DeviceChainState> state) noexcept : m_state(std::move(state)) {}

    /// The chain's record, or null.
    std::shared_ptr<DeviceChainState> m_state;
};

} // namespace an
