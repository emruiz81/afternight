// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract and property tests for AfterNightEngine/Algorithms/Demosaic/Demosaic.h.
//
// A demosaic has no closed form to check against, so these cases test the things that are true of
// every estimate whatever the picture is.
//
// **A picture with no detail comes back with no detail.** If every photosite reads the same value,
// every colour of every pixel has to read that value too: there is nothing for a directional
// decision to decide and nothing for a colour difference to carry. Every method is held to that,
// which is a strong test - it exercises the whole chain of every pass, and a transposed index or a
// wrong phase breaks it immediately.
//
// **The measured sample survives.** Whatever else an estimate does, the colour that was actually
// recorded at a site is the number that was recorded, not an interpolation of its neighbours.
//
// **The answer does not depend on how many threads ran.** Every output pixel belongs to exactly one
// tile, so a one-worker run and a many-worker run produce the same bytes.
//
// **What is refused is refused by name**, and the reasons are the ones a caller can act on.

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>

#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

/// One real camera table, for the tabulated methods.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// Every method this build knows, with a mosaic it applies to.
struct MethodCase
{
    /// The estimate.
    an::DemosaicMethod method;

    /// A mosaic of the shape it estimates.
    an::CfaDescriptor cfa;

    /// How close a constant picture has to come back, relative to the constant.
    float constantTolerance;
};

/// Records one demosaic's progress and traces without allocating in a callback.
class DemosaicTelemetrySink final : public an::ProgressSink, public an::TraceSink
{
public:
    /// Optionally asks the execution to stop after its first interior-tile report.
    ///
    /// @param stop the operation's stop source, or null to let it finish.
    explicit DemosaicTelemetrySink(std::stop_source* stop = nullptr) noexcept : m_stop(stop) {}

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        if (m_progressCount < m_completed.size())
        {
            m_completed[m_progressCount] = report.completed;
            m_totals[m_progressCount] = report.total;
            ++m_progressCount;
        }
        if (std::string_view{report.stage} == "estimating demosaic interior")
        {
            m_sawInterior = true;
            if (m_stop != nullptr && !m_requestedStop)
            {
                m_requestedStop = true;
                static_cast<void>(m_stop->request_stop());
            }
        }
    }

    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (event.kind == an::TraceEventKind::SpanBegin && std::string_view{event.name} == "demosaicImage")
        {
            ++m_spanBegins;
        }
        else if (event.kind == an::TraceEventKind::SpanEnd && std::string_view{event.name} == "demosaicImage")
        {
            ++m_spanEnds;
        }
        else if (event.kind == an::TraceEventKind::Counter)
        {
            if (std::string_view{event.name} == "tiles")
            {
                m_tiles = event.value;
            }
            else if (std::string_view{event.name} == "working-memory")
            {
                m_workingBytes = event.value;
            }
            else if (std::string_view{event.name} == "samples-converted")
            {
                m_sawConversionCounter = true;
            }
            else if (std::string_view{event.name} == "valid-output-samples")
            {
                m_sawValidityCounter = true;
            }
        }
    }

    [[nodiscard]] bool progressIsMonotonic() const noexcept
    {
        for (std::size_t index = 1u; index < m_progressCount; ++index)
        {
            if (m_completed[index] < m_completed[index - 1u])
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t progressCount() const noexcept
    {
        return m_progressCount;
    }

    [[nodiscard]] std::uint64_t lastCompleted() const noexcept
    {
        return m_progressCount == 0u ? 0u : m_completed[m_progressCount - 1u];
    }

    [[nodiscard]] std::uint64_t lastTotal() const noexcept
    {
        return m_progressCount == 0u ? 0u : m_totals[m_progressCount - 1u];
    }

    [[nodiscard]] bool sawInterior() const noexcept
    {
        return m_sawInterior;
    }

    [[nodiscard]] std::uint32_t spanBegins() const noexcept
    {
        return m_spanBegins;
    }

    [[nodiscard]] std::uint32_t spanEnds() const noexcept
    {
        return m_spanEnds;
    }

    [[nodiscard]] double tiles() const noexcept
    {
        return m_tiles;
    }

    [[nodiscard]] double workingBytes() const noexcept
    {
        return m_workingBytes;
    }

    [[nodiscard]] bool sawEveryResultCounter() const noexcept
    {
        return m_sawConversionCounter && m_sawValidityCounter;
    }

private:
    std::stop_source* m_stop = nullptr;
    std::array<std::uint64_t, 512u> m_completed{};
    std::array<std::uint64_t, 512u> m_totals{};
    std::size_t m_progressCount = 0u;
    std::uint32_t m_spanBegins = 0u;
    std::uint32_t m_spanEnds = 0u;
    double m_tiles = -1.0;
    double m_workingBytes = -1.0;
    bool m_sawInterior = false;
    bool m_requestedStop = false;
    bool m_sawConversionCounter = false;
    bool m_sawValidityCounter = false;
};

/// A named Bayer mosaic with no phase.
///
/// @param pattern which arrangement.
/// @return the descriptor.
[[nodiscard]] an::CfaDescriptor bayer(an::CfaPattern pattern)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, 0u, 0u);
    EXPECT_TRUE(cfa.has_value());
    return cfa.value_or(an::CfaDescriptor{});
}

/// The tabulated mosaic with no phase.
///
/// @return the descriptor.
[[nodiscard]] an::CfaDescriptor tabulated()
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, 0u, 0u);
    EXPECT_TRUE(cfa.has_value());
    return cfa.value_or(an::CfaDescriptor{});
}

/// Every method, paired with a mosaic it estimates and the tolerance a constant picture needs.
///
/// The two methods that correct by a ratio rather than a difference add a tolerance to every
/// denominator, so a constant picture comes back within a few parts in a million rather than
/// exactly; the rest are exact.
///
/// @return the cases.
[[nodiscard]] std::vector<MethodCase> everyMethod()
{
    return {{an::DemosaicMethod::Bilinear, bayer(an::CfaPattern::Rggb), 0.0f},
            {an::DemosaicMethod::Fast, bayer(an::CfaPattern::Grbg), 0.0f},
            {an::DemosaicMethod::Rcd, bayer(an::CfaPattern::Bggr), 1e-4f},
            {an::DemosaicMethod::Amaze, bayer(an::CfaPattern::Gbrg), 1e-4f},
            {an::DemosaicMethod::XTransFast, tabulated(), 0.0f},
            {an::DemosaicMethod::MarkesteijnOnePass, tabulated(), 1e-5f},
            {an::DemosaicMethod::MarkesteijnThreePass, tabulated(), 1e-5f},
            {an::DemosaicMethod::Lmmse, bayer(an::CfaPattern::Rggb), 1e-5f},
            {an::DemosaicMethod::Vng4, bayer(an::CfaPattern::Grbg), 1e-5f},
            {an::DemosaicMethod::LuminanceAhd, bayer(an::CfaPattern::Bggr), 1e-5f}};
}

/// A demosaic plan for one method and mosaic.
///
/// @param method the estimate.
/// @param cfa the mosaic.
/// @return the plan.
[[nodiscard]] an::DemosaicPlan planFor(an::DemosaicMethod method, const an::CfaDescriptor& cfa)
{
    const an::DemosaicRequest request{.method = method};
    an::Result<an::DemosaicPlan> plan = an::planDemosaic(request, cfa);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    return plan.hasValue() ? plan.value() : an::DemosaicPlan{request, cfa};
}

/// One demosaic over a Float32 planar picture.
///
/// @param execution the operation.
/// @param mosaic the source samples, tightly packed.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param plan the estimate.
/// @param destination the three destination planes, resized to fit.
/// @param selection which source samples mean something.
/// @return whatever the operation reported.
[[nodiscard]] an::Result<an::DemosaicResult> runDemosaic(const an::ExecutionContext& execution,
                                                         const std::vector<float>& mosaic, std::uint32_t width,
                                                         std::uint32_t height, const an::DemosaicPlan& plan,
                                                         std::vector<float>& destination,
                                                         const an::DemosaicSelection& selection = {})
{
    destination.assign(static_cast<std::size_t>(width) * height * 3u, -1.0f);
    return an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(),
                             planarLayout(an::SampleType::Float32, width, height, 1u), destination.data(),
                             planarLayout(an::SampleType::Float32, width, height, 3u), plan, selection);
}

/// A mosaic sampled from a smooth picture, so every method has something to interpolate.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param cfa which filter covers each pixel.
/// @return the samples.
[[nodiscard]] std::vector<float> smoothMosaic(std::uint32_t width, std::uint32_t height, const an::CfaDescriptor& cfa)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const float red = 0.2f + 0.6f * static_cast<float>(x) / static_cast<float>(width);
            const float green = 0.3f + 0.4f * static_cast<float>(y) / static_cast<float>(height);
            const float blue = 0.5f - 0.2f * static_cast<float>(x + y) / static_cast<float>(width + height);
            const an::CfaColor color = cfa.colorAt(x, y);
            const float value = color == an::CfaColor::Red ? red : (color == an::CfaColor::Green ? green : blue);
            samples[static_cast<std::size_t>(y) * width + x] = value;
        }
    }
    return samples;
}

TEST(DemosaicTest, EveryMethodIsNamedDistinctlyAndAppliesToOneShapeOfMosaic)
{
    for (const MethodCase& first : everyMethod())
    {
        EXPECT_TRUE(an::isDemosaicMethod(first.method));
        EXPECT_TRUE(an::demosaicMethodAppliesTo(first.method, first.cfa.pattern()));
        EXPECT_FALSE(an::demosaicMethodAppliesTo(first.method, an::CfaPattern::None));
        EXPECT_GT(an::demosaicMethodReach(first.method), 0u);
        EXPECT_GT(an::demosaicMethodPasses(first.method), 0u);
        for (const MethodCase& second : everyMethod())
        {
            const bool sameName =
                std::string(an::demosaicMethodName(first.method)) == an::demosaicMethodName(second.method);
            EXPECT_EQ(sameName, first.method == second.method);
        }
    }
    EXPECT_FALSE(an::isDemosaicMethod(static_cast<an::DemosaicMethod>(40)));
    EXPECT_STREQ(an::demosaicMethodName(static_cast<an::DemosaicMethod>(40)), "UnknownDemosaicMethod");
    EXPECT_FALSE(an::demosaicMethodAppliesTo(an::DemosaicMethod::Rcd, an::CfaPattern::XTrans));
    EXPECT_FALSE(an::demosaicMethodAppliesTo(an::DemosaicMethod::XTransFast, an::CfaPattern::Rggb));
}

TEST(DemosaicTest, APlanIsRefusedForEveryReasonSeparately)
{
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    EXPECT_EQ(
        an::planDemosaic(an::DemosaicRequest{.method = static_cast<an::DemosaicMethod>(40)}, rggb).status().code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::Rcd, .domain = {.whiteLevel = 0.0}}, rggb)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::Rcd, .domain = {.clipFraction = 1.5}}, rggb)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::Rcd}, an::CfaDescriptor{}).status().code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::XTransFast}, rggb).status().code(),
              an::ErrorCode::IncompatibleArguments);
    an::DemosaicRequest rescaling{.method = an::DemosaicMethod::Rcd};
    rescaling.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::planDemosaic(rescaling, rggb).status().code(), an::ErrorCode::InvalidArgument);

    an::DemosaicRequest unknownPrecision{.method = an::DemosaicMethod::Rcd};
    unknownPrecision.output.contract = static_cast<an::OutputPrecisionContract>(40);
    EXPECT_EQ(an::planDemosaic(unknownPrecision, rggb).status().code(), an::ErrorCode::InvalidArgument);

    an::DemosaicRequest unknownConversion{.method = an::DemosaicMethod::Rcd};
    unknownConversion.output.writeBack.range = static_cast<an::RangePolicy>(40);
    EXPECT_EQ(an::planDemosaic(unknownConversion, rggb).status().code(), an::ErrorCode::InvalidArgument);

    an::DemosaicRequest tooLarge{.method = an::DemosaicMethod::Rcd,
                                 .domain = {.whiteLevel = (std::numeric_limits<double>::max)()}};
    EXPECT_EQ(an::planDemosaic(tooLarge, rggb).status().code(), an::ErrorCode::InvalidArgument);

    an::DemosaicRequest tooSmall{.method = an::DemosaicMethod::Rcd,
                                 .domain = {.whiteLevel = (std::numeric_limits<double>::denorm_min)()}};
    EXPECT_EQ(an::planDemosaic(tooSmall, rggb).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DemosaicTest, APictureWithNoDetailComesBackWithNoDetail)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 84u;
    constexpr float kValue = 0.375f;
    const std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, kValue);
    std::vector<float> destination;
    for (const MethodCase& method : everyMethod())
    {
        const an::DemosaicPlan plan = planFor(method.method, method.cfa);
        const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
        ASSERT_TRUE(ran.hasValue()) << an::demosaicMethodName(method.method) << ": " << ran.status().message();
        EXPECT_EQ(ran.value().method, method.method);
        EXPECT_EQ(ran.value().run.outputsWritten, static_cast<std::uint64_t>(kWidth) * kHeight);
        const float tolerance = (std::max)(method.constantTolerance * kValue, 1e-7f);
        for (std::size_t index = 0u; index < destination.size(); ++index)
        {
            ASSERT_NEAR(destination[index], kValue, tolerance)
                << an::demosaicMethodName(method.method) << " at sample " << index;
        }
    }
}

TEST(DemosaicTest, TheColourThatWasMeasuredIsTheColourThatIsReported)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 84u;
    std::vector<float> destination;
    for (const MethodCase& method : everyMethod())
    {
        if (method.method == an::DemosaicMethod::MarkesteijnOnePass ||
            method.method == an::DemosaicMethod::MarkesteijnThreePass)
        {
            // This estimate averages several directions of a full colour reconstruction rather than
            // carrying the measurement through, so the measured sample is not preserved exactly.
            continue;
        }
        const std::vector<float> mosaic = smoothMosaic(kWidth, kHeight, method.cfa);
        const an::DemosaicPlan plan = planFor(method.method, method.cfa);
        const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
        ASSERT_TRUE(ran.hasValue()) << an::demosaicMethodName(method.method) << ": " << ran.status().message();
        const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                const std::size_t channel = static_cast<std::size_t>(method.cfa.colorAt(x, y));
                if (method.method == an::DemosaicMethod::Vng4 && method.cfa.colorAt(x, y) == an::CfaColor::Green)
                {
                    // VNG4 reports the mean of its two green phases at a green site; see the case below.
                    continue;
                }
                ASSERT_NEAR(destination[channel * plane + index], mosaic[index], 2e-6f)
                    << an::demosaicMethodName(method.method) << " at " << x << "," << y;
            }
        }
    }
}

TEST(DemosaicTest, Vng4ReportsTheMeanOfItsTwoGreenPhasesAndKeepsRedAndBlue)
{
    // VNG4 estimates both green phases of a four-colour working image and reports their mean, so at a green
    // site it mixes its estimate of the other phase into the measurement. A flat field whose two green
    // phases differ shows it exactly: every interior green is their mean, and red and blue are kept.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    constexpr float kRed = 0.25f;
    constexpr float kRedRowGreen = 0.375f;
    constexpr float kBlueRowGreen = 0.5f;
    constexpr float kBlue = 0.625f;
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        const an::CfaDescriptor cfa = bayer(pattern);
        std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const an::CfaColor color = cfa.colorAt(x, y);
                const bool redRow = cfa.colorAt(x + 1u, y) == an::CfaColor::Red || color == an::CfaColor::Red;
                mosaic[static_cast<std::size_t>(y) * kWidth + x] = color == an::CfaColor::Red ? kRed
                                                                   : color == an::CfaColor::Blue
                                                                       ? kBlue
                                                                       : (redRow ? kRedRowGreen : kBlueRowGreen);
            }
        }
        const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Vng4, cfa);
        std::vector<float> destination;
        const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
        const std::uint32_t reach = plan.reach();
        for (std::uint32_t y = reach; y < kHeight - reach; ++y)
        {
            for (std::uint32_t x = reach; x < kWidth - reach; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                ASSERT_EQ(destination[index], kRed) << an::cfaPatternName(pattern) << " at " << x << "," << y;
                ASSERT_EQ(destination[plane + index], 0.5f * (kRedRowGreen + kBlueRowGreen))
                    << an::cfaPatternName(pattern) << " at " << x << "," << y;
                ASSERT_EQ(destination[2u * plane + index], kBlue)
                    << an::cfaPatternName(pattern) << " at " << x << "," << y;
            }
        }
    }
}

TEST(DemosaicTest, ASmoothPictureIsRecoveredInEveryColourAndNotOnlyWhereItWasMeasured)
{
    // Every other case here checks that an estimate preserves what the sensor recorded, which a
    // structurally wrong reconstruction can also do: most of these methods write the measured colour
    // straight through whatever the neighbourhood arithmetic did. This one checks the two colours
    // that were *not* recorded, against a picture whose values are known everywhere.
    //
    // Two pictures, because they test different things. The neutral one varies in brightness alone,
    // so the colour-difference constancy every one of these methods assumes holds exactly and the
    // reconstruction should be near-perfect - which is what makes its bound tight enough to catch a
    // transposed offset or a mistaken filter phase. The coloured one gives each channel its own
    // gradient, which breaks that assumption deliberately: no method recovers it exactly and the
    // bound is only asking that none of them is structurally wrong.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 192u;
    constexpr std::uint32_t kHeight = 168u;
    constexpr float kTwoPi = 6.28318530718f;
    const auto neutral = [](std::uint32_t x, std::uint32_t y, std::size_t channel) {
        static_cast<void>(channel);
        const float across = static_cast<float>(x) / static_cast<float>(kWidth);
        const float down = static_cast<float>(y) / static_cast<float>(kHeight);
        return 0.45f + 0.20f * std::sin(kTwoPi * across) + 0.10f * std::cos(kTwoPi * down);
    };
    const auto coloured = [](std::uint32_t x, std::uint32_t y, std::size_t channel) {
        const float across = static_cast<float>(x) / static_cast<float>(kWidth);
        const float down = static_cast<float>(y) / static_cast<float>(kHeight);
        if (channel == 0u)
        {
            return 0.45f + 0.20f * std::sin(kTwoPi * across);
        }
        if (channel == 1u)
        {
            return 0.50f + 0.20f * std::cos(kTwoPi * down);
        }
        return 0.40f + 0.15f * std::sin(kTwoPi * 0.5f * (across + down));
    };

    std::vector<float> destination;
    std::vector<double> neutralError;
    for (const auto& picture :
         {std::pair<const char*, float>{"neutral", 0.005f}, std::pair<const char*, float>{"coloured", 0.02f}})
    {
        const bool isNeutral = std::string(picture.first) == "neutral";
        for (const MethodCase& method : everyMethod())
        {
            std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    const std::size_t channel = static_cast<std::size_t>(method.cfa.colorAt(x, y));
                    mosaic[static_cast<std::size_t>(y) * kWidth + x] =
                        isNeutral ? neutral(x, y, channel) : coloured(x, y, channel);
                }
            }
            const an::DemosaicPlan plan = planFor(method.method, method.cfa);
            const an::Result<an::DemosaicResult> ran =
                runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
            ASSERT_TRUE(ran.hasValue()) << an::demosaicMethodName(method.method) << ": " << ran.status().message();

            // The band the shared border rule fills is a three-by-three average by design rather
            // than a reconstruction, so the picture is judged where the method itself wrote it.
            const std::uint32_t border = plan.reach();
            const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
            double worst = 0.0;
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                for (std::uint32_t y = border; y < kHeight - border; ++y)
                {
                    for (std::uint32_t x = border; x < kWidth - border; ++x)
                    {
                        const double produced = destination[channel * plane + static_cast<std::size_t>(y) * kWidth + x];
                        const double expected = isNeutral ? neutral(x, y, channel) : coloured(x, y, channel);
                        worst = (std::max)(worst, std::abs(produced - expected));
                    }
                }
            }
            EXPECT_LT(worst, picture.second) << picture.first << ": " << an::demosaicMethodName(method.method)
                                             << " recovered the picture to " << worst << " at worst";
            if (isNeutral)
            {
                neutralError.push_back(worst);
            }
        }
    }

    // Three passes refine what one pass estimated, so on a picture whose colour differences really
    // are constant the second one has to be at least as close. A refinement that made the answer
    // worse would still satisfy every bound above.
    ASSERT_EQ(neutralError.size(), everyMethod().size());
    constexpr std::size_t onePass = 5u;
    constexpr std::size_t threePass = 6u;
    EXPECT_LE(neutralError[threePass], neutralError[onePass])
        << "three passes recovered the neutral picture to " << neutralError[threePass] << " and one pass to "
        << neutralError[onePass];
}

TEST(DemosaicTest, NoEstimateProducesAValueThatIsNotANumber)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 120u;
    constexpr std::uint32_t kHeight = 108u;
    std::vector<float> destination;
    for (const MethodCase& method : everyMethod())
    {
        Sequence sequence{7u};
        std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
        for (float& sample : mosaic)
        {
            sample = sequence.next();
        }
        const an::DemosaicPlan plan = planFor(method.method, method.cfa);
        const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
        ASSERT_TRUE(ran.hasValue()) << an::demosaicMethodName(method.method) << ": " << ran.status().message();
        for (std::size_t index = 0u; index < destination.size(); ++index)
        {
            ASSERT_TRUE(std::isfinite(destination[index]))
                << an::demosaicMethodName(method.method) << " at sample " << index;
        }
    }
}

TEST(DemosaicTest, TheAnswerDoesNotDependOnHowManyThreadsRan)
{
    const an::EngineContext engine = makeEngine(8u);
    constexpr std::uint32_t kWidth = 150u;
    constexpr std::uint32_t kHeight = 130u;
    std::vector<float> single;
    std::vector<float> many;
    for (const MethodCase& method : everyMethod())
    {
        const std::vector<float> mosaic = smoothMosaic(kWidth, kHeight, method.cfa);
        const an::DemosaicPlan plan = planFor(method.method, method.cfa);
        const an::ExecutionContext narrow = makeExecution(engine, 1u);
        const an::ExecutionContext wide = makeExecution(engine, 7u);
        ASSERT_TRUE(runDemosaic(narrow, mosaic, kWidth, kHeight, plan, single).hasValue());
        ASSERT_TRUE(runDemosaic(wide, mosaic, kWidth, kHeight, plan, many).hasValue());
        ASSERT_EQ(single.size(), many.size());
        for (std::size_t index = 0u; index < single.size(); ++index)
        {
            ASSERT_EQ(single[index], many[index]) << an::demosaicMethodName(method.method) << " at " << index;
        }
    }
}

TEST(DemosaicTest, ADemosaicIsRefusedForEveryReasonSeparately)
{
    const an::EngineContext engine = makeEngine(2u);
    const an::ExecutionContext execution = makeExecution(engine, 2u);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Bilinear, rggb);
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    std::vector<float> destination(mosaic.size() * 3u, 0.0f);
    const an::ImageLayout mosaicLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    const an::ImageLayout colourLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);

    EXPECT_EQ(an::demosaicImage(an::ExecutionContext{}, an::SampleType::Float32, mosaic.data(), mosaicLayout,
                                destination.data(), colourLayout, plan)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, nullptr, mosaicLayout, destination.data(),
                                colourLayout, plan)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), colourLayout, destination.data(),
                                colourLayout, plan)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, mosaic.data(),
                                colourLayout, plan)
                  .status()
                  .code(),
              an::ErrorCode::Aliasing);

    std::vector<std::byte> misalignedSource(static_cast<std::size_t>(mosaicLayout.totalBytes()) + 1u);
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, misalignedSource.data() + 1u, mosaicLayout,
                                destination.data(), colourLayout, plan)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    std::vector<std::byte> misalignedDestination(static_cast<std::size_t>(colourLayout.totalBytes()) + 1u);
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout,
                                misalignedDestination.data() + 1u, colourLayout, plan)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    an::DemosaicSelection unwanted;
    std::vector<std::uint8_t> maskBits(an::packedMaskRowBytes(kWidth) * kHeight, 0xFFu);
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::optional<an::ValidityMaskView<std::uint8_t>> mask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBits, *maskExtent);
    ASSERT_TRUE(mask.has_value());
    unwanted.produced = &mask.value();
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, destination.data(),
                                colourLayout, plan, unwanted)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    an::DemosaicSelection missing;
    missing.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, destination.data(),
                                colourLayout, plan, missing)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    std::vector<std::uint8_t> sourceMaskBits(an::packedMaskRowBytes(kWidth + 1u) * kHeight, 0xFFu);
    const std::optional<an::ImageExtent> sourceMaskExtent =
        an::ImageExtent::create(kWidth + 1u, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(sourceMaskExtent.has_value());
    std::optional<an::ValidityMaskView<std::uint8_t>> sourceMaskWritable =
        an::ValidityMaskView<std::uint8_t>::createTight(sourceMaskBits, *sourceMaskExtent);
    ASSERT_TRUE(sourceMaskWritable.has_value());
    const an::ValidityMaskView<const std::uint8_t> sourceMask = sourceMaskWritable->constView();

    an::DemosaicSelection wrongExtent;
    wrongExtent.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}).value();
    wrongExtent.mask = &sourceMask;
    wrongExtent.produced = &mask.value();
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, destination.data(),
                                colourLayout, plan, wrongExtent)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    an::DemosaicSelection maskWithValueRule;
    maskWithValueRule.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();
    maskWithValueRule.mask = &sourceMask;
    maskWithValueRule.produced = &mask.value();
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, destination.data(),
                                colourLayout, plan, maskWithValueRule)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    const an::ValidityMaskView<const std::uint8_t> aliasedMask = mask->constView();
    an::DemosaicSelection aliasedMasks;
    aliasedMasks.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}).value();
    aliasedMasks.mask = &aliasedMask;
    aliasedMasks.produced = &mask.value();
    EXPECT_EQ(an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(), mosaicLayout, destination.data(),
                                colourLayout, plan, aliasedMasks)
                  .status()
                  .code(),
              an::ErrorCode::Aliasing);
}

TEST(DemosaicTest, AMeaninglessSampleIsMarkedThroughoutTheFootprintItReached)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    std::vector<float> mosaic = smoothMosaic(kWidth, kHeight, rggb);
    constexpr std::uint32_t kBrokenX = 48u;
    constexpr std::uint32_t kBrokenY = 50u;
    mosaic[static_cast<std::size_t>(kBrokenY) * kWidth + kBrokenX] = std::numeric_limits<float>::quiet_NaN();

    std::vector<std::uint8_t> maskBits(an::packedMaskRowBytes(kWidth) * kHeight, 0u);
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::optional<an::ValidityMaskView<std::uint8_t>> mask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBits, *maskExtent);
    ASSERT_TRUE(mask.has_value());

    an::DemosaicSelection selection;
    selection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();
    selection.produced = &mask.value();

    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Fast, rggb);
    std::vector<float> destination;
    const an::Result<an::DemosaicResult> ran =
        runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination, selection);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran.value().outputValidity.rule(), an::ValidityRule::ExplicitMask);

    const std::uint32_t reach = plan.reach();
    EXPECT_EQ(mask->test(0u, kBrokenX, kBrokenY), std::optional<bool>{false});
    EXPECT_EQ(mask->test(0u, kBrokenX + reach, kBrokenY), std::optional<bool>{false});
    EXPECT_EQ(mask->test(0u, kBrokenX + reach + 1u, kBrokenY), std::optional<bool>{true});
    EXPECT_EQ(mask->test(0u, kBrokenX, kBrokenY - reach), std::optional<bool>{false});
    EXPECT_EQ(mask->test(0u, kBrokenX, kBrokenY - reach - 1u), std::optional<bool>{true});
    EXPECT_EQ(ran.value().run.selection.total - ran.value().run.selection.valid,
              static_cast<std::uint64_t>(2u * reach + 1u) * (2u * reach + 1u));
}

TEST(DemosaicTest, FootprintValidityUsesBoundedScratchInsteadOfFullFrameWorkingPlanes)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Bilinear, rggb);

    const auto runWithMask = [&](std::uint32_t width, std::uint32_t height) {
        const std::vector<float> mosaic(static_cast<std::size_t>(width) * height, 0.25f);
        std::vector<float> destination;
        std::vector<std::uint8_t> maskBits(an::packedMaskRowBytes(width) * height, 0u);
        const std::optional<an::ImageExtent> maskExtent =
            an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
        EXPECT_TRUE(maskExtent.has_value());
        std::optional<an::ValidityMaskView<std::uint8_t>> mask =
            an::ValidityMaskView<std::uint8_t>::createTight(maskBits, *maskExtent);
        EXPECT_TRUE(mask.has_value());

        an::DemosaicSelection selection;
        selection.validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly}).value();
        selection.produced = &mask.value();
        return runDemosaic(execution, mosaic, width, height, plan, destination, selection);
    };

    const an::Result<an::DemosaicResult> small = runWithMask(512u, 256u);
    const an::Result<an::DemosaicResult> large = runWithMask(1024u, 512u);
    ASSERT_TRUE(small.hasValue()) << small.status().message();
    ASSERT_TRUE(large.hasValue()) << large.status().message();
    EXPECT_EQ(small.value().workingBytes, large.value().workingBytes);
    EXPECT_LT(small.value().workingBytes, 512u * 256u);
}

TEST(DemosaicTest, AnEstimateStopsWhenItIsAskedTo)
{
    const an::EngineContext engine = makeEngine(4u);
    std::stop_source stop;
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.stopToken = stop.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    std::vector<float> destination;
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Rcd, rggb);
    EXPECT_EQ(runDemosaic(execution.value(), mosaic, kWidth, kHeight, plan, destination).status().code(),
              an::ErrorCode::Cancelled);
}

TEST(DemosaicTest, ARunReportsMonotonicProgressAndMachineReadableTrace)
{
    const an::EngineContext engine = makeEngine(4u);
    DemosaicTelemetrySink sink;
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.backend = an::ComputeBackend::Reference;
    options.progressSink = &sink;
    options.traceSink = &sink;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    constexpr std::uint32_t kWidth = 192u;
    constexpr std::uint32_t kHeight = 160u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic = smoothMosaic(kWidth, kHeight, rggb);
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Fast, rggb);
    std::vector<float> destination;
    const an::Result<an::DemosaicResult> ran = runDemosaic(*execution, mosaic, kWidth, kHeight, plan, destination);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    EXPECT_TRUE(sink.progressIsMonotonic());
    EXPECT_GE(sink.progressCount(), 5u);
    EXPECT_EQ(sink.lastCompleted(), sink.lastTotal());
    EXPECT_EQ(sink.spanBegins(), 1u);
    EXPECT_EQ(sink.spanEnds(), 1u);
    EXPECT_DOUBLE_EQ(sink.tiles(), static_cast<double>(ran->tileCount));
    EXPECT_DOUBLE_EQ(sink.workingBytes(), static_cast<double>(ran->workingBytes));
    EXPECT_TRUE(sink.sawEveryResultCounter());
}

TEST(DemosaicTest, AProgressSinkCanCancelInteriorTileWork)
{
    const an::EngineContext engine = makeEngine(4u);
    std::stop_source stop;
    DemosaicTelemetrySink sink(&stop);
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.backend = an::ComputeBackend::Reference;
    options.stopToken = stop.get_token();
    options.progressSink = &sink;
    options.traceSink = &sink;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    constexpr std::uint32_t kWidth = 512u;
    constexpr std::uint32_t kHeight = 384u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic = smoothMosaic(kWidth, kHeight, rggb);
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Rcd, rggb);
    std::vector<float> destination;
    const an::Result<an::DemosaicResult> ran = runDemosaic(*execution, mosaic, kWidth, kHeight, plan, destination);

    EXPECT_EQ(ran.status().code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(sink.sawInterior());
    EXPECT_TRUE(sink.progressIsMonotonic());
    EXPECT_EQ(sink.spanBegins(), 1u);
    EXPECT_EQ(sink.spanEnds(), 1u);
    const std::size_t untouched = static_cast<std::size_t>(std::count(destination.begin(), destination.end(), -1.0f));
    EXPECT_GT(untouched, 0u);
    EXPECT_LT(untouched, destination.size());
}

TEST(DemosaicTest, APictureTooSmallForAnyNeighbourhoodIsAllBorder)
{
    const an::EngineContext engine = makeEngine(2u);
    const an::ExecutionContext execution = makeExecution(engine, 2u);
    constexpr std::uint32_t kWidth = 6u;
    constexpr std::uint32_t kHeight = 6u;
    constexpr float kValue = 0.5f;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, kValue);
    std::vector<float> destination;
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Amaze, rggb);
    const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran.value().tileCount, 0u);
    for (const float sample : destination)
    {
        EXPECT_FLOAT_EQ(sample, kValue);
    }
}

TEST(DemosaicTest, ARepresentationOtherThanFloatIsGatheredAndScatteredRatherThanRefused)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    constexpr std::uint16_t kValue = 12345u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const std::vector<std::uint16_t> mosaic(static_cast<std::size_t>(kWidth) * kHeight, kValue);
    std::vector<std::uint16_t> destination(mosaic.size() * 3u, 0u);
    const an::DemosaicRequest request{.method = an::DemosaicMethod::Bilinear, .domain = {.whiteLevel = 65535.0}};
    const an::Result<an::DemosaicPlan> plan = an::planDemosaic(request, rggb);
    ASSERT_TRUE(plan.hasValue());
    const an::Result<an::DemosaicResult> ran = an::demosaicImage(
        execution, an::SampleType::UInt16, mosaic.data(), planarLayout(an::SampleType::UInt16, kWidth, kHeight, 1u),
        destination.data(), planarLayout(an::SampleType::UInt16, kWidth, kHeight, 3u), plan.value());
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_FALSE(ran.value().readInPlace);
    EXPECT_FALSE(ran.value().wroteInPlace);
    EXPECT_GT(ran.value().workingBytes, 0u);
    EXPECT_EQ(ran.value().conversion.samplesConverted, mosaic.size() * 3u);
    EXPECT_EQ(ran.value().conversion.nonFiniteInputs, 0u);
    EXPECT_EQ(ran.value().conversion.saturatedSamples, 0u);
    for (const std::uint16_t sample : destination)
    {
        EXPECT_EQ(sample, kValue);
    }
}

TEST(DemosaicTest, TheReferenceAndTheDispatchedImplementationAgree)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    const an::ExecutionContext compiled = makeExecution(engine, 3u, an::ComputeBackend::Cpu);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 112u;
    std::vector<float> scalar;
    std::vector<float> dispatched;
    for (const MethodCase& method : everyMethod())
    {
        const std::vector<float> mosaic = smoothMosaic(kWidth, kHeight, method.cfa);
        const an::DemosaicPlan plan = planFor(method.method, method.cfa);
        const an::Result<an::DemosaicResult> first = runDemosaic(reference, mosaic, kWidth, kHeight, plan, scalar);
        ASSERT_TRUE(first.hasValue()) << first.status().message();
        EXPECT_EQ(first.value().backend.backend, an::ComputeBackend::Reference);
        const an::Result<an::DemosaicResult> second = runDemosaic(compiled, mosaic, kWidth, kHeight, plan, dispatched);
        ASSERT_TRUE(second.hasValue()) << second.status().message();
        const bool hasCompiledKernel =
            method.method != an::DemosaicMethod::Lmmse && method.method != an::DemosaicMethod::Vng4;
        EXPECT_EQ(second.value().backend.backend,
                  hasCompiledKernel ? an::ComputeBackend::Cpu : an::ComputeBackend::Reference);
        EXPECT_EQ(second.value().backend.instructionSet == an::InstructionSet::Scalar, !hasCompiledKernel);
        ASSERT_EQ(scalar.size(), dispatched.size());
        for (std::size_t index = 0u; index < scalar.size(); ++index)
        {
            ASSERT_NEAR(scalar[index], dispatched[index], 1e-5f)
                << an::demosaicMethodName(method.method) << " at " << index;
        }
    }
}

TEST(DemosaicTest, DeferredBayerMethodsAreIndependentEstimatesWithHonestNames)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 112u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    Sequence sequence{20260902u};
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const float edge = x < kWidth / 2u ? 0.15f : 0.75f;
            mosaic[static_cast<std::size_t>(y) * kWidth + x] =
                (std::min)(1.0f, (std::max)(0.0f, edge + 0.08f * (sequence.next() - 0.5f)));
        }
    }

    constexpr std::array<an::DemosaicMethod, 3u> kMethods = {an::DemosaicMethod::Lmmse, an::DemosaicMethod::Vng4,
                                                             an::DemosaicMethod::LuminanceAhd};
    constexpr std::array<const char*, 3u> kNames = {"Lmmse", "Vng4", "LuminanceAhd"};
    std::array<std::vector<float>, 3u> outputs;
    for (std::size_t index = 0u; index < kMethods.size(); ++index)
    {
        EXPECT_STREQ(an::demosaicMethodName(kMethods[index]), kNames[index]);
        const an::DemosaicPlan plan = planFor(kMethods[index], cfa);
        const an::Result<an::DemosaicResult> ran =
            runDemosaic(execution, mosaic, kWidth, kHeight, plan, outputs[index]);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(ran->backend.backend, an::ComputeBackend::Reference);
    }

    const std::uint32_t border = an::demosaicMethodReach(an::DemosaicMethod::LuminanceAhd);
    const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t left = 0u; left < outputs.size(); ++left)
    {
        for (std::size_t right = left + 1u; right < outputs.size(); ++right)
        {
            bool differs = false;
            for (std::size_t channel = 0u; channel < 3u && !differs; ++channel)
            {
                for (std::uint32_t y = border; y < kHeight - border && !differs; ++y)
                {
                    for (std::uint32_t x = border; x < kWidth - border; ++x)
                    {
                        const std::size_t sample = channel * plane + static_cast<std::size_t>(y) * kWidth + x;
                        if (outputs[left][sample] != outputs[right][sample])
                        {
                            differs = true;
                            break;
                        }
                    }
                }
            }
            EXPECT_TRUE(differs) << kNames[left] << " and " << kNames[right] << " are the same estimate";
        }
    }
}

TEST(DemosaicTest, DeferredBayerMethodsUseTheWhiteLevelWithoutAssumingSixteenBitCodes)
{
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 112u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    constexpr std::array<an::DemosaicMethod, 3u> kMethods = {an::DemosaicMethod::Lmmse, an::DemosaicMethod::Vng4,
                                                             an::DemosaicMethod::LuminanceAhd};
    std::vector<float> destination;
    for (const float whiteLevel : {1.0f, 65535.0f, 1.0e30f})
    {
        const std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, whiteLevel);
        for (const an::DemosaicMethod method : kMethods)
        {
            an::DemosaicRequest request{.method = method};
            request.domain.whiteLevel = whiteLevel;
            const an::Result<an::DemosaicPlan> plan = an::planDemosaic(request, cfa);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            const an::Result<an::DemosaicResult> ran =
                runDemosaic(execution, mosaic, kWidth, kHeight, *plan, destination);
            ASSERT_TRUE(ran.hasValue()) << ran.status().message();
            for (const float sample : destination)
            {
                ASSERT_NEAR(sample, whiteLevel, whiteLevel * 1.0e-5f)
                    << an::demosaicMethodName(method) << " at white level " << whiteLevel;
            }
        }
    }
}

TEST(DemosaicTest, FloatStructureBelowOneSixteenBitCodeIsNeverQuantizedAway)
{
    const an::EngineContext engine = makeEngine(3u);
    const an::ExecutionContext execution = makeExecution(engine, 2u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 56u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < mosaic.size(); ++index)
    {
        mosaic[index] = 0.25f + static_cast<float>(index % 11u) * 1.0e-7f;
    }
    const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Lmmse, cfa);
    std::vector<float> destination;
    const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    constexpr std::uint32_t kX = 24u;
    constexpr std::uint32_t kY = 24u;
    const std::size_t pixel = static_cast<std::size_t>(kY) * kWidth + kX;
    const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
    const std::size_t measuredChannel = static_cast<std::size_t>(cfa.colorAt(kX, kY));
    const float narrowedToSixteenBits = std::round(mosaic[pixel] * 65535.0f) / 65535.0f;
    EXPECT_NE(mosaic[pixel], narrowedToSixteenBits);
    EXPECT_FLOAT_EQ(destination[measuredChannel * plane + pixel], mosaic[pixel]);
    EXPECT_NE(destination[measuredChannel * plane + pixel], narrowedToSixteenBits);
}

TEST(DemosaicTest, ScalarFirstMethodsRefuseAForcedCpuBackendWhenFallbackIsForbidden)
{
    const an::EngineContext engine = makeEngine(3u);
    an::ExecutionOptions options{};
    options.maxConcurrency = 2u;
    options.backend = an::ComputeBackend::Cpu;
    options.backendFallback = an::BackendFallback::Forbidden;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 56u;
    const an::CfaDescriptor cfa = bayer(an::CfaPattern::Rggb);
    const std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    // The luminance-only AHD estimate has a compiled body and satisfies the request; the two left here do not.
    constexpr std::array<an::DemosaicMethod, 2u> kMethods = {an::DemosaicMethod::Lmmse, an::DemosaicMethod::Vng4};
    for (const an::DemosaicMethod method : kMethods)
    {
        const an::DemosaicPlan plan = planFor(method, cfa);
        std::vector<float> destination;
        const an::Result<an::DemosaicResult> ran = runDemosaic(*execution, mosaic, kWidth, kHeight, plan, destination);
        EXPECT_FALSE(ran.hasValue()) << an::demosaicMethodName(method);
        EXPECT_EQ(ran.status().code(), an::ErrorCode::UnsupportedBackend) << an::demosaicMethodName(method);
    }
}

TEST(DemosaicTest, WriteBackCountsAndCanRejectANonFiniteEstimate)
{
    const an::EngineContext engine = makeEngine(3u);
    const an::ExecutionContext execution = makeExecution(engine, 2u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 32u;
    constexpr std::uint32_t kHeight = 32u;
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    mosaic[static_cast<std::size_t>(kHeight / 2u) * kWidth + kWidth / 2u] = std::numeric_limits<float>::infinity();

    an::DemosaicRequest mappedRequest{.method = an::DemosaicMethod::Bilinear};
    const an::Result<an::DemosaicPlan> mappedPlan = an::planDemosaic(mappedRequest, rggb);
    ASSERT_TRUE(mappedPlan.hasValue());
    std::vector<float> destination;
    const an::Result<an::DemosaicResult> mapped =
        runDemosaic(execution, mosaic, kWidth, kHeight, *mappedPlan, destination);
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_EQ(mapped.value().conversion.samplesConverted, mosaic.size() * 3u);
    EXPECT_GT(mapped.value().conversion.nonFiniteInputs, 0u);

    an::DemosaicRequest rejectingRequest{.method = an::DemosaicMethod::Bilinear};
    rejectingRequest.output.writeBack.nonFinite = an::NonFinitePolicy::Reject;
    const an::Result<an::DemosaicPlan> rejectingPlan = an::planDemosaic(rejectingRequest, rggb);
    ASSERT_TRUE(rejectingPlan.hasValue());
    EXPECT_EQ(runDemosaic(execution, mosaic, kWidth, kHeight, *rejectingPlan, destination).status().code(),
              an::ErrorCode::InvalidData);
}

TEST(DemosaicTest, WriteBackCountsAndCanRejectAnOutOfRangeEstimate)
{
    const an::EngineContext engine = makeEngine(4u);
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.backend = an::ComputeBackend::Reference;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 84u;
    std::vector<std::uint8_t> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            mosaic[static_cast<std::size_t>(y) * kWidth + x] = ((x * 13u + y * 7u + (x ^ y)) % 5u) < 2u ? 0u : 255u;
        }
    }
    const an::CfaDescriptor rggb = bayer(an::CfaPattern::Rggb);
    const an::DemosaicRequest saturatingRequest{.method = an::DemosaicMethod::Fast, .domain = {.whiteLevel = 255.0}};
    const an::Result<an::DemosaicPlan> saturatingPlan = an::planDemosaic(saturatingRequest, rggb);
    ASSERT_TRUE(saturatingPlan.hasValue());
    std::vector<std::uint8_t> destination(mosaic.size() * 3u, 0u);
    const an::Result<an::DemosaicResult> saturated = an::demosaicImage(
        *execution, an::SampleType::UInt8, mosaic.data(), planarLayout(an::SampleType::UInt8, kWidth, kHeight, 1u),
        destination.data(), planarLayout(an::SampleType::UInt8, kWidth, kHeight, 3u), *saturatingPlan);
    ASSERT_TRUE(saturated.hasValue()) << saturated.status().message();
    ASSERT_GT(saturated->conversion.saturatedSamples, 0u);

    an::DemosaicRequest rejectingRequest = saturatingRequest;
    rejectingRequest.output.writeBack.range = an::RangePolicy::Reject;
    const an::Result<an::DemosaicPlan> rejectingPlan = an::planDemosaic(rejectingRequest, rggb);
    ASSERT_TRUE(rejectingPlan.hasValue());
    EXPECT_EQ(an::demosaicImage(*execution, an::SampleType::UInt8, mosaic.data(),
                                planarLayout(an::SampleType::UInt8, kWidth, kHeight, 1u), destination.data(),
                                planarLayout(an::SampleType::UInt8, kWidth, kHeight, 3u), *rejectingPlan)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
}

/// A digest of a picture's stored bits, so a retained arithmetic program can be pinned exactly.
///
/// @param samples the samples.
/// @return the FNV-1a hash of their bit patterns.
[[nodiscard]] std::uint64_t bitDigest(const std::vector<float>& samples) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const float sample : samples)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(sample);
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
        {
            hash ^= (bits >> shift) & 0xFFu;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

TEST(DemosaicTest, Vng4KeepsItsRetainedGradientProgramBitForBit)
{
    // The variable-number-of-gradients estimate is the retained 64-term, eight-direction program,
    // and the shape of its evaluation is free to change only while the bits it produces do not.
    // These digests were taken from the program before its per-phase term tables existed, on a
    // noisy picture over every Bayer arrangement, so a restructuring that alters one rounding fails
    // here.
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 4u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 80u;
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    Sequence sequence{0x5EEDu};
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const float ramp = 0.2f + 0.5f * static_cast<float>(x + y) / static_cast<float>(kWidth + kHeight);
            mosaic[static_cast<std::size_t>(y) * kWidth + x] = ramp + 0.1f * (sequence.next() - 0.5f);
        }
    }
    struct PhaseCase
    {
        an::CfaPattern pattern;
        std::uint32_t originX;
        std::uint32_t originY;
        std::uint64_t digest;
    };
    constexpr std::array<PhaseCase, 5u> kCases{PhaseCase{an::CfaPattern::Rggb, 0u, 0u, 0xC99497C9AA995B0Dull},
                                               PhaseCase{an::CfaPattern::Grbg, 0u, 0u, 0x2DFC3E63EBE28B1Eull},
                                               PhaseCase{an::CfaPattern::Gbrg, 0u, 0u, 0x39FEE383FEBA8D02ull},
                                               PhaseCase{an::CfaPattern::Bggr, 0u, 0u, 0xA59B86219A0B9A21ull},
                                               PhaseCase{an::CfaPattern::Rggb, 1u, 1u, 0xA59B86219A0B9A21ull}};
    for (const PhaseCase& phase : kCases)
    {
        const std::optional<an::CfaDescriptor> cfa =
            an::CfaDescriptor::create(phase.pattern, phase.originX, phase.originY);
        ASSERT_TRUE(cfa.has_value());
        const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Vng4, *cfa);
        std::vector<float> destination;
        const an::Result<an::DemosaicResult> ran = runDemosaic(execution, mosaic, kWidth, kHeight, plan, destination);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(ran->backend.backend, an::ComputeBackend::Reference);
        EXPECT_TRUE(
            std::all_of(destination.begin(), destination.end(), [](float sample) { return std::isfinite(sample); }));
        EXPECT_EQ(bitDigest(destination), phase.digest)
            << "pattern " << static_cast<unsigned>(phase.pattern) << " origin " << phase.originX << "," << phase.originY
            << " digest 0x" << std::hex << bitDigest(destination);
    }
}

/// A mosaic for the aliasing-minimised estimate that reaches every branch it takes: noise on a dark
/// floor, stripes at the mosaic's own Nyquist frequency in both directions, isolated samples above the
/// highlight threshold, darker blocks where the colour ratios swing, and negative zeros in those blocks,
/// which the medians and the bounds have to order as the reference orders them. Every other value is an
/// integer number of 1/65536ths, divided once, so it is the same float on every compiler.
///
/// @param x the column.
/// @param y the row.
/// @return the sample.
[[nodiscard]] float amazeTexture(std::uint32_t x, std::uint32_t y)
{
    std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u);
    hash ^= hash >> 13u;
    hash *= 0x5BD1E995u;
    hash ^= hash >> 15u;
    std::uint32_t code = 9830u + (hash & 0x1FFFu);
    if (x % 64u < 16u && x % 2u == 0u)
    {
        code += 19661u;
    }
    if (y % 48u < 8u && y % 2u == 0u)
    {
        code += 16384u;
    }
    if (hash % 97u == 0u)
    {
        code = 62259u;
    }
    if ((x / 32u + y / 32u) % 5u == 0u)
    {
        if (hash % 5u == 0u)
        {
            return -0.0f;
        }
        code /= 4u;
    }
    return static_cast<float>(code) / 65536.0f;
}

TEST(DemosaicTest, AmazeKeepsItsProgramBitForBit)
{
    // The aliasing-minimised estimate is free to change how it evaluates its passes only while the bits it
    // produces do not. These digests were taken from the estimate before any of its passes was restructured,
    // on pictures whose last tile has an even and an odd padded width, so a change of addressing, of pass
    // order or of one rounding fails here, on the reference and on the dispatched implementation alike.
    const an::EngineContext engine = makeEngine(4u);
    struct SizeCase
    {
        std::uint32_t width;
        std::uint32_t height;
        an::CfaPattern pattern;
        std::uint64_t digest;
    };
    constexpr std::array<SizeCase, 3u> kCases{SizeCase{263u, 71u, an::CfaPattern::Rggb, 0xD075CFAB8FC81D0Dull},
                                              SizeCase{256u, 130u, an::CfaPattern::Gbrg, 0x9215BD361F90F01Bull},
                                              SizeCase{331u, 97u, an::CfaPattern::Grbg, 0xF597B0429FDE87E3ull}};
    for (const an::ComputeBackend backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        const an::ExecutionContext execution = makeExecution(engine, 4u, backend);
        for (const SizeCase& size : kCases)
        {
            std::vector<float> mosaic(static_cast<std::size_t>(size.width) * size.height, 0.0f);
            for (std::uint32_t y = 0u; y < size.height; ++y)
            {
                for (std::uint32_t x = 0u; x < size.width; ++x)
                {
                    mosaic[static_cast<std::size_t>(y) * size.width + x] = amazeTexture(x, y);
                }
            }
            const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Amaze, bayer(size.pattern));
            std::vector<float> destination;
            const an::Result<an::DemosaicResult> ran =
                runDemosaic(execution, mosaic, size.width, size.height, plan, destination);
            ASSERT_TRUE(ran.hasValue()) << ran.status().message();
            EXPECT_EQ(bitDigest(destination), size.digest)
                << size.width << " x " << size.height << " backend " << static_cast<unsigned>(backend) << " digest 0x"
                << std::hex << bitDigest(destination);
        }
    }
}

TEST(DemosaicTest, RcdKeepsItsProgramBitForBit)
{
    // The ratio-corrected estimate is free to change how its passes are compiled only while the bits it
    // produces do not. These digests were taken from the estimate while every pass the compiler could not
    // prove independent still ran scalar, on pictures of one tile and of several whose last tiles are
    // partial in both directions, over every Bayer arrangement, on the reference and on the dispatched
    // implementation alike.
    const an::EngineContext engine = makeEngine(4u);
    struct SizeCase
    {
        std::uint32_t width;
        std::uint32_t height;
        an::CfaPattern pattern;
        std::uint64_t digest;
    };
    constexpr std::array<SizeCase, 4u> kCases{SizeCase{97u, 71u, an::CfaPattern::Rggb, 0x8632AA999C8ACCE1ull},
                                              SizeCase{256u, 130u, an::CfaPattern::Gbrg, 0x2E619048B081CE74ull},
                                              SizeCase{331u, 197u, an::CfaPattern::Grbg, 0x61F4B02347F855A3ull},
                                              SizeCase{190u, 363u, an::CfaPattern::Bggr, 0xBC2F833CE7F576E0ull}};
    for (const an::ComputeBackend backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        const an::ExecutionContext execution = makeExecution(engine, 4u, backend);
        for (const SizeCase& size : kCases)
        {
            std::vector<float> mosaic(static_cast<std::size_t>(size.width) * size.height, 0.0f);
            for (std::uint32_t y = 0u; y < size.height; ++y)
            {
                for (std::uint32_t x = 0u; x < size.width; ++x)
                {
                    mosaic[static_cast<std::size_t>(y) * size.width + x] = amazeTexture(x, y);
                }
            }
            const an::DemosaicPlan plan = planFor(an::DemosaicMethod::Rcd, bayer(size.pattern));
            std::vector<float> destination;
            const an::Result<an::DemosaicResult> ran =
                runDemosaic(execution, mosaic, size.width, size.height, plan, destination);
            ASSERT_TRUE(ran.hasValue()) << ran.status().message();
            EXPECT_EQ(bitDigest(destination), size.digest)
                << size.width << " x " << size.height << " backend " << static_cast<unsigned>(backend) << " digest 0x"
                << std::hex << bitDigest(destination);
        }
    }
}
} // namespace
