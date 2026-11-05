// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each of the combination processes' windows does beyond the result the records hold: the launch through its
/// registry descriptor, the preview and a preview area, a mask, a sequence, the history step, the result's metadata,
/// what the window keeps between openings, a stopped run, every representation an image can hold and a run the engine
/// refuses. One case per behaviour, run for every process it applies to, on the corpus scene with the images the
/// process reads beside it.

#include "Test_ProcessesCombinations/CombinationsTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessExecution.h"
#include "controls/SequenceContainers.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QCheckBox>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
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
using namespace an::tests::processes::combinations;
using an::tests::viewer::Channels;

[[nodiscard]] Channels channelsOf(const fixtures::FixtureSamples& samples)
{
    return samples.channelCount() == 1u ? Channels::Grey : Channels::Rgb;
}

/// A Float32 asset of samples that carries a metadata record, as an image whose header states it.
[[nodiscard]] std::optional<an::consumer::ImageAsset> assetWithMetadata(const an::consumer::EngineSession& session,
                                                                        const fixtures::FixtureSamples& samples,
                                                                        const an::ImageMetadata& metadata)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> described = an::tests::viewer::describeImage(
        scope->engine(), an::SampleType::Float32, samples.width(), samples.height(), channelsOf(samples));
    EXPECT_TRUE(described.has_value());
    if (!described.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        EXPECT_TRUE(access.has_value());
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t c = 0u; c < samples.channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < samples.height(); ++y)
            {
                auto row = access->view().row(c, y);
                EXPECT_TRUE(row.has_value());
                if (!row.has_value())
                {
                    return std::nullopt;
                }
                for (std::uint32_t x = 0u; x < samples.width(); ++x)
                {
                    (*row)[x] = static_cast<float>(samples.value(x, y, c));
                }
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// Makes every side view of a case's targets show its own samples carrying a metadata record, as views of images whose
/// headers state it; done before the window is opened, which snapshots what the views show when a run is asked for.
void stampSides(ProcessTestContext& context, Targets& targets, const an::ImageMetadata& metadata)
{
    for (SideView& side : targets.sides)
    {
        ASSERT_NE(side.view, nullptr) << side.name;
        const std::optional<fixtures::FixtureSamples> samples = documentSamples(side.view->document());
        ASSERT_TRUE(samples.has_value()) << side.name;
        std::optional<an::consumer::ImageAsset> stamped =
            assetWithMetadata(context.engineSession(), *samples, metadata);
        ASSERT_TRUE(stamped.has_value()) << side.name;
        auto outcome = std::make_shared<std::optional<an::Status>>();
        side.view->replaceImage(std::move(*stamped), true, [outcome](const an::Status& status) { *outcome = status; });
        ASSERT_TRUE(pumpUntil([&outcome]() { return outcome->has_value(); })) << side.name;
        ASSERT_TRUE((*outcome)->ok()) << side.name << ": " << (*outcome)->message();
        ASSERT_TRUE(settleView(*side.view)) << side.name;
    }
}

/// Adds a card of the FITS namespace; fails the case when the engine refuses it.
void addFitsCard(std::vector<an::CodecCard>& cards, const char* keyword, const std::string& value)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, "");
    EXPECT_TRUE(card.hasValue()) << keyword << ": " << card.status().message();
    if (card.hasValue())
    {
        cards.push_back(std::move(*card));
    }
}

/// A code of a representation standing for a value in the unit interval: an unsigned integer a fraction of its largest
/// value, a signed one counted from its smallest.
template <typename SampleT> [[nodiscard]] SampleT codeOf(double value)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<SampleT>(value);
    }
    else
    {
        const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
        const double width = static_cast<double>((std::numeric_limits<SampleT>::max)()) - lowest;
        return static_cast<SampleT>(std::llround(value * width + lowest));
    }
}

/// The value in the unit interval a code of a representation stands for.
[[nodiscard]] double valueOf(double code, fixtures::FixtureSampleType type)
{
    return unitValueOf(code, type);
}

/// How far apart one code of a representation is, in the unit interval; nothing for a floating one.
[[nodiscard]] double codeStep(fixtures::FixtureSampleType type)
{
    switch (type)
    {
    case fixtures::FixtureSampleType::UInt8:
    case fixtures::FixtureSampleType::Int8:
        return 1.0 / 255.0;
    case fixtures::FixtureSampleType::UInt16:
    case fixtures::FixtureSampleType::Int16:
        return 1.0 / 65535.0;
    case fixtures::FixtureSampleType::UInt32:
    case fixtures::FixtureSampleType::Int32:
        return 1.0 / 4294967295.0;
    default:
        return 0.0;
    }
}

/// A scene's values as a new asset of a representation, each value `transform` of the scene's.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset> typedSceneAsset(
    const an::consumer::EngineSession& session, const fixtures::FixtureSamples& scene,
    const std::function<double(std::uint32_t, std::uint32_t, std::uint32_t, double)>& transform =
        [](std::uint32_t, std::uint32_t, std::uint32_t, double value) { return value; })
{
    return an::tests::viewer::makeAsset<SampleT>(
        session, scene.width(), scene.height(), channelsOf(scene),
        [&scene, &transform](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return codeOf<SampleT>(transform(x, y, c, scene.value(x, y, c)));
        });
}

/// The preview check box of a window, or null for a window whose preview is always on.
[[nodiscard]] QCheckBox* previewBoxOf(QWidget& window)
{
    for (const char* name : {"preview", "previewCheckbox"})
    {
        if (auto* box = window.findChild<QCheckBox*>(QString::fromLatin1(name)))
        {
            return box;
        }
    }
    return nullptr;
}

/// Turns a window's preview on - a window without the box previews always - and waits for it to show what the
/// process makes.
[[nodiscard]] bool showPreview(const CombinationsProcess& process, ImageRTPreviewControl& window)
{
    if (QCheckBox* preview = previewBoxOf(window))
    {
        preview->setChecked(true);
    }
    return pumpUntil([&window]() { return window.runTelemetry().previewsCommitted >= 1u; }) &&
           settleWindow(process, window);
}

/// The samples a window's Apply of a mode makes of an asset, through views of its own: the target's document, or the
/// new view's. Fails the case when it cannot.
[[nodiscard]] std::optional<fixtures::FixtureSamples> appliedTo(ProcessTestContext& context,
                                                                const CombinationsProcess& process,
                                                                an::consumer::ImageAsset&& asset, std::string_view mode)
{
    Targets targets = openTargets(context, process, std::move(asset), "applied");
    EXPECT_NE(targets.target, nullptr);
    if (targets.target == nullptr)
    {
        return std::nullopt;
    }
    WindowHandle window = process.open(targets.target.get());
    process.configure(*window, targets, mode);
    const int errors = context.errorCount();
    std::optional<fixtures::FixtureSamples> samples = applyAndRead(context, process, *window, targets);
    EXPECT_EQ(process.committed(*window), 1u);
    EXPECT_EQ(context.errorCount(), errors) << context.errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(context, targets);
    return samples;
}

/// A copy of what a document holds now, as a new asset of the session.
[[nodiscard]] std::optional<an::consumer::ImageAsset> snapshotOf(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::ImageAsset> copy = document.snapshot();
    EXPECT_TRUE(copy.hasValue()) << copy.status().message();
    return copy.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*copy)) : std::nullopt;
}

/// A rectangle of a document's image as a new asset of the session, through the framework's own crop.
[[nodiscard]] std::optional<an::consumer::ImageAsset> cropOf(const an::consumer::EngineSession& session,
                                                             const an::ui::ImageDocument& document, const QRect& region)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> crop = an::ui::cropProcessRegion(session, scope->engine(), scope->execution(),
                                                                          lease->image(), lease->validity(), region);
    EXPECT_TRUE(crop.hasValue()) << crop.status().message();
    return crop.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*crop)) : std::nullopt;
}

/// Every value a window's widgets hold, by object name, for a case asking what a window keeps - the selectors that
/// name views left out, because the views are the case's.
[[nodiscard]] std::map<QString, QString> widgetValues(const QWidget& window)
{
    std::map<QString, QString> values;
    for (const QSpinBox* box : window.findChildren<QSpinBox*>())
    {
        values[box->objectName()] = QString::number(box->value());
    }
    for (const QDoubleSpinBox* box : window.findChildren<QDoubleSpinBox*>())
    {
        values[box->objectName()] = QString::number(box->value(), 'g', 17);
    }
    for (const QCheckBox* box : window.findChildren<QCheckBox*>())
    {
        values[box->objectName()] = box->isChecked() ? QStringLiteral("checked") : QStringLiteral("clear");
    }
    for (const QComboBox* box : window.findChildren<QComboBox*>())
    {
        if (box->findText(NO_IMAGE_STR) < 0 && !box->objectName().isEmpty())
        {
            values[box->objectName()] = box->currentText();
        }
    }
    return values;
}

/// Writes a SER recording of three frames of a scene, each a little brighter than the one before, published at `path`;
/// fails the case when it cannot.
void writeRecording(const an::consumer::EngineSession& session, const QString& path,
                    const fixtures::FixtureSamples& scene)
{
    constexpr std::uint32_t kFrames = 3u;
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = kFrames});
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    ASSERT_TRUE(scope.has_value());
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        std::optional<an::consumer::ImageAsset> asset =
            typedSceneAsset<float>(session, scene, [frame](std::uint32_t, std::uint32_t, std::uint32_t, double value) {
                return 0.8 * value + 0.05 * static_cast<double>(frame);
            });
        ASSERT_TRUE(asset.has_value());
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
        ASSERT_TRUE(appended.ok()) << appended.message();
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    ASSERT_TRUE(published.hasValue()) << published.status().message();
}

/// Every frame of a SER file, each a new asset of the session; fails the case when it cannot be read.
[[nodiscard]] std::vector<an::consumer::ImageAsset> framesOf(const an::consumer::EngineSession& session,
                                                             const QString& path)
{
    std::vector<an::consumer::ImageAsset> frames;
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return frames;
    }
    an::Result<an::ui::OpenedSequence> opened =
        an::ui::openSequenceSource(scope->engine(), scope->execution(), path, an::SequenceFormat::Ser);
    EXPECT_TRUE(opened.hasValue()) << opened.status().message();
    if (!opened.hasValue())
    {
        return frames;
    }
    for (;;)
    {
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            an::ui::readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
        EXPECT_TRUE(read.hasValue()) << read.status().message();
        if (!read.hasValue() || !read->has_value())
        {
            return frames;
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(session, std::move((*read)->second));
        EXPECT_TRUE(asset.hasValue()) << asset.status().message();
        if (!asset.hasValue())
        {
            return frames;
        }
        frames.push_back(std::move(*asset));
    }
}

/// A view over a sequence of a scene's frames, showing its first.
[[nodiscard]] ViewHandle sequenceView(const an::consumer::EngineSession& session, const QString& path,
                                      const fixtures::FixtureSamples& scene)
{
    std::optional<an::consumer::ImageAsset> first = typedSceneAsset<float>(session, scene);
    EXPECT_TRUE(first.has_value());
    if (!first.has_value())
    {
        return {};
    }
    an::Result<an::ui::ImageDocument> made = an::ui::ImageDocument::create(std::move(*first));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return {};
    }
    an::ui::ImageDocument shown = std::move(*made);
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 3u;
    sequence.currentFrame = 0u;
    shown.setSequence(sequence);
    ViewHandle view(new ImageViewControl());
    view->initialize("sequence", ViewControlMode::standard);
    view->resize(640, 480);
    view->setSequenceDocument(std::move(shown), false);
    view->initializeNewImageHistory("Sequence opened");
    EXPECT_TRUE(settleView(*view));
    return view;
}

[[nodiscard]] ImageRTPreviewControl* previewWindowOf(QWidget& window)
{
    auto* preview = dynamic_cast<ImageRTPreviewControl*>(&window);
    EXPECT_NE(preview, nullptr);
    return preview;
}

class CombinationsWindowTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const CombinationsProcess& process() const
    {
        return combinationsProcesses()[GetParam()];
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

    /// Lets the case's context go, for a case that installs one of its own: a process has one at a time.
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

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// The cases of the processes whose window is the framework's preview window with its preview box.
class CombinationsPreviewTest : public CombinationsWindowTest
{
};

/// The cases of the processes whose preview offers an area that shows what an Apply makes of that area alone.
class CombinationsPreviewAreaTest : public CombinationsWindowTest
{
};

/// The cases of the processes whose result is written in place, as one step of the target's history.
class CombinationsInPlaceTest : public CombinationsWindowTest
{
};

/// The cases of the processes that change an image: every one but the comparison.
class CombinationsChangingTest : public CombinationsWindowTest
{
};

/// The cases of the processes a sequence reaches: those written in place, and those that refuse one by name.
class CombinationsSequenceTest : public CombinationsWindowTest
{
};

/// The cases of the processes that read views beside their target.
class CombinationsSidesTest : public CombinationsWindowTest
{
};

[[nodiscard]] std::string processName(const ::testing::TestParamInfo<std::size_t>& info)
{
    return std::string(combinationsProcesses()[info.param].name);
}

} // namespace

// launch: the registered action opens the process's window - over the active view for a single-image process, over
// no view for a top-level one - attached under the process's name.
TEST_P(CombinationsWindowTest, TheRegisteredActionOpensTheWindow)
{
    const CombinationsProcess& under = process();
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    if (!under.topLevel)
    {
        context().addViewControl(targets.target.get());
    }
    context().setActiveImageViewControl(targets.target.get());

    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(under.descriptorId);
    ASSERT_NE(descriptor, nullptr) << under.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, under.action);
    EXPECT_EQ(descriptor->process_name, under.processName);
    if (under.topLevel)
    {
        ASSERT_TRUE(descriptor->launch_top_level);
        descriptor->launch_top_level();
    }
    else
    {
        ASSERT_TRUE(descriptor->launch_single_image);
        descriptor->launch_single_image(targets.target.get());
    }

    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, under.className);
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    TopLevelProcessBaseControl* window = context().openProcesses.front();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->isVisible());
    if (auto* preview = dynamic_cast<ImageRTPreviewControl*>(window))
    {
        EXPECT_EQ(preview->getProcessName(), under.processName);
        EXPECT_EQ(preview->attachmentName(), under.processName);
    }

    window->close();
    ASSERT_TRUE(pumpUntil([this]() { return context().openProcesses.empty(); }));
    dropTargets(context(), targets);
}

// preview: the preview shows what the Apply makes - the target itself for the comparison, which changes nothing - and
// leaves the image as it was.
TEST_P(CombinationsPreviewTest, ThePreviewShowsWhatTheApplyMakesAndLeavesTheImage)
{
    const CombinationsProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    WindowHandle window = under.open(targets.target.get());
    ImageRTPreviewControl* preview = previewWindowOf(*window);
    ASSERT_NE(preview, nullptr);
    under.configure(*window, targets, under.changingMode);
    const an::ui::DocumentRevision before = targets.target->document().revision();

    ASSERT_TRUE(showPreview(under, *preview));
    ASSERT_NE(preview->previewView(), nullptr);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(preview->previewView()->document());
    ASSERT_TRUE(shown.has_value() && source.has_value());
    EXPECT_EQ(targets.target->document().revision(), before) << "a preview never writes the image";

    if (QCheckBox* box = previewBoxOf(*window))
    {
        box->setChecked(false);
    }
    ASSERT_TRUE(settleWindow(under, *window));
    const std::optional<fixtures::FixtureSamples> applied = applyAndRead(context(), under, *window, targets);
    ASSERT_TRUE(applied.has_value());
    const fixtures::FixtureSamples& expected = under.leavesTarget ? *source : *applied;
    ASSERT_EQ(shown->channelCount(), expected.channelCount());
    EXPECT_LE(largestDifference(*shown, expected), 1.0e-6);
    window.reset();
    dropTargets(context(), targets);
}

// preview-region: a preview area shows the process's result over the area - what the Apply makes of an image of that
// area, with the second image cropped to it - and the image as it was around it.
TEST_P(CombinationsPreviewAreaTest, APreviewAreaShowsTheApplyOfThatAreaOverTheImage)
{
    const CombinationsProcess& under = process();
    // Larger than the largest area a window starts its preview with on both axes, so the area is part of the image.
    constexpr std::uint32_t kSceneWidth = 1320u;
    constexpr std::uint32_t kSceneHeight = 1240u;
    std::optional<an::consumer::ImageAsset> large = an::tests::viewer::makeAsset<float>(
        session(), kSceneWidth, kSceneHeight, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            const double u = static_cast<double>(x) / (kSceneWidth - 1u);
            const double v = static_cast<double>(y) / (kSceneHeight - 1u);
            const double star = (x % 97u == 40u && y % 89u == 30u) ? 0.6 : 0.0;
            return static_cast<float>(0.08 + 0.1 * u + 0.05 * v + 0.02 * static_cast<double>(c) + star);
        });
    ASSERT_TRUE(large.has_value());
    Targets targets = openTargets(context(), under, std::move(*large));
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(targets.target.get());
    ImageRTPreviewControl* preview = previewWindowOf(*window);
    ASSERT_NE(preview, nullptr);
    under.configure(*window, targets, under.changingMode);
    ASSERT_TRUE(showPreview(under, *preview));
    ASSERT_NE(preview->previewView(), nullptr);
    QCheckBox* areaBox = preview->previewView()->getRTPreviewAreaCheckBox();
    ASSERT_NE(areaBox, nullptr) << "the preview offers an area";
    if (!areaBox->isChecked())
    {
        const std::uint64_t committed = preview->runTelemetry().previewsCommitted;
        areaBox->setChecked(true);
        ASSERT_TRUE(
            pumpUntil([preview, committed]() { return preview->runTelemetry().previewsCommitted > committed; }));
    }
    ASSERT_TRUE(settleWindow(under, *window));
    const QRect area = preview->previewArea();
    ASSERT_FALSE(area.isEmpty());
    ASSERT_LT(area.width(), static_cast<int>(kSceneWidth));
    ASSERT_LT(area.height(), static_cast<int>(kSceneHeight));

    const std::optional<fixtures::FixtureSamples> shown = documentSamples(preview->previewView()->document());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    std::optional<an::consumer::ImageAsset> crop = cropOf(session(), targets.target->document(), area);
    ASSERT_TRUE(shown.has_value() && source.has_value() && crop.has_value());
    // The second image an Apply of the area reads is the second image cropped to it: the side views of the crop are
    // derived from the crop's own values, as the window's were from the target's, so the two agree.
    window.reset();
    dropTargets(context(), targets);
    const std::optional<fixtures::FixtureSamples> ofTheArea =
        appliedTo(context(), under, std::move(*crop), under.changingMode);
    ASSERT_TRUE(ofTheArea.has_value());
    ASSERT_EQ(shown->channelCount(), ofTheArea->channelCount());
    double insideLargest = 0.0;
    double outsideLargest = 0.0;
    for (std::uint32_t c = 0u; c < shown->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < kSceneHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kSceneWidth; ++x)
            {
                const bool inside = area.contains(static_cast<int>(x), static_cast<int>(y));
                const double expected = inside ? ofTheArea->value(x - static_cast<std::uint32_t>(area.x()),
                                                                  y - static_cast<std::uint32_t>(area.y()), c)
                                               : source->value(x, y, c);
                const double apart = std::abs(shown->value(x, y, c) - expected);
                (inside ? insideLargest : outsideLargest) = (std::max)(inside ? insideLargest : outsideLargest, apart);
            }
        }
    }
    EXPECT_LE(insideLargest, 1.0e-6) << "inside the area the preview is what the Apply makes of it";
    EXPECT_EQ(outsideLargest, 0.0) << "around the area the preview is the image";
}

// apply-mask: a mask on the target weighs a result written in place pixel by pixel - the source where it is black, the
// result where it is white.
TEST_P(CombinationsInPlaceTest, TheTargetsMaskWeighsTheResultPixelByPixel)
{
    const CombinationsProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    std::optional<an::consumer::ImageAsset> copy = snapshotOf(targets.target->document());
    ASSERT_TRUE(source.has_value() && copy.has_value());
    const std::optional<fixtures::FixtureSamples> unmasked =
        appliedTo(context(), under, std::move(*copy), under.changingMode);
    ASSERT_TRUE(unmasked.has_value());

    const std::uint32_t half = source->width() / 2u;
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), source->width(), source->height(),
                     [half](std::uint32_t x, std::uint32_t) { return x < half ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    targets.target->setCurrentMask(mask.get());
    targets.target->setEnableMask(true);
    ASSERT_TRUE(targets.target->isMasked());
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    const std::optional<fixtures::FixtureSamples> masked = documentSamples(targets.target->document());
    ASSERT_TRUE(masked.has_value());
    ASSERT_EQ(masked->channelCount(), unmasked->channelCount());
    EXPECT_FALSE(fixtures::sameSamples(*source, *unmasked)) << "the mode changes the scene";
    for (std::uint32_t c = 0u; c < masked->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < masked->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < masked->width(); ++x)
            {
                const double expected = x < half ? source->value(x, y, c) : unmasked->value(x, y, c);
                ASSERT_NEAR(masked->value(x, y, c), expected, 1.0e-6) << "at " << x << "," << y << " channel " << c;
            }
        }
    }
    targets.target->removeCurrentMask();
    window.reset();
    dropView(context(), mask);
    dropTargets(context(), targets);
}

// sequence: an Apply over a sequence processes every frame as an Apply processes a still image of it, with the same
// second image - or is refused by a process that needs a still image - and the view shows the sequence written.
TEST_P(CombinationsSequenceTest, EveryFrameOfASequenceIsProcessedAsAStillImageOfItIs)
{
    const CombinationsProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    const QString path = recordings.path() + QStringLiteral("/frames.ser");
    writeRecording(session(), path, *scene);
    ViewHandle target = sequenceView(session(), path, *scene);
    ASSERT_NE(target, nullptr);
    Targets targets;
    targets.target = std::move(target);
    targets.sides = under.openSides(context(), *scene, under.scene);

    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    const an::ui::DocumentRevision before = targets.target->document().revision();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    if (under.stillImageOnly)
    {
        EXPECT_EQ(under.committed(*window), 0u);
        EXPECT_EQ(targets.target->document().revision(), before);
        ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
        EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("still image")))
            << context().errorTexts.back().toStdString();
        window.reset();
        dropTargets(context(), targets);
        return;
    }
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ui::DocumentSequence> written = targets.target->document().sequence();
    ASSERT_TRUE(written.has_value());
    ASSERT_NE(written->sourcePath, path) << "the view shows the sequence written";
    std::vector<an::consumer::ImageAsset> results = framesOf(session(), written->sourcePath);
    std::vector<an::consumer::ImageAsset> sources = framesOf(session(), path);
    ASSERT_EQ(results.size(), 3u);
    ASSERT_EQ(sources.size(), 3u);
    window.reset();

    for (std::size_t frame = 0u; frame < sources.size(); ++frame)
    {
        SCOPED_TRACE(frame);
        // The still image's run reads the same side views: the frame opened as a target beside them.
        Targets still;
        still.target = assetView(std::move(sources[frame]), "frame");
        ASSERT_NE(still.target, nullptr);
        WindowHandle one = under.open(still.target.get());
        under.configure(*one, targets, under.changingMode);
        const std::size_t viewsBefore = context().madeViews.size();
        ASSERT_TRUE(applyAndSettle(under, *one, still));
        const an::ui::ImageDocument* document = resultDocument(context(), under, still, viewsBefore);
        ASSERT_NE(document, nullptr);
        const std::optional<fixtures::FixtureSamples> ofTheStill = documentSamples(*document);
        const std::optional<fixtures::FixtureSamples> fromTheSequence = samplesOf(results[frame]);
        ASSERT_TRUE(ofTheStill.has_value() && fromTheSequence.has_value());
        ASSERT_EQ(ofTheStill->sampleType(), fromTheSequence->sampleType());
        EXPECT_TRUE(fixtures::sameSamples(*ofTheStill, *fromTheSequence))
            << "the largest difference is " << largestDifference(*ofTheStill, *fromTheSequence);
        one.reset();
        dropView(context(), still.target);
    }
    dropTargets(context(), targets);
}

// history: an Apply in place is one step of the target's history, named by the process and described as its table
// states, which undoes; one that makes a new image leaves the target and its history as they were.
TEST_P(CombinationsChangingTest, AnApplyIsOneHistoryStepThatUndoes)
{
    const CombinationsProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    if (under.describe)
    {
        const an::ui::ProcessStep step = under.describe(targets, under.changingMode);
        EXPECT_EQ(step.name, under.processName);
        if (std::string_view(under.changingStep).size() > 0u)
        {
            EXPECT_EQ(step.description, under.changingStep);
        }
    }
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    const an::ui::DocumentRevision before = targets.target->document().revision();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    an::ui::IViewHistory* history = targets.target->viewHistory();
    ASSERT_NE(history, nullptr);

    if (under.landing == ResultLanding::NewView)
    {
        EXPECT_EQ(targets.target->document().revision(), before);
        EXPECT_FALSE(history->hasUndo());
        const std::vector<ImageViewControl*> made = viewsMadeSince(context(), viewsBefore);
        ASSERT_EQ(made.size(), 1u);
        EXPECT_FALSE(made.front()->getUniqueName().empty());
        window.reset();
        dropTargets(context(), targets);
        return;
    }
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), under.processName);
    const std::optional<fixtures::FixtureSamples> applied = documentSamples(targets.target->document());
    ASSERT_TRUE(source.has_value() && applied.has_value());
    EXPECT_FALSE(fixtures::sameSamples(*source, *applied)) << "the mode changes the scene";

    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*targets.target));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(targets.target->document());
    ASSERT_TRUE(undone.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*source, *undone));
    window.reset();
    dropTargets(context(), targets);
}

// metadata: the result's record is built whole - a combination's over its sources, a recombination's from the
// starless image's - keeping the solution; the result of a process that names a state operation is recorded
// nonlinear, on the processing-state cards, by a process, under the operation's name, and a linear meaning is no
// longer claimed; a recombination keeps the cards it does not own; the lineage names the engine operation.
TEST_P(CombinationsChangingTest, TheResultsRecordIsBuiltWholeAndNamesItsOperation)
{
    const CombinationsProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described = an::tests::viewer::describeImage(
        scope->engine(), an::SampleType::Float32, scene->width(), scene->height(), channelsOf(*scene));
    ASSERT_TRUE(described.has_value());
    an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "target");
    an::Result<an::CodecCard> linear = an::CodecCard::create(an::CodecNamespace::Fits, "ANLIN", "'linear'", "");
    ASSERT_TRUE(object.hasValue() && linear.hasValue());
    an::Result<an::CodecMetadata> cards =
        an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{*object, *linear});
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    an::ImageSemanticsRequest meaning = an::ImageSemantics{}.toRequest();
    meaning.transferFunction = an::TransferFunction::Linear;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
    const an::ImageMetadata metadata = an::ImageMetadata::create(
        an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{}, *cards, std::nullopt);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        ASSERT_TRUE(access.has_value());
        for (std::uint32_t c = 0u; c < scene->channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < scene->height(); ++y)
            {
                auto row = access->view().row(c, y);
                ASSERT_TRUE(row.has_value());
                for (std::uint32_t x = 0u; x < scene->width(); ++x)
                {
                    (*row)[x] = static_cast<float>(scene->value(x, y, c));
                }
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();
    Targets targets = openTargets(context(), under, std::move(*asset), "target", under.scene);
    ASSERT_NE(targets.target, nullptr);
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    const an::ui::ImageDocument* document = resultDocument(context(), under, targets, viewsBefore);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(settleWindow(under, *window));

    std::optional<an::consumer::AssetReadLease> lease;
    {
        an::Result<an::consumer::AssetReadLease> read = document->read();
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        lease.emplace(std::move(*read));
    }
    const an::ImageMetadata& result =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    const an::ProcessingStateRecord state = an::readProcessingStateCards(result.codec());
    if (std::string_view(under.stateOperation).empty())
    {
        EXPECT_NE(state.linearity, an::RecordedLinearity::Nonlinear) << "the process records nothing";
    }
    else
    {
        EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
        EXPECT_EQ(state.assignmentSource, an::StateAssignmentSource::Process);
        EXPECT_EQ(state.operation, under.stateOperation);
        EXPECT_NE(result.semantics().transferFunction(), an::TransferFunction::Linear);
    }
    // A combination's cards are those every source states identically, and the sources beside the target state no
    // card; a recombination starts from the starless image's record, which here is the side view's, with no card -
    // the process case holds that the starless image's own cards are kept.
    bool keptObject = false;
    for (const an::CodecCard& card : result.codec().cards())
    {
        keptObject = keptObject || (card.keyword() == "OBJECT" && card.value() == "'M 42'");
    }
    EXPECT_FALSE(keptObject) << "a card one source alone states is no card of the combination";
    EXPECT_EQ(result.astrometry(), metadata.astrometry());
    ASSERT_FALSE(result.provenance().entries().empty());
    EXPECT_EQ(result.provenance().entries().back().operation().rfind("an::", 0u), 0u)
        << "the lineage names the operation: " << result.provenance().entries().back().operation();
    lease.reset();
    window.reset();
    dropTargets(context(), targets);
}

// metadata: when every image a combination reads states the same cards and the same plate solution, the result keeps
// those cards and the first source's solution - the cards the viewer, the astrometry window and a new plate solve read
// a solution from - but no exposure, which a combination of several images does not have; a recombination keeps the
// starless image's record whole, its exposure with it.
TEST_P(CombinationsChangingTest, TheCardsEverySourceStatesAndTheFirstSourcesSolutionAreKept)
{
    const CombinationsProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    // A gnomonic solution about the scene's centre, written in the C locale whatever the process's is.
    std::vector<an::CodecCard> stated;
    addFitsCard(stated, "OBJECT", "'M 42'");
    addFitsCard(stated, "EXPTIME", "300.0");
    addFitsCard(stated, "CTYPE1", "'RA---TAN'");
    addFitsCard(stated, "CTYPE2", "'DEC--TAN'");
    addFitsCard(stated, "CRPIX1",
                QString::number(static_cast<double>(scene->width()) / 2.0 + 0.5, 'f', 1).toStdString());
    addFitsCard(stated, "CRPIX2",
                QString::number(static_cast<double>(scene->height()) / 2.0 + 0.5, 'f', 1).toStdString());
    addFitsCard(stated, "CRVAL1", "83.82");
    addFitsCard(stated, "CRVAL2", "-5.39");
    addFitsCard(stated, "CD1_1", "-0.00028");
    addFitsCard(stated, "CD1_2", "0.0");
    addFitsCard(stated, "CD2_1", "0.0");
    addFitsCard(stated, "CD2_2", "0.00028");
    addFitsCard(stated, "RADESYS", "'ICRS'");
    ASSERT_EQ(stated.size(), 13u);
    an::Result<an::CodecMetadata> cards = an::CodecMetadata::create(an::SourceEncoding{}, std::move(stated));
    ASSERT_TRUE(cards.hasValue()) << cards.status().message();
    ASSERT_TRUE(an::parseWcsCards(cards->cards(), scene->width(), scene->height()).hasValue())
        << "the sources state a solution";
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, *cards, std::nullopt);
    std::optional<an::consumer::ImageAsset> asset = assetWithMetadata(session(), *scene, metadata);
    ASSERT_TRUE(asset.has_value());
    Targets targets = openTargets(context(), under, std::move(*asset), "target", under.scene);
    ASSERT_NE(targets.target, nullptr);
    stampSides(context(), targets, metadata);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    const an::ui::ImageDocument* document = resultDocument(context(), under, targets, viewsBefore);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(settleWindow(under, *window));

    std::optional<an::consumer::AssetReadLease> lease;
    {
        an::Result<an::consumer::AssetReadLease> read = document->read();
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        lease.emplace(std::move(*read));
    }
    const an::ImageMetadata& result =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, lease->image());
    ASSERT_TRUE(described.has_value());
    bool keptObject = false;
    bool statesExposure = false;
    std::string kept;
    for (const an::CodecCard& card : result.codec().cards())
    {
        kept += std::string(card.keyword()) + "=" + std::string(card.value()) + " ";
        keptObject = keptObject || (card.keyword() == "OBJECT" && card.value() == "'M 42'");
        statesExposure = statesExposure || card.keyword() == "EXPTIME" || card.keyword() == "EXPOSURE";
    }
    EXPECT_TRUE(keptObject) << "a card every source states is the combination's: " << kept;
    const an::Result<an::WcsSolution> solution =
        an::parseWcsCards(result.codec().cards(), described->extent().width(), described->extent().height());
    ASSERT_TRUE(solution.hasValue()) << solution.status().message() << " - " << kept;
    EXPECT_NEAR(solution->referencePoint().rightAscensionDegrees, 83.82, 1e-9);
    EXPECT_NEAR(solution->referencePoint().declinationDegrees, -5.39, 1e-9);
    const bool recombination = std::string_view(under.name).find("Recombination") != std::string_view::npos;
    EXPECT_EQ(statesExposure, recombination) << (recombination ? "a recombination keeps the starless image's exposure"
                                                               : "a combination of several images states no exposure")
                                             << ": " << kept;
    EXPECT_EQ(result.astrometry(), metadata.astrometry());
    lease.reset();
    window.reset();
    dropTargets(context(), targets);
}

// persistence: a window keeps nothing between openings - one opened after another was set and closed shows the
// dialog's own values.
TEST_P(CombinationsWindowTest, AWindowOpenedAgainShowsTheDialogsOwnValues)
{
    const CombinationsProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    std::map<QString, QString> fresh;
    {
        WindowHandle first = under.open(targets.target.get());
        fresh = widgetValues(*first);
        under.configure(*first, targets, under.changingMode);
        first->close();
    }
    WindowHandle second = under.open(targets.target.get());
    EXPECT_EQ(widgetValues(*second), fresh);
    second.reset();
    dropTargets(context(), targets);
}

// cancel: a run whose stop is asked for while its body works publishes nothing - the engine sees the stop - leaves the
// image and its history as they were, makes no new image and shows no error.
TEST_P(CombinationsChangingTest, AStoppedApplyPublishesNothing)
{
    const CombinationsProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const an::ui::DocumentRevision before = targets.target->document().revision();
    WindowHandle window = under.openStopping(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    if (auto* preview = dynamic_cast<ImageRTPreviewControl*>(window.get()))
    {
        const ProcessRunTelemetry telemetry = preview->runTelemetry();
        EXPECT_EQ(telemetry.runsCancelled, 1u);
    }
    EXPECT_EQ(under.committed(*window), 0u);
    EXPECT_EQ(targets.target->document().revision(), before);
    EXPECT_TRUE(viewsMadeSince(context(), viewsBefore).empty());
    ASSERT_NE(targets.target->viewHistory(), nullptr);
    EXPECT_FALSE(targets.target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(context(), targets);
}

// leases: a run reads the views it combines under leases it takes before it asks to be admitted - and, written in
// place, in one order with its target's writer. An edit of a side view holds that view's writer and then asks to be
// admitted, so at a width of one a run admitted first would hold the only thread while it waited for the writer, and
// the edit would wait for the thread: neither would end.
TEST_P(CombinationsSidesTest, ARunWaitingForAnEditOfAViewItReadsHoldsNoThreadTheEditNeeds)
{
    const CombinationsProcess& under = process();
    releaseContext();
    std::unique_ptr<ProcessTestContext> narrow = openProcessContext(1u);
    ASSERT_NE(narrow, nullptr);
    const InstalledContext installed(*narrow);
    Targets targets = openScene(*narrow, under);
    ASSERT_NE(targets.target, nullptr);
    ASSERT_FALSE(targets.sides.empty());
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    ASSERT_TRUE(settleWindow(under, *window));
    const std::uint64_t committed = under.committed(*window);

    // A recombination's window takes the starless side view as its target and reads the case's target, the stars,
    // beside it; every other process reads its side views beside the case's target.
    const bool recombination = std::string_view(under.name).find("Recombination") != std::string_view::npos;
    ImageViewControl* read = recombination ? targets.target.get() : targets.sides.front().view.get();
    an::tests::viewer::EditHoldingTheWriter edit(*read);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    under.apply(*window);
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the run held while the run waited for the edit's writer";

    ASSERT_TRUE(settleWindow(under, *window));
    EXPECT_EQ(under.committed(*window), committed + 1u);
    EXPECT_EQ(narrow->errorCount(), 0) << narrow->errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(*narrow, targets);
}

// representation: every representation an image can hold is processed as fractions of full scale - an integer as a
// fraction of its range, a signed one counted from its smallest value - and its result kept in it where the process
// writes into the target's own image, to the nearest code; a result that moves smoothly with the samples is within a
// code of what the same values held in Float64 make.
TEST_P(CombinationsChangingTest, EveryRepresentationIsProcessedAsFractionsOfFullScale)
{
    const CombinationsProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    auto check = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        std::optional<an::consumer::ImageAsset> asset = typedSceneAsset<SampleT>(session(), *scene);
        const std::optional<fixtures::FixtureSampleType> type =
            fixtures::engine::fixtureSampleTypeOf(an::kSampleTypeOf<SampleT>);
        ASSERT_TRUE(asset.has_value() && type.has_value());
        // The very values the codes stand for, in Float64.
        std::optional<an::consumer::ImageAsset> wide = typedSceneAsset<double>(
            session(), *scene, [&type](std::uint32_t, std::uint32_t, std::uint32_t, double value) {
                return valueOf(static_cast<double>(codeOf<SampleT>(value)), *type);
            });
        ASSERT_TRUE(wide.has_value());
        const std::optional<fixtures::FixtureSamples> result =
            appliedTo(context(), under, std::move(*asset), under.changingMode);
        const std::optional<fixtures::FixtureSamples> reference =
            appliedTo(context(), under, std::move(*wide), under.changingMode);
        ASSERT_TRUE(result.has_value() && reference.has_value());
        ASSERT_EQ(result->channelCount(), reference->channelCount());
        if (under.keepsRepresentation)
        {
            ASSERT_EQ(result->sampleType(), *type) << "the result keeps the representation";
        }
        if (!under.smooth)
        {
            return;
        }
        // The narrowing to a code, and single-precision arithmetic where the samples are worked on in Float32.
        const double bound = codeStep(result->sampleType()) + 2.0e-5;
        double largest = 0.0;
        for (std::uint32_t c = 0u; c < result->channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < result->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < result->width(); ++x)
                {
                    largest =
                        (std::max)(largest, std::abs(valueOf(result->value(x, y, c), result->sampleType()) -
                                                     valueOf(reference->value(x, y, c), reference->sampleType())));
                }
            }
        }
        EXPECT_LE(largest, bound);
    };
    check.template operator()<std::uint8_t>();
    check.template operator()<std::int8_t>();
    check.template operator()<std::uint16_t>();
    check.template operator()<std::int16_t>();
    check.template operator()<std::uint32_t>();
    check.template operator()<std::int32_t>();
    check.template operator()<float>();
}

// error: a run whose own working memory the engine refuses publishes nothing, says why, and leaves the image as it was.
TEST_P(CombinationsChangingTest, ARunTheEngineRefusesPublishesNothingAndSaysWhy)
{
    const CombinationsProcess& under = process();
    // A session whose engine holds the image, the images beside it and the run's copy of it, and not much more.
    constexpr std::uint32_t kSide = 256u;
    const std::uint64_t imageBytes = static_cast<std::uint64_t>(kSide) * kSide * 3u * sizeof(float);
    releaseContext();
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 4u;
    settings.limits.memoryBudgetBytes = imageBytes * 7u / 2u;
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    ProcessTestContext tight(std::move(*opened));
    const InstalledContext installed(tight);
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        tight.engineSession(), kSide, kSide, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(0.1 + 0.8 * static_cast<double>((x + y + c) % kSide) / kSide);
        });
    ASSERT_TRUE(asset.has_value());
    Targets targets = openTargets(tight, under, std::move(*asset));
    ASSERT_NE(targets.target, nullptr);
    ASSERT_TRUE(settleView(*targets.target));
    const an::ui::DocumentRevision before = targets.target->document().revision();
    WindowHandle window = under.open(targets.target.get());
    under.configure(*window, targets, under.changingMode);
    const std::size_t viewsBefore = tight.madeViews.size();
    ASSERT_TRUE(applyAndSettle(under, *window, targets));
    EXPECT_EQ(under.committed(*window), 0u);
    EXPECT_EQ(targets.target->document().revision(), before);
    EXPECT_TRUE(viewsMadeSince(tight, viewsBefore).empty());
    EXPECT_FALSE(targets.target->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([&tight]() { return tight.errorCount() >= 1; })) << "the window says why";
    EXPECT_TRUE(tight.errorTexts.back().contains(QStringLiteral("memory budget")))
        << tight.errorTexts.back().toStdString();
    window.reset();
    dropTargets(tight, targets);
}

INSTANTIATE_TEST_SUITE_P(Processes, CombinationsWindowTest,
                         ::testing::Range<std::size_t>(0u, combinationsProcesses().size()), processName);
INSTANTIATE_TEST_SUITE_P(Preview, CombinationsPreviewTest,
                         ::testing::ValuesIn(processesWhere([](const CombinationsProcess& process) {
                             return process.realtimePreview && process.previewBox;
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(PreviewArea, CombinationsPreviewAreaTest,
                         ::testing::ValuesIn(processesWhere([](const CombinationsProcess& process) {
                             return process.previewArea;
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(InPlace, CombinationsInPlaceTest,
                         ::testing::ValuesIn(processesWhere([](const CombinationsProcess& process) {
                             return process.landing == ResultLanding::InPlace && !process.leavesTarget;
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(Changing, CombinationsChangingTest,
                         ::testing::ValuesIn(processesWhere([](const CombinationsProcess& process) {
                             return !process.leavesTarget;
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(Sides, CombinationsSidesTest,
                         ::testing::ValuesIn(processesWhere([](const CombinationsProcess& process) {
                             return !process.leavesTarget && static_cast<bool>(process.openSides);
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(Sequence, CombinationsSequenceTest,
                         ::testing::ValuesIn(processesWhere([](const CombinationsProcess& process) {
                             return !process.leavesTarget && process.realtimePreview &&
                                    (process.landing == ResultLanding::InPlace || process.stillImageOnly);
                         })),
                         processName);
