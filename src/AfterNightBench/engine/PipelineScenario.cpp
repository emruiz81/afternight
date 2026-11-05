// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "PipelineScenario.h"

#include "PipelineStream.h"

#include "Digest.h"
#include "ProcessClock.h"

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethodRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::bench::engine
{
namespace
{

using Clock = std::chrono::steady_clock;

/// The most stars a frame contributes, and the fewest a registration may be fitted from.
///
/// Stated rather than left to the engine's defaults: a registration fitted from a different number
/// of stars is doing different work, and two runs of the row would measure two amounts of it.
constexpr std::uint32_t kMaxStars = 2000u;
constexpr std::uint32_t kMinStars = 10u;

double elapsedMs(const Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// Both clocks for the region just measured, read as one value so a stage cannot record half of it.
PipelineStageTiming stageTiming(const Clock::time_point started, const double cpuStarted)
{
    const double wallMs = elapsedMs(started);
    return PipelineStageTiming{wallMs, processCpuMilliseconds() - cpuStarted};
}

/// A stage failure, with the engine's own words rather than a summary of them.
std::string engineError(const std::string& what, const an::Status& status)
{
    return what + ": " + std::string(an::errorCodeName(status.code())) + " - " + status.message();
}

/// The paths of one role, in manifest order, trimmed to what the scenario asks for.
std::vector<std::string> rolePaths(const FixtureSet::Role& role, const ScenarioSpec& scenario, const char* name)
{
    const std::size_t count = pipelineRoleLimit(scenario, name, role.frames.size());
    std::vector<std::string> paths;
    paths.reserve(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        paths.push_back(role.frames[index].absolutePath);
    }
    return paths;
}

/// Exact zero is not a measurement in this row.
///
/// The frames this stack reads state the rule rather than inheriting a silent one. It is the same
/// rule the stacking rows declare.
std::optional<an::ValidityDescriptor> zeroIsNoData()
{
    return an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
}

/// One stack's frames, read a band at a time out of the files the engine decodes them from.
///
/// This is what keeps the pipeline's memory bounded: the executor asks for the rows of one tile of
/// one frame, and the source decodes exactly those rows into the memory the executor lends the read -
/// the frame's own band of the tile - through a row reader it holds open on that frame for the whole
/// stack, so the header is read once per frame and the system's read-ahead sees each file read in
/// order. Nothing holds a whole frame. Reads of different frames touch different readers and
/// different records, so the executor may run them together.
class StreamingFileStackSource final : public an::StackFrameSource
{
public:
    StreamingFileStackSource(const an::EngineContext& engine, std::span<const std::string> paths,
                             an::SampleType sampleType, const an::ValidityDescriptor& validity)
        : m_engine(engine), m_paths(paths), m_sampleType(sampleType), m_validity(validity), m_readers(paths.size()),
          m_layouts(paths.size())
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::ConcurrentRegion;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        if (frameIndex >= m_paths.size())
        {
            return an::Status::failure(an::ErrorCode::OutOfRange, "the pipeline source has no such frame");
        }
        an::ImageRowReader& reader = m_readers[frameIndex];
        if (!reader.valid())
        {
            // The container is the file's own, as its name states it: the stack streams FITS and XISF frames alike.
            an::Result<an::ImageRowReader> opened = an::ImageRowReader::open(
                m_engine, execution, {.path = m_paths[frameIndex], .format = an::CodecFormat::Unknown});
            if (!opened.hasValue())
            {
                return opened.status();
            }
            reader = std::move(*opened);
        }
        const an::Result<an::ImageLayout> layout = reader.readRows(execution, firstRow, rows, buffer);
        if (!layout.hasValue())
        {
            return layout.status();
        }
        m_layouts[frameIndex] = *layout;
        m_reads.fetch_add(1u, std::memory_order_relaxed);
        return an::StackSourceBand{.sampleType = m_sampleType,
                                   .samples = buffer.data(),
                                   .layout = &*m_layouts[frameIndex],
                                   .frameIndex = frameIndex,
                                   .firstRow = firstRow,
                                   .validity = an::StackBandValidity{.descriptor = m_validity, .mask = nullptr}};
    }

    [[nodiscard]] std::uint64_t reads() const noexcept
    {
        return m_reads.load(std::memory_order_relaxed);
    }

private:
    const an::EngineContext& m_engine;
    std::span<const std::string> m_paths;
    an::SampleType m_sampleType;
    an::ValidityDescriptor m_validity;
    std::vector<an::ImageRowReader> m_readers;
    std::vector<std::optional<an::ImageLayout>> m_layouts;
    std::atomic<std::uint64_t> m_reads{0u};
};

/// What one stack of the pipeline produced, kept alive so the next stage can read it.
struct StackedMaster
{
    std::optional<an::StackResult> result;
    std::optional<an::ImageDescriptor> descriptor;
    std::optional<an::ImageLayout> layout;
};

/// The stack every pipeline stack runs: the average of unrejected frames with no normalization, which
/// is what a master is.
std::optional<an::StackModel> pipelineStackModel(std::size_t frames, const an::ImageDescriptor& frameDescriptor,
                                                 std::string& error)
{
    an::StackRequest request{.input = an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(frames),
                                                                .descriptor = frameDescriptor},
                             .combination = an::StackCombination::Average};
    std::optional<an::StackModel> model = an::StackModel::create(request);
    if (!model.has_value())
    {
        error = std::string("the stack model refused a pipeline stack: ") +
                an::stackModelErrorName(an::StackModel::check(request));
    }
    return model;
}

/// Stacks a list of files, optionally calibrating every frame on the way in.
///
/// The stack `pipelineStackModel` states. `calibration` is empty for the master bias, one plan for
/// the master dark and the master flat, and the plan is prepared by the caller against the tile
/// description this returns. With a sink the stack hands its master to it tile by tile, and `out`
/// then holds the description and the report and no image.
bool stackFiles(const an::EngineContext& engine, const an::ExecutionContext& execution,
                std::span<const std::string> paths, const an::ImageDescriptor& frameDescriptor,
                const an::StackSourceContract& contract, StackedMaster& out, std::uint64_t& reads, std::string& error,
                an::StackOutputSink* sink = nullptr)
{
    if (paths.empty())
    {
        error = "a pipeline stack was given no frames";
        return false;
    }
    const std::optional<an::StackModel> model = pipelineStackModel(paths.size(), frameDescriptor, error);
    if (!model.has_value())
    {
        return false;
    }
    const std::optional<an::ValidityDescriptor> rule = zeroIsNoData();
    if (!rule.has_value())
    {
        error = "the pipeline could not state its validity rule";
        return false;
    }
    StreamingFileStackSource source(engine, paths, frameDescriptor.sampleType(), *rule);

    an::StackExecutionRequest run;
    // Every frame's band of one tile is resident at once. A stack that reads straight into the tile
    // folds it after the last band lands, so its tile is sized to stay in the cache between the two;
    // one that calibrates spreads each band's calibration over the workers, and keeps a band budget.
    // A tile also holds the rows one compressed piece of a frame spans, which a narrower one would decode again for
    // every tile that reaches it, as the product's lights stacking stage sizes it.
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(frameDescriptor.sampleType());
    const an::Result<an::ImageRowReader> first =
        an::ImageRowReader::open(engine, execution, {.path = paths.front(), .format = an::CodecFormat::Unknown});
    run.sourceGranularityRows = first.hasValue() ? first->decodeGranularityRows() : 1u;
    run.maximumTileRows =
        contract.calibration.empty()
            ? (std::max)(pipelineStackTileRows(paths.size(), frameDescriptor.extent().width(), elementBytes,
                                               execution.admittedConcurrency()),
                         run.sourceGranularityRows)
            : pipelineCalibratingStackTileRows(paths.size(), frameDescriptor.extent().width(), elementBytes);
    // A stack that does not calibrate reads each band into the tile and may read a worker's worth
    // together. One that calibrates widens every band through a read slot, so its reads stay one at a
    // time, and a second slot lets the next frame's band be read while this one is calibrated.
    run.concurrentFrameReads =
        contract.calibration.empty()
            ? (std::max)(1u, (std::min)(static_cast<std::uint32_t>(paths.size()), execution.admittedConcurrency()))
            : 2u;
    run.outputSink = sink;
    an::Result<an::StackResult> stacked =
        an::executeStack(engine, execution, *model, source, contract, run, an::StackExecutionOutputs{});
    if (!stacked.hasValue())
    {
        error = engineError("a pipeline stack failed", stacked.status());
        return false;
    }
    if (sink != nullptr)
    {
        reads += source.reads();
        out.descriptor = stacked->descriptor;
        out.result = std::move(stacked).value();
        return true;
    }
    const std::optional<an::ImageLayout> layout = stacked->image.layout();
    if (!layout.has_value())
    {
        error = "a pipeline stack handed over no image";
        return false;
    }
    reads += source.reads();
    out.descriptor = stacked->descriptor;
    out.layout = *layout;
    out.result = std::move(stacked).value();
    return true;
}

/// What every frame of the set is, read from its first light rather than assumed.
///
/// The stack model and every plan are built from this description, so a set that is not what the
/// manifest says fails here instead of being measured.
std::optional<an::ImageDescriptor> describeSet(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                               std::span<const std::string> lightPaths, std::string& error)
{
    const an::Result<an::CodecImageInfo> probed =
        an::probeImage(engine, execution, {.path = lightPaths.front(), .format = an::CodecFormat::Unknown});
    if (!probed.hasValue())
    {
        error = engineError("could not probe '" + lightPaths.front() + "'", probed.status());
        return std::nullopt;
    }
    std::optional<an::ImageDescriptor> descriptor = probed->descriptor.toImageDescriptor();
    if (!descriptor.has_value())
    {
        error = "the reference frame does not describe a resident image";
    }
    return descriptor;
}

/// One frame held as Float32 samples, with everything the operations that read it need.
struct FloatFrame
{
    an::SampleBuffer samples;

    void* data()
    {
        return samples.mutableBytes()->data();
    }
    std::optional<an::RawFrameDescriptor> raw;
    std::optional<an::ImageDescriptor> descriptor;
    std::optional<an::ImageLayout> layout;
};

/// The Float32 description of a decoded frame, keeping everything else it says about itself.
///
/// Only the representation changes. The numeric domain, channel schema, origin and pixel aspect are
/// the file's own facts and a pipeline that quietly restated one of them would be measuring a
/// different image from the one it opened.
bool describeAsFloat(const an::RawFrameDescriptor& source, const an::EngineContext& engine, FloatFrame& out,
                     std::string& error)
{
    an::RawFrameDescriptorRequest request = source.toRequest();
    request.sampleFormat = an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint};
    request.byteOrder = an::ByteOrder::Native;
    const std::optional<an::RawFrameDescriptor> raw = an::RawFrameDescriptor::create(request);
    const std::optional<an::ImageDescriptor> descriptor = raw.has_value() ? raw->toImageDescriptor() : std::nullopt;
    if (!raw.has_value() || !descriptor.has_value())
    {
        error = "the decoded frame has no Float32 description";
        return false;
    }
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create({.extent = descriptor->extent(),
                                 .elementSizeBytes = 4u,
                                 .samplesPerPixel = 1u,
                                 .planeCount = descriptor->extent().channelCount(),
                                 .rowAlignmentBytes = an::kDefaultBufferAlignmentBytes},
                                engine.limits());
    if (!layout.has_value())
    {
        error = "the Float32 frame has no layout the engine's limits admit";
        return false;
    }
    out.raw = *raw;
    out.descriptor = *descriptor;
    out.layout = *layout;
    if (out.samples.byteCount() != layout->totalBytes())
    {
        auto buffer = an::SampleBuffer::createOwned(engine, *layout, an::StorageInitialization::Zeroed);
        if (!buffer)
        {
            error = engineError("could not allocate a Float32 frame", buffer.status());
            return false;
        }
        out.samples = std::move(*buffer);
    }
    return true;
}

/// Releases the caller's ownership of a frame's context-accounted samples.
void releaseFrame(FloatFrame& frame)
{
    frame.samples = {};
}

/// Widens a decoded integer frame into Float32 samples through the engine's conversion.
bool widenToFloat(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::RawFrame& decoded,
                  FloatFrame& out, std::string& error)
{
    const std::optional<an::RawFrameDescriptor> raw = decoded.descriptor();
    const std::optional<an::ImageLayout> layout = decoded.layout();
    const std::optional<an::SampleType> sampleType = raw.has_value() ? raw->residentSampleType() : std::nullopt;
    const std::optional<an::ImageDescriptor> descriptor = raw.has_value() ? raw->toImageDescriptor() : std::nullopt;
    if (!raw.has_value() || !layout.has_value() || !sampleType.has_value() || !descriptor.has_value())
    {
        error = "a decoded frame does not describe itself as a resident image";
        return false;
    }
    if (!describeAsFloat(*raw, engine, out, error))
    {
        return false;
    }
    if (*sampleType == an::SampleType::Float32)
    {
        const std::span<const std::byte> bytes = decoded.bytes();
        std::memcpy(out.data(), bytes.data(), static_cast<std::size_t>(out.layout->totalBytes()));
        return true;
    }
    // The engine's own rule for the frame's numeric domain, not the default policy. A sensor code
    // widens to the number it is; rescaling it by a full-scale ratio would invent a calibration
    // nobody performed - and would put this frame on a different scale from the masters, which the
    // stack widens by the same rule.
    an::ConversionCounters counters;
    const an::Status converted = an::convertImageSamples(
        execution, *sampleType, decoded.bytes().data(), *layout, an::SampleType::Float32, out.data(), *out.layout,
        an::conversionPolicyFor(descriptor->numericDomain()), counters);
    if (converted.failed())
    {
        error = engineError("could not widen a decoded frame", converted);
        return false;
    }
    return true;
}

/// The inverse of a row-major homogeneous three-by-three.
///
/// The fitted transform maps this frame's coordinates onto the reference's; a warp is told where
/// each destination pixel comes from, which is the other direction.
std::optional<std::array<double, 9>> invert(const std::array<double, 9>& m)
{
    const double a = m[4] * m[8] - m[5] * m[7];
    const double b = m[5] * m[6] - m[3] * m[8];
    const double c = m[3] * m[7] - m[4] * m[6];
    const double determinant = m[0] * a + m[1] * b + m[2] * c;
    if (!std::isfinite(determinant) || determinant == 0.0)
    {
        return std::nullopt;
    }
    const double scale = 1.0 / determinant;
    std::array<double, 9> inverse{a * scale, (m[2] * m[7] - m[1] * m[8]) * scale, (m[1] * m[5] - m[2] * m[4]) * scale,
                                  b * scale, (m[0] * m[8] - m[2] * m[6]) * scale, (m[2] * m[3] - m[0] * m[5]) * scale,
                                  c * scale, (m[1] * m[6] - m[0] * m[7]) * scale, (m[0] * m[4] - m[1] * m[3]) * scale};
    for (const double value : inverse)
    {
        if (!std::isfinite(value))
        {
            return std::nullopt;
        }
    }
    return inverse;
}

/// The largest value the frames' stored representation holds, in the code values they widen to.
double sampleTypeFullScale(an::SampleType sampleType)
{
    switch (sampleType)
    {
    case an::SampleType::UInt8:
    case an::SampleType::Int8:
        return 255.0;
    case an::SampleType::UInt16:
    case an::SampleType::Int16:
        return 65535.0;
    case an::SampleType::UInt32:
    case an::SampleType::Int32:
        return 4294967295.0;
    default:
        break;
    }
    return 1.0;
}

/// The star positions of one frame, brightest first, as the matcher wants them.
std::vector<an::StarAlignmentPoint> alignmentPoints(const an::StarDetectionResult& detected)
{
    std::vector<an::StarAlignmentPoint> points;
    points.reserve(detected.stars.size());
    for (const an::DetectedStar& star : detected.stars)
    {
        points.push_back(an::StarAlignmentPoint{star.positionX, star.positionY});
    }
    return points;
}

/// Digests a stage's stacked plane and hands it to `hooks.afterClock`.
/// Hands a stacked plane, wherever it sits, to the row's check: digested and, as tight rows, to the after-clock hook.
bool handOverPlane(std::optional<an::SampleType> sampleType, const an::ImageLayout& layout, const void* samples,
                   std::span<const std::string> paths, const StageClockHooks& hooks, const char* what,
                   std::string& digest, std::string& error)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    if (sampleType != an::SampleType::Float32 || layout.extent().channelCount() != 1u)
    {
        error = std::string(what) + " is not one plane of Float32 values";
        return false;
    }
    digest = Sha256::of(samples, static_cast<std::size_t>(layout.totalBytes()));
    // The plane as tight rows: the image's own when its rows carry no padding, and a copy otherwise.
    const auto* image = static_cast<const std::byte*>(samples);
    std::vector<float> tight;
    std::span<const float> plane;
    if (layout.rowStrideBytes() == static_cast<std::uint64_t>(width) * sizeof(float))
    {
        plane = std::span<const float>(reinterpret_cast<const float*>(image), static_cast<std::size_t>(width) * height);
    }
    else
    {
        tight.resize(static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            std::memcpy(tight.data() + static_cast<std::size_t>(y) * width,
                        image + static_cast<std::size_t>(y) * layout.rowStrideBytes(), width * sizeof(float));
        }
        plane = tight;
    }
    if (hooks.afterClock)
    {
        hooks.afterClock(paths, plane);
    }
    return true;
}

bool handOverStagePlane(const StackedMaster& master, std::span<const std::string> paths, const StageClockHooks& hooks,
                        const char* what, std::string& digest, std::string& error)
{
    return handOverPlane(master.descriptor->sampleType(), *master.layout, master.result->image.data(), paths, hooks,
                         what, digest, error);
}

/// Hands each finished tile of a master to the output transaction that reserved it.
class ReservedMasterSink final : public an::StackOutputSink
{
public:
    explicit ReservedMasterSink(an::OutputTransaction& transaction) noexcept : m_transaction(transaction) {}

    [[nodiscard]] an::Status write(const an::ExecutionContext&, const an::StackOutputRows& rows) override
    {
        return m_transaction.writeRows(rows.firstRow, *rows.layout, rows.image);
    }

private:
    an::OutputTransaction& m_transaction;
};

} // namespace

bool runIntegrationPipeline(const an::EngineContext& engine, const an::ExecutionContext& execution,
                            const FixtureSet& frames, const ScenarioSpec& scenario, const std::string& scratchRoot,
                            PipelineStageTimings& timings, std::string& digest, std::string& error)
{
    const FixtureSet::Role* lights = frames.role("lights");
    const FixtureSet::Role* flats = frames.role("flats");
    const FixtureSet::Role* darks = frames.role("darks");
    const FixtureSet::Role* bias = frames.role("bias");
    if (lights == nullptr || flats == nullptr || darks == nullptr || bias == nullptr)
    {
        error = "the frame set needs lights, flats, darks and bias roles";
        return false;
    }

    const std::filesystem::path root(scratchRoot);
    const std::filesystem::path calibratedDir = root / "calibrated";
    const std::filesystem::path alignedDir = root / "aligned";
    std::error_code status;
    std::filesystem::create_directories(calibratedDir, status);
    std::filesystem::create_directories(alignedDir, status);
    if (status)
    {
        error = "could not create the pipeline scratch directories under '" + scratchRoot + "'";
        return false;
    }

    const std::vector<std::string> biasPaths = rolePaths(*bias, scenario, "bias");
    const std::vector<std::string> darkPaths = rolePaths(*darks, scenario, "darks");
    const std::vector<std::string> flatPaths = rolePaths(*flats, scenario, "flats");
    const std::vector<std::string> lightPaths = rolePaths(*lights, scenario, "lights");
    if (biasPaths.empty() || darkPaths.empty() || flatPaths.empty() || lightPaths.empty())
    {
        error = "the frame set selects no frame for one of its four roles";
        return false;
    }

    const std::optional<an::ImageDescriptor> rawDescriptor = describeSet(engine, execution, lightPaths, error);
    if (!rawDescriptor.has_value())
    {
        return false;
    }
    const double fullScale = sampleTypeFullScale(rawDescriptor->sampleType());
    std::uint64_t frameReads = 0u;

    // Stage 1: the master bias. Nothing calibrates it, so it is a plain stack.
    auto started = Clock::now();
    double cpuStarted = processCpuMilliseconds();
    StackedMaster masterBias;
    const bool biasStacked = stackFiles(engine, execution, biasPaths, *rawDescriptor, an::StackSourceContract{},
                                        masterBias, frameReads, error);
    timings.masterBias = stageTiming(started, cpuStarted);
    if (!biasStacked)
    {
        return false;
    }

    // The description every calibrated band is widened to, which is what a plan is prepared against.
    an::StackRequest tileRequest{
        .input = an::StackInputDescription{.frameCount = static_cast<std::uint32_t>(darkPaths.size()),
                                           .descriptor = *rawDescriptor},
        .combination = an::StackCombination::Average};
    const std::optional<an::StackModel> tileModel = an::StackModel::create(tileRequest);
    if (!tileModel.has_value())
    {
        error = "the stack model refused the calibration description";
        return false;
    }
    const an::ImageDescriptor tileDescriptor = an::stackSourceCalibrationDescriptor(*tileModel);
    const an::Result<an::CalibrationFrameView> biasMaster = an::CalibrationFrameView::create(
        masterBias.result->image.data(), masterBias.layout->totalBytes(), *masterBias.layout, tileDescriptor);
    if (!biasMaster.hasValue())
    {
        error = engineError("the engine refused the master bias as a calibration master", biasMaster.status());
        return false;
    }

    // Stage 2: the master dark, bias-subtracted as it stacks.
    //
    // The plan is prepared against the master bias rather than against a light. With a fixed dark
    // scale and no flat it holds nothing but scalar decisions and the description it was prepared
    // for, so any frame of that description prepares the same plan - and the master bias is the one
    // frame of it this stage already has.
    const an::CalibrationMasters darkMasters{.bias = *biasMaster};
    const an::CalibrationRequest calibrationRequest{.pedestal = 0.0,
                                                    .darkScaling = an::DarkScalingMode::Fixed,
                                                    .darkScale = 1.0,
                                                    .flatNormalization = an::FlatNormalizationMode::None};
    const an::Result<an::CalibrationPlan> darkPlan =
        an::prepareImageCalibration(execution, *biasMaster, darkMasters, calibrationRequest);
    if (!darkPlan.hasValue())
    {
        error = engineError("the engine refused the master dark's calibration", darkPlan.status());
        return false;
    }
    const std::array<an::CalibrationPlan, 1u> darkPlans{*darkPlan};
    an::StackSourceContract darkContract{
        .state = an::StackFrameState::Raw, .calibration = darkPlans, .masters = &darkMasters};
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    StackedMaster masterDark;
    const bool darkStacked =
        stackFiles(engine, execution, darkPaths, *rawDescriptor, darkContract, masterDark, frameReads, error);
    timings.masterDark = stageTiming(started, cpuStarted);
    if (!darkStacked)
    {
        return false;
    }
    const an::Result<an::CalibrationFrameView> darkMaster = an::CalibrationFrameView::create(
        masterDark.result->image.data(), masterDark.layout->totalBytes(), *masterDark.layout, tileDescriptor);
    if (!darkMaster.hasValue())
    {
        error = engineError("the engine refused the master dark as a calibration master", darkMaster.status());
        return false;
    }

    // Stage 3: the master flat, bias- and dark-corrected as it stacks.
    const an::CalibrationMasters flatMasters{.bias = *biasMaster, .dark = *darkMaster};
    const an::Result<an::CalibrationPlan> flatPlan =
        an::prepareImageCalibration(execution, *biasMaster, flatMasters, calibrationRequest);
    if (!flatPlan.hasValue())
    {
        error = engineError("the engine refused the master flat's calibration", flatPlan.status());
        return false;
    }
    const std::array<an::CalibrationPlan, 1u> flatPlans{*flatPlan};
    an::StackSourceContract flatContract{
        .state = an::StackFrameState::Raw, .calibration = flatPlans, .masters = &flatMasters};
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    StackedMaster masterFlat;
    const bool flatStacked =
        stackFiles(engine, execution, flatPaths, *rawDescriptor, flatContract, masterFlat, frameReads, error);
    timings.masterFlat = stageTiming(started, cpuStarted);
    if (!flatStacked)
    {
        return false;
    }
    const an::Result<an::CalibrationFrameView> flatMaster = an::CalibrationFrameView::create(
        masterFlat.result->image.data(), masterFlat.layout->totalBytes(), *masterFlat.layout, tileDescriptor);
    if (!flatMaster.hasValue())
    {
        error = engineError("the engine refused the master flat as a calibration master", flatMaster.status());
        return false;
    }

    // Stage 4: bounded decode/compute/encode overlap, followed by atomic publication of the
    // complete calibrated files. Every stage runs inside the execution's existing thread grant.
    const an::CalibrationMasters lightMasters{.bias = *biasMaster, .dark = *darkMaster, .flat = *flatMaster};
    const an::CalibrationRequest lightRequest{.pedestal = 0.0,
                                              .darkScaling = an::DarkScalingMode::Fixed,
                                              .darkScale = 1.0,
                                              .flatNormalization = an::FlatNormalizationMode::CentralMean};
    std::vector<std::string> calibratedPaths;
    calibratedPaths.reserve(lightPaths.size());
    for (std::size_t index = 0u; index < lightPaths.size(); ++index)
        calibratedPaths.push_back((calibratedDir / ("light_" + std::to_string(index) + "_cal.fits")).string());
    std::optional<an::CalibrationPlan> lightPlan;
    FloatFrame light;
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    auto calibrate = [&](const an::EngineContext& owner, const an::ExecutionContext& operation,
                         an::SequenceFrame frame) -> an::Result<an::SequenceFrame> {
        if (!widenToFloat(owner, operation, frame.frame, light, error))
            return an::Status::failure(an::ErrorCode::ExternalFailure, error.c_str());
        frame.frame.release();
        const auto view = an::CalibrationFrameView::create(light.data(), light.layout->totalBytes(), *light.layout,
                                                           *light.descriptor);
        if (!view)
            return view.status();
        if (!lightPlan)
        {
            auto prepared = an::prepareImageCalibration(operation, *view, lightMasters, lightRequest);
            if (!prepared)
                return prepared.status();
            lightPlan = std::move(*prepared);
        }
        FloatFrame calibrated;
        if (!describeAsFloat(*light.raw, owner, calibrated, error))
            return an::Status::failure(an::ErrorCode::ExternalFailure, error.c_str());
        const auto ran = an::calibrateImage(operation, *lightPlan, *view, lightMasters, calibrated.data(),
                                            calibrated.layout->totalBytes(), *calibrated.layout);
        if (!ran)
            return ran.status();
        auto raw = an::RawFrame::create(*calibrated.raw, *calibrated.layout, std::move(calibrated.samples));
        if (!raw)
            return raw.status();
        frame.frame = std::move(*raw);
        return frame;
    };
    an::FrameStreamCounters calibrationCounters;
    const auto calibratedStatus =
        streamPipelineFiles(engine, execution, lightPaths, calibratedPaths, calibrate, calibrationCounters);
    timings.calibrateLights = stageTiming(started, cpuStarted);
    if (calibratedStatus.failed())
    {
        error = engineError("could not stream the light calibration", calibratedStatus);
        return false;
    }
    timings.lightsCalibrated = calibrationCounters.framesWritten;

    // Nothing after this reads a master or a light in memory: the calibrated frames are on disk and
    // every later stage opens them. Half a gigabyte of masters held through the stack would sit in
    // the pipeline's peak without being read, which is the figure this row's ceiling is stated on.
    releaseFrame(light);
    lightPlan.reset();
    masterBias.result.reset();
    masterDark.result.reset();
    masterFlat.result.reset();

    // Stage 5: detect stars on the calibrated lights. They are read through the same bounded stream
    // as the calibration's and the alignment's inputs, so the next frame decodes while this one's
    // stars are found; decoded inline, a frame's read was a quarter of the stage.
    std::vector<std::vector<an::StarAlignmentPoint>> fields;
    fields.reserve(calibratedPaths.size());
    an::StarDetectionRequest detection;
    detection.maximumStars = kMaxStars;
    // What full scale is on these frames, stated rather than defaulted. The samples are the sensor's
    // own codes widened to Float32, so full scale is the largest code the camera's representation
    // holds; leaving the unit-interval default in place would make every star look saturated.
    detection.fullScale = fullScale;
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    auto detect = [&](const an::EngineContext&, const an::ExecutionContext& operation,
                      an::SequenceFrame frame) -> an::Result<an::SequenceFrame> {
        const std::optional<an::RawFrameDescriptor> raw = frame.frame.descriptor();
        const std::optional<an::ImageLayout> layout = frame.frame.layout();
        const std::optional<an::ImageDescriptor> described = raw.has_value() ? raw->toImageDescriptor() : std::nullopt;
        if (!layout.has_value() || !described.has_value())
            return an::Status::failure(an::ErrorCode::InvalidData,
                                       "a calibrated frame does not describe itself as a resident image");
        const an::Result<an::StarFrameView> view =
            an::StarFrameView::create(frame.frame.bytes().data(), frame.frame.bytes().size(), *layout, *described);
        if (!view)
            return view.status();
        const an::Result<an::StarDetectionPlan> plan = an::planStarDetection(detection, *described, *layout);
        if (!plan)
            return plan.status();
        an::StarDetectionResult detected;
        if (const an::Status ran = an::detectStars(operation, *view, *plan, detected); ran.failed())
            return ran;
        timings.peakDetectionWorkingBytes =
            (std::max)(timings.peakDetectionWorkingBytes, detected.report.peakWorkingBytes);
        if (fields.empty())
        {
            // The reference frame's field, described in the failure the registration would
            // otherwise give without saying what it was handed. A pipeline that detected nothing
            // has a calibration problem, not a matching one, and the two are told apart here.
            timings.referenceStars = detected.stars.size();
            timings.referenceBackground = detected.summary.channel.background;
            timings.referenceBackgroundNoise = detected.summary.channel.backgroundNoise;
        }
        fields.push_back(alignmentPoints(detected));
        return frame;
    };
    an::FrameStreamCounters detectionCounters;
    const auto detectedStatus = streamPipelineFiles(engine, execution, calibratedPaths, {}, detect, detectionCounters);
    timings.detectStars = stageTiming(started, cpuStarted);
    if (detectedStatus.failed())
    {
        error = engineError("could not stream the star detection", detectedStatus);
        return false;
    }
    if (fields.size() != calibratedPaths.size())
    {
        error = "star detection visited " + std::to_string(fields.size()) + " of " +
                std::to_string(calibratedPaths.size()) + " calibrated lights";
        return false;
    }

    // Stage 6: match the detected stars into transforms.
    //
    // The reference is named rather than left to the engine's own choice: a benchmark whose
    // reference frame is selected by a heuristic
    // registers a different subset from run to run, and its wall time then describes a different
    // amount of work each time. The first light is the reference because it is the one choice that
    // does not depend on the data.
    an::StarAlignmentRequest alignment;
    alignment.model = an::StarTransformModel::Projective;
    alignment.minimumMatches = kMinStars;
    std::vector<std::optional<an::StarTransform>> transforms(fields.size());
    std::vector<double> residuals;
    residuals.reserve(fields.size());
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    const an::Result<an::PreparedStarAlignmentReference> reference =
        an::prepareStarAlignmentReference(execution, fields.front(), alignment);
    if (!reference.hasValue())
    {
        error = engineError("the engine could not prepare the reference field of " +
                                std::to_string(timings.referenceStars) + " stars over a background of " +
                                std::to_string(timings.referenceBackground) + " plus or minus " +
                                std::to_string(timings.referenceBackgroundNoise),
                            reference.status());
        timings.registerStars = stageTiming(started, cpuStarted);
        return false;
    }
    transforms.front() = an::StarTransform{.model = an::StarTransformModel::Projective};
    std::size_t registered = 1u;
    for (std::size_t index = 1u; index < fields.size(); ++index)
    {
        const an::Result<an::StarAlignmentResult> matched = an::alignStarFields(execution, fields[index], *reference);
        if (!matched.hasValue())
        {
            continue;
        }
        transforms[index] = matched->transform;
        residuals.push_back(matched->quality.rootMeanSquareResidual);
        ++registered;
    }
    timings.registerStars = stageTiming(started, cpuStarted);
    timings.lightsRegistered = registered;
    timings.residuals = summarizePipelineResiduals(residuals);

    // Stage 7: resample every registered frame, overlapping bounded reads and private writes.
    std::vector<std::string> alignmentInputs;
    std::vector<std::string> alignedPaths;
    std::vector<std::array<double, 9>> matrices;
    for (std::size_t index = 0u; index < calibratedPaths.size(); ++index)
    {
        if (!transforms[index])
            continue;
        const auto inverse = invert(transforms[index]->sourceToReference);
        if (!inverse)
            continue;
        alignmentInputs.push_back(calibratedPaths[index]);
        alignedPaths.push_back((alignedDir / ("light_" + std::to_string(index) + "_reg.fits")).string());
        matrices.push_back(*inverse);
    }
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    auto align = [&](const an::EngineContext& owner, const an::ExecutionContext& operation,
                     an::SequenceFrame frame) -> an::Result<an::SequenceFrame> {
        const auto raw = frame.frame.descriptor();
        const auto sourceLayout = frame.frame.layout();
        const auto source = raw ? raw->toImageDescriptor() : std::nullopt;
        if (!sourceLayout || !source)
            return an::Status::failure(an::ErrorCode::InvalidData);
        FloatFrame warped;
        if (!describeAsFloat(*raw, owner, warped, error))
            return an::Status::failure(an::ErrorCode::ExternalFailure, error.c_str());
        const auto plan = an::planProjectiveWarp(
            *source, {.width = source->extent().width(),
                      .height = source->extent().height(),
                      .destinationToSource = matrices[static_cast<std::size_t>(frame.position.index)],
                      .destinationPixelAspectRatio = source->pixelAspectRatio(),
                      .sampling = {.interpolation = an::KernelInterpolation::Cubic,
                                   .border = an::KernelBorderMode::Constant,
                                   .invalid = an::SamplingInvalidPolicy::RequireAll}});
        if (!plan)
            return plan.status();
        auto mask = an::ValidityMask::create(owner, warped.descriptor->extent());
        if (!mask)
            return mask.status();
        const auto ran =
            an::transformImageGeometry(operation, an::SampleType::Float32, frame.frame.bytes().data(), *sourceLayout,
                                       warped.data(), *warped.layout, *plan, {}, *mask->view());
        if (!ran)
            return ran.status();
        timings.peakAlignmentWorkingBytes = (std::max)(timings.peakAlignmentWorkingBytes, ran->workingBytes);
        auto output = an::RawFrame::create(*warped.raw, *warped.layout, std::move(warped.samples));
        if (!output)
            return output.status();
        frame.frame = std::move(*output);
        return frame;
    };
    an::FrameStreamCounters alignmentCounters;
    const auto alignedStatus =
        streamPipelineFiles(engine, execution, alignmentInputs, alignedPaths, align, alignmentCounters);
    timings.align = stageTiming(started, cpuStarted);
    if (alignedStatus.failed())
    {
        error = engineError("could not stream the alignment", alignedStatus);
        return false;
    }
    timings.lightsAligned = alignmentCounters.framesWritten;

    // Not merely "produced something". A pipeline that stacked a third of its lights ran different
    // work from one that stacked all of them, so its wall time is not a faster version of the same
    // measurement and must not be recorded as one. The counts say where the frames went.
    if (alignedPaths.size() != lightPaths.size())
    {
        error = "alignment kept " + std::to_string(alignedPaths.size()) + " of " + std::to_string(lightPaths.size()) +
                " lights (" + std::to_string(timings.lightsCalibrated) + " calibrated, " +
                std::to_string(timings.lightsRegistered) +
                " registered); a partial pipeline is not a smaller measurement of the same work";
        return false;
    }

    // Stage 8: stack the aligned lights. This is the frame the whole pipeline exists to produce.
    started = Clock::now();
    cpuStarted = processCpuMilliseconds();
    StackedMaster stacked;
    const bool lightsStacked = stackFiles(engine, execution, alignedPaths, tileDescriptor, an::StackSourceContract{},
                                          stacked, frameReads, error);
    timings.stack = stageTiming(started, cpuStarted);
    if (!lightsStacked)
    {
        return false;
    }

    // The same whole-payload digest every other scenario uses, so a pipeline that produces a
    // different image cannot pass as a faster one.
    timings.lightsStacked = alignedPaths.size();
    timings.framesProcessed = lightPaths.size() + flatPaths.size() + darkPaths.size() + biasPaths.size();
    digest = Sha256::of(stacked.result->image.data(), static_cast<std::size_t>(stacked.layout->totalBytes()));
    if (digest.empty())
    {
        error = "the stacked image could not be digested";
        return false;
    }
    return true;
}

bool runMasterBiasStage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                        const FixtureSet& frames, const ScenarioSpec& scenario, const StageClockHooks& hooks,
                        PipelineStageTiming& timing, std::uint64_t& frameReads, std::string& digest, std::string& error)
{
    const FixtureSet::Role* lights = frames.role("lights");
    const FixtureSet::Role* bias = frames.role("bias");
    if (lights == nullptr || bias == nullptr)
    {
        error = "the frame set needs lights and bias roles";
        return false;
    }
    const std::vector<std::string> biasPaths = rolePaths(*bias, scenario, "bias");
    const std::vector<std::string> lightPaths = rolePaths(*lights, scenario, "lights");
    if (biasPaths.empty() || lightPaths.empty())
    {
        error = "the frame set selects no bias frame or no light";
        return false;
    }
    // What the pipeline reads before its first stage, read the same way and just as untimed.
    const std::optional<an::ImageDescriptor> rawDescriptor = describeSet(engine, execution, lightPaths, error);
    if (!rawDescriptor.has_value())
    {
        return false;
    }
    if (hooks.beforeClock && !hooks.beforeClock(biasPaths, error))
    {
        return false;
    }

    const auto started = Clock::now();
    const double cpuStarted = processCpuMilliseconds();
    StackedMaster master;
    const bool stacked =
        stackFiles(engine, execution, biasPaths, *rawDescriptor, an::StackSourceContract{}, master, frameReads, error);
    timing = stageTiming(started, cpuStarted);
    if (!stacked)
    {
        return false;
    }

    return handOverStagePlane(master, biasPaths, hooks, "the master bias", digest, error);
}

bool runFinalStackStage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                        const FixtureSet& frames, const ScenarioSpec& scenario, const StageClockHooks& hooks,
                        PipelineStageTiming& timing, std::uint64_t& frameReads, std::string& digest, std::string& error)
{
    const FixtureSet::Role* lights = frames.role("lights");
    if (lights == nullptr)
    {
        error = "the aligned frame set needs a lights role";
        return false;
    }
    const std::vector<std::string> alignedPaths = rolePaths(*lights, scenario, "lights");
    if (alignedPaths.empty())
    {
        error = "the aligned frame set selects no light";
        return false;
    }
    // The pipeline hands its stack the description it widened every calibrated band to, and these
    // files are those bands as its alignment wrote them, so the first one says what that is.
    const std::optional<an::ImageDescriptor> descriptor = describeSet(engine, execution, alignedPaths, error);
    if (!descriptor.has_value())
    {
        return false;
    }
    if (hooks.beforeClock && !hooks.beforeClock(alignedPaths, error))
    {
        return false;
    }

    const auto started = Clock::now();
    const double cpuStarted = processCpuMilliseconds();
    StackedMaster stacked;
    const bool completed =
        stackFiles(engine, execution, alignedPaths, *descriptor, an::StackSourceContract{}, stacked, frameReads, error);
    timing = stageTiming(started, cpuStarted);
    if (!completed)
    {
        return false;
    }
    return handOverStagePlane(stacked, alignedPaths, hooks, "the final stack", digest, error);
}

namespace
{
/// A path as the UTF-8 the engine takes.
std::string utf8Of(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

/// The final stack over the copies with its master written into a reserved XISF image as it is combined, and,
/// untimed, the same stack handing its master over whole and then writing it.
bool stackIntoReservedMaster(const an::EngineContext& engine, const an::ExecutionContext& execution,
                             std::span<const std::string> copies, const an::ImageDescriptor& descriptor,
                             const std::string& scratchRoot, const StageClockHooks& onCopies,
                             PipelineStageTiming& timing, XisfStackStreamFigures& figures, std::uint64_t& frameReads,
                             std::string& digest, std::string& error)
{
    const std::optional<an::StackModel> model = pipelineStackModel(copies.size(), descriptor, error);
    if (!model.has_value())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> master =
        an::stackMethodOutputDescriptor(*model, an::kDefaultStackSumDomain);
    const std::optional<an::RawFrameDescriptor> raw =
        master.has_value() ? an::RawFrameDescriptor::fromImageDescriptor(*master) : std::nullopt;
    const an::Result<an::OutputIntent> intent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    if (!raw.has_value() || !intent.hasValue())
    {
        error = "the master has no description or intent a reservation can take";
        return false;
    }
    figures.reservedMaster = true;
    // The conventional path, untimed: the master handed over whole, then written in one call.
    {
        const auto started = Clock::now();
        StackedMaster whole;
        std::uint64_t reads = 0u;
        if (!stackFiles(engine, execution, copies, descriptor, an::StackSourceContract{}, whole, reads, error))
        {
            return false;
        }
        figures.wholePeakHeldBytes = whole.result->report.memory.peakHeldBytes;
        figures.wholePlannedPeakBytes = whole.result->report.plan.memory.peakBytes;
        an::Result<an::ImageStorage<float>> storage =
            an::ImageStorage<float>::fromBytes(std::move(whole.result->image));
        an::Result<an::Image<float>> image = storage.hasValue()
                                                 ? an::Image<float>::fromStorage(std::move(*storage), *whole.descriptor)
                                                 : an::Result<an::Image<float>>(storage.status());
        const an::Result<an::RawFrame> frame =
            image.hasValue() ? an::borrowImageFrame(*image) : an::Result<an::RawFrame>(image.status());
        if (!frame.hasValue())
        {
            error = engineError("the whole master could not be lent to a writer", frame.status());
            return false;
        }
        const an::Result<an::OutputReport> written =
            an::writeImage(engine, execution,
                           {.destinationPath = utf8Of(std::filesystem::path(scratchRoot) / "xisf-master-whole.xisf"),
                            .intent = &*intent,
                            .durability = an::OutputDurability::CloseOnly},
                           *frame, {});
        if (!written.hasValue())
        {
            error = engineError("the whole master could not be written", written.status());
            return false;
        }
        figures.wholeThenWriteMs = elapsedMs(started);
    }
    // The row's storage state again, which the conventional run just read through.
    if (onCopies.beforeClock && !onCopies.beforeClock(copies, error))
    {
        return false;
    }

    const std::string destination = utf8Of(std::filesystem::path(scratchRoot) / "xisf-master-reserved.xisf");
    const auto started = Clock::now();
    const double cpuStarted = processCpuMilliseconds();
    an::Result<an::OutputTransaction> transaction = an::OutputTransaction::begin(
        engine, execution,
        {.destinationPath = destination, .intent = &*intent, .durability = an::OutputDurability::CloseOnly});
    if (!transaction.hasValue())
    {
        error = engineError("the master's output transaction was refused", transaction.status());
        return false;
    }
    if (const an::Status reserved = transaction->reserve(*raw, an::StorageArrangement::Planar, {}); reserved.failed())
    {
        error = engineError("the master could not be reserved", reserved);
        return false;
    }
    ReservedMasterSink sink(*transaction);
    StackedMaster stacked;
    if (!stackFiles(engine, execution, copies, descriptor, an::StackSourceContract{}, stacked, frameReads, error,
                    &sink))
    {
        return false;
    }
    const an::Result<an::OutputReport> committed = transaction->commit();
    timing = stageTiming(started, cpuStarted);
    if (!committed.hasValue())
    {
        error = engineError("the reserved master could not be published", committed.status());
        return false;
    }
    figures.peakHeldBytes = stacked.result->report.memory.peakHeldBytes;
    figures.plannedPeakBytes = stacked.result->report.plan.memory.peakBytes;
    figures.masterBytes = committed->bytesWritten;
    // The plane, read back from the published unit outside the clock.
    const an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(engine, execution, {.path = destination, .readMetadata = false});
    if (!decoded.hasValue())
    {
        error = engineError("the reserved master could not be read back", decoded.status());
        return false;
    }
    return handOverPlane(decoded->frame.descriptor()->residentSampleType(), *decoded->frame.layout(),
                         decoded->frame.bytes().data(), copies, onCopies, "the reserved XISF master", digest, error);
}
} // namespace

bool runXisfStackStreamStage(const an::EngineContext& engine, const an::ExecutionContext& execution,
                             const FixtureSet& frames, const ScenarioSpec& scenario, const std::string& scratchRoot,
                             const StageClockHooks& hooks, PipelineStageTiming& timing, XisfStackStreamFigures& figures,
                             std::uint64_t& frameReads, std::string& digest, std::string& error)
{
    const FixtureSet::Role* lights = frames.role("lights");
    if (lights == nullptr)
    {
        error = "the aligned frame set needs a lights role";
        return false;
    }
    const std::vector<std::string> alignedPaths = rolePaths(*lights, scenario, "lights");
    if (alignedPaths.empty())
    {
        error = "the aligned frame set selects no light";
        return false;
    }
    const auto compression = scenario.parameters.find("compression");
    if (compression == scenario.parameters.end() || (compression->second != "None" && compression->second != "Zstd"))
    {
        error = "the row names a compression this runner does not write";
        return false;
    }
    // Zstandard in the form the writer gives it when nothing more is stated: shuffled, in its own subblocks.
    const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
        {.sampleType = an::SampleType::Float32,
         .compression = compression->second == "Zstd" ? an::OutputCompression::Zstd : an::OutputCompression::None});
    if (!intent.hasValue())
    {
        error = engineError("the XISF copies' intent was refused", intent.status());
        return false;
    }
    // The copies: each light decoded from its FITS file and written as XISF, nothing of it timed.
    std::vector<std::string> copies;
    copies.reserve(alignedPaths.size());
    for (std::size_t index = 0u; index < alignedPaths.size(); ++index)
    {
        const an::Result<an::CodecDecodeResult> decoded = an::decodeImage(
            engine, execution, {.path = alignedPaths[index], .format = an::CodecFormat::Fits, .readMetadata = false});
        if (!decoded.hasValue())
        {
            error = engineError("could not read '" + alignedPaths[index] + "'", decoded.status());
            return false;
        }
        const auto target = std::filesystem::path(scratchRoot) / ("xisf-light-" + std::to_string(index) + ".xisf");
        const auto utf8 = target.u8string();
        copies.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        const an::Result<an::OutputReport> written = an::writeImage(
            engine, execution,
            {.destinationPath = copies.back(), .intent = &*intent, .durability = an::OutputDurability::CloseOnly},
            decoded->frame, decoded->metadata);
        if (!written.hasValue())
        {
            error = engineError("could not write the XISF copy of '" + alignedPaths[index] + "'", written.status());
            return false;
        }
    }
    // What a stack that read every frame whole would read once, decoded here outside the row's clock.
    {
        const auto started = Clock::now();
        for (const std::string& copy : copies)
        {
            const an::Result<an::CodecDecodeResult> whole =
                an::decodeImage(engine, execution, {.path = copy, .readMetadata = false});
            if (!whole.hasValue())
            {
                error = engineError("could not read '" + copy + "' whole", whole.status());
                return false;
            }
        }
        figures.wholeReadMs = elapsedMs(started);
    }
    const std::optional<an::ImageDescriptor> descriptor = describeSet(engine, execution, copies, error);
    if (!descriptor.has_value())
    {
        return false;
    }
    // The copies are what the stack reads, so they are what the row's storage state is put on; the FITS files they
    // were made from are what its plane is checked against.
    StageClockHooks onCopies;
    onCopies.beforeClock = [&](StageFramePaths, std::string& reason) {
        return !hooks.beforeClock || hooks.beforeClock(copies, reason);
    };
    onCopies.afterClock = [&](StageFramePaths, std::span<const float> plane) {
        if (hooks.afterClock)
        {
            hooks.afterClock(alignedPaths, plane);
        }
    };
    if (!onCopies.beforeClock(copies, error))
    {
        return false;
    }
    if (writesReservedMaster(scenario))
    {
        return stackIntoReservedMaster(engine, execution, copies, *descriptor, scratchRoot, onCopies, timing, figures,
                                       frameReads, digest, error);
    }

    const auto started = Clock::now();
    const double cpuStarted = processCpuMilliseconds();
    StackedMaster stacked;
    const bool completed =
        stackFiles(engine, execution, copies, *descriptor, an::StackSourceContract{}, stacked, frameReads, error);
    timing = stageTiming(started, cpuStarted);
    if (!completed)
    {
        return false;
    }
    return handOverStagePlane(stacked, copies, onCopies, "the XISF final stack", digest, error);
}

} // namespace an::bench::engine
