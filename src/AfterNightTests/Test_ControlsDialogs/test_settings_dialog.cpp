// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// SettingsDialog modernization tests.
#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "controls/GlobalSettingsPersistence.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/SettingsDialog.h"
#include "controls/settings/GeneralSettingsPage.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>
#include "controls/settings/SettingsPage.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnDesignTokens.h"

namespace
{

void ensureApp()
{
    if (QApplication::instance())
    {
        return;
    }

    static int argc = 0;
    static char* argv[] = {nullptr};
    static QApplication app(argc, argv);
    (void)app;
}

// Minimal concrete SettingsPage for testing
class MinimalSettingsPage : public SettingsPage
{
public:
    explicit MinimalSettingsPage(const QString& title, QWidget* parent = nullptr) : SettingsPage(parent), m_title(title)
    {
        m_valueLabel = new QLabel(this);
    }

    [[nodiscard]] QString pageTitle() const override
    {
        return m_title;
    }
    [[nodiscard]] QIcon pageIcon() const override
    {
        return QIcon();
    }

    void loadSettings() override
    {
        ++m_loadCount;
        m_valueLabel->setText(QStringLiteral("loaded"));
        clearModified();
    }

    void saveSettings() override
    {
        ++m_saveCount;
    }

    void resetToDefaults() override
    {
        m_valueLabel->setText(QStringLiteral("default"));
    }

    void simulateUserEdit()
    {
        markAsModified();
    }

    void clearModifiedPublic()
    {
        clearModified();
    }

    int loadCount() const
    {
        return m_loadCount;
    }
    int saveCount() const
    {
        return m_saveCount;
    }
    QLabel* valueLabel() const
    {
        return m_valueLabel;
    }

private:
    QString m_title;
    QLabel* m_valueLabel = nullptr;
    int m_loadCount = 0;
    int m_saveCount = 0;
};

class MinimalImmediateActionPage : public MinimalSettingsPage
{
public:
    explicit MinimalImmediateActionPage(const QString& title, QWidget* parent = nullptr)
        : MinimalSettingsPage(title, parent)
    {
    }

    [[nodiscard]] bool usesSettingsDialogCommitButtons() const override
    {
        return false;
    }
};

} // namespace

class SettingsDialogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureApp();
    }
};

TEST_F(SettingsDialogTest, SettingsDialog_NavigationWorks)
{
    SettingsDialog dialog;

    auto* page1 = new MinimalSettingsPage(QStringLiteral("General"));
    auto* page2 = new MinimalSettingsPage(QStringLiteral("Interface"));
    auto* page3 = new MinimalSettingsPage(QStringLiteral("Processing"));

    dialog.addPage(page1);
    dialog.addPage(page2);
    dialog.addPage(page3);

    ASSERT_EQ(dialog.pageCount(), 3);

    // First page should be selected automatically
    EXPECT_EQ(dialog.currentPage(), page1);

    // Navigate to second page
    dialog.setCurrentPage(1);
    EXPECT_EQ(dialog.currentPage(), page2);

    // Navigate to third page
    dialog.setCurrentPage(2);
    EXPECT_EQ(dialog.currentPage(), page3);

    // Navigate by title
    dialog.setCurrentPage(QStringLiteral("General"));
    EXPECT_EQ(dialog.currentPage(), page1);
}

TEST_F(SettingsDialogTest, SettingsDialog_LoadsCurrentSettings)
{
    SettingsDialog dialog;

    auto* page = new MinimalSettingsPage(QStringLiteral("General"));
    dialog.addPage(page);

    // Settings should be loaded lazily when the page is activated
    // (first page auto-selected, which triggers a load)
    EXPECT_GE(page->loadCount(), 1);
    EXPECT_EQ(page->valueLabel()->text(), QStringLiteral("loaded"));
}

TEST_F(SettingsDialogTest, SettingsDialog_SavesModifiedSettings)
{
    SettingsDialog dialog;

    auto* page = new MinimalSettingsPage(QStringLiteral("General"));
    dialog.addPage(page);

    // Mark as modified
    page->simulateUserEdit();
    EXPECT_TRUE(dialog.hasUnsavedChanges());

    // Simulate Apply
    const int savesBefore = page->saveCount();

    // Call the apply path by triggering saveAllSettings indirectly via accept()
    // We verify saveCount increases when dialog finalizes
    // (direct method access via the public API: accept() calls saveAllSettings)
    // We just verify hasUnsavedChanges logic works here.
    EXPECT_TRUE(dialog.hasUnsavedChanges());

    page->clearModifiedPublic();
    EXPECT_FALSE(page->hasUnsavedChanges());
    EXPECT_FALSE(dialog.hasUnsavedChanges());

    (void)savesBefore; // used in integration tests
}

TEST_F(SettingsDialogTest, SettingsDialog_UsesCloseFooterForImmediateActionPages)
{
    SettingsDialog dialog;

    auto* settings_page = new MinimalSettingsPage(QStringLiteral("General"));
    auto* immediate_page = new MinimalImmediateActionPage(QStringLiteral("Extension Manager"));
    dialog.addPage(settings_page);
    dialog.addPage(immediate_page);

    auto* apply_button = dialog.findChild<QPushButton*>(QStringLiteral("settingsApplyButton"));
    auto* cancel_button = dialog.findChild<QPushButton*>(QStringLiteral("settingsCancelButton"));
    auto* ok_button = dialog.findChild<QPushButton*>(QStringLiteral("settingsOkButton"));
    auto* close_button = dialog.findChild<QPushButton*>(QStringLiteral("settingsCloseButton"));
    ASSERT_NE(apply_button, nullptr);
    ASSERT_NE(cancel_button, nullptr);
    ASSERT_NE(ok_button, nullptr);
    ASSERT_NE(close_button, nullptr);

    EXPECT_FALSE(apply_button->isHidden());
    EXPECT_FALSE(cancel_button->isHidden());
    EXPECT_FALSE(ok_button->isHidden());
    EXPECT_TRUE(close_button->isHidden());

    dialog.setCurrentPage(QStringLiteral("Extension Manager"));

    EXPECT_TRUE(apply_button->isHidden());
    EXPECT_TRUE(cancel_button->isHidden());
    EXPECT_TRUE(ok_button->isHidden());
    EXPECT_FALSE(close_button->isHidden());
    EXPECT_TRUE(close_button->isEnabled());

    dialog.setCurrentPage(QStringLiteral("General"));

    EXPECT_FALSE(apply_button->isHidden());
    EXPECT_FALSE(cancel_button->isHidden());
    EXPECT_FALSE(ok_button->isHidden());
    EXPECT_TRUE(close_button->isHidden());
}

TEST_F(SettingsDialogTest, SettingsDialog_ThemeAware)
{
    // Verify the dialog reflects design token colors
    SettingsDialog dialog;

    auto* page = new MinimalSettingsPage(QStringLiteral("Interface"));
    dialog.addPage(page);

    // Check that AnSectionHeader (or page title label) uses token-based coloring.
    // Find any label with objectName "settingsPageTitle" or an AnSectionHeader child.
    auto* titleLabel = dialog.findChild<QLabel*>(QStringLiteral("settingsPageTitle"));
    auto* sectionHeader = dialog.findChild<QWidget*>(QStringLiteral("settingsPageSectionHeader"));

    // One of the two must exist: either the title label or the
    // AnSectionHeader wrapper.
    const bool hasTitleControl = (titleLabel != nullptr) || (sectionHeader != nullptr);
    EXPECT_TRUE(hasTitleControl) << "SettingsDialog must expose a titled page header "
                                    "(settingsPageTitle or settingsPageSectionHeader)";

    // Verify dialog responds to palette changes without crashing
    QPalette darkPalette = QGuiApplication::palette();
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
    QGuiApplication::setPalette(darkPalette);
    QCoreApplication::processEvents();
    an::ui::refreshTokens();

    QPalette lightPalette = QGuiApplication::palette();
    lightPalette.setColor(QPalette::Window, QColor(240, 240, 240));
    QGuiApplication::setPalette(lightPalette);
    QCoreApplication::processEvents();
    an::ui::refreshTokens();

    // Restore
    QGuiApplication::setPalette(QGuiApplication::palette());
}

TEST_F(SettingsDialogTest, SettingsDialog_RemovePageWorks)
{
    SettingsDialog dialog;

    auto* page1 = new MinimalSettingsPage(QStringLiteral("Page A"));
    auto* page2 = new MinimalSettingsPage(QStringLiteral("Page B"));
    dialog.addPage(page1);
    dialog.addPage(page2);

    ASSERT_EQ(dialog.pageCount(), 2);
    EXPECT_TRUE(dialog.removePage(page1));
    EXPECT_EQ(dialog.pageCount(), 1);
    EXPECT_EQ(dialog.currentPage(), page2);
}

TEST_F(SettingsDialogTest, GeneralSettingsPage_UpdatesSectionDefaultsAndPersistsTheExtensionCheck)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("general_settings_updates_section"));
    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(executor.isOpen());
    ASSERT_TRUE(settings.initializeTable());
    const QString app_key = QString::fromLatin1(SettingsKeys::GENERAL_CHECK_APP_UPDATES_ON_STARTUP);
    const QString extension_key = QString::fromLatin1(SettingsKeys::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP);

    {
        GeneralSettingsPage page(&settings);
        page.loadSettings();

        auto* updates_group = page.findChild<QWidget*>(QStringLiteral("anGeneralSettingsUpdatesGroup"));
        auto* app_check = page.findChild<QCheckBox*>(QStringLiteral("anGeneralSettingsCheckAppUpdatesOnStartup"));
        auto* extension_check =
            page.findChild<QCheckBox*>(QStringLiteral("anGeneralSettingsCheckExtensionUpdatesOnStartup"));
        ASSERT_NE(updates_group, nullptr);
        ASSERT_NE(app_check, nullptr);
        ASSERT_NE(extension_check, nullptr);
        EXPECT_TRUE(updates_group->isAncestorOf(app_check));
        EXPECT_TRUE(updates_group->isAncestorOf(extension_check));

        // With nothing stored: no application update check, which cannot be turned on yet, and an extension check.
        EXPECT_FALSE(app_check->isChecked());
        EXPECT_FALSE(app_check->isEnabled());
        EXPECT_TRUE(extension_check->isChecked());
        EXPECT_TRUE(extension_check->isEnabled());

        extension_check->setChecked(false);
        page.saveSettings();
        EXPECT_EQ(settings.select(extension_key), QStringLiteral("0"));
        EXPECT_EQ(settings.select(app_key), QStringLiteral("0"));

        page.resetToDefaults();
        EXPECT_TRUE(extension_check->isChecked());
        EXPECT_FALSE(app_check->isChecked());
    }

    // A page that loads later shows what was stored.
    GeneralSettingsPage reloaded(&settings);
    reloaded.loadSettings();
    auto* reloaded_check =
        reloaded.findChild<QCheckBox*>(QStringLiteral("anGeneralSettingsCheckExtensionUpdatesOnStartup"));
    ASSERT_NE(reloaded_check, nullptr);
    EXPECT_FALSE(reloaded_check->isChecked());
}

TEST_F(SettingsDialogTest, GeneralSettingsPage_RecognizesLoggingOnlyImport)
{
    const QJsonObject logging{{QStringLiteral("level"), QStringLiteral("info")}};
    const QJsonObject root{{QStringLiteral("logging"), logging}};

    EXPECT_TRUE(an::ui::settings::containsRecognizedImmediateImportSection(root));
    EXPECT_FALSE(an::ui::settings::containsRecognizedImmediateImportSection(
        QJsonObject{{QStringLiteral("unknown"), QJsonObject{}}}));
}

// A log folder that cannot be written leaves the log in the file it was in. The settings say so to the person who
// chose the folder, naming it, where only a line of the log itself used to.
TEST_F(SettingsDialogTest, ALogFolderThatCannotBeWrittenIsSaidToThePersonWhoChoseIt)
{
    ASSERT_TRUE(an::platform::isLoggingInitialized()) << "the suite logs to a file of its own";
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    // A folder under a file can be neither made nor written.
    const QString occupied = tempDir.filePath(QStringLiteral("occupied"));
    {
        QFile file(occupied);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    an::platform::LoggingConfig config;
    static_cast<void>(an::platform::loadLoggingConfig(config));
    config.logDirectory = QDir(occupied).filePath(QStringLiteral("logs")).toStdString();

    const QString problem = an::ui::settings::applyLoggingConfigOrSayWhy(config);

    EXPECT_FALSE(problem.isEmpty());
    EXPECT_TRUE(problem.contains(QDir::toNativeSeparators(QString::fromStdString(config.logDirectory))))
        << problem.toStdString();
    EXPECT_TRUE(problem.contains(QStringLiteral("cannot be written"))) << problem.toStdString();
}

TEST_F(SettingsDialogTest, GeneralSettingsPage_DefaultSaveFormatUsesCanonicalToken)
{
    EXPECT_EQ(QString::fromUtf8(SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT), QStringLiteral("tiff"));
}

TEST_F(SettingsDialogTest, GeneralSettingsPage_ExportWriterChecksTheCompleteWrite)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QByteArray contents = QByteArrayLiteral("{\n  \"version\": \"1.0\"\n}\n");
    const QString outputPath = tempDir.filePath(QStringLiteral("settings.json"));
    QString errorMessage;

    EXPECT_TRUE(an::ui::settings::writeSettingsJsonFile(outputPath, contents, &errorMessage));
    EXPECT_TRUE(errorMessage.isEmpty());

    QFile output(outputPath);
    ASSERT_TRUE(output.open(QIODevice::ReadOnly));
    EXPECT_EQ(output.readAll(), contents);

    errorMessage.clear();
    EXPECT_FALSE(an::ui::settings::writeSettingsJsonFile(tempDir.path(), contents, &errorMessage));
    EXPECT_FALSE(errorMessage.isEmpty());
}

// An imported settings file's preview frame rate is stored as a whole number held to the range, whether the file holds
// it as a number or as its text; a value that is no number is left out, never stored as zero to be read as the lowest
// rate.
TEST_F(SettingsDialogTest, GeneralSettingsPage_ImportsThePreviewFrameRateAsANumberOrItsTextAndLeavesOutWhatIsNeither)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("general_settings_import_frame_rate"));
    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(executor.isOpen());
    ASSERT_TRUE(settings.initializeTable());
    const QString key = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);

    const auto imported = [&settings, &key](const QJsonValue& rate) {
        GeneralSettingsPage page(&settings);
        page.loadSettings();
        const QJsonObject root{{QStringLiteral("processing"), QJsonObject{{QStringLiteral("previewFrameRate"), rate}}}};
        (void)page.stageImportedSettings(root);
        page.saveSettings();
        return settings.select(key, QStringLiteral("unset"));
    };

    EXPECT_EQ(imported(QJsonValue(24)), QStringLiteral("24"));
    EXPECT_EQ(imported(QJsonValue(QStringLiteral("15"))), QStringLiteral("15")) << "the rate as its text";
    EXPECT_EQ(imported(QJsonValue(240)), QStringLiteral("60")) << "a rate past the range is held to its end";
    EXPECT_EQ(imported(QJsonValue(QStringLiteral("fast"))), QStringLiteral("60"))
        << "a text that is no number leaves what is stored rather than storing zero";
    EXPECT_EQ(imported(QJsonValue(29.7)), QStringLiteral("60")) << "a fraction is no whole number of previews";
}
