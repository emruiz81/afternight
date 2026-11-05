// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each of the mask, star and background processes' windows does beyond the result the records hold: the launch
/// through its registry descriptor, the preview and a preview area, a mask, a sequence, the history step, the result's
/// metadata, what the window keeps between openings, a stopped run, every representation an image can hold, a pixel
/// holding no number and a run the engine refuses. One case per behaviour, run for every process, on the corpus scene
/// the process's own mode changes.

#include "Test_ProcessesMasks/MasksTestSupport.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPreviewLayout.h"
#include "controls/SequenceContainers.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
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

/// A scene's samples, in the unit interval; fails the case when it cannot be made.
[[nodiscard]] std::optional<fixtures::FixtureSamples> sceneSamples(std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr) << scene;
    if (image == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

[[nodiscard]] Channels channelsOf(const fixtures::FixtureSamples& samples)
{
    return samples.channelCount() == 1u ? Channels::Grey : Channels::Rgb;
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
    switch (type)
    {
    case fixtures::FixtureSampleType::UInt8:
        return code / 255.0;
    case fixtures::FixtureSampleType::Int8:
        return (code + 128.0) / 255.0;
    case fixtures::FixtureSampleType::UInt16:
        return code / 65535.0;
    case fixtures::FixtureSampleType::Int16:
        return (code + 32768.0) / 65535.0;
    case fixtures::FixtureSampleType::UInt32:
        return code / 4294967295.0;
    case fixtures::FixtureSampleType::Int32:
        return (code + 2147483648.0) / 4294967295.0;
    default:
        return code;
    }
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
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(
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

/// The views a process's window opens over: the target, and the starless image a star reduction reads beside it.
struct Targets
{
    ViewHandle target;
    ViewHandle starless;
};

/// Opens a target over an asset and, for a process that reads one, a starless image beside it: the corpus scene's
/// own starless scene when the target is that scene's size, the target's values scaled down otherwise.
[[nodiscard]] Targets openTargets(ProcessTestContext& context, const MasksProcess& process,
                                  an::consumer::ImageAsset&& asset, const std::string& name = "target")
{
    Targets made;
    const std::optional<an::ImageDescriptor> described = asset.descriptor();
    made.target = assetView(std::move(asset), name);
    if (process.needsStarless && described.has_value())
    {
        const std::optional<fixtures::FixtureSamples> starless = sceneSamples(std::string(process.scene) + "-starless");
        if (starless.has_value() && starless->width() == described->extent().width() &&
            starless->height() == described->extent().height())
        {
            std::optional<an::consumer::ImageAsset> starlessAsset =
                sceneAsset<float>(context.engineSession(), *starless);
            if (starlessAsset.has_value())
            {
                made.starless = assetView(std::move(*starlessAsset), "starless");
            }
        }
        else
        {
            const std::optional<fixtures::FixtureSamples> target = documentSamples(made.target->document());
            if (target.has_value())
            {
                std::optional<an::consumer::ImageAsset> starlessAsset = an::tests::viewer::makeAsset<float>(
                    context.engineSession(), target->width(), target->height(), channelsOf(*target),
                    [&target](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                        return static_cast<float>(0.8 * valueOf(target->value(x, y, c), target->sampleType()));
                    });
                if (starlessAsset.has_value())
                {
                    made.starless = assetView(std::move(*starlessAsset), "starless");
                }
            }
        }
        EXPECT_NE(made.starless, nullptr) << "the starless image beside the target";
        if (made.starless != nullptr)
        {
            context.addViewControl(made.starless.get());
        }
    }
    return made;
}

/// The process's scene, opened as a target, with a starless image beside it when the process reads one.
[[nodiscard]] Targets openScene(ProcessTestContext& context, const MasksProcess& process)
{
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(process.scene);
    if (!scene.has_value())
    {
        return {};
    }
    std::optional<an::consumer::ImageAsset> asset = sceneAsset<float>(context.engineSession(), *scene);
    EXPECT_TRUE(asset.has_value());
    return asset.has_value() ? openTargets(context, process, std::move(*asset), "scene") : Targets{};
}

void dropTargets(ProcessTestContext& context, Targets& targets)
{
    dropView(context, targets.starless);
    dropView(context, targets.target);
}

/// The preview check box of a window, whichever name its builder gave it.
[[nodiscard]] QCheckBox* previewBoxOf(QWidget& window)
{
    for (const char* name : {"preview", "previewCheckbox"})
    {
        if (auto* box = window.findChild<QCheckBox*>(QString::fromLatin1(name)))
        {
            return box;
        }
    }
    ADD_FAILURE() << "the window has no preview check box";
    return nullptr;
}

// How many of the suites' patience a preview is given before it is said not to show. A Debug build runs the vector code
// of the larger previews unoptimized - the dark structure enhancement over the eight hundred pixels its window starts
// its area with takes about the patience alone and more beside other suites.
#ifdef NDEBUG
constexpr int kPreviewPatienceRounds = 1;
#else
constexpr int kPreviewPatienceRounds = 3;
#endif

/// Turns the event loop until a preview's condition holds, through `kPreviewPatienceRounds` of the suites' patience.
[[nodiscard]] bool pumpUntilPreviewed(const std::function<bool()>& condition)
{
    for (int round = 0; round < kPreviewPatienceRounds; ++round)
    {
        if (pumpUntil(condition))
        {
            return true;
        }
    }
    return false;
}

/// Turns a window's preview on and waits for it to show what the process makes: past its Preview button, for a
/// process whose preview computes only when asked.
[[nodiscard]] bool showPreview(const MasksProcess& process, ImageRTPreviewControl& window)
{
    QCheckBox* preview = previewBoxOf(window);
    if (preview == nullptr)
    {
        return false;
    }
    preview->setChecked(true);
    if (!pumpUntilPreviewed([&window]() { return window.runTelemetry().previewsCommitted >= 1u; }) ||
        !settleWindow(window))
    {
        return false;
    }
    if (process.previewOnButton)
    {
        auto* button = window.findChild<QPushButton*>(QStringLiteral("previewButton"));
        EXPECT_NE(button, nullptr);
        if (button == nullptr)
        {
            return false;
        }
        const std::uint64_t committed = window.runTelemetry().previewsCommitted;
        button->click();
        if (!pumpUntilPreviewed([&window, committed]() { return window.runTelemetry().previewsCommitted > committed; }))
        {
            return false;
        }
    }
    return settleWindow(window);
}

/// The samples a window's Apply of a mode makes of an asset, through a view of its own: the target's document, or the
/// new view's. Fails the case when it cannot.
[[nodiscard]] std::optional<fixtures::FixtureSamples> appliedTo(ProcessTestContext& context,
                                                                const MasksProcess& process,
                                                                an::consumer::ImageAsset&& asset, std::string_view mode)
{
    Targets targets = openTargets(context, process, std::move(asset), "applied");
    EXPECT_NE(targets.target, nullptr);
    if (targets.target == nullptr)
    {
        return std::nullopt;
    }
    WindowHandle window = process.open(*targets.target);
    process.configure(*window, mode);
    const int errors = context.errorCount();
    std::optional<fixtures::FixtureSamples> samples = applyAndRead(context, process, *window, *targets.target);
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
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

/// Every value a window's widgets hold, by object name, for a case asking what a window keeps.
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
        if (box->objectName() != QStringLiteral("targetImageSelector") &&
            box->objectName() != QStringLiteral("starlessImageSelector"))
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
            sceneAsset<float>(session, scene, [frame](std::uint32_t, std::uint32_t, std::uint32_t, double value) {
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
    std::optional<an::consumer::ImageAsset> first = sceneAsset<float>(session, scene);
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

/// What a run's result holds: the target's document, or the new view's; null when the run made none.
[[nodiscard]] const an::ui::ImageDocument* resultDocument(const ProcessTestContext& context,
                                                          const MasksProcess& process, ImageViewControl& target,
                                                          std::size_t viewsBefore)
{
    if (process.landing == ResultLanding::InPlace)
    {
        return &target.document();
    }
    const std::vector<ImageViewControl*> made = viewsMadeSince(context, viewsBefore);
    return made.size() == 1u ? &made.front()->document() : nullptr;
}

class MasksWindowTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const MasksProcess& process() const
    {
        return masksProcesses()[GetParam()];
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

/// The cases of the processes whose result is written in place, as one step of the target's history.
class MasksInPlaceWindowTest : public MasksWindowTest
{
};

/// The cases of the processes whose preview offers an area that shows what an Apply makes of that area alone.
class MasksPreviewAreaTest : public MasksWindowTest
{
};

/// The cases of the processes whose run the engine can be made to refuse with a session too small for it: every one
/// but the drawing, which holds planes of the image's size itself, so a session too small for the run is too small
/// for the drawing.
class MasksEngineRefusalTest : public MasksWindowTest
{
};

/// The processes of the table a predicate holds for, by index.
[[nodiscard]] std::vector<std::size_t> processesWhere(const std::function<bool(const MasksProcess&)>& holds)
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0u; index < masksProcesses().size(); ++index)
    {
        if (holds(masksProcesses()[index]))
        {
            indices.push_back(index);
        }
    }
    return indices;
}

[[nodiscard]] std::string processName(const ::testing::TestParamInfo<std::size_t>& info)
{
    return std::string(masksProcesses()[info.param].name);
}

} // namespace

// launch: the registered action opens the process's window over the active view, attached under the process's name.
TEST_P(MasksWindowTest, TheRegisteredActionOpensTheWindowOverTheActiveView)
{
    const MasksProcess& under = process();
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    context().addViewControl(targets.target.get());
    context().setActiveImageViewControl(targets.target.get());

    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(under.descriptorId);
    ASSERT_NE(descriptor, nullptr) << under.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, under.action);
    EXPECT_EQ(descriptor->process_name, under.processName);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(targets.target.get());

    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, under.className);
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    auto* window = dynamic_cast<ImageRTPreviewControl*>(context().openProcesses.front());
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->getProcessName(), under.processName);
    EXPECT_EQ(window->attachmentName(), under.processName);
    EXPECT_TRUE(window->isVisible());
    auto* selector = window->findChild<QComboBox*>(QStringLiteral("channelSelector"));
    EXPECT_EQ(selector != nullptr, under.perChannel) << "the channel selector is offered where the process runs on one";

    window->close();
    ASSERT_TRUE(pumpUntil([this]() { return context().openProcesses.empty(); }));
    dropTargets(context(), targets);
}

// preview: the preview shows what the Apply makes - the image itself for the drawing, which is painted over it - and
// leaves the image as it was; a preview computed on request computes once its button asks.
TEST_P(MasksWindowTest, ThePreviewShowsWhatTheApplyMakesAndLeavesTheImage)
{
    const MasksProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    const an::ui::DocumentRevision before = targets.target->document().revision();

    ASSERT_TRUE(showPreview(under, *window));
    ASSERT_NE(window->previewView(), nullptr);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    ASSERT_TRUE(shown.has_value() && source.has_value());
    EXPECT_EQ(targets.target->document().revision(), before) << "a preview never writes the image";

    previewBoxOf(*window)->setChecked(false);
    ASSERT_TRUE(settleWindow(*window));
    const std::optional<fixtures::FixtureSamples> applied = applyAndRead(context(), under, *window, *targets.target);
    ASSERT_TRUE(applied.has_value());
    const fixtures::FixtureSamples& expected = under.previewShowsTarget ? *source : *applied;
    ASSERT_EQ(shown->channelCount(), expected.channelCount());
    EXPECT_LE(largestDifference(*shown, expected), 1.0e-6);
    window.reset();
    dropTargets(context(), targets);
}

// preview-region: a preview area shows the process's result over the area - what the Apply makes of an image of that
// area - and the image as it was around it.
TEST_P(MasksPreviewAreaTest, APreviewAreaShowsTheApplyOfThatAreaOverTheImage)
{
    const MasksProcess& under = process();
    // Larger than the largest area a window starts its preview with on both axes - the multiscale transform's twelve
    // hundred pixels - so the area is part of the image.
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
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(showPreview(under, *window));
    ASSERT_NE(window->previewView(), nullptr);
    QCheckBox* areaBox = window->previewView()->getRTPreviewAreaCheckBox();
    ASSERT_NE(areaBox, nullptr) << "the preview offers an area";
    // A window that starts its preview over an area has shown it already.
    if (!areaBox->isChecked())
    {
        const std::uint64_t committed = window->runTelemetry().previewsCommitted;
        areaBox->setChecked(true);
        ASSERT_TRUE(pumpUntilPreviewed(
            [&window, committed]() { return window->runTelemetry().previewsCommitted > committed; }));
    }
    ASSERT_TRUE(settleWindow(*window));
    const QRect area = window->previewArea();
    ASSERT_FALSE(area.isEmpty());
    ASSERT_LT(area.width(), static_cast<int>(kSceneWidth));
    ASSERT_LT(area.height(), static_cast<int>(kSceneHeight));

    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    std::optional<an::consumer::ImageAsset> crop = cropOf(session(), targets.target->document(), area);
    ASSERT_TRUE(shown.has_value() && source.has_value() && crop.has_value());
    window.reset();
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
    dropTargets(context(), targets);
}

// apply-mask: a mask on the target weighs a result written in place pixel by pixel - the source where it is black, the
// result where it is white.
TEST_P(MasksInPlaceWindowTest, TheTargetsMaskWeighsTheResultPixelByPixel)
{
    const MasksProcess& under = process();
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
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *targets.target));
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

// sequence: an Apply over a sequence processes every frame as an Apply processes a still image of it - or is refused
// by a process that needs a still image - and the view shows the sequence written.
TEST_P(MasksInPlaceWindowTest, EveryFrameOfASequenceIsProcessedAsAStillImageOfItIs)
{
    const MasksProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    const QString path = recordings.path() + QStringLiteral("/frames.ser");
    writeRecording(session(), path, *scene);
    ViewHandle target = sequenceView(session(), path, *scene);
    ASSERT_NE(target, nullptr);
    Targets beside;
    if (under.needsStarless)
    {
        std::optional<an::consumer::ImageAsset> stand = sceneAsset<float>(session(), *scene);
        ASSERT_TRUE(stand.has_value());
        beside = openTargets(context(), under, std::move(*stand), "frame");
    }

    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    const an::ui::DocumentRevision before = target->document().revision();
    ASSERT_TRUE(applyAndSettle(*window, *target));
    if (under.stillImageOnly)
    {
        EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
        EXPECT_EQ(target->document().revision(), before);
        ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
        EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("still image")))
            << context().errorTexts.back().toStdString();
        window.reset();
        dropTargets(context(), beside);
        dropView(context(), target);
        return;
    }
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ui::DocumentSequence> written = target->document().sequence();
    ASSERT_TRUE(written.has_value());
    ASSERT_NE(written->sourcePath, path) << "the view shows the sequence written";
    std::vector<an::consumer::ImageAsset> results = framesOf(session(), written->sourcePath);
    std::vector<an::consumer::ImageAsset> sources = framesOf(session(), path);
    ASSERT_EQ(results.size(), 3u);
    ASSERT_EQ(sources.size(), 3u);
    window.reset();
    dropTargets(context(), beside);

    for (std::size_t frame = 0u; frame < sources.size(); ++frame)
    {
        SCOPED_TRACE(frame);
        const std::optional<fixtures::FixtureSamples> still =
            appliedTo(context(), under, std::move(sources[frame]), under.changingMode);
        const std::optional<fixtures::FixtureSamples> fromTheSequence = samplesOf(results[frame]);
        ASSERT_TRUE(still.has_value() && fromTheSequence.has_value());
        ASSERT_EQ(still->sampleType(), fromTheSequence->sampleType());
        EXPECT_TRUE(fixtures::sameSamples(*still, *fromTheSequence))
            << "the largest difference is " << largestDifference(*still, *fromTheSequence);
    }
    dropView(context(), target);
}

// history: an Apply in place is one step of the target's history, named by the process and described as its table
// states, which undoes; one that makes a new image leaves the target and its history as they were.
TEST_P(MasksWindowTest, AnApplyIsOneHistoryStepThatUndoes)
{
    const MasksProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    const an::ui::ProcessStep step = under.describe(*targets.target, under.changingMode);
    EXPECT_EQ(step.name, under.processName);
    EXPECT_EQ(step.description, under.changingStep);
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    const an::ui::DocumentRevision before = targets.target->document().revision();
    ASSERT_TRUE(applyAndSettle(*window, *targets.target));
    an::ui::IViewHistory* history = targets.target->viewHistory();
    ASSERT_NE(history, nullptr);

    if (under.landing == ResultLanding::NewView)
    {
        EXPECT_EQ(targets.target->document().revision(), before);
        EXPECT_FALSE(history->hasUndo());
        const std::vector<ImageViewControl*> made = viewsMadeSince(context(), viewsBefore);
        ASSERT_EQ(made.size(), 1u);
        EXPECT_EQ(made.front()->getUniqueName(), targets.target->getUniqueName() + under.newViewSuffix);
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

// metadata: the result keeps the source's solution and acquisition record; the result of a process that names a state
// operation is recorded nonlinear - on the processing-state cards, by a process, under the operation's name - and a
// linear meaning is no longer claimed; a result written in place keeps the cards it does not own; the lineage names the
// operation.
TEST_P(MasksWindowTest, TheResultKeepsTheSourcesRecordAndNamesItsOperation)
{
    const MasksProcess& under = process();
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
    Targets targets = openTargets(context(), under, std::move(*asset));
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *targets.target));
    const an::ui::ImageDocument* document = resultDocument(context(), under, *targets.target, viewsBefore);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(settleWindow(*window));

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
    if (under.landing == ResultLanding::InPlace)
    {
        bool keptObject = false;
        for (const an::CodecCard& card : result.codec().cards())
        {
            keptObject = keptObject || (card.keyword() == "OBJECT" && card.value() == "'M 42'");
        }
        EXPECT_TRUE(keptObject) << "a card the process does not own is kept";
    }
    EXPECT_EQ(result.astrometry(), metadata.astrometry());
    EXPECT_EQ(result.acquisition(), metadata.acquisition());
    ASSERT_FALSE(result.provenance().entries().empty());
    EXPECT_EQ(result.provenance().entries().back().operation().rfind("an::", 0u), 0u)
        << "the lineage names the operation: " << result.provenance().entries().back().operation();
    lease.reset();
    window.reset();
    dropTargets(context(), targets);
}

// persistence: a window keeps nothing between openings - one opened after another was set and closed shows the
// dialog's own values.
TEST_P(MasksWindowTest, AWindowOpenedAgainShowsTheDialogsOwnValues)
{
    const MasksProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    std::map<QString, QString> fresh;
    {
        WindowHandle first = under.open(*targets.target);
        fresh = widgetValues(*first);
        under.configure(*first, under.changingMode);
        first->close();
    }
    WindowHandle second = under.open(*targets.target);
    EXPECT_EQ(widgetValues(*second), fresh);
    second.reset();
    dropTargets(context(), targets);
}

// cancel: a run whose stop is asked for while its body works publishes nothing - the engine sees the stop - leaves the
// image and its history as they were, makes no new image and shows no error.
TEST_P(MasksWindowTest, AStoppedApplyPublishesNothing)
{
    const MasksProcess& under = process();
    Targets targets = openScene(context(), under);
    ASSERT_NE(targets.target, nullptr);
    const an::ui::DocumentRevision before = targets.target->document().revision();
    WindowHandle window = under.openStopping(*targets.target);
    under.configure(*window, under.changingMode);
    const std::size_t viewsBefore = context().madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *targets.target));
    const ProcessRunTelemetry telemetry = window->runTelemetry();
    EXPECT_EQ(telemetry.runsCancelled, 1u);
    EXPECT_EQ(telemetry.runsCommitted, 0u);
    EXPECT_EQ(targets.target->document().revision(), before);
    EXPECT_TRUE(viewsMadeSince(context(), viewsBefore).empty());
    ASSERT_NE(targets.target->viewHistory(), nullptr);
    EXPECT_FALSE(targets.target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    window.reset();
    dropTargets(context(), targets);
}

// representation: every representation an image can hold is processed as fractions of full scale and its result kept
// in it - an integer as a fraction of its range, a signed one counted from its smallest value, to the nearest code -
// and a result that moves smoothly with the samples is within a code of what the same values held in Float64 make.
TEST_P(MasksWindowTest, EveryRepresentationIsProcessedAsFractionsOfFullScaleAndKept)
{
    const MasksProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    auto check = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        std::optional<an::consumer::ImageAsset> asset = sceneAsset<SampleT>(session(), *scene);
        const std::optional<fixtures::FixtureSampleType> type =
            fixtures::engine::fixtureSampleTypeOf(an::kSampleTypeOf<SampleT>);
        ASSERT_TRUE(asset.has_value() && type.has_value());
        // The very values the codes stand for, in Float64.
        std::optional<an::consumer::ImageAsset> wide =
            sceneAsset<double>(session(), *scene, [&type](std::uint32_t, std::uint32_t, std::uint32_t, double value) {
                return valueOf(static_cast<double>(codeOf<SampleT>(value)), *type);
            });
        ASSERT_TRUE(wide.has_value());
        const std::optional<fixtures::FixtureSamples> result =
            appliedTo(context(), under, std::move(*asset), under.changingMode);
        const std::optional<fixtures::FixtureSamples> reference =
            appliedTo(context(), under, std::move(*wide), under.changingMode);
        ASSERT_TRUE(result.has_value() && reference.has_value());
        ASSERT_EQ(result->channelCount(), reference->channelCount());
        if (std::string_view(under.name) != "RangeSelection")
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
                    largest = (std::max)(largest, std::abs(valueOf(result->value(x, y, c), result->sampleType()) -
                                                           reference->value(x, y, c)));
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

// representation: a pixel holding no number does not stop a run, and the run still makes its result.
TEST_P(MasksWindowTest, APixelHoldingNoNumberDoesNotStopTheRun)
{
    const MasksProcess& under = process();
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(under.scene);
    ASSERT_TRUE(scene.has_value());
    const std::uint32_t holeX = scene->width() / 3u;
    const std::uint32_t holeY = scene->height() / 3u;
    std::optional<an::consumer::ImageAsset> asset = sceneAsset<float>(
        session(), *scene, [holeX, holeY](std::uint32_t x, std::uint32_t y, std::uint32_t c, double v) {
            return x == holeX && y == holeY && c == 0u ? std::numeric_limits<double>::quiet_NaN() : v;
        });
    ASSERT_TRUE(asset.has_value());
    Targets targets = openTargets(context(), under, std::move(*asset));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(targets.target->document());
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    const std::optional<fixtures::FixtureSamples> result = applyAndRead(context(), under, *window, *targets.target);
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    ASSERT_TRUE(source.has_value() && result.has_value());
    if (under.landing == ResultLanding::InPlace)
    {
        EXPECT_FALSE(fixtures::sameSamples(*source, *result)) << "the other pixels are processed";
    }
    window.reset();
    dropTargets(context(), targets);
}

// error: a run whose own working memory the engine refuses publishes nothing, says why, and leaves the image as it was.
TEST_P(MasksEngineRefusalTest, ARunTheEngineRefusesPublishesNothingAndSaysWhy)
{
    const MasksProcess& under = process();
    // A session whose engine holds the image and half an image more: too little for the run's copy of it. A process
    // that reads a starless image makes one in the session beside the target, and is given two images more, which
    // still leaves too little for its run. The window's preview, turned off as it opens, holds no copy of the target.
    constexpr std::uint32_t kSide = 256u;
    const std::uint64_t imageBytes = static_cast<std::uint64_t>(kSide) * kSide * 3u * sizeof(float);
    releaseContext();
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 4u;
    settings.limits.memoryBudgetBytes = imageBytes * (under.needsStarless ? 3u : 1u) + imageBytes / 2u;
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
    ASSERT_TRUE(settleView(*targets.target));
    const an::ui::DocumentRevision before = targets.target->document().revision();
    WindowHandle window = under.open(*targets.target);
    under.configure(*window, under.changingMode);
    const std::size_t viewsBefore = tight.madeViews.size();
    ASSERT_TRUE(applyAndSettle(*window, *targets.target));
    // The refusal is the body's, or - for a body that works in place and allocates next to nothing - the history's,
    // which cannot keep the state the Apply would replace.
    EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
    EXPECT_EQ(targets.target->document().revision(), before);
    EXPECT_TRUE(viewsMadeSince(tight, viewsBefore).empty());
    EXPECT_FALSE(targets.target->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([&tight]() { return tight.errorCount() >= 1; })) << "the window says why";
    EXPECT_TRUE(tight.errorTexts.back().contains(QStringLiteral("memory budget")))
        << tight.errorTexts.back().toStdString();
    window.reset();
    dropTargets(tight, targets);
}

INSTANTIATE_TEST_SUITE_P(Processes, MasksWindowTest, ::testing::Range<std::size_t>(0u, masksProcesses().size()),
                         processName);
INSTANTIATE_TEST_SUITE_P(InPlace, MasksInPlaceWindowTest,
                         ::testing::ValuesIn(processesWhere([](const MasksProcess& process) {
                             return process.landing == ResultLanding::InPlace;
                         })),
                         processName);
INSTANTIATE_TEST_SUITE_P(
    PreviewArea, MasksPreviewAreaTest,
    ::testing::ValuesIn(processesWhere([](const MasksProcess& process) { return process.previewArea; })), processName);
INSTANTIATE_TEST_SUITE_P(EngineRefusal, MasksEngineRefusalTest,
                         ::testing::ValuesIn(processesWhere([](const MasksProcess& process) {
                             return std::string_view(process.name) != "MaskDrawing";
                         })),
                         processName);
