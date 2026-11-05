// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// A census of the Qt objects made while it is open: every object constructed in its lifetime and not yet destroyed,
// read through the hooks Qt calls from every QObject's constructor and destructor.
//
// It is how a case shows that a window it closed left nothing it made behind - a layout taken out of its parent and
// never deleted, a child given no parent - on a host whose sanitizer has no leak detection to say so. Open it around
// the second window of a kind, not the first: a window's first opening makes what the process keeps for every later one
// - a style's caches - and those are not the window's. A thread pool's threads are set aside wherever they are made.
//
// The hooks are one table for the process, so one census is open at a time; a census chains to whatever was installed
// before it and puts it back when it closes. Objects made on any thread are counted.

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QThread>
#include <QtGlobal>

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
// Qt's hook table, declared by its private qhooks_p.h: the table's version, its size, Qt's version, then the callbacks
// for an object's construction and destruction.
extern quintptr Q_CORE_EXPORT qtHookData[];
QT_END_NAMESPACE

namespace an::tests::harness
{

namespace census_detail
{

inline constexpr std::size_t kHookDataSize = 1;
inline constexpr std::size_t kAddQObject = 3;
inline constexpr std::size_t kRemoveQObject = 4;

using ObjectHook = void (*)(QObject*);

struct State
{
    std::mutex mutex;
    std::unordered_set<QObject*>* open = nullptr;
    ObjectHook previousAdd = nullptr;
    ObjectHook previousRemove = nullptr;
};

[[nodiscard]] inline State& state()
{
    static State instance;
    return instance;
}

inline void added(QObject* object)
{
    State& census = state();
    ObjectHook previous = nullptr;
    {
        const std::lock_guard<std::mutex> lock(census.mutex);
        if (census.open != nullptr)
        {
            census.open->insert(object);
        }
        previous = census.previousAdd;
    }
    if (previous != nullptr)
    {
        previous(object);
    }
}

inline void removed(QObject* object)
{
    State& census = state();
    ObjectHook previous = nullptr;
    {
        const std::lock_guard<std::mutex> lock(census.mutex);
        if (census.open != nullptr)
        {
            census.open->erase(object);
        }
        previous = census.previousRemove;
    }
    if (previous != nullptr)
    {
        previous(object);
    }
}

} // namespace census_detail

class QObjectCensus
{
public:
    QObjectCensus()
    {
        census_detail::State& census = census_detail::state();
        const std::lock_guard<std::mutex> lock(census.mutex);
        m_usable = census.open == nullptr && qtHookData[census_detail::kHookDataSize] > census_detail::kRemoveQObject;
        if (!m_usable)
        {
            return;
        }
        census.open = &m_alive;
        census.previousAdd = reinterpret_cast<census_detail::ObjectHook>(qtHookData[census_detail::kAddQObject]);
        census.previousRemove = reinterpret_cast<census_detail::ObjectHook>(qtHookData[census_detail::kRemoveQObject]);
        qtHookData[census_detail::kAddQObject] = reinterpret_cast<quintptr>(&census_detail::added);
        qtHookData[census_detail::kRemoveQObject] = reinterpret_cast<quintptr>(&census_detail::removed);
    }

    ~QObjectCensus()
    {
        if (!m_usable)
        {
            return;
        }
        census_detail::State& census = census_detail::state();
        const std::lock_guard<std::mutex> lock(census.mutex);
        qtHookData[census_detail::kAddQObject] = reinterpret_cast<quintptr>(census.previousAdd);
        qtHookData[census_detail::kRemoveQObject] = reinterpret_cast<quintptr>(census.previousRemove);
        census.open = nullptr;
        census.previousAdd = nullptr;
        census.previousRemove = nullptr;
    }

    QObjectCensus(const QObjectCensus&) = delete;
    QObjectCensus& operator=(const QObjectCensus&) = delete;

    /// Whether this Qt offers the hooks and no other census was open, so the census counts anything at all.
    [[nodiscard]] bool usable() const
    {
        return m_usable;
    }

    /// Every object made since the census opened and not destroyed, each as its class and, when it has one, its object
    /// name - in no particular order.
    [[nodiscard]] std::vector<std::string> survivors() const
    {
        census_detail::State& census = census_detail::state();
        const std::lock_guard<std::mutex> lock(census.mutex);
        std::vector<std::string> named;
        named.reserve(m_alive.size());
        for (const QObject* object : m_alive)
        {
            // The gesture manager keeps a deleted widget's gestures until it next filters an event, and deletes them
            // then or with the application; they are Qt's, not the window's. A single-shot timer still waiting deletes
            // itself when it fires, whether or not what it was to call is still there. A thread pool starts a thread
            // when it has more work than idle threads and ends it, with the thread's event dispatcher, once it has
            // idled past its expiry; both are the pool's, whoever's work started the thread.
            if (object->inherits("QGesture") ||
                std::string_view(object->metaObject()->className()) == "QSingleShotTimer" || pooled(object) ||
                (object->inherits("QAbstractEventDispatcher") && pooled(object->thread())))
            {
                continue;
            }
            std::string name = object->metaObject()->className();
            const QString objectName = object->objectName();
            if (!objectName.isEmpty())
            {
                name += " '" + objectName.toStdString() + "'";
            }
            named.push_back(std::move(name));
        }
        return named;
    }

private:
    /// Whether an object is a thread a thread pool started.
    [[nodiscard]] static bool pooled(const QObject* object)
    {
        return object != nullptr && std::string_view(object->metaObject()->className()) == "QThreadPoolThread";
    }

    std::unordered_set<QObject*> m_alive;
    bool m_usable = false;
};

/// Survivors as one failure message: each on a line of its own.
[[nodiscard]] inline std::string listed(const std::vector<std::string>& survivors)
{
    std::string text;
    for (const std::string& survivor : survivors)
    {
        text += "\n  " + survivor;
    }
    return text;
}

/// Delivers the deletions objects asked for with deleteLater, and what those deletions post in turn.
inline void deliverDeferredDeletions()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// What a piece of work - open a window and close it - made and left behind. The work is done once outside a census,
/// so what a first run makes for every later one is made, and then again inside one; deferred deletions are delivered
/// after each. A census that could not be opened is reported as a survivor, so a case cannot pass by counting nothing.
template <typename Work> [[nodiscard]] std::vector<std::string> leftBehindBy(Work&& work)
{
    work();
    deliverDeferredDeletions();
    QObjectCensus census;
    if (!census.usable())
    {
        return {"no census: the hooks are missing or another census is open"};
    }
    work();
    deliverDeferredDeletions();
    return census.survivors();
}

} // namespace an::tests::harness
