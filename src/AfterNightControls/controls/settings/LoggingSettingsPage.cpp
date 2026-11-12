// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LoggingSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "PathPickerWidget.h"
#include "GeneralSettingsPage.h"
#include "../GlobalSettingsPersistence.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileDialog>
#include <QProcess>
#include <QUrl>

#include "app/DesktopUtils.h"

W_OBJECT_IMPL(LoggingSettingsPage)

namespace
{
// Log level options (display name, value stored in config file)
struct LogLevelInfo
{
    QString display;
    QString value;
};

const QList<LogLevelInfo> LOG_LEVELS = {
    {QObject::tr("Trace (Most Verbose)"), "trace"},
    {QObject::tr("Debug"), "debug"},
    {QObject::tr("Info (Recommended)"), "info"},
    {QObject::tr("Warning"), "warning"},
    {QObject::tr("Error"), "error"},
    {QObject::tr("Critical"), "critical"},
    {QObject::tr("Off (Disable Logging)"), "off"},
};
} // namespace

LoggingSettingsPage::LoggingSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString LoggingSettingsPage::pageTitle() const
{
    return tr("Logging");
}

QIcon LoggingSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("format_align_left"));
}

QStringList LoggingSettingsPage::searchKeywords() const
{
    return QStringList{tr("Logging"), tr("Log"),      tr("Debug"), tr("Trace"),     tr("Error"), tr("Warning"),
                       tr("File"),    tr("Rotation"), tr("Level"), tr("Directory"), tr("Size")};
}

void LoggingSettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === Log Location Section ===
    auto* locationGroup = new QGroupBox(tr("Log File Location"), this);
    auto* locationLayout = new QVBoxLayout(locationGroup);
    locationLayout->setSpacing(12);

    // Log directory picker
    auto* dirLayout = new QFormLayout();
    m_LogDirectoryPicker = new PathPickerWidget(PathPickerWidget::Mode::Directory, locationGroup);
    m_LogDirectoryPicker->setPlaceholderText(tr("Select log files directory..."));
    m_LogDirectoryPicker->setDialogTitle(tr("Select Log Directory"));
    dirLayout->addRow(tr("Directory:"), m_LogDirectoryPicker);
    locationLayout->addLayout(dirLayout);

    // Open log directory button
    auto* buttonLayout = new QHBoxLayout();
    m_OpenLogDirButton = new QPushButton(tr("Open Log Directory"), locationGroup);
    m_OpenLogDirButton->setToolTip(tr("Open the log directory in your file manager"));
    buttonLayout->addWidget(m_OpenLogDirButton);
    buttonLayout->addStretch();
    locationLayout->addLayout(buttonLayout);

    mainLayout->addWidget(locationGroup);

    // === Log Level Section ===
    auto* levelGroup = new QGroupBox(tr("Log Level"), this);
    auto* levelLayout = new QFormLayout(levelGroup);
    levelLayout->setSpacing(12);

    m_LogLevelCombo = new QComboBox(levelGroup);
    for (const auto& info : LOG_LEVELS)
    {
        m_LogLevelCombo->addItem(info.display, info.value);
    }
    levelLayout->addRow(tr("Minimum Level:"), m_LogLevelCombo);

    auto* levelInfo =
        createInfoLabel(tr("Lower levels include more detail but may impact performance and disk usage."));
    levelLayout->addRow("", levelInfo);

    mainLayout->addWidget(levelGroup);

    // === Log Rotation Section ===
    auto* rotationGroup = new QGroupBox(tr("Log Rotation"), this);
    auto* rotationLayout = new QFormLayout(rotationGroup);
    rotationLayout->setSpacing(12);

    // Max file size
    m_MaxFileSizeSpin = new QSpinBox(rotationGroup);
    m_MaxFileSizeSpin->setRange(1, 1000);
    m_MaxFileSizeSpin->setSuffix(tr(" MB"));
    m_MaxFileSizeSpin->setToolTip(tr("Maximum size of each log file before rotation"));
    rotationLayout->addRow(tr("Maximum File Size:"), m_MaxFileSizeSpin);

    // Max files
    m_MaxFilesSpin = new QSpinBox(rotationGroup);
    m_MaxFilesSpin->setRange(1, 100);
    m_MaxFilesSpin->setToolTip(tr("Number of rotated log files to keep"));
    rotationLayout->addRow(tr("Maximum Files to Keep:"), m_MaxFilesSpin);

    auto* rotationInfo = createInfoLabel(tr("Older log files are automatically deleted when limits are reached."));
    rotationLayout->addRow("", rotationInfo);

    mainLayout->addWidget(rotationGroup);

    // Note about settings taking effect
    auto* infoLabel = createInfoLabel(tr("All logging settings take effect immediately when saved."));
    mainLayout->addWidget(infoLabel);

    // Add stretch to push content to top
    mainLayout->addStretch(1);
}

void LoggingSettingsPage::connectSignals()
{
    // Track changes for all controls
    connect(m_LogDirectoryPicker, &PathPickerWidget::pathChanged, this, &LoggingSettingsPage::markAsModified);
    trackChanges(m_LogLevelCombo);
    trackChanges(m_MaxFileSizeSpin);
    trackChanges(m_MaxFilesSpin);

    // Open log directory button
    connect(m_OpenLogDirButton, &QPushButton::clicked, this, &LoggingSettingsPage::onOpenLogDirectory);
}

void LoggingSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();

    // Load settings from the logging config file
    an::platform::LoggingConfig config;
    an::platform::loadLoggingConfig(config);

    // Log directory
    QString logDir = QString::fromStdString(config.logDirectory);
    m_LogDirectoryPicker->setPath(logDir.isEmpty() ? getDefaultLogDirectory() : logDir);

    // Log level
    QString level = QString::fromStdString(config.logLevel);
    int levelIndex = m_LogLevelCombo->findData(level);
    m_LogLevelCombo->setCurrentIndex(levelIndex >= 0 ? levelIndex : 2); // Default to "info" (index 2)

    // Max file size
    m_MaxFileSizeSpin->setValue(static_cast<int>(config.maxFileSizeMb));

    // Max files
    m_MaxFilesSpin->setValue(static_cast<int>(config.maxFiles));

    clearModified();
}

void LoggingSettingsPage::saveSettings()
{
    // Build the config from the UI
    an::platform::LoggingConfig config;
    config.logDirectory = m_LogDirectoryPicker->path().toStdString();
    config.logLevel = m_LogLevelCombo->currentData().toString().toStdString();
    config.maxFileSizeMb = static_cast<size_t>(m_MaxFileSizeSpin->value());
    config.maxFiles = static_cast<size_t>(m_MaxFilesSpin->value());

    // Save to the config file
    an::platform::saveLoggingConfig(config);

    // Apply the change to the running logger at once; a folder that cannot be written leaves the log where it was,
    // and the page says so rather than leave it to a line of that log.
    an::ui::settings::sayLoggingConfigProblem(this, an::ui::settings::applyLoggingConfigOrSayWhy(config));

    clearModified();
}

void LoggingSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();

    // Log directory - use the default from the config system
    m_LogDirectoryPicker->setPath(getDefaultLogDirectory());

    // Log level - default to "info"
    int levelIndex = m_LogLevelCombo->findData(QString(SettingsDefaults::LOGGING_LEVEL));
    m_LogLevelCombo->setCurrentIndex(levelIndex >= 0 ? levelIndex : 2);

    // Max file size
    m_MaxFileSizeSpin->setValue(SettingsDefaults::LOGGING_MAX_SIZE_MB);

    // Max files
    m_MaxFilesSpin->setValue(SettingsDefaults::LOGGING_MAX_FILES);

    markAsModified();
}

ValidationResult LoggingSettingsPage::validateSettings() const
{
    // Validate log directory exists and is writable
    QString logPath = m_LogDirectoryPicker->path();
    if (!logPath.isEmpty())
    {
        QDir dir(logPath);
        if (!dir.exists())
        {
            // Try to create it
            if (!dir.mkpath("."))
            {
                return ValidationResult::invalid(
                    tr("The log directory does not exist and could not be created: %1").arg(logPath), "logDirectory");
            }
        }

        // Check if writable by trying to create a test file
        QFile testFile(dir.filePath(".afternight_log_test"));
        if (testFile.open(QIODevice::WriteOnly))
        {
            testFile.close();
            testFile.remove();
        }
        else
        {
            return ValidationResult::invalid(tr("The log directory is not writable: %1").arg(logPath), "logDirectory");
        }
    }

    return ValidationResult::valid();
}

QString LoggingSettingsPage::getDefaultLogDirectory() const
{
    return QString::fromStdString(an::platform::LoggingConfig::defaultLogDirectory().string());
}

void LoggingSettingsPage::onOpenLogDirectory()
{
    QString logDir = m_LogDirectoryPicker->path();
    if (logDir.isEmpty())
    {
        logDir = getDefaultLogDirectory();
    }

    // Ensure directory exists
    QDir dir(logDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    an::ui::openLocalDirectory(dir.absolutePath());
}
