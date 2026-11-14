// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QString>

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
