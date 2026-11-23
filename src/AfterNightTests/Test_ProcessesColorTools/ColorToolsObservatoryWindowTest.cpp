// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour tools' windows in the Observatory look: each launched through its registry descriptor over a colour
/// view, at each density. No control lies outside the widget that holds it, no text is cut where the platform measures
/// text as the interface does, and grabbed with Night on the window holds no green or blue.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"
#include "Test_ProcessesShared/ObservatoryWindowCheck.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::channels;

} // namespace

TEST(ObservatoryWindows, ColorToolsWindowsKeepTheirTextAndShowNoHueAtNight)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = sceneView(context->engineSession(), "scene-rgb-float32");
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());

    std::vector<std::string> ids;
    for (const auto& process : an::tests::processes::colortools::colorTools())
    {
        ids.emplace_back(process.descriptorId);
    }
    ASSERT_FALSE(ids.empty());
    shared::expectObservatoryWindowsHold(ids, *target);

    dropView(*context, target);
}
