// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the image-list manifest read and written by hand, to pugixml's rules.
//
// The manifest is the document pugixml 1.16 formats, and that library is not linked here: it would be
// an engine dependency for one small document. The reader is a scan of the grammar the library
// accepts - where a document ends, what counts as white space or a name, what markup is passed over,
// how an attribute's references decode, how a number or a boolean is read - and the writer reproduces
// the library's formatting and attribute escaping. Compared differentially with the library, the two
// agree except for one bound added on purpose: elements nest at most `kMaxDocumentDepth` deep, and the
// reader never recurses.

#include "io/sequences/ImageListFormat.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

namespace an::image_list_detail
{
namespace
{
/// pugixml's whitespace: space, tab, line feed and carriage return.
constexpr bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/// A character that may begin a name: an ASCII letter, `_`, `:`, or any byte of a multi-byte character.
constexpr bool isNameStart(char c) noexcept
{
    const auto byte = static_cast<unsigned char>(c);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_' || byte == ':' || byte >= 0x80u;
}

/// A character that may continue a name: one that may begin one, a digit, `-` or `.`.
constexpr bool isNameChar(char c) noexcept
{
    return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

constexpr char lowered(char c) noexcept
{
    return static_cast<char>(c | ' ');
}

/// Appends a code point as pugixml encodes one, including the four-byte form it writes for values beyond
/// Unicode, which the caller's UTF-8 check then refuses.
void appendCodePoint(std::string& text, std::uint32_t code)
{
    if (code < 0x80u)
        text.push_back(static_cast<char>(code));
    else if (code < 0x800u)
    {
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0xc0u | (code >> 6u))));
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0x80u | (code & 0x3fu))));
    }
    else if (code < 0x10000u)
    {
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0xe0u | (code >> 12u))));
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0x80u | ((code >> 6u) & 0x3fu))));
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0x80u | (code & 0x3fu))));
    }
    else
    {
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0xf0u | (code >> 18u))));
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0x80u | ((code >> 12u) & 0x3fu))));
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0x80u | ((code >> 6u) & 0x3fu))));
        text.push_back(static_cast<char>(static_cast<std::uint8_t>(0x80u | (code & 0x3fu))));
    }
}

/// Decodes the reference that begins with the `&` at `at`, appending what it names, and returns where
/// scanning resumes. A reference that is not recognised appends nothing: the characters it consumed are
/// appended as they are spelled and scanning resumes at the first one it did not.
std::size_t appendReference(std::string& text, std::string_view raw, std::size_t at)
{
    const auto charAt = [&](std::size_t index) noexcept { return index < raw.size() ? raw[index] : '\0'; };
    const auto keep = [&](std::size_t resume) {
        text.append(raw.substr(at, resume - at));
        return resume;
    };
    std::size_t next = at + 1u;
    switch (charAt(next))
    {
    case '#': {
        constexpr std::uint32_t kInvalidCodePoint = 0x110000u;
        std::uint32_t code = 0u;
        if (charAt(next + 1u) == 'x')
        {
            next += 2u;
            if (charAt(next) == ';')
                return keep(next);
            for (;; ++next)
            {
                const char c = charAt(next);
                std::uint32_t digit = 0u;
                if (c >= '0' && c <= '9')
                    digit = static_cast<std::uint32_t>(c - '0');
                else if (lowered(c) >= 'a' && lowered(c) <= 'f')
                    digit = static_cast<std::uint32_t>(lowered(c) - 'a' + 10);
                else if (c == ';')
                    break;
                else
                    return keep(next);
                if (code != kInvalidCodePoint)
                    code = code > (kInvalidCodePoint - 1u - digit) / 16u ? kInvalidCodePoint : 16u * code + digit;
            }
        }
        else
        {
            ++next;
            if (charAt(next) == ';')
                return keep(next);
            for (;; ++next)
            {
                const char c = charAt(next);
                if (c >= '0' && c <= '9')
                {
                    const auto digit = static_cast<std::uint32_t>(c - '0');
                    if (code != kInvalidCodePoint)
                        code = code > (kInvalidCodePoint - 1u - digit) / 10u ? kInvalidCodePoint : 10u * code + digit;
                }
                else if (c == ';')
                    break;
                else
                    return keep(next);
            }
        }
        appendCodePoint(text, code);
        return next + 1u;
    }
    case 'a':
        ++next;
        if (charAt(next) == 'm')
        {
            if (charAt(++next) == 'p' && charAt(++next) == ';')
            {
                text.push_back('&');
                return next + 1u;
            }
        }
        else if (charAt(next) == 'p')
        {
            if (charAt(++next) == 'o' && charAt(++next) == 's' && charAt(++next) == ';')
            {
                text.push_back('\'');
                return next + 1u;
            }
        }
        return keep(next);
    case 'g':
        if (charAt(++next) == 't' && charAt(++next) == ';')
        {
            text.push_back('>');
            return next + 1u;
        }
        return keep(next);
    case 'l':
        if (charAt(++next) == 't' && charAt(++next) == ';')
        {
            text.push_back('<');
            return next + 1u;
        }
        return keep(next);
    case 'q':
        if (charAt(++next) == 'u' && charAt(++next) == 'o' && charAt(++next) == 't' && charAt(++next) == ';')
        {
            text.push_back('"');
            return next + 1u;
        }
        return keep(next);
    default:
        return keep(next);
    }
}

/// An integer as pugixml reads one into an unsigned type. `minimum` is the bit pattern of
/// the smallest value the result stands for - zero for an unsigned result, the two's complement of the
/// most negative value for a signed one - and a negative value is answered with its own two's complement
/// when its magnitude is within that, and with `minimum` when it is not.
template <typename UnsignedT>
UnsignedT readWithLibraryRules(std::string_view text, UnsignedT minimum, UnsignedT maximum) noexcept
{
    std::size_t at = 0u;
    while (at < text.size() && isSpace(text[at]))
        ++at;
    const bool negative = at < text.size() && text[at] == '-';
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
        ++at;
    UnsignedT result = 0u;
    bool overflow = false;
    if (at + 1u < text.size() && text[at] == '0' && lowered(text[at + 1u]) == 'x')
    {
        at += 2u;
        while (at < text.size() && text[at] == '0')
            ++at;
        const std::size_t start = at;
        for (; at < text.size(); ++at)
        {
            const char c = text[at];
            if (c >= '0' && c <= '9')
                result = static_cast<UnsignedT>(result * 16u + static_cast<UnsignedT>(c - '0'));
            else if (lowered(c) >= 'a' && lowered(c) <= 'f')
                result = static_cast<UnsignedT>(result * 16u + static_cast<UnsignedT>(lowered(c) - 'a' + 10));
            else
                break;
        }
        overflow = at - start > sizeof(UnsignedT) * 2u;
    }
    else
    {
        while (at < text.size() && text[at] == '0')
            ++at;
        const std::size_t start = at;
        for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at)
            result = static_cast<UnsignedT>(result * 10u + static_cast<UnsignedT>(text[at] - '0'));
        const std::size_t digits = at - start;
        const std::size_t maxDigits = sizeof(UnsignedT) == 8u ? 20u : 10u;
        const char maxLead = sizeof(UnsignedT) == 8u ? '1' : '4';
        const auto highBit = static_cast<unsigned>(sizeof(UnsignedT) * 8u - 1u);
        overflow =
            digits >= maxDigits &&
            !(digits == maxDigits && (text[start] < maxLead || (text[start] == maxLead && (result >> highBit) != 0u)));
    }
    if (negative)
        return (overflow || result > static_cast<UnsignedT>(0u - minimum)) ? minimum
                                                                           : static_cast<UnsignedT>(0u - result);
    return (overflow || result > maximum) ? maximum : result;
}

class DocumentReader
{
public:
    DocumentReader(std::string_view text, std::uint64_t maxFrames) noexcept : m_text(text), m_maxFrames(maxFrames) {}

    ParsedDocument read()
    {
        ParsedDocument parsed;
        // pugixml reads a document as a C string, so its first NUL ends it.
        if (const std::size_t nul = m_text.find('\0'); nul != std::string_view::npos)
            m_text = m_text.substr(0u, nul);
        if (m_text.starts_with("\xef\xbb\xbf"))
            m_at = 3u;
        while (m_at < m_text.size())
        {
            // Character data is not part of a manifest, wherever it appears.
            const std::size_t opening = m_text.find('<', m_at);
            if (opening == std::string_view::npos)
                break;
            m_at = opening;
            if (const DocumentProblem problem = markup(parsed.manifest); problem != DocumentProblem::None)
                return refused(problem);
        }
        if (!m_open.empty() || !m_sawElement)
            return refused(DocumentProblem::NotWellFormed);
        if (!m_manifestFound)
            return refused(DocumentProblem::NoManifest);
        return parsed;
    }

private:
    static ParsedDocument refused(DocumentProblem problem)
    {
        ParsedDocument parsed;
        parsed.problem = problem;
        return parsed;
    }

    [[nodiscard]] char charAt(std::size_t index) const noexcept
    {
        return index < m_text.size() ? m_text[index] : '\0';
    }

    DocumentProblem markup(ManifestElement& manifest)
    {
        const std::string_view rest = m_text.substr(m_at);
        if (rest.starts_with("<?"))
            return instruction();
        if (rest.starts_with("<!--"))
            return skipPast(4u, "-->");
        if (rest.starts_with("<![CDATA["))
            return skipPast(9u, "]]>");
        if (rest.starts_with("<!DOCTYPE"))
            return m_open.empty() ? documentType() : DocumentProblem::NotWellFormed;
        if (rest.starts_with("<!"))
            return DocumentProblem::NotWellFormed;
        if (rest.starts_with("</"))
            return endTag();
        return startTag(manifest);
    }

    DocumentProblem skipPast(std::size_t opening, std::string_view terminator) noexcept
    {
        const std::size_t end = m_text.find(terminator, m_at + opening);
        if (end == std::string_view::npos)
            return DocumentProblem::NotWellFormed;
        m_at = end + terminator.size();
        return DocumentProblem::None;
    }

    std::string_view name() noexcept
    {
        const std::size_t start = m_at;
        if (m_at >= m_text.size() || !isNameStart(m_text[m_at]))
            return {};
        ++m_at;
        while (m_at < m_text.size() && isNameChar(m_text[m_at]))
            ++m_at;
        return m_text.substr(start, m_at - start);
    }

    void skipSpace() noexcept
    {
        while (m_at < m_text.size() && isSpace(m_text[m_at]))
            ++m_at;
    }

    /// A processing instruction, the declaration included: a name, then anything up to `?>`.
    DocumentProblem instruction() noexcept
    {
        m_at += 2u;
        if (name().empty())
            return DocumentProblem::NotWellFormed;
        return skipPast(0u, "?>");
    }

    /// Skips a document type declaration as pugixml does: quoted strings, processing instructions
    /// and comments inside it are passed over whole, markup declarations nest, and ignored sections nest within
    /// themselves.
    DocumentProblem documentType() noexcept
    {
        std::size_t at = m_at + 2u;
        std::size_t depth = 0u;
        while (at < m_text.size())
        {
            const char c = m_text[at];
            if (c == '<' && charAt(at + 1u) == '!' && charAt(at + 2u) != '-')
            {
                if (charAt(at + 2u) == '[')
                {
                    at += 3u;
                    std::size_t nested = 0u;
                    bool closed = false;
                    while (at < m_text.size())
                    {
                        if (m_text.compare(at, 3u, "<![") == 0)
                        {
                            at += 3u;
                            ++nested;
                        }
                        else if (m_text.compare(at, 3u, "]]>") == 0)
                        {
                            at += 3u;
                            if (nested == 0u)
                            {
                                closed = true;
                                break;
                            }
                            --nested;
                        }
                        else
                            ++at;
                    }
                    if (!closed)
                        return DocumentProblem::NotWellFormed;
                }
                else
                {
                    at += 2u;
                    ++depth;
                }
            }
            else if (c == '"' || c == '\'')
            {
                const std::size_t end = m_text.find(c, at + 1u);
                if (end == std::string_view::npos)
                    return DocumentProblem::NotWellFormed;
                at = end + 1u;
            }
            else if (c == '<')
            {
                std::size_t end = std::string_view::npos;
                if (charAt(at + 1u) == '?')
                {
                    end = m_text.find("?>", at + 2u);
                    if (end != std::string_view::npos)
                        end += 2u;
                }
                else if (charAt(at + 1u) == '!' && charAt(at + 2u) == '-' && charAt(at + 3u) == '-')
                {
                    end = m_text.find("-->", at + 4u);
                    if (end != std::string_view::npos)
                        end += 3u;
                }
                if (end == std::string_view::npos)
                    return DocumentProblem::NotWellFormed;
                at = end;
            }
            else if (c == '>')
            {
                if (depth == 0u)
                {
                    m_at = at + 1u;
                    return DocumentProblem::None;
                }
                --depth;
                ++at;
            }
            else
                ++at;
        }
        return DocumentProblem::NotWellFormed;
    }

    DocumentProblem endTag() noexcept
    {
        m_at += 2u;
        const std::string_view closing = name();
        if (closing.empty() || m_open.empty() || closing != m_open.back())
            return DocumentProblem::NotWellFormed;
        skipSpace();
        if (m_at >= m_text.size() || m_text[m_at] != '>')
            return DocumentProblem::NotWellFormed;
        ++m_at;
        m_open.pop_back();
        if (m_open.empty())
            m_manifestOpen = false;
        return DocumentProblem::None;
    }

    static void keepFirst(ManifestElement& manifest, std::string_view attribute, std::string_view raw)
    {
        std::optional<std::string>* slot = nullptr;
        if (attribute == "version")
            slot = &manifest.version;
        else if (attribute == "timestamp")
            slot = &manifest.timestamp;
        else if (attribute == "width")
            slot = &manifest.width;
        else if (attribute == "height")
            slot = &manifest.height;
        else if (attribute == "channels")
            slot = &manifest.channels;
        else if (attribute == "isCFA")
            slot = &manifest.mosaiced;
        else if (attribute == "fps")
            slot = &manifest.framesPerSecond;
        if (slot != nullptr && !slot->has_value())
            *slot = decodeAttribute(raw);
    }

    DocumentProblem startTag(ManifestElement& manifest)
    {
        ++m_at;
        const std::string_view element = name();
        if (element.empty())
            return DocumentProblem::NotWellFormed;
        m_sawElement = true;
        const bool isManifest = m_open.empty() && !m_manifestFound && element == "sequence";
        const bool isFrame = m_manifestOpen && m_open.size() == 1u && element == "frame";
        std::optional<std::string_view> path;
        bool closed = false;
        for (;;)
        {
            const std::size_t before = m_at;
            skipSpace();
            if (m_at >= m_text.size())
                return DocumentProblem::NotWellFormed;
            const char next = m_text[m_at];
            if (next == '/')
            {
                if (charAt(m_at + 1u) != '>')
                    return DocumentProblem::NotWellFormed;
                m_at += 2u;
                closed = true;
                break;
            }
            if (next == '>')
            {
                ++m_at;
                break;
            }
            // Each attribute is separated from what precedes it by whitespace.
            if (m_at == before)
                return DocumentProblem::NotWellFormed;
            const std::string_view attribute = name();
            if (attribute.empty())
                return DocumentProblem::NotWellFormed;
            skipSpace();
            if (m_at >= m_text.size() || m_text[m_at] != '=')
                return DocumentProblem::NotWellFormed;
            ++m_at;
            skipSpace();
            if (m_at >= m_text.size() || (m_text[m_at] != '"' && m_text[m_at] != '\''))
                return DocumentProblem::NotWellFormed;
            const char quote = m_text[m_at++];
            const std::size_t end = m_text.find(quote, m_at);
            if (end == std::string_view::npos)
                return DocumentProblem::NotWellFormed;
            const std::string_view raw = m_text.substr(m_at, end - m_at);
            m_at = end + 1u;
            if (isManifest)
                keepFirst(manifest, attribute, raw);
            else if (isFrame && !path && attribute == "path")
                path = raw;
        }
        if (isManifest)
        {
            m_manifestFound = true;
            m_manifestOpen = !closed;
        }
        if (isFrame)
        {
            if (manifest.framePaths.size() >= m_maxFrames)
                return DocumentProblem::TooManyFrames;
            if (path)
                manifest.framePaths.emplace_back(decodeAttribute(*path));
            else
                manifest.framePaths.emplace_back(std::nullopt);
        }
        if (!closed)
        {
            if (m_open.size() >= kMaxDocumentDepth)
                return DocumentProblem::NotWellFormed;
            m_open.push_back(element);
        }
        return DocumentProblem::None;
    }

    std::string_view m_text;
    std::uint64_t m_maxFrames;
    std::size_t m_at = 0u;
    std::vector<std::string_view> m_open;
    bool m_sawElement = false;
    bool m_manifestFound = false;
    bool m_manifestOpen = false;
};

void appendUnsigned(std::string& text, std::uint64_t value)
{
    std::array<char, 24u> digits{};
    const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    text.append(digits.data(), written.ptr);
}

void appendReal(std::string& text, double value)
{
    // A rate is written as "%.4g" prints it, which is what the general format with a precision of four
    // is specified to produce.
    std::array<char, 64u> digits{};
    const auto written =
        std::to_chars(digits.data(), digits.data() + digits.size(), value, std::chars_format::general, 4);
    text.append(digits.data(), written.ptr);
}

constexpr bool isEscaped(char c) noexcept
{
    return c == '&' || c == '<' || c == '"' || static_cast<unsigned char>(c) < 32u;
}
} // namespace

ParsedDocument parseManifestDocument(std::string_view text, std::uint64_t maxFrames)
{
    return DocumentReader(text, maxFrames).read();
}

std::string decodeAttribute(std::string_view raw)
{
    std::string value;
    value.reserve(raw.size());
    for (std::size_t at = 0u; at < raw.size();)
    {
        const char c = raw[at];
        if (c == '\r')
        {
            value.push_back(' ');
            ++at;
            if (at < raw.size() && raw[at] == '\n')
                ++at;
        }
        else if (c == '\t' || c == '\n')
        {
            value.push_back(' ');
            ++at;
        }
        else if (c == '&')
            at = appendReference(value, raw, at);
        else
        {
            value.push_back(c);
            ++at;
        }
    }
    return value;
}

std::uint64_t readUnsigned(std::string_view text) noexcept
{
    return readWithLibraryRules<std::uint64_t>(text, 0u, (std::numeric_limits<std::uint64_t>::max)());
}

std::int32_t readInteger(std::string_view text) noexcept
{
    const auto minimum = static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::min)());
    const auto maximum = static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());
    return static_cast<std::int32_t>(readWithLibraryRules<std::uint32_t>(text, minimum, maximum));
}

double readReal(std::string_view text) noexcept
{
    std::size_t at = 0u;
    while (at < text.size() && (isSpace(text[at]) || text[at] == '\v' || text[at] == '\f'))
        ++at;
    bool negative = false;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
    {
        negative = text[at] == '-';
        ++at;
    }
    const char* first = text.data() + at;
    const char* const last = text.data() + text.size();
    const bool hexadecimal = last - first >= 2 && first[0] == '0' && lowered(first[1]) == 'x';
    if (hexadecimal)
        first += 2;
    // A second sign is not a number, and "0x" followed by no hexadecimal digit is the zero before the x.
    if (first < last && (*first == '+' || *first == '-'))
        return hexadecimal && negative ? -0.0 : 0.0;
    double value = 0.0;
    const std::from_chars_result parsed =
        std::from_chars(first, last, value, hexadecimal ? std::chars_format::hex : std::chars_format::general);
    if (parsed.ec == std::errc::invalid_argument)
        return hexadecimal && negative ? -0.0 : 0.0;
    if (parsed.ec == std::errc::result_out_of_range)
    {
        // strtod answers a value too large with an infinity and one too small with zero.
        const std::string_view matched(first, static_cast<std::size_t>(parsed.ptr - first));
        const std::size_t exponent = matched.find_first_of(hexadecimal ? "pP" : "eE");
        const bool tooSmall =
            exponent != std::string_view::npos && exponent + 1u < matched.size() && matched[exponent + 1u] == '-';
        value = tooSmall ? 0.0 : std::numeric_limits<double>::infinity();
    }
    return negative ? -value : value;
}

bool readBoolean(std::string_view text) noexcept
{
    if (text.empty())
        return false;
    const char first = text.front();
    return first == '1' || first == 't' || first == 'T' || first == 'y' || first == 'Y';
}

ManifestWriter::ManifestWriter(SequenceFile& file, std::string_view path,
                               sequence_detail::AccountedBlock buffer) noexcept
    : m_file(file), m_path(path), m_buffer(std::move(buffer))
{
}

Status ManifestWriter::head(const ExecutionContext& execution, const ManifestFacts& facts, bool empty)
{
    std::string text = "<?xml version=\"1.0\"?>\n<sequence version=\"1\" timestamp=\"";
    appendUnsigned(text, facts.timestampSeconds);
    text.append("\" width=\"");
    appendUnsigned(text, facts.width);
    text.append("\" height=\"");
    appendUnsigned(text, facts.height);
    text.append("\" channels=\"");
    appendUnsigned(text, facts.channels);
    text.append(facts.mosaiced ? "\" isCFA=\"true\" fps=\"" : "\" isCFA=\"false\" fps=\"");
    appendReal(text, facts.framesPerSecond);
    text.append(empty ? "\" />\n" : "\">\n");
    return put(execution, text);
}

Status ManifestWriter::frame(const ExecutionContext& execution, std::string_view path)
{
    if (const Status written = put(execution, "\t<frame path=\""); written.failed())
        return written;
    if (const Status written = putEscaped(execution, path); written.failed())
        return written;
    return put(execution, "\" />\n");
}

Status ManifestWriter::tail(const ExecutionContext& execution)
{
    return put(execution, "</sequence>\n");
}

Status ManifestWriter::flush(const ExecutionContext& execution)
{
    if (m_filled == 0u)
        return Status::success();
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (const int error = m_file.write({m_buffer.bytes(), static_cast<std::size_t>(m_filled)}); error != 0)
        return ioFailure(error, "the manifest could not be written", m_path);
    m_bytes += m_filled;
    m_filled = 0u;
    return Status::success();
}

Status ManifestWriter::put(const ExecutionContext& execution, std::string_view text)
{
    while (!text.empty())
    {
        if (m_filled == m_buffer.size())
            if (const Status flushed = flush(execution); flushed.failed())
                return flushed;
        const auto count =
            static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(text.size()), m_buffer.size() - m_filled));
        std::memcpy(m_buffer.bytes() + m_filled, text.data(), count);
        m_filled += count;
        text.remove_prefix(count);
    }
    return Status::success();
}

Status ManifestWriter::putEscaped(const ExecutionContext& execution, std::string_view text)
{
    while (!text.empty())
    {
        std::size_t plain = 0u;
        while (plain < text.size() && !isEscaped(text[plain]))
            ++plain;
        if (const Status written = put(execution, text.substr(0u, plain)); written.failed())
            return written;
        if (plain == text.size())
            break;
        const char c = text[plain];
        Status written;
        if (c == '&')
            written = put(execution, "&amp;");
        else if (c == '<')
            written = put(execution, "&lt;");
        else if (c == '"')
            written = put(execution, "&quot;");
        else
        {
            const auto code = static_cast<unsigned char>(c);
            const std::array<char, 5u> reference{'&', '#', static_cast<char>('0' + code / 10u),
                                                 static_cast<char>('0' + code % 10u), ';'};
            written = put(execution, {reference.data(), reference.size()});
        }
        if (written.failed())
            return written;
        text.remove_prefix(plain + 1u);
    }
    return Status::success();
}

bool namesManifest(std::string_view path) noexcept
{
    constexpr std::string_view kExtension = ".anseq";
    if (path.size() <= kExtension.size())
        return false;
    const std::string_view tail = path.substr(path.size() - kExtension.size());
    for (std::size_t i = 0u; i < kExtension.size(); ++i)
    {
        const char c = tail[i];
        const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
        if (lower != kExtension[i])
            return false;
    }
    const char before = path[path.size() - kExtension.size() - 1u];
    return before != '/' && before != '\\';
}

std::string_view directoryOf(std::string_view path) noexcept
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0u, slash + 1u);
}

std::string_view stemOf(std::string_view path) noexcept
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1u);
    return name.substr(0u, name.size() - 6u);
}

std::filesystem::path nativePath(std::string_view path)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

Status manifestFailure(ErrorCode code, SequenceFailure reason, const char* message, std::string_view parameter,
                       std::string_view path, std::vector<StatusContextField> extra)
{
    std::vector<StatusContextField> context;
    context.reserve(3u + extra.size());
    context.push_back(StatusContextField::ofCount(ContextKey::SequenceReason, static_cast<std::uint64_t>(reason)));
    if (!parameter.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Parameter, std::string(parameter)));
    if (!path.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Path, std::string(path)));
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(
        code,
        {.message = message, .operation = "image-list", .domain = ErrorDomain::Io, .context = std::move(context)});
}

Status argumentFailure(const char* message, std::string_view parameter, std::vector<StatusContextField> extra)
{
    std::vector<StatusContextField> context;
    context.reserve(1u + extra.size());
    context.push_back(StatusContextField::ofText(ContextKey::Parameter, std::string(parameter)));
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(
        ErrorCode::InvalidArgument,
        {.message = message, .operation = "image-list", .domain = ErrorDomain::Io, .context = std::move(context)});
}

Status ioFailure(int platformError, const char* message, std::string_view path)
{
    return manifestFailure(ErrorCode::ExternalFailure, SequenceFailure::IoFailure, message, {}, path,
                           {StatusContextField::ofNumber(ContextKey::LibraryCode, platformError)});
}

} // namespace an::image_list_detail
