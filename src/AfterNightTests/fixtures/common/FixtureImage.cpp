// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureImage.h"

#include "FixtureDigest.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <system_error>

namespace an::tests::fixtures
{

static_assert(std::endian::native == std::endian::little,
              "fixture samples are stated little-endian, and they are held in memory as the host stores them");

namespace
{

// The keywords the neutral file layout writes itself, and the commentary keywords that carry no value; a description
// may carry neither. NAXISn is refused by its prefix.
constexpr std::string_view kReservedKeywords[] = {"SIMPLE",  "BITPIX",  "NAXIS",    "EXTEND",   "BZERO",
                                                  "BSCALE",  "BLANK",   "BLOCKED",  "ROWORDER", "BAYERPAT",
                                                  "COMMENT", "HISTORY", "CONTINUE", "END"};

// The longest card value and comment a description may state: what every recorded header carries unchanged.
constexpr std::size_t kLongestCardValue = 32u;
constexpr std::size_t kLongestCardComment = 24u;

[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept
{
    value ^= value >> 30u;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27u;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31u;
    return value;
}

// A value in [-1, 1) that depends on nothing but the seed, the pixel and the channel.
[[nodiscard]] double noiseUnit(std::uint64_t seed, std::uint32_t x, std::uint32_t y, std::uint32_t channel) noexcept
{
    const std::uint64_t key = mix(seed ^ 0x6A09E667F3BCC909ull) ^
                              (static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull) ^
                              (static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full) ^
                              (static_cast<std::uint64_t>(channel) * 0x165667B19E3779F9ull);
    const double unit = static_cast<double>(mix(mix(key)) >> 11u) / 9007199254740992.0;
    return 2.0 * unit - 1.0;
}

[[nodiscard]] double patternValue(const FixtureImage& image, std::uint32_t x, std::uint32_t y,
                                  std::uint32_t channel) noexcept
{
    const FixtureChannelLevels& levels = image.levels[channel];
    switch (image.pattern)
    {
    case FixturePattern::Constant:
        return levels.base;
    case FixturePattern::Gradient:
        return levels.base + levels.perColumn * static_cast<double>(x) + levels.perRow * static_cast<double>(y);
    case FixturePattern::Checker:
        return ((x / image.checkerCell) + (y / image.checkerCell)) % 2u == 0u ? levels.base : levels.alternate;
    case FixturePattern::Noise:
        return levels.base + levels.amplitude * noiseUnit(image.seed, x, y, channel);
    case FixturePattern::GradientNoise:
        return levels.base + levels.perColumn * static_cast<double>(x) + levels.perRow * static_cast<double>(y) +
               levels.amplitude * noiseUnit(image.seed, x, y, channel);
    case FixturePattern::StarField: {
        double stars = 0.0;
        for (const FixtureStar& star : image.stars)
        {
            const double dx = static_cast<double>(x) - star.x;
            const double dy = static_cast<double>(y) - star.y;
            const double spread = 1.0 + (dx * dx + dy * dy) / (star.width * star.width);
            stars += star.peak / (spread * spread);
        }
        return levels.base + levels.perColumn * static_cast<double>(x) + levels.perRow * static_cast<double>(y) +
               levels.amplitude * noiseUnit(image.seed, x, y, channel) + levels.alternate * stars;
    }
    }
    return levels.base;
}

// Nearest, with a tie going to the even neighbour, independent of the floating-point environment.
[[nodiscard]] double roundHalfEven(double value) noexcept
{
    const double lower = std::floor(value);
    const double fraction = value - lower;
    if (fraction > 0.5)
    {
        return lower + 1.0;
    }
    if (fraction < 0.5)
    {
        return lower;
    }
    return std::fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}

template <typename SampleT> void storeSample(std::byte* at, double value) noexcept
{
    const SampleT typed = static_cast<SampleT>(value);
    std::memcpy(at, &typed, sizeof(SampleT));
}

template <typename SampleT> [[nodiscard]] double loadSample(const std::byte* at) noexcept
{
    SampleT typed{};
    std::memcpy(&typed, at, sizeof(SampleT));
    return static_cast<double>(typed);
}

[[nodiscard]] bool printable(std::string_view text) noexcept
{
    return std::all_of(text.begin(), text.end(), [](char character) {
        const auto code = static_cast<unsigned char>(character);
        return code >= 0x20u && code <= 0x7Eu;
    });
}

[[nodiscard]] std::optional<std::string> checkCard(const FixtureCard& card)
{
    const std::string& keyword = card.keyword;
    if (keyword.empty() || keyword.size() > 8u || !std::all_of(keyword.begin(), keyword.end(), [](char character) {
            return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                   character == '-' || character == '_';
        }))
    {
        return "the card keyword '" + keyword +
               "' is not one to eight upper-case letters, digits, hyphens and "
               "underscores";
    }
    const bool numbered = keyword.rfind("NAXIS", 0) == 0;
    if (numbered ||
        std::find(std::begin(kReservedKeywords), std::end(kReservedKeywords), keyword) != std::end(kReservedKeywords))
    {
        return "the card keyword " + keyword + " is one the file layout writes itself";
    }
    if (!printable(card.value) || card.value.size() > kLongestCardValue)
    {
        return "the value of " + keyword + " is not at most " + std::to_string(kLongestCardValue) +
               " printable ASCII characters";
    }
    if (!printable(card.comment) || card.comment.size() > kLongestCardComment)
    {
        return "the comment of " + keyword + " is not at most " + std::to_string(kLongestCardComment) +
               " printable ASCII characters";
    }
    switch (card.kind)
    {
    case FixtureCardKind::Text:
        if (card.value.empty() || card.value.front() == ' ' || card.value.back() == ' ')
        {
            return "the text of " + keyword + " is empty or starts or ends with a space";
        }
        break;
    case FixtureCardKind::Integer: {
        long long parsed = 0;
        const char* end = card.value.data() + card.value.size();
        const auto [at, error] =
            std::from_chars(card.value.data() + (card.value.rfind('+', 0) == 0 ? 1 : 0), end, parsed);
        if (error != std::errc() || at != end || card.value.empty())
        {
            return "the integer card " + keyword + " holds '" + card.value + "'";
        }
        break;
    }
    case FixtureCardKind::Real: {
        const std::optional<double> parsed = parseDouble(card.value);
        if (!parsed.has_value() || !std::isfinite(*parsed) || card.value.find_first_of(" /'") != std::string::npos)
        {
            return "the real card " + keyword + " holds '" + card.value + "'";
        }
        break;
    }
    case FixtureCardKind::Logical:
        if (card.value != "T" && card.value != "F")
        {
            return "the logical card " + keyword + " holds '" + card.value + "', not T or F";
        }
        break;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view cardKindName(FixtureCardKind kind) noexcept
{
    switch (kind)
    {
    case FixtureCardKind::Text:
        return "text";
    case FixtureCardKind::Integer:
        return "integer";
    case FixtureCardKind::Real:
        return "real";
    case FixtureCardKind::Logical:
        return "logical";
    }
    return "unknown";
}

// A field of the canonical text that may hold any character: its length, a colon, then itself.
[[nodiscard]] std::string counted(std::string_view text)
{
    return std::to_string(text.size()) + ":" + std::string(text);
}

} // namespace

FixtureSamples::FixtureSamples(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
                               FixtureSampleType sampleType)
    : m_width(width), m_height(height), m_channelCount(channelCount), m_sampleType(sampleType),
      m_bytes(static_cast<std::size_t>(width) * height * channelCount * sampleSizeOf(sampleType), std::byte{0})
{
}

std::uint32_t FixtureSamples::width() const noexcept
{
    return m_width;
}

std::uint32_t FixtureSamples::height() const noexcept
{
    return m_height;
}

std::uint32_t FixtureSamples::channelCount() const noexcept
{
    return m_channelCount;
}

FixtureSampleType FixtureSamples::sampleType() const noexcept
{
    return m_sampleType;
}

bool FixtureSamples::empty() const noexcept
{
    return m_bytes.empty();
}

std::span<const std::byte> FixtureSamples::bytes() const noexcept
{
    return m_bytes;
}

std::span<std::byte> FixtureSamples::mutableBytes() noexcept
{
    return m_bytes;
}

std::span<const std::byte> FixtureSamples::plane(std::uint32_t channel) const noexcept
{
    if (channel >= m_channelCount)
    {
        return {};
    }
    const std::size_t planeBytes = static_cast<std::size_t>(m_width) * m_height * sampleSizeOf(m_sampleType);
    return std::span<const std::byte>(m_bytes).subspan(channel * planeBytes, planeBytes);
}

std::span<std::byte> FixtureSamples::mutablePlane(std::uint32_t channel) noexcept
{
    if (channel >= m_channelCount)
    {
        return {};
    }
    const std::size_t planeBytes = static_cast<std::size_t>(m_width) * m_height * sampleSizeOf(m_sampleType);
    return std::span<std::byte>(m_bytes).subspan(channel * planeBytes, planeBytes);
}

double FixtureSamples::value(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const
{
    if (x >= m_width || y >= m_height || channel >= m_channelCount)
    {
        return 0.0;
    }
    const std::size_t index = (static_cast<std::size_t>(channel) * m_height + y) * m_width + x;
    const std::byte* at = m_bytes.data() + index * sampleSizeOf(m_sampleType);
    switch (m_sampleType)
    {
    case FixtureSampleType::UInt8:
        return loadSample<std::uint8_t>(at);
    case FixtureSampleType::Int8:
        return loadSample<std::int8_t>(at);
    case FixtureSampleType::UInt16:
        return loadSample<std::uint16_t>(at);
    case FixtureSampleType::Int16:
        return loadSample<std::int16_t>(at);
    case FixtureSampleType::UInt32:
        return loadSample<std::uint32_t>(at);
    case FixtureSampleType::Int32:
        return loadSample<std::int32_t>(at);
    case FixtureSampleType::Float32:
        return loadSample<float>(at);
    case FixtureSampleType::Float64:
        return loadSample<double>(at);
    }
    return 0.0;
}

void FixtureSamples::setValue(std::uint32_t x, std::uint32_t y, std::uint32_t channel, double value)
{
    if (x >= m_width || y >= m_height || channel >= m_channelCount)
    {
        return;
    }
    const std::size_t index = (static_cast<std::size_t>(channel) * m_height + y) * m_width + x;
    std::byte* at = m_bytes.data() + index * sampleSizeOf(m_sampleType);
    switch (m_sampleType)
    {
    case FixtureSampleType::UInt8:
        storeSample<std::uint8_t>(at, value);
        break;
    case FixtureSampleType::Int8:
        storeSample<std::int8_t>(at, value);
        break;
    case FixtureSampleType::UInt16:
        storeSample<std::uint16_t>(at, value);
        break;
    case FixtureSampleType::Int16:
        storeSample<std::int16_t>(at, value);
        break;
    case FixtureSampleType::UInt32:
        storeSample<std::uint32_t>(at, value);
        break;
    case FixtureSampleType::Int32:
        storeSample<std::int32_t>(at, value);
        break;
    case FixtureSampleType::Float32:
        storeSample<float>(at, value);
        break;
    case FixtureSampleType::Float64:
        storeSample<double>(at, value);
        break;
    }
}

std::string FixtureSamples::digest() const
{
    return sha256Hex(bytes());
}

std::uint32_t channelCountOf(FixtureChannels channels) noexcept
{
    return channels == FixtureChannels::Rgb ? 3u : 1u;
}

std::size_t sampleSizeOf(FixtureSampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
    case FixtureSampleType::Int8:
        return 1u;
    case FixtureSampleType::UInt16:
    case FixtureSampleType::Int16:
        return 2u;
    case FixtureSampleType::UInt32:
    case FixtureSampleType::Int32:
    case FixtureSampleType::Float32:
        return 4u;
    case FixtureSampleType::Float64:
        return 8u;
    }
    return 0u;
}

bool isFloating(FixtureSampleType sampleType) noexcept
{
    return sampleType == FixtureSampleType::Float32 || sampleType == FixtureSampleType::Float64;
}

double lowestOf(FixtureSampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
    case FixtureSampleType::UInt16:
    case FixtureSampleType::UInt32:
        return 0.0;
    case FixtureSampleType::Int8:
        return (std::numeric_limits<std::int8_t>::min)();
    case FixtureSampleType::Int16:
        return (std::numeric_limits<std::int16_t>::min)();
    case FixtureSampleType::Int32:
        return (std::numeric_limits<std::int32_t>::min)();
    case FixtureSampleType::Float32:
        return std::numeric_limits<float>::lowest();
    case FixtureSampleType::Float64:
        return std::numeric_limits<double>::lowest();
    }
    return 0.0;
}

double highestOf(FixtureSampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
        return (std::numeric_limits<std::uint8_t>::max)();
    case FixtureSampleType::Int8:
        return (std::numeric_limits<std::int8_t>::max)();
    case FixtureSampleType::UInt16:
        return (std::numeric_limits<std::uint16_t>::max)();
    case FixtureSampleType::Int16:
        return (std::numeric_limits<std::int16_t>::max)();
    case FixtureSampleType::UInt32:
        return (std::numeric_limits<std::uint32_t>::max)();
    case FixtureSampleType::Int32:
        return (std::numeric_limits<std::int32_t>::max)();
    case FixtureSampleType::Float32:
        return (std::numeric_limits<float>::max)();
    case FixtureSampleType::Float64:
        return (std::numeric_limits<double>::max)();
    }
    return 0.0;
}

std::string_view sampleTypeName(FixtureSampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
        return "uint8";
    case FixtureSampleType::Int8:
        return "int8";
    case FixtureSampleType::UInt16:
        return "uint16";
    case FixtureSampleType::Int16:
        return "int16";
    case FixtureSampleType::UInt32:
        return "uint32";
    case FixtureSampleType::Int32:
        return "int32";
    case FixtureSampleType::Float32:
        return "float32";
    case FixtureSampleType::Float64:
        return "float64";
    }
    return "unknown";
}

std::optional<FixtureSampleType> sampleTypeFromName(std::string_view name) noexcept
{
    for (const FixtureSampleType sampleType :
         {FixtureSampleType::UInt8, FixtureSampleType::Int8, FixtureSampleType::UInt16, FixtureSampleType::Int16,
          FixtureSampleType::UInt32, FixtureSampleType::Int32, FixtureSampleType::Float32, FixtureSampleType::Float64})
    {
        if (sampleTypeName(sampleType) == name)
        {
            return sampleType;
        }
    }
    return std::nullopt;
}

std::string_view channelsName(FixtureChannels channels) noexcept
{
    return channels == FixtureChannels::Rgb ? "rgb" : "mono";
}

std::string_view domainName(FixtureDomain domain) noexcept
{
    switch (domain)
    {
    case FixtureDomain::NormalizedLinear:
        return "normalized-linear";
    case FixtureDomain::SensorCode:
        return "sensor-code";
    case FixtureDomain::PhysicalScaled:
        return "physical-scaled";
    }
    return "unknown";
}

std::string_view patternName(FixturePattern pattern) noexcept
{
    switch (pattern)
    {
    case FixturePattern::Constant:
        return "constant";
    case FixturePattern::Gradient:
        return "gradient";
    case FixturePattern::Checker:
        return "checker";
    case FixturePattern::Noise:
        return "noise";
    case FixturePattern::GradientNoise:
        return "gradient-noise";
    case FixturePattern::StarField:
        return "star-field";
    }
    return "unknown";
}

std::string_view cfaName(FixtureCfa cfa) noexcept
{
    switch (cfa)
    {
    case FixtureCfa::None:
        return {};
    case FixtureCfa::Rggb:
        return "RGGB";
    case FixtureCfa::Bggr:
        return "BGGR";
    case FixtureCfa::Grbg:
        return "GRBG";
    case FixtureCfa::Gbrg:
        return "GBRG";
    }
    return {};
}

std::optional<FixtureCfa> cfaFromName(std::string_view name) noexcept
{
    for (const FixtureCfa cfa : {FixtureCfa::Rggb, FixtureCfa::Bggr, FixtureCfa::Grbg, FixtureCfa::Gbrg})
    {
        if (cfaName(cfa) == name)
        {
            return cfa;
        }
    }
    return std::nullopt;
}

double quantizeSample(double value, FixtureSampleType sampleType) noexcept
{
    if (sampleType == FixtureSampleType::Float64)
    {
        return value;
    }
    if (sampleType == FixtureSampleType::Float32)
    {
        // Beyond the midpoint between the largest float and the next power of two, nearest rounding reaches the
        // infinity of the value's sign; everything nearer is an ordinary conversion.
        constexpr double kOverflow = 3.4028235677973366e38;
        if (std::isfinite(value) && std::fabs(value) >= kOverflow)
        {
            return std::copysign(std::numeric_limits<double>::infinity(), value);
        }
        return static_cast<double>(static_cast<float>(value));
    }
    if (std::isnan(value))
    {
        return 0.0;
    }
    return std::clamp(roundHalfEven(value), lowestOf(sampleType), highestOf(sampleType));
}

std::optional<std::string> checkFixture(const FixtureImage& image)
{
    if (image.name.empty() || !std::all_of(image.name.begin(), image.name.end(), [](char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.';
        }))
    {
        return "a fixture is named by letters, digits, dots, hyphens and underscores, and '" + image.name + "' is not";
    }
    const std::string where = image.name + ": ";
    if (image.width == 0u || image.height == 0u)
    {
        return where + "the image has no pixels";
    }
    const std::uint32_t channels = channelCountOf(image.channels);
    if (image.levels.size() != channels)
    {
        return where + "it states levels for " + std::to_string(image.levels.size()) + " channels and has " +
               std::to_string(channels);
    }
    for (const FixtureChannelLevels& levels : image.levels)
    {
        for (const double parameter :
             {levels.base, levels.perColumn, levels.perRow, levels.alternate, levels.amplitude})
        {
            if (!std::isfinite(parameter))
            {
                return where + "a pattern parameter is not finite; state a non-finite sample as an override";
            }
        }
    }
    if (!image.stars.empty() && image.pattern != FixturePattern::StarField)
    {
        return where + "a star belongs to a star field only";
    }
    for (const FixtureStar& star : image.stars)
    {
        if (!std::isfinite(star.x) || !std::isfinite(star.y) || !std::isfinite(star.width) || !std::isfinite(star.peak))
        {
            return where + "a star's position, width and peak are finite";
        }
        if (star.width <= 0.0)
        {
            return where + "a star's width is positive";
        }
    }
    if (image.pattern == FixturePattern::Checker && image.checkerCell == 0u)
    {
        return where + "a checker needs cells of at least one pixel";
    }
    if (image.domain == FixtureDomain::PhysicalScaled && !isFloating(image.sampleType))
    {
        return where + "a physical quantity needs a floating representation";
    }
    if (image.cfa != FixtureCfa::None && image.channels != FixtureChannels::Mono)
    {
        return where + "a filter array covers a mono image only";
    }
    for (const FixtureSampleOverride& sample : image.overrides)
    {
        if (sample.x >= image.width || sample.y >= image.height || sample.channel >= channels)
        {
            return where + "an override at " + std::to_string(sample.x) + "," + std::to_string(sample.y) + " channel " +
                   std::to_string(sample.channel) + " is outside the image";
        }
        if (!isFloating(image.sampleType) && !std::isfinite(sample.value))
        {
            return where + "a non-finite sample needs a floating representation";
        }
    }
    for (const FixtureCard& card : image.cards)
    {
        if (const std::optional<std::string> reason = checkCard(card))
        {
            return where + *reason;
        }
    }
    return std::nullopt;
}

std::optional<FixtureSamples> materializeFixture(const FixtureImage& image, std::string& error)
{
    if (const std::optional<std::string> reason = checkFixture(image))
    {
        error = *reason;
        return std::nullopt;
    }
    FixtureSamples samples(image.width, image.height, channelCountOf(image.channels), image.sampleType);
    for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < image.height; ++y)
        {
            for (std::uint32_t x = 0u; x < image.width; ++x)
            {
                samples.setValue(x, y, channel, quantizeSample(patternValue(image, x, y, channel), image.sampleType));
            }
        }
    }
    for (const FixtureSampleOverride& sample : image.overrides)
    {
        samples.setValue(sample.x, sample.y, sample.channel, quantizeSample(sample.value, image.sampleType));
    }
    return samples;
}

std::string describeFixture(const FixtureImage& image)
{
    std::string text;
    text += "fixture " + counted(image.name) + "\n";
    text += "size " + std::to_string(image.width) + " " + std::to_string(image.height) + "\n";
    text += "channels " + std::string(channelsName(image.channels)) + "\n";
    text += "sample " + std::string(sampleTypeName(image.sampleType)) + "\n";
    text += "domain " + std::string(domainName(image.domain)) + "\n";
    text += "cfa " + (image.cfa == FixtureCfa::None ? std::string("none") : std::string(cfaName(image.cfa))) + "\n";
    text += "pattern " + std::string(patternName(image.pattern)) + " cell " + std::to_string(image.checkerCell) +
            " seed " + std::to_string(image.seed) + "\n";
    for (std::size_t channel = 0u; channel < image.levels.size(); ++channel)
    {
        const FixtureChannelLevels& levels = image.levels[channel];
        text += "level " + std::to_string(channel) + " base " + formatDouble(levels.base) + " column " +
                formatDouble(levels.perColumn) + " row " + formatDouble(levels.perRow) + " alternate " +
                formatDouble(levels.alternate) + " amplitude " + formatDouble(levels.amplitude) + "\n";
    }
    for (const FixtureSampleOverride& sample : image.overrides)
    {
        text += "override " + std::to_string(sample.x) + " " + std::to_string(sample.y) + " " +
                std::to_string(sample.channel) + " " + formatDouble(sample.value) + "\n";
    }
    for (const FixtureCard& card : image.cards)
    {
        text += "card " + counted(card.keyword) + " " + std::string(cardKindName(card.kind)) + " " +
                counted(card.value) + " " + counted(card.comment) + "\n";
    }
    // Stars are written only when there are any, so the text of every description made before stars existed - and
    // the digest its captured records state - is unchanged.
    for (const FixtureStar& star : image.stars)
    {
        text += "star " + formatDouble(star.x) + " " + formatDouble(star.y) + " width " + formatDouble(star.width) +
                " peak " + formatDouble(star.peak) + "\n";
    }
    return text;
}

std::string fixtureDigest(const FixtureImage& image)
{
    return sha256Hex(describeFixture(image));
}

bool sameSamples(const FixtureSamples& left, const FixtureSamples& right)
{
    if (left.width() != right.width() || left.height() != right.height() ||
        left.channelCount() != right.channelCount() || left.sampleType() != right.sampleType())
    {
        return false;
    }
    if (!isFloating(left.sampleType()))
    {
        return std::equal(left.bytes().begin(), left.bytes().end(), right.bytes().begin(), right.bytes().end());
    }
    const std::size_t size = sampleSizeOf(left.sampleType());
    const std::span<const std::byte> first = left.bytes();
    const std::span<const std::byte> second = right.bytes();
    for (std::size_t offset = 0u; offset < first.size(); offset += size)
    {
        if (std::memcmp(first.data() + offset, second.data() + offset, size) == 0)
        {
            continue;
        }
        const bool bothNan = size == 4u ? std::isnan(loadSample<float>(first.data() + offset)) &&
                                              std::isnan(loadSample<float>(second.data() + offset))
                                        : std::isnan(loadSample<double>(first.data() + offset)) &&
                                              std::isnan(loadSample<double>(second.data() + offset));
        if (!bothNan)
        {
            return false;
        }
    }
    return true;
}

std::string formatDouble(double value)
{
    if (std::isnan(value))
    {
        // The quiet NaN is `nan`; any other - the negative one a processor returns for 0/0, or one with a payload - is
        // written with its bits, so that samples read back from text are the samples that were written.
        const auto bits = std::bit_cast<std::uint64_t>(value);
        if (bits == std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()))
        {
            return "nan";
        }
        char hex[17];
        const auto [end, error] = std::to_chars(hex, hex + sizeof(hex), bits, 16);
        return "nan(0x" + std::string(hex, error == std::errc() ? end : hex) + ")";
    }
    if (std::isinf(value))
    {
        return value > 0.0 ? "inf" : "-inf";
    }
    char buffer[64];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return error == std::errc() ? std::string(buffer, end) : std::string("nan");
}

std::optional<double> parseDouble(std::string_view text) noexcept
{
    if (text == "nan")
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (text.size() > 7u && text.substr(0u, 6u) == "nan(0x" && text.back() == ')')
    {
        const std::string_view digits = text.substr(6u, text.size() - 7u);
        std::uint64_t bits = 0u;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), bits, 16);
        const double value = std::bit_cast<double>(bits);
        if (error != std::errc() || end != digits.data() + digits.size() || !std::isnan(value))
        {
            return std::nullopt;
        }
        return value;
    }
    if (text == "inf")
    {
        return std::numeric_limits<double>::infinity();
    }
    if (text == "-inf")
    {
        return -std::numeric_limits<double>::infinity();
    }
    if (text.empty())
    {
        return std::nullopt;
    }
    double value = 0.0;
    const char* first = text.data() + (text.front() == '+' ? 1 : 0);
    const auto [end, error] = std::from_chars(first, text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || std::isnan(value))
    {
        return std::nullopt;
    }
    return value;
}

} // namespace an::tests::fixtures
