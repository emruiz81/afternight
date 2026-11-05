// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each colour calibration window does beyond the result the records hold: the launch through its registry
/// descriptor, what it keeps between openings, a stopped run, a run that reads the dialog as it was when Apply was
/// pressed, the view's mask, every representation an image can hold, a run the engine refuses, a sequence, the history
/// and the result's metadata. One case per behaviour, run for every process.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "app/FileCompanions.h"
#include "app/ProcessRegistry.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ProcessCompanions.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::colortools;

/// What a case opens a process over: a view of an image or of a sequence, and the linear match's reference beside it.
struct Targets
{
    ViewHandle target;
    ViewHandle reference;
    ImageViewControl* sequence = nullptr;

    [[nodiscard]] ImageViewControl* view() const
    {
        return sequence != nullptr ? sequence : target.get();
    }
};

/// The reference a linear match reads: the second scene, in a view of its own made active.
[[nodiscard]] ViewHandle referenceView(ProcessTestContext& context, std::optional<an::consumer::ImageAsset> asset = {})
{
    if (!asset.has_value())
    {
        asset = sceneAsset(context.engineSession(), "scene-rgb-float32-second");
    }
    if (!asset.has_value())
    {
        return ViewHandle();
    }
    ViewHandle view = assetView(std::move(*asset), kReferenceView);
    activate(context, *view);
    return view;
}

/// Opens what a process runs over: its scene, or a given asset, with the reference a linear match reads.
[[nodiscard]] Targets openTargets(ProcessTestContext& context, const ColorTool& tool,
                                  std::optional<an::consumer::ImageAsset> asset = std::nullopt)
{
    Targets targets;
    if (tool.needsReference)
    {
        targets.reference = referenceView(context);
    }
    if (!asset.has_value())
    {
        asset = sceneAsset(context.engineSession(), tool.scene);
    }
    if (!asset.has_value())
    {
        return targets;
    }
    targets.target = assetView(std::move(*asset), "target");
    activate(context, *targets.target);
    return targets;
}

void dropTargets(ProcessTestContext& context, Targets& targets)
{
    if (targets.target != nullptr)
    {
        dropView(context, targets.target);
    }
    if (targets.reference != nullptr)
    {
        dropView(context, targets.reference);
    }
}

/// Opens a process's window over its targets, set to its cases' mode, reading the reference a linear match reads.
[[nodiscard]] WindowHandle openWindow(const ColorTool& tool, const Targets& targets, bool stopping = false)
{
    WindowHandle window = stopping ? tool.openStopping(targets.view()) : tool.open(targets.view());
    tool.configure(*window, "-");
    if (tool.needsReference)
    {
        tool.chooseReference(*window, QString::fromLatin1(kReferenceView));
    }
    return window;
}

/// Every widget value a window shows, as text, but the target selector, which follows the views.
[[nodiscard]] std::string dialogState(QWidget& window)
{
    std::string state;
    for (const QSpinBox* box : window.findChildren<QSpinBox*>())
    {
        state += box->objectName().toStdString() + "=" + std::to_string(box->value()) + ";";
    }
    for (const QDoubleSpinBox* box : window.findChildren<QDoubleSpinBox*>())
    {
        state += box->objectName().toStdString() + "=" + std::to_string(box->value()) + ";";
    }
    for (const QCheckBox* box : window.findChildren<QCheckBox*>())
    {
        state += box->objectName().toStdString() + "=" + (box->isChecked() ? "1;" : "0;");
    }
    for (const QGroupBox* box : window.findChildren<QGroupBox*>())
    {
        if (box->isCheckable())
        {
            state += box->objectName().toStdString() + "=" + (box->isChecked() ? "1;" : "0;");
        }
    }
    for (const QComboBox* combo : window.findChildren<QComboBox*>())
    {
        if (combo->objectName() != QStringLiteral("targetImageSelector"))
        {
            state += combo->objectName().toStdString() + "=" + combo->currentText().toStdString() + ";";
        }
    }
    return state;
}

/// Sets a window's dialog to something other than its own values, as a person changes it, so that a run reading it
/// makes another result.
void changeDialog(const ColorTool& tool, QWidget& window)
{
    const std::string name = tool.name;
    if (name == "ColorCalibration")
    {
        setDouble(window, "whiteUpperLimitValue", 0.5);
    }
    else if (name == "BackgroundNeutralization")
    {
        setDouble(window, "backgroundUpperLimitValue", 0.5);
    }
    else if (name == "LinearMatch")
    {
        setDouble(window, "rejectHighValue", 0.5);
    }
    else if (name == "Scnr")
    {
        setComboText(window, "protectionType", QStringLiteral("Maximum Neutral"));
    }
}

class ColorToolsWindowTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const ColorTool& tool() const
    {
        return colorTools()[GetParam()];
    }

    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        releaseContext();
    }

    void releaseContext()
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

    /// Opens the process's targets and window, runs one Apply of its cases' mode, and returns what it published.
    [[nodiscard]] std::optional<fixtures::FixtureSamples>
    runOnce(std::optional<an::consumer::ImageAsset> asset = std::nullopt)
    {
        Targets targets = openTargets(context(), tool(), std::move(asset));
        if (targets.view() == nullptr)
        {
            return std::nullopt;
        }
        WindowHandle window = openWindow(tool(), targets);
        if (!applyAndSettle(context(), *window, tool()))
        {
            return std::nullopt;
        }
        std::optional<fixtures::FixtureSamples> made = documentSamples(targets.view()->document());
        window.reset();
        dropTargets(context(), targets);
        return made;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

std::string toolName(const ::testing::TestParamInfo<std::size_t>& info)
{
    return colorTools()[info.param].name;
}

/// The metadata a document's image carries.
[[nodiscard]] std::optional<an::ImageMetadata> documentMetadata(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease->image());
}

/// Whether metadata carries a card of a keyword.
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

/// Every frame of a sequence file as recorded, each a new asset: what a still image of the frame is.
[[nodiscard]] std::vector<an::consumer::ImageAsset> frameAssets(const an::consumer::EngineSession& session,
                                                                const QString& path)
{
    std::vector<an::consumer::ImageAsset> frames;
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(session, path, an::SequenceFormat::ImageList, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue() || !document->sequence().has_value())
    {
        return frames;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
        an::ui::openProcessFrames(session, *document->sequence(), false);
    EXPECT_TRUE(scope.hasValue() && reader.hasValue());
    if (!scope.hasValue() || !reader.hasValue())
    {
        return frames;
    }
    for (;;)
    {
        an::Result<std::optional<an::ui::ProcessSequenceFrame>> frame =
            (*reader)->next(scope->engine(), scope->execution());
        if (!frame.hasValue() || !frame->has_value())
        {
            break;
        }
        frames.push_back(std::move((**frame).asset));
    }
    scope->finish(an::Status::success());
    return frames;
}

/// The cases of the tools that read a reference view the dialog names.
class ColorToolsReferenceTest : public ColorToolsWindowTest
{
};

} // namespace

// launch: the registered action opens the process's window over the active view under the process's name, needing the
// view to itself where the descriptor says so.
TEST_P(ColorToolsWindowTest, TheRegisteredActionOpensTheWindow)
{
    const ColorTool& under = tool();
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets targets = openTargets(context(), under);
    ASSERT_NE(targets.view(), nullptr);
    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(under.descriptorId);
    ASSERT_NE(descriptor, nullptr) << under.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, under.action);
    EXPECT_EQ(descriptor->process_name, under.processName);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(targets.view());
    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, under.className);
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    QWidget* window = context().openProcesses.front();
    EXPECT_TRUE(window->isVisible());
    auto* single = dynamic_cast<ImageProcessControl*>(window);
    ASSERT_NE(single, nullptr);
    EXPECT_EQ(single->getProcessName(), under.processName);
    EXPECT_EQ(single->attachmentName(), under.processName);
    EXPECT_EQ(single->requiresExclusiveAccess(), under.exclusive);
    window->close();
    ASSERT_TRUE(settleAll(context()));
    dropTargets(context(), targets);
}

// persistence: a window keeps nothing between openings - one opened after another was changed and closed shows the
// dialog's own values.
TEST_P(ColorToolsWindowTest, AWindowOpenedAgainShowsTheDialogsOwnValues)
{
    const ColorTool& under = tool();
    Targets targets = openTargets(context(), under);
    std::string fresh;
    {
        WindowHandle window = under.open(targets.view());
        fresh = dialogState(*window);
        changeDialog(under, *window);
        EXPECT_NE(dialogState(*window), fresh) << "the change reaches the dialog";
    }
    WindowHandle again = under.open(targets.view());
    EXPECT_EQ(dialogState(*again), fresh);
    again.reset();
    dropTargets(context(), targets);
}

// cancel: a run whose stop is asked for publishes nothing, leaves the image and its history as they were, says nothing,
// and leaves the window open to run again.
TEST_P(ColorToolsWindowTest, AStoppedRunPublishesNothing)
{
    const ColorTool& under = tool();
    Targets targets = openTargets(context(), under);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(targets.target->document());
    WindowHandle window = openWindow(under, targets, true);
    const std::size_t viewsBefore = context().madeViews.size();
    const int errorsBefore = context().errorCount();
    ASSERT_TRUE(applyAndSettle(context(), *window, under));
    EXPECT_EQ(context().madeViews.size(), viewsBefore) << "nothing is opened";
    EXPECT_EQ(context().errorCount(), errorsBefore) << "a stop the user asked for is quiet";
    const std::optional<fixtures::FixtureSamples> after = documentSamples(targets.target->document());
    ASSERT_TRUE(before.has_value() && after.has_value());
    EXPECT_EQ(after->digest(), before->digest());
    EXPECT_FALSE(targets.target->viewHistory()->hasUndo());
    QPushButton* apply = applyButtonOf(*window, under);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->isEnabled()) << "the window can run again";
    window.reset();
    dropTargets(context(), targets);
}

// parameter-snapshot: a run reads the dialog as it was when Apply was pressed: a value changed while it runs does not
// reach it.
TEST_P(ColorToolsWindowTest, ARunReadsTheDialogAsItWasWhenApplyWasPressed)
{
    const ColorTool& under = tool();
    const std::optional<fixtures::FixtureSamples> asPressed = runOnce();
    ASSERT_TRUE(asPressed.has_value());

    Targets targets = openTargets(context(), under);
    WindowHandle window = openWindow(under, targets);
    pressApply(*window, under);
    // The dialog changes before the run's worker has landed anything.
    changeDialog(under, *window);
    ASSERT_TRUE(settleWindow(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(targets.view()->document());
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->digest(), asPressed->digest()) << "the run read the dialog after Apply was pressed";
    window.reset();
    dropTargets(context(), targets);

    // The change itself makes another result, so the case would see a run that read it.
    Targets changed = openTargets(context(), under);
    WindowHandle second = openWindow(under, changed);
    changeDialog(under, *second);
    ASSERT_TRUE(applyAndSettle(context(), *second, under));
    const std::optional<fixtures::FixtureSamples> other = documentSamples(changed.view()->document());
    ASSERT_TRUE(other.has_value());
    EXPECT_NE(other->digest(), asPressed->digest());
    second.reset();
    dropTargets(context(), changed);
}

// apply-mask: the view's mask weighs the result - the source where the mask is empty, the result where it is full -
// and an inverted mask the other way round; the linear match applies no mask.
TEST_P(ColorToolsWindowTest, TheViewsMaskWeighsTheResult)
{
    const ColorTool& under = tool();
    const std::optional<fixtures::FixtureSamples> source = sceneSamples(under.scene);
    const std::optional<fixtures::FixtureSamples> plain = runOnce();
    ASSERT_TRUE(source.has_value() && plain.has_value());
    for (const bool inverted : {false, true})
    {
        SCOPED_TRACE(inverted ? "inverted" : "as it is");
        Targets targets = openTargets(context(), under);
        const std::uint32_t width = source->width();
        std::optional<an::consumer::ImageAsset> maskAsset =
            an::tests::viewer::makeAsset<float>(session(), width, source->height(), an::tests::viewer::Channels::Grey,
                                                [width](std::uint32_t x, std::uint32_t, std::uint32_t) {
                                                    return static_cast<float>(x) / static_cast<float>(width - 1u);
                                                });
        ASSERT_TRUE(maskAsset.has_value());
        ViewHandle mask = assetView(std::move(*maskAsset), "mask");
        targets.view()->setCurrentMask(mask.get());
        targets.view()->setEnableMask(true);
        targets.view()->setInvertMask(inverted);
        ASSERT_TRUE(targets.view()->isMasked());
        WindowHandle window = openWindow(under, targets);
        ASSERT_TRUE(applyAndSettle(context(), *window, under));
        const std::optional<fixtures::FixtureSamples> made = documentSamples(targets.view()->document());
        ASSERT_TRUE(made.has_value());
        if (std::string_view(under.name) == "LinearMatch")
        {
            EXPECT_EQ(made->digest(), plain->digest()) << "the mask changed what the linear match made";
        }
        else
        {
            double largest = 0.0;
            for (std::uint32_t c = 0u; c < made->channelCount(); ++c)
            {
                for (std::uint32_t y = 0u; y < made->height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < made->width(); ++x)
                    {
                        const double weight =
                            static_cast<double>(static_cast<float>(x) / static_cast<float>(width - 1u));
                        const double w = inverted ? 1.0 - weight : weight;
                        const double expected =
                            fractionOf(*source, x, y, c) * (1.0 - w) + fractionOf(*plain, x, y, c) * w;
                        largest = (std::max)(largest, std::fabs(fractionOf(*made, x, y, c) - expected));
                    }
                }
            }
            EXPECT_LE(largest, 1.0e-6) << "the result is not the source and the plain result weighed by the mask";
        }
        window.reset();
        targets.view()->removeCurrentMask();
        dropTargets(context(), targets);
        dropView(context(), mask);
    }
}

// representation: every representation an image can hold is run and kept, each sample worked as a fraction of full
// scale and brought back to the image's own representation.
TEST_P(ColorToolsWindowTest, EveryRepresentationIsKept)
{
    const ColorTool& under = tool();
    const std::optional<an::consumer::ImageAsset> scene = sceneAsset(session(), under.scene);
    ASSERT_TRUE(scene.has_value());
    const std::optional<fixtures::FixtureSamples> asFloat = runOnce();
    ASSERT_TRUE(asFloat.has_value());
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        std::optional<an::consumer::ImageAsset> converted = convertedTo(session(), *scene, type);
        ASSERT_TRUE(converted.has_value());
        Targets targets = openTargets(context(), under, std::move(*converted));
        WindowHandle window = openWindow(under, targets);
        ASSERT_TRUE(applyAndSettle(context(), *window, under));
        const std::optional<an::ImageDescriptor> described = targets.view()->imageDescriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->sampleType(), type);
        // A signed sample is a fraction counted from its representation's smallest value, so a scene converted at
        // two's complement holds its levels about one half - where no background sample of a neutralization's limits
        // lies, which it refuses by name - and its result is not the floating scene's.
        const bool isSigned =
            type == an::SampleType::Int8 || type == an::SampleType::Int16 || type == an::SampleType::Int32;
        if (isSigned)
        {
            const bool recorded = targets.view()->viewHistory() != nullptr && targets.view()->viewHistory()->hasUndo();
            EXPECT_TRUE(recorded || context().errorTexts.size() == 1)
                << "a signed image is either run or refused with a reason";
        }
        else
        {
            EXPECT_TRUE(context().errorTexts.isEmpty())
                << context().errorTexts.join(QStringLiteral("; ")).toStdString();
            ASSERT_NE(targets.view()->viewHistory(), nullptr);
            EXPECT_TRUE(targets.view()->viewHistory()->hasUndo()) << "the run was recorded";
            const std::optional<fixtures::FixtureSamples> made = documentSamples(targets.view()->document());
            ASSERT_TRUE(made.has_value());
            // An eight-bit image's codes are coarse enough that the scene's levels, and so the measurements, move.
            if (type != an::SampleType::UInt8)
            {
                EXPECT_LE(largestFractionDifference(*made, *asFloat), 5.0e-3)
                    << "the representation changed the result";
            }
        }
        context().errorTexts.clear();
        window.reset();
        dropTargets(context(), targets);
    }
}

// error: a run whose memory the engine refuses publishes nothing, says why under the process's title, and leaves the
// image and its history as they were.
TEST_P(ColorToolsWindowTest, ARunTheEngineRefusesSaysWhyAndChangesNothing)
{
    const ColorTool& under = tool();
    // A session whose engine holds the images and little more than a copy of one.
    constexpr std::uint32_t kSide = 256u;
    const std::uint64_t imageBytes = static_cast<std::uint64_t>(kSide) * kSide * 3u * sizeof(float);
    releaseContext();
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 4u;
    settings.limits.memoryBudgetBytes = imageBytes * (under.needsReference ? 7u : 3u) / 2u;
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    ProcessTestContext tight(std::move(*opened));
    const InstalledContext installed(tight);
    const auto pattern = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<float>(0.02 + 0.8 * static_cast<double>((x * 7u + y * 3u + c * 11u) % kSide) / kSide);
    };
    ViewHandle reference;
    if (under.needsReference)
    {
        std::optional<an::consumer::ImageAsset> second = an::tests::viewer::makeAsset<float>(
            tight.engineSession(), kSide, kSide, an::tests::viewer::Channels::Rgb, pattern);
        ASSERT_TRUE(second.has_value());
        reference = referenceView(tight, std::move(*second));
    }
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        tight.engineSession(), kSide, kSide, an::tests::viewer::Channels::Rgb, pattern);
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset), "target");
    activate(tight, *target);
    ASSERT_TRUE(settleView(*target));
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = under.open(target.get());
    under.configure(*window, "-");
    if (under.needsReference)
    {
        under.chooseReference(*window, QString::fromLatin1(kReferenceView));
    }
    ASSERT_TRUE(applyAndSettle(tight, *window, under));
    EXPECT_EQ(target->document().revision(), before);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([&tight]() { return tight.errorCount() >= 1; })) << "the window says why";
    EXPECT_TRUE(tight.messages.back().contains(QString::fromLatin1(under.errorTitle)))
        << tight.messages.back().toStdString();
    ASSERT_FALSE(tight.errorTexts.isEmpty());
    EXPECT_TRUE(tight.errorTexts.back().startsWith(QString::fromLatin1(under.errorLead) + QStringLiteral(":")))
        << tight.errorTexts.back().toStdString();
    window.reset();
    dropView(tight, target);
    if (reference != nullptr)
    {
        dropView(tight, reference);
    }
}

// sequence: over a sequence every frame becomes what the still image of it becomes against the same reference - the
// frame the view shows, when the reference is the target - published whole behind a progress window, the view showing
// the new sequence as one history step.
TEST_P(ColorToolsWindowTest, EveryFrameOfASequenceIsWhatTheStillImageOfItBecomes)
{
    const ColorTool& under = tool();
    const std::vector<std::string_view> scenes{under.scene, std::string_view(under.scene) == "stars-rgb-float32"
                                                                ? std::string_view("stars-rgb-float32-starless")
                                                                : std::string_view("scene-rgb-float32-second")};
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(session(), directory, scenes);
    ASSERT_FALSE(path.isEmpty());
    // What each frame, as the sequence holds it, becomes as a still image read against the frame the sequence's view
    // shows - its second, so a run that measured the first frame it reads would differ - which is the reference a
    // sequence run of the colour calibration and the neutralization reads.
    std::vector<an::consumer::ImageAsset> recorded = frameAssets(session(), path);
    ASSERT_EQ(recorded.size(), scenes.size());
    std::vector<fixtures::FixtureSamples> stills;
    for (an::consumer::ImageAsset& frame : recorded)
    {
        ViewHandle firstFrame;
        if (under.chooseReference && !under.needsReference)
        {
            std::vector<an::consumer::ImageAsset> again = frameAssets(session(), path);
            ASSERT_FALSE(again.empty());
            ASSERT_GE(again.size(), 2u);
            firstFrame = assetView(std::move(again[1]), "shown");
            activate(context(), *firstFrame);
        }
        Targets still = openTargets(context(), under, std::move(frame));
        WindowHandle window = openWindow(under, still);
        if (firstFrame != nullptr)
        {
            under.chooseReference(*window, QStringLiteral("shown"));
        }
        ASSERT_TRUE(applyAndSettle(context(), *window, under));
        std::optional<fixtures::FixtureSamples> made = documentSamples(still.view()->document());
        ASSERT_TRUE(made.has_value());
        stills.push_back(std::move(*made));
        window.reset();
        dropTargets(context(), still);
        if (firstFrame != nullptr)
        {
            dropView(context(), firstFrame);
        }
    }

    Targets targets;
    if (under.needsReference)
    {
        targets.reference = referenceView(context());
    }
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    ASSERT_NE(sequence->sequencePlayer(), nullptr);
    bool onSecond = false;
    sequence->sequencePlayer()->setCurrentFrame(1u, [&onSecond]() { onSecond = true; });
    ASSERT_TRUE(pumpUntil([&onSecond]() { return onSecond; }));
    ASSERT_TRUE(settleView(*sequence));
    targets.sequence = sequence;
    WindowHandle window = openWindow(under, targets);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window, under));
    EXPECT_EQ(context().madeViews.size(), before);
    const std::optional<an::ui::DocumentSequence> shown = sequence->document().sequence();
    ASSERT_TRUE(shown.has_value());
    EXPECT_NE(shown->sourcePath, path) << "the view shows the new sequence";
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *sequence);
    ASSERT_EQ(frames.size(), scenes.size());
    for (std::size_t frame = 0u; frame < frames.size(); ++frame)
    {
        EXPECT_EQ(frames[frame].digest(), stills[frame].digest()) << "frame " << frame;
    }
    ASSERT_NE(sequence->viewHistory(), nullptr);
    EXPECT_EQ(sequence->viewHistory()->previousStepName(), under.processName);
    window.reset();
    dropTargets(context(), targets);
}

// history: an Apply is one history step under the process's name, which undo takes back to the image as it was; the
// window closes once its result is shown, but for SCNR, which stays open for another run.
TEST_P(ColorToolsWindowTest, AnApplyIsOneHistoryStepAndTheWindowClosesOrStays)
{
    const ColorTool& under = tool();
    Targets targets = openTargets(context(), under);
    const std::optional<fixtures::FixtureSamples> before = documentSamples(targets.target->document());
    ASSERT_TRUE(before.has_value());
    WindowHandle window = openWindow(under, targets);
    window->show();
    ASSERT_TRUE(applyAndSettle(context(), *window, under));
    an::ui::IViewHistory* history = targets.target->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), under.processName);
    EXPECT_EQ(window->isVisible(), !under.closesWhenShown);
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*targets.target));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(targets.target->document());
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->digest(), before->digest());
    window.reset();
    dropTargets(context(), targets);
}

// metadata: a result keeps the image's acquisition record, solution and cards, with a lineage entry naming the engine
// operation that changed its samples; the neutralization, the colour calibration and the linear match keep its meaning,
// and SCNR records its result nonlinear under its own name.
TEST_P(ColorToolsWindowTest, AResultKeepsTheMetadataAndNamesItsOperationInTheLineage)
{
    const ColorTool& under = tool();
    const std::optional<an::consumer::ImageAsset> plain = sceneAsset(session(), under.scene);
    ASSERT_TRUE(plain.has_value());
    std::optional<an::consumer::ImageAsset> described = withMetadata(session(), *plain, describedMetadata());
    ASSERT_TRUE(described.has_value());
    Targets targets = openTargets(context(), under, std::move(*described));
    WindowHandle window = openWindow(under, targets);
    ASSERT_TRUE(applyAndSettle(context(), *window, under));
    const std::optional<an::ImageMetadata> result = documentMetadata(targets.target->document());
    ASSERT_TRUE(result.has_value());
    const an::ImageMetadata expected = describedMetadata();
    EXPECT_EQ(result->acquisition(), expected.acquisition());
    EXPECT_EQ(result->astrometry(), expected.astrometry());
    EXPECT_TRUE(hasCard(*result, "OBJECT"));
    EXPECT_TRUE(hasCard(*result, "ANLIN"));
    const std::string name = under.name;
    const an::ProcessingStateRecord state = an::readProcessingStateCards(result->codec());
    if (name == "Scnr")
    {
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.assignmentSource, an::StateAssignmentSource::Process);
        EXPECT_EQ(state.operation, "scnr");
        EXPECT_NE(result->semantics().transferFunction(), an::TransferFunction::Linear);
    }
    else
    {
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Linear) << "the cards still say what the source's said";
        EXPECT_EQ(result->semantics(), expected.semantics()) << "the meaning is the source's";
    }
    ASSERT_FALSE(result->provenance().entries().empty());
    const std::string operation = result->provenance().entries().back().operation();
    EXPECT_EQ(operation, name == "Scnr"          ? "an::reduceImageChromaticNoise"
                         : name == "LinearMatch" ? "an::applyGlobalNormalization"
                                                 : "an::applyColorCalibration");
    window.reset();
    dropTargets(context(), targets);
}

// leases: a run reads the references the dialog names under leases it takes before it asks to be admitted, in one order
// with its target's writer, and reads a view the dialog names twice under one lease. An edit of the reference's view
// holds that view's writer and then asks to be admitted, so at a width of one a run admitted first would hold the only
// thread while it waited for the writer, and the edit would wait for the thread: neither would end.
TEST_P(ColorToolsReferenceTest, ARunWaitingForAnEditOfItsReferenceHoldsNoThreadTheEditNeeds)
{
    const ColorTool& under = tool();
    releaseContext();
    std::unique_ptr<ProcessTestContext> narrow = openProcessContext(1u);
    ASSERT_NE(narrow, nullptr);
    const InstalledContext installed(*narrow);
    Targets targets = openTargets(*narrow, under);
    ASSERT_NE(targets.target, nullptr);
    if (targets.reference == nullptr)
    {
        // Another view of the tool's own scene, whose samples its default limits are made for.
        targets.reference = referenceView(*narrow, sceneAsset(narrow->engineSession(), under.scene));
    }
    ASSERT_NE(targets.reference, nullptr);
    WindowHandle window = openWindow(under, targets);
    under.chooseReference(*window, QString::fromLatin1(kReferenceView));

    an::tests::viewer::EditHoldingTheWriter edit(*targets.reference);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    pressApply(*window, under);
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the run held while the run waited for the edit's writer";

    ASSERT_TRUE(settleWindow(*narrow, *window));
    EXPECT_EQ(narrow->errorCount(), 0) << narrow->errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_NE(targets.target->viewHistory(), nullptr);
    EXPECT_TRUE(targets.target->viewHistory()->hasUndo()) << "the run was not shown";
    window.reset();
    dropTargets(*narrow, targets);
}

// leases: a run reads the view's mask under a lease it takes before it asks to be admitted, in one order with its
// target's writer: an edit of the mask's view holds that writer and then asks to be admitted.
TEST_P(ColorToolsWindowTest, ARunWaitingForAnEditOfItsMaskHoldsNoThreadTheEditNeeds)
{
    const ColorTool& under = tool();
    releaseContext();
    std::unique_ptr<ProcessTestContext> narrow = openProcessContext(1u);
    ASSERT_NE(narrow, nullptr);
    const InstalledContext installed(*narrow);
    Targets targets = openTargets(*narrow, under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<an::ImageDescriptor> described = targets.target->imageDescriptor();
    ASSERT_TRUE(described.has_value());
    std::optional<an::consumer::ImageAsset> maskAsset = an::tests::viewer::makeAsset<float>(
        narrow->engineSession(), described->extent().width(), described->extent().height(),
        an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<float>(x % 2u); });
    ASSERT_TRUE(maskAsset.has_value());
    ViewHandle mask = assetView(std::move(*maskAsset), "mask");
    targets.target->setCurrentMask(mask.get());
    targets.target->setEnableMask(true);
    ASSERT_TRUE(targets.target->isMasked());
    WindowHandle window = openWindow(under, targets);

    an::tests::viewer::EditHoldingTheWriter edit(*mask);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    pressApply(*window, under);
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the mask's edit waited for the thread the run held while the run waited for the mask's writer";

    ASSERT_TRUE(settleWindow(*narrow, *window));
    EXPECT_EQ(narrow->errorCount(), 0) << narrow->errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    targets.target->removeCurrentMask();
    dropTargets(*narrow, targets);
    dropView(*narrow, mask);
}

INSTANTIATE_TEST_SUITE_P(Tools, ColorToolsWindowTest, ::testing::Range<std::size_t>(0u, colorTools().size()), toolName);
INSTANTIATE_TEST_SUITE_P(References, ColorToolsReferenceTest, ::testing::ValuesIn([]() {
                             std::vector<std::size_t> reading;
                             for (std::size_t index = 0u; index < colorTools().size(); ++index)
                             {
                                 if (std::string_view(colorTools()[index].name) != "Scnr")
                                 {
                                     reading.push_back(index);
                                 }
                             }
                             return reading;
                         }()),
                         toolName);
