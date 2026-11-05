// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image menu's four sequence actions the registry launches: Export opens the export window over the sequence the
/// active view shows, Convert to FITS refuses by name and changes nothing, and the SER and image-list conversions run
/// the non-interactive conversion on the view - each saying so when the view shows no sequence, or there is no view.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/FileCompanions.h"
#include "controls/ExportSequenceControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QDialog>
#include <QString>
#include <QTemporaryDir>

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

/// The refusal Convert to FITS gives a sequence: there is no FITS sequence container to convert to.
const QString kNoFitsSequence = QStringLiteral("Converting to a FITS sequence is not available. Convert to Image List "
                                               "writes every frame of the sequence as a FITS "
                                               "file.");

/// A view of a three-frame grey SER sequence written below a directory, made by the context's view factory as the
/// application makes one, and made the active view.
[[nodiscard]] ImageViewControl* activeSequenceView(ProcessTestContext& context, const QTemporaryDir& directory)
{
    const QString path = directory.filePath(QStringLiteral("frames.ser"));
    an::tests::viewer::writeSerFile(context.engineSession(), path, 8u, 6u, 3u,
                                    [](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
                                        return static_cast<float>(frame * 16u + x + y) / 64.0f;
                                    });
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context.engineSession(), path, an::SequenceFormat::Ser, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    ImageViewControl* view = context.createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, false, "frames", ViewControlMode::standard, nullptr, false, false);
    EXPECT_NE(view, nullptr);
    if (view == nullptr)
    {
        return nullptr;
    }
    EXPECT_TRUE(settleView(*view));
    context.addViewControl(view);
    context.setActiveImageViewControl(view);
    return view;
}

/// A view of a grey still image, made the active view.
[[nodiscard]] ViewHandle activeStillView(ProcessTestContext& context)
{
    std::optional<an::ui::ImageDocument> document =
        makeDocument<float>(context.engineSession(), 8u, 6u, Channels::Grey,
                            [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x * y); });
    EXPECT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("still", std::move(*document));
    context.addViewControl(view.get());
    context.setActiveImageViewControl(view.get());
    return view;
}

/// The export windows open over a view.
[[nodiscard]] int openExportWindows(const ImageViewControl& view)
{
    int open = 0;
    for (QDialog* dialog : view.findChildren<QDialog*>())
    {
        open += dynamic_cast<ExportSequenceControl*>(dialog) != nullptr && dialog->isVisible() ? 1 : 0;
    }
    return open;
}

/// Runs a registered image operation, as the image menu does.
void runImageOperation(const std::string& action)
{
    const auto* descriptor = registered("non_interactive." + action);
    ASSERT_NE(descriptor, nullptr) << action;
    ASSERT_TRUE(static_cast<bool>(descriptor->launch_non_interactive)) << action;
    descriptor->launch_non_interactive();
}

} // namespace

// Export opens the export window over the sequence the active view shows: the registry wires the dialog the image
// operations coordinator asks for.
TEST(ImageOperationsTest, ExportOpensTheExportWindowOverTheActiveSequence)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ImageViewControl* view = activeSequenceView(*context, directory);
    ASSERT_NE(view, nullptr);

    runImageOperation("actionExport");
    EXPECT_EQ(openExportWindows(*view), 1);
    EXPECT_TRUE(context->messages.isEmpty());
    for (QDialog* dialog : view->findChildren<QDialog*>())
    {
        if (dynamic_cast<ExportSequenceControl*>(dialog) != nullptr)
        {
            dialog->close();
        }
    }
    an::tests::processes::pumpQuietly();
}

// Export of a still image, or with no view, says there is no sequence to export and opens nothing.
TEST(ImageOperationsTest, ExportOfAStillImageOrOfNoViewSaysThereIsNoSequence)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle still = activeStillView(*context);

    runImageOperation("actionExport");
    EXPECT_EQ(openExportWindows(*still), 0);
    context->setActiveImageViewControl(nullptr);
    runImageOperation("actionExport");
    EXPECT_EQ(context->messages, (QStringList{QStringLiteral("error:Convert to AVI sequence"),
                                              QStringLiteral("error:Convert to AVI sequence")}));
    EXPECT_EQ(context->errorTexts, (QStringList{QStringLiteral("No selected sequence to process"),
                                                QStringLiteral("No selected sequence to process")}));
}

// Convert to FITS refuses a sequence by name and points at Convert to Image List: the sequence's document, its
// representation, its metadata and its history are as they were, no view is made and nothing is run.
TEST(ImageOperationsTest, ConvertToFitsIsRefusedByNameAndChangesNothing)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ImageViewControl* view = activeSequenceView(*context, directory);
    ASSERT_NE(view, nullptr);
    const std::size_t viewsBefore = context->madeViews.size();
    const an::ui::DocumentRevision revision = view->document().revision();
    const std::optional<an::ImageDescriptor> described = view->document().descriptor();
    ASSERT_NE(view->viewHistory(), nullptr);
    const bool hadUndo = view->viewHistory()->hasUndo();

    runImageOperation("actionConvert_to_FITS");
    an::tests::processes::pumpQuietly();
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:Convert to FITS sequence")});
    EXPECT_EQ(context->errorTexts, QStringList{kNoFitsSequence});
    EXPECT_EQ(view->document().revision(), revision);
    EXPECT_EQ(view->document().descriptor(), described);
    EXPECT_TRUE(view->document().sequence().has_value());
    EXPECT_EQ(view->viewHistory()->hasUndo(), hadUndo);
    EXPECT_EQ(context->madeViews.size(), viewsBefore);
}

// Convert to FITS of a still image, or with no view, says there is no sequence to convert.
TEST(ImageOperationsTest, ConvertToFitsOfAStillImageOrOfNoViewSaysThereIsNoSequence)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle still = activeStillView(*context);
    const an::ui::DocumentRevision revision = still->document().revision();

    runImageOperation("actionConvert_to_FITS");
    context->setActiveImageViewControl(nullptr);
    runImageOperation("actionConvert_to_FITS");
    EXPECT_EQ(context->messages, (QStringList{QStringLiteral("error:Convert to FITS sequence"),
                                              QStringLiteral("error:Convert to FITS sequence")}));
    EXPECT_EQ(context->errorTexts, (QStringList{QStringLiteral("No selected sequence to process"),
                                                QStringLiteral("No selected sequence to process")}));
    EXPECT_EQ(still->document().revision(), revision);
}

// The SER and image-list conversions run the non-interactive conversion on the active sequence's view, which then shows
// the sequence written in the container asked for; a sequence already in that container is refused by name.
TEST(ImageOperationsTest, TheSequenceConversionsRunTheNonInteractiveConversion)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ImageViewControl* view = activeSequenceView(*context, directory);
    ASSERT_NE(view, nullptr);

    const std::size_t viewsBefore = context->madeViews.size();
    runImageOperation("actionConvert_to_SER");
    an::tests::processes::pumpQuietly();
    EXPECT_EQ(context->madeViews.size(), viewsBefore);
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:Convert to SER sequence")});

    // The view shows the sequence written in the container asked for, from a file of its own, with every frame.
    context->messages.clear();
    const QString recording = view->document().sequence()->sourcePath;
    const std::optional<std::uint64_t> frames = view->document().sequence()->descriptor.frameCount;
    runImageOperation("actionConvert_to_Image_List");
    ASSERT_TRUE(an::tests::processes::pumpUntil([&] {
        return view->document().sequence().has_value() && view->document().sequence()->sourcePath != recording;
    }));
    an::tests::processes::pumpQuietly();
    EXPECT_TRUE(context->messages.isEmpty()) << context->messages.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(view->document().sequence()->descriptor.format, an::SequenceFormat::ImageList);
    EXPECT_EQ(view->document().sequence()->descriptor.frameCount, frames);
    EXPECT_EQ(context->madeViews.size(), viewsBefore);
}
