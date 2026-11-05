// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the SER container's private vocabulary, shared by inspection, the source and
// the sink: field offsets, colour identifiers, text fields, typed failures, accounted blocks, and the
// open-file preparation both file entry points run.

#pragma once

#include <AfterNightEngine/IO/SerSequence.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "io/sequences/AccountedBlock.h"
#include "io/sequences/SequenceFile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The SER inspection, source and sink's private machinery.
namespace an::ser_detail
{

/// The most bytes transferred between two cancellation checkpoints. Each transfer is one system call, so
/// a bound that holds a typical frame reads and writes it in one, and a stop waits for at most one copy.
inline constexpr std::uint64_t kTransferBytes = 16u * 1024u * 1024u;

// Where each fixed header field starts, in bytes from the start of the file, and the widths of the text
// fields. Every integer field is little-endian; `FileID` starts at the first byte.
inline constexpr std::size_t kLuIdOffset = 14u; ///< `LuID`, a 32-bit camera series identifier.
inline constexpr std::size_t kColorIdOffset = 18u; ///< `ColorID`, the 32-bit colour identifier.
inline constexpr std::size_t kLittleEndianOffset = 22u; ///< `LittleEndian`, the 32-bit byte-order flag.
inline constexpr std::size_t kWidthOffset = 26u; ///< `ImageWidth`, a 32-bit count of pixels per row.
inline constexpr std::size_t kHeightOffset = 30u; ///< `ImageHeight`, a 32-bit count of rows per frame.
inline constexpr std::size_t kDepthOffset = 34u; ///< `PixelDepthPerPlane`, the 32-bit significant bits.
inline constexpr std::size_t kFrameCountOffset = 38u; ///< `FrameCount`, a 32-bit count read as unsigned.
inline constexpr std::size_t kObserverOffset = 42u; ///< `Observer`, a text field of `kTextBytes` bytes.
inline constexpr std::size_t kInstrumentOffset = 82u; ///< `Instrument`, a text field of `kTextBytes` bytes.
inline constexpr std::size_t kTelescopeOffset = 122u; ///< `Telescope`, a text field of `kTextBytes` bytes.
inline constexpr std::size_t kDateTimeOffset = 162u; ///< `DateTime`, the 64-bit local start time in SER ticks.
inline constexpr std::size_t kDateTimeUtcOffset = 170u; ///< `DateTime_UTC`, the 64-bit UTC start time in SER ticks.
inline constexpr std::size_t kFileIdBytes = 14u; ///< The width of `FileID`, the header's first field.
inline constexpr std::size_t kTextBytes = 40u; ///< The width of each of the three text fields.

/// The fixed header, as bytes.
using HeaderBytes = std::array<std::byte, kSerHeaderBytes>;

/// What one colour identifier stores.
struct ColorLayout
{
    ChannelSchema schema; ///< Mono for mono, mosaic and CMY containers; RGB for both colour orders.
    CfaPattern pattern; ///< The Bayer pattern, or none.
    std::uint32_t channels; ///< Samples per pixel, all interleaved in one plane.
    bool exchanged; ///< Stored BGR, delivered RGB.
    bool cmy; ///< A mosaic the filter-array vocabulary cannot describe.
};

/// The layout a stored colour identifier names, or nothing for an identifier SER does not define.
[[nodiscard]] std::optional<ColorLayout> colorLayoutOf(std::int32_t colorId) noexcept;

/// The colour identifier that stores a frame description, or nothing when none does.
[[nodiscard]] std::optional<std::int32_t> colorIdOf(const RawFrameDescriptor& frame) noexcept;

/// Whether a stored byte-order flag means big-endian two-byte samples under a convention.
[[nodiscard]] bool storesBigEndian(std::int32_t flag, SerEndianConvention convention) noexcept;

/// Reads a little-endian 32-bit field at `offset`; the span must hold its four bytes.
[[nodiscard]] std::uint32_t readLe32(std::span<const std::byte> bytes, std::size_t offset) noexcept;

/// Reads a little-endian 64-bit field at `offset`; the span must hold its eight bytes.
[[nodiscard]] std::uint64_t readLe64(std::span<const std::byte> bytes, std::size_t offset) noexcept;

/// Writes a little-endian 32-bit field at `offset`; the span must hold its four bytes.
void writeLe32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept;

/// Writes a little-endian 64-bit field at `offset`; the span must hold its eight bytes.
void writeLe64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) noexcept;

/// A text field as UTF-8: the bytes before the first NUL, trailing spaces removed, Latin-1 when the
/// bytes are not UTF-8.
[[nodiscard]] std::string decodeText(std::span<const std::byte> field);

/// A refusal carrying its sequence reason and whichever of the parameter, the path and further
/// fields apply.
[[nodiscard]] Status serFailure(ErrorCode code, SequenceFailure reason, const char* message, std::string_view parameter,
                                std::string_view path, std::vector<StatusContextField> extra = {}, Status cause = {});

/// An `ErrorCode::InvalidArgument` refusal naming the parameter, with no sequence reason.
[[nodiscard]] Status argumentFailure(const char* message, const char* parameter);

/// An operating-system refusal: `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure`, carrying
/// the platform's error code and the path.
[[nodiscard]] Status ioFailure(int platformError, const char* message, std::string_view path);

/// Refuses an option outside its enumeration as `ErrorCode::InvalidArgument` naming the option.
[[nodiscard]] Status checkOptions(const SerSourceOptions& options);

/// Decodes and judges a header against a container size; the path only names a failure.
///
/// The header is judged in a fixed order: the colour identifier, the depth, the extent, the engine's
/// limits, the stated meaning, and then the frame count against the payload. A trailer is present when
/// the count was not recovered and the bytes after the frames hold one eight-byte tick per frame.
///
/// A header span shorter than `kSerHeaderBytes`, or an option outside its enumeration, is
/// `ErrorCode::InvalidArgument`. A container shorter than the header, or a payload holding fewer frames
/// than the header counts when the options do not recover the count, is `ErrorCode::InvalidData` with
/// `SequenceFailure::TruncatedContainer`. An unknown colour identifier, a depth outside one to sixteen
/// bits, a width or height that is not positive, or a zero count over a payload that holds frames is
/// `ErrorCode::InvalidData` with `SequenceFailure::MalformedContainer`. A CMY mosaic the options refuse
/// is `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding`. Frames beyond the engine's
/// limits are `ErrorCode::LimitExceeded`, and a stated colour space or numeric domain that cannot
/// describe the frames is `ErrorCode::IncompatibleArguments`, both with
/// `SequenceFailure::DescriptorMismatch`.
[[nodiscard]] Result<SerContainer> inspect(std::span<const std::byte> header, std::uint64_t fileBytes,
                                           const SerSourceOptions& options, const EngineLimits& limits,
                                           std::string_view path);

/// One allocation from an engine context's memory resource, shared with the other sequence adapters.
using AccountedBlock = sequence_detail::AccountedBlock;

/// What a trailer's raw ticks say.
struct TimelineSummary
{
    std::uint64_t present = 0u; ///< Entries naming an instant.
    bool ordered = false; ///< Every entry names an instant and none decreases.
    std::optional<UtcInstant> first; ///< The first entry's instant.
    std::optional<UtcInstant> last; ///< The last entry's instant.
    /// The rate: the present entries less one over the span from the earliest to the latest,
    /// in frames per second; absent with fewer than two present entries or a zero span.
    std::optional<double> frameRate;
};

/// Summarizes raw trailer ticks; an empty span is no trailer.
[[nodiscard]] TimelineSummary summarize(std::span<const std::uint64_t> ticks) noexcept;

/// An opened and judged container with its trailer decoded into host-order ticks.
struct OpenedContainer
{
    SequenceFile file; ///< Open for positioned reads.
    SerContainer container; ///< What the header and the size say.
    AccountedBlock ticks; ///< One raw tick per frame when a trailer is present, otherwise nothing.
    TimelineSummary summary; ///< What the ticks say.
    bool timeline; ///< Whether the ticks become the presentation timeline under the options.
};

/// Opens, judges and reads the trailer of a container, refusing what a source refuses.
///
/// Beside `checkContext`'s and `inspect`'s refusals: an engine context that holds nothing is
/// `ErrorCode::NotInitialized`; a path that is not bounded UTF-8 is
/// `ErrorCode::InvalidArgument`; a file the platform refuses is `ErrorCode::ExternalFailure` with
/// `SequenceFailure::IoFailure`; a file that ends inside its header or trailer is `ErrorCode::InvalidData`
/// with `SequenceFailure::TruncatedContainer`; and a timeline the options require but the trailer does
/// not hold is `ErrorCode::InvalidData` with `SequenceFailure::InvalidTimestamps`. The trailer's memory
/// comes from the engine context, whose refusal is returned as it stands.
[[nodiscard]] Result<OpenedContainer> open(const EngineContext& engine, const ExecutionContext& execution,
                                           std::string_view path, const SerSourceOptions& options);

/// The source encoding and header cards every frame of a container carries: `FileID`, `LuID`, `ColorID`,
/// `LittleEndian`, `PixelDepthPerPlane`, `DateTime` and `DateTime_UTC`, in the `Ser` namespace.
[[nodiscard]] Result<CodecMetadata> sourceCodecMetadata(const SerContainer& container);

} // namespace an::ser_detail
