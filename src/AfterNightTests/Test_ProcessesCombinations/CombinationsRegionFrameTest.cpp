// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star mask recombinations' frames over the part of the target the preview view shows. Their bodies put the stars
/// back pixel by pixel, weighed by a mask at the same place - the profiled one painting its mask over the whole image
/// and cutting it with the rest - so they declare that an interactive frame may run at full scale over the part shown:
/// the stars image and the mask are cut to the same part, and over that part the frame is the Apply's answer, sample
/// for sample.

#include "Test_ProcessesCombinations/CombinationsTestSupport.h"

#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControl.h"
#include "processes/ProfiledStarmaskRecombinationProcess.h"
#include "processes/StarmaskRecombinationProcess.h"
#include "widgets/ImageViewWidget.h"

#include <QCheckBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::combinations;
using an::tests::viewer::Channels;

/// One preview the window's body made, as the framework handed it on.
struct Frame
{
    an::ui::PreviewFrameKind kind = an::ui::PreviewFrameKind::Settled;
    bool partOfTarget = false;
    QRect part;
    std::optional<fixtures::FixtureSamples> samples;
};

/// The star mask recombination's window, recording the previews its body made and asking for one as a change does.
class RecordingRecombination final : public StarmaskRecombinationProcess
{
public:
    using StarmaskRecombinationProcess::StarmaskRecombinationProcess;

    [[nodiscard]] std::vector<Frame> frames() const
    {
        const std::lock_guard lock(m_lock);
        return m_frames;
    }

    void askForPreview()
    {
        {
            const std::lock_guard lock(m_lock);
            m_frames.clear();
        }
        schedulePreviewProcessExecution();
    }

protected:
    void imageReadyToRender(const an::ui::ProcessSource& result, const an::ui::ProcessRun& run) override
    {
        if (run.kind != an::ui::ProcessRunKind::Preview)
        {
            return;
        }
        std::string error;
        Frame frame{.kind = run.frameKind,
                    .partOfTarget = run.partOfTarget,
                    .part = run.part,
                    .samples = fixtures::engine::readImageSamples(result.image(), error)};
        const std::lock_guard lock(m_lock);
        m_frames.push_back(std::move(frame));
    }

private:
    mutable std::mutex m_lock;
    std::vector<Frame> m_frames;
};

/// A 640 by 480 colour Float32 view: a rippled sky with a grain, or the stars alone on black.
[[nodiscard]] ViewHandle skyOrStars(const an::consumer::EngineSession& session, bool stars, const std::string& name)
{
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        session, 640u, 480u, Channels::Rgb, [stars](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            double value = 0.0;
            if (stars)
            {
                for (std::uint32_t star = 0u; star < 80u; ++star)
                {
                    const double dx = static_cast<double>(x) - static_cast<double>((star * 89u) % 640u);
                    const double dy = static_cast<double>(y) - static_cast<double>((star * 53u) % 480u);
                    value += 0.6 * std::exp(-(dx * dx + dy * dy) / 5.0);
                }
            }
            else
            {
                const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u) ^ (c * 83492791u);
                value = 0.15 + 0.05 * c +
                        0.06 * std::sin(static_cast<double>(x) / 9.0) * std::cos(static_cast<double>(y) / 7.0) +
                        0.01 * (static_cast<double>(hash % 1000u) / 1000.0 - 0.5);
            }
            return static_cast<float>(std::clamp(value, 0.0, 1.0));
        });
    EXPECT_TRUE(document.has_value());
    return document.has_value() ? makeTargetView(name, std::move(*document)) : ViewHandle();
}

} // namespace

// Both recombinations declare that a frame may run over the part the view shows; the profiled one reads one pixel
// beyond it when it tightens the edges.
TEST(CombinationsRegionFrame, TheRecombinationsDeclareWhatTheirFramesRunOver)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    WindowHandle plain = processNamed("StarmaskRecombination").open(nullptr);
    auto* recombination = dynamic_cast<ImageRTPreviewControl*>(plain.get());
    ASSERT_NE(recombination, nullptr);
    EXPECT_EQ(recombination->previewScaling(), an::ui::PreviewScaling::NativeRegion);
    plain.reset();
    WindowHandle profiled = processNamed("ProfiledStarmaskRecombination").open(nullptr);
    auto* profiledWindow = dynamic_cast<ImageRTPreviewControl*>(profiled.get());
    ASSERT_NE(profiledWindow, nullptr);
    EXPECT_EQ(profiledWindow->previewScaling(), an::ui::PreviewScaling::NativeRegion);
    profiled.reset();
}

// With the view showing a part of the starless image at full scale, the frame a change asks for at once runs over the
// part shown, the stars cut to the same part, and over that part its samples are the Apply's.
TEST(CombinationsRegionFrame, AStarmaskRecombinationFrameOverThePartShownIsTheApplysAnswerThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    Targets targets;
    targets.target = skyOrStars(context->engineSession(), true, "stars");
    ASSERT_NE(targets.target, nullptr);
    context->addViewControl(targets.target.get());
    targets.sides.push_back(
        SideView{.name = "starless", .view = skyOrStars(context->engineSession(), false, "starless")});
    ASSERT_NE(targets.sides.back().view, nullptr);
    context->addViewControl(targets.sides.back().view.get());

    const CombinationsProcess& under = processNamed("StarmaskRecombination");
    auto* recording = new RecordingRecombination(nullptr);
    recording->setAttribute(Qt::WA_DeleteOnClose, false);
    recording->resize(900, 600);
    WindowHandle window(recording);
    under.configure(*window, targets, "-");
    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    window->show();
    ASSERT_TRUE(settleWindow(under, *window));
    ImageViewControl* previewView = recording->previewView();
    ASSERT_NE(previewView, nullptr);
    if (QCheckBox* area = previewView->getRTPreviewAreaCheckBox(); area != nullptr && area->isChecked())
    {
        area->setChecked(false);
        ASSERT_TRUE(settleWindow(under, *window));
    }
    ImageViewWidget* widget = previewView->getViewWidget();
    ASSERT_NE(widget, nullptr);
    widget->scaleImageView(4.0, true, true);
    ASSERT_TRUE(settleWindow(under, *window));
    const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    const QRect part(static_cast<int>(shown->x), static_cast<int>(shown->y), static_cast<int>(shown->width),
                     static_cast<int>(shown->height));
    ASSERT_NE(part, QRect(0, 0, 640, 480)) << "at four times the view shows a part of the image";

    const ProcessRunTelemetry before = recording->runTelemetry();
    recording->askForPreview();
    ASSERT_TRUE(settleWindow(under, *window));
    std::optional<Frame> interactive;
    for (const Frame& frame : recording->frames())
    {
        if (frame.kind == an::ui::PreviewFrameKind::Interactive)
        {
            interactive = frame;
            break;
        }
    }
    ASSERT_TRUE(interactive.has_value()) << "no interactive frame ran";
    EXPECT_TRUE(interactive->partOfTarget);
    ASSERT_EQ(interactive->part, part) << "the frame runs over the part shown, with no reach around it";
    EXPECT_GT(recording->runTelemetry().viewFramesLayered, before.viewFramesLayered) << "the frame was not layered";
    EXPECT_EQ(recording->runTelemetry().previewsFailed, before.previewsFailed)
        << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_TRUE(interactive->samples.has_value());

    const std::optional<fixtures::FixtureSamples> apply = applyAndRead(*context, under, *window, targets);
    ASSERT_TRUE(apply.has_value());
    std::uint64_t differ = 0u;
    std::ostringstream first;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (int y = part.top(); y <= part.bottom(); ++y)
        {
            for (int x = part.left(); x <= part.right(); ++x)
            {
                const double framed = interactive->samples->value(static_cast<std::uint32_t>(x - part.x()),
                                                                  static_cast<std::uint32_t>(y - part.y()), channel);
                const double whole =
                    apply->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), channel);
                if (framed != whole && differ++ == 0u)
                {
                    first << "(" << x << "," << y << "," << channel << ") " << framed << " against " << whole;
                }
            }
        }
    }
    EXPECT_EQ(differ, 0u) << "the part shown differs from the Apply, first at " << first.str();
    window.reset();
    dropTargets(*context, targets);
}
