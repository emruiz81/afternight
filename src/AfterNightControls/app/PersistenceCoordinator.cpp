// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/PersistenceCoordinator.h"

#include "app/ApplicationStorage.h"

#include "controls/DashboardUtils.h"
#include "controls/DbSchemaVersionManager.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <stdexcept>

namespace
{

const auto MAIN_PERSISTENCE_DB_FILENAME = QStringLiteral("afternight_data.db");
const auto MAIN_PERSISTENCE_CONNECTION = QStringLiteral("__afternight_main_persistence__");
const auto FOLDER_TRACKER_GLOBAL_CONTEXT = QStringLiteral("__GLOBAL__");

/// The format the settings database is written in: the tables below, and the format stated beside them.
constexpr int kSettingsDatabaseFormat = 2;

/// Makes the settings database's tables, in a new database.
bool createSettingsTables(QSqlDatabase& db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS global_settings "
                                 "(setting_key TEXT PRIMARY KEY, setting_value TEXT)")) &&
           q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS folder_tracker "
                                 "(context TEXT PRIMARY KEY, folder TEXT)")) &&
           q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS thumbnails "
                                 "(file_name TEXT PRIMARY KEY, thumbnail_file_name TEXT)")) &&
           q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS process_settings "
                                 "(process_name TEXT PRIMARY KEY, window_state INTEGER, "
                                 "window_width INTEGER, window_height INTEGER, json_settings TEXT)"));
}

} // anonymous namespace

namespace an::ui
{

bool shouldSkipPersistLastOpenDirectory(const std::string& path)
{
    // The recent files' predicate: a folder Qt hands back is slash-separated, so a backslash test alone never matched.
    return DashboardUtils::isNetworkPath(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())));
}

PersistenceCoordinator::PersistenceCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void PersistenceCoordinator::initializePersistence() const
{
    if (!m_dependencies.localDataPath || !m_dependencies.lastOpenDirectory || !m_dependencies.dbExecutor ||
        !m_dependencies.thumbnailPersistence || !m_dependencies.folderTrackerPersistence ||
        !m_dependencies.processSettingsPersistence || !m_dependencies.globalSettingsPersistence)
    {
        throw std::runtime_error("Persistence coordinator dependencies are not fully configured");
    }

    *m_dependencies.localDataPath = applicationLocalDataPath();

    QDir dir(*m_dependencies.localDataPath);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    const QString dbPath = dir.filePath(MAIN_PERSISTENCE_DB_FILENAME);
    const auto openDatabase = [this, &dbPath]() {
        // The executor opens the DB on its dedicated thread and serialises all SQL access.
        *m_dependencies.dbExecutor = std::make_unique<PersistenceDbExecutor>(dbPath, MAIN_PERSISTENCE_CONNECTION);
        if (!(*m_dependencies.dbExecutor)->isOpen())
        {
            const std::string errorMsg =
                fmt::format("Failed to open persistence database: {}", dbPath.toUtf8().toStdString());
            LOG_ERROR(errorMsg);
            throw std::runtime_error(errorMsg);
        }
        DbSchemaVersionManager format(**m_dependencies.dbExecutor, kSettingsDatabaseFormat, createSettingsTables);
        return format.initialize();
    };

    // A database of another format - older, newer, or one that states none - is never read, written or deleted: it
    // is set aside beside itself, and a new one is made in its place, so the settings start from their defaults.
    m_settingsStoreNotice.clear();
    DbSchemaVersionManager::Result opened = openDatabase();
    if (opened.outcome == DbSchemaVersionManager::Outcome::OtherFormat)
    {
        m_dependencies.dbExecutor->reset();
        const QString setAside = setAsideStore(dbPath, opened.foundFormat);
        const QString found = opened.foundFormat > 0 ? QObject::tr("was written in format %1").arg(opened.foundFormat)
                                                     : QObject::tr("states no format version");
        if (setAside.isEmpty())
        {
            const std::string errorMsg = fmt::format(
                "The settings database {} {}; this version of AfterNight reads format {}, and the database "
                "could not be set aside",
                QDir::toNativeSeparators(dbPath).toStdString(), found.toStdString(), kSettingsDatabaseFormat);
            LOG_ERROR(errorMsg);
            throw std::runtime_error(errorMsg);
        }
        m_settingsStoreNotice = QObject::tr("The settings database %1; this version of AfterNight reads format %2. It "
                                            "was set aside as %3, and the settings start from their defaults.")
                                    .arg(found)
                                    .arg(kSettingsDatabaseFormat)
                                    .arg(QDir::toNativeSeparators(setAside));
        LOG_WARNING("PersistenceCoordinator: {}", m_settingsStoreNotice.toStdString());
        opened = openDatabase();
    }
    if (opened.outcome != DbSchemaVersionManager::Outcome::Created &&
        opened.outcome != DbSchemaVersionManager::Outcome::Current)
    {
        throw std::runtime_error("Failed to initialize main database schema");
    }
    LOG_INFO("Settings database at format {}", kSettingsDatabaseFormat);

    // Write-ahead logging once the database is this version's own: a database of another format is never written.
    (*m_dependencies.dbExecutor)->execute([](QSqlDatabase& db) {
        QSqlQuery q(db);
        q.exec("PRAGMA journal_mode=WAL;");
    });

    *m_dependencies.thumbnailPersistence = std::make_unique<ThumbnailPersistence>(**m_dependencies.dbExecutor);
    *m_dependencies.folderTrackerPersistence = std::make_unique<FolderTrackerPersistence>(**m_dependencies.dbExecutor);
    *m_dependencies.processSettingsPersistence =
        std::make_unique<ProcessSettingsPersistence>(**m_dependencies.dbExecutor);
    *m_dependencies.globalSettingsPersistence =
        std::make_unique<GlobalSettingsPersistence>(**m_dependencies.dbExecutor);

    if (!(*m_dependencies.thumbnailPersistence)->initializeTable())
    {
        throw std::runtime_error("Failed to initialize thumbnail persistence table");
    }

    if (!(*m_dependencies.folderTrackerPersistence)->initializeTable())
    {
        throw std::runtime_error("Failed to initialize folder tracker persistence table");
    }

    if (!(*m_dependencies.processSettingsPersistence)->initializeTable())
    {
        throw std::runtime_error("Failed to initialize process settings persistence table");
    }

    if (!(*m_dependencies.globalSettingsPersistence)->initializeTable())
    {
        throw std::runtime_error("Failed to initialize global settings persistence table");
    }

    if (m_dependencies.setFileManagerPersistenceDependencies)
    {
        m_dependencies.setFileManagerPersistenceDependencies((*m_dependencies.thumbnailPersistence).get(),
                                                             (*m_dependencies.folderTrackerPersistence).get(),
                                                             (*m_dependencies.globalSettingsPersistence).get());
    }

    *m_dependencies.lastOpenDirectory =
        (*m_dependencies.folderTrackerPersistence)->select(FOLDER_TRACKER_GLOBAL_CONTEXT).toUtf8().toStdString();

    loadRawOpenPolicy();

    if (m_dependencies.loadRecentFilesFromDisk)
    {
        m_dependencies.loadRecentFilesFromDisk();
    }

    if (m_dependencies.ensureTempDirCreated)
    {
        m_dependencies.ensureTempDirCreated();
    }

    if (m_dependencies.ensureThumbnailsDirCreated)
    {
        m_dependencies.ensureThumbnailsDirCreated();
    }
}

const QString& PersistenceCoordinator::settingsStoreNotice() const noexcept
{
    return m_settingsStoreNotice;
}

void PersistenceCoordinator::persistLastOpenDir() const
{
    if (!m_dependencies.lastOpenDirectory || !m_dependencies.folderTrackerPersistence ||
        !(*m_dependencies.folderTrackerPersistence))
    {
        return;
    }

    if (shouldSkipPersistLastOpenDirectory(*m_dependencies.lastOpenDirectory))
    {
        return;
    }

    (*m_dependencies.folderTrackerPersistence)
        ->upsert(FOLDER_TRACKER_GLOBAL_CONTEXT, QString::fromUtf8(*m_dependencies.lastOpenDirectory));
}

void PersistenceCoordinator::loadRawOpenPolicy() const
{
    const RawOpenSettings settings = readRawOpenSettings(settingReader());
    an::Result<std::shared_ptr<const RawOpenPolicy>> policy = RawOpenPolicy::create(settings);
    if (!policy.hasValue())
    {
        LOG_ERROR("Could not build the RAW open policy: {}", policy.status().message());
        return;
    }
    // Published, never written into an engine: an open takes the policy published when it is submitted.
    publish(std::move(*policy));
    LOG_INFO("Published the RAW open policy: white balance {}, auto-rotate {}",
             storedRawWhiteBalance(settings.whiteBalance).toStdString(), settings.autoRotate);
}

std::shared_ptr<const RawOpenPolicy> PersistenceCoordinator::rawOpenPolicy() const
{
    {
        std::lock_guard lock(m_policyMutex);
        if (m_rawOpenPolicy)
        {
            return m_rawOpenPolicy;
        }
    }
    an::Result<std::shared_ptr<const RawOpenPolicy>> defaults = RawOpenPolicy::create(RawOpenSettings{});
    if (!defaults.hasValue())
    {
        return nullptr;
    }
    std::lock_guard lock(m_policyMutex);
    if (!m_rawOpenPolicy)
    {
        m_rawOpenPolicy = std::move(*defaults);
    }
    return m_rawOpenPolicy;
}

RawOpenSettings PersistenceCoordinator::rawOpenSettings() const
{
    const std::shared_ptr<const RawOpenPolicy> policy = rawOpenPolicy();
    return policy ? policy->settings() : RawOpenSettings{};
}

an::Status PersistenceCoordinator::setRawOpenSettings(const RawOpenSettings& settings) const
{
    if (m_dependencies.globalSettingsPersistence && *m_dependencies.globalSettingsPersistence)
    {
        GlobalSettingsPersistence* store = m_dependencies.globalSettingsPersistence->get();
        writeRawOpenSettings(settings,
                             [store](const QString& key, const QString& value) { store->upsert(key, value); });
    }
    an::Result<std::shared_ptr<const RawOpenPolicy>> policy = RawOpenPolicy::create(settings);
    if (!policy.hasValue())
    {
        return policy.status();
    }
    publish(std::move(*policy));
    LOG_DEBUG("Stored the RAW settings and published their policy");
    return an::Status::success();
}

ExportSettings PersistenceCoordinator::exportSettings() const
{
    return readExportSettings(settingReader());
}

an::Result<an::OutputIntent> PersistenceCoordinator::buildOutputIntent(const an::ImageDescriptor& source,
                                                                       an::CodecFormat container) const
{
    // The export settings are read when the write is asked for, so a write on its way keeps the intent it was given.
    const ExportSettings settings = exportSettings();
    if (const std::shared_ptr<const RawOpenPolicy> policy = rawOpenPolicy())
    {
        return buildExportIntent(settings, policy->registry(), source, container);
    }
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = createProductCodecRegistry();
    if (!registry.hasValue())
    {
        return registry.status();
    }
    return buildExportIntent(settings, **registry, source, container);
}

SettingReader PersistenceCoordinator::settingReader() const
{
    GlobalSettingsPersistence* store =
        m_dependencies.globalSettingsPersistence && *m_dependencies.globalSettingsPersistence
            ? m_dependencies.globalSettingsPersistence->get()
            : nullptr;
    return [store](const QString& key, const QString& fallback) {
        return store != nullptr ? store->select(key, fallback) : fallback;
    };
}

void PersistenceCoordinator::publish(std::shared_ptr<const RawOpenPolicy> policy) const
{
    std::lock_guard lock(m_policyMutex);
    m_rawOpenPolicy = std::move(policy);
}

} // namespace an::ui
