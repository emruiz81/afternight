// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Sequence compositions a caller needs - opening by name, extracting, cloning, combining, demosaicing a frame and
// retiming - stated as the sources, sinks and operations a caller composes them from. Each helper below is the
// whole of what a caller writes, and each case pins the behaviour the composition has to have.

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/IO/FfmpegSink.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/IO/SerSequence.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "VideoFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Pixels = std::vector<std::uint8_t>;

/// A frame's samples, row after row, for the one-channel eight-bit frames these cases use.
Pixels pixelsOf(const an::RawFrame& frame)
{
    const std::optional<an::ImageLayout> described = frame.layout();
    if (!described)
        return {};
    const an::ImageLayout& layout = *described;
    const std::uint32_t width = layout.extent().width();
    Pixels pixels;
    pixels.reserve(std::size_t{width} * layout.extent().height() * layout.extent().channelCount());
    for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
    {
        const auto row = *frame.row(0u, y);
        for (std::size_t x = 0u; x < std::size_t{width} * layout.samplesPerPixel(); ++x)
            pixels.push_back(static_cast<std::uint8_t>(row[x]));
    }
    return pixels;
}

class SequenceCompositionTest : public testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        an::test::quietFfmpegLog();
    }

    void SetUp() override
    {
        directory =
            std::filesystem::temp_directory_path() /
            ("an-sequence-composition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        const std::array codecs{*an::createFitsCodec()};
        an::EngineContextOptions options;
        options.maxConcurrency = 4u;
        options.codecRegistry = *an::CodecRegistry::create(codecs);
        engine = std::move(*an::EngineContext::create(options));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 2u}));
    }

    void TearDown() override
    {
        execution = {};
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(engine.shutdown().ok());
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(directory, error);
        ASSERT_FALSE(error);
        ASSERT_EQ(root.parent_path(), std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()));
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
    }

    static std::string utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    std::string pathOf(std::string_view name) const
    {
        return utf8(directory / std::filesystem::path(
                                    std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size())));
    }

    an::RawFrameDescriptor describe(std::uint32_t width, std::uint32_t height, bool mosaiced = false) const
    {
        const auto extent = *an::ImageExtent::create(width, height, 1u, engine.limits());
        return *an::RawFrameDescriptor::create(
            {extent,
             {1u, an::RawSampleClass::UnsignedInteger},
             an::ByteOrder::Native,
             an::ChannelSchema::Mono,
             an::ColorSpace::Unspecified,
             an::NumericDomain::RawSensorCode,
             mosaiced ? *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u) : an::CfaDescriptor{},
             an::CoordinateOrigin::TopLeft,
             1.0});
    }

    /// A frame whose sample at column x and row y of frame `index` is `value(x, y, index)`.
    an::RawFrame frameOf(const an::RawFrameDescriptor& descriptor, std::uint64_t index,
                         const std::function<int(std::uint32_t, std::uint32_t, std::uint64_t)>& value)
    {
        const an::ImageExtent& extent = descriptor.extent();
        const auto layout = *an::ImageLayout::create({extent, 1u, 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Uninitialized);
        auto frame = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
        {
            auto row = *frame->mutableRow(0u, y);
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
                row[x] = static_cast<std::byte>(value(x, y, index) & 0xff);
        }
        return std::move(*frame);
    }

    /// Writes frames as a SER recording, each with the presentation ticks given, and returns its path.
    std::string writeSer(std::string_view name, const an::RawFrameDescriptor& descriptor, std::uint64_t frames,
                         const std::function<int(std::uint32_t, std::uint32_t, std::uint64_t)>& value,
                         std::optional<std::uint64_t> origin = std::nullopt, std::uint64_t interval = 400000u)
    {
        const std::string path = pathOf(name);
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = path});
        if (!output.hasValue())
        {
            ADD_FAILURE() << output.status().message();
            return path;
        }
        an::SequenceDescriptor sequence{.format = an::SequenceFormat::Ser, .fixedFrame = descriptor};
        an::SerSinkRequest request;
        if (origin)
        {
            sequence.timeBase = an::SequenceRate{1u, static_cast<std::uint32_t>(an::kSerTicksPerSecond)};
            sequence.timestampsRequired = true;
            request.timestamps = an::SerTimestampSource::PresentationTicks;
            request.timelineOriginTicks = *origin;
        }
        auto sink = an::createSerSink(engine, execution, *output, sequence, request);
        if (!sink.hasValue())
        {
            ADD_FAILURE() << sink.status().message();
            return path;
        }
        for (std::uint64_t index = 0u; index < frames; ++index)
        {
            an::SequencePosition position{index, {}, {}};
            if (origin)
                position.presentationTicks = static_cast<std::int64_t>(index * interval);
            EXPECT_TRUE(sink->append(engine, execution, {frameOf(descriptor, index, value), {}, position}).hasValue());
        }
        auto finished = sink->finish(execution);
        if (!finished.hasValue())
        {
            ADD_FAILURE() << finished.status().message();
            return path;
        }
        EXPECT_TRUE(output->commit(execution, *finished).hasValue());
        EXPECT_TRUE(sink->close().ok());
        return path;
    }

    // ----- The compositions -----

    /// Opening by name: the container the name claims, opened by the source that reads it.
    an::Result<an::FrameSource> openByName(std::string_view path)
    {
        const std::optional<an::SequenceFormat> format = an::sequenceFormatForPath(path);
        if (!format)
            return an::Status::failure(an::ErrorCode::Unsupported);
        if (*format == an::SequenceFormat::Ser)
            return an::createSerSource(engine, execution, path, {});
        if (an::readsThroughFfmpeg(*format))
            return an::createFfmpegSource(engine, execution, path, {});
        auto manifest = an::readImageListManifest(engine, execution, path);
        if (!manifest.hasValue())
            return manifest.status();
        std::vector<std::string> resolved;
        for (const std::string& framePath : manifest->framePaths)
        {
            auto one = an::resolveImageListFramePath(path, framePath);
            if (!one.hasValue())
                return one.status();
            resolved.push_back(std::move(*one));
        }
        std::vector<an::CodecReadRequest> requests;
        for (const std::string& one : resolved)
            requests.push_back({one, an::codecFormatFromPath(one)});
        return an::createImageListSource(engine, execution, requests, {});
    }

    /// An inherited list: a new list stating the source's frame and rate.
    static an::SequenceDescriptor inheritedList(const an::SequenceDescriptor& source)
    {
        return {.format = an::SequenceFormat::ImageList,
                .fixedFrame = source.fixedFrame,
                .nominalFrameRate = source.nominalFrameRate};
    }

    using Stage = std::function<an::SequenceFrame(an::SequenceFrame)>;

    /// `sequence::process`: every frame through the first stage and then the second into a list, published only
    /// when every frame was written.
    an::Status processInto(an::FrameSource& source, std::string_view destination, const Stage& first,
                           const Stage& second, const an::ExecutionContext& operation)
    {
        auto output = an::SequenceOutput::begin(engine, operation, {.destinationPath = destination});
        if (!output.hasValue())
            return output.status();
        auto sink = an::createImageListSink(engine, operation, *output, inheritedList(*source.descriptor()), {});
        if (!sink.hasValue())
            return sink.status();
        for (;;)
        {
            auto read = source.read(engine, operation);
            if (!read.hasValue())
                return read.status();
            if (!read->frame)
                break;
            an::SequenceFrame frame = std::move(*read->frame);
            if (first)
                frame = first(std::move(frame));
            if (second)
                frame = second(std::move(frame));
            if (auto appended = sink->append(engine, operation, frame); !appended.hasValue())
                return appended.status();
        }
        auto finished = sink->finish(operation);
        if (!finished.hasValue())
            return finished.status();
        auto committed = output->commit(operation, *finished);
        return committed.hasValue() ? an::Status::success() : committed.status();
    }

    /// A frame each sample of which is `change(sample)`, with the source frame's meaning and position.
    an::SequenceFrame mapped(const an::SequenceFrame& frame, const std::function<int(int)>& change)
    {
        const an::RawFrameDescriptor descriptor = *frame.frame.descriptor();
        const Pixels pixels = pixelsOf(frame.frame);
        const std::uint32_t width = descriptor.extent().width();
        return {frameOf(descriptor, frame.position.index,
                        [&](std::uint32_t x, std::uint32_t y, std::uint64_t) {
                            return change(pixels[std::size_t{y} * width + x]);
                        }),
                frame.metadata, frame.position};
    }

    /// The frames a published list holds, in order.
    std::vector<Pixels> listFrames(std::string_view manifest)
    {
        std::vector<Pixels> frames;
        auto source = openByName(manifest);
        EXPECT_TRUE(source.hasValue()) << source.status().message();
        if (!source.hasValue())
            return frames;
        for (;;)
        {
            auto read = source->read(engine, execution);
            EXPECT_TRUE(read.hasValue()) << read.status().message();
            if (!read.hasValue() || !read->frame)
                break;
            frames.push_back(pixelsOf(read->frame->frame));
        }
        return frames;
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

int gradient(std::uint32_t x, std::uint32_t y, std::uint64_t index)
{
    return static_cast<int>(x * 16u + y * 4u + index * 3u);
}

TEST_F(SequenceCompositionTest, ASequenceOpensThroughTheContainerItsNameClaims)
{
    const an::RawFrameDescriptor frame = describe(8u, 6u);
    const std::string ser = writeSer("night.SER", frame, 3u, gradient);
    auto recording = openByName(ser);
    ASSERT_TRUE(recording.hasValue()) << recording.status().message();
    EXPECT_EQ(recording->descriptor()->format, an::SequenceFormat::Ser);
    EXPECT_EQ(recording->descriptor()->frameCount, 3u);

    ASSERT_TRUE(processInto(*recording, pathOf("list.anseq"), {}, {}, execution).ok());
    auto list = openByName(pathOf("list.anseq"));
    ASSERT_TRUE(list.hasValue()) << list.status().message();
    EXPECT_EQ(list->descriptor()->format, an::SequenceFormat::ImageList);
    EXPECT_EQ(list->descriptor()->frameCount, 3u);

    const an::test::VideoFixtureSpec spec{.width = 8, .height = 6, .frames = 2};
    if (an::test::canWriteVideo(spec))
    {
        const std::string video = pathOf("clip.mkv");
        ASSERT_TRUE(an::test::writeVideo(directory / "clip.mkv", spec).empty());
        auto opened = openByName(video);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        EXPECT_EQ(opened->descriptor()->format, an::SequenceFormat::Video);
    }
    EXPECT_EQ(openByName(pathOf("notes.txt")).status().code(), an::ErrorCode::Unsupported);
}

TEST_F(SequenceCompositionTest, ProcessingWritesEveryFrameThroughBothStagesIntoAListThatInheritsTheFrame)
{
    const an::RawFrameDescriptor frame = describe(8u, 4u);
    auto source = openByName(writeSer("in.ser", frame, 4u, gradient));
    ASSERT_TRUE(source.hasValue());
    const std::string destination = pathOf("processed.anseq");
    ASSERT_TRUE(
        processInto(
            *source, destination,
            [&](an::SequenceFrame one) { return mapped(one, [](int sample) { return 255 - sample; }); },
            [&](an::SequenceFrame one) { return mapped(one, [](int sample) { return (std::min)(sample + 1, 255); }); },
            execution)
            .ok());
    const std::vector<Pixels> written = listFrames(destination);
    ASSERT_EQ(written.size(), 4u);
    for (std::uint64_t index = 0u; index < 4u; ++index)
        for (std::uint32_t y = 0u; y < 4u; ++y)
            for (std::uint32_t x = 0u; x < 8u; ++x)
                ASSERT_EQ(written[index][y * 8u + x], (std::min)(255 - (gradient(x, y, index) & 0xff) + 1, 255))
                    << index << ":" << x << "," << y;
    auto manifest = an::readImageListManifest(engine, execution, destination);
    ASSERT_TRUE(manifest.hasValue());
    EXPECT_EQ(manifest->width, 8u);
    EXPECT_EQ(manifest->height, 4u);
}

TEST_F(SequenceCompositionTest, AStoppedProcessPublishesNoListAndLeavesNoFrame)
{
    const an::RawFrameDescriptor frame = describe(8u, 4u);
    auto source = openByName(writeSer("stopped.ser", frame, 5u, gradient));
    ASSERT_TRUE(source.hasValue());
    std::stop_source stop;
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stop.get_token();
    auto stoppable = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(stoppable.hasValue());
    std::uint64_t processed = 0u;
    const an::Status stopped = processInto(
        *source, pathOf("stopped.anseq"),
        [&](an::SequenceFrame one) {
            if (++processed == 2u)
                stop.request_stop();
            return one;
        },
        {}, *stoppable);
    EXPECT_EQ(stopped.code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(directory / "stopped.anseq"));
    EXPECT_FALSE(std::filesystem::exists(directory / "stopped-frames"));
}

TEST_F(SequenceCompositionTest, ExtractionClampsItsRangeToTheSourceAndKeepsWhatTheFilterAccepts)
{
    const an::RawFrameDescriptor frame = describe(4u, 2u);
    const std::string ser = writeSer("extract.ser", frame, 6u, gradient);
    // Extraction: an end past the last frame is the last frame, and a start past the end is the end.
    const auto extract = [&](std::uint64_t start, std::uint64_t end, std::string_view name,
                             const std::function<bool(std::uint64_t)>& keep) -> std::vector<Pixels> {
        auto source = openByName(ser);
        if (!source.hasValue())
        {
            ADD_FAILURE() << source.status().message();
            return {};
        }
        const std::uint64_t count = *source->descriptor()->frameCount;
        end = (std::min)(end, count - 1u);
        start = (std::min)(start, end);
        EXPECT_TRUE(source->seek(execution, {.index = start}).hasValue());
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf(name)});
        if (!output.hasValue())
        {
            ADD_FAILURE() << output.status().message();
            return {};
        }
        auto sink = an::createImageListSink(engine, execution, *output, inheritedList(*source->descriptor()), {});
        if (!sink.hasValue())
        {
            ADD_FAILURE() << sink.status().message();
            return {};
        }
        std::uint64_t written = 0u;
        for (std::uint64_t index = start; index <= end; ++index)
        {
            auto read = source->read(engine, execution);
            if (!read.hasValue() || !read->frame)
            {
                ADD_FAILURE() << (read.hasValue() ? std::string("end of the sequence") : read.status().message());
                return {};
            }
            if (keep(index))
            {
                // A kept frame takes the next position of the list it is written into.
                auto appended = sink->append(
                    engine, execution, {std::move(read->frame->frame), read->frame->metadata, {written++, {}, {}}});
                EXPECT_TRUE(appended.hasValue()) << appended.status().message();
            }
        }
        auto finished = sink->finish(execution);
        if (!finished.hasValue())
        {
            ADD_FAILURE() << finished.status().message();
            return {};
        }
        EXPECT_TRUE(output->commit(execution, *finished).hasValue());
        return listFrames(pathOf(name));
    };
    const std::vector<Pixels> even =
        extract(2u, 10u, "even.anseq", [](std::uint64_t index) { return index % 2u == 0u; });
    ASSERT_EQ(even.size(), 2u);
    EXPECT_EQ(even[0][1], gradient(1u, 0u, 2u));
    EXPECT_EQ(even[1][1], gradient(1u, 0u, 4u));
    const std::vector<Pixels> reversed = extract(9u, 3u, "one.anseq", [](std::uint64_t) { return true; });
    ASSERT_EQ(reversed.size(), 1u);
    EXPECT_EQ(reversed[0][1], gradient(1u, 0u, 3u));
}

TEST_F(SequenceCompositionTest, CloningStretchesFitsFillsAndCentresFramesInTheTargetFrame)
{
    const an::RawFrameDescriptor source = describe(8u, 4u);
    const an::RawFrame picture = frameOf(source, 0u, gradient);
    const Pixels original = pixelsOf(picture);
    constexpr std::uint8_t kBlack = 0u;

    // Resizes with the Lanczos kernel, for every mode.
    const auto resized = [&](const an::RawFrame& from, std::uint32_t width, std::uint32_t height) -> an::RawFrame {
        const an::ImageDescriptor described = *from.descriptor()->toImageDescriptor();
        auto plan = an::planResize(described, {.width = width,
                                               .height = height,
                                               .sampling = {.interpolation = an::KernelInterpolation::Lanczos4,
                                                            .border = an::KernelBorderMode::ReflectExcludingEdge,
                                                            .invalid = an::SamplingInvalidPolicy::RequireAll}});
        if (!plan.hasValue())
        {
            ADD_FAILURE() << plan.status().message();
            return {};
        }
        const an::RawFrameDescriptor target = describe(width, height);
        const auto layout = *an::ImageLayout::create({target.extent(), 1u, 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        auto frame = an::RawFrame::create(target, layout, std::move(*buffer));
        auto mask = an::ValidityMask::create(engine, target.extent());
        if (!mask.hasValue())
        {
            ADD_FAILURE() << mask.status().message();
            return {};
        }
        auto transformed =
            an::transformImageGeometry(execution, an::SampleType::UInt8, from.bytes().data(), *from.layout(),
                                       frame->mutableBytes()->data(), layout, *plan, {}, *mask->view());
        EXPECT_TRUE(transformed.hasValue()) << transformed.status().message();
        return std::move(*frame);
    };
    // Copies the part of `from` that falls on `canvas` at an offset, as `image_operations::paste` did.
    const auto pasted = [&](const Pixels& from, std::uint32_t fromWidth, std::uint32_t fromHeight, std::uint32_t width,
                            std::uint32_t height, std::uint32_t offsetX, std::uint32_t offsetY) {
        Pixels canvas(std::size_t{width} * height, kBlack);
        if (from.size() != std::size_t{fromWidth} * fromHeight)
        {
            ADD_FAILURE() << "a " << fromWidth << "x" << fromHeight << " frame has " << from.size() << " samples";
            return canvas;
        }
        for (std::uint32_t y = 0u; y < fromHeight && offsetY + y < height; ++y)
            for (std::uint32_t x = 0u; x < fromWidth && offsetX + x < width; ++x)
                canvas[std::size_t{offsetY + y} * width + offsetX + x] = from[std::size_t{y} * fromWidth + x];
        return canvas;
    };

    // Stretch: the frame resized to the target, whatever its shape.
    const Pixels stretched = pixelsOf(resized(picture, 4u, 4u));
    EXPECT_EQ(stretched.size(), 16u);

    // Fit: scaled by the smaller ratio, truncated, resized and centred on black - an 8x4 frame in 4x4 is 4x2 at row 1.
    const double fitScale = (std::min)(4.0 / 8.0, 4.0 / 4.0);
    const auto fitWidth = static_cast<std::uint32_t>(8.0 * fitScale);
    const auto fitHeight = static_cast<std::uint32_t>(4.0 * fitScale);
    ASSERT_EQ(fitWidth, 4u);
    ASSERT_EQ(fitHeight, 2u);
    const Pixels fitted = pasted(pixelsOf(resized(picture, fitWidth, fitHeight)), fitWidth, fitHeight, 4u, 4u,
                                 (4u - fitWidth) / 2u, (4u - fitHeight) / 2u);
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        EXPECT_EQ(fitted[x], kBlack);
        EXPECT_EQ(fitted[12u + x], kBlack);
    }

    // Fill: scaled by the larger ratio, resized, and cropped at the centre - the 8x4 frame's middle four columns.
    const double fillScale = (std::max)(4.0 / 8.0, 4.0 / 4.0);
    ASSERT_EQ(fillScale, 1.0);
    const an::RawFrame full = resized(picture, 8u, 4u);
    ASSERT_TRUE(full.descriptor().has_value());
    const an::ImageDescriptor fullDescribed = *full.descriptor()->toImageDescriptor();
    auto crop = an::planCrop(fullDescribed, {.x = 2u, .y = 0u, .width = 4u, .height = 4u});
    ASSERT_TRUE(crop.hasValue()) << crop.status().message();
    Pixels filled(16u, kBlack);
    const Pixels fullPixels = pixelsOf(full);
    ASSERT_EQ(fullPixels.size(), 32u);
    for (std::uint32_t y = 0u; y < 4u; ++y)
        for (std::uint32_t x = 0u; x < 4u; ++x)
            filled[y * 4u + x] = fullPixels[y * 8u + 2u + x];
    for (std::uint32_t y = 0u; y < 4u; ++y)
        for (std::uint32_t x = 0u; x < 4u; ++x)
            EXPECT_EQ(filled[y * 4u + x], original[y * 8u + 2u + x])
                << "a same-size Lanczos resize is the frame itself";

    // Center: the frame unscaled on black, placed at the origin when it is larger than the canvas.
    const Pixels centred = pasted(original, 8u, 4u, 4u, 4u, 0u, 0u);
    for (std::uint32_t y = 0u; y < 4u; ++y)
        for (std::uint32_t x = 0u; x < 4u; ++x)
            EXPECT_EQ(centred[y * 4u + x], original[y * 8u + x]);
    const Pixels smaller = pasted(original, 8u, 4u, 12u, 8u, (12u - 8u) / 2u, (8u - 4u) / 2u);
    EXPECT_EQ(smaller[2u * 12u + 2u], original[0]);
    EXPECT_EQ(smaller[0], kBlack);
}

TEST_F(SequenceCompositionTest, AnMp4TargetIsBoundedByItsCodingThenAligned)
{
    // A clone's MP4 rules: the coding's largest picture, a width truncated to a multiple of 32, and one row dropped
    // from an odd height.
    const auto target = [](an::FfmpegVideoCodec codec, std::uint32_t width, std::uint32_t height) {
        const std::uint32_t maxWidth = codec == an::FfmpegVideoCodec::H264 ? 4096u : 8192u;
        const std::uint32_t maxHeight = codec == an::FfmpegVideoCodec::H264   ? 2304u
                                        : codec == an::FfmpegVideoCodec::H265 ? 4320u
                                                                              : 8192u;
        width = (std::min)(width, maxWidth);
        height = (std::min)(height, maxHeight);
        width = width / 32u * 32u;
        if (height % 2u != 0u)
            --height;
        return std::pair{width, height};
    };
    EXPECT_EQ(target(an::FfmpegVideoCodec::H264, 5000u, 3001u), (std::pair{4096u, 2304u}));
    EXPECT_EQ(target(an::FfmpegVideoCodec::H265, 1000u, 601u), (std::pair{992u, 600u}));
    EXPECT_EQ(target(an::FfmpegVideoCodec::Vp9, 9000u, 9001u), (std::pair{8192u, 8192u}));
}

TEST_F(SequenceCompositionTest, CombiningSequencesNeedsEqualLengthsAndSizesAndWritesEachCombination)
{
    const an::RawFrameDescriptor frame = describe(4u, 2u);
    const std::string first = writeSer("first.ser", frame, 3u, gradient);
    const std::string second =
        writeSer("second.ser", frame, 3u, [](std::uint32_t x, std::uint32_t, std::uint64_t index) {
            return static_cast<int>(100u + x + index);
        });
    const std::string shorter = writeSer("shorter.ser", frame, 2u, gradient);
    // Combining sequences: every input as long and as large as the first, one combined frame per index.
    const auto combine = [&](const std::vector<std::string>& inputs, std::string_view name) -> an::Status {
        std::vector<an::FrameSource> sources;
        for (const std::string& input : inputs)
        {
            auto opened = openByName(input);
            if (!opened.hasValue())
                return opened.status();
            sources.push_back(std::move(*opened));
        }
        const an::SequenceDescriptor& firstDescriptor = *sources.front().descriptor();
        for (const an::FrameSource& source : sources)
            if (source.descriptor()->frameCount != firstDescriptor.frameCount ||
                source.descriptor()->fixedFrame != firstDescriptor.fixedFrame)
                return an::Status::failure(an::ErrorCode::IncompatibleArguments);
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf(name)});
        if (!output.hasValue())
            return output.status();
        auto sink = an::createImageListSink(engine, execution, *output, inheritedList(firstDescriptor), {});
        if (!sink.hasValue())
            return sink.status();
        for (std::uint64_t index = 0u; index < *firstDescriptor.frameCount; ++index)
        {
            std::vector<Pixels> frames;
            for (an::FrameSource& source : sources)
            {
                auto read = source.read(engine, execution);
                if (!read.hasValue())
                    return read.status();
                if (!read->frame)
                    return an::Status::failure(an::ErrorCode::InvalidState);
                frames.push_back(pixelsOf(read->frame->frame));
            }
            an::SequenceFrame combined{frameOf(frame, index,
                                               [&](std::uint32_t x, std::uint32_t y, std::uint64_t) {
                                                   int sum = 0;
                                                   for (const Pixels& one : frames)
                                                       sum += one[std::size_t{y} * 4u + x];
                                                   return sum / static_cast<int>(frames.size());
                                               }),
                                       {},
                                       {index, {}, {}}};
            if (auto appended = sink->append(engine, execution, combined); !appended.hasValue())
                return appended.status();
        }
        auto finished = sink->finish(execution);
        if (!finished.hasValue())
            return finished.status();
        auto committed = output->commit(execution, *finished);
        return committed.hasValue() ? an::Status::success() : committed.status();
    };
    ASSERT_TRUE(combine({first, second}, "mean.anseq").ok());
    const std::vector<Pixels> means = listFrames(pathOf("mean.anseq"));
    ASSERT_EQ(means.size(), 3u);
    EXPECT_EQ(means[2][3], (gradient(3u, 0u, 2u) + 100 + 3 + 2) / 2);
    EXPECT_EQ(combine({first, shorter}, "refused.anseq").code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_FALSE(std::filesystem::exists(directory / "refused.anseq"));
}

TEST_F(SequenceCompositionTest, AMosaicIsReadDebayeredWhenAskedAndAsStoredOtherwise)
{
    const an::RawFrameDescriptor mosaic = describe(8u, 8u, true);
    auto source = openByName(writeSer("mosaic.ser", mosaic, 1u, gradient));
    ASSERT_TRUE(source.hasValue());
    auto read = source->read(engine, execution);
    ASSERT_TRUE(read.hasValue() && read->frame);
    const an::RawFrame& stored = read->frame->frame;
    EXPECT_TRUE(stored.descriptor()->cfa().isMosaiced());
    // A demosaiced frame: the frame demosaiced by the sequence's method and its stored pattern.
    auto plan = an::planDemosaic({.method = an::DemosaicMethod::Bilinear}, stored.descriptor()->cfa());
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const auto extent = *an::ImageExtent::create(8u, 8u, 3u, engine.limits());
    const auto layout = *an::ImageLayout::create({extent, 1u, 3u, 1u, 1u}, engine.limits());
    std::vector<std::byte> colour(static_cast<std::size_t>(layout.totalBytes()));
    auto demosaiced = an::demosaicImage(execution, an::SampleType::UInt8, stored.bytes().data(), *stored.layout(),
                                        colour.data(), layout, *plan);
    ASSERT_TRUE(demosaiced.hasValue()) << demosaiced.status().message();
    EXPECT_EQ(demosaiced->method, an::DemosaicMethod::Bilinear);
    EXPECT_EQ(demosaiced->pattern, an::CfaPattern::Rggb);
    // At a red site the red channel is the stored sample.
    EXPECT_EQ(static_cast<int>(colour[0]), gradient(0u, 0u, 0u));
}

TEST_F(SequenceCompositionTest, ASerFrameRateRewriteRestatesEveryTimestampFromTheFirst)
{
    const an::RawFrameDescriptor frame = describe(4u, 2u);
    constexpr std::uint64_t kFirst = an::kSerUnixEpochTicks + 123456789u;
    const std::string recorded = writeSer("recorded.ser", frame, 4u, gradient, kFirst, 333333u);
    // Retiming a SER file to 25 frames a second: frame i at the first timestamp plus i periods of truncated 100-ns
    // ticks; a recording whose first timestamp is zero starts at the Unix epoch.
    const auto rewrite = [&](std::string_view from, std::string_view to, double fps) -> std::string {
        auto source = openByName(from);
        if (!source.hasValue())
        {
            ADD_FAILURE() << source.status().message();
            return {};
        }
        const auto period = static_cast<std::uint64_t>(1.0 / fps * 10000000.0);
        return writeSer(to, *source->descriptor()->fixedFrame, *source->descriptor()->frameCount, gradient, kFirst,
                        period);
    };
    const std::string rewritten = rewrite(recorded, "rewritten.ser", 25.0);
    std::ifstream stream(directory / "rewritten.ser", std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    ASSERT_GE(bytes.size(), 4u * 8u);
    const std::size_t trailer = bytes.size() - 4u * 8u;
    for (std::uint64_t index = 0u; index < 4u; ++index)
    {
        std::uint64_t ticks = 0u;
        for (std::size_t byte = 0u; byte < 8u; ++byte)
            ticks |= std::uint64_t{static_cast<std::uint8_t>(bytes[trailer + index * 8u + byte])} << (8u * byte);
        EXPECT_EQ(ticks, kFirst + index * 400000u) << index;
    }
    auto reopened = openByName(rewritten);
    ASSERT_TRUE(reopened.hasValue());
    EXPECT_TRUE(reopened->descriptor()->timeBase.has_value());
}

TEST_F(SequenceCompositionTest, SizesAreAFramesBytesAndItsCountAndACopyIsTheFilesBytes)
{
    const an::RawFrameDescriptor frame = describe(6u, 4u);
    const std::string recorded = writeSer("sized.ser", frame, 5u, gradient);
    auto source = openByName(recorded);
    ASSERT_TRUE(source.hasValue());
    // `get_frame_size_in_bytes` and `get_sequence_size_in_bytes` for an uncompressed sequence.
    const an::RawFrameDescriptor described = *source->descriptor()->fixedFrame;
    const std::uint64_t frameBytes = std::uint64_t{described.extent().width()} * described.extent().height() *
                                     described.extent().channelCount() * described.sampleFormat().sizeBytes;
    EXPECT_EQ(frameBytes, 24u);
    EXPECT_EQ(frameBytes * *source->descriptor()->frameCount, 120u);
    // `sequence::copy` without a debayer: the file's bytes at a new name.
    std::filesystem::copy_file(directory / "sized.ser", directory / "copied.ser");
    std::ifstream original(directory / "sized.ser", std::ios::binary);
    std::ifstream copied(directory / "copied.ser", std::ios::binary);
    EXPECT_TRUE(std::equal(std::istreambuf_iterator<char>(original), std::istreambuf_iterator<char>(),
                           std::istreambuf_iterator<char>(copied)));
    auto reopened = openByName(pathOf("copied.ser"));
    ASSERT_TRUE(reopened.hasValue());
    EXPECT_EQ(reopened->descriptor()->frameCount, 5u);
}

} // namespace
