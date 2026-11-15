// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The pointwise and tone processes as pages: each in its column at 600 px of height, in both densities, with every
/// control reached by Tab and nothing clipped sideways where text is measured; and each giving through its page the
/// result its window gives for the same parameters.

#include "Test_ProcessesPointwise/PointwiseTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{

using namespace an::tests::processes;

/// A colour Float32 image of a gradient in every channel, as a view the context lists and makes active.
[[nodiscard]] ViewHandle gradientTarget(ProcessTestContext& context, const std::string& name)
{
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        context.engineSession(), 64u, 48u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<float>(0.1 + 0.6 * (x + 2u * y + 5u * channel) / (64.0 + 96.0 + 10.0));
        });
    EXPECT_TRUE(document.has_value());
    return document.has_value() ? makeTargetView(name, std::move(*document)) : nullptr;
}

/// The page of a process held to its column; reported skipped where text is not measured as the interface measures it.
void fitsItsColumn(const std::string& descriptorId)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = gradientTarget(*context, "page target");
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());
    const bool measured = shared::expectPageFitsItsColumn(descriptorId, *target, *context);
    context->removeViewControl(target.get(), false);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// The page of a process gives the window's result.
void appliesAsInAWindow(const std::string& descriptorId)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    shared::expectPageAppliesAsTheWindow(descriptorId, *context,
                                         [&context]() { return gradientTarget(*context, "applied target"); });
}

} // namespace

#define AN_POINTWISE_PAGE_CASES(Name, Id)      \
    TEST(PageLayout, Name##FitsItsColumn)      \
    {                                          \
        fitsItsColumn(Id);                     \
    }                                          \
    TEST(PageLayout, Name##AppliesAsInAWindow) \
    {                                          \
        appliesAsInAWindow(Id);                \
    }

AN_POINTWISE_PAGE_CASES(BrightnessContrast, "tone_brightness.actionAdjustBrightness_Contrast")
AN_POINTWISE_PAGE_CASES(GammaCorrection, "tone_brightness.actionGamma_Correction")
AN_POINTWISE_PAGE_CASES(Exposure, "tone_brightness.actionExposure")
AN_POINTWISE_PAGE_CASES(AdjustHsl, "tone_brightness.actionAdjustHSL")
AN_POINTWISE_PAGE_CASES(AdjustLch, "tone_brightness.actionAdjustLightness_Chroma_Hue")
AN_POINTWISE_PAGE_CASES(ArcSinhStretch, "tone_brightness.actionArcSinh_Stretch")
AN_POINTWISE_PAGE_CASES(ColorSaturation, "color.actionColor_Saturation")
AN_POINTWISE_PAGE_CASES(Colorize, "color.actionColorize")
