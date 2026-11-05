// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionSdkService.h"
#include "app/ExtensionCommon.h"

#include "controls/GlobalSettingsPersistence.h"

#include <AfterNightPlatform/Logging.h>

#include <QFile>
#include <QCborParserError>
#include <QCborArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <set>

namespace
{

constexpr std::string_view kEncodedExtensionSettingPrefix = "__an_setting_cbor_v1__:";
constexpr int kMaxExtensionSettingDepth = 64;

[[nodiscard]] QCborValue jsonValueToCborValue(const QJsonValue& value)
{
    if (value.isUndefined())
    {
        return QCborValue();
    }
    if (value.isNull())
    {
        return QCborValue(nullptr);
    }
    if (value.isBool())
    {
        return QCborValue(value.toBool());
    }
    if (value.isDouble())
    {
        const double numeric = value.toDouble();
        const qint64 integer = static_cast<qint64>(numeric);
        if (std::floor(numeric) == numeric && numeric >= static_cast<double>(std::numeric_limits<qint64>::min()) &&
            numeric <= static_cast<double>(std::numeric_limits<qint64>::max()))
        {
            return QCborValue(integer);
        }
        return QCborValue(numeric);
    }
    if (value.isString())
    {
        return QCborValue(value.toString());
    }
    if (value.isArray())
    {
        QCborArray array;
        for (const QJsonValue& entry : value.toArray())
        {
            array.push_back(jsonValueToCborValue(entry));
        }
        return QCborValue(array);
    }
    if (value.isObject())
    {
        QCborMap map;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            map.insert(it.key(), jsonValueToCborValue(it.value()));
        }
        return QCborValue(map);
    }

    return QCborValue();
}

[[nodiscard]] bool isSafeExtensionId(const std::string_view value) noexcept
{
    if (value.empty())
    {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](const char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) != 0 || ch == '.' || ch == '_' || ch == '-';
    });
}

[[nodiscard]] bool isSafeSettingSegment(const std::string_view value) noexcept
{
    if (value.empty() || value == "." || value == "..")
    {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](const char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) != 0 || ch == '.' || ch == '_' || ch == '-';
    });
}

[[nodiscard]] bool isSafeExtensionSettingKey(const std::string_view value) noexcept
{
    if (value.empty() || value.front() == '/' || value.back() == '/')
    {
        return false;
    }

    std::size_t start = 0;
    while (start < value.size())
    {
        const std::size_t end = value.find('/', start);
        const std::string_view segment =
            value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
        if (!isSafeSettingSegment(segment))
        {
            return false;
        }

        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }

    return true;
}

[[nodiscard]] bool isSafeWorkflowRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
    {
        return false;
    }

    for (const auto& part : path)
    {
        const std::string segment = part.generic_string();
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::filesystem::path weaklyCanonicalOrNormalized(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec)
    {
        return canonical;
    }
    return path.lexically_normal();
}

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdString(path.generic_string());
}

void logExtensionEventToMainLogger(const std::string_view extension_id, const std::string_view severity,
                                   const std::string_view component, const std::string_view message)
{
    if (!an::platform::isLoggingInitialized() || message.empty())
    {
        return;
    }

    std::string source = extension_id.empty() ? std::string("extension") : std::string(extension_id);
    if (!component.empty())
    {
        source += "/";
        source += component;
    }

    if (severity == "trace")
    {
        LOG_TRACE("Extension {}: {}", source, message);
    }
    else if (severity == "debug")
    {
        LOG_DEBUG("Extension {}: {}", source, message);
    }
    else if (severity == "warning")
    {
        LOG_WARNING("Extension {}: {}", source, message);
    }
    else if (severity == "error")
    {
        LOG_ERROR("Extension {}: {}", source, message);
    }
    else
    {
        LOG_INFO("Extension {}: {}", source, message);
    }
}

[[nodiscard]] bool buildExtensionSettingStoragePrefix(const std::string_view extension_id, QString* out_prefix,
                                                      std::string* error)
{
    if (!out_prefix)
    {
        if (error)
        {
            *error = "Extension setting storage-prefix output is null";
        }
        return false;
    }
    if (!isSafeExtensionId(extension_id))
    {
        if (error)
        {
            *error = "Extension setting uses an invalid extension id";
        }
        return false;
    }

    *out_prefix = QStringLiteral("extension/%1/").arg(QString::fromStdString(std::string(extension_id)));
    return true;
}

[[nodiscard]] std::string_view messageNamespace(const std::string_view message_type) noexcept
{
    const std::size_t separator = message_type.find('.');
    return separator == std::string_view::npos ? message_type : message_type.substr(0, separator);
}

void buildProtocolErrorResponse(const an::ui::extensions::ExtensionProtocolMessage& request,
                                const an::ui::extensions::ExtensionProtocolErrorDomain domain, const int local_code,
                                const std::string& message, const std::string& details,
                                an::ui::extensions::ExtensionProtocolMessage* response)
{
    if (!response)
    {
        return;
    }

    response->type = request.type + ".error";
    response->session_id = request.session_id;
    response->request_id = request.request_id;
    response->fields = QCborMap{};
    response->fields[QStringLiteral("error")] =
        an::ui::extensions::protocolErrorToCborMap(an::ui::extensions::ExtensionProtocolError{
            an::ui::extensions::makeProtocolErrorCode(domain, local_code), message, details});
}

[[nodiscard]] bool readRequiredStringField(const QCborMap& fields, const char* key, std::string* out_value,
                                           std::string* error)
{
    const auto it = fields.find(QString::fromLatin1(key));
    if (it == fields.end() || !it->isString())
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

[[nodiscard]] bool readOptionalStringField(const QCborMap& fields, const char* key, QString* out_value,
                                           std::string* error)
{
    const auto it = fields.find(QString::fromLatin1(key));
    if (it == fields.end())
    {
        if (out_value)
        {
            *out_value = {};
        }
        return true;
    }
    if (!it->isString())
    {
        if (error)
        {
            *error = std::string("Optional string field is invalid: ") + key;
        }
        return false;
    }

    if (out_value)
    {
        *out_value = it->toString();
    }
    return true;
}

[[nodiscard]] bool readOptionalBoolField(const QCborMap& fields, const char* key, const bool default_value,
                                         bool* out_value, std::string* error)
{
    if (!out_value)
    {
        if (error)
        {
            *error = std::string("Optional bool output is null: ") + key;
        }
        return false;
    }

    const auto it = fields.find(QString::fromLatin1(key));
    if (it == fields.end())
    {
        *out_value = default_value;
        return true;
    }
    if (!it->isBool())
    {
        if (error)
        {
            *error = std::string("Optional bool field is invalid: ") + key;
        }
        return false;
    }

    *out_value = it->toBool();
    return true;
}

bool readOptionalCborField(const QCborMap& fields, const char* key, QCborValue* out_value)
{
    if (!out_value)
    {
        return false;
    }

    const auto it = fields.find(QString::fromLatin1(key));
    *out_value = it == fields.end() ? QCborValue() : it.value();
    return true;
}

[[nodiscard]] std::string normalizeLowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    return value;
}

[[nodiscard]] bool isSupportedLogSeverity(const std::string_view severity) noexcept
{
    return severity == "trace" || severity == "debug" || severity == "info" || severity == "warning" ||
           severity == "error" || severity == "critical";
}

template <typename T> void appendBounded(std::vector<T>* values, T value)
{
    if (!values)
    {
        return;
    }

    constexpr std::size_t kMaxRetainedEntries = 128;
    values->push_back(std::move(value));
    if (values->size() > kMaxRetainedEntries)
    {
        values->erase(values->begin(),
                      values->begin() + static_cast<std::ptrdiff_t>(values->size() - kMaxRetainedEntries));
    }
}

constexpr int kMaxUiDescribeTreeDepth = 32;

struct UiDescribeValidationState
{
    int widget_count = 0;
    int current_depth = 0;
    QStringList widget_ids;
    std::set<std::string> seen_widget_ids;
};

[[nodiscard]] bool isSupportedUiDescribeWidgetType(const std::string_view widget_type) noexcept
{
    constexpr std::array<std::string_view, 14> kSupportedWidgetTypes = {
        "column",      "slider",     "spin_int",       "spin_double",         "checkbox",    "combo", "line_edit",
        "file_picker", "dir_picker", "section_header", "collapsible_section", "info_banner", "card",  "button_group",
    };
    constexpr std::string_view kTableWidgetType = "table";

    return std::find(kSupportedWidgetTypes.begin(), kSupportedWidgetTypes.end(), widget_type) !=
               kSupportedWidgetTypes.end() ||
           widget_type == kTableWidgetType;
}

bool validateUiDescribeNode(const QCborValue& node_value, UiDescribeValidationState* state, std::string* error)
{
    if (state && state->current_depth >= kMaxUiDescribeTreeDepth)
    {
        if (error)
        {
            *error = "ui.describe tree exceeds maximum nesting depth (" + std::to_string(kMaxUiDescribeTreeDepth) + ")";
        }
        return false;
    }

    if (!node_value.isMap())
    {
        if (error)
        {
            *error = "ui.describe tree nodes must be CBOR maps";
        }
        return false;
    }

    const QCborMap node = node_value.toMap();
    const auto widget_it = node.find(QStringLiteral("widget"));
    if (widget_it == node.end() || !widget_it->isString())
    {
        if (error)
        {
            *error = "ui.describe tree nodes must declare a string widget type";
        }
        return false;
    }

    const std::string widget_type = widget_it->toString().toStdString();
    if (!isSupportedUiDescribeWidgetType(widget_type))
    {
        if (error)
        {
            *error = "ui.describe contains an unsupported widget type: " + widget_type;
        }
        return false;
    }

    if (state)
    {
        state->widget_count += 1;
    }

    const auto id_it = node.find(QStringLiteral("id"));
    if (id_it != node.end())
    {
        if (!id_it->isString())
        {
            if (error)
            {
                *error = "ui.describe widget ids must be strings when present";
            }
            return false;
        }

        const QString widget_id = id_it->toString();
        if (widget_id.isEmpty())
        {
            if (error)
            {
                *error = "ui.describe widget ids must not be empty";
            }
            return false;
        }

        if (state)
        {
            const std::string widget_id_key = widget_id.toStdString();
            if (!state->seen_widget_ids.insert(widget_id_key).second)
            {
                if (error)
                {
                    *error = "ui.describe contains duplicate widget ids: " + widget_id_key;
                }
                return false;
            }
            state->widget_ids.push_back(widget_id);
        }
    }

    const auto children_it = node.find(QStringLiteral("children"));
    if (children_it == node.end())
    {
        return true;
    }
    if (!children_it->isArray())
    {
        if (error)
        {
            *error = "ui.describe children must be arrays when present";
        }
        return false;
    }

    const QCborArray children = children_it->toArray();
    if (state)
    {
        state->current_depth += 1;
    }
    for (const QCborValue& child_value : children)
    {
        if (!validateUiDescribeNode(child_value, state, error))
        {
            return false;
        }
    }
    if (state)
    {
        state->current_depth -= 1;
    }

    return true;
}

[[nodiscard]] bool validateExtensionSettingValue(const QCborValue& value, const int depth, std::string* error)
{
    if (depth > kMaxExtensionSettingDepth)
    {
        if (error)
        {
            *error = "Extension setting value exceeds maximum nesting depth";
        }
        return false;
    }

    if (value.isNull() || value.isBool() || value.isInteger() || value.isString())
    {
        return true;
    }
    if (value.isDouble())
    {
        if (!std::isfinite(value.toDouble()))
        {
            if (error)
            {
                *error = "Extension setting numbers must be finite";
            }
            return false;
        }
        return true;
    }
    if (value.isArray())
    {
        const QCborArray array = value.toArray();
        for (const QCborValue& entry : array)
        {
            if (!validateExtensionSettingValue(entry, depth + 1, error))
            {
                return false;
            }
        }
        return true;
    }
    if (value.isMap())
    {
        const QCborMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            if (!it.key().isString())
            {
                if (error)
                {
                    *error = "Extension setting objects require string keys";
                }
                return false;
            }
            if (!validateExtensionSettingValue(it.value(), depth + 1, error))
            {
                return false;
            }
        }
        return true;
    }

    if (error)
    {
        *error = "Extension settings must be JSON-serializable values";
    }
    return false;
}

[[nodiscard]] bool encodeExtensionSettingValue(const QCborValue& value, QString* out_storage_value, std::string* error)
{
    if (!out_storage_value)
    {
        if (error)
        {
            *error = "Extension setting storage output is null";
        }
        return false;
    }
    if (!validateExtensionSettingValue(value, 0, error))
    {
        return false;
    }

    const QString encoded_prefix = QString::fromLatin1(kEncodedExtensionSettingPrefix);
    if (value.isString())
    {
        const QString string_value = value.toString();
        if (!string_value.startsWith(encoded_prefix))
        {
            *out_storage_value = string_value;
            return true;
        }
    }

    const QByteArray cbor_payload = value.toCbor();
    *out_storage_value = encoded_prefix + QString::fromLatin1(cbor_payload.toBase64(QByteArray::Base64UrlEncoding |
                                                                                    QByteArray::OmitTrailingEquals));
    return true;
}

[[nodiscard]] bool decodeExtensionSettingValue(const QString& storage_value, QCborValue* out_value, std::string* error)
{
    if (!out_value)
    {
        if (error)
        {
            *error = "Extension setting value output is null";
        }
        return false;
    }

    const QString encoded_prefix = QString::fromLatin1(kEncodedExtensionSettingPrefix);
    if (!storage_value.startsWith(encoded_prefix))
    {
        *out_value = QCborValue(storage_value);
        return true;
    }

    const QByteArray encoded_payload = storage_value.mid(encoded_prefix.size()).toLatin1();
    if (encoded_payload.isEmpty())
    {
        if (error)
        {
            *error = "Encoded extension setting payload is empty";
        }
        return false;
    }

    const QByteArray cbor_payload = QByteArray::fromBase64(encoded_payload, QByteArray::Base64UrlEncoding);
    if (cbor_payload.isEmpty())
    {
        if (error)
        {
            *error = "Encoded extension setting payload is invalid base64";
        }
        return false;
    }

    QCborParserError parser_error;
    const QCborValue decoded_value = QCborValue::fromCbor(cbor_payload, &parser_error);
    if (parser_error.error != QCborError::NoError)
    {
        if (error)
        {
            *error = "Encoded extension setting payload is invalid CBOR";
        }
        return false;
    }
    if (!validateExtensionSettingValue(decoded_value, 0, error))
    {
        return false;
    }

    *out_value = decoded_value;
    return true;
}

bool validateUiDescribeTree(const QCborMap& tree, QString* out_root_widget, int* out_widget_count,
                            QStringList* out_widget_ids, std::string* error)
{
    const auto root_it = tree.find(QStringLiteral("root"));
    if (root_it == tree.end() || !root_it->isMap())
    {
        if (error)
        {
            *error = "ui.describe tree must contain a root widget map";
        }
        return false;
    }

    const QCborMap root = root_it->toMap();
    const auto root_widget_it = root.find(QStringLiteral("widget"));
    if (root_widget_it == root.end() || !root_widget_it->isString())
    {
        if (error)
        {
            *error = "ui.describe root must declare a string widget type";
        }
        return false;
    }

    UiDescribeValidationState state;
    if (!validateUiDescribeNode(root, &state, error))
    {
        return false;
    }

    if (out_root_widget)
    {
        *out_root_widget = root_widget_it->toString();
    }
    if (out_widget_count)
    {
        *out_widget_count = state.widget_count;
    }
    if (out_widget_ids)
    {
        *out_widget_ids = state.widget_ids;
    }
    return true;
}

bool validateUiPatchOperations(const QCborArray& patch_values,
                               const an::ui::extensions::ExtensionUiDescription& description,
                               std::vector<an::ui::extensions::ExtensionUiPatchOperation>* out_operations,
                               std::string* error)
{
    if (!out_operations)
    {
        if (error)
        {
            *error = "ui.patch output operation list is null";
        }
        return false;
    }

    std::set<std::string> known_widget_ids;
    for (const QString& widget_id : description.widget_ids)
    {
        known_widget_ids.insert(widget_id.toStdString());
    }

    out_operations->clear();
    out_operations->reserve(static_cast<std::size_t>(patch_values.size()));
    for (const QCborValue& patch_value : patch_values)
    {
        if (!patch_value.isMap())
        {
            if (error)
            {
                *error = "ui.patch entries must be CBOR maps";
            }
            return false;
        }

        const QCborMap patch_map = patch_value.toMap();
        const auto id_it = patch_map.find(QStringLiteral("id"));
        const auto prop_it = patch_map.find(QStringLiteral("prop"));
        const auto value_it = patch_map.find(QStringLiteral("value"));
        if (id_it == patch_map.end() || !id_it->isString())
        {
            if (error)
            {
                *error = "ui.patch entries must declare a string id";
            }
            return false;
        }
        if (prop_it == patch_map.end() || !prop_it->isString())
        {
            if (error)
            {
                *error = "ui.patch entries must declare a string prop";
            }
            return false;
        }
        if (value_it == patch_map.end())
        {
            if (error)
            {
                *error = "ui.patch entries must declare a value field";
            }
            return false;
        }

        const QString widget_id = id_it->toString();
        const QString property_name = prop_it->toString();
        if (widget_id.isEmpty())
        {
            if (error)
            {
                *error = "ui.patch widget ids must not be empty";
            }
            return false;
        }
        if (property_name.isEmpty())
        {
            if (error)
            {
                *error = "ui.patch property names must not be empty";
            }
            return false;
        }

        const std::string widget_id_key = widget_id.toStdString();
        if (!known_widget_ids.contains(widget_id_key))
        {
            if (error)
            {
                *error = "ui.patch references an unknown widget id: " + widget_id_key;
            }
            return false;
        }

        an::ui::extensions::ExtensionUiPatchOperation operation;
        operation.widget_id = widget_id;
        operation.property_name = property_name;
        operation.value = value_it.value();
        out_operations->push_back(std::move(operation));
    }

    return true;
}

QCborMap extensionViewStateToCborMap(const an::ui::extensions::ExtensionViewState& view_state)
{
    QCborMap map{
        {QStringLiteral("name"), QString::fromStdString(view_state.view_name)},
        {QStringLiteral("file_name"), QString::fromStdString(view_state.file_name)},
        {QStringLiteral("width"), view_state.width},
        {QStringLiteral("height"), view_state.height},
        {QStringLiteral("channels"), view_state.channels},
        {QStringLiteral("zoom_percent"), view_state.zoom_percent},
        {QStringLiteral("is_active"), view_state.is_active},
        {QStringLiteral("has_image"), view_state.has_image},
        {QStringLiteral("has_sequence"), view_state.has_sequence},
        {QStringLiteral("has_mask"), view_state.has_mask},
        {QStringLiteral("mask_enabled"), view_state.mask_enabled},
        {QStringLiteral("show_mask"), view_state.show_mask},
        {QStringLiteral("mask_inverted"), view_state.mask_inverted},
        {QStringLiteral("has_alternate_image"), view_state.has_alternate_image},
        {QStringLiteral("selection_area_enabled"), view_state.selection_area_enabled},
        {QStringLiteral("selection_area"), QCborMap{{QStringLiteral("x"), view_state.selection_area_x},
                                                    {QStringLiteral("y"), view_state.selection_area_y},
                                                    {QStringLiteral("width"), view_state.selection_area_width},
                                                    {QStringLiteral("height"), view_state.selection_area_height}}},
        {QStringLiteral("viewport_offset"), QCborMap{{QStringLiteral("x"), view_state.viewport_offset_x},
                                                     {QStringLiteral("y"), view_state.viewport_offset_y}}}};
    if (!view_state.selector_tooltip.empty())
    {
        map[QStringLiteral("selector_tooltip")] = QString::fromStdString(view_state.selector_tooltip);
    }
    if (!view_state.view_mode.empty())
    {
        map[QStringLiteral("view_mode")] = QString::fromStdString(view_state.view_mode);
    }
    if (!view_state.image_display_mode.empty())
    {
        map[QStringLiteral("image_display_mode")] = QString::fromStdString(view_state.image_display_mode);
    }
    if (!view_state.channel_view_mode.empty())
    {
        map[QStringLiteral("channel_view_mode")] = QString::fromStdString(view_state.channel_view_mode);
    }
    if (!view_state.mask_rendering_mode.empty())
    {
        map[QStringLiteral("mask_rendering_mode")] = QString::fromStdString(view_state.mask_rendering_mode);
    }
    return map;
}

QCborMap extensionViewDisplaySettingsToCborMap(const an::ui::extensions::ExtensionViewState& view_state)
{
    QCborMap display_settings{
        {QStringLiteral("zoom_percent"), view_state.zoom_percent},
        {QStringLiteral("viewport_offset"), QCborMap{{QStringLiteral("x"), view_state.viewport_offset_x},
                                                     {QStringLiteral("y"), view_state.viewport_offset_y}}},
        {QStringLiteral("has_mask"), view_state.has_mask},
        {QStringLiteral("mask_enabled"), view_state.mask_enabled},
        {QStringLiteral("show_mask"), view_state.show_mask},
        {QStringLiteral("mask_inverted"), view_state.mask_inverted},
        {QStringLiteral("has_alternate_image"), view_state.has_alternate_image},
        {QStringLiteral("has_sequence"), view_state.has_sequence},
        {QStringLiteral("selection_area_enabled"), view_state.selection_area_enabled},
        {QStringLiteral("selection_area"), QCborMap{{QStringLiteral("x"), view_state.selection_area_x},
                                                    {QStringLiteral("y"), view_state.selection_area_y},
                                                    {QStringLiteral("width"), view_state.selection_area_width},
                                                    {QStringLiteral("height"), view_state.selection_area_height}}}};
    if (!view_state.view_mode.empty())
    {
        display_settings[QStringLiteral("view_mode")] = QString::fromStdString(view_state.view_mode);
    }
    if (!view_state.image_display_mode.empty())
    {
        display_settings[QStringLiteral("image_display_mode")] = QString::fromStdString(view_state.image_display_mode);
    }
    if (!view_state.channel_view_mode.empty())
    {
        display_settings[QStringLiteral("channel_view_mode")] = QString::fromStdString(view_state.channel_view_mode);
    }
    if (!view_state.mask_rendering_mode.empty())
    {
        display_settings[QStringLiteral("mask_rendering_mode")] =
            QString::fromStdString(view_state.mask_rendering_mode);
    }
    return display_settings;
}

bool loadViewMetadataFromDescriptor(const an::ui::extensions::SharedBufferDescriptor& descriptor,
                                    QCborMap* out_metadata, std::string* error)
{
    if (!out_metadata)
    {
        if (error)
        {
            *error = "ExtensionSdkService view-metadata output is null";
        }
        return false;
    }

    out_metadata->clear();
    if (descriptor.metadata_sidecar.empty())
    {
        return true;
    }

    const std::filesystem::path sidecar_path = std::filesystem::path(descriptor.metadata_sidecar).lexically_normal();
    QFile file(QString::fromStdString(sidecar_path.string()));
    if (!file.exists())
    {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
        {
            *error = "Failed to open view metadata sidecar: " + sidecar_path.generic_string();
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
        {
            *error = "View metadata sidecar is not valid JSON: " + sidecar_path.generic_string();
        }
        return false;
    }

    QCborMap metadata;
    const QJsonObject root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        metadata.insert(it.key(), jsonValueToCborValue(it.value()));
    }

    *out_metadata = std::move(metadata);
    return true;
}

const an::ui::extensions::ExtensionViewState*
findActiveViewState(const std::vector<an::ui::extensions::ExtensionViewState>& view_states)
{
    const auto it = std::find_if(view_states.begin(), view_states.end(),
                                 [](const auto& view_state) { return view_state.is_active; });
    return it == view_states.end() ? nullptr : &(*it);
}

const an::ui::extensions::ExtensionViewState*
findViewStateByName(const std::vector<an::ui::extensions::ExtensionViewState>& view_states,
                    const std::string_view view_name)
{
    const auto it = std::find_if(view_states.begin(), view_states.end(),
                                 [view_name](const auto& view_state) { return view_state.view_name == view_name; });
    return it == view_states.end() ? nullptr : &(*it);
}

const an::ui::extensions::ExtensionViewState*
resolveViewState(const std::vector<an::ui::extensions::ExtensionViewState>& view_states,
                 const QString& requested_view_name, const bool has_explicit_view_name)
{
    if (has_explicit_view_name)
    {
        return findViewStateByName(view_states, requested_view_name.toStdString());
    }

    return findActiveViewState(view_states);
}

} // anonymous namespace

namespace an::ui::extensions
{

ExtensionSdkService::ExtensionSdkService(GlobalSettingsPersistence* global_settings_persistence)
    : global_settings_persistence_(global_settings_persistence)
{
}

ExtensionSdkService::WorkflowSessionState& ExtensionSdkService::workflowSessionState(const std::string_view session_id)
{
    const std::string key = session_id.empty() ? std::string("session") : std::string(session_id);
    auto [it, inserted] = workflow_sessions_.try_emplace(key);
    if (inserted || it->second.temp_dir.empty() || it->second.output_dir.empty())
    {
        const std::filesystem::path root = extensionHostSessionRootPath(key) / "workflow";
        it->second.temp_dir = root / "tmp";
        it->second.output_dir = root / "outputs";
        it->second.working_dir = it->second.output_dir;
    }
    return it->second;
}

bool ExtensionSdkService::cleanupWorkflowSessionState(const std::string_view session_id, const bool remove_outputs,
                                                      std::string* error)
{
    const std::string key = session_id.empty() ? std::string("session") : std::string(session_id);
    workflow_sessions_.erase(key);

    const std::filesystem::path workflow_root = extensionHostSessionRootPath(key) / "workflow";
    const std::filesystem::path cleanup_path = remove_outputs ? workflow_root : workflow_root / "tmp";
    std::error_code ec;
    std::filesystem::remove_all(cleanup_path, ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to clean workflow session artifacts: " + ec.message();
        }
        return false;
    }
    return true;
}

void ExtensionSdkService::setGlobalSettingsPersistence(GlobalSettingsPersistence* global_settings_persistence)
{
    global_settings_persistence_ = global_settings_persistence;
    persistence_initialized_ = false;
}

void ExtensionSdkService::setViewStateProvider(ExtensionSdkViewStateProvider view_state_provider)
{
    view_state_provider_ = std::move(view_state_provider);
}

void ExtensionSdkService::setViewSnapshotProvider(ExtensionSdkViewSnapshotProvider view_snapshot_provider)
{
    view_snapshot_provider_ = std::move(view_snapshot_provider);
}

void ExtensionSdkService::setViewBindingHandler(ExtensionSdkViewBindingHandler view_binding_handler)
{
    view_binding_handler_ = std::move(view_binding_handler);
}

void ExtensionSdkService::setThemeSnapshotProvider(ExtensionSdkThemeSnapshotProvider theme_snapshot_provider)
{
    theme_snapshot_provider_ = std::move(theme_snapshot_provider);
}

void ExtensionSdkService::setUiInteractionHandler(ExtensionSdkUiInteractionHandler ui_interaction_handler)
{
    ui_interaction_handler_ = std::move(ui_interaction_handler);
}

void ExtensionSdkService::setUiOpenImageHandler(ExtensionSdkUiOpenImageHandler ui_open_image_handler)
{
    ui_open_image_handler_ = std::move(ui_open_image_handler);
}

void ExtensionSdkService::setWorkflowOutputHandler(ExtensionWorkflowOutputHandler workflow_output_handler)
{
    workflow_output_handler_ = std::move(workflow_output_handler);
}

bool ExtensionSdkService::buildThemeChangedMessage(const std::string_view session_id, ThemeChangedMessage* out_message,
                                                   bool* out_has_message, std::string* error) const
{
    if (!out_message || !out_has_message)
    {
        if (error)
        {
            *error = "ExtensionSdkService changed-theme output is null";
        }
        return false;
    }

    *out_has_message = false;
    *out_message = ThemeChangedMessage{};
    if (!theme_snapshot_provider_)
    {
        return true;
    }

    const ExtensionThemeSnapshot snapshot = theme_snapshot_provider_();
    if (snapshot.theme_name.empty() && snapshot.tokens.isEmpty())
    {
        return true;
    }

    ThemeChangedMessage message;
    message.session_id = std::string(session_id);
    message.request_id = generateOpaqueToken();
    message.theme_name = snapshot.theme_name;
    message.tokens = snapshot.tokens;
    *out_message = std::move(message);
    *out_has_message = true;
    return true;
}

bool ExtensionSdkService::describeActiveViewState(ExtensionViewState* out_view_state, bool* out_has_view_state,
                                                  std::string* error) const
{
    if (!out_view_state || !out_has_view_state)
    {
        if (error)
        {
            *error = "ExtensionSdkService active-view output is null";
        }
        return false;
    }

    *out_has_view_state = false;
    *out_view_state = ExtensionViewState{};
    if (!view_state_provider_)
    {
        return true;
    }

    std::vector<ExtensionViewState> view_states;
    if (!collectViewStates(&view_states, error))
    {
        return false;
    }

    const ExtensionViewState* active_view = findActiveViewState(view_states);
    if (!active_view)
    {
        return true;
    }

    *out_view_state = *active_view;
    *out_has_view_state = true;
    return true;
}

bool ExtensionSdkService::describeViewState(const std::string_view view_name, ExtensionViewState* out_view_state,
                                            bool* out_has_view_state, std::string* error) const
{
    if (!out_view_state || !out_has_view_state)
    {
        if (error)
        {
            *error = "ExtensionSdkService view-state output is null";
        }
        return false;
    }

    *out_has_view_state = false;
    *out_view_state = ExtensionViewState{};
    if (!view_state_provider_ || view_name.empty())
    {
        return true;
    }

    std::vector<ExtensionViewState> view_states;
    if (!collectViewStates(&view_states, error))
    {
        return false;
    }

    const ExtensionViewState* resolved_view = findViewStateByName(view_states, view_name);
    if (!resolved_view)
    {
        return true;
    }

    *out_view_state = *resolved_view;
    *out_has_view_state = true;
    return true;
}

bool ExtensionSdkService::buildInitialThemeDescribeMessage(const std::string_view session_id,
                                                           ThemeDescribeMessage* out_message, bool* out_has_message,
                                                           std::string* error) const
{
    if (!out_message || !out_has_message)
    {
        if (error)
        {
            *error = "ExtensionSdkService initial theme output is null";
        }
        return false;
    }

    *out_has_message = false;
    *out_message = ThemeDescribeMessage{};
    if (!theme_snapshot_provider_)
    {
        return true;
    }

    const ExtensionThemeSnapshot snapshot = theme_snapshot_provider_();
    if (snapshot.theme_name.empty() && snapshot.tokens.isEmpty())
    {
        return true;
    }

    ThemeDescribeMessage message;
    message.session_id = std::string(session_id);
    message.request_id = generateOpaqueToken();
    message.theme_name = snapshot.theme_name;
    message.tokens = snapshot.tokens;
    *out_message = std::move(message);
    *out_has_message = true;
    return true;
}

bool ExtensionSdkService::buildInitialViewDescribeMessage(const std::string_view session_id,
                                                          ViewDescribeMessage* out_message, bool* out_has_message,
                                                          std::string* error) const
{
    return buildInitialViewDescribeMessage(session_id, {}, out_message, out_has_message, error);
}

bool ExtensionSdkService::buildInitialViewDescribeMessage(const std::string_view session_id,
                                                          const std::string_view preferred_view_name,
                                                          ViewDescribeMessage* out_message, bool* out_has_message,
                                                          std::string* error) const
{
    if (!out_message || !out_has_message)
    {
        if (error)
        {
            *error = "ExtensionSdkService initial view output is null";
        }
        return false;
    }

    *out_has_message = false;
    *out_message = ViewDescribeMessage{};
    ExtensionViewState initial_view;
    bool has_initial_view = false;
    const bool described_view = preferred_view_name.empty()
                                    ? describeActiveViewState(&initial_view, &has_initial_view, error)
                                    : describeViewState(preferred_view_name, &initial_view, &has_initial_view, error);
    if (!described_view)
    {
        return false;
    }

    if (!has_initial_view || !initial_view.has_image)
    {
        return true;
    }

    ViewDescribeMessage message;
    message.session_id = std::string(session_id);
    message.request_id = generateOpaqueToken();
    message.view_id = initial_view.view_name;
    message.view_name = initial_view.view_name;
    message.file_name = initial_view.file_name;
    message.display_settings = extensionViewDisplaySettingsToCborMap(initial_view);

    ExtensionViewSnapshot snapshot;
    if (!exportViewSnapshot(session_id, message.request_id, initial_view.view_name, &snapshot, error))
    {
        return false;
    }
    message.source_buffer = std::move(snapshot.source_buffer);
    message.auxiliary_buffers = std::move(snapshot.auxiliary_buffers);
    if (!loadViewMetadataFromDescriptor(message.source_buffer, &message.metadata, error))
    {
        return false;
    }

    *out_message = std::move(message);
    *out_has_message = true;
    return true;
}

bool ExtensionSdkService::buildViewUpdatedMessage(const std::string_view session_id, const std::string_view view_id,
                                                  const std::string_view view_name, ViewUpdatedMessage* out_message,
                                                  bool* out_has_message, std::string* error) const
{
    if (!out_message || !out_has_message)
    {
        if (error)
        {
            *error = "ExtensionSdkService updated-view output is null";
        }
        return false;
    }

    *out_has_message = false;
    *out_message = ViewUpdatedMessage{};

    ExtensionViewState resolved_view;
    bool has_resolved_view = false;
    if (!describeViewState(view_name, &resolved_view, &has_resolved_view, error))
    {
        return false;
    }

    if (!has_resolved_view || !resolved_view.has_image)
    {
        return true;
    }

    ViewUpdatedMessage message;
    message.session_id = std::string(session_id);
    message.request_id = generateOpaqueToken();
    message.view_id = view_id.empty() ? resolved_view.view_name : std::string(view_id);
    message.display_settings = extensionViewDisplaySettingsToCborMap(resolved_view);

    ExtensionViewSnapshot snapshot;
    if (!exportViewSnapshot(session_id, message.request_id, resolved_view.view_name, &snapshot, error))
    {
        return false;
    }
    message.source_buffer = std::move(snapshot.source_buffer);
    message.auxiliary_buffers = std::move(snapshot.auxiliary_buffers);
    if (!loadViewMetadataFromDescriptor(message.source_buffer, &message.metadata, error))
    {
        return false;
    }

    *out_message = std::move(message);
    *out_has_message = true;
    return true;
}

bool ExtensionSdkService::collectViewStates(std::vector<ExtensionViewState>* out_views, std::string* error) const
{
    if (!out_views)
    {
        if (error)
        {
            *error = "ExtensionSdkService view-state output is null";
        }
        return false;
    }
    if (!view_state_provider_)
    {
        if (error)
        {
            *error = "ExtensionSdkService has no view-state provider";
        }
        return false;
    }

    *out_views = view_state_provider_();
    if (std::any_of(out_views->begin(), out_views->end(),
                    [](const auto& view_state) { return view_state.view_name.empty(); }))
    {
        if (error)
        {
            *error = "ExtensionSdkService view-state provider returned a view with an empty name";
        }
        return false;
    }

    std::sort(out_views->begin(), out_views->end(),
              [](const auto& lhs, const auto& rhs) { return lhs.view_name < rhs.view_name; });
    return true;
}

bool ExtensionSdkService::exportViewSnapshot(const std::string_view session_id, const std::string_view request_id,
                                             const std::string_view view_name, ExtensionViewSnapshot* out_snapshot,
                                             std::string* error) const
{
    if (!out_snapshot)
    {
        if (error)
        {
            *error = "ExtensionSdkService snapshot output is null";
        }
        return false;
    }
    if (!view_snapshot_provider_)
    {
        if (error)
        {
            *error = "ExtensionSdkService has no view snapshot provider";
        }
        return false;
    }
    *out_snapshot = ExtensionViewSnapshot{};
    if (!view_snapshot_provider_(session_id, request_id, view_name, out_snapshot, error))
    {
        return false;
    }

    const auto validation = validateSharedBufferDescriptor(out_snapshot->source_buffer);
    if (!validation.ok())
    {
        if (error)
        {
            *error = validation.error;
        }
        return false;
    }

    for (const auto& descriptor : out_snapshot->auxiliary_buffers)
    {
        const auto auxiliary_validation = validateSharedBufferDescriptor(descriptor);
        if (!auxiliary_validation.ok())
        {
            if (error)
            {
                *error = auxiliary_validation.error;
            }
            return false;
        }
    }
    return true;
}

bool ExtensionSdkService::upsertExtensionSetting(const std::string_view extension_id,
                                                 const std::string_view setting_key, const QString& value,
                                                 std::string* error)
{
    return upsertExtensionSetting(extension_id, setting_key, QCborValue(value), error);
}

bool ExtensionSdkService::upsertExtensionSetting(const std::string_view extension_id,
                                                 const std::string_view setting_key, const QCborValue& value,
                                                 std::string* error)
{
    if (!ensurePersistenceReady(error))
    {
        return false;
    }

    QString storage_key;
    if (!buildExtensionSettingStorageKey(extension_id, setting_key, &storage_key, error))
    {
        return false;
    }

    QString storage_value;
    if (!encodeExtensionSettingValue(value, &storage_value, error))
    {
        return false;
    }

    if (!global_settings_persistence_->upsert(storage_key, storage_value))
    {
        if (error)
        {
            *error = "Failed to persist extension setting";
        }
        return false;
    }

    return true;
}

bool ExtensionSdkService::selectExtensionSetting(const std::string_view extension_id,
                                                 const std::string_view setting_key, QString* out_value,
                                                 const QString& default_value, std::string* error)
{
    if (!out_value)
    {
        if (error)
        {
            *error = "Extension setting output is null";
        }
        return false;
    }

    QCborValue value;
    if (!selectExtensionSetting(extension_id, setting_key, &value, QCborValue(default_value), error))
    {
        return false;
    }

    if (value.isNull())
    {
        out_value->clear();
        return true;
    }
    if (value.isString())
    {
        *out_value = value.toString();
        return true;
    }
    if (value.isBool())
    {
        *out_value = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        return true;
    }
    if (value.isInteger())
    {
        *out_value = QString::number(value.toInteger());
        return true;
    }
    if (value.isDouble())
    {
        *out_value = QString::number(value.toDouble(), 'g', 17);
        return true;
    }
    if (value.isArray() || value.isMap())
    {
        const QVariant variant = value.toVariant();
        const QJsonDocument document = value.isArray() ? QJsonDocument::fromVariant(variant.toList())
                                                       : QJsonDocument::fromVariant(variant.toMap());
        *out_value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
        return true;
    }

    *out_value = value.toVariant().toString();
    return true;
}

bool ExtensionSdkService::selectExtensionSetting(const std::string_view extension_id,
                                                 const std::string_view setting_key, QCborValue* out_value,
                                                 const QCborValue& default_value, std::string* error)
{
    if (!out_value)
    {
        if (error)
        {
            *error = "Extension setting output is null";
        }
        return false;
    }
    if (!ensurePersistenceReady(error))
    {
        return false;
    }

    QString storage_key;
    if (!buildExtensionSettingStorageKey(extension_id, setting_key, &storage_key, error))
    {
        return false;
    }

    const std::optional<QString> storage_value = global_settings_persistence_->selectOptional(storage_key);
    if (!storage_value.has_value())
    {
        *out_value = default_value;
        return true;
    }

    if (!decodeExtensionSettingValue(*storage_value, out_value, error))
    {
        return false;
    }
    return true;
}

bool ExtensionSdkService::removeExtensionSetting(const std::string_view extension_id,
                                                 const std::string_view setting_key, std::string* error)
{
    if (!ensurePersistenceReady(error))
    {
        return false;
    }

    QString storage_key;
    if (!buildExtensionSettingStorageKey(extension_id, setting_key, &storage_key, error))
    {
        return false;
    }

    if (!global_settings_persistence_->remove(storage_key))
    {
        if (error)
        {
            *error = "Failed to remove extension setting";
        }
        return false;
    }

    return true;
}

bool ExtensionSdkService::selectExtensionSettingKeys(const std::string_view extension_id, QStringList* out_keys,
                                                     std::string* error)
{
    if (!out_keys)
    {
        if (error)
        {
            *error = "Extension setting keys output is null";
        }
        return false;
    }
    if (!ensurePersistenceReady(error))
    {
        return false;
    }

    QString storage_prefix;
    if (!buildExtensionSettingStoragePrefix(extension_id, &storage_prefix, error))
    {
        return false;
    }

    const QStringList stored_keys = global_settings_persistence_->selectKeysWithPrefix(storage_prefix);
    out_keys->clear();
    out_keys->reserve(stored_keys.size());
    for (const QString& stored_key : stored_keys)
    {
        if (!stored_key.startsWith(storage_prefix))
        {
            continue;
        }

        const QString relative_key = stored_key.mid(storage_prefix.size());
        if (!relative_key.isEmpty())
        {
            out_keys->push_back(relative_key);
        }
    }

    return true;
}

bool ExtensionSdkService::dispatchProtocolMessage(const ExtensionProtocolMessage& request,
                                                  const std::string_view extension_id,
                                                  const ExtensionLaunchMode launch_mode,
                                                  ExtensionProtocolMessage* response, std::string* error)
{
    if (!response)
    {
        if (error)
        {
            *error = "ExtensionSdkService response output is null";
        }
        return false;
    }
    if (request.type.empty())
    {
        if (error)
        {
            *error = "ExtensionSdkService request type is empty";
        }
        return false;
    }
    if (request.request_id.empty())
    {
        if (error)
        {
            *error = "ExtensionSdkService request_id is required";
        }
        return false;
    }

    auto make_error_response = [&](const ExtensionProtocolErrorDomain domain, const int local_code,
                                   const std::string& message, const std::string& details = {}) {
        buildProtocolErrorResponse(request, domain, local_code, message, details, response);
    };

    const auto availability = resolveNamespaceAvailability(request.type, launch_mode);
    if (!availability.available)
    {
        make_error_response(ExtensionProtocolErrorDomain::Host, 60,
                            "Requested protocol service namespace is unavailable", availability.diagnostic);
        return true;
    }

    if (messageNamespace(request.type) != "settings")
    {
        auto handle_ui_notification = [&](const char* operation_name, const char* result_type,
                                          const ExtensionSdkUiNotificationHandler& handler,
                                          const QString& default_title, const int invalid_request_code,
                                          const int unavailable_code, const int failed_code) -> bool {
            std::string message;
            QString title;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "message", &message, &local_error) ||
                !readOptionalStringField(request.fields, "title", &title, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, invalid_request_code,
                                    std::string("Invalid ") + operation_name + " request", local_error);
                return true;
            }
            if (message.empty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, invalid_request_code,
                                    std::string("Invalid ") + operation_name + " request", "message must not be empty");
                return true;
            }
            if (!handler)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, unavailable_code,
                                    std::string(operation_name) + " is unavailable in this host session");
                return true;
            }

            if (title.isEmpty())
            {
                title = default_title;
            }

            if (!handler(request.session_id, request.request_id, title, QString::fromStdString(message), &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, failed_code,
                                    std::string("Failed to complete ") + operation_name, local_error);
                return true;
            }

            response->type = result_type;
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("accepted"), true}, {QStringLiteral("title"), title}};
            return true;
        };

        if (request.type == "logging.event")
        {
            std::string severity;
            std::string component;
            std::string message;
            QCborValue payload;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "severity", &severity, &local_error) ||
                !readRequiredStringField(request.fields, "component", &component, &local_error) ||
                !readRequiredStringField(request.fields, "message", &message, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Logging, 1, "Invalid logging.event request",
                                    local_error);
                return true;
            }

            severity = normalizeLowercase(std::move(severity));
            if (!isSupportedLogSeverity(severity))
            {
                make_error_response(ExtensionProtocolErrorDomain::Logging, 2, "Invalid logging.event severity",
                                    severity);
                return true;
            }

            readOptionalCborField(request.fields, "payload", &payload);

            ExtensionLogEvent event;
            event.extension_id = std::string(extension_id);
            event.session_id = request.session_id;
            event.request_id = request.request_id;
            event.severity = severity;
            event.component = component;
            event.message = message;
            event.payload = payload;
            appendBounded(&recent_log_events_, std::move(event));
            logExtensionEventToMainLogger(extension_id, severity, component, message);

            response->type = "logging.event.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("accepted"), true},
                                        {QStringLiteral("severity"), QString::fromStdString(severity)},
                                        {QStringLiteral("component"), QString::fromStdString(component)}};
            return true;
        }

        if (request.type == "diagnostics.checkpoint")
        {
            std::string name;
            QString detail;
            QCborValue fields;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "name", &name, &local_error) ||
                !readOptionalStringField(request.fields, "detail", &detail, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 61, "Invalid diagnostics.checkpoint request",
                                    local_error);
                return true;
            }

            readOptionalCborField(request.fields, "fields", &fields);

            ExtensionDiagnosticCheckpoint checkpoint;
            checkpoint.extension_id = std::string(extension_id);
            checkpoint.session_id = request.session_id;
            checkpoint.request_id = request.request_id;
            checkpoint.name = name;
            checkpoint.detail = detail.toStdString();
            checkpoint.fields = fields;
            appendBounded(&diagnostic_checkpoints_, std::move(checkpoint));

            response->type = "diagnostics.checkpoint.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("accepted"), true}, {QStringLiteral("name"), QString::fromStdString(name)}};
            return true;
        }

        if (request.type == "workflow.session_info")
        {
            response->type = "workflow.session_info.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("session_id"), QString::fromStdString(request.session_id)},
                         {QStringLiteral("extension_id"), QString::fromStdString(std::string(extension_id))},
                         {QStringLiteral("launch_mode"), QStringLiteral("workflow")}};
            return true;
        }

        if (request.type == "workflow.get_working_dir")
        {
            auto& state = workflowSessionState(request.session_id);
            std::error_code ec;
            std::filesystem::create_directories(state.working_dir, ec);
            if (ec)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 90,
                                    "Failed to create workflow working directory", ec.message());
                return true;
            }

            response->type = "workflow.get_working_dir.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("path"), pathToQString(state.working_dir)}};
            return true;
        }

        if (request.type == "workflow.set_working_dir")
        {
            std::string path_text;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "path", &path_text, &local_error) || path_text.empty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 91, "Invalid workflow.set_working_dir request",
                                    local_error.empty() ? "path must not be empty" : local_error);
                return true;
            }

            const std::filesystem::path requested_path(path_text);
            std::error_code ec;
            if (!std::filesystem::exists(requested_path, ec) || !std::filesystem::is_directory(requested_path, ec))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 92, "Workflow working directory does not exist",
                                    requested_path.generic_string());
                return true;
            }

            auto& state = workflowSessionState(request.session_id);
            state.working_dir = weaklyCanonicalOrNormalized(requested_path);

            response->type = "workflow.set_working_dir.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("path"), pathToQString(state.working_dir)}};
            return true;
        }

        if (request.type == "workflow.temp_dir")
        {
            auto& state = workflowSessionState(request.session_id);
            std::error_code ec;
            std::filesystem::create_directories(state.temp_dir, ec);
            if (ec)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 93, "Failed to create workflow temp directory",
                                    ec.message());
                return true;
            }

            response->type = "workflow.temp_dir.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("path"), pathToQString(state.temp_dir)}};
            return true;
        }

        if (request.type == "workflow.output_path")
        {
            std::string name;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "name", &name, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 94, "Invalid workflow.output_path request",
                                    local_error);
                return true;
            }

            const std::filesystem::path relative_name(name);
            if (!isSafeWorkflowRelativePath(relative_name))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 95, "Invalid workflow output path",
                                    "name must be a safe relative path");
                return true;
            }

            auto& state = workflowSessionState(request.session_id);
            const std::filesystem::path requested_output_path = (state.output_dir / relative_name).lexically_normal();
            std::error_code ec;
            std::filesystem::create_directories(requested_output_path.parent_path(), ec);
            if (ec)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 96,
                                    "Failed to create workflow output directory", ec.message());
                return true;
            }
            const std::filesystem::path output_path = weaklyCanonicalOrNormalized(requested_output_path);

            response->type = "workflow.output_path.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("path"), pathToQString(output_path)}};
            return true;
        }

        if (request.type == "workflow.get_sequences")
        {
            response->type = "workflow.get_sequences.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("sequences"), QCborArray{}}, {QStringLiteral("sequence_count"), 0}};
            return true;
        }

        if (request.type == "workflow.open_result_sequence")
        {
            std::string path_text;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "path", &path_text, &local_error) || path_text.empty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 97,
                                    "Invalid workflow.open_result_sequence request",
                                    local_error.empty() ? "path must not be empty" : local_error);
                return true;
            }

            auto& state = workflowSessionState(request.session_id);
            std::filesystem::path result_path(path_text);
            if (result_path.is_relative())
            {
                result_path = state.working_dir / result_path;
            }
            result_path = weaklyCanonicalOrNormalized(result_path);

            ExtensionWorkflowOutput output;
            output.extension_id = std::string(extension_id);
            output.session_id = request.session_id;
            output.request_id = request.request_id;
            output.kind = "sequence";
            output.path = result_path;
            appendBounded(&workflow_outputs_, std::move(output));

            response->type = "workflow.open_result_sequence.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("accepted"), true}, {QStringLiteral("path"), pathToQString(result_path)}};
            return true;
        }

        if (request.type == "ui.describe")
        {
            const auto tree_it = request.fields.find(QStringLiteral("tree"));
            if (tree_it == request.fields.end() || !tree_it->isMap())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 79, "Invalid ui.describe request",
                                    "tree must be a CBOR map");
                return true;
            }

            QString root_widget;
            int widget_count = 0;
            QStringList widget_ids;
            bool supports_ui_events = true;
            bool supports_ui_validation = true;
            std::string local_error;
            const QCborMap tree = tree_it->toMap();
            if (!validateUiDescribeTree(tree, &root_widget, &widget_count, &widget_ids, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 80, "Invalid ui.describe tree", local_error);
                return true;
            }
            if (!readOptionalBoolField(request.fields, "supports_ui_events", true, &supports_ui_events, &local_error) ||
                !readOptionalBoolField(request.fields, "supports_ui_validation", true, &supports_ui_validation,
                                       &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 80, "Invalid ui.describe tree", local_error);
                return true;
            }

            ExtensionUiDescription description;
            description.extension_id = std::string(extension_id);
            description.session_id = request.session_id;
            description.request_id = request.request_id;
            description.root_widget = root_widget;
            description.widget_count = widget_count;
            description.widget_ids = widget_ids;
            description.supports_ui_events = supports_ui_events;
            description.supports_ui_validation = supports_ui_validation;
            description.tree = tree;
            latest_ui_description_ = std::move(description);
            latest_ui_patch_state_.reset();

            QCborArray widget_ids_array;
            for (const QString& widget_id : widget_ids)
            {
                widget_ids_array.push_back(widget_id);
            }

            response->type = "ui.describe.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("accepted"), true},
                                        {QStringLiteral("root_widget"), root_widget},
                                        {QStringLiteral("widget_count"), widget_count},
                                        {QStringLiteral("widget_ids"), widget_ids_array}};
            return true;
        }

        if (request.type == "ui.patch")
        {
            const auto patches_it = request.fields.find(QStringLiteral("patches"));
            if (patches_it == request.fields.end() || !patches_it->isArray())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 81, "Invalid ui.patch request",
                                    "patches must be a CBOR array");
                return true;
            }
            if (!latest_ui_description_.has_value() || latest_ui_description_->extension_id != extension_id ||
                latest_ui_description_->session_id != request.session_id)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 82,
                                    "ui.patch requires a prior ui.describe for the active session");
                return true;
            }
            if (!latest_ui_description_->supports_ui_events)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 84,
                                    "ui.patch is not supported for the active session",
                                    "active ui.describe declares supports_ui_events=false");
                return true;
            }

            std::vector<ExtensionUiPatchOperation> operations;
            std::string local_error;
            const QCborArray patch_values = patches_it->toArray();
            if (!validateUiPatchOperations(patch_values, *latest_ui_description_, &operations, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 83, "Invalid ui.patch request", local_error);
                return true;
            }

            ExtensionUiPatchState patch_state;
            patch_state.extension_id = std::string(extension_id);
            patch_state.session_id = request.session_id;
            patch_state.request_id = request.request_id;
            patch_state.patch_count = static_cast<int>(operations.size());
            patch_state.operations = operations;
            latest_ui_patch_state_ = std::move(patch_state);

            QCborArray widget_ids_array;
            for (const auto& operation : operations)
            {
                widget_ids_array.push_back(operation.widget_id);
            }

            response->type = "ui.patch.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("accepted"), true},
                                        {QStringLiteral("patch_count"), static_cast<qint64>(operations.size())},
                                        {QStringLiteral("widget_ids"), widget_ids_array}};
            return true;
        }

        if (request.type == "ui.show_error")
        {
            return handle_ui_notification("ui.show_error", "ui.show_error.result", ui_interaction_handler_.show_error,
                                          QStringLiteral("Extension Error"), 67, 68, 69);
        }

        if (request.type == "ui.show_warning")
        {
            return handle_ui_notification("ui.show_warning", "ui.show_warning.result",
                                          ui_interaction_handler_.show_warning, QStringLiteral("Extension Warning"), 70,
                                          71, 72);
        }

        if (request.type == "ui.show_info")
        {
            return handle_ui_notification("ui.show_info", "ui.show_info.result", ui_interaction_handler_.show_info,
                                          QStringLiteral("Extension Message"), 73, 74, 75);
        }

        if (request.type == "ui.confirm")
        {
            std::string message;
            QString title;
            bool warn = false;
            std::string local_error;
            if (!readRequiredStringField(request.fields, "message", &message, &local_error) ||
                !readOptionalStringField(request.fields, "title", &title, &local_error) ||
                !readOptionalBoolField(request.fields, "warn", false, &warn, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 76, "Invalid ui.confirm request", local_error);
                return true;
            }
            if (message.empty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 76, "Invalid ui.confirm request",
                                    "message must not be empty");
                return true;
            }
            if (!ui_interaction_handler_.confirm)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 77,
                                    "ui.confirm is unavailable in this host session");
                return true;
            }

            if (title.isEmpty())
            {
                title = QStringLiteral("Extension Confirmation");
            }

            const auto completion_consumed = std::make_shared<bool>(false);
            if (!ui_interaction_handler_.confirm(
                    request.session_id, request.request_id, title, QString::fromStdString(message), warn,
                    [this, request, title, warn, completion_consumed](const bool confirmed,
                                                                      const std::string& completion_error) {
                        if (*completion_consumed)
                        {
                            return;
                        }
                        *completion_consumed = true;

                        ExtensionProtocolMessage deferred_response;
                        if (!completion_error.empty())
                        {
                            buildProtocolErrorResponse(request, ExtensionProtocolErrorDomain::Host, 78,
                                                       "Failed to complete ui.confirm", completion_error,
                                                       &deferred_response);
                        }
                        else
                        {
                            deferred_response.type = "ui.confirm.result";
                            deferred_response.session_id = request.session_id;
                            deferred_response.request_id = request.request_id;
                            deferred_response.fields = QCborMap{{QStringLiteral("confirmed"), confirmed},
                                                                {QStringLiteral("title"), title},
                                                                {QStringLiteral("warn"), warn}};
                        }

                        enqueueDeferredProtocolResponse(request.session_id, std::move(deferred_response));
                    },
                    &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 78, "Failed to complete ui.confirm",
                                    local_error);
                return true;
            }

            return true;
        }

        if (request.type == "ui.open_extension_settings")
        {
            std::string local_error;
            if (!ui_interaction_handler_.open_extension_settings)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 90,
                                    "ui.open_extension_settings is unavailable in this host session");
                return true;
            }

            const auto completion_consumed = std::make_shared<bool>(false);
            if (!ui_interaction_handler_.open_extension_settings(
                    request.session_id, request.request_id, extension_id,
                    [this, request, completion_consumed](const std::string& completion_error) {
                        if (*completion_consumed)
                        {
                            return;
                        }
                        *completion_consumed = true;

                        ExtensionProtocolMessage deferred_response;
                        if (!completion_error.empty())
                        {
                            buildProtocolErrorResponse(request, ExtensionProtocolErrorDomain::Host, 91,
                                                       "Failed to open extension settings", completion_error,
                                                       &deferred_response);
                        }
                        else
                        {
                            deferred_response.type = "ui.open_extension_settings.result";
                            deferred_response.session_id = request.session_id;
                            deferred_response.request_id = request.request_id;
                            deferred_response.fields = QCborMap{{QStringLiteral("accepted"), true}};
                        }

                        enqueueDeferredProtocolResponse(request.session_id, std::move(deferred_response));
                    },
                    &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 91, "Failed to open extension settings",
                                    local_error);
                return true;
            }

            return true;
        }

        if (request.type == "ui.open_image")
        {
            QString title;
            std::string local_error;
            if (!readOptionalStringField(request.fields, "title", &title, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 79, "Invalid ui.open_image request",
                                    local_error);
                return true;
            }

            SharedBufferDescriptor buffer;
            if (!sharedBufferDescriptorFromCborValue(request.fields.value(QStringLiteral("buffer")), &buffer,
                                                     &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 79, "Invalid ui.open_image request",
                                    local_error.empty() ? std::string("buffer descriptor is required") : local_error);
                return true;
            }

            if (!ui_open_image_handler_)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 80,
                                    "ui.open_image is unavailable in this host session", request.session_id);
                return true;
            }

            if (!ui_open_image_handler_(request.session_id, request.request_id, buffer, title, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 81, "Failed to complete ui.open_image",
                                    local_error);
                return true;
            }

            response->type = "ui.open_image.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("accepted"), true},
                         {QStringLiteral("title"), title.isEmpty() ? QStringLiteral("Extension Image") : title}};
            return true;
        }

        if (request.type == "views.describe")
        {
            std::vector<ExtensionViewState> view_states;
            std::string local_error;
            if (!collectViewStates(&view_states, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 62, "Failed to query view state", local_error);
                return true;
            }

            QCborArray views_array;
            for (const auto& view_state : view_states)
            {
                views_array.push_back(extensionViewStateToCborMap(view_state));
            }

            const ExtensionViewState* active_view = findActiveViewState(view_states);
            response->type = "views.describe.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("views"), views_array},
                                        {QStringLiteral("view_count"), static_cast<qint64>(view_states.size())},
                                        {QStringLiteral("has_active_image"), active_view && active_view->has_image}};
            if (active_view)
            {
                response->fields[QStringLiteral("active_view_name")] = QString::fromStdString(active_view->view_name);
                response->fields[QStringLiteral("active_file_name")] = QString::fromStdString(active_view->file_name);
            }
            return true;
        }

        if (request.type == "views.has_active_image")
        {
            std::vector<ExtensionViewState> view_states;
            std::string local_error;
            if (!collectViewStates(&view_states, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 62, "Failed to query view state", local_error);
                return true;
            }

            const ExtensionViewState* active_view = findActiveViewState(view_states);
            response->type = "views.has_active_image.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("has_active_image"), active_view && active_view->has_image}};
            if (active_view)
            {
                response->fields[QStringLiteral("active_view_name")] = QString::fromStdString(active_view->view_name);
            }
            return true;
        }

        if (request.type == "views.get_filename")
        {
            std::vector<ExtensionViewState> view_states;
            std::string local_error;
            if (!collectViewStates(&view_states, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 62, "Failed to query view state", local_error);
                return true;
            }

            const auto view_name_it = request.fields.find(QStringLiteral("view_name"));
            const bool has_explicit_view_name = view_name_it != request.fields.end();
            QString requested_view_name;
            if (!readOptionalStringField(request.fields, "view_name", &requested_view_name, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 63, "Invalid views.get_filename request",
                                    local_error);
                return true;
            }
            if (has_explicit_view_name && requested_view_name.isEmpty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 63, "Invalid views.get_filename request",
                                    "view_name must not be empty when provided");
                return true;
            }

            const ExtensionViewState* resolved_view =
                resolveViewState(view_states, requested_view_name, has_explicit_view_name);
            if (has_explicit_view_name && !resolved_view)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 64, "Requested view does not exist",
                                    requested_view_name.toStdString());
                return true;
            }

            response->type = "views.get_filename.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields = QCborMap{{QStringLiteral("resolved"), resolved_view != nullptr}};
            if (resolved_view)
            {
                response->fields[QStringLiteral("view_name")] = QString::fromStdString(resolved_view->view_name);
                response->fields[QStringLiteral("file_name")] = QString::fromStdString(resolved_view->file_name);
                response->fields[QStringLiteral("has_image")] = resolved_view->has_image;
            }
            return true;
        }

        if (request.type == "views.snapshot")
        {
            std::vector<ExtensionViewState> view_states;
            std::string local_error;
            if (!collectViewStates(&view_states, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 62, "Failed to query view state", local_error);
                return true;
            }

            const auto view_name_it = request.fields.find(QStringLiteral("view_name"));
            const bool has_explicit_view_name = view_name_it != request.fields.end();
            QString requested_view_name;
            if (!readOptionalStringField(request.fields, "view_name", &requested_view_name, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 63, "Invalid views.snapshot request",
                                    local_error);
                return true;
            }
            if (has_explicit_view_name && requested_view_name.isEmpty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 63, "Invalid views.snapshot request",
                                    "view_name must not be empty when provided");
                return true;
            }

            const ExtensionViewState* resolved_view =
                resolveViewState(view_states, requested_view_name, has_explicit_view_name);
            if (!resolved_view)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 64,
                                    has_explicit_view_name ? "Requested view does not exist"
                                                           : "Active view is unavailable",
                                    has_explicit_view_name ? requested_view_name.toStdString() : std::string());
                return true;
            }
            if (!resolved_view->has_image)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 65, "Requested view has no exportable image",
                                    resolved_view->view_name);
                return true;
            }

            ExtensionViewSnapshot snapshot;
            if (!exportViewSnapshot(request.session_id, request.request_id, resolved_view->view_name, &snapshot,
                                    &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 66, "Failed to export view snapshot",
                                    local_error);
                return true;
            }

            QCborMap metadata;
            if (!loadViewMetadataFromDescriptor(snapshot.source_buffer, &metadata, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 67, "Failed to load exported view metadata",
                                    local_error);
                return true;
            }

            response->type = "views.snapshot.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("view_name"), QString::fromStdString(resolved_view->view_name)},
                         {QStringLiteral("buffer"), sharedBufferDescriptorToCborMap(snapshot.source_buffer)}};
            if (!snapshot.auxiliary_buffers.empty())
            {
                QCborArray auxiliary_buffers;
                for (const auto& descriptor : snapshot.auxiliary_buffers)
                {
                    auxiliary_buffers.push_back(sharedBufferDescriptorToCborMap(descriptor));
                }
                response->fields[QStringLiteral("auxiliary_buffers")] = auxiliary_buffers;
            }
            if (!metadata.isEmpty())
            {
                response->fields[QStringLiteral("metadata")] = metadata;
            }
            if (!resolved_view->file_name.empty())
            {
                response->fields[QStringLiteral("file_name")] = QString::fromStdString(resolved_view->file_name);
            }
            return true;
        }

        if (request.type == "views.bind")
        {
            std::string local_error;
            std::string requested_view_name;
            if (!readRequiredStringField(request.fields, "view_name", &requested_view_name, &local_error) ||
                requested_view_name.empty())
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 68, "Invalid views.bind request",
                                    local_error.empty() ? std::string("view_name is required") : local_error);
                return true;
            }

            std::vector<ExtensionViewState> view_states;
            if (!collectViewStates(&view_states, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 62, "Failed to query view state", local_error);
                return true;
            }

            const ExtensionViewState* resolved_view =
                resolveViewState(view_states, QString::fromStdString(requested_view_name), true);
            if (!resolved_view)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 64, "Requested view does not exist",
                                    requested_view_name);
                return true;
            }
            if (!resolved_view->has_image)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 65, "Requested view has no exportable image",
                                    resolved_view->view_name);
                return true;
            }

            ExtensionViewSnapshot snapshot;
            if (!exportViewSnapshot(request.session_id, request.request_id, resolved_view->view_name, &snapshot,
                                    &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 66, "Failed to export view snapshot",
                                    local_error);
                return true;
            }

            QCborMap metadata;
            if (!loadViewMetadataFromDescriptor(snapshot.source_buffer, &metadata, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 67, "Failed to load exported view metadata",
                                    local_error);
                return true;
            }

            if (!view_binding_handler_)
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 69,
                                    "View binding is unavailable for this session", request.session_id);
                return true;
            }
            if (!view_binding_handler_(request.session_id, resolved_view->view_name, &local_error))
            {
                make_error_response(ExtensionProtocolErrorDomain::Host, 70, "Failed to bind the requested view",
                                    local_error);
                return true;
            }

            response->type = "views.bind.result";
            response->session_id = request.session_id;
            response->request_id = request.request_id;
            response->fields =
                QCborMap{{QStringLiteral("view_id"), QString::fromStdString(resolved_view->view_name)},
                         {QStringLiteral("view_name"), QString::fromStdString(resolved_view->view_name)},
                         {QStringLiteral("buffer"), sharedBufferDescriptorToCborMap(snapshot.source_buffer)},
                         {QStringLiteral("display_settings"), extensionViewDisplaySettingsToCborMap(*resolved_view)}};
            if (!snapshot.auxiliary_buffers.empty())
            {
                QCborArray auxiliary_buffers;
                for (const auto& descriptor : snapshot.auxiliary_buffers)
                {
                    auxiliary_buffers.push_back(sharedBufferDescriptorToCborMap(descriptor));
                }
                response->fields[QStringLiteral("auxiliary_buffers")] = auxiliary_buffers;
            }
            if (!metadata.isEmpty())
            {
                response->fields[QStringLiteral("metadata")] = metadata;
            }
            if (!resolved_view->file_name.empty())
            {
                response->fields[QStringLiteral("file_name")] = QString::fromStdString(resolved_view->file_name);
            }
            return true;
        }

        make_error_response(ExtensionProtocolErrorDomain::Host, 61,
                            "Requested protocol service namespace is not implemented", request.type);
        return true;
    }

    if (request.type == "settings.get")
    {
        std::string key;
        QCborValue default_value;
        std::string local_error;
        if (!readRequiredStringField(request.fields, "key", &key, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 1, "Invalid settings.get request", local_error);
            return true;
        }

        readOptionalCborField(request.fields, "default_value", &default_value);
        if (default_value.isUndefined())
        {
            default_value = QCborValue(nullptr);
        }

        QCborValue value;
        if (!selectExtensionSetting(extension_id, key, &value, default_value, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 2, "Failed to read extension setting",
                                local_error);
            return true;
        }

        response->type = "settings.get.result";
        response->session_id = request.session_id;
        response->request_id = request.request_id;
        response->fields =
            QCborMap{{QStringLiteral("key"), QString::fromStdString(key)}, {QStringLiteral("value"), value}};
        return true;
    }

    if (request.type == "settings.set")
    {
        std::string key;
        std::string local_error;
        if (!readRequiredStringField(request.fields, "key", &key, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 3, "Invalid settings.set request", local_error);
            return true;
        }

        const auto value_it = request.fields.find(QStringLiteral("value"));
        if (value_it == request.fields.end())
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 3, "Invalid settings.set request",
                                "Missing value field");
            return true;
        }
        const QCborValue value = value_it.value();

        if (!upsertExtensionSetting(extension_id, key, value, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 4, "Failed to write extension setting",
                                local_error);
            return true;
        }

        response->type = "settings.set.result";
        response->session_id = request.session_id;
        response->request_id = request.request_id;
        response->fields = QCborMap{{QStringLiteral("key"), QString::fromStdString(key)}};
        return true;
    }

    if (request.type == "settings.remove")
    {
        std::string key;
        std::string local_error;
        if (!readRequiredStringField(request.fields, "key", &key, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 5, "Invalid settings.remove request",
                                local_error);
            return true;
        }

        if (!removeExtensionSetting(extension_id, key, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 6, "Failed to remove extension setting",
                                local_error);
            return true;
        }

        response->type = "settings.remove.result";
        response->session_id = request.session_id;
        response->request_id = request.request_id;
        response->fields =
            QCborMap{{QStringLiteral("key"), QString::fromStdString(key)}, {QStringLiteral("removed"), true}};
        return true;
    }

    if (request.type == "settings.keys")
    {
        QStringList keys;
        std::string local_error;
        if (!selectExtensionSettingKeys(extension_id, &keys, &local_error))
        {
            make_error_response(ExtensionProtocolErrorDomain::Settings, 7, "Failed to enumerate extension settings",
                                local_error);
            return true;
        }

        QCborArray key_array;
        for (const QString& key : keys)
        {
            key_array.push_back(key);
        }

        response->type = "settings.keys.result";
        response->session_id = request.session_id;
        response->request_id = request.request_id;
        response->fields = QCborMap{{QStringLiteral("keys"), key_array}};
        return true;
    }

    make_error_response(ExtensionProtocolErrorDomain::Settings, 8, "Requested settings.* operation is not implemented",
                        request.type);
    return true;
}

std::vector<ExtensionProtocolMessage>
ExtensionSdkService::takeDeferredProtocolResponses(const std::string_view session_id)
{
    std::vector<ExtensionProtocolMessage> responses;
    const auto new_end = std::remove_if(deferred_protocol_responses_.begin(), deferred_protocol_responses_.end(),
                                        [&](DeferredProtocolResponse& deferred) {
                                            if (deferred.session_id != session_id)
                                            {
                                                return false;
                                            }

                                            responses.push_back(std::move(deferred.message));
                                            return true;
                                        });
    deferred_protocol_responses_.erase(new_end, deferred_protocol_responses_.end());
    return responses;
}

const std::vector<ExtensionLogEvent>& ExtensionSdkService::recentLogEvents() const noexcept
{
    return recent_log_events_;
}

const std::vector<ExtensionDiagnosticCheckpoint>& ExtensionSdkService::diagnosticCheckpoints() const noexcept
{
    return diagnostic_checkpoints_;
}

const std::vector<ExtensionWorkflowOutput>& ExtensionSdkService::workflowOutputs() const noexcept
{
    return workflow_outputs_;
}

bool ExtensionSdkService::presentWorkflowOutputs(const std::string_view session_id, std::string* error)
{
    const std::string key = session_id.empty() ? std::string("session") : std::string(session_id);
    std::vector<ExtensionWorkflowOutput> outputs;
    for (const auto& output : workflow_outputs_)
    {
        if (output.session_id == key)
        {
            outputs.push_back(output);
        }
    }

    if (outputs.empty() || !workflow_output_handler_)
    {
        return cleanupWorkflowSessionState(key, false, error);
    }

    std::string local_error;
    if (!workflow_output_handler_(key, outputs, &local_error))
    {
        if (error)
        {
            *error = local_error.empty() ? "Workflow output handler failed" : std::move(local_error);
        }
        return false;
    }

    const auto output_end =
        std::remove_if(workflow_outputs_.begin(), workflow_outputs_.end(),
                       [&key](const ExtensionWorkflowOutput& output) { return output.session_id == key; });
    workflow_outputs_.erase(output_end, workflow_outputs_.end());
    return cleanupWorkflowSessionState(key, false, error);
}

bool ExtensionSdkService::discardWorkflowSession(const std::string_view session_id, std::string* error)
{
    const std::string key = session_id.empty() ? std::string("session") : std::string(session_id);
    const auto output_end =
        std::remove_if(workflow_outputs_.begin(), workflow_outputs_.end(),
                       [&key](const ExtensionWorkflowOutput& output) { return output.session_id == key; });
    workflow_outputs_.erase(output_end, workflow_outputs_.end());
    return cleanupWorkflowSessionState(key, true, error);
}

const std::optional<ExtensionUiDescription>& ExtensionSdkService::latestUiDescription() const noexcept
{
    return latest_ui_description_;
}

const std::optional<ExtensionUiPatchState>& ExtensionSdkService::latestUiPatchState() const noexcept
{
    return latest_ui_patch_state_;
}

ExtensionSdkNamespaceAvailability
ExtensionSdkService::resolveNamespaceAvailability(const std::string_view message_type,
                                                  const ExtensionLaunchMode launch_mode)
{
    const std::string_view service_namespace = messageNamespace(message_type);
    ExtensionSdkNamespaceAvailability availability;
    availability.namespace_name = std::string(service_namespace);

    constexpr std::array<std::string_view, 5> kAlwaysAvailableNamespaces = {"settings", "views", "ui", "logging",
                                                                            "diagnostics"};
    if (std::find(kAlwaysAvailableNamespaces.begin(), kAlwaysAvailableNamespaces.end(), service_namespace) !=
        kAlwaysAvailableNamespaces.end())
    {
        availability.available = true;
        return availability;
    }

    if (service_namespace == "workflow")
    {
        availability.available = launch_mode == ExtensionLaunchMode::Workflow;
        if (!availability.available)
        {
            availability.diagnostic = "workflow.* services are only available to workflow extension sessions";
        }
        return availability;
    }

    if (service_namespace == "sdk")
    {
        availability.diagnostic = "sdk.* engine-domain protocol services are deferred until the rpc backend ships";
        return availability;
    }

    if (service_namespace.empty())
    {
        availability.diagnostic = "Protocol service namespace is empty";
        return availability;
    }

    availability.diagnostic = "Protocol service namespace is not available from ExtensionSdkService in this build";
    return availability;
}

bool ExtensionSdkService::buildExtensionSettingStorageKey(const std::string_view extension_id,
                                                          const std::string_view setting_key, QString* out_key,
                                                          std::string* error)
{
    if (!out_key)
    {
        if (error)
        {
            *error = "Extension setting storage-key output is null";
        }
        return false;
    }
    if (!isSafeExtensionId(extension_id))
    {
        if (error)
        {
            *error = "Extension setting uses an invalid extension id";
        }
        return false;
    }
    if (!isSafeExtensionSettingKey(setting_key))
    {
        if (error)
        {
            *error = "Extension setting uses an invalid setting key";
        }
        return false;
    }

    *out_key =
        QStringLiteral("extension/%1/%2")
            .arg(QString::fromStdString(std::string(extension_id)), QString::fromStdString(std::string(setting_key)));
    return true;
}

void ExtensionSdkService::enqueueDeferredProtocolResponse(const std::string_view session_id,
                                                          ExtensionProtocolMessage message)
{
    deferred_protocol_responses_.push_back(DeferredProtocolResponse{std::string(session_id), std::move(message)});
}

bool ExtensionSdkService::ensurePersistenceReady(std::string* error)
{
    if (!global_settings_persistence_)
    {
        if (error)
        {
            *error = "ExtensionSdkService has no GlobalSettingsPersistence backing store";
        }
        return false;
    }
    if (persistence_initialized_)
    {
        return true;
    }
    if (!global_settings_persistence_->initializeTable())
    {
        if (error)
        {
            *error = "Failed to initialize extension settings persistence";
        }
        return false;
    }

    persistence_initialized_ = true;
    return true;
}

} // namespace an::ui::extensions
