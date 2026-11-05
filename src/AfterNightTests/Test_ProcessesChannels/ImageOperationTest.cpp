// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image menu's other non-interactive actions, run as the menu runs them: duplicating a view and extracting a
/// sequence's frame, which open new views; converting to grey, to colour and to the negative, which become steps of the
/// view's history over a still image and replace a sequence frame by frame; converting a sequence's container; and the
/// mirrors and turns of a sequence - each saying why when it publishes nothing.

#include "Test_ProcessesChannels/ChannelsTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/ImageViewCompanions.h"
#include "processes/NonInteractiveProcesses.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;

/// The metadata of what a document holds now.
[[nodiscard]] an::ImageMetadata metadataOf(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return an::ImageMetadata{};
    }
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease->image());
}

/// The grey of a colour pixel, as Convert to Grayscale weighs it.
[[nodiscard]] double greyOf(const fixtures::FixtureSamples& colour, std::uint32_t x, std::uint32_t y)
{
    return 0.2989 * colour.value(x, y, 0u) + 0.5870 * colour.value(x, y, 1u) + 0.1140 * colour.value(x, y, 2u);
}

class ImageOperationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(sessionWidth());
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        ASSERT_TRUE(m_directory.isValid());
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context()
    {
        return *m_context;
    }

    [[nodiscard]] const QTemporaryDir& directory() const
    {
        return m_directory;
    }

    /// Runs an in-place operation on a still view as the menu does and waits for its edit to be shown, or for a
    /// message.
    [[nodiscard]] bool editAndSettle(ImageViewControl& view,
                                     const std::function<void(const an::ui::ImageOperationsCoordinator&)>& operation)
    {
        const std::uint64_t edits = view.workTelemetry().editsCommitted;
        const int messages = static_cast<int>(m_context->messages.size());
        operation(imageOperations());
        const bool landed = pumpUntil([&]() {
            return view.workTelemetry().editsCommitted > edits ||
                   static_cast<int>(m_context->messages.size()) > messages;
        });
        return landed && settleAll(*m_context) && view.workTelemetry().editsCommitted == edits + 1u;
    }

    /// Runs an in-place operation on a sequence view as the menu does and waits for the sequence it makes, or for a
    /// message.
    [[nodiscard]] bool replaceAndSettle(ImageViewControl& view,
                                        const std::function<void(const an::ui::ImageOperationsCoordinator&)>& operation)
    {
        const QString before = view.document().names().sourcePath;
        const int messages = static_cast<int>(m_context->messages.size());
        operation(imageOperations());
        const bool landed = pumpUntil([&]() {
            return view.document().names().sourcePath != before ||
                   static_cast<int>(m_context->messages.size()) > messages;
        });
        return landed && settleAll(*m_context) && view.document().names().sourcePath != before;
    }

private:
    shared::RegistrySnapshot m_snapshot;
    std::unique_ptr<ProcessTestContext> m_context;
    /// The session's width.
    [[nodiscard]] virtual std::uint32_t sessionWidth() const
    {
        return 4u;
    }

    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_directory;
};

/// The operations over a session one operation wide, where work that holds the only thread while it waits for an
/// image's writer stops everything.
class ImageOperationTestAtWidthOne : public ImageOperationTest
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

} // namespace

// Duplicate of a still image opens a copy beside it under its name, its history saying what it was cloned from: the
// copy has never been a file, keeps everything the image states and is an image of its own.
TEST_F(ImageOperationTest, DuplicateOpensACopyThatHasNeverBeenAFile)
{
    ViewHandle target = sceneView(context().engineSession(), "scene-rgb-uint16", "scene");
    ASSERT_NE(target, nullptr);
    an::ui::DocumentNames names = target->document().names();
    names.sourcePath = directory().filePath(QStringLiteral("scene.fits"));
    target->document().setNames(names);
    activate(context(), *target);
    const std::vector<ImageViewControl*> views = [&]() {
        imageOperations().duplicateImage();
        return awaitNewViews(context(), 0u, 1u, 0);
    }();
    ASSERT_EQ(views.size(), 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* copy = views.front();
    EXPECT_EQ(copy->getUniqueName(), "scene");
    EXPECT_EQ(copy->document().names().sourcePath, QString());
    EXPECT_EQ(madeHistoryOf(context(), copy), "Cloned from image scene");
    EXPECT_EQ(madeNeighbourOf(context(), copy), target.get());
    EXPECT_NE(copy->document().identity(), target->document().identity());
    EXPECT_NE(copy->document().asset().get(), target->document().asset().get());
    const std::optional<fixtures::FixtureSamples> original = documentSamples(target->document());
    const std::optional<fixtures::FixtureSamples> copied = documentSamples(copy->document());
    ASSERT_TRUE(original.has_value() && copied.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*original, *copied));
    EXPECT_EQ(metadataOf(copy->document()), metadataOf(target->document()));
    EXPECT_TRUE(context().messages.isEmpty());
    dropView(context(), target);
}

// Duplicate of a sequence opens the sequence again from its file in a new view, its history saying what it was cloned
// from.
TEST_F(ImageOperationTest, DuplicateOfASequenceOpensItAgainFromItsFile)
{
    const QString path =
        writeSceneSequence(context().engineSession(), directory(), {"scene-rgb-float32", "scene-rgb-float32-second"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    imageOperations().duplicateImage();
    const std::vector<ImageViewControl*> views = awaitNewViews(context(), 1u, 1u, 0);
    ASSERT_EQ(views.size(), 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(views.front()->isSequence());
    EXPECT_EQ(views.front()->document().names().sourcePath, path);
    EXPECT_EQ(madeHistoryOf(context(), views.front()), "Cloned from sequence frames");
    const std::vector<fixtures::FixtureSamples> original = sequenceFrames(context().engineSession(), *source);
    const std::vector<fixtures::FixtureSamples> cloned = sequenceFrames(context().engineSession(), *views.front());
    ASSERT_EQ(original.size(), 2u);
    ASSERT_EQ(cloned.size(), 2u);
    EXPECT_EQ(largestDifference(original[1], cloned[1]), 0.0);
}

// Extract Frame, launched through the registry, opens the frame the sequence view shows as a still image named after
// it, its history saying which frame of which sequence; a still image says it is not a sequence.
TEST_F(ImageOperationTest, ExtractFrameOpensTheShownFrameAsAStillImage)
{
    const QString path =
        writeSceneSequence(context().engineSession(), directory(), {"scene-rgb-float32", "scene-rgb-float32-second"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    const auto* descriptor = shared::registered("non_interactive.actionExtract_Frame");
    ASSERT_NE(descriptor, nullptr);
    descriptor->launch_non_interactive();
    const std::vector<ImageViewControl*> views = awaitNewViews(context(), 1u, 1u, 0);
    ASSERT_EQ(views.size(), 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_FALSE(views.front()->isSequence());
    EXPECT_EQ(views.front()->getUniqueName(), "frames - FRAME_1");
    EXPECT_EQ(madeHistoryOf(context(), views.front()), "Frame 1 extracted from sequence 'frames'");
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(source->document());
    const std::optional<fixtures::FixtureSamples> extracted = documentSamples(views.front()->document());
    ASSERT_TRUE(shown.has_value() && extracted.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*shown, *extracted));

    ViewHandle still = sceneView(context().engineSession(), "scene-rgb-float32", "still");
    activate(context(), *still);
    descriptor->launch_non_interactive();
    ASSERT_TRUE(settleAll(context()));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Extract Frame"));
    EXPECT_EQ(context().errorTexts.back(),
              QStringLiteral("Could NOT extract frame from image still because it is not a sequence"));
    dropView(context(), still);
}

// Convert to Grayscale of a still image is a step of its history, recorded before it is shown: one channel of
// 0.2989 R + 0.5870 G + 0.1140 B in the image's representation, keeping what the image states; undo brings the colour
// image back.
TEST_F(ImageOperationTest, GrayscaleIsAHistoryStepOfTheLumaWeights)
{
    ViewHandle target = sceneView(context().engineSession(), "scene-rgb-float32", "scene");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<fixtures::FixtureSamples> colour = documentSamples(target->document());
    const an::ImageMetadata before = metadataOf(target->document());
    ASSERT_TRUE(editAndSettle(*target, [](const auto& operations) { operations.convertToGrayscale(); }))
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> grey = documentSamples(target->document());
    ASSERT_TRUE(colour.has_value() && grey.has_value());
    ASSERT_EQ(grey->channelCount(), 1u);
    EXPECT_EQ(grey->sampleType(), fixtures::FixtureSampleType::Float32);
    for (std::uint32_t y = 0u; y < grey->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < grey->width(); ++x)
        {
            EXPECT_NEAR(grey->value(x, y, 0u), greyOf(*colour, x, y), 1.0e-6);
        }
    }
    const an::ImageMetadata after = metadataOf(target->document());
    EXPECT_EQ(after.acquisition(), before.acquisition());
    EXPECT_EQ(after.astrometry(), before.astrometry());
    EXPECT_EQ(after.provenance().entries().back().operation(), "an::executePointwiseProgram");
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->previousStepName(), "Convert To Grayscale");
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(settleAll(context()));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(target->document());
    ASSERT_TRUE(undone.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*colour, *undone));
    EXPECT_TRUE(context().messages.isEmpty());
    dropView(context(), target);
}

// The grey of an image every pixel of which is valid states that every pixel of it is, and carries no mask: a mask of
// every pixel would send each process run over the grey image down the engine's pixel-by-pixel path.
TEST_F(ImageOperationTest, TheGreyOfAnImageWhoseEveryPixelIsValidCarriesNoMask)
{
    for (const char* scene : {"scene-rgb-uint16", "scene-rgb-float32"})
    {
        SCOPED_TRACE(scene);
        ViewHandle target = sceneView(context().engineSession(), scene, "scene");
        ASSERT_NE(target, nullptr);
        activate(context(), *target);
        ASSERT_EQ(metadataOf(target->document()).semantics().validity().rule(), an::ValidityRule::AllValid);
        ASSERT_TRUE(editAndSettle(*target, [](const auto& operations) { operations.convertToGrayscale(); }))
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(metadataOf(target->document()).semantics().validity().rule(), an::ValidityRule::AllValid);
        {
            const an::Result<an::consumer::AssetReadLease> grey = target->document().read();
            ASSERT_TRUE(grey.hasValue());
            EXPECT_EQ(grey->validity(), nullptr);
        }
        dropView(context(), target);
    }
}

// Convert to RGB of a grey image is a step of its history: the grey plane in three channels. A mosaic asks first, and
// an answer of no runs nothing.
TEST_F(ImageOperationTest, RgbRepeatsTheGreyPlaneAndAsksBeforeConvertingAMosaic)
{
    ViewHandle target = sceneView(context().engineSession(), "scene-mono-uint8", "grey");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<fixtures::FixtureSamples> grey = documentSamples(target->document());
    ASSERT_TRUE(editAndSettle(*target, [](const auto& operations) { operations.convertToRGB(); }))
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> colour = documentSamples(target->document());
    ASSERT_TRUE(grey.has_value() && colour.has_value());
    ASSERT_EQ(colour->channelCount(), 3u);
    EXPECT_EQ(colour->sampleType(), fixtures::FixtureSampleType::UInt8);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < colour->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < colour->width(); ++x)
            {
                EXPECT_EQ(colour->value(x, y, c), grey->value(x, y, 0u));
            }
        }
    }
    EXPECT_EQ(target->viewHistory()->previousStepName(), "Convert To RGB");
    dropView(context(), target);

    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    std::optional<an::consumer::ImageAsset> mosaic = makeAsset<float>(
        context().engineSession(), 8u, 6u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + y) / 16.0f; },
        an::NumericDomain::NormalizedLinear, *rggb);
    ASSERT_TRUE(mosaic.has_value());
    ViewHandle mosaicView = assetView(std::move(*mosaic), "mosaic");
    activate(context(), *mosaicView);
    ASSERT_TRUE(mosaicView->isCFA());
    context().answerYes = false;
    const std::uint64_t edits = mosaicView->workTelemetry().editsCommitted;
    imageOperations().convertToRGB();
    ASSERT_TRUE(settleAll(context()));
    EXPECT_EQ(context().messages.back(), QStringLiteral("question:Convert CFA to RGB"));
    EXPECT_EQ(mosaicView->workTelemetry().editsCommitted, edits);
    EXPECT_EQ(mosaicView->imageDescriptor()->extent().channelCount(), 1u);
    dropView(context(), mosaicView);
}

// The negative of a still image is a step of its history: one minus a floating sample, the largest code minus an
// unsigned one, the smallest and the largest codes' sum minus a signed one, recorded nonlinear under the operation's
// name.
TEST_F(ImageOperationTest, TheNegativeIsEachSamplesComplementInItsRepresentation)
{
    for (const char* scene : {"scene-rgb-float32-unbounded", "scene-rgb-uint16", "scene-mono-int16"})
    {
        SCOPED_TRACE(scene);
        ViewHandle target = sceneView(context().engineSession(), scene, "scene");
        ASSERT_NE(target, nullptr);
        activate(context(), *target);
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        ASSERT_TRUE(editAndSettle(*target, [](const auto& operations) { operations.negativeTransformation(); }))
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> negative = documentSamples(target->document());
        ASSERT_TRUE(source.has_value() && negative.has_value());
        ASSERT_EQ(negative->sampleType(), source->sampleType());
        for (std::uint32_t c = 0u; c < source->channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < source->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < source->width(); ++x)
                {
                    const double value = source->value(x, y, c);
                    double expected = 1.0 - value;
                    if (source->sampleType() == fixtures::FixtureSampleType::UInt16)
                    {
                        expected = 65535.0 - value;
                    }
                    else if (source->sampleType() == fixtures::FixtureSampleType::Int16)
                    {
                        expected = -1.0 - value;
                    }
                    EXPECT_NEAR(negative->value(x, y, c), expected, 1.0e-6);
                }
            }
        }
        const an::ImageMetadata after = metadataOf(target->document());
        const an::ProcessingStateRecord state = an::readProcessingStateCards(after.codec());
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.operation, "negative-transform");
        EXPECT_EQ(target->viewHistory()->previousStepName(), "Negative Transform");
        dropView(context(), target);
    }
}

// The negative is weighed by the view's enabled mask, as a process's result is: where the mask is zero the image keeps
// its samples, and an inverted mask weighs the other way.
TEST_F(ImageOperationTest, TheNegativeIsWeighedByTheViewsMask)
{
    for (const bool inverted : {false, true})
    {
        SCOPED_TRACE(inverted);
        ViewHandle target = sceneView(context().engineSession(), "scene-rgb-float32", "scene");
        ViewHandle mask = sceneView(context().engineSession(), "scene-mono-float32", "mask");
        ASSERT_TRUE(target != nullptr && mask != nullptr);
        activate(context(), *target);
        target->setCurrentMask(mask.get());
        target->setEnableMask(true);
        target->setInvertMask(inverted);
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        const std::optional<fixtures::FixtureSamples> weights = documentSamples(mask->document());
        ASSERT_TRUE(editAndSettle(*target, [](const auto& operations) { operations.negativeTransformation(); }))
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> blended = documentSamples(target->document());
        ASSERT_TRUE(source.has_value() && weights.has_value() && blended.has_value());
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < source->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < source->width(); ++x)
                {
                    const double weight = inverted ? 1.0 - weights->value(x, y, 0u) : weights->value(x, y, 0u);
                    const double value = source->value(x, y, c);
                    EXPECT_NEAR(blended->value(x, y, c), value + weight * ((1.0 - value) - value), 1.0e-5);
                }
            }
        }
        target->removeCurrentMask();
        dropView(context(), mask);
        dropView(context(), target);
    }
}

// The negative reads the view's mask under a lease it takes before it asks to be admitted, in one order with its
// target's writer: an edit of the mask's view holds that writer and then asks to be admitted, so at a width of one a
// negative admitted first would hold the only thread while it waited for the writer, and the edit would wait for it.
TEST_F(ImageOperationTestAtWidthOne, TheNegativeWaitingForAnEditOfItsMaskHoldsNoThreadTheEditNeeds)
{
    ViewHandle target = sceneView(context().engineSession(), "scene-rgb-float32", "scene");
    ViewHandle mask = sceneView(context().engineSession(), "scene-mono-float32", "mask");
    ASSERT_TRUE(target != nullptr && mask != nullptr);
    activate(context(), *target);
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    const std::uint64_t edits = target->workTelemetry().editsCommitted;

    an::tests::viewer::EditHoldingTheWriter edit(*mask);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    imageOperations().negativeTransformation();
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the mask's edit waited for the thread the negative held while the negative waited for the mask's writer";

    ASSERT_TRUE(pumpUntil([&]() { return target->workTelemetry().editsCommitted > edits; }));
    ASSERT_TRUE(settleAll(context()));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    target->removeCurrentMask();
    dropView(context(), mask);
    dropView(context(), target);
}

// Over a sequence, grey, colour, the negative and every mirror and turn replace the view's sequence with one whose
// every frame went through the operation, as a step of its history; the sequence the view showed is left as it was.
TEST_F(ImageOperationTest, OverASequenceEachOperationReplacesItFrameByFrame)
{
    struct Operation
    {
        const char* step;
        std::function<void(const an::ui::ImageOperationsCoordinator&)> run;
        std::function<double(const fixtures::FixtureSamples&, std::uint32_t, std::uint32_t, std::uint32_t)> expected;
        std::uint32_t channels;
        bool turnsQuarter = false;
    };
    const std::vector<Operation> operations{
        {"Convert To Grayscale", [](const auto& o) { o.convertToGrayscale(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t) {
             return greyOf(s, x, y);
         },
         1u},
        {"Negative Transform", [](const auto& o) { o.negativeTransformation(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return 1.0 - s.value(x, y, c);
         },
         3u},
        {"Sequence Horizontal Mirror", [](const auto& o) { o.horizontalMirror(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(s.width() - 1u - x, y, c);
         },
         3u},
        {"Sequence Vertical Mirror", [](const auto& o) { o.verticalMirror(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(x, s.height() - 1u - y, c);
         },
         3u},
        {"Sequence Rotation", [](const auto& o) { o.rotate180Degree(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(s.width() - 1u - x, s.height() - 1u - y, c);
         },
         3u},
        {"Sequence Rotation", [](const auto& o) { o.rotate90DegreeRight(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(y, s.height() - 1u - x, c);
         },
         3u, true},
        {"Sequence Rotation", [](const auto& o) { o.rotate90DegreeLeft(); },
         [](const fixtures::FixtureSamples& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(s.width() - 1u - y, x, c);
         },
         3u, true},
    };
    for (const Operation& operation : operations)
    {
        SCOPED_TRACE(operation.step);
        const QString path = writeSceneSequence(
            context().engineSession(), directory(), {"scene-rgb-float32", "scene-rgb-float32-second"},
            an::SequenceFormat::ImageList, QStringLiteral("frames-%1").arg(context().madeViews.size()));
        ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList);
        ASSERT_NE(source, nullptr);
        const std::vector<fixtures::FixtureSamples> before = sequenceFrames(context().engineSession(), *source);
        ASSERT_TRUE(replaceAndSettle(*source, operation.run))
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_TRUE(source->isSequence());
        EXPECT_EQ(source->viewHistory()->previousStepName(), operation.step);
        const std::vector<fixtures::FixtureSamples> after = sequenceFrames(context().engineSession(), *source);
        ASSERT_EQ(after.size(), before.size());
        for (std::size_t frame = 0u; frame < after.size(); ++frame)
        {
            const fixtures::FixtureSamples& was = before[frame];
            const fixtures::FixtureSamples& is = after[frame];
            ASSERT_EQ(is.channelCount(), operation.channels);
            ASSERT_EQ(is.width(), operation.turnsQuarter ? was.height() : was.width());
            for (std::uint32_t c = 0u; c < is.channelCount(); ++c)
            {
                for (std::uint32_t y = 0u; y < is.height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < is.width(); ++x)
                    {
                        EXPECT_NEAR(is.value(x, y, c), operation.expected(was, x, y, c), 1.0e-6);
                    }
                }
            }
        }
        EXPECT_TRUE(QFile::exists(path)) << "the sequence the view showed is left as it was";
        EXPECT_TRUE(source->tryRequestLockForProcessing());
        source->releaseLockForProcessing();
    }
    EXPECT_TRUE(context().messages.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
}

// Convert to SER and Convert to Image List write every frame into the other container and show that sequence in the
// view, as a step of its history naming both containers; converting to the container the sequence is in is refused.
TEST_F(ImageOperationTest, AContainerConversionShowsTheSequenceWrittenInTheOtherContainer)
{
    const QString path =
        writeSceneSequence(context().engineSession(), directory(), {"scene-rgb-uint16", "scene-rgb-uint16"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    const std::vector<fixtures::FixtureSamples> before = sequenceFrames(context().engineSession(), *source);

    ASSERT_TRUE(replaceAndSettle(*source, [](const auto& operations) { operations.convertToSERSeq(); }))
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(source->isSERSequence());
    EXPECT_TRUE(source->document().names().sourcePath.endsWith(QStringLiteral(".ser")));
    EXPECT_EQ(source->viewHistory()->previousStepName(), "Convert Sequence Type");
    std::vector<fixtures::FixtureSamples> after = sequenceFrames(context().engineSession(), *source);
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(largestDifference(after[0], before[0]), 0.0);

    imageOperations().convertToSERSeq();
    ASSERT_TRUE(settleAll(context()));
    EXPECT_EQ(context().errorTexts.back(), QStringLiteral("The selected sequence is already in SER format"));

    ASSERT_TRUE(replaceAndSettle(*source, [](const auto& operations) { operations.convertToImageListSeq(); }))
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(source->isImageListSequence());
    after = sequenceFrames(context().engineSession(), *source);
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(largestDifference(after[1], before[1]), 0.0);
}

// A still image's mirrors and turns are the view's own edits, named as the view names them.
TEST_F(ImageOperationTest, AStillImagesMirrorIsTheViewsOwnEdit)
{
    ViewHandle target = sceneView(context().engineSession(), "scene-rgb-uint16", "scene");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    ASSERT_TRUE(editAndSettle(*target, [](const auto& operations) { operations.horizontalMirror(); }));
    EXPECT_EQ(target->viewHistory()->previousStepName(), "Image Horizontal Mirror");
    const std::optional<fixtures::FixtureSamples> mirrored = documentSamples(target->document());
    ASSERT_TRUE(source.has_value() && mirrored.has_value());
    EXPECT_EQ(mirrored->value(0u, 0u, 0u), source->value(source->width() - 1u, 0u, 0u));
    dropView(context(), target);
}

// error: an in-place operation that cannot run says why, in a message titled by the operation, and leaves the image -
// or the sequence, and nothing in the temporary directory - as it was.
TEST_F(ImageOperationTest, AnOperationThatCannotRunSaysWhyAndLeavesTheImage)
{
    ViewHandle grey = sceneView(context().engineSession(), "scene-mono-float32", "grey");
    ASSERT_NE(grey, nullptr);
    activate(context(), *grey);
    imageOperations().convertToGrayscale();
    ASSERT_TRUE(awaitMessage(context(), 0));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Convert to grayscale"));
    EXPECT_TRUE(
        context().errorTexts.back().startsWith(QStringLiteral("Could not convert the image 'grey' to grayscale")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(grey->imageDescriptor()->extent().channelCount(), 1u);
    EXPECT_EQ(grey->workTelemetry().editsCommitted, 0u);
    dropView(context(), grey);

    const QString path = writeSceneSequence(context().engineSession(), directory(), {"scene-rgb-float32"});
    ImageViewControl* colour = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(colour, nullptr);
    const QStringList left =
        QDir(context().getTempDirPath()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    imageOperations().convertToRGB();
    ASSERT_TRUE(awaitMessage(context(), 1));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Convert to RGB"));
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("not one grey channel")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(colour->document().names().sourcePath, path);
    EXPECT_EQ(QDir(context().getTempDirPath()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot), left);
    EXPECT_TRUE(colour->tryRequestLockForProcessing());
    colour->releaseLockForProcessing();
}

// error: with no view, each operation says there is nothing to run on, and runs nothing.
TEST_F(ImageOperationTest, WithNoViewEachOperationSaysThereIsNothingToRunOn)
{
    context().setActiveImageViewControl(nullptr);
    const an::ui::ImageOperationsCoordinator operations = imageOperations();
    operations.duplicateImage();
    operations.convertToGrayscale();
    operations.convertToRGB();
    operations.negativeTransformation();
    operations.convertToSERSeq();
    operations.convertToImageListSeq();
    ASSERT_TRUE(settleAll(context()));
    EXPECT_EQ(
        context().messages,
        (QStringList{QStringLiteral("error:Image duplicate error"), QStringLiteral("error:Convert to grayscale"),
                     QStringLiteral("error:Convert to RGB"), QStringLiteral("error:Image negative transformation"),
                     QStringLiteral("error:Convert to SER sequence"),
                     QStringLiteral("error:Convert to image list sequence")}));
    EXPECT_TRUE(context().madeViews.empty());
}

// Convert to RGB over a grey sequence replaces it with one whose every frame is the grey plane in three channels, as a
// step of its history.
TEST_F(ImageOperationTest, RgbOverAGreySequenceRepeatsEachFramesPlane)
{
    const QString path =
        writeSceneSequence(context().engineSession(), directory(), {"scene-mono-float32", "scene-mono-float32-second"});
    ImageViewControl* source = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(source, nullptr);
    const std::vector<fixtures::FixtureSamples> before = sequenceFrames(context().engineSession(), *source);
    ASSERT_TRUE(replaceAndSettle(*source, [](const auto& operations) { operations.convertToRGB(); }))
        << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(source->viewHistory()->previousStepName(), "Convert To RGB");
    const std::vector<fixtures::FixtureSamples> after = sequenceFrames(context().engineSession(), *source);
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t frame = 0u; frame < after.size(); ++frame)
    {
        ASSERT_EQ(after[frame].channelCount(), 3u);
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < after[frame].height(); ++y)
            {
                for (std::uint32_t x = 0u; x < after[frame].width(); ++x)
                {
                    EXPECT_EQ(after[frame].value(x, y, c), before[frame].value(x, y, 0u));
                }
            }
        }
    }
    EXPECT_TRUE(context().messages.isEmpty());
}

// error: Extract Frame with no view says there is no sequence, as the launcher said it, and opens nothing.
TEST_F(ImageOperationTest, ExtractFrameWithNoViewSaysThereIsNoSequence)
{
    context().setActiveImageViewControl(nullptr);
    const auto* descriptor = shared::registered("non_interactive.actionExtract_Frame");
    ASSERT_NE(descriptor, nullptr);
    descriptor->launch_non_interactive();
    ASSERT_TRUE(settleAll(context()));
    EXPECT_EQ(context().messages, QStringList{QStringLiteral("error:Extract frame error")});
    EXPECT_EQ(context().errorTexts, QStringList{QStringLiteral("No selected sequence")});
    EXPECT_TRUE(context().madeViews.empty());
}
