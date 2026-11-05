// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <verdigris/wobjectdefs.h>

#include <AfterNightPlatform/Logging.h>

namespace an::ui
{

struct SessionConsoleEntry
{
    QString text;
    spdlog::level::level_enum level = spdlog::level::info;
};

class AFTERNIGHTCONTROLS_API SessionConsoleBridge : public QObject
{
    W_OBJECT(SessionConsoleBridge)

public:
    explicit SessionConsoleBridge(QObject* parent = nullptr, int maxEntries = 10000);
    ~SessionConsoleBridge() override;

    [[nodiscard]] static SessionConsoleBridge* initialize(QObject* parent = nullptr, int maxEntries = 10000);
    [[nodiscard]] static SessionConsoleBridge* instance();

    [[nodiscard]] std::shared_ptr<spdlog::sinks::sink> createSink();
    [[nodiscard]] QVector<SessionConsoleEntry> entriesSnapshot() const;
    [[nodiscard]] int maxEntries() const;

    void appendEntry(const QString& text, spdlog::level::level_enum level);
    void clearHistory();

    void sessionEntryAppended(QString text, int level) W_SIGNAL(sessionEntryAppended, text, level);
    void sessionHistoryCleared() W_SIGNAL(sessionHistoryCleared);

private:
    // Main-thread only; no synchronization needed since initialize()/instance()
    // are always called from the GUI thread.
    static SessionConsoleBridge* s_instance;

    QVector<SessionConsoleEntry> m_entries;
    int m_maxEntries = 10000;
};

} // namespace an::ui
