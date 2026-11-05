// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/settings/ExtensionSettingsSection.h"

#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/GlobalSettingsPersistence.h"
#include "theme/AnDesignTokens.h"

#include <QFutureWatcher>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <utility>

#include <verdigris/wobjectimpl.h>

namespace
{

using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionUiDescription;
using an::ui::extensions::ExtensionUiPatchOperation;
using an::ui::extensions::ExtensionUiPatchState;

using namespace an::ui::extensions::schema_utils;

QString sanitizeObjectToken(const std::string_view value)
{
    QString sanitized;
    sanitized.reserve(static_cast<int>(value.size()));

    bool previous_was_separator = false;
    for (const char ch : value)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0)
        {
            sanitized.push_back(QChar::fromLatin1(static_cast<char>(std::tolower(uch))));
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator)
        {
            sanitized.push_back(QChar::fromLatin1('_'));
            previous_was_separator = true;
        }
    }

    while (sanitized.endsWith(QChar::fromLatin1('_')))
    {
        sanitized.chop(1);
    }

    return sanitized.isEmpty() ? QStringLiteral("extension") : sanitized;
}

QString currentPlatformKey()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("default");
#endif
}

QStringList stringListValue(const QJsonValue& value)
{
    QStringList values;
    const auto append_array = [&values](const QJsonArray& array) {
        for (const QJsonValue& item : array)
        {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty() && !values.contains(text))
            {
                values.push_back(text);
            }
        }
    };

    if (value.isArray())
    {
        append_array(value.toArray());
    }
    return values;
}

QStringList platformStringListValue(const QJsonObject& config, const QString& key)
{
    const QJsonValue value = config.value(key);
    if (value.isArray())
    {
        return stringListValue(value);
    }
    if (!value.isObject())
    {
        return {};
    }

    const QJsonObject object = value.toObject();
    QStringList values = stringListValue(object.value(currentPlatformKey()));
    if (values.isEmpty())
    {
        values = stringListValue(object.value(QStringLiteral("default")));
    }
    return values;
}

QString firstLineOrTrimmed(QString text)
{
    text = text.trimmed();
    const int newline_index = text.indexOf(QLatin1Char('\n'));
    if (newline_index >= 0)
    {
        text = text.left(newline_index).trimmed();
    }
    return text.left(300);
}

QString versionFromOutput(QString text, const QString& pattern)
{
    text = text.trimmed();
    const QString trimmed_pattern = pattern.trimmed();
    if (!trimmed_pattern.isEmpty())
    {
        const QRegularExpression expression(trimmed_pattern);
        if (expression.isValid())
        {
            const QRegularExpressionMatch match = expression.match(text);
            if (match.hasMatch())
            {
                QString captured = match.lastCapturedIndex() >= 1 ? match.captured(1) : match.captured(0);
                captured = captured.trimmed();
                if (!captured.isEmpty())
                {
                    return captured.left(300);
                }
            }
        }
    }

    return firstLineOrTrimmed(text);
}

QCborMap buildWidgetNodeForField(const ExtensionSettingsSection::SettingField& field, std::string* error)
{
    QCborMap node;
    const QString type = normalizedParamType(field.type);

    node.insert(QStringLiteral("id"), field.id);
    node.insert(QStringLiteral("label"), field.label.isEmpty() ? field.id : field.label);
    node.insert(QStringLiteral("visible"), field.visible);
    node.insert(QStringLiteral("enabled"), field.enabled);
    if (!field.tooltip.isEmpty())
    {
        node.insert(QStringLiteral("tooltip"), field.tooltip);
    }

    if (type == QStringLiteral("button") || type == QStringLiteral("action"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("section_header"));
        node.insert(QStringLiteral("label"), field.definition.value(QStringLiteral("title")).toString());
        node.insert(QStringLiteral("description"), field.tooltip);
        node.insert(QStringLiteral("action_text"), field.label.isEmpty() ? field.id : field.label);
        return node;
    }
    if (type == QStringLiteral("status") ||
        field.definition.value(QStringLiteral("widget")).toString().trimmed() == QStringLiteral("status"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("status"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.definition.contains(QStringLiteral("tone")))
        {
            node.insert(QStringLiteral("tone"), jsonValueToCbor(field.definition.value(QStringLiteral("tone"))));
        }
        return node;
    }
    if (type == QStringLiteral("console_output") ||
        field.definition.value(QStringLiteral("widget")).toString().trimmed() == QStringLiteral("console_output"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("console_output"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.definition.contains(QStringLiteral("placeholder")))
        {
            node.insert(QStringLiteral("placeholder"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("placeholder"))));
        }
        if (field.definition.contains(QStringLiteral("min_lines")))
        {
            node.insert(QStringLiteral("min_lines"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("min_lines"))));
        }
        if (field.definition.contains(QStringLiteral("collapsed_when_empty")))
        {
            node.insert(QStringLiteral("collapsed_when_empty"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("collapsed_when_empty"))));
        }
        return node;
    }
    if (type == QStringLiteral("bool"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("checkbox"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value).isBool()
                                                 ? coerceSchemaFieldValue(field, field.default_value)
                                                 : QCborValue(false));
        return node;
    }
    if (type == QStringLiteral("int"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("spin_int"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.definition.contains(QStringLiteral("min")))
        {
            node.insert(QStringLiteral("min"), jsonValueToCbor(field.definition.value(QStringLiteral("min"))));
        }
        if (field.definition.contains(QStringLiteral("max")))
        {
            node.insert(QStringLiteral("max"), jsonValueToCbor(field.definition.value(QStringLiteral("max"))));
        }
        return node;
    }
    if (type == QStringLiteral("float"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("spin_double"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.definition.contains(QStringLiteral("min")))
        {
            node.insert(QStringLiteral("min"), jsonValueToCbor(field.definition.value(QStringLiteral("min"))));
        }
        if (field.definition.contains(QStringLiteral("max")))
        {
            node.insert(QStringLiteral("max"), jsonValueToCbor(field.definition.value(QStringLiteral("max"))));
        }
        if (field.definition.contains(QStringLiteral("step")))
        {
            node.insert(QStringLiteral("step"), jsonValueToCbor(field.definition.value(QStringLiteral("step"))));
        }
        return node;
    }
    if (type == QStringLiteral("slider") || type == QStringLiteral("int_slider"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("slider"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.definition.contains(QStringLiteral("min")))
        {
            node.insert(QStringLiteral("min"), jsonValueToCbor(field.definition.value(QStringLiteral("min"))));
        }
        if (field.definition.contains(QStringLiteral("max")))
        {
            node.insert(QStringLiteral("max"), jsonValueToCbor(field.definition.value(QStringLiteral("max"))));
        }
        if (field.definition.contains(QStringLiteral("step")))
        {
            node.insert(QStringLiteral("step"), jsonValueToCbor(field.definition.value(QStringLiteral("step"))));
        }
        else if (type == QStringLiteral("int_slider"))
        {
            node.insert(QStringLiteral("step"), 1.0);
        }
        return node;
    }
    if (type == QStringLiteral("choice"))
    {
        const QJsonValue options_value = field.definition.value(QStringLiteral("options"));
        if (!options_value.isArray() || options_value.toArray().isEmpty())
        {
            if (error)
            {
                *error = "choice settings require a non-empty options array";
            }
            return {};
        }

        node.insert(QStringLiteral("widget"), QStringLiteral("combo"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        node.insert(QStringLiteral("options"), QCborValue::fromVariant(options_value.toVariant()));
        return node;
    }
    if (type == QStringLiteral("string"))
    {
        node.insert(QStringLiteral("widget"), QStringLiteral("line_edit"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.secret)
        {
            node.insert(QStringLiteral("echo_mode"), QStringLiteral("password"));
        }
        if (field.definition.contains(QStringLiteral("placeholder")))
        {
            node.insert(QStringLiteral("placeholder"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("placeholder"))));
        }
        return node;
    }
    if (type == QStringLiteral("file_path") || type == QStringLiteral("dir_path"))
    {
        node.insert(QStringLiteral("widget"),
                    type == QStringLiteral("file_path") ? QStringLiteral("file_picker") : QStringLiteral("dir_picker"));
        node.insert(QStringLiteral("value"), coerceSchemaFieldValue(field, field.default_value));
        if (field.definition.contains(QStringLiteral("placeholder")))
        {
            node.insert(QStringLiteral("placeholder"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("placeholder"))));
        }
        if (field.definition.contains(QStringLiteral("button_label")))
        {
            node.insert(QStringLiteral("button_label"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("button_label"))));
        }
        if (field.definition.contains(QStringLiteral("read_only")))
        {
            node.insert(QStringLiteral("read_only"),
                        jsonValueToCbor(field.definition.value(QStringLiteral("read_only"))));
        }
        return node;
    }

    if (error)
    {
        *error = "Unsupported extension setting type: " + type.toStdString();
    }
    return {};
}

QString nextPatchId(const std::string_view extension_id, const int sequence)
{
    return QStringLiteral("settings.patch.%1.%2").arg(QString::fromStdString(std::string(extension_id))).arg(sequence);
}

} // anonymous namespace

W_OBJECT_IMPL(ExtensionSettingsSection)

ExtensionSettingsSection::ExtensionSettingsSection(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : QWidget(parent), settings_persistence_(settingsPersistence), sdk_service_(settingsPersistence)
{
    root_layout_ = new QVBoxLayout(this);
    root_layout_->setContentsMargins(0, 0, 0, 0);
    root_layout_->setSpacing(an::ui::tokens().spacing.md);

    action_status_banner_ = new an::ui::AnInfoBanner(QString(), an::ui::AnInfoBanner::Tone::Info, this);
    action_status_banner_->setObjectName(QStringLiteral("anExtensionSettingsActionStatus"));
    action_status_banner_->hide();
    root_layout_->addWidget(action_status_banner_);

    action_progress_bar_ = new QProgressBar(this);
    action_progress_bar_->setObjectName(QStringLiteral("anExtensionSettingsActionProgress"));
    action_progress_bar_->setRange(0, 0);
    action_progress_bar_->hide();
    root_layout_->addWidget(action_progress_bar_);

    tool_status_banner_ = new an::ui::AnInfoBanner(QString(), an::ui::AnInfoBanner::Tone::Info, this);
    tool_status_banner_->setObjectName(QStringLiteral("anExtensionSettingsToolStatus"));
    tool_status_banner_->hide();
    root_layout_->addWidget(tool_status_banner_);

    widget_host_ = new an::ui::extensions::ExtensionUiWidgetHost(this);
    root_layout_->addWidget(widget_host_);

    settings_action_runner_ = [](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
        return an::ui::extensions::run_extension_settings_action(request);
    };

    QObject::connect(widget_host_, &an::ui::extensions::ExtensionUiWidgetHost::widgetEvent, this,
                     [this](const QString& widget_id, const QString& event_name, const QCborValue& value,
                            const bool coalesced) { onWidgetEvent(widget_id, event_name, value, coalesced); });
}

bool ExtensionSettingsSection::bindExtension(const ExtensionLoadResult& result, std::string* error)
{
    if (!result.manifest.has_value() || !result.settings_schema.has_value())
    {
        if (error)
        {
            *error = "Extension settings require manifest metadata and a cached settings schema";
        }
        return false;
    }

    extension_id_ = result.manifest->id;
    extension_name_ = QString::fromStdString(result.manifest->name);
    load_result_ = result;
    object_token_ = sanitizeObjectToken(extension_id_);
    setObjectName(QStringLiteral("anExtensionSettingsSectionBody_%1").arg(object_token_));

    fields_.clear();
    resetExternalToolConfiguration();
    const QJsonArray schema = result.settings_schema->schema;
    fields_.reserve(static_cast<std::size_t>(schema.size()));
    for (const QJsonValue& value : schema)
    {
        if (!value.isObject())
        {
            if (error)
            {
                *error = "Extension settings schema entries must be objects";
            }
            return false;
        }

        const QJsonObject definition = value.toObject();
        const QString id = definition.value(QStringLiteral("id")).toString().trimmed();
        const QString type = normalizedParamType(definition.value(QStringLiteral("type")).toString());
        if (id.isEmpty() || type.isEmpty())
        {
            if (error)
            {
                *error = "Extension settings schema entries require non-empty id and type fields";
            }
            return false;
        }

        SettingField field;
        field.id = id;
        field.type = type;
        field.label = definition.value(QStringLiteral("label")).toString();
        field.group = definition.value(QStringLiteral("group")).toString();
        field.tooltip = definition.value(QStringLiteral("tooltip")).toString();
        field.visible_when = definition.value(QStringLiteral("visible_when")).toString();
        field.enabled_when = definition.value(QStringLiteral("enabled_when")).toString();
        field.group_style = definition.value(QStringLiteral("group_style")).toString();
        field.default_value = jsonValueToCbor(definition.value(QStringLiteral("default")));
        field.visible = definition.value(QStringLiteral("visible")).toBool(true);
        field.enabled = definition.value(QStringLiteral("enabled")).toBool(true);
        field.secret = definition.value(QStringLiteral("secret")).toBool(false);
        field.sensitive = definition.value(QStringLiteral("sensitive")).toBool(false) || field.secret;
        field.persist = definition.value(QStringLiteral("persist")).toBool(true);
        field.action = type == QStringLiteral("button") || type == QStringLiteral("action");
        field.autorun = definition.value(QStringLiteral("autorun"))
                            .toBool(definition.value(QStringLiteral("auto_run")).toBool(false));
        field.show_status = definition.value(QStringLiteral("show_status")).toBool(true);
        field.timeout_ms = definition.value(QStringLiteral("timeout_ms")).toInt(60000);
        field.definition = definition;
        const QJsonValue tool_configuration = definition.value(QStringLiteral("tool_configuration"));
        if (tool_configuration.isObject())
        {
            parseExternalToolConfiguration(tool_configuration.toObject());
        }
        fields_.push_back(std::move(field));
    }

    ExtensionUiDescription description;
    if (!buildDescription(schema, &description, error))
    {
        return false;
    }

    widget_host_->setObjectName(QStringLiteral("anExtensionSettingsHost_%1").arg(object_token_));

    suppress_events_ = true;
    const bool described = widget_host_->applyDescription(description, error);
    suppress_events_ = false;
    if (!described)
    {
        return false;
    }

    loadPersistedValues();
    refreshExternalToolConfiguration();
    startAutorunSettingsActions();
    return true;
}

bool ExtensionSettingsSection::hasSchema() const noexcept
{
    return widget_host_ && widget_host_->currentDescription().has_value();
}

an::ui::extensions::ExtensionUiWidgetHost* ExtensionSettingsSection::widgetHost() const noexcept
{
    return widget_host_;
}

std::string ExtensionSettingsSection::extensionId() const noexcept
{
    return extension_id_;
}

void ExtensionSettingsSection::loadPersistedValues()
{
    std::string error;
    suppress_events_ = true;
    const bool loaded = loadWidgetState(false, &error);
    const bool conditioned = loaded && evaluateAndApplyConditions(&error);
    suppress_events_ = false;
    if (!loaded || !conditioned)
    {
        LOG_WARNING("Failed to load extension settings for {}: {}", extension_id_, error);
    }
}

bool ExtensionSettingsSection::saveSettings(std::string* error)
{
    if (!widget_host_ || extension_id_.empty())
    {
        return true;
    }
    if (!settings_persistence_)
    {
        return true;
    }

    QCborMap params;
    if (!widget_host_->snapshotValidationParams(&params, error))
    {
        return false;
    }

    for (const SettingField& field : fields_)
    {
        if (!shouldPersistField(field))
        {
            continue;
        }

        QCborValue value = params.value(field.id);
        value = coerceSchemaFieldValue(field, value);
        if (!sdk_service_.upsertExtensionSetting(extension_id_, field.id.toStdString(), value, error))
        {
            return false;
        }
    }

    return true;
}

void ExtensionSettingsSection::resetToDefaults()
{
    suppress_events_ = true;
    std::string error;
    const bool loaded = loadWidgetState(true, &error);
    const bool conditioned = loaded && evaluateAndApplyConditions(&error);
    suppress_events_ = false;
    if (!loaded || !conditioned)
    {
        LOG_WARNING("Failed to reset extension settings for {}: {}", extension_id_, error);
        return;
    }

    settingsModified();
}

void ExtensionSettingsSection::refreshTheme()
{
    if (root_layout_)
    {
        root_layout_->setSpacing(an::ui::tokens().spacing.md);
    }
    if (widget_host_)
    {
        widget_host_->refreshTheme();
    }
}

bool ExtensionSettingsSection::buildDescription(const QJsonArray&, ExtensionUiDescription* out_description,
                                                std::string* error) const
{
    return buildDescriptionForFields(fields_, QStringLiteral("settings.%1").arg(QString::fromStdString(extension_id_)),
                                     QStringLiteral("settings.describe.%1").arg(QString::fromStdString(extension_id_)),
                                     out_description, error);
}

bool ExtensionSettingsSection::buildDescriptionForFields(const std::vector<SettingField>& fields,
                                                         const QString& session_id, const QString& request_id,
                                                         ExtensionUiDescription* out_description,
                                                         std::string* error) const
{
    if (!out_description)
    {
        if (error)
        {
            *error = "Extension settings description output is null";
        }
        return false;
    }

    QCborArray root_children;
    QStringList widget_ids;

    QStringList group_order;
    QHash<QString, QString> group_styles;
    QHash<QString, QCborArray> grouped_children;
    for (const SettingField& field : fields)
    {
        std::string local_error;
        QCborMap field_node = buildWidgetNodeForField(field, &local_error);
        if (field_node.isEmpty() && !local_error.empty())
        {
            if (error)
            {
                *error = local_error;
            }
            return false;
        }

        widget_ids.push_back(field.id);
        const QString group = field.group.trimmed();
        if (!group.isEmpty() && !group_order.contains(group))
        {
            group_order.push_back(group);
        }
        const QString group_style = field.group_style.trimmed().toLower();
        if (!group.isEmpty() && !group_style.isEmpty() && !group_styles.contains(group))
        {
            group_styles.insert(group, group_style);
        }
        grouped_children[group].push_back(field_node);
    }

    root_children = grouped_children.value(QString());
    for (const QString& group : group_order)
    {
        const bool use_card = group_styles.value(group) == QStringLiteral("card");
        QCborMap group_node{
            {QStringLiteral("widget"), use_card ? QStringLiteral("card") : QStringLiteral("collapsible_section")},
            {QStringLiteral("label"), group},
            {QStringLiteral("children"), grouped_children.value(group)}};
        if (use_card)
        {
            group_node.insert(QStringLiteral("compact"), true);
            group_node.insert(QStringLiteral("body_padded"), true);
        }
        else
        {
            group_node.insert(QStringLiteral("expanded"), true);
        }
        root_children.push_back(group_node);
    }

    out_description->extension_id = extension_id_;
    out_description->session_id = session_id.toStdString();
    out_description->request_id = request_id.toStdString();
    out_description->root_widget = QStringLiteral("column");
    out_description->widget_count = widget_ids.size() + group_order.size() + 1;
    out_description->widget_ids = widget_ids;
    out_description->tree =
        QCborMap{{QStringLiteral("root"), QCborMap{{QStringLiteral("widget"), QStringLiteral("column")},
                                                   {QStringLiteral("children"), root_children}}}};
    return true;
}

bool ExtensionSettingsSection::loadWidgetState(const bool use_defaults, std::string* error)
{
    if (!widget_host_ || extension_id_.empty())
    {
        return true;
    }

    ExtensionUiPatchState patch_state;
    patch_state.extension_id = extension_id_;
    patch_state.session_id = "settings." + extension_id_;
    patch_state.request_id = nextPatchId(extension_id_, ++patch_sequence_).toStdString();

    for (const SettingField& field : fields_)
    {
        if (field.action)
        {
            continue;
        }

        QCborValue value = field.default_value;
        if (!use_defaults && settings_persistence_ && shouldPersistField(field))
        {
            if (!sdk_service_.selectExtensionSetting(extension_id_, field.id.toStdString(), &value, field.default_value,
                                                     error))
            {
                return false;
            }
        }

        bool used_default = false;
        const QCborValue coerced = coerceSchemaFieldValue(field, value, &used_default);
        patch_state.operations.push_back(ExtensionUiPatchOperation{field.id, QStringLiteral("value"), coerced});
        if (used_default && !use_defaults && !value.isNull() && !value.isUndefined())
        {
            LOG_WARNING("Extension setting {}.{} used its default value because the stored value could not be coerced "
                        "to type {}",
                        extension_id_, field.id.toStdString(), field.type.toStdString());
        }
    }

    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    return widget_host_->applyPatchState(patch_state, error);
}

bool ExtensionSettingsSection::evaluateAndApplyConditions(std::string* error)
{
    if (!widget_host_ || extension_id_.empty())
    {
        return true;
    }

    QCborMap params;
    if (!widget_host_->snapshotValidationParams(&params, error))
    {
        return false;
    }

    ExtensionUiPatchState patch_state;
    patch_state.extension_id = extension_id_;
    patch_state.session_id = "settings." + extension_id_;
    patch_state.request_id = nextPatchId(extension_id_, ++patch_sequence_).toStdString();

    for (const SettingField& field : fields_)
    {
        bool visible = field.visible;
        if (!field.visible_when.trimmed().isEmpty())
        {
            const auto condition = parseCondition(field.visible_when);
            visible = condition.has_value() ? evaluateCondition(*condition, params) : true;
        }

        bool enabled = field.enabled;
        if (!field.enabled_when.trimmed().isEmpty())
        {
            const auto condition = parseCondition(field.enabled_when);
            enabled = condition.has_value() ? evaluateCondition(*condition, params) : true;
        }

        patch_state.operations.push_back(
            ExtensionUiPatchOperation{field.id, QStringLiteral("visible"), QCborValue(visible)});
        patch_state.operations.push_back(
            ExtensionUiPatchOperation{field.id, QStringLiteral("enabled"), QCborValue(enabled)});
    }

    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    return widget_host_->applyPatchState(patch_state, error);
}

std::optional<ExtensionSettingsSection::ParsedCondition>
ExtensionSettingsSection::parseCondition(const QString& expression)
{
    return parseSchemaCondition(expression);
}

bool ExtensionSettingsSection::evaluateCondition(const ParsedCondition& condition, const QCborMap& params)
{
    return evaluateSchemaCondition(condition, params);
}

const ExtensionSettingsSection::SettingField* ExtensionSettingsSection::fieldById(const QString& id) const
{
    const auto it =
        std::find_if(fields_.cbegin(), fields_.cend(), [&](const SettingField& field) { return field.id == id; });
    return it == fields_.cend() ? nullptr : &(*it);
}

bool ExtensionSettingsSection::shouldPersistField(const SettingField& field) const noexcept
{
    return !field.action && !field.secret && field.persist;
}

QCborMap ExtensionSettingsSection::actionSnapshot(std::string* error) const
{
    QCborMap params;
    if (widget_host_ && !widget_host_->snapshotValidationParams(&params, error))
    {
        return {};
    }

    QCborMap snapshot;
    for (const SettingField& field : fields_)
    {
        if (field.action)
        {
            continue;
        }
        const QCborValue value = params.contains(field.id) ? params.value(field.id) : field.default_value;
        snapshot.insert(field.id, coerceSchemaFieldValue(field, value));
    }
    if (external_tool_config_.enabled)
    {
        const ExternalToolResolution resolution = resolveExternalTool(snapshot);
        const QCborMap record{{QStringLiteral("configured_folder"), resolution.configured_folder},
                              {QStringLiteral("resolved_cli_executable"), resolution.executable},
                              {QStringLiteral("resolution_source"), resolution.source},
                              {QStringLiteral("resolved_cli_version"), resolution.version},
                              {QStringLiteral("resolver_diagnostic"), resolution.diagnostic}};
        for (auto it = record.cbegin(); it != record.cend(); ++it)
        {
            snapshot.insert(it.key().toString(), it.value());
        }
    }
    return snapshot;
}

QString ExtensionSettingsSection::redactSensitiveText(QString text, const QCborMap& snapshot) const
{
    if (text.isEmpty())
    {
        return text;
    }

    for (const SettingField& field : fields_)
    {
        if (!field.secret && !field.sensitive)
        {
            continue;
        }
        const QString value = cborToString(snapshot.value(field.id)).trimmed();
        if (value.isEmpty())
        {
            continue;
        }
        text.replace(value, QStringLiteral("[redacted]"), Qt::CaseSensitive);
    }
    return text;
}

QCborValue ExtensionSettingsSection::redactSensitiveValue(const QCborValue& value, const QCborMap& snapshot) const
{
    if (value.isString())
    {
        return QCborValue(redactSensitiveText(value.toString(), snapshot));
    }
    if (value.isArray())
    {
        QCborArray redacted;
        const QCborArray array = value.toArray();
        for (const QCborValue& item : array)
        {
            redacted.push_back(redactSensitiveValue(item, snapshot));
        }
        return redacted;
    }
    if (value.isMap())
    {
        QCborMap redacted;
        const QCborMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
        {
            redacted.insert(it.key().toString(), redactSensitiveValue(it.value(), snapshot));
        }
        return redacted;
    }
    return value;
}

void ExtensionSettingsSection::setActionWidgetsEnabled(const bool enabled)
{
    if (!widget_host_ || extension_id_.empty())
    {
        return;
    }

    ExtensionUiPatchState patch_state;
    patch_state.extension_id = extension_id_;
    patch_state.session_id = "settings." + extension_id_;
    patch_state.request_id = nextPatchId(extension_id_, ++patch_sequence_).toStdString();
    for (const SettingField& field : fields_)
    {
        if (field.action)
        {
            patch_state.operations.push_back(
                ExtensionUiPatchOperation{field.id, QStringLiteral("enabled"), QCborValue(enabled)});
        }
    }
    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    if (patch_state.patch_count > 0)
    {
        std::string error;
        if (!widget_host_->applyPatchState(patch_state, &error))
        {
            LOG_WARNING("Failed to update extension settings action controls for {}: {}", extension_id_, error);
        }
    }
}

void ExtensionSettingsSection::setSettingsActionRunner(SettingsActionRunner runner)
{
    settings_action_runner_ = std::move(runner);
}

bool ExtensionSettingsSection::actionInProgress() const noexcept
{
    return action_watcher_ != nullptr;
}

void ExtensionSettingsSection::startAutorunSettingsActions()
{
    if (actionInProgress() || !settings_action_runner_)
    {
        return;
    }

    for (const SettingField& field : fields_)
    {
        if (!field.action || !field.autorun)
        {
            continue;
        }

        const QString field_id = field.id;
        QTimer::singleShot(0, this, [this, field_id]() {
            const SettingField* autorun_field = fieldById(field_id);
            if (autorun_field && autorun_field->action && !actionInProgress())
            {
                startSettingsAction(*autorun_field);
            }
        });
        return;
    }
}

void ExtensionSettingsSection::startSettingsAction(const SettingField& field, std::optional<QCborMap> snapshot_override)
{
    if (actionInProgress() || !settings_action_runner_)
    {
        return;
    }

    refreshExternalToolConfiguration();

    std::string error;
    active_action_snapshot_ = snapshot_override.has_value() ? *snapshot_override : actionSnapshot(&error);
    if (!error.empty())
    {
        LOG_WARNING("Failed to snapshot settings for action {}.{}: {}", extension_id_, field.id.toStdString(), error);
        return;
    }

    action_cancellation_token_ = an::ui::extensions::ExtensionOperationCancellationToken{};
    active_action_id_ = field.definition.value(QStringLiteral("action_id")).toString(field.id).trimmed();
    if (active_action_id_.isEmpty())
    {
        active_action_id_ = field.id;
    }

    active_action_show_status_ = field.show_status;
    if (active_action_show_status_ && action_status_banner_)
    {
        action_status_banner_->setMessage(QStringLiteral("Working..."));
        action_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Info);
        action_status_banner_->show();
    }
    else if (action_status_banner_)
    {
        action_status_banner_->hide();
    }
    if (active_action_show_status_ && action_progress_bar_)
    {
        action_progress_bar_->show();
    }
    else if (action_progress_bar_)
    {
        action_progress_bar_->hide();
    }
    setActionWidgetsEnabled(false);

    an::ui::extensions::ExtensionSettingsActionRequest request;
    request.load_result = load_result_;
    request.context = an::ui::extensions::default_extension_discovery_options().context;
    request.action_id = active_action_id_.toStdString();
    request.settings_snapshot = active_action_snapshot_;
    request.timeout_ms = field.timeout_ms > 0 ? field.timeout_ms : 60000;
    request.cancellation_token = action_cancellation_token_;
    const int action_generation = ++active_action_generation_;
    const QPointer<ExtensionSettingsSection> section_guard(this);
    request.update_callback = [section_guard,
                               action_generation](const an::ui::extensions::ExtensionSettingsActionResult& update) {
        if (!section_guard)
        {
            return;
        }
        QMetaObject::invokeMethod(
            section_guard.data(),
            [section_guard, action_generation, update]() {
                if (!section_guard || section_guard->active_action_generation_ != action_generation ||
                    !section_guard->actionInProgress())
                {
                    return;
                }
                section_guard->applySettingsActionUpdate(update);
            },
            Qt::QueuedConnection);
    };

    auto runner = settings_action_runner_;
    action_watcher_ = new QFutureWatcher<an::ui::extensions::ExtensionSettingsActionResult>(this);
    QObject::connect(action_watcher_, &QFutureWatcher<an::ui::extensions::ExtensionSettingsActionResult>::finished,
                     this, [this]() {
                         auto* watcher = action_watcher_;
                         if (!watcher)
                         {
                             return;
                         }
                         const an::ui::extensions::ExtensionSettingsActionResult result = watcher->result();
                         action_watcher_ = nullptr;
                         watcher->deleteLater();
                         finishSettingsAction(result, active_action_snapshot_);
                     });
    action_watcher_->setFuture(QtConcurrent::run([runner, request]() {
        try
        {
            return runner(request);
        }
        catch (const std::exception& e)
        {
            an::ui::extensions::ExtensionSettingsActionResult result;
            result.ok = false;
            result.message = e.what();
            result.tone = "error";
            return result;
        }
    }));
}

void ExtensionSettingsSection::applySettingsActionUpdate(
    const an::ui::extensions::ExtensionSettingsActionResult& result)
{
    if (result.settings_updates.isEmpty() && result.transient_updates.isEmpty())
    {
        return;
    }

    auto* dialog_host = active_action_dialog_host_.data();
    const std::vector<SettingField> dialog_fields = active_action_dialog_fields_;
    const QCborMap snapshot = active_action_snapshot_;

    std::string error;
    applyActionUpdates(result.settings_updates, true, snapshot, &error);
    applyActionUpdates(result.transient_updates, false, snapshot, &error);
    applyActionUpdatesToHost(dialog_host, dialog_fields, result.settings_updates, true, false, snapshot, &error);
    applyActionUpdatesToHost(dialog_host, dialog_fields, result.transient_updates, false, false, snapshot, &error);
    if (!error.empty())
    {
        LOG_WARNING("Failed to apply streamed extension settings action update for {}: {}", extension_id_, error);
    }
    settingsModified();
}

void ExtensionSettingsSection::finishSettingsAction(const an::ui::extensions::ExtensionSettingsActionResult& result,
                                                    const QCborMap& snapshot)
{
    auto* dialog_host = active_action_dialog_host_.data();
    auto* dialog_buttons = active_action_dialog_buttons_.data();
    const std::vector<SettingField> dialog_fields = active_action_dialog_fields_;

    std::string error;
    applyActionUpdates(result.settings_updates, true, snapshot, &error);
    applyActionUpdates(result.transient_updates, false, snapshot, &error);
    applyActionUpdatesToHost(dialog_host, dialog_fields, result.settings_updates, true, false, snapshot, &error);
    applyActionUpdatesToHost(dialog_host, dialog_fields, result.transient_updates, false, false, snapshot, &error);
    clearTransientActionFields();
    clearTransientActionFields(dialog_host, dialog_fields);
    refreshExternalToolConfiguration();

    if (action_progress_bar_)
    {
        action_progress_bar_->hide();
    }
    if (action_status_banner_)
    {
        if (active_action_show_status_)
        {
            QString message =
                QString::fromStdString(result.message.empty() ? (result.ok ? std::string("Settings action completed.")
                                                                           : std::string("Settings action failed."))
                                                              : result.message);
            message = redactSensitiveText(std::move(message), snapshot);
            action_status_banner_->setMessage(message);
            an::ui::AnInfoBanner::Tone tone =
                result.ok ? an::ui::AnInfoBanner::Tone::Success : an::ui::AnInfoBanner::Tone::Error;
            if (result.tone == "info")
            {
                tone = an::ui::AnInfoBanner::Tone::Info;
            }
            else if (result.tone == "warning")
            {
                tone = an::ui::AnInfoBanner::Tone::Warning;
            }
            else if (result.tone == "success")
            {
                tone = an::ui::AnInfoBanner::Tone::Success;
            }
            action_status_banner_->setTone(tone);
            action_status_banner_->show();
        }
        else
        {
            action_status_banner_->hide();
        }
    }
    active_action_show_status_ = true;
    if (dialog_buttons)
    {
        dialog_buttons->setEnabled(true);
    }

    const bool conditioned = evaluateAndApplyConditions(&error);
    if (!conditioned)
    {
        LOG_WARNING("Failed to restore extension settings action controls for {}: {}", extension_id_, error);
        setActionWidgetsEnabled(true);
    }
    settingsModified();
}

void ExtensionSettingsSection::applyActionUpdates(const QCborMap& updates, const bool persist_updates,
                                                  const QCborMap& snapshot, std::string* error)
{
    applyActionUpdatesToHost(widget_host_, fields_, updates, persist_updates, true, snapshot, error);
}

void ExtensionSettingsSection::applyActionUpdatesToHost(an::ui::extensions::ExtensionUiWidgetHost* host,
                                                        const std::vector<SettingField>& fields,
                                                        const QCborMap& updates, const bool persist_updates,
                                                        const bool persist_to_settings, const QCborMap& snapshot,
                                                        std::string* error)
{
    if (updates.isEmpty() || !host || extension_id_.empty())
    {
        return;
    }

    ExtensionUiPatchState patch_state;
    if (const auto& description = host->currentDescription())
    {
        patch_state.extension_id = description->extension_id;
        patch_state.session_id = description->session_id;
    }
    else
    {
        patch_state.extension_id = extension_id_;
        patch_state.session_id = "settings." + extension_id_;
    }
    patch_state.request_id = nextPatchId(extension_id_, ++patch_sequence_).toStdString();

    for (auto it = updates.cbegin(); it != updates.cend(); ++it)
    {
        const QString id = it.key().toString();
        const auto field_it = std::find_if(fields.cbegin(), fields.cend(),
                                           [&](const SettingField& candidate) { return candidate.id == id; });
        const SettingField* field = field_it == fields.cend() ? nullptr : &(*field_it);
        if (!field || field->action || field->secret || field->sensitive)
        {
            continue;
        }
        if (persist_updates && !shouldPersistField(*field))
        {
            continue;
        }

        const QCborValue value = coerceSchemaFieldValue(*field, redactSensitiveValue(it.value(), snapshot));
        patch_state.operations.push_back(ExtensionUiPatchOperation{id, QStringLiteral("value"), value});
        if (persist_to_settings && persist_updates && shouldPersistField(*field))
        {
            if (!sdk_service_.upsertExtensionSetting(extension_id_, field->id.toStdString(), value, error))
            {
                return;
            }
        }
    }

    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    if (patch_state.patch_count > 0)
    {
        suppress_events_ = true;
        const bool applied = host->applyPatchState(patch_state, error);
        suppress_events_ = false;
        if (!applied)
        {
            return;
        }
    }
}

void ExtensionSettingsSection::clearTransientActionFields(an::ui::extensions::ExtensionUiWidgetHost* host,
                                                          const std::vector<SettingField>& fields)
{
    if (!host || extension_id_.empty())
    {
        return;
    }

    ExtensionUiPatchState patch_state;
    if (const auto& description = host->currentDescription())
    {
        patch_state.extension_id = description->extension_id;
        patch_state.session_id = description->session_id;
    }
    else
    {
        patch_state.extension_id = extension_id_;
        patch_state.session_id = "settings." + extension_id_;
    }
    patch_state.request_id = nextPatchId(extension_id_, ++patch_sequence_).toStdString();
    for (const SettingField& field : fields)
    {
        if (field.action || (!field.secret && !(field.sensitive && !field.persist)))
        {
            continue;
        }
        if (!host->widgetById(field.id))
        {
            continue;
        }
        patch_state.operations.push_back(
            ExtensionUiPatchOperation{field.id, QStringLiteral("value"), coerceSchemaFieldValue(field, QString{})});
    }
    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    if (patch_state.patch_count > 0)
    {
        std::string error;
        suppress_events_ = true;
        const bool applied = host->applyPatchState(patch_state, &error);
        suppress_events_ = false;
        if (!applied)
        {
            LOG_WARNING("Failed to clear transient extension settings fields for {}: {}", extension_id_, error);
        }
    }
}

void ExtensionSettingsSection::clearTransientActionFields()
{
    clearTransientActionFields(widget_host_, fields_);
}

void ExtensionSettingsSection::resetExternalToolConfiguration()
{
    external_tool_config_ = ExternalToolConfiguration{};
    if (tool_status_banner_)
    {
        tool_status_banner_->hide();
    }
}

void ExtensionSettingsSection::parseExternalToolConfiguration(const QJsonObject& config)
{
    const QString setting_key = config.value(QStringLiteral("settings_key")).toString().trimmed();
    if (setting_key.isEmpty())
    {
        return;
    }

    external_tool_config_.enabled = true;
    external_tool_config_.setting_key = setting_key;
    external_tool_config_.label =
        config.value(QStringLiteral("label")).toString(QStringLiteral("External tool")).trimmed();
    if (external_tool_config_.label.isEmpty())
    {
        external_tool_config_.label = QStringLiteral("External tool");
    }
    external_tool_config_.not_configured_text =
        config.value(QStringLiteral("not_configured_text"))
            .toString(QStringLiteral("%1 is not configured.").arg(external_tool_config_.label))
            .trimmed();
    external_tool_config_.configured_text =
        config.value(QStringLiteral("configured_text"))
            .toString(QStringLiteral("%1 is configured and ready to use.").arg(external_tool_config_.label))
            .trimmed();
    external_tool_config_.invalid_text = config.value(QStringLiteral("invalid_text")).toString().trimmed();
    external_tool_config_.append_platform_suffix =
        config.value(QStringLiteral("append_platform_executable_suffix")).toBool(true);
    external_tool_config_.require_executable = config.value(QStringLiteral("require_executable")).toBool(true);
    external_tool_config_.required_files.clear();
    external_tool_config_.executable_candidates.clear();
    external_tool_config_.candidate_subdirectories =
        platformStringListValue(config, QStringLiteral("candidate_subdirectories"));
    for (const QString& file_name : stringListValue(config.value(QStringLiteral("required_files"))))
    {
        if (!external_tool_config_.required_files.contains(file_name))
        {
            external_tool_config_.required_files.push_back(file_name);
        }
    }
    for (const QString& file_name : stringListValue(config.value(QStringLiteral("required_executables"))))
    {
        if (!external_tool_config_.required_files.contains(file_name))
        {
            external_tool_config_.required_files.push_back(file_name);
        }
    }
    external_tool_config_.candidate_directories =
        platformStringListValue(config, QStringLiteral("candidate_directories"));
    for (const QString& directory : platformStringListValue(config, QStringLiteral("platform_candidate_directories")))
    {
        if (!external_tool_config_.candidate_directories.contains(directory))
        {
            external_tool_config_.candidate_directories.push_back(directory);
        }
    }
    external_tool_config_.primary_executable = config.value(QStringLiteral("primary_executable")).toString().trimmed();
    if (external_tool_config_.primary_executable.isEmpty() && !external_tool_config_.required_files.isEmpty())
    {
        external_tool_config_.primary_executable = external_tool_config_.required_files.front();
    }
    external_tool_config_.executable_candidates =
        platformStringListValue(config, QStringLiteral("primary_executable_candidates"));
    if (!external_tool_config_.primary_executable.isEmpty() &&
        !external_tool_config_.executable_candidates.contains(external_tool_config_.primary_executable))
    {
        external_tool_config_.executable_candidates.push_front(external_tool_config_.primary_executable);
    }
    if (external_tool_config_.executable_candidates.isEmpty() && !external_tool_config_.primary_executable.isEmpty())
    {
        external_tool_config_.executable_candidates.push_back(external_tool_config_.primary_executable);
    }
    external_tool_config_.version_arguments = stringListValue(config.value(QStringLiteral("version_arguments")));
    external_tool_config_.version_pattern = config.value(QStringLiteral("version_pattern")).toString().trimmed();
    external_tool_config_.show_configured_banner = config.value(QStringLiteral("show_configured_banner")).toBool(true);
}

void ExtensionSettingsSection::refreshExternalToolConfiguration()
{
    if (!external_tool_config_.enabled || !widget_host_ || extension_id_.empty())
    {
        external_tool_config_.resolved_record = QCborMap{};
        if (tool_status_banner_)
        {
            tool_status_banner_->hide();
        }
        return;
    }

    std::string error;
    QCborMap params;
    if (!widget_host_->snapshotValidationParams(&params, &error))
    {
        if (tool_status_banner_)
        {
            tool_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Error);
            tool_status_banner_->setMessage(QString::fromStdString(error));
            tool_status_banner_->show();
        }
        return;
    }

    const ExternalToolResolution resolution = resolveExternalTool(params);
    external_tool_config_.resolved_record =
        QCborMap{{QStringLiteral("configured_folder"), resolution.configured_folder},
                 {QStringLiteral("resolved_cli_executable"), resolution.executable},
                 {QStringLiteral("resolution_source"), resolution.source},
                 {QStringLiteral("resolved_cli_version"), resolution.version},
                 {QStringLiteral("resolver_diagnostic"), resolution.diagnostic}};
    if (resolution.valid && !resolution.executable.isEmpty())
    {
        const SettingField* configured_field = fieldById(external_tool_config_.setting_key);
        if (configured_field && configured_field->type == QStringLiteral("file_path"))
        {
            external_tool_config_.resolved_record.insert(external_tool_config_.setting_key, resolution.executable);
        }
    }

    if (tool_status_banner_)
    {
        if (resolution.valid && !external_tool_config_.show_configured_banner)
        {
            tool_status_banner_->hide();
            applyExternalToolRecordToFields(external_tool_config_.resolved_record);
            return;
        }
        QString message =
            resolution.diagnostic.isEmpty() ? external_tool_config_.not_configured_text : resolution.diagnostic;
        if (resolution.valid)
        {
            if (!resolution.executable.isEmpty())
            {
                message += QStringLiteral(" Executable: %1.").arg(resolution.executable);
            }
            if (!resolution.version.isEmpty())
            {
                message += QStringLiteral(" Version: %1.").arg(resolution.version);
            }
        }
        tool_status_banner_->setMessage(message);
        tool_status_banner_->setTone(resolution.valid ? an::ui::AnInfoBanner::Tone::Success
                                                      : an::ui::AnInfoBanner::Tone::Warning);
        tool_status_banner_->show();
    }

    applyExternalToolRecordToFields(external_tool_config_.resolved_record);
}

QString ExtensionSettingsSection::currentExternalToolPath(const QCborMap& params) const
{
    if (!external_tool_config_.enabled || external_tool_config_.setting_key.isEmpty())
    {
        return {};
    }
    return cborToString(params.value(external_tool_config_.setting_key)).trimmed();
}

QString ExtensionSettingsSection::currentExternalToolDirectory(const QCborMap& params) const
{
    const QString configured_path = currentExternalToolPath(params);
    const QFileInfo configured_info(configured_path);
    if (configured_info.exists() && configured_info.isFile())
    {
        return configured_info.absolutePath();
    }
    return configured_path;
}

QString ExtensionSettingsSection::platformAdjustedRequiredFile(const QString& fileName) const
{
    QString adjusted = fileName.trimmed();
#ifdef _WIN32
    if (external_tool_config_.append_platform_suffix &&
        !adjusted.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
        !adjusted.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) &&
        !adjusted.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive))
    {
        adjusted += QStringLiteral(".exe");
    }
#endif
    return adjusted;
}

QString ExtensionSettingsSection::externalToolExecutableInDirectory(const QString& directory) const
{
    const QString trimmed_directory = directory.trimmed();
    if (trimmed_directory.isEmpty() || external_tool_config_.executable_candidates.isEmpty())
    {
        return {};
    }

    QStringList relative_directories{QString()};
    for (const QString& raw_subdirectory : external_tool_config_.candidate_subdirectories)
    {
        const QString subdirectory = raw_subdirectory.trimmed();
        if (!subdirectory.isEmpty() && !relative_directories.contains(subdirectory))
        {
            relative_directories.push_back(subdirectory);
        }
    }

    for (const QString& relative_directory : relative_directories)
    {
        QDir tool_dir(trimmed_directory);
        if (!relative_directory.isEmpty())
        {
            tool_dir = QDir(tool_dir.filePath(relative_directory));
        }
        if (!tool_dir.exists())
        {
            continue;
        }

        const QStringList entries = tool_dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& raw_candidate : external_tool_config_.executable_candidates)
        {
            const QString candidate = platformAdjustedRequiredFile(raw_candidate);
            if (candidate.isEmpty())
            {
                continue;
            }

            const QFileInfo file_info(tool_dir.filePath(candidate));
            if (file_info.exists() && file_info.isFile())
            {
                return file_info.absoluteFilePath();
            }

            if (candidate.contains(QLatin1Char('/')) || candidate.contains(QDir::separator()))
            {
                continue;
            }

            for (const QString& entry : entries)
            {
                if (entry.compare(candidate, Qt::CaseInsensitive) == 0)
                {
                    return QFileInfo(tool_dir.filePath(entry)).absoluteFilePath();
                }
            }
        }
    }

    return {};
}

bool ExtensionSettingsSection::validateExternalToolExecutable(const QString& executablePath, QString* outMessage) const
{
    const QString trimmed_path = executablePath.trimmed();
    if (trimmed_path.isEmpty())
    {
        if (outMessage)
        {
            *outMessage = external_tool_config_.not_configured_text;
        }
        return false;
    }

    const QFileInfo file_info(trimmed_path);
    if (!file_info.exists())
    {
        if (outMessage)
        {
            *outMessage = QStringLiteral("Configured %1 executable does not exist: %2")
                              .arg(external_tool_config_.label, trimmed_path);
        }
        return false;
    }
    if (!file_info.isFile())
    {
        if (outMessage)
        {
            *outMessage = QStringLiteral("Configured %1 executable is not a file: %2")
                              .arg(external_tool_config_.label, trimmed_path);
        }
        return false;
    }

    bool candidate_matched = external_tool_config_.executable_candidates.isEmpty();
    QStringList expected_names;
    for (const QString& raw_candidate : external_tool_config_.executable_candidates)
    {
        const QString adjusted_candidate = platformAdjustedRequiredFile(raw_candidate);
        if (adjusted_candidate.isEmpty())
        {
            continue;
        }
        QString candidate_name = QFileInfo(adjusted_candidate).fileName();
        if (candidate_name.isEmpty())
        {
            candidate_name = adjusted_candidate;
        }
        if (!expected_names.contains(candidate_name, Qt::CaseInsensitive))
        {
            expected_names.push_back(candidate_name);
        }
        if (file_info.fileName().compare(candidate_name, Qt::CaseInsensitive) == 0)
        {
            candidate_matched = true;
        }
    }

    if (!candidate_matched)
    {
        if (outMessage)
        {
            const QString expected = expected_names.join(QStringLiteral(", "));
            const QString detail = QStringLiteral("Configured %1 executable is not supported. Expected: %2")
                                       .arg(external_tool_config_.label, expected);
            *outMessage = external_tool_config_.invalid_text.isEmpty()
                              ? detail
                              : external_tool_config_.invalid_text.arg(expected);
        }
        return false;
    }

    bool require_executable = external_tool_config_.require_executable;
#ifdef _WIN32
    const QString executable_name = file_info.fileName();
    if (require_executable && !external_tool_config_.append_platform_suffix &&
        !executable_name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
        !executable_name.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) &&
        !executable_name.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive))
    {
        require_executable = false;
    }
#endif
    if (require_executable && !file_info.isExecutable())
    {
        if (outMessage)
        {
            *outMessage = QStringLiteral("Configured %1 executable is not runnable: %2")
                              .arg(external_tool_config_.label, file_info.absoluteFilePath());
        }
        return false;
    }

    if (!external_tool_config_.required_files.isEmpty())
    {
        QString directory_message;
        if (!validateExternalToolDirectory(file_info.absolutePath(), &directory_message))
        {
            if (outMessage)
            {
                *outMessage = directory_message;
            }
            return false;
        }
    }

    if (outMessage)
    {
        *outMessage = external_tool_config_.configured_text;
    }
    return true;
}

bool ExtensionSettingsSection::validateExternalToolDirectory(const QString& directory, QString* outMessage) const
{
    const QString trimmed_directory = directory.trimmed();
    if (trimmed_directory.isEmpty())
    {
        if (outMessage)
        {
            *outMessage = external_tool_config_.not_configured_text;
        }
        return false;
    }

    const QFileInfo dir_info(trimmed_directory);
    if (!dir_info.exists() || !dir_info.isDir())
    {
        if (outMessage)
        {
            *outMessage = QStringLiteral("Configured %1 folder does not exist: %2")
                              .arg(external_tool_config_.label, trimmed_directory);
        }
        return false;
    }

    const QString executable_path = externalToolExecutableInDirectory(trimmed_directory);
    if (!external_tool_config_.executable_candidates.isEmpty())
    {
        if (executable_path.isEmpty())
        {
            if (outMessage)
            {
                const QString missing = external_tool_config_.executable_candidates.join(QStringLiteral(", "));
                const QString detail = QStringLiteral("Configured %1 folder is missing a supported executable: %2")
                                           .arg(external_tool_config_.label, missing);
                *outMessage = external_tool_config_.invalid_text.isEmpty()
                                  ? detail
                                  : external_tool_config_.invalid_text.arg(missing);
            }
            return false;
        }

        bool require_executable = external_tool_config_.require_executable;
#ifdef _WIN32
        const QString executable_name = QFileInfo(executable_path).fileName();
        if (require_executable && !external_tool_config_.append_platform_suffix &&
            !executable_name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
            !executable_name.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) &&
            !executable_name.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive))
        {
            require_executable = false;
        }
#endif
        if (require_executable && !QFileInfo(executable_path).isExecutable())
        {
            if (outMessage)
            {
                *outMessage = QStringLiteral("Configured %1 executable is not runnable: %2")
                                  .arg(external_tool_config_.label, executable_path);
            }
            return false;
        }
    }

    const QDir tool_dir(trimmed_directory);
    const auto resolved_file_info = [&tool_dir](const QString& required_file) {
        QFileInfo file_info(tool_dir.filePath(required_file));
        if (file_info.exists() && file_info.isFile())
        {
            return file_info;
        }
        if (required_file.contains(QLatin1Char('/')) || required_file.contains(QDir::separator()))
        {
            return file_info;
        }
        const QStringList entries = tool_dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& entry : entries)
        {
            if (entry.compare(required_file, Qt::CaseInsensitive) == 0)
            {
                return QFileInfo(tool_dir.filePath(entry));
            }
        }
        return file_info;
    };

    QStringList missing_files;
    QStringList not_runnable_files;
    for (const QString& raw_required_file : external_tool_config_.required_files)
    {
        const QString required_file = platformAdjustedRequiredFile(raw_required_file);
        const QFileInfo file_info = resolved_file_info(required_file);
        if (!file_info.exists() || !file_info.isFile())
        {
            missing_files.push_back(required_file);
            continue;
        }
        bool require_executable = external_tool_config_.require_executable;
#ifdef _WIN32
        if (require_executable && !external_tool_config_.append_platform_suffix &&
            !required_file.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        {
            require_executable = false;
        }
#endif
        if (require_executable && !file_info.isExecutable())
        {
            not_runnable_files.push_back(required_file);
        }
    }

    if (!missing_files.isEmpty())
    {
        if (outMessage)
        {
            const QString missing = missing_files.join(QStringLiteral(", "));
            const QString detail = QStringLiteral("Configured %1 folder is missing required executables: %2")
                                       .arg(external_tool_config_.label, missing);
            *outMessage =
                external_tool_config_.invalid_text.isEmpty() ? detail : external_tool_config_.invalid_text.arg(missing);
        }
        return false;
    }

    if (!not_runnable_files.isEmpty())
    {
        if (outMessage)
        {
            *outMessage = QStringLiteral("Configured %1 executables are not runnable: %2")
                              .arg(external_tool_config_.label, not_runnable_files.join(QStringLiteral(", ")));
        }
        return false;
    }

    if (outMessage)
    {
        *outMessage = external_tool_config_.configured_text;
    }
    return true;
}

ExtensionSettingsSection::ExternalToolResolution
ExtensionSettingsSection::resolveExternalTool(const QCborMap& params) const
{
    ExternalToolResolution resolution;
    const QString configured_path = currentExternalToolPath(params);
    resolution.configured_folder = currentExternalToolDirectory(params);

    if (!external_tool_config_.enabled)
    {
        resolution.valid = true;
        resolution.source = QStringLiteral("disabled");
        return resolution;
    }

    QString message;
    const SettingField* configured_field = fieldById(external_tool_config_.setting_key);
    const bool expects_executable = configured_field && configured_field->type == QStringLiteral("file_path");
    if (!configured_path.trimmed().isEmpty())
    {
        const QFileInfo configured_info(configured_path);
        if (expects_executable || (configured_info.exists() && configured_info.isFile()))
        {
            if (validateExternalToolExecutable(configured_path, &message))
            {
                const QFileInfo executable_info(configured_path);
                resolution.valid = true;
                resolution.configured_folder = executable_info.absolutePath();
                resolution.executable = executable_info.absoluteFilePath();
                resolution.source = QStringLiteral("settings");
                resolution.version = queryExternalToolVersion(resolution.executable);
                resolution.diagnostic = message;
                return resolution;
            }
            resolution.diagnostic = message;
            return resolution;
        }

        if (validateExternalToolDirectory(resolution.configured_folder, &message))
        {
            resolution.valid = true;
            resolution.executable = externalToolExecutableInDirectory(resolution.configured_folder);
            resolution.source = QStringLiteral("settings");
            resolution.version = queryExternalToolVersion(resolution.executable);
            resolution.diagnostic = message;
            return resolution;
        }
        resolution.diagnostic = message;
        return resolution;
    }

    for (const QString& candidate_directory : external_tool_config_.candidate_directories)
    {
        if (validateExternalToolDirectory(candidate_directory, &message))
        {
            resolution.valid = true;
            resolution.configured_folder = candidate_directory;
            resolution.executable = externalToolExecutableInDirectory(candidate_directory);
            resolution.source = QStringLiteral("candidate_directory");
            resolution.version = queryExternalToolVersion(resolution.executable);
            resolution.diagnostic = message;
            return resolution;
        }
    }

    const QString path_text = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    const QChar path_separator =
#ifdef _WIN32
        QLatin1Char(';');
#else
        QLatin1Char(':');
#endif
    for (const QString& path_directory : path_text.split(path_separator, Qt::SkipEmptyParts))
    {
        if (validateExternalToolDirectory(path_directory, &message))
        {
            resolution.valid = true;
            resolution.configured_folder = path_directory;
            resolution.executable = externalToolExecutableInDirectory(path_directory);
            resolution.source = QStringLiteral("path");
            resolution.version = queryExternalToolVersion(resolution.executable);
            resolution.diagnostic = message;
            return resolution;
        }
    }

    if (resolution.configured_folder.trimmed().isEmpty())
    {
        resolution.diagnostic = external_tool_config_.not_configured_text;
    }
    else
    {
        resolution.diagnostic = message.isEmpty() ? external_tool_config_.not_configured_text : message;
    }
    return resolution;
}

QString ExtensionSettingsSection::queryExternalToolVersion(const QString& executablePath) const
{
    if (executablePath.trimmed().isEmpty())
    {
        return {};
    }

    QProcess process;
    process.setProgram(executablePath);
    process.setArguments(external_tool_config_.version_arguments.isEmpty() ? QStringList{QStringLiteral("--version")}
                                                                           : external_tool_config_.version_arguments);
    process.start();
    if (!process.waitForStarted(500))
    {
        return {};
    }
    if (!process.waitForFinished(1000))
    {
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit)
    {
        return {};
    }

    const QString stdout_output = QString::fromUtf8(process.readAllStandardOutput());
    const QString stderr_output = QString::fromUtf8(process.readAllStandardError());
    QString output = stdout_output;
    if (!stderr_output.trimmed().isEmpty())
    {
        output += QStringLiteral("\n");
        output += stderr_output;
    }
    return versionFromOutput(output, external_tool_config_.version_pattern);
}

void ExtensionSettingsSection::applyExternalToolRecordToFields(const QCborMap& record)
{
    if (record.isEmpty() || !widget_host_ || extension_id_.empty())
    {
        return;
    }

    ExtensionUiPatchState patch_state;
    patch_state.extension_id = extension_id_;
    patch_state.session_id = "settings." + extension_id_;
    patch_state.request_id = nextPatchId(extension_id_, ++patch_sequence_).toStdString();

    for (auto it = record.cbegin(); it != record.cend(); ++it)
    {
        const QString id = it.key().toString();
        const SettingField* field = fieldById(id);
        if (!field || field->action || field->secret || field->sensitive)
        {
            continue;
        }
        patch_state.operations.push_back(
            ExtensionUiPatchOperation{id, QStringLiteral("value"), coerceSchemaFieldValue(*field, it.value())});
    }

    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    if (patch_state.patch_count == 0)
    {
        return;
    }

    std::string error;
    suppress_events_ = true;
    const bool applied = widget_host_->applyPatchState(patch_state, &error);
    suppress_events_ = false;
    if (!applied)
    {
        LOG_WARNING("Failed to update extension settings tool status for {}: {}", extension_id_, error);
    }
}

std::vector<ExtensionSettingsSection::SettingField>
ExtensionSettingsSection::dialogFieldsForAction(const SettingField& field, const QCborMap& snapshot,
                                                std::string* error) const
{
    const QJsonObject dialog = field.definition.value(QStringLiteral("dialog")).toObject();
    const QJsonArray field_ids = dialog.value(QStringLiteral("fields")).toArray();
    if (field_ids.isEmpty())
    {
        if (error)
        {
            *error = "Settings action dialog requires a non-empty fields array";
        }
        return {};
    }

    std::vector<SettingField> dialog_fields;
    dialog_fields.reserve(static_cast<std::size_t>(field_ids.size()));
    for (const QJsonValue& value : field_ids)
    {
        const QString field_id = value.toString().trimmed();
        const SettingField* source = fieldById(field_id);
        if (field_id.isEmpty() || !source || source->action)
        {
            if (error)
            {
                *error = "Settings action dialog references an unknown or unsupported field: " + field_id.toStdString();
            }
            return {};
        }

        SettingField copy = *source;
        copy.visible = true;
        copy.enabled = true;
        copy.group.clear();
        copy.group_style.clear();
        if (snapshot.contains(copy.id))
        {
            copy.default_value = coerceSchemaFieldValue(copy, snapshot.value(copy.id));
        }
        dialog_fields.push_back(std::move(copy));
    }
    return dialog_fields;
}

void ExtensionSettingsSection::openSettingsActionDialog(const SettingField& field)
{
    refreshExternalToolConfiguration();

    std::string error;
    const QCborMap snapshot = actionSnapshot(&error);
    if (!error.empty())
    {
        if (action_status_banner_)
        {
            action_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Error);
            action_status_banner_->setMessage(QString::fromStdString(error));
            action_status_banner_->show();
        }
        return;
    }

    const std::vector<SettingField> dialog_fields = dialogFieldsForAction(field, snapshot, &error);
    if (!error.empty())
    {
        if (action_status_banner_)
        {
            action_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Error);
            action_status_banner_->setMessage(QString::fromStdString(error));
            action_status_banner_->show();
        }
        return;
    }

    const QJsonObject dialog_config = field.definition.value(QStringLiteral("dialog")).toObject();
    const bool keep_open_on_accept = dialog_config.value(QStringLiteral("keep_open_on_accept")).toBool(false);
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setModal(true);
    const QString fallback_title = field.definition.value(QStringLiteral("title")).toString(field.label);
    dialog->setWindowTitle(dialog_config.value(QStringLiteral("title")).toString(fallback_title));
    dialog->setMinimumWidth(
        dialog_config.value(QStringLiteral("minimum_width")).toInt(keep_open_on_accept ? 720 : 480));

    auto* layout = new QVBoxLayout(dialog);
    const auto& spacing = an::ui::tokens().spacing;
    layout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);
    layout->setSpacing(spacing.md);

    ExtensionUiDescription description;
    const QString action_token = sanitizeObjectToken(field.id.toStdString());
    if (!buildDescriptionForFields(
            dialog_fields, QStringLiteral("settings.dialog.%1.%2").arg(object_token_, action_token),
            QStringLiteral("settings.dialog.describe.%1.%2").arg(object_token_, action_token), &description, &error))
    {
        dialog->deleteLater();
        if (action_status_banner_)
        {
            action_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Error);
            action_status_banner_->setMessage(QString::fromStdString(error));
            action_status_banner_->show();
        }
        return;
    }

    auto* dialog_host = new an::ui::extensions::ExtensionUiWidgetHost(dialog);
    dialog_host->setObjectName(QStringLiteral("anExtensionSettingsDialogHost_%1").arg(action_token));
    if (!dialog_host->applyDescription(description, &error))
    {
        dialog->deleteLater();
        if (action_status_banner_)
        {
            action_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Error);
            action_status_banner_->setMessage(QString::fromStdString(error));
            action_status_banner_->show();
        }
        return;
    }

    auto* scroll_area = new QScrollArea(dialog);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    scroll_area->setWidget(dialog_host);
    layout->addWidget(scroll_area, 1);

    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    if (auto* accept_button = button_box->button(QDialogButtonBox::Ok))
    {
        const QString accept_label = dialog_config.value(QStringLiteral("accept_label"))
                                         .toString(field.label.isEmpty() ? field.id : field.label);
        accept_button->setText(accept_label);
        accept_button->setProperty("anButtonVariant", QStringLiteral("primary"));
    }
    layout->addWidget(button_box);

    const QString field_id = field.id;
    QObject::connect(dialog, &QObject::destroyed, this, [this, dialog_host]() {
        if (active_action_dialog_host_ == dialog_host)
        {
            active_action_dialog_host_.clear();
            active_action_dialog_buttons_.clear();
            active_action_dialog_fields_.clear();
        }
    });

    QObject::connect(button_box, &QDialogButtonBox::accepted, dialog,
                     [this, dialog, dialog_host, button_box, field_id, keep_open_on_accept, dialog_fields]() {
                         const SettingField* action_field = fieldById(field_id);
                         if (!action_field)
                         {
                             dialog->reject();
                             return;
                         }

                         std::string snapshot_error;
                         QCborMap merged_snapshot = actionSnapshot(&snapshot_error);
                         QCborMap dialog_params;
                         if (snapshot_error.empty())
                         {
                             (void)dialog_host->snapshotValidationParams(&dialog_params, &snapshot_error);
                         }
                         if (!snapshot_error.empty())
                         {
                             if (action_status_banner_)
                             {
                                 action_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Error);
                                 action_status_banner_->setMessage(QString::fromStdString(snapshot_error));
                                 action_status_banner_->show();
                             }
                             return;
                         }

                         for (auto it = dialog_params.cbegin(); it != dialog_params.cend(); ++it)
                         {
                             merged_snapshot.insert(it.key().toString(), it.value());
                         }
                         if (keep_open_on_accept)
                         {
                             active_action_dialog_host_ = dialog_host;
                             active_action_dialog_buttons_ = button_box;
                             active_action_dialog_fields_ = dialog_fields;
                             button_box->setEnabled(false);
                             startSettingsAction(*action_field, merged_snapshot);
                             if (!actionInProgress())
                             {
                                 button_box->setEnabled(true);
                             }
                             return;
                         }
                         dialog->accept();
                         startSettingsAction(*action_field, merged_snapshot);
                     });
    QObject::connect(button_box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    dialog->open();
}

void ExtensionSettingsSection::onWidgetEvent(const QString& widgetId, const QString& eventName, const QCborValue&,
                                             const bool)
{
    if (suppress_events_)
    {
        return;
    }

    if (eventName == QStringLiteral("clicked"))
    {
        const SettingField* field = fieldById(widgetId);
        if (field && field->action)
        {
            if (field->definition.value(QStringLiteral("dialog")).isObject())
            {
                openSettingsActionDialog(*field);
                return;
            }
            startSettingsAction(*field);
            return;
        }
    }

    std::string error;
    if (!evaluateAndApplyConditions(&error))
    {
        LOG_WARNING("Failed to evaluate extension settings visibility rules for {}: {}", extension_id_, error);
    }
    if (external_tool_config_.enabled && widgetId == external_tool_config_.setting_key)
    {
        refreshExternalToolConfiguration();
    }
    settingsModified();
}
