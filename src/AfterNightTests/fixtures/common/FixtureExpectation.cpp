// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureExpectation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace an::tests::fixtures
{

namespace
{

constexpr const char* kExpectationsVariable = "AFTERNIGHT_FIXTURE_EXPECTATIONS";
constexpr const char* kExpectationsInTree = "src/AfterNightTests/fixtures/common/expectations";

// The record's fields in the order they are written; a record states each at most once.
constexpr const char* kFields[] = {"expectation", "operation", "input",         "class",    "tolerance",
                                   "accepted",    "producer",  "configuration", "compiler", "platform",
                                   "captured",    "commit",    "result"};

[[nodiscard]] std::string field(std::string_view name, std::string_view value)
{
    std::string line(name);
    if (line.size() < 12u)
    {
        line.resize(12u, ' ');
    }
    return line + " " + std::string(value) + "\n";
}

[[nodiscard]] bool isDigest(std::string_view text) noexcept
{
    return text.size() == 64u && std::all_of(text.begin(), text.end(), [](char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

// A sample as the record writes it: an integer in decimal, a Float32 by its own shortest text, a Float64 by its own.
[[nodiscard]] std::string formatSample(double value, FixtureSampleType sampleType)
{
    if (!isFloating(sampleType))
    {
        return std::to_string(static_cast<long long>(value));
    }
    if (sampleType == FixtureSampleType::Float32 && std::isfinite(value))
    {
        char buffer[48];
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value));
        if (error == std::errc())
        {
            return std::string(buffer, end);
        }
    }
    return formatDouble(value);
}

[[nodiscard]] std::optional<double> parseSample(std::string_view text, FixtureSampleType sampleType) noexcept
{
    if (!isFloating(sampleType))
    {
        long long value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || end != text.data() + text.size() || text.empty() ||
            static_cast<double>(value) < lowestOf(sampleType) || static_cast<double>(value) > highestOf(sampleType))
        {
            return std::nullopt;
        }
        return static_cast<double>(value);
    }
    // A NaN keeps its sign and payload only through the fixture text form, so every NaN spelling reads through it.
    if (sampleType == FixtureSampleType::Float32 && text.rfind("nan", 0) != 0 && text != "inf" && text != "-inf")
    {
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || end != text.data() + text.size() || text.empty())
        {
            return std::nullopt;
        }
        return static_cast<double>(value);
    }
    return parseDouble(text);
}

[[nodiscard]] std::vector<std::string> words(std::string_view text)
{
    std::vector<std::string> result;
    std::size_t at = 0u;
    while (at < text.size())
    {
        const std::size_t start = text.find_first_not_of(' ', at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const std::size_t end = text.find(' ', start);
        result.emplace_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        at = end == std::string_view::npos ? text.size() : end;
    }
    return result;
}

// The value after `key=` in one of a line's words, or nothing.
[[nodiscard]] std::optional<std::string> keyed(const std::vector<std::string>& parts, std::string_view key)
{
    for (const std::string& part : parts)
    {
        if (part.size() > key.size() && part.compare(0u, key.size(), key) == 0 && part[key.size()] == '=')
        {
            return part.substr(key.size() + 1u);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string shape(const FixtureSamples& samples)
{
    return std::to_string(samples.width()) + "x" + std::to_string(samples.height()) + "x" +
           std::to_string(samples.channelCount()) + " " + std::string(sampleTypeName(samples.sampleType()));
}

[[nodiscard]] std::string today()
{
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &parts);
    return buffer;
}

[[nodiscard]] std::filesystem::path executableDirectory()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH)
    {
        return {};
    }
    return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path() : path.parent_path();
#else
    return {};
#endif
}

// One record's lines, from `expectation` to `end`, turned into a record, or the reason it cannot be.
[[nodiscard]] std::optional<Expectation> parseRecord(const std::vector<std::pair<std::size_t, std::string>>& lines,
                                                     std::string& error)
{
    std::map<std::string, std::string> fields;
    std::vector<std::pair<std::size_t, std::string>> rows;
    for (const auto& [number, line] : lines)
    {
        const std::size_t split = line.find(' ');
        const std::string name = line.substr(0u, split);
        const std::string value =
            split == std::string::npos ? std::string() : line.substr(line.find_first_not_of(' ', split));
        if (name == "row")
        {
            rows.emplace_back(number, value);
            continue;
        }
        if (std::find_if(std::begin(kFields), std::end(kFields), [&](const char* known) { return name == known; }) ==
            std::end(kFields))
        {
            error = "line " + std::to_string(number) + ": a record has no field '" + name + "'";
            return std::nullopt;
        }
        if (!fields.emplace(name, value).second)
        {
            error = "line " + std::to_string(number) + ": the record states " + name + " twice";
            return std::nullopt;
        }
    }
    for (const char* required : {"expectation", "operation", "input", "class", "producer", "configuration", "compiler",
                                 "platform", "captured", "commit", "result"})
    {
        if (fields.find(required) == fields.end() || fields[required].empty())
        {
            error = "line " + std::to_string(lines.front().first) + ": the record states no " + required;
            return std::nullopt;
        }
    }

    Expectation expectation;
    expectation.id = fields["expectation"];
    const std::string where = expectation.id + ": ";
    expectation.operation = fields["operation"];

    const std::vector<std::string> input = words(fields["input"]);
    const std::optional<std::string> description = keyed(input, "description");
    const std::optional<std::string> inputSamples = keyed(input, "samples");
    if (input.size() != 3u || !description.has_value() || !isDigest(*description) || !inputSamples.has_value() ||
        !isDigest(*inputSamples))
    {
        error = where + "the input is not '<name> description=<digest> samples=<digest>'";
        return std::nullopt;
    }
    expectation.inputName = input[0];
    expectation.inputDescription = *description;
    expectation.inputSamples = *inputSamples;

    const std::optional<ExpectationClass> classification = expectationClassFromName(fields["class"]);
    if (!classification.has_value())
    {
        error = where + "the class '" + fields["class"] + "' is not exact, tolerance or accepted";
        return std::nullopt;
    }
    expectation.classification = *classification;
    const bool tolerance = fields.count("tolerance") != 0u;
    const bool accepted = fields.count("accepted") != 0u;
    if (tolerance != (expectation.classification == ExpectationClass::Tolerance))
    {
        error = where + "a tolerance belongs to, and only to, a tolerance record";
        return std::nullopt;
    }
    if (accepted != (expectation.classification == ExpectationClass::Accepted))
    {
        error = where + "a reason belongs to, and only to, an accepted record";
        return std::nullopt;
    }
    if (tolerance)
    {
        const std::vector<std::string> bounds = words(fields["tolerance"]);
        const std::optional<double> absolute = parseDouble(keyed(bounds, "absolute").value_or(""));
        const std::optional<double> relative = parseDouble(keyed(bounds, "relative").value_or(""));
        if (bounds.size() != 2u || !absolute.has_value() || !relative.has_value() || !(*absolute >= 0.0) ||
            !(*relative >= 0.0))
        {
            error = where + "the tolerance is not 'absolute=<bound> relative=<bound>' with finite bounds";
            return std::nullopt;
        }
        expectation.absoluteTolerance = *absolute;
        expectation.relativeTolerance = *relative;
    }
    if (accepted)
    {
        expectation.acceptedReason = fields["accepted"];
    }

    const std::vector<std::string> producer = words(fields["producer"]);
    const std::optional<std::string> abi = keyed(producer, "abi");
    if (producer.size() != 3u || !abi.has_value())
    {
        error = where + "the producer is not '<library> <version> abi=<generation>'";
        return std::nullopt;
    }
    expectation.provenance.producer = producer[0];
    expectation.provenance.producerVersion = producer[1];
    expectation.provenance.producerAbi = *abi;
    expectation.provenance.configuration = fields["configuration"];
    expectation.provenance.compiler = fields["compiler"];
    expectation.provenance.platform = fields["platform"];
    expectation.provenance.captured = fields["captured"];
    expectation.provenance.commit = fields["commit"];

    const std::vector<std::string> result = words(fields["result"]);
    const std::optional<std::string> digest = keyed(result, "sha256");
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
    const std::optional<FixtureSampleType> sampleType =
        result.size() == 5u ? sampleTypeFromName(result[3]) : std::nullopt;
    const auto dimension = [](const std::string& text, std::uint32_t& out) {
        const auto [end, failure] = std::from_chars(text.data(), text.data() + text.size(), out);
        return failure == std::errc() && end == text.data() + text.size() && out > 0u;
    };
    if (!sampleType.has_value() || !digest.has_value() || !isDigest(*digest) || !dimension(result[0], width) ||
        !dimension(result[1], height) || !dimension(result[2], channels))
    {
        error = where + "the result is not '<width> <height> <channels> <sample type> sha256=<digest>'";
        return std::nullopt;
    }
    expectation.result = FixtureSamples(width, height, channels, *sampleType);
    if (rows.size() != static_cast<std::size_t>(height) * channels)
    {
        error = where + "the result has " + std::to_string(rows.size()) + " rows, and a " + shape(expectation.result) +
                " result has " + std::to_string(static_cast<std::size_t>(height) * channels);
        return std::nullopt;
    }
    std::size_t index = 0u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y, ++index)
        {
            const auto& [number, text] = rows[index];
            const std::vector<std::string> values = words(text);
            if (values.size() != static_cast<std::size_t>(width) + 2u || values[0] != std::to_string(channel) ||
                values[1] != std::to_string(y))
            {
                error = "line " + std::to_string(number) + ": expected row " + std::to_string(channel) + " " +
                        std::to_string(y) + " with " + std::to_string(width) + " values";
                return std::nullopt;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::optional<double> value = parseSample(values[x + 2u], *sampleType);
                if (!value.has_value())
                {
                    error = "line " + std::to_string(number) + ": '" + values[x + 2u] + "' is not a " +
                            std::string(sampleTypeName(*sampleType)) + " sample";
                    return std::nullopt;
                }
                expectation.result.setValue(x, y, channel, *value);
            }
        }
    }
    if (expectation.result.digest() != *digest)
    {
        error = where + "the samples do not match the digest the record states";
        return std::nullopt;
    }
    return expectation;
}

} // namespace

std::string_view expectationClassName(ExpectationClass classification) noexcept
{
    switch (classification)
    {
    case ExpectationClass::Exact:
        return "exact";
    case ExpectationClass::Tolerance:
        return "tolerance";
    case ExpectationClass::Accepted:
        return "accepted";
    }
    return "unknown";
}

std::optional<ExpectationClass> expectationClassFromName(std::string_view name) noexcept
{
    for (const ExpectationClass classification :
         {ExpectationClass::Exact, ExpectationClass::Tolerance, ExpectationClass::Accepted})
    {
        if (expectationClassName(classification) == name)
        {
            return classification;
        }
    }
    return std::nullopt;
}

ExpectationProvenance provenanceOfThisBuild(std::string producer, std::string producerVersion, std::string producerAbi,
                                            std::string commit)
{
    ExpectationProvenance provenance;
    provenance.producer = std::move(producer);
    provenance.producerVersion = std::move(producerVersion);
    provenance.producerAbi = std::move(producerAbi);
#ifdef NDEBUG
    provenance.configuration = "Release";
#else
    provenance.configuration = "Debug";
#endif
#if defined(__clang__)
    provenance.compiler = "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." +
                          std::to_string(__clang_patchlevel__);
#elif defined(_MSC_VER)
    provenance.compiler = "msvc " + std::to_string(_MSC_FULL_VER / 10000000) + "." +
                          std::to_string(_MSC_FULL_VER / 100000 % 100) + "." + std::to_string(_MSC_FULL_VER % 100000);
#elif defined(__GNUC__)
    provenance.compiler = "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    provenance.compiler = "unknown";
#endif
#if defined(_WIN32)
    provenance.platform = "windows-x64";
#elif defined(__linux__)
    provenance.platform = "linux-x64";
#else
    provenance.platform = "unknown";
#endif
    provenance.captured = today();
    provenance.commit = commit.empty() ? std::string("-") : std::move(commit);
    return provenance;
}

Expectation makeExpectation(const std::string& operationKey, std::string operationText, const FixtureImage& input,
                            const FixtureSamples& inputSamples, FixtureSamples result, ExpectationClass classification,
                            ExpectationProvenance provenance)
{
    Expectation expectation;
    expectation.id = operationKey + "/" + input.name;
    expectation.operation = std::move(operationText);
    expectation.inputName = input.name;
    expectation.inputDescription = fixtureDigest(input);
    expectation.inputSamples = inputSamples.digest();
    expectation.classification = classification;
    if (provenance.captured.empty())
    {
        provenance.captured = today();
    }
    expectation.provenance = std::move(provenance);
    expectation.result = std::move(result);
    return expectation;
}

std::string formatExpectation(const Expectation& expectation)
{
    const ExpectationProvenance& provenance = expectation.provenance;
    const FixtureSamples& result = expectation.result;
    std::string text;
    text += field("expectation", expectation.id);
    text += field("operation", expectation.operation);
    text += field("input", expectation.inputName + " description=" + expectation.inputDescription +
                               " samples=" + expectation.inputSamples);
    text += field("class", expectationClassName(expectation.classification));
    if (expectation.classification == ExpectationClass::Tolerance)
    {
        text += field("tolerance", "absolute=" + formatDouble(expectation.absoluteTolerance) +
                                       " relative=" + formatDouble(expectation.relativeTolerance));
    }
    if (expectation.classification == ExpectationClass::Accepted)
    {
        text += field("accepted", expectation.acceptedReason);
    }
    text +=
        field("producer", provenance.producer + " " + provenance.producerVersion + " abi=" + provenance.producerAbi);
    text += field("configuration", provenance.configuration);
    text += field("compiler", provenance.compiler);
    text += field("platform", provenance.platform);
    text += field("captured", provenance.captured);
    text += field("commit", provenance.commit.empty() ? std::string("-") : provenance.commit);
    text += field("result", std::to_string(result.width()) + " " + std::to_string(result.height()) + " " +
                                std::to_string(result.channelCount()) + " " +
                                std::string(sampleTypeName(result.sampleType())) + " sha256=" + result.digest());
    for (std::uint32_t channel = 0u; channel < result.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < result.height(); ++y)
        {
            std::string row = std::to_string(channel) + " " + std::to_string(y);
            for (std::uint32_t x = 0u; x < result.width(); ++x)
            {
                row += " " + formatSample(result.value(x, y, channel), result.sampleType());
            }
            text += field("row", row);
        }
    }
    text += "end\n";
    return text;
}

std::optional<std::vector<Expectation>> parseExpectations(std::string_view text, std::string& error)
{
    std::vector<Expectation> records;
    std::vector<std::pair<std::size_t, std::string>> current;
    bool inside = false;
    std::size_t number = 0u;
    std::size_t at = 0u;
    while (at <= text.size())
    {
        const std::size_t end = text.find('\n', at);
        std::string line(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        at = end == std::string_view::npos ? text.size() + 1u : end + 1u;
        ++number;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        while (!line.empty() && line.back() == ' ')
        {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (line.rfind("expectation ", 0) == 0)
        {
            if (inside)
            {
                error = "line " + std::to_string(number) + ": a record starts before the one before it ends";
                return std::nullopt;
            }
            inside = true;
            current.clear();
        }
        else if (!inside)
        {
            error = "line " + std::to_string(number) + ": '" + line + "' is outside any record";
            return std::nullopt;
        }
        if (line == "end")
        {
            std::optional<Expectation> record = parseRecord(current, error);
            if (!record.has_value())
            {
                return std::nullopt;
            }
            for (const Expectation& earlier : records)
            {
                if (earlier.id == record->id)
                {
                    error = record->id + " is recorded twice";
                    return std::nullopt;
                }
            }
            records.push_back(std::move(*record));
            inside = false;
            continue;
        }
        current.emplace_back(number, line);
    }
    if (inside)
    {
        error = "the last record has no end";
        return std::nullopt;
    }
    return records;
}

ExpectationComparison compareWithExpectation(const Expectation& expectation, const FixtureSamples& candidate)
{
    ExpectationComparison comparison;
    const FixtureSamples& expected = expectation.result;
    if (candidate.width() != expected.width() || candidate.height() != expected.height() ||
        candidate.channelCount() != expected.channelCount() || candidate.sampleType() != expected.sampleType())
    {
        comparison.reason = "the candidate is " + shape(candidate) + ", and the record's result is " + shape(expected);
        return comparison;
    }
    const bool floating = isFloating(expected.sampleType());
    std::string first;
    std::uint64_t outside = 0u;
    for (std::uint32_t channel = 0u; channel < expected.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < expected.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < expected.width(); ++x)
            {
                const double want = expected.value(x, y, channel);
                const double got = candidate.value(x, y, channel);
                bool same = false;
                if (std::isnan(want) || std::isnan(got))
                {
                    same = std::isnan(want) && std::isnan(got);
                }
                else if (floating)
                {
                    same = std::signbit(want) == std::signbit(got) && want == got;
                }
                else
                {
                    same = want == got;
                }
                if (same)
                {
                    continue;
                }
                ++comparison.differingSamples;
                if (std::isfinite(want) && std::isfinite(got))
                {
                    comparison.largestDifference = (std::max)(comparison.largestDifference, std::fabs(want - got));
                }
                bool within = false;
                if (expectation.classification == ExpectationClass::Tolerance && std::isfinite(want) &&
                    std::isfinite(got))
                {
                    within = std::fabs(want - got) <=
                             expectation.absoluteTolerance + expectation.relativeTolerance * std::fabs(want);
                }
                if (!within)
                {
                    ++outside;
                    if (first.empty())
                    {
                        first = "channel " + std::to_string(channel) + " row " + std::to_string(y) + " column " +
                                std::to_string(x) + ": expected " + formatDouble(want) + ", got " + formatDouble(got);
                    }
                }
            }
        }
    }
    const std::string summary = std::to_string(comparison.differingSamples) + " samples differ, the largest by " +
                                formatDouble(comparison.largestDifference);
    switch (expectation.classification)
    {
    case ExpectationClass::Exact:
    case ExpectationClass::Tolerance:
        comparison.matched = outside == 0u;
        if (!comparison.matched)
        {
            comparison.reason = std::to_string(outside) + " samples fall outside the " +
                                std::string(expectationClassName(expectation.classification)) +
                                " classification; the first at " + first + "; " + summary;
        }
        break;
    case ExpectationClass::Accepted:
        comparison.matched = true;
        comparison.reason = "an accepted difference - " + expectation.acceptedReason + ": " + summary;
        break;
    }
    return comparison;
}

bool expectationDescribes(const Expectation& expectation, const FixtureImage& input, const FixtureSamples& inputSamples)
{
    return expectation.inputName == input.name && expectation.inputDescription == fixtureDigest(input) &&
           expectation.inputSamples == inputSamples.digest();
}

std::optional<std::vector<Expectation>> readExpectationFile(const std::filesystem::path& path, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "could not open " + path.string();
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::optional<std::vector<Expectation>> records = parseExpectations(text, error);
    if (!records.has_value())
    {
        error = path.filename().string() + ": " + error;
    }
    return records;
}

bool writeExpectationFile(const std::filesystem::path& path, const std::vector<Expectation>& expectations,
                          std::string& error)
{
    std::string text =
        "# Reference expectations of the fixture layers: each record states its input, the engine that produced "
        "it\n# and how a result is compared with it. See docs/dev/guides/TESTING.md.\n";
    for (const Expectation& expectation : expectations)
    {
        text += "\n" + formatExpectation(expectation);
    }
    std::string crlf;
    crlf.reserve(text.size() + text.size() / 40u);
    for (const char character : text)
    {
        if (character == '\n')
        {
            crlf += '\r';
        }
        crlf += character;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(crlf.data(), static_cast<std::streamsize>(crlf.size()));
    stream.close();
    if (!stream)
    {
        error = "could not write " + path.string();
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> expectationDirectory(std::string& error)
{
    std::error_code ignored;
#ifdef _WIN32
    char* stated = nullptr;
    std::size_t length = 0u;
    std::string variable;
    if (_dupenv_s(&stated, &length, kExpectationsVariable) == 0 && stated != nullptr)
    {
        variable = stated;
    }
    std::free(stated);
#else
    const char* stated = std::getenv(kExpectationsVariable);
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

} // namespace an::tests::fixtures
