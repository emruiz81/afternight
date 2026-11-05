// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What must hold of every byte string offered as an image's description.
//
// Two entry points are under test and they are the two ends of one claim. `ImageDescriptor::check`
// and `create` decide whether a set of values describes an image; `checkImageDescriptorText` and
// `parseImageDescriptor` decide whether a line of text names such a set. The properties worth
// fuzzing are the ones that tie them together:
//
// * **A reason and a value never disagree.** `check` naming no reason and `create` yielding nothing
//   would be a descriptor that exists and is invalid, or a refusal with no rule behind it.
// * **The text form is lossless.** Every descriptor that can be built formats to a line that parses
//   back to exactly that descriptor - the pixel aspect included, which is the field where a decimal
//   representation can quietly lose a bit.
// * **Parsing builds nothing the direct entry point would refuse.** A parser that accepted a line
//   the request check refuses would be a second, weaker door into the same type.
//
// The limits are fuzzed alongside the values, because they are the caller's rather than the text's:
// the same line describes an image one configuration admits and another refuses, and a parser that
// consulted its own idea of the maxima instead of the caller's would pass every test that used the
// defaults.

#include "../FuzzEngine/FuzzHarness.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/DescriptorText.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace an::fuzz
{

TargetInfo targetInfo()
{
    return {.name = "Fuzz_EngineDescriptor", .maxInputBytes = 4096u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    // Lines rather than bytes, because this target's subject is a text form. Each is either what the
    // formatter writes or one step away from it, so a single mutation reaches a wrong enumerator, a
    // repeated field, a missing one, or an aspect ratio that is not a number.
    static constexpr const char* kLines[] = {
        "extent=640x480x3 sample=Float32 channels=Rgb color=LinearRgb domain=NormalizedLinear cfa=None@0,0 "
        "origin=TopLeft aspect=1",
        "extent=1x1x1 sample=UInt8 channels=Mono color=Unspecified domain=RawSensorCode cfa=None@0,0 "
        "origin=BottomLeft aspect=1",
        "extent=4096x4096x1 sample=UInt16 channels=Mono color=Unspecified domain=RawSensorCode cfa=Rggb@1,1 "
        "origin=TopLeft aspect=1.5",
        "extent=36x36x1 sample=UInt16 channels=Mono color=Unspecified domain=RawSensorCode "
        "cfa=XTrans@0,0:GGRGGBGGBGGRBRGRBGGGBGGRGGRGGBRBGBRG origin=TopLeft aspect=1",
        "extent=8x8x4 sample=Int32 channels=Rgba color=DisplayRgb domain=EncodedNonlinear cfa=None@0,0 "
        "origin=TopLeft aspect=0.9999999999999999",
        "extent=3x2x3 sample=Float64 channels=Lab color=CieLab domain=PhysicalScaled cfa=None@0,0 origin=TopLeft "
        "aspect=2.2250738585072014e-308",
        "aspect=1 origin=TopLeft cfa=None@0,0 domain=NormalizedLinear color=LinearRgb channels=Rgb sample=Float32 "
        "extent=2x2x3",
        "extent=0x0x0 sample=Float32 channels=Mono color=Unspecified domain=NormalizedLinear cfa=None@0,0 "
        "origin=TopLeft aspect=1",
        "",
    };

    std::vector<std::vector<std::uint8_t>> corpus;
    for (const char* line : kLines)
    {
        const std::string text(line);
        corpus.emplace_back(reinterpret_cast<const std::uint8_t*>(text.data()),
                            reinterpret_cast<const std::uint8_t*>(text.data()) + text.size());
    }
    return corpus;
}

} // namespace an::fuzz

namespace
{

/// The maxima this input wants in force, which are sometimes tighter than the defaults.
///
/// @param input where the choices come from, read from the back so the payload stays whole.
/// @return the limits.
[[nodiscard]] an::EngineLimits limitsFrom(an::fuzz::Input& input)
{
    an::EngineLimits limits = an::defaultEngineLimits();
    switch (input.controlBelow(4u))
    {
    case 0:
        break;
    case 1:
        limits.maxImageWidth = 1u + input.controlBelow(64u);
        limits.maxImageHeight = 1u + input.controlBelow(64u);
        break;
    case 2:
        limits.maxChannelCount = 1u + input.controlBelow(4u);
        break;
    default:
        limits.maxImageBytes = 1u + static_cast<std::uint64_t>(input.controlBelow(4096u));
        break;
    }
    return limits;
}

/// Every claim the two text entry points make about one line.
///
/// @param text the line.
/// @param limits the maxima in force.
void checkTextForm(const std::string& text, const an::EngineLimits& limits)
{
    const an::DescriptorError reason = an::checkImageDescriptorText(text, limits);
    const std::optional<an::ImageDescriptor> parsed = an::parseImageDescriptor(text, limits);

    AN_FUZZ_CHECK((reason == an::DescriptorError::None) == parsed.has_value(),
                  "a line either describes an image or names the reason it does not, never both and never neither");
    an::fuzz::countReached(parsed.has_value() ? "lines-parsed" : "lines-refused");
    if (!parsed.has_value())
    {
        return;
    }

    AN_FUZZ_CHECK(an::ImageDescriptor::check(parsed->toRequest()) == an::DescriptorError::None,
                  "a parsed descriptor must be one the request check would also have built");

    const std::string written = an::formatImageDescriptor(*parsed);
    const std::optional<an::ImageDescriptor> reread = an::parseImageDescriptor(written, limits);
    AN_FUZZ_CHECK(reread.has_value(), "what the formatter writes must parse");
    AN_FUZZ_CHECK(*reread == *parsed, "the text form must be lossless");
    AN_FUZZ_CHECK(an::formatImageDescriptor(*reread) == written, "formatting must be stable under a round trip");
}

/// Every claim the request entry points make about one set of values.
///
/// @param input where the values come from.
/// @param limits the maxima in force.
void checkRequestForm(an::fuzz::Input& input, const an::EngineLimits& limits)
{
    // The schema comes first and the channel count usually follows from it. Chosen independently, the
    // two agreed about one request in ten and every other one was refused by the channel-count rule
    // before any other rule was reached - so the sweep spent its budget re-proving the first check in
    // the list. One request in five still states a count of its own, which is what keeps that rule
    // covered.
    const an::ChannelSchema channelSchema = input.takeEnum(an::ChannelSchema::Named);
    const std::optional<std::uint32_t> implied =
        an::isChannelSchema(channelSchema) ? an::channelCountOf(channelSchema) : std::nullopt;
    const bool stateOwnCount = input.takeBelow(5u) == 0u || !implied.has_value();
    const std::uint32_t channelCount = stateOwnCount ? input.takeBelow(limits.maxChannelCount + 2u) : *implied;

    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
        input.takeBelow(limits.maxImageWidth + 2u), input.takeBelow(limits.maxImageHeight + 2u), channelCount, limits);
    if (!extent.has_value())
    {
        return;
    }

    an::CfaDescriptor cfa;
    if (input.takeFlag())
    {
        an::CfaColorEntries entries{};
        for (std::size_t index = 0u; index < an::kCfaTableEntries; ++index)
        {
            entries[index] = input.takeEnum(an::CfaColor::Blue);
        }
        const std::optional<an::CfaDescriptor> tabulated = an::CfaDescriptor::create(
            entries, input.takeBelow(an::kCfaTableSide + 1u), input.takeBelow(an::kCfaTableSide + 1u));
        if (tabulated.has_value())
        {
            cfa = *tabulated;
        }
    }
    else
    {
        const std::optional<an::CfaDescriptor> named =
            an::CfaDescriptor::create(input.takeEnum(an::CfaPattern::XTrans), input.takeBelow(4u), input.takeBelow(4u));
        if (named.has_value())
        {
            cfa = *named;
        }
    }

    // The aspect is the one field whose valid values are a vanishing fraction of the bit patterns a
    // double holds, so most inputs choose from the ratios a sensor actually has and the rest get a
    // raw eight bytes - which is where the zero, the infinity and the not-a-number come from.
    static constexpr double kAspects[] = {1.0, 0.5, 2.0, 1.5, 4.0 / 3.0, 0.9999999999999999, 1e-300, 1e300};
    const std::uint32_t aspectChoice = input.takeBelow(static_cast<std::uint32_t>(std::size(kAspects)) + 2u);
    const double pixelAspectRatio = aspectChoice < std::size(kAspects) ? kAspects[aspectChoice] : input.takeDouble();

    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = input.takeEnum(an::SampleType::Float64),
                                             .channelSchema = channelSchema,
                                             .colorSpace = input.takeEnum(an::ColorSpace::NarrowbandMapping),
                                             .numericDomain = input.takeEnum(an::NumericDomain::PhysicalScaled),
                                             .cfa = cfa,
                                             .coordinateOrigin = input.takeEnum(an::CoordinateOrigin::BottomLeft),
                                             .pixelAspectRatio = pixelAspectRatio};

    const an::DescriptorError reason = an::ImageDescriptor::check(request);
    const std::optional<an::ImageDescriptor> built = an::ImageDescriptor::create(request);
    AN_FUZZ_CHECK((reason == an::DescriptorError::None) == built.has_value(),
                  "a request either builds a descriptor or names the rule that refused it");
    an::fuzz::countReached(built.has_value() ? "requests-built" : "requests-refused");
    if (!built.has_value())
    {
        return;
    }

    AN_FUZZ_CHECK(an::ImageDescriptor::check(built->toRequest()) == an::DescriptorError::None,
                  "a descriptor's own request must build it again");
    const std::optional<an::ImageDescriptor> rebuilt = an::ImageDescriptor::create(built->toRequest());
    AN_FUZZ_CHECK(rebuilt.has_value() && *rebuilt == *built, "rebuilding from toRequest must give the same descriptor");

    checkTextForm(an::formatImageDescriptor(*built), limits);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);
    const an::EngineLimits limits = limitsFrom(input);
    // The mode comes from the back, so the bytes in front stay the text a mutation is editing.
    if (input.controlBelow(2u) == 0u)
    {
        checkTextForm(input.restAsText(), limits);
    }
    else
    {
        checkRequestForm(input, limits);
    }
    return 0;
}
