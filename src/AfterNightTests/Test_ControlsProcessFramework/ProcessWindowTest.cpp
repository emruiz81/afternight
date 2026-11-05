// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process window around its runs: the channel selector filled from the one table with each item carrying the scope
/// it runs on, a grey target offered its whole image alone, and a second real-time preview on a view turning off the
/// one a window already shows there.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::processes;

} // namespace

TEST(ProcessWindowTest, TheSelectorOffersEveryChoiceForAColourTargetEachCarryingItsScope)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context->engineSession(), 8u, 6u, Channels::Rgb,
        [](std::uint32_t, std::uint32_t, std::uint32_t channel) { return 0.1f * static_cast<float>(channel + 1u); });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("colour", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.perChannel = true});
    ASSERT_TRUE(settleProcess(*process));

    QComboBox& selector = *process->channelSelector;
    EXPECT_TRUE(selector.isEnabled());
    std::vector<an::ChannelScope> offered;
    for (int index = 0; index < selector.count(); ++index)
    {
        const QVariant scope = selector.itemData(index);
        if (scope.isValid())
        {
            offered.push_back(static_cast<an::ChannelScope>(scope.toInt()));
        }
    }
    const std::span<const an::ui::ProcessChannelChoice> choices = an::ui::processChannelChoices();
    ASSERT_EQ(offered.size(), choices.size());
    for (std::size_t index = 0u; index < choices.size(); ++index)
    {
        EXPECT_EQ(offered[index], choices[index].scope) << choices[index].label;
    }
    EXPECT_EQ(process->selectedChannelScope(), an::ChannelScope::Whole);

    // The scope a run takes is the one the item carries, whatever its label says.
    const int red = selector.findData(QVariant(static_cast<int>(an::ChannelScope::Red)));
    ASSERT_GE(red, 0);
    selector.setItemText(red, QStringLiteral("renamed"));
    selector.setCurrentIndex(red);
    EXPECT_EQ(process->selectedChannelScope(), an::ChannelScope::Red);
}

TEST(ProcessWindowTest, AGreyTargetIsOfferedItsWholeImageAlone)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("grey", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.perChannel = true});
    ASSERT_TRUE(settleProcess(*process));

    QComboBox& selector = *process->channelSelector;
    ASSERT_EQ(selector.count(), 1);
    EXPECT_EQ(selector.itemText(0), QString::fromLatin1(an::ui::kProcessGreyscaleChannelLabel));
    EXPECT_EQ(static_cast<an::ChannelScope>(selector.itemData(0).toInt()), an::ChannelScope::Whole);
    EXPECT_FALSE(selector.isEnabled());
    EXPECT_EQ(process->selectedChannelScope(), an::ChannelScope::Whole);
}

TEST(ProcessWindowTest, ASecondPreviewOnAViewTurnsOffTheOneAlreadyShownThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));

    ProcessHandle first = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*first));
    ASSERT_NE(first->previewView(), nullptr);

    ProcessHandle second(new TestProcess(target.get(), TestProcessOptions{.preview = true}, "Another Scale"));
    second->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(settleProcess(*second));
    ASSERT_TRUE(settleProcess(*first));

    ASSERT_NE(second->previewView(), nullptr);
    EXPECT_EQ(first->previewView(), nullptr) << "Only one window previews a view at a time";
    EXPECT_FALSE(first->previewCheckbox->isChecked());
    // The preview turned off released the session's preview reservation it held; the one shown now holds the only one.
    EXPECT_EQ(context->engineSession().outstanding().previewReservations, 1u);
}
