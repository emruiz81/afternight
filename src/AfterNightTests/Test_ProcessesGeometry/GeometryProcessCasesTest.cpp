// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one geometry or filter-array window does that the others do not: the history step each records and the views
/// each opens, what each result keeps of its source's metadata, the interpolation Auto names, a mosaic sequence
/// demosaiced by the one setting, a mosaic read as grey only when the person says so, the filter under every sample of
/// a cropped mosaic, the methods and patterns Debayer offers, the planes Split CFA names, Merge CFA's choices and
/// sequences, Extract Sequence's range, Integer Resample following a target that changes size, and a resized image
/// that can be rotated.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewFiles.h"
#include "app/PlateSolveTypes.h"
#include "processes/CFAIntegerResampleProcess.h"
#include "processes/CfaProcessSupport.h"
#include "processes/CropProcess.h"
#include "processes/DebayerProcess.h"
#include "processes/ExtractSequenceProcess.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/IntegerResampleProcess.h"
#include "processes/MergeCFAProcess.h"
#include "processes/ResizeProcess.h"
#include "processes/RotateProcess.h"
#include "processes/SplitCFAProcess.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::geometry;

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

/// A scene under the described metadata, as a view made active.
[[nodiscard]] ViewHandle describedView(ProcessTestContext& context, std::string_view scene)
{
    const std::optional<an::consumer::ImageAsset> plain = sceneAsset(context.engineSession(), scene);
    if (!plain.has_value())
    {
        return ViewHandle();
    }
    std::optional<an::consumer::ImageAsset> described =
        withMetadata(context.engineSession(), *plain, describedMetadata());
    if (!described.has_value())
    {
        return ViewHandle();
    }
    ViewHandle view = assetView(std::move(*described), "target");
    activate(context, *view);
    return view;
}

/// Runs an operation over an asset under an admitted operation and returns its samples.
[[nodiscard]] std::optional<fixtures::FixtureSamples>
run(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& asset,
    const std::function<an::Result<an::consumer::ImageAsset>(const an::ui::ExecutionScope&,
                                                             const an::ui::ProcessSource&)>& operation)
{
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(scope.hasValue() && lease.hasValue());
    if (!scope.hasValue() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> made = operation(*scope, an::ui::ProcessSource::of(*lease));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    lease->release();
    scope->finish(an::Status::success());
    return made.hasValue() ? assetSamples(*made) : std::nullopt;
}

class GeometryProcessCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(sessionWidth());
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    /// The session's width.
    [[nodiscard]] virtual std::uint32_t sessionWidth() const
    {
        return 4u;
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

    [[nodiscard]] QTemporaryDir& directory()
    {
        return m_directory;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_directory;
};

/// A process's window over a session one operation wide, where work that holds the only thread while it waits for an
/// image's writer stops everything.
class GeometryProcessCaseAtWidthOne : public GeometryProcessCase
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

/// Opens a single-image window kept by the case.
template <typename WindowT> [[nodiscard]] WindowHandle openKept(ImageViewControl& target)
{
    WindowHandle window(new WindowT(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    return window;
}

} // namespace

// history: an in-place Apply is one history step under the process's name, described by the values it ran with, and
// undoing it brings the image back.
TEST_F(GeometryProcessCase, AnInPlaceApplyIsOneHistoryStepThatUndoes)
{
    for (std::size_t index : processesLanding(Landing::InPlace))
    {
        const GeometryProcess& under = geometryProcesses()[index];
        SCOPED_TRACE(under.name);
        const std::optional<an::consumer::ImageAsset> asset = sceneAsset(session(), under.scene);
        ASSERT_TRUE(asset.has_value());
        std::optional<an::consumer::ImageAsset> copy = withMetadata(session(), *asset, an::ImageMetadata{});
        ASSERT_TRUE(copy.has_value());
        ViewHandle target = assetView(std::move(*copy), "target");
        activate(context(), *target);
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        WindowHandle window = under.open(target.get());
        under.configure(*window, "-");
        ASSERT_TRUE(applyAndSettle(context(), *window));
        an::ui::IViewHistory* history = target->viewHistory();
        ASSERT_NE(history, nullptr);
        ASSERT_TRUE(history->hasUndo());
        EXPECT_EQ(history->previousStepName(), under.processName);
        window.reset();
        ASSERT_TRUE(history->undo());
        ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
        ASSERT_TRUE(settleView(*target));
        const std::optional<fixtures::FixtureSamples> undone = documentSamples(target->document());
        ASSERT_TRUE(source.has_value() && undone.has_value());
        EXPECT_EQ(undone->digest(), source->digest());
        dropView(context(), target);
    }
}

// history: each in-place window describes its step with the values it ran with - an interpolation named, never Auto.
TEST_F(GeometryProcessCase, EachStepIsDescribedWithTheValuesItRanWith)
{
    EXPECT_EQ(CropProcess::stepDescription({.x = 2u, .y = 1u, .width = 8u, .height = 5u}),
              "Cropped area : X: 2 Y: 1 W:8 H:5");
    ResizeProcess::Settings resize{.originalWidth = 12u, .originalHeight = 8u, .width = 24u, .height = 16u};
    resize.interpolation = an::ui::processes::resolveResizeInterpolation(QStringLiteral("Auto"), true);
    EXPECT_EQ(ResizeProcess::stepDescription(resize),
              "Previous size: 12x8 - New size: 24x16 - Interpolation: Lanczos4");
    RotateProcess::Settings rotate{.degrees = 30.0};
    rotate.interpolation = an::ui::processes::resolveRotationInterpolation(QStringLiteral("Auto"));
    EXPECT_EQ(RotateProcess::stepDescription(rotate), "Rotated 30 degrees - Interpolation: Lanczos4");
    rotate.crop = an::CropRequest{.x = 1u, .y = 2u, .width = 3u, .height = 4u};
    EXPECT_EQ(RotateProcess::stepDescription(rotate),
              "Rotated 30 degrees - Cropped area : X: 1 Y: 2 W:3 H:4 - Interpolation: Lanczos4");
    EXPECT_EQ(
        IntegerResampleProcess::stepDescription(
            {.factor = 2u, .enlarge = false, .reductionLabel = QStringLiteral("Median"), .width = 6u, .height = 4u}),
        "Binning factor: 2X - New size: 6x4 - Binning method: Median");
    EXPECT_EQ(IntegerResampleProcess::stepDescription({.factor = 3u, .enlarge = true, .width = 36u, .height = 24u}),
              "Resample factor: 3X - New size: 36x24");
}

// history and outputs: a new-view window opens its views under names that say what each holds, each beginning its
// history with a step that says what it was made from, beside the target.
TEST_F(GeometryProcessCase, NewViewsAreNamedAndBeginTheirHistoryBesideTheTarget)
{
    struct Case
    {
        const char* process;
        const char* mode;
        std::vector<std::pair<std::string, std::string>> views;
    };
    const std::vector<Case> cases{
        {"Debayer", "-", {{"target - RGB - RCD", "Debayer: RGB image created from RAW image 'target'"}}},
        {"Debayer",
         "both",
         {{"target - RGB - RCD", "Debayer: RGB image created from RAW image 'target'"},
          {"target - RED - RCD", "Debayer: Red channel extracted from RAW image 'target'"},
          {"target - GREEN - RCD", "Debayer: Green channel extracted from RAW image 'target'"},
          {"target - BLUE - RCD", "Debayer: Blue channel extracted from RAW image 'target'"}}},
        {"Debayer",
         "separate",
         {{"target - RED - RCD", "Debayer: Red channel extracted from RAW image 'target'"},
          {"target - GREEN - RCD", "Debayer: Green channel extracted from RAW image 'target'"},
          {"target - BLUE - RCD", "Debayer: Blue channel extracted from RAW image 'target'"}}},
        {"SplitCfa",
         "-",
         {{"target - CFA0 - Red", "Split CFA: CFA0 extracted from RAW image 'target'"},
          {"target - CFA1 - Green1", "Split CFA: CFA1 extracted from RAW image 'target'"},
          {"target - CFA2 - Green2", "Split CFA: CFA2 extracted from RAW image 'target'"},
          {"target - CFA3 - Blue", "Split CFA: CFA3 extracted from RAW image 'target'"}}},
        {"SplitCfa", "ha", {{"target - Ha", "Split CFA: Ha extracted from RAW image 'target'"}}},
        {"SplitCfa", "green", {{"target - Green", "Split CFA: Green extracted from RAW image 'target'"}}},
        {"SplitCfa",
         "ha-oiii-no-resampling-ha",
         {{"target - Ha", "Split CFA: Ha extracted from RAW image 'target'"},
          {"target - Oiii", "Split CFA: OIII extracted from RAW image 'target'"}}}};
    for (const Case& item : cases)
    {
        SCOPED_TRACE(std::string(item.process) + " " + item.mode);
        const GeometryProcess* under = nullptr;
        for (const GeometryProcess& process : geometryProcesses())
        {
            if (std::string_view(process.name) == item.process)
            {
                under = &process;
            }
        }
        ASSERT_NE(under, nullptr);
        std::optional<an::consumer::ImageAsset> asset = sceneAsset(session(), under->scene);
        ASSERT_TRUE(asset.has_value());
        ViewHandle target = assetView(std::move(*asset), "target");
        activate(context(), *target);
        WindowHandle window = under->open(target.get());
        if (std::string_view(item.mode) == "both" || std::string_view(item.mode) == "separate")
        {
            setComboText(*window, "outputMode",
                         std::string_view(item.mode) == "both" ? QStringLiteral("RGB Color + Separate RGB Channels")
                                                               : QStringLiteral("Separate RGB Channels"));
        }
        else
        {
            under->configure(*window, item.mode);
        }
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *window));
        ASSERT_EQ(context().madeViews.size(), before + item.views.size())
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        for (std::size_t index = 0u; index < item.views.size(); ++index)
        {
            ImageViewControl* made = context().madeViews[before + index];
            EXPECT_EQ(made->document().names().displayName.toStdString(), item.views[index].first);
            EXPECT_EQ(madeHistoryOf(context(), made), item.views[index].second);
            EXPECT_EQ(madeNeighbourOf(context(), made), target.get()) << "the view sits beside the target";
        }
        window.reset();
        dropView(context(), target);
    }
}

// metadata: a geometry change keeps the acquisition record and the meaning, drops the codec's cards and withdraws an
// output decision, and its lineage names the engine's geometry operation and the process - one policy for the crop, the
// resize, the rotation and both resamples, as for the view's own turns. A summary of a solution no card states goes
// with them; a solution the cards state is the next case's.
TEST_F(GeometryProcessCase, AGeometryChangeKeepsTheRecordAndDropsTheSolutionAndCards)
{
    for (std::size_t index : processesLanding(Landing::InPlace))
    {
        const GeometryProcess& under = geometryProcesses()[index];
        SCOPED_TRACE(under.name);
        ViewHandle target = describedView(context(), under.scene);
        ASSERT_NE(target, nullptr);
        WindowHandle window = under.open(target.get());
        under.configure(*window, "-");
        ASSERT_TRUE(applyAndSettle(context(), *window));
        const std::optional<an::ImageMetadata> result = documentMetadata(target->document());
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->acquisition(), describedMetadata().acquisition());
        EXPECT_FALSE(result->astrometry().present()) << "the solution no longer describes the pixels";
        EXPECT_TRUE(result->codec().cards().empty()) << "the codec's cards are dropped";
        EXPECT_FALSE(result->outputIntent().has_value());
        EXPECT_EQ(result->semantics().transferFunction(), an::TransferFunction::Linear);
        ASSERT_FALSE(result->provenance().entries().empty());
        const an::ProvenanceEntry& entry = result->provenance().entries().back();
        EXPECT_EQ(entry.operation(), "an::transformImageGeometry");
        EXPECT_EQ(entry.detail(), std::string(under.processName));
        window.reset();
        dropView(context(), target);
    }
}

// run: Debayer, Split CFA and CFA Integer Resample hold their target while a run is on its way - the run's finish reads
// the view the selector names, and a selector left live would let a person retarget it mid-run - and give it back once
// a run the engine refused has landed, the window staying open.
TEST_F(GeometryProcessCase, AFilterArrayWindowHoldsItsTargetWhileItRuns)
{
    ViewHandle target = describedView(context(), "mosaic-rggb-uint16");
    ASSERT_NE(target, nullptr);
    const auto selectorOf = [](QWidget& window) {
        return window.findChild<QComboBox*>(QStringLiteral("targetImageSelector"));
    };
    const auto holds = [this, &selectorOf](QWidget& window, const char* name) {
        QComboBox* selector = selectorOf(window);
        ASSERT_NE(selector, nullptr) << name;
        EXPECT_TRUE(selector->isEnabled()) << name;
        pressOk(window);
        EXPECT_FALSE(selector->isEnabled()) << name << ": the target can be changed while the run is on its way";
        ASSERT_TRUE(settleWindow(context(), window)) << name;
    };
    {
        WindowHandle window = openKept<DebayerProcess>(*target);
        holds(*window, "Debayer");
    }
    {
        WindowHandle window = openKept<SplitCFAProcess>(*target);
        holds(*window, "Split CFA");
    }
    {
        WindowHandle window = openKept<CFAIntegerResampleProcess>(*target);
        holds(*window, "CFA Integer Resample");
    }

    // A grey image states no filter array, so each of the three refuses it by name and the window stays open.
    ViewHandle grey = sceneView(session(), "scene-mono-float32", "grey");
    ASSERT_NE(grey, nullptr);
    activate(context(), *grey);
    const auto givesBack = [this, &selectorOf](QWidget& window, const char* name) {
        QComboBox* selector = selectorOf(window);
        ASSERT_NE(selector, nullptr) << name;
        const int errors = context().errorCount();
        pressOk(window);
        // A refusal the engine answers at once may land before the window held its selector, so only the end is held.
        ASSERT_TRUE(settleWindow(context(), window)) << name;
        ASSERT_TRUE(pumpUntil([this, errors]() { return context().errorCount() > errors; })) << name;
        EXPECT_TRUE(selector->isEnabled()) << name << ": the target is given back once the refused run has landed";
    };
    context().answerYes = true;
    {
        WindowHandle window = openKept<DebayerProcess>(*grey);
        givesBack(*window, "Debayer");
    }
    context().answerYes = false;
    {
        WindowHandle window = openKept<SplitCFAProcess>(*grey);
        givesBack(*window, "Split CFA");
    }
    {
        WindowHandle window = openKept<CFAIntegerResampleProcess>(*grey);
        givesBack(*window, "CFA Integer Resample");
    }
    dropView(context(), grey);
}

// metadata: Split CFA's planes keep the mosaic's record and cards and drop its solution and filter array; a measured
// channel names its extraction; Debayer's colour image keeps the record and states no filter array; Merge CFA's mosaic
// states the arrangement it was joined under and keeps the planes' cards.
TEST_F(GeometryProcessCase, AFilterArrayResultKeepsWhatItsOperationKeeps)
{
    ViewHandle target = describedView(context(), "mosaic-rggb-uint16");
    ASSERT_NE(target, nullptr);
    {
        WindowHandle split = openKept<SplitCFAProcess>(*target);
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *split));
        ASSERT_EQ(context().madeViews.size(), before + 4u);
        const std::optional<an::ImageMetadata> plane = documentMetadata(context().madeViews[before]->document());
        ASSERT_TRUE(plane.has_value());
        EXPECT_EQ(plane->acquisition(), describedMetadata().acquisition());
        EXPECT_FALSE(plane->astrometry().present());
        EXPECT_TRUE(hasCard(*plane, "OBJECT")) << "a plane keeps the mosaic's cards";
        EXPECT_FALSE(context().madeViews[before]->imageDescriptor()->isMosaiced());
        EXPECT_EQ(plane->provenance().entries().back().operation(), "an::splitCfaBlockPlanes");
    }
    {
        WindowHandle split = openKept<SplitCFAProcess>(*target);
        setComboText(*split, "methodSelector", QStringLiteral("Extract Ha"));
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *split));
        ASSERT_EQ(context().madeViews.size(), before + 1u);
        const std::optional<an::ImageMetadata> ha = documentMetadata(context().madeViews[before]->document());
        ASSERT_TRUE(ha.has_value());
        EXPECT_EQ(ha->acquisition(), describedMetadata().acquisition());
        EXPECT_FALSE(ha->astrometry().present());
        EXPECT_TRUE(ha->semantics().processingState().demosaiced) << "a measured channel is a resolved measurement";
    }
    {
        WindowHandle debayer = openKept<DebayerProcess>(*target);
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *debayer));
        ASSERT_EQ(context().madeViews.size(), before + 1u);
        const std::optional<an::ImageDescriptor> colour = context().madeViews[before]->imageDescriptor();
        ASSERT_TRUE(colour.has_value());
        EXPECT_FALSE(colour->isMosaiced());
        EXPECT_EQ(colour->extent().channelCount(), 3u);
        const std::optional<an::ImageMetadata> metadata = documentMetadata(context().madeViews[before]->document());
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(metadata->acquisition(), describedMetadata().acquisition());
        EXPECT_TRUE(metadata->semantics().processingState().demosaiced);
    }
    dropView(context(), target);
}

// Auto says what it chooses - Lanczos-4 to enlarge and the pixel area relation to reduce for a resize, Lanczos-4
// for a rotation - and the step names the interpolation that ran.
TEST_F(GeometryProcessCase, TheAutoChoiceSaysWhatItChoosesAndTheRunNamesIt)
{
    ViewHandle target = sceneView(session(), "scene-rgb-float32");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    {
        WindowHandle window = openKept<ResizeProcess>(*target);
        auto* interpolation = widgetOf<QComboBox>(*window, "interpolation");
        ASSERT_NE(interpolation, nullptr);
        EXPECT_EQ(interpolation->itemText(0), an::ui::processes::resizeAutoInterpolationText());
        EXPECT_EQ(an::ui::processes::selectedInterpolationName(interpolation), QStringLiteral("Auto"));
        setDouble(*window, "targetWidthPercent", 200.0);
        const ResizeProcess::Settings enlarge = static_cast<ResizeProcess&>(*window).settings();
        EXPECT_EQ(enlarge.interpolation.interpolation, an::KernelInterpolation::Lanczos4);
        EXPECT_EQ(enlarge.interpolation.label, QStringLiteral("Lanczos4"));
        EXPECT_TRUE(enlarge.interpolation.automatic);
        setDouble(*window, "targetWidthPercent", 50.0);
        const ResizeProcess::Settings reduce = static_cast<ResizeProcess&>(*window).settings();
        EXPECT_EQ(reduce.interpolation.interpolation, an::KernelInterpolation::Area);
        EXPECT_EQ(reduce.interpolation.label, QStringLiteral("Pixel Area Relation"));
        an::ui::processes::selectInterpolationName(interpolation, QStringLiteral("Bicubic"));
        EXPECT_EQ(static_cast<ResizeProcess&>(*window).settings().interpolation.interpolation,
                  an::KernelInterpolation::Cubic);
    }
    {
        WindowHandle window = openKept<RotateProcess>(*target);
        auto* interpolation = widgetOf<QComboBox>(*window, "interpolation");
        ASSERT_NE(interpolation, nullptr);
        EXPECT_EQ(interpolation->itemText(0), an::ui::processes::rotationAutoInterpolationText());
        setDouble(*window, "rotationAngle", 10.0);
        const RotateProcess::Settings asked = static_cast<RotateProcess&>(*window).settings();
        EXPECT_EQ(asked.interpolation.interpolation, an::KernelInterpolation::Lanczos4);
        EXPECT_EQ(RotateProcess::stepDescription(asked), "Rotated 10 degrees - Interpolation: Lanczos4");
    }
    dropView(context(), target);
}

// A geometry run over a mosaic sequence its view shows demosaiced reads each frame demosaiced by the method the
// one RAW demosaic setting names, which the view stated when it showed the sequence - never a method of its own.
TEST_F(GeometryProcessCase, AMosaicSequenceIsDemosaicedByTheMethodTheSettingNames)
{
    const QString path = writeSceneSequence(session(), directory(), {"mosaic-rggb-uint16", "mosaic-rggb-uint16"});
    ASSERT_FALSE(path.isEmpty());
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    const an::CropRequest crop{.x = 2u, .y = 1u, .width = 8u, .height = 5u};
    std::optional<fixtures::FixtureSamples> byVng4;
    for (const auto& [setting, method] : {std::pair<const char*, an::DemosaicMethod>{"vng4", an::DemosaicMethod::Vng4},
                                          std::pair<const char*, an::DemosaicMethod>{"rcd", an::DemosaicMethod::Rcd}})
    {
        SCOPED_TRACE(setting);
        (void)context().upsert(QStringLiteral("raw/demosaicAlgorithm"), QString::fromLatin1(setting));
        ImageViewControl* view = openDemosaicedSequenceView(context(), path, an::SequenceFormat::ImageList,
                                                            std::string("frames-") + setting);
        ASSERT_NE(view, nullptr);
        ASSERT_TRUE(view->isSequenceAutodebayerEnabled());
        WindowHandle window = openKept<CropProcess>(*view);
        setInt(*window, "targetPosX", static_cast<int>(crop.x));
        setInt(*window, "targetPosY", static_cast<int>(crop.y));
        setInt(*window, "targetWidth", static_cast<int>(crop.width));
        setInt(*window, "targetHeight", static_cast<int>(crop.height));
        ASSERT_TRUE(applyAndSettle(context(), *window));
        const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *view);
        ASSERT_EQ(frames.size(), 2u);
        const std::optional<fixtures::FixtureSamples> expected =
            run(session(), *mosaic,
                [method, crop](const an::ui::ExecutionScope& scope,
                               const an::ui::ProcessSource& source) -> an::Result<an::consumer::ImageAsset> {
                    an::Result<an::consumer::AnyImage> colour =
                        an::ui::demosaicViewFrame(scope.engine(), scope.execution(), source.image(), method);
                    if (!colour.hasValue())
                    {
                        return colour.status();
                    }
                    an::Result<an::consumer::ImageAsset> asset =
                        an::consumer::ImageAsset::adopt(scope.session(), std::move(*colour));
                    if (!asset.hasValue())
                    {
                        return asset.status();
                    }
                    an::Result<an::consumer::AssetReadLease> lease = asset->read();
                    const std::array<an::ui::processes::GeometryStep, 1u> steps{crop};
                    return an::ui::processes::transformImage(scope, an::ui::ProcessSource::of(*lease), steps, "Crop");
                });
        ASSERT_TRUE(expected.has_value());
        EXPECT_EQ(frames[0].channelCount(), 3u) << "the frames were read demosaiced";
        EXPECT_EQ(frames[0].digest(), expected->digest());
        if (method == an::DemosaicMethod::Vng4)
        {
            byVng4 = frames[0];
        }
        else
        {
            ASSERT_TRUE(byVng4.has_value());
            EXPECT_NE(frames[0].digest(), byVng4->digest()) << "the setting decides the method";
        }
        window.reset();
    }
}

// A resize, a rotation or an integer resample of a mosaic asks first, and reads the mosaic as one grey plane - its
// filter array forgotten - only when the person says so.
TEST_F(GeometryProcessCase, AMosaicIsReadAsGreyOnlyWhenThePersonSaysSo)
{
    for (const char* name : {"Resize", "Rotate", "IntegerResample"})
    {
        SCOPED_TRACE(name);
        const GeometryProcess* under = nullptr;
        for (const GeometryProcess& process : geometryProcesses())
        {
            if (std::string_view(process.name) == name)
            {
                under = &process;
            }
        }
        ASSERT_NE(under, nullptr);
        ViewHandle target = sceneView(session(), "mosaic-rggb-uint16");
        ASSERT_NE(target, nullptr);
        activate(context(), *target);
        const an::ui::DocumentRevision revision = target->document().revision();
        WindowHandle window = under->open(target.get());
        under->configure(*window, "-");
        context().answerYes = false;
        const int questionsBefore = static_cast<int>(context().messages.size());
        pressOk(*window);
        ASSERT_TRUE(settleWindow(context(), *window));
        ASSERT_GT(context().messages.size(), questionsBefore);
        EXPECT_TRUE(context().messages.back().startsWith(QStringLiteral("question:")));
        EXPECT_EQ(target->document().revision(), revision) << "No leaves the mosaic as it was";
        context().answerYes = true;
        ASSERT_TRUE(applyAndSettle(context(), *window));
        const std::optional<an::ImageDescriptor> result = target->imageDescriptor();
        ASSERT_TRUE(result.has_value());
        EXPECT_FALSE(result->isMosaiced()) << "the grey result states no filter array";
        context().answerYes = false;
        window.reset();
        dropView(context(), target);
    }
}

// A crop of a mosaic keeps the filter under every sample: the crop's phase is the origin's, so an odd origin turns RGGB
// into GRBG or GBRG rather than lying about it.
TEST_F(GeometryProcessCase, ACropKeepsTheFilterUnderEverySample)
{
    for (const auto& [x, y, colour] : {std::tuple<int, int, an::CfaColor>{1, 0, an::CfaColor::Green},
                                       {0, 1, an::CfaColor::Green},
                                       std::tuple<int, int, an::CfaColor>{1, 1, an::CfaColor::Blue},
                                       {2, 2, an::CfaColor::Red}})
    {
        SCOPED_TRACE(std::to_string(x) + "," + std::to_string(y));
        ViewHandle target = sceneView(session(), "mosaic-rggb-uint16");
        ASSERT_NE(target, nullptr);
        activate(context(), *target);
        WindowHandle window = openKept<CropProcess>(*target);
        setInt(*window, "targetPosX", x);
        setInt(*window, "targetPosY", y);
        setInt(*window, "targetWidth", 9);
        setInt(*window, "targetHeight", 7);
        const int messagesBefore = static_cast<int>(context().messages.size());
        ASSERT_TRUE(applyAndSettle(context(), *window));
        EXPECT_EQ(static_cast<int>(context().messages.size()), messagesBefore) << "nothing is asked";
        const std::optional<an::ImageDescriptor> result = target->imageDescriptor();
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->isMosaiced());
        EXPECT_EQ(result->cfa().colorAt(0u, 0u), colour);
        EXPECT_EQ(result->extent().width(), 9u);
        window.reset();
        dropView(context(), target);
    }
}

// A filter-array integer resample keeps the arrangement and phase; a mosaic of an odd width or height loses its last
// column or row first; and an image that is not a mosaic is refused by name.
TEST_F(GeometryProcessCase, AFilterArrayResampleKeepsTheArrangementAndRefusesAGreyImage)
{
    ViewHandle target = sceneView(session(), "mosaic-rggb-uint16");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    {
        WindowHandle crop = openKept<CropProcess>(*target);
        setInt(*crop, "targetWidth", 23);
        setInt(*crop, "targetHeight", 15);
        ASSERT_TRUE(applyAndSettle(context(), *crop));
    }
    const std::optional<fixtures::FixtureSamples> odd = documentSamples(target->document());
    ASSERT_TRUE(odd.has_value());
    WindowHandle window = openKept<CFAIntegerResampleProcess>(*target);
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<an::ImageDescriptor> result = target->imageDescriptor();
    ASSERT_TRUE(result.has_value());
    // 22 by 14 after the odd edges go, planes of 11 by 7 reduced to 5 by 3, a mosaic of 10 by 6.
    EXPECT_EQ(result->extent().width(), 10u);
    EXPECT_EQ(result->extent().height(), 6u);
    ASSERT_TRUE(result->isMosaiced());
    EXPECT_EQ(result->cfa().pattern(), an::CfaPattern::Rggb);
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value());
    // The red site of the first block is the average of the red sites of the first two-by-two blocks.
    const double red =
        (odd->value(0u, 0u, 0u) + odd->value(2u, 0u, 0u) + odd->value(0u, 2u, 0u) + odd->value(2u, 2u, 0u)) / 4.0;
    EXPECT_NEAR(made->value(0u, 0u, 0u), red, 0.5 + 1e-9);
    window.reset();
    dropView(context(), target);

    ViewHandle grey = sceneView(session(), "scene-mono-float32", "grey");
    ASSERT_NE(grey, nullptr);
    activate(context(), *grey);
    const an::ui::DocumentRevision revision = grey->document().revision();
    WindowHandle refused = openKept<CFAIntegerResampleProcess>(*grey);
    ASSERT_TRUE(applyAndSettle(context(), *refused));
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() >= 1; }));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running integer resample"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("states no colour filter array")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(grey->document().revision(), revision);
    refused.reset();
    dropView(context(), grey);
}

// Debayer offers the six methods the engine implements, and reads the filter array the image states for "From
// metadata": a grey image that states none is refused by name once the person says to run anyway, a stated arrangement
// demosaics it, and a colour image is not debayered.
TEST_F(GeometryProcessCase, DebayerOffersTheEnginesMethodsAndReadsThePatternFromTheImage)
{
    ViewHandle grey = sceneView(session(), "scene-mono-float32", "grey");
    ASSERT_NE(grey, nullptr);
    activate(context(), *grey);
    WindowHandle window = openKept<DebayerProcess>(*grey);
    auto* methods = widgetOf<QComboBox>(*window, "demosaicingMethod");
    ASSERT_NE(methods, nullptr);
    QStringList offered;
    for (int index = 0; index < methods->count(); ++index)
    {
        offered.push_back(methods->itemText(index));
    }
    EXPECT_EQ(offered, (QStringList{"Bilinear", "VNG4", "AHD", "AMaZE", "LMMSE", "RCD"}));
    EXPECT_EQ(methods->currentText(), QStringLiteral("RCD"));
    auto* patterns = widgetOf<QComboBox>(*window, "bayerPattern");
    ASSERT_NE(patterns, nullptr);
    EXPECT_EQ(patterns->itemText(0), QStringLiteral("From metadata"));
    EXPECT_EQ(patterns->itemData(0).toString(), QStringLiteral("Auto"));
    EXPECT_TRUE(patterns->toolTip().contains(QStringLiteral("refuses")));

    context().answerYes = true;
    std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() >= 1; }));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running Debayer"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("states no colour filter array")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), before);

    setComboText(*window, "bayerPattern", QStringLiteral("RGGB"));
    before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(context().madeViews.back()->imageDescriptor()->extent().channelCount(), 3u);
    window.reset();
    dropView(context(), grey);

    ViewHandle colour = sceneView(session(), "scene-rgb-float32", "colour");
    ASSERT_NE(colour, nullptr);
    activate(context(), *colour);
    WindowHandle second = openKept<DebayerProcess>(*colour);
    const int errors = context().errorCount();
    before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *second));
    ASSERT_TRUE(pumpUntil([this, errors]() { return context().errorCount() > errors; }));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("more than one channel")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), before);
    context().answerYes = false;
    second.reset();
    dropView(context(), colour);
}

// Debayer asked the engine for its Bayer method over an X-Trans image, which the engine refuses, and placed a named
// two-by-two pattern over one at its origin and demosaiced the six-by-six samples under it. From metadata demosaics it
// by the method's tabulated counterpart into one colour image, and a named pattern is refused by name, changing
// nothing.
TEST_F(GeometryProcessCase, DebayerDemosaicsAnXTransImageFromItsMetadataAndRefusesANamedBayerPatternOverIt)
{
    std::optional<an::consumer::ImageAsset> mosaic = an::tests::viewer::xtransMosaicAsset(session());
    ASSERT_TRUE(mosaic.has_value());
    ViewHandle target = assetView(std::move(*mosaic), "target");
    activate(context(), *target);
    struct Case
    {
        const char* method;
        const char* ran;
    };
    for (const Case& item : {Case{"RCD", "MarkesteijnThreePass"}, Case{"Bilinear", "XTransFast"}})
    {
        SCOPED_TRACE(item.method);
        WindowHandle window = openKept<DebayerProcess>(*target);
        setComboText(*window, "demosaicingMethod", QString::fromLatin1(item.method));
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *window));
        ASSERT_EQ(context().madeViews.size(), before + 1u)
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::shared_ptr<const an::consumer::ImageAsset> made = context().madeViews.back()->document().asset();
        ASSERT_NE(made, nullptr);
        ASSERT_TRUE(made->descriptor().has_value());
        EXPECT_EQ(made->descriptor()->extent().channelCount(), 3u);
        EXPECT_FALSE(made->descriptor()->isMosaiced());
        EXPECT_TRUE(an::tests::viewer::lineageMentions(*made, item.ran));
        window.reset();
        activate(context(), *target);
    }

    WindowHandle named = openKept<DebayerProcess>(*target);
    setComboText(*named, "bayerPattern", QStringLiteral("RGGB"));
    const an::ui::DocumentRevision revision = target->document().revision();
    const int errors = context().errorCount();
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *named));
    ASSERT_TRUE(pumpUntil([this, errors]() { return context().errorCount() > errors; }));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running Debayer"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("X-Trans")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), before);
    EXPECT_EQ(target->document().revision(), revision);
    named.reset();
    dropView(context(), target);
}

namespace
{

/// Where the meaningless sample of `mosaicWithAMeaninglessSample` lies.
constexpr std::uint32_t kMeaninglessX = 12u;
constexpr std::uint32_t kMeaninglessY = 8u;

/// The catalogue's Bayer mosaic with the sample at (`kMeaninglessX`, `kMeaninglessY`) meaning nothing, as an asset
/// stating the rule that says so: a NaN among normalized floats under the finite-only rule, a zero under the
/// zero-is-no-data rule, or a sample its mask clears.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
mosaicWithAMeaninglessSample(const an::consumer::EngineSession& session, an::ValidityRule rule)
{
    const fixtures::FixtureImage* catalogued = fixtures::fixtureNamed("mosaic-rggb-uint16");
    EXPECT_NE(catalogued, nullptr);
    if (catalogued == nullptr)
    {
        return std::nullopt;
    }
    fixtures::FixtureImage image = *catalogued;
    image.name = "mosaic-rggb-meaningless-sample";
    if (rule == an::ValidityRule::FiniteOnly)
    {
        image.sampleType = fixtures::FixtureSampleType::Float32;
        image.domain = fixtures::FixtureDomain::NormalizedLinear;
        image.levels = {{.base = 0.05, .perColumn = 0.007, .perRow = 0.011, .amplitude = 0.003}};
        image.overrides = {
            {.x = kMeaninglessX, .y = kMeaninglessY, .channel = 0u, .value = std::numeric_limits<double>::quiet_NaN()}};
    }
    else if (rule == an::ValidityRule::ZeroIsNoData)
    {
        image.overrides = {{.x = kMeaninglessX, .y = kMeaninglessY, .channel = 0u, .value = 0.0}};
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> plain = fixtures::engine::makeImageAsset(session, image, *samples);
    EXPECT_TRUE(plain.hasValue()) << plain.status().message();
    if (!plain.hasValue())
    {
        return std::nullopt;
    }
    std::optional<an::ValidityMask> mask;
    if (rule == an::ValidityRule::ExplicitMask)
    {
        an::Result<an::consumer::SessionHold> hold = session.hold();
        EXPECT_TRUE(hold.hasValue());
        if (!hold.hasValue())
        {
            return std::nullopt;
        }
        an::Result<an::ValidityMask> made = an::ValidityMask::createAllValid(
            hold->engine(), *an::ImageExtent::create(image.width, image.height, 1u, hold->engine().limits()));
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        if (!made.hasValue())
        {
            return std::nullopt;
        }
        EXPECT_TRUE(made->view()->setValid(0u, kMeaninglessX, kMeaninglessY, false));
        mask.emplace(std::move(*made));
    }
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(
        an::ImageSemanticsRequest{.validity = *an::ValidityDescriptor::create({.rule = rule})});
    EXPECT_TRUE(semantics.hasValue());
    if (!semantics.hasValue())
    {
        return std::nullopt;
    }
    return withMetadata(session, *plain, an::ImageMetadata::create({}, *semantics, {}, {}, {}, std::nullopt),
                        std::move(mask));
}

/// Checks that a colour image demosaiced by `method` from `mosaicWithAMeaninglessSample` states its validity by a mask
/// clearing exactly the pixels whose footprint holds the meaningless sample: the method's reach inside the picture, one
/// pixel inside the border band its reach leaves.
void expectTheMeaninglessSampleMarked(const an::consumer::AssetReadLease& colour, an::DemosaicMethod method)
{
    const an::ValidityDescriptor rule =
        std::visit([](const auto& typed) { return typed.metadata().semantics().validity(); }, colour.image());
    EXPECT_EQ(rule.rule(), an::ValidityRule::ExplicitMask) << "the colour image states which samples mean something";
    const an::ValidityMask* mask = colour.validity();
    ASSERT_NE(mask, nullptr) << "the colour image carries the mask the demosaic wrote";
    const std::optional<an::ValidityMaskView<const std::uint8_t>> bits = mask->view();
    ASSERT_TRUE(bits.has_value());
    const std::uint32_t width = bits->width();
    const std::uint32_t height = bits->height();
    const auto reach = static_cast<std::int64_t>(an::demosaicMethodReach(method));
    std::uint32_t cleared = 0u;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const bool band = x < reach || y < reach || x + reach >= width || y + reach >= height;
            const std::int64_t footprint = band ? 1 : reach;
            const bool reached = std::abs(static_cast<std::int64_t>(x) - kMeaninglessX) <= footprint &&
                                 std::abs(static_cast<std::int64_t>(y) - kMeaninglessY) <= footprint;
            for (std::uint32_t plane = 0u; plane < bits->planeCount(); ++plane)
            {
                const std::optional<bool> valid = bits->test(plane, x, y);
                ASSERT_TRUE(valid.has_value());
                EXPECT_EQ(*valid, !reached) << "plane " << plane << " at " << x << "," << y;
            }
            cleared += reached ? 1u : 0u;
        }
    }
    EXPECT_GT(cleared, 1u) << "a meaningless sample reaches its neighbours' estimates";
}

} // namespace

// A demosaic honours the mosaic's validity: a NaN the finite-only rule leaves out, a zero the zero-is-no-data rule
// leaves out and a sample the mosaic's mask clears each make every colour pixel estimated from it meaningless, and the
// colour image says so by a mask rather than claiming every sample - through the view's demosaic and through Debayer.
TEST_F(GeometryProcessCase, ADemosaicMarksWhatAMeaninglessMosaicSampleReaches)
{
    for (const an::ValidityRule rule :
         {an::ValidityRule::FiniteOnly, an::ValidityRule::ZeroIsNoData, an::ValidityRule::ExplicitMask})
    {
        SCOPED_TRACE(std::string(an::validityRuleName(rule)));
        const std::optional<an::consumer::ImageAsset> mosaic = mosaicWithAMeaninglessSample(session(), rule);
        ASSERT_TRUE(mosaic.has_value());
        an::Result<an::consumer::ImageAsset> colour =
            an::ui::demosaicViewImage(session(), *mosaic, an::DemosaicMethod::Bilinear, {});
        ASSERT_TRUE(colour.hasValue()) << colour.status().message();
        an::Result<an::consumer::AssetReadLease> lease = colour->read();
        ASSERT_TRUE(lease.hasValue());
        expectTheMeaninglessSampleMarked(*lease, an::DemosaicMethod::Bilinear);
    }

    // Debayer's "Bilinear" is the engine's fast estimate.
    std::optional<an::consumer::ImageAsset> masked =
        mosaicWithAMeaninglessSample(session(), an::ValidityRule::ExplicitMask);
    ASSERT_TRUE(masked.has_value());
    ViewHandle target = assetView(std::move(*masked), "target");
    activate(context(), *target);
    WindowHandle window = openKept<DebayerProcess>(*target);
    setComboText(*window, "demosaicingMethod", QStringLiteral("Bilinear"));
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    an::Result<an::consumer::AssetReadLease> lease = context().madeViews.back()->document().read();
    ASSERT_TRUE(lease.hasValue());
    expectTheMeaninglessSampleMarked(*lease, an::DemosaicMethod::Fast);
    lease->release();

    // Each separated channel keeps the colour image's mask.
    window.reset();
    activate(context(), *target);
    window = openKept<DebayerProcess>(*target);
    setComboText(*window, "demosaicingMethod", QStringLiteral("Bilinear"));
    setComboText(*window, "outputMode", QStringLiteral("Separate RGB Channels"));
    const std::size_t separated = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), separated + 3u)
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    for (std::size_t made = separated; made < separated + 3u; ++made)
    {
        an::Result<an::consumer::AssetReadLease> channel = context().madeViews[made]->document().read();
        ASSERT_TRUE(channel.hasValue());
        expectTheMeaninglessSampleMarked(*channel, an::DemosaicMethod::Fast);
    }
    window.reset();
    dropView(context(), target);
}

// A floating mosaic in a physical domain has no full scale of one, so it is demosaiced against its own brightest
// sample: AMaZE estimates it as it estimates the same mosaic stated as fractions of that sample, rather than taking
// every sample above one for a clipped highlight.
TEST_F(GeometryProcessCase, APhysicalMosaicIsDemosaicedAgainstItsOwnBrightestSample)
{
    const fixtures::FixtureImage* catalogued = fixtures::fixtureNamed("mosaic-rggb-uint16-large");
    ASSERT_NE(catalogued, nullptr);
    fixtures::FixtureImage physical = *catalogued;
    physical.name = "mosaic-rggb-float32-physical-stars";
    physical.sampleType = fixtures::FixtureSampleType::Float32;
    physical.domain = fixtures::FixtureDomain::PhysicalScaled;
    physical.pattern = fixtures::FixturePattern::StarField;
    physical.levels = {{.base = 2400.0, .perColumn = 9.0, .perRow = 6.0, .alternate = 1.0, .amplitude = 30.0}};
    physical.stars = {{.x = 20.3, .y = 17.6, .width = 1.7, .peak = 41000.0},
                      {.x = 41.8, .y = 24.2, .width = 2.2, .peak = 52000.0},
                      {.x = 31.5, .y = 31.1, .width = 1.4, .peak = 23000.0}};
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(physical, error);
    ASSERT_TRUE(samples.has_value()) << error;
    float brightest = 0.0f;
    for (std::uint32_t y = 0u; y < samples->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples->width(); ++x)
        {
            brightest = (std::max)(brightest, static_cast<float>(samples->value(x, y, 0u)));
        }
    }
    ASSERT_GT(brightest, 1.0f);
    fixtures::FixtureImage fractions = physical;
    fractions.name = "mosaic-rggb-float32-fractions-stars";
    fractions.domain = fixtures::FixtureDomain::NormalizedLinear;
    fixtures::FixtureSamples fractionSamples(samples->width(), samples->height(), 1u,
                                             fixtures::FixtureSampleType::Float32);
    for (std::uint32_t y = 0u; y < samples->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples->width(); ++x)
        {
            fractionSamples.setValue(x, y, 0u,
                                     static_cast<double>(static_cast<float>(samples->value(x, y, 0u)) / brightest));
        }
    }
    an::Result<an::consumer::ImageAsset> physicalAsset =
        fixtures::engine::makeImageAsset(session(), physical, *samples);
    an::Result<an::consumer::ImageAsset> fractionAsset =
        fixtures::engine::makeImageAsset(session(), fractions, fractionSamples);
    ASSERT_TRUE(physicalAsset.hasValue()) << physicalAsset.status().message();
    ASSERT_TRUE(fractionAsset.hasValue()) << fractionAsset.status().message();

    an::Result<an::consumer::ImageAsset> physicalColour =
        an::ui::demosaicViewImage(session(), *physicalAsset, an::DemosaicMethod::Amaze, {});
    an::Result<an::consumer::ImageAsset> fractionColour =
        an::ui::demosaicViewImage(session(), *fractionAsset, an::DemosaicMethod::Amaze, {});
    ASSERT_TRUE(physicalColour.hasValue()) << physicalColour.status().message();
    ASSERT_TRUE(fractionColour.hasValue()) << fractionColour.status().message();
    const std::optional<fixtures::FixtureSamples> estimated = assetSamples(*physicalColour);
    const std::optional<fixtures::FixtureSamples> expected = assetSamples(*fractionColour);
    ASSERT_TRUE(estimated.has_value() && expected.has_value());
    ASSERT_EQ(estimated->channelCount(), 3u);
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < estimated->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < estimated->width(); ++x)
            {
                largest =
                    (std::max)(largest, std::abs(estimated->value(x, y, channel) -
                                                 static_cast<double>(brightest) * expected->value(x, y, channel)));
            }
        }
    }
    EXPECT_LE(largest, 1e-4 * static_cast<double>(brightest))
        << "the physical mosaic's estimate departs from its fractions' by " << largest
        << " against a brightest sample of " << brightest;
}

// A rule that does not make a non-finite sample meaningless leaves it among the samples that mean something, but it
// still has no brightness: the white level is the brightest finite sample, as it is for the same mosaic without it.
TEST_F(GeometryProcessCase, ANonFiniteSampleTheRuleKeepsDoesNotSetAPhysicalMosaicsWhiteLevel)
{
    const fixtures::FixtureImage* catalogued = fixtures::fixtureNamed("mosaic-rggb-uint16-large");
    ASSERT_NE(catalogued, nullptr);
    fixtures::FixtureImage physical = *catalogued;
    physical.name = "mosaic-rggb-float32-physical-zero-no-data";
    physical.sampleType = fixtures::FixtureSampleType::Float32;
    physical.domain = fixtures::FixtureDomain::PhysicalScaled;
    physical.pattern = fixtures::FixturePattern::StarField;
    physical.levels = {{.base = 2400.0, .perColumn = 9.0, .perRow = 6.0, .alternate = 1.0, .amplitude = 30.0}};
    physical.stars = {{.x = 40.3, .y = 37.6, .width = 1.7, .peak = 41000.0},
                      {.x = 51.8, .y = 44.2, .width = 2.2, .peak = 52000.0}};
    std::string error;
    const std::optional<fixtures::FixtureSamples> clean = fixtures::materializeFixture(physical, error);
    ASSERT_TRUE(clean.has_value()) << error;
    fixtures::FixtureImage poisoned = physical;
    poisoned.overrides = {{.x = 2u, .y = 2u, .channel = 0u, .value = std::numeric_limits<double>::quiet_NaN()}};
    const std::optional<fixtures::FixtureSamples> withNaN = fixtures::materializeFixture(poisoned, error);
    ASSERT_TRUE(withNaN.has_value()) << error;

    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{
        .validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData})});
    ASSERT_TRUE(semantics.hasValue());
    const auto stated =
        [this, &semantics](const fixtures::FixtureImage& image,
                           const fixtures::FixtureSamples& samples) -> std::optional<an::consumer::ImageAsset> {
        an::Result<an::consumer::ImageAsset> plain = fixtures::engine::makeImageAsset(session(), image, samples);
        EXPECT_TRUE(plain.hasValue()) << plain.status().message();
        if (!plain.hasValue())
        {
            return std::nullopt;
        }
        return withMetadata(session(), *plain, an::ImageMetadata::create({}, *semantics, {}, {}, {}, std::nullopt),
                            std::nullopt);
    };
    const std::optional<an::consumer::ImageAsset> cleanAsset = stated(physical, *clean);
    const std::optional<an::consumer::ImageAsset> nanAsset = stated(poisoned, *withNaN);
    ASSERT_TRUE(cleanAsset.has_value() && nanAsset.has_value());

    an::Result<an::consumer::ImageAsset> cleanColour =
        an::ui::demosaicViewImage(session(), *cleanAsset, an::DemosaicMethod::Amaze, {});
    an::Result<an::consumer::ImageAsset> nanColour =
        an::ui::demosaicViewImage(session(), *nanAsset, an::DemosaicMethod::Amaze, {});
    ASSERT_TRUE(cleanColour.hasValue()) << cleanColour.status().message();
    ASSERT_TRUE(nanColour.hasValue()) << nanColour.status().message();
    const std::optional<fixtures::FixtureSamples> expected = assetSamples(*cleanColour);
    const std::optional<fixtures::FixtureSamples> estimated = assetSamples(*nanColour);
    ASSERT_TRUE(estimated.has_value() && expected.has_value());
    // Beyond the reach of the non-finite sample itself, the two estimates are one.
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < estimated->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < estimated->width(); ++x)
            {
                if (x < 16u && y < 16u)
                {
                    continue;
                }
                largest =
                    (std::max)(largest, std::abs(estimated->value(x, y, channel) - expected->value(x, y, channel)));
            }
        }
    }
    EXPECT_EQ(largest, 0.0) << "a non-finite sample moved the estimate far from it by " << largest;
}

// Split CFA names each plane by the filter it holds under the mosaic's own phase, and refuses an image that is not a
// mosaic by name.
TEST_F(GeometryProcessCase, SplitCfaNamesEachPlaneByTheFilterItHolds)
{
    ViewHandle target = sceneView(session(), "mosaic-rggb-uint16", "target");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    {
        WindowHandle crop = openKept<CropProcess>(*target);
        setInt(*crop, "targetPosX", 1);
        setInt(*crop, "targetWidth", 22);
        ASSERT_TRUE(applyAndSettle(context(), *crop));
    }
    ASSERT_EQ(target->imageDescriptor()->cfa().colorAt(0u, 0u), an::CfaColor::Green);
    WindowHandle window = openKept<SplitCFAProcess>(*target);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 4u);
    const std::array<const char*, 4u> names{"target - CFA0 - Green1", "target - CFA1 - Red", "target - CFA2 - Blue",
                                            "target - CFA3 - Green2"};
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        EXPECT_EQ(context().madeViews[before + plane]->document().names().displayName.toStdString(), names[plane]);
    }
    window.reset();
    dropView(context(), target);

    ViewHandle grey = sceneView(session(), "scene-mono-float32", "grey");
    ASSERT_NE(grey, nullptr);
    activate(context(), *grey);
    WindowHandle refused = openKept<SplitCFAProcess>(*grey);
    const std::size_t viewsBefore = context().madeViews.size();
    pressOk(*refused);
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() >= 1; }));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running Split CFA"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("states no colour filter array")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), viewsBefore);
    refused.reset();
    dropView(context(), grey);
}

// Merge CFA joins the four planes it is given, says so in the new view's history under its own name, refuses planes of
// different sizes and a mixture of sequences and images by name, and joins four sequences frame by frame into one
// mosaic sequence.
TEST_F(GeometryProcessCase, MergeCfaJoinsItsPlanesAndRefusesWhatCannotBeJoined)
{
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    std::vector<ViewHandle> planes;
    {
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
        an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
        ASSERT_TRUE(scope.hasValue() && lease.hasValue());
        an::Result<std::vector<an::consumer::ImageAsset>> split =
            an::ui::processes::splitMosaicPlanes(*scope, an::ui::ProcessSource::of(*lease));
        ASSERT_TRUE(split.hasValue());
        lease->release();
        scope->finish(an::Status::success());
        for (std::size_t plane = 0u; plane < 4u; ++plane)
        {
            planes.push_back(assetView(std::move((*split)[plane]), "cfa" + std::to_string(plane)));
            activate(context(), *planes.back());
        }
    }
    WindowHandle window(new MergeCFAProcess());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*window, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u);
    ImageViewControl* merged = context().madeViews.back();
    EXPECT_EQ(merged->document().names().displayName, QStringLiteral("CFA MERGED"));
    EXPECT_EQ(madeHistoryOf(context(), merged),
              "MergeCFA: created 'RGGB' pattern from image channels CFA0:'cfa0' CFA1:'cfa1' CFA2:'cfa2 CFA3:'cfa3'");
    EXPECT_EQ(merged->imageDescriptor()->cfa().pattern(), an::CfaPattern::Rggb);
    EXPECT_EQ(madeNeighbourOf(context(), merged), planes[0].get());

    // Under another arrangement, the mosaic states it; a plane of a higher representation lifts the others to it.
    std::optional<an::consumer::ImageAsset> lifted =
        convertedTo(session(), *planes[2]->document().asset(), an::SampleType::Float32);
    ASSERT_TRUE(lifted.has_value());
    ViewHandle floating = assetView(std::move(*lifted), "cfa2f");
    activate(context(), *floating);
    WindowHandle bggr(new MergeCFAProcess());
    bggr->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*bggr, selector.c_str(),
                     QString::fromStdString(plane == 2u ? floating->getUniqueName() : planes[plane]->getUniqueName()));
    }
    setComboText(*bggr, "patternSelector", QStringLiteral("BGGR"));
    before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *bggr));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ImageDescriptor> joined = context().madeViews.back()->imageDescriptor();
    ASSERT_TRUE(joined.has_value());
    EXPECT_EQ(joined->cfa().pattern(), an::CfaPattern::Bggr);
    EXPECT_EQ(joined->sampleType(), an::SampleType::Float32);
    bggr.reset();
    dropView(context(), floating);

    // A plane of another size.
    std::optional<an::consumer::ImageAsset> smaller = an::tests::viewer::makeAsset<float>(
        session(), 5u, 4u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return 0.1f * static_cast<float>(x + y); });
    ASSERT_TRUE(smaller.has_value());
    ViewHandle small = assetView(std::move(*smaller), "small");
    activate(context(), *small);
    WindowHandle mismatched(new MergeCFAProcess());
    mismatched->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 3u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*mismatched, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    setComboText(*mismatched, "cfa3Selector", QStringLiteral("small"));
    before = context().madeViews.size();
    pressOk(*mismatched);
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() >= 1; }));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running Merge CFA"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("All the images must have the same size")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), before);
    mismatched.reset();
    dropView(context(), small);

    // A sequence among images.
    const QString path = writeSceneSequence(session(), directory(), {"scene-mono-float32"});
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList, "planes");
    ASSERT_NE(sequence, nullptr);
    WindowHandle mixed(new MergeCFAProcess());
    mixed->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 3u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*mixed, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    setComboText(*mixed, "cfa3Selector", QString::fromStdString(sequence->getUniqueName()));
    const int errors = context().errorCount();
    pressOk(*mixed);
    ASSERT_TRUE(pumpUntil([this, errors]() { return context().errorCount() > errors; }));
    EXPECT_TRUE(context().errorTexts.back().contains(
        QStringLiteral("All the selected images must be either sequences or images")))
        << context().errorTexts.back().toStdString();
    mixed.reset();
    window.reset();
    for (ViewHandle& plane : planes)
    {
        dropView(context(), plane);
    }
}

// Merge CFA over four sequences joins them frame by frame into one mosaic sequence, published whole.
// A merge reads its planes under leases taken before it asks to be admitted. An edit of a plane's view holds its writer
// and then asks to be admitted, so at a width of one a merge admitted first would hold the only thread while it waited
// for the writer, and the edit would wait for the thread: neither would end.
TEST_F(GeometryProcessCaseAtWidthOne, MergeCfaWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    std::vector<ViewHandle> planes;
    {
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
        an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
        ASSERT_TRUE(scope.hasValue() && lease.hasValue());
        an::Result<std::vector<an::consumer::ImageAsset>> split =
            an::ui::processes::splitMosaicPlanes(*scope, an::ui::ProcessSource::of(*lease));
        ASSERT_TRUE(split.hasValue());
        lease->release();
        scope->finish(an::Status::success());
        for (std::size_t plane = 0u; plane < 4u; ++plane)
        {
            planes.push_back(assetView(std::move((*split)[plane]), "cfa" + std::to_string(plane)));
            activate(context(), *planes.back());
        }
    }
    WindowHandle window(new MergeCFAProcess());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*window, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    const std::size_t before = context().madeViews.size();

    an::tests::viewer::EditHoldingTheWriter edit(*planes[1]);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    pressOk(*window);
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the merge was admitted while it could not read its planes";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the merge held while the merge waited for the edit's writer";

    ASSERT_TRUE(settleWindow(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u);
    EXPECT_EQ(context().madeViews.back()->imageDescriptor()->cfa().pattern(), an::CfaPattern::Rggb);
}

TEST_F(GeometryProcessCase, MergeCfaJoinsFourSequencesFrameByFrame)
{
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    std::vector<fixtures::FixtureSamples> planeSamples;
    {
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
        an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
        ASSERT_TRUE(scope.hasValue() && lease.hasValue());
        an::Result<std::vector<an::consumer::ImageAsset>> split =
            an::ui::processes::splitMosaicPlanes(*scope, an::ui::ProcessSource::of(*lease));
        ASSERT_TRUE(split.hasValue());
        lease->release();
        scope->finish(an::Status::success());
        for (an::consumer::ImageAsset& plane : *split)
        {
            std::optional<fixtures::FixtureSamples> samples = assetSamples(plane);
            ASSERT_TRUE(samples.has_value());
            planeSamples.push_back(std::move(*samples));
        }
    }
    // Each plane written as a two-frame sequence through the Split CFA window over a two-frame mosaic sequence.
    const QString path = writeSceneSequence(session(), directory(), {"mosaic-rggb-uint16", "mosaic-rggb-uint16"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList, "mosaics");
    ASSERT_NE(source, nullptr);
    std::size_t before = context().madeViews.size();
    {
        WindowHandle split = openKept<SplitCFAProcess>(*source);
        ASSERT_TRUE(applyAndSettle(context(), *split));
    }
    ASSERT_EQ(context().madeViews.size(), before + 4u);
    std::array<ImageViewControl*, 4u> sequences{};
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        sequences[plane] = context().madeViews[before + plane];
        ASSERT_TRUE(sequences[plane]->isSequence());
        const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *sequences[plane]);
        ASSERT_EQ(frames.size(), 2u);
        EXPECT_EQ(frames[1].digest(), planeSamples[plane].digest());
    }
    WindowHandle window(new MergeCFAProcess());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*window, selector.c_str(), QString::fromStdString(sequences[plane]->getUniqueName()));
    }
    before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* merged = context().madeViews.back();
    ASSERT_TRUE(merged->isSequence());
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *merged);
    const std::optional<fixtures::FixtureSamples> original = assetSamples(*mosaic);
    ASSERT_EQ(frames.size(), 2u);
    ASSERT_TRUE(original.has_value());
    EXPECT_EQ(frames[0].digest(), original->digest());
    EXPECT_EQ(frames[1].digest(), original->digest());

    // A run whose window is gone when it lands opens nothing and leaves no sequence behind.
    const auto temporaryEntries = [this]() {
        return QDir(context().getTempDirPath())
            .entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)
            .size();
    };
    const qsizetype entries = temporaryEntries();
    before = context().madeViews.size();
    pressOk(*window);
    QThreadPool::globalInstance()->waitForDone();
    // Gone before its landing is delivered; the handle's own deleter would let it land first.
    delete window.release();
    ASSERT_TRUE(pumpUntil([&temporaryEntries, entries]() { return temporaryEntries() == entries; }))
        << QDir(context().getTempDirPath())
               .entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)
               .join(QStringLiteral(", "))
               .toStdString()
        << " where there were " << entries << " entries";
    EXPECT_EQ(context().madeViews.size(), before);
}

// Extract Sequence writes the frames of the range, as recorded, into a new sequence of the source's container under
// the source's name, shown as the source is, and says which frames it took.
TEST_F(GeometryProcessCase, ExtractSequenceWritesTheRangeAsRecorded)
{
    const QString path = writeSceneSequence(session(), directory(),
                                            {"scene-rgb-float32", "scene-rgb-float32-second", "scene-rgb-float32"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList, "frames");
    ASSERT_NE(source, nullptr);
    const std::vector<fixtures::FixtureSamples> recorded = sequenceFrames(session(), *source);
    ASSERT_EQ(recorded.size(), 3u);
    WindowHandle window = openKept<ExtractSequenceProcess>(*source);
    setInt(*window, "startFrameValue", 2);
    setInt(*window, "endFrameValue", 3);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* extracted = context().madeViews.back();
    ASSERT_TRUE(extracted->isSequence());
    EXPECT_EQ(extracted->document().names().displayName, QStringLiteral("frames"));
    EXPECT_EQ(madeHistoryOf(context(), extracted), "Extract Sequence: Extracted frames 2 to 3 from sequence 'frames'");
    EXPECT_EQ(extracted->document().sequence()->descriptor.format, an::SequenceFormat::ImageList);
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *extracted);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].digest(), recorded[1].digest());
    EXPECT_EQ(frames[1].digest(), recorded[2].digest());
    window.reset();
}

// A sequence run with no temporary files lent, or one stopped, leaves nothing behind and the view as it was.
TEST_F(GeometryProcessCase, ASequenceRunThatCannotFinishLeavesNothingBehind)
{
    const QString path = writeSceneSequence(session(), directory(), {"scene-rgb-float32", "scene-rgb-float32"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList, "frames");
    ASSERT_NE(source, nullptr);
    context().lendTemporaryFiles = false;
    {
        WindowHandle window = openKept<ExtractSequenceProcess>(*source);
        const std::size_t before = context().madeViews.size();
        pressOk(*window);
        ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() >= 1; }));
        EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running Extract Sequence"));
        EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("temporary files")))
            << context().errorTexts.back().toStdString();
        EXPECT_EQ(context().madeViews.size(), before);
        EXPECT_TRUE(source->tryRequestLockForProcessing()) << "the view is let go";
        source->releaseLockForProcessing();
    }
    context().lendTemporaryFiles = true;
    const GeometryProcess* crop = processOfAction("actionCrop");
    ASSERT_NE(crop, nullptr);
    const QString next = context().nextTempFilePathForNewSequence(an::SequenceFormat::ImageList);
    WindowHandle stopping = crop->openStopping(source);
    crop->configure(*stopping, "-");
    ASSERT_TRUE(applyAndSettle(context(), *stopping));
    EXPECT_FALSE(QFileInfo::exists(next)) << "a stopped run leaves no sequence behind";
    EXPECT_EQ(source->document().sequence()->sourcePath, path);
    stopping.reset();
}

// An in-place run over a sequence transforms every frame of it, whatever frames the player has selected: the sequence
// it shows is replaced whole.
TEST_F(GeometryProcessCase, AnInPlaceSequenceRunTransformsEveryFrameWhateverIsSelected)
{
    const QString path = writeSceneSequence(session(), directory(),
                                            {"scene-rgb-float32", "scene-rgb-float32-second", "scene-rgb-float32"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList, "frames");
    ASSERT_NE(source, nullptr);
    std::optional<an::ui::DocumentSequence> sequence = source->document().sequence();
    ASSERT_TRUE(sequence.has_value());
    sequence->selectedFrames = std::vector<std::uint64_t>{1u};
    source->document().setSequence(*sequence);
    WindowHandle window = openKept<CropProcess>(*source);
    setInt(*window, "targetWidth", 5);
    ASSERT_TRUE(applyAndSettle(context(), *window));
    std::optional<an::ui::DocumentSequence> shown = source->document().sequence();
    ASSERT_TRUE(shown.has_value());
    shown->selectedFrames.reset();
    source->document().setSequence(*shown);
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *source);
    ASSERT_EQ(frames.size(), 3u);
    for (const fixtures::FixtureSamples& frame : frames)
    {
        EXPECT_EQ(frame.width(), 5u);
    }
    window.reset();
}

// error: an edit the history cannot keep is not applied, and the person hears of it once - from the history, which
// says why - and not again from the window.
TEST_F(GeometryProcessCase, AnEditTheHistoryCannotKeepIsSaidOnceAndChangesNothing)
{
    TearDown();
    std::unique_ptr<ProcessTestContext> tight = openProcessContext(4u, an::tests::viewer::historyCannotKeepEdits());
    ASSERT_NE(tight, nullptr);
    const InstalledContext installed(*tight);
    ViewHandle target = sceneView(tight->engineSession(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    activate(*tight, *target);
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = openKept<CropProcess>(*target);
    setInt(*window, "targetPosX", 1);
    ASSERT_TRUE(applyAndSettle(*tight, *window));
    ASSERT_TRUE(pumpUntil([&tight]() { return tight->errorCount() >= 1; })) << "the history says why";
    ASSERT_TRUE(settleWindow(*tight, *window));
    EXPECT_EQ(target->document().revision(), before);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(tight->errorCount(), 1) << tight->errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(tight->messages.back(), QStringLiteral("error:Image History"));
    window.reset();
    dropView(*tight, target);
}

// Rotate's optional crop keeps a rectangle of the turned image.
TEST_F(GeometryProcessCase, RotateCropsTheTurnedImageWhenAsked)
{
    ViewHandle turned = sceneView(session(), "scene-rgb-float32", "turned");
    ASSERT_NE(turned, nullptr);
    activate(context(), *turned);
    {
        WindowHandle window = openKept<RotateProcess>(*turned);
        setDouble(*window, "rotationAngle", 30.0);
        ASSERT_TRUE(applyAndSettle(context(), *window));
    }
    const std::optional<fixtures::FixtureSamples> whole = documentSamples(turned->document());
    ASSERT_TRUE(whole.has_value());
    dropView(context(), turned);

    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    WindowHandle window = openKept<RotateProcess>(*target);
    setDouble(*window, "rotationAngle", 30.0);
    setGroupChecked(*window, "groupBox", true);
    setInt(*window, "targetPosX", 2);
    setInt(*window, "targetPosY", 1);
    setInt(*window, "targetWidth", 5);
    setInt(*window, "targetHeight", 4);
    const RotateProcess::Settings asked = static_cast<RotateProcess&>(*window).settings();
    ASSERT_TRUE(asked.crop.has_value());
    EXPECT_EQ(RotateProcess::stepDescription(asked),
              "Rotated 30 degrees - Cropped area : X: 2 Y: 1 W:5 H:4 - Interpolation: Lanczos4");
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value());
    ASSERT_EQ(made->width(), 5u);
    ASSERT_EQ(made->height(), 4u);
    for (std::uint32_t c = 0u; c < made->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < 4u; ++y)
        {
            for (std::uint32_t x = 0u; x < 5u; ++x)
            {
                EXPECT_EQ(made->value(x, y, c), whole->value(x + 2u, y + 1u, c));
            }
        }
    }
    window.reset();
    dropView(context(), target);
}

// The crop a rotation onto a grown canvas offers reaches exactly the canvas the run makes: the engine's plan of the
// rotation decides it, at every angle. The limits came from the preview's drawing of the turned frame, which could be a
// row or a column off the canvas.
TEST_F(GeometryProcessCase, RotateOffersACropOfExactlyTheCanvasTheRunMakes)
{
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<an::ImageDescriptor> described = target->imageDescriptor();
    ASSERT_TRUE(described.has_value());
    WindowHandle window = openKept<RotateProcess>(*target);
    setChecked(*window, "resizeImage", true);
    auto* posX = window->findChild<QSpinBox*>(QStringLiteral("targetPosX"));
    auto* posY = window->findChild<QSpinBox*>(QStringLiteral("targetPosY"));
    ASSERT_NE(posX, nullptr);
    ASSERT_NE(posY, nullptr);
    for (double degrees = 1.0; degrees < 180.0; degrees += 7.0)
    {
        setDouble(*window, "rotationAngle", degrees);
        const an::Result<an::GeometryPlan> plan = an::ui::processes::planGeometryStep(
            *described, an::ui::processes::rotationRequest(degrees, true, an::KernelInterpolation::Linear));
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const an::ImageExtent& canvas = plan->destinationDescriptor().extent();
        EXPECT_EQ(posX->maximum(), static_cast<int>(canvas.width())) << degrees << " degrees";
        EXPECT_EQ(posY->maximum(), static_cast<int>(canvas.height())) << degrees << " degrees";
    }
    window.reset();
    dropView(context(), target);
}

// Split CFA and Merge CFA refuse a mosaic whose valid samples are stated in a mask, which they carry through no plane.
TEST_F(GeometryProcessCase, AMosaicWhoseValidityIsAMaskIsRefusedByName)
{
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    an::ImageSemanticsRequest meaning;
    meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *an::ImageSemantics::create(meaning),
                                  an::AstrometricSolution{}, an::Provenance{}, an::CodecMetadata{}, std::nullopt);
    std::optional<an::ValidityMask> mask;
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        an::Result<an::ValidityMask> made = an::ValidityMask::create(scope->engine(), mosaic->descriptor()->extent());
        ASSERT_TRUE(made.hasValue());
        std::optional<an::ValidityMaskView<std::uint8_t>> view = made->view();
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < view->width(); ++x)
            {
                (void)view->setValid(0u, x, y, true);
            }
        }
        mask.emplace(std::move(*made));
    }
    std::optional<an::consumer::ImageAsset> masked = withMetadata(session(), *mosaic, metadata, std::move(mask));
    ASSERT_TRUE(masked.has_value());
    ViewHandle target = assetView(std::move(*masked), "masked");
    activate(context(), *target);
    WindowHandle window = openKept<SplitCFAProcess>(*target);
    const std::size_t before = context().madeViews.size();
    pressOk(*window);
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() >= 1; }));
    ASSERT_TRUE(settleWindow(context(), *window));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running Split CFA"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("stated in a mask cannot be split")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), before);
    // The narrowband extractions take a mosaic whose every sample is valid, and refuse it before any work.
    for (const char* method : {"Extract Ha", "Extract Green", "Extract Ha/OIII"})
    {
        SCOPED_TRACE(method);
        setComboText(*window, "methodSelector", QString::fromLatin1(method));
        const int errors = context().errorCount();
        pressOk(*window);
        ASSERT_TRUE(pumpUntil([this, errors]() { return context().errorCount() > errors; }));
        ASSERT_TRUE(settleWindow(context(), *window));
        EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("cannot have a narrowband channel extracted")))
            << context().errorTexts.back().toStdString();
        EXPECT_EQ(context().madeViews.size(), before);
    }
    window.reset();
    dropView(context(), target);
}

// A resize that scales the axes differently decides a new grid of square pixels, so the image it makes can be
// rotated.
TEST_F(GeometryProcessCase, AnImageResizedUnevenlyKeepsSquarePixelsAndCanBeRotated)
{
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const auto pixelAspect = [&target]() -> std::optional<double> {
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        if (!lease.hasValue())
        {
            return std::nullopt;
        }
        const std::optional<an::ImageDescriptor> described =
            std::visit([](const auto& typed) { return typed.descriptor(); }, lease->image());
        return described.has_value() ? std::optional<double>(described->pixelAspectRatio()) : std::nullopt;
    };
    {
        WindowHandle window = openKept<ResizeProcess>(*target);
        setChecked(*window, "keepAspectRatio", false);
        setInt(*window, "targetWidth", 7);
        setInt(*window, "targetHeight", 6);
        ASSERT_TRUE(applyAndSettle(context(), *window));
    }
    const std::optional<fixtures::FixtureSamples> resized = documentSamples(target->document());
    ASSERT_TRUE(resized.has_value());
    ASSERT_EQ(resized->width(), 7u);
    ASSERT_EQ(resized->height(), 6u);
    EXPECT_EQ(pixelAspect(), std::optional<double>(1.0));
    {
        WindowHandle window = openKept<RotateProcess>(*target);
        setDouble(*window, "rotationAngle", 30.0);
        ASSERT_TRUE(applyAndSettle(context(), *window));
    }
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> rotated = documentSamples(target->document());
    ASSERT_TRUE(rotated.has_value());
    EXPECT_NE(rotated->digest(), resized->digest()) << "the rotation was applied";
    EXPECT_EQ(pixelAspect(), std::optional<double>(1.0));
    dropView(context(), target);
}

// Debayer over a mosaic sequence its view shows demosaiced reads the frames as recorded, which are mosaics, so it asks
// nothing before it runs.
TEST_F(GeometryProcessCase, DebayerAsksNothingOfAMosaicSequenceItsViewDemosaics)
{
    const QString path = writeSceneSequence(session(), directory(), {"mosaic-rggb-uint16", "mosaic-rggb-uint16"});
    ASSERT_FALSE(path.isEmpty());
    ImageViewControl* view =
        openDemosaicedSequenceView(context(), path, an::SequenceFormat::ImageList, std::string("frames"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->isSequenceAutodebayerEnabled());
    ASSERT_FALSE(view->isCFA()) << "the view shows colour frames";
    context().answerYes = false;
    const std::size_t messages = context().messages.size();
    const std::size_t before = context().madeViews.size();
    WindowHandle window = openKept<DebayerProcess>(*view);
    pressOk(*window);
    ASSERT_TRUE(pumpUntil([this, before]() {
        return context().madeViews.size() > before || context().errorCount() > 0;
    })) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_TRUE(settleWindow(context(), *window));
    for (qsizetype index = static_cast<qsizetype>(messages); index < context().messages.size(); ++index)
    {
        EXPECT_FALSE(context().messages[index].startsWith(QStringLiteral("question:")))
            << context().messages[index].toStdString();
    }
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *context().madeViews.back());
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].channelCount(), 3u);
    window.reset();
}

// A crop, a resize and an integer resample carry a still image's solution moved with its pixels and the solver's card
// - its reference pixel moved and its matrix scaled: every pixel of the result points where the source's pixel it came
// from pointed. A rotation drops the solution. No other card stays.
TEST_F(GeometryProcessCase, AStillImagesSolutionFollowsACropAResizeAndAResampleAndNotARotation)
{
    const std::optional<an::consumer::ImageAsset> plain = sceneAsset(session(), "scene-rgb-float32");
    ASSERT_TRUE(plain.has_value());
    const an::ImageExtent extent = plain->descriptor()->extent();
    const double width = extent.width();
    const double height = extent.height();
    constexpr double kAngle = 17.0 * std::numbers::pi / 180.0;
    constexpr double kScale = 2.4 / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.4 * width, .y = 0.55 * height};
    request.referencePoint = {.rightAscensionDegrees = 83.82, .declinationDegrees = -5.39};
    request.linear = {.cd11 = -kScale * std::cos(kAngle),
                      .cd12 = kScale * std::sin(kAngle),
                      .cd21 = kScale * std::sin(kAngle),
                      .cd22 = kScale * std::cos(kAngle)};
    request.imageWidth = extent.width();
    request.imageHeight = extent.height();
    const an::Result<an::WcsSolution> truth = an::WcsSolution::create(request);
    ASSERT_TRUE(truth.hasValue()) << truth.status().message();
    an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "");
    an::Result<an::CodecCard> solver = an::CodecCard::create(an::CodecNamespace::Fits, "PLTSOLVR", "'ASTAP'", "");
    ASSERT_TRUE(object.hasValue() && solver.hasValue());
    an::Result<an::CodecMetadata> cards = an::CodecMetadata::create(an::SourceEncoding{}, {*object, *solver});
    ASSERT_TRUE(cards.hasValue());
    an::Result<an::CodecMetadata> solved = an::withWcsCards(*cards, *truth);
    an::Result<an::AstrometricSolution> summary = truth->toAstrometricSolution();
    ASSERT_TRUE(solved.hasValue() && summary.hasValue());
    const an::ImageMetadata metadata = an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{},
                                                                 *summary, an::Provenance{}, *solved, std::nullopt);

    /// Where a result pixel's edge coordinate came from in the source, stated from the step's geometry.
    struct Followed
    {
        const char* name;
        const char* mode;
        std::function<an::PixelCoordinate(an::PixelCoordinate, double, double)> source;
        bool keepsSolution;
    };
    const std::vector<Followed> steps{
        {"Crop", "x-2-y-1-width-8-height-5",
         [](an::PixelCoordinate at, double, double) { return an::PixelCoordinate{.x = at.x + 2.0, .y = at.y + 1.0}; },
         true},
        {"Resize", "percent-150-bicubic",
         [width, height](an::PixelCoordinate at, double resultWidth, double resultHeight) {
             return an::PixelCoordinate{.x = at.x * width / resultWidth, .y = at.y * height / resultHeight};
         },
         true},
        {"IntegerResample", "-",
         [](an::PixelCoordinate at, double, double) { return an::PixelCoordinate{.x = at.x * 2.0, .y = at.y * 2.0}; },
         true},
        {"Rotate", "angle-30-auto", nullptr, false},
    };
    for (const Followed& step : steps)
    {
        SCOPED_TRACE(step.name);
        const GeometryProcess* under = nullptr;
        for (const GeometryProcess& process : geometryProcesses())
        {
            if (std::string_view(process.name) == step.name)
            {
                under = &process;
            }
        }
        ASSERT_NE(under, nullptr);
        std::optional<an::consumer::ImageAsset> described = withMetadata(session(), *plain, metadata);
        ASSERT_TRUE(described.has_value());
        ViewHandle target = assetView(std::move(*described), "target");
        activate(context(), *target);
        WindowHandle window = under->open(target.get());
        under->configure(*window, step.mode);
        ASSERT_TRUE(applyAndSettle(context(), *window));
        const std::optional<an::ImageMetadata> made = documentMetadata(target->document());
        const std::optional<an::ImageDescriptor> result = target->imageDescriptor();
        ASSERT_TRUE(made.has_value() && result.has_value());
        EXPECT_FALSE(hasCard(*made, "OBJECT")) << "no other card stays";
        const an::Result<an::WcsSolution> carried =
            an::ui::readImageWcsSolution(made->codec().cards(), result->extent().width(), result->extent().height());
        if (!step.keepsSolution)
        {
            EXPECT_FALSE(carried.hasValue());
            EXPECT_FALSE(made->astrometry().present());
            EXPECT_FALSE(hasCard(*made, "PLTSOLVR"));
            window.reset();
            dropView(context(), target);
            continue;
        }
        ASSERT_TRUE(carried.hasValue()) << carried.status().message();
        EXPECT_TRUE(made->astrometry().present());
        EXPECT_TRUE(hasCard(*made, "PLTSOLVR"));
        const double resultWidth = result->extent().width();
        const double resultHeight = result->extent().height();
        for (const an::PixelCoordinate at :
             {an::PixelCoordinate{.x = 0.5, .y = 0.5}, an::PixelCoordinate{.x = resultWidth - 0.5, .y = 0.5},
              an::PixelCoordinate{.x = 0.5 * resultWidth, .y = resultHeight - 1.5}})
        {
            const an::Result<an::SkyCoordinate> moved = carried->pixelToWorld(at);
            const an::Result<an::SkyCoordinate> original =
                truth->pixelToWorld(step.source(at, resultWidth, resultHeight));
            ASSERT_TRUE(moved.hasValue() && original.hasValue());
            EXPECT_NEAR(moved->rightAscensionDegrees, original->rightAscensionDegrees, 1e-9);
            EXPECT_NEAR(moved->declinationDegrees, original->declinationDegrees, 1e-9);
        }
        window.reset();
        dropView(context(), target);
    }
}

// A resize that keeps the aspect ratio lands each side of its result on a whole pixel, so a frame its percentage does
// not divide is scaled a little differently along each axis: 1001 by 751 at half its size is 500 by 375. Those two
// scales agree within a thousandth, so the solution keeps its distortion model and the cards that state it, every
// coefficient rescaled by the mean of the two scales, and still points every pixel where the source's pixel it came
// from pointed. A resize whose scales differ by more keeps the linear solution and drops the model.
TEST_F(GeometryProcessCase, AnAspectLockedResizeKeepsTheDistortionModelItsRoundingBarelyMoves)
{
    fixtures::FixtureImage image;
    image.name = "solved-field-1001x751";
    image.width = 1001u;
    image.height = 751u;
    image.channels = fixtures::FixtureChannels::Mono;
    image.sampleType = fixtures::FixtureSampleType::Float32;
    image.domain = fixtures::FixtureDomain::NormalizedLinear;
    image.pattern = fixtures::FixturePattern::GradientNoise;
    image.seed = 41u;
    image.levels = {{.base = 0.1, .perColumn = 0.0004, .perRow = 0.0005, .amplitude = 0.01}};
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
    ASSERT_TRUE(samples.has_value()) << error;
    const an::Result<an::consumer::ImageAsset> plain = fixtures::engine::makeImageAsset(session(), image, *samples);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();

    an::SipDistortionRequest bend;
    bend.referencePixel = {.x = 500.5, .y = 375.5};
    bend.forwardX.order = 3u;
    bend.forwardX.coefficients[2][0] = 4.0e-7;
    bend.forwardX.coefficients[0][2] = -1.5e-7;
    bend.forwardX.coefficients[3][0] = 2.0e-10;
    bend.forwardY.order = 3u;
    bend.forwardY.coefficients[1][1] = 2.5e-7;
    bend.forwardY.coefficients[1][2] = -3.0e-10;
    bend.inverseX.order = 3u;
    bend.inverseX.coefficients[2][0] = -4.0e-7;
    bend.inverseX.coefficients[0][2] = 1.5e-7;
    bend.inverseY.order = 3u;
    bend.inverseY.coefficients[1][1] = -2.5e-7;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(bend);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    constexpr double kScale = 2.4 / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = bend.referencePixel;
    request.referencePoint = {.rightAscensionDegrees = 83.82, .declinationDegrees = -5.39};
    request.linear = {.cd11 = -kScale, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScale};
    request.imageWidth = image.width;
    request.imageHeight = image.height;
    request.distortion = *distortion;
    const an::Result<an::WcsSolution> truth = an::WcsSolution::create(request);
    ASSERT_TRUE(truth.hasValue()) << truth.status().message();
    an::Result<an::CodecCard> solver = an::CodecCard::create(an::CodecNamespace::Fits, "PLTSOLVR", "'ASTAP'", "");
    ASSERT_TRUE(solver.hasValue());
    an::Result<an::CodecMetadata> cards = an::CodecMetadata::create(an::SourceEncoding{}, {*solver});
    ASSERT_TRUE(cards.hasValue());
    an::Result<an::CodecMetadata> solved = an::withWcsCards(*cards, *truth);
    an::Result<an::AstrometricSolution> summary = truth->toAstrometricSolution();
    ASSERT_TRUE(solved.hasValue() && summary.hasValue());
    const an::ImageMetadata metadata = an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{},
                                                                 *summary, an::Provenance{}, *solved, std::nullopt);
    ASSERT_TRUE(hasCard(metadata, "A_ORDER")) << "the source's cards state its distortion model";

    struct Resized
    {
        const char* name;
        bool keepAspectRatio;
        std::uint32_t width;
        std::uint32_t height;
        bool keepsDistortion;
    };
    for (const Resized& step : {Resized{"half the size, aspect ratio kept", true, 500u, 375u, true},
                                Resized{"500 by 300", false, 500u, 300u, false}})
    {
        SCOPED_TRACE(step.name);
        std::optional<an::consumer::ImageAsset> described = withMetadata(session(), *plain, metadata);
        ASSERT_TRUE(described.has_value());
        ViewHandle target = assetView(std::move(*described), "target");
        activate(context(), *target);
        {
            WindowHandle window = openKept<ResizeProcess>(*target);
            setChecked(*window, "keepAspectRatio", step.keepAspectRatio);
            if (step.keepAspectRatio)
            {
                setDouble(*window, "targetWidthPercent", 50.0);
            }
            else
            {
                setInt(*window, "targetWidth", static_cast<int>(step.width));
                setInt(*window, "targetHeight", static_cast<int>(step.height));
            }
            ASSERT_TRUE(applyAndSettle(context(), *window));
        }
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<an::ImageMetadata> made = documentMetadata(target->document());
        const std::optional<an::ImageDescriptor> result = target->imageDescriptor();
        ASSERT_TRUE(made.has_value() && result.has_value());
        ASSERT_EQ(result->extent().width(), step.width);
        ASSERT_EQ(result->extent().height(), step.height);
        const double scaleX = static_cast<double>(step.width) / image.width;
        const double scaleY = static_cast<double>(step.height) / image.height;
        const double spread = std::abs(scaleX - scaleY) / (std::max)(scaleX, scaleY);
        EXPECT_GT(spread, 0.0) << "the two axes are scaled differently";
        EXPECT_EQ(spread < 1.0e-3, step.keepsDistortion);

        const an::Result<an::WcsSolution> carried =
            an::ui::readImageWcsSolution(made->codec().cards(), step.width, step.height);
        ASSERT_TRUE(carried.hasValue()) << carried.status().message();
        EXPECT_TRUE(made->astrometry().present());
        EXPECT_TRUE(hasCard(*made, "PLTSOLVR"));
        EXPECT_NEAR(carried->referencePixel().x, truth->referencePixel().x * scaleX, 1e-9);
        EXPECT_NEAR(carried->referencePixel().y, truth->referencePixel().y * scaleY, 1e-9);
        for (const char* keyword : {"A_ORDER", "B_ORDER", "AP_ORDER", "BP_ORDER"})
        {
            EXPECT_EQ(hasCard(*made, keyword), step.keepsDistortion) << keyword;
        }
        if (!step.keepsDistortion)
        {
            EXPECT_FALSE(carried->distortion().hasForwardModel() || carried->distortion().hasInverseModel());
            dropView(context(), target);
            continue;
        }
        ASSERT_TRUE(carried->distortion().hasForwardModel() && carried->distortion().hasInverseModel());
        const double mean = 0.5 * (scaleX + scaleY);
        const std::pair<const an::SipPolynomial*, const an::SipPolynomial*> components[] = {
            {&truth->distortion().forwardX(), &carried->distortion().forwardX()},
            {&truth->distortion().forwardY(), &carried->distortion().forwardY()},
            {&truth->distortion().inverseX(), &carried->distortion().inverseX()},
            {&truth->distortion().inverseY(), &carried->distortion().inverseY()}};
        for (const auto& [source, moved] : components)
        {
            EXPECT_EQ(moved->order, source->order);
            for (std::size_t i = 0u; i < an::kSipCoefficientRowCount; ++i)
            {
                for (std::size_t j = 0u; j < an::kSipCoefficientRowCount; ++j)
                {
                    const double expected =
                        source->coefficients[i][j] * std::pow(mean, 1.0 - static_cast<double>(i + j));
                    EXPECT_NEAR(moved->coefficients[i][j], expected, 1e-12 * std::abs(expected))
                        << "the coefficient of u^" << i << " v^" << j;
                }
            }
        }
        // Near each corner the model bends a pixel's direction by one to three tenths of a second of arc; the carried
        // model still follows it to a few milliarcseconds.
        for (const an::PixelCoordinate at :
             {an::PixelCoordinate{.x = 0.5, .y = 0.5}, an::PixelCoordinate{.x = step.width - 0.5, .y = 0.5},
              an::PixelCoordinate{.x = 0.5, .y = step.height - 0.5},
              an::PixelCoordinate{.x = step.width - 0.5, .y = step.height - 0.5}})
        {
            const an::Result<an::SkyCoordinate> moved = carried->pixelToWorld(at);
            const an::Result<an::SkyCoordinate> original =
                truth->pixelToWorld({.x = at.x / scaleX, .y = at.y / scaleY});
            ASSERT_TRUE(moved.hasValue() && original.hasValue());
            EXPECT_NEAR(moved->rightAscensionDegrees, original->rightAscensionDegrees, 1e-6);
            EXPECT_NEAR(moved->declinationDegrees, original->declinationDegrees, 1e-6);
        }
        dropView(context(), target);
    }
}

// A filter-array change drops the solution together with every card that states it: Split CFA's positional planes and
// Merge CFA's mosaic carry no world-coordinate card and no solver's card, so nothing reads a solution off a grid it
// does not describe; the source's other cards stay.
TEST_F(GeometryProcessCase, SplitAndMergeCfaCarryNoWorldCoordinateCards)
{
    const auto solvedMetadata = [](std::uint32_t width, std::uint32_t height) -> std::optional<an::ImageMetadata> {
        constexpr double kScale = 2.4 / 3600.0;
        an::WcsSolutionRequest request;
        request.referencePixel = {.x = 0.5 * width, .y = 0.5 * height};
        request.referencePoint = {.rightAscensionDegrees = 83.82, .declinationDegrees = -5.39};
        request.linear = {.cd11 = -kScale, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScale};
        request.imageWidth = width;
        request.imageHeight = height;
        const an::Result<an::WcsSolution> solution = an::WcsSolution::create(request);
        an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "");
        an::Result<an::CodecCard> solver = an::CodecCard::create(an::CodecNamespace::Fits, "PLTSOLVR", "'ASTAP'", "");
        EXPECT_TRUE(solution.hasValue() && object.hasValue() && solver.hasValue());
        if (!solution.hasValue() || !object.hasValue() || !solver.hasValue())
        {
            return std::nullopt;
        }
        an::Result<an::CodecMetadata> cards = an::CodecMetadata::create(an::SourceEncoding{}, {*object, *solver});
        an::Result<an::CodecMetadata> solved =
            cards.hasValue() ? an::withWcsCards(*cards, *solution) : an::Result<an::CodecMetadata>(cards.status());
        an::Result<an::AstrometricSolution> summary = solution->toAstrometricSolution();
        EXPECT_TRUE(solved.hasValue() && summary.hasValue());
        if (!solved.hasValue() || !summary.hasValue())
        {
            return std::nullopt;
        }
        return an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, *summary, an::Provenance{},
                                         *solved, std::nullopt);
    };
    const auto expectNoSolution = [](const an::ImageMetadata& metadata, const an::ImageDescriptor& described) {
        for (const an::CodecCard& card : metadata.codec().cards())
        {
            EXPECT_FALSE(an::isWcsKeyword(card.keyword())) << "a world-coordinate card stays: " << card.keyword();
        }
        EXPECT_FALSE(hasCard(metadata, "PLTSOLVR")) << "the solver of a solution the image no longer has stays";
        EXPECT_FALSE(an::ui::readImageWcsSolution(metadata.codec().cards(), described.extent().width(),
                                                  described.extent().height())
                         .hasValue());
        EXPECT_FALSE(metadata.astrometry().present());
        EXPECT_TRUE(hasCard(metadata, "OBJECT")) << "the other cards stay";
    };

    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    const an::ImageExtent extent = mosaic->descriptor()->extent();
    const std::optional<an::ImageMetadata> mosaicMetadata = solvedMetadata(extent.width(), extent.height());
    ASSERT_TRUE(mosaicMetadata.has_value());
    std::optional<an::consumer::ImageAsset> solved = withMetadata(session(), *mosaic, *mosaicMetadata);
    ASSERT_TRUE(solved.has_value());
    ViewHandle target = assetView(std::move(*solved), "target");
    activate(context(), *target);
    std::vector<ViewHandle> planes;
    {
        WindowHandle split = openKept<SplitCFAProcess>(*target);
        const std::size_t before = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(context(), *split));
        ASSERT_EQ(context().madeViews.size(), before + 4u);
        for (std::size_t plane = 0u; plane < 4u; ++plane)
        {
            SCOPED_TRACE("plane " + std::to_string(plane));
            ImageViewControl* made = context().madeViews[before + plane];
            const std::optional<an::ImageMetadata> metadata = documentMetadata(made->document());
            const std::optional<an::ImageDescriptor> described = made->imageDescriptor();
            ASSERT_TRUE(metadata.has_value() && described.has_value());
            expectNoSolution(*metadata, *described);

            // The plane Merge CFA joins below states a solution of its own grid, which the mosaic cannot carry either.
            const std::optional<an::ImageMetadata> planeMetadata =
                solvedMetadata(described->extent().width(), described->extent().height());
            ASSERT_TRUE(planeMetadata.has_value());
            std::optional<an::consumer::ImageAsset> solvedPlane =
                withMetadata(session(), *made->document().asset(), *planeMetadata);
            ASSERT_TRUE(solvedPlane.has_value());
            planes.push_back(assetView(std::move(*solvedPlane), "cfa" + std::to_string(plane)));
            activate(context(), *planes.back());
        }
    }
    WindowHandle merge(new MergeCFAProcess());
    merge->setAttribute(Qt::WA_DeleteOnClose, false);
    for (std::size_t plane = 0u; plane < 4u; ++plane)
    {
        const std::string selector = "cfa" + std::to_string(plane) + "Selector";
        setComboText(*merge, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
    }
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *merge));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* merged = context().madeViews.back();
    const std::optional<an::ImageMetadata> metadata = documentMetadata(merged->document());
    const std::optional<an::ImageDescriptor> described = merged->imageDescriptor();
    ASSERT_TRUE(metadata.has_value() && described.has_value());
    expectNoSolution(*metadata, *described);
    merge.reset();
    for (ViewHandle& plane : planes)
    {
        dropView(context(), plane);
    }
    dropView(context(), target);
}

// Merge CFA asked to run with a plane it cannot find says what is missing.
TEST_F(GeometryProcessCase, MergeCfaWithAPlaneMissingSaysSo)
{
    WindowHandle merge(new MergeCFAProcess());
    merge->setAttribute(Qt::WA_DeleteOnClose, false);
    auto* buttons = widgetOf<QDialogButtonBox>(*merge, "buttonBox");
    ASSERT_NE(buttons, nullptr);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    const int errors = context().errorCount();
    // Ok is disabled while a plane is missing, so the press is the one a plane's view closing under it would answer.
    emit buttons->clicked(ok);
    ASSERT_TRUE(pumpUntil([this, errors]() { return context().errorCount() > errors; }));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("Missing images to assemble the CFA")))
        << context().errorTexts.back().toStdString();
    merge.reset();
}

// Filter-array Integer Resample follows its target too: when the mosaic changes size while the window is open, the
// original and target readouts follow the new size.
TEST_F(GeometryProcessCase, FilterArrayResampleFollowsATargetThatChangesSize)
{
    ViewHandle target = sceneView(session(), "mosaic-rggb-uint16");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<an::ImageDescriptor> source = target->imageDescriptor();
    ASSERT_TRUE(source.has_value());
    WindowHandle window = openKept<CFAIntegerResampleProcess>(*target);
    auto* originalWidth = widgetOf<QSpinBox>(*window, "originalWidth");
    auto* targetWidth = widgetOf<QSpinBox>(*window, "targetWidth");
    auto* targetHeight = widgetOf<QSpinBox>(*window, "targetHeight");
    ASSERT_TRUE(originalWidth != nullptr && targetWidth != nullptr && targetHeight != nullptr);
    const int width = static_cast<int>(source->extent().width()) - 8;
    const int height = static_cast<int>(source->extent().height()) - 4;
    {
        WindowHandle crop = openKept<CropProcess>(*target);
        setInt(*crop, "targetWidth", width);
        setInt(*crop, "targetHeight", height);
        ASSERT_TRUE(applyAndSettle(context(), *crop));
    }
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(pumpUntil([originalWidth, width]() { return originalWidth->value() == width; }));
    EXPECT_EQ(targetWidth->value(), width / 2);
    EXPECT_EQ(targetHeight->value(), height / 2);
    window.reset();
    dropView(context(), target);
}

// Integer Resample's readouts hold any size: the image's own and the size the run makes, so the step the history
// records names the size the run writes. They stopped at 100,000, which an image read wider or an upsample past it
// overran.
TEST_F(GeometryProcessCase, IntegerResampleShowsAndRecordsSizesPastAHundredThousand)
{
    constexpr std::uint32_t kWide = 100003u;
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWide, 2u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.001f * static_cast<float>(x % 997u); });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset), "wide");
    activate(context(), *target);
    WindowHandle window = openKept<IntegerResampleProcess>(*target);
    auto* originalWidth = widgetOf<QSpinBox>(*window, "originalWidth");
    auto* targetWidth = widgetOf<QSpinBox>(*window, "targetWidth");
    auto* upsample = window->findChild<QRadioButton*>(QStringLiteral("UpsampleRadioButton"));
    ASSERT_TRUE(originalWidth != nullptr && targetWidth != nullptr && upsample != nullptr);
    EXPECT_EQ(originalWidth->value(), static_cast<int>(kWide));
    EXPECT_EQ(targetWidth->value(), static_cast<int>(kWide / 2u)) << "a binning by two of the image's own width";

    upsample->click();
    setInt(*window, "resampleFactor", 2);
    EXPECT_EQ(targetWidth->value(), static_cast<int>(kWide * 2u));
    const IntegerResampleProcess::Settings asked = static_cast<IntegerResampleProcess&>(*window).settings();
    EXPECT_EQ(asked.width, kWide * 2u);
    EXPECT_EQ(IntegerResampleProcess::stepDescription(asked),
              "Resample factor: 2X - New size: " + std::to_string(kWide * 2u) + "x4");
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(target->imageDescriptor()->extent().width(), kWide * 2u);
    window.reset();
    dropView(context(), target);
}

// Integer Resample follows its target: when the image changes size while the window is open, the original and target
// readouts, both size estimates, Ok and the step the history records follow the new size.
TEST_F(GeometryProcessCase, IntegerResampleFollowsATargetThatChangesSize)
{
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<an::ImageDescriptor> source = target->imageDescriptor();
    ASSERT_TRUE(source.has_value());
    WindowHandle window = openKept<IntegerResampleProcess>(*target);
    auto* originalWidth = widgetOf<QSpinBox>(*window, "originalWidth");
    auto* originalHeight = widgetOf<QSpinBox>(*window, "originalHeight");
    auto* targetWidth = widgetOf<QSpinBox>(*window, "targetWidth");
    auto* targetHeight = widgetOf<QSpinBox>(*window, "targetHeight");
    auto* currentSize = widgetOf<QLineEdit>(*window, "currentSize");
    auto* newSize = widgetOf<QLineEdit>(*window, "newSize");
    auto* buttons = widgetOf<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_TRUE(originalWidth != nullptr && originalHeight != nullptr && targetWidth != nullptr &&
                targetHeight != nullptr && currentSize != nullptr && newSize != nullptr && buttons != nullptr);
    ASSERT_EQ(targetWidth->value(), static_cast<int>(source->extent().width() / 2u));

    const int width = static_cast<int>(source->extent().width()) - 4;
    const int height = static_cast<int>(source->extent().height()) - 2;
    {
        WindowHandle crop = openKept<CropProcess>(*target);
        setInt(*crop, "targetWidth", width);
        setInt(*crop, "targetHeight", height);
        ASSERT_TRUE(applyAndSettle(context(), *crop));
    }
    ASSERT_TRUE(settleView(*target));
    ASSERT_EQ(target->imageDescriptor()->extent().width(), static_cast<std::uint32_t>(width));
    ASSERT_TRUE(pumpUntil([originalWidth, width]() { return originalWidth->value() == width; }));
    EXPECT_EQ(originalHeight->value(), height);
    EXPECT_EQ(targetWidth->value(), width / 2);
    EXPECT_EQ(targetHeight->value(), height / 2);
    const double pixelBytes = static_cast<double>(source->extent().channelCount()) *
                              static_cast<double>(an::sampleTypeSizeBytes(source->sampleType()));
    EXPECT_EQ(currentSize->text(), an::ui::processes::sizeText(static_cast<double>(width) * height * pixelBytes));
    EXPECT_EQ(newSize->text(), an::ui::processes::sizeText(static_cast<double>(width / 2) * (height / 2) * pixelBytes));
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    EXPECT_EQ(IntegerResampleProcess::stepDescription(static_cast<IntegerResampleProcess&>(*window).settings()),
              "Binning factor: 2X - New size: " + std::to_string(width / 2) + "x" + std::to_string(height / 2) +
                  " - Binning method: Average");
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(target->imageDescriptor()->extent().width(), static_cast<std::uint32_t>(width / 2));
    EXPECT_EQ(target->imageDescriptor()->extent().height(), static_cast<std::uint32_t>(height / 2));
    window.reset();
    dropView(context(), target);
}

// A resize whose scales round so that the aspect it is planned with does not come back exactly still states the
// source's square pixels, so the image it makes can be rotated.
TEST_F(GeometryProcessCase, AnImageResizedToAnySizeCanBeRotated)
{
    struct Case
    {
        int width;
        int height;
    };
    for (const Case& item : {Case{10, 11}, Case{3, 3}, Case{7, 13}})
    {
        SCOPED_TRACE(std::to_string(item.width) + "x" + std::to_string(item.height));
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
            session(), 60u, 40u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return 0.01f * static_cast<float>((x * 3u + y) % 50u);
            });
        ASSERT_TRUE(asset.has_value());
        ViewHandle target = assetView(std::move(*asset), "target");
        activate(context(), *target);
        {
            WindowHandle window = openKept<ResizeProcess>(*target);
            setChecked(*window, "keepAspectRatio", false);
            setInt(*window, "targetWidth", item.width);
            setInt(*window, "targetHeight", item.height);
            ASSERT_TRUE(applyAndSettle(context(), *window));
        }
        const std::optional<an::ImageDescriptor> resized = target->imageDescriptor();
        ASSERT_TRUE(resized.has_value());
        ASSERT_EQ(resized->extent().width(), static_cast<std::uint32_t>(item.width));
        ASSERT_EQ(resized->extent().height(), static_cast<std::uint32_t>(item.height));
        EXPECT_EQ(resized->pixelAspectRatio(), 1.0);
        const std::optional<fixtures::FixtureSamples> before = documentSamples(target->document());
        ASSERT_TRUE(before.has_value());
        const int errors = context().errorCount();
        {
            WindowHandle window = openKept<RotateProcess>(*target);
            setDouble(*window, "rotationAngle", 30.0);
            ASSERT_TRUE(applyAndSettle(context(), *window));
        }
        EXPECT_EQ(context().errorCount(), errors) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> rotated = documentSamples(target->document());
        ASSERT_TRUE(rotated.has_value());
        EXPECT_NE(rotated->digest(), before->digest()) << "the rotation was applied";
        dropView(context(), target);
    }
}
