// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What only one of the mask, star and background processes does: the background extraction's sample list and its
/// grid, placed on a worker; the images the background processes, the dark structure enhancement, the multiscale
/// transform and the star reduction open beside their target; the star reduction's starless image and its protection
/// mask; the star mask's one detector over a field and a picture too small for its search; and the mask drawing's
/// import, export and edits, which land through its queue.

#include "Test_ProcessesMasks/MasksTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"
#include "fixtures/harness/QObjectCensus.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingToolbar.h"
#include "processes/AutoGradientRemovalProcess.h"
#include "processes/BackgroundExtractionProcess.h"
#include "processes/MaskDrawingProcess.h"
#include "processes/MultiscaleComposition.h"

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QPushButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::masks;
using an::tests::viewer::Channels;

/// One process's window, for what only that process does.
class MasksProcessCase : public ::testing::Test
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

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A grey Float32 view of `value(x, y)`.
    [[nodiscard]] ViewHandle greyView(std::uint32_t width, std::uint32_t height,
                                      const std::function<float(std::uint32_t, std::uint32_t)>& value,
                                      const std::string& name = "target") const
    {
        std::optional<an::ui::ImageDocument> document = greyDocument(session(), width, height, value);
        EXPECT_TRUE(document.has_value());
        return document.has_value() ? makeTargetView(name, std::move(*document)) : ViewHandle();
    }

    /// The names of the views made since a count of them.
    [[nodiscard]] std::vector<std::string> madeNames(std::size_t before) const
    {
        std::vector<std::string> names;
        for (ImageViewControl* view : viewsMadeSince(context(), before))
        {
            names.push_back(view->getUniqueName());
        }
        return names;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// A process's window over a session one operation wide, where work that holds the only thread while it waits for an
/// image's writer stops everything.
class MasksProcessCaseAtWidthOne : public MasksProcessCase
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

/// A smooth grey background with one bright square in it, the size of a few samples' boxes.
[[nodiscard]] float backgroundWithASquare(std::uint32_t x, std::uint32_t y)
{
    const bool inSquare = x >= 120u && x < 180u && y >= 80u && y < 140u;
    return inSquare ? 0.9f : static_cast<float>(0.1 + 0.0004 * x + 0.0002 * y);
}

[[nodiscard]] QAbstractButton* applyButtonOf(QWidget& window)
{
    auto* buttons = window.findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    EXPECT_NE(buttons, nullptr);
    return buttons != nullptr ? buttons->button(QDialogButtonBox::Apply) : nullptr;
}

/// Asks a background extraction window for its grid and waits for it to land.
[[nodiscard]] bool generateGrid(ImageRTPreviewControl& window, int perLine)
{
    setInt(window, "samplesPerLineValue", perLine);
    setDouble(window, "gridToleranceValue", 2.0);
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(&window);
    auto* generate = window.findChild<QPushButton*>(QStringLiteral("generateSamplesButton"));
    if (extraction == nullptr || generate == nullptr)
    {
        return false;
    }
    generate->click();
    return pumpUntil([extraction]() { return !extraction->isGeneratingSamples(); });
}

/// A view of a fixture scene in Float32 whose lineage holds one step of its own, with one sample of its first channel
/// holding no number when asked, so a case can tell an image derived from it.
[[nodiscard]] ViewHandle sceneWithLineage(const an::consumer::EngineSession& session, std::string_view scene,
                                          bool withNaN = false)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr);
    if (image == nullptr)
    {
        return {};
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!samples.has_value() || !scope.has_value())
    {
        return {};
    }
    const Channels channels = samples->channelCount() == 1u ? Channels::Grey : Channels::Rgb;
    const std::optional<an::ImageDescriptor> described = an::tests::viewer::describeImage(
        scope->engine(), an::SampleType::Float32, samples->width(), samples->height(), channels);
    an::Result<an::ProvenanceEntry> step = an::ProvenanceEntry::create("an::sourceStep", "the scene's own step");
    EXPECT_TRUE(described.has_value() && step.hasValue());
    if (!described.has_value() || !step.hasValue())
    {
        return {};
    }
    an::Result<an::Provenance> lineage = an::Provenance{}.appended(*step);
    EXPECT_TRUE(lineage.hasValue()) << lineage.status().message();
    if (!lineage.hasValue())
    {
        return {};
    }
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{}, *lineage,
                                  an::CodecMetadata{}, std::nullopt);
    an::Result<an::Image<float>> made = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return {};
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = made->write();
        EXPECT_TRUE(access.has_value());
        if (!access.has_value())
        {
            return {};
        }
        for (std::uint32_t c = 0u; c < samples->channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < samples->height(); ++y)
            {
                auto row = access->view().row(c, y);
                if (!row.has_value())
                {
                    return {};
                }
                for (std::uint32_t x = 0u; x < samples->width(); ++x)
                {
                    const bool hole = withNaN && c == 0u && x == samples->width() / 2u && y == samples->height() / 2u;
                    (*row)[x] =
                        hole ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(samples->value(x, y, c));
                }
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*made)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? assetView(std::move(*asset)) : ViewHandle();
}

/// Whether a view's document carries the lineage step `sceneWithLineage` gives its scene.
[[nodiscard]] bool carriesTheScenesStep(const ImageViewControl& view)
{
    an::Result<an::consumer::AssetReadLease> lease = view.document().read();
    if (!lease.hasValue())
    {
        return false;
    }
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    for (const an::ProvenanceEntry& entry : metadata.provenance().entries())
    {
        if (entry.operation() == "an::sourceStep")
        {
            return true;
        }
    }
    return false;
}

} // namespace

// apply: Apply is offered once the window holds a sample, and no longer once the samples are cleared.
TEST_F(MasksProcessCase, BackgroundExtractionOffersApplyOnlyWithSamples)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("BackgroundExtraction").open(*target);
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(window.get());
    ASSERT_NE(extraction, nullptr);
    QAbstractButton* apply = applyButtonOf(*window);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(apply->isEnabled());
    ImageSampleSelection site;
    site.pos = QPoint(40, 40);
    site.size = 25u;
    extraction->addSampleSelection(site);
    EXPECT_TRUE(apply->isEnabled());
    window->findChild<QPushButton*>(QStringLiteral("clearSamplesButton"))->click();
    EXPECT_TRUE(extraction->samplePositions().empty());
    EXPECT_FALSE(apply->isEnabled());
    window.reset();
    dropView(context(), target);
}

// apply: the grid is placed on a worker, and keeps the samples that sit on the background - none centred on the bright
// square, whose median is the square's - each inside the picture, and drops the rest.
TEST_F(MasksProcessCase, BackgroundExtractionGridKeepsTheBackgroundSamples)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("BackgroundExtraction").open(*target);
    ASSERT_TRUE(generateGrid(*window, 8));
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(window.get());
    ASSERT_NE(extraction, nullptr);
    const std::vector<BackgroundSamplePosition>& samples = extraction->samplePositions();
    ASSERT_FALSE(samples.empty());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    for (const BackgroundSamplePosition& sample : samples)
    {
        EXPECT_TRUE(an::ui::processes::background_extraction_detail::sampleFits(sample, 300u, 220u))
            << sample.x << "," << sample.y;
        const bool onSquare = sample.x >= 120u && sample.x < 180u && sample.y >= 80u && sample.y < 140u;
        EXPECT_FALSE(onSquare) << "a sample over the square at " << sample.x << "," << sample.y;
    }
    // Eight a line over the picture's width, and as many rows as fit its height: the square's sites are dropped.
    EXPECT_LT(samples.size(), 8u * 7u);
    EXPECT_TRUE(applyButtonOf(*window)->isEnabled());
    window.reset();
    dropView(context(), target);
}

// error: a grid whose row of boxes is wider than the picture is refused by name, and places no sample.
TEST_F(MasksProcessCase, BackgroundExtractionGridWiderThanThePictureIsRefusedByName)
{
    ViewHandle target = greyView(112u, 104u, [](std::uint32_t x, std::uint32_t) { return 0.1f + 0.001f * x; });
    WindowHandle window = named("BackgroundExtraction").open(*target);
    ASSERT_TRUE(generateGrid(*window, 5));
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(window.get());
    ASSERT_NE(extraction, nullptr);
    EXPECT_TRUE(extraction->samplePositions().empty());
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("fit")))
        << context().errorTexts.back().toStdString();
    window.reset();
    dropView(context(), target);
}

// apply: a sample whose box would leave the picture is not kept, and a removal takes the sample nearest the click.
TEST_F(MasksProcessCase, BackgroundExtractionKeepsOnlySamplesInsideAndRemovesTheNearest)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("BackgroundExtraction").open(*target);
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(window.get());
    ASSERT_NE(extraction, nullptr);
    auto place = [extraction](int x, int y) {
        ImageSampleSelection site;
        site.pos = QPoint(x, y);
        site.size = 25u;
        extraction->addSampleSelection(site);
    };
    place(5, 100);
    EXPECT_TRUE(extraction->samplePositions().empty()) << "its box would leave the picture";
    place(40, 40);
    place(250, 180);
    place(60, 50);
    ASSERT_EQ(extraction->samplePositions().size(), 3u);
    ImageSampleSelection click;
    click.pos = QPoint(55, 47);
    extraction->removeSampleSelection(click);
    ASSERT_EQ(extraction->samplePositions().size(), 2u);
    EXPECT_EQ(extraction->samplePositions()[0], (BackgroundSamplePosition{.x = 40u, .y = 40u}));
    EXPECT_EQ(extraction->samplePositions()[1], (BackgroundSamplePosition{.x = 250u, .y = 180u}));
    window.reset();
    dropView(context(), target);
}

// outputs: an Apply asked to keep the background opens it beside the target, in the target's representation.
TEST_F(MasksProcessCase, BackgroundExtractionOpensTheModelBesideTheTarget)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("BackgroundExtraction").open(*target);
    ASSERT_TRUE(generateGrid(*window, 8));
    setChecked(*window, "outputBackgroundModel", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(madeNames(before), std::vector<std::string>{"target - GRADIENT"});
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    const std::optional<fixtures::FixtureSamples> model = documentSamples(made.front()->document());
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->sampleType(), fixtures::FixtureSampleType::Float32);
    // The background the samples describe: about 0.1 in the top left, rising to the right and down.
    EXPECT_NEAR(model->value(20u, 20u, 0u), 0.1 + 0.0004 * 20.0 + 0.0002 * 20.0, 0.02);
    EXPECT_GT(model->value(280u, 200u, 0u), model->value(20u, 20u, 0u));
    // The model is a derived image, recorded as the background it is.
    {
        an::Result<an::consumer::AssetReadLease> lease = made.front()->document().read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const an::ImageMetadata& metadata =
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
        const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata.codec());
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.operation, "background-extraction-gradient");
    }
    window.reset();
    dropView(context(), target);
}

// preview: a preview that computes on request shows the target until its button asks, and the result once it has;
// the background windows offer no preview area.
TEST_F(MasksProcessCase, BackgroundPreviewsShowTheTargetUntilAsked)
{
    for (const char* name : {"BackgroundExtraction", "AutoGradientRemoval"})
    {
        SCOPED_TRACE(name);
        const MasksProcess& process = named(name);
        ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        WindowHandle window = process.open(*target);
        if (std::string_view(name) == "BackgroundExtraction")
        {
            ASSERT_TRUE(generateGrid(*window, 8));
        }
        auto* preview = window->findChild<QCheckBox*>(QStringLiteral("previewCheckbox"));
        ASSERT_NE(preview, nullptr);
        preview->setChecked(true);
        ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
        ASSERT_TRUE(settleWindow(*window));
        ASSERT_NE(window->previewView(), nullptr);
        // The model is fitted over the whole image - the samples are its positions - so no preview area is offered.
        if (QCheckBox* areaBox = window->previewView()->getRTPreviewAreaCheckBox(); areaBox != nullptr)
        {
            areaBox->setChecked(true);
            EXPECT_TRUE(window->previewArea().isEmpty()) << "the window offers no preview area";
            areaBox->setChecked(false);
            ASSERT_TRUE(settleWindow(*window));
        }
        const std::optional<fixtures::FixtureSamples> before = documentSamples(window->previewView()->document());
        ASSERT_TRUE(source.has_value() && before.has_value());
        EXPECT_TRUE(fixtures::sameSamples(*source, *before)) << "the preview shows the target until asked";
        const std::uint64_t committed = window->runTelemetry().previewsCommitted;
        window->findChild<QPushButton*>(QStringLiteral("previewButton"))->click();
        ASSERT_TRUE(pumpUntil([&window, committed]() { return window->runTelemetry().previewsCommitted > committed; }));
        ASSERT_TRUE(settleWindow(*window));
        const std::optional<fixtures::FixtureSamples> after = documentSamples(window->previewView()->document());
        ASSERT_TRUE(after.has_value());
        EXPECT_FALSE(fixtures::sameSamples(*source, *after)) << "the preview shows the result once asked";
        window.reset();
        dropView(context(), target);
    }
}

// outputs: the automatic gradient removal opens the background it found and the structure it kept out of the fit
// beside the target when asked.
TEST_F(MasksProcessCase, AutoGradientRemovalOpensItsModelAndProtectionBesideTheTarget)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("AutoGradientRemoval").open(*target);
    named("AutoGradientRemoval").configure(*window, "-");
    setChecked(*window, "outputGradientModel", true);
    setChecked(*window, "outputProtectionMask", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(madeNames(before), (std::vector<std::string>{"target - AGR Gradient", "target - AGR Protection Mask"}));
    window.reset();
    dropView(context(), target);
}

namespace
{

/// Turns an automatic gradient removal window's preview on, waits for it to show the target, asks for the preview and
/// waits for it to land; false when either never lands.
[[nodiscard]] bool computeAutoGradientPreview(ImageRTPreviewControl& window)
{
    auto* preview = window.findChild<QCheckBox*>(QStringLiteral("previewCheckbox"));
    auto* button = window.findChild<QPushButton*>(QStringLiteral("previewButton"));
    if (preview == nullptr || button == nullptr)
    {
        return false;
    }
    preview->setChecked(true);
    if (!pumpUntil([&window]() { return window.runTelemetry().previewsCommitted >= 1u; }) || !settleWindow(window))
    {
        return false;
    }
    const std::uint64_t committed = window.runTelemetry().previewsCommitted;
    button->click();
    return pumpUntil([&window, committed]() { return window.runTelemetry().previewsCommitted > committed; }) &&
           settleWindow(window);
}

} // namespace

// preview: one preview run makes the corrected image, the gradient model and the protection mask; changing what the
// preview shows starts no new run and shows each at once, as a preview run for that display alone shows it.
TEST_F(MasksProcessCase, AutoGradientRemovalShowsEveryDisplayOfOnePreviewRunWithNoNewRun)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    ASSERT_TRUE(source.has_value());
    const MasksProcess& process = named("AutoGradientRemoval");
    // Corrected, gradient model, protection mask: what a preview run for each display alone shows.
    constexpr int kDisplays = 3;
    std::array<std::optional<fixtures::FixtureSamples>, kDisplays> alone;
    for (int display = 0; display < kDisplays; ++display)
    {
        SCOPED_TRACE(display);
        WindowHandle window = process.open(*target);
        process.configure(*window, "-");
        setIndex(*window, "previewDisplaySelector", display);
        ASSERT_TRUE(computeAutoGradientPreview(*window));
        alone[static_cast<std::size_t>(display)] = documentSamples(window->previewView()->document());
        ASSERT_TRUE(alone[static_cast<std::size_t>(display)].has_value());
    }

    WindowHandle window = process.open(*target);
    process.configure(*window, "-");
    ASSERT_TRUE(computeAutoGradientPreview(*window));
    EXPECT_EQ(window->keptPreviewCount(), 3u) << "the run keeps the image of every display";
    const ProcessRunTelemetry computed = window->runTelemetry();
    std::array<std::optional<fixtures::FixtureSamples>, kDisplays> shown;
    shown[0] = documentSamples(window->previewView()->document());
    for (const int display : {1, 2, 0})
    {
        SCOPED_TRACE(display);
        setIndex(*window, "previewDisplaySelector", display);
        ASSERT_TRUE(settleWindow(*window));
        const std::optional<fixtures::FixtureSamples> now = documentSamples(window->previewView()->document());
        ASSERT_TRUE(now.has_value());
        EXPECT_TRUE(fixtures::sameSamples(*alone[static_cast<std::size_t>(display)], *now))
            << "the display shows another image than a preview run for it alone";
        if (display != 0)
        {
            shown[static_cast<std::size_t>(display)] = now;
        }
        else
        {
            EXPECT_TRUE(fixtures::sameSamples(*shown[0], *now)) << "the corrected image changed between displays";
        }
    }
    EXPECT_EQ(window->runTelemetry().previewsStarted, computed.previewsStarted)
        << "changing the display ran the preview again";
    EXPECT_EQ(window->runTelemetry().keptPreviewsShown, computed.keptPreviewsShown + 3u);
    EXPECT_FALSE(fixtures::sameSamples(*source, *shown[0])) << "the preview shows the corrected image";
    EXPECT_FALSE(fixtures::sameSamples(*shown[0], *shown[1])) << "the model is another image";
    EXPECT_FALSE(fixtures::sameSamples(*shown[1], *shown[2])) << "the protection mask is another image";
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropView(context(), target);
}

// preview: a snapshot of the preview is the image the display shows, made by a run of its own for that display alone.
TEST_F(MasksProcessCase, AutoGradientRemovalSnapshotsTheDisplayShown)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    const MasksProcess& process = named("AutoGradientRemoval");
    WindowHandle window = process.open(*target);
    process.configure(*window, "-");
    ASSERT_TRUE(computeAutoGradientPreview(*window));
    for (const int display : {1, 2, 0})
    {
        SCOPED_TRACE(display);
        setIndex(*window, "previewDisplaySelector", display);
        ASSERT_TRUE(settleWindow(*window));
        const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
        ASSERT_TRUE(shown.has_value());
        const std::size_t before = context().madeViews.size();
        window->onRTTakeSnapshot(window->previewView());
        ASSERT_TRUE(settleWindow(*window));
        ASSERT_EQ(context().madeViews.size(), before + 1u);
        const std::optional<fixtures::FixtureSamples> snapshot =
            documentSamples(context().madeViews.back()->document());
        ASSERT_TRUE(snapshot.has_value());
        EXPECT_TRUE(fixtures::sameSamples(*shown, *snapshot)) << "the snapshot is not the image shown";
    }
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropView(context(), target);
}

// preview: a preview the view stretches from the target's statistics changes display with no new run too: the kept
// image is stretched through the analysis the run's frame was.
TEST_F(MasksProcessCase, AutoGradientRemovalChangesTheDisplayOfAStretchedPreviewWithNoNewRun)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    const MasksProcess& process = named("AutoGradientRemoval");
    WindowHandle window = process.open(*target);
    process.configure(*window, "-");
    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("previewCheckbox"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
    ASSERT_TRUE(settleWindow(*window));
    window->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleWindow(*window));
    ASSERT_TRUE(window->previewView()->isAutoStretchEnabled());
    ASSERT_TRUE(computeAutoGradientPreview(*window));
    const ProcessRunTelemetry computed = window->runTelemetry();

    for (const int display : {1, 2, 0})
    {
        setIndex(*window, "previewDisplaySelector", display);
        ASSERT_TRUE(settleWindow(*window));
    }
    EXPECT_EQ(window->runTelemetry().previewsStarted, computed.previewsStarted)
        << "changing the display of a stretched preview ran it again";
    EXPECT_EQ(window->runTelemetry().keptPreviewsShown, computed.keptPreviewsShown + 3u);
    EXPECT_TRUE(window->previewView()->isAutoStretchEnabled());
    window.reset();
    dropView(context(), target);
}

// preview: a setting changed after a preview run lets go of the images the run kept, so the next change of display
// shows the target, never an image of the settings before; asked again, the preview keeps the new settings' images.
TEST_F(MasksProcessCase, AutoGradientRemovalLetsGoOfThePreviewsImagesWhenASettingChanges)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    ASSERT_TRUE(source.has_value());
    const MasksProcess& process = named("AutoGradientRemoval");
    WindowHandle window = process.open(*target);
    process.configure(*window, "-");
    ASSERT_TRUE(computeAutoGradientPreview(*window));
    const ProcessRunTelemetry computed = window->runTelemetry();

    ASSERT_EQ(window->keptPreviewCount(), 3u);
    setDouble(*window, "scaleValue", 6.0);
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->keptPreviewCount(), 0u) << "the images of the settings before are still kept";
    setIndex(*window, "previewDisplaySelector", 1);
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->runTelemetry().keptPreviewsShown, computed.keptPreviewsShown)
        << "an image of the settings before was shown";
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    ASSERT_TRUE(shown.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*source, *shown)) << "the preview shows the target until asked again";

    const ProcessRunTelemetry changed = window->runTelemetry();
    window->findChild<QPushButton*>(QStringLiteral("previewButton"))->click();
    ASSERT_TRUE(pumpUntil(
        [&window, &changed]() { return window->runTelemetry().previewsCommitted > changed.previewsCommitted; }));
    ASSERT_TRUE(settleWindow(*window));
    const ProcessRunTelemetry asked = window->runTelemetry();
    EXPECT_EQ(window->keptPreviewCount(), 3u);
    setIndex(*window, "previewDisplaySelector", 0);
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->runTelemetry().previewsStarted, asked.previewsStarted);
    EXPECT_EQ(window->runTelemetry().keptPreviewsShown, asked.keptPreviewsShown + 1u);
    window.reset();
    dropView(context(), target);
}

// state: the protection's values follow its switch and the polynomial degree the simplified model's.
TEST_F(MasksProcessCase, AutoGradientRemovalDependentControlsFollowTheirSwitches)
{
    auto state = autoGradientRemovalDependentUiState(true, false, false);
    EXPECT_FALSE(state.protectionParametersEnabled);
    EXPECT_FALSE(state.protectionMaskOutputEnabled);
    EXPECT_FALSE(state.protectionMaskPreviewEnabled);
    EXPECT_FALSE(state.polynomialDegreeEnabled);

    state = autoGradientRemovalDependentUiState(true, true, true);
    EXPECT_TRUE(state.protectionParametersEnabled);
    EXPECT_TRUE(state.protectionMaskOutputEnabled);
    EXPECT_TRUE(state.protectionMaskPreviewEnabled);
    EXPECT_TRUE(state.polynomialDegreeEnabled);

    state = autoGradientRemovalDependentUiState(false, true, true);
    EXPECT_FALSE(state.protectionParametersEnabled);
    EXPECT_FALSE(state.polynomialDegreeEnabled);
}

// outputs: the dark structure enhancement opens the mask it enhanced through beside the target when asked, derived from
// the target's record; the dialog offers the ten layers the engine's starlet holds.
TEST_F(MasksProcessCase, DarkStructureOpensItsMaskBesideTheTarget)
{
    ViewHandle target = sceneWithLineage(session(), "stars-mono-float32");
    ASSERT_NE(target, nullptr);
    WindowHandle window = named("DarkStructure").open(*target);
    named("DarkStructure").configure(*window, "-");
    auto* layers = window->findChild<QSpinBox*>(QStringLiteral("layersValue"));
    ASSERT_NE(layers, nullptr);
    EXPECT_EQ(layers->maximum(), 10);
    setChecked(*window, "createMaskImage", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(madeNames(before), std::vector<std::string>{"target - DarkStructureEnhance Mask"});
    const std::vector<ImageViewControl*> made = viewsMadeSince(context(), before);
    ASSERT_EQ(made.size(), 1u);
    EXPECT_TRUE(carriesTheScenesStep(*made.front())) << "the mask derives its record from the target";
    window.reset();
    dropView(context(), target);
}

// outputs: the multiscale transform opens the deringing maps beside the target when asked, derived from the target's
// record.
TEST_F(MasksProcessCase, MultiscaleOpensItsDeringingMapsBesideTheTarget)
{
    ViewHandle target = sceneWithLineage(session(), "stars-mono-float32");
    ASSERT_NE(target, nullptr);
    WindowHandle window = named("MultiscaleLinear").open(*target);
    named("MultiscaleLinear").configure(*window, "linear-three-layers-finest-removed");
    setGroupChecked(*window, "deringingGroup", true);
    setChecked(*window, "deringingOutputMapsCheckBox", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(madeNames(before),
              (std::vector<std::string>{"target - Deringing Dark Map", "target - Deringing Bright Map"}));
    for (ImageViewControl* map : viewsMadeSince(context(), before))
    {
        EXPECT_TRUE(carriesTheScenesStep(*map)) << map->getUniqueName() << " derives its record from the target";
    }
    window.reset();
    dropView(context(), target);
}

// apply: the star reduction offers Apply only once a starless image is chosen.
TEST_F(MasksProcessCase, StarReductionOffersApplyOnlyWithAStarlessImage)
{
    ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
    ViewHandle starless = sceneView(session(), "stars-rgb-float32-starless", "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    WindowHandle window = named("StarReduction").open(*target);
    auto* selector = window->findChild<QComboBox*>(QStringLiteral("starlessImageSelector"));
    ASSERT_NE(selector, nullptr);
    // The dialog runs from its Ok button.
    QAbstractButton* apply =
        window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"))->button(QDialogButtonBox::Ok);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(apply->isEnabled());
    selector->setCurrentIndex(selector->findText(QStringLiteral("starless")));
    EXPECT_TRUE(apply->isEnabled());
    window.reset();
    dropView(context(), starless);
    dropView(context(), target);
}

// outputs: the stars the reduction took away open beside the target when asked.
TEST_F(MasksProcessCase, StarReductionOpensTheStarsBesideTheTarget)
{
    ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
    ViewHandle starless = sceneView(session(), "stars-rgb-float32-starless", "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    WindowHandle window = named("StarReduction").open(*target);
    named("StarReduction").configure(*window, "-");
    setChecked(*window, "createStarsImage", true);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(madeNames(before), std::vector<std::string>{"target - reduced stars"});
    window.reset();
    dropView(context(), starless);
    dropView(context(), target);
}

// mask: the small-star protection mask is made on a worker and becomes the target's mask when it lands.
TEST_F(MasksProcessCase, StarReductionProtectionMaskBecomesTheTargetsMask)
{
    ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
    ViewHandle starless = sceneView(session(), "stars-rgb-float32-starless", "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    WindowHandle window = named("StarReduction").open(*target);
    named("StarReduction").configure(*window, "-");
    const std::size_t before = context().madeViews.size();
    window->findChild<QPushButton*>(QStringLiteral("initializeSmallStarProtection"))->click();
    ASSERT_TRUE(pumpUntil([&target]() { return target->isMasked(); }));
    EXPECT_EQ(madeNames(before), std::vector<std::string>{"target - star reduced mask"});
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    target->removeCurrentMask();
    window.reset();
    dropView(context(), starless);
    dropView(context(), target);
}

// The work a button starts reads the target under a lease taken before it asks to be admitted. An edit of the view
// holds the target's writer and then asks to be admitted, so at a width of one work admitted first would hold the only
// thread while it waited for the writer, and the edit would wait for the thread: neither would end. The background
// extraction's grid waits for the target's writer.
TEST_F(MasksProcessCaseAtWidthOne, BackgroundExtractionsGridWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("BackgroundExtraction").open(*target);
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(window.get());
    ASSERT_NE(extraction, nullptr);
    setInt(*window, "samplesPerLineValue", 8);
    setDouble(*window, "gridToleranceValue", 2.0);
    auto* generate = window->findChild<QPushButton*>(QStringLiteral("generateSamplesButton"));
    ASSERT_NE(generate, nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    generate->click();
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the grid was admitted while it could not read the target";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the grid held while the grid waited for the edit's writer";

    ASSERT_TRUE(pumpUntil([extraction]() { return !extraction->isGeneratingSamples(); }));
    EXPECT_FALSE(extraction->samplePositions().empty());
    window.reset();
}

// The star reduction's Apply reads its starless image under a lease it takes before it asks to be admitted, in one
// order with its target's writer: an edit of the starless image's view holds that writer and then asks to be admitted.
TEST_F(MasksProcessCaseAtWidthOne, StarReductionWaitingForAnEditOfItsStarlessImageHoldsNoThreadTheEditNeeds)
{
    ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
    ViewHandle starless = sceneView(session(), "stars-rgb-float32-starless", "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    WindowHandle window = named("StarReduction").open(*target);
    named("StarReduction").configure(*window, "-");

    an::tests::viewer::EditHoldingTheWriter edit(*starless);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    window->applyProcess();
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the run held while the run waited for the edit's writer";

    ASSERT_TRUE(settleWindow(*window) && settleView(*target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropView(context(), starless);
    dropView(context(), target);
}

// The star reduction's protection mask waits for the target's writer.
TEST_F(MasksProcessCaseAtWidthOne, StarReductionsProtectionMaskWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
    ViewHandle starless = sceneView(session(), "stars-rgb-float32-starless", "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    WindowHandle window = named("StarReduction").open(*target);
    named("StarReduction").configure(*window, "-");

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    window->findChild<QPushButton*>(QStringLiteral("initializeSmallStarProtection"))->click();
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the protection mask was admitted while it could not read the target";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the mask held while the mask waited for the edit's writer";

    ASSERT_TRUE(pumpUntil([&target]() { return target->isMasked(); }));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    target->removeCurrentMask();
    window.reset();
    dropView(context(), starless);
    dropView(context(), target);
}

// The grid's worker takes the window's stop: at the application's exit every window's work is stopped, and a grid
// waiting for the target's writer ends then rather than when the writer lets go - the exit waited for it before.
TEST_F(MasksProcessCase, BackgroundExtractionsGridEndsWhenTheWindowsWorkIsStopped)
{
    ViewHandle target = greyView(300u, 220u, backgroundWithASquare);
    WindowHandle window = named("BackgroundExtraction").open(*target);
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(window.get());
    ASSERT_NE(extraction, nullptr);
    setInt(*window, "samplesPerLineValue", 8);
    setDouble(*window, "gridToleranceValue", 2.0);
    auto* generate = window->findChild<QPushButton*>(QStringLiteral("generateSamplesButton"));
    ASSERT_NE(generate, nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    generate->click();
    EXPECT_TRUE(extraction->isGeneratingSamples());
    extraction->stopWorkInFlight();
    EXPECT_TRUE(pumpUntil([extraction]() { return !extraction->isGeneratingSamples(); }))
        << "the grid waited for the writer after the window's work was stopped";
    EXPECT_TRUE(extraction->samplePositions().empty());

    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    window.reset();
    dropView(context(), target);
}

// The protection mask's worker takes the window's stop too: a mask waiting for the target's writer ends when the
// window's work is stopped.
TEST_F(MasksProcessCase, StarReductionsProtectionMaskEndsWhenTheWindowsWorkIsStopped)
{
    ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
    ViewHandle starless = sceneView(session(), "stars-rgb-float32-starless", "starless");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(starless, nullptr);
    context().addViewControl(starless.get());
    WindowHandle window = named("StarReduction").open(*target);
    named("StarReduction").configure(*window, "-");
    auto* initialize = window->findChild<QPushButton*>(QStringLiteral("initializeSmallStarProtection"));
    ASSERT_NE(initialize, nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    initialize->click();
    EXPECT_FALSE(initialize->isEnabled());
    window->stopWorkInFlight();
    EXPECT_TRUE(pumpUntil([initialize]() { return initialize->isEnabled(); }))
        << "the mask waited for the writer after the window's work was stopped";
    EXPECT_FALSE(target->isMasked());

    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    window.reset();
    dropView(context(), starless);
    dropView(context(), target);
}

// The mask file transfers take the window's stop: once the window's work is stopped, an import or an export asked for
// is refused rather than read or written in full, so the exit is not kept waiting for a file.
TEST_F(MasksProcessCase, MaskDrawingTransfersAfterTheWindowsWorkIsStoppedAreRefused)
{
    const MasksProcess& process = named("MaskDrawing");
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = process.open(*target);
    process.configure(*window, "imported");
    auto* drawing = dynamic_cast<MaskDrawingProcess*>(window.get());
    ASSERT_NE(drawing, nullptr);
    ASSERT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("drawn.fits"));
    drawing->exportMaskTo(path);
    ASSERT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
    ASSERT_TRUE(QFileInfo::exists(path));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();

    drawing->stopWorkInFlight();
    drawing->importMaskFrom(path);
    ASSERT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
    EXPECT_EQ(context().errorCount(), 1) << "the import went on after the window's work was stopped";
    const QString again = directory.filePath(QStringLiteral("again.fits"));
    drawing->exportMaskTo(again);
    ASSERT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
    EXPECT_EQ(context().errorCount(), 2) << "the export went on after the window's work was stopped";
    EXPECT_FALSE(QFileInfo::exists(again));
    window.reset();
    dropView(context(), target);
}

// apply: the star mask's detector, at the dialog's own values over a field wide enough for its search, paints every
// star planted in it.
TEST_F(MasksProcessCase, StarMaskPaintsEveryStarOfTheField)
{
    ViewHandle target = sceneView(session(), "stars-mono-float32-wide", "target");
    ASSERT_NE(target, nullptr);
    const fixtures::FixtureImage* field = fixtures::fixtureNamed("stars-mono-float32-wide");
    ASSERT_NE(field, nullptr);
    WindowHandle window = named("StarMask").open(*target);
    std::optional<fixtures::FixtureSamples> mask = applyAndRead(context(), named("StarMask"), *window, *target);
    ASSERT_TRUE(mask.has_value());
    EXPECT_EQ(mask->channelCount(), 1u);
    for (const fixtures::FixtureStar& star : field->stars)
    {
        const auto x = static_cast<std::uint32_t>(std::floor(star.x));
        const auto y = static_cast<std::uint32_t>(std::floor(star.y));
        EXPECT_GT(mask->value(x, y, 0u), 0.5) << "the star at " << star.x << "," << star.y;
    }
    // A shaped star falls off from its centre, so the mask holds coverages between none and whole.
    std::uint64_t graded = 0u;
    for (std::uint32_t y = 0u; y < mask->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < mask->width(); ++x)
        {
            graded += mask->value(x, y, 0u) > 0.0 && mask->value(x, y, 0u) < 1.0 ? 1u : 0u;
        }
    }
    EXPECT_GT(graded, 0u) << "the shaped areas fall off";
    window.reset();
    dropView(context(), target);
}

// error: a picture too small to hold the search box at its radius is refused by name, and makes no mask.
TEST_F(MasksProcessCase, StarMaskSearchThePictureCannotHoldIsRefusedByName)
{
    ViewHandle target = sceneView(session(), "stars-mono-float32", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = named("StarMask").open(*target);
    setInt(*window, "radiusValue", 5);
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
    EXPECT_TRUE(viewsMadeSince(context(), before).empty());
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
    EXPECT_FALSE(context().errorTexts.back().isEmpty());
    window.reset();
    dropView(context(), target);
}

// lifetime: a star mask window closed leaves nothing it made behind - its detection settings take the builder's grid
// and shape row out of its layout and must delete them.
TEST_F(MasksProcessCase, AStarMaskWindowClosedLeavesNothingItMadeBehind)
{
    ViewHandle target = sceneView(session(), "stars-mono-float32", "target");
    ASSERT_NE(target, nullptr);
    const std::vector<std::string> survivors = an::tests::harness::leftBehindBy([this, &target]() {
        WindowHandle window = named("StarMask").open(*target);
        EXPECT_TRUE(settleWindow(*window));
    });
    EXPECT_TRUE(survivors.empty()) << "left behind: " << an::tests::harness::listed(survivors);
    dropView(context(), target);
}

// apply: without shaped areas each star is a solid ellipse - every sample of the mask none or whole.
TEST_F(MasksProcessCase, StarMaskWithoutShapedAreasPaintsSolidEllipses)
{
    ViewHandle target = sceneView(session(), "stars-mono-float32-wide", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = named("StarMask").open(*target);
    setGroupChecked(*window, "shapedAreas", false);
    std::optional<fixtures::FixtureSamples> mask = applyAndRead(context(), named("StarMask"), *window, *target);
    ASSERT_TRUE(mask.has_value());
    std::uint64_t painted = 0u;
    for (std::uint32_t y = 0u; y < mask->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < mask->width(); ++x)
        {
            const double value = mask->value(x, y, 0u);
            EXPECT_TRUE(value == 0.0 || value == 1.0) << x << "," << y << ": " << value;
            painted += value == 1.0 ? 1u : 0u;
        }
    }
    EXPECT_GT(painted, 0u);
    window.reset();
    dropView(context(), target);
}

// apply: the imported drawing is the mask an Apply makes, in the target's representation.
TEST_F(MasksProcessCase, MaskDrawingCreatesTheImportedDrawing)
{
    const MasksProcess& drawing = named("MaskDrawing");
    ViewHandle target = sceneView(session(), "scene-rgb-uint16", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = drawing.open(*target);
    // A preview asks for the drawing as it is - empty - before the import changes it.
    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
    ASSERT_TRUE(settleWindow(*window));
    preview->setChecked(false);
    drawing.configure(*window, "imported");
    std::optional<fixtures::FixtureSamples> mask = applyAndRead(context(), drawing, *window, *target);
    ASSERT_TRUE(mask.has_value());
    EXPECT_EQ(mask->channelCount(), 1u);
    EXPECT_EQ(mask->sampleType(), fixtures::FixtureSampleType::UInt16);
    const std::uint32_t width = mask->width();
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const double expected = std::round(65535.0 * static_cast<double>(x) / static_cast<double>(width - 1u));
        EXPECT_NEAR(mask->value(x, 0u, 0u), expected, 1.0) << "column " << x;
    }
    window.reset();
    dropView(context(), target);
}

// error: a drawing file of another size than the target is refused by name and changes nothing.
TEST_F(MasksProcessCase, MaskDrawingImportOfAnotherSizeIsRefusedByName)
{
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = named("MaskDrawing").open(*target);
    auto* drawing = dynamic_cast<MaskDrawingProcess*>(window.get());
    ASSERT_NE(drawing, nullptr);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("wide.fits"));
    // The target's height, another width.
    std::optional<an::consumer::ImageAsset> wide = an::tests::viewer::makeAsset<float>(
        session(), 40u, 8u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(wide.has_value());
    const std::optional<an::ImageDescriptor> described = wide->descriptor();
    ASSERT_TRUE(described.has_value());
    an::Result<an::OutputIntent> intent = context().buildOutputIntent(*described, an::CodecFormat::Fits);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    ASSERT_TRUE(an::ui::writeViewImage(session(), *wide, path, *intent, std::stop_token{}).ok());
    drawing->importMaskFrom(path);
    ASSERT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("must match")))
        << context().errorTexts.back().toStdString();
    EXPECT_TRUE(drawing->document()->isEmpty());
    window.reset();
    dropView(context(), target);
}

// import: a colour mask file - sixteen-bit codes or floating samples, read back as sensor codes and as a physical
// quantity as every file is read - is imported as its luma read as the fractions of full scale the view shows.
TEST_F(MasksProcessCase, MaskDrawingImportsAColourFileAsItsLumaAsTheViewShowsIt)
{
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    const std::optional<an::ImageDescriptor> targetDescribed = target->document().descriptor();
    ASSERT_TRUE(targetDescribed.has_value());
    const std::uint32_t width = targetDescribed->extent().width();
    const std::uint32_t height = targetDescribed->extent().height();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto imported = [&]<typename SampleT>(const char* name) -> std::optional<fixtures::FixtureSamples> {
        SCOPED_TRACE(name);
        // The same colour mask in either representation: 0.25, 0.5 and 0.75 in red, green and blue.
        std::optional<an::consumer::ImageAsset> colour = an::tests::viewer::makeAsset<SampleT>(
            session(), width, height, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
                const double value = 0.25 * static_cast<double>(c + 1u);
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<SampleT>(value);
                }
                else
                {
                    return static_cast<SampleT>(std::lround(value * 65535.0));
                }
            });
        EXPECT_TRUE(colour.has_value());
        if (!colour.has_value())
        {
            return std::nullopt;
        }
        const QString path = directory.filePath(QString::fromLatin1(name));
        const std::optional<an::ImageDescriptor> described = colour->descriptor();
        EXPECT_TRUE(described.has_value());
        if (!described.has_value())
        {
            return std::nullopt;
        }
        an::Result<an::OutputIntent> intent = context().buildOutputIntent(*described, an::CodecFormat::Fits);
        EXPECT_TRUE(intent.hasValue()) << intent.status().message();
        EXPECT_TRUE(intent.hasValue() && an::ui::writeViewImage(session(), *colour, path, *intent, {}).ok());
        WindowHandle window = named("MaskDrawing").open(*target);
        auto* drawing = dynamic_cast<MaskDrawingProcess*>(window.get());
        EXPECT_NE(drawing, nullptr);
        if (drawing == nullptr)
        {
            return std::nullopt;
        }
        const int errorsBefore = context().errorCount();
        drawing->importMaskFrom(path);
        EXPECT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
        EXPECT_TRUE(pumpUntil([drawing]() { return drawing->pendingEdits() == 0u; }));
        EXPECT_EQ(context().errorCount(), errorsBefore)
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_FALSE(drawing->document()->isEmpty()) << "the file was imported";
        std::optional<fixtures::FixtureSamples> mask = samplesOf(*drawing->document()->mask());
        window.reset();
        return mask;
    };
    // Rec. 601 luma of the three: 0.299 * 0.25 + 0.587 * 0.5 + 0.114 * 0.75.
    const double luma = 0.299 * 0.25 + 0.587 * 0.5 + 0.114 * 0.75;
    const std::optional<fixtures::FixtureSamples> fromFloats = imported.template operator()<float>("floats.fits");
    ASSERT_TRUE(fromFloats.has_value());
    EXPECT_NEAR(fromFloats->value(2u, 2u, 0u), luma, 1.0e-6);
    const std::optional<fixtures::FixtureSamples> fromCodes = imported.template operator()<std::uint16_t>("codes.fits");
    ASSERT_TRUE(fromCodes.has_value());
    EXPECT_NEAR(fromCodes->value(2u, 2u, 0u), luma, 1.0e-4);
    dropView(context(), target);
}

// preview: the dark structure's mask preview on one colour component reads a file's image - sensor codes, or a
// floating physical quantity, as the reader describes every file - as the fractions of full scale the view shows, as
// it reads the same samples stated normalized.
TEST_F(MasksProcessCase, DarkStructureMaskPreviewOnAComponentReadsAFilesImageAsTheViewShowsIt)
{
    const auto previewed = [&]<typename SampleT>(an::NumericDomain domain) -> std::optional<fixtures::FixtureSamples> {
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), 32u, 24u, Channels::Rgb,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                // Dark lanes across a bright field: structure for the mask to find.
                const double value = ((x / 3u + y / 4u) % 3u == 0u ? 0.15 : 0.65) + 0.05 * static_cast<double>(c);
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<SampleT>(value);
                }
                else
                {
                    return static_cast<SampleT>(std::lround(value * 65535.0));
                }
            },
            domain);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return std::nullopt;
        }
        ViewHandle target = assetView(std::move(*asset));
        WindowHandle window = named("DarkStructure").open(*target);
        named("DarkStructure").configure(*window, "-");
        setIndex(*window, "previewModeComboBox", 1);
        auto* channel = window->findChild<QComboBox*>(QStringLiteral("channelSelector"));
        EXPECT_NE(channel, nullptr);
        if (channel == nullptr)
        {
            return std::nullopt;
        }
        const int lightness = channel->findText(QStringLiteral("CIE L* (Lightness)"));
        EXPECT_GE(lightness, 0);
        channel->setCurrentIndex(lightness);
        auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
        EXPECT_NE(preview, nullptr);
        if (preview == nullptr)
        {
            return std::nullopt;
        }
        preview->setChecked(true);
        EXPECT_TRUE(pumpUntil([&window]() {
            return window->runTelemetry().previewsCommitted + window->runTelemetry().previewsFailed >= 1u;
        }));
        EXPECT_TRUE(settleWindow(*window));
        EXPECT_EQ(window->runTelemetry().previewsFailed, 0u)
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        std::optional<fixtures::FixtureSamples> shown =
            window->previewView() != nullptr ? documentSamples(window->previewView()->document()) : std::nullopt;
        window.reset();
        dropView(context(), target);
        return shown;
    };
    const std::optional<fixtures::FixtureSamples> physical =
        previewed.template operator()<float>(an::NumericDomain::PhysicalScaled);
    const std::optional<fixtures::FixtureSamples> floating =
        previewed.template operator()<float>(an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(physical.has_value() && floating.has_value());
    EXPECT_LE(largestDifference(*physical, *floating), 1.0e-6) << "a floating physical quantity";
    const std::optional<fixtures::FixtureSamples> codes =
        previewed.template operator()<std::uint16_t>(an::NumericDomain::RawSensorCode);
    const std::optional<fixtures::FixtureSamples> sixteen =
        previewed.template operator()<std::uint16_t>(an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(codes.has_value() && sixteen.has_value());
    EXPECT_LE(largestDifference(*codes, *sixteen), 1.0e-6) << "sensor codes";
}

// edits: the toolbar's clear, undo and redo land through the window's queue, in the order they were asked for.
TEST_F(MasksProcessCase, MaskDrawingClearUndoAndRedoLandInOrder)
{
    const MasksProcess& process = named("MaskDrawing");
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = process.open(*target);
    process.configure(*window, "imported");
    auto* drawing = dynamic_cast<MaskDrawingProcess*>(window.get());
    ASSERT_NE(drawing, nullptr);
    auto* toolbar = window->findChild<an::ui::drawing::DrawingToolbar*>();
    ASSERT_NE(toolbar, nullptr);
    ASSERT_FALSE(drawing->document()->isEmpty());

    emit toolbar->clearRequested();
    emit toolbar->undoRequested();
    emit toolbar->redoRequested();
    emit toolbar->undoRequested();
    ASSERT_TRUE(pumpUntil([drawing]() { return drawing->pendingEdits() == 0u; }));
    EXPECT_FALSE(drawing->document()->isEmpty()) << "the clear was undone, redone and undone again";
    EXPECT_TRUE(drawing->document()->canRedo());
    window.reset();
    dropView(context(), target);
}

// export: the drawing is written to a file on a worker, under the export settings, and reads back as itself.
TEST_F(MasksProcessCase, MaskDrawingExportWritesTheDrawing)
{
    const MasksProcess& process = named("MaskDrawing");
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    WindowHandle window = process.open(*target);
    process.configure(*window, "imported");
    auto* drawing = dynamic_cast<MaskDrawingProcess*>(window.get());
    ASSERT_NE(drawing, nullptr);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("drawn.fits"));
    drawing->exportMaskTo(path);
    ASSERT_TRUE(pumpUntil([drawing]() { return !drawing->isTransferring(); }));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_TRUE(QFileInfo::exists(path));
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session(), an::ui::ViewReadRequest{.path = path}, std::stop_token{}, nullptr);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    const std::optional<fixtures::FixtureSamples> written = samplesOf(read->asset);
    const std::optional<fixtures::FixtureSamples> drawn = samplesOf(*drawing->document()->mask());
    ASSERT_TRUE(written.has_value() && drawn.has_value());
    EXPECT_LE(largestDifference(*written, *drawn), 1.0e-6);
    window.reset();
    dropView(context(), target);
}

// apply: binarize compares codes - an unsigned threshold truncated to a code, a signed one rounded from the smallest
// code - so a sample on the threshold's code is selected and the one below it is not.
TEST_F(MasksProcessCase, BinarizeComparesTheThresholdsCode)
{
    const MasksProcess& binarize = named("Binarize");
    auto check = [&]<typename SampleT>(std::vector<SampleT> codes, std::vector<bool> selected) {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        const auto width = static_cast<std::uint32_t>(codes.size());
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), width, 1u, Channels::Grey,
            [&codes](std::uint32_t x, std::uint32_t, std::uint32_t) { return codes[x]; });
        ASSERT_TRUE(asset.has_value());
        ViewHandle target = assetView(std::move(*asset));
        WindowHandle window = binarize.open(*target);
        binarize.configure(*window, "-");
        std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), binarize, *window, *target);
        ASSERT_TRUE(result.has_value());
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double expected = selected[x] ? static_cast<double>((std::numeric_limits<SampleT>::max)())
                                                : static_cast<double>((std::numeric_limits<SampleT>::min)());
            EXPECT_EQ(result->value(x, 0u, 0u), expected) << "code " << static_cast<double>(codes[x]);
        }
        window.reset();
        dropView(context(), target);
    };
    // A threshold of one half is code 32767 of an unsigned sixteen-bit image, truncated from 32767.5, and code -1 of a
    // signed one, rounded from -0.5 away from zero.
    check.template operator()<std::uint16_t>({32766u, 32767u, 32768u}, {false, true, true});
    check.template operator()<std::int16_t>({-2, -1, 0}, {false, true, true});
}

// apply: a colour image's stars are found in its green channel - its red and blue channels here hold the field turned
// left to right, whose stars lie elsewhere - and every green star is painted.
TEST_F(MasksProcessCase, StarMaskFindsAColourImagesStarsInItsGreenChannel)
{
    const fixtures::FixtureImage* field = fixtures::fixtureNamed("stars-mono-float32-wide");
    ASSERT_NE(field, nullptr);
    std::string error;
    const std::optional<fixtures::FixtureSamples> mono = fixtures::materializeFixture(*field, error);
    ASSERT_TRUE(mono.has_value()) << error;
    const std::uint32_t width = mono->width();
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), width, mono->height(), Channels::Rgb,
        [&mono, width](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(c == 1u ? mono->value(x, y, 0u) : mono->value(width - 1u - x, y, 0u));
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    WindowHandle window = named("StarMask").open(*target);
    std::optional<fixtures::FixtureSamples> mask = applyAndRead(context(), named("StarMask"), *window, *target);
    ASSERT_TRUE(mask.has_value());
    for (const fixtures::FixtureStar& star : field->stars)
    {
        EXPECT_GT(mask->value(static_cast<std::uint32_t>(std::floor(star.x)),
                              static_cast<std::uint32_t>(std::floor(star.y)), 0u),
                  0.5)
            << "the green star at " << star.x << "," << star.y;
    }
    window.reset();
    dropView(context(), target);
}

// apply: a colour image read from a floating file states a physical quantity, as the reader describes every floating
// file, and its CIE lightness is taken from its samples read as fractions of full scale, as the view shows them: the
// star mask on the lightness and the range selection on it answer as they do over the same samples stated normalized.
TEST_F(MasksProcessCase, ALightnessOfAFloatingFilesColourImageIsTakenAsTheViewShowsIt)
{
    const fixtures::FixtureImage* field = fixtures::fixtureNamed("stars-mono-float32-wide");
    ASSERT_NE(field, nullptr);
    std::string error;
    const std::optional<fixtures::FixtureSamples> mono = fixtures::materializeFixture(*field, error);
    ASSERT_TRUE(mono.has_value()) << error;
    const auto masked = [&](const char* process, an::NumericDomain domain) -> std::optional<fixtures::FixtureSamples> {
        SCOPED_TRACE(process);
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
            session(), mono->width(), mono->height(), Channels::Rgb,
            [&mono](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                return static_cast<float>(mono->value(x, y, 0u) * (1.0 - 0.1 * c));
            },
            domain);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return std::nullopt;
        }
        ViewHandle target = assetView(std::move(*asset));
        const MasksProcess& under = named(process);
        WindowHandle window = under.open(*target);
        under.configure(*window, "-");
        setChecked(*window, std::string_view(process) == "StarMask" ? "useLightness" : "lightness", true);
        const int errorsBefore = context().errorCount();
        std::optional<fixtures::FixtureSamples> mask = applyAndRead(context(), under, *window, *target);
        EXPECT_EQ(context().errorCount(), errorsBefore)
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        window.reset();
        dropView(context(), target);
        return mask;
    };
    for (const char* process : {"StarMask", "RangeSelection"})
    {
        const std::optional<fixtures::FixtureSamples> physical = masked(process, an::NumericDomain::PhysicalScaled);
        const std::optional<fixtures::FixtureSamples> normalized = masked(process, an::NumericDomain::NormalizedLinear);
        ASSERT_TRUE(physical.has_value() && normalized.has_value()) << process;
        EXPECT_LE(largestDifference(*physical, *normalized), 1.0e-6) << process;
    }
}

// metadata: a binarized image gives every sample a value, so its result states every sample valid whatever rule its
// source stated.
TEST_F(MasksProcessCase, BinarizeStatesEverySampleValid)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, 6u, 4u, Channels::Grey);
    ASSERT_TRUE(described.has_value());
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finiteOnly.has_value());
    an::ImageSemanticsRequest meaning = an::ImageSemantics{}.toRequest();
    meaning.validity = *finiteOnly;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        ASSERT_TRUE(access.has_value());
        for (std::uint32_t y = 0u; y < 4u; ++y)
        {
            auto row = access->view().row(0u, y);
            ASSERT_TRUE(row.has_value());
            for (std::uint32_t x = 0u; x < 6u; ++x)
            {
                (*row)[x] = x == 2u && y == 1u ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(x) / 5.0f;
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();
    ViewHandle target = assetView(std::move(*asset));
    WindowHandle window = named("Binarize").open(*target);
    named("Binarize").configure(*window, "-");
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    {
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const an::ImageMetadata& result =
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
        EXPECT_TRUE(result.semantics().validity() == an::ValidityDescriptor::allValid());
    }
    window.reset();
    dropView(context(), target);
}

// apply: star reduction holds both images to the unit interval before it reduces, a sample holding no number made
// zero: a starless image with such samples reduces as its held copy does.
TEST_F(MasksProcessCase, StarReductionHoldsBothImagesToTheUnitInterval)
{
    const fixtures::FixtureImage* field = fixtures::fixtureNamed("stars-rgb-float32-starless");
    ASSERT_NE(field, nullptr);
    std::string error;
    const std::optional<fixtures::FixtureSamples> starless = fixtures::materializeFixture(*field, error);
    ASSERT_TRUE(starless.has_value()) << error;
    auto starlessAsset = [&](bool held) {
        return an::tests::viewer::makeAsset<float>(
            session(), starless->width(), starless->height(), Channels::Rgb,
            [&starless, held](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                if (x == 5u && y == 5u && c == 0u)
                {
                    return held ? 0.0f : std::numeric_limits<float>::quiet_NaN();
                }
                if (x == 6u && y == 6u && c == 1u)
                {
                    return held ? 1.0f : 1.5f;
                }
                return static_cast<float>(starless->value(x, y, c));
            });
    };
    std::optional<an::consumer::ImageAsset> raw = starlessAsset(false);
    std::optional<an::consumer::ImageAsset> held = starlessAsset(true);
    ASSERT_TRUE(raw.has_value() && held.has_value());
    ViewHandle rawView = assetView(std::move(*raw), "starless");
    ViewHandle heldView = assetView(std::move(*held), "held");
    context().addViewControl(rawView.get());
    context().addViewControl(heldView.get());
    auto reduceWith = [&](const QString& starlessName) -> std::optional<fixtures::FixtureSamples> {
        ViewHandle target = sceneView(session(), "stars-rgb-float32", "target");
        EXPECT_NE(target, nullptr);
        if (target == nullptr)
        {
            return std::nullopt;
        }
        WindowHandle window = named("StarReduction").open(*target);
        named("StarReduction").configure(*window, "-");
        auto* selector = window->findChild<QComboBox*>(QStringLiteral("starlessImageSelector"));
        EXPECT_NE(selector, nullptr);
        if (selector != nullptr)
        {
            selector->setCurrentIndex(selector->findText(starlessName));
        }
        std::optional<fixtures::FixtureSamples> result =
            applyAndRead(context(), named("StarReduction"), *window, *target);
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
        window.reset();
        dropView(context(), target);
        return result;
    };
    const std::optional<fixtures::FixtureSamples> fromRaw = reduceWith(QStringLiteral("starless"));
    const std::optional<fixtures::FixtureSamples> fromHeld = reduceWith(QStringLiteral("held"));
    ASSERT_TRUE(fromRaw.has_value() && fromHeld.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*fromRaw, *fromHeld))
        << "the largest difference is " << largestDifference(*fromRaw, *fromHeld);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    dropView(context(), heldView);
    dropView(context(), rawView);
}

// units: a layer is added back with its gain - nothing when disabled, one plus a negative bias, and one plus a positive
// bias times six for the starlet, eight at the multiscale linear transform's finest scale and six at its others.
TEST_F(MasksProcessCase, MultiscaleLayerWeightsFollowTheBias)
{
    using process_utils::multiscale::LayerSettings;
    using process_utils::multiscale::TransformRequest;
    for (const an::MultiscaleAlgorithm algorithm :
         {an::MultiscaleAlgorithm::Starlet, an::MultiscaleAlgorithm::MultiscaleLinear})
    {
        TransformRequest request;
        request.algorithm = algorithm;
        request.detailLayers = 3u;
        request.layers = {LayerSettings{.enabled = true, .bias = 0.5}, LayerSettings{.enabled = true, .bias = 0.5},
                          LayerSettings{.enabled = false, .bias = 0.5}, LayerSettings{.enabled = true, .bias = -0.25}};
        const an::Result<an::MultiscaleTransformPlan> plan = process_utils::multiscale::planTransform(request);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        const bool linear = algorithm == an::MultiscaleAlgorithm::MultiscaleLinear;
        EXPECT_EQ(process_utils::multiscale::layerWeight(request, *plan, 0u), linear ? 5.0 : 4.0);
        EXPECT_EQ(process_utils::multiscale::layerWeight(request, *plan, 1u), 4.0);
        EXPECT_EQ(process_utils::multiscale::layerWeight(request, *plan, 2u), 0.0);
        EXPECT_EQ(process_utils::multiscale::layerWeight(request, *plan, 3u), 0.75);
    }
}

// apply: the range selection's smoothing reaches the engine's widest exact Gaussian, and the selection, a mask of its
// own, is not weighed by a mask of the target.
TEST_F(MasksProcessCase, RangeSelectionSmoothsUpToItsLimitAndIgnoresTheTargetsMask)
{
    const MasksProcess& range = named("RangeSelection");
    ViewHandle target = sceneView(session(), "scene-rgb-float32", "target");
    ASSERT_NE(target, nullptr);
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), 12u, 8u, [](std::uint32_t x, std::uint32_t) { return x < 6u ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    WindowHandle plain = range.open(*target);
    range.configure(*plain, "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2");
    const std::optional<fixtures::FixtureSamples> unmasked = applyAndRead(context(), range, *plain, *target);
    plain.reset();
    ASSERT_TRUE(unmasked.has_value());

    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    WindowHandle window = range.open(*target);
    range.configure(*window, "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2");
    auto* smoothness = window->findChild<QDoubleSpinBox*>(QStringLiteral("smoothnessValue"));
    ASSERT_NE(smoothness, nullptr);
    EXPECT_EQ(smoothness->maximum(), 32.0);
    const std::optional<fixtures::FixtureSamples> masked = applyAndRead(context(), range, *window, *target);
    ASSERT_TRUE(masked.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*unmasked, *masked)) << "the target's mask weighs nothing";
    window.reset();

    WindowHandle widest = range.open(*target);
    range.configure(*widest, "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2");
    setDouble(*widest, "smoothnessValue", 32.0);
    const std::optional<fixtures::FixtureSamples> smoothest = applyAndRead(context(), range, *widest, *target);
    EXPECT_TRUE(smoothest.has_value());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    widest.reset();
    target->removeCurrentMask();
    dropView(context(), mask);
    dropView(context(), target);
}

// preview: the dark structure's mask preview builds the mask as the Apply does - a sample holding no number takes no
// part - and shows a colour image's mask over its three channels, which a mask of the target can weigh.
TEST_F(MasksProcessCase, DarkStructureMaskPreviewSkipsNumberlessSamplesAndWeighsAColourTarget)
{
    ViewHandle target = sceneWithLineage(session(), "stars-rgb-float32", true);
    ASSERT_NE(target, nullptr);
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), 32u, 24u, [](std::uint32_t x, std::uint32_t) { return x < 16u ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    WindowHandle window = named("DarkStructure").open(*target);
    named("DarkStructure").configure(*window, "-");
    setIndex(*window, "previewModeComboBox", 1);
    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window]() {
        return window->runTelemetry().previewsCommitted + window->runTelemetry().previewsFailed >= 1u;
    }));
    ASSERT_TRUE(settleWindow(*window));
    EXPECT_EQ(window->runTelemetry().previewsFailed, 0u)
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_NE(window->previewView(), nullptr);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->channelCount(), 3u);
    // The hole itself is carried over from the source by the mask's blend; nothing around it is touched by it.
    std::uint32_t numberless = 0u;
    for (std::uint32_t c = 0u; c < shown->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < shown->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < shown->width(); ++x)
            {
                const bool hole = c == 0u && x == shown->width() / 2u && y == shown->height() / 2u;
                if (!hole && !std::isfinite(shown->value(x, y, c)))
                {
                    ++numberless;
                }
            }
        }
    }
    EXPECT_EQ(numberless, 0u) << "no sample around the hole holds no number";
    // Where the target's mask shows the result, the mask still describes structure: a hole that took part would leave
    // its smoothings no number, which the mask's rescaling flattens to nothing.
    double strongest = 0.0;
    for (std::uint32_t c = 0u; c < shown->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < shown->height(); ++y)
        {
            for (std::uint32_t x = 16u; x < shown->width(); ++x)
            {
                const bool hole = c == 0u && x == shown->width() / 2u && y == shown->height() / 2u;
                if (!hole)
                {
                    strongest = (std::max)(strongest, shown->value(x, y, c));
                }
            }
        }
    }
    EXPECT_GT(strongest, 0.05) << "the mask is not flattened by the hole";
    target->removeCurrentMask();
    window.reset();
    dropView(context(), mask);
    dropView(context(), target);
}
