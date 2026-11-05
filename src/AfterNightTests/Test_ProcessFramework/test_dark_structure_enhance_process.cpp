// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Dark Structure Enhance: the dialog its builder makes - the defaults and ranges of the script it follows, the layers
/// the engine's starlet can remove, laid out with the theme's spacing and no style sheet - the mask its preview shows
/// for the channel the selector names, what a history step adds for that channel, and the Details menu entry that
/// launches it through its registry descriptor.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "controls/ImageRTPreviewControl.h"
#include "processes/DarkStructureEnhanceProcess.h"
#include "processes/DarkStructureEnhanceProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QWidget>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

/// The width and height of the image the mask preview runs on: wide enough for the starlet's eight layers.
constexpr std::uint32_t kSide = 256u;

/// A flat grey image whose red channel alone holds a dark vertical band seven pixels wide down its middle.
float redOnlyDarkBand(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    const bool band = channel == 0u && y >= 24u && y + 24u < kSide && x + 3u >= kSide / 2u && x <= kSide / 2u + 3u;
    return band ? 0.18f : 0.58f;
}

/// The mean of a preview's first channel over the band's middle five columns, or NaN when it cannot be read.
double bandMean(const an::ui::ImageDocument& document)
{
    double sum = 0.0;
    std::uint32_t count = 0u;
    for (std::uint32_t y = 24u; y + 24u < kSide; ++y)
    {
        for (std::uint32_t x = kSide / 2u - 2u; x <= kSide / 2u + 2u; ++x)
        {
            sum += documentSample(document, x, y, 0u);
            ++count;
        }
    }
    return sum / static_cast<double>(count);
}

/// Turns a real-time preview's event loop until it has shown more previews than `shown`, and then until it is idle.
[[nodiscard]] bool awaitPreview(const ImageRTPreviewControl& window, std::uint64_t shown)
{
    if (!pumpUntil([&window, shown]() {
            const ProcessRunTelemetry telemetry = window.runTelemetry();
            return telemetry.previewsCommitted > shown || telemetry.previewsFailed > 0u;
        }))
    {
        return false;
    }
    return settle(window) && window.runTelemetry().previewsFailed == 0u;
}

// The dialog opens at the defaults and ranges of the script it follows - eight of the seven to ten layers the engine's
// starlet can remove, the B3 spline, an amount of 0.4 standing for a midtones balance of 0.7, one iteration - with
// the processed image previewed and no mask image asked for.
TEST(DarkStructureEnhanceProgrammaticUiTest, DefaultsMatchPixInsightScriptContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildDarkStructureEnhanceProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.layersValue, nullptr);
    ASSERT_NE(ui.layersSlider, nullptr);
    ASSERT_NE(ui.scalingFunctionComboBox, nullptr);
    ASSERT_NE(ui.amountValue, nullptr);
    ASSERT_NE(ui.amountSlider, nullptr);
    ASSERT_NE(ui.iterationsValue, nullptr);
    ASSERT_NE(ui.iterationsSlider, nullptr);
    ASSERT_NE(ui.previewModeComboBox, nullptr);
    ASSERT_NE(ui.createMaskImage, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.channelSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("DarkStructureEnhance"));

    EXPECT_EQ(ui.layersValue->minimum(), 7);
    EXPECT_EQ(ui.layersValue->maximum(), 10);
    EXPECT_EQ(ui.layersValue->value(), 8);
    EXPECT_EQ(ui.layersSlider->minimum(), 7);
    EXPECT_EQ(ui.layersSlider->maximum(), 10);
    EXPECT_EQ(ui.layersSlider->value(), 8);

    EXPECT_EQ(ui.scalingFunctionComboBox->count(), 2);
    EXPECT_EQ(ui.scalingFunctionComboBox->currentIndex(), 1);
    EXPECT_EQ(ui.scalingFunctionComboBox->currentText(), QStringLiteral("5x5 B3 Spline"));

    EXPECT_DOUBLE_EQ(ui.amountValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.amountValue->maximum(), 0.99);
    EXPECT_DOUBLE_EQ(ui.amountValue->value(), 0.40);
    EXPECT_NEAR(0.5 * ui.amountValue->value() + 0.5, 0.7, 1e-9);

    EXPECT_EQ(ui.iterationsValue->minimum(), 1);
    EXPECT_EQ(ui.iterationsValue->maximum(), 10);
    EXPECT_EQ(ui.iterationsValue->value(), 1);
    EXPECT_EQ(ui.previewModeComboBox->currentIndex(), 0);
    EXPECT_FALSE(ui.createMaskImage->isChecked());
    EXPECT_TRUE(ui.preview->isChecked());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// The mask preview shows the mask the channel the selector names would be enhanced through: over an image whose red
// channel alone holds a dark band, the red channel's mask marks the band and the green channel's does not.
TEST(DarkStructureEnhancePreviewMaskTest, TheMaskPreviewIsTheMaskOfTheSelectedChannel)
{
    const std::unique_ptr<ContextView> target = floatView("target", kSide, kSide, Channels::Rgb, redOnlyDarkBand);
    ASSERT_TRUE(target != nullptr && *target);
    WindowHandle<DarkStructureEnhanceProcess> window = openOver<DarkStructureEnhanceProcess>(**target);
    chooseText(*window, "channelSelector", QStringLiteral("Red"));
    auto* mode = widgetNamed<QComboBox>(*window, "previewModeComboBox");
    auto* preview = widgetNamed<QCheckBox>(*window, "preview");
    ASSERT_TRUE(mode != nullptr && preview != nullptr);
    mode->setCurrentIndex(1);
    const std::uint64_t before = window->runTelemetry().previewsCommitted;
    preview->setChecked(true);
    ASSERT_TRUE(awaitPreview(*window, before));
    ASSERT_NE(window->previewView(), nullptr);
    const double red = bandMean(window->previewView()->document());

    const std::uint64_t shownRed = window->runTelemetry().previewsCommitted;
    chooseText(*window, "channelSelector", QStringLiteral("Green"));
    ASSERT_TRUE(awaitPreview(*window, shownRed));
    const double green = bandMean(window->previewView()->document());

    ASSERT_TRUE(std::isfinite(red) && std::isfinite(green));
    EXPECT_GT(red, green + 0.05) << "the red channel's mask does not mark its band above the green channel's";
}

// A history step names the channel or colour component a run ran on, and says nothing of the whole image or of a grey
// image's one channel.
TEST(DarkStructureEnhanceHistoryTest, AStepNamesTheChannelItRanOnAndNothingForTheWholeImage)
{
    using an::ui::processes::dark_structure_enhance_detail::channelHistorySuffix;
    EXPECT_EQ(channelHistorySuffix("CIE Y (Luminance)"), " in channel: CIE Y (Luminance)");
    EXPECT_EQ(channelHistorySuffix("Red"), " in channel: Red");
    EXPECT_EQ(channelHistorySuffix("RGB"), "");
    EXPECT_EQ(channelHistorySuffix("Grayscale"), "");
    EXPECT_EQ(channelHistorySuffix(""), "");
}

// The dialog is laid out with the theme's spacing and painted by the application's style: its margins and spacing are
// the tokens' and no widget carries a style sheet.
TEST(DarkStructureEnhanceThemeContractTest, TheDialogIsLaidOutWithTheThemesSpacingAndNoStyleSheet)
{
    const auto& t = an::ui::tokens();
    QWidget host;
    auto ui = an::ui::processes::buildDarkStructureEnhanceProcessUi(&host);
    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins(),
              QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    expectNoStyleSheet(host);
}

// The Process menu's Details menu offers the process, and its entry asks the launcher for the process's own registry
// descriptor.
TEST(DarkStructureEnhanceRegistrationTest, TheDetailsMenuLaunchesItThroughItsDescriptor)
{
    ProcessMenus menus;
    QAction* entry = menus.entry(QStringLiteral("Details"), QStringLiteral("Dark Structure Enhance"));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry, menus.ui().actionDark_Structure_Enhance);
    ASSERT_TRUE(entry->isEnabled());
    entry->trigger();
    EXPECT_EQ(menus.launched, std::vector<std::string>{"sharpening_enhancement.actionDark_Structure_Enhance"});
}

} // namespace
