// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "AfterNightExtensionProtocol.h"

#include <QCborArray>
#include <QCborParserError>
#include <QCborValue>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace
{

using an::ui::extensions::ExtensionProtocolError;
using an::ui::extensions::ExtensionProtocolErrorDomain;
using an::ui::extensions::ExtensionProtocolMessage;
using an::ui::extensions::RuntimeHelloAckMessage;
using an::ui::extensions::RuntimeHelloMessage;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferColorSpace;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferPixelFormat;
using an::ui::extensions::SharedBufferPixelType;
using an::ui::extensions::SharedBufferRole;

template <typename EnumT> struct EnumNameEntry
{
    EnumT value;
    std::string_view name;
};

template <typename EnumT, std::size_t N>
std::string_view enumName(const EnumT value, const std::array<EnumNameEntry<EnumT>, N>& entries)
{
    for (const auto& entry : entries)
    {
        if (entry.value == value)
        {
            return entry.name;
        }
    }

    return {};
}

template <typename EnumT, std::size_t N>
bool parseEnum(const std::string_view name, const std::array<EnumNameEntry<EnumT>, N>& entries, EnumT* out_value)
{
    for (const auto& entry : entries)
    {
        if (entry.name == name)
        {
            if (out_value)
            {
                *out_value = entry.value;
            }
            return true;
        }
    }

    return false;
}

const std::array<EnumNameEntry<ExtensionProtocolErrorDomain>, 6> kErrorDomains = {{
    {ExtensionProtocolErrorDomain::Runtime, "runtime"},
    {ExtensionProtocolErrorDomain::Logging, "logging"},
    {ExtensionProtocolErrorDomain::Settings, "settings"},
    {ExtensionProtocolErrorDomain::Buffers, "buffers"},
    {ExtensionProtocolErrorDomain::Execution, "execution"},
    {ExtensionProtocolErrorDomain::Host, "host"},
}};

const std::array<EnumNameEntry<SharedBufferPixelType>, 4> kPixelTypes = {{
    {SharedBufferPixelType::UInt8, "uint8"},
    {SharedBufferPixelType::UInt16, "uint16"},
    {SharedBufferPixelType::Float32, "float32"},
    {SharedBufferPixelType::Float64, "float64"},
}};

const std::array<EnumNameEntry<SharedBufferPixelFormat>, 6> kPixelFormats = {{
    {SharedBufferPixelFormat::Mono, "mono"},
    {SharedBufferPixelFormat::RGB, "rgb"},
    {SharedBufferPixelFormat::BayerRGGB, "bayer_rggb"},
    {SharedBufferPixelFormat::BayerBGGR, "bayer_bggr"},
    {SharedBufferPixelFormat::BayerGRBG, "bayer_grbg"},
    {SharedBufferPixelFormat::BayerGBRG, "bayer_gbrg"},
}};

const std::array<EnumNameEntry<SharedBufferColorSpace>, 3> kColorSpaces = {{
    {SharedBufferColorSpace::Linear, "linear"},
    {SharedBufferColorSpace::SRGB, "srgb"},
    {SharedBufferColorSpace::Grayscale, "grayscale"},
}};

const std::array<EnumNameEntry<SharedBufferAccessMode>, 2> kAccessModes = {{
    {SharedBufferAccessMode::ReadOnly, "read_only"},
    {SharedBufferAccessMode::ReadWrite, "read_write"},
}};

const std::array<EnumNameEntry<SharedBufferRole>, 5> kBufferRoles = {{
    {SharedBufferRole::Source, "source"},
    {SharedBufferRole::Destination, "destination"},
    {SharedBufferRole::Preview, "preview"},
    {SharedBufferRole::Mask, "mask"},
    {SharedBufferRole::Weight, "weight"},
}};

bool requireString(const QCborMap& map, const char* key, std::string* out_value, std::string* error)
{
    const auto it = map.find(QString::fromLatin1(key));
    if (it == map.end() || !it->isString())
    {
        if (error)
        {
            *error = std::string("Missing or invalid string field: ") + key;
        }
        return false;
    }

    if (out_value)
    {
        *out_value = it->toString().toStdString();
    }
    return true;
}

bool requireInt(const QCborMap& map, const char* key, int* out_value, std::string* error)
{
    const auto it = map.find(QString::fromLatin1(key));
    if (it == map.end() || !it->isInteger())
    {
        if (error)
        {
            *error = std::string("Missing or invalid integer field: ") + key;
        }
        return false;
    }

    const qint64 value = it->toInteger();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
    {
        if (error)
        {
            *error = std::string("Integer field is out of range: ") + key;
        }
        return false;
    }

    if (out_value)
    {
        *out_value = static_cast<int>(value);
    }
    return true;
}

bool requireUInt64(const QCborMap& map, const char* key, std::uint64_t* out_value, std::string* error)
{
    const auto it = map.find(QString::fromLatin1(key));
    if (it == map.end() || !it->isInteger())
    {
        if (error)
        {
            *error = std::string("Missing or invalid integer field: ") + key;
        }
        return false;
    }

    const qint64 value = it->toInteger();
    if (value < 0)
    {
        if (error)
        {
            *error = std::string("Negative integer field is invalid: ") + key;
        }
        return false;
    }

    if (out_value)
    {
        *out_value = static_cast<std::uint64_t>(value);
    }
    return true;
}

bool readOptionalInt(const QCborMap& map, const char* key, std::optional<int>* out_value, std::string* error)
{
    const auto it = map.find(QString::fromLatin1(key));
    if (it == map.end())
    {
        if (out_value)
        {
            *out_value = std::nullopt;
        }
        return true;
    }

    if (!it->isInteger())
    {
        if (error)
        {
            *error = std::string("Optional integer field is invalid: ") + key;
        }
        return false;
    }

    const qint64 value = it->toInteger();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
    {
        if (error)
        {
            *error = std::string("Optional integer field is out of range: ") + key;
        }
        return false;
    }

    if (out_value)
    {
        *out_value = static_cast<int>(value);
    }
    return true;
}

bool requireStringList(const QCborMap& map, const char* key, std::vector<std::string>* out_values, std::string* error)
{
    const auto it = map.find(QString::fromLatin1(key));
    if (it == map.end() || !it->isArray())
    {
        if (error)
        {
            *error = std::string("Missing or invalid array field: ") + key;
        }
        return false;
    }

    std::vector<std::string> values;
    const auto array = it->toArray();
    values.reserve(array.size());
    for (const auto& entry : array)
    {
        if (!entry.isString())
        {
            if (error)
            {
                *error = std::string("Array field contains a non-string entry: ") + key;
            }
            return false;
        }
        values.push_back(entry.toString().toStdString());
    }

    if (out_values)
    {
        *out_values = std::move(values);
    }
    return true;
}

QCborArray toStringArray(const std::vector<std::string>& values)
{
    QCborArray array;
    for (const auto& value : values)
    {
        array.push_back(QString::fromStdString(value));
    }
    return array;
}

bool readOptionalSharedBufferDescriptorList(const QCborMap& map, const char* key,
                                            std::vector<SharedBufferDescriptor>* out_values, std::string* error)
{
    const auto it = map.find(QString::fromLatin1(key));
    if (it == map.end())
    {
        if (out_values)
        {
            out_values->clear();
        }
        return true;
    }
    if (!it->isArray())
    {
        if (error)
        {
            *error = std::string("Optional array field is invalid: ") + key;
        }
        return false;
    }

    std::vector<SharedBufferDescriptor> values;
    const auto array = it->toArray();
    values.reserve(array.size());
    for (const auto& entry : array)
    {
        SharedBufferDescriptor descriptor;
        if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(entry, &descriptor, error))
        {
            if (error && !error->empty())
            {
                *error = std::string("Invalid shared buffer descriptor in field ") + key + ": " + *error;
            }
            return false;
        }
        values.push_back(std::move(descriptor));
    }

    if (out_values)
    {
        *out_values = std::move(values);
    }
    return true;
}

QCborArray sharedBufferDescriptorArray(const std::vector<SharedBufferDescriptor>& descriptors)
{
    QCborArray array;
    for (const auto& descriptor : descriptors)
    {
        array.push_back(sharedBufferDescriptorToCborMap(descriptor));
    }
    return array;
}

bool isValidMessageType(const std::string_view type) noexcept
{
    const auto dot = type.find('.');
    return dot != std::string_view::npos && dot > 0 && dot + 1 < type.size();
}

int bytesPerSample(const SharedBufferPixelType pixel_type) noexcept
{
    switch (pixel_type)
    {
    case SharedBufferPixelType::UInt8:
        return 1;
    case SharedBufferPixelType::UInt16:
        return 2;
    case SharedBufferPixelType::Float32:
        return 4;
    case SharedBufferPixelType::Float64:
        return 8;
    }

    return 0;
}

bool pixelFormatMatchesChannels(const SharedBufferDescriptor& descriptor) noexcept
{
    switch (descriptor.pixel_format)
    {
    case SharedBufferPixelFormat::RGB:
        return descriptor.channels == 3;
    case SharedBufferPixelFormat::Mono:
    case SharedBufferPixelFormat::BayerRGGB:
    case SharedBufferPixelFormat::BayerBGGR:
    case SharedBufferPixelFormat::BayerGRBG:
    case SharedBufferPixelFormat::BayerGBRG:
        return descriptor.channels == 1;
    }

    return false;
}

QCborMap mapWithoutReservedMessageKeys(const QCborMap& map)
{
    QCborMap fields = map;
    fields.remove(QStringLiteral("type"));
    fields.remove(QStringLiteral("session_id"));
    fields.remove(QStringLiteral("request_id"));
    return fields;
}

bool parseRuntimeMessageType(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                             std::string* error)
{
    if (message.type != expected_type)
    {
        if (error)
        {
            *error = "Unexpected message type: " + message.type;
        }
        return false;
    }
    if (message.session_id.empty())
    {
        if (error)
        {
            *error = "Runtime protocol message requires a session_id";
        }
        return false;
    }
    return true;
}

bool parseRuntimeHeartbeatMessage(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                                  int* out_sequence, std::string* error)
{
    if (!parseRuntimeMessageType(message, expected_type, error))
    {
        return false;
    }
    if (message.request_id.empty())
    {
        if (error)
        {
            *error = "Runtime heartbeat message requires a request_id";
        }
        return false;
    }
    return requireInt(message.fields, "sequence", out_sequence, error);
}

bool parseExecutionRequestMessage(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                                  std::string* out_params_token, QCborMap* out_params,
                                  std::vector<SharedBufferDescriptor>* out_buffers, std::string* error)
{
    if (!parseRuntimeMessageType(message, expected_type, error))
    {
        return false;
    }
    if (message.request_id.empty())
    {
        if (error)
        {
            *error = "Execution request message requires a request_id";
        }
        return false;
    }
    if (!requireString(message.fields, "params_token", out_params_token, error))
    {
        return false;
    }

    const auto params_it = message.fields.find(QStringLiteral("params"));
    if (params_it != message.fields.end())
    {
        if (!params_it->isMap())
        {
            if (error)
            {
                *error = "Invalid map field: params";
            }
            return false;
        }
        if (out_params)
        {
            *out_params = params_it->toMap();
        }
    }
    else if (out_params)
    {
        out_params->clear();
    }

    return readOptionalSharedBufferDescriptorList(message.fields, "buffers", out_buffers, error);
}

bool parseExecutionResultLikeMessage(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                                     std::string* target_request_id, std::string* error)
{
    if (!parseRuntimeMessageType(message, expected_type, error))
    {
        return false;
    }
    if (message.request_id.empty())
    {
        if (error)
        {
            *error = "Execution result message requires a request_id";
        }
        return false;
    }
    return requireString(message.fields, "target_request_id", target_request_id, error);
}

bool parseUiEventMessage(const ExtensionProtocolMessage& message, std::string* widget_id, std::string* event_name,
                         QCborValue* value, std::string* error)
{
    if (!parseRuntimeMessageType(message, "ui.event", error))
    {
        return false;
    }
    if (!requireString(message.fields, "widget_id", widget_id, error) ||
        !requireString(message.fields, "event", event_name, error))
    {
        return false;
    }

    const auto value_it = message.fields.find(QStringLiteral("value"));
    if (value_it == message.fields.end())
    {
        if (error)
        {
            *error = "Missing value field: value";
        }
        return false;
    }

    if (value)
    {
        *value = value_it.value();
    }
    return true;
}

bool parseUiValidateMessage(const ExtensionProtocolMessage& message, QCborMap* params, std::string* error)
{
    if (!parseRuntimeMessageType(message, "ui.validate", error))
    {
        return false;
    }
    if (message.request_id.empty())
    {
        if (error)
        {
            *error = "ui.validate message requires a request_id";
        }
        return false;
    }

    const auto params_it = message.fields.find(QStringLiteral("params"));
    if (params_it == message.fields.end() || !params_it->isMap())
    {
        if (error)
        {
            *error = "Missing or invalid map field: params";
        }
        return false;
    }

    if (params)
    {
        *params = params_it->toMap();
    }
    return true;
}

bool parseUiValidateResultMessage(const ExtensionProtocolMessage& message, bool* valid, QCborMap* errors_map,
                                  std::string* error)
{
    if (!parseRuntimeMessageType(message, "ui.validate.result", error))
    {
        return false;
    }
    if (message.request_id.empty())
    {
        if (error)
        {
            *error = "ui.validate.result message requires a request_id";
        }
        return false;
    }

    const auto valid_it = message.fields.find(QStringLiteral("valid"));
    if (valid_it == message.fields.end() || !valid_it->isBool())
    {
        if (error)
        {
            *error = "Missing or invalid bool field: valid";
        }
        return false;
    }
    if (valid)
    {
        *valid = valid_it->toBool();
    }

    const auto errors_it = message.fields.find(QStringLiteral("errors"));
    if (errors_it == message.fields.end())
    {
        if (errors_map)
        {
            errors_map->clear();
        }
        return true;
    }
    if (!errors_it->isMap())
    {
        if (error)
        {
            *error = "Invalid map field: errors";
        }
        return false;
    }

    const QCborMap parsed_errors = errors_it->toMap();
    for (auto it = parsed_errors.begin(); it != parsed_errors.end(); ++it)
    {
        if (!it.key().isString() || !it.value().isString())
        {
            if (error)
            {
                *error = "ui.validate.result errors must map string widget ids to string messages";
            }
            return false;
        }
    }

    if (errors_map)
    {
        *errors_map = parsed_errors;
    }
    return true;
}

bool parseViewDescribeLikeMessage(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                                  std::string* out_view_id, SharedBufferDescriptor* out_source_buffer,
                                  std::vector<SharedBufferDescriptor>* out_auxiliary_buffers, QCborMap* out_metadata,
                                  QCborMap* out_display_settings, std::string* error)
{
    if (!parseRuntimeMessageType(message, expected_type, error) ||
        !requireString(message.fields, "view_id", out_view_id, error))
    {
        return false;
    }

    const auto source_it = message.fields.find(QStringLiteral("source_buffer"));
    if (source_it == message.fields.end())
    {
        if (error)
        {
            *error = "Missing source_buffer field";
        }
        return false;
    }

    if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(source_it.value(), out_source_buffer, error))
    {
        if (error && !error->empty())
        {
            *error = "Invalid source_buffer field: " + *error;
        }
        return false;
    }

    if (!readOptionalSharedBufferDescriptorList(message.fields, "auxiliary_buffers", out_auxiliary_buffers, error))
    {
        return false;
    }

    const auto read_optional_map = [&message](const char* key, QCborMap* destination, std::string* out_error) -> bool {
        const auto it = message.fields.find(QString::fromLatin1(key));
        if (it == message.fields.end())
        {
            if (destination)
            {
                destination->clear();
            }
            return true;
        }

        if (!it->isMap())
        {
            if (out_error)
            {
                *out_error = std::string("Optional map field is invalid: ") + key;
            }
            return false;
        }

        if (destination)
        {
            *destination = it->toMap();
        }
        return true;
    };

    return read_optional_map("metadata", out_metadata, error) &&
           read_optional_map("display_settings", out_display_settings, error);
}

bool parseViewLifecycleMessage(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                               std::string* out_view_id, std::string* out_view_name, std::string* out_file_name,
                               std::string* out_selector_tooltip, std::string* error)
{
    if (!parseRuntimeMessageType(message, expected_type, error) ||
        !requireString(message.fields, "view_id", out_view_id, error))
    {
        return false;
    }

    const auto read_optional_string = [&message](const char* key, std::string* destination,
                                                 std::string* out_error) -> bool {
        const auto it = message.fields.find(QString::fromLatin1(key));
        if (it == message.fields.end())
        {
            if (destination)
            {
                destination->clear();
            }
            return true;
        }

        if (!it->isString())
        {
            if (out_error)
            {
                *out_error = std::string("Optional string field is invalid: ") + key;
            }
            return false;
        }

        if (destination)
        {
            *destination = it->toString().toStdString();
        }
        return true;
    };

    return read_optional_string("view_name", out_view_name, error) &&
           read_optional_string("file_name", out_file_name, error) &&
           read_optional_string("selector_tooltip", out_selector_tooltip, error);
}

bool parseThemeMessage(const ExtensionProtocolMessage& message, const std::string_view expected_type,
                       std::string* out_theme_name, QCborMap* out_tokens, std::string* error)
{
    if (!parseRuntimeMessageType(message, expected_type, error))
    {
        return false;
    }

    const auto theme_name_it = message.fields.find(QStringLiteral("theme_name"));
    if (theme_name_it != message.fields.end())
    {
        if (!theme_name_it->isString())
        {
            if (error)
            {
                *error = "theme_name must be a string when present";
            }
            return false;
        }
        if (out_theme_name)
        {
            *out_theme_name = theme_name_it->toString().toStdString();
        }
    }
    else if (out_theme_name)
    {
        out_theme_name->clear();
    }

    const auto tokens_it = message.fields.find(QStringLiteral("tokens"));
    if (tokens_it == message.fields.end() || !tokens_it->isMap())
    {
        if (error)
        {
            *error = "Missing or invalid map field: tokens";
        }
        return false;
    }

    if (out_tokens)
    {
        *out_tokens = tokens_it->toMap();
    }
    return true;
}

// The first key a map anywhere inside the value states twice, in CBOR's diagnostic notation; empty when every map's
// keys are distinct. CBOR leaves what a repeated key means to the protocol, and a reader that took the first and
// carried the rest would hand a message on otherwise than it read it, so this protocol refuses one. The walk keeps its
// own stack, so a value nested as deeply as the parser admits costs no recursion here.
std::string firstRepeatedKey(const QCborValue& value)
{
    std::vector<QCborValue> pending{value};
    std::vector<QCborValue> keys;
    while (!pending.empty())
    {
        QCborValue current = std::move(pending.back());
        pending.pop_back();
        while (current.isTag())
        {
            current = current.taggedValue();
        }
        if (current.isArray())
        {
            const QCborArray array = current.toArray();
            for (const QCborValue& element : array)
            {
                pending.push_back(element);
            }
        }
        else if (current.isMap())
        {
            const QCborMap map = current.toMap();
            keys.clear();
            for (auto entry = map.cbegin(); entry != map.cend(); ++entry)
            {
                keys.push_back(entry.key());
                pending.push_back(entry.value());
            }
            std::sort(keys.begin(), keys.end(),
                      [](const QCborValue& left, const QCborValue& right) { return left.compare(right) < 0; });
            const auto repeated =
                std::adjacent_find(keys.begin(), keys.end(), [](const QCborValue& left, const QCborValue& right) {
                    return left.compare(right) == 0;
                });
            if (repeated != keys.end())
            {
                return repeated->toDiagnosticNotation().toStdString();
            }
        }
    }
    return {};
}

} // anonymous namespace

namespace an::ui::extensions
{

bool ExtensionProtocolError::valid() const noexcept
{
    return isValidProtocolErrorCode(code);
}

ProtocolCompatibilityResult negotiateProtocolVersion(const int supported_version, const int peer_version)
{
    ProtocolCompatibilityResult result;
    if (supported_version <= 0 || peer_version <= 0)
    {
        result.state = ProtocolCompatibilityState::Invalid;
        result.diagnostic = "Protocol versions must be positive integers";
        return result;
    }

    if (supported_version == peer_version)
    {
        result.state = ProtocolCompatibilityState::Compatible;
        result.negotiated_version = supported_version;
        return result;
    }

    if (peer_version < supported_version)
    {
        result.state = ProtocolCompatibilityState::PeerTooOld;
        result.diagnostic = "Peer protocol version is older than the supported host version";
        return result;
    }

    result.state = ProtocolCompatibilityState::PeerTooNew;
    result.diagnostic = "Peer protocol version is newer than the supported host version";
    return result;
}

int makeProtocolErrorCode(const ExtensionProtocolErrorDomain domain, const int local_code) noexcept
{
    if (local_code < 0 || local_code > 999)
    {
        return 0;
    }

    switch (domain)
    {
    case ExtensionProtocolErrorDomain::Runtime:
        return 1000 + local_code;
    case ExtensionProtocolErrorDomain::Logging:
        return 2000 + local_code;
    case ExtensionProtocolErrorDomain::Settings:
        return 3000 + local_code;
    case ExtensionProtocolErrorDomain::Buffers:
        return 4000 + local_code;
    case ExtensionProtocolErrorDomain::Execution:
        return 5000 + local_code;
    case ExtensionProtocolErrorDomain::Host:
        return 6000 + local_code;
    case ExtensionProtocolErrorDomain::Invalid:
        break;
    }

    return 0;
}

bool isValidProtocolErrorCode(const int code) noexcept
{
    return code >= 1000 && code <= 6999;
}

ExtensionProtocolErrorDomain classifyProtocolErrorCode(const int code) noexcept
{
    if (code >= 1000 && code <= 1999)
    {
        return ExtensionProtocolErrorDomain::Runtime;
    }
    if (code >= 2000 && code <= 2999)
    {
        return ExtensionProtocolErrorDomain::Logging;
    }
    if (code >= 3000 && code <= 3999)
    {
        return ExtensionProtocolErrorDomain::Settings;
    }
    if (code >= 4000 && code <= 4999)
    {
        return ExtensionProtocolErrorDomain::Buffers;
    }
    if (code >= 5000 && code <= 5999)
    {
        return ExtensionProtocolErrorDomain::Execution;
    }
    if (code >= 6000 && code <= 6999)
    {
        return ExtensionProtocolErrorDomain::Host;
    }

    return ExtensionProtocolErrorDomain::Invalid;
}

std::string formatProtocolErrorCode(const int code)
{
    if (!isValidProtocolErrorCode(code))
    {
        return "E0000";
    }

    return fmt::format("E{:04d}", code);
}

std::string_view protocolErrorDomainName(const ExtensionProtocolErrorDomain domain) noexcept
{
    return enumName(domain, kErrorDomains);
}

std::string_view sharedBufferPixelTypeName(const SharedBufferPixelType value) noexcept
{
    return enumName(value, kPixelTypes);
}

std::string_view sharedBufferPixelFormatName(const SharedBufferPixelFormat value) noexcept
{
    return enumName(value, kPixelFormats);
}

std::string_view sharedBufferColorSpaceName(const SharedBufferColorSpace value) noexcept
{
    return enumName(value, kColorSpaces);
}

std::string_view sharedBufferAccessModeName(const SharedBufferAccessMode value) noexcept
{
    return enumName(value, kAccessModes);
}

std::string_view sharedBufferRoleName(const SharedBufferRole value) noexcept
{
    return enumName(value, kBufferRoles);
}

QCborMap protocolErrorToCborMap(const ExtensionProtocolError& error)
{
    QCborMap map;
    map[QStringLiteral("code")] = error.code;
    map[QStringLiteral("message")] = QString::fromStdString(error.message);
    if (!error.details.empty())
    {
        map[QStringLiteral("details")] = QString::fromStdString(error.details);
    }
    return map;
}

bool protocolErrorFromCborValue(const QCborValue& value, ExtensionProtocolError* out_error, std::string* error)
{
    if (!value.isMap())
    {
        if (error)
        {
            *error = "Protocol error value must be a map";
        }
        return false;
    }

    const auto map = value.toMap();
    ExtensionProtocolError parsed;
    if (!requireInt(map, "code", &parsed.code, error) || !requireString(map, "message", &parsed.message, error))
    {
        return false;
    }

    const auto details_it = map.find(QStringLiteral("details"));
    if (details_it != map.end())
    {
        if (!details_it->isString())
        {
            if (error)
            {
                *error = "Protocol error details must be a string";
            }
            return false;
        }
        parsed.details = details_it->toString().toStdString();
    }

    if (!parsed.valid())
    {
        if (error)
        {
            *error = "Protocol error code is outside the supported 1xxx-6xxx ranges";
        }
        return false;
    }

    if (out_error)
    {
        *out_error = std::move(parsed);
    }
    return true;
}

QCborMap protocolMessageToCborMap(const ExtensionProtocolMessage& message)
{
    QCborMap map = message.fields;
    map[QStringLiteral("type")] = QString::fromStdString(message.type);
    map[QStringLiteral("session_id")] = QString::fromStdString(message.session_id);
    if (!message.request_id.empty())
    {
        map[QStringLiteral("request_id")] = QString::fromStdString(message.request_id);
    }
    return map;
}

bool protocolMessageFromCborValue(const QCborValue& value, ExtensionProtocolMessage* out_message, std::string* error)
{
    if (!value.isMap())
    {
        if (error)
        {
            *error = "Protocol message payload must be a CBOR map";
        }
        return false;
    }

    const auto map = value.toMap();
    ExtensionProtocolMessage message;
    if (!requireString(map, "type", &message.type, error) ||
        !requireString(map, "session_id", &message.session_id, error))
    {
        return false;
    }
    if (!isValidMessageType(message.type))
    {
        if (error)
        {
            *error = "Protocol message type must use the namespace.message form";
        }
        return false;
    }

    const auto request_id_it = map.find(QStringLiteral("request_id"));
    if (request_id_it != map.end())
    {
        if (!request_id_it->isString())
        {
            if (error)
            {
                *error = "Protocol request_id must be a string";
            }
            return false;
        }
        message.request_id = request_id_it->toString().toStdString();
    }

    message.fields = mapWithoutReservedMessageKeys(map);
    if (out_message)
    {
        *out_message = std::move(message);
    }
    return true;
}

QByteArray serializeProtocolMessage(const ExtensionProtocolMessage& message, std::string* error)
{
    if (!isValidMessageType(message.type))
    {
        if (error)
        {
            *error = "Protocol message type must use the namespace.message form";
        }
        return {};
    }
    if (message.session_id.empty())
    {
        if (error)
        {
            *error = "Protocol message requires a session_id";
        }
        return {};
    }

    return QCborValue(protocolMessageToCborMap(message)).toCbor();
}

bool deserializeProtocolMessage(const QByteArray& cbor_payload, ExtensionProtocolMessage* out_message,
                                std::string* error)
{
    QCborParserError parser_error;
    const QCborValue value = QCborValue::fromCbor(cbor_payload, &parser_error);
    if (parser_error.error != QCborError::NoError)
    {
        if (error)
        {
            *error = "Failed to parse CBOR payload";
        }
        return false;
    }
    if (const std::string repeated = firstRepeatedKey(value); !repeated.empty())
    {
        if (error)
        {
            *error = "Protocol message repeats the key " + repeated + " in one map";
        }
        return false;
    }

    return protocolMessageFromCborValue(value, out_message, error);
}

QByteArray frameControlPayload(const QByteArray& cbor_payload, std::string* error)
{
    if (cbor_payload.size() < 0 || static_cast<std::uint32_t>(cbor_payload.size()) > kMaxControlMessageBytes)
    {
        if (error)
        {
            *error = fmt::format("Control payload exceeds {} bytes", kMaxControlMessageBytes);
        }
        return {};
    }

    QByteArray framed;
    framed.resize(static_cast<int>(sizeof(std::uint32_t)) + cbor_payload.size());
    const quint32 encoded_length = qToBigEndian(static_cast<quint32>(cbor_payload.size()));
    std::memcpy(framed.data(), &encoded_length, sizeof(encoded_length));
    if (!cbor_payload.isEmpty())
    {
        std::memcpy(framed.data() + sizeof(encoded_length), cbor_payload.constData(),
                    static_cast<std::size_t>(cbor_payload.size()));
    }
    return framed;
}

QByteArray frameProtocolMessage(const ExtensionProtocolMessage& message, std::string* error)
{
    const QByteArray payload = serializeProtocolMessage(message, error);
    if (payload.isEmpty() && (error && !error->empty()))
    {
        return {};
    }

    return frameControlPayload(payload, error);
}

ProtocolFrameDecodeStatus tryConsumeFramedControlPayload(QByteArray* buffered_bytes, QByteArray* cbor_payload,
                                                         std::string* error)
{
    if (!buffered_bytes)
    {
        if (error)
        {
            *error = "Buffered input cannot be null";
        }
        return ProtocolFrameDecodeStatus::InvalidFrame;
    }

    if (buffered_bytes->size() < static_cast<int>(sizeof(std::uint32_t)))
    {
        return ProtocolFrameDecodeStatus::NeedMoreData;
    }

    quint32 encoded_length = 0;
    std::memcpy(&encoded_length, buffered_bytes->constData(), sizeof(encoded_length));
    const quint32 payload_size = qFromBigEndian(encoded_length);
    if (payload_size > kMaxControlMessageBytes)
    {
        if (error)
        {
            *error = fmt::format("Control payload exceeds {} bytes", kMaxControlMessageBytes);
        }
        return ProtocolFrameDecodeStatus::InvalidFrame;
    }

    const qsizetype total_size = static_cast<qsizetype>(sizeof(std::uint32_t)) + payload_size;
    if (buffered_bytes->size() < total_size)
    {
        return ProtocolFrameDecodeStatus::NeedMoreData;
    }

    if (cbor_payload)
    {
        *cbor_payload = buffered_bytes->mid(sizeof(std::uint32_t), payload_size);
    }
    buffered_bytes->remove(0, total_size);
    return ProtocolFrameDecodeStatus::Complete;
}

ProtocolFrameDecodeStatus tryConsumeFramedProtocolMessage(QByteArray* buffered_bytes,
                                                          ExtensionProtocolMessage* out_message, std::string* error)
{
    QByteArray payload;
    const auto status = tryConsumeFramedControlPayload(buffered_bytes, &payload, error);
    if (status != ProtocolFrameDecodeStatus::Complete)
    {
        return status;
    }

    if (!deserializeProtocolMessage(payload, out_message, error))
    {
        return ProtocolFrameDecodeStatus::InvalidFrame;
    }

    return ProtocolFrameDecodeStatus::Complete;
}

ExtensionProtocolMessage toProtocolMessage(const RuntimeHelloMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "runtime.hello";
    protocol.session_id = message.session_id;
    protocol.fields[QStringLiteral("extension_id")] = QString::fromStdString(message.extension_id);
    protocol.fields[QStringLiteral("protocol_version")] = message.protocol_version;
    protocol.fields[QStringLiteral("sdk_version")] = message.sdk_version;
    protocol.fields[QStringLiteral("sdk_backend")] = QString::fromStdString(message.sdk_backend);
    protocol.fields[QStringLiteral("host_version")] = QString::fromStdString(message.host_version);
    protocol.fields[QStringLiteral("capabilities")] = toStringArray(message.capabilities);
    protocol.fields[QStringLiteral("session_secret_proof")] = QString::fromStdString(message.session_secret_proof);
    if (message.native_process_window)
    {
        protocol.fields[QStringLiteral("native_process_window")] = true;
    }
    if (message.bootstrap_status_messages)
    {
        protocol.fields[QStringLiteral("bootstrap_status_messages")] = true;
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const RuntimeHelloAckMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "runtime.hello_ack";
    protocol.session_id = message.session_id;
    protocol.fields[QStringLiteral("accepted")] = message.accepted;
    protocol.fields[QStringLiteral("protocol_version")] = message.protocol_version;
    protocol.fields[QStringLiteral("sdk_version")] = message.sdk_version;
    protocol.fields[QStringLiteral("host_version")] = QString::fromStdString(message.host_version);
    protocol.fields[QStringLiteral("heartbeat_interval_ms")] = message.heartbeat_interval_ms;
    protocol.fields[QStringLiteral("heartbeat_miss_limit")] = message.heartbeat_miss_limit;
    protocol.fields[QStringLiteral("cancel_grace_period_ms")] = message.cancel_grace_period_ms;
    protocol.fields[QStringLiteral("capabilities")] = toStringArray(message.capabilities);
    if (message.error.has_value())
    {
        protocol.fields[QStringLiteral("error")] = protocolErrorToCborMap(*message.error);
    }
    if (message.native_process_window)
    {
        protocol.fields[QStringLiteral("native_process_window")] = true;
    }
    if (message.bootstrap_status_messages)
    {
        protocol.fields[QStringLiteral("bootstrap_status_messages")] = true;
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const RuntimePingMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "runtime.ping";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("sequence")] = message.sequence;
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const RuntimePongMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "runtime.pong";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("sequence")] = message.sequence;
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ExecutionExecuteMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "execution.execute";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("params_token")] = QString::fromStdString(message.params_token);
    if (!message.params.isEmpty())
    {
        protocol.fields[QStringLiteral("params")] = message.params;
    }
    if (!message.buffers.empty())
    {
        protocol.fields[QStringLiteral("buffers")] = sharedBufferDescriptorArray(message.buffers);
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ExecutionPreviewMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "execution.preview";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("params_token")] = QString::fromStdString(message.params_token);
    if (!message.params.isEmpty())
    {
        protocol.fields[QStringLiteral("params")] = message.params;
    }
    if (!message.buffers.empty())
    {
        protocol.fields[QStringLiteral("buffers")] = sharedBufferDescriptorArray(message.buffers);
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ExecutionCancelMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "execution.cancel";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("target_request_id")] = QString::fromStdString(message.target_request_id);
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ExecutionResultMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "execution.result";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("target_request_id")] = QString::fromStdString(message.target_request_id);
    protocol.fields[QStringLiteral("result_kind")] = QString::fromStdString(message.result_kind);
    protocol.fields[QStringLiteral("status")] = QString::fromStdString(message.status);
    protocol.fields[QStringLiteral("detail")] = QString::fromStdString(message.detail);
    if (!message.history_step_name.empty())
    {
        protocol.fields[QStringLiteral("history_step_name")] = QString::fromStdString(message.history_step_name);
    }
    if (!message.history_step_description.empty())
    {
        protocol.fields[QStringLiteral("history_step_description")] =
            QString::fromStdString(message.history_step_description);
    }
    if (!message.output_buffers.empty())
    {
        protocol.fields[QStringLiteral("output_buffers")] = sharedBufferDescriptorArray(message.output_buffers);
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const UiEventMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "ui.event";
    protocol.session_id = message.session_id;
    protocol.fields[QStringLiteral("widget_id")] = QString::fromStdString(message.widget_id);
    protocol.fields[QStringLiteral("event")] = QString::fromStdString(message.event_name);
    protocol.fields[QStringLiteral("value")] = message.value;
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const UiValidateMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "ui.validate";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("params")] = message.params;
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const UiValidateResultMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "ui.validate.result";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("valid")] = message.valid;
    if (!message.errors.isEmpty())
    {
        protocol.fields[QStringLiteral("errors")] = message.errors;
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ViewDescribeMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "view.describe";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("view_id")] = QString::fromStdString(message.view_id);
    if (!message.view_name.empty())
    {
        protocol.fields[QStringLiteral("view_name")] = QString::fromStdString(message.view_name);
    }
    if (!message.file_name.empty())
    {
        protocol.fields[QStringLiteral("file_name")] = QString::fromStdString(message.file_name);
    }
    protocol.fields[QStringLiteral("source_buffer")] = sharedBufferDescriptorToCborMap(message.source_buffer);
    if (!message.auxiliary_buffers.empty())
    {
        protocol.fields[QStringLiteral("auxiliary_buffers")] = sharedBufferDescriptorArray(message.auxiliary_buffers);
    }
    if (!message.metadata.isEmpty())
    {
        protocol.fields[QStringLiteral("metadata")] = message.metadata;
    }
    if (!message.display_settings.isEmpty())
    {
        protocol.fields[QStringLiteral("display_settings")] = message.display_settings;
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ViewCommitMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "view.commit";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("view_id")] = QString::fromStdString(message.view_id);
    if (!message.history_step_name.empty())
    {
        protocol.fields[QStringLiteral("history_step_name")] = QString::fromStdString(message.history_step_name);
    }
    if (!message.history_step_description.empty())
    {
        protocol.fields[QStringLiteral("history_step_description")] =
            QString::fromStdString(message.history_step_description);
    }
    if (message.allow_resized_output)
    {
        protocol.fields[QStringLiteral("allow_resized_output")] = true;
    }
    if (!message.params.isEmpty())
    {
        protocol.fields[QStringLiteral("params")] = message.params;
    }
    if (!message.result_buffers.empty())
    {
        protocol.fields[QStringLiteral("result_buffers")] = sharedBufferDescriptorArray(message.result_buffers);
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ViewUpdatedMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "view.updated";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("view_id")] = QString::fromStdString(message.view_id);
    protocol.fields[QStringLiteral("source_buffer")] = sharedBufferDescriptorToCborMap(message.source_buffer);
    if (!message.auxiliary_buffers.empty())
    {
        protocol.fields[QStringLiteral("auxiliary_buffers")] = sharedBufferDescriptorArray(message.auxiliary_buffers);
    }
    if (!message.metadata.isEmpty())
    {
        protocol.fields[QStringLiteral("metadata")] = message.metadata;
    }
    if (!message.display_settings.isEmpty())
    {
        protocol.fields[QStringLiteral("display_settings")] = message.display_settings;
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ViewCreatedMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "view.created";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("view_id")] = QString::fromStdString(message.view_id);
    if (!message.view_name.empty())
    {
        protocol.fields[QStringLiteral("view_name")] = QString::fromStdString(message.view_name);
    }
    if (!message.file_name.empty())
    {
        protocol.fields[QStringLiteral("file_name")] = QString::fromStdString(message.file_name);
    }
    if (!message.selector_tooltip.empty())
    {
        protocol.fields[QStringLiteral("selector_tooltip")] = QString::fromStdString(message.selector_tooltip);
    }
    if (message.width.has_value())
    {
        protocol.fields[QStringLiteral("width")] = *message.width;
    }
    if (message.height.has_value())
    {
        protocol.fields[QStringLiteral("height")] = *message.height;
    }
    if (message.channels.has_value())
    {
        protocol.fields[QStringLiteral("channels")] = *message.channels;
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ViewDestroyedMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "view.destroyed";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("view_id")] = QString::fromStdString(message.view_id);
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ViewRenamedMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "view.renamed";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    protocol.fields[QStringLiteral("view_id")] = QString::fromStdString(message.view_id);
    if (!message.view_name.empty())
    {
        protocol.fields[QStringLiteral("view_name")] = QString::fromStdString(message.view_name);
    }
    if (!message.file_name.empty())
    {
        protocol.fields[QStringLiteral("file_name")] = QString::fromStdString(message.file_name);
    }
    if (!message.selector_tooltip.empty())
    {
        protocol.fields[QStringLiteral("selector_tooltip")] = QString::fromStdString(message.selector_tooltip);
    }
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ThemeDescribeMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "theme.describe";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    if (!message.theme_name.empty())
    {
        protocol.fields[QStringLiteral("theme_name")] = QString::fromStdString(message.theme_name);
    }
    protocol.fields[QStringLiteral("tokens")] = message.tokens;
    return protocol;
}

ExtensionProtocolMessage toProtocolMessage(const ThemeChangedMessage& message)
{
    ExtensionProtocolMessage protocol;
    protocol.type = "theme.changed";
    protocol.session_id = message.session_id;
    protocol.request_id = message.request_id;
    if (!message.theme_name.empty())
    {
        protocol.fields[QStringLiteral("theme_name")] = QString::fromStdString(message.theme_name);
    }
    protocol.fields[QStringLiteral("tokens")] = message.tokens;
    return protocol;
}

bool runtimeHelloFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimeHelloMessage* out_message,
                                     std::string* error)
{
    if (!parseRuntimeMessageType(message, "runtime.hello", error))
    {
        return false;
    }

    RuntimeHelloMessage parsed;
    parsed.session_id = message.session_id;
    if (!requireString(message.fields, "extension_id", &parsed.extension_id, error) ||
        !requireInt(message.fields, "protocol_version", &parsed.protocol_version, error) ||
        !requireInt(message.fields, "sdk_version", &parsed.sdk_version, error) ||
        !requireString(message.fields, "sdk_backend", &parsed.sdk_backend, error) ||
        !requireString(message.fields, "host_version", &parsed.host_version, error) ||
        !requireStringList(message.fields, "capabilities", &parsed.capabilities, error) ||
        !requireString(message.fields, "session_secret_proof", &parsed.session_secret_proof, error))
    {
        return false;
    }

    const auto npw_it = message.fields.find(QStringLiteral("native_process_window"));
    if (npw_it != message.fields.end() && npw_it->isBool())
    {
        parsed.native_process_window = npw_it->toBool();
    }
    const auto bootstrap_status_it = message.fields.find(QStringLiteral("bootstrap_status_messages"));
    if (bootstrap_status_it != message.fields.end() && bootstrap_status_it->isBool())
    {
        parsed.bootstrap_status_messages = bootstrap_status_it->toBool();
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool runtimeHelloAckFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimeHelloAckMessage* out_message,
                                        std::string* error)
{
    if (!parseRuntimeMessageType(message, "runtime.hello_ack", error))
    {
        return false;
    }

    RuntimeHelloAckMessage parsed;
    parsed.session_id = message.session_id;

    const auto accepted_it = message.fields.find(QStringLiteral("accepted"));
    if (accepted_it == message.fields.end() || !accepted_it->isBool())
    {
        if (error)
        {
            *error = "Missing or invalid bool field: accepted";
        }
        return false;
    }
    parsed.accepted = accepted_it->toBool();

    if (!requireInt(message.fields, "protocol_version", &parsed.protocol_version, error) ||
        !requireInt(message.fields, "sdk_version", &parsed.sdk_version, error) ||
        !requireString(message.fields, "host_version", &parsed.host_version, error) ||
        !requireInt(message.fields, "heartbeat_interval_ms", &parsed.heartbeat_interval_ms, error) ||
        !requireInt(message.fields, "heartbeat_miss_limit", &parsed.heartbeat_miss_limit, error) ||
        !requireInt(message.fields, "cancel_grace_period_ms", &parsed.cancel_grace_period_ms, error) ||
        !requireStringList(message.fields, "capabilities", &parsed.capabilities, error))
    {
        return false;
    }

    const auto error_it = message.fields.find(QStringLiteral("error"));
    if (error_it != message.fields.end())
    {
        ExtensionProtocolError protocol_error;
        if (!protocolErrorFromCborValue(error_it.value(), &protocol_error, error))
        {
            return false;
        }
        parsed.error = protocol_error;
    }

    const auto npw_it = message.fields.find(QStringLiteral("native_process_window"));
    if (npw_it != message.fields.end() && npw_it->isBool())
    {
        parsed.native_process_window = npw_it->toBool();
    }
    const auto bootstrap_status_it = message.fields.find(QStringLiteral("bootstrap_status_messages"));
    if (bootstrap_status_it != message.fields.end() && bootstrap_status_it->isBool())
    {
        parsed.bootstrap_status_messages = bootstrap_status_it->toBool();
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool runtimePingFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimePingMessage* out_message,
                                    std::string* error)
{
    RuntimePingMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseRuntimeHeartbeatMessage(message, "runtime.ping", &parsed.sequence, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool runtimePongFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimePongMessage* out_message,
                                    std::string* error)
{
    RuntimePongMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseRuntimeHeartbeatMessage(message, "runtime.pong", &parsed.sequence, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool executionExecuteFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionExecuteMessage* out_message,
                                         std::string* error)
{
    ExecutionExecuteMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseExecutionRequestMessage(message, "execution.execute", &parsed.params_token, &parsed.params,
                                      &parsed.buffers, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool executionPreviewFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionPreviewMessage* out_message,
                                         std::string* error)
{
    ExecutionPreviewMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseExecutionRequestMessage(message, "execution.preview", &parsed.params_token, &parsed.params,
                                      &parsed.buffers, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool executionCancelFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionCancelMessage* out_message,
                                        std::string* error)
{
    ExecutionCancelMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseExecutionResultLikeMessage(message, "execution.cancel", &parsed.target_request_id, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool executionResultFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionResultMessage* out_message,
                                        std::string* error)
{
    ExecutionResultMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseExecutionResultLikeMessage(message, "execution.result", &parsed.target_request_id, error) ||
        !requireString(message.fields, "result_kind", &parsed.result_kind, error) ||
        !requireString(message.fields, "status", &parsed.status, error) ||
        !requireString(message.fields, "detail", &parsed.detail, error))
    {
        return false;
    }

    const auto read_optional_string = [&message](const char* key, std::string* out_value) {
        const auto it = message.fields.find(QString::fromLatin1(key));
        if (it != message.fields.end() && it->isString())
        {
            *out_value = it->toString().toStdString();
        }
    };
    read_optional_string("history_step_name", &parsed.history_step_name);
    read_optional_string("history_step_description", &parsed.history_step_description);
    if (!readOptionalSharedBufferDescriptorList(message.fields, "output_buffers", &parsed.output_buffers, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool uiEventFromProtocolMessage(const ExtensionProtocolMessage& message, UiEventMessage* out_message,
                                std::string* error)
{
    UiEventMessage parsed;
    parsed.session_id = message.session_id;
    if (!parseUiEventMessage(message, &parsed.widget_id, &parsed.event_name, &parsed.value, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool uiValidateFromProtocolMessage(const ExtensionProtocolMessage& message, UiValidateMessage* out_message,
                                   std::string* error)
{
    UiValidateMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseUiValidateMessage(message, &parsed.params, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool uiValidateResultFromProtocolMessage(const ExtensionProtocolMessage& message, UiValidateResultMessage* out_message,
                                         std::string* error)
{
    UiValidateResultMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseUiValidateResultMessage(message, &parsed.valid, &parsed.errors, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool viewDescribeFromProtocolMessage(const ExtensionProtocolMessage& message, ViewDescribeMessage* out_message,
                                     std::string* error)
{
    ViewDescribeMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseViewDescribeLikeMessage(message, "view.describe", &parsed.view_id, &parsed.source_buffer,
                                      &parsed.auxiliary_buffers, &parsed.metadata, &parsed.display_settings, error))
    {
        return false;
    }

    // view.describe also carries the optional lifecycle fields (view_name,
    // file_name) that view.created/view.renamed share.  Parse them without
    // re-checking message type or view_id, which parseViewDescribeLikeMessage
    // already validated.
    const auto read_optional_string = [&message](const char* key, std::string* destination) -> void {
        const auto it = message.fields.find(QString::fromLatin1(key));
        if (it != message.fields.end() && it->isString() && destination)
        {
            *destination = it->toString().toStdString();
        }
    };
    read_optional_string("view_name", &parsed.view_name);
    read_optional_string("file_name", &parsed.file_name);

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool viewCommitFromProtocolMessage(const ExtensionProtocolMessage& message, ViewCommitMessage* out_message,
                                   std::string* error)
{
    ViewCommitMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseRuntimeMessageType(message, "view.commit", error) ||
        !requireString(message.fields, "view_id", &parsed.view_id, error))
    {
        return false;
    }

    const auto read_optional_string = [&message](const char* key, std::string* destination) {
        const auto it = message.fields.find(QString::fromLatin1(key));
        if (it != message.fields.end() && it->isString())
        {
            *destination = it->toString().toStdString();
        }
    };
    read_optional_string("history_step_name", &parsed.history_step_name);
    read_optional_string("history_step_description", &parsed.history_step_description);

    const auto allow_resized_output_it = message.fields.find(QStringLiteral("allow_resized_output"));
    if (allow_resized_output_it != message.fields.end())
    {
        if (!allow_resized_output_it->isBool())
        {
            if (error)
            {
                *error = "Optional bool field is invalid: allow_resized_output";
            }
            return false;
        }
        parsed.allow_resized_output = allow_resized_output_it->toBool();
    }

    const auto params_it = message.fields.find(QStringLiteral("params"));
    if (params_it != message.fields.end())
    {
        if (!params_it->isMap())
        {
            if (error)
            {
                *error = "Optional map field is invalid: params";
            }
            return false;
        }
        parsed.params = params_it->toMap();
    }

    if (!readOptionalSharedBufferDescriptorList(message.fields, "result_buffers", &parsed.result_buffers, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool viewUpdatedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewUpdatedMessage* out_message,
                                    std::string* error)
{
    ViewUpdatedMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseViewDescribeLikeMessage(message, "view.updated", &parsed.view_id, &parsed.source_buffer,
                                      &parsed.auxiliary_buffers, &parsed.metadata, &parsed.display_settings, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool viewCreatedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewCreatedMessage* out_message,
                                    std::string* error)
{
    ViewCreatedMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseViewLifecycleMessage(message, "view.created", &parsed.view_id, &parsed.view_name, &parsed.file_name,
                                   &parsed.selector_tooltip, error))
    {
        return false;
    }

    auto read_optional_int = [&](const char* key, std::optional<int>* out_value) -> bool {
        const auto it = message.fields.find(QString::fromLatin1(key));
        if (it == message.fields.end())
        {
            return true;
        }
        if (!it->isInteger())
        {
            if (error)
            {
                *error = std::string("view.created.") + key + " must be an integer when present";
            }
            return false;
        }

        *out_value = static_cast<int>(it->toInteger());
        return true;
    };

    if (!read_optional_int("width", &parsed.width) || !read_optional_int("height", &parsed.height) ||
        !read_optional_int("channels", &parsed.channels))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool viewDestroyedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewDestroyedMessage* out_message,
                                      std::string* error)
{
    ViewDestroyedMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseRuntimeMessageType(message, "view.destroyed", error) ||
        !requireString(message.fields, "view_id", &parsed.view_id, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool viewRenamedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewRenamedMessage* out_message,
                                    std::string* error)
{
    ViewRenamedMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseViewLifecycleMessage(message, "view.renamed", &parsed.view_id, &parsed.view_name, &parsed.file_name,
                                   &parsed.selector_tooltip, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool themeDescribeFromProtocolMessage(const ExtensionProtocolMessage& message, ThemeDescribeMessage* out_message,
                                      std::string* error)
{
    ThemeDescribeMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseThemeMessage(message, "theme.describe", &parsed.theme_name, &parsed.tokens, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

bool themeChangedFromProtocolMessage(const ExtensionProtocolMessage& message, ThemeChangedMessage* out_message,
                                     std::string* error)
{
    ThemeChangedMessage parsed;
    parsed.session_id = message.session_id;
    parsed.request_id = message.request_id;
    if (!parseThemeMessage(message, "theme.changed", &parsed.theme_name, &parsed.tokens, error))
    {
        return false;
    }

    if (out_message)
    {
        *out_message = std::move(parsed);
    }
    return true;
}

QCborMap sharedBufferDescriptorToCborMap(const SharedBufferDescriptor& descriptor)
{
    QCborMap map;
    map[QStringLiteral("buffer_id")] = QString::fromStdString(descriptor.buffer_id);
    if (!descriptor.symbolic_name.empty())
    {
        map[QStringLiteral("symbolic_name")] = QString::fromStdString(descriptor.symbolic_name);
    }
    if (!descriptor.mapping_name.empty())
    {
        map[QStringLiteral("mapping_name")] = QString::fromStdString(descriptor.mapping_name);
    }
    if (!descriptor.mapped_file_path.empty())
    {
        map[QStringLiteral("mapped_file_path")] = QString::fromStdString(descriptor.mapped_file_path);
    }
    map[QStringLiteral("byte_size")] = static_cast<qint64>(descriptor.byte_size);
    map[QStringLiteral("width")] = descriptor.width;
    map[QStringLiteral("height")] = descriptor.height;
    map[QStringLiteral("channels")] = descriptor.channels;
    map[QStringLiteral("pixel_type")] = QString::fromLatin1(sharedBufferPixelTypeName(descriptor.pixel_type).data());
    map[QStringLiteral("pixel_format")] =
        QString::fromLatin1(sharedBufferPixelFormatName(descriptor.pixel_format).data());
    map[QStringLiteral("color_space")] = QString::fromLatin1(sharedBufferColorSpaceName(descriptor.color_space).data());
    map[QStringLiteral("row_stride")] = static_cast<qint64>(descriptor.row_stride);
    map[QStringLiteral("plane_stride")] = static_cast<qint64>(descriptor.plane_stride);
    map[QStringLiteral("access_mode")] = QString::fromLatin1(sharedBufferAccessModeName(descriptor.access_mode).data());
    map[QStringLiteral("buffer_role")] = QString::fromLatin1(sharedBufferRoleName(descriptor.buffer_role).data());
    if (descriptor.offset_x.has_value())
    {
        map[QStringLiteral("offset_x")] = *descriptor.offset_x;
    }
    if (descriptor.offset_y.has_value())
    {
        map[QStringLiteral("offset_y")] = *descriptor.offset_y;
    }
    if (descriptor.source_width.has_value())
    {
        map[QStringLiteral("source_width")] = *descriptor.source_width;
    }
    if (descriptor.source_height.has_value())
    {
        map[QStringLiteral("source_height")] = *descriptor.source_height;
    }
    if (!descriptor.metadata_sidecar.empty())
    {
        map[QStringLiteral("metadata_sidecar")] = QString::fromStdString(descriptor.metadata_sidecar);
    }
    return map;
}

bool sharedBufferDescriptorFromCborValue(const QCborValue& value, SharedBufferDescriptor* out_descriptor,
                                         std::string* error)
{
    if (!value.isMap())
    {
        if (error)
        {
            *error = "Shared buffer descriptor must be a map";
        }
        return false;
    }

    const auto map = value.toMap();
    SharedBufferDescriptor descriptor;
    if (!requireString(map, "buffer_id", &descriptor.buffer_id, error) ||
        !requireUInt64(map, "byte_size", &descriptor.byte_size, error) ||
        !requireInt(map, "width", &descriptor.width, error) || !requireInt(map, "height", &descriptor.height, error) ||
        !requireInt(map, "channels", &descriptor.channels, error) ||
        !requireUInt64(map, "row_stride", &descriptor.row_stride, error) ||
        !requireUInt64(map, "plane_stride", &descriptor.plane_stride, error))
    {
        return false;
    }

    const auto optional_string = [&map](const char* key, std::string* out_value, std::string* out_error) -> bool {
        const auto it = map.find(QString::fromLatin1(key));
        if (it == map.end())
        {
            if (out_value)
            {
                out_value->clear();
            }
            return true;
        }
        if (!it->isString())
        {
            if (out_error)
            {
                *out_error = std::string("Optional string field is invalid: ") + key;
            }
            return false;
        }
        if (out_value)
        {
            *out_value = it->toString().toStdString();
        }
        return true;
    };

    if (!optional_string("symbolic_name", &descriptor.symbolic_name, error) ||
        !optional_string("mapping_name", &descriptor.mapping_name, error) ||
        !optional_string("mapped_file_path", &descriptor.mapped_file_path, error) ||
        !optional_string("metadata_sidecar", &descriptor.metadata_sidecar, error) ||
        !readOptionalInt(map, "offset_x", &descriptor.offset_x, error) ||
        !readOptionalInt(map, "offset_y", &descriptor.offset_y, error) ||
        !readOptionalInt(map, "source_width", &descriptor.source_width, error) ||
        !readOptionalInt(map, "source_height", &descriptor.source_height, error))
    {
        return false;
    }

    std::string enum_value;
    if (!requireString(map, "pixel_type", &enum_value, error) ||
        !parseEnum(enum_value, kPixelTypes, &descriptor.pixel_type))
    {
        if (error && error->empty())
        {
            *error = "Unknown pixel_type value";
        }
        return false;
    }

    if (!requireString(map, "pixel_format", &enum_value, error) ||
        !parseEnum(enum_value, kPixelFormats, &descriptor.pixel_format))
    {
        if (error && error->empty())
        {
            *error = "Unknown pixel_format value";
        }
        return false;
    }

    if (!requireString(map, "color_space", &enum_value, error) ||
        !parseEnum(enum_value, kColorSpaces, &descriptor.color_space))
    {
        if (error && error->empty())
        {
            *error = "Unknown color_space value";
        }
        return false;
    }

    if (!requireString(map, "access_mode", &enum_value, error) ||
        !parseEnum(enum_value, kAccessModes, &descriptor.access_mode))
    {
        if (error && error->empty())
        {
            *error = "Unknown access_mode value";
        }
        return false;
    }

    if (!requireString(map, "buffer_role", &enum_value, error) ||
        !parseEnum(enum_value, kBufferRoles, &descriptor.buffer_role))
    {
        if (error && error->empty())
        {
            *error = "Unknown buffer_role value";
        }
        return false;
    }

    const auto validation = validateSharedBufferDescriptor(descriptor);
    if (!validation.ok())
    {
        if (error)
        {
            *error = validation.error;
        }
        return false;
    }

    if (out_descriptor)
    {
        *out_descriptor = std::move(descriptor);
    }
    return true;
}

SharedBufferDescriptorValidation validateSharedBufferDescriptor(const SharedBufferDescriptor& descriptor)
{
    SharedBufferDescriptorValidation validation;
    if (descriptor.buffer_id.empty())
    {
        validation.error = "Shared buffer descriptor requires a buffer_id";
        return validation;
    }
    if (descriptor.width <= 0 || descriptor.height <= 0 || descriptor.channels <= 0)
    {
        validation.error = "Shared buffer descriptor dimensions must be positive";
        return validation;
    }
    if (!pixelFormatMatchesChannels(descriptor))
    {
        validation.error = "Shared buffer descriptor channel count does not match its declared pixel_format";
        return validation;
    }

    const int sample_bytes = bytesPerSample(descriptor.pixel_type);
    if (sample_bytes <= 0)
    {
        validation.error = "Shared buffer descriptor uses an unsupported pixel type";
        return validation;
    }

    const auto expected_row_stride =
        static_cast<std::uint64_t>(descriptor.width) * static_cast<std::uint64_t>(sample_bytes);
    if (descriptor.row_stride != expected_row_stride)
    {
        validation.error = "Shared buffer descriptor row_stride does not match the v1 planar contract";
        return validation;
    }

    const auto expected_plane_stride = descriptor.row_stride * static_cast<std::uint64_t>(descriptor.height);
    if (descriptor.plane_stride != expected_plane_stride)
    {
        validation.error = "Shared buffer descriptor plane_stride does not match the v1 planar contract";
        return validation;
    }

    const auto minimum_byte_size = descriptor.plane_stride * static_cast<std::uint64_t>(descriptor.channels);
    if (descriptor.byte_size < minimum_byte_size)
    {
        validation.error = "Shared buffer descriptor byte_size is smaller than the declared image layout";
        return validation;
    }

    if (descriptor.offset_x.has_value() != descriptor.offset_y.has_value() ||
        descriptor.source_width.has_value() != descriptor.source_height.has_value())
    {
        validation.error = "Shared buffer descriptor sub-area fields must be provided as complete coordinate pairs";
        return validation;
    }

    if (descriptor.offset_x.has_value())
    {
        if (!descriptor.source_width.has_value() || !descriptor.source_height.has_value())
        {
            validation.error = "Shared buffer descriptor sub-area offsets require source dimensions";
            return validation;
        }
        if (*descriptor.offset_x < 0 || *descriptor.offset_y < 0 || *descriptor.source_width <= 0 ||
            *descriptor.source_height <= 0)
        {
            validation.error = "Shared buffer descriptor sub-area fields must be non-negative and bounded";
            return validation;
        }
        if (*descriptor.offset_x + descriptor.width > *descriptor.source_width ||
            *descriptor.offset_y + descriptor.height > *descriptor.source_height)
        {
            validation.error = "Shared buffer descriptor sub-area exceeds the declared source bounds";
            return validation;
        }
    }

    return validation;
}

} // namespace an::ui::extensions
