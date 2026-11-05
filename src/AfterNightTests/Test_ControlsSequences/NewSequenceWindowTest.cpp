// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The New Sequence window: files probed on a worker and listed in order, a duplicate passed over and an unreadable
/// file reported; a row previewed on a worker; an image list published naming the files where they are, and a SER
/// sequence holding every file demosaiced by the one demosaic setting, each opened in a new sequence view; a list whose
/// files differ refused before anything is written; and a run cancelled, or a window closed while it runs, leaving
/// nothing.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "controls/SequenceContainers.h"
#include "controls/ImageViewFiles.h"
#include "controls/NewSequenceControl.h"
#include "controls/ProgressDialogControl.h"
#include "controls/settings/SettingsKeys.h"
#include "widgets/TableWidget.h"

#include <AfterNightEngine/IO/CodecIO.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::sequences;

/// A window, deleted with the case once what it started has landed.
struct WindowDeleter
{
    void operator()(NewSequenceControl* window) const
    {
        if (window == nullptr)
        {
            return;
        }
        (void)pumpUntil([window]() { return window->isIdle(); });
        delete window;
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    }
};
using WindowHandle = std::unique_ptr<NewSequenceControl, WindowDeleter>;

/// The window, shown, without the delete-on-close it sets so the case decides when it goes.
[[nodiscard]] WindowHandle openWindow()
{
    WindowHandle window(new NewSequenceControl());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->show();
    return window;
}

/// Writes still FITS files of the same shape; frame `f`'s samples are `fixtureSample(f, ...)`.
[[nodiscard]] QStringList writeStills(SequencesTestContext& context, std::uint32_t count, std::uint32_t width,
                                      std::uint32_t height, an::CfaDescriptor cfa = an::CfaDescriptor{},
                                      const std::vector<an::CodecCard>& cards = {}, const QString& stem = "still",
                                      bool textured = false)
{
    QStringList paths;
    for (std::uint32_t frame = 0u; frame < count; ++frame)
    {
        const QString path = context.pathOf(QStringLiteral("%1-%2.fits").arg(stem).arg(frame));
        const auto texture = [frame](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return texturedSample(frame, x, y);
        };
        EXPECT_TRUE(writeFitsStill(
            context.engineSession(), path, frame, width, height, Channels::Grey, cfa, cards,
            textured ? std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)>(texture) : nullptr));
        paths.push_back(path);
    }
    return paths;
}

/// A FITS card.
[[nodiscard]] an::CodecCard card(const char* keyword, const char* value)
{
    an::Result<an::CodecCard> made = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, {});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// The paths a window lists, in order.
[[nodiscard]] QStringList listed(const NewSequenceControl& window)
{
    QStringList paths;
    for (const ImageListProcessHelpers::ImageListEntry& entry : window.entries())
    {
        paths.push_back(entry.path);
    }
    return paths;
}

/// Adds files and waits for their probes.
void addFiles(NewSequenceControl& window, const QStringList& paths)
{
    window.addItemsNonBlocking(paths);
    ASSERT_TRUE(pumpUntil([&window]() { return window.isIdle(); }));
}

/// The temporary sequences the context's directory holds.
[[nodiscard]] QStringList temporarySequences(const SequencesTestContext& context)
{
    return QDir(context.directory()).entryList({QStringLiteral("SEQ_SWAP*")}, QDir::Files | QDir::Dirs);
}

/// The progress dialog a window shows while it runs.
[[nodiscard]] ProgressDialogControl* progressOf(NewSequenceControl& window)
{
    for (QDialog* dialog : window.findChildren<QDialog*>())
    {
        if (auto* progress = dynamic_cast<ProgressDialogControl*>(dialog))
        {
            return progress;
        }
    }
    return nullptr;
}

} // namespace

// Files are probed on a worker and listed in the order asked, each row stating what its file does; a file already
// listed is passed over, and a removed file leaves the list.
TEST(NewSequenceWindowTest, AddedFilesAreProbedAndListedInOrder)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 3u, 8u, 6u, an::CfaDescriptor{},
                                           {card("GAIN", "100"), card("OFFSET", "10"), card("EXPTIME", "30")});
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    EXPECT_EQ(listed(*window), stills);
    auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    EXPECT_EQ(table->item(1, 0)->text(), stills[1]);
    EXPECT_EQ(table->item(1, 3)->text(), QStringLiteral("8x6"));
    EXPECT_EQ(table->item(1, 4)->text(), QStringLiteral("100"));
    EXPECT_EQ(table->item(1, 5)->text(), QStringLiteral("10"));
    EXPECT_EQ(table->item(1, 6)->text(), QStringLiteral("30"));
    EXPECT_EQ(table->item(1, 7)->text(), QStringLiteral("No"));
    EXPECT_EQ(table->item(1, 8)->text(), QStringLiteral("16 bit"));

    addFiles(*window, {stills[1], stills[0]});
    EXPECT_EQ(listed(*window), stills);

    window->removeItems({stills[1]});
    EXPECT_EQ(listed(*window), (QStringList{stills[0], stills[2]}));
    EXPECT_EQ(table->rowCount(), 2);
}

// The first file that cannot be read is reported and ends its batch; the files before it stay listed.
TEST(NewSequenceWindowTest, AnUnreadableFileIsReportedAndEndsItsBatch)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 2u, 8u, 6u);
    WindowHandle window = openWindow();
    addFiles(*window, {stills[0], context->pathOf(QStringLiteral("missing.fits")), stills[1]});
    EXPECT_EQ(listed(*window), QStringList{stills[0]});
    EXPECT_TRUE(context->messages.contains(QStringLiteral("error:Error loading image metadata")));
}

// The selected row is read and presented on a worker, and the preview and its histogram show that file.
TEST(NewSequenceWindowTest, TheSelectedRowIsPreviewed)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 3u, 8u, 6u);
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    ASSERT_TRUE(pumpUntil([&]() { return window->previewsShown() >= 1u && window->isIdle(); }));
    const FixtureSequence still{.width = 8u, .height = 6u};
    ASSERT_NE(window->previewImage(), nullptr);
    EXPECT_EQ(largestDifference(samplesOf(*window->previewImage()), fixtureSamples(0u, still, false)), 0.0);

    auto* table = window->findChild<QTableWidget*>(QStringLiteral("tableWidget"));
    ASSERT_NE(table, nullptr);
    const std::uint64_t before = window->previewsShown();
    table->selectRow(2);
    ASSERT_TRUE(pumpUntil([&]() { return window->previewsShown() > before && window->isIdle(); }));
    EXPECT_EQ(largestDifference(samplesOf(*window->previewImage()), fixtureSamples(2u, still, false)), 0.0);
}

// An image list is published naming the files where they are, at the frame rate asked, and opens in a new sequence view
// under the name asked; the window closes.
TEST(NewSequenceWindowTest, AnImageListNamesTheFilesAndOpensInANewSequenceView)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 3u, 8u, 6u);
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    window->findChild<QComboBox*>(QStringLiteral("sequenceTypeSelector"))->setCurrentText(QStringLiteral("Image List"));
    window->findChild<QDoubleSpinBox*>(QStringLiteral("fpsValue"))->setValue(2.5);
    window->findChild<QLineEdit*>(QStringLiteral("sequenceNameEdit"))->setText(QStringLiteral("M42 run"));
    window->findChild<QCheckBox*>(QStringLiteral("debayerCFA"))->setChecked(false);

    window->createSequence();
    ASSERT_TRUE(pumpUntil([&]() { return context->madeViews.size() == 1u && window->isIdle(); }));
    EXPECT_FALSE(window->isVisible());
    ImageViewControl* view = context->madeViews.front();
    const std::optional<an::ui::DocumentSequence> sequence = view->document().sequence();
    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(sequence->descriptor.format, an::SequenceFormat::ImageList);
    EXPECT_EQ(sequence->descriptor.frameCount, 3u);
    ASSERT_TRUE(sequence->descriptor.nominalFrameRate.has_value());
    EXPECT_DOUBLE_EQ(an::ui::framesPerSecondOf(*sequence->descriptor.nominalFrameRate), 2.5);
    EXPECT_EQ(view->document().names().displayName, QStringLiteral("M42 run"));
    EXPECT_FALSE(context->madeAutoDebayer.front());

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    const std::string manifestPath = sequence->sourcePath.toStdString();
    an::Result<an::ImageListManifest> manifest =
        an::readImageListManifest(scope->engine(), scope->execution(), manifestPath);
    ASSERT_TRUE(manifest.hasValue()) << manifest.status().message();
    scope.reset();
    ASSERT_EQ(manifest->framePaths.size(), 3u);
    for (int frame = 0; frame < 3; ++frame)
    {
        EXPECT_EQ(QString::fromStdString(manifest->framePaths[static_cast<std::size_t>(frame)]),
                  QFileInfo(stills[frame]).absoluteFilePath());
    }
    EXPECT_DOUBLE_EQ(manifest->framesPerSecond, 2.5);
    const std::optional<ReadBack> read =
        readBack(context->engineSession(), sequence->sourcePath, an::SequenceFormat::ImageList);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->samples.size(), 3u);
    EXPECT_EQ(
        largestDifference(read->samples[2], fixtureSamples(2u, FixtureSequence{.width = 8u, .height = 6u}, false)),
        0.0);
}

// A SER sequence holds every file, a mosaic demosaiced by the method the one demosaic setting names - not a method of
// its own - and opens in a sequence view that demosaics.
TEST(NewSequenceWindowTest, ASerHoldsEveryFileDemosaicedByTheOneSetting)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ASSERT_TRUE(context->upsert(QString::fromLatin1(SettingsKeys::RAW_DEMOSAIC_ALGORITHM), QStringLiteral("vng4")));
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    const QStringList stills = writeStills(*context, 3u, 32u, 24u, *rggb, {}, QStringLiteral("still"), true);
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    window->findChild<QComboBox*>(QStringLiteral("sequenceTypeSelector"))
        ->setCurrentText(QStringLiteral("SER Sequence"));
    window->findChild<QCheckBox*>(QStringLiteral("debayerCFA"))->setChecked(true);

    window->createSequence();
    ASSERT_TRUE(pumpUntil([&]() { return context->madeViews.size() == 1u && window->isIdle(); }));
    ImageViewControl* view = context->madeViews.front();
    EXPECT_TRUE(context->madeAutoDebayer.front());
    const std::optional<an::ui::DocumentSequence> sequence = view->document().sequence();
    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(sequence->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(sequence->demosaic, an::DemosaicMethod::Vng4);
    const std::optional<ReadBack> read =
        readBack(context->engineSession(), sequence->sourcePath, an::SequenceFormat::Ser);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->frames.size(), 3u);
    EXPECT_EQ(read->frames[1].toImageDescriptor()->channelSchema(), an::ChannelSchema::Rgb);

    // The second file demosaiced by VNG4 straight through the viewer's demosaic; RCD would differ.
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    const std::string still = stills[1].toStdString();
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(scope->engine(), scope->execution(), an::CodecReadRequest{.path = still});
    ASSERT_TRUE(decoded.hasValue());
    an::Result<an::consumer::AnyImage> mosaic = an::ui::importViewFrame(
        scope->engine(), scope->execution(), std::move(decoded->frame), std::move(decoded->metadata));
    ASSERT_TRUE(mosaic.hasValue());
    std::vector<std::vector<double>> expected;
    for (const an::DemosaicMethod method : {an::DemosaicMethod::Vng4, an::DemosaicMethod::Rcd})
    {
        an::Result<an::consumer::AnyImage> colour =
            an::ui::demosaicViewFrame(scope->engine(), scope->execution(), *mosaic, method);
        ASSERT_TRUE(colour.hasValue()) << colour.status().message();
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(context->engineSession(), std::move(*colour));
        ASSERT_TRUE(asset.hasValue());
        expected.push_back(samplesOf(*asset));
    }
    scope.reset();
    EXPECT_EQ(largestDifference(read->samples[1], expected[0]), 0.0);
    EXPECT_GT(largestDifference(read->samples[1], expected[1]), 0.0) << "the fixture cannot tell VNG4 from RCD";
}

// A list whose files differ in size or in depth is refused before anything is written, and no view is made.
TEST(NewSequenceWindowTest, AListWhoseFilesDifferIsRefusedBeforeAnythingIsWritten)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    QStringList stills = writeStills(*context, 2u, 8u, 6u);
    stills.append(writeStills(*context, 1u, 4u, 4u, an::CfaDescriptor{}, {}, QStringLiteral("small")));
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    window->createSequence();
    pumpQuietly();
    EXPECT_TRUE(context->madeViews.empty());
    ASSERT_FALSE(context->errorTexts.isEmpty());
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("different size")));
    EXPECT_TRUE(temporarySequences(*context).isEmpty());
    EXPECT_TRUE(window->isVisible());
}

// A run cancelled from its progress dialog publishes nothing and leaves the window open, ready to run again.
TEST(NewSequenceWindowTest, ACancelledRunLeavesNothingAndTheWindowOpen)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 3u, 8u, 6u);
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    window->findChild<QComboBox*>(QStringLiteral("sequenceTypeSelector"))
        ->setCurrentText(QStringLiteral("SER Sequence"));
    // The session's only thread is held, so the run waits for it.
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(held.has_value());
    window->createSequence();
    ProgressDialogControl* progress = progressOf(*window);
    ASSERT_NE(progress, nullptr);
    progress->requestCancel();
    held.reset();
    ASSERT_TRUE(pumpUntil([&]() { return window->isIdle(); }));
    pumpQuietly();
    EXPECT_TRUE(context->madeViews.empty());
    EXPECT_EQ(context->errorCount(), 0);
    EXPECT_TRUE(temporarySequences(*context).isEmpty());
    EXPECT_TRUE(window->isVisible());
    auto* buttons = window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_NE(buttons, nullptr);
    EXPECT_TRUE(buttons->button(QDialogButtonBox::Apply)->isEnabled());
}

// A window closed while its run waits stops the run and waits for none of it: nothing is published and no view made.
TEST(NewSequenceWindowTest, AWindowClosedWhileItRunsKeepsNothing)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 3u, 8u, 6u);
    WindowHandle window = openWindow();
    addFiles(*window, stills);
    window->findChild<QComboBox*>(QStringLiteral("sequenceTypeSelector"))->setCurrentText(QStringLiteral("Image List"));
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(held.has_value());
    window->createSequence();
    window->close();
    EXPECT_FALSE(window->isVisible());
    held.reset();
    ASSERT_TRUE(pumpUntil([&]() { return window->isIdle(); }));
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_TRUE(context->madeViews.empty());
    EXPECT_TRUE(temporarySequences(*context).isEmpty());
}

// A window closed while its files are being probed stops the probes and waits for none of them: nothing is listed and
// nothing is reported.
TEST(NewSequenceWindowTest, AWindowClosedWhileFilesAreProbedKeepsNothing)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const QStringList stills = writeStills(*context, 3u, 8u, 6u);
    WindowHandle window = openWindow();
    // The session's only thread is held, so the probes wait for it.
    std::optional<an::consumer::OperationScope> held = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(held.has_value());
    window->addItemsNonBlocking(stills);
    EXPECT_FALSE(window->isIdle());
    window->close();
    EXPECT_FALSE(window->isVisible());
    held.reset();
    ASSERT_TRUE(pumpUntil([&]() { return window->isIdle(); }));
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_TRUE(window->entries().empty());
    EXPECT_EQ(context->errorCount(), 0);
}

// The FITS frame container is offered and cannot be chosen: the engine writes none.
TEST(NewSequenceWindowTest, TheFitsChoiceIsShownAndCannotBeMade)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    WindowHandle window = openWindow();
    auto* kinds = window->findChild<QComboBox*>(QStringLiteral("sequenceTypeSelector"));
    ASSERT_NE(kinds, nullptr);
    ASSERT_EQ(kinds->count(), 3);
    EXPECT_EQ(kinds->itemText(2), QStringLiteral("FITS Sequence"));
    auto* model = qobject_cast<QStandardItemModel*>(kinds->model());
    ASSERT_NE(model, nullptr);
    EXPECT_FALSE(model->item(2)->flags().testFlag(Qt::ItemIsEnabled));
    EXPECT_TRUE(model->item(1)->flags().testFlag(Qt::ItemIsEnabled));
}
