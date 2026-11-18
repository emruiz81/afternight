// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The controls of an extension's panel: each kind builds its control with its range, step and default; a person's
/// change says one event and a burst of them from one control says its last; values, defaults and patches are taken
/// without a word; tabs, sections and folding sections nest; a hosted kind is the process's to draw; and the panel is
/// drawn from the tokens of each look, in both densities.

#include "controls/ExtensionParamPanel.h"

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramWidget.h"
#include "widgets/VectorSlopeWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

namespace
{

using an::ui::extensions::ExtensionPanelDescription;
using an::ui::extensions::ExtensionPanelHost;
using an::ui::extensions::ExtensionPanelPatch;
using an::ui::extensions::ExtensionParamPanel;
using an::ui::extensions::panelDescriptionFromCbor;
using an::ui::extensions::panelDescriptionFromParamDefs;
using an::ui::extensions::PanelNode;

/// What a panel said, in order.
struct Said
{
    QString id;
    QString name;
    QCborValue value;
    bool coalescable = false;
};

class Listener
{
public:
    explicit Listener(ExtensionParamPanel& panel)
    {
        QObject::connect(&panel, &ExtensionParamPanel::controlEvent, &panel,
                         [this](const QString& id, const QString& name, const QCborValue& value,
                                const bool coalescable) { said.push_back({id, name, value, coalescable}); });
    }

    std::vector<Said> said;
};

/// A panel built from a process window's list.
void buildFromList(ExtensionParamPanel& panel, const QJsonArray& defs)
{
    std::string error;
    ASSERT_TRUE(panel.setDescription(panelDescriptionFromParamDefs(defs), &error)) << error;
}

/// A panel built from a description's own form.
void buildFromCbor(ExtensionParamPanel& panel, const QCborMap& description)
{
    ExtensionPanelDescription read;
    std::string error;
    ASSERT_TRUE(panelDescriptionFromCbor(description, &read, &error)) << error;
    ASSERT_TRUE(panel.setDescription(read, &error)) << error;
}

template <typename T> T* only(QWidget& root, const char* name)
{
    const auto found = root.findChildren<T*>(QString::fromLatin1(name));
    EXPECT_EQ(found.size(), 1) << name;
    return found.isEmpty() ? nullptr : found.front();
}

QJsonArray everyParameterKind()
{
    return QJsonArray{
        QJsonObject{{"id", "strength"},
                    {"type", "float"},
                    {"label", "Strength"},
                    {"min", -0.5},
                    {"max", 2.0},
                    {"step", 0.005},
                    {"default", 1.25},
                    {"tracking", false}},
        QJsonObject{{"id", "passes"}, {"type", "int"}, {"min", 1}, {"max", 9}, {"step", 2}, {"default", 3}},
        QJsonObject{{"id", "keep"}, {"type", "bool"}, {"default", true}},
        QJsonObject{{"id", "mode"},
                    {"type", "choice"},
                    {"default", "best"},
                    {"options", QJsonArray{QJsonArray{"Fast", "fast"}, QJsonArray{"Best", "best"}}},
                    {"inline_actions", QJsonArray{QJsonObject{{"id", "refresh"}, {"label", "Refresh"}}}}},
        QJsonObject{{"id", "note"}, {"type", "string"}, {"default", "hello"}, {"placeholder", "A note"}},
        QJsonObject{{"id", "reference"}, {"type", "view_selector"}, {"placeholder", "Choose one"}},
        QJsonObject{{"id", "curve"},
                    {"type", "curve_editor"},
                    {"default", QJsonArray{QJsonArray{0, 0}, QJsonArray{0.5, 0.25}, QJsonArray{1, 1}}}},
        QJsonObject{{"id", "go"}, {"type", "action"}, {"label", "Go"}, {"button_role", "primary"}},
        QJsonObject{{"id", "note_banner"}, {"type", "info"}, {"tone", "warning"}, {"text", "Careful."}},
        QJsonObject{{"id", "mix"}, {"type", "percentage_mix_label"}, {"source", "keep_share"}},
        QJsonObject{{"id", "words"},
                    {"type", "value_description_label"},
                    {"source", "strength"},
                    {"ranges", QJsonArray{QJsonArray{0.0, 1.0, "Low"}, QJsonArray{1.0, 2.0, "High"}}}},
        QJsonObject{{"id", "histogram"}, {"type", "rt_histogram"}, {"height", 150}},
        QJsonObject{{"id", "slope"}, {"type", "vector_slope"}, {"height", 140}},
    };
}

TEST(ParamPanel, EachKindBuildsItsControlWithItsRangeStepAndDefault)
{
    ExtensionParamPanel panel;
    buildFromList(panel, everyParameterKind());

    auto* strength = only<QDoubleSpinBox>(panel, "anNativeParamDoubleSpinBox");
    ASSERT_NE(strength, nullptr);
    EXPECT_DOUBLE_EQ(strength->minimum(), -0.5);
    EXPECT_DOUBLE_EQ(strength->maximum(), 2.0);
    EXPECT_DOUBLE_EQ(strength->singleStep(), 0.005);
    EXPECT_EQ(strength->decimals(), 3) << "the step's decimals";
    EXPECT_DOUBLE_EQ(strength->value(), 1.25);
    auto* strengthSlider = only<QSlider>(panel, "anNativeParamDoubleSlider");
    ASSERT_NE(strengthSlider, nullptr);
    EXPECT_FALSE(strengthSlider->hasTracking());

    auto* passes = only<QSpinBox>(panel, "anNativeParamSpinBox");
    ASSERT_NE(passes, nullptr);
    EXPECT_EQ(passes->minimum(), 1);
    EXPECT_EQ(passes->maximum(), 9);
    EXPECT_EQ(passes->singleStep(), 2);
    EXPECT_EQ(passes->value(), 3);

    auto* keep = only<QCheckBox>(panel, "anNativeParamCheckBox");
    ASSERT_NE(keep, nullptr);
    EXPECT_TRUE(keep->isChecked());

    auto* mode = only<QComboBox>(panel, "anNativeParamComboBox");
    ASSERT_NE(mode, nullptr);
    ASSERT_EQ(mode->count(), 2);
    EXPECT_EQ(mode->itemText(1), QStringLiteral("Best"));
    EXPECT_EQ(mode->currentData().toString(), QStringLiteral("best"));
    QPushButton* refresh = panel.actionButton(QStringLiteral("refresh"));
    ASSERT_NE(refresh, nullptr);
    EXPECT_EQ(refresh->text(), QStringLiteral("Refresh"));

    auto* note = only<QLineEdit>(panel, "anNativeParamLineEdit");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->text(), QStringLiteral("hello"));
    EXPECT_EQ(note->placeholderText(), QStringLiteral("A note"));

    auto* reference = only<QComboBox>(panel, "anNativeParamViewSelector");
    ASSERT_NE(reference, nullptr);
    ASSERT_EQ(reference->count(), 1);
    EXPECT_EQ(reference->itemText(0), QStringLiteral("Choose one"));

    ASSERT_EQ(panel.curveEditors().size(), 1u);
    EXPECT_EQ(panel.values().value(QStringLiteral("curve")).toArray().size(), 3);

    QPushButton* go = panel.actionButton(QStringLiteral("go"));
    ASSERT_NE(go, nullptr);
    EXPECT_EQ(go->text(), QStringLiteral("Go"));
    EXPECT_EQ(go->property("anProcessButtonRole").toString(), QStringLiteral("primary"));

    an::ui::AnInfoBanner* banner = panel.banner(QStringLiteral("note_banner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_EQ(banner->message(), QStringLiteral("Careful."));
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    auto* words = only<QLabel>(panel, "anNativeParamValueDescriptionLabel");
    ASSERT_NE(words, nullptr);
    EXPECT_EQ(words->text(), QStringLiteral("1.25 - High")) << "a readout follows its parameter as it is built";
    EXPECT_NE(only<QLabel>(panel, "anNativeParamPercentageMixLabel"), nullptr);

    ASSERT_EQ(panel.histograms().size(), 1u);
    EXPECT_FALSE(panel.histograms().front().frame) << "with no host to make the preview's, it is a plain one";
    EXPECT_EQ(panel.histograms().front().scrollArea->minimumHeight(), 150);
    EXPECT_NE(only<an::ui::VectorSlopeWidget>(panel, "anNativeParamVectorSlope"), nullptr);

    // Every parameter's value, in the description's order; actions, readouts and displays hold none.
    const QCborMap values = panel.values();
    EXPECT_EQ(values.keys().size(), 7);
    EXPECT_DOUBLE_EQ(values.value(QStringLiteral("strength")).toDouble(), 1.25);
    EXPECT_EQ(values.value(QStringLiteral("passes")).toInteger(), 3);
    EXPECT_TRUE(values.value(QStringLiteral("keep")).toBool());
    EXPECT_EQ(values.value(QStringLiteral("mode")).toString(), QStringLiteral("best"));
    EXPECT_EQ(values.value(QStringLiteral("note")).toString(), QStringLiteral("hello"));
    EXPECT_EQ(values.value(QStringLiteral("reference")).toString(), QString());
    EXPECT_TRUE(panel.hasVisibleContent());
}

TEST(ParamPanel, ARefusedDescriptionBuildsNothing)
{
    ExtensionParamPanel panel;
    std::string error;
    const ExtensionPanelDescription refused = panelDescriptionFromParamDefs(
        QJsonArray{QJsonObject{{"id", "mode"}, {"type", "choice"}, {"options", QJsonArray{}}}});
    EXPECT_FALSE(panel.setDescription(refused, &error));
    EXPECT_NE(error.find("no entries"), std::string::npos) << error;
    EXPECT_FALSE(panel.description().has_value());
    EXPECT_FALSE(panel.hasVisibleContent());
    EXPECT_TRUE(panel.values().isEmpty());
}

TEST(ParamPanel, AChangeSaysOneEvent)
{
    for (const ExtensionParamPanel::EventDelivery delivery :
         {ExtensionParamPanel::EventDelivery::EachChange, ExtensionParamPanel::EventDelivery::LastOfABurst})
    {
        ExtensionParamPanel panel;
        panel.setEventDelivery(delivery);
        buildFromList(panel, everyParameterKind());
        Listener listener(panel);
        EXPECT_TRUE(listener.said.empty()) << "building says nothing";

        only<QSpinBox>(panel, "anNativeParamSpinBox")->setValue(5);
        QCoreApplication::processEvents();
        ASSERT_EQ(listener.said.size(), 1u);
        EXPECT_EQ(listener.said[0].id, QStringLiteral("passes"));
        EXPECT_EQ(listener.said[0].name, QStringLiteral("value_changed"));
        EXPECT_EQ(listener.said[0].value.toInteger(), 5);
        EXPECT_TRUE(listener.said[0].coalescable) << "a number typed or dragged is a step of a continuous change";

        panel.actionButton(QStringLiteral("go"))->click();
        ASSERT_EQ(listener.said.size(), 2u);
        EXPECT_EQ(listener.said[1].id, QStringLiteral("go"));
        EXPECT_EQ(listener.said[1].name, QStringLiteral("clicked"));
        EXPECT_FALSE(listener.said[1].coalescable);

        only<QCheckBox>(panel, "anNativeParamCheckBox")->setChecked(false);
        ASSERT_EQ(listener.said.size(), 3u) << "a check is said at once";
        EXPECT_EQ(listener.said[2].id, QStringLiteral("keep"));
        EXPECT_FALSE(listener.said[2].value.toBool());
    }
}

TEST(ParamPanel, ABurstFromOneControlIsCoalescedToItsLast)
{
    ExtensionParamPanel panel;
    buildFromList(panel, everyParameterKind());
    Listener listener(panel);

    auto* strength = only<QDoubleSpinBox>(panel, "anNativeParamDoubleSpinBox");
    auto* passes = only<QSpinBox>(panel, "anNativeParamSpinBox");
    strength->setValue(0.5);
    strength->setValue(0.75);
    passes->setValue(7);
    strength->setValue(1.5);
    EXPECT_TRUE(listener.said.empty()) << "a burst waits for the turn to end";

    QCoreApplication::processEvents();
    ASSERT_EQ(listener.said.size(), 2u) << "one event a control";
    EXPECT_EQ(listener.said[0].id, QStringLiteral("strength")) << "in the order each control first changed";
    EXPECT_DOUBLE_EQ(listener.said[0].value.toDouble(), 1.5) << "its last value";
    EXPECT_EQ(listener.said[1].id, QStringLiteral("passes"));
    EXPECT_EQ(listener.said[1].value.toInteger(), 7);

    // A check made during a burst is said after what waited before it.
    strength->setValue(0.25);
    only<QCheckBox>(panel, "anNativeParamCheckBox")->setChecked(false);
    ASSERT_EQ(listener.said.size(), 4u);
    EXPECT_EQ(listener.said[2].id, QStringLiteral("strength"));
    EXPECT_DOUBLE_EQ(listener.said[2].value.toDouble(), 0.25);
    EXPECT_EQ(listener.said[3].id, QStringLiteral("keep"));
    QCoreApplication::processEvents();
    EXPECT_EQ(listener.said.size(), 4u) << "nothing waits once it was said";
}

TEST(ParamPanel, ValuesAndDefaultsAreTakenWithoutAWord)
{
    ExtensionParamPanel panel;
    panel.setEventDelivery(ExtensionParamPanel::EventDelivery::EachChange);
    buildFromList(panel, everyParameterKind());
    Listener listener(panel);

    const QStringList changed = panel.setValues(QCborMap{{QStringLiteral("strength"), 0.5},
                                                         {QStringLiteral("mode"), QStringLiteral("fast")},
                                                         {QStringLiteral("keep"), true},
                                                         {QStringLiteral("missing"), 3}});
    EXPECT_EQ(changed, (QStringList{QStringLiteral("strength"), QStringLiteral("mode")}))
        << "a value already held and an id the panel has not change nothing";
    EXPECT_TRUE(listener.said.empty());
    EXPECT_EQ(only<QLabel>(panel, "anNativeParamValueDescriptionLabel")->text(), QStringLiteral("0.50 - Low"))
        << "the readouts follow";

    const QStringList away = panel.resetToDefaults();
    EXPECT_EQ(away, (QStringList{QStringLiteral("strength"), QStringLiteral("mode")}));
    EXPECT_TRUE(listener.said.empty());
    EXPECT_DOUBLE_EQ(panel.values().value(QStringLiteral("strength")).toDouble(), 1.25);
    EXPECT_EQ(panel.values().value(QStringLiteral("mode")).toString(), QStringLiteral("best"));
    EXPECT_EQ(panel.defaultOf(QStringLiteral("passes")).toInt(), 3);
    EXPECT_EQ(panel.kindOf(QStringLiteral("strength")), QStringLiteral("float"));
}

TEST(ParamPanel, APatchSetsValueEnabledVisibleChoicesAndText)
{
    ExtensionParamPanel panel;
    panel.setEventDelivery(ExtensionParamPanel::EventDelivery::EachChange);
    buildFromList(panel, everyParameterKind());
    Listener listener(panel);

    std::string error;
    ASSERT_TRUE(panel.applyPatches(
        {
            {QStringLiteral("strength"), QStringLiteral("value"), 1.75},
            {QStringLiteral("passes"), QStringLiteral("enabled"), false},
            {QStringLiteral("note"), QStringLiteral("visible"), false},
            {QStringLiteral("mode"), QStringLiteral("choices"),
             QCborArray{QCborArray{QStringLiteral("Slow"), QStringLiteral("slow")},
                        QCborArray{QStringLiteral("Best"), QStringLiteral("best")}}},
            {QStringLiteral("strength"), QStringLiteral("text"), QStringLiteral("Amount")},
            {QStringLiteral("note_banner"), QStringLiteral("text"), QStringLiteral("All good.")},
            {QStringLiteral("go"), QStringLiteral("text"), QStringLiteral("Run")},
            {QStringLiteral("keep"), QStringLiteral("tooltip"), QStringLiteral("Keeps it")},
        },
        &error))
        << error;
    EXPECT_TRUE(listener.said.empty()) << "a patch says nothing";

    EXPECT_DOUBLE_EQ(only<QDoubleSpinBox>(panel, "anNativeParamDoubleSpinBox")->value(), 1.75);
    EXPECT_FALSE(panel.control(QStringLiteral("passes"))->isEnabledTo(&panel));
    EXPECT_FALSE(panel.control(QStringLiteral("note"))->isVisibleTo(&panel));
    auto* mode = only<QComboBox>(panel, "anNativeParamComboBox");
    ASSERT_EQ(mode->count(), 2);
    EXPECT_EQ(mode->itemText(0), QStringLiteral("Slow"));
    EXPECT_EQ(mode->currentData().toString(), QStringLiteral("best")) << "the value held stays when it is an entry";
    bool labelled = false;
    for (QLabel* label : panel.findChildren<QLabel*>(QStringLiteral("anNativeParamLabel")))
    {
        labelled = labelled || label->text() == QStringLiteral("Amount");
    }
    EXPECT_TRUE(labelled);
    EXPECT_EQ(panel.banner(QStringLiteral("note_banner"))->message(), QStringLiteral("All good."));
    EXPECT_EQ(panel.actionButton(QStringLiteral("go"))->text(), QStringLiteral("Run"));
    EXPECT_EQ(panel.control(QStringLiteral("keep"))->toolTip(), QStringLiteral("Keeps it"));
    EXPECT_EQ(only<QLabel>(panel, "anNativeParamValueDescriptionLabel")->text(), QStringLiteral("1.75 - High"));

    // Refused whole at the first patch the panel cannot make, and named.
    EXPECT_FALSE(panel.applyPatches({{QStringLiteral("nowhere"), QStringLiteral("value"), 1}}, &error));
    EXPECT_NE(error.find("'nowhere'"), std::string::npos) << error;
    EXPECT_FALSE(panel.applyPatches({{QStringLiteral("mode"), QStringLiteral("choices"), QCborArray{}}}, &error));
    EXPECT_NE(error.find("no entries"), std::string::npos) << error;
    EXPECT_FALSE(panel.applyPatches({{QStringLiteral("keep"), QStringLiteral("colour"), true}}, &error));
    EXPECT_NE(error.find("does not patch"), std::string::npos) << error;
}

/// A node of a description's own form: its kind, its fields and what it holds.
QCborMap node(const char* kind, const char* id, QCborMap fields = {}, const QCborArray& children = {})
{
    fields.insert(QStringLiteral("kind"), QString::fromLatin1(kind));
    fields.insert(QStringLiteral("id"), QString::fromLatin1(id));
    if (!children.isEmpty())
    {
        fields.insert(QStringLiteral("children"), children);
    }
    return fields;
}

TEST(ParamPanel, TabsSectionsAndCollapsiblesNest)
{
    const QCborMap innerNode =
        node("section", "inner", {{QStringLiteral("presentation"), QStringLiteral("collapsible")}},
             QCborArray{node("bool", "flag")});
    const QCborMap outerNode = node("section", "outer", {{QStringLiteral("label"), QStringLiteral("Outer")}},
                                    {node("float", "amount"), innerNode});
    const QCborMap pageNode = node("tab", "first", {{QStringLiteral("label"), QStringLiteral("First")}}, {outerNode});
    const QCborMap description{{QStringLiteral("nodes"), QCborArray{node("tabs", "pages", {}, {pageNode})}}};
    ExtensionParamPanel panel;
    buildFromCbor(panel, description);

    auto* tabs = only<QTabWidget>(panel, "anNativeParamTabs");
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 1);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("First"));
    QWidget* page = tabs->widget(0);
    QWidget* flag = panel.control(QStringLiteral("flag"));
    ASSERT_NE(flag, nullptr);
    QWidget* outer = nullptr;
    QWidget* inner = nullptr;
    for (QWidget* section : panel.findChildren<QWidget*>(QStringLiteral("anNativeParamSectionContainer")))
    {
        (section->property("anSectionId").toString() == QStringLiteral("outer") ? outer : inner) = section;
    }
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);
    EXPECT_NE(dynamic_cast<an::ui::AnCardWidget*>(outer), nullptr) << "a section is a card";
    EXPECT_NE(dynamic_cast<an::ui::AnCollapsibleSection*>(inner), nullptr) << "a collapsible one folds";
    EXPECT_TRUE(page->isAncestorOf(outer));
    EXPECT_TRUE(outer->isAncestorOf(inner));
    EXPECT_TRUE(inner->isAncestorOf(flag));
    EXPECT_TRUE(outer->isAncestorOf(panel.control(QStringLiteral("amount"))));
}

TEST(ParamPanel, ConditionsFollowAPersonsChange)
{
    ExtensionParamPanel panel;
    panel.setEventDelivery(ExtensionParamPanel::EventDelivery::EachChange);
    buildFromList(panel,
                  QJsonArray{
                      QJsonObject{{"id", "mode"},
                                  {"type", "choice"},
                                  {"default", "fast"},
                                  {"options", QJsonArray{QJsonArray{"Fast", "fast"}, QJsonArray{"Best", "best"}}}},
                      QJsonObject{{"id", "detail"}, {"type", "float"}, {"visible_when", "mode == best"}},
                      QJsonObject{{"id", "go"},
                                  {"type", "action"},
                                  {"enabled_when", QJsonObject{{"param", "mode"}, {"equals", "best"}}}},
                  });
    QWidget* detailRow = panel.control(QStringLiteral("detail"))->parentWidget()->parentWidget();
    QPushButton* go = panel.actionButton(QStringLiteral("go"));
    EXPECT_TRUE(detailRow->isHidden());
    EXPECT_FALSE(go->isEnabledTo(&panel));

    only<QComboBox>(panel, "anNativeParamComboBox")->setCurrentIndex(1);
    EXPECT_FALSE(detailRow->isHidden());
    EXPECT_TRUE(go->isEnabledTo(&panel));
}

TEST(ParamPanel, AHostedKindIsTheHostsToDraw)
{
    ExtensionParamPanel panel;
    std::vector<QString> drawn;
    QPointer<QCheckBox> use;
    ExtensionPanelHost host;
    host.buildHosted = [&](const PanelNode& node, const ExtensionPanelHost::Placement& place) -> QWidget* {
        drawn.push_back(node.kind);
        if (node.kind != QLatin1String("manual_mask_editor"))
        {
            return nullptr;
        }
        auto* group = new QWidget(&panel);
        use = new QCheckBox(group);
        place()->addWidget(group);
        panel.registerHostedValue(QStringLiteral("use_mask"), QStringLiteral("bool"), use, QVariant(true));
        return group;
    };
    panel.setHost(std::move(host));
    buildFromList(panel, QJsonArray{
                             QJsonObject{{"id", "target"}, {"type", "target_selector"}},
                             QJsonObject{{"id", "mask"}, {"type", "manual_mask_editor"}},
                         });
    EXPECT_EQ(drawn, (std::vector<QString>{QStringLiteral("target_selector"), QStringLiteral("manual_mask_editor")}));
    ASSERT_NE(use, nullptr);
    EXPECT_TRUE(panel.values().contains(QStringLiteral("use_mask"))) << "the hosted control's value is the panel's";
    EXPECT_FALSE(panel.values().value(QStringLiteral("use_mask")).toBool());
    EXPECT_EQ(panel.resetToDefaults(), QStringList{QStringLiteral("use_mask")});
    EXPECT_TRUE(use->isChecked());
}

TEST(ParamPanel, TheFirstHistogramIsTheHostsAndItsResolutionIsSaid)
{
    ExtensionParamPanel panel;
    std::vector<QString> chosen;
    int made = 0;
    ExtensionPanelHost host;
    host.makeFrameHistogram = [&made](QScrollArea* scrollArea) -> HistogramWidget* {
        ++made;
        auto* histogram = new HistogramWidget(scrollArea);
        scrollArea->setWidget(histogram);
        return histogram;
    };
    host.frameHistogramResolutionChosen = [&chosen](const QString& resolution) { chosen.push_back(resolution); };
    panel.setHost(std::move(host));
    buildFromList(panel, QJsonArray{
                             QJsonObject{{"id", "first"}, {"type", "rt_histogram"}},
                             QJsonObject{{"id", "second"}, {"type", "rt_histogram"}},
                         });
    EXPECT_EQ(made, 1);
    ASSERT_EQ(panel.histograms().size(), 2u);
    EXPECT_TRUE(panel.histograms()[0].frame);
    EXPECT_FALSE(panel.histograms()[1].frame);
    ASSERT_EQ(chosen.size(), 1u) << "the preview's histogram is told its resolution as it is built";
    panel.histograms()[0].plotResolution->setCurrentIndex(0);
    EXPECT_EQ(chosen.size(), 2u);
}

// A tree of widgets is the panel's too: each widget built with its range, step and value, its value the panel's, a
// patch of it taken without a word, and a person's change said through the panel's one event.
TEST(ParamPanel, EachWidgetOfATreeBuildsItsControl)
{
    const QCborArray options{QCborValue(QCborArray{QStringLiteral("Fast"), QStringLiteral("fast")}),
                             QStringLiteral("slow")};
    const QCborMap root{
        {QStringLiteral("widget"), QStringLiteral("column")},
        {QStringLiteral("children"),
         QCborArray{
             QCborMap{{QStringLiteral("widget"), QStringLiteral("spin_double")},
                      {QStringLiteral("id"), QStringLiteral("gain")},
                      {QStringLiteral("min"), -1.0},
                      {QStringLiteral("max"), 4.0},
                      {QStringLiteral("step"), 0.25},
                      {QStringLiteral("value"), 1.5}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("spin_int")},
                      {QStringLiteral("id"), QStringLiteral("count")},
                      {QStringLiteral("min"), 2},
                      {QStringLiteral("max"), 8},
                      {QStringLiteral("value"), 5}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("checkbox")},
                      {QStringLiteral("id"), QStringLiteral("gpu")},
                      {QStringLiteral("value"), true}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("combo")},
                      {QStringLiteral("id"), QStringLiteral("mode")},
                      {QStringLiteral("options"), options},
                      {QStringLiteral("value"), QStringLiteral("slow")}},
             QCborMap{{QStringLiteral("widget"), QStringLiteral("line_edit")},
                      {QStringLiteral("id"), QStringLiteral("name")},
                      {QStringLiteral("value"), QStringLiteral("M31")}},
         }},
    };
    ExtensionPanelDescription description;
    std::string error;
    ASSERT_TRUE(an::ui::extensions::panelDescriptionFromWidgetTree(root, &description, &error)) << error;
    ExtensionParamPanel panel;
    ASSERT_TRUE(panel.setDescription(description, &error)) << error;
    Listener listener(panel);

    auto* gain =
        qobject_cast<QDoubleSpinBox*>(panel.treeWidget(QStringLiteral("gain"))
                                          ->findChild<QDoubleSpinBox*>(QStringLiteral("anExtensionUiDoubleSpinBox")));
    ASSERT_NE(gain, nullptr);
    EXPECT_DOUBLE_EQ(gain->minimum(), -1.0);
    EXPECT_DOUBLE_EQ(gain->maximum(), 4.0);
    EXPECT_DOUBLE_EQ(gain->singleStep(), 0.25);
    EXPECT_DOUBLE_EQ(gain->value(), 1.5);
    auto* count =
        panel.treeWidget(QStringLiteral("count"))->findChild<QSpinBox*>(QStringLiteral("anExtensionUiSpinBox"));
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->minimum(), 2);
    EXPECT_EQ(count->maximum(), 8);
    EXPECT_EQ(count->value(), 5);
    EXPECT_EQ(panel.treeWidgetIds(),
              (QStringList{QStringLiteral("gain"), QStringLiteral("count"), QStringLiteral("gpu"),
                           QStringLiteral("mode"), QStringLiteral("name")}));

    const QCborMap values = panel.values();
    EXPECT_DOUBLE_EQ(values.value(QStringLiteral("gain")).toDouble(), 1.5);
    EXPECT_EQ(values.value(QStringLiteral("count")).toInteger(), 5);
    EXPECT_TRUE(values.value(QStringLiteral("gpu")).toBool());
    EXPECT_EQ(values.value(QStringLiteral("mode")).toString(), QStringLiteral("slow"));
    EXPECT_EQ(values.value(QStringLiteral("name")).toString(), QStringLiteral("M31"));

    ASSERT_TRUE(panel.applyPatches({{QStringLiteral("count"), QStringLiteral("value"), 7},
                                    {QStringLiteral("gpu"), QStringLiteral("enabled"), false}},
                                   &error))
        << error;
    EXPECT_EQ(count->value(), 7);
    EXPECT_FALSE(panel.treeWidget(QStringLiteral("gpu"))->isEnabledTo(&panel));
    QCoreApplication::processEvents();
    EXPECT_TRUE(listener.said.empty()) << "a patch says nothing";

    count->setValue(3);
    QCoreApplication::processEvents();
    ASSERT_EQ(listener.said.size(), 1u);
    EXPECT_EQ(listener.said[0].id, QStringLiteral("count"));
    EXPECT_EQ(listener.said[0].name, QStringLiteral("value_changed"));
    EXPECT_EQ(listener.said[0].value.toInteger(), 3);
}

// The panel is drawn from the tokens of the look in force: its controls are a control high in each density, and a
// banner's tone bar is the tone's colour - under dark, light and a tinted preset, at every device pixel ratio.
TEST(ParamPanel, RendersFromTheTokensInEachLookAndDensity)
{
    for (const an::ui::testing::ObservatoryLook& look : an::ui::testing::observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        an::ui::testing::ObservatoryStyleScope scope(look);
        const auto& t = an::ui::tokens();

        ExtensionParamPanel panel;
        buildFromList(
            panel,
            QJsonArray{
                QJsonObject{{"id", "note"}, {"type", "info"}, {"text", "Information."}},
                QJsonObject{{"id", "group"}, {"type", "section"}, {"label", "Group"}},
                QJsonObject{{"id", "amount"}, {"type", "float"}, {"default", 0.5}},
                QJsonObject{{"id", "mode"}, {"type", "choice"}, {"options", QJsonArray{QJsonArray{"Fast", "fast"}}}},
            });
        panel.refreshTheme();
        panel.setAttribute(Qt::WA_DontShowOnScreen);
        panel.resize(520, 420);
        panel.show();
        QCoreApplication::processEvents();

        EXPECT_EQ(panel.control(QStringLiteral("amount"))->sizeHint().height(), t.metrics.control);
        EXPECT_EQ(panel.control(QStringLiteral("mode"))->sizeHint().height(), t.metrics.control);
        EXPECT_EQ(panel.layout()->spacing(), t.spacing.sm);

        an::ui::AnInfoBanner* banner = panel.banner(QStringLiteral("note"));
        ASSERT_NE(banner, nullptr);
        const QPoint at = banner->mapTo(&panel, QPoint(0, banner->height() / 2));
        for (const qreal ratio : an::ui::testing::kRenderRatios)
        {
            SCOPED_TRACE(ratio);
            const an::ui::testing::RenderedPicture picture =
                an::ui::testing::RenderedPicture::ofWidget(panel, ratio, t.layers.surface1);
            const QColor bar = picture.at(at.x() + 1.5, at.y() + 0.5);
            EXPECT_TRUE(an::ui::testing::sameColour(bar, t.colors.statusInfo))
                << "the tone's bar is " << an::ui::testing::colourName(bar) << ", not "
                << an::ui::testing::colourName(t.colors.statusInfo);
        }
    }
}

} // namespace
