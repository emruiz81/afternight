// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What must hold when a sequence container the engine did not write is opened and read to its end.
//
// A sequence is a codec with a cursor, and the cursor is what makes it its own target. A decoder that
// reads one image either produces it or refuses; a source has to produce a stated number of frames of
// a stated geometry, in order, from a header that a mutation may have made inconsistent with the file
// behind it. The failure that matters is not a wrong pixel but a frame count derived from a length
// field, a seek past the end, and a read loop that never reaches the end at all.
//
// Three containers are here. The SER reader is checked twice - once through the in-memory header
// inspection, which is the cheapest useful call in this campaign, and once through the real source -
// because the first is what the second's decisions are made from. The image-list manifest is a text
// document, so its reader is a parser in the ordinary sense. The video path is behind a mode byte, so
// only a fraction of inputs pay for what FFmpeg costs to refuse.
//
// The claims:
//
// * **An inspection agrees with the file.** A container that inspected states a frame count and a
//   layout whose bytes fit in the file it came from; a header claiming more frames than the file holds
//   is exactly what a mutated length field produces.
// * **A source that opens can be read to its end.** Reading past the end is end of file rather than a
//   failure, and the frames that come back match the descriptor the source published.
// * **Reading ends.** A source whose frame count came from the file cannot be allowed to make the read
//   loop unbounded, so the loop is bounded by the count the source itself declared and by the
//   watchdog behind it.
// * **A seek resolves or refuses.** A seek that reported a position leaves the source able to read
//   from it.

#include "../FuzzEngine/FuzzHarness.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/IO/SequenceFrame.h>
#include <AfterNightEngine/IO/SerSequence.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

/// How many bytes this target's engine may hold at once.
///
/// Stated for the same reason the codec target states one: the default budget refuses nothing, so a
/// container declaring an enormous frame would be answered by the platform allocator rather than by
/// the engine, and what a run found would be a property of the machine it ran on. Far above anything
/// the seed corpus opens and far below any machine this runs on.
constexpr std::uint64_t kMemoryBudgetBytes = 256ull * 1024ull * 1024ull;

/// The engine every input is opened in.
///
/// @return the context, valid for the life of the process.
[[nodiscard]] const an::EngineContext& engine()
{
    static const an::EngineContext context = []() {
        an::EngineContextOptions options;
        options.maxConcurrency = 2u;
        options.limits.memoryBudgetBytes = kMemoryBudgetBytes;
        an::Result<an::EngineContext> created = an::EngineContext::create(options);
        AN_FUZZ_CHECK(created.hasValue(), "the engine context this target opens every input in must exist");
        return std::move(*created);
    }();
    return context;
}

/// Where candidate containers are written.
///
/// @return the directory, which exists.
[[nodiscard]] const std::filesystem::path& scratchDirectory()
{
    static const std::filesystem::path directory = []() {
        const std::string root = an::fuzz::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
        const std::filesystem::path base =
            root.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(root);
        const std::filesystem::path made = base / "Fuzz_EngineSequence";
        std::error_code ignored;
        std::filesystem::create_directories(made, ignored);
        return made;
    }();
    return directory;
}

/// Writes the input where a source can be opened over it.
///
/// @param extension what the path ends in.
/// @param bytes what to write.
/// @return the path, or an empty string when the write failed.
[[nodiscard]] std::string writeCandidate(const char* extension, std::span<const std::uint8_t> bytes)
{
    const std::filesystem::path path = scratchDirectory() / (std::string("candidate") + extension);
    const std::string spelled = path.string();
    std::FILE* file = an::fuzz::openFile(spelled.c_str(), "wb");
    if (file == nullptr)
    {
        return {};
    }
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1u, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    return written && closed ? spelled : std::string{};
}

/// How many frames one input may make this target read.
///
/// A header names its own frame count and a mutation can name a very large one. The source refuses a
/// count the file cannot hold, but reading even a thousand small frames per input would make the sweep
/// a throughput benchmark rather than a search. The cap is a property of the campaign, not of the
/// engine, and a source that declares more is read to this bound and then closed.
constexpr std::uint64_t kMaxFramesRead = 24u;

/// How many bytes of an input are the selections rather than the container.
///
/// The mode, whether a seek is made and its three fields, the two SER options and the file size the
/// header inspection is told about. A seed carries this prefix so the container it holds reaches the
/// reader it was written for.
constexpr std::size_t kPrefixBytes = 9u;

/// Reads a source to its end, or to the campaign's bound.
///
/// @param source the open source.
/// @param execution the operation the reads belong to.
void readToEnd(an::FrameSource& source, const an::ExecutionContext& execution)
{
    const an::SequenceDescriptor* descriptor = source.descriptor();
    AN_FUZZ_CHECK(descriptor != nullptr, "an open source publishes a descriptor");

    // A source that published a frame count has promised that many frames and an end of file after
    // them, so a refusal at or past that point is a defect rather than a malformed file. Without this
    // the target accepted any failure as "a refused read names a reason" and a source that returned a
    // failure in place of end of file passed - which a deliberate defect proved it did.
    const std::uint64_t declared = descriptor->frameCount.value_or(0u);
    const bool countIsKnown = descriptor->frameCount.has_value() && declared <= kMaxFramesRead;

    std::uint64_t read = 0u;
    bool reachedEnd = false;
    while (read < kMaxFramesRead)
    {
        const std::uint64_t before = source.nextIndex();
        const an::Result<an::SequenceReadResult> result = source.read(engine(), execution);
        if (!result.hasValue())
        {
            AN_FUZZ_CHECK(!result.status().ok(), "a refused read names a reason");
            AN_FUZZ_CHECK(source.nextIndex() == before, "a failed read does not advance the source");
            AN_FUZZ_CHECK(!countIsKnown || read < declared,
                          "a source that declared its frames ends with end of file, not with a failure");
            return;
        }
        if (!result->frame.has_value())
        {
            reachedEnd = true;
            break;
        }
        AN_FUZZ_CHECK(source.nextIndex() == before + 1u, "a read that produced a frame advances by one");
        an::fuzz::countReached("frames-read");
        ++read;
    }

    if (!reachedEnd)
    {
        return;
    }
    // End of file repeats rather than becoming a failure, which is what lets a caller poll a source it
    // has already exhausted.
    const an::Result<an::SequenceReadResult> again = source.read(engine(), execution);
    AN_FUZZ_CHECK(again.hasValue() && !again->frame.has_value(), "reading past the end is end of file, not a failure");
}

/// A seek either resolves to a position or refuses, and a resolved position is one that can be read.
///
/// @param source the open source.
/// @param execution the operation the seek belongs to.
/// @param request where to go.
void seekAndRead(an::FrameSource& source, const an::ExecutionContext& execution, const an::SequenceSeekRequest& request)
{
    const an::Result<an::SequencePosition> position = source.seek(execution, request);
    if (!position.hasValue())
    {
        AN_FUZZ_CHECK(!position.status().ok(), "a refused seek names a reason");
        return;
    }
    const an::Result<an::SequenceReadResult> result = source.read(engine(), execution);
    AN_FUZZ_CHECK(result.hasValue() || !result.status().ok(), "a read after a resolved seek answers");
}

} // namespace

namespace an::fuzz
{

TargetInfo targetInfo()
{
    // A SER header is 178 bytes and a frame is as large as its geometry says; the bound is what keeps
    // one mutated header from making the target write a large file per iteration.
    return {.name = "Fuzz_EngineSequence", .maxInputBytes = 131072u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    std::vector<std::vector<std::uint8_t>> corpus;

    /// Gives a container the prefix that offers it to the reader it was written for.
    ///
    /// @param mode which reader, as the entry point's mode byte spells it.
    /// @param bytes the container.
    /// @return the seed.
    const auto seeded = [](std::uint8_t mode, const std::vector<std::uint8_t>& bytes) {
        std::vector<std::uint8_t> seed;
        seed.reserve(kPrefixBytes + bytes.size());
        seed.push_back(mode);
        seed.push_back(1u); // No seek; a mutation to a multiple of three asks for one.
        seed.push_back(0u); // The seek's kind, index and the two bytes its time is a difference of.
        seed.push_back(0u);
        seed.push_back(0u);
        seed.push_back(0u);
        seed.push_back(0u); // The compatible endian convention and the exact frame-count policy.
        seed.push_back(0u);
        seed.push_back(0u); // The file size the header inspection is told about, when it is not real.
        seed.insert(seed.end(), bytes.begin(), bytes.end());
        return seed;
    };

    // A SER container written by hand, because its header is a fixed 178-byte record and writing it
    // out here means every field is in front of the mutator from the first iteration. Two 4x3
    // sixteen-bit mono frames follow it.
    {
        constexpr std::uint32_t kWidth = 4u;
        constexpr std::uint32_t kHeight = 3u;
        constexpr std::uint32_t kFrames = 2u;
        constexpr std::size_t kHeaderBytes = 178u;
        std::vector<std::uint8_t> ser(kHeaderBytes, 0u);
        const char* signature = "LUCAM-RECORDER";
        std::memcpy(ser.data(), signature, 14u);
        const auto put32 = [&ser](std::size_t offset, std::uint32_t value) {
            ser[offset + 0u] = static_cast<std::uint8_t>(value & 0xFFu);
            ser[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
            ser[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
            ser[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
        };
        put32(14u, 0u); // LuID.
        put32(18u, 0u); // ColorID: mono.
        put32(22u, 0u); // LittleEndian: the flag whose meaning this format inverts.
        put32(26u, kWidth);
        put32(30u, kHeight);
        put32(34u, 16u); // Bit depth.
        put32(38u, kFrames);
        const std::size_t frameBytes = static_cast<std::size_t>(kWidth) * kHeight * 2u;
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            for (std::size_t index = 0u; index < frameBytes; ++index)
            {
                ser.push_back(static_cast<std::uint8_t>(index * 11u + frame));
            }
        }
        // Mode zero inspects the header with the real file size; modes three and four open the file.
        corpus.push_back(seeded(0u, ser));
        corpus.push_back(seeded(3u, ser));
    }

    // The image-list manifest, which is a text document and so seeds as text.
    static constexpr const char* kManifests[] = {
        "<?xml version=\"1.0\"?>\n<sequence version=\"1\" timestamp=\"0\" width=\"4\" height=\"3\" channels=\"1\" "
        "isCFA=\"0\" fps=\"1\">\n  <frame path=\"frame-0.fits\"/>\n  <frame path=\"frame-1.fits\"/>\n</sequence>\n",
        "<?xml version=\"1.0\"?>\n<sequence version=\"999\"/>\n",
        "<?xml version=\"1.0\"?>\n<sequence>\n",
        "not a document at all",
        "",
    };
    for (const char* text : kManifests)
    {
        const std::string document(text);
        corpus.push_back(seeded(5u, {reinterpret_cast<const std::uint8_t*>(document.data()),
                                     reinterpret_cast<const std::uint8_t*>(document.data()) + document.size()}));
    }

    // The heads of the two video containers the video path can be pointed at. A mutation of a
    // signature is how a reader that recognised a container by its first four bytes and then trusted
    // the rest of it is found.
    static constexpr std::uint8_t kRiffAvi[] = {'R', 'I', 'F', 'F', 0x24u, 0x00u, 0x00u, 0x00u,
                                                'A', 'V', 'I', ' ', 'L',   'I',   'S',   'T'};
    static constexpr std::uint8_t kMatroska[] = {0x1Au, 0x45u, 0xDFu, 0xA3u, 0x01u, 0x00u,
                                                 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x1Fu};
    corpus.push_back(seeded(7u, {std::begin(kRiffAvi), std::end(kRiffAvi)}));
    corpus.push_back(seeded(7u, {std::begin(kMatroska), std::end(kMatroska)}));

    return corpus;
}

} // namespace an::fuzz

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);

    // The selections are the input's first bytes and the container is everything after them, as
    // `kPrefixBytes` describes, so a seed reaches the reader it was written for.
    const std::uint32_t mode = input.takeBelow(8u);
    const bool wantSeek = input.takeBelow(3u) == 0u;
    const an::SequenceSeekRequest seek{.kind = input.takeEnum(an::SequenceSeekKind::TimeAtOrBefore),
                                       .index = static_cast<std::uint64_t>(input.take()),
                                       .ticks = static_cast<std::int64_t>(input.take()) -
                                                static_cast<std::int64_t>(input.take())};
    an::SerSourceOptions inspectOptions;
    inspectOptions.endianConvention = input.takeEnum(an::SerEndianConvention::Specification);
    inspectOptions.frameCountPolicy = input.takeEnum(an::SerFrameCountPolicy::CompleteFrames);
    const std::uint64_t claimedOverride = static_cast<std::uint64_t>(input.take()) << 8u;

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine(), {});
    if (!execution.hasValue())
    {
        return 0;
    }
    const std::span<const std::uint8_t> bytes = input.rest();

    if (mode < 3u)
    {
        // The header inspection, which needs no file at all. It is given the file size the bytes
        // really are and, sometimes, one they are not, because the reader is entitled to trust the
        // size it is handed and must still not read past what it was given.
        const std::uint64_t claimedBytes = mode == 0u ? bytes.size() : claimedOverride;
        const an::Result<an::SerContainer> inspected = an::inspectSerContainer(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()), claimedBytes,
            inspectOptions, engine().limits());
        an::fuzz::countReached(inspected.hasValue() ? "headers-inspected" : "headers-refused");
        if (inspected.hasValue())
        {
            AN_FUZZ_CHECK(inspected->fileBytes == claimedBytes, "an inspection reports the file size it was given");
            AN_FUZZ_CHECK(inspected->layout.totalBytes() != 0u, "a frame of an inspected container has bytes in it");
            AN_FUZZ_CHECK(inspected->frameCount == 0u ||
                              inspected->frameCount <= claimedBytes / inspected->layout.totalBytes(),
                          "a container cannot hold more frames than its file has room for");
            AN_FUZZ_CHECK(inspected->trailingBytes <= claimedBytes, "a trailer is inside the file");
        }
        else
        {
            AN_FUZZ_CHECK(!inspected.status().ok(), "a refused inspection names a reason");
        }
    }

    if (mode < 5u)
    {
        const std::string path = writeCandidate(".ser", bytes);
        if (!path.empty())
        {
            an::SerSourceOptions options;
            an::Result<an::FrameSource> source = an::createSerSource(engine(), *execution, path, options);
            an::fuzz::countReached(source.hasValue() ? "ser-opened" : "ser-refused");
            if (source.hasValue())
            {
                if (wantSeek)
                {
                    seekAndRead(*source, *execution, seek);
                }
                readToEnd(*source, *execution);
                AN_FUZZ_CHECK(source->close().ok(), "an open source closes");
            }
            else
            {
                AN_FUZZ_CHECK(!source.status().ok(), "a refused source names a reason");
            }
        }
    }
    else if (mode == 5u || mode == 6u)
    {
        const std::string path = writeCandidate(".seq", bytes);
        if (!path.empty())
        {
            const an::Result<an::ImageListManifest> manifest = an::readImageListManifest(engine(), *execution, path);
            an::fuzz::countReached(manifest.hasValue() ? "manifests-read" : "manifests-refused");
            if (manifest.hasValue())
            {
                AN_FUZZ_CHECK(manifest->framePaths.size() <= an::kMaxImageListManifestFrames,
                              "a manifest lists no more frames than the bound allows");
                for (const std::string& frame : manifest->framePaths)
                {
                    AN_FUZZ_CHECK(!frame.empty(), "a listed frame has a path");
                    AN_FUZZ_CHECK(frame.find('\0') == std::string::npos, "a listed frame's path holds no NUL");
                }
            }
            else
            {
                AN_FUZZ_CHECK(!manifest.status().ok(), "a refused manifest names a reason");
            }
        }
    }
    else
    {
        // The video path, behind one value of the mode byte because it is the most expensive refusal
        // in this target: FFmpeg reads a container to index it before it will say anything about it.
        const std::string path = writeCandidate(".mkv", bytes);
        if (!path.empty())
        {
            an::FfmpegSourceOptions options;
            an::Result<an::FrameSource> source = an::createFfmpegSource(engine(), *execution, path, options);
            an::fuzz::countReached(source.hasValue() ? "video-opened" : "video-refused");
            if (source.hasValue())
            {
                readToEnd(*source, *execution);
                AN_FUZZ_CHECK(source->close().ok(), "an open source closes");
            }
            else
            {
                AN_FUZZ_CHECK(!source.status().ok(), "a refused source names a reason");
            }
        }
    }
    return 0;
}
