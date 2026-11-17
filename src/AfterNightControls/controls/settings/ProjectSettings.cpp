// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stored project settings and the texts they are kept in.

#include "stdafx.hpp"
#include "controls/settings/ProjectSettings.h"

#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>

namespace an::ui
{

namespace
{

/// A stored switch: `1` on, `0` off, anything else the default.
[[nodiscard]] bool storedSwitch(const QString& text, const bool fallback)
{
    const QString trimmed = text.trimmed();
    if (trimmed == QLatin1String("1"))
    {
        return true;
    }
    if (trimmed == QLatin1String("0"))
    {
        return false;
    }
    return fallback;
}

[[nodiscard]] QString switchText(const bool on)
{
    return on ? QStringLiteral("1") : QStringLiteral("0");
}

} // namespace

QString defaultProjectsLocation()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
    {
        documents = QDir::homePath();
    }
    return QDir::cleanPath(QDir(documents).filePath(QStringLiteral("AfterNight Projects")));
}

std::optional<history::StepCompression> stepCompressionFromName(const QString& name)
{
    const QString trimmed = name.trimmed().toLower();
    for (const history::StepCompression compression :
         {history::StepCompression::None, history::StepCompression::Lz4, history::StepCompression::Zstd})
    {
        const std::string_view stated = history::stepCompressionName(compression);
        if (trimmed == QLatin1String(stated.data(), static_cast<qsizetype>(stated.size())))
        {
            return compression;
        }
    }
    return std::nullopt;
}

bool isValidProjectName(const QString& name)
{
    constexpr qsizetype kLongest = 128;
    if (name.isEmpty() || name.size() > kLongest || name != name.trimmed() || name.endsWith(QLatin1Char('.')))
    {
        return false;
    }
    for (const QChar character : name)
    {
        if (character.unicode() < 0x20u || QStringLiteral("<>:\"/\\|?*").contains(character))
        {
            return false;
        }
    }
    // The device names Windows reserves, with or without an extension.
    const QString stem = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    for (const char* reserved : {"CON", "PRN", "AUX", "NUL"})
    {
        if (stem == QLatin1String(reserved))
        {
            return false;
        }
    }
    const bool numbered =
        stem.size() == 4 && (stem.startsWith(QLatin1String("COM")) || stem.startsWith(QLatin1String("LPT")));
    return !(numbered && stem.at(3) >= QLatin1Char('1') && stem.at(3) <= QLatin1Char('9'));
}

ProjectSettings readProjectSettings(const SettingReader& read)
{
    ProjectSettings settings;
    settings.defaultLocation = defaultProjectsLocation();
    settings.createForIntegrationJobs = SettingsDefaults::PROJECTS_CREATE_FOR_INTEGRATION_JOBS;
    settings.openLastOnStartup = SettingsDefaults::PROJECTS_OPEN_LAST_ON_STARTUP;
    settings.compactAskThresholdGb = SettingsDefaults::PROJECTS_COMPACT_ASK_THRESHOLD_GB;
    if (!read)
    {
        return settings;
    }
    const QString location = read(QString::fromLatin1(SettingsKeys::PROJECTS_DEFAULT_LOCATION), QString()).trimmed();
    if (!location.isEmpty())
    {
        settings.defaultLocation = QDir::cleanPath(QDir::fromNativeSeparators(location));
    }
    settings.createForIntegrationJobs =
        storedSwitch(read(QString::fromLatin1(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS), QString()),
                     SettingsDefaults::PROJECTS_CREATE_FOR_INTEGRATION_JOBS);
    settings.openLastOnStartup =
        storedSwitch(read(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QString()),
                     SettingsDefaults::PROJECTS_OPEN_LAST_ON_STARTUP);
    settings.historyCompression =
        stepCompressionFromName(read(QString::fromLatin1(SettingsKeys::HISTORY_COMPRESSION),
                                     QString::fromLatin1(SettingsDefaults::HISTORY_COMPRESSION)))
            .value_or(history::StepCompression::Lz4);
    bool whole = false;
    const int threshold =
        read(QString::fromLatin1(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB), QString()).trimmed().toInt(&whole);
    if (whole && threshold >= 1)
    {
        settings.compactAskThresholdGb = threshold;
    }
    return settings;
}

void writeProjectSettings(const ProjectSettings& settings, const SettingWriter& write)
{
    if (!write)
    {
        return;
    }
    write(QString::fromLatin1(SettingsKeys::PROJECTS_DEFAULT_LOCATION), settings.defaultLocation);
    write(QString::fromLatin1(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS),
          switchText(settings.createForIntegrationJobs));
    write(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), switchText(settings.openLastOnStartup));
    const std::string_view compression = history::stepCompressionName(settings.historyCompression);
    write(QString::fromLatin1(SettingsKeys::HISTORY_COMPRESSION),
          QString::fromLatin1(compression.data(), static_cast<qsizetype>(compression.size())));
    write(QString::fromLatin1(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB),
          QString::number((std::max)(settings.compactAskThresholdGb, 1)));
}

} // namespace an::ui
