// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open())
    {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST(BlinkComparatorResultWiring, CallersConsumeComparisonResultsWithoutDialogAcceptance)
{
    const auto root = findRepoRoot();
    ASSERT_FALSE(root.empty());

    const std::string processSource =
        readFileText(root / "src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp");
    const std::string registrationSource =
        readFileText(root / "src/AfterNightProcesses/processes/ImageIntegration/tabs/RegistrationTabActions.cpp");
    const std::string dashboardSource =
        readFileText(root / "src/AfterNightProcesses/processes/ImageIntegration/widgets/FrameQualityDashboard.cpp");

    ASSERT_FALSE(processSource.empty());
    ASSERT_FALSE(registrationSource.empty());
    ASSERT_FALSE(dashboardSource.empty());

    EXPECT_NE(processSource.find("dialogPtr->comparisonResult()"), std::string::npos);
    EXPECT_NE(processSource.find("dialog->open()"), std::string::npos);
    EXPECT_EQ(processSource.find("Q_UNUSED(frameType);"), std::string::npos);

    const auto processBlinkPos =
        processSource.find("auto* dialog = new BlinkComparatorDialog(paths, labels, contexts, this);");
    ASSERT_NE(processBlinkPos, std::string::npos);
    const auto processBlinkEnd = processSource.find("dialog->open();", processBlinkPos);
    ASSERT_NE(processBlinkEnd, std::string::npos);
    const std::string processBlinkSource = processSource.substr(processBlinkPos, processBlinkEnd - processBlinkPos);
    EXPECT_EQ(processBlinkSource.find(".exec()"), std::string::npos);
    EXPECT_NE(processBlinkSource.find("dialog->setHistoryRows(historyRows)"), std::string::npos);

    // The registration tab and the quality dashboard open the comparator without waiting for it, and read its result
    // when it finishes - never through a dialog's acceptance.
    for (const std::string* source : {&registrationSource, &dashboardSource})
    {
        const auto blinkPos = source->find("auto* dialog = new BlinkComparatorDialog(paths, ");
        ASSERT_NE(blinkPos, std::string::npos);
        const auto blinkEnd = source->find("dialog->open();", blinkPos);
        ASSERT_NE(blinkEnd, std::string::npos);
        const std::string blinkSource = source->substr(blinkPos, blinkEnd - blinkPos);
        EXPECT_EQ(blinkSource.find(".exec()"), std::string::npos);
        EXPECT_NE(blinkSource.find("&QDialog::finished"), std::string::npos);
        EXPECT_NE(blinkSource.find("dialogPtr->comparisonResult()"), std::string::npos);
    }
}
