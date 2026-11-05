// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Representative measurements for the optional G'MIC adapter.
//
// The benchmark runner links the engine and nothing else of AfterNight's, and the build graph refuses
// an optional adapter in any target but its own suite - so a catalogue scenario for this adapter would
// need a second runner whose own provenance depended on an optional library. The adapter also
// implements no engine operation, so there is no catalogue row to compare it against: what it costs is
// boundary cost, and this is where that is observed.
//
// Three numbers, because they are the three a caller sizing a G'MIC call has to know and none of them
// is guessable from the others:
//
//   - **what a command costs before it touches a pixel.** The library's C bridge builds an
//     interpreter per call and re-parses its standard library of commands every time. The
//     measurement is the same empty command with the standard library available and without it, and
//     the ratio between them is the answer;
//   - **what the two sharing decisions cost.** Copying is one pass over the samples on the way in
//     plus the library's own allocation on the way out; sharing is neither. The counters are
//     asserted rather than the times, because the counter is the contract and the time is the host;
//     and
//   - **what a real filter costs over a representative image**, so the first two numbers can be read
//     against something rather than in isolation.
//
// Nothing here gates on an absolute time. A suite that failed on a wall clock would fail on a busy
// machine, and this is not the formal measurement harness - it is the place the adapter's own costs
// are recorded so they can be quoted. What is asserted is what must hold on any
// host: that sharing copies nothing, that copying copies exactly the image, and that the peak the
// library allocated is the result it produced.

#include <AfterNightEngineGmicAdapter/GmicRuntimeAdapter.h>

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>
#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// The image these measurements run over: four megabytes of one plane, which is the order of a
/// preview or a tile rather than of a full frame, and small enough that a suite stays a suite.
constexpr std::uint32_t kBenchWidth = 1024u;
constexpr std::uint32_t kBenchHeight = 1024u;

/// How many times each measurement runs. The reported number is the median, so an outlier from a
/// scheduler decision does not become the record.
constexpr int kRepetitions = 5;

[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout packedFloatLayout(std::uint32_t width, std::uint32_t height)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{
        .extent = *extent, .elementSizeBytes = 4u, .samplesPerPixel = 1u, .planeCount = 1u, .rowAlignmentBytes = 1u};
    const std::uint64_t row = static_cast<std::uint64_t>(width) * 4u;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, row, row * height, limits);
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] constexpr an::GmicResultIntent monoIntent() noexcept
{
    return an::GmicResultIntent{.channelSchema = an::ChannelSchema::Mono,
                                .colorSpace = an::ColorSpace::Unspecified,
                                .numericDomain = an::NumericDomain::NormalizedLinear,
                                .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                .pixelAspectRatio = 1.0};
}

/// The median of what a measurement produced, in microseconds.
[[nodiscard]] double medianMicroseconds(std::vector<double> samples)
{
    EXPECT_FALSE(samples.empty());
    std::sort(samples.begin(), samples.end());
    return samples.empty() ? 0.0 : samples[samples.size() / 2u];
}

/// Reports one number to the console, where a measurement record is written from.
void record(const char* name, double microseconds)
{
    std::printf("  %-44s %10.1f us\n", name, microseconds);
    std::fflush(stdout);
}

class GmicBenchmark : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!an::gmicAdapterCapabilities().libraryCompiledIn)
        {
            GTEST_SKIP() << "this adapter was built with no G'MIC library; there is nothing to measure";
        }
        m_engine = makeEngineContext();
        ASSERT_TRUE(m_engine.valid());
    }

    an::EngineContext m_engine;
};

} // namespace

TEST_F(GmicBenchmark, WhatACommandCostsBeforeItTouchesAPixel)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    an::Result<an::GmicRuntimeAdapter> withLibrary =
        an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{.useStandardLibrary = true});
    ASSERT_TRUE(withLibrary.hasValue()) << withLibrary.status().message();
    an::Result<an::GmicRuntimeAdapter> bare =
        an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{.useStandardLibrary = false});
    ASSERT_TRUE(bare.hasValue()) << bare.status().message();

    // One command that does nothing but be parsed and run, so what is left is the fixed cost: the
    // interpreter this library builds per call, and the standard library it parses into it.
    const auto measure = [&execution](an::GmicRuntimeAdapter& session) {
        std::vector<double> samples;
        samples.reserve(kRepetitions);
        for (int index = 0; index < kRepetitions; ++index)
        {
            const auto started = std::chrono::steady_clock::now();
            const an::Status status = session.execute(execution, "v 0");
            const auto elapsed = std::chrono::steady_clock::now() - started;
            EXPECT_TRUE(status.ok()) << status.message();
            samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
        }
        return medianMicroseconds(std::move(samples));
    };

    ASSERT_TRUE(withLibrary->execute(execution, "v 0").ok()) << "one warm-up, because the library "
                                                                "decompresses its command file once";
    std::printf("[ G'MIC ] fixed per-call cost\n");
    const double withStdlib = measure(*withLibrary);
    const double withoutStdlib = measure(*bare);
    record("fixed_call_with_standard_library_us", withStdlib);
    record("fixed_call_without_standard_library_us", withoutStdlib);
    record("standard_library_parse_per_call_us", withStdlib - withoutStdlib);

    EXPECT_GT(withStdlib, withoutStdlib)
        << "the standard library is parsed into a fresh interpreter on every call, which is the "
           "cost this adapter's session options let a caller decline";
    EXPECT_TRUE(withLibrary->shutdown().ok());
    EXPECT_TRUE(bare->shutdown().ok());
}

TEST_F(GmicBenchmark, WhatTheTwoSharingDecisionsCost)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    an::Result<an::GmicRuntimeAdapter> session = an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{});
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    const an::ImageLayout layout = packedFloatLayout(kBenchWidth, kBenchHeight);
    const std::uint64_t imageBytes = layout.totalBytes();
    std::vector<float> samples(static_cast<std::size_t>(kBenchWidth) * kBenchHeight, 0.25f);

    const auto describe = [&samples, &layout](an::BufferMutability mutability) {
        const std::optional<an::ImageView<float>> image =
            an::ImageView<float>::create(std::span<float>(samples), layout);
        EXPECT_TRUE(image.has_value());
        const an::ExternalBufferContract contract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                                  .libraryDomain = an::NumericDomain::NormalizedLinear,
                                                  .ownership = an::BufferOwnership::EngineOwned,
                                                  .scope = an::BorrowScope::SingleCall,
                                                  .mutability = mutability};
        const std::optional<an::GmicImageDescription> described = an::describeAsGmicImage(*image, contract);
        EXPECT_TRUE(described.has_value());
        return described.value_or(an::GmicImageDescription{});
    };

    const auto measure = [&](an::GmicSourceSharing sharing, an::BufferMutability mutability) {
        std::vector<double> times;
        times.reserve(kRepetitions);
        for (int index = 0; index < kRepetitions; ++index)
        {
            const an::GmicImageCall call{.command = "add 0",
                                         .imageName = "bench",
                                         .source = describe(mutability),
                                         .sharing = sharing,
                                         .intent = monoIntent()};
            const auto started = std::chrono::steady_clock::now();
            an::Result<an::RawFrame> ran = session->run(execution, call);
            const auto elapsed = std::chrono::steady_clock::now() - started;
            EXPECT_TRUE(ran.hasValue()) << ran.status().message();
            times.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
        }
        return medianMicroseconds(std::move(times));
    };

    std::printf("[ G'MIC ] %u x %u float plane, %llu bytes\n", kBenchWidth, kBenchHeight,
                static_cast<unsigned long long>(imageBytes));
    const double shared = measure(an::GmicSourceSharing::Share, an::BufferMutability::ReadWrite);
    const an::GmicCallCounters afterShare = session->counters();
    const double copied = measure(an::GmicSourceSharing::Copy, an::BufferMutability::ReadOnly);
    const an::GmicCallCounters afterCopy = session->counters();
    record("share_add_one_us", shared);
    record("copy_add_one_us", copied);

    // The contract, which is what is asserted: sharing moves no bytes and allocates nothing on the
    // far side; copying moves exactly the image in each direction.
    EXPECT_EQ(afterShare.sharedBytes, imageBytes * kRepetitions);
    EXPECT_EQ(afterShare.copiedIntoLibraryBytes, 0u);
    EXPECT_EQ(afterShare.libraryResultBytes, 0u);
    EXPECT_EQ(afterCopy.copiedIntoLibraryBytes, imageBytes * kRepetitions);
    EXPECT_EQ(afterCopy.libraryResultBytes, imageBytes * kRepetitions);
    EXPECT_EQ(afterCopy.peakLibraryResultBytes, imageBytes);
    EXPECT_TRUE(session->shutdown().ok());
}

TEST_F(GmicBenchmark, WhatARepresentativeFilterCosts)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    an::Result<an::GmicRuntimeAdapter> session = an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{});
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    const an::ImageLayout layout = packedFloatLayout(kBenchWidth, kBenchHeight);
    std::vector<float> samples(static_cast<std::size_t>(kBenchWidth) * kBenchHeight, 0.25f);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>(samples), layout);
    ASSERT_TRUE(image.has_value());
    const an::ExternalBufferContract contract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                              .libraryDomain = an::NumericDomain::NormalizedLinear,
                                              .ownership = an::BufferOwnership::EngineOwned,
                                              .scope = an::BorrowScope::SingleCall,
                                              .mutability = an::BufferMutability::ReadOnly};
    const std::optional<an::GmicImageDescription> described = an::describeAsGmicImage(*image, contract);
    ASSERT_TRUE(described.has_value());

    std::printf("[ G'MIC ] representative filters\n");
    int measured = 0;
    for (const char* command : {"blur 4", "sharpen 30", "denoise_haar 0.1,3,1"})
    {
        std::vector<double> times;
        times.reserve(kRepetitions);
        bool ran = true;
        for (int index = 0; index < kRepetitions && ran; ++index)
        {
            const auto started = std::chrono::steady_clock::now();
            an::Result<an::RawFrame> frame =
                session->run(execution, an::GmicImageCall{.command = command,
                                                          .source = *described,
                                                          .sharing = an::GmicSourceSharing::Copy,
                                                          .intent = monoIntent()});
            const auto elapsed = std::chrono::steady_clock::now() - started;
            ran = frame.hasValue();
            times.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
        }
        if (ran)
        {
            ++measured;
            record((std::string("filter_") + command).c_str(), medianMicroseconds(std::move(times)));
        }
        else
        {
            // A filter this build of the library does not define is not a failure of the adapter.
            // The measurement is of what is there; which commands a G'MIC build carries is the
            // dependency record's business, not this suite's.
            std::printf("  %-44s %10s\n", command, "absent");
        }
    }
    EXPECT_GT(measured, 0) << "no representative filter ran, which is a library that answers nothing";
    EXPECT_TRUE(session->shutdown().ok());
}
