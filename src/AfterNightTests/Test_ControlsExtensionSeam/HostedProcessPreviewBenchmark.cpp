// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The hosted process's drag benchmark, run only when asked for: the extension window's drag
/// (`NativeWindowPreviewBenchmark`, in `Test_ExtensionHostNative`) through the hosted path. An extension's process the
/// application draws, over a target of the same frame, its slider pressed, dragged a step at a time at a steady rate
/// and released; each frame's source and output lent from the session's pool on the application's side of the seam, the
/// request sent over the session's socket, written by a stand-in for the extension's preview - the same scale and
/// offset of every sample, in C++ - on a thread of its own standing for the host's process, answered over the socket
/// and taken back as the extension's result is, the session pumped as the application pumps it (when its socket has
/// something to read, and every 100 ms). What the drag cost is reported as the window's benchmark reports it: the
/// previews shown per second and by kind of frame, the pixels each frame lent, how long the stand-in took, and how long
/// the settled frame after the release took. The two are run in alternation at the same frame size and the median of
/// each compared.
///
/// It runs when AN_BENCH_IMAGE is `synthetic`, a 6016 by 4016 sixteen-bit grey frame, or `synthetic:<width>x<height>`.
/// The other knobs are the window's benchmark's, each optional:
///
///   AN_BENCH_SCALING    full_only, decimated or native_region: what the extension's manifest states; full_only by
///   default AN_BENCH_ZOOM100    1 zooms the preview view to 1:1 before the drag; 0, the default, fits it to the page
///   AN_BENCH_STEPS      the slider's steps, 60 by default
///   AN_BENCH_STEP_MS    the milliseconds between steps, 16 by default
///   AN_BENCH_FPS        the previews a second the Processing settings state, the product's thirty by default
///   AN_BENCH_CHANNELS   3 makes the frame colour; 1, the default, grey
///   AN_BENCH_FLOAT      1 makes the frame's samples Float32; 0, the default, sixteen-bit
///
/// The report is printed and recorded as the case's properties; the case asserts only that the drag ran and the process
/// went idle after it.

#include "Test_ControlsExtensionSeam/HostedPanelStandIn.h"
#include "Test_ControlsExtensionSeam/HostedProcessContext.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionHostedSession.h"
#include "app/SharedBufferManager.h"
#include "controls/ExtensionHostedProcess.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalSocket>
#include <QMetaObject>
#include <QSlider>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using an::tests::seam::HostedPanelStandIn;
using an::tests::seam::HostedProcessContext;
using an::ui::extensions::ExecutionPreviewMessage;
using an::ui::extensions::ExecutionResultMessage;
using an::ui::extensions::ExtensionHostedProcess;
using an::ui::extensions::ExtensionHostedSession;
using an::ui::extensions::ExtensionHostManager;
using an::ui::extensions::ExtensionProtocolMessage;
using an::ui::extensions::ExtensionSessionController;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferRole;
using an::ui::extensions::SharedImageBuffer;

constexpr const char* kSessionId = "hosted-bench";

/// A knob's whole number, or its default when it is not set or not a number.
[[nodiscard]] int knob(const char* name, int fallback)
{
    bool ok = false;
    const int value = qEnvironmentVariable(name).toInt(&ok);
    return ok ? value : fallback;
}

[[nodiscard]] double milliseconds(std::chrono::nanoseconds duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

/// What the stand-in's frames cost, gathered on the host's thread.
struct HostCosts
{
    std::mutex mutex;
    std::uint64_t frames = 0u;
    std::uint64_t pixels = 0u;
    std::chrono::nanoseconds open{0};
    std::chrono::nanoseconds transform{0};

    void clear()
    {
        const std::lock_guard lock(mutex);
        frames = 0u;
        pixels = 0u;
        open = transform = std::chrono::nanoseconds{0};
    }
};

/// Every sample of a lent plane scaled and offset into the output's, as an extension's own transform writes it: the
/// window's benchmark's stand-in, sample for sample.
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

/// The stand-in host served from a thread of its own, as a host's process serves its socket: each frame's output
/// written from its source by the stand-in body and answered; a frame whose cancel came with it, before it ran,
/// answered cancelled.
class ServingHost final : public QObject
{
public:
    ServingHost(std::unique_ptr<QLocalSocket> socket, bool floating, std::shared_ptr<HostCosts> costs)
        : m_socket(socket.release()), m_floating(floating), m_costs(std::move(costs))
    {
        m_socket->setParent(this);
    }

    /// On the host's thread.
    void start()
    {
        QObject::connect(m_socket, &QLocalSocket::readyRead, this, [this]() { serve(); });
        serve();
    }

private:
    void serve()
    {
        m_buffered.append(m_socket->readAll());
        std::vector<ExtensionProtocolMessage> messages;
        for (;;)
        {
            ExtensionProtocolMessage message;
            std::string error;
            if (an::ui::extensions::tryConsumeFramedProtocolMessage(&m_buffered, &message, &error) !=
                an::ui::extensions::ProtocolFrameDecodeStatus::Complete)
            {
                break;
            }
            messages.push_back(std::move(message));
        }
        std::set<std::string> cancelled;
        for (const ExtensionProtocolMessage& message : messages)
        {
            an::ui::extensions::ExecutionCancelMessage cancel;
            std::string error;
            if (message.type == "execution.cancel" &&
                an::ui::extensions::executionCancelFromProtocolMessage(message, &cancel, &error))
            {
                cancelled.insert(cancel.target_request_id);
            }
        }
        for (const ExtensionProtocolMessage& message : messages)
        {
            if (message.type == "execution.preview")
            {
                answer(message, cancelled);
            }
        }
    }

    void answer(const ExtensionProtocolMessage& request, const std::set<std::string>& cancelled)
    {
        ExecutionPreviewMessage preview;
        std::string error;
        if (!an::ui::extensions::executionPreviewFromProtocolMessage(request, &preview, &error))
        {
            return;
        }
        ExecutionResultMessage result;
        result.request_id = an::ui::extensions::generateOpaqueToken();
        result.target_request_id = preview.request_id;
        result.result_kind = "preview";
        const auto role = [&preview](const SharedBufferRole wanted) -> const SharedBufferDescriptor* {
            const auto found = std::find_if(preview.buffers.begin(), preview.buffers.end(),
                                            [wanted](const auto& buffer) { return buffer.buffer_role == wanted; });
            return found != preview.buffers.end() ? &*found : nullptr;
        };
        const SharedBufferDescriptor* source = role(SharedBufferRole::Source);
        const SharedBufferDescriptor* output = role(SharedBufferRole::Preview);
        result.status = "cancelled";
        if (!cancelled.contains(preview.request_id) && source != nullptr && output != nullptr)
        {
            const auto started = std::chrono::steady_clock::now();
            const an::ui::extensions::SharedBufferManager manager(an::ui::extensions::extensionHostManagedRootPath());
            // As the full host does: the files a frame names stay mapped for the next, and every other is let go.
            keepOnly(preview.buffers);
            an::Result<SharedImageBuffer> in = open(manager, *source);
            an::Result<SharedImageBuffer> out = open(manager, *output);
            const auto opened = std::chrono::steady_clock::now();
            if (in.hasValue() && out.hasValue())
            {
                scaleInto(*in, *out, m_floating);
                result.status = "succeeded";
                result.output_buffers = {*output};
            }
            else
            {
                result.status = "failed";
                result.detail = "the stand-in could not open the frame's buffers";
            }
            const auto written = std::chrono::steady_clock::now();
            const std::lock_guard lock(m_costs->mutex);
            ++m_costs->frames;
            m_costs->pixels += static_cast<std::uint64_t>(source->width) * static_cast<std::uint64_t>(source->height);
            m_costs->open += opened - started;
            m_costs->transform += written - opened;
        }
        result.session_id = kSessionId;
        std::string framingError;
        const QByteArray framed =
            an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(result), &framingError);
        m_socket->write(framed);
        m_socket->flush();
    }

    void keepOnly(const std::vector<SharedBufferDescriptor>& named)
    {
        for (auto it = m_kept.begin(); it != m_kept.end();)
        {
            const bool stillNamed =
                std::any_of(named.begin(), named.end(), [&it](const SharedBufferDescriptor& buffer) {
                    return buffer.mapped_file_path == it->first;
                });
            it = stillNamed ? std::next(it) : m_kept.erase(it);
        }
    }

    [[nodiscard]] an::Result<SharedImageBuffer> open(const an::ui::extensions::SharedBufferManager& manager,
                                                     const SharedBufferDescriptor& descriptor)
    {
        const auto kept = m_kept.find(descriptor.mapped_file_path);
        an::Result<SharedImageBuffer> opened =
            kept != m_kept.end() ? manager.openBuffer(descriptor, kept->second) : manager.openBuffer(descriptor);
        if (opened.hasValue())
        {
            m_kept.insert_or_assign(descriptor.mapped_file_path, opened->shared());
        }
        return opened;
    }

    QLocalSocket* m_socket = nullptr;
    std::map<std::string, SharedImageBuffer> m_kept;
    bool m_floating = false;
    std::shared_ptr<HostCosts> m_costs;
    QByteArray m_buffered;
};

/// Turns the event loop until a condition holds or the bound passes.
[[nodiscard]] bool waitUntil(const std::function<bool()>& condition, int boundMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < boundMs)
    {
        if (condition())
        {
            return true;
        }
        // Waiting in the event loop, as the application waits: woken by what is posted to it.
        QEventLoop pause;
        QTimer::singleShot(1, &pause, &QEventLoop::quit);
        pause.exec();
    }
    return condition();
}

} // namespace

TEST(HostedProcessPreviewBenchmark, ASliderDragThroughTheHostedPath)
{
    const QString named = qEnvironmentVariable("AN_BENCH_IMAGE");
    if (!named.startsWith(QStringLiteral("synthetic")))
    {
        GTEST_SKIP() << "set AN_BENCH_IMAGE to synthetic, or synthetic:<width>x<height>, to run the hosted process's "
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
    const bool colour = knob("AN_BENCH_CHANNELS", 1) == 3;
    const bool floating = knob("AN_BENCH_FLOAT", 0) != 0;

    // The application's session as wide as the window's benchmark's, and its target of the same frame.
    std::optional<an::tests::controls::TestSessionProvider> provider =
        an::tests::viewer::openFileProvider(an::kUseHostConcurrency, an::consumer::MemoryAllowances{});
    ASSERT_TRUE(provider.has_value());
    auto context = std::make_unique<HostedProcessContext>(std::move(provider->owned()));
    const an::tests::viewer::InstalledContext installed(*context);
    (void)context->getGlobalSettings()->upsert(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE),
                                               QString::number(fps));
    an::tests::viewer::ViewHandle target = an::tests::viewer::makeView("bench_target");
    const an::tests::viewer::Channels channels =
        colour ? an::tests::viewer::Channels::Rgb : an::tests::viewer::Channels::Grey;
    std::optional<an::ui::ImageDocument> document =
        floating ? an::tests::viewer::makeDocument<float>(
                       context->engineSession(), width, height, channels,
                       [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.3f; },
                       an::ui::DocumentNames{QStringLiteral("bench_target"), QStringLiteral("C:/images/bench.fit")})
                 : an::tests::viewer::makeDocument<std::uint16_t>(
                       context->engineSession(), width, height, channels,
                       [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint16_t{20000u}; },
                       an::ui::DocumentNames{QStringLiteral("bench_target"), QStringLiteral("C:/images/bench.fit")});
    ASSERT_TRUE(document.has_value());
    target->setDocument(std::move(*document));
    ASSERT_TRUE(an::tests::viewer::settle(*target));

    // The session of a host serving the panel, its far end served on a thread of its own.
    auto controller = std::make_unique<ExtensionSessionController>();
    controller->descriptor_id = "bench.extension.process";
    controller->display_name = "Hosted Benchmark";
    controller->target_control = target.get();
    HostedPanelStandIn host;
    host.install(*controller, kSessionId);
    const QCborArray params{QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                                     {QStringLiteral("type"), QStringLiteral("double")},
                                     {QStringLiteral("label"), QStringLiteral("Strength")},
                                     {QStringLiteral("default"), 0.0},
                                     {QStringLiteral("min"), 0.0},
                                     {QStringLiteral("max"), 1.0},
                                     {QStringLiteral("step"), 0.01}}};
    host.describe(params);
    auto costs = std::make_shared<HostCosts>();
    QThread hostThread;
    auto* serving = new ServingHost(host.releasePeer(), floating, costs);
    serving->moveToThread(&hostThread);
    hostThread.start();
    QMetaObject::invokeMethod(serving, [serving]() { serving->start(); }, Qt::QueuedConnection);

    ExtensionHostedProcess::Setup setup;
    setup.displayName = "Hosted Benchmark";
    setup.previews = true;
    setup.previewScaling = scaling;
    setup.previewRegionMargin = 2u;
    setup.keptParams = params.toJsonArray();
    QPointer<ExtensionHostedProcess> process = new ExtensionHostedProcess(target.get(), std::move(setup));
    std::shared_ptr<ExtensionHostedSession> hosted = process->hostedSession();
    ExtensionHostManager manager;
    hosted->bind(manager, *controller);

    // Pumped as the application pumps a session: when its socket has something to read, and on a timer.
    QObject pumping;
    bool wakePending = false;
    const auto pump = [&manager, &controller, &hosted]() {
        std::string error;
        static_cast<void>(manager.pumpSession(&controller->running_session, 0, &error));
        hosted->route();
        hosted->dispatch();
    };
    QObject::connect(controller->running_session.socket.get(), &QLocalSocket::readyRead, &pumping,
                     [&wakePending, &pumping, pump]() {
                         if (wakePending)
                         {
                             return;
                         }
                         wakePending = true;
                         QTimer::singleShot(0, &pumping, [&wakePending, pump]() {
                             wakePending = false;
                             pump();
                         });
                     });
    QTimer pumpTimer;
    pumpTimer.setInterval(100);
    QObject::connect(&pumpTimer, &QTimer::timeout, &pumping, pump);
    pumpTimer.start();
    pump();

    process->resize(1200, 800);
    process->show();
    ASSERT_TRUE(
        waitUntil([&]() { return process->hostReady() && process->isProcessIdle() && costs->frames >= 1u; }, 120000));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    if (zoom100)
    {
        preview->getViewWidget()->scaleImageView(1.0, true, true);
        ASSERT_TRUE(waitUntil([&]() { return process->isProcessIdle(); }, 120000));
    }
    costs->clear();
    const ProcessRunTelemetry before = process->runTelemetry();

    auto* slider = process->findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    const auto dragStart = std::chrono::steady_clock::now();
    slider->setSliderDown(true);
    // A step at a steady rate from a precise timer, the event loop waiting for events between the steps as the
    // application's does: the GUI thread is never asleep while a frame's request, its answer or its result waits for
    // it.
    int step = 1;
    slider->setValue(step % 100);
    QEventLoop drag;
    QTimer stepper;
    stepper.setTimerType(Qt::PreciseTimer);
    stepper.setInterval(stepMs);
    QObject::connect(&stepper, &QTimer::timeout, &drag, [&]() {
        if (step >= steps)
        {
            stepper.stop();
            drag.quit();
            return;
        }
        ++step;
        slider->setValue(step % 100);
    });
    stepper.start();
    drag.exec();
    const ProcessRunTelemetry atRelease = process->runTelemetry();
    std::uint64_t dragFrames = 0u;
    std::uint64_t dragPixels = 0u;
    {
        const std::lock_guard lock(costs->mutex);
        dragFrames = costs->frames;
        dragPixels = costs->pixels;
    }
    const auto released = std::chrono::steady_clock::now();
    slider->setSliderDown(false);
    ASSERT_TRUE(waitUntil([&]() { return process->isProcessIdle(); }, 120000));
    const auto settled = std::chrono::steady_clock::now();
    const ProcessRunTelemetry after = process->runTelemetry();

    const double dragSeconds = std::chrono::duration<double>(released - dragStart).count();
    const std::uint64_t interactive = atRelease.interactivePreviewsCommitted - before.interactivePreviewsCommitted;
    const std::uint64_t frames = (std::max)(costs->frames, std::uint64_t{1u});
    const auto mean = [frames](std::chrono::nanoseconds total) {
        return std::to_string(milliseconds(total) / static_cast<double>(frames));
    };
    std::printf("[ hosted drag ] %ux%u %s %s, %s, %s, %d steps every %d ms at %d previews a second\n", width, height,
                colour ? "RGB" : "grey", floating ? "Float32" : "UInt16", scalingName.toStdString().c_str(),
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
        {"host_open_mean_ms", mean(costs->open)},
        {"stand_in_mean_ms", mean(costs->transform)},
        {"discarded", std::to_string(after.previewsDiscarded - before.previewsDiscarded)},
        {"failed", std::to_string(after.previewsFailed - before.previewsFailed)},
    };
    for (const auto& [name, value] : fields)
    {
        std::printf("  %-28s %s\n", name.c_str(), value.c_str());
        ::testing::Test::RecordProperty(name, value);
    }
    std::fflush(stdout);
    EXPECT_GT(interactive + (after.settledPreviewsCommitted - before.settledPreviewsCommitted), 0u);
    EXPECT_EQ(after.previewsFailed, before.previewsFailed);

    // The frames land before the process goes, and the host's thread stops before its socket's session does.
    process->stopPreviews();
    ASSERT_TRUE(waitUntil([&]() { return process->isRunIdle(); }, 20000));
    pumpTimer.stop();
    delete process.data();
    hosted->unbind({.sessionId = kSessionId, .failed = false, .reason = {}});
    QMetaObject::invokeMethod(serving, [serving]() { delete serving; }, Qt::BlockingQueuedConnection);
    hostThread.quit();
    hostThread.wait();
    controller.reset();
    target.reset();
}
