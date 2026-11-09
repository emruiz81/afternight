// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a FITS header's cards from its records, and writing one back.
//
// This is the one reading of a card: the FITS codec hands it the records of an image's header, which OpenFITS keeps as
// the file holds them, and a header-only document hands it its own, so the two carry equal cards. The reading is the
// one the codec has always given a header, which CFITSIO's readers defined while it read them: a record is trimmed of
// its trailing blanks, its keyword is read as `ffgknm` reads it and its value and comment as `ffpsvc` splits them,
// and a string value is unquoted and requoted as the codec did after `fits_read_key_longstr`, which also joins a long
// string's `CONTINUE` records. A record whose keyword is blank is skipped, and so is every `CONTINUE` record right
// after a string. So a card an earlier version of the engine stored - an artifact's stamp among them - reads back as
// it was written.
//
// The two layouts differ only in how records are found. Records are eighty bytes each, laid end to
// end; lines are split at line feeds, with a carriage return before one dropped and blank lines
// skipped. Either way the first `END` record ends the header and nothing after it is looked at. The codec's records
// are always records, and a byte outside printable ASCII in them is kept, as the codec always kept it; a document read
// on its own is refused for one.
//
// The way back, a card to its record, restates CFITSIO's writer the same way: `ffmkky` for a keyword,
// `ffs2c` for a string's quoting and `ffpcom` and `ffphis` for commentary.

#include <AfterNightEngine/IO/FitsHeaderCards.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "core/Utf8.h"
#include "io/fits/FitsCardRecords.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kParseOperation = "an::parseFitsHeaderCards";
constexpr const char* kReadOperation = "an::readFitsHeaderCards";
constexpr const char* kFormatOperation = "an::formatFitsCardRecord";

/// The bytes of one header record.
constexpr std::size_t kRecordBytes = 80u;

/// How many leading bytes decide the layout: a record and the carriage return and line feed after it.
constexpr std::size_t kLayoutProbeBytes = kRecordBytes + 2u;

/// The longest keyword CFITSIO's keyword reading returns.
constexpr std::size_t kMaxKeywordBytes = 74u;

/// Why a header could not be read, beyond the failure itself.
enum class HeaderFault : std::uint8_t
{
    /// Nothing went wrong.
    None,

    /// The bytes ended before an `END` record.
    MissingEnd,

    /// Anything else.
    Other
};

/// A header failure carrying how many cards were read before it.
///
/// @param message the explanation for a person.
/// @param cardsRead the cards read before the fault.
/// @return the complete failure.
[[nodiscard]] Status headerFailure(std::string message, const std::uint64_t cardsRead)
{
    return Status::failure(ErrorCode::InvalidData,
                           StatusDetail{.message = std::move(message),
                                        .operation = kParseOperation,
                                        .domain = ErrorDomain::Io,
                                        .context = {StatusContextField::ofCount(ContextKey::Completed, cardsRead)}});
}

/// The record without its trailing blanks.
///
/// @param record the record's bytes.
/// @return the trimmed view.
[[nodiscard]] std::string_view withoutTrailingBlanks(std::string_view record) noexcept
{
    while (!record.empty() && record.back() == ' ')
    {
        record.remove_suffix(1u);
    }
    return record;
}

/// Whether a record holds a byte no header text may hold.
///
/// @param record the record's bytes.
/// @return true for a byte below 32 or the byte 127.
[[nodiscard]] bool holdsControlByte(const std::string_view record) noexcept
{
    return std::any_of(record.begin(), record.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 32u || byte == 127u;
    });
}

/// Whether a record is the one that ends a header: `END` in columns one to eight.
///
/// @param record the record, trailing blanks removed.
/// @return true for an `END` record.
[[nodiscard]] bool isEndRecord(const std::string_view record) noexcept
{
    if (!record.starts_with("END"))
    {
        return false;
    }
    const std::string_view rest = record.substr(3u, (std::min)(record.size(), std::size_t{8u}) - 3u);
    return std::all_of(rest.begin(), rest.end(), [](const char character) { return character == ' '; });
}

/// The keyword a record names, as CFITSIO reads it.
///
/// @param card the record, trailing blanks removed.
/// @return up to the first blank or `=`, or after `HIERARCH ` the name before the `=`.
[[nodiscard]] std::string_view keywordOf(const std::string_view card) noexcept
{
    if (card.starts_with("HIERARCH "))
    {
        const std::size_t equals = card.find('=');
        if (equals == std::string_view::npos)
        {
            return card.substr(0u, 8u);
        }
        std::size_t begin = 9u;
        while (begin < equals && card[begin] == ' ')
        {
            ++begin;
        }
        std::size_t end = equals;
        while (end > begin && card[end - 1u] == ' ')
        {
            --end;
        }
        return card.substr(begin, (std::min)(end - begin, kMaxKeywordBytes));
    }
    const std::size_t end = card.find_first_of(" =");
    return card.substr(0u, (std::min)((std::min)(end, card.size()), kMaxKeywordBytes));
}

/// A record's value and comment as CFITSIO splits them.
struct ValueAndComment
{
    /// The value's text; a string keeps its quotes, its doubled quotes and every blank inside them.
    std::string_view value{};

    /// The comment, without the slash that introduced it, one blank after that, or trailing blanks.
    std::string_view comment{};

    /// Whether the value is a string, which the codec unquotes and requotes.
    bool stringValue = false;

    /// Whether the string ran to the end of the record without a closing quote.
    bool unterminatedString = false;

    /// Whether a complex value never closed its parenthesis.
    bool unclosedComplex = false;
};

/// The comment that runs from a column to the end of the record.
///
/// @param card the record, trailing blanks removed.
/// @param from the first column of the comment.
/// @return the comment, empty when the record ends before it.
[[nodiscard]] std::string_view commentFrom(const std::string_view card, const std::size_t from) noexcept
{
    return card.size() > from ? withoutTrailingBlanks(card.substr(from)) : std::string_view{};
}

/// Splits a record's value and comment as CFITSIO's `ffpsvc` does.
///
/// @param card the record, trailing blanks removed.
/// @return the value and comment.
[[nodiscard]] ValueAndComment splitValueAndComment(const std::string_view card) noexcept
{
    ValueAndComment out;
    std::size_t valuePosition = 0u;
    if (card.starts_with("HIERARCH "))
    {
        const std::size_t equals = card.find('=');
        if (equals == std::string_view::npos)
        {
            out.comment = commentFrom(card, 8u);
            return out;
        }
        valuePosition = equals + 1u;
    }
    else if (card.size() < 9u || card.starts_with("COMMENT ") || card.starts_with("HISTORY ") ||
             card.starts_with("END     ") || card.starts_with("CONTINUE") || card.starts_with("        "))
    {
        out.comment = commentFrom(card, 8u);
        return out;
    }
    else if (card.substr(8u, 2u) == "= ")
    {
        valuePosition = 10u;
    }
    else
    {
        const std::size_t equals = card.find('=');
        if (equals == std::string_view::npos)
        {
            out.comment = commentFrom(card, 8u);
            return out;
        }
        valuePosition = equals + 1u;
    }

    std::size_t index = valuePosition;
    while (index < card.size() && card[index] == ' ')
    {
        ++index;
    }
    if (index == card.size())
    {
        return out;
    }

    if (card[index] == '/')
    {
        ++index;
    }
    else if (card[index] == '\'')
    {
        const std::size_t start = index;
        ++index;
        bool closed = false;
        while (index < card.size())
        {
            if (card[index] == '\'')
            {
                if (index + 1u < card.size() && card[index + 1u] == '\'')
                {
                    index += 2u;
                    continue;
                }
                closed = true;
                break;
            }
            ++index;
        }
        out.stringValue = true;
        out.unterminatedString = !closed;
        out.value = card.substr(start, closed ? index - start + 1u : index - start);
        index = closed ? index + 1u : index;
    }
    else if (card[index] == '(')
    {
        const std::size_t close = card.find(')', index);
        if (close == std::string_view::npos)
        {
            out.unclosedComplex = true;
            return out;
        }
        out.value = card.substr(index, close - index + 1u);
        index = close + 1u;
    }
    else
    {
        const std::size_t end = card.find_first_of(" /", index);
        const std::size_t stop = end == std::string_view::npos ? card.size() : end;
        out.value = card.substr(index, stop - index);
        index = stop;
    }

    while (index < card.size() && card[index] == ' ')
    {
        ++index;
    }
    if (index < card.size())
    {
        if (card[index] == '/')
        {
            ++index;
            if (index < card.size() && card[index] == ' ')
            {
                ++index;
            }
        }
        out.comment = commentFrom(card, index);
    }
    return out;
}

/// A string value's content as CFITSIO's `ffc2s` gives it: no enclosing quotes, doubled quotes
/// undone, trailing blanks removed.
///
/// @param value the string value with its opening quote, closed or not.
/// @return the content.
[[nodiscard]] std::string stringContent(const std::string_view value)
{
    std::string content;
    content.reserve(value.size());
    for (std::size_t index = 1u; index < value.size(); ++index)
    {
        if (value[index] == '\'')
        {
            if (index + 1u < value.size() && value[index + 1u] == '\'')
            {
                content.push_back('\'');
                ++index;
                continue;
            }
            break;
        }
        content.push_back(value[index]);
    }
    while (!content.empty() && content.back() == ' ')
    {
        content.pop_back();
    }
    return content;
}

/// The value and comment a `CONTINUE` record carries, as CFITSIO's `ffgcnt` reads them.
///
/// @param card the record, trailing blanks removed.
/// @return the continuation's string content and comment, or nothing when the record is not a
///         continuation - its first ten columns are not `CONTINUE` and two blanks.
[[nodiscard]] std::optional<std::pair<std::string, std::string>> continuationOf(const std::string_view card)
{
    if (!card.starts_with("CONTINUE  "))
    {
        return std::nullopt;
    }
    // CFITSIO overwrites the first ten columns with a keyword and a value indicator and splits the rest.
    const std::string standIn = "D2345678= " + std::string(card.substr(10u));
    const ValueAndComment split = splitValueAndComment(standIn);
    std::string content;
    if (split.stringValue)
    {
        content = stringContent(split.value);
    }
    else
    {
        content = std::string(split.value);
    }
    return std::make_pair(std::move(content), std::string(split.comment));
}

/// Whose header the records are, which decides how they are found and what a record may hold.
enum class CardReading : std::uint8_t
{
    /// A header-only document: its layout decided by its first bytes, and a control byte refused.
    Document,

    /// An image's header, as the codec hands it over from the file: records always, every byte kept.
    Codec
};

/// The records of a header, one at a time, in either layout.
class RecordReader
{
public:
    /// Reads records from the header's bytes, whose layout the first bytes decide for a document; the codec's are
    /// always records.
    ///
    /// @param header the bytes, which must outlive this object.
    /// @param reading whose header it is.
    RecordReader(const std::string_view header, const CardReading reading) noexcept
        : m_header(header),
          m_lines(reading == CardReading::Document &&
                  header.substr(0u, (std::min)(header.size(), kLayoutProbeBytes)).find('\n') != std::string_view::npos)
    {
    }

    /// The next record, trailing blanks removed, or nothing at the end of the bytes.
    ///
    /// @param malformed set when a line is longer than a record; the reader then stops.
    /// @return the record.
    [[nodiscard]] std::optional<std::string_view> next(bool& malformed) noexcept
    {
        malformed = false;
        if (!m_lines)
        {
            if (m_header.size() - m_position < kRecordBytes)
            {
                return std::nullopt;
            }
            const std::string_view record = m_header.substr(m_position, kRecordBytes);
            m_position += kRecordBytes;
            return withoutTrailingBlanks(record);
        }
        while (m_position < m_header.size())
        {
            const std::size_t feed = m_header.find('\n', m_position);
            const std::size_t end = feed == std::string_view::npos ? m_header.size() : feed;
            std::string_view line = m_header.substr(m_position, end - m_position);
            m_position = feed == std::string_view::npos ? m_header.size() : feed + 1u;
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1u);
            }
            line = withoutTrailingBlanks(line);
            if (line.empty())
            {
                continue;
            }
            if (line.size() > kRecordBytes)
            {
                malformed = true;
                return std::nullopt;
            }
            return line;
        }
        return std::nullopt;
    }

    /// Looks at the next record without consuming it.
    ///
    /// @return the record, or nothing at the end of the bytes or before a malformed line.
    [[nodiscard]] std::optional<std::string_view> peek() noexcept
    {
        const std::size_t position = m_position;
        bool malformed = false;
        const std::optional<std::string_view> record = next(malformed);
        m_position = position;
        return malformed ? std::nullopt : record;
    }

private:
    /// The header's bytes.
    std::string_view m_header;

    /// Where the next record starts.
    std::size_t m_position = 0u;

    /// Whether the header is laid out as lines rather than records.
    bool m_lines = false;
};

/// Reads a header's cards, and says when the only fault was a missing `END` record.
///
/// @param header the bytes.
/// @param fault set to why the read failed, or `HeaderFault::None`.
/// @param reading whose header it is.
/// @return the cards, or the failure `parseFitsHeaderCards` documents.
[[nodiscard]] Result<std::vector<CodecCard>> readCards(const std::string_view header, HeaderFault& fault,
                                                       const CardReading reading)
{
    fault = HeaderFault::Other;
    std::vector<CodecCard> cards;
    RecordReader reader(header, reading);
    // A record a document may not hold, which also ends a long string's continuation.
    const auto refused = [reading](const std::string_view record) noexcept {
        return reading == CardReading::Document && holdsControlByte(record);
    };
    while (true)
    {
        bool malformed = false;
        const std::optional<std::string_view> record = reader.next(malformed);
        if (malformed)
        {
            return headerFailure("a line is longer than a header record's eighty bytes", cards.size());
        }
        if (!record.has_value())
        {
            fault = HeaderFault::MissingEnd;
            return headerFailure("the header ends without an END record", cards.size());
        }
        const std::string_view card = *record;
        if (refused(card))
        {
            return headerFailure("a header record holds a control character", cards.size());
        }
        if (isEndRecord(card))
        {
            break;
        }
        const std::string_view keyword = keywordOf(card);
        if (keyword.empty())
        {
            continue;
        }
        const ValueAndComment split = splitValueAndComment(card);
        if (split.unclosedComplex)
        {
            return headerFailure("a complex value never closes its parenthesis", cards.size());
        }

        std::string value(split.value);
        if (split.stringValue)
        {
            // The codec reads every string through the long-string reader, which joins the CONTINUE
            // records a value ending in an ampersand announces, and then requotes what it read.
            std::string content = stringContent(split.value);
            while (!content.empty() && content.back() == '&')
            {
                const std::optional<std::string_view> following = reader.peek();
                if (!following.has_value() || refused(*following))
                {
                    break;
                }
                const std::optional<std::pair<std::string, std::string>> continuation = continuationOf(*following);
                if (!continuation.has_value() || (continuation->first.empty() && continuation->second.empty()))
                {
                    break;
                }
                content.pop_back();
                content += continuation->first;
                bool ignored = false;
                (void)reader.next(ignored);
            }
            if (content.size() > kMaxCodecCardTextBytes - 2u)
            {
                return Status::failure(
                    ErrorCode::LimitExceeded,
                    StatusDetail{.message = "a long string value is longer than one card's text holds",
                                 .operation = kParseOperation,
                                 .domain = ErrorDomain::Io,
                                 .context = {StatusContextField::ofCount(ContextKey::Requested, content.size() + 2u),
                                             StatusContextField::ofCount(ContextKey::Limit, kMaxCodecCardTextBytes)}});
            }
            value.assign(1u, '\'');
            for (const char character : content)
            {
                value.push_back(character);
                if (character == '\'')
                {
                    value.push_back('\'');
                }
            }
            value.push_back('\'');
            // Every CONTINUE record right after a string is the string's, whether it was joined or not,
            // and so is a record with a blank keyword between them.
            while (true)
            {
                const std::optional<std::string_view> following = reader.peek();
                if (!following.has_value() || refused(*following) || isEndRecord(*following))
                {
                    break;
                }
                const std::string_view followingKeyword = keywordOf(*following);
                if (!followingKeyword.empty() && followingKeyword != "CONTINUE")
                {
                    break;
                }
                bool ignored = false;
                (void)reader.next(ignored);
            }
        }

        if (cards.size() == kMaxCodecCards)
        {
            return Status::failure(
                ErrorCode::LimitExceeded,
                StatusDetail{.message = "the header holds more cards than one image preserves",
                             .operation = kParseOperation,
                             .domain = ErrorDomain::Io,
                             .context = {StatusContextField::ofCount(ContextKey::Requested, cards.size() + 1u),
                                         StatusContextField::ofCount(ContextKey::Limit, kMaxCodecCards)}});
        }
        Result<CodecCard> made =
            CodecCard::create(CodecNamespace::Fits, std::string(keyword), std::move(value), std::string(split.comment));
        if (!made.hasValue())
        {
            return made.status();
        }
        cards.push_back(std::move(*made));
    }
    fault = HeaderFault::None;
    return cards;
}

/// A failure naming the parameter at fault.
///
/// @param message the explanation for a person.
/// @param parameter the name of what was refused.
/// @return the complete failure.
[[nodiscard]] Status argumentFailure(const char* const message, const char* const parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = kReadOperation,
                                        .domain = ErrorDomain::Io,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// A failure of the operating system's file access.
///
/// @param message the explanation for a person.
/// @param path the file's path.
/// @param code the operating system's code, or zero when it gave none.
/// @return the complete failure.
[[nodiscard]] Status fileFailure(const char* const message, const std::string_view path, const std::int64_t code = 0)
{
    std::vector<StatusContextField> context{StatusContextField::ofText(ContextKey::Path, std::string(path))};
    if (code != 0)
    {
        context.push_back(StatusContextField::ofNumber(ContextKey::LibraryCode, code));
    }
    return Status::failure(ErrorCode::ExternalFailure, StatusDetail{.message = message,
                                                                    .operation = kReadOperation,
                                                                    .domain = ErrorDomain::Io,
                                                                    .context = std::move(context)});
}

/// The columns a header record holds.
constexpr std::size_t kRecordColumns = 80u;

/// The column a short value or a short string's comment reaches before its comment separator.
constexpr std::size_t kValueEndColumn = 30u;

/// The fewest characters CFITSIO writes between a string's quotes.
constexpr std::size_t kMinimumStringContent = 8u;

/// The most text one commentary record holds after its keyword.
constexpr std::size_t kMaxCommentaryText = 72u;

/// Whether a keyword is a standard FITS keyword, as CFITSIO's `fftkey` decides: at most eight
/// characters, each an upper-case letter, a digit, a hyphen or an underscore.
///
/// @param keyword the keyword.
/// @return true when it is written in the first eight columns.
[[nodiscard]] bool isStandardKeyword(const std::string_view keyword) noexcept
{
    return !keyword.empty() && keyword.size() <= 8u && std::all_of(keyword.begin(), keyword.end(), [](const char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

/// A string value quoted as CFITSIO's `ffs2c` quotes one: blanks added before the closing quote until
/// the content holds at least eight characters.
///
/// @param value the value with its quotes, as the codec reads it.
/// @return the value as a record holds it.
[[nodiscard]] std::string paddedString(const std::string_view value)
{
    if (value.size() < 2u || value.back() != '\'')
    {
        return std::string(value);
    }
    std::string padded(value.substr(0u, value.size() - 1u));
    while (padded.size() - 1u < kMinimumStringContent)
    {
        padded.push_back(' ');
    }
    padded.push_back('\'');
    return padded;
}

/// A record that would not fit its eighty columns.
///
/// @param length the columns the keyword and value need.
/// @return the failure, carrying the length and the bound.
[[nodiscard]] Status recordTooLong(const std::size_t length)
{
    return Status::failure(ErrorCode::LimitExceeded,
                           StatusDetail{.message = "the card does not fit one header record",
                                        .operation = kFormatOperation,
                                        .domain = ErrorDomain::Io,
                                        .context = {StatusContextField::ofCount(ContextKey::Requested, length),
                                                    StatusContextField::ofCount(ContextKey::Limit, kRecordColumns)}});
}

/// The record without its trailing blanks.
///
/// @param record the record being built.
/// @return the record.
[[nodiscard]] std::string trimmedRecord(std::string record)
{
    while (!record.empty() && record.back() == ' ')
    {
        record.pop_back();
    }
    return record;
}

/// The record of one card, as CFITSIO's `ffmkky` and its commentary writers lay it out.
///
/// @param card the card.
/// @return the record, or why it cannot be written as one.
[[nodiscard]] Result<std::string> recordOf(const CodecCard& card)
{
    const std::string& keyword = card.keyword();
    const std::string& value = card.value();
    const std::string& comment = card.comment();
    if (keyword.empty() || keyword.find('=') != std::string::npos)
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "a header record's keyword is nonempty and holds no '='",
                                            .operation = kFormatOperation,
                                            .domain = ErrorDomain::Io});
    }
    if (keyword == "COMMENT" || keyword == "HISTORY")
    {
        const std::string& text = value.empty() ? comment : value;
        if (text.size() > kMaxCommentaryText)
        {
            return recordTooLong(8u + text.size());
        }
        return trimmedRecord(keyword + " " + text);
    }
    if (keyword == "CONTINUE")
    {
        std::string record = "CONTINUE  " + value;
        if (record.size() > kRecordColumns)
        {
            return recordTooLong(record.size());
        }
        if (!comment.empty() && record.size() + 3u < kRecordColumns)
        {
            record += " / " + comment.substr(0u, kRecordColumns - 3u - record.size());
        }
        return trimmedRecord(std::move(record));
    }

    const bool quoted = !value.empty() && value.front() == '\'';
    const std::string written = quoted ? paddedString(value) : value;
    std::string record;
    if (isStandardKeyword(keyword))
    {
        record = keyword;
        record.resize(8u, ' ');
        record += "= ";
    }
    else
    {
        record = "HIERARCH " + keyword;
        record += record.size() + 3u + written.size() > kRecordColumns ? "= " : " = ";
    }

    if (written.empty())
    {
        if (isStandardKeyword(keyword))
        {
            // A keyword with no value has no value indicator, and its comment is read from column nine with the
            // blanks before it, so it is written back from there.
            record.resize(8u);
            record += comment.substr(0u, kRecordColumns - record.size());
        }
        return trimmedRecord(std::move(record));
    }
    if (record.size() + written.size() > kRecordColumns)
    {
        return recordTooLong(record.size() + written.size());
    }
    if (!quoted && record.size() + written.size() < kValueEndColumn)
    {
        record.append(kValueEndColumn - record.size() - written.size(), ' ');
    }
    record += written;
    if (quoted && !comment.empty() && record.size() < kValueEndColumn)
    {
        record.resize(kValueEndColumn, ' ');
    }
    if (!comment.empty() && record.size() < kRecordColumns - 3u)
    {
        record += " / " + comment.substr(0u, kRecordColumns - 3u - record.size());
    }
    return trimmedRecord(std::move(record));
}

} // namespace

Result<std::vector<CodecCard>> parseFitsHeaderCards(const std::string_view header)
{
    constexpr ExternalBoundary kBoundary{
        .library = "standard-library", .operation = kParseOperation, .domain = ErrorDomain::Io};
    try
    {
        HeaderFault fault = HeaderFault::None;
        return readCards(header, fault, CardReading::Document);
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

Result<std::vector<CodecCard>> readFitsRecordCards(const std::string_view records)
{
    constexpr ExternalBoundary kBoundary{
        .library = "standard-library", .operation = "an::readFitsRecordCards", .domain = ErrorDomain::Io};
    try
    {
        HeaderFault fault = HeaderFault::None;
        return readCards(records, fault, CardReading::Codec);
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

Result<std::vector<CodecCard>> readFitsHeaderCards(const ExecutionContext& execution, const std::string_view path,
                                                   const std::uint64_t maximumBytes)
{
    if (!execution.valid())
    {
        return Status::failure(ErrorCode::NotInitialized);
    }
    if (!isBoundaryPath(path))
    {
        return argumentFailure("a header path is nonempty bounded UTF-8 without NUL", "path");
    }
    if (maximumBytes == 0u)
    {
        return argumentFailure("a header read reads at least one byte", "maximumBytes");
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    constexpr ExternalBoundary kBoundary{
        .library = "filesystem", .operation = kReadOperation, .domain = ErrorDomain::Io};
    try
    {
        const std::filesystem::path native(
            std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
        std::error_code sizeError;
        const std::uint64_t size = std::filesystem::file_size(native, sizeError);
        if (sizeError)
        {
            return fileFailure("the header file cannot be inspected", path, sizeError.value());
        }
        const std::uint64_t toRead = (std::min)(size, maximumBytes);
        std::ifstream input(native, std::ios::binary);
        if (!input.is_open())
        {
            return fileFailure("the header file cannot be opened", path);
        }
        std::string bytes(static_cast<std::size_t>(toRead), '\0');
        if (toRead != 0u)
        {
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        if (!input || static_cast<std::uint64_t>(input.gcount()) != toRead)
        {
            return fileFailure("the header file could not be read", path);
        }
        HeaderFault fault = HeaderFault::None;
        Result<std::vector<CodecCard>> cards = readCards(bytes, fault, CardReading::Document);
        if (!cards.hasValue())
        {
            if (fault == HeaderFault::MissingEnd && size > maximumBytes)
            {
                return Status::failure(
                    ErrorCode::LimitExceeded,
                    StatusDetail{.message = "no END record lies within the bytes the read may take",
                                 .operation = kReadOperation,
                                 .domain = ErrorDomain::Io,
                                 .context = {StatusContextField::ofCount(ContextKey::Requested, size),
                                             StatusContextField::ofCount(ContextKey::Limit, maximumBytes)}});
            }
            return cards.status();
        }
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        return cards;
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

Result<std::string> formatFitsCardRecord(const CodecCard& card)
{
    constexpr ExternalBoundary kBoundary{
        .library = "standard-library", .operation = kFormatOperation, .domain = ErrorDomain::Io};
    try
    {
        return recordOf(card);
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

} // namespace an
