// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings page section an extension's settings schema builds: values saved under the extension's own scope, with
/// secret, action and transient fields left out; groups shown as cards; actions run off the GUI thread, collecting
/// fields from a dialog first, streaming console updates into a dialog that may stay open, filtering their results and
/// redacting their messages, or running silently when the extension is bound; and tool configuration that validates a
/// folder or an executable and hands the resolved record to the actions. The settings live in an in-memory database.

#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "components/AnCardWidget.h"
#include "components/AnInfoBanner.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/settings/ExtensionSettingsSection.h"
#include "theme/AnDesignTokens.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayout>
#include <QLineEdit>
#include <QMargins>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace
{

using an::ui::extensions::ExtensionEnvironmentSnapshot;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::extensions::ExtensionManifest;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionSdkBackend;
using an::ui::extensions::ExtensionSettingsSchemaSnapshot;
using an::ui::extensions::ExtensionSettingsSchemaState;

ExtensionLoadResult makeSettingsResult()
{
    ExtensionLoadResult result;
    result.source = ExtensionPackageSource::User;
    result.status = ExtensionLoadStatus::Ready;
    result.package_root = "/tmp/extensions/demo_settings";
    result.manifest_path = "/tmp/extensions/demo_settings/extension.json";

    ExtensionManifest manifest;
    manifest.id = "demo_settings";
    manifest.name = "Demo Settings";
    manifest.version = "1.0.0";
    manifest.entry_point = "demo_settings";
    manifest.category = "filters";
    manifest.package_root = result.package_root.string();
    manifest.manifest_path = result.manifest_path.string();
    manifest.sdk_backend = ExtensionSdkBackend::Runtime;
    result.manifest = manifest;

    ExtensionEnvironmentSnapshot environment;
    environment.state = ExtensionEnvironmentState::Ready;
    environment.environment_path = "/tmp/extensions/demo_settings/.venv";
    result.environment = environment;

    ExtensionSettingsSchemaSnapshot schema;
    schema.state = ExtensionSettingsSchemaState::Ready;
    schema.schema = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("gpu_enabled")},
                                           {QStringLiteral("type"), QStringLiteral("bool")},
                                           {QStringLiteral("label"), QStringLiteral("Enable GPU")},
                                           {QStringLiteral("default"), true}},
                               QJsonObject{{QStringLiteral("id"), QStringLiteral("models_dir")},
                                           {QStringLiteral("type"), QStringLiteral("dir_path")},
                                           {QStringLiteral("label"), QStringLiteral("Models Directory")},
                                           {QStringLiteral("default"), QStringLiteral("/tmp/models")}}};
    result.settings_schema = schema;

    return result;
}

QWidget* findSettingsWidgetById(QWidget* root, const QString& widget_id)
{
    if (!root)
    {
        return nullptr;
    }

    const auto widgets = root->findChildren<QWidget*>();
    for (QWidget* widget : widgets)
    {
        if (widget && widget->property("anExtensionUiWidgetId").toString() == widget_id)
        {
            return widget;
        }
    }
    return nullptr;
}

QCheckBox* findSettingsCheckBoxById(QWidget* root, const QString& widget_id)
{
    QWidget* container = findSettingsWidgetById(root, widget_id);
    return container ? container->findChild<QCheckBox*>(QStringLiteral("anExtensionUiCheckBox")) : nullptr;
}

QLineEdit* findSettingsLineEditById(QWidget* root, const QString& widget_id)
{
    QWidget* container = findSettingsWidgetById(root, widget_id);
    return container ? container->findChild<QLineEdit*>(QStringLiteral("anInputFieldLineEdit")) : nullptr;
}

QComboBox* findSettingsComboBoxById(QWidget* root, const QString& widget_id)
{
    QWidget* container = findSettingsWidgetById(root, widget_id);
    return container ? container->findChild<QComboBox*>(QStringLiteral("anExtensionUiComboBox")) : nullptr;
}

QPlainTextEdit* findSettingsConsoleById(QWidget* root, const QString& widget_id)
{
    QWidget* container = findSettingsWidgetById(root, widget_id);
    return container ? container->findChild<QPlainTextEdit*>(QStringLiteral("anExtensionUiConsoleOutput")) : nullptr;
}

an::ui::AnInfoBanner* findInfoBannerByObjectName(QWidget& root, const QString& object_name)
{
    for (QWidget* widget : root.findChildren<QWidget*>(object_name))
    {
        if (auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(widget))
        {
            return banner;
        }
    }
    return nullptr;
}

} // anonymous namespace

// Saving writes each field's value under the extension's own scope.
TEST(ExtensionSettingsSectionTest, SaveSettingsPersistsExtensionScopedValues)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_section_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionSettingsSection section(&settings);
    const ExtensionLoadResult result = makeSettingsResult();

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;

    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_demo_settings"));
    ASSERT_NE(host, nullptr);

    auto* gpu_enabled = findSettingsCheckBoxById(host, QStringLiteral("gpu_enabled"));
    ASSERT_NE(gpu_enabled, nullptr);
    gpu_enabled->click();

    auto* models_dir = findSettingsWidgetById(host, QStringLiteral("models_dir"));
    ASSERT_NE(models_dir, nullptr);
    auto* models_dir_line = models_dir->findChild<QLineEdit*>(QStringLiteral("anExtensionUiPickerLineEdit"));
    ASSERT_NE(models_dir_line, nullptr);
    models_dir_line->setText(QStringLiteral("/tmp/models/custom"));

    ASSERT_TRUE(section.saveSettings(&error)) << error;

    an::ui::extensions::ExtensionSdkService service(&settings);

    QCborValue gpu_value;
    ASSERT_TRUE(service.selectExtensionSetting("demo_settings", "gpu_enabled", &gpu_value, {}, &error)) << error;
    ASSERT_TRUE(gpu_value.isBool());
    EXPECT_FALSE(gpu_value.toBool());

    QCborValue models_dir_value;
    ASSERT_TRUE(service.selectExtensionSetting("demo_settings", "models_dir", &models_dir_value, {}, &error)) << error;
    ASSERT_TRUE(models_dir_value.isString());
    EXPECT_EQ(models_dir_value.toString(), QStringLiteral("/tmp/models/custom"));
}

// A group the schema asks to show as a card is a compact card with its title, its fields and the tokens' padding.
TEST(ExtensionSettingsSectionTest, SettingsGroupsCanRenderAsCards)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_card_group_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "card_settings";
    result.manifest->name = "Card Settings";
    result.settings_schema->schema = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("status")},
                                                            {QStringLiteral("type"), QStringLiteral("string")},
                                                            {QStringLiteral("label"), QStringLiteral("Status")},
                                                            {QStringLiteral("group"), QStringLiteral("Connection")},
                                                            {QStringLiteral("group_style"), QStringLiteral("card")},
                                                            {QStringLiteral("default"), QStringLiteral("Ready")},
                                                            {QStringLiteral("persist"), false},
                                                            {QStringLiteral("enabled"), false}}};

    ExtensionSettingsSection section(&settings);
    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;

    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_card_settings"));
    ASSERT_NE(host, nullptr);

    an::ui::AnCardWidget* card = nullptr;
    for (QWidget* widget : host->findChildren<QWidget*>())
    {
        if (auto* candidate = dynamic_cast<an::ui::AnCardWidget*>(widget))
        {
            card = candidate;
            break;
        }
    }
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->title(), QStringLiteral("Connection"));
    EXPECT_TRUE(card->property("anCardCompact").toBool());
    EXPECT_TRUE(card->property("anCardBodyPadded").toBool());
    ASSERT_NE(card->contentLayout(), nullptr);
    const QMargins body_margins = card->contentLayout()->contentsMargins();
    EXPECT_EQ(body_margins.left(), an::ui::tokens().spacing.sm);
    EXPECT_EQ(body_margins.top(), an::ui::tokens().spacing.sm);
    EXPECT_EQ(body_margins.right(), an::ui::tokens().spacing.sm);
    EXPECT_EQ(body_margins.bottom(), an::ui::tokens().spacing.sm);
    EXPECT_NE(findSettingsLineEditById(host, QStringLiteral("status")), nullptr);
}

// Saving leaves out secret, action and transient fields.
TEST(ExtensionSettingsSectionTest, SaveSettingsFiltersSecretActionAndTransientFields)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_filter_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "secure_settings";
    result.manifest->name = "Secure Settings";
    result.settings_schema->schema =
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("public_value")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Public Value")},
                               {QStringLiteral("default"), QStringLiteral("initial")}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_email")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Activation Email")},
                               {QStringLiteral("sensitive"), true},
                               {QStringLiteral("persist"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_key")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Activation Key")},
                               {QStringLiteral("secret"), true},
                               {QStringLiteral("sensitive"), true},
                               {QStringLiteral("persist"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("transient_note")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Transient Note")},
                               {QStringLiteral("persist"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("activate")},
                               {QStringLiteral("type"), QStringLiteral("action")},
                               {QStringLiteral("label"), QStringLiteral("Activate")}}};

    ExtensionSettingsSection section(&settings);
    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;

    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_secure_settings"));
    ASSERT_NE(host, nullptr);

    ASSERT_NE(findSettingsLineEditById(host, QStringLiteral("public_value")), nullptr);
    findSettingsLineEditById(host, QStringLiteral("public_value"))->setText(QStringLiteral("saved"));
    ASSERT_NE(findSettingsLineEditById(host, QStringLiteral("activation_email")), nullptr);
    findSettingsLineEditById(host, QStringLiteral("activation_email"))->setText(QStringLiteral("user@example.test"));
    ASSERT_NE(findSettingsLineEditById(host, QStringLiteral("activation_key")), nullptr);
    findSettingsLineEditById(host, QStringLiteral("activation_key"))->setText(QStringLiteral("SECRET-KEY"));
    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("activation_key"))->echoMode(), QLineEdit::Password);
    ASSERT_NE(findSettingsLineEditById(host, QStringLiteral("transient_note")), nullptr);
    findSettingsLineEditById(host, QStringLiteral("transient_note"))->setText(QStringLiteral("transient"));

    ASSERT_TRUE(section.saveSettings(&error)) << error;

    an::ui::extensions::ExtensionSdkService service(&settings);
    QCborValue public_value;
    ASSERT_TRUE(service.selectExtensionSetting("secure_settings", "public_value", &public_value, {}, &error)) << error;
    EXPECT_EQ(public_value.toString(), QStringLiteral("saved"));

    QCborValue filtered_value;
    ASSERT_TRUE(service.selectExtensionSetting("secure_settings", "activation_key", &filtered_value,
                                               QCborValue(QStringLiteral("missing")), &error))
        << error;
    EXPECT_EQ(filtered_value.toString(), QStringLiteral("missing"));
    ASSERT_TRUE(service.selectExtensionSetting("secure_settings", "activation_email", &filtered_value,
                                               QCborValue(QStringLiteral("missing")), &error))
        << error;
    EXPECT_EQ(filtered_value.toString(), QStringLiteral("missing"));
    ASSERT_TRUE(service.selectExtensionSetting("secure_settings", "transient_note", &filtered_value,
                                               QCborValue(QStringLiteral("missing")), &error))
        << error;
    EXPECT_EQ(filtered_value.toString(), QStringLiteral("missing"));
    ASSERT_TRUE(service.selectExtensionSetting("secure_settings", "activate", &filtered_value,
                                               QCborValue(QStringLiteral("missing")), &error))
        << error;
    EXPECT_EQ(filtered_value.toString(), QStringLiteral("missing"));
}

// An action that asks for fields collects them in a dialog and runs with them once it is accepted.
TEST(ExtensionSettingsSectionTest, SettingsActionCanCollectFieldsFromDialogBeforeRunning)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_dialog_action_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "dialog_action_settings";
    result.manifest->name = "Dialog Action Settings";
    result.settings_schema->schema = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_product")},
                    {QStringLiteral("type"), QStringLiteral("choice")},
                    {QStringLiteral("label"), QStringLiteral("Product")},
                    {QStringLiteral("default"), QStringLiteral("bxt")},
                    {QStringLiteral("visible"), false},
                    {QStringLiteral("options"),
                     QJsonArray{QJsonArray{QStringLiteral("BlurXTerminator"), QStringLiteral("bxt")},
                                QJsonArray{QStringLiteral("StarXTerminator"), QStringLiteral("sxt")}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_email")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Email")},
                    {QStringLiteral("sensitive"), true},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("visible"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_key")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Activation Key")},
                    {QStringLiteral("secret"), true},
                    {QStringLiteral("sensitive"), true},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("visible"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("open_activation_dialog")},
                    {QStringLiteral("type"), QStringLiteral("action")},
                    {QStringLiteral("label"), QStringLiteral("Activation...")},
                    {QStringLiteral("title"), QStringLiteral("Activation")},
                    {QStringLiteral("action_id"), QStringLiteral("activate_selected")},
                    {QStringLiteral("dialog"),
                     QJsonObject{{QStringLiteral("title"), QStringLiteral("RC-Astro Activation")},
                                 {QStringLiteral("accept_label"), QStringLiteral("Activate Selected Product")},
                                 {QStringLiteral("fields"),
                                  QJsonArray{QStringLiteral("activation_product"), QStringLiteral("activation_email"),
                                             QStringLiteral("activation_key")}}}}}};

    ExtensionSettingsSection section(&settings);
    an::ui::extensions::ExtensionSettingsActionRequest captured_request;
    section.setSettingsActionRunner(
        [&captured_request](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
            captured_request = request;
            an::ui::extensions::ExtensionSettingsActionResult action_result;
            action_result.ok = true;
            action_result.tone = "success";
            action_result.message = "Activation complete";
            action_result.settings_updates =
                QCborMap{{QStringLiteral("activation_product"),
                          request.settings_snapshot.value(QStringLiteral("activation_product"))}};
            return action_result;
        });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;

    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_dialog_action_settings"));
    ASSERT_NE(host, nullptr);
    QWidget* action_widget = findSettingsWidgetById(host, QStringLiteral("open_activation_dialog"));
    ASSERT_NE(action_widget, nullptr);
    auto* action_button = action_widget->findChild<QPushButton*>(QStringLiteral("anSectionHeaderActionButton"));
    ASSERT_NE(action_button, nullptr);

    action_button->click();
    QCoreApplication::processEvents();

    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->windowTitle(), QStringLiteral("RC-Astro Activation"));

    auto* combo = findSettingsComboBoxById(dialog, QStringLiteral("activation_product"));
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(1);
    EXPECT_EQ(combo->currentData().toString(), QStringLiteral("sxt"));
    auto* email = findSettingsLineEditById(dialog, QStringLiteral("activation_email"));
    ASSERT_NE(email, nullptr);
    email->setText(QStringLiteral("user@example.test"));
    auto* key = findSettingsLineEditById(dialog, QStringLiteral("activation_key"));
    ASSERT_NE(key, nullptr);
    key->setText(QStringLiteral("SECRET-KEY"));

    auto* button_box = dialog->findChild<QDialogButtonBox*>();
    ASSERT_NE(button_box, nullptr);
    auto* accept_button = button_box->button(QDialogButtonBox::Ok);
    ASSERT_NE(accept_button, nullptr);
    EXPECT_EQ(accept_button->text(), QStringLiteral("Activate Selected Product"));
    accept_button->click();

    for (int attempt = 0; attempt < 100 && section.actionInProgress(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }

    EXPECT_FALSE(section.actionInProgress());
    EXPECT_EQ(QString::fromStdString(captured_request.action_id), QStringLiteral("activate_selected"));
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("activation_product")).toString(),
              QStringLiteral("sxt"));
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("activation_email")).toString(),
              QStringLiteral("user@example.test"));
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("activation_key")).toString(),
              QStringLiteral("SECRET-KEY"));
}

// An action's dialog can stay open after the action and shows the console updates the action sends.
TEST(ExtensionSettingsSectionTest, SettingsActionDialogCanStayOpenAndReceiveConsoleUpdates)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"),
                                   QStringLiteral("extension_settings_dialog_console_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "dialog_console_settings";
    result.manifest->name = "Dialog Console Settings";
    result.settings_schema->schema = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_email")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Email")},
                    {QStringLiteral("sensitive"), true},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("visible"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_key")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Activation Key")},
                    {QStringLiteral("secret"), true},
                    {QStringLiteral("sensitive"), true},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("visible"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_console_output")},
                    {QStringLiteral("type"), QStringLiteral("console_output")},
                    {QStringLiteral("label"), QStringLiteral("Activation Output")},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("visible"), false},
                    {QStringLiteral("min_lines"), 6}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("open_activation_dialog")},
                    {QStringLiteral("type"), QStringLiteral("action")},
                    {QStringLiteral("label"), QStringLiteral("Activation...")},
                    {QStringLiteral("title"), QStringLiteral("Activation")},
                    {QStringLiteral("action_id"), QStringLiteral("activate_selected")},
                    {QStringLiteral("dialog"),
                     QJsonObject{{QStringLiteral("title"), QStringLiteral("RC-Astro Activation")},
                                 {QStringLiteral("accept_label"), QStringLiteral("Activate Selected Product")},
                                 {QStringLiteral("keep_open_on_accept"), true},
                                 {QStringLiteral("fields"),
                                  QJsonArray{QStringLiteral("activation_email"), QStringLiteral("activation_key"),
                                             QStringLiteral("activation_console_output")}}}}}};

    ExtensionSettingsSection section(&settings);
    std::atomic_bool runner_called = false;
    section.setSettingsActionRunner([&runner_called](const an::ui::extensions::ExtensionSettingsActionRequest&) {
        runner_called = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        an::ui::extensions::ExtensionSettingsActionResult action_result;
        action_result.ok = true;
        action_result.tone = "success";
        action_result.message = "Activated SECRET-KEY for user@example.test";
        action_result.transient_updates =
            QCborMap{{QStringLiteral("activation_console_output"),
                      QCborArray{QCborMap{{QStringLiteral("stream"), QStringLiteral("stdout")},
                                          {QStringLiteral("text"), QStringLiteral("activation started")}},
                                 QCborMap{{QStringLiteral("stream"), QStringLiteral("stderr")},
                                          {QStringLiteral("text"), QStringLiteral("SECRET-KEY user@example.test")}}}}};
        return action_result;
    });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;

    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_dialog_console_settings"));
    ASSERT_NE(host, nullptr);
    QWidget* action_widget = findSettingsWidgetById(host, QStringLiteral("open_activation_dialog"));
    ASSERT_NE(action_widget, nullptr);
    auto* action_button = action_widget->findChild<QPushButton*>(QStringLiteral("anSectionHeaderActionButton"));
    ASSERT_NE(action_button, nullptr);

    action_button->click();
    QCoreApplication::processEvents();

    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    ASSERT_NE(dialog, nullptr);
    auto* email = findSettingsLineEditById(dialog, QStringLiteral("activation_email"));
    ASSERT_NE(email, nullptr);
    email->setText(QStringLiteral("user@example.test"));
    auto* key = findSettingsLineEditById(dialog, QStringLiteral("activation_key"));
    ASSERT_NE(key, nullptr);
    key->setText(QStringLiteral("SECRET-KEY"));

    auto* button_box = dialog->findChild<QDialogButtonBox*>();
    ASSERT_NE(button_box, nullptr);
    auto* accept_button = button_box->button(QDialogButtonBox::Ok);
    ASSERT_NE(accept_button, nullptr);
    accept_button->click();

    for (int attempt = 0; attempt < 100 && !runner_called.load(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }
    ASSERT_TRUE(runner_called.load());

    for (int attempt = 0; attempt < 100 && section.actionInProgress(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }

    EXPECT_FALSE(section.actionInProgress());
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_TRUE(button_box->isEnabled());

    auto* console = findSettingsConsoleById(dialog, QStringLiteral("activation_console_output"));
    ASSERT_NE(console, nullptr);
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("activation started")));
    EXPECT_FALSE(console->toPlainText().contains(QStringLiteral("[stdout]")));
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("[stderr]")));
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("[redacted]")));
    EXPECT_FALSE(console->toPlainText().contains(QStringLiteral("SECRET-KEY")));
    EXPECT_FALSE(console->toPlainText().contains(QStringLiteral("user@example.test")));
    EXPECT_TRUE(email->text().isEmpty());
    EXPECT_TRUE(key->text().isEmpty());

    dialog->reject();
    QCoreApplication::processEvents();
}

// An action's console updates are shown while it runs, before it completes.
TEST(ExtensionSettingsSectionTest, SettingsActionStreamsConsoleUpdatesBeforeCompletion)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_stream_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "stream_action_settings";
    result.manifest->name = "Stream Action Settings";
    result.settings_schema->schema = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("action_console")},
                                                            {QStringLiteral("type"), QStringLiteral("console_output")},
                                                            {QStringLiteral("label"), QStringLiteral("Console")},
                                                            {QStringLiteral("persist"), false},
                                                            {QStringLiteral("collapsed_when_empty"), true}},
                                                QJsonObject{{QStringLiteral("id"), QStringLiteral("run_action")},
                                                            {QStringLiteral("type"), QStringLiteral("action")},
                                                            {QStringLiteral("label"), QStringLiteral("Run")}}};

    std::atomic_bool runner_started = false;
    std::atomic_bool allow_finish = false;
    ExtensionSettingsSection section(&settings);
    section.setSettingsActionRunner([&](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
        runner_started = true;
        an::ui::extensions::ExtensionSettingsActionResult update;
        update.transient_updates =
            QCborMap{{QStringLiteral("action_console"),
                      QCborMap{{QStringLiteral("append"),
                                QCborArray{QCborMap{{QStringLiteral("stream"), QStringLiteral("stdout")},
                                                    {QStringLiteral("text"), QStringLiteral("streamed line")}}}}}}};
        if (request.update_callback)
        {
            request.update_callback(update);
        }
        while (!allow_finish.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        an::ui::extensions::ExtensionSettingsActionResult action_result;
        action_result.ok = true;
        action_result.tone = "success";
        action_result.message = "done";
        action_result.transient_updates =
            QCborMap{{QStringLiteral("action_console"),
                      QCborArray{QCborMap{{QStringLiteral("stream"), QStringLiteral("stdout")},
                                          {QStringLiteral("text"), QStringLiteral("streamed line")}},
                                 QCborMap{{QStringLiteral("stream"), QStringLiteral("stderr")},
                                          {QStringLiteral("text"), QStringLiteral("final line")}}}}};
        return action_result;
    });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;
    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_stream_action_settings"));
    ASSERT_NE(host, nullptr);

    QWidget* action_widget = findSettingsWidgetById(host, QStringLiteral("run_action"));
    ASSERT_NE(action_widget, nullptr);
    auto* action_button = action_widget->findChild<QPushButton*>(QStringLiteral("anSectionHeaderActionButton"));
    ASSERT_NE(action_button, nullptr);
    auto* console = findSettingsConsoleById(host, QStringLiteral("action_console"));
    ASSERT_NE(console, nullptr);

    action_button->click();
    bool saw_streamed_line = false;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
        saw_streamed_line = console->toPlainText().contains(QStringLiteral("streamed line"));
        if (runner_started.load() && saw_streamed_line && section.actionInProgress())
        {
            break;
        }
    }

    EXPECT_TRUE(runner_started.load());
    EXPECT_TRUE(section.actionInProgress());
    EXPECT_TRUE(saw_streamed_line);
    allow_finish = true;
    for (int attempt = 0; attempt < 100 && section.actionInProgress(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }

    EXPECT_FALSE(section.actionInProgress());
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("streamed line")));
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("[stderr] final line")));
}

// An action runs off the GUI thread with its button disabled and its progress shown; its result's fields are set and
// stored, the secrets it was given are cleared, and its banner and console messages are redacted.
TEST(ExtensionSettingsSectionTest, SettingsActionsRunAsyncFilterResultsAndRedactMessages)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_action_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "action_settings";
    result.manifest->name = "Action Settings";
    result.settings_schema->schema =
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("public_value")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Public Value")},
                               {QStringLiteral("default"), QStringLiteral("initial")}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_email")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Activation Email")},
                               {QStringLiteral("sensitive"), true},
                               {QStringLiteral("persist"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("activation_key")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Activation Key")},
                               {QStringLiteral("secret"), true},
                               {QStringLiteral("sensitive"), true},
                               {QStringLiteral("persist"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("transient_note")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Transient Note")},
                               {QStringLiteral("persist"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("cli_output")},
                               {QStringLiteral("type"), QStringLiteral("console_output")},
                               {QStringLiteral("label"), QStringLiteral("CLI Output")},
                               {QStringLiteral("persist"), false},
                               {QStringLiteral("min_lines"), 6}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("activate")},
                               {QStringLiteral("type"), QStringLiteral("action")},
                               {QStringLiteral("label"), QStringLiteral("Activate")},
                               {QStringLiteral("timeout_ms"), 5000}}};

    ExtensionSettingsSection section(&settings);
    an::ui::extensions::ExtensionSettingsActionRequest captured_request;
    section.setSettingsActionRunner(
        [&captured_request](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
            captured_request = request;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));

            an::ui::extensions::ExtensionSettingsActionResult action_result;
            action_result.ok = true;
            action_result.tone = "success";
            action_result.message = "Activated SECRET-KEY for user@example.test";
            action_result.settings_updates =
                QCborMap{{QStringLiteral("public_value"), QStringLiteral("updated")},
                         {QStringLiteral("activation_key"), QStringLiteral("leaked")},
                         {QStringLiteral("transient_note"), QStringLiteral("should-not-apply")},
                         {QStringLiteral("unknown"), QStringLiteral("ignored")}};
            action_result.transient_updates =
                QCborMap{{QStringLiteral("transient_note"), QStringLiteral("Ready")},
                         {QStringLiteral("activation_email"), QStringLiteral("leaked")},
                         {QStringLiteral("cli_output"),
                          QCborArray{QCborMap{{QStringLiteral("stream"), QStringLiteral("stderr")},
                                              {QStringLiteral("text"),
                                               QStringLiteral("Activated SECRET-KEY for user@example.test")}}}}};
            return action_result;
        });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;

    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_action_settings"));
    ASSERT_NE(host, nullptr);
    findSettingsLineEditById(host, QStringLiteral("activation_email"))->setText(QStringLiteral("user@example.test"));
    findSettingsLineEditById(host, QStringLiteral("activation_key"))->setText(QStringLiteral("SECRET-KEY"));

    QWidget* action_widget = findSettingsWidgetById(host, QStringLiteral("activate"));
    ASSERT_NE(action_widget, nullptr);
    auto* action_button = action_widget->findChild<QPushButton*>(QStringLiteral("anSectionHeaderActionButton"));
    ASSERT_NE(action_button, nullptr);
    auto* progress = section.findChild<QProgressBar*>(QStringLiteral("anExtensionSettingsActionProgress"));
    ASSERT_NE(progress, nullptr);

    action_button->click();
    QCoreApplication::processEvents();
    EXPECT_TRUE(section.actionInProgress());
    EXPECT_FALSE(action_button->isEnabled());
    EXPECT_TRUE(progress->isVisibleTo(&section));

    for (int attempt = 0; attempt < 100 && section.actionInProgress(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }
    EXPECT_FALSE(section.actionInProgress());

    EXPECT_EQ(QString::fromStdString(captured_request.action_id), QStringLiteral("activate"));
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("activation_key")).toString(),
              QStringLiteral("SECRET-KEY"));
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("activation_email")).toString(),
              QStringLiteral("user@example.test"));

    auto* banner = findInfoBannerByObjectName(section, QStringLiteral("anExtensionSettingsActionStatus"));
    ASSERT_NE(banner, nullptr);
    EXPECT_FALSE(banner->message().contains(QStringLiteral("SECRET-KEY")));
    EXPECT_FALSE(banner->message().contains(QStringLiteral("user@example.test")));
    EXPECT_TRUE(banner->message().contains(QStringLiteral("[redacted]")));

    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("public_value"))->text(), QStringLiteral("updated"));
    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("transient_note"))->text(), QStringLiteral("Ready"));
    auto* console = findSettingsConsoleById(host, QStringLiteral("cli_output"));
    ASSERT_NE(console, nullptr);
    EXPECT_FALSE(console->toPlainText().contains(QStringLiteral("SECRET-KEY")));
    EXPECT_FALSE(console->toPlainText().contains(QStringLiteral("user@example.test")));
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("[redacted]")));
    EXPECT_TRUE(console->toPlainText().contains(QStringLiteral("[stderr]")));
    EXPECT_TRUE(findSettingsLineEditById(host, QStringLiteral("activation_key"))->text().isEmpty());
    EXPECT_TRUE(findSettingsLineEditById(host, QStringLiteral("activation_email"))->text().isEmpty());

    an::ui::extensions::ExtensionSdkService service(&settings);
    QCborValue stored;
    ASSERT_TRUE(service.selectExtensionSetting("action_settings", "public_value", &stored, {}, &error)) << error;
    EXPECT_EQ(stored.toString(), QStringLiteral("updated"));
    ASSERT_TRUE(service.selectExtensionSetting("action_settings", "transient_note", &stored,
                                               QCborValue(QStringLiteral("missing")), &error))
        << error;
    EXPECT_EQ(stored.toString(), QStringLiteral("missing"));
}

// A tool folder is validated and the record it resolves to is handed to the actions.
TEST(ExtensionSettingsSectionTest, ToolConfigurationValidatesFolderAndPassesResolvedRecordToActions)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_tool_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    const QFileInfo executable_info(QCoreApplication::applicationFilePath());
    ASSERT_TRUE(executable_info.isFile());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "tool_settings";
    result.manifest->name = "Tool Settings";
    result.settings_schema->schema =
        QJsonArray{QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("cli_folder")},
                       {QStringLiteral("type"), QStringLiteral("dir_path")},
                       {QStringLiteral("label"), QStringLiteral("CLI Folder")},
                       {QStringLiteral("tool_configuration"),
                        QJsonObject{
                            {QStringLiteral("settings_key"), QStringLiteral("cli_folder")},
                            {QStringLiteral("label"), QStringLiteral("Demo CLI")},
                            {QStringLiteral("not_configured_text"), QStringLiteral("Demo CLI is not configured.")},
                            {QStringLiteral("configured_text"), QStringLiteral("Demo CLI is ready.")},
                            {QStringLiteral("primary_executable"), QStringLiteral("missing-demo-cli")},
                            {QStringLiteral("primary_executable_candidates"),
                             QJsonArray{QStringLiteral("missing-demo-cli"), executable_info.fileName()}},
                            {QStringLiteral("candidate_subdirectories"), QJsonArray{QStringLiteral("bin")}},
                            {QStringLiteral("append_platform_executable_suffix"), false},
                            {QStringLiteral("version_arguments"), QJsonArray{QStringLiteral("--gtest_list_tests")}},
                            {QStringLiteral("version_pattern"), QStringLiteral("(ExtensionSettingsSectionTest\\.)")},
                        }},
                   },
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("resolved_cli_executable")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Executable")},
                               {QStringLiteral("persist"), false},
                               {QStringLiteral("enabled"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("resolved_cli_version")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Version")},
                               {QStringLiteral("persist"), false},
                               {QStringLiteral("enabled"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("refresh_status")},
                               {QStringLiteral("type"), QStringLiteral("action")},
                               {QStringLiteral("label"), QStringLiteral("Refresh Status")}}};

    ExtensionSettingsSection section(&settings);
    an::ui::extensions::ExtensionSettingsActionRequest captured_request;
    section.setSettingsActionRunner(
        [&captured_request](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
            captured_request = request;
            an::ui::extensions::ExtensionSettingsActionResult result;
            result.ok = true;
            result.tone = "success";
            result.message = "ok";
            return result;
        });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;
    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_tool_settings"));
    ASSERT_NE(host, nullptr);

    QWidget* folder_widget = findSettingsWidgetById(host, QStringLiteral("cli_folder"));
    ASSERT_NE(folder_widget, nullptr);
    auto* folder_edit = folder_widget->findChild<QLineEdit*>(QStringLiteral("anExtensionUiPickerLineEdit"));
    ASSERT_NE(folder_edit, nullptr);
    folder_edit->setText(executable_info.absolutePath());
    QCoreApplication::processEvents();

    auto* tool_banner = findInfoBannerByObjectName(section, QStringLiteral("anExtensionSettingsToolStatus"));
    ASSERT_NE(tool_banner, nullptr);
    EXPECT_TRUE(tool_banner->isVisibleTo(&section));
    EXPECT_TRUE(tool_banner->message().contains(QStringLiteral("Demo CLI is ready.")));

    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("resolved_cli_executable"))->text(),
              executable_info.absoluteFilePath());
    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("resolved_cli_version"))->text(),
              QStringLiteral("ExtensionSettingsSectionTest."));

    QWidget* action_widget = findSettingsWidgetById(host, QStringLiteral("refresh_status"));
    ASSERT_NE(action_widget, nullptr);
    auto* action_button = action_widget->findChild<QPushButton*>(QStringLiteral("anSectionHeaderActionButton"));
    ASSERT_NE(action_button, nullptr);
    action_button->click();
    for (int attempt = 0; attempt < 100 && section.actionInProgress(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }

    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("resolved_cli_executable")).toString(),
              executable_info.absoluteFilePath());
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("resolved_cli_version")).toString(),
              QStringLiteral("ExtensionSettingsSectionTest."));
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("resolution_source")).toString(),
              QStringLiteral("settings"));
}

// A tool executable is validated and the record it resolves to is handed to the actions.
TEST(ExtensionSettingsSectionTest, ToolConfigurationValidatesExecutableAndPassesResolvedRecordToActions)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_tool_file_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    const QFileInfo executable_info(QCoreApplication::applicationFilePath());
    ASSERT_TRUE(executable_info.isFile());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "tool_file_settings";
    result.manifest->name = "Tool File Settings";
    result.settings_schema->schema = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cli_executable")},
                    {QStringLiteral("type"), QStringLiteral("file_path")},
                    {QStringLiteral("label"), QStringLiteral("CLI Executable")},
                    {QStringLiteral("button_label"), QStringLiteral("Browse...")},
                    {QStringLiteral("read_only"), true},
                    {QStringLiteral("tool_configuration"),
                     QJsonObject{
                         {QStringLiteral("settings_key"), QStringLiteral("cli_executable")},
                         {QStringLiteral("label"), QStringLiteral("Demo CLI")},
                         {QStringLiteral("not_configured_text"), QStringLiteral("Demo CLI is not configured.")},
                         {QStringLiteral("configured_text"), QStringLiteral("Demo CLI is ready.")},
                         {QStringLiteral("primary_executable"), QStringLiteral("missing-demo-cli")},
                         {QStringLiteral("primary_executable_candidates"),
                          QJsonArray{QStringLiteral("missing-demo-cli"), executable_info.fileName()}},
                         {QStringLiteral("append_platform_executable_suffix"), false},
                         {QStringLiteral("version_arguments"), QJsonArray{QStringLiteral("--gtest_list_tests")}},
                         {QStringLiteral("version_pattern"), QStringLiteral("(ExtensionSettingsSectionTest\\.)")},
                     }}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("resolved_cli_executable")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Executable")},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("enabled"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("resolved_cli_version")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Version")},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("enabled"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("refresh_status")},
                    {QStringLiteral("type"), QStringLiteral("action")},
                    {QStringLiteral("label"), QStringLiteral("Refresh Status")}}};

    ExtensionSettingsSection section(&settings);
    an::ui::extensions::ExtensionSettingsActionRequest captured_request;
    section.setSettingsActionRunner(
        [&captured_request](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
            captured_request = request;
            an::ui::extensions::ExtensionSettingsActionResult result;
            result.ok = true;
            result.tone = "success";
            result.message = "ok";
            return result;
        });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;
    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_tool_file_settings"));
    ASSERT_NE(host, nullptr);

    QWidget* executable_widget = findSettingsWidgetById(host, QStringLiteral("cli_executable"));
    ASSERT_NE(executable_widget, nullptr);
    auto* executable_edit = executable_widget->findChild<QLineEdit*>(QStringLiteral("anExtensionUiPickerLineEdit"));
    ASSERT_NE(executable_edit, nullptr);
    EXPECT_TRUE(executable_edit->isReadOnly());
    executable_edit->setText(executable_info.absoluteFilePath());
    QCoreApplication::processEvents();

    auto* picker_button = executable_widget->findChild<QPushButton*>(QStringLiteral("anExtensionUiPickerButton"));
    ASSERT_NE(picker_button, nullptr);
    EXPECT_EQ(picker_button->text(), QStringLiteral("Browse..."));

    auto* tool_banner = findInfoBannerByObjectName(section, QStringLiteral("anExtensionSettingsToolStatus"));
    ASSERT_NE(tool_banner, nullptr);
    EXPECT_TRUE(tool_banner->isVisibleTo(&section));
    EXPECT_TRUE(tool_banner->message().contains(QStringLiteral("Demo CLI is ready.")));

    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("resolved_cli_executable"))->text(),
              executable_info.absoluteFilePath());
    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("resolved_cli_version"))->text(),
              QStringLiteral("ExtensionSettingsSectionTest."));

    QWidget* action_widget = findSettingsWidgetById(host, QStringLiteral("refresh_status"));
    ASSERT_NE(action_widget, nullptr);
    auto* action_button = action_widget->findChild<QPushButton*>(QStringLiteral("anSectionHeaderActionButton"));
    ASSERT_NE(action_button, nullptr);
    action_button->click();
    for (int attempt = 0; attempt < 100 && section.actionInProgress(); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }

    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("cli_executable")).toString(),
              executable_info.absoluteFilePath());
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("configured_folder")).toString(),
              executable_info.absolutePath());
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("resolved_cli_executable")).toString(),
              executable_info.absoluteFilePath());
    EXPECT_EQ(captured_request.settings_snapshot.value(QStringLiteral("resolution_source")).toString(),
              QStringLiteral("settings"));
}

// An action marked to run on binding runs when the extension is bound, with no banner and no progress shown.
TEST(ExtensionSettingsSectionTest, SettingsActionCanAutorunSilentlyOnBind)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_autorun_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "autorun_settings";
    result.manifest->name = "Autorun Settings";
    result.settings_schema->schema =
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("status")},
                               {QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("label"), QStringLiteral("Status")},
                               {QStringLiteral("default"), QStringLiteral("pending")},
                               {QStringLiteral("persist"), false},
                               {QStringLiteral("enabled"), false}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("refresh_status_on_open")},
                               {QStringLiteral("type"), QStringLiteral("action")},
                               {QStringLiteral("label"), QStringLiteral("Refresh")},
                               {QStringLiteral("action_id"), QStringLiteral("refresh_status")},
                               {QStringLiteral("visible"), false},
                               {QStringLiteral("autorun"), true},
                               {QStringLiteral("show_status"), false}}};

    ExtensionSettingsSection section(&settings);
    QString captured_action_id;
    section.setSettingsActionRunner(
        [&captured_action_id](const an::ui::extensions::ExtensionSettingsActionRequest& request) {
            captured_action_id = QString::fromStdString(request.action_id);
            an::ui::extensions::ExtensionSettingsActionResult result;
            result.ok = true;
            result.tone = "success";
            result.message = "silent";
            result.transient_updates.insert(QStringLiteral("status"), QStringLiteral("ready"));
            return result;
        });

    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;
    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_autorun_settings"));
    ASSERT_NE(host, nullptr);
    auto* status_edit = findSettingsLineEditById(host, QStringLiteral("status"));
    ASSERT_NE(status_edit, nullptr);

    for (int attempt = 0; attempt < 100 && status_edit->text() != QStringLiteral("ready"); ++attempt)
    {
        QTest::qWait(10);
        QCoreApplication::processEvents();
    }

    EXPECT_EQ(captured_action_id, QStringLiteral("refresh_status"));
    EXPECT_EQ(status_edit->text(), QStringLiteral("ready"));

    auto* action_banner = findInfoBannerByObjectName(section, QStringLiteral("anExtensionSettingsActionStatus"));
    ASSERT_NE(action_banner, nullptr);
    EXPECT_FALSE(action_banner->isVisibleTo(&section));
    auto* progress = section.findChild<QProgressBar*>(QStringLiteral("anExtensionSettingsActionProgress"));
    ASSERT_NE(progress, nullptr);
    EXPECT_FALSE(progress->isVisibleTo(&section));
}

// A tool configuration can keep its success banner hidden once the tool is configured.
TEST(ExtensionSettingsSectionTest, ToolConfigurationCanHideConfiguredSuccessBanner)
{
    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_settings_tool_banner_test"));
    ASSERT_TRUE(executor.isOpen());

    GlobalSettingsPersistence settings(executor);
    ASSERT_TRUE(settings.initializeTable());

    const QFileInfo executable_info(QCoreApplication::applicationFilePath());
    ASSERT_TRUE(executable_info.isFile());

    ExtensionLoadResult result = makeSettingsResult();
    result.manifest->id = "quiet_tool_settings";
    result.manifest->name = "Quiet Tool Settings";
    result.settings_schema->schema = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cli_folder")},
                    {QStringLiteral("type"), QStringLiteral("dir_path")},
                    {QStringLiteral("label"), QStringLiteral("CLI Folder")},
                    {QStringLiteral("tool_configuration"),
                     QJsonObject{
                         {QStringLiteral("settings_key"), QStringLiteral("cli_folder")},
                         {QStringLiteral("label"), QStringLiteral("Demo CLI")},
                         {QStringLiteral("not_configured_text"), QStringLiteral("Demo CLI is not configured.")},
                         {QStringLiteral("configured_text"), QStringLiteral("Demo CLI is ready.")},
                         {QStringLiteral("primary_executable"), QStringLiteral("missing-demo-cli")},
                         {QStringLiteral("primary_executable_candidates"),
                          QJsonArray{QStringLiteral("missing-demo-cli"), executable_info.fileName()}},
                         {QStringLiteral("append_platform_executable_suffix"), false},
                         {QStringLiteral("version_arguments"), QJsonArray{QStringLiteral("--gtest_list_tests")}},
                         {QStringLiteral("version_pattern"), QStringLiteral("(ExtensionSettingsSectionTest\\.)")},
                         {QStringLiteral("show_configured_banner"), false},
                     }}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("resolved_cli_executable")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Executable")},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("enabled"), false}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("resolved_cli_version")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("label"), QStringLiteral("Version")},
                    {QStringLiteral("persist"), false},
                    {QStringLiteral("enabled"), false}}};

    ExtensionSettingsSection section(&settings);
    std::string error;
    ASSERT_TRUE(section.bindExtension(result, &error)) << error;
    auto* host = section.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_quiet_tool_settings"));
    ASSERT_NE(host, nullptr);

    QWidget* folder_widget = findSettingsWidgetById(host, QStringLiteral("cli_folder"));
    ASSERT_NE(folder_widget, nullptr);
    auto* folder_edit = folder_widget->findChild<QLineEdit*>(QStringLiteral("anExtensionUiPickerLineEdit"));
    ASSERT_NE(folder_edit, nullptr);
    folder_edit->setText(executable_info.absolutePath());
    QCoreApplication::processEvents();

    auto* tool_banner = findInfoBannerByObjectName(section, QStringLiteral("anExtensionSettingsToolStatus"));
    ASSERT_NE(tool_banner, nullptr);
    EXPECT_FALSE(tool_banner->isVisibleTo(&section));
    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("resolved_cli_executable"))->text(),
              executable_info.absoluteFilePath());
    EXPECT_EQ(findSettingsLineEditById(host, QStringLiteral("resolved_cli_version"))->text(),
              QStringLiteral("ExtensionSettingsSectionTest."));
}
