// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The two mask actions the registry launches through the launcher, which open no process of their own: Select Mask
/// opens the mask selector over the active view, and Remove Mask detaches the view's mask and changes nothing else -
/// not a sample, not the representation, not the metadata, not the history - on a still image or a sequence alike.
/// With no view each says there is no image.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/FileCompanions.h"
#include "controls/ImageMaskSelectorControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QApplication>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::InstalledContext;
using an::tests::processes::makeDocument;
using an::tests::processes::makeTargetView;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessTestContext;
using an::tests::processes::settleView;
using an::tests::processes::ViewHandle;
using an::tests::processes::shared::registered;
using an::tests::processes::shared::RegistrySnapshot;

/// A view of a grey Float32 image of eight by six.
[[nodiscard]] ViewHandle greyView(ProcessTestContext& context, const std::string& name, float scale)
{
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context.engineSession(), 8u, 6u, Channels::Grey,
        [scale](std::uint32_t x, std::uint32_t y, std::uint32_t) { return scale * static_cast<float>(x + y) / 16.0f; });
    EXPECT_TRUE(document.has_value());
    ViewHandle view = makeTargetView(name, std::move(*document));
    context.addViewControl(view.get());
    return view;
}

/// Runs a registered mask action on the active view, as the process menu does.
void runMaskAction(const std::string& action, ImageViewControl* active)
{
    const auto* descriptor = registered("non_interactive." + action);
    ASSERT_NE(descriptor, nullptr) << action;
    ASSERT_TRUE(static_cast<bool>(descriptor->launch_single_image)) << action;
    descriptor->launch_single_image(active);
}

/// The mask selectors open now.
[[nodiscard]] int openMaskSelectors()
{
    int open = 0;
    for (QWidget* window : QApplication::topLevelWidgets())
    {
        open += dynamic_cast<ImageMaskSelectorControl*>(window) != nullptr && window->isVisible() ? 1 : 0;
    }
    return open;
}

/// Closes every mask selector and lets the event loop delete it.
void closeMaskSelectors()
{
    for (QWidget* window : QApplication::topLevelWidgets())
    {
        if (auto* selector = dynamic_cast<ImageMaskSelectorControl*>(window))
        {
            selector->close();
            selector->deleteLater();
        }
    }
    an::tests::processes::pumpQuietly();
}

} // namespace

// Select Mask opens the mask selector over the active view.
TEST(MaskActionsTest, SelectMaskOpensTheMaskSelectorOverTheActiveView)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = greyView(*context, "target", 1.0f);
    ViewHandle mask = greyView(*context, "mask", 0.5f);
    context->setActiveImageViewControl(target.get());

    const int before = openMaskSelectors();
    runMaskAction("actionSelect_Mask", target.get());
    EXPECT_EQ(openMaskSelectors(), before + 1);
    EXPECT_TRUE(context->messages.isEmpty());
    closeMaskSelectors();
}

// Select Mask with no view says there is no image, and opens nothing.
TEST(MaskActionsTest, SelectMaskWithNoViewSaysThereIsNoImage)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;

    const int before = openMaskSelectors();
    runMaskAction("actionSelect_Mask", nullptr);
    EXPECT_EQ(openMaskSelectors(), before);
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:ImageMaskSelector")});
    EXPECT_EQ(context->errorTexts, QStringList{QStringLiteral("No selected image to process")});
}

// Remove Mask detaches the active view's mask and nothing else changes: the view's document is the revision it was -
// no sample, representation or metadata moved - its history has no new step, and the mask's own view is untouched.
TEST(MaskActionsTest, RemoveMaskDetachesTheMaskAndChangesNothingElse)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = greyView(*context, "target", 1.0f);
    ViewHandle mask = greyView(*context, "mask", 0.5f);
    context->setActiveImageViewControl(target.get());
    target->setCurrentMask(mask.get());
    ASSERT_TRUE(target->isMasked());
    ASSERT_TRUE(settleView(*target));
    const an::ui::DocumentRevision revision = target->document().revision();
    const an::ui::DocumentRevision maskRevision = mask->document().revision();
    const std::optional<an::ImageDescriptor> described = target->document().descriptor();
    ASSERT_NE(target->viewHistory(), nullptr);
    const bool hadUndo = target->viewHistory()->hasUndo();

    runMaskAction("actionRemove_Mask", target.get());
    ASSERT_TRUE(settleView(*target));
    EXPECT_FALSE(target->isMasked());
    EXPECT_EQ(target->getCurrentMask(), nullptr);
    EXPECT_EQ(target->document().revision(), revision);
    EXPECT_EQ(target->document().descriptor(), described);
    EXPECT_EQ(target->viewHistory()->hasUndo(), hadUndo);
    EXPECT_EQ(mask->document().revision(), maskRevision);
    EXPECT_TRUE(context->messages.isEmpty());
}

// Remove Mask on a sequence's view detaches its mask and leaves the sequence as it was.
TEST(MaskActionsTest, RemoveMaskOfASequenceDetachesItsMask)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("frames.ser"));
    an::tests::viewer::writeSerFile(context->engineSession(), path, 8u, 6u, 3u,
                                    [](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
                                        return static_cast<float>(frame * 16u + x + y) / 64.0f;
                                    });
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context->engineSession(), path, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(document.hasValue()) << document.status().message();
    ImageViewControl* sequence = context->createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, false, "frames", ViewControlMode::standard, nullptr, false, false);
    ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(settleView(*sequence));
    ViewHandle mask = greyView(*context, "mask", 0.5f);
    context->setActiveImageViewControl(sequence);
    sequence->setCurrentMask(mask.get());
    ASSERT_TRUE(sequence->isMasked());
    const an::ui::DocumentRevision revision = sequence->document().revision();

    runMaskAction("actionRemove_Mask", sequence);
    ASSERT_TRUE(settleView(*sequence));
    EXPECT_FALSE(sequence->isMasked());
    EXPECT_EQ(sequence->document().revision(), revision);
    EXPECT_TRUE(sequence->document().sequence().has_value());
    EXPECT_TRUE(context->messages.isEmpty());
}

// Remove Mask with no view says there is no image; on a view with no mask it does nothing and says nothing.
TEST(MaskActionsTest, RemoveMaskWithNoViewSaysThereIsNoImageAndOfAnUnmaskedViewDoesNothing)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;

    runMaskAction("actionRemove_Mask", nullptr);
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:Remove Mask")});
    EXPECT_EQ(context->errorTexts, QStringList{QStringLiteral("No selected image to process")});

    context->messages.clear();
    ViewHandle target = greyView(*context, "target", 1.0f);
    context->setActiveImageViewControl(target.get());
    const an::ui::DocumentRevision revision = target->document().revision();
    runMaskAction("actionRemove_Mask", target.get());
    EXPECT_FALSE(target->isMasked());
    EXPECT_EQ(target->document().revision(), revision);
    EXPECT_TRUE(context->messages.isEmpty());
}
