// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The fixture that drives the Observatory window as a person drives it: the application started in its order - the
/// session and the core, the stores, the core initialized and started, the shell made and built - its window shown
/// off the screen, and ended in its order after the last case of a suite; each case starts with no view and no dialog
/// open.

#pragma once

#include "ObservatoryShell.h"
#include "ObservatoryWindow.h"

#include "fixtures/AppTestSupport.h"
#include "fixtures/WindowDriving.h"

#include "app/FileCompanions.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/SequenceContainers.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "services/OpenProcessRegistry.h"
#include "session/ImageDocument.h"
#include "views/ViewService.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/Status.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace an::tests::observatory
{

/// A process of the suite's own that states a window of its own. No process of the application is one - each states a
/// page - and the cases of a task that floats in a window, with no page to attach to, need one. Made and shown as any
/// process is, it is a task of the window from its first show.
class WindowOfItsOwn final : public TopLevelProcessBaseControl
{
public:
    WindowOfItsOwn() : TopLevelProcessBaseControl(kName)
    {
        auto* const layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral("A process in a window of its own"), this));
    }

    /// The process's name, which its task says.
    static constexpr char kName[] = "Window Of Its Own";

    [[nodiscard]] an::ui::ProcessSurface processSurface() const override
    {
        return an::ui::ProcessSurface::Window;
    }
};

/// An Observatory shell started as the application starts it, and what it was started over.
struct RunningObservatory
{
    std::unique_ptr<an::ui::ApplicationSession> session;
    std::unique_ptr<an::ui::shell::ApplicationCore> core;
    std::unique_ptr<an::ui::observatory::ObservatoryShell> shell;
};

/// Starts the Observatory shell in the application's order and shows its window, under the suites' platform, at the
/// size of the floor display's, active. Qt's own file dialog is used, which a case can answer. `beforeBuild` is given
/// the started core before the shell is made, to store what a start reads.
[[nodiscard]] inline std::unique_ptr<RunningObservatory>
startObservatory(const std::function<void(an::ui::shell::ApplicationCore&)>& beforeBuild = {})
{
    auto running = std::make_unique<RunningObservatory>();
    running->session = std::make_unique<an::ui::ApplicationSession>();
    running->core = std::make_unique<an::ui::shell::ApplicationCore>();
    running->core->setApplicationSession(running->session.get());
    running->core->openStores();
    running->core->initialize();
    running->core->start();
    if (beforeBuild)
    {
        beforeBuild(*running->core);
    }
    running->shell = std::make_unique<an::ui::observatory::ObservatoryShell>(*running->core);
    running->shell->build();
    an::ui::IGlobalSettingsProvider* const settings = an::ui::appContext()->getGlobalSettings();
    (void)settings->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER), QStringLiteral("0"));
    an::ui::observatory::ObservatoryWindow* const window = running->shell->window();
    window->resize(1536, 864);
    window->show();
    window->activateWindow();
    (void)QTest::qWaitForWindowActive(window);
    QCoreApplication::processEvents();
    return running;
}

/// Ends a started shell in the application's order: the shell goes with its window, its views and its context while
/// the session is open, then the session shuts down, then the core goes.
inline an::Status stopObservatory(std::unique_ptr<RunningObservatory> running)
{
    if (running == nullptr)
    {
        return an::Status::success();
    }
    const an::Status shut = an::ui::shutDownApplication(*running->session, [&running]() { running->shell.reset(); });
    running->core.reset();
    running->session.reset();
    return shut;
}

/// The Observatory window made once for a suite of cases and ended after the last of them; each case starts with no
/// view and no dialog open, on Home, with its window active.
class ObservatoryWindowCase : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        s_running = startObservatory();
        s_platformActivatesWindows = window().isActiveWindow();
    }

    /// A menu a case opened was the active window when it closed, under a platform that gives the activation to a
    /// popup and does not hand it back: the window is then inactive, the application has no focus widget and a focus
    /// moved inside the window is never told. Each case starts with the window active, where the platform activates
    /// windows at all.
    void SetUp() override
    {
        if (s_platformActivatesWindows)
        {
            (void)an::tests::app::activateWindow(window());
        }
    }

    static void TearDownTestSuite()
    {
        const an::Status shut = stopObservatory(std::move(s_running));
        EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the window went: " << shut.message();
    }

    void TearDown() override
    {
        ++an::tests::app::g_caseGeneration;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget != nullptr && widget != &window() && widget->isVisible() &&
                (an::tests::app::isA<QDialog>(widget) || an::tests::app::isA<TopLevelProcessBaseControl>(widget)))
            {
                widget->close();
            }
        }
        // A process shown as a page is no top-level window: every open process is closed, its run ended first.
        const std::vector<TopLevelProcessBaseControl*> open = core().openProcesses().snapshot();
        for (const QPointer<TopLevelProcessBaseControl>& process :
             std::vector<QPointer<TopLevelProcessBaseControl>>(open.begin(), open.end()))
        {
            if (!process.isNull())
            {
                (void)process->cancelRun();
                if (process->runState().running())
                {
                    process->setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
                }
                (void)process->close();
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        for (ImageViewControl* view : views())
        {
            core().viewService().removeViewControl(view, true);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        window().showHome();
    }

    [[nodiscard]] static an::ui::observatory::ObservatoryWindow& window()
    {
        return *s_running->shell->window();
    }

    [[nodiscard]] static an::ui::observatory::ObservatoryShell& shell()
    {
        return *s_running->shell;
    }

    [[nodiscard]] static an::ui::shell::ApplicationCore& core()
    {
        return *s_running->core;
    }

    [[nodiscard]] static const an::consumer::EngineSession& session()
    {
        return s_running->session->engineSession();
    }

    [[nodiscard]] static std::vector<ImageViewControl*> views()
    {
        std::vector<ImageViewControl*> found;
        for (ImageViewControl* view : core().viewService().getListOfViewControls())
        {
            if (view != nullptr)
            {
                found.push_back(view);
            }
        }
        return found;
    }

    /// Opens an image as a person's file does: a small grey image under `name`, made by the views of the person.
    static ImageViewControl* openView(const QString& name, float value = 0.25f)
    {
        std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), name, value);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        ImageViewControl* const view = core().viewService().userViews().createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            core().viewService().setActiveImageViewControl(view);
            QCoreApplication::processEvents();
        }
        return view;
    }

    /// Opens a sequence of grey frames recorded in a SER file as a person's file does, the active view.
    static ImageViewControl* openSequence(const QString& name, const std::uint32_t frames)
    {
        // Made once the suite's temporary directory is in force.
        static QTemporaryDir recordings(QDir::temp().filePath(QStringLiteral("observatory-runs-XXXXXX")));
        const QString path = recordings.filePath(name + QStringLiteral(".ser"));
        {
            an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
                path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frames});
            EXPECT_TRUE(writer.hasValue()) << writer.status().message();
            if (!writer.hasValue())
            {
                return nullptr;
            }
            an::Result<an::consumer::OperationScope> scope =
                session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
            EXPECT_TRUE(scope.hasValue()) << scope.status().message();
            if (!scope.hasValue())
            {
                return nullptr;
            }
            for (std::uint32_t frame = 0u; frame < frames; ++frame)
            {
                std::optional<an::consumer::ImageAsset> asset =
                    an::tests::app::filledAsset(session(), 0.1f + 0.1f * static_cast<float>(frame), 32u, 24u);
                EXPECT_TRUE(asset.has_value());
                if (!asset.has_value())
                {
                    return nullptr;
                }
                an::Result<an::consumer::AssetReadLease> lease = asset->read();
                EXPECT_TRUE(lease.hasValue());
                if (!lease.hasValue())
                {
                    return nullptr;
                }
                const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
                EXPECT_TRUE(appended.ok()) << appended.message();
            }
            an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
            EXPECT_TRUE(published.hasValue()) << published.status().message();
        }
        an::Result<an::ui::ImageDocument> document =
            an::ui::openSequenceDocument(session(), path, an::SequenceFormat::Ser, {});
        EXPECT_TRUE(document.hasValue()) << document.status().message();
        if (!document.hasValue())
        {
            return nullptr;
        }
        ImageViewControl* const view = core().viewService().userViews().createNewImageViewControlFromSequence(
            nullptr, std::move(*document), nullptr, false, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            core().viewService().setActiveImageViewControl(view);
            QCoreApplication::processEvents();
        }
        return view;
    }

    static inline std::unique_ptr<RunningObservatory> s_running;
    static inline bool s_platformActivatesWindows = false;
};

} // namespace an::tests::observatory
