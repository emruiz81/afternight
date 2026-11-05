// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image menu's mirrors and turns, which the view makes of its own image and the non-interactive processes make of
/// every frame of a sequence: each one exact in every representation, the solution and the cards dropped by the one
/// policy every geometry change follows, one history step that undoes, and a sequence another run holds left alone.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"

#include "controls/ImageViewCompanions.h"

#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::geometry;

/// One turn or mirror as the menu runs it, the history step it records, and where each of its samples comes from.
struct Turn
{
    const char* name;
    std::function<void(const an::ui::ImageOperationsCoordinator&)> run;
    const char* step;
    bool swapsAxes;
    std::function<double(const fixtures::FixtureSamples&, std::uint32_t, std::uint32_t, std::uint32_t)> from;
};

[[nodiscard]] const std::vector<Turn>& turns()
{
    static const std::vector<Turn> table{
        {"HorizontalMirror", [](const auto& o) { o.horizontalMirror(); }, "Image Horizontal Mirror", false,
         [](const auto& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(s.width() - 1u - x, y, c);
         }},
        {"VerticalMirror", [](const auto& o) { o.verticalMirror(); }, "Image Vertical Mirror", false,
         [](const auto& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(x, s.height() - 1u - y, c);
         }},
        {"Rotate180", [](const auto& o) { o.rotate180Degree(); }, "Image Rotation", false,
         [](const auto& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(s.width() - 1u - x, s.height() - 1u - y, c);
         }},
        {"Rotate90Right", [](const auto& o) { o.rotate90DegreeRight(); }, "Image Rotation", true,
         [](const auto& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(y, s.height() - 1u - x, c);
         }},
        {"Rotate90Left", [](const auto& o) { o.rotate90DegreeLeft(); }, "Image Rotation", true,
         [](const auto& s, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
             return s.value(s.width() - 1u - y, x, c);
         }}};
    return table;
}

class ImageTurnsTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const Turn& turn() const
    {
        return turns()[GetParam()];
    }

    void SetUp() override
    {
        m_context = openProcessContext(4u);
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

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

std::string turnName(const ::testing::TestParamInfo<std::size_t>& info)
{
    return turns()[info.param].name;
}

} // namespace

// representation: every representation is turned exactly, each sample one of the source's, and kept.
TEST_P(ImageTurnsTest, EveryRepresentationIsTurnedExactlyAndKept)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(image != nullptr && samples.has_value());
    an::Result<an::consumer::ImageAsset> scene =
        an::tests::fixtures::engine::makeImageAsset(session(), *image, *samples);
    ASSERT_TRUE(scene.hasValue());
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        std::optional<an::consumer::ImageAsset> converted = convertedTo(session(), *scene, type);
        ASSERT_TRUE(converted.has_value());
        ViewHandle target = assetView(std::move(*converted), "target");
        activate(context(), *target);
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        ASSERT_TRUE(source.has_value());
        turn().run(imageOperations());
        ASSERT_TRUE(settleAll(context()));
        const std::optional<fixtures::FixtureSamples> turned = documentSamples(target->document());
        ASSERT_TRUE(turned.has_value());
        EXPECT_EQ(target->imageDescriptor()->sampleType(), type);
        ASSERT_EQ(turned->width(), turn().swapsAxes ? source->height() : source->width());
        ASSERT_EQ(turned->height(), turn().swapsAxes ? source->width() : source->height());
        bool exact = true;
        for (std::uint32_t c = 0u; c < turned->channelCount() && exact; ++c)
        {
            for (std::uint32_t y = 0u; y < turned->height() && exact; ++y)
            {
                for (std::uint32_t x = 0u; x < turned->width() && exact; ++x)
                {
                    exact = turned->value(x, y, c) == turn().from(*source, x, y, c);
                }
            }
        }
        EXPECT_TRUE(exact) << "a sample is not the source's sample it turned to";
        dropView(context(), target);
    }
}

// metadata: a turn keeps the acquisition record and the meaning and drops the solution and the codec's cards, the one
// policy every geometry change follows.
TEST_P(ImageTurnsTest, ATurnKeepsTheRecordAndDropsTheSolutionAndCards)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed("scene-rgb-float32");
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples("scene-rgb-float32");
    ASSERT_TRUE(image != nullptr && samples.has_value());
    an::Result<an::consumer::ImageAsset> scene =
        an::tests::fixtures::engine::makeImageAsset(session(), *image, *samples);
    ASSERT_TRUE(scene.hasValue());
    std::optional<an::consumer::ImageAsset> described = withMetadata(session(), *scene, describedMetadata());
    ASSERT_TRUE(described.has_value());
    ViewHandle target = assetView(std::move(*described), "target");
    activate(context(), *target);
    turn().run(imageOperations());
    ASSERT_TRUE(settleAll(context()));
    an::Result<an::consumer::AssetReadLease> lease = target->document().read();
    ASSERT_TRUE(lease.hasValue());
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    EXPECT_EQ(metadata.acquisition(), describedMetadata().acquisition());
    EXPECT_FALSE(metadata.astrometry().present());
    EXPECT_TRUE(metadata.codec().cards().empty());
    EXPECT_EQ(metadata.semantics().transferFunction(), an::TransferFunction::Linear);
    ASSERT_FALSE(metadata.provenance().entries().empty());
    EXPECT_EQ(metadata.provenance().entries().back().operation(), "an::transformImageGeometry");
    lease->release();
    dropView(context(), target);
}

// history: a turn is one history step under the view's name for it, and undoing it brings the image back.
TEST_P(ImageTurnsTest, ATurnIsOneHistoryStepThatUndoes)
{
    ViewHandle target = sceneView(session(), "scene-rgb-uint16");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    turn().run(imageOperations());
    ASSERT_TRUE(settleAll(context()));
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), turn().step);
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*target));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(target->document());
    ASSERT_TRUE(source.has_value() && undone.has_value());
    EXPECT_EQ(undone->digest(), source->digest());
    dropView(context(), target);
}

// error: a turn of a sequence another run holds says the view is busy and changes nothing.
TEST_P(ImageTurnsTest, ATurnOfASequenceAnotherRunHoldsSaysItIsBusy)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = writeSceneSequence(session(), directory, {"scene-rgb-uint16"});
    ImageViewControl* sequence = openSequenceView(context(), path, an::SequenceFormat::ImageList);
    ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->tryRequestLockForProcessing());
    const int before = static_cast<int>(context().messages.size());
    turn().run(imageOperations());
    ASSERT_TRUE(awaitMessage(context(), before));
    EXPECT_TRUE(context().messages.back().startsWith(QStringLiteral("warning:")))
        << context().messages.back().toStdString();
    EXPECT_EQ(sequence->document().sequence()->sourcePath, path);
    sequence->releaseLockForProcessing();
}

INSTANTIATE_TEST_SUITE_P(Turns, ImageTurnsTest, ::testing::Range<std::size_t>(0u, turns().size()), turnName);
