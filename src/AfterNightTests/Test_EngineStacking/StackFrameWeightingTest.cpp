// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the derived frame weights: every source of `StackWeighting` computed from a
// set of per-frame measurements into the working set's frame-major weight table, normalized to
// mean one per channel, with the rules written out as the oracle and the cases they leave silent
// refused by name or reported as a disposition.

#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kWidth = 8u;
constexpr std::uint32_t kHeight = 4u;
constexpr std::uint32_t kFrames = 4u;
constexpr const char* kOperation = "an::computeStackFrameWeights";

/// An engine with a stated concurrency.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 2u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// One operation in an engine, optionally listened to and stoppable.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, an::DiagnosticSink* sink = nullptr,
                                                 std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.diagnosticSink = sink;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// The bytes an engine has outstanding right now.
[[nodiscard]] std::uint64_t liveBytes(const an::EngineContext& engine)
{
    return engine.memoryResource()->counters().liveBytes;
}

/// A descriptor of the stated schema in the normalized linear domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::ChannelSchema schema = an::ChannelSchema::Mono)
{
    const std::uint32_t channels = an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(kWidth, kHeight, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{
        .extent = *extent,
        .sampleType = an::SampleType::Float32,
        .channelSchema = schema,
        .colorSpace = schema == an::ChannelSchema::Rgb ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified,
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = {},
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A weighted average of the stated source over the stated frames.
[[nodiscard]] an::StackRequest requestOf(an::StackWeighting source, std::uint32_t frames = kFrames,
                                         an::ChannelSchema schema = an::ChannelSchema::Mono)
{
    an::StackRequest request{.input =
                                 an::StackInputDescription{.frameCount = frames, .descriptor = makeDescriptor(schema)}};
    request.weighting.method = source;
    return request;
}

/// The checked model of a request every rule admits.
[[nodiscard]] an::StackModel makeModel(const an::StackRequest& request)
{
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    EXPECT_TRUE(model.has_value()) << an::stackModelErrorName(an::StackModel::check(request));
    return model.has_value() ? *model : *an::StackModel::create(requestOf(an::StackWeighting::Uniform));
}

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
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

/// A sink that keeps every event it hears.
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
        const std::lock_guard<std::mutex> held(m_guard);
        m_events.push_back(Event{.severity = event.severity, .operation = event.operation, .message = event.message});
    }

    [[nodiscard]] std::vector<Event> from(std::string_view operation, an::DiagnosticSeverity severity) const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        std::vector<Event> matching;
        for (const Event& event : m_events)
        {
            if (event.operation == operation && event.severity == severity)
            {
                matching.push_back(event);
            }
        }
        return matching;
    }

private:
    mutable std::mutex m_guard;
    std::vector<Event> m_events;
};

/// Divides a set of raw weights by their mean, which is what every derived source ends with.
[[nodiscard]] std::vector<double> meanOne(std::vector<double> raw)
{
    double sum = 0.0;
    for (const double value : raw)
    {
        sum += value;
    }
    const double mean = sum / static_cast<double>(raw.size());
    for (double& value : raw)
    {
        value /= mean;
    }
    return raw;
}

/// Runs the operation and returns its table, expecting success.
[[nodiscard]] std::vector<double> weightsOf(const an::ExecutionContext& execution, const an::StackModel& model,
                                            std::span<const an::StackFrameMeasurement> measurements,
                                            an::StackFrameWeightingResult* result = nullptr)
{
    const std::uint32_t channels = model.request().input.descriptor.extent().channelCount();
    std::vector<double> table(static_cast<std::size_t>(model.request().input.frameCount) * channels, -1.0);
    const an::Result<an::StackFrameWeightingResult> made =
        an::computeStackFrameWeights(execution, model, measurements, table);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (made.hasValue() && result != nullptr)
    {
        *result = *made;
    }
    return table;
}

TEST(StackFrameWeightingTest, UniformSourceWritesOnes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf(an::StackWeighting::Uniform));
    const std::vector<an::StackFrameMeasurement> measurements(kFrames);
    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    for (const double weight : table)
    {
        EXPECT_DOUBLE_EQ(weight, 1.0);
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Uniform);
    EXPECT_EQ(result.source, an::StackWeighting::Uniform);
    EXPECT_EQ(result.frameCount, kFrames);
    EXPECT_EQ(result.channelCount, 1u);
    EXPECT_FALSE(result.perChannel);
    EXPECT_DOUBLE_EQ(result.effectiveFrameCount, static_cast<double>(kFrames));
    EXPECT_EQ(result.workingBytes, 0u);
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackFrameWeightingTest, NoiseWeightsAreTheInverseSquaredNoiseScaledToTheReferenceDispersion)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::StackRequest request = requestOf(an::StackWeighting::Noise);
    request.normalization.referenceFrame = 1u;
    const an::StackModel model = makeModel(request);

    const std::array<double, kFrames> noise{0.010, 0.020, 0.040, 0.015};
    const std::array<double, kFrames> dispersion{1.0, 2.0, 4.0, 0.5};
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].backgroundNoise = noise[frame];
        measurements[frame].dispersion = dispersion[frame];
    }

    // The rule: `w = 1 / (scale^2 * noise^2)` with `scale = referenceDispersion /
    // frameDispersion`, then every weight divided by the mean.
    std::vector<double> expected;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const double scale = dispersion[1] / dispersion[frame];
        expected.push_back(1.0 / (scale * scale * noise[frame] * noise[frame]));
    }
    expected = meanOne(expected);

    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    ASSERT_EQ(table.size(), kFrames);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(table[frame], expected[frame]) << frame;
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
    EXPECT_EQ(result.source, an::StackWeighting::Noise);
    EXPECT_EQ(result.measuredFrameCount, kFrames);
    EXPECT_FALSE(result.perChannel);
    EXPECT_EQ(result.summary.positiveCount, kFrames);
    const an::StackWeightSummary summary = an::summarizeStackFrameWeights(table);
    EXPECT_DOUBLE_EQ(result.summary.efficiency, summary.efficiency);
    EXPECT_DOUBLE_EQ(result.effectiveFrameCount, summary.efficiency * kFrames);
    EXPECT_EQ(liveBytes(engine), 0u);

    // Without a dispersion on the reference the noise stands alone; a frame missing its own
    // dispersion also stands alone.
    std::vector<an::StackFrameMeasurement> plain = measurements;
    plain[1].dispersion.reset();
    std::vector<double> alone;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        alone.push_back(1.0 / (noise[frame] * noise[frame]));
    }
    alone = meanOne(alone);
    const std::vector<double> unscaled = weightsOf(execution, model, plain);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(unscaled[frame], alone[frame]) << frame;
    }

    std::vector<an::StackFrameMeasurement> oneMissing = measurements;
    oneMissing[2].dispersion.reset();
    std::vector<double> mixed;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const double scale = frame == 2u ? 1.0 : dispersion[1] / dispersion[frame];
        mixed.push_back(1.0 / (scale * scale * noise[frame] * noise[frame]));
    }
    mixed = meanOne(mixed);
    const std::vector<double> partial = weightsOf(execution, model, oneMissing);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(partial[frame], mixed[frame]) << frame;
    }
}

TEST(StackFrameWeightingTest, NoiseWeightsAreDerivedPerChannelWhenEveryFrameMeasuresEveryChannel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf(an::StackWeighting::Noise, kFrames, an::ChannelSchema::Rgb));
    constexpr std::uint32_t kChannels = 3u;

    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    std::array<std::array<double, kChannels>, kFrames> noise{};
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].backgroundNoise = 0.01 * static_cast<double>(frame + 1u);
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            noise[frame][channel] = 0.005 * static_cast<double>(frame + 1u) * static_cast<double>(channel + 2u);
            measurements[frame].channels[channel].backgroundNoise = noise[frame][channel];
        }
    }

    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    ASSERT_EQ(table.size(), static_cast<std::size_t>(kFrames) * kChannels);
    EXPECT_TRUE(result.perChannel);
    EXPECT_EQ(result.channelCount, kChannels);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        std::vector<double> expected;
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            expected.push_back(1.0 / (noise[frame][channel] * noise[frame][channel]));
        }
        expected = meanOne(expected);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            EXPECT_DOUBLE_EQ(table[static_cast<std::size_t>(frame) * kChannels + channel], expected[frame])
                << frame << ':' << channel;
        }
    }

    // A channel no frame measures takes the frame-level weights; a channel only some frames
    // measure is refused, naming the first frame without it.
    std::vector<an::StackFrameMeasurement> twoChannels = measurements;
    for (an::StackFrameMeasurement& measurement : twoChannels)
    {
        measurement.channels[2].backgroundNoise.reset();
    }
    const std::vector<double> mixed = weightsOf(execution, model, twoChannels);
    std::vector<double> frameLevel;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        const double value = *measurements[frame].backgroundNoise;
        frameLevel.push_back(1.0 / (value * value));
    }
    frameLevel = meanOne(frameLevel);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(mixed[static_cast<std::size_t>(frame) * kChannels + 2u], frameLevel[frame]) << frame;
        EXPECT_DOUBLE_EQ(mixed[static_cast<std::size_t>(frame) * kChannels],
                         table[static_cast<std::size_t>(frame) * kChannels])
            << frame;
    }

    std::vector<an::StackFrameMeasurement> partial = measurements;
    partial[2].channels[1].backgroundNoise.reset();
    std::vector<double> untouched(static_cast<std::size_t>(kFrames) * kChannels, -1.0);
    const an::Result<an::StackFrameWeightingResult> refused =
        an::computeStackFrameWeights(execution, model, partial, untouched);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "channel")) << refused.status().message();
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), 2u);
    for (const double weight : untouched)
    {
        EXPECT_DOUBLE_EQ(weight, -1.0);
    }
}

TEST(StackFrameWeightingTest, APartiallyMeasuredSetIsRefusedByFrameAndAnUnmeasuredSetIsUniformWithAWarning)
{
    const an::EngineContext engine = makeEngine();
    RecordingDiagnosticSink sink;
    const an::ExecutionContext execution = makeExecution(engine, &sink);
    const an::StackModel model = makeModel(requestOf(an::StackWeighting::Noise));

    std::vector<an::StackFrameMeasurement> partial(kFrames);
    partial[0].backgroundNoise = 0.01;
    partial[1].backgroundNoise = 0.02;
    partial[3].backgroundNoise = 0.03;
    std::vector<double> table(kFrames, -1.0);
    const an::Result<an::StackFrameWeightingResult> refused =
        an::computeStackFrameWeights(execution, model, partial, table);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "backgroundNoise")) << refused.status().message();
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), 2u);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Limit), kFrames);
    for (const double weight : table)
    {
        EXPECT_DOUBLE_EQ(weight, -1.0);
    }

    const std::vector<an::StackFrameMeasurement> nothing(kFrames);
    an::StackFrameWeightingResult result;
    const std::vector<double> uniform = weightsOf(execution, model, nothing, &result);
    for (const double weight : uniform)
    {
        EXPECT_DOUBLE_EQ(weight, 1.0);
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::UniformNoMeasurements);
    EXPECT_EQ(result.measuredFrameCount, 0u);
    EXPECT_EQ(result.source, an::StackWeighting::Noise);
    const std::vector<RecordingDiagnosticSink::Event> warnings = sink.from(kOperation, an::DiagnosticSeverity::Warning);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].message.find("weight one"), std::string::npos) << warnings[0].message;
}

TEST(StackFrameWeightingTest, StarCountScalingsFollowTheirRuleAndTheFloorKeepsTheWorstFrame)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<std::uint32_t, kFrames> counts{100u, 50u, 0u, 80u};
    const std::array<double, kFrames> roundness{1.0, 0.5, 0.8, 0.25};
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].starCount = counts[frame];
        measurements[frame].medianRoundness = roundness[frame];
    }

    const auto expectedFor = [&](an::StarCountScaling scaling, double alpha, double floor) {
        std::vector<double> effective;
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            const double count = static_cast<double>(counts[frame]);
            effective.push_back(scaling == an::StarCountScaling::RoundnessWeighted && counts[frame] > 0u
                                    ? count * roundness[frame]
                                    : count);
        }
        const double lowest = *std::min_element(effective.begin(), effective.end());
        const double highest = *std::max_element(effective.begin(), effective.end());
        std::vector<double> raw;
        for (const double value : effective)
        {
            const double normal = (value - lowest) / (highest - lowest);
            double weight = 0.0;
            switch (scaling)
            {
            case an::StarCountScaling::Linear:
                weight = normal;
                break;
            case an::StarCountScaling::Quadratic:
            case an::StarCountScaling::RoundnessWeighted:
                weight = normal * normal;
                break;
            case an::StarCountScaling::Exponential:
                weight = std::exp(-alpha * (1.0 - normal));
                break;
            }
            raw.push_back((std::max)(weight, floor));
        }
        return meanOne(raw);
    };

    for (const an::StarCountScaling scaling :
         {an::StarCountScaling::Linear, an::StarCountScaling::Quadratic, an::StarCountScaling::Exponential,
          an::StarCountScaling::RoundnessWeighted})
    {
        an::StackRequest request = requestOf(an::StackWeighting::StarCount);
        request.weighting.starCount.scaling = scaling;
        request.weighting.starCount.exponentialAlpha = 1.5;
        request.weighting.minimumWeight = 0.05;
        const an::StackModel model = makeModel(request);
        an::StackFrameWeightingResult result;
        const std::vector<double> table = weightsOf(execution, model, measurements, &result);
        const std::vector<double> expected = expectedFor(scaling, 1.5, 0.05);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            EXPECT_DOUBLE_EQ(table[frame], expected[frame]) << an::starCountScalingName(scaling) << ' ' << frame;
        }
        EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
        // The frame with no stars is floored under every rule but the exponential one, whose
        // fall-off never reaches the floor; the roundness rule also floors the frame whose
        // effective count squares below it.
        const std::uint32_t floored = scaling == an::StarCountScaling::Exponential         ? 0u
                                      : scaling == an::StarCountScaling::RoundnessWeighted ? 2u
                                                                                           : 1u;
        EXPECT_EQ(result.flooredCount, floored) << an::starCountScalingName(scaling);
        EXPECT_GT(table[2], 0.0) << an::starCountScalingName(scaling);
    }

    // The roundness-weighted rule falls back to the plain count where no roundness was measured.
    std::vector<an::StackFrameMeasurement> unrounded = measurements;
    unrounded[1].medianRoundness.reset();
    an::StackRequest request = requestOf(an::StackWeighting::StarCount);
    request.weighting.starCount.scaling = an::StarCountScaling::RoundnessWeighted;
    const an::StackModel model = makeModel(request);
    const std::vector<double> table = weightsOf(execution, model, unrounded);
    std::vector<double> effective{100.0, 50.0, 0.0, 20.0};
    std::vector<double> raw;
    for (const double value : effective)
    {
        const double normal = value / 100.0;
        raw.push_back((std::max)(normal * normal, an::kDefaultMinimumFrameWeight));
    }
    raw = meanOne(raw);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(table[frame], raw[frame]) << frame;
    }
}

TEST(StackFrameWeightingTest, EqualStarCountsAreUniformAndSaySo)
{
    const an::EngineContext engine = makeEngine();
    RecordingDiagnosticSink sink;
    const an::ExecutionContext execution = makeExecution(engine, &sink);
    const an::StackModel model = makeModel(requestOf(an::StackWeighting::StarCount));
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (an::StackFrameMeasurement& measurement : measurements)
    {
        measurement.starCount = 42u;
    }
    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    for (const double weight : table)
    {
        EXPECT_DOUBLE_EQ(weight, 1.0);
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::UniformEqualMeasurements);
    EXPECT_EQ(result.measuredFrameCount, kFrames);
    EXPECT_EQ(sink.from(kOperation, an::DiagnosticSeverity::Warning).size(), 0u);
    EXPECT_GE(sink.from(kOperation, an::DiagnosticSeverity::Info).size(), 1u);

    // Every frame with no stars at all is the same case.
    for (an::StackFrameMeasurement& measurement : measurements)
    {
        measurement.starCount = 0u;
    }
    const std::vector<double> none = weightsOf(execution, model, measurements, &result);
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::UniformEqualMeasurements);
    EXPECT_DOUBLE_EQ(none[0], 1.0);
}

TEST(StackFrameWeightingTest, WidthWeightsFollowTheInverseSquareRuleWithTheStarPenalty)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<double, kFrames> width{2.0, 3.0, 4.0, 2.5};
    const std::array<std::uint32_t, kFrames> stars{100u, 80u, 50u, 120u};

    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].fullWidth = width[frame];
        measurements[frame].starCount = stars[frame];
    }

    // The rule: with a reference star count, each width is penalized by
    // `2 * width * (referenceStars - stars) / stars` and floored at a tenth of itself; the raw
    // weight is the inverse square rescaled between the narrowest and the widest; the floor keeps
    // the widest frame.
    const auto expectedFor = [&](bool penalty, double floor) {
        std::vector<double> effective;
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            double value = width[frame];
            if (penalty)
            {
                const double starPenalty = 2.0 * value *
                                           (static_cast<double>(stars[0]) - static_cast<double>(stars[frame])) /
                                           static_cast<double>(stars[frame]);
                value = (std::max)(value + starPenalty, width[frame] * 0.1);
            }
            effective.push_back(value);
        }
        const double lowest = *std::min_element(effective.begin(), effective.end());
        const double highest = *std::max_element(effective.begin(), effective.end());
        const double inverseDenominator = 1.0 / ((1.0 / (lowest * lowest)) - (1.0 / (highest * highest)));
        const double inverseHighest = 1.0 / (highest * highest);
        std::vector<double> raw;
        for (const double value : effective)
        {
            const double inverse = 1.0 / (value * value);
            raw.push_back((std::max)((std::max)(0.0, (inverse - inverseHighest) * inverseDenominator), floor));
        }
        return meanOne(raw);
    };

    an::StackRequest request = requestOf(an::StackWeighting::Fwhm);
    request.weighting.minimumWeight = 0.02;
    const an::StackModel model = makeModel(request);
    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    const std::vector<double> expected = expectedFor(true, 0.02);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(table[frame], expected[frame]) << frame;
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
    EXPECT_EQ(result.flooredCount, 1u);

    // Without star counts the width stands alone.
    std::vector<an::StackFrameMeasurement> unstarred = measurements;
    for (an::StackFrameMeasurement& measurement : unstarred)
    {
        measurement.starCount.reset();
    }
    const std::vector<double> plain = weightsOf(execution, model, unstarred);
    const std::vector<double> plainExpected = expectedFor(false, 0.02);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(plain[frame], plainExpected[frame]) << frame;
    }

    // Equal widths cannot rank the frames and are uniform.
    std::vector<an::StackFrameMeasurement> same = unstarred;
    for (an::StackFrameMeasurement& measurement : same)
    {
        measurement.fullWidth = 3.0;
    }
    const std::vector<double> uniform = weightsOf(execution, model, same, &result);
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::UniformEqualMeasurements);
    EXPECT_DOUBLE_EQ(uniform[3], 1.0);
}

TEST(StackFrameWeightingTest, WidthWeightsUseEachChannelsWidthOnlyWhenAskedAndEveryFrameCarriesIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kChannels = 3u;
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    const std::array<double, kFrames> width{2.0, 3.0, 4.0, 2.5};
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].fullWidth = width[frame];
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            measurements[frame].channels[channel].fullWidth = width[frame] + 0.5 * static_cast<double>(channel);
        }
    }

    const auto expectedFor = [&](std::optional<std::uint32_t> channel) {
        std::vector<double> effective;
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            effective.push_back(channel.has_value() ? *measurements[frame].channels[*channel].fullWidth : width[frame]);
        }
        const double lowest = *std::min_element(effective.begin(), effective.end());
        const double highest = *std::max_element(effective.begin(), effective.end());
        const double inverseDenominator = 1.0 / ((1.0 / (lowest * lowest)) - (1.0 / (highest * highest)));
        const double inverseHighest = 1.0 / (highest * highest);
        std::vector<double> raw;
        for (const double value : effective)
        {
            raw.push_back((std::max)((std::max)(0.0, (1.0 / (value * value) - inverseHighest) * inverseDenominator),
                                     an::kDefaultMinimumFrameWeight));
        }
        return meanOne(raw);
    };

    an::StackRequest request = requestOf(an::StackWeighting::Fwhm, kFrames, an::ChannelSchema::Rgb);
    request.weighting.perChannelFwhm = true;
    an::StackFrameWeightingResult result;
    const std::vector<double> perChannel = weightsOf(execution, makeModel(request), measurements, &result);
    EXPECT_TRUE(result.perChannel);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        const std::vector<double> expected = expectedFor(channel);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            EXPECT_DOUBLE_EQ(perChannel[static_cast<std::size_t>(frame) * kChannels + channel], expected[frame])
                << frame << ':' << channel;
        }
    }

    request.weighting.perChannelFwhm = false;
    const std::vector<double> shared = weightsOf(execution, makeModel(request), measurements, &result);
    EXPECT_FALSE(result.perChannel);
    const std::vector<double> frameLevel = expectedFor(std::nullopt);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            EXPECT_DOUBLE_EQ(shared[static_cast<std::size_t>(frame) * kChannels + channel], frameLevel[frame])
                << frame << ':' << channel;
        }
    }

    request.weighting.perChannelFwhm = true;
    std::vector<an::StackFrameMeasurement> partial = measurements;
    partial[3].channels[0].fullWidth.reset();
    std::vector<double> untouched(static_cast<std::size_t>(kFrames) * kChannels, -1.0);
    const an::Result<an::StackFrameWeightingResult> refused =
        an::computeStackFrameWeights(execution, makeModel(request), partial, untouched);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(refused.status(), "fullWidth")) << refused.status().message();
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), 3u);
}

TEST(StackFrameWeightingTest, ImageCountWeightsAreTheIntegratedFrameCounts)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::StackModel model = makeModel(requestOf(an::StackWeighting::ImageCount));
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    measurements[0].integratedFrameCount = 4u;
    measurements[1].integratedFrameCount = 1u;
    measurements[2].integratedFrameCount = 2u;
    // A frame that states nothing is one frame, as a missing card is read.
    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    const std::vector<double> expected = meanOne({4.0, 1.0, 2.0, 1.0});
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT_DOUBLE_EQ(table[frame], expected[frame]) << frame;
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
    EXPECT_EQ(result.measuredFrameCount, 3u);

    // Nothing stated anywhere is a uniform stack of single frames, computed rather than a fallback.
    const std::vector<an::StackFrameMeasurement> singles(kFrames);
    const std::vector<double> ones = weightsOf(execution, model, singles, &result);
    EXPECT_DOUBLE_EQ(ones[2], 1.0);
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);

    std::vector<an::StackFrameMeasurement> zero = measurements;
    zero[2].integratedFrameCount = 0u;
    std::vector<double> untouched(kFrames, -1.0);
    const an::Result<an::StackFrameWeightingResult> refused =
        an::computeStackFrameWeights(execution, model, zero, untouched);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), 2u);
}

/// Sets the figure the exposure or the signal-to-noise source reads.
void setFigure(an::StackFrameMeasurement& measurement, an::StackWeighting source, double value)
{
    if (source == an::StackWeighting::Exposure)
    {
        measurement.exposureSeconds = value;
    }
    else
    {
        measurement.signalToNoise = value;
    }
}

/// The member spelling a refusal about the figure a source reads names.
[[nodiscard]] std::string_view figureSpelling(an::StackWeighting source)
{
    return source == an::StackWeighting::Exposure ? "exposureSeconds" : "signalToNoise";
}

TEST(StackFrameWeightingTest, ExposureAndSignalToNoiseWeightsAreTheFiguresOverTheirMean)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kChannels = 3u;

    // The standalone stacking weighs a frame by its exposure in seconds, or by the signal-to-noise
    // ratio its registration measured, and divides every weight by the mean.
    const std::array<double, kFrames> exposures{30.0, 60.0, 120.0, 45.0};
    const std::array<double, kFrames> ratios{12.5, 20.0, 8.0, 15.25};
    for (const an::StackWeighting source : {an::StackWeighting::Exposure, an::StackWeighting::SignalToNoise})
    {
        const std::array<double, kFrames>& figures = source == an::StackWeighting::Exposure ? exposures : ratios;
        an::StackRequest request = requestOf(source, kFrames, an::ChannelSchema::Rgb);
        // The floor belongs to the ranking sources; a ratio source is positive by construction.
        request.weighting.minimumWeight = 0.9;
        const an::StackModel model = makeModel(request);

        std::vector<an::StackFrameMeasurement> measurements(kFrames);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            setFigure(measurements[frame], source, figures[frame]);
            // Figures the source does not read change nothing.
            measurements[frame].backgroundNoise = 0.01 * static_cast<double>(frame + 1u);
            measurements[frame].starCount = 10u * (frame + 1u);
        }
        const std::vector<double> expected = meanOne({figures.begin(), figures.end()});

        an::StackFrameWeightingResult result;
        const std::vector<double> table = weightsOf(execution, model, measurements, &result);
        ASSERT_EQ(table.size(), static_cast<std::size_t>(kFrames) * kChannels);
        for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
        {
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                EXPECT_DOUBLE_EQ(table[static_cast<std::size_t>(frame) * kChannels + channel], expected[frame])
                    << an::stackWeightingName(source) << ' ' << frame << ' ' << channel;
            }
        }
        EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
        EXPECT_EQ(result.source, source);
        EXPECT_EQ(result.measuredFrameCount, kFrames);
        EXPECT_EQ(result.flooredCount, 0u);
        EXPECT_FALSE(result.perChannel);
        EXPECT_EQ(result.summary.positiveCount, static_cast<std::uint64_t>(kFrames) * kChannels);
        EXPECT_DOUBLE_EQ(result.effectiveFrameCount, an::summarizeStackFrameWeights(table).efficiency * kFrames);
        EXPECT_FALSE(an::stackWeightingUsesMinimumWeight(source));

        // Equal figures weigh every frame the same, which the ratio computes rather than falls back to.
        std::vector<an::StackFrameMeasurement> equal(kFrames);
        for (an::StackFrameMeasurement& measurement : equal)
        {
            setFigure(measurement, source, figures[1]);
        }
        const std::vector<double> ones = weightsOf(execution, model, equal, &result);
        for (const double weight : ones)
        {
            EXPECT_DOUBLE_EQ(weight, 1.0) << an::stackWeightingName(source);
        }
        EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
    }
    EXPECT_EQ(liveBytes(engine), 0u);
}

TEST(StackFrameWeightingTest, ExposureAndSignalToNoiseRefuseAPartialSetAndAreUniformWithoutTheirFigure)
{
    const an::EngineContext engine = makeEngine();
    RecordingDiagnosticSink sink;
    const an::ExecutionContext execution = makeExecution(engine, &sink);
    std::size_t warningsSoFar = 0u;
    for (const an::StackWeighting source : {an::StackWeighting::Exposure, an::StackWeighting::SignalToNoise})
    {
        const an::StackModel model = makeModel(requestOf(source));

        // A frame without the figure is not given weight zero while the rest are stacked: a weighting
        // that drops a frame nobody asked to drop is refused, naming the first frame without it.
        std::vector<an::StackFrameMeasurement> partial(kFrames);
        setFigure(partial[0], source, 30.0);
        setFigure(partial[1], source, 60.0);
        setFigure(partial[3], source, 45.0);
        std::vector<double> table(kFrames, -1.0);
        const an::Result<an::StackFrameWeightingResult> refused =
            an::computeStackFrameWeights(execution, model, partial, table);
        ASSERT_FALSE(refused.hasValue()) << an::stackWeightingName(source);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(messageNames(refused.status(), figureSpelling(source))) << refused.status().message();
        EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), 2u);
        EXPECT_EQ(countField(refused.status(), an::ContextKey::Limit), kFrames);
        for (const double weight : table)
        {
            EXPECT_DOUBLE_EQ(weight, -1.0) << an::stackWeightingName(source);
        }

        // No frame carrying it is a uniform stack, and says so.
        const std::vector<an::StackFrameMeasurement> nothing(kFrames);
        an::StackFrameWeightingResult result;
        const std::vector<double> uniform = weightsOf(execution, model, nothing, &result);
        for (const double weight : uniform)
        {
            EXPECT_DOUBLE_EQ(weight, 1.0) << an::stackWeightingName(source);
        }
        EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::UniformNoMeasurements);
        EXPECT_EQ(result.measuredFrameCount, 0u);
        const std::vector<RecordingDiagnosticSink::Event> warnings =
            sink.from(kOperation, an::DiagnosticSeverity::Warning);
        ASSERT_EQ(warnings.size(), warningsSoFar + 1u) << an::stackWeightingName(source);
        warningsSoFar = warnings.size();
    }
}

TEST(StackFrameWeightingTest, PhotometricWeightsAreTheModelsOwnReplicatedAcrossChannels)
{
    const an::EngineContext engine = makeEngine();
    RecordingDiagnosticSink sink;
    const an::ExecutionContext execution = makeExecution(engine, &sink);
    constexpr std::uint32_t kChannels = 3u;
    an::StackRequest request = requestOf(an::StackWeighting::Photometric, kFrames, an::ChannelSchema::Rgb);
    request.weighting.photometric.minimumRawWeight = 0.05;
    const an::StackModel model = makeModel(request);

    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    std::vector<an::PhotometricFrameMeasurement> photometric(kFrames);
    photometric[0].zeroPointMagnitudes = 21.0;
    photometric[0].zeroPointStandardUncertaintyMagnitudes = 0.02;
    photometric[0].astrometricRootMeanSquarePixels = 0.3;
    photometric[1].zeroPointMagnitudes = 20.5;
    photometric[1].zeroPointStandardUncertaintyMagnitudes = 0.05;
    photometric[1].astrometricRootMeanSquarePixels = 0.9;
    photometric[3].zeroPointMagnitudes = 21.2;
    photometric[3].zeroPointStandardUncertaintyMagnitudes = 0.01;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].photometric = photometric[frame];
    }

    std::vector<an::PhotometricStackWeight> direct(kFrames);
    const an::Result<an::PhotometricWeightingResult> reference =
        an::computePhotometricStackWeights(execution, photometric, request.weighting.photometric, direct);
    ASSERT_TRUE(reference.hasValue()) << reference.status().message();

    an::StackFrameWeightingResult result;
    const std::vector<double> table = weightsOf(execution, model, measurements, &result);
    ASSERT_EQ(table.size(), static_cast<std::size_t>(kFrames) * kChannels);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
        {
            EXPECT_DOUBLE_EQ(table[static_cast<std::size_t>(frame) * kChannels + channel], direct[frame].value)
                << frame << ':' << channel;
        }
    }
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::Computed);
    EXPECT_FALSE(result.perChannel);
    EXPECT_EQ(result.measuredFrameCount, reference->measuredFrameCount);
    ASSERT_TRUE(result.photometric.has_value());
    EXPECT_EQ(result.photometric->disposition, an::PhotometricWeightingDisposition::Computed);
    EXPECT_EQ(result.photometric->medianSubstitutionCount, reference->medianSubstitutionCount);
    EXPECT_DOUBLE_EQ(result.photometric->effectiveFrameCount, reference->effectiveFrameCount);
    EXPECT_GT(result.workingBytes, 0u);
    EXPECT_EQ(liveBytes(engine), 0u);

    // No zero point anywhere is the model's own uniform fallback, reported through both dispositions.
    const std::vector<an::StackFrameMeasurement> nothing(kFrames);
    const std::vector<double> uniform = weightsOf(execution, model, nothing, &result);
    EXPECT_DOUBLE_EQ(uniform[5], 1.0);
    EXPECT_EQ(result.disposition, an::StackFrameWeightingDisposition::UniformNoMeasurements);
    ASSERT_TRUE(result.photometric.has_value());
    EXPECT_EQ(result.photometric->disposition, an::PhotometricWeightingDisposition::UniformNoZeroPoints);

    // A malformed photometric record is the model's own refusal, passed through.
    std::vector<an::StackFrameMeasurement> malformed = measurements;
    malformed[2].photometric.zeroPointStandardUncertaintyMagnitudes = 0.1;
    std::vector<double> untouched(static_cast<std::size_t>(kFrames) * kChannels, -1.0);
    const an::Result<an::StackFrameWeightingResult> refused =
        an::computeStackFrameWeights(execution, model, malformed, untouched);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_DOUBLE_EQ(untouched[0], -1.0);
}

TEST(StackFrameWeightingTest, MalformedMeasurementsAreRefusedNamingTheFrame)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<double> table(kFrames, -1.0);

    const auto expectRefusal = [&](const an::StackModel& model, std::span<const an::StackFrameMeasurement> measurements,
                                   std::uint32_t frame, std::string_view field) {
        const an::Result<an::StackFrameWeightingResult> refused =
            an::computeStackFrameWeights(execution, model, measurements, table);
        ASSERT_FALSE(refused.hasValue()) << field;
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument) << field;
        EXPECT_TRUE(messageNames(refused.status(), field)) << refused.status().message();
        EXPECT_EQ(countField(refused.status(), an::ContextKey::Requested), frame) << field;
        for (const double weight : table)
        {
            EXPECT_DOUBLE_EQ(weight, -1.0) << field;
        }
    };

    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (an::StackFrameMeasurement& measurement : measurements)
    {
        measurement.backgroundNoise = 0.01;
        measurement.dispersion = 1.0;
        measurement.fullWidth = 2.5;
        measurement.starCount = 10u;
        measurement.medianRoundness = 0.9;
    }

    std::vector<an::StackFrameMeasurement> negativeNoise = measurements;
    negativeNoise[1].backgroundNoise = -0.01;
    expectRefusal(makeModel(requestOf(an::StackWeighting::Noise)), negativeNoise, 1u, "backgroundNoise");

    std::vector<an::StackFrameMeasurement> zeroDispersion = measurements;
    zeroDispersion[2].dispersion = 0.0;
    expectRefusal(makeModel(requestOf(an::StackWeighting::Noise)), zeroDispersion, 2u, "dispersion");

    std::vector<an::StackFrameMeasurement> infiniteWidth = measurements;
    infiniteWidth[3].fullWidth = std::numeric_limits<double>::infinity();
    expectRefusal(makeModel(requestOf(an::StackWeighting::Fwhm)), infiniteWidth, 3u, "fullWidth");

    std::vector<an::StackFrameMeasurement> roundness = measurements;
    roundness[0].medianRoundness = 1.5;
    expectRefusal(makeModel(requestOf(an::StackWeighting::StarCount)), roundness, 0u, "medianRoundness");

    std::vector<an::StackFrameMeasurement> channel = measurements;
    channel[1].channels[0].backgroundNoise = std::numeric_limits<double>::quiet_NaN();
    expectRefusal(makeModel(requestOf(an::StackWeighting::Noise)), channel, 1u, "backgroundNoise");

    // A source that reads none of a malformed field still refuses it: a record is checked whole.
    std::vector<an::StackFrameMeasurement> unread = measurements;
    unread[2].fullWidth = 0.0;
    expectRefusal(makeModel(requestOf(an::StackWeighting::Noise)), unread, 2u, "fullWidth");

    for (const double bad :
         {0.0, -30.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        std::vector<an::StackFrameMeasurement> exposure = measurements;
        exposure[3].exposureSeconds = bad;
        expectRefusal(makeModel(requestOf(an::StackWeighting::Exposure)), exposure, 3u, "exposureSeconds");
        expectRefusal(makeModel(requestOf(an::StackWeighting::Noise)), exposure, 3u, "exposureSeconds");

        std::vector<an::StackFrameMeasurement> ratio = measurements;
        ratio[1].signalToNoise = bad;
        expectRefusal(makeModel(requestOf(an::StackWeighting::SignalToNoise)), ratio, 1u, "signalToNoise");
        expectRefusal(makeModel(requestOf(an::StackWeighting::StarCount)), ratio, 1u, "signalToNoise");
    }
}

TEST(StackFrameWeightingTest, ShapesAndTheExplicitSourceAreRefusedBeforeAnythingIsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (an::StackFrameMeasurement& measurement : measurements)
    {
        measurement.backgroundNoise = 0.01;
    }
    std::vector<double> table(kFrames, -1.0);
    const an::StackModel model = makeModel(requestOf(an::StackWeighting::Noise));

    const an::Result<an::StackFrameWeightingResult> explicitSource = an::computeStackFrameWeights(
        execution, makeModel(requestOf(an::StackWeighting::Explicit)), measurements, table);
    ASSERT_FALSE(explicitSource.hasValue());
    EXPECT_EQ(explicitSource.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(explicitSource.status(), "Explicit")) << explicitSource.status().message();

    const std::vector<an::StackFrameMeasurement> tooFew(kFrames - 1u);
    const an::Result<an::StackFrameWeightingResult> count =
        an::computeStackFrameWeights(execution, model, tooFew, table);
    ASSERT_FALSE(count.hasValue());
    EXPECT_EQ(count.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(count.status(), "measurements")) << count.status().message();

    std::vector<double> shortTable(kFrames - 1u, -1.0);
    const an::Result<an::StackFrameWeightingResult> output =
        an::computeStackFrameWeights(execution, model, measurements, shortTable);
    ASSERT_FALSE(output.hasValue());
    EXPECT_EQ(output.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(output.status(), "frameWeights")) << output.status().message();

    const an::ExecutionContext nothing;
    const an::Result<an::StackFrameWeightingResult> unheld =
        an::computeStackFrameWeights(nothing, model, measurements, table);
    ASSERT_FALSE(unheld.hasValue());
    EXPECT_EQ(unheld.status().code(), an::ErrorCode::NotInitialized);

    // A stopped operation on an engine of its own, because the live execution above has this
    // engine's whole thread budget admitted.
    const an::EngineContext other = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(other, nullptr, stop.get_token());
    const an::Result<an::StackFrameWeightingResult> cancelled =
        an::computeStackFrameWeights(stopped, model, measurements, table);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);
    for (const double weight : table)
    {
        EXPECT_DOUBLE_EQ(weight, -1.0);
    }
    EXPECT_EQ(liveBytes(engine), 0u);
    EXPECT_EQ(liveBytes(other), 0u);
}

TEST(StackFrameWeightingTest, TheTableSatisfiesTheModelsOwnExplicitWeightCheck)
{
    // What this operation writes is exactly what a caller could have handed to an explicit stack,
    // so the model's checker accepts it as a per-frame-and-channel table.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kChannels = 3u;
    std::vector<an::StackFrameMeasurement> measurements(kFrames);
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        measurements[frame].backgroundNoise = 0.01 * static_cast<double>(frame + 1u);
    }
    const std::vector<double> table = weightsOf(
        execution, makeModel(requestOf(an::StackWeighting::Noise, kFrames, an::ChannelSchema::Rgb)), measurements);
    ASSERT_EQ(table.size(), static_cast<std::size_t>(kFrames) * kChannels);
    const an::StackModel explicitModel =
        makeModel(requestOf(an::StackWeighting::Explicit, kFrames, an::ChannelSchema::Rgb));
    const an::Status accepted =
        an::checkStackFrameWeights(explicitModel, an::StackFrameWeights{.perFrameChannel = table});
    EXPECT_TRUE(accepted.ok()) << accepted.message();
    double sum = 0.0;
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        sum += table[static_cast<std::size_t>(frame) * kChannels];
    }
    EXPECT_NEAR(sum, static_cast<double>(kFrames), 1e-12);
}

TEST(StackFrameWeightingTest, NameTablesAreClosed)
{
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(an::StackFrameWeightingDisposition::Uniform), "Uniform");
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(an::StackFrameWeightingDisposition::Computed), "Computed");
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(an::StackFrameWeightingDisposition::UniformNoMeasurements),
                 "UniformNoMeasurements");
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(an::StackFrameWeightingDisposition::UniformEqualMeasurements),
                 "UniformEqualMeasurements");
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(an::StackFrameWeightingDisposition::UniformDegenerate),
                 "UniformDegenerate");
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(an::StackFrameWeightingDisposition::Supplied), "Supplied");
    EXPECT_STREQ(an::stackFrameWeightingDispositionName(static_cast<an::StackFrameWeightingDisposition>(9u)),
                 "UnknownStackFrameWeightingDisposition");
    static_assert(an::isStackFrameWeightingDisposition(an::StackFrameWeightingDisposition::Supplied));
    EXPECT_FALSE(an::isStackFrameWeightingDisposition(static_cast<an::StackFrameWeightingDisposition>(6u)));
    static_assert(an::kMaxStackWeightChannels == 4u);
}

} // namespace
