// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SessionConsoleBridge.h"

#include <verdigris/wobjectimpl.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <mutex>

namespace
{

QString trimTrailingNewlines(QString text)
{
    while (!text.isEmpty() && (text.endsWith('\n') || text.endsWith('\r')))
    {
        text.chop(1);
    }

    return text;
}

template <typename Mutex> class session_console_sink final : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit session_console_sink(an::ui::SessionConsoleBridge* bridge) : m_bridge(bridge) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        QPointer<an::ui::SessionConsoleBridge> bridge = m_bridge;
        if (!bridge)
        {
            return;
        }

        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);

        const QString text =
            trimTrailingNewlines(QString::fromUtf8(formatted.data(), static_cast<int>(formatted.size())));
        if (text.isEmpty())
        {
            return;
        }

        const auto level = msg.level;
        QMetaObject::invokeMethod(
            bridge,
            [bridge, text, level]() {
                if (bridge)
                {
                    bridge->appendEntry(text, level);
                }
            },
            Qt::QueuedConnection);
    }

    void flush_() override {}

private:
    QPointer<an::ui::SessionConsoleBridge> m_bridge;
};

using session_console_sink_mt = session_console_sink<std::mutex>;

} // anonymous namespace

namespace an::ui
{

SessionConsoleBridge* SessionConsoleBridge::s_instance = nullptr;

W_OBJECT_IMPL(SessionConsoleBridge)

SessionConsoleBridge::SessionConsoleBridge(QObject* parent, const int maxEntries)
    : QObject(parent), m_maxEntries((std::max)(1, maxEntries))
{
}

SessionConsoleBridge::~SessionConsoleBridge()
{
    if (s_instance == this)
    {
        s_instance = nullptr;
        an::platform::clearSessionConsoleSink();
    }
}

SessionConsoleBridge* SessionConsoleBridge::initialize(QObject* parent, const int maxEntries)
{
    if (!s_instance)
    {
        QObject* bridgeParent = parent ? parent : QCoreApplication::instance();
        s_instance = new SessionConsoleBridge(bridgeParent, maxEntries);
        an::platform::setSessionConsoleSink(s_instance->createSink());
    }

    return s_instance;
}

SessionConsoleBridge* SessionConsoleBridge::instance()
{
    return s_instance;
}

std::shared_ptr<spdlog::sinks::sink> SessionConsoleBridge::createSink()
{
    auto sink = std::make_shared<session_console_sink_mt>(this);
    sink->set_pattern(std::string(an::platform::kConsoleLogPattern));
    return sink;
}

QVector<SessionConsoleEntry> SessionConsoleBridge::entriesSnapshot() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_entries;
}

int SessionConsoleBridge::maxEntries() const
{
    return m_maxEntries;
}

void SessionConsoleBridge::appendEntry(const QString& text, const spdlog::level::level_enum level)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (text.isEmpty())
    {
        return;
    }

    m_entries.push_back(SessionConsoleEntry{text, level});
    while (m_entries.size() > m_maxEntries)
    {
        m_entries.removeFirst();
    }

    sessionEntryAppended(text, static_cast<int>(level));
}

void SessionConsoleBridge::clearHistory()
{
    if (m_entries.isEmpty())
    {
        return;
    }

    m_entries.clear();
    sessionHistoryCleared();
}

} // namespace an::ui
