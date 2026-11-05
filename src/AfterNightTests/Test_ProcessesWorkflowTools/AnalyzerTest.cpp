// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image List Analyzer: the files listed play as an image-list sequence, Optimize For Fast Play writes each file's
/// proxy as it is shown - stretched, in eight bits - and the preview then plays the proxies, a removed file's proxy
/// goes with it, Export Sequence opens over the original files, the playback rate is what the window keeps, and a
/// window closed while it works waits for nothing.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "controls/ExportSequenceControl.h"
#include "controls/ImageViewControl.h"
#include "controls/SequencePlayerControl.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/WorkflowToolFiles.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QApplication>
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
