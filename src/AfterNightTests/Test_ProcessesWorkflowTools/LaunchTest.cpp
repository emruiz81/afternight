// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The four standalone workflow tools' registered actions: each opens its own top-level window over no view, under its
/// name, through the registry's descriptor and the launcher's entry point.

#include "app/ApplicationStorage.h"
#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "processes/ImageCalibrationProcess.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/ImageRegistrationProcess.h"
#include "processes/ImageStackingProcess.h"

#include <app/ProcessRegistry.h>

#include <QDir>
#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace an::tests::processes::workflow
{
namespace
{

/// One tool: its descriptor, the class its entry point opens, its name and how its window is recognised.
struct Tool
{
    std::string descriptorId;
    std::string action;
    std::string className;
    std::string processName;
    std::function<bool(TopLevelProcessBaseControl*)> isTool;
};

template <typename WindowT>
[[nodiscard]] Tool toolOf(std::string descriptorId, std::string action, std::string className)
{
    return Tool{.descriptorId = std::move(descriptorId),
                .action = std::move(action),
                .className = std::move(className),
                .processName = WindowT::getProcessNameStatic(),
                .isTool = [](TopLevelProcessBaseControl* window) { return dynamic_cast<WindowT*>(window) != nullptr; }};
}

[[nodiscard]] std::vector<Tool> tools()
{
    return {
        toolOf<ImageCalibrationProcess>("sensor_calibration.actionImage_Calibration", "actionImage_Calibration",
                                        "ImageCalibrationProcess"),
        toolOf<ImageStackingProcess>("multi_image_stacking.actionImage_Stacking", "actionImage_Stacking",
                                     "ImageStackingProcess"),
        toolOf<ImageRegistrationProcess>("multi_image_stacking.actionImage_Registration", "actionImage_Registration",
                                         "ImageRegistrationProcess"),
        toolOf<ImageListAnalyzerProcess>("multi_image_stacking.actionImage_List_Analyzer", "actionImage_List_Analyzer",
                                         "ImageListAnalyzerProcess"),
    };
}

class LaunchTest : public WorkflowCase, public ::testing::WithParamInterface<std::size_t>
{
};

TEST_P(LaunchTest, TheRegisteredActionOpensTheToolsWindow)
{
    const Tool tool = tools()[GetParam()];
    const shared::RegistrySnapshot snapshot;
    const an::ui::process_registry::ProcessDescriptor* descriptor = shared::registered(tool.descriptorId);
    ASSERT_NE(descriptor, nullptr) << tool.descriptorId;
    EXPECT_EQ(descriptor->action_object_name, tool.action);
    EXPECT_EQ(descriptor->launch_mode, an::ui::process_registry::ProcessLaunchMode::TopLevel);
    ASSERT_TRUE(descriptor->launch_top_level);
    descriptor->launch_top_level();
    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, tool.className);
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    TopLevelProcessBaseControl* window = context().openProcesses.front();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->isVisible());
    EXPECT_TRUE(tool.isTool(window));
    EXPECT_EQ(descriptor->process_name, tool.processName);
    window->close();
    for (int round = 0; round < 10; ++round)
    {
        QCoreApplication::processEvents();
    }
    EXPECT_TRUE(context().openProcesses.empty());
}

// The windows these tools open - and the integration's dialogs they reach - keep their settings in the application's
// settings file, under the suite's storage root on every platform, never in the native store.
TEST(WorkflowToolsSettingsTest, TheWindowsSettingsLiveUnderTheSuitesStorageRoot)
{
    const QString root = QDir::cleanPath(QString::fromLocal8Bit(qgetenv("AFTERNIGHT_TEST_STORAGE_ROOT")));
    ASSERT_FALSE(root.isEmpty());
    const QSettings settings = an::ui::applicationSettings();
    EXPECT_EQ(settings.format(), QSettings::IniFormat);
    EXPECT_TRUE(QDir::cleanPath(settings.fileName()).startsWith(root + QLatin1Char('/')))
        << settings.fileName().toStdString();
}

INSTANTIATE_TEST_SUITE_P(WorkflowTools, LaunchTest, ::testing::Range<std::size_t>(0u, 4u),
                         [](const ::testing::TestParamInfo<std::size_t>& info) {
                             std::string name = tools()[info.param].className;
                             return name;
                         });

} // namespace
} // namespace an::tests::processes::workflow
