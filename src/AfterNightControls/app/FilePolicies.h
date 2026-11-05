// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the product's settings decide about opening and writing a file, taken once as values and kept: the RAW policy
/// an open reads a camera file under, with the codec registry that carries its full-colour white balance, and the
/// output intent an image is written with.
///
/// **A setting changed while an open or a save is on its way reaches the next one, never that one.** An open holds the
/// `RawOpenPolicy` it was submitted with - its registry included - and a save the intent built when it was submitted.
/// Nothing here is process state: the persistence coordinator publishes the policy it built last, and whoever submits
/// work takes it from there.
///
/// **Every setting is read through a fixed table from the text the settings database stores**, so a stored value
/// outside the table reads as the product's default for it rather than as whatever an engine name would parse it to.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/ImageViewFiles.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::ui
{

/// Reads one stored setting: its value, or `fallback` when none is stored.
using SettingReader = std::function<QString(const QString& key, const QString& fallback)>;

/// Stores one setting.
using SettingWriter = std::function<void(const QString& key, const QString& value)>;

/// Where a camera RAW frame's white balance comes from, as the product's settings name it.
enum class RawWhiteBalanceSetting : std::uint8_t
{
    /// Equal gains.
    None,

    /// The camera's daylight gains.
    Daylight,

    /// The gains the camera recorded for the shot.
    CameraAsShot,

    /// Gains measured from the frame.
    Automatic,

    /// The user's three gains.
    Custom
};

/// The white-balance mode a stored setting names: the integer the product stores, `0` none, `1` daylight, `2` as shot,
/// `3` automatic and `4` custom. Anything else - absent, not a number, out of range - reads as daylight.
///
/// @param stored the stored text.
/// @return the mode.
[[nodiscard]] AFTERNIGHTCONTROLS_API RawWhiteBalanceSetting rawWhiteBalanceFromStored(const QString& stored) noexcept;

/// The text a white-balance mode is stored as.
///
/// @param mode the mode.
/// @return its integer, as text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString storedRawWhiteBalance(RawWhiteBalanceSetting mode);

/// The RAW settings an open reads a camera file under.
struct RawOpenSettings
{
    /// Where the white balance comes from.
    RawWhiteBalanceSetting whiteBalance = RawWhiteBalanceSetting::Daylight;

    /// The red, green and blue gains `RawWhiteBalanceSetting::Custom` applies; each positive and finite.
    std::array<double, 3u> customGains{1.0, 1.0, 1.0};

    /// Whether a camera frame is turned by the orientation its camera recorded.
    bool autoRotate = true;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const RawOpenSettings& left, const RawOpenSettings& right) noexcept
    {
        return left.whiteBalance == right.whiteBalance && left.customGains == right.customGains &&
               left.autoRotate == right.autoRotate;
    }
};

/// Reads the RAW settings from the settings store. A custom gain stored as zero, as less, or as anything that is not a
/// finite number reads as one, since the engine refuses such a gain.
///
/// @param read the settings store.
/// @return the settings.
[[nodiscard]] AFTERNIGHTCONTROLS_API RawOpenSettings readRawOpenSettings(const SettingReader& read);

/// Stores the RAW settings, in the texts `readRawOpenSettings` reads.
///
/// @param settings the settings.
/// @param write the settings store.
AFTERNIGHTCONTROLS_API void writeRawOpenSettings(const RawOpenSettings& settings, const SettingWriter& write);

/// The codec registry the product reads and writes files through: FITS, XISF, TIFF, PNG, JPEG and camera RAW, the
/// camera RAW adapter configured as stated.
///
/// @param raw the camera RAW adapter's options.
/// @return the registry, or what an adapter refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<const an::CodecRegistry>>
createProductCodecRegistry(const an::RawCodecOptions& raw = {});

/// The RAW policy an open reads under: the settings, the registry whose camera RAW adapter carries their full-colour
/// white balance, and what a read does to a camera frame after decoding it.
///
/// Immutable, and shared by every open submitted while it is the one published. Its accessors are defined here, so a
/// view that reads under a policy needs only this header.
class RawOpenPolicy
{
public:
    /// The policy of a set of RAW settings, with a registry of its own.
    ///
    /// @param settings the settings; a custom gain that is not positive and finite reads as one.
    /// @return the policy, or what building its registry refused.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<const RawOpenPolicy>>
    create(const RawOpenSettings& settings);

    /// The settings the policy was built from.
    [[nodiscard]] const RawOpenSettings& settings() const noexcept
    {
        return m_settings;
    }

    /// The registry an open decodes through: every container the product reads, the camera RAW adapter carrying the
    /// full-colour white balance.
    [[nodiscard]] const an::CodecRegistry& registry() const noexcept
    {
        return *m_registry;
    }

    /// What a read does to a camera frame after decoding it: the filter-array balance of a mosaic and the turn.
    [[nodiscard]] ViewRawTreatment treatment() const noexcept
    {
        return m_treatment;
    }

    /// The read of a file under the policy, keeping the mosaic as recorded. The request borrows the policy's registry,
    /// so the policy must outlive the read.
    ///
    /// @param path the file.
    /// @return the request.
    [[nodiscard]] ViewReadRequest readRequest(const QString& path) const
    {
        ViewReadRequest request;
        request.path = path;
        request.registry = m_registry.get();
        request.raw = m_treatment;
        return request;
    }

private:
    RawOpenPolicy(const RawOpenSettings& settings, std::shared_ptr<const an::CodecRegistry> registry);

    RawOpenSettings m_settings;
    std::shared_ptr<const an::CodecRegistry> m_registry;
    ViewRawTreatment m_treatment;
};

/// The export settings an image is written with.
struct ExportSettings
{
    /// The compression a TIFF file is written with: none, LZW, deflate or the JPEG transform.
    an::OutputCompression tiffCompression = an::OutputCompression::None;

    /// The compression an XISF file's pixel data are written with: none, zlib as `Deflate`, LZ4, LZ4-HC or
    /// Zstandard. None by default, the form every XISF reader takes; LZ4 and Zstandard are read by the readers of the
    /// specification's first revision.
    an::OutputCompression xisfCompression = an::OutputCompression::None;

    /// Whether a compressed XISF file's samples of more than one byte have their bytes shuffled before they are
    /// compressed, as PixInsight writes them; a file written without compression has nothing to shuffle.
    bool xisfByteShuffle = true;

    /// The checksum an XISF file states for its pixel data, which a reader verifies before it uses them: none or
    /// SHA-256, which every XISF reader verifies.
    an::OutputChecksum xisfChecksum = an::OutputChecksum::None;

    /// The JPEG quality, from one to a hundred.
    std::uint32_t jpegQuality = 95u;

    /// The PNG deflate level, from zero to nine.
    std::uint32_t pngCompressionLevel = 6u;

    /// Whether a raster file - TIFF, PNG or JPEG - carries the camera's EXIF record and the other cards the image was
    /// read with. A FITS or XISF file always carries its header.
    bool embedExif = true;

    /// Whether a file is to carry the image's processing history: read and kept, and written into no file.
    bool embedHistory = false;
};

/// Reads the export settings from the settings store, each through its fixed table: the TIFF compression texts `none`,
/// `lzw`, `zip` and `jpeg`, an unknown one reading as LZW; the XISF compression and checksum texts of
/// `xisfCompressionFromStored` and `xisfChecksumFromStored`; the JPEG quality and PNG level as integers held to their
/// ranges, a text that is not one reading as the default.
///
/// @param read the settings store.
/// @return the settings.
[[nodiscard]] AFTERNIGHTCONTROLS_API ExportSettings readExportSettings(const SettingReader& read);

/// The TIFF compression a stored text names: `none`, `lzw`, `zip` for deflate and `jpeg` for the JPEG transform,
/// compared after trimming; anything else reads as LZW.
///
/// @param stored the stored text.
/// @return the compression.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::OutputCompression tiffCompressionFromStored(const QString& stored);

/// The text a TIFF compression is stored as: `none`, `lzw`, `zip` for deflate and `jpeg` for the JPEG transform. A
/// compression the setting does not name is stored as `lzw`, which is what reading an unknown text gives.
///
/// @param compression the compression.
/// @return the stored text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString storedTiffCompression(an::OutputCompression compression);

/// The XISF compression a stored text names: `none`, `zlib`, `lz4`, `lz4hc` and `zstd`, compared after trimming;
/// anything else reads as none, the default.
///
/// @param stored the stored text.
/// @return the compression, `Deflate` for zlib.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::OutputCompression xisfCompressionFromStored(const QString& stored);

/// The text an XISF compression is stored as: `none`, `zlib`, `lz4`, `lz4hc` and `zstd`. A compression the setting
/// does not name is stored as `none`, which is what reading an unknown text gives.
///
/// @param compression the compression.
/// @return the stored text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString storedXisfCompression(an::OutputCompression compression);

/// The XISF checksum a stored text names: `none` and `sha256`, compared after trimming; anything else reads as none.
/// The setting offers no other algorithm: PixInsight refuses a unit checksummed with SHA3.
///
/// @param stored the stored text.
/// @return the checksum.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::OutputChecksum xisfChecksumFromStored(const QString& stored);

/// The text an XISF checksum is stored as: `none` and `sha256`. A checksum the setting does not name is stored as
/// `none`, which is what reading an unknown text gives.
///
/// @param checksum the checksum.
/// @return the stored text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString storedXisfChecksum(an::OutputChecksum checksum);

/// Stores the export settings, in the texts `readExportSettings` reads.
///
/// @param settings the settings.
/// @param write the settings store.
AFTERNIGHTCONTROLS_API void writeExportSettings(const ExportSettings& settings, const SettingWriter& write);

/// The representation an image is written in, for a container's encode set: the image's own when the container holds
/// it; otherwise the narrowest the container holds that is at least as wide, or the widest it holds when none is,
/// preferring the image's own kind of number - floating point, unsigned or signed - then unsigned integers, signed
/// integers and floating point in that order. So a PNG holds sixteen bits for every image wider than eight and eight
/// bits otherwise, and a JPEG eight bits for every image.
///
/// @param source the image's representation.
/// @param encodeSamples the container's encode set, an OR of `codecSampleBit` values.
/// @return the representation, or nothing when the container holds none of the eight.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::SampleType>
exportSampleType(an::SampleType source, std::uint32_t encodeSamples) noexcept;

/// The output intent the export settings give for writing an image of a description into a container.
///
/// The representation is `exportSampleType`'s; the compression is the container's own - deflate at the stated level for
/// PNG, the JPEG transform at the stated quality for JPEG, the stated one for TIFF, none for FITS and XISF. The JPEG
/// transform is defined for eight-bit samples only, so a TIFF of a wider representation asked to use it is written with
/// LZW; the intent says so. A FITS or XISF file carries the image's cards, its world coordinates among them; a raster
/// file carries them when `ExportSettings::embedExif` says so.
///
/// @param settings the export settings.
/// @param registry the registry the write goes through, asked what the container holds.
/// @param source the image's description.
/// @param container the container.
/// @return the intent; `ErrorCode::Unsupported` for a container the product does not write or the registry has no
///         encoder for, or a mosaic for a container that cannot state its filter array - an X-Trans mosaic for FITS
///         among them; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::OutputIntent> buildExportIntent(const ExportSettings& settings,
                                                                                    const an::CodecRegistry& registry,
                                                                                    const an::ImageDescriptor& source,
                                                                                    an::CodecFormat container);

} // namespace an::ui
