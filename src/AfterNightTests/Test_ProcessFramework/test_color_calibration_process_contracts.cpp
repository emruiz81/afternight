// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Color Calibration window over a colour image and a reference view beside it: each region it measures is drawn
/// on the reference that region's selector names, a white or background region that does not lie inside its reference
/// is refused by name before anything runs, a manual white balance takes the white reference's controls out of use,
/// and a window closed while its run is on its way goes out of sight and closes only once that run has landed.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "processes/ColorCalibrationProcess.h"
#include "session/ExecutionScope.h"
#include "widgets/ImageViewWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

/// A dark sky whose channels sit at different levels, inside the dialog's default background range.
float sky(std::uint32_t x, std::uint32_t, std::uint32_t channel)
{
    return 0.02f * static_cast<float>(channel + 1u) + 0.0005f * static_cast<float>(x);
}

/// A window whose run waits at a gate once its operation is admitted, until the case opens it.
class GatedCalibration final : public ColorCalibrationProcess
{
public:
    GatedCalibration(ImageViewControl* target, std::shared_ptr<Gate> gate)
        : ColorCalibrationProcess(target), m_gate(std::move(gate))
    {
    }

protected:
    [[nodiscard]] an::ui::processes::AdmissionHook admissionHook() const override
    {
        return [gate = m_gate](an::ui::ExecutionScope& scope) {
            (void)gate->pass([&scope]() { return scope.stopRequested(); });
        };
    }

private:
    std::shared_ptr<Gate> m_gate;
};

/// Presses a window's Apply.
void pressApply(const QWidget& window)
{
    auto* box = widgetNamed<QDialogButtonBox>(window, "buttonBox");
    ASSERT_NE(box, nullptr);
    QPushButton* apply = box->button(QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
}

/// Sets the dialog's manual white balance on, as a person checks it, to a white of 0.9, 1 and 0.8.
void chooseManualWhite(const QWidget& window)
{
    clickGroup(window, "mwbGroupBox", true);
    for (const auto& [name, value] :
         {std::pair{"mwbRedValue", 0.9}, std::pair{"mwbGreenValue", 1.0}, std::pair{"mwbBlueValue", 0.8}})
    {
        if (auto* box = widgetNamed<QDoubleSpinBox>(window, name))
        {
            box->setValue(value);
        }
    }
}

// Each region a person draws is drawn on the reference its own selector names, never on the image the window
// changes: the white region's and then the background region's reference view selects areas and the target is left
// as it was.
TEST(ColorCalibrationProcessContracts, EachRegionIsDrawnOnTheReferenceItsSelectorNames)
{
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    const std::unique_ptr<ContextView> white = floatView("white", 32u, 24u, Channels::Rgb, sky);
    const std::unique_ptr<ContextView> background = floatView("background", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    ASSERT_TRUE(white != nullptr && *white);
    ASSERT_TRUE(background != nullptr && *background);
    const ImageViewSelectionStyle targetStyle = (*target)->getViewWidget()->getImageViewSelectionStyle();
    ASSERT_NE(targetStyle, ImageViewSelectionStyle::AREA_SELECTION_STYLE);

    WindowHandle<ColorCalibrationProcess> window = openOver<ColorCalibrationProcess>(**target);
    chooseText(*window, "whiteReferenceImageSelector", QStringLiteral("white"));
    chooseText(*window, "backgroundReferenceImageSelector", QStringLiteral("background"));

    clickGroup(*window, "whiteRoiGroupBox", true);
    auto* selectWhite = widgetNamed<QPushButton>(*window, "whiteRoiSelectionButton");
    ASSERT_NE(selectWhite, nullptr);
    ASSERT_TRUE(selectWhite->isEnabled());
    selectWhite->click();
    EXPECT_EQ((*white)->getViewWidget()->getImageViewSelectionStyle(), ImageViewSelectionStyle::AREA_SELECTION_STYLE);
    EXPECT_NE((*background)->getViewWidget()->getImageViewSelectionStyle(),
              ImageViewSelectionStyle::AREA_SELECTION_STYLE);
    EXPECT_EQ((*target)->getViewWidget()->getImageViewSelectionStyle(), targetStyle);
    selectWhite->click();

    clickGroup(*window, "backgroundRoiGroupBox", true);
    auto* selectBackground = widgetNamed<QPushButton>(*window, "backgroundRoiSelectionButton");
    ASSERT_NE(selectBackground, nullptr);
    ASSERT_TRUE(selectBackground->isEnabled());
    selectBackground->click();
    EXPECT_EQ((*background)->getViewWidget()->getImageViewSelectionStyle(),
              ImageViewSelectionStyle::AREA_SELECTION_STYLE);
    EXPECT_EQ((*target)->getViewWidget()->getImageViewSelectionStyle(), targetStyle);
    selectBackground->click();
}

// A white region and a background region with no width are each refused by name before anything runs: the window
// says which region must lie inside its reference, starts no run and leaves the image as it was.
TEST(ColorCalibrationProcessContracts, ARegionOutsideItsReferenceIsRefusedByNameBeforeAnythingRuns)
{
    ProcessTestContext& context = suiteContext();
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    const an::ui::DocumentRevision before = (*target)->document().revision();

    struct Region
    {
        const char* group;
        const char* width;
        const char* refusal;
    };
    for (const Region& region :
         {Region{"whiteRoiGroupBox", "whiteRoiWidthValue", "The white reference ROI must be fully inside"},
          Region{"backgroundRoiGroupBox", "backgroundRoiWidthValue",
                 "The background reference ROI must be fully inside"}})
    {
        SCOPED_TRACE(region.group);
        WindowHandle<ColorCalibrationProcess> window = openOver<ColorCalibrationProcess>(**target);
        clickGroup(*window, region.group, true);
        auto* width = widgetNamed<QSpinBox>(*window, region.width);
        ASSERT_NE(width, nullptr);
        EXPECT_EQ(width->value(), 32) << "checking a region starts it as the whole reference";
        width->setValue(0);
        const int messages = static_cast<int>(context.messages.size());
        pressApply(*window);
        ASSERT_TRUE(settle(*window, {target->get()}));

        ASSERT_EQ(static_cast<int>(context.messages.size()), messages + 1);
        EXPECT_EQ(context.messages.back(), QStringLiteral("error:Color Calibration"));
        EXPECT_TRUE(context.errorTexts.back().startsWith(QString::fromLatin1(region.refusal)))
            << context.errorTexts.back().toStdString();
        EXPECT_TRUE(window->isRunIdle());
        EXPECT_EQ((*target)->document().revision(), before);
    }
}

// A manual white balance needs no white reference: checking it takes the white reference's selector, its limits, its
// mask output and its region out of use, and clearing it gives them back, the region's own controls only while the
// region is checked.
TEST(ColorCalibrationProcessContracts, AManualWhiteBalanceTakesTheWhiteReferencesControlsOutOfUse)
{
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    WindowHandle<ColorCalibrationProcess> window = openOver<ColorCalibrationProcess>(**target);
    auto* selector = widgetNamed<QComboBox>(*window, "whiteReferenceImageSelector");
    auto* mask = widgetNamed<QCheckBox>(*window, "outputWhiteReferenceMask");
    auto* region = widgetNamed<QGroupBox>(*window, "whiteRoiGroupBox");
    auto* select = widgetNamed<QPushButton>(*window, "whiteRoiSelectionButton");
    auto* left = widgetNamed<QSpinBox>(*window, "whiteRoiLeftValue");
    ASSERT_TRUE(selector != nullptr && mask != nullptr && region != nullptr && select != nullptr && left != nullptr);

    clickGroup(*window, "whiteRoiGroupBox", true);
    EXPECT_TRUE(selector->isEnabled());
    EXPECT_TRUE(mask->isEnabled());
    EXPECT_TRUE(region->isEnabled());
    EXPECT_TRUE(select->isEnabled());
    EXPECT_TRUE(left->isEnabled());

    clickGroup(*window, "mwbGroupBox", true);
    EXPECT_FALSE(selector->isEnabled());
    EXPECT_FALSE(mask->isEnabled());
    EXPECT_FALSE(region->isEnabled());
    EXPECT_FALSE(select->isEnabled());
    EXPECT_FALSE(left->isEnabled());

    clickGroup(*window, "mwbGroupBox", false);
    EXPECT_TRUE(selector->isEnabled());
    EXPECT_TRUE(region->isEnabled());
    EXPECT_TRUE(select->isEnabled());
    clickGroup(*window, "whiteRoiGroupBox", false);
    EXPECT_FALSE(select->isEnabled()) << "a region not checked offers nothing to draw";
}

// A window closed while its run is on its way goes out of sight at once and stays until the run lands; the run is
// not abandoned, its result is shown, and then the window closes and goes.
TEST(ColorCalibrationProcessContracts, AWindowClosedDuringItsRunClosesOnceTheRunLands)
{
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    const an::ui::DocumentRevision before = (*target)->document().revision();
    auto gate = std::make_shared<Gate>();
    gate->close();

    QPointer<QWidget> window(new GatedCalibration(target->get(), gate));
    // Whatever the case finds, the run is let go and the window does not outlive the case.
    const AtCaseEnd cleanup([&]() {
        gate->open();
        if (!window.isNull())
        {
            (void)pumpUntil([&window]() { return window.isNull() || windowIdle(*window); });
            delete window.data();
        }
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    });
    window->show();
    chooseManualWhite(*window);
    pressApply(*window);
    ASSERT_TRUE(pumpUntil([&gate]() { return gate->arrived() >= 1; })) << "the run never started";

    window->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_FALSE(window.isNull()) << "the window went while its run was on its way";
    EXPECT_FALSE(window->isVisible());
    EXPECT_EQ((*target)->document().revision(), before);

    gate->open();
    EXPECT_TRUE(awaitDeleted(window)) << "the window did not close once its run landed";
    ASSERT_TRUE(pumpUntil([&target]() { return (*target)->isWorkIdle(); }));
    EXPECT_NE((*target)->document().revision(), before) << "the run that was on its way was not shown";
}

} // namespace
