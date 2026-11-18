// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ExtensionPanelDescription.h"

#include <QCborArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <utility>

namespace an::ui::extensions
{

namespace
{

using namespace panel_limits;

// ---- The fields every kind of a vocabulary shares.

PanelFieldSpec idField(const char* meaning = "The name the extension knows the control by: a parameter's key, an "
                                             "action's, the target of a patch.")
{
    return {"id", PanelFieldType::Text, "", kMaxShortText, meaning};
}

PanelFieldSpec labelField(const char* absent = "the id", const char* meaning = "The text beside the control.")
{
    return {"label", PanelFieldType::Text, absent, kMaxShortText, meaning};
}

PanelFieldSpec tooltipField()
{
    return {"tooltip", PanelFieldType::Text, "", kMaxLongText, "Shown when the pointer rests on the control."};
}

PanelFieldSpec visibleWhenField()
{
    return {"visible_when", PanelFieldType::Condition, "always", kMaxShortText * 4,
            "Shows the control only while the expression over the parameters holds."};
}

PanelFieldSpec enabledWhenField()
{
    return {"enabled_when", PanelFieldType::Condition, "always", kMaxShortText * 4,
            "Enables the control only while the expression holds, or while a parameter equals (`equals`) or "
            "differs from (`not_equals`) a value."};
}

PanelFieldSpec previewInvalidatesField()
{
    return {"preview_invalidates", PanelFieldType::Boolean, "false", 0,
            "A change marks a preview refreshed by hand as out of date instead of asking for a frame."};
}

PanelFieldSpec trackingField(const char* name, const char* meaning)
{
    return {name, PanelFieldType::Boolean, "true", 0, meaning};
}

PanelFieldSpec visibleField()
{
    return {"visible", PanelFieldType::Boolean, "true", 0, "Whether the widget is shown."};
}

PanelFieldSpec enabledField()
{
    return {"enabled", PanelFieldType::Boolean, "true", 0, "Whether the widget takes input."};
}

/// The fields of a widget of the tree: its id, its tooltip, whether it shows and takes input, and its own.
std::vector<PanelFieldSpec> widgetFields(std::vector<PanelFieldSpec> own)
{
    std::vector<PanelFieldSpec> fields{idField(), tooltipField(), visibleField(), enabledField()};
    fields.insert(fields.end(), own.begin(), own.end());
    return fields;
}

/// The fields of a parameter of the list: its id and label, its tooltip, its own, the preview mark and its
/// conditions.
std::vector<PanelFieldSpec> parameterFields(std::vector<PanelFieldSpec> own)
{
    std::vector<PanelFieldSpec> fields{idField(), labelField(), tooltipField()};
    fields.insert(fields.end(), own.begin(), own.end());
    fields.push_back(previewInvalidatesField());
    fields.push_back(visibleWhenField());
    fields.push_back(enabledWhenField());
    return fields;
}

std::vector<PanelKindSpec> buildKinds()
{
    std::vector<PanelKindSpec> kinds;

    // ---- The process window's list of parameters.

    kinds.push_back({"float",
                     {"double"},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Parameter,
                     false,
                     "A real number: a spin box beside a slider.",
                     parameterFields({
                         {"default", PanelFieldType::Number, "0", 0, "The value the control starts at and resets to."},
                         {"min", PanelFieldType::Number, "-1000000", 0, "The least value."},
                         {"max", PanelFieldType::Number, "1000000", 0, "The greatest value."},
                         {"step", PanelFieldType::Number, "0.01", 0,
                          "The step of the spin box; it also sets how many decimals are shown, from two to six."},
                         trackingField("tracking", "Whether the slider changes the value while it is dragged."),
                         trackingField("slider_tracking", "The older name of `tracking`."),
                     })});
    kinds.push_back({"int",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Parameter,
                     false,
                     "A whole number: a spin box beside a slider. Its numbers are read as whole numbers.",
                     parameterFields({
                         {"default", PanelFieldType::Number, "0", 0, "The value the control starts at and resets to."},
                         {"min", PanelFieldType::Number, "-1000", 0, "The least value."},
                         {"max", PanelFieldType::Number, "1000", 0, "The greatest value."},
                         {"step", PanelFieldType::Number, "1", 0, "The step of the spin box; at least 1."},
                         trackingField("tracking", "Whether the slider changes the value while it is dragged."),
                         trackingField("slider_tracking", "The older name of `tracking`."),
                     })});
    kinds.push_back({"bool",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Parameter,
                     false,
                     "A check box.",
                     parameterFields({
                         {"default", PanelFieldType::Boolean, "false", 0, "Whether it starts checked."},
                     })});
    kinds.push_back({"choice",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Parameter,
                     false,
                     "One value of a list: a combo box. Its value is the chosen entry's value, as text.",
                     parameterFields({
                         {"default", PanelFieldType::Scalar, "the first entry", kMaxShortText,
                          "The value of the entry chosen at the start."},
                         {"options", PanelFieldType::Options, "", kMaxOptions,
                          "The entries, as `[label, value]` pairs; at least one."},
                         {"inline_actions", PanelFieldType::Actions, "", 16,
                          "Buttons beside the combo box, each an action of its own."},
                     })});
    kinds.push_back(
        {"string",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Parameter,
         false,
         "A line of text.",
         parameterFields({
             {"default", PanelFieldType::Text, "", kMaxLongText, "The text it starts with."},
             {"placeholder", PanelFieldType::Text, "the label", kMaxShortText, "Shown while the line is empty."},
         })});
    kinds.push_back(
        {"view_selector",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Parameter,
         false,
         "One of the open images: a combo box of their names, listed by the process that shows the panel. "
         "Its value is the chosen image's name.",
         parameterFields({
             {"default", PanelFieldType::Text, "", kMaxShortText, "The value of the empty entry."},
             {"placeholder", PanelFieldType::Text, "Select image", kMaxShortText, "The text of the empty entry."},
             {"channel_filter", PanelFieldType::Integers, "every count", 8,
              "The channel counts an image may have to be listed."},
             {"match_target_geometry", PanelFieldType::Boolean, "false", 0,
              "Lists only images of the target's width and height."},
             {"match_target_channels", PanelFieldType::Boolean, "false", 0,
              "Lists only images with as many channels as the target."},
             {"exclude_target", PanelFieldType::Boolean, "false", 0, "Leaves the target out."},
         })});
    kinds.push_back({"curve_editor",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Parameter,
                     false,
                     "A tone curve the person drags, over the image's histogram. Its value is its points.",
                     parameterFields({
                         {"default", PanelFieldType::Points, "the straight line", kMaxEntries,
                          "The points it starts with, in [0, 1]."},
                         {"interpolation", PanelFieldType::Text, "akima", kMaxShortText,
                          "How the curve passes through its points: `akima`, `linear`, `cubic` or `steffen`."},
                         {"clipping_info", PanelFieldType::Boolean, "true", 0, "Shows the clipped share at each end."},
                         {"grid", PanelFieldType::Boolean, "true", 0, "Draws the grid."},
                         {"guide", PanelFieldType::Boolean, "true", 0, "Draws the guide line."},
                         {"histogram", PanelFieldType::Boolean, "true", 0, "Draws the histogram behind the curve."},
                         {"histogram_areas", PanelFieldType::Boolean, "false", 0, "Fills the histogram's areas."},
                     })});
    kinds.push_back({"action",
                     {"button"},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Action,
                     false,
                     "A button; the extension hears of a press and may answer with parameters to change.",
                     {
                         idField("The name the extension hears the press by."),
                         labelField(),
                         tooltipField(),
                         {"button_role", PanelFieldType::Text, "secondary", kMaxShortText,
                          "How the button is drawn: `primary`, `secondary` or `danger`."},
                         {"role", PanelFieldType::Text, "", kMaxShortText, "An older name of `button_role`."},
                         {"variant", PanelFieldType::Text, "", kMaxShortText, "An older name of `button_role`."},
                         {"preview_refresh_role", PanelFieldType::Boolean, "false", 0,
                          "The button refreshes a preview the person refreshes by hand."},
                         {"preview_refresh_on_open", PanelFieldType::Boolean, "false", 0,
                          "Such a refresh also runs once as the panel opens."},
                         visibleWhenField(),
                         enabledWhenField(),
                     }});
    kinds.push_back({"section",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Group,
                     true,
                     "A titled group of the controls after it: a card, or a section that folds.",
                     {
                         idField("The section's name, for a patch."),
                         labelField(),
                         {"text", PanelFieldType::Text, "the label", kMaxShortText, "The title."},
                         {"presentation", PanelFieldType::Text, "card", kMaxShortText,
                          "`card`, or `collapsible` for a section that folds; every section folds in the custom "
                          "look."},
                         visibleWhenField(),
                         enabledWhenField(),
                     }});
    kinds.push_back({"tabs",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Group,
                     true,
                     "A set of tabs; each `tab` after it is a page, until `end_tabs`.",
                     {
                         idField("The set's name, for a patch."),
                         visibleWhenField(),
                         enabledWhenField(),
                     }});
    kinds.push_back({"tab",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Group,
                     true,
                     "A page of a set of tabs, holding the controls after it.",
                     {
                         idField("The page's name, for a patch."),
                         labelField(),
                         {"text", PanelFieldType::Text, "the label", kMaxShortText, "The tab's title."},
                     }});
    kinds.push_back(
        {"info",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Display,
         false,
         "A banner of text. One marked as the extension's credits is shown with the process's "
         "description instead.",
         {
             idField("The banner's name; `attribution`, `credits` or `credit` marks the credits."),
             labelField("the id", "The text, when `text` is absent."),
             {"text", PanelFieldType::Text, "the label", kMaxLongText, "The banner's text."},
             {"tone", PanelFieldType::Text, "info", kMaxShortText,
              "`info`, `success`, `warning` or `error`; `warning` when absent in the custom look."},
             {"role", PanelFieldType::Text, "", kMaxShortText, "`attribution` or `credits` marks the credits."},
             {"purpose", PanelFieldType::Text, "", kMaxShortText, "`attribution` or `credits` marks the credits."},
             {"preview_status", PanelFieldType::Boolean, "false", 0,
              "The banner says whether a preview refreshed by hand is current."},
             {"stale_text", PanelFieldType::Text, "Preview update needed.", kMaxShortText,
              "Its text while the preview is out of date."},
             {"ready_text", PanelFieldType::Text, "Preview is up to date.", kMaxShortText,
              "Its text while the preview is current."},
             {"updating_text", PanelFieldType::Text, "Updating preview...", kMaxShortText,
              "Its text while the preview is refreshed."},
         }});
    kinds.push_back(
        {"percentage_mix_label",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Display,
         false,
         "A readout of a blend: `75% Ha, 25% OIII` for a parameter at 0.25.",
         {
             idField("The readout's name."),
             labelField(),
             {"text", PanelFieldType::Text, "the label", kMaxShortText, "Its text until the parameter is read."},
             tooltipField(),
             {"source", PanelFieldType::Text, "", kMaxShortText,
              "The parameter read, from 0 (all left) to 1 (all right)."},
             {"left_label", PanelFieldType::Text, "Ha", kMaxShortText, "The name of the left share."},
             {"right_label", PanelFieldType::Text, "OIII", kMaxShortText, "The name of the right share."},
             visibleWhenField(),
             enabledWhenField(),
         }});
    kinds.push_back(
        {"value_description_label",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Display,
         false,
         "A readout of a parameter's value, with the words of the range it falls in.",
         {
             idField("The readout's name."),
             labelField(),
             {"text", PanelFieldType::Text, "the label", kMaxShortText, "Its text until the parameter is read."},
             tooltipField(),
             {"source", PanelFieldType::Text, "", kMaxShortText, "The parameter read."},
             {"decimals", PanelFieldType::Number, "2", 0, "The decimals shown, from 0 to 6."},
             {"prefix", PanelFieldType::Text, "", kMaxShortText, "Written before the value."},
             {"suffix", PanelFieldType::Text, "", kMaxShortText, "Written after the value."},
             {"separator", PanelFieldType::Text, " - ", kMaxShortText,
              "Written between the value and the range's words."},
             {"ranges", PanelFieldType::Ranges, "", 256, "`[least, most, words]` for each range."},
             visibleWhenField(),
             enabledWhenField(),
         }});
    kinds.push_back({"rt_histogram",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Display,
                     false,
                     "A histogram with its toolbar. The first is the preview's, made with each frame; any other is "
                     "counted from what is shown.",
                     {
                         idField("The histogram's name."),
                         tooltipField(),
                         {"height", PanelFieldType::Number, "220", 0, "Its least height, at least 120."},
                         {"clipping_info", PanelFieldType::Boolean, "true", 0, "Shows the clipped share at each end."},
                         {"grid", PanelFieldType::Boolean, "true", 0, "Draws the grid."},
                         {"local_channel_normalization", PanelFieldType::Boolean, "false", 0,
                          "Scales each channel to its own peak."},
                         {"histogram_areas", PanelFieldType::Boolean, "false", 0, "Fills the histogram's areas."},
                         {"show_clipping_info_control", PanelFieldType::Boolean, "true", 0,
                          "Puts the clipping button in the toolbar."},
                         visibleWhenField(),
                         enabledWhenField(),
                     }});
    kinds.push_back(
        {"vector_slope",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Display,
         false,
         "A graph of hue vectors drawn from pairs of parameters. It sits in the page, beside the "
         "sections.",
         {
             idField("The graph's name."),
             {"label", PanelFieldType::Text, "the id", kMaxShortText, "The title above the graph; none when empty."},
             tooltipField(),
             {"height", PanelFieldType::Number, "220", 0, "The graph's height."},
             {"vectors", PanelFieldType::Vectors, "the hue wheel's six", 64,
              "The vectors: each its key, its hue and saturation parameters, its base hue and colour."},
             visibleWhenField(),
             enabledWhenField(),
         }});
    kinds.push_back({"target_selector",
                     {},
                     PanelVocabulary::Parameters,
                     PanelKindRole::Hosted,
                     false,
                     "Where the process that shows the panel lets the person choose the image the process runs on.",
                     {
                         idField("The selector's name."),
                         {"label", PanelFieldType::Text, "the id", kMaxShortText, "The text beside it."},
                         tooltipField(),
                         visibleWhenField(),
                         enabledWhenField(),
                     }});
    kinds.push_back(
        {"manual_mask_editor",
         {},
         PanelVocabulary::Parameters,
         PanelKindRole::Hosted,
         false,
         "A mask the person paints over the image, drawn by the process that shows the panel. Its use is "
         "a parameter of its own.",
         {
             {"id", PanelFieldType::Text, "manual_mask", kMaxShortText, "The editor's name."},
             {"label", PanelFieldType::Text, "Manual Mask", kMaxShortText, "The editor's title."},
             tooltipField(),
             {"use_param", PanelFieldType::Text, "use_manual_mask", kMaxShortText,
              "The parameter that says whether the mask is used."},
             {"use_label", PanelFieldType::Text, "Use manual mask", kMaxShortText, "The text beside its check box."},
             {"use_default", PanelFieldType::Boolean, "false", 0, "Whether the mask is used at the start."},
             {"display_param", PanelFieldType::Text, "", kMaxShortText,
              "A choice whose entry shows the mask while it is chosen."},
             {"display_value", PanelFieldType::Scalar, "", kMaxShortText, "That entry's value."},
             {"brush_size", PanelFieldType::Number, "50", 0, "The brush's size, from 1 to 512."},
             {"min_brush_size", PanelFieldType::Number, "10", 0, "The least brush size."},
             {"max_brush_size", PanelFieldType::Number, "200", 0, "The greatest brush size."},
             previewInvalidatesField(),
             visibleWhenField(),
             enabledWhenField(),
         }});

    // ---- The tree of widgets.

    kinds.push_back({"column",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Group,
                     true,
                     "Its children, one under another.",
                     widgetFields({})});
    kinds.push_back({"card",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Group,
                     true,
                     "A titled card holding its children.",
                     widgetFields({
                         labelField("", "The title."),
                         {"compact", PanelFieldType::Boolean, "false", 0, "Draws the card's tighter header."},
                         {"body_padded", PanelFieldType::Boolean, "false", 0, "Pads the card's body."},
                     })});
    kinds.push_back({"collapsible_section",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Group,
                     true,
                     "A titled section holding its children, which folds.",
                     widgetFields({
                         labelField("", "The title."),
                         {"expanded", PanelFieldType::Boolean, "true", 0, "Whether it starts unfolded."},
                     })});
    kinds.push_back({"slider",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "A real number: a slider beside a spin box, under its label.",
                     widgetFields({
                         labelField("", "The text above the control."),
                         {"min", PanelFieldType::Number, "0", 0, "The least value."},
                         {"max", PanelFieldType::Number, "1", 0, "The greatest value."},
                         {"step", PanelFieldType::Number, "0.01", 0, "The step."},
                         {"value", PanelFieldType::Number, "the least", 0, "The value."},
                     })});
    kinds.push_back({"spin_int",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "A whole number: a spin box beside its label. Its numbers are read as whole numbers.",
                     widgetFields({
                         labelField("", "The text beside the control."),
                         {"min", PanelFieldType::Number, "0", 0, "The least value."},
                         {"max", PanelFieldType::Number, "100", 0, "The greatest value."},
                         {"step", PanelFieldType::Number, "1", 0, "The step; at least 1."},
                         {"value", PanelFieldType::Number, "the least", 0, "The value."},
                     })});
    kinds.push_back({"spin_double",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "A real number: a spin box beside its label.",
                     widgetFields({
                         labelField("", "The text beside the control."),
                         {"min", PanelFieldType::Number, "0", 0, "The least value."},
                         {"max", PanelFieldType::Number, "1", 0, "The greatest value."},
                         {"step", PanelFieldType::Number, "0.01", 0, "The step; it also sets the decimals shown."},
                         {"value", PanelFieldType::Number, "the least", 0, "The value."},
                     })});
    kinds.push_back({"checkbox",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "A check box beside its label.",
                     widgetFields({
                         labelField("", "The text beside the control."),
                         {"value", PanelFieldType::Boolean, "false", 0, "Whether it is checked."},
                     })});
    kinds.push_back({"combo",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "One value of a list: a combo box beside its label.",
                     widgetFields({
                         labelField("", "The text beside the control."),
                         {"options", PanelFieldType::Options, "", kMaxOptions,
                          "The entries: texts, or `[label, value]` pairs; at least one."},
                         {"value", PanelFieldType::Scalar, "none chosen", kMaxShortText, "The chosen entry's value."},
                     })});
    kinds.push_back(
        {"line_edit",
         {},
         PanelVocabulary::Widgets,
         PanelKindRole::Parameter,
         false,
         "A line of text under its label.",
         widgetFields({
             labelField("", "The text above the control."),
             {"placeholder", PanelFieldType::Text, "", kMaxShortText, "Shown while the line is empty."},
             {"value", PanelFieldType::Scalar, "", kMaxLongText, "The text."},
             {"echo_mode", PanelFieldType::Text, "normal", kMaxShortText, "`password` hides what is typed."},
         })});
    kinds.push_back({"file_picker",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "A file's path: a line of text and a button that opens a file dialog.",
                     widgetFields({
                         labelField("", "The text beside the control."),
                         {"placeholder", PanelFieldType::Text, "", kMaxShortText, "Shown while the line is empty."},
                         {"value", PanelFieldType::Scalar, "", kMaxLongText, "The path."},
                         {"read_only", PanelFieldType::Boolean, "false", 0, "Only the dialog sets the path."},
                         {"button_label", PanelFieldType::Text, "Select File", kMaxShortText, "The button's text."},
                     })});
    kinds.push_back({"dir_picker",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "A folder's path: a line of text and a button that opens a folder dialog.",
                     widgetFields({
                         labelField("", "The text beside the control."),
                         {"placeholder", PanelFieldType::Text, "", kMaxShortText, "Shown while the line is empty."},
                         {"value", PanelFieldType::Scalar, "", kMaxLongText, "The path."},
                         {"read_only", PanelFieldType::Boolean, "false", 0, "Only the dialog sets the path."},
                         {"button_label", PanelFieldType::Text, "Select Folder", kMaxShortText, "The button's text."},
                     })});
    kinds.push_back(
        {"status",
         {},
         PanelVocabulary::Widgets,
         PanelKindRole::Display,
         false,
         "A line of status text with a tone's mark, beside its label.",
         widgetFields({
             labelField("", "The text beside the status."),
             {"value", PanelFieldType::Scalar, "", kMaxLongText, "The status text."},
             {"tone", PanelFieldType::Text, "neutral", kMaxShortText, "`success`, `warning`, `error` or `info`."},
         })});
    kinds.push_back({"console_output",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Display,
                     false,
                     "Read-only console text, under its label.",
                     widgetFields({
                         labelField("", "The text above the console."),
                         {"placeholder", PanelFieldType::Text, "", kMaxShortText, "Shown while it is empty."},
                         {"value", PanelFieldType::Lines, "", kMaxConsoleText,
                          "The text, or its lines, each a text or a map of `text` and `stream`."},
                         {"min_lines", PanelFieldType::Number, "12", 0, "Its least height in lines, at least 4."},
                         {"collapsed_when_empty", PanelFieldType::Boolean, "false", 0, "Hidden while it is empty."},
                     })});
    kinds.push_back(
        {"section_header",
         {},
         PanelVocabulary::Widgets,
         PanelKindRole::Action,
         false,
         "A heading with a description and, when it has a text, an action button.",
         widgetFields({
             labelField("", "The heading."),
             {"description", PanelFieldType::Text, "", kMaxLongText, "The line under the heading."},
             {"action_text", PanelFieldType::Text, "", kMaxShortText, "The button's text; no button when empty."},
         })});
    kinds.push_back({"info_banner",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Display,
                     false,
                     "A banner of text.",
                     widgetFields({
                         {"message", PanelFieldType::Text, "the label", kMaxLongText, "The banner's text."},
                         labelField("", "The text, when `message` is absent."),
                         {"tone", PanelFieldType::Text, "info", kMaxShortText, "`info`, `warning` or `error`."},
                     })});
    kinds.push_back({"button_group",
                     {},
                     PanelVocabulary::Widgets,
                     PanelKindRole::Parameter,
                     false,
                     "One value of a few: a row of buttons, the chosen one drawn as primary.",
                     widgetFields({
                         {"options", PanelFieldType::Options, "", kMaxOptions,
                          "The entries: texts, or `[label, value]` pairs; at least one."},
                         {"value", PanelFieldType::Scalar, "none chosen", kMaxShortText, "The chosen entry's value."},
                     })});
    kinds.push_back(
        {"table",
         {},
         PanelVocabulary::Widgets,
         PanelKindRole::Display,
         false,
         "A table of rows.",
         widgetFields({
             {"columns", PanelFieldType::Columns, "", 64, "The columns: texts, or maps of `id` and `label`."},
             {"rows", PanelFieldType::Rows, "", kMaxEntries,
              "The rows: lists of values in the columns' order, or maps of a column's id to its value."},
         })});

    return kinds;
}

// ---- Values.

[[nodiscard]] std::string quoted(const QString& text)
{
    return "'" + text.toStdString() + "'";
}

/// The node as an error names it: its kind and, when it has one, its id.
[[nodiscard]] std::string nodeName(const PanelNode& node)
{
    const QString id = node.id();
    return id.isEmpty() ? node.kind.toStdString() : node.kind.toStdString() + " " + quoted(id);
}

[[nodiscard]] std::optional<double> numberOf(const QCborValue& value)
{
    if (value.isInteger())
    {
        return static_cast<double>(value.toInteger());
    }
    if (value.isDouble())
    {
        return value.toDouble();
    }
    return std::nullopt;
}

/// Why a text may not be shown, or empty: too long, or holding a control character but a tab or a line break.
[[nodiscard]] std::string textProblem(const QString& text, const int limit, const bool anyCharacter = false)
{
    if (text.size() > limit)
    {
        return "is " + std::to_string(text.size()) + " characters long, past its bound of " + std::to_string(limit);
    }
    for (const QChar character : text)
    {
        const char16_t code = character.unicode();
        if (code == 0 || (!anyCharacter && code < 0x20 && code != u'\t' && code != u'\n' && code != u'\r') ||
            (!anyCharacter && code == 0x7f))
        {
            return "holds a control character";
        }
    }
    return {};
}

[[nodiscard]] bool isScalar(const QCborValue& value)
{
    if (value.isBool() || value.isInteger() || value.isString())
    {
        return true;
    }
    return value.isDouble() && std::isfinite(value.toDouble());
}

/// The checks of one field's value against its spec. Each returns why the value is refused, or empty.
class FieldCheck
{
public:
    FieldCheck(const PanelFieldSpec& spec, QCborValue value) : m_spec(spec), m_value(std::move(value)) {}

    [[nodiscard]] std::string problem() const
    {
        switch (m_spec.type)
        {
        case PanelFieldType::Text:
            return text(m_value, limit(kMaxShortText));
        case PanelFieldType::Number:
            return number(m_value);
        case PanelFieldType::Integer:
            return integer(m_value);
        case PanelFieldType::Boolean:
            return m_value.isBool() ? std::string() : "is not true or false";
        case PanelFieldType::Scalar:
            return scalar(m_value, limit(kMaxShortText));
        case PanelFieldType::Condition:
            return condition();
        case PanelFieldType::Options:
            return options();
        case PanelFieldType::Integers:
            return integers();
        case PanelFieldType::Points:
            return points();
        case PanelFieldType::Ranges:
            return ranges();
        case PanelFieldType::Actions:
            return listOfMaps([](const QCborMap& action) { return actionProblem(action); });
        case PanelFieldType::Vectors:
            return listOfMaps([](const QCborMap& vector) { return vectorProblem(vector); });
        case PanelFieldType::Columns:
            return columns();
        case PanelFieldType::Rows:
            return rows();
        case PanelFieldType::Lines:
            return lines();
        }
        return "has a type the panel does not know";
    }

private:
    [[nodiscard]] int limit(const int fallback) const
    {
        return m_spec.limit > 0 ? m_spec.limit : fallback;
    }

    [[nodiscard]] static std::string text(const QCborValue& value, const int bound)
    {
        if (!value.isString())
        {
            return "is not a text";
        }
        return textProblem(value.toString(), bound);
    }

    [[nodiscard]] static std::string number(const QCborValue& value)
    {
        const std::optional<double> number = numberOf(value);
        if (!number.has_value())
        {
            return "is not a number";
        }
        return std::isfinite(*number) ? std::string() : "is not a finite number";
    }

    [[nodiscard]] static std::string integer(const QCborValue& value)
    {
        const std::optional<double> number = numberOf(value);
        if (!number.has_value() || !std::isfinite(*number))
        {
            return "is not a whole number";
        }
        if (std::floor(*number) != *number || *number < static_cast<double>(INT_MIN) ||
            *number > static_cast<double>(INT_MAX))
        {
            return "is not a whole number an int holds";
        }
        return {};
    }

    [[nodiscard]] static std::string scalar(const QCborValue& value, const int bound)
    {
        if (!isScalar(value))
        {
            return "is not a boolean, a finite number or a text";
        }
        return value.isString() ? textProblem(value.toString(), bound) : std::string();
    }

    [[nodiscard]] std::string condition() const
    {
        if (m_value.isString())
        {
            return text(m_value, limit(kMaxShortText));
        }
        if (!m_value.isMap())
        {
            return "is neither an expression nor a map of `param` and `equals` or `not_equals`";
        }
        const QCborMap map = m_value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            const QString key = it.key().toString();
            if (key == QLatin1String("param"))
            {
                if (const std::string problem = text(it.value(), kMaxShortText); !problem.empty())
                {
                    return "names a parameter that " + problem;
                }
            }
            else if (key == QLatin1String("equals") || key == QLatin1String("not_equals"))
            {
                if (const std::string problem = scalar(it.value(), kMaxShortText); !problem.empty())
                {
                    return "compares with a value that " + problem;
                }
            }
            else
            {
                return "states " + quoted(key) + ", which a condition has not";
            }
        }
        return {};
    }

    [[nodiscard]] std::string list(qsizetype* size) const
    {
        if (!m_value.isArray())
        {
            return "is not a list";
        }
        *size = m_value.toArray().size();
        if (*size > limit(kMaxEntries))
        {
            return "has " + std::to_string(*size) + " entries, past its bound of " + std::to_string(limit(kMaxEntries));
        }
        return {};
    }

    [[nodiscard]] std::string options() const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        if (size == 0)
        {
            return "has no entries: a choice needs at least one";
        }
        for (const QCborValue& entry : m_value.toArray())
        {
            if (entry.isString())
            {
                if (std::string problem = textProblem(entry.toString(), kMaxShortText); !problem.empty())
                {
                    return "has an entry that " + problem;
                }
                continue;
            }
            if (!entry.isArray() || entry.toArray().size() < 2)
            {
                return "has an entry that is neither a text nor a [label, value] pair";
            }
            const QCborArray pair = entry.toArray();
            if (std::string problem = text(pair.at(0), kMaxShortText); !problem.empty())
            {
                return "has an entry whose label " + problem;
            }
            if (std::string problem = scalar(pair.at(1), kMaxShortText); !problem.empty())
            {
                return "has an entry whose value " + problem;
            }
        }
        return {};
    }

    [[nodiscard]] std::string integers() const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        for (const QCborValue& entry : m_value.toArray())
        {
            if (std::string problem = integer(entry); !problem.empty())
            {
                return "has an entry that " + problem;
            }
        }
        return {};
    }

    [[nodiscard]] static bool coordinate(const QCborValue& value)
    {
        if (value.isString())
        {
            return value.toString().size() <= kMaxShortText;
        }
        const std::optional<double> number = numberOf(value);
        return number.has_value() && std::isfinite(*number);
    }

    [[nodiscard]] std::string points() const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        for (const QCborValue& point : m_value.toArray())
        {
            if (point.isArray())
            {
                const QCborArray pair = point.toArray();
                if (pair.size() < 2 || !coordinate(pair.at(0)) || !coordinate(pair.at(1)))
                {
                    return "has a point that is not an [x, y] pair of finite numbers";
                }
                continue;
            }
            if (point.isMap())
            {
                const QCborMap map = point.toMap();
                for (auto it = map.begin(); it != map.end(); ++it)
                {
                    if (!coordinate(it.value()))
                    {
                        return "has a point whose " + quoted(it.key().toString()) + " is not a finite number";
                    }
                }
                continue;
            }
            return "has a point that is neither a pair nor a map";
        }
        return {};
    }

    [[nodiscard]] std::string ranges() const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        for (const QCborValue& entry : m_value.toArray())
        {
            if (!entry.isArray())
            {
                return "has a range that is not a [least, most, words] list";
            }
            const QCborArray range = entry.toArray();
            if (range.size() < 3)
            {
                continue;
            }
            const std::optional<double> least = numberOf(range.at(0));
            const std::optional<double> most = numberOf(range.at(1));
            if (!least.has_value() || !most.has_value() || !std::isfinite(*least) || !std::isfinite(*most))
            {
                return "has a range whose ends are not finite numbers";
            }
            if (*least > *most)
            {
                return "has a range that is not one: its least is above its most";
            }
            if (std::string problem = text(range.at(2), kMaxShortText); !problem.empty())
            {
                return "has a range whose words " + problem;
            }
        }
        return {};
    }

    template <typename Check> [[nodiscard]] std::string listOfMaps(Check check) const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        for (const QCborValue& entry : m_value.toArray())
        {
            if (!entry.isMap())
            {
                return "has an entry that is not a map";
            }
            if (std::string problem = check(entry.toMap()); !problem.empty())
            {
                return "has an entry that " + problem;
            }
        }
        return {};
    }

    /// An inline action: its texts and marks.
    [[nodiscard]] static std::string actionProblem(const QCborMap& action)
    {
        for (auto it = action.begin(); it != action.end(); ++it)
        {
            const QString key = it.key().toString();
            const bool mark =
                key == QLatin1String("preview_refresh_role") || key == QLatin1String("preview_refresh_on_open");
            if (mark)
            {
                if (!it.value().isBool())
                {
                    return "marks " + quoted(key) + " with something that is not true or false";
                }
                continue;
            }
            const int bound = key == QLatin1String("tooltip") ? kMaxLongText : kMaxShortText;
            if (std::string problem = text(it.value(), bound); !problem.empty())
            {
                return "states " + quoted(key) + ", which " + problem;
            }
        }
        return {};
    }

    /// A vector of a slope graph: its names, its numbers and its colour.
    [[nodiscard]] static std::string vectorProblem(const QCborMap& vector)
    {
        for (auto it = vector.begin(); it != vector.end(); ++it)
        {
            const QString key = it.key().toString();
            if (key == QLatin1String("base_hue_degrees") || key == QLatin1String("base_hue") ||
                key == QLatin1String("saturation_scale"))
            {
                if (std::string problem = number(it.value()); !problem.empty())
                {
                    return "states " + quoted(key) + ", which " + problem;
                }
                continue;
            }
            if (key == QLatin1String("color") && it.value().isArray())
            {
                const QCborArray channels = it.value().toArray();
                if (channels.size() > 4)
                {
                    return "states a colour of more than four channels";
                }
                for (const QCborValue& channel : channels)
                {
                    if (!number(channel).empty())
                    {
                        return "states a colour whose channels are not numbers";
                    }
                }
                continue;
            }
            if (std::string problem = text(it.value(), kMaxShortText); !problem.empty())
            {
                return "states " + quoted(key) + ", which " + problem;
            }
        }
        return {};
    }

    [[nodiscard]] std::string columns() const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        for (const QCborValue& column : m_value.toArray())
        {
            if (column.isString())
            {
                if (std::string problem = textProblem(column.toString(), kMaxShortText); !problem.empty())
                {
                    return "has a column that " + problem;
                }
                continue;
            }
            if (!column.isMap())
            {
                return "has a column that is neither a text nor a map";
            }
            const QCborMap map = column.toMap();
            for (auto it = map.begin(); it != map.end(); ++it)
            {
                if (std::string problem = text(it.value(), kMaxShortText); !problem.empty())
                {
                    return "has a column whose " + quoted(it.key().toString()) + " " + problem;
                }
            }
        }
        return {};
    }

    [[nodiscard]] std::string rows() const
    {
        qsizetype size = 0;
        if (std::string problem = list(&size); !problem.empty())
        {
            return problem;
        }
        const auto cell = [](const QCborValue& value) -> std::string {
            if (value.isNull() || value.isUndefined())
            {
                return {};
            }
            return scalar(value, kMaxLongText);
        };
        for (const QCborValue& row : m_value.toArray())
        {
            if (row.isArray())
            {
                const QCborArray cells = row.toArray();
                if (cells.size() > 64)
                {
                    return "has a row of more than 64 cells";
                }
                for (const QCborValue& value : cells)
                {
                    if (std::string problem = cell(value); !problem.empty())
                    {
                        return "has a cell that " + problem;
                    }
                }
                continue;
            }
            if (row.isMap())
            {
                const QCborMap cells = row.toMap();
                if (cells.size() > 64)
                {
                    return "has a row of more than 64 cells";
                }
                for (auto it = cells.begin(); it != cells.end(); ++it)
                {
                    if (std::string problem = cell(it.value()); !problem.empty())
                    {
                        return "has a cell that " + problem;
                    }
                }
                continue;
            }
            return "has a row that is neither a list nor a map";
        }
        return {};
    }

    [[nodiscard]] std::string lines() const
    {
        const int bound = limit(kMaxConsoleText);
        if (m_value.isString())
        {
            return textProblem(m_value.toString(), bound, true);
        }
        if (!m_value.isArray())
        {
            return "is neither a text nor a list of lines";
        }
        qsizetype characters = 0;
        const QCborArray entries = m_value.toArray();
        if (entries.size() > kMaxConsoleText)
        {
            return "has more lines than its bound of " + std::to_string(kMaxConsoleText);
        }
        for (const QCborValue& entry : entries)
        {
            QString line;
            if (entry.isString())
            {
                line = entry.toString();
            }
            else if (entry.isMap())
            {
                const QCborMap map = entry.toMap();
                for (auto it = map.begin(); it != map.end(); ++it)
                {
                    if (!it.value().isString())
                    {
                        return "has a line whose " + quoted(it.key().toString()) + " is not a text";
                    }
                    line += it.value().toString();
                }
            }
            else
            {
                return "has a line that is neither a text nor a map";
            }
            characters += line.size();
            if (std::string problem = textProblem(line, bound, true); !problem.empty())
            {
                return "has a line that " + problem;
            }
        }
        if (characters > bound)
        {
            return "holds " + std::to_string(characters) + " characters, past its bound of " + std::to_string(bound);
        }
        return {};
    }

    const PanelFieldSpec& m_spec;
    QCborValue m_value;
};

/// The ranges a kind states: the least and the most of its value, of its brush.
struct RangeFields
{
    const char* least;
    const char* most;
};

constexpr RangeFields kRangeFields[] = {{"min", "max"}, {"min_brush_size", "max_brush_size"}};

class Validation
{
public:
    explicit Validation(std::string* error) : m_error(error) {}

    [[nodiscard]] bool nodes(const std::vector<PanelNode>& nodes, const int depth)
    {
        if (depth > kMaxDepth)
        {
            return refuse("groups nest deeper than " + std::to_string(kMaxDepth));
        }
        for (const PanelNode& node : nodes)
        {
            if (++m_count > kMaxNodes)
            {
                return refuse("the description holds more than " + std::to_string(kMaxNodes) + " controls");
            }
            if (!this->node(node, depth))
            {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] bool refuse(std::string why)
    {
        if (m_error)
        {
            *m_error = std::move(why);
        }
        return false;
    }

    [[nodiscard]] bool node(const PanelNode& node, const int depth)
    {
        const PanelKindSpec* kind = panelKind(node.kind);
        if (kind == nullptr)
        {
            return refuse("the description holds a kind the panel does not know: " + quoted(node.kind));
        }
        if (node.kind != QLatin1String(kind->name))
        {
            return refuse("the description names " + quoted(node.kind) + " by an alias of " + kind->name);
        }
        if (!node.children.empty() && !kind->holdsChildren)
        {
            return refuse(nodeName(node) + " holds children, which a " + kind->name + " does not");
        }
        for (auto it = node.fields.begin(); it != node.fields.end(); ++it)
        {
            const QString name = it.key().toString();
            const PanelFieldSpec* spec = kind->field(name);
            if (spec == nullptr)
            {
                return refuse(nodeName(node) + " states " + quoted(name) + ", a field a " + kind->name +
                              " does not have");
            }
            if (const std::string problem = FieldCheck(*spec, it.value()).problem(); !problem.empty())
            {
                return refuse(nodeName(node) + ": its " + quoted(name) + " " + problem);
            }
        }
        if (!ranges(node))
        {
            return false;
        }
        if (kind->vocabulary == PanelVocabulary::Parameters &&
            (kind->role == PanelKindRole::Parameter || kind->role == PanelKindRole::Action) &&
            node.id().trimmed().isEmpty())
        {
            return refuse("a " + node.kind.toStdString() + " has no id, which a parameter and an action need");
        }
        if (kind->role == PanelKindRole::Parameter)
        {
            const QString id = node.id();
            if (!id.isEmpty() && !m_values.insert(id).second)
            {
                return refuse("two values are named " + quoted(id));
            }
        }
        return nodes(node.children, depth + 1);
    }

    [[nodiscard]] bool ranges(const PanelNode& node)
    {
        for (const RangeFields& range : kRangeFields)
        {
            const std::optional<double> least = numberOf(node.field(QLatin1String(range.least)));
            const std::optional<double> most = numberOf(node.field(QLatin1String(range.most)));
            if (least.has_value() && most.has_value() && *least > *most)
            {
                return refuse(nodeName(node) + " states a range that is not one: its " + range.least +
                              " is above its " + range.most);
            }
        }
        const std::optional<double> step = numberOf(node.field(u"step"));
        if (step.has_value() && *step < 0.0)
        {
            return refuse(nodeName(node) + " states a negative step");
        }
        return true;
    }

    std::string* m_error;
    int m_count = 0;
    std::set<QString> m_values;
};

// ---- Reading.

/// The fields of a source map its kind has, in the order the kind lists them.
[[nodiscard]] QCborMap knownFields(const PanelKindSpec& kind, const QCborMap& source)
{
    QCborMap fields;
    for (const PanelFieldSpec& spec : kind.fields)
    {
        const QString name = QString::fromLatin1(spec.name);
        const auto it = source.find(name);
        if (it != source.end() && !it->isUndefined())
        {
            fields.insert(name, it.value());
        }
    }
    return fields;
}

/// A node of a known kind, read from a map: its fields kept, the rest left.
[[nodiscard]] PanelNode readNode(const PanelKindSpec& kind, const QCborMap& source)
{
    PanelNode node;
    node.kind = QString::fromLatin1(kind.name);
    node.fields = knownFields(kind, source);
    return node;
}

/// Reads a process window's list, keeping where the next entry goes as the window does: the set of tabs open, its
/// page open, the section open.
class ParamListReading
{
public:
    explicit ParamListReading(PanelReadingNotes* notes) : m_notes(notes) {}

    [[nodiscard]] ExtensionPanelDescription read(const QJsonArray& defs)
    {
        for (const QJsonValue& value : defs)
        {
            entry(value.toObject());
        }
        return std::move(m_description);
    }

private:
    // Where a node lies: the index of each node from the top down.
    using Path = std::vector<std::size_t>;

    [[nodiscard]] PanelNode& at(const Path& path)
    {
        PanelNode* node = &m_description.nodes[path.front()];
        for (std::size_t level = 1; level < path.size(); ++level)
        {
            node = &node->children[path[level]];
        }
        return *node;
    }

    [[nodiscard]] std::vector<PanelNode>& childrenAt(const std::optional<Path>& path)
    {
        return path.has_value() ? at(*path).children : m_description.nodes;
    }

    /// Adds a node to a container and answers where it lies.
    Path add(const std::optional<Path>& container, PanelNode node)
    {
        std::vector<PanelNode>& children = childrenAt(container);
        children.push_back(std::move(node));
        Path path = container.value_or(Path{});
        path.push_back(children.size() - 1u);
        return path;
    }

    /// The page open, or the top of the panel.
    [[nodiscard]] std::optional<Path> activeContainer() const
    {
        return m_tab;
    }

    /// The section open, opening one titled as the look titles it when none is.
    Path section()
    {
        if (!m_section.has_value())
        {
            const PanelKindSpec* kind = panelKind(u"section");
            PanelNode opened;
            opened.kind = QString::fromLatin1(kind->name);
            opened.fields.insert(QStringLiteral("label"), m_description.variant == PanelVariant::Custom
                                                              ? QStringLiteral("Creative Controls")
                                                              : QStringLiteral("Parameters"));
            m_section = add(activeContainer(), std::move(opened));
        }
        return *m_section;
    }

    void leaveOut(const QJsonObject& entry, const char* why)
    {
        if (m_notes)
        {
            const QString id = entry.value(QStringLiteral("id")).toString();
            const QString type = entry.value(QStringLiteral("type")).toString();
            m_notes->leftOut.append(
                QStringLiteral("%1 %2: %3")
                    .arg(type.isEmpty() ? QStringLiteral("an entry") : type,
                         id.isEmpty() ? QStringLiteral("with no id") : QLatin1Char('\'') + id + QLatin1Char('\''),
                         QString::fromLatin1(why)));
        }
    }

    void entry(const QJsonObject& entry)
    {
        const QString type = entry.value(QStringLiteral("type")).toString().trimmed().toLower();
        const QCborMap source = QCborMap::fromJsonObject(entry);
        if (type == QLatin1String("meta"))
        {
            m_description.variant =
                entry.value(QStringLiteral("panel_variant")).toString().trimmed().toLower() == QLatin1String("custom")
                    ? PanelVariant::Custom
                    : PanelVariant::Native;
            return;
        }
        if (type == QLatin1String("end_tabs"))
        {
            m_tabs.reset();
            m_tab.reset();
            m_section.reset();
            return;
        }
        const PanelKindSpec* kind = panelKind(type);
        if (kind == nullptr || kind->vocabulary != PanelVocabulary::Parameters)
        {
            leaveOut(entry, type.isEmpty() ? "it states no type" : "the panel draws no such type");
            return;
        }
        PanelNode node = readNode(*kind, source);
        const QString name = QString::fromLatin1(kind->name);
        if (name == QLatin1String("tabs"))
        {
            m_tabs = add(std::nullopt, std::move(node));
            m_tab.reset();
            m_section.reset();
            return;
        }
        if (name == QLatin1String("tab"))
        {
            if (!m_tabs.has_value())
            {
                PanelNode opened;
                opened.kind = QStringLiteral("tabs");
                opened.fields.insert(QStringLiteral("id"), QStringLiteral("tabs"));
                m_tabs = add(std::nullopt, std::move(opened));
            }
            m_tab = add(m_tabs, std::move(node));
            m_section.reset();
            return;
        }
        if (name == QLatin1String("section"))
        {
            m_section = add(activeContainer(), std::move(node));
            return;
        }
        if (name == QLatin1String("info"))
        {
            // The credits stay where they were written; the process that shows the panel shows them, not the panel.
            add(m_section.has_value() ? m_section : activeContainer(), std::move(node));
            return;
        }
        // A readout and the mask's editor need no id; any other kind does, and goes where the window drew it.
        if (name != QLatin1String("percentage_mix_label") && name != QLatin1String("value_description_label") &&
            name != QLatin1String("manual_mask_editor") && entry.value(QStringLiteral("id")).toString().isEmpty())
        {
            leaveOut(entry, "it states no id");
            return;
        }
        if (name == QLatin1String("vector_slope"))
        {
            add(activeContainer(), std::move(node));
            return;
        }
        add(section(), std::move(node));
    }

    PanelReadingNotes* m_notes;
    ExtensionPanelDescription m_description;
    std::optional<Path> m_tabs;
    std::optional<Path> m_tab;
    std::optional<Path> m_section;
};

[[nodiscard]] bool readWidgetNode(const QCborValue& value, const int depth, PanelNode* out, std::string* error)
{
    const auto refuse = [error](std::string why) {
        if (error)
        {
            *error = std::move(why);
        }
        return false;
    };
    if (depth > kMaxDepth)
    {
        return refuse("the tree nests deeper than " + std::to_string(kMaxDepth));
    }
    if (!value.isMap())
    {
        return refuse("a node of the tree is not a map");
    }
    const QCborMap map = value.toMap();
    const QCborValue widget = map.value(QStringLiteral("widget"));
    if (!widget.isString())
    {
        return refuse("a node of the tree names no widget");
    }
    const PanelKindSpec* kind = panelKind(widget.toString());
    if (kind == nullptr || kind->vocabulary != PanelVocabulary::Widgets)
    {
        return refuse("the tree holds a widget the panel does not know: " + quoted(widget.toString()));
    }
    *out = readNode(*kind, map);
    const QCborValue children = map.value(QStringLiteral("children"));
    if (children.isUndefined())
    {
        return true;
    }
    if (!children.isArray())
    {
        return refuse(nodeName(*out) + ": its children are not a list");
    }
    for (const QCborValue& child : children.toArray())
    {
        PanelNode read;
        if (!readWidgetNode(child, depth + 1, &read, error))
        {
            return false;
        }
        out->children.push_back(std::move(read));
    }
    return true;
}

[[nodiscard]] QCborMap nodeToCbor(const PanelNode& node)
{
    QCborMap map;
    map.insert(QStringLiteral("kind"), node.kind);
    for (auto it = node.fields.begin(); it != node.fields.end(); ++it)
    {
        map.insert(it.key(), it.value());
    }
    if (!node.children.empty())
    {
        QCborArray children;
        for (const PanelNode& child : node.children)
        {
            children.append(nodeToCbor(child));
        }
        map.insert(QStringLiteral("children"), children);
    }
    return map;
}

[[nodiscard]] bool nodeFromCbor(const QCborValue& value, const int depth, PanelNode* out, std::string* error)
{
    const auto refuse = [error](std::string why) {
        if (error)
        {
            *error = std::move(why);
        }
        return false;
    };
    if (depth > kMaxDepth)
    {
        return refuse("groups nest deeper than " + std::to_string(kMaxDepth));
    }
    if (!value.isMap())
    {
        return refuse("a node is not a map");
    }
    const QCborMap map = value.toMap();
    const QCborValue kindName = map.value(QStringLiteral("kind"));
    if (!kindName.isString())
    {
        return refuse("a node names no kind");
    }
    const PanelKindSpec* kind = panelKind(kindName.toString());
    if (kind == nullptr)
    {
        return refuse("the description holds a kind the panel does not know: " + quoted(kindName.toString()));
    }
    PanelNode node = readNode(*kind, map);
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        if (!it.key().isString())
        {
            return refuse(nodeName(node) + " has a field whose name is not a text");
        }
        const QString name = it.key().toString();
        if (name != QLatin1String("kind") && name != QLatin1String("children") && kind->field(name) == nullptr)
        {
            return refuse(nodeName(node) + " states " + quoted(name) + ", a field a " + kind->name + " does not have");
        }
    }
    const QCborValue children = map.value(QStringLiteral("children"));
    if (!children.isUndefined())
    {
        if (!children.isArray())
        {
            return refuse(nodeName(node) + ": its children are not a list");
        }
        for (const QCborValue& child : children.toArray())
        {
            PanelNode read;
            if (!nodeFromCbor(child, depth + 1, &read, error))
            {
                return false;
            }
            node.children.push_back(std::move(read));
        }
    }
    *out = std::move(node);
    return true;
}

int countNodes(const std::vector<PanelNode>& nodes) noexcept
{
    int count = 0;
    for (const PanelNode& node : nodes)
    {
        count += 1 + countNodes(node.children);
    }
    return count;
}

} // namespace

const char* panelFieldTypeName(const PanelFieldType type) noexcept
{
    switch (type)
    {
    case PanelFieldType::Text:
        return "text";
    case PanelFieldType::Number:
        return "number";
    case PanelFieldType::Integer:
        return "whole number";
    case PanelFieldType::Boolean:
        return "boolean";
    case PanelFieldType::Scalar:
        return "value";
    case PanelFieldType::Condition:
        return "condition";
    case PanelFieldType::Options:
        return "entries";
    case PanelFieldType::Integers:
        return "whole numbers";
    case PanelFieldType::Points:
        return "points";
    case PanelFieldType::Ranges:
        return "ranges";
    case PanelFieldType::Actions:
        return "actions";
    case PanelFieldType::Vectors:
        return "vectors";
    case PanelFieldType::Columns:
        return "columns";
    case PanelFieldType::Rows:
        return "rows";
    case PanelFieldType::Lines:
        return "lines";
    }
    return "";
}

const char* panelKindRoleName(const PanelKindRole role) noexcept
{
    switch (role)
    {
    case PanelKindRole::Parameter:
        return "parameter";
    case PanelKindRole::Group:
        return "group";
    case PanelKindRole::Action:
        return "action";
    case PanelKindRole::Display:
        return "display";
    case PanelKindRole::Hosted:
        return "hosted";
    }
    return "";
}

const PanelFieldSpec* PanelKindSpec::field(const QStringView fieldName) const noexcept
{
    for (const PanelFieldSpec& spec : fields)
    {
        if (fieldName == QLatin1String(spec.name))
        {
            return &spec;
        }
    }
    return nullptr;
}

std::span<const PanelKindSpec> panelKinds()
{
    static const std::vector<PanelKindSpec> kinds = buildKinds();
    return kinds;
}

const PanelKindSpec* panelKind(const QStringView name) noexcept
{
    for (const PanelKindSpec& kind : panelKinds())
    {
        if (name == QLatin1String(kind.name))
        {
            return &kind;
        }
        for (const char* alias : kind.aliases)
        {
            if (name == QLatin1String(alias))
            {
                return &kind;
            }
        }
    }
    return nullptr;
}

bool isPanelCredits(const PanelNode& node)
{
    if (node.kind != QLatin1String("info"))
    {
        return false;
    }
    const auto marked = [&node](const char* key) {
        return node.field(QLatin1String(key)).toString().trimmed().toLower();
    };
    const QString id = marked("id");
    const QString role = marked("role");
    const QString purpose = marked("purpose");
    return id == QLatin1String("attribution") || id == QLatin1String("credits") || id == QLatin1String("credit") ||
           role == QLatin1String("attribution") || role == QLatin1String("credits") ||
           purpose == QLatin1String("attribution") || purpose == QLatin1String("credits");
}

QString PanelNode::id() const
{
    return fields.value(QStringLiteral("id")).toString();
}

QCborValue PanelNode::field(const QAnyStringView name) const
{
    return fields.value(name.toString());
}

bool PanelNode::has(const QAnyStringView name) const
{
    return fields.contains(name.toString());
}

ExtensionPanelDescription panelDescriptionFromParamDefs(const QJsonArray& defs, PanelReadingNotes* notes)
{
    return ParamListReading(notes).read(defs);
}

bool panelDescriptionFromWidgetTree(const QCborMap& root, ExtensionPanelDescription* out, std::string* error)
{
    if (out == nullptr)
    {
        if (error)
        {
            *error = "the description has nowhere to be written";
        }
        return false;
    }
    PanelNode node;
    if (!readWidgetNode(root, 0, &node, error))
    {
        return false;
    }
    out->variant = PanelVariant::Native;
    out->nodes.clear();
    out->nodes.push_back(std::move(node));
    return true;
}

QCborMap panelDescriptionToCbor(const ExtensionPanelDescription& description)
{
    QCborArray nodes;
    for (const PanelNode& node : description.nodes)
    {
        nodes.append(nodeToCbor(node));
    }
    return QCborMap{
        {QStringLiteral("variant"),
         description.variant == PanelVariant::Custom ? QStringLiteral("custom") : QStringLiteral("native")},
        {QStringLiteral("nodes"), nodes},
    };
}

bool panelDescriptionFromCbor(const QCborValue& value, ExtensionPanelDescription* out, std::string* error)
{
    const auto refuse = [error](std::string why) {
        if (error)
        {
            *error = std::move(why);
        }
        return false;
    };
    if (out == nullptr)
    {
        return refuse("the description has nowhere to be written");
    }
    if (!value.isMap())
    {
        return refuse("a description is a map of its variant and its nodes");
    }
    const QCborMap map = value.toMap();
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        const QString key = it.key().toString();
        if (key != QLatin1String("variant") && key != QLatin1String("nodes"))
        {
            return refuse("a description states " + quoted(key) + ", which it has not");
        }
    }
    ExtensionPanelDescription description;
    const QCborValue variant = map.value(QStringLiteral("variant"));
    if (!variant.isUndefined())
    {
        if (variant.toString() == QLatin1String("custom"))
        {
            description.variant = PanelVariant::Custom;
        }
        else if (variant.toString() != QLatin1String("native"))
        {
            return refuse("a description's variant is `native` or `custom`");
        }
    }
    const QCborValue nodes = map.value(QStringLiteral("nodes"));
    if (!nodes.isArray())
    {
        return refuse("a description's nodes are not a list");
    }
    if (nodes.toArray().size() > kMaxNodes)
    {
        return refuse("the description holds more than " + std::to_string(kMaxNodes) + " controls");
    }
    for (const QCborValue& node : nodes.toArray())
    {
        PanelNode read;
        if (!nodeFromCbor(node, 1, &read, error))
        {
            return false;
        }
        description.nodes.push_back(std::move(read));
        if (panelNodeCount(description) > kMaxNodes)
        {
            return refuse("the description holds more than " + std::to_string(kMaxNodes) + " controls");
        }
    }
    if (!validatePanelDescription(description, error))
    {
        return false;
    }
    *out = std::move(description);
    return true;
}

bool validatePanelDescription(const ExtensionPanelDescription& description, std::string* error)
{
    return Validation(error).nodes(description.nodes, 1);
}

int panelNodeCount(const ExtensionPanelDescription& description) noexcept
{
    return countNodes(description.nodes);
}

} // namespace an::ui::extensions
