// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RawPolicyCases.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <utility>

namespace an::tests::fixtures
{
namespace
{

// ---- Inputs.

/// The camera facts both inputs state: black 100 under a twelve-bit white, the as-shot neutral of a camera whose
/// multipliers are 2, 1 and 1.5, and a clockwise quarter turn.
RawPolicyInput cameraFrame(FixtureImage image)
{
    RawPolicyInput input;
    input.image = std::move(image);
    input.blackLevel = 100u;
    input.whiteLevel = 4095u;
    input.asShotNeutral = {1u, 2u, 1u, 1u, 2u, 3u};
    input.orientation = 6u;
    return input;
}

std::vector<RawPolicyInput> makeInputs()
{
    FixtureImage mosaic;
    mosaic.name = "raw-mosaic-rggb-u16";
    mosaic.width = 32u;
    mosaic.height = 24u;
    mosaic.channels = FixtureChannels::Mono;
    mosaic.sampleType = FixtureSampleType::UInt16;
    mosaic.domain = FixtureDomain::SensorCode;
    mosaic.cfa = FixtureCfa::Rggb;
    mosaic.pattern = FixturePattern::GradientNoise;
    mosaic.seed = 20261003u;
    mosaic.levels = {FixtureChannelLevels{.base = 400.0, .perColumn = 60.0, .perRow = 40.0, .amplitude = 150.0}};
    // A code at the white level and one at the black level, so saturation and zero both show.
    mosaic.overrides = {FixtureSampleOverride{.x = 30u, .y = 22u, .channel = 0u, .value = 4095.0},
                        FixtureSampleOverride{.x = 1u, .y = 1u, .channel = 0u, .value = 100.0}};

    FixtureImage colour;
    colour.name = "raw-colour-u16";
    colour.width = 32u;
    colour.height = 24u;
    colour.channels = FixtureChannels::Rgb;
    colour.sampleType = FixtureSampleType::UInt16;
    colour.domain = FixtureDomain::SensorCode;
    colour.pattern = FixturePattern::GradientNoise;
    colour.seed = 20261004u;
    colour.levels = {FixtureChannelLevels{.base = 900.0, .perColumn = 40.0, .perRow = 30.0, .amplitude = 120.0},
                     FixtureChannelLevels{.base = 1400.0, .perColumn = 35.0, .perRow = 25.0, .amplitude = 120.0},
                     FixtureChannelLevels{.base = 600.0, .perColumn = 30.0, .perRow = 45.0, .amplitude = 120.0}};

    return {cameraFrame(std::move(mosaic)), cameraFrame(std::move(colour))};
}

// ---- Cases.

/// Where the product publishes the policy of its persisted record, which every RAW open takes.
constexpr const char* kSource = "src/AfterNightControls/app/PersistenceCoordinator.cpp:193";

/// The gains a `custom` case states.
constexpr std::array<double, 3> kCustomGains{1.8, 1.0, 1.25};

std::string textOf(const RawLoadPolicy& policy)
{
    std::string text = "open the DNG under the product's RAW options: white balance " + policy.whiteBalance;
    if (policy.whiteBalance == "custom")
    {
        text += " with gains " + formatDouble(policy.customGains[0]) + " " + formatDouble(policy.customGains[1]) + " " +
                formatDouble(policy.customGains[2]);
    }
    return text + (policy.autoRotate ? ", turned by the recorded orientation" : ", not turned");
}

RawPolicyCase makeCase(std::string_view mode, const std::string& input, bool autoRotate)
{
    RawPolicyCase item;
    item.operation = "raw-open." + std::string(mode) + (autoRotate ? ".rotated" : "");
    item.input = input;
    item.policy.whiteBalance = std::string(mode);
    item.policy.customGains = kCustomGains;
    item.policy.autoRotate = autoRotate;
    item.text = textOf(item.policy);
    item.source = kSource;
    return item;
}

/// Why a mosaic's codes may differ by one: the record holds every sample below 65535 and was multiplied in single
/// precision, where the step multiplies in double.
constexpr const char* kMosaicNote =
    "the record's bitmap writer holds every sample at 65534 or below, so a saturated code reads 65534 there and "
    "65535 here, and its single-precision product truncates across a code boundary the double product here does not";

/// Why every full-colour record differs from a correct open.
constexpr const char* kInterleavedReason =
    "the record holds a full-colour frame's interleaved samples written across its three planes, each held at 65534 "
    "or below, so every plane holds the wrong samples; written that way, the engine's planes are the record "
    "exactly";

/// The samples each full-colour record differs in, and by how much at most, by white-balance mode.
struct InterleavedDifference
{
    std::string_view mode;
    std::uint64_t differing;
    double bound;
};
constexpr InterleavedDifference kInterleavedDifferences[] = {{"none", 2302u, 40147.0},
                                                             {"daylight", 2302u, 42871.0},
                                                             {"camera_as_shot", 2302u, 58169.0},
                                                             {"auto_wb", 2302u, 39512.0},
                                                             {"custom", 2302u, 56152.0}};

/// States how a case's answer is held to its record.
void classify(RawPolicyCase& item, bool mosaic)
{
    if (mosaic && item.policy.whiteBalance == "auto_wb")
    {
        item.classification = ExpectationClass::Accepted;
        item.acceptedReason = "the record's automatic balance applies a mosaic's daylight gains, where the step "
                              "measures the mosaic's grey world";
        item.differing = 576u;
        item.acceptedBound = 3997.0;
        item.note = "every sample a measured gain moves; the bound is the largest such move over these codes";
        return;
    }
    if (mosaic)
    {
        item.classification = ExpectationClass::Tolerance;
        item.absoluteTolerance = 1.0;
        item.note = kMosaicNote;
        return;
    }
    item.classification = ExpectationClass::Accepted;
    item.recordLayout = item.policy.autoRotate ? RawRecordLayout::InterleavedUnturned : RawRecordLayout::Interleaved;
    item.acceptedReason = kInterleavedReason;
    if (item.policy.autoRotate)
    {
        item.acceptedReason += "; and the record holds a full-colour frame unturned, whatever its options asked";
        item.note = "the record is the engine's unturned answer written the record's way; the turned answer has "
                    "another shape, so no sample count applies";
        return;
    }
    for (const InterleavedDifference& difference : kInterleavedDifferences)
    {
        if (difference.mode == item.policy.whiteBalance)
        {
            item.differing = difference.differing;
            item.acceptedBound = difference.bound;
        }
    }
    item.note = "every sample but the two a plane shares with the interleaved order";
}

std::vector<RawPolicyCase> makeCases()
{
    std::vector<RawPolicyCase> cases;
    for (const RawPolicyInput& input : rawPolicyInputs())
    {
        const bool mosaic = input.image.channels == FixtureChannels::Mono;
        for (const std::string_view mode : kRawWhiteBalanceModes)
        {
            cases.push_back(makeCase(mode, input.image.name, false));
            classify(cases.back(), mosaic);
        }
        cases.push_back(makeCase("daylight", input.image.name, true));
        classify(cases.back(), mosaic);
    }
    return cases;
}

// ---- The DNG writer.

/// The DNG colour indices of a named filter array's top-left cell: 0 red, 1 green, 2 blue.
std::array<std::uint8_t, 4> cfaIndices(FixtureCfa cfa)
{
    switch (cfa)
    {
    case FixtureCfa::Rggb:
        return {0u, 1u, 1u, 2u};
    case FixtureCfa::Bggr:
        return {2u, 1u, 1u, 0u};
    case FixtureCfa::Grbg:
        return {1u, 0u, 2u, 1u};
    case FixtureCfa::Gbrg:
        return {1u, 2u, 0u, 1u};
    case FixtureCfa::None:
        break;
    }
    return {0u, 0u, 0u, 0u};
}

/// Little-endian values of one width, concatenated.
std::vector<std::uint8_t> words(std::initializer_list<std::uint32_t> values, unsigned size = 4u)
{
    std::vector<std::uint8_t> bytes;
    for (const std::uint32_t value : values)
    {
        for (unsigned i = 0u; i < size; ++i)
        {
            bytes.push_back(static_cast<std::uint8_t>(value >> (8u * i)));
        }
    }
    return bytes;
}

/// One directory entry: tag, field type, count and the value's bytes.
struct Tag
{
    std::uint16_t key;
    std::uint16_t type;
    std::uint32_t count;
    std::vector<std::uint8_t> bytes;
};

} // namespace

const std::vector<RawPolicyInput>& rawPolicyInputs()
{
    static const std::vector<RawPolicyInput> inputs = makeInputs();
    return inputs;
}

const RawPolicyInput* rawPolicyInput(std::string_view name)
{
    for (const RawPolicyInput& input : rawPolicyInputs())
    {
        if (input.image.name == name)
        {
            return &input;
        }
    }
    return nullptr;
}

const std::vector<RawPolicyCase>& rawPolicyCases()
{
    static const std::vector<RawPolicyCase> cases = makeCases();
    return cases;
}

const RawPolicyCase* rawPolicyCase(std::string_view id)
{
    for (const RawPolicyCase& item : rawPolicyCases())
    {
        if (item.id() == id)
        {
            return &item;
        }
    }
    return nullptr;
}

bool writeRawPolicyDng(const RawPolicyInput& input, const FixtureSamples& samples, const std::filesystem::path& path,
                       std::string& error)
{
    const FixtureImage& image = input.image;
    const bool mosaic = image.channels == FixtureChannels::Mono;
    const std::uint32_t channels = channelCountOf(image.channels);
    if (samples.width() != image.width || samples.height() != image.height || samples.channelCount() != channels ||
        samples.sampleType() != FixtureSampleType::UInt16 || (mosaic && image.cfa == FixtureCfa::None) ||
        (!mosaic && image.cfa != FixtureCfa::None))
    {
        error = image.name + ": a DNG holds sixteen-bit codes, one channel under a filter array or three without one";
        return false;
    }
    const auto& neutral = input.asShotNeutral;
    const std::uint32_t payloadBytes = image.width * image.height * channels * 2u;
    std::vector<Tag> tags{
        {254, 4, 1, words({0u})},
        {256, 4, 1, words({image.width})},
        {257, 4, 1, words({image.height})},
        {258, 3, channels, mosaic ? words({16u}, 2u) : words({16u, 16u, 16u}, 2u)},
        {259, 3, 1, words({1u}, 2u)},
        {262, 3, 1, words({mosaic ? 32803u : 34892u}, 2u)},
        {271, 2, 5, {'T', 'e', 's', 't', 0}},
        {272, 2, 7, {'C', 'a', 'm', 'e', 'r', 'a', 0}},
        {273, 4, 1, words({0u})},
        {274, 3, 1, words({input.orientation}, 2u)},
        {277, 3, 1, words({channels}, 2u)},
        {278, 4, 1, words({image.height})},
        {279, 4, 1, words({payloadBytes})},
        {284, 3, 1, words({1u}, 2u)},
        {50706, 1, 4, {1, 4, 0, 0}},
        {50707, 1, 4, {1, 1, 0, 0}},
        {50708, 2, 12, {'T', 'e', 's', 't', ' ', 'C', 'a', 'm', 'e', 'r', 'a', 0}},
        {50714, 5, 1, words({input.blackLevel, 1u})},
        {50717, 4, 1, words({input.whiteLevel})},
        {50721, 10, 9, words({1u, 1u, 0u, 1u, 0u, 1u, 0u, 1u, 1u, 1u, 0u, 1u, 0u, 1u, 0u, 1u, 1u, 1u})},
        {50728, 5, 3, words({neutral[0], neutral[1], neutral[2], neutral[3], neutral[4], neutral[5]})},
        {50778, 3, 1, words({21u}, 2u)}};
    if (mosaic)
    {
        const std::array<std::uint8_t, 4> indices = cfaIndices(image.cfa);
        tags.push_back({33421, 3, 2, words({2u, 2u}, 2u)});
        tags.push_back({33422, 1, 4, {indices.begin(), indices.end()}});
        tags.push_back({50710, 1, 3, {0, 1, 2}});
        tags.push_back({50711, 3, 1, words({1u}, 2u)});
    }
    std::sort(tags.begin(), tags.end(), [](const Tag& left, const Tag& right) { return left.key < right.key; });

    std::vector<std::uint8_t> bytes{'I', 'I', 42, 0, 8, 0, 0, 0};
    const auto append = [&bytes](const std::vector<std::uint8_t>& part) {
        bytes.insert(bytes.end(), part.begin(), part.end());
    };
    append(words({static_cast<std::uint32_t>(tags.size())}, 2u));
    const std::uint32_t extraStart = 8u + 2u + static_cast<std::uint32_t>(tags.size()) * 12u + 4u;
    std::vector<std::uint8_t> extra;
    std::size_t stripOffset = 0u;
    for (const Tag& tag : tags)
    {
        append(words({tag.key, tag.type}, 2u));
        append(words({tag.count}));
        if (tag.key == 273)
        {
            stripOffset = bytes.size();
        }
        if (tag.bytes.size() <= 4u)
        {
            append(tag.bytes);
            bytes.resize(bytes.size() + 4u - tag.bytes.size(), 0u);
        }
        else
        {
            append(words({extraStart + static_cast<std::uint32_t>(extra.size())}));
            extra.insert(extra.end(), tag.bytes.begin(), tag.bytes.end());
            if (extra.size() % 2u != 0u)
            {
                extra.push_back(0u);
            }
        }
    }
    append(words({0u}));
    append(extra);
    const std::vector<std::uint8_t> offset = words({static_cast<std::uint32_t>(bytes.size())});
    std::copy(offset.begin(), offset.end(), bytes.begin() + static_cast<std::ptrdiff_t>(stripOffset));
    for (std::uint32_t y = 0u; y < image.height; ++y)
    {
        for (std::uint32_t x = 0u; x < image.width; ++x)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                append(words({static_cast<std::uint32_t>(samples.value(x, y, channel))}, 2u));
            }
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output)
    {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

} // namespace an::tests::fixtures
