// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "app/ExtensionLaunchPreparations.h"

#include "app/ExtensionEnvironmentManager.h"
#include "controls/ProgressDialogControl.h"

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <stop_token>
#include <utility>

namespace an::ui::extensions
{

struct ExtensionLaunchPreparations::Impl
{
    /// A launch asked for while the extension's preparation was being stopped.
    struct Launch
    {
        std::string processId;
        std::string displayName;
    };

    /// A preparation, recorded from its start until its work has returned.
    struct Preparation
    {
        /// Which start this is, so a completion completes the preparation it belongs to and no later one.
        std::uint64_t serial = 0;
        QPointer<ProgressDialogControl> window;
        /// The stop the window's Cancel and its going request; it outlives the window.
        std::stop_token windowStop;
        ExtensionOperationCancellationToken cancellation;
        std::optional<Launch> next;
    };

    QPointer<QWidget> windowParent;
    Work work;
    Finished finished;
    /// Lives as long as this does: a completion that finds it gone finds nothing to complete.
    QObject alive;
    std::map<std::string, Preparation, std::less<>> preparations;
    std::uint64_t nextSerial = 1;
    bool closing = false;

    [[nodiscard]] static bool beingStopped(const Preparation& preparation)
    {
        return preparation.windowStop.stop_requested() || preparation.cancellation.isCancellationRequested();
    }

    /// Whether the preparation is being stopped or the person closed its window: a close hides the window at once and
    /// deletes it later, and only the deletion requests its stop.
    [[nodiscard]] static bool closedOrBeingStopped(const Preparation& preparation)
    {
        return beingStopped(preparation) || preparation.window.isNull() || !preparation.window->isVisible();
    }

    void start(const std::string& processId, const std::string& extensionId, const std::string& displayName);
    void complete(const std::string& extensionId, std::uint64_t serial, const std::string& processId,
                  ExtensionLaunchPreparationOutcome outcome, bool cancelled);
};

void ExtensionLaunchPreparations::Impl::start(const std::string& processId, const std::string& extensionId,
                                              const std::string& displayName)
{
    auto* window = new ProgressDialogControl(windowParent.data(),
                                             QCoreApplication::translate("ExtensionLaunchPreparations", "Preparing %1")
                                                 .arg(QString::fromStdString(displayName)));
    window->setText(
        QCoreApplication::translate("ExtensionLaunchPreparations", "Preparing the extension's Python environment"));
    window->setBusy(true);
    window->show();

    Preparation& preparation = preparations[extensionId];
    preparation = Preparation{};
    preparation.serial = nextSerial++;
    preparation.window = window;
    preparation.windowStop = window->stopToken();

    const QPointer<QObject> receiver(&alive);
    const QPointer<ProgressDialogControl> windowGuard(window);
    QThreadPool::globalInstance()->start([self = this, receiver, windowGuard, work = work,
                                          stop = preparation.windowStop, cancellation = preparation.cancellation,
                                          serial = preparation.serial, processId, extensionId]() {
        // The window's Cancel stops the preparation, as its going and the application's closing do.
        const std::stop_callback cancelOnStop(stop, [cancellation]() { cancellation.requestCancellation(); });
        const Progress progress = [windowGuard](const ExtensionOperationProgress& reported) {
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [windowGuard, message = QString::fromStdString(reported.message), current = reported.current,
                 total = reported.total, indeterminate = reported.indeterminate]() {
                    if (!windowGuard)
                    {
                        return;
                    }
                    if (!message.isEmpty())
                    {
                        windowGuard->setText(message);
                    }
                    const bool busy = indeterminate || total <= 0;
                    windowGuard->setBusy(busy);
                    if (!busy)
                    {
                        windowGuard->setProgressBarRange(0, total);
                        windowGuard->setValue(current);
                    }
                },
                Qt::QueuedConnection);
        };

        // A preparation that throws still completes: its record goes, so the next launch prepares afresh.
        ExtensionLaunchPreparationOutcome outcome;
        try
        {
            outcome = work(extensionId, progress, cancellation);
        }
        catch (const std::exception& error)
        {
            outcome = ExtensionLaunchPreparationOutcome{};
            outcome.diagnostic = error.what();
        }
        catch (...)
        {
            outcome = ExtensionLaunchPreparationOutcome{};
            outcome.diagnostic = "The extension's environment could not be prepared.";
        }
        const bool cancelled = cancellation.isCancellationRequested();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, receiver, extensionId, serial, processId, outcome = std::move(outcome), cancelled]() mutable {
                if (!receiver)
                {
                    return;
                }
                self->complete(extensionId, serial, processId, std::move(outcome), cancelled);
            },
            Qt::QueuedConnection);
    });
}

void ExtensionLaunchPreparations::Impl::complete(const std::string& extensionId, const std::uint64_t serial,
                                                 const std::string& processId,
                                                 ExtensionLaunchPreparationOutcome outcome, const bool cancelled)
{
    const auto recorded = preparations.find(extensionId);
    if (recorded == preparations.end() || recorded->second.serial != serial)
    {
        return;
    }
    // The person may have closed the window after the work returned: the launch is cancelled all the same.
    const bool stopped = closedOrBeingStopped(recorded->second);
    const QPointer<ProgressDialogControl> window = recorded->second.window;
    std::optional<Launch> next = std::move(recorded->second.next);
    preparations.erase(recorded);
    if (window)
    {
        window->accept();
    }
    if (closing)
    {
        return;
    }
    if (!cancelled)
    {
        // The work returned before any stop reached it, so the environment is as prepared as it will be. A launch asked
        // for while the window was going - after the work had returned, before this landed - is the one this completes:
        // the person closed the first launch's window and asked for this one. Without one, the launch that started the
        // preparation is completed - unless the person closed its window meanwhile, which cancels it: the process
        // window of a launch just cancelled once opened.
        if (finished && (next || !stopped))
        {
            finished(next ? next->processId : processId, extensionId, std::move(outcome));
        }
        return;
    }
    if (next)
    {
        start(next->processId, extensionId, next->displayName);
    }
}

ExtensionLaunchPreparations::ExtensionLaunchPreparations(QWidget* windowParent, Work work, Finished finished)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->windowParent = windowParent;
    m_impl->work = std::move(work);
    m_impl->finished = std::move(finished);
}

ExtensionLaunchPreparations::~ExtensionLaunchPreparations()
{
    cancelAll();
}

void ExtensionLaunchPreparations::launch(const std::string& processId, const std::string& extensionId,
                                         const std::string& displayName)
{
    if (m_impl->closing)
    {
        return;
    }
    const auto recorded = m_impl->preparations.find(extensionId);
    if (recorded == m_impl->preparations.end())
    {
        m_impl->start(processId, extensionId, displayName);
        return;
    }
    Impl::Preparation& preparation = recorded->second;
    if (!Impl::closedOrBeingStopped(preparation))
    {
        preparation.window->raise();
        preparation.window->activateWindow();
        return;
    }
    // The preparation is being stopped and its work still runs to its next check of the stop: this launch starts once
    // it has stopped, rather than beside it in a second window.
    preparation.next = Impl::Launch{processId, displayName};
}

void ExtensionLaunchPreparations::cancelAll()
{
    m_impl->closing = true;
    for (auto& [extensionId, preparation] : m_impl->preparations)
    {
        preparation.next.reset();
        preparation.cancellation.requestCancellation();
    }
}

bool ExtensionLaunchPreparations::isPreparing(const std::string_view extensionId) const
{
    return m_impl->preparations.find(extensionId) != m_impl->preparations.end();
}

ProgressDialogControl* ExtensionLaunchPreparations::windowOf(const std::string_view extensionId) const
{
    const auto recorded = m_impl->preparations.find(extensionId);
    return recorded == m_impl->preparations.end() ? nullptr : recorded->second.window.data();
}

ExtensionLaunchPreparationOutcome prepareExtensionEnvironmentForLaunch(
    const std::string& extensionId, ExtensionDiscoveryOptions discoveryOptions,
    const std::function<bool(const std::vector<ExtensionLoadResult>&, const ExtensionDiscoveryOptions&)>& primeCaches,
    const ExtensionLaunchPreparations::Progress& progress, const ExtensionOperationCancellationToken& cancellation)
{
    discoveryOptions.cancellation_token = cancellation;
    ExtensionLaunchPreparationOutcome outcome;

    ExtensionManager extensionManager;
    std::vector<ExtensionLoadResult> discovered = extensionManager.discoverExtensions(discoveryOptions);
    const auto found = std::find_if(discovered.begin(), discovered.end(), [&extensionId](const auto& result) {
        return result.manifest && result.manifest->id == extensionId && result.status == ExtensionLoadStatus::Ready &&
               result.environment;
    });
    if (found == discovered.end())
    {
        outcome.diagnostic = "The extension is no longer installed, or its package is no longer valid.";
    }
    else
    {
        ExtensionEnvironmentManager environmentManager;
        // An environment another package of the extension's shared group still uses is never taken over for it.
        environmentManager.setDiscoveredEnvironments(ExtensionEnvironmentManager::discoveredEnvironmentsOf(discovered));
        outcome.prepared =
            environmentManager.prepareEnvironmentForLaunch(*found->manifest, found->source, discoveryOptions.context,
                                                           nullptr, &outcome.diagnostic, progress, cancellation);
    }
    if (cancellation.isCancellationRequested())
    {
        return outcome;
    }

    // What the menus and the launch read next is what the packages are now.
    discovered = extensionManager.discoverExtensions(discoveryOptions);
    if (outcome.prepared && primeCaches && primeCaches(discovered, discoveryOptions) &&
        !cancellation.isCancellationRequested())
    {
        discovered = extensionManager.discoverExtensions(discoveryOptions);
    }
    if (!store_ready_extension_startup_catalog(discovered, discoveryOptions.context) &&
        an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Failed to refresh the extension startup activation catalog after preparing extension id='{}'",
                    extensionId);
    }
    outcome.discovered = std::move(discovered);
    return outcome;
}

} // namespace an::ui::extensions
