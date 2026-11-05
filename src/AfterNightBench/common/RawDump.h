// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the engine-neutral raw-dump format.
//
// A raw dump is a descriptor JSON plus a file of raw sample bytes. It exists so that real image
// data can reach a consumer that has no file reader of its own: the exporter that produces one is
// linked to an engine that can decode the source file, and the reader needs only this format.
//
// The descriptor carries the source's metadata cards as *data*, not as a parsed model, which is
// what lets a consumer test real headers before it has a codec. Reading a card out of this JSON is
// not file-format parsing; the file format was already parsed by whoever exported it.
//
// The payload lives beside the descriptor rather than inside it. Base64 in JSON would inflate a
// 178 MB frame by a third and force the whole thing through a string, and the digest recorded here
// is what ties the two files together anyway.

#pragma once

#include "Descriptors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// One preserved metadata card, exactly as the source file carried it.
///
/// Kept as text rather than a typed value on purpose: the point of preserving a card is that a
/// consumer can check what the file actually said, and a value that has been through someone
/// else's type conversion no longer answers that question.
struct MetadataCard
{
    std::string key;
    std::string value;
    std::string comment;

    friend bool operator==(const MetadataCard& lhs, const MetadataCard& rhs) = default;
};

/// The descriptor half of a raw dump: everything except the sample bytes.
struct RawDump
{
    /// Bumped when the on-disk shape changes incompatibly.
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;

    /// Shape and representation of the payload.
    SampleDescriptor descriptor;

    /// Where the payload came from, and what produced this dump.
    std::string sourcePath;
    std::string sourceFormat; ///< The source file's format as the exporting engine named it.
    std::string exporterVersion;

    /// Name of the payload file, relative to the directory holding the descriptor.
    ///
    /// Relative so that a dump can be moved or archived as a pair. An absolute path here would
    /// make a dump valid only on the machine that wrote it.
    std::string payloadFile;

    std::uint64_t payloadBytes = 0;
    std::string payloadDigest; ///< SHA-256 over the payload file, lowercase hex.

    /// The source's header and metadata cards, in the order the source carried them.
    std::vector<MetadataCard> cards;

    /// Empty when the dump is well formed; otherwise one message per problem.
    [[nodiscard]] std::vector<std::string> validate() const;

    /// The card named `key`, or nullptr. Comparison is exact, as headers are case-sensitive here.
    [[nodiscard]] const MetadataCard* findCard(const std::string& key) const;
};

/// Write `dump` and `payload` as a descriptor JSON at `descriptorPath` plus its payload file.
///
/// The payload file goes beside the descriptor, named by `dump.payloadFile`; both are written to a
/// pending file beside them and renamed into place, so an interrupted export cannot leave a descriptor that
/// points at a half-written payload. `dump.payloadBytes` and `dump.payloadDigest` are computed
/// from `payload` and overwritten, because a digest supplied by a caller describes what the caller
/// believed rather than what was written.
///
/// Returns false and sets `error` on any failure, leaving no partial pair behind.
[[nodiscard]] bool writeRawDump(const std::string& descriptorPath, RawDump& dump,
                                const std::vector<std::uint8_t>& payload, std::string& error);

/// Read the descriptor at `descriptorPath` and the payload it names.
///
/// Verifies the payload against the recorded size and digest, because a dump whose payload has
/// been replaced would otherwise feed a consumer data that its descriptor does not describe.
/// Returns false and sets `error` when either file is missing, malformed, or inconsistent.
[[nodiscard]] bool readRawDump(const std::string& descriptorPath, RawDump& out, std::vector<std::uint8_t>& payload,
                               std::string& error);

} // namespace an::bench
