// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Export page reads and writes the export settings through the table every save reads them through, so what it
/// shows is what a save writes with - the settings a settings database holds among them.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "app/FilePolicies.h"
#include "controls/settings/ExportSettingsPage.h"
#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/SettingsKeys.h"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QSlider>

#include <gtest/gtest.h>

namespace
{

using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::SettingsStore;

class ExportSettingsTest : public ::testing::Test
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

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

TEST_F(ExportSettingsTest, AStoredCompressionTheTableDoesNotNameShowsTheLzwASaveWritesWith)
{
    SettingsStore store;
    store.store(SettingsKeys::EXPORT_TIFF_COMPRESSION, QStringLiteral("packbits"));
    ExportSettingsPage page(store.persistence());
    page.loadSettings();

    auto* combo = page.findChild<QComboBox*>();
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentData().toString(), QStringLiteral("lzw"));
    EXPECT_EQ(an::ui::tiffCompressionFromStored(QStringLiteral("packbits")), an::OutputCompression::Lzw);
    EXPECT_FALSE(page.hasUnsavedChanges());
}

TEST_F(ExportSettingsTest, SavingStoresTheTextsASaveReadsBack)
{
    SettingsStore store;
    ExportSettingsPage page(store.persistence());
    page.loadSettings();

    auto* combo = page.findChild<QComboBox*>();
    const QList<QSlider*> sliders = page.findChildren<QSlider*>();
    const QList<QCheckBox*> checks = page.findChildren<QCheckBox*>();
    auto* xisfCompression = page.findChild<QComboBox*>(QStringLiteral("xisfCompression"));
    auto* xisfShuffle = page.findChild<QCheckBox*>(QStringLiteral("xisfByteShuffle"));
    auto* xisfChecksum = page.findChild<QComboBox*>(QStringLiteral("xisfChecksum"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(xisfCompression, nullptr);
    ASSERT_NE(xisfShuffle, nullptr);
    ASSERT_NE(xisfChecksum, nullptr);
    ASSERT_EQ(sliders.size(), 2);
    ASSERT_EQ(checks.size(), 3);
    combo->setCurrentIndex(combo->findData(QStringLiteral("zip")));
    sliders[0]->setValue(80);
    sliders[1]->setValue(9);
    checks[1]->setChecked(false);
    checks[2]->setChecked(true);
    xisfCompression->setCurrentIndex(xisfCompression->findData(QStringLiteral("zstd")));
    xisfShuffle->setChecked(false);
    xisfChecksum->setCurrentIndex(xisfChecksum->findData(QStringLiteral("sha256")));
    page.saveSettings();

    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_TIFF_COMPRESSION), QStringLiteral("zip"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_JPEG_QUALITY), QStringLiteral("80"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_PNG_COMPRESSION), QStringLiteral("9"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_EMBED_EXIF), QStringLiteral("0"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_EMBED_HISTORY), QStringLiteral("1"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_XISF_COMPRESSION), QStringLiteral("zstd"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE), QStringLiteral("0"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_XISF_CHECKSUM), QStringLiteral("sha256"));

    const an::ui::ExportSettings read = an::ui::readExportSettings(
        [&store](const QString& key, const QString& fallback) { return store.persistence()->select(key, fallback); });
    EXPECT_EQ(read.tiffCompression, an::OutputCompression::Deflate);
    EXPECT_EQ(read.jpegQuality, 80u);
    EXPECT_EQ(read.pngCompressionLevel, 9u);
    EXPECT_FALSE(read.embedExif);
    EXPECT_TRUE(read.embedHistory);
    EXPECT_EQ(read.xisfCompression, an::OutputCompression::Zstd);
    EXPECT_FALSE(read.xisfByteShuffle);
    EXPECT_EQ(read.xisfChecksum, an::OutputChecksum::Sha256);
}

// Byte shuffling is a step of a compression, so the page offers it only beside one, and shows a stored form as a save
// reads it.
TEST_F(ExportSettingsTest, TheXisfFormIsShownAsASaveReadsItAndShufflingOnlyBesideACompression)
{
    SettingsStore store;
    store.store(SettingsKeys::EXPORT_XISF_COMPRESSION, QStringLiteral("lz4hc"));
    store.store(SettingsKeys::EXPORT_XISF_CHECKSUM, QStringLiteral("sha256"));
    ExportSettingsPage page(store.persistence());
    page.loadSettings();
    auto* compression = page.findChild<QComboBox*>(QStringLiteral("xisfCompression"));
    auto* shuffle = page.findChild<QCheckBox*>(QStringLiteral("xisfByteShuffle"));
    auto* checksum = page.findChild<QComboBox*>(QStringLiteral("xisfChecksum"));
    ASSERT_NE(compression, nullptr);
    ASSERT_NE(shuffle, nullptr);
    ASSERT_NE(checksum, nullptr);
    EXPECT_EQ(compression->currentData().toString(), QStringLiteral("lz4hc"));
    EXPECT_EQ(checksum->currentData().toString(), QStringLiteral("sha256"));
    EXPECT_TRUE(shuffle->isChecked());
    EXPECT_TRUE(shuffle->isEnabled());
    EXPECT_FALSE(page.hasUnsavedChanges());
    compression->setCurrentIndex(compression->findData(QStringLiteral("none")));
    EXPECT_FALSE(shuffle->isEnabled());
    EXPECT_TRUE(page.hasUnsavedChanges());
    // The page names no checksum PixInsight refuses.
    for (int index = 0; index < checksum->count(); ++index)
        EXPECT_FALSE(checksum->itemData(index).toString().startsWith(QStringLiteral("sha3")));
}

// A settings file carries the XISF form among the export settings, so importing one stores the texts a save reads.
TEST_F(ExportSettingsTest, AnImportedSettingsFileStoresTheXisfFormItStates)
{
    SettingsStore store;
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();
    const QJsonObject exported{{QStringLiteral("xisfCompression"), QStringLiteral("lz4hc")},
                               {QStringLiteral("xisfByteShuffle"), false},
                               {QStringLiteral("xisfChecksum"), QStringLiteral("sha256")}};
    ASSERT_TRUE(page.stageImportedSettings(QJsonObject{{QStringLiteral("export"), exported}}));
    page.saveSettings();

    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_XISF_COMPRESSION), QStringLiteral("lz4hc"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE), QStringLiteral("0"));
    EXPECT_EQ(store.stored(SettingsKeys::EXPORT_XISF_CHECKSUM), QStringLiteral("sha256"));
    const an::ui::ExportSettings read = an::ui::readExportSettings(
        [&store](const QString& key, const QString& fallback) { return store.persistence()->select(key, fallback); });
    EXPECT_EQ(read.xisfCompression, an::OutputCompression::Lz4Hc);
    EXPECT_FALSE(read.xisfByteShuffle);
    EXPECT_EQ(read.xisfChecksum, an::OutputChecksum::Sha256);
}

TEST_F(ExportSettingsTest, EveryXisfFormTheTablesNameIsStoredAsItsText)
{
    for (const auto& [compression, text] :
         std::vector<std::pair<an::OutputCompression, QString>>{{an::OutputCompression::None, "none"},
                                                                {an::OutputCompression::Deflate, "zlib"},
                                                                {an::OutputCompression::Lz4, "lz4"},
                                                                {an::OutputCompression::Lz4Hc, "lz4hc"},
                                                                {an::OutputCompression::Zstd, "zstd"}})
    {
        EXPECT_EQ(an::ui::storedXisfCompression(compression), text);
        EXPECT_EQ(an::ui::xisfCompressionFromStored(text), compression);
    }
    EXPECT_EQ(an::ui::storedXisfCompression(an::OutputCompression::Rice), QStringLiteral("none"));
    for (const auto& [checksum, text] : std::vector<std::pair<an::OutputChecksum, QString>>{
             {an::OutputChecksum::None, "none"}, {an::OutputChecksum::Sha256, "sha256"}})
    {
        EXPECT_EQ(an::ui::storedXisfChecksum(checksum), text);
        EXPECT_EQ(an::ui::xisfChecksumFromStored(text), checksum);
    }
    EXPECT_EQ(an::ui::storedXisfChecksum(an::OutputChecksum::Sha3Digest512), QStringLiteral("none"));
}

// The export settings an Export page stored - LZW, quality 80, compression level 3, no Exif, the processing history -
// are read from a copy of the settings database as the page left it, and the page shows them unchanged.
TEST_F(ExportSettingsTest, TheStoredExportSettingsAreReadAsTheDatabaseHoldsThem)
{
    QTemporaryDir copies;
    ASSERT_TRUE(copies.isValid());
    const QString database = QDir(copies.path()).filePath(QStringLiteral("settings.db"));
    {
        SettingsStore writer;
        for (const auto& [key, value] :
             {std::pair{SettingsKeys::EXPORT_TIFF_COMPRESSION, "lzw"},
              std::pair{SettingsKeys::EXPORT_JPEG_QUALITY, "80"}, std::pair{SettingsKeys::EXPORT_PNG_COMPRESSION, "3"},
              std::pair{SettingsKeys::EXPORT_EMBED_EXIF, "0"}, std::pair{SettingsKeys::EXPORT_EMBED_HISTORY, "1"}})
        {
            writer.store(key, QString::fromLatin1(value));
        }
        ASSERT_TRUE(writer.copyTo(database));
    }
    SettingsStore store(database);

    const an::ui::ExportSettings read = an::ui::readExportSettings(store.reader());
    EXPECT_EQ(read.tiffCompression, an::OutputCompression::Lzw);
    EXPECT_EQ(read.jpegQuality, 80u);
    EXPECT_EQ(read.pngCompressionLevel, 3u);
    EXPECT_FALSE(read.embedExif);
    EXPECT_TRUE(read.embedHistory);

    ExportSettingsPage page(store.persistence());
    page.loadSettings();
    auto* combo = page.findChild<QComboBox*>();
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentData().toString(), QStringLiteral("lzw"));
    const QList<QSlider*> sliders = page.findChildren<QSlider*>();
    ASSERT_EQ(sliders.size(), 2);
    EXPECT_EQ(sliders[0]->value(), 80);
    EXPECT_EQ(sliders[1]->value(), 3);
    EXPECT_FALSE(page.hasUnsavedChanges());
}

TEST_F(ExportSettingsTest, EveryCompressionTheTableNamesIsStoredAsItsText)
{
    for (const auto& [compression, text] :
         std::vector<std::pair<an::OutputCompression, QString>>{{an::OutputCompression::None, "none"},
                                                                {an::OutputCompression::Lzw, "lzw"},
                                                                {an::OutputCompression::Deflate, "zip"},
                                                                {an::OutputCompression::Jpeg, "jpeg"}})
    {
        EXPECT_EQ(an::ui::storedTiffCompression(compression), text);
        EXPECT_EQ(an::ui::tiffCompressionFromStored(text), compression);
    }
}

TEST_F(ExportSettingsTest, ThePageSaysNoFileIsWrittenWithAProcessingHistory)
{
    ExportSettingsPage page(nullptr);
    bool said = false;
    for (QLabel* label : page.findChildren<QLabel*>())
    {
        said = said || label->text().contains(QStringLiteral("No file is written with a processing history"));
    }
    EXPECT_TRUE(said);
}

} // namespace
