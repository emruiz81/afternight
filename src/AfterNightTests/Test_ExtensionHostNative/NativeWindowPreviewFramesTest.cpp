// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The native window's preview is the framework's: frames of a drag are interactive, paced at the Processing settings'
/// rate, and a settled frame follows the release; what an interactive frame hands the extension is what its manifest
/// lets it run over - the image reduced to what the preview view paints for a Decimated extension, the part the view
/// shows grown by its reach for a NativeRegion one, the whole image for a FullOnly one - with the manual mask laid over
/// it the same way; an Apply's source is the image itself, never a frame; and the window's close leaves no file of its
/// frames behind.

#include "ExtensionNativeProcessWindow.h"
#include "HostNativeTestSupport.h"
#include "HostPreviewFrames.h"
#include "app/SharedBufferManager.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using an::tests::host_native::FramesLandFirst;
using an::tests::host_native::PreviewFrameGate;
using an::tests::host_native::PreviewFrameRecord;
using an::tests::host_native::recordingPreviewBody;
using an::tests::host_native::showGrey;
using an::tests::host_native::suiteContext;
using an::tests::host_native::viewSampleAt;
using an::tests::host_native::waitUntil;
using Window = an::ui::extensions::ExtensionNativeProcessWindow;

/// States a frame rate in the host's settings, and states the one it held - or the default - again when it goes.
class ScopedFrameRate
{
public:
    explicit ScopedFrameRate(const QString& rate) : m_previous(suiteContext().selectOptional(key()))
    {
        (void)suiteContext().upsert(key(), rate);
    }

    ~ScopedFrameRate()
    {
        (void)suiteContext().upsert(key(), m_previous.value_or(QStringLiteral("30")));
    }

    ScopedFrameRate(const ScopedFrameRate&) = delete;
    ScopedFrameRate& operator=(const ScopedFrameRate&) = delete;

private:
    [[nodiscard]] static QString key()
    {
        return QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);
    }

    std::optional<QString> m_previous;
};

/// One slider of real numbers, as an extension's schema states it.
[[nodiscard]] QJsonObject strengthParam()
{
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("strength")},
        {QStringLiteral("type"), QStringLiteral("double")},
        {QStringLiteral("label"), QStringLiteral("Strength")},
        {QStringLiteral("default"), 0.25},
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("step"), 0.01},
    };
}

/// A preview-mode view the host mirrors an image in.
[[nodiscard]] std::unique_ptr<ImageViewControl> makeSourceView(const char* uniqueName)
{
    auto view = std::make_unique<ImageViewControl>();
    view->initialize(uniqueName, ViewControlMode::st_preview);
    return view;
}

/// Turns the event loop for a while, whatever happens.
void pumpFor(std::chrono::milliseconds duration)
{
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(1ms);
    }
}

/// Shows the window, waits for its first frame, scales its preview view and waits for the frame that follows.
[[nodiscard]] bool showScaled(Window& window, const PreviewFrameRecord& frames, double scale)
{
    window.resize(900, 640);
    window.show();
    if (!waitUntil([&]() { return frames.count() >= 1 && window.isProcessIdle(); }) ||
        window.previewView() == nullptr || window.previewView()->getViewWidget() == nullptr)
    {
        return false;
    }
    window.previewView()->getViewWidget()->scaleImageView(scale, true, true);
    pumpFor(50ms);
    return waitUntil([&]() { return window.isProcessIdle(); });
}

/// Drags the window's strength slider to a value, held, until a frame of it has been handed to the extension, and lets
/// it go; the frames from then on, the settled one last.
[[nodiscard]] std::vector<PreviewFrameRecord::Frame> dragTo(Window& window, PreviewFrameRecord& frames, int value)
{
    frames.clear();
    auto* slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    EXPECT_NE(slider, nullptr);
    if (slider == nullptr)
    {
        return {};
    }
    slider->setSliderDown(true);
    slider->setValue(value);
    EXPECT_TRUE(waitUntil([&]() { return frames.count() >= 1; })) << "the drag was never shown";
    slider->setSliderDown(false);
    EXPECT_TRUE(waitUntil([&]() {
        const std::optional<PreviewFrameRecord::Frame> last = frames.last();
        return last.has_value() && !last->interactive && window.isProcessIdle();
    })) << "the release was never settled";
    return frames.frames();
}

/// The width and height of what a view's document holds.
[[nodiscard]] QSize documentSize(const ImageViewControl& view)
{
    const std::optional<an::ImageDescriptor> described = view.imageDescriptor();
    return described.has_value()
               ? QSize(static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height()))
               : QSize();
}

} // namespace

TEST(NativeWindowPreviewFrames, ADragIsShownAtTheConfiguredRateAndSettlesOnRelease)
{
    // Ten a second: frames a tenth of a second apart, however fast the slider moves.
    const ScopedFrameRate rate(QStringLiteral("10"));
    Window window("Native Window", "session-frames-rate", nullptr);
    const auto frames = std::make_shared<PreviewFrameRecord>();
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{strengthParam()});
    auto source_view = makeSourceView("frames_rate_source");
    showGrey(*source_view, 64, 48, 0.25);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(waitUntil([&]() { return frames->count() >= 1 && window.isProcessIdle(); }));
    frames->clear();

    auto* slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    const auto drag_start = std::chrono::steady_clock::now();
    slider->setSliderDown(true);
    for (int value = 1; value <= 100; ++value)
    {
        slider->setValue(value);
        pumpFor(5ms);
    }
    slider->setSliderDown(false);
    const auto drag_length = std::chrono::steady_clock::now() - drag_start;
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle(); }));

    const std::vector<PreviewFrameRecord::Frame> seen = frames->frames();
    ASSERT_GE(seen.size(), 3u);
    for (std::size_t index = 0u; index + 1u < seen.size(); ++index)
    {
        EXPECT_TRUE(seen[index].interactive) << "frame " << index << " of the drag";
    }
    EXPECT_FALSE(seen.back().interactive) << "the drag ends on a settled frame";
    EXPECT_EQ(seen.back().params, window.currentParams()) << "the settled frame shows where the drag ended";
    for (std::size_t index = 1u; index < seen.size(); ++index)
    {
        // A tenth of a second, less what a worker's start adds to a frame's.
        EXPECT_GE(seen[index].started - seen[index - 1u].started, 80ms) << "frame " << index;
    }
    EXPECT_LE(seen.size(), static_cast<std::size_t>(drag_length / 100ms) + 2u);
}

TEST(NativeWindowPreviewFrames, ADecimatedExtensionIsHandedWhatThePreviewViewPaints)
{
    Window window("Native Window", "session-frames-decimated", nullptr);
    window.setPreviewScaling(an::ui::PreviewScaling::Decimated);
    const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{strengthParam()});
    auto source_view = makeSourceView("frames_decimated_source");
    showGrey(*source_view, 1600, 1200, 0.25);
    const std::shared_ptr<const an::consumer::ImageAsset> image = source_view->document().asset();
    window.setSourceView(source_view.get());
    ASSERT_TRUE(showScaled(window, *frames, 0.25));

    const std::vector<PreviewFrameRecord::Frame> seen = dragTo(window, *frames, 40);
    ASSERT_GE(seen.size(), 2u);
    const PreviewFrameRecord::Frame& interactive = seen.front();
    ASSERT_TRUE(interactive.interactive);
    EXPECT_GE(interactive.factor, 2u) << "an interactive frame runs over the image reduced";
    EXPECT_LT(interactive.width, 1600);
    EXPECT_GE(interactive.width * static_cast<int>(interactive.factor), 1600);
    EXPECT_GE(interactive.height * static_cast<int>(interactive.factor), 1200);
    EXPECT_TRUE(interactive.part.isEmpty());
    EXPECT_EQ(interactive.imageSize, QSize(1600, 1200));

    const PreviewFrameRecord::Frame& settled = seen.back();
    EXPECT_FALSE(settled.interactive);
    EXPECT_EQ(settled.factor, 1u) << "the settled frame runs over the image itself";
    EXPECT_EQ(settled.width, 1600);
    EXPECT_EQ(settled.height, 1200);
    ASSERT_NE(window.previewView(), nullptr);
    EXPECT_EQ(documentSize(*window.previewView()), QSize(1600, 1200));
    EXPECT_NEAR(viewSampleAt(*window.previewView(), 0, 0), 0.5, 1e-6);

    // An Apply runs over the image the source view shows, as it was: never a frame's reduction or result.
    EXPECT_EQ(window.sourceImage(), image);
    EXPECT_EQ(documentSize(*source_view), QSize(1600, 1200));
    EXPECT_NEAR(viewSampleAt(*source_view, 0, 0), 0.25, 1e-6);
}

TEST(NativeWindowPreviewFrames, AFullOnlyExtensionIsHandedTheWholeImageInEveryFrame)
{
    Window window("Native Window", "session-frames-full-only", nullptr);
    ASSERT_EQ(window.previewScaling(), an::ui::PreviewScaling::FullOnly) << "what an extension that states nothing is";
    const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{strengthParam()});
    auto source_view = makeSourceView("frames_full_only_source");
    showGrey(*source_view, 1600, 1200, 0.25);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(showScaled(window, *frames, 0.25));

    const std::vector<PreviewFrameRecord::Frame> seen = dragTo(window, *frames, 40);
    ASSERT_GE(seen.size(), 2u);
    for (const PreviewFrameRecord::Frame& frame : seen)
    {
        EXPECT_EQ(frame.factor, 1u);
        EXPECT_EQ(frame.width, 1600);
        EXPECT_EQ(frame.height, 1200);
        EXPECT_TRUE(frame.part.isEmpty());
    }
    EXPECT_TRUE(seen.front().interactive) << "a drag is still shown in interactive frames";
}

TEST(NativeWindowPreviewFrames, ANativeRegionExtensionIsHandedThePartShownGrownByItsReach)
{
    constexpr std::uint32_t kReach = 6u;
    Window window("Native Window", "session-frames-native-region", nullptr);
    window.setPreviewScaling(an::ui::PreviewScaling::NativeRegion, kReach);
    const auto frames = std::make_shared<PreviewFrameRecord>(0.5);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{strengthParam()});
    auto source_view = makeSourceView("frames_native_region_source");
    showGrey(*source_view, 1600, 1200, 0.25);
    window.setSourceView(source_view.get());
    ASSERT_TRUE(showScaled(window, *frames, 1.0));

    const std::vector<PreviewFrameRecord::Frame> seen = dragTo(window, *frames, 40);
    ASSERT_GE(seen.size(), 2u);
    const PreviewFrameRecord::Frame& interactive = seen.front();
    ASSERT_TRUE(interactive.interactive);
    EXPECT_EQ(interactive.factor, 1u) << "a part runs at full scale";
    ASSERT_FALSE(interactive.part.isEmpty()) << "a view at full scale shows a part of the image";
    EXPECT_LT(interactive.part.width(), 1600);
    EXPECT_EQ(interactive.width, interactive.part.width());
    EXPECT_EQ(interactive.height, interactive.part.height());
    EXPECT_TRUE(QRect(0, 0, 1600, 1200).contains(interactive.part));
    EXPECT_EQ(interactive.imageSize, QSize(1600, 1200));
    // The part reaches past what the view shows by the reach, where the image allows: it is wider than the viewport.
    const QWidget* viewport = window.previewView()->getScrollArea()->viewport();
    EXPECT_GE(interactive.part.width(), (std::min)(1600, viewport->width() + static_cast<int>(kReach)));
}

TEST(NativeWindowPreviewFrames, TheManualMaskIsLaidOverTheFrameAsItsSourceLiesOverTheImage)
{
    Window window("Native Window", "session-frames-manual-mask", nullptr);
    window.setPreviewScaling(an::ui::PreviewScaling::Decimated);
    window.setParamDefs(QJsonArray{
        strengthParam(),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("manual_mask")},
            {QStringLiteral("type"), QStringLiteral("manual_mask_editor")},
            {QStringLiteral("label"), QStringLiteral("Manual Protection Mask")},
            {QStringLiteral("use_param"), QStringLiteral("use_manual_mask")},
            {QStringLiteral("use_label"), QStringLiteral("Use manual protection mask")},
            {QStringLiteral("use_default"), false},
            {QStringLiteral("brush_size"), 40},
            {QStringLiteral("min_brush_size"), 2},
            {QStringLiteral("max_brush_size"), 200},
        },
    });
    auto source_view = makeSourceView("frames_manual_mask_source");
    showGrey(*source_view, 320, 240, 0.5);
    window.setSourceView(source_view.get());
    window.resize(900, 640);
    window.show();
    QTest::qWait(60);
    source_view->zoom100();
    QCoreApplication::processEvents();

    // A stroke across the image's middle, drawn on the source view while the window does not preview.
    auto* use_mask = window.findChild<QCheckBox*>(QStringLiteral("anNativeManualMaskUseCheckBox"));
    auto* edit_button = window.findChild<QPushButton*>(QStringLiteral("anNativeManualMaskEditButton"));
    ASSERT_NE(use_mask, nullptr);
    ASSERT_NE(edit_button, nullptr);
    edit_button->click();
    QCoreApplication::processEvents();
    QWidget* viewport = source_view->getScrollArea()->viewport();
    const QPoint origin = source_view->getScrollArea()->widget()->pos();
    const auto send = [&](QEvent::Type type, int x, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPointF at(origin.x() + x, origin.y() + 120);
        QMouseEvent event(type, at, viewport->mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &event);
    };
    send(QEvent::MouseButtonPress, 20, Qt::LeftButton, Qt::LeftButton);
    for (int x = 30; x <= 300; x += 10)
    {
        send(QEvent::MouseMove, x, Qt::NoButton, Qt::LeftButton);
    }
    send(QEvent::MouseButtonRelease, 300, Qt::LeftButton, Qt::NoButton);
    ASSERT_TRUE(waitUntil([&]() { return window.hasManualMask(); })) << "the stroke lands in the mask";
    edit_button->click();
    QCoreApplication::processEvents();
    ASSERT_TRUE(use_mask->isChecked());
    ASSERT_TRUE(window.manualMaskImage());

    // The window previews from here: a frame over the image reduced is handed the mask reduced with it.
    const auto frames = std::make_shared<PreviewFrameRecord>(0.75);
    window.setPreviewBody(recordingPreviewBody(frames));
    const FramesLandFirst landed(window);
    ASSERT_TRUE(showScaled(window, *frames, 0.25));
    const std::vector<PreviewFrameRecord::Frame> seen = dragTo(window, *frames, 40);
    ASSERT_GE(seen.size(), 2u);
    const PreviewFrameRecord::Frame& interactive = seen.front();
    ASSERT_GE(interactive.factor, 2u);
    EXPECT_TRUE(interactive.masked);
    EXPECT_EQ(interactive.maskWidth, interactive.width);
    EXPECT_EQ(interactive.maskHeight, interactive.height);
    const PreviewFrameRecord::Frame& settled = seen.back();
    EXPECT_TRUE(settled.masked);
    EXPECT_EQ(settled.maskWidth, 320);
    EXPECT_EQ(settled.maskHeight, 240);

    // While the mask is edited the preview shows the source itself, and the extension runs nothing.
    const int before = frames->count();
    edit_button->click();
    ASSERT_TRUE(waitUntil([&]() {
        return window.isProcessIdle() && std::abs(viewSampleAt(*window.previewView(), 0, 0) - 0.5) < 1e-6;
    })) << "the preview shows the source while the mask is drawn";
    EXPECT_EQ(frames->count(), before);
    edit_button->click();
    EXPECT_TRUE(waitUntil([&]() {
        return frames->count() > before && window.isProcessIdle() &&
               std::abs(viewSampleAt(*window.previewView(), 0, 0) - 0.75) < 1e-6;
    })) << "the extension's preview comes back once the mask is done";
}

TEST(NativeWindowPreviewFrames, AWindowsCloseLeavesNoFileOfItsFramesBehind)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const std::filesystem::path rootPath(root.path().toStdWString());
    const auto filesLeft = [&rootPath]() {
        std::size_t count = 0u;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(rootPath, ec), end; it != end && !ec; it.increment(ec))
        {
            count += it->is_regular_file(ec) ? 1u : 0u;
        }
        return count;
    };

    // As the host lends a window's frames: through the pool of the window's session, which the window's close empties.
    const auto pool = std::make_shared<an::ui::extensions::PreviewFrameBufferPool>(
        an::ui::extensions::SharedBufferManager(rootPath), "window-session");
    const auto gate = std::make_shared<PreviewFrameGate>();
    const an::consumer::EngineSession* session = &an::tests::host_native::suiteSession();
    Window window("Native Window", "session-frames-close", nullptr);
    window.setPreviewBody([pool, gate, session](const an::ui::extensions::ExtensionPreviewFrame& frame)
                              -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::ui::extensions::PreviewFrameBuffers> lent = an::ui::extensions::lendPreviewFrame(*pool, frame);
        if (!lent.hasValue())
        {
            return lent.status();
        }
        if (!gate->pass(frame.stop))
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the frame was stopped holding its buffers");
        }
        std::memset(lent->output.bytes(), 0, static_cast<std::size_t>(lent->output.mappedSize()));
        return an::ui::extensions::takePreviewFrameResult(pool->manager(), *session, *lent);
    });
    window.setCloseCallback([pool]() { pool->close(); });
    const FramesLandFirst landed(window);
    window.setParamDefs(QJsonArray{strengthParam()});
    auto source_view = makeSourceView("frames_close_source");
    showGrey(*source_view, 320, 240, 0.25);
    window.setSourceView(source_view.get());
    window.resize(900, 640);
    window.show();
    ASSERT_TRUE(waitUntil([&]() { return window.isProcessIdle() && pool->idleBuffers() >= 2u; }))
        << "the first frame never gave its buffers back";
    EXPECT_GT(filesLeft(), 0u) << "the pool keeps a frame's buffers for the next";

    // A frame holding its buffers when the window closes.
    gate->close();
    auto* slider = window.findChild<QSlider*>(QStringLiteral("anNativeParamDoubleSlider"));
    ASSERT_NE(slider, nullptr);
    slider->setSliderDown(true);
    slider->setValue(60);
    ASSERT_TRUE(waitUntil([&]() { return gate->waiting() == 1; })) << "no frame reached the extension";
    window.requestClose();
    slider->setSliderDown(false);
    ASSERT_TRUE(waitUntil([&]() { return window.isRunIdle(); })) << "the stopped frame never landed";
    EXPECT_EQ(filesLeft(), 0u) << "the close removed what the pool kept, and the frame's buffers went as it ended";
    EXPECT_EQ(pool->idleBuffers(), 0u);
}
