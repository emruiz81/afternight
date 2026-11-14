// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the shell core's cases about views and dialogs share: a shell that records what it is asked, an engine session
/// for the documents a view shows, and the dialogs a case finds on screen and answers.

#include "core/IShell.h"

#include "controls/ImageViewControl.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace an::tests::shellcore
{

/// A shell that builds nothing and records what it is asked, in order, as one line per call: `place <view> beside
/// <neighbour>`, `added <view> user`, `showViews`, `home`, and so on. Its window is a plain widget it owns.
class RecordingShell final : public an::ui::shell::IShell,
                             public an::ui::shell::IViewHost,
                             public an::ui::shell::IStatusDisplay
{
public:
    RecordingShell() : m_window(std::make_unique<QWidget>())
    {
        m_window->setObjectName(QStringLiteral("recordingShellWindow"));
    }

    std::vector<std::string> calls;

    /// The thread each call was made on, in the order of `calls`.
    std::vector<std::thread::id> threads;

    /// Called as the shell is told a view went, after the call is recorded.
    std::function<void(ImageViewControl&)> onViewRemoved;

    /// The calls that name `view`, or that name none, in order.
    [[nodiscard]] std::vector<std::string> callsAbout(const std::string& name) const
    {
        std::vector<std::string> found;
        for (const std::string& call : calls)
        {
            if (call.find(name) != std::string::npos)
            {
                found.push_back(call);
            }
        }
        return found;
    }

    [[nodiscard]] bool asked(const std::string& call) const
    {
        return std::find(calls.cbegin(), calls.cend(), call) != calls.cend();
    }

    // ---- IShell
    void build() override
    {
        record("build");
    }
    [[nodiscard]] QWidget* mainWindow() override
    {
        return m_window.get();
    }
    [[nodiscard]] an::ui::shell::IViewHost& viewHost() override
    {
        return *this;
    }
    [[nodiscard]] an::ui::shell::IStatusDisplay& statusDisplay() override
    {
        return *this;
    }
    [[nodiscard]] an::ui::IProcessPresenter* processPresenter() override
    {
        return nullptr;
    }
    void setSessionConsoleVisible(const bool visible) override
    {
        m_consoleVisible = visible;
        record(visible ? "console shown" : "console hidden");
    }
    [[nodiscard]] bool isSessionConsoleVisible() const override
    {
        return m_consoleVisible;
    }
    void refreshTheme() override
    {
        record("refreshTheme");
    }
    void showWithSavedGeometry() override
    {
        record("show");
    }
    void saveGeometry() override
    {
        record("saveGeometry");
    }

    // ---- IViewHost
    void place(ImageViewControl& view, const an::ui::shell::ViewPlacement& placement) override
    {
        record("place " + view.getUniqueName() + " beside " +
               (placement.neighbour != nullptr ? placement.neighbour->getUniqueName() : std::string("none")));
    }
    void activate(ImageViewControl& view) override
    {
        record("activate " + view.getUniqueName());
    }
    void viewAdded(ImageViewControl& view, const an::ui::shell::ViewOrigin origin) override
    {
        record("added " + view.getUniqueName() +
               (origin == an::ui::shell::ViewOrigin::User ? std::string(" user") : std::string(" process")));
    }
    void viewRemoved(ImageViewControl& view) override
    {
        record("removed " + view.getUniqueName());
        if (onViewRemoved)
        {
            onViewRemoved(view);
        }
    }
    void viewRenamed(ImageViewControl& view, const std::string& previousName) override
    {
        record("renamed " + previousName + " to " + view.getUniqueName());
    }
    void maximize(ImageViewControl& view) override
    {
        record("maximize " + view.getUniqueName());
    }
    void restore(ImageViewControl& view) override
    {
        record("restore " + view.getUniqueName());
    }
    [[nodiscard]] bool isMaximizedViewActive() const override
    {
        return false;
    }
    [[nodiscard]] an::ui::shell::DockTarget dockTarget(const ImageViewControl* /*view*/) override
    {
        return {};
    }
    void showHome() override
    {
        record("home");
    }
    void showViews() override
    {
        record("showViews");
    }

    // ---- IStatusDisplay
    void showImageInfo(ImageViewControl* view) override
    {
        record("info " + (view != nullptr ? view->getUniqueName() : std::string("none")));
    }
    void showZoom(ImageViewControl* view) override
    {
        record("zoom " + (view != nullptr ? view->getUniqueName() : std::string("none")));
    }
    void showCelestialCoordinates(const double raDegrees, const double decDegrees) override
    {
        record("coordinates " + std::to_string(raDegrees) + " " + std::to_string(decDegrees));
    }
    void clearCelestialCoordinates() override
    {
        record("coordinates cleared");
    }
    void clearImageInfo() override
    {
        record("info cleared");
    }

private:
    void record(std::string call)
    {
        calls.push_back(std::move(call));
        threads.push_back(std::this_thread::get_id());
    }

    std::unique_ptr<QWidget> m_window;
    bool m_consoleVisible = false;
};

/// The provider an application lends its session through; it owns the session.
class SessionProvider final : public an::ui::IEngineSessionProvider
{
public:
    explicit SessionProvider(an::consumer::EngineSession session) : m_session(std::move(session)) {}

    [[nodiscard]] const an::consumer::EngineSession& engineSession() const noexcept override
    {
        return m_session;
    }

private:
    an::consumer::EngineSession m_session;
};

/// A provider over a session of two workers; fails the case when none can be opened.
[[nodiscard]] inline std::unique_ptr<SessionProvider> openSession()
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 2u;
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return nullptr;
    }
    return std::make_unique<SessionProvider>(std::move(*session));
}

/// A new document of the session around a single-channel Float32 image of 8 x 4 samples of one value, shown under
/// `name`; nothing when it cannot be made.
[[nodiscard]] inline std::optional<an::ui::ImageDocument> documentNamed(const an::consumer::EngineSession& session,
                                                                        const QString& name)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8u, 4u, 1u, hold->engine().limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, 0.25f);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document =
        an::ui::ImageDocument::create(std::move(*asset), an::ui::DocumentNames{.displayName = name, .sourcePath = {}});
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// Runs the GUI thread's events until `done` holds, for at most five seconds; whether it held.
[[nodiscard]] inline bool pumpUntil(const std::function<bool()>& done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done())
    {
        if (timer.elapsed() > 5000)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

/// Lets every worker a view started finish and runs what they handed the GUI thread.
inline void settle()
{
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
}

/// The top-level window of type `WindowT` on screen now, the most recent one first; null when there is none.
template <typename WindowT> [[nodiscard]] WindowT* visibleWindowOf()
{
    const QWidgetList windows = QApplication::topLevelWidgets();
    for (auto it = windows.crbegin(); it != windows.crend(); ++it)
    {
        if (auto* window = qobject_cast<WindowT*>(*it); window != nullptr && window->isVisible())
        {
            return window;
        }
    }
    return nullptr;
}

/// Answers the first window of type `WindowT` that comes on screen - a modal dialog the case is about to open, whose
/// `exec` holds the case until it closes - by handing it to `answer`, which must close it.
template <typename WindowT> void answerWhenShown(std::function<void(WindowT&)> answer)
{
    auto* timer = new QTimer(qApp);
    timer->setInterval(10);
    auto remaining = std::make_shared<int>(500);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, remaining, answer = std::move(answer)]() {
        if (WindowT* window = visibleWindowOf<WindowT>())
        {
            timer->stop();
            timer->deleteLater();
            answer(*window);
            return;
        }
        if (--*remaining <= 0)
        {
            ADD_FAILURE() << "no window of the expected kind came on screen";
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

} // namespace an::tests::shellcore
