// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The pieces the process windows share, as the processes and Controls libraries define them: a function event runs
/// its function once and says it finished, an empty one runs nothing; a window built by the standard initializer
/// deletes itself when it is closed; a recent process not registered yet keeps its place in the menu; a point or
/// sample selection turned off forgets what it held; the list helpers' move buttons and cell reads; the stacking
/// window's rejection thresholds; the channel composition's Ok, offered once its first component is chosen; and the
/// crop and rotation windows' origins, bounded by the target's extent.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"

#include "app/ProcessControlEvent.h"
#include "app/ProcessMenuState.h"
#include "app/ProcessRegistry.h"
#include "controls/ImagePointSelectionController.h"
#include "controls/ImageSampleSelectionController.h"
#include "processes/ChannelCompositionProcess.h"
#include "processes/CropProcess.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/ImageStackingProcess.h"
#include "processes/ImageStackingProcessUiBuilder.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/RotateProcess.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace
{

using namespace an::tests::processes;
using framework::greySuiteView;
using framework::SuiteView;

/// A point selector that is told nothing it needs to keep.
class IdlePointSelector final : public ImagePointSelectorControl
{
public:
    void onPointSelected(const QPoint&) override {}
};

/// A sample selector of a fixed radius that is told nothing it needs to keep.
class IdleSampleSelector final : public ImageSampleSelectorControl
{
public:
    [[nodiscard]] size_t getSampleRadius() const override
    {
        return 3u;
    }
    void addSampleSelection(const ImageSampleSelection&) override {}
    void removeSampleSelection(const ImageSampleSelection&) override {}
};

/// Turns the event loop, deferred deletions included, until a window is gone or the patience runs out.
[[nodiscard]] bool waitUntilDeleted(const QPointer<QWidget>& window)
{
    for (int turn = 0; turn < 200 && !window.isNull(); ++turn)
    {
        QTest::qWait(10);
    }
    return window.isNull();
}

} // namespace

// A function event runs its function once when it is run, and says it has finished only after the function returned.
TEST(ProcessControlEventTest, AFunctionEventRunsItsFunctionOnceAndSaysItFinished)
{
    std::atomic_bool finished{true};
    int calls = 0;
    bool finishedWhileRunning = true;
    an::ui::FunctionEvent event(
        [&]() {
            ++calls;
            finishedWhileRunning = finished.load();
        },
        &finished);
    EXPECT_EQ(event.type(), AfterNightEvent::getEventType());

    event.run();
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(finishedWhileRunning);
    EXPECT_TRUE(finished.load());
}

// An event whose function is empty runs nothing and still says it has finished; a callable that is not a function is
// run as it is.
TEST(ProcessControlEventTest, AnEmptyFunctionEventRunsNothingAndStillSaysItFinished)
{
    std::atomic_bool finished{false};
    an::ui::FunctionEvent empty(std::function<void()>(), &finished);
    EXPECT_NO_THROW(empty.run());
    EXPECT_TRUE(finished.load());

    int calls = 0;
    const auto body = [&calls]() { ++calls; };
    ProcessControlEvent<decltype(body)> lambdaEvent(body);
    lambdaEvent.run();
    EXPECT_EQ(calls, 1);
}

// A process window built by the framework's standard initializer is deleted when it is closed: nothing the window
// opened outlives it.
TEST(ProcessWindowLifetimeTest, AStandardProcessWindowIsDeletedWhenItIsClosed)
{
    SuiteView target = greySuiteView("closing target", 8u, 6u);
    ASSERT_NE(target, nullptr);
    auto* process = new TestProcess(target.get(), TestProcessOptions{});
    EXPECT_TRUE(process->testAttribute(Qt::WA_DeleteOnClose));
    const QPointer<QWidget> window(process);
    process->show();
    ASSERT_TRUE(pumpUntil([process]() { return process->isProcessIdle(); }));

    process->close();
    EXPECT_TRUE(waitUntilDeleted(window));
}

// The recent processes menu keeps a process whose descriptor is not registered yet - an extension still loading -
// under its id, and names a registered one by its descriptor; an empty id is left out.
TEST(RecentProcessMenuTest, ARecentProcessNotRegisteredYetIsKeptUnderItsId)
{
    an::ui::process_registry::ProcessDescriptor curves;
    curves.id = "builtin.curves";
    curves.display_name = "Curves Transformation";
    const an::ui::ProcessDescriptorLookup lookup =
        [&curves](const std::string& id) -> const an::ui::process_registry::ProcessDescriptor* {
        return id == curves.id ? &curves : nullptr;
    };

    const std::deque<an::ui::RecentProcessEntryData> entries =
        an::ui::buildRecentProcessEntries({"builtin.curves", "", "extension.sample.denoise"}, lookup);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].id, "builtin.curves");
    EXPECT_EQ(entries[0].displayName, "Curves Transformation");
    EXPECT_EQ(entries[1].id, "extension.sample.denoise");
    EXPECT_EQ(entries[1].displayName, "extension.sample.denoise");
}

// Turning a point selection off forgets the point, whether the selection itself is turned off or the view's listener
// that carries it.
TEST(SelectionControllerTest, TurningAPointSelectionOffForgetsThePoint)
{
    SuiteView view = greySuiteView("points", 16u, 12u);
    ASSERT_NE(view, nullptr);
    IdlePointSelector selector;
    ImagePointSelectionController controller(view->getScrollArea(), &selector);

    controller.enablePointSelection(true);
    controller.updatePointSelection(QPoint(5, 7));
    EXPECT_TRUE(controller.isPointSelectionEnabled());
    EXPECT_EQ(controller.getPointSelection(), QPoint(5, 7));
    controller.enablePointSelection(false);
    EXPECT_FALSE(controller.isPointSelectionEnabled());
    EXPECT_EQ(controller.getPointSelection(), QPoint(0, 0));

    controller.enablePointSelection(true);
    controller.updatePointSelection(QPoint(3, 4));
    controller.setListenerEnabled(false);
    EXPECT_FALSE(controller.isPointSelectionEnabled());
    EXPECT_EQ(controller.getPointSelection(), QPoint(0, 0));
}

// Turning a sample selection off empties the samples the window shares with it, whether the selection itself is turned
// off or the view's listener that carries it.
TEST(SelectionControllerTest, TurningASampleSelectionOffEmptiesTheSamplesTheWindowShares)
{
    SuiteView view = greySuiteView("samples", 16u, 12u);
    ASSERT_NE(view, nullptr);
    IdleSampleSelector selector;
    ImageSampleSelectionController controller(view->getScrollArea(), &selector);
    const auto shared = std::make_shared<ImageSampleSelectionList>();
    shared->push_back(ImageSampleSelection{.pos = QPoint(4, 4), .size = 3u, .scaledArea = QRect()});
    controller.updateSampleSelection(shared);

    controller.enableSampleSelection(true);
    ASSERT_EQ(controller.getSampleSelection(), shared);
    EXPECT_EQ(shared->size(), 1u);
    controller.enableSampleSelection(false);
    EXPECT_FALSE(controller.isSampleSelectionEnabled());
    EXPECT_TRUE(shared->empty());

    shared->push_back(ImageSampleSelection{.pos = QPoint(2, 2), .size = 3u, .scaledArea = QRect()});
    controller.enableSampleSelection(true);
    controller.setListenerEnabled(false);
    EXPECT_FALSE(controller.isSampleSelectionEnabled());
    EXPECT_TRUE(shared->empty());
}

// A list's move buttons follow the row chosen: neither with no row or no list, only down at the first row, only up at
// the last, and both between.
TEST(ProcessListHelpersTest, TheMoveButtonsFollowTheRowAndTheEndsOfTheList)
{
    QPushButton moveUp;
    QPushButton moveDown;

    ImageListProcessHelpers::updateMoveButtonStates(&moveUp, &moveDown, -1, 3u);
    EXPECT_FALSE(moveUp.isEnabled());
    EXPECT_FALSE(moveDown.isEnabled());

    ImageListProcessHelpers::updateMoveButtonStates(&moveUp, &moveDown, 0, 0u);
    EXPECT_FALSE(moveUp.isEnabled());
    EXPECT_FALSE(moveDown.isEnabled());

    ImageListProcessHelpers::updateMoveButtonStates(&moveUp, &moveDown, 0, 3u);
    EXPECT_FALSE(moveUp.isEnabled());
    EXPECT_TRUE(moveDown.isEnabled());

    ImageListProcessHelpers::updateMoveButtonStates(&moveUp, &moveDown, 1, 3u);
    EXPECT_TRUE(moveUp.isEnabled());
    EXPECT_TRUE(moveDown.isEnabled());

    ImageListProcessHelpers::updateMoveButtonStates(&moveUp, &moveDown, 2, 3u);
    EXPECT_TRUE(moveUp.isEnabled());
    EXPECT_FALSE(moveDown.isEnabled());
}

// A table cell is read as its text, and a missing cell, an empty one and a missing table as the fallback.
TEST(ProcessListHelpersTest, AMissingOrEmptyCellReadsAsTheFallback)
{
    QTableWidget table(2, 2);
    table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("value")));
    table.setItem(0, 1, new QTableWidgetItem(QString()));

    EXPECT_EQ(process_utils::tableItemTextOrDefault(&table, 0, 0, QStringLiteral("fallback")), QStringLiteral("value"));
    EXPECT_EQ(process_utils::tableItemTextOrDefault(&table, 0, 1, QStringLiteral("fallback")),
              QStringLiteral("fallback"));
    EXPECT_EQ(process_utils::tableItemTextOrDefault(&table, 1, 1, QStringLiteral("fallback")),
              QStringLiteral("fallback"));
    EXPECT_EQ(process_utils::tableItemTextOrDefault(nullptr, 0, 0, QStringLiteral("fallback")),
              QStringLiteral("fallback"));
}

// Choosing another rejection puts its own default thresholds in the two boxes; anything else that asks the rejection
// to be shown again - the stacking method changed and changed back - keeps the thresholds the person typed.
TEST(StackingWindowRejectionTest, OnlyAChangeOfRejectionRestoresItsDefaultThresholds)
{
    auto* window = new ImageStackingProcess();
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    const std::unique_ptr<ImageStackingProcess> owned(window);
    an::ui::processes::ImageStackingProcessUiElements& ui = window->ui();
    ui.stackMethodSelector->setCurrentText(QStringLiteral("Average"));
    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("None"));
    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("Linear Fit"));
    EXPECT_DOUBLE_EQ(ui.sigmaLowValue->value(), 5.0);
    EXPECT_DOUBLE_EQ(ui.sigmaHighValue->value(), 4.0);

    ui.sigmaLowValue->setValue(6.5);
    ui.sigmaHighValue->setValue(2.5);
    ui.stackMethodSelector->setCurrentText(QStringLiteral("Median"));
    ui.stackMethodSelector->setCurrentText(QStringLiteral("Average"));
    EXPECT_DOUBLE_EQ(ui.sigmaLowValue->value(), 6.5);
    EXPECT_DOUBLE_EQ(ui.sigmaHighValue->value(), 2.5);

    ui.rejectionMethodSelector->setCurrentText(QStringLiteral("GESDT"));
    EXPECT_DOUBLE_EQ(ui.sigmaLowValue->value(), 0.30);
    EXPECT_DOUBLE_EQ(ui.sigmaHighValue->value(), 0.05);
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
}

// The channel composition offers Ok only once its first component names an image: a second or third component alone is
// not enough, and taking the first back withdraws Ok.
TEST(ChannelCompositionWindowTest, OkIsOfferedOnlyOnceTheFirstComponentIsChosen)
{
    SuiteView red = greySuiteView("component-red", 8u, 6u, 0.2f);
    SuiteView green = greySuiteView("component-green", 8u, 6u, 0.4f);
    ASSERT_TRUE(red != nullptr && green != nullptr);
    auto* window = new ChannelCompositionProcess();
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    const std::unique_ptr<ChannelCompositionProcess> owned(window);
    auto* redSelector = window->findChild<QComboBox*>(QStringLiteral("redChannel"));
    auto* greenSelector = window->findChild<QComboBox*>(QStringLiteral("greenChannel"));
    auto* buttons = window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_TRUE(redSelector != nullptr && greenSelector != nullptr && buttons != nullptr);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    ASSERT_NE(redSelector->findText(QStringLiteral("component-red")), -1);
    EXPECT_EQ(redSelector->currentText(), NO_IMAGE_STR);
    EXPECT_FALSE(ok->isEnabled());

    greenSelector->setCurrentText(QStringLiteral("component-green"));
    EXPECT_FALSE(ok->isEnabled());

    redSelector->setCurrentText(QStringLiteral("component-red"));
    EXPECT_TRUE(ok->isEnabled());

    redSelector->setCurrentText(NO_IMAGE_STR);
    EXPECT_FALSE(ok->isEnabled());
}

// The crop's and the rotation's origin boxes stop at the target's width and height, and the size boxes at what is left
// of the image past the origin.
TEST(GeometryWindowOriginTest, TheCropAndRotationOriginsStopAtTheTargetsExtent)
{
    SuiteView target = greySuiteView("origin target", 40u, 30u);
    ASSERT_NE(target, nullptr);
    {
        const std::unique_ptr<CropProcess> crop(new CropProcess(target.get()));
        crop->setAttribute(Qt::WA_DeleteOnClose, false);
        auto* x = crop->findChild<QSpinBox*>(QStringLiteral("targetPosX"));
        auto* y = crop->findChild<QSpinBox*>(QStringLiteral("targetPosY"));
        auto* width = crop->findChild<QSpinBox*>(QStringLiteral("targetWidth"));
        ASSERT_TRUE(x != nullptr && y != nullptr && width != nullptr);
        EXPECT_EQ(x->maximum(), 40);
        EXPECT_EQ(y->maximum(), 30);
        EXPECT_EQ(width->maximum(), 40);
        pumpQuietly();
    }
    {
        const std::unique_ptr<RotateProcess> rotate(new RotateProcess(target.get()));
        rotate->setAttribute(Qt::WA_DeleteOnClose, false);
        auto* x = rotate->findChild<QSpinBox*>(QStringLiteral("targetPosX"));
        auto* y = rotate->findChild<QSpinBox*>(QStringLiteral("targetPosY"));
        ASSERT_TRUE(x != nullptr && y != nullptr);
        EXPECT_EQ(x->maximum(), 40);
        EXPECT_EQ(y->maximum(), 30);
        pumpQuietly();
    }
}
