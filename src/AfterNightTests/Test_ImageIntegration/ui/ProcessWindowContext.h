// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application context the integration's process windows run under in this suite: a window registers itself,
/// restores and stores its geometry and settings, remembers its directories and reads its frames in the one engine
/// session the context lends. Settings and paths are kept in memory and die with the context, so nothing a case does
/// reaches the user's storage; every message the window would show through the dialog service is kept for the case to
/// read. A case installs one for its own lifetime with `ScopedProcessWindowContext`, and can configure a plate solver
/// in it that solves nothing with `configureEmptyGaiaSolver`.

#include "Test_ImageIntegration/ImportSession.h"

#include "controls/IAppContext.h"
#include "controls/settings/SettingsKeys.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/Result.h>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <memory>
#include <optional>
#include <utility>

namespace an::tests::integration
{

/// Icons the windows ask for by name, all empty.
class StandInIconProvider final : public an::ui::IIconProvider
{
public:
    [[nodiscard]] QIcon getIcon(const QString&) override
    {
        return QIcon();
    }
    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString&, QSize size, QColor) override
    {
        return QPixmap(size);
    }
    [[nodiscard]] QColor getColorNormal() const override
    {
        return QColor(Qt::white);
    }
    [[nodiscard]] QColor getColorDisabled() const override
    {
        return QColor(Qt::gray);
    }
};

/// An application context over one engine session, implementing what the process windows reach; every other service
/// is absent, which the windows' helpers read as no service.
class ProcessWindowContext final : public an::ui::IAppContext,
                                   public an::ui::IDialogService,
                                   public an::ui::IGlobalSettingsProvider,
                                   public an::ui::IProcessSettingsProvider,
                                   public an::ui::IPathMemoryService,
                                   public an::ui::IProcessRegistry,
                                   public an::ui::IEngineSessionProvider
{
public:
    ProcessWindowContext() : m_opened(openImportSession(2u)), m_icons(std::make_shared<StandInIconProvider>()) {}

    ~ProcessWindowContext() override
    {
        // A window's workers read the session; they finish before it goes.
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    }

    ProcessWindowContext(const ProcessWindowContext&) = delete;
    ProcessWindowContext& operator=(const ProcessWindowContext&) = delete;

    /// Whether the session opened.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_opened.hasValue();
    }

    // ---- IAppContext.

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return m_icons;
    }
    [[nodiscard]] an::ui::IDialogService* getDialogService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IGlobalSettingsProvider* getGlobalSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewRegistry* getViewRegistry() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IAppEventBus* getAppEventBus() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewControlManager* getViewControlManager() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IStatusBarService* getStatusBarService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IActionStateManager* getActionStateManager() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IEngineSessionProvider* getEngineSessionProvider() override
    {
        return m_opened.hasValue() ? this : nullptr;
    }
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return nullptr;
    }
    [[nodiscard]] QWidget* getMainWindow() override
    {
        return nullptr;
    }

    // ---- IEngineSessionProvider.

    [[nodiscard]] const an::consumer::EngineSession& engineSession() const noexcept override
    {
        return *m_opened;
    }

    // ---- IDialogService: every message is kept, and a question is answered as the case says.

    void openSettingsAt(const QString& pageName) override
    {
        messages.push_back(QStringLiteral("settings:") + pageName);
    }
    void messageBoxYesNo(QWidget*, const QString& title, const QString&, bool, int* ret) override
    {
        messages.push_back(QStringLiteral("question:") + title);
        if (ret != nullptr)
        {
            *ret = answerYes ? QMessageBox::Yes : QMessageBox::No;
        }
    }
    void notifyErrorMessage(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("error:") + title);
    }
    void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text) override
    {
        notifyErrorMessage(parent, title, text);
    }
    void notifyWarnMessage(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("warning:") + title);
    }
    void notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text) override
    {
        notifyWarnMessage(parent, title, text);
    }
    void notifyInfoMessage(QWidget*, const QString& title, const QString&) override
    {
        messages.push_back(QStringLiteral("info:") + title);
    }
    void notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text) override
    {
        notifyInfoMessage(parent, title, text);
    }

    // ---- IGlobalSettingsProvider.

    bool upsert(const QString& key, const QString& value) override
    {
        m_global.insert(key, value);
        return true;
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return m_global.value(key, defaultValue);
    }
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const auto found = m_global.constFind(key);
        return found == m_global.constEnd() ? std::nullopt : std::optional<QString>(*found);
    }

    // ---- IProcessSettingsProvider.

    bool upsert(const QString& processName, int winState, int width, int height, const QString& jsonSettings) override
    {
        m_processes.insert(processName, StoredProcess{winState, width, height, jsonSettings});
        return true;
    }
    [[nodiscard]] bool select(const QString& processName, int& winState, int& width, int& height,
                              QString& jsonSettings) override
    {
        const auto found = m_processes.constFind(processName);
        if (found == m_processes.constEnd())
        {
            return false;
        }
        winState = found->winState;
        width = found->width;
        height = found->height;
        jsonSettings = found->jsonSettings;
        return true;
    }

    // ---- IPathMemoryService.

    [[nodiscard]] QString selectPath(const QString& contextKey, const QString& defaultValue = {}) override
    {
        const QString path = m_paths.value(contextKey);
        return path.isEmpty() ? defaultValue : path;
    }
    bool upsertPath(const QString& contextKey, const QString& path) override
    {
        m_paths.insert(contextKey, path);
        return true;
    }

    // ---- IProcessRegistry.

    void registerProcessInstance(TopLevelProcessBaseControl*) override
    {
        ++registeredProcesses;
    }
    void unregisterProcessInstance(TopLevelProcessBaseControl*) override
    {
        --registeredProcesses;
    }

    /// Every message the dialog service was asked to show, as `kind:title`.
    QStringList messages;

    /// What a yes-or-no question is answered.
    bool answerYes = true;

    /// How many windows are registered now.
    int registeredProcesses = 0;

private:
    struct StoredProcess
    {
        int winState = 0;
        int width = -1;
        int height = -1;
        QString jsonSettings;
    };

    an::Result<an::consumer::EngineSession> m_opened;
    std::shared_ptr<StandInIconProvider> m_icons;
    QHash<QString, QString> m_global;
    QHash<QString, StoredProcess> m_processes;
    QHash<QString, QString> m_paths;
};

/// Installs a context for as long as it lives, and puts back the one installed before it after - the suite's own, which
/// `main` installs for the cases that need none of their own.
class ScopedProcessWindowContext
{
public:
    ScopedProcessWindowContext() : m_previous(an::ui::appContext())
    {
        // setAppContext installs over nothing, so whatever is installed - the suite's own - is taken down first.
        an::ui::clearAppContext();
        an::ui::setAppContext(&m_context);
    }
    ~ScopedProcessWindowContext()
    {
        // A window closed with deleteLater unregisters itself through the context, so it goes before the context.
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        an::ui::clearAppContext();
        if (m_previous != nullptr)
        {
            an::ui::setAppContext(m_previous);
        }
    }
    ScopedProcessWindowContext(const ScopedProcessWindowContext&) = delete;
    ScopedProcessWindowContext& operator=(const ScopedProcessWindowContext&) = delete;

    [[nodiscard]] ProcessWindowContext& context() noexcept
    {
        return m_context;
    }

private:
    an::ui::IAppContext* m_previous = nullptr;
    ProcessWindowContext m_context;
};

/// Writes a Gaia catalog that is ready and holds no star - one chunk, empty of sources - and names it as the internal
/// plate solver's catalog in a context's settings: a solver every check counts as configured, which reaches no network
/// and solves nothing.
///
/// @param context the context whose settings name it.
/// @param directory an empty directory the catalog is written in.
/// @return whether it was written.
[[nodiscard]] inline bool configureEmptyGaiaSolver(ProcessWindowContext& context, const QString& directory)
{
    if (!QDir().mkpath(QDir(directory).filePath(QStringLiteral("chunks"))))
    {
        return false;
    }
    QFile chunk(QDir(directory).filePath(QStringLiteral("chunks/empty.bin")));
    QFile meta(QDir(directory).filePath(QStringLiteral("catalog_meta.json")));
    if (!chunk.open(QIODevice::WriteOnly) || !meta.open(QIODevice::WriteOnly))
    {
        return false;
    }
    meta.write(R"({"catalog_id":"empty","storage":"chunks","format_version":3,"chunk_count":1,)"
               R"("chunks":[{"file":"chunks/empty.bin"}]})");
    return context.upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY), directory);
}

} // namespace an::tests::integration
