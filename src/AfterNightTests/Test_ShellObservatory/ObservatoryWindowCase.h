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

#include "Test_ShellClassic/AppTestSupport.h"
#include "fixtures/WindowDriving.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "session/ImageDocument.h"
#include "views/ViewService.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/Status.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace an::tests::observatory
{

/// An Observatory shell started as the application starts it, and what it was started over.
struct RunningObservatory
{
    std::unique_ptr<an::ui::ApplicationSession> session;
    std::unique_ptr<an::ui::shell::ApplicationCore> core;
    std::unique_ptr<an::ui::observatory::ObservatoryShell> shell;
};

/// Starts the Observatory shell in the application's order and shows its window off the screen, at a size of the
/// floor display's. Qt's own file dialog is used, which a case can answer.
[[nodiscard]] inline std::unique_ptr<RunningObservatory> startObservatory()
{
    auto running = std::make_unique<RunningObservatory>();
    running->session = std::make_unique<an::ui::ApplicationSession>();
    running->core = std::make_unique<an::ui::shell::ApplicationCore>();
    running->core->setApplicationSession(running->session.get());
    running->core->openStores();
    running->core->initialize();
    running->core->start();
    running->shell = std::make_unique<an::ui::observatory::ObservatoryShell>(*running->core);
    running->shell->build();
    an::ui::IGlobalSettingsProvider* const settings = an::ui::appContext()->getGlobalSettings();
    (void)settings->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER), QStringLiteral("0"));
    an::ui::observatory::ObservatoryWindow* const window = running->shell->window();
    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    window->resize(1536, 864);
    window->show();
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
/// view and no dialog open, on Home.
class ObservatoryWindowCase : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        s_running = startObservatory();
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

    static inline std::unique_ptr<RunningObservatory> s_running;
};

} // namespace an::tests::observatory
