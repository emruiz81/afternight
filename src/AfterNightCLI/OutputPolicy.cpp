// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command-line tool's output policy: the representation a run writes and the compression each container is
// written with.

#include "OutputPolicy.h"

#include "CommandLine.h"

#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>

#include <algorithm>
#include <array>

namespace an::cli
{
namespace
{

/// The eight representations, in the order the engine enumerates them.
constexpr std::array<an::SampleType, 8u> kSampleTypes{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

/// The three kinds of number a representation holds.
enum class NumberKind : std::uint8_t
{
    Unsigned,
    Signed,
    Floating
};

[[nodiscard]] constexpr NumberKind kindOf(an::SampleType type) noexcept
{
    if (an::isFloatingSampleType(type))
    {
        return NumberKind::Floating;
    }
    return an::isSignedSampleType(type) ? NumberKind::Signed : NumberKind::Unsigned;
}

/// How much a candidate is preferred among candidates of one width for an input: the input's own kind first, then
/// unsigned integers, signed integers and floating point. Lower is preferred.
[[nodiscard]] constexpr int kindRank(an::SampleType candidate, an::SampleType source) noexcept
{
    const NumberKind kind = kindOf(candidate);
    if (kind == kindOf(source))
    {
        return 0;
    }
    return kind == NumberKind::Unsigned ? 1 : kind == NumberKind::Signed ? 2 : 3;
}

} // namespace

std::vector<an::SampleType> encodableSampleTypes(std::uint32_t encodeSamples)
{
    std::vector<an::SampleType> types;
    for (const an::SampleType type : kSampleTypes)
    {
        if ((encodeSamples & an::codecSampleBit(an::rawSampleFormatOf(type))) != 0u)
        {
            types.push_back(type);
        }
    }
    return types;
}

std::optional<an::SampleType> chooseOutputSampleType(an::SampleType source, std::optional<an::SampleType> requested,
                                                     std::uint32_t encodeSamples)
{
    const std::vector<an::SampleType> encodable = encodableSampleTypes(encodeSamples);
    const auto holds = [&encodable](an::SampleType type) {
        return std::find(encodable.begin(), encodable.end(), type) != encodable.end();
    };
    if (requested.has_value())
    {
        return holds(*requested) ? requested : std::nullopt;
    }
    if (encodable.empty())
    {
        return std::nullopt;
    }
    if (holds(source))
    {
        return source;
    }
    // The narrowest at least as wide as the input; the widest when none is. Among one width, by kind.
    const std::uint32_t sourceWidth = an::sampleTypeSizeBytes(source);
    std::optional<an::SampleType> chosen;
    for (const an::SampleType candidate : encodable)
    {
        if (!chosen.has_value())
        {
            chosen = candidate;
            continue;
        }
        const std::uint32_t width = an::sampleTypeSizeBytes(candidate);
        const std::uint32_t chosenWidth = an::sampleTypeSizeBytes(*chosen);
        const bool holdsInput = width >= sourceWidth;
        const bool chosenHoldsInput = chosenWidth >= sourceWidth;
        bool better = false;
        if (holdsInput != chosenHoldsInput)
        {
            better = holdsInput;
        }
        else if (width != chosenWidth)
        {
            better = holdsInput ? width < chosenWidth : width > chosenWidth;
        }
        else
        {
            better = kindRank(candidate, source) < kindRank(*chosen, source);
        }
        if (better)
        {
            chosen = candidate;
        }
    }
    return chosen;
}

bool holdsFilterArrayMosaic(an::CodecFormat format, an::CfaPattern pattern) noexcept
{
    if (format == an::CodecFormat::Fits)
    {
        return !an::cfaPatternIsTabulated(pattern);
    }
    return format == an::CodecFormat::Xisf || format == an::CodecFormat::Tiff;
}

an::OutputCompression outputCompression(an::CodecFormat format) noexcept
{
    switch (format)
    {
    case an::CodecFormat::Png:
        return an::OutputCompression::Deflate;
    case an::CodecFormat::Jpeg:
        return an::OutputCompression::Jpeg;
    default:
        return an::OutputCompression::None;
    }
}

std::string describeEncodableSampleTypes(std::uint32_t encodeSamples)
{
    const std::vector<an::SampleType> encodable = encodableSampleTypes(encodeSamples);
    if (encodable.empty())
    {
        return "nothing";
    }
    std::string text;
    for (std::size_t index = 0u; index < encodable.size(); ++index)
    {
        if (index > 0u)
        {
            text += index + 1u == encodable.size() ? " and " : ", ";
        }
        text += sampleTypeArgument(encodable[index]);
    }
    return text;
}

} // namespace an::cli
