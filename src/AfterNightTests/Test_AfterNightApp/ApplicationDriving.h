// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the cases that drive the application's own main window share: running the event loop until something holds,
/// answering the dialogs a menu action opens from the event loop the dialog runs, the windows a case looks for, and the
/// fixture that makes the window as the application makes it.
#pragma once

#include "app/AfterNightApp.h"

#include "AppTestSupport.h"
#include "app/ApplicationSession.h"
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

// Runs the event loop until `done` holds or `bound` milliseconds have passed; answers whether it held.
inline bool processEventsUntil(const std::function<bool()>& done, int bound = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < bound)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return done();
}

// Whether a widget is of a class, read from its dynamic type: not every window declares a meta-object of its own.
template <typename WindowT> [[nodiscard]] bool isA(const QObject* object)
{
    return object != nullptr && dynamic_cast<const WindowT*>(object) != nullptr;
}

// Whether a widget's dynamic type is named `name` - for a window the library does not export.
[[nodiscard]] inline bool isNamed(const QObject* object, const char* name)
{
    return object != nullptr && std::string(typeid(*object).name()).find(name) != std::string::npos;
}

// The visible windows of a view's own: the tool windows it opens are its children.
[[nodiscard]] inline std::vector<QWidget*> visibleChildWindows(const QWidget& view)
{
    std::vector<QWidget*> windows;
    for (QObject* child : view.children())
    {
        auto* widget = qobject_cast<QWidget*>(child);
        if (widget != nullptr && widget->isWindow() && widget->isVisible())
        {
            windows.push_back(widget);
        }
    }
    return windows;
}

// The first visible widget anywhere in the application that `match` accepts, or null.
[[nodiscard]] inline QWidget* visibleWidget(const std::function<bool(QWidget*)>& match)
{
    for (QWidget* widget : QApplication::allWidgets())
    {
        if (widget != nullptr && widget->isVisible() && match(widget))
        {
            return widget;
        }
    }
    return nullptr;
}

// What a modal step a case answered showed.
struct Answered
{
    bool seen = false;
    QString title;
    QString text;
    /// For a file dialog: whether it refused what was chosen - saying so in a message box, or staying open - and
    /// what it said.
    bool refused = false;
    QString refusal;
};

// The case the polls below belong to: each poll stops once the case that started it has ended, so none answers a later
// case's window through what an ended case owned.
inline int g_caseGeneration = 0;

// Answers, from the event loop a modal dialog runs, the first visible window `act` takes; tried every ten milliseconds
// for ten seconds, or, `repeatedly`, every window it takes until the case ends. Calls made from the window's own loop
// are how a modal dialog is answered in a case: the action that opened it returns only once it has been.
struct WindowPoll
{
    std::function<bool(QWidget*)> act;
    bool repeatedly = false;
    int generation = 0;
    int attemptsLeft = 1000;
    /// Set by whoever started the poll once what it waited for can no longer come. The poll then answers nothing:
    /// left running, it would answer the next window of its kind, which is another step's to answer.
    bool ended = false;
};

inline void runWindowPoll(const std::shared_ptr<WindowPoll>& poll);

inline void scheduleWindowPoll(const std::shared_ptr<WindowPoll>& poll, int delay)
{
    QTimer::singleShot(delay, [poll]() { runWindowPoll(poll); });
}

inline void runWindowPoll(const std::shared_ptr<WindowPoll>& poll)
{
    if (poll->ended || poll->generation != g_caseGeneration)
    {
        return;
    }
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (widget != nullptr && widget->isVisible() && poll->act(widget))
        {
            if (!poll->repeatedly)
            {
                return;
            }
            break;
        }
    }
    if (poll->repeatedly || --poll->attemptsLeft > 0)
    {
        scheduleWindowPoll(poll, 10);
    }
}

//
// Returns the poll, so a step that knows its window can no longer come ends it (`WindowPoll::ended`).
inline std::shared_ptr<WindowPoll> answerWhenShown(std::function<bool(QWidget*)> act, bool repeatedly = false)
{
    auto poll = std::make_shared<WindowPoll>();
    poll->act = std::move(act);
    poll->repeatedly = repeatedly;
    poll->generation = g_caseGeneration;
    scheduleWindowPoll(poll, 0);
    return poll;
}

// Presses Yes on a message box when it offers Yes, otherwise Ok; records what it showed.
inline bool pressYesOrOk(QWidget* widget, Answered* answered)
{
    auto* box = qobject_cast<QMessageBox*>(widget);
    if (box == nullptr)
    {
        return false;
    }
    answered->seen = true;
    answered->title = box->windowTitle();
    answered->text = box->text();
    QAbstractButton* const yes = box->button(QMessageBox::Yes);
    QAbstractButton* const ok = box->button(QMessageBox::Ok);
    if (yes != nullptr)
    {
        yes->click();
    }
    else if (ok != nullptr)
    {
        ok->click();
    }
    else
    {
        box->reject();
    }
    return true;
}

// Answers every message box shown until the case ends - the question a close asks about unsaved changes among them -
// with Yes, or Ok where it offers no Yes.
inline void answerEveryMessageBox(const std::shared_ptr<Answered>& answered)
{
    answerWhenShown([answered](QWidget* widget) { return pressYesOrOk(widget, answered.get()); }, true);
}

// Answers the next message box shown: records its title and text and presses Yes when it offers Yes, otherwise Ok.
inline void answerTheNextMessageBox(Answered* answered)
{
    answerWhenShown([answered](QWidget* widget) { return pressYesOrOk(widget, answered); });
}

// Takes the next modal dialog shown, whichever it is: `inspect` reads it, its title and dynamic type are recorded, and
// it is rejected, so a case that expected another dialog fails rather than waits for one that never comes.
inline void answerTheNextModalDialog(std::function<void(QDialog&)> inspect, Answered* answered)
{
    answerWhenShown([inspect = std::move(inspect), answered](QWidget*) {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || !dialog->isVisible())
        {
            return false;
        }
        answered->seen = true;
        answered->title = dialog->windowTitle();
        answered->text = QString::fromLatin1(typeid(*dialog).name());
        inspect(*dialog);
        dialog->reject();
        return true;
    });
}

// Chooses `files` in the next file dialog shown and accepts it.
inline void chooseInTheNextFileDialog(const QStringList& files, Answered* answered)
{
    answerWhenShown([files, answered](QWidget* widget) {
        auto* dialog = qobject_cast<QFileDialog*>(widget);
        if (dialog == nullptr)
        {
            return false;
        }
        answered->seen = true;
        answered->title = dialog->windowTitle();
        // The names are typed into the dialog's own name field, as a person types them: selectFile leaves a field that
        // has the focus - as it has once the dialog is shown - as it is.
        dialog->setDirectory(QFileInfo(files.front()).absolutePath());
        QStringList names;
        for (const QString& file : files)
        {
            names.append(files.size() == 1 ? QFileInfo(file).fileName()
                                           : QLatin1Char('"') + QFileInfo(file).fileName() + QLatin1Char('"'));
        }
        auto* const field = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (field != nullptr)
        {
            field->setText(names.join(QLatin1Char(' ')));
        }
        else
        {
            dialog->selectFile(names.join(QLatin1Char(' ')));
        }
        // A dialog that refuses the choice says so in a message box from inside its accept; the box is answered and
        // the dialog, still open, closed, so the case fails rather than waits.
        auto said = std::make_shared<Answered>();
        const std::shared_ptr<WindowPoll> refusal =
            answerWhenShown([said](QWidget* box) { return pressYesOrOk(box, said.get()); });
        // A file dialog accepts through its dialog base: its own accept is not public.
        static_cast<QDialog*>(dialog)->accept();
        // The refusal is shown from inside the accept, so it has been answered by now or there is none. The poll ends
        // here: left armed for its ten seconds, it would answer the next message box of the case, which a case that
        // opens a file and then waits for a box of its own lost to it about every other run.
        refusal->ended = true;
        if (said->seen)
        {
            answered->refused = true;
            answered->refusal = said->text;
        }
        if (dialog->isVisible())
        {
            answered->refused = true;
            dialog->reject();
        }
        return true;
    });
}

// A read lease of an asset a view shows, taken without waiting on the GUI thread: an edit holds the asset's writer
// until it publishes, which it does on this thread, so a lease that waited would wait for itself. The events are pumped
// until the writer lets go, or the bound passes and nothing is read.
[[nodiscard]] inline std::optional<an::consumer::AssetReadLease>
readOnTheGuiThread(const an::consumer::ImageAsset& asset, int bound = 10000)
{
    std::optional<an::consumer::AssetReadLease> lease;
    processEventsUntil(
        [&]() {
            if (!lease.has_value())
            {
                an::Result<an::consumer::AssetReadLease> attempt = asset.tryRead();
                if (attempt.hasValue())
                {
                    lease.emplace(std::move(*attempt));
                }
            }
            return lease.has_value();
        },
        bound);
    return lease;
}

// The sample at a position of a Float32 image a view shows, or NaN.
[[nodiscard]] inline double viewSample(const ImageViewControl& view, std::uint32_t x = 0u, std::uint32_t y = 0u)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = view.document().asset();
    if (!asset)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::optional<an::consumer::AssetReadLease> lease = readOnTheGuiThread(*asset);
    if (!lease.has_value())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto* image = std::get_if<an::Image<float>>(&lease->image());
    if (image == nullptr || !image->view().has_value())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto row = image->view()->row(0u, y);
    return row.has_value() && x < row->size() ? static_cast<double>((*row)[x])
                                              : std::numeric_limits<double>::quiet_NaN();
}

/// The application's own main window, made once for a suite of cases as the application makes it and torn down
/// after the last of them in the application's order; each case starts with no view and no dialog open.
class RealWindowCase : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        s_files = std::make_unique<QTemporaryDir>();
        s_session = std::make_unique<an::ui::ApplicationSession>();
        s_window = AfterNightApp::getInstance();
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
    static inline AfterNightApp* s_window = nullptr;
};

} // namespace an::tests::app
