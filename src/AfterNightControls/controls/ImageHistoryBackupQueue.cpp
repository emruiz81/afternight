// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The history's backup writer: the copies, the session's admission of their bytes, the writer's thread, and the
/// engine's image snapshot behind it.

#include "stdafx.hpp"
#include "ImageHistoryBackupQueue.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ImageSnapshot.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <system_error>
#include <utility>
#include <variant>

namespace an::ui::history
{

namespace
{

/// How long `reserve` waits before it asks the allowance again when nothing of its own queue has settled: room another
/// history's writer gives back wakes nothing here.
constexpr std::chrono::milliseconds kRoomPollInterval{20};

[[nodiscard]] an::Status refusal(const an::ErrorCode code, std::string message, const char* operation)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message), .operation = operation});
}

/// A copy of an image and its mask as a new asset of the session.
[[nodiscard]] an::Result<an::consumer::ImageAsset> copyImage(const an::consumer::EngineSession& session,
                                                             const an::consumer::AnyImage& image,
                                                             const an::ValidityMask* validity)
{
    an::Result<an::consumer::AnyImage> copied = std::visit(
        [](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            auto clone = typed.clone();
            if (!clone.hasValue())
            {
                return clone.status();
            }
            return an::consumer::AnyImage{std::move(*clone)};
        },
        image);
    if (!copied.hasValue())
    {
        return copied.status();
    }
    std::optional<an::ValidityMask> mask;
    if (validity != nullptr)
    {
        an::Result<an::ValidityMask> clone = validity->clone();
        if (!clone.hasValue())
        {
            return clone.status();
        }
        mask.emplace(std::move(*clone));
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*copied), std::move(mask));
}

/// The bytes an image and its mask hold.
[[nodiscard]] std::uint64_t bytesOf(const an::consumer::AnyImage& image, const an::ValidityMask* validity)
{
    const std::uint64_t samples = std::visit([](const auto& typed) { return typed.storage().capacityBytes(); }, image);
    return samples + (validity != nullptr ? validity->capacityBytes() : 0u);
}

} // namespace

an::Status writeHistorySnapshot(const an::consumer::EngineSession& session, const an::consumer::AssetReadLease& image,
                                const std::string& path, const an::ArtifactIdentity& identity, std::stop_token stop,
                                const StepDurability durability)
{
    if (!image.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "there is no image to keep", "writeHistorySnapshot");
    }
    // A snapshot is written with one thread: it is a file, and the edit it keeps has the session's others.
    an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 1u, .stopToken = std::move(stop)});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    return image.visit([&](const auto& typed) -> an::Status {
        an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        // Published whole or not at all; whether its bytes are on the device first is the caller's to say.
        const an::Result<an::ImageSnapshotWriteReport> written = an::writeImageSnapshot(
            scope->engine(), scope->execution(),
            an::ImageSnapshotWriteRequest{.destinationPath = path,
                                          .identity = &identity,
                                          .durability = durability == StepDurability::Flushed
                                                            ? an::OutputDurability::FlushBeforePublish
                                                            : an::OutputDurability::CloseOnly},
            *frame, typed.metadata(), image.validity());
        return written.hasValue() ? an::Status::success() : written.status();
    });
}

an::Result<an::consumer::ImageAsset> readHistorySnapshot(const an::consumer::EngineSession& session,
                                                         const std::string& path, const an::ArtifactIdentity& expected,
                                                         std::stop_token stop)
{
    an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 1u, .stopToken = std::move(stop)});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::ImageSnapshotContents> contents = an::readImageSnapshot(scope->engine(), scope->execution(), path);
    scope->end();
    if (!contents.hasValue())
    {
        return contents.status();
    }
    // Only the snapshot this history wrote at this path is its step: anything else there is another file.
    if (!(contents->identity == expected))
    {
        return refusal(an::ErrorCode::InvalidData, "the file at the step's path is not the snapshot the history wrote",
                       "readHistorySnapshot");
    }
    const std::optional<an::RawFrameDescriptor> described = contents->frame.descriptor();
    const std::optional<an::SampleType> sampleType =
        described.has_value() ? described->residentSampleType() : std::nullopt;
    if (!sampleType.has_value())
    {
        return refusal(an::ErrorCode::InvalidData, "the snapshot's samples are not of a resident type",
                       "readHistorySnapshot");
    }
    an::Result<an::consumer::AnyImage> image =
        refusal(an::ErrorCode::Internal, "the snapshot's samples were not adopted", "readHistorySnapshot");
    (void)an::dispatchSampleType(*sampleType, [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> adopted =
            an::Image<SampleT>::adopt(std::move(contents->frame), std::move(contents->metadata));
        image = adopted.hasValue() ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage{std::move(*adopted)})
                                   : an::Result<an::consumer::AnyImage>(adopted.status());
    });
    if (!image.hasValue())
    {
        return image.status();
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*image), std::move(contents->validity));
}

an::Result<an::consumer::ImageAsset> copyForHistory(const an::consumer::EngineSession& session,
                                                    const an::consumer::AssetReadLease& image)
{
    if (!image.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "there is no image to copy", "copyForHistory");
    }
    return copyImage(session, image.image(), image.validity());
}

an::Result<an::consumer::ImageAsset> copyForHistory(const an::consumer::EngineSession& session,
                                                    const an::consumer::AssetWriteLease& image)
{
    if (!image.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "there is no image to copy", "copyForHistory");
    }
    return copyImage(session, image.image(), image.validity());
}

std::uint64_t historyBytesOf(const an::consumer::AssetReadLease& image)
{
    return image.valid() ? (std::max)(bytesOf(image.image(), image.validity()), std::uint64_t{1}) : 1u;
}

std::uint64_t historyBytesOf(const an::consumer::AssetWriteLease& image)
{
    return image.valid() ? (std::max)(bytesOf(image.image(), image.validity()), std::uint64_t{1}) : 1u;
}

Snapshot::Snapshot(std::string path, std::string label, an::ArtifactIdentity identity, const std::uint64_t bytes)
    : m_Label(std::move(label)), m_Identity(std::move(identity)), m_Bytes(bytes), m_Path(std::move(path))
{
}

std::shared_ptr<Snapshot> Snapshot::written(std::string path, std::string label, an::ArtifactIdentity identity,
                                            const std::uint64_t fileBytes)
{
    auto snapshot = std::make_shared<Snapshot>(std::move(path), std::move(label), std::move(identity), 0u);
    snapshot->m_FileBytes = fileBytes;
    snapshot->m_State = SnapshotState::Written;
    return snapshot;
}

std::string Snapshot::path() const
{
    std::lock_guard lock(m_Mutex);
    return m_Path;
}

void Snapshot::moveTo(std::string path)
{
    std::lock_guard lock(m_Mutex);
    m_Path = std::move(path);
}

std::uint64_t Snapshot::fileBytes() const
{
    std::lock_guard lock(m_Mutex);
    return m_FileBytes;
}

void Snapshot::drop() noexcept
{
    m_Dropped.store(true, std::memory_order_release);
}

bool Snapshot::dropped() const noexcept
{
    return m_Dropped.load(std::memory_order_acquire);
}

const std::string& Snapshot::label() const
{
    return m_Label;
}

const an::ArtifactIdentity& Snapshot::identity() const
{
    return m_Identity;
}

std::uint64_t Snapshot::bytes() const
{
    return m_Bytes;
}

SnapshotState Snapshot::state() const
{
    std::lock_guard lock(m_Mutex);
    return m_State;
}

std::string Snapshot::failure() const
{
    std::lock_guard lock(m_Mutex);
    return m_Failure;
}

SnapshotState Snapshot::waitUntilSettled() const
{
    std::unique_lock lock(m_Mutex);
    m_Settled.wait(lock, [this] { return m_State != SnapshotState::Pending; });
    return m_State;
}

SnapshotState Snapshot::waitUntilSettled(std::stop_token stop) const
{
    std::unique_lock lock(m_Mutex);
    m_Settled.wait(lock, stop, [this] { return m_State != SnapshotState::Pending; });
    return m_State;
}

void Snapshot::fail(std::string failure)
{
    settle(SnapshotState::Failed, std::move(failure));
}

void Snapshot::settle(const SnapshotState state, std::string failure, std::string path, const std::uint64_t fileBytes)
{
    {
        std::lock_guard lock(m_Mutex);
        if (m_State != SnapshotState::Pending)
        {
            return;
        }
        m_State = state;
        m_Failure = std::move(failure);
        if (!path.empty())
        {
            m_Path = std::move(path);
        }
        m_FileBytes = fileBytes;
    }
    m_Settled.notify_all();
}

BackupQueue::BackupQueue(const an::consumer::EngineSession& session, StepCodecPtr codec, SettledListener onSettled)
    : m_Session(session), m_OnSettled(std::move(onSettled)), m_Codec(codec ? std::move(codec) : historyStepCodec())
{
    m_WriterThread = std::jthread([this](std::stop_token stop) { runWriter(std::move(stop)); });
}

BackupQueue::~BackupQueue()
{
    close();
}

const an::consumer::EngineSession& BackupQueue::session() const
{
    return m_Session;
}

StepCodecPtr BackupQueue::codec() const
{
    std::lock_guard lock(m_Mutex);
    return m_Codec;
}

void BackupQueue::setCodec(StepCodecPtr codec)
{
    if (!codec)
    {
        return;
    }
    std::lock_guard lock(m_Mutex);
    m_Codec = std::move(codec);
}

an::Result<an::consumer::MemoryReservation> BackupQueue::tryReserve(const std::uint64_t bytes,
                                                                    const an::consumer::ReservationKind kind)
{
    {
        std::lock_guard lock(m_Mutex);
        if (m_Closed || m_Finishing)
        {
            return refusal(an::ErrorCode::InvalidState, "the history is closing", "BackupQueue::tryReserve");
        }
    }
    return m_Session.memory().reserve(an::consumer::MemoryCategory::PendingSnapshots,
                                      (std::max)(bytes, std::uint64_t{1}), kind);
}

an::Result<an::consumer::MemoryReservation> BackupQueue::reserve(const std::uint64_t bytes, std::stop_token stop)
{
    bool waited = false;
    while (true)
    {
        // Room given back after this is read wakes the wait below even when it comes before the wait begins.
        std::uint64_t seen = 0u;
        {
            std::lock_guard lock(m_Mutex);
            seen = m_RoomGeneration;
        }
        an::Result<an::consumer::MemoryReservation> room = tryReserve(bytes);
        if (room.hasValue())
        {
            return room;
        }
        const an::ErrorCode code = room.status().code();
        const bool full = code == an::ErrorCode::MemoryBudgetExceeded || code == an::ErrorCode::LimitExceeded;
        // Waiting admits a request only when a copy still waiting for a writer holds the allowance: with nothing
        // pending, the refusal is the request's own size.
        const bool held = m_Session.memory().counters(an::consumer::MemoryCategory::PendingSnapshots).entries > 0u;
        if (!full || !held)
        {
            return room;
        }
        std::unique_lock lock(m_Mutex);
        if (!waited)
        {
            waited = true;
            ++m_AdmissionWaits;
        }
        (void)m_RoomAvailable.wait_for(lock, stop, kRoomPollInterval,
                                       [this, seen] { return m_Closed || m_Finishing || m_RoomGeneration != seen; });
        if (stop.stop_requested())
        {
            return refusal(an::ErrorCode::Cancelled, "the wait for room in the history's backup queue was stopped",
                           "BackupQueue::reserve");
        }
    }
}

SnapshotPtr BackupQueue::submit(an::consumer::ImageAsset&& copy, an::consumer::MemoryReservation&& room,
                                std::string path, std::string label, an::ArtifactIdentity identity)
{
    auto snapshot = std::make_shared<Snapshot>(std::move(path), std::move(label), std::move(identity), room.bytes());
    if (!copy.valid())
    {
        // Nothing is queued, so the admission goes back now.
        room.release();
        snapshot->fail("there is no image to keep");
        return snapshot;
    }
    {
        std::lock_guard lock(m_Mutex);
        if (!m_Closed && !m_Finishing)
        {
            ++m_PendingSnapshots;
            m_PeakPendingSnapshots = (std::max)(m_PeakPendingSnapshots, m_PendingSnapshots);
            m_Queue.push_back(Entry{.snapshot = snapshot,
                                    .copy = std::make_shared<an::consumer::ImageAsset>(std::move(copy)),
                                    .room = std::move(room),
                                    .codec = m_Codec});
            m_WorkAvailable.notify_one();
            return snapshot;
        }
    }
    room.release();
    snapshot->fail("the history is closing");
    return snapshot;
}

void BackupQueue::discard(const SnapshotPtr& snapshot)
{
    std::optional<Entry> removed;
    {
        std::lock_guard lock(m_Mutex);
        const auto found = std::find_if(m_Queue.begin(), m_Queue.end(),
                                        [&snapshot](const Entry& entry) { return entry.snapshot == snapshot; });
        if (found == m_Queue.end())
        {
            return;
        }
        removed.emplace(std::move(*found));
        m_Queue.erase(found);
        --m_PendingSnapshots;
    }
    // The copy and its admission go back before anyone waiting for room is told.
    removed->copy.reset();
    removed->room.release();
    {
        std::lock_guard lock(m_Mutex);
        ++m_RoomGeneration;
    }
    m_RoomAvailable.notify_all();
    removed->snapshot->settle(SnapshotState::Discarded, {});
}

void BackupQueue::close()
{
    std::deque<Entry> discarded;
    {
        std::lock_guard lock(m_Mutex);
        if (m_Closed)
        {
            return;
        }
        m_Closed = true;
        discarded.swap(m_Queue);
        m_PendingSnapshots -= discarded.size();
    }
    m_WriterThread.request_stop();
    m_WorkAvailable.notify_all();
    m_RoomAvailable.notify_all();
    for (Entry& entry : discarded)
    {
        entry.copy.reset();
        entry.room.release();
        entry.snapshot->settle(SnapshotState::Discarded, {});
    }
    if (m_WriterThread.joinable())
    {
        m_WriterThread.join();
    }
}

void BackupQueue::finish()
{
    {
        std::lock_guard lock(m_Mutex);
        if (m_Closed)
        {
            return;
        }
        // The writer ends by itself once nothing waits; no stop is asked for, so the write in progress finishes.
        m_Finishing = true;
    }
    m_WorkAvailable.notify_all();
    m_RoomAvailable.notify_all();
    if (m_WriterThread.joinable())
    {
        m_WriterThread.join();
    }
    std::lock_guard lock(m_Mutex);
    m_Closed = true;
}

std::size_t BackupQueue::pendingSnapshots() const
{
    std::lock_guard lock(m_Mutex);
    return m_PendingSnapshots;
}

std::size_t BackupQueue::peakPendingSnapshots() const
{
    std::lock_guard lock(m_Mutex);
    return m_PeakPendingSnapshots;
}

std::size_t BackupQueue::admissionWaits() const
{
    std::lock_guard lock(m_Mutex);
    return m_AdmissionWaits;
}

void BackupQueue::runWriter(std::stop_token stop)
{
    while (true)
    {
        Entry entry;
        {
            std::unique_lock lock(m_Mutex);
            m_WorkAvailable.wait(lock, [this] { return m_Closed || m_Finishing || !m_Queue.empty(); });
            // Closed, the writer ends at once; finishing, once nothing waits.
            if (m_Closed || m_Queue.empty())
            {
                return;
            }
            entry = std::move(m_Queue.front());
            m_Queue.pop_front();
        }

        an::Result<std::string> written =
            refusal(an::ErrorCode::Internal, "the writer reported nothing", "BackupQueue");
        try
        {
            an::Result<an::consumer::AssetReadLease> lease = entry.copy->read(stop);
            written = lease.hasValue() ? entry.codec->write(m_Session, *lease, entry.snapshot->path(),
                                                            entry.snapshot->identity(), stop)
                                       : an::Result<std::string>(lease.status());
        }
        catch (const std::exception& error)
        {
            written = refusal(an::ErrorCode::Internal, error.what(), "BackupQueue");
        }
        catch (...)
        {
            written = refusal(an::ErrorCode::Internal, "the writer failed with an unknown error", "BackupQueue");
        }

        // The copy's memory and its admission go back before the room is announced.
        entry.copy.reset();
        entry.room.release();
        {
            std::lock_guard lock(m_Mutex);
            --m_PendingSnapshots;
            ++m_RoomGeneration;
        }
        m_RoomAvailable.notify_all();

        if (written.hasValue())
        {
            // The file's size is read here, on the writer's thread, so nothing else has to ask the file system.
            std::error_code ignored;
            const std::uintmax_t size = std::filesystem::file_size(
                std::filesystem::path(std::u8string(written->begin(), written->end())), ignored);
            entry.snapshot->settle(SnapshotState::Written, {}, *written,
                                   ignored ? std::uint64_t{0} : static_cast<std::uint64_t>(size));
        }
        else
        {
            std::string failure = written.status().message();
            entry.snapshot->settle(SnapshotState::Failed,
                                   failure.empty() ? std::string("the writer reported a failure without a reason")
                                                   : std::move(failure));
        }
        if (m_OnSettled)
        {
            m_OnSettled(*entry.snapshot);
        }
    }
}

} // namespace an::ui::history
