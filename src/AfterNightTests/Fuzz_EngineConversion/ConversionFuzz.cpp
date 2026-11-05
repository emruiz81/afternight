// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What must hold of every conversion, whatever the policy, the representations and the layouts are.
//
// A conversion is the one operation in this engine with two compiled implementations that are
// required to agree bit for bit: the scalar matrix reached by forcing `ComputeBackend::Reference`,
// and whatever the CPU dispatch table selected for this host. That makes the differential the
// property worth fuzzing, because it needs no oracle - the engine is its own - and because the place
// a vector conversion goes wrong is the end of a row, the row whose stride is not its width, and the
// value the destination cannot hold, all of which a mutator reaches far faster than a written case
// enumerates.
//
// Three other properties come free from the same inputs and are checked here:
//
// * **A byte swap is its own inverse.** Two swaps of the same samples return the bytes that were
//   there, whatever those bytes mean.
// * **A repack is a permutation.** Planar to interleaved and back returns the samples that were
//   there, in the places they were in.
// * **The counters are a property of the samples.** Both backends report the same three numbers, so
//   a count that came out of a vector lane's mask rather than the value it was about would show.
//
// Both destinations are compared rather than one checked against an expectation, so this target
// states nothing about what a conversion should produce. It states that the answer does not depend
// on which of two implementations produced it, which is the claim the engine actually makes.

#include "../FuzzEngine/FuzzHarness.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace an::fuzz
{

TargetInfo targetInfo()
{
    // Small, because the input is parameters and a handful of pixels rather than a container. A
    // conversion's defects live in the tail of a row, not in the length of the image.
    return {.name = "Fuzz_EngineConversion", .maxInputBytes = 1024u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    // Nothing structured to seed with: every byte of this target's input is a parameter or a sample,
    // so a run of plain patterns is a better start than one hand-written case. The zeros, the ones
    // and the alternating bits put the interesting sample values in front of the mutator immediately.
    std::vector<std::vector<std::uint8_t>> corpus;
    corpus.emplace_back(64u, 0x00u);
    corpus.emplace_back(64u, 0xFFu);
    corpus.emplace_back(64u, 0x7Fu);
    corpus.emplace_back(64u, 0x80u);
    std::vector<std::uint8_t> alternating(96u);
    for (std::size_t index = 0u; index < alternating.size(); ++index)
    {
        alternating[index] = static_cast<std::uint8_t>((index & 1u) != 0u ? 0xFFu : 0x00u);
    }
    corpus.push_back(std::move(alternating));
    std::vector<std::uint8_t> counting(128u);
    for (std::size_t index = 0u; index < counting.size(); ++index)
    {
        counting[index] = static_cast<std::uint8_t>(index);
    }
    corpus.push_back(std::move(counting));
    return corpus;
}

} // namespace an::fuzz

namespace
{

/// The engine every input runs in.
///
/// One context for the whole process rather than one per input: building it is the expensive part of
/// an iteration, its configuration is not what this target varies, and an execution already carries
/// everything an input does choose. Its maximum concurrency is above one so that a conversion that
/// scheduled its rows differently on two backends would show.
///
/// @return the context, which is valid for the life of the process.
[[nodiscard]] const an::EngineContext& engine()
{
    static const an::EngineContext context = []() {
        an::EngineContextOptions options;
        options.maxConcurrency = 4u;
        an::Result<an::EngineContext> created = an::EngineContext::create(options);
        AN_FUZZ_CHECK(created.hasValue(), "the engine context this target runs every input in must exist");
        return std::move(*created);
    }();
    return context;
}

/// One operation on a stated backend.
///
/// @param backend which implementation to require.
/// @param concurrency how many threads the operation may use.
/// @return the execution, or nothing when the engine refused it.
[[nodiscard]] std::optional<an::ExecutionContext> execution(an::ComputeBackend backend, std::uint32_t concurrency)
{
    an::ExecutionOptions options;
    options.backend = backend;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine(), options);
    if (!created.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*created);
}

/// A layout the input asked for, or nothing when it asked for one that does not exist.
///
/// @param type what the samples are.
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channels how many channels.
/// @param interleaved whether one plane holds every channel.
/// @param rowPaddingBytes how much padding follows each row.
/// @return the layout.
[[nodiscard]] std::optional<an::ImageLayout> layoutOf(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                                      std::uint32_t channels, bool interleaved,
                                                      std::uint32_t rowPaddingBytes)
{
    if (!an::isSampleType(type))
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::uint32_t samplesPerPixel = interleaved ? channels : 1u;
    const std::uint32_t planeCount = interleaved ? 1u : channels;
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = an::sampleTypeSizeBytes(type),
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planeCount,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        static_cast<std::uint64_t>(width) * samplesPerPixel * an::sampleTypeSizeBytes(type) + rowPaddingBytes;
    return an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
}

/// A buffer of the size a layout needs, filled from the input and then from a repeating pattern.
///
/// The input is usually shorter than the buffer, and a tail of zeros would make every sample past
/// the input's end the same value - which is exactly the sample a conversion is least likely to get
/// wrong. Repeating what the input gave keeps the tail as varied as the head.
///
/// @param layout what has to fit.
/// @param input where the bytes come from.
/// @return the buffer.
[[nodiscard]] std::vector<std::byte> bufferFor(const an::ImageLayout& layout, an::fuzz::Input& input)
{
    const std::size_t bytes = static_cast<std::size_t>(layout.totalBytes());
    std::vector<std::byte> buffer(bytes, std::byte{0});
    const std::vector<std::uint8_t> taken = input.takeBytes(bytes);
    for (std::size_t index = 0u; index < bytes; ++index)
    {
        buffer[index] = taken.empty() ? std::byte{0} : static_cast<std::byte>(taken[index % taken.size()]);
    }
    return buffer;
}

/// The two backends must convert the same samples into the same bytes and report the same counts.
///
/// @param sourceType what the source samples are.
/// @param source where they are.
/// @param sourceLayout how they sit.
/// @param destinationType what they become.
/// @param destinationLayout how the result sits.
/// @param policy the five decisions.
/// @param concurrency how many threads each conversion may use.
void checkBackendsAgree(an::SampleType sourceType, const std::vector<std::byte>& source,
                        const an::ImageLayout& sourceLayout, an::SampleType destinationType,
                        const an::ImageLayout& destinationLayout, const an::ConversionPolicy& policy,
                        std::uint32_t concurrency)
{
    const std::optional<an::ExecutionContext> reference = execution(an::ComputeBackend::Reference, concurrency);
    const std::optional<an::ExecutionContext> automatic = execution(an::ComputeBackend::Automatic, concurrency);
    if (!reference.has_value() || !automatic.has_value())
    {
        return;
    }

    // The fill is not zero, so a conversion that wrote fewer samples than it claimed - a tail it
    // skipped, a padded row it treated as contiguous - leaves evidence rather than matching a
    // destination that was already correct by accident.
    std::vector<std::byte> referenceBytes(static_cast<std::size_t>(destinationLayout.totalBytes()), std::byte{0xCD});
    std::vector<std::byte> automaticBytes(referenceBytes.size(), std::byte{0xCD});

    an::ConversionCounters referenceCounters;
    an::ConversionCounters automaticCounters;
    const an::Status referenceStatus =
        an::convertImageSamples(*reference, sourceType, source.data(), sourceLayout, destinationType,
                                referenceBytes.data(), destinationLayout, policy, referenceCounters);
    const an::Status automaticStatus =
        an::convertImageSamples(*automatic, sourceType, source.data(), sourceLayout, destinationType,
                                automaticBytes.data(), destinationLayout, policy, automaticCounters);

    AN_FUZZ_CHECK(referenceStatus.code() == automaticStatus.code(),
                  "the two backends must accept and refuse the same conversions");
    an::fuzz::countReached(referenceStatus.ok() ? "conversions" : "conversions-refused");
    if (!referenceStatus.ok())
    {
        return;
    }

    AN_FUZZ_CHECK(referenceBytes == automaticBytes, "the two backends must produce the same bytes");
    AN_FUZZ_CHECK(referenceCounters == automaticCounters, "the two backends must report the same counters");
    AN_FUZZ_CHECK(referenceCounters.samplesConverted == static_cast<std::uint64_t>(sourceLayout.extent().width()) *
                                                            sourceLayout.extent().height() *
                                                            sourceLayout.extent().channelCount(),
                  "a successful conversion converts every sample of the image exactly once");
}

/// A byte swap is its own inverse and a repack is a permutation.
///
/// @param type what the samples are.
/// @param layout how they sit.
/// @param bytes the samples, which are copied rather than changed.
/// @param concurrency how many threads each operation may use.
void checkInvolutions(an::SampleType type, const an::ImageLayout& layout, const std::vector<std::byte>& bytes,
                      std::uint32_t concurrency)
{
    const std::optional<an::ExecutionContext> operation = execution(an::ComputeBackend::Automatic, concurrency);
    if (!operation.has_value())
    {
        return;
    }

    std::vector<std::byte> swapped = bytes;
    const an::Status first = an::swapImageByteOrder(*operation, type, swapped.data(), layout);
    if (first.ok())
    {
        const an::Status second = an::swapImageByteOrder(*operation, type, swapped.data(), layout);
        AN_FUZZ_CHECK(second.ok(), "a swap that succeeded once must succeed again on the same samples");
        AN_FUZZ_CHECK(swapped == bytes, "two byte swaps of the same samples must return the bytes that were there");
        an::fuzz::countReached("swaps");
    }

    // The repack's partner layout holds the same pixels in the other arrangement, so it exists
    // exactly when this layout's does.
    const std::uint32_t channels = layout.extent().channelCount();
    const bool interleaved = layout.planeCount() == 1u && channels > 1u;
    const std::optional<an::ImageLayout> other =
        layoutOf(type, layout.extent().width(), layout.extent().height(), channels, !interleaved, 0u);
    const std::optional<an::ImageLayout> tight =
        layoutOf(type, layout.extent().width(), layout.extent().height(), channels, interleaved, 0u);
    if (!other.has_value() || !tight.has_value() || channels < 2u)
    {
        return;
    }

    // A padded source is repacked from a tight copy of itself, because the round trip's claim is
    // about the samples rather than about the padding, which a repack does not carry.
    std::vector<std::byte> start(static_cast<std::size_t>(tight->totalBytes()), std::byte{0});
    for (std::size_t index = 0u; index < start.size(); ++index)
    {
        start[index] = bytes[index % bytes.size()];
    }
    std::vector<std::byte> middle(static_cast<std::size_t>(other->totalBytes()), std::byte{0xCD});
    std::vector<std::byte> back(start.size(), std::byte{0xCD});

    if (!an::repackImageSamples(*operation, type, start.data(), *tight, middle.data(), *other).ok())
    {
        return;
    }
    const an::Status returned = an::repackImageSamples(*operation, type, middle.data(), *other, back.data(), *tight);
    AN_FUZZ_CHECK(returned.ok(), "a repack that succeeded must succeed in the other direction too");
    AN_FUZZ_CHECK(back == start, "a repack and its inverse must return the samples that were there");
    an::fuzz::countReached("repacks");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);

    const an::ConversionPolicy policy{.rounding = input.takeEnum(an::RoundingMode::Ceiling),
                                      .range = input.takeEnum(an::RangePolicy::Reject),
                                      .nonFinite = input.takeEnum(an::NonFinitePolicy::Reject),
                                      .domain = input.takeEnum(an::DomainMapping::CodeValue),
                                      .signedConvention = input.takeEnum(an::SignedIntegerConvention::OffsetBinary)};

    const bool everyFieldKnown = an::isRoundingMode(policy.rounding) && an::isRangePolicy(policy.range) &&
                                 an::isNonFinitePolicy(policy.nonFinite) && an::isDomainMapping(policy.domain) &&
                                 an::isSignedIntegerConvention(policy.signedConvention);
    const an::Status decided = an::checkConversionPolicy(policy, "Fuzz_EngineConversion");
    AN_FUZZ_CHECK(decided.ok() == everyFieldKnown,
                  "a policy is accepted exactly when all five of its decisions are values this build knows");

    const an::SampleType sourceType = input.takeEnum(an::SampleType::Float64);
    const an::SampleType destinationType = input.takeEnum(an::SampleType::Float64);
    const std::uint32_t width = 1u + input.takeBelow(19u);
    const std::uint32_t height = 1u + input.takeBelow(5u);
    const std::uint32_t channels = 1u + input.takeBelow(3u);
    const bool interleaved = input.takeFlag();
    const std::uint32_t sourcePadding = input.takeBelow(9u);
    const std::uint32_t destinationPadding = input.takeBelow(9u);
    const std::uint32_t concurrency = 1u + input.takeBelow(4u);

    const std::optional<an::ImageLayout> sourceLayout =
        layoutOf(sourceType, width, height, channels, interleaved, sourcePadding);
    const std::optional<an::ImageLayout> destinationLayout =
        layoutOf(destinationType, width, height, channels, interleaved, destinationPadding);
    if (!sourceLayout.has_value() || !destinationLayout.has_value())
    {
        return 0;
    }

    const std::vector<std::byte> source = bufferFor(*sourceLayout, input);
    checkBackendsAgree(sourceType, source, *sourceLayout, destinationType, *destinationLayout, policy, concurrency);
    checkInvolutions(sourceType, *sourceLayout, source, concurrency);
    return 0;
}
