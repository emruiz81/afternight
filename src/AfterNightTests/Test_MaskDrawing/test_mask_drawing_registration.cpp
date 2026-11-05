// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "app/AfterNightAppUiBuilder.h"
#include "app/MenuActionManager.h"
#include "app/ProcessRegistry.h"

#include <QAction>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

std::filesystem::path findRepoRoot()
{
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty())
    {
        if (std::filesystem::exists(current / "src" / "AfterNightApp"))
        {
            return current;
        }
        if (current == current.root_path())
        {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

std::string readFileText(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        return {};
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// The menu titled `title` among the entries of a menu bar or a menu, or null.
QMenu* submenuTitled(const QList<QAction*>& entries, const QString& title)
{
    for (QAction* entry : entries)
    {
        QMenu* menu = entry->menu();
        if (menu != nullptr && menu->title() == title)
        {
            return menu;
        }
    }
    return nullptr;
}

/// Empties the process registry for a case, and again when the case ends, passing or failing.
class ClearedProcessRegistry
{
public:
    ClearedProcessRegistry()
    {
        an::ui::process_registry::ProcessRegistry::instance().clear();
    }
    ~ClearedProcessRegistry()
    {
        an::ui::process_registry::ProcessRegistry::instance().clear();
    }
    ClearedProcessRegistry(const ClearedProcessRegistry&) = delete;
    ClearedProcessRegistry& operator=(const ClearedProcessRegistry&) = delete;
};

// The application's Process menu holds a Mask Generation menu, and Mask Drawing is one of its entries.
TEST(MaskDrawingRegistrationTest, TheProcessMenusMaskGenerationMenuOffersMaskDrawing)
{
    QMainWindow window;
    const an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window);

    QMenu* const process = submenuTitled(window.menuBar()->actions(), QStringLiteral("Process"));
    ASSERT_NE(process, nullptr);
    QMenu* const maskGeneration = submenuTitled(process->actions(), QStringLiteral("Mask Generation"));
    ASSERT_NE(maskGeneration, nullptr);
    const QList<QAction*> entries = maskGeneration->actions();
    const auto drawing = std::find_if(entries.cbegin(), entries.cend(), [](const QAction* entry) {
        return entry->text() == QStringLiteral("Mask Drawing");
    });
    ASSERT_TRUE(drawing != entries.cend());
    EXPECT_EQ(*drawing, ui.actionMask_Drawing);
}

// The menu action manager binds Mask Drawing to its mask generation descriptor and launches it through its launcher.
TEST(MaskDrawingRegistrationTest, TheMaskDrawingActionAsksTheLauncherForItsMaskGenerationDescriptor)
{
    const ClearedProcessRegistry cleared;
    an::ui::process_registry::ProcessRegistry& registry = an::ui::process_registry::ProcessRegistry::instance();
    // Registered as the processes library registers it: named after its action until the menu names it.
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "mask_generation.actionMask_Drawing";
    descriptor.category = "mask_generation";
    descriptor.action_object_name = "actionMask_Drawing";
    descriptor.display_name = descriptor.action_object_name;
    descriptor.process_name = descriptor.display_name;
    ASSERT_TRUE(registry.registerProcess(std::move(descriptor)));

    QMainWindow window;
    an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window);
    std::vector<std::string> launched;
    an::ui::MenuActionManager::Dependencies dependencies;
    dependencies.signalContext = &window;
    dependencies.ui = &ui;
    dependencies.tryLaunchRegistryProcessById = [&launched](const std::string& id) {
        launched.push_back(id);
        return true;
    };
    const an::ui::MenuActionManager manager(dependencies);
    manager.connectProcessActions();

    const an::ui::process_registry::ProcessDescriptor* bound = registry.find("mask_generation.actionMask_Drawing");
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->display_name, "Mask Drawing");
    EXPECT_TRUE(ui.actionMask_Drawing->isEnabled());
    ui.actionMask_Drawing->trigger();
    EXPECT_EQ(launched, std::vector<std::string>{"mask_generation.actionMask_Drawing"});
}

TEST(MaskDrawingRegistrationTest, MaskDrawingProcessEnablesExecutionAutostretch)
{
    const auto root = findRepoRoot();
    ASSERT_FALSE(root.empty()) << "Could not locate repository root";

    const std::string processSource =
        readFileText(root / "src" / "AfterNightProcesses" / "processes" / "MaskDrawingProcess.cpp");
    ASSERT_FALSE(processSource.empty());

    EXPECT_NE(processSource.find("setExecutionAutostretchSupported(true)"), std::string::npos);
}

} // anonymous namespace
