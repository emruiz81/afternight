// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The widget host that renders an extension's declarative interface: the tree a description builds, the state a patch
/// sets, the path pickers' and status and console widgets' layout and formatting, the theme followed through the design
/// tokens with no style sheet, the parameters a validation snapshot reads, and the widget events a person's interaction
/// sends while a patch's own changes send none.

#include "Test_Widgets/theme_test_utils.h"

#include "components/AnButtonGroup.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "components/AnInputField.h"
#include "components/AnSectionHeader.h"
#include "components/AnStyledTable.h"
#include "controls/ExtensionUiWidgetHost.h"
#include "theme/AnDesignTokens.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTextCursor>
#include <QTextDocument>

#include <gtest/gtest.h>

#include <string>

namespace
{

using an::ui::extensions::ExtensionUiDescription;
using an::ui::extensions::ExtensionUiPatchOperation;
using an::ui::extensions::ExtensionUiPatchState;

ExtensionUiDescription makeWidgetHostDescription()
{
    ExtensionUiDescription description;
    description.extension_id = "an.demo";
    description.session_id = "session-ui-host";
    description.request_id = "request-ui-host-describe";
    QCborArray children;
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                             {QStringLiteral("widget"), QStringLiteral("slider")},
                             {QStringLiteral("label"), QStringLiteral("Strength")},
                             {QStringLiteral("min"), 0.0},
                             {QStringLiteral("max"), 1.0},
                             {QStringLiteral("step"), 0.01},
                             {QStringLiteral("value"), 0.5}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("iterations")},
                             {QStringLiteral("widget"), QStringLiteral("spin_int")},
                             {QStringLiteral("label"), QStringLiteral("Iterations")},
                             {QStringLiteral("min"), 1},
                             {QStringLiteral("max"), 20},
                             {QStringLiteral("value"), 4}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("sigma")},
                             {QStringLiteral("widget"), QStringLiteral("spin_double")},
                             {QStringLiteral("label"), QStringLiteral("Sigma")},
                             {QStringLiteral("min"), 0.1},
                             {QStringLiteral("max"), 5.0},
                             {QStringLiteral("step"), 0.1},
                             {QStringLiteral("value"), 1.5}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("use_mask")},
                             {QStringLiteral("widget"), QStringLiteral("checkbox")},
                             {QStringLiteral("label"), QStringLiteral("Use Mask")},
                             {QStringLiteral("value"), true}});
    children.append(QCborMap{
        {QStringLiteral("id"), QStringLiteral("mode")},
        {QStringLiteral("widget"), QStringLiteral("combo")},
        {QStringLiteral("label"), QStringLiteral("Mode")},
        {QStringLiteral("options"), QCborArray{QCborArray{QStringLiteral("Fast"), QStringLiteral("fast")},
                                               QCborArray{QStringLiteral("Quality"), QStringLiteral("quality")}}},
        {QStringLiteral("value"), QStringLiteral("fast")}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("output_name")},
                             {QStringLiteral("widget"), QStringLiteral("line_edit")},
                             {QStringLiteral("label"), QStringLiteral("Output Name")},
                             {QStringLiteral("placeholder"), QStringLiteral("Processed image")},
                             {QStringLiteral("value"), QStringLiteral("Preview")}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("source_path")},
                             {QStringLiteral("widget"), QStringLiteral("file_picker")},
                             {QStringLiteral("label"), QStringLiteral("Source File")},
                             {QStringLiteral("value"), QStringLiteral("/tmp/input.fit")}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("export_dir")},
                             {QStringLiteral("widget"), QStringLiteral("dir_picker")},
                             {QStringLiteral("label"), QStringLiteral("Export Folder")},
                             {QStringLiteral("value"), QStringLiteral("/tmp/output")}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("summary")},
                             {QStringLiteral("widget"), QStringLiteral("section_header")},
                             {QStringLiteral("label"), QStringLiteral("Summary")},
                             {QStringLiteral("description"), QStringLiteral("Review parameters before execute")},
                             {QStringLiteral("action_text"), QStringLiteral("Reset")}});

    QCborArray advanced_children;
    advanced_children.append(
        QCborMap{{QStringLiteral("id"), QStringLiteral("advanced_note")},
                 {QStringLiteral("widget"), QStringLiteral("info_banner")},
                 {QStringLiteral("message"), QStringLiteral("Advanced controls can increase runtime")},
                 {QStringLiteral("tone"), QStringLiteral("info")}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("advanced")},
                             {QStringLiteral("widget"), QStringLiteral("collapsible_section")},
                             {QStringLiteral("label"), QStringLiteral("Advanced")},
                             {QStringLiteral("expanded"), true},
                             {QStringLiteral("children"), advanced_children}});

    QCborArray settings_card_children;
    settings_card_children.append(QCborMap{
        {QStringLiteral("id"), QStringLiteral("quality_buttons")},
        {QStringLiteral("widget"), QStringLiteral("button_group")},
        {QStringLiteral("options"), QCborArray{QCborArray{QStringLiteral("Fast"), QStringLiteral("fast")},
                                               QCborArray{QStringLiteral("Balanced"), QStringLiteral("balanced")},
                                               QCborArray{QStringLiteral("Best"), QStringLiteral("best")}}},
        {QStringLiteral("value"), QStringLiteral("balanced")}});
    settings_card_children.append(
        QCborMap{{QStringLiteral("id"), QStringLiteral("metrics")},
                 {QStringLiteral("widget"), QStringLiteral("table")},
                 {QStringLiteral("columns"), QCborArray{QStringLiteral("Name"), QStringLiteral("Value")}},
                 {QStringLiteral("rows"),
                  QCborArray{QCborArray{QStringLiteral("Stars"), 120}, QCborArray{QStringLiteral("Score"), 0.97}}}});
    children.append(QCborMap{{QStringLiteral("id"), QStringLiteral("settings_card")},
                             {QStringLiteral("widget"), QStringLiteral("card")},
                             {QStringLiteral("label"), QStringLiteral("Session Settings")},
                             {QStringLiteral("children"), settings_card_children}});

    QCborMap root;
    root.insert(QStringLiteral("widget"), QStringLiteral("column"));
    root.insert(QStringLiteral("children"), children);

    QCborMap tree;
    tree.insert(QStringLiteral("root"), root);
    description.tree = tree;
    description.root_widget = QStringLiteral("column");
    description.widget_count = 14;
    description.widget_ids = {
        QStringLiteral("strength"),        QStringLiteral("iterations"),    QStringLiteral("sigma"),
        QStringLiteral("use_mask"),        QStringLiteral("mode"),          QStringLiteral("output_name"),
        QStringLiteral("source_path"),     QStringLiteral("export_dir"),    QStringLiteral("summary"),
        QStringLiteral("advanced"),        QStringLiteral("advanced_note"), QStringLiteral("settings_card"),
        QStringLiteral("quality_buttons"), QStringLiteral("metrics")};
    return description;
}

ExtensionUiPatchState makeWidgetHostPatchState()
{
    ExtensionUiPatchState patch_state;
    patch_state.extension_id = "an.demo";
    patch_state.session_id = "session-ui-host";
    patch_state.request_id = "request-ui-host-patch";
    patch_state.operations = {
        ExtensionUiPatchOperation{QStringLiteral("strength"), QStringLiteral("value"), QCborValue(0.8)},
        ExtensionUiPatchOperation{QStringLiteral("mode"), QStringLiteral("value"),
                                  QCborValue(QStringLiteral("quality"))},
        ExtensionUiPatchOperation{QStringLiteral("output_name"), QStringLiteral("value"),
                                  QCborValue(QStringLiteral("Final Output"))},
        ExtensionUiPatchOperation{QStringLiteral("source_path"), QStringLiteral("value"),
                                  QCborValue(QStringLiteral("/tmp/updated.fit"))},
        ExtensionUiPatchOperation{QStringLiteral("export_dir"), QStringLiteral("enabled"), QCborValue(false)},
        ExtensionUiPatchOperation{QStringLiteral("summary"), QStringLiteral("description"),
                                  QCborValue(QStringLiteral("Updated after validation"))},
        ExtensionUiPatchOperation{QStringLiteral("advanced"), QStringLiteral("expanded"), QCborValue(false)},
        ExtensionUiPatchOperation{QStringLiteral("advanced_note"), QStringLiteral("tone"),
                                  QCborValue(QStringLiteral("warning"))},
        ExtensionUiPatchOperation{QStringLiteral("advanced_note"), QStringLiteral("message"),
                                  QCborValue(QStringLiteral("Preview disabled in quality mode"))},
        ExtensionUiPatchOperation{QStringLiteral("settings_card"), QStringLiteral("title"),
                                  QCborValue(QStringLiteral("Execution Settings"))},
        ExtensionUiPatchOperation{QStringLiteral("quality_buttons"), QStringLiteral("value"),
                                  QCborValue(QStringLiteral("best"))},
        ExtensionUiPatchOperation{QStringLiteral("metrics"), QStringLiteral("rows"),
                                  QCborArray{QCborArray{QStringLiteral("Stars"), 220},
                                             QCborArray{QStringLiteral("Score"), 0.99},
                                             QCborArray{QStringLiteral("FWHM"), 2.1}}}};
    patch_state.patch_count = static_cast<int>(patch_state.operations.size());
    return patch_state;
}

} // anonymous namespace

// A description builds every widget it names, each the component its type maps to, with its label and value.
TEST(ExtensionUiWidgetHostTest, ApplyDescriptionBuildsSupportedWidgetTree)
{
    an::ui::extensions::ExtensionUiWidgetHost host;
    const ExtensionUiDescription description = makeWidgetHostDescription();

    std::string error;
    ASSERT_TRUE(host.applyDescription(description, &error)) << error;
    ASSERT_TRUE(host.currentDescription().has_value());

    EXPECT_NE(host.widgetById(QStringLiteral("strength")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("iterations")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("sigma")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("use_mask")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("mode")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("output_name")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("source_path")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("export_dir")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("summary")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("advanced")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("advanced_note")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("settings_card")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("quality_buttons")), nullptr);
    EXPECT_NE(host.widgetById(QStringLiteral("metrics")), nullptr);

    auto* use_mask =
        host.widgetById(QStringLiteral("use_mask"))->findChild<QCheckBox*>(QStringLiteral("anExtensionUiCheckBox"));
    ASSERT_TRUE(use_mask);
    EXPECT_TRUE(use_mask->isChecked());

    auto* combo_container = host.widgetById(QStringLiteral("mode"));
    ASSERT_TRUE(combo_container);
    auto* combo_box = combo_container->findChild<QComboBox*>(QStringLiteral("anExtensionUiComboBox"));
    ASSERT_TRUE(combo_box);
    EXPECT_EQ(combo_box->currentData().toString(), QStringLiteral("fast"));

    auto* output_name = dynamic_cast<an::ui::AnInputField*>(host.widgetById(QStringLiteral("output_name")));
    ASSERT_TRUE(output_name);
    EXPECT_EQ(output_name->text(), QStringLiteral("Preview"));
    EXPECT_EQ(output_name->placeholderText(), QStringLiteral("Processed image"));

    auto* summary = dynamic_cast<an::ui::AnSectionHeader*>(host.widgetById(QStringLiteral("summary")));
    ASSERT_TRUE(summary);
    EXPECT_EQ(summary->title(), QStringLiteral("Summary"));
    EXPECT_EQ(summary->description(), QStringLiteral("Review parameters before execute"));
    EXPECT_EQ(summary->actionText(), QStringLiteral("Reset"));

    auto* advanced = dynamic_cast<an::ui::AnCollapsibleSection*>(host.widgetById(QStringLiteral("advanced")));
    ASSERT_TRUE(advanced);
    EXPECT_TRUE(advanced->isExpanded());

    auto* advanced_note = dynamic_cast<an::ui::AnInfoBanner*>(host.widgetById(QStringLiteral("advanced_note")));
    ASSERT_TRUE(advanced_note);
    EXPECT_EQ(advanced_note->message(), QStringLiteral("Advanced controls can increase runtime"));
    EXPECT_EQ(advanced_note->tone(), an::ui::AnInfoBanner::Tone::Info);

    auto* settings_card = dynamic_cast<an::ui::AnCardWidget*>(host.widgetById(QStringLiteral("settings_card")));
    ASSERT_TRUE(settings_card);
    EXPECT_EQ(settings_card->title(), QStringLiteral("Session Settings"));

    auto* quality_buttons = dynamic_cast<an::ui::AnButtonGroup*>(host.widgetById(QStringLiteral("quality_buttons")));
    ASSERT_TRUE(quality_buttons);
    ASSERT_EQ(quality_buttons->buttons().size(), 3);
    EXPECT_EQ(quality_buttons->buttonVariant(quality_buttons->buttons().at(1)),
              an::ui::AnButtonGroup::ButtonVariant::Primary);

    auto* metrics = dynamic_cast<an::ui::AnStyledTable*>(host.widgetById(QStringLiteral("metrics")));
    ASSERT_TRUE(metrics);
    EXPECT_EQ(metrics->columnCount(), 2);
    EXPECT_EQ(metrics->rowCount(), 2);
    ASSERT_TRUE(metrics->item(0, 0));
    EXPECT_EQ(metrics->item(0, 0)->text(), QStringLiteral("Stars"));
}

// A patch sets each property it names on the rendered widget.
TEST(ExtensionUiWidgetHostTest, ApplyPatchStateUpdatesRenderedWidgets)
{
    an::ui::extensions::ExtensionUiWidgetHost host;
    const ExtensionUiDescription description = makeWidgetHostDescription();
    const ExtensionUiPatchState patch_state = makeWidgetHostPatchState();

    std::string error;
    ASSERT_TRUE(host.applyDescription(description, &error)) << error;
    ASSERT_TRUE(host.applyPatchState(patch_state, &error)) << error;
    ASSERT_TRUE(host.currentPatchState().has_value());
    EXPECT_EQ(host.currentPatchState()->patch_count, patch_state.patch_count);

    auto* slider_container = host.widgetById(QStringLiteral("strength"));
    ASSERT_TRUE(slider_container);
    auto* slider_spin = slider_container->findChild<QDoubleSpinBox*>(QStringLiteral("anExtensionUiSliderSpinBox"));
    ASSERT_TRUE(slider_spin);
    EXPECT_DOUBLE_EQ(slider_spin->value(), 0.8);

    auto* combo_box =
        host.widgetById(QStringLiteral("mode"))->findChild<QComboBox*>(QStringLiteral("anExtensionUiComboBox"));
    ASSERT_TRUE(combo_box);
    EXPECT_EQ(combo_box->currentData().toString(), QStringLiteral("quality"));

    auto* output_name = dynamic_cast<an::ui::AnInputField*>(host.widgetById(QStringLiteral("output_name")));
    ASSERT_TRUE(output_name);
    EXPECT_EQ(output_name->text(), QStringLiteral("Final Output"));

    auto* source_path_line = host.widgetById(QStringLiteral("source_path"))
                                 ->findChild<QLineEdit*>(QStringLiteral("anExtensionUiPickerLineEdit"));
    ASSERT_TRUE(source_path_line);
    EXPECT_EQ(source_path_line->text(), QStringLiteral("/tmp/updated.fit"));

    auto* export_dir = host.widgetById(QStringLiteral("export_dir"));
    ASSERT_TRUE(export_dir);
    EXPECT_FALSE(export_dir->isEnabled());

    auto* summary = dynamic_cast<an::ui::AnSectionHeader*>(host.widgetById(QStringLiteral("summary")));
    ASSERT_TRUE(summary);
    EXPECT_EQ(summary->description(), QStringLiteral("Updated after validation"));

    auto* advanced = dynamic_cast<an::ui::AnCollapsibleSection*>(host.widgetById(QStringLiteral("advanced")));
    ASSERT_TRUE(advanced);
    EXPECT_FALSE(advanced->isExpanded());

    auto* advanced_note = dynamic_cast<an::ui::AnInfoBanner*>(host.widgetById(QStringLiteral("advanced_note")));
    ASSERT_TRUE(advanced_note);
    EXPECT_EQ(advanced_note->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_EQ(advanced_note->message(), QStringLiteral("Preview disabled in quality mode"));

    auto* settings_card = dynamic_cast<an::ui::AnCardWidget*>(host.widgetById(QStringLiteral("settings_card")));
    ASSERT_TRUE(settings_card);
    EXPECT_EQ(settings_card->title(), QStringLiteral("Execution Settings"));

    auto* quality_buttons = dynamic_cast<an::ui::AnButtonGroup*>(host.widgetById(QStringLiteral("quality_buttons")));
    ASSERT_TRUE(quality_buttons);
    ASSERT_EQ(quality_buttons->buttons().size(), 3);
    EXPECT_EQ(quality_buttons->buttonVariant(quality_buttons->buttons().at(2)),
              an::ui::AnButtonGroup::ButtonVariant::Primary);

    auto* metrics = dynamic_cast<an::ui::AnStyledTable*>(host.widgetById(QStringLiteral("metrics")));
    ASSERT_TRUE(metrics);
    EXPECT_EQ(metrics->rowCount(), 3);
    ASSERT_TRUE(metrics->item(2, 0));
    EXPECT_EQ(metrics->item(2, 0)->text(), QStringLiteral("FWHM"));
}

// A path picker's label takes the width its text needs and its field takes the rest.
TEST(ExtensionUiWidgetHostTest, PathPickerRowsDoNotReserveAWideLabelGutter)
{
    an::ui::extensions::ExtensionUiWidgetHost host;
    const ExtensionUiDescription description = makeWidgetHostDescription();

    std::string error;
    ASSERT_TRUE(host.applyDescription(description, &error)) << error;

    QWidget* source_path = host.widgetById(QStringLiteral("source_path"));
    ASSERT_NE(source_path, nullptr);
    auto* label = source_path->findChild<QLabel*>(QStringLiteral("anExtensionUiFieldLabel"));
    ASSERT_NE(label, nullptr);
    auto* line_edit = source_path->findChild<QLineEdit*>(QStringLiteral("anExtensionUiPickerLineEdit"));
    ASSERT_NE(line_edit, nullptr);

    const QFontMetrics metrics(label->font());
    const int wide_gutter = metrics.horizontalAdvance(QStringLiteral("M")) * 18;
    EXPECT_LT(label->minimumWidth(), wide_gutter);
    EXPECT_NE(label->width(), wide_gutter);
    EXPECT_EQ(label->sizePolicy().horizontalPolicy(), QSizePolicy::Preferred);
    EXPECT_EQ(line_edit->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
}

// A status widget shows the icon of its tone and takes a patched value.
TEST(ExtensionUiWidgetHostTest, StatusWidgetShowsSemanticIconAndPatchesValue)
{
    an::ui::extensions::ExtensionUiWidgetHost host;

    ExtensionUiDescription description;
    description.extension_id = "an.demo";
    description.session_id = "session-ui-host-status";
    description.request_id = "request-ui-host-status-describe";
    description.root_widget = QStringLiteral("column");
    description.widget_count = 2;
    description.widget_ids = {QStringLiteral("activation_status")};
    description.tree =
        QCborMap{{QStringLiteral("root"),
                  QCborMap{{QStringLiteral("widget"), QStringLiteral("column")},
                           {QStringLiteral("children"),
                            QCborArray{QCborMap{{QStringLiteral("id"), QStringLiteral("activation_status")},
                                                {QStringLiteral("widget"), QStringLiteral("status")},
                                                {QStringLiteral("label"), QStringLiteral("BlurXTerminator")},
                                                {QStringLiteral("value"), QStringLiteral("Activated")}}}}}}};

    std::string error;
    ASSERT_TRUE(host.applyDescription(description, &error)) << error;

    QWidget* status_widget = host.widgetById(QStringLiteral("activation_status"));
    ASSERT_NE(status_widget, nullptr);
    auto* icon_label = status_widget->findChild<QLabel*>(QStringLiteral("anExtensionUiStatusIcon"));
    ASSERT_NE(icon_label, nullptr);
    auto* text_label = status_widget->findChild<QLabel*>(QStringLiteral("anExtensionUiStatusText"));
    ASSERT_NE(text_label, nullptr);
    const QPixmap status_pixmap = icon_label->pixmap();
    EXPECT_FALSE(status_pixmap.isNull());
    EXPECT_EQ(text_label->text(), QStringLiteral("Activated"));
    EXPECT_EQ(status_widget->property("anExtensionUiStatusTone").toString(), QStringLiteral("success"));

    ExtensionUiPatchState patch_state;
    patch_state.extension_id = "an.demo";
    patch_state.session_id = "session-ui-host-status";
    patch_state.request_id = "request-ui-host-status-patch";
    patch_state.operations = {ExtensionUiPatchOperation{QStringLiteral("activation_status"), QStringLiteral("value"),
                                                        QCborValue(QStringLiteral("Not activated"))}};
    patch_state.patch_count = 1;

    ASSERT_TRUE(host.applyPatchState(patch_state, &error)) << error;
    EXPECT_EQ(text_label->text(), QStringLiteral("Not activated"));
    EXPECT_EQ(status_widget->property("anExtensionUiStatusTone").toString(), QStringLiteral("error"));
}

// A console widget writes each stream in its own token colour and takes a patched value.
TEST(ExtensionUiWidgetHostTest, ConsoleOutputWidgetFormatsStreamsAndPatchesValue)
{
    an::ui::extensions::ExtensionUiWidgetHost host;

    ExtensionUiDescription description;
    description.extension_id = "an.console";
    description.session_id = "session-console";
    description.request_id = "request-console-describe";
    description.root_widget = QStringLiteral("column");
    description.widget_count = 2;
    description.widget_ids = {QStringLiteral("cli_output")};
    description.tree =
        QCborMap{{QStringLiteral("root"),
                  QCborMap{{QStringLiteral("widget"), QStringLiteral("column")},
                           {QStringLiteral("children"),
                            QCborArray{QCborMap{{QStringLiteral("id"), QStringLiteral("cli_output")},
                                                {QStringLiteral("widget"), QStringLiteral("console_output")},
                                                {QStringLiteral("label"), QStringLiteral("CLI Output")},
                                                {QStringLiteral("min_lines"), 8},
                                                {QStringLiteral("collapsed_when_empty"), true},
                                                {QStringLiteral("value"), QString()}}}}}}};

    std::string error;
    ASSERT_TRUE(host.applyDescription(description, &error)) << error;

    QWidget* output_widget = host.widgetById(QStringLiteral("cli_output"));
    ASSERT_NE(output_widget, nullptr);
    auto* editor = output_widget->findChild<QPlainTextEdit*>(QStringLiteral("anExtensionUiConsoleOutput"));
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(editor->isReadOnly());
    EXPECT_TRUE(output_widget->isHidden());
    EXPECT_EQ(editor->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    EXPECT_EQ(editor->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    EXPECT_GE(editor->minimumHeight(), QFontMetrics(editor->font()).lineSpacing() * 8);

    ExtensionUiPatchState visible_patch;
    visible_patch.extension_id = "an.console";
    visible_patch.session_id = "session-console";
    visible_patch.request_id = "request-console-visible-patch";
    visible_patch.operations = {
        ExtensionUiPatchOperation{QStringLiteral("cli_output"), QStringLiteral("visible"), QCborValue(true)}};
    visible_patch.patch_count = 1;

    ASSERT_TRUE(host.applyPatchState(visible_patch, &error)) << error;
    EXPECT_TRUE(output_widget->isHidden());

    ExtensionUiPatchState output_patch;
    output_patch.extension_id = "an.console";
    output_patch.session_id = "session-console";
    output_patch.request_id = "request-console-output-patch";
    output_patch.operations = {
        ExtensionUiPatchOperation{QStringLiteral("cli_output"), QStringLiteral("value"),
                                  QCborArray{QCborMap{{QStringLiteral("stream"), QStringLiteral("stdout")},
                                                      {QStringLiteral("text"), QStringLiteral("ready")}},
                                             QCborMap{{QStringLiteral("stream"), QStringLiteral("stderr")},
                                                      {QStringLiteral("text"), QStringLiteral("failed")}}}}};
    output_patch.patch_count = 1;

    ASSERT_TRUE(host.applyPatchState(output_patch, &error)) << error;
    EXPECT_FALSE(output_widget->isHidden());
    EXPECT_TRUE(editor->toPlainText().contains(QStringLiteral("ready")));
    EXPECT_FALSE(editor->toPlainText().contains(QStringLiteral("[stdout]")));
    EXPECT_TRUE(editor->toPlainText().contains(QStringLiteral("[stderr] failed")));

    QTextCursor stderr_cursor = editor->document()->find(QStringLiteral("failed"));
    ASSERT_FALSE(stderr_cursor.isNull());
    EXPECT_EQ(stderr_cursor.charFormat().foreground().color(), an::ui::tokens().colors.statusError);

    ExtensionUiPatchState warning_patch;
    warning_patch.extension_id = "an.console";
    warning_patch.session_id = "session-console";
    warning_patch.request_id = "request-console-warning-patch";
    warning_patch.operations = {
        ExtensionUiPatchOperation{QStringLiteral("cli_output"), QStringLiteral("value"),
                                  QCborArray{QCborMap{{QStringLiteral("stream"), QStringLiteral("warning")},
                                                      {QStringLiteral("text"), QStringLiteral("new warning")}}}}};
    warning_patch.patch_count = 1;

    ASSERT_TRUE(host.applyPatchState(warning_patch, &error)) << error;
    EXPECT_EQ(editor->toPlainText(), QStringLiteral("[warning] new warning"));

    ExtensionUiPatchState clear_patch;
    clear_patch.extension_id = "an.console";
    clear_patch.session_id = "session-console";
    clear_patch.request_id = "request-console-clear-patch";
    clear_patch.operations = {
        ExtensionUiPatchOperation{QStringLiteral("cli_output"), QStringLiteral("value"), QCborValue(QString())}};
    clear_patch.patch_count = 1;

    ASSERT_TRUE(host.applyPatchState(clear_patch, &error)) << error;
    EXPECT_TRUE(output_widget->isHidden());
    EXPECT_TRUE(editor->toPlainText().isEmpty());
}

// A theme switch recolours the rendered widgets from the design tokens, and no widget carries a style sheet.
TEST(ExtensionUiWidgetHostTest, RefreshThemeKeepsRenderedWidgetsStyleSheetFreeAndTokenDriven)
{
    an::tests::widgets::ScopedPaletteOverride palette_override;
    an::ui::extensions::ExtensionUiWidgetHost host;

    std::string error;
    ASSERT_TRUE(host.applyDescription(makeWidgetHostDescription(), &error)) << error;

    auto assertNoStyleSheets = [&host]() {
        EXPECT_TRUE(host.styleSheet().isEmpty());

        const auto widgets = host.findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            ASSERT_NE(widget, nullptr);
            EXPECT_TRUE(widget->styleSheet().isEmpty())
                << widget->metaObject()->className() << " " << widget->objectName().toStdString();
        }
    };

    auto* strength_label =
        host.widgetById(QStringLiteral("strength"))->findChild<QLabel*>(QStringLiteral("anExtensionUiFieldLabel"));
    auto* advanced_note = dynamic_cast<an::ui::AnInfoBanner*>(host.widgetById(QStringLiteral("advanced_note")));
    auto* output_name = dynamic_cast<an::ui::AnInputField*>(host.widgetById(QStringLiteral("output_name")));
    auto* settings_card = dynamic_cast<an::ui::AnCardWidget*>(host.widgetById(QStringLiteral("settings_card")));
    ASSERT_NE(strength_label, nullptr);
    ASSERT_NE(advanced_note, nullptr);
    ASSERT_NE(output_name, nullptr);
    ASSERT_NE(output_name->label(), nullptr);
    ASSERT_NE(settings_card, nullptr);

    palette_override.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    host.refreshTheme();
    assertNoStyleSheets();

    const QColor dark_field_color = strength_label->palette().color(QPalette::WindowText);
    const QColor dark_input_color = output_name->label()->palette().color(QPalette::WindowText);
    const QColor dark_banner_color = advanced_note->messageLabel()->palette().color(QPalette::WindowText);
    const QColor dark_card_color = settings_card->property("anCardBgColor").value<QColor>();

    EXPECT_EQ(dark_field_color, an::ui::tokens().colors.textSecondary);
    EXPECT_EQ(dark_input_color, an::ui::tokens().colors.textSecondary);
    EXPECT_EQ(dark_banner_color, an::ui::tokens().colors.statusInfo);
    EXPECT_TRUE(dark_card_color.isValid());

    palette_override.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    host.refreshTheme();
    assertNoStyleSheets();

    const QColor light_field_color = strength_label->palette().color(QPalette::WindowText);
    const QColor light_input_color = output_name->label()->palette().color(QPalette::WindowText);
    const QColor light_banner_color = advanced_note->messageLabel()->palette().color(QPalette::WindowText);
    const QColor light_card_color = settings_card->property("anCardBgColor").value<QColor>();

    EXPECT_EQ(light_field_color, an::ui::tokens().colors.textSecondary);
    EXPECT_EQ(light_input_color, an::ui::tokens().colors.textSecondary);
    EXPECT_EQ(light_banner_color, an::ui::tokens().colors.statusInfo);
    EXPECT_TRUE(light_card_color.isValid());

    EXPECT_NE(dark_field_color, light_field_color);
    EXPECT_NE(dark_input_color, light_input_color);
    EXPECT_NE(dark_banner_color, light_banner_color);
    EXPECT_NE(dark_card_color, light_card_color);
}

// A validation snapshot reads each input's current value.
TEST(ExtensionUiWidgetHostTest, SnapshotValidationParamsReflectsRenderedInputState)
{
    an::ui::extensions::ExtensionUiWidgetHost host;

    std::string error;
    ASSERT_TRUE(host.applyDescription(makeWidgetHostDescription(), &error)) << error;

    QCborMap params;
    ASSERT_TRUE(host.snapshotValidationParams(&params, &error)) << error;
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("strength")).toDouble(), 0.5);
    EXPECT_EQ(params.value(QStringLiteral("iterations")).toInteger(), 4);
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("sigma")).toDouble(), 1.5);
    EXPECT_TRUE(params.value(QStringLiteral("use_mask")).toBool());
    EXPECT_EQ(params.value(QStringLiteral("mode")).toString(), QStringLiteral("fast"));
    EXPECT_EQ(params.value(QStringLiteral("output_name")).toString(), QStringLiteral("Preview"));
    EXPECT_EQ(params.value(QStringLiteral("source_path")).toString(), QStringLiteral("/tmp/input.fit"));
    EXPECT_EQ(params.value(QStringLiteral("export_dir")).toString(), QStringLiteral("/tmp/output"));
    EXPECT_TRUE(params.value(QStringLiteral("advanced")).toBool());
    EXPECT_EQ(params.value(QStringLiteral("quality_buttons")).toString(), QStringLiteral("balanced"));
    EXPECT_TRUE(params.value(QStringLiteral("summary")).isUndefined());
    EXPECT_TRUE(params.value(QStringLiteral("metrics")).isUndefined());

    ASSERT_TRUE(host.applyPatchState(makeWidgetHostPatchState(), &error)) << error;
    ASSERT_TRUE(host.snapshotValidationParams(&params, &error)) << error;
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("strength")).toDouble(), 0.8);
    EXPECT_EQ(params.value(QStringLiteral("mode")).toString(), QStringLiteral("quality"));
    EXPECT_EQ(params.value(QStringLiteral("output_name")).toString(), QStringLiteral("Final Output"));
    EXPECT_FALSE(params.value(QStringLiteral("advanced")).toBool());
    EXPECT_EQ(params.value(QStringLiteral("quality_buttons")).toString(), QStringLiteral("best"));
}

// A person's change to a widget sends that widget's event with its new value.
TEST(ExtensionUiWidgetHostTest, EmitsWidgetEventForUserInteraction)
{
    an::ui::extensions::ExtensionUiWidgetHost host;

    std::string error;
    ASSERT_TRUE(host.applyDescription(makeWidgetHostDescription(), &error)) << error;

    int event_count = 0;
    QString emitted_widget_id;
    QString emitted_event_name;
    QCborValue emitted_value;
    bool emitted_coalesced = true;
    QObject::connect(&host, &an::ui::extensions::ExtensionUiWidgetHost::widgetEvent, &host,
                     [&event_count, &emitted_widget_id, &emitted_event_name, &emitted_value,
                      &emitted_coalesced](const QString& widget_id, const QString& event_name, const QCborValue& value,
                                          const bool coalesced) {
                         ++event_count;
                         emitted_widget_id = widget_id;
                         emitted_event_name = event_name;
                         emitted_value = value;
                         emitted_coalesced = coalesced;
                     });

    auto* combo_container = host.widgetById(QStringLiteral("mode"));
    ASSERT_NE(combo_container, nullptr);
    auto* combo_box = combo_container->findChild<QComboBox*>(QStringLiteral("anExtensionUiComboBox"));
    ASSERT_NE(combo_box, nullptr);

    combo_box->setCurrentIndex(1);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);

    EXPECT_EQ(event_count, 1);
    EXPECT_EQ(emitted_widget_id, QStringLiteral("mode"));
    EXPECT_EQ(emitted_event_name, QStringLiteral("value_changed"));
    ASSERT_TRUE(emitted_value.isString());
    EXPECT_EQ(emitted_value.toString(), QStringLiteral("quality"));
    EXPECT_FALSE(emitted_coalesced);
}

// A change a patch made sends no widget event.
TEST(ExtensionUiWidgetHostTest, ApplyPatchStateSuppresssPatchOriginatedWidgetEvents)
{
    an::ui::extensions::ExtensionUiWidgetHost host;

    std::string error;
    ASSERT_TRUE(host.applyDescription(makeWidgetHostDescription(), &error)) << error;

    int event_count = 0;
    QObject::connect(&host, &an::ui::extensions::ExtensionUiWidgetHost::widgetEvent, &host,
                     [&event_count](const QString&, const QString&, const QCborValue&, const bool) { ++event_count; });

    ASSERT_TRUE(host.applyPatchState(makeWidgetHostPatchState(), &error)) << error;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);

    EXPECT_EQ(event_count, 0);
}

// A patch for another session is refused.
TEST(ExtensionUiWidgetHostTest, ApplyPatchStateRejectsMismatchedSession)
{
    an::ui::extensions::ExtensionUiWidgetHost host;
    const ExtensionUiDescription description = makeWidgetHostDescription();
    ExtensionUiPatchState patch_state = makeWidgetHostPatchState();
    patch_state.session_id = "session-other";

    std::string error;
    ASSERT_TRUE(host.applyDescription(description, &error)) << error;
    EXPECT_FALSE(host.applyPatchState(patch_state, &error));
    EXPECT_NE(error.find("does not match"), std::string::npos);
}
