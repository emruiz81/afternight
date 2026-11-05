// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - local normalization: the persisted coefficient document.
//
// A set of frames' grids and scales as lines of `key=value` tokens: one header line, one request
// line, one reference line, then one line per frame and one more for each frame that also carries
// a global match. The form is what this engine writes and this engine reads, so the parser has no
// compatibility to keep and is strict about everything - every field exactly once, every grid
// exactly its size, no number that does not read back finite - and every number is written through
// the shortest decimal that reads back as the same Float64, so a parse and a format are the
// identity.

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include "algorithms/normalization/LocalNormalizationInternal.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace an
{

namespace
{

using local_normalization_detail::failure;
using local_normalization_detail::parameterFailure;

constexpr const char* kFormatOperation = "an::formatLocalNormalizationDocument";
constexpr const char* kParseOperation = "an::parseLocalNormalizationDocument";

/// A parse failure naming the line at fault.
[[nodiscard]] Status lineFailure(const char* message, std::uint64_t line)
{
    return Status::failure(ErrorCode::InvalidData,
                           StatusDetail{.message = message,
                                        .operation = kParseOperation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofCount(ContextKey::Completed, line)}});
}

/// Appends a Float64 in its shortest round-tripping decimal form.
void appendNumber(std::string& out, double value)
{
    std::array<char, 32> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

/// Appends an unsigned count.
void appendCount(std::string& out, std::uint64_t value)
{
    std::array<char, 24> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

/// Appends a comma-separated run of numbers.
void appendNumbers(std::string& out, std::span<const double> values)
{
    for (std::size_t index = 0u; index < values.size(); ++index)
    {
        if (index != 0u)
        {
            out.push_back(',');
        }
        appendNumber(out, values[index]);
    }
}

/// Reads one Float64 from a whole token.
[[nodiscard]] bool readNumber(std::string_view token, double& out) noexcept
{
    const std::from_chars_result result = std::from_chars(token.data(), token.data() + token.size(), out);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size() && std::isfinite(out);
}

/// Reads one count from a whole token.
[[nodiscard]] bool readCount(std::string_view token, std::uint64_t& out) noexcept
{
    if (token.empty() || token.front() == '-' || token.front() == '+')
    {
        return false;
    }
    const std::from_chars_result result = std::from_chars(token.data(), token.data() + token.size(), out);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

/// Reads a comma-separated run of numbers into a vector that must end up exactly `expected` long.
[[nodiscard]] bool readNumbers(std::string_view token, std::uint64_t expected, std::vector<double>& out)
{
    out.clear();
    out.reserve(static_cast<std::size_t>(expected));
    std::size_t position = 0u;
    while (position <= token.size())
    {
        const std::size_t comma = token.find(',', position);
        const std::string_view piece =
            token.substr(position, comma == std::string_view::npos ? std::string_view::npos : comma - position);
        double value = 0.0;
        if (!readNumber(piece, value))
        {
            return false;
        }
        out.push_back(value);
        if (out.size() > expected)
        {
            return false;
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        position = comma + 1u;
    }
    return out.size() == expected;
}

/// The enumerator a name spells, by searching the names the engine writes.
template <typename EnumT, typename NameT>
[[nodiscard]] std::optional<EnumT> enumeratorOf(std::string_view name, std::uint8_t count, NameT nameOf) noexcept
{
    for (std::uint8_t value = 0u; value < count; ++value)
    {
        if (name == nameOf(static_cast<EnumT>(value)))
        {
            return static_cast<EnumT>(value);
        }
    }
    return std::nullopt;
}

/// One `key=value` token split.
struct Field
{
    std::string_view key;
    std::string_view value;
};

/// Splits one line into its leading record name and its fields.
[[nodiscard]] bool splitLine(std::string_view line, std::string_view& record, std::vector<Field>& fields)
{
    fields.clear();
    std::size_t position = 0u;
    bool first = true;
    while (position < line.size())
    {
        while (position < line.size() && line[position] == ' ')
        {
            ++position;
        }
        if (position >= line.size())
        {
            break;
        }
        const std::size_t end = line.find(' ', position);
        const std::string_view token =
            line.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
        position = end == std::string_view::npos ? line.size() : end;
        if (first)
        {
            record = token;
            first = false;
            continue;
        }
        const std::size_t equals = token.find('=');
        if (equals == std::string_view::npos || equals == 0u)
        {
            return false;
        }
        fields.push_back(Field{.key = token.substr(0u, equals), .value = token.substr(equals + 1u)});
    }
    return !first;
}

/// The value of a field that must appear exactly once.
[[nodiscard]] bool takeField(std::vector<Field>& fields, std::string_view key, std::string_view& out) noexcept
{
    std::size_t found = fields.size();
    for (std::size_t index = 0u; index < fields.size(); ++index)
    {
        if (fields[index].key == key)
        {
            if (found != fields.size())
            {
                return false;
            }
            found = index;
        }
    }
    if (found == fields.size())
    {
        return false;
    }
    out = fields[found].value;
    fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(found));
    return true;
}

/// The checks a document must pass whichever way it is travelling.
[[nodiscard]] Status checkDocument(const LocalNormalizationDocument& document, const char* operation)
{
    const Status shapeReady = checkLocalNormalizationGridShape(document.shape);
    if (shapeReady.failed())
    {
        return shapeReady;
    }
    const Result<LocalNormalizationPlan> plan = planLocalNormalization(
        document.request, document.shape.width, document.shape.height, document.shape.channelCount);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    if (document.request.gridSize != document.shape.gridSize)
    {
        return failure(operation, ErrorCode::IncompatibleArguments, "the request and the shape disagree in grid size");
    }
    if (!isValidityRule(document.validity))
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "the validity rule is not one this build implements", "validity");
    }
    if (document.frames.empty())
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "a document carries at least one frame",
                                "frames");
    }
    if (document.frames.size() > kMaxLocalNormalizationDocumentFrames)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{
                .message = "a document carries at most the stated number of frames",
                .operation = operation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofCount(ContextKey::Requested, document.frames.size()),
                            StatusContextField::ofCount(ContextKey::Limit, kMaxLocalNormalizationDocumentFrames)}});
    }
    const std::uint64_t valueCount = document.shape.valueCount();
    if (document.referenceBackground.size() != valueCount)
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "the reference background does not hold exactly the shape's values");
    }
    if (!local_normalization_detail::allFinite(document.referenceBackground))
    {
        return failure(operation, ErrorCode::InvalidData, "a reference background node is not finite");
    }
    bool referenceFound = !document.referenceIndex.has_value();
    std::optional<std::uint32_t> previousIndex;
    for (const LocalNormalizationFrameCoefficients& frame : document.frames)
    {
        if (previousIndex.has_value() && frame.frameIndex <= *previousIndex)
        {
            return parameterFailure(operation, ErrorCode::InvalidArgument, "frame indices are strictly increasing",
                                    "frames");
        }
        previousIndex = frame.frameIndex;
        referenceFound =
            referenceFound || (document.referenceIndex.has_value() && *document.referenceIndex == frame.frameIndex);
        if (frame.background.size() != valueCount || (!frame.scaleGrid.empty() && frame.scaleGrid.size() != valueCount))
        {
            return failure(operation, ErrorCode::IncompatibleArguments,
                           "a frame grid does not hold exactly the shape's values");
        }
        if (!local_normalization_detail::allFinite(frame.background) ||
            !local_normalization_detail::allFinite(frame.scaleGrid))
        {
            return failure(operation, ErrorCode::InvalidData, "a frame grid node is not finite");
        }
        for (const double scale : frame.scaleGrid)
        {
            if (scale <= 0.0)
            {
                return failure(operation, ErrorCode::InvalidData, "a scale grid node is not positive");
            }
        }
        for (std::uint32_t channel = 0u; channel < document.shape.channelCount; ++channel)
        {
            if (!std::isfinite(frame.scale[channel]) || frame.scale[channel] <= 0.0)
            {
                return failure(operation, ErrorCode::InvalidData, "a frame scale is not finite and positive");
            }
        }
        if (frame.global.has_value())
        {
            const FrameNormalization& global = *frame.global;
            if (!isNormalizationMethod(global.method) || !isScaleEstimator(global.estimator))
            {
                return parameterFailure(operation, ErrorCode::InvalidArgument,
                                        "a global match names a method or estimator this build does not implement",
                                        "global");
            }
            if (global.channelCount != document.shape.channelCount)
            {
                return parameterFailure(operation, ErrorCode::InvalidArgument,
                                        "a global match does not carry one coefficient pair per channel", "global");
            }
            for (std::uint32_t channel = 0u; channel < global.channelCount; ++channel)
            {
                const NormalizationChannelCoefficients& coefficients = global.channels[channel];
                if (!std::isfinite(coefficients.scale) || coefficients.scale == 0.0 ||
                    !std::isfinite(coefficients.offset))
                {
                    return failure(operation, ErrorCode::InvalidData,
                                   "a global coefficient is not finite or its scale is zero");
                }
            }
        }
    }
    if (!referenceFound)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "the reference index names no frame of the set",
                                "referenceIndex");
    }
    return Status::success();
}

} // namespace

Result<std::string> formatLocalNormalizationDocument(const LocalNormalizationDocument& document)
{
    const Status ready = checkDocument(document, kFormatOperation);
    if (ready.failed())
    {
        return ready;
    }
    std::string out;
    const LocalNormalizationRequest& request = document.request;
    const LocalNormalizationGridShape& shape = document.shape;
    out.append("localnormalization=");
    appendCount(out, kLocalNormalizationDocumentVersion);
    out.append(" grid=");
    appendCount(out, shape.gridSize);
    out.append(" extent=");
    appendCount(out, shape.width);
    out.push_back('x');
    appendCount(out, shape.height);
    out.push_back('x');
    appendCount(out, shape.channelCount);
    out.append(" interpolation=").append(localGridInterpolationName(request.interpolation));
    out.append(" validity=").append(validityRuleName(document.validity));
    out.append(" reference=");
    if (document.referenceIndex.has_value())
    {
        appendCount(out, *document.referenceIndex);
    }
    else
    {
        out.append("none");
    }
    out.append(" frames=");
    appendCount(out, document.frames.size());
    out.push_back('\n');

    out.append("request estimator=").append(localBackgroundEstimatorName(request.estimator));
    out.append(" percentile=");
    appendNumber(out, request.backgroundPercentile);
    out.append(" sigmaClip=");
    appendNumber(out, request.sigmaClip);
    out.append(" sigmaClipIterations=");
    appendCount(out, request.sigmaClipIterations);
    out.append(" minRoi=");
    appendCount(out, request.backgroundMinRoiExtent);
    out.append(" roiOverlap=");
    appendNumber(out, request.backgroundRoiOverlap);
    out.append(" backgroundSmoothing=");
    appendCount(out, request.backgroundSmoothingRadius);
    out.append(" lowClip=");
    appendNumber(out, request.lowClip);
    out.append(" highClip=");
    appendNumber(out, request.highClip);
    out.append(" scaleMode=").append(localScaleModeName(request.scaleMode));
    out.append(" scaleSamples=");
    appendCount(out, request.scaleSamples);
    out.append(" localScaleSmoothing=");
    appendCount(out, request.localScaleSmoothingRadius);
    out.push_back('\n');

    out.append("referencebackground values=");
    appendNumbers(out, document.referenceBackground);
    out.push_back('\n');

    for (const LocalNormalizationFrameCoefficients& frame : document.frames)
    {
        out.append("frame index=");
        appendCount(out, frame.frameIndex);
        out.append(" scale=");
        appendNumbers(out, std::span<const double>{frame.scale.data(), shape.channelCount});
        out.append(" background=");
        appendNumbers(out, frame.background);
        if (!frame.scaleGrid.empty())
        {
            out.append(" scalegrid=");
            appendNumbers(out, frame.scaleGrid);
        }
        out.push_back('\n');
        if (frame.global.has_value())
        {
            const FrameNormalization& global = *frame.global;
            out.append("global index=");
            appendCount(out, frame.frameIndex);
            out.append(" method=").append(normalizationMethodName(global.method));
            out.append(" estimator=").append(scaleEstimatorName(global.estimator));
            out.append(" coefficients=");
            for (std::uint32_t channel = 0u; channel < global.channelCount; ++channel)
            {
                if (channel != 0u)
                {
                    out.push_back(',');
                }
                appendNumber(out, global.channels[channel].scale);
                out.push_back(',');
                appendNumber(out, global.channels[channel].offset);
            }
            out.append(" dispersionFallbacks=");
            appendCount(out, global.dispersionFallbacks);
            out.append(" locationFallbacks=");
            appendCount(out, global.locationFallbacks);
            out.push_back('\n');
        }
        if (out.size() > kMaxLocalNormalizationDocumentBytes)
        {
            return Status::failure(
                ErrorCode::LimitExceeded,
                StatusDetail{
                    .message = "the document exceeds the stated byte ceiling",
                    .operation = kFormatOperation,
                    .domain = ErrorDomain::Algorithms,
                    .context = {StatusContextField::ofCount(ContextKey::Requested, out.size()),
                                StatusContextField::ofCount(ContextKey::Limit, kMaxLocalNormalizationDocumentBytes)}});
        }
    }
    return out;
}

Result<LocalNormalizationDocument> parseLocalNormalizationDocument(const std::string& documentText)
{
    // Viewed once: every line below is a view into this string and must never point at a substring
    // temporary.
    const std::string_view text{documentText};
    if (text.size() > kMaxLocalNormalizationDocumentBytes)
    {
        return Status::failure(ErrorCode::LimitExceeded,
                               StatusDetail{.message = "the document exceeds the stated byte ceiling",
                                            .operation = kParseOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofCount(ContextKey::Requested, text.size()),
                                                        StatusContextField::ofCount(
                                                            ContextKey::Limit, kMaxLocalNormalizationDocumentBytes)}});
    }
    LocalNormalizationDocument document;
    std::vector<Field> fields;
    std::uint64_t lineNumber = 0u;
    std::uint64_t declaredFrames = 0u;
    bool sawHeader = false;
    bool sawRequest = false;
    bool sawReference = false;
    bool lastWasFrame = false;
    std::size_t position = 0u;
    while (position < text.size())
    {
        std::size_t end = text.find('\n', position);
        std::string_view line =
            text.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
        position = end == std::string_view::npos ? text.size() : end + 1u;
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1u);
        }
        if (line.empty())
        {
            return lineFailure("an empty line is not part of the form", lineNumber);
        }
        std::string_view record;
        if (!splitLine(line, record, fields))
        {
            return lineFailure("a token is not key=value", lineNumber);
        }
        std::string_view value;
        if (!sawHeader)
        {
            if (record != "localnormalization=1" && record.rfind("localnormalization=", 0u) != 0u)
            {
                return lineFailure("the first line does not name this form", lineNumber);
            }
            std::uint64_t version = 0u;
            if (!readCount(record.substr(record.find('=') + 1u), version) ||
                version != kLocalNormalizationDocumentVersion)
            {
                return lineFailure("the document version is not the one this build reads", lineNumber);
            }
            std::uint64_t grid = 0u;
            if (!takeField(fields, "grid", value) || !readCount(value, grid))
            {
                return lineFailure("the grid field is missing or malformed", lineNumber);
            }
            if (!takeField(fields, "extent", value))
            {
                return lineFailure("the extent field is missing", lineNumber);
            }
            const std::size_t firstX = value.find('x');
            const std::size_t secondX = firstX == std::string_view::npos ? firstX : value.find('x', firstX + 1u);
            std::uint64_t width = 0u;
            std::uint64_t height = 0u;
            std::uint64_t channels = 0u;
            if (firstX == std::string_view::npos || secondX == std::string_view::npos ||
                !readCount(value.substr(0u, firstX), width) ||
                !readCount(value.substr(firstX + 1u, secondX - firstX - 1u), height) ||
                !readCount(value.substr(secondX + 1u), channels) || width > UINT32_MAX || height > UINT32_MAX ||
                channels > UINT32_MAX || grid > UINT32_MAX)
            {
                return lineFailure("the extent field is malformed", lineNumber);
            }
            document.shape = LocalNormalizationGridShape{.gridSize = static_cast<std::uint32_t>(grid),
                                                         .width = static_cast<std::uint32_t>(width),
                                                         .height = static_cast<std::uint32_t>(height),
                                                         .channelCount = static_cast<std::uint32_t>(channels)};
            document.request.gridSize = document.shape.gridSize;
            if (!takeField(fields, "interpolation", value))
            {
                return lineFailure("the interpolation field is missing", lineNumber);
            }
            const std::optional<LocalGridInterpolation> interpolation =
                enumeratorOf<LocalGridInterpolation>(value, 2u, &localGridInterpolationName);
            if (!interpolation.has_value())
            {
                return lineFailure("the interpolation names no enumerator", lineNumber);
            }
            document.request.interpolation = *interpolation;
            if (!takeField(fields, "validity", value))
            {
                return lineFailure("the validity field is missing", lineNumber);
            }
            const std::optional<ValidityRule> validity =
                enumeratorOf<ValidityRule>(value, static_cast<std::uint8_t>(kValidityRuleCount), &validityRuleName);
            if (!validity.has_value())
            {
                return lineFailure("the validity names no rule", lineNumber);
            }
            document.validity = *validity;
            if (!takeField(fields, "reference", value))
            {
                return lineFailure("the reference field is missing", lineNumber);
            }
            if (value != "none")
            {
                std::uint64_t reference = 0u;
                if (!readCount(value, reference) || reference > UINT32_MAX)
                {
                    return lineFailure("the reference field is malformed", lineNumber);
                }
                document.referenceIndex = static_cast<std::uint32_t>(reference);
            }
            if (!takeField(fields, "frames", value) || !readCount(value, declaredFrames) || declaredFrames == 0u ||
                declaredFrames > kMaxLocalNormalizationDocumentFrames)
            {
                return lineFailure("the frames field is missing or outside its range", lineNumber);
            }
            if (!fields.empty())
            {
                return lineFailure("the header carries a field this form does not know", lineNumber);
            }
            const Status shapeReady = checkLocalNormalizationGridShape(document.shape);
            if (shapeReady.failed())
            {
                return lineFailure("the header describes a grid this build cannot hold", lineNumber);
            }
            document.frames.reserve(static_cast<std::size_t>(declaredFrames));
            sawHeader = true;
            continue;
        }
        if (record == "request")
        {
            if (sawRequest)
            {
                return lineFailure("the request line appears twice", lineNumber);
            }
            LocalNormalizationRequest& request = document.request;
            std::uint64_t count = 0u;
            if (!takeField(fields, "estimator", value))
            {
                return lineFailure("the estimator field is missing", lineNumber);
            }
            const std::optional<LocalBackgroundEstimator> estimator =
                enumeratorOf<LocalBackgroundEstimator>(value, 3u, &localBackgroundEstimatorName);
            if (!estimator.has_value())
            {
                return lineFailure("the estimator names no enumerator", lineNumber);
            }
            request.estimator = *estimator;
            if (!takeField(fields, "percentile", value) || !readNumber(value, request.backgroundPercentile) ||
                !takeField(fields, "sigmaClip", value) || !readNumber(value, request.sigmaClip) ||
                !takeField(fields, "sigmaClipIterations", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("a request number is missing or malformed", lineNumber);
            }
            request.sigmaClipIterations = static_cast<std::uint32_t>(count);
            if (!takeField(fields, "minRoi", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("the minRoi field is missing or malformed", lineNumber);
            }
            request.backgroundMinRoiExtent = static_cast<std::uint32_t>(count);
            if (!takeField(fields, "roiOverlap", value) || !readNumber(value, request.backgroundRoiOverlap) ||
                !takeField(fields, "backgroundSmoothing", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("a request field is missing or malformed", lineNumber);
            }
            request.backgroundSmoothingRadius = static_cast<std::uint32_t>(count);
            if (!takeField(fields, "lowClip", value) || !readNumber(value, request.lowClip) ||
                !takeField(fields, "highClip", value) || !readNumber(value, request.highClip))
            {
                return lineFailure("the clip window is missing or malformed", lineNumber);
            }
            if (!takeField(fields, "scaleMode", value))
            {
                return lineFailure("the scaleMode field is missing", lineNumber);
            }
            const std::optional<LocalScaleMode> mode = enumeratorOf<LocalScaleMode>(value, 2u, &localScaleModeName);
            if (!mode.has_value())
            {
                return lineFailure("the scale mode names no enumerator", lineNumber);
            }
            request.scaleMode = *mode;
            if (!takeField(fields, "scaleSamples", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("the scaleSamples field is missing or malformed", lineNumber);
            }
            request.scaleSamples = static_cast<std::uint32_t>(count);
            if (!takeField(fields, "localScaleSmoothing", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("the localScaleSmoothing field is missing or malformed", lineNumber);
            }
            request.localScaleSmoothingRadius = static_cast<std::uint32_t>(count);
            if (!fields.empty())
            {
                return lineFailure("the request carries a field this form does not know", lineNumber);
            }
            sawRequest = true;
            continue;
        }
        if (record == "referencebackground")
        {
            if (sawReference)
            {
                return lineFailure("the reference background appears twice", lineNumber);
            }
            if (!takeField(fields, "values", value) ||
                !readNumbers(value, document.shape.valueCount(), document.referenceBackground))
            {
                return lineFailure("the reference background is missing or not the grid's size", lineNumber);
            }
            if (!fields.empty())
            {
                return lineFailure("the reference background carries a field this form does not know", lineNumber);
            }
            sawReference = true;
            continue;
        }
        if (record == "frame")
        {
            if (!sawRequest || !sawReference)
            {
                return lineFailure("a frame appears before the request and reference lines", lineNumber);
            }
            LocalNormalizationFrameCoefficients frame;
            std::uint64_t index = 0u;
            if (!takeField(fields, "index", value) || !readCount(value, index) || index > UINT32_MAX)
            {
                return lineFailure("the frame index is missing or malformed", lineNumber);
            }
            frame.frameIndex = static_cast<std::uint32_t>(index);
            std::vector<double> scale;
            if (!takeField(fields, "scale", value) || !readNumbers(value, document.shape.channelCount, scale))
            {
                return lineFailure("the frame scale is missing or not one value per channel", lineNumber);
            }
            for (std::uint32_t channel = 0u; channel < document.shape.channelCount; ++channel)
            {
                frame.scale[channel] = scale[channel];
            }
            if (!takeField(fields, "background", value) ||
                !readNumbers(value, document.shape.valueCount(), frame.background))
            {
                return lineFailure("the frame background is missing or not the grid's size", lineNumber);
            }
            if (takeField(fields, "scalegrid", value) &&
                !readNumbers(value, document.shape.valueCount(), frame.scaleGrid))
            {
                return lineFailure("the frame scale grid is not the grid's size", lineNumber);
            }
            if (!fields.empty())
            {
                return lineFailure("the frame carries a field this form does not know", lineNumber);
            }
            if (document.frames.size() >= declaredFrames)
            {
                return lineFailure("more frames than the header declared", lineNumber);
            }
            document.frames.push_back(std::move(frame));
            lastWasFrame = true;
            continue;
        }
        if (record == "global")
        {
            if (!lastWasFrame)
            {
                return lineFailure("a global match does not follow its frame", lineNumber);
            }
            LocalNormalizationFrameCoefficients& frame = document.frames.back();
            std::uint64_t index = 0u;
            if (!takeField(fields, "index", value) || !readCount(value, index) || index != frame.frameIndex)
            {
                return lineFailure("the global match names a frame other than the one it follows", lineNumber);
            }
            FrameNormalization global;
            if (!takeField(fields, "method", value))
            {
                return lineFailure("the global method is missing", lineNumber);
            }
            const std::optional<NormalizationMethod> method =
                enumeratorOf<NormalizationMethod>(value, 4u, &normalizationMethodName);
            if (!takeField(fields, "estimator", value))
            {
                return lineFailure("the global estimator is missing", lineNumber);
            }
            const std::optional<ScaleEstimator> estimator =
                enumeratorOf<ScaleEstimator>(value, 2u, &scaleEstimatorName);
            if (!method.has_value() || !estimator.has_value())
            {
                return lineFailure("the global method or estimator names no enumerator", lineNumber);
            }
            global.method = *method;
            global.estimator = *estimator;
            global.channelCount = document.shape.channelCount;
            std::vector<double> coefficients;
            if (!takeField(fields, "coefficients", value) ||
                !readNumbers(value, 2ull * document.shape.channelCount, coefficients))
            {
                return lineFailure("the global coefficients are missing or not two per channel", lineNumber);
            }
            for (std::uint32_t channel = 0u; channel < global.channelCount; ++channel)
            {
                global.channels[channel] = NormalizationChannelCoefficients{.scale = coefficients[2u * channel],
                                                                            .offset = coefficients[2u * channel + 1u]};
            }
            std::uint64_t count = 0u;
            if (!takeField(fields, "dispersionFallbacks", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("the global dispersion fallback count is missing or malformed", lineNumber);
            }
            global.dispersionFallbacks = static_cast<std::uint32_t>(count);
            if (!takeField(fields, "locationFallbacks", value) || !readCount(value, count) || count > UINT32_MAX)
            {
                return lineFailure("the global location fallback count is missing or malformed", lineNumber);
            }
            global.locationFallbacks = static_cast<std::uint32_t>(count);
            if (!fields.empty())
            {
                return lineFailure("the global match carries a field this form does not know", lineNumber);
            }
            frame.global = global;
            lastWasFrame = false;
            continue;
        }
        return lineFailure("a line names a record this form does not know", lineNumber);
    }
    if (!sawHeader || !sawRequest || !sawReference)
    {
        return lineFailure("the document ends before its header, request and reference lines", lineNumber);
    }
    if (document.frames.size() != declaredFrames)
    {
        return lineFailure("fewer frames than the header declared", lineNumber);
    }
    const Status ready = checkDocument(document, kParseOperation);
    if (ready.failed())
    {
        return lineFailure(ready.message(), lineNumber);
    }
    return document;
}

} // namespace an
