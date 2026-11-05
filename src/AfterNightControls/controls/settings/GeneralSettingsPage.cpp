// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GeneralSettingsPage.h"
#include "PathPickerWidget.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "../GlobalSettingsPersistence.h"
#include "ProcessingLimits.h"
#include "controls/IAppContext.h"
#include "app/FileDialogUtils.h"
#include "theme/AnDesignTokens.h"
#include "app/FilePolicies.h"

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <algorithm>
#include <cstdint>
#include <optional>

W_OBJECT_IMPL(GeneralSettingsPage)

namespace
{
const QString kSettingsImportExportContext = QStringLiteral("__SETTINGS_IMPORT_EXPORT__");

/// The format a settings file states in its `version` member: the one Export Settings writes and the only one Import
/// Settings reads.
constexpr const char* kSettingsFileFormat = "1.0";

// Save format options (value -> display text)
const QList<QPair<QString, QString>> SAVE_FORMATS = {{"tiff", QObject::tr("TIFF")},
                                                     {"fits", QObject::tr("FITS")},
                                                     {"png", QObject::tr("PNG")},
                                                     {"xisf", QObject::tr("XISF")},
                                                     {"jpeg", QObject::tr("JPEG")}};

// Language options (value -> display text)
const QList<QPair<QString, QString>> LANGUAGES = {
    {"system", QObject::tr("System Default")},
    {"en", QObject::tr("English")},
    // Additional languages can be added here as translations become available
};

constexpr int kDefaultWbMode = 1; // daylight

QString normalizeToken(const QString& value)
{
    return value.trimmed().toLower();
}

QString wbModeToString(int mode)
{
    switch (mode)
    {
    case 0:
        return "none";
    case 1:
        return "daylight";
    case 2:
        return "camera_as_shot";
    case 3:
        return "auto_wb";
    case 4:
        return "custom";
    default:
        return "daylight";
    }
}

QString wbModeFromStoredValue(const QString& stored)
{
    bool ok = false;
    const int mode = stored.toInt(&ok);
    if (ok)
    {
        return wbModeToString(mode);
    }

    const QString token = normalizeToken(stored);
    if (token == "none" || token == "daylight" || token == "camera_as_shot" || token == "auto_wb" || token == "custom")
    {
        return token;
    }

    return "daylight";
}

int wbModeFromJsonValue(const QJsonValue& value, int fallback)
{
    if (value.isDouble())
    {
        return std::clamp(value.toInt(fallback), 0, 4);
    }

    if (value.isString())
    {
        const QString token = normalizeToken(value.toString());
        if (token == "none")
            return 0;
        if (token == "daylight")
            return 1;
        if (token == "camera_as_shot")
            return 2;
        if (token == "auto_wb")
            return 3;
        if (token == "custom")
            return 4;

        bool ok = false;
        const int parsed = token.toInt(&ok);
        if (ok)
        {
            return std::clamp(parsed, 0, 4);
        }
    }

    return fallback;
}

QIcon settingsPageIcon(const QString& iconName)
{
    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            return iconProvider->getIcon(iconName);
        }
    }

    return {};
}

QStringList settingsJsonFilters()
{
    return {QObject::tr("JSON Files (*.json)"), QObject::tr("All Files (*)")};
}
} // namespace

GeneralSettingsPage::GeneralSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString GeneralSettingsPage::pageTitle() const
{
    return tr("General");
}

QIcon GeneralSettingsPage::pageIcon() const
{
    return settingsPageIcon(QStringLiteral("settings"));
}

QStringList GeneralSettingsPage::searchKeywords() const
{
    return QStringList{tr("General"), tr("Language"),  tr("Save"),      tr("Format"),     tr("Recent"),
                       tr("Files"),   tr("Processes"), tr("Temporary"), tr("Temp"),       tr("Import"),
                       tr("Export"),  tr("Reset"),     tr("Updates"),   tr("Extensions"), tr("Startup")};
}

void GeneralSettingsPage::setupUi()
{
    const auto& t = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t.spacing.lg);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === Language & Format Section ===
    auto* basicGroup = new QGroupBox(tr("Basic Settings"), this);
    auto* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setSpacing(t.spacing.md);

    // Language dropdown
    m_LanguageCombo = new QComboBox(basicGroup);
    for (const auto& [value, display] : LANGUAGES)
    {
        m_LanguageCombo->addItem(display, value);
    }
    m_LanguageCombo->setEnabled(false);
    m_LanguageCombo->setToolTip(tr("Language selection is not yet implemented"));
    basicLayout->addRow(tr("Language:"), m_LanguageCombo);

    // Info about language (restart required)
    auto* langInfo = createInfoLabel(tr("Localization is not yet implemented"));
    basicLayout->addRow("", langInfo);

    // Default save format
    m_DefaultSaveFormatCombo = new QComboBox(basicGroup);
    for (const auto& [value, display] : SAVE_FORMATS)
    {
        m_DefaultSaveFormatCombo->addItem(display, value);
    }
    basicLayout->addRow(tr("Default Save Format:"), m_DefaultSaveFormatCombo);

    // Recent files limit
    m_RecentFilesLimitSpin = new QSpinBox(basicGroup);
    m_RecentFilesLimitSpin->setRange(1, 100);
    m_RecentFilesLimitSpin->setSuffix(tr(" files"));
    m_RecentFilesLimitSpin->setToolTip(tr("Maximum number of recent files to remember"));
    basicLayout->addRow(tr("Recent Files Limit:"), m_RecentFilesLimitSpin);

    // Recent processes limit
    m_RecentProcessesLimitSpin = new QSpinBox(basicGroup);
    m_RecentProcessesLimitSpin->setRange(1, 100);
    m_RecentProcessesLimitSpin->setSuffix(tr(" processes"));
    m_RecentProcessesLimitSpin->setToolTip(tr("Maximum number of recent processes to remember"));
    basicLayout->addRow(tr("Recent Processes Limit:"), m_RecentProcessesLimitSpin);

    mainLayout->addWidget(basicGroup);

    // === Behavior Section ===
    auto* behaviorGroup = new QGroupBox(tr("Behavior"), this);
    auto* behaviorLayout = new QVBoxLayout(behaviorGroup);
    behaviorLayout->setSpacing(t.spacing.sm);

    m_ConfirmCloseUnsavedCheck = new QCheckBox(tr("Confirm before closing unsaved images"), behaviorGroup);
    m_ConfirmCloseUnsavedCheck->setToolTip(tr("Show a confirmation dialog when closing images with unsaved changes"));
    behaviorLayout->addWidget(m_ConfirmCloseUnsavedCheck);

    m_ClearTempOnExitCheck = new QCheckBox(tr("Clear temporary files on exit"), behaviorGroup);
    m_ClearTempOnExitCheck->setToolTip(tr("Automatically delete temporary files when the application closes"));
    behaviorLayout->addWidget(m_ClearTempOnExitCheck);

    mainLayout->addWidget(behaviorGroup);

    // === Updates Section ===
    auto* updatesGroup = new QGroupBox(tr("Updates"), this);
    updatesGroup->setObjectName(QStringLiteral("anGeneralSettingsUpdatesGroup"));
    auto* updatesLayout = new QVBoxLayout(updatesGroup);
    updatesLayout->setSpacing(t.spacing.sm);

    m_CheckAppUpdatesOnStartupCheck = new QCheckBox(tr("Check for AfterNight updates on startup"), updatesGroup);
    m_CheckAppUpdatesOnStartupCheck->setObjectName(QStringLiteral("anGeneralSettingsCheckAppUpdatesOnStartup"));
    m_CheckAppUpdatesOnStartupCheck->setEnabled(false);
    m_CheckAppUpdatesOnStartupCheck->setToolTip(tr("Application update checks are not implemented yet."));
    updatesLayout->addWidget(m_CheckAppUpdatesOnStartupCheck);

    m_CheckExtensionUpdatesOnStartupCheck = new QCheckBox(tr("Check for extension updates on startup"), updatesGroup);
    m_CheckExtensionUpdatesOnStartupCheck->setObjectName(
        QStringLiteral("anGeneralSettingsCheckExtensionUpdatesOnStartup"));
    m_CheckExtensionUpdatesOnStartupCheck->setToolTip(
        tr("Check the official extension repository in the background after AfterNight starts."));
    updatesLayout->addWidget(m_CheckExtensionUpdatesOnStartupCheck);

    mainLayout->addWidget(updatesGroup);

    // === Temporary Files Section ===
    auto* tempGroup = new QGroupBox(tr("Temporary Files"), this);
    auto* tempLayout = new QFormLayout(tempGroup);
    tempLayout->setSpacing(t.spacing.md);

    m_TempLocationPicker = new PathPickerWidget(PathPickerWidget::Mode::Directory, tempGroup);
    m_TempLocationPicker->setPlaceholderText(tr("Select temporary files directory..."));
    m_TempLocationPicker->setDialogTitle(tr("Select Temporary Files Directory"));
    tempLayout->addRow(tr("Location:"), m_TempLocationPicker);

    auto* tempInfo = createInfoLabel(tr("Used for intermediate processing files. Ensure sufficient disk space."));
    tempLayout->addRow("", tempInfo);

    mainLayout->addWidget(tempGroup);

    // === Settings Management Section ===
    auto* managementGroup = new QGroupBox(tr("Settings Management"), this);
    auto* managementLayout = new QHBoxLayout(managementGroup);
    managementLayout->setSpacing(t.spacing.md);

    m_ImportButton = new QPushButton(tr("Import Settings..."), managementGroup);
    m_ImportButton->setToolTip(tr("Import settings from a JSON file"));
    managementLayout->addWidget(m_ImportButton);

    m_ExportButton = new QPushButton(tr("Export Settings..."), managementGroup);
    m_ExportButton->setToolTip(tr("Export current settings to a JSON file"));
    managementLayout->addWidget(m_ExportButton);

    managementLayout->addStretch();

    mainLayout->addWidget(managementGroup);

    // Add stretch to push content to top
    mainLayout->addStretch(1);
}

void GeneralSettingsPage::connectSignals()
{
    // Track changes for all controls
    trackChanges(m_LanguageCombo);
    trackChanges(m_DefaultSaveFormatCombo);
    trackChanges(m_RecentFilesLimitSpin);
    trackChanges(m_RecentProcessesLimitSpin);
    trackChanges(m_ConfirmCloseUnsavedCheck);
    trackChanges(m_ClearTempOnExitCheck);
    trackChanges(m_CheckAppUpdatesOnStartupCheck);
    trackChanges(m_CheckExtensionUpdatesOnStartupCheck);

    // PathPickerWidget emits pathChanged
    connect(m_TempLocationPicker, &PathPickerWidget::pathChanged, this, &GeneralSettingsPage::markAsModified);

    // Import/Export buttons
    connect(m_ImportButton, &QPushButton::clicked, this, &GeneralSettingsPage::onImportSettings);
    connect(m_ExportButton, &QPushButton::clicked, this, &GeneralSettingsPage::onExportSettings);
}

void GeneralSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();

    if (!m_Settings)
    {
        resetToDefaults();
        return;
    }

    // Language
    QString language = m_Settings->select(SettingsKeys::GENERAL_LANGUAGE, SettingsDefaults::GENERAL_LANGUAGE);
    int langIndex = m_LanguageCombo->findData(language);
    m_LanguageCombo->setCurrentIndex(langIndex >= 0 ? langIndex : 0);

    // Default save format
    QString format =
        m_Settings->select(SettingsKeys::GENERAL_DEFAULT_SAVE_FORMAT, SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT);
    int formatIndex = m_DefaultSaveFormatCombo->findData(format);
    m_DefaultSaveFormatCombo->setCurrentIndex(formatIndex >= 0 ? formatIndex : 0);

    // Recent files limit
    QString limitStr = m_Settings->select(SettingsKeys::GENERAL_RECENT_FILES_LIMIT,
                                          QString::number(SettingsDefaults::GENERAL_RECENT_FILES_LIMIT));
    m_RecentFilesLimitSpin->setValue(limitStr.toInt());

    // Recent processes limit
    const QString processLimitStr =
        m_Settings->select(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT,
                           QString::number(SettingsDefaults::GENERAL_RECENT_PROCESSES_LIMIT));
    m_RecentProcessesLimitSpin->setValue(processLimitStr.toInt());

    // Confirm close unsaved
    QString confirmStr = m_Settings->select(SettingsKeys::GENERAL_CONFIRM_CLOSE_UNSAVED,
                                            SettingsDefaults::GENERAL_CONFIRM_CLOSE_UNSAVED ? "1" : "0");
    m_ConfirmCloseUnsavedCheck->setChecked(confirmStr == "1");

    // Temp files location
    QString defaultTemp = getDefaultTempLocation();
    QString tempLocation = m_Settings->select(SettingsKeys::GENERAL_TEMP_FILES_LOCATION, defaultTemp);
    m_TempLocationPicker->setPath(tempLocation.isEmpty() ? defaultTemp : tempLocation);

    // Clear temp on exit
    QString clearStr = m_Settings->select(SettingsKeys::GENERAL_CLEAR_TEMP_ON_EXIT,
                                          SettingsDefaults::GENERAL_CLEAR_TEMP_ON_EXIT ? "1" : "0");
    m_ClearTempOnExitCheck->setChecked(clearStr == "1");

    // Startup update checks
    const QString appUpdateStr = m_Settings->select(SettingsKeys::GENERAL_CHECK_APP_UPDATES_ON_STARTUP,
                                                    SettingsDefaults::GENERAL_CHECK_APP_UPDATES_ON_STARTUP ? "1" : "0");
    m_CheckAppUpdatesOnStartupCheck->setChecked(appUpdateStr == "1");

    const QString extensionUpdateStr =
        m_Settings->select(SettingsKeys::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP,
                           SettingsDefaults::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP ? "1" : "0");
    m_CheckExtensionUpdatesOnStartupCheck->setChecked(extensionUpdateStr == "1");

    clearModified();
}

void GeneralSettingsPage::saveSettings()
{
    if (!m_Settings)
    {
        return;
    }

    // Language
    QString language = m_LanguageCombo->currentData().toString();
    m_Settings->upsert(SettingsKeys::GENERAL_LANGUAGE, language);

    // Default save format
    QString format = m_DefaultSaveFormatCombo->currentData().toString();
    m_Settings->upsert(SettingsKeys::GENERAL_DEFAULT_SAVE_FORMAT, format);

    // Recent files limit
    m_Settings->upsert(SettingsKeys::GENERAL_RECENT_FILES_LIMIT, QString::number(m_RecentFilesLimitSpin->value()));

    // Recent processes limit
    m_Settings->upsert(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT,
                       QString::number(m_RecentProcessesLimitSpin->value()));

    // Confirm close unsaved
    m_Settings->upsert(SettingsKeys::GENERAL_CONFIRM_CLOSE_UNSAVED,
                       m_ConfirmCloseUnsavedCheck->isChecked() ? "1" : "0");

    // Temp files location
    m_Settings->upsert(SettingsKeys::GENERAL_TEMP_FILES_LOCATION, m_TempLocationPicker->path());

    // Clear temp on exit
    m_Settings->upsert(SettingsKeys::GENERAL_CLEAR_TEMP_ON_EXIT, m_ClearTempOnExitCheck->isChecked() ? "1" : "0");

    // Startup update checks
    m_Settings->upsert(SettingsKeys::GENERAL_CHECK_APP_UPDATES_ON_STARTUP,
                       m_CheckAppUpdatesOnStartupCheck->isChecked() ? "1" : "0");
    m_Settings->upsert(SettingsKeys::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP,
                       m_CheckExtensionUpdatesOnStartupCheck->isChecked() ? "1" : "0");

    if (!m_PendingImportUpserts.isEmpty())
    {
        for (auto it = m_PendingImportUpserts.constBegin(); it != m_PendingImportUpserts.constEnd(); ++it)
        {
            m_Settings->upsert(it.key(), it.value());
        }

        // An imported RAW setting reaches the reads asked for after the import as one policy of every RAW setting
        // stored now - the mode, the gains and auto-rotate together - published as the Raw Images page publishes it.
        const bool hasRawUpdate = m_PendingImportUpserts.contains(SettingsKeys::RAW_WHITE_BALANCE_MODE) ||
                                  m_PendingImportUpserts.contains(SettingsKeys::RAW_CUSTOM_WB_R) ||
                                  m_PendingImportUpserts.contains(SettingsKeys::RAW_CUSTOM_WB_G) ||
                                  m_PendingImportUpserts.contains(SettingsKeys::RAW_CUSTOM_WB_B) ||
                                  m_PendingImportUpserts.contains(SettingsKeys::RAW_AUTO_ROTATE);
        auto* context = an::ui::appContext();
        an::ui::IRawOpenPolicyService* rawPolicies = context ? context->getRawOpenPolicyService() : nullptr;
        if (hasRawUpdate && rawPolicies != nullptr)
        {
            const an::ui::RawOpenSettings imported = an::ui::readRawOpenSettings(
                [this](const QString& key, const QString& fallback) { return m_Settings->select(key, fallback); });
            const an::Status published = rawPolicies->setRawOpenSettings(imported);
            if (!published.ok())
            {
                LOG_WARNING("The imported RAW settings were stored and could not be applied to new reads: {}",
                            published.message());
            }
        }

        m_PendingImportUpserts.clear();
    }

    clearModified();
}

void GeneralSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();

    // Language
    int langIndex = m_LanguageCombo->findData(QString(SettingsDefaults::GENERAL_LANGUAGE));
    m_LanguageCombo->setCurrentIndex(langIndex >= 0 ? langIndex : 0);

    // Default save format
    int formatIndex = m_DefaultSaveFormatCombo->findData(QString(SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT));
    m_DefaultSaveFormatCombo->setCurrentIndex(formatIndex >= 0 ? formatIndex : 0);

    // Recent files limit
    m_RecentFilesLimitSpin->setValue(SettingsDefaults::GENERAL_RECENT_FILES_LIMIT);

    // Recent processes limit
    m_RecentProcessesLimitSpin->setValue(SettingsDefaults::GENERAL_RECENT_PROCESSES_LIMIT);

    // Confirm close unsaved
    m_ConfirmCloseUnsavedCheck->setChecked(SettingsDefaults::GENERAL_CONFIRM_CLOSE_UNSAVED);

    // Temp files location
    m_TempLocationPicker->setPath(getDefaultTempLocation());

    // Clear temp on exit
    m_ClearTempOnExitCheck->setChecked(SettingsDefaults::GENERAL_CLEAR_TEMP_ON_EXIT);

    // Startup update checks
    m_CheckAppUpdatesOnStartupCheck->setChecked(SettingsDefaults::GENERAL_CHECK_APP_UPDATES_ON_STARTUP);
    m_CheckExtensionUpdatesOnStartupCheck->setChecked(SettingsDefaults::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP);

    markAsModified();
}

ValidationResult GeneralSettingsPage::validateSettings() const
{
    // Validate temp directory exists and is writable
    QString tempPath = m_TempLocationPicker->path();
    if (!tempPath.isEmpty())
    {
        QDir dir(tempPath);
        if (!dir.exists())
        {
            // Try to create it
            if (!dir.mkpath("."))
            {
                return ValidationResult::invalid(
                    tr("The temporary files directory does not exist and could not be created: %1").arg(tempPath),
                    "tempLocation");
            }
        }

        // Check if writable by trying to create a test file
        QFile testFile(dir.filePath(".afternight_test_write"));
        if (testFile.open(QIODevice::WriteOnly))
        {
            testFile.close();
            testFile.remove();
        }
        else
        {
            return ValidationResult::invalid(tr("The temporary files directory is not writable: %1").arg(tempPath),
                                             "tempLocation");
        }
    }

    return ValidationResult::valid();
}

QString GeneralSettingsPage::getDefaultTempLocation() const
{
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(tempPath).filePath("AfterNight");
}

void GeneralSettingsPage::onImportSettings()
{
    QString initialDir;
    if (auto* context = an::ui::appContext())
    {
        if (auto* pathMemory = context->getPathMemoryService())
        {
            initialDir = pathMemory->selectPath(kSettingsImportExportContext);
            if (!initialDir.isEmpty() && !QDir(initialDir).exists())
            {
                initialDir.clear();
            }
        }
    }
    if (initialDir.isEmpty())
    {
        initialDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    const QStringList filters = settingsJsonFilters();
    const QString filePath = an::ui::dialogs::getOpenFileName(m_Settings, this, tr("Import Settings"), initialDir,
                                                              filters.join(QStringLiteral(";;")));

    if (filePath.isEmpty())
    {
        return;
    }

    if (auto* context = an::ui::appContext())
    {
        if (auto* pathMemory = context->getPathMemoryService())
        {
            pathMemory->upsertPath(kSettingsImportExportContext, QFileInfo(filePath).absolutePath());
        }
    }

    QString error;
    if (!importSettingsFile(filePath, &error))
    {
        QMessageBox::warning(this, tr("Import Failed"), error);
        return;
    }

    QMessageBox::information(this, tr("Import Successful"), tr("Settings have been imported. Click OK to apply them."));
}

bool GeneralSettingsPage::importSettingsFile(const QString& filePath, QString* error)
{
    const auto refuse = [error](const QString& message) {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refuse(tr("Could not open file for reading: %1").arg(file.errorString()));
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        return refuse(tr("Invalid JSON file: %1").arg(parseError.errorString()));
    }

    if (!doc.isObject())
    {
        return refuse(tr("Invalid settings file format."));
    }

    // A file of another format, or one that states none, is refused before anything it states is staged.
    const QJsonObject root = doc.object();
    const QJsonValue stated = root.value(QStringLiteral("version"));
    const QString format = stated.isString()   ? stated.toString().trimmed()
                           : stated.isDouble() ? QString::number(stated.toDouble())
                                               : QString();
    if (format != QLatin1String(kSettingsFileFormat))
    {
        const QString found =
            format.isEmpty() ? tr("states no format version") : tr("was written in format %1").arg(format);
        return refuse(tr("The settings file '%1' %2; this version of AfterNight reads format %3. Nothing was imported.")
                          .arg(QDir::toNativeSeparators(filePath), found, QLatin1String(kSettingsFileFormat)));
    }

    const bool anyImported = stageImportedSettings(root);

    if (!anyImported)
    {
        return refuse(tr("No recognized settings were found in the selected file."));
    }

    markAsModified();
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool GeneralSettingsPage::stageImportedSettings(const QJsonObject& root)
{
    m_PendingImportUpserts.clear();

    bool anyImported = an::ui::settings::containsRecognizedImmediateImportSection(root);

    if (root.contains("general") && root["general"].isObject())
    {
        QJsonObject general = root["general"].toObject();

        if (general.contains("language"))
        {
            int idx = m_LanguageCombo->findData(general["language"].toString());
            if (idx >= 0)
                m_LanguageCombo->setCurrentIndex(idx);
        }
        if (general.contains("defaultSaveFormat"))
        {
            int idx = m_DefaultSaveFormatCombo->findData(general["defaultSaveFormat"].toString());
            if (idx >= 0)
                m_DefaultSaveFormatCombo->setCurrentIndex(idx);
        }
        if (general.contains("recentFilesLimit"))
        {
            m_RecentFilesLimitSpin->setValue(general["recentFilesLimit"].toInt());
        }
        if (general.contains("recentProcessesLimit"))
        {
            m_RecentProcessesLimitSpin->setValue(general["recentProcessesLimit"].toInt());
        }
        if (general.contains("confirmCloseUnsaved"))
        {
            m_ConfirmCloseUnsavedCheck->setChecked(general["confirmCloseUnsaved"].toBool());
        }
        if (general.contains("tempFilesLocation"))
        {
            m_TempLocationPicker->setPath(general["tempFilesLocation"].toString());
        }
        if (general.contains("clearTempOnExit"))
        {
            m_ClearTempOnExitCheck->setChecked(general["clearTempOnExit"].toBool());
        }
        if (general.contains("checkAppUpdatesOnStartup"))
        {
            m_CheckAppUpdatesOnStartupCheck->setChecked(general["checkAppUpdatesOnStartup"].toBool());
        }
        if (general.contains("checkExtensionUpdatesOnStartup"))
        {
            m_CheckExtensionUpdatesOnStartupCheck->setChecked(general["checkExtensionUpdatesOnStartup"].toBool());
        }

        anyImported = true;
    }

    if (root.contains("raw") && root["raw"].isObject())
    {
        const QJsonObject raw = root["raw"].toObject();

        if (raw.contains("whiteBalanceMode"))
        {
            const int mode = wbModeFromJsonValue(raw["whiteBalanceMode"], kDefaultWbMode);
            m_PendingImportUpserts.insert(SettingsKeys::RAW_WHITE_BALANCE_MODE, QString::number(mode));
        }
        if (raw.contains("customWbR"))
        {
            m_PendingImportUpserts.insert(
                SettingsKeys::RAW_CUSTOM_WB_R,
                QString::number(raw["customWbR"].toDouble(SettingsDefaults::RAW_CUSTOM_WB_R), 'f', 6));
        }
        if (raw.contains("customWbG"))
        {
            m_PendingImportUpserts.insert(
                SettingsKeys::RAW_CUSTOM_WB_G,
                QString::number(raw["customWbG"].toDouble(SettingsDefaults::RAW_CUSTOM_WB_G), 'f', 6));
        }
        if (raw.contains("customWbB"))
        {
            m_PendingImportUpserts.insert(
                SettingsKeys::RAW_CUSTOM_WB_B,
                QString::number(raw["customWbB"].toDouble(SettingsDefaults::RAW_CUSTOM_WB_B), 'f', 6));
        }
        if (raw.contains("autoRotate"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::RAW_AUTO_ROTATE, raw["autoRotate"].toBool() ? "1" : "0");
        }
        if (raw.contains("demosaicAlgorithm"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, raw["demosaicAlgorithm"].toString());
        }
    }

    if (root.contains("tools") && root["tools"].isObject())
    {
        const QJsonObject tools = root["tools"].toObject();
        if (tools.contains("starnetPath"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::TOOLS_STARNET_PATH, tools["starnetPath"].toString());
        }
        if (tools.contains("deepsnrPath"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::TOOLS_DEEPSNR_PATH, tools["deepsnrPath"].toString());
        }
        if (tools.contains("astapPath"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::TOOLS_ASTAP_PATH, tools["astapPath"].toString());
        }
        if (tools.contains("astrometryLocalPath"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH,
                                          tools["astrometryLocalPath"].toString());
        }
        if (tools.contains("astrometryApiKey"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, tools["astrometryApiKey"].toString());
        }
    }

    if (root.contains("processing") && root["processing"].isObject())
    {
        const QJsonObject processing = root["processing"].toObject();
        if (processing.contains("maxThreadsBatch"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PROCESSING_MAX_THREADS_BATCH,
                                          QString::number(processing["maxThreadsBatch"].toInt()));
        }
        if (processing.contains("maxThreadsPreview"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PROCESSING_MAX_THREADS_PREVIEW,
                                          QString::number(processing["maxThreadsPreview"].toInt()));
        }
        if (processing.contains("gpuEnabled"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PROCESSING_GPU_ENABLED,
                                          processing["gpuEnabled"].toBool() ? "1" : "0");
        }
        if (processing.contains("preferredGpu"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PROCESSING_PREFERRED_GPU,
                                          processing["preferredGpu"].toString());
        }
        if (processing.contains("memoryLimitMb"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB,
                                          QString::number(processing["memoryLimitMb"].toInt()));
        }
        if (processing.contains("previewFrameRate"))
        {
            // A number or its text, held to the rate's range; anything else is left out rather than read as zero,
            // which the store would hold to the range's lowest end.
            const QJsonValue value = processing["previewFrameRate"];
            const QString text = value.isString()   ? value.toString()
                                 : value.isDouble() ? QString::number(value.toDouble())
                                                    : QString();
            if (const std::optional<std::uint32_t> rate = an::ui::parsePreviewFrameRate(text); rate.has_value())
            {
                m_PendingImportUpserts.insert(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE, QString::number(*rate));
            }
        }
    }

    if (root.contains("interface") && root["interface"].isObject())
    {
        const QJsonObject iface = root["interface"].toObject();
        if (iface.contains("theme"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_THEME, iface["theme"].toString());
        }
        if (iface.contains("materialBackdropMode"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_MATERIAL_BACKDROP_MODE,
                                          iface["materialBackdropMode"].toString());
        }
        if (iface.contains("themeContrastProfile"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE,
                                          iface["themeContrastProfile"].toString());
        }
        if (iface.contains("themeAccentFamily"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY,
                                          iface["themeAccentFamily"].toString());
        }
        if (iface.contains("themeSurfaceTintFamily"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY,
                                          iface["themeSurfaceTintFamily"].toString());
        }
        if (iface.contains("zoomSensitivity"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_ZOOM_SENSITIVITY,
                                          QString::number(iface["zoomSensitivity"].toInt()));
        }
        if (iface.contains("zoomRequiresCtrl"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_ZOOM_REQUIRES_CTRL,
                                          iface["zoomRequiresCtrl"].toBool() ? "1" : "0");
        }
        if (iface.contains("lockLayoutByDefault"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_LOCK_LAYOUT_BY_DEFAULT,
                                          iface["lockLayoutByDefault"].toBool() ? "1" : "0");
        }
        if (iface.contains("rememberWindowSize"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE,
                                          iface["rememberWindowSize"].toBool() ? "1" : "0");
        }
        if (iface.contains("recentProcessIds"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS,
                                          iface["recentProcessIds"].toString());
        }
        if (iface.contains("favoriteProcessIds"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::INTERFACE_FAVORITE_PROCESS_IDS,
                                          iface["favoriteProcessIds"].toString());
        }
    }

    if (root.contains("export") && root["export"].isObject())
    {
        const QJsonObject exportObj = root["export"].toObject();
        if (exportObj.contains("tiffCompression"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_TIFF_COMPRESSION,
                                          exportObj["tiffCompression"].toString());
        }
        if (exportObj.contains("xisfCompression"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_XISF_COMPRESSION,
                                          exportObj["xisfCompression"].toString());
        }
        if (exportObj.contains("xisfByteShuffle"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE, exportObj["xisfByteShuffle"].toBool()
                                                                                      ? QStringLiteral("1")
                                                                                      : QStringLiteral("0"));
        }
        if (exportObj.contains("xisfChecksum"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_XISF_CHECKSUM, exportObj["xisfChecksum"].toString());
        }
        if (exportObj.contains("jpegQuality"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_JPEG_QUALITY,
                                          QString::number(exportObj["jpegQuality"].toInt()));
        }
        if (exportObj.contains("pngCompression"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_PNG_COMPRESSION,
                                          QString::number(exportObj["pngCompression"].toInt()));
        }
        if (exportObj.contains("fitsFloatFormat"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_FITS_FLOAT_FORMAT,
                                          exportObj["fitsFloatFormat"].toString());
        }
        if (exportObj.contains("embedExif"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_EMBED_EXIF, exportObj["embedExif"].toBool() ? "1" : "0");
        }
        if (exportObj.contains("embedHistory"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_EMBED_HISTORY,
                                          exportObj["embedHistory"].toBool() ? "1" : "0");
        }
        if (exportObj.contains("defaultDirectory"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::EXPORT_DEFAULT_DIRECTORY,
                                          exportObj["defaultDirectory"].toString());
        }
    }

    // Logging settings are stored in a separate config file, not in the database
    if (root.contains("logging") && root["logging"].isObject())
    {
        const QJsonObject loggingJson = root["logging"].toObject();
        an::platform::LoggingConfig logConfig;
        an::platform::loadLoggingConfig(logConfig); // Load current values first

        if (loggingJson.contains("directory"))
        {
            logConfig.logDirectory = loggingJson["directory"].toString().toStdString();
        }
        if (loggingJson.contains("level"))
        {
            logConfig.logLevel = loggingJson["level"].toString().toStdString();
        }
        if (loggingJson.contains("maxSizeMb"))
        {
            logConfig.maxFileSizeMb = static_cast<size_t>(loggingJson["maxSizeMb"].toInt());
        }
        if (loggingJson.contains("maxFiles"))
        {
            logConfig.maxFiles = static_cast<size_t>(loggingJson["maxFiles"].toInt());
        }
        // Note: logToConsole option has been removed

        // Save to config file and apply log level immediately
        an::platform::saveLoggingConfig(logConfig);
        an::platform::applyLoggingConfig(logConfig);
    }

    if (root.contains("platesolve") && root["platesolve"].isObject())
    {
        const QJsonObject platesolve = root["platesolve"].toObject();
        if (platesolve.contains("defaultSolver"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER,
                                          platesolve["defaultSolver"].toString());
        }
        if (platesolve.contains("searchRadius"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_SEARCH_RADIUS,
                                          QString::number(platesolve["searchRadius"].toDouble()));
        }
        if (platesolve.contains("downsample"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_DOWNSAMPLE,
                                          QString::number(platesolve["downsample"].toInt()));
        }
        if (platesolve.contains("timeout"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_TIMEOUT,
                                          QString::number(platesolve["timeout"].toInt()));
        }
        if (platesolve.contains("indexPath"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_INDEX_PATH, platesolve["indexPath"].toString());
        }
        if (platesolve.contains("gaiaCatalogDirectory"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                          platesolve["gaiaCatalogDirectory"].toString());
        }
        if (platesolve.contains("gaiaCatalogPack"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK,
                                          platesolve["gaiaCatalogPack"].toString());
        }
        if (platesolve.contains("useHints"))
        {
            m_PendingImportUpserts.insert(SettingsKeys::PLATESOLVE_USE_HINTS,
                                          platesolve["useHints"].toBool() ? "1" : "0");
        }
    }

    if (!m_PendingImportUpserts.isEmpty())
    {
        anyImported = true;
    }

    return anyImported;
}

QJsonObject GeneralSettingsPage::exportedSettings() const
{
    const auto selectSetting = [this](const char* key, const QString& defaultValue) -> QString {
        return m_Settings ? m_Settings->select(key, defaultValue) : defaultValue;
    };

    const auto selectInt = [&selectSetting](const char* key, int defaultValue) -> int {
        bool ok = false;
        const int value = selectSetting(key, QString::number(defaultValue)).toInt(&ok);
        return ok ? value : defaultValue;
    };

    const auto selectDouble = [&selectSetting](const char* key, double defaultValue) -> double {
        bool ok = false;
        const double value = selectSetting(key, QString::number(defaultValue)).toDouble(&ok);
        return ok ? value : defaultValue;
    };

    const auto selectBool = [&selectSetting](const char* key, bool defaultValue) -> bool {
        const QString value = selectSetting(key, defaultValue ? "1" : "0");
        return (value == "1");
    };

    QJsonObject general;
    general["language"] = m_LanguageCombo->currentData().toString();
    general["defaultSaveFormat"] = m_DefaultSaveFormatCombo->currentData().toString();
    general["recentFilesLimit"] = m_RecentFilesLimitSpin->value();
    general["recentProcessesLimit"] = m_RecentProcessesLimitSpin->value();
    general["confirmCloseUnsaved"] = m_ConfirmCloseUnsavedCheck->isChecked();
    general["tempFilesLocation"] = m_TempLocationPicker->path();
    general["clearTempOnExit"] = m_ClearTempOnExitCheck->isChecked();
    general["checkAppUpdatesOnStartup"] = m_CheckAppUpdatesOnStartupCheck->isChecked();
    general["checkExtensionUpdatesOnStartup"] = m_CheckExtensionUpdatesOnStartupCheck->isChecked();

    QJsonObject raw;
    const QString wbStored = selectSetting(SettingsKeys::RAW_WHITE_BALANCE_MODE, QString::number(kDefaultWbMode));
    raw["whiteBalanceMode"] = wbModeFromStoredValue(wbStored);
    raw["customWbR"] = selectDouble(SettingsKeys::RAW_CUSTOM_WB_R, SettingsDefaults::RAW_CUSTOM_WB_R);
    raw["customWbG"] = selectDouble(SettingsKeys::RAW_CUSTOM_WB_G, SettingsDefaults::RAW_CUSTOM_WB_G);
    raw["customWbB"] = selectDouble(SettingsKeys::RAW_CUSTOM_WB_B, SettingsDefaults::RAW_CUSTOM_WB_B);
    raw["autoRotate"] = selectBool(SettingsKeys::RAW_AUTO_ROTATE, SettingsDefaults::RAW_AUTO_ROTATE);
    raw["demosaicAlgorithm"] =
        selectSetting(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);

    QJsonObject tools;
    tools["starnetPath"] = selectSetting(SettingsKeys::TOOLS_STARNET_PATH, SettingsDefaults::TOOLS_STARNET_PATH);
    tools["deepsnrPath"] = selectSetting(SettingsKeys::TOOLS_DEEPSNR_PATH, SettingsDefaults::TOOLS_DEEPSNR_PATH);
    tools["astapPath"] = selectSetting(SettingsKeys::TOOLS_ASTAP_PATH, SettingsDefaults::TOOLS_ASTAP_PATH);
    tools["astrometryLocalPath"] =
        selectSetting(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH, SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH);
    tools["astrometryApiKey"] =
        selectSetting(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, SettingsDefaults::TOOLS_ASTROMETRY_API_KEY);

    QJsonObject processing;
    processing["maxThreadsBatch"] =
        selectInt(SettingsKeys::PROCESSING_MAX_THREADS_BATCH, SettingsDefaults::PROCESSING_MAX_THREADS_BATCH);
    processing["maxThreadsPreview"] =
        selectInt(SettingsKeys::PROCESSING_MAX_THREADS_PREVIEW, SettingsDefaults::PROCESSING_MAX_THREADS_PREVIEW);
    processing["gpuEnabled"] =
        selectBool(SettingsKeys::PROCESSING_GPU_ENABLED, SettingsDefaults::PROCESSING_GPU_ENABLED);
    processing["preferredGpu"] =
        selectSetting(SettingsKeys::PROCESSING_PREFERRED_GPU, SettingsDefaults::PROCESSING_PREFERRED_GPU);
    processing["memoryLimitMb"] =
        selectInt(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB, SettingsDefaults::PROCESSING_MEMORY_LIMIT_MB);
    processing["previewFrameRate"] =
        selectInt(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE, SettingsDefaults::PROCESSING_PREVIEW_FRAME_RATE);

    QJsonObject iface;
    iface["theme"] = selectSetting(SettingsKeys::INTERFACE_THEME, SettingsDefaults::INTERFACE_THEME);
    iface["materialBackdropMode"] = selectSetting(SettingsKeys::INTERFACE_MATERIAL_BACKDROP_MODE,
                                                  SettingsDefaults::INTERFACE_MATERIAL_BACKDROP_MODE);
    iface["themeContrastProfile"] = selectSetting(SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE,
                                                  SettingsDefaults::INTERFACE_THEME_CONTRAST_PROFILE);
    iface["themeAccentFamily"] =
        selectSetting(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY, SettingsDefaults::INTERFACE_THEME_ACCENT_FAMILY);
    iface["themeSurfaceTintFamily"] = selectSetting(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY,
                                                    SettingsDefaults::INTERFACE_THEME_SURFACE_TINT_FAMILY);
    iface["zoomSensitivity"] =
        selectInt(SettingsKeys::INTERFACE_ZOOM_SENSITIVITY, SettingsDefaults::INTERFACE_ZOOM_SENSITIVITY);
    iface["zoomRequiresCtrl"] =
        selectBool(SettingsKeys::INTERFACE_ZOOM_REQUIRES_CTRL, SettingsDefaults::INTERFACE_ZOOM_REQUIRES_CTRL);
    iface["lockLayoutByDefault"] =
        selectBool(SettingsKeys::INTERFACE_LOCK_LAYOUT_BY_DEFAULT, SettingsDefaults::INTERFACE_LOCK_LAYOUT_BY_DEFAULT);
    iface["rememberWindowSize"] =
        selectBool(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE, SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE);
    iface["recentProcessIds"] =
        selectSetting(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS, SettingsDefaults::INTERFACE_RECENT_PROCESS_IDS);
    iface["favoriteProcessIds"] =
        selectSetting(SettingsKeys::INTERFACE_FAVORITE_PROCESS_IDS, SettingsDefaults::INTERFACE_FAVORITE_PROCESS_IDS);

    QJsonObject exportObj;
    exportObj["tiffCompression"] =
        selectSetting(SettingsKeys::EXPORT_TIFF_COMPRESSION, SettingsDefaults::EXPORT_TIFF_COMPRESSION);
    exportObj["xisfCompression"] =
        selectSetting(SettingsKeys::EXPORT_XISF_COMPRESSION, SettingsDefaults::EXPORT_XISF_COMPRESSION);
    exportObj["xisfByteShuffle"] =
        selectBool(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE, SettingsDefaults::EXPORT_XISF_BYTE_SHUFFLE);
    exportObj["xisfChecksum"] =
        selectSetting(SettingsKeys::EXPORT_XISF_CHECKSUM, SettingsDefaults::EXPORT_XISF_CHECKSUM);
    exportObj["jpegQuality"] = selectInt(SettingsKeys::EXPORT_JPEG_QUALITY, SettingsDefaults::EXPORT_JPEG_QUALITY);
    exportObj["pngCompression"] =
        selectInt(SettingsKeys::EXPORT_PNG_COMPRESSION, SettingsDefaults::EXPORT_PNG_COMPRESSION);
    exportObj["fitsFloatFormat"] =
        selectSetting(SettingsKeys::EXPORT_FITS_FLOAT_FORMAT, SettingsDefaults::EXPORT_FITS_FLOAT_FORMAT);
    exportObj["embedExif"] = selectBool(SettingsKeys::EXPORT_EMBED_EXIF, SettingsDefaults::EXPORT_EMBED_EXIF);
    exportObj["embedHistory"] = selectBool(SettingsKeys::EXPORT_EMBED_HISTORY, SettingsDefaults::EXPORT_EMBED_HISTORY);
    exportObj["defaultDirectory"] =
        selectSetting(SettingsKeys::EXPORT_DEFAULT_DIRECTORY, SettingsDefaults::EXPORT_DEFAULT_DIRECTORY);

    // Logging settings come from the separate config file, not the database
    an::platform::LoggingConfig logConfig;
    an::platform::loadLoggingConfig(logConfig);
    QJsonObject logging;
    logging["directory"] = QString::fromStdString(logConfig.logDirectory);
    logging["level"] = QString::fromStdString(logConfig.logLevel);
    logging["maxSizeMb"] = static_cast<int>(logConfig.maxFileSizeMb);
    logging["maxFiles"] = static_cast<int>(logConfig.maxFiles);

    QJsonObject platesolve;
    platesolve["defaultSolver"] =
        selectSetting(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER, SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER);
    platesolve["searchRadius"] =
        selectDouble(SettingsKeys::PLATESOLVE_SEARCH_RADIUS, SettingsDefaults::PLATESOLVE_SEARCH_RADIUS);
    platesolve["downsample"] = selectInt(SettingsKeys::PLATESOLVE_DOWNSAMPLE, SettingsDefaults::PLATESOLVE_DOWNSAMPLE);
    platesolve["timeout"] = selectInt(SettingsKeys::PLATESOLVE_TIMEOUT, SettingsDefaults::PLATESOLVE_TIMEOUT);
    platesolve["indexPath"] =
        selectSetting(SettingsKeys::PLATESOLVE_INDEX_PATH, SettingsDefaults::PLATESOLVE_INDEX_PATH);
    platesolve["gaiaCatalogDirectory"] = selectSetting(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                                       SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY);
    platesolve["gaiaCatalogPack"] =
        selectSetting(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK, SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
    platesolve["useHints"] = selectBool(SettingsKeys::PLATESOLVE_USE_HINTS, SettingsDefaults::PLATESOLVE_USE_HINTS);

    QJsonObject root;
    root["version"] = QString::fromLatin1(kSettingsFileFormat);
    root["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["general"] = general;
    root["raw"] = raw;
    root["tools"] = tools;
    root["processing"] = processing;
    root["interface"] = iface;
    root["export"] = exportObj;
    root["logging"] = logging;
    root["platesolve"] = platesolve;
    return root;
}

void GeneralSettingsPage::onExportSettings()
{
    QString initialDir;
    if (auto* context = an::ui::appContext())
    {
        if (auto* pathMemory = context->getPathMemoryService())
        {
            initialDir = pathMemory->selectPath(kSettingsImportExportContext);
            if (!initialDir.isEmpty() && !QDir(initialDir).exists())
            {
                initialDir.clear();
            }
        }
    }
    if (initialDir.isEmpty())
    {
        initialDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    const QStringList filters = settingsJsonFilters();
    QString filePath =
        an::ui::dialogs::getSaveFileName(m_Settings, this, tr("Export Settings"),
                                         initialDir + "/afternight_settings.json", filters.join(QStringLiteral(";;")));

    if (filePath.isEmpty())
    {
        return;
    }

    if (auto* context = an::ui::appContext())
    {
        if (auto* pathMemory = context->getPathMemoryService())
        {
            pathMemory->upsertPath(kSettingsImportExportContext, QFileInfo(filePath).absolutePath());
        }
    }

    // Ensure .json extension
    if (!filePath.endsWith(".json", Qt::CaseInsensitive))
    {
        filePath += ".json";
    }

    const QJsonObject root = exportedSettings();

    const QJsonDocument doc(root);
    QString errorMessage;
    if (!an::ui::settings::writeSettingsJsonFile(filePath, doc.toJson(QJsonDocument::Indented), &errorMessage))
    {
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write the settings file: %1").arg(errorMessage));
        return;
    }

    QMessageBox::information(this, tr("Export Successful"), tr("Settings have been exported to:\n%1").arg(filePath));
}
