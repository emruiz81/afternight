// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Raw Images page and the settings import over the published RAW policy: the page shows the stored settings read
/// through the table every open reads them through, saving stores them and publishes one policy for the reads asked
/// after it, an import publishes the mode, the gains and auto-rotate together, and the demosaic list is the engine's; a
/// settings database the page wrote and a settings file a page exported are read so, and a settings file of another
/// format is refused with nothing imported.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "app/DemosaicTextMaps.h"
#include "app/FilePolicies.h"
#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/RawImageSettingsPage.h"
#include "controls/settings/SettingsKeys.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QStringList>
#include <QTemporaryDir>

#include <utility>

#include <gtest/gtest.h>

#include <array>

namespace
{

using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::SettingsStore;
using an::ui::RawOpenSettings;
using an::ui::RawWhiteBalanceSetting;

class RawSettingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
    }

    void TearDown() override
    {
        installed.reset();
        context.reset();
    }

    /// The page's controls, found by type in the order the page lays them out.
    struct Controls
    {
        QComboBox* mode = nullptr;
        QComboBox* demosaic = nullptr;
        QList<QDoubleSpinBox*> gains;
        QCheckBox* autoRotate = nullptr;
    };

    [[nodiscard]] static Controls controlsOf(RawImageSettingsPage& page)
    {
        Controls controls;
        const QList<QComboBox*> combos = page.findChildren<QComboBox*>();
        EXPECT_EQ(combos.size(), 2);
        if (combos.size() == 2)
        {
            controls.mode = combos[0];
            controls.demosaic = combos[1];
        }
        controls.gains = page.findChildren<QDoubleSpinBox*>();
        EXPECT_EQ(controls.gains.size(), 3);
        controls.autoRotate = page.findChild<QCheckBox*>();
        EXPECT_NE(controls.autoRotate, nullptr);
        return controls;
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

TEST_F(RawSettingsTest, ThePageShowsTheStoredSettingsReadThroughTheTableEveryOpenReads)
{
    SettingsStore store;
    store.store(SettingsKeys::RAW_WHITE_BALANCE_MODE, QStringLiteral("4"));
    store.store(SettingsKeys::RAW_CUSTOM_WB_R, QStringLiteral("2.000000"));
    store.store(SettingsKeys::RAW_CUSTOM_WB_G, QStringLiteral("1.000000"));
    store.store(SettingsKeys::RAW_CUSTOM_WB_B, QStringLiteral("1.500000"));
    store.store(SettingsKeys::RAW_AUTO_ROTATE, QStringLiteral("0"));

    RawImageSettingsPage page(store.persistence());
    page.loadSettings();
    const Controls controls = controlsOf(page);

    const RawOpenSettings staged = page.stagedSettings();
    EXPECT_EQ(staged.whiteBalance, RawWhiteBalanceSetting::Custom);
    EXPECT_DOUBLE_EQ(staged.customGains[0], 2.0);
    EXPECT_DOUBLE_EQ(staged.customGains[2], 1.5);
    EXPECT_FALSE(staged.autoRotate);
    EXPECT_FALSE(page.hasUnsavedChanges());
    EXPECT_EQ(staged, an::ui::readRawOpenSettings([&store](const QString& key, const QString& fallback) {
                  return store.persistence()->select(key, fallback);
              }));
}

TEST_F(RawSettingsTest, AStoredModeOutsideTheTableShowsDaylightAsEveryOpenReadsIt)
{
    SettingsStore store;
    store.store(SettingsKeys::RAW_WHITE_BALANCE_MODE, QStringLiteral("9"));

    RawImageSettingsPage page(store.persistence());
    page.loadSettings();

    EXPECT_EQ(page.stagedSettings().whiteBalance, RawWhiteBalanceSetting::Daylight);
}

TEST_F(RawSettingsTest, SavingStoresTheSettingsAndPublishesOnePolicyForTheReadsAskedAfterIt)
{
    SettingsStore store;
    RawImageSettingsPage page(store.persistence());
    page.loadSettings();
    Controls controls = controlsOf(page);

    // A read asked before the change keeps the policy it was asked under.
    const std::shared_ptr<const an::ui::RawOpenPolicy> before = context->rawPolicies.rawOpenPolicy();
    ASSERT_NE(before, nullptr);

    controls.mode->setCurrentIndex(
        controls.mode->findData(an::ui::storedRawWhiteBalance(RawWhiteBalanceSetting::Custom)));
    controls.gains[0]->setValue(1.25);
    controls.gains[1]->setValue(1.0);
    controls.gains[2]->setValue(0.5);
    controls.autoRotate->setChecked(false);
    ASSERT_TRUE(page.validateSettings().isValid);
    page.saveSettings();

    EXPECT_EQ(store.stored(SettingsKeys::RAW_WHITE_BALANCE_MODE), QStringLiteral("4"));
    EXPECT_EQ(store.stored(SettingsKeys::RAW_CUSTOM_WB_R), QStringLiteral("1.250000"));
    EXPECT_EQ(store.stored(SettingsKeys::RAW_CUSTOM_WB_B), QStringLiteral("0.500000"));
    EXPECT_EQ(store.stored(SettingsKeys::RAW_AUTO_ROTATE), QStringLiteral("0"));
    EXPECT_EQ(store.stored(SettingsKeys::RAW_DEMOSAIC_ALGORITHM), QStringLiteral("rcd"));

    ASSERT_EQ(context->rawPolicies.published.size(), 1u);
    const RawOpenSettings published = context->rawPolicies.published.front();
    EXPECT_EQ(published.whiteBalance, RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(published.customGains, (std::array<double, 3u>{1.25, 1.0, 0.5}));
    EXPECT_FALSE(published.autoRotate);
    EXPECT_EQ(context->rawPolicies.rawOpenPolicy()->settings(), published);
    EXPECT_EQ(before->settings(), RawOpenSettings{});
    EXPECT_FALSE(page.hasUnsavedChanges());
}

TEST_F(RawSettingsTest, ThePageWithNoStoreShowsThePolicyPublishedLast)
{
    RawOpenSettings custom;
    custom.whiteBalance = RawWhiteBalanceSetting::None;
    custom.autoRotate = false;
    ASSERT_TRUE(context->rawPolicies.setRawOpenSettings(custom).ok());

    RawImageSettingsPage page(nullptr);
    page.loadSettings();

    EXPECT_EQ(page.stagedSettings(), custom);
}

// The page offers the engine's six methods; a stored method the product does not know - DCB among them - shows as
// RCD, the setting's default, and the page says nothing about it.
TEST_F(RawSettingsTest, TheDemosaicListIsTheEnginesSixMethodsAndAnUnknownStoredOneShowsTheDefault)
{
    SettingsStore store;
    store.store(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, QStringLiteral("dcb"));
    RawImageSettingsPage page(store.persistence());
    page.loadSettings();
    const Controls controls = controlsOf(page);

    QStringList keys;
    for (int index = 0; index < controls.demosaic->count(); ++index)
    {
        keys << controls.demosaic->itemData(index).toString();
    }
    EXPECT_EQ(keys, (QStringList{"rcd", "amaze", "lmmse", "ahd", "vng4", "bilinear"}));

    EXPECT_EQ(controls.demosaic->currentData().toString(), QStringLiteral("rcd"));
    for (const QLabel* label : page.findChildren<QLabel*>())
    {
        EXPECT_FALSE(label->text().contains(QStringLiteral("DCB"))) << label->text().toStdString();
    }
    EXPECT_EQ(page.findChild<QLabel*>(QStringLiteral("rawRetiredDemosaicLabel")), nullptr);

    store.store(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, QStringLiteral("amaze"));
    page.loadSettings();
    EXPECT_EQ(controls.demosaic->currentData().toString(), QStringLiteral("amaze"));
}

// The RAW settings a Raw Images page stored - a custom white balance of 1.5, 0.9 and 1.25, auto-rotate off and AMaZE -
// are read from a copy of the settings database the page left: the page and every open read the same settings, and
// nothing else is stored under the RAW settings' keys.
TEST_F(RawSettingsTest, TheStoredRawSettingsAreReadAsTheDatabaseHoldsThem)
{
    QTemporaryDir copies;
    ASSERT_TRUE(copies.isValid());
    const QString database = QDir(copies.path()).filePath(QStringLiteral("settings.db"));
    {
        SettingsStore writer;
        RawImageSettingsPage page(writer.persistence());
        page.loadSettings();
        Controls controls = controlsOf(page);
        controls.mode->setCurrentIndex(
            controls.mode->findData(an::ui::storedRawWhiteBalance(RawWhiteBalanceSetting::Custom)));
        controls.gains[0]->setValue(1.5);
        controls.gains[1]->setValue(0.9);
        controls.gains[2]->setValue(1.25);
        controls.autoRotate->setChecked(false);
        controls.demosaic->setCurrentIndex(controls.demosaic->findData(QStringLiteral("amaze")));
        ASSERT_TRUE(page.validateSettings().isValid);
        page.saveSettings();
        ASSERT_TRUE(writer.copyTo(database));
    }
    SettingsStore store(database);

    QStringList stored = store.keysUnder(QStringLiteral("raw/"));
    stored.sort();
    EXPECT_EQ(stored, (QStringList{QString::fromLatin1(SettingsKeys::RAW_AUTO_ROTATE),
                                   QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_B),
                                   QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_G),
                                   QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_R),
                                   QString::fromLatin1(SettingsKeys::RAW_DEMOSAIC_ALGORITHM),
                                   QString::fromLatin1(SettingsKeys::RAW_WHITE_BALANCE_MODE)}));

    const RawOpenSettings read = an::ui::readRawOpenSettings(store.reader());
    EXPECT_EQ(read.whiteBalance, RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(read.customGains, (std::array<double, 3u>{1.5, 0.9, 1.25}));
    EXPECT_FALSE(read.autoRotate);
    EXPECT_EQ(
        an::ui::demosaicMethodFromSettingsKey(store.stored(SettingsKeys::RAW_DEMOSAIC_ALGORITHM).value_or(QString()))
            .method,
        an::DemosaicMethod::Amaze);

    RawImageSettingsPage page(store.persistence());
    page.loadSettings();
    const Controls controls = controlsOf(page);
    EXPECT_EQ(page.stagedSettings(), read);
    EXPECT_EQ(controls.demosaic->currentData().toString(), QStringLiteral("amaze"));
}

// The settings file a General page exports imports through another General page as the database it came from holds
// them: each RAW, processing and export setting it states is stored as that database holds it, and the RAW ones reach
// the reads asked for after the import as one policy. The file's logging section is left out: an import writes and
// applies it at once, to the process's own logger.
TEST_F(RawSettingsTest, ExportedSettingsImportAsTheDatabaseTheyCameFromHoldsThem)
{
    SettingsStore source;
    for (const auto& [key, value] :
         {std::pair{SettingsKeys::RAW_WHITE_BALANCE_MODE, "4"}, std::pair{SettingsKeys::RAW_CUSTOM_WB_R, "1.500000"},
          std::pair{SettingsKeys::RAW_CUSTOM_WB_G, "0.900000"}, std::pair{SettingsKeys::RAW_CUSTOM_WB_B, "1.250000"},
          std::pair{SettingsKeys::RAW_AUTO_ROTATE, "0"}, std::pair{SettingsKeys::RAW_DEMOSAIC_ALGORITHM, "amaze"},
          std::pair{SettingsKeys::PROCESSING_MAX_THREADS_BATCH, "3"},
          std::pair{SettingsKeys::PROCESSING_MAX_THREADS_PREVIEW, "2"},
          std::pair{SettingsKeys::PROCESSING_MEMORY_LIMIT_MB, "2048"},
          std::pair{SettingsKeys::EXPORT_TIFF_COMPRESSION, "lzw"}, std::pair{SettingsKeys::EXPORT_JPEG_QUALITY, "80"},
          std::pair{SettingsKeys::EXPORT_PNG_COMPRESSION, "3"}, std::pair{SettingsKeys::EXPORT_EMBED_EXIF, "0"},
          std::pair{SettingsKeys::EXPORT_EMBED_HISTORY, "1"}})
    {
        source.store(key, QString::fromLatin1(value));
    }
    GeneralSettingsPage exporter(source.persistence());
    exporter.loadSettings();
    QJsonObject root = exporter.exportedSettings();
    ASSERT_TRUE(root.contains(QStringLiteral("logging")));
    root.remove(QStringLiteral("logging"));
    QTemporaryDir files;
    ASSERT_TRUE(files.isValid());
    const QString path = QDir(files.path()).filePath(QStringLiteral("afternight_settings.json"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
    }

    SettingsStore store;
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();
    QString error;
    ASSERT_TRUE(page.importSettingsFile(path, &error)) << error.toStdString();
    page.saveSettings();

    for (const char* key :
         {SettingsKeys::RAW_WHITE_BALANCE_MODE, SettingsKeys::RAW_CUSTOM_WB_R, SettingsKeys::RAW_CUSTOM_WB_G,
          SettingsKeys::RAW_CUSTOM_WB_B, SettingsKeys::RAW_AUTO_ROTATE, SettingsKeys::RAW_DEMOSAIC_ALGORITHM,
          SettingsKeys::PROCESSING_MAX_THREADS_BATCH, SettingsKeys::PROCESSING_MAX_THREADS_PREVIEW,
          SettingsKeys::PROCESSING_MEMORY_LIMIT_MB, SettingsKeys::EXPORT_TIFF_COMPRESSION,
          SettingsKeys::EXPORT_JPEG_QUALITY, SettingsKeys::EXPORT_PNG_COMPRESSION, SettingsKeys::EXPORT_EMBED_EXIF,
          SettingsKeys::EXPORT_EMBED_HISTORY})
    {
        ASSERT_TRUE(source.stored(key).has_value()) << key;
        EXPECT_EQ(store.stored(key), source.stored(key)) << key;
    }

    ASSERT_EQ(context->rawPolicies.published.size(), 1u);
    const RawOpenSettings published = context->rawPolicies.published.front();
    EXPECT_EQ(published.whiteBalance, RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(published.customGains, (std::array<double, 3u>{1.5, 0.9, 1.25}));
    EXPECT_FALSE(published.autoRotate);
}

// A settings file of another format, or one that states none, is refused naming the file, the format found and the one
// this version reads, and nothing it states is staged, stored or published.
TEST_F(RawSettingsTest, ASettingsFileOfAnotherFormatIsRefusedAndNothingImported)
{
    SettingsStore store;
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();
    QTemporaryDir files;
    ASSERT_TRUE(files.isValid());
    const QString path = QDir(files.path()).filePath(QStringLiteral("afternight_settings.json"));
    const QJsonObject raw{{QStringLiteral("whiteBalanceMode"), 4}, {QStringLiteral("autoRotate"), false}};
    for (const QString& version : {QStringLiteral("0.9"), QStringLiteral("2.0"), QString()})
    {
        QJsonObject root{{QStringLiteral("raw"), raw}};
        if (!version.isEmpty())
        {
            root.insert(QStringLiteral("version"), version);
        }
        {
            QFile file(path);
            ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(root).toJson());
        }
        QString error;
        EXPECT_FALSE(page.importSettingsFile(path, &error)) << version.toStdString();
        EXPECT_TRUE(error.contains(QDir::toNativeSeparators(path))) << error.toStdString();
        EXPECT_TRUE(error.contains(version.isEmpty() ? QStringLiteral("states no format version")
                                                     : QStringLiteral("written in format %1").arg(version)))
            << error.toStdString();
        EXPECT_TRUE(error.contains(QStringLiteral("this version of AfterNight reads format 1.0")))
            << error.toStdString();
    }
    page.saveSettings();
    EXPECT_FALSE(store.stored(SettingsKeys::RAW_WHITE_BALANCE_MODE).has_value());
    EXPECT_FALSE(store.stored(SettingsKeys::RAW_AUTO_ROTATE).has_value());
    EXPECT_TRUE(context->rawPolicies.published.empty());
}

TEST_F(RawSettingsTest, ResetPageSelectsNoneAsItAlwaysHas)
{
    SettingsStore store;
    RawImageSettingsPage page(store.persistence());
    page.loadSettings();
    EXPECT_EQ(page.stagedSettings().whiteBalance, RawWhiteBalanceSetting::Daylight);

    page.resetToDefaults();

    EXPECT_EQ(page.stagedSettings().whiteBalance, RawWhiteBalanceSetting::None);
    EXPECT_TRUE(page.stagedSettings().autoRotate);
    EXPECT_TRUE(page.hasUnsavedChanges());
}

// An imported auto-rotate takes effect with the rest of the import, for the reads asked after it, rather than waiting
// for the next start.
TEST_F(RawSettingsTest, ImportingAutoRotateAloneReachesTheReadsAfterTheImport)
{
    SettingsStore store;
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();

    const QJsonObject raw{{QStringLiteral("autoRotate"), false}};
    ASSERT_TRUE(page.stageImportedSettings(QJsonObject{{QStringLiteral("raw"), raw}}));
    page.saveSettings();

    EXPECT_EQ(store.stored(SettingsKeys::RAW_AUTO_ROTATE), QStringLiteral("0"));
    ASSERT_EQ(context->rawPolicies.published.size(), 1u);
    EXPECT_FALSE(context->rawPolicies.published.front().autoRotate);
    EXPECT_FALSE(context->rawPolicies.rawOpenPolicy()->settings().autoRotate);
}

TEST_F(RawSettingsTest, ImportingRawSettingsPublishesTheModeTheGainsAndAutoRotateAsOnePolicy)
{
    SettingsStore store;
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();

    const QJsonObject raw{{QStringLiteral("whiteBalanceMode"), 4},
                          {QStringLiteral("customWbR"), 1.5},
                          {QStringLiteral("customWbG"), 1.0},
                          {QStringLiteral("customWbB"), 2.0},
                          {QStringLiteral("autoRotate"), false}};
    ASSERT_TRUE(page.stageImportedSettings(QJsonObject{{QStringLiteral("raw"), raw}}));
    page.saveSettings();

    ASSERT_EQ(context->rawPolicies.published.size(), 1u);
    const RawOpenSettings published = context->rawPolicies.published.front();
    EXPECT_EQ(published.whiteBalance, RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(published.customGains, (std::array<double, 3u>{1.5, 1.0, 2.0}));
    EXPECT_FALSE(published.autoRotate);
}

TEST_F(RawSettingsTest, AnImportWithNoRawSettingPublishesNothing)
{
    SettingsStore store;
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();

    const QJsonObject exported{{QStringLiteral("jpegQuality"), 80}};
    ASSERT_TRUE(page.stageImportedSettings(QJsonObject{{QStringLiteral("export"), exported}}));
    page.saveSettings();

    EXPECT_TRUE(context->rawPolicies.published.empty());
}

} // namespace
