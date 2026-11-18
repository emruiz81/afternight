// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An extension's panel as data: every kind of both vocabularies read with its fields, each bound refused with its
/// reason, a kind or a field the panel does not know refused by name, a description written and read back the same,
/// and a process window's list read into the tree the window draws.

#include "controls/ExtensionPanelDescription.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace
{

using an::ui::extensions::ExtensionPanelDescription;
using an::ui::extensions::panelDescriptionFromCbor;
using an::ui::extensions::panelDescriptionFromParamDefs;
using an::ui::extensions::panelDescriptionFromWidgetTree;
using an::ui::extensions::panelDescriptionToCbor;
using an::ui::extensions::PanelFieldSpec;
using an::ui::extensions::PanelFieldType;
using an::ui::extensions::panelKind;
using an::ui::extensions::panelKinds;
using an::ui::extensions::PanelKindSpec;
using an::ui::extensions::PanelNode;
using an::ui::extensions::PanelReadingNotes;
using an::ui::extensions::PanelVariant;
using an::ui::extensions::validatePanelDescription;
namespace panel_limits = an::ui::extensions::panel_limits;

/// A value a field of that type holds.
QCborValue sampleOf(const PanelFieldType type)
{
    switch (type)
    {
    case PanelFieldType::Text:
        return QStringLiteral("text");
    case PanelFieldType::Number:
        return 0.5;
    case PanelFieldType::Integer:
        return 3;
    case PanelFieldType::Boolean:
        return true;
    case PanelFieldType::Scalar:
        return QStringLiteral("value");
    case PanelFieldType::Condition:
        return QStringLiteral("mode == fast");
    case PanelFieldType::Options:
        return QCborArray{QCborArray{QStringLiteral("Fast"), QStringLiteral("fast")}, QStringLiteral("slow")};
    case PanelFieldType::Integers:
        return QCborArray{1, 3};
    case PanelFieldType::Points:
        return QCborArray{QCborArray{0.0, 0.0}, QCborMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}}};
    case PanelFieldType::Ranges:
        // One range: a list holding one list, which brace initialisation would read as a copy.
        return QCborArray{QCborValue(QCborArray{0.0, 0.5, QStringLiteral("Low")})};
    case PanelFieldType::Actions:
        return QCborArray{QCborMap{{QStringLiteral("id"), QStringLiteral("go")},
                                   {QStringLiteral("label"), QStringLiteral("Go")},
                                   {QStringLiteral("preview_refresh_role"), true}}};
    case PanelFieldType::Vectors:
        return QCborArray{QCborMap{{QStringLiteral("key"), QStringLiteral("red")},
                                   {QStringLiteral("hue_param"), QStringLiteral("red_hue")},
                                   {QStringLiteral("saturation_param"), QStringLiteral("red_saturation")},
                                   {QStringLiteral("base_hue_degrees"), 10.0},
                                   {QStringLiteral("color"), QCborArray{255, 0, 0}}}};
    case PanelFieldType::Columns:
        return QCborArray{QStringLiteral("Name"), QCborMap{{QStringLiteral("id"), QStringLiteral("size")},
                                                           {QStringLiteral("label"), QStringLiteral("Size")}}};
    case PanelFieldType::Rows:
        return QCborArray{QCborArray{QStringLiteral("a"), 1}, QCborMap{{QStringLiteral("size"), 2}}};
    case PanelFieldType::Lines:
        return QCborArray{QStringLiteral("first"), QCborMap{{QStringLiteral("text"), QStringLiteral("second")},
                                                            {QStringLiteral("stream"), QStringLiteral("stderr")}}};
    }
    return {};
}

/// A node of a kind stating every field it has, each with a value of its type.
QCborMap everyFieldOf(const PanelKindSpec& kind)
{
    QCborMap node{{QStringLiteral("kind"), QString::fromLatin1(kind.name)}};
    for (const PanelFieldSpec& field : kind.fields)
    {
        node.insert(QString::fromLatin1(field.name), sampleOf(field.type));
    }
    if (kind.holdsChildren)
    {
        node.insert(QStringLiteral("children"),
                    QCborArray{QCborMap{{QStringLiteral("kind"), QStringLiteral("column")}}});
    }
    return node;
}

QCborMap descriptionOf(const QCborArray& nodes, const QString& variant = QStringLiteral("native"))
{
    return QCborMap{{QStringLiteral("variant"), variant}, {QStringLiteral("nodes"), nodes}};
}

/// Reads a description that is to be refused and answers why.
std::string refusalOf(const QCborMap& description)
{
    ExtensionPanelDescription read;
    std::string error;
    EXPECT_FALSE(panelDescriptionFromCbor(description, &read, &error)) << "it was read";
    return error;
}

QCborMap floatNode(const QString& id)
{
    return QCborMap{{QStringLiteral("kind"), QStringLiteral("float")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("min"), 0.0},
                    {QStringLiteral("max"), 1.0}};
}

TEST(PanelDescription, EachKindParsesWithItsFields)
{
    ASSERT_FALSE(panelKinds().empty());
    for (const PanelKindSpec& kind : panelKinds())
    {
        SCOPED_TRACE(kind.name);
        ExtensionPanelDescription read;
        std::string error;
        ASSERT_TRUE(panelDescriptionFromCbor(descriptionOf({everyFieldOf(kind)}), &read, &error)) << error;
        ASSERT_EQ(read.nodes.size(), 1u);
        const PanelNode& node = read.nodes.front();
        EXPECT_EQ(node.kind, QString::fromLatin1(kind.name));
        for (const PanelFieldSpec& field : kind.fields)
        {
            EXPECT_EQ(node.field(QString::fromLatin1(field.name)), sampleOf(field.type)) << field.name;
        }
        EXPECT_EQ(node.children.size(), kind.holdsChildren ? 1u : 0u);
    }
}

TEST(PanelDescription, AnAliasIsReadAsItsKind)
{
    EXPECT_EQ(panelKind(u"double"), panelKind(u"float"));
    EXPECT_EQ(panelKind(u"button"), panelKind(u"action"));

    ExtensionPanelDescription read;
    std::string error;
    ASSERT_TRUE(panelDescriptionFromCbor(descriptionOf({QCborMap{{QStringLiteral("kind"), QStringLiteral("button")},
                                                                 {QStringLiteral("id"), QStringLiteral("go")}}}),
                                         &read, &error))
        << error;
    EXPECT_EQ(read.nodes.front().kind, QStringLiteral("action"));
}

TEST(PanelDescription, AnUnknownKindIsRefusedByName)
{
    EXPECT_NE(refusalOf(descriptionOf({QCborMap{{QStringLiteral("kind"), QStringLiteral("colour_wheel")}}}))
                  .find("'colour_wheel'"),
              std::string::npos);

    // A field the kind does not have is refused by name too.
    QCborMap node = floatNode(QStringLiteral("strength"));
    node.insert(QStringLiteral("gradient"), true);
    const std::string field = refusalOf(descriptionOf({node}));
    EXPECT_NE(field.find("'gradient'"), std::string::npos) << field;

    // And a widget the tree names.
    ExtensionPanelDescription tree;
    std::string error;
    EXPECT_FALSE(
        panelDescriptionFromWidgetTree(QCborMap{{QStringLiteral("widget"), QStringLiteral("dial")}}, &tree, &error));
    EXPECT_NE(error.find("'dial'"), std::string::npos) << error;
}

TEST(PanelDescription, TooManyControlsAreRefused)
{
    QCborArray nodes;
    for (int index = 0; index <= panel_limits::kMaxNodes; ++index)
    {
        nodes.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("info")},
                              {QStringLiteral("text"), QStringLiteral("note")}});
    }
    EXPECT_NE(refusalOf(descriptionOf(nodes)).find("more than 512 controls"), std::string::npos);

    // Nested ones count: one column holding the rest.
    QCborArray children;
    for (int index = 0; index < panel_limits::kMaxNodes; ++index)
    {
        children.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("checkbox")}});
    }
    EXPECT_NE(refusalOf(descriptionOf({QCborMap{{QStringLiteral("kind"), QStringLiteral("column")},
                                                {QStringLiteral("children"), children}}}))
                  .find("more than 512 controls"),
              std::string::npos);

    // And groups nested too deep.
    QCborMap deepest{{QStringLiteral("kind"), QStringLiteral("column")}};
    for (int level = 0; level < panel_limits::kMaxDepth + 1; ++level)
    {
        deepest = QCborMap{{QStringLiteral("kind"), QStringLiteral("column")},
                           {QStringLiteral("children"), QCborArray{deepest}}};
    }
    EXPECT_NE(refusalOf(descriptionOf({deepest})).find("nest deeper"), std::string::npos);
}

TEST(PanelDescription, TooLongATextIsRefused)
{
    QCborMap label = floatNode(QStringLiteral("strength"));
    label.insert(QStringLiteral("label"), QString(panel_limits::kMaxShortText + 1, QLatin1Char('a')));
    const std::string labelRefusal = refusalOf(descriptionOf({label}));
    EXPECT_NE(labelRefusal.find("'label'"), std::string::npos) << labelRefusal;
    EXPECT_NE(labelRefusal.find("past its bound of 256"), std::string::npos) << labelRefusal;

    const QCborMap banner{{QStringLiteral("kind"), QStringLiteral("info")},
                          {QStringLiteral("text"), QString(panel_limits::kMaxLongText + 1, QLatin1Char('a'))}};
    EXPECT_NE(refusalOf(descriptionOf({banner})).find("past its bound of 8192"), std::string::npos);

    // A text may not hold a control character.
    QCborMap control = floatNode(QStringLiteral("strength"));
    control.insert(QStringLiteral("label"), QStringLiteral("bell\aring"));
    EXPECT_NE(refusalOf(descriptionOf({control})).find("control character"), std::string::npos);

    // Nor a list run past its bound.
    QCborArray options;
    for (int index = 0; index <= panel_limits::kMaxOptions; ++index)
    {
        options.append(QStringLiteral("entry"));
    }
    EXPECT_NE(refusalOf(descriptionOf({QCborMap{{QStringLiteral("kind"), QStringLiteral("choice")},
                                                {QStringLiteral("id"), QStringLiteral("mode")},
                                                {QStringLiteral("options"), options}}}))
                  .find("past its bound of 1024"),
              std::string::npos);
}

TEST(PanelDescription, ARangeThatIsNotOneIsRefused)
{
    QCborMap reversed = floatNode(QStringLiteral("strength"));
    reversed.insert(QStringLiteral("min"), 2.0);
    const std::string refusal = refusalOf(descriptionOf({reversed}));
    EXPECT_NE(refusal.find("range that is not one"), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("'strength'"), std::string::npos) << refusal;

    const QCborMap brush{{QStringLiteral("kind"), QStringLiteral("manual_mask_editor")},
                         {QStringLiteral("min_brush_size"), 100.0},
                         {QStringLiteral("max_brush_size"), 20.0}};
    EXPECT_NE(refusalOf(descriptionOf({brush})).find("range that is not one"), std::string::npos);

    const QCborMap words{
        {QStringLiteral("kind"), QStringLiteral("value_description_label")},
        {QStringLiteral("ranges"), QCborArray{QCborValue(QCborArray{1.0, 0.0, QStringLiteral("Upside down")})}}};
    EXPECT_NE(refusalOf(descriptionOf({words})).find("range that is not one"), std::string::npos);

    QCborMap backwards = floatNode(QStringLiteral("strength"));
    backwards.insert(QStringLiteral("step"), -0.1);
    EXPECT_NE(refusalOf(descriptionOf({backwards})).find("negative step"), std::string::npos);

    QCborMap infinite = floatNode(QStringLiteral("strength"));
    infinite.insert(QStringLiteral("max"), std::numeric_limits<double>::infinity());
    EXPECT_NE(refusalOf(descriptionOf({infinite})).find("finite"), std::string::npos);
}

TEST(PanelDescription, AChoiceWithNoEntriesIsRefused)
{
    for (const char* kind : {"choice", "combo", "button_group"})
    {
        SCOPED_TRACE(kind);
        const QCborMap empty{{QStringLiteral("kind"), QString::fromLatin1(kind)},
                             {QStringLiteral("id"), QStringLiteral("mode")},
                             {QStringLiteral("options"), QCborArray{}}};
        EXPECT_NE(refusalOf(descriptionOf({empty})).find("no entries"), std::string::npos);
    }
}

TEST(PanelDescription, AParameterNeedsAnIdOfItsOwn)
{
    EXPECT_NE(refusalOf(descriptionOf({QCborMap{{QStringLiteral("kind"), QStringLiteral("bool")}}})).find("has no id"),
              std::string::npos);
    EXPECT_NE(refusalOf(descriptionOf({floatNode(QStringLiteral("strength")), floatNode(QStringLiteral("strength"))}))
                  .find("two values are named 'strength'"),
              std::string::npos);
}

TEST(PanelDescription, ADescriptionRoundTrips)
{
    QCborArray nodes;
    for (const PanelKindSpec& kind : panelKinds())
    {
        QCborMap node = everyFieldOf(kind);
        node.insert(QStringLiteral("id"), QStringLiteral("node_%1").arg(nodes.size()));
        nodes.append(node);
    }
    ExtensionPanelDescription first;
    std::string error;
    ASSERT_TRUE(panelDescriptionFromCbor(descriptionOf(nodes, QStringLiteral("custom")), &first, &error)) << error;
    EXPECT_EQ(first.variant, PanelVariant::Custom);

    const QCborMap written = panelDescriptionToCbor(first);
    ExtensionPanelDescription second;
    ASSERT_TRUE(panelDescriptionFromCbor(QCborValue::fromCbor(QCborValue(written).toCbor()), &second, &error)) << error;
    EXPECT_EQ(second, first);
    EXPECT_EQ(QCborValue(panelDescriptionToCbor(second)).toCbor(), QCborValue(written).toCbor());
}

TEST(PanelDescription, AProcessWindowsListReadsAsTheWindowDrawsIt)
{
    const QJsonArray defs{
        QJsonObject{{"id", "window_meta"}, {"type", "meta"}, {"panel_variant", "Custom"}},
        QJsonObject{{"id", "credits"}, {"type", "info"}, {"text", "By the examples."}},
        QJsonObject{{"id", "loose"}, {"type", "Double"}, {"label", "Loose"}, {"surplus", true}},
        QJsonObject{{"id", "page"}, {"type", "tab"}, {"label", "Page"}},
        QJsonObject{{"id", "in_page"}, {"type", "section"}, {"label", "In Page"}},
        QJsonObject{{"id", "slope"}, {"type", "vector_slope"}},
        QJsonObject{{"id", "after"}, {"type", "bool"}},
        QJsonObject{{"id", "done"}, {"type", "end_tabs"}},
        QJsonObject{{"id", "note"}, {"type", "info"}, {"text", "Loose note."}},
        QJsonObject{{"type", "int"}},
        QJsonObject{{"id", "unknown"}, {"type", "colour"}},
        QJsonObject{{"id", "go"}, {"type", "button"}},
    };
    PanelReadingNotes notes;
    const ExtensionPanelDescription read = panelDescriptionFromParamDefs(defs, &notes);
    EXPECT_EQ(read.variant, PanelVariant::Custom);
    std::string error;
    EXPECT_TRUE(validatePanelDescription(read, &error)) << error;

    // The credits and the custom look's section of the loose parameter; the tabs opened by the page, holding the
    // page's section and the slope beside it; the note at the top and a section of the look for the action.
    ASSERT_EQ(read.nodes.size(), 5u);
    EXPECT_EQ(read.nodes[0].kind, QStringLiteral("info"));
    EXPECT_TRUE(an::ui::extensions::isPanelCredits(read.nodes[0]));
    EXPECT_EQ(read.nodes[1].kind, QStringLiteral("section"));
    EXPECT_EQ(read.nodes[1].field(u"label").toString(), QStringLiteral("Creative Controls"));
    ASSERT_EQ(read.nodes[1].children.size(), 1u);
    EXPECT_EQ(read.nodes[1].children[0].kind, QStringLiteral("float"));
    EXPECT_FALSE(read.nodes[1].children[0].has(u"surplus"));
    EXPECT_EQ(read.nodes[2].kind, QStringLiteral("tabs"));
    EXPECT_EQ(read.nodes[2].id(), QStringLiteral("tabs"));
    ASSERT_EQ(read.nodes[2].children.size(), 1u);
    const PanelNode& page = read.nodes[2].children[0];
    EXPECT_EQ(page.kind, QStringLiteral("tab"));
    ASSERT_EQ(page.children.size(), 2u);
    EXPECT_EQ(page.children[0].id(), QStringLiteral("in_page"));
    ASSERT_EQ(page.children[0].children.size(), 1u);
    EXPECT_EQ(page.children[0].children[0].id(), QStringLiteral("after"));
    EXPECT_EQ(page.children[1].kind, QStringLiteral("vector_slope"));
    EXPECT_EQ(read.nodes[3].id(), QStringLiteral("note"));
    EXPECT_EQ(read.nodes[4].kind, QStringLiteral("section"));
    ASSERT_EQ(read.nodes[4].children.size(), 1u);
    EXPECT_EQ(read.nodes[4].children[0].kind, QStringLiteral("action"));

    ASSERT_EQ(notes.leftOut.size(), 2);
    EXPECT_TRUE(notes.leftOut[0].contains(QStringLiteral("no id")));
    EXPECT_TRUE(notes.leftOut[1].contains(QStringLiteral("'unknown'")));
}

TEST(PanelDescription, AWidgetTreeReadsAsOneNode)
{
    const QCborMap root{
        {QStringLiteral("widget"), QStringLiteral("column")},
        {QStringLiteral("children"),
         QCborArray{QCborMap{{QStringLiteral("widget"), QStringLiteral("card")},
                             {QStringLiteral("label"), QStringLiteral("Paths")},
                             {QStringLiteral("children"),
                              QCborArray{QCborMap{{QStringLiteral("widget"), QStringLiteral("dir_picker")},
                                                  {QStringLiteral("id"), QStringLiteral("models")},
                                                  {QStringLiteral("value"), QStringLiteral("/tmp")}}}}},
                    QCborMap{{QStringLiteral("widget"), QStringLiteral("checkbox")},
                             {QStringLiteral("id"), QStringLiteral("gpu")},
                             {QStringLiteral("value"), true}}}},
    };
    ExtensionPanelDescription read;
    std::string error;
    ASSERT_TRUE(panelDescriptionFromWidgetTree(root, &read, &error)) << error;
    EXPECT_TRUE(validatePanelDescription(read, &error)) << error;
    ASSERT_EQ(read.nodes.size(), 1u);
    EXPECT_EQ(read.nodes[0].kind, QStringLiteral("column"));
    ASSERT_EQ(read.nodes[0].children.size(), 2u);
    EXPECT_EQ(read.nodes[0].children[0].children[0].field(u"value").toString(), QStringLiteral("/tmp"));
    EXPECT_TRUE(read.nodes[0].children[1].field(u"value").toBool());
}

} // namespace
