// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QScrollArea>
#include <QTest>

#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "ExtensionNativeProcessWindow.h"
#include "HostNativeTestSupport.h"
#include "controls/ImageViewControl.h"
#include "widgets/CurvesWidget.h"

namespace
{

std::unique_ptr<ImageViewControl> makePreviewView(const QString& unique_name)
{
    auto view = std::make_unique<ImageViewControl>();
    const QByteArray encoded_name = unique_name.toLatin1();
    view->initialize(encoded_name.constData(), ViewControlMode::st_preview);
    return view;
}

QJsonArray jsonIntArray(const std::initializer_list<int> values)
{
    QJsonArray array;
    for (const int value : values)
    {
        array.append(value);
    }
    return array;
}

QJsonObject nativeWindowMeta(const int width, const int height, const std::initializer_list<int> target_channel_filter,
                             const bool preview_autostretch = false, const bool sub_area = false,
                             const bool sub_area_default_enabled = false, const char* sub_area_label = "",
                             const bool preview_autostretch_default = false)
{
    QJsonObject meta{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("panel_variant"), QStringLiteral("native")},
        {QStringLiteral("window_size"), QJsonArray{width, height}},
        {QStringLiteral("resizable"), true},
        {QStringLiteral("target_selector"), true},
        {QStringLiteral("target_channel_filter"), jsonIntArray(target_channel_filter)},
    };
    if (preview_autostretch)
    {
        meta.insert(QStringLiteral("preview_autostretch"), true);
    }
    if (preview_autostretch_default)
    {
        meta.insert(QStringLiteral("preview_autostretch_default"), true);
    }
    if (sub_area)
    {
        meta.insert(QStringLiteral("sub_area"), true);
        meta.insert(QStringLiteral("sub_area_default_enabled"), sub_area_default_enabled);
        meta.insert(QStringLiteral("sub_area_size"), QJsonArray{800, 600});
        meta.insert(QStringLiteral("sub_area_label"), QString::fromLatin1(sub_area_label));
    }
    return meta;
}

QJsonObject infoParam(const char* id, const char* text)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("info")},
        {QStringLiteral("text"), QString::fromLatin1(text)},
    };
}

QJsonObject sectionParam(const char* id, const char* label)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("section")},
        {QStringLiteral("label"), QString::fromLatin1(label)},
    };
}

QJsonObject floatParam(const char* id, const char* label, const double default_value, const double minimum,
                       const double maximum, const double step)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("float")},
        {QStringLiteral("label"), QString::fromLatin1(label)},
        {QStringLiteral("default"), default_value},
        {QStringLiteral("min"), minimum},
        {QStringLiteral("max"), maximum},
        {QStringLiteral("step"), step},
    };
}

QJsonObject boolParam(const char* id, const char* label, const bool default_value)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("bool")},
        {QStringLiteral("label"), QString::fromLatin1(label)},
        {QStringLiteral("default"), default_value},
    };
}

QJsonArray choiceOptions(const std::initializer_list<std::pair<const char*, const char*>> options)
{
    QJsonArray array;
    for (const auto& [label, value] : options)
    {
        array.append(QJsonArray{QString::fromLatin1(label), QString::fromLatin1(value)});
    }
    return array;
}

QJsonObject choiceParam(const char* id, const char* label, const char* default_value,
                        const std::initializer_list<std::pair<const char*, const char*>> options)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("choice")},
        {QStringLiteral("label"), QString::fromLatin1(label)},
        {QStringLiteral("default"), QString::fromLatin1(default_value)},
        {QStringLiteral("options"), choiceOptions(options)},
    };
}

QJsonObject curveEditorParam(const char* id, const char* label)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("curve_editor")},
        {QStringLiteral("label"), QString::fromLatin1(label)},
        {QStringLiteral("default"), QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{0.5, 0.5}, QJsonArray{1.0, 1.0}}},
        {QStringLiteral("interpolation"), QStringLiteral("akima")},
        {QStringLiteral("histogram"), true},
        {QStringLiteral("grid"), true},
        {QStringLiteral("guide"), true},
    };
}

QJsonObject buttonParam(const char* id, const char* label)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("button")},
        {QStringLiteral("label"), QString::fromLatin1(label)},
    };
}

QJsonObject percentageMixLabelParam(const char* id, const char* source, const char* text)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("type"), QStringLiteral("percentage_mix_label")},
        {QStringLiteral("source"), QString::fromLatin1(source)},
        {QStringLiteral("left_label"), QStringLiteral("Ha")},
        {QStringLiteral("right_label"), QStringLiteral("OIII")},
        {QStringLiteral("text"), QString::fromLatin1(text)},
    };
}

struct NativeParamExpectation
{
    QString id;
    QString type;
    QCborValue default_value;
    QCborValue edited_value;
};

struct VeraLuxNativeSchemaFixture
{
    QString display_name;
    QJsonArray defs;
    std::vector<NativeParamExpectation> params;
};

NativeParamExpectation floatExpectation(const char* id, const double default_value, const double edited_value)
{
    return {
        QString::fromLatin1(id),
        QStringLiteral("float"),
        QCborValue(default_value),
        QCborValue(edited_value),
    };
}

NativeParamExpectation boolExpectation(const char* id, const bool default_value, const bool edited_value)
{
    return {
        QString::fromLatin1(id),
        QStringLiteral("bool"),
        QCborValue(default_value),
        QCborValue(edited_value),
    };
}

NativeParamExpectation choiceExpectation(const char* id, const char* default_value, const char* edited_value)
{
    return {
        QString::fromLatin1(id),
        QStringLiteral("choice"),
        QCborValue(QString::fromLatin1(default_value)),
        QCborValue(QString::fromLatin1(edited_value)),
    };
}

NativeParamExpectation curveExpectation(const char* id)
{
    return {
        QString::fromLatin1(id),
        QStringLiteral("curve_editor"),
        QCborArray{QCborArray{0.0, 0.0}, QCborArray{0.5, 0.5}, QCborArray{1.0, 1.0}},
        QCborArray{QCborArray{0.0, 0.0}, QCborArray{0.40, 0.62}, QCborArray{1.0, 1.0}},
    };
}

template <typename WidgetT>
WidgetT* findNativeParamWidget(QWidget& root, const QString& object_name, const QString& param_id)
{
    const auto widgets = root.findChildren<WidgetT*>(object_name);
    for (WidgetT* widget : widgets)
    {
        if (widget && widget->property("anParamId").toString() == param_id)
        {
            return widget;
        }
    }
    return nullptr;
}

CurvesWidget* findNativeCurveEditor(QWidget& root, const QString& param_id)
{
    const auto widgets = root.findChildren<QWidget*>(QStringLiteral("anNativeParamCurveEditor"));
    for (QWidget* widget : widgets)
    {
        auto* curve_editor = dynamic_cast<CurvesWidget*>(widget);
        if (curve_editor && curve_editor->property("anParamId").toString() == param_id)
        {
            return curve_editor;
        }
    }
    return nullptr;
}

void expectParamValue(const QCborMap& params, const NativeParamExpectation& expectation, const QCborValue& expected,
                      const QString& display_name)
{
    ASSERT_TRUE(params.contains(expectation.id))
        << display_name.toStdString() << " missing param " << expectation.id.toStdString();

    const QCborValue actual = params.value(expectation.id);
    if (expectation.type == QStringLiteral("float"))
    {
        EXPECT_NEAR(actual.toDouble(), expected.toDouble(), 1.0e-9)
            << display_name.toStdString() << " param " << expectation.id.toStdString();
        return;
    }
    if (expectation.type == QStringLiteral("bool"))
    {
        EXPECT_EQ(actual.toBool(), expected.toBool())
            << display_name.toStdString() << " param " << expectation.id.toStdString();
        return;
    }
    if (expectation.type == QStringLiteral("choice"))
    {
        EXPECT_EQ(actual.toString(), expected.toString())
            << display_name.toStdString() << " param " << expectation.id.toStdString();
        return;
    }
    if (expectation.type == QStringLiteral("curve_editor"))
    {
        const QCborArray actual_points = actual.toArray();
        const QCborArray expected_points = expected.toArray();
        ASSERT_EQ(actual_points.size(), expected_points.size())
            << display_name.toStdString() << " param " << expectation.id.toStdString();
        for (qsizetype i = 0; i < expected_points.size(); ++i)
        {
            const QCborArray actual_point = actual_points.at(i).toArray();
            const QCborArray expected_point = expected_points.at(i).toArray();
            ASSERT_EQ(actual_point.size(), 2);
            ASSERT_EQ(expected_point.size(), 2);
            EXPECT_NEAR(actual_point.at(0).toDouble(), expected_point.at(0).toDouble(), 1.0e-9)
                << display_name.toStdString() << " param " << expectation.id.toStdString();
            EXPECT_NEAR(actual_point.at(1).toDouble(), expected_point.at(1).toDouble(), 1.0e-9)
                << display_name.toStdString() << " param " << expectation.id.toStdString();
        }
        return;
    }

    FAIL() << "Unsupported expectation type: " << expectation.type.toStdString();
}

void applyParamEdit(an::ui::extensions::ExtensionNativeProcessWindow& window, const NativeParamExpectation& expectation)
{
    if (expectation.type == QStringLiteral("float"))
    {
        auto* spin =
            findNativeParamWidget<QDoubleSpinBox>(window, QStringLiteral("anNativeParamDoubleSpinBox"), expectation.id);
        ASSERT_NE(spin, nullptr) << expectation.id.toStdString();
        spin->setValue(expectation.edited_value.toDouble());
        return;
    }
    if (expectation.type == QStringLiteral("bool"))
    {
        auto* checkbox =
            findNativeParamWidget<QCheckBox>(window, QStringLiteral("anNativeParamCheckBox"), expectation.id);
        ASSERT_NE(checkbox, nullptr) << expectation.id.toStdString();
        checkbox->setChecked(expectation.edited_value.toBool());
        return;
    }
    if (expectation.type == QStringLiteral("choice"))
    {
        auto* combo = findNativeParamWidget<QComboBox>(window, QStringLiteral("anNativeParamComboBox"), expectation.id);
        ASSERT_NE(combo, nullptr) << expectation.id.toStdString();
        const QString edited_value = expectation.edited_value.toString();
        const int index = combo->findData(edited_value);
        ASSERT_NE(index, -1) << expectation.id.toStdString();
        combo->setCurrentIndex(index);
        return;
    }
    if (expectation.type == QStringLiteral("curve_editor"))
    {
        auto* curve_editor = findNativeCurveEditor(window, expectation.id);
        ASSERT_NE(curve_editor, nullptr) << expectation.id.toStdString();
        curve_editor->setCurve(
            an::ui::ToneCurveModel({{0.0, 0.0}, {0.40, 0.62}, {1.0, 1.0}}, an::CurveInterpolation::Akima));
        return;
    }

    FAIL() << "Unsupported expectation type: " << expectation.type.toStdString();
}

std::vector<VeraLuxNativeSchemaFixture> makeVeraLuxNativeSchemaFixtures()
{
    return {
        {
            QStringLiteral("VeraLux Revela"),
            QJsonArray{
                nativeWindowMeta(1260, 760, {1, 3}),
                infoParam("attribution", "AfterNight port of VeraLux Revela."),
                sectionParam("enhancement", "Enhancement"),
                floatParam("texture", "Texture", 0.0, 0.0, 1.0, 0.01),
                floatParam("structure", "Structure", 0.0, 0.0, 1.0, 0.01),
                sectionParam("protection", "Protection"),
                floatParam("shadow_authority", "Shadow Authority", 33.0, 0.0, 100.0, 1.0),
                boolParam("protect_stars", "Isolate Stars", true),
                boolParam("show_mask", "Render Protection Mask", false),
            },
            {
                floatExpectation("texture", 0.0, 0.37),
                floatExpectation("structure", 0.0, 0.52),
                floatExpectation("shadow_authority", 33.0, 48.0),
                boolExpectation("protect_stars", true, false),
                boolExpectation("show_mask", false, true),
            },
        },
        {
            QStringLiteral("VeraLux Silentium"),
            QJsonArray{
                nativeWindowMeta(1260, 760, {1, 3}, true, true, true, "Preview: Silentium", true),
                infoParam("attribution", "AfterNight port of VeraLux Silentium."),
                sectionParam("silentium_core", "Silentium Core"),
                floatParam("noise_intensity", "Noise Intensity (Log S)", 0.5, 0.0, 2.0, 0.01),
                boolParam("adaptive_noise", "Adaptive Noise Model", true),
                floatParam("detail_guard", "Detail Guard", 50.0, 0.0, 100.0, 1.0),
                sectionParam("chroma", "Chrominance (Color Noise)"),
                boolParam("enable_chroma", "Enable Chroma Denoise (LAB)", true),
                floatParam("chroma_strength", "Chroma Strength", 30.0, 0.0, 100.0, 1.0),
                sectionParam("deep_space", "Deep Space Smoothness (Shadows)"),
                floatParam("shadow_smoothness", "Shadow Smoothness", 10.0, 0.0, 100.0, 1.0),
                sectionParam("star_field", "Star Field Handling"),
                boolParam("use_stars", "Use Star Protection", true),
                boolParam("auto_starless", "Auto Starless Detection", true),
                infoParam("upstream_version", "Ported from VeraLux Silentium 1.0.3."),
            },
            {
                floatExpectation("noise_intensity", 0.5, 1.4),
                boolExpectation("adaptive_noise", true, false),
                floatExpectation("detail_guard", 50.0, 61.0),
                boolExpectation("enable_chroma", true, false),
                floatExpectation("chroma_strength", 30.0, 45.0),
                floatExpectation("shadow_smoothness", 10.0, 24.0),
                boolExpectation("use_stars", true, false),
                boolExpectation("auto_starless", true, false),
            },
        },
        {
            QStringLiteral("VeraLux Alchemy"),
            QJsonArray{
                nativeWindowMeta(1260, 760, {3}, true),
                infoParam("attribution", "AfterNight port of VeraLux Alchemy."),
                sectionParam("sensor_profile_section", "Sensor Profile"),
                choiceParam("sensor_profile", "Sensor", "Generic OSC",
                            {{"Generic OSC", "Generic OSC"}, {"Sony IMX571", "Sony IMX571"}}),
                boolParam("quantum_unmix", "Quantum Unmixing", false),
                sectionParam("normalization", "Normalization"),
                boolParam("bg_align", "Background Neutralization", true),
                boolParam("auto_fit", "Auto Signal Fit", true),
                floatParam("boost", "OIII Boost", 1.0, 0.5, 5.0, 0.01),
                sectionParam("palette", "Palette Mixer"),
                floatParam("mix_r", "Red OIII Mix", 0.0, 0.0, 1.0, 0.01),
                percentageMixLabelParam("mix_r_readout", "mix_r", "100% Ha, 0% OIII"),
                floatParam("mix_g", "Green OIII Mix", 1.0, 0.0, 1.0, 0.01),
                percentageMixLabelParam("mix_g_readout", "mix_g", "0% Ha, 100% OIII"),
                floatParam("mix_b", "Blue OIII Mix", 1.0, 0.0, 1.0, 0.01),
                percentageMixLabelParam("mix_b_readout", "mix_b", "0% Ha, 100% OIII"),
                buttonParam("preset_hoo", "HOO"),
                buttonParam("preset_pseudo_sho", "Pseudo-SHO"),
                buttonParam("preset_hso", "HSO"),
            },
            {
                choiceExpectation("sensor_profile", "Generic OSC", "Sony IMX571"),
                boolExpectation("quantum_unmix", false, true),
                boolExpectation("bg_align", true, false),
                boolExpectation("auto_fit", true, false),
                floatExpectation("boost", 1.0, 1.75),
                floatExpectation("mix_r", 0.0, 0.25),
                floatExpectation("mix_g", 1.0, 0.72),
                floatExpectation("mix_b", 1.0, 0.81),
            },
        },
        {
            QStringLiteral("VeraLux Vectra"),
            QJsonArray{
                nativeWindowMeta(720, 760, {3}),
                infoParam("attribution", "AfterNight port of VeraLux Vectra."),
                sectionParam("primary_vectors", "Primary Vectors"),
                floatParam("red_hue", "Red Hue Shift", 0.0, -60.0, 60.0, 1.0),
                floatParam("red_saturation", "Red Saturation", 0.0, -1.0, 1.0, 0.01),
                floatParam("green_hue", "Green Hue Shift", 0.0, -60.0, 60.0, 1.0),
                floatParam("green_saturation", "Green Saturation", 0.0, -1.0, 1.0, 0.01),
                floatParam("blue_hue", "Blue Hue Shift", 0.0, -60.0, 60.0, 1.0),
                floatParam("blue_saturation", "Blue Saturation", 0.0, -1.0, 1.0, 0.01),
                sectionParam("secondary_vectors", "Secondary Vectors"),
                floatParam("cyan_hue", "Cyan Hue Shift", 0.0, -60.0, 60.0, 1.0),
                floatParam("cyan_saturation", "Cyan Saturation", 0.0, -1.0, 1.0, 0.01),
                floatParam("magenta_hue", "Magenta Hue Shift", 0.0, -60.0, 60.0, 1.0),
                floatParam("magenta_saturation", "Magenta Saturation", 0.0, -1.0, 1.0, 0.01),
                floatParam("yellow_hue", "Yellow Hue Shift", 0.0, -60.0, 60.0, 1.0),
                floatParam("yellow_saturation", "Yellow Saturation", 0.0, -1.0, 1.0, 0.01),
                sectionParam("protection", "Protection"),
                floatParam("shadow_authority", "Shadow Authority", 0.0, 0.0, 100.0, 1.0),
                boolParam("protect_stars", "White Star Integrity", true),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("vector_slope")},
                            {QStringLiteral("type"), QStringLiteral("vector_slope")},
                            {QStringLiteral("label"), QStringLiteral("Vector Slope")},
                            {QStringLiteral("height"), 220}},
            },
            {
                floatExpectation("red_hue", 0.0, 12.0),
                floatExpectation("red_saturation", 0.0, 0.18),
                floatExpectation("green_hue", 0.0, -8.0),
                floatExpectation("green_saturation", 0.0, -0.14),
                floatExpectation("blue_hue", 0.0, 10.0),
                floatExpectation("blue_saturation", 0.0, 0.22),
                floatExpectation("cyan_hue", 0.0, -11.0),
                floatExpectation("cyan_saturation", 0.0, 0.15),
                floatExpectation("magenta_hue", 0.0, 9.0),
                floatExpectation("magenta_saturation", 0.0, -0.12),
                floatExpectation("yellow_hue", 0.0, 7.0),
                floatExpectation("yellow_saturation", 0.0, 0.19),
                floatExpectation("shadow_authority", 0.0, 35.0),
                boolExpectation("protect_stars", true, false),
            },
        },
        {
            QStringLiteral("VeraLux Nox"),
            QJsonArray{
                nativeWindowMeta(700, 640, {1, 3}),
                infoParam("attribution", "AfterNight port of VeraLux Nox."),
                sectionParam("solver", "Background Model"),
                boolParam("auto_tune", "Auto Tune", true),
                boolParam("auto_mask", "Protect Signal", true),
                floatParam("stiffness", "Stiffness", 2.0, 1.0, 4.0, 0.1),
                floatParam("rejection_power", "Rejection Power", 55.0, 25.0, 72.0, 1.0),
                floatParam("model_grid", "Model Grid", 64.0, 16.0, 128.0, 1.0),
                sectionParam("correction", "Correction"),
                floatParam("correction_strength", "Correction Strength", 1.0, 0.0, 1.25, 0.05),
                boolParam("output_model", "Output Background Model", false),
                infoParam("upstream_version", "Ported from VeraLux Nox 1.0.1."),
            },
            {
                boolExpectation("auto_tune", true, false),
                boolExpectation("auto_mask", true, false),
                floatExpectation("stiffness", 2.0, 3.1),
                floatExpectation("rejection_power", 55.0, 61.0),
                floatExpectation("model_grid", 64.0, 96.0),
                floatExpectation("correction_strength", 1.0, 0.65),
                boolExpectation("output_model", false, true),
            },
        },
        {
            QStringLiteral("VeraLux HyperMetric Stretch"),
            QJsonArray{
                nativeWindowMeta(720, 680, {1, 3}),
                infoParam("attribution", "AfterNight port of VeraLux HyperMetric Stretch."),
                sectionParam("workflow", "Workflow"),
                choiceParam("processing_mode", "Mode", "ready_to_use",
                            {{"Ready to Use", "ready_to_use"}, {"Scientific", "scientific"}}),
                choiceParam("working_space", "Working Space", "Rec.709 (Recommended)",
                            {{"Rec.709 (Recommended)", "Rec.709 (Recommended)"}, {"Narrowband SHO", "Narrowband SHO"}}),
                floatParam("target_bg", "Target Background", 0.2, 0.05, 0.5, 0.01),
                boolParam("use_adaptive_anchor", "Adaptive Anchor", true),
                boolParam("auto_log_d", "Auto Log D", false),
                sectionParam("stretch", "Stretch"),
                floatParam("log_d", "Log D", 2.0, 0.0, 7.0, 0.01),
                floatParam("protect_b", "Protect b", 6.0, 0.1, 15.0, 0.1),
                floatParam("convergence_power", "Star Core Recovery", 3.5, 1.0, 10.0, 0.1),
                sectionParam("color", "Color Engine"),
                floatParam("color_strategy", "Ready Color Strategy", 0.0, -1.0, 1.0, 0.01),
                floatParam("linear_expansion", "Linear Expansion", 0.0, 0.0, 1.0, 0.01),
                floatParam("color_grip", "Color Grip", 1.0, 0.0, 1.0, 0.01),
                floatParam("shadow_convergence", "Shadow Convergence", 0.0, 0.0, 3.0, 0.1),
            },
            {
                choiceExpectation("processing_mode", "ready_to_use", "scientific"),
                choiceExpectation("working_space", "Rec.709 (Recommended)", "Narrowband SHO"),
                floatExpectation("target_bg", 0.2, 0.28),
                boolExpectation("use_adaptive_anchor", true, false),
                boolExpectation("auto_log_d", false, true),
                floatExpectation("log_d", 2.0, 3.25),
                floatExpectation("protect_b", 6.0, 7.5),
                floatExpectation("convergence_power", 3.5, 4.2),
                floatExpectation("color_strategy", 0.0, -0.35),
                floatExpectation("linear_expansion", 0.0, 0.22),
                floatExpectation("color_grip", 1.0, 0.84),
                floatExpectation("shadow_convergence", 0.0, 1.2),
            },
        },
        {
            QStringLiteral("VeraLux StarComposer"),
            QJsonArray{
                nativeWindowMeta(700, 680, {1, 3}),
                infoParam("attribution", "AfterNight port of VeraLux StarComposer."),
                infoParam("input_note", "This native slice shapes the active star mask."),
                sectionParam("sensor", "Sensor Profile"),
                choiceParam("working_space", "Working Space", "Rec.709 (Recommended)",
                            {{"Rec.709 (Recommended)", "Rec.709 (Recommended)"}, {"Narrowband HOO", "Narrowband HOO"}}),
                boolParam("use_adaptive_anchor", "Adaptive Anchor", true),
                sectionParam("stretch", "Star Stretch"),
                floatParam("log_d", "Star Intensity", 1.0, 1.0, 21.0, 0.1),
                floatParam("profile_hardness", "Profile Hardness", 50.0, 1.0, 100.0, 1.0),
                sectionParam("physics", "Hybrid Physics"),
                floatParam("color_grip", "Color Grip", 0.5, 0.0, 1.0, 0.01),
                floatParam("shadow_convergence", "Shadow Convergence", 0.0, 0.0, 3.0, 0.1),
                sectionParam("surgery", "Star Surgery"),
                floatParam("large_structure_rejection", "Large Structure Rejection", 0.0, 0.0, 1.0, 0.01),
                floatParam("star_reduction", "Star Reduction", 0.0, 0.0, 1.0, 0.01),
                floatParam("optical_healing", "Optical Healing", 0.0, 0.0, 20.0, 1.0),
            },
            {
                choiceExpectation("working_space", "Rec.709 (Recommended)", "Narrowband HOO"),
                boolExpectation("use_adaptive_anchor", true, false),
                floatExpectation("log_d", 1.0, 2.5),
                floatExpectation("profile_hardness", 50.0, 68.0),
                floatExpectation("color_grip", 0.5, 0.62),
                floatExpectation("shadow_convergence", 0.0, 0.8),
                floatExpectation("large_structure_rejection", 0.0, 0.21),
                floatExpectation("star_reduction", 0.0, 0.34),
                floatExpectation("optical_healing", 0.0, 6.0),
            },
        },
        {
            QStringLiteral("VeraLux Curves"),
            QJsonArray{
                nativeWindowMeta(1260, 760, {1, 3}),
                infoParam("attribution", "AfterNight port of VeraLux Curves."),
                sectionParam("domain_section", "Curve Domain"),
                choiceParam("domain", "Domain", "RGB/K", {{"RGB/K", "RGB/K"}, {"Luminance", "L"}}),
                sectionParam("curve_section", "Curve"),
                curveEditorParam("curve_points", "Curve"),
                sectionParam("range_section", "Luminance Range"),
                boolParam("range_enabled", "Enable Range Limiting", false),
                floatParam("lum_min", "Range Min", 0.0, 0.0, 1.0, 0.001),
                floatParam("lum_max", "Range Max", 1.0, 0.0, 1.0, 0.001),
                floatParam("feather", "Feather", 0.25, 0.0, 1.0, 0.001),
            },
            {
                choiceExpectation("domain", "RGB/K", "L"),
                curveExpectation("curve_points"),
                boolExpectation("range_enabled", false, true),
                floatExpectation("lum_min", 0.0, 0.14),
                floatExpectation("lum_max", 1.0, 0.86),
                floatExpectation("feather", 0.25, 0.31),
            },
        },
    };
}

} // namespace

TEST(VeraLuxNativeProcessParamsTest, NativeSchemasRenderRespondAndSerializeParams)
{
    const auto fixtures = makeVeraLuxNativeSchemaFixtures();
    ASSERT_EQ(fixtures.size(), 8u);

    int fixture_index = 0;
    for (const VeraLuxNativeSchemaFixture& fixture : fixtures)
    {
        ++fixture_index;
        an::ui::extensions::ExtensionNativeProcessWindow window(
            fixture.display_name.toStdString(),
            QStringLiteral("session-veralux-native-%1").arg(fixture_index).toStdString(), nullptr);

        const auto frames = std::make_shared<an::tests::host_native::PreviewFrameRecord>();
        window.setPreviewBody(an::tests::host_native::recordingPreviewBody(frames));
        const an::tests::host_native::FramesLandFirst landed(window);
        window.setParamDefs(fixture.defs);

        auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
        ASSERT_NE(param_scroll, nullptr) << fixture.display_name.toStdString();
        EXPECT_FALSE(param_scroll->isHidden()) << fixture.display_name.toStdString();

        EXPECT_GE(window.findChildren<QWidget*>(QStringLiteral("anNativeParamSectionContainer")).size(), 1)
            << fixture.display_name.toStdString();
        const QString header_description = window.property("anProcessDescription").toString();
        EXPECT_TRUE(header_description.startsWith(QStringLiteral("AfterNight port"), Qt::CaseInsensitive))
            << fixture.display_name.toStdString();
        EXPECT_TRUE(header_description.contains(fixture.display_name, Qt::CaseInsensitive))
            << fixture.display_name.toStdString();

        const QCborMap defaults = window.currentParams();
        EXPECT_EQ(defaults.size(), static_cast<qsizetype>(fixture.params.size())) << fixture.display_name.toStdString();
        EXPECT_FALSE(defaults.contains(QStringLiteral("window_meta"))) << fixture.display_name.toStdString();
        EXPECT_FALSE(defaults.contains(QStringLiteral("attribution"))) << fixture.display_name.toStdString();

        for (const NativeParamExpectation& expectation : fixture.params)
        {
            expectParamValue(defaults, expectation, expectation.default_value, fixture.display_name);
        }

        auto source_view = makePreviewView(QStringLiteral("veralux_native_source_%1").arg(fixture_index));
        an::tests::host_native::showGrey(*source_view, 12, 10, 0.5);
        window.setSourceView(source_view.get());
        ASSERT_TRUE(an::tests::host_native::waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }))
            << fixture.display_name.toStdString();
        frames->clear();

        for (const NativeParamExpectation& expectation : fixture.params)
        {
            applyParamEdit(window, expectation);
        }

        // The edits are previewed: the last frame hands the extension the parameters as they were left.
        EXPECT_TRUE(an::tests::host_native::waitUntil([&]() {
            const std::optional<an::tests::host_native::PreviewFrameRecord::Frame> last = frames->last();
            return last.has_value() && last->params == window.currentParams() && window.isProcessIdle();
        })) << fixture.display_name.toStdString();

        const QCborMap edited = window.currentParams();
        EXPECT_EQ(edited.size(), static_cast<qsizetype>(fixture.params.size())) << fixture.display_name.toStdString();
        for (const NativeParamExpectation& expectation : fixture.params)
        {
            expectParamValue(edited, expectation, expectation.edited_value, fixture.display_name);
        }

        window.setSourceView(nullptr);
    }
}
