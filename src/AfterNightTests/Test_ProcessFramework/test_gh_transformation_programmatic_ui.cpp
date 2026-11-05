// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Generalized Hyperbolic Transformation dialog: the controls its builder makes, the controls the window shows for
/// the transformation and the channel chosen, and the channel choices it offers for a grey and a colour target, each
/// carrying the transformation and the channel a run reads.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "controls/TypedProcessControl.h"
#include "processes/GHTransformationProcess.h"
#include "processes/GHTransformationProcessUiBuilder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

bool comboContains(QComboBox* combo, const QString& text)
{
    return combo && combo->findText(text) >= 0;
}

/// Whether a widget of the window is shown in it: not hidden by the window, whether or not the window is on screen.
[[nodiscard]] bool shownIn(const QWidget& window, const char* name)
{
    const auto* widget = widgetNamed<QWidget>(window, name);
    return widget != nullptr && !widget->isHidden();
}

/// The parameters a run of the window would read now.
[[nodiscard]] GHTransformationParams paramsOf(const GHTransformationProcess& window)
{
    return static_cast<const an::ui::TypedProcessControl<GHTransformationParams>&>(window).getParams();
}

/// A view of a grey or colour Float32 image of soft values.
[[nodiscard]] std::unique_ptr<ContextView> targetOf(Channels channels)
{
    return floatView("target", 32u, 24u, channels, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return 0.05f + 0.01f * static_cast<float>((x + y + 3u * channel) % 32u);
    });
}

// The dialog offers the transformations, the colour clipping and the luminance sources of the process it follows,
// opens on a linear range of zero to one, and lays its channel row out with the stretch at its end.
TEST(GHTransformationProgrammaticUiTest, ConstructorDefaultsExposePixInsightParityControls)
{
    QWidget host;
    auto ui = an::ui::processes::buildGHTransformationProcessUi(&host);

    ASSERT_NE(ui.transformationTypeSelector, nullptr);
    ASSERT_NE(ui.channelSelector2, nullptr);
    ASSERT_NE(ui.stretchFactorValue, nullptr);
    ASSERT_NE(ui.localIntensityValue, nullptr);
    ASSERT_NE(ui.symmetryPointValue, nullptr);
    ASSERT_NE(ui.protectShadowsValue, nullptr);
    ASSERT_NE(ui.protectHighlightsValue, nullptr);
    ASSERT_NE(ui.blackPointValue, nullptr);
    ASSERT_NE(ui.whitePointValue, nullptr);
    ASSERT_NE(ui.linkedStf, nullptr);
    ASSERT_NE(ui.colourClipSelector, nullptr);
    ASSERT_NE(ui.lumCoeffSourceSelector, nullptr);
    ASSERT_NE(ui.lumCoeffRValue, nullptr);
    ASSERT_NE(ui.lumCoeffGValue, nullptr);
    ASSERT_NE(ui.lumCoeffBValue, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Generalized Hyperbolic Transformation"));
    EXPECT_TRUE(comboContains(ui.transformationTypeSelector, QStringLiteral("Generalized Hyperbolic")));
    EXPECT_TRUE(comboContains(ui.transformationTypeSelector, QStringLiteral("Histogram Transformation")));
    EXPECT_TRUE(comboContains(ui.transformationTypeSelector, QStringLiteral("Arcsinh")));
    EXPECT_TRUE(comboContains(ui.transformationTypeSelector, QStringLiteral("Linear Stretch")));
    EXPECT_TRUE(comboContains(ui.transformationTypeSelector, QStringLiteral("Image Inversion")));
    EXPECT_TRUE(comboContains(ui.transformationTypeSelector, QStringLiteral("STF Transformation")));

    EXPECT_EQ(ui.colourClipSelector->currentText(), QStringLiteral("Clip"));
    EXPECT_TRUE(comboContains(ui.colourClipSelector, QStringLiteral("Rescale")));
    EXPECT_EQ(ui.lumCoeffSourceSelector->currentText(), QStringLiteral("Default"));
    EXPECT_TRUE(comboContains(ui.lumCoeffSourceSelector, QStringLiteral("Image")));
    EXPECT_TRUE(comboContains(ui.lumCoeffSourceSelector, QStringLiteral("Manual")));
    EXPECT_DOUBLE_EQ(ui.blackPointValue->value(), 0.0);
    EXPECT_DOUBLE_EQ(ui.whitePointValue->value(), 1.0);
    EXPECT_EQ(ui.channelSelectorLayout_2->count(), 12);
    EXPECT_EQ(ui.channelSelectorLayout_2->stretch(11), 8);
    EXPECT_FALSE(ui.linkedStf->isChecked());
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// The controls follow the transformation chosen: a linear stretch shows its black and white points and hides the
// symmetry point's range, a generalized hyperbolic stretch the other way round; an inversion, a screen transfer and a
// linear stretch cannot be inverted, and only the screen transfer offers its link.
TEST(GHTransformationProgrammaticUiTest, TheControlsFollowTheTransformationChosen)
{
    const std::unique_ptr<ContextView> target = targetOf(Channels::Rgb);
    ASSERT_TRUE(target != nullptr && *target);
    WindowHandle<GHTransformationProcess> window = openOver<GHTransformationProcess>(**target);
    auto* invert = widgetNamed<QCheckBox>(*window, "invert");
    ASSERT_NE(invert, nullptr);

    chooseText(*window, "transformationTypeSelector", QStringLiteral("Linear Stretch"));
    EXPECT_TRUE(shownIn(*window, "blackPointLabel"));
    EXPECT_TRUE(shownIn(*window, "whitePointLabel"));
    EXPECT_FALSE(shownIn(*window, "label_13"));
    EXPECT_FALSE(shownIn(*window, "protectShadowsValue"));
    EXPECT_FALSE(shownIn(*window, "protectHighlightsValue"));
    EXPECT_FALSE(invert->isEnabled());
    EXPECT_FALSE(shownIn(*window, "linkedStf"));

    chooseText(*window, "transformationTypeSelector", QStringLiteral("Generalized Hyperbolic"));
    EXPECT_FALSE(shownIn(*window, "blackPointLabel"));
    EXPECT_FALSE(shownIn(*window, "whitePointLabel"));
    EXPECT_TRUE(shownIn(*window, "label_13"));
    EXPECT_TRUE(shownIn(*window, "protectShadowsValue"));
    EXPECT_TRUE(shownIn(*window, "protectHighlightsValue"));
    EXPECT_TRUE(invert->isEnabled());

    invert->setChecked(true);
    chooseText(*window, "transformationTypeSelector", QStringLiteral("Image Inversion"));
    EXPECT_FALSE(invert->isEnabled());
    EXPECT_FALSE(invert->isChecked()) << "a transformation that cannot be inverted keeps no inversion";

    chooseText(*window, "transformationTypeSelector", QStringLiteral("STF Transformation"));
    EXPECT_FALSE(invert->isEnabled());
    EXPECT_TRUE(shownIn(*window, "linkedStf"));
}

// The colour controls follow the channel chosen: the colour clipping and the luminance source are shown for the
// colour channel only, and the luminance weights only once a person chooses to state them.
TEST(GHTransformationProgrammaticUiTest, TheColourControlsFollowTheChannelChosen)
{
    const std::unique_ptr<ContextView> target = targetOf(Channels::Rgb);
    ASSERT_TRUE(target != nullptr && *target);
    WindowHandle<GHTransformationProcess> window = openOver<GHTransformationProcess>(**target);

    chooseText(*window, "channelSelector2", QStringLiteral("Colour"));
    EXPECT_TRUE(shownIn(*window, "colourClipLabel"));
    EXPECT_TRUE(shownIn(*window, "lumCoeffSourceSelector"));
    EXPECT_FALSE(shownIn(*window, "lumCoeffRValue"));

    chooseText(*window, "lumCoeffSourceSelector", QStringLiteral("Manual"));
    EXPECT_TRUE(shownIn(*window, "lumCoeffRValue"));
    chooseText(*window, "lumCoeffSourceSelector", QStringLiteral("Default"));
    EXPECT_FALSE(shownIn(*window, "lumCoeffRValue"));

    chooseText(*window, "channelSelector2", QStringLiteral("RGB"));
    EXPECT_FALSE(shownIn(*window, "colourClipLabel"));
    EXPECT_FALSE(shownIn(*window, "lumCoeffSourceSelector"));
    EXPECT_FALSE(shownIn(*window, "lumCoeffRValue"));
}

// A grey target offers its one channel, by the framework's grey label, and nothing to choose; a colour target offers
// the stored channels and the colour components, and each choice - the channel and the transformation - is what a run
// reads.
TEST(GHTransformationProgrammaticUiTest, TheChannelChoicesFollowTheTargetAndAreWhatARunReads)
{
    {
        const std::unique_ptr<ContextView> grey = targetOf(Channels::Grey);
        ASSERT_TRUE(grey != nullptr && *grey);
        WindowHandle<GHTransformationProcess> window = openOver<GHTransformationProcess>(**grey);
        auto* channels = widgetNamed<QComboBox>(*window, "channelSelector2");
        ASSERT_NE(channels, nullptr);
        ASSERT_EQ(channels->count(), 1);
        EXPECT_EQ(channels->itemText(0), QStringLiteral("Grayscale"));
        EXPECT_FALSE(channels->isEnabled());
        EXPECT_EQ(paramsOf(*window).channel, GhtChannel::Rgb);
    }

    const std::unique_ptr<ContextView> colour = targetOf(Channels::Rgb);
    ASSERT_TRUE(colour != nullptr && *colour);
    WindowHandle<GHTransformationProcess> window = openOver<GHTransformationProcess>(**colour);
    auto* channels = widgetNamed<QComboBox>(*window, "channelSelector2");
    ASSERT_NE(channels, nullptr);
    EXPECT_TRUE(channels->isEnabled());
    struct Choice
    {
        const char* label;
        GhtChannel channel;
    };
    for (const Choice& choice :
         {Choice{"RGB", GhtChannel::Rgb}, Choice{"Red", GhtChannel::Red}, Choice{"Green", GhtChannel::Green},
          Choice{"Blue", GhtChannel::Blue}, Choice{"Lightness", GhtChannel::Lightness},
          Choice{"Saturation", GhtChannel::Saturation}, Choice{"Colour", GhtChannel::Colour}})
    {
        SCOPED_TRACE(choice.label);
        chooseText(*window, "channelSelector2", QString::fromLatin1(choice.label));
        EXPECT_EQ(paramsOf(*window).channel, choice.channel);
    }
    struct Transformation
    {
        const char* label;
        GhtTransformation transformation;
    };
    for (const Transformation& choice :
         {Transformation{"Generalized Hyperbolic", GhtTransformation::GeneralizedHyperbolic},
          Transformation{"Histogram Transformation", GhtTransformation::Histogram},
          Transformation{"Arcsinh", GhtTransformation::Arcsinh},
          Transformation{"Linear Stretch", GhtTransformation::Linear},
          Transformation{"Image Inversion", GhtTransformation::Inversion},
          Transformation{"STF Transformation", GhtTransformation::ScreenTransfer}})
    {
        SCOPED_TRACE(choice.label);
        chooseText(*window, "transformationTypeSelector", QString::fromLatin1(choice.label));
        EXPECT_EQ(paramsOf(*window).transformation, choice.transformation);
    }
}

} // namespace
