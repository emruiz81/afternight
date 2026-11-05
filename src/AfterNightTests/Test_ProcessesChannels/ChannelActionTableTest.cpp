// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The one table every channel and conversion action runs through, held to the registry in both directions: every
/// registered channel or conversion action is one row and every row is registered, each row is reached by the channel
/// extractor's member and the non-interactive process of its name, and the rows state each action's planes, names and
/// targets.

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include "app/ChannelExtractor.h"
#include "app/ProcessRegistry.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using process_utils::ChannelAction;
using process_utils::ChannelActionKind;

/// The registered actions the registry sends to the channel extractor: the image menu's channel and conversion
/// actions, which are neither a process, an image operation nor a mask action.
[[nodiscard]] std::set<std::string> registeredChannelActions()
{
    std::set<std::string> actions;
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        const std::string action = descriptor.action_object_name;
        if (descriptor.category == "non_interactive" &&
            (action.rfind("actionExtract_", 0u) == 0u || action.rfind("actionConvert_to_", 0u) == 0u) &&
            action != "actionExtract_Frame" && action != "actionExtract_Sequence" && action != "actionConvert_to_SER" &&
            action != "actionConvert_to_Image_List" && action != "actionConvert_to_FITS")
        {
            actions.insert(action);
        }
    }
    return actions;
}

} // namespace

// The thirty-nine registered channel and conversion actions are the table's thirty-nine rows, and no row is
// registered twice or not at all.
TEST(ChannelActionTableTest, EveryRegisteredChannelActionIsOneRowAndEveryRowIsRegistered)
{
    std::set<std::string> rows;
    for (const ChannelAction& action : process_utils::channelActions())
    {
        EXPECT_TRUE(rows.insert(action.action).second) << action.action << " is two rows";
        EXPECT_EQ(process_utils::channelActionNamed(action.action), &action);
        EXPECT_EQ(process_utils::channelActionOfMember(action.member), &action);
    }
    EXPECT_EQ(rows.size(), 39u);
    EXPECT_EQ(registeredChannelActions(), rows);
    EXPECT_EQ(process_utils::channelActionNamed("actionExtract_Frame"), nullptr);
    EXPECT_EQ(process_utils::channelActionOfMember("duplicateImage"), nullptr);
}

// Thirty-two extractions - thirty-one of planes and the a*b* view - and seven conversions, one to each representation
// but the two eight-bit signed and unsigned the menu does not offer twice; every plane has its own name.
TEST(ChannelActionTableTest, EachRowStatesThePlanesItMakesTheirNamesAndWhatItConvertsTo)
{
    std::size_t extractions = 0u;
    std::size_t conversions = 0u;
    std::set<an::SampleType> targets;
    std::set<std::string> suffixes;
    for (const ChannelAction& action : process_utils::channelActions())
    {
        SCOPED_TRACE(action.action);
        switch (action.kind)
        {
        case ChannelActionKind::Extract:
            ++extractions;
            ASSERT_TRUE(action.outputCount == 1u || action.outputCount == 3u);
            EXPECT_EQ(process_utils::channelActionImageCount(action), action.outputCount);
            for (std::uint32_t index = 0u; index < action.outputCount; ++index)
            {
                EXPECT_NE(action.outputs[index].scope, an::ChannelScope::Whole);
                EXPECT_EQ(std::string(action.outputs[index].suffix).rfind(" - ", 0u), 0u);
                EXPECT_FALSE(std::string(action.outputs[index].label).empty());
                if (action.outputCount == 1u)
                {
                    EXPECT_TRUE(suffixes.insert(action.outputs[index].suffix).second);
                }
            }
            EXPECT_EQ(std::string(action.progressBefore).rfind("Extracting ", 0u), 0u);
            EXPECT_TRUE(process_utils::channelActionNeedsColour(action));
            break;
        case ChannelActionKind::ChromaView:
            ++extractions;
            EXPECT_EQ(action.outputs[0].scope, an::ChannelScope::CieA);
            EXPECT_EQ(action.outputs[1].scope, an::ChannelScope::CieB);
            EXPECT_EQ(process_utils::channelActionImageCount(action), 1u);
            EXPECT_STREQ(action.suffix, " - CIE a*=R / b*=G");
            EXPECT_TRUE(process_utils::channelActionNeedsColour(action));
            break;
        case ChannelActionKind::Convert:
            ++conversions;
            EXPECT_EQ(action.outputCount, 0u);
            EXPECT_EQ(process_utils::channelActionImageCount(action), 1u);
            EXPECT_TRUE(targets.insert(action.target).second);
            EXPECT_EQ(std::string(action.suffix).rfind(" - CONV_", 0u), 0u);
            EXPECT_STREQ(action.progressBefore, "Converting sequence ");
            EXPECT_FALSE(process_utils::channelActionNeedsColour(action));
            break;
        }
    }
    EXPECT_EQ(extractions, 32u);
    EXPECT_EQ(conversions, 7u);
    EXPECT_EQ(targets, (std::set<an::SampleType>{an::SampleType::UInt8, an::SampleType::Int16, an::SampleType::UInt16,
                                                 an::SampleType::Int32, an::SampleType::UInt32, an::SampleType::Float32,
                                                 an::SampleType::Float64}));
}

// Each three-plane row takes one encoding's components in that encoding's own order - L*C*h's lightness being
// L*a*b*'s - the order of the three views it opens.
TEST(ChannelActionTableTest, TheTriplesTakeEachEncodingsComponentsInItsOwnOrder)
{
    for (const ChannelAction& action : process_utils::channelActions())
    {
        if (action.kind != ChannelActionKind::Extract || action.outputCount != 3u)
        {
            continue;
        }
        SCOPED_TRACE(action.action);
        const an::ChannelSchema encoding = an::channelScopeEncoding(action.outputs[1].scope);
        for (std::uint32_t index = 0u; index < 3u; ++index)
        {
            const an::ChannelScope scope = action.outputs[index].scope;
            EXPECT_EQ(an::channelScopeComponent(scope), index);
            if (scope != an::ChannelScope::CieLightness)
            {
                EXPECT_EQ(an::channelScopeEncoding(scope), encoding);
            }
        }
    }
}

// With no active view, every registered channel or conversion action says there is nothing to extract from or to
// convert, and runs nothing.
TEST(ChannelActionTableTest, WithNoViewEveryActionSaysThereIsNothingToRunOn)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    context->setActiveImageViewControl(nullptr);
    for (const ChannelAction& action : process_utils::channelActions())
    {
        SCOPED_TRACE(action.action);
        const int before = static_cast<int>(context->messages.size());
        runRegisteredAction(action.action);
        ASSERT_EQ(static_cast<int>(context->messages.size()), before + 1);
        const bool conversion = action.kind == ChannelActionKind::Convert;
        EXPECT_EQ(context->messages.back(),
                  conversion ? QStringLiteral("error:Convert format") : QStringLiteral("error:Extract channel"));
        const bool plural = action.kind == ChannelActionKind::Extract && action.outputCount == 3u;
        EXPECT_EQ(context->errorTexts.back(),
                  conversion ? QStringLiteral("No selected image or sequence to convert")
                  : plural   ? QStringLiteral("No selected image or sequence to extract the channels from")
                             : QStringLiteral("No selected image or sequence to extract the channel from"));
    }
    EXPECT_TRUE(settleAll(*context));
    EXPECT_TRUE(context->madeViews.empty());
}
