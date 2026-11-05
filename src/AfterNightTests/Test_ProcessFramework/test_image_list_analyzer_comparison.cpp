// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image List Analyzer over files it lists: its preview is a view inside the window, the player moving to a frame
/// selects that frame's row, the preview made again after a removal plays the proxies of the files left, an
/// optimization that cannot make every proxy keeps the ones it made and says how many it could not, the list is out of
/// reach while the proxies are made, Export Sequence states the rate the window plays at, and each proxy is named for
/// its file and the folder it lives in.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "controls/ExportSequenceControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/WorkflowToolFiles.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::framework;

/// An analyzer window, deleted with the case once nothing it started is on its way.
struct AnalyzerWindow
{
    AnalyzerWindow() : window(new ImageListAnalyzerProcess())
    {
        window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    ~AnalyzerWindow()
    {
        (void)pumpUntil([this]() { return window->isIdle(); });
        delete window;
        QCoreApplication::processEvents();
    }
    AnalyzerWindow(const AnalyzerWindow&) = delete;
    AnalyzerWindow& operator=(const AnalyzerWindow&) = delete;

    ImageListAnalyzerProcess* window;
};

/// Writes `count` sixteen-bit grey frames, each its own gradient, into a directory; fails the case when one cannot be
/// written.
[[nodiscard]] QStringList writeFrames(const QTemporaryDir& directory, int count)
{
    QStringList paths;
    for (int index = 0; index < count; ++index)
    {
        fixtures::FixtureImage image;
        image.name = "frame" + std::to_string(index);
        image.width = 48u;
        image.height = 32u;
        image.sampleType = fixtures::FixtureSampleType::UInt16;
        image.pattern = fixtures::FixturePattern::Gradient;
        image.levels = {
            fixtures::FixtureChannelLevels{.base = 1000.0 + 400.0 * index, .perColumn = 200.0, .perRow = 100.0}};
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        if (!samples.has_value())
        {
            return {};
        }
        const QString path = directory.filePath(QStringLiteral("frame_%1.fits").arg(index));
        EXPECT_TRUE(fixtures::writeFixtureFits(image, *samples, path.toStdWString(), error)) << error;
        paths << path;
    }
    return paths;
}

/// Lists files and waits until every one is probed and nothing the window started is on its way.
[[nodiscard]] bool listed(AnalyzerWindow& analyzer, const QStringList& paths)
{
    analyzer.window->addItemsNonBlocking(paths);
    return pumpUntil([&analyzer, &paths]() {
        return analyzer.window->isIdle() && analyzer.window->fileList().paths().size() == paths.size() &&
               !analyzer.window->previewSequencePath().isEmpty();
    });
}

/// The text of the image-list manifest the preview plays.
[[nodiscard]] QString manifestText(const ImageListAnalyzerProcess& window)
{
    QFile manifest(window.previewSequencePath());
    return manifest.open(QIODevice::ReadOnly) ? QString::fromUtf8(manifest.readAll()) : QString();
}

// The preview is a view the window holds inside its own preview area, made beside nothing the application makes: no
// view is asked of the view factory, and the preview plays the files listed.
TEST(ImageListAnalyzerImportContract, ThePreviewIsAViewInsideTheWindowAndNoneTheApplicationMakes)
{
    ProcessTestContext& context = suiteContext();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QStringList frames = writeFrames(directory, 2);
    ASSERT_EQ(frames.size(), 2);
    const std::size_t viewsBefore = context.madeViews.size();

    AnalyzerWindow analyzer;
    ASSERT_TRUE(listed(analyzer, frames));
    ImageViewControl* preview = analyzer.window->previewView();
    ASSERT_NE(preview, nullptr);
    EXPECT_TRUE(analyzer.window->ui().imagePreviewWidget->isAncestorOf(preview));
    EXPECT_EQ(context.madeViews.size(), viewsBefore) << "the preview was asked of the application's view factory";
    ASSERT_TRUE(pumpUntil([preview]() { return preview->isWorkIdle() && preview->hasImage(); }));
}

// When the player moves to a frame the table follows: the frame's row becomes the selected one, as a person who
// selected it would see.
TEST(ImageListAnalyzerImportContract, ThePlayerMovingToAFrameSelectsItsRow)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QStringList frames = writeFrames(directory, 3);
    ASSERT_EQ(frames.size(), 3);
    AnalyzerWindow analyzer;
    ASSERT_TRUE(listed(analyzer, frames));
    ImageViewControl* preview = analyzer.window->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(pumpUntil([preview]() { return preview->isWorkIdle() && preview->sequencePlayer() != nullptr; }));

    bool shown = false;
    preview->sequencePlayer()->setCurrentFrame(2u, [&shown]() { shown = true; });
    ASSERT_TRUE(pumpUntil([&shown, &analyzer]() { return shown && analyzer.window->isIdle(); }));
    pumpQuietly();
    EXPECT_EQ(analyzer.window->fileList().selectedRow(), 2);
}

// Once the proxies are made, the preview made again after a file is removed plays the proxies of the files left -
// never the original files, and never the removed file's proxy.
TEST(ImageListAnalyzerImportContract, ThePreviewMadeAgainAfterARemovalPlaysTheProxiesLeft)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QStringList frames = writeFrames(directory, 3);
    ASSERT_EQ(frames.size(), 3);
    AnalyzerWindow analyzer;
    ASSERT_TRUE(listed(analyzer, frames));
    analyzer.window->optimize();
    ASSERT_TRUE(pumpUntil([&analyzer]() { return analyzer.window->isIdle() && analyzer.window->imagesOptimized(); }));
    const QString removedProxy = analyzer.window->proxyPathOf(frames[1]);
    ASSERT_FALSE(removedProxy.isEmpty());

    const QString before = analyzer.window->previewSequencePath();
    analyzer.window->removeItems({frames[1]});
    ASSERT_TRUE(pumpUntil([&analyzer, &before]() {
        return analyzer.window->isIdle() && !analyzer.window->previewSequencePath().isEmpty() &&
               analyzer.window->previewSequencePath() != before;
    }));
    const QString text = manifestText(*analyzer.window);
    for (const QString& kept : {frames[0], frames[2]})
    {
        const QString proxy = analyzer.window->proxyPathOf(kept);
        ASSERT_FALSE(proxy.isEmpty());
        EXPECT_TRUE(text.contains(QFileInfo(proxy).fileName())) << proxy.toStdString();
    }
    EXPECT_FALSE(text.contains(QFileInfo(removedProxy).fileName()));
}

// While the proxies are made the list is out of reach - the table, adding and removing - and once they are, an
// optimization that could not make every proxy keeps the ones it made, says how many it could not under its own
// title, and gives the list back.
TEST(ImageListAnalyzerImportContract, AnOptimizationThatCannotMakeEveryProxyKeepsTheRestAndSaysSo)
{
    ProcessTestContext& context = suiteContext();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QStringList frames = writeFrames(directory, 2);
    ASSERT_EQ(frames.size(), 2);
    AnalyzerWindow analyzer;
    ASSERT_TRUE(listed(analyzer, frames));
    ASSERT_TRUE(QFile::remove(frames[1]));
    const auto messages = context.messages.size();
    const auto errors = context.errorTexts.size();

    analyzer.window->optimize();
    EXPECT_FALSE(analyzer.window->ui().tableWidget->isEnabled()) << "the list can be changed while proxies are made";
    EXPECT_FALSE(analyzer.window->ui().addFiles->isEnabled());
    ASSERT_TRUE(pumpUntil([&analyzer]() { return analyzer.window->isIdle(); }));
    pumpQuietly();

    EXPECT_TRUE(analyzer.window->ui().tableWidget->isEnabled());
    EXPECT_TRUE(analyzer.window->ui().addFiles->isEnabled());
    EXPECT_FALSE(analyzer.window->proxyPathOf(frames[0]).isEmpty()) << "the proxy that could be made was not kept";
    EXPECT_TRUE(QFileInfo::exists(analyzer.window->proxyPathOf(frames[0])));
    EXPECT_TRUE(analyzer.window->proxyPathOf(frames[1]).isEmpty());
    EXPECT_FALSE(analyzer.window->imagesOptimized());
    const QStringList said = context.messages.mid(messages);
    EXPECT_EQ(said.count(QStringLiteral("error:Image optimization incomplete")), 1) << said.join(';').toStdString();
    const QStringList texts = context.errorTexts.mid(errors);
    EXPECT_EQ(texts.filter(QRegularExpression(QStringLiteral("^1 image\\(s\\) could not be optimized"))).size(), 1)
        << texts.join(';').toStdString();
}

// Export Sequence opens over the files listed at the rate the window plays them.
TEST(ImageListAnalyzerImportContract, ExportSequenceStatesTheRateTheWindowPlaysAt)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QStringList frames = writeFrames(directory, 2);
    ASSERT_EQ(frames.size(), 2);
    AnalyzerWindow analyzer;
    analyzer.window->deserialize_json_settings(R"({"version":1,"params":{"fps":12.0}})");
    ASSERT_TRUE(listed(analyzer, frames));
    analyzer.window->exportSequence();
    QPointer<ExportSequenceControl> opened;
    ASSERT_TRUE(pumpUntil([&opened]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* control = dynamic_cast<ExportSequenceControl*>(widget);
                control != nullptr && control->isVisible())
            {
                opened = control;
                return true;
            }
        }
        return false;
    }));
    EXPECT_DOUBLE_EQ(opened->buildExportRequest().fps, 12.0);
    opened->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

// A proxy is named for its file and the folder it lives in: two files of one name in two folders have proxies of two
// names, each starting with the file's own base name and written as FITS.
TEST(ImageListAnalyzerImportContract, OptimizationProxyNamesIncludeSourcePathHash)
{
    const QString first = ImageListProcessHelpers::optimizedProxyFileName(QStringLiteral("C:/one/light_001.cr2"));
    const QString second = ImageListProcessHelpers::optimizedProxyFileName(QStringLiteral("C:/two/light_001.cr2"));

    EXPECT_NE(first, second);
    EXPECT_TRUE(first.startsWith(QStringLiteral("light_001_")));
    EXPECT_TRUE(first.endsWith(QStringLiteral(".fits")));
    EXPECT_EQ(first, ImageListProcessHelpers::optimizedProxyFileName(QStringLiteral("C:/one/light_001.cr2")))
        << "a file's proxy name is the same every time it is asked for";
}

} // namespace
