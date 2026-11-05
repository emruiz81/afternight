// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineBench - the raw-dump exporter.
//
// A raw dump is the engine-neutral pair `RawDump` defines: a descriptor JSON naming a file's shape,
// representation and cards, and the file's samples beside it, planar, top row first, tightly packed and
// little-endian. It is how real image data reaches something that has no codec of its own - the corpus
// preparation scripts, and any consumer tested on real files before it can read one.
//
// This exporter decodes through the engine and writes that pair. `Test_EngineFixtureBridge` holds it to
// the recorded exports of the files checked in beside the record: the descriptor and the payload's
// digest equal, a file the record refused refused here too and nothing written, and every card
// difference named in a register.

#pragma once

#include "RawDump.h"

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRequests.h>

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench::engine
{

/// Identity recorded in every dump this exporter writes.
///
/// A dump names the exporter that produced it, so a difference traced back to an export can be
/// attributed. Bump the trailing number when the exported bytes or cards change for the same input.
inline constexpr const char* kRawDumpExporterVersion = "AfterNightEngineBench raw-dump 1";

/// The name a dump records for a container format.
///
/// The spellings a dump carries: `FITS`, `XISF`, `TIFF`, `PNG`, `JPEG`, `RAW`, and `unknown` for
/// anything else.
///
/// @param format the container a file was read as.
/// @return a null-terminated string literal, never freed.
[[nodiscard]] const char* rawDumpFormatName(an::CodecFormat format) noexcept;

/// Build the descriptor and payload for a decoded frame.
///
/// The payload is the frame's samples planar, top row first, tightly packed and little-endian,
/// whatever arrangement, row order and byte order the decoder handed back. The cards are the
/// metadata's codec cards in the order they were read, each with its key, value and comment; a card
/// with no key is left out, because it cannot be looked up or written back.
///
/// @param decoded what the engine decoded.
/// @param sourcePath recorded as provenance; may be empty for a frame that came from memory.
/// @param sourceFormat the container's name as `rawDumpFormatName` spells it; may be empty.
/// @param dump receives the descriptor; its payload file name is left empty for the caller to set.
/// @param payload receives the sample bytes.
/// @param error receives why the frame cannot be described in the schema's terms: a representation
///        the schema does not name, a filter array other than a two-by-two Bayer pattern, or a frame
///        whose rows cannot be read. A refusal rather than a fallback, because a dump whose descriptor
///        does not match its bytes misleads every later reader.
/// @return true when `dump` and `payload` were filled.
[[nodiscard]] bool buildRawDump(const an::CodecDecodeResult& decoded, const std::string& sourcePath,
                                const std::string& sourceFormat, RawDump& dump, std::vector<std::uint8_t>& payload,
                                std::string& error);

/// Decode `sourcePath` through this engine and write the pair at `descriptorPath`.
///
/// The file is read with every codec the product reads - FITS, XISF, TIFF, PNG, JPEG and camera RAW -
/// chosen by its extension. The payload file is named after the descriptor, with `.anraw`, and
/// written beside it. Nothing partial is left behind on any failure.
///
/// @param sourcePath the file to read, UTF-8.
/// @param descriptorPath where the descriptor goes, UTF-8.
/// @param error receives why nothing was written: the file could not be decoded, could not be
///        described, or the pair could not be written.
/// @return true when the pair was written.
[[nodiscard]] bool exportRawDump(const std::string& sourcePath, const std::string& descriptorPath, std::string& error);

} // namespace an::bench::engine
