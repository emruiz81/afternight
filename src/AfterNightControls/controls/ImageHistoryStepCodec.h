// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a history step is written to a file and read back: the codec every history writes through, and the two formats
/// behind it.
///
/// A step is a whole state of an image - its samples, its description, every metadata object, its validity mask and
/// the identity the history wrote it for - and a codec is what puts that in one file and gets it back. A history
/// writes every step through the codec it was given, which says where the step landed, so the history itself knows
/// nothing of a format.
///
/// **The history's codec** (`historyStepCodec`) is the one every history is made with, in a project or not. It writes a
/// step as an XISF image, its samples compressed as stated, and as a snapshot beside the others in the two cases an
/// XISF image cannot be the step: a record longer than one image's cards carry (below), and a session whose engine
/// has no XISF adapter. No edit is refused for the format. A step is read by the codec its file's extension names,
/// so a history's directory may hold both kinds and a step written under one compression is read under any other.
///
/// **The snapshot codec** is the engine's image snapshot (`IO/ImageSnapshot.h`): the format a history wrote before
/// there was another, and what the history's codec falls back to.
///
/// **The XISF codec** writes a step as an XISF file that any reader of the format opens as the image: the samples in
/// the format's own representation, and the cards the format writes back - the object, the exposure, the astrometric
/// solution - where a person looks for them. XISF has no vocabulary for the rest of a step - what was done to the
/// samples, the lineage, a decision about how the image is written, the cards of another format, the mask - and a
/// read of an XISF file restates an image's description from the file's own. So the file also carries the step's
/// record, which this application alone reads, as two properties of the image: `AfterNight:HistoryStep:Record`, the
/// engine's own snapshot of the step's metadata, description and identity, and `AfterNight:HistoryStep:Validity`,
/// the mask's bits; each compressed and written as text. A read takes the samples from the image and everything
/// else from the record, and refuses a file without one.
///
/// Four things about an XISF step are the format's, not the step's:
///
/// - Its header opens without the XML declaration and the comment the specification opens one with
///   (`OutputHeaderProlog::Blank`): spaces stand where they were. A virus scanner takes a file that opens with
///   them for markup and reads all of it for tags, which the bytes of normalized 32-bit floating samples are full
///   of - half a second of a processor for every step, and a wait for a step read while it is at it. This is the
///   one departure from the specification, made for a history's steps alone; this application and PixInsight read
///   such a file as any other, and every other XISF file the application writes opens as the specification says.
/// - The record travels in the image's cards, of which one image holds `kMaxCodecCards` of
///   `kMaxCodecCardTextBytes` each. A record longer than `kMaxXisfStepRecordTextBytes` - in practice a mask with no
///   pattern to compress, of an image of well over a hundred megapixels - is refused when it is written, with
///   nothing published.
/// - XISF has no signed integers and counts rows from the top. A signed image's samples are written as the unsigned
///   codes of the same bits, and an image whose rows count from the bottom in the order it holds them; this
///   application reads both back exactly, and another reader shows those codes and that order.
/// - The image's own cards are written beside the record for another reader, less the ones XISF cannot spell; when
///   the format still refuses them, the step is written with its record and its identity alone.
///
/// **FITS is not a codec.** A FITS header is where a record would travel, and the engine's FITS writer takes a
/// header of at most `kMaxCodecCards` records of eighty bytes: about 400 KiB of record, which the mask of an
/// ordinary image exceeds as soon as it does not compress to almost nothing. FITS holds such data in an extension,
/// which the engine's FITS adapter does not write.
///
/// **A step is not flushed to stable storage** (`StepDurability::CloseOnly`), in a project or not: it is published
/// whole or not at all against a crash of the process, and a power cut within the seconds the system takes to write it
/// out may leave a step that does not read - which a later session leaves out of the history and reports. Flushing
/// each step costs the edit that waits for it, and the choice is the one a step's checksum was decided by: the write's
/// time comes first. `StepDurability::Flushed` is what turning that choice round takes.
///
/// Every codec is stateless and safe to call from any thread; a write and a read each run under one admitted operation
/// of the session, with one thread.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace an::ui::history
{

/// Writes a history step to a file and reads it back.
class AFTERNIGHTCONTROLS_API StepCodec
{
public:
    virtual ~StepCodec() = default;

    /// The codec's name: `history`, `snapshot` or `xisf`.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// The extension of a step's file, with its dot: the one a step is asked to be written under.
    [[nodiscard]] virtual std::string_view fileExtension() const noexcept = 0;

    /// Writes the image under `image` as a step at `path`, for `identity`. Called on the backup writer's thread.
    ///
    /// @param session the session the image belongs to.
    /// @param image the copy, under a read lease.
    /// @param path the file, as UTF-8.
    /// @param identity the identity the step is written for.
    /// @param stop stops the write; a stopped write leaves no file.
    /// @return the file the step is whole in - `path`, or the file beside it under another extension when the codec
    ///         wrote the step in its other format - or why nothing was published.
    [[nodiscard]] virtual an::Result<std::string> write(const an::consumer::EngineSession& session,
                                                        const an::consumer::AssetReadLease& image,
                                                        const std::string& path, const an::ArtifactIdentity& identity,
                                                        std::stop_token stop) const = 0;

    /// Reads a step back as a new asset of the session, with a new identity.
    ///
    /// @param session the session the asset is made in.
    /// @param path the file a write answered, as UTF-8.
    /// @param expected the identity the history wrote the step for; a file that states another is refused.
    /// @param stop stops the admission and the read.
    /// @return the asset; `ErrorCode::InvalidData` for a file that is not a step of this codec or not the one
    ///         expected; or what the engine refuses.
    [[nodiscard]] virtual an::Result<an::consumer::ImageAsset> read(const an::consumer::EngineSession& session,
                                                                    const std::string& path,
                                                                    const an::ArtifactIdentity& expected,
                                                                    std::stop_token stop) const = 0;
};

using StepCodecPtr = std::shared_ptr<const StepCodec>;

/// How an XISF step's samples are compressed.
enum class StepCompression : std::uint8_t
{
    /// Not compressed.
    None,

    /// LZ4, the fastest to write and to read.
    Lz4,

    /// Zstandard, the smallest.
    Zstd
};

/// The compression's name where a setting or a project states it: `none`, `lz4` or `zstd`.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string_view stepCompressionName(StepCompression compression) noexcept;

/// Whether a step's bytes are on the device before its file takes its name.
enum class StepDurability : std::uint8_t
{
    /// Closed and renamed without a flush: whole or absent against a crash of the process, not of the machine. What
    /// every history writes.
    CloseOnly,

    /// Flushed to the device before the rename, so a power cut cannot leave a whole name over a part of a file.
    Flushed
};

/// The longest record an XISF step carries, as the text its cards hold: what the cards of one image hold, less room
/// for the image's own. A record is the step's snapshot and its mask, each compressed and written as text, so this
/// is three quarters as many bytes of both.
inline constexpr std::size_t kMaxXisfStepRecordTextBytes = std::size_t{24u} * 1024u * 1024u;

/// The extension of a step written as a snapshot, and of one written as an XISF image.
inline constexpr std::string_view kSnapshotStepExtension = ".ansnap";
inline constexpr std::string_view kXisfStepExtension = ".xisf";

/// The codec every history writes its steps through: an XISF image with the blank prolog, its samples compressed as
/// stated, or a snapshot under `.ansnap` beside it for a step an XISF image cannot be - one whose record is longer
/// than `kMaxXisfStepRecordTextBytes`, or any step of a session whose engine has no XISF adapter. It reads a step by
/// the codec its file's extension names.
[[nodiscard]] AFTERNIGHTCONTROLS_API StepCodecPtr historyStepCodec(
    StepCompression compression = StepCompression::Lz4, StepDurability durability = StepDurability::CloseOnly);

/// The codec of the engine's image snapshot, `.ansnap`.
[[nodiscard]] AFTERNIGHTCONTROLS_API StepCodecPtr
snapshotStepCodec(StepDurability durability = StepDurability::CloseOnly);

/// The codec that writes a step as an XISF image, `.xisf`, its samples compressed as stated; a step beyond what an
/// XISF image carries is refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API StepCodecPtr xisfStepCodec(StepCompression compression = StepCompression::Lz4,
                                                                StepDurability durability = StepDurability::CloseOnly);

} // namespace an::ui::history
