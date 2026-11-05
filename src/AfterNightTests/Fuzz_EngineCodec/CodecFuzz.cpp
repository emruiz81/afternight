// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What must hold when a file the engine did not write is offered to a decoder.
//
// Every other target in this campaign reads a structure the engine itself built. This one reads a
// stranger's bytes, which is the only input a user can hand the engine without typing it, and the
// one that arrives from a network, a card reader or a shared drive. Seven containers are behind the
// registry - FITS, XISF, camera raw, PNG, JPEG, TIFF and the developer dump - and four of them are
// decoded by an external library, so this target is also the campaign's cover of those boundaries.
//
// The claims are deliberately weak about content and strong about behaviour, because a mutated
// container has no correct decoding:
//
// * **A decode ends.** It returns a frame or a typed reason; it does not crash, does not read past
//   what it was given, and does not run past the watchdog's bound.
// * **A probe and a decode agree.** A decode that succeeded where the probe refused, or that produced
//   a frame whose description is not the one the probe stated, would make the probe useless - a
//   caller allocates against what the probe said.
// * **A frame that exists is consistent with itself.** Its layout holds its extent, its element size
//   is its sample format's width, and its buffer is at least its layout's total. A decoder that
//   returned a short buffer would hand every later pass an out-of-bounds read.
// * **A region is inside the image.** The region entry point is asked for rectangles that are inside
//   and rectangles that are not, and it must refuse the second rather than clip.
//
// The path's extension is fuzzed separately from the requested format, so the target reaches both the
// resolution-by-extension path and the explicit-format path, including the case where they disagree.

#include "../FuzzEngine/FuzzHarness.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

/// Every container the registry knows, and the extension each is recognised by.
///
/// The dump format reserves no extension of its own, so it is reached by naming the format rather
/// than by spelling a path - which is the case the registry's own rule is about.
struct FormatEntry
{
    an::CodecFormat format;
    const char* extension;
};

/// How many bytes of an input are the selections rather than the container.
///
/// One for the extension the file is written under, one for the format the read asks for, one for
/// whether a region is read, and four for the rectangle. A seed carries this prefix so that the
/// container it holds reaches the reader it belongs to.
constexpr std::size_t kPrefixBytes = 7u;

constexpr FormatEntry kFormats[] = {
    {an::CodecFormat::Fits, ".fits"},   {an::CodecFormat::Xisf, ".xisf"}, {an::CodecFormat::Png, ".png"},
    {an::CodecFormat::Jpeg, ".jpg"},    {an::CodecFormat::Tiff, ".tif"},  {an::CodecFormat::Raw, ".cr2"},
    {an::CodecFormat::Dump, ".andump"},
};

/// How many bytes this target's engine may hold at once.
///
/// Stated rather than left at the default, which refuses nothing by design: until a host names a
/// number the physical allocator is the engine's backstop. That makes a container claiming a
/// billion-pixel image a question about the machine rather than about the engine - the request
/// reaches `calloc`, and what answers is a null on one host, a killed process on another and an
/// abort from the sanitizer runtime in an instrumented build. With a budget the same input is
/// refused by the limit, carrying what it asked for, what was left and what the limit is, which is
/// the refusal this target exists to exercise. The number is far above anything the seed corpus
/// decodes to and far below any machine this runs on.
constexpr std::uint64_t kMemoryBudgetBytes = 256ull * 1024ull * 1024ull;

/// The engine every input decodes in, with every adapter this build has.
///
/// One context for the process: what this target varies is the file, and building a registry per
/// input would measure the registry rather than the decoders.
///
/// @return the context, valid for the life of the process.
[[nodiscard]] const an::EngineContext& engine()
{
    static const an::EngineContext context = []() {
        std::vector<std::shared_ptr<const an::ImageCodec>> adapters;
        const auto add = [&adapters](an::Result<std::shared_ptr<const an::ImageCodec>> made) {
            if (made.hasValue())
            {
                adapters.push_back(std::move(*made));
            }
        };
        add(an::createFitsCodec());
        add(an::createXisfCodec());
        add(an::createPngCodec());
        add(an::createJpegCodec());
        add(an::createTiffCodec());
        add(an::createRawCodec());
        add(an::createDumpCodec());
        AN_FUZZ_CHECK(!adapters.empty(), "this build must have at least one codec to fuzz");

        an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::CodecRegistry::create(adapters);
        AN_FUZZ_CHECK(registry.hasValue(), "the registry of every codec must build");

        an::EngineContextOptions options;
        options.maxConcurrency = 2u;
        options.limits.memoryBudgetBytes = kMemoryBudgetBytes;
        options.codecRegistry = *registry;
        an::Result<an::EngineContext> created = an::EngineContext::create(options);
        AN_FUZZ_CHECK(created.hasValue(), "the engine context this target decodes every input in must exist");
        return std::move(*created);
    }();
    return context;
}

/// The directory this target writes candidate files into, made once and swept at exit.
///
/// Test-owned storage rather than anywhere near the production tree, which is the rule every suite in
/// this repository follows; `AFTERNIGHT_TEST_STORAGE_ROOT` is how a runner names one.
///
/// @return the directory, which exists.
[[nodiscard]] const std::filesystem::path& scratchDirectory()
{
    static const std::filesystem::path directory = []() {
        const std::string root = an::fuzz::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
        const std::filesystem::path base =
            root.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(root);
        const std::filesystem::path made = base / "Fuzz_EngineCodec";
        std::error_code ignored;
        std::filesystem::create_directories(made, ignored);
        return made;
    }();
    return directory;
}

/// Writes the input where a decoder can be pointed at it.
///
/// One path per extension rather than one per input, because a fuzzing run that left a file behind
/// per iteration would fill a disk long before it found anything, and because a decoder that opened
/// the path twice must see the same bytes both times.
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

/// Everything a decoded frame must be able to say about itself.
///
/// @param frame what came back.
void checkFrame(const an::RawFrame& frame)
{
    AN_FUZZ_CHECK(frame.valid(), "a decode that succeeded produces a frame that holds bytes");
    const std::optional<an::RawFrameDescriptor> descriptor = frame.descriptor();
    const std::optional<an::ImageLayout> layout = frame.layout();
    AN_FUZZ_CHECK(descriptor.has_value() && layout.has_value(), "a valid frame has a description and a layout");
    AN_FUZZ_CHECK(layout->extent() == descriptor->extent(), "a frame's layout describes the frame's own extent");
    AN_FUZZ_CHECK(layout->elementSizeBytes() == descriptor->sampleFormat().sizeBytes,
                  "a frame's element size is its sample format's width");
    AN_FUZZ_CHECK(frame.bytes().size() >= layout->totalBytes(),
                  "a frame's buffer holds at least what its layout addresses");
}

/// The same request asking a probe for the header and metadata only.
///
/// A probe that validates the payload is stricter than a decode on purpose: it verifies the
/// checksums and that the whole data unit is in the file, where a decode reads the samples it needs
/// and verifies nothing it does not read. So what a decode is held to agree with is the probe of
/// the header, and the validating probe is held to the other direction.
///
/// @param request what to read.
/// @return the request without payload validation.
[[nodiscard]] an::CodecReadRequest headerOnly(an::CodecReadRequest request)
{
    request.validatePayload = false;
    return request;
}

/// A probe and a decode of the same request must agree about what is in the file, and a payload a
/// probe validated must decode.
///
/// @param execution the operation both belong to.
/// @param request what to read.
void checkProbeAndDecode(const an::ExecutionContext& execution, const an::CodecReadRequest& request)
{
    const an::Result<an::CodecImageInfo> probed = an::probeImage(engine(), execution, headerOnly(request));
    const an::Result<an::CodecImageInfo> validated = an::probeImage(engine(), execution, request);
    const an::Result<an::CodecDecodeResult> decoded = an::decodeImage(engine(), execution, request);

    an::fuzz::countReached(probed.hasValue() ? "probed" : "probe-refused");
    if (validated.hasValue())
    {
        AN_FUZZ_CHECK(probed.hasValue(), "a probe that validated the payload read a header the plain probe refused");
        an::fuzz::countReached(validated->disposition == an::CodecProbeDisposition::PayloadValidatedWithoutDecode
                                   ? "payload-validated"
                                   : "payload-left-to-decode");
        // The budget is the one refusal a validated payload may still meet: validation allocates no
        // pixel storage, and a decode has to hold the image.
        AN_FUZZ_CHECK(validated->disposition != an::CodecProbeDisposition::PayloadValidatedWithoutDecode ||
                          decoded.hasValue() || decoded.code() == an::ErrorCode::MemoryBudgetExceeded,
                      "a payload a probe validated must decode");
    }
    // A refusal by the memory budget is counted apart from every other refusal, because it is the
    // one this target's stated budget exists to produce: a geometry the file declares and the engine
    // will not hold. Counted with the rest it would be indistinguishable from a header this decoder
    // rejected outright, and a budget quietly back at its default would print the same clean line.
    if (decoded.hasValue())
    {
        an::fuzz::countReached("decoded");
    }
    else
    {
        an::fuzz::countReached(decoded.code() == an::ErrorCode::MemoryBudgetExceeded ? "decode-over-budget"
                                                                                     : "decode-refused");
    }
    if (decoded.hasValue())
    {
        AN_FUZZ_CHECK(probed.hasValue(), "a file that decodes must have probed");
        checkFrame(decoded->frame);
        AN_FUZZ_CHECK(decoded->frame.descriptor()->extent() == probed->descriptor.extent(),
                      "a decode produces the image the probe said was there");
        AN_FUZZ_CHECK(decoded->frame.layout()->extent() == probed->decodedLayout.extent() &&
                          decoded->frame.layout()->rowStrideBytes() == probed->decodedLayout.rowStrideBytes() &&
                          decoded->frame.layout()->planeStrideBytes() == probed->decodedLayout.planeStrideBytes() &&
                          decoded->frame.layout()->planeCount() == probed->decodedLayout.planeCount(),
                      "a decode produces the layout the probe stated");
    }
    if (!probed.hasValue())
    {
        AN_FUZZ_CHECK(!probed.status().ok(), "a refused probe names a reason");
        return;
    }
    AN_FUZZ_CHECK(probed->imageCount >= 1u, "a container that probed holds at least one image");
}

/// The region entry point must refuse a rectangle that is not inside the image.
///
/// @param execution the operation this belongs to.
/// @param request what to read.
/// @param region the rectangle asked for.
void checkRegion(const an::ExecutionContext& execution, const an::CodecReadRequest& request,
                 const an::CodecRegion& region)
{
    const an::Result<an::CodecImageInfo> probed = an::probeImage(engine(), execution, headerOnly(request));
    const an::Result<an::CodecDecodeResult> decoded = an::decodeImageRegion(engine(), execution, request, region);
    if (!decoded.hasValue())
    {
        AN_FUZZ_CHECK(!decoded.status().ok(), "a refused region read names a reason");
        // Counted by reason, because "no region was ever read" and "no codec in this build reads
        // regions" print the same zero otherwise, and only one of them is about the inputs.
        an::fuzz::countReached(decoded.code() == an::ErrorCode::Unsupported ? "regions-unsupported"
                                                                            : "regions-refused");
        return;
    }

    AN_FUZZ_CHECK(probed.hasValue(), "a file a region was read from must have probed");
    an::fuzz::countReached("regions-read");
    checkFrame(decoded->frame);
    const an::ImageExtent whole = probed->descriptor.extent();
    AN_FUZZ_CHECK(region.width != 0u && region.height != 0u, "an empty rectangle is never read");
    AN_FUZZ_CHECK(static_cast<std::uint64_t>(region.x) + region.width <= whole.width() &&
                      static_cast<std::uint64_t>(region.y) + region.height <= whole.height(),
                  "a region that was read has to have been inside the image");
    AN_FUZZ_CHECK(decoded->frame.descriptor()->extent().width() == region.width &&
                      decoded->frame.descriptor()->extent().height() == region.height,
                  "a region read produces the rectangle that was asked for");
    AN_FUZZ_CHECK(decoded->frame.descriptor()->extent().channelCount() == whole.channelCount(),
                  "a region read keeps every channel of the image");
}

} // namespace

namespace an::fuzz
{

TargetInfo targetInfo()
{
    // Big enough for a small real container and for a length field that claims far more than the file
    // holds, which is the shape of most decoder defects.
    return {.name = "Fuzz_EngineCodec", .maxInputBytes = 262144u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    std::vector<std::vector<std::uint8_t>> corpus;

    /// Gives a container the prefix that offers it to its own reader.
    ///
    /// @param format which of `kFormats` the bytes are, by index.
    /// @param bytes the container.
    /// @return the seed.
    const auto seeded = [](std::size_t format, const std::vector<std::uint8_t>& bytes) {
        std::vector<std::uint8_t> seed;
        seed.reserve(kPrefixBytes + bytes.size());
        seed.push_back(static_cast<std::uint8_t>(format)); // The extension it is written under.
        seed.push_back(static_cast<std::uint8_t>(format)); // The format the read asks for.
        seed.push_back(0u); // Read a region as well, which two of the seven formats support.
        seed.push_back(0u); // The rectangle: the two-by-two at the origin, inside every seed image.
        seed.push_back(0u);
        seed.push_back(1u);
        seed.push_back(1u);
        seed.insert(seed.end(), bytes.begin(), bytes.end());
        return seed;
    };

    // A real container per format, produced by this engine's own encoder. Encoding rather than
    // committing binaries keeps the corpus honest - it is what this build writes - and keeps the
    // repository free of sample files whose licence would have to be tracked. A format whose encoder
    // refuses this frame contributes no seed, which is a property of the build rather than a failure.
    const std::optional<ImageExtent> extent = ImageExtent::create(4u, 3u, 1u, engine().limits());
    if (extent.has_value())
    {
        std::vector<std::byte> samples(
            static_cast<std::size_t>(extent->width()) * extent->height() * sizeof(std::uint16_t), std::byte{0});
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            samples[index] = static_cast<std::byte>(index * 37u);
        }

        const RawSampleFormat format{sizeof(std::uint16_t), RawSampleClass::UnsignedInteger};
        const std::optional<RawFrameDescriptor> descriptor =
            RawFrameDescriptor::create({.extent = *extent,
                                        .sampleFormat = format,
                                        .byteOrder = ByteOrder::Native,
                                        .channelSchema = ChannelSchema::Mono,
                                        .colorSpace = ColorSpace::Unspecified,
                                        .numericDomain = NumericDomain::RawSensorCode,
                                        .cfa = {},
                                        .coordinateOrigin = CoordinateOrigin::TopLeft,
                                        .pixelAspectRatio = 1.0});
        const std::optional<ImageLayout> layout =
            ImageLayout::create({.extent = *extent,
                                 .elementSizeBytes = static_cast<std::uint32_t>(sizeof(std::uint16_t)),
                                 .samplesPerPixel = 1u,
                                 .planeCount = 1u,
                                 .rowAlignmentBytes = 1u},
                                engine().limits());

        Result<ExecutionContext> execution = ExecutionContext::create(engine(), {});
        if (descriptor.has_value() && layout.has_value() && execution.hasValue())
        {
            for (const FormatEntry& entry : kFormats)
            {
                Result<SampleBuffer> buffer = SampleBuffer::borrow(std::span<std::byte>(samples));
                if (!buffer.hasValue())
                {
                    continue;
                }
                Result<RawFrame> frame = RawFrame::create(*descriptor, *layout, std::move(*buffer));
                if (!frame.hasValue())
                {
                    continue;
                }
                const std::filesystem::path path =
                    scratchDirectory() / (std::string("seed") + entry.extension + ".pending");
                const std::string spelled = path.string();
                const CodecEncodeRequest request{.pendingPath = spelled, .format = entry.format, .intent = nullptr};
                const ImageMetadata metadata;
                if (!encodeImage(engine(), *execution, request, *frame, metadata).hasValue())
                {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                    continue;
                }
                std::FILE* file = openFile(spelled.c_str(), "rb");
                if (file != nullptr)
                {
                    std::vector<std::uint8_t> bytes;
                    std::uint8_t chunk[4096];
                    std::size_t read = 0u;
                    while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) != 0u)
                    {
                        bytes.insert(bytes.end(), chunk, chunk + read);
                    }
                    std::fclose(file);
                    if (!bytes.empty())
                    {
                        corpus.push_back(seeded(static_cast<std::size_t>(&entry - std::begin(kFormats)), bytes));
                    }
                }
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
    }

    // Signatures written by hand, so the corpus is never empty and never depends on which encoders
    // this build has. Each is the first bytes of a real container and nothing after them, which is
    // the truncation every reader has to survive.
    static constexpr std::uint8_t kPngSignature[] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    static constexpr std::uint8_t kJpegSignature[] = {0xFFu, 0xD8u, 0xFFu, 0xE0u, 0x00u, 0x10u,
                                                      0x4Au, 0x46u, 0x49u, 0x46u, 0x00u};
    static constexpr std::uint8_t kTiffLittle[] = {0x49u, 0x49u, 0x2Au, 0x00u, 0x08u, 0x00u, 0x00u, 0x00u};
    static constexpr std::uint8_t kTiffBig[] = {0x4Du, 0x4Du, 0x00u, 0x2Au, 0x00u, 0x00u, 0x00u, 0x08u};
    static constexpr std::uint8_t kXisfSignature[] = {0x58u, 0x49u, 0x53u, 0x46u, 0x30u, 0x31u, 0x30u, 0x30u};
    corpus.push_back(seeded(2u, {std::begin(kPngSignature), std::end(kPngSignature)}));
    corpus.push_back(seeded(3u, {std::begin(kJpegSignature), std::end(kJpegSignature)}));
    corpus.push_back(seeded(4u, {std::begin(kTiffLittle), std::end(kTiffLittle)}));
    corpus.push_back(seeded(4u, {std::begin(kTiffBig), std::end(kTiffBig)}));
    corpus.push_back(seeded(1u, {std::begin(kXisfSignature), std::end(kXisfSignature)}));

    // A TIFF directory declaring a geometry whose decoded frame is about 35 GB, found by libFuzzer
    // on the nominated Linux host when this target's context still carried the default budget. It is
    // kept as a seed because it is the shape a refusal by the budget needs - a header that is
    // internally consistent enough to be believed and far larger than anything the engine will hold -
    // and because replaying it every run is what keeps the stated budget above from being dropped.
    static constexpr std::uint8_t kOverBudgetTiff[] = {
        0x04u, 0x04u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x49u, 0x49u, 0x2Au, 0x00u, 0x20u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x25u, 0x4Au, 0x6Fu, 0x94u, 0xB9u, 0xDEu, 0x03u, 0x28u, 0x4Du, 0x72u, 0x97u, 0xBCu, 0xE1u, 0x06u, 0x2Bu, 0x50u,
        0x75u, 0x9Au, 0xBFu, 0xE4u, 0x09u, 0x2Eu, 0x53u, 0x0Du, 0x00u, 0x00u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u, 0x00u,
        0x00u, 0x01u, 0x0Fu, 0x42u, 0x40u, 0x01u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x57u, 0x62u, 0x69u,
        0x4Cu, 0x02u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x00u, 0x00u, 0x03u, 0x01u, 0x03u,
        0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x06u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u, 0x00u,
        0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x11u, 0x01u, 0x04u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x08u, 0x00u, 0x00u,
        0x00u, 0x12u, 0x01u, 0x03u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x15u, 0x01u, 0x01u,
        0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0xB9u, 0x01u, 0x00u, 0x20u, 0x00u, 0x00u, 0x16u, 0x01u, 0x03u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x03u, 0x17u, 0x01u, 0x04u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x18u, 0x00u, 0x4Eu, 0x37u,
        0x4Cu, 0x69u, 0x62u, 0x58u, 0x49u, 0x53u, 0x46u, 0x35u, 0x45u, 0x72u, 0x72u, 0x6Fu, 0x72u, 0x45u, 0x53u, 0x01u,
        0x03u, 0x00u, 0x01u, 0x3Fu, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x30u, 0x2Eu,
        0x39u, 0x31u, 0xD0u, 0x30u, 0x00u};
    corpus.push_back({std::begin(kOverBudgetTiff), std::end(kOverBudgetTiff)});

    // A FITS primary header of one 2880-byte block, which is the one container whose text form a
    // single-byte mutation can turn into a different claim about the image.
    std::vector<std::uint8_t> fits(2880u, static_cast<std::uint8_t>(' '));
    const auto card = [&fits](std::size_t index, const char* text) {
        for (std::size_t offset = 0u; text[offset] != '\0' && offset < 80u; ++offset)
        {
            fits[index * 80u + offset] = static_cast<std::uint8_t>(text[offset]);
        }
    };
    card(0u, "SIMPLE  =                    T");
    card(1u, "BITPIX  =                   16");
    card(2u, "NAXIS   =                    2");
    card(3u, "NAXIS1  =                    4");
    card(4u, "NAXIS2  =                    3");
    card(5u, "BZERO   =                32768");
    card(6u, "END");
    corpus.push_back(seeded(0u, fits));

    return corpus;
}

} // namespace an::fuzz

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);

    // The selections are the input's first bytes and the container is everything after them, which is
    // the arrangement `kPrefixBytes` describes: a seed then reaches the decoder of the format it
    // actually is, and a mutation of the prefix is what offers it to every other one.
    const FormatEntry& named = kFormats[input.takeBelow(static_cast<std::uint32_t>(std::size(kFormats)))];
    const std::uint32_t formatChoice = input.takeBelow(static_cast<std::uint32_t>(std::size(kFormats)) + 1u);
    const an::CodecFormat requested =
        formatChoice == std::size(kFormats) ? an::CodecFormat::Unknown : kFormats[formatChoice].format;
    const bool wantRegion = input.takeBelow(2u) == 0u;
    // Small rectangles, because the interesting answer is a region the decoder reads rather than the
    // refusal every rectangle wider than the image gets. Bytes taken raw put the origin past the last
    // column of a four-pixel seed almost every time, and the region path was then reached twice in
    // twenty thousand inputs. Four by four over a four-by-three seed still lands outside the image
    // more often than inside it, so the refusal stays covered as well.
    const std::uint32_t regionX = input.takeBelow(4u);
    const std::uint32_t regionY = input.takeBelow(4u);
    const std::uint32_t regionWidth = 1u + input.takeBelow(4u);
    const std::uint32_t regionHeight = 1u + input.takeBelow(4u);

    const std::string path = writeCandidate(named.extension, input.rest());
    if (path.empty())
    {
        return 0;
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine(), {});
    if (!execution.hasValue())
    {
        return 0;
    }

    const an::CodecReadRequest request{
        .path = path, .format = requested, .imageIndex = 0u, .readMetadata = true, .validatePayload = true};
    checkProbeAndDecode(*execution, request);
    if (wantRegion)
    {
        checkRegion(*execution, request,
                    an::CodecRegion{.x = regionX, .y = regionY, .width = regionWidth, .height = regionHeight});
    }
    return 0;
}
