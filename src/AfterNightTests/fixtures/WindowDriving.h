// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the cases that drive a shell's own main window share, whichever shell it is: running the event loop until
/// something holds, answering the dialogs a menu action opens from the event loop the dialog runs, the windows a case
/// looks for, and reading what a view shows without waiting on the GUI thread.

#pragma once

#include "controls/ImageViewControl.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Image/Image.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Makes a window the active one, and answers whether it is.
//
// A request for the activation is queued among the window system's events. The `minimal` platform - the suites' on
// Windows - delivers those only as a window is shown or hidden, never from the event loop, and hands the activation
// to no window when the one that had it closes. So once a dialog, a menu or a floating window has come and gone the
// application has no active window, and `QWidget::activateWindow()` followed by a wait asks in vain until the wait's
// bound runs out - five seconds a case, for most cases of a suite. Under that platform the request is therefore
// followed by a window of the helper's own, shown and gone, which delivers what is queued. That window is made
// before the request: the platform queues an activation for every window it makes, and the one asked for must be
// the last in the queue. Any other platform delivers the request from its event loop, and is waited for.
inline bool activateWindow(QWidget& window, int bound = 5000)
{
    if (window.isActiveWindow())
    {
        return true;
    }
    if (QGuiApplication::platformName() != QLatin1String("minimal"))
    {
        window.activateWindow();
        return processEventsUntil([&window]() { return window.isActiveWindow(); }, bound);
    }
    {
        QWindow delivers;
        delivers.create();
        window.activateWindow();
        delivers.setVisible(true);
        delivers.setVisible(false);
    }
    return processEventsUntil([&window]() { return window.isActiveWindow(); }, (std::min)(bound, 500));
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

} // namespace an::tests::app
