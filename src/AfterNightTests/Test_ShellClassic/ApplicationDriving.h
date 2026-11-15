// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the cases that drive the application's own main window share: running the event loop until something holds,
/// answering the dialogs a menu action opens from the event loop the dialog runs, the windows a case looks for, and the
/// fixture that makes the window as the application makes it.
#pragma once

#include "AfterNightApp.h"

#include "AppTestSupport.h"
#include "fixtures/WindowDriving.h"
#include "core/ApplicationSession.h"
#include "core/ApplicationCore.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/NewSequenceControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"

#include "fixtures/common/RawPolicyCases.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace an::tests::app
{

/// The application's own main window, made once for a suite of cases as the application makes it and torn down
/// after the last of them in the application's order; each case starts with no view and no dialog open.
class RealWindowCase : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        s_files = std::make_unique<QTemporaryDir>();
        s_session = std::make_unique<an::ui::ApplicationSession>();
        s_core = std::make_unique<an::ui::shell::ApplicationCore>();
        // The application is readied before its window, as it starts: the stores, the context and the session,
        // the installed extensions.
        s_core->setApplicationSession(s_session.get());
        s_core->openStores();
        s_core->initialize();
        s_core->start();
        s_window = AfterNightApp::createInstance(*s_core);
        s_window->initialize(*s_session);
        // The application's own file dialog, which a case can answer, rather than the system's.
        an::ui::IGlobalSettingsProvider* const settings = an::ui::appContext()->getGlobalSettings();
        ASSERT_NE(settings, nullptr);
        ASSERT_TRUE(settings->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER),
                                     QStringLiteral("0")));
    }

    static void TearDownTestSuite()
    {
        const an::Status shut = an::ui::shutDownApplication(*s_session, []() { AfterNightApp::destroyInstance(); });
        EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the window went: " << shut.message();
        s_core.reset();
        s_session.reset();
        s_window = nullptr;
        s_files.reset();
    }

    void TearDown() override
    {
        // The case's polls stop; every dialog and window a case left open goes, then every view, so the next case
        // starts with none.
        ++g_caseGeneration;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            // A process window too: one left attached to a view that goes would say so in a message box.
            if (widget != nullptr && widget != s_window && widget->isVisible() &&
                (isA<QDialog>(widget) || isA<TopLevelProcessBaseControl>(widget)))
            {
                widget->close();
            }
        }
        // A view's own windows go before the view, as its close takes them down first: a window destroyed with its view
        // moves the focus while the view is half destroyed, and the docking framework then signals the view.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        for (ImageViewControl* view : views())
        {
            for (QDialog* window : view->findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly))
            {
                delete window;
            }
            s_window->removeImageViewControl(view, true);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    [[nodiscard]] static an::ui::IAppContext& context()
    {
        an::ui::IAppContext* const installed = an::ui::appContext();
        EXPECT_NE(installed, nullptr);
        return *installed;
    }

    [[nodiscard]] static const an::consumer::EngineSession& session()
    {
        return s_session->engineSession();
    }

    // The action the window's menus hold under an object name.
    [[nodiscard]] static QAction* menuAction(const char* name)
    {
        QAction* const action = s_window->findChild<QAction*>(QLatin1String(name));
        EXPECT_NE(action, nullptr) << "the window holds no action " << name;
        return action;
    }

    // Triggers a menu action as the menu does.
    static void invoke(const char* name)
    {
        QAction* const action = menuAction(name);
        ASSERT_NE(action, nullptr);
        action->trigger();
    }

    [[nodiscard]] static std::vector<ImageViewControl*> views()
    {
        std::vector<ImageViewControl*> found;
        for (ImageViewControl* view : context().getViewControlManager()->getListOfViewControls())
        {
            if (view != nullptr)
            {
                found.push_back(view);
            }
        }
        return found;
    }

    [[nodiscard]] static ImageViewControl* activeView()
    {
        return context().getViewRegistry()->getActiveImageViewControl();
    }

    // A view the application's factory makes over a grey 8x4 Float32 image of one value; it becomes the active view.
    static ImageViewControl* makeView(const QString& name, float value = 0.25f)
    {
        std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), name, value);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        ImageViewControl* const view = context().getViewFactory()->createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, name.toStdString());
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            s_window->setActiveImageViewControl(view);
        }
        return view;
    }

    [[nodiscard]] static QString filePath(const QString& name)
    {
        return QDir(s_files->path()).filePath(name);
    }

    // Writes a grey Float32 image of one value to a file of the container its extension names, as the export settings
    // write that container.
    static void writeFile(const QString& path, an::CodecFormat container, float value)
    {
        std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), value);
        ASSERT_TRUE(asset.has_value());
        const std::optional<an::ImageDescriptor> descriptor = asset->descriptor();
        ASSERT_TRUE(descriptor.has_value());
        const an::Result<an::OutputIntent> intent =
            context().getOutputIntentProvider()->buildOutputIntent(*descriptor, container);
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();
        const an::Status written = an::ui::writeViewImage(session(), *asset, path, *intent, {});
        ASSERT_TRUE(written.ok()) << written.message();
    }

    // The sample at the origin of a file read as the views read it, as a fraction of full scale.
    [[nodiscard]] static double fileSample(const QString& path)
    {
        const an::Result<an::ui::ViewReadResult> read =
            an::ui::readViewImage(session(), an::ui::ViewReadRequest{.path = path}, {}, nullptr);
        EXPECT_TRUE(read.hasValue()) << read.status().message();
        if (!read.hasValue())
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        an::Result<an::consumer::AssetReadLease> lease = read->asset.read();
        if (!lease.hasValue())
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (const auto* image = std::get_if<an::Image<float>>(&lease->image()); image && image->view().has_value())
        {
            const auto row = image->view()->row(0u, 0u);
            return row.has_value() && !row->empty() ? static_cast<double>((*row)[0]) : std::nan("");
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Opens a file through the file menu as a person does and answers the view the open made for it, or null.
    static ImageViewControl* openThroughTheFileMenu(const QString& path)
    {
        const std::vector<ImageViewControl*> before = views();
        Answered dialog;
        chooseInTheNextFileDialog({path}, &dialog);
        invoke("actionOpen");
        EXPECT_TRUE(dialog.seen) << "the file menu showed no file dialog for " << path.toStdString();
        EXPECT_FALSE(dialog.refused) << dialog.refusal.toStdString();
        ImageViewControl* opened = nullptr;
        const bool shown = processEventsUntil(
            [&]() {
                for (ImageViewControl* view : views())
                {
                    if (std::find(before.begin(), before.end(), view) == before.end() && view->hasImage() &&
                        QDir::cleanPath(view->document().names().sourcePath) == QDir::cleanPath(path))
                    {
                        opened = view;
                    }
                }
                return opened != nullptr;
            },
            20000);
        EXPECT_TRUE(shown) << path.toStdString() << " was not shown in a view of its own";
        return opened;
    }

    // A camera RAW file the neutral fixture layer writes from the DNG specification's tags alone, under a name.
    static QString writeDng(const QString& name = QStringLiteral("camera.dng"))
    {
        const an::tests::fixtures::RawPolicyInput& input = an::tests::fixtures::rawPolicyInputs().front();
        std::string error;
        const std::optional<an::tests::fixtures::FixtureSamples> samples =
            an::tests::fixtures::materializeFixture(input.image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        const QString path = filePath(name);
        if (samples.has_value())
        {
            EXPECT_TRUE(an::tests::fixtures::writeRawPolicyDng(input, *samples,
                                                               std::filesystem::path(path.toStdWString()), error))
                << error;
        }
        return path;
    }

    // A second view, of the active view's size and one channel, made the active view's mask; the view becomes the
    // active one again.
    static void giveAMask(ImageViewControl* view)
    {
        ImageViewControl* const mask = makeView(QStringLiteral("mask"), 0.5f);
        ASSERT_NE(mask, nullptr);
        view->setCurrentMask(mask);
        s_window->setActiveImageViewControl(view);
        ASSERT_EQ(view->getCurrentMask(), mask);
    }

    // Triggers a checkable mask action from a stated checked state, as a click on it does.
    static void clickCheckable(const char* name, bool from)
    {
        QAction* const action = menuAction(name);
        ASSERT_NE(action, nullptr);
        ASSERT_TRUE(action->isCheckable());
        action->setChecked(from);
        action->trigger();
    }

    // Opens a view tool from the image menu over the active view and answers whether a window it accepts opened.
    [[nodiscard]] static bool opensToolWindow(const char* name, const std::function<bool(QWidget*)>& accepts)
    {
        ImageViewControl* const view = makeView(QStringLiteral("tool"));
        if (view == nullptr)
        {
            return false;
        }
        invoke(name);
        return processEventsUntil([view, &accepts]() {
            for (QWidget* window : visibleChildWindows(*view))
            {
                if (accepts(window))
                {
                    return true;
                }
            }
            return false;
        });
    }

    static inline std::unique_ptr<QTemporaryDir> s_files;
    static inline std::unique_ptr<an::ui::ApplicationSession> s_session;
    static inline std::unique_ptr<an::ui::shell::ApplicationCore> s_core;
    static inline AfterNightApp* s_window = nullptr;
};

} // namespace an::tests::app
