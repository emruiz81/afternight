// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Shared fixtures for the neighbourhood-filter suites.
//
// The three suites - convolution, Gaussian and order filter - all need the same four things: an
// engine, an execution, a layout, and an image whose samples are known. Writing them once keeps the
// three from disagreeing about what a planar layout is, which is exactly the kind of drift that
// makes two suites pass while the operation is wrong for one of them.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace spatial_test
{

/// An engine context with a known thread budget.
///
/// @param concurrency how many threads the context owns.
/// @return the context.
[[nodiscard]] inline an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// An execution over one engine context.
///
/// @param engine the context to lease from.
/// @param concurrency how many threads the operation asks for.
/// @param backend which backend to force, or `Automatic` to let dispatch decide.
/// @return the execution.
[[nodiscard]] inline an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 3u,
                                                        an::ComputeBackend backend = an::ComputeBackend::Automatic)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A planar layout: one plane per channel, with optional row padding.
///
/// @param type the resident representation.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many channels.
/// @param rowPaddingSamples how many unused samples follow each row.
/// @return the layout.
[[nodiscard]] inline an::ImageLayout planarLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                  std::uint32_t channels = 1u, std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width + rowPaddingSamples) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// An interleaved layout: one plane holding every channel of every pixel.
///
/// @param type the resident representation.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many channels.
/// @return the layout.
[[nodiscard]] inline an::ImageLayout interleavedLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                       std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = channels,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width) * channels * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A deterministic pseudo-random sequence, so a differential test is reproducible.
///
/// A small linear congruential generator rather than the standard library's, because the standard
/// library's distributions are not specified to produce the same sequence across implementations
/// and a fixture that changes between two toolchains is not a fixture.
class Sequence
{
public:
    /// Starts a sequence.
    ///
    /// @param seed where to start.
    explicit Sequence(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    /// The next value, uniform over the unit interval.
    ///
    /// @return the value.
    [[nodiscard]] float next() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>((m_state >> 40) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

private:
    /// The generator state.
    std::uint64_t m_state;
};

/// A plane of samples produced by a deterministic sequence.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param seed which sequence to draw from.
/// @return the samples, row-major and tightly packed.
[[nodiscard]] inline std::vector<float> randomPlane(std::uint32_t width, std::uint32_t height, std::uint64_t seed)
{
    Sequence sequence{seed};
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (float& sample : samples)
    {
        sample = sequence.next();
    }
    return samples;
}

} // namespace spatial_test
