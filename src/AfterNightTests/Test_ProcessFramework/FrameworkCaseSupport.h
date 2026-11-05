// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the process framework suite's own cases share beyond the suite's context: a view of a document registered in
/// that context and taken out of it before it is deleted, and an application context that lends everything the suite's
/// context lends except the global settings and the process windows' settings, which it keeps in memory of its own so
/// a case states what a window restores, sees what a window stores, and makes a read or a write fail.
#pragma once

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"

#include <QHash>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace an::tests::processes::framework
{

/// Deletes a view the suite's context registered, taking it out of the registry first: a view leaves the registry when
/// it is closed, not when it is deleted, and a registry that kept it would hand a later case a deleted view.
struct SuiteViewDeleter
{
    void operator()(ImageViewControl* view) const
    {
        if (view == nullptr)
        {
            return;
        }
        suiteContext().removeViewControl(view, false);
        delete view;
        QCoreApplication::processEvents();
    }
};

/// A view made for one case and registered in the suite's context.
using SuiteView = std::unique_ptr<ImageViewControl, SuiteViewDeleter>;

/// A grey Float32 view of a size, every sample `value`, registered in the suite's context as the application registers
/// a view it opens; null, failing the case, when its document cannot be made.
[[nodiscard]] inline SuiteView greySuiteView(const std::string& name, std::uint32_t width, std::uint32_t height,
                                             float value = 0.25f)
{
    std::optional<an::ui::ImageDocument> document = greyDocument(
        suiteContext().engineSession(), width, height, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    if (!document.has_value())
    {
        return nullptr;
    }
    return SuiteView(makeTargetView(name, std::move(*document)).release());
}

/// What a window stored for its process.
struct StoredWindow
{
    int windowState = 0;
    int width = 0;
    int height = 0;
    QString json;
};

/// An application context that lends what another lends, except the global settings and the process windows'
/// settings, which it keeps itself. GUI thread only; a case whose settings are read on a worker derives a context that
/// locks around them.
class WindowSettingsContext : public an::ui::IAppContext,
                              public an::ui::IGlobalSettingsProvider,
                              public an::ui::IProcessSettingsProvider
{
public:
    explicit WindowSettingsContext(an::ui::IAppContext& lender) : m_lender(lender) {}

    // ---- IAppContext: the lender's, but for the two settings stores.

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return m_lender.getIconProvider();
    }
    [[nodiscard]] an::ui::IDialogService* getDialogService() override
    {
        return m_lender.getDialogService();
    }
    [[nodiscard]] an::ui::IGlobalSettingsProvider* getGlobalSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override
    {
        return m_lender.getInterfaceSettingsRuntimeService();
    }
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return m_lender.getTempFileService();
    }
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override
    {
        return m_lender.getPathMemoryService();
    }
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override
    {
        return m_lender.getFileDialogService();
    }
    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return m_lender.getViewFactory();
    }
    [[nodiscard]] an::ui::IViewRegistry* getViewRegistry() override
    {
        return m_lender.getViewRegistry();
    }
    [[nodiscard]] an::ui::IAppEventBus* getAppEventBus() override
    {
        return m_lender.getAppEventBus();
    }
    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return m_lender.getProcessRegistry();
    }
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override
    {
        return m_lender.getOutputIntentProvider();
    }
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override
    {
        return m_lender.getRecentFilesService();
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return m_lender.getThumbnailPathProvider();
    }
    [[nodiscard]] an::ui::IViewControlManager* getViewControlManager() override
    {
        return m_lender.getViewControlManager();
    }
    [[nodiscard]] an::ui::IStatusBarService* getStatusBarService() override
    {
        return m_lender.getStatusBarService();
    }
    [[nodiscard]] an::ui::IActionStateManager* getActionStateManager() override
    {
        return m_lender.getActionStateManager();
    }
    [[nodiscard]] an::ui::IEngineSessionProvider* getEngineSessionProvider() override
    {
        return m_lender.getEngineSessionProvider();
    }
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return m_lender.getRawOpenPolicyService();
    }
    [[nodiscard]] QWidget* getMainWindow() override
    {
        return m_lender.getMainWindow();
    }

    // ---- IGlobalSettingsProvider, in memory.

    bool upsert(const QString& key, const QString& value) override
    {
        m_settings.insert(key, value);
        return true;
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return m_settings.value(key, defaultValue);
    }
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const auto found = m_settings.constFind(key);
        return found == m_settings.constEnd() ? std::nullopt : std::optional<QString>(*found);
    }

    // ---- IProcessSettingsProvider, in memory, failing on request.

    bool upsert(const QString& processName, int windowState, int width, int height, const QString& json) override
    {
        ++windowWrites;
        if (failWindowWrites)
        {
            return false;
        }
        m_windows.insert(processName,
                         StoredWindow{.windowState = windowState, .width = width, .height = height, .json = json});
        return true;
    }
    [[nodiscard]] bool select(const QString& processName, int& windowState, int& width, int& height,
                              QString& json) override
    {
        if (failWindowReads)
        {
            return false;
        }
        const auto found = m_windows.constFind(processName);
        if (found == m_windows.constEnd())
        {
            return false;
        }
        windowState = found->windowState;
        width = found->width;
        height = found->height;
        json = found->json;
        return true;
    }

    // ---- What the cases read and steer.

    /// Whether a window's read of what it stored fails.
    bool failWindowReads = false;

    /// Whether a window's write of what it stores fails.
    bool failWindowWrites = false;

    /// How many writes windows asked for, failed ones included.
    int windowWrites = 0;

    /// What a window of a process stored, if anything.
    [[nodiscard]] std::optional<StoredWindow> stored(const QString& processName) const
    {
        const auto found = m_windows.constFind(processName);
        return found == m_windows.constEnd() ? std::nullopt : std::optional<StoredWindow>(*found);
    }

private:
    an::ui::IAppContext& m_lender;
    QHash<QString, QString> m_settings;
    QHash<QString, StoredWindow> m_windows;
};

} // namespace an::tests::processes::framework
