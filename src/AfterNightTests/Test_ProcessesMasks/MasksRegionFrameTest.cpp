// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star reduction's frames over the part of the target the preview view shows. Its body reduces every sample from
/// the target's and the starless image's samples at the same place, so it declares that an interactive frame may run at
/// full scale over the part shown with no reach around it: the starless image is cut to the same part, and over the
/// part the frame is the Apply's answer, sample for sample. The star mask, whose detection measures the whole image's
/// sky and ranks every star in it, runs every frame over the whole image.

#include "Test_ProcessesMasks/MasksTestSupport.h"

#include "controls/ImageViewControl.h"
#include "processes/StarMaskProcess.h"
#include "processes/StarReductionProcess.h"
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
using namespace an::tests::processes::masks;
using an::tests::viewer::Channels;

/// One preview the window's body made, as the framework handed it on.
struct Frame
{
    an::ui::PreviewFrameKind kind = an::ui::PreviewFrameKind::Settled;
    bool partOfTarget = false;
    QRect part;
    std::optional<fixtures::FixtureSamples> samples;
};

/// The star reduction's window, recording the previews its body made and asking for one as a change does.
class RecordingStarReduction final : public StarReductionProcess
{
public:
    using StarReductionProcess::StarReductionProcess;

    [[nodiscard]] std::vector<Frame> frames() const
    {
        const std::lock_guard lock(m_lock);
        return m_frames;
    }

    void askForPreview()
    {
        const std::lock_guard lock(m_lock);
        m_frames.clear();
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

/// A 640 by 480 colour Float32 view of a rippled sky with a grain, and stars on it unless asked for the starless one.
[[nodiscard]] ViewHandle skyView(const an::consumer::EngineSession& session, bool stars, const std::string& name)
{
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        session, 640u, 480u, Channels::Rgb, [stars](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            double value = 0.15 + 0.05 * c +
                           0.06 * std::sin(static_cast<double>(x) / 9.0) * std::cos(static_cast<double>(y) / 7.0);
            const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u) ^ (c * 83492791u);
            value += 0.01 * (static_cast<double>(hash % 1000u) / 1000.0 - 0.5);
            if (stars)
            {
                for (std::uint32_t star = 0u; star < 80u; ++star)
                {
                    const double dx = static_cast<double>(x) - static_cast<double>((star * 89u) % 640u);
                    const double dy = static_cast<double>(y) - static_cast<double>((star * 53u) % 480u);
                    value += 0.6 * std::exp(-(dx * dx + dy * dy) / 5.0);
                }
            }
            return static_cast<float>(std::clamp(value, 0.0, 1.0));
        });
    EXPECT_TRUE(document.has_value());
    return document.has_value() ? makeTargetView(name, std::move(*document)) : ViewHandle();
}

/// An installed application context for one case.
class MasksRegionFrame : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(8u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// The process of the table a case is about.
    [[nodiscard]] static const MasksProcess& named(std::string_view name)
    {
        for (const MasksProcess& candidate : masksProcesses())
        {
            if (std::string_view(candidate.name) == name)
            {
                return candidate;
            }
        }
        ADD_FAILURE() << "no process named " << name;
        return masksProcesses().front();
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

// The star mask's detection measures the whole image's sky and ranks every star in it, so its frames run over the whole
// image; the star reduction reduces every sample from the samples at the same place, so a frame may run over the part
// the view shows.
TEST_F(MasksRegionFrame, TheStarToolsDeclareWhatTheirFramesRunOver)
{
    ViewHandle target = skyView(session(), true, "target");
    ASSERT_NE(target, nullptr);
    WindowHandle mask = named("StarMask").open(*target);
    EXPECT_EQ(mask->previewScaling(), an::ui::PreviewScaling::FullOnly);
    mask.reset();
    WindowHandle reduction = named("StarReduction").open(*target);
    EXPECT_EQ(reduction->previewScaling(), an::ui::PreviewScaling::NativeRegion);
    reduction.reset();
    dropView(context(), target);
}

// With no preview area drawn and the view showing a part of the target at full scale, the frame a change asks for at
// once runs over the part shown, the starless image cut to the same part, and over that part its samples are the
// Apply's.
TEST_F(MasksRegionFrame, AStarReductionFrameOverThePartShownIsTheApplysAnswerThere)
{
    ViewHandle target = skyView(session(), true, "target");
    ViewHandle starless = skyView(session(), false, "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    auto* recording = new RecordingStarReduction(target.get());
    recording->setAttribute(Qt::WA_DeleteOnClose, false);
    recording->resize(900, 600);
    WindowHandle window(recording);
    named("StarReduction").configure(*window, "-");
    setChecked(*window, "preview", true);
    window->show();
    ASSERT_TRUE(settleWindow(*window));
    ImageViewControl* preview = window->previewView();
    ASSERT_NE(preview, nullptr);
    if (QCheckBox* area = preview->getRTPreviewAreaCheckBox(); area != nullptr && area->isChecked())
    {
        area->setChecked(false);
        ASSERT_TRUE(settleWindow(*window));
    }
    ASSERT_TRUE(window->previewArea().isEmpty());
    ImageViewWidget* widget = preview->getViewWidget();
    ASSERT_NE(widget, nullptr);
    widget->scaleImageView(4.0, true, true);
    ASSERT_TRUE(settleWindow(*window));
    const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    const QRect part(static_cast<int>(shown->x), static_cast<int>(shown->y), static_cast<int>(shown->width),
                     static_cast<int>(shown->height));
    ASSERT_NE(part, QRect(0, 0, 640, 480)) << "at four times the view shows a part of the target";

    const ProcessRunTelemetry before = window->runTelemetry();
    recording->askForPreview();
    ASSERT_TRUE(settleWindow(*window));
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
    EXPECT_GT(window->runTelemetry().viewFramesLayered, before.viewFramesLayered) << "the frame was not layered";
    EXPECT_EQ(window->runTelemetry().previewsFailed, before.previewsFailed)
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_TRUE(interactive->samples.has_value());

    ASSERT_TRUE(applyAndSettle(*window, *target));
    const std::optional<fixtures::FixtureSamples> apply = documentSamples(target->document());
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
    dropView(context(), starless);
    dropView(context(), target);
}
