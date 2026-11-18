// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration's stages as a list, on a page of a main window: the list holds every page the window's two levels
/// of tabs hold - the same widgets, by the record of the tabs' pages - and none else; each row's mark and line follow
/// the job, what its output folder holds of its runs and a run on its way; the Navigate menu's actions and their keys
/// move the list's selection, and so does the keyboard in the list; the list's foot holds the output folder and the
/// project file's acts; a page with a side panel keeps it as an aside and the stacking page's options stand in two
/// rows; a frame shown in the lights' tree from another page goes there through the list; and in a window of its own
/// the process keeps its tabs and its foot.

#include "Test_ImageIntegration/ui/IntegrationPageCase.h"

#include "app/ApplicationStorage.h"
#include "components/AnStageList.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "processes/ImageIntegration/tabs/CalibrationTab.h"
#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/ui_models/IntegrationStageStates.h"
#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/widgets/StageListNavigator.h"
#include "processes/ImageIntegration/widgets/StageNavigator.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::integration::entryText;
using an::tests::integration::IntegrationPageCase;
using an::tests::integration::lightFrame;
using an::tests::integration::press;
using an::tests::integration::pump;
using an::tests::integration::pumpUntil;
using an::ui::AnStageList;
using an::ui::StageRow;
using an::ui::StageStatus;
using ImageIntegration::StageListNavigator;
using ImageIntegration::StageNavigator;
using ImageIntegration::StagePage;
using ImageIntegration::TabStageNavigator;

/// A page as the record of the tabs' pages states it.
struct RecordedPage
{
    QString first;
    QString second;
    QString name;
    QString holds;

    [[nodiscard]] bool operator==(const RecordedPage&) const = default;
};

[[nodiscard]] std::string said(const std::vector<RecordedPage>& pages)
{
    QStringList lines;
    for (const RecordedPage& page : pages)
    {
        lines.append(QStringList{page.first, page.second, page.name, page.holds}.join(QStringLiteral(" | ")));
    }
    return lines.join(QLatin1Char('\n')).toStdString();
}

/// The record of the tabs' pages, read from the tree.
[[nodiscard]] std::vector<RecordedPage> recordedPages()
{
    const QString relative = QStringLiteral("src/AfterNightTests/Test_ImageIntegration/data/IntegrationPages.txt");
    const QStringList seeds = {QDir::currentPath(), QFileInfo(QString::fromUtf8(__FILE__)).absolutePath(),
                               QCoreApplication::applicationDirPath()};
    for (const QString& seed : seeds)
    {
        for (QString at = seed; !at.isEmpty();)
        {
            QFile file(QDir(at).filePath(relative));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                std::vector<RecordedPage> pages;
                for (const QString& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')))
                {
                    const QString entry = line.trimmed();
                    if (entry.isEmpty() || entry.startsWith(QLatin1Char('#')))
                    {
                        continue;
                    }
                    const QStringList fields = entry.split(QStringLiteral(" | "));
                    if (fields.size() == 4)
                    {
                        pages.push_back(RecordedPage{fields[0], fields[1], fields[2], fields[3]});
                    }
                }
                return pages;
            }
            const QString up = QFileInfo(at).absolutePath();
            at = up == at ? QString() : up;
        }
    }
    return {};
}

/// The class of what a page holds: the one widget its layout holds.
[[nodiscard]] QString heldBy(const QWidget& page)
{
    const QList<QWidget*> held = page.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    return held.isEmpty() ? QString() : QString::fromLatin1(held.front()->metaObject()->className());
}

/// A tab's name without the frames a frame source's tab says after it.
[[nodiscard]] QString tabName(const QTabWidget& tabs, const int tab)
{
    QString text = tabs.tabText(tab);
    if (const qsizetype counted = text.lastIndexOf(QStringLiteral(" (")); counted > 0)
    {
        text.truncate(counted);
    }
    return text;
}

[[nodiscard]] const StageRow* rowOf(const std::vector<StageRow>& rows, const char* id)
{
    for (const StageRow& row : rows)
    {
        if (row.id == QLatin1String(id))
        {
            return &row;
        }
    }
    return nullptr;
}

[[nodiscard]] const char* statusName(const StageStatus status)
{
    switch (status)
    {
    case StageStatus::Pending:
        return "not started";
    case StageStatus::Ready:
        return "ready";
    case StageStatus::Running:
        return "running";
    case StageStatus::Done:
        return "done";
    case StageStatus::Warning:
        return "warning";
    case StageStatus::Failed:
        return "failed";
    case StageStatus::OutOfDate:
        return "out of date";
    }
    return "?";
}

[[nodiscard]] std::string said(const std::vector<StageRow>& rows)
{
    QStringList lines;
    for (const StageRow& row : rows)
    {
        lines.append(QStringLiteral("%1: %2 [%3]%4")
                         .arg(row.id, QString::fromLatin1(statusName(row.status)), row.line,
                              row.progress ? QStringLiteral(" %1 %").arg(*row.progress) : QString()));
    }
    return lines.join(QLatin1Char('\n')).toStdString();
}

/// Drags a splitter's handle sideways by a number of pixels, as the pointer does: pressed on it, moved, let go.
void drag(QSplitterHandle& handle, const int dx)
{
    const QPoint from = handle.rect().center();
    const QPoint fromGlobal = handle.mapToGlobal(from);
    const QPoint toGlobal = fromGlobal + QPoint(dx, 0);
    QMouseEvent press(QEvent::MouseButtonPress, from, fromGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&handle, &press);
    QMouseEvent move(QEvent::MouseMove, handle.mapFromGlobal(toGlobal), toGlobal, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&handle, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, handle.mapFromGlobal(toGlobal), toGlobal, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&handle, &release);
    pump();
}

/// Forgets the width a person gave the aside, so that a page opens with its own.
void forgetTheAsideWidth()
{
    QSettings settings = an::ui::applicationSettings();
    settings.remove(QString::fromLatin1(ImageIntegration::kPageAsideWidthSetting));
}

/// The cases of the stage list.
class StageList : public IntegrationPageCase
{
protected:
    /// The stage list of a process shown as a page; null for one that keeps its tabs.
    [[nodiscard]] StageListNavigator* stages() const
    {
        return m_process.isNull() ? nullptr : dynamic_cast<StageListNavigator*>(m_process->stageNavigator());
    }

    [[nodiscard]] std::vector<StageRow> rows() const
    {
        StageListNavigator* const list = stages();
        return list != nullptr ? list->list()->rows() : std::vector<StageRow>{};
    }

    /// Waits until a row stands as stated; whether it came to.
    [[nodiscard]] bool waitFor(const char* id, const StageStatus status)
    {
        return pumpUntil([this, id, status]() {
            const std::vector<StageRow> now = rows();
            const StageRow* const row = rowOf(now, id);
            return row != nullptr && row->status == status;
        });
    }

    /// The manifest the job would run now, as a run adjusts it.
    [[nodiscard]] WorkflowManifest manifestNow() const
    {
        WorkflowManifest manifest = ImageIntegration::buildFullWorkflowManifest(*m_process->model(), {});
        ImageIntegration::normalizeManifestForExecution(manifest);
        return manifest;
    }
};

} // namespace

// The record holds the pages the window's tabs reach, and the list on a page reaches the same ones - the very widgets
// the tabs held - and no other: a row for each, and no page shown by two rows.
TEST_F(StageList, HoldsEveryTabPage)
{
    const std::vector<RecordedPage> recorded = recordedPages();
    ASSERT_EQ(recorded.size(), 8u) << "the record of the tabs' pages was not found, or does not hold eight";

    // The window's tabs, as they are.
    {
        ImageIntegrationProcess& window = openAsWindow();
        ASSERT_FALSE(window.hasPageLayout());
        const auto* const root = window.findChild<QTabWidget*>(QStringLiteral("rootTabWidget"));
        ASSERT_NE(root, nullptr);
        std::vector<RecordedPage> ofTheTabs;
        for (int first = 0; first < root->count(); ++first)
        {
            const auto* const second = root->widget(first)->findChild<QTabWidget*>();
            ASSERT_NE(second, nullptr) << root->tabText(first).toStdString();
            for (int tab = 0; tab < second->count(); ++tab)
            {
                const QWidget* const page = second->widget(tab);
                ofTheTabs.push_back(
                    RecordedPage{tabName(*root, first), tabName(*second, tab), page->objectName(), heldBy(*page)});
            }
        }
        EXPECT_EQ(ofTheTabs, recorded) << "the tabs hold\n"
                                       << said(ofTheTabs) << "\nand the record\n"
                                       << said(recorded);
        delete m_process.data();
        pump();
    }

    // The same process, first with its tabs, then taken into a page: the pages the list reaches.
    m_scope.context().presenter = &m_window;
    ImageIntegrationProcess& process = make();
    ASSERT_NE(process.stageNavigator(), nullptr);
    ASSERT_NE(dynamic_cast<TabStageNavigator*>(process.stageNavigator()), nullptr) << "tabs until it is a page";
    std::array<QPointer<QWidget>, ImageIntegration::kStagePageCount> ofTheTabs;
    for (int page = 0; page < ImageIntegration::kStagePageCount; ++page)
    {
        ofTheTabs[static_cast<std::size_t>(page)] = process.stageNavigator()->page(static_cast<StagePage>(page));
        ASSERT_FALSE(ofTheTabs[static_cast<std::size_t>(page)].isNull());
        EXPECT_EQ(ofTheTabs[static_cast<std::size_t>(page)]->objectName(),
                  recorded[static_cast<std::size_t>(page)].name);
    }
    m_window.show();
    process.show();
    pump();
    ASSERT_TRUE(process.isPresentedAsPage());
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr) << "a page shows its stages as a list";
    EXPECT_EQ(process.findChild<QTabWidget*>(QStringLiteral("rootTabWidget")), nullptr) << "the tabs are gone";
    EXPECT_EQ(process.findChild<QTabWidget*>(QStringLiteral("sourceTabWidget")), nullptr);
    EXPECT_EQ(process.findChild<QTabWidget*>(QStringLiteral("workflowTabWidget")), nullptr);

    const std::vector<StageRow> shown = list->list()->rows();
    QStringList titles;
    for (const StageRow& row : shown)
    {
        titles.append(row.title);
    }
    EXPECT_EQ(titles,
              (QStringList{QStringLiteral("Bias"), QStringLiteral("Darks"), QStringLiteral("Flats"),
                           QStringLiteral("Lights"), QStringLiteral("Calibration"), QStringLiteral("Registration"),
                           QStringLiteral("Stacking"), QStringLiteral("Summary")}));

    std::vector<RecordedPage> reached;
    QList<QWidget*> reachedWidgets;
    for (const StageRow& row : shown)
    {
        list->list()->setSelectedId(row.id);
        pump();
        QWidget* const page = list->page(list->currentPage());
        ASSERT_NE(page, nullptr) << row.id.toStdString();
        EXPECT_TRUE(page->isVisibleTo(&process)) << row.id.toStdString() << " shows its page";
        EXPECT_FALSE(reachedWidgets.contains(page)) << row.id.toStdString() << " shows a page another row shows";
        if (!reachedWidgets.contains(page))
        {
            reachedWidgets.append(page);
        }
    }
    ASSERT_EQ(shown.size(), static_cast<std::size_t>(ImageIntegration::kStagePageCount)) << "a row for each page";
    ASSERT_EQ(reachedWidgets.size(), ImageIntegration::kStagePageCount) << "eight rows reach the eight pages";
    for (int page = 0; page < ImageIntegration::kStagePageCount; ++page)
    {
        QWidget* const widget = list->page(static_cast<StagePage>(page));
        ASSERT_NE(widget, nullptr);
        EXPECT_EQ(widget, ofTheTabs[static_cast<std::size_t>(page)].data()) << "the widget the tabs held";
        EXPECT_TRUE(reachedWidgets.contains(widget)) << widget->objectName().toStdString() << " is reached";
        const RecordedPage& entry = recorded[static_cast<std::size_t>(page)];
        reached.push_back(RecordedPage{entry.first, entry.second, widget->objectName(), heldBy(*widget)});
    }
    EXPECT_EQ(reached, recorded) << "the list reaches\n"
                                 << said(reached) << "\nand the record holds\n"
                                 << said(recorded);
}

// Each row's mark and line follow the workflow: not started with nothing to do, ready once it has what it needs, done
// when the last run's result is current, out of date when the job changed since, failed where the last run failed,
// a warning where the validation holds one, and running, with how far, where a run is.
TEST_F(StageList, StatusFollowsTheWorkflow)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString output = QDir(folder.path()).filePath(QStringLiteral("out"));
    ASSERT_TRUE(QDir().mkpath(output));
    ImageIntegrationProcess& process = openAsPage();
    ASSERT_NE(stages(), nullptr);

    // Not started: a job with no frames has nothing for a stage to do.
    ASSERT_TRUE(pumpUntil([this]() {
        const std::vector<StageRow> now = rows();
        const StageRow* const stacking = rowOf(now, "stacking");
        return stacking != nullptr && !stacking->line.isEmpty();
    })) << said(rows());
    {
        const std::vector<StageRow> now = rows();
        for (const char* id : {"bias", "darks", "flats"})
        {
            const StageRow* const row = rowOf(now, id);
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->status, StageStatus::Pending) << id << "\n" << said(now);
            EXPECT_EQ(row->line, QStringLiteral("no frames")) << id;
        }
        for (const char* id : {"calibration", "registration", "stacking"})
        {
            const StageRow* const row = rowOf(now, id);
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->status, StageStatus::Pending) << id << "\n" << said(now);
        }
        EXPECT_EQ(rowOf(now, "registration")->line, QStringLiteral("no lights"));
        EXPECT_NE(rowOf(now, "summary")->status, StageStatus::Done);
    }

    // Ready: lights and an output folder, and no run yet.
    process.model()->setOutputDirectory(output);
    process.model()->addFrames(
        FrameType::Light, {lightFrame(folder.path(), 0), lightFrame(folder.path(), 1), lightFrame(folder.path(), 2)});
    ASSERT_TRUE(waitFor("stacking", StageStatus::Ready)) << said(rows());
    {
        const std::vector<StageRow> now = rows();
        EXPECT_EQ(rowOf(now, "lights")->status, StageStatus::Ready) << said(now);
        EXPECT_EQ(rowOf(now, "lights")->line, QStringLiteral("3 frames · 1 group"));
        EXPECT_EQ(rowOf(now, "registration")->status, StageStatus::Ready) << said(now);
        EXPECT_EQ(rowOf(now, "registration")->line, QStringLiteral("1 group · 3 frames"));
        EXPECT_TRUE(rowOf(now, "stacking")->line.startsWith(QStringLiteral("1 group"))) << said(now);
        EXPECT_EQ(rowOf(now, "bias")->status, StageStatus::Pending) << "a source with no frames stays not started";
        EXPECT_FALSE(rowOf(now, "registration")->toolTip.isEmpty()) << "a row says its state in a sentence";
        EXPECT_TRUE(rowOf(now, "stacking")->toolTip.contains(QStringLiteral("Drizzle: off")))
            << rowOf(now, "stacking")->toolTip.toStdString();
    }

    // A group drizzled: the stacking row, which stands for the drizzle, says it - and no row of its own does.
    {
        ASSERT_FALSE(process.model()->integrationGroups().empty());
        const GroupKey group = process.model()->integrationGroups().front();
        StackingSettings plain;
        if (const auto found = process.model()->stackingSettings(FrameType::Light).find(group);
            found != process.model()->stackingSettings(FrameType::Light).end())
        {
            plain = found->second;
        }
        StackingSettings drizzled = plain;
        drizzled.drizzle.enabled = true;
        drizzled.drizzle.scale = 2.0;
        process.model()->setStackingSettings(FrameType::Light, group, drizzled);
        ASSERT_TRUE(pumpUntil([this]() {
            const std::vector<StageRow> now = rows();
            return rowOf(now, "stacking")->line == QStringLiteral("1 group · drizzle 2x");
        })) << said(rows());
        EXPECT_EQ(rowOf(rows(), "drizzle"), nullptr) << "the drizzle is the stacking row's";
        EXPECT_TRUE(rowOf(rows(), "stacking")->toolTip.contains(QStringLiteral("Drizzle: 2x · 1 group")))
            << rowOf(rows(), "stacking")->toolTip.toStdString();
        process.model()->setStackingSettings(FrameType::Light, group, plain);
        ASSERT_TRUE(pumpUntil([this]() {
            const std::vector<StageRow> now = rows();
            return !rowOf(now, "stacking")->line.contains(QStringLiteral("drizzle"));
        })) << said(rows());
    }

    // Done: a run that succeeded left the manifest the job still asks for, and its record.
    WorkflowRunStateStore store(output);
    ASSERT_TRUE(store.saveLastSuccessfulManifest(manifestNow()));
    WorkflowRunRecord record;
    record.ended = QDateTime::currentDateTimeUtc();
    record.durationMs = 3060000;
    record.end = WorkflowRunEnd::Completed;
    record.masters = 1;
    ASSERT_TRUE(store.saveRunRecord(record));
    process.refreshAfterRun();
    ASSERT_TRUE(waitFor("stacking", StageStatus::Done)) << said(rows());
    {
        const std::vector<StageRow> now = rows();
        EXPECT_EQ(rowOf(now, "lights")->status, StageStatus::Done) << said(now);
        EXPECT_EQ(rowOf(now, "registration")->status, StageStatus::Done) << said(now);
        EXPECT_EQ(rowOf(now, "summary")->status, StageStatus::Done) << said(now);
        EXPECT_EQ(rowOf(now, "summary")->line, QStringLiteral("1 master · 51m 0s"));
    }

    // Out of date: the stack method changed, so the stack must be made again - and the stages before it need not.
    ASSERT_FALSE(process.model()->integrationGroups().empty());
    const GroupKey group = process.model()->integrationGroups().front();
    StackingSettings settings;
    if (const auto found = process.model()->stackingSettings(FrameType::Light).find(group);
        found != process.model()->stackingSettings(FrameType::Light).end())
    {
        settings = found->second;
    }
    settings.stackMethod =
        settings.stackMethod == QStringLiteral("Median") ? QStringLiteral("Average") : QStringLiteral("Median");
    process.model()->setStackingSettings(FrameType::Light, group, settings);
    ASSERT_TRUE(waitFor("stacking", StageStatus::OutOfDate)) << said(rows());
    {
        const std::vector<StageRow> now = rows();
        EXPECT_EQ(rowOf(now, "registration")->status, StageStatus::Done) << said(now);
        EXPECT_EQ(rowOf(now, "lights")->status, StageStatus::Done) << said(now);
        EXPECT_EQ(rowOf(now, "summary")->status, StageStatus::OutOfDate) << said(now);
        EXPECT_TRUE(rowOf(now, "stacking")->toolTip.contains(QStringLiteral("Out of date")))
            << rowOf(now, "stacking")->toolTip.toStdString();
    }

    // Failed: the last run failed at the registration, and no run succeeded before it.
    ASSERT_TRUE(QFile::remove(store.lastSuccessfulManifestPath(QStringLiteral("full"))));
    record.end = WorkflowRunEnd::Failed;
    record.stage = QStringLiteral("LightsRegistration");
    record.masters = 0;
    ASSERT_TRUE(store.saveRunRecord(record));
    process.refreshAfterRun();
    ASSERT_TRUE(waitFor("registration", StageStatus::Failed)) << said(rows());
    {
        const std::vector<StageRow> now = rows();
        EXPECT_EQ(rowOf(now, "summary")->status, StageStatus::Failed) << said(now);
        EXPECT_EQ(rowOf(now, "summary")->line, QStringLiteral("failed at Registration"));
        EXPECT_EQ(rowOf(now, "stacking")->status, StageStatus::Ready) << said(now);
    }

    // Running: the run is at the registration, half through its second group of two.
    process.setRunState(an::ui::runningState(QStringLiteral("running")));
    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::LightsRegistration;
    progress.currentGroup = 2;
    progress.totalGroups = 2;
    progress.stageProgress = 0.5f;
    progress.overallProgress = 0.4f;
    process.workflowRunProgressed(progress);
    {
        const std::vector<StageRow> now = rows();
        const StageRow* const registration = rowOf(now, "registration");
        EXPECT_EQ(registration->status, StageStatus::Running) << said(now);
        EXPECT_EQ(registration->progress, std::optional<int>(50));
        EXPECT_EQ(registration->line, QStringLiteral("running · 2 / 2"));
        EXPECT_EQ(rowOf(now, "lights")->status, StageStatus::Ready) << "the run is still at them\n" << said(now);
        EXPECT_EQ(rowOf(now, "stacking")->status, StageStatus::Ready) << said(now);
        EXPECT_EQ(rowOf(now, "summary")->status, StageStatus::Running) << said(now);
        EXPECT_EQ(rowOf(now, "summary")->progress, std::optional<int>(40));
        EXPECT_EQ(rowOf(now, "summary")->line, QStringLiteral("running · Registration"));
    }
    process.setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
    process.refreshAfterRun();
    ASSERT_TRUE(pumpUntil([this]() {
        const std::vector<StageRow> now = rows();
        return rowOf(now, "summary")->status != StageStatus::Running;
    })) << said(rows());
    EXPECT_NE(rowOf(rows(), "registration")->status, StageStatus::Running) << "the run ended";

    // A warning: what the validation finds in a page is its row's - here the output folder's, which the summary says.
    process.model()->setOutputDirectory(QString());
    ASSERT_TRUE(pumpUntil([this]() {
        const std::vector<StageRow> now = rows();
        const StageRow* const summary = rowOf(now, "summary");
        return summary != nullptr && summary->line.contains(QStringLiteral("issue"));
    })) << said(rows());
    {
        const std::vector<StageRow> now = rows();
        const StageRow* const summary = rowOf(now, "summary");
        EXPECT_TRUE(summary->status == StageStatus::Warning || summary->status == StageStatus::Failed) << said(now);
        EXPECT_FALSE(summary->toolTip.isEmpty()) << "the issues are spelled out in the row's tooltip";
    }
}

// The Navigate menu's eight actions move the list's selection, each to the row of its page, and so do their keys
// inside the page.
TEST_F(StageList, NavigateActions)
{
    ImageIntegrationProcess& process = openAsPage();
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(m_window.isActive()) << "a shortcut is looked for in the active window";
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    QMenu* navigate = nullptr;
    for (QMenu* menu : page->menus())
    {
        if (menu->title() == QStringLiteral("Navigate"))
        {
            navigate = menu;
        }
    }
    ASSERT_NE(navigate, nullptr);
    const QList<QAction*> entries = navigate->actions();
    ASSERT_EQ(entries.size(), ImageIntegration::kStagePageCount);
    const QStringList rowsOfThePages = {QStringLiteral("bias"),        QStringLiteral("darks"),
                                        QStringLiteral("flats"),       QStringLiteral("lights"),
                                        QStringLiteral("calibration"), QStringLiteral("registration"),
                                        QStringLiteral("stacking"),    QStringLiteral("summary")};
    QSignalSpy told(list, &StageNavigator::currentPageChanged);
    for (int entry = ImageIntegration::kStagePageCount - 1; entry >= 0; --entry)
    {
        entries[entry]->trigger();
        pump();
        EXPECT_EQ(list->currentPage(), static_cast<StagePage>(entry)) << entryText(*entries[entry]).toStdString();
        EXPECT_EQ(list->currentRow(), rowsOfThePages[entry]);
        EXPECT_EQ(list->list()->selectedId(), rowsOfThePages[entry]);
        EXPECT_TRUE(list->page(static_cast<StagePage>(entry))->isVisibleTo(&process));
    }
    EXPECT_EQ(told.count(), ImageIntegration::kStagePageCount) << "each move told once";

    // Their keys are the page's: Ctrl+1 to Ctrl+8, with the focus inside it.
    auto* const inside = process.findChild<QPushButton*>(QStringLiteral("outputDirectoryBrowseButton"));
    ASSERT_NE(inside, nullptr);
    ASSERT_TRUE(inside->isVisibleTo(&process));
    for (int entry = 0; entry < ImageIntegration::kStagePageCount; ++entry)
    {
        press(*inside, QKeySequence(QStringLiteral("Ctrl+%1").arg(entry + 1)));
        EXPECT_EQ(list->currentRow(), rowsOfThePages[entry]) << "Ctrl+" << entry + 1;
    }

    // The page shown already: asked for again, nothing is told.
    const qsizetype toldBefore = told.count();
    entries[static_cast<int>(StagePage::Summary)]->trigger();
    EXPECT_EQ(list->currentRow(), QStringLiteral("summary"));
    EXPECT_EQ(told.count(), toldBefore);
}

// The keyboard moves the selection as in a list - Down, Up, Home, End - and each row shows its page: every move to
// another row is a move to another page.
TEST_F(StageList, Keyboard)
{
    ImageIntegrationProcess& process = openAsPage();
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr);
    AnStageList* const rows = list->list();
    ASSERT_NE(rows->focusPolicy(), Qt::NoFocus) << "the list is a stop of the keyboard";
    rows->setFocus(Qt::TabFocusReason);
    pump();

    QTest::keyClick(rows, Qt::Key_Home);
    EXPECT_EQ(list->currentRow(), QStringLiteral("bias"));
    EXPECT_EQ(list->currentPage(), StagePage::Bias);
    QTest::keyClick(rows, Qt::Key_Down);
    EXPECT_EQ(list->currentRow(), QStringLiteral("darks"));
    EXPECT_EQ(list->currentPage(), StagePage::Darks);
    EXPECT_TRUE(list->page(StagePage::Darks)->isVisibleTo(&process));
    QTest::keyClick(rows, Qt::Key_Up);
    EXPECT_EQ(list->currentRow(), QStringLiteral("bias"));

    QTest::keyClick(rows, Qt::Key_End);
    EXPECT_EQ(list->currentRow(), QStringLiteral("summary"));
    EXPECT_EQ(list->currentPage(), StagePage::Summary);

    list->list()->setSelectedId(QStringLiteral("registration"));
    QSignalSpy told(list, &StageNavigator::currentPageChanged);
    QTest::keyClick(rows, Qt::Key_Down);
    EXPECT_EQ(list->currentRow(), QStringLiteral("stacking"));
    EXPECT_EQ(list->currentPage(), StagePage::Stacking);
    EXPECT_TRUE(list->page(StagePage::Stacking)->isVisibleTo(&process));
    QTest::keyClick(rows, Qt::Key_Down);
    EXPECT_EQ(list->currentRow(), QStringLiteral("summary"));
    EXPECT_EQ(list->currentPage(), StagePage::Summary);
    EXPECT_EQ(told.count(), 2) << "two rows down, two pages";
}

// The list's foot holds the job's output folder - the field and the button the window's foot holds, the field at the
// foot's whole width under its label and the button, saying the whole path when pointed at - and the project file's
// acts: import, export, the recent projects, the presets and Reset Settings, each the process's own act or menu,
// enabled as it is. The window's foot, with its Close and Reset, is not shown on a page.
TEST_F(StageList, TheFootHoldsTheOutputFolderAndTheProjectFilesActs)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    ImageIntegrationProcess& process = openAsPage();
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr);
    QWidget* const foot = list->foot();
    ASSERT_NE(foot, nullptr);
    EXPECT_TRUE(list->column()->isAncestorOf(foot));
    EXPECT_EQ(list->column()->width(), an::ui::parts::kStageColumnWidth);

    auto* const edit = process.findChild<QLineEdit*>(QStringLiteral("outputDirectoryEdit"));
    auto* const browse = process.findChild<QPushButton*>(QStringLiteral("outputDirectoryBrowseButton"));
    ASSERT_TRUE(edit != nullptr && browse != nullptr);
    EXPECT_TRUE(foot->isAncestorOf(edit));
    EXPECT_TRUE(foot->isAncestorOf(browse));
    EXPECT_TRUE(edit->isVisibleTo(&process));
    process.model()->setOutputDirectory(folder.path());
    pump();
    EXPECT_EQ(QDir::cleanPath(edit->text()), QDir::cleanPath(folder.path())) << "the field is the job's output folder";
    // A path is long and the column narrow: the field has the foot's whole width, the button that chooses the folder
    // standing beside the label above it, and the field says the whole path when it is pointed at.
    const auto* const label = foot->findChild<QLabel*>(QStringLiteral("label"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(edit->width(), foot->width() - 2 * an::ui::parts::kStageColumnFootInset)
        << "the field is " << edit->width() << " wide in a foot of " << foot->width();
    const QRect buttonAt(browse->mapTo(foot, QPoint(0, 0)), browse->size());
    const QRect labelAt(label->mapTo(foot, QPoint(0, 0)), label->size());
    const QRect fieldAt(edit->mapTo(foot, QPoint(0, 0)), edit->size());
    EXPECT_LE(buttonAt.bottom(), fieldAt.top()) << "the button is above the field";
    EXPECT_LE(labelAt.bottom(), fieldAt.top()) << "and so is the label";
    EXPECT_GT(buttonAt.left(), labelAt.left()) << "the button at the label's right";
    EXPECT_TRUE(buttonAt.top() <= labelAt.bottom() && labelAt.top() <= buttonAt.bottom()) << "in the label's row";
    EXPECT_EQ(edit->toolTip(), QDir::toNativeSeparators(folder.path()));
    const auto* const windowsFoot = process.findChild<QWidget*>(QStringLiteral("bottomActionBar"));
    ASSERT_NE(windowsFoot, nullptr);
    EXPECT_FALSE(windowsFoot->isVisibleTo(&process)) << "Close and Reset are the header's and the list's foot's";

    const QList<QAbstractButton*> buttons = list->footButtons();
    ASSERT_EQ(buttons.size(), 5);
    QStringList texts;
    for (const QAbstractButton* button : buttons)
    {
        texts.append(button->text());
        EXPECT_TRUE(foot->isAncestorOf(button));
        EXPECT_TRUE(button->isVisibleTo(&process));
    }
    EXPECT_EQ(texts, (QStringList{QStringLiteral("Import..."), QStringLiteral("Export..."), QStringLiteral("Recent"),
                                  QStringLiteral("Presets"), QStringLiteral("Reset Settings...")}));

    // Import, Export and Reset Settings press the Project menu's own acts; each is counted in place of run.
    const std::array<std::pair<int, QString>, 3> acts = {{{0, QStringLiteral("Import Project...")},
                                                          {1, QStringLiteral("Export Project...")},
                                                          {4, QStringLiteral("Reset Settings...")}}};
    for (const auto& [button, text] : acts)
    {
        QAction* const act = action(text);
        ASSERT_NE(act, nullptr) << text.toStdString();
        int ran = 0;
        QObject::disconnect(act, &QAction::triggered, nullptr, nullptr);
        const QMetaObject::Connection counted =
            QObject::connect(act, &QAction::triggered, &process, [&ran]() { ++ran; });
        // The button was bound before the act's own slot was taken off: bind what it presses again.
        QObject::connect(buttons[button], &QAbstractButton::clicked, act, &QAction::trigger, Qt::UniqueConnection);
        EXPECT_EQ(buttons[button]->toolTip(), act->toolTip());
        buttons[button]->click();
        EXPECT_EQ(ran, 1) << text.toStdString();
        act->setEnabled(false);
        EXPECT_FALSE(buttons[button]->isEnabled()) << "a button is enabled as its act is";
        buttons[button]->click();
        EXPECT_EQ(ran, 1);
        act->setEnabled(true);
        EXPECT_TRUE(buttons[button]->isEnabled());
        QObject::disconnect(counted);
    }

    // The recent projects and the presets are the Project menu's own menus.
    const QAction* const recent = action(QStringLiteral("Open Recent"));
    const QAction* const presets = action(QStringLiteral("Presets"));
    ASSERT_TRUE(recent != nullptr && presets != nullptr);
    ASSERT_TRUE(recent->menu() != nullptr && presets->menu() != nullptr);
    auto* const recentButton = qobject_cast<QPushButton*>(buttons[2]);
    auto* const presetsButton = qobject_cast<QPushButton*>(buttons[3]);
    ASSERT_TRUE(recentButton != nullptr && presetsButton != nullptr);
    EXPECT_EQ(recentButton->menu(), recent->menu());
    EXPECT_EQ(presetsButton->menu(), presets->menu());
    QStringList offered;
    for (const QAction* entry : presetsButton->menu()->actions())
    {
        offered.append(entryText(*entry));
    }
    EXPECT_TRUE(offered.contains(QStringLiteral("Save Preset..."))) << offered.join(QStringLiteral(", ")).toStdString();
}

// On a page a side panel is an aside - the frame pages' and the calibration's - that opens at the aside's width, and
// the stacking page's execution options stand in two rows; in a window the panels keep their own widths and the
// options stand in one.
TEST_F(StageList, ASidePanelIsAnAsideAndTheStackingOptionsStandInTwoRows)
{
    const auto optionsAreInTwoRows = [](const ImageIntegrationProcess& process) {
        const auto* const first = process.findChild<QCheckBox*>(QStringLiteral("chkGenerate64Bit"));
        const auto* const moved = process.findChild<QCheckBox*>(QStringLiteral("chkSaveCalibratedFrames"));
        EXPECT_TRUE(first != nullptr && moved != nullptr);
        return process.findChild<QHBoxLayout*>(QStringLiteral("horizontalLayoutMachineOptions")) != nullptr;
    };
    {
        ImageIntegrationProcess& window = openAsWindow();
        ASSERT_FALSE(window.hasPageLayout());
        const auto* const lights = window.findChild<QWidget*>(QStringLiteral("lightTab"))->findChild<FrameSourceTab*>();
        ASSERT_NE(lights, nullptr);
        ASSERT_NE(lights->sidePanel(), nullptr);
        EXPECT_GT(lights->sidePanel()->minimumSizeHint().width(), an::ui::parts::kStageAsideWidth)
            << "a window's panel keeps its own least width";
        EXPECT_NE(lights->sidePanel()->maximumWidth(), an::ui::parts::kStageAsideWidth);
        EXPECT_FALSE(optionsAreInTwoRows(window));
        delete m_process.data();
        pump();
    }

    forgetTheAsideWidth();
    ImageIntegrationProcess& process = openAsPage();
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr);
    for (const StagePage page : {StagePage::Bias, StagePage::Darks, StagePage::Flats, StagePage::Lights})
    {
        list->setCurrentPage(page);
        pump();
        const auto* const source = list->page(page)->findChild<FrameSourceTab*>();
        ASSERT_NE(source, nullptr);
        ASSERT_NE(source->sidePanel(), nullptr);
        EXPECT_EQ(source->sidePanel()->width(), an::ui::parts::kStageAsideWidth) << static_cast<int>(page);
        EXPECT_TRUE(source->sidePanel()->isVisibleTo(&process));
        EXPECT_GT(source->width(), 2 * an::ui::parts::kStageAsideWidth) << "the frames keep the rest";
        // The aside stands at the page's right edge: the frames take every width the panel gave up.
        const int asideEnd = source->sidePanel()->mapTo(source, QPoint(source->sidePanel()->width(), 0)).x();
        EXPECT_GE(asideEnd, source->width() - 16)
            << "the aside ends at " << asideEnd << " of " << source->width() << ", page " << static_cast<int>(page);
    }
    list->setCurrentPage(StagePage::Calibration);
    pump();
    const auto* const calibration = list->page(StagePage::Calibration)->findChild<CalibrationTab*>();
    ASSERT_NE(calibration, nullptr);
    ASSERT_NE(calibration->sidePanel(), nullptr);
    EXPECT_EQ(calibration->sidePanel()->width(), an::ui::parts::kStageAsideWidth);

    list->setCurrentPage(StagePage::Stacking);
    pump();
    ASSERT_TRUE(optionsAreInTwoRows(process));
    const auto* const first = process.findChild<QCheckBox*>(QStringLiteral("chkGenerate64Bit"));
    const auto* const moved = process.findChild<QCheckBox*>(QStringLiteral("chkSaveCalibratedFrames"));
    ASSERT_TRUE(first->isVisibleTo(&process) && moved->isVisibleTo(&process));
    EXPECT_GT(moved->mapTo(&process, QPoint(0, 0)).y(), first->mapTo(&process, QPoint(0, 0)).y())
        << "what the run may use of the machine stands under what it writes";
    EXPECT_EQ(moved->parentWidget(), first->parentWidget()) << "in the same card";
}

// A person sizes the aside: dragged, it is wider or narrower within its least and its greatest width, and the preview
// it holds grows with it; the other pages' asides follow, the calibration's among them; and the width is the one the
// page opens with next time.
TEST_F(StageList, APersonSizesTheAsideWithinItsBoundsAndItIsKept)
{
    namespace parts = an::ui::parts;
    ASSERT_LT(parts::kStageAsideMinWidth, parts::kStageAsideWidth);
    ASSERT_LT(parts::kStageAsideWidth, parts::kStageAsideMaxWidth);
    forgetTheAsideWidth();
    const auto asideOf = [this](const StagePage page) -> QWidget* {
        StageListNavigator* const list = stages();
        list->setCurrentPage(page);
        pump();
        if (page == StagePage::Calibration)
        {
            const auto* const calibration = list->page(page)->findChild<CalibrationTab*>();
            return calibration != nullptr ? calibration->sidePanel() : nullptr;
        }
        const auto* const source = list->page(page)->findChild<FrameSourceTab*>();
        return source != nullptr ? source->sidePanel() : nullptr;
    };
    const auto handleOf = [](QWidget* const aside) -> QSplitterHandle* {
        auto* const splitter = aside != nullptr ? qobject_cast<QSplitter*>(aside->parentWidget()) : nullptr;
        return splitter != nullptr ? splitter->handle(splitter->indexOf(aside)) : nullptr;
    };
    {
        openAsPage();
        ASSERT_NE(stages(), nullptr);
        QWidget* const lights = asideOf(StagePage::Lights);
        ASSERT_NE(lights, nullptr);
        ASSERT_EQ(lights->width(), parts::kStageAsideWidth) << "it opens at the aside's width";
        QSplitterHandle* const handle = handleOf(lights);
        ASSERT_NE(handle, nullptr);
        ASSERT_TRUE(handle->isEnabled()) << "the line between the frames and the aside is dragged";

        // Wider by what the pointer moved.
        drag(*handle, -64);
        EXPECT_EQ(lights->width(), parts::kStageAsideWidth + 64);
        // The other pages' asides follow, each as it is shown.
        for (const StagePage page : {StagePage::Bias, StagePage::Darks, StagePage::Flats, StagePage::Calibration})
        {
            QWidget* const other = asideOf(page);
            ASSERT_NE(other, nullptr);
            EXPECT_EQ(other->width(), parts::kStageAsideWidth + 64) << static_cast<int>(page);
        }

        // No wider than its greatest, no narrower than its least; and the preview grows and shrinks with it.
        ASSERT_EQ(asideOf(StagePage::Lights), lights);
        const auto* const source = stages()->page(StagePage::Lights)->findChild<FrameSourceTab*>();
        ASSERT_NE(source->previewPanel(), nullptr);
        drag(*handle, -2000);
        EXPECT_EQ(lights->width(), parts::kStageAsideMaxWidth);
        const int tallest = source->previewPanel()->maximumHeight();
        drag(*handle, 2000);
        EXPECT_EQ(lights->width(), parts::kStageAsideMinWidth);
        EXPECT_LT(source->previewPanel()->maximumHeight(), tallest) << "the preview is as tall as its width lets it";

        // The calibration's aside is dragged as the frame pages' are, and they follow it.
        QWidget* const calibration = asideOf(StagePage::Calibration);
        ASSERT_EQ(calibration->width(), parts::kStageAsideMinWidth);
        QSplitterHandle* const calibrationHandle = handleOf(calibration);
        ASSERT_NE(calibrationHandle, nullptr);
        drag(*calibrationHandle, -32);
        EXPECT_EQ(calibration->width(), parts::kStageAsideMinWidth + 32);
        EXPECT_EQ(asideOf(StagePage::Lights)->width(), parts::kStageAsideMinWidth + 32);
        delete m_process.data();
        pump();
    }

    // The next page opens with the width it was left with.
    {
        openAsPage();
        ASSERT_NE(stages(), nullptr);
        QWidget* const lights = asideOf(StagePage::Lights);
        ASSERT_NE(lights, nullptr);
        EXPECT_EQ(lights->width(), parts::kStageAsideMinWidth + 32);
        EXPECT_EQ(asideOf(StagePage::Calibration)->width(), parts::kStageAsideMinWidth + 32);
        delete m_process.data();
        pump();
    }
    forgetTheAsideWidth();
}

// The page's least size is the arranged pages' from its first show: a page that was never shown - the stacking page,
// behind the page the process opens on - does not keep the least width it had before its options were laid in two
// rows, which would make the whole page scroll sideways until that page was visited.
TEST_F(StageList, ThePagesLeastSizeIsTheArrangedPagesFromItsFirstShow)
{
    ImageIntegrationProcess& process = openAsPage();
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr);
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_NE(page->fullArea(), nullptr);
    const QWidget* const contents = page->fullArea()->widget();
    ASSERT_NE(contents, nullptr);
    ASSERT_NE(list->currentPage(), StagePage::Stacking) << "the stacking page was not shown yet";
    const QSize atFirstShow = contents->minimumSizeHint();

    for (int each = 0; each < ImageIntegration::kStagePageCount; ++each)
    {
        list->setCurrentPage(static_cast<StagePage>(each));
        pump();
    }
    list->setCurrentPage(StagePage::Bias);
    pump();
    EXPECT_EQ(atFirstShow.width(), contents->minimumSizeHint().width())
        << "visiting the pages changed the page's least width";
}

// A frame shown in the lights' tree from the stacking page goes to the lights' page through the list: the page is
// found by what moves between the pages, not by a tab's name.
TEST_F(StageList, AFrameShownInTheLightsTreeGoesThereThroughTheList)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    ImageIntegrationProcess& process = openAsPage();
    StageListNavigator* const list = stages();
    ASSERT_NE(list, nullptr);
    const FrameInfo light = lightFrame(folder.path(), 0);
    process.model()->addFrames(FrameType::Light, {light, lightFrame(folder.path(), 1)});
    pump();
    list->setCurrentPage(StagePage::Stacking);
    pump();
    ASSERT_EQ(list->currentRow(), QStringLiteral("stacking"));
    auto* const dashboard = list->page(StagePage::Stacking)->findChild<FrameQualityDashboard*>();
    ASSERT_NE(dashboard, nullptr) << "the stacking page's frame quality";
    EXPECT_EQ(StageNavigator::of(dashboard), list) << "a part of a page finds what moves between the pages";

    dashboard->showInLightsTree(light.id.canonicalPath());
    pump();
    EXPECT_EQ(list->currentPage(), StagePage::Lights);
    EXPECT_EQ(list->currentRow(), QStringLiteral("lights"));
    EXPECT_TRUE(list->page(StagePage::Lights)->isVisibleTo(&process));
    ASSERT_EQ(process.model()->selectedFrameIds().size(), 1u);
    EXPECT_EQ(process.model()->selectedFrameIds().front().canonicalPath(), light.id.canonicalPath());
}

// In a window of its own the process is what it was: its two levels of tabs, with a frame source's frames in its
// tab's label and a page's issues in its badge, and its foot with the output folder, Close and Reset.
TEST_F(StageList, InAWindowTheTabsAndTheFootStay)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    ImageIntegrationProcess& window = openAsWindow();
    ASSERT_FALSE(window.hasPageLayout());
    EXPECT_EQ(stages(), nullptr);
    auto* const tabs = dynamic_cast<TabStageNavigator*>(window.stageNavigator());
    ASSERT_NE(tabs, nullptr);
    const auto* const root = window.findChild<QTabWidget*>(QStringLiteral("rootTabWidget"));
    const auto* const sources = window.findChild<QTabWidget*>(QStringLiteral("sourceTabWidget"));
    ASSERT_TRUE(root != nullptr && sources != nullptr);
    EXPECT_EQ(tabs->widget(), root);
    EXPECT_TRUE(root->isVisibleTo(&window));
    EXPECT_EQ(window.findChild<AnStageList*>(), nullptr) << "no list in a window";
    const auto* const foot = window.findChild<QWidget*>(QStringLiteral("bottomActionBar"));
    const auto* const edit = window.findChild<QLineEdit*>(QStringLiteral("outputDirectoryEdit"));
    ASSERT_TRUE(foot != nullptr && edit != nullptr);
    EXPECT_TRUE(foot->isVisibleTo(&window));
    EXPECT_TRUE(foot->isAncestorOf(edit));

    // The tabs are asked through the same seam: a page shown, and told.
    QSignalSpy told(tabs, &StageNavigator::currentPageChanged);
    tabs->setCurrentPage(StagePage::Registration);
    EXPECT_EQ(tabs->currentPage(), StagePage::Registration);
    EXPECT_EQ(root->currentIndex(), 1);
    EXPECT_EQ(told.count(), 1);
    EXPECT_EQ(StageNavigator::of(tabs->page(StagePage::Lights)), tabs);

    // A source's frames are in its tab's label.
    window.model()->addFrames(FrameType::Light, {lightFrame(folder.path(), 0), lightFrame(folder.path(), 1)});
    pump();
    EXPECT_EQ(sources->tabText(static_cast<int>(StagePage::Lights)), QStringLiteral("Lights (2)"));
}
