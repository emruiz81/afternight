// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the dashboard suite, linked against the Controls library: the dashboard, its toolbar, recent files
/// grid, featured file and onboarding panel are the library's, made under a headless QApplication in an application
/// double whose settings and thumbnails are the library's own repositories over an in-memory database, and reached
/// through an application context that lends them as the application's does. The dashboard reads no image here, so the
/// context lends no engine session and the dashboard shows no file facts.

#include "Test_MainDashboard/DashboardApplication.h"

#include "controls/IAppContext.h"
#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace
{

/// Icons the dashboard asks for by name, all empty, and transparent pixmaps of the size asked for.
class DashboardIcons final : public an::ui::IIconProvider
{
public:
    [[nodiscard]] QIcon getIcon(const QString&) override
    {
        return {};
    }

    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString&, QSize size, QColor) override
    {
        if (!size.isValid())
        {
            return {};
        }
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        return pixmap;
    }

    [[nodiscard]] QColor getColorNormal() const override
    {
        return QColor(245, 245, 245);
    }

    [[nodiscard]] QColor getColorDisabled() const override
    {
        return QColor(140, 140, 140);
    }
};

/// The application context the dashboard reaches its settings, recent files and thumbnails through, each lent by the
/// application double; the services the dashboard does not use are not lent.
class DashboardContext final : public an::ui::IAppContext,
                               public an::ui::IGlobalSettingsProvider,
                               public an::ui::IRecentFilesService,
                               public an::ui::IThumbnailPathProvider
{
public:
    explicit DashboardContext(DashboardApplication& application)
        : m_application(application), m_icons(std::make_shared<DashboardIcons>())
    {
    }

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return m_icons;
    }
    [[nodiscard]] an::ui::IDialogService* getDialogService() override
    {
        return nullptr;
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
        return nullptr;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override
    {
        return nullptr;
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
        return nullptr;
    }
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return this;
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
        return nullptr;
    }
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return nullptr;
    }
    [[nodiscard]] QWidget* getMainWindow() override
    {
        return &m_application;
    }

    // ---- IGlobalSettingsProvider: the application double's settings repository.

    bool upsert(const QString& key, const QString& value) override
    {
        return m_application.getGlobalSettingsPersistence()->upsert(key, value);
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return m_application.getGlobalSettingsPersistence()->select(key, defaultValue);
    }
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        return m_application.getGlobalSettingsPersistence()->selectOptional(key);
    }

    // ---- IRecentFilesService and IThumbnailPathProvider: the application double's recent files and thumbnails.

    [[nodiscard]] std::deque<std::string> getRecentFilesList() const override
    {
        return m_application.getRecentFilesList();
    }
    void openRecentSingleFile(const QString&) override {}
    void openDroppedFiles(const QStringList& files) override
    {
        m_application.openDroppedFiles(files);
    }
    [[nodiscard]] QString selectThumbnailPath(const QString& filePath) const override
    {
        return m_application.getThumbnailPersistence()->select(filePath);
    }

private:
    DashboardApplication& m_application;
    an::ui::IIconProviderPtr m_icons;
};

/// Resets the application double before every case, so each starts from no recent files, thumbnails, settings or
/// dropped files whatever ran before it: the dashboard keeps its toolbar's order and hidden actions in the settings.
class ResetBetweenCases final : public ::testing::EmptyTestEventListener
{
public:
    explicit ResetBetweenCases(DashboardApplication& application) : m_application(application) {}

    void OnTestStart(const ::testing::TestInfo&) override
    {
        m_application.resetForTest();
    }

private:
    DashboardApplication& m_application;
};

} // namespace

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_MainDashboard", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);

    int result = 1;
    {
        DashboardApplication application;
        if (!application.ready())
        {
            return 1;
        }
        DashboardContext context(application);
        an::ui::setAppContext(&context);
        ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
        auto* reset = new ResetBetweenCases(application);
        listeners.Append(reset);
        result = RUN_ALL_TESTS();
        delete listeners.Release(reset);
        an::ui::clearAppContext();
    }
    return harness.finish(result);
}
