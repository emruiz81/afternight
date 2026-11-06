// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What each of the eight pointwise and tone processes' windows does beyond the result the records hold: the launch
/// through its registry descriptor, the preview and a preview area, a mask, a sequence, the history step, the result's
/// metadata, what the window keeps between openings, a stopped run, every representation an image can hold, and a run
/// that cannot be done, what the window declares an interactive frame may run over, and that the histogram such a
/// frame remaps from the target's codes is the count of what the Apply writes. One case per behaviour, run for every
/// process; and the background the colour saturation derives - of the whole target for a run over a view of it - and
/// the intensity a colorized grey image starts from.

#include "Test_ProcessesPointwise/PointwiseTestSupport.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPreviewLayout.h"
#include "controls/SequenceContainers.h"
#include "processes/ColorSaturationProcess.h"
#include "processes/ExposureCorrectionProcess.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/StretchProcessSupport.h"
#include "session/PreviewProxies.h"
#include "widgets/HistogramComputation.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
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
using namespace an::tests::processes::pointwise;
using an::tests::viewer::Channels;

constexpr std::uint32_t kWidth = 24u;
constexpr std::uint32_t kHeight = 16u;

/// A colour sample of the cases' own scene of a size in the unit interval: three gradients that cross, so the scene
/// holds many hues, lightnesses and saturations, and none at either end of the interval.
[[nodiscard]] double sceneValue(std::uint32_t x, std::uint32_t y, std::uint32_t c, std::uint32_t width = kWidth,
                                std::uint32_t height = kHeight)
{
    const double u = static_cast<double>(x) / static_cast<double>(width - 1u);
    const double v = static_cast<double>(y) / static_cast<double>(height - 1u);
    switch (c)
    {
    case 0u:
        return 0.1 + 0.8 * u;
    case 1u:
        return 0.1 + 0.8 * v;
    default:
        return 0.1 + 0.8 * (0.5 + 0.5 * std::sin(6.0 * (u - v)));
    }
}

/// The scene as a document of a view, in Float32.
[[nodiscard]] ViewHandle colourScene(const an::consumer::EngineSession& session, std::uint32_t width = kWidth,
                                     std::uint32_t height = kHeight)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session, width, height, Channels::Rgb, [width, height](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(sceneValue(x, y, c, width, height));
        });
    EXPECT_TRUE(asset.has_value());
    return asset.has_value() ? assetView(std::move(*asset), "scene") : ViewHandle();
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

/// The samples a window's Apply makes of an image, through a view of its own; fails the case when it cannot.
[[nodiscard]] std::optional<fixtures::FixtureSamples> appliedTo(ProcessTestContext& context,
                                                                const PointwiseProcess& process,
                                                                an::consumer::ImageAsset&& asset, std::string_view mode)
{
    ViewHandle view = assetView(std::move(asset), "applied");
    EXPECT_NE(view, nullptr);
    if (view == nullptr)
    {
        return std::nullopt;
    }
    WindowHandle window = process.open(*view);
    process.configure(*window, mode);
    EXPECT_TRUE(applyAndSettle(*window, *view));
    std::optional<fixtures::FixtureSamples> samples = documentSamples(view->document());
    window.reset();
    dropView(context, view);
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
        if (box->objectName() != QStringLiteral("targetImageSelector"))
        {
            values[box->objectName()] = box->currentText();
        }
    }
    return values;
}

/// Writes a SER recording of three frames of the cases' scene, each a little brighter than the one before, grey or
/// colour, published at `path`; fails the case when it cannot.
void writeRecording(const an::consumer::EngineSession& session, const QString& path, Channels channels)
{
    constexpr std::uint32_t kFrames = 3u;
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = kFrames});
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    ASSERT_TRUE(scope.has_value());
    for (std::uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
            session, kWidth, kHeight, channels, [frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                return static_cast<float>(0.8 * sceneValue(x, y, c) + 0.05 * static_cast<double>(frame));
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

/// The samples of an asset; fails the case when it cannot be read.
[[nodiscard]] std::optional<fixtures::FixtureSamples> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readAssetSamples(asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

class PointwiseWindowTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const PointwiseProcess& process() const
    {
        return pointwiseProcesses()[GetParam()];
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

} // namespace

// launch: the registered action opens the process's window over the active view, attached under the process's name.
TEST_P(PointwiseWindowTest, TheRegisteredActionOpensTheWindowOverTheActiveView)
{
    const PointwiseProcess& under = process();
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = colourScene(session());
    context().addViewControl(target.get());
    context().setActiveImageViewControl(target.get());

    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(under.descriptorId);
    ASSERT_NE(descriptor, nullptr) << under.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, under.action);
    EXPECT_EQ(descriptor->process_name, under.processName);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(target.get());

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
    if (selector != nullptr)
    {
        EXPECT_EQ(selector->currentText(), an::ui::processChannelLabel(an::ChannelScope::Whole));
    }

    window->close();
    ASSERT_TRUE(pumpUntil([this]() { return context().openProcesses.empty(); }));
    dropView(context(), target);
}

// The single-image pointwise colour and tone dialogs may run an interactive frame over a reduction of the target, and
// those of them that map each channel alone say so; the arcsinh stretch and the colorization run every frame over the
// whole image.
TEST_P(PointwiseWindowTest, AnInteractiveFrameRunsOverAReductionOnlyWhereTheBodyAllowsIt)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    WindowHandle window = under.open(*target);
    const std::string_view name = under.name;
    const bool reducible = name == "BrightnessContrast" || name == "Gamma" || name == "Exposure" ||
                           name == "Saturation" || name == "Hsl" || name == "Lch";
    const bool channelsAlone = name == "BrightnessContrast" || name == "Gamma" || name == "Exposure";
    EXPECT_EQ(window->previewScaling(),
              reducible ? an::ui::PreviewScaling::Decimated : an::ui::PreviewScaling::FullOnly);
    EXPECT_EQ(window->mapsEachChannelAlone(), channelsAlone);
    window.reset();
    dropView(context(), target);
}

// apply: the dialog offers Apply where its values change the image - at its own values only the colorization and the
// saturation do - and once a value moves.
TEST_P(PointwiseWindowTest, ApplyIsOfferedWhereTheValuesChangeTheImage)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    WindowHandle window = under.open(*target);
    auto* buttons = window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_NE(buttons, nullptr);
    QAbstractButton* apply = buttons->button(QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    under.configure(*window, "-");
    EXPECT_EQ(apply->isEnabled(), under.applyAtDefaults);
    under.configure(*window, under.changingMode);
    EXPECT_TRUE(apply->isEnabled());
    window.reset();
    dropView(context(), target);
}

// error: a grey image is refused by name by a process that works on red, green and blue, and processed by the others -
// the colorization making a colour image of it.
TEST_P(PointwiseWindowTest, AGreyImageIsRefusedByAColourProcessAndProcessedByTheOthers)
{
    const PointwiseProcess& under = process();
    std::optional<an::ui::ImageDocument> grey =
        greyDocument(session(), kWidth, kHeight,
                     [](std::uint32_t x, std::uint32_t y) { return static_cast<float>(sceneValue(x, y, 0u)); });
    ASSERT_TRUE(grey.has_value());
    ViewHandle target = makeTargetView("grey", std::move(*grey));
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    if (under.colourOnly)
    {
        EXPECT_EQ(window->runTelemetry().runsFailed, 1u);
        EXPECT_EQ(target->document().revision(), before);
        ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
        EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("red, green and blue")))
            << context().errorTexts.back().toStdString();
    }
    else
    {
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
        EXPECT_EQ(context().errorCount(), 0);
        const std::optional<an::ImageDescriptor> described = target->document().descriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->extent().channelCount(), std::string_view(under.action) == "actionColorize" ? 3u : 1u);
    }
    window.reset();
    dropView(context(), target);
}

// preview: the preview shows what the Apply writes - the very operation, the gamma's included - and leaves the image
// as it was.
TEST_P(PointwiseWindowTest, ThePreviewShowsWhatTheApplyWritesAndLeavesTheImage)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    const an::ui::DocumentRevision before = target->document().revision();

    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
    ASSERT_TRUE(settleWindow(*window));
    ASSERT_NE(window->previewView(), nullptr);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(target->document().revision(), before) << "a preview never writes the image";

    preview->setChecked(false);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    const std::optional<fixtures::FixtureSamples> applied = documentSamples(target->document());
    ASSERT_TRUE(applied.has_value());
    ASSERT_EQ(shown->channelCount(), applied->channelCount());
    EXPECT_EQ(largestDifference(*shown, *applied), 0.0) << "the preview is the Apply's own operation";
    window.reset();
    dropView(context(), target);
}

// preview-region: a preview area shows over the area what the Apply writes there - a body that measures its image, as
// the saturation measures its background, takes the whole image's measurement, never the area's - and the image as it
// was around it.
TEST_P(PointwiseWindowTest, APreviewAreaShowsTheApplyOfThatAreaOverTheImage)
{
    const PointwiseProcess& under = process();
    // Larger than the area a preview starts with on both axes, so the area is part of the image.
    constexpr std::uint32_t kSceneWidth = an::ui::kProcessDefaultPreviewAreaEdge + 120;
    constexpr std::uint32_t kSceneHeight = an::ui::kProcessDefaultPreviewAreaEdge + 40;
    ViewHandle target = colourScene(session(), kSceneWidth, kSceneHeight);
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
    ASSERT_NE(window->previewView(), nullptr);
    QCheckBox* areaBox = window->previewView()->getRTPreviewAreaCheckBox();
    ASSERT_NE(areaBox, nullptr) << "the preview offers an area";
    const std::uint64_t committed = window->runTelemetry().previewsCommitted;
    areaBox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window, committed]() { return window->runTelemetry().previewsCommitted > committed; }));
    ASSERT_TRUE(settleWindow(*window));
    const QRect area = window->previewArea();
    ASSERT_FALSE(area.isEmpty());
    ASSERT_LT(area.width(), static_cast<int>(kSceneWidth));
    ASSERT_LT(area.height(), static_cast<int>(kSceneHeight));

    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    std::optional<an::consumer::ImageAsset> whole = cropOf(
        session(), target->document(), QRect(0, 0, static_cast<int>(kSceneWidth), static_cast<int>(kSceneHeight)));
    ASSERT_TRUE(shown.has_value() && source.has_value() && whole.has_value());
    const std::optional<fixtures::FixtureSamples> applied =
        appliedTo(context(), under, std::move(*whole), under.changingMode);
    ASSERT_TRUE(applied.has_value());
    ASSERT_EQ(shown->channelCount(), applied->channelCount());
    double insideLargest = 0.0;
    double outsideLargest = 0.0;
    for (std::uint32_t c = 0u; c < shown->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < kSceneHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kSceneWidth; ++x)
            {
                const bool inside = area.contains(static_cast<int>(x), static_cast<int>(y));
                const double expected =
                    inside ? applied->value(x, y, c) : source->value(x, y, (std::min)(c, source->channelCount() - 1u));
                const double apart = std::abs(shown->value(x, y, c) - expected);
                (inside ? insideLargest : outsideLargest) = (std::max)(inside ? insideLargest : outsideLargest, apart);
            }
        }
    }
    EXPECT_EQ(insideLargest, 0.0) << "inside the area the preview is what the Apply makes of it";
    EXPECT_EQ(outsideLargest, 0.0) << "around the area the preview is the image";
    window.reset();
    dropView(context(), target);
}

// apply-mask: a mask on the target weighs the result pixel by pixel - the source where it is black, the result where it
// is white - and an inverted mask the other way.
TEST_P(PointwiseWindowTest, TheTargetsMaskWeighsTheResultPixelByPixel)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    std::optional<an::consumer::ImageAsset> copy = snapshotOf(target->document());
    ASSERT_TRUE(source.has_value() && copy.has_value());
    const std::optional<fixtures::FixtureSamples> unmasked =
        appliedTo(context(), under, std::move(*copy), under.changingMode);
    ASSERT_TRUE(unmasked.has_value());

    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), kWidth, kHeight, [](std::uint32_t x, std::uint32_t) { return x < 12u ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    const std::optional<fixtures::FixtureSamples> masked = documentSamples(target->document());
    ASSERT_TRUE(masked.has_value());
    ASSERT_EQ(masked->channelCount(), unmasked->channelCount());
    for (std::uint32_t c = 0u; c < masked->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const double expected = x < 12u ? source->value(x, y, c) : unmasked->value(x, y, c);
                ASSERT_NEAR(masked->value(x, y, c), expected, 1.0e-6) << "at " << x << "," << y << " channel " << c;
            }
        }
    }
    target->removeCurrentMask();
    window.reset();
    dropView(context(), mask);
    dropView(context(), target);
}

// sequence: an Apply over a sequence processes every frame as an Apply processes a still image of it, and the view
// shows the sequence written.
TEST_P(PointwiseWindowTest, EveryFrameOfASequenceIsProcessedAsAStillImageOfItIs)
{
    const PointwiseProcess& under = process();
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    const QString path = recordings.path() + QStringLiteral("/frames.ser");
    writeRecording(session(), path, Channels::Rgb);
    std::optional<an::ui::ImageDocument> shown = an::tests::viewer::makeDocument<float>(
        session(), kWidth, kHeight, Channels::Rgb,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return static_cast<float>(sceneValue(x, y, c)); });
    ASSERT_TRUE(shown.has_value());
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 3u;
    sequence.currentFrame = 0u;
    shown->setSequence(sequence);
    ViewHandle target(new ImageViewControl());
    target->initialize("sequence", ViewControlMode::standard);
    target->resize(640, 480);
    target->setSequenceDocument(std::move(*shown), false);
    target->initializeNewImageHistory("Sequence opened");
    ASSERT_TRUE(settleView(*target));

    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ui::DocumentSequence> written = target->document().sequence();
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

// history: an Apply is one step of the target's history, named by the process and described as its table states;
// undoing it brings the image back.
TEST_P(PointwiseWindowTest, AnApplyIsOneHistoryStepThatUndoes)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    const an::ui::ProcessStep step = under.describe(*target, under.changingMode);
    EXPECT_EQ(step.name, under.processName);
    EXPECT_EQ(step.description, under.changingStep);
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));

    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), under.processName);
    const std::optional<fixtures::FixtureSamples> applied = documentSamples(target->document());
    ASSERT_TRUE(source.has_value() && applied.has_value());
    EXPECT_FALSE(fixtures::sameSamples(*source, *applied)) << "the mode changes the scene";

    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*target));
    const std::optional<fixtures::FixtureSamples> undone = documentSamples(target->document());
    ASSERT_TRUE(undone.has_value());
    EXPECT_TRUE(fixtures::sameSamples(*source, *undone));
    window.reset();
    dropView(context(), target);
}

// metadata: the result keeps the source's cards, solution and acquisition record, and is recorded nonlinear - on the
// processing-state cards, by a process, under the operation's name - with its lineage naming the engine operation and
// a linear meaning no longer claimed.
TEST_P(PointwiseWindowTest, TheResultIsRecordedNonlinearAndKeepsTheSourcesMetadata)
{
    const PointwiseProcess& under = process();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, kWidth, kHeight, Channels::Rgb);
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
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                auto row = access->view().row(c, y);
                ASSERT_TRUE(row.has_value());
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    (*row)[x] = static_cast<float>(sceneValue(x, y, c));
                }
            }
        }
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();
    ViewHandle target = assetView(std::move(*asset));
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));

    an::Result<an::consumer::AssetReadLease> lease = target->document().read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    const an::ImageMetadata& result =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    const an::ProcessingStateRecord state = an::readProcessingStateCards(result.codec());
    EXPECT_EQ(state.linearity, an::RecordedLinearity::Nonlinear);
    EXPECT_EQ(state.assignmentSource, an::StateAssignmentSource::Process);
    EXPECT_EQ(state.operation, under.stateOperation);
    EXPECT_EQ(state.channelBasis, an::RecordedChannelBasis::DisplayRgb);
    bool keptObject = false;
    for (const an::CodecCard& card : result.codec().cards())
    {
        keptObject = keptObject || (card.keyword() == "OBJECT" && card.value() == "'M 42'");
    }
    EXPECT_TRUE(keptObject) << "a card the process does not own is kept";
    EXPECT_NE(result.semantics().transferFunction(), an::TransferFunction::Linear);
    EXPECT_EQ(result.astrometry(), metadata.astrometry());
    EXPECT_EQ(result.acquisition(), metadata.acquisition());
    ASSERT_FALSE(result.provenance().entries().empty());
    EXPECT_EQ(result.provenance().entries().back().operation().rfind("an::", 0u), 0u)
        << "the lineage names the engine operation: " << result.provenance().entries().back().operation();
    window.reset();
    dropView(context(), target);
}

// validity: a sample holding no number in a pixel the image's rule admits is left as it was, under every rule - here
// exact zero is no data, so the not-a-number is admitted; the rest of the image is changed and the run lands.
TEST_P(PointwiseWindowTest, ASampleHoldingNoNumberIsLeftAsItWasUnderEveryRule)
{
    const PointwiseProcess& under = process();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, kWidth, kHeight, Channels::Rgb);
    ASSERT_TRUE(described.has_value());
    an::ImageSemanticsRequest meaning = an::ImageSemantics{}.toRequest();
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroIsNoData.has_value());
    meaning.validity = *zeroIsNoData;
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
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                auto row = access->view().row(c, y);
                ASSERT_TRUE(row.has_value());
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    (*row)[x] = static_cast<float>(sceneValue(x, y, c));
                }
            }
        }
        auto row = access->view().row(1u, 2u);
        ASSERT_TRUE(row.has_value());
        (*row)[3] = std::numeric_limits<float>::quiet_NaN();
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();
    ViewHandle target = assetView(std::move(*asset));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();

    {
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const auto* result = std::get_if<an::Image<float>>(&lease->image());
        ASSERT_NE(result, nullptr);
        const std::optional<an::ImageView<const float>> view = result->view();
        ASSERT_TRUE(view.has_value());
        const auto row = view->row(1u, 2u);
        ASSERT_TRUE(row.has_value());
        EXPECT_TRUE(std::isnan((*row)[3]));
    }
    const std::optional<fixtures::FixtureSamples> applied = documentSamples(target->document());
    ASSERT_TRUE(source.has_value() && applied.has_value());
    EXPECT_FALSE(fixtures::sameSamples(*source, *applied)) << "the rest of the image is changed";
    window.reset();
    dropView(context(), target);
}

// persistence: a window keeps nothing between openings - one opened after another was set and closed shows the
// dialog's own values.
TEST_P(PointwiseWindowTest, AWindowOpenedAgainShowsTheDialogsOwnValues)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    std::map<QString, QString> fresh;
    {
        WindowHandle first = under.open(*target);
        fresh = widgetValues(*first);
        under.configure(*first, under.changingMode);
        EXPECT_NE(widgetValues(*first), fresh) << "the mode moves a widget";
        first->close();
    }
    WindowHandle second = under.open(*target);
    EXPECT_EQ(widgetValues(*second), fresh);
    second.reset();
    dropView(context(), target);
}

// cancel: a run whose stop is asked for while its body works publishes nothing - the engine sees the stop - leaves the
// image and its history as they were and shows no error.
TEST_P(PointwiseWindowTest, AStoppedApplyPublishesNothing)
{
    const PointwiseProcess& under = process();
    ViewHandle target = colourScene(session());
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = under.openStopping(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    const ProcessRunTelemetry telemetry = window->runTelemetry();
    EXPECT_EQ(telemetry.runsCancelled, 1u);
    EXPECT_EQ(telemetry.runsCommitted, 0u);
    EXPECT_EQ(target->document().revision(), before);
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 0);
    window.reset();
    dropView(context(), target);
}

// representation: every representation an image can hold is processed as fractions of full scale and written back in
// its own - an integer as a fraction of its range, a signed one counted from its smallest value, to the nearest code -
// as the same values held in Float64 are processed.
TEST_P(PointwiseWindowTest, EveryRepresentationIsProcessedAsFractionsOfFullScaleAndKept)
{
    const PointwiseProcess& under = process();
    auto check = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), kWidth, kHeight, Channels::Rgb,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return codeOf<SampleT>(sceneValue(x, y, c)); });
        const std::optional<fixtures::FixtureSampleType> type =
            fixtures::engine::fixtureSampleTypeOf(an::kSampleTypeOf<SampleT>);
        ASSERT_TRUE(asset.has_value() && type.has_value());
        // The very values the codes stand for, in Float64.
        std::optional<an::consumer::ImageAsset> wide = an::tests::viewer::makeAsset<double>(
            session(), kWidth, kHeight, Channels::Rgb, [&type](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                return valueOf(static_cast<double>(codeOf<SampleT>(sceneValue(x, y, c))), *type);
            });
        ASSERT_TRUE(wide.has_value());
        const std::optional<fixtures::FixtureSamples> result =
            appliedTo(context(), under, std::move(*asset), under.changingMode);
        const std::optional<fixtures::FixtureSamples> reference =
            appliedTo(context(), under, std::move(*wide), under.changingMode);
        ASSERT_TRUE(result.has_value() && reference.has_value());
        ASSERT_EQ(result->sampleType(), *type) << "the result keeps the representation";
        ASSERT_EQ(result->channelCount(), reference->channelCount());
        // The narrowing to a code, and single-precision arithmetic where the samples are worked on in Float32: the
        // LCh adjustment's compiled row evaluates its transcendentals in Float32 lanes, within the bound the engine
        // states for it, and the others narrow once.
        const double bound = codeStep(result->sampleType()) + (std::string_view(under.name) == "Lch" ? 1.0e-4 : 1.0e-6);
        double largest = 0.0;
        for (std::uint32_t c = 0u; c < result->channelCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
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

// representation: a pixel holding no number does not stop a run - the stretch and the colour operations leave it as it
// was, a pointwise program carries it through - and every other pixel is processed.
TEST_P(PointwiseWindowTest, APixelHoldingNoNumberDoesNotStopTheRun)
{
    const PointwiseProcess& under = process();
    constexpr std::uint32_t kX = 5u;
    constexpr std::uint32_t kY = 4u;
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return x == kX && y == kY && c == 1u ? std::numeric_limits<float>::quiet_NaN()
                                                 : static_cast<float>(sceneValue(x, y, c));
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
    ASSERT_TRUE(source.has_value() && result.has_value());
    EXPECT_TRUE(std::isnan(result->value(kX, kY, 1u)));
    if (!under.perChannel)
    {
        EXPECT_EQ(result->value(kX, kY, 0u), source->value(kX, kY, 0u)) << "the pixel is left as it was";
        EXPECT_EQ(result->value(kX, kY, 2u), source->value(kX, kY, 2u)) << "the pixel is left as it was";
    }
    std::uint64_t processed = 0u;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                processed += (x != kX || y != kY) && result->value(x, y, c) != source->value(x, y, c) ? 1u : 0u;
            }
        }
    }
    EXPECT_GT(processed, 0u) << "the other pixels are processed";
    window.reset();
    dropView(context(), target);
}

// error: a run whose own working memory the engine refuses publishes nothing, says why, and leaves the image as it was.
TEST_P(PointwiseWindowTest, ARunTheEngineRefusesPublishesNothingAndSaysWhy)
{
    const PointwiseProcess& under = process();
    // A session whose engine holds the image and half an image more: too little for the run's copy of it, which every
    // process works in - its cards are restated where the result is, so no process needs a third image of that size,
    // and the run's own copy is the working memory the engine refuses. The window's preview is off as it opens and
    // holds no copy of the target.
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
    ViewHandle target = assetView(std::move(*asset));
    ASSERT_TRUE(settleView(*target));
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = under.open(*target);
    under.configure(*window, under.changingMode);
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
    EXPECT_EQ(window->runTelemetry().runsFailed, 1u);
    EXPECT_EQ(target->document().revision(), before);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([&tight]() { return tight.errorCount() == 1; })) << "the window says why";
    EXPECT_TRUE(tight.errorTexts.back().contains(QStringLiteral("memory budget")))
        << tight.errorTexts.back().toStdString();
    window.reset();
    dropView(tight, target);
}

// coverage: a body that declares, for its window's values, that it writes every sample leaves none of a destination
// that starts holding none of the image. Run as an Apply runs it over a colour image of every representation, every
// pixel of which is valid, into a destination of the image's shape every byte of which is a marker, it writes what it
// writes into a copy of the image, sample for sample. Every one of the eight but Colorize declares it at the values
// that change an image; a body that declares it writes part is handed a copy, which is all its declaration asks.
TEST_P(PointwiseWindowTest, ABodyThatDeclaresItWritesEverySampleLeavesNoneOfAnUnwrittenDestination)
{
    const PointwiseProcess& under = process();
    const bool declares = std::string_view(under.name) != "Colorize";
    auto check = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), kWidth, kHeight, Channels::Rgb,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return codeOf<SampleT>(sceneValue(x, y, c)); });
        ASSERT_TRUE(asset.has_value());
        ViewHandle target = assetView(std::move(*asset));
        const BodyOverDestinations ran = under.runBodyOverDestinations(*target, under.changingMode);
        EXPECT_EQ(ran.coverage == an::ui::ProcessBodyCoverage::WritesEverySample, declares);
        ASSERT_TRUE(ran.overCopy.has_value());
        ASSERT_TRUE(ran.overUnwritten.has_value());
        if (ran.coverage == an::ui::ProcessBodyCoverage::WritesEverySample)
        {
            EXPECT_TRUE(std::ranges::equal(ran.overCopy->bytes(), ran.overUnwritten->bytes()))
                << "the body left a sample of its destination as it found it";
        }
        dropView(context(), target);
    };
    check.template operator()<std::uint8_t>();
    check.template operator()<std::int8_t>();
    check.template operator()<std::uint16_t>();
    check.template operator()<std::int16_t>();
    check.template operator()<std::uint32_t>();
    check.template operator()<std::int32_t>();
    check.template operator()<float>();
    check.template operator()<double>();
}

// histogram: the histogram an interactive frame of a body that maps each channel alone shows is the target's codes
// remapped through what the body makes of every code, run over one row of them; that is exactly the count of what its
// Apply writes over the whole image, for the scene in eight and in sixteen bits, its rows of an odd width so no code
// keeps a place of its own in them.
TEST_P(PointwiseWindowTest, AMapOfEachChannelAloneMakesOfEveryCodeWhatItMakesOfTheImage)
{
    const PointwiseProcess& under = process();
    auto check = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), 263u, 31u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                return codeOf<SampleT>(sceneValue(x, y, c, 263u, 31u));
            });
        ASSERT_TRUE(asset.has_value());
        ViewHandle target = assetView(std::move(*asset));
        WindowHandle window = under.open(*target);
        if (!window->mapsEachChannelAlone())
        {
            // A body that mixes channels has nothing remapped through it.
            window.reset();
            dropView(context(), target);
            return;
        }
        std::optional<an::ui::CodeHistogram> codes;
        std::optional<an::ui::HistogramDisplayData> before;
        std::optional<an::consumer::ImageAsset> row;
        {
            std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
            ASSERT_TRUE(scope.has_value());
            an::Result<an::consumer::AssetReadLease> lease = target->document().read();
            ASSERT_TRUE(lease.hasValue()) << lease.status().message();
            an::Result<an::ui::CodeHistogram> counted = an::ui::countCodes(scope->execution(), *lease);
            ASSERT_TRUE(counted.hasValue()) << counted.status().message();
            codes.emplace(std::move(*counted));
            an::Result<an::ui::HistogramDisplayData> shown =
                an::ui::computeHistogramDisplayData(scope->execution(), *lease, an::ui::HistogramResolution::Bits16);
            ASSERT_TRUE(shown.hasValue()) << shown.status().message();
            before.emplace(std::move(*shown));
            an::Result<an::consumer::ImageAsset> ramp = an::ui::codeRampOf(session(), scope->engine(), *lease);
            ASSERT_TRUE(ramp.hasValue()) << ramp.status().message();
            row.emplace(std::move(*ramp));
        }
        under.configure(*window, under.changingMode);
        ASSERT_TRUE(applyAndSettle(*window, *target));
        window.reset();
        ViewHandle everyCode = assetView(std::move(*row), "codes");
        WindowHandle overCodes = under.open(*everyCode);
        under.configure(*overCodes, under.changingMode);
        ASSERT_TRUE(applyAndSettle(*overCodes, *everyCode));
        overCodes.reset();
        {
            std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
            ASSERT_TRUE(scope.has_value());
            an::Result<an::consumer::AssetReadLease> written = target->document().read();
            ASSERT_TRUE(written.hasValue()) << written.status().message();
            an::Result<an::consumer::AssetReadLease> mapped = everyCode->document().read();
            ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
            for (const an::ui::HistogramResolution resolution :
                 {an::ui::HistogramResolution::Bits8, an::ui::HistogramResolution::Bits16})
            {
                an::Result<an::ui::HistogramDisplayData> counted =
                    an::ui::computeHistogramDisplayData(scope->execution(), *written, resolution);
                ASSERT_TRUE(counted.hasValue()) << counted.status().message();
                if (resolution == an::ui::HistogramResolution::Bits16)
                {
                    bool moved = false;
                    for (std::size_t channel = 0u; channel < counted->channels.size(); ++channel)
                    {
                        moved = moved || counted->channels[channel].bins != before->channels[channel].bins;
                    }
                    EXPECT_TRUE(moved) << "the mode left the scene's histogram as it was, so nothing is compared";
                }
                an::Result<an::ui::HistogramDisplayData> remapped =
                    an::ui::remapHistogramDisplayData(scope->engine(), scope->execution(), *codes, *mapped, resolution);
                ASSERT_TRUE(remapped.hasValue()) << remapped.status().message();
                ASSERT_EQ(remapped->channels.size(), counted->channels.size());
                for (std::size_t channel = 0u; channel < counted->channels.size(); ++channel)
                {
                    SCOPED_TRACE(channel);
                    const an::ui::HistogramDisplayChannel& expected = counted->channels[channel];
                    const an::ui::HistogramDisplayChannel& shown = remapped->channels[channel];
                    EXPECT_EQ(shown.bins, expected.bins);
                    EXPECT_EQ(shown.clippedLow, expected.clippedLow);
                    EXPECT_EQ(shown.clippedHigh, expected.clippedHigh);
                    EXPECT_EQ(shown.samples, expected.samples);
                }
            }
        }
        dropView(context(), everyCode);
        dropView(context(), target);
    };
    check.template operator()<std::uint8_t>();
    check.template operator()<std::uint16_t>();
}

INSTANTIATE_TEST_SUITE_P(EightProcesses, PointwiseWindowTest,
                         ::testing::Range<std::size_t>(0u, pointwiseProcesses().size()),
                         [](const ::testing::TestParamInfo<std::size_t>& info) {
                             return std::string(pointwiseProcesses()[info.param].name);
                         });

namespace
{

/// One process's window, for what only that process does.
class NamedProcessTest : public ::testing::Test
{
protected:
    explicit NamedProcessTest(std::string_view name) : m_name(name) {}

    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        for (const PointwiseProcess& candidate : pointwiseProcesses())
        {
            if (std::string_view(candidate.name) == m_name)
            {
                m_process = &candidate;
            }
        }
        ASSERT_NE(m_process, nullptr);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const PointwiseProcess& process() const
    {
        return *m_process;
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
    std::string_view m_name;
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    const PointwiseProcess* m_process = nullptr;
};

/// The colour saturation's window, for the background it derives from the green channel.
class SaturationBackgroundTest : public NamedProcessTest
{
protected:
    SaturationBackgroundTest() : NamedProcessTest("Saturation") {}
};

/// A colour saturation window whose body a case runs itself, over a source and as a run of its choosing.
class SaturationBody final : public ColorSaturationProcess
{
public:
    using ColorSaturationProcess::ColorSaturationProcess;

    /// Runs the body with the window's values over a source, into a copy of it, as a run of a kind runs it.
    [[nodiscard]] std::optional<fixtures::FixtureSamples> runOver(const an::consumer::AssetReadLease& source,
                                                                  const an::ui::ProcessRun& run)
    {
        const ColorSaturationParams params =
            static_cast<const an::ui::TypedProcessControl<ColorSaturationParams>&>(*this).getParams();
        const an::consumer::EngineSession* session = engineSession();
        if (session == nullptr)
        {
            ADD_FAILURE() << "the body has no session";
            return std::nullopt;
        }
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            *session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
            an::ui::ExecutionObserver{});
        if (!scope.hasValue())
        {
            ADD_FAILURE() << scope.status().message();
            return std::nullopt;
        }
        const an::ui::ProcessSource from = an::ui::ProcessSource::of(source);
        an::Result<an::ui::ProcessDestination> destination =
            an::ui::ProcessDestination::copyOf(*session, scope->engine(), scope->execution(), from);
        if (!destination.hasValue())
        {
            ADD_FAILURE() << destination.status().message();
            return std::nullopt;
        }
        const an::Status ran = executeTypedProcessImpl(from, *destination, run, params, *scope);
        if (ran.failed())
        {
            ADD_FAILURE() << ran.message();
            return std::nullopt;
        }
        an::Result<an::consumer::ImageAsset> written = destination->release();
        if (!written.hasValue())
        {
            ADD_FAILURE() << written.status().message();
            return std::nullopt;
        }
        std::string error;
        std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readAssetSamples(*written, error);
        EXPECT_TRUE(samples.has_value()) << error;
        return samples;
    }
};

/// The colorization's window, for the colour image it makes of a grey one.
class ColorizeIntensityTest : public NamedProcessTest
{
protected:
    ColorizeIntensityTest() : NamedProcessTest("Colorize") {}
};

} // namespace

// apply: the background a pixel's lightness must pass is the green channel's median plus its deviation, times the
// factor - not its mean - so a pixel whose lightness lies between the two is enhanced.
TEST_F(SaturationBackgroundTest, TheBackgroundIsTheGreenChannelsMedianPlusItsDeviation)
{
    // One pixel in four a green of lightness 0.55, the rest a dark grey: the green channel's median, 0.1, plus its
    // deviation, 0.346, is a background below that lightness, where its mean, 0.3, plus the deviation is one above it.
    const auto green = [](std::uint32_t x, std::uint32_t y) { return (x + y) % 4u == 0u; };
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgb, [&green](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return green(x, y) ? (c == 1u ? 0.9f : 0.2f) : 0.1f;
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    WindowHandle window = process().open(*target);
    process().configure(*window, "amount-0.5");
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
    ASSERT_TRUE(result.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            if (green(x, y))
            {
                // Its saturation, 0.78, times 1.5 is held to one at the same hue and lightness.
                EXPECT_NEAR(result->value(x, y, 0u), 0.1, 1e-5) << x << "," << y;
                EXPECT_NEAR(result->value(x, y, 1u), 1.0, 1e-5) << x << "," << y;
                EXPECT_NEAR(result->value(x, y, 2u), 0.1, 1e-5) << x << "," << y;
            }
            else
            {
                for (std::uint32_t c = 0u; c < 3u; ++c)
                {
                    EXPECT_EQ(result->value(x, y, c), 0.1f) << x << "," << y << "," << c;
                }
            }
        }
    }
    window.reset();
    dropView(context(), target);
}

// preview: a run over a view of the target - the reduction an interactive frame runs over - takes the whole target's
// background, as an Apply over the target does, never one derived from the view: averaging moves a median.
TEST_F(SaturationBackgroundTest, ARunOverAReductionOfTheTargetTakesTheWholeTargetsBackground)
{
    // One pixel in four of every two-by-two block is a green of 0.9, the rest a green of 0.1, each under a red of 0.6
    // and a blue of 0.1. The target's background, its green median 0.1 plus its deviation 0.346, lies above the
    // lightness of every pixel of the target reduced by two - (0.6 + 0.1) / 2 = 0.35 - while the reduction's own, the
    // mean 0.3 with no deviation, lies below it: under its own background the reduction would be enhanced.
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            const bool bright = x % 2u == 0u && y % 2u == 0u;
            return c == 0u ? 0.6f : (c == 1u ? (bright ? 0.9f : 0.1f) : 0.1f);
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    auto* window = new SaturationBody(target.get());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    WindowHandle owned(window);
    process().configure(*window, "amount-0.5");

    std::optional<an::consumer::ImageAsset> reduced;
    {
        an::Result<an::consumer::OperationScope> scope =
            session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue());
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue());
        an::Result<an::consumer::ImageAsset> made =
            an::ui::reduceForPreview(session(), scope->engine(), scope->execution(), *lease, 2u);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        reduced.emplace(std::move(*made));
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> source = fixtures::engine::readAssetSamples(*reduced, error);
    ASSERT_TRUE(source.has_value()) << error;
    {
        an::Result<an::consumer::AssetReadLease> reducedLease = reduced->read();
        ASSERT_TRUE(reducedLease.hasValue());
        an::ui::ProcessRun overReduction{.kind = an::ui::ProcessRunKind::Preview,
                                         .frameKind = an::ui::PreviewFrameKind::Interactive};
        overReduction.proxyFactor = 2u;
        const std::optional<fixtures::FixtureSamples> framed = window->runOver(*reducedLease, overReduction);
        ASSERT_TRUE(framed.has_value());
        EXPECT_EQ(largestDifference(*framed, *source), 0.0)
            << "a frame over the reduction was enhanced against the reduction's own background";

        const an::ui::ProcessRun overItself{.kind = an::ui::ProcessRunKind::Preview};
        const std::optional<fixtures::FixtureSamples> own = window->runOver(*reducedLease, overItself);
        ASSERT_TRUE(own.has_value());
        EXPECT_GT(largestDifference(*own, *source), 0.01) << "the image under its own background is left as it was";
    }
    owned.reset();
    dropView(context(), target);
}

// apply: a background past one - a high factor over a bright image - is held to one, so no pixel is enhanced and the
// Apply lands rather than being refused.
TEST_F(SaturationBackgroundTest, ABackgroundPastOneEnhancesNothing)
{
    // A green of lightness 0.725 everywhere: the median, 0.95, with no deviation, times the factor two is 1.9.
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgb,
        [](std::uint32_t x, std::uint32_t, std::uint32_t c) { return c == 1u ? 0.95f : (x % 2u == 0u ? 0.5f : 0.6f); });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
    WindowHandle window = process().open(*target);
    process().configure(*window, "amount-0.5-background-factor-2");
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
    ASSERT_TRUE(source.has_value() && result.has_value());
    EXPECT_EQ(largestDifference(*source, *result), 0.0) << "no pixel is enhanced";
    window.reset();
    dropView(context(), target);
}

// apply: the colorization makes red, green and blue of a grey image by starting each channel as the intensity, so a
// pixel it does not colorize - one holding no number - keeps that intensity in all three.
TEST_F(ColorizeIntensityTest, AGreyPixelHoldingNoNumberKeepsItInEveryChannel)
{
    constexpr std::uint32_t kX = 5u;
    constexpr std::uint32_t kY = 4u;
    std::optional<an::ui::ImageDocument> grey =
        greyDocument(session(), kWidth, kHeight, [](std::uint32_t x, std::uint32_t y) {
            return x == kX && y == kY ? std::numeric_limits<float>::quiet_NaN()
                                      : static_cast<float>(sceneValue(x, y, 0u));
        });
    ASSERT_TRUE(grey.has_value());
    ViewHandle target = makeTargetView("grey", std::move(*grey));
    WindowHandle window = process().open(*target);
    process().configure(*window, "hue-200-saturation-0.6-hsi");
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ImageDescriptor> described = target->document().descriptor();
    ASSERT_TRUE(described.has_value());
    ASSERT_EQ(described->extent().channelCount(), 3u);
    const std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
    ASSERT_TRUE(result.has_value());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_TRUE(std::isnan(result->value(kX, kY, c))) << "channel " << c;
    }
    std::uint64_t coloured = 0u;
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            coloured += (x != kX || y != kY) && result->value(x, y, 0u) != result->value(x, y, 2u) ? 1u : 0u;
        }
    }
    EXPECT_GT(coloured, 0u) << "the other pixels are colorized";
    window.reset();
    dropView(context(), target);
}

// apply: an image with an alpha channel is refused by name, its document left at its revision, and its lightness is
// not offered - the colorization reads a grey image, or red, green and blue.
TEST_F(ColorizeIntensityTest, AnImageWithAlphaIsRefusedByNameAndOffersNoLightness)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgba, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return c == 3u ? 1.0f : static_cast<float>(sceneValue(x, y, c));
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    ASSERT_TRUE(settleView(*target));
    const an::ui::DocumentRevision before = target->document().revision();
    WindowHandle window = process().open(*target);
    auto* useLightness = window->findChild<QCheckBox*>(QStringLiteral("useLightness"));
    ASSERT_NE(useLightness, nullptr);
    EXPECT_FALSE(useLightness->isEnabled()) << "the lightness of an image with alpha is not offered";
    process().configure(*window, "hue-200-saturation-0.6-hsi");
    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
    EXPECT_EQ(window->runTelemetry().runsFailed, 1u);
    EXPECT_EQ(target->document().revision(), before);
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() > 0; })) << "the window says why";
    const QString said = context().errorTexts.join(QStringLiteral("; "));
    EXPECT_TRUE(said.contains(QStringLiteral("red, green and blue"))) << said.toStdString();
    window.reset();
    dropView(context(), target);
}

// apply: a file's colour image - sensor codes, or a floating physical quantity, as the reader describes every file -
// is colorized on its lightness read as the fractions of full scale the view shows, as the same samples stated
// normalized are.
TEST_F(ColorizeIntensityTest, AFilesColourImageIsColorizedOnItsLightnessAsTheViewShowsIt)
{
    const auto colorized = [&]<typename SampleT>(an::NumericDomain domain) -> std::optional<fixtures::FixtureSamples> {
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), kWidth, kHeight, Channels::Rgb,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<SampleT>(sceneValue(x, y, c));
                }
                else
                {
                    return static_cast<SampleT>(std::lround(sceneValue(x, y, c) * 65535.0));
                }
            },
            domain);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return std::nullopt;
        }
        ViewHandle target = assetView(std::move(*asset));
        WindowHandle window = process().open(*target);
        process().configure(*window, "hue-200-saturation-0.6-lightness");
        const int errorsBefore = context().errorCount();
        EXPECT_TRUE(applyAndSettle(*window, *target));
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
        EXPECT_EQ(context().errorCount(), errorsBefore)
            << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
        window.reset();
        dropView(context(), target);
        return result;
    };
    const std::optional<fixtures::FixtureSamples> physical =
        colorized.template operator()<float>(an::NumericDomain::PhysicalScaled);
    const std::optional<fixtures::FixtureSamples> floating =
        colorized.template operator()<float>(an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(physical.has_value() && floating.has_value());
    EXPECT_LE(largestDifference(*physical, *floating), 1.0e-6) << "a floating physical quantity";
    const std::optional<fixtures::FixtureSamples> codes =
        colorized.template operator()<std::uint16_t>(an::NumericDomain::RawSensorCode);
    const std::optional<fixtures::FixtureSamples> sixteen =
        colorized.template operator()<std::uint16_t>(an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(codes.has_value() && sixteen.has_value());
    EXPECT_EQ(largestDifference(*codes, *sixteen), 0.0) << "sensor codes";
}

namespace
{

class ExposureControlTest : public NamedProcessTest
{
protected:
    ExposureControlTest() : NamedProcessTest("Exposure") {}
};

} // namespace

// launch: the exposure control says what its factor does - which is not stops, and which still brightens between
// -0.10 and zero - rather than promising stops and a darkening it does not give.
TEST_F(ExposureControlTest, TheControlSaysWhatItsFactorDoes)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgb,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return static_cast<float>(sceneValue(x, y, c)); });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    WindowHandle window = process().open(*target);
    for (const char* name : {"exposureValue", "exposureSlider"})
    {
        auto* control = window->findChild<QWidget*>(QString::fromLatin1(name));
        ASSERT_NE(control, nullptr) << name;
        const QString said = control->toolTip();
        EXPECT_FALSE(said.contains(QStringLiteral("stops"))) << name << ": " << said.toStdString();
        EXPECT_FALSE(said.contains(QStringLiteral("Negative values darken"))) << name << ": " << said.toStdString();
        EXPECT_TRUE(said.contains(QStringLiteral("1 + value / 5"))) << name << ": " << said.toStdString();
        EXPECT_TRUE(said.contains(QStringLiteral("1.02 + value / 5"))) << name << ": " << said.toStdString();
        EXPECT_TRUE(said.contains(QStringLiteral("-0.10"))) << name << ": " << said.toStdString();
    }
    EXPECT_FLOAT_EQ(ExposureCorrectionProcess::exposureFactor(1.0), 1.2f);
    EXPECT_FLOAT_EQ(ExposureCorrectionProcess::exposureFactor(-1.0), 0.82f);
    EXPECT_FLOAT_EQ(ExposureCorrectionProcess::exposureFactor(-0.1), 1.0f);
    EXPECT_GT(ExposureCorrectionProcess::exposureFactor(-0.05), 1.0f) << "a small negative value still brightens";
    window.reset();
    dropView(context(), target);
}

// preview: a run over a view of a sixteen-bit target takes the whole target's background in the units the lightness is
// compared in, fractions of full scale, so a frame over the reduction of such a target is enhanced where its Apply is.
TEST_F(SaturationBackgroundTest, ARunOverAReductionOfASixteenBitTargetTakesItsBackgroundInFractionsOfFullScale)
{
    // A red of 0.6, a green of 0.3 and a blue of 0.1 everywhere: the lightness, (0.6 + 0.1) / 2 = 0.35, lies above the
    // background, the green median 0.3 with no deviation, so every pixel is enhanced under a background measured in
    // fractions of full scale and none under one measured in codes, which the unit interval holds to one.
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<std::uint16_t>(
        session(), kWidth, kHeight, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return static_cast<std::uint16_t>(c == 0u ? 39321u : (c == 1u ? 19661u : 6554u));
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    auto* window = new SaturationBody(target.get());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    WindowHandle owned(window);
    process().configure(*window, "amount-0.5");

    std::optional<an::consumer::ImageAsset> reduced;
    {
        an::Result<an::consumer::OperationScope> scope =
            session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue());
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue());
        an::Result<an::consumer::ImageAsset> made =
            an::ui::reduceForPreview(session(), scope->engine(), scope->execution(), *lease, 2u);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        reduced.emplace(std::move(*made));
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> source = fixtures::engine::readAssetSamples(*reduced, error);
    ASSERT_TRUE(source.has_value()) << error;
    {
        an::Result<an::consumer::AssetReadLease> reducedLease = reduced->read();
        ASSERT_TRUE(reducedLease.hasValue());
        const an::ui::ProcessRun overItself{.kind = an::ui::ProcessRunKind::Preview};
        const std::optional<fixtures::FixtureSamples> own = window->runOver(*reducedLease, overItself);
        ASSERT_TRUE(own.has_value());
        EXPECT_GT(largestDifference(*own, *source), 0.0) << "the image under its own background is left as it was";

        an::ui::ProcessRun overReduction{.kind = an::ui::ProcessRunKind::Preview,
                                         .frameKind = an::ui::PreviewFrameKind::Interactive};
        overReduction.proxyFactor = 2u;
        const std::optional<fixtures::FixtureSamples> framed = window->runOver(*reducedLease, overReduction);
        ASSERT_TRUE(framed.has_value());
        EXPECT_EQ(largestDifference(*framed, *own), 0.0)
            << "a frame over the reduction of a sixteen-bit target is not enhanced as the target is";
        EXPECT_GT(largestDifference(*framed, *source), 0.0) << "the frame over the reduction enhanced nothing";
    }
    owned.reset();
    dropView(context(), target);
}

// preview: a run over a view of a floating target whose rule is not all-valid - here exact zero is no data - leaves the
// samples holding no number out of the whole target's background, as the Apply leaves them out of its own: the frame
// lands, enhanced as a run over the target itself is, rather than refused for a sample the Apply never reads.
TEST_F(SaturationBackgroundTest, AFrameOverAFloatingTargetHoldingNoNumberUnderAValueRuleTakesTheApplysBackground)
{
    // The scene of the reduction case above - one pixel of every two-by-two block a green of 0.9, the rest a green of
    // 0.1, each under a red of 0.6 and a blue of 0.1 - with no number in the green of three pixels and a zero in that
    // of a fourth.
    const auto greenAt = [](std::uint32_t x, std::uint32_t y) -> float {
        if ((x == 3u && y == 2u) || (x == 10u && y == 7u) || (x == 17u && y == 12u))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        if (x == 5u && y == 9u)
        {
            return 0.0f;
        }
        return x % 2u == 0u && y % 2u == 0u ? 0.9f : 0.1f;
    };
    an::ImageSemanticsRequest meaning = an::ImageSemantics{}.toRequest();
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroIsNoData.has_value());
    meaning.validity = *zeroIsNoData;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, kWidth, kHeight, Channels::Rgb);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<float>> image = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt));
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        ASSERT_TRUE(access.has_value());
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                auto row = access->view().row(c, y);
                ASSERT_TRUE(row.has_value());
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    (*row)[x] = c == 0u ? 0.6f : (c == 1u ? greenAt(x, y) : 0.1f);
                }
            }
        }
    }

    // The background the Apply measures: the target's own samples, under the rule with every sample holding no number
    // counted out.
    std::optional<double> applied;
    {
        an::Result<an::Image<float>> copy = image->clone();
        ASSERT_TRUE(copy.hasValue()) << copy.status().message();
        std::optional<an::ImageWriteAccess<float>> access = copy->write();
        ASSERT_TRUE(access.has_value());
        an::Result<process_utils::NormalizedSamples> samples = process_utils::NormalizedSamples::open(
            scope->engine(), scope->execution(), *described, access->view().data(), access->view().layout(),
            an::KernelValidityView{.descriptor = *zeroIsNoData});
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope->execution());
        ASSERT_TRUE(skipping.hasValue()) << skipping.status().message();
        const an::Result<double> background = an::deriveSaturationEnhancementBackground(
            scope->execution(), samples->sampleType(), samples->samples(), samples->layout(), 1.0,
            an::SaturationBackgroundLocation::Median,
            an::StatisticsSelection{.validity = skipping->descriptor, .mask = skipping->mask});
        ASSERT_TRUE(background.hasValue()) << background.status().message();
        applied = *background;
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();
    ViewHandle target = assetView(std::move(*asset));
    auto* window = new SaturationBody(target.get());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    WindowHandle owned(window);
    process().configure(*window, "amount-0.5");

    {
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        {
            std::optional<an::consumer::OperationScope> measuring = an::tests::viewer::admitted(session());
            ASSERT_TRUE(measuring.has_value());
            const an::Result<double> measured = ColorSaturationProcess::measureBackgroundAtFactorOne(
                measuring->engine(), measuring->execution(), *lease);
            ASSERT_TRUE(measured.hasValue()) << measured.status().message();
            EXPECT_EQ(*measured, *applied) << "the whole target's background is not the one its Apply measures";
        }
        const std::optional<fixtures::FixtureSamples> source = documentSamples(target->document());
        ASSERT_TRUE(source.has_value());
        const an::ui::ProcessRun overItself{.kind = an::ui::ProcessRunKind::Preview};
        const std::optional<fixtures::FixtureSamples> own = window->runOver(*lease, overItself);
        ASSERT_TRUE(own.has_value());
        EXPECT_GT(largestDifference(*own, *source), 0.0) << "the run over the target enhanced nothing";

        an::ui::ProcessRun overPart{.kind = an::ui::ProcessRunKind::Preview,
                                    .frameKind = an::ui::PreviewFrameKind::Interactive};
        overPart.partOfTarget = true;
        const std::optional<fixtures::FixtureSamples> framed = window->runOver(*lease, overPart);
        ASSERT_TRUE(framed.has_value());
        EXPECT_EQ(largestDifference(*framed, *own), 0.0)
            << "a frame over a view of the target is not enhanced as the target is";
    }
    owned.reset();
    dropView(context(), target);
}

// The background of an integer target is measured in its codes and read as the fractions of full scale the body
// compares lightness in - counted from the representation's smallest value - which is the figure a floating copy of
// the target in those fractions gives, for every integer representation.
TEST_F(SaturationBackgroundTest, AnIntegerTargetsBackgroundInItsCodesIsTheBackgroundOfItsFractions)
{
    const auto holdsAlike = [&]<typename SampleT>() {
        SCOPED_TRACE(an::sampleTypeName(an::kSampleTypeOf<SampleT>));
        const double smallest = static_cast<double>((std::numeric_limits<SampleT>::lowest)());
        const double range = static_cast<double>((std::numeric_limits<SampleT>::max)()) - smallest;
        // Codes spread over the whole range, each channel its own.
        std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<SampleT>(
            session(), 64u, 48u, Channels::Rgb, [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                const double fraction = static_cast<double>((x * 131u + y * 7919u + c * 21737u) % 1009u) / 1008.0;
                return static_cast<SampleT>(std::llround(smallest + fraction * range));
            });
        ASSERT_TRUE(asset.has_value());
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        const an::Result<double> measured =
            ColorSaturationProcess::measureBackgroundAtFactorOne(scope->engine(), scope->execution(), *lease);
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();

        const an::Result<an::consumer::AnyImage> fractions =
            an::ui::processes::makeStretchWorkingCopy(scope->engine(), scope->execution(), lease->image());
        ASSERT_TRUE(fractions.hasValue()) << fractions.status().message();
        const std::optional<an::ValidityDescriptor> finite =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
        const an::Result<double> expected = std::visit(
            [&](const auto& typed) {
                const auto view = typed.view();
                return an::deriveSaturationEnhancementBackground(
                    scope->execution(), std::decay_t<decltype(typed)>::kSampleType, view->data(), view->layout(), 1.0,
                    an::SaturationBackgroundLocation::Median, an::StatisticsSelection{.validity = *finite});
            },
            *fractions);
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        EXPECT_GT(*expected, 0.25) << "the scene's background lies inside the range";
        EXPECT_NEAR(*measured, *expected, 1.0e-6);
    };
    holdsAlike.template operator()<std::uint8_t>();
    holdsAlike.template operator()<std::int8_t>();
    holdsAlike.template operator()<std::uint16_t>();
    holdsAlike.template operator()<std::int16_t>();
    holdsAlike.template operator()<std::uint32_t>();
    holdsAlike.template operator()<std::int32_t>();
}

// An integer target whose rule a value decides - here exact zero is no data - is measured over the pixels the Apply
// measures: the rule judged on the stored codes, so a signed target's zero is no data and its smallest code is, and
// judged over the pixel, so a pixel whose red is zero is no data either, whatever its green holds.
TEST_F(SaturationBackgroundTest, AnIntegerTargetUnderAValueRuleIsMeasuredOverThePixelsTheApplyMeasures)
{
    constexpr std::uint32_t kSide = 64u;
    const auto codeAt = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) -> std::int16_t {
        // A fifth of the pixels have no red, under a bright green no measurement may take.
        if (x % 5u == 0u)
        {
            return static_cast<std::int16_t>(c == 0u ? 0 : (c == 1u ? 30000 : 500));
        }
        if (c != 1u)
        {
            return static_cast<std::int16_t>(c == 0u ? 1000 : 500);
        }
        // The green holds no data, the smallest code - which is data - and codes spread below the middle.
        const std::uint32_t kind = (x + y) % 4u;
        const int spread = static_cast<int>((x * 131u + y * 71u) % 20000u);
        return static_cast<std::int16_t>(kind == 0u ? 0 : (kind == 1u ? -32768 : -20000 + spread));
    };
    an::ImageSemanticsRequest meaning = an::ImageSemantics{}.toRequest();
    const std::optional<an::ValidityDescriptor> zeroIsNoData =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroIsNoData.has_value());
    meaning.validity = *zeroIsNoData;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Int16, kSide, kSide, Channels::Rgb);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<std::int16_t>> image = an::Image<std::int16_t>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt));
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    {
        std::optional<an::ImageWriteAccess<std::int16_t>> access = image->write();
        ASSERT_TRUE(access.has_value());
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < kSide; ++y)
            {
                auto row = access->view().row(c, y);
                ASSERT_TRUE(row.has_value());
                for (std::uint32_t x = 0u; x < kSide; ++x)
                {
                    (*row)[x] = codeAt(x, y, c);
                }
            }
        }
    }

    // The background the Apply measures: the target opened as fractions of full scale, its rule judged on the stored
    // codes, every sample holding no number counted out.
    std::optional<double> applied;
    {
        an::Result<an::Image<std::int16_t>> copy = image->clone();
        ASSERT_TRUE(copy.hasValue()) << copy.status().message();
        std::optional<an::ImageWriteAccess<std::int16_t>> access = copy->write();
        ASSERT_TRUE(access.has_value());
        an::Result<process_utils::NormalizedSamples> samples = process_utils::NormalizedSamples::open(
            scope->engine(), scope->execution(), *described, access->view().data(), access->view().layout(),
            an::KernelValidityView{.descriptor = *zeroIsNoData});
        ASSERT_TRUE(samples.hasValue()) << samples.status().message();
        const an::Result<an::KernelValidityView> skipping = samples->skippingNonFinite(scope->execution());
        ASSERT_TRUE(skipping.hasValue()) << skipping.status().message();
        const an::Result<double> background = an::deriveSaturationEnhancementBackground(
            scope->execution(), samples->sampleType(), samples->samples(), samples->layout(), 1.0,
            an::SaturationBackgroundLocation::Median,
            an::StatisticsSelection{.validity = skipping->descriptor, .mask = skipping->mask});
        ASSERT_TRUE(background.hasValue()) << background.status().message();
        applied = *background;
    }
    scope.reset();
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage(std::move(*image)));
    ASSERT_TRUE(asset.hasValue()) << asset.status().message();
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    std::optional<an::consumer::OperationScope> measuring = an::tests::viewer::admitted(session());
    ASSERT_TRUE(measuring.has_value());
    const an::Result<double> measured =
        ColorSaturationProcess::measureBackgroundAtFactorOne(measuring->engine(), measuring->execution(), *lease);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(*measured, *applied, 1.0e-6) << "the whole target's background is not the one its Apply measures";
}

// An integer target's background takes no floating copy of it: under a memory budget that holds a camera frame twice
// but not its Float32 copy beside it, the background is still measured, and is the frame's green fraction.
TEST_F(SaturationBackgroundTest, AnIntegerTargetsBackgroundIsMeasuredWithoutAFloatingCopyOfIt)
{
    constexpr std::uint32_t kFrameWidth = 1024u;
    constexpr std::uint32_t kFrameHeight = 512u;
    constexpr std::uint64_t kFrameBytes = std::uint64_t{kFrameWidth} * kFrameHeight * 3u * sizeof(std::uint16_t);
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 4u;
    settings.limits.memoryBudgetBytes = 2u * kFrameBytes;
    an::Result<an::consumer::EngineSession> budgeted = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(budgeted.hasValue()) << budgeted.status().message();
    std::optional<an::consumer::ImageAsset> frame = an::tests::viewer::makeAsset<std::uint16_t>(
        *budgeted, kFrameWidth, kFrameHeight, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return static_cast<std::uint16_t>(c == 0u ? 39321u : (c == 1u ? 19661u : 6554u));
        });
    ASSERT_TRUE(frame.has_value());
    an::Result<an::consumer::AssetReadLease> lease = frame->read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(*budgeted);
    ASSERT_TRUE(scope.has_value());
    const an::Result<double> measured =
        ColorSaturationProcess::measureBackgroundAtFactorOne(scope->engine(), scope->execution(), *lease);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(*measured, 19661.0 / 65535.0, 1.0e-12);
}

/// The gamma correction's window, for what its runs make of a floating sample below zero.
class GammaCorrectionTest : public NamedProcessTest
{
protected:
    GammaCorrectionTest() : NamedProcessTest("Gamma") {}
};

// apply and preview: a floating sample below zero - a stack's background noise, say - is answered a not-a-number by the
// power, as the records hold, in the settled preview as in the Apply; every other sample is its power. The preview is
// the Apply's bytes, so it never shows black where the Apply writes a not-a-number.
TEST_F(GammaCorrectionTest, AFloatingSampleBelowZeroIsANotANumberInThePreviewAsInTheApply)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session(), kWidth, kHeight, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return x == 2u && y == 1u ? -0.2f : 0.1f + 0.05f * static_cast<float>(c) + 0.01f * static_cast<float>(x);
        });
    ASSERT_TRUE(asset.has_value());
    ViewHandle target = assetView(std::move(*asset));
    WindowHandle window = process().open(*target);
    process().configure(*window, "gamma-2.2");

    auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
    ASSERT_TRUE(settleWindow(*window));
    ASSERT_NE(window->previewView(), nullptr);
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
    ASSERT_TRUE(shown.has_value());
    preview->setChecked(false);

    ASSERT_TRUE(applyAndSettle(*window, *target));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
    ASSERT_TRUE(result.has_value());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_TRUE(std::isnan(result->value(2u, 1u, c))) << "channel " << c << " of the Apply's sample below zero";
        EXPECT_TRUE(std::isnan(shown->value(2u, 1u, c))) << "channel " << c << " of the preview's sample below zero";
        EXPECT_NEAR(result->value(0u, 0u, c), std::pow(0.1 + 0.05 * c, 1.0 / 2.2), 1.0e-5) << "channel " << c;
        EXPECT_NEAR(result->value(5u, 3u, c), std::pow(0.1 + 0.05 * c + 0.05, 1.0 / 2.2), 1.0e-5) << "channel " << c;
    }
    EXPECT_EQ(largestDifference(*shown, *result), 0.0) << "the preview is the Apply's own operation";
    window.reset();
    dropView(context(), target);
}

// preview: the settled preview of a scene of 640 by 480 is the Apply, bit for bit, in Float32 and in sixteen bits -
// a scene wide enough for its rows to be split over the threads and its samples over the vector lanes and their
// remainders, which a scene of a few pixels never exercises.
TEST_P(PointwiseWindowTest, TheSettledPreviewOfALargeSceneIsTheApplyAcrossTheThreadsAndLanesItIsSplitOver)
{
    const PointwiseProcess& under = process();
    constexpr std::uint32_t kLargeWidth = 640u;
    constexpr std::uint32_t kLargeHeight = 480u;
    for (const bool sixteenBit : {false, true})
    {
        ViewHandle target;
        if (sixteenBit)
        {
            std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<std::uint16_t>(
                session(), kLargeWidth, kLargeHeight, Channels::Rgb,
                [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                    return codeOf<std::uint16_t>(sceneValue(x, y, c, kLargeWidth, kLargeHeight));
                });
            ASSERT_TRUE(asset.has_value());
            target = assetView(std::move(*asset), "large");
        }
        else
        {
            target = colourScene(session(), kLargeWidth, kLargeHeight);
        }
        ASSERT_NE(target, nullptr);
        WindowHandle window = under.open(*target);
        under.configure(*window, under.changingMode);
        auto* preview = window->findChild<QCheckBox*>(QStringLiteral("preview"));
        ASSERT_NE(preview, nullptr);
        preview->setChecked(true);
        ASSERT_TRUE(pumpUntil([&window]() { return window->runTelemetry().previewsCommitted >= 1u; }));
        ASSERT_TRUE(settleWindow(*window));
        ASSERT_NE(window->previewView(), nullptr);
        const std::optional<fixtures::FixtureSamples> shown = documentSamples(window->previewView()->document());
        ASSERT_TRUE(shown.has_value());

        preview->setChecked(false);
        ASSERT_TRUE(applyAndSettle(*window, *target));
        const std::optional<fixtures::FixtureSamples> applied = documentSamples(target->document());
        ASSERT_TRUE(applied.has_value());
        ASSERT_EQ(shown->channelCount(), applied->channelCount());
        EXPECT_EQ(largestDifference(*shown, *applied), 0.0)
            << (sixteenBit ? "sixteen-bit" : "Float32") << ": the settled preview of the large scene is not the Apply";
        window.reset();
        dropView(context(), target);
    }
}
