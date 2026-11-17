// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProjectsSettingsPage.h"

#include "../GlobalSettingsPersistence.h"
#include "PathPickerWidget.h"
#include "SettingsDefaults.h"
#include "theme/AnDesignTokens.h"

#include <verdigris/wobjectimpl.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

W_OBJECT_IMPL(an::ui::ProjectsSettingsPage)

namespace an::ui
{

ProjectsSettingsPage::ProjectsSettingsPage(GlobalSettingsPersistence* settings, QWidget* parent)
    : ::SettingsPage(parent), m_settings(settings)
{
    setupUi();
    trackChanges(m_createForJobs);
    trackChanges(m_compression);
    trackChanges(m_openLast);
    connect(m_location, &PathPickerWidget::pathChanged, this, &ProjectsSettingsPage::markAsModified);
}

QString ProjectsSettingsPage::title()
{
    return tr("Projects");
}

QString ProjectsSettingsPage::pageTitle() const
{
    return title();
}

QIcon ProjectsSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("folder_closed"));
}

QStringList ProjectsSettingsPage::searchKeywords() const
{
    return QStringList{tr("Projects"), tr("Project"),   tr("Location"), tr("Folder"),      tr("Integration"),
                       tr("History"),  tr("Undo"),      tr("Steps"),    tr("XISF"),        tr("Compression"),
                       tr("LZ4"),      tr("Zstandard"), tr("Startup"),  tr("Last project")};
}

void ProjectsSettingsPage::setupUi()
{
    const auto& t = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t.spacing.lg);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === New projects ===
    auto* newGroup = new QGroupBox(tr("New Projects"), this);
    newGroup->setObjectName(QStringLiteral("anProjectsSettingsNewGroup"));
    auto* newLayout = new QFormLayout(newGroup);
    newLayout->setSpacing(t.spacing.md);

    m_location = new PathPickerWidget(PathPickerWidget::Mode::Directory, newGroup);
    m_location->setObjectName(QStringLiteral("anProjectsSettingsLocation"));
    m_location->setPlaceholderText(tr("Select the folder new projects are made in..."));
    m_location->setDialogTitle(tr("Select the Default Location of Projects"));
    newLayout->addRow(tr("Default location:"), m_location);
    newLayout->addRow(QString(), createInfoLabel(tr("A project is a folder there. Images are referenced where they "
                                                    "are, not copied.")));

    m_createForJobs = new QCheckBox(tr("Create a project for every integration job"), newGroup);
    m_createForJobs->setObjectName(QStringLiteral("anProjectsSettingsCreateForIntegrationJobs"));
    m_createForJobs->setToolTip(tr("In the output folder of a job that runs with no project open."));
    newLayout->addRow(m_createForJobs);
    mainLayout->addWidget(newGroup);

    // === Undo history ===
    auto* historyGroup = new QGroupBox(tr("Undo History"), this);
    historyGroup->setObjectName(QStringLiteral("anProjectsSettingsHistoryGroup"));
    auto* historyLayout = new QFormLayout(historyGroup);
    historyLayout->setSpacing(t.spacing.md);

    m_compression = new QComboBox(historyGroup);
    m_compression->setObjectName(QStringLiteral("anProjectsSettingsCompression"));
    m_compression->addItem(tr("LZ4 (fast)"), QStringLiteral("lz4"));
    m_compression->addItem(tr("None"), QStringLiteral("none"));
    m_compression->addItem(tr("Zstandard (smallest)"), QStringLiteral("zstd"));
    m_compression->setToolTip(tr("How the steps of a history are compressed. Every history written from now on takes "
                                 "it, in a project or not, and a new project starts from it."));
    historyLayout->addRow(tr("History compression:"), m_compression);
    historyLayout->addRow(QString(), createInfoLabel(tr("Without a project, history is kept in the temporary folder "
                                                        "and cleared on exit.")));
    mainLayout->addWidget(historyGroup);

    // === Startup ===
    auto* startupGroup = new QGroupBox(tr("Startup"), this);
    startupGroup->setObjectName(QStringLiteral("anProjectsSettingsStartupGroup"));
    auto* startupLayout = new QVBoxLayout(startupGroup);
    startupLayout->setSpacing(t.spacing.sm);
    m_openLast = new QCheckBox(tr("Open the last project on startup"), startupGroup);
    m_openLast->setObjectName(QStringLiteral("anProjectsSettingsOpenLastOnStartup"));
    startupLayout->addWidget(m_openLast);
    mainLayout->addWidget(startupGroup);

    mainLayout->addStretch(1);
}

void ProjectsSettingsPage::showSettings(const ProjectSettings& settings)
{
    m_location->setPath(QDir::toNativeSeparators(settings.defaultLocation));
    m_createForJobs->setChecked(settings.createForIntegrationJobs);
    const std::string_view name = history::stepCompressionName(settings.historyCompression);
    const int index = m_compression->findData(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())));
    m_compression->setCurrentIndex(index >= 0 ? index : 0);
    m_openLast->setChecked(settings.openLastOnStartup);
}

void ProjectsSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();
    GlobalSettingsPersistence* const store = m_settings;
    showSettings(store != nullptr ? readProjectSettings([store](const QString& key, const QString& fallback) {
        return store->select(key, fallback);
    })
                                  : readProjectSettings({}));
    clearModified();
}

void ProjectsSettingsPage::saveSettings()
{
    if (m_settings == nullptr)
    {
        return;
    }
    GlobalSettingsPersistence* const store = m_settings;
    writeProjectSettings(shownSettings(),
                         [store](const QString& key, const QString& value) { (void)store->upsert(key, value); });
    clearModified();
}

void ProjectsSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();
    showSettings(readProjectSettings({}));
    markAsModified();
}

ValidationResult ProjectsSettingsPage::validateSettings() const
{
    const QString location = m_location->path().trimmed();
    if (location.isEmpty() || !QDir::isAbsolutePath(QDir::fromNativeSeparators(location)))
    {
        return ValidationResult::invalid(tr("The default location of projects must be a folder's full path."),
                                         QStringLiteral("defaultLocation"));
    }
    return ValidationResult::valid();
}

ProjectSettings ProjectsSettingsPage::shownSettings() const
{
    ProjectSettings settings;
    const QString location = m_location->path().trimmed();
    settings.defaultLocation =
        location.isEmpty() ? defaultProjectsLocation() : QDir::cleanPath(QDir::fromNativeSeparators(location));
    settings.createForIntegrationJobs = m_createForJobs->isChecked();
    settings.historyCompression =
        stepCompressionFromName(m_compression->currentData().toString()).value_or(history::StepCompression::Lz4);
    settings.openLastOnStartup = m_openLast->isChecked();
    return settings;
}

PathPickerWidget* ProjectsSettingsPage::locationPicker() const noexcept
{
    return m_location;
}

QCheckBox* ProjectsSettingsPage::createForIntegrationJobsCheck() const noexcept
{
    return m_createForJobs;
}

QComboBox* ProjectsSettingsPage::compressionCombo() const noexcept
{
    return m_compression;
}

QCheckBox* ProjectsSettingsPage::openLastOnStartupCheck() const noexcept
{
    return m_openLast;
}

} // namespace an::ui
