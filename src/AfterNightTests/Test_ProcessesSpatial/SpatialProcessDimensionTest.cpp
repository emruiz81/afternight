// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every spatial and noise process held to each behavior its dialog owes: launched through the registry and attached
/// to its view, previewed with the Apply's own answer, previewed over an area as the crop it is, weighed by a mask,
/// run over every frame of a sequence, recorded in the history, keeping the image's cards and stating the state it
/// leaves, opening with its own settings every time, stopped by its run's stop, and running over every resident
/// representation.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"
#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ProcessRegistry.h"
#include "controls/SequenceContainers.h"
#include "processes/GmicProcessSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QAbstractButton>
#include <QGroupBox>
#include <QLabel>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;
using an::tests::processes::shared::openedProcessWindows;
using an::tests::processes::shared::RegistrySnapshot;

/// The largest absolute difference between two images of one shape; infinity for two shapes.
[[nodiscard]] double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double a = left.value(x, y, channel);
                const double b = right.value(x, y, channel);
                if (std::isnan(a) != std::isnan(b))
                {
                    return std::numeric_limits<double>::infinity();
                }
                if (!std::isnan(a))
                {
                    largest = (std::max)(largest, std::abs(a - b));
                }
            }
        }
    }
    return largest;
}

/// An asset's samples in the fixture layer's terms; fails the case when they cannot be read.
[[nodiscard]] std::optional<fixtures::FixtureSamples> assetSamples(const an::consumer::ImageAsset& asset)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readAssetSamples(asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

/// A grey or colour Float32 document of the stars fixture's samples, carrying an `OBJECT` card when asked.
[[nodiscard]] std::optional<an::ui::ImageDocument> starsDocument(const an::consumer::EngineSession& session,
                                                                 bool colour, bool withCard = false)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(colour ? "stars-rgb-float32" : "stars-mono-float32");
    EXPECT_NE(image, nullptr);
    if (image == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    if (!withCard)
    {
        return fixtureDocument(session, image->name);
    }
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::Float32, samples->width(), samples->height(),
                                         colour ? an::tests::viewer::Channels::Rgb : an::tests::viewer::Channels::Grey);
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M42'", "target");
    an::Result<an::CodecMetadata> codec = card.hasValue() ? an::CodecMetadata::create(an::SourceEncoding{}, {*card})
                                                          : an::Result<an::CodecMetadata>(card.status());
    EXPECT_TRUE(described.has_value() && codec.hasValue());
    if (!described.has_value() || !codec.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> made = an::Image<float>::create(
        hold->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, *codec, std::nullopt));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = made->write();
        for (std::uint32_t channel = 0u; channel < samples->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < samples->height(); ++y)
            {
                const auto row = access->view().row(channel, y);
                for (std::uint32_t x = 0u; x < samples->width(); ++x)
                {
                    (*row)[x] = static_cast<float>(samples->value(x, y, channel));
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*made)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document =
        an::ui::ImageDocument::create(std::move(*asset), an::ui::DocumentNames{.displayName = QStringLiteral("stars")});
    return document.hasValue() ? std::optional<an::ui::ImageDocument>(std::move(*document)) : std::nullopt;
}

/// The name a parameterized case is printed under: the action.
struct ActionName
{
    std::string operator()(const ::testing::TestParamInfo<std::string>& info) const
    {
        return info.param;
    }
};

[[nodiscard]] std::vector<std::string> everyAction()
{
    std::vector<std::string> actions;
    for (const SpatialProcessSpec& spec : spatialProcesses())
    {
        actions.push_back(spec.action);
    }
    return actions;
}

/// One case over one process: its spec and an installed application context.
class SpatialProcessTest : public ::testing::TestWithParam<std::string>
{
protected:
    void SetUp() override
    {
        m_spec = spatialProcess(GetParam());
        ASSERT_NE(m_spec, nullptr);
        if (!runsHere(*m_spec))
        {
            GTEST_SKIP() << "this build carries no G'MIC library; GmicProcessTest holds the refusal";
        }
        m_context = openProcessContext();
        ASSERT_NE(m_context, nullptr);
        m_installed = std::make_unique<InstalledContext>(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
    }

    [[nodiscard]] const SpatialProcessSpec& spec() const
    {
        return *m_spec;
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

private:
    const SpatialProcessSpec* m_spec = nullptr;
    std::unique_ptr<ProcessTestContext> m_context;
    std::unique_ptr<InstalledContext> m_installed;
};

} // namespace

// The registry's descriptor of the action carries what the class says of itself; a launch through the registry opens
// that class on the active view; the window opened as the entry point opens it attaches to the view under its name.
TEST_P(SpatialProcessTest, LaunchOpensTheClassOnItsViewUnderItsName)
{
    const RegistrySnapshot snapshot;
    const an::ui::process_registry::ProcessDescriptor* descriptor = nullptr;
    const std::vector<an::ui::process_registry::ProcessDescriptor> descriptors =
        an::ui::process_registry::ProcessRegistry::instance().all();
    for (const an::ui::process_registry::ProcessDescriptor& candidate : descriptors)
    {
        if (candidate.action_object_name == spec().action)
        {
            descriptor = &candidate;
        }
    }
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->process_name, spec().name);
    EXPECT_EQ(descriptor->requires_exclusive_access, spec().exclusive);
    EXPECT_EQ(descriptor->launch_mode, an::ui::process_registry::ProcessLaunchMode::SingleImage);

    std::optional<an::ui::ImageDocument> document = starsDocument(session(), true);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    context().addViewControl(&target.view());
    context().setActiveImageViewControl(&target.view());
    ASSERT_TRUE(static_cast<bool>(descriptor->launch_single_image));
    descriptor->launch_single_image(&target.view());
    ASSERT_EQ(openedProcessWindows().size(), 1u);
    EXPECT_EQ(openedProcessWindows()[0].className, spec().className);
    EXPECT_EQ(openedProcessWindows()[0].view, &target.view());

    const ProbedWindow window = spec().open(target.view());
    EXPECT_EQ(window->getProcessName(), spec().name);
    EXPECT_TRUE(target.view().hasProcessAttached(spec().name));
    EXPECT_EQ(window->requiresExclusiveAccess(), spec().exclusive);
}

// A preview shows what an Apply with the same settings publishes.
TEST_P(SpatialProcessTest, APreviewShowsTheApplysAnswer)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), false);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const ProbedWindow window = spec().open(target.view());
    spec().configure(*window);
    setChecked(*window, "preview", true);
    ASSERT_TRUE(settleRun(*window, target.view()));
    ImageViewControl* preview = window->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(pumpUntilSettled([preview]() { return preview->isWorkIdle(); }));
    EXPECT_GE(window->runTelemetry().previewsCommitted, 1u);
    EXPECT_EQ(window->runTelemetry().previewsFailed, 0u) << context().errorTexts.join("; ").toStdString();
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(preview->document());
    ASSERT_TRUE(shown.has_value());

    const an::Result<an::consumer::ImageAsset> previewed =
        runProbeBody(*window.probe, session(), target.view().document(),
                     an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Preview});
    ASSERT_TRUE(previewed.hasValue()) << previewed.status().message();
    const std::optional<fixtures::FixtureSamples> expected = assetSamples(*previewed);
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(largestDifference(*shown, *expected), 0.0);

    const an::Result<an::consumer::ImageAsset> applied = runProbeBody(
        *window.probe, session(), target.view().document(), an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    const std::optional<fixtures::FixtureSamples> apply = assetSamples(*applied);
    ASSERT_TRUE(apply.has_value());
    EXPECT_EQ(largestDifference(*shown, *apply), 0.0);
}

// A preview over an area runs over the area's crop and is shown over the whole image: inside the area the crop's own
// answer, outside it the image as it is.
TEST_P(SpatialProcessTest, APreviewAreaIsTheCropsAnswerOverTheWholeImage)
{
    // The wide stars, so an area holds a whole search window of the patch denoiser at the noise level it is set to.
    std::optional<an::ui::ImageDocument> document = fixtureDocument(session(), "stars-mono-float32-wide");
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const ProbedWindow window = spec().open(target.view());
    spec().configure(*window);
    setChecked(*window, "preview", true);
    ASSERT_TRUE(settleRun(*window, target.view()));
    window.probe->usePreviewArea(QSize(64, 48));
    ASSERT_TRUE(settleRun(*window, target.view()));
    ImageViewControl* preview = window->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(pumpUntilSettled([preview]() { return preview->isWorkIdle(); }));
    const QRect area = window->previewArea();
    ASSERT_EQ(area, QRect(24, 28, 64, 48));

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = target.view().document().read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::consumer::ImageAsset> crop = an::ui::cropProcessRegion(
        session(), scope->engine(), scope->execution(), lease->image(), lease->validity(), area);
    ASSERT_TRUE(crop.hasValue()) << crop.status().message();
    scope.reset();
    an::Result<an::ui::ImageDocument> cropped = an::ui::ImageDocument::create(std::move(*crop), {});
    ASSERT_TRUE(cropped.hasValue());
    const an::Result<an::consumer::ImageAsset> answered =
        runProbeBody(*window.probe, session(), *cropped, an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Preview});
    ASSERT_TRUE(answered.hasValue()) << answered.status().message();
    const std::optional<fixtures::FixtureSamples> cropAnswer = assetSamples(*answered);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(preview->document());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(cropAnswer.has_value() && shown.has_value() && source.has_value());
    for (std::uint32_t y = 0u; y < shown->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < shown->width(); ++x)
        {
            const bool inside = area.contains(static_cast<int>(x), static_cast<int>(y));
            const double expected = inside ? cropAnswer->value(x - 24u, y - 28u, 0u) : source->value(x, y, 0u);
            ASSERT_EQ(shown->value(x, y, 0u), expected) << x << "," << y << (inside ? " inside" : " outside");
        }
    }
}

// An Apply under the target's mask is the result where the mask is one and the image where it is zero.
TEST_P(SpatialProcessTest, AMaskKeepsTheImageWhereItIsZeroAndTheResultWhereItIsOne)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), false);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), 32u, 24u, [](std::uint32_t x, std::uint32_t) { return x < 16u ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    const TargetView mask(context(), "mask", std::move(*maskDocument));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target.view().document());
    ASSERT_TRUE(source.has_value());

    const ProbedWindow window = spec().open(target.view());
    setChecked(*window, "preview", false);
    spec().configure(*window);
    const an::Result<an::consumer::ImageAsset> unmasked = runProbeBody(
        *window.probe, session(), target.view().document(), an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
    ASSERT_TRUE(unmasked.hasValue()) << unmasked.status().message();
    const std::optional<fixtures::FixtureSamples> result = assetSamples(*unmasked);
    ASSERT_TRUE(result.has_value());

    target.view().setCurrentMask(&mask.view());
    target.view().setEnableMask(true);
    ASSERT_TRUE(target.view().isMasked());
    window->applyProcess();
    ASSERT_TRUE(settleRun(*window, target.view()));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u) << context().errorTexts.join("; ").toStdString();
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(target.view().document());
    ASSERT_TRUE(shown.has_value());
    for (std::uint32_t y = 0u; y < 24u; ++y)
    {
        for (std::uint32_t x = 0u; x < 32u; ++x)
        {
            const double expected = x < 16u ? source->value(x, y, 0u) : result->value(x, y, 0u);
            ASSERT_NEAR(shown->value(x, y, 0u), expected, 1e-6) << x << "," << y;
        }
    }
    target.view().removeCurrentMask();
}

// A run over a sequence writes every frame as the process's answer for that frame, and the view shows the sequence
// written.
TEST_P(SpatialProcessTest, ASequenceRunWritesEveryFrameAsItsOwnAnswer)
{
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    const QString path = recordings.path() + QStringLiteral("/frames.ser");
    const std::optional<fixtures::FixtureSamples> stars = [&]() -> std::optional<fixtures::FixtureSamples> {
        std::string error;
        return fixtures::materializeFixture(*fixtures::fixtureNamed("stars-mono-float32"), error);
    }();
    ASSERT_TRUE(stars.has_value());
    an::tests::viewer::writeSerFile(session(), path, 32u, 24u, 3u,
                                    [&](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
                                        return static_cast<float>(stars->value(x, y, 0u) * (0.5 + 0.25 * frame));
                                    });
    std::optional<an::ui::ImageDocument> shown =
        greyDocument(session(), 32u, 24u, [](std::uint32_t, std::uint32_t) { return 0.0f; });
    ASSERT_TRUE(shown.has_value());
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 3u;
    sequence.currentFrame = 0u;
    shown->setSequence(sequence);
    ViewHandle view(new ImageViewControl());
    view->initialize("sequence", ViewControlMode::standard);
    view->resize(640, 480);
    view->setSequenceDocument(std::move(*shown), false);
    view->initializeNewImageHistory("Sequence opened");
    ASSERT_TRUE(pumpUntilSettled([&view]() { return view->isWorkIdle(); }));

    std::vector<an::consumer::AnyImage> frames;
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        an::Result<an::ui::OpenedSequence> opened =
            an::ui::openSequenceSource(scope->engine(), scope->execution(), path, an::SequenceFormat::Ser);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        for (;;)
        {
            an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
                an::ui::readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
            ASSERT_TRUE(read.hasValue()) << read.status().message();
            if (!read->has_value())
            {
                break;
            }
            frames.push_back(std::move((*read)->second));
        }
    }
    ASSERT_EQ(frames.size(), 3u);

    {
        const ProbedWindow window = spec().open(*view);
        setChecked(*window, "preview", false);
        spec().configure(*window);
        // What each frame's run answers, frame by frame, over the frame as the sequence run reads it.
        std::vector<fixtures::FixtureSamples> expected;
        const std::uint64_t frameCount = frames.size();
        for (std::uint64_t frame = 0u; frame < frameCount; ++frame)
        {
            an::Result<an::consumer::ImageAsset> asset =
                an::consumer::ImageAsset::adopt(session(), std::move(frames[frame]));
            ASSERT_TRUE(asset.hasValue()) << asset.status().message();
            an::Result<an::ui::ImageDocument> frameDocument = an::ui::ImageDocument::create(std::move(*asset), {});
            ASSERT_TRUE(frameDocument.hasValue());
            const an::Result<an::consumer::ImageAsset> answered = runProbeBody(
                *window.probe, session(), *frameDocument,
                an::ui::ProcessRun{
                    .kind = an::ui::ProcessRunKind::SequenceFrame, .frame = frame, .frameCount = frameCount});
            ASSERT_TRUE(answered.hasValue()) << answered.status().message();
            std::optional<fixtures::FixtureSamples> samples = assetSamples(*answered);
            ASSERT_TRUE(samples.has_value());
            expected.push_back(std::move(*samples));
        }

        window->applyProcess();
        ASSERT_TRUE(settleRun(*window, *view));
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u) << context().errorTexts.join("; ").toStdString();
        const std::optional<an::ui::DocumentSequence> written = view->document().sequence();
        ASSERT_TRUE(written.has_value());
        EXPECT_NE(written->sourcePath, path);

        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        an::Result<an::ui::OpenedSequence> opened = an::ui::openSequenceSource(
            scope->engine(), scope->execution(), written->sourcePath, an::SequenceFormat::Ser);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        for (std::size_t frame = 0u; frame < expected.size(); ++frame)
        {
            an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
                an::ui::readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
            ASSERT_TRUE(read.hasValue() && read->has_value());
            std::string error;
            std::optional<fixtures::FixtureSamples> samples =
                fixtures::engine::readImageSamples((*read)->second, error);
            ASSERT_TRUE(samples.has_value()) << error;
            // The container holds the frame's own representation, so the frame written is the answer itself.
            EXPECT_EQ(largestDifference(*samples, expected[frame]), 0.0) << "frame " << frame;
        }
    }
}

// An Apply is recorded under the process's name before the view shows it, and undoing it brings the image back.
TEST_P(SpatialProcessTest, AnApplyIsRecordedUnderTheProcessesNameAndUndoes)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), true);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target.view().document());
    ASSERT_TRUE(before.has_value());
    const ProbedWindow window = spec().open(target.view());
    setChecked(*window, "preview", false);
    spec().configure(*window);
    const an::ui::ProcessStep step = window.probe->step();
    EXPECT_EQ(step.name, spec().name);
    EXPECT_FALSE(step.description.empty());

    window->applyProcess();
    ASSERT_TRUE(settleRun(*window, target.view()));
    ASSERT_EQ(window->runTelemetry().runsCommitted, 1u) << context().errorTexts.join("; ").toStdString();
    ASSERT_NE(target.view().viewHistory(), nullptr);
    EXPECT_EQ(target.view().viewHistory()->previousStepName(), spec().name);
    const std::optional<fixtures::FixtureSamples> after = documentSamples(target.view().document());
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(largestDifference(*before, *after), 0.0) << "the setting changes the image";

    an::ui::IViewHistory* history = target.view().viewHistory();
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntilSettled([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(pumpUntilSettled([&target]() { return target.view().isWorkIdle(); }));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(target.view().document());
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(largestDifference(*before, *undone), 0.0);
}

// The result keeps the image's cards; a process that leaves the samples nonlinear says so in the processing-state
// cards, and every other leaves the state as it was.
TEST_P(SpatialProcessTest, TheResultKeepsTheCardsAndStatesWhatItLeaves)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), false, true);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const ProbedWindow window = spec().open(target.view());
    setChecked(*window, "preview", false);
    spec().configure(*window);
    window->applyProcess();
    ASSERT_TRUE(settleRun(*window, target.view()));
    ASSERT_EQ(window->runTelemetry().runsCommitted, 1u) << context().errorTexts.join("; ").toStdString();

    an::Result<an::consumer::AssetReadLease> lease = target.view().document().read();
    ASSERT_TRUE(lease.hasValue());
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    const auto cards = metadata.codec().cards();
    EXPECT_TRUE(std::ranges::any_of(cards, [](const an::CodecCard& card) {
        return card.keyword() == "OBJECT" && card.value() == "'M42'";
    })) << "the image's own card is kept";
    const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata.codec());
    if (!spec().nonlinearOperation.empty())
    {
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.operation, spec().nonlinearOperation);
    }
    else
    {
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Unknown);
    }
}

// The history step names each figure as `Name: value`, a colon after each name and a comma between figures, and each
// name is one the dialog labels, so Image History reads as the dialog did.
TEST_P(SpatialProcessTest, TheStepNamesItsFiguresAsTheDialogLabelsThem)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), false);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const ProbedWindow window = spec().open(target.view());
    spec().configure(*window);
    const std::string description = window.probe->step().description;
    EXPECT_EQ(description.find(';'), std::string::npos) << "a figure separated by a semicolon: " << description;

    // What the dialog labels, as letters alone, so a label's case, spacing and punctuation do not decide.
    const auto lettersOf = [](const QString& text) {
        QString letters;
        for (const QChar character : text)
        {
            if (character.isLetter())
            {
                letters.append(character.toLower());
            }
        }
        return letters;
    };
    QStringList labels;
    for (const QLabel* label : window->findChildren<QLabel*>())
    {
        labels << lettersOf(label->text());
    }
    for (const QAbstractButton* button : window->findChildren<QAbstractButton*>())
    {
        labels << lettersOf(button->text());
    }
    for (const QGroupBox* group : window->findChildren<QGroupBox*>())
    {
        labels << lettersOf(group->title());
    }
    const QString step = QString::fromStdString(description);
    for (const QString& figure : step.split(QStringLiteral(", ")))
    {
        const qsizetype colon = figure.indexOf(QLatin1Char(':'));
        if (colon <= 0)
        {
            continue;
        }
        const QString name = lettersOf(figure.left(colon));
        EXPECT_TRUE(std::ranges::any_of(labels, [&name](const QString& label) { return label.contains(name); }))
            << "the step names '" << figure.left(colon).toStdString()
            << "', which the dialog does not label: " << description;
    }
}

// The dialog remembers no setting: closed and opened again it holds what it first opened with.
TEST_P(SpatialProcessTest, EveryOpeningStartsFromTheDialogsOwnSettings)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), false);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    std::string opened;
    {
        const ProbedWindow window = spec().open(target.view());
        opened = window.probe->step().description;
        spec().configure(*window);
        EXPECT_NE(window.probe->step().description, opened) << "the setting is one the step names";
        window->close();
        ASSERT_TRUE(settleRun(*window, target.view()));
    }
    const ProbedWindow again = spec().open(target.view());
    EXPECT_EQ(again.probe->step().description, opened);
}

// A run whose stop was asked for before its body reaches the engine ends cancelled, with nothing to publish.
TEST_P(SpatialProcessTest, ARunsStopReachesTheEngineCall)
{
    std::optional<an::ui::ImageDocument> document = starsDocument(session(), false);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const ProbedWindow window = spec().open(target.view());
    setChecked(*window, "preview", false);
    spec().configure(*window);
    const an::Result<an::consumer::ImageAsset> stopped =
        runProbeBody(*window.probe, session(), target.view().document(),
                     an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply}, true);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled) << stopped.status().message();
}

// Every resident representation is processed in its own representation, the shape kept.
TEST_P(SpatialProcessTest, EveryRepresentationIsProcessedInItself)
{
    const std::optional<fixtures::FixtureSamples> stars = [&]() -> std::optional<fixtures::FixtureSamples> {
        std::string error;
        return fixtures::materializeFixture(*fixtures::fixtureNamed("stars-mono-float32"), error);
    }();
    ASSERT_TRUE(stars.has_value());
    const auto build = [&]<typename SampleT>() -> std::optional<an::ui::ImageDocument> {
        return makeDocument<SampleT>(
            session(), 32u, 24u, Channels::Grey, [&](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                const double fraction = stars->value(x, y, 0u);
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<SampleT>(fraction);
                }
                else
                {
                    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::lowest)());
                    const double span = static_cast<double>((std::numeric_limits<SampleT>::max)()) - lowest;
                    return static_cast<SampleT>(std::llround(lowest + fraction * span));
                }
            });
    };
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        std::optional<an::ui::ImageDocument> document;
        (void)an::dispatchSampleType(type,
                                     [&]<typename SampleT>() { document = build.template operator()<SampleT>(); });
        ASSERT_TRUE(document.has_value());
        const TargetView target(context(), "stars", std::move(*document));
        const ProbedWindow window = spec().open(target.view());
        setChecked(*window, "preview", false);
        spec().configure(*window);
        const an::Result<an::consumer::ImageAsset> answered =
            runProbeBody(*window.probe, session(), target.view().document(),
                         an::ui::ProcessRun{.kind = an::ui::ProcessRunKind::Apply});
        ASSERT_TRUE(answered.hasValue()) << answered.status().message();
        const std::optional<an::ImageDescriptor> described = answered->descriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->sampleType(), type);
        EXPECT_EQ(described->extent().width(), 32u);
        EXPECT_EQ(described->extent().height(), 24u);
    }
}

// A sample that is not a finite number, in an image that states every sample valid: a G'MIC process refuses the image
// by name under the framework's error and changes nothing, a process that ranks its neighbours ranks the finite ones
// and leaves the sample where it is, and every other publishes what its arithmetic makes of it.
TEST_P(SpatialProcessTest, ANonFiniteSampleIsReadAsTheProcessReadsIt)
{
    constexpr std::uint32_t kWidth = 48u;
    constexpr std::uint32_t kHeight = 40u;
    const auto value = [](std::uint32_t x, std::uint32_t y) {
        if (x == 20u && y == 20u)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        if (x == 30u && y == 10u)
        {
            return std::numeric_limits<float>::infinity();
        }
        return 0.01f * static_cast<float>((x * 7u + y * 13u) % 50u);
    };
    std::optional<an::ui::ImageDocument> document = greyDocument(session(), kWidth, kHeight, value);
    ASSERT_TRUE(document.has_value());
    const TargetView target(context(), "stars", std::move(*document));
    const std::optional<fixtures::FixtureSamples> before = documentSamples(target.view().document());
    ASSERT_TRUE(before.has_value());
    const ProbedWindow window = spec().open(target.view());
    setChecked(*window, "preview", false);
    spec().configure(*window);
    window->applyProcess();
    ASSERT_TRUE(settleRun(*window, target.view()));
    const std::optional<fixtures::FixtureSamples> after = documentSamples(target.view().document());
    ASSERT_TRUE(after.has_value());

    switch (spec().nonFinite)
    {
    case NonFiniteReading::Refused:
        EXPECT_EQ(window->runTelemetry().runsFailed, 1u);
        EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
        ASSERT_FALSE(context().errorTexts.isEmpty());
        EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("not a finite number")))
            << context().errorTexts.back().toStdString();
        EXPECT_EQ(largestDifference(*before, *after), 0.0);
        EXPECT_NE(target.view().viewHistory()->previousStepName(), spec().name);
        break;
    case NonFiniteReading::Excluded:
        ASSERT_EQ(window->runTelemetry().runsCommitted, 1u) << context().errorTexts.join("; ").toStdString();
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const float stated = value(x, y);
                if (std::isfinite(stated))
                {
                    ASSERT_TRUE(std::isfinite(after->value(x, y, 0u))) << x << "," << y;
                }
                else if (std::isnan(stated))
                {
                    ASSERT_TRUE(std::isnan(after->value(x, y, 0u))) << x << "," << y;
                }
                else
                {
                    ASSERT_EQ(after->value(x, y, 0u), static_cast<double>(stated)) << x << "," << y;
                }
            }
        }
        break;
    case NonFiniteReading::Computed:
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u) << context().errorTexts.join("; ").toStdString();
        EXPECT_EQ(target.view().viewHistory()->previousStepName(), spec().name);
        break;
    }
}

INSTANTIATE_TEST_SUITE_P(EverySpatialProcess, SpatialProcessTest, ::testing::ValuesIn(everyAction()), ActionName{});
