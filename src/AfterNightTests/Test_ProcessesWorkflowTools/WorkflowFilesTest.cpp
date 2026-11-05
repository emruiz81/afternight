// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow tools' file list: a file's header probed through the engine and its figures read as the integration's
/// import reads them, the frame-type filter an Add Folder applies, the columns' texts and orders, and the list over a
/// table - files probed on the thread pool, a file already listed passed over, and a selection of several rows kept
/// and reported as the person makes it.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "processes/ImageCalibrationProcess.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/ImageRegistrationProcess.h"
#include "processes/ImageStackingProcess.h"
#include "processes/WorkflowToolFiles.h"
#include "widgets/TableWidget.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QTimeZone>
#include <QThreadPool>
#include <QWidget>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace an::tests::processes::workflow
{
namespace
{

using an::ui::processes::WorkflowFile;
using an::ui::processes::WorkflowFileColumn;
using an::ui::processes::WorkflowFileList;

class WorkflowFilesTest : public WorkflowCase
{
protected:
    [[nodiscard]] QString frame(const QString& name, double exposure, const QString& filter,
                                an::SampleType type = an::SampleType::UInt16)
    {
        const QString path = temporaryPath(name);
        EXPECT_TRUE(writeFlatFrame(session(), path, 32u, 24u, 1u, 0.25, type, exposure, filter));
        return path;
    }
};

/// A list over a table in a window of its own.
struct ListedTable
{
    ListedTable()
        : owner(std::make_unique<QWidget>()), table(new TableWidget(owner.get())),
          list(std::make_unique<WorkflowFileList>(
              owner.get(), an::ui::processes::WorkflowFileListWidgets{.table = table},
              an::ui::processes::WorkflowFileListOptions{
                  .columns = {{QStringLiteral("File"), WorkflowFileColumn::File},
                              {QStringLiteral("Exposure"), WorkflowFileColumn::Exposure}},
                  .initialSortColumn = std::nullopt}))
    {
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->onImportFinished = [this](const an::ui::processes::WorkflowImportReport& report) {
            reports.push_back(report);
        };
    }

    std::unique_ptr<QWidget> owner;
    TableWidget* table;
    std::unique_ptr<WorkflowFileList> list;
    std::vector<an::ui::processes::WorkflowImportReport> reports;
};

TEST(WorkflowFileFilterTest, AFrameTypePassesUnderEachOfItsSpellings)
{
    using an::ui::processes::matchesFrameTypeFilter;
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("LIGHT"), QStringLiteral("LIGHT")));
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("LIGHT FRAME"), QStringLiteral("LIGHT")));
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("DARK"), QStringLiteral("DARK")));
    EXPECT_FALSE(matchesFrameTypeFilter(QStringLiteral("DARKFLAT"), QStringLiteral("DARK")));
    EXPECT_FALSE(matchesFrameTypeFilter(QStringLiteral("DARKFLAT"), QStringLiteral("FLAT")));
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("DARKFLAT"), QStringLiteral("DARKFLAT")));
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("OFFSET"), QStringLiteral("BIAS")));
    EXPECT_FALSE(matchesFrameTypeFilter(QStringLiteral("LIGHT"), QStringLiteral("BIAS")));
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("FLAT"), QString()));
    EXPECT_TRUE(matchesFrameTypeFilter(QStringLiteral("FLAT"), QStringLiteral("unknown")));
}

TEST_F(WorkflowFilesTest, AProbeReadsTheHeaderAndTheFiguresTheIntegrationReads)
{
    const QString path = frame(QStringLiteral("ha.fits"), 300.0, QStringLiteral("Ha"));
    const an::Result<WorkflowFile> file = an::ui::processes::probeWorkflowFile(session(), path, {});
    ASSERT_TRUE(file.hasValue()) << file.status().message();
    EXPECT_EQ(file->path(), path);
    EXPECT_EQ(file->frameTypeText, QStringLiteral("LIGHT"));
    EXPECT_EQ(file->figures.filter(), QStringLiteral("Ha"));
    EXPECT_DOUBLE_EQ(file->figures.exposureSeconds(), 300.0);
    EXPECT_FALSE(file->displayDate.isEmpty());
    using an::ui::processes::workflowFileColumnText;
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::File), QStringLiteral("ha.fits"));
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::Size), QStringLiteral("32x24"));
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::Filter), QStringLiteral("Ha"));
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::Exposure), QStringLiteral("300"));
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::Cfa), QStringLiteral("No"));
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::Depth), QStringLiteral("16 bit"));
    EXPECT_EQ(workflowFileColumnText(*file, WorkflowFileColumn::IsoGain), QStringLiteral("-"));
}

TEST_F(WorkflowFilesTest, AProbeOfNoFileIsRefused)
{
    const an::Result<WorkflowFile> file =
        an::ui::processes::probeWorkflowFile(session(), temporaryPath(QStringLiteral("missing.fits")), {});
    ASSERT_FALSE(file.hasValue());
    EXPECT_EQ(file.status().code(), an::ErrorCode::InvalidArgument);
}

TEST_F(WorkflowFilesTest, FilesAreOrderedByTheColumnAsked)
{
    std::vector<WorkflowFile> files;
    for (const auto& [name, exposure] :
         {std::pair{QStringLiteral("b.fits"), 120.0}, std::pair{QStringLiteral("a.fits"), 30.0},
          std::pair{QStringLiteral("c.fits"), 60.0}})
    {
        an::Result<WorkflowFile> file =
            an::ui::processes::probeWorkflowFile(session(), frame(name, exposure, QStringLiteral("L")), {});
        ASSERT_TRUE(file.hasValue());
        files.push_back(std::move(*file));
    }
    ASSERT_TRUE(an::ui::processes::sortWorkflowFiles(files, WorkflowFileColumn::Exposure, false));
    EXPECT_DOUBLE_EQ(files[0].figures.exposureSeconds(), 30.0);
    EXPECT_DOUBLE_EQ(files[2].figures.exposureSeconds(), 120.0);
    ASSERT_TRUE(an::ui::processes::sortWorkflowFiles(files, WorkflowFileColumn::File, true));
    EXPECT_EQ(workflowFileColumnText(files[0], WorkflowFileColumn::File), QStringLiteral("c.fits"));
    EXPECT_FALSE(an::ui::processes::sortWorkflowFiles(files, WorkflowFileColumn::Size, false));
}

TEST_F(WorkflowFilesTest, TheListProbesOnThePoolAndPassesOverWhatItHolds)
{
    const QStringList paths{frame(QStringLiteral("one.fits"), 10.0, QStringLiteral("R")),
                            frame(QStringLiteral("two.fits"), 10.0, QStringLiteral("G")),
                            temporaryPath(QStringLiteral("absent.fits"))};
    ListedTable listed;
    listed.list->addFiles(paths);
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    EXPECT_EQ(listed.reports[0].added, 2u);
    EXPECT_EQ(listed.reports[0].failed.size(), 1u);
    EXPECT_EQ(listed.list->files().size(), 2u);
    EXPECT_EQ(listed.table->rowCount(), 2);

    listed.list->addFiles(paths.mid(0, 1));
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 2u; }));
    EXPECT_EQ(listed.reports[1].added, 0u);
    EXPECT_EQ(listed.reports[1].skipped, 1u);

    listed.list->remove({paths[0]});
    EXPECT_EQ(listed.list->paths(), QStringList{paths[1]});
}

TEST_F(WorkflowFilesTest, AFileNamedTwiceInOneAddIsListedOnceAndPassedOverOnce)
{
    const QString path = frame(QStringLiteral("twice.fits"), 10.0, QStringLiteral("R"));
    ListedTable listed;
    listed.list->addFiles(QStringList{path, path});
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    EXPECT_EQ(listed.reports[0].added, 1u);
    EXPECT_EQ(listed.reports[0].skipped, 1u);
    EXPECT_EQ(listed.list->paths(), QStringList{path});
}

TEST_F(WorkflowFilesTest, FilesLandingKeepTheRowThatWasSelected)
{
    const QStringList paths{frame(QStringLiteral("a.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("b.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("c.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("d.fits"), 10.0, QStringLiteral("L"))};
    ListedTable listed;
    listed.list->addFiles(paths.mid(0, 3));
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    ASSERT_EQ(listed.list->selectedRow(), 0);
    listed.list->selectRow(1);
    ASSERT_EQ(listed.list->selectedRow(), 1);

    listed.list->addFiles(paths.mid(3, 1));
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 2u; }));
    ASSERT_EQ(listed.list->files().size(), 4u);
    EXPECT_EQ(listed.list->selectedRow(), 1);
    ASSERT_NE(listed.list->selectedFile(), nullptr);
    EXPECT_EQ(listed.list->selectedFile()->path(), paths[1]);
}

TEST_F(WorkflowFilesTest, ADestroyedListStopsTheImportsItHasInFlight)
{
    // Every path is missing, so each is a warning of its own; the first holds the import's worker where it stands.
    QStringList absent;
    for (int index = 0; index < 40; ++index)
    {
        absent << temporaryPath(QStringLiteral("absent_%1.fits").arg(index));
    }
    LogCapture log;
    log.holdAt("could not be read");
    {
        ListedTable listed;
        listed.list->addFiles(absent);
        ASSERT_TRUE(log.waitHeld());
        // The list goes while its worker is held, and the worker is let go of afterwards.
        listed.list.reset();
        log.release();
    }
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_EQ(log.count("could not be read"), 1u);
}

// The list takes the window's stop beside its own: an import in flight stops when the window's work is stopped - at
// the application's exit - as it does when the list goes or is cleared, so the exit waits for the file being probed
// and not for the import.
TEST_F(WorkflowFilesTest, TheWindowsStopStopsTheImportsTheListHasInFlight)
{
    // The first path is missing, so its warning holds the import's worker before it reads the files after it.
    const QStringList paths{temporaryPath(QStringLiteral("absent.fits")),
                            frame(QStringLiteral("a.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("b.fits"), 10.0, QStringLiteral("L"))};
    std::stop_source windowStop;
    auto owner = std::make_unique<QWidget>();
    auto* table = new TableWidget(owner.get());
    WorkflowFileList list(
        owner.get(), an::ui::processes::WorkflowFileListWidgets{.table = table},
        an::ui::processes::WorkflowFileListOptions{.columns = {{QStringLiteral("File"), WorkflowFileColumn::File}},
                                                   .initialSortColumn = std::nullopt,
                                                   .stop = windowStop.get_token()});
    std::vector<an::ui::processes::WorkflowImportReport> reports;
    list.onImportFinished = [&reports](const an::ui::processes::WorkflowImportReport& report) {
        reports.push_back(report);
    };
    LogCapture log;
    log.holdAt("could not be read");
    list.addFiles(paths);
    ASSERT_TRUE(log.waitHeld());
    windowStop.request_stop();
    log.release();
    ASSERT_TRUE(pumpUntil([&reports]() { return reports.size() == 1u; }));
    EXPECT_TRUE(reports[0].stopped) << "the import went on after the window's stop";
    EXPECT_EQ(reports[0].added, 0u);
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_TRUE(list.files().empty());
}

// Every workflow window hands its list its own stop: an import in flight when the window's work is stopped ends at the
// file being probed rather than at the import's last file.
TEST_F(WorkflowFilesTest, EveryWindowsStopStopsTheImportsItHasInFlight)
{
    const QStringList paths{temporaryPath(QStringLiteral("absent.fits")),
                            frame(QStringLiteral("a.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("b.fits"), 10.0, QStringLiteral("L"))};
    const auto stops = [&paths](QWidget* made, auto* window, const char* name) {
        made->setAttribute(Qt::WA_DeleteOnClose, false);
        LogCapture log;
        log.holdAt("could not be read");
        window->addItemsNonBlocking(paths);
        ASSERT_TRUE(log.waitHeld()) << name;
        window->stopWorkInFlight();
        log.release();
        ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); })) << name;
        EXPECT_EQ(window->fileList().paths().size(), 0) << name << ": the import went on after the window's stop";
        delete made;
        QCoreApplication::processEvents();
    };
    {
        auto* window = new ImageCalibrationProcess();
        stops(window, window, "calibration");
    }
    {
        auto* window = new ImageRegistrationProcess();
        stops(window, window, "registration");
    }
    {
        auto* window = new ImageStackingProcess();
        stops(window, window, "stacking");
    }
    {
        auto* window = new ImageListAnalyzerProcess();
        stops(window, window, "analyzer");
    }
}

TEST_F(WorkflowFilesTest, ClearingTheListDropsTheImportsInFlight)
{
    // The first path is missing, so its warning holds the import's worker before it reads the files after it.
    const QStringList paths{temporaryPath(QStringLiteral("absent.fits")),
                            frame(QStringLiteral("a.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("b.fits"), 10.0, QStringLiteral("L"))};
    ListedTable listed;
    LogCapture log;
    log.holdAt("could not be read");
    listed.list->addFiles(paths);
    ASSERT_TRUE(log.waitHeld());
    EXPECT_FALSE(listed.list->idle());
    listed.list->clear();
    log.release();
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    EXPECT_TRUE(listed.reports[0].stopped);
    EXPECT_EQ(listed.reports[0].added, 0u);
    EXPECT_TRUE(listed.list->idle());
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_TRUE(listed.list->files().empty());
    EXPECT_EQ(listed.table->rowCount(), 0);

    // An import asked for after the clear lands as usual.
    listed.list->addFiles(paths.mid(1));
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 2u; }));
    EXPECT_EQ(listed.list->files().size(), 2u);
}

TEST_F(WorkflowFilesTest, ClearingTheListDropsAFolderStillBeingListed)
{
    const QString folder = temporaryPath(QStringLiteral("listed"));
    ASSERT_TRUE(QDir().mkpath(folder));
    static_cast<void>(frame(QStringLiteral("listed/a.fits"), 10.0, QStringLiteral("L")));
    static_cast<void>(frame(QStringLiteral("listed/b.fits"), 10.0, QStringLiteral("L")));
    ListedTable listed;
    LogCapture log;
    log.holdAt("listing folder");
    listed.list->addFolder(folder, false);
    ASSERT_TRUE(log.waitHeld());
    EXPECT_FALSE(listed.list->idle());
    listed.list->clear();
    log.release();
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    EXPECT_TRUE(listed.reports[0].stopped);
    EXPECT_EQ(listed.reports[0].added, 0u);
    EXPECT_TRUE(listed.list->idle());
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_TRUE(listed.list->files().empty());
    EXPECT_EQ(listed.table->rowCount(), 0);

    // A folder asked for after the clear lands as usual.
    listed.list->addFolder(folder, false);
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 2u; }));
    EXPECT_FALSE(listed.reports[1].stopped);
    EXPECT_EQ(listed.list->files().size(), 2u);
}

/// Runs the case in a local time five hours behind UTC whatever the host's own zone, so a time written in local time
/// cannot pass for one written in UTC; the host's zone comes back when it goes.
class FiveHoursBehindUtc
{
public:
    FiveHoursBehindUtc() : m_had(qEnvironmentVariableIsSet("TZ")), m_previous(qgetenv("TZ"))
    {
        qputenv("TZ", "AFT5");
    }
    ~FiveHoursBehindUtc()
    {
        if (m_had)
        {
            qputenv("TZ", m_previous);
        }
        else
        {
            qunsetenv("TZ");
        }
    }
    FiveHoursBehindUtc(const FiveHoursBehindUtc&) = delete;
    FiveHoursBehindUtc& operator=(const FiveHoursBehindUtc&) = delete;

private:
    bool m_had;
    QByteArray m_previous;
};

TEST_F(WorkflowFilesTest, TheDateColumnStatesHeaderAndFileTimesInOneTimeBase)
{
    const FiveHoursBehindUtc zone;
    ASSERT_EQ(QDateTime(QDate(2020, 1, 1), QTime(12, 0, 0), QTimeZone(QTimeZone::UTC)).toLocalTime().offsetFromUtc(),
              -5 * 3600)
        << "the case's local time did not take effect";
    // A header time at noon in UTC, and a file that states none, modified an hour later.
    const QString stated = temporaryPath(QStringLiteral("stated.fits"));
    an::Result<an::CodecCard> date =
        an::CodecCard::create(an::CodecNamespace::Fits, "DATE", "'2020-01-01T12:00:00'", "");
    ASSERT_TRUE(date.hasValue());
    std::vector<an::CodecCard> cards;
    cards.push_back(std::move(*date));
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_TRUE(codec.hasValue());
    ASSERT_TRUE(writeFrame(
        session(), stated, 16u, 16u, 1u, an::SampleType::UInt16,
        [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25; },
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, std::move(*codec), std::nullopt)));
    const QString unstated = frame(QStringLiteral("unstated.fits"), 10.0, QStringLiteral("L"));
    const QDateTime modified(QDate(2020, 1, 1), QTime(13, 0, 0), QTimeZone(QTimeZone::UTC));
    {
        QFile file(unstated);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(modified, QFileDevice::FileModificationTime));
    }

    std::vector<WorkflowFile> files;
    for (const QString& path : {unstated, stated})
    {
        an::Result<WorkflowFile> file = an::ui::processes::probeWorkflowFile(session(), path, {});
        ASSERT_TRUE(file.hasValue()) << file.status().message();
        files.push_back(std::move(*file));
    }
    EXPECT_EQ(files[1].displayDate, QStringLiteral("2020-01-01 12:00:00.000"));
    EXPECT_EQ(files[0].displayDate, QStringLiteral("2020-01-01 13:00:00.000"));
    ASSERT_TRUE(an::ui::processes::sortWorkflowFiles(files, WorkflowFileColumn::Date, false));
    EXPECT_EQ(files[0].path(), stated);
    EXPECT_EQ(files[1].path(), unstated);
}

TEST_F(WorkflowFilesTest, AFolderFilterPassesOnlyTheFilterAsked)
{
    const QStringList paths{frame(QStringLiteral("r.fits"), 10.0, QStringLiteral("R")),
                            frame(QStringLiteral("ha.fits"), 10.0, QStringLiteral("ha"))};
    ListedTable listed;
    listed.list->addFiles(paths, an::ui::processes::WorkflowFileFilter{.filterName = QStringLiteral("Ha")});
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    EXPECT_EQ(listed.list->paths(), QStringList{paths[1]});
    EXPECT_EQ(listed.reports[0].skipped, 1u);
}

// An import that does not add every file it was given says how many it added, passed over - already listed or kept out
// by the filter - and could not read, with the first reason; one that added them all, or was stopped, says nothing.
TEST(WorkflowImportSummaryTest, TheSummaryCountsWhatWasAddedPassedOverAndRefused)
{
    using an::ui::processes::WorkflowImportReport;
    using an::ui::processes::workflowImportSummary;
    EXPECT_FALSE(workflowImportSummary(WorkflowImportReport{.added = 3u}).has_value());
    EXPECT_FALSE(workflowImportSummary(WorkflowImportReport{.skipped = 2u, .stopped = true}).has_value());
    const std::optional<QString> passed = workflowImportSummary(WorkflowImportReport{.added = 1u, .skipped = 2u});
    ASSERT_TRUE(passed.has_value());
    EXPECT_EQ(*passed, QStringLiteral("Added 1 file(s); passed over 2 already listed or kept out by the filter."));
    const std::optional<QString> none = workflowImportSummary(WorkflowImportReport{.skipped = 4u});
    ASSERT_TRUE(none.has_value()) << "a filter that passes nothing says so";
    EXPECT_EQ(*none, QStringLiteral("Added 0 file(s); passed over 4 already listed or kept out by the filter."));
    WorkflowImportReport refused{.added = 1u, .skipped = 2u};
    refused.failed.emplace_back(
        QStringLiteral("x.fit"),
        an::Status::failure(an::ErrorCode::InvalidData, an::StatusDetail{.message = "bad header"}));
    const std::optional<QString> said = workflowImportSummary(refused);
    ASSERT_TRUE(said.has_value());
    EXPECT_EQ(*said, QStringLiteral("Added 1 file(s); passed over 2 already listed or kept out by the filter; 1 could "
                                    "not be read. First error: bad header. See the log for per-file details."));
}

// Every workflow window says what an import passed over, as it says what an import could not read: an import that kept
// every file out said nothing.
TEST_F(WorkflowFilesTest, EveryWindowSaysWhatAnImportPassedOver)
{
    const QStringList paths{frame(QStringLiteral("a.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("b.fits"), 10.0, QStringLiteral("L"))};
    const auto says = [this, &paths](QWidget* made, auto* window, const char* name) {
        made->setAttribute(Qt::WA_DeleteOnClose, false);
        window->addItemsNonBlocking(paths);
        ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); })) << name;
        ASSERT_EQ(window->fileList().paths().size(), 2) << name;
        const qsizetype before = context().messages.size();
        window->addItemsNonBlocking(paths);
        ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); })) << name;
        ASSERT_EQ(context().messages.size(), before + 1) << name << ": the import that added nothing said nothing";
        EXPECT_EQ(context().messages.back(), QStringLiteral("info:Some files were passed over")) << name;
        delete made;
        QCoreApplication::processEvents();
    };
    {
        auto* window = new ImageCalibrationProcess();
        says(window, window, "calibration");
    }
    {
        auto* window = new ImageRegistrationProcess();
        says(window, window, "registration");
    }
    {
        auto* window = new ImageStackingProcess();
        says(window, window, "stacking");
    }
    {
        auto* window = new ImageListAnalyzerProcess();
        says(window, window, "analyzer");
    }
}

// A workflow window's saved list comes back in the order it was saved, an order the person made by hand included, when
// the window is reopened.
TEST_F(WorkflowFilesTest, ASavedListComesBackInTheOrderItWasSaved)
{
    QStringList paths;
    for (const int hour : {12, 13, 14})
    {
        const QString path = frame(QStringLiteral("f%1.fits").arg(hour), 10.0, QStringLiteral("L"));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(QDateTime(QDate(2020, 1, 1), QTime(hour, 0, 0), QTimeZone(QTimeZone::UTC)),
                                     QFileDevice::FileModificationTime));
        paths.push_back(path);
    }
    QStringList reversed = paths;
    std::reverse(reversed.begin(), reversed.end());
    const auto keeps = [&paths, &reversed](QWidget* madeFirst, auto* first, QWidget* madeSecond, auto* second,
                                           const char* name) {
        madeFirst->setAttribute(Qt::WA_DeleteOnClose, false);
        madeSecond->setAttribute(Qt::WA_DeleteOnClose, false);
        first->addItemsNonBlocking(paths);
        ASSERT_TRUE(pumpUntil([first]() { return first->isIdle() && first->fileList().paths().size() == 3; })) << name;
        first->ui().tableWidget->selectRow(1);
        first->ui().moveUpButton->click();
        const QStringList made = first->fileList().paths();
        ASSERT_NE(made, paths) << name << ": the case makes an order by hand";
        ASSERT_NE(made, reversed) << name << ": the case makes an order by hand";

        second->deserialize_json_settings(first->serialize_json_settings());
        ASSERT_TRUE(pumpUntil([second]() { return second->isIdle() && second->fileList().paths().size() == 3; }))
            << name;
        EXPECT_EQ(second->fileList().paths(), made) << name;
        delete madeFirst;
        delete madeSecond;
        QCoreApplication::processEvents();
    };
    {
        auto* first = new ImageCalibrationProcess();
        auto* second = new ImageCalibrationProcess();
        keeps(first, first, second, second, "calibration");
    }
    {
        auto* first = new ImageRegistrationProcess();
        auto* second = new ImageRegistrationProcess();
        keeps(first, first, second, second, "registration");
    }
    {
        auto* first = new ImageStackingProcess();
        auto* second = new ImageStackingProcess();
        keeps(first, first, second, second, "stacking");
    }
}

TEST_F(WorkflowFilesTest, ASelectionOfSeveralRowsIsKeptAndReported)
{
    const QStringList paths{frame(QStringLiteral("a.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("b.fits"), 10.0, QStringLiteral("L")),
                            frame(QStringLiteral("c.fits"), 10.0, QStringLiteral("L"))};
    ListedTable listed;
    int selected = 0;
    int changed = 0;
    listed.list->onSelected = [&selected](int) { ++selected; };
    listed.list->onSelectionChanged = [&changed]() { ++changed; };
    listed.list->addFiles(paths);
    ASSERT_TRUE(pumpUntil([&listed]() { return listed.reports.size() == 1u; }));
    ASSERT_EQ(listed.list->selectedRow(), 0);

    // The selection grows around the current row.
    listed.table->selectionModel()->select(listed.table->model()->index(1, 0),
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(listed.list->selectedRow(), 0);
    EXPECT_EQ(listed.table->selectionModel()->selectedRows().size(), 2);

    // A row added to the selection as the current one moves the current row and keeps the rows selected.
    const int before = selected;
    listed.table->selectionModel()->setCurrentIndex(listed.table->model()->index(2, 0),
                                                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
    EXPECT_EQ(listed.list->selectedRow(), 2);
    EXPECT_GT(selected, before);
    EXPECT_EQ(listed.table->selectionModel()->selectedRows().size(), 3);
}

} // namespace
} // namespace an::tests::processes::workflow
