// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DeviceError.h>

#include "kernels/reference/DeviceState.h"

#include <mutex>
#include <utility>

// What a backend is handed: the work object's accessors, its recording calls and its completion, plus the
// two small value helpers the seam's header declares.
//
// A work object is a handle on the device's record of the work, so everything here reads or moves that
// record and the device decides what completion means. The one rule a work object enforces on its own is
// that it is never dropped silently: destroying one whose work was never completed completes it as
// abandoned.

namespace an
{

namespace
{

/// Completes a held piece of work as abandoned, if it was never completed, and empties the slot.
///
/// @param slot the work object's record, left empty.
void abandon(std::shared_ptr<DeviceWorkRecord>& slot) noexcept
{
    if (slot == nullptr)
    {
        return;
    }
    const std::shared_ptr<DeviceWorkRecord> record = std::move(slot);
    std::shared_ptr<DeviceState> device;
    {
        const std::lock_guard<std::mutex> held(record->mutex);
        if (record->stage == DeviceWorkStage::Finished)
        {
            return;
        }
        device = record->device;
    }
    if (device == nullptr)
    {
        return;
    }
    device->countBackendDefect();
    try
    {
        device->finish(record,
                       deviceFailure(ErrorCode::Internal, DeviceFailure::WorkAbandoned,
                                     StatusDetail{.message = "the backend released this work without completing it",
                                                  .operation = "DeviceWork"}));
    }
    catch (...)
    {
        device->finish(record, Status::failure(ErrorCode::Internal));
    }
}

} // namespace

const char* kernelDeterminismName(KernelDeterminism determinism) noexcept
{
    switch (determinism)
    {
    case KernelDeterminism::Reproducible:
        return "Reproducible";
    case KernelDeterminism::WithinAccuracy:
        return "WithinAccuracy";
    }
    return "UnknownKernelDeterminism";
}

bool offersResidency(const DeviceCapabilities& capabilities, ResourceResidency residency) noexcept
{
    switch (residency)
    {
    case ResourceResidency::Host:
        return capabilities.hostResidency;
    case ResourceResidency::Pinned:
        return capabilities.pinnedResidency;
    case ResourceResidency::Shared:
        return capabilities.sharedResidency;
    case ResourceResidency::Device:
        return capabilities.deviceResidency;
    }
    return false;
}

DeviceWork::DeviceWork(std::shared_ptr<DeviceWorkRecord> record) noexcept : m_record(std::move(record)) {}

DeviceWork::~DeviceWork()
{
    abandon(m_record);
}

DeviceWork::DeviceWork(DeviceWork&& other) noexcept : m_record(std::move(other.m_record)) {}

DeviceWork& DeviceWork::operator=(DeviceWork&& other) noexcept
{
    if (this != &other)
    {
        abandon(m_record);
        m_record = std::move(other.m_record);
    }
    return *this;
}

std::uint64_t DeviceWork::eventId() const noexcept
{
    return m_record != nullptr ? m_record->event->id() : 0u;
}

std::span<const DeviceTransferStep> DeviceWork::transfersBefore() const noexcept
{
    return m_record != nullptr ? std::span<const DeviceTransferStep>(m_record->transfersBefore)
                               : std::span<const DeviceTransferStep>{};
}

const DeviceKernel* DeviceWork::kernel() const noexcept
{
    return m_record != nullptr && m_record->kernel != nullptr ? &m_record->kernel->kernel() : nullptr;
}

std::span<const DeviceBindingStep> DeviceWork::bindings() const noexcept
{
    return m_record != nullptr ? std::span<const DeviceBindingStep>(m_record->bindings)
                               : std::span<const DeviceBindingStep>{};
}

std::optional<DeviceAllocation> DeviceWork::scratch() const noexcept
{
    return m_record != nullptr ? m_record->scratch : std::nullopt;
}

std::span<const DeviceTransferStep> DeviceWork::transfersAfter() const noexcept
{
    return m_record != nullptr ? std::span<const DeviceTransferStep>(m_record->transfersAfter)
                               : std::span<const DeviceTransferStep>{};
}

Status DeviceWork::checkCancellation() const noexcept
{
    return m_record != nullptr ? m_record->checkCancellation() : Status::failure(ErrorCode::NotInitialized);
}

void DeviceWork::markRunning() noexcept
{
    if (m_record != nullptr)
    {
        m_record->event->markRunning();
    }
}

void DeviceWork::recordTransfer(std::uint64_t nanoseconds) noexcept
{
    if (m_record != nullptr)
    {
        m_record->event->recordTransfer(nanoseconds, m_record->declaredTransfers());
    }
}

void DeviceWork::recordKernel(std::uint64_t nanoseconds) noexcept
{
    if (m_record != nullptr && m_record->kernel != nullptr)
    {
        m_record->event->recordKernel(nanoseconds);
    }
}

void DeviceWork::complete(Status outcome) noexcept
{
    if (m_record == nullptr)
    {
        return;
    }
    const std::shared_ptr<DeviceWorkRecord> record = m_record;
    bool alreadyFinished = false;
    std::shared_ptr<DeviceState> device;
    {
        const std::lock_guard<std::mutex> held(record->mutex);
        alreadyFinished = record->stage == DeviceWorkStage::Finished;
        device = alreadyFinished ? record->deviceObserver.lock() : record->device;
    }
    if (device == nullptr)
    {
        return;
    }
    if (alreadyFinished)
    {
        device->countBackendDefect();
        return;
    }
    device->complete(record, std::move(outcome));
}

} // namespace an
