// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "IntegrationPipelineProbe.h"

#include "ProbeSupport.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleSource.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <system_error>
#include <utility>

// The pipeline's implementation, written the way the product's workflow executor is written: one
// function per stage, each deciding first whether its artifacts are still valid and only then doing
// the work, each writing what it produced through the output transaction with the identity of the work
// stamped into the file.
//
// Four habits in here are what the engine asks of a caller that orchestrates it.
//
// **An artifact's identity is the workflow's, and the engine carries it.** The identity is a digest of
// the stage, its parameters and the identities of what it read. The engine has no opinion about what
// goes into it - that is the product's manifest - and it has a firm opinion about where it goes: into
// the file's own header through `OutputRequest::identity`, out of it through `artifactIdentityOf`, and
// never inherited by a file derived from a stamped one.
//
// **Every operation is started and ended by the stage that needs it.** An execution context holds a
// share of the thread budget, so a stage builds one per frame and lets it go before the write.
//
// **The workflow says which stage an operation belongs to.** A progress report carries its
// operation's identity and its own units; the workflow is the only thing that knows the operation was
// calibrating the third light, so a forwarding sink tags every report with the running stage.
//
// **What a file cannot carry is restated on read.** A registered canvas states that zero is no data;
// the explicit mask the warp produced describes that, and a sixteen- or thirty-two-bit FITS file has no
// place for a mask, so the stack states the rule again for the frames it reads.

namespace probe::integration
{
namespace
{

/// The lights' dimensions. Small, because the probe is about shape; large enough for a field of stars
/// the matcher can register.
constexpr std::uint32_t kWidth = 160u;

/// The lights' height. See `kWidth`.
constexpr std::uint32_t kHeight = 120u;

/// How many bias, dark and flat frames each master is stacked from.
constexpr std::uint32_t kCalibrationFrames = 3u;

/// The lights' exposure, which the dark matches.
constexpr double kLightExposure = 120.0;

/// The flats' exposure.
constexpr double kFlatExposure = 2.0;

/// The drizzle's enlargement.
constexpr double kDrizzleScale = 2.0;

/// A stage failure the probe itself describes.
///
/// @param code what kind of failure.
/// @param message what went wrong.
/// @return the failure.
[[nodiscard]] an::Status probeFailure(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A 64-bit FNV-1a digest of text, seeded, which two different seeds make into a 128-bit identity.
[[nodiscard]] std::uint64_t digest(std::string_view text, std::uint64_t seed) noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ull ^ seed;
    for (const char character : text)
    {
        hash ^= static_cast<unsigned char>(character);
        hash *= 0x100000001B3ull;
    }
    return hash;
}

/// The identity of one artifact: a digest of everything that decides it, as 32 hexadecimal characters
/// - the longest identity one card holds.
///
/// @param parts the stage, its parameters and the identities or stamps of its inputs, in order.
/// @return the identity text.
[[nodiscard]] std::string identityOf(const std::vector<std::string>& parts)
{
    std::string joined;
    for (const std::string& part : parts)
    {
        joined += part;
        joined.push_back('\x1F');
    }
    char text[33] = {};
    std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(digest(joined, 0u)),
                  static_cast<unsigned long long>(digest(joined, 0x9E3779B97F4A7C15ull)));
    return std::string(text, 32u);
}

/// What identifies a raw input file for the purpose of reuse: its path, its size and when it was last
/// written. The product adds the platform's file identity; a probe that rewrites a file through the
/// same path does not need it.
[[nodiscard]] std::string fileStamp(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path native(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    const std::uintmax_t size = std::filesystem::file_size(native, error);
    const auto written = std::filesystem::last_write_time(native, error);
    return path + "|" + std::to_string(size) + "|" + std::to_string(written.time_since_epoch().count());
}

/// Tags every engine progress report with the stage that started the operation, counts it, and
/// forwards it.
class StageProgress final : public an::ProgressSink
{
public:
    /// Builds a sink for one run.
    ///
    /// @param observer where tagged reports go, or null.
    /// @param names where distinct engine stage names are collected.
    StageProgress(PipelineObserver* observer, std::vector<std::string>& names) noexcept
        : m_observer(observer), m_names(names)
    {
    }

    /// Sets the stage every following report belongs to.
    ///
    /// @param stage the running stage.
    void enter(Stage stage) noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_stage = stage;
        m_reports = 0u;
    }

    /// How many reports arrived since the stage was entered.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t reports() const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        return m_reports;
    }

    /// Tags, counts and forwards one report.
    ///
    /// @param report what the operation said.
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        Stage stage = Stage::MasterBias;
        {
            const std::scoped_lock lock(m_mutex);
            stage = m_stage;
            ++m_reports;
            try
            {
                const std::string name = report.stage == nullptr ? std::string() : std::string(report.stage);
                if (std::find(m_names.begin(), m_names.end(), name) == m_names.end())
                {
                    m_names.push_back(name);
                }
            }
            catch (...)
            {
                // A name that could not be recorded is a name the report does not list.
            }
        }
        if (m_observer != nullptr)
        {
            m_observer->onEngineProgress(stage, report);
        }
    }

private:
    /// Guards the stage, the count and the names; reports arrive on the operation's threads.
    mutable std::mutex m_mutex;

    /// Where tagged reports go.
    PipelineObserver* m_observer = nullptr;

    /// Where distinct names are collected.
    std::vector<std::string>& m_names;

    /// The running stage.
    Stage m_stage = Stage::MasterBias;

    /// Reports since the stage was entered.
    std::uint64_t m_reports = 0u;
};

/// Starts one operation.
[[nodiscard]] an::Result<an::ExecutionContext>
startOperation(const an::EngineContext& engine, const std::stop_token& stopToken, an::ProgressSink& progress)
{
    an::ExecutionOptions options;
    options.stopToken = stopToken;
    options.progressSink = &progress;
    return an::ExecutionContext::create(engine, options);
}

/// The identity a file's header states, or empty when it states none or cannot be read.
///
/// The reuse decision: a header read, no payload decode and no digest of the pixels.
[[nodiscard]] std::string statedIdentity(const an::EngineContext& engine, const std::string& path,
                                         const std::stop_token& stopToken, an::ProgressSink& progress)
{
    std::error_code error;
    if (!std::filesystem::exists(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())),
            error))
    {
        return {};
    }
    an::Result<an::ExecutionContext> execution = startOperation(engine, stopToken, progress);
    if (!execution.hasValue())
    {
        return {};
    }
    const an::Result<an::CodecImageInfo> probed = an::probeImage(
        engine, *execution, an::CodecReadRequest{.path = path, .readMetadata = true, .validatePayload = false});
    if (!probed.hasValue())
    {
        return {};
    }
    const an::Result<std::optional<an::ArtifactIdentity>> identity =
        an::artifactIdentityOf(probed->metadata.codec(), an::CodecNamespace::Fits);
    if (!identity.hasValue() || !identity->has_value())
    {
        return {};
    }
    return (*identity)->artifactId();
}

/// A decoded frame, with the layout and description the workflow restated for it.
///
/// The two are kept rather than re-derived because a frame is read by several steps and the
/// description is the workflow's own - a file has no card for what its samples mean - and no longer
/// because a view points at them: a frame view copies both, so nothing here outlives anything.
struct LoadedFrame
{
    an::RawFrame frame{};
    an::ImageMetadata metadata{};
    an::ImageLayout layout;
    an::ImageDescriptor descriptor;
};

/// Decodes a whole file into a loaded frame.
///
/// @param restated the numeric domain the workflow knows the file's samples to be in, for a file the
///        workflow wrote itself; empty to take the decoder's description. A FITS file has no card for
///        what its samples mean, so a Float32 frame of sensor codes written by one stage is described as
///        physically scaled when the next stage reads it, and a calibration refuses it as a master for
///        frames of sensor codes.
[[nodiscard]] an::Result<LoadedFrame> loadFrame(const an::EngineContext& engine, const std::string& path,
                                                const std::stop_token& stopToken, an::ProgressSink& progress,
                                                std::optional<an::NumericDomain> restated = std::nullopt)
{
    an::Result<an::ExecutionContext> execution = startOperation(engine, stopToken, progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    an::Result<an::CodecDecodeResult> decoded = an::decodeImage(engine, *execution, an::CodecReadRequest{.path = path});
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    const std::optional<an::RawFrameDescriptor> raw = decoded->frame.descriptor();
    const std::optional<an::ImageLayout> layout = decoded->frame.layout();
    std::optional<an::ImageDescriptor> descriptor = raw.has_value() ? raw->toImageDescriptor() : std::nullopt;
    if (!layout.has_value() || !descriptor.has_value())
    {
        return probeFailure(an::ErrorCode::Unsupported, "'" + path + "' is not a resident image");
    }
    if (restated.has_value())
    {
        an::ImageDescriptorRequest request = descriptor->toRequest();
        request.numericDomain = *restated;
        descriptor = an::ImageDescriptor::create(request);
        if (!descriptor.has_value())
        {
            return probeFailure(an::ErrorCode::IncompatibleArguments,
                                "'" + path + "' cannot be restated in that domain");
        }
    }
    return LoadedFrame{.frame = std::move(decoded->frame),
                       .metadata = std::move(decoded->metadata),
                       .layout = *layout,
                       .descriptor = *descriptor};
}

/// Widens a loaded sensor frame into a Float32 image of the same codes, carrying its metadata.
[[nodiscard]] an::Result<an::Image<float>> widen(const an::EngineContext& engine, const LoadedFrame& loaded,
                                                 const std::stop_token& stopToken, an::ProgressSink& progress)
{
    const an::Result<an::ImageDescriptor> described = an::describeSamplesAs(loaded.descriptor, an::SampleType::Float32);
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::Image<float>> image = an::Image<float>::create(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, loaded.metadata);
    if (!image.hasValue())
    {
        return image.status();
    }
    an::Result<an::ExecutionContext> execution = startOperation(engine, stopToken, progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    const std::optional<an::ImageWriteAccess<float>> access = image->write();
    an::ConversionCounters counters;
    const an::Status converted =
        an::convertImageSamples(*execution, loaded.descriptor.sampleType(), loaded.frame.bytes().data(), loaded.layout,
                                an::SampleType::Float32, access->view().data(), access->view().layout(),
                                an::conversionPolicyFor(loaded.descriptor.numericDomain()), counters);
    if (converted.failed())
    {
        return converted;
    }
    return std::move(*image);
}

/// Publishes a Float32 image through the output transaction, stamped with an artifact identity and
/// with any extra cards the workflow states.
[[nodiscard]] an::Status publish(const an::EngineContext& engine, const std::string& path, an::Image<float>& image,
                                 const std::string& identity, std::vector<an::CodecCard> cards,
                                 const std::stop_token& stopToken, an::ProgressSink& progress)
{
    an::Result<an::RawFrame> frame = an::borrowImageFrame(image);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
        an::OutputIntentRequest{.sampleType = an::SampleType::Float32, .cards = std::move(cards)});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    const an::Result<an::ArtifactIdentity> stamp = an::ArtifactIdentity::create(identity, "probe-run");
    if (!stamp.hasValue())
    {
        return stamp.status();
    }
    an::Result<an::ExecutionContext> execution = startOperation(engine, stopToken, progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    const an::Result<an::OutputReport> written =
        an::writeImage(engine, *execution,
                       an::OutputRequest{.destinationPath = path,
                                         .format = an::CodecFormat::Fits,
                                         .intent = &*intent,
                                         .identity = &*stamp,
                                         .durability = an::OutputDurability::CloseOnly},
                       *frame, image.metadata());
    if (!written.hasValue())
    {
        return written.status();
    }
    return image.transition(written->transition);
}

/// One stack's frames, read a band at a time out of the files the codec facade decodes them from.
///
/// The executor hands every read the execution the stack is running as, so the source holds none:
/// it is built once from the paths and reads for whichever operation is asking, exactly as the
/// drizzle source beside it does.
class FileStackSource final : public an::StackFrameSource
{
public:
    /// Builds a source over files.
    ///
    /// @param engine the context the codecs belong to.
    /// @param paths the frames, in frame order.
    /// @param descriptor what every frame is.
    /// @param validity the rule every frame states.
    FileStackSource(const an::EngineContext& engine, const std::vector<std::string>& paths,
                    const an::ImageDescriptor& descriptor, const an::ValidityDescriptor& validity)
        : m_engine(engine), m_paths(paths), m_descriptor(descriptor), m_validity(validity), m_layouts(paths.size())
    {
    }

    /// Reads by region, so no frame is ever held whole.
    ///
    /// @return `StackSourceAccess::Region`.
    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::Region;
    }

    /// Decodes one band of one frame into the executor's slot.
    ///
    /// @param execution the stack's running operation, which the decode runs as.
    /// @param frameIndex which frame.
    /// @param firstRow the band's first row.
    /// @param rows how many rows.
    /// @param buffer the executor's slot.
    /// @return the band, or the decode's failure.
    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        if (frameIndex >= m_paths.size())
        {
            return probeFailure(an::ErrorCode::OutOfRange, "the stack source has no such frame");
        }
        an::Result<an::CodecDecodeResult> decoded = an::decodeImageRegion(
            m_engine, execution, an::CodecReadRequest{.path = m_paths[frameIndex]},
            an::CodecRegion{.x = 0u, .y = firstRow, .width = m_descriptor.extent().width(), .height = rows});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::optional<an::ImageLayout> layout = decoded->frame.layout();
        const std::span<const std::byte> bytes = decoded->frame.bytes();
        if (!layout.has_value() || bytes.size() > buffer.size())
        {
            return probeFailure(an::ErrorCode::IncompatibleArguments, "a decoded band does not fit the read slot");
        }
        std::copy(bytes.begin(), bytes.end(), buffer.begin());
        m_layouts[frameIndex] = *layout;
        return an::StackSourceBand{.sampleType = m_descriptor.sampleType(),
                                   .samples = buffer.data(),
                                   .layout = &*m_layouts[frameIndex],
                                   .frameIndex = frameIndex,
                                   .firstRow = firstRow,
                                   .validity = an::StackBandValidity{.descriptor = m_validity, .mask = nullptr}};
    }

private:
    const an::EngineContext& m_engine;
    const std::vector<std::string>& m_paths;
    an::ImageDescriptor m_descriptor;
    an::ValidityDescriptor m_validity;
    std::vector<std::optional<an::ImageLayout>> m_layouts;
};

/// The calibrated lights, read whole for the drizzle, one at a time.
class FileDrizzleSource final : public an::DrizzleFrameSource
{
public:
    /// Builds a source over files.
    ///
    /// @param engine the context the codecs belong to.
    /// @param paths the frames, in frame order.
    FileDrizzleSource(const an::EngineContext& engine, const std::vector<std::string>& paths)
        : m_engine(engine), m_paths(paths)
    {
    }

    /// Reads whole frames.
    ///
    /// @return `DrizzleSourceAccess::WholeFrame`.
    [[nodiscard]] an::DrizzleSourceAccess access() const noexcept override
    {
        return an::DrizzleSourceAccess::WholeFrame;
    }

    /// Decodes one whole frame into the executor's pixel buffer.
    ///
    /// @param execution the drizzle's own execution, which this source is handed on every read.
    /// @param frameIndex which frame.
    /// @param firstRow zero.
    /// @param rows the frame's height.
    /// @param buffers the executor's buffers.
    /// @return the band, or the decode's failure.
    [[nodiscard]] an::Result<an::DrizzleFrameData> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                        std::uint32_t firstRow, std::uint32_t rows,
                                                        const an::DrizzleReadBuffers& buffers) override
    {
        if (frameIndex >= m_paths.size() || firstRow != 0u)
        {
            return probeFailure(an::ErrorCode::OutOfRange, "the drizzle source has no such frame");
        }
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(m_engine, execution, an::CodecReadRequest{.path = m_paths[frameIndex]});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::optional<an::RawFrameDescriptor> raw = decoded->frame.descriptor();
        if (!raw.has_value() || raw->extent().height() != rows)
        {
            return probeFailure(an::ErrorCode::IncompatibleArguments, "a decoded frame is not the drizzle's frame");
        }
        const std::optional<an::ImageLayout> packed =
            an::ImageLayout::create(an::ImageLayoutRequest{.extent = raw->extent(),
                                                           .elementSizeBytes = 4u,
                                                           .samplesPerPixel = 1u,
                                                           .planeCount = raw->extent().channelCount(),
                                                           .rowAlignmentBytes = 1u},
                                    m_engine.limits());
        const std::optional<an::ImageLayout> source = decoded->frame.layout();
        if (!packed.has_value() || !source.has_value() || packed->totalBytes() > buffers.pixels.size())
        {
            return probeFailure(an::ErrorCode::IncompatibleArguments, "a decoded frame does not fit the pixel buffer");
        }
        for (std::uint32_t plane = 0u; plane < raw->extent().channelCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < rows; ++y)
            {
                const std::optional<std::span<const std::byte>> row = decoded->frame.row(plane, y);
                if (!row.has_value())
                {
                    return probeFailure(an::ErrorCode::InvalidState, "a decoded frame has no such row");
                }
                std::memcpy(buffers.pixels.data() + plane * packed->planeStrideBytes() + y * packed->rowStrideBytes(),
                            row->data(), row->size());
            }
        }
        return an::DrizzleFrameData{.pixels = an::DrizzleSampleBand{
                                        .layout = *packed,
                                        .samples = buffers.pixels.first(static_cast<std::size_t>(packed->totalBytes())),
                                        .sampleType = an::SampleType::Float32}};
    }

private:
    const an::EngineContext& m_engine;
    const std::vector<std::string>& m_paths;
};

/// The text form of a transform, for an identity.
[[nodiscard]] std::string transformText(const std::optional<an::RegistrationTransform>& transform)
{
    if (!transform.has_value())
    {
        return "unregistered";
    }
    std::string text;
    for (const double value : transform->matrix())
    {
        char number[32] = {};
        std::snprintf(number, sizeof(number), "%.17g,", value);
        text += number;
    }
    return text;
}

/// Zero is no data: what a registered canvas states about its uncovered border.
[[nodiscard]] an::ValidityDescriptor zeroIsNoData()
{
    return *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
}

/// A card a workflow states on an output.
[[nodiscard]] an::Result<an::CodecCard> card(const char* keyword, std::string value, const char* comment)
{
    return an::CodecCard::create(an::CodecNamespace::Fits, keyword, std::move(value), comment);
}

/// Everything one run holds between stages.
struct RunState
{
    const an::EngineContext& engine;
    const std::stop_token& stopToken;
    PipelineObserver* observer = nullptr;
    StageProgress& progress;
    PipelineReport& report;

    std::string root{};
    std::vector<std::string> biasPaths{};
    std::vector<std::string> darkPaths{};
    std::vector<std::string> flatPaths{};
    std::vector<std::string> lightPaths{};

    std::string biasIdentity{};
    std::string darkIdentity{};
    std::string flatIdentity{};
    std::vector<std::string> calibratedPaths{};
    std::vector<std::string> calibratedIdentities{};
    std::string registrationIdentity{};
    std::vector<std::optional<an::RegistrationTransform>> transforms{};
    std::vector<std::string> alignedPaths{};
    std::vector<std::string> alignedIdentities{};
    std::string normalizationIdentity{};
    std::vector<an::FrameNormalization> matches{};

    /// What the workflow recorded about every artifact it published, keyed by identity. The pipeline's
    /// own record, which outlives the run.
    std::map<std::string, ArtifactRecord>* artifacts = nullptr;
};

/// Enters a published artifact in the workflow's record: which stage made it, which artifacts it read,
/// and the lineage its image carried when it was written.
///
/// The file carries its samples, its acquisition facts, its cards and its identity; it does not carry
/// what was done to it. So the history of a stacked frame is walked through this record, from the
/// stack's inputs to theirs, the way the product walks its own manifest and archive.
void recordArtifact(RunState& state, Stage stage, const std::string& identity, std::vector<std::string> inputs,
                    const an::Image<float>& image)
{
    ArtifactRecord record{.stage = stage, .inputs = std::move(inputs)};
    for (const an::ProvenanceEntry& entry : image.metadata().provenance().entries())
    {
        record.lineage.push_back(entry.operation() + ": " + entry.detail());
    }
    (*state.artifacts)[identity] = std::move(record);
}

/// Begins a stage's record and tells the progress sink.
[[nodiscard]] StageRecord& beginStage(RunState& state, Stage stage)
{
    state.progress.enter(stage);
    state.report.stages.push_back(StageRecord{.stage = stage});
    return state.report.stages.back();
}

/// Records one finished unit of a stage.
void unitFinished(RunState& state, StageRecord& record, std::uint32_t completed, std::uint32_t total)
{
    ++record.workflowReports;
    record.engineReports = state.progress.reports();
    if (state.observer != nullptr)
    {
        state.observer->onStageProgress(record.stage, completed, total);
    }
}

/// A calibration master held in memory for the stages that subtract or divide it.
struct LoadedMaster
{
    std::optional<LoadedFrame> loaded{};
    an::CalibrationFrameView view{};
};

/// Loads a master from its file and describes it as a calibration master.
[[nodiscard]] an::Status loadMaster(RunState& state, const std::string& path, LoadedMaster& master)
{
    an::Result<LoadedFrame> loaded =
        loadFrame(state.engine, path, state.stopToken, state.progress, an::NumericDomain::RawSensorCode);
    if (!loaded.hasValue())
    {
        return loaded.status();
    }
    master.loaded.emplace(std::move(*loaded));
    const an::Result<an::CalibrationFrameView> view =
        an::CalibrationFrameView::create(master.loaded->frame.bytes().data(), master.loaded->frame.bytes().size(),
                                         master.loaded->layout, master.loaded->descriptor);
    if (!view.hasValue())
    {
        return view.status();
    }
    master.view = *view;
    return an::Status::success();
}

/// Stacks a set of files into a master, optionally calibrating each frame on the way in, and publishes
/// it.
[[nodiscard]] an::Status stackMaster(RunState& state, StageRecord& record, const std::vector<std::string>& paths,
                                     const std::string& destination, const std::string& identity,
                                     const an::CalibrationMasters* masters, std::vector<std::string> upstream)
{
    an::Result<an::ExecutionContext> probeExecution = startOperation(state.engine, state.stopToken, state.progress);
    if (!probeExecution.hasValue())
    {
        return probeExecution.status();
    }
    const an::Result<an::CodecImageInfo> probed =
        an::probeImage(state.engine, *probeExecution, an::CodecReadRequest{.path = paths.front()});
    if (!probed.hasValue())
    {
        return probed.status();
    }
    // Released before the stack starts: a live execution holds a share of the thread budget, and the
    // stack's own execution asks for all of it.
    *probeExecution = an::ExecutionContext{};
    const std::optional<an::ImageDescriptor> input = probed->descriptor.toImageDescriptor();
    if (!input.has_value())
    {
        return probeFailure(an::ErrorCode::Unsupported, "a calibration frame is not a resident image");
    }

    const an::StackRequest request{
        .input =
            an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(paths.size()), .descriptor = *input},
        .combination = an::StackCombination::Average,
        .rejection = an::StackRejectionRequest{.method = an::StackRejection::WinsorizedSigmaClipping}};
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    if (!model.has_value())
    {
        return probeFailure(an::ErrorCode::InvalidArgument,
                            std::string("the stack model refused a master: ") +
                                an::stackModelErrorName(an::StackModel::check(request)));
    }

    std::vector<an::CalibrationPlan> plans;
    an::StackSourceContract contract{.state = an::StackFrameState::Raw};
    const an::ImageDescriptor tile = an::stackSourceCalibrationDescriptor(*model);
    if (masters != nullptr)
    {
        an::Result<an::ExecutionContext> preparing = startOperation(state.engine, state.stopToken, state.progress);
        if (!preparing.hasValue())
        {
            return preparing.status();
        }
        an::Result<an::CalibrationPlan> plan = an::prepareImageCalibration(
            *preparing, tile, *masters, an::CalibrationRequest{.flatNormalization = an::FlatNormalizationMode::None});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        plans.push_back(std::move(*plan));
        contract.calibration = plans;
        contract.masters = masters;
    }

    an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    FileStackSource source(state.engine, paths, *input, an::ValidityDescriptor{});
    an::Result<an::StackResult> stacked = an::executeStack(state.engine, *execution, *model, source, contract,
                                                           an::StackExecutionRequest{}, an::StackExecutionOutputs{});
    if (!stacked.hasValue())
    {
        return stacked.status();
    }
    // The stack's execution ends here rather than at the end of the function. The write below starts
    // an execution of its own, and while this one lives it holds the whole thread budget: the first
    // version of this stage kept it and every write was refused as busy.
    *execution = an::ExecutionContext{};

    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(stacked->image));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::fromStorage(std::move(*storage), stacked->descriptor, probed->metadata);
    if (!image.hasValue())
    {
        return image.status();
    }
    const an::Result<an::MetadataTransition> change =
        an::stackTransition(*model, stacked->report, probed->metadata.semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    const an::Status stated = image->transition(*change);
    if (stated.failed())
    {
        return stated;
    }
    const an::Status published =
        publish(state.engine, destination, *image, identity, {}, state.stopToken, state.progress);
    if (published.failed())
    {
        return published;
    }
    recordArtifact(state, record.stage, identity, std::move(upstream), *image);
    ++record.produced;
    return an::Status::success();
}

/// Runs one master stage: reuse when the header says so, stack when it does not.
[[nodiscard]] an::Status masterStage(RunState& state, Stage stage, const std::vector<std::string>& paths,
                                     const char* name, std::vector<std::string> upstream, std::string& identity)
{
    StageRecord& record = beginStage(state, stage);
    std::vector<std::string> parts{stageName(stage), "average winsorized"};
    for (const std::string& path : paths)
    {
        parts.push_back(fileStamp(path));
    }
    parts.insert(parts.end(), upstream.begin(), upstream.end());
    identity = identityOf(parts);
    const std::string destination = state.root + "/masters/" + name + ".fits";

    if (statedIdentity(state.engine, destination, state.stopToken, state.progress) == identity)
    {
        ++record.reused;
        unitFinished(state, record, 1u, 1u);
        return an::Status::success();
    }

    an::Status stacked = an::Status::success();
    if (stage == Stage::MasterBias)
    {
        stacked = stackMaster(state, record, paths, destination, identity, nullptr, upstream);
    }
    else
    {
        LoadedMaster bias;
        LoadedMaster dark;
        an::Status loaded = loadMaster(state, state.root + "/masters/bias.fits", bias);
        if (loaded.ok() && stage == Stage::MasterFlat)
        {
            loaded = loadMaster(state, state.root + "/masters/dark.fits", dark);
        }
        if (loaded.failed())
        {
            return loaded;
        }
        an::CalibrationMasters masters{.bias = bias.view};
        if (stage == Stage::MasterFlat)
        {
            masters.dark = dark.view;
        }
        stacked = stackMaster(state, record, paths, destination, identity, &masters, upstream);
    }
    if (stacked.failed())
    {
        return stacked;
    }
    unitFinished(state, record, 1u, 1u);
    return an::Status::success();
}

/// Calibrates every light against the three masters.
[[nodiscard]] an::Status calibrateLights(RunState& state)
{
    StageRecord& record = beginStage(state, Stage::CalibrateLights);
    const auto total = static_cast<std::uint32_t>(state.lightPaths.size());
    LoadedMaster bias;
    LoadedMaster dark;
    LoadedMaster flat;
    bool mastersLoaded = false;
    an::CalibrationMasters masters;
    std::optional<an::CalibrationPlan> plan;

    for (std::uint32_t index = 0u; index < total; ++index)
    {
        const std::string identity = identityOf({stageName(Stage::CalibrateLights), "fixed dark, central-mean flat",
                                                 fileStamp(state.lightPaths[index]), state.biasIdentity,
                                                 state.darkIdentity, state.flatIdentity});
        const std::string destination = state.root + "/calibrated/light_" + std::to_string(index) + "_cal.fits";
        state.calibratedPaths.push_back(destination);
        state.calibratedIdentities.push_back(identity);
        if (statedIdentity(state.engine, destination, state.stopToken, state.progress) == identity)
        {
            ++record.reused;
            unitFinished(state, record, index + 1u, total);
            continue;
        }

        if (!mastersLoaded)
        {
            for (const auto& [path, master] : {std::pair<const char*, LoadedMaster*>{"bias", &bias},
                                               std::pair<const char*, LoadedMaster*>{"dark", &dark},
                                               std::pair<const char*, LoadedMaster*>{"flat", &flat}})
            {
                const an::Status loaded = loadMaster(state, state.root + "/masters/" + path + ".fits", *master);
                if (loaded.failed())
                {
                    return loaded;
                }
            }
            masters = an::CalibrationMasters{.bias = bias.view, .dark = dark.view, .flat = flat.view};
            mastersLoaded = true;
        }

        an::Result<LoadedFrame> raw = loadFrame(state.engine, state.lightPaths[index], state.stopToken, state.progress);
        if (!raw.hasValue())
        {
            return raw.status();
        }
        an::Result<an::Image<float>> light = widen(state.engine, *raw, state.stopToken, state.progress);
        if (!light.hasValue())
        {
            return light.status();
        }
        // The spelling that used to dangle: an image hands its layout and its description out as
        // values, and the view keeps what it is given rather than pointing into this expression.
        const an::Result<an::CalibrationFrameView> view = an::CalibrationFrameView::create(
            light->storage().data(), light->layout()->totalBytes(), *light->layout(), *light->descriptor());
        if (!view.hasValue())
        {
            return view.status();
        }
        an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        if (!plan.has_value())
        {
            an::Result<an::CalibrationPlan> prepared = an::prepareImageCalibration(*execution, *view, masters);
            if (!prepared.hasValue())
            {
                return prepared.status();
            }
            plan = std::move(*prepared);
        }
        an::Result<an::Image<float>> calibrated =
            an::Image<float>::createDestination(state.engine, view->descriptor(), an::StorageArrangement::Planar,
                                                an::StorageInitialization::Uninitialized, *light);
        if (!calibrated.hasValue())
        {
            return calibrated.status();
        }
        {
            const std::optional<an::ImageWriteAccess<float>> access = calibrated->write();
            const an::Result<an::CalibrationResult> ran =
                an::calibrateImage(*execution, *plan, *view, masters, access->view().data(),
                                   access->view().layout().totalBytes(), access->view().layout());
            if (!ran.hasValue())
            {
                return ran.status();
            }
        }
        *execution = an::ExecutionContext{};
        const an::Result<an::MetadataTransition> change =
            an::imageCalibrationTransition(*plan, light->metadata().semantics());
        if (!change.hasValue())
        {
            return change.status();
        }
        const an::Status stated = calibrated->transition(*change);
        if (stated.failed())
        {
            return stated;
        }
        const an::Status published =
            publish(state.engine, destination, *calibrated, identity, {}, state.stopToken, state.progress);
        if (published.failed())
        {
            return published;
        }
        recordArtifact(state, Stage::CalibrateLights, identity,
                       {state.biasIdentity, state.darkIdentity, state.flatIdentity}, *calibrated);
        ++record.produced;
        unitFinished(state, record, index + 1u, total);
    }
    return an::Status::success();
}

/// Detects stars on every calibrated light and matches each field to the reference light's.
[[nodiscard]] an::Status
registerLights(RunState& state, std::map<std::string, std::vector<std::optional<an::RegistrationTransform>>>& cache)
{
    StageRecord& record = beginStage(state, Stage::RegisterLights);
    const auto total = static_cast<std::uint32_t>(state.calibratedPaths.size());
    std::vector<std::string> parts{stageName(Stage::RegisterLights), "affine, reference light 0"};
    parts.insert(parts.end(), state.calibratedIdentities.begin(), state.calibratedIdentities.end());
    state.registrationIdentity = identityOf(parts);

    const auto cached = cache.find(state.registrationIdentity);
    if (cached != cache.end())
    {
        state.transforms = cached->second;
        record.reused = total;
        unitFinished(state, record, total, total);
    }
    else
    {
        std::vector<std::vector<an::StarAlignmentPoint>> fields;
        for (std::uint32_t index = 0u; index < total; ++index)
        {
            an::Result<LoadedFrame> loaded = loadFrame(state.engine, state.calibratedPaths[index], state.stopToken,
                                                       state.progress, an::NumericDomain::RawSensorCode);
            if (!loaded.hasValue())
            {
                return loaded.status();
            }
            an::StarDetectionRequest request;
            request.fullScale = 65535.0;
            request.maximumStars = 200u;
            const an::Result<an::StarFrameView> view = an::StarFrameView::create(
                loaded->frame.bytes().data(), loaded->frame.bytes().size(), loaded->layout, loaded->descriptor);
            if (!view.hasValue())
            {
                return view.status();
            }
            const an::Result<an::StarDetectionPlan> plan =
                an::planStarDetection(request, loaded->descriptor, loaded->layout);
            if (!plan.hasValue())
            {
                return plan.status();
            }
            an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
            if (!execution.hasValue())
            {
                return execution.status();
            }
            an::StarDetectionResult detected;
            const an::Status ran = an::detectStars(*execution, *view, *plan, detected);
            if (ran.failed())
            {
                return ran;
            }
            std::vector<an::StarAlignmentPoint> points;
            for (const an::DetectedStar& star : detected.stars)
            {
                points.push_back(an::StarAlignmentPoint{.x = star.positionX, .y = star.positionY});
            }
            fields.push_back(std::move(points));
            unitFinished(state, record, index + 1u, total);
        }

        an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        const an::StarAlignmentRequest alignment{.model = an::StarTransformModel::Affine, .minimumMatches = 6u};
        const an::Result<an::PreparedStarAlignmentReference> reference =
            an::prepareStarAlignmentReference(*execution, fields.front(), alignment);
        if (!reference.hasValue())
        {
            return reference.status();
        }
        state.transforms.assign(total, std::nullopt);
        state.transforms.front() = an::RegistrationTransform{};
        for (std::uint32_t index = 1u; index < total; ++index)
        {
            const an::Result<an::StarAlignmentResult> matched =
                an::alignStarFields(*execution, fields[index], *reference);
            if (!matched.hasValue())
            {
                continue;
            }
            const an::Result<an::RegistrationTransform> transform = an::registrationTransformOf(matched->transform);
            if (transform.hasValue())
            {
                state.transforms[index] = *transform;
            }
        }
        cache[state.registrationIdentity] = state.transforms;
        record.produced = total;
    }
    state.report.registeredLights = static_cast<std::uint32_t>(
        std::count_if(state.transforms.begin(), state.transforms.end(), [](const auto& t) { return t.has_value(); }));
    return an::Status::success();
}

/// Warps every registered light onto the reference geometry.
[[nodiscard]] an::Status alignLights(RunState& state)
{
    StageRecord& record = beginStage(state, Stage::AlignLights);
    const auto total = static_cast<std::uint32_t>(state.calibratedPaths.size());
    for (std::uint32_t index = 0u; index < total; ++index)
    {
        if (!state.transforms[index].has_value())
        {
            unitFinished(state, record, index + 1u, total);
            continue;
        }
        const std::string identity =
            identityOf({stageName(Stage::AlignLights), "linear, constant border", state.calibratedIdentities[index],
                        transformText(state.transforms[index])});
        const std::string destination = state.root + "/registered/light_" + std::to_string(index) + "_reg.fits";
        state.alignedPaths.push_back(destination);
        state.alignedIdentities.push_back(identity);
        if (statedIdentity(state.engine, destination, state.stopToken, state.progress) == identity)
        {
            ++record.reused;
            unitFinished(state, record, index + 1u, total);
            continue;
        }

        an::Result<LoadedFrame> loaded = loadFrame(state.engine, state.calibratedPaths[index], state.stopToken,
                                                   state.progress, an::NumericDomain::RawSensorCode);
        if (!loaded.hasValue())
        {
            return loaded.status();
        }
        const an::Result<an::RegistrationWarpPlan> plan =
            an::planRegistrationWarp(loaded->descriptor, *state.transforms[index], an::SipDistortion{},
                                     an::RegistrationWarpRequest{.width = kWidth, .height = kHeight});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        an::Result<an::Image<float>> warped =
            an::Image<float>::create(state.engine, plan->destinationDescriptor(), an::StorageArrangement::Planar,
                                     an::StorageInitialization::Zeroed, loaded->metadata);
        an::Result<an::ValidityMask> mask =
            an::ValidityMask::create(state.engine, plan->destinationDescriptor().extent());
        if (!warped.hasValue() || !mask.hasValue())
        {
            return warped.hasValue() ? mask.status() : warped.status();
        }
        {
            an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
            if (!execution.hasValue())
            {
                return execution.status();
            }
            const std::optional<an::ImageWriteAccess<float>> access = warped->write();
            const std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
            const an::Result<an::RegistrationWarpResult> ran = an::warpRegisteredImage(
                *execution, an::SampleType::Float32, loaded->frame.bytes().data(), loaded->layout,
                access->view().data(), access->view().layout(), *plan, an::KernelValidityView{}, *maskView);
            if (!ran.hasValue())
            {
                return ran.status();
            }
        }
        const an::Result<an::MetadataTransition> change = an::operationTransition(
            an::kRegistrationWarpEffect,
            an::OperationOutcome{.operation = "an::warpRegisteredImage", .detail = "onto the reference light"});
        if (!change.hasValue())
        {
            return change.status();
        }
        const an::Status stated = warped->transition(plan->destinationDescriptor(), *change);
        if (stated.failed())
        {
            return stated;
        }
        const an::Status published =
            publish(state.engine, destination, *warped, identity, {}, state.stopToken, state.progress);
        if (published.failed())
        {
            return published;
        }
        recordArtifact(state, Stage::AlignLights, identity, {state.calibratedIdentities[index]}, *warped);
        ++record.produced;
        unitFinished(state, record, index + 1u, total);
    }
    return an::Status::success();
}

/// Measures every registered light and matches it to the reference light's level.
[[nodiscard]] an::Status normalizeLights(RunState& state,
                                         std::map<std::string, std::vector<an::FrameNormalization>>& cache)
{
    StageRecord& record = beginStage(state, Stage::NormalizeLights);
    const auto total = static_cast<std::uint32_t>(state.alignedPaths.size());
    std::vector<std::string> parts{stageName(Stage::NormalizeLights), "additive scaling, MAD, reference light 0"};
    parts.insert(parts.end(), state.alignedIdentities.begin(), state.alignedIdentities.end());
    state.normalizationIdentity = identityOf(parts);

    const auto cached = cache.find(state.normalizationIdentity);
    if (cached != cache.end())
    {
        state.matches = cached->second;
        record.reused = total;
        unitFinished(state, record, total, total);
        return an::Status::success();
    }

    std::vector<an::NormalizationFrameStatistics> measured;
    const an::ValidityDescriptor border = zeroIsNoData();
    for (std::uint32_t index = 0u; index < total; ++index)
    {
        an::Result<LoadedFrame> loaded = loadFrame(state.engine, state.alignedPaths[index], state.stopToken,
                                                   state.progress, an::NumericDomain::RawSensorCode);
        if (!loaded.hasValue())
        {
            return loaded.status();
        }
        const an::Result<an::NormalizationFrameView> view = an::NormalizationFrameView::create(
            loaded->frame.bytes().data(), loaded->frame.bytes().size(), loaded->layout, loaded->descriptor,
            an::KernelValidityView{.descriptor = border});
        if (!view.hasValue())
        {
            return view.status();
        }
        an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        an::Result<an::NormalizationFrameStatistics> statistics =
            an::measureGlobalNormalization(*execution, *view, an::ScaleEstimator::MedianAbsoluteDeviation);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        measured.push_back(*statistics);
        unitFinished(state, record, index + 1u, total);
    }
    const an::Result<an::NormalizationReference> reference =
        an::combineNormalizationReferences(std::span<const an::NormalizationFrameStatistics>(measured.data(), 1u));
    if (!reference.hasValue())
    {
        return reference.status();
    }
    state.matches.clear();
    for (std::uint32_t index = 0u; index < total; ++index)
    {
        an::Result<an::FrameNormalization> match =
            index == 0u ? an::identityNormalization(an::NormalizationMethod::AdditiveScaling, 1u)
                        : an::estimateGlobalNormalization(*reference, measured[index],
                                                          an::NormalizationMethod::AdditiveScaling);
        if (!match.hasValue())
        {
            return match.status();
        }
        match->estimator = an::ScaleEstimator::MedianAbsoluteDeviation;
        state.matches.push_back(*match);
    }
    cache[state.normalizationIdentity] = state.matches;
    record.produced = total;
    return an::Status::success();
}

/// Stacks the registered lights with rejection, using the prepared matches.
[[nodiscard]] an::Status stackLights(RunState& state)
{
    StageRecord& record = beginStage(state, Stage::StackLights);
    std::vector<std::string> parts{stageName(Stage::StackLights), "average, sigma clipping, global additive scaling",
                                   state.normalizationIdentity};
    parts.insert(parts.end(), state.alignedIdentities.begin(), state.alignedIdentities.end());
    const std::string identity = identityOf(parts);
    const std::string destination = state.root + "/integration/light_stack.fits";
    if (statedIdentity(state.engine, destination, state.stopToken, state.progress) == identity)
    {
        ++record.reused;
        unitFinished(state, record, 1u, 1u);
        return an::Status::success();
    }

    an::Result<an::ExecutionContext> probing = startOperation(state.engine, state.stopToken, state.progress);
    if (!probing.hasValue())
    {
        return probing.status();
    }
    const an::Result<an::CodecImageInfo> probed =
        an::probeImage(state.engine, *probing, an::CodecReadRequest{.path = state.alignedPaths.front()});
    if (!probed.hasValue())
    {
        return probed.status();
    }
    *probing = an::ExecutionContext{};
    const an::ImageDescriptor input = *probed->descriptor.toImageDescriptor();

    // The exposures the lights carry, summed into the card the product writes. The stack preserves the
    // reference frame's acquisition record, which is one exposure and not the integration's.
    double totalExposure = 0.0;
    for (const std::string& path : state.alignedPaths)
    {
        an::Result<an::ExecutionContext> reading = startOperation(state.engine, state.stopToken, state.progress);
        if (!reading.hasValue())
        {
            return reading.status();
        }
        const an::Result<an::CodecImageInfo> header =
            an::probeImage(state.engine, *reading, an::CodecReadRequest{.path = path, .validatePayload = false});
        if (header.hasValue())
        {
            totalExposure += header->metadata.acquisition().exposureSeconds().value_or(0.0);
        }
    }

    const an::StackRequest request{
        .input = an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(state.alignedPaths.size()),
                                           .descriptor = input},
        .combination = an::StackCombination::Average,
        .rejection = an::StackRejectionRequest{.method = an::StackRejection::SigmaClipping},
        .normalization = an::StackNormalizationRequest{.scope = an::StackNormalizationScope::Global,
                                                       .globalMethod = an::NormalizationMethod::AdditiveScaling,
                                                       .scaleEstimator = an::ScaleEstimator::MedianAbsoluteDeviation}};
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    if (!model.has_value())
    {
        return probeFailure(an::ErrorCode::InvalidArgument,
                            std::string("the stack model refused the lights: ") +
                                an::stackModelErrorName(an::StackModel::check(request)));
    }
    const an::StackSourceContract contract{.state = an::StackFrameState::Calibrated, .normalization = state.matches};

    an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    FileStackSource source(state.engine, state.alignedPaths, input, zeroIsNoData());
    an::Result<an::StackResult> stacked =
        an::executeStack(state.engine, *execution, *model, source, contract,
                         an::StackExecutionRequest{.maximumTileRows = 32u}, an::StackExecutionOutputs{});
    if (!stacked.hasValue())
    {
        return stacked.status();
    }
    *execution = an::ExecutionContext{};

    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(stacked->image));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::fromStorage(std::move(*storage), stacked->descriptor, probed->metadata);
    if (!image.hasValue())
    {
        return image.status();
    }
    const an::Result<an::MetadataTransition> change =
        an::stackTransition(*model, stacked->report, probed->metadata.semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    const an::Status stated = image->transition(*change);
    if (stated.failed())
    {
        return stated;
    }
    std::vector<an::CodecCard> cards;
    an::Result<an::CodecCard> exposure = card("TOTEXP", std::to_string(totalExposure), "total integration in seconds");
    if (!exposure.hasValue())
    {
        return exposure.status();
    }
    cards.push_back(std::move(*exposure));
    const an::Status published =
        publish(state.engine, destination, *image, identity, std::move(cards), state.stopToken, state.progress);
    if (published.failed())
    {
        return published;
    }
    recordArtifact(state, Stage::StackLights, identity, state.alignedIdentities, *image);
    ++record.produced;
    unitFinished(state, record, 1u, 1u);
    return an::Status::success();
}

/// Drizzles the calibrated lights onto a finer grid through the registration.
[[nodiscard]] an::Status drizzleLights(RunState& state)
{
    StageRecord& record = beginStage(state, Stage::DrizzleLights);
    std::vector<std::string> paths;
    std::vector<std::string> parts{stageName(Stage::DrizzleLights), "square, scale 2, drop 0.9"};
    std::vector<an::RegistrationTransform> transforms;
    for (std::size_t index = 0u; index < state.calibratedPaths.size(); ++index)
    {
        if (!state.transforms[index].has_value())
        {
            continue;
        }
        paths.push_back(state.calibratedPaths[index]);
        transforms.push_back(*state.transforms[index]);
        parts.push_back(state.calibratedIdentities[index]);
        parts.push_back(transformText(state.transforms[index]));
    }
    const std::string identity = identityOf(parts);
    const std::string destination = state.root + "/integration/light_drizzle.fits";
    if (statedIdentity(state.engine, destination, state.stopToken, state.progress) == identity)
    {
        ++record.reused;
        unitFinished(state, record, 1u, 1u);
        return an::Status::success();
    }

    an::Result<an::ExecutionContext> probing = startOperation(state.engine, state.stopToken, state.progress);
    if (!probing.hasValue())
    {
        return probing.status();
    }
    const an::Result<an::CodecImageInfo> probed =
        an::probeImage(state.engine, *probing, an::CodecReadRequest{.path = paths.front()});
    if (!probed.hasValue())
    {
        return probed.status();
    }
    *probing = an::ExecutionContext{};

    an::Result<an::DrizzleModel> model =
        an::DrizzleModel::create(an::DrizzleRequest{.input = *probed->descriptor.toImageDescriptor(),
                                                    .frameCount = static_cast<std::uint32_t>(paths.size()),
                                                    .parameters = an::DrizzleParameters{.scale = kDrizzleScale}},
                                 state.engine.limits());
    if (!model.hasValue())
    {
        return model.status();
    }
    std::vector<an::DrizzleFrameParameters> frames;
    for (const an::RegistrationTransform& transform : transforms)
    {
        an::Result<an::DrizzleMapDescriptor> geometry =
            an::DrizzleMapDescriptor::create(*model, an::DrizzleMappingRequest{.transform = transform});
        if (!geometry.hasValue())
        {
            return geometry.status();
        }
        frames.push_back(an::DrizzleFrameParameters{.geometry = *geometry});
    }

    an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    FileDrizzleSource source(state.engine, paths);
    // The drizzle carries an artifact identity of its own, as two integers; the file carries one as
    // card-safe text. The workflow has one identity and has to spell it twice.
    const an::DrizzleExecutionRequest drizzleRequest{
        .artifact = an::DrizzleArtifactIdentity{.runId = 1u, .artifactId = digest(identity, 0u) | 1u}};
    an::Result<an::DrizzleResult> drizzled =
        an::executeDrizzle(state.engine, *execution, *model, source, frames, drizzleRequest);
    if (!drizzled.hasValue())
    {
        return drizzled.status();
    }
    *execution = an::ExecutionContext{};

    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(drizzled->image));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::fromStorage(std::move(*storage), drizzled->descriptor, probed->metadata);
    if (!image.hasValue())
    {
        return image.status();
    }
    const an::Result<an::MetadataTransition> change = an::drizzleTransition(*model, probed->metadata.semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    const an::Status stated = image->transition(*change);
    if (stated.failed())
    {
        return stated;
    }
    const an::Status published =
        publish(state.engine, destination, *image, identity, {}, state.stopToken, state.progress);
    if (published.failed())
    {
        return published;
    }
    std::vector<std::string> inputs;
    for (std::size_t index = 0u; index < state.calibratedPaths.size(); ++index)
    {
        if (state.transforms[index].has_value())
        {
            inputs.push_back(state.calibratedIdentities[index]);
        }
    }
    recordArtifact(state, Stage::DrizzleLights, identity, std::move(inputs), *image);
    ++record.produced;
    unitFinished(state, record, 1u, 1u);
    return an::Status::success();
}

/// Reads back what the stacked and drizzled files say about themselves.
[[nodiscard]] an::Status readProducts(RunState& state)
{
    an::Result<LoadedFrame> stacked =
        loadFrame(state.engine, state.root + "/integration/light_stack.fits", state.stopToken, state.progress);
    if (!stacked.hasValue())
    {
        return stacked.status();
    }
    for (const an::ProvenanceEntry& entry : stacked->metadata.provenance().entries())
    {
        state.report.stackLineage.push_back(entry.operation() + ": " + entry.detail());
    }
    const an::Result<std::optional<an::ArtifactIdentity>> stackIdentity =
        an::artifactIdentityOf(stacked->metadata.codec(), an::CodecNamespace::Fits);
    if (stackIdentity.hasValue() && stackIdentity->has_value())
    {
        state.report.stackIdentity = (*stackIdentity)->artifactId();
    }
    an::Result<an::ExecutionContext> execution = startOperation(state.engine, state.stopToken, state.progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    const an::Result<an::RobustStatistics> statistics =
        an::computeImageRobustStatistics(*execution, stacked->descriptor.sampleType(), stacked->frame.bytes().data(),
                                         stacked->layout, 0u, an::StatisticsSelection{}, an::RobustStatisticsRequest{});
    if (!statistics.hasValue())
    {
        return statistics.status();
    }
    state.report.stackMedian = statistics->median;

    const an::Result<an::CodecImageInfo> drizzled = an::probeImage(
        state.engine, *execution,
        an::CodecReadRequest{.path = state.root + "/integration/light_drizzle.fits", .validatePayload = false});
    if (!drizzled.hasValue())
    {
        return drizzled.status();
    }
    state.report.drizzleWidth = drizzled->descriptor.extent().width();
    const an::Result<std::optional<an::ArtifactIdentity>> drizzleIdentity =
        an::artifactIdentityOf(drizzled->metadata.codec(), an::CodecNamespace::Fits);
    if (drizzleIdentity.hasValue() && drizzleIdentity->has_value())
    {
        state.report.drizzleIdentity = (*drizzleIdentity)->artifactId();
    }
    return an::Status::success();
}

/// The frame set's file names, in role order.
[[nodiscard]] std::vector<std::string> rolePaths(const std::string& root, const char* role, std::uint32_t count)
{
    std::vector<std::string> paths;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        paths.push_back(root + "/raw/" + role + "_" + std::to_string(index) + ".fits");
    }
    return paths;
}

/// The light at an index, with the drift and transparency every light of the set shows.
[[nodiscard]] SensorFrameRequest lightRequest(std::uint32_t index, std::uint64_t revision)
{
    return SensorFrameRequest{.width = kWidth,
                              .height = kHeight,
                              .kind = ExposureKind::Light,
                              .exposureSeconds = kLightExposure,
                              .shiftX = 1.5 * static_cast<double>(index),
                              .shiftY = -0.75 * static_cast<double>(index),
                              .transparency = 1.0 - 0.04 * static_cast<double>(index),
                              .seed = 100u + index + 1000u * revision};
}

} // namespace

const char* stageName(Stage stage) noexcept
{
    switch (stage)
    {
    case Stage::MasterBias:
        return "MasterBias";
    case Stage::MasterDark:
        return "MasterDark";
    case Stage::MasterFlat:
        return "MasterFlat";
    case Stage::CalibrateLights:
        return "CalibrateLights";
    case Stage::RegisterLights:
        return "RegisterLights";
    case Stage::AlignLights:
        return "AlignLights";
    case Stage::NormalizeLights:
        return "NormalizeLights";
    case Stage::StackLights:
        return "StackLights";
    case Stage::DrizzleLights:
        return "DrizzleLights";
    }
    return "UnknownStage";
}

Pipeline::Pipeline(std::string root, std::uint32_t lightCount)
    : m_root(std::move(root)), m_lightCount(lightCount), m_lightRevisions(lightCount, 0u)
{
}

an::Result<Pipeline> Pipeline::create(const an::EngineContext& engine, const std::string& root,
                                      std::uint32_t lightCount)
{
    if (lightCount < 3u)
    {
        return probeFailure(an::ErrorCode::InvalidArgument, "a pipeline needs at least three lights");
    }
    std::error_code error;
    for (const char* directory : {"raw", "masters", "calibrated", "registered", "integration"})
    {
        std::filesystem::create_directories(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(root.data()), root.size())) /
                directory,
            error);
        if (error)
        {
            return probeFailure(an::ErrorCode::ExternalFailure, "could not create the pipeline's directories");
        }
    }
    const std::array<std::pair<const char*, ExposureKind>, 3u> roles{
        std::pair<const char*, ExposureKind>{"bias", ExposureKind::Bias},
        std::pair<const char*, ExposureKind>{"dark", ExposureKind::Dark},
        std::pair<const char*, ExposureKind>{"flat", ExposureKind::Flat}};
    for (const auto& [role, kind] : roles)
    {
        const std::vector<std::string> paths = rolePaths(root, role, kCalibrationFrames);
        for (std::uint32_t index = 0u; index < kCalibrationFrames; ++index)
        {
            const an::Status written =
                writeSensorFrame(engine, paths[index],
                                 SensorFrameRequest{.width = kWidth,
                                                    .height = kHeight,
                                                    .kind = kind,
                                                    .exposureSeconds = kind == ExposureKind::Bias   ? 0.0
                                                                       : kind == ExposureKind::Flat ? kFlatExposure
                                                                                                    : kLightExposure,
                                                    .seed = static_cast<std::uint64_t>(kind) * 10u + index});
            if (written.failed())
            {
                return written;
            }
        }
    }
    const std::vector<std::string> lights = rolePaths(root, "light", lightCount);
    for (std::uint32_t index = 0u; index < lightCount; ++index)
    {
        const an::Status written = writeSensorFrame(engine, lights[index], lightRequest(index, 0u));
        if (written.failed())
        {
            return written;
        }
    }
    return Pipeline{root, lightCount};
}

an::Status Pipeline::rewriteLight(const an::EngineContext& engine, std::uint32_t index)
{
    if (index >= m_lightCount)
    {
        return probeFailure(an::ErrorCode::OutOfRange, "the pipeline has no such light");
    }
    ++m_lightRevisions[index];
    return writeSensorFrame(engine, rolePaths(m_root, "light", m_lightCount)[index],
                            lightRequest(index, m_lightRevisions[index]));
}

std::vector<std::string> Pipeline::files() const
{
    std::vector<std::string> names;
    std::error_code error;
    const std::filesystem::path base(
        std::u8string_view(reinterpret_cast<const char8_t*>(m_root.data()), m_root.size()));
    for (std::filesystem::recursive_directory_iterator entry(base, error), end; !error && entry != end;
         entry.increment(error))
    {
        if (entry->is_regular_file(error))
        {
            const std::u8string relative = entry->path().lexically_relative(base).generic_u8string();
            names.emplace_back(relative.begin(), relative.end());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string Pipeline::stackPath() const
{
    return m_root + "/integration/light_stack.fits";
}

const ArtifactRecord* Pipeline::artifact(const std::string& identity) const
{
    const auto found = m_artifacts.find(identity);
    return found == m_artifacts.end() ? nullptr : &found->second;
}

an::Result<PipelineReport> Pipeline::run(const an::EngineContext& engine, std::stop_token stopToken,
                                         PipelineObserver* observer)
{
    PipelineReport report;
    StageProgress progress(observer, report.engineStageNames);
    RunState state{
        .engine = engine, .stopToken = stopToken, .observer = observer, .progress = progress, .report = report};
    state.root = m_root;
    state.artifacts = &m_artifacts;
    state.biasPaths = rolePaths(m_root, "bias", kCalibrationFrames);
    state.darkPaths = rolePaths(m_root, "dark", kCalibrationFrames);
    state.flatPaths = rolePaths(m_root, "flat", kCalibrationFrames);
    state.lightPaths = rolePaths(m_root, "light", m_lightCount);

    an::Status status = masterStage(state, Stage::MasterBias, state.biasPaths, "bias", {}, state.biasIdentity);
    if (status.ok())
    {
        status =
            masterStage(state, Stage::MasterDark, state.darkPaths, "dark", {state.biasIdentity}, state.darkIdentity);
    }
    if (status.ok())
    {
        status = masterStage(state, Stage::MasterFlat, state.flatPaths, "flat",
                             {state.biasIdentity, state.darkIdentity}, state.flatIdentity);
    }
    if (status.ok())
    {
        status = calibrateLights(state);
    }
    if (status.ok())
    {
        status = registerLights(state, m_registrations);
    }
    if (status.ok())
    {
        status = alignLights(state);
    }
    if (status.ok())
    {
        status = normalizeLights(state, m_normalizations);
    }
    if (status.ok())
    {
        status = stackLights(state);
    }
    if (status.ok())
    {
        status = drizzleLights(state);
    }
    if (status.ok())
    {
        status = readProducts(state);
    }
    if (status.failed())
    {
        // The engine's failure names the operation that refused and not the stage that called it; the
        // workflow is the only thing that knows which stage it was in, so it says so, keeping the
        // engine's code and the engine's failure as the cause.
        const char* const stage =
            report.stages.empty() ? "before the first stage" : stageName(report.stages.back().stage);
        return an::Status::failure(status.code(),
                                   an::StatusDetail{.message = std::string("the pipeline stopped in ") + stage,
                                                    .operation = "probe::integration::Pipeline::run"},
                                   status);
    }
    return report;
}

} // namespace probe::integration
