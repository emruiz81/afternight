// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What every one of the four stretching dialogs does the same way, each held to it in turn: its preview is the
/// production result its Apply writes; an Apply is recorded in the history before it is shown and undone back to the
/// image; the result keeps the image's facts and records the stretch and that its samples are no longer linear; the
/// target's mask weighs the result; every frame of a sequence is stretched as the dialog stretches that frame alone;
/// an image comes back in its own representation; nothing is kept between openings; and an Apply refuses the preview
/// on its way and publishes the dialog's latest values.

#include "Test_ProcessesStretching/StretchingTestSupport.h"

#include "app/PlateSolveUtils.h"
#include "processes/CurvesTransformationProcess.h"
#include "processes/GHTransformationProcess.h"
#include "processes/HistogramTransformationProcess.h"
#include "processes/StatisticalStretch.h"

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes::stretching;

using AnyWindow = WindowHandle<ImageRTPreviewControl>;

/// A window of a dialog's class over a target, as the launcher opens one.
template <typename ProcessT> [[nodiscard]] AnyWindow openAs(ImageViewControl& target)
{
    auto* process = new ProcessT(&target);
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    process->resize(900, 640);
    return AnyWindow(process);
}

/// One of the four dialogs, and what the shared cases set it to.
struct Dialog
{
    /// The process's name, which titles its history step.
    std::string name;

    std::function<AnyWindow(ImageViewControl&)> open;

    /// Sets the dialog to a stretch that changes every sample of the scenes, decided from the image's statistics
    /// where the dialog has a stretch that is.
    std::function<void(ImageRTPreviewControl&)> configure;

    /// The engine operation the result's lineage names, and the name the processing-state cards record.
    std::string engineOperation;
    std::string processingOperation;

    /// What an interactive frame may run over: the whole image, for a stretch its dialog may take from the image's
    /// statistics; what the preview view paints, for a curve the dialog's points fix alone.
    an::ui::PreviewScaling scaling = an::ui::PreviewScaling::FullOnly;
};

[[nodiscard]] std::vector<Dialog> dialogs()
{
    return {
        Dialog{.name = StatisticalStretch::getProcessNameStatic(),
               .open = openAs<StatisticalStretch>,
               .configure =
                   [](ImageRTPreviewControl& process) {
                       setValue(process, "targetMedianValue", 0.2);
                       setValue(process, "blackPointSigmaValue", 4.0);
                   },
               .engineOperation = "an::stretchImageStatistically",
               .processingOperation = "statistical-stretch"},
        Dialog{.name = HistogramTransformationProcess::getProcessNameStatic(),
               .open = openAs<HistogramTransformationProcess>,
               .configure =
                   [](ImageRTPreviewControl& process) {
                       openPreview(process);
                       auto* button = child<QAbstractButton>(process, "autostretchLinked");
                       ASSERT_NE(button, nullptr);
                       button->click();
                   },
               .engineOperation = "an::stretchImage",
               .processingOperation = "histogram-mtf"},
        Dialog{.name = GHTransformationProcess::getProcessNameStatic(),
               .open = openAs<GHTransformationProcess>,
               .configure =
                   [](ImageRTPreviewControl& process) {
                       choose(process, "transformationTypeSelector", QStringLiteral("STF Transformation"));
                   },
               .engineOperation = "an::stretchImage",
               .processingOperation = "ght-stretch"},
        Dialog{.name = CurvesTransformationProcess::getProcessNameStatic(),
               .open = openAs<CurvesTransformationProcess>,
               .configure =
                   [](ImageRTPreviewControl& process) {
                       CurvesWidget* widget = curvesWidgetOf(process);
                       ASSERT_NE(widget, nullptr);
                       widget->setCurve(an::ui::ToneCurveModel({{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}},
                                                               an::CurveInterpolation::NaturalCubic));
                   },
               .engineOperation = "an::applyChannelScope",
               .processingOperation = "curves-transformation",
               .scaling = an::ui::PreviewScaling::Decimated},
    };
}

/// Rewrites a document's meaning - its image's semantics, as `restate` changes them - and appends `card` to its cards
/// when one is given, through the document's writer.
[[nodiscard]] bool restateMeaning(const an::consumer::EngineSession& session, an::ui::ImageDocument& document,
                                  const std::function<void(an::ImageSemanticsRequest&)>& restate,
                                  const std::function<an::Result<an::CodecMetadata>(an::CodecMetadata)>& recard = {})
{
    an::Result<an::ui::ImageDocumentWriter> writer = document.write();
    EXPECT_TRUE(writer.hasValue());
    if (!writer.hasValue())
    {
        return false;
    }
    const an::ImageMetadata& metadata = std::visit(
        [](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, writer->asset().image());
    an::ImageSemanticsRequest meaning = metadata.semantics().toRequest();
    restate(meaning);
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    EXPECT_TRUE(semantics.hasValue());
    if (!semantics.hasValue())
    {
        return false;
    }
    an::Result<an::CodecMetadata> codec =
        recard ? recard(metadata.codec()) : an::Result<an::CodecMetadata>(metadata.codec());
    EXPECT_TRUE(codec.hasValue());
    if (!codec.hasValue())
    {
        return false;
    }
    an::Result<an::consumer::ImageAsset> copy = an::ui::copyImageWithMetadata(
        session, writer->asset(),
        an::ImageMetadata::create(metadata.acquisition(), *semantics, metadata.astrometry(), metadata.provenance(),
                                  std::move(*codec), metadata.outputIntent()),
        {});
    EXPECT_TRUE(copy.hasValue()) << copy.status().message();
    if (!copy.hasValue() || writer->replace(std::move(*copy)).failed())
    {
        return false;
    }
    writer->release();
    return true;
}

/// A document of a fixture image whose metadata states a linear meaning and carries a card of its own and the
/// processing-state cards the product records a linear image under.
[[nodiscard]] std::optional<an::ui::ImageDocument> linearDocument(const an::consumer::EngineSession& session,
                                                                  const std::string& fixture)
{
    std::optional<an::ui::ImageDocument> document = documentOf(session, *fixtures::fixtureNamed(fixture));
    if (!document.has_value())
    {
        return std::nullopt;
    }
    const bool restated = restateMeaning(
        session, *document,
        [](an::ImageSemanticsRequest& meaning) { meaning.transferFunction = an::TransferFunction::Linear; },
        [](an::CodecMetadata codec) -> an::Result<an::CodecMetadata> {
            const an::Result<an::CodecCard> object =
                an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "the target");
            if (!object.hasValue())
            {
                return object.status();
            }
            an::Result<an::CodecMetadata> carded = codec.appended(*object);
            if (!carded.hasValue())
            {
                return carded.status();
            }
            const an::ProcessingStateRecord linear = an::linearState(
                an::readProcessingStateCards(*carded), an::RecordedChannelBasis::LinearRgb, "calibration");
            return an::withProcessingStateCards(*carded, linear);
        });
    if (!restated)
    {
        return std::nullopt;
    }
    return document;
}

/// The metadata a document holds now.
[[nodiscard]] an::ImageMetadata metadataOf(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return {};
    }
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease->image());
}

/// A document of an image read from a sequence's frame, as a still.
[[nodiscard]] ViewHandle stillViewOf(const an::consumer::EngineSession& session, an::consumer::AnyImage&& frame)
{
    an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(session, std::move(frame));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return nullptr;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset));
    EXPECT_TRUE(document.hasValue());
    if (!document.hasValue())
    {
        return nullptr;
    }
    ViewHandle view = makeTargetView("frame", std::move(*document));
    EXPECT_TRUE(settleView(*view));
    return view;
}

/// The frames of a SER file as images.
[[nodiscard]] std::vector<an::consumer::AnyImage> frameImagesOf(const an::consumer::EngineSession& session,
                                                                const QString& path)
{
    std::vector<an::consumer::AnyImage> frames;
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return frames;
    }
    an::Result<an::ui::OpenedSequence> opened =
        an::ui::openSequenceSource(scope->engine(), scope->execution(), path, an::SequenceFormat::Ser);
    EXPECT_TRUE(opened.hasValue());
    if (!opened.hasValue())
    {
        return frames;
    }
    for (;;)
    {
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            an::ui::readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
        if (!read.hasValue() || !read->has_value())
        {
            return frames;
        }
        frames.push_back(std::move((*read)->second));
    }
}

/// What an Apply of a configured dialog writes over a fixture image, in a context of its own.
[[nodiscard]] std::optional<fixtures::FixtureSamples> appliedOver(const Dialog& dialog, const std::string& input)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    EXPECT_NE(context, nullptr);
    if (context == nullptr)
    {
        return std::nullopt;
    }
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), input);
    AnyWindow process = dialog.open(*view);
    dialog.configure(*process);
    EXPECT_TRUE(settleWindow(*process));
    process->applyProcess();
    EXPECT_TRUE(settleWindow(*process));
    EXPECT_TRUE(settleView(*view));
    return samplesOf(view->document());
}

} // namespace

// A stretch its dialog may take from the image's statistics is exact in every frame: the statistical stretch, the
// histogram transformation and the generalized hyperbolic one declare no interactive frame over a reduction of their
// target, so every frame of a drag runs the production body over the whole image as its settled frame and its Apply
// do. The curves transformation's curve is fixed by its points alone and maps every pixel by it wherever the pixel
// lies, so a drag of a point runs over what the preview view paints, as the tone dialogs' drags do. None declares that
// its body maps each channel alone.
TEST(StretchingDialogsTest, OnlyAStretchFixedByTheDialogAloneRunsAnInteractiveFrameOverAReduction)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        AnyWindow process = dialog.open(*view);
        EXPECT_EQ(process->previewScaling(), dialog.scaling);
        EXPECT_FALSE(process->mapsEachChannelAlone());
    }
}

TEST(StretchingDialogsTest, EveryPreviewIsTheProductionResultItsApplyWrites)
{
    for (const Dialog& dialog : dialogs())
    {
        for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16"})
        {
            SCOPED_TRACE(dialog.name + " over " + input);
            std::unique_ptr<ProcessTestContext> context = openProcessContext();
            ASSERT_NE(context, nullptr);
            const InstalledContext installed(*context);
            ViewHandle view = fixtureView(context->engineSession(), input);
            AnyWindow process = dialog.open(*view);
            dialog.configure(*process);
            openPreview(*process);
            const std::optional<fixtures::FixtureSamples> previewed = samplesOf(process->previewView()->document());
            process->applyProcess();
            ASSERT_TRUE(settleWindow(*process));
            ASSERT_TRUE(settleView(*view));
            const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
            ASSERT_TRUE(previewed.has_value() && applied.has_value());
            EXPECT_EQ(largestDifference(*previewed, *applied), 0.0);
            EXPECT_EQ(context->errorCount(), 0);
        }
    }
}

TEST(StretchingDialogsTest, EveryApplyIsRecordedBeforeItIsShownAndUndoneBackToTheImage)
{
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
        const std::optional<fixtures::FixtureSamples> original = samplesOf(view->document());
        AnyWindow process = dialog.open(*view);
        dialog.configure(*process);
        ASSERT_TRUE(settleWindow(*process));
        process->applyProcess();
        ASSERT_TRUE(settleWindow(*process));
        ASSERT_TRUE(settleView(*view));
        ASSERT_NE(view->viewHistory(), nullptr);
        EXPECT_EQ(view->viewHistory()->previousStepName(), dialog.name);
        const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
        ASSERT_TRUE(original.has_value() && applied.has_value());
        EXPECT_GT(largestDifference(*original, *applied), 1.0e-3);

        ASSERT_TRUE(view->viewHistory()->undo());
        ASSERT_TRUE(settleView(*view));
        ASSERT_TRUE(settleWindow(*process));
        const std::optional<fixtures::FixtureSamples> undone = samplesOf(view->document());
        ASSERT_TRUE(undone.has_value());
        EXPECT_EQ(largestDifference(*original, *undone), 0.0);
    }
}

TEST(StretchingDialogsTest, EveryResultKeepsTheImagesFactsAndRecordsAStretchThatIsNoLongerLinear)
{
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        std::optional<an::ui::ImageDocument> document = linearDocument(context->engineSession(), "scene-rgb-float32");
        ASSERT_TRUE(document.has_value());
        const an::ImageMetadata before = metadataOf(*document);
        ASSERT_EQ(before.semantics().transferFunction(), an::TransferFunction::Linear);
        ASSERT_EQ(an::readProcessingStateCards(before.codec()).linearity, an::RecordedLinearity::Linear);
        ViewHandle view = makeTargetView("linear", std::move(*document));
        ASSERT_TRUE(settleView(*view));
        AnyWindow process = dialog.open(*view);
        dialog.configure(*process);
        ASSERT_TRUE(settleWindow(*process));
        process->applyProcess();
        ASSERT_TRUE(settleWindow(*process));
        ASSERT_TRUE(settleView(*view));

        const an::ImageMetadata after = metadataOf(view->document());
        const an::CodecCard* object = after.codec().find(an::CodecNamespace::Fits, "OBJECT");
        ASSERT_NE(object, nullptr) << "the image's own card is kept";
        EXPECT_EQ(*object, *before.codec().find(an::CodecNamespace::Fits, "OBJECT"));
        EXPECT_EQ(after.acquisition(), before.acquisition());
        EXPECT_EQ(after.astrometry(), before.astrometry());
        EXPECT_EQ(after.semantics().transferFunction(), an::TransferFunction::Unspecified)
            << "a stretched sample no longer stands in proportion to the light";
        EXPECT_EQ(after.semantics().validity(), before.semantics().validity());
        const an::ProcessingStateRecord state = an::readProcessingStateCards(after.codec());
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.operation, dialog.processingOperation);
        ASSERT_FALSE(after.provenance().entries().empty());
        EXPECT_EQ(after.provenance().entries().back().operation(), dialog.engineOperation);
        EXPECT_EQ(after.provenance().entries().size(), before.provenance().entries().size() + 1u);
    }
}

TEST(StretchingDialogsTest, EveryResultIsWeighedByTheTargetsMask)
{
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        const std::optional<fixtures::FixtureSamples> unmasked = appliedOver(dialog, "scene-rgb-float32");
        ASSERT_TRUE(unmasked.has_value());
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
        const std::optional<fixtures::FixtureSamples> original = samplesOf(view->document());
        std::optional<an::ui::ImageDocument> maskDocument = an::tests::processes::greyDocument(
            context->engineSession(), 12u, 8u, [](std::uint32_t x, std::uint32_t) { return x < 6u ? 0.0f : 1.0f; });
        ASSERT_TRUE(maskDocument.has_value());
        ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
        view->setCurrentMask(mask.get());
        view->setEnableMask(true);
        ASSERT_TRUE(view->isMasked());
        {
            AnyWindow process = dialog.open(*view);
            dialog.configure(*process);
            ASSERT_TRUE(settleWindow(*process));
            process->applyProcess();
            ASSERT_TRUE(settleWindow(*process));
            ASSERT_TRUE(settleView(*view));
        }
        const std::optional<fixtures::FixtureSamples> masked = samplesOf(view->document());
        ASSERT_TRUE(original.has_value() && masked.has_value());
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < 8u; ++y)
            {
                for (std::uint32_t x = 0u; x < 12u; ++x)
                {
                    const double expected = x < 6u ? original->value(x, y, channel) : unmasked->value(x, y, channel);
                    EXPECT_NEAR(masked->value(x, y, channel), expected, 1.0e-6) << x << ',' << y << ',' << channel;
                }
            }
        }
        view->removeCurrentMask();
    }
}

TEST(StretchingDialogsTest, EverySequenceFrameIsStretchedAsTheDialogStretchesThatFrameAlone)
{
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        QTemporaryDir recordings;
        const QString path = recordings.filePath(QStringLiteral("frames.ser"));
        ImageViewControl* view = sequenceView(*context, path);
        ASSERT_NE(view, nullptr);
        {
            AnyWindow process = dialog.open(*view);
            dialog.configure(*process);
            ASSERT_TRUE(settleWindow(*process));
            process->applyProcess();
            ASSERT_TRUE(settleWindow(*process));
            ASSERT_TRUE(settleView(*view));
            EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
        }
        const std::optional<an::ui::DocumentSequence> written = view->document().sequence();
        ASSERT_TRUE(written.has_value());
        ASSERT_NE(written->sourcePath, path);
        const std::vector<fixtures::FixtureSamples> stretched = framesOf(context->engineSession(), written->sourcePath);
        std::vector<an::consumer::AnyImage> originals = frameImagesOf(context->engineSession(), path);
        ASSERT_EQ(stretched.size(), 3u);
        ASSERT_EQ(originals.size(), 3u);
        for (std::size_t frame = 0u; frame < originals.size(); ++frame)
        {
            SCOPED_TRACE(frame);
            ViewHandle still = stillViewOf(context->engineSession(), std::move(originals[frame]));
            ASSERT_NE(still, nullptr);
            AnyWindow process = dialog.open(*still);
            dialog.configure(*process);
            ASSERT_TRUE(settleWindow(*process));
            process->applyProcess();
            ASSERT_TRUE(settleWindow(*process));
            ASSERT_TRUE(settleView(*still));
            const std::optional<fixtures::FixtureSamples> alone = samplesOf(still->document());
            ASSERT_TRUE(alone.has_value());
            EXPECT_EQ(largestDifference(stretched[frame], *alone), 0.0);
        }
        // Each frame was stretched by its own statistics: the frames' gains differ, and so do their stretches.
        if (dialog.name != CurvesTransformationProcess::getProcessNameStatic())
        {
            EXPECT_GT(std::abs(stretched[0].value(12u, 8u, 0u) - stretched[2].value(12u, 8u, 0u)), 0.0);
        }
        EXPECT_EQ(context->errorCount(), 0);
    }
}

TEST(StretchingDialogsTest, EveryImageComesBackInItsOwnRepresentation)
{
    for (const Dialog& dialog : dialogs())
    {
        for (const char* input : {"scene-mono-uint8", "scene-mono-int16", "gradient-rgb-float64", "scene-rgb-uint16",
                                  "scene-rgb-float32-unbounded", "gradient-mono-uint32", "checker-rgb-int32"})
        {
            SCOPED_TRACE(dialog.name + " over " + input);
            std::unique_ptr<ProcessTestContext> context = openProcessContext();
            ASSERT_NE(context, nullptr);
            const InstalledContext installed(*context);
            ViewHandle view = fixtureView(context->engineSession(), input);
            const std::optional<an::ImageDescriptor> before = view->document().descriptor();
            const std::optional<fixtures::FixtureSamples> original = samplesOf(view->document());
            AnyWindow process = dialog.open(*view);
            dialog.configure(*process);
            ASSERT_TRUE(settleWindow(*process));
            process->applyProcess();
            ASSERT_TRUE(settleWindow(*process));
            ASSERT_TRUE(settleView(*view));
            EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
            EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral(" | ")).toStdString();
            const std::optional<an::ImageDescriptor> after = view->document().descriptor();
            ASSERT_TRUE(before.has_value() && after.has_value());
            EXPECT_EQ(after->sampleType(), before->sampleType());
            EXPECT_EQ(after->extent(), before->extent());
            EXPECT_EQ(after->numericDomain(), before->numericDomain());
            const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
            ASSERT_TRUE(original.has_value() && applied.has_value());
            EXPECT_GT(largestDifference(*original, *applied), 0.0) << "the stretch changed the image";
        }
    }
}

TEST(StretchingDialogsTest, EveryDialogOpensWithItsOwnValuesAndKeepsNothingBetweenOpenings)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
    struct Control
    {
        std::function<AnyWindow(ImageViewControl&)> open;
        const char* spinBox;
        double opensWith;
    };
    for (const Control& control : {Control{openAs<StatisticalStretch>, "targetMedianValue", 0.25},
                                   Control{openAs<HistogramTransformationProcess>, "midtonesValue", 0.5},
                                   Control{openAs<GHTransformationProcess>, "stretchFactorValue", 0.0}})
    {
        {
            AnyWindow process = control.open(*view);
            ASSERT_TRUE(settleWindow(*process));
            EXPECT_DOUBLE_EQ(child<QDoubleSpinBox>(*process, control.spinBox)->value(), control.opensWith);
            setValue(*process, control.spinBox, 0.4);
            ASSERT_TRUE(settleWindow(*process));
        }
        AnyWindow reopened = control.open(*view);
        ASSERT_TRUE(settleWindow(*reopened));
        EXPECT_DOUBLE_EQ(child<QDoubleSpinBox>(*reopened, control.spinBox)->value(), control.opensWith)
            << control.spinBox;
    }
    {
        AnyWindow process = openAs<CurvesTransformationProcess>(*view);
        ASSERT_TRUE(settleWindow(*process));
        curvesWidgetOf(*process)->setCurve(
            an::ui::ToneCurveModel({{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}}, an::CurveInterpolation::Linear));
    }
    AnyWindow reopened = openAs<CurvesTransformationProcess>(*view);
    ASSERT_TRUE(settleWindow(*reopened));
    EXPECT_TRUE(curvesWidgetOf(*reopened)->curveIsIdentity());
    EXPECT_EQ(child<QComboBox>(*reopened, "interpolationSelector")->currentText(), QStringLiteral("Cubic"));
}

// The session runs one operation at a time and the case holds it, so the preview a change asks for waits; an Apply
// clicked meanwhile refuses that preview, and once the session is let go the Apply publishes the dialog's values as
// they are then.
TEST(StretchingDialogsTest, AnApplyRefusesThePreviewOnItsWayAndPublishesTheDialogsLatestValues)
{
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        const std::optional<fixtures::FixtureSamples> expected = appliedOver(dialog, "scene-rgb-float32");
        ASSERT_TRUE(expected.has_value());
        std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
        AnyWindow process = dialog.open(*view);
        ASSERT_TRUE(settleWindow(*process));
        std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(context->engineSession());
        ASSERT_TRUE(held.has_value());
        dialog.configure(*process);
        pumpQuietly();
        process->applyProcess();
        pumpQuietly();
        held.reset();
        ASSERT_TRUE(settleWindow(*process));
        ASSERT_TRUE(settleView(*view));
        const ProcessRunTelemetry telemetry = process->runTelemetry();
        EXPECT_GE(telemetry.previewsDiscarded, 1u);
        EXPECT_EQ(telemetry.runsCommitted, 1u);
        const std::optional<fixtures::FixtureSamples> applied = samplesOf(view->document());
        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(largestDifference(*applied, *expected), 0.0);
    }
}

// A stretch decided from the image's statistics has nothing to decide from when no sample of the image is valid: the
// statistical stretch, the histogram transformation's automatic stretch and the screen transfer are refused by name,
// each Apply publishing nothing; a curve, which reads no statistics, runs.
TEST(StretchingDialogsTest, AStretchDecidedFromStatisticsIsRefusedForAnImageWithNoValidSample)
{
    for (const Dialog& dialog : dialogs())
    {
        SCOPED_TRACE(dialog.name);
        std::unique_ptr<ProcessTestContext> context = openProcessContext();
        ASSERT_NE(context, nullptr);
        const InstalledContext installed(*context);
        std::optional<an::ui::ImageDocument> document = an::tests::processes::makeDocument<float>(
            context->engineSession(), 12u, 8u, an::tests::processes::Channels::Rgb,
            [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::numeric_limits<float>::quiet_NaN(); });
        ASSERT_TRUE(document.has_value());
        ASSERT_TRUE(restateMeaning(context->engineSession(), *document, [](an::ImageSemanticsRequest& meaning) {
            meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        }));
        ViewHandle view = makeTargetView("nothing valid", std::move(*document));
        ASSERT_TRUE(settleView(*view));
        AnyWindow process = dialog.open(*view);
        dialog.configure(*process);
        ASSERT_TRUE(settleWindow(*process));
        process->applyProcess();
        ASSERT_TRUE(settleWindow(*process));
        ASSERT_TRUE(settleView(*view));
        if (dialog.name == CurvesTransformationProcess::getProcessNameStatic())
        {
            EXPECT_EQ(process->runTelemetry().runsCommitted, 1u);
            EXPECT_EQ(context->errorCount(), 0);
            continue;
        }
        EXPECT_EQ(process->runTelemetry().runsCommitted, 0u);
        EXPECT_EQ(process->runTelemetry().runsFailed, 1u);
        ASSERT_GE(context->errorCount(), 1);
        EXPECT_FALSE(view->viewHistory()->hasUndo());
        for (const QString& text : context->errorTexts)
        {
            EXPECT_TRUE(text.contains(QStringLiteral("no pixel participates")) ||
                        text.contains(QStringLiteral("selected no samples")))
                << text.toStdString();
        }
    }
}

// A signed image is read from its smallest value to its largest - an int16 sample v is (v + 32768) / 65535 -
// stretched, and written back to the nearest code; nothing below zero is lost to the stretch.
TEST(StretchingDialogsTest, ASignedImageIsReadFromItsSmallestValueToItsLargest)
{
    std::string error;
    const std::optional<fixtures::FixtureSamples> original =
        fixtures::materializeFixture(*fixtures::fixtureNamed("scene-mono-int16"), error);
    ASSERT_TRUE(original.has_value()) << error;
    const auto fraction = [](double code) { return (code + 32768.0) / 65535.0; };
    const auto code = [](double value) { return value * 65535.0 - 32768.0; };

    HistogramTransformationParams params;
    params.shadows = 0.02;
    params.midtones = 0.25;
    params.highlights = 0.95;
    const an::Result<HistogramTransfer> transfer = histogramTransferOf(params, nullptr);
    ASSERT_TRUE(transfer.hasValue());
    const an::ui::ToneCurveModel curve({{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}}, an::CurveInterpolation::NaturalCubic);
    struct Case
    {
        Dialog dialog;
        std::function<double(double)> expected;
    };
    const std::vector<Dialog> all = dialogs();
    Dialog histogram = all[1];
    histogram.configure = [](ImageRTPreviewControl& process) {
        setValue(process, "shadowsValue", 0.02);
        setValue(process, "midtonesValue", 0.25);
        setValue(process, "highlightsValue", 0.95);
    };
    const std::vector<Case> cases{Case{histogram,
                                       [&](double value) {
                                           const an::Result<double> transferred =
                                               an::evaluateMidtonesTransfer(value, transfer->channels[0]);
                                           return transferred.hasValue() ? *transferred : -1.0;
                                       }},
                                  Case{all[3], [&](double value) { return curve.evaluate(value); }}};
    int below = 0;
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.dialog.name);
        const std::optional<fixtures::FixtureSamples> applied = appliedOver(probe.dialog, "scene-mono-int16");
        ASSERT_TRUE(applied.has_value());
        for (std::uint32_t y = 0u; y < original->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < original->width(); ++x)
            {
                const double stored = original->value(x, y, 0u);
                below += stored < 0.0 ? 1 : 0;
                EXPECT_NEAR(applied->value(x, y, 0u), code(probe.expected(fraction(stored))), 1.0)
                    << x << ',' << y << " from " << stored;
            }
        }
    }
    EXPECT_GT(below, 0) << "the scene holds samples below zero";
}
