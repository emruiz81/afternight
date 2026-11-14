// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The NB Color Mapper as a window: the values a run starts from, an edit of the selected layer reaching the layers
/// and the next run at once, and the Color Spaces menu entry that launches it through its registry descriptor.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "controls/TypedProcessControl.h"
#include "processes/NBColorMapperProcess.h"

#include <QDoubleSpinBox>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

// A run starts with the preview stretched to a quarter's background, no background measured or subtracted - its sky
// read between -2.8 and 2 deviations once it is - the result not stretched and no colourized layer opened; the window
// is a process of the typed framework, whose runs read these values as one snapshot.
TEST(NBColorMapperProcessContracts, ARunStartsWithAStretchedPreviewAndNoBackgroundCorrection)
{
    static_assert(std::is_base_of_v<an::ui::TypedProcessControl<NBColorMapperProcessParams>, NBColorMapperProcess>);
    const NBColorMapperProcessParams params;
    EXPECT_TRUE(params.applyPreviewStretch);
    EXPECT_DOUBLE_EQ(params.request.targetBackground, 0.25);
    EXPECT_FALSE(params.request.background.enabled);
    EXPECT_FALSE(params.request.background.rescaleHighlights);
    EXPECT_DOUBLE_EQ(params.request.background.lowerSigmas, -2.8);
    EXPECT_DOUBLE_EQ(params.request.background.upperSigmas, 2.0);
    EXPECT_FALSE(params.request.background.region.has_value());
    EXPECT_FALSE(params.applyStretchToOutput);
    EXPECT_FALSE(params.returnColorizedLayers);
    EXPECT_TRUE(params.layers.empty());
}

// An edit of the selected layer's colour in the editor is the layer's at once - its row and the next run read it -
// with no button to press first: the editor has none.
TEST(NBColorMapperProcessContracts, AnEditOfTheSelectedLayerReachesTheLayerAndTheNextRunAtOnce)
{
    const std::unique_ptr<ContextView> target =
        floatView("target", 24u, 16u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return 0.1f + 0.01f * static_cast<float>(x + y + channel);
        });
    ASSERT_TRUE(target != nullptr && *target);
    WindowHandle<NBColorMapperProcess> window = openOver<NBColorMapperProcess>(**target);
    ASSERT_EQ(window->layers().size(), 1u) << "the target is the first layer";
    EXPECT_EQ(window->findChild<QWidget*>(QStringLiteral("updateLayerButton")), nullptr);

    auto* table = widgetNamed<QTableWidget>(*window, "layersTable");
    auto* hue = widgetNamed<QDoubleSpinBox>(*window, "layerHue");
    ASSERT_TRUE(table != nullptr && hue != nullptr);
    table->setCurrentCell(0, 0);
    const double edited = hue->value() == 90.0 ? 120.0 : 90.0;
    hue->setValue(edited);

    EXPECT_DOUBLE_EQ(window->layers().front().layer.hueDegrees, edited);
    const auto& typed = static_cast<const an::ui::TypedProcessControl<NBColorMapperProcessParams>&>(*window);
    const NBColorMapperProcessParams params = typed.getParams();
    ASSERT_EQ(params.layers.size(), 1u);
    EXPECT_DOUBLE_EQ(params.layers.front().layer.hueDegrees, edited);
    EXPECT_EQ(params.layers.front().imageName, "target");
    EXPECT_NE(params.layers.front().image, nullptr) << "a run reads the layer's image as its view holds it";
}

// The Process menu's Color Spaces menu offers the mapper, and its entry asks the launcher for the mapper's own
// registry descriptor.
TEST(NBColorMapperProcessContracts, TheColorSpacesMenuLaunchesTheMapperThroughItsDescriptor)
{
    ProcessMenus menus;
    QAction* entry = menus.entry(QStringLiteral("Color Spaces"), QStringLiteral("NB Color Mapper"));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry, menus.action("actionNBColorMapper"));
    ASSERT_TRUE(entry->isEnabled());
    entry->trigger();
    EXPECT_EQ(menus.launched, std::vector<std::string>{"color.actionNBColorMapper"});
}

} // namespace
