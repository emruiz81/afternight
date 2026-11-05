// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A preview's two kinds of frame and their pace, against the Controls library: a slider a person holds is shown in
/// interactive frames, which run on the session's preview reservation, and one settled frame follows its release or a
/// hold kept still for the settle delay; a change made with no slider held is a settled frame; a control the window
/// watches, deleted with the window, is no longer watched once the frames' state is gone; and frames start no closer
/// than the period the Processing settings' frame rate states, read when a chain of frames begins.

#include "Test_ImageRTPreviewControl/SliderProcessSupport.h"

#include "controls/settings/SettingsKeys.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QDoubleSpinBox>
#include <QObject>
#include <QSlider>
#include <QSpinBox>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::frames;
using namespace std::chrono_literals;

/// The settle delay a drag held still waits before its settled frame, and some room for the timer's grain.
constexpr std::chrono::milliseconds kPastTheSettleDelay{400};

/// Moves a held slider to a value and turns the event loop until a frame of that value has started.
[[nodiscard]] bool dragTo(SliderProcess& process, int value)
{
    process.slider->setValue(value);
    return pumpUntil([&process, value]() {
        for (const SliderRun& run : process.runs())
        {
            if (run.kind == an::ui::ProcessRunKind::Preview && run.value == value)
            {
                return true;
            }
        }
        return false;
    });
}

/// Turns the event loop for a while, whatever happens.
void pumpFor(std::chrono::milliseconds duration)
{
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(1ms);
    }
}

/// States the previews a second the window's settings hold.
void setFrameRate(ProcessTestContext& context, const QString& rate)
{
    (void)context.upsert(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE), rate);
}

/// A slider control - of whole or of real numbers - that tells how many connections its hold and its going have, a
/// watch's among them.
template <typename ControlT> class CountedControl final : public ControlT
{
public:
    using ControlT::ControlT;

    [[nodiscard]] int watchConnections() const
    {
        return this->receivers(SIGNAL(interaction(bool))) + this->receivers(SIGNAL(destroyed(QObject*)));
    }
};

} // namespace

// A drag: while the slider is held its changes are interactive frames; letting it go asks for one settled frame, the
// last, with the value the drag ended on.
TEST(PreviewFrameKinds, ADragIsShownInInteractiveFramesAndOneSettledFrameFollowsTheRelease)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry before = process->runTelemetry();
    const std::size_t from = process->runs().size();

    process->slider->setSliderDown(true);
    for (const int value : {10, 20, 30, 40})
    {
        ASSERT_TRUE(dragTo(*process, value)) << value;
    }
    // The release asks for the settled frame as it happens, not once the settle delay has run out.
    const std::uint64_t requestedBeforeRelease = process->runTelemetry().previewsRequested;
    process->slider->setSliderDown(false);
    EXPECT_EQ(process->runTelemetry().previewsRequested, requestedBeforeRelease + 1u)
        << "letting the slider go asked for no frame";
    ASSERT_TRUE(settleSlider(*process));

    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_GE(previews.size(), 5u);
    for (std::size_t index = 0u; index + 1u < previews.size(); ++index)
    {
        EXPECT_EQ(previews[index].frameKind, an::ui::PreviewFrameKind::Interactive)
            << "frame " << index << " of the drag, value " << previews[index].value;
    }
    EXPECT_EQ(previews.back().frameKind, an::ui::PreviewFrameKind::Settled) << "the drag ends on a settled frame";
    EXPECT_EQ(previews.back().value, 40);

    const ProcessRunTelemetry after = process->runTelemetry();
    EXPECT_EQ(after.settledPreviewsCommitted - before.settledPreviewsCommitted, 1u);
    EXPECT_EQ(after.interactivePreviewsCommitted - before.interactivePreviewsCommitted, previews.size() - 1u);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.25 * 1.4, 1e-6);
}

// An interactive frame asks admission for the session's preview reservation and borrows nothing past it; the settled
// frame after it may borrow as a preview always could.
TEST(PreviewFrameKinds, AnInteractiveFrameRunsOnThePreviewReservationAndTheSettledOneMayBorrow)
{
    constexpr std::uint32_t kWidth = 8u;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(kWidth);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t from = process->runs().size();

    process->slider->setSliderDown(true);
    ASSERT_TRUE(dragTo(*process, 25));
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));

    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_EQ(previews.size(), 2u);
    const std::uint32_t reservation = an::consumer::previewReservationWidth(kWidth);
    ASSERT_EQ(reservation, 2u);
    EXPECT_EQ(previews[0].frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(previews[0].granted, reservation) << "the interactive frame asked for the reservation, no more";
    EXPECT_EQ(previews[1].frameKind, an::ui::PreviewFrameKind::Settled);
    EXPECT_GT(previews[1].granted, reservation) << "the settled frame borrows the threads nothing else uses";
}

// A drag held still is settled once the settle delay passes with no change, the slider still held; letting it go
// then asks for nothing more.
TEST(PreviewFrameKinds, ADragHeldStillIsSettledAfterTheSettleDelay)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t from = process->runs().size();

    process->slider->setSliderDown(true);
    ASSERT_TRUE(dragTo(*process, 15));
    pumpFor(kPastTheSettleDelay);
    ASSERT_TRUE(settleSlider(*process));
    std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_EQ(previews.size(), 2u);
    EXPECT_EQ(previews[0].frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(previews[1].frameKind, an::ui::PreviewFrameKind::Settled) << "held still, the drag settled";
    EXPECT_EQ(previews[1].value, 15);

    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(runsFrom(process->runs(), from).size(), 2u) << "nothing is owed to a drag already settled";
}

// A change made with no slider held - a value typed, a reset, a box ticked - is a settled frame, and nothing more.
TEST(PreviewFrameKinds, AChangeWithNoSliderHeldIsOneSettledFrame)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry before = process->runTelemetry();
    const std::size_t from = process->runs().size();

    process->spinBox->setValue(60);
    ASSERT_TRUE(settleSlider(*process));
    pumpFor(kPastTheSettleDelay);
    ASSERT_TRUE(settleSlider(*process));

    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_EQ(previews.size(), 1u);
    EXPECT_EQ(previews[0].frameKind, an::ui::PreviewFrameKind::Settled);
    EXPECT_EQ(previews[0].value, 60);
    const ProcessRunTelemetry after = process->runTelemetry();
    EXPECT_EQ(after.interactivePreviewsCommitted, before.interactivePreviewsCommitted);
    EXPECT_EQ(after.settledPreviewsCommitted - before.settledPreviewsCommitted, 1u);
}

// A control the window watches may be one of the window's children - an extension window's sliders are - which the
// window's base deletes once the window's own destructor has let the frames' state go: by then the watch is gone, and
// the control's going reaches nothing of that state.
TEST(PreviewFrameKinds, AWatchedControlDeletedWithItsWindowIsNoLongerWatchedOnceTheFramesStateIsGone)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    int connectionsOnceTheFramesStateWasGone = -1;
    SliderHandle process = openSliderProcess(*target);

    // The probe is deleted before the controls, which come after it among the window's children.
    auto* probe = new QObject(process.get());
    auto* whole = new CountedControl<QSliderSpinBox>(new QSpinBox(process.get()),
                                                     new QSlider(Qt::Horizontal, process.get()), process.get());
    auto* real = new CountedControl<QSliderDoubleSpinBox>(new QDoubleSpinBox(process.get()),
                                                          new QSlider(Qt::Horizontal, process.get()), process.get());
    const auto connections = [whole, real]() { return whole->watchConnections() + real->watchConnections(); };
    const int unwatched = connections();
    process->watchInteraction(*whole);
    process->watchInteraction(*real);
    ASSERT_EQ(connections(), unwatched + 4) << "the window watches each control's hold and its going";
    QObject::connect(probe, &QObject::destroyed, [whole, real, &connectionsOnceTheFramesStateWasGone]() {
        connectionsOnceTheFramesStateWasGone = whole->watchConnections() + real->watchConnections();
    });

    process.reset();
    EXPECT_EQ(connectionsOnceTheFramesStateWasGone, unwatched)
        << "a watch outlived the frames' state, and its control's going would reach it";
}

// Frames start no closer together than the period the frame rate states, however fast the slider moves: at twenty a
// second, fifty milliseconds apart.
TEST(PreviewFramePacing, FramesStartNoCloserThanThePeriodOfTheFrameRate)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    setFrameRate(*context, QStringLiteral("20"));
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t from = process->previewStarts().size();

    // A step every five milliseconds or so, for half a second or more: far faster than twenty frames a second.
    const auto dragStart = std::chrono::steady_clock::now();
    process->slider->setSliderDown(true);
    for (int value = 1; value <= 100; ++value)
    {
        process->slider->setValue(value);
        pumpFor(5ms);
    }
    process->slider->setSliderDown(false);
    const auto dragLength = std::chrono::steady_clock::now() - dragStart;
    ASSERT_TRUE(settleSlider(*process));

    const std::vector<std::chrono::steady_clock::time_point>& starts = process->previewStarts();
    ASSERT_GE(starts.size() - from, 4u);
    for (std::size_t index = from + 1u; index < starts.size(); ++index)
    {
        // A twentieth of a second, less the grain of the clocks that read it.
        EXPECT_GE(starts[index] - starts[index - 1u], 49ms) << "frame " << index - from;
    }
    // At twenty a second, the drag's length allows one frame per period, the first at once, and the settled one after
    // the release.
    const auto allowed = static_cast<std::size_t>(dragLength / 50ms) + 2u;
    EXPECT_LE(starts.size() - from, allowed);
    EXPECT_EQ(runsFrom(process->runs(), 0u).back().value, 100);
}

// The frame rate is read from the settings off the GUI thread - when the preview is turned on, when the window is
// activated and when a chain of frames begins - and what it answers paces the chains that begin after the read lands,
// so a change applies to the next drag: five a second spaces a chain's frames two hundred milliseconds apart, and sixty
// a second the next chain's sixteen or seventeen. The GUI thread never waits for the settings store's thread.
TEST(PreviewFramePacing, TheFrameRateIsReadOffTheGuiThreadAndPacesTheChainsThatBeginAfterTheReadLands)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_TRUE(pumpUntil([&process]() { return process->runTelemetry().frameRatesRead >= 1u; }))
        << "turning the preview on reads the rate";

    // A chain begun now is paced at the last read and reads the rate for the chains after it: a change stored since is
    // in force once that read has landed. A period then passes, so the next chain's first frame starts at once rather
    // than paced behind this chain's.
    const auto readTheRate = [&process]() {
        const std::uint64_t reads = process->runTelemetry().frameRatesRead;
        process->spinBox->setValue(process->spinBox->value() + 1);
        EXPECT_TRUE(settleSlider(*process));
        EXPECT_TRUE(pumpUntil([&process, reads]() { return process->runTelemetry().frameRatesRead > reads; }));
        pumpFor(250ms);
    };
    const auto chainSpacing = [&process](int first, int second) {
        const std::size_t from = process->previewStarts().size();
        process->spinBox->setValue(first);
        process->spinBox->setValue(second);
        EXPECT_TRUE(settleSlider(*process));
        const std::vector<std::chrono::steady_clock::time_point>& starts = process->previewStarts();
        EXPECT_EQ(starts.size() - from, 2u) << "the second change waited out the period";
        return starts.size() - from == 2u ? starts[from + 1u] - starts[from] : std::chrono::steady_clock::duration{};
    };

    setFrameRate(*context, QStringLiteral("5"));
    readTheRate();
    EXPECT_GE(chainSpacing(10, 20), 199ms);

    setFrameRate(*context, QStringLiteral("60"));
    readTheRate();
    const std::chrono::steady_clock::duration fast = chainSpacing(30, 40);
    EXPECT_GE(fast, 16ms);
    EXPECT_LT(fast, 150ms) << "the new rate applied to the chain that began after its read had landed";
}

// A read of the frame rate the settings store refuses by throwing - as the application's store does once it has been
// shut down, with a read still queued behind the frames it paces - ends on its worker and its answer is dropped: the
// process goes on, and so does the preview, at the rate the window had.
TEST(PreviewFramePacing, ARateReadTheStoreRefusesByThrowingEndsOnItsWorkerAndThePreviewGoesOn)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_TRUE(pumpUntil([&process]() { return process->runTelemetry().frameRatesRead >= 1u; }));
    pumpFor(250ms);

    // A chain begun now asks the store again, on a worker, and the store throws.
    context->settingsReadsThrow = true;
    const std::uint64_t reads = process->runTelemetry().frameRatesRead;
    process->spinBox->setValue(process->spinBox->value() + 1);
    ASSERT_TRUE(settleSlider(*process));
    QThreadPool::globalInstance()->waitForDone();
    pumpFor(50ms);
    EXPECT_EQ(process->runTelemetry().frameRatesRead, reads) << "a read that threw was answered";

    const std::size_t from = process->runs().size();
    pumpFor(250ms);
    process->spinBox->setValue(process->spinBox->value() + 1);
    EXPECT_TRUE(settleSlider(*process));
    EXPECT_GT(process->runs().size(), from) << "the window stopped previewing once a read had thrown";
    context->settingsReadsThrow = false;
}
