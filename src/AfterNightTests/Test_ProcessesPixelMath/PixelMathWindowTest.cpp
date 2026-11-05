// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The PixelMath window as a person runs it: launched through the registry, applied in place as one history step that
/// undoes, opening a new image or a new sequence beside its target, stopped, read when Apply was pressed, refusing an
/// expression with its reason, keeping every representation, running over every frame of a sequence, reading the
/// target's mask without applying it, and keeping nothing between openings.

#include "Test_ProcessesPixelMath/PixelMathTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "controls/ImageViewCompanions.h"
#include "controls/PixelMathVocabulary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::pixelmath;
using an::ui::processes::PixelMathExpressionTab;

class PixelMathWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(sessionWidth());
        ASSERT_NE(m_context, nullptr);
        m_installed = std::make_unique<InstalledContext>(*m_context);
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

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A view of a scene made active.
    [[nodiscard]] ViewHandle activeScene(std::string_view scene, const std::string& name = "scene")
    {
        ViewHandle view = sceneView(session(), scene, name);
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            activate(context(), *view);
        }
        return view;
    }

    /// The session's width.
    [[nodiscard]] virtual std::uint32_t sessionWidth() const
    {
        return 4u;
    }

    std::unique_ptr<ProcessTestContext> m_context;
    std::unique_ptr<InstalledContext> m_installed;
};

/// The window over a session one operation wide, where work that holds the only thread while it waits for an
/// image's writer stops everything.
class PixelMathWindowTestAtWidthOne : public PixelMathWindowTest
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

/// Every sample of one image the double of another's, as fractions of a floating image.
void expectScaled(const fixtures::FixtureSamples& made, const fixtures::FixtureSamples& source, double factor)
{
    ASSERT_EQ(made.width(), source.width());
    ASSERT_EQ(made.height(), source.height());
    ASSERT_EQ(made.channelCount(), source.channelCount());
    for (std::uint32_t channel = 0u; channel < made.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < made.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < made.width(); ++x)
            {
                const double expected = source.value(x, y, channel) * factor;
                EXPECT_NEAR(made.value(x, y, channel), expected, 1e-6 * (1.0 + std::fabs(expected)))
                    << "at " << x << ", " << y << ", channel " << channel;
            }
        }
    }
}

// launch: the registered action opens the window attached to the active view under its name, sharing the view.
TEST_F(PixelMathWindowTest, TheRegisteredActionOpensTheWindow)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = activeScene("scene-rgb-float32");
    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(kDescriptorId);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->action_object_name, kAction);
    EXPECT_EQ(descriptor->process_name, "PixelMath");
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(target.get());
    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, "PixelMathProcess");
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    auto* window = dynamic_cast<ImageProcessControl*>(context().openProcesses.front());
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->isVisible());
    EXPECT_EQ(window->getProcessName(), "PixelMath");
    EXPECT_EQ(window->attachmentName(), "PixelMath");
    EXPECT_FALSE(window->requiresExclusiveAccess());
    window->close();
    ASSERT_TRUE(settleAll(context()));
    dropView(context(), target);
}

// apply and history: an Apply replaces the image as one history step named PixelMath, which undoes.
TEST_F(PixelMathWindowTest, AnApplyIsOneHistoryStepThatUndoes)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value());
    expectScaled(*made, *source, 0.5);
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), "PixelMath");
    window.reset();
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleAll(context()));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(target->document());
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->digest(), source->digest());
    dropView(context(), target);
}

// new image: "Create new image" opens the result beside the target under the dialog's name - PIXEL_MATH_IMAGE when it
// is blank - beginning its history with the script's step, and leaves the target as it was.
TEST_F(PixelMathWindowTest, CreateNewImageOpensTheResultBesideTheTarget)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 2"));
    setComboText(*window, "outputModeSelector", QStringLiteral("Create new image"));
    setLineText(*window, "newImadeIdEdit", QStringLiteral("DOUBLED"));
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* made = context().madeViews.back();
    EXPECT_EQ(made->getUniqueName(), "DOUBLED");
    EXPECT_EQ(madeHistoryOf(context(), made), "Image created from PixelMath script");
    EXPECT_EQ(madeNeighbourOf(context(), made), target.get());
    const std::optional<fixtures::FixtureSamples> doubled = documentSamples(made->document());
    ASSERT_TRUE(doubled.has_value() && source.has_value());
    expectScaled(*doubled, *source, 2.0);
    const std::optional<fixtures::FixtureSamples> kept = documentSamples(target->document());
    EXPECT_EQ(kept->digest(), source->digest()) << "the target changed";

    setLineText(*window, "newImadeIdEdit", QStringLiteral("  "));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 2u);
    EXPECT_EQ(context().madeViews.back()->getUniqueName(), "PIXEL_MATH_IMAGE");
    window.reset();
    dropView(context(), target);
}

// cancel: a run whose stop is asked for publishes nothing, says nothing, and leaves the image and its history.
TEST_F(PixelMathWindowTest, AStoppedRunPublishesNothing)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = openStoppingWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    const int messages = static_cast<int>(context().messages.size());
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> kept = documentSamples(target->document());
    ASSERT_TRUE(kept.has_value() && source.has_value());
    EXPECT_EQ(kept->digest(), source->digest());
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(static_cast<int>(context().messages.size()), messages) << "a stop said something";

    setComboText(*window, "outputModeSelector", QStringLiteral("Create new image"));
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(context().madeViews.size(), before);
    window.reset();
    dropView(context(), target);
}

// parameter-snapshot: a run reads the dialog as it was when Apply was pressed.
TEST_F(PixelMathWindowTest, ARunReadsTheDialogAsItWasWhenApplyWasPressed)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    pressApply(*window);
    // The dialog changes before the run's worker has landed anything.
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0"));
    setChecked(*window, "rescaleResult", true);
    ASSERT_TRUE(settleWindow(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value() && source.has_value());
    expectScaled(*made, *source, 0.5);
    window.reset();
    dropView(context(), target);
}

// random: the window draws a new seed at every Apply, so two Applies of random() make two images.
TEST_F(PixelMathWindowTest, EveryApplyDrawsANewRandomSeed)
{
    ViewHandle target = activeScene("scene-mono-float32");
    WindowHandle window(new PixelMathProcess(target.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("random()"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> first = documentSamples(target->document());
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> second = documentSamples(target->document());
    ASSERT_TRUE(first.has_value() && second.has_value());
    EXPECT_NE(first->digest(), second->digest()) << "two Applies drew the same numbers";
    window.reset();
    dropView(context(), target);
}

// error: an expression the engine refuses is said under "Error running PixelMath", naming its tab and its reason, and
// changes nothing; the window can run again.
TEST_F(PixelMathWindowTest, ARefusedExpressionSaysWhyAndChangesNothing)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * (1 +"));
    const int errors = context().errorCount();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().errorCount(), errors + 1);
    EXPECT_TRUE(context().messages.contains(QStringLiteral("error:Error running PixelMath")))
        << context().messages.join(QStringLiteral("; ")).toStdString();
    const QString text = context().errorTexts.back();
    EXPECT_TRUE(text.startsWith(QStringLiteral("An error occurred when executing the PixelMath process:")))
        << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("RGB/K expression"))) << text.toStdString();
    const std::optional<fixtures::FixtureSamples> kept = documentSamples(target->document());
    EXPECT_EQ(kept->digest(), source->digest());
    EXPECT_FALSE(target->viewHistory()->hasUndo());

    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_TRUE(target->viewHistory()->hasUndo());
    window.reset();
    dropView(context(), target);
}

// representation: every representation is run as fractions of full scale and kept.
TEST_F(PixelMathWindowTest, EveryRepresentationIsKept)
{
    const an::SampleType types[] = {an::SampleType::UInt8,   an::SampleType::Int8,   an::SampleType::UInt16,
                                    an::SampleType::Int16,   an::SampleType::UInt32, an::SampleType::Int32,
                                    an::SampleType::Float32, an::SampleType::Float64};
    for (const an::SampleType type : types)
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        const bool floating = type == an::SampleType::Float32 || type == an::SampleType::Float64;
        std::optional<an::consumer::ImageAsset> asset = filledAsset(
            session(), 6u, 4u, 3u, type, an::NumericDomain::NormalizedLinear,
            [floating](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                return floating ? 0.1 * (x + 1) + 0.01 * y + 0.001 * c : static_cast<double>(10 * (x + y + c));
            });
        ASSERT_TRUE(asset.has_value());
        ViewHandle target = assetView(std::move(*asset), "target");
        activate(context(), *target);
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        WindowHandle window = openWindow(target.get());
        setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T"));
        ASSERT_TRUE(applyAndSettle(context(), *window));
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
        ASSERT_TRUE(made.has_value() && source.has_value());
        EXPECT_EQ(made->sampleType(), source->sampleType());
        EXPECT_EQ(made->digest(), source->digest()) << "$T did not give the image back";
        window.reset();
        dropView(context(), target);
    }
}

// sequence: over a sequence every frame becomes what the still image of it becomes - replaced as one history step,
// or opened as a new sequence under the dialog's name.
TEST_F(PixelMathWindowTest, EveryFrameOfASequenceIsWhatItsStillImageBecomes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(session(), directory, {"scene-rgb-float32", "scene-rgb-float32-second"});
    ASSERT_FALSE(path.isEmpty());
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    const std::vector<fixtures::FixtureSamples> frames = sequenceFrames(session(), *sequence);
    ASSERT_EQ(frames.size(), 2u);

    WindowHandle window = openWindow(sequence);
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    setComboText(*window, "outputModeSelector", QStringLiteral("Create new image"));
    setLineText(*window, "newImadeIdEdit", QStringLiteral("HALVED"));
    const std::size_t before = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    ASSERT_EQ(context().madeViews.size(), before + 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ImageViewControl* made = context().madeViews.back();
    ASSERT_TRUE(made->isSequence());
    EXPECT_EQ(made->getUniqueName(), "HALVED");
    EXPECT_EQ(madeHistoryOf(context(), made), "Sequence created from PixelMath script");
    const std::vector<fixtures::FixtureSamples> halved = sequenceFrames(session(), *made);
    ASSERT_EQ(halved.size(), 2u);
    for (std::size_t frame = 0u; frame < halved.size(); ++frame)
    {
        SCOPED_TRACE(frame);
        expectScaled(halved[frame], frames[frame], 0.5);
    }

    setComboText(*window, "outputModeSelector", QStringLiteral("Replace target image"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(context().madeViews.size(), before + 1u);
    const std::vector<fixtures::FixtureSamples> replaced = sequenceFrames(session(), *sequence);
    ASSERT_EQ(replaced.size(), 2u);
    for (std::size_t frame = 0u; frame < replaced.size(); ++frame)
    {
        SCOPED_TRACE(frame);
        expectScaled(replaced[frame], frames[frame], 0.5);
    }
    ASSERT_NE(sequence->viewHistory(), nullptr);
    EXPECT_EQ(sequence->viewHistory()->previousStepName(), "PixelMath");
    window.reset();
}

// A sequence's frames draw their random numbers from streams of their own.
TEST_F(PixelMathWindowTest, EveryFrameOfASequenceDrawsItsOwnRandomNumbers)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(session(), directory, {"scene-rgb-float32", "scene-rgb-float32"});
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    WindowHandle window = openWindow(sequence);
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("random()"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::vector<fixtures::FixtureSamples> drawn = sequenceFrames(session(), *sequence);
    ASSERT_EQ(drawn.size(), 2u);
    EXPECT_NE(drawn[0].digest(), drawn[1].digest()) << "two frames drew the same numbers";
    window.reset();
}

// apply-mask: the target's mask is what `$M` reads, and is not applied to the result.
TEST_F(PixelMathWindowTest, TheMaskIsReadAsMAndNotApplied)
{
    ViewHandle target = activeScene("scene-mono-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    std::optional<an::consumer::ImageAsset> maskAsset = filledAsset(
        session(), source->width(), source->height(), 1u, an::SampleType::Float32, an::NumericDomain::NormalizedLinear,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x % 2u == 0u ? 0.0 : 1.0; });
    ASSERT_TRUE(maskAsset.has_value());
    ViewHandle mask = assetView(std::move(*maskAsset), "mask");
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    activate(context(), *target);

    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 2"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> doubled = documentSamples(target->document());
    ASSERT_TRUE(doubled.has_value());
    expectScaled(*doubled, *source, 2.0);

    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$M"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> read = documentSamples(target->document());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->value(0u, 0u, 0u), 0.0);
    EXPECT_EQ(read->value(1u, 0u, 0u), 1.0);
    window.reset();
    target->removeCurrentMask();
    dropView(context(), target);
    dropView(context(), mask);
}

// Another view is read by its name, resampled to the target's size when it has another.
TEST_F(PixelMathWindowTest, AnotherViewIsReadByItsName)
{
    ViewHandle other = activeScene("scene-mono-float32", "other");
    ViewHandle target = activeScene("scene-rgb-float32", "target");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    const std::optional<fixtures::FixtureSamples> named = documentSamples(other->document());
    ASSERT_TRUE(source.has_value() && named.has_value());
    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T - other"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value());
    if (named->width() == source->width() && named->height() == source->height())
    {
        EXPECT_NEAR(made->value(3u, 2u, 1u), static_cast<float>(source->value(3u, 2u, 1u) - named->value(3u, 2u, 0u)),
                    1e-6);
    }
    window.reset();
    dropView(context(), target);
    dropView(context(), other);
}

// leases: a run reads the views its expressions name under leases it takes before it asks to be admitted, in one order
// with its target's writer. An edit of a named view holds that view's writer and then asks to be admitted, so at a
// width of one a run admitted first would hold the only thread while it waited for the writer, and the edit would wait
// for the thread: neither would end.
TEST_F(PixelMathWindowTestAtWidthOne, AnApplyWaitingForAnEditOfAViewItNamesHoldsNoThreadTheEditNeeds)
{
    ViewHandle other = activeScene("scene-mono-float32", "other");
    ViewHandle target = activeScene("scene-rgb-float32", "target");
    WindowHandle window = openWindow(target.get());
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T - other"));

    an::tests::viewer::EditHoldingTheWriter edit(*other);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    pressApply(*window);
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the run held while the run waited for the edit's writer";

    ASSERT_TRUE(settleWindow(context(), *window));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_TRUE(target->viewHistory()->hasUndo()) << "the run was not shown";
    window.reset();
    dropView(context(), target);
    dropView(context(), other);
}

// persistence: a window keeps nothing between openings, and shows a settings file's values as it imports them.
TEST_F(PixelMathWindowTest, AWindowKeepsNothingBetweenOpeningsAndShowsImportedSettings)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    WindowHandle window = openWindow(target.get());
    const an::ui::processes::PixelMathSettings defaults = window->settings();
    EXPECT_TRUE(defaults.expressions[0].empty());
    EXPECT_EQ(defaults.outputMode, an::ui::processes::PixelMathOutputMode::ReplaceTarget);
    EXPECT_TRUE(defaults.singleExpression);
    EXPECT_FALSE(defaults.rescale);
    EXPECT_EQ(defaults.rescaleHigh, 1.0);
    EXPECT_EQ(defaults.newImageId, "PIXEL_MATH_IMAGE");
    EXPECT_EQ(defaults.colorSpace, an::ui::processes::PixelMathColorSpace::AsTarget);
    EXPECT_EQ(defaults.colorFormat, an::ui::processes::PixelMathColorFormat::AsTarget);

    an::ui::processes::PixelMathSettings imported = defaults;
    imported.expressions = {"$T * 2", "$T", "$T * 0", ""};
    imported.singleExpression = false;
    imported.localSymbols = "a = 1";
    imported.globalSymbols = "k = 3";
    imported.rescale = true;
    imported.rescaleLow = 0.25;
    imported.outputMode = an::ui::processes::PixelMathOutputMode::CreateNewImage;
    imported.newImageId = "IMPORTED";
    imported.width = 10u;
    imported.colorSpace = an::ui::processes::PixelMathColorSpace::Rgb;
    imported.colorFormat = an::ui::processes::PixelMathColorFormat::UInt16;
    imported.codeOptimization = false;
    imported.singleThreaded = true;
    window->applySettings(imported);
    const an::ui::processes::PixelMathSettings shown = window->settings();
    EXPECT_EQ(shown.expressions, imported.expressions);
    EXPECT_EQ(shown.singleExpression, imported.singleExpression);
    EXPECT_EQ(shown.localSymbols, imported.localSymbols);
    EXPECT_EQ(shown.globalSymbols, imported.globalSymbols);
    EXPECT_EQ(shown.rescale, imported.rescale);
    EXPECT_EQ(shown.rescaleLow, imported.rescaleLow);
    EXPECT_EQ(shown.outputMode, imported.outputMode);
    EXPECT_EQ(shown.newImageId, imported.newImageId);
    EXPECT_EQ(shown.width, imported.width);
    EXPECT_EQ(shown.colorSpace, imported.colorSpace);
    EXPECT_EQ(shown.colorFormat, imported.colorFormat);
    EXPECT_FALSE(shown.codeOptimization);
    EXPECT_TRUE(shown.singleThreaded);
    window.reset();

    WindowHandle again = openWindow(target.get());
    const an::ui::processes::PixelMathSettings reopened = again->settings();
    EXPECT_TRUE(reopened.expressions[0].empty());
    EXPECT_EQ(reopened.outputMode, an::ui::processes::PixelMathOutputMode::ReplaceTarget);
    again.reset();
    dropView(context(), target);
}

// The colour space a new image takes from its target is the dialog's first choice, "As target": over a colour target
// it offers an expression per colour channel, over a grey one it does not.
TEST_F(PixelMathWindowTest, AsTargetTakesTheTargetsOwnChannels)
{
    ViewHandle colour = activeScene("scene-rgb-float32", "colour");
    WindowHandle window = openWindow(colour.get());
    auto* selector = widgetOf<QComboBox>(*window, "colorSpaceSelector");
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->itemText(0), QStringLiteral("As target"));
    setComboText(*window, "outputModeSelector", QStringLiteral("Create new image"));
    setComboText(*window, "colorSpaceSelector", QStringLiteral("As target"));
    auto* single = widgetOf<QCheckBox>(*window, "useSingleRGBKExpression");
    ASSERT_NE(single, nullptr);
    EXPECT_TRUE(single->isEnabled()) << "a colour target's own channels offer an expression per channel";
    EXPECT_EQ(window->settings().colorSpace, an::ui::processes::PixelMathColorSpace::AsTarget);
    window.reset();
    dropView(context(), colour);

    ViewHandle grey = activeScene("scene-mono-float32", "grey");
    WindowHandle greyWindow = openWindow(grey.get());
    setComboText(*greyWindow, "outputModeSelector", QStringLiteral("Create new image"));
    setComboText(*greyWindow, "colorSpaceSelector", QStringLiteral("As target"));
    EXPECT_FALSE(widgetOf<QCheckBox>(*greyWindow, "useSingleRGBKExpression")->isEnabled());
    greyWindow.reset();
    dropView(context(), grey);
}

// The alpha expression tab is offered only while the output has an alpha channel: a window opened over a colour or a
// grey target without one does not offer it, since its expression would be read for no channel; a new RGBA image does.
TEST_F(PixelMathWindowTest, TheAlphaTabIsOfferedOnlyForAnOutputWithAlpha)
{
    constexpr int kAlphaTab = 3;
    ViewHandle colour = activeScene("scene-rgb-float32", "colour");
    WindowHandle window = openWindow(colour.get());
    auto* tabs = widgetOf<QTabWidget>(*window, "tabWidget");
    ASSERT_NE(tabs, nullptr);
    EXPECT_FALSE(tabs->isTabEnabled(kAlphaTab)) << "a three-channel target has no alpha channel to write";
    EXPECT_TRUE(widgetOf<QCheckBox>(*window, "useSingleRGBKExpression")->isEnabled());
    setComboText(*window, "outputModeSelector", QStringLiteral("Create new image"));
    setComboText(*window, "colorSpaceSelector", QStringLiteral("RGBA"));
    EXPECT_TRUE(tabs->isTabEnabled(kAlphaTab)) << "a new RGBA image has an alpha channel";
    setComboText(*window, "colorSpaceSelector", QStringLiteral("RGB"));
    EXPECT_FALSE(tabs->isTabEnabled(kAlphaTab));
    setComboText(*window, "colorSpaceSelector", QStringLiteral("RGBA"));
    setComboText(*window, "outputModeSelector", QStringLiteral("Replace target image"));
    EXPECT_FALSE(tabs->isTabEnabled(kAlphaTab)) << "replacing the target writes the target's three channels";
    window.reset();
    dropView(context(), colour);

    ViewHandle grey = activeScene("scene-mono-float32", "grey");
    WindowHandle greyWindow = openWindow(grey.get());
    auto* greyTabs = widgetOf<QTabWidget>(*greyWindow, "tabWidget");
    ASSERT_NE(greyTabs, nullptr);
    EXPECT_FALSE(greyTabs->isTabEnabled(kAlphaTab));
    EXPECT_FALSE(greyTabs->isTabEnabled(1));
    EXPECT_FALSE(greyTabs->isTabEnabled(2));
    EXPECT_FALSE(widgetOf<QCheckBox>(*greyWindow, "useSingleRGBKExpression")->isEnabled());
    greyWindow.reset();
    dropView(context(), grey);
}

// Settings read from a file are offered by the same rule as the target: a grey target writes one channel whatever the
// file asked for, so the green and blue programs stay closed.
TEST_F(PixelMathWindowTest, ImportedSettingsOfferOnlyTheTabsTheTargetWrites)
{
    ViewHandle grey = activeScene("scene-mono-float32", "grey");
    WindowHandle window = openWindow(grey.get());
    auto* tabs = widgetOf<QTabWidget>(*window, "tabWidget");
    ASSERT_NE(tabs, nullptr);
    an::ui::processes::PixelMathSettings separate = window->settings();
    separate.singleExpression = false;
    separate.outputMode = an::ui::processes::PixelMathOutputMode::ReplaceTarget;
    window->applySettings(separate);
    EXPECT_FALSE(tabs->isTabEnabled(1));
    EXPECT_FALSE(tabs->isTabEnabled(2));
    EXPECT_FALSE(tabs->isTabEnabled(3));
    EXPECT_FALSE(widgetOf<QCheckBox>(*window, "useSingleRGBKExpression")->isEnabled());
    window.reset();
    dropView(context(), grey);

    // A colour target writes three channels, so the same settings open the green and blue programs.
    ViewHandle colour = activeScene("scene-rgb-float32", "colour");
    WindowHandle colourWindow = openWindow(colour.get());
    auto* colourTabs = widgetOf<QTabWidget>(*colourWindow, "tabWidget");
    ASSERT_NE(colourTabs, nullptr);
    colourWindow->applySettings(separate);
    EXPECT_TRUE(colourTabs->isTabEnabled(1));
    EXPECT_TRUE(colourTabs->isTabEnabled(2));
    EXPECT_FALSE(colourTabs->isTabEnabled(3));
    colourWindow.reset();
    dropView(context(), colour);
}

// Replacing the target keeps its size, channels and representation whatever the disabled output controls hold - a
// choice left from a new image or an import.
TEST_F(PixelMathWindowTest, ReplacingTheTargetKeepsItsOwnDescription)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    ASSERT_TRUE(source.has_value());
    WindowHandle window = openWindow(target.get());
    an::ui::processes::PixelMathSettings left = window->settings();
    left.colorSpace = an::ui::processes::PixelMathColorSpace::Grayscale;
    left.colorFormat = an::ui::processes::PixelMathColorFormat::UInt8;
    left.width = 3u;
    left.height = 2u;
    window->applySettings(left);
    setComboText(*window, "outputModeSelector", QStringLiteral("Replace target image"));
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * 0.5"));
    ASSERT_TRUE(applyAndSettle(context(), *window));
    const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->sampleType(), fixtures::FixtureSampleType::Float32);
    expectScaled(*made, *source, 0.5);
    window.reset();
    dropView(context(), target);
}

// Every expression is evaluated in double precision: the dialog says so and offers no choice, and imported settings
// cannot clear it.
TEST_F(PixelMathWindowTest, EveryExpressionIsEvaluatedInDoublePrecisionAndTheDialogSaysSo)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    WindowHandle window = openWindow(target.get());
    auto* box = widgetOf<QCheckBox>(*window, "processIn64Bits");
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->isChecked());
    EXPECT_FALSE(box->isEnabled());
    an::ui::processes::PixelMathSettings imported = window->settings();
    imported.processIn64Bits = false;
    window->applySettings(imported);
    EXPECT_TRUE(box->isChecked());
    EXPECT_TRUE(window->settings().processIn64Bits);
    window.reset();
    dropView(context(), target);
}

// A symbol named as a function is refused on every frame of a sequence, the first among them, and the sequence is
// left as it was.
TEST_F(PixelMathWindowTest, ASymbolNamedAsAFunctionIsRefusedOverASequence)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(session(), directory, {"scene-rgb-float32", "scene-rgb-float32-second"});
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    const std::vector<fixtures::FixtureSamples> before = sequenceFrames(session(), *sequence);
    WindowHandle window = openWindow(sequence);
    setExpression(*window, PixelMathExpressionTab::RedOrAll, QStringLiteral("$T * sin"));
    setPlainText(*window, "localSymbolsTextEdit", QStringLiteral("sin = 0.5"));
    const int errors = context().errorCount();
    ASSERT_TRUE(applyAndSettle(context(), *window));
    EXPECT_EQ(context().errorCount(), errors + 1);
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("sin")))
        << context().errorTexts.back().toStdString();
    const std::vector<fixtures::FixtureSamples> after = sequenceFrames(session(), *sequence);
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t frame = 0u; frame < after.size(); ++frame)
    {
        EXPECT_EQ(after[frame].digest(), before[frame].digest()) << "frame " << frame;
    }
    window.reset();
}

// The editor's vocabulary names each function once: the special ones - the conditional, the region tests and the sample
// of another image - are not among the plain ones.
TEST(PixelMathVocabularyListsTest, EachFunctionIsListedOnceAndTheSpecialOnesAreTheRegionTestsTheConditionalAndTheSample)
{
    const an::ui::PixelMathVocabulary vocabulary = an::ui::pixelMathVocabulary();
    const std::set<QString> plain(vocabulary.functions.begin(), vocabulary.functions.end());
    const std::set<QString> special(vocabulary.specialFunctions.begin(), vocabulary.specialFunctions.end());
    for (const char* word : {"iif", "inrect", "inellipse", "pixel"})
    {
        EXPECT_EQ(special.count(QString::fromLatin1(word)), 1u) << word << " is a special function";
        EXPECT_EQ(plain.count(QString::fromLatin1(word)), 0u) << word << " is listed among the plain functions";
    }
    for (const QString& word : special)
    {
        EXPECT_EQ(plain.count(word), 0u) << word.toStdString();
    }
    EXPECT_EQ(plain.count(QStringLiteral("sin")), 1u);
}

// The function and operator tables list the engine's vocabulary once each, and every tab's symbol table lists the
// meta symbols, the symbols and the open views.
TEST_F(PixelMathWindowTest, TheTablesListTheEnginesVocabularyAndEveryTabsSymbols)
{
    ViewHandle target = activeScene("scene-rgb-float32");
    WindowHandle window = openWindow(target.get());
    const an::ui::PixelMathVocabulary vocabulary = an::ui::pixelMathVocabulary();
    for (const char* name : {"functionsTableWidget1", "functionsTableWidget4"})
    {
        auto* table = widgetOf<QTableWidget>(*window, name);
        ASSERT_NE(table, nullptr);
        EXPECT_EQ(table->rowCount(), vocabulary.functions.size() + vocabulary.specialFunctions.size());
        std::set<QString> listed;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            EXPECT_TRUE(listed.insert(table->item(row, 0)->text()).second)
                << "listed twice: " << table->item(row, 0)->text().toStdString();
        }
    }
    auto* operators = widgetOf<QTableWidget>(*window, "operatorsTableWidget2");
    ASSERT_NE(operators, nullptr);
    EXPECT_EQ(operators->rowCount(), vocabulary.operators.size());
    setPlainText(*window, "localSymbolsTextEdit", QStringLiteral("a = 1, b"));
    setComboText(*window, "outputModeSelector", QStringLiteral("Create new image"));
    for (const char* name :
         {"symbolsTableWidget1", "symbolsTableWidget2", "symbolsTableWidget3", "symbolsTableWidget4"})
    {
        auto* table = widgetOf<QTableWidget>(*window, name);
        ASSERT_NE(table, nullptr);
        EXPECT_GE(table->rowCount(), 4) << name << " lists the meta symbols and the target";
    }
    window.reset();
    dropView(context(), target);
}

} // namespace
