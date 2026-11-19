// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The sessions of the extension host as a shell shows them, with no widget: where each session's host stands, the run
/// of the window it shows, whether its host raises that window when asked, and how a session ended.

#include "AfterNightControlsExport.h"

#include "controls/ProcessPresentation.h"

#include <QPointer>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class ImageViewControl;

namespace an::ui::extensions
{

/// Where a session's host stands.
enum class ExtensionHostStanding : std::uint8_t
{
    /// Started; its runtime - the interpreter and the extension's import - is not ready yet.
    Starting,
    /// Its runtime is ready.
    Ready,
    /// It stopped answering its heartbeat: the session ends.
    NotResponding
};

/// A session of the extension host, as a shell shows it.
struct ExtensionSessionStanding
{
    std::string sessionId;

    /// The registry descriptor the session was launched for.
    std::string descriptorId;

    /// The name the session was launched under: its process's.
    QString displayName;

    /// The image the session was launched on; null for a session launched on none.
    QPointer<ImageViewControl> target;

    ExtensionHostStanding host = ExtensionHostStanding::Starting;

    /// The run of the window the host shows - as the host tells it, or as the application's own request in flight is.
    ProcessRunState run;

    /// Whether the host shows its window in front when asked (`raiseSessionWindow`).
    bool raisesItsWindow = false;

    /// Whether the application draws the session's panel: its process is the application's, a task like any other's,
    /// so a shell makes no task of the session itself.
    bool hostedPanel = false;
};

/// Whether two standings say the same.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool sameStanding(const ExtensionSessionStanding& a,
                                                       const ExtensionSessionStanding& b);

/// How a session ended.
struct ExtensionSessionEnd
{
    std::string sessionId;

    /// Whether its host failed - crashed, stopped answering, refused what it was sent or failed to start - rather than
    /// closing as it should: its window closed, or the application stopped it.
    bool failed = false;

    /// Why it failed; empty for a session that closed as it should.
    QString reason;
};

/// What tells a shell of the sessions of the extension host; the launch coordinator is one.
class AFTERNIGHTCONTROLS_API IExtensionSessionFeed
{
public:
    /// What an observer is told, on the GUI thread.
    struct Observer
    {
        /// A session started, or where it stands changed.
        std::function<void(const ExtensionSessionStanding& standing)> stood;
        /// A session ended: it is not told of again.
        std::function<void(const ExtensionSessionEnd& end)> ended;
    };

    IExtensionSessionFeed() = default;
    virtual ~IExtensionSessionFeed() = default;
    IExtensionSessionFeed(const IExtensionSessionFeed&) = delete;
    IExtensionSessionFeed& operator=(const IExtensionSessionFeed&) = delete;
    IExtensionSessionFeed(IExtensionSessionFeed&&) = delete;
    IExtensionSessionFeed& operator=(IExtensionSessionFeed&&) = delete;

    /// The sessions as they stand now, in the order they started.
    [[nodiscard]] virtual std::vector<ExtensionSessionStanding> sessionStandings() const = 0;

    /// Adds an observer, told of every change from now on; the token takes it out again.
    virtual std::size_t observeSessions(Observer observer) = 0;
    virtual void stopObservingSessions(std::size_t token) = 0;

    /// Asks a session's host to show its window in front; false, saying why, for a session that is not running or whose
    /// host does not raise its window.
    virtual bool raiseSessionWindow(std::string_view sessionId, std::string* error = nullptr) = 0;

    /// Ends a session as the application stops one: its host is told to close, and the session ends as it should.
    virtual void closeSession(std::string_view sessionId) = 0;
};

/// What observers were told of the sessions, and the telling of what changed: a standing is told when it was not
/// told before or says something else now, and an end once, for a session that was told of. Used on the GUI thread.
class AFTERNIGHTCONTROLS_API ExtensionSessionReport final
{
public:
    ExtensionSessionReport();
    ~ExtensionSessionReport();
    ExtensionSessionReport(const ExtensionSessionReport&) = delete;
    ExtensionSessionReport& operator=(const ExtensionSessionReport&) = delete;
    ExtensionSessionReport(ExtensionSessionReport&&) = delete;
    ExtensionSessionReport& operator=(ExtensionSessionReport&&) = delete;

    std::size_t observe(IExtensionSessionFeed::Observer observer);
    void stopObserving(std::size_t token);

    /// A session stands as `standing` says: told when it says something new.
    void stood(const ExtensionSessionStanding& standing);

    /// A session ended: told when the session was told of, and forgotten.
    void ended(const ExtensionSessionEnd& end);

    /// The sessions told of and not ended, in the order they were first told.
    [[nodiscard]] std::vector<ExtensionSessionStanding> told() const;

private:
    std::vector<ExtensionSessionStanding> m_told;
    std::map<std::size_t, IExtensionSessionFeed::Observer> m_observers;
    std::size_t m_nextToken = 1;
};

} // namespace an::ui::extensions
