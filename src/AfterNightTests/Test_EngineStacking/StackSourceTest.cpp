// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the stack source path: the frame-state contract checked whole, one band read
// through widening, calibration, the local surface and the global match in one pass and held bit for
// bit to the three operations it composes, the validity the tile band carries, the window a whole
// frame is cut into, the coefficient table, the planner's conversion band and measurement frame, and
// the local-normalization document started and fitted from the stack's frames.

#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 12u;
constexpr std::uint32_t kHeight = 8u;
constexpr std::uint32_t kFrames = 3u;

/// An engine with a stated concurrency.
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

/// One operation in an engine, on a stated backend, stoppable.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.backend = backend;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A descriptor of the stated representation, schema and domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::SampleType sampleType = an::SampleType::Float32,
                                                 an::ChannelSchema schema = an::ChannelSchema::Mono,
                                                 an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                                 std::uint32_t width = kWidth, std::uint32_t height = kHeight,
                                                 an::CfaDescriptor cfa = {})
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
        .cfa = cfa,
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

/// A request with the stated normalization scope and, under a global method, additive scaling.
[[nodiscard]] an::StackRequest scopedRequest(an::StackNormalizationScope scope,
                                             const an::ImageDescriptor& descriptor = makeDescriptor(),
                                             an::NormalizationMethod method = an::NormalizationMethod::AdditiveScaling)
{
    an::StackRequest request = baseRequest(descriptor);
    request.normalization.scope = scope;
    if (an::stackNormalizationScopeUsesGlobalMethod(scope))
    {
        request.normalization.globalMethod = method;
    }
    return request;
}

/// A layout of the stated arrangement with rows of exactly the width.
[[nodiscard]] an::ImageLayout makeLayout(an::SampleType sampleType, std::uint32_t width, std::uint32_t rows,
                                         std::uint32_t channels, bool interleaved)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, rows, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = an::sampleTypeSizeBytes(sampleType),
                                         .samplesPerPixel = interleaved ? channels : 1u,
                                         .planeCount = interleaved ? 1u : channels,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A validity descriptor of one rule, with the stated range under `DomainRange`.
[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule, double lowest = 0.0, double highest = 1.0)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    if (rule == an::ValidityRule::DomainRange)
    {
        request.lowestValid = lowest;
        request.highestValid = highest;
    }
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

/// One frame owned here, at any representation and either arrangement, with its rule and mask.
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
                an::ValidityRule rule = an::ValidityRule::AllValid, bool interleaved = false)
    {
        sampleType = type;
        width = frameWidth;
        height = frameHeight;
        channels = frameChannels;
        layout = makeLayout(type, width, height, channels, interleaved);
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
        const std::uint32_t plane = channel / layout->samplesPerPixel();
        const std::uint32_t inPixel = channel % layout->samplesPerPixel();
        return plane * layout->planeStrideBytes() + y * layout->rowStrideBytes() +
               (static_cast<std::uint64_t>(x) * layout->samplesPerPixel() + inPixel) * layout->elementSizeBytes();
    }

    template <typename SampleT> [[nodiscard]] SampleT& at(std::uint32_t channel, std::uint32_t x, std::uint32_t y)
    {
        return *reinterpret_cast<SampleT*>(bytes() + offsetOf(channel, x, y));
    }

    template <typename SampleT> [[nodiscard]] SampleT at(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        return *reinterpret_cast<const SampleT*>(bytes() + offsetOf(channel, x, y));
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
                    const double value =
                        patterned((static_cast<std::uint64_t>(channel) * height + y) * width + x, salt);
                    put(channel, x, y, value);
                }
            }
        }
    }

    /// Writes one unit-interval value, scaled to full scale for an integer representation.
    void put(std::uint32_t channel, std::uint32_t x, std::uint32_t y, double unit)
    {
        switch (sampleType)
        {
        case an::SampleType::UInt8:
            at<std::uint8_t>(channel, x, y) = static_cast<std::uint8_t>(std::lround(unit * 255.0));
            break;
        case an::SampleType::Int8:
            at<std::int8_t>(channel, x, y) = static_cast<std::int8_t>(std::lround(unit * 127.0));
            break;
        case an::SampleType::UInt16:
            at<std::uint16_t>(channel, x, y) = static_cast<std::uint16_t>(std::lround(unit * 65535.0));
            break;
        case an::SampleType::Int16:
            at<std::int16_t>(channel, x, y) = static_cast<std::int16_t>(std::lround(unit * 32767.0));
            break;
        case an::SampleType::UInt32:
            at<std::uint32_t>(channel, x, y) = static_cast<std::uint32_t>(unit * 4294967295.0);
            break;
        case an::SampleType::Int32:
            at<std::int32_t>(channel, x, y) = static_cast<std::int32_t>(unit * 2147483647.0);
            break;
        case an::SampleType::Float32:
            at<float>(channel, x, y) = static_cast<float>(unit);
            break;
        case an::SampleType::Float64:
            at<double>(channel, x, y) = unit;
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

    /// The frame's validity as the calibration and normalization views take it.
    [[nodiscard]] an::KernelValidityView kernelValidity() const noexcept
    {
        return an::KernelValidityView{
            .descriptor = descriptor, .mask = mask.has_value() ? &*mask : nullptr, .maskPlane = 0u};
    }

    /// The frame as a checked calibration frame of the stated description.
    [[nodiscard]] an::CalibrationFrameView calibrationView(const an::ImageDescriptor& description) const
    {
        const an::Result<an::CalibrationFrameView> made =
            an::CalibrationFrameView::create(bytes(), layout->totalBytes(), *layout, description, kernelValidity());
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? *made : an::CalibrationFrameView{};
    }

    /// The frame as a checked normalization frame of the stated description.
    [[nodiscard]] an::NormalizationFrameView normalizationView(const an::ImageDescriptor& description) const
    {
        const an::Result<an::NormalizationFrameView> made =
            an::NormalizationFrameView::create(bytes(), layout->totalBytes(), *layout, description, kernelValidity());
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? *made : an::NormalizationFrameView{};
    }
};

/// One frame's band of the tile, owned here.
struct OwnedTile
{
    an::SampleType sampleType = an::SampleType::Float32;
    std::vector<double> storage;
    std::optional<an::ImageLayout> layout;

    void create(const an::StackModel& model, std::uint32_t rows)
    {
        sampleType = an::stackTileSampleTypeOf(model.request().input.descriptor.sampleType());
        const an::Result<an::ImageLayout> made = an::stackTileBandLayout(model, rows);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        layout = *made;
        storage.assign(static_cast<std::size_t>(layout->totalBytes() / sizeof(double)) + 1u, 0.0);
    }

    [[nodiscard]] std::byte* bytes() noexcept
    {
        return reinterpret_cast<std::byte*>(storage.data());
    }

    [[nodiscard]] const std::byte* bytes() const noexcept
    {
        return reinterpret_cast<const std::byte*>(storage.data());
    }

    [[nodiscard]] an::StackTileBand band() noexcept
    {
        return an::StackTileBand{
            .sampleType = sampleType, .samples = bytes(), .layout = layout.has_value() ? &*layout : nullptr};
    }

    [[nodiscard]] std::span<const std::byte> written() const noexcept
    {
        return std::span<const std::byte>(bytes(), static_cast<std::size_t>(layout->totalBytes()));
    }

    template <typename SampleT> [[nodiscard]] SampleT at(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        const std::uint64_t offset = channel * layout->planeStrideBytes() + y * layout->rowStrideBytes() +
                                     static_cast<std::uint64_t>(x) * layout->elementSizeBytes();
        return *reinterpret_cast<const SampleT*>(bytes() + offset);
    }
};

/// Worker scratch slices of the model's contract, aligned as the working set hands them out.
struct OwnedScratch
{
    std::vector<std::byte> storage;
    std::vector<std::span<std::byte>> slices;

    void create(const an::StackModel& model, std::uint32_t workers)
    {
        const std::uint64_t perWorker = an::stackScratchBytesPerWorker(model);
        storage.assign(static_cast<std::size_t>(perWorker * workers + an::kStackScratchAlignmentBytes), std::byte{0});
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage.data());
        const std::uintptr_t aligned = (base + an::kStackScratchAlignmentBytes - 1u) &
                                       ~static_cast<std::uintptr_t>(an::kStackScratchAlignmentBytes - 1u);
        std::byte* first = storage.data() + (aligned - base);
        slices.clear();
        for (std::uint32_t worker = 0u; worker < workers; ++worker)
        {
            slices.emplace_back(first + perWorker * worker, static_cast<std::size_t>(perWorker));
        }
    }

    [[nodiscard]] std::span<const std::span<std::byte>> spans() const noexcept
    {
        return slices;
    }
};

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
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

/// Whether two byte runs hold the same bits.
[[nodiscard]] bool sameBits(std::span<const std::byte> left, std::span<const std::byte> right)
{
    return left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size()) == 0;
}

/// The node grid of a plane over a shape, one value per node per channel.
[[nodiscard]] std::vector<double> planeGrid(const an::LocalNormalizationGridShape& shape, double base, double slopeX,
                                            double slopeY)
{
    std::vector<double> values(static_cast<std::size_t>(shape.valueCount()), 0.0);
    for (std::uint32_t channel = 0u; channel < shape.channelCount; ++channel)
    {
        for (std::uint32_t ny = 0u; ny < shape.nodesPerAxis(); ++ny)
        {
            for (std::uint32_t nx = 0u; nx < shape.nodesPerAxis(); ++nx)
            {
                const double x = static_cast<double>(nx) / shape.gridSize;
                const double y = static_cast<double>(ny) / shape.gridSize;
                values[an::localNormalizationNodeIndex(shape, channel, nx, ny)] =
                    base + 0.01 * channel + slopeX * x + slopeY * y;
            }
        }
    }
    return values;
}

/// A local-normalization document for the stack's frames: a reference grid and one record per frame.
[[nodiscard]] an::LocalNormalizationDocument
makeDocument(const an::StackModel& model, bool scaleGrids,
             an::LocalGridInterpolation interpolation = an::LocalGridInterpolation::Bicubic,
             bool postLocalMatches = false)
{
    const an::StackRequest& request = model.request();
    const an::ImageExtent& extent = request.input.descriptor.extent();
    an::LocalNormalizationDocument document;
    document.request.gridSize = 2u;
    document.request.interpolation = interpolation;
    document.shape = an::LocalNormalizationGridShape{
        .gridSize = 2u, .width = extent.width(), .height = extent.height(), .channelCount = extent.channelCount()};
    document.validity = an::ValidityRule::AllValid;
    document.referenceIndex = request.normalization.referenceFrame;
    document.referenceBackground = planeGrid(document.shape, 0.20, 0.02, -0.01);
    for (std::uint32_t frame = 0u; frame < request.input.frameCount; ++frame)
    {
        an::LocalNormalizationFrameCoefficients record;
        record.frameIndex = frame;
        if (frame == *document.referenceIndex)
        {
            record.background = document.referenceBackground;
        }
        else
        {
            record.background = planeGrid(document.shape, 0.22 + 0.03 * frame, -0.015 * frame, 0.02);
            for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
            {
                record.scale[channel] = 1.1 + 0.05 * frame + 0.02 * channel;
            }
            if (scaleGrids)
            {
                record.scaleGrid = planeGrid(document.shape, 1.05 + 0.02 * frame, 0.04, -0.03);
            }
        }
        if (postLocalMatches)
        {
            an::FrameNormalization match;
            match.method = *request.normalization.globalMethod;
            match.estimator = request.normalization.scaleEstimator;
            match.channelCount = extent.channelCount();
            for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
            {
                const bool reference = frame == *document.referenceIndex;
                match.channels[channel] = an::NormalizationChannelCoefficients{
                    .scale = reference ? 1.0 : 1.02 + 0.01 * frame, .offset = reference ? 0.0 : -0.004 * (frame + 1u)};
            }
            record.global = match;
        }
        document.frames.push_back(std::move(record));
    }
    return document;
}

/// A global match for one frame of a model.
[[nodiscard]] an::FrameNormalization makeMatch(const an::StackModel& model, std::uint32_t frame)
{
    const an::StackRequest& request = model.request();
    const std::uint32_t channels = request.input.descriptor.extent().channelCount();
    const bool multiplicative = an::normalizationMethodIsMultiplicative(*request.normalization.globalMethod);
    const bool reference = frame == request.normalization.referenceFrame;
    an::FrameNormalization match;
    match.method = *request.normalization.globalMethod;
    match.estimator = request.normalization.scaleEstimator;
    match.channelCount = channels;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        match.channels[channel] = an::NormalizationChannelCoefficients{
            .scale = reference ? 1.0 : 0.9 + 0.07 * frame + 0.01 * channel,
            .offset = reference || multiplicative ? 0.0 : 0.003 * (frame + 1u) - 0.001 * channel};
    }
    return match;
}

/// The working set's coefficient table filled from one match per frame.
[[nodiscard]] std::vector<double> makeTable(const an::StackModel& model,
                                            std::span<const an::FrameNormalization> matches)
{
    const an::StackRequest& request = model.request();
    std::vector<double> table(static_cast<std::size_t>(request.input.frameCount) *
                                  request.input.descriptor.extent().channelCount() * 2u,
                              0.0);
    for (std::uint32_t frame = 0u; frame < request.input.frameCount; ++frame)
    {
        const an::Status stored = an::storeStackFrameNormalization(model, frame, matches[frame], table);
        EXPECT_TRUE(stored.ok()) << stored.message();
    }
    return table;
}

/// Converts a whole frame through the source path on a fresh execution and returns the outcome.
[[nodiscard]] an::Result<an::StackSourceOutcome>
convertWhole(const an::EngineContext& engine, an::ComputeBackend backend, const an::StackModel& model,
             const an::StackSourceContract& contract, const OwnedFrame& frame, std::span<const double> table,
             std::span<const std::span<std::byte>> scratch, OwnedTile& tile)
{
    const an::ExecutionContext execution = makeExecution(engine, backend);
    return an::convertStackSourceBand(execution, model, contract, frame.band(), table, scratch, tile.band());
}

/// Every representation the stack admits, in enumeration order.
constexpr std::array<an::SampleType, 8u> kEveryType{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

// The constant-expression half of the contract: the stages follow from the model and the contract.
constexpr std::optional<an::ImageExtent> kConstantExtent =
    an::ImageExtent::create(16u, 16u, 1u, an::defaultEngineLimits());
static_assert(kConstantExtent.has_value());
constexpr std::optional<an::ImageDescriptor> kConstantDescriptor =
    an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *kConstantExtent,
                                                           .sampleType = an::SampleType::UInt16,
                                                           .channelSchema = an::ChannelSchema::Mono,
                                                           .colorSpace = an::ColorSpace::Unspecified,
                                                           .numericDomain = an::NumericDomain::NormalizedLinear,
                                                           .cfa = {},
                                                           .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                           .pixelAspectRatio = 1.0});
static_assert(kConstantDescriptor.has_value());
constexpr std::optional<an::StackModel> kConstantModel = an::StackModel::create(
    an::StackRequest{.input = an::StackInputDescription{.frameCount = 4u, .descriptor = *kConstantDescriptor}});
static_assert(kConstantModel.has_value());
static_assert(an::stackSourceStagesOf(*kConstantModel, an::StackSourceContract{}).convert);
static_assert(!an::stackSourceStagesOf(*kConstantModel, an::StackSourceContract{}).calibrate);
static_assert(an::stackSourceStagesOf(*kConstantModel, an::StackSourceContract{}).normalizationPrepared);
static_assert(an::stackSourceCalibrationDescriptor(*kConstantModel).sampleType() == an::SampleType::Float32);
static_assert(an::stackTileFrameBytes(*kConstantModel, 4u) == 16u * 4u * 4u);
static_assert(an::stackSourceScratchBytesPerWorker(*kConstantModel) == 0u);
static_assert(an::isStackFrameState(an::StackFrameState::LocalNormalized));
static_assert(!an::isStackFrameState(static_cast<an::StackFrameState>(3)));

TEST(StackSourceTest, NamesEveryStateAndDerivesTheStagesFromTheModelAndTheContract)
{
    EXPECT_STREQ(an::stackFrameStateName(an::StackFrameState::Raw), "Raw");
    EXPECT_STREQ(an::stackFrameStateName(an::StackFrameState::Calibrated), "Calibrated");
    EXPECT_STREQ(an::stackFrameStateName(an::StackFrameState::LocalNormalized), "LocalNormalized");
    EXPECT_STREQ(an::stackFrameStateName(static_cast<an::StackFrameState>(7)), "UnknownStackFrameState");

    const an::StackModel plain = makeModel(baseRequest());
    const an::StackModel wide = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    const an::StackModel global = makeModel(scopedRequest(an::StackNormalizationScope::Global));
    const an::StackModel local = makeModel(scopedRequest(an::StackNormalizationScope::Local));
    const an::StackModel both = makeModel(scopedRequest(an::StackNormalizationScope::LocalThenGlobal));
    const an::LocalNormalizationDocument document = makeDocument(local, false);
    const std::array<an::CalibrationPlan, 1u> plans{};
    const an::CalibrationMasters masters{};

    EXPECT_FALSE(an::stackSourceStagesOf(plain, an::StackSourceContract{}).any());
    EXPECT_TRUE(an::stackSourceStagesOf(wide, an::StackSourceContract{}).convert);
    EXPECT_TRUE(an::stackSourceStagesOf(plain, an::StackSourceContract{.state = an::StackFrameState::Raw,
                                                                       .calibration = plans,
                                                                       .masters = &masters})
                    .calibrate);
    EXPECT_FALSE(an::stackSourceStagesOf(plain, an::StackSourceContract{.state = an::StackFrameState::Raw}).calibrate);
    const an::StackSourceStages globalStages = an::stackSourceStagesOf(global, an::StackSourceContract{});
    EXPECT_TRUE(globalStages.normalizeGlobally);
    EXPECT_FALSE(globalStages.normalizeLocally);
    EXPECT_FALSE(globalStages.normalizationPrepared);
    const an::StackSourceStages localStages =
        an::stackSourceStagesOf(local, an::StackSourceContract{.localNormalization = &document});
    EXPECT_TRUE(localStages.normalizeLocally);
    EXPECT_FALSE(localStages.normalizeGlobally);
    EXPECT_TRUE(localStages.normalizationPrepared);
    EXPECT_FALSE(an::stackSourceStagesOf(local, an::StackSourceContract{}).normalizationPrepared);
    const an::StackSourceStages skipped =
        an::stackSourceStagesOf(both, an::StackSourceContract{.state = an::StackFrameState::LocalNormalized});
    EXPECT_FALSE(skipped.normalizeLocally);
    EXPECT_TRUE(skipped.normalizeGlobally);
    EXPECT_FALSE(skipped.normalizationPrepared);
    const an::StackSourceStages bothStages =
        an::stackSourceStagesOf(both, an::StackSourceContract{.localNormalization = &document});
    EXPECT_TRUE(bothStages.normalizeLocally && bothStages.normalizeGlobally && bothStages.normalizationPrepared);
}

TEST(StackSourceTest, ContractRefusesWhatItCannotServe)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel plain = makeModel(baseRequest());
    const an::StackModel local = makeModel(scopedRequest(an::StackNormalizationScope::Local));
    const an::StackModel global = makeModel(scopedRequest(an::StackNormalizationScope::Global));
    const an::StackModel both = makeModel(scopedRequest(an::StackNormalizationScope::LocalThenGlobal));

    OwnedFrame bias;
    bias.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    bias.fill(3u);
    const an::ImageDescriptor description = an::stackSourceCalibrationDescriptor(plain);
    const an::CalibrationMasters masters{.bias = bias.calibrationView(description)};
    const an::Result<an::CalibrationPlan> planned = an::prepareImageCalibration(execution, description, masters, {});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    const std::array<an::CalibrationPlan, 1u> plans{*planned};
    const std::array<an::CalibrationPlan, 2u> twoPlans{*planned, *planned};
    const std::array<an::CalibrationPlan, 1u> emptyPlans{};
    const an::LocalNormalizationDocument document = makeDocument(local, true);
    const an::LocalNormalizationDocument withMatches =
        makeDocument(both, true, an::LocalGridInterpolation::Bicubic, true);
    std::array<an::FrameNormalization, kFrames> matches{makeMatch(global, 0u), makeMatch(global, 1u),
                                                        makeMatch(global, 2u)};

    // What the table admits.
    {
        const an::Result<an::StackSourceStages> raw = an::checkStackSourceContract(
            plain,
            an::StackSourceContract{.state = an::StackFrameState::Raw, .calibration = plans, .masters = &masters});
        ASSERT_TRUE(raw.hasValue()) << raw.status().message();
        EXPECT_TRUE(raw->calibrate);
        const an::Result<an::StackSourceStages> fitted =
            an::checkStackSourceContract(local, an::StackSourceContract{.localNormalization = &document});
        ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
        EXPECT_TRUE(fitted->normalizeLocally && fitted->normalizationPrepared);
        const an::Result<an::StackSourceStages> prepared =
            an::checkStackSourceContract(global, an::StackSourceContract{.normalization = matches});
        ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();
        EXPECT_TRUE(prepared->normalizationPrepared);
        const an::Result<an::StackSourceStages> measured =
            an::checkStackSourceContract(global, an::StackSourceContract{});
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        EXPECT_FALSE(measured->normalizationPrepared);
        const an::Result<an::StackSourceStages> composed =
            an::checkStackSourceContract(both, an::StackSourceContract{.localNormalization = &withMatches});
        ASSERT_TRUE(composed.hasValue()) << composed.status().message();
        EXPECT_TRUE(composed->normalizeLocally && composed->normalizeGlobally && composed->normalizationPrepared);
    }

    struct Refusal
    {
        const an::StackModel* model;
        an::StackSourceContract contract;
        an::ErrorCode code;
        const char* parameter;
    };
    const std::vector<Refusal> refusals{
        {&plain, an::StackSourceContract{.state = static_cast<an::StackFrameState>(9)}, an::ErrorCode::InvalidArgument,
         "contract.state"},
        {&plain,
         an::StackSourceContract{.state = an::StackFrameState::Calibrated, .calibration = plans, .masters = &masters},
         an::ErrorCode::IncompatibleArguments, "contract.calibration"},
        {&plain,
         an::StackSourceContract{.state = an::StackFrameState::Raw, .calibration = twoPlans, .masters = &masters},
         an::ErrorCode::IncompatibleArguments, "contract.calibration"},
        {&plain,
         an::StackSourceContract{.state = an::StackFrameState::Raw, .calibration = emptyPlans, .masters = &masters},
         an::ErrorCode::InvalidArgument, "contract.calibration"},
        {&plain, an::StackSourceContract{.state = an::StackFrameState::Raw, .calibration = plans},
         an::ErrorCode::InvalidArgument, "contract.masters"},
        {&plain, an::StackSourceContract{.state = an::StackFrameState::Raw, .masters = &masters},
         an::ErrorCode::IncompatibleArguments, "contract.masters"},
        {&plain, an::StackSourceContract{.localNormalization = &document}, an::ErrorCode::IncompatibleArguments,
         "contract.localNormalization"},
        {&local,
         an::StackSourceContract{.state = an::StackFrameState::LocalNormalized, .localNormalization = &document},
         an::ErrorCode::IncompatibleArguments, "contract.localNormalization"},
        {&local, an::StackSourceContract{.localNormalization = &withMatches}, an::ErrorCode::IncompatibleArguments,
         "contract.localNormalization"},
        {&both, an::StackSourceContract{.localNormalization = &document}, an::ErrorCode::IncompatibleArguments,
         "contract.localNormalization"},
        {&plain, an::StackSourceContract{.normalization = matches}, an::ErrorCode::IncompatibleArguments,
         "contract.normalization"},
        {&both, an::StackSourceContract{.normalization = matches, .localNormalization = &withMatches},
         an::ErrorCode::IncompatibleArguments, "contract.normalization"},
        {&global, an::StackSourceContract{.normalization = std::span<const an::FrameNormalization>(matches).first(2u)},
         an::ErrorCode::IncompatibleArguments, "contract.normalization"},
    };
    for (std::size_t index = 0u; index < refusals.size(); ++index)
    {
        const Refusal& refusal = refusals[index];
        const an::Result<an::StackSourceStages> checked =
            an::checkStackSourceContract(*refusal.model, refusal.contract);
        ASSERT_FALSE(checked.hasValue()) << index;
        EXPECT_EQ(checked.status().code(), refusal.code) << index << ": " << checked.status().message();
        EXPECT_EQ(textField(checked.status(), an::ContextKey::Parameter), std::string(refusal.parameter)) << index;
    }

    // A plan prepared for another description, a master of another rule, and a document of another shape.
    {
        OwnedFrame widerBias;
        widerBias.create(an::SampleType::Float32, kWidth + 2u, kHeight, 1u);
        const an::ImageDescriptor wider = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono,
                                                         an::NumericDomain::NormalizedLinear, kWidth + 2u, kHeight);
        const an::CalibrationMasters widerMasters{.bias = widerBias.calibrationView(wider)};
        const an::Result<an::CalibrationPlan> widerPlan =
            an::prepareImageCalibration(execution, wider, widerMasters, {});
        ASSERT_TRUE(widerPlan.hasValue()) << widerPlan.status().message();
        const std::array<an::CalibrationPlan, 1u> widerPlans{*widerPlan};
        const an::Result<an::StackSourceStages> checked = an::checkStackSourceContract(
            plain, an::StackSourceContract{
                       .state = an::StackFrameState::Raw, .calibration = widerPlans, .masters = &widerMasters});
        ASSERT_FALSE(checked.hasValue());
        EXPECT_EQ(checked.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(checked.status(), an::ContextKey::Parameter), "contract.calibration");

        OwnedFrame zeroBias;
        zeroBias.create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
        zeroBias.fill(4u);
        const an::CalibrationMasters zeroMasters{.bias = zeroBias.calibrationView(description)};
        const an::Result<an::StackSourceStages> masked = an::checkStackSourceContract(
            plain,
            an::StackSourceContract{.state = an::StackFrameState::Raw, .calibration = plans, .masters = &zeroMasters});
        ASSERT_FALSE(masked.hasValue());
        EXPECT_EQ(masked.status().code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(textField(masked.status(), an::ContextKey::Parameter), "contract.masters->bias");

        an::LocalNormalizationDocument short_ = document;
        short_.frames.pop_back();
        const an::Result<an::StackSourceStages> fewer =
            an::checkStackSourceContract(local, an::StackSourceContract{.localNormalization = &short_});
        ASSERT_FALSE(fewer.hasValue());
        EXPECT_EQ(fewer.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(fewer.status(), "one record per frame"));

        an::LocalNormalizationDocument misplaced = document;
        misplaced.referenceIndex = 1u;
        const an::Result<an::StackSourceStages> reference =
            an::checkStackSourceContract(local, an::StackSourceContract{.localNormalization = &misplaced});
        ASSERT_FALSE(reference.hasValue());
        EXPECT_TRUE(messageNames(reference.status(), "reference frame"));

        an::LocalNormalizationDocument broken = document;
        broken.frames[1].background[3] = std::numeric_limits<double>::quiet_NaN();
        const an::Result<an::StackSourceStages> nonFinite =
            an::checkStackSourceContract(local, an::StackSourceContract{.localNormalization = &broken});
        ASSERT_FALSE(nonFinite.hasValue());
        EXPECT_EQ(nonFinite.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_TRUE(messageNames(nonFinite.status(), "non-finite node"));
    }
}

TEST(StackSourceTest, PreparedMatchesAreValidatedLikeTheFormerValidators)
{
    const an::StackModel global = makeModel(scopedRequest(an::StackNormalizationScope::Global));
    const an::StackModel multiplicative = makeModel(
        scopedRequest(an::StackNormalizationScope::Global, makeDescriptor(), an::NormalizationMethod::Multiplicative));
    std::array<an::FrameNormalization, kFrames> matches{makeMatch(global, 0u), makeMatch(global, 1u),
                                                        makeMatch(global, 2u)};

    const auto refused = [&](const an::StackModel& model, std::span<const an::FrameNormalization> set,
                             std::string_view fragment) {
        const an::Result<an::StackSourceStages> checked =
            an::checkStackSourceContract(model, an::StackSourceContract{.normalization = set});
        EXPECT_FALSE(checked.hasValue()) << fragment;
        if (!checked.hasValue())
        {
            EXPECT_TRUE(messageNames(checked.status(), fragment)) << checked.status().message();
            EXPECT_EQ(textField(checked.status(), an::ContextKey::Parameter), "contract.normalization");
        }
    };

    std::array<an::FrameNormalization, kFrames> wrongMethod = matches;
    wrongMethod[1].method = an::NormalizationMethod::Additive;
    refused(global, wrongMethod, "not the stack's global method");
    std::array<an::FrameNormalization, kFrames> wrongEstimator = matches;
    wrongEstimator[2].estimator = an::ScaleEstimator::MedianAbsoluteDeviation;
    refused(global, wrongEstimator, "estimator that is not the stack's");
    std::array<an::FrameNormalization, kFrames> wrongChannels = matches;
    wrongChannels[1].channelCount = 3u;
    refused(global, wrongChannels, "one coefficient pair per channel");
    std::array<an::FrameNormalization, kFrames> zeroScale = matches;
    zeroScale[2].channels[0].scale = 0.0;
    refused(global, zeroScale, "zero scale");
    std::array<an::FrameNormalization, kFrames> nonFinite = matches;
    nonFinite[1].channels[0].offset = std::numeric_limits<double>::infinity();
    refused(global, nonFinite, "non-finite coefficient");
    std::array<an::FrameNormalization, kFrames> movedReference = matches;
    movedReference[0].channels[0].offset = 0.01;
    refused(global, movedReference, "reference frame");
    std::array<an::FrameNormalization, kFrames> offsetUnderProduct{
        makeMatch(multiplicative, 0u), makeMatch(multiplicative, 1u), makeMatch(multiplicative, 2u)};
    offsetUnderProduct[1].channels[0].offset = 0.5;
    refused(multiplicative, offsetUnderProduct, "offset under a multiplicative method");

    const an::Result<an::StackSourceStages> admitted =
        an::checkStackSourceContract(global, an::StackSourceContract{.normalization = matches});
    EXPECT_TRUE(admitted.hasValue()) << admitted.status().message();
}

TEST(StackSourceTest, WidensEveryStoredRepresentationLikeTheImageConversion)
{
    const an::EngineContext engine = makeEngine();
    for (const an::SampleType type : kEveryType)
    {
        for (const an::NumericDomain domain : {an::NumericDomain::NormalizedLinear, an::NumericDomain::RawSensorCode})
        {
            const an::StackModel model = makeModel(baseRequest(makeDescriptor(type, an::ChannelSchema::Mono, domain)));
            OwnedFrame frame;
            frame.create(type, kWidth, kHeight, 1u);
            frame.fill(11u);
            frame.frameIndex = 1u;
            OwnedTile tile;
            tile.create(model, kHeight);

            const an::Result<an::StackSourceOutcome> outcome = convertWhole(
                engine, an::ComputeBackend::Automatic, model, an::StackSourceContract{}, frame, {}, {}, tile);
            ASSERT_TRUE(outcome.hasValue()) << an::sampleTypeName(type) << ": " << outcome.status().message();
            EXPECT_EQ(outcome->samplesWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
            EXPECT_EQ(outcome->samplesTransformed, outcome->samplesWritten);
            EXPECT_EQ(outcome->samplesPreserved, 0u);
            EXPECT_EQ(outcome->stages.convert, type != tile.sampleType);
            EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::AllValid);
            EXPECT_FALSE(outcome->inPlace);

            const an::ExecutionContext execution = makeExecution(engine);
            OwnedTile oracle;
            oracle.create(model, kHeight);
            an::ConversionCounters counters;
            const an::Status converted =
                an::convertImageSamples(execution, type, frame.bytes(), *frame.layout, tile.sampleType, oracle.bytes(),
                                        *oracle.layout, an::conversionPolicyFor(domain), counters);
            ASSERT_TRUE(converted.ok()) << converted.message();
            EXPECT_TRUE(sameBits(tile.written(), oracle.written())) << an::sampleTypeName(type);
            if (type == an::SampleType::UInt16 && domain == an::NumericDomain::RawSensorCode)
            {
                EXPECT_EQ(tile.at<float>(0u, 3u, 2u), static_cast<float>(frame.at<std::uint16_t>(0u, 3u, 2u)));
            }
            if (type == an::SampleType::UInt16 && domain == an::NumericDomain::NormalizedLinear)
            {
                EXPECT_EQ(tile.at<float>(0u, 3u, 2u),
                          static_cast<float>(static_cast<double>(frame.at<std::uint16_t>(0u, 3u, 2u)) / 65535.0));
            }
        }
    }

    // A frame stored at the tile width is written where it was read when the caller asks.
    {
        const an::StackModel model = makeModel(scopedRequest(an::StackNormalizationScope::Global));
        OwnedFrame frame;
        frame.create(an::SampleType::Float32, kWidth, kHeight, 1u);
        frame.fill(12u);
        frame.frameIndex = 1u;
        const std::array<an::FrameNormalization, kFrames> matches{makeMatch(model, 0u), makeMatch(model, 1u),
                                                                  makeMatch(model, 2u)};
        const std::vector<double> table = makeTable(model, matches);
        OwnedFrame copy = frame;
        copy.layout = frame.layout;
        const an::ExecutionContext execution = makeExecution(engine);
        const an::StackTileBand inPlace{
            .sampleType = an::SampleType::Float32, .samples = frame.bytes(), .layout = &*frame.layout};
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), table, {}, inPlace);
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_TRUE(outcome->inPlace);
        EXPECT_FALSE(outcome->stages.convert);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const float before = copy.at<float>(0u, x, 5u);
            const double scaled = static_cast<double>(before) * matches[1].channels[0].scale;
            EXPECT_EQ(frame.at<float>(0u, x, 5u), static_cast<float>(scaled + matches[1].channels[0].offset)) << x;
        }
    }
}

TEST(StackSourceTest, CalibratesABandExactlyAsCalibrateImage)
{
    const an::EngineContext engine = makeEngine();
    for (const an::SampleType stored : {an::SampleType::Float32, an::SampleType::UInt16})
    {
        for (const an::ChannelSchema schema : {an::ChannelSchema::Mono, an::ChannelSchema::Rgb})
        {
            const an::StackModel model = makeModel(baseRequest(makeDescriptor(stored, schema)));
            const std::uint32_t channels = schema == an::ChannelSchema::Rgb ? 3u : 1u;
            const an::ImageDescriptor description = an::stackSourceCalibrationDescriptor(model);
            const an::ImageDescriptor monoDescription =
                makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono);
            OwnedFrame bias;
            bias.create(an::SampleType::Float32, kWidth, kHeight, channels);
            bias.fill(21u);
            OwnedFrame dark;
            dark.create(an::SampleType::Float32, kWidth, kHeight, channels);
            dark.fill(22u);
            OwnedFrame flat;
            const bool monoFlat = schema == an::ChannelSchema::Rgb;
            flat.create(an::SampleType::Float32, kWidth, kHeight, monoFlat ? 1u : channels);
            flat.fill(23u);
            flat.put(0u, 4u, 4u, 0.0); // A zero flat sample, which produces a quiet NaN.
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        bias.put(channel, x, y, 0.01 * patterned(x + y * kWidth, 31u + channel));
                        dark.put(channel, x, y, 0.02 * patterned(x + y * kWidth, 32u + channel));
                    }
                }
            }
            const an::CalibrationMasters masters{.bias = bias.calibrationView(description),
                                                 .dark = dark.calibrationView(description),
                                                 .flat =
                                                     flat.calibrationView(monoFlat ? monoDescription : description)};
            const an::CalibrationRequest request{.pedestal = 0.001,
                                                 .darkScaling = an::DarkScalingMode::Fixed,
                                                 .darkScale = 1.25,
                                                 .flatNormalization = an::FlatNormalizationMode::CentralMean};
            const an::ExecutionContext execution = makeExecution(engine);
            const an::Result<an::CalibrationPlan> planned =
                an::prepareImageCalibration(execution, description, masters, request);
            ASSERT_TRUE(planned.hasValue()) << planned.status().message();
            const std::array<an::CalibrationPlan, 1u> plans{*planned};
            const an::StackSourceContract contract{
                .state = an::StackFrameState::Raw, .calibration = plans, .masters = &masters};
            ASSERT_TRUE(an::checkStackSourceContract(model, contract).hasValue())
                << an::checkStackSourceContract(model, contract).status().message();

            OwnedFrame frame;
            frame.create(stored, kWidth, kHeight, channels);
            frame.fill(24u);
            frame.frameIndex = 2u;
            OwnedTile tile;
            tile.create(model, kHeight);
            const an::Result<an::StackSourceOutcome> outcome =
                an::convertStackSourceBand(execution, model, contract, frame.band(), {}, {}, tile.band());
            ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
            EXPECT_TRUE(outcome->stages.calibrate);
            EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::FiniteOnly);
            EXPECT_EQ(outcome->nonFiniteOutputs, static_cast<std::uint64_t>(monoFlat ? channels : 1u));
            EXPECT_EQ(outcome->invalidCalibrationInputs, 0u);

            // The oracle: the light widened by the image conversion, then the calibration executor.
            OwnedFrame widened;
            widened.create(an::SampleType::Float32, kWidth, kHeight, channels);
            an::ConversionCounters counters;
            ASSERT_TRUE(an::convertImageSamples(execution, stored, frame.bytes(), *frame.layout,
                                                an::SampleType::Float32, widened.bytes(), *widened.layout,
                                                an::conversionPolicyFor(an::NumericDomain::NormalizedLinear), counters)
                            .ok());
            OwnedTile oracle;
            oracle.create(model, kHeight);
            const an::Result<an::CalibrationResult> calibrated =
                an::calibrateImage(execution, *planned, widened.calibrationView(description), masters, oracle.bytes(),
                                   oracle.layout->totalBytes(), *oracle.layout);
            ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
            EXPECT_TRUE(sameBits(tile.written(), oracle.written()))
                << an::sampleTypeName(stored) << ' ' << an::channelSchemaName(schema);
            EXPECT_TRUE(std::isnan(tile.at<float>(0u, 4u, 4u)));
        }
    }

    // Excluded inputs become a quiet NaN, counted apart from the flat's.
    {
        const an::StackModel model = makeModel(baseRequest());
        const an::ImageDescriptor description = an::stackSourceCalibrationDescriptor(model);
        OwnedFrame bias;
        bias.create(an::SampleType::Float32, kWidth, kHeight, 1u);
        bias.fill(41u);
        const an::CalibrationMasters masters{.bias = bias.calibrationView(description)};
        const an::ExecutionContext execution = makeExecution(engine);
        const an::Result<an::CalibrationPlan> planned =
            an::prepareImageCalibration(execution, description, masters, {});
        ASSERT_TRUE(planned.hasValue()) << planned.status().message();
        const std::array<an::CalibrationPlan, 1u> plans{*planned};
        const an::StackSourceContract contract{
            .state = an::StackFrameState::Raw, .calibration = plans, .masters = &masters};
        OwnedFrame frame;
        frame.create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
        frame.fill(42u);
        frame.put(0u, 1u, 1u, 0.0);
        frame.put(0u, 7u, 3u, 0.0);
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, contract, frame.band(), {}, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->invalidCalibrationInputs, 2u);
        EXPECT_EQ(outcome->samplesPreserved, 2u);
        EXPECT_EQ(outcome->nonFiniteOutputs, 0u);
        EXPECT_TRUE(std::isnan(tile.at<float>(0u, 1u, 1u)));
        EXPECT_TRUE(std::isnan(tile.at<float>(0u, 7u, 3u)));
        EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::FiniteOnly);
    }
}

TEST(StackSourceTest, CalibratesAMosaicWithPerSiteFlatMeans)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(cfa.has_value());
    const an::ImageDescriptor stored = makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Mono,
                                                      an::NumericDomain::RawSensorCode, kWidth, kHeight, *cfa);
    const an::StackModel model = makeModel(baseRequest(stored));
    const an::ImageDescriptor description = an::stackSourceCalibrationDescriptor(model);
    EXPECT_TRUE(description.isMosaiced());

    OwnedFrame flat;
    flat.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            flat.at<float>(0u, x, y) = 20000.0f + 3000.0f * static_cast<float>((y % 2u) * 2u + (x % 2u)) +
                                       500.0f * static_cast<float>(patterned(x + y * kWidth, 51u));
        }
    }
    OwnedFrame bias;
    bias.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            bias.at<float>(0u, x, y) = 500.0f + 10.0f * static_cast<float>(patterned(x + y * kWidth, 52u));
        }
    }
    const an::CalibrationMasters masters{.bias = bias.calibrationView(description),
                                         .flat = flat.calibrationView(description)};
    const an::CalibrationRequest request{.flatNormalization = an::FlatNormalizationMode::CentralMean};
    const an::Result<an::CalibrationPlan> planned =
        an::prepareImageCalibration(execution, description, masters, request);
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->flatNormalizationGroupCount(), 4u);
    const std::array<an::CalibrationPlan, 1u> plans{*planned};
    const an::StackSourceContract contract{
        .state = an::StackFrameState::Raw, .calibration = plans, .masters = &masters};

    OwnedFrame frame;
    frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            frame.at<std::uint16_t>(0u, x, y) = static_cast<std::uint16_t>(30000u + 900u * ((x + y) % 7u));
        }
    }
    OwnedTile tile;
    tile.create(model, kHeight);
    const an::Result<an::StackSourceOutcome> outcome =
        an::convertStackSourceBand(execution, model, contract, frame.band(), {}, {}, tile.band());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();

    OwnedFrame widened;
    widened.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    an::ConversionCounters counters;
    ASSERT_TRUE(an::convertImageSamples(execution, an::SampleType::UInt16, frame.bytes(), *frame.layout,
                                        an::SampleType::Float32, widened.bytes(), *widened.layout,
                                        an::conversionPolicyFor(an::NumericDomain::RawSensorCode), counters)
                    .ok());
    OwnedTile oracle;
    oracle.create(model, kHeight);
    const an::Result<an::CalibrationResult> calibrated =
        an::calibrateImage(execution, *planned, widened.calibrationView(description), masters, oracle.bytes(),
                           oracle.layout->totalBytes(), *oracle.layout);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    EXPECT_EQ(calibrated->path, an::CalibrationExecutionPath::Reference);
    EXPECT_TRUE(sameBits(tile.written(), oracle.written()));
    // The four sites divide by four different means, so neighbours that were equal codes differ.
    EXPECT_NE(tile.at<float>(0u, 0u, 0u), tile.at<float>(0u, 1u, 1u));
}

TEST(StackSourceTest, AppliesTheGlobalMatchExactlyAsApplyGlobalNormalization)
{
    const an::EngineContext engine = makeEngine();
    for (const an::NormalizationMethod method :
         {an::NormalizationMethod::AdditiveScaling, an::NormalizationMethod::MultiplicativeScaling})
    {
        for (const an::ValidityRule rule : {an::ValidityRule::AllValid, an::ValidityRule::ZeroIsNoData})
        {
            const an::ImageDescriptor descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb);
            const an::StackModel model =
                makeModel(scopedRequest(an::StackNormalizationScope::Global, descriptor, method));
            const std::array<an::FrameNormalization, kFrames> matches{makeMatch(model, 0u), makeMatch(model, 1u),
                                                                      makeMatch(model, 2u)};
            const std::vector<double> table = makeTable(model, matches);
            const an::ExecutionContext execution = makeExecution(engine);
            for (std::uint32_t frameIndex = 0u; frameIndex < kFrames; ++frameIndex)
            {
                OwnedFrame frame;
                frame.create(an::SampleType::Float32, kWidth, kHeight, 3u, rule, frameIndex == 1u);
                frame.fill(61u + frameIndex);
                frame.frameIndex = frameIndex;
                if (rule == an::ValidityRule::ZeroIsNoData)
                {
                    frame.put(0u, 2u, 2u, 0.0);
                    frame.put(1u, 5u, 6u, 0.0);
                    // A sample that lands exactly on zero after the match, moved off the marker.
                    if (frameIndex != 0u && method == an::NormalizationMethod::AdditiveScaling)
                    {
                        frame.at<float>(2u, 3u, 3u) = static_cast<float>(-matches[frameIndex].channels[2].offset /
                                                                         matches[frameIndex].channels[2].scale);
                    }
                }
                OwnedTile tile;
                tile.create(model, kHeight);
                const an::Result<an::StackSourceOutcome> outcome =
                    an::convertStackSourceBand(execution, model, an::StackSourceContract{.normalization = matches},
                                               frame.band(), table, {}, tile.band());
                ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
                EXPECT_TRUE(outcome->stages.normalizeGlobally);
                EXPECT_EQ(outcome->validity.rule(), rule);

                OwnedFrame oracle;
                oracle.create(an::SampleType::Float32, kWidth, kHeight, 3u, rule, frameIndex == 1u);
                const an::Result<an::NormalizationApplyResult> applied =
                    an::applyGlobalNormalization(execution, matches[frameIndex], frame.normalizationView(descriptor),
                                                 oracle.bytes(), oracle.layout->totalBytes(), *oracle.layout);
                ASSERT_TRUE(applied.hasValue()) << applied.status().message();
                // The oracle writes the frame's own arrangement; compare sample by sample.
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    for (std::uint32_t y = 0u; y < kHeight; ++y)
                    {
                        for (std::uint32_t x = 0u; x < kWidth; ++x)
                        {
                            const std::uint64_t offset = frame.offsetOf(channel, x, y);
                            const float expected = *reinterpret_cast<const float*>(oracle.bytes() + offset);
                            const float actual = tile.at<float>(channel, x, y);
                            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), std::bit_cast<std::uint32_t>(expected))
                                << an::normalizationMethodName(method) << ' ' << frameIndex << ' ' << channel << ','
                                << x << ',' << y;
                        }
                    }
                }
                EXPECT_EQ(outcome->samplesPreserved, applied->samplesPreserved);
                EXPECT_EQ(outcome->noDataCollisions, applied->noDataCollisions);
            }
        }
    }

    // A match that lands an admitted sample exactly on zero under the zero rule moves it off the
    // marker and counts it, as the apply does: a unit scale and an offset that is the sample's negative.
    {
        const an::StackModel model = makeModel(scopedRequest(an::StackNormalizationScope::Global));
        std::array<an::FrameNormalization, kFrames> matches{makeMatch(model, 0u), makeMatch(model, 1u),
                                                            makeMatch(model, 2u)};
        matches[1].channels[0] = an::NormalizationChannelCoefficients{.scale = 1.0, .offset = -0.25};
        const std::vector<double> table = makeTable(model, matches);
        const an::ExecutionContext execution = makeExecution(engine);
        OwnedFrame frame;
        frame.create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
        frame.fill(69u);
        frame.frameIndex = 1u;
        frame.at<float>(0u, 3u, 3u) = 0.25f;
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome = an::convertStackSourceBand(
            execution, model, an::StackSourceContract{.normalization = matches}, frame.band(), table, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->noDataCollisions, 1u);
        EXPECT_NE(tile.at<float>(0u, 3u, 3u), 0.0f);
        EXPECT_EQ(tile.at<float>(0u, 3u, 3u), (std::numeric_limits<float>::min)());
        OwnedFrame oracle;
        oracle.create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
        const an::Result<an::NormalizationApplyResult> applied =
            an::applyGlobalNormalization(execution, matches[1], frame.normalizationView(makeDescriptor()),
                                         oracle.bytes(), oracle.layout->totalBytes(), *oracle.layout);
        ASSERT_TRUE(applied.hasValue()) << applied.status().message();
        EXPECT_EQ(applied->noDataCollisions, 1u);
        EXPECT_TRUE(sameBits(
            tile.written(),
            std::span<const std::byte>(oracle.bytes(), static_cast<std::size_t>(oracle.layout->totalBytes()))));
    }
}

TEST(StackSourceTest, AppliesTheLocalDocumentExactlyAsApplyLocalNormalization)
{
    const an::EngineContext engine = makeEngine(4u);
    for (const an::LocalGridInterpolation interpolation :
         {an::LocalGridInterpolation::Bicubic, an::LocalGridInterpolation::Bilinear})
    {
        for (const bool scaleGrids : {false, true})
        {
            const an::ImageDescriptor descriptor = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb);
            const an::StackModel model = makeModel(scopedRequest(an::StackNormalizationScope::Local, descriptor));
            const an::LocalNormalizationDocument document = makeDocument(model, scaleGrids, interpolation);
            const an::StackSourceContract contract{.localNormalization = &document};
            ASSERT_TRUE(an::checkStackSourceContract(model, contract).hasValue());
            OwnedScratch scratch;
            scratch.create(model, 4u);
            const an::ExecutionContext execution = makeExecution(engine);
            for (std::uint32_t frameIndex = 0u; frameIndex < kFrames; ++frameIndex)
            {
                OwnedFrame frame;
                frame.create(an::SampleType::Float32, kWidth, kHeight, 3u, an::ValidityRule::AllValid,
                             frameIndex == 2u);
                frame.fill(71u + frameIndex);
                frame.frameIndex = frameIndex;
                OwnedTile tile;
                tile.create(model, kHeight);
                const an::Result<an::StackSourceOutcome> outcome = an::convertStackSourceBand(
                    execution, model, contract, frame.band(), {}, scratch.spans(), tile.band());
                ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
                EXPECT_TRUE(outcome->stages.normalizeLocally);

                const an::LocalNormalizationFrameCoefficients& record = document.frames[frameIndex];
                const an::LocalNormalizationApplyRequest apply{.shape = document.shape,
                                                               .interpolation = interpolation,
                                                               .targetBackground = record.background,
                                                               .referenceBackground = document.referenceBackground,
                                                               .scale =
                                                                   std::span<const double>(record.scale.data(), 3u),
                                                               .scaleGrid = record.scaleGrid};
                OwnedFrame oracle;
                oracle.create(an::SampleType::Float32, kWidth, kHeight, 3u, an::ValidityRule::AllValid,
                              frameIndex == 2u);
                const an::Result<an::LocalNormalizationApplyResult> applied =
                    an::applyLocalNormalization(execution, apply, frame.normalizationView(descriptor), oracle.bytes(),
                                                oracle.layout->totalBytes(), *oracle.layout);
                ASSERT_TRUE(applied.hasValue()) << applied.status().message();
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    for (std::uint32_t y = 0u; y < kHeight; ++y)
                    {
                        for (std::uint32_t x = 0u; x < kWidth; ++x)
                        {
                            const float expected =
                                frameIndex == *document.referenceIndex
                                    ? frame.at<float>(channel, x, y)
                                    : *reinterpret_cast<const float*>(oracle.bytes() + frame.offsetOf(channel, x, y));
                            EXPECT_EQ(std::bit_cast<std::uint32_t>(tile.at<float>(channel, x, y)),
                                      std::bit_cast<std::uint32_t>(expected))
                                << frameIndex << ' ' << channel << ',' << x << ',' << y;
                        }
                    }
                }
            }
        }
    }
}

TEST(StackSourceTest, ComposesLocalThenGlobalOnTheCorrectedSamplesAndSkipsWhatIsAlreadyLocal)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = makeDescriptor();
    const an::StackModel model = makeModel(scopedRequest(an::StackNormalizationScope::LocalThenGlobal, descriptor));
    const an::LocalNormalizationDocument document =
        makeDocument(model, true, an::LocalGridInterpolation::Bicubic, true);
    std::vector<double> table(kFrames * 2u, 0.0);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        ASSERT_TRUE(an::storeStackFrameNormalization(model, frame, *document.frames[frame].global, table).ok());
    }
    OwnedScratch scratch;
    scratch.create(model, 2u);

    OwnedFrame frame;
    frame.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    frame.fill(81u);
    frame.frameIndex = 2u;
    OwnedTile tile;
    tile.create(model, kHeight);
    const an::StackSourceContract contract{.localNormalization = &document};
    const an::Result<an::StackSourceOutcome> outcome =
        an::convertStackSourceBand(execution, model, contract, frame.band(), table, scratch.spans(), tile.band());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_TRUE(outcome->stages.normalizeLocally && outcome->stages.normalizeGlobally);

    // The oracle: the local apply into a frame, then the global apply over that frame.
    const an::LocalNormalizationFrameCoefficients& record = document.frames[2];
    OwnedFrame corrected;
    corrected.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    const an::LocalNormalizationApplyRequest apply{.shape = document.shape,
                                                   .interpolation = an::LocalGridInterpolation::Bicubic,
                                                   .targetBackground = record.background,
                                                   .referenceBackground = document.referenceBackground,
                                                   .scale = std::span<const double>(record.scale.data(), 1u),
                                                   .scaleGrid = record.scaleGrid};
    ASSERT_TRUE(an::applyLocalNormalization(execution, apply, frame.normalizationView(descriptor), corrected.bytes(),
                                            corrected.layout->totalBytes(), *corrected.layout)
                    .hasValue());
    OwnedTile oracle;
    oracle.create(model, kHeight);
    ASSERT_TRUE(an::applyGlobalNormalization(execution, *record.global, corrected.normalizationView(descriptor),
                                             oracle.bytes(), oracle.layout->totalBytes(), *oracle.layout)
                    .hasValue());
    EXPECT_TRUE(sameBits(tile.written(), oracle.written()));

    // Frames already brought to the reference take the global match alone, from the contract's matches.
    const std::array<an::FrameNormalization, kFrames> matches{*document.frames[0].global, *document.frames[1].global,
                                                              *document.frames[2].global};
    const an::StackSourceContract already{.state = an::StackFrameState::LocalNormalized, .normalization = matches};
    const an::Result<an::StackSourceStages> stages = an::checkStackSourceContract(model, already);
    ASSERT_TRUE(stages.hasValue()) << stages.status().message();
    EXPECT_FALSE(stages->normalizeLocally);
    EXPECT_TRUE(stages->normalizeGlobally && stages->normalizationPrepared);
    OwnedTile skipped;
    skipped.create(model, kHeight);
    const an::Result<an::StackSourceOutcome> onlyGlobal =
        an::convertStackSourceBand(execution, model, already, frame.band(), table, {}, skipped.band());
    ASSERT_TRUE(onlyGlobal.hasValue()) << onlyGlobal.status().message();
    OwnedTile globalOracle;
    globalOracle.create(model, kHeight);
    ASSERT_TRUE(an::applyGlobalNormalization(execution, *record.global, frame.normalizationView(descriptor),
                                             globalOracle.bytes(), globalOracle.layout->totalBytes(),
                                             *globalOracle.layout)
                    .hasValue());
    EXPECT_TRUE(sameBits(skipped.written(), globalOracle.written()));
}

TEST(StackSourceTest, CarriesEveryValidityRuleIntoTheTile)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Finite-only: the non-finite samples are preserved and the rule travels.
    {
        const an::StackModel model = makeModel(baseRequest());
        OwnedFrame frame;
        frame.create(an::SampleType::Float32, kWidth, kHeight, 1u, an::ValidityRule::FiniteOnly);
        frame.fill(91u);
        frame.at<float>(0u, 2u, 2u) = std::numeric_limits<float>::quiet_NaN();
        frame.at<float>(0u, 3u, 2u) = std::numeric_limits<float>::infinity();
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesPreserved, 2u);
        EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::FiniteOnly);
        EXPECT_TRUE(std::isnan(tile.at<float>(0u, 2u, 2u)));
        EXPECT_TRUE(std::isinf(tile.at<float>(0u, 3u, 2u)));
    }

    // An explicit mask: masked samples are copied and the tile keeps the mask rule.
    {
        const an::StackModel model =
            makeModel(scopedRequest(an::StackNormalizationScope::Global, makeDescriptor(an::SampleType::UInt16)));
        const std::array<an::FrameNormalization, kFrames> matches{makeMatch(model, 0u), makeMatch(model, 1u),
                                                                  makeMatch(model, 2u)};
        const std::vector<double> table = makeTable(model, matches);
        OwnedFrame frame;
        frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u, an::ValidityRule::ExplicitMask);
        frame.fill(92u);
        frame.frameIndex = 1u;
        frame.addMask(1u);
        frame.clearBit(0u, 4u, 1u);
        frame.clearBit(0u, 11u, 7u);
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome = an::convertStackSourceBand(
            execution, model, an::StackSourceContract{.normalization = matches}, frame.band(), table, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesPreserved, 2u);
        EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::ExplicitMask);
        EXPECT_EQ(tile.at<float>(0u, 4u, 1u), static_cast<float>(frame.at<std::uint16_t>(0u, 4u, 1u) / 65535.0));
        const double scaled = frame.at<std::uint16_t>(0u, 5u, 1u) / 65535.0 * matches[1].channels[0].scale;
        EXPECT_EQ(
            tile.at<float>(0u, 5u, 1u),
            static_cast<float>(static_cast<double>(static_cast<float>(frame.at<std::uint16_t>(0u, 5u, 1u) / 65535.0)) *
                                   matches[1].channels[0].scale +
                               matches[1].channels[0].offset))
            << scaled;
    }

    // Range membership is carried as finite-only with NaNs for excluded samples, and it cannot
    // pass a normalization that was not prepared under that output validity rule.
    {
        const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
        OwnedFrame frame;
        frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u, an::ValidityRule::DomainRange);
        frame.descriptor = ruleOf(an::ValidityRule::DomainRange, 100.0, 60000.0);
        frame.fill(93u);
        frame.at<std::uint16_t>(0u, 0u, 0u) = 50u;
        frame.at<std::uint16_t>(0u, 1u, 0u) = 65000u;
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesPreserved, 2u);
        EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::FiniteOnly);
        EXPECT_TRUE(std::isnan(tile.at<float>(0u, 0u, 0u)));
        EXPECT_TRUE(std::isnan(tile.at<float>(0u, 1u, 0u)));

        const an::StackModel normalized =
            makeModel(scopedRequest(an::StackNormalizationScope::Global, makeDescriptor(an::SampleType::UInt16)));
        const std::array<an::FrameNormalization, kFrames> matches{makeMatch(normalized, 0u), makeMatch(normalized, 1u),
                                                                  makeMatch(normalized, 2u)};
        const std::vector<double> table = makeTable(normalized, matches);
        OwnedTile refusedTile;
        refusedTile.create(normalized, kHeight);
        const an::Result<an::StackSourceOutcome> refused =
            an::convertStackSourceBand(execution, normalized, an::StackSourceContract{.normalization = matches},
                                       frame.band(), table, {}, refusedTile.band());
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
        EXPECT_EQ(textField(refused.status(), an::ContextKey::Parameter), "source.validity");
    }

    // Raw code frames preserve the same range membership without changing admitted code values.
    {
        const an::StackModel model = makeModel(baseRequest(
            makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Mono, an::NumericDomain::RawSensorCode)));
        OwnedFrame frame;
        frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u, an::ValidityRule::DomainRange);
        frame.descriptor = ruleOf(an::ValidityRule::DomainRange, 100.0, 60000.0);
        frame.fill(94u);
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::FiniteOnly);
    }

    // The zero marker is preserved as zero and the rule travels.
    {
        const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
        OwnedFrame frame;
        frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u, an::ValidityRule::ZeroIsNoData);
        frame.fill(95u);
        frame.at<std::uint16_t>(0u, 6u, 6u) = 0u;
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        EXPECT_EQ(outcome->samplesPreserved, 1u);
        EXPECT_EQ(outcome->validity.rule(), an::ValidityRule::ZeroIsNoData);
        EXPECT_EQ(tile.at<float>(0u, 6u, 6u), 0.0f);
    }

    // The document's rule and the band's must agree.
    {
        const an::StackModel model = makeModel(scopedRequest(an::StackNormalizationScope::Local));
        an::LocalNormalizationDocument document = makeDocument(model, false);
        document.validity = an::ValidityRule::ZeroIsNoData;
        OwnedScratch scratch;
        scratch.create(model, 2u);
        OwnedFrame frame;
        frame.create(an::SampleType::Float32, kWidth, kHeight, 1u);
        frame.fill(96u);
        frame.frameIndex = 1u;
        OwnedTile tile;
        tile.create(model, kHeight);
        const an::Result<an::StackSourceOutcome> refused =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{.localNormalization = &document},
                                       frame.band(), {}, scratch.spans(), tile.band());
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(refused.status(), an::ContextKey::Parameter), "source.validity");
    }
}

TEST(StackSourceTest, RefusesAnAdmittedNonFiniteSample)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest());
    OwnedFrame frame;
    frame.create(an::SampleType::Float32, kWidth, kHeight, 1u);
    frame.fill(101u);
    frame.at<float>(0u, 5u, 5u) = std::numeric_limits<float>::quiet_NaN();
    OwnedTile tile;
    tile.create(model, kHeight);
    const an::Result<an::StackSourceOutcome> outcome =
        an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
    ASSERT_FALSE(outcome.hasValue());
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(textField(outcome.status(), an::ContextKey::Parameter), "source.samples");
}

TEST(StackSourceTest, WindowsAWholeFrameIntoBandsThatConvertLikeTheWhole)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(scopedRequest(
        an::StackNormalizationScope::Local, makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Rgb)));
    an::LocalNormalizationDocument document = makeDocument(model, true);
    document.validity = an::ValidityRule::ExplicitMask;
    OwnedScratch scratch;
    scratch.create(model, 2u);
    OwnedFrame frame;
    frame.create(an::SampleType::UInt16, kWidth, kHeight, 3u, an::ValidityRule::ExplicitMask);
    frame.fill(111u);
    frame.frameIndex = 1u;
    frame.addMask(3u);
    frame.clearBit(1u, 3u, 4u);
    frame.clearBit(2u, 0u, 7u);

    OwnedTile whole;
    whole.create(model, kHeight);
    const an::StackSourceContract contract{.localNormalization = &document};
    const an::Result<an::StackSourceOutcome> all =
        an::convertStackSourceBand(execution, model, contract, frame.band(), {}, scratch.spans(), whole.band());
    ASSERT_TRUE(all.hasValue()) << all.status().message();
    EXPECT_EQ(all->samplesPreserved, 2u);

    // Three bands of three, three and two rows, each converted on its own, agree with the whole.
    constexpr std::array<std::uint32_t, 3u> kRows{3u, 3u, 2u};
    std::uint32_t firstRow = 0u;
    std::uint64_t preserved = 0u;
    for (const std::uint32_t rows : kRows)
    {
        const an::Result<an::StackSourceRows> window = an::stackSourceRowsOf(frame.band(), firstRow, rows);
        ASSERT_TRUE(window.hasValue()) << window.status().message();
        const an::StackSourceBand band = window->band();
        EXPECT_EQ(band.firstRow, firstRow);
        EXPECT_EQ(band.layout->extent().height(), rows);
        EXPECT_EQ(band.frameIndex, 1u);
        ASSERT_NE(band.validity.mask, nullptr);
        EXPECT_EQ(band.validity.mask->height(), rows);
        EXPECT_EQ(band.validity.mask->planeCount(), 3u);
        OwnedTile tile;
        tile.create(model, rows);
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, contract, band, {}, scratch.spans(), tile.band());
        ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
        preserved += outcome->samplesPreserved;
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < rows; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(tile.at<float>(channel, x, y)),
                              std::bit_cast<std::uint32_t>(whole.at<float>(channel, x, firstRow + y)))
                        << channel << ',' << x << ',' << firstRow + y;
                }
            }
        }
        firstRow += rows;
    }
    EXPECT_EQ(preserved, 2u);

    // The window refuses what does not lie inside the frame.
    const an::Result<an::StackSourceRows> outside = an::stackSourceRowsOf(frame.band(), 6u, 3u);
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::OutOfRange);
    const an::Result<an::StackSourceRows> empty = an::stackSourceRowsOf(frame.band(), 0u, 0u);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(StackSourceTest, StoresTheSameBitsAtEveryThreadCountAndOnEveryBackend)
{
    // Every representation through every stage, on one and four workers and on the reference and
    // the compiled backend: the four tiles hold the same bits.
    const an::EngineContext narrow = makeEngine(1u);
    const an::EngineContext wide = makeEngine(4u);
    std::uint64_t compiledRuns = 0u;
    for (const an::SampleType type : kEveryType)
    {
        const an::ImageDescriptor descriptor = makeDescriptor(type, an::ChannelSchema::Rgb);
        const an::StackModel model = makeModel(scopedRequest(an::StackNormalizationScope::LocalThenGlobal, descriptor));
        an::LocalNormalizationDocument document = makeDocument(model, true, an::LocalGridInterpolation::Bicubic, true);
        document.validity = an::ValidityRule::FiniteOnly;
        std::vector<double> table(kFrames * 3u * 2u, 0.0);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            ASSERT_TRUE(an::storeStackFrameNormalization(model, frame, *document.frames[frame].global, table).ok());
        }
        const an::ImageDescriptor description = an::stackSourceCalibrationDescriptor(model);
        OwnedFrame bias;
        bias.create(description.sampleType(), kWidth, kHeight, 3u);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    bias.put(channel, x, y, 0.01 * patterned(x + y * kWidth, 121u + channel));
                }
            }
        }
        const an::CalibrationMasters masters{.bias = bias.calibrationView(description)};
        an::Result<an::CalibrationPlan> planned =
            an::prepareImageCalibration(makeExecution(narrow), description, masters, {});
        ASSERT_TRUE(planned.hasValue()) << planned.status().message();
        const std::array<an::CalibrationPlan, 1u> plans{*planned};
        const an::StackSourceContract contract{.state = an::StackFrameState::Raw,
                                               .calibration = plans,
                                               .masters = &masters,
                                               .localNormalization = &document};
        ASSERT_TRUE(an::checkStackSourceContract(model, contract).hasValue())
            << an::checkStackSourceContract(model, contract).status().message();
        OwnedFrame frame;
        frame.create(type, kWidth, kHeight, 3u, an::ValidityRule::AllValid, type == an::SampleType::Int16);
        frame.fill(131u);
        frame.frameIndex = 1u;
        OwnedScratch scratch;
        scratch.create(model, 4u);

        std::vector<std::vector<std::byte>> tiles;
        for (const an::EngineContext* engine : {&narrow, &wide})
        {
            for (const an::ComputeBackend backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
            {
                OwnedTile tile;
                tile.create(model, kHeight);
                const an::Result<an::StackSourceOutcome> outcome =
                    convertWhole(*engine, backend, model, contract, frame, table, scratch.spans(), tile);
                ASSERT_TRUE(outcome.hasValue()) << an::sampleTypeName(type) << ": " << outcome.status().message();
                EXPECT_TRUE(outcome->stages.calibrate && outcome->stages.normalizeLocally &&
                            outcome->stages.normalizeGlobally);
                if (backend == an::ComputeBackend::Reference)
                {
                    EXPECT_FALSE(outcome->compiledRowUsed);
                    EXPECT_EQ(outcome->backend.backend, an::ComputeBackend::Reference);
                }
                else
                {
                    compiledRuns += outcome->compiledRowUsed ? 1u : 0u;
                }
                tiles.emplace_back(tile.written().begin(), tile.written().end());
            }
        }
        for (std::size_t index = 1u; index < tiles.size(); ++index)
        {
            EXPECT_TRUE(sameBits(tiles[0], tiles[index])) << an::sampleTypeName(type) << " run " << index;
        }
    }
    const an::BackendSelection selection = *an::selectBackend(makeExecution(narrow));
    if (selection.backend != an::ComputeBackend::Reference)
    {
        EXPECT_EQ(compiledRuns, kEveryType.size() * 2u)
            << "compiled rows were expected on " << an::computeBackendName(selection.backend);
    }
}

TEST(StackSourceTest, RefusesABandThatDoesNotDescribeTheModel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    OwnedFrame frame;
    frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u);
    frame.fill(141u);
    OwnedTile tile;
    tile.create(model, kHeight);

    const auto refuse = [&](an::StackSourceBand band, an::StackTileBand target, an::ErrorCode code,
                            std::string_view parameter) {
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, band, {}, {}, target);
        ASSERT_FALSE(outcome.hasValue()) << parameter;
        EXPECT_EQ(outcome.status().code(), code) << parameter << ": " << outcome.status().message();
        EXPECT_EQ(textField(outcome.status(), an::ContextKey::Parameter), std::string(parameter));
    };

    an::StackSourceBand nullSamples = frame.band();
    nullSamples.samples = nullptr;
    refuse(nullSamples, tile.band(), an::ErrorCode::InvalidArgument, "source.samples");
    an::StackSourceBand wrongType = frame.band();
    wrongType.sampleType = an::SampleType::Float32;
    refuse(wrongType, tile.band(), an::ErrorCode::IncompatibleArguments, "source.sampleType");
    an::StackSourceBand farFrame = frame.band();
    farFrame.frameIndex = kFrames;
    refuse(farFrame, tile.band(), an::ErrorCode::OutOfRange, "source.frameIndex");
    an::StackSourceBand lowRows = frame.band();
    lowRows.firstRow = 1u;
    refuse(lowRows, tile.band(), an::ErrorCode::OutOfRange, "source.firstRow");
    an::StackSourceBand maskUnderValue = frame.band();
    OwnedFrame masked;
    masked.create(an::SampleType::UInt16, kWidth, kHeight, 1u);
    masked.addMask(1u);
    maskUnderValue.validity.mask = &*masked.mask;
    refuse(maskUnderValue, tile.band(), an::ErrorCode::InvalidArgument, "source.validity.mask");
    an::StackSourceBand noMask = frame.band();
    noMask.validity.descriptor = ruleOf(an::ValidityRule::ExplicitMask);
    refuse(noMask, tile.band(), an::ErrorCode::InvalidArgument, "source.validity.mask");

    const an::ImageLayout narrow = makeLayout(an::SampleType::UInt16, kWidth - 1u, kHeight, 1u, false);
    an::StackSourceBand narrowBand = frame.band();
    narrowBand.layout = &narrow;
    refuse(narrowBand, tile.band(), an::ErrorCode::IncompatibleArguments, "source.layout");

    an::StackTileBand wrongTileType = tile.band();
    wrongTileType.sampleType = an::SampleType::Float64;
    refuse(frame.band(), wrongTileType, an::ErrorCode::IncompatibleArguments, "tile.sampleType");
    OwnedTile shortTile;
    shortTile.create(model, kHeight - 1u);
    refuse(frame.band(), shortTile.band(), an::ErrorCode::IncompatibleArguments, "tile.layout");
    an::StackTileBand nullTile = tile.band();
    nullTile.samples = nullptr;
    refuse(frame.band(), nullTile, an::ErrorCode::InvalidArgument, "tile.samples");

    // A tile that partly overlaps the source, and a stored band offered in place at another width.
    {
        OwnedTile overlapping;
        overlapping.create(model, kHeight);
        an::StackTileBand overlap = tile.band();
        overlap.samples = frame.bytes() + 16u;
        const an::Result<an::StackSourceOutcome> outcome =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, overlap);
        ASSERT_FALSE(outcome.hasValue());
        EXPECT_EQ(outcome.status().code(), an::ErrorCode::Aliasing);
        an::StackTileBand inPlace = tile.band();
        inPlace.samples = frame.bytes();
        const an::Result<an::StackSourceOutcome> widened =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, inPlace);
        ASSERT_FALSE(widened.hasValue());
        EXPECT_EQ(widened.status().code(), an::ErrorCode::Aliasing);
    }

    // A table of the wrong size, a table offered where no match applies, and scratch that is short.
    {
        const an::StackModel global =
            makeModel(scopedRequest(an::StackNormalizationScope::Global, makeDescriptor(an::SampleType::UInt16)));
        const std::array<an::FrameNormalization, kFrames> matches{makeMatch(global, 0u), makeMatch(global, 1u),
                                                                  makeMatch(global, 2u)};
        std::vector<double> table = makeTable(global, matches);
        table.pop_back();
        OwnedTile target;
        target.create(global, kHeight);
        const an::Result<an::StackSourceOutcome> shortTable =
            an::convertStackSourceBand(execution, global, an::StackSourceContract{.normalization = matches},
                                       frame.band(), table, {}, target.band());
        ASSERT_FALSE(shortTable.hasValue());
        EXPECT_EQ(shortTable.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(shortTable.status(), an::ContextKey::Parameter), "frameNormalization");
        table.push_back(1.0);
        table[1] = 0.0;
        const an::Result<an::StackSourceOutcome> zeroScale =
            an::convertStackSourceBand(execution, global, an::StackSourceContract{.normalization = matches},
                                       frame.band(), table, {}, target.band());
        ASSERT_FALSE(zeroScale.hasValue());
        EXPECT_EQ(zeroScale.status().code(), an::ErrorCode::InvalidArgument);
        const an::Result<an::StackSourceOutcome> spurious = an::convertStackSourceBand(
            execution, model, an::StackSourceContract{}, frame.band(), table, {}, tile.band());
        ASSERT_FALSE(spurious.hasValue());
        EXPECT_EQ(textField(spurious.status(), an::ContextKey::Parameter), "frameNormalization");

        const an::StackModel local =
            makeModel(scopedRequest(an::StackNormalizationScope::Local, makeDescriptor(an::SampleType::UInt16)));
        const an::LocalNormalizationDocument document = makeDocument(local, false);
        OwnedTile localTile;
        localTile.create(local, kHeight);
        an::StackSourceBand localBand = frame.band();
        localBand.frameIndex = 1u;
        const an::Result<an::StackSourceOutcome> noScratch =
            an::convertStackSourceBand(execution, local, an::StackSourceContract{.localNormalization = &document},
                                       localBand, {}, {}, localTile.band());
        ASSERT_FALSE(noScratch.hasValue());
        EXPECT_EQ(noScratch.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_EQ(textField(noScratch.status(), an::ContextKey::Parameter), "workerScratch");
        OwnedScratch scratch;
        scratch.create(local, 2u);
        std::array<std::span<std::byte>, 2u> shortened{scratch.slices[0].first(64u), scratch.slices[1]};
        const an::Result<an::StackSourceOutcome> shortScratch =
            an::convertStackSourceBand(execution, local, an::StackSourceContract{.localNormalization = &document},
                                       localBand, {}, shortened, localTile.band());
        ASSERT_FALSE(shortScratch.hasValue());
        EXPECT_EQ(textField(shortScratch.status(), an::ContextKey::Parameter), "workerScratch");
        const an::Result<an::StackSourceOutcome> noDocument = an::convertStackSourceBand(
            execution, local, an::StackSourceContract{}, localBand, {}, scratch.spans(), localTile.band());
        ASSERT_FALSE(noDocument.hasValue());
        EXPECT_EQ(noDocument.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(textField(noDocument.status(), an::ContextKey::Parameter), "contract.localNormalization");
    }

    const an::Result<an::StackSourceOutcome> empty = an::convertStackSourceBand(
        an::ExecutionContext{}, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::NotInitialized);
    const an::Result<an::ImageLayout> tooTall = an::stackTileBandLayout(model, kHeight + 1u);
    ASSERT_FALSE(tooTall.hasValue());
    EXPECT_EQ(tooTall.status().code(), an::ErrorCode::OutOfRange);
}

TEST(StackSourceTest, StoresAndReadsTheFrameNormalizationTable)
{
    const an::StackModel model = makeModel(scopedRequest(
        an::StackNormalizationScope::Global, makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb)));
    const std::array<an::FrameNormalization, kFrames> matches{makeMatch(model, 0u), makeMatch(model, 1u),
                                                              makeMatch(model, 2u)};
    const std::vector<double> table = makeTable(model, matches);
    ASSERT_EQ(table.size(), kFrames * 3u * 2u);
    // Frame-major, per channel the offset first and the scale second.
    EXPECT_EQ(table[(1u * 3u + 2u) * 2u], matches[1].channels[2].offset);
    EXPECT_EQ(table[(1u * 3u + 2u) * 2u + 1u], matches[1].channels[2].scale);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const an::Result<an::FrameNormalization> read = an::stackFrameNormalizationAt(model, frame, table);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_EQ(read->method, matches[frame].method);
        EXPECT_EQ(read->estimator, matches[frame].estimator);
        EXPECT_EQ(read->channelCount, 3u);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_EQ(read->channels[channel].scale, matches[frame].channels[channel].scale);
            EXPECT_EQ(read->channels[channel].offset, matches[frame].channels[channel].offset);
        }
    }

    std::vector<double> scratch = table;
    an::FrameNormalization wrong = matches[1];
    wrong.method = an::NormalizationMethod::Multiplicative;
    EXPECT_EQ(an::storeStackFrameNormalization(model, 1u, wrong, scratch).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::storeStackFrameNormalization(model, kFrames, matches[0], scratch).code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::storeStackFrameNormalization(model, 1u, matches[1], std::span<double>(scratch).first(4u)).code(),
              an::ErrorCode::IncompatibleArguments);
    const an::StackModel plain = makeModel(baseRequest());
    EXPECT_EQ(an::storeStackFrameNormalization(plain, 0u, matches[0], scratch).code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::stackFrameNormalizationAt(plain, 0u, table).status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::stackFrameNormalizationAt(model, kFrames, table).status().code(), an::ErrorCode::OutOfRange);
}

TEST(StackSourceTest, TakesNothingFromACancelledExecution)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Automatic, stop.get_token());
    const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    OwnedFrame frame;
    frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u);
    frame.fill(151u);
    OwnedTile tile;
    tile.create(model, kHeight);
    const an::Result<an::StackSourceOutcome> outcome =
        an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
    ASSERT_FALSE(outcome.hasValue());
    EXPECT_EQ(outcome.status().code(), an::ErrorCode::Cancelled);
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        EXPECT_EQ(tile.at<float>(0u, x, 0u), 0.0f);
    }
}

TEST(StackSourceTest, FeedsTheAccumulatorsThroughTheWorkingSetsConversionBand)
{
    // An average of three UInt16 frames: the plan holds one frame's widened band as its tile, each
    // frame is read into it and accumulated from there, and the sums are the widened samples' sums.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16)));
    const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->strategy.strategy, an::StackAccumulationStrategy::WholeOutputAccumulators);
    EXPECT_EQ(plan->memory.tileBytesPerRow, static_cast<std::uint64_t>(kWidth) * sizeof(float));
    EXPECT_EQ(plan->memory.tileBytes, an::stackTileFrameBytes(model, kHeight));
    EXPECT_EQ(plan->memory.scratchBytes, 0u);

    an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, model, *plan);
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::StackWorkingSet set = std::move(*made);
    ASSERT_TRUE(set.beginPass(execution, an::StackPassKind::Combine).ok());
    const std::span<std::byte> tileBytes = set.tile();
    ASSERT_EQ(tileBytes.size(), plan->memory.tileBytes);
    const an::Result<an::ImageLayout> tileLayout = an::stackTileBandLayout(model, kHeight);
    ASSERT_TRUE(tileLayout.hasValue());
    const an::StackAccumulatorSet terms{.sums = set.accumulatorSums(), .counts = set.accumulatorCounts()};

    std::vector<double> expected(static_cast<std::size_t>(kWidth) * kHeight, 0.0);
    for (std::uint32_t frameIndex = 0u; frameIndex < kFrames; ++frameIndex)
    {
        OwnedFrame frame;
        frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u);
        frame.fill(161u + frameIndex);
        frame.frameIndex = frameIndex;
        const an::StackTileBand tile{
            .sampleType = an::SampleType::Float32, .samples = tileBytes.data(), .layout = &*tileLayout};
        const an::Result<an::StackSourceOutcome> converted =
            an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile);
        ASSERT_TRUE(converted.hasValue()) << converted.status().message();
        const an::StackFrameBand band{.sampleType = an::SampleType::Float32,
                                      .samples = tileBytes.data(),
                                      .layout = &*tileLayout,
                                      .frameIndex = frameIndex,
                                      .firstOutputRow = 0u,
                                      .validity = an::StackBandValidity{.descriptor = converted->validity}};
        const an::Result<an::StackAccumulationOutcome> accumulated =
            an::accumulateStackFrame(execution, model, terms, band, {});
        ASSERT_TRUE(accumulated.hasValue()) << accumulated.status().message();
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                expected[static_cast<std::size_t>(y) * kWidth + x] +=
                    static_cast<double>(static_cast<float>(frame.at<std::uint16_t>(0u, x, y) / 65535.0));
            }
        }
    }
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_EQ(terms.sums[index], expected[index]) << index;
        EXPECT_EQ(terms.counts[index], kFrames) << index;
    }
    set.endPass();
}

TEST(StackSourceTest, PlansTheConversionBandTheMeasurementFrameAndTheRowScratch)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // A streaming stack over frames stored at the tile width holds no conversion band.
    const an::StackModel plain = makeModel(baseRequest());
    const an::Result<an::StackPlan> plainPlan = an::planStack(execution, plain, an::StackPlanningRequest{});
    ASSERT_TRUE(plainPlan.hasValue());
    EXPECT_EQ(plainPlan->memory.tileBytesPerRow, 0u);
    EXPECT_EQ(plainPlan->memory.tileBytes, 0u);

    // The same stack over UInt16 frames holds one frame's band at the tile width, and its working
    // set takes exactly that.
    const an::StackModel wide = makeModel(baseRequest(makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Rgb)));
    const an::Result<an::StackPlan> widePlan = an::planStack(execution, wide, an::StackPlanningRequest{});
    ASSERT_TRUE(widePlan.hasValue());
    EXPECT_EQ(widePlan->memory.tileBytesPerRow, static_cast<std::uint64_t>(kWidth) * 3u * sizeof(float));
    EXPECT_EQ(widePlan->memory.tileBytes, an::stackTileFrameBytes(wide, kHeight));
    EXPECT_EQ(widePlan->memory.peakBytes,
              widePlan->memory.persistentBytes + widePlan->memory.readBufferBytes + widePlan->memory.tileBytes);
    {
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, wide, *widePlan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::Combine).ok());
        EXPECT_EQ(made->tile().size(), widePlan->memory.tileBytes);
        EXPECT_EQ(made->account().heldBytes, widePlan->memory.peakBytes);
        made->endPass();
    }

    // A budget bounds the conversion band like any tile.
    {
        const std::uint64_t fixed = widePlan->memory.persistentBytes + widePlan->memory.readBufferBytes;
        const an::EngineContext bounded =
            makeEngine(2u, fixed + 3u * widePlan->memory.tileBytesPerRow + widePlan->memory.tileBytesPerRow / 2u);
        const an::ExecutionContext boundedExecution = makeExecution(bounded);
        const an::Result<an::StackPlan> banded = an::planStack(boundedExecution, wide, an::StackPlanningRequest{});
        ASSERT_TRUE(banded.hasValue()) << banded.status().message();
        EXPECT_EQ(banded->tile.rows, 3u);
        EXPECT_EQ(banded->tile.reason, an::StackTileReason::BudgetBound);
        EXPECT_EQ(banded->memory.tileBytes, an::stackTileFrameBytes(wide, 3u));
    }

    // The measurement passes hold one widened frame beside the stored one, and a local scope also
    // holds the reference at the tile width.
    {
        an::StackRequest request =
            scopedRequest(an::StackNormalizationScope::Global, makeDescriptor(an::SampleType::UInt16));
        request.localWeighting.enabled = true;
        const an::StackModel model = makeModel(request);
        const an::Result<an::StackPlan> plan = an::planStack(execution, model, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const std::uint64_t widened = an::stackTileFrameBytes(model, kHeight);
        const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
        ASSERT_TRUE(shape.has_value());
        EXPECT_EQ(plan->passes.passes[0].kind, an::StackPassKind::MeasureNormalization);
        EXPECT_EQ(plan->passes.passes[0].workingSetBytes, plan->memory.persistentBytes + plan->io.frameBytes + widened +
                                                              plan->memory.normalizationScratchBytes);
        EXPECT_EQ(plan->passes.passes[1].kind, an::StackPassKind::MeasureLocalWeights);
        EXPECT_EQ(plan->passes.passes[1].workingSetBytes,
                  plan->memory.persistentBytes + plan->io.frameBytes + widened +
                      an::localWeightMeasurementWorkspaceBytes(*shape, kFrames));
        EXPECT_EQ(plan->memory.measurementWorkspaceBytes,
                  widened + an::localWeightMeasurementWorkspaceBytes(*shape, kFrames));

        const an::StackModel floating = makeModel(scopedRequest(an::StackNormalizationScope::Global));
        const an::Result<an::StackPlan> floatingPlan = an::planStack(execution, floating, an::StackPlanningRequest{});
        ASSERT_TRUE(floatingPlan.hasValue());
        EXPECT_EQ(floatingPlan->passes.passes[0].workingSetBytes, floatingPlan->memory.persistentBytes +
                                                                      floatingPlan->io.frameBytes +
                                                                      floatingPlan->memory.normalizationScratchBytes);
        EXPECT_EQ(floatingPlan->memory.measurementWorkspaceBytes, 0u);

        const an::StackModel local =
            makeModel(scopedRequest(an::StackNormalizationScope::Local, makeDescriptor(an::SampleType::UInt16)));
        const an::Result<an::StackPlan> localPlan = an::planStack(execution, local, an::StackPlanningRequest{});
        ASSERT_TRUE(localPlan.hasValue()) << localPlan.status().message();
        EXPECT_EQ(localPlan->passes.passes[0].workingSetBytes,
                  localPlan->memory.persistentBytes + localPlan->io.frameBytes + 2u * widened);
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, local, *localPlan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::MeasureNormalization).ok());
        EXPECT_EQ(made->measurementWorkspace().size(), 2u * widened);
        made->endPass();
    }

    // The row scratch of the local surface is part of every worker's slice, under either strategy.
    {
        const an::StackModel local = makeModel(scopedRequest(an::StackNormalizationScope::Local));
        const std::uint64_t source = an::stackSourceScratchBytesPerWorker(local);
        EXPECT_GT(source, 0u);
        EXPECT_EQ(source % an::kStackScratchAlignmentBytes, 0u);
        EXPECT_GE(source, an::kStackSourceRowArrays * (an::kStackSourceGridCeiling + 1u) * sizeof(double) +
                              kWidth * sizeof(double) + (an::kStackSourceGridCeiling + 2u) * sizeof(std::uint32_t));
        EXPECT_EQ(an::stackScratchBytesPerWorker(local), source);
        an::StackRequest clipped = scopedRequest(an::StackNormalizationScope::Local);
        clipped.rejection.method = an::StackRejection::SigmaClipping;
        const an::StackModel history = makeModel(clipped);
        an::StackRequest unscoped = baseRequest();
        unscoped.rejection.method = an::StackRejection::SigmaClipping;
        EXPECT_EQ(an::stackScratchBytesPerWorker(history),
                  an::stackScratchBytesPerWorker(makeModel(unscoped)) + source);
        const an::Result<an::StackPlan> plan = an::planStack(execution, local, an::StackPlanningRequest{});
        ASSERT_TRUE(plan.hasValue());
        EXPECT_EQ(plan->memory.scratchBytesPerWorker, source);
        EXPECT_EQ(plan->memory.scratchBytes, source * plan->memory.workers);
        an::Result<an::StackWorkingSet> made = an::StackWorkingSet::create(engine, execution, local, *plan);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::MeasureNormalization).ok());
        made->endPass();
        ASSERT_TRUE(made->beginPass(execution, an::StackPassKind::Combine).ok());
        EXPECT_EQ(made->workerScratch(0u).size(), source);
        made->endPass();
    }
}

TEST(StackSourceTest, PreparesACalibrationPlanFromADescription)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor description = makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb);
    OwnedFrame light;
    light.create(an::SampleType::Float32, kWidth, kHeight, 3u);
    light.fill(171u);
    OwnedFrame dark;
    dark.create(an::SampleType::Float32, kWidth, kHeight, 3u);
    dark.fill(172u);
    OwnedFrame flat;
    flat.create(an::SampleType::Float32, kWidth, kHeight, 3u);
    flat.fill(173u);
    const an::CalibrationMasters masters{.dark = dark.calibrationView(description),
                                         .flat = flat.calibrationView(description)};
    const an::CalibrationRequest request{
        .pedestal = 0.002, .darkScale = 0.75, .flatNormalization = an::FlatNormalizationMode::CentralMean};

    const an::Result<an::CalibrationPlan> described =
        an::prepareImageCalibration(execution, description, masters, request);
    ASSERT_TRUE(described.hasValue()) << described.status().message();
    const an::Result<an::CalibrationPlan> resident =
        an::prepareImageCalibration(execution, light.calibrationView(description), masters, request);
    ASSERT_TRUE(resident.hasValue()) << resident.status().message();
    EXPECT_EQ(described->description(), description);
    EXPECT_EQ(described->pedestal(), 0.002);
    EXPECT_EQ(described->effectiveDarkScale(), resident->effectiveDarkScale());
    EXPECT_EQ(described->flatNormalization(), an::FlatNormalizationMode::CentralMean);
    EXPECT_EQ(described->flatChannelCount(), 3u);
    EXPECT_EQ(described->flatNormalizationGroupCount(), 3u);
    ASSERT_EQ(described->flatNormalizationFactors().size(), 3u);
    for (std::uint32_t group = 0u; group < 3u; ++group)
    {
        EXPECT_EQ(described->flatNormalizationFactors()[group], resident->flatNormalizationFactor(group));
    }
    EXPECT_FALSE(an::CalibrationPlan{}.description().has_value());

    const an::CalibrationRequest optimized{.darkScaling = an::DarkScalingMode::OptimizeCentralRegion};
    const an::Result<an::CalibrationPlan> refused =
        an::prepareImageCalibration(execution, description, masters, optimized);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(messageNames(refused.status(), "optimized dark scaling reads the light"));
    const an::Result<an::CalibrationPlan> integer = an::prepareImageCalibration(
        execution, makeDescriptor(an::SampleType::UInt16, an::ChannelSchema::Rgb), masters, request);
    ASSERT_FALSE(integer.hasValue());
    EXPECT_EQ(integer.status().code(), an::ErrorCode::Unsupported);
}

TEST(StackSourceTest, MeasuresLocalWeightsOnTheWidenedFrame)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = baseRequest(makeDescriptor(an::SampleType::UInt16));
    request.localWeighting.enabled = true;
    request.localWeighting.gridSpacing = 4u;
    const an::StackModel model = makeModel(request);
    const std::optional<an::LocalWeightGridShape> shape = an::LocalWeightGridShape::of(model);
    ASSERT_TRUE(shape.has_value());

    OwnedFrame frame;
    frame.create(an::SampleType::UInt16, kWidth, kHeight, 1u);
    frame.fill(181u);
    OwnedTile widened;
    widened.create(model, kHeight);
    ASSERT_TRUE(
        an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, widened.band())
            .hasValue());

    std::vector<double> workspace(
        static_cast<std::size_t>(an::localWeightMeasurementWorkspaceBytes(*shape, kFrames) / sizeof(double)) + 1u, 0.0);
    const std::span<std::byte> bytes(reinterpret_cast<std::byte*>(workspace.data()),
                                     an::localWeightMeasurementWorkspaceBytes(*shape, kFrames));
    std::vector<float> stored(static_cast<std::size_t>(shape->valuesPerFrame()), 0.0f);
    std::vector<float> fromTile(stored.size(), 0.0f);
    const an::LocalWeightFrameSource asStored{.sampleType = an::SampleType::UInt16,
                                              .samples = frame.bytes(),
                                              .layout = &*frame.layout,
                                              .validity = an::StackBandValidity{.descriptor = frame.descriptor}};
    ASSERT_TRUE(an::measureLocalWeightGrid(execution, model, asStored, bytes, stored).hasValue());
    const an::LocalWeightFrameSource asWidened{.sampleType = an::SampleType::Float32,
                                               .samples = widened.bytes(),
                                               .layout = &*widened.layout,
                                               .validity = an::StackBandValidity{.descriptor = frame.descriptor}};
    const an::Result<an::LocalWeightMeasurement> measured =
        an::measureLocalWeightGrid(execution, model, asWidened, bytes, fromTile);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_EQ(measured->nodeCount, shape->valuesPerFrame() / an::kLocalWeightNodeValues);
    // The widened frame is the stored one divided by full scale, so every level is that fraction.
    for (std::size_t index = 0u; index < stored.size(); index += an::kLocalWeightNodeValues)
    {
        if (std::isnan(stored[index]))
        {
            EXPECT_TRUE(std::isnan(fromTile[index]));
            continue;
        }
        EXPECT_NEAR(fromTile[index], stored[index] / 65535.0f, 1.0e-6f) << index;
    }
    an::LocalWeightFrameSource other = asWidened;
    other.sampleType = an::SampleType::Float64;
    EXPECT_EQ(an::measureLocalWeightGrid(execution, model, other, bytes, fromTile).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(StackSourceTest, StartsAndFitsALocalNormalizationDocumentFromTheFrames)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor =
        makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Mono, an::NumericDomain::NormalizedLinear, 96u, 64u);
    an::StackRequest request = scopedRequest(an::StackNormalizationScope::Local, descriptor);
    request.normalization.referenceFrame = 1u;
    const an::StackModel model = makeModel(request);

    // Three frames: a reference, and two that differ from it by a gradient and a factor.
    std::array<OwnedFrame, kFrames> frames;
    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        frames[index].create(an::SampleType::Float32, 96u, 64u, 1u);
        frames[index].frameIndex = index;
        for (std::uint32_t y = 0u; y < 64u; ++y)
        {
            for (std::uint32_t x = 0u; x < 96u; ++x)
            {
                const double noise = 0.02 * (patterned(x + y * 96u, 191u + index) - 0.475);
                const double sky = 0.20 + 0.03 * index + 0.05 * index * static_cast<double>(x) / 95.0;
                frames[index].at<float>(0u, x, y) = static_cast<float>(sky + (1.0 + 0.2 * index) * noise);
            }
        }
    }
    an::LocalNormalizationRequest fit;
    fit.gridSize = 2u;
    fit.backgroundMinRoiExtent = 16u;
    fit.scaleSamples = 512u;

    an::LocalNormalizationDocument document;
    const an::Result<an::StackLocalNormalizationFit> early =
        an::fitStackLocalNormalizationFrame(execution, model, frames[0].normalizationView(descriptor), 0u,
                                            frames[1].normalizationView(descriptor), false, document);
    ASSERT_FALSE(early.hasValue());
    EXPECT_EQ(early.status().code(), an::ErrorCode::InvalidState);

    const an::Result<an::LocalBackgroundFitResult> started =
        an::beginStackLocalNormalization(execution, model, fit, frames[1].normalizationView(descriptor), document);
    ASSERT_TRUE(started.hasValue()) << started.status().message();
    EXPECT_EQ(started->channelCount, 1u);
    EXPECT_EQ(document.referenceIndex, 1u);
    EXPECT_EQ(document.frames.size(), kFrames);
    EXPECT_EQ(document.shape.gridSize, 2u);
    EXPECT_EQ(document.validity, an::ValidityRule::AllValid);
    EXPECT_EQ(document.frames[1].background, document.referenceBackground);
    EXPECT_EQ(document.frames[1].scale[0], 1.0);
    EXPECT_TRUE(document.frames[0].background.empty());

    for (std::uint32_t index = 0u; index < kFrames; ++index)
    {
        const an::Result<an::StackLocalNormalizationFit> fitted =
            an::fitStackLocalNormalizationFrame(execution, model, frames[index].normalizationView(descriptor), index,
                                                frames[1].normalizationView(descriptor), index == 2u, document);
        ASSERT_TRUE(fitted.hasValue()) << index << ": " << fitted.status().message();
        EXPECT_EQ(fitted->isReference, index == 1u);
        if (index == 1u)
        {
            continue;
        }
        ASSERT_TRUE(fitted->scale.has_value());
        EXPECT_EQ(fitted->scaleGrid.has_value(), index == 2u);
        EXPECT_EQ(document.frames[index].background.size(), document.shape.valueCount());
        EXPECT_EQ(document.frames[index].scaleGrid.size(), index == 2u ? document.shape.valueCount() : 0u);
        EXPECT_GT(document.frames[index].scale[0], 0.0);
        EXPECT_FALSE(document.frames[index].global.has_value());
    }
    // The fitted background follows the frame's sky, and the scale brings its noise toward the reference's.
    EXPECT_GT(document.frames[2].background[an::localNormalizationNodeIndex(document.shape, 0u, 2u, 1u)],
              document.frames[2].background[an::localNormalizationNodeIndex(document.shape, 0u, 0u, 1u)]);
    EXPECT_LT(document.frames[2].scale[0], 1.0);

    // The started document is what the source path applies.
    const an::StackSourceContract contract{.localNormalization = &document};
    ASSERT_TRUE(an::checkStackSourceContract(model, contract).hasValue())
        << an::checkStackSourceContract(model, contract).status().message();
    OwnedScratch scratch;
    scratch.create(model, 4u);
    OwnedTile tile;
    tile.create(model, 64u);
    const an::Result<an::StackSourceOutcome> outcome =
        an::convertStackSourceBand(execution, model, contract, frames[2].band(), {}, scratch.spans(), tile.band());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    EXPECT_TRUE(outcome->stages.normalizeLocally);
    OwnedTile reference;
    reference.create(model, 64u);
    const an::Result<an::StackSourceOutcome> untouched =
        an::convertStackSourceBand(execution, model, contract, frames[1].band(), {}, scratch.spans(), reference.band());
    ASSERT_TRUE(untouched.hasValue()) << untouched.status().message();
    EXPECT_TRUE(sameBits(
        reference.written(),
        std::span<const std::byte>(frames[1].bytes(), static_cast<std::size_t>(frames[1].layout->totalBytes()))));

    const an::StackModel plain = makeModel(baseRequest(descriptor));
    an::LocalNormalizationDocument other;
    EXPECT_EQ(an::beginStackLocalNormalization(execution, plain, fit, frames[1].normalizationView(descriptor), other)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(StackSourceTest, ReportsTheBackendAndTheStagesItRan)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(baseRequest(makeDescriptor(an::SampleType::Int32)));
    OwnedFrame frame;
    frame.create(an::SampleType::Int32, kWidth, kHeight, 1u);
    frame.fill(201u);
    OwnedTile tile;
    tile.create(model, kHeight);
    const an::Result<an::StackSourceOutcome> outcome =
        an::convertStackSourceBand(execution, model, an::StackSourceContract{}, frame.band(), {}, {}, tile.band());
    ASSERT_TRUE(outcome.hasValue()) << outcome.status().message();
    const an::BackendSelection selection = *an::selectBackend(execution);
    EXPECT_EQ(outcome->backend.backend, selection.backend);
    EXPECT_EQ(outcome->backend.instructionSet, selection.instructionSet);
    EXPECT_EQ(outcome->compiledRowUsed, selection.backend != an::ComputeBackend::Reference);
    EXPECT_EQ(outcome->stages, (an::StackSourceStages{.convert = true,
                                                      .calibrate = false,
                                                      .normalizeLocally = false,
                                                      .normalizeGlobally = false,
                                                      .normalizationPrepared = true}));
    std::cout << "stack-source | " << an::sampleTypeName(an::SampleType::Int32) << " on "
              << an::computeBackendName(outcome->backend.backend) << '/'
              << an::instructionSetName(outcome->backend.instructionSet)
              << (outcome->compiledRowUsed ? " compiled" : " scalar") << std::endl;
}

} // namespace
