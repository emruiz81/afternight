// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The eight pointwise and tone processes' windows under the Observatory profile, as the developer switch shows them:
/// each launched through its registry descriptor over a colour view, in the 1.1 look and then in the Observatory one at
/// each density. No control lies outside the widget that holds it, no text is cut that the 1.1 window shows whole, and
/// grabbed with Night on the window holds no green or blue.

#include "Test_ProcessesPointwise/PointwiseTestSupport.h"
#include "Test_ProcessesShared/ObservatoryWindowCheck.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::pointwise;

} // namespace

TEST(ObservatoryWindows, PointwiseWindowsKeepTheirTextAndShowNoHueAtNight)
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
    for (const auto& process : pointwiseProcesses())
    {
        ids.emplace_back(process.descriptorId);
    }
    ASSERT_FALSE(ids.empty());
    shared::expectObservatoryWindowsHold(ids, *target);

    dropView(*context, target);
}
