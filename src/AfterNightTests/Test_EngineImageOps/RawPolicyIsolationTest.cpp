// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The RAW white-balance policy belongs to the read, never to the process.
//
// One record of RAW options for the whole process, rewritten by a workflow for the length of its run, would balance a
// picture the viewer opened meanwhile the workflow's way. Here a full-colour
// sensor's balance is its adapter's configuration, and a read that wants another names a registry built with it; a
// mosaic's decoder returns the camera's codes, and its balance is a step the operation takes under its own execution.
// Each case runs a viewer's policy and a workflow's at once in one engine context and holds each answer to the one
// that policy gives alone - for a full-colour frame, the one an engine context built with that policy gives.

#include "SyntheticDng.h"

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using Bytes = std::vector<std::byte>;

/// A registry holding one camera RAW adapter configured with this full-colour balance, or the test fails.
[[nodiscard]] std::shared_ptr<const an::CodecRegistry> rawRegistry(an::RawFullColorWhiteBalance balance)
{
    an::Result<std::shared_ptr<const an::ImageCodec>> codec =
        an::createRawCodec({.fullColorWhiteBalance = balance, .fullColorMultipliers = {2.0, 1.0, 1.5}});
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    if (!codec.hasValue())
    {
        return nullptr;
    }
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> codecs{*codec};
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::CodecRegistry::create(codecs);
    EXPECT_TRUE(registry.hasValue()) << registry.status().message();
    return registry.hasValue() ? *registry : nullptr;
}

/// An engine context of two workers over this registry, and this budget when one is given.
[[nodiscard]] an::EngineContext engineOf(std::shared_ptr<const an::CodecRegistry> registry, std::uint64_t budget = 0u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.codecRegistry = std::move(registry);
    if (budget != 0u)
    {
        options.limits.memoryBudgetBytes = budget;
    }
    an::Result<an::EngineContext> engine = an::EngineContext::create(options);
    EXPECT_TRUE(engine.hasValue()) << engine.status().message();
    return engine.hasValue() ? std::move(*engine) : an::EngineContext{};
}

/// An execution admitted one worker.
[[nodiscard]] an::Result<an::ExecutionContext> oneWorker(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    return an::ExecutionContext::create(engine, options);
}

/// A decoded frame's samples, or nothing with the reason in `error`.
[[nodiscard]] std::optional<Bytes> samplesOf(const an::Result<an::CodecDecodeResult>& decoded, std::string& error)
{
    if (!decoded.hasValue())
    {
        error = decoded.status().message();
        return std::nullopt;
    }
    const std::span<const std::byte> bytes = decoded->frame.bytes();
    return Bytes(bytes.begin(), bytes.end());
}

/// What a full-colour frame reads as through a registry, in an execution of one worker.
[[nodiscard]] std::optional<Bytes> readThrough(const an::EngineContext& engine, const an::CodecRegistry& registry,
                                               const std::string& path, std::string& error)
{
    an::Result<an::ExecutionContext> execution = oneWorker(engine);
    if (!execution.hasValue())
    {
        error = execution.status().message();
        return std::nullopt;
    }
    return samplesOf(an::decodeImage(engine, *execution, registry, {.path = path}), error);
}

/// What a full-colour frame reads as in an engine context built with this balance: the one-context-per-policy route
/// the explicit registry replaces.
[[nodiscard]] Bytes readWithOwnContext(an::RawFullColorWhiteBalance balance, const std::string& path)
{
    const an::EngineContext engine = engineOf(rawRegistry(balance));
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, {});
    EXPECT_TRUE(execution.hasValue());
    std::string error;
    const std::optional<Bytes> samples = samplesOf(an::decodeImage(engine, *execution, {.path = path}), error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples.value_or(Bytes{});
}

/// A decoded mosaic and the camera record its balance resolves against.
struct Mosaic
{
    an::CodecDecodeResult decoded;
    an::RawFrameDescriptor descriptor;
    an::ImageLayout layout;
    an::RawCameraMetadata camera;
};

/// A mosaic balanced the way a mode asks, under an execution of one worker.
[[nodiscard]] std::optional<std::vector<std::uint16_t>> balance(const an::EngineContext& engine, const Mosaic& mosaic,
                                                                an::CfaWhiteBalanceMode mode, std::string& error)
{
    an::Result<an::ExecutionContext> execution = oneWorker(engine);
    if (!execution.hasValue())
    {
        error = execution.status().message();
        return std::nullopt;
    }
    const an::Result<an::CfaWhiteBalance> resolved = an::resolveCfaWhiteBalance(mosaic.camera, {.mode = mode});
    if (!resolved.hasValue())
    {
        error = resolved.status().message();
        return std::nullopt;
    }
    const an::ImageLayout destination = *an::ImageLayout::create({.extent = mosaic.layout.extent(),
                                                                  .elementSizeBytes = 2u,
                                                                  .samplesPerPixel = 1u,
                                                                  .planeCount = 1u,
                                                                  .rowAlignmentBytes = 1u},
                                                                 engine.limits());
    std::vector<std::uint16_t> balanced(static_cast<std::size_t>(mosaic.layout.extent().width()) *
                                        mosaic.layout.extent().height());
    const an::Result<an::CfaChannelResult> applied =
        an::applyCfaChannelGains(*execution, an::SampleType::UInt16, mosaic.decoded.frame.bytes().data(), mosaic.layout,
                                 mosaic.descriptor.cfa(), balanced.data(), destination, resolved->multipliers());
    if (!applied.hasValue())
    {
        error = applied.status().message();
        return std::nullopt;
    }
    return balanced;
}

/// Runs a viewer's and a workflow's work at once, each `rounds` times, both released together.
template <typename ViewerT, typename WorkflowT> void atOnce(int rounds, ViewerT viewer, WorkflowT workflow)
{
    std::latch start(2);
    std::thread other([&] {
        start.arrive_and_wait();
        for (int round = 0; round < rounds; ++round)
        {
            workflow(round);
        }
    });
    start.arrive_and_wait();
    for (int round = 0; round < rounds; ++round)
    {
        viewer(round);
    }
    other.join();
}

/// A camera RAW adapter whose decode waits until the test lets it go, so a read can be held in flight.
class HeldCodec final : public an::ImageCodec
{
public:
    explicit HeldCodec(std::shared_ptr<const an::ImageCodec> inner) : m_inner(std::move(inner)) {}

    an::CodecCapabilities capabilities() const override
    {
        return m_inner->capabilities();
    }

    an::Result<an::CodecImageInfo> probe(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                         const an::CodecReadRequest& request) const override
    {
        return m_inner->probe(engine, execution, request);
    }

    an::Result<an::CodecDecodeResult> decode(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                             const an::CodecReadRequest& request) const override
    {
        entered.set_value();
        // Bounded, so that a read that never arrives here fails the case rather than hanging it.
        if (released.wait_for(std::chrono::seconds{60}) != std::future_status::ready)
        {
            return an::Status::failure(an::ErrorCode::DeadlineExceeded);
        }
        return m_inner->decode(engine, execution, request);
    }

    /// Set when the read reaches the decoder.
    mutable std::promise<void> entered;
    /// Ready when the test lets the read go on.
    std::shared_future<void> released;

private:
    std::shared_ptr<const an::ImageCodec> m_inner;
};

class RawPolicyIsolationTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() /
                      ("an-raw-policy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_directory);
        ASSERT_TRUE(an::tests::writeSyntheticDng({.fullColor = true}, m_directory / "full-colour.dng"));
        ASSERT_TRUE(an::tests::writeSyntheticDng({.fullColor = false}, m_directory / "mosaic.dng"));
        m_fullColour = an::tests::utf8Path(m_directory / "full-colour.dng");
        m_mosaic = an::tests::utf8Path(m_directory / "mosaic.dng");
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    std::filesystem::path m_directory;
    std::string m_fullColour;
    std::string m_mosaic;
};

} // namespace

TEST_F(RawPolicyIsolationTest, AViewerAndAWorkflowReadOneFullColourFrameWithTheirOwnBalanceAtOnce)
{
    // The viewer opens with the camera's as-shot balance while a workflow, which balances nothing, reads the same
    // file in the same session.
    const Bytes viewerAlone = readWithOwnContext(an::RawFullColorWhiteBalance::Camera, m_fullColour);
    const Bytes workflowAlone = readWithOwnContext(an::RawFullColorWhiteBalance::None, m_fullColour);
    ASSERT_FALSE(viewerAlone.empty());
    ASSERT_EQ(viewerAlone.size(), workflowAlone.size());
    ASSERT_NE(viewerAlone, workflowAlone)
        << "the two policies must give different frames for the case to mean anything";

    const an::EngineContext session = engineOf(rawRegistry(an::RawFullColorWhiteBalance::Daylight));
    const std::shared_ptr<const an::CodecRegistry> viewerPolicy = rawRegistry(an::RawFullColorWhiteBalance::Camera);
    const std::shared_ptr<const an::CodecRegistry> workflowPolicy = rawRegistry(an::RawFullColorWhiteBalance::None);
    std::atomic<int> viewerMismatches{0};
    std::atomic<int> workflowMismatches{0};
    std::mutex failures;
    std::vector<std::string> errors;
    const auto check = [&](const an::CodecRegistry& policy, const Bytes& expected, std::atomic<int>& mismatches) {
        std::string error;
        const std::optional<Bytes> samples = readThrough(session, policy, m_fullColour, error);
        if (!samples.has_value())
        {
            const std::lock_guard lock(failures);
            errors.push_back(error);
        }
        else if (*samples != expected)
        {
            ++mismatches;
        }
    };
    atOnce(
        8, [&](int) { check(*viewerPolicy, viewerAlone, viewerMismatches); },
        [&](int) { check(*workflowPolicy, workflowAlone, workflowMismatches); });
    EXPECT_TRUE(errors.empty()) << errors.front();
    EXPECT_EQ(viewerMismatches.load(), 0);
    EXPECT_EQ(workflowMismatches.load(), 0);
    EXPECT_EQ(session.memoryResource()->counters().liveBytes, 0u);

    // The session's own registry, which neither read named, still gives its own balance.
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(session, {});
    ASSERT_TRUE(execution.hasValue());
    std::string error;
    const std::optional<Bytes> ordinary =
        samplesOf(an::decodeImage(session, *execution, {.path = m_fullColour}), error);
    ASSERT_TRUE(ordinary.has_value()) << error;
    EXPECT_EQ(*ordinary, readWithOwnContext(an::RawFullColorWhiteBalance::Daylight, m_fullColour));
}

TEST_F(RawPolicyIsolationTest, AMosaicIsBalancedByEachOperationsOwnStepAtOnce)
{
    const an::EngineContext session = engineOf(rawRegistry(an::RawFullColorWhiteBalance::None));
    std::optional<Mosaic> mosaic;
    {
        // One worker, so the read through a named registry below is admitted beside it.
        an::Result<an::ExecutionContext> execution = oneWorker(session);
        ASSERT_TRUE(execution.hasValue());
        an::Result<an::CodecDecodeResult> decoded = an::decodeImage(session, *execution, {.path = m_mosaic});
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        ASSERT_TRUE(decoded->metadata.acquisition().rawCamera().has_value());
        const an::RawFrameDescriptor descriptor = *decoded->frame.descriptor();
        const an::ImageLayout layout = *decoded->frame.layout();
        const an::RawCameraMetadata camera = *decoded->metadata.acquisition().rawCamera();
        mosaic.emplace(Mosaic{std::move(*decoded), descriptor, layout, camera});

        // A full-colour balance named for the read changes nothing on a mosaic: its codes come back as they are.
        std::string error;
        const std::optional<Bytes> named =
            readThrough(session, *rawRegistry(an::RawFullColorWhiteBalance::Camera), m_mosaic, error);
        ASSERT_TRUE(named.has_value()) << error;
        const std::span<const std::byte> codes = mosaic->decoded.frame.bytes();
        EXPECT_EQ(*named, Bytes(codes.begin(), codes.end()));
    }
    const Bytes before(mosaic->decoded.frame.bytes().begin(), mosaic->decoded.frame.bytes().end());
    std::string error;
    const std::optional<std::vector<std::uint16_t>> viewerAlone =
        balance(session, *mosaic, an::CfaWhiteBalanceMode::Camera, error);
    ASSERT_TRUE(viewerAlone.has_value()) << error;
    const std::optional<std::vector<std::uint16_t>> workflowAlone =
        balance(session, *mosaic, an::CfaWhiteBalanceMode::None, error);
    ASSERT_TRUE(workflowAlone.has_value()) << error;
    ASSERT_NE(*viewerAlone, *workflowAlone);

    std::atomic<int> mismatches{0};
    std::atomic<int> failures{0};
    const auto check = [&](an::CfaWhiteBalanceMode mode, const std::vector<std::uint16_t>& expected) {
        std::string reason;
        const std::optional<std::vector<std::uint16_t>> balanced = balance(session, *mosaic, mode, reason);
        if (!balanced.has_value())
        {
            ++failures;
        }
        else if (*balanced != expected)
        {
            ++mismatches;
        }
    };
    atOnce(
        8, [&](int) { check(an::CfaWhiteBalanceMode::Camera, *viewerAlone); },
        [&](int) { check(an::CfaWhiteBalanceMode::None, *workflowAlone); });
    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(mismatches.load(), 0);
    // Each step wrote its own destination; the decoded codes both read are as they were.
    EXPECT_EQ(Bytes(mosaic->decoded.frame.bytes().begin(), mosaic->decoded.frame.bytes().end()), before);
}

TEST_F(RawPolicyIsolationTest, ASettingsChangeReachesTheNextReadAndNeverOneInFlight)
{
    const Bytes cameraAlone = readWithOwnContext(an::RawFullColorWhiteBalance::Camera, m_fullColour);
    const Bytes noneAlone = readWithOwnContext(an::RawFullColorWhiteBalance::None, m_fullColour);
    ASSERT_NE(cameraAlone, noneAlone);

    an::Result<std::shared_ptr<const an::ImageCodec>> camera =
        an::createRawCodec({.fullColorWhiteBalance = an::RawFullColorWhiteBalance::Camera});
    ASSERT_TRUE(camera.hasValue());
    const auto held = std::make_shared<HeldCodec>(*camera);
    std::promise<void> release;
    held->released = release.get_future().share();
    std::future<void> entered = held->entered.get_future();
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> codecs{held};
    an::Result<std::shared_ptr<const an::CodecRegistry>> heldRegistry = an::CodecRegistry::create(codecs);
    ASSERT_TRUE(heldRegistry.hasValue());

    // The settings a session publishes: one immutable snapshot, replaced whole when the user changes it.
    std::mutex settingsLock;
    std::shared_ptr<const an::CodecRegistry> settings = *heldRegistry;
    const auto snapshot = [&] {
        const std::lock_guard lock(settingsLock);
        return settings;
    };

    const an::EngineContext session = engineOf(nullptr);
    std::string inFlightError;
    std::future<std::optional<Bytes>> inFlight = std::async(std::launch::async, [&, policy = snapshot()] {
        return readThrough(session, *policy, m_fullColour, inFlightError);
    });
    const bool reached = entered.wait_for(std::chrono::seconds{60}) == std::future_status::ready;
    EXPECT_TRUE(reached) << "the first read never reached its decoder";
    {
        const std::lock_guard lock(settingsLock);
        settings = rawRegistry(an::RawFullColorWhiteBalance::None);
    }
    std::string error;
    const std::optional<Bytes> next = readThrough(session, *snapshot(), m_fullColour, error);
    release.set_value();
    const std::optional<Bytes> first = inFlight.get();
    ASSERT_TRUE(next.has_value()) << error;
    ASSERT_TRUE(first.has_value()) << inFlightError;
    EXPECT_EQ(*first, cameraAlone);
    EXPECT_EQ(*next, noneAlone);
}

TEST_F(RawPolicyIsolationTest, EveryPolicyDrawsOnTheSessionsOneBudget)
{
    const Bytes frame = readWithOwnContext(an::RawFullColorWhiteBalance::None, m_fullColour);
    ASSERT_FALSE(frame.empty());
    // Room for one frame and not two.
    const an::EngineContext session = engineOf(nullptr, frame.size() + frame.size() / 2u);
    const std::shared_ptr<const an::CodecRegistry> viewerPolicy = rawRegistry(an::RawFullColorWhiteBalance::Camera);
    const std::shared_ptr<const an::CodecRegistry> workflowPolicy = rawRegistry(an::RawFullColorWhiteBalance::None);

    an::Result<an::ExecutionContext> viewerExecution = oneWorker(session);
    ASSERT_TRUE(viewerExecution.hasValue());
    std::optional<an::CodecDecodeResult> shown;
    {
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(session, *viewerExecution, *viewerPolicy, {.path = m_fullColour});
        ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
        shown.emplace(std::move(*decoded));
    }
    const std::uint64_t held = session.memoryResource()->counters().liveBytes;
    EXPECT_GE(held, frame.size());
    {
        an::Result<an::ExecutionContext> workflowExecution = oneWorker(session);
        ASSERT_TRUE(workflowExecution.hasValue());
        const an::Result<an::CodecDecodeResult> refused =
            an::decodeImage(session, *workflowExecution, *workflowPolicy, {.path = m_fullColour});
        EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(session.memoryResource()->counters().liveBytes, held);
    }
    shown.reset();
    EXPECT_EQ(session.memoryResource()->counters().liveBytes, 0u);
    an::Result<an::ExecutionContext> workflowExecution = oneWorker(session);
    ASSERT_TRUE(workflowExecution.hasValue());
    const an::Result<an::CodecDecodeResult> admitted =
        an::decodeImage(session, *workflowExecution, *workflowPolicy, {.path = m_fullColour});
    EXPECT_TRUE(admitted.hasValue()) << admitted.status().message();
    EXPECT_LE(session.memoryResource()->counters().peakBytes, frame.size() + frame.size() / 2u);
}

TEST_F(RawPolicyIsolationTest, AStopReachesAFullColourReadThroughItsRegistry)
{
    struct StopAtFirstReport final : an::ProgressSink
    {
        std::stop_source stop;
        void onProgress(const an::ProgressReport&) noexcept override
        {
            stop.request_stop();
        }
    } sink;
    const an::EngineContext session = engineOf(nullptr);
    const std::shared_ptr<const an::CodecRegistry> policy = rawRegistry(an::RawFullColorWhiteBalance::Camera);
    an::ExecutionOptions options;
    options.stopToken = sink.stop.get_token();
    options.progressSink = &sink;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(session, options);
    ASSERT_TRUE(execution.hasValue());
    const an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(session, *execution, *policy, {.path = m_fullColour});
    EXPECT_TRUE(sink.stop.stop_requested());
    EXPECT_EQ(decoded.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(session.memoryResource()->counters().liveBytes, 0u);
}
