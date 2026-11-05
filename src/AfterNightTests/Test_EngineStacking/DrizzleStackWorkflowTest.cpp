// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackWorkflow.h>

#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/DumpHistory.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
[[nodiscard]] an::ImageDescriptor descriptor()
{
    const auto extent = *an::ImageExtent::create(5u, 3u, 1u, an::defaultEngineLimits());
    return *an::ImageDescriptor::create({.extent = extent,
                                         .sampleType = an::SampleType::Float32,
                                         .channelSchema = an::ChannelSchema::Mono,
                                         .colorSpace = an::ColorSpace::Unspecified,
                                         .numericDomain = an::NumericDomain::NormalizedLinear,
                                         .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                         .pixelAspectRatio = 1.0});
}

[[nodiscard]] an::StackModel model(an::StackCombination combination = an::StackCombination::Average,
                                   an::StackRejection rejection = an::StackRejection::None, bool variance = false)
{
    an::StackRequest request{.input = {5u, descriptor()}};
    request.combination = combination;
    request.rejection.method = rejection;
    request.output.varianceMap = variance;
    return *an::StackModel::create(request);
}

[[nodiscard]] std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("an-drizzle-history-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        EXPECT_TRUE(std::filesystem::create_directory(path));
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        EXPECT_FALSE(error) << error.message();
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

class DumpHistoryStackSource final : public an::StackFrameSource
{
public:
    DumpHistoryStackSource(const an::DumpHistory& history, const an::ImageDescriptor& descriptor)
        : m_history(&history), m_descriptor(descriptor)
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::Region;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override
    {
        static_cast<void>(execution);
        const auto extent = *an::ImageExtent::create(m_descriptor.extent().width(), rows,
                                                     m_descriptor.extent().channelCount(), an::defaultEngineLimits());
        m_layout = *an::ImageLayout::create({.extent = extent,
                                             .elementSizeBytes = sizeof(float),
                                             .samplesPerPixel = 1u,
                                             .planeCount = extent.channelCount(),
                                             .rowAlignmentBytes = 1u},
                                            an::defaultEngineLimits());
        if (buffer.size() < m_layout->totalBytes())
            return an::Status::failure(an::ErrorCode::IncompatibleArguments);
        const std::uint64_t width = extent.width();
        const std::uint64_t fullHeight = m_descriptor.extent().height();
        for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < rows; ++y)
            {
                for (std::uint32_t x = 0u; x < extent.width(); ++x)
                {
                    const std::uint64_t sample =
                        (static_cast<std::uint64_t>(channel) * fullHeight + firstRow + y) * width + x;
                    const auto stored = m_history->sample(frameIndex, sample);
                    if (!stored)
                        return stored.status();
                    std::memcpy(buffer.data() + static_cast<std::size_t>(channel) * m_layout->planeStrideBytes() +
                                    static_cast<std::size_t>(y) * m_layout->rowStrideBytes() +
                                    static_cast<std::size_t>(x) * sizeof(float),
                                stored->data(), sizeof(float));
                }
            }
        }
        ++readCount;
        return an::StackSourceBand{.sampleType = an::SampleType::Float32,
                                   .samples = buffer.data(),
                                   .layout = &*m_layout,
                                   .frameIndex = frameIndex,
                                   .firstRow = firstRow};
    }

    std::uint64_t readCount = 0u;

private:
    const an::DumpHistory* m_history;
    an::ImageDescriptor m_descriptor;
    std::optional<an::ImageLayout> m_layout;
};
} // namespace

TEST(DrizzleStackWorkflowTest, AutomaticNamesNoHistoryDeferredReductionAndBoundedSampleHistory)
{
    const auto direct = an::planDrizzleStackWorkflow(model(), an::DrizzleStackExecutionPolicy::Automatic);
    ASSERT_TRUE(direct.hasValue());
    EXPECT_EQ(direct->path, an::DrizzleStackExecutionPath::DirectPerFrameSource);
    EXPECT_EQ(direct->historyReason, an::DrizzleStackHistoryReason::None);
    EXPECT_EQ(direct->accumulationStrategy, an::StackAccumulationStrategy::WholeOutputAccumulators);
    EXPECT_FALSE(direct->dumpBacked);

    for (const auto& reduced : {model(an::StackCombination::Minimum), model(an::StackCombination::Maximum),
                                model(an::StackCombination::Average, an::StackRejection::None, true)})
    {
        const auto planned = an::planDrizzleStackWorkflow(reduced, an::DrizzleStackExecutionPolicy::Automatic);
        ASSERT_TRUE(planned.hasValue());
        EXPECT_EQ(planned->historyReason, an::DrizzleStackHistoryReason::DeferredPerFrameReduction);
        EXPECT_EQ(planned->accumulationStrategy, an::StackAccumulationStrategy::WholeOutputAccumulators);
    }

    for (const auto& history :
         {model(an::StackCombination::Median), model(an::StackCombination::AutoAdaptiveWeightedAverage),
          model(an::StackCombination::Average, an::StackRejection::SigmaClipping)})
    {
        const auto planned = an::planDrizzleStackWorkflow(history, an::DrizzleStackExecutionPolicy::Automatic);
        ASSERT_TRUE(planned.hasValue());
        EXPECT_EQ(planned->historyReason, an::DrizzleStackHistoryReason::SampleHistory);
        EXPECT_EQ(planned->accumulationStrategy, an::StackAccumulationStrategy::TileSampleHistory);
        EXPECT_FALSE(planned->dumpBacked);
    }
}

TEST(DrizzleStackWorkflowTest, ReferencePolicyOverridesMethodAndUnknownPolicyIsRefused)
{
    const auto planned = an::planDrizzleStackWorkflow(model(an::StackCombination::Median),
                                                      an::DrizzleStackExecutionPolicy::DumpHistoryReference);
    ASSERT_TRUE(planned.hasValue());
    EXPECT_EQ(planned->path, an::DrizzleStackExecutionPath::DumpHistoryReference);
    EXPECT_EQ(planned->historyReason, an::DrizzleStackHistoryReason::ReferencePolicy);
    EXPECT_EQ(planned->accumulationStrategy, an::StackAccumulationStrategy::TileSampleHistory);
    EXPECT_TRUE(planned->dumpBacked);

    const auto invalid = an::planDrizzleStackWorkflow(
        model(), static_cast<an::DrizzleStackExecutionPolicy>((std::numeric_limits<std::uint8_t>::max)()));
    EXPECT_EQ(invalid.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_STREQ(invalid.status().operation(), "an::planDrizzleStackWorkflow");
}

TEST(DrizzleStackWorkflowTest, BothDumpLayoutsFeedEveryRejectionThroughTheCompleteStackExecutor)
{
    TemporaryDirectory directory;
    auto codec = an::createDumpCodec();
    ASSERT_TRUE(codec.hasValue());
    const std::array codecs{*codec};
    auto registry = an::CodecRegistry::create(codecs);
    ASSERT_TRUE(registry.hasValue());
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 2u;
    engineOptions.codecRegistry = *registry;
    auto madeEngine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(madeEngine.hasValue());
    an::EngineContext engine = std::move(*madeEngine);
    auto execution = an::ExecutionContext::create(engine, {});
    ASSERT_TRUE(execution.hasValue());

    const an::ImageDescriptor imageDescriptor = descriptor();
    const auto rawDescriptor = *an::RawFrameDescriptor::create({.extent = imageDescriptor.extent(),
                                                                .sampleFormat = {4u, an::RawSampleClass::FloatingPoint},
                                                                .byteOrder = an::nativeByteOrder(),
                                                                .channelSchema = an::ChannelSchema::Mono,
                                                                .colorSpace = an::ColorSpace::Unspecified,
                                                                .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                .pixelAspectRatio = 1.0});
    const auto layout = *an::ImageLayout::create({.extent = imageDescriptor.extent(),
                                                  .elementSizeBytes = sizeof(float),
                                                  .samplesPerPixel = 1u,
                                                  .planeCount = 1u,
                                                  .rowAlignmentBytes = 1u},
                                                 engine.limits());
    std::vector<std::vector<float>> samples(5u, std::vector<float>(imageDescriptor.extent().sampleCount()));
    std::vector<std::string> paths;
    paths.reserve(samples.size());
    for (std::size_t frameIndex = 0u; frameIndex < samples.size(); ++frameIndex)
    {
        for (std::size_t sample = 0u; sample < samples[frameIndex].size(); ++sample)
            samples[frameIndex][sample] =
                0.1f + 0.01f * static_cast<float>(frameIndex) + 0.001f * static_cast<float>(sample);
        if (frameIndex == 4u)
            samples[frameIndex][7u] = 0.95f;
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Uninitialized);
        ASSERT_TRUE(buffer.hasValue());
        std::memcpy(buffer->mutableBytes()->data(), samples[frameIndex].data(), layout.totalBytes());
        auto frame = an::RawFrame::create(rawDescriptor, layout, std::move(*buffer));
        ASSERT_TRUE(frame.hasValue());
        paths.push_back(utf8Path(directory.path / ("frame-" + std::to_string(frameIndex) + ".dump")));
        auto written = an::writeImage(engine, *execution,
                                      {.destinationPath = paths.back(), .format = an::CodecFormat::Dump}, *frame, {});
        ASSERT_TRUE(written.hasValue()) << written.status().message();
    }
    std::vector<std::string_view> pathViews(paths.begin(), paths.end());
    const an::CodecRegion region{0u, 0u, imageDescriptor.extent().width(), imageDescriptor.extent().height()};
    std::vector<an::StackSourceBand> residentBands;
    residentBands.reserve(samples.size());
    for (std::uint32_t frameIndex = 0u; frameIndex < samples.size(); ++frameIndex)
    {
        residentBands.push_back({.sampleType = an::SampleType::Float32,
                                 .samples = samples[frameIndex].data(),
                                 .layout = &layout,
                                 .frameIndex = frameIndex});
    }

    for (const auto historyLayout : {an::DumpHistoryLayout::FrameMajor, an::DumpHistoryLayout::BlockedPixelMajor})
    {
        an::DumpHistoryOptions historyOptions;
        historyOptions.layout = historyLayout;
        historyOptions.readers = an::DumpHistoryReaderPolicy::Reopen;
        historyOptions.blockSamples = 3u;
        historyOptions.workingSetBytes = 4096u;
        auto history = an::DumpHistory::read(engine, *execution, pathViews, rawDescriptor, region, historyOptions);
        ASSERT_TRUE(history.hasValue()) << history.status().message();
        for (std::uint8_t rejection = static_cast<std::uint8_t>(an::StackRejection::SigmaClipping);
             rejection <= static_cast<std::uint8_t>(an::StackRejection::GeneralizedEsdTest); ++rejection)
        {
            SCOPED_TRACE(static_cast<unsigned>(historyLayout));
            SCOPED_TRACE(static_cast<unsigned>(rejection));
            const an::StackModel stackModel =
                model(an::StackCombination::Average, static_cast<an::StackRejection>(rejection));
            auto resident = an::ResidentStackFrameSource::create(residentBands);
            ASSERT_TRUE(resident.hasValue());
            auto expected =
                an::executeStack(engine, *execution, stackModel, *resident, {}, {.maximumTileRows = 1u}, {});
            ASSERT_TRUE(expected.hasValue()) << expected.status().message();
            DumpHistoryStackSource source(*history, imageDescriptor);
            auto actual = an::executeStack(engine, *execution, stackModel, source, {}, {.maximumTileRows = 1u}, {});
            ASSERT_TRUE(actual.hasValue()) << actual.status().message();
            EXPECT_EQ(std::memcmp(expected->image.data(), actual->image.data(), expected->image.logicalBytes()), 0);
            EXPECT_GT(source.readCount, 0u);
        }
    }
}
