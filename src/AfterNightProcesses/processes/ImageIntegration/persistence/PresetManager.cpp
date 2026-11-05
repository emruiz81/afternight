// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PresetManager.h"
#include "SessionDocument.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <array>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(PresetManager)

namespace
{
constexpr std::array<const char*, 8> kPresetConfigurationKeys = {
    "lightCalibrationCacheMode", "calibrationPolicy",    "groupingOptions",          "lightIntegrationGroupingOptions",
    "stackingSettings",          "registrationSettings", "lightCalibrationSettings", "calibrationOverrides"};

[[nodiscard]] QJsonObject presetConfigurationFromJson(const QJsonObject& json)
{
    QJsonObject preset;
    if (json.contains(QStringLiteral("version")))
    {
        preset.insert(QStringLiteral("version"), json.value(QStringLiteral("version")));
    }

    for (const char* key : kPresetConfigurationKeys)
    {
        const QString qkey = QString::fromLatin1(key);
        if (json.contains(qkey))
        {
            preset.insert(qkey, json.value(qkey));
        }
    }

    return preset;
}

[[nodiscard]] QJsonObject applyPresetConfiguration(const QJsonObject& baseState, const QJsonObject& presetJson)
{
    QJsonObject merged = baseState;
    const QJsonObject filteredPreset = presetConfigurationFromJson(presetJson);
    for (const char* key : kPresetConfigurationKeys)
    {
        const QString qkey = QString::fromLatin1(key);
        if (filteredPreset.contains(qkey))
        {
            merged.insert(qkey, filteredPreset.value(qkey));
        }
    }

    if (filteredPreset.contains(QStringLiteral("version")))
    {
        merged.insert(QStringLiteral("version"), filteredPreset.value(QStringLiteral("version")));
    }

    return merged;
}

/// The key a preset file states its envelope's schema under, and the key its configuration is held under.
constexpr char kPresetSchemaKey[] = "presetSchema";
constexpr char kPresetConfigurationKey[] = "configuration";

[[nodiscard]] QString presetContext(const QString& name)
{
    return QStringLiteral("preset '%1'").arg(name);
}

/// A preset named with the file it is read from, for a refusal.
[[nodiscard]] QString presetFileContext(const QString& name, const QString& path)
{
    return QStringLiteral("preset '%1' ('%2')").arg(name, QDir::toNativeSeparators(path));
}

/// The format a preset's envelope states: zero for one that states none - the configuration itself, with no envelope
/// around it - or -1 for a stated format that is not a positive integer.
[[nodiscard]] int presetFormat(const QJsonObject& stored)
{
    const QJsonValue value = stored.value(QLatin1String(kPresetSchemaKey));
    if (value.isUndefined())
    {
        return 0;
    }
    if (!value.isDouble() || value.toDouble() != static_cast<double>(value.toInt()) || value.toInt() <= 0)
    {
        return -1;
    }
    return value.toInt();
}

[[nodiscard]] QJsonObject presetEnvelope(const QJsonObject& document)
{
    QJsonObject envelope;
    envelope.insert(QLatin1String(kPresetSchemaKey), ImageIntegration::kSessionSchemaVersion);
    envelope.insert(QLatin1String(kPresetConfigurationKey), document);
    return envelope;
}

[[nodiscard]] bool writeAllAndCommit(QSaveFile& file, const QByteArray& payload)
{
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    if (file.write(payload) != payload.size())
    {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

[[nodiscard]] QString normalizedPathForContainment(QString path)
{
    path = QDir::cleanPath(std::move(path));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

[[nodiscard]] std::optional<QString> safePresetPath(const QString& storageDir, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || PresetManager::sanitizeName(trimmed) != trimmed)
    {
        return std::nullopt;
    }

    const QDir dir(storageDir);
    const QString storageRoot = normalizedPathForContainment(QFileInfo(dir.absolutePath()).absoluteFilePath());
    const QString presetPath = QFileInfo(dir.filePath(trimmed + QStringLiteral(".json"))).absoluteFilePath();
    const QString normalizedPresetPath = normalizedPathForContainment(presetPath);
    const QString requiredPrefix = storageRoot + QLatin1Char('/');
    if (!normalizedPresetPath.startsWith(requiredPrefix))
    {
        return std::nullopt;
    }

    return presetPath;
}
} // namespace

PresetManager::PresetManager(const QString& storageDir, QObject* parent) : QObject(parent), m_storageDir(storageDir)
{
    QDir dir(m_storageDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
}

PresetManager::~PresetManager() = default;

bool PresetManager::savePreset(const QString& name, const IntegrationDataModel& model)
{
    m_lastError.clear();
    const auto presetPath = safePresetPath(m_storageDir, name);
    if (!presetPath.has_value())
        return false;

    const ImageIntegration::SessionDocumentResult document =
        ImageIntegration::writeSessionDocument(presetConfigurationFromJson(model.toJson()), presetContext(name));
    if (!document.ok())
    {
        m_lastError = document.refusal;
        return false;
    }

    QSaveFile file(*presetPath);
    return writeAllAndCommit(file, QJsonDocument(presetEnvelope(*document.document)).toJson());
}

bool PresetManager::loadPreset(const QString& name, IntegrationDataModel& model)
{
    const auto appliedState = buildAppliedState(name, model);
    if (!appliedState.has_value())
    {
        return false;
    }

    model.fromJson(*appliedState);
    return true;
}

std::optional<QJsonObject> PresetManager::buildAppliedState(const QString& name, const IntegrationDataModel& model)
{
    m_lastError.clear();
    const auto presetPath = safePresetPath(m_storageDir, name);
    if (!presetPath.has_value())
    {
        return std::nullopt;
    }

    QFile file(*presetPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isNull() || !doc.isObject())
    {
        m_lastError = QStringLiteral("The %1 is not a document this version can read.").arg(presetContext(name));
        return std::nullopt;
    }

    // A preset is read only at the format its writer writes; one of another format, or one that states none, is
    // refused and left as it is.
    const QJsonObject stored = doc.object();
    const QString context = presetFileContext(name, *presetPath);
    const int format = presetFormat(stored);
    if (format != ImageIntegration::kSessionSchemaVersion)
    {
        m_lastError = ImageIntegration::sessionFormatRefusal(context, format) + QStringLiteral(" It was not changed.");
        LOG_WARNING("PresetManager: {}", m_lastError.toStdString());
        return std::nullopt;
    }

    const ImageIntegration::SessionDocumentResult read =
        ImageIntegration::readSessionDocument(stored.value(QLatin1String(kPresetConfigurationKey)).toObject(), context);
    if (!read.ok())
    {
        m_lastError = read.refusal;
        LOG_WARNING("PresetManager: {}", m_lastError.toStdString());
        return std::nullopt;
    }

    return applyPresetConfiguration(model.toJson(), *read.document);
}

const QString& PresetManager::lastError() const noexcept
{
    return m_lastError;
}

QStringList PresetManager::listPresets() const
{
    QDir dir(m_storageDir);
    QStringList filters;
    filters << "*.json";
    QStringList files = dir.entryList(filters, QDir::Files);

    QStringList presets;
    for (const auto& f : files)
    {
        presets << QFileInfo(f).completeBaseName();
    }
    return presets;
}

bool PresetManager::deletePreset(const QString& name)
{
    const auto presetPath = safePresetPath(m_storageDir, name);
    if (!presetPath.has_value())
    {
        return false;
    }

    return QFile::remove(*presetPath);
}

bool PresetManager::presetExists(const QString& name) const
{
    const auto presetPath = safePresetPath(m_storageDir, name);
    return presetPath.has_value() && QFile::exists(*presetPath);
}

QString PresetManager::sanitizeName(const QString& name)
{
    QString sanitized = name.trimmed();
    // Remove characters that are invalid in filenames
    static const QRegularExpression invalidChars(QStringLiteral("[<>:\"/\\\\|?*]"));
    sanitized.remove(invalidChars);
    // Limit length
    if (sanitized.length() > 100)
    {
        sanitized = sanitized.left(100);
    }
    return sanitized;
}
