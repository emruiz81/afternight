// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each external tool's window does, over the stand-in program: the launch through its registry descriptor, a
/// run's outputs beside the target or in its place, the dialog read as it was when OK was pressed, a stopped run, a
/// failed program said under the window's title, every representation an image can hold, a sequence frame by frame,
/// the history, the result's metadata, the program read from the settings, and what only one window does - StarNet's
/// star outputs and its integrated starless generation for the star reduction, the star mask's shaping, DeepSNR's
/// floor. One case per behaviour, run for every process where it applies.

#include "Test_ProcessesExternalTools/ExternalToolsTestSupport.h"

#include "app/ProcessLauncher.h"
#include "app/ProcessRegistry.h"
#include "controls/ImageViewCompanions.h"
#include "components/AnProgressCard.h"
#include "controls/ProgressDialogControl.h"
#include "processes/DeepSNRProcess.h"
#include "processes/StarnetProcess.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::externaltools;

class ExternalToolsWindowTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const ExternalTool& tool() const
    {
        return externalTools()[GetParam()];
    }

    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_standIn = std::make_unique<StandInTools>(*m_context, tool());
    }

    void TearDown() override
    {
        m_standIn.reset();
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

    [[nodiscard]] StandInTools& standIn() const
    {
        return *m_standIn;
    }

    /// A view of the tool's scene, or of a given asset, made active.
    [[nodiscard]] ViewHandle openTarget(std::optional<an::consumer::ImageAsset> asset = std::nullopt)
    {
        if (!asset.has_value())
        {
            asset = sceneAsset(session(), tool().scene);
        }
        if (!asset.has_value())
        {
            return ViewHandle();
        }
        ViewHandle view = assetView(std::move(*asset), "target");
        activate(context(), *view);
        return view;
    }

    /// Opens the window over a target set to a mode.
    [[nodiscard]] WindowHandle openWindow(ImageViewControl* target, std::string_view mode = "-", bool stopping = false)
    {
        WindowHandle window = stopping ? tool().openStopping(target) : tool().open(target);
        tool().configure(*window, mode);
        return window;
    }

    /// The views a run opened beside a target, in order; fails the case when the count is not the tool's.
    [[nodiscard]] std::vector<ImageViewControl*> outputsOf(std::size_t before)
    {
        const std::vector<ImageViewControl*> made(context().madeViews.begin() + static_cast<std::ptrdiff_t>(before),
                                                  context().madeViews.end());
        return made;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    std::unique_ptr<StandInTools> m_standIn;
};

std::string toolName(const ::testing::TestParamInfo<std::size_t>& info)
{
    return externalTools()[info.param].name;
}

[[nodiscard]] bool hasCard(const an::ImageMetadata& metadata, std::string_view keyword)
{
    for (const an::CodecCard& card : metadata.codec().cards())
    {
        if (card.keyword() == keyword)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool lineageNames(const an::ImageMetadata& metadata, std::string_view text)
{
    for (const an::ProvenanceEntry& entry : metadata.provenance().entries())
    {
        if (entry.operation().find(text) != std::string::npos || entry.detail().find(text) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/// The one error shown, or empty.
[[nodiscard]] QString shownError(const ProcessTestContext& context)
{
    return context.errorTexts.isEmpty() ? QString() : context.errorTexts.front();
}

} // namespace

// launch: the registered action opens the process's window over the active view under the process's name, which
// needs no exclusive access, and reads the program from the global settings.
TEST_P(ExternalToolsWindowTest, TheRegisteredActionOpensTheWindow)
{
    const ExternalTool& under = tool();
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(under.descriptorId);
    ASSERT_NE(descriptor, nullptr) << under.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, under.action);
    EXPECT_EQ(descriptor->process_name, under.processName);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(target.get());
    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, under.className);
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    QWidget* window = context().openProcesses.front();
    EXPECT_TRUE(window->isVisible());
    auto* single = dynamic_cast<ImageProcessControl*>(window);
    ASSERT_NE(single, nullptr);
    EXPECT_EQ(single->getProcessName(), under.processName);
    EXPECT_EQ(single->attachmentName(), under.processName);
    EXPECT_FALSE(single->requiresExclusiveAccess());
    QPushButton* ok = okButtonOf(*window);
    ASSERT_NE(ok, nullptr);
    EXPECT_TRUE(ok->isEnabled()) << "the stand-in program is configured";
    window->close();
    ASSERT_TRUE(settleAll(context()));
    dropView(context(), target);
}

// persistence: a window reads the program from the global settings when it opens and after Configure, and offers no
// run without one; it keeps nothing of its dialog between openings.
TEST_P(ExternalToolsWindowTest, TheWindowReadsTheProgramFromTheSettings)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    {
        WindowHandle window = openWindow(target.get());
        setChecked(*window, "preStretchLinearImage", true);
        EXPECT_TRUE(okButtonOf(*window)->isEnabled());
        QLabel* label = widgetOf<QLabel>(*window, "label");
        ASSERT_NE(label, nullptr);
        EXPECT_NE(label->text().indexOf(QStringLiteral("ready")), -1);
    }
    (void)context().upsert(QString::fromLatin1(under.settingsKey), QString());
    {
        WindowHandle window = openWindow(target.get());
        EXPECT_FALSE(okButtonOf(*window)->isEnabled()) << "no program is named";
        QLabel* label = widgetOf<QLabel>(*window, "label");
        ASSERT_NE(label, nullptr);
        EXPECT_NE(label->text().indexOf(QStringLiteral("empty")), -1);
        QCheckBox* preStretch = widgetOf<QCheckBox>(*window, "preStretchLinearImage");
        ASSERT_NE(preStretch, nullptr);
        EXPECT_FALSE(preStretch->isChecked()) << "nothing is kept between openings";
        (void)context().upsert(QString::fromLatin1(under.settingsKey), StandInTools::executablePath());
        QPushButton* configure = window->findChild<QPushButton*>(QStringLiteral("configureStarnet"));
        if (configure == nullptr)
        {
            configure = window->findChild<QPushButton*>(QStringLiteral("configureDeepSNR"));
        }
        ASSERT_NE(configure, nullptr);
        emit configure->released();
        EXPECT_TRUE(okButtonOf(*window)->isEnabled()) << "Configure re-reads the settings";
        EXPECT_EQ(context().messages.count(QStringLiteral("settings:External Tools")), 1);
    }
    dropView(context(), target);
}

// apply: a run over a still image opens the tool's outputs beside the target under the tool's names and histories,
// or puts the answer in the image's place; the stand-in changes nothing, so the starless image and the
// denoised image are the scene within one sixteen-bit code, and the star mask is the tool's own answer.
TEST_P(ExternalToolsWindowTest, ARunOpensTheOutputsOrReplacesTheImage)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
    ASSERT_TRUE(before.has_value());
    const std::size_t viewsBefore = context().madeViews.size();
    WindowHandle window = openWindow(target.get());
    window->show();
    ASSERT_TRUE(runAndSettle(context(), *window, under));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
    ASSERT_EQ(standIn().recordedRuns().size(), 1) << "the program ran once";
    if (under.inPlace)
    {
        EXPECT_EQ(context().madeViews.size(), viewsBefore);
        const std::optional<fixtures::FixtureSamples> after = documentSamples(target->document());
        ASSERT_TRUE(after.has_value());
        EXPECT_LE(largestFractionDifference(*before, *after), 1.0 / 65535.0 + 1.0e-6);
        EXPECT_TRUE(window->isVisible()) << "the window stays open for another run";
    }
    else
    {
        const std::vector<ImageViewControl*> outputs = outputsOf(viewsBefore);
        ASSERT_EQ(outputs.size(), under.outputSuffixes.size());
        for (std::size_t index = 0u; index < outputs.size(); ++index)
        {
            EXPECT_EQ(outputs[index]->getUniqueName(), target->getUniqueName() + under.outputSuffixes[index]);
            EXPECT_EQ(madeNeighbourOf(context(), outputs[index]), target.get());
            EXPECT_NE(madeHistoryOf(context(), outputs[index]).find("StarNet2:"), std::string::npos);
            EXPECT_NE(madeHistoryOf(context(), outputs[index]).find(target->getUniqueName()), std::string::npos);
        }
        const std::optional<fixtures::FixtureSamples> made = documentSamples(outputs.front()->document());
        ASSERT_TRUE(made.has_value());
        if (std::string_view(under.name) == "Starnet")
        {
            EXPECT_LE(largestFractionDifference(*before, *made), 1.0 / 65535.0 + 1.0e-6);
        }
        else
        {
            // The stand-in's mask is the complement of what it was handed.
            double largest = 0.0;
            for (std::uint32_t c = 0u; c < made->channelCount(); ++c)
            {
                for (std::uint32_t y = 0u; y < made->height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < made->width(); ++x)
                    {
                        largest = (std::max)(largest, std::fabs(fractionOf(*made, x, y, c) -
                                                                (1.0 - fractionOf(*before, x, y, c))));
                    }
                }
            }
            EXPECT_LE(largest, 2.0 / 65535.0);
        }
        const std::optional<fixtures::FixtureSamples> unchanged = documentSamples(target->document());
        ASSERT_TRUE(unchanged.has_value());
        EXPECT_EQ(unchanged->digest(), before->digest()) << "the target is not changed";
        EXPECT_TRUE(window->isVisible()) << "the window stays open for another run";
    }
    window.reset();
    dropView(context(), target);
}

// parameter-snapshot: a run reads the dialog as it was when OK was pressed; a change made while the program runs does
// not reach it.
TEST_P(ExternalToolsWindowTest, ARunReadsTheDialogAsItWasWhenOkWasPressed)
{
    const ExternalTool& under = tool();
    standIn().setBehaviour("slow");
    qputenv(kStandInSlowMsVariable, QByteArray("1500"));
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    WindowHandle window = openWindow(target.get());
    setComboData(*window, "stridePreset", QStringLiteral("custom"));
    setInt(*window, "customStrideValue", 64);
    under.run(*window);
    ASSERT_TRUE(pumpUntil([this]() { return !standIn().recordedRuns().isEmpty(); }));
    setInt(*window, "customStrideValue", 128);
    setChecked(*window, "preStretchLinearImage", true);
    ASSERT_TRUE(settleWindow(context(), *window));
    const QStringList runs = standIn().recordedRuns();
    ASSERT_EQ(runs.size(), 1);
    EXPECT_NE(runs.front().indexOf(QStringLiteral("--stride 64")), -1) << runs.front().toStdString();
    EXPECT_EQ(runs.front().indexOf(QStringLiteral("--stride 128")), -1);
    window.reset();
    dropView(context(), target);
}

// cancel: a run stopped before it starts publishes nothing and says nothing, and one stopped through its progress
// window kills the program within the read wait.
TEST_P(ExternalToolsWindowTest, AStoppedRunPublishesNothing)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
    const std::size_t viewsBefore = context().madeViews.size();
    {
        WindowHandle window = openWindow(target.get(), "-", true);
        ASSERT_TRUE(runAndSettle(context(), *window, under));
        EXPECT_TRUE(context().messages.isEmpty()) << context().messages.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(context().madeViews.size(), viewsBefore);
        EXPECT_TRUE(standIn().recordedRuns().isEmpty()) << "the program never ran";
        EXPECT_FALSE(QFileInfo::exists(standIn().keptInputPath())) << "nothing was handed to the program";
        EXPECT_TRUE(okButtonOf(*window)->isEnabled()) << "the window is ready for another run";
    }
    standIn().setBehaviour("slow");
    qputenv(kStandInSlowMsVariable, QByteArray("20000"));
    {
        WindowHandle window = openWindow(target.get());
        under.run(*window);
        ASSERT_TRUE(pumpUntil([this]() { return !standIn().recordedRuns().isEmpty(); }));
        ProgressDialogControl* progress = nullptr;
        ASSERT_TRUE(pumpUntil([&progress]() {
            for (QWidget* top : QApplication::topLevelWidgets())
            {
                if (auto* dialog = dynamic_cast<ProgressDialogControl*>(top); dialog != nullptr && dialog->isVisible())
                {
                    progress = dialog;
                    return true;
                }
            }
            return false;
        })) << "the run shows its progress window";
        // The program's own progress moves the window: the stand-in prints its first quarter at once.
        const QList<an::ui::AnProgressCard*> cards = progress->findChildren<an::ui::AnProgressCard*>();
        ASSERT_FALSE(cards.isEmpty());
        const auto moved = [&cards]() {
            return std::any_of(cards.cbegin(), cards.cend(), [](const an::ui::AnProgressCard* card) {
                return card->progressValue() >= 10 && card->title().startsWith(QStringLiteral("Executing"));
            });
        };
        EXPECT_TRUE(pumpUntil(moved)) << "the program's progress moved the run's window";
        const auto started = std::chrono::steady_clock::now();
        progress->requestCancel();
        ASSERT_TRUE(settleWindow(context(), *window));
        EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8));
        EXPECT_TRUE(context().messages.isEmpty()) << context().messages.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(context().madeViews.size(), viewsBefore);
        if (target->viewHistory() != nullptr)
        {
            EXPECT_FALSE(target->viewHistory()->hasUndo());
        }
    }
    const std::optional<fixtures::FixtureSamples> after = documentSamples(target->document());
    ASSERT_TRUE(before.has_value() && after.has_value());
    EXPECT_EQ(after->digest(), before->digest());
    dropView(context(), target);
}

// error: a program that fails is said under the window's title with what it printed, and nothing is published; a
// program that crashes is said to have crashed.
TEST_P(ExternalToolsWindowTest, AFailedProgramIsSaidUnderTheWindowsTitle)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::size_t viewsBefore = context().madeViews.size();
    standIn().setBehaviour("exit-code");
    {
        WindowHandle window = openWindow(target.get());
        ASSERT_TRUE(runAndSettle(context(), *window, under));
        ASSERT_EQ(context().messages.size(), 1);
        EXPECT_EQ(context().messages.front(), QStringLiteral("error:") + QString::fromLatin1(under.errorTitle));
        EXPECT_TRUE(shownError(context()).startsWith(QString::fromLatin1(under.errorLead) + QStringLiteral(":")))
            << shownError(context()).toStdString();
        EXPECT_NE(shownError(context()).indexOf(QStringLiteral("exited with code 3")), -1)
            << shownError(context()).toStdString();
        EXPECT_NE(shownError(context()).indexOf(QStringLiteral("bad input")), -1);
        EXPECT_EQ(context().madeViews.size(), viewsBefore);
        EXPECT_TRUE(okButtonOf(*window)->isEnabled());
    }
    context().messages.clear();
    context().errorTexts.clear();
    standIn().setBehaviour("crash");
    {
        WindowHandle window = openWindow(target.get());
        ASSERT_TRUE(runAndSettle(context(), *window, under));
        ASSERT_EQ(context().messages.size(), 1);
        EXPECT_NE(shownError(context()).indexOf(QStringLiteral("crashed")), -1) << shownError(context()).toStdString();
    }
    if (target->viewHistory() != nullptr)
    {
        EXPECT_FALSE(target->viewHistory()->hasUndo());
    }
    dropView(context(), target);
}

// representation: every representation an image can hold is handed as sixteen-bit codes and comes back in its own
// representation, the stand-in changing nothing, within one code of its own.
TEST_P(ExternalToolsWindowTest, EveryRepresentationIsHandedAndComesBackInItsOwn)
{
    const ExternalTool& under = tool();
    const std::optional<an::consumer::ImageAsset> scene = sceneAsset(session(), under.scene);
    ASSERT_TRUE(scene.has_value());
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        SCOPED_TRACE(static_cast<int>(type));
        std::optional<an::consumer::ImageAsset> converted = convertedTo(session(), *scene, type);
        ASSERT_TRUE(converted.has_value());
        ViewHandle target = openTarget(std::move(converted));
        ASSERT_NE(target, nullptr);
        const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
        ASSERT_TRUE(before.has_value());
        const std::size_t viewsBefore = context().madeViews.size();
        WindowHandle window = openWindow(target.get());
        ASSERT_TRUE(runAndSettle(context(), *window, under));
        EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
        const ImageViewControl* result = under.inPlace ? target.get() : outputsOf(viewsBefore).front();
        ASSERT_NE(result, nullptr);
        const std::optional<fixtures::FixtureSamples> made = documentSamples(result->document());
        ASSERT_TRUE(made.has_value());
        EXPECT_EQ(made->sampleType(), before->sampleType());
        if (std::string_view(under.name) != "StarnetStarMask")
        {
            const double codes = type == an::SampleType::UInt8 || type == an::SampleType::Int8 ? 255.0 : 65535.0;
            EXPECT_LE(largestFractionDifference(*before, *made), 1.0 / codes + 1.0e-6);
        }
        window.reset();
        dropView(context(), target);
    }
}

// sequence: a run over a sequence hands every frame to the program and publishes a new sequence per output, or the
// view's own sequence in place as one history step; each frame is what the still frame becomes.
TEST_P(ExternalToolsWindowTest, ASequenceIsRunFrameByFrame)
{
    const ExternalTool& under = tool();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::vector<std::string_view> scenes{"stars-rgb-float32", "scene-rgb-float32"};
    const QString path = writeSceneSequence(session(), directory, scenes);
    ASSERT_FALSE(path.isEmpty());
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    std::vector<fixtures::FixtureSamples> stills;
    for (const std::string_view scene : scenes)
    {
        std::optional<fixtures::FixtureSamples> samples = sceneSamples(scene);
        ASSERT_TRUE(samples.has_value());
        stills.push_back(std::move(*samples));
    }
    const std::size_t viewsBefore = context().madeViews.size();
    WindowHandle window = openWindow(sequence);
    ASSERT_TRUE(runAndSettle(context(), *window, under));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
    EXPECT_EQ(standIn().recordedRuns().size(), 2) << "one run per frame";
    const ImageViewControl* result = under.inPlace ? sequence : outputsOf(viewsBefore).front();
    ASSERT_NE(result, nullptr);
    if (!under.inPlace)
    {
        ASSERT_EQ(outputsOf(viewsBefore).size(), under.outputSuffixes.size());
        EXPECT_EQ(result->getUniqueName(), sequence->getUniqueName() + under.outputSuffixes.front());
    }
    ASSERT_TRUE(result->isSequence());
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *result);
    ASSERT_EQ(frames.size(), scenes.size());
    for (std::size_t frame = 0u; frame < frames.size(); ++frame)
    {
        SCOPED_TRACE(frame);
        if (std::string_view(under.name) == "StarnetStarMask")
        {
            EXPECT_EQ(frames[frame].width(), stills[frame].width());
            EXPECT_EQ(frames[frame].channelCount(), stills[frame].channelCount());
        }
        else
        {
            EXPECT_LE(largestFractionDifference(stills[frame], frames[frame]), 1.0 / 65535.0 + 1.0e-6);
        }
    }
    if (under.inPlace)
    {
        ASSERT_NE(sequence->viewHistory(), nullptr);
        EXPECT_EQ(sequence->viewHistory()->previousStepName(), under.processName);
    }
    window.reset();
}

// history: a run in place is one history step under the process's name, which undo takes back to the image as it
// was; a run to new views leaves the target's history as it was and begins each view's with the stated text.
TEST_P(ExternalToolsWindowTest, TheHistoryRecordsARunInPlaceAndNotOneToNewViews)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
    ASSERT_TRUE(before.has_value());
    standIn().setBehaviour("half");
    WindowHandle window = openWindow(target.get());
    ASSERT_TRUE(runAndSettle(context(), *window, under));
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    if (under.inPlace)
    {
        ASSERT_TRUE(history->hasUndo());
        EXPECT_EQ(history->previousStepName(), under.processName);
        const std::optional<fixtures::FixtureSamples> halved = documentSamples(target->document());
        ASSERT_TRUE(halved.has_value());
        EXPECT_NEAR(fractionOf(*halved, 4u, 4u, 0u), 0.5 * fractionOf(*before, 4u, 4u, 0u), 2.0 / 65535.0);
        ASSERT_TRUE(history->undo());
        ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
        ASSERT_TRUE(settleView(*target));
        const std::optional<fixtures::FixtureSamples> undone = documentSamples(target->document());
        ASSERT_TRUE(undone.has_value());
        EXPECT_EQ(undone->digest(), before->digest());
    }
    else
    {
        EXPECT_FALSE(history->hasUndo());
    }
    window.reset();
    dropView(context(), target);
}

// metadata: a StarNet output is derived - the acquisition record, the solution and the lineage kept, the processing
// state under the operation's name and no other card - and a DeepSNR result keeps the image's metadata whole; each
// names the program in its lineage.
TEST_P(ExternalToolsWindowTest, TheResultCarriesItsMetadata)
{
    const ExternalTool& under = tool();
    const std::optional<an::consumer::ImageAsset> plain = sceneAsset(session(), under.scene);
    ASSERT_TRUE(plain.has_value());
    std::optional<an::consumer::ImageAsset> described = withMetadata(session(), *plain, describedMetadata());
    ASSERT_TRUE(described.has_value());
    ViewHandle target = openTarget(std::move(described));
    ASSERT_NE(target, nullptr);
    const std::size_t viewsBefore = context().madeViews.size();
    WindowHandle window = openWindow(target.get());
    ASSERT_TRUE(runAndSettle(context(), *window, under));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
    const ImageViewControl* result = under.inPlace ? target.get() : outputsOf(viewsBefore).front();
    ASSERT_NE(result, nullptr);
    std::shared_ptr<const an::consumer::ImageAsset> asset = result->document().asset();
    ASSERT_NE(asset, nullptr);
    const std::optional<an::ImageMetadata> metadata = assetMetadata(*asset);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->acquisition().objectName(), "M 42");
    EXPECT_TRUE(metadata->astrometry().present());
    EXPECT_TRUE(lineageNames(*metadata, under.programName));
    const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata->codec());
    if (under.inPlace)
    {
        EXPECT_TRUE(hasCard(*metadata, "OBJECT"));
        EXPECT_EQ(metadata->semantics().transferFunction(), an::TransferFunction::Linear);
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Linear);
    }
    else
    {
        EXPECT_FALSE(hasCard(*metadata, "OBJECT"));
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.operation,
                  std::string_view(under.name) == "Starnet" ? "starnet-starless" : "starnet-star-mask");
    }
    window.reset();
    dropView(context(), target);
}

// prestretch: with the automatic stretch the program is handed a stretched image and the answer comes back through
// the inverse: the stand-in's identity answer is still the scene, and the handed copy is brighter than the scene.
TEST_P(ExternalToolsWindowTest, ThePreStretchIsInvertedOnTheWayBack)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
    ASSERT_TRUE(before.has_value());
    const auto handedMean = [this]() -> double {
        an::Result<an::tests::fixtures::engine::DecodedFile> handed = an::tests::fixtures::engine::decodeEngineFile(
            session(), std::filesystem::path(standIn().keptInputPath().toStdWString()));
        EXPECT_TRUE(handed.hasValue()) << handed.status().message();
        if (!handed.hasValue())
        {
            return 0.0;
        }
        double sum = 0.0;
        for (std::uint32_t y = 0u; y < handed->samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < handed->samples.width(); ++x)
            {
                sum += fractionOf(handed->samples, x, y, 0u);
            }
        }
        return sum;
    };
    {
        WindowHandle plain = openWindow(target.get(), "-");
        ASSERT_TRUE(runAndSettle(context(), *plain, under));
    }
    const double unstretched = handedMean();
    const std::size_t viewsBefore = context().madeViews.size();
    WindowHandle window = openWindow(target.get(), "prestretch");
    ASSERT_TRUE(runAndSettle(context(), *window, under));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
    EXPECT_GT(handedMean(), unstretched * 1.05) << "the handed image is stretched";
    if (std::string_view(under.name) != "StarnetStarMask")
    {
        const ImageViewControl* result = under.inPlace ? target.get() : outputsOf(viewsBefore).front();
        ASSERT_NE(result, nullptr);
        const std::optional<fixtures::FixtureSamples> made = documentSamples(result->document());
        ASSERT_TRUE(made.has_value());
        EXPECT_LE(largestFractionDifference(*before, *made), 2.0e-4);
    }
    window.reset();
    dropView(context(), target);
}

INSTANTIATE_TEST_SUITE_P(EveryTool, ExternalToolsWindowTest, ::testing::Range<std::size_t>(0u, 3u), toolName);

// What only StarNet does.
class StarnetWindowTest : public ExternalToolsWindowTest
{
};

// apply: with a star output asked for, a run opens the star mask and the star layer beside the starless image under
// their own names, by the mode: the mask, the unscreened layer, or both.
TEST_P(StarnetWindowTest, TheStarOutputsOpenBesideTheStarlessImage)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
    ASSERT_TRUE(before.has_value());
    struct Mode
    {
        const char* data;
        std::vector<std::string> suffixes;
    };
    for (const Mode& mode :
         {Mode{"mask", {" - starless", " - star mask"}}, Mode{"unscreen", {" - starless", " - stars"}},
          Mode{"both", {" - starless", " - star mask", " - stars"}}})
    {
        SCOPED_TRACE(mode.data);
        const std::size_t viewsBefore = context().madeViews.size();
        WindowHandle window = openWindow(target.get(), "stars");
        setComboData(*window, "starOutputMode", QString::fromLatin1(mode.data));
        ASSERT_TRUE(runAndSettle(context(), *window, under));
        EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
        const std::vector<ImageViewControl*> outputs = outputsOf(viewsBefore);
        ASSERT_EQ(outputs.size(), mode.suffixes.size());
        for (std::size_t index = 0u; index < outputs.size(); ++index)
        {
            EXPECT_EQ(outputs[index]->getUniqueName(), target->getUniqueName() + mode.suffixes[index]);
        }
        if (std::string_view(mode.data) == "unscreen")
        {
            // The stand-in's star layer is half the handed codes, brought back by the span alone.
            const std::optional<fixtures::FixtureSamples> stars = documentSamples(outputs.back()->document());
            ASSERT_TRUE(stars.has_value());
            EXPECT_NEAR(fractionOf(*stars, 4u, 4u, 1u), 0.5 * fractionOf(*before, 4u, 4u, 1u), 2.0 / 65535.0);
        }
        window.reset();
    }
    dropView(context(), target);
}

// The star reduction's integrated generation: the launcher opens StarNet set to the unscreened star layer and no star
// output, the run hands the starless view to the caller once it is shown, and the window closes after.
TEST_P(StarnetWindowTest, TheIntegratedGenerationHandsTheStarlessViewOnAndCloses)
{
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    const std::size_t viewsBefore = context().madeViews.size();
    ImageViewControl* handedOn = nullptr;
    an::ui::ProcessLauncher::openIntegratedStarlessGeneration(target.get(),
                                                              [&handedOn](ImageViewControl* view) { handedOn = view; });
    ASSERT_EQ(context().openProcesses.size(), 1u);
    auto* window = dynamic_cast<StarnetProcess*>(context().openProcesses.front());
    ASSERT_NE(window, nullptr);
    const StarnetProcess::Settings settings = window->settings();
    EXPECT_FALSE(settings.generateStarOutput);
    EXPECT_EQ(settings.starOutputMode, QStringLiteral("unscreen"));
    pressOk(*window);
    ASSERT_TRUE(pumpUntil([this]() { return context().openProcesses.empty(); })) << "the window closes once shown";
    ASSERT_TRUE(settleAll(context()));
    const std::vector<ImageViewControl*> outputs = outputsOf(viewsBefore);
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(handedOn, outputs.front());
    EXPECT_EQ(outputs.front()->getUniqueName(), target->getUniqueName() + " - starless");
    dropView(context(), target);
}

INSTANTIATE_TEST_SUITE_P(Starnet, StarnetWindowTest, ::testing::Values<std::size_t>(0u), toolName);

// What only the star mask does.
class StarMaskWindowTest : public ExternalToolsWindowTest
{
};

// The shaping: the lightness of a colour mask is one grey view, the binary selection is one or zero, and the mask as
// the tool wrote it keeps its three channels.
TEST_P(StarMaskWindowTest, TheMaskIsShapedAsTheDialogAsks)
{
    const ExternalTool& under = tool();
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    for (const char* mode : {"-", "lightness", "binary"})
    {
        SCOPED_TRACE(mode);
        const std::size_t viewsBefore = context().madeViews.size();
        WindowHandle window = openWindow(target.get(), mode);
        ASSERT_TRUE(runAndSettle(context(), *window, under));
        EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
        const std::vector<ImageViewControl*> outputs = outputsOf(viewsBefore);
        ASSERT_EQ(outputs.size(), 1u);
        const std::optional<fixtures::FixtureSamples> made = documentSamples(outputs.front()->document());
        ASSERT_TRUE(made.has_value());
        EXPECT_EQ(made->channelCount(), std::string_view(mode) == "-" ? 3u : 1u);
        EXPECT_EQ(made->sampleType(), fixtures::FixtureSampleType::Float32);
        if (std::string_view(mode) == "binary")
        {
            for (std::uint32_t y = 0u; y < made->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < made->width(); ++x)
                {
                    const double value = fractionOf(*made, x, y, 0u);
                    EXPECT_TRUE(value < 1.0e-6 || value > 1.0 - 1.0e-6) << x << "," << y << " " << value;
                }
            }
        }
        window.reset();
    }
    dropView(context(), target);
}

INSTANTIATE_TEST_SUITE_P(StarMask, StarMaskWindowTest, ::testing::Values<std::size_t>(1u), toolName);

// What only DeepSNR does.
class DeepSnrWindowTest : public ExternalToolsWindowTest
{
};

// The program's floor: the product's window refuses an image below 512 by 512 by name before anything runs, and a grey
// image of that size runs.
TEST_P(DeepSnrWindowTest, AnImageBelowTheFloorIsRefusedByName)
{
    ViewHandle target = openTarget();
    ASSERT_NE(target, nullptr);
    {
        WindowHandle window(new DeepSNRProcess(target.get()));
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        pressOk(*window);
        ASSERT_TRUE(settleWindow(context(), *window));
        ASSERT_EQ(context().messages.size(), 1);
        EXPECT_NE(shownError(context()).indexOf(QStringLiteral("at least 512x512")), -1)
            << shownError(context()).toStdString();
        EXPECT_TRUE(standIn().recordedRuns().isEmpty());
    }
    dropView(context(), target);
    context().messages.clear();
    context().errorTexts.clear();
    std::optional<an::consumer::ImageAsset> large =
        madeAsset(session(), 512u, 512u, 1u, an::SampleType::UInt16,
                  [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return ((x + y) % 64u) / 64.0; });
    ASSERT_TRUE(large.has_value());
    ViewHandle big = openTarget(std::move(large));
    ASSERT_NE(big, nullptr);
    {
        WindowHandle window(new DeepSNRProcess(big.get()));
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        ASSERT_TRUE(runAndSettle(context(), *window, tool()));
        EXPECT_TRUE(context().errorTexts.isEmpty()) << shownError(context()).toStdString();
        EXPECT_EQ(standIn().recordedRuns().size(), 1);
        ASSERT_NE(big->viewHistory(), nullptr);
        EXPECT_TRUE(big->viewHistory()->hasUndo());
    }
    dropView(context(), big);
}

INSTANTIATE_TEST_SUITE_P(DeepSnr, DeepSnrWindowTest, ::testing::Values<std::size_t>(2u), toolName);
