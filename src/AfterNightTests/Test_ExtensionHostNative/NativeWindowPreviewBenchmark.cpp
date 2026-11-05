// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension window's drag benchmark, run only when asked for: a native window over a frame, its slider pressed,
/// dragged a step at a time at a steady rate and released, each frame's source lent through the shared-buffer seam,
/// written by a stand-in for the extension's preview - a scale and an offset of every sample, in C++, on the frame's
/// worker - and taken back as the extension's result is; and what the drag cost reported: the previews shown per
/// second and by kind of frame, the pixels each frame lent, how long each spent lending, in the stand-in and taking its
/// result back, each step of the seam's way (`PreviewFrameSpans`), and how long the settled frame after the release
/// took. The Python an extension runs adds its own time to the stand-in's; what is measured is the window's and the
/// seam's.
///
/// It runs when AN_BENCH_IMAGE is `synthetic`, a 6016 by 4016 sixteen-bit grey frame, or `synthetic:<width>x<height>`.
/// The other knobs, each optional:
///
///   AN_BENCH_SCALING    full_only, decimated or native_region: what the window's manifest states; full_only by default
///   AN_BENCH_ZOOM100    1 zooms the preview view to 1:1 before the drag; 0, the default, fits it to the window
///   AN_BENCH_STEPS      the slider's steps, 60 by default
///   AN_BENCH_STEP_MS    the milliseconds between steps, 16 by default
///   AN_BENCH_FPS        the previews a second the Processing settings state, the product's thirty by default
///   AN_BENCH_CHANNELS   3 makes the frame colour; 1, the default, grey
///   AN_BENCH_FLOAT      1 makes the frame's samples Float32; 0, the default, sixteen-bit
///   AN_BENCH_POOL_LARGEST_MIB   the largest buffer the frames' pool keeps, in MiB, or `unbounded`; the product's
///                               bound by default
///   AN_BENCH_POOL_IDLE_MIB      the most the pool keeps idle in all, in MiB; the product's bound by default
///
/// The report is printed and recorded as the case's properties; the case asserts only that the drag ran and the window
/// went idle after it.

#include "ExtensionNativeProcessWindow.h"
#include "HostNativeTestSupport.h"
#include "HostPreviewFrames.h"
#include "app/SharedBufferManager.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSlider>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using an::tests::host_native::FramesLandFirst;
using an::tests::host_native::showInView;
using an::tests::host_native::suiteContext;
using an::tests::host_native::suiteSession;
using an::tests::host_native::waitUntil;
using an::ui::extensions::ExtensionPreviewFrame;
using an::ui::extensions::SharedImageBuffer;
using Window = an::ui::extensions::ExtensionNativeProcessWindow;

/// A knob's whole number, or its default when it is not set or not a number.
[[nodiscard]] int knob(const char* name, int fallback)
{
    bool ok = false;
    const int value = qEnvironmentVariable(name).toInt(&ok);
    return ok ? value : fallback;
}

/// A knob stating bytes in MiB - `unbounded` for no bound - or its default when it is not set or not a number.
[[nodiscard]] std::uint64_t mebibytesKnob(const char* name, std::uint64_t fallback)
{
    const QString named = qEnvironmentVariable(name).trimmed();
    if (named == QStringLiteral("unbounded"))
    {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    bool ok = false;
    const qulonglong mebibytes = named.toULongLong(&ok);
    return ok ? static_cast<std::uint64_t>(mebibytes) << 20u : fallback;
}

/// Bytes in MiB, as the report states a bound.
[[nodiscard]] std::string mebibytes(std::uint64_t bytes)
{
    return bytes == (std::numeric_limits<std::uint64_t>::max)() ? std::string("unbounded")
                                                                : std::to_string(bytes >> 20u);
}

/// A frame of the run's session, grey or colour, every sample a level; nothing when it cannot be made.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset> filledFrame(std::uint32_t width, std::uint32_t height,
                                                                  std::uint32_t channels, SampleT level)
{
    const an::consumer::EngineSession& session = suiteSession();
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, hold->engine().limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const bool colour = channels == 3u;
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::kSampleTypeOf<SampleT>,
                                   .channelSchema = colour ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono,
                                   .colorSpace = colour ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, level);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// What the stand-in's frames cost, gathered on their workers.
struct SeamCosts
{
    std::mutex mutex;
    std::uint64_t frames = 0u;
    std::uint64_t pixels = 0u;
    std::chrono::nanoseconds lend{0};
    std::chrono::nanoseconds transform{0};
    std::chrono::nanoseconds take{0};

    /// The seam's own steps, summed over the frames.
    std::chrono::nanoseconds lendSource{0};
    std::chrono::nanoseconds lendOutput{0};
    std::chrono::nanoseconds lendMask{0};
    std::chrono::nanoseconds takeBack{0};
    std::chrono::nanoseconds release{0};

    void clear()
    {
        const std::lock_guard lock(mutex);
        frames = 0u;
        pixels = 0u;
        lend = transform = take = std::chrono::nanoseconds{0};
        lendSource = lendOutput = lendMask = takeBack = release = std::chrono::nanoseconds{0};
    }

    void add(const an::ui::extensions::PreviewFrameSpans& spans)
    {
        lendSource += spans.lendSource;
        lendOutput += spans.lendOutput;
        lendMask += spans.lendMask;
        takeBack += spans.take;
        release += spans.release;
    }
};

/// Every sample of a lent plane scaled and offset into the output's, as an extension's own transform writes it.
template <typename SampleT>
void scaleRows(const SharedImageBuffer& source, SharedImageBuffer& output, double scale, double offset, double ceiling)
{
    const auto& in = source.descriptor();
    const auto& out = output.descriptor();
    for (int plane = 0; plane < in.channels; ++plane)
    {
        for (int y = 0; y < in.height; ++y)
        {
            const std::byte* from = source.bytes() + plane * in.plane_stride + y * in.row_stride;
            std::byte* to = output.bytes() + plane * out.plane_stride + y * out.row_stride;
            for (int x = 0; x < in.width; ++x)
            {
                SampleT sample{};
                std::memcpy(&sample, from + x * sizeof(SampleT), sizeof(SampleT));
                const double scaled = (std::min)(ceiling, static_cast<double>(sample) * scale + offset);
                const auto written = static_cast<SampleT>(scaled);
                std::memcpy(to + x * sizeof(SampleT), &written, sizeof(SampleT));
            }
        }
    }
}

/// The stand-in for an extension's preview: the lent source, sixteen-bit or Float32 as the benchmark made the frame,
/// scaled and offset into the output.
void scaleInto(const SharedImageBuffer& source, SharedImageBuffer& output, bool floating)
{
    if (floating)
    {
        scaleRows<float>(source, output, 1.1, 0.0015, 1.0);
    }
    else
    {
        scaleRows<std::uint16_t>(source, output, 1.1, 100.0, 65535.0);
    }
}

[[nodiscard]] double milliseconds(std::chrono::nanoseconds duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

} // namespace

TEST(NativeWindowPreviewBenchmark, ASliderDragThroughTheFrameSeam)
{
    const QString named = qEnvironmentVariable("AN_BENCH_IMAGE");
    if (!named.startsWith(QStringLiteral("synthetic")))
    {
        GTEST_SKIP() << "set AN_BENCH_IMAGE to synthetic, or synthetic:<width>x<height>, to run the extension window's "
                        "drag benchmark";
    }
    std::uint32_t width = 6016u;
    std::uint32_t height = 4016u;
    if (const qsizetype colon = named.indexOf(QLatin1Char(':')); colon >= 0)
    {
        const QStringList size = named.mid(colon + 1).split(QLatin1Char('x'));
        ASSERT_EQ(size.size(), 2) << named.toStdString();
        width = size[0].toUInt();
        height = size[1].toUInt();
    }
    const QString scalingName = qEnvironmentVariable("AN_BENCH_SCALING", QStringLiteral("full_only"));
    const an::ui::PreviewScaling scaling =
        scalingName == QStringLiteral("decimated")       ? an::ui::PreviewScaling::Decimated
        : scalingName == QStringLiteral("native_region") ? an::ui::PreviewScaling::NativeRegion
                                                         : an::ui::PreviewScaling::FullOnly;
    const bool zoom100 = knob("AN_BENCH_ZOOM100", 0) != 0;
    const int steps = knob("AN_BENCH_STEPS", 60);
    const int stepMs = knob("AN_BENCH_STEP_MS", 16);
    const int fps = knob("AN_BENCH_FPS", 30);
    const std::uint32_t channels = knob("AN_BENCH_CHANNELS", 1) == 3 ? 3u : 1u;
    const bool floating = knob("AN_BENCH_FLOAT", 0) != 0;
    const QString rateKey = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);
    const std::optional<QString> storedRate = suiteContext().selectOptional(rateKey);
    (void)suiteContext().upsert(rateKey, QString::number(fps));

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const an::ui::extensions::PreviewFrameBufferPoolBounds product;
    const an::ui::extensions::PreviewFrameBufferPoolBounds bounds{
        .largestBufferBytes = mebibytesKnob("AN_BENCH_POOL_LARGEST_MIB", product.largestBufferBytes),
        .idleBytes = mebibytesKnob("AN_BENCH_POOL_IDLE_MIB", product.idleBytes),
        .idleFrames = product.idleFrames};
    // The pool outlives the window, whose frames land before it goes.
    an::ui::extensions::PreviewFrameBufferPool pool(
        an::ui::extensions::SharedBufferManager(std::filesystem::path(root.path().toStdWString())), "bench", bounds);
    const an::consumer::EngineSession* session = &suiteSession();
    auto costs = std::make_shared<SeamCosts>();

    Window window("Extension Window Benchmark", "session-bench", nullptr);
    window.setPreviewScaling(scaling, 2u);
    window.setPreviewBody(
        [&pool, session, costs, floating](const ExtensionPreviewFrame& frame) -> an::Result<an::consumer::ImageAsset> {
            an::ui::extensions::PreviewFrameSpans spans;
            const auto started = std::chrono::steady_clock::now();
            an::Result<an::ui::extensions::PreviewFrameBuffers> lent =
                an::ui::extensions::lendPreviewFrame(pool, frame, &spans);
            if (!lent.hasValue())
            {
                return lent.status();
            }
            const auto lentAt = std::chrono::steady_clock::now();
            scaleInto(lent->source, lent->output, floating);
            const auto writtenAt = std::chrono::steady_clock::now();
            const std::uint64_t pixels = static_cast<std::uint64_t>(lent->source.descriptor().width) *
                                         static_cast<std::uint64_t>(lent->source.descriptor().height);
            an::Result<an::consumer::ImageAsset> made =
                an::ui::extensions::takePreviewFrameResult(pool.manager(), *session, *lent, &spans);
            lent->release(&spans);
            const auto takenAt = std::chrono::steady_clock::now();
            const std::lock_guard lock(costs->mutex);
            ++costs->frames;
            costs->pixels += pixels;
            costs->lend += lentAt - started;
            costs->transform += writtenAt - lentAt;
            costs->take += takenAt - writtenAt;
            costs->add(spans);
            return made;
        });
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.0},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.01},
    }});

    auto source_view = std::make_unique<ImageViewControl>();
    source_view->initialize("bench_source", ViewControlMode::st_preview);
    std::optional<an::consumer::ImageAsset> frameImage =
        floating ? filledFrame<float>(width, height, channels, 0.3f)
                 : filledFrame<std::uint16_t>(width, height, channels, std::uint16_t{20000u});
    ASSERT_TRUE(frameImage.has_value());
    showInView(*source_view, std::move(*frameImage));
    window.setSourceView(source_view.get());
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle() && costs->frames >= 1u; }, 120000));
    ImageViewControl* preview = window.previewView();
    ASSERT_NE(preview, nullptr);
    if (zoom100)
    {
        preview->getViewWidget()->scaleImageView(1.0, true, true);
        ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }, 120000));
    }
    costs->clear();
    const ProcessRunTelemetry before = window.runTelemetry();

    auto* slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    const auto dragStart = std::chrono::steady_clock::now();
    slider->setSliderDown(true);
    for (int step = 1; step <= steps; ++step)
    {
        slider->setValue(step % 100);
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepMs);
        while (std::chrono::steady_clock::now() < until)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            std::this_thread::sleep_for(1ms);
        }
    }
    const ProcessRunTelemetry atRelease = window.runTelemetry();
    std::uint64_t dragFrames = 0u;
    std::uint64_t dragPixels = 0u;
    {
        const std::lock_guard lock(costs->mutex);
        dragFrames = costs->frames;
        dragPixels = costs->pixels;
    }
    const auto released = std::chrono::steady_clock::now();
    slider->setSliderDown(false);
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }, 120000));
    const auto settled = std::chrono::steady_clock::now();
    const ProcessRunTelemetry after = window.runTelemetry();

    const double dragSeconds = std::chrono::duration<double>(released - dragStart).count();
    const std::uint64_t interactive = atRelease.interactivePreviewsCommitted - before.interactivePreviewsCommitted;
    const std::uint64_t frames = (std::max)(costs->frames, std::uint64_t{1u});
    const auto mean = [frames](std::chrono::nanoseconds total) {
        return std::to_string(milliseconds(total) / static_cast<double>(frames));
    };
    std::printf("[ extension drag ] %ux%u %s %s, %s, %s, %d steps every %d ms at %d previews a second\n", width, height,
                channels == 3u ? "RGB" : "grey", floating ? "Float32" : "UInt16", scalingName.toStdString().c_str(),
                zoom100 ? "1:1" : "fit", steps, stepMs, fps);
    const std::vector<std::pair<std::string, std::string>> fields{
        {"drag_s", std::to_string(dragSeconds)},
        {"interactive_shown_in_drag", std::to_string(interactive)},
        {"previews_per_s_in_drag", std::to_string(static_cast<double>(interactive) / dragSeconds)},
        {"frames_in_drag", std::to_string(dragFrames)},
        {"mpx_lent_per_drag_frame", std::to_string(static_cast<double>(dragPixels) / 1.0e6 /
                                                   static_cast<double>((std::max)(dragFrames, std::uint64_t{1u})))},
        {"view_factor", std::to_string(after.viewFrameFactor)},
        {"settled_shown", std::to_string(after.settledPreviewsCommitted - before.settledPreviewsCommitted)},
        {"settled_after_release_ms", std::to_string(milliseconds(settled - released))},
        {"lend_mean_ms", mean(costs->lend)},
        {"stand_in_mean_ms", mean(costs->transform)},
        {"take_mean_ms", mean(costs->take)},
        {"span_lend_source_ms", mean(costs->lendSource)},
        {"span_lend_output_ms", mean(costs->lendOutput)},
        {"span_lend_mask_ms", mean(costs->lendMask)},
        {"span_take_ms", mean(costs->takeBack)},
        {"span_release_ms", mean(costs->release)},
        {"discarded", std::to_string(after.previewsDiscarded - before.previewsDiscarded)},
        {"failed", std::to_string(after.previewsFailed - before.previewsFailed)},
        {"pool_largest_mib", mebibytes(bounds.largestBufferBytes)},
        {"pool_idle_mib", mebibytes(bounds.idleBytes)},
    };
    for (const auto& [name, value] : fields)
    {
        std::printf("  %-28s %s\n", name.c_str(), value.c_str());
        ::testing::Test::RecordProperty(name, value);
    }
    std::fflush(stdout);
    EXPECT_GT(interactive + (after.settledPreviewsCommitted - before.settledPreviewsCommitted), 0u);
    EXPECT_EQ(after.previewsFailed, before.previewsFailed);
    (void)suiteContext().upsert(rateKey, storedRate.value_or(QStringLiteral("30")));
}
