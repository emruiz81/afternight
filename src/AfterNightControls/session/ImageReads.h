// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The images a run reads beside the one it writes or presents, each read under one lease the run takes before it asks
/// to be admitted.
///
/// **A run takes every lease it needs before it asks for a thread, and in one order.** An edit holds its view's writer
/// and then asks to be admitted, so a run that was admitted and then waited for a lease on an image an edit was writing
/// would hold a thread the edit waits for while it waited for the edit: at a session width of one nothing else would
/// run. Leases taken first hold no thread. Taken in order of the images' identities - an edit's own writer at its
/// image's place in that order - two runs that each read the image the other writes cannot each hold what the other
/// waits for, at any width.
///
/// **One lease per image.** Writers are preferred: a run that asked for a second lease on an image it already reads
/// would wait behind a writer that asked for the image between the two, and the writer would wait for the first lease
/// for ever. A run names each image once, takes one lease on it, and reads it through shares of that lease, which never
/// wait.
///
/// **What a run names it reads through `readBeside`.** A body admitted under a request that states the run's reads -
/// `ExecutionRequest::reads` - is handed a share of the lease the run took; an image the run did not name, such as one
/// it made itself, is leased on its own; and the image the run writes is refused rather than leased under its own
/// writer, since the run reads it through that writer.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <vector>

namespace an::ui
{

class ExecutionScope;

/// The images one run reads beside the one it writes or presents: named on the GUI thread when the run is issued,
/// leased on its worker before it asks to be admitted, and let go when its computation ends. Move-only.
class ImageReads
{
public:
    /// Names nothing.
    AFTERNIGHTCONTROLS_API ImageReads() noexcept;

    /// Lets every lease go.
    AFTERNIGHTCONTROLS_API ~ImageReads();

    /// Takes over another run's images and leases.
    ///
    /// @param other the images and leases to take over.
    AFTERNIGHTCONTROLS_API ImageReads(ImageReads&& other) noexcept;

    /// Lets this run's leases go and takes over another's.
    ///
    /// @param other the images and leases to take over.
    /// @return this.
    AFTERNIGHTCONTROLS_API ImageReads& operator=(ImageReads&& other) noexcept;

    ImageReads(const ImageReads&) = delete;
    ImageReads& operator=(const ImageReads&) = delete;

    /// Names an image the run reads. An image named again, and a null one, change nothing. GUI thread, before the run
    /// is handed to its worker.
    ///
    /// @param image the image.
    AFTERNIGHTCONTROLS_API void name(std::shared_ptr<const an::consumer::ImageAsset> image);

    /// Holds a lease the run took already - its target's, taken without waiting - under its image's identity, so the
    /// image is read through shares of it and never leased again. A lease on an image already held or named replaces
    /// nothing; a lease on nothing changes nothing.
    ///
    /// @param lease the lease.
    AFTERNIGHTCONTROLS_API void hold(an::consumer::AssetReadLease lease);

    /// The identities of the images named, in the order their leases are taken.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<std::uint64_t> identities() const;

    /// Takes one lease on every image named, in order of identity, and the run's own writer at its image's place in
    /// that order. The run's own image is leased by no read: the run reads it through its writer. A failure lets every
    /// lease this call took go; a writer `takeWriter` took stays with whoever holds it.
    ///
    /// @param stop stops every wait.
    /// @param written the identity of the image the run writes, or zero when it writes none.
    /// @param takeWriter takes that image's writer, called once at its place in the order; may be empty.
    /// @return success; or why a lease or the writer was not taken.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Status take(std::stop_token stop, std::uint64_t written = 0u,
                                                         const std::function<an::Status()>& takeWriter = {});

    /// The lease held on an image, or null when the run did not name it or holds no lease on it.
    ///
    /// @param identity the image's identity.
    /// @return the lease, owned by this.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::consumer::AssetReadLease*
    find(std::uint64_t identity) const noexcept;

    /// The identity of the image the run writes, which it reads through its writer; zero when it writes none.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t written() const noexcept;

    /// Lets every lease go; the images stay named.
    AFTERNIGHTCONTROLS_API void release() noexcept;

private:
    struct Named;

    std::vector<Named> m_named;
    std::uint64_t m_written = 0u;
};

/// A lease to read an image beside the one a run writes or presents. Under a request that states the run's reads, an
/// image the run named is read through a share of the lease the run holds, which never waits, and the image the run
/// writes is refused; any other image is leased as `ImageAsset::read` leases it.
///
/// @param scope the admitted operation the image is read in.
/// @param image the image.
/// @param stop stops the wait for a lease of its own.
/// @return the lease; or why it was not taken.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::AssetReadLease>
readBeside(const ExecutionScope& scope, const an::consumer::ImageAsset& image, std::stop_token stop = {});

} // namespace an::ui
