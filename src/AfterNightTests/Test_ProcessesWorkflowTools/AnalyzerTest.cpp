// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image List Analyzer: the files listed play as an image-list sequence, Optimize For Fast Play writes each file's
/// proxy as it is shown - stretched, in eight bits - and the preview then plays the proxies in the autostretch display
/// they hold, and the original files in any other, a removed file's proxy goes with it, Export Sequence opens over the
/// original files, the playback rate is what the window keeps, and a window closed while it works waits for nothing.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "controls/ExportSequenceControl.h"
#include "controls/ImageViewControl.h"
#include "controls/SequencePlayerControl.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/WorkflowToolFiles.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QThread>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>

namespace an::tests::processes::workflow
{
namespace
{

using an::tests::processes::stars::makeStarField;

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

/// The preview's display mode selector, or null.
[[nodiscard]] QComboBox* displayModeOf(const AnalyzerWindow& window)
{
    for (QComboBox* combo : window.window->previewView()->findChildren<QComboBox*>())
    {
        if (combo->accessibleName() == QStringLiteral("Display Mode"))
        {
            return combo;
        }
    }
    return nullptr;
}

/// The preview player's Play action, or null.
[[nodiscard]] QAction* playActionOf(const AnalyzerWindow& window)
{
    auto* player = dynamic_cast<SequencePlayerControl*>(window.window->previewView()->sequencePlayer());
    if (player == nullptr)
    {
        return nullptr;
    }
    for (QAction* action : player->actions())
    {
        if (action->text() == QStringLiteral("Play sequence"))
        {
            return action;
        }
    }
    return nullptr;
}

/// Whether the image list the preview plays names every one of these files.
[[nodiscard]] bool previewPlays(const AnalyzerWindow& window, const QStringList& paths)
{
    QFile manifest(window.window->previewSequencePath());
    if (paths.isEmpty() || !manifest.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QString text = QString::fromUtf8(manifest.readAll());
    return std::all_of(paths.cbegin(), paths.cend(),
                       [&text](const QString& path) { return text.contains(QFileInfo(path).fileName()); });
}

/// Whether nothing the window or its preview started is on its way: no file, sequence, frame or presentation.
[[nodiscard]] bool settled(const AnalyzerWindow& window)
{
    return window.window->isIdle() && window.window->previewView()->isWorkIdle();
}

/// The representation of the frame the preview shows, or nothing.
[[nodiscard]] std::optional<an::SampleType> shownRepresentation(const AnalyzerWindow& window)
{
    const std::optional<an::ImageDescriptor> shown = window.window->previewView()->imageDescriptor();
    return shown.has_value() ? std::optional<an::SampleType>(shown->sampleType()) : std::nullopt;
}

/// The median of the bytes the preview paints, over its first colour, or nothing before a presentation is painted.
[[nodiscard]] std::optional<int> paintedMedian(const AnalyzerWindow& window)
{
    const std::shared_ptr<const an::ui::PresentedImage> painted =
        window.window->previewView()->getViewWidget()->presentedImage();
    if (painted == nullptr || painted->width() == 0u || painted->height() == 0u)
    {
        return std::nullopt;
    }
    std::vector<int> bytes;
    bytes.reserve(static_cast<std::size_t>(painted->width()) * painted->height());
    for (std::uint32_t y = 0u; y < painted->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < painted->width(); ++x)
        {
            bytes.push_back(painted->pixel(x, y)[0]);
        }
    }
    const auto middle = bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2u);
    std::nth_element(bytes.begin(), middle, bytes.end());
    return *middle;
}

/// The median of a file's samples, as fractions of full scale, or nothing when it cannot be read.
[[nodiscard]] std::optional<double> fileMedian(const an::consumer::EngineSession& session, const QString& path)
{
    const std::optional<ReadBack> read = readBack(session, path);
    if (!read.has_value() || read->fractions.empty())
    {
        return std::nullopt;
    }
    std::vector<double> sorted = read->fractions;
    const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2u);
    std::nth_element(sorted.begin(), middle, sorted.end());
    return *middle;
}

/// When each file was last written.
[[nodiscard]] std::vector<QDateTime> writtenAt(const QStringList& paths)
{
    std::vector<QDateTime> times;
    for (const QString& path : paths)
    {
        times.push_back(QFileInfo(path).lastModified());
    }
    return times;
}

class AnalyzerTest : public WorkflowCase
{
protected:
    /// Three star fields of a faint linear sky, as a camera writes them.
    [[nodiscard]] QStringList writeFields()
    {
        const StarField field = makeStarField(160u, 120u);
        QStringList paths;
        for (int index = 0; index < 3; ++index)
        {
            const QString path = temporaryPath(QStringLiteral("field_%1.fits").arg(index));
            EXPECT_TRUE(writeShiftedField(session(), path, field, index * 2.0, index * 1.0, false));
            paths << path;
        }
        return paths;
    }

    static void listed(AnalyzerWindow& window, const QStringList& paths)
    {
        window.window->addItemsNonBlocking(paths);
        ASSERT_TRUE(pumpUntil([&window, &paths]() {
            return window.window->isIdle() && window.window->fileList().paths().size() == paths.size();
        }));
    }

    /// Optimizes the listed files and waits until the preview plays their proxies, which it returns.
    static QStringList optimized(AnalyzerWindow& window, const QStringList& paths)
    {
        window.window->optimize();
        EXPECT_TRUE(pumpUntil([&window]() { return window.window->isIdle() && window.window->imagesOptimized(); }));
        QStringList proxies;
        for (const QString& path : paths)
        {
            proxies << window.window->proxyPathOf(path);
        }
        EXPECT_TRUE(pumpUntil([&window, &proxies]() { return settled(window) && previewPlays(window, proxies); }));
        return proxies;
    }
};

TEST_F(AnalyzerTest, TheListedFilesPlayAsAnImageListSequence)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields);
    ASSERT_TRUE(pumpUntil([&window]() { return !window.window->previewSequencePath().isEmpty(); }));
    QFile manifest(window.window->previewSequencePath());
    ASSERT_TRUE(manifest.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(manifest.readAll());
    for (const QString& field : fields)
    {
        EXPECT_TRUE(text.contains(QFileInfo(field).fileName())) << field.toStdString();
    }
    EXPECT_FALSE(window.window->imagesOptimized());
}

TEST_F(AnalyzerTest, APreviewAskedForBeforeTheLastIsDroppedWhenItLands)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields);
    ASSERT_TRUE(
        pumpUntil([&window]() { return window.window->isIdle() && !window.window->previewSequencePath().isEmpty(); }));
    const QString shown = window.window->previewSequencePath();
    const QDir directory = QFileInfo(shown).dir();

    // The older request is made and finished, its answer left waiting for the event loop.
    window.window->removeItems({fields[2]});
    const auto olderFile = [&directory, &shown]() {
        for (const QFileInfo& file : directory.entryInfoList(QDir::Files))
        {
            if (file.absoluteFilePath() != QFileInfo(shown).absoluteFilePath())
            {
                return file.absoluteFilePath();
            }
        }
        return QString();
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline &&
           (olderFile().isEmpty() || QThreadPool::globalInstance()->activeThreadCount() != 0))
    {
        QThread::msleep(2);
    }
    const QString older = olderFile();
    ASSERT_FALSE(older.isEmpty());
    ASSERT_EQ(QThreadPool::globalInstance()->activeThreadCount(), 0);

    // The newer request waits for the session, which the case holds, so the older answer lands first.
    std::optional<an::Result<an::consumer::OperationScope>> held;
    held.emplace(session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch}));
    ASSERT_TRUE(held->hasValue());
    window.window->removeItems({fields[1]});
    QThread::msleep(100);
    const QStringList before = directory.entryList(QDir::Files);
    EXPECT_EQ(before.size(), 2) << "the newer request was not held";

    const auto landed = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < landed && QFileInfo::exists(older))
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    // The older answer was dropped, and what is shown is still what was shown before.
    EXPECT_FALSE(QFileInfo::exists(older));
    EXPECT_EQ(window.window->previewSequencePath(), shown);
    held.reset();

    ASSERT_TRUE(pumpUntil(
        [&window, &shown]() { return window.window->isIdle() && window.window->previewSequencePath() != shown; }));
    QFile manifest(window.window->previewSequencePath());
    ASSERT_TRUE(manifest.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(manifest.readAll());
    EXPECT_TRUE(text.contains(QFileInfo(fields[0]).fileName()));
    EXPECT_FALSE(text.contains(QFileInfo(fields[1]).fileName()));
    EXPECT_FALSE(text.contains(QFileInfo(fields[2]).fileName()));
}

TEST_F(AnalyzerTest, AProxyIsTheFileStretchedAsItIsShownInEightBits)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields);
    window.window->optimize();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle() && window.window->imagesOptimized(); }));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    for (const QString& field : fields)
    {
        const QString proxy = window.window->proxyPathOf(field);
        ASSERT_FALSE(proxy.isEmpty()) << field.toStdString();
        const std::optional<ReadBack> written = readBack(session(), proxy);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->type, an::SampleType::UInt8);
        EXPECT_EQ(written->width, 160u);
        // The sky at five per cent of full scale is stretched toward the display background; converted unstretched it
        // would sit at thirteen of two hundred and fifty-five.
        std::vector<double> sorted = written->fractions;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2u),
                         sorted.end());
        EXPECT_GT(sorted[sorted.size() / 2u], 0.15) << proxy.toStdString();
    }
    // The preview now plays the proxies.
    ASSERT_TRUE(pumpUntil([&window, &fields]() {
        QFile manifest(window.window->previewSequencePath());
        return manifest.open(QIODevice::ReadOnly) &&
               QString::fromUtf8(manifest.readAll())
                   .contains(QFileInfo(window.window->proxyPathOf(fields[0])).fileName());
    }));
}

TEST_F(AnalyzerTest, ARemovedFilesProxyGoesWithIt)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields);
    window.window->optimize();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle() && window.window->imagesOptimized(); }));
    const QString proxy = window.window->proxyPathOf(fields[1]);
    ASSERT_TRUE(QFileInfo::exists(proxy));
    window.window->removeItems({fields[1]});
    EXPECT_TRUE(window.window->proxyPathOf(fields[1]).isEmpty());
    ASSERT_TRUE(pumpUntil([&proxy]() { return !QFileInfo::exists(proxy); }));
    EXPECT_TRUE(window.window->imagesOptimized());
}

TEST_F(AnalyzerTest, AFileAddedAfterOptimizingHasNoProxyUntilAsked)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields.mid(0, 2));
    window.window->optimize();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle() && window.window->imagesOptimized(); }));
    listed(window, fields);
    EXPECT_FALSE(window.window->imagesOptimized());
    EXPECT_TRUE(window.window->proxyPathOf(fields[2]).isEmpty());
}

TEST_F(AnalyzerTest, TheLinearDisplayShowsTheOriginalFilesAndAutostretchPlaysTheProxiesAgain)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    window.window->show();
    listed(window, fields);
    QComboBox* mode = displayModeOf(window);
    ASSERT_NE(mode, nullptr);
    const QStringList proxies = optimized(window, fields);
    ImageViewWidget* view = window.window->previewView()->getViewWidget();

    // The proxies hold the autostretch display, which the selector names, and are shown as they are.
    EXPECT_EQ(mode->currentText().toStdString(), "Autostretch");
    EXPECT_EQ(shownRepresentation(window), an::SampleType::UInt8);
    EXPECT_EQ(view->presentationRequest().stretch, an::PresentationStretch::Linear) << "a proxy is not stretched twice";
    QAction* play = playActionOf(window);
    ASSERT_NE(play, nullptr);
    EXPECT_TRUE(play->isEnabled());
    const std::vector<QDateTime> written = writtenAt(proxies);

    // The linear display shows the original files' own samples, and the window says that fast play needs autostretch.
    mode->setCurrentText(QStringLiteral("Linear"));
    ASSERT_TRUE(pumpUntil([&window, &fields]() { return settled(window) && previewPlays(window, fields); }));
    EXPECT_EQ(mode->currentText().toStdString(), "Linear");
    EXPECT_EQ(shownRepresentation(window), an::SampleType::Float32);
    const std::optional<int> linear = paintedMedian(window);
    ASSERT_TRUE(linear.has_value());
    EXPECT_LT(*linear, 26) << "the sky at five per cent of full scale, unstretched, sits at thirteen";
    EXPECT_TRUE(window.window->optimizationStatusText().contains(QStringLiteral("Autostretch")))
        << window.window->optimizationStatusText().toStdString();
    EXPECT_FALSE(window.window->ui().optimizeButton->isEnabled()) << "every file has its proxy";
    play = playActionOf(window);
    ASSERT_NE(play, nullptr);
    EXPECT_FALSE(play->isEnabled());
    EXPECT_TRUE(play->toolTip().contains(QStringLiteral("Autostretch"))) << play->toolTip().toStdString();

    // Back in the autostretch display the proxies play again, as they were written.
    mode->setCurrentText(QStringLiteral("Autostretch"));
    ASSERT_TRUE(pumpUntil([&window, &proxies]() { return settled(window) && previewPlays(window, proxies); }));
    EXPECT_EQ(writtenAt(proxies), written) << "no proxy was written again";
    EXPECT_EQ(mode->currentText().toStdString(), "Autostretch");
    EXPECT_EQ(shownRepresentation(window), an::SampleType::UInt8);
    EXPECT_EQ(view->presentationRequest().stretch, an::PresentationStretch::Linear);
    const std::optional<int> stretched = paintedMedian(window);
    ASSERT_TRUE(stretched.has_value());
    EXPECT_GT(*stretched, 38) << "the proxy's stretched sky";
    play = playActionOf(window);
    ASSERT_NE(play, nullptr);
    EXPECT_TRUE(play->isEnabled());
}

TEST_F(AnalyzerTest, TheProxiesLeaveThePreviewAtOnceWhenTheLinearDisplayIsChosen)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    window.window->show();
    listed(window, fields);
    const QStringList proxies = optimized(window, fields);
    QComboBox* mode = displayModeOf(window);
    ASSERT_NE(mode, nullptr);
    ASSERT_TRUE(window.window->previewView()->hasImage());

    // The files' sequence waits for the session, which the case holds, so what the preview shows until it lands is
    // seen.
    std::optional<an::Result<an::consumer::OperationScope>> held;
    held.emplace(session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch}));
    ASSERT_TRUE(held->hasValue());
    mode->setCurrentText(QStringLiteral("Linear"));
    EXPECT_TRUE(pumpUntil([&window]() { return !window.window->previewView()->hasImage(); }))
        << "a proxy shown in the linear display would show what that display is not";
    EXPECT_TRUE(previewPlays(window, proxies)) << "the files are still on their way";
    held.reset();

    ASSERT_TRUE(pumpUntil([&window, &fields]() { return settled(window) && previewPlays(window, fields); }));
    EXPECT_TRUE(window.window->previewView()->hasImage());
    EXPECT_EQ(shownRepresentation(window), an::SampleType::Float32);
}

TEST_F(AnalyzerTest, TheProxiesPlayOnlyThroughTheStretchTheyHold)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    window.window->show();
    listed(window, fields);
    const QStringList held = optimized(window, fields);
    ImageViewWidget* view = window.window->previewView()->getViewWidget();
    const AutostretchParams heldStretch = view->getAutostretchSettings();
    AutostretchParams brighter = heldStretch;
    brighter.targetBackground = heldStretch.targetBackground + 0.2;

    // Other settings, set as the Autostretch Settings window sets them, show the original files stretched through them.
    view->setAutostretchSettings(brighter);
    ASSERT_TRUE(pumpUntil([&window, &fields]() { return settled(window) && previewPlays(window, fields); }));
    EXPECT_EQ(shownRepresentation(window), an::SampleType::Float32);
    EXPECT_EQ(view->presentationRequest().stretch, an::PresentationStretch::Automatic);
    EXPECT_FALSE(window.window->imagesOptimized());
    EXPECT_EQ(window.window->optimizationStatusText().toStdString(), "Images Need Optimization");
    EXPECT_TRUE(window.window->ui().optimizeButton->isEnabled());

    // The settings the proxies hold bring them back, as they were written.
    const std::vector<QDateTime> written = writtenAt(held);
    view->setAutostretchSettings(heldStretch);
    ASSERT_TRUE(pumpUntil([&window, &held]() { return settled(window) && previewPlays(window, held); }));
    EXPECT_EQ(writtenAt(held), written) << "no proxy was written again";
    EXPECT_TRUE(window.window->imagesOptimized());

    // Optimizing under the other settings writes proxies that hold them, in place of the others.
    const std::optional<double> heldMedian = fileMedian(session(), held[0]);
    ASSERT_TRUE(heldMedian.has_value());
    view->setAutostretchSettings(brighter);
    ASSERT_TRUE(pumpUntil([&window, &fields]() { return settled(window) && previewPlays(window, fields); }));
    const QStringList rewritten = optimized(window, fields);
    ASSERT_EQ(rewritten.size(), held.size());
    for (qsizetype index = 0; index < held.size(); ++index)
    {
        EXPECT_NE(rewritten[index], held[index]);
        EXPECT_FALSE(QFileInfo::exists(held[index])) << "a proxy of the other stretch goes";
    }
    const std::optional<double> brighterMedian = fileMedian(session(), rewritten[0]);
    ASSERT_TRUE(brighterMedian.has_value());
    EXPECT_GT(*brighterMedian, *heldMedian + 0.1) << "the new proxy holds the brighter background";
}

TEST_F(AnalyzerTest, ExportSequenceOpensOverTheOriginalFiles)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields);
    window.window->exportSequence();
    ExportSequenceControl* opened = nullptr;
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
    EXPECT_TRUE(context().errorTexts.isEmpty());
    opened->close();
    QCoreApplication::processEvents();
}

TEST_F(AnalyzerTest, ThePlaybackRateIsWhatTheWindowKeeps)
{
    AnalyzerWindow window;
    window.window->deserialize_json_settings(R"({"version":1,"params":{"fps":12.0}})");
    nlohmann::json stored = nlohmann::json::parse(window.window->serialize_json_settings());
    EXPECT_DOUBLE_EQ(stored["params"].value("fps", 0.0), 12.0);
    window.window->deserialize_json_settings(R"({"version":1,"params":{"fps":500.0}})");
    stored = nlohmann::json::parse(window.window->serialize_json_settings());
    EXPECT_DOUBLE_EQ(stored["params"].value("fps", 0.0), 60.0);
    window.window->deserialize_json_settings("not json");
    stored = nlohmann::json::parse(window.window->serialize_json_settings());
    EXPECT_DOUBLE_EQ(stored["params"].value("fps", 0.0), 60.0);
}

TEST_F(AnalyzerTest, ThePlayersRateSurvivesTheListChanging)
{
    const QStringList fields = writeFields();
    AnalyzerWindow window;
    listed(window, fields);
    ASSERT_TRUE(
        pumpUntil([&window]() { return window.window->isIdle() && !window.window->previewSequencePath().isEmpty(); }));
    const auto player = [&window]() {
        return dynamic_cast<SequencePlayerControl*>(window.window->previewView()->sequencePlayer());
    };
    ASSERT_NE(player(), nullptr);
    player()->setFramesPerSecond(7.0);
    ASSERT_DOUBLE_EQ(player()->framesPerSecond(), 7.0);

    // The preview is made again over the list that is left, at the rate the person chose.
    const QString shown = window.window->previewSequencePath();
    window.window->removeItems({fields[2]});
    ASSERT_TRUE(pumpUntil(
        [&window, &shown]() { return window.window->isIdle() && window.window->previewSequencePath() != shown; }));
    ASSERT_NE(player(), nullptr);
    EXPECT_DOUBLE_EQ(player()->framesPerSecond(), 7.0);
    const nlohmann::json stored = nlohmann::json::parse(window.window->serialize_json_settings());
    EXPECT_DOUBLE_EQ(stored["params"].value("fps", 0.0), 7.0);
}

TEST_F(AnalyzerTest, ClosingTheWindowWhileItOptimizesWaitsForNothing)
{
    const QStringList fields = writeFields();
    auto* window = new ImageListAnalyzerProcess();
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->addItemsNonBlocking(fields);
    ASSERT_TRUE(pumpUntil(
        [window, &fields]() { return window->isIdle() && window->fileList().paths().size() == fields.size(); }));
    window->optimize();
    window->close();
    for (int round = 0; round < 50; ++round)
    {
        QCoreApplication::processEvents();
    }
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    SUCCEED();
}

} // namespace
} // namespace an::tests::processes::workflow
