// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Background Neutralization window over a colour image and a reference view beside it: the region it measures is
/// drawn on the reference it names, a region that does not lie inside the reference is refused by name before anything
/// runs, and a window closed while its run is on its way goes out of sight and closes only once that run has landed.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "processes/BackgroundNeutralizationProcess.h"
#include "session/ExecutionScope.h"
#include "widgets/ImageViewWidget.h"

#include <QDialogButtonBox>
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
class GatedNeutralization final : public BackgroundNeutralizationProcess
{
public:
    GatedNeutralization(ImageViewControl* target, std::shared_ptr<Gate> gate)
        : BackgroundNeutralizationProcess(target), m_gate(std::move(gate))
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

// The region a person draws is drawn on the reference the dialog names, never on the image the window changes: the
// reference view selects areas and the target is left as it was.
TEST(BackgroundNeutralizationProcessContracts, TheRegionIsDrawnOnTheReferenceTheDialogNames)
{
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    const std::unique_ptr<ContextView> reference = floatView("reference", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    ASSERT_TRUE(reference != nullptr && *reference);
    const ImageViewSelectionStyle targetStyle = (*target)->getViewWidget()->getImageViewSelectionStyle();
    ASSERT_NE(targetStyle, ImageViewSelectionStyle::AREA_SELECTION_STYLE);
    ASSERT_NE((*reference)->getViewWidget()->getImageViewSelectionStyle(),
              ImageViewSelectionStyle::AREA_SELECTION_STYLE);

    WindowHandle<BackgroundNeutralizationProcess> window = openOver<BackgroundNeutralizationProcess>(**target);
    chooseText(*window, "backgroundReferenceImageSelector", QStringLiteral("reference"));
    clickGroup(*window, "backgroundRoiGroupBox", true);
    auto* select = widgetNamed<QPushButton>(*window, "backgroundRoiSelectionButton");
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->isEnabled());
    select->click();
    EXPECT_TRUE(select->isChecked());
    EXPECT_EQ((*reference)->getViewWidget()->getImageViewSelectionStyle(),
              ImageViewSelectionStyle::AREA_SELECTION_STYLE);
    EXPECT_EQ((*target)->getViewWidget()->getImageViewSelectionStyle(), targetStyle);
    select->click();
    EXPECT_FALSE(select->isChecked());
}

// A region with no width is refused by name before anything runs: the window says the region must lie inside the
// reference, starts no run and leaves the image as it was.
TEST(BackgroundNeutralizationProcessContracts, ARegionOutsideTheReferenceIsRefusedBeforeAnythingRuns)
{
    ProcessTestContext& context = suiteContext();
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    const an::ui::DocumentRevision before = (*target)->document().revision();

    WindowHandle<BackgroundNeutralizationProcess> window = openOver<BackgroundNeutralizationProcess>(**target);
    clickGroup(*window, "backgroundRoiGroupBox", true);
    auto* width = widgetNamed<QSpinBox>(*window, "backgroundRoiWidthValue");
    ASSERT_NE(width, nullptr);
    EXPECT_EQ(width->value(), 32) << "checking the region starts it as the whole reference";
    width->setValue(0);
    const int messages = static_cast<int>(context.messages.size());
    auto* box = widgetNamed<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(box, nullptr);
    box->button(QDialogButtonBox::Ok)->click();
    ASSERT_TRUE(settle(*window, {target->get()}));

    ASSERT_EQ(static_cast<int>(context.messages.size()), messages + 1);
    EXPECT_EQ(context.messages.back(), QStringLiteral("error:Background Neutralization"));
    EXPECT_TRUE(context.errorTexts.back().contains(QStringLiteral("must be fully inside the reference image")))
        << context.errorTexts.back().toStdString();
    EXPECT_TRUE(window->isRunIdle());
    EXPECT_EQ((*target)->document().revision(), before);
    EXPECT_TRUE(box->button(QDialogButtonBox::Ok)->isEnabled()) << "the window can run again";
}

// A window closed while its run is on its way goes out of sight at once and stays until the run lands; the run is
// not abandoned, its result is shown, and then the window closes and goes.
TEST(BackgroundNeutralizationProcessContracts, AWindowClosedDuringItsRunClosesOnceTheRunLands)
{
    const std::unique_ptr<ContextView> target = floatView("target", 32u, 24u, Channels::Rgb, sky);
    ASSERT_TRUE(target != nullptr && *target);
    const an::ui::DocumentRevision before = (*target)->document().revision();
    auto gate = std::make_shared<Gate>();
    gate->close();

    QPointer<QWidget> window(new GatedNeutralization(target->get(), gate));
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
    auto* box = widgetNamed<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(box, nullptr);
    box->button(QDialogButtonBox::Ok)->click();
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
