// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the `AN` processing-state card family, read into records and written back.
//
// One family holds three groups of cards: the processing state (`ANLIN`, `ANBASIS` and the rest), the
// colour-calibration record (`ANCC*`, `ANRSP*`, `ANCAT*` and the rest), and the transport of the
// calibration record's JSON document (`ANCJ*`), whose rules are documented on the header. Reading is
// lenient - a spelling compares in ASCII lower case, and a list takes the numbers it can find - and
// writing goes through `FitsCardEdit`, which places every card by one rule, so a header reads the same
// whoever wrote it.
//
// The document's checksum is 64-bit FNV-1a over the document's bytes. It detects a chunk damaged or
// edited by a tool that knew only cards; it is not a cryptographic seal.

#include <AfterNightEngine/IO/ProcessingStateCards.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include "interop/json/CalibrationDocument.h"
#include "io/metadata/FitsCardEdit.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace an
{
namespace
{
using std::string_view_literals::operator""sv;

constexpr const char* kWriteStateOperation = "an::withProcessingStateCards";
constexpr const char* kClearOperation = "an::withoutProcessingStateCards";
constexpr const char* kWriteCalibrationOperation = "an::withColorCalibrationCards";
constexpr const char* kWritePayloadOperation = "an::withColorCalibrationPayload";

// The processing-state cards.
constexpr std::string_view kStateVersionKeyword = "ANSTVER"sv;
constexpr std::string_view kLinearityKeyword = "ANLIN"sv;
constexpr std::string_view kChannelBasisKeyword = "ANBASIS"sv;
constexpr std::string_view kAssignmentSourceKeyword = "ANASRC"sv;
constexpr std::string_view kWhiteBalanceModeKeyword = "ANWBMOD"sv;
constexpr std::string_view kWhiteBalanceAppliedKeyword = "ANWBAPP"sv;
constexpr std::string_view kWhiteBalanceMultipliersKeyword = "ANWBMUL"sv;
constexpr std::string_view kCameraMatrixKeyword = "ANCMATP"sv;
constexpr std::string_view kOperationKeyword = "ANOP"sv;
constexpr std::string_view kAttestedPathKeyword = "ANASFILE"sv;
constexpr std::string_view kAttestedHashKeyword = "ANASHSH"sv;
constexpr std::string_view kAttestedByKeyword = "ANASBY"sv;
constexpr std::string_view kAttestedTimeKeyword = "ANASUTC"sv;
constexpr std::string_view kAttestedToolKeyword = "ANASTOOL"sv;
constexpr std::string_view kAttestedWarningsKeyword = "ANASWARN"sv;

// The colour-calibration cards.
constexpr std::string_view kCalibrationKeyword = "ANCCAL"sv;
constexpr std::string_view kGainsKeyword = "ANCCGN"sv;
constexpr std::string_view kOffsetsKeyword = "ANCCOF"sv;
constexpr std::string_view kResponseProfileKeyword = "ANRSPID"sv;
constexpr std::string_view kResponseVersionKeyword = "ANRSPVR"sv;
constexpr std::string_view kWhiteReferenceKeyword = "ANWREF"sv;
constexpr std::string_view kWhiteVersionKeyword = "ANWREFV"sv;
constexpr std::string_view kCatalogFamilyKeyword = "ANCATF"sv;
constexpr std::string_view kCatalogManifestKeyword = "ANCATM"sv;
constexpr std::string_view kCatalogAccessKeyword = "ANCATP"sv;
constexpr std::string_view kAlgorithmVersionKeyword = "ANALGOV"sv;
constexpr std::string_view kInputLinearityKeyword = "ANINLIN"sv;
constexpr std::string_view kInputBasisKeyword = "ANINBAS"sv;

// The calibration document's transport.
constexpr std::string_view kPayloadVersionKeyword = "ANCJVER"sv;
constexpr std::string_view kPayloadChunkCountKeyword = "ANCJCNT"sv;
constexpr std::string_view kPayloadLengthKeyword = "ANCJLEN"sv;
constexpr std::string_view kPayloadChecksumKeyword = "ANCJCHK"sv;
constexpr std::string_view kPayloadChunkPrefix = "ANCJ"sv;
constexpr std::size_t kPayloadChunkDigits = 32u;
constexpr std::size_t kMaxPayloadChunks = 10000u;

/// Every keyword of the family whose name is fixed; the numbered chunks are recognised by their shape.
constexpr std::array kFamilyKeywords = {kStateVersionKeyword,
                                        kLinearityKeyword,
                                        kChannelBasisKeyword,
                                        kAssignmentSourceKeyword,
                                        kWhiteBalanceModeKeyword,
                                        kWhiteBalanceAppliedKeyword,
                                        kWhiteBalanceMultipliersKeyword,
                                        kCameraMatrixKeyword,
                                        kOperationKeyword,
                                        kAttestedPathKeyword,
                                        kAttestedHashKeyword,
                                        kAttestedByKeyword,
                                        kAttestedTimeKeyword,
                                        kAttestedToolKeyword,
                                        kAttestedWarningsKeyword,
                                        kCalibrationKeyword,
                                        kGainsKeyword,
                                        kOffsetsKeyword,
                                        kResponseProfileKeyword,
                                        kResponseVersionKeyword,
                                        kWhiteReferenceKeyword,
                                        kWhiteVersionKeyword,
                                        kCatalogFamilyKeyword,
                                        kCatalogManifestKeyword,
                                        kCatalogAccessKeyword,
                                        kAlgorithmVersionKeyword,
                                        kInputLinearityKeyword,
                                        kInputBasisKeyword,
                                        kPayloadVersionKeyword,
                                        kPayloadChunkCountKeyword,
                                        kPayloadLengthKeyword,
                                        kPayloadChecksumKeyword};

constexpr std::string_view kWhitespace = " \t\n\v\f\r"sv;

std::string_view stripped(std::string_view text) noexcept
{
    const std::size_t first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(kWhitespace) - first + 1u);
}

/// A card's text: white space and one pair of single quotes removed, a doubled quote inside them read as
/// one, and the blanks a FITS string pads its value with removed again.
std::string cardText(std::string_view value)
{
    value = stripped(value);
    if (value.size() < 2u || value.front() != '\'' || value.back() != '\'')
        return std::string(value);
    const std::string_view inner = value.substr(1u, value.size() - 2u);
    std::string text;
    text.reserve(inner.size());
    for (std::size_t i = 0u; i < inner.size(); ++i)
    {
        text.push_back(inner[i]);
        if (inner[i] == '\'' && i + 1u < inner.size() && inner[i + 1u] == '\'')
            ++i;
    }
    return std::string(stripped(text));
}

std::string readCard(const CodecMetadata& codec, std::string_view keyword)
{
    const CodecCard* const card = codec.find(CodecNamespace::Fits, keyword);
    return card != nullptr ? cardText(card->value()) : std::string();
}

/// A spelling as it is compared: the card's text in ASCII lower case.
std::string spelling(std::string_view text)
{
    std::string lowered = cardText(text);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

RecordedLinearity linearityOf(std::string_view text)
{
    const std::string word = spelling(text);
    for (const RecordedLinearity linearity : {RecordedLinearity::Linear, RecordedLinearity::Nonlinear})
        if (word == stateCardText(linearity))
            return linearity;
    return RecordedLinearity::Unknown;
}

RecordedChannelBasis basisOf(std::string_view text)
{
    const std::string word = spelling(text);
    for (const RecordedChannelBasis basis :
         {RecordedChannelBasis::RawCfa, RecordedChannelBasis::LinearRgb, RecordedChannelBasis::CameraRgb,
          RecordedChannelBasis::DisplayRgb, RecordedChannelBasis::Monochrome})
        if (word == stateCardText(basis))
            return basis;
    return RecordedChannelBasis::Unknown;
}

RecordedColorCalibration calibrationOf(std::string_view text)
{
    const std::string word = spelling(text);
    if (word.empty())
        return RecordedColorCalibration::None;
    for (const RecordedColorCalibration calibration :
         {RecordedColorCalibration::None, RecordedColorCalibration::Photometric,
          RecordedColorCalibration::Spectrophotometric, RecordedColorCalibration::Manual})
        if (word == stateCardText(calibration))
            return calibration;
    return RecordedColorCalibration::Unknown;
}

StateAssignmentSource sourceOf(std::string_view text)
{
    const std::string word = spelling(text);
    for (const StateAssignmentSource source :
         {StateAssignmentSource::Loader, StateAssignmentSource::Process, StateAssignmentSource::UserAttestation})
        if (word == stateCardText(source))
            return source;
    return StateAssignmentSource::None;
}

RecordedWhiteBalance whiteBalanceOf(std::string_view text)
{
    const std::string word = spelling(text);
    for (const RecordedWhiteBalance balance : {RecordedWhiteBalance::None, RecordedWhiteBalance::CameraAsShot,
                                               RecordedWhiteBalance::Automatic, RecordedWhiteBalance::Custom})
        if (word == stateCardText(balance))
            return balance;
    return RecordedWhiteBalance::Daylight;
}

bool statesTrue(std::string_view text)
{
    const std::string word = spelling(text);
    return word == "t"sv || word == "true"sv || word == "1"sv || word == "yes"sv;
}

/// The finite number a text begins with.
std::optional<double> leadingNumber(std::string_view text) noexcept
{
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1u);
    if (text.empty())
        return std::nullopt;
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || !std::isfinite(value))
        return std::nullopt;
    return value;
}

/// Numbers taken in order from the comma-separated fields that begin with one; a position with no such field
/// keeps the value it had.
template <std::size_t Count>
std::array<double, Count> numbersIn(std::string_view text, std::array<double, Count> values) noexcept
{
    std::size_t index = 0u;
    std::size_t start = 0u;
    for (std::size_t end = 0u; end <= text.size() && index < Count; ++end)
    {
        if (end < text.size() && text[end] != ',')
            continue;
        if (const std::optional<double> number = leadingNumber(text.substr(start, end - start)); number.has_value())
            values[index++] = *number;
        start = end + 1u;
    }
    return values;
}

std::optional<std::size_t> wholeNumber(std::string_view text) noexcept
{
    if (text.empty())
        return std::nullopt;
    std::size_t value = 0u;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

/// Text as a quoted FITS string, with every quote inside it doubled.
std::string quoted(std::string_view text)
{
    std::string card;
    card.reserve(text.size() + 2u);
    card.push_back('\'');
    for (const char character : text)
    {
        card.push_back(character);
        if (character == '\'')
            card.push_back('\'');
    }
    card.push_back('\'');
    return card;
}

/// Finite numbers with six decimals each, separated by commas.
template <std::size_t Count> std::string sixDecimalList(const std::array<double, Count>& values)
{
    // The widest finite double in fixed notation is 309 digits, a sign, a point and six decimals.
    std::array<char, 400u> buffer{};
    std::string text;
    for (std::size_t i = 0u; i < Count; ++i)
    {
        if (i > 0u)
            text.push_back(',');
        const std::to_chars_result result =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), values[i], std::chars_format::fixed, 6);
        text.append(buffer.data(), result.ptr);
    }
    return text;
}

template <std::size_t Count> bool allFinite(const std::array<double, Count>& values) noexcept
{
    return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
}

Status invalidArgument(const char* operation, const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status checkState(const ProcessingStateRecord& record, const char* operation)
{
    if (!isRecordedLinearity(record.linearity))
        return invalidArgument(operation, "the recorded linearity is outside its enumeration", "linearity");
    if (!isRecordedChannelBasis(record.channelBasis))
        return invalidArgument(operation, "the recorded channel basis is outside its enumeration", "channelBasis");
    if (!isRecordedColorCalibration(record.colorCalibration))
        return invalidArgument(operation, "the recorded colour calibration is outside its enumeration",
                               "colorCalibration");
    if (!isStateAssignmentSource(record.assignmentSource))
        return invalidArgument(operation, "the assignment source is outside its enumeration", "assignmentSource");
    if (record.rawWhiteBalance.has_value() && !isRecordedWhiteBalance(record.rawWhiteBalance->mode))
        return invalidArgument(operation, "the recorded white balance is outside its enumeration", "rawWhiteBalance");
    if (record.rawWhiteBalanceMultipliers.has_value() && !allFinite(*record.rawWhiteBalanceMultipliers))
        return invalidArgument(operation, "a white balance multiplier is not finite", "rawWhiteBalanceMultipliers");
    return Status::success();
}

Status checkCalibration(const ColorCalibrationRecord& record, const char* operation)
{
    if (!isRecordedColorCalibration(record.calibration))
        return invalidArgument(operation, "the colour calibration is outside its enumeration", "calibration");
    if (!isRecordedLinearity(record.inputLinearity))
        return invalidArgument(operation, "the input linearity is outside its enumeration", "inputLinearity");
    if (!isRecordedChannelBasis(record.inputChannelBasis))
        return invalidArgument(operation, "the input channel basis is outside its enumeration", "inputChannelBasis");
    if (!allFinite(record.gains))
        return invalidArgument(operation, "a colour calibration gain is not finite", "gains");
    if (!allFinite(record.offsets))
        return invalidArgument(operation, "a colour calibration offset is not finite", "offsets");
    return Status::success();
}

void writeText(FitsCardEdit& cards, std::string_view keyword, const std::string& text)
{
    if (text.empty())
        cards.remove(keyword);
    else
        cards.write(keyword, quoted(text));
}

void writeState(FitsCardEdit& cards, const ProcessingStateRecord& record)
{
    cards.write(kStateVersionKeyword, "1");
    cards.write(kLinearityKeyword, quoted(stateCardText(record.linearity)));
    cards.write(kChannelBasisKeyword, quoted(stateCardText(record.channelBasis)));
    cards.write(kAssignmentSourceKeyword, quoted(stateCardText(record.assignmentSource)));
    cards.write(kCalibrationKeyword, quoted(stateCardText(record.colorCalibration)));
    if (record.rawWhiteBalance.has_value())
    {
        cards.write(kWhiteBalanceModeKeyword, quoted(stateCardText(record.rawWhiteBalance->mode)));
        cards.write(kWhiteBalanceAppliedKeyword, record.rawWhiteBalance->applied ? "T" : "F");
    }
    else
    {
        cards.remove(kWhiteBalanceModeKeyword);
        cards.remove(kWhiteBalanceAppliedKeyword);
    }
    if (record.rawWhiteBalanceMultipliers.has_value())
        cards.write(kWhiteBalanceMultipliersKeyword, quoted(sixDecimalList(*record.rawWhiteBalanceMultipliers)));
    else
        cards.remove(kWhiteBalanceMultipliersKeyword);
    writeText(cards, kCameraMatrixKeyword, record.cameraMatrixProvenance);
    writeText(cards, kOperationKeyword, record.operation);
    writeText(cards, kAttestedPathKeyword, record.attestation.sourcePath);
    writeText(cards, kAttestedHashKeyword, record.attestation.sourceHash);
    writeText(cards, kAttestedByKeyword, record.attestation.attestedBy);
    writeText(cards, kAttestedTimeKeyword, record.attestation.timestampUtc);
    writeText(cards, kAttestedToolKeyword, record.attestation.toolVersion);
    writeText(cards, kAttestedWarningsKeyword, record.attestation.warnings);
}

void writeCalibration(FitsCardEdit& cards, const ColorCalibrationRecord& record)
{
    cards.write(kCalibrationKeyword, quoted(stateCardText(record.calibration)));
    cards.write(kGainsKeyword, quoted(sixDecimalList(record.gains)));
    cards.write(kOffsetsKeyword, quoted(sixDecimalList(record.offsets)));
    writeText(cards, kResponseProfileKeyword, record.responseProfileId);
    writeText(cards, kResponseVersionKeyword, record.responseProfileVersion);
    writeText(cards, kWhiteReferenceKeyword, record.whiteReferenceId);
    writeText(cards, kWhiteVersionKeyword, record.whiteReferenceVersion);
    writeText(cards, kCatalogFamilyKeyword, record.catalogFamilyId);
    writeText(cards, kCatalogManifestKeyword, record.catalogManifestId);
    writeText(cards, kCatalogAccessKeyword, record.catalogAccessProfileId);
    writeText(cards, kAlgorithmVersionKeyword, record.algorithmVersion);
    cards.write(kInputLinearityKeyword, quoted(stateCardText(record.inputLinearity)));
    cards.write(kInputBasisKeyword, quoted(stateCardText(record.inputChannelBasis)));
}

bool chunkKeyword(std::string_view keyword) noexcept
{
    if (keyword.size() != kPayloadChunkPrefix.size() + 4u || !keyword.starts_with(kPayloadChunkPrefix))
        return false;
    const std::string_view digits = keyword.substr(kPayloadChunkPrefix.size());
    return std::all_of(digits.begin(), digits.end(), [](char digit) { return digit >= '0' && digit <= '9'; });
}

bool payloadKeyword(std::string_view keyword) noexcept
{
    return keyword == kPayloadVersionKeyword || keyword == kPayloadChunkCountKeyword ||
           keyword == kPayloadLengthKeyword || keyword == kPayloadChecksumKeyword || chunkKeyword(keyword);
}

bool familyKeyword(std::string_view keyword) noexcept
{
    return std::find(kFamilyKeywords.begin(), kFamilyKeywords.end(), keyword) != kFamilyKeywords.end() ||
           chunkKeyword(keyword);
}

std::string chunkKeywordOf(std::size_t index)
{
    const std::string digits = std::to_string(index);
    std::string keyword(kPayloadChunkPrefix);
    keyword.append(4u - (std::min)(digits.size(), std::size_t{4u}), '0');
    keyword.append(digits);
    return keyword;
}

constexpr std::string_view kHexDigits = "0123456789abcdef"sv;

/// The 64-bit FNV-1a checksum of a text, as sixteen lower-case hexadecimal digits.
std::string checksumText(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char character : text)
    {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ull;
    }
    std::string digits(16u, '0');
    for (std::size_t i = digits.size(); i > 0u; --i)
    {
        digits[i - 1u] = kHexDigits[hash & 0x0fu];
        hash >>= 4u;
    }
    return digits;
}

std::string hexText(std::string_view text)
{
    std::string hex;
    hex.reserve(text.size() * 2u);
    for (const char character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        hex.push_back(kHexDigits[byte >> 4u]);
        hex.push_back(kHexDigits[byte & 0x0fu]);
    }
    return hex;
}

int hexValue(char digit) noexcept
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    return -1;
}

std::optional<std::string> hexDecoded(std::string_view hex)
{
    if (hex.size() % 2u != 0u)
        return std::nullopt;
    std::string bytes;
    bytes.reserve(hex.size() / 2u);
    for (std::size_t i = 0u; i < hex.size(); i += 2u)
    {
        const int high = hexValue(hex[i]);
        const int low = hexValue(hex[i + 1u]);
        if (high < 0 || low < 0)
            return std::nullopt;
        bytes.push_back(static_cast<char>((high << 4) | low));
    }
    return bytes;
}

void writePayload(FitsCardEdit& cards, std::string_view document, const char* operation)
{
    const std::string hex = hexText(document);
    const std::size_t chunks = (hex.size() + kPayloadChunkDigits - 1u) / kPayloadChunkDigits;
    if (chunks > kMaxPayloadChunks)
    {
        cards.fail(
            Status::failure(ErrorCode::LimitExceeded,
                            {.message = "the calibration document needs more chunk cards than its transport numbers",
                             .operation = operation,
                             .domain = ErrorDomain::Io,
                             .context = {StatusContextField::ofText(ContextKey::Parameter, "document"),
                                         StatusContextField::ofCount(ContextKey::Requested, chunks),
                                         StatusContextField::ofCount(ContextKey::Limit, kMaxPayloadChunks)}}));
        return;
    }
    cards.removeWhere(payloadKeyword);
    cards.write(kPayloadVersionKeyword, "1");
    cards.write(kPayloadChunkCountKeyword, std::to_string(chunks));
    cards.write(kPayloadLengthKeyword, std::to_string(document.size()));
    cards.write(kPayloadChecksumKeyword, quoted(checksumText(document)));
    for (std::size_t chunk = 0u; chunk < chunks; ++chunk)
        cards.write(chunkKeywordOf(chunk),
                    quoted(std::string_view(hex).substr(chunk * kPayloadChunkDigits, kPayloadChunkDigits)));
}

/// A calibration document read out of its cards, with the fields it verified to.
struct VerifiedPayload
{
    CalibrationPayloadReading reading{};
    std::optional<CalibrationDocument> document{};
};

VerifiedPayload verifiedPayload(const CodecMetadata& codec)
{
    VerifiedPayload verified;
    CalibrationPayloadStatus& status = verified.reading.status;
    status.present = std::any_of(codec.cards().begin(), codec.cards().end(), [](const CodecCard& card) {
        return card.origin() == CodecNamespace::Fits && payloadKeyword(card.keyword());
    });
    if (!status.present)
        return verified;
    const auto refuse = [&status](CalibrationPayloadProblem problem) { status.problem = problem; };

    if (readCard(codec, kPayloadVersionKeyword) != "1"sv)
    {
        refuse(CalibrationPayloadProblem::UnsupportedVersion);
        return verified;
    }
    const std::optional<std::size_t> chunks = wholeNumber(readCard(codec, kPayloadChunkCountKeyword));
    if (!chunks.has_value() || *chunks == 0u || *chunks > kMaxPayloadChunks)
    {
        refuse(CalibrationPayloadProblem::InvalidChunkCount);
        return verified;
    }
    const std::optional<std::size_t> length = wholeNumber(readCard(codec, kPayloadLengthKeyword));
    if (!length.has_value())
    {
        refuse(CalibrationPayloadProblem::InvalidLength);
        return verified;
    }
    const std::string checksum = spelling(readCard(codec, kPayloadChecksumKeyword));
    if (checksum.empty())
    {
        refuse(CalibrationPayloadProblem::MissingChecksum);
        return verified;
    }
    std::string hex;
    hex.reserve(*chunks * kPayloadChunkDigits);
    for (std::size_t chunk = 0u; chunk < *chunks; ++chunk)
    {
        const std::string part = readCard(codec, chunkKeywordOf(chunk));
        if (part.empty())
        {
            refuse(CalibrationPayloadProblem::MissingChunk);
            status.missingChunk = static_cast<std::uint32_t>(chunk);
            return verified;
        }
        hex += part;
    }
    std::optional<std::string> bytes = hexDecoded(hex);
    if (!bytes.has_value())
    {
        refuse(CalibrationPayloadProblem::InvalidHex);
        return verified;
    }
    if (bytes->size() != *length)
    {
        refuse(CalibrationPayloadProblem::LengthMismatch);
        return verified;
    }
    if (checksumText(*bytes) != checksum)
    {
        refuse(CalibrationPayloadProblem::ChecksumMismatch);
        return verified;
    }
    Result<CalibrationDocument> parsed = parseCalibrationDocument(*bytes);
    if (!parsed.hasValue())
    {
        refuse(parsed.status().code() == ErrorCode::Unsupported ? CalibrationPayloadProblem::UnsupportedSchema
                                                                : CalibrationPayloadProblem::InvalidDocument);
        return verified;
    }
    status.valid = true;
    status.problem = CalibrationPayloadProblem::None;
    verified.reading.document = std::move(*bytes);
    verified.document = std::move(*parsed);
    return verified;
}

ColorCalibrationRecord recordOf(const CalibrationDocument& document)
{
    ColorCalibrationRecord record;
    record.calibration = calibrationOf(document.calibrationState);
    record.gains = document.gains;
    record.offsets = document.offsets;
    record.responseProfileId = document.responseProfileId;
    record.responseProfileVersion = document.responseProfileVersion;
    record.whiteReferenceId = document.whiteReferenceId;
    record.whiteReferenceVersion = document.whiteReferenceVersion;
    record.catalogFamilyId = document.catalogFamilyId;
    record.catalogManifestId = document.catalogManifestId;
    record.catalogAccessProfileId = document.catalogAccessProfileId;
    record.algorithmVersion = document.algorithmVersion;
    record.inputLinearity = linearityOf(document.inputLinearity);
    record.inputChannelBasis = basisOf(document.inputChannelBasis);
    return record;
}

CalibrationDocument documentOf(const ColorCalibrationRecord& record)
{
    CalibrationDocument document;
    document.calibrationState = stateCardText(record.calibration);
    document.gains = record.gains;
    document.offsets = record.offsets;
    document.responseProfileId = record.responseProfileId;
    document.responseProfileVersion = record.responseProfileVersion;
    document.whiteReferenceId = record.whiteReferenceId;
    document.whiteReferenceVersion = record.whiteReferenceVersion;
    document.catalogFamilyId = record.catalogFamilyId;
    document.catalogManifestId = record.catalogManifestId;
    document.catalogAccessProfileId = record.catalogAccessProfileId;
    document.algorithmVersion = record.algorithmVersion;
    document.inputLinearity = stateCardText(record.inputLinearity);
    document.inputChannelBasis = stateCardText(record.inputChannelBasis);
    return document;
}

ColorCalibrationRecord calibrationFromCards(const CodecMetadata& codec)
{
    ColorCalibrationRecord record;
    record.calibration = calibrationOf(readCard(codec, kCalibrationKeyword));
    record.gains = numbersIn(readCard(codec, kGainsKeyword), record.gains);
    record.offsets = numbersIn(readCard(codec, kOffsetsKeyword), record.offsets);
    record.responseProfileId = readCard(codec, kResponseProfileKeyword);
    record.responseProfileVersion = readCard(codec, kResponseVersionKeyword);
    record.whiteReferenceId = readCard(codec, kWhiteReferenceKeyword);
    record.whiteReferenceVersion = readCard(codec, kWhiteVersionKeyword);
    record.catalogFamilyId = readCard(codec, kCatalogFamilyKeyword);
    record.catalogManifestId = readCard(codec, kCatalogManifestKeyword);
    record.catalogAccessProfileId = readCard(codec, kCatalogAccessKeyword);
    record.algorithmVersion = readCard(codec, kAlgorithmVersionKeyword);
    record.inputLinearity = linearityOf(readCard(codec, kInputLinearityKeyword));
    record.inputChannelBasis = basisOf(readCard(codec, kInputBasisKeyword));
    return record;
}

bool monochrome(const ImageDescriptor& descriptor) noexcept
{
    return descriptor.channelSchema() == ChannelSchema::Mono || descriptor.extent().channelCount() == 1u;
}

bool redGreenBlue(const ImageDescriptor& descriptor) noexcept
{
    return descriptor.channelSchema() == ChannelSchema::Rgb;
}

bool usableGains(const double gains[4]) noexcept
{
    for (std::size_t i = 0u; i < 3u; ++i)
        if (!std::isfinite(gains[i]) || gains[i] <= 0.0)
            return false;
    return true;
}
} // namespace

const char* stateCardText(RecordedLinearity linearity) noexcept
{
    switch (linearity)
    {
    case RecordedLinearity::Linear:
        return "linear";
    case RecordedLinearity::Nonlinear:
        return "nonlinear";
    case RecordedLinearity::Unknown:
        break;
    }
    return "unknown";
}

const char* stateCardText(RecordedChannelBasis basis) noexcept
{
    switch (basis)
    {
    case RecordedChannelBasis::RawCfa:
        return "raw-cfa";
    case RecordedChannelBasis::LinearRgb:
        return "linear-rgb";
    case RecordedChannelBasis::CameraRgb:
        return "camera-rgb";
    case RecordedChannelBasis::DisplayRgb:
        return "display-rgb";
    case RecordedChannelBasis::Monochrome:
        return "monochrome";
    case RecordedChannelBasis::Unknown:
        break;
    }
    return "unknown";
}

const char* stateCardText(RecordedColorCalibration calibration) noexcept
{
    switch (calibration)
    {
    case RecordedColorCalibration::None:
        return "none";
    case RecordedColorCalibration::Photometric:
        return "pcc";
    case RecordedColorCalibration::Spectrophotometric:
        return "spcc";
    case RecordedColorCalibration::Manual:
        return "manual";
    case RecordedColorCalibration::Unknown:
        break;
    }
    return "unknown";
}

const char* stateCardText(StateAssignmentSource source) noexcept
{
    switch (source)
    {
    case StateAssignmentSource::Loader:
        return "loader";
    case StateAssignmentSource::Process:
        return "process";
    case StateAssignmentSource::UserAttestation:
        return "user-attestation";
    case StateAssignmentSource::None:
        break;
    }
    return "none";
}

const char* stateCardText(RecordedWhiteBalance balance) noexcept
{
    switch (balance)
    {
    case RecordedWhiteBalance::None:
        return "none";
    case RecordedWhiteBalance::CameraAsShot:
        return "camera-as-shot";
    case RecordedWhiteBalance::Automatic:
        return "auto";
    case RecordedWhiteBalance::Custom:
        return "custom";
    case RecordedWhiteBalance::Daylight:
        break;
    }
    return "daylight";
}

const char* calibrationPayloadProblemName(CalibrationPayloadProblem problem) noexcept
{
    switch (problem)
    {
    case CalibrationPayloadProblem::None:
        return "None";
    case CalibrationPayloadProblem::Absent:
        return "Absent";
    case CalibrationPayloadProblem::UnsupportedVersion:
        return "UnsupportedVersion";
    case CalibrationPayloadProblem::InvalidChunkCount:
        return "InvalidChunkCount";
    case CalibrationPayloadProblem::InvalidLength:
        return "InvalidLength";
    case CalibrationPayloadProblem::MissingChecksum:
        return "MissingChecksum";
    case CalibrationPayloadProblem::MissingChunk:
        return "MissingChunk";
    case CalibrationPayloadProblem::InvalidHex:
        return "InvalidHex";
    case CalibrationPayloadProblem::LengthMismatch:
        return "LengthMismatch";
    case CalibrationPayloadProblem::ChecksumMismatch:
        return "ChecksumMismatch";
    case CalibrationPayloadProblem::InvalidDocument:
        return "InvalidDocument";
    case CalibrationPayloadProblem::UnsupportedSchema:
        return "UnsupportedSchema";
    }
    return "UnknownCalibrationPayloadProblem";
}

const char* spectrophotometricInputProblemName(SpectrophotometricInputProblem problem) noexcept
{
    switch (problem)
    {
    case SpectrophotometricInputProblem::None:
        return "None";
    case SpectrophotometricInputProblem::UnknownLinearity:
        return "UnknownLinearity";
    case SpectrophotometricInputProblem::Nonlinear:
        return "Nonlinear";
    case SpectrophotometricInputProblem::IncompatibleChannelBasis:
        return "IncompatibleChannelBasis";
    case SpectrophotometricInputProblem::IncompleteAttestation:
        return "IncompleteAttestation";
    case SpectrophotometricInputProblem::AlreadyColorCalibrated:
        return "AlreadyColorCalibrated";
    }
    return "UnknownSpectrophotometricInputProblem";
}

ProcessingStateRecord readProcessingStateCards(const CodecMetadata& codec)
{
    ProcessingStateRecord record;
    record.linearity = linearityOf(readCard(codec, kLinearityKeyword));
    record.channelBasis = basisOf(readCard(codec, kChannelBasisKeyword));
    record.colorCalibration = calibrationOf(readCard(codec, kCalibrationKeyword));
    record.assignmentSource = sourceOf(readCard(codec, kAssignmentSourceKeyword));
    if (const std::string mode = readCard(codec, kWhiteBalanceModeKeyword); !mode.empty())
        record.rawWhiteBalance = RecordedRawWhiteBalance{
            .mode = whiteBalanceOf(mode), .applied = statesTrue(readCard(codec, kWhiteBalanceAppliedKeyword))};
    if (const std::string multipliers = readCard(codec, kWhiteBalanceMultipliersKeyword); !multipliers.empty())
        record.rawWhiteBalanceMultipliers = numbersIn(multipliers, std::array<double, 4u>{1.0, 1.0, 1.0, 1.0});
    record.cameraMatrixProvenance = readCard(codec, kCameraMatrixKeyword);
    record.operation = readCard(codec, kOperationKeyword);
    record.attestation.sourcePath = readCard(codec, kAttestedPathKeyword);
    record.attestation.sourceHash = readCard(codec, kAttestedHashKeyword);
    record.attestation.attestedBy = readCard(codec, kAttestedByKeyword);
    record.attestation.timestampUtc = readCard(codec, kAttestedTimeKeyword);
    record.attestation.toolVersion = readCard(codec, kAttestedToolKeyword);
    record.attestation.warnings = readCard(codec, kAttestedWarningsKeyword);
    return record;
}

Result<CodecMetadata> withProcessingStateCards(const CodecMetadata& codec, const ProcessingStateRecord& record)
{
    if (Status checked = checkState(record, kWriteStateOperation); checked.failed())
        return checked;
    FitsCardEdit cards(codec, kWriteStateOperation);
    writeState(cards, record);
    return std::move(cards).finish();
}

Result<CodecMetadata> withoutProcessingStateCards(const CodecMetadata& codec)
{
    FitsCardEdit cards(codec, kClearOperation);
    cards.removeWhere(familyKeyword);
    return std::move(cards).finish();
}

RecordedChannelBasis linearChannelBasisOf(const ImageDescriptor& descriptor, RecordedChannelBasis recorded) noexcept
{
    if (descriptor.cfa().isMosaiced())
        return RecordedChannelBasis::RawCfa;
    if (monochrome(descriptor))
        return RecordedChannelBasis::Monochrome;
    if (redGreenBlue(descriptor))
        return recorded == RecordedChannelBasis::CameraRgb || recorded == RecordedChannelBasis::LinearRgb
                   ? recorded
                   : RecordedChannelBasis::LinearRgb;
    return RecordedChannelBasis::Unknown;
}

RecordedChannelBasis nonlinearChannelBasisOf(const ImageDescriptor& descriptor, RecordedChannelBasis recorded) noexcept
{
    if (recorded == RecordedChannelBasis::LinearRgb || recorded == RecordedChannelBasis::CameraRgb ||
        recorded == RecordedChannelBasis::DisplayRgb)
        return RecordedChannelBasis::DisplayRgb;
    if (recorded == RecordedChannelBasis::Monochrome)
        return RecordedChannelBasis::Monochrome;
    if (redGreenBlue(descriptor))
        return RecordedChannelBasis::DisplayRgb;
    if (monochrome(descriptor))
        return RecordedChannelBasis::Monochrome;
    return recorded;
}

ProcessingStateRecord linearState(const ProcessingStateRecord& record, RecordedChannelBasis basis,
                                  std::string_view operation)
{
    ProcessingStateRecord state = record;
    state.linearity = RecordedLinearity::Linear;
    state.channelBasis = basis;
    state.assignmentSource = StateAssignmentSource::Process;
    state.operation = std::string(operation);
    return state;
}

ProcessingStateRecord nonlinearState(const ProcessingStateRecord& record, const ImageDescriptor& descriptor,
                                     std::string_view operation)
{
    ProcessingStateRecord state = record;
    state.linearity = RecordedLinearity::Nonlinear;
    state.channelBasis = nonlinearChannelBasisOf(descriptor, record.channelBasis);
    state.assignmentSource = StateAssignmentSource::Process;
    state.operation = std::string(operation);
    return state;
}

ProcessingStateRecord attestedState(const ProcessingStateRecord& record, RecordedLinearity linearity,
                                    RecordedChannelBasis basis, const StateAttestation& attestation)
{
    ProcessingStateRecord state = record;
    state.linearity = linearity;
    state.channelBasis = basis;
    state.assignmentSource = StateAssignmentSource::UserAttestation;
    state.operation.clear();
    state.attestation = attestation;
    return state;
}

ProcessingStateRecord loaderState(const ProcessingStateRecord& record, const ImageDescriptor& descriptor,
                                  std::string_view formatName, const RawCameraMetadata* camera,
                                  RecordedWhiteBalance applied)
{
    ProcessingStateRecord state = record;
    state.linearity = RecordedLinearity::Linear;
    state.assignmentSource = StateAssignmentSource::Loader;
    state.operation = std::string(formatName);
    if (descriptor.cfa().isMosaiced())
        state.channelBasis = RecordedChannelBasis::RawCfa;
    else if (redGreenBlue(descriptor))
        state.channelBasis = RecordedChannelBasis::LinearRgb;
    else if (monochrome(descriptor))
        state.channelBasis = RecordedChannelBasis::Monochrome;
    else
        state.channelBasis = RecordedChannelBasis::Unknown;
    if (camera == nullptr)
        return state;

    state.rawWhiteBalance = RecordedRawWhiteBalance{.mode = applied, .applied = applied != RecordedWhiteBalance::None};
    const double* const gains = usableGains(camera->daylightMultipliers) ? camera->daylightMultipliers
                                : usableGains(camera->cameraMultipliers) ? camera->cameraMultipliers
                                                                         : nullptr;
    if (gains != nullptr)
        state.rawWhiteBalanceMultipliers = std::array<double, 4u>{gains[0], gains[1], gains[2], gains[3]};
    for (const auto& row : camera->cameraToRgb)
        for (const double entry : row)
            if (entry != 0.0)
                state.cameraMatrixProvenance = "libraw:rgb_cam";
    return state;
}

CalibrationPayloadReading readColorCalibrationPayload(const CodecMetadata& codec)
{
    return verifiedPayload(codec).reading;
}

ColorCalibrationReading readColorCalibrationCards(const CodecMetadata& codec)
{
    VerifiedPayload verified = verifiedPayload(codec);
    ColorCalibrationReading reading;
    reading.status = verified.reading.status;
    if (verified.document.has_value())
    {
        reading.record = recordOf(*verified.document);
        reading.status.usedPayload = true;
        return reading;
    }
    reading.status.fellBackToCards = reading.status.present;
    reading.record = calibrationFromCards(codec);
    return reading;
}

Result<CodecMetadata> withColorCalibrationCards(const CodecMetadata& codec, const ColorCalibrationRecord& record)
{
    if (Status checked = checkCalibration(record, kWriteCalibrationOperation); checked.failed())
        return checked;
    ProcessingStateRecord state = readProcessingStateCards(codec);
    ColorCalibrationRecord written = record;
    if (written.inputLinearity == RecordedLinearity::Unknown)
        written.inputLinearity = state.linearity;
    if (written.inputChannelBasis == RecordedChannelBasis::Unknown)
        written.inputChannelBasis = state.channelBasis;
    state.colorCalibration = record.calibration;
    Result<std::string> document = writeCalibrationDocument(documentOf(written));
    if (!document.hasValue())
        return document.status();

    FitsCardEdit cards(codec, kWriteCalibrationOperation);
    writeState(cards, state);
    writeCalibration(cards, written);
    writePayload(cards, *document, kWriteCalibrationOperation);
    return std::move(cards).finish();
}

Result<CodecMetadata> withColorCalibrationPayload(const CodecMetadata& codec, std::string_view document)
{
    Result<CalibrationDocument> parsed = parseCalibrationDocument(document);
    if (!parsed.hasValue())
    {
        const ErrorCode code = parsed.status().code();
        if (code != ErrorCode::InvalidData && code != ErrorCode::Unsupported)
            return parsed.status();
        return Status::failure(ErrorCode::InvalidData,
                               {.message = "the text is not a document of the calibration schema",
                                .operation = kWritePayloadOperation,
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "document")}});
    }
    const ColorCalibrationRecord record = recordOf(*parsed);
    ProcessingStateRecord state = readProcessingStateCards(codec);
    state.colorCalibration = record.calibration;

    FitsCardEdit cards(codec, kWritePayloadOperation);
    writeState(cards, state);
    writeCalibration(cards, record);
    writePayload(cards, document, kWritePayloadOperation);
    return std::move(cards).finish();
}

SpectrophotometricInputProblem spectrophotometricInputProblem(const ProcessingStateRecord& record) noexcept
{
    if (record.linearity == RecordedLinearity::Unknown)
        return SpectrophotometricInputProblem::UnknownLinearity;
    if (record.linearity == RecordedLinearity::Nonlinear)
        return SpectrophotometricInputProblem::Nonlinear;
    if (record.channelBasis != RecordedChannelBasis::LinearRgb &&
        record.channelBasis != RecordedChannelBasis::CameraRgb)
        return SpectrophotometricInputProblem::IncompatibleChannelBasis;
    if (record.assignmentSource == StateAssignmentSource::UserAttestation &&
        (record.attestation.sourcePath.empty() || record.attestation.sourceHash.empty() ||
         record.attestation.attestedBy.empty()))
        return SpectrophotometricInputProblem::IncompleteAttestation;
    if (record.colorCalibration != RecordedColorCalibration::None)
        return SpectrophotometricInputProblem::AlreadyColorCalibrated;
    return SpectrophotometricInputProblem::None;
}

} // namespace an
