// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A person's settings carried over from AfterNight 1.1: the settings store AfterNight 1.1.0 wrote, recorded once into
/// the fixture layers (`fixtures/common/settings/PROVENANCE.md`), opened as this version opens the store it finds.

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include "app/PersistenceCoordinator.h"
#include "app/ThemeManager.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/ScopedEnvVar.h"
#include "theme/AnDesignTokens.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace
{

/// Where the recorded store is kept, from the repository's root.
constexpr auto kRecordedStoreInTree = "src/AfterNightTests/fixtures/common/settings/afternight-1.1.0.db";

/// What the recorded store's bytes hash to, as its provenance states: a store edited or captured again is another
/// input, and the case refuses it.
constexpr auto kRecordedStoreSha256 = "abfe73712135ac2c421a8449f3cd1a393c9b61a6421745222c9aabdb15398e5e";

/// The name 1.1 gives its settings database in its data folder, which this version opens by the same name.
constexpr auto kStoreFileName = "afternight_data.db";

/// The recorded store, found above the executable or the working directory; empty when it is not there.
QString recordedStore()
{
    for (const QString& start : {QCoreApplication::applicationDirPath(), QDir::currentPath()})
    {
        for (QDir directory(start);;)
        {
            const QString candidate = directory.filePath(QString::fromLatin1(kRecordedStoreInTree));
            if (QFileInfo::exists(candidate))
            {
                return candidate;
            }
            if (!directory.cdUp())
            {
                break;
            }
        }
    }
    return {};
}

/// What a database file holds, read as it is stored, a NULL value kept apart from an empty one.
struct StoredRows
{
    QStringList tables;
    int format = -1;
    std::map<QString, std::optional<QString>> settings;
};

/// Reads a settings database through a connection of its own, which is gone again when this returns.
StoredRows rowsOf(const QString& databasePath)
{
    StoredRows rows;
    const QString connection = QStringLiteral("settings_carry_over_rows");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(databasePath);
        if (db.open())
        {
            QSqlQuery tables(db);
            if (tables.exec(QStringLiteral(
                    "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name")))
            {
                while (tables.next())
                {
                    rows.tables << tables.value(0).toString();
                }
            }
            QSqlQuery format(db);
            if (format.exec(QStringLiteral("SELECT version FROM migrations")) && format.next())
            {
                rows.format = format.value(0).toInt();
            }
            QSqlQuery settings(db);
            if (settings.exec(QStringLiteral("SELECT setting_key, setting_value FROM global_settings")))
            {
                while (settings.next())
                {
                    const QVariant value = settings.value(1);
                    rows.settings[settings.value(0).toString()] =
                        value.isNull() ? std::nullopt : std::optional<QString>(value.toString());
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return rows;
}

/// The stores the application holds, opened by the persistence coordinator as the application opens them.
struct OpenedStores
{
    QString localDataPath;
    std::string lastOpenDirectory;
    std::unique_ptr<PersistenceDbExecutor> executor;
    std::unique_ptr<ThumbnailPersistence> thumbnails;
    std::unique_ptr<FolderTrackerPersistence> folders;
    std::unique_ptr<ProcessSettingsPersistence> processSettings;
    std::unique_ptr<GlobalSettingsPersistence> settings;

    /// Closes them in the application's order: the stores, then the executor they share.
    void close()
    {
        folders.reset();
        thumbnails.reset();
        processSettings.reset();
        settings.reset();
        if (executor)
        {
            executor->shutdown();
        }
        executor.reset();
    }
};

QString setting(GlobalSettingsPersistence& store, const char* key, const char* fallback)
{
    return store.select(QString::fromLatin1(key), QString::fromLatin1(fallback));
}

} // namespace

// The settings store AfterNight 1.1.0 wrote opens under this version as it is: not set aside, no key lost and none
// changed - the two it holds with no value among them - and nothing written into it. Read as the application reads it
// as it starts, its theme and contrast are the ones the person chose, its accent and tint the preset they make with the
// Balanced strength an earlier version's settings are read with (Rose and Amethyst, the Nebula preset), its density
// Compact, and the shell it starts the Observatory one: 1.1 stored none of the four.
TEST(SettingsCarryOver, A1_1StoreOpensUnder2_0)
{
    const QString recorded = recordedStore();
    ASSERT_FALSE(recorded.isEmpty()) << "the recorded store " << kRecordedStoreInTree
                                     << " is not above the executable or the working directory";
    QFile recordedFile(recorded);
    ASSERT_TRUE(recordedFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(QCryptographicHash::hash(recordedFile.readAll(), QCryptographicHash::Sha256).toHex(),
              QByteArray(kRecordedStoreSha256))
        << "the recorded store is not the one AfterNight 1.1.0 wrote";
    recordedFile.close();

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString store = QDir(root.path()).filePath(QString::fromLatin1(kStoreFileName));
    ASSERT_TRUE(QFile::copy(recorded, store));
    ASSERT_TRUE(QFile::setPermissions(store, QFile::ReadOwner | QFile::WriteOwner));

    const StoredRows before = rowsOf(store);
    ASSERT_EQ(before.format, 2);
    ASSERT_EQ(before.settings.size(), 30u) << "the recorded store reads as another store than its provenance states";
    ASSERT_EQ(before.settings.count(QStringLiteral("interface/recentProcessIds")), 1u);
    ASSERT_EQ(before.settings.at(QStringLiteral("interface/recentProcessIds")), std::nullopt);

    OpenedStores opened;
    {
        const TestHelpers::ScopedEnvVar storage("AFTERNIGHT_TEST_STORAGE_ROOT", root.path().toUtf8().constData());
        an::ui::PersistenceCoordinator::Dependencies dependencies;
        dependencies.localDataPath = &opened.localDataPath;
        dependencies.lastOpenDirectory = &opened.lastOpenDirectory;
        dependencies.dbExecutor = &opened.executor;
        dependencies.thumbnailPersistence = &opened.thumbnails;
        dependencies.folderTrackerPersistence = &opened.folders;
        dependencies.processSettingsPersistence = &opened.processSettings;
        dependencies.globalSettingsPersistence = &opened.settings;
        const an::ui::PersistenceCoordinator coordinator(dependencies);
        ASSERT_NO_THROW(coordinator.initializePersistence());
        EXPECT_TRUE(coordinator.settingsStoreNotice().isEmpty())
            << "the 1.1 store was set aside: " << coordinator.settingsStoreNotice().toStdString();
    }
    ASSERT_NE(opened.settings, nullptr);
    EXPECT_EQ(QDir::cleanPath(opened.localDataPath), QDir::cleanPath(root.path()));
    GlobalSettingsPersistence& settings = *opened.settings;

    // Every key 1.1 stored reads back as it stored it.
    for (const auto& [key, value] : before.settings)
    {
        if (value.has_value())
        {
            EXPECT_EQ(settings.selectOptional(key), value) << key.toStdString();
        }
    }

    // The theme as the person chose it, and the preset the accent and the tint make.
    EXPECT_EQ(setting(settings, SettingsKeys::INTERFACE_THEME, SettingsDefaults::INTERFACE_THEME),
              QStringLiteral("light"));
    EXPECT_EQ(an::ui::resolveThemeContrastProfile(setting(settings, SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE,
                                                          SettingsDefaults::INTERFACE_THEME_CONTRAST_PROFILE)),
              an::ui::ThemeContrastProfile::High);
    EXPECT_EQ(settings.selectOptional(QString::fromLatin1(SettingsKeys::INTERFACE_THEME_PRESET)), std::nullopt);
    EXPECT_EQ(settings.selectOptional(QString::fromLatin1(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH)), std::nullopt);
    const an::ui::ThemePreset preset = an::ui::themePresetOf(
        an::ui::resolveThemeAccentFamily(setting(settings, SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY,
                                                 SettingsDefaults::INTERFACE_THEME_ACCENT_FAMILY)),
        an::ui::resolveThemeSurfaceTintFamily(setting(settings, SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY,
                                                      SettingsDefaults::INTERFACE_THEME_SURFACE_TINT_FAMILY)),
        an::ui::resolveThemeTintStrength(setting(settings, SettingsKeys::INTERFACE_THEME_TINT_STRENGTH,
                                                 SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH)));
    EXPECT_EQ(preset, an::ui::ThemePreset::Nebula) << an::ui::themePresetId(preset).toStdString();

    // The density and the shell, which 1.1 did not store.
    EXPECT_EQ(settings.selectOptional(QString::fromLatin1(SettingsKeys::INTERFACE_DENSITY)), std::nullopt);
    EXPECT_EQ(an::ui::resolveThemeDensity(
                  setting(settings, SettingsKeys::INTERFACE_DENSITY, SettingsDefaults::INTERFACE_DENSITY)),
              an::ui::ThemeDensity::Compact);
    EXPECT_EQ(settings.selectOptional(QString::fromLatin1(SettingsKeys::INTERFACE_SHELL)), std::nullopt);
    EXPECT_EQ(setting(settings, SettingsKeys::INTERFACE_SHELL, SettingsDefaults::INTERFACE_SHELL),
              QStringLiteral("observatory"))
        << "a 1.1 store starts another shell than the Observatory one";

    // Closed again, the file holds what it held: every table, the format, every key with its value or its NULL.
    opened.close();
    const StoredRows after = rowsOf(store);
    EXPECT_EQ(after.tables, before.tables);
    EXPECT_EQ(after.format, before.format);
    EXPECT_EQ(after.settings.size(), before.settings.size());
    for (const auto& [key, value] : before.settings)
    {
        const auto kept = after.settings.find(key);
        ASSERT_NE(kept, after.settings.end()) << key.toStdString() << " was lost";
        EXPECT_EQ(kept->second, value) << key.toStdString();
    }
    for (const auto& [key, value] : after.settings)
    {
        EXPECT_EQ(before.settings.count(key), 1u) << key.toStdString() << " was written into the 1.1 store";
    }
    const QStringList setAside = QDir(root.path()).entryList({QStringLiteral("afternight_data.*.db")}, QDir::Files);
    EXPECT_TRUE(setAside.isEmpty()) << setAside.join(QStringLiteral(", ")).toStdString();
}
