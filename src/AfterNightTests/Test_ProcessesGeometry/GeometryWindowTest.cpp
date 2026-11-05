// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each geometry and filter-array window does beyond the result the records hold: the launch through its registry
/// descriptor, what it keeps between openings, a stopped run, a run that reads the dialog as it was when Apply was
/// pressed, a mask on the target, every representation an image can hold, a run the engine refuses, a sequence, the
/// history and the result's metadata. One case per behaviour, run for every process its behaviour applies to.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"

#include "app/FileCompanions.h"
#include "app/ProcessRegistry.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ProcessCompanions.h"
#include "processes/CfaProcessSupport.h"
#include "processes/CropProcess.h"
#include "processes/ExtractSequenceProcess.h"
#include "processes/IntegerResampleProcess.h"
#include "processes/MergeCFAProcess.h"
#include "processes/ResizeProcess.h"
#include "processes/RotateProcess.h"

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::geometry;

/// What a case opens a process over: a view of an image, or of a sequence, or the four plane views a Merge CFA window
/// reads.
struct Targets
{
    ViewHandle target;
    ImageViewControl* sequence = nullptr;
    std::vector<ViewHandle> planes;
    std::unique_ptr<QTemporaryDir> directory;

    [[nodiscard]] ImageViewControl* view() const
    {
        return sequence != nullptr ? sequence : target.get();
    }
};

/// The four planes of a mosaic asset as views named "cfa0" to "cfa3", made active in turn.
[[nodiscard]] std::vector<ViewHandle> planeViewsOf(ProcessTestContext& context, const an::consumer::ImageAsset& mosaic)
{
    std::vector<ViewHandle> views;
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        context.engineSession(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = mosaic.read();
    EXPECT_TRUE(scope.hasValue() && lease.hasValue());
    if (!scope.hasValue() || !lease.hasValue())
    {
        return views;
    }
    an::Result<std::vector<an::consumer::ImageAsset>> planes =
        an::ui::processes::splitMosaicPlanes(*scope, an::ui::ProcessSource::of(*lease));
    EXPECT_TRUE(planes.hasValue()) << planes.status().message();
    lease->release();
    scope->finish(an::Status::success());
    if (!planes.hasValue())
    {
        return views;
    }
    for (std::size_t plane = 0u; plane < planes->size(); ++plane)
    {
        views.push_back(assetView(std::move((*planes)[plane]), "cfa" + std::to_string(plane)));
        activate(context, *views.back());
    }
    return views;
}

/// The asset of a fixture scene.
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                                 std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples(scene);
    if (image == nullptr || !samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = an::tests::fixtures::engine::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
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

/// Opens what a process runs over: its scene, or a given asset; a sequence of its scene's frames for Extract Sequence;
/// the planes of its mosaic for Merge CFA.
[[nodiscard]] Targets openTargets(ProcessTestContext& context, const GeometryProcess& process,
                                  std::optional<an::consumer::ImageAsset> asset = std::nullopt)
{
    Targets targets;
    if (!asset.has_value())
    {
        asset = sceneAsset(context.engineSession(), process.scene);
    }
    if (!asset.has_value())
    {
        return targets;
    }
    if (process.landing == Landing::TopLevel)
    {
        targets.planes = planeViewsOf(context, *asset);
        return targets;
    }
    if (std::string_view(process.name) == "ExtractSequence")
    {
        targets.directory = std::make_unique<QTemporaryDir>();
        const QString path = writeSceneSequence(context.engineSession(), *targets.directory,
                                                {"scene-rgb-float32", "scene-rgb-float32-second", "scene-rgb-float32"});
        targets.sequence = openSequenceView(context, path, an::SequenceFormat::ImageList, "frames");
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
    for (ViewHandle& plane : targets.planes)
    {
        dropView(context, plane);
    }
    targets.planes.clear();
}

/// Opens a process's window over its targets: a Merge CFA window with the four planes chosen.
[[nodiscard]] WindowHandle openWindow(const GeometryProcess& process, const Targets& targets, bool stopping = false)
{
    WindowHandle window = stopping ? process.openStopping(targets.view()) : process.open(targets.view());
    if (process.landing == Landing::TopLevel && targets.planes.size() == 4u)
    {
        for (std::size_t plane = 0u; plane < 4u; ++plane)
        {
            const std::string selector = "cfa" + std::to_string(plane) + "Selector";
            setComboText(*window, selector.c_str(), QString::fromStdString(targets.planes[plane]->getUniqueName()));
        }
    }
    return window;
}

/// What a run published: the target's image in place, the first new view's image, or the first frame of the new
/// sequence Extract Sequence opens. Nothing when the run published nothing there.
[[nodiscard]] std::optional<fixtures::FixtureSamples> resultOf(ProcessTestContext& context,
                                                               const GeometryProcess& process, const Targets& targets,
                                                               std::size_t viewsBefore, std::size_t output = 0u)
{
    if (process.landing == Landing::InPlace)
    {
        return documentSamples(targets.view()->document());
    }
    if (context.madeViews.size() <= viewsBefore + output)
    {
        return std::nullopt;
    }
    return documentSamples(context.madeViews[viewsBefore + output]->document());
}

/// Every widget value a window shows, as text, but the target selectors and the plane choices, which follow the views.
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
    for (const QRadioButton* button : window.findChildren<QRadioButton*>())
    {
        state += button->objectName().toStdString() + "=" + (button->isChecked() ? "1;" : "0;");
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
        const QString name = combo->objectName();
        if (name == QStringLiteral("targetImageSelector") || name.startsWith(QStringLiteral("cfa")))
        {
            continue;
        }
        state += name.toStdString() + "=" + combo->currentText().toStdString() + ";";
    }
    return state;
}

/// Sets a window's dialog to something other than what the mode the cases run sets, as a person changes it.
void changeDialog(const GeometryProcess& process, QWidget& window)
{
    const std::string name = process.name;
    if (name == "Crop")
    {
        setInt(window, "targetWidth", 6);
    }
    else if (name == "Resize")
    {
        setDouble(window, "targetWidthPercent", 200.0);
    }
    else if (name == "Rotate")
    {
        setDouble(window, "rotationAngle", 45.0);
    }
    else if (name == "IntegerResample" || name == "CfaIntegerResample")
    {
        clickRadio(window, "UpsampleRadioButton");
    }
    else if (name == "Debayer")
    {
        setComboText(window, "demosaicingMethod", QStringLiteral("VNG4"));
    }
    else if (name == "SplitCfa")
    {
        setComboText(window, "methodSelector", QStringLiteral("Extract Green"));
    }
    else if (name == "MergeCfa")
    {
        setComboText(window, "patternSelector", QStringLiteral("BGGR"));
    }
    else if (name == "ExtractSequence")
    {
        setInt(window, "startFrameValue", 2);
    }
}

class GeometryWindowTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const GeometryProcess& process() const
    {
        return geometryProcesses()[GetParam()];
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
        Targets targets = openTargets(context(), process(), std::move(asset));
        if (targets.view() == nullptr && targets.planes.empty())
        {
            return std::nullopt;
        }
        WindowHandle window = openWindow(process(), targets);
        process().configure(*window, "-");
        const std::size_t before = context().madeViews.size();
        if (!applyAndSettle(context(), *window))
        {
            return std::nullopt;
        }
        std::optional<fixtures::FixtureSamples> made = resultOf(context(), process(), targets, before);
        window.reset();
        dropTargets(context(), targets);
        return made;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// The cases of the processes that run over one view: every one but Merge CFA.
class GeometryTargetWindowTest : public GeometryWindowTest
{
};

/// The cases of the processes that change a still image in place.
class GeometryInPlaceWindowTest : public GeometryWindowTest
{
};

std::string processName(const ::testing::TestParamInfo<std::size_t>& info)
{
    return geometryProcesses()[info.param].name;
}

[[nodiscard]] std::vector<std::size_t> processesWhere(const std::function<bool(const GeometryProcess&)>& keep)
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0u; index < geometryProcesses().size(); ++index)
    {
        if (keep(geometryProcesses()[index]))
        {
            indices.push_back(index);
        }
    }
    return indices;
}

} // namespace

// launch: the registered action opens the process's window - over the active view, or as a top-level window - under the
// process's name, needing the view to itself where the descriptor says so.
TEST_P(GeometryWindowTest, TheRegisteredActionOpensTheWindow)
{
    const GeometryProcess& under = process();
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets targets = openTargets(context(), under);
    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(under.descriptorId);
    ASSERT_NE(descriptor, nullptr) << under.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, under.action);
    EXPECT_EQ(descriptor->process_name, under.processName);
    if (under.landing == Landing::TopLevel)
    {
        ASSERT_TRUE(descriptor->launch_top_level);
        descriptor->launch_top_level();
    }
    else
    {
        ASSERT_NE(targets.view(), nullptr);
        ASSERT_TRUE(descriptor->launch_single_image);
        descriptor->launch_single_image(targets.view());
    }
    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, under.className);
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    QWidget* window = context().openProcesses.front();
    EXPECT_TRUE(window->isVisible());
    if (auto* single = dynamic_cast<ImageProcessControl*>(window))
    {
        EXPECT_EQ(single->getProcessName(), under.processName);
        EXPECT_EQ(single->attachmentName(), under.processName);
        EXPECT_EQ(single->requiresExclusiveAccess(), under.exclusive);
    }
    window->close();
    ASSERT_TRUE(settleAll(context()));
    dropTargets(context(), targets);
}

// persistence: a window keeps nothing between openings - one opened after another was changed and closed shows the
// dialog's own values.
TEST_P(GeometryWindowTest, AWindowOpenedAgainShowsTheDialogsOwnValues)
{
    const GeometryProcess& under = process();
    Targets targets = openTargets(context(), under);
    std::string fresh;
    {
        WindowHandle window = openWindow(under, targets);
        fresh = dialogState(*window);
        changeDialog(under, *window);
        EXPECT_NE(dialogState(*window), fresh) << "the change reaches the dialog";
    }
    WindowHandle again = openWindow(under, targets);
    EXPECT_EQ(dialogState(*again), fresh);
    again.reset();
    dropTargets(context(), targets);
}

// cancel: a run whose stop is asked for publishes nothing, leaves the image, its history and the views as they were,
// says nothing, and leaves the window open to run again.
TEST_P(GeometryWindowTest, AStoppedRunPublishesNothing)
{
    const GeometryProcess& under = process();
    Targets targets = openTargets(context(), under);
    std::optional<fixtures::FixtureSamples> before;
    if (targets.target != nullptr)
    {
        before = documentSamples(targets.target->document());
    }
    WindowHandle window = openWindow(under, targets, true);
    under.configure(*window, "-");
    const std::size_t viewsBefore = context().madeViews.size();
    const int errorsBefore = context().errorCount();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(context().madeViews.size(), viewsBefore) << "nothing is opened";
    EXPECT_EQ(context().errorCount(), errorsBefore) << "a stop the user asked for is quiet";
    if (targets.target != nullptr)
    {
        const std::optional<fixtures::FixtureSamples> after = documentSamples(targets.target->document());
        ASSERT_TRUE(before.has_value() && after.has_value());
        EXPECT_EQ(after->digest(), before->digest());
        EXPECT_FALSE(targets.target->viewHistory()->hasUndo());
    }
    auto* box = window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->button(QDialogButtonBox::Close)->isEnabled()) << "the window can be closed or run again";
    window.reset();
    dropTargets(context(), targets);
}

// parameter-snapshot: a run reads the dialog as it was when Apply was pressed: a value changed while it runs does not
// reach it.
TEST_P(GeometryWindowTest, ARunReadsTheDialogAsItWasWhenApplyWasPressed)
{
    const GeometryProcess& under = process();
    const std::optional<fixtures::FixtureSamples> asPressed = runOnce();
    ASSERT_TRUE(asPressed.has_value());

    Targets targets = openTargets(context(), under);
    WindowHandle window = openWindow(under, targets);
    under.configure(*window, "-");
    const std::size_t before = context().madeViews.size();
    pressOk(*window);
    // The dialog changes before the run's worker has landed anything.
    changeDialog(under, *window);
    ASSERT_TRUE(settleWindow(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = resultOf(context(), under, targets, before);
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->digest(), asPressed->digest()) << "the run read the dialog after Apply was pressed";
    window.reset();
    dropTargets(context(), targets);
}

// apply-mask: a mask on the target is not applied - a geometry or filter-array change makes another picture, which the
// mask does not describe.
TEST_P(GeometryTargetWindowTest, AMaskOnTheTargetIsNotApplied)
{
    const GeometryProcess& under = process();
    const std::optional<fixtures::FixtureSamples> plain = runOnce();
    ASSERT_TRUE(plain.has_value());

    Targets targets = openTargets(context(), under);
    ASSERT_NE(targets.view(), nullptr);
    // A mask of the target's own image, so it covers it whatever its size.
    std::optional<an::consumer::ImageAsset> maskAsset =
        std::string_view(under.name) == "ExtractSequence"
            ? sceneAsset(session(), "scene-rgb-float32")
            : withMetadata(session(), *targets.view()->document().asset(), an::ImageMetadata{});
    ASSERT_TRUE(maskAsset.has_value());
    ViewHandle mask = assetView(std::move(*maskAsset), "mask");
    targets.view()->setCurrentMask(mask.get());
    targets.view()->setEnableMask(true);
    ASSERT_TRUE(targets.view()->isMasked());
    WindowHandle window = openWindow(under, targets);
    under.configure(*window, "-");
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = resultOf(context(), under, targets, before);
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->digest(), plain->digest()) << "the mask changed what the run made";
    window.reset();
    targets.view()->removeCurrentMask();
    dropTargets(context(), targets);
    dropView(context(), mask);
}

// representation: every representation an image can hold is run and kept - a crop, a turn, a resample or a split in
// the image's own samples, and a debayer's colour image and channels in the mosaic's.
TEST_P(GeometryWindowTest, EveryRepresentationIsKept)
{
    const GeometryProcess& under = process();
    if (std::string_view(under.name) == "ExtractSequence")
    {
        GTEST_SKIP() << "a sequence's frames are copied as recorded; GeometryProcessCase holds that";
    }
    const std::optional<an::consumer::ImageAsset> scene = sceneAsset(session(), under.scene);
    ASSERT_TRUE(scene.has_value());
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        std::optional<an::consumer::ImageAsset> converted = convertedTo(session(), *scene, type);
        ASSERT_TRUE(converted.has_value());
        Targets targets = openTargets(context(), under, std::move(*converted));
        WindowHandle window = openWindow(under, targets);
        under.configure(*window, "-");
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *window));
        const ImageViewControl* result =
            under.landing == Landing::InPlace
                ? targets.view()
                : (context().madeViews.size() > before ? context().madeViews[before] : nullptr);
        ASSERT_NE(result, nullptr) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<an::ImageDescriptor> described = result->imageDescriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->sampleType(), type);
        window.reset();
        dropTargets(context(), targets);
    }
}

// error: a run whose memory the engine refuses publishes nothing, says why under the process's title, and leaves the
// image and its history as they were.
TEST_P(GeometryInPlaceWindowTest, ARunTheEngineRefusesSaysWhyAndChangesNothing)
{
    const GeometryProcess& under = process();
    if (under.scene == std::string_view("mosaic-rggb-uint16"))
    {
        GTEST_SKIP() << "GeometryProcessCase holds the filter-array integer resample's refusal of an image that is "
                        "not a mosaic";
    }
    // A session whose engine holds the image and little more than a copy of it.
    constexpr std::uint32_t kSide = 256u;
    const std::uint64_t imageBytes = static_cast<std::uint64_t>(kSide) * kSide * 3u * sizeof(float);
    releaseContext();
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 4u;
    settings.limits.memoryBudgetBytes = imageBytes * 3u / 2u;
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    ProcessTestContext tight(std::move(*opened));
    const InstalledContext installed(tight);
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        tight.engineSession(), kSide, kSide, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(0.1 + 0.8 * static_cast<double>((x + y + c) % kSide) / kSide);
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset), "target");
    activate(tight, *target);
    ASSERT_TRUE(settleView(*target));
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = under.open(target.get());
    if (std::string_view(under.name) == "Crop")
    {
        setInt(*window, "targetPosX", 1);
    }
    else if (std::string_view(under.name) == "Resize")
    {
        setDouble(*window, "targetWidthPercent", 300.0);
    }
    else if (std::string_view(under.name) == "IntegerResample")
    {
        clickRadio(*window, "UpsampleRadioButton");
    }
    else
    {
        under.configure(*window, "-");
    }
    const std::size_t viewsBefore = tight.madeViews.size();
    ASSERT_TRUE(applyAndSettle(tight, *window));
    EXPECT_EQ(target->document().revision(), before);
    EXPECT_EQ(tight.madeViews.size(), viewsBefore);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([&tight]() { return tight.errorCount() >= 1; })) << "the window says why";
    EXPECT_TRUE(tight.messages.back().contains(QString::fromLatin1(under.errorTitle)))
        << tight.messages.back().toStdString();
    window.reset();
    dropView(tight, target);
}

// sequence: over a sequence every frame becomes what the still image of it becomes - in place, the view showing the
// new sequence as one history step; or in new sequences, one per output - published whole behind a progress window.
TEST_P(GeometryTargetWindowTest, EveryFrameOfASequenceIsWhatTheStillImageOfItBecomes)
{
    const GeometryProcess& under = process();
    if (std::string_view(under.name) == "ExtractSequence")
    {
        GTEST_SKIP() << "GeometryProcessCase holds the extracted range";
    }
    const bool mosaic = under.scene == std::string_view("mosaic-rggb-uint16");
    const std::vector<std::string_view> scenes =
        mosaic ? std::vector<std::string_view>{"mosaic-rggb-uint16", "mosaic-rggb-uint16"}
               : std::vector<std::string_view>{under.scene, under.scene == std::string_view("scene-rgb-float32")
                                                                ? std::string_view("scene-rgb-float32-second")
                                                                : under.scene};
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(session(), directory, scenes);
    ASSERT_FALSE(path.isEmpty());
    // What each frame, as the sequence holds it, becomes as a still image.
    std::vector<an::consumer::ImageAsset> recorded = frameAssets(session(), path);
    ASSERT_EQ(recorded.size(), scenes.size());
    std::vector<std::vector<fixtures::FixtureSamples>> stills;
    for (an::consumer::ImageAsset& frame : recorded)
    {
        Targets still = openTargets(context(), under, std::move(frame));
        WindowHandle window = openWindow(under, still);
        under.configure(*window, "-");
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *window));
        std::vector<fixtures::FixtureSamples> outputs;
        if (under.landing == Landing::InPlace)
        {
            std::optional<fixtures::FixtureSamples> made = documentSamples(still.view()->document());
            ASSERT_TRUE(made.has_value());
            outputs.push_back(std::move(*made));
        }
        else
        {
            for (std::size_t index = before; index < context().madeViews.size(); ++index)
            {
                std::optional<fixtures::FixtureSamples> made = documentSamples(context().madeViews[index]->document());
                ASSERT_TRUE(made.has_value());
                outputs.push_back(std::move(*made));
            }
        }
        stills.push_back(std::move(outputs));
        window.reset();
        dropTargets(context(), still);
    }

    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    Targets targets;
    targets.sequence = sequence;
    WindowHandle window = openWindow(under, targets);
    under.configure(*window, "-");
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    if (under.landing == Landing::InPlace)
    {
        EXPECT_EQ(context().madeViews.size(), before);
        const std::optional<an::ui::DocumentSequence> shown = sequence->document().sequence();
        ASSERT_TRUE(shown.has_value());
        EXPECT_NE(shown->sourcePath, path) << "the view shows the new sequence";
        const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *sequence);
        ASSERT_EQ(frames.size(), scenes.size());
        for (std::size_t frame = 0u; frame < frames.size(); ++frame)
        {
            EXPECT_EQ(frames[frame].digest(), stills[frame].front().digest()) << "frame " << frame;
        }
        ASSERT_NE(sequence->viewHistory(), nullptr);
        EXPECT_EQ(sequence->viewHistory()->previousStepName(), under.processName);
    }
    else
    {
        ASSERT_EQ(context().madeViews.size(), before + stills.front().size())
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        for (std::size_t output = 0u; output < stills.front().size(); ++output)
        {
            ImageViewControl* made = context().madeViews[before + output];
            ASSERT_TRUE(made->isSequence());
            const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *made);
            ASSERT_EQ(frames.size(), scenes.size());
            for (std::size_t frame = 0u; frame < frames.size(); ++frame)
            {
                EXPECT_EQ(frames[frame].digest(), stills[frame][output].digest())
                    << "output " << output << ", frame " << frame;
            }
        }
    }
    window.reset();
}

INSTANTIATE_TEST_SUITE_P(Processes, GeometryWindowTest, ::testing::Range<std::size_t>(0u, geometryProcesses().size()),
                         processName);
INSTANTIATE_TEST_SUITE_P(Targets, GeometryTargetWindowTest,
                         ::testing::ValuesIn(processesWhere([](const GeometryProcess& process) {
                             return process.landing != Landing::TopLevel;
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(InPlace, GeometryInPlaceWindowTest,
                         ::testing::ValuesIn(processesWhere([](const GeometryProcess& process) {
                             return process.landing == Landing::InPlace;
                         })),
                         processName);
