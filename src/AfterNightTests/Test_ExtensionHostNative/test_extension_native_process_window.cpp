// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCborArray>
#include <QCborMap>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileDevice>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMouseEvent>
#include <QMargins>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTabWidget>
#include <QToolBar>
#include <QWindow>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "../fixtures/ScopedEnvVar.h"
#include "ExtensionNativeProcessWindow.h"
#include "HostInFlightExecution.h"
#include "HostNativeTestSupport.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewScrollArea.h"
#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ToneCurveModel.h"
#include "widgets/VectorSlopeWidget.h"
#include "widgets/waitingspinnerwidget.h"

namespace
{

using an::tests::host_native::assetSampleAt;
using an::tests::host_native::FramesLandFirst;
using an::tests::host_native::gatedPreviewBody;
using an::tests::host_native::greyAsset;
using an::tests::host_native::PreviewFrameGate;
using an::tests::host_native::PreviewFrameRecord;
using an::tests::host_native::recordingPreviewBody;
using an::tests::host_native::rgbAsset;
using an::tests::host_native::showGrey;
using an::tests::host_native::suiteSession;
using an::tests::host_native::viewSampleAt;
using an::tests::host_native::waitUntil;

/// The count in one bin of the first channel a histogram widget draws, or nothing while it draws none.
std::optional<std::uint64_t> firstChannelBin(const HistogramWidget& widget, std::size_t bin)
{
    const std::shared_ptr<const an::ui::HistogramDisplayData> data = widget.getHistogram();
    if (!data || !data->isValid() || bin >= data->channels.front().bins.size())
    {
        return std::nullopt;
    }
    return data->channels.front().bins[bin];
}

QLabel* findPlaceholderLabel(an::ui::extensions::ExtensionNativeProcessWindow& window)
{
    const auto labels = window.findChildren<QLabel*>();
    for (QLabel* label : labels)
    {
        if (label && label->text() == QStringLiteral("Waiting for source image from host."))
        {
            return label;
        }
    }

    return nullptr;
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

/// The parameters of a process whose preview is rendered only when its refresh action asks for it: the action sits
/// beside the display choice, and a status banner says whether the preview shown is current.
///
/// @param refresh_on_open whether the action also runs once when the window opens.
QJsonArray manualPreviewParamDefs(bool refresh_on_open = false)
{
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview_mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Display")},
            {QStringLiteral("default"), QStringLiteral("corrected")},
            {QStringLiteral("options"),
             QJsonArray{QJsonArray{QStringLiteral("Processed Image"), QStringLiteral("corrected")}}},
            {QStringLiteral("inline_actions"), QJsonArray{QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("refresh_preview")},
                                                   {QStringLiteral("type"), QStringLiteral("button")},
                                                   {QStringLiteral("label"), QStringLiteral("Update Preview")},
                                                   {QStringLiteral("preview_refresh_role"), true},
                                                   {QStringLiteral("preview_refresh_on_open"), refresh_on_open},
                                               }}},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview_status")},
            {QStringLiteral("type"), QStringLiteral("info")},
            {QStringLiteral("preview_status"), true},
            {QStringLiteral("text"), QStringLiteral("Preview update needed.")},
            {QStringLiteral("stale_text"), QStringLiteral("Preview update needed.")},
            {QStringLiteral("ready_text"), QStringLiteral("Preview is up to date.")},
            {QStringLiteral("updating_text"), QStringLiteral("Updating preview...")},
        },
    };
}

/// Replaces the image a view shows in place, as another process's Apply or an undo does, and waits until it has.
void replaceShownImage(ImageViewControl& view, an::consumer::ImageAsset&& asset)
{
    bool replaced = false;
    view.replaceImage(std::move(asset), false, [&replaced](const an::Status& status) {
        EXPECT_TRUE(status.ok()) << status.message();
        replaced = true;
    });
    ASSERT_TRUE(waitUntil([&replaced]() { return replaced; }));
}

/// The waiting spinner under a widget, or null: the spinner has no meta-object of its own, so it is told apart by its
/// type.
WaitingSpinnerWidget* findSpinner(QWidget& root)
{
    for (QWidget* widget : root.findChildren<QWidget*>())
    {
        if (auto* spinner = dynamic_cast<WaitingSpinnerWidget*>(widget))
        {
            return spinner;
        }
    }
    return nullptr;
}

std::unique_ptr<ImageViewControl> makePreviewView(const char* unique_name)
{
    auto view = std::make_unique<ImageViewControl>();
    view->initialize(unique_name, ViewControlMode::st_preview);
    return view;
}

QWidget* findParamRow(QWidget& root, const QString& id)
{
    const auto widgets = root.findChildren<QWidget*>();
    for (QWidget* widget : widgets)
    {
        if (widget && widget->property("anParamId").toString() == id && widget->layout())
        {
            return widget;
        }
    }

    return nullptr;
}

QWidget* findSectionById(QWidget& root, const QString& id)
{
    const auto widgets = root.findChildren<QWidget*>(QStringLiteral("anNativeParamSectionContainer"));
    for (QWidget* widget : widgets)
    {
        if (widget && widget->property("anSectionId").toString() == id)
        {
            return widget;
        }
    }

    return nullptr;
}

bool isDescendantOf(QWidget* child, QWidget* ancestor)
{
    for (QWidget* widget = child; widget; widget = widget->parentWidget())
    {
        if (widget == ancestor)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(ExtensionNativeProcessWindowTest, WindowMetaAppliesExplicitFixedSizing)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Extension AI Denoise", "session-fixed-sizing", nullptr);

    window.setParamDefs({
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("window_meta")},
            {QStringLiteral("type"), QStringLiteral("meta")},
            {QStringLiteral("window_size"), QJsonArray{1380, 820}},
            {QStringLiteral("fixed_size"), true},
        },
    });

    EXPECT_TRUE(window.property("anFixedSizeWindow").toBool());

    window.show();
    QTest::qWait(60);

    EXPECT_EQ(window.minimumSize(), window.maximumSize());
    EXPECT_FALSE(window.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    EXPECT_GE(window.width(), 1380);
    EXPECT_GE(window.height(), 820);
}

TEST(ExtensionNativeProcessWindowTest, WindowMetaCanDeclareResizableInitialSize)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-resizable-sizing", nullptr);

    window.setParamDefs({
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("window_meta")},
            {QStringLiteral("type"), QStringLiteral("meta")},
            {QStringLiteral("window"),
             QJsonObject{
                 {QStringLiteral("size"), QJsonArray{1280, 780}},
                 {QStringLiteral("resizable"), true},
             }},
        },
    });

    EXPECT_FALSE(window.property("anFixedSizeWindow").toBool());

    window.show();
    QTest::qWait(60);

    EXPECT_NE(window.minimumSize(), window.maximumSize());
    EXPECT_TRUE(window.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    EXPECT_GE(window.width(), 1280);
    EXPECT_GE(window.height(), 780);
}

TEST(ExtensionNativeProcessWindowTest, ParamDefsRoundTripAndUnsupportedSchemasHidePanel)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-1", nullptr);

    auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    ASSERT_NE(param_scroll, nullptr);
    EXPECT_TRUE(param_scroll->isHidden());

    QJsonArray defs;
    defs.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.05},
    });
    defs.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("iterations")},
        {QStringLiteral("type"), QStringLiteral("int")},
        {QStringLiteral("label"), QStringLiteral("Iterations")},
        {QStringLiteral("default"), 2},
        {QStringLiteral("min"), 1},
        {QStringLiteral("max"), 10},
        {QStringLiteral("step"), 1},
    });
    defs.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("preserve_background")},
        {QStringLiteral("type"), QStringLiteral("bool")},
        {QStringLiteral("label"), QStringLiteral("Preserve background")},
        {QStringLiteral("default"), false},
    });
    defs.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("mode")},
        {QStringLiteral("type"), QStringLiteral("choice")},
        {QStringLiteral("label"), QStringLiteral("Mode")},
        {QStringLiteral("default"), QStringLiteral("gentle")},
        {QStringLiteral("options"),
         QJsonArray{
             QJsonArray{QStringLiteral("Gentle"), QStringLiteral("gentle")},
             QJsonArray{QStringLiteral("Punchy"), QStringLiteral("punchy")},
         }},
    });
    defs.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("note")},
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("label"), QStringLiteral("Note")},
        {QStringLiteral("default"), QStringLiteral("initial")},
    });

    window.setParamDefs(defs);
    EXPECT_FALSE(param_scroll->isHidden());

    auto* strength_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    auto* iterations_spin = window.findChild<QSpinBox*>(QStringLiteral("anNativeParamSpinBox"));
    auto* preserve_background = window.findChild<QCheckBox*>(QStringLiteral("anNativeParamCheckBox"));
    auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("anNativeParamComboBox"));
    auto* note_edit = window.findChild<QLineEdit*>(QStringLiteral("anNativeParamLineEdit"));

    ASSERT_NE(strength_spin, nullptr);
    ASSERT_NE(iterations_spin, nullptr);
    ASSERT_NE(preserve_background, nullptr);
    ASSERT_NE(mode_combo, nullptr);
    ASSERT_NE(note_edit, nullptr);

    strength_spin->setValue(0.75);
    iterations_spin->setValue(4);
    preserve_background->setChecked(true);
    mode_combo->setCurrentIndex(1);
    note_edit->setText(QStringLiteral("preview"));

    const QCborMap params = window.currentParams();
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("strength")).toDouble(), 0.75);
    EXPECT_EQ(params.value(QStringLiteral("iterations")).toInteger(), 4);
    EXPECT_TRUE(params.value(QStringLiteral("preserve_background")).toBool());
    EXPECT_EQ(params.value(QStringLiteral("mode")).toString(), QStringLiteral("punchy"));
    EXPECT_EQ(params.value(QStringLiteral("note")).toString(), QStringLiteral("preview"));

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("unsupported")},
        {QStringLiteral("type"), QStringLiteral("color")},
    }});
    EXPECT_TRUE(param_scroll->isHidden());
    EXPECT_TRUE(window.currentParams().isEmpty());
}

TEST(ExtensionNativeProcessWindowTest, SliderParamDefsCanDisableTrackingForPreviewUpdates)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-slider-tracking", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.25},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("step"), 0.05},
            {QStringLiteral("tracking"), false},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("iterations")},
            {QStringLiteral("type"), QStringLiteral("int")},
            {QStringLiteral("label"), QStringLiteral("Iterations")},
            {QStringLiteral("default"), 2},
            {QStringLiteral("min"), 1},
            {QStringLiteral("max"), 10},
            {QStringLiteral("step"), 1},
            {QStringLiteral("tracking"), false},
        },
    });

    const auto sliders = window.findChildren<QSlider*>();
    ASSERT_EQ(sliders.size(), 2);
    for (QSlider* slider : sliders)
    {
        ASSERT_NE(slider, nullptr);
        EXPECT_FALSE(slider->hasTracking());
    }
}

TEST(ExtensionNativeProcessWindowTest, ParamActionButtonInvokesCallbackAndUpdatesParameter)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-param-action", nullptr);

    int action_count = 0;
    window.setParamActionCallback([&action_count](const std::string& action_id, const QCborMap& params) {
        ++action_count;
        EXPECT_EQ(action_id, "auto_log_d");
        EXPECT_DOUBLE_EQ(params.value(QStringLiteral("log_d")).toDouble(), 2.0);
        EXPECT_FALSE(params.contains(QStringLiteral("auto_log_d")));
        return QCborMap{{QStringLiteral("log_d"), 3.25}};
    });

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("stretch")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("HyperMetric Stretch")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("auto_log_d")},
            {QStringLiteral("type"), QStringLiteral("button")},
            {QStringLiteral("label"), QStringLiteral("Auto-Calc Log D")},
            {QStringLiteral("button_role"), QStringLiteral("primary")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("log_d")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Log D")},
            {QStringLiteral("default"), 2.0},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 7.0},
            {QStringLiteral("step"), 0.01},
        },
    });

    auto* action_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    auto* log_d_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    ASSERT_NE(action_button, nullptr);
    ASSERT_NE(log_d_spin, nullptr);
    EXPECT_EQ(action_button->text(), QStringLiteral("Auto-Calc Log D"));
    EXPECT_EQ(action_button->property("anProcessButtonRole").toString(), QStringLiteral("primary"));

    action_button->click();

    EXPECT_EQ(action_count, 1);
    EXPECT_DOUBLE_EQ(log_d_spin->value(), 3.25);
    EXPECT_DOUBLE_EQ(window.currentParams().value(QStringLiteral("log_d")).toDouble(), 3.25);
}

TEST(ExtensionNativeProcessWindowTest, PercentageMixLabelTracksSliderAndParamActionUpdates)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-percentage-mix", nullptr);

    window.setParamActionCallback([](const std::string& action_id, const QCborMap& params) {
        EXPECT_EQ(action_id, "preset_pseudo_sho");
        EXPECT_DOUBLE_EQ(params.value(QStringLiteral("mix_g")).toDouble(), 0.25);
        return QCborMap{{QStringLiteral("mix_g"), 0.5}};
    });

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("palette")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Palette Mixer")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preset_pseudo_sho")},
            {QStringLiteral("type"), QStringLiteral("button")},
            {QStringLiteral("label"), QStringLiteral("Pseudo-SHO")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("mix_g")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Green OIII Mix")},
            {QStringLiteral("default"), 1.0},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("step"), 0.01},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("mix_g_readout")},
            {QStringLiteral("type"), QStringLiteral("percentage_mix_label")},
            {QStringLiteral("source"), QStringLiteral("mix_g")},
            {QStringLiteral("left_label"), QStringLiteral("Ha")},
            {QStringLiteral("right_label"), QStringLiteral("OIII")},
            {QStringLiteral("text"), QStringLiteral("0% Ha, 100% OIII")},
        },
    });

    auto* mix_label = window.findChild<QLabel*>(QStringLiteral("anNativeParamPercentageMixLabel"));
    auto* mix_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    auto* preset_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    ASSERT_NE(mix_label, nullptr);
    ASSERT_NE(mix_spin, nullptr);
    ASSERT_NE(preset_button, nullptr);

    EXPECT_EQ(mix_label->text(), QStringLiteral("0% Ha, 100% OIII"));

    mix_spin->setValue(0.25);
    QCoreApplication::processEvents();
    EXPECT_EQ(mix_label->text(), QStringLiteral("75% Ha, 25% OIII"));

    preset_button->click();
    EXPECT_DOUBLE_EQ(window.currentParams().value(QStringLiteral("mix_g")).toDouble(), 0.5);
    EXPECT_EQ(mix_label->text(), QStringLiteral("50% Ha, 50% OIII"));
}

TEST(ExtensionNativeProcessWindowTest, ValueDescriptionLabelTracksNumericParams)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-value-description", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("physics")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Physics")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("aggression")},
            {QStringLiteral("type"), QStringLiteral("int")},
            {QStringLiteral("label"), QStringLiteral("Aggression")},
            {QStringLiteral("default"), 50},
            {QStringLiteral("min"), 0},
            {QStringLiteral("max"), 100},
            {QStringLiteral("step"), 1},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("aggression_readout")},
            {QStringLiteral("type"), QStringLiteral("value_description_label")},
            {QStringLiteral("source"), QStringLiteral("aggression")},
            {QStringLiteral("decimals"), 0},
            {QStringLiteral("suffix"), QStringLiteral("%")},
            {QStringLiteral("text"), QStringLiteral("50% - Balanced")},
            {QStringLiteral("ranges"),
             QJsonArray{
                 QJsonArray{0.0, 29.999, QStringLiteral("Risky")},
                 QJsonArray{30.0, 70.0, QStringLiteral("Balanced")},
                 QJsonArray{70.001, 100.0, QStringLiteral("Safe")},
             }},
        },
    });

    auto* readout = window.findChild<QLabel*>(QStringLiteral("anNativeParamValueDescriptionLabel"));
    auto* spin = window.findChild<QSpinBox*>(QStringLiteral("anNativeParamSpinBox"));
    ASSERT_NE(readout, nullptr);
    ASSERT_NE(spin, nullptr);

    EXPECT_EQ(readout->text(), QStringLiteral("50% - Balanced"));

    spin->setValue(75);
    QCoreApplication::processEvents();
    EXPECT_EQ(readout->text(), QStringLiteral("75% - Safe"));
}

TEST(ExtensionNativeProcessWindowTest, EmptyParamActionSchedulesPreview)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-empty-action-preview", nullptr);

    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamActionCallback([](const std::string& action_id, const QCborMap&) {
        EXPECT_EQ(action_id, "refresh_preview");
        return QCborMap{};
    });
    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("refresh_preview")},
            {QStringLiteral("type"), QStringLiteral("button")},
            {QStringLiteral("label"), QStringLiteral("Update Preview")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.5},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("step"), 0.1},
        },
    });

    auto source_view = makePreviewView("native_empty_action_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    frames->clear();

    auto* action_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    ASSERT_NE(action_button, nullptr);
    action_button->click();

    EXPECT_TRUE(waitUntil([&]() { return frames->count() >= 1; }))
        << "an action that changes nothing still asks for a frame";
}

TEST(ExtensionNativeProcessWindowTest, ParamRowsPlaceLabelsBesideControls)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-param-row-grid", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("processing_mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Processing Mode")},
            {QStringLiteral("default"), QStringLiteral("ready")},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonArray{QStringLiteral("Ready"), QStringLiteral("ready")},
                 QJsonArray{QStringLiteral("Scientific"), QStringLiteral("scientific")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("target_bg")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Target Background")},
            {QStringLiteral("default"), 0.20},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("use_adaptive_anchor")},
            {QStringLiteral("type"), QStringLiteral("bool")},
            {QStringLiteral("label"), QStringLiteral("Adaptive Anchor")},
            {QStringLiteral("default"), true},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("auto_calculate")},
            {QStringLiteral("type"), QStringLiteral("button")},
            {QStringLiteral("label"), QStringLiteral("Auto Calculate")},
        },
    });

    for (const auto& [param_id, label_text] : {
             std::pair{QStringLiteral("processing_mode"), QStringLiteral("Processing Mode")},
             std::pair{QStringLiteral("target_bg"), QStringLiteral("Target Background")},
             std::pair{QStringLiteral("use_adaptive_anchor"), QStringLiteral("Adaptive Anchor")},
         })
    {
        QWidget* row = findParamRow(window, param_id);
        ASSERT_NE(row, nullptr) << param_id.toStdString();
        auto* grid = qobject_cast<QGridLayout*>(row->layout());
        ASSERT_NE(grid, nullptr) << param_id.toStdString();
        ASSERT_NE(grid->itemAtPosition(0, 0), nullptr) << param_id.toStdString();
        ASSERT_NE(grid->itemAtPosition(0, 1), nullptr) << param_id.toStdString();
        EXPECT_GE(grid->columnMinimumWidth(0), 160) << param_id.toStdString();

        auto* label = qobject_cast<QLabel*>(grid->itemAtPosition(0, 0)->widget());
        ASSERT_NE(label, nullptr) << param_id.toStdString();
        EXPECT_EQ(label->text(), label_text);
        EXPECT_EQ(label->minimumWidth(), label->maximumWidth()) << param_id.toStdString();
        EXPECT_EQ(label->minimumWidth(), grid->columnMinimumWidth(0)) << param_id.toStdString();
    }

    QWidget* action_row = findParamRow(window, QStringLiteral("auto_calculate"));
    ASSERT_NE(action_row, nullptr);
    auto* action_grid = qobject_cast<QGridLayout*>(action_row->layout());
    ASSERT_NE(action_grid, nullptr);
    EXPECT_EQ(action_grid->itemAtPosition(0, 0), nullptr);
    ASSERT_NE(action_grid->itemAtPosition(0, 1), nullptr);
    EXPECT_GE(action_grid->columnMinimumWidth(0), 160);
    auto* action_button = qobject_cast<QPushButton*>(action_grid->itemAtPosition(0, 1)->widget());
    ASSERT_NE(action_button, nullptr);
    EXPECT_EQ(action_button->text(), QStringLiteral("Auto Calculate"));
}

TEST(ExtensionNativeProcessWindowTest, EnabledWhenTogglesSectionContainers)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-enabled-when", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("processing_mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Processing Mode")},
            {QStringLiteral("default"), QStringLiteral("ready_to_use")},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonArray{QStringLiteral("Ready to Use"), QStringLiteral("ready_to_use")},
                 QJsonArray{QStringLiteral("Scientific"), QStringLiteral("scientific")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("color")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Ready Mode Color")},
            {QStringLiteral("enabled_when"),
             QJsonObject{
                 {QStringLiteral("param"), QStringLiteral("processing_mode")},
                 {QStringLiteral("equals"), QStringLiteral("ready_to_use")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("color_strategy")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Color Strategy")},
            {QStringLiteral("default"), 0.0},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("scientific_color")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Scientific Color Reconstruction")},
            {QStringLiteral("enabled_when"),
             QJsonObject{
                 {QStringLiteral("param"), QStringLiteral("processing_mode")},
                 {QStringLiteral("equals"), QStringLiteral("scientific")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("linear_expansion")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Linear Expansion")},
            {QStringLiteral("default"), 0.0},
        },
    });

    auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("anNativeParamComboBox"));
    QWidget* ready_section = findSectionById(window, QStringLiteral("color"));
    QWidget* scientific_section = findSectionById(window, QStringLiteral("scientific_color"));
    QWidget* linear_expansion_row = findParamRow(window, QStringLiteral("linear_expansion"));
    ASSERT_NE(mode_combo, nullptr);
    ASSERT_NE(ready_section, nullptr);
    ASSERT_NE(scientific_section, nullptr);
    ASSERT_NE(linear_expansion_row, nullptr);

    EXPECT_TRUE(ready_section->isEnabled());
    EXPECT_FALSE(scientific_section->isEnabled());
    EXPECT_FALSE(linear_expansion_row->isEnabled());

    window.setExecutionInProgress(true);
    window.setExecutionInProgress(false);

    mode_combo->setCurrentIndex(1);
    QCoreApplication::processEvents();

    EXPECT_FALSE(ready_section->isEnabled());
    EXPECT_TRUE(scientific_section->isEnabled());
    EXPECT_TRUE(linear_expansion_row->isEnabled());
}

TEST(ExtensionNativeProcessWindowTest, StringVisibleWhenAndEnabledWhenUpdateNativeWidgets)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-string-conditions", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("processing_mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Processing Mode")},
            {QStringLiteral("default"), QStringLiteral("basic")},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonArray{QStringLiteral("Basic"), QStringLiteral("basic")},
                 QJsonArray{QStringLiteral("Advanced"), QStringLiteral("advanced")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("advanced_strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Advanced Strength")},
            {QStringLiteral("default"), 0.5},
            {QStringLiteral("visible_when"), QStringLiteral("processing_mode == 'advanced'")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("advanced_only")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Advanced Only")},
            {QStringLiteral("default"), 0.2},
            {QStringLiteral("enabled_when"), QStringLiteral("processing_mode == 'advanced'")},
        },
    });
    window.show();
    QCoreApplication::processEvents();

    auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("anNativeParamComboBox"));
    QWidget* strength_row = findParamRow(window, QStringLiteral("advanced_strength"));
    QWidget* enabled_row = findParamRow(window, QStringLiteral("advanced_only"));
    ASSERT_NE(mode_combo, nullptr);
    ASSERT_NE(strength_row, nullptr);
    ASSERT_NE(enabled_row, nullptr);

    EXPECT_FALSE(strength_row->isVisible());
    EXPECT_FALSE(enabled_row->isEnabled());

    mode_combo->setCurrentIndex(1);
    QCoreApplication::processEvents();

    EXPECT_TRUE(strength_row->isVisible());
    EXPECT_TRUE(enabled_row->isEnabled());
}

TEST(ExtensionNativeProcessWindowTest, StringEnabledWhenSupportsConjunctions)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-compound-enabled", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("correct_only")},
            {QStringLiteral("type"), QStringLiteral("bool")},
            {QStringLiteral("label"), QStringLiteral("Correct Only")},
            {QStringLiteral("default"), false},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("auto_radius")},
            {QStringLiteral("type"), QStringLiteral("bool")},
            {QStringLiteral("label"), QStringLiteral("Auto Radius")},
            {QStringLiteral("default"), true},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("nonstellar_radius")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Nonstellar Radius")},
            {QStringLiteral("default"), 0.0},
            {QStringLiteral("enabled_when"), QStringLiteral("correct_only != true && auto_radius != true")},
        },
    });
    window.show();
    QCoreApplication::processEvents();

    QWidget* correct_row = findParamRow(window, QStringLiteral("correct_only"));
    QWidget* auto_row = findParamRow(window, QStringLiteral("auto_radius"));
    QWidget* radius_row = findParamRow(window, QStringLiteral("nonstellar_radius"));
    ASSERT_NE(correct_row, nullptr);
    ASSERT_NE(auto_row, nullptr);
    ASSERT_NE(radius_row, nullptr);

    auto* correct_checkbox = correct_row->findChild<QCheckBox*>();
    auto* auto_checkbox = auto_row->findChild<QCheckBox*>();
    ASSERT_NE(correct_checkbox, nullptr);
    ASSERT_NE(auto_checkbox, nullptr);

    EXPECT_FALSE(radius_row->isEnabled());

    auto_checkbox->setChecked(false);
    QCoreApplication::processEvents();
    EXPECT_TRUE(radius_row->isEnabled());

    correct_checkbox->setChecked(true);
    QCoreApplication::processEvents();
    EXPECT_FALSE(radius_row->isEnabled());
}

TEST(ExtensionNativeProcessWindowTest, DisplayOnlyParamDefsRenderWithoutContributingValues)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-display-only", nullptr);

    auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    ASSERT_NE(param_scroll, nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("overview")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Overview")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("summary")},
            {QStringLiteral("type"), QStringLiteral("info")},
            {QStringLiteral("text"), QStringLiteral("This is a display-only helper entry for the native panel.")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.25},
        }});

    EXPECT_FALSE(param_scroll->isHidden());
    const auto& spacing = an::ui::tokens().spacing;
    ASSERT_NE(param_scroll->widget(), nullptr);
    auto* panel_layout = qobject_cast<QVBoxLayout*>(param_scroll->widget()->layout());
    ASSERT_NE(panel_layout, nullptr);
    EXPECT_EQ(panel_layout->spacing(), spacing.sm);

    int section_card_count = 0;
    int info_banner_count = 0;
    for (QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->objectName() == QStringLiteral("anNativeParamSectionContainer"))
        {
            if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget))
            {
                ++section_card_count;
                EXPECT_EQ(card->title(), QStringLiteral("Overview"));
                EXPECT_TRUE(card->property("anCardCompact").toBool());
                EXPECT_TRUE(card->property("anCardBodyPadded").toBool());
                ASSERT_NE(card->contentLayout(), nullptr);
                EXPECT_EQ(card->contentLayout()->contentsMargins(),
                          QMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm));
            }
        }
        if (widget->objectName() == QStringLiteral("anNativeParamInfoBanner"))
        {
            if (auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(widget))
            {
                ++info_banner_count;
                EXPECT_TRUE(banner->message().contains(QStringLiteral("display-only helper")));
            }
        }
    }
    EXPECT_EQ(section_card_count, 1);
    EXPECT_EQ(info_banner_count, 1);

    const QCborMap params = window.currentParams();
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("strength")).toDouble(), 0.25);
    EXPECT_FALSE(params.contains(QStringLiteral("overview")));
    EXPECT_FALSE(params.contains(QStringLiteral("summary")));
}

TEST(ExtensionNativeProcessWindowTest, ManualMaskEditorUsesExplicitEditMode)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-manual-mask", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Preview")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview_mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Display")},
            {QStringLiteral("default"), QStringLiteral("corrected")},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonArray{QStringLiteral("Processed Image"), QStringLiteral("corrected")},
                 QJsonArray{QStringLiteral("Extracted Gradient"), QStringLiteral("background_model")},
                 QJsonArray{QStringLiteral("Protection Mask"), QStringLiteral("protection_mask")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("manual_mask")},
            {QStringLiteral("type"), QStringLiteral("manual_mask_editor")},
            {QStringLiteral("label"), QStringLiteral("Manual Protection Mask")},
            {QStringLiteral("use_param"), QStringLiteral("use_manual_mask")},
            {QStringLiteral("use_label"), QStringLiteral("Use manual protection mask")},
            {QStringLiteral("use_default"), false},
            {QStringLiteral("display_param"), QStringLiteral("preview_mode")},
            {QStringLiteral("display_value"), QStringLiteral("protection_mask")},
            {QStringLiteral("brush_size"), 50},
            {QStringLiteral("min_brush_size"), 10},
            {QStringLiteral("max_brush_size"), 200},
        },
    });

    auto source_view = makePreviewView("native_manual_mask_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    auto* combo = window.findChild<QComboBox*>(QStringLiteral("anNativeParamComboBox"));
    ASSERT_NE(combo, nullptr);
    auto* editor = window.findChild<QGroupBox*>(QStringLiteral("anNativeManualMaskEditor"));
    ASSERT_NE(editor, nullptr);
    auto* use_mask = window.findChild<QCheckBox*>(QStringLiteral("anNativeManualMaskUseCheckBox"));
    ASSERT_NE(use_mask, nullptr);
    auto* edit_button = window.findChild<QPushButton*>(QStringLiteral("anNativeManualMaskEditButton"));
    ASSERT_NE(edit_button, nullptr);
    auto* cancel_button = window.findChild<QPushButton*>(QStringLiteral("anNativeManualMaskCancelButton"));
    ASSERT_NE(cancel_button, nullptr);
    auto* tool_panel = window.findChild<QWidget*>(QStringLiteral("anNativeManualMaskToolPanel"));
    ASSERT_NE(tool_panel, nullptr);
    auto* combo_model = qobject_cast<QStandardItemModel*>(combo->model());
    ASSERT_NE(combo_model, nullptr);
    ASSERT_LT(2, combo_model->rowCount());

    EXPECT_TRUE(combo->isEnabled());
    EXPECT_EQ(window.currentParams().value(QStringLiteral("preview_mode")).toString(), QStringLiteral("corrected"));
    EXPECT_FALSE(window.currentParams().contains(QStringLiteral("manual_mask")));
    EXPECT_FALSE(window.currentParams().value(QStringLiteral("use_manual_mask")).toBool());
    EXPECT_FALSE(use_mask->isEnabled());
    EXPECT_FALSE(use_mask->isChecked());
    EXPECT_FALSE(combo_model->item(2)->isEnabled());
    EXPECT_FALSE(window.hasManualMask());
    EXPECT_FALSE(window.manualMaskImage());

    edit_button->click();
    QCoreApplication::processEvents();

    EXPECT_FALSE(combo->isEnabled());
    EXPECT_EQ(edit_button->text(), QStringLiteral("Done"));
    EXPECT_FALSE(cancel_button->isHidden());
    EXPECT_FALSE(tool_panel->isHidden());
    EXPECT_TRUE(tool_panel->isEnabled());
    EXPECT_FALSE(combo_model->item(2)->isEnabled());
    EXPECT_EQ(window.currentParams().value(QStringLiteral("preview_mode")).toString(), QStringLiteral("corrected"));

    edit_button->click();
    QCoreApplication::processEvents();

    EXPECT_TRUE(combo->isEnabled());
    EXPECT_EQ(edit_button->text(), QStringLiteral("Edit Mask"));
    EXPECT_TRUE(cancel_button->isHidden());
    EXPECT_TRUE(tool_panel->isHidden());
    EXPECT_EQ(window.currentParams().value(QStringLiteral("preview_mode")).toString(), QStringLiteral("corrected"));
}

TEST(ExtensionNativeProcessWindowTest, AManualMaskIsHandedOnOnceDrawnAndUsed)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-manual-mask-drawn", nullptr);
    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("manual_mask")},
            {QStringLiteral("type"), QStringLiteral("manual_mask_editor")},
            {QStringLiteral("label"), QStringLiteral("Manual Protection Mask")},
            {QStringLiteral("use_param"), QStringLiteral("use_manual_mask")},
            {QStringLiteral("use_label"), QStringLiteral("Use manual protection mask")},
            {QStringLiteral("use_default"), false},
            {QStringLiteral("brush_size"), 10},
            {QStringLiteral("min_brush_size"), 2},
            {QStringLiteral("max_brush_size"), 200},
        },
    });

    auto source_view = makePreviewView("native_manual_mask_drawn_source");
    showGrey(*source_view, 32, 24, 0.5);
    window.setSourceView(source_view.get());
    window.show();
    QTest::qWait(60);
    source_view->zoom100();
    QCoreApplication::processEvents();

    auto* use_mask = window.findChild<QCheckBox*>(QStringLiteral("anNativeManualMaskUseCheckBox"));
    auto* edit_button = window.findChild<QPushButton*>(QStringLiteral("anNativeManualMaskEditButton"));
    ASSERT_NE(use_mask, nullptr);
    ASSERT_NE(edit_button, nullptr);
    EXPECT_FALSE(window.manualMaskImage()) << "nothing is handed on before a mask is drawn";

    edit_button->click();
    QCoreApplication::processEvents();

    // A stroke across the image's middle, through the view's viewport, as a person paints it.
    QWidget* viewport = source_view->getScrollArea()->viewport();
    const QPoint origin = source_view->getScrollArea()->widget()->pos();
    int row = 12;
    const auto send = [&](QEvent::Type type, int x, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPointF at(origin.x() + x, origin.y() + row);
        QMouseEvent event(type, at, viewport->mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &event);
    };
    const auto stroke = [&]() {
        send(QEvent::MouseButtonPress, 4, Qt::LeftButton, Qt::LeftButton);
        for (int x = 6; x <= 28; x += 2)
        {
            send(QEvent::MouseMove, x, Qt::NoButton, Qt::LeftButton);
        }
        send(QEvent::MouseButtonRelease, 28, Qt::LeftButton, Qt::NoButton);
    };
    stroke();
    ASSERT_TRUE(waitUntil([&]() { return window.hasManualMask(); })) << "the stroke lands in the mask";

    edit_button->click();
    QCoreApplication::processEvents();
    ASSERT_TRUE(use_mask->isEnabled());
    EXPECT_TRUE(use_mask->isChecked()) << "a mask just drawn is used";
    use_mask->setChecked(false);
    QCoreApplication::processEvents();
    EXPECT_FALSE(window.manualMaskImage()) << "a mask drawn and not used is not handed on";
    use_mask->setChecked(true);
    QCoreApplication::processEvents();

    const auto mask = window.manualMaskImage();
    ASSERT_TRUE(mask) << "a mask drawn and used is handed on";
    const std::optional<an::ImageDescriptor> described = mask->descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 32u);
    EXPECT_EQ(described->extent().height(), 24u);
    EXPECT_GT(assetSampleAt(*mask, 16, 12), 0.5) << "the stroke's middle is masked";
    EXPECT_NEAR(assetSampleAt(*mask, 16, 0), 0.0, 1e-6) << "the image's top row is not";
    EXPECT_EQ(window.manualMaskImage(), mask) << "the runs of one drawing share its copy, which nothing writes";

    // Another stroke is another copy: what a run was handed never changes under it.
    edit_button->click();
    QCoreApplication::processEvents();
    row = 2;
    stroke();
    edit_button->click();
    QCoreApplication::processEvents();
    const auto redrawn = window.manualMaskImage();
    ASSERT_TRUE(redrawn);
    EXPECT_NE(redrawn, mask);
    EXPECT_GT(assetSampleAt(*redrawn, 16, 0), 0.5) << "the second stroke reaches the top row";
    EXPECT_NEAR(assetSampleAt(*mask, 16, 0), 0.0, 1e-6) << "the copy handed on before it is as it was";

    window.setSourceView(nullptr);
}

// The placeholder is the window's own: a view shown in its place hides it and leaves it the window's, so it goes with
// the window rather than staying behind as a window of its own.
TEST(ExtensionNativeProcessWindowTest, AViewShownInThePlaceholdersPlaceLeavesItTheWindows)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-placeholder-owner", nullptr);
    auto source_view = makePreviewView("native_placeholder_owner_source");
    showGrey(*source_view, 32, 24, 0.5);
    window.setSourceView(source_view.get());
    QCoreApplication::processEvents();

    QLabel* placeholder = findPlaceholderLabel(window);
    ASSERT_NE(placeholder, nullptr) << "the placeholder left the window when a view took its place";
    EXPECT_TRUE(placeholder->isHidden());

    window.setSourceView(nullptr);
    QCoreApplication::processEvents();
    EXPECT_FALSE(placeholder->isHidden()) << "with no view, the placeholder shows again";
}

// The source views are the host's: one embedded while the window shows it goes back parentless when another takes its
// place or the window lets it go, and the window's end deletes neither.
TEST(ExtensionNativeProcessWindowTest, ASourceViewLetGoGoesBackToItsOwner)
{
    auto first = makePreviewView("native_owner_first_source");
    auto second = makePreviewView("native_owner_second_source");
    showGrey(*first, 16, 12, 0.5);
    showGrey(*second, 16, 12, 0.25);
    const QPointer<ImageViewControl> first_alive(first.get());
    const QPointer<ImageViewControl> second_alive(second.get());
    {
        an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-view-owner", nullptr);
        window.setSourceView(first.get());
        EXPECT_NE(first->parentWidget(), nullptr) << "a window that does not preview embeds its source";

        window.setSourceView(second.get());
        EXPECT_EQ(first->parentWidget(), nullptr) << "the view replaced goes back to its owner";
        EXPECT_NE(second->parentWidget(), nullptr);

        window.setSourceView(nullptr);
        EXPECT_EQ(second->parentWidget(), nullptr) << "the view let go goes back to its owner";

        window.setSourceView(first.get());
    }
    EXPECT_TRUE(first_alive) << "the window's end deleted the view it showed";
    EXPECT_TRUE(second_alive);
}

TEST(ExtensionNativeProcessWindowTest, AttributionInfoParamDefMovesToHeaderDescription)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-leading-info", nullptr);

    window.setParamDefs(
        QJsonArray{QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("attribution")},
                       {QStringLiteral("type"), QStringLiteral("info")},
                       {QStringLiteral("text"), QStringLiteral("Original process by an upstream author.")},
                   },
                   QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("controls")},
                       {QStringLiteral("type"), QStringLiteral("section")},
                       {QStringLiteral("label"), QStringLiteral("Controls")},
                   },
                   QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("strength")},
                       {QStringLiteral("type"), QStringLiteral("double")},
                       {QStringLiteral("label"), QStringLiteral("Strength")},
                       {QStringLiteral("default"), 0.25},
                   }});

    window.show();
    QTest::qWait(60);

    auto* header_description = window.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription"));
    ASSERT_NE(header_description, nullptr);
    EXPECT_EQ(header_description->text(), QStringLiteral("Original process by an upstream author."));
    EXPECT_TRUE(header_description->isVisible());

    int parameters_card_count = 0;
    int controls_card_count = 0;
    int info_banner_count = 0;
    for (QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->objectName() == QStringLiteral("anNativeParamSectionContainer"))
        {
            if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget))
            {
                if (card->title() == QStringLiteral("Parameters"))
                {
                    ++parameters_card_count;
                }
                if (card->title() == QStringLiteral("Controls"))
                {
                    ++controls_card_count;
                }
            }
        }
        if (widget->objectName() == QStringLiteral("anNativeParamInfoBanner"))
        {
            ++info_banner_count;
        }
    }

    EXPECT_EQ(parameters_card_count, 0);
    EXPECT_EQ(controls_card_count, 1);
    EXPECT_EQ(info_banner_count, 0);

    const QCborMap params = window.currentParams();
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("strength")).toDouble(), 0.25);
    EXPECT_FALSE(params.contains(QStringLiteral("attribution")));
}

TEST(ExtensionNativeProcessWindowTest, SourceViewEnablesExecuteAndInteractiveParamsTriggerAutoPreview)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-2", nullptr);

    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    auto* main_splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    QLabel* placeholder_label = findPlaceholderLabel(window);
    ASSERT_NE(execute_button, nullptr);
    ASSERT_NE(main_splitter, nullptr);
    ASSERT_NE(placeholder_label, nullptr);
    EXPECT_EQ(main_splitter->count(), 2);
    EXPECT_EQ(main_splitter->widget(0)->objectName(), QStringLiteral("controlsPanel"));
    EXPECT_EQ(main_splitter->widget(1)->objectName(), QStringLiteral("previewPanel"));

    int execute_count = 0;
    const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
    window.setExecuteCallback([&execute_count]() { ++execute_count; });
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.05},
    }});

    EXPECT_FALSE(execute_button->isEnabled());
    EXPECT_FALSE(placeholder_label->isHidden());
    EXPECT_EQ(window.previewView(), nullptr) << "nothing is previewed before there is an image";

    auto source_view = makePreviewView("native_source");
    showGrey(*source_view, 10, 8, 1.0);

    window.setSourceView(source_view.get());
    EXPECT_EQ(window.sourceView(), source_view.get());
    EXPECT_TRUE(execute_button->isEnabled());
    EXPECT_TRUE(placeholder_label->isHidden());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr) << "the window previews through a preview view of its own";
    EXPECT_FALSE(preview_view->isHidden());
    EXPECT_EQ(source_view->parentWidget(), nullptr) << "the source view is the host's mirror and is not reparented";
    EXPECT_NE(preview_view->parentWidget(), nullptr);
    ASSERT_NE(preview_view->getRTPreviewAreaCheckBox(), nullptr);
    EXPECT_FALSE(preview_view->getRTPreviewAreaCheckBox()->isChecked());

    // The first frame shows what the extension made of the image; the image itself is left as it was.
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && viewSampleAt(*preview_view, 0, 0) == 0.5; }));
    EXPECT_EQ(viewSampleAt(*source_view, 0, 0), 1.0);
    // The preview view is fitted to its window once its first frame is shown: the small source is scaled up.
    EXPECT_TRUE(waitUntil([&]() { return preview_view->getCurrentZoom() > 1.0f; }, 2000))
        << "zoom " << preview_view->getCurrentZoom();

    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }));
    frames->clear();
    execute_button->click();
    EXPECT_EQ(execute_count, 1);

    auto* strength_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    ASSERT_NE(strength_spin, nullptr);
    strength_spin->setValue(0.55);

    // A changed parameter asks for a frame, which hands the extension the parameters the window holds.
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    const std::optional<PreviewFrameRecord::Frame> last = frames->last();
    ASSERT_TRUE(last.has_value());
    EXPECT_DOUBLE_EQ(last->params.value(QStringLiteral("strength")).toDouble(), 0.55);

    window.setSourceView(nullptr);
    EXPECT_EQ(window.sourceView(), nullptr);
    EXPECT_EQ(window.previewView(), nullptr) << "the preview goes with the image";
    EXPECT_FALSE(execute_button->isEnabled());
    EXPECT_FALSE(placeholder_label->isHidden());
}

TEST(ExtensionNativeProcessWindowTest, ExecutionInProgressDisablesControlsExceptClose)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-execution-lock", nullptr);
    window.setKeepOpenMode(true);

    int execute_count = 0;
    int close_count = 0;
    int cancel_count = 0;
    window.setExecuteCallback([&execute_count]() { ++execute_count; });
    window.setCloseCallback([&close_count]() { ++close_count; });
    window.setCancelCallback([&cancel_count]() { ++cancel_count; });
    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.25},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("step"), 0.05},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("enabled")},
            {QStringLiteral("type"), QStringLiteral("bool")},
            {QStringLiteral("label"), QStringLiteral("Enabled")},
            {QStringLiteral("default"), true},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Mode")},
            {QStringLiteral("default"), QStringLiteral("a")},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonArray{QStringLiteral("A"), QStringLiteral("a")},
                 QJsonArray{QStringLiteral("B"), QStringLiteral("b")},
             }},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("note")},
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("label"), QStringLiteral("Note")},
            {QStringLiteral("default"), QStringLiteral("ready")},
        },
    });

    auto source_view = makePreviewView("execution_lock_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    auto* close_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessCloseButton"));
    auto* action_bar = window.findChild<QWidget*>(QStringLiteral("anNativeProcessActionBar"));
    auto* button_box = window.findChild<QDialogButtonBox*>();
    auto* reset_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessResetButton"));
    auto* strength_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    auto* strength_slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    auto* enabled_checkbox = window.findChild<QCheckBox*>(QStringLiteral("anNativeParamCheckBox"));
    auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("anNativeParamComboBox"));
    auto* note_edit = window.findChild<QLineEdit*>(QStringLiteral("anNativeParamLineEdit"));
    ASSERT_NE(execute_button, nullptr);
    ASSERT_NE(close_button, nullptr);
    ASSERT_NE(action_bar, nullptr);
    ASSERT_NE(button_box, nullptr);
    ASSERT_NE(reset_button, nullptr);
    ASSERT_NE(strength_spin, nullptr);
    ASSERT_NE(strength_slider, nullptr);
    ASSERT_NE(enabled_checkbox, nullptr);
    ASSERT_NE(mode_combo, nullptr);
    ASSERT_NE(note_edit, nullptr);
    ASSERT_TRUE(execute_button->isEnabled());
    const QString close_text = close_button->text();

    window.setExecutionInProgress(true);

    EXPECT_TRUE(window.isExecutionInProgress());
    EXPECT_FALSE(execute_button->isEnabled());
    EXPECT_TRUE(close_button->isEnabled());
    EXPECT_EQ(close_button->text(), QStringLiteral("Cancel"));
    EXPECT_FALSE(reset_button->isEnabled());
    EXPECT_FALSE(strength_spin->isEnabled());
    EXPECT_FALSE(strength_slider->isEnabled());
    EXPECT_FALSE(enabled_checkbox->isEnabled());
    EXPECT_FALSE(mode_combo->isEnabled());
    EXPECT_FALSE(note_edit->isEnabled());

    execute_button->click();
    EXPECT_EQ(execute_count, 0);
    close_button->click();
    EXPECT_EQ(cancel_count, 1);
    EXPECT_EQ(close_count, 0);
    EXPECT_TRUE(window.isExecutionInProgress());

    window.setExecutionInProgress(false);

    EXPECT_FALSE(window.isExecutionInProgress());
    EXPECT_TRUE(execute_button->isEnabled());
    EXPECT_TRUE(close_button->isEnabled());
    EXPECT_EQ(close_button->text(), close_text);
    EXPECT_TRUE(reset_button->isEnabled());
    EXPECT_TRUE(strength_spin->isEnabled());
    EXPECT_TRUE(strength_slider->isEnabled());
    EXPECT_TRUE(enabled_checkbox->isEnabled());
    EXPECT_TRUE(mode_combo->isEnabled());
    EXPECT_TRUE(note_edit->isEnabled());

    execute_button->click();
    EXPECT_EQ(execute_count, 1);
    close_button->click();
    EXPECT_EQ(cancel_count, 1);
    EXPECT_EQ(close_count, 1);
}

// An Apply holds the frames back: what the extension's run changes while it runs starts no frame, and what the run's
// end does in the call that ends it - its result shown, the parameters reset - is one frame, of the parameters it
// leaves over the result, as the host ends a run.
TEST(ExtensionNativeProcessWindowTest, AnApplyHoldsTheFramesBackAndOneFrameOfWhatItLeavesFollowsIt)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-apply-holds-frames", nullptr);
    window.setKeepOpenMode(true);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setExecuteCallback([]() {});
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.05},
    }});

    auto source_view = makePreviewView("apply_holds_frames_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    const int before = frames->count();

    // The run sets a parameter, as an extension's run may while it executes.
    window.setExecutionInProgress(true);
    window.applyParamUpdates(QCborMap{{QStringLiteral("strength"), 0.7}});
    QTest::qWait(250);
    EXPECT_EQ(frames->count(), before) << "no frame starts while the Apply runs";

    window.setExecutionInProgress(false);
    window.applyCommittedResult(greyAsset(10, 8, 3.0));
    window.resetParamsToDefaults();
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->firstSample == 3.0 && window.isProcessIdle();
    })) << "a frame over the result follows the Apply";
    QTest::qWait(150);
    EXPECT_EQ(frames->count(), before + 1) << "what the run's end asks for is one frame";
    const std::optional<PreviewFrameRecord::Frame> last = frames->last();
    ASSERT_TRUE(last.has_value());
    EXPECT_DOUBLE_EQ(last->params.value(QStringLiteral("strength")).toDouble(), 0.25);
}

// A window whose preview is made before it is shown, as the host makes it, shows its source fitted to the preview view
// while its first frame is on its way, and that frame, of the source's size, lands at the zoom the view already has.
TEST(ExtensionNativeProcessWindowTest, PreviewMadeBeforeTheWindowIsShownIsFittedBeforeItsFirstFrameLands)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-preview-fit", nullptr);
    window.setKeepOpenMode(true);

    const auto gate = std::make_shared<PreviewFrameGate>();
    const auto frames = std::make_shared<PreviewFrameRecord>();
    gate->close();
    window.setExecuteCallback([]() {});
    window.setPreviewBody(gatedPreviewBody(gate, frames));
    const FramesLandFirst landed(window);

    auto source_view = makePreviewView("preview_fit_source");
    showGrey(*source_view, 40, 30, 1.0);
    window.setSourceView(source_view.get());
    window.resize(900, 600);
    window.show();
    ASSERT_TRUE(waitUntil([&]() { return gate->waiting() == 1; }));
    QTest::qWait(150);

    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    ASSERT_EQ(frames->count(), 0) << "no frame has landed yet";
    const auto fit_factor = [preview_view]() {
        const ImageViewScrollArea* area = preview_view->getScrollArea();
        QRect viewport = area->viewport()->rect();
        if (area->horizontalScrollBar()->isVisible())
        {
            viewport.setHeight(viewport.height() + area->horizontalScrollBar()->rect().height());
        }
        if (area->verticalScrollBar()->isVisible())
        {
            viewport.setWidth(viewport.width() + area->verticalScrollBar()->rect().width());
        }
        return (std::min)(static_cast<double>(viewport.width()) / 40.0, static_cast<double>(viewport.height()) / 30.0);
    };
    const double fitted = preview_view->getViewWidget()->getCurrentScaleFactor();
    EXPECT_NEAR(fitted, fit_factor(), 1e-9) << "the source is fitted before the first frame lands";
    ASSERT_GT(std::abs(fitted - 1.0), 0.01) << "the fit is a zoom of its own";

    gate->open();
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    QTest::qWait(150);
    EXPECT_DOUBLE_EQ(preview_view->getViewWidget()->getCurrentScaleFactor(), fitted);
}

TEST(ExtensionNativeProcessWindowTest, PreviewInProgressKeepsControlsEnabledAndQueuesNextRefresh)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-preview-busy", nullptr);
    window.setKeepOpenMode(true);

    const auto gate = std::make_shared<PreviewFrameGate>();
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setExecuteCallback([]() {});
    window.setPreviewBody(gatedPreviewBody(gate, frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.25},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 1.0},
            {QStringLiteral("step"), 0.05},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("enabled")},
            {QStringLiteral("type"), QStringLiteral("bool")},
            {QStringLiteral("label"), QStringLiteral("Enabled")},
            {QStringLiteral("default"), true},
        },
    });

    auto source_view = makePreviewView("preview_busy_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));

    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    auto* close_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessCloseButton"));
    auto* reset_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessResetButton"));
    auto* strength_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    auto* strength_slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    auto* enabled_checkbox = window.findChild<QCheckBox*>(QStringLiteral("anNativeParamCheckBox"));
    ASSERT_NE(execute_button, nullptr);
    ASSERT_NE(close_button, nullptr);
    ASSERT_NE(reset_button, nullptr);
    ASSERT_NE(strength_spin, nullptr);
    ASSERT_NE(strength_slider, nullptr);
    ASSERT_NE(enabled_checkbox, nullptr);
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    WaitingSpinnerWidget* spinner = findSpinner(*preview_view);
    ASSERT_NE(spinner, nullptr) << "the framework's spinner stands over the preview view";

    // A frame caught on its way: the window stays usable, and its spinner is shown once the frame outlasts the delay.
    gate->close();
    const int arrived = gate->arrived();
    const QString close_text = close_button->text();
    strength_spin->setValue(0.4);
    ASSERT_TRUE(waitUntil([&]() { return gate->waiting() == 1; }));
    EXPECT_TRUE(window.isPreviewInProgress());
    EXPECT_FALSE(window.isExecutionInProgress());
    EXPECT_TRUE(execute_button->isEnabled());
    EXPECT_TRUE(close_button->isEnabled());
    EXPECT_EQ(close_button->text(), close_text);
    EXPECT_TRUE(reset_button->isEnabled());
    EXPECT_TRUE(strength_spin->isEnabled());
    EXPECT_TRUE(strength_slider->isEnabled());
    EXPECT_TRUE(enabled_checkbox->isEnabled());
    EXPECT_TRUE(waitUntil([&]() { return spinner->isSpinning(); }, 3000));

    // Changes while it runs wait for it, and one frame of the last of them follows it.
    strength_spin->setValue(0.5);
    strength_spin->setValue(0.6);
    QTest::qWait(250);
    EXPECT_EQ(gate->arrived(), arrived + 1) << "no frame starts while one is on its way";

    gate->open();
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }));
    EXPECT_FALSE(window.isPreviewInProgress());
    EXPECT_FALSE(spinner->isSpinning());
    const std::optional<PreviewFrameRecord::Frame> last = frames->last();
    ASSERT_TRUE(last.has_value());
    EXPECT_DOUBLE_EQ(last->params.value(QStringLiteral("strength")).toDouble(), 0.6);
    EXPECT_LE(gate->arrived(), arrived + 3) << "the changes made while a frame ran collapse into what follows it";
}

TEST(ExtensionNativeProcessWindowTest, ManualPreviewRefreshClearsDirtyAfterSynchronousParamUpdate)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-refresh-generation", nullptr);

    window.setParamActionCallback([](const std::string& action_id, const QCborMap&) {
        EXPECT_EQ(action_id, "refresh_preview");
        QCborMap updates;
        updates.insert(QStringLiteral("strength"), 0.75);
        return updates;
    });
    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Preview")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview_mode")},
            {QStringLiteral("type"), QStringLiteral("choice")},
            {QStringLiteral("label"), QStringLiteral("Display")},
            {QStringLiteral("default"), QStringLiteral("corrected")},
            {QStringLiteral("options"),
             QJsonArray{
                 QJsonArray{QStringLiteral("Processed Image"), QStringLiteral("corrected")},
                 QJsonArray{QStringLiteral("Extracted Gradient"), QStringLiteral("background_model")},
             }},
            {QStringLiteral("inline_actions"), QJsonArray{QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("refresh_preview")},
                                                   {QStringLiteral("type"), QStringLiteral("button")},
                                                   {QStringLiteral("label"), QStringLiteral("Update Preview")},
                                                   {QStringLiteral("preview_refresh_role"), true},
                                               }}},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("preview_status")},
            {QStringLiteral("type"), QStringLiteral("info")},
            {QStringLiteral("preview_status"), true},
            {QStringLiteral("text"), QStringLiteral("Preview update needed.")},
            {QStringLiteral("stale_text"), QStringLiteral("Preview update needed.")},
            {QStringLiteral("ready_text"), QStringLiteral("Preview is up to date.")},
            {QStringLiteral("updating_text"), QStringLiteral("Updating preview...")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.5},
            {QStringLiteral("preview_invalidates"), true},
        },
    });

    auto source_view = makePreviewView("refresh_generation_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    auto* action_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeParamInfoBanner"));
    ASSERT_NE(action_button, nullptr);
    ASSERT_NE(status_banner, nullptr);
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    action_button->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Info);

    window.markManualPreviewRefreshCompleted(true);

    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Success);
    EXPECT_TRUE(status_banner->message().contains(QStringLiteral("up to date")));
    EXPECT_DOUBLE_EQ(window.currentParams().value(QStringLiteral("strength")).toDouble(), 0.75);
}

TEST(ExtensionNativeProcessWindowTest, AManualPreviewIsStaleOnceItsSourceImageChanges)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-manual-preview-source", nullptr);
    window.setParamActionCallback([](const std::string&, const QCborMap&) { return QCborMap{}; });
    window.setParamDefs(manualPreviewParamDefs());

    auto source_view = makePreviewView("manual_preview_source");
    showGrey(*source_view, 10, 8, 0.5);
    window.setSourceView(source_view.get());

    auto* refresh_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeParamInfoBanner"));
    ASSERT_NE(refresh_button, nullptr);
    ASSERT_NE(status_banner, nullptr);

    refresh_button->click();
    QCoreApplication::processEvents();
    window.markManualPreviewRefreshCompleted(true);
    ASSERT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Success);
    ASSERT_FALSE(refresh_button->isEnabled());

    // The image changes in place at the same size: the preview shown was rendered from the one before it.
    replaceShownImage(*source_view, greyAsset(10, 8, 0.25));

    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_EQ(status_banner->message(), QStringLiteral("Preview update needed."));
    EXPECT_TRUE(refresh_button->isEnabled());

    refresh_button->click();
    QCoreApplication::processEvents();
    window.markManualPreviewRefreshCompleted(true);
    ASSERT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Success);

    // The view shows a new document.
    showGrey(*source_view, 10, 8, 0.75);

    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(refresh_button->isEnabled());

    window.setSourceView(nullptr);
}

TEST(ExtensionNativeProcessWindowTest, AManualPreviewRefreshedWhileItsSourceImageChangesStaysStale)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-manual-preview-in-flight",
                                                            nullptr);
    window.setParamActionCallback([](const std::string&, const QCborMap&) { return QCborMap{}; });
    window.setParamDefs(manualPreviewParamDefs());

    auto source_view = makePreviewView("manual_preview_in_flight");
    showGrey(*source_view, 10, 8, 0.5);
    window.setSourceView(source_view.get());

    auto* refresh_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeParamInfoBanner"));
    ASSERT_NE(refresh_button, nullptr);
    ASSERT_NE(status_banner, nullptr);

    refresh_button->click();
    QCoreApplication::processEvents();
    ASSERT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Info);

    // The refresh asked for may have read the image before it changed.
    replaceShownImage(*source_view, greyAsset(10, 8, 0.25));
    window.markManualPreviewRefreshCompleted(true);

    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(refresh_button->isEnabled());

    window.setSourceView(nullptr);
}

TEST(ExtensionNativeProcessWindowTest, AManualPreviewRefreshesOnOpenWhicheverOfItsViewAndItsBodyComesFirst)
{
    // The host binds the window to its view as the view reaches it, which can be before the preview body is set.
    for (const bool body_first : {true, false})
    {
        SCOPED_TRACE(body_first ? "the preview body set first" : "the source view bound first");
        an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-manual-preview-on-open",
                                                                nullptr);
        const FramesLandFirst landed(window);
        std::vector<std::string> actions;
        window.setParamActionCallback([&actions](const std::string& action_id, const QCborMap&) {
            actions.push_back(action_id);
            return QCborMap{};
        });
        window.setParamDefs(manualPreviewParamDefs(true));

        const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
        auto source_view = makePreviewView(body_first ? "manual_preview_body_first" : "manual_preview_view_first");
        showGrey(*source_view, 10, 8, 1.0);
        if (body_first)
        {
            window.setPreviewBody(recordingPreviewBody(frames));
            window.setSourceView(source_view.get());
        }
        else
        {
            window.setSourceView(source_view.get());
            window.setPreviewBody(recordingPreviewBody(frames));
        }

        auto* refresh_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
        auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeParamInfoBanner"));
        ASSERT_NE(refresh_button, nullptr);
        ASSERT_NE(status_banner, nullptr);

        ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
        EXPECT_EQ(actions, std::vector<std::string>{"refresh_preview"});
        EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Success);
        EXPECT_FALSE(refresh_button->isEnabled());

        window.setSourceView(nullptr);
    }
}

TEST(ExtensionNativeProcessWindowTest, AnExtensionsOwnEngineWorkInsideAFrameIsAdmitted)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-extension-engine-work", nullptr);
    const FramesLandFirst landed(window);

    // An extension's preview asks the engine for work of its own, as an SDK call such as a star search does: an
    // admission of its own, from inside the frame that waits for it.
    const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
    const auto admitted = std::make_shared<std::atomic<int>>(0);
    const auto refused = std::make_shared<std::atomic<int>>(0);
    window.setPreviewBody([body = recordingPreviewBody(frames), admitted,
                           refused](const an::ui::extensions::ExtensionPreviewFrame& frame) {
        an::consumer::OperationRequest request;
        request.purpose = an::consumer::OperationPurpose::Apply;
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        if (an::Result<an::consumer::OperationScope> own = suiteSession().admit(request); own.hasValue())
        {
            ++*admitted;
        }
        else
        {
            ++*refused;
        }
        return body(frame);
    });
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
    }});

    auto source_view = makePreviewView("extension_engine_work_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }, 20000));
    EXPECT_GE(admitted->load(), 1);
    EXPECT_EQ(refused->load(), 0) << "the extension's admission waited for the frame that waited for it";

    window.setSourceView(nullptr);
}

TEST(ExtensionNativeProcessWindowTest, CurveEditorDragShowsInteractiveFramesAndSettlesOnRelease)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-curve-drag", nullptr);

    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    int changed = 0;
    window.setParamChangedCallback([&changed](const QCborMap&, const QStringList&, const QString&) { ++changed; });
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("curve_points")},
        {QStringLiteral("type"), QStringLiteral("curve_editor")},
        {QStringLiteral("label"), QStringLiteral("Curve")},
        {QStringLiteral("default"), QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{0.5, 0.5}, QJsonArray{1.0, 1.0}}},
    }});

    auto source_view = makePreviewView("curve_drag_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    frames->clear();
    changed = 0;

    auto* curve_editor_widget = window.findChild<QWidget*>(QStringLiteral("anNativeParamCurveEditor"));
    auto* curve_editor = dynamic_cast<CurvesWidget*>(curve_editor_widget);
    ASSERT_NE(curve_editor, nullptr);

    // A curve held and dragged is shown in interactive frames, as a dragged slider is; the extension hears of the
    // change once the curve is let go.
    QTest::mousePress(curve_editor, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    curve_editor->setCurve(
        an::ui::ToneCurveModel({{0.0, 0.0}, {0.35, 0.65}, {1.0, 1.0}}, an::CurveInterpolation::Akima));
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->interactive;
    })) << "a dragged curve is shown in interactive frames";
    EXPECT_EQ(changed, 0);

    QTest::mouseRelease(curve_editor, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && !last->interactive && window.isProcessIdle();
    })) << "letting the curve go settles the preview";
    EXPECT_EQ(changed, 1);
    const QCborArray points = frames->last()->params.value(QStringLiteral("curve_points")).toArray();
    ASSERT_EQ(points.size(), 3);
    EXPECT_NEAR(points.at(1).toArray().at(1).toDouble(), 0.65, 1e-9);
}

TEST(ExtensionNativeProcessWindowTest, CurveEditorInterpolationNamesMapToTheirRules)
{
    // The schema's names, each mapped to the rule a spline of that name evaluates.
    const std::pair<const char*, an::CurveInterpolation> names[] = {
        {"linear", an::CurveInterpolation::Linear},       {"cubic", an::CurveInterpolation::NaturalCubic},
        {"steffen", an::CurveInterpolation::Steffen},     {"akima", an::CurveInterpolation::Akima},
        {"anything else", an::CurveInterpolation::Akima},
    };
    for (const auto& [name, rule] : names)
    {
        an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-curve-interpolation",
                                                                nullptr);
        window.setParamDefs(QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("curve_points")},
            {QStringLiteral("type"), QStringLiteral("curve_editor")},
            {QStringLiteral("label"), QStringLiteral("Curve")},
            {QStringLiteral("interpolation"), QString::fromLatin1(name)},
            {QStringLiteral("default"), QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{0.25, 0.4}, QJsonArray{0.5, 0.5},
                                                   QJsonArray{0.75, 0.6}, QJsonArray{1.0, 1.0}}},
        }});
        auto* editor =
            dynamic_cast<CurvesWidget*>(window.findChild<QWidget*>(QStringLiteral("anNativeParamCurveEditor")));
        ASSERT_NE(editor, nullptr) << name;
        EXPECT_EQ(editor->getCurve().interpolation(), rule) << name;
        EXPECT_EQ(editor->getCurve().size(), 5u) << name;

        // A value the extension sets keeps the rule the schema named.
        window.applyParamUpdates(
            QCborMap{{QStringLiteral("curve_points"),
                      QCborArray{QCborArray{0.0, 0.0}, QCborArray{0.5, 0.7}, QCborArray{1.0, 1.0}}}});
        EXPECT_EQ(editor->getCurve().interpolation(), rule) << name;
        const QCborArray points = window.currentParams().value(QStringLiteral("curve_points")).toArray();
        ASSERT_EQ(points.size(), 3) << name;
        EXPECT_DOUBLE_EQ(points.at(1).toArray().at(1).toDouble(), 0.7) << name;
    }
}

TEST(ExtensionNativeProcessWindowTest, RtHistogramParamUsesNativeHistogramWidget)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-rt-histogram", nullptr);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("preview_histogram")},
        {QStringLiteral("type"), QStringLiteral("rt_histogram")},
        {QStringLiteral("label"), QStringLiteral("Histogram")},
        {QStringLiteral("height"), 160},
        {QStringLiteral("grid"), true},
        {QStringLiteral("clipping_info"), true},
        {QStringLiteral("histogram_areas"), false},
    }});

    auto* histogram_widget =
        dynamic_cast<HistogramWidget*>(window.findChild<QWidget*>(QStringLiteral("anNativeParamRtHistogram")));
    ASSERT_NE(histogram_widget, nullptr);
    EXPECT_EQ(histogram_widget->property("anParamId").toString(), QStringLiteral("preview_histogram"));
    EXPECT_TRUE(histogram_widget->getGridEnabled());
    EXPECT_TRUE(histogram_widget->getClippingInfoEnabled());
    EXPECT_FALSE(histogram_widget->getAreaModeEnabled());

    auto* plot_resolution = window.findChild<QComboBox*>(QStringLiteral("plotResolution"));
    ASSERT_NE(plot_resolution, nullptr);
    EXPECT_EQ(plot_resolution->currentText(), QStringLiteral("8-bit (256)"));
    EXPECT_NE(window.findChild<QToolButton*>(QStringLiteral("zoomIn")), nullptr);
    EXPECT_NE(window.findChild<QToolButton*>(QStringLiteral("showHistogramAreas")), nullptr);

    auto source_view = makePreviewView("rt_histogram_source");
    showGrey(*source_view, 16, 12, 0.0);
    window.setSourceView(source_view.get());

    // The widget is counted on the thread pool and shows the latest count once it lands.
    constexpr std::uint64_t kPixels = 16u * 12u;
    const auto counts = [&](std::uint64_t darkest, std::uint64_t brightest) {
        return waitUntil([&]() {
            return firstChannelBin(*histogram_widget, 0u) == darkest &&
                   firstChannelBin(*histogram_widget, 255u) == brightest;
        });
    };
    EXPECT_TRUE(counts(kPixels, 0u)) << "the source's counts while the window does not preview";

    // Previewing, the histogram is the preview's own: each frame counts what it made, in its operation.
    const auto frames = std::make_shared<PreviewFrameRecord>(1.0);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    EXPECT_TRUE(counts(0u, kPixels)) << "the preview's counts";
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }));

    ASSERT_NE(preview_view->getViewWidget(), nullptr);
    preview_view->getViewWidget()->enableAlternateImageRendering(true);
    EXPECT_TRUE(counts(kPixels, 0u)) << "the original's counts while the original is shown";

    preview_view->getViewWidget()->enableAlternateImageRendering(false);
    EXPECT_TRUE(counts(0u, kPixels)) << "the preview's counts again";

    // Another resolution is counted again, at once, into as many bins as it names.
    plot_resolution->setCurrentText(QStringLiteral("6-bit (64)"));
    EXPECT_TRUE(waitUntil([&]() {
        const std::shared_ptr<const an::ui::HistogramDisplayData> data = histogram_widget->getHistogram();
        return data && data->isValid() && data->channels.front().bins.size() == 64u &&
               data->channels.front().bins.back() == kPixels;
    })) << "the preview counted again at 64 bins";
}

TEST(ExtensionNativeProcessWindowTest, VectorSlopeParamUsesNativeGraphAndTracksVectorParams)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-vectra-vector-slope", nullptr);
    window.setParamDefs(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("red_hue")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Red Hue")},
                    {QStringLiteral("default"), 0.0},
                    {QStringLiteral("min"), -60.0},
                    {QStringLiteral("max"), 60.0},
                    {QStringLiteral("step"), 1.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("red_saturation")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Red Saturation")},
                    {QStringLiteral("default"), 0.0},
                    {QStringLiteral("min"), -100.0},
                    {QStringLiteral("max"), 100.0},
                    {QStringLiteral("step"), 1.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("yellow_hue")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Yellow Hue")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("yellow_saturation")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Yellow Saturation")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("green_hue")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Green Hue")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("green_saturation")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Green Saturation")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cyan_hue")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Cyan Hue")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cyan_saturation")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Cyan Saturation")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("blue_hue")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Blue Hue")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("blue_saturation")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Blue Saturation")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("magenta_hue")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Magenta Hue")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("magenta_saturation")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("label"), QStringLiteral("Magenta Saturation")},
                    {QStringLiteral("default"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vector_slope")},
                    {QStringLiteral("type"), QStringLiteral("vector_slope")},
                    {QStringLiteral("label"), QStringLiteral("Vector Slope")},
                    {QStringLiteral("height"), 180}},
    });

    auto* graph = dynamic_cast<an::ui::VectorSlopeWidget*>(
        window.findChild<QWidget*>(QStringLiteral("anNativeParamVectorSlope")));
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->property("anParamId").toString(), QStringLiteral("vector_slope"));
    EXPECT_EQ(graph->minimumHeight(), 180);
    EXPECT_DOUBLE_EQ(graph->property("anVectorSlopeRedHueShift").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(graph->property("anVectorSlopeRedSaturation").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(graph->hueShiftDegrees(QStringLiteral("R")), 0.0);
    EXPECT_DOUBLE_EQ(graph->saturation(QStringLiteral("R")), 0.0);
    EXPECT_FALSE(window.currentParams().contains(QStringLiteral("vector_slope")));

    const auto spins = window.findChildren<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    QDoubleSpinBox* red_hue = nullptr;
    QDoubleSpinBox* red_saturation = nullptr;
    for (QDoubleSpinBox* spin : spins)
    {
        if (spin->property("anParamId").toString() == QStringLiteral("red_hue"))
        {
            red_hue = spin;
        }
        if (spin->property("anParamId").toString() == QStringLiteral("red_saturation"))
        {
            red_saturation = spin;
        }
    }
    ASSERT_NE(red_hue, nullptr);
    ASSERT_NE(red_saturation, nullptr);

    red_hue->setValue(24.0);
    red_saturation->setValue(35.0);

    EXPECT_DOUBLE_EQ(graph->property("anVectorSlopeRedHueShift").toDouble(), 24.0);
    EXPECT_NEAR(graph->property("anVectorSlopeRedSaturation").toDouble(), 0.35, 1.0e-12);
    EXPECT_DOUBLE_EQ(graph->hueShiftDegrees(QStringLiteral("R")), 24.0);
    EXPECT_NEAR(graph->saturation(QStringLiteral("R")), 0.35, 1.0e-12);
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigurationGatesExecuteButton)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("External Tool Denoise", "session-tool-config", nullptr);

    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    ASSERT_NE(execute_button, nullptr);

    QMap<QString, QCborValue> settings;
    window.setSettingsCallbacks(
        [&settings](const QString& key, const QCborValue& default_value) { return settings.value(key, default_value); },
        [&settings](const QString& key, const QCborValue& value, std::string*) {
            settings.insert(key, value);
            return true;
        });

    int execute_count = 0;
    window.setExecuteCallback([&execute_count]() { ++execute_count; });
    window.setParamDefs(
        QJsonArray{QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("window_meta")},
                       {QStringLiteral("type"), QStringLiteral("meta")},
                       {QStringLiteral("tool_configuration"),
                        QJsonObject{
                            {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
                            {QStringLiteral("button_label"), QStringLiteral("Configure")},
                            {QStringLiteral("dialog_title"), QStringLiteral("Select Tool Folder")},
                            {QStringLiteral("not_configured_text"), QStringLiteral("External tool is not configured.")},
                            {QStringLiteral("configured_text"), QStringLiteral("External tool is configured.")},
                            {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
                            {QStringLiteral("append_platform_executable_suffix"), false},
                        }},
                   },
                   QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("strength")},
                       {QStringLiteral("type"), QStringLiteral("double")},
                       {QStringLiteral("label"), QStringLiteral("Strength")},
                       {QStringLiteral("default"), 0.25},
                   }});

    auto* configure_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessToolConfigButton"));
    auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeProcessToolConfigBanner"));
    ASSERT_NE(configure_button, nullptr);
    ASSERT_NE(status_banner, nullptr);
    EXPECT_TRUE(configure_button->isVisibleTo(&window));
    EXPECT_EQ(configure_button->text(), QStringLiteral("Configure"));
    EXPECT_TRUE(status_banner->message().contains(QStringLiteral("not configured")));
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    auto source_view = makePreviewView("tool_config_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    EXPECT_FALSE(execute_button->isEnabled());

    QTemporaryDir invalid_dir;
    ASSERT_TRUE(invalid_dir.isValid());
    settings.insert(QStringLiteral("executable_path"), invalid_dir.path());
    window.refreshExternalToolConfiguration();
    EXPECT_FALSE(execute_button->isEnabled());
    EXPECT_TRUE(status_banner->message().contains(QStringLiteral("cosmic_tool")));
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    QFile executable(QDir(invalid_dir.path()).filePath(QStringLiteral("Cosmic_Tool")));
    ASSERT_TRUE(executable.open(QIODevice::WriteOnly));
    executable.write("placeholder");
    executable.close();
    executable.setPermissions(executable.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                              QFileDevice::ExeGroup | QFileDevice::ExeOther);

    window.refreshExternalToolConfiguration();
    QTRY_VERIFY_WITH_TIMEOUT(execute_button->isEnabled(), 2000);
    EXPECT_EQ(status_banner->message(), QStringLiteral("External tool is configured."));
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Success);

    QTest::mouseClick(execute_button, Qt::LeftButton);
    EXPECT_EQ(execute_count, 1);
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolDiagnosticDescribesTheConfiguredFolderNotTheFallbackSearch)
{
    // When the configured folder is rejected, the resolver keeps probing the declared candidate
    // directories and then every PATH entry. Those probes are a silent fallback: only the configured
    // folder is something the user can act on, so a failed probe must not become the banner text.
    // The regression this pins reported whichever location was probed last - in practice the final
    // PATH entry - so the banner read "Configured Cosmic folder does not exist: <unrelated path>".
    //
    // The configured folder is deliberately absent rather than merely incomplete: the missing-
    // executable diagnostic does not name the folder it describes, so only the "does not exist"
    // wording can distinguish the configured folder from a fallback location on any machine,
    // whatever that machine's PATH happens to contain.
    an::ui::extensions::ExtensionNativeProcessWindow window("Cosmic", "session-tool-diagnostic", nullptr);

    QTemporaryDir temp_root;
    ASSERT_TRUE(temp_root.isValid());
    const QDir root(temp_root.path());
    const QString configured_path = root.filePath(QStringLiteral("configured-tool-folder"));
    const QString candidate_path = root.filePath(QStringLiteral("candidate-tool-folder"));
    ASSERT_FALSE(QFileInfo::exists(configured_path));
    ASSERT_FALSE(QFileInfo::exists(candidate_path));

    QMap<QString, QCborValue> settings;
    settings.insert(QStringLiteral("executable_path"), configured_path);
    window.setSettingsCallbacks(
        [&settings](const QString& key, const QCborValue& default_value) { return settings.value(key, default_value); },
        [&settings](const QString& key, const QCborValue& value, std::string*) {
            settings.insert(key, value);
            return true;
        });

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("tool_configuration"),
         QJsonObject{
             {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
             {QStringLiteral("label"), QStringLiteral("Cosmic")},
             {QStringLiteral("not_configured_text"), QStringLiteral("External tool is not configured.")},
             {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
             {QStringLiteral("append_platform_executable_suffix"), false},
             {QStringLiteral("candidate_directories"), QJsonArray{candidate_path}},
         }},
    }});

    auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeProcessToolConfigBanner"));
    ASSERT_NE(status_banner, nullptr);

    window.refreshExternalToolConfiguration();

    const QString message = status_banner->message();
    EXPECT_TRUE(message.contains(configured_path))
        << "banner described a fallback location instead of the configured folder: " << message.toStdString();
    EXPECT_FALSE(message.contains(candidate_path))
        << "banner named a candidate directory the user never configured: " << message.toStdString();
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigurationResolvesCandidateDirectoryRecord)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RC Astro", "session-tool-candidate", nullptr);

    window.setSettingsCallbacks([](const QString&, const QCborValue& default_value) { return default_value; },
                                [](const QString&, const QCborValue&, std::string*) { return true; });

    QTemporaryDir candidate_dir;
    ASSERT_TRUE(candidate_dir.isValid());
    QDir root_dir(candidate_dir.path());
    ASSERT_TRUE(root_dir.mkpath(QStringLiteral("CLI")));
#ifdef _WIN32
    const QString executable_name = QStringLiteral("rc-astro-cli.cmd");
#else
    const QString executable_name = QStringLiteral("rc-astro-cli");
#endif
    QFile executable(root_dir.filePath(QStringLiteral("CLI/%1").arg(executable_name)));
    ASSERT_TRUE(executable.open(QIODevice::WriteOnly));
#ifdef _WIN32
    executable.write("@echo off\r\necho RC-Astro CLI\r\necho Version 1.2.3\r\n");
#else
    executable.write("#!/bin/sh\necho RC-Astro CLI\necho Version 1.2.3\n");
#endif
    executable.close();
    executable.setPermissions(executable.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                              QFileDevice::ExeGroup | QFileDevice::ExeOther);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("tool_configuration"),
         QJsonObject{
             {QStringLiteral("settings_key"), QStringLiteral("rc_astro_folder")},
             {QStringLiteral("label"), QStringLiteral("RC-Astro")},
             {QStringLiteral("primary_executable"), QStringLiteral("rc-astro")},
             {QStringLiteral("primary_executable_candidates"), QJsonArray{QStringLiteral("rc-astro"), executable_name}},
             {QStringLiteral("candidate_subdirectories"), QJsonArray{QStringLiteral("CLI")}},
             {QStringLiteral("candidate_directories"), QJsonArray{candidate_dir.path()}},
             {QStringLiteral("version_arguments"), QJsonArray{QStringLiteral("--help")}},
             {QStringLiteral("version_pattern"), QStringLiteral("Version\\s+([^\\r\\n]+)")},
         }},
    }});

    const QCborMap record = window.resolvedExternalToolRecord();
    EXPECT_EQ(record.value(QStringLiteral("resolution_source")).toString(), QStringLiteral("candidate_directory"));
    EXPECT_EQ(record.value(QStringLiteral("configured_folder")).toString(), candidate_dir.path());
    EXPECT_EQ(QFileInfo(record.value(QStringLiteral("resolved_cli_executable")).toString()).absoluteFilePath(),
              QFileInfo(root_dir.filePath(QStringLiteral("CLI/%1").arg(executable_name))).absoluteFilePath());
    EXPECT_TRUE(record.value(QStringLiteral("resolved_cli_version")).toString().isEmpty())
        << "Parameter construction must not synchronously wait for an external CLI version probe.";
    EXPECT_TRUE(record.value(QStringLiteral("resolved_cli_version_pending")).toBool());
    EXPECT_EQ(
        window.transientExternalToolSettingValue(QStringLiteral("resolved_cli_executable"), QCborValue()).toString(),
        record.value(QStringLiteral("resolved_cli_executable")).toString());
    QTRY_COMPARE_WITH_TIMEOUT(
        window.resolvedExternalToolRecord().value(QStringLiteral("resolved_cli_version")).toString(),
        QStringLiteral("1.2.3"), 2000);
    EXPECT_FALSE(window.resolvedExternalToolRecord().value(QStringLiteral("resolved_cli_version_pending")).toBool());
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigureButtonIsLeftAlignedInActionRow)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("External Tool Denoise", "session-tool-config-left-aligned",
                                                            nullptr);

    window.setSettingsCallbacks([](const QString&, const QCborValue& default_value) { return default_value; },
                                [](const QString&, const QCborValue&, std::string*) { return true; });

    window.setParamDefs(
        QJsonArray{QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("window_meta")},
                       {QStringLiteral("type"), QStringLiteral("meta")},
                       {QStringLiteral("tool_configuration"),
                        QJsonObject{
                            {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
                            {QStringLiteral("button_label"), QStringLiteral("Configure")},
                            {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
                            {QStringLiteral("append_platform_executable_suffix"), false},
                        }},
                   },
                   QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("strength")},
                       {QStringLiteral("type"), QStringLiteral("double")},
                       {QStringLiteral("label"), QStringLiteral("Strength")},
                       {QStringLiteral("default"), 0.25},
                   }});

    window.show();
    QTest::qWait(60);

    auto* action_bar = window.findChild<QWidget*>(QStringLiteral("anNativeProcessActionBar"));
    auto* button_box = window.findChild<QDialogButtonBox*>();
    auto* configure_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessToolConfigButton"));
    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    auto* close_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessCloseButton"));

    ASSERT_NE(action_bar, nullptr);
    ASSERT_NE(button_box, nullptr);
    ASSERT_NE(configure_button, nullptr);
    ASSERT_NE(execute_button, nullptr);
    ASSERT_NE(close_button, nullptr);
    EXPECT_TRUE(configure_button->isVisibleTo(&window));

    const int action_row_left = action_bar->mapToGlobal(QPoint(0, 0)).x();
    const int action_row_right = action_bar->mapToGlobal(QPoint(action_bar->width(), 0)).x();
    const int configure_left = configure_button->mapToGlobal(QPoint(0, 0)).x();
    const int configure_right = configure_button->mapToGlobal(QPoint(configure_button->width(), 0)).x();
    const int execute_left = execute_button->mapToGlobal(QPoint(0, 0)).x();
    const int standard_buttons_right = (std::max)(execute_button->mapToGlobal(QPoint(execute_button->width(), 0)).x(),
                                                  close_button->mapToGlobal(QPoint(close_button->width(), 0)).x());

    EXPECT_LE(std::abs(configure_left - action_row_left), an::ui::tokens().spacing.md);
    EXPECT_GT(execute_left, configure_right + an::ui::tokens().spacing.xl);
    EXPECT_LE(action_row_right - standard_buttons_right, an::ui::tokens().spacing.lg);
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigurationCanHideConfiguredProcessBanner)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("External Tool Denoise",
                                                            "session-tool-config-hidden-success", nullptr);

    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    ASSERT_NE(execute_button, nullptr);

    QMap<QString, QCborValue> settings;
    window.setSettingsCallbacks(
        [&settings](const QString& key, const QCborValue& default_value) { return settings.value(key, default_value); },
        [&settings](const QString& key, const QCborValue& value, std::string*) {
            settings.insert(key, value);
            return true;
        });

    window.setExecuteCallback([]() {});
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("tool_configuration"),
         QJsonObject{
             {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
             {QStringLiteral("button_label"), QStringLiteral("Configure")},
             {QStringLiteral("not_configured_text"), QStringLiteral("External tool is not configured.")},
             {QStringLiteral("configured_text"), QStringLiteral("External tool is configured.")},
             {QStringLiteral("show_configured_banner"), false},
             {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
             {QStringLiteral("append_platform_executable_suffix"), false},
         }},
    }});

    auto* status_banner = findInfoBannerByObjectName(window, QStringLiteral("anNativeProcessToolConfigBanner"));
    ASSERT_NE(status_banner, nullptr);
    EXPECT_TRUE(status_banner->isVisibleTo(&window));
    EXPECT_EQ(status_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    auto source_view = makePreviewView("tool_config_hidden_success_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    EXPECT_FALSE(execute_button->isEnabled());

    QTemporaryDir valid_dir;
    ASSERT_TRUE(valid_dir.isValid());
    QFile executable(QDir(valid_dir.path()).filePath(QStringLiteral("Cosmic_Tool")));
    ASSERT_TRUE(executable.open(QIODevice::WriteOnly));
    executable.write("placeholder");
    executable.close();
    executable.setPermissions(executable.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                              QFileDevice::ExeGroup | QFileDevice::ExeOther);

    settings.insert(QStringLiteral("executable_path"), valid_dir.path());
    window.refreshExternalToolConfiguration();

    QTRY_VERIFY_WITH_TIMEOUT(execute_button->isEnabled(), 2000);
    EXPECT_TRUE(status_banner->isHidden());
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigurationCanOpenExtensionSettingsInsteadOfManagementSurface)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("External Tool Denoise", "session-tool-config-settings",
                                                            nullptr);

    int open_settings_count = 0;
    window.setOpenSettingsCallback([&open_settings_count](std::string*) {
        ++open_settings_count;
        return true;
    });
    window.setSettingsCallbacks([](const QString&, const QCborValue& default_value) { return default_value; },
                                [](const QString&, const QCborValue&, std::string*) { return true; });

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("tool_configuration"),
         QJsonObject{
             {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
             {QStringLiteral("button_label"), QStringLiteral("Settings")},
             {QStringLiteral("open_extension_settings"), true},
             {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
             {QStringLiteral("append_platform_executable_suffix"), false},
         }},
    }});

    auto* configure_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessToolConfigButton"));
    ASSERT_NE(configure_button, nullptr);
    EXPECT_EQ(configure_button->text(), QStringLiteral("Settings"));

    QTest::mouseClick(configure_button, Qt::LeftButton);
    QCoreApplication::processEvents();

    EXPECT_EQ(open_settings_count, 1);
    EXPECT_EQ(window.findChild<QDialog*>(QStringLiteral("anExternalToolConfigurationDialog")), nullptr);
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigurationDialogOpensManagementSurfaceAsynchronously)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("External Tool Denoise", "session-tool-config-async",
                                                            nullptr);

    window.setSettingsCallbacks([](const QString&, const QCborValue& default_value) { return default_value; },
                                [](const QString&, const QCborValue&, std::string*) { return true; });

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("tool_configuration"),
         QJsonObject{
             {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
             {QStringLiteral("button_label"), QStringLiteral("Configure")},
             {QStringLiteral("dialog_title"), QStringLiteral("Select Tool Folder")},
             {QStringLiteral("dialog_size"), QJsonArray{720, 500}},
             {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
             {QStringLiteral("append_platform_executable_suffix"), false},
             {QStringLiteral("download_page_url"), QStringLiteral("https://example.test/cosmic")},
             {QStringLiteral("download_button_label"), QStringLiteral("Download Suite")},
             {QStringLiteral("platform_update_api_urls"),
              QJsonObject{
                  {QStringLiteral("linux"), QStringLiteral("https://api.example.test/linux")},
                  {QStringLiteral("windows"), QStringLiteral("https://api.example.test/windows")},
              }},
             {QStringLiteral("update_page_url"), QStringLiteral("https://example.test/releases")},
             {QStringLiteral("install_instructions"), QStringLiteral("Download, unzip, then select the folder.")},
         }},
    }});

    auto* configure_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessToolConfigButton"));
    ASSERT_NE(configure_button, nullptr);

    QTest::mouseClick(configure_button, Qt::LeftButton);
    QCoreApplication::processEvents();

    auto* dialog = window.findChild<QDialog*>(QStringLiteral("anExternalToolConfigurationDialog"));
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_EQ(dialog->minimumSize(), dialog->maximumSize());
    EXPECT_EQ(dialog->minimumSize(), QSize(720, 500));

    auto* path_edit = dialog->findChild<QLineEdit*>(QStringLiteral("anExternalToolConfigPathEdit"));
    auto* browse_button = dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigBrowseButton"));
    auto* download_button = dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigDownloadButton"));
    auto* check_updates_button =
        dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigCheckUpdatesButton"));
    auto* update_button = dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigUpdateButton"));
    auto* button_box = dialog->findChild<QDialogButtonBox*>();
    ASSERT_NE(path_edit, nullptr);
    ASSERT_NE(browse_button, nullptr);
    ASSERT_NE(download_button, nullptr);
    ASSERT_NE(check_updates_button, nullptr);
    ASSERT_NE(update_button, nullptr);
    ASSERT_NE(button_box, nullptr);
    EXPECT_TRUE(download_button->isEnabled());
    EXPECT_TRUE(check_updates_button->isEnabled());
    EXPECT_FALSE(update_button->isEnabled());
    EXPECT_EQ(download_button->text(), QStringLiteral("Download Suite"));
    EXPECT_GT(button_box->geometry().top(), update_button->geometry().bottom());

    QTest::mouseClick(browse_button, Qt::LeftButton);
    QCoreApplication::processEvents();

    auto* folder_dialog = dialog->findChild<QFileDialog*>(QStringLiteral("anExternalToolConfigFolderDialog"));
    ASSERT_NE(folder_dialog, nullptr);
    EXPECT_TRUE(folder_dialog->isVisible());

    folder_dialog->reject();
    QCoreApplication::processEvents();

    dialog->reject();
    QCoreApplication::processEvents();
}

TEST(ExtensionNativeProcessWindowTest, ExternalToolConfigurationDialogOmitsUpdateControlsWithoutUpdateSource)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("External Tool Denoise", "session-tool-config-no-updater",
                                                            nullptr);

    window.setSettingsCallbacks([](const QString&, const QCborValue& default_value) { return default_value; },
                                [](const QString&, const QCborValue&, std::string*) { return true; });

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("tool_configuration"),
         QJsonObject{
             {QStringLiteral("settings_key"), QStringLiteral("executable_path")},
             {QStringLiteral("button_label"), QStringLiteral("Configure")},
             {QStringLiteral("required_executables"), QJsonArray{QStringLiteral("cosmic_tool")}},
             {QStringLiteral("append_platform_executable_suffix"), false},
             {QStringLiteral("download_page_url"), QStringLiteral("https://example.test/cosmic")},
         }},
    }});

    auto* configure_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessToolConfigButton"));
    ASSERT_NE(configure_button, nullptr);

    QTest::mouseClick(configure_button, Qt::LeftButton);
    QCoreApplication::processEvents();

    auto* dialog = window.findChild<QDialog*>(QStringLiteral("anExternalToolConfigurationDialog"));
    ASSERT_NE(dialog, nullptr);

    auto* download_button = dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigDownloadButton"));
    auto* check_updates_button =
        dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigCheckUpdatesButton"));
    auto* update_button = dialog->findChild<QPushButton*>(QStringLiteral("anExternalToolConfigUpdateButton"));
    ASSERT_NE(download_button, nullptr);
    EXPECT_TRUE(download_button->isVisibleTo(dialog));
    EXPECT_EQ(check_updates_button, nullptr);
    EXPECT_EQ(update_button, nullptr);

    dialog->reject();
    QCoreApplication::processEvents();
}

TEST(ExtensionNativeProcessWindowTest, PreviewAreaSelectionLivesOnPreviewSurface)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-preview-subarea",
                                                            nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("sub_area"), true},
        {QStringLiteral("sub_area_size"), QJsonArray{3, 2}},
        {QStringLiteral("sub_area_label"), QStringLiteral("Preview: Contrast Demo")},
    }});

    auto source_view = makePreviewView("native_source_subarea");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    auto* preview_area = preview_view->getRTPreviewAreaCheckBox();
    ASSERT_NE(preview_area, nullptr);
    EXPECT_TRUE(preview_area->isChecked());
    ASSERT_NE(preview_view->getViewWidget(), nullptr);
    ASSERT_NE(source_view->getViewWidget(), nullptr);
    EXPECT_TRUE(preview_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_FALSE(source_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_TRUE(window.isSubAreaEnabled());
    const QRect area = window.previewArea();
    ASSERT_EQ(area.size(), QSize(3, 2));

    // The extension is handed the area cut from the image, and told where it lies.
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->part == area && window.isProcessIdle();
    }));
    EXPECT_EQ(frames->last()->width, 3);
    EXPECT_EQ(frames->last()->height, 2);
    EXPECT_EQ(frames->last()->imageSize, QSize(10, 8));

    preview_view->getViewWidget()->updateSelectionArea(QRect(1, 2, 3, 4));
    window.selectionAreaChanged();

    EXPECT_EQ(window.previewArea(), QRect(1, 2, 3, 4));
    EXPECT_FALSE(source_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->part == QRect(1, 2, 3, 4) && window.isProcessIdle();
    })) << "a moved area is previewed where it now lies";
}

TEST(ExtensionNativeProcessWindowTest, PreviewAreaCanBeDisabledByMeta)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-preview-area-disabled",
                                                            nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("preview_area"), false},
        {QStringLiteral("sub_area"), true},
        {QStringLiteral("sub_area_default_enabled"), true},
        {QStringLiteral("sub_area_size"), QJsonArray{3, 2}},
        {QStringLiteral("sub_area_label"), QStringLiteral("Preview: Disabled")},
    }});

    auto source_view = makePreviewView("native_source_preview_area_disabled");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());

    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    auto* preview_area = preview_view->getRTPreviewAreaCheckBox();
    ASSERT_NE(preview_area, nullptr);
    EXPECT_FALSE(preview_area->isEnabled());
    EXPECT_TRUE(preview_area->isHidden());
    EXPECT_FALSE(preview_area->isChecked());
    ASSERT_NE(preview_view->getViewWidget(), nullptr);
    EXPECT_FALSE(preview_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_TRUE(window.previewArea().isEmpty());

    preview_area->setChecked(true);
    QCoreApplication::processEvents();

    EXPECT_FALSE(preview_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_TRUE(window.previewArea().isEmpty());
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    EXPECT_TRUE(frames->last()->part.isEmpty()) << "the extension is handed the whole image";
}

TEST(ExtensionNativeProcessWindowTest, PreviewAreaCanStartDisabledAndRtPreviewMetaAppliesDefaults)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-preview-defaults",
                                                            nullptr);
    window.resize(1000, 700);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("window_meta")},
            {QStringLiteral("type"), QStringLiteral("meta")},
            {QStringLiteral("sub_area"), true},
            {QStringLiteral("sub_area_default_enabled"), false},
            {QStringLiteral("sub_area_size"), QJsonArray{3, 2}},
            {QStringLiteral("sub_area_label"), QStringLiteral("Preview: Contrast Demo")},
            {QStringLiteral("controls_panel_width"), 520},
            {QStringLiteral("preview_hq_default"), true},
            {QStringLiteral("preview_autostretch"), true},
            {QStringLiteral("preview_autostretch_default"), true},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("strength")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Strength")},
            {QStringLiteral("default"), 0.25},
            {QStringLiteral("tooltip"), QStringLiteral("Controls how strongly the stretch is applied.")},
        },
    });

    auto* main_splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    ASSERT_NE(main_splitter, nullptr);
    ASSERT_NE(main_splitter->widget(0), nullptr);
    EXPECT_EQ(main_splitter->widget(0)->minimumWidth(), 520);

    auto source_view = makePreviewView("native_source_preview_defaults");
    ASSERT_NE(source_view->getViewWidget(), nullptr);

    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    ASSERT_NE(preview_view->getViewWidget(), nullptr);

    auto* preview_area = preview_view->getRTPreviewAreaCheckBox();
    ASSERT_NE(preview_area, nullptr);
    EXPECT_FALSE(preview_area->isChecked());
    EXPECT_FALSE(preview_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_EQ(preview_view->getViewWidget()->presentationRequest().promotion, an::PresentationPromotion::Gradient)
        << "a stated high-quality default changes nothing: every view carries each sample at full precision";
    QComboBox* display_mode = nullptr;
    const auto preview_combos = preview_view->findChildren<QComboBox*>();
    for (QComboBox* combo : preview_combos)
    {
        if (combo && combo->accessibleName() == QStringLiteral("Display Mode"))
        {
            display_mode = combo;
            break;
        }
    }
    ASSERT_NE(display_mode, nullptr);
    // Offered on the preview's toolbar: its entry is visible, wherever the toolbar's overflow puts the widget while the
    // window is not shown.
    QAction* display_mode_entry = nullptr;
    for (QToolBar* toolbar : preview_view->findChildren<QToolBar*>())
    {
        for (QAction* entry : toolbar->actions())
        {
            if (toolbar->widgetForAction(entry) == display_mode)
            {
                display_mode_entry = entry;
            }
        }
    }
    ASSERT_NE(display_mode_entry, nullptr);
    EXPECT_TRUE(display_mode_entry->isVisible());
    EXPECT_NE(display_mode->findText(QStringLiteral("Autostretch")), -1);
    EXPECT_EQ(display_mode->currentText(), QStringLiteral("Autostretch"));

    auto* strength_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    auto* strength_slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(strength_spin, nullptr);
    ASSERT_NE(strength_slider, nullptr);
    EXPECT_EQ(strength_spin->toolTip(), QStringLiteral("Controls how strongly the stretch is applied."));
    EXPECT_EQ(strength_slider->toolTip(), QStringLiteral("Controls how strongly the stretch is applied."));

    preview_area->setChecked(true);
    QCoreApplication::processEvents();

    EXPECT_TRUE(preview_area->isChecked());
    EXPECT_TRUE(preview_view->getViewWidget()->isSelectionAreaEnabled());
    EXPECT_FALSE(window.previewArea().isEmpty());

    window.setSourceView(nullptr);
    EXPECT_EQ(window.previewView(), nullptr);
}

TEST(ExtensionNativeProcessWindowTest, RtPreviewMetaHidesHeaderProgressUsesPreviewSpinnerAndLeftReset)
{
    TestHelpers::ScopedEnvVar ui_tokens_enabled("AN_UI_TOKENS_ENABLED", "1");

    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-preview-spinner",
                                                            nullptr);
    window.setKeepOpenMode(true);
    const auto gate = std::make_shared<PreviewFrameGate>();
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(gatedPreviewBody(gate, frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("header_progress"), false},
    }});

    window.show();
    QTest::qWait(60);

    auto* action_bar = window.findChild<QWidget*>(QStringLiteral("anNativeProcessActionBar"));
    auto* reset_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessResetButton"));
    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    auto* close_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessCloseButton"));
    auto* header_banner = window.findChild<QWidget*>(QStringLiteral("anProcessShellBanner"));
    auto* header_message = window.findChild<QLabel*>(QStringLiteral("anProcessShellBannerMessage"));
    auto* header_progress = window.findChild<QProgressBar*>(QStringLiteral("anProcessShellBannerProgress"));

    ASSERT_NE(action_bar, nullptr);
    ASSERT_NE(reset_button, nullptr);
    ASSERT_NE(execute_button, nullptr);
    ASSERT_NE(close_button, nullptr);
    ASSERT_NE(header_banner, nullptr);
    ASSERT_NE(header_message, nullptr);
    ASSERT_NE(header_progress, nullptr);

    EXPECT_EQ(reset_button->parentWidget(), action_bar);
    EXPECT_TRUE(reset_button->isVisibleTo(&window));

    const int action_row_left = action_bar->mapToGlobal(QPoint(0, 0)).x();
    const int reset_left = reset_button->mapToGlobal(QPoint(0, 0)).x();
    const int reset_right = reset_button->mapToGlobal(QPoint(reset_button->width(), 0)).x();
    const int execute_left = execute_button->mapToGlobal(QPoint(0, 0)).x();
    const int reset_center_y = reset_button->mapToGlobal(reset_button->rect().center()).y();
    const int execute_center_y = execute_button->mapToGlobal(execute_button->rect().center()).y();

    EXPECT_LE(std::abs(reset_left - action_row_left), an::ui::tokens().spacing.md);
    EXPECT_GT(execute_left, reset_right + an::ui::tokens().spacing.xl);
    EXPECT_NEAR(reset_center_y, execute_center_y, 8);

    window.showProcessMessage(QStringLiteral("Working"), QStringLiteral("Rendering preview"),
                              TopLevelProcessBaseControl::ProcessMessageTone::Info, 25.0);

    EXPECT_FALSE(window.isHeaderProgressVisible());
    EXPECT_TRUE(header_banner->isHidden());
    EXPECT_TRUE(header_message->text().isEmpty());
    EXPECT_FALSE(header_progress->isVisible());

    // The preview's spinner is the framework's, over the preview view: shown while a frame outlasts its delay.
    gate->close();
    auto source_view = makePreviewView("native_source_preview_spinner");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    WaitingSpinnerWidget* spinner = findSpinner(*preview_view);
    ASSERT_NE(spinner, nullptr);
    ASSERT_TRUE(waitUntil([&]() { return gate->waiting() == 1; }));
    EXPECT_TRUE(waitUntil([&]() { return spinner->isSpinning(); }, 3000));
    EXPECT_TRUE(spinner->isVisibleTo(&window));
    EXPECT_TRUE(header_banner->isHidden()) << "a frame shows no header progress";

    // An Apply stops the frame on its way and its spinner with it; no frame starts while it runs, and one frame of the
    // parameters it leaves follows it.
    window.setExecutionInProgress(true);
    EXPECT_TRUE(waitUntil([&]() { return !spinner->isSpinning() && window.isRunIdle(); }));
    EXPECT_FALSE(window.isPreviewInProgress());
    gate->open();
    const int arrived = gate->arrived();
    QTest::qWait(200);
    EXPECT_EQ(gate->arrived(), arrived);
    window.setExecutionInProgress(false);
    EXPECT_TRUE(waitUntil([&]() { return gate->arrived() == arrived + 1 && window.isProcessIdle(); }));
    EXPECT_FALSE(spinner->isSpinning());
}

// An extension that hides its process window for a dialog of its own closes the window when its dialog closes: the
// close ends the session whether the window was ever shown or not, as the window's Close button does.
TEST(ExtensionNativeProcessWindowTest, ARequestedCloseEndsTheSessionWhetherOrNotTheWindowWasShown)
{
    TestHelpers::ScopedEnvVar ui_tokens_enabled("AN_UI_TOKENS_ENABLED", "1");

    struct Setup
    {
        const char* name;
        std::function<void(an::ui::extensions::ExtensionNativeProcessWindow&)> prepare;
    };
    const std::vector<Setup> setups{{"hidden before it was shown", [](auto& window) { window.hide(); }},
                                    {"hidden with no platform window",
                                     [](auto& window) {
                                         static_cast<void>(window.winId());
                                         if (QWindow* handle = window.windowHandle())
                                         {
                                             handle->destroy();
                                         }
                                         window.hide();
                                     }},
                                    {"shown, then hidden",
                                     [](auto& window) {
                                         window.show();
                                         QTest::qWait(30);
                                         window.hide();
                                     }},
                                    {"shown", [](auto& window) {
                                         window.show();
                                         QTest::qWait(30);
                                     }}};
    for (const Setup& setup : setups)
    {
        SCOPED_TRACE(setup.name);
        an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-requested-close", nullptr);
        int close_count = 0;
        window.setCloseCallback([&close_count]() { ++close_count; });
        setup.prepare(window);

        window.requestClose();
        QCoreApplication::processEvents();
        EXPECT_EQ(close_count, 1) << "the close reached the session";
        EXPECT_FALSE(window.isVisible());
    }

    // With nothing to tell, the window closes as any window does.
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-requested-close-plain", nullptr);
    window.show();
    QTest::qWait(30);
    window.requestClose();
    QCoreApplication::processEvents();
    EXPECT_FALSE(window.isVisible());
}

TEST(ExtensionNativeProcessWindowTest, FullExecuteCancelButtonCancelsWithoutClosing)
{
    TestHelpers::ScopedEnvVar ui_tokens_enabled("AN_UI_TOKENS_ENABLED", "1");

    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-execute-cancel",
                                                            nullptr);
    int close_count = 0;
    int cancel_count = 0;
    window.setCloseCallback([&]() { ++close_count; });
    window.setCancelCallback([&]() { ++cancel_count; });

    window.show();
    QTest::qWait(30);

    auto* button_box = window.findChild<QDialogButtonBox*>();
    auto* close_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessCloseButton"));
    ASSERT_NE(button_box, nullptr);
    ASSERT_NE(close_button, nullptr);
    EXPECT_EQ(button_box->buttonRole(close_button), QDialogButtonBox::ActionRole);
    EXPECT_EQ(button_box->button(QDialogButtonBox::Close), nullptr);

    window.setExecutionInProgress(true);
    EXPECT_EQ(close_button->text(), QStringLiteral("Cancel"));

    QTest::mouseClick(close_button, Qt::LeftButton);
    QCoreApplication::processEvents();

    EXPECT_EQ(cancel_count, 1);
    EXPECT_EQ(close_count, 0);
    EXPECT_TRUE(window.isVisible());
    EXPECT_TRUE(window.isExecutionInProgress());
    EXPECT_TRUE(window.executionCancelRequested());
    EXPECT_FALSE(window.shouldCloseAfterExecute());
    EXPECT_FALSE(window.shouldCloseAfterExecutionCancel());

    window.close();
    QCoreApplication::processEvents();

    EXPECT_EQ(cancel_count, 1);
    EXPECT_EQ(close_count, 1);
    EXPECT_FALSE(window.isVisible());

    window.setExecutionInProgress(false);
    window.show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window.isVisible());
    EXPECT_EQ(close_button->text(), QStringLiteral("Close"));

    window.setExecutionInProgress(true);
    window.requestCloseAfterExecutionCancel();
    EXPECT_TRUE(window.executionCancelRequested());
    EXPECT_TRUE(window.shouldCloseAfterExecutionCancel());
    window.setExecutionInProgress(false);
    EXPECT_TRUE(window.shouldCloseAfterExecutionCancel());

    window.clearExecutionCancelRequested();
    EXPECT_FALSE(window.executionCancelRequested());
    EXPECT_FALSE(window.shouldCloseAfterExecutionCancel());
    QTest::mouseClick(close_button, Qt::LeftButton);
    QCoreApplication::processEvents();

    EXPECT_EQ(cancel_count, 1);
    EXPECT_EQ(close_count, 2);
    EXPECT_FALSE(window.isVisible());
}

TEST(ExtensionNativeProcessWindowTest, PreviewAreaRestoresHoverTrackingAfterBaselineLoad)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window",
                                                            "session-preview-hover-tracking", nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("sub_area"), true},
        {QStringLiteral("sub_area_size"), QJsonArray{3, 2}},
    }});

    auto source_view = makePreviewView("native_source_hover_tracking");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    ASSERT_NE(preview_view->getScrollArea(), nullptr);

    // Loading an image with no solution turns a view's hover tracking off; the preview area's resize cursors need it,
    // after the preview's first image and after the source shows another.
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    EXPECT_TRUE(preview_view->getViewWidget()->hasMouseTracking());

    showGrey(*source_view, 10, 8, 0.5);
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->firstSample == 0.5 && window.isProcessIdle();
    }));
    ASSERT_EQ(window.previewView(), preview_view);
    EXPECT_TRUE(preview_view->getViewWidget()->hasMouseTracking());
}

TEST(ExtensionNativeProcessWindowTest, SubAreaPreviewCompositesIntoFullImageAndLeavesSourceUntouched)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-preview-composite",
                                                            nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>(2.0);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("sub_area"), true},
        {QStringLiteral("sub_area_size"), QJsonArray{2, 2}},
    }});

    auto source_view = makePreviewView("native_source_composite");
    showGrey(*source_view, 5, 4, 1.0);
    an::tests::host_native::ViewEventCounter source_events(*source_view);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);

    preview_view->getViewWidget()->updateSelectionArea(QRect(1, 1, 2, 2));
    window.selectionAreaChanged();
    ASSERT_EQ(window.previewArea(), QRect(1, 1, 2, 2));

    // What the extension made of the area is shown where the area lies, over the image; the source is left as it was.
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->part == QRect(1, 1, 2, 2) && window.isProcessIdle() &&
               std::abs(viewSampleAt(*preview_view, 1, 1) - 2.0) < 1e-6;
    }));
    EXPECT_NEAR(viewSampleAt(*preview_view, 0, 0), 1.0, 1e-6);
    EXPECT_NEAR(viewSampleAt(*preview_view, 2, 2), 2.0, 1e-6);
    EXPECT_NEAR(viewSampleAt(*preview_view, 3, 3), 1.0, 1e-6);
    EXPECT_NEAR(viewSampleAt(*source_view, 1, 1), 1.0, 1e-6);
    EXPECT_EQ(source_events.updated, 0);
    EXPECT_EQ(source_events.loaded, 0);
}

TEST(ExtensionNativeProcessWindowTest, SubAreaPreviewCompositePromotesMonoBaseForRgbPatch)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window",
                                                            "session-preview-composite-rgb-patch", nullptr);
    // The extension answers a colour patch for a grey image.
    const auto frames = std::make_shared<PreviewFrameRecord>(0.4, 3);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("window_meta")},
        {QStringLiteral("type"), QStringLiteral("meta")},
        {QStringLiteral("sub_area"), true},
        {QStringLiteral("sub_area_size"), QJsonArray{2, 2}},
    }});

    auto source_view = makePreviewView("native_source_composite_rgb_patch");
    showGrey(*source_view, 5, 4, 1.0);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);

    preview_view->getViewWidget()->updateSelectionArea(QRect(1, 1, 2, 2));
    window.selectionAreaChanged();
    ASSERT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return last.has_value() && last->part == QRect(1, 1, 2, 2) && window.isProcessIdle() &&
               std::abs(viewSampleAt(*preview_view, 1, 1) - 0.4) < 1e-6;
    }));

    // The image around the patch is shown in the patch's colours: grey promoted to three equal channels.
    ASSERT_TRUE(preview_view->imageDescriptor().has_value());
    EXPECT_EQ(preview_view->imageDescriptor()->extent().width(), 5u);
    EXPECT_EQ(preview_view->imageDescriptor()->extent().height(), 4u);
    ASSERT_EQ(preview_view->imageDescriptor()->extent().channelCount(), 3u);
    ASSERT_TRUE(source_view->imageDescriptor().has_value());
    EXPECT_EQ(source_view->imageDescriptor()->extent().channelCount(), 1u) << "the source is left grey";
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(viewSampleAt(*preview_view, 0, 0, channel), 1.0, 1e-6) << channel;
        EXPECT_NEAR(viewSampleAt(*preview_view, 1, 1, channel), 0.4, 1e-6) << channel;
    }
}

TEST(ExtensionNativeProcessWindowTest, PreviewLayoutUsesModernShellAndRtPreviewSplitPane)
{
    TestHelpers::ScopedEnvVar ui_tokens_enabled("AN_UI_TOKENS_ENABLED", "1");

    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-split-pane", nullptr);
    window.setParamDefs(QJsonArray{QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("strength")},
                                       {QStringLiteral("type"), QStringLiteral("double")},
                                       {QStringLiteral("label"), QStringLiteral("Strength")},
                                       {QStringLiteral("default"), 0.25},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("preserve_background")},
                                       {QStringLiteral("type"), QStringLiteral("bool")},
                                       {QStringLiteral("label"), QStringLiteral("Preserve background")},
                                       {QStringLiteral("default"), true},
                                   }});

    window.show();
    QTest::qWait(30);

    auto* shell_card = window.findChild<QWidget*>(QStringLiteral("anProcessShellCard"));
    auto* shell_content = window.findChild<QWidget*>(QStringLiteral("anProcessShellContent"));
    auto* main_splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    auto* action_bar = window.findChild<QWidget*>(QStringLiteral("anNativeProcessActionBar"));
    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));

    ASSERT_NE(shell_card, nullptr);
    ASSERT_NE(shell_content, nullptr);
    ASSERT_NE(main_splitter, nullptr);
    ASSERT_NE(param_scroll, nullptr);
    ASSERT_NE(action_bar, nullptr);
    ASSERT_NE(execute_button, nullptr);

    EXPECT_TRUE(shell_card->isVisible());
    EXPECT_EQ(main_splitter->parentWidget(), shell_content);
    EXPECT_EQ(main_splitter->widget(0)->objectName(), QStringLiteral("controlsPanel"));
    EXPECT_EQ(main_splitter->widget(1)->objectName(), QStringLiteral("previewPanel"));
    EXPECT_EQ(main_splitter->property("anSplitterVariant").toString(), QStringLiteral("process"));
    EXPECT_EQ(main_splitter->widget(1)->minimumWidth(), an::ui::processes::rt_preview_ui::kMinPreviewPaneWidth);

    auto* controls_panel = main_splitter->widget(0);
    ASSERT_NE(controls_panel, nullptr);

    const int scroll_top = param_scroll->mapToGlobal(QPoint(0, 0)).y();
    const int action_bar_top = action_bar->mapToGlobal(QPoint(0, 0)).y();
    const int shell_card_bottom = shell_card->mapToGlobal(QPoint(0, shell_card->height())).y();
    EXPECT_LT(scroll_top, action_bar_top);
    EXPECT_LE(shell_card_bottom, action_bar_top);
}

TEST(ExtensionNativeProcessWindowTest, BasicNativeControlsColumnDoesNotInsertExtraStretchSpacer)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-controls-height",
                                                            nullptr);
    window.setParamDefs(QJsonArray{QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("strength")},
                                       {QStringLiteral("type"), QStringLiteral("double")},
                                       {QStringLiteral("label"), QStringLiteral("Strength")},
                                       {QStringLiteral("default"), 0.25},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("preserve_background")},
                                       {QStringLiteral("type"), QStringLiteral("bool")},
                                       {QStringLiteral("label"), QStringLiteral("Preserve background")},
                                       {QStringLiteral("default"), true},
                                   }});

    auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    auto* action_bar = window.findChild<QWidget*>(QStringLiteral("anNativeProcessActionBar"));
    auto* button_box = window.findChild<QDialogButtonBox*>();
    auto* main_splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));

    ASSERT_NE(param_scroll, nullptr);
    ASSERT_NE(action_bar, nullptr);
    ASSERT_NE(button_box, nullptr);
    ASSERT_NE(main_splitter, nullptr);

    auto* controls_panel = main_splitter->widget(0);
    ASSERT_NE(controls_panel, nullptr);

    auto* controls_layout = qobject_cast<QVBoxLayout*>(controls_panel->layout());
    ASSERT_NE(controls_layout, nullptr);

    ASSERT_EQ(controls_layout->count(), 2);
    EXPECT_EQ(controls_layout->itemAt(0)->widget(), param_scroll);
    EXPECT_EQ(controls_layout->itemAt(1)->widget(), action_bar);
    EXPECT_EQ(button_box->parentWidget(), action_bar);
}

TEST(ExtensionNativeProcessWindowTest, CustomPanelVariantUsesCollapsibleSections)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Custom Demo", "session-custom", nullptr);

    window.setParamDefs(QJsonArray{QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("meta")},
                                       {QStringLiteral("type"), QStringLiteral("meta")},
                                       {QStringLiteral("panel_variant"), QStringLiteral("custom")},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("look")},
                                       {QStringLiteral("type"), QStringLiteral("section")},
                                       {QStringLiteral("label"), QStringLiteral("Look")},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("summary")},
                                       {QStringLiteral("type"), QStringLiteral("info")},
                                       {QStringLiteral("text"), QStringLiteral("Custom layout banner")},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("strength")},
                                       {QStringLiteral("type"), QStringLiteral("double")},
                                       {QStringLiteral("label"), QStringLiteral("Strength")},
                                       {QStringLiteral("default"), 0.3},
                                   }});

    int collapsible_count = 0;
    for (QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->objectName() != QStringLiteral("anNativeParamSectionContainer"))
        {
            continue;
        }

        if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
        {
            ++collapsible_count;
            EXPECT_EQ(section->title(), QStringLiteral("Look"));
        }
    }
    EXPECT_EQ(collapsible_count, 1);
}

TEST(ExtensionNativeProcessWindowTest, TabParamDefsGroupRowsIntoNativeTabs)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("VeraLux Vectra", "session-native-tabs", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("vector_tabs")},
            {QStringLiteral("type"), QStringLiteral("tabs")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("primary_vectors")},
            {QStringLiteral("type"), QStringLiteral("tab")},
            {QStringLiteral("label"), QStringLiteral("Primary Vectors")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("red_vector")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Red Vector (0 deg)")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("red_hue")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Hue Shift")},
            {QStringLiteral("default"), 0.0},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("secondary_vectors")},
            {QStringLiteral("type"), QStringLiteral("tab")},
            {QStringLiteral("label"), QStringLiteral("Secondary Vectors")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("cyan_vector")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Cyan Vector (180 deg)")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("cyan_hue")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Hue Shift")},
            {QStringLiteral("default"), 0.0},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("end_vector_tabs")},
            {QStringLiteral("type"), QStringLiteral("end_tabs")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("protection")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Protection")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("protect_stars")},
            {QStringLiteral("type"), QStringLiteral("bool")},
            {QStringLiteral("label"), QStringLiteral("White Star Integrity")},
            {QStringLiteral("default"), true},
        },
    });

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("anNativeParamTabs"));
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("Primary Vectors"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("Secondary Vectors"));

    QWidget* primary_page = tabs->widget(0);
    QWidget* secondary_page = tabs->widget(1);
    ASSERT_NE(primary_page, nullptr);
    ASSERT_NE(secondary_page, nullptr);

    EXPECT_NE(findSectionById(*primary_page, QStringLiteral("red_vector")), nullptr);
    EXPECT_NE(findParamRow(*primary_page, QStringLiteral("red_hue")), nullptr);
    EXPECT_EQ(findParamRow(*primary_page, QStringLiteral("cyan_hue")), nullptr);

    EXPECT_NE(findSectionById(*secondary_page, QStringLiteral("cyan_vector")), nullptr);
    EXPECT_NE(findParamRow(*secondary_page, QStringLiteral("cyan_hue")), nullptr);
    EXPECT_EQ(findParamRow(*secondary_page, QStringLiteral("red_hue")), nullptr);

    EXPECT_EQ(findSectionById(*primary_page, QStringLiteral("protection")), nullptr);
    EXPECT_NE(findSectionById(window, QStringLiteral("protection")), nullptr);
    EXPECT_NE(findParamRow(window, QStringLiteral("protect_stars")), nullptr);
}

TEST(ExtensionNativeProcessWindowTest, TargetSelectorTracksAddedRenamedAndRemovedViews)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-target-selector",
                                                            nullptr);

    window.enableTargetSelector({{
                                     {"name", QStringLiteral("Alpha")},
                                     {"file_name", QStringLiteral("alpha.fit")},
                                     {"selector_tooltip", QStringLiteral("<b>Alpha</b><br><b>Source:</b> alpha.fit")},
                                     {"channels", 1},
                                 },
                                 {
                                     {"name", QStringLiteral("Beta")},
                                     {"file_name", QStringLiteral("beta.fit")},
                                     {"channels", 3},
                                 }},
                                {1, 3});

    auto* selector = window.findChild<QComboBox*>(QStringLiteral("anNativeProcessTargetSelector"));
    ASSERT_NE(selector, nullptr);
    ASSERT_EQ(selector->count(), 2);
    EXPECT_NE(selector->findText(QStringLiteral("Alpha")), -1);
    EXPECT_NE(selector->findText(QStringLiteral("Beta")), -1);
    EXPECT_EQ(selector->itemData(selector->findText(QStringLiteral("Alpha")), Qt::ToolTipRole).toString(),
              QStringLiteral("<b>Alpha</b><br><b>Source:</b> alpha.fit"));

    selector->setCurrentText(QStringLiteral("Beta"));

    window.addOrUpdateTargetSelectorView({
        {"name", QStringLiteral("Gamma")},
        {"file_name", QStringLiteral("gamma.fit")},
        {"selector_tooltip", QStringLiteral("<b>Gamma</b><br><b>Source:</b> In-memory file")},
        {"channels", 1},
    });

    EXPECT_EQ(selector->count(), 3);
    EXPECT_NE(selector->findText(QStringLiteral("Gamma")), -1);
    EXPECT_EQ(selector->itemData(selector->findText(QStringLiteral("Gamma")), Qt::ToolTipRole).toString(),
              QStringLiteral("<b>Gamma</b><br><b>Source:</b> In-memory file"));

    window.renameTargetSelectorView(QStringLiteral("Beta"), QStringLiteral("Beta_Renamed"),
                                    QStringLiteral("beta_renamed.fit"),
                                    QStringLiteral("<b>Beta_Renamed</b><br><b>Source:</b> beta_renamed.fit"));

    EXPECT_EQ(selector->findText(QStringLiteral("Beta")), -1);
    EXPECT_NE(selector->findText(QStringLiteral("Beta_Renamed")), -1);
    EXPECT_EQ(selector->currentText(), QStringLiteral("Beta_Renamed"));
    EXPECT_EQ(selector->itemData(selector->findText(QStringLiteral("Beta_Renamed")), Qt::ToolTipRole).toString(),
              QStringLiteral("<b>Beta_Renamed</b><br><b>Source:</b> beta_renamed.fit"));

    window.removeTargetSelectorView(QStringLiteral("Gamma"));
    EXPECT_EQ(selector->count(), 2);
    EXPECT_EQ(selector->findText(QStringLiteral("Gamma")), -1);
}

TEST(ExtensionNativeProcessWindowTest, TargetSelectorHonorsChannelFilterForNewViews)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window",
                                                            "session-target-selector-filter", nullptr);

    window.enableTargetSelector({}, {1});

    auto* selector = window.findChild<QComboBox*>(QStringLiteral("anNativeProcessTargetSelector"));
    ASSERT_NE(selector, nullptr);

    window.addOrUpdateTargetSelectorView({
        {"name", QStringLiteral("Mono")},
        {"file_name", QStringLiteral("mono.fit")},
        {"channels", 1},
    });
    window.addOrUpdateTargetSelectorView({
        {"name", QStringLiteral("RGB")},
        {"file_name", QStringLiteral("rgb.fit")},
        {"channels", 3},
    });

    EXPECT_NE(selector->findText(QStringLiteral("Mono")), -1);
    EXPECT_EQ(selector->findText(QStringLiteral("RGB")), -1);
}

TEST(ExtensionNativeProcessWindowTest, TargetSelectorRendersLabeledSectionAboveActionButtons)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window",
                                                            "session-target-selector-layout", nullptr);

    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
    }});
    window.enableTargetSelector({{
                                    {"name", QStringLiteral("Alpha")},
                                    {"file_name", QStringLiteral("alpha.fit")},
                                    {"channels", 1},
                                }},
                                {1});

    window.show();
    QTest::qWait(30);

    auto* selector_section = window.findChild<QWidget*>(QStringLiteral("anNativeProcessTargetSelectorSection"));
    auto* selector_label = window.findChild<QLabel*>(QStringLiteral("anNativeProcessTargetSelectorLabel"));
    auto* selector = window.findChild<QComboBox*>(QStringLiteral("anNativeProcessTargetSelector"));
    auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    auto* button_box = window.findChild<QDialogButtonBox*>();
    auto* main_splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));

    ASSERT_NE(selector_section, nullptr);
    ASSERT_NE(selector_label, nullptr);
    ASSERT_NE(selector, nullptr);
    ASSERT_NE(param_scroll, nullptr);
    ASSERT_NE(button_box, nullptr);
    ASSERT_NE(main_splitter, nullptr);

    auto* controls_panel = main_splitter->widget(0);
    ASSERT_NE(controls_panel, nullptr);

    auto* controls_layout = qobject_cast<QVBoxLayout*>(controls_panel->layout());
    ASSERT_NE(controls_layout, nullptr);

    auto* selector_layout = qobject_cast<QFormLayout*>(selector_section->layout());
    ASSERT_NE(selector_layout, nullptr);

    EXPECT_EQ(selector_label->text(), QStringLiteral("Target Image"));
    EXPECT_EQ(controls_layout->count(), 2);
    EXPECT_EQ(controls_layout->indexOf(param_scroll), 0);
    EXPECT_EQ(controls_layout->indexOf(selector_section), 1);
    ASSERT_NE(selector_layout->itemAt(0, QFormLayout::LabelRole), nullptr);
    ASSERT_NE(selector_layout->itemAt(0, QFormLayout::FieldRole), nullptr);
    EXPECT_EQ(selector_layout->itemAt(0, QFormLayout::LabelRole)->widget(), selector_label);
    EXPECT_EQ(selector_layout->itemAt(0, QFormLayout::FieldRole)->widget(), selector);

    const int selector_label_center_y = selector_label->mapToGlobal(selector_label->rect().center()).y();
    const int selector_center_y = selector->mapToGlobal(selector->rect().center()).y();
    const int selector_label_left = selector_label->mapToGlobal(QPoint(0, 0)).x();
    const int selector_left = selector->mapToGlobal(QPoint(0, 0)).x();

    const int param_scroll_top = param_scroll->mapToGlobal(QPoint(0, 0)).y();
    const int selector_section_top = selector_section->mapToGlobal(QPoint(0, 0)).y();
    const int button_box_top = button_box->mapToGlobal(QPoint(0, 0)).y();

    EXPECT_LT(param_scroll_top, selector_section_top);
    EXPECT_LT(selector_section_top, button_box_top);
    EXPECT_NEAR(selector_label_center_y, selector_center_y, 6);
    EXPECT_LT(selector_label_left, selector_left);
}

TEST(ExtensionNativeProcessWindowTest, TargetSelectorCanRenderInlineInsideParamSection)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window",
                                                            "session-target-selector-inline", nullptr);

    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("window_meta")},
            {QStringLiteral("type"), QStringLiteral("meta")},
            {QStringLiteral("target_selector"), true},
            {QStringLiteral("target_channel_filter"), QJsonArray{1, 3}},
            {QStringLiteral("target_selector_position"), QStringLiteral("params")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("inputs")},
            {QStringLiteral("type"), QStringLiteral("section")},
            {QStringLiteral("label"), QStringLiteral("Inputs")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("starless_view")},
            {QStringLiteral("type"), QStringLiteral("target_selector")},
            {QStringLiteral("label"), QStringLiteral("Starless (stretched)")},
            {QStringLiteral("tooltip"), QStringLiteral("Target starless base image.")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("stars_view")},
            {QStringLiteral("type"), QStringLiteral("view_selector")},
            {QStringLiteral("label"), QStringLiteral("Stars (linear)")},
            {QStringLiteral("default"), QString()},
            {QStringLiteral("match_target_geometry"), true},
            {QStringLiteral("match_target_channels"), true},
            {QStringLiteral("exclude_target"), true},
            {QStringLiteral("channel_filter"), QJsonArray{1, 3}},
        },
    });
    window.enableTargetSelector({{
                                     {"name", QStringLiteral("Starless")},
                                     {"file_name", QStringLiteral("starless.fit")},
                                     {"width", 100},
                                     {"height", 80},
                                     {"channels", 1},
                                 },
                                 {
                                     {"name", QStringLiteral("Stars")},
                                     {"file_name", QStringLiteral("stars.fit")},
                                     {"width", 100},
                                     {"height", 80},
                                     {"channels", 1},
                                 }},
                                {1, 3});

    auto* inputs_section = findSectionById(window, QStringLiteral("inputs"));
    auto* starless_row = findParamRow(window, QStringLiteral("starless_view"));
    auto* stars_row = findParamRow(window, QStringLiteral("stars_view"));
    auto* target_selector = window.findChild<QComboBox*>(QStringLiteral("anNativeProcessTargetSelector"));
    auto* stars_selector = window.findChild<QComboBox*>(QStringLiteral("anNativeParamViewSelector"));
    auto* param_scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));

    ASSERT_NE(inputs_section, nullptr);
    ASSERT_NE(starless_row, nullptr);
    ASSERT_NE(stars_row, nullptr);
    ASSERT_NE(target_selector, nullptr);
    ASSERT_NE(stars_selector, nullptr);
    ASSERT_NE(param_scroll, nullptr);

    EXPECT_TRUE(isDescendantOf(starless_row, inputs_section));
    EXPECT_TRUE(isDescendantOf(stars_row, inputs_section));
    EXPECT_TRUE(isDescendantOf(target_selector, starless_row));
    EXPECT_FALSE(window.currentParams().contains(QStringLiteral("starless_view")));
    EXPECT_EQ(window.selectedTargetView(), "Starless");
    EXPECT_NE(stars_selector->findText(QStringLiteral("Stars")), -1);

    window.show();
    QTest::qWait(30);

    const int starless_y = starless_row->mapToGlobal(QPoint(0, 0)).y();
    const int stars_y = stars_row->mapToGlobal(QPoint(0, 0)).y();
    const int param_scroll_y = param_scroll->mapToGlobal(QPoint(0, 0)).y();
    EXPECT_GE(starless_y, param_scroll_y);
    EXPECT_LT(starless_y, stars_y);
}

TEST(ExtensionNativeProcessWindowTest, ViewAreaVisibilityControlsSourceViewEmbedding)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Non-Preview Process", "session-no-view", nullptr);

    EXPECT_EQ(window.windowTitle(), QStringLiteral("Non-Preview Process"));
    EXPECT_TRUE(window.isViewAreaVisible());

    window.setViewAreaVisible(false);
    EXPECT_FALSE(window.isViewAreaVisible());

    // Source view should still be logically attached for execute even when
    // the view area is invisible.
    auto source_view = makePreviewView("hidden_source");
    window.setSourceView(source_view.get());
    EXPECT_EQ(window.sourceView(), source_view.get());

    auto* execute_button = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    ASSERT_NE(execute_button, nullptr);
    int execute_count = 0;
    window.setExecuteCallback([&execute_count]() { ++execute_count; });
    execute_button->click();
    EXPECT_EQ(execute_count, 1);

    window.setSourceView(nullptr);
    window.setViewAreaVisible(true);
    EXPECT_TRUE(window.isViewAreaVisible());
}

TEST(ExtensionNativeProcessWindowTest, HiddenViewAreaShrinksWindowToContents)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Execute Only Process", "session-fit-window", nullptr);

    window.show();
    QTest::qWait(30);
    const int initial_height = window.height();

    window.setViewAreaVisible(false);
    window.setParamDefs(QJsonArray{QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("strength")},
                                       {QStringLiteral("type"), QStringLiteral("double")},
                                       {QStringLiteral("label"), QStringLiteral("Strength")},
                                       {QStringLiteral("default"), 0.4},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("mode")},
                                       {QStringLiteral("type"), QStringLiteral("choice")},
                                       {QStringLiteral("label"), QStringLiteral("Mode")},
                                       {QStringLiteral("default"), QStringLiteral("balanced")},
                                       {QStringLiteral("options"),
                                        QJsonArray{
                                            QJsonArray{QStringLiteral("Balanced"), QStringLiteral("balanced")},
                                            QJsonArray{QStringLiteral("Gentle"), QStringLiteral("gentle")},
                                        }},
                                   }});

    QTest::qWait(30);
    EXPECT_LT(window.height(), initial_height);
}

TEST(ExtensionNativeProcessWindowTest, PreviewSurfaceFalseStillSchedulesAutoPreview)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Standalone Preview", "session-standalone-preview",
                                                            nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("window_meta")},
                                       {QStringLiteral("type"), QStringLiteral("meta")},
                                       {QStringLiteral("preview_surface"), false},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("preview_enabled")},
                                       {QStringLiteral("type"), QStringLiteral("bool")},
                                       {QStringLiteral("label"), QStringLiteral("Enable Cached Preview")},
                                       {QStringLiteral("default"), true},
                                   }});

    // A window that previews shows its frames in its preview view whatever the meta says of a surface.
    auto source_view = makePreviewView("standalone_source");
    showGrey(*source_view, 5, 4, 1.0);
    window.setSourceView(source_view.get());
    ASSERT_NE(window.previewView(), nullptr);
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));

    frames->clear();
    auto* preview_enabled = window.findChild<QCheckBox*>(QStringLiteral("anNativeParamCheckBox"));
    ASSERT_NE(preview_enabled, nullptr);
    preview_enabled->setChecked(false);

    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    EXPECT_FALSE(frames->last()->params.value(QStringLiteral("preview_enabled")).toBool(true));
}

TEST(ExtensionNativeProcessWindowTest, APreviewNeverWritesTheSourceView)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Standalone Preview", "session-standalone-preview-update",
                                                            nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>(2.0);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);

    auto source_view = makePreviewView("standalone_source_update");
    showGrey(*source_view, 5, 4, 1.0);
    const std::shared_ptr<const an::consumer::ImageAsset> image = source_view->document().asset();
    an::tests::host_native::ViewEventCounter events(*source_view);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);

    ASSERT_TRUE(waitUntil([&]() {
        return frames->count() >= 1 && window.isProcessIdle() &&
               std::abs(viewSampleAt(*preview_view, 0, 0) - 2.0) < 1e-6;
    }));

    // What an Apply runs over is the source view's own image, as it was: never a frame's result.
    EXPECT_EQ(window.sourceImage(), image);
    EXPECT_EQ(source_view->document().asset(), image);
    EXPECT_NEAR(viewSampleAt(*source_view, 0, 0), 1.0, 1e-6);
    EXPECT_EQ(events.updated, 0);
    EXPECT_EQ(events.loaded, 0);
}

TEST(ExtensionNativeProcessWindowTest, CommittedResultBecomesTheOriginalAndTheBaseline)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("RT Preview Native Window", "session-committed-result",
                                                            nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    auto source_view = makePreviewView("committed_source");
    showGrey(*source_view, 5, 4, 1.0);
    window.setSourceView(source_view.get());
    ImageViewControl* preview_view = window.previewView();
    ASSERT_NE(preview_view, nullptr);
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    EXPECT_EQ(frames->last()->firstSample, 1.0);

    window.applyCommittedResult(greyAsset(5, 4, 3.0));

    // The source view shows the result, the next frame runs over it, and the original the preview is compared with is
    // the result.
    EXPECT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames->last();
        return std::abs(viewSampleAt(*source_view, 0, 0) - 3.0) < 1e-6 && last.has_value() &&
               last->firstSample == 3.0 && window.isProcessIdle();
    })) << "the next preview starts from the result";
    EXPECT_EQ(window.sourceImage(), source_view->document().asset());
    const std::shared_ptr<const an::consumer::ImageAsset> original = preview_view->alternateAsset();
    ASSERT_TRUE(original);
    EXPECT_NEAR(assetSampleAt(*original, 0, 0), 3.0, 1e-6);

    window.setSourceView(nullptr);
    EXPECT_EQ(window.previewView(), nullptr);
}

TEST(ExtensionNativeProcessWindowTest, HostOwnedWindowDisablesDeleteOnClose)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Host Owned Window", "session-host-owned-window", nullptr);

    EXPECT_FALSE(window.testAttribute(Qt::WA_DeleteOnClose));
}

TEST(ExtensionNativeProcessWindowTest, CustomPanelVariantEmitsAccentSeparator)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Custom Demo Separator", "session-custom-sep", nullptr);

    window.setParamDefs(QJsonArray{QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("meta")},
                                       {QStringLiteral("type"), QStringLiteral("meta")},
                                       {QStringLiteral("panel_variant"), QStringLiteral("custom")},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("look")},
                                       {QStringLiteral("type"), QStringLiteral("section")},
                                       {QStringLiteral("label"), QStringLiteral("Look")},
                                   },
                                   QJsonObject{
                                       {QStringLiteral("id"), QStringLiteral("strength")},
                                       {QStringLiteral("type"), QStringLiteral("double")},
                                       {QStringLiteral("label"), QStringLiteral("Strength")},
                                       {QStringLiteral("default"), 0.3},
                                   }});

    auto* separator = window.findChild<QFrame*>(QStringLiteral("anCustomPanelSeparator"));
    EXPECT_NE(separator, nullptr);
}

// A parameter action's callback may answer later - once the frame on its way has landed - through the completion it is
// handed. The window applies the updates when the answer comes, and refuses another action until then: two answers to
// two clicks would race for the parameters.
TEST(ExtensionNativeProcessWindowTest, AParamActionAnsweringLaterAppliesItsUpdatesThenAndRefusesAnotherMeanwhile)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-async-param-action", nullptr);
    int action_count = 0;
    an::ui::extensions::ExtensionNativeProcessWindow::ParamActionCompletion pending;
    window.setParamActionCallback([&](const std::string& action_id, const QCborMap&,
                                      an::ui::extensions::ExtensionNativeProcessWindow::ParamActionCompletion done) {
        ++action_count;
        EXPECT_EQ(action_id, "auto_log_d");
        pending = std::move(done);
    });
    window.setParamDefs(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("auto_log_d")},
            {QStringLiteral("type"), QStringLiteral("button")},
            {QStringLiteral("label"), QStringLiteral("Auto-Calc Log D")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("log_d")},
            {QStringLiteral("type"), QStringLiteral("double")},
            {QStringLiteral("label"), QStringLiteral("Log D")},
            {QStringLiteral("default"), 2.0},
            {QStringLiteral("min"), 0.0},
            {QStringLiteral("max"), 7.0},
            {QStringLiteral("step"), 0.01},
        },
    });
    auto* action_button = window.findChild<QPushButton*>(QStringLiteral("anNativeParamActionButton"));
    auto* log_d_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("anNativeParamDoubleSpinBox"));
    ASSERT_NE(action_button, nullptr);
    ASSERT_NE(log_d_spin, nullptr);

    action_button->click();
    EXPECT_EQ(action_count, 1);
    ASSERT_TRUE(pending);
    EXPECT_DOUBLE_EQ(log_d_spin->value(), 2.0) << "the parameters changed before the action answered";
    action_button->click();
    EXPECT_EQ(action_count, 1) << "a second action ran while the first had yet to answer";

    pending(QCborMap{{QStringLiteral("log_d"), 3.25}});
    EXPECT_DOUBLE_EQ(log_d_spin->value(), 3.25);
    EXPECT_DOUBLE_EQ(window.currentParams().value(QStringLiteral("log_d")).toDouble(), 3.25);
    action_button->click();
    EXPECT_EQ(action_count, 2) << "an action after the answer was refused";
}

// What the GUI thread runs once the frame on its way has landed waits for that frame without turning the event loop
// itself: it runs at once when no frame is on its way, and when the frame lands otherwise, in the order it was asked.
// The host runs its own calls into the extension - an action, an Apply's validation, a target bound again - this way.
TEST(ExtensionNativeProcessWindowTest, WorkForWhenTheFramesHaveLandedRunsOnceTheFrameOnItsWayHas)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-when-run-idle", nullptr);
    window.setKeepOpenMode(true);
    const auto gate = std::make_shared<PreviewFrameGate>();
    const auto frames = std::make_shared<PreviewFrameRecord>();
    gate->close();
    window.setExecuteCallback([]() {});
    window.setPreviewBody(gatedPreviewBody(gate, frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.05},
    }});

    auto source_view = makePreviewView("when_run_idle_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(waitUntil([&]() { return gate->waiting() == 1; })) << "no frame reached the gate";
    ASSERT_FALSE(window.isRunIdle());

    std::vector<int> ran;
    window.whenRunIdle([&ran]() { ran.push_back(1); });
    window.whenRunIdle([&ran]() { ran.push_back(2); });
    QCoreApplication::processEvents();
    EXPECT_TRUE(ran.empty()) << "work for when the frames have landed ran while one was on its way";

    gate->open();
    ASSERT_TRUE(waitUntil([&]() { return ran.size() == 2u; })) << "the work never ran once the frame landed";
    EXPECT_EQ(ran, (std::vector<int>{1, 2}));

    // No frame starts while the previews are paused, so the work asked for now finds none on its way.
    window.pausePreviews();
    ASSERT_TRUE(waitUntil([&]() { return window.isRunIdle(); }));
    window.whenRunIdle([&ran]() { ran.push_back(3); });
    EXPECT_EQ(ran.size(), 3u) << "work asked for with no frame on its way did not run at once";
    window.resumePreviews();
}

// The frames a native window holds for the application's request in flight are let go when the request ends, however
// it ends - its result written, a cancel, a refusal - and right after the extension's run when the host asks: a cancel
// that reached the host while the request waited for the frame on its way once left the frames paused for good, the
// window's Apply disabled with them.
TEST(HostInFlightExecutionTest, TheFramesHeldForTheRequestAreLetGoWhenItEnds)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-in-flight-frames", nullptr);
    an::ui::extensions::HostInFlightExecution in_flight;
    EXPECT_FALSE(in_flight.active());
    EXPECT_FALSE(window.previewsPaused());

    in_flight.begin("request-1", "execute", "params-1", QCborMap{}, {}, 40);
    EXPECT_TRUE(in_flight.active());
    EXPECT_EQ(in_flight.requestId(), "request-1");
    EXPECT_EQ(in_flight.kind(), "execute");
    EXPECT_EQ(in_flight.dueElapsedMs(), 40);
    in_flight.holdFrames(&window);
    in_flight.holdFrames(&window);
    EXPECT_TRUE(in_flight.framesHeld());
    EXPECT_TRUE(window.previewsPaused());
    // The request is cancelled before its run: its end lets the frames go.
    in_flight.clear();
    EXPECT_FALSE(in_flight.active());
    EXPECT_FALSE(in_flight.framesHeld());
    EXPECT_FALSE(window.previewsPaused()) << "the request ended with the frames still held";
    EXPECT_EQ(in_flight.dueElapsedMs(), -1);

    // The run's end lets the frames go before the request's; the request's end then lets nothing more go.
    in_flight.begin("request-2", "preview", "params-2", QCborMap{}, {}, 0);
    in_flight.holdFrames(&window);
    in_flight.releaseFrames();
    EXPECT_FALSE(window.previewsPaused());
    window.pausePreviews();
    in_flight.clear();
    EXPECT_TRUE(window.previewsPaused()) << "the request's end let go of a hold that was not its own";
    window.resumePreviews();
}

// While the frames are paused for a run the GUI thread makes itself - a parameter action waiting for the frame on its
// way to land - an Apply is refused and its button disabled: a run started then would take the extension's lane ahead
// of that run and leave the GUI thread waiting on a worker.
TEST(ExtensionNativeProcessWindowTest, AnApplyIsRefusedWhileTheFramesArePausedForARunOfTheGuiThreadsOwn)
{
    an::ui::extensions::ExtensionNativeProcessWindow window("Native Window", "session-apply-while-paused", nullptr);
    window.setKeepOpenMode(true);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    int executes = 0;
    window.setExecuteCallback([&executes]() { ++executes; });
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.05},
    }});

    auto source_view = makePreviewView("apply_while_paused_source");
    showGrey(*source_view, 10, 8, 1.0);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));

    auto* apply = window.findChild<QPushButton*>(QStringLiteral("anNativeProcessExecuteButton"));
    ASSERT_NE(apply, nullptr);
    ASSERT_TRUE(apply->isEnabled());

    window.pausePreviews();
    EXPECT_TRUE(window.previewsPaused());
    EXPECT_FALSE(apply->isEnabled()) << "the Apply button stays enabled while the frames are paused";
    apply->click();
    EXPECT_EQ(executes, 0) << "an Apply ran while the frames were paused";

    window.resumePreviews();
    EXPECT_FALSE(window.previewsPaused());
    EXPECT_TRUE(apply->isEnabled());
    apply->click();
    EXPECT_EQ(executes, 1);
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }));
}
