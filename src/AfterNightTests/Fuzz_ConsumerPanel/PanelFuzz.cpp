// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What must hold of every description of an extension's panel the application could be handed.
///
/// An extension describes its controls as data - a process window's list of parameters, a tree of widgets, or a
/// description in its own form - and the application builds controls from it. The extension is code a person
/// installed, and what it states is read as untrusted data: each reading answers, and the validator refuses a
/// description whole, with a reason, before a control is made from it.
///
/// The claims:
///
/// * **A reading answers.** A description in its own form is read or refused with a reason; a tree of widgets is read
///   or refused with a reason; a list of parameters is always read, what it leaves out named. None may crash.
/// * **The validator answers, and what it accepts it accepts again.** A description read from any form is held to
///   its kinds and bounds and refused with a reason or accepted; one accepted, written in its own form and read back,
///   is equal to it and written again the same - so a description that was valid once is never refused later for
///   having been carried.

#include "../FuzzEngine/FuzzHarness.h"

#include "controls/ExtensionPanelDescription.h"

#include <QByteArray>
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdint>
#include <string>
#include <vector>

namespace an::fuzz
{
namespace
{

namespace panel = an::ui::extensions;

/// The modes a seed's first byte selects.
enum Mode : std::uint8_t
{
    OwnForm = 0,
    ParameterList = 1,
    WidgetTree = 2,
    ModeCount = 3
};

[[nodiscard]] std::vector<std::uint8_t> seed(Mode mode, const QByteArray& payload)
{
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(mode)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

/// Reads CBOR the way the protocol does: bytes that are not well-formed are refused before anything looks inside.
[[nodiscard]] bool decodeCbor(const QByteArray& bytes, QCborValue& value)
{
    QCborParserError parserError;
    value = QCborValue::fromCbor(bytes, &parserError);
    if (parserError.error != QCborError::NoError)
    {
        countReached("cbor-refused");
        return false;
    }
    return true;
}

/// Holds a description read from any form to the validator, and one it accepts to its own form's round trip.
void checkValidated(const panel::ExtensionPanelDescription& description)
{
    std::string error;
    if (!panel::validatePanelDescription(description, &error))
    {
        AN_FUZZ_CHECK(!error.empty(), "a description refused without a reason");
        countReached("descriptions-refused");
        return;
    }
    countReached("descriptions-accepted");
    const QCborMap written = panel::panelDescriptionToCbor(description);
    panel::ExtensionPanelDescription reread;
    std::string rereadError;
    AN_FUZZ_CHECK(
        panel::panelDescriptionFromCbor(QCborValue::fromCbor(QCborValue(written).toCbor()), &reread, &rereadError),
        "a description written from one the validator accepted cannot be read back");
    AN_FUZZ_CHECK(reread == description, "a description read back differs from the one written");
    AN_FUZZ_CHECK(QCborValue(panel::panelDescriptionToCbor(reread)).toCbor() == QCborValue(written).toCbor(),
                  "a description written twice is written differently");
}

void checkOwnForm(const QByteArray& bytes)
{
    QCborValue value;
    if (!decodeCbor(bytes, value))
    {
        return;
    }
    panel::ExtensionPanelDescription description;
    std::string error;
    if (!panel::panelDescriptionFromCbor(value, &description, &error))
    {
        AN_FUZZ_CHECK(!error.empty(), "a description refused without a reason");
        countReached("own-form-refused");
        return;
    }
    countReached("own-form-read");
    checkValidated(description);
}

void checkParameterList(const QByteArray& bytes)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        countReached("json-refused");
        return;
    }
    panel::PanelReadingNotes notes;
    const panel::ExtensionPanelDescription description = panel::panelDescriptionFromParamDefs(document.array(), &notes);
    countReached(notes.leftOut.isEmpty() ? "lists-read-whole" : "lists-read-with-entries-left-out");
    checkValidated(description);
}

void checkWidgetTree(const QByteArray& bytes)
{
    QCborValue value;
    if (!decodeCbor(bytes, value))
    {
        return;
    }
    if (!value.isMap())
    {
        countReached("trees-not-a-map");
        return;
    }
    panel::ExtensionPanelDescription description;
    std::string error;
    if (!panel::panelDescriptionFromWidgetTree(value.toMap(), &description, &error))
    {
        AN_FUZZ_CHECK(!error.empty(), "a tree refused without a reason");
        countReached("trees-refused");
        return;
    }
    countReached("trees-read");
    checkValidated(description);
}

/// A process window's list using every kind it draws.
[[nodiscard]] QJsonArray everyParameterKind()
{
    return QJsonArray{
        QJsonObject{{"id", "window_meta"}, {"type", "meta"}, {"panel_variant", "native"}},
        QJsonObject{{"id", "credits"}, {"type", "info"}, {"text", "By the examples."}},
        QJsonObject{{"id", "tabs"}, {"type", "tabs"}},
        QJsonObject{{"id", "basic"}, {"type", "tab"}, {"label", "Basic"}},
        QJsonObject{{"id", "group"}, {"type", "section"}, {"label", "Group"}, {"presentation", "collapsible"}},
        QJsonObject{{"id", "strength"},
                    {"type", "float"},
                    {"label", "Strength"},
                    {"min", 0.0},
                    {"max", 2.0},
                    {"step", 0.05},
                    {"default", 1.0},
                    {"visible_when", "mode != fast"}},
        QJsonObject{{"id", "passes"}, {"type", "int"}, {"min", 1}, {"max", 9}, {"default", 3}},
        QJsonObject{{"id", "keep"}, {"type", "bool"}, {"default", true}},
        QJsonObject{{"id", "mode"},
                    {"type", "choice"},
                    {"default", "fast"},
                    {"options", QJsonArray{QJsonArray{"Fast", "fast"}, QJsonArray{"Best", "best"}}},
                    {"inline_actions", QJsonArray{QJsonObject{{"id", "refresh"}, {"preview_refresh_role", true}}}}},
        QJsonObject{{"id", "note"}, {"type", "string"}, {"placeholder", "A note"}},
        QJsonObject{{"id", "mix"}, {"type", "percentage_mix_label"}, {"source", "strength"}},
        QJsonObject{{"id", "words"},
                    {"type", "value_description_label"},
                    {"source", "strength"},
                    {"ranges", QJsonArray{QJsonArray{0.0, 1.0, "Low"}, QJsonArray{1.0, 2.0, "High"}}}},
        QJsonObject{{"id", "tools"}, {"type", "tab"}, {"label", "Tools"}},
        QJsonObject{
            {"id", "curve"}, {"type", "curve_editor"}, {"default", QJsonArray{QJsonArray{0, 0}, QJsonArray{1, 1}}}},
        QJsonObject{{"id", "histogram"}, {"type", "rt_histogram"}, {"height", 180}},
        QJsonObject{{"id", "slope"}, {"type", "vector_slope"}},
        QJsonObject{{"id", "reference"}, {"type", "view_selector"}, {"channel_filter", QJsonArray{1, 3}}},
        QJsonObject{{"id", "target"}, {"type", "target_selector"}},
        QJsonObject{{"id", "mask"}, {"type", "manual_mask_editor"}, {"display_param", "mode"}},
        QJsonObject{{"id", "end"}, {"type", "end_tabs"}},
        QJsonObject{
            {"id", "go"}, {"type", "button"}, {"enabled_when", QJsonObject{{"param", "keep"}, {"equals", true}}}},
    };
}

/// A settings tree using every kind of widget.
[[nodiscard]] QCborMap everyWidgetKind()
{
    const QCborArray options{QCborValue(QCborArray{QStringLiteral("Fast"), QStringLiteral("fast")}),
                             QStringLiteral("slow")};
    return QCborMap{
        {QStringLiteral("widget"), QStringLiteral("column")},
        {QStringLiteral("children"),
         QCborArray{
             QCborMap{{QStringLiteral("widget"), QStringLiteral("card")},
                      {QStringLiteral("label"), QStringLiteral("Card")},
                      {QStringLiteral("children"),
                       QCborArray{QCborMap{{QStringLiteral("widget"), QStringLiteral("slider")},
                                           {QStringLiteral("id"), QStringLiteral("amount")},
                                           {QStringLiteral("value"), 0.5}},
                                  QCborMap{{QStringLiteral("widget"), QStringLiteral("spin_int")},
                                           {QStringLiteral("id"), QStringLiteral("count")},
                                           {QStringLiteral("value"), 3}}}}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("collapsible_section")},
                      {QStringLiteral("label"), QStringLiteral("More")},
                      {QStringLiteral("children"),
                       QCborArray{QCborMap{{QStringLiteral("widget"), QStringLiteral("spin_double")},
                                           {QStringLiteral("id"), QStringLiteral("gain")}},
                                  QCborMap{{QStringLiteral("widget"), QStringLiteral("checkbox")},
                                           {QStringLiteral("id"), QStringLiteral("gpu")}},
                                  QCborMap{{QStringLiteral("widget"), QStringLiteral("combo")},
                                           {QStringLiteral("id"), QStringLiteral("mode")},
                                           {QStringLiteral("options"), options}}}}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("line_edit")},
                      {QStringLiteral("id"), QStringLiteral("name")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("file_picker")},
                      {QStringLiteral("id"), QStringLiteral("file")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("dir_picker")},
                      {QStringLiteral("id"), QStringLiteral("folder")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("status")},
                      {QStringLiteral("tone"), QStringLiteral("success")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("console_output")},
                      {QStringLiteral("value"), QCborArray{QStringLiteral("line")}}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("section_header")},
                      {QStringLiteral("action_text"), QStringLiteral("Run")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("info_banner")},
                      {QStringLiteral("message"), QStringLiteral("Note")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("button_group")},
                      {QStringLiteral("id"), QStringLiteral("choice")},
                      {QStringLiteral("options"), options}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("table")},
                      {QStringLiteral("columns"), QCborArray{QStringLiteral("Name")}},
                      {QStringLiteral("rows"), QCborArray{QCborValue(QCborArray{QStringLiteral("a")})}}},
         }},
    };
}

} // namespace

TargetInfo targetInfo()
{
    return {.name = "Fuzz_ConsumerPanel", .maxInputBytes = 16384u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    std::vector<std::vector<std::uint8_t>> seeds;
    const QJsonArray list = everyParameterKind();
    seeds.push_back(seed(ParameterList, QJsonDocument(list).toJson(QJsonDocument::Compact)));
    seeds.push_back(seed(WidgetTree, QCborValue(everyWidgetKind()).toCbor()));

    const panel::ExtensionPanelDescription fromList = panel::panelDescriptionFromParamDefs(list);
    seeds.push_back(seed(OwnForm, QCborValue(panel::panelDescriptionToCbor(fromList)).toCbor()));
    panel::ExtensionPanelDescription fromTree;
    if (panel::panelDescriptionFromWidgetTree(everyWidgetKind(), &fromTree))
    {
        seeds.push_back(seed(OwnForm, QCborValue(panel::panelDescriptionToCbor(fromTree)).toCbor()));
    }

    // A refused one of each kind of refusal: a kind it does not know, a range that is not one, a choice with no
    // entries.
    const QJsonArray refused{
        QJsonObject{{"id", "mode"}, {"type", "choice"}, {"options", QJsonArray{}}},
        QJsonObject{{"id", "strength"}, {"type", "float"}, {"min", 2.0}, {"max", 1.0}},
    };
    seeds.push_back(seed(ParameterList, QJsonDocument(refused).toJson(QJsonDocument::Compact)));
    seeds.push_back(
        seed(OwnForm, QCborValue(QCborMap{{QStringLiteral("nodes"),
                                           QCborArray{QCborMap{{QStringLiteral("kind"), QStringLiteral("dial")}}}}})
                          .toCbor()));
    return seeds;
}

} // namespace an::fuzz

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);
    // The mode is taken from the front, so a seed's first byte names what follows it.
    const std::uint32_t mode = input.takeBelow(an::fuzz::ModeCount);
    const QByteArray bytes = QByteArray::fromStdString(input.restAsText());
    switch (mode)
    {
    case an::fuzz::OwnForm:
        an::fuzz::checkOwnForm(bytes);
        break;
    case an::fuzz::ParameterList:
        an::fuzz::checkParameterList(bytes);
        break;
    default:
        an::fuzz::checkWidgetTree(bytes);
        break;
    }
    return 0;
}
