// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureFits.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace an::tests::fixtures
{

namespace
{

constexpr std::size_t kRecordBytes = 2880u;
constexpr std::size_t kCardBytes = 80u;

// The BITPIX a representation is stored with, and the BZERO that makes an unsigned sixteen- or thirty-two-bit, or a
// signed eight-bit, value fit the signed or unsigned width FITS stores.
struct StoredForm
{
    int bitpix = 0;
    double bzero = 0.0;
};

[[nodiscard]] StoredForm storedFormOf(FixtureSampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case FixtureSampleType::UInt8:
        return {8, 0.0};
    case FixtureSampleType::Int8:
        return {8, -128.0};
    case FixtureSampleType::UInt16:
        return {16, 32768.0};
    case FixtureSampleType::Int16:
        return {16, 0.0};
    case FixtureSampleType::UInt32:
        return {32, 2147483648.0};
    case FixtureSampleType::Int32:
        return {32, 0.0};
    case FixtureSampleType::Float32:
        return {-32, 0.0};
    case FixtureSampleType::Float64:
        return {-64, 0.0};
    }
    return {};
}

[[nodiscard]] std::optional<FixtureSampleType> sampleTypeOfStored(int bitpix, double bzero) noexcept
{
    for (const FixtureSampleType sampleType :
         {FixtureSampleType::UInt8, FixtureSampleType::Int8, FixtureSampleType::UInt16, FixtureSampleType::Int16,
          FixtureSampleType::UInt32, FixtureSampleType::Int32, FixtureSampleType::Float32, FixtureSampleType::Float64})
    {
        const StoredForm form = storedFormOf(sampleType);
        if (form.bitpix == bitpix && form.bzero == bzero)
        {
            return sampleType;
        }
    }
    return std::nullopt;
}

// One card: the keyword in columns one to eight, `= ` in nine and ten, a number or logical right-justified to column
// thirty or a quoted string from column eleven, and ` / ` with the comment after it.
[[nodiscard]] std::optional<std::string> formatCard(std::string_view keyword, std::string_view value,
                                                    FixtureCardKind kind, std::string_view comment)
{
    std::string card(keyword);
    card.resize(8u, ' ');
    card += "= ";
    if (kind == FixtureCardKind::Text)
    {
        std::string quoted = "'";
        for (const char character : value)
        {
            quoted += character;
            if (character == '\'')
            {
                quoted += '\'';
            }
        }
        while (quoted.size() < 9u)
        {
            quoted += ' ';
        }
        card += quoted + "'";
    }
    else
    {
        if (value.size() < 20u)
        {
            card += std::string(20u - value.size(), ' ');
        }
        card += value;
    }
    if (!comment.empty())
    {
        card += " / ";
        card += comment;
    }
    if (card.size() > kCardBytes)
    {
        return std::nullopt;
    }
    card.resize(kCardBytes, ' ');
    return card;
}

[[nodiscard]] std::string trimmed(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string_view::npos)
    {
        return {};
    }
    return std::string(text.substr(first, text.find_last_not_of(' ') - first + 1u));
}

// A card's value and comment, from the text after `= `.
void parseValue(std::string_view text, FitsHeaderCard& card)
{
    const std::size_t start = text.find_first_not_of(' ');
    if (start == std::string_view::npos)
    {
        return;
    }
    std::size_t at = start;
    if (text[at] == '\'')
    {
        std::string value;
        ++at;
        while (at < text.size())
        {
            if (text[at] == '\'')
            {
                if (at + 1u < text.size() && text[at + 1u] == '\'')
                {
                    value += '\'';
                    at += 2u;
                    continue;
                }
                ++at;
                break;
            }
            value += text[at++];
        }
        while (!value.empty() && value.back() == ' ')
        {
            value.pop_back();
        }
        card.value = value;
    }
    else
    {
        const std::size_t slash = text.find('/', at);
        card.value = trimmed(text.substr(at, slash == std::string_view::npos ? std::string_view::npos : slash - at));
        at = slash == std::string_view::npos ? text.size() : slash;
    }
    const std::size_t slash = text.find('/', at);
    if (slash != std::string_view::npos)
    {
        card.comment = trimmed(text.substr(slash + 1u));
    }
}

template <typename StoredT> void appendBigEndian(std::vector<char>& out, StoredT stored)
{
    const auto bits = std::bit_cast<std::conditional_t<
        sizeof(StoredT) == 1u, std::uint8_t,
        std::conditional_t<sizeof(StoredT) == 2u, std::uint16_t,
                           std::conditional_t<sizeof(StoredT) == 4u, std::uint32_t, std::uint64_t>>>>(stored);
    for (int shift = static_cast<int>(sizeof(StoredT) * 8u) - 8; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<char>((bits >> shift) & 0xFFu));
    }
}

[[nodiscard]] std::uint64_t readBigEndian(const unsigned char* at, std::size_t size) noexcept
{
    std::uint64_t bits = 0u;
    for (std::size_t index = 0u; index < size; ++index)
    {
        bits = (bits << 8u) | at[index];
    }
    return bits;
}

[[nodiscard]] std::optional<long long> integerOf(const std::string& text) noexcept
{
    long long value = 0;
    const char* first = text.data() + (text.rfind('+', 0) == 0 ? 1 : 0);
    const auto [end, error] = std::from_chars(first, text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::string> FixtureFitsFile::card(std::string_view keyword) const
{
    for (const FitsHeaderCard& entry : cards)
    {
        if (entry.keyword == keyword)
        {
            return entry.value;
        }
    }
    return std::nullopt;
}

bool writeFixtureFits(const FixtureImage& image, const FixtureSamples& samples, const std::filesystem::path& path,
                      std::string& error, FixtureRowOrder rowOrder)
{
    if (const std::optional<std::string> reason = checkFixture(image))
    {
        error = *reason;
        return false;
    }
    const std::uint32_t channels = channelCountOf(image.channels);
    if (samples.width() != image.width || samples.height() != image.height || samples.channelCount() != channels ||
        samples.sampleType() != image.sampleType)
    {
        error = image.name + ": the samples are " + std::to_string(samples.width()) + "x" +
                std::to_string(samples.height()) + "x" + std::to_string(samples.channelCount()) + " " +
                std::string(sampleTypeName(samples.sampleType())) + ", and the description is " +
                std::to_string(image.width) + "x" + std::to_string(image.height) + "x" + std::to_string(channels) +
                " " + std::string(sampleTypeName(image.sampleType));
        return false;
    }

    const StoredForm form = storedFormOf(image.sampleType);
    std::vector<std::optional<std::string>> cards;
    cards.push_back(formatCard("SIMPLE", "T", FixtureCardKind::Logical, "a FITS file"));
    cards.push_back(formatCard("BITPIX", std::to_string(form.bitpix), FixtureCardKind::Integer, ""));
    cards.push_back(formatCard("NAXIS", channels == 1u ? "2" : "3", FixtureCardKind::Integer, ""));
    cards.push_back(formatCard("NAXIS1", std::to_string(image.width), FixtureCardKind::Integer, "columns"));
    cards.push_back(formatCard("NAXIS2", std::to_string(image.height), FixtureCardKind::Integer, "rows"));
    if (channels != 1u)
    {
        cards.push_back(formatCard("NAXIS3", std::to_string(channels), FixtureCardKind::Integer, "channel planes"));
    }
    if (form.bzero != 0.0)
    {
        cards.push_back(formatCard("BZERO", formatDouble(form.bzero), FixtureCardKind::Real, "stored + BZERO"));
        cards.push_back(formatCard("BSCALE", "1", FixtureCardKind::Real, ""));
    }
    const bool bottomUp = rowOrder == FixtureRowOrder::BottomUp;
    cards.push_back(formatCard("ROWORDER", bottomUp ? "BOTTOM-UP" : "TOP-DOWN", FixtureCardKind::Text,
                               bottomUp ? "the last row is stored first" : "row zero is stored first"));
    if (image.cfa != FixtureCfa::None)
    {
        cards.push_back(formatCard("BAYERPAT", cfaName(image.cfa), FixtureCardKind::Text, "filter array"));
    }
    for (const FixtureCard& card : image.cards)
    {
        cards.push_back(formatCard(card.keyword, card.value, card.kind, card.comment));
    }
    cards.push_back(std::string("END") + std::string(kCardBytes - 3u, ' '));

    std::vector<char> bytes;
    for (const std::optional<std::string>& card : cards)
    {
        if (!card.has_value())
        {
            error = image.name + ": a card does not fit in eighty characters";
            return false;
        }
        bytes.insert(bytes.end(), card->begin(), card->end());
    }
    bytes.resize((bytes.size() + kRecordBytes - 1u) / kRecordBytes * kRecordBytes, ' ');

    const std::size_t header = bytes.size();
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t row = 0u; row < image.height; ++row)
        {
            const std::uint32_t y = bottomUp ? image.height - 1u - row : row;
            for (std::uint32_t x = 0u; x < image.width; ++x)
            {
                const double value = samples.value(x, y, channel);
                switch (image.sampleType)
                {
                case FixtureSampleType::UInt8:
                    appendBigEndian(bytes, static_cast<std::uint8_t>(value));
                    break;
                case FixtureSampleType::Int8:
                    appendBigEndian(bytes, static_cast<std::uint8_t>(static_cast<int>(value) + 128));
                    break;
                case FixtureSampleType::UInt16:
                    appendBigEndian(bytes, static_cast<std::int16_t>(static_cast<std::int32_t>(value) - 32768));
                    break;
                case FixtureSampleType::Int16:
                    appendBigEndian(bytes, static_cast<std::int16_t>(value));
                    break;
                case FixtureSampleType::UInt32:
                    appendBigEndian(bytes, static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 2147483648ll));
                    break;
                case FixtureSampleType::Int32:
                    appendBigEndian(bytes, static_cast<std::int32_t>(value));
                    break;
                case FixtureSampleType::Float32:
                    appendBigEndian(bytes, static_cast<float>(value));
                    break;
                case FixtureSampleType::Float64:
                    appendBigEndian(bytes, value);
                    break;
                }
            }
        }
    }
    bytes.resize(header + (bytes.size() - header + kRecordBytes - 1u) / kRecordBytes * kRecordBytes, '\0');

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        error = "could not write " + path.string();
        return false;
    }
    return true;
}

std::optional<FixtureFitsFile> readFixtureFits(const std::filesystem::path& path, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "could not open " + path.string();
        return std::nullopt;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const std::string name = path.filename().string();
    if (bytes.size() < kCardBytes || std::string_view(bytes.data(), 30u) != "SIMPLE  =                    T")
    {
        error = name + " does not start with a FITS primary header";
        return std::nullopt;
    }

    FixtureFitsFile file;
    std::size_t offset = 0u;
    bool ended = false;
    for (; offset + kCardBytes <= bytes.size(); offset += kCardBytes)
    {
        const std::string_view card(bytes.data() + offset, kCardBytes);
        FitsHeaderCard entry;
        entry.keyword = trimmed(card.substr(0u, 8u));
        if (entry.keyword == "END")
        {
            ended = true;
            offset += kCardBytes;
            break;
        }
        if (card.substr(8u, 2u) == "= ")
        {
            parseValue(card.substr(10u), entry);
        }
        else
        {
            entry.comment = trimmed(card.substr(8u));
        }
        file.cards.push_back(std::move(entry));
    }
    if (!ended)
    {
        error = name + " ends before its header does";
        return std::nullopt;
    }
    const std::size_t dataStart = (offset + kRecordBytes - 1u) / kRecordBytes * kRecordBytes;

    auto integerCard = [&](std::string_view keyword) -> std::optional<long long> {
        const std::optional<std::string> value = file.card(keyword);
        return value.has_value() ? integerOf(*value) : std::nullopt;
    };
    auto realCard = [&](std::string_view keyword, double otherwise) -> std::optional<double> {
        const std::optional<std::string> value = file.card(keyword);
        if (!value.has_value())
        {
            return otherwise;
        }
        return parseDouble(*value);
    };

    const std::optional<long long> bitpix = integerCard("BITPIX");
    const std::optional<long long> naxis = integerCard("NAXIS");
    const std::optional<long long> width = integerCard("NAXIS1");
    const std::optional<long long> height = integerCard("NAXIS2");
    if (!bitpix.has_value() || !naxis.has_value() || !width.has_value() || !height.has_value() || *width <= 0 ||
        *height <= 0)
    {
        error = name + " states no image: BITPIX, NAXIS, NAXIS1 and NAXIS2 are required";
        return std::nullopt;
    }
    long long planes = 1;
    if (*naxis == 3)
    {
        planes = integerCard("NAXIS3").value_or(0);
        if (planes != 1 && planes != 3)
        {
            error = name + " has " + std::to_string(planes) + " planes; a fixture has one or three";
            return std::nullopt;
        }
    }
    else if (*naxis != 2)
    {
        error = name + " has NAXIS " + std::to_string(*naxis) + "; a fixture has two or three axes";
        return std::nullopt;
    }
    const std::optional<double> bscale = realCard("BSCALE", 1.0);
    if (!bscale.has_value() || *bscale != 1.0)
    {
        error = name + " has BSCALE " + file.card("BSCALE").value_or("?") + "; a fixture is stored unscaled";
        return std::nullopt;
    }
    const std::optional<double> bzero = realCard("BZERO", 0.0);
    const std::optional<FixtureSampleType> sampleType =
        bzero.has_value() ? sampleTypeOfStored(static_cast<int>(*bitpix), *bzero) : std::nullopt;
    if (!sampleType.has_value())
    {
        error = name + " has BITPIX " + std::to_string(*bitpix) + " and BZERO " + file.card("BZERO").value_or("0") +
                ", which is not one of the eight representations with its canonical offset";
        return std::nullopt;
    }

    const std::size_t size = sampleSizeOf(*sampleType);
    const std::size_t count =
        static_cast<std::size_t>(*width) * static_cast<std::size_t>(*height) * static_cast<std::size_t>(planes);
    if (bytes.size() < dataStart + count * size)
    {
        error = name + " ends before its data does";
        return std::nullopt;
    }

    const bool bottomUp = file.card("ROWORDER").value_or("") == "BOTTOM-UP";
    const std::string pattern = file.card("BAYERPAT").value_or("");
    file.cfa = cfaFromName(pattern).value_or(FixtureCfa::None);
    file.samples = FixtureSamples(static_cast<std::uint32_t>(*width), static_cast<std::uint32_t>(*height),
                                  static_cast<std::uint32_t>(planes), *sampleType);
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data() + dataStart);
    std::size_t index = 0u;
    for (std::uint32_t channel = 0u; channel < planes; ++channel)
    {
        for (std::uint32_t row = 0u; row < *height; ++row)
        {
            const std::uint32_t y = bottomUp ? static_cast<std::uint32_t>(*height) - 1u - row : row;
            for (std::uint32_t x = 0u; x < *width; ++x, ++index)
            {
                const std::uint64_t bits = readBigEndian(data + index * size, size);
                double value = 0.0;
                switch (*sampleType)
                {
                case FixtureSampleType::UInt8:
                    value = static_cast<double>(static_cast<std::uint8_t>(bits));
                    break;
                case FixtureSampleType::Int8:
                    value = static_cast<double>(static_cast<std::uint8_t>(bits)) - 128.0;
                    break;
                case FixtureSampleType::UInt16:
                    value = static_cast<double>(static_cast<std::int16_t>(bits)) + 32768.0;
                    break;
                case FixtureSampleType::Int16:
                    value = static_cast<double>(static_cast<std::int16_t>(bits));
                    break;
                case FixtureSampleType::UInt32:
                    value = static_cast<double>(static_cast<std::int32_t>(bits)) + 2147483648.0;
                    break;
                case FixtureSampleType::Int32:
                    value = static_cast<double>(static_cast<std::int32_t>(bits));
                    break;
                case FixtureSampleType::Float32:
                    value = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
                    break;
                case FixtureSampleType::Float64:
                    value = std::bit_cast<double>(bits);
                    break;
                }
                file.samples.setValue(x, y, channel, value);
            }
        }
    }
    return file;
}

} // namespace an::tests::fixtures
