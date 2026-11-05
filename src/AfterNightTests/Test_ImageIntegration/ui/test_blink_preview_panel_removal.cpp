// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
    {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

TEST(BlinkPreviewPanelRemoval, ManualRegistrationDialogUsesImageViewControlInsteadOfImagePreviewPanel)
{
    const auto root = findRepoRoot();
    ASSERT_FALSE(root.empty()) << "Could not locate repository root";

    const std::string header = readFileText(root / "src" / "AfterNightProcesses" / "processes" / "ImageIntegration" /
                                            "dialogs" / "ManualRegistrationDialog.h");
    const std::string source = readFileText(root / "src" / "AfterNightProcesses" / "processes" / "ImageIntegration" /
                                            "dialogs" / "ManualRegistrationDialog.cpp");

    ASSERT_FALSE(header.empty()) << "Could not load ManualRegistrationDialog.h";
    ASSERT_FALSE(source.empty()) << "Could not load ManualRegistrationDialog.cpp";

    EXPECT_EQ(header.find("ImagePreviewPanel"), std::string::npos);
    EXPECT_EQ(source.find("ImagePreviewPanel"), std::string::npos);
    EXPECT_NE(source.find("#include \"controls/ImageViewControl.h\""), std::string::npos);
    EXPECT_NE(source.find("new ImageViewControl(parent)"), std::string::npos);
    EXPECT_NE(source.find("control->initialize(objectName ? objectName : \"\", ViewControlMode::st_preview);"),
              std::string::npos);
    EXPECT_EQ(source.find("createNewImageViewControlFromImage"), std::string::npos);
    EXPECT_EQ(source.find("getViewFactory()"), std::string::npos);
}

TEST(BlinkPreviewPanelRemoval, DeprecatedImagePreviewPanelSourcesAreRemoved)
{
    const auto root = findRepoRoot();
    ASSERT_FALSE(root.empty()) << "Could not locate repository root";

    EXPECT_FALSE(std::filesystem::exists(root / "src" / "AfterNightProcesses" / "processes" / "ImageIntegration" /
                                         "widgets" / "ImagePreviewPanel.h"));
    EXPECT_FALSE(std::filesystem::exists(root / "src" / "AfterNightProcesses" / "processes" / "ImageIntegration" /
                                         "widgets" / "ImagePreviewPanel.cpp"));
}

TEST(BlinkPreviewPanelRemoval, ProductionSourcesNoLongerUseDeprecatedPreviewEscapeHatch)
{
    const auto root = findRepoRoot();
    ASSERT_FALSE(root.empty()) << "Could not locate repository root";

    const auto sourceRoot = root / "src" / "AfterNightControls";
    ASSERT_TRUE(std::filesystem::exists(sourceRoot));

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const auto extension = entry.path().extension().string();
        if (extension != ".h" && extension != ".cpp")
        {
            continue;
        }

        const std::string text = readFileText(entry.path());
        ASSERT_FALSE(text.empty()) << "Could not read " << entry.path().string();

        EXPECT_EQ(text.find("AN_ALLOW_DEPRECATED_IMAGE_PREVIEW_PANEL"), std::string::npos) << entry.path().string();
        EXPECT_EQ(text.find("ImagePreviewPanel"), std::string::npos) << entry.path().string();
    }
}
