// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "../Test_EngineStars/StarFieldFixture.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{
class StarSource final : public an::FrameSourceBackend
{
public:
    StarSource(const an::RawFrameDescriptor& description, const an::ImageLayout& layout, std::span<const float> samples)
        : m_description(description), m_layout(layout), m_samples(samples)
    {
    }
    an::Result<an::SequenceReadResult> read(const an::EngineContext& engine, const an::ExecutionContext&,
                                            std::uint64_t index) override
    {
        auto buffer = an::SampleBuffer::createOwned(engine, m_layout, an::StorageInitialization::Uninitialized);
        if (!buffer)
            return buffer.status();
        std::memcpy(buffer->mutableBytes()->data(), m_samples.data(), m_samples.size_bytes());
        auto raw = an::RawFrame::create(m_description, m_layout, std::move(*buffer));
        if (!raw)
            return raw.status();
        return an::SequenceReadResult{an::SequenceFrame{std::move(*raw), {}, {index}}};
    }
    an::Status close() override
    {
        return an::Status::success();
    }

private:
    an::RawFrameDescriptor m_description;
    an::ImageLayout m_layout;
    std::span<const float> m_samples;
};

class CalibrateDetectAlign final : public an::FrameStreamProcessor
{
public:
    explicit CalibrateDetectAlign(const an::CalibrationPlan& calibration, const an::CalibrationMasters& masters,
                                  std::span<const float> expected)
        : m_calibration(calibration), m_masters(masters), m_expected(expected)
    {
    }
    std::vector<std::size_t> starCounts;
    std::vector<an::ValidityMask> masks;
    an::Result<an::SequenceFrame> process(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                          an::SequenceFrame frame) override
    {
        const auto descriptor = *frame.frame.descriptor()->toImageDescriptor();
        const auto layout = *frame.frame.layout();
        auto light =
            an::CalibrationFrameView::create(frame.frame.bytes().data(), layout.totalBytes(), layout, descriptor);
        auto calibrated = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Uninitialized);
        if (!light || !calibrated)
            return light ? calibrated.status() : light.status();
        auto calibration = an::calibrateImage(execution, m_calibration, *light, m_masters,
                                              calibrated->mutableBytes()->data(), layout.totalBytes(), layout);
        if (!calibration)
            return calibration.status();
        auto transition = an::imageCalibrationTransition(m_calibration, frame.metadata.semantics());
        if (!transition)
            return transition.status();
        auto metadata = an::applyMetadataTransition(frame.metadata, *transition);
        if (!metadata)
            return metadata.status();
        auto stars = an::StarFrameView::create(calibrated->bytes().data(), layout.totalBytes(), layout, descriptor);
        auto detection = an::planStarDetection({}, descriptor, layout);
        if (!stars || !detection)
            return stars ? detection.status() : stars.status();
        an::StarDetectionResult detected;
        if (auto status = an::detectStars(execution, *stars, *detection, detected); status.failed())
            return status;
        starCounts.push_back(detected.stars.size());

        // Identity placement exercises the complete warp and mask path while giving an exact
        // independent pixel oracle. The explicit mask remains beside its returned frame metadata.
        auto plan = an::planRegistrationWarp(
            descriptor, {}, {}, {.width = descriptor.extent().width(), .height = descriptor.extent().height()});
        auto mask = an::ValidityMask::create(engine, descriptor.extent());
        auto output = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
        if (!plan || !mask || !output)
            return !plan ? plan.status() : !mask ? mask.status() : output.status();
        auto warped = an::warpRegisteredImage(execution, an::SampleType::Float32, calibrated->bytes().data(), layout,
                                              output->mutableBytes()->data(), layout, *plan, {}, *mask->view());
        if (!warped)
            return warped.status();
        if (std::memcmp(output->bytes().data(), m_expected.data(), m_expected.size_bytes()) != 0)
            return an::Status::failure(an::ErrorCode::InvalidData);
        auto warpTransition =
            an::operationTransition(an::kRegistrationWarpEffect, {.operation = "warpRegisteredImage"});
        if (!warpTransition)
            return warpTransition.status();
        metadata = an::applyMetadataTransition(*metadata, *warpTransition);
        if (!metadata)
            return metadata.status();
        auto raw = an::RawFrame::create(*frame.frame.descriptor(), layout, std::move(*output));
        if (!raw)
            return raw.status();
        frame.frame = std::move(*raw);
        frame.metadata = std::move(*metadata);
        masks.push_back(std::move(*mask));
        return frame;
    }

private:
    const an::CalibrationPlan& m_calibration;
    const an::CalibrationMasters& m_masters;
    std::span<const float> m_expected;
};

TEST(FrameStreamCompositionTest, BoundedFramesComposeCalibrationDetectionAndRegistration)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 3u});
    ASSERT_TRUE(engine);
    auto execution = an::ExecutionContext::create(*engine, {});
    ASSERT_TRUE(execution);
    an::test::StarFieldRequest fieldRequest;
    fieldRequest.width = fieldRequest.height = 256u;
    const auto field = an::test::makeStarField(fieldRequest);
    const auto extent = *an::ImageExtent::create(256u, 256u, 1u, engine->limits());
    const auto rawDescriptor = *an::RawFrameDescriptor::create({extent,
                                                                {4u, an::RawSampleClass::FloatingPoint},
                                                                an::ByteOrder::Native,
                                                                an::ChannelSchema::Mono,
                                                                an::ColorSpace::Unspecified,
                                                                an::NumericDomain::NormalizedLinear,
                                                                {},
                                                                an::CoordinateOrigin::TopLeft,
                                                                1.0});
    const auto descriptor = *rawDescriptor.toImageDescriptor();
    const auto layout = *an::ImageLayout::create({extent, 4u, 1u, 1u, 1u}, engine->limits());
    std::vector<float> bias(field.samples.size(), 0.02f);
    auto master = an::CalibrationFrameView::create(bias.data(), layout.totalBytes(), layout, descriptor);
    ASSERT_TRUE(master);
    const an::CalibrationMasters masters{.bias = *master};
    auto plan = an::prepareImageCalibration(*execution, descriptor, masters, {});
    ASSERT_TRUE(plan);
    std::vector<float> expected(field.samples.size());
    for (std::size_t i = 0u; i < expected.size(); ++i)
        expected[i] = static_cast<float>(static_cast<double>(field.samples[i]) - static_cast<double>(bias[i]));
    auto source = an::FrameSource::create(
        *engine, {.format = an::SequenceFormat::ImageList, .fixedFrame = rawDescriptor, .frameCount = 3u},
        std::make_unique<StarSource>(rawDescriptor, layout, field.samples));
    ASSERT_TRUE(source);
    CalibrateDetectAlign processor(*plan, masters, expected);
    an::FrameStreamCounters counters;
    auto status = an::streamFrames(*engine, *execution, *source, nullptr, &processor,
                                   {.readAheadDepth = 1u,
                                    .writeBehindDepth = 0u,
                                    .maxFrameBytes = layout.totalBytes(),
                                    .readAheadBytes = layout.totalBytes()},
                                   counters);
    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_EQ(counters.framesProcessed, 3u);
    ASSERT_EQ(processor.starCounts.size(), 3u);
    EXPECT_GT(processor.starCounts[0], 0u);
    EXPECT_EQ(processor.starCounts[0], processor.starCounts[1]);
    EXPECT_EQ(processor.starCounts[1], processor.starCounts[2]);
    EXPECT_EQ(processor.masks.size(), 3u);
    EXPECT_EQ(counters.readAhead.peakFrames, 1u);
}
} // namespace
