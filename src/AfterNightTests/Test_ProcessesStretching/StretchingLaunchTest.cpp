// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The four stretching dialogs as the registry launches them: each action's descriptor states the name its process's
/// class attaches to a view under and whether it needs the view to itself, as the class says of itself; and its launch
/// reaches the member that opens that class on the active view, whose window - the suite has the shared stand-in open
/// it - attaches to the view under that name and leaves it when it closes.

#include "Test_ProcessesStretching/StretchingTestSupport.h"

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ProcessRegistry.h"
#include "processes/CurvesTransformationProcess.h"
#include "processes/GHTransformationProcess.h"
#include "processes/HistogramTransformationProcess.h"
#include "processes/StatisticalStretch.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes::stretching;
using an::tests::processes::shared::openedProcessWindows;
using an::tests::processes::shared::RegistrySnapshot;

/// One dialog as the registry and its class state it.
struct Launched
{
    const char* action;
    const char* opens;
    std::string processName;
    bool exclusive;
};

template <typename ProcessT> [[nodiscard]] Launched launchedAs(const char* action, const char* opens)
{
    return Launched{.action = action,
                    .opens = opens,
                    .processName = ProcessT::getProcessNameStatic(),
                    .exclusive = ProcessT::requiresExclusiveAccessStatic()};
}

[[nodiscard]] std::vector<Launched> launchedDialogs()
{
    return {
        launchedAs<HistogramTransformationProcess>("actionHistogram_Transformation", "HistogramTransformationProcess"),
        launchedAs<GHTransformationProcess>("actionGeneralized_Hyperbolic_Transformation", "GHTransformationProcess"),
        launchedAs<CurvesTransformationProcess>("actionCurves_Transformation", "CurvesTransformationProcess"),
        launchedAs<StatisticalStretch>("actionStatistical_Stretch", "StatisticalStretch")};
}

[[nodiscard]] const an::ui::process_registry::ProcessDescriptor*
descriptorActing(const std::vector<an::ui::process_registry::ProcessDescriptor>& descriptors, const char* action)
{
    for (const an::ui::process_registry::ProcessDescriptor& descriptor : descriptors)
    {
        if (descriptor.action_object_name == action)
        {
            return &descriptor;
        }
    }
    return nullptr;
}

} // namespace

TEST(StretchingLaunchTest, EachDescriptorStatesWhatItsClassSaysOfItselfAndOpensThatClass)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = fixtureView(context->engineSession(), "scene-rgb-float32");
    ASSERT_NE(view, nullptr);
    context->addViewControl(view.get());
    context->setActiveImageViewControl(view.get());
    const std::vector<an::ui::process_registry::ProcessDescriptor> descriptors =
        an::ui::process_registry::ProcessRegistry::instance().all();
    for (const Launched& dialog : launchedDialogs())
    {
        SCOPED_TRACE(dialog.action);
        const an::ui::process_registry::ProcessDescriptor* descriptor = descriptorActing(descriptors, dialog.action);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->launch_mode, an::ui::process_registry::ProcessLaunchMode::SingleImage);
        EXPECT_EQ(descriptor->process_name, dialog.processName);
        EXPECT_EQ(descriptor->requires_exclusive_access, dialog.exclusive);
        EXPECT_FALSE(descriptor->requires_still_image) << "a stretch runs over every frame of a sequence";
        ASSERT_TRUE(static_cast<bool>(descriptor->launch_single_image));

        descriptor->launch_single_image(view.get());
        ASSERT_EQ(openedProcessWindows().size(), 1u);
        EXPECT_EQ(openedProcessWindows()[0].className, dialog.opens);
        EXPECT_EQ(openedProcessWindows()[0].view, view.get());

        // The window the member opened: the class's, attached under the descriptor's name.
        ASSERT_EQ(context->openProcesses.size(), 1u) << "the launch opens one window";
        auto* window = dynamic_cast<ImageRTPreviewControl*>(context->openProcesses.front());
        ASSERT_NE(window, nullptr);
        ASSERT_TRUE(settleWindow(*window));
        EXPECT_TRUE(window->isVisible());
        EXPECT_EQ(window->getProcessName(), descriptor->process_name);
        EXPECT_TRUE(view->hasProcessAttached(descriptor->process_name));
        EXPECT_EQ(view->hasProcessWithExclusiveAccessAttached(), descriptor->requires_exclusive_access);
        EXPECT_EQ(context->errorCount(), 0);
        window->close();
        ASSERT_TRUE(pumpUntil([&context]() { return context->openProcesses.empty(); }));
        EXPECT_FALSE(view->hasProcessAttached(descriptor->process_name));
    }
}
