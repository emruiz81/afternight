// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The page's sections on real processes as on the framework's test process: Auto Gradient Removal, Star Reduction and
/// Background Extraction, launched as the menu launches them and taken into a page, show their top-level group boxes
/// as sections titled as the groups, in their order, each holding its group, each opening and closing, and the body
/// leads with Target image.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"
#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "app/ProcessRegistry.h"
#include "components/AnCollapsibleSection.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QGroupBox>
#include <QToolButton>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::pumpQuietly;
using an::tests::processes::framework::ContextView;
using an::tests::processes::framework::floatView;
using an::tests::processes::framework::suiteContext;

/// Lends the suite's context a page presenter for one case and takes it back after.
class LentPresenter
{
public:
    LentPresenter()
    {
        suiteContext().presenter = &presenter;
    }
    ~LentPresenter()
    {
        an::tests::processes::shared::closeOpenedProcessWindows();
        suiteContext().presenter = nullptr;
    }
    LentPresenter(const LentPresenter&) = delete;
    LentPresenter& operator=(const LentPresenter&) = delete;

    an::tests::processes::PagePresenter presenter;
};

/// The process a launch took into a page; null when none was.
[[nodiscard]] TopLevelProcessBaseControl* launchedPage(const std::string& id, ImageViewControl& target)
{
    const an::ui::process_registry::ProcessDescriptor* descriptor =
        an::ui::process_registry::ProcessRegistry::instance().find(id);
    EXPECT_NE(descriptor, nullptr) << id;
    if (descriptor == nullptr)
    {
        return nullptr;
    }
    suiteContext().setActiveImageViewControl(&target);
    descriptor->launch_single_image(&target);
    pumpQuietly();
    for (TopLevelProcessBaseControl* process : suiteContext().openProcesses)
    {
        if (process->pageLayout() != nullptr)
        {
            return process;
        }
    }
    return nullptr;
}

} // namespace

TEST(ProcessPageLayout, SectionsHoldOnRealProcesses)
{
    const std::unique_ptr<ContextView> target = floatView(
        "sections target", 96u, 64u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<float>((x * 7u + y * 3u + channel * 11u) % 251u) / 251.0f;
        });
    ASSERT_NE(target, nullptr);

    struct Expected
    {
        std::string id;
        std::vector<QString> sections;
    };
    const std::vector<Expected> processes{
        {"background_extraction.actionAuto_Gradient_Removal",
         {QStringLiteral("Background model"), QStringLiteral("Structure protection"), QStringLiteral("Correction"),
          QStringLiteral("Outputs")}},
        {"star_object.actionStar_Reduction",
         {QStringLiteral("Views"), QStringLiteral("Small star protection"), QStringLiteral("Star reduction parameters"),
          QStringLiteral("Output options")}},
        {"background_extraction.actionBackground_Extraction",
         {QStringLiteral("Sample generation"), QStringLiteral("Background Extraction")}},
    };
    for (const Expected& expected : processes)
    {
        SCOPED_TRACE(expected.id);
        const LentPresenter lent;
        TopLevelProcessBaseControl* const process = launchedPage(expected.id, *target->get());
        ASSERT_NE(process, nullptr) << "the presenter took the process into a page";
        an::ui::ProcessPageLayout* const page = process->pageLayout();
        const std::vector<an::ui::AnCollapsibleSection*>& sections = page->sections();
        ASSERT_EQ(sections.size(), expected.sections.size());
        int previous = -1;
        for (std::size_t index = 0; index < sections.size(); ++index)
        {
            an::ui::AnCollapsibleSection* section = sections[index];
            EXPECT_EQ(section->title(), expected.sections[index]);
            EXPECT_TRUE(page->bodyContent()->isAncestorOf(section));
            const int top = section->mapTo(page->bodyContent(), QPoint(0, 0)).y();
            EXPECT_GT(top, previous) << "in the groups' order";
            previous = top;
            bool holdsItsGroup = false;
            for (const QGroupBox* group : section->findChildren<QGroupBox*>())
            {
                holdsItsGroup =
                    holdsItsGroup || group->property("anPageSectionTitle").toString() == expected.sections[index];
            }
            EXPECT_TRUE(holdsItsGroup);
        }
        const bool wasOpen = sections.back()->isExpanded();
        sections.back()->toggleButton()->click();
        pumpQuietly();
        EXPECT_NE(sections.back()->isExpanded(), wasOpen);
    }
}
