// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The work a thread hands the GUI thread through the application core: each event runs once, on the GUI thread,
/// against what the core was given to run it on; none runs once the core, or the target it was posted for, is gone; and
/// one that fails is reported without stopping the others.

#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"

#include "app/AfterNightEvent.h"
#include "session/ImageDocument.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QString>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using an::ui::shell::ApplicationCore;
using an::ui::shell::IApplicationEventTarget;

/// What a target was asked, on which thread.
struct Call
{
    std::string what;
    std::thread::id thread;
};

/// A target that records what each event asks of it.
class RecordingTarget final : public IApplicationEventTarget
{
public:
    std::vector<Call> calls;

    [[nodiscard]] std::vector<std::string> asked() const
    {
        std::vector<std::string> names;
        for (const Call& call : calls)
        {
            names.push_back(call.what);
        }
        return names;
    }

    ImageViewControl* createNewImageViewControlFromImage(ImageViewControl* /*customViewControl*/,
                                                         an::ui::ImageDocument&& /*document*/, QWidget* /*parent*/,
                                                         const std::string& historyDesc, ViewControlMode /*mode*/,
                                                         ImageViewControl* /*neighbour*/, bool /*keepFocusedNeighbour*/,
                                                         bool /*pendingChanges*/) override
    {
        record("image " + historyDesc);
        return nullptr;
    }

    ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl* /*customViewControl*/,
                                                            an::ui::ImageDocument&& /*document*/, QWidget* /*parent*/,
                                                            bool autoDebayer, const std::string& historyDesc,
                                                            ViewControlMode /*mode*/, ImageViewControl* /*neighbour*/,
                                                            bool /*keepFocusedNeighbour*/,
                                                            bool /*pendingChanges*/) override
    {
        record("sequence " + historyDesc + (autoDebayer ? " debayered" : ""));
        return nullptr;
    }

    void addImageViewControl(ImageViewControl* /*view*/) override
    {
        record("add");
    }

    void removeImageViewControl(ImageViewControl* /*view*/, bool deleteObject) override
    {
        record(deleteObject ? "remove and delete" : "remove");
    }

    void updateImageViewControlList() override
    {
        record("list");
    }

    void updateActiveImageActions() override
    {
        record("actions");
    }

    void addRecentFile(const std::string& fileName,
                       const std::shared_ptr<const an::consumer::ImageAsset>& /*image*/) override
    {
        record("add recent " + fileName);
    }

    void removeRecentFile(const std::string& fileName) override
    {
        record("remove recent " + fileName);
    }

    void notifyErrorMessage(QWidget* /*parent*/, const QString& title, const QString& text) override
    {
        record("error " + title.toStdString() + ": " + text.toStdString());
    }

    void notifyWarnMessage(QWidget* /*parent*/, const QString& title, const QString& text) override
    {
        record("warning " + title.toStdString() + ": " + text.toStdString());
    }

    void notifyInfoMessage(QWidget* /*parent*/, const QString& title, const QString& text) override
    {
        record("info " + title.toStdString() + ": " + text.toStdString());
    }

    void messageBoxYesNo(QWidget* /*parent*/, const QString& title, const QString& /*text*/, bool warn,
                         int* ret) override
    {
        record("ask " + title.toStdString() + (warn ? " warning" : ""));
        if (ret != nullptr)
        {
            *ret = 42;
        }
    }

    void setActiveImageViewControl(ImageViewControl* /*view*/) override
    {
        record("activate");
    }

    void reportEventFailure(const QString& title, const QString& text) override
    {
        record("failure " + title.toStdString() + ": " + text.toStdString());
    }

private:
    void record(std::string what)
    {
        calls.push_back({std::move(what), std::this_thread::get_id()});
    }
};

/// Delivers what was posted to the GUI thread.
void deliverPostedEvents()
{
    QCoreApplication::sendPostedEvents();
}

TEST(ApplicationEvents, EachEventPostedFromAWorkerRunsOnceOnTheGuiThread)
{
    ApplicationCore core;
    RecordingTarget target;
    core.setEventTarget(&target);
    std::atomic_bool imageMade{false};
    std::atomic_bool sequenceMade{false};
    std::atomic_bool listed{false};
    std::atomic_bool added{false};
    std::atomic_bool removed{false};
    std::atomic_bool asked{false};
    std::atomic_bool activated{false};
    int answer = 0;
    std::thread::id customThread;
    int customRuns = 0;

    std::thread worker([&]() {
        core.createNewImageViewControlFromImageAsync(nullptr, an::ui::ImageDocument(), nullptr, "of an image",
                                                     ViewControlMode::standard, nullptr, false, true, &imageMade);
        core.createNewImageViewControlFromSequenceAsync(nullptr, an::ui::ImageDocument(), nullptr, true,
                                                        "of a sequence", ViewControlMode::standard, nullptr, false,
                                                        true, &sequenceMade);
        core.addImageViewControlAsync(nullptr);
        core.removeImageViewControlAsync(nullptr, true);
        core.updateImageViewControlListAsync(&listed);
        core.updateActiveImageActionsAsync();
        core.addRecentFileAsync("a.fits", nullptr, &added);
        core.removeRecentFileAsync("b.fits", &removed);
        core.notifyErrorMessageAsync(nullptr, QStringLiteral("Open"), QStringLiteral("unreadable"));
        core.notifyWarnMessageAsync(nullptr, QStringLiteral("Save"), QStringLiteral("clipped"));
        core.notifyInfoMessageAsync(nullptr, QStringLiteral("Settings"), QStringLiteral("set aside"));
        core.messageBoxYesNoAsync(nullptr, QStringLiteral("Close"), QStringLiteral("Discard?"), true, &answer, &asked);
        core.setActiveImageViewControlAsync(nullptr, &activated);
        core.runCustomCodeAsync([&customThread, &customRuns]() {
            customThread = std::this_thread::get_id();
            ++customRuns;
        });
    });
    worker.join();
    EXPECT_TRUE(target.calls.empty()) << "nothing runs before the GUI thread takes its events";
    EXPECT_FALSE(asked.load()) << "a question is unanswered until it is asked";

    deliverPostedEvents();
    deliverPostedEvents();

    EXPECT_EQ(target.asked(), (std::vector<std::string>{
                                  "image of an image",
                                  "sequence of a sequence debayered",
                                  "add",
                                  "remove and delete",
                                  "list",
                                  "actions",
                                  "add recent a.fits",
                                  "remove recent b.fits",
                                  "error Open: unreadable",
                                  "warning Save: clipped",
                                  "info Settings: set aside",
                                  "ask Close warning",
                                  "activate",
                              }));
    for (const Call& call : target.calls)
    {
        EXPECT_EQ(call.thread, std::this_thread::get_id()) << call.what << " ran off the GUI thread";
    }
    EXPECT_EQ(customRuns, 1);
    EXPECT_EQ(customThread, std::this_thread::get_id());
    EXPECT_EQ(answer, 42);
    for (const std::atomic_bool* finished : {&imageMade, &sequenceMade, &listed, &added, &removed, &asked, &activated})
    {
        EXPECT_TRUE(finished->load()) << "an event that ran says so";
    }
}

TEST(ApplicationEvents, NoneRunsAfterTheCoreIsGone)
{
    RecordingTarget target;
    bool customRan = false;
    {
        auto core = std::make_unique<ApplicationCore>();
        core->setEventTarget(&target);
        std::thread worker([&]() {
            core->notifyErrorMessageAsync(nullptr, QStringLiteral("Open"), QStringLiteral("unreadable"));
            core->addRecentFileAsync("a.fits", nullptr, nullptr);
            core->runCustomCodeAsync([&customRan]() { customRan = true; });
        });
        worker.join();
    }

    deliverPostedEvents();

    EXPECT_TRUE(target.calls.empty());
    EXPECT_FALSE(customRan);
}

// The window the events were posted for goes before the core: what it had not run yet is let go, and what is posted
// once it is gone asks nothing of it.
TEST(ApplicationEvents, NoneRunsForATargetThatWent)
{
    ApplicationCore core;
    RecordingTarget target;
    core.setEventTarget(&target);
    bool customRan = false;
    core.notifyInfoMessageAsync(nullptr, QStringLiteral("Settings"), QStringLiteral("set aside"));
    core.runCustomCodeAsync([&customRan]() { customRan = true; });

    core.discardPendingEvents();
    core.setEventTarget(nullptr);
    core.updateActiveImageActionsAsync();
    core.createNewImageViewControlFromImageAsync(nullptr, an::ui::ImageDocument(), nullptr, "late",
                                                 ViewControlMode::standard, nullptr, false, true, nullptr);
    deliverPostedEvents();

    EXPECT_TRUE(target.calls.empty());
    EXPECT_FALSE(customRan) << "a discarded event never runs";
    EXPECT_EQ(core.eventTarget(), nullptr);
}

TEST(ApplicationEvents, AnEventThatFailsIsReportedAndTheNextOneStillRuns)
{
    ApplicationCore core;
    RecordingTarget target;
    core.setEventTarget(&target);
    bool nextRan = false;

    core.runCustomCodeAsync([]() { throw std::runtime_error("the work failed"); });
    core.runCustomCodeAsync([&nextRan]() { nextRan = true; });
    deliverPostedEvents();

    EXPECT_EQ(target.asked(),
              (std::vector<std::string>{
                  "failure Async Operation Error: Exception running async operation: the work failed"}));
    EXPECT_TRUE(nextRan);
}

/// An event of the kind every library posts through the application context, which runs whatever it was made with.
class PlainEvent final : public AfterNightEvent
{
public:
    explicit PlainEvent(bool& ran) : AfterNightEvent(nullptr), m_ran(ran) {}

private:
    void runImpl() override
    {
        m_ran = true;
    }

    bool& m_ran;
};

// The libraries' own events - a process control's, posted through the context - run on the core as the application's.
TEST(ApplicationEvents, AnyApplicationEventPostedToTheCoreRunsThere)
{
    ApplicationCore core;
    bool ran = false;
    core.postEvent(new PlainEvent(ran));
    EXPECT_FALSE(ran);

    deliverPostedEvents();

    EXPECT_TRUE(ran);
}

} // namespace
