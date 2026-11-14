// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "app/AfterNightAppUiBuilder.h"

#include "fixtures/RecordedActions.h"

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

// The application's Process menu holds a Mask Generation menu, and Mask Drawing is one of its entries.
TEST(MaskDrawingRegistrationTest, TheProcessMenusMaskGenerationMenuOffersMaskDrawing)
{
    QMainWindow window;
    const an::tests::fixtures::RecordedActions actions(window);
    ASSERT_TRUE(actions.loaded());
    const an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window, actions.lookup());

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
