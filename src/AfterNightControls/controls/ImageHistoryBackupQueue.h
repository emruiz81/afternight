// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The history's backup writer: the bounded queue of step snapshots copied at registration and written, one at a time,
/// as versioned image snapshots to a directory the history owns.
///
/// A step is a copy of an image taken under a lease when the step is registered, so nothing that happens to the shown
/// image afterwards reaches it. The copy waits in memory until the writer has written it, and that memory is the
/// session's to bound: every copy is admitted against the session's pending-snapshot allowance before it is made, and
/// holds its admission until the writer lets it go. A snapshot is the engine's image snapshot - samples, every metadata
/// object, the validity mask and the identity the history gave it - published whole or not at all, and read back only
/// when its identity is the one the history wrote it for.
///
/// Nothing here waits on the thread that registers a step: `tryReserve` answers at once, and `reserve`, which waits for
/// room, is for a worker.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace an::ui::history
{

/// Where one history snapshot stands. It is pending from the moment it is queued until the writer has written it, has
/// failed to, or it was discarded before its turn came.
enum class SnapshotState
{
    Pending,
    Written,
    Failed,
    Discarded
};

/// Writes one snapshot of the image under `image` to `path`, for `identity`. Called on the backup writer's thread.
///
/// @param session the session the image belongs to.
/// @param image the copy, under a read lease.
/// @param path the file, as UTF-8.
/// @param identity the identity the snapshot is written for.
/// @param stop stops the write; a stopped write leaves no file.
/// @return success once the snapshot is whole at `path`, or why nothing was published there.
using SnapshotWriter =
    std::function<an::Status(const an::consumer::EngineSession& session, const an::consumer::AssetReadLease& image,
                             const std::string& path, const an::ArtifactIdentity& identity, std::stop_token stop)>;

/// The writer the product uses: the engine's image snapshot, published through a pending sibling and renamed into
/// place, under one admitted operation of the session.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status
writeHistorySnapshot(const an::consumer::EngineSession& session, const an::consumer::AssetReadLease& image,
                     const std::string& path, const an::ArtifactIdentity& identity, std::stop_token stop);

/// Reads a snapshot the history wrote back as a new asset of the session, with a new identity.
///
/// @param session the session the asset is made in.
/// @param path the file, as UTF-8.
/// @param expected the identity the history wrote it for; a file that states another is refused.
/// @param stop stops the admission and the read.
/// @return the asset; `ErrorCode::InvalidData` for a file that is not a snapshot or not the one expected - a developer
///         dump of an earlier session included; or what the engine refuses.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
readHistorySnapshot(const an::consumer::EngineSession& session, const std::string& path,
                    const an::ArtifactIdentity& expected, std::stop_token stop);

/// A copy of the image a read lease holds, and its validity mask, as a new asset of the session.
///
/// @param session the session the copy is made in.
/// @param image the image, under its lease.
/// @return the copy, or what the engine refuses when it allocates it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
copyForHistory(const an::consumer::EngineSession& session, const an::consumer::AssetReadLease& image);

/// A copy of the image a write lease holds, and its validity mask, as a new asset of the session: what an edit
/// replaces, read through the lease the edit holds.
///
/// @param session the session the copy is made in.
/// @param image the image, under its writer's lease.
/// @return the copy, or what the engine refuses when it allocates it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
copyForHistory(const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image);

/// The bytes a copy of the image a read lease holds takes: its samples and its validity mask.
///
/// @param image the image, under its lease.
/// @return the bytes; at least one, so an admission is never of nothing.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t historyBytesOf(const an::consumer::AssetReadLease& image);

/// The bytes a copy of the image a write lease holds takes: its samples and its validity mask.
///
/// @param image the image, under its writer's lease.
/// @return the bytes; at least one, so an admission is never of nothing.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t historyBytesOf(const an::consumer::AssetWriteLease& image);

/// One history step's image on its way to a file, and then in it.
class AFTERNIGHTCONTROLS_API Snapshot
{
public:
    /// A snapshot written to `path` for `identity`; `label` names it in a failure.
    Snapshot(std::string path, std::string label, an::ArtifactIdentity identity, std::uint64_t bytes);

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    /// The file the snapshot is written to.
    [[nodiscard]] const std::string& path() const;

    /// What the snapshot is of, for a person reading a failure.
    [[nodiscard]] const std::string& label() const;

    /// The identity it is written for, which a read requires the file to state.
    [[nodiscard]] const an::ArtifactIdentity& identity() const;

    /// The bytes of the copy it was taken from.
    [[nodiscard]] std::uint64_t bytes() const;

    /// Where it stands now.
    [[nodiscard]] SnapshotState state() const;

    /// Why it failed, or nothing.
    [[nodiscard]] std::string failure() const;

    /// Waits until the snapshot is no longer pending, and returns where it ended.
    SnapshotState waitUntilSettled() const;

    /// Waits until the snapshot is no longer pending or `stop` is requested.
    ///
    /// @return where it ended, or `SnapshotState::Pending` when stopped first.
    SnapshotState waitUntilSettled(std::stop_token stop) const;

    /// Settles a snapshot that never reached a queue: one whose copy could not be made or admitted.
    ///
    /// @param failure why.
    void fail(std::string failure);

private:
    friend class BackupQueue;

    void settle(SnapshotState state, std::string failure);

    const std::string m_Path;
    const std::string m_Label;
    const an::ArtifactIdentity m_Identity;
    const std::uint64_t m_Bytes;

    mutable std::mutex m_Mutex;
    mutable std::condition_variable_any m_Settled;
    SnapshotState m_State = SnapshotState::Pending;
    std::string m_Failure;
};

using SnapshotPtr = std::shared_ptr<Snapshot>;

/// One history's backup writer and the queue in front of it. Thread-safe.
///
/// Every copy the queue holds was admitted against the session's pending-snapshot allowance, which every history of
/// the session shares, and holds its reservation until the writer has finished with it. The queue never waits on the
/// thread that submits to it.
class AFTERNIGHTCONTROLS_API BackupQueue
{
public:
    /// Called on the writer's thread when a snapshot settles, written or not.
    using SettledListener = std::function<void(const Snapshot& snapshot)>;

    /// A queue writing through `writer` for the histories of `session`, which must outlive it.
    explicit BackupQueue(const an::consumer::EngineSession& session, SnapshotWriter writer = writeHistorySnapshot,
                         SettledListener onSettled = {});

    /// Closes the queue as `close` does.
    ~BackupQueue();

    BackupQueue(const BackupQueue&) = delete;
    BackupQueue& operator=(const BackupQueue&) = delete;

    /// The session whose allowance the queue admits against.
    [[nodiscard]] const an::consumer::EngineSession& session() const;

    /// Admits a copy's bytes now, or says why not. Never waits.
    ///
    /// @param bytes the copy's size.
    /// @param kind `ReservationKind::Required` for a state already shown, which must be kept: it is admitted beyond
    ///        the allowance and counted as an overrun.
    /// @return the reservation, or the allowance's refusal; `ErrorCode::InvalidState` once the queue is closed.
    [[nodiscard]] an::Result<an::consumer::MemoryReservation>
    tryReserve(std::uint64_t bytes, an::consumer::ReservationKind kind = an::consumer::ReservationKind::Ordinary);

    /// Admits a copy's bytes, waiting while the allowance is held by copies still waiting for a writer. For a worker:
    /// it blocks. The wait is decided again whenever a snapshot of this queue settles, and at least every twenty
    /// milliseconds for room another history's writer gives back.
    ///
    /// @param bytes the copy's size.
    /// @param stop stops the wait.
    /// @return the reservation; `ErrorCode::Cancelled` when stopped; `ErrorCode::InvalidState` once the queue is
    ///         closed; or the allowance's refusal when no pending copy holds it, so waiting would never admit it.
    [[nodiscard]] an::Result<an::consumer::MemoryReservation> reserve(std::uint64_t bytes, std::stop_token stop);

    /// Queues a copy for writing. Never waits.
    ///
    /// @param copy the copy; nothing else may hold it.
    /// @param room its admission, held until the writer has finished with it.
    /// @param path the file it is written to.
    /// @param label what it is of, for a failure.
    /// @param identity what it is written for.
    /// @return the snapshot, pending; already failed when the queue is closed or `copy` holds nothing.
    [[nodiscard]] SnapshotPtr submit(an::consumer::ImageAsset&& copy, an::consumer::MemoryReservation&& room,
                                     std::string path, std::string label, an::ArtifactIdentity identity);

    /// Takes `snapshot` off the queue if its write has not started, which settles it as discarded; a write in progress
    /// finishes.
    void discard(const SnapshotPtr& snapshot);

    /// Discards everything waiting, stops the write in progress - which leaves no file - and waits for the writer to
    /// end. Nothing is admitted or queued afterwards. Closing twice closes once.
    void close();

    /// The snapshots queued and not yet written, the one being written included.
    [[nodiscard]] std::size_t pendingSnapshots() const;

    /// The most snapshots that were ever pending at once.
    [[nodiscard]] std::size_t peakPendingSnapshots() const;

    /// How many `reserve` calls had to wait before they were admitted or gave up.
    [[nodiscard]] std::size_t admissionWaits() const;

private:
    struct Entry
    {
        SnapshotPtr snapshot;
        std::shared_ptr<an::consumer::ImageAsset> copy;
        an::consumer::MemoryReservation room;
    };

    void runWriter(std::stop_token stop);

    const an::consumer::EngineSession& m_Session;
    const SnapshotWriter m_Writer;
    const SettledListener m_OnSettled;

    mutable std::mutex m_Mutex;
    std::condition_variable m_WorkAvailable;
    std::condition_variable_any m_RoomAvailable;
    std::deque<Entry> m_Queue;
    std::size_t m_PendingSnapshots = 0;
    std::size_t m_PeakPendingSnapshots = 0;
    std::size_t m_AdmissionWaits = 0;
    // Moves whenever a copy gives its room back, so a waiting `reserve` asks the allowance again.
    std::uint64_t m_RoomGeneration = 0;
    bool m_Closed = false;

    std::jthread m_WriterThread;
};

} // namespace an::ui::history
