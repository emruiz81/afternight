// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RawDumpExportRecord.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace an::bench
{
namespace
{

using Json = nlohmann::json;

constexpr const char* kExpectationsVariable = "AFTERNIGHT_FIXTURE_EXPECTATIONS";
constexpr const char* kExpectationsInTree = "src/AfterNightTests/fixtures/common/expectations";
constexpr const char* kRecordName = "raw-dump-exports";
constexpr int kRecordSchema = 1;
constexpr const char* kDifferencesName = "raw-dump-export-differences";

SampleDescriptor describe(SampleType type, std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                          const char* cfaPattern = "")
{
    SampleDescriptor descriptor;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.channels = channels;
    descriptor.sampleType = type;
    descriptor.layout = Layout::Planar;
    descriptor.byteOrder = ByteOrder::Little;
    descriptor.cfaPattern = cfaPattern;
    return descriptor;
}

RawDumpExportCase image(const char* name, const char* file, SampleDescriptor descriptor, const char* generator,
                        bool cards = false)
{
    return RawDumpExportCase{name, file, RawDumpExportInput::Image, std::move(descriptor), generator, cards};
}

Json descriptorJson(const SampleDescriptor& d)
{
    return Json{{"width", d.width},
                {"height", d.height},
                {"channels", d.channels},
                {"sampleType", toString(d.sampleType)},
                {"layout", toString(d.layout)},
                {"byteOrder", toString(d.byteOrder)},
                {"rowStrideBytes", d.rowStrideBytes},
                {"planeStrideBytes", d.planeStrideBytes},
                {"cfaPattern", d.cfaPattern}};
}

bool readDescriptor(const Json& json, SampleDescriptor& out, std::string& error)
{
    if (!json.is_object())
    {
        error = "an outcome's descriptor is not an object";
        return false;
    }
    try
    {
        out.width = json.at("width").get<std::uint32_t>();
        out.height = json.at("height").get<std::uint32_t>();
        out.channels = json.at("channels").get<std::uint32_t>();
        out.rowStrideBytes = json.at("rowStrideBytes").get<std::uint64_t>();
        out.planeStrideBytes = json.at("planeStrideBytes").get<std::uint64_t>();
        out.cfaPattern = json.at("cfaPattern").get<std::string>();
        if (!fromString(json.at("sampleType").get<std::string>(), out.sampleType) ||
            !fromString(json.at("layout").get<std::string>(), out.layout) ||
            !fromString(json.at("byteOrder").get<std::string>(), out.byteOrder))
        {
            error = "an outcome's descriptor names a representation, layout or order the schema does not";
            return false;
        }
    }
    catch (const Json::exception& failure)
    {
        error = std::string("an outcome's descriptor is malformed: ") + failure.what();
        return false;
    }
    return true;
}

bool readText(const std::filesystem::path& path, std::string& text, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

std::string trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(' ');
    return std::string(text.substr(first, last - first + 1u));
}

/// A FITS value as what it means: a string without its quotes, its doubled quotes and its trailing blanks;
/// a logical by its letter; a number by its value; anything else as written, blanks trimmed.
struct CardMeaning
{
    enum class Kind : std::uint8_t
    {
        Text,
        Number,
        Logical
    };
    Kind kind = Kind::Text;
    std::string text;
    double number = 0.0;
};

CardMeaning meaningOf(const std::string& value)
{
    const std::string written = trimmed(value);
    CardMeaning meaning;
    if (written.size() >= 2u && written.front() == '\'' && written.back() == '\'')
    {
        std::string inner;
        for (std::size_t index = 1u; index + 1u < written.size(); ++index)
        {
            inner += written[index];
            if (written[index] == '\'' && index + 2u < written.size() && written[index + 1u] == '\'')
            {
                ++index;
            }
        }
        const auto last = inner.find_last_not_of(' ');
        meaning.text = last == std::string::npos ? std::string() : inner.substr(0u, last + 1u);
        return meaning;
    }
    if (written == "T" || written == "F")
    {
        meaning.kind = CardMeaning::Kind::Logical;
        meaning.text = written;
        return meaning;
    }
    std::string spelled = written;
    std::replace(spelled.begin(), spelled.end(), 'D', 'E');
    std::replace(spelled.begin(), spelled.end(), 'd', 'e');
    const char* const begin = spelled.data();
    const char* const end = begin + spelled.size();
    const char* const start = (!spelled.empty() && spelled.front() == '+') ? begin + 1 : begin;
    double number = 0.0;
    const auto [stop, failure] = std::from_chars(start, end, number);
    if (!spelled.empty() && failure == std::errc() && stop == end)
    {
        meaning.kind = CardMeaning::Kind::Number;
        meaning.number = number;
        return meaning;
    }
    meaning.text = written;
    return meaning;
}

bool registeredHere(const RawDumpCardDifference& entry, const std::string& caseName, const std::string& key,
                    RawDumpCardChange change, const MetadataCard* recorded, const MetadataCard* current)
{
    if (entry.caseName != "*" && entry.caseName != caseName)
    {
        return false;
    }
    const bool prefix = !entry.key.empty() && entry.key.back() == '*';
    if (prefix ? key.compare(0u, entry.key.size() - 1u, entry.key, 0u, entry.key.size() - 1u) != 0 : entry.key != key)
    {
        return false;
    }
    if (entry.change != change)
    {
        return false;
    }
    if (change == RawDumpCardChange::Value)
    {
        return (!entry.recorded.has_value() || *entry.recorded == recorded->value) &&
               (!entry.current.has_value() || *entry.current == current->value);
    }
    return true;
}

const char* changeName(RawDumpCardChange change)
{
    switch (change)
    {
    case RawDumpCardChange::OnlyRecorded:
        return "only-recorded";
    case RawDumpCardChange::OnlyCurrent:
        return "only-current";
    case RawDumpCardChange::Value:
        return "value";
    case RawDumpCardChange::Comment:
        return "comment";
    }
    return "unknown";
}

std::string spell(const std::optional<std::string>& value)
{
    return value.has_value() ? "'" + *value + "'" : std::string("no card");
}

std::filesystem::path executableDirectory()
{
#if defined(_WIN32)
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0u || length >= buffer.size())
    {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ignored;
    return std::filesystem::read_symlink("/proc/self/exe", ignored).parent_path();
#endif
}

} // namespace

const std::vector<RawDumpExportCase>& rawDumpExportCases()
{
    // Small and odd-sized, so a stride, a padded row or a transposed plane changes the digest; every
    // representation FITS holds, colour and grey, a Bayer mosaic, and each other container the engine reads
    // in the representations its writer offers. The last three are refusals.
    static const std::vector<RawDumpExportCase> cases = [] {
        std::vector<RawDumpExportCase> list{
            image("fits-uint8-grey", "fits-uint8-grey.fits", describe(SampleType::UInt8, 7, 5, 1),
                  "synthetic.extremes"),
            image("fits-int8-grey", "fits-int8-grey.fits", describe(SampleType::Int8, 7, 5, 1), "synthetic.extremes"),
            image("fits-uint16-grey", "fits-uint16-grey.fit", describe(SampleType::UInt16, 7, 5, 1),
                  "synthetic.extremes", true),
            image("fits-int16-grey", "fits-int16-grey.fits", describe(SampleType::Int16, 7, 5, 1),
                  "synthetic.extremes"),
            image("fits-uint32-grey", "fits-uint32-grey.fits", describe(SampleType::UInt32, 7, 5, 1),
                  "synthetic.extremes"),
            image("fits-int32-grey", "fits-int32-grey.fits", describe(SampleType::Int32, 7, 5, 1),
                  "synthetic.extremes"),
            image("fits-float32-grey", "fits-float32-grey.fits", describe(SampleType::Float32, 9, 6, 1),
                  "synthetic.gaussian_noise", true),
            image("fits-float64-colour", "fits-float64-colour.fits", describe(SampleType::Float64, 7, 5, 3),
                  "synthetic.gradient"),
            image("fits-uint16-colour", "fits-uint16-colour.fts", describe(SampleType::UInt16, 7, 5, 3),
                  "synthetic.ramp", true),
            image("fits-uint16-bayer", "fits-uint16-bayer.fits", describe(SampleType::UInt16, 8, 6, 1, "RGGB"),
                  "synthetic.stars", true),
            image("xisf-uint16-grey", "xisf-uint16-grey.xisf", describe(SampleType::UInt16, 7, 5, 1), "synthetic.ramp",
                  true),
            image("xisf-float32-colour", "xisf-float32-colour.xisf", describe(SampleType::Float32, 7, 5, 3),
                  "synthetic.gaussian_noise"),
            image("tiff-uint8-colour", "tiff-uint8-colour.tif", describe(SampleType::UInt8, 7, 5, 3), "synthetic.ramp"),
            image("tiff-uint16-grey", "tiff-uint16-grey.tiff", describe(SampleType::UInt16, 7, 5, 1), "synthetic.ramp"),
            image("tiff-float32-grey", "tiff-float32-grey.tif", describe(SampleType::Float32, 7, 5, 1),
                  "synthetic.gaussian_noise"),
            image("png-uint8-colour", "png-uint8-colour.png", describe(SampleType::UInt8, 7, 5, 3), "synthetic.ramp"),
            image("png-uint16-grey", "png-uint16-grey.png", describe(SampleType::UInt16, 7, 5, 1), "synthetic.ramp"),
            image("jpeg-uint8-colour", "jpeg-uint8-colour.jpg", describe(SampleType::UInt8, 16, 8, 3),
                  "synthetic.gradient"),
            image("jpeg-uint8-grey", "jpeg-uint8-grey.jpeg", describe(SampleType::UInt8, 16, 8, 1),
                  "synthetic.gradient"),
        };
        list.push_back(RawDumpExportCase{"not-an-image", "not-an-image.fits", RawDumpExportInput::NotAnImage,
                                         describe(SampleType::UInt16, 7, 5, 1), "synthetic.ramp", false});
        list.push_back(RawDumpExportCase{"truncated-fits", "truncated-fits.fits", RawDumpExportInput::Truncated,
                                         describe(SampleType::UInt16, 64, 64, 1), "synthetic.ramp", false});
        list.push_back(RawDumpExportCase{"unknown-extension", "unknown-extension.anbin",
                                         RawDumpExportInput::UnknownExtension, describe(SampleType::UInt16, 7, 5, 1),
                                         "synthetic.ramp", false});
        return list;
    }();
    return cases;
}

RawDumpExportOutcome exportedOutcome(const std::string& name, const RawDump& dump)
{
    RawDumpExportOutcome outcome;
    outcome.name = name;
    outcome.exported = true;
    outcome.sourceFormat = dump.sourceFormat;
    outcome.descriptor = dump.descriptor;
    outcome.payloadBytes = dump.payloadBytes;
    outcome.payloadDigest = dump.payloadDigest;
    outcome.cards = dump.cards;
    return outcome;
}

const RawDumpExportOutcome* RawDumpExportRecord::find(const std::string& name) const
{
    for (const RawDumpExportOutcome& outcome : outcomes)
    {
        if (outcome.name == name)
        {
            return &outcome;
        }
    }
    return nullptr;
}

bool readRawDumpExportRecord(const std::filesystem::path& path, RawDumpExportRecord& out, std::string& error)
{
    std::string text;
    if (!readText(path, text, error))
    {
        return false;
    }
    try
    {
        const Json document = Json::parse(text);
        if (document.at("record").get<std::string>() != kRecordName ||
            document.at("schema").get<int>() != kRecordSchema)
        {
            error =
                "'" + path.string() + "' is not a raw-dump export record of schema " + std::to_string(kRecordSchema);
            return false;
        }
        out = RawDumpExportRecord{};
        out.producer = document.at("producer").get<std::string>();
        out.commit = document.at("commit").get<std::string>();
        out.captured = document.at("captured").get<std::string>();
        for (const Json& entry : document.at("outcomes"))
        {
            RawDumpExportOutcome outcome;
            outcome.name = entry.at("name").get<std::string>();
            outcome.exported = entry.at("exported").get<bool>();
            if (outcome.exported)
            {
                outcome.sourceFormat = entry.at("sourceFormat").get<std::string>();
                if (!readDescriptor(entry.at("descriptor"), outcome.descriptor, error))
                {
                    return false;
                }
                outcome.payloadBytes = entry.at("payloadBytes").get<std::uint64_t>();
                outcome.payloadDigest = entry.at("payloadDigest").get<std::string>();
                for (const Json& card : entry.at("cards"))
                {
                    outcome.cards.push_back(MetadataCard{card.at("key").get<std::string>(),
                                                         card.at("value").get<std::string>(),
                                                         card.at("comment").get<std::string>()});
                }
            }
            out.outcomes.push_back(std::move(outcome));
        }
    }
    catch (const Json::exception& failure)
    {
        error = "'" + path.string() + "' is malformed: " + failure.what();
        return false;
    }
    return true;
}

bool writeRawDumpExportRecord(const std::filesystem::path& path, const RawDumpExportRecord& record, std::string& error)
{
    Json outcomes = Json::array();
    for (const RawDumpExportOutcome& outcome : record.outcomes)
    {
        Json entry{{"name", outcome.name}, {"exported", outcome.exported}};
        if (outcome.exported)
        {
            Json cards = Json::array();
            for (const MetadataCard& card : outcome.cards)
            {
                cards.push_back(Json{{"key", card.key}, {"value", card.value}, {"comment", card.comment}});
            }
            entry["sourceFormat"] = outcome.sourceFormat;
            entry["descriptor"] = descriptorJson(outcome.descriptor);
            entry["payloadBytes"] = outcome.payloadBytes;
            entry["payloadDigest"] = outcome.payloadDigest;
            entry["cards"] = std::move(cards);
        }
        outcomes.push_back(std::move(entry));
    }
    const Json document{{"record", kRecordName},   {"schema", kRecordSchema},     {"producer", record.producer},
                        {"commit", record.commit}, {"captured", record.captured}, {"outcomes", std::move(outcomes)}};
    // CRLF, as every text file the repository owns is written.
    std::string text = document.dump(2);
    std::string lines;
    lines.reserve(text.size() + text.size() / 16u);
    for (const char character : text)
    {
        if (character == '\n')
        {
            lines += '\r';
        }
        lines += character;
    }
    lines += "\r\n";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "cannot open '" + path.string() + "' for writing";
        return false;
    }
    file.write(lines.data(), static_cast<std::streamsize>(lines.size()));
    if (!file)
    {
        error = "write to '" + path.string() + "' failed";
        return false;
    }
    return true;
}

bool readRawDumpCardDifferences(const std::filesystem::path& path, std::vector<RawDumpCardDifference>& out,
                                std::string& error)
{
    std::string text;
    if (!readText(path, text, error))
    {
        return false;
    }
    try
    {
        const Json document = Json::parse(text);
        if (document.at("record").get<std::string>() != kDifferencesName)
        {
            error = "'" + path.string() + "' is not a raw-dump export difference register";
            return false;
        }
        out.clear();
        for (const Json& entry : document.at("differences"))
        {
            RawDumpCardDifference difference;
            difference.caseName = entry.at("case").get<std::string>();
            difference.key = entry.at("key").get<std::string>();
            const std::string change = entry.at("change").get<std::string>();
            bool known = false;
            for (const RawDumpCardChange candidate : {RawDumpCardChange::OnlyRecorded, RawDumpCardChange::OnlyCurrent,
                                                      RawDumpCardChange::Value, RawDumpCardChange::Comment})
            {
                if (change == changeName(candidate))
                {
                    difference.change = candidate;
                    known = true;
                }
            }
            if (entry.contains("recorded"))
            {
                difference.recorded = entry.at("recorded").get<std::string>();
            }
            if (entry.contains("current"))
            {
                difference.current = entry.at("current").get<std::string>();
            }
            difference.reason = entry.at("reason").get<std::string>();
            const bool statesValues = difference.recorded.has_value() || difference.current.has_value();
            if (!known || difference.reason.empty() || difference.key.empty() || difference.caseName.empty() ||
                (statesValues && difference.change != RawDumpCardChange::Value) ||
                (difference.recorded.has_value() && difference.recorded == difference.current))
            {
                error = "'" + path.string() + "' holds an entry for " + difference.caseName + " " + difference.key +
                        " that names no difference it can match or gives no reason";
                return false;
            }
            out.push_back(std::move(difference));
        }
    }
    catch (const Json::exception& failure)
    {
        error = "'" + path.string() + "' is malformed: " + failure.what();
        return false;
    }
    return true;
}

bool sameCardValue(const std::string& left, const std::string& right)
{
    if (left == right)
    {
        return true;
    }
    const CardMeaning a = meaningOf(left);
    const CardMeaning b = meaningOf(right);
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == CardMeaning::Kind::Number)
    {
        return a.number == b.number;
    }
    return a.text == b.text;
}

std::vector<std::string> compareRawDumpExports(const RawDumpExportOutcome& recorded,
                                               const RawDumpExportOutcome& current,
                                               const std::vector<RawDumpCardDifference>& registered,
                                               std::vector<bool>& used)
{
    used.resize(registered.size(), false);
    std::vector<std::string> problems;
    const std::string& name = recorded.name;
    if (recorded.exported != current.exported)
    {
        problems.push_back(name + ": the record " + (recorded.exported ? "exported" : "refused") +
                           " the file and this exporter " + (current.exported ? "exported" : "refused") + " it");
        return problems;
    }
    if (!recorded.exported)
    {
        return problems;
    }
    if (!(recorded.descriptor == current.descriptor))
    {
        problems.push_back(name + ": the descriptor differs from the recorded one");
    }
    if (recorded.payloadBytes != current.payloadBytes || recorded.payloadDigest != current.payloadDigest)
    {
        problems.push_back(name + ": the payload is " + std::to_string(current.payloadBytes) + " bytes hashing to " +
                           current.payloadDigest + ", the recorded one " + std::to_string(recorded.payloadBytes) +
                           " bytes hashing to " + recorded.payloadDigest);
    }
    if (recorded.sourceFormat != current.sourceFormat)
    {
        problems.push_back(name + ": the source format is '" + current.sourceFormat + "', the recorded one '" +
                           recorded.sourceFormat + "'");
    }

    // Each key's occurrences, in the order the exporter wrote them; the nth of a key is matched to the nth.
    std::map<std::string, std::vector<const MetadataCard*>> before;
    std::map<std::string, std::vector<const MetadataCard*>> after;
    std::vector<std::string> keys;
    for (const MetadataCard& card : recorded.cards)
    {
        if (before.find(card.key) == before.end() && after.find(card.key) == after.end())
        {
            keys.push_back(card.key);
        }
        before[card.key].push_back(&card);
    }
    for (const MetadataCard& card : current.cards)
    {
        if (before.find(card.key) == before.end() && after.find(card.key) == after.end())
        {
            keys.push_back(card.key);
        }
        after[card.key].push_back(&card);
    }
    for (const std::string& key : keys)
    {
        const std::vector<const MetadataCard*>& left = before[key];
        const std::vector<const MetadataCard*>& right = after[key];
        for (std::size_t index = 0u; index < (std::max)(left.size(), right.size()); ++index)
        {
            const MetadataCard* const a = index < left.size() ? left[index] : nullptr;
            const MetadataCard* const b = index < right.size() ? right[index] : nullptr;
            if (a != nullptr && b != nullptr && sameCardValue(a->value, b->value) &&
                trimmed(a->comment) == trimmed(b->comment))
            {
                continue;
            }
            const RawDumpCardChange change = a == nullptr                        ? RawDumpCardChange::OnlyCurrent
                                             : b == nullptr                      ? RawDumpCardChange::OnlyRecorded
                                             : sameCardValue(a->value, b->value) ? RawDumpCardChange::Comment
                                                                                 : RawDumpCardChange::Value;
            const std::optional<std::string> was = a != nullptr ? std::optional<std::string>(a->value) : std::nullopt;
            const std::optional<std::string> is = b != nullptr ? std::optional<std::string>(b->value) : std::nullopt;
            bool explained = false;
            for (std::size_t entry = 0u; entry < registered.size(); ++entry)
            {
                if (registeredHere(registered[entry], name, key, change, a, b))
                {
                    used[entry] = true;
                    explained = true;
                }
            }
            if (!explained)
            {
                std::string sentence = name + ": card " + key + " #" + std::to_string(index + 1u) + " is " + spell(is) +
                                       ", the recorded one " + spell(was);
                if (change == RawDumpCardChange::Comment)
                {
                    sentence += " (its comment is '" + b->comment + "', the recorded one '" + a->comment + "')";
                }
                problems.push_back(sentence);
            }
        }
    }
    return problems;
}

std::optional<std::filesystem::path> locateExpectationDirectory(std::string& error)
{
    std::error_code ignored;
#if defined(_WIN32)
    std::string variable;
    char* stated = nullptr;
    std::size_t length = 0u;
    if (_dupenv_s(&stated, &length, kExpectationsVariable) == 0 && stated != nullptr)
    {
        variable = stated;
    }
    std::free(stated);
#else
    const char* const stated = std::getenv(kExpectationsVariable);
    const std::string variable = stated != nullptr ? stated : "";
#endif
    if (!variable.empty())
    {
        if (std::filesystem::is_directory(variable, ignored))
        {
            return std::filesystem::path(variable);
        }
        error = std::string(kExpectationsVariable) + " names " + variable + ", which is not a directory";
        return std::nullopt;
    }
    std::string looked;
    for (const std::filesystem::path& start : {executableDirectory(), std::filesystem::current_path(ignored)})
    {
        if (start.empty())
        {
            continue;
        }
        looked += (looked.empty() ? "" : " and ") + start.string();
        for (std::filesystem::path directory = start; !directory.empty(); directory = directory.parent_path())
        {
            const std::filesystem::path candidate = directory / kExpectationsInTree;
            if (std::filesystem::is_directory(candidate, ignored))
            {
                return candidate;
            }
            if (directory == directory.parent_path())
            {
                break;
            }
        }
    }
    error = std::string("no ") + kExpectationsInTree + " above " + looked + "; set " + kExpectationsVariable;
    return std::nullopt;
}

} // namespace an::bench
