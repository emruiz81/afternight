// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Launching a process the registry holds: one it does not hold is not launched; one that opens its own window is,
/// and joins the processes used last; one that acts on an image says so when there is none to act on; and the
/// extensions are listed once each, by name.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "core/ApplicationCore.h"
#include "services/ProcessLaunchService.h"
#include "services/RecentProcessesModel.h"

#include "app/ExtensionManager.h"
#include "app/ProcessRegistry.h"

#include <gtest/gtest.h>

#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>

#include <string>
#include <vector>

namespace
{

using an::tests::shellcore::answerWhenShown;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessLaunchMode;
using an::ui::process_registry::ProcessRegistry;
using an::ui::shell::ApplicationCore;
using an::ui::shell::ProcessLaunchService;

class ProcessLaunchServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        // The suite links no processes library: the registry holds only what a case puts in it.
        ProcessRegistry::instance().clear();
        m_core.openStores();
        m_core.setShell(&m_shell);
    }

    void TearDown() override
    {
        ProcessRegistry::instance().clear();
        m_core.setShell(nullptr);
        m_core.closeStores();
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    RecordingShell m_shell;
    ApplicationCore m_core;
};

TEST_F(ProcessLaunchServiceTest, AProcessTheRegistryDoesNotHoldIsNotLaunched)
{
    EXPECT_FALSE(m_core.processLaunch().launch("tests.actionNowhere"));
    EXPECT_TRUE(m_core.recentProcesses().entries().empty());
}

TEST_F(ProcessLaunchServiceTest, AProcessWithItsOwnWindowIsLaunchedAndJoinsTheRecentOnes)
{
    int launched = 0;
    ProcessDescriptor descriptor;
    descriptor.id = "tests.actionOwnWindow";
    descriptor.display_name = "Own Window";
    descriptor.launch_mode = ProcessLaunchMode::TopLevel;
    descriptor.launch_top_level = [&launched]() { ++launched; };
    ASSERT_TRUE(ProcessRegistry::instance().registerProcess(descriptor));
    int recentChanged = 0;
    ProcessLaunchService::Hooks hooks;
    hooks.recentProcessesChanged = [&recentChanged]() { ++recentChanged; };
    m_core.processLaunch().setHooks(std::move(hooks));

    EXPECT_TRUE(m_core.processLaunch().launch("tests.actionOwnWindow"));

    EXPECT_EQ(launched, 1);
    ASSERT_EQ(m_core.recentProcesses().entries().size(), 1u);
    EXPECT_EQ(m_core.recentProcesses().entries().front().id, "tests.actionOwnWindow");
    EXPECT_EQ(m_core.recentProcesses().entries().front().displayName, "Own Window");
    EXPECT_EQ(recentChanged, 1);
}

TEST_F(ProcessLaunchServiceTest, AnImageProcessWithNoImageToActOnSaysSoOverTheShellsWindow)
{
    int launched = 0;
    ProcessDescriptor descriptor;
    descriptor.id = "tests.actionOnImage";
    descriptor.display_name = "On Image";
    descriptor.process_name = "On Image";
    descriptor.launch_mode = ProcessLaunchMode::SingleImage;
    descriptor.launch_single_image = [&launched](ImageViewControl*) { ++launched; };
    ASSERT_TRUE(ProcessRegistry::instance().registerProcess(descriptor));

    bool shown = false;
    QPointer<QWidget> parent;
    QString text;
    answerWhenShown<QMessageBox>([&](QMessageBox& box) {
        shown = true;
        parent = box.parentWidget();
        text = box.text();
        box.accept();
    });
    (void)m_core.processLaunch().launch("tests.actionOnImage");

    EXPECT_EQ(launched, 0);
    ASSERT_TRUE(shown);
    EXPECT_EQ(parent.data(), m_shell.mainWindow());
    EXPECT_EQ(text, QStringLiteral("No selected image to process"));
    EXPECT_TRUE(m_core.recentProcesses().entries().empty());
}

TEST_F(ProcessLaunchServiceTest, TheExtensionsAreListedOnceEachInTheOrderOfTheirNames)
{
    using an::ui::extensions::ExtensionLoadResult;
    using an::ui::extensions::ExtensionLoadStatus;
    using an::ui::extensions::ExtensionManifest;

    const auto loaded = [](const std::string& id, const std::string& name, const std::string& homepage,
                           const ExtensionLoadStatus status) {
        ExtensionLoadResult result;
        result.status = status;
        ExtensionManifest manifest;
        manifest.id = id;
        manifest.name = name;
        manifest.repository_metadata.homepage_url = homepage;
        result.manifest = manifest;
        return result;
    };
    const std::vector<ExtensionLoadResult> discovered = {
        loaded("org.zeta", "zeta tools", "https://zeta.example", ExtensionLoadStatus::Ready),
        loaded("org.alpha", "Alpha", "", ExtensionLoadStatus::Ready),
        loaded("org.zeta", "Zeta Tools", "https://zeta.example/2", ExtensionLoadStatus::Ready),
        loaded("org.broken", "Broken", "", ExtensionLoadStatus::Invalid),
        loaded("", "Nameless", "", ExtensionLoadStatus::Ready)};

    const std::vector<an::ui::shell::ExtensionFamily> families = ProcessLaunchService::extensionFamilies(discovered);

    ASSERT_EQ(families.size(), 2u);
    EXPECT_EQ(families[0].displayName, QStringLiteral("Alpha"));
    EXPECT_EQ(families[1].displayName, QStringLiteral("Zeta Tools")) << "the last description of an extension stands";
    EXPECT_EQ(families[1].homepageUrl, "https://zeta.example/2");
}

} // namespace
