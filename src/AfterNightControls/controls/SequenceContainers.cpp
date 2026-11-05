// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The product's sequence containers over the engine's sources and sinks: the containers it reads and writes, opening
/// a file as a source, reading one frame into an image, the conversion a frame needs for a container, and the writer
/// whose container is published whole or not at all.

#include "controls/SequenceContainers.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewFiles.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/AviSequence.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/SequenceFrame.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/IO/SerSequence.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QFileInfo>

#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::ui
{

namespace
{

/// A failure Controls decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A path as the engine reads it.
[[nodiscard]] std::string enginePath(const QString& path)
{
    return path.toUtf8().toStdString();
}

/// A path as the file system reads it.
[[nodiscard]] std::filesystem::path fileSystemPath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

/// How the product reads a SER: under the convention the files in circulation were written with, counting the complete
/// frames of a recording that stopped before its count was written, and a cyan, yellow and magenta mosaic as grey
/// samples.
[[nodiscard]] an::SerSourceOptions serSourceOptions() noexcept
{
    an::SerSourceOptions options;
    options.frameCountPolicy = an::SerFrameCountPolicy::CompleteFrames;
    options.cmyMosaicPolicy = an::SerCmyMosaicPolicy::DescribeAsMono;
    return options;
}

/// The rate a SER's trailer gives, as an exact fraction: one frame per mean interval between the earliest and the
/// latest instant the trailer names, the interval rounded to a whole tick of 100 nanoseconds - the rate the inspection
/// states in frames per second, which a SER is played at. Nothing for a trailer that names fewer than two distinct
/// instants, or an interval that rounds to no tick or past what a fraction's 32-bit term holds.
[[nodiscard]] std::optional<an::SequenceRate> serTimestampRate(const an::SerFileInfo& info) noexcept
{
    if (!info.timestampFrameRate.has_value() || !(*info.timestampFrameRate > 0.0) ||
        !std::isfinite(*info.timestampFrameRate) || info.timestampCount < 2u)
    {
        return std::nullopt;
    }
    // The inspection states the rate over the span between the earliest and the latest instant; the span in ticks is
    // recovered from it to far better than a tick, and the mean interval rounded from it in whole ticks.
    const std::uint64_t intervals = info.timestampCount - 1u;
    const double span = std::round(static_cast<double>(intervals) * static_cast<double>(an::kSerTicksPerSecond) /
                                   *info.timestampFrameRate);
    constexpr double kLargestSpan = 1.0e19;
    if (!(span >= 1.0) || span >= kLargestSpan)
    {
        return std::nullopt;
    }
    const auto ticks = static_cast<std::uint64_t>(span);
    const std::uint64_t interval = ticks / intervals + ((ticks % intervals) * 2u >= intervals ? 1u : 0u);
    if (interval == 0u || interval > (std::numeric_limits<std::uint32_t>::max)())
    {
        return std::nullopt;
    }
    const std::uint64_t divisor = std::gcd(an::kSerTicksPerSecond, interval);
    return an::SequenceRate{static_cast<std::uint32_t>(an::kSerTicksPerSecond / divisor),
                            static_cast<std::uint32_t>(interval / divisor)};
}

/// When a SER says its recording began: its header's UTC start time, or its first frame's timestamp when the header
/// states none.
[[nodiscard]] std::optional<an::UtcInstant> serRecordingTime(const an::SerFileInfo& info) noexcept
{
    if (const std::optional<an::UtcInstant> stated = an::utcInstantFromSerTicks(info.container.header.dateTimeUtcTicks))
    {
        return stated;
    }
    return info.firstTimestamp;
}

/// When an image list's manifest says the list was made: nothing for zero, which states it unknown, or for a time past
/// the instants an `an::UtcInstant` holds.
[[nodiscard]] std::optional<an::UtcInstant> imageListCreationTime(std::uint64_t seconds) noexcept
{
    constexpr std::uint64_t kNanosecondsPerSecond = 1000000000u;
    constexpr std::uint64_t kLargestSeconds =
        static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) / kNanosecondsPerSecond;
    if (seconds == 0u || seconds > kLargestSeconds)
    {
        return std::nullopt;
    }
    return an::UtcInstant{static_cast<std::int64_t>(seconds * kNanosecondsPerSecond)};
}

/// The container's own name, for a message.
[[nodiscard]] const char* containerName(an::SequenceFormat format) noexcept
{
    switch (format)
    {
    case an::SequenceFormat::ImageList:
        return "an image list";
    case an::SequenceFormat::Ser:
        return "SER";
    case an::SequenceFormat::Avi:
        return "AVI";
    case an::SequenceFormat::Mp4:
        return "MP4";
    case an::SequenceFormat::Fits:
        return "a FITS frame container";
    case an::SequenceFormat::Dummy:
        return "generated frames";
    case an::SequenceFormat::Video:
        return "this video container";
    }
    return "this container";
}

/// Whether a container is one of the video containers the FFmpeg sink writes.
[[nodiscard]] constexpr bool isVideoContainer(an::SequenceFormat format) noexcept
{
    return format == an::SequenceFormat::Mp4 || format == an::SequenceFormat::Video;
}

/// The description an image states.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// The video container a path's extension names.
[[nodiscard]] std::optional<an::FfmpegVideoContainer> videoContainerFor(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("mp4") || suffix == QLatin1String("mov"))
    {
        return an::FfmpegVideoContainer::Mp4;
    }
    if (suffix == QLatin1String("mkv"))
    {
        return an::FfmpegVideoContainer::Matroska;
    }
    if (suffix == QLatin1String("webm"))
    {
        return an::FfmpegVideoContainer::WebM;
    }
    return std::nullopt;
}

/// The top-left part of an image at another extent, copied into a planar image of its representation: what a video
/// container's even width and height keep of a frame with an odd one.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
cropTopLeft(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image,
            std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    an::ImageDescriptorRequest request = stored->toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, stored->extent().channelCount(), engine.limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the cropped frame has no extent");
    }
    request.extent = *extent;
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    const auto part = view->subview(0u, 0u, width, height);
    if (!described.has_value() || !part.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the frame has no cropped description");
    }
    an::Result<an::Image<SampleT>> cropped = an::Image<SampleT>::createDestination(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, image);
    if (!cropped.hasValue())
    {
        return cropped.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = cropped->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the cropped frame holds nothing");
        }
        const an::Status copied =
            an::repackImageSamples(execution, an::kSampleTypeOf<SampleT>, part->data(), part->layout(),
                                   access->view().data(), access->view().layout());
        if (copied.failed())
        {
            return copied;
        }
    }
    return an::consumer::AnyImage(std::move(*cropped));
}

/// What a frame becomes for a container: nothing when it is stored as it is, or the converted - and for a video
/// container, cropped - copy the container stores. An image list's frames are stored in its first frame's
/// representation, `listRepresentation`, which is nothing until the list holds a frame.
[[nodiscard]] an::Result<std::optional<an::consumer::AnyImage>>
prepareFrame(const an::EngineContext& engine, const an::ExecutionContext& execution,
             const an::consumer::AnyImage& frame, an::SequenceFormat container,
             const std::optional<an::SampleType>& listRepresentation)
{
    const std::optional<an::ImageDescriptor> stored = descriptorOf(frame);
    if (!stored.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    if (container == an::SequenceFormat::ImageList)
    {
        // Every frame is its own file, written under an intent that converts nothing: a frame of another
        // representation than the list's first is converted to it at corresponding full scales.
        if (!listRepresentation.has_value() || *listRepresentation == stored->sampleType())
        {
            return std::optional<an::consumer::AnyImage>{};
        }
        an::Result<an::consumer::AnyImage> converted = convertViewFrame(engine, execution, frame, *listRepresentation);
        if (!converted.hasValue())
        {
            return converted.status();
        }
        return std::optional<an::consumer::AnyImage>(std::move(*converted));
    }
    const an::ChannelSchema schema = stored->channelSchema();
    const bool mono = schema == an::ChannelSchema::Mono;
    if (!mono && schema != an::ChannelSchema::Rgb)
    {
        return refusal(an::ErrorCode::Unsupported,
                       std::string(containerName(container)) +
                           " stores a grey, mosaic or RGB frame, and this frame is neither");
    }
    an::SampleType target = an::SampleType::UInt8;
    if (container == an::SequenceFormat::Ser)
    {
        const an::SampleType type = stored->sampleType();
        target = type == an::SampleType::UInt8 || type == an::SampleType::Int8 ? an::SampleType::UInt8
                                                                               : an::SampleType::UInt16;
    }
    std::optional<an::consumer::AnyImage> prepared;
    if (stored->sampleType() != target)
    {
        an::Result<an::consumer::AnyImage> converted = convertViewFrame(engine, execution, frame, target);
        if (!converted.hasValue())
        {
            return converted.status();
        }
        prepared.emplace(std::move(*converted));
    }
    if (isVideoContainer(container))
    {
        const std::uint32_t width = stored->extent().width() & ~1u;
        const std::uint32_t height = stored->extent().height() & ~1u;
        if (width == 0u || height == 0u)
        {
            return refusal(an::ErrorCode::Unsupported, "a video frame needs at least two columns and two rows");
        }
        if (width != stored->extent().width() || height != stored->extent().height())
        {
            const an::consumer::AnyImage& source = prepared.has_value() ? *prepared : frame;
            an::Result<an::consumer::AnyImage> cropped = std::visit(
                [&](const auto& typed) { return cropTopLeft(engine, execution, typed, width, height); }, source);
            if (!cropped.hasValue())
            {
                return cropped.status();
            }
            prepared.emplace(std::move(*cropped));
        }
    }
    return prepared;
}

} // namespace

an::DemosaicMethod demosaicMethodFromSettings()
{
    QString setting = QString::fromLatin1(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
    if (auto* context = appContext())
    {
        if (auto* settings = context->getGlobalSettings())
        {
            setting = settings->select(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, setting);
        }
    }
    return demosaicMethodForSetting(setting).method;
}

bool isReadableSequenceContainer(an::SequenceFormat format) noexcept
{
    return format == an::SequenceFormat::Ser || format == an::SequenceFormat::ImageList ||
           an::readsThroughFfmpeg(format);
}

bool isWritableSequenceContainer(an::SequenceFormat format) noexcept
{
    return format == an::SequenceFormat::Ser || format == an::SequenceFormat::ImageList ||
           format == an::SequenceFormat::Avi || isVideoContainer(format);
}

an::Result<OpenedSequence> openSequenceSource(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                              const QString& path, an::SequenceFormat format)
{
    const std::string file = enginePath(path);
    OpenedSequence opened;
    switch (format)
    {
    case an::SequenceFormat::Ser: {
        const an::SerSourceOptions options = serSourceOptions();
        an::Result<an::FrameSource> source = an::createSerSource(engine, execution, file, options);
        if (!source.hasValue())
        {
            return source.status();
        }
        opened.source = std::move(*source);
        if (const an::SequenceDescriptor* described = opened.source.descriptor(); described != nullptr)
        {
            opened.descriptor = *described;
        }
        // The header has no field for a rate, and the engine's source states none: the rate the trailer's instants
        // give is the one the recording was made at. With the time the recording began, it is a fact about the file
        // and never a reason to refuse it, so a trailer that cannot be summarized leaves both unstated.
        an::Result<an::SerFileInfo> info = an::inspectSerFile(engine, execution, file, options);
        if (info.hasValue())
        {
            if (!opened.descriptor.nominalFrameRate.has_value())
            {
                opened.descriptor.nominalFrameRate = serTimestampRate(*info);
            }
            opened.created = serRecordingTime(*info);
        }
        else
        {
            LOG_DEBUG("Sequences: the timing of '{}' could not be read: {}", file, info.status().message());
        }
        return opened;
    }
    case an::SequenceFormat::ImageList: {
        an::Result<an::ImageListManifest> manifest = an::readImageListManifest(engine, execution, file);
        if (!manifest.hasValue())
        {
            return manifest.status();
        }
        std::vector<std::string> frames;
        frames.reserve(manifest->framePaths.size());
        for (const std::string& framePath : manifest->framePaths)
        {
            an::Result<std::string> resolved = an::resolveImageListFramePath(file, framePath);
            if (!resolved.hasValue())
            {
                return resolved.status();
            }
            frames.push_back(std::move(*resolved));
        }
        std::vector<an::CodecReadRequest> requests;
        requests.reserve(frames.size());
        for (const std::string& frame : frames)
        {
            requests.push_back(an::CodecReadRequest{.path = frame});
        }
        an::Result<an::FrameSource> source =
            an::createImageListSource(engine, execution, requests, an::ImageListSourceOptions{});
        if (!source.hasValue())
        {
            return source.status();
        }
        opened.source = std::move(*source);
        if (const an::SequenceDescriptor* described = opened.source.descriptor(); described != nullptr)
        {
            opened.descriptor = *described;
        }
        if (manifest->framesPerSecond > 0.0 && std::isfinite(manifest->framesPerSecond))
        {
            opened.descriptor.nominalFrameRate = sequenceRateOf(manifest->framesPerSecond);
        }
        opened.created = imageListCreationTime(manifest->timestampSeconds);
        opened.framePaths = std::move(frames);
        return opened;
    }
    case an::SequenceFormat::Avi:
    case an::SequenceFormat::Mp4:
    case an::SequenceFormat::Video: {
        an::FfmpegSourceOptions options;
        options.unspecifiedMatrix = an::FfmpegMatrixAssumption::Bt601;
        options.unspecifiedRange = an::FfmpegRangeAssumption::Limited;
        options.emptyPalette = an::FfmpegEmptyPalettePolicy::Grayscale;
        an::Result<an::FrameSource> source = an::createFfmpegSource(engine, execution, file, options);
        if (!source.hasValue())
        {
            return source.status();
        }
        opened.source = std::move(*source);
        break;
    }
    case an::SequenceFormat::Fits:
    case an::SequenceFormat::Dummy:
        return refusal(an::ErrorCode::Unsupported,
                       std::string("the engine reads no sequence from ") + containerName(format) + " file");
    }
    if (const an::SequenceDescriptor* described = opened.source.descriptor(); described != nullptr)
    {
        opened.descriptor = *described;
    }
    return opened;
}

an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>>
readSequenceFrame(const an::EngineContext& engine, const an::ExecutionContext& execution, an::FrameSource& source,
                  std::optional<std::uint64_t> index)
{
    if (index.has_value() && (source.nextIndex() != *index || source.state() == an::SequenceState::EndOfStream))
    {
        const an::Result<an::SequencePosition> moved =
            source.seek(execution, an::SequenceSeekRequest{.kind = an::SequenceSeekKind::Index, .index = *index});
        if (!moved.hasValue())
        {
            return moved.status();
        }
    }
    an::Result<an::SequenceReadResult> read = source.read(engine, execution);
    if (!read.hasValue())
    {
        return read.status();
    }
    if (!read->frame.has_value())
    {
        return std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>{};
    }
    an::SequenceFrame& frame = *read->frame;
    const std::uint64_t position = frame.position.index;
    // Every image the product makes keeps its channels in planes; a video decoder hands out interleaved colour.
    an::Result<an::consumer::AnyImage> image =
        importViewFrame(engine, execution, std::move(frame.frame), std::move(frame.metadata), true);
    if (!image.hasValue())
    {
        return image.status();
    }
    return std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>(
        std::pair<std::uint64_t, an::consumer::AnyImage>(position, std::move(*image)));
}

an::SequenceRate sequenceRateOf(double framesPerSecond) noexcept
{
    if (!(framesPerSecond > 0.0) || !std::isfinite(framesPerSecond))
    {
        return an::SequenceRate{1u, 1u};
    }
    constexpr double kLargest = 4294967.0;
    const double bounded = (std::min)(framesPerSecond, kLargest);
    if (bounded == std::floor(bounded))
    {
        return an::SequenceRate{static_cast<std::uint32_t>(bounded), 1u};
    }
    const auto thousandths = static_cast<std::uint32_t>((std::max)(1.0, std::round(bounded * 1000.0)));
    const std::uint32_t divisor = std::gcd(thousandths, 1000u);
    return an::SequenceRate{thousandths / divisor, 1000u / divisor};
}

double framesPerSecondOf(an::SequenceRate rate) noexcept
{
    if (rate.numerator == 0u || rate.denominator == 0u)
    {
        return 1.0;
    }
    return static_cast<double>(rate.numerator) / static_cast<double>(rate.denominator);
}

std::optional<an::SequenceFormat> writableSequenceContainerFor(const QString& path)
{
    const std::optional<an::SequenceFormat> named = an::sequenceFormatForPath(enginePath(path));
    if (!named.has_value())
    {
        return std::nullopt;
    }
    if (*named == an::SequenceFormat::Video && !videoContainerFor(path).has_value())
    {
        return std::nullopt;
    }
    return isWritableSequenceContainer(*named) ? named : std::nullopt;
}

struct SequenceWriter::State
{
    std::string path;
    SequenceWriteSettings settings;
    std::optional<an::FfmpegVideoContainer> videoContainer;
    std::optional<an::SequenceOutput> output;
    std::optional<an::FrameSink> sink;
    an::SequenceDescriptor descriptor{};
    std::optional<an::SampleType> listRepresentation;
    std::uint64_t written = 0u;
    bool published = false;
    bool failed = false;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    ~State()
    {
        abortNow();
    }

    /// Discards everything not published: the sink first, which gives the payload back and removes the frames an image
    /// list published, then the output, which removes the payload.
    void abortNow() noexcept
    {
        if (sink.has_value())
        {
            (void)sink->close();
            sink.reset();
        }
        if (output.has_value())
        {
            output->abort();
            output.reset();
        }
    }

    /// Fails the writer: what was written is discarded and nothing is written after it.
    [[nodiscard]] an::Status fail(an::Status cause) noexcept
    {
        failed = true;
        abortNow();
        return cause;
    }

    /// Begins the container with the frame it describes.
    [[nodiscard]] an::Status begin(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                   const an::ImageDescriptor& first)
    {
        an::Result<an::SequenceOutput> begun =
            an::SequenceOutput::begin(engine, execution, an::SequenceOutputRequest{.destinationPath = path});
        if (!begun.hasValue())
        {
            return begun.status();
        }
        output.emplace(std::move(*begun));
        descriptor = an::SequenceDescriptor{};
        descriptor.format = settings.container;
        descriptor.frameCount = settings.frameCount;
        descriptor.nominalFrameRate = settings.frameRate;
        if (settings.container != an::SequenceFormat::ImageList)
        {
            descriptor.fixedFrame = an::RawFrameDescriptor::fromImageDescriptor(first);
            if (!descriptor.fixedFrame.has_value())
            {
                return refusal(an::ErrorCode::Unsupported, "the frame has no description a container states");
            }
        }
        an::Result<an::FrameSink> opened = refusal(an::ErrorCode::Unsupported, "no sink writes this container");
        switch (settings.container)
        {
        case an::SequenceFormat::Ser:
            // Each frame's exposure start goes into the trailer - a frame read from a SER carries its own - so the SER
            // keeps the timing its frames state, each frame's capture time written as the frame states it; a frame
            // that states none is written as unknown.
            opened = an::createSerSink(engine, execution, *output, descriptor,
                                       an::SerSinkRequest{.timestamps = an::SerTimestampSource::ExposureStart});
            break;
        case an::SequenceFormat::ImageList: {
            // Each frame is a FITS file of its own, written as a FITS save writes one: in the first frame's
            // representation and with its cards, so its exposure, filter, date and world coordinates stay in its file.
            an::Result<an::OutputIntent> intent = an::OutputIntent::create(
                an::OutputIntentRequest{.sampleType = first.sampleType(), .preserveSourceCards = true});
            if (!intent.hasValue())
            {
                return intent.status();
            }
            listRepresentation = first.sampleType();
            an::ImageListSinkRequest request{.frameIntent = std::move(*intent)};
            request.timestampSeconds = settings.createdSeconds;
            opened = an::createImageListSink(engine, execution, *output, descriptor, request);
            break;
        }
        case an::SequenceFormat::Avi:
            opened = an::createAviSink(engine, execution, *output, descriptor,
                                       an::AviSinkRequest{.mosaicFrames = an::AviMosaicPolicy::WriteSamplesAsMono});
            break;
        case an::SequenceFormat::Mp4:
        case an::SequenceFormat::Video: {
            an::FfmpegSinkRequest request;
            request.container = videoContainer.value_or(an::FfmpegVideoContainer::Mp4);
            request.codec =
                request.container == an::FfmpegVideoContainer::WebM ? an::FfmpegVideoCodec::Vp9 : settings.codec;
            request.mosaicFrames = an::FfmpegMosaicPolicy::WriteSamplesAsMono;
            opened = an::createFfmpegSink(engine, execution, *output, descriptor, request);
            break;
        }
        case an::SequenceFormat::Fits:
        case an::SequenceFormat::Dummy:
            break;
        }
        if (!opened.hasValue())
        {
            return opened.status();
        }
        sink.emplace(std::move(*opened));
        return an::Status::success();
    }
};

SequenceWriter::SequenceWriter() noexcept = default;

SequenceWriter::~SequenceWriter() = default;

SequenceWriter::SequenceWriter(SequenceWriter&& other) noexcept = default;

SequenceWriter& SequenceWriter::operator=(SequenceWriter&& other) noexcept = default;

SequenceWriter::SequenceWriter(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

an::Result<SequenceWriter> SequenceWriter::create(const QString& path, const SequenceWriteSettings& settings)
{
    if (path.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a sequence is written to a named file");
    }
    if (!isWritableSequenceContainer(settings.container))
    {
        return refusal(an::ErrorCode::Unsupported,
                       std::string("the product writes no sequence as ") + containerName(settings.container));
    }
    if (settings.frameRate.numerator == 0u || settings.frameRate.denominator == 0u)
    {
        return refusal(an::ErrorCode::InvalidArgument, "a sequence's frame rate needs two positive terms");
    }
    auto state = std::make_unique<State>();
    state->path = enginePath(path);
    state->settings = settings;
    if (settings.container == an::SequenceFormat::ImageList &&
        QFileInfo(path).suffix().compare(QLatin1String("anseq"), Qt::CaseInsensitive) != 0)
    {
        return refusal(an::ErrorCode::InvalidArgument, "an image list is written to a file ending in .anseq");
    }
    if (isVideoContainer(settings.container))
    {
        state->videoContainer = videoContainerFor(path);
        if (!state->videoContainer.has_value())
        {
            return refusal(an::ErrorCode::Unsupported,
                           "a video is written as MP4, Matroska or WebM, which the file's extension names");
        }
    }
    return SequenceWriter(std::move(state));
}

an::Status SequenceWriter::append(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                  const an::consumer::AnyImage& frame)
{
    if (!m_state || m_state->failed || m_state->published)
    {
        return refusal(an::ErrorCode::InvalidState, "the sequence is no longer being written");
    }
    an::Result<std::optional<an::consumer::AnyImage>> prepared =
        prepareFrame(engine, execution, frame, m_state->settings.container, m_state->listRepresentation);
    if (!prepared.hasValue())
    {
        return m_state->fail(prepared.status());
    }
    const an::consumer::AnyImage& stored = prepared->has_value() ? **prepared : frame;
    if (!m_state->sink.has_value())
    {
        const std::optional<an::ImageDescriptor> first = descriptorOf(stored);
        if (!first.has_value())
        {
            return m_state->fail(refusal(an::ErrorCode::NotInitialized, "the frame holds nothing"));
        }
        if (an::Status begun = m_state->begin(engine, execution, *first); begun.failed())
        {
            return m_state->fail(std::move(begun));
        }
    }
    const std::uint64_t index = m_state->written;
    const an::Status appended = std::visit(
        [&](const auto& typed) -> an::Status {
            an::Result<an::RawFrame> borrowed = an::borrowImageFrame(typed);
            if (!borrowed.hasValue())
            {
                return borrowed.status();
            }
            an::SequenceFrame sequenceFrame{.frame = std::move(*borrowed),
                                            .metadata = typed.metadata(),
                                            .position = an::SequencePosition{.index = index}};
            const an::Result<an::SequenceWriteResult> written = m_state->sink->append(engine, execution, sequenceFrame);
            return written.hasValue() ? an::Status::success() : written.status();
        },
        stored);
    if (appended.failed())
    {
        return m_state->fail(appended);
    }
    ++m_state->written;
    return an::Status::success();
}

an::Result<an::SequenceDescriptor> SequenceWriter::finish(const an::ExecutionContext& execution)
{
    if (!m_state || m_state->failed || m_state->published)
    {
        return refusal(an::ErrorCode::InvalidState, "the sequence is no longer being written");
    }
    if (!m_state->sink.has_value() || m_state->written == 0u)
    {
        return m_state->fail(refusal(an::ErrorCode::EmptySelection, "no frame was written to the sequence"));
    }
    const an::Result<an::SequenceWriteResult> finished = m_state->sink->finish(execution);
    if (!finished.hasValue())
    {
        return m_state->fail(finished.status());
    }
    // A finished sink has handed its payload back; an image list's frames are now the caller's until it is published.
    (void)m_state->sink->close();
    m_state->sink.reset();
    const an::Result<an::SequenceOutputReport> committed = m_state->output->commit(execution, *finished);
    if (!committed.hasValue())
    {
        if (m_state->settings.container == an::SequenceFormat::ImageList)
        {
            (void)an::removeImageListFromDisk(m_state->path);
        }
        return m_state->fail(committed.status());
    }
    m_state->output.reset();
    m_state->published = true;
    an::SequenceDescriptor published = m_state->descriptor;
    published.frameCount = finished->framesWritten;
    return published;
}

void SequenceWriter::abort() noexcept
{
    if (m_state && !m_state->published)
    {
        m_state->failed = true;
        m_state->abortNow();
    }
}

std::uint64_t SequenceWriter::framesWritten() const noexcept
{
    return m_state ? m_state->written : 0u;
}

bool SequenceWriter::published() const noexcept
{
    return m_state && m_state->published;
}

an::Status removeSequenceFromDisk(const QString& path)
{
    if (path.isEmpty())
    {
        return an::Status::success();
    }
    if (QFileInfo(path).suffix().compare(QLatin1String("anseq"), Qt::CaseInsensitive) == 0)
    {
        return an::removeImageListFromDisk(enginePath(path));
    }
    std::error_code failure;
    std::filesystem::remove(fileSystemPath(path), failure);
    if (failure)
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       "the sequence '" + enginePath(path) + "' could not be removed: " + failure.message());
    }
    return an::Status::success();
}

} // namespace an::ui
