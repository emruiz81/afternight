// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the streaming stack executor: the plan's passes run in order over the working
// set's memory, every band read through the source path under both access modes, the streaming and
// sample-history combinations held to their oracles and to the operations they compose, the
// measurement passes, the weighting and rejection diagnostics, cancellation and failure leaving
// nothing behind, memory held equal to the plan, progress and traces, and the transition a stacked
// image takes.

#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPreparedState.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <thread>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 12u;
constexpr std::uint32_t kHeight = 8u;
constexpr std::uint32_t kFrames = 3u;

/// An engine with a stated concurrency and memory budget.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u,
                                           std::uint64_t memoryBudgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryBudgetBytes;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// Everything a case may state about the operation a stack runs as.
struct ExecutionSetup
{
    an::ComputeBackend backend = an::ComputeBackend::Automatic;
    std::stop_token stopToken{};
    std::optional<std::chrono::steady_clock::time_point> deadline{};
    an::ProgressSink* progressSink = nullptr;
    an::DiagnosticSink* diagnosticSink = nullptr;
    an::TraceSink* traceSink = nullptr;
    std::uint32_t concurrency = an::kUseContextConcurrency;
};

/// One operation in an engine, as the setup states it.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, const ExecutionSetup& setup = {})
{
    an::ExecutionOptions options;
    options.backend = setup.backend;
    options.stopToken = setup.stopToken;
    options.deadline = setup.deadline;
    options.progressSink = setup.progressSink;
    options.diagnosticSink = setup.diagnosticSink;
    options.traceSink = setup.traceSink;
    options.maxConcurrency = setup.concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A descriptor of the stated representation, schema and domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::SampleType sampleType = an::SampleType::Float32,
                                                 an::ChannelSchema schema = an::ChannelSchema::Mono,
                                                 an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                                 std::uint32_t width = kWidth, std::uint32_t height = kHeight)
{
    const std::uint32_t channels = an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = *extent,
        .sampleType = sampleType,
        .channelSchema = schema,
        .colorSpace = schema == an::ChannelSchema::Rgb ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified,
        .numericDomain = domain,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A plain average of the stated frames, which every rule admits.
[[nodiscard]] an::StackRequest baseRequest(const an::ImageDescriptor& descriptor = makeDescriptor(),
                                           std::uint32_t frames = kFrames)
{
    return an::StackRequest{.input = an::StackInputDescription{.frameCount = frames, .descriptor = descriptor}};
}

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(baseRequest());
}

/// A layout with rows of exactly the width, planar.
[[nodiscard]] an::ImageLayout makeLayout(an::SampleType sampleType, std::uint32_t width, std::uint32_t rows,
                                         std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, rows, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = an::sampleTypeSizeBytes(sampleType),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A validity descriptor of one rule.
[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    const std::optional<an::ValidityDescriptor> descriptor = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value_or(an::ValidityDescriptor{});
}

/// A deterministic value in the unit interval that differs at every position.
[[nodiscard]] double patterned(std::uint64_t index, std::uint32_t salt) noexcept
{
    const std::uint64_t mixed = (index + 1u) * 2654435761ull + salt * 40503ull;
    return 0.05 + 0.85 * static_cast<double>(mixed % 9973u) / 9973.0;
}

/// One frame owned here, planar, with its rule and an optional mask.
struct OwnedFrame
{
    an::SampleType sampleType = an::SampleType::Float32;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 1u;
    std::vector<double> storage;
    std::optional<an::ImageLayout> layout;
    an::ValidityDescriptor descriptor{};
    std::vector<std::uint8_t> maskBits;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    std::uint32_t frameIndex = 0u;

    void create(an::SampleType type, std::uint32_t frameWidth, std::uint32_t frameHeight, std::uint32_t frameChannels,
                an::ValidityRule rule = an::ValidityRule::AllValid)
    {
        sampleType = type;
        width = frameWidth;
        height = frameHeight;
        channels = frameChannels;
        layout = makeLayout(type, width, height, channels);
        storage.assign(static_cast<std::size_t>(layout->totalBytes() / sizeof(double)) + 1u, 0.0);
        descriptor = ruleOf(rule);
    }

    [[nodiscard]] std::byte* bytes() noexcept
    {
        return reinterpret_cast<std::byte*>(storage.data());
    }

    [[nodiscard]] const std::byte* bytes() const noexcept
    {
        return reinterpret_cast<const std::byte*>(storage.data());
    }

    [[nodiscard]] std::uint64_t offsetOf(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const noexcept
    {
        return channel * layout->planeStrideBytes() + y * layout->rowStrideBytes() +
               static_cast<std::uint64_t>(x) * layout->elementSizeBytes();
    }

    template <typename SampleT> [[nodiscard]] SampleT& at(std::uint32_t channel, std::uint32_t x, std::uint32_t y)
    {
        return *reinterpret_cast<SampleT*>(bytes() + offsetOf(channel, x, y));
    }

    template <typename SampleT> [[nodiscard]] SampleT at(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        return *reinterpret_cast<const SampleT*>(bytes() + offsetOf(channel, x, y));
    }

    /// The sample at a position widened to double, whatever the representation.
    [[nodiscard]] double value(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        switch (sampleType)
        {
        case an::SampleType::UInt16:
            return static_cast<double>(at<std::uint16_t>(channel, x, y)) / 65535.0;
        case an::SampleType::Float64:
            return at<double>(channel, x, y);
        default:
            return static_cast<double>(at<float>(channel, x, y));
        }
    }

    /// Writes a unit-interval value at every position, encoded for the representation.
    void fill(std::uint32_t salt)
    {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    put(channel, x, y, patterned((static_cast<std::uint64_t>(channel) * height + y) * width + x, salt));
                }
            }
        }
    }

    /// Writes one unit-interval value, scaled to full scale for an integer representation.
    void put(std::uint32_t channel, std::uint32_t x, std::uint32_t y, double unit)
    {
        switch (sampleType)
        {
        case an::SampleType::UInt16:
            at<std::uint16_t>(channel, x, y) = static_cast<std::uint16_t>(std::lround(unit * 65535.0));
            break;
        case an::SampleType::Float64:
            at<double>(channel, x, y) = unit;
            break;
        default:
            at<float>(channel, x, y) = static_cast<float>(unit);
            break;
        }
    }

    /// Gives the frame a packed mask of the stated plane count, every bit set.
    void addMask(std::uint32_t planes)
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, height, planes, an::defaultEngineLimits());
        ASSERT_TRUE(extent.has_value());
        const std::uint64_t rowBytes = an::packedMaskRowBytes(width);
        maskBits.assign(static_cast<std::size_t>(rowBytes * height * planes), 0xFFu);
        mask = an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>(maskBits), *extent);
        ASSERT_TRUE(mask.has_value());
    }

    /// Clears one bit of the mask.
    void clearBit(std::uint32_t plane, std::uint32_t x, std::uint32_t y)
    {
        ASSERT_TRUE(mask.has_value());
        const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask->planeStrideBytes() +
                                     static_cast<std::uint64_t>(y) * mask->rowStrideBytes() + x / 8u;
        maskBits[static_cast<std::size_t>(offset)] &= static_cast<std::uint8_t>(~(1u << (x % 8u)));
    }

    /// The whole frame as a source band.
    [[nodiscard]] an::StackSourceBand band() const noexcept
    {
        return an::StackSourceBand{
            .sampleType = sampleType,
            .samples = bytes(),
            .layout = layout.has_value() ? &*layout : nullptr,
            .frameIndex = frameIndex,
            .firstRow = 0u,
            .validity = an::StackBandValidity{.descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr}};
    }

    /// The frame's validity as the normalization views take it.
    [[nodiscard]] an::KernelValidityView kernelValidity() const noexcept
    {
        return an::KernelValidityView{
            .descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr, .maskPlane = 0u};
    }

    /// The frame as a checked normalization frame of the stated description.
    [[nodiscard]] an::NormalizationFrameView normalizationView(const an::ImageDescriptor& description) const
    {
        const an::Result<an::NormalizationFrameView> made =
            an::NormalizationFrameView::create(bytes(), layout->totalBytes(), *layout, description, kernelValidity());
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? *made : an::NormalizationFrameView{};
    }

    /// The frame as a checked calibration frame of the stated description.
    [[nodiscard]] an::CalibrationFrameView calibrationView(const an::ImageDescriptor& description) const
    {
        const an::Result<an::CalibrationFrameView> made =
            an::CalibrationFrameView::create(bytes(), layout->totalBytes(), *layout, description, kernelValidity());
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? *made : an::CalibrationFrameView{};
    }

    /// The whole frame as the local-weight measurement reads it.
    [[nodiscard]] an::LocalWeightFrameSource localWeightSource() const noexcept
    {
        return an::LocalWeightFrameSource{
            .sampleType = sampleType,
            .samples = bytes(),
            .layout = layout.has_value() ? &*layout : nullptr,
            .validity = an::StackBandValidity{.descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr}};
    }

    /// The whole frame as an accumulator band.
    [[nodiscard]] an::StackFrameBand frameBand(std::span<const float> localWeights = {}) const noexcept
    {
        return an::StackFrameBand{
            .sampleType = sampleType,
            .samples = bytes(),
            .layout = layout.has_value() ? &*layout : nullptr,
            .frameIndex = frameIndex,
            .firstOutputRow = 0u,
            .validity = an::StackBandValidity{.descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr},
            .localWeights = localWeights};
    }
};

/// A set of frames of one description, patterned differently, each at its own index.
[[nodiscard]] std::vector<OwnedFrame> makeFrames(std::uint32_t frames, an::SampleType type = an::SampleType::Float32,
                                                 std::uint32_t channels = 1u, std::uint32_t width = kWidth,
                                                 std::uint32_t height = kHeight,
                                                 an::ValidityRule rule = an::ValidityRule::AllValid)
{
    std::vector<OwnedFrame> owned(frames);
    for (std::uint32_t index = 0u; index < frames; ++index)
    {
        owned[index].create(type, width, height, channels, rule);
        owned[index].frameIndex = index;
        owned[index].fill(11u + 7u * index);
    }
    return owned;
}

/// Every frame as a source band, in frame order.
[[nodiscard]] std::vector<an::StackSourceBand> bandsOf(const std::vector<OwnedFrame>& frames)
{
    std::vector<an::StackSourceBand> bands;
    bands.reserve(frames.size());
    for (const OwnedFrame& frame : frames)
    {
        bands.push_back(frame.band());
    }
    return bands;
}

/// A resident source over the frames.
[[nodiscard]] an::ResidentStackFrameSource makeSource(const std::vector<OwnedFrame>& frames,
                                                      an::StackSourceAccess access = an::StackSourceAccess::WholeFrame)
{
    const std::vector<an::StackSourceBand> bands = bandsOf(frames);
    an::Result<an::ResidentStackFrameSource> made = an::ResidentStackFrameSource::create(bands, access);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        std::array<OwnedFrame, 1u> fallback{};
        fallback[0].create(an::SampleType::Float32, 1u, 1u, 1u);
        const std::array<an::StackSourceBand, 1u> one{fallback[0].band()};
        return *an::ResidentStackFrameSource::create(one);
    }
    return std::move(*made);
}

/// Runs a stack over resident frames on a fresh execution.
[[nodiscard]] an::Result<an::StackResult>
run(const an::EngineContext& engine, const an::StackModel& model, const std::vector<OwnedFrame>& frames,
    const an::StackSourceContract& contract = {}, const an::StackExecutionRequest& request = {},
    const an::StackExecutionOutputs& outputs = {}, an::StackSourceAccess access = an::StackSourceAccess::WholeFrame,
    const ExecutionSetup& setup = {})
{
    const an::ExecutionContext execution = makeExecution(engine, setup);
    an::ResidentStackFrameSource source = makeSource(frames, access);
    return an::executeStack(engine, execution, model, source, contract, request, outputs);
}

/// One output sample of a result, read at the stated representation.
template <typename SampleT>
[[nodiscard]] SampleT sampleAt(const an::ImageStorageBytes& storage, std::uint32_t channel, std::uint32_t x,
                               std::uint32_t y)
{
    const std::optional<an::ImageLayout> layout = storage.layout();
    EXPECT_TRUE(layout.has_value());
    const std::uint64_t offset = channel * layout->planeStrideBytes() + y * layout->rowStrideBytes() +
                                 static_cast<std::uint64_t>(x) * layout->elementSizeBytes();
    SampleT sample{};
    std::memcpy(&sample, static_cast<const std::byte*>(storage.data()) + offset, sizeof(SampleT));
    return sample;
}

/// Whether the result's mask selects one element.
[[nodiscard]] bool maskAt(const an::ValidityMask& mask, std::uint32_t channel, std::uint32_t x, std::uint32_t y)
{
    const std::optional<an::ValidityMaskView<const std::uint8_t>> view = mask.view();
    EXPECT_TRUE(view.has_value());
    return view.has_value() && view->test(channel, x, y).value_or(false);
}

/// What an engine's resource has outstanding right now.
[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    const an::MemoryResource* resource = engine.memoryResource();
    return resource != nullptr ? resource->counters().liveBytes : 0u;
}

/// The first text-valued context field under a key, if the failure carries one.
[[nodiscard]] std::optional<std::string> textField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() == nullptr)
    {
        return std::nullopt;
    }
    for (const an::StatusContextField& field : status.detail()->context)
    {
        if (field.key == key && field.kind == an::ContextValueKind::Text)
        {
            return field.text;
        }
    }
    return std::nullopt;
}

/// The first count-valued context field under a key, if the failure carries one.
[[nodiscard]] std::optional<std::uint64_t> countField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() == nullptr)
    {
        return std::nullopt;
    }
    for (const an::StatusContextField& field : status.detail()->context)
    {
        if (field.key == key && field.kind == an::ContextValueKind::Count)
        {
            return field.count;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
}

/// The plain mean the accumulators compute: the frames' samples summed in order, over the count.
[[nodiscard]] float meanOf(const std::vector<OwnedFrame>& frames, std::uint32_t channel, std::uint32_t x,
                           std::uint32_t y)
{
    double sum = 0.0;
    for (const OwnedFrame& frame : frames)
    {
        sum = sum + frame.value(channel, x, y) * 1.0;
    }
    return static_cast<float>(sum / static_cast<double>(frames.size()));
}

/// The weighted mean the accumulators compute with one weight per frame.
[[nodiscard]] float weightedMeanOf(const std::vector<OwnedFrame>& frames, std::span<const double> weights,
                                   std::uint32_t channel, std::uint32_t x, std::uint32_t y)
{
    double sum = 0.0;
    double weightSum = 0.0;
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        sum = sum + frames[index].value(channel, x, y) * weights[index];
        weightSum = weightSum + weights[index];
    }
    return static_cast<float>(sum / weightSum);
}

/// The exact median the column kernel computes.
[[nodiscard]] float medianOf(const std::vector<OwnedFrame>& frames, std::uint32_t channel, std::uint32_t x,
                             std::uint32_t y)
{
    std::vector<double> column;
    for (const OwnedFrame& frame : frames)
    {
        column.push_back(frame.value(channel, x, y) + 0.0);
    }
    std::sort(column.begin(), column.end());
    const std::size_t middle = column.size() / 2u;
    if (column.size() % 2u == 1u)
    {
        return static_cast<float>(column[middle]);
    }
    return static_cast<float>((column[middle - 1u] + column[middle]) * 0.5);
}

/// Every output sample of a mono Float32 result as a vector, for bit comparisons between runs.
[[nodiscard]] std::vector<std::uint32_t> bitsOf(const an::StackResult& result)
{
    const std::optional<an::ImageLayout> layout = result.image.layout();
    EXPECT_TRUE(layout.has_value());
    const std::size_t bytes = static_cast<std::size_t>(layout->totalBytes());
    std::vector<std::uint32_t> words(bytes / sizeof(std::uint32_t));
    std::memcpy(words.data(), result.image.data(), bytes);
    return words;
}

/// A progress sink that keeps every report.
class RecordingProgressSink final : public an::ProgressSink
{
public:
    struct Report
    {
        std::uint64_t completed = 0u;
        std::uint64_t total = 0u;
        std::string stage;
    };

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_reports.push_back(Report{report.completed, report.total, report.stage});
    }

    [[nodiscard]] std::vector<Report> reports() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_reports;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Report> m_reports;
};

/// A diagnostic sink that keeps every sentence.
class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    struct Event
    {
        an::DiagnosticSeverity severity = an::DiagnosticSeverity::Info;
        std::string operation;
        std::string message;
    };

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(Event{event.severity, event.operation, event.message});
    }

    [[nodiscard]] std::vector<Event> events() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_events;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Event> m_events;
};

/// A trace sink that keeps every counter.
class RecordingTraceSink final : public an::TraceSink
{
public:
    struct Event
    {
        an::TraceEventKind kind = an::TraceEventKind::Instant;
        std::string category;
        std::string name;
        double value = 0.0;
    };

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(Event{event.kind, event.category, event.name, event.value});
    }

    [[nodiscard]] std::vector<Event> events() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_events;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Event> m_events;
};

/// A source that fails one frame's read, to prove the stack fails whole.
class FailingSource final : public an::StackFrameSource
{
public:
    FailingSource(an::ResidentStackFrameSource inner, std::uint32_t failAt, an::ErrorCode code)
        : m_inner(std::move(inner)), m_failAt(failAt), m_code(code)
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return m_inner.access();
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        ++reads;
        if (frameIndex == m_failAt)
        {
            return an::Status::failure(m_code, an::StatusDetail{.message = "the test source refused this frame",
                                                                .operation = "FailingSource::read",
                                                                .domain = an::ErrorDomain::Io});
        }
        return m_inner.read(execution, frameIndex, firstRow, rows, buffer);
    }

    std::uint32_t reads = 0u;

private:
    an::ResidentStackFrameSource m_inner;
    std::uint32_t m_failAt;
    an::ErrorCode m_code;
};

/// A source that asks the operation to stop after a stated number of reads.
class StoppingSource final : public an::StackFrameSource
{
public:
    StoppingSource(an::ResidentStackFrameSource inner, std::stop_source& stop, std::uint32_t stopAfter)
        : m_inner(std::move(inner)), m_stop(&stop), m_stopAfter(stopAfter)
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return m_inner.access();
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        ++reads;
        if (reads == m_stopAfter)
        {
            m_stop->request_stop();
        }
        return m_inner.read(execution, frameIndex, firstRow, rows, buffer);
    }

    std::uint32_t reads = 0u;

private:
    an::ResidentStackFrameSource m_inner;
    std::stop_source* m_stop;
    std::uint32_t m_stopAfter;
};

/// A source that decodes every read into the executor's slot, the way a codec would, so the bands
/// the executor sees live in its own buffer and may be transformed in place.
class DecodingSource final : public an::StackFrameSource
{
public:
    DecodingSource(const std::vector<OwnedFrame>& frames, an::StackSourceAccess access)
        : m_frames(&frames), m_access(access)
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return m_access;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        ++reads;
        const OwnedFrame& frame = (*m_frames)[frameIndex];
        const an::Result<an::StackSourceRows> window = an::stackSourceRowsOf(frame.band(), firstRow, rows);
        if (!window.hasValue())
        {
            return window.status();
        }
        const an::StackSourceBand borrowed = window->band();
        const std::uint64_t bytes = borrowed.layout->totalBytes();
        if (buffer.size() < bytes)
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       an::StatusDetail{.message = "the executor lent a slot smaller than the band",
                                                        .operation = "DecodingSource::read",
                                                        .domain = an::ErrorDomain::Io});
        }
        // The window keeps the frame's strides; a decoder writes rows packed. Copy row by row.
        m_layout = makeLayout(frame.sampleType, frame.width, rows, frame.channels);
        for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < rows; ++y)
            {
                std::memcpy(buffer.data() + channel * m_layout->planeStrideBytes() + y * m_layout->rowStrideBytes(),
                            static_cast<const std::byte*>(borrowed.samples) +
                                channel * borrowed.layout->planeStrideBytes() + y * borrowed.layout->rowStrideBytes(),
                            static_cast<std::size_t>(m_layout->rowStrideBytes()));
            }
        }
        m_window = *window;
        an::StackSourceBand band = borrowed;
        band.samples = buffer.data();
        band.layout = &*m_layout;
        band.validity.mask = m_window.mask.has_value() ? &*m_window.mask : nullptr;
        slotBands += 1u;
        return band;
    }

    std::uint32_t reads = 0u;
    std::uint32_t slotBands = 0u;

private:
    const std::vector<OwnedFrame>* m_frames;
    an::StackSourceAccess m_access;
    std::optional<an::ImageLayout> m_layout;
    an::StackSourceRows m_window{};
};

// The constant-expression half: the bookkeeping layout and the tile's weight bands follow from
// the model, so an executor and a test agree on them without running anything.
constexpr std::optional<an::ImageExtent> kConstantExtent =
    an::ImageExtent::create(16u, 16u, 1u, an::defaultEngineLimits());
static_assert(kConstantExtent.has_value());
constexpr std::optional<an::ImageDescriptor> kConstantDescriptor =
    an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *kConstantExtent,
                                                           .sampleType = an::SampleType::Float32,
                                                           .channelSchema = an::ChannelSchema::Mono,
                                                           .colorSpace = an::ColorSpace::Unspecified,
                                                           .numericDomain = an::NumericDomain::NormalizedLinear,
                                                           .cfa = {},
                                                           .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                           .pixelAspectRatio = 1.0});
static_assert(kConstantDescriptor.has_value());
constexpr an::StackRequest kPlainRequest{
    .input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor}};
constexpr std::optional<an::StackModel> kPlainModel = an::StackModel::create(kPlainRequest);
static_assert(kPlainModel.has_value());
constexpr an::StackRequest kClippedRequest{
    .input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
    .rejection = an::StackRejectionRequest{.method = an::StackRejection::SigmaClipping},
    .localWeighting = an::LocalWeightingRequest{.enabled = true}};
constexpr std::optional<an::StackModel> kClippedModel = an::StackModel::create(kClippedRequest);
static_assert(kClippedModel.has_value());

constexpr an::StackFrameBookkeepingLayout kPlainLayout = an::stackFrameBookkeepingLayoutOf(*kPlainModel, 2u);
static_assert(kPlainLayout.sampleCountersOffset == 0u);
static_assert(kPlainLayout.coverageHistogramOffset == 2u * 3u * 8u);
static_assert(kPlainLayout.workerSlicesOffset == 2u * 3u * 8u + 4u * 8u);
static_assert(kPlainLayout.workerSlicesBytes == 2u * an::kStackWorkerSliceRecordBytes);
static_assert(kPlainLayout.workerCountersBytes == 0u);
static_assert(kPlainLayout.bandRecordsBytes == 3u * an::kStackBandRecordBytes);
static_assert(kPlainLayout.rateRecordsBytes == 0u);
static_assert(kPlainLayout.flaggedFramesBytes == 0u);
static_assert(kPlainLayout.totalBytes ==
              2u * 3u * 8u + 4u * 8u + 2u * an::kStackWorkerSliceRecordBytes + 3u * an::kStackBandRecordBytes);
static_assert(an::stackFrameBookkeepingBytes(*kPlainModel, 2u) == kPlainLayout.totalBytes);
static_assert(an::stackLocalWeightTileBytesPerRow(*kPlainModel) == 0u);
static_assert(sizeof(std::span<std::byte>) <= an::kStackWorkerSliceRecordBytes);
static_assert(an::stackAccumulatorsCoverOneTile(*kPlainModel, an::StackSourceAccess::Region));
static_assert(!an::stackAccumulatorsCoverOneTile(*kPlainModel, an::StackSourceAccess::WholeFrame));
static_assert(!an::stackAccumulatorsCoverOneTile(*kClippedModel, an::StackSourceAccess::Region));
static_assert(an::stackAccumulatorBytesPerRow(*kPlainModel) == 16u * (8u + 4u));
static_assert(an::stackAccumulatorBytesPerRow(*kClippedModel) == 0u);

constexpr an::StackFrameBookkeepingLayout kClippedLayout = an::stackFrameBookkeepingLayoutOf(*kClippedModel, 2u);
static_assert(kClippedLayout.workerCountersOffset == 2u * 3u * 8u + 4u * 8u + 2u * an::kStackWorkerSliceRecordBytes);
static_assert(kClippedLayout.workerCountersBytes == 2u * 3u * 2u * 8u);
static_assert(kClippedLayout.bandRecordsOffset == kClippedLayout.workerCountersOffset + 2u * 3u * 2u * 8u);
static_assert(kClippedLayout.bandRecordsBytes == 3u * an::kStackBandRecordBytes);
static_assert(kClippedLayout.rateRecordsBytes == 3u * an::kStackFrameRateRecordBytes);
static_assert(kClippedLayout.flaggedFramesBytes == 16u);
static_assert(kClippedLayout.totalBytes == kClippedLayout.flaggedFramesOffset + 16u);
static_assert(an::stackLocalWeightTileBytesPerRow(*kClippedModel) == 16u * 3u * sizeof(float));
static_assert(an::stackLocalWeightTileBytesPerRow(*kPlainModel, an::StackSourceAccess::WholeFrame) == 0u);
static_assert(an::stackLocalWeightTileBytesPerRow(*kPlainModel, an::StackSourceAccess::Region) == 0u);
static_assert(an::stackLocalWeightTileBytesPerRow(*kClippedModel, an::StackSourceAccess::Region) ==
              16u * 3u * sizeof(float));
static_assert(sizeof(an::StackFrameBand) <= an::kStackBandRecordBytes);
static_assert(sizeof(an::FrameRejectionRate) <= an::kStackFrameRateRecordBytes);
static_assert(alignof(an::StackFrameBand) <= an::kStackFrameCounterBytes);
static_assert(alignof(an::FrameRejectionRate) <= an::kStackFrameCounterBytes);

// The measurement workspaces follow the same rule: the reference frame first under a local fit,
// the integral images first under the lattice, each rounded to a boundary before what follows.
constexpr an::StackRequest kLocalRequest{
    .input = an::StackInputDescription{.frameCount = 3u, .descriptor = *kConstantDescriptor},
    .localWeighting = an::LocalWeightingRequest{.enabled = true},
    .normalization = an::StackNormalizationRequest{.scope = an::StackNormalizationScope::Local}};
constexpr std::optional<an::StackModel> kLocalModel = an::StackModel::create(kLocalRequest);
static_assert(kLocalModel.has_value());
constexpr an::StackMeasurementWorkspaceLayout kLocalWorkspace =
    an::stackMeasurementWorkspaceLayoutOf(*kLocalModel, 2u, 1000u);
static_assert(kLocalWorkspace.normalizationReferenceOffset == 0u);
static_assert(kLocalWorkspace.normalizationFrameOffset == 1024u);
static_assert(kLocalWorkspace.normalizationBytes == 1024u);
static_assert(kLocalWorkspace.localWeightIntegralOffset == 0u);
static_assert(kLocalWorkspace.localWeightFrameOffset == 1000u);
static_assert(kLocalWorkspace.localWeightScratchOffset == 1024u);
static_assert(kLocalWorkspace.localWeightBytes == 1024u + 2u * an::stackScratchBytesPerWorker(*kLocalModel));
constexpr an::StackMeasurementWorkspaceLayout kPlainWorkspace =
    an::stackMeasurementWorkspaceLayoutOf(*kPlainModel, 2u, 0u);
static_assert(kPlainWorkspace.normalizationBytes == 0u);
static_assert(kPlainWorkspace.localWeightBytes == 0u);
constexpr an::StackMeasurementWorkspaceLayout kLatticeWorkspace =
    an::stackMeasurementWorkspaceLayoutOf(*kClippedModel, 2u, 1000u);
static_assert(kLatticeWorkspace.localWeightFrameOffset == 1000u);
static_assert(kLatticeWorkspace.localWeightScratchOffset == 1000u);
static_assert(kLatticeWorkspace.localWeightBytes == 1000u);

TEST(StackExecutorTest, ResidentSourceRefusesWhatItCannotServe)
{
    std::vector<OwnedFrame> frames = makeFrames(2u);
    {
        const an::Result<an::ResidentStackFrameSource> none = an::ResidentStackFrameSource::create({});
        EXPECT_EQ(none.status().code(), an::ErrorCode::EmptySelection);
    }
    {
        std::vector<an::StackSourceBand> bands = bandsOf(frames);
        bands[1].samples = nullptr;
        const an::Result<an::ResidentStackFrameSource> made = an::ResidentStackFrameSource::create(bands);
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(countField(made.status(), an::ContextKey::Requested), 1u);
    }
    {
        std::vector<an::StackSourceBand> bands = bandsOf(frames);
        bands[1].frameIndex = 0u;
        const an::Result<an::ResidentStackFrameSource> made = an::ResidentStackFrameSource::create(bands);
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(made.status(), "frameIndex")) << made.status().message();
    }
    {
        std::vector<an::StackSourceBand> bands = bandsOf(frames);
        bands[0].firstRow = 1u;
        const an::Result<an::ResidentStackFrameSource> made = an::ResidentStackFrameSource::create(bands);
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(made.status(), "firstRow")) << made.status().message();
    }
    {
        OwnedFrame other;
        other.create(an::SampleType::Float32, kWidth, kHeight + 1u, 1u);
        other.frameIndex = 1u;
        const std::array<an::StackSourceBand, 2u> bands{frames[0].band(), other.band()};
        const an::Result<an::ResidentStackFrameSource> made = an::ResidentStackFrameSource::create(bands);
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(countField(made.status(), an::ContextKey::Requested), 1u);
    }
    {
        std::vector<an::StackSourceBand> bands = bandsOf(frames);
        const an::Result<an::ResidentStackFrameSource> made =
            an::ResidentStackFrameSource::create(bands, static_cast<an::StackSourceAccess>(9u));
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "access");
    }
    {
        frames[0].addMask(1u);
        const std::vector<an::StackSourceBand> bands = bandsOf(frames);
        const an::Result<an::ResidentStackFrameSource> made = an::ResidentStackFrameSource::create(bands);
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(made.status(), "mask")) << made.status().message();
        frames[0].mask.reset();
    }

    // Reads: the whole frame under whole-frame access, any window under region access.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<std::byte> unused(16u);
    {
        an::ResidentStackFrameSource whole = makeSource(frames);
        EXPECT_EQ(whole.frameCount(), 2u);
        EXPECT_EQ(whole.access(), an::StackSourceAccess::WholeFrame);
        const an::Result<an::StackSourceBand> band = whole.read(execution, 1u, 0u, kHeight, unused);
        ASSERT_TRUE(band.hasValue()) << band.status().message();
        EXPECT_EQ(band->samples, frames[1].bytes());
        EXPECT_EQ(band->frameIndex, 1u);
        EXPECT_EQ(whole.read(execution, 1u, 2u, 3u, unused).status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(whole.read(execution, 2u, 0u, kHeight, unused).status().code(), an::ErrorCode::OutOfRange);
        EXPECT_EQ(whole.read(execution, 0u, 0u, 0u, unused).status().code(), an::ErrorCode::InvalidArgument);
    }
    {
        an::ResidentStackFrameSource region = makeSource(frames, an::StackSourceAccess::Region);
        EXPECT_EQ(region.access(), an::StackSourceAccess::Region);
        const an::Result<an::StackSourceBand> window = region.read(execution, 0u, 2u, 3u, unused);
        ASSERT_TRUE(window.hasValue()) << window.status().message();
        EXPECT_EQ(window->firstRow, 2u);
        EXPECT_EQ(window->layout->extent().height(), 3u);
        EXPECT_EQ(window->samples, frames[0].bytes() + frames[0].offsetOf(0u, 0u, 2u));
        EXPECT_EQ(region.read(execution, 0u, 6u, 3u, unused).status().code(), an::ErrorCode::OutOfRange);
        an::ResidentStackFrameSource moved = std::move(region);
        EXPECT_EQ(moved.frameCount(), 2u);
        EXPECT_EQ(region.frameCount(), 0u); // NOLINT(bugprone-use-after-move)
        EXPECT_EQ(region.read(execution, 0u, 0u, 1u, unused).status().code(), an::ErrorCode::NotInitialized);
    }
}

TEST(StackExecutorTest, AResidentSourceReadsForTheOperationItIsHandedRatherThanOneItRemembered)
{
    std::vector<OwnedFrame> frames = makeFrames(2u);
    an::ResidentStackFrameSource source = makeSource(frames);
    std::vector<std::byte> unused(16u);

    // The execution is an argument of the read, so a source holds none and cannot be built around
    // one: handed nothing to run as, it refuses rather than reading for whatever it was made with.
    EXPECT_EQ(source.read(an::ExecutionContext{}, 0u, 0u, kHeight, unused).status().code(),
              an::ErrorCode::NotInitialized);

    // And the stop it honours is the one belonging to the operation that is actually asking.
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext stopping = makeExecution(engine, ExecutionSetup{.stopToken = stop.get_token()});
    EXPECT_TRUE(source.read(stopping, 0u, 0u, kHeight, unused).hasValue());
    stop.request_stop();
    EXPECT_EQ(source.read(stopping, 0u, 0u, kHeight, unused).status().code(), an::ErrorCode::Cancelled);
}

TEST(StackExecutorTest, AveragesFramesStoredAtTheTileWidth)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(baseRequest());
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    std::uint64_t handedOver = 0u;
    {
        an::Result<an::StackResult> made = run(engine, model, frames);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        const an::StackResult& result = *made;
        ASSERT_TRUE(result.image.valid());
        ASSERT_TRUE(result.mask.valid());
        EXPECT_EQ(result.descriptor, model.outputDescriptor());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_EQ(sampleAt<float>(result.image, 0u, x, y), meanOf(frames, 0u, x, y)) << x << "," << y;
                EXPECT_TRUE(maskAt(result.mask, 0u, x, y)) << x << "," << y;
            }
        }
        for (const an::ImageStorageBytes& map : result.maps)
        {
            EXPECT_FALSE(map.valid());
        }

        const an::StackExecutionReport& report = result.report;
        EXPECT_EQ(report.plan.strategy.strategy, an::StackAccumulationStrategy::WholeOutputAccumulators);
        EXPECT_FALSE(report.stages.any());
        EXPECT_EQ(report.passesRun, 2u);
        EXPECT_EQ(report.frameReads, kFrames);
        EXPECT_EQ(report.frameReads, report.plan.io.frameReads);
        EXPECT_EQ(report.inputBytesRead, report.plan.io.logicalInputBytes);
        EXPECT_EQ(report.tilesCombined, 1u);
        EXPECT_EQ(report.samplesAdmitted, static_cast<std::uint64_t>(kFrames) * kWidth * kHeight);
        EXPECT_EQ(report.samplesExcludedByValidity, 0u);
        EXPECT_EQ(report.elementsWithSamples, static_cast<std::uint64_t>(kWidth) * kHeight);
        EXPECT_EQ(report.elementsWithoutSamples, 0u);
        EXPECT_TRUE(report.frameSampleCountsComplete);
        EXPECT_EQ(report.weighting.disposition, an::StackFrameWeightingDisposition::Uniform);
        EXPECT_EQ(report.weighting.source, an::StackWeighting::Uniform);
        EXPECT_DOUBLE_EQ(report.weighting.summary.efficiency, 1.0);
        EXPECT_TRUE(report.coverage.available);
        EXPECT_EQ(report.coverage.minimum, kFrames);
        EXPECT_EQ(report.coverage.maximum, kFrames);
        EXPECT_DOUBLE_EQ(report.coverage.mean, 3.0);
        EXPECT_DOUBLE_EQ(report.coverage.median, 3.0);
        EXPECT_DOUBLE_EQ(report.coverage.standardDeviation, 0.0);
        EXPECT_DOUBLE_EQ(report.effectiveFrameCount, 3.0);
        EXPECT_DOUBLE_EQ(report.theoreticalSnrGain, std::sqrt(3.0));
        EXPECT_DOUBLE_EQ(report.actualSnrGain, std::sqrt(3.0));
        EXPECT_DOUBLE_EQ(report.integrationEfficiency, 1.0);
        EXPECT_EQ(report.finalize.outputDomain, an::NumericDomain::NormalizedLinear);
        EXPECT_EQ(report.memory.passesCompleted, 2u);
        EXPECT_EQ(report.memory.refusals, 0u);
        EXPECT_EQ(report.memory.peakHeldBytes, report.plan.memory.peakBytes);
        EXPECT_EQ(report.memory.handedOverBytes,
                  report.plan.memory.outputImageBytes + report.plan.memory.outputMaskBytes);
        handedOver = report.memory.handedOverBytes;
        // What the result owns is exactly what is still outstanding in the engine context.
        EXPECT_EQ(liveBytes(engine), handedOver);
    }
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackExecutorTest, RegionAccessAndARowCapReadTheSameBits)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(baseRequest());
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    const an::Result<an::StackResult> whole = run(engine, model, frames);
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();

    an::StackExecutionRequest capped;
    capped.maximumTileRows = 3u;
    const an::Result<an::StackResult> tiled = run(engine, model, frames, {}, capped);
    ASSERT_TRUE(tiled.hasValue()) << tiled.status().message();
    EXPECT_EQ(tiled->report.plan.tile.tileCount, 3u);
    EXPECT_EQ(tiled->report.tilesCombined, 3u);
    // A streaming stack over whole frames reads each frame once and walks its tiles from there.
    EXPECT_EQ(tiled->report.frameReads, kFrames);
    EXPECT_EQ(bitsOf(*tiled), bitsOf(*whole));

    const an::Result<an::StackResult> region =
        run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(region->report.plan.io.access, an::StackSourceAccess::Region);
    EXPECT_EQ(region->report.frameReads, static_cast<std::uint64_t>(kFrames) * 3u);
    EXPECT_EQ(region->report.frameReads, region->report.plan.io.frameReads);
    EXPECT_EQ(bitsOf(*region), bitsOf(*whole));
}

TEST(StackExecutorTest, ASourcesGranularityReachesThePlanAndTheSameBitsAreCombined)
{
    // The executor hands the source's granularity to the planner: a sigma-clipped stack over region access, whose cache
    // band holds a few rows of these wide frames, takes tiles of the rows one piece of the source spans instead, reads
    // each frame once a tile, and combines the same bits.
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kWide = 4096u;
    constexpr std::uint32_t kTall = 128u;
    constexpr std::uint32_t kPieceRows = 96u;
    an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                          an::NumericDomain::NormalizedLinear, kWide, kTall),
                                           kFrames);
    request.rejection.method = an::StackRejection::SigmaClipping;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames, an::SampleType::Float32, 1u, kWide, kTall);
    const an::Result<an::StackResult> banded = run(engine, model, frames, {}, {}, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(banded.hasValue()) << banded.status().message();
    ASSERT_EQ(banded->report.plan.tile.reason, an::StackTileReason::CacheBand);
    ASSERT_LT(banded->report.plan.tile.rows, kPieceRows);

    an::StackExecutionRequest granular;
    granular.sourceGranularityRows = kPieceRows;
    const an::Result<an::StackResult> widened =
        run(engine, model, frames, {}, granular, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(widened.hasValue()) << widened.status().message();
    EXPECT_EQ(widened->report.plan.tile.rows, kPieceRows);
    EXPECT_EQ(widened->report.plan.tile.reason, an::StackTileReason::SourceGranularity);
    EXPECT_EQ(widened->report.tilesCombined, 2u);
    EXPECT_EQ(widened->report.frameReads, static_cast<std::uint64_t>(kFrames) * 2u);
    EXPECT_EQ(bitsOf(*widened), bitsOf(*banded));
}

TEST(StackExecutorTest, RegionAccessFoldsEveryFrameOfATileInOnePass)
{
    // A weighted, locally weighted colour average with every map, over region access: the tile
    // holds every frame's band and the accumulators take them in one call per tile, and the bits
    // are those of the whole-frame walk, with or without a row cap, at one and four workers, with
    // the reads the plan counted and nothing beside the plan taken from the context.
    const an::EngineContext engine = makeEngine(4u);
    constexpr std::uint32_t kCount = 5u;
    an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb), kCount);
    request.weighting.method = an::StackWeighting::Explicit;
    request.localWeighting.enabled = true;
    request.localWeighting.gridSpacing = 4u;
    request.localWeighting.windowSize = 3u;
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kCount, an::SampleType::Float32, 3u);
    const std::array<double, kCount> perFrame{1.0, 1.5, 0.5, 2.0, 1.25};
    an::StackExecutionRequest execution;
    execution.explicitWeights = an::StackFrameWeights{.perFrame = perFrame};
    const an::Result<an::StackResult> whole = run(engine, model, frames, {}, execution);
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();

    for (const std::uint32_t workers : {1u, 4u})
    {
        for (const std::uint32_t cap : {0u, 3u})
        {
            an::StackExecutionRequest capped = execution;
            capped.maximumTileRows = cap;
            const an::Result<an::StackResult> region =
                run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region, {.concurrency = workers});
            ASSERT_TRUE(region.hasValue()) << region.status().message();
            EXPECT_EQ(bitsOf(*region), bitsOf(*whole)) << workers << " workers, cap " << cap;
            EXPECT_EQ(region->report.plan.io.access, an::StackSourceAccess::Region);
            EXPECT_EQ(region->report.frameReads, region->report.plan.io.frameReads);
            EXPECT_EQ(region->report.tilesCombined, region->report.plan.tile.tileCount);
            EXPECT_EQ(region->report.plan.memory.tileBytesPerRow,
                      static_cast<std::uint64_t>(kWidth) * 3u * kCount * sizeof(float) +
                          static_cast<std::uint64_t>(kWidth) * kCount * sizeof(float));
            EXPECT_TRUE(region->report.frameSampleCountsComplete);
            EXPECT_EQ(region->report.memory.peakHeldBytes, region->report.plan.memory.peakBytes);
        }
    }

    // Integer frames widened into the tile under region access read the same bits too.
    const an::StackModel widened = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    const std::vector<OwnedFrame> integers = makeFrames(kFrames, an::SampleType::UInt16);
    const an::Result<an::StackResult> wideWhole = run(engine, widened, integers);
    ASSERT_TRUE(wideWhole.hasValue()) << wideWhole.status().message();
    const an::Result<an::StackResult> wideRegion =
        run(engine, widened, integers, {}, {}, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(wideRegion.hasValue()) << wideRegion.status().message();
    EXPECT_TRUE(wideRegion->report.stages.convert);
    EXPECT_EQ(bitsOf(*wideRegion), bitsOf(*wideWhole));
}

TEST(StackExecutorTest, WidensIntegerFramesIntoTheTile)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    const std::vector<OwnedFrame> frames = makeFrames(kFrames, an::SampleType::UInt16);
    const an::Result<an::StackResult> made = run(engine, model, frames);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_TRUE(made->report.stages.convert);
    EXPECT_EQ(made->report.plan.memory.tileBytesPerRow, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            // Widened in double and narrowed once per sample, then summed and divided in double.
            double sum = 0.0;
            for (const OwnedFrame& frame : frames)
            {
                sum = sum + static_cast<double>(static_cast<float>(frame.value(0u, x, y))) * 1.0;
            }
            EXPECT_EQ(sampleAt<float>(made->image, 0u, x, y), static_cast<float>(sum / 3.0)) << x << "," << y;
        }
    }
    an::StackExecutionRequest capped;
    capped.maximumTileRows = 2u;
    const an::Result<an::StackResult> region =
        run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(bitsOf(*region), bitsOf(*made));
}

TEST(StackExecutorTest, AccumulatesRawCodesAsStoredWithTheBitsOfTheirWidening)
{
    // Raw sensor codes of 8 and 16 bits widen to Float32 as the numbers they are, so a plain stack
    // accumulates them where they are stored instead of writing each widened band first. Its output,
    // its counts and every frame's admitted count are those of the same codes widened by the caller
    // and stacked as Float32, under whole-frame and region access, from a resident source and from
    // one that decodes into the tile, with zeros the rule excludes and tiles that do not divide the
    // height.
    constexpr std::uint32_t kWide = 37u;
    constexpr std::uint32_t kTall = 11u;
    constexpr std::uint32_t kCount = 5u;
    const an::EngineContext engine = makeEngine(3u);
    for (const an::SampleType type :
         {an::SampleType::UInt16, an::SampleType::Int16, an::SampleType::UInt8, an::SampleType::Int8})
    {
        const an::StackModel codes = makeModel(baseRequest(
            makeDescriptor(type, an::ChannelSchema::Mono, an::NumericDomain::RawSensorCode, kWide, kTall), kCount));
        const an::StackModel widened =
            makeModel(baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                 an::NumericDomain::RawSensorCode, kWide, kTall),
                                  kCount));
        std::vector<OwnedFrame> stored(kCount);
        std::vector<OwnedFrame> floats(kCount);
        for (std::uint32_t frame = 0u; frame < kCount; ++frame)
        {
            stored[frame].create(type, kWide, kTall, 1u, an::ValidityRule::ZeroIsNoData);
            stored[frame].frameIndex = frame;
            floats[frame].create(an::SampleType::Float32, kWide, kTall, 1u, an::ValidityRule::ZeroIsNoData);
            floats[frame].frameIndex = frame;
            for (std::uint32_t y = 0u; y < kTall; ++y)
            {
                for (std::uint32_t x = 0u; x < kWide; ++x)
                {
                    const std::uint64_t index = static_cast<std::uint64_t>(y) * kWide + x;
                    // Every fifth element of a frame is no data; element 3 is no data in every frame.
                    const bool empty = (index + frame) % 5u == 0u || index == 3u;
                    const double unit = patterned(index, 5u + frame);
                    double code = 0.0;
                    switch (type)
                    {
                    case an::SampleType::UInt16:
                        code = empty ? 0.0 : std::floor(unit * 65535.0);
                        stored[frame].at<std::uint16_t>(0u, x, y) = static_cast<std::uint16_t>(code);
                        break;
                    case an::SampleType::Int16:
                        code = empty ? 0.0 : std::floor(unit * 65535.0) - 32768.0;
                        stored[frame].at<std::int16_t>(0u, x, y) = static_cast<std::int16_t>(code);
                        break;
                    case an::SampleType::UInt8:
                        code = empty ? 0.0 : std::floor(unit * 255.0);
                        stored[frame].at<std::uint8_t>(0u, x, y) = static_cast<std::uint8_t>(code);
                        break;
                    default:
                        code = empty ? 0.0 : std::floor(unit * 255.0) - 128.0;
                        stored[frame].at<std::int8_t>(0u, x, y) = static_cast<std::int8_t>(code);
                        break;
                    }
                    floats[frame].at<float>(0u, x, y) = static_cast<float>(code);
                }
            }
        }
        const an::Result<an::StackResult> expected = run(engine, widened, floats);
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        EXPECT_GT(expected->report.elementsWithoutSamples, 0u);

        an::StackExecutionRequest tiles;
        tiles.maximumTileRows = 4u;
        for (const an::StackSourceAccess access : {an::StackSourceAccess::WholeFrame, an::StackSourceAccess::Region})
        {
            const an::Result<an::StackResult> resident = run(engine, codes, stored, {}, tiles, {}, access);
            ASSERT_TRUE(resident.hasValue()) << resident.status().message();
            const an::ExecutionContext execution = makeExecution(engine);
            DecodingSource decoding(stored, access);
            const an::Result<an::StackResult> decoded =
                an::executeStack(engine, execution, codes, decoding, {}, tiles, {});
            ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
            for (const an::StackResult* result : {&*resident, &*decoded})
            {
                const std::string where = std::string(an::sampleTypeName(type)) + " under " +
                                          an::stackSourceAccessName(access) +
                                          (result == &*resident ? " from a resident source" : " from a decoder");
                EXPECT_EQ(bitsOf(*result), bitsOf(*expected)) << where;
                EXPECT_TRUE(result->report.stages.convert) << where;
                EXPECT_EQ(result->report.samplesAdmitted, expected->report.samplesAdmitted) << where;
                EXPECT_EQ(result->report.samplesExcludedByValidity, expected->report.samplesExcludedByValidity)
                    << where;
                EXPECT_EQ(result->report.elementsWithSamples, expected->report.elementsWithSamples) << where;
                EXPECT_EQ(result->report.elementsWithoutSamples, expected->report.elementsWithoutSamples) << where;
                for (std::uint32_t y = 0u; y < kTall; ++y)
                    for (std::uint32_t x = 0u; x < kWide; ++x)
                        EXPECT_EQ(maskAt(result->mask, 0u, x, y), maskAt(expected->mask, 0u, x, y)) << where;
            }
        }
    }
}

TEST(StackExecutorTest, KeepsWideningCodesWhoseWideningIsNotTheIdentity)
{
    // Full-scale codes widen by division, so they may not stand in for their widening. The public
    // accumulators take tile-width bands only, whatever the codes' domain: a band at the stored
    // representation is refused by name there, as it always has been.
    const an::EngineContext engine = makeEngine();
    const an::StackModel fullScale = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    const std::vector<OwnedFrame> frames = makeFrames(kFrames, an::SampleType::UInt16);
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<double> sums(kWidth * kHeight, 0.0);
    std::vector<std::uint32_t> counts(kWidth * kHeight, 0u);
    const an::StackAccumulatorSet set{.sums = sums, .counts = counts};
    const an::Result<an::StackAccumulationOutcome> refused =
        an::accumulateStackFrame(execution, fullScale, set, frames[0].frameBand(), {});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "band.sampleType"));

    const an::StackModel raw = makeModel(
        baseRequest(makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Mono, an::NumericDomain::RawSensorCode)));
    const an::Result<an::StackAccumulationOutcome> stillRefused =
        an::accumulateStackFrame(execution, raw, set, frames[0].frameBand(), {});
    ASSERT_FALSE(stillRefused.hasValue()) << "the published accumulators take tile-width bands only";
    EXPECT_TRUE(messageNames(stillRefused.status(), "band.sampleType"));
}

/// A decoding source that lets reads of different frames overlap, and records how many overlapped.
class OverlappingSource final : public an::StackFrameSource
{
public:
    OverlappingSource(const std::vector<OwnedFrame>& frames, std::vector<std::uint32_t> failing = {})
        : m_frames(&frames), m_failing(std::move(failing)), m_layouts(frames.size())
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::ConcurrentRegion;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext&, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        const std::uint32_t now = inFlight.fetch_add(1u) + 1u;
        std::uint32_t seen = mostInFlight.load();
        while (now > seen && !mostInFlight.compare_exchange_weak(seen, now))
        {
        }
        // Long enough that reads a scheduler runs together are in flight together.
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        reads.fetch_add(1u);
        {
            const std::lock_guard<std::mutex> lock(m_slotsMutex);
            slots.push_back(buffer.data());
        }
        struct Leave
        {
            std::atomic<std::uint32_t>& count;
            ~Leave()
            {
                count.fetch_sub(1u);
            }
        } leave{inFlight};
        if (std::find(m_failing.begin(), m_failing.end(), frameIndex) != m_failing.end())
        {
            return an::Status::failure(an::ErrorCode::ExternalFailure,
                                       an::StatusDetail{.message = "frame " + std::to_string(frameIndex) + " is gone",
                                                        .operation = "OverlappingSource::read",
                                                        .domain = an::ErrorDomain::Io});
        }
        const OwnedFrame& frame = (*m_frames)[frameIndex];
        const an::Result<an::StackSourceRows> window = an::stackSourceRowsOf(frame.band(), firstRow, rows);
        if (!window.hasValue())
        {
            return window.status();
        }
        const an::StackSourceBand borrowed = window->band();
        m_layouts[frameIndex] = makeLayout(frame.sampleType, frame.width, rows, frame.channels);
        const an::ImageLayout& layout = *m_layouts[frameIndex];
        if (buffer.size() < layout.totalBytes())
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments);
        }
        for (std::uint32_t channel = 0u; channel < frame.channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < rows; ++y)
            {
                std::memcpy(buffer.data() + channel * layout.planeStrideBytes() + y * layout.rowStrideBytes(),
                            static_cast<const std::byte*>(borrowed.samples) +
                                channel * borrowed.layout->planeStrideBytes() + y * borrowed.layout->rowStrideBytes(),
                            static_cast<std::size_t>(layout.rowStrideBytes()));
            }
        }
        an::StackSourceBand band = borrowed;
        band.samples = buffer.data();
        band.layout = &layout;
        band.validity.mask = nullptr;
        return band;
    }

    std::atomic<std::uint32_t> inFlight{0u};
    std::atomic<std::uint32_t> mostInFlight{0u};
    std::atomic<std::uint32_t> reads{0u};
    /// Where each read was asked to put its band, in the order the reads ran.
    std::vector<const std::byte*> slots;

private:
    std::mutex m_slotsMutex;
    const std::vector<OwnedFrame>* m_frames;
    std::vector<std::uint32_t> m_failing;
    std::vector<std::optional<an::ImageLayout>> m_layouts;
};

TEST(StackExecutorTest, ReadsATilesFramesTogetherFromASourceThatAllowsIt)
{
    // A source that allows concurrent region reads is read up to the request's concurrent reads at
    // once on the operation's workers, when every read lands in its frame's own band of the tile. The
    // output and every count are those of one read at a time, and a failing read fails the stack
    // with the first failure in frame order.
    constexpr std::uint32_t kCount = 11u;
    constexpr std::uint32_t kTall = 9u;
    const an::EngineContext engine = makeEngine(4u);
    const auto descriptor = makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Mono,
                                           an::NumericDomain::RawSensorCode, kWidth, kTall);
    const an::StackModel model = makeModel(baseRequest(descriptor, kCount));
    std::vector<OwnedFrame> frames =
        makeFrames(kCount, an::SampleType::UInt16, 1u, kWidth, kTall, an::ValidityRule::ZeroIsNoData);
    frames[3].at<std::uint16_t>(0u, 2u, 1u) = 0u;
    an::StackExecutionRequest serial;
    serial.maximumTileRows = 4u;
    const an::Result<an::StackResult> expected =
        run(engine, model, frames, {}, serial, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();

    an::StackExecutionRequest together = serial;
    together.concurrentFrameReads = 3u;
    {
        const an::ExecutionContext execution = makeExecution(engine);
        OverlappingSource source(frames);
        const an::Result<an::StackResult> result = an::executeStack(engine, execution, model, source, {}, together, {});
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(bitsOf(*result), bitsOf(*expected));
        EXPECT_EQ(result->report.plan.io.access, an::StackSourceAccess::ConcurrentRegion);
        EXPECT_EQ(result->report.frameReads, expected->report.frameReads);
        EXPECT_EQ(result->report.inputBytesRead, expected->report.inputBytesRead);
        EXPECT_EQ(result->report.samplesAdmitted, expected->report.samplesAdmitted);
        EXPECT_EQ(result->report.samplesExcludedByValidity, expected->report.samplesExcludedByValidity);
        EXPECT_EQ(source.reads.load(), kCount * 3u);
        EXPECT_GT(source.mostInFlight.load(), 1u) << "reads of different frames overlapped";
        EXPECT_LE(source.mostInFlight.load(), 3u) << "never more than the request's reads at once";
    }
    {
        // One read in flight, however many workers there are, when the request asks for one.
        const an::ExecutionContext execution = makeExecution(engine);
        OverlappingSource source(frames);
        const an::Result<an::StackResult> result = an::executeStack(engine, execution, model, source, {}, serial, {});
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(bitsOf(*result), bitsOf(*expected));
        EXPECT_EQ(source.mostInFlight.load(), 1u);
    }
    {
        // Two frames of one group fail; the stack fails with the earlier one, whichever finished first.
        const an::ExecutionContext execution = makeExecution(engine);
        OverlappingSource source(frames, {5u, 4u});
        const an::Result<an::StackResult> result = an::executeStack(engine, execution, model, source, {}, together, {});
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_TRUE(messageNames(result.status(), "frame 4 could not be read")) << result.status().message();
    }
    {
        // Under one worker the reads a group holds run one after another.
        const an::ExecutionContext execution = makeExecution(engine, ExecutionSetup{.concurrency = 1u});
        OverlappingSource source(frames);
        const an::Result<an::StackResult> result = an::executeStack(engine, execution, model, source, {}, together, {});
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(bitsOf(*result), bitsOf(*expected));
        EXPECT_EQ(source.mostInFlight.load(), 1u);
    }
}

TEST(StackExecutorTest, ReadsTheNextFramesBandWhileTheCurrentOneIsCalibrated)
{
    // A stack that calibrates converts every band from a read slot into the tile. A source that allows
    // concurrent reads and a request with a second slot have the next frame's band read while the
    // current one is calibrated: the output and every count are those of one read at a time, one read
    // is ever in flight, and a failing read fails the stack with the first failure in frame order,
    // after the frames before it are calibrated.
    constexpr std::uint32_t kCount = 11u;
    constexpr std::uint32_t kTall = 9u;
    const an::EngineContext engine = makeEngine(4u);
    const auto descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                           an::NumericDomain::RawSensorCode, kWidth, kTall);
    const an::StackModel model = makeModel(baseRequest(descriptor, kCount));
    const std::vector<OwnedFrame> frames = makeFrames(kCount, an::SampleType::Float32, 1u, kWidth, kTall);
    OwnedFrame bias;
    bias.create(an::SampleType::Float32, kWidth, kTall, 1u);
    bias.frameIndex = 0u;
    for (std::uint32_t y = 0u; y < kTall; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            bias.put(0u, x, y, 0.02 + 0.001 * (x + y));
        }
    }
    const an::ImageDescriptor calibrationDescription = an::stackSourceCalibrationDescriptor(model);
    const an::CalibrationMasters masters{.bias = bias.calibrationView(calibrationDescription)};
    // Prepared under an execution of its own, which returns the engine's whole budget before the stacks run.
    const an::Result<an::CalibrationPlan> planned = [&] {
        const an::ExecutionContext preparing = makeExecution(engine);
        return an::prepareImageCalibration(preparing, calibrationDescription, masters, {});
    }();
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const std::array<an::CalibrationPlan, 1u> plans{*planned};
    const an::StackSourceContract contract{
        .state = an::StackFrameState::Raw, .calibration = plans, .masters = &masters};

    an::StackExecutionRequest serial;
    serial.maximumTileRows = 4u;
    an::StackExecutionRequest ahead = serial;
    ahead.concurrentFrameReads = 2u;
    const auto stack = [&](const an::StackExecutionRequest& request, OverlappingSource& source,
                           std::uint32_t concurrency) {
        const an::ExecutionContext execution = makeExecution(engine, ExecutionSetup{.concurrency = concurrency});
        return an::executeStack(engine, execution, model, source, contract, request, {});
    };
    OverlappingSource serialSource(frames);
    const an::Result<an::StackResult> expected = stack(serial, serialSource, 4u);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    EXPECT_TRUE(expected->report.biasSubtracted);
    EXPECT_EQ(serialSource.mostInFlight.load(), 1u);
    for (const std::uint32_t concurrency : {4u, 1u})
    {
        SCOPED_TRACE(::testing::Message() << concurrency << " workers");
        OverlappingSource source(frames);
        const an::Result<an::StackResult> result = stack(ahead, source, concurrency);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        EXPECT_EQ(bitsOf(*result), bitsOf(*expected));
        EXPECT_EQ(result->report.frameReads, expected->report.frameReads);
        EXPECT_EQ(result->report.inputBytesRead, expected->report.inputBytesRead);
        EXPECT_EQ(result->report.samplesAdmitted, expected->report.samplesAdmitted);
        EXPECT_EQ(result->report.samplesExcludedByValidity, expected->report.samplesExcludedByValidity);
        EXPECT_EQ(source.reads.load(), serialSource.reads.load());
        EXPECT_EQ(source.mostInFlight.load(), 1u) << "one read in flight, beside the conversion";
        if (concurrency > 1u)
        {
            // The band read ahead goes into the slot the band being calibrated does not occupy.
            std::size_t sameSlot = 0u;
            for (std::size_t index = 1u; index < source.slots.size(); ++index)
            {
                sameSlot += (index % kCount != 0u && source.slots[index] == source.slots[index - 1u]) ? 1u : 0u;
            }
            EXPECT_EQ(sameSlot, 0u);
        }
    }
    {
        OverlappingSource source(frames, {6u, 9u});
        const an::Result<an::StackResult> result = stack(ahead, source, 4u);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_TRUE(messageNames(result.status(), "frame 6 could not be read")) << result.status().message();
    }
}

TEST(StackExecutorTest, TakesTheExactMedianThroughTheTile)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest(makeDescriptor(), 5u);
    request.combination = an::StackCombination::Median;
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(5u);
    an::StackExecutionRequest capped;
    capped.maximumTileRows = 3u;
    const an::Result<an::StackResult> made = run(engine, model, frames, {}, capped);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_EQ(made->report.plan.strategy.strategy, an::StackAccumulationStrategy::TileSampleHistory);
    EXPECT_EQ(made->report.frameReads, 5u * 3u);
    EXPECT_EQ(made->report.tilesCombined, 3u);
    ASSERT_TRUE(made->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)].valid());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_EQ(sampleAt<float>(made->image, 0u, x, y), medianOf(frames, 0u, x, y)) << x << "," << y;
            EXPECT_EQ(
                sampleAt<std::uint32_t>(made->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)], 0u, x, y),
                5u);
        }
    }
    EXPECT_TRUE(made->report.frameSampleCountsComplete);
    EXPECT_EQ(made->report.samplesAdmitted, 5ull * kWidth * kHeight);
    EXPECT_TRUE(made->report.coverage.available);
    EXPECT_DOUBLE_EQ(made->report.coverage.mean, 5.0);
    EXPECT_DOUBLE_EQ(made->report.effectiveFrameCount, 5.0);
    EXPECT_EQ(liveBytes(engine), made->report.memory.handedOverBytes);
    const an::Result<an::StackResult> region =
        run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(region.hasValue()) << region.status().message();
    EXPECT_EQ(bitsOf(*region), bitsOf(*made));
}

TEST(StackExecutorTest, RejectsOutliersAndReportsEveryFrame)
{
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kCount = 6u;
    constexpr std::uint32_t kBad = 4u;
    an::StackRequest request = baseRequest(makeDescriptor(), kCount);
    request.rejection.method = an::StackRejection::SigmaClipping;
    request.rejection.sigma = an::SigmaClipThresholds{.low = 2.0, .high = 2.0};
    request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);

    // Six frames whose samples at every pixel are evenly spaced - which no two-sigma clip rejects -
    // and a bright rectangle planted on one of them, which every clip rejects.
    std::vector<OwnedFrame> frames(kCount);
    for (std::uint32_t index = 0u; index < kCount; ++index)
    {
        frames[index].create(an::SampleType::Float32, kWidth, kHeight, 1u);
        frames[index].frameIndex = index;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const double spread = 0.001 * (static_cast<double>(index) - 2.5) / 2.5;
                frames[index].put(0u, x, y, 0.3 + 0.01 * (x % 3u) + 0.02 * (y % 2u) + spread);
            }
        }
    }
    for (std::uint32_t y = 2u; y < 6u; ++y)
    {
        for (std::uint32_t x = 3u; x < 9u; ++x)
        {
            frames[kBad].put(0u, x, y, 0.9);
        }
    }

    std::vector<an::FrameRejectionRate> rates(kCount);
    std::vector<std::uint32_t> flagged(kCount, 99u);
    std::vector<an::RejectionClusterDetection> clusters(4u);
    an::StackExecutionRequest execution;
    execution.clusterAnalysis = an::RejectionClusterAnalysisRequest{.source = an::RejectionClusterSource::High,
                                                                    .minimumRejections = 1u,
                                                                    .minimumClusterPixels = 4u,
                                                                    .includeDiagonalNeighbours = true};
    const an::StackExecutionOutputs outputs{
        .frameRejectionRates = rates, .flaggedFrames = flagged, .clusters = clusters};
    const an::Result<an::StackResult> made = run(engine, model, frames, {}, execution, outputs);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    const an::StackExecutionReport& report = made->report;

    const an::ImageStorageBytes& high = made->maps[static_cast<std::size_t>(an::StackOutputMap::RejectionHigh)];
    const an::ImageStorageBytes& low = made->maps[static_cast<std::size_t>(an::StackOutputMap::RejectionLow)];
    ASSERT_TRUE(high.valid() && low.valid());
    std::uint64_t highSum = 0u;
    std::uint64_t lowSum = 0u;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            highSum += sampleAt<std::uint32_t>(high, 0u, x, y);
            lowSum += sampleAt<std::uint32_t>(low, 0u, x, y);
            const bool planted = y >= 2u && y < 6u && x >= 3u && x < 9u;
            if (planted)
            {
                EXPECT_GE(sampleAt<std::uint32_t>(high, 0u, x, y), 1u) << x << "," << y;
                EXPECT_LT(sampleAt<float>(made->image, 0u, x, y), 0.5f) << x << "," << y;
            }
        }
    }
    EXPECT_EQ(report.mappedHigh, highSum);
    EXPECT_EQ(report.mappedLow, lowSum);
    EXPECT_EQ(report.mappedHigh, 24u);
    EXPECT_EQ(report.mappedLow, 0u);
    EXPECT_EQ(report.rejectedHigh, 24u);

    // The engine's own scan of the maps the stack wrote reads the same totals the stack kept, and
    // finds the planted rectangle's elements and nothing else.
    std::vector<std::uint32_t> packedLow;
    std::vector<std::uint32_t> packedHigh;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            packedLow.push_back(sampleAt<std::uint32_t>(low, 0u, x, y));
            packedHigh.push_back(sampleAt<std::uint32_t>(high, 0u, x, y));
        }
    }
    {
        const an::ExecutionContext scan = makeExecution(engine);
        const std::optional<an::ImageExtent> plane =
            an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
        ASSERT_TRUE(plane.has_value());
        const an::Result<an::RejectionMapCounts> counted = an::countRejectionMaps(scan, packedLow, packedHigh, *plane);
        ASSERT_TRUE(counted.hasValue()) << counted.status().message();
        EXPECT_EQ(counted->lowRejections, report.mappedLow);
        EXPECT_EQ(counted->highRejections, report.mappedHigh);
        EXPECT_EQ(counted->highElements, 24u);
        EXPECT_EQ(counted->eitherElements, 24u);
        EXPECT_EQ(counted->lowElements, 0u);
    }

    // Every frame's admitted count is every sample; the planted frame rejects the most.
    EXPECT_TRUE(report.frameSampleCountsComplete);
    ASSERT_TRUE(report.rejectionSummarized);
    EXPECT_EQ(report.rejection.frameCount, kCount);
    EXPECT_EQ(report.rejection.ratedFrameCount, kCount);
    EXPECT_EQ(report.rejection.validSamples, static_cast<std::uint64_t>(kCount) * kWidth * kHeight);
    EXPECT_EQ(report.rejection.rejectedLow + report.rejection.rejectedHigh, report.mappedLow + report.mappedHigh);
    EXPECT_GT(report.rejection.maximumRate, 0.0);
    for (std::uint32_t index = 0u; index < kCount; ++index)
    {
        EXPECT_TRUE(rates[index].rated) << index;
        EXPECT_DOUBLE_EQ(rates[index].rate, index == kBad ? 24.0 / (kWidth * kHeight) : 0.0) << index;
    }
    EXPECT_DOUBLE_EQ(report.rejection.maximumRate, 24.0 / (kWidth * kHeight));
    EXPECT_DOUBLE_EQ(report.rejection.globalRate, 24.0 / (static_cast<double>(kCount) * kWidth * kHeight));
    ASSERT_TRUE(report.frameOutliersDetected);
    ASSERT_EQ(report.frameOutliers.flaggedFrameCount, 1u);
    EXPECT_EQ(flagged[0], kBad);
    EXPECT_EQ(flagged[1], 99u);

    ASSERT_TRUE(report.clustersAnalyzed);
    EXPECT_EQ(report.clusters.source, an::RejectionClusterSource::High);
    ASSERT_GE(report.clusters.clustersReported, 1u);
    EXPECT_EQ(clusters[0].source, an::RejectionClusterSource::High);
    EXPECT_LE(clusters[0].minX, 3u);
    EXPECT_GE(clusters[0].maxX, 8u);
    EXPECT_LE(clusters[0].minY, 2u);
    EXPECT_GE(clusters[0].maxY, 5u);
    EXPECT_GE(clusters[0].pixelCount, 24u);
    EXPECT_GT(report.diagnosticsWorkingBytes, 0u);

    // The coverage map counts survivors; the planted pixels lost one frame.
    const an::ImageStorageBytes& coverage = made->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)];
    ASSERT_TRUE(coverage.valid());
    EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 4u, 3u), kCount - 1u);
    EXPECT_TRUE(report.coverage.available);
    EXPECT_EQ(report.coverage.maximum, kCount);
    EXPECT_LT(report.coverage.mean, static_cast<double>(kCount));
    EXPECT_LT(report.effectiveFrameCount, static_cast<double>(kCount));

    // A caller that asks for no records gets the counts and no summaries.
    const an::Result<an::StackResult> bare = run(engine, model, frames, {}, {});
    ASSERT_TRUE(bare.hasValue()) << bare.status().message();
    EXPECT_FALSE(bare->report.clustersAnalyzed);
    EXPECT_TRUE(bare->report.rejectionSummarized);
    EXPECT_TRUE(bare->report.frameOutliersDetected);
    EXPECT_EQ(bare->report.frameOutliers.flaggedFrameCount, 1u);
    EXPECT_EQ(bitsOf(*bare), bitsOf(*made));
}

TEST(StackExecutorTest, WeighsFramesExplicitlyAndDerivesThemFromMeasurements)
{
    const an::EngineContext engine = makeEngine();
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    const std::array<double, kFrames> perFrame{1.0, 2.0, 3.0};

    an::StackRequest explicitRequest = baseRequest();
    explicitRequest.weighting.method = an::StackWeighting::Explicit;
    explicitRequest.output.weightSumMap = true;
    explicitRequest.output.signalToNoiseGainMap = true;
    const an::StackModel explicitModel = makeModel(explicitRequest);
    std::vector<double> table(kFrames, -1.0);
    an::StackExecutionRequest request;
    request.explicitWeights = an::StackFrameWeights{.perFrame = perFrame};
    const an::StackExecutionOutputs outputs{.frameWeights = table};
    const an::Result<an::StackResult> weighted = run(engine, explicitModel, frames, {}, request, outputs);
    ASSERT_TRUE(weighted.hasValue()) << weighted.status().message();
    EXPECT_EQ(table, std::vector<double>(perFrame.begin(), perFrame.end()));
    EXPECT_EQ(weighted->report.weighting.disposition, an::StackFrameWeightingDisposition::Supplied);
    EXPECT_EQ(weighted->report.weighting.source, an::StackWeighting::Explicit);
    EXPECT_EQ(weighted->report.weighting.frameCount, kFrames);
    EXPECT_DOUBLE_EQ(weighted->report.weighting.summary.maximum, 3.0);
    EXPECT_DOUBLE_EQ(weighted->report.weighting.summary.minimum, 1.0);
    const an::ImageStorageBytes& weightSum = weighted->maps[static_cast<std::size_t>(an::StackOutputMap::WeightSum)];
    ASSERT_TRUE(weightSum.valid());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_EQ(sampleAt<float>(weighted->image, 0u, x, y), weightedMeanOf(frames, perFrame, 0u, x, y))
                << x << "," << y;
            EXPECT_EQ(sampleAt<float>(weightSum, 0u, x, y), 6.0f);
        }
    }
    // The effective count of weights 1, 2, 3 is 36 / 14.
    EXPECT_NEAR(weighted->report.effectiveFrameCount, 36.0 / 14.0, 1e-6);
    EXPECT_NEAR(weighted->report.integrationEfficiency, 36.0 / 14.0 / 3.0, 1e-6);

    // The same weights derived from noise measurements go through the weighting operation and
    // land in the same table, normalized to mean one, which combines identically.
    an::StackRequest derivedRequest = baseRequest();
    derivedRequest.weighting.method = an::StackWeighting::Noise;
    const an::StackModel derivedModel = makeModel(derivedRequest);
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        measurements[index].backgroundNoise = 1.0 / std::sqrt(perFrame[index]);
    }
    std::vector<double> derivedTable(kFrames, -1.0);
    an::StackExecutionRequest derived;
    derived.measurements = measurements;
    const an::StackExecutionOutputs derivedOutputs{.frameWeights = derivedTable};
    const an::Result<an::StackResult> fromMeasurements = run(engine, derivedModel, frames, {}, derived, derivedOutputs);
    ASSERT_TRUE(fromMeasurements.hasValue()) << fromMeasurements.status().message();
    EXPECT_EQ(fromMeasurements->report.weighting.disposition, an::StackFrameWeightingDisposition::Computed);
    EXPECT_EQ(fromMeasurements->report.weighting.measuredFrameCount, kFrames);
    std::vector<double> expected(kFrames, -1.0);
    {
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackFrameWeightingResult> direct =
            an::computeStackFrameWeights(execution, derivedModel, measurements, expected);
        ASSERT_TRUE(direct.hasValue()) << direct.status().message();
    }
    EXPECT_EQ(derivedTable, expected);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_EQ(sampleAt<float>(fromMeasurements->image, 0u, x, y), weightedMeanOf(frames, expected, 0u, x, y))
                << x << "," << y;
        }
    }
}

TEST(StackExecutorTest, MeasuresAGlobalMatchOrTakesAPreparedOne)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::AdditiveScaling;
    request.normalization.referenceFrame = 1u;
    const an::StackModel model = makeModel(request);
    const an::ImageDescriptor descriptor = makeDescriptor();

    // Frames that differ from the reference by a level and a spread.
    std::vector<OwnedFrame> frames(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        frames[index].create(an::SampleType::Float32, kWidth, kHeight, 1u);
        frames[index].frameIndex = index;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const double noise = patterned(x + y * kWidth, 5u) - 0.475;
                frames[index].put(0u, x, y, 0.3 + 0.05 * index + (0.1 + 0.03 * index) * noise);
            }
        }
    }

    std::vector<an::FrameNormalization> matches(kFrames);
    const an::StackExecutionOutputs outputs{.frameNormalization = matches};
    const an::Result<an::StackResult> measured = run(engine, model, frames, {}, {}, outputs);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_TRUE(measured->report.normalizationMeasured);
    EXPECT_TRUE(measured->report.stages.normalizeGlobally);
    EXPECT_EQ(measured->report.plan.passes.count, 3u);
    EXPECT_EQ(measured->report.plan.passes.passes[0].kind, an::StackPassKind::MeasureNormalization);
    EXPECT_EQ(measured->report.frameReads, 2ull * kFrames);
    EXPECT_EQ(matches[1].channels[0].scale, 1.0);
    EXPECT_EQ(matches[1].channels[0].offset, 0.0);
    EXPECT_NE(matches[0].channels[0].scale, 1.0);
    EXPECT_EQ(matches[0].method, an::NormalizationMethod::AdditiveScaling);

    // The matches are the measurement's own, and the output is the normalized frames averaged.
    {
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::NormalizationFrameStatistics> referenceStatistics = an::measureGlobalNormalization(
            execution, frames[1].normalizationView(descriptor), request.normalization.scaleEstimator);
        ASSERT_TRUE(referenceStatistics.hasValue()) << referenceStatistics.status().message();
        const std::array<an::NormalizationFrameStatistics, 1u> members{*referenceStatistics};
        const an::Result<an::NormalizationReference> reference = an::combineNormalizationReferences(members);
        ASSERT_TRUE(reference.hasValue()) << reference.status().message();
        std::vector<OwnedFrame> normalized = frames;
        for (std::uint32_t index = 0u; index < kFrames; ++index)
        {
            if (index == 1u)
            {
                continue;
            }
            const an::Result<an::NormalizationFrameStatistics> statistics = an::measureGlobalNormalization(
                execution, frames[index].normalizationView(descriptor), request.normalization.scaleEstimator);
            ASSERT_TRUE(statistics.hasValue()) << statistics.status().message();
            const an::Result<an::FrameNormalization> match =
                an::estimateGlobalNormalization(*reference, *statistics, an::NormalizationMethod::AdditiveScaling);
            ASSERT_TRUE(match.hasValue()) << match.status().message();
            EXPECT_EQ(matches[index].channels[0].scale, match->channels[0].scale) << index;
            EXPECT_EQ(matches[index].channels[0].offset, match->channels[0].offset) << index;
            const an::Result<an::NormalizationApplyResult> applied = an::applyGlobalNormalization(
                execution, *match, frames[index].normalizationView(descriptor), normalized[index].bytes(),
                normalized[index].layout->totalBytes(), *normalized[index].layout);
            ASSERT_TRUE(applied.hasValue()) << applied.status().message();
        }
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_EQ(sampleAt<float>(measured->image, 0u, x, y), meanOf(normalized, 0u, x, y)) << x << "," << y;
            }
        }
    }

    // The same matches supplied by the contract skip the measurement pass and land the same bits.
    const an::StackSourceContract prepared{.normalization = matches};
    std::vector<an::FrameNormalization> echoed(kFrames);
    const an::StackExecutionOutputs preparedOutputs{.frameNormalization = echoed};
    const an::Result<an::StackResult> supplied = run(engine, model, frames, prepared, {}, preparedOutputs);
    ASSERT_TRUE(supplied.hasValue()) << supplied.status().message();
    EXPECT_FALSE(supplied->report.normalizationMeasured);
    EXPECT_EQ(supplied->report.plan.passes.count, 2u);
    EXPECT_EQ(supplied->report.plan.memory.preparedStateBytes,
              an::preparedStateBytesOf(std::span<const an::FrameNormalization>(matches)));
    EXPECT_EQ(supplied->report.frameReads, kFrames);
    EXPECT_EQ(echoed[0].channels[0].scale, matches[0].channels[0].scale);
    EXPECT_EQ(bitsOf(*supplied), bitsOf(*measured));
}

TEST(StackExecutorTest, FitsALocalDocumentOrTakesAPreparedOne)
{
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kWide = 96u;
    constexpr std::uint32_t kTall = 64u;
    const an::ImageDescriptor descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                          an::NumericDomain::NormalizedLinear, kWide, kTall);
    std::vector<OwnedFrame> frames(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        frames[index].create(an::SampleType::Float32, kWide, kTall, 1u);
        frames[index].frameIndex = index;
        for (std::uint32_t y = 0u; y < kTall; ++y)
        {
            for (std::uint32_t x = 0u; x < kWide; ++x)
            {
                const double noise = 0.02 * (patterned(x + y * kWide, 191u + index) - 0.475);
                const double sky = 0.20 + 0.03 * index + 0.05 * index * static_cast<double>(x) / 95.0;
                frames[index].put(0u, x, y, sky + (1.0 + 0.2 * index) * noise);
            }
        }
    }
    an::StackExecutionRequest request;
    request.localNormalization.gridSize = 2u;
    request.localNormalization.backgroundMinRoiExtent = 16u;
    request.localNormalization.scaleSamples = 512u;
    request.fitLocalScaleGrid = true;

    for (const an::StackNormalizationScope scope :
         {an::StackNormalizationScope::Local, an::StackNormalizationScope::LocalThenGlobal})
    {
        an::StackRequest stack = baseRequest(descriptor);
        stack.normalization.scope = scope;
        stack.normalization.referenceFrame = 1u;
        if (scope == an::StackNormalizationScope::LocalThenGlobal)
        {
            stack.normalization.globalMethod = an::NormalizationMethod::AdditiveScaling;
        }
        const an::StackModel model = makeModel(stack);

        an::LocalNormalizationDocument document;
        const an::StackExecutionOutputs outputs{.localNormalization = &document};
        const an::Result<an::StackResult> fitted = run(engine, model, frames, {}, request, outputs);
        ASSERT_TRUE(fitted.hasValue()) << an::stackNormalizationScopeName(scope) << ": " << fitted.status().message();
        EXPECT_TRUE(fitted->report.localNormalizationFitted);
        EXPECT_EQ(fitted->report.localNormalizationBytes, an::preparedStateBytesOf(document));
        EXPECT_TRUE(fitted->report.stages.normalizeLocally);
        EXPECT_EQ(fitted->report.stages.normalizeGlobally, scope == an::StackNormalizationScope::LocalThenGlobal);
        EXPECT_EQ(fitted->report.plan.passes.count, 3u);
        EXPECT_EQ(fitted->report.frameReads, 2ull * kFrames);
        ASSERT_EQ(document.frames.size(), kFrames);
        EXPECT_EQ(document.referenceIndex, 1u);
        EXPECT_EQ(document.shape.gridSize, 2u);
        for (std::uint32_t index = 0u; index < kFrames; ++index)
        {
            EXPECT_EQ(document.frames[index].background.size(), document.shape.valueCount()) << index;
            EXPECT_EQ(document.frames[index].scaleGrid.size(), index == 1u ? 0u : document.shape.valueCount()) << index;
            EXPECT_EQ(document.frames[index].global.has_value(), scope == an::StackNormalizationScope::LocalThenGlobal)
                << index;
            if (document.frames[index].global.has_value())
            {
                EXPECT_EQ(document.frames[index].global->method, an::NormalizationMethod::AdditiveScaling);
                if (index == 1u)
                {
                    EXPECT_EQ(document.frames[index].global->channels[0].scale, 1.0);
                    EXPECT_EQ(document.frames[index].global->channels[0].offset, 0.0);
                }
            }
        }
        // The fitted surface brings the tilted frames toward the reference: the output's mean is
        // within a per cent of the reference's.
        double outputSum = 0.0;
        double referenceSum = 0.0;
        for (std::uint32_t y = 0u; y < kTall; ++y)
        {
            for (std::uint32_t x = 0u; x < kWide; ++x)
            {
                outputSum += sampleAt<float>(fitted->image, 0u, x, y);
                referenceSum += frames[1].value(0u, x, y);
            }
        }
        EXPECT_NEAR(outputSum / referenceSum, 1.0, 0.01) << an::stackNormalizationScopeName(scope);

        const an::StackSourceContract prepared{.localNormalization = &document};
        const an::Result<an::StackResult> supplied = run(engine, model, frames, prepared, request);
        ASSERT_TRUE(supplied.hasValue()) << supplied.status().message();
        EXPECT_FALSE(supplied->report.localNormalizationFitted);
        EXPECT_EQ(supplied->report.localNormalizationBytes, 0u);
        EXPECT_EQ(supplied->report.plan.passes.count, 2u);
        EXPECT_EQ(supplied->report.plan.memory.preparedStateBytes, an::preparedStateBytesOf(document));
        EXPECT_EQ(bitsOf(*supplied), bitsOf(*fitted)) << an::stackNormalizationScopeName(scope);
        EXPECT_EQ(liveBytes(engine), fitted->report.memory.handedOverBytes + supplied->report.memory.handedOverBytes);
    }
}

// A document measured on a plane of its own is applied through the frames' mapping onto that plane: the stack measures
// no normalization of its own and holds the document as prepared state, and it combines what a stack of the same frames
// combines when each is normalized beforehand where its pixels lie on the plane - read with `evaluateLocalGrid` there -
// in tiles whose rows begin anywhere on the plane.
TEST(StackExecutorTest, AppliesAMappedDocumentAndMeasuresNoNormalizationOfItsOwn)
{
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kPlaneWide = 13u;
    constexpr std::uint32_t kPlaneTall = 9u;
    constexpr double kScale = 2.0;
    constexpr std::uint32_t kWide = 26u;
    constexpr std::uint32_t kTall = 18u;
    const an::ImageDescriptor descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                          an::NumericDomain::NormalizedLinear, kWide, kTall);
    std::vector<OwnedFrame> frames(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        frames[index].create(an::SampleType::Float32, kWide, kTall, 1u, an::ValidityRule::FiniteOnly);
        frames[index].frameIndex = index;
        frames[index].fill(201u + index);
    }
    // Where no drop reached one frame.
    frames[2].at<float>(0u, 25u, 17u) = std::numeric_limits<float>::quiet_NaN();

    const an::LocalNormalizationGridShape shape{
        .gridSize = 3u, .width = kPlaneWide, .height = kPlaneTall, .channelCount = 1u};
    an::LocalNormalizationDocument document;
    document.request.gridSize = shape.gridSize;
    document.request.interpolation = an::LocalGridInterpolation::Bicubic;
    document.shape = shape;
    document.validity = an::ValidityRule::ZeroIsNoData;
    document.referenceIndex = 1u;
    const auto grid = [&shape](double base, std::uint32_t salt) {
        std::vector<double> values(static_cast<std::size_t>(shape.valueCount()), 0.0);
        for (std::size_t node = 0u; node < values.size(); ++node)
        {
            values[node] = base + 0.04 * (patterned(node, salt) - 0.5);
        }
        return values;
    };
    document.referenceBackground = grid(0.20, 211u);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        an::LocalNormalizationFrameCoefficients record;
        record.frameIndex = index;
        record.background = index == 1u ? document.referenceBackground : grid(0.25 + 0.02 * index, 221u + index);
        record.scale[0] = index == 1u ? 1.0 : 0.9 + 0.1 * index;
        document.frames.push_back(std::move(record));
    }
    an::StackRequest stack = baseRequest(descriptor);
    stack.normalization.scope = an::StackNormalizationScope::Local;
    stack.normalization.referenceFrame = 1u;
    const an::StackModel model = makeModel(stack);
    const an::StackPlaneMapping mapping{.scale = kScale, .frameValidity = an::ValidityRule::FiniteOnly};
    an::StackExecutionRequest request;
    request.maximumTileRows = 5u;

    const an::Result<an::StackResult> mapped =
        run(engine, model, frames, {.localNormalization = &document, .localNormalizationPlane = mapping}, request);
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_FALSE(mapped->report.normalizationMeasured);
    EXPECT_FALSE(mapped->report.localNormalizationFitted);
    EXPECT_TRUE(mapped->report.stages.normalizeLocally && mapped->report.stages.normalizationPrepared);
    EXPECT_EQ(mapped->report.plan.memory.preparedStateBytes, an::preparedStateBytesOf(document));
    EXPECT_GT(mapped->report.plan.tile.tileCount, 1u);

    // The oracle: every frame normalized beforehand where its pixels lie on the plane, and stacked as it is.
    std::vector<OwnedFrame> normalized(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        normalized[index].create(an::SampleType::Float32, kWide, kTall, 1u, an::ValidityRule::FiniteOnly);
        normalized[index].frameIndex = index;
        const an::LocalNormalizationFrameCoefficients& record = document.frames[index];
        for (std::uint32_t y = 0u; y < kTall; ++y)
        {
            for (std::uint32_t x = 0u; x < kWide; ++x)
            {
                const float sample = frames[index].at<float>(0u, x, y);
                if (index == 1u || std::isnan(sample))
                {
                    normalized[index].at<float>(0u, x, y) = sample;
                    continue;
                }
                const double column = (static_cast<double>(x) + 0.5 + 0.0) / kScale - 0.5;
                const double row = (static_cast<double>(y) + 0.5 + 0.0) / kScale - 0.5;
                const an::Result<double> target =
                    an::evaluateLocalGrid(shape, record.background, document.request.interpolation, 0u, column, row);
                const an::Result<double> reference = an::evaluateLocalGrid(
                    shape, document.referenceBackground, document.request.interpolation, 0u, column, row);
                ASSERT_TRUE(target.hasValue() && reference.hasValue());
                normalized[index].at<float>(0u, x, y) =
                    static_cast<float>(record.scale[0] * (static_cast<double>(sample) - *target) + *reference);
            }
        }
    }
    an::StackRequest plainStack = baseRequest(descriptor);
    plainStack.normalization.referenceFrame = 1u;
    const an::Result<an::StackResult> expected = run(engine, makeModel(plainStack), normalized, {}, request);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    EXPECT_EQ(bitsOf(*mapped), bitsOf(*expected));
}

TEST(StackExecutorTest, MeasuresLocalWeightsAndComposesThemLikeTheOperations)
{
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t kWide = 40u;
    constexpr std::uint32_t kTall = 24u;
    const an::ImageDescriptor descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                          an::NumericDomain::NormalizedLinear, kWide, kTall);
    an::StackRequest request = baseRequest(descriptor);
    request.localWeighting.enabled = true;
    request.localWeighting.gridSpacing = 8u;
    request.localWeighting.windowSize = 5u;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    std::vector<OwnedFrame> frames(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        frames[index].create(an::SampleType::Float32, kWide, kTall, 1u);
        frames[index].frameIndex = index;
        for (std::uint32_t y = 0u; y < kTall; ++y)
        {
            for (std::uint32_t x = 0u; x < kWide; ++x)
            {
                const double noise = (0.01 + 0.03 * index) * (patterned(x + y * kWide, 17u + index) - 0.475);
                const double signal = 0.2 + 0.4 * std::exp(-0.02 * ((x - 20.0) * (x - 20.0) + (y - 12.0) * (y - 12.0)));
                frames[index].put(0u, x, y, signal + noise);
            }
        }
    }
    an::StackExecutionRequest capped;
    capped.maximumTileRows = 7u;
    const an::Result<an::StackResult> made = run(engine, model, frames, {}, capped);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_TRUE(made->report.localWeightsMeasured);
    EXPECT_EQ(made->report.plan.passes.count, 3u);
    EXPECT_EQ(made->report.plan.passes.passes[0].kind, an::StackPassKind::MeasureLocalWeights);
    EXPECT_EQ(made->report.plan.memory.tileBytesPerRow, static_cast<std::uint64_t>(kWide) * sizeof(float));
    EXPECT_EQ(made->report.localWeighting.frameCount, kFrames);
    EXPECT_EQ(made->report.localWeighting.gridSpacing, 8u);
    EXPECT_GT(made->report.localWeighting.weightedFrameNodes, 0u);
    EXPECT_LT(made->report.localWeighting.minimumWeight, 1.0);

    // The same operations run by hand: measure every frame, finalize the grids, fill every band's
    // weights, accumulate every frame and finalize the method.
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());
    const std::size_t perFrame = static_cast<std::size_t>(shape->valuesPerFrame());
    std::vector<float> grids(perFrame * kFrames, 0.0f);
    std::vector<std::byte> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames)) + 64u);
    const std::span<std::byte> alignedWorkspace =
        std::span<std::byte>(workspace).subspan((64u - reinterpret_cast<std::uintptr_t>(workspace.data()) % 64u) % 64u);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        const an::Result<an::LocalWeightMeasurement> measured =
            an::measureLocalWeightGrid(execution, model, frames[index].localWeightSource(), alignedWorkspace,
                                       std::span<float>(grids).subspan(index * perFrame, perFrame));
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    }
    const an::Result<an::LocalWeightingSummary> finalized =
        an::finalizeLocalWeightGrids(execution, model, grids, alignedWorkspace);
    ASSERT_TRUE(finalized.hasValue()) << finalized.status().message();
    EXPECT_EQ(finalized->weightedFrameNodes, made->report.localWeighting.weightedFrameNodes);
    EXPECT_EQ(finalized->minimumWeight, made->report.localWeighting.minimumWeight);
    const std::size_t elements = static_cast<std::size_t>(kWide) * kTall;
    std::vector<double> sums(elements, 0.0);
    std::vector<std::uint32_t> counts(elements, 0u);
    std::vector<double> weightSums(elements, 0.0);
    const an::StackAccumulatorSet set{.sums = sums, .counts = counts, .weightSums = weightSums};
    std::vector<float> weights(elements, 0.0f);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        const an::Status filled = an::fillLocalWeightBand(
            *shape, std::span<const float>(grids).subspan(index * perFrame, perFrame), 0u, kTall, weights);
        ASSERT_TRUE(filled.ok()) << filled.message();
        const an::Result<an::StackAccumulationOutcome> accumulated =
            an::accumulateStackFrame(execution, model, set, frames[index].frameBand(weights), {});
        ASSERT_TRUE(accumulated.hasValue()) << accumulated.status().message();
    }
    std::vector<float> image(elements, -1.0f);
    std::vector<float> weightSumMap(elements, -1.0f);
    const an::StackFinalizeTargets targets{.outputSampleType = an::SampleType::Float32,
                                           .outputImage = std::as_writable_bytes(std::span<float>(image)),
                                           .outputMask = nullptr,
                                           .weightSum = weightSumMap};
    const an::Result<an::StackMethodFinalizeOutcome> finalizedMethod =
        an::finalizeStackMethod(execution, model, an::kDefaultStackSumDomain, set, targets);
    ASSERT_TRUE(finalizedMethod.hasValue()) << finalizedMethod.status().message();
    const an::ImageStorageBytes& map = made->maps[static_cast<std::size_t>(an::StackOutputMap::WeightSum)];
    ASSERT_TRUE(map.valid());
    for (std::uint32_t y = 0u; y < kTall; ++y)
    {
        for (std::uint32_t x = 0u; x < kWide; ++x)
        {
            EXPECT_EQ(sampleAt<float>(made->image, 0u, x, y), image[y * kWide + x]) << x << "," << y;
            EXPECT_EQ(sampleAt<float>(map, 0u, x, y), weightSumMap[y * kWide + x]) << x << "," << y;
        }
    }
}

TEST(StackExecutorTest, CancelsAndFailsWithNothingHandedOver)
{
    const an::EngineContext engine = makeEngine();
    const an::StackModel model = makeModel(baseRequest());
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);

    // A stop requested before the run takes nothing.
    {
        std::stop_source stop;
        stop.request_stop();
        const an::Result<an::StackResult> made =
            run(engine, model, frames, {}, {}, {}, an::StackSourceAccess::WholeFrame,
                ExecutionSetup{.stopToken = stop.get_token()});
        EXPECT_EQ(made.status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(liveBytes(engine), 0u);
    }
    // A stop requested by the second read lands within one band of it.
    {
        std::stop_source stop;
        const an::ExecutionContext execution = makeExecution(engine, ExecutionSetup{.stopToken = stop.get_token()});
        StoppingSource source(makeSource(frames), stop, 2u);
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::Cancelled);
        EXPECT_EQ(source.reads, 2u);
        EXPECT_EQ(liveBytes(engine), 0u);
    }
    // A deadline already past is exceeded rather than cancelled.
    {
        const an::Result<an::StackResult> made =
            run(engine, model, frames, {}, {}, {}, an::StackSourceAccess::WholeFrame,
                ExecutionSetup{.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1)});
        EXPECT_EQ(made.status().code(), an::ErrorCode::DeadlineExceeded);
        EXPECT_EQ(liveBytes(engine), 0u);
    }
    // A read that fails fails the stack, with the read as the cause and the frame named.
    {
        const an::ExecutionContext execution = makeExecution(engine);
        FailingSource source(makeSource(frames), 1u, an::ErrorCode::InvalidData);
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidData);
        EXPECT_EQ(countField(made.status(), an::ContextKey::Requested), 1u);
        EXPECT_TRUE(messageNames(made.status(), "frame 1")) << made.status().message();
        ASSERT_NE(made.status().detail(), nullptr);
        EXPECT_EQ(std::string_view(made.status().detail()->operation), "an::executeStack");
        EXPECT_EQ(source.reads, 2u);
        EXPECT_EQ(liveBytes(engine), 0u);
    }
    // A budget that cannot hold the plan is refused before anything is taken.
    {
        const an::EngineContext small = makeEngine(2u, 256u);
        const an::Result<an::StackResult> made = run(small, model, frames);
        EXPECT_EQ(made.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_TRUE(countField(made.status(), an::ContextKey::Requested).has_value());
        EXPECT_EQ(liveBytes(small), 0u);
        EXPECT_EQ(small.memoryResource()->counters().deniedAllocations, 0u);
    }
}

TEST(StackExecutorTest, RetainsMaskedHistoryBandsUntilEveryFrameIsCombined)
{
    for (const an::StackSourceAccess access : {an::StackSourceAccess::WholeFrame, an::StackSourceAccess::Region})
    {
        for (const an::StackCombination combination :
             {an::StackCombination::Median, an::StackCombination::AutoAdaptiveWeightedAverage,
              an::StackCombination::Average})
        {
            SCOPED_TRACE(static_cast<int>(access));
            SCOPED_TRACE(static_cast<int>(combination));
            const an::EngineContext engine = makeEngine(1u);
            an::StackRequest request = baseRequest();
            request.combination = combination;
            request.output.coverageMap = true;
            if (combination == an::StackCombination::Average)
            {
                request.rejection.method = an::StackRejection::SigmaClipping;
            }
            const an::StackModel model = makeModel(request);
            std::vector<OwnedFrame> frames = makeFrames(kFrames);
            for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
            {
                frames[frame].descriptor = ruleOf(an::ValidityRule::ExplicitMask);
                frames[frame].addMask(1u);
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        frames[frame].put(0u, x, y, 0.1 + 0.4 * frame);
                    }
                }
            }
            frames[0].clearBit(0u, 2u, 2u);
            frames[1].clearBit(0u, 6u, 6u);
            an::StackExecutionRequest executionRequest;
            executionRequest.maximumTileRows = 5u;
            const an::Result<an::StackResult> result = run(engine, model, frames, {}, executionRequest, {}, access);
            ASSERT_TRUE(result.hasValue()) << result.status().message();
            EXPECT_NEAR(sampleAt<float>(result->image, 0u, 2u, 2u), 0.7f, 1e-6f);
            EXPECT_NEAR(sampleAt<float>(result->image, 0u, 6u, 6u), 0.5f, 1e-6f);
            const auto& coverage = result->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)];
            EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 2u, 2u), 2u);
            EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 6u, 6u), 2u);
        }
    }
}

TEST(StackExecutorTest, TranslatesSourceExceptionsWithFrameAndCause)
{
    class ThrowingSource final : public an::StackFrameSource
    {
    public:
        an::StackSourceAccess access() const noexcept override
        {
            return an::StackSourceAccess::WholeFrame;
        }
        an::Result<an::StackSourceBand> read(const an::ExecutionContext&, std::uint32_t, std::uint32_t, std::uint32_t,
                                             std::span<std::byte>) override
        {
            throw std::runtime_error("decoder failed on corrupt data");
        }
    } source;
    const an::EngineContext engine = makeEngine(1u);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest());
    EXPECT_NO_THROW({
        const an::Result<an::StackResult> result = an::executeStack(engine, execution, model, source, {}, {}, {});
        EXPECT_EQ(result.status().code(), an::ErrorCode::ExternalFailure);
        EXPECT_EQ(countField(result.status(), an::ContextKey::Requested), 0u);
        EXPECT_TRUE(messageNames(result.status(), "decoder failed on corrupt data"));
        EXPECT_EQ(result.status().cause().code(), an::ErrorCode::ExternalFailure);
    });
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackExecutorTest, IncludesNormalizationScratchInThePlan)
{
    const an::EngineContext engine = makeEngine(1u);
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    const an::Result<an::StackResult> result = run(engine, model, frames);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const std::uint64_t peak = engine.memoryResource()->counters().peakBytes;
    EXPECT_LE(peak, result->report.plan.memory.peakBytes);
    EXPECT_EQ(peak, result->report.memory.peakHeldBytes);
}

TEST(StackExecutorTest, RefusesUnbudgetedNormalizationBeforeReading)
{
    const an::EngineContext engine = makeEngine(1u);
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    FailingSource source(makeSource(frames), 0u, an::ErrorCode::InvalidData);
    an::StackExecutionRequest executionRequest;
    executionRequest.workingSetLimitBytes = 65536u;
    const an::Result<an::StackResult> result =
        an::executeStack(engine, execution, model, source, {}, executionRequest, {});
    EXPECT_EQ(result.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(source.reads, 0u);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackExecutorTest, BudgetsFittedLocalNormalizationAndSmoothing)
{
    for (const an::StackNormalizationScope scope :
         {an::StackNormalizationScope::Local, an::StackNormalizationScope::LocalThenGlobal})
    {
        const an::EngineContext engine = makeEngine(2u);
        const auto descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                               an::NumericDomain::NormalizedLinear, 96u, 64u);
        an::StackRequest request = baseRequest(descriptor);
        request.normalization.scope = scope;
        if (scope == an::StackNormalizationScope::LocalThenGlobal)
        {
            request.normalization.globalMethod = an::NormalizationMethod::AdditiveScaling;
        }
        const an::StackModel model = makeModel(request);
        const auto frames = makeFrames(kFrames, an::SampleType::Float32, 1u, 96u, 64u);
        an::StackExecutionRequest executionRequest;
        executionRequest.localNormalization.gridSize = 2u;
        executionRequest.localNormalization.backgroundMinRoiExtent = 16u;
        executionRequest.localNormalization.scaleSamples = 512u;
        executionRequest.localNormalization.backgroundSmoothingRadius = 2u;
        executionRequest.localNormalization.localScaleSmoothingRadius = 2u;
        executionRequest.fitLocalScaleGrid = true;
        const auto result = run(engine, model, frames, {}, executionRequest);
        ASSERT_TRUE(result.hasValue()) << result.status().message();
        const auto& memory = result->report.plan.memory;
        EXPECT_GT(memory.fittedNormalizationBytes, 0u);
        EXPECT_GE(memory.fittedNormalizationBytes, result->report.localNormalizationBytes);
        EXPECT_LE(engine.memoryResource()->counters().peakBytes, result->report.memory.peakHeldBytes);
        EXPECT_LE(result->report.memory.peakHeldBytes, memory.peakBytes);

        const an::EngineContext bounded = makeEngine(2u);
        const auto execution = makeExecution(bounded);
        FailingSource source(makeSource(frames), 0u, an::ErrorCode::InvalidData);
        executionRequest.workingSetLimitBytes = result->report.plan.passes.passes[0].workingSetBytes - 1u;
        const auto refused = an::executeStack(bounded, execution, model, source, {}, executionRequest, {});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(source.reads, 0u);
        EXPECT_EQ(liveBytes(bounded), 0u);
    }
}

TEST(StackExecutorTest, CountsPhotometricPreparationScratch)
{
    const an::EngineContext engine = makeEngine(1u);
    auto request = baseRequest(
        makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono, an::NumericDomain::NormalizedLinear, 1u, 1u),
        8u);
    request.weighting.method = an::StackWeighting::Photometric;
    const auto model = makeModel(request);
    const auto frames = makeFrames(8u, an::SampleType::Float32, 1u, 1u, 1u);
    std::array<an::StackFrameMeasurement, 8u> measurements{};
    for (std::size_t frame = 0u; frame < measurements.size(); ++frame)
    {
        measurements[frame].photometric.zeroPointMagnitudes = 20.0 + 0.1 * static_cast<double>(frame);
        measurements[frame].photometric.zeroPointStandardUncertaintyMagnitudes = 0.01;
    }
    an::StackExecutionRequest executionRequest;
    executionRequest.measurements = measurements;
    const auto result = run(engine, model, frames, {}, executionRequest);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_GT(result->report.plan.memory.initializationScratchBytes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, result->report.memory.peakHeldBytes);
    EXPECT_LE(result->report.memory.peakHeldBytes, result->report.plan.memory.peakBytes);
}

TEST(StackExecutorTest, BudgetsFinalClusterScratchWithSmallTiles)
{
    const an::EngineContext engine = makeEngine(1u);
    auto request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                              an::NumericDomain::NormalizedLinear, 128u, 128u),
                               8u);
    request.rejection.method = an::StackRejection::SigmaClipping;
    request.output.rejectionMaps = an::RejectionMapForm::Merged;
    const auto model = makeModel(request);
    const auto frames = makeFrames(8u, an::SampleType::Float32, 1u, 128u, 128u);
    std::array<an::RejectionClusterDetection, 8u> clusters{};
    an::StackExecutionRequest executionRequest;
    executionRequest.maximumTileRows = 1u;
    executionRequest.clusterAnalysis = an::RejectionClusterAnalysisRequest{};
    const auto result =
        run(engine, model, frames, {}, executionRequest, {.clusters = clusters}, an::StackSourceAccess::Region);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_TRUE(result->report.clustersAnalyzed);
    EXPECT_GT(result->report.plan.memory.diagnosticsScratchBytes, 128u * 128u * sizeof(std::uint32_t));
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, result->report.memory.peakHeldBytes);
    EXPECT_LE(result->report.memory.peakHeldBytes, result->report.plan.memory.peakBytes);
}

TEST(StackExecutorTest, ReportsProgressDiagnosticsAndTraces)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    RecordingProgressSink progress;
    RecordingDiagnosticSink diagnostics;
    RecordingTraceSink traces;
    an::StackExecutionRequest capped;
    capped.maximumTileRows = 4u;
    const an::Result<an::StackResult> made =
        run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region,
            ExecutionSetup{.progressSink = &progress, .diagnosticSink = &diagnostics, .traceSink = &traces});
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    const std::uint64_t total = made->report.plan.io.frameReads + 1u;
    EXPECT_EQ(made->report.frameReads, made->report.plan.io.frameReads);

    const std::vector<RecordingProgressSink::Report> reports = progress.reports();
    ASSERT_GE(reports.size(), total);
    std::uint64_t previous = 0u;
    bool sawMeasure = false;
    bool sawCombine = false;
    bool sawFinalize = false;
    for (const RecordingProgressSink::Report& report : reports)
    {
        EXPECT_GE(report.completed, previous);
        EXPECT_EQ(report.total, total);
        previous = report.completed;
        sawMeasure = sawMeasure || report.stage == "MeasureNormalization";
        sawCombine = sawCombine || report.stage == "Combine";
        sawFinalize = sawFinalize || report.stage == "Finalize";
    }
    EXPECT_EQ(reports.back().completed, total);
    EXPECT_TRUE(sawMeasure && sawCombine && sawFinalize);

    bool plannerSpoke = false;
    bool setSpoke = false;
    bool executorSpoke = false;
    for (const RecordingDiagnosticSink::Event& event : diagnostics.events())
    {
        plannerSpoke = plannerSpoke || event.operation == "an::planStack";
        setSpoke = setSpoke || event.operation == "StackWorkingSet::beginPass";
        executorSpoke = executorSpoke || (event.operation == "an::executeStack" &&
                                          event.message.find("frame reads") != std::string::npos);
    }
    EXPECT_TRUE(plannerSpoke && setSpoke && executorSpoke);

    bool sawReads = false;
    bool sawTiles = false;
    for (const RecordingTraceSink::Event& event : traces.events())
    {
        if (event.kind != an::TraceEventKind::Counter || event.category != "stacking")
        {
            continue;
        }
        if (event.name == "frameReads")
        {
            sawReads = true;
            EXPECT_EQ(event.value, static_cast<double>(made->report.frameReads));
        }
        if (event.name == "tilesCombined")
        {
            sawTiles = true;
            EXPECT_EQ(event.value, static_cast<double>(made->report.tilesCombined));
        }
    }
    EXPECT_TRUE(sawReads && sawTiles);
}

TEST(StackExecutorTest, NormalizesASumToItsPeakOrLeavesItRaw)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest();
    request.combination = an::StackCombination::Sum;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);

    const an::Result<an::StackResult> normalized = run(engine, model, frames);
    ASSERT_TRUE(normalized.hasValue()) << normalized.status().message();
    EXPECT_EQ(normalized->descriptor.numericDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(normalized->report.sumDomain, an::StackSumDomain::NormalizedToPeak);
    EXPECT_TRUE(normalized->report.finalize.normalizedToPeak);
    EXPECT_GT(normalized->report.finalize.peakSum, 0.0);

    an::StackExecutionRequest raw;
    raw.sumDomain = an::StackSumDomain::Raw;
    const an::Result<an::StackResult> plain = run(engine, model, frames, {}, raw);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_EQ(plain->descriptor.numericDomain(), an::NumericDomain::PhysicalScaled);
    EXPECT_FALSE(plain->report.finalize.normalizedToPeak);
    float peak = 0.0f;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            double sum = 0.0;
            for (const OwnedFrame& frame : frames)
            {
                sum = sum + frame.value(0u, x, y) * 1.0;
            }
            EXPECT_EQ(sampleAt<float>(plain->image, 0u, x, y), static_cast<float>(sum)) << x << "," << y;
            peak = (std::max)(peak, sampleAt<float>(plain->image, 0u, x, y));
            EXPECT_EQ(sampleAt<float>(normalized->image, 0u, x, y),
                      static_cast<float>(sum / normalized->report.finalize.peakSum))
                << x << "," << y;
        }
    }
    EXPECT_EQ(peak, static_cast<float>(normalized->report.finalize.peakSum));

    // A raw sum of encoded samples has no domain and is refused before a frame is read.
    an::StackRequest encoded = baseRequest(
        makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono, an::NumericDomain::EncodedNonlinear));
    encoded.combination = an::StackCombination::Sum;
    const an::Result<an::StackResult> refused = run(engine, makeModel(encoded), frames, {}, raw);
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(liveBytes(engine), normalized->report.memory.handedOverBytes + plain->report.memory.handedOverBytes);
}

TEST(StackExecutorTest, KeepsTheExtremaAndTheAutoAdaptiveMean)
{
    const an::EngineContext engine = makeEngine();
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    for (const an::StackCombination combination : {an::StackCombination::Minimum, an::StackCombination::Maximum})
    {
        an::StackRequest request = baseRequest();
        request.combination = combination;
        const an::Result<an::StackResult> made = run(engine, makeModel(request), frames);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                float expected = static_cast<float>(frames[0].value(0u, x, y));
                for (const OwnedFrame& frame : frames)
                {
                    const float value = static_cast<float>(frame.value(0u, x, y));
                    expected = combination == an::StackCombination::Minimum ? (value < expected ? value : expected)
                                                                            : (expected < value ? value : expected);
                }
                EXPECT_EQ(sampleAt<float>(made->image, 0u, x, y), expected) << x << "," << y;
            }
        }
    }
    an::StackRequest adaptive = baseRequest();
    adaptive.combination = an::StackCombination::AutoAdaptiveWeightedAverage;
    const an::Result<an::StackResult> made = run(engine, makeModel(adaptive), frames);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_EQ(made->report.plan.strategy.reason, an::StackStrategyReason::AutoAdaptiveNeedsSampleHistory);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const float value = sampleAt<float>(made->image, 0u, x, y);
            float lowest = 1.0f;
            float highest = 0.0f;
            for (const OwnedFrame& frame : frames)
            {
                lowest = (std::min)(lowest, static_cast<float>(frame.value(0u, x, y)));
                highest = (std::max)(highest, static_cast<float>(frame.value(0u, x, y)));
            }
            EXPECT_GE(value, lowest - 1e-6f) << x << "," << y;
            EXPECT_LE(value, highest + 1e-6f) << x << "," << y;
            EXPECT_TRUE(maskAt(made->mask, 0u, x, y));
        }
    }
}

TEST(StackExecutorTest, WritesFloat64ColourOutputWithPerChannelWeights)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    request.output.sampleType = an::SampleType::Float64;
    request.output.varianceMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames, an::SampleType::Float32, 3u);
    const std::array<double, kFrames * 3u> perFrameChannel{1.0, 2.0, 3.0, 2.0, 2.0, 2.0, 3.0, 1.0, 0.5};
    an::StackExecutionRequest execution;
    execution.explicitWeights = an::StackFrameWeights{.perFrameChannel = perFrameChannel};
    const an::Result<an::StackResult> made = run(engine, model, frames, {}, execution);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_EQ(made->descriptor.sampleType(), an::SampleType::Float64);
    EXPECT_EQ(made->descriptor.channelSchema(), an::ChannelSchema::Rgb);
    const an::ImageStorageBytes& variance = made->maps[static_cast<std::size_t>(an::StackOutputMap::Variance)];
    ASSERT_TRUE(variance.valid());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                double sum = 0.0;
                double weightSum = 0.0;
                for (std::uint32_t index = 0u; index < kFrames; ++index)
                {
                    const double weight = perFrameChannel[index * 3u + channel];
                    sum = sum + frames[index].value(channel, x, y) * weight;
                    weightSum = weightSum + weight;
                }
                EXPECT_EQ(sampleAt<double>(made->image, channel, x, y), sum / weightSum)
                    << channel << ":" << x << "," << y;
                EXPECT_TRUE(maskAt(made->mask, channel, x, y));
                EXPECT_GE(sampleAt<float>(variance, channel, x, y), 0.0f);
            }
        }
    }
}

TEST(StackExecutorTest, HonoursEachFrameValidityRule)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest();
    request.output.coverageMap = true;
    const an::StackModel model = makeModel(request);
    std::vector<OwnedFrame> frames(kFrames);
    frames[0].create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
    frames[1].create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::ExplicitMask);
    frames[2].create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::FiniteOnly);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        frames[index].frameIndex = index;
        frames[index].fill(31u + index);
    }
    frames[1].addMask(1u);
    // One pixel nothing reaches, and one pixel each frame loses on its own.
    frames[0].put(0u, 2u, 2u, 0.0);
    frames[1].clearBit(0u, 2u, 2u);
    frames[2].at<float>(0u, 2u, 2u) = std::numeric_limits<float>::quiet_NaN();
    frames[0].put(0u, 5u, 5u, 0.0);
    frames[1].clearBit(0u, 6u, 6u);
    frames[2].at<float>(0u, 7u, 7u) = std::numeric_limits<float>::quiet_NaN();

    for (const an::StackSourceAccess access : {an::StackSourceAccess::WholeFrame, an::StackSourceAccess::Region})
    {
        an::StackExecutionRequest capped;
        capped.maximumTileRows = 5u;
        const an::Result<an::StackResult> made = run(engine, model, frames, {}, capped, {}, access);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        const an::ImageStorageBytes& coverage = made->maps[static_cast<std::size_t>(an::StackOutputMap::Coverage)];
        ASSERT_TRUE(coverage.valid());
        EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 2u, 2u), 0u);
        EXPECT_FALSE(maskAt(made->mask, 0u, 2u, 2u));
        EXPECT_EQ(sampleAt<float>(made->image, 0u, 2u, 2u), 0.0f);
        EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 5u, 5u), 2u);
        EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 6u, 6u), 2u);
        EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 7u, 7u), 2u);
        EXPECT_EQ(sampleAt<std::uint32_t>(coverage, 0u, 0u, 0u), 3u);
        EXPECT_EQ(sampleAt<float>(made->image, 0u, 5u, 5u),
                  static_cast<float>((frames[1].value(0u, 5u, 5u) + frames[2].value(0u, 5u, 5u)) / 2.0));
        EXPECT_EQ(sampleAt<float>(made->image, 0u, 6u, 6u),
                  static_cast<float>((frames[0].value(0u, 6u, 6u) + frames[2].value(0u, 6u, 6u)) / 2.0));
        const an::StackExecutionReport& report = made->report;
        EXPECT_EQ(report.elementsWithoutSamples, 1u);
        EXPECT_EQ(report.elementsWithSamples, static_cast<std::uint64_t>(kWidth) * kHeight - 1u);
        EXPECT_EQ(report.samplesExcludedByValidity, 6u);
        EXPECT_EQ(report.samplesAdmitted, 3ull * kWidth * kHeight - 6u);
        EXPECT_TRUE(report.coverage.available);
        EXPECT_EQ(report.coverage.minimum, 0u);
        EXPECT_EQ(report.coverage.maximum, 3u);
        EXPECT_DOUBLE_EQ(report.coverage.median, 3.0);
        EXPECT_NEAR(report.coverage.mean, (3.0 * 96.0 - 6.0) / 96.0, 1e-12);
        EXPECT_GT(report.coverage.standardDeviation, 0.0);
    }
}

TEST(StackExecutorTest, RefusesANaNTheZeroRuleAdmitsWhicheverBackendRuns)
{
    // Zero-is-no-data admits every sample but an exact zero, so a NaN in a frame that states it is
    // an admitted sample that is not finite, and a frame no stage transforms reaches the combine as
    // it is stored. An average, an extreme, a median and a rejection each refuse the stack whether
    // the reference rows run or the compiled rows the execution selects, rather than the compiled
    // rows leaving the NaN out as if it were a zero. The rows are wide enough that the NaN lies in
    // the vector body of every compiled row, beside a zero and a negative zero the rule excludes.
    constexpr std::uint32_t kWide = 40u;
    constexpr std::uint32_t kTall = 3u;
    const an::EngineContext engine = makeEngine();
    struct Shape
    {
        const char* name;
        an::StackCombination combination;
        an::StackRejection rejection;
    };
    constexpr std::array<Shape, 4u> kShapes{
        Shape{"average", an::StackCombination::Average, an::StackRejection::None},
        Shape{"maximum", an::StackCombination::Maximum, an::StackRejection::None},
        Shape{"median", an::StackCombination::Median, an::StackRejection::None},
        Shape{"sigma clipping", an::StackCombination::Average, an::StackRejection::SigmaClipping}};
    constexpr std::array<an::StackSourceAccess, 2u> kAccesses{an::StackSourceAccess::WholeFrame,
                                                              an::StackSourceAccess::Region};
    constexpr std::array<an::ComputeBackend, 2u> kBackends{an::ComputeBackend::Reference,
                                                           an::ComputeBackend::Automatic};
    for (const Shape& shape : kShapes)
    {
        an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                              an::NumericDomain::NormalizedLinear, kWide, kTall));
        request.combination = shape.combination;
        request.rejection.method = shape.rejection;
        const an::StackModel model = makeModel(request);
        std::vector<OwnedFrame> frames =
            makeFrames(kFrames, an::SampleType::Float32, 1u, kWide, kTall, an::ValidityRule::ZeroIsNoData);
        frames[0].put(0u, 5u, 1u, 0.0);
        frames[2].at<float>(0u, 6u, 2u) = -0.0f;
        for (const an::StackSourceAccess access : kAccesses)
        {
            for (const an::ComputeBackend backend : kBackends)
            {
                SCOPED_TRACE(std::string(shape.name) + " without the NaN, backend " +
                             std::to_string(static_cast<int>(backend)) + ", access " +
                             std::to_string(static_cast<int>(access)));
                const an::Result<an::StackResult> clean =
                    run(engine, model, frames, {}, {}, {}, access, {.backend = backend});
                ASSERT_TRUE(clean.hasValue()) << clean.status().message();
                EXPECT_EQ(clean->report.samplesExcludedByValidity, 2u);
                const an::BackendSelection& selected = clean->report.backend;
                const bool compiledTable = selected.backend == an::ComputeBackend::Cpu &&
                                           (selected.instructionSet == an::InstructionSet::Avx2 ||
                                            selected.instructionSet == an::InstructionSet::Avx512Skylake);
                EXPECT_EQ(clean->report.compiledRowUsed, compiledTable);
            }
        }

        frames[1].at<float>(0u, 1u, 0u) = std::numeric_limits<float>::quiet_NaN();
        for (const an::StackSourceAccess access : kAccesses)
        {
            for (const an::ComputeBackend backend : kBackends)
            {
                SCOPED_TRACE(std::string(shape.name) + ", backend " + std::to_string(static_cast<int>(backend)) +
                             ", access " + std::to_string(static_cast<int>(access)));
                const an::Result<an::StackResult> refused =
                    run(engine, model, frames, {}, {}, {}, access, {.backend = backend});
                ASSERT_FALSE(refused.hasValue()) << "the NaN was left out instead of refused";
                EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
                EXPECT_TRUE(messageNames(refused.status(), "finite")) << refused.status().message();
                EXPECT_EQ(liveBytes(engine), 0u);
            }
        }
    }
}

/// Every sample of a stored image or map, as words, for bit comparisons between runs.
[[nodiscard]] std::vector<std::uint8_t> storedBytesOf(const an::ImageStorageBytes& storage)
{
    if (!storage.valid())
    {
        return {};
    }
    const std::optional<an::ImageLayout> layout = storage.layout();
    EXPECT_TRUE(layout.has_value());
    const auto* first = static_cast<const std::uint8_t*>(storage.data());
    return std::vector<std::uint8_t>(first, first + layout->totalBytes());
}

TEST(StackExecutorTest, RegionAccessKeepsTermsForOneTileWhenEachTileIsFinalOnItsOwn)
{
    // Under region access every frame's band of a tile is folded in before the next tile is read,
    // so an unweighted mean or extremum is final for a tile's rows once the tile is combined. Such
    // a stack keeps its terms for one tile rather than the output and hands over the image, the
    // mask, the maps and the summaries of the whole-frame walk, which keeps them for the output -
    // at one and four workers, with and without a row cap, over widened frames some of whose
    // samples are no data.
    const an::EngineContext engine = makeEngine(4u);
    std::vector<OwnedFrame> frames =
        makeFrames(kFrames, an::SampleType::UInt16, 1u, kWidth, kHeight, an::ValidityRule::ZeroIsNoData);
    for (OwnedFrame& frame : frames)
    {
        frame.put(0u, 2u, 2u, 0.0);
    }
    frames[1].put(0u, 7u, 5u, 0.0);
    constexpr std::uint64_t kElements = static_cast<std::uint64_t>(kWidth) * kHeight;
    for (const an::StackCombination combination :
         {an::StackCombination::Average, an::StackCombination::Minimum, an::StackCombination::Maximum})
    {
        an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::UInt16));
        request.combination = combination;
        request.output.coverageMap = true;
        request.output.varianceMap = combination == an::StackCombination::Average;
        const an::StackModel model = makeModel(request);
        // Sums and counts, and the two variance moments when the map is asked for.
        const std::uint64_t perElement = 8u + 4u + (request.output.varianceMap ? 16u : 0u);
        const an::Result<an::StackResult> whole = run(engine, model, frames);
        ASSERT_TRUE(whole.hasValue()) << whole.status().message();
        EXPECT_EQ(whole->report.plan.memory.accumulatorBytes, kElements * perElement);
        const an::StackExecutionReport& expected = whole->report;
        for (const std::uint32_t workers : {1u, 4u})
        {
            for (const std::uint32_t cap : {0u, 3u})
            {
                an::StackExecutionRequest capped;
                capped.maximumTileRows = cap;
                const an::Result<an::StackResult> region =
                    run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region, {.concurrency = workers});
                ASSERT_TRUE(region.hasValue()) << region.status().message();
                const an::StackExecutionReport& report = region->report;
                const std::string where = std::string(an::stackCombinationName(combination)) + ", " +
                                          std::to_string(workers) + " workers, cap " + std::to_string(cap);
                EXPECT_EQ(report.plan.memory.accumulatorBytes,
                          static_cast<std::uint64_t>(report.plan.tile.rows) * kWidth * perElement)
                    << where;
                EXPECT_EQ(report.plan.tile.tileCount, cap == 0u ? 1u : 3u) << where;
                EXPECT_EQ(report.tilesCombined, report.plan.tile.tileCount) << where;
                EXPECT_EQ(bitsOf(*region), bitsOf(*whole)) << where;
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        EXPECT_EQ(maskAt(region->mask, 0u, x, y), maskAt(whole->mask, 0u, x, y))
                            << where << " at " << x << "," << y;
                    }
                }
                for (std::size_t map = 0u; map < an::kStackOutputMapCount; ++map)
                {
                    EXPECT_EQ(storedBytesOf(region->maps[map]), storedBytesOf(whole->maps[map]))
                        << where << ", map " << map;
                }
                EXPECT_EQ(report.finalize, expected.finalize) << where;
                EXPECT_EQ(report.elementsWithSamples, expected.elementsWithSamples) << where;
                EXPECT_EQ(report.elementsWithoutSamples, expected.elementsWithoutSamples) << where;
                EXPECT_EQ(report.elementsWithoutSamples, 1u) << where;
                EXPECT_EQ(report.nonFiniteOutputs, expected.nonFiniteOutputs) << where;
                EXPECT_EQ(report.samplesAdmitted, expected.samplesAdmitted) << where;
                EXPECT_EQ(report.samplesExcludedByValidity, expected.samplesExcludedByValidity) << where;
                EXPECT_TRUE(report.coverage.available) << where;
                EXPECT_EQ(report.coverage.elements, expected.coverage.elements) << where;
                EXPECT_EQ(report.coverage.elementsWithSamples, expected.coverage.elementsWithSamples) << where;
                EXPECT_EQ(report.coverage.minimum, expected.coverage.minimum) << where;
                EXPECT_EQ(report.coverage.maximum, expected.coverage.maximum) << where;
                EXPECT_EQ(report.coverage.mean, expected.coverage.mean) << where;
                EXPECT_EQ(report.coverage.median, expected.coverage.median) << where;
                EXPECT_EQ(report.coverage.standardDeviation, expected.coverage.standardDeviation) << where;
                EXPECT_EQ(report.effectiveFrameCount, expected.effectiveFrameCount) << where;
                EXPECT_TRUE(report.frameSampleCountsComplete) << where;
                EXPECT_EQ(report.memory.peakHeldBytes, report.plan.memory.peakBytes) << where;
            }
        }
    }

    // A sum may be divided by the output's peak, and a weighted mean's summary reads its weight
    // sums over the output, so both keep whole-output terms under region access too.
    an::StackRequest sum = baseRequest(makeDescriptor(an::SampleType::UInt16));
    sum.combination = an::StackCombination::Sum;
    an::StackRequest weighted = baseRequest(makeDescriptor(an::SampleType::UInt16));
    weighted.weighting.method = an::StackWeighting::Explicit;
    const std::array<double, kFrames> weights{1.0, 2.0, 0.5};
    an::StackExecutionRequest explicitWeights;
    explicitWeights.explicitWeights = an::StackFrameWeights{.perFrame = weights};
    explicitWeights.maximumTileRows = 3u;
    an::StackExecutionRequest capped;
    capped.maximumTileRows = 3u;
    const an::Result<an::StackResult> summed =
        run(engine, makeModel(sum), frames, {}, capped, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(summed.hasValue()) << summed.status().message();
    EXPECT_EQ(summed->report.plan.memory.accumulatorBytes, kElements * (8u + 4u));
    const an::Result<an::StackResult> weighed =
        run(engine, makeModel(weighted), frames, {}, explicitWeights, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(weighed.hasValue()) << weighed.status().message();
    EXPECT_EQ(weighed->report.plan.memory.accumulatorBytes, kElements * (8u + 4u + 8u));
}

TEST(StackExecutorTest, BorrowedRejectionBandsMatchStagedBandsAndTheirValidityCounts)
{
    const an::EngineContext engine = makeEngine(2u);
    for (const auto type : {an::SampleType::Float32, an::SampleType::Float64})
    {
        std::vector<OwnedFrame> frames = makeFrames(8u, type);
        frames[0].descriptor = ruleOf(an::ValidityRule::ZeroIsNoData);
        frames[0].put(0u, 2u, 2u, 0.0);
        frames[1].descriptor = ruleOf(an::ValidityRule::ExplicitMask);
        frames[1].addMask(1u);
        frames[1].clearBit(0u, 3u, 3u);
        frames[2].descriptor = ruleOf(an::ValidityRule::FiniteOnly);
        frames[2].put(0u, 4u, 4u, std::numeric_limits<double>::quiet_NaN());
        for (std::uint32_t mode = 1u; mode <= 7u; ++mode)
        {
            auto request = baseRequest(makeDescriptor(type));
            request.input.frameCount = 8u;
            request.rejection.method = static_cast<an::StackRejection>(mode);
            request.output.sampleType = type;
            request.output.coverageMap = true;
            request.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
            const auto model = makeModel(request);
            an::StackExecutionRequest capped;
            capped.maximumTileRows = 3u;
            const auto staged = run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::WholeFrame);
            const auto borrowed = run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region);
            ASSERT_TRUE(staged.hasValue());
            ASSERT_TRUE(borrowed.hasValue());
            const auto execution = makeExecution(engine);
            DecodingSource source(frames, an::StackSourceAccess::Region);
            const auto decoded = an::executeStack(engine, execution, model, source, {}, capped, {});
            ASSERT_TRUE(decoded.hasValue());
            for (const an::StackResult* result : {&*borrowed, &*decoded})
            {
                EXPECT_EQ(bitsOf(*result), bitsOf(*staged));
                EXPECT_EQ(result->report.samplesAdmitted, staged->report.samplesAdmitted);
                EXPECT_EQ(result->report.samplesExcludedByValidity, 3u);
                EXPECT_EQ(result->report.rejectedLow, staged->report.rejectedLow);
                EXPECT_EQ(result->report.rejectedHigh, staged->report.rejectedHigh);
                for (std::size_t i = 0u; i < staged->maps.size(); ++i)
                    if (staged->maps[i].valid())
                    {
                        ASSERT_TRUE(result->maps[i].valid());
                        EXPECT_EQ(std::memcmp(staged->maps[i].data(), result->maps[i].data(),
                                              static_cast<std::size_t>(staged->maps[i].layout()->totalBytes())),
                                  0);
                    }
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                        EXPECT_EQ(maskAt(result->mask, 0u, x, y), maskAt(staged->mask, 0u, x, y));
            }
        }
    }
}

TEST(StackExecutorTest, ConvertsInPlaceWhenTheSourceDecodesIntoTheSlot)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Multiplicative;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    std::vector<an::FrameNormalization> matches(kFrames);
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        matches[index].method = an::NormalizationMethod::Multiplicative;
        matches[index].estimator = request.normalization.scaleEstimator;
        matches[index].channelCount = 1u;
        matches[index].channels[0] =
            an::NormalizationChannelCoefficients{.scale = index == 0u ? 1.0 : 0.9 + 0.1 * index, .offset = 0.0};
    }
    const an::StackSourceContract contract{.normalization = matches};
    const an::Result<an::StackResult> resident = run(engine, model, frames, contract);
    ASSERT_TRUE(resident.hasValue()) << resident.status().message();
    EXPECT_TRUE(resident->report.stages.normalizeGlobally);
    EXPECT_FALSE(resident->report.stages.convert);
    EXPECT_EQ(resident->report.plan.memory.tileBytesPerRow, 0u);

    for (const an::StackSourceAccess access : {an::StackSourceAccess::WholeFrame, an::StackSourceAccess::Region})
    {
        const an::ExecutionContext execution = makeExecution(engine);
        DecodingSource source(frames, access);
        an::StackExecutionRequest capped;
        capped.maximumTileRows = 3u;
        const an::Result<an::StackResult> decoded =
            an::executeStack(engine, execution, model, source, contract, capped, {});
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        EXPECT_EQ(source.reads, decoded->report.frameReads);
        EXPECT_EQ(source.slotBands, source.reads);
        EXPECT_EQ(bitsOf(*decoded), bitsOf(*resident)) << an::stackSourceAccessName(access);
    }
    // The frames were never written: they still hold what the pattern put there.
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        EXPECT_EQ(frames[index].value(0u, 3u, 3u),
                  static_cast<double>(static_cast<float>(patterned(3u + 3u * kWidth, 11u + 7u * index))));
    }
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            double sum = 0.0;
            for (std::uint32_t index = 0u; index < kFrames; ++index)
            {
                const float normalized =
                    static_cast<float>(frames[index].value(0u, x, y) * matches[index].channels[0].scale);
                sum = sum + static_cast<double>(normalized) * 1.0;
            }
            EXPECT_EQ(sampleAt<float>(resident->image, 0u, x, y), static_cast<float>(sum / 3.0)) << x << "," << y;
        }
    }
}

TEST(StackExecutorTest, CalibratesRawFramesOnTheWayIn)
{
    const an::EngineContext engine = makeEngine();
    const an::ImageDescriptor description =
        makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono, an::NumericDomain::RawSensorCode);
    const an::StackModel model = makeModel(baseRequest(description));
    const an::ExecutionContext execution = makeExecution(engine);
    OwnedFrame bias;
    bias.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    bias.frameIndex = 0u;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            bias.put(0u, x, y, 0.02 + 0.001 * (x + y));
        }
    }
    const an::ImageDescriptor calibrationDescription = an::stackSourceCalibrationDescriptor(model);
    const an::CalibrationMasters masters{.bias = bias.calibrationView(calibrationDescription)};
    const an::Result<an::CalibrationPlan> planned =
        an::prepareImageCalibration(execution, calibrationDescription, masters, {});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const std::array<an::CalibrationPlan, 1u> plans{*planned};
    const an::StackSourceContract contract{
        .state = an::StackFrameState::Raw, .calibration = plans, .masters = &masters};
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    an::ResidentStackFrameSource source = makeSource(frames);
    const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, contract, {}, {});
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    EXPECT_TRUE(made->report.stages.calibrate);
    EXPECT_TRUE(made->report.biasSubtracted);
    EXPECT_FALSE(made->report.darkSubtracted);
    EXPECT_FALSE(made->report.flatFielded);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            double sum = 0.0;
            for (const OwnedFrame& frame : frames)
            {
                const float calibrated =
                    static_cast<float>(static_cast<double>(static_cast<float>(frame.value(0u, x, y))) -
                                       static_cast<double>(static_cast<float>(bias.value(0u, x, y))));
                sum = sum + static_cast<double>(calibrated) * 1.0;
            }
            EXPECT_EQ(sampleAt<float>(made->image, 0u, x, y), static_cast<float>(sum / 3.0)) << x << "," << y;
        }
    }
}

TEST(StackExecutorTest, StatesTheTransitionAStackedImageTakes)
{
    const an::EngineContext engine = makeEngine();
    an::StackRequest request = baseRequest();
    request.normalization.scope = an::StackNormalizationScope::Global;
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    an::Result<an::StackResult> made = run(engine, model, frames);
    ASSERT_TRUE(made.hasValue()) << made.status().message();

    an::ImageSemanticsRequest sourceRequest;
    sourceRequest.transferFunction = an::TransferFunction::Linear;
    sourceRequest.processingState.registered = true;
    const an::Result<an::ImageSemantics> sourceSemantics = an::ImageSemantics::create(sourceRequest);
    ASSERT_TRUE(sourceSemantics.hasValue());
    const an::Result<an::MetadataTransition> transition = an::stackTransition(model, made->report, *sourceSemantics);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_TRUE(transition->semantics->processingState().normalized);
    EXPECT_TRUE(transition->semantics->processingState().registered);
    EXPECT_FALSE(transition->semantics->processingState().biasSubtracted);
    EXPECT_EQ(transition->semantics->validity().rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(transition->semantics->transferFunction(), an::TransferFunction::Linear);
    EXPECT_FALSE(transition->astrometry.has_value());
    EXPECT_TRUE(transition->clearCodecMetadata);
    EXPECT_TRUE(transition->clearOutputIntent);
    ASSERT_TRUE(transition->record.has_value());
    EXPECT_EQ(transition->record->operation(), "an::executeStack");
    EXPECT_NE(transition->record->detail().find("Average"), std::string::npos);
    EXPECT_NE(transition->record->detail().find("frames=3"), std::string::npos);
    EXPECT_NE(transition->record->detail().find("Global"), std::string::npos);

    // The result becomes an image with two calls and takes the transition.
    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(made->image));
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();
    an::Result<an::Image<float>> image = an::Image<float>::fromStorage(std::move(*storage), made->descriptor);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    const an::Status applied = image->transition(*transition);
    ASSERT_TRUE(applied.ok()) << applied.message();
    EXPECT_TRUE(image->metadata().semantics().processingState().normalized);
    ASSERT_EQ(image->metadata().provenance().entries().size(), 1u);
    EXPECT_EQ(image->metadata().provenance().entries()[0].operation(), "an::executeStack");
    EXPECT_EQ(image->descriptor(), made->descriptor);
}

TEST(StackExecutorTest, RefusesWhatItCannotRunBeforeReadingAFrame)
{
    const an::EngineContext engine = makeEngine();
    const an::EngineContext other = makeEngine();
    const an::StackModel model = makeModel(baseRequest());
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    an::ResidentStackFrameSource source = makeSource(frames);
    {
        const an::ExecutionContext execution = makeExecution(other);
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "execution");
    }
    {
        const an::ExecutionContext none;
        const an::Result<an::StackResult> made = an::executeStack(engine, none, model, source, {}, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::NotInitialized);
    }
    const an::ExecutionContext execution = makeExecution(engine);
    {
        // A contract the stack cannot serve is the contract check's refusal.
        const std::array<an::CalibrationPlan, 1u> plans{};
        const an::StackSourceContract wrong{.state = an::StackFrameState::Calibrated, .calibration = plans};
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, wrong, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(made.status(), "contract.calibration")) << made.status().message();
    }
    {
        an::StackRequest explicitRequest = baseRequest();
        explicitRequest.weighting.method = an::StackWeighting::Explicit;
        const an::Result<an::StackResult> made =
            an::executeStack(engine, execution, makeModel(explicitRequest), source, {}, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(made.status(), "explicitWeights")) << made.status().message();
    }
    {
        an::StackRequest derived = baseRequest();
        derived.weighting.method = an::StackWeighting::Noise;
        const an::Result<an::StackResult> made =
            an::executeStack(engine, execution, makeModel(derived), source, {}, {}, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(made.status(), "measurements")) << made.status().message();
    }
    {
        std::vector<double> short_(kFrames - 1u);
        const an::StackExecutionOutputs outputs{.frameWeights = short_};
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, outputs);
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "outputs.frameWeights");
    }
    {
        std::vector<an::FrameNormalization> short_(kFrames - 1u);
        const an::StackExecutionOutputs outputs{.frameNormalization = short_};
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, outputs);
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "outputs.frameNormalization");
    }
    {
        std::vector<an::FrameRejectionRate> short_(kFrames + 1u);
        const an::StackExecutionOutputs outputs{.frameRejectionRates = short_};
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, outputs);
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "outputs.frameRejectionRates");
    }
    {
        std::vector<std::uint32_t> short_(kFrames - 1u);
        const an::StackExecutionOutputs outputs{.flaggedFrames = short_};
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, {}, outputs);
        EXPECT_EQ(made.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(made.status(), an::ContextKey::Parameter), "outputs.flaggedFrames");
    }
    {
        an::StackExecutionRequest request;
        request.concurrentFrameReads = 0u;
        const an::Result<an::StackResult> made = an::executeStack(engine, execution, model, source, {}, request, {});
        EXPECT_EQ(made.status().code(), an::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackExecutorTest, RunsTheSameBitsAtOneAndFourWorkers)
{
    const an::EngineContext engine = makeEngine(4u);
    constexpr std::uint32_t kCount = 5u;
    an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb), kCount);
    request.rejection.method = an::StackRejection::WinsorizedSigmaClipping;
    request.weighting.method = an::StackWeighting::Explicit;
    request.localWeighting.enabled = true;
    request.localWeighting.gridSpacing = 4u;
    request.localWeighting.windowSize = 3u;
    request.output.rejectionMaps = an::RejectionMapForm::Merged;
    request.output.coverageMap = true;
    request.output.varianceMap = true;
    request.output.signalToNoiseGainMap = true;
    request.output.weightSumMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kCount, an::SampleType::Float32, 3u);
    const std::array<double, kCount> perFrame{1.0, 1.5, 0.5, 2.0, 1.25};
    an::StackExecutionRequest execution;
    execution.explicitWeights = an::StackFrameWeights{.perFrame = perFrame};
    execution.maximumTileRows = 3u;

    std::vector<an::FrameRejectionRate> narrowRates(kCount);
    std::vector<an::FrameRejectionRate> wideRates(kCount);
    const an::Result<an::StackResult> narrow =
        run(engine, model, frames, {}, execution, an::StackExecutionOutputs{.frameRejectionRates = narrowRates},
            an::StackSourceAccess::WholeFrame, ExecutionSetup{.concurrency = 1u});
    ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
    const an::Result<an::StackResult> wide =
        run(engine, model, frames, {}, execution, an::StackExecutionOutputs{.frameRejectionRates = wideRates},
            an::StackSourceAccess::Region, ExecutionSetup{.concurrency = 4u});
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_EQ(narrow->report.plan.memory.workers, 1u);
    EXPECT_EQ(wide->report.plan.memory.workers, 4u);
    EXPECT_EQ(bitsOf(*narrow), bitsOf(*wide));
    for (std::size_t index = 0u; index < an::kStackOutputMapCount; ++index)
    {
        ASSERT_EQ(narrow->maps[index].valid(), wide->maps[index].valid()) << index;
        if (!narrow->maps[index].valid())
        {
            continue;
        }
        const std::size_t bytes = static_cast<std::size_t>(narrow->maps[index].layout()->totalBytes());
        EXPECT_EQ(std::memcmp(narrow->maps[index].data(), wide->maps[index].data(), bytes), 0) << index;
    }
    EXPECT_EQ(narrow->report.samplesAdmitted, wide->report.samplesAdmitted);
    EXPECT_EQ(narrow->report.mappedLow, wide->report.mappedLow);
    EXPECT_EQ(narrow->report.mappedHigh, wide->report.mappedHigh);
    EXPECT_EQ(narrow->report.rejection.globalRate, wide->report.rejection.globalRate);
    EXPECT_EQ(narrow->report.coverage.mean, wide->report.coverage.mean);
    EXPECT_EQ(narrow->report.effectiveFrameCount, wide->report.effectiveFrameCount);
    EXPECT_EQ(narrow->report.localWeighting.meanWeight, wide->report.localWeighting.meanWeight);
    for (std::uint32_t index = 0u; index < kCount; ++index)
    {
        EXPECT_EQ(narrowRates[index].rate, wideRates[index].rate) << index;
    }
    EXPECT_EQ(narrow->report.plan.passes.count, 3u);
    EXPECT_EQ(narrow->report.plan.memory.tileBytesPerRow,
              static_cast<std::uint64_t>(kWidth) * 3u * kCount * sizeof(float) +
                  static_cast<std::uint64_t>(kWidth) * kCount * sizeof(float));
}

TEST(StackExecutorTest, SummarizesTheSameAtOneAndFourWorkersOverManyChunks)
{
    // The coverage summary and the effective frame count are reduced on the operation's workers,
    // in chunks the element count alone decides. An image many chunks long, weighted so that every
    // gain is fractional, reports the same bits at one worker and at four - for a streaming stack,
    // whose gains come from its accumulators, and for a sample-history stack, whose gains its
    // rejection writes.
    const an::EngineContext engine = makeEngine(4u);
    constexpr std::uint32_t kCount = 4u;
    constexpr std::uint32_t kLargeWidth = 256u;
    constexpr std::uint32_t kLargeHeight = 96u;
    const std::vector<OwnedFrame> frames = makeFrames(kCount, an::SampleType::Float32, 3u, kLargeWidth, kLargeHeight);
    const std::array<double, kCount> perFrame{1.0, 1.7, 0.3, 2.2};
    for (const bool history : {false, true})
    {
        SCOPED_TRACE(history ? "sample history" : "streaming");
        an::StackRequest request =
            baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb,
                                       an::NumericDomain::NormalizedLinear, kLargeWidth, kLargeHeight),
                        kCount);
        request.weighting.method = an::StackWeighting::Explicit;
        request.output.coverageMap = true;
        request.output.signalToNoiseGainMap = true;
        if (history)
        {
            request.rejection.method = an::StackRejection::SigmaClipping;
        }
        const an::StackModel model = makeModel(request);
        an::StackExecutionRequest execution;
        execution.explicitWeights = an::StackFrameWeights{.perFrame = perFrame};
        const an::Result<an::StackResult> narrow =
            run(engine, model, frames, {}, execution, {}, an::StackSourceAccess::WholeFrame,
                ExecutionSetup{.concurrency = 1u});
        ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
        const an::Result<an::StackResult> wide =
            run(engine, model, frames, {}, execution, {}, an::StackSourceAccess::WholeFrame,
                ExecutionSetup{.concurrency = 4u});
        ASSERT_TRUE(wide.hasValue()) << wide.status().message();
        EXPECT_EQ(wide->report.plan.memory.workers, 4u);
        EXPECT_EQ(bitsOf(*narrow), bitsOf(*wide));
        const an::StackCoverageSummary& one = narrow->report.coverage;
        const an::StackCoverageSummary& four = wide->report.coverage;
        EXPECT_TRUE(one.available);
        EXPECT_EQ(one.elements, static_cast<std::uint64_t>(kLargeWidth) * kLargeHeight * 3u);
        EXPECT_EQ(four.elements, one.elements);
        EXPECT_EQ(four.elementsWithSamples, one.elementsWithSamples);
        EXPECT_EQ(four.minimum, one.minimum);
        EXPECT_EQ(four.maximum, one.maximum);
        EXPECT_EQ(four.mean, one.mean);
        EXPECT_EQ(four.median, one.median);
        EXPECT_EQ(four.standardDeviation, one.standardDeviation);
        EXPECT_GT(narrow->report.effectiveFrameCount, 1.0);
        EXPECT_LT(narrow->report.effectiveFrameCount, static_cast<double>(kCount));
        EXPECT_EQ(wide->report.effectiveFrameCount, narrow->report.effectiveFrameCount);
        EXPECT_EQ(wide->report.actualSnrGain, narrow->report.actualSnrGain);
    }
}

TEST(StackExecutorTest, CancellationAtFinalProgressHandsOverNothing)
{
    class CancelAtEnd final : public an::ProgressSink
    {
    public:
        std::stop_source stop;
        bool reached = false;
        void onProgress(const an::ProgressReport& report) noexcept override
        {
            if (report.total != 0u && report.completed == report.total)
            {
                reached = true;
                stop.request_stop();
            }
        }
    } progress;
    const auto engine = makeEngine();
    const auto result =
        run(engine, makeModel(baseRequest()), makeFrames(kFrames), {}, {}, {}, an::StackSourceAccess::Region,
            {.stopToken = progress.stop.get_token(), .progressSink = &progress});
    ASSERT_TRUE(progress.reached);
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackExecutorTest, CancellationFromFinalTraceAndDiagnosticHandsOverNothing)
{
    class CancelAtHandover final : public an::TraceSink, public an::DiagnosticSink
    {
    public:
        std::stop_source stop;
        bool reached = false;
        bool stopOnTrace = false;
        void onTrace(const an::TraceEvent& event) noexcept override
        {
            if (std::string_view(event.category) == "stacking" && std::string_view(event.name) == "inputBytesRead")
            {
                reached = true;
                if (stopOnTrace)
                    stop.request_stop();
            }
        }
        void onDiagnostic(const an::DiagnosticEvent&) noexcept override
        {
            if (reached)
                stop.request_stop();
        }
    };
    const auto engine = makeEngine();
    const auto model = makeModel(baseRequest());
    const auto frames = makeFrames(kFrames);
    for (const bool stopOnTrace : {true, false})
    {
        CancelAtHandover sink;
        sink.stopOnTrace = stopOnTrace;
        const auto result = run(engine, model, frames, {}, {}, {}, an::StackSourceAccess::Region,
                                {.stopToken = sink.stop.get_token(), .diagnosticSink = &sink, .traceSink = &sink});
        ASSERT_TRUE(sink.reached);
        EXPECT_TRUE(sink.stop.stop_requested());
        EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
        EXPECT_FALSE(result.hasValue());
        EXPECT_EQ(liveBytes(engine), 0u);
    }
}

TEST(StackExecutorTest, RawFloat32SumOverflowIsReported)
{
    const auto engine = makeEngine();
    auto request = baseRequest();
    request.combination = an::StackCombination::Sum;
    const auto model = makeModel(request);
    auto frames = makeFrames(kFrames);
    for (auto& frame : frames)
    {
        std::fill_n(reinterpret_cast<float*>(frame.bytes()), kWidth * kHeight,
                    (std::numeric_limits<float>::max)() * 0.75f);
    }
    const auto result = run(engine, model, frames, {}, {.sumDomain = an::StackSumDomain::Raw});
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_FALSE(std::isfinite(sampleAt<float>(result->image, 0u, 0u, 0u)));
    EXPECT_EQ(result->report.nonFiniteOutputs, kWidth * kHeight);
}

TEST(StackExecutorTest, Float64HistoryRefusesOverflowIntoFloat32)
{
    const auto engine = makeEngine();
    // Exactly representable while beyond Float32, including after the adaptive mean's scale.
    const double large = std::ldexp(1.0, 128);
    for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        for (const auto combination : {an::StackCombination::Median, an::StackCombination::AutoAdaptiveWeightedAverage})
        {
            SCOPED_TRACE(static_cast<int>(backend));
            SCOPED_TRACE(static_cast<int>(combination));
            auto request = baseRequest(makeDescriptor(an::SampleType::Float64));
            request.combination = combination;
            auto frames = makeFrames(kFrames, an::SampleType::Float64);
            for (auto& frame : frames)
            {
                std::fill_n(reinterpret_cast<double*>(frame.bytes()), kWidth * kHeight, large);
            }
            const auto result = run(engine, makeModel(request), frames, {}, {}, {}, an::StackSourceAccess::Region,
                                    {.backend = backend});
            EXPECT_EQ(result.status().code(), an::ErrorCode::InvalidData);
            EXPECT_FALSE(result.hasValue());
            EXPECT_EQ(liveBytes(engine), 0u);
            request.output.sampleType = an::SampleType::Float64;
            const auto wide = run(engine, makeModel(request), frames, {}, {}, {}, an::StackSourceAccess::Region,
                                  {.backend = backend});
            ASSERT_TRUE(wide.hasValue()) << wide.status().message();
            EXPECT_DOUBLE_EQ(sampleAt<double>(wide->image, 0u, 0u, 0u), large);
        }
    }
}

TEST(StackExecutorTest, Float32OverflowChecksTheRoundedOutput)
{
    const auto engine = makeEngine();
    const double largest = (std::numeric_limits<float>::max)();
    const double rounded = std::nextafter(largest, std::numeric_limits<double>::infinity());
    ASSERT_EQ(static_cast<float>(rounded), static_cast<float>(largest));
    for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        for (const auto combination : {an::StackCombination::Sum, an::StackCombination::Median})
        {
            for (const double sign : {-1.0, 1.0})
            {
                auto request = baseRequest(makeDescriptor(an::SampleType::Float64));
                request.combination = combination;
                auto frames = makeFrames(kFrames, an::SampleType::Float64);
                for (auto& frame : frames)
                {
                    const double value = combination == an::StackCombination::Median ? rounded : rounded / kFrames;
                    std::fill_n(reinterpret_cast<double*>(frame.bytes()), kWidth * kHeight, value * sign);
                }
                const auto result = run(engine, makeModel(request), frames, {}, {.sumDomain = an::StackSumDomain::Raw},
                                        {}, an::StackSourceAccess::Region, {.backend = backend});
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_EQ(result->report.nonFiniteOutputs, 0u);
                EXPECT_EQ(sampleAt<float>(result->image, 0u, 0u, 0u), static_cast<float>(largest * sign));
            }
        }
    }
}

TEST(StackExecutorTest, StackWindowsRetainRaisedGeometryLimits)
{
    auto limits = an::defaultEngineLimits();
    limits.maxImageWidth = an::kDefaultMaxImageWidth + 1u;
    const auto extent = an::ImageExtent::create(limits.maxImageWidth, 2u, 1u, limits);
    ASSERT_TRUE(extent.has_value());
    const auto layout = an::ImageLayout::create({*extent, 4u, 1u, 1u, 1u}, limits);
    ASSERT_TRUE(layout.has_value());
    std::vector<float> samples(static_cast<std::size_t>(extent->sampleCount()), 0.5f);
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(an::packedMaskRowBytes(extent->width()) * 2u), 0u);
    const auto mask = an::ValidityMaskView<const std::uint8_t>::createTight(maskBytes, *extent);
    ASSERT_TRUE(mask.has_value());
    const auto validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(validity.has_value());
    const an::StackSourceBand frame{.sampleType = an::SampleType::Float32,
                                    .samples = samples.data(),
                                    .layout = &*layout,
                                    .validity = {.descriptor = *validity, .mask = &*mask}};
    const auto window = an::stackSourceRowsOf(frame, 1u, 1u);
    ASSERT_TRUE(window.hasValue()) << window.status().message();
    EXPECT_EQ(window->samples, samples.data() + extent->width());
    ASSERT_TRUE(window->mask.has_value());
    EXPECT_EQ(window->mask->height(), 1u);
    auto description = makeDescriptor().toRequest();
    description.extent = *extent;
    const auto descriptor = an::ImageDescriptor::create(description);
    ASSERT_TRUE(descriptor.has_value());
    const auto band = an::stackTileBandLayout(makeModel(baseRequest(*descriptor)), 1u);
    ASSERT_TRUE(band.hasValue()) << band.status().message();
    EXPECT_EQ(band->extent().width(), limits.maxImageWidth);
}

TEST(StackExecutorTest, IntegerRangeValiditySurvivesTileConversion)
{
    const auto engine = makeEngine();
    for (const auto type : {an::SampleType::UInt16, an::SampleType::UInt32})
    {
        const double boundary = type == an::SampleType::UInt16 ? 100.0 : 4294967294.0;
        const double maximum = type == an::SampleType::UInt16 ? 65535.0 : 4294967295.0;
        const auto validity = an::ValidityDescriptor::create(
            {.rule = an::ValidityRule::DomainRange, .lowestValid = boundary, .highestValid = boundary});
        ASSERT_TRUE(validity.has_value());
        auto frames = makeFrames(kFrames, type);
        for (auto& frame : frames)
        {
            frame.descriptor = *validity;
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    const double value = boundary + static_cast<double>(x % 3u) - 1.0;
                    if (type == an::SampleType::UInt16)
                        frame.at<std::uint16_t>(0u, x, y) = static_cast<std::uint16_t>(value);
                    else
                        frame.at<std::uint32_t>(0u, x, y) = static_cast<std::uint32_t>(value);
                }
            }
        }
        for (const auto access : {an::StackSourceAccess::Region, an::StackSourceAccess::WholeFrame})
        {
            for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
            {
                for (const auto combination : {an::StackCombination::Average, an::StackCombination::Median})
                {
                    auto request = baseRequest(makeDescriptor(type));
                    request.combination = combination;
                    const auto result =
                        run(engine, makeModel(request), frames, {}, {}, {}, access, {.backend = backend});
                    ASSERT_TRUE(result.hasValue()) << result.status().message();
                    EXPECT_EQ(result->report.elementsWithSamples, kWidth * kHeight / 3u);
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        EXPECT_EQ(maskAt(result->mask, 0u, x, 0u), x % 3u == 1u);
                        EXPECT_FLOAT_EQ(sampleAt<float>(result->image, 0u, x, 0u),
                                        x % 3u == 1u ? static_cast<float>(boundary / maximum) : 0.0f);
                    }
                }
            }
        }
    }
}

/// A sink that puts the output it is handed tile by tile back together, and can fail or throw at a stated tile.
class CollectingOutputSink final : public an::StackOutputSink
{
public:
    CollectingOutputSink(std::uint32_t width, std::uint32_t height, std::uint32_t channels, std::size_t sampleBytes)
        : m_width(width), m_height(height), m_channels(channels), m_sampleBytes(sampleBytes),
          image(static_cast<std::size_t>(width) * height * channels * sampleBytes),
          mask(static_cast<std::size_t>(width) * height * channels, false)
    {
    }

    an::Status write(const an::ExecutionContext& execution, const an::StackOutputRows& rows) override
    {
        if (!execution.valid())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized);
        }
        tiles.emplace_back(rows.firstRow, rows.rows);
        if (failAt.has_value() && tiles.size() == *failAt)
        {
            return an::Status::failure(an::ErrorCode::ExternalFailure, "the sink could not take the tile");
        }
        if (throwAt.has_value() && tiles.size() == *throwAt)
        {
            throw std::runtime_error("the sink threw on a tile");
        }
        const std::size_t plane = static_cast<std::size_t>(m_width) * rows.rows;
        const std::size_t wholePlane = static_cast<std::size_t>(m_width) * m_height;
        const std::size_t first = static_cast<std::size_t>(m_width) * rows.firstRow;
        layoutsAgree = layoutsAgree && rows.layout != nullptr && rows.layout->extent().height() == rows.rows &&
                       rows.layout->extent().width() == m_width && rows.layout->planeCount() == m_channels &&
                       rows.layout->planeStrideBytes() == plane * m_sampleBytes &&
                       rows.image.size() == plane * m_channels * m_sampleBytes;
        for (std::uint32_t channel = 0u; channel < m_channels; ++channel)
        {
            std::memcpy(image.data() + (channel * wholePlane + first) * m_sampleBytes,
                        rows.image.data() + channel * plane * m_sampleBytes, plane * m_sampleBytes);
            for (std::uint32_t y = 0u; y < rows.rows && rows.mask != nullptr; ++y)
            {
                for (std::uint32_t x = 0u; x < m_width; ++x)
                {
                    mask[channel * wholePlane + first + static_cast<std::size_t>(y) * m_width + x] =
                        rows.mask->test(channel, x, y).value_or(false);
                }
            }
        }
        for (std::size_t index = 0u; index < an::kStackOutputMapCount; ++index)
        {
            if (rows.maps[index].empty())
            {
                continue;
            }
            std::vector<std::uint8_t>& map = maps[index];
            map.resize(wholePlane * m_channels * an::kStackMapSampleBytes);
            for (std::uint32_t channel = 0u; channel < m_channels; ++channel)
            {
                std::memcpy(map.data() + (channel * wholePlane + first) * an::kStackMapSampleBytes,
                            rows.maps[index].data() + channel * plane * an::kStackMapSampleBytes,
                            plane * an::kStackMapSampleBytes);
            }
        }
        return an::Status::success();
    }

    /// Whether the tiles cover the output once, in row order.
    [[nodiscard]] bool coversInOrder() const
    {
        std::uint32_t next = 0u;
        for (const auto& [first, rows] : tiles)
        {
            if (first != next || rows == 0u)
            {
                return false;
            }
            next = first + rows;
        }
        return next == m_height;
    }

    std::optional<std::size_t> failAt{};
    std::optional<std::size_t> throwAt{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> tiles;
    std::vector<std::uint8_t> image;
    std::vector<bool> mask;
    std::array<std::vector<std::uint8_t>, an::kStackOutputMapCount> maps{};
    bool layoutsAgree = true;

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_channels;
    std::size_t m_sampleBytes;
};

TEST(StackExecutorTest, HandsTheOutputToASinkTileByTileWithTheBitsOfAWholeRun)
{
    // A stack whose tiles are finished as they are combined - the terms of one tile under region access, and sample
    // history with and without a rejection, whole frames or regions - hands each tile's image, mask and maps to a
    // sink as soon as it is finished, holds them for one tile, and hands none of them over. What the sink is given,
    // put back together, is what the same stack hands over whole, bit for bit, and so are the summaries, at one and
    // four workers.
    const an::EngineContext engine = makeEngine(4u);
    struct Case
    {
        const char* name;
        an::StackRequest request;
        an::StackSourceAccess access;
        std::vector<OwnedFrame> frames;
    };
    std::vector<Case> cases;
    {
        std::vector<OwnedFrame> frames =
            makeFrames(kFrames, an::SampleType::UInt16, 1u, kWidth, kHeight, an::ValidityRule::ZeroIsNoData);
        for (OwnedFrame& frame : frames)
        {
            frame.put(0u, 2u, 2u, 0.0);
        }
        an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::UInt16));
        request.output.coverageMap = true;
        request.output.varianceMap = true;
        request.output.signalToNoiseGainMap = true;
        request.output.weightSumMap = true;
        cases.push_back({"average of one-tile terms", request, an::StackSourceAccess::Region, std::move(frames)});
    }
    constexpr std::uint32_t kClipped = 6u;
    an::StackRequest clipped = baseRequest(makeDescriptor(), kClipped);
    clipped.rejection.method = an::StackRejection::SigmaClipping;
    clipped.rejection.sigma = an::SigmaClipThresholds{.low = 2.0, .high = 2.0};
    clipped.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    clipped.output.coverageMap = true;
    clipped.output.signalToNoiseGainMap = true;
    // Six frames whose samples at every pixel are evenly spaced, which no two-sigma clip rejects, and a bright
    // rectangle over rows 2 to 5 of one of them, which every clip rejects: the gain and the rejection maps then
    // differ from one tile to the next.
    std::vector<OwnedFrame> clippedFrames(kClipped);
    for (std::uint32_t index = 0u; index < kClipped; ++index)
    {
        clippedFrames[index].create(an::SampleType::Float32, kWidth, kHeight, 1u);
        clippedFrames[index].frameIndex = index;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const double spread = 0.001 * (static_cast<double>(index) - 2.5) / 2.5;
                clippedFrames[index].put(0u, x, y, 0.3 + 0.01 * (x % 3u) + 0.02 * (y % 2u) + spread);
            }
        }
    }
    for (std::uint32_t y = 2u; y < 6u; ++y)
    {
        for (std::uint32_t x = 3u; x < 9u; ++x)
        {
            clippedFrames[4].put(0u, x, y, 0.9);
        }
    }
    cases.push_back({"sigma clipping over whole frames", clipped, an::StackSourceAccess::WholeFrame, clippedFrames});
    cases.push_back({"sigma clipping over regions", clipped, an::StackSourceAccess::Region, clippedFrames});
    {
        an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb));
        request.combination = an::StackCombination::Median;
        request.output.sampleType = an::SampleType::Float64;
        request.output.coverageMap = true;
        cases.push_back({"colour median written as Float64", request, an::StackSourceAccess::Region,
                         makeFrames(kFrames, an::SampleType::Float32, 3u)});
    }

    for (const Case& stack : cases)
    {
        const an::StackModel model = makeModel(stack.request);
        const std::uint32_t channels = stack.request.input.descriptor.extent().channelCount();
        const std::size_t sampleBytes = an::sampleTypeSizeBytes(stack.request.output.sampleType);
        EXPECT_TRUE(an::stackOutputCanCoverOneTile(model, stack.access)) << stack.name;
        for (const std::uint32_t workers : {1u, 4u})
        {
            const std::string where = std::string(stack.name) + ", " + std::to_string(workers) + " workers";
            an::StackExecutionRequest capped;
            capped.maximumTileRows = 3u;
            const an::Result<an::StackResult> whole =
                run(engine, model, stack.frames, {}, capped, {}, stack.access, {.concurrency = workers});
            ASSERT_TRUE(whole.hasValue()) << where << ": " << whole.status().message();
            CollectingOutputSink sink(kWidth, kHeight, channels, sampleBytes);
            an::StackExecutionRequest tiled = capped;
            tiled.outputSink = &sink;
            const an::Result<an::StackResult> handed =
                run(engine, model, stack.frames, {}, tiled, {}, stack.access, {.concurrency = workers});
            ASSERT_TRUE(handed.hasValue()) << where << ": " << handed.status().message();

            const an::StackExecutionReport& report = handed->report;
            const an::StackExecutionReport& expected = whole->report;
            EXPECT_FALSE(handed->image.valid()) << where;
            EXPECT_FALSE(handed->mask.valid()) << where;
            for (const an::ImageStorageBytes& map : handed->maps)
            {
                EXPECT_FALSE(map.valid()) << where;
            }
            EXPECT_EQ(handed->descriptor, whole->descriptor) << where;
            EXPECT_TRUE(report.plan.memory.outputCoversOneTile) << where;
            EXPECT_FALSE(expected.plan.memory.outputCoversOneTile) << where;
            EXPECT_EQ(report.plan.memory.outputImageBytes,
                      static_cast<std::uint64_t>(report.plan.tile.rows) * kWidth * channels * sampleBytes)
                << where;
            EXPECT_EQ(expected.plan.memory.outputImageBytes,
                      static_cast<std::uint64_t>(kHeight) * kWidth * channels * sampleBytes)
                << where;
            EXPECT_LT(report.plan.memory.peakBytes, expected.plan.memory.peakBytes) << where;
            EXPECT_LE(report.memory.peakHeldBytes, report.plan.memory.peakBytes) << where;

            EXPECT_EQ(sink.tiles.size(), report.plan.tile.tileCount) << where;
            EXPECT_EQ(report.tilesCombined, report.plan.tile.tileCount) << where;
            EXPECT_TRUE(sink.coversInOrder()) << where;
            EXPECT_TRUE(sink.layoutsAgree) << where;
            EXPECT_EQ(sink.image, storedBytesOf(whole->image)) << where;
            for (std::size_t map = 0u; map < an::kStackOutputMapCount; ++map)
            {
                EXPECT_EQ(sink.maps[map], storedBytesOf(whole->maps[map])) << where << ", map " << map;
            }
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        EXPECT_EQ(sink.mask[(static_cast<std::size_t>(channel) * kHeight + y) * kWidth + x],
                                  maskAt(whole->mask, channel, x, y))
                            << where << " at " << channel << "," << x << "," << y;
                    }
                }
            }

            EXPECT_EQ(report.finalize, expected.finalize) << where;
            EXPECT_EQ(report.elementsWithSamples, expected.elementsWithSamples) << where;
            EXPECT_EQ(report.elementsWithoutSamples, expected.elementsWithoutSamples) << where;
            EXPECT_EQ(report.nonFiniteOutputs, expected.nonFiniteOutputs) << where;
            EXPECT_EQ(report.samplesAdmitted, expected.samplesAdmitted) << where;
            EXPECT_EQ(report.samplesExcludedByValidity, expected.samplesExcludedByValidity) << where;
            EXPECT_EQ(report.rejectedLow, expected.rejectedLow) << where;
            EXPECT_EQ(report.rejectedHigh, expected.rejectedHigh) << where;
            EXPECT_EQ(report.coverage.available, expected.coverage.available) << where;
            EXPECT_EQ(report.coverage.elements, expected.coverage.elements) << where;
            EXPECT_EQ(report.coverage.elementsWithSamples, expected.coverage.elementsWithSamples) << where;
            EXPECT_EQ(report.coverage.minimum, expected.coverage.minimum) << where;
            EXPECT_EQ(report.coverage.maximum, expected.coverage.maximum) << where;
            EXPECT_EQ(report.coverage.mean, expected.coverage.mean) << where;
            EXPECT_EQ(report.coverage.median, expected.coverage.median) << where;
            EXPECT_EQ(report.coverage.standardDeviation, expected.coverage.standardDeviation) << where;
            EXPECT_DOUBLE_EQ(report.effectiveFrameCount, expected.effectiveFrameCount) << where;
            if (stack.request.rejection.method != an::StackRejection::None)
            {
                // The rectangle was rejected, so the gain is not the same in every tile.
                EXPECT_EQ(expected.rejectedHigh, 24u) << where;
                EXPECT_LT(expected.effectiveFrameCount, static_cast<double>(kClipped)) << where;
            }
            EXPECT_TRUE(report.frameSampleCountsComplete) << where;
        }
    }
}

TEST(StackExecutorTest, RefusesAnOutputSinkWhereATileIsNotFinishedAsItIsCombined)
{
    // Whole-output terms are finalized after the last frame, so no tile of their output is finished before then: a
    // plain average over whole frames, and a sum, whose domain may divide by the output's peak, under region access
    // too. A cluster search reads the whole rejection map. Each is refused before a frame is read.
    const an::EngineContext engine = makeEngine();
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    CollectingOutputSink sink(kWidth, kHeight, 1u, sizeof(float));
    an::StackExecutionRequest request;
    request.outputSink = &sink;
    const an::StackModel average = makeModel(baseRequest());
    EXPECT_FALSE(an::stackOutputCanCoverOneTile(average, an::StackSourceAccess::WholeFrame));
    EXPECT_TRUE(an::stackOutputCanCoverOneTile(average, an::StackSourceAccess::Region));
    const an::Result<an::StackResult> wholeFrames = run(engine, average, frames, {}, request);
    EXPECT_EQ(wholeFrames.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(textField(wholeFrames.status(), an::ContextKey::Parameter), "outputByTile");
    an::StackRequest summed = baseRequest();
    summed.combination = an::StackCombination::Sum;
    const an::StackModel sum = makeModel(summed);
    EXPECT_FALSE(an::stackOutputCanCoverOneTile(sum, an::StackSourceAccess::Region));
    EXPECT_EQ(run(engine, sum, frames, {}, request, {}, an::StackSourceAccess::Region).status().code(),
              an::ErrorCode::IncompatibleArguments);

    an::StackRequest rejected = baseRequest(makeDescriptor(), 6u);
    rejected.rejection.method = an::StackRejection::SigmaClipping;
    rejected.rejection.sigma = an::SigmaClipThresholds{.low = 2.0, .high = 2.0};
    rejected.output.rejectionMaps = an::RejectionMapForm::LowAndHigh;
    const an::StackModel clipping = makeModel(rejected);
    EXPECT_TRUE(an::stackOutputCanCoverOneTile(clipping, an::StackSourceAccess::WholeFrame));
    std::vector<an::RejectionClusterDetection> clusters(2u);
    an::StackExecutionRequest searched = request;
    searched.clusterAnalysis = an::RejectionClusterAnalysisRequest{.source = an::RejectionClusterSource::High};
    const an::Result<an::StackResult> search =
        run(engine, clipping, makeFrames(6u), {}, searched, an::StackExecutionOutputs{.clusters = clusters});
    EXPECT_EQ(search.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(textField(search.status(), an::ContextKey::Parameter), "clusterAnalysis");
    EXPECT_TRUE(sink.tiles.empty());
    {
        // The planner holds the same rule for a caller that plans without running.
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::StackPlan> planned =
            an::planStack(execution, average,
                          an::StackPlanningRequest{.access = an::StackSourceAccess::WholeFrame, .outputByTile = true});
        EXPECT_EQ(planned.status().code(), an::ErrorCode::IncompatibleArguments);
        const an::Result<an::StackPlan> region =
            an::planStack(execution, average,
                          an::StackPlanningRequest{.access = an::StackSourceAccess::Region, .outputByTile = true});
        ASSERT_TRUE(region.hasValue()) << region.status().message();
        EXPECT_TRUE(region->memory.outputCoversOneTile);
    }
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackExecutorTest, AnOutputTooLargeForTheBudgetRunsWhenTheSinkTakesItTileByTile)
{
    // A rejection over regions holds its output whole beside one tile: under a limit set to what the same stack holds
    // when a sink takes the output tile by tile, the whole-output stack is refused and the tiled one runs, writing the
    // same bits. A measurement pass ahead of the combine holds the output's tile beside its frame.
    const an::EngineContext engine = makeEngine(2u);
    constexpr std::uint32_t kTall = 96u;
    constexpr std::uint32_t kCount = 6u;
    an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                          an::NumericDomain::NormalizedLinear, kWidth, kTall),
                                           kCount);
    request.rejection.method = an::StackRejection::SigmaClipping;
    request.rejection.sigma = an::SigmaClipThresholds{.low = 3.0, .high = 3.0};
    request.output.coverageMap = true;
    request.output.signalToNoiseGainMap = true;
    const an::StackModel model = makeModel(request);
    const std::vector<OwnedFrame> frames = makeFrames(kCount, an::SampleType::Float32, 1u, kWidth, kTall);
    an::StackExecutionRequest capped;
    capped.maximumTileRows = 8u;
    const an::Result<an::StackResult> whole = run(engine, model, frames, {}, capped, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(whole.hasValue()) << whole.status().message();
    CollectingOutputSink measuring(kWidth, kTall, 1u, sizeof(float));
    an::StackExecutionRequest tiled = capped;
    tiled.outputSink = &measuring;
    const an::Result<an::StackResult> planned =
        run(engine, model, frames, {}, tiled, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const std::uint64_t limit = planned->report.plan.memory.peakBytes;
    EXPECT_LT(limit, whole->report.plan.memory.peakBytes);

    an::StackExecutionRequest wholeLimited = capped;
    wholeLimited.workingSetLimitBytes = limit;
    EXPECT_EQ(run(engine, model, frames, {}, wholeLimited, {}, an::StackSourceAccess::Region).status().code(),
              an::ErrorCode::MemoryBudgetExceeded);
    CollectingOutputSink sink(kWidth, kTall, 1u, sizeof(float));
    an::StackExecutionRequest limited = tiled;
    limited.outputSink = &sink;
    limited.workingSetLimitBytes = limit;
    const an::Result<an::StackResult> bounded =
        run(engine, model, frames, {}, limited, {}, an::StackSourceAccess::Region);
    ASSERT_TRUE(bounded.hasValue()) << bounded.status().message();
    EXPECT_TRUE(bounded->report.plan.memory.budgetBounded);
    EXPECT_LE(bounded->report.plan.memory.peakBytes, limit);
    EXPECT_LE(bounded->report.memory.peakHeldBytes, limit);
    EXPECT_TRUE(sink.coversInOrder());
    EXPECT_EQ(sink.image, storedBytesOf(whole->image));
    for (std::size_t map = 0u; map < an::kStackOutputMapCount; ++map)
    {
        EXPECT_EQ(sink.maps[map], storedBytesOf(whole->maps[map])) << "map " << map;
    }

    an::StackRequest normalized = request;
    normalized.normalization.scope = an::StackNormalizationScope::Global;
    normalized.normalization.globalMethod = an::NormalizationMethod::Additive;
    const an::StackModel normalizing = makeModel(normalized);
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackPlanningRequest planning{.access = an::StackSourceAccess::Region, .maximumTileRows = 8u};
    const an::Result<an::StackPlan> wholePlan = an::planStack(execution, normalizing, planning);
    planning.outputByTile = true;
    const an::Result<an::StackPlan> tilePlan = an::planStack(execution, normalizing, planning);
    ASSERT_TRUE(wholePlan.hasValue()) << wholePlan.status().message();
    ASSERT_TRUE(tilePlan.hasValue()) << tilePlan.status().message();
    ASSERT_EQ(tilePlan->passes.passes[0].kind, an::StackPassKind::MeasureNormalization);
    const auto outputOf = [](const an::StackMemoryPlan& memory) {
        return memory.outputImageBytes + memory.outputMaskBytes + memory.outputMapBytes;
    };
    EXPECT_EQ(tilePlan->passes.passes[0].workingSetBytes,
              wholePlan->passes.passes[0].workingSetBytes - outputOf(wholePlan->memory) + outputOf(tilePlan->memory));
    EXPECT_EQ(tilePlan->memory.persistentBytes,
              wholePlan->memory.persistentBytes - outputOf(wholePlan->memory) + outputOf(tilePlan->memory));
}

TEST(StackExecutorTest, ASinkThatFailsOrThrowsStopsTheStackWithNothingHandedOver)
{
    const an::EngineContext engine = makeEngine();
    const std::vector<OwnedFrame> frames = makeFrames(kFrames);
    an::StackRequest request = baseRequest();
    request.combination = an::StackCombination::Median;
    const an::StackModel model = makeModel(request);
    for (const bool throws : {false, true})
    {
        CollectingOutputSink sink(kWidth, kHeight, 1u, sizeof(float));
        (throws ? sink.throwAt : sink.failAt) = 2u;
        an::StackExecutionRequest tiled;
        tiled.maximumTileRows = 2u;
        tiled.outputSink = &sink;
        EXPECT_NO_THROW({
            const an::Result<an::StackResult> result =
                run(engine, model, frames, {}, tiled, {}, an::StackSourceAccess::Region);
            ASSERT_FALSE(result.hasValue());
            EXPECT_EQ(result.status().code(), an::ErrorCode::ExternalFailure);
            if (throws)
            {
                EXPECT_TRUE(messageNames(result.status(), "the sink threw on a tile"));
            }
        });
        EXPECT_EQ(sink.tiles.size(), 2u);
        EXPECT_EQ(liveBytes(engine), 0u);
    }
}

} // namespace
