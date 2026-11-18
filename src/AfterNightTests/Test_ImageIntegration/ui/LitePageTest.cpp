// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration Lite as a page: it is taken into a page whole, with no column and no menus of its own; the page's
/// primary action is its Run Integration button; and a run it starts there is shown in the page - the run's own view
/// in the process's place, no Processing window over the application - going through the stages and ending as the run
/// its window starts does, the process back in its page once the view is closed. The runs are the suite's fixture
/// pipeline's, over lights that are real files, so the manifest is the one the process prepares.

#include "Test_ImageIntegration/ui/IntegrationPageCase.h"
#include "Test_ImageIntegration/workflow/FixturePipeline.h"
#include "Test_ImageIntegration/reporting/ReportScenarios.h"

#include "components/AnInfoBanner.h"
#include "components/AnStageList.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"
#include "processes/ImageIntegrationLiteProcess.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QGuiApplication>
#include <QLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace an::tests::integration
{
namespace
{

/// The descriptor Image Integration Lite is launched as.
constexpr char kImageIntegrationLite[] = "multi_image_stacking.actionImage_Integration_Lite";

/// Whether a scroll bar can move: the content is larger than the area along it.
[[nodiscard]] bool scrolls(const QScrollBar& bar)
{
    return bar.maximum() > bar.minimum();
}

/// The least size a page's contents take, as the page's area reads it: their least width, and at that width the
/// height they ask for where it follows the width - text that wraps - when that is more than their least height.
[[nodiscard]] QSize leastSizeOf(const QWidget& contents)
{
    QSize least = contents.minimumSizeHint();
    if (contents.layout() != nullptr && contents.layout()->hasHeightForWidth())
    {
        least.setHeight((std::max)(least.height(), contents.heightForWidth(least.width())));
    }
    return least;
}

/// A presenter that takes a process into a page of a host of its own - a stand-in for a shell's centre, kept off the
/// screen under a platform that would show it.
class PageHost final : public an::ui::IProcessPresenter
{
public:
    PageHost()
    {
        const QString platform = QGuiApplication::platformName();
        if (platform != QLatin1String("minimal") && platform != QLatin1String("offscreen"))
        {
            host.setAttribute(Qt::WA_DontShowOnScreen, true);
        }
        auto* const layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        // The centre a 1536 x 864 window leaves a page.
        host.resize(1492, 794);
    }

    bool adopt(TopLevelProcessBaseControl& process) override
    {
        if (!process.attachToPage(host))
        {
            return false;
        }
        host.show();
        return true;
    }
    void runStateChanged(TopLevelProcessBaseControl&, const an::ui::ProcessRunState&) override {}
    void targetChanged(TopLevelProcessBaseControl&, ImageViewControl*) override {}

    QWidget host;
};

/// What a run of the process left the person with: how it ended, the stages its view showed, and what the process
/// says of it.
struct LiteRunEnd
{
    an::ui::ProcessRunState::Phase phase = an::ui::ProcessRunState::Phase::Idle;

    /// Each stage by its title, and where it stood at the end. The time a stage took is the run's own.
    std::vector<std::pair<QString, an::ui::StageStatus>> stages;
    an::ui::AnInfoBanner::Tone tone = an::ui::AnInfoBanner::Tone::Info;
    QString said;
};

/// The stages a run's view shows, each by its title and where it stands.
[[nodiscard]] std::vector<std::pair<QString, an::ui::StageStatus>> stagesOf(const WorkflowRunView& view)
{
    std::vector<std::pair<QString, an::ui::StageStatus>> stages;
    for (const an::ui::StageRow& row : view.stageRows())
    {
        stages.emplace_back(row.title, row.status);
    }
    return stages;
}

/// The cases of Image Integration Lite as a page: each in an application context of its own, over lights it writes
/// into a folder of its own.
class LitePageCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());
        ASSERT_TRUE(m_folder.isValid());
    }

    void TearDown() override
    {
        if (m_gate)
        {
            m_gate->open();
        }
        if (!m_process.isNull())
        {
            (void)pumpUntil([this]() { return !m_process->runState().running(); });
        }
        delete m_process.data();
        m_scope.context().presenter = nullptr;
        pump();
    }

    /// Makes the process as its launch makes it - its descriptor stating a full page - and shows it: in the case's
    /// centre as a page, or as the window it is where no presenter takes it.
    ImageIntegrationLiteProcess& open(const bool asPage)
    {
        delete m_process.data();
        m_scope.context().presenter = asPage ? &m_page : nullptr;
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = kImageIntegrationLite;
        descriptor.surface = an::ui::ProcessSurface::FullPage;
        {
            const an::ui::ProcessLaunchScope launch(descriptor);
            m_process = new ImageIntegrationLiteProcess(nullptr);
        }
        m_process->setAttribute(Qt::WA_DeleteOnClose, false);
        m_process->show();
        pump();
        return *m_process;
    }

    /// Two lights of one filter, written as real files, and the folder a run writes to: the process is ready to run.
    void giveItLights(ImageIntegrationLiteProcess& process, const QString& output)
    {
        std::vector<FrameInfo> lights;
        for (int index = 0; index < 2; ++index)
        {
            const QString file = QDir(m_folder.path()).filePath(QStringLiteral("light_%1.fit").arg(index));
            if (!QFileInfo::exists(file))
            {
                (void)report::writeLightFrameWithMetadata(file, 120.0, 800, false, 10, QStringLiteral("M 42"),
                                                          QString(), QString(), QString());
            }
            lights.emplace_back(FrameId(file), file,
                                FrameMetadata(FrameType::Light, QSize(16, 10), 120.0, -10.0, 800, 10,
                                              QStringLiteral("L"), QStringLiteral("M 42"),
                                              QDateTime::currentDateTimeUtc(), QString()));
        }
        ASSERT_TRUE(QDir().mkpath(output));
        process.model()->setOutputDirectory(output);
        process.model()->addFrames(FrameType::Light, lights);
        if (auto* const openResults = process.findChild<QCheckBox*>(QStringLiteral("liteOpenResultsCheck")))
        {
            openResults->setChecked(false);
        }
        pump();
    }

    [[nodiscard]] static QPushButton* runButton(const ImageIntegrationLiteProcess& process)
    {
        return process.findChild<QPushButton*>(QStringLiteral("liteRunIntegrationButton"));
    }

    [[nodiscard]] static an::ui::AnInfoBanner* banner(const ImageIntegrationLiteProcess& process)
    {
        return dynamic_cast<an::ui::AnInfoBanner*>(process.findChild<QWidget*>(QStringLiteral("liteStatusBanner")));
    }

    /// Closes every message box shown: the Processing window says how a run ended in one.
    static void closeMessageBoxes()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* const box = qobject_cast<QMessageBox*>(widget); box != nullptr && box->isVisible())
            {
                box->close();
            }
        }
        pump();
    }

    QTemporaryDir m_folder;
    ScopedProcessWindowContext m_scope;
    PageHost m_page;
    std::shared_ptr<RunGate> m_gate;
    QPointer<ImageIntegrationLiteProcess> m_process;
};

class PageLayout : public LitePageCase
{
};

class LitePage : public LitePageCase
{
};

} // namespace

// Image Integration Lite is its page whole: what its window laid out in the page's area, no column, no menus of its
// own, and no row for a run at the page's head, since it shows its run in itself. The page scrolls only where the
// centre is smaller than the least the process takes, and its primary action is the Run Integration button. The case
// records the least size (`leastWidth`, `leastHeight`), the interface's when started under the native platform.
TEST_F(PageLayout, ImageIntegrationLiteFillsItsPage)
{
    ImageIntegrationLiteProcess& process = open(true);
    EXPECT_EQ(process.processSurface(), an::ui::ProcessSurface::FullPage);
    ASSERT_TRUE(process.isPresentedAsPage());
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    QScrollArea* const area = page->fullArea();
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(page->column(), nullptr);
    EXPECT_TRUE(page->menus().isEmpty()) << "no menus of its own, so its header shows no menu button";
    EXPECT_TRUE(process.findChildren<QMenuBar*>().isEmpty());
    EXPECT_EQ(page->runBanner(), nullptr) << "it shows its run in itself";
    EXPECT_EQ(page->cancelButton(), nullptr);
    EXPECT_TRUE(m_page.host.isAncestorOf(&process));
    EXPECT_EQ(page->page()->size(), m_page.host.size()) << "the page fills the centre";

    int inTheArea = 0;
    for (QAbstractButton* button : process.findChildren<QAbstractButton*>())
    {
        if (!button->isVisibleTo(&process))
        {
            continue;
        }
        EXPECT_TRUE(area->isAncestorOf(button)) << "'" << button->text().toStdString() << "' lies outside the area";
        ++inTheArea;
    }
    EXPECT_GT(inTheArea, 0);

    // Nothing to integrate: the run button is not offered, and the primary action does nothing.
    QPushButton* const run = runButton(process);
    ASSERT_NE(run, nullptr);
    EXPECT_TRUE(area->isAncestorOf(run));
    ASSERT_FALSE(run->isEnabled());
    EXPECT_FALSE(process.triggerPrimaryAction());
    EXPECT_FALSE(process.runState().running());

    // The page scrolls only where the centre is smaller than the least the process takes: a centre as large holds it
    // whole, and a smaller one scrolls to the rest, the process keeping its least size.
    const QSize least = leastSizeOf(*area->widget());
    ASSERT_TRUE(least.isValid());
    RecordProperty("leastWidth", least.width());
    RecordProperty("leastHeight", least.height());
    m_page.host.resize(least);
    pump();
    EXPECT_FALSE(scrolls(*area->horizontalScrollBar()))
        << "a centre of the process's least size, " << least.width() << " x " << least.height() << ", holds it whole";
    EXPECT_FALSE(scrolls(*area->verticalScrollBar()))
        << "a centre of the process's least size, " << least.width() << " x " << least.height() << ", holds it whole";
    m_page.host.resize(least.width() / 2, least.height() / 2);
    pump();
    EXPECT_TRUE(scrolls(*area->horizontalScrollBar())) << "a smaller centre scrolls sideways";
    EXPECT_TRUE(scrolls(*area->verticalScrollBar())) << "and up and down";
    EXPECT_GE(area->widget()->width(), least.width()) << "the process keeps its least size";
}

// A run started on the page goes as the run its window starts: through the same stages, to the same end, the process
// saying the same of it. In the window the Processing window shows it; on the page the run's view takes the process's
// place - no window opens over the application - and the process is back once the view is closed.
TEST_F(LitePage, AppliesAsInAWindow)
{
    LiteRunEnd inWindow;
    {
        ImageIntegrationLiteProcess& process = open(false);
        ASSERT_FALSE(process.isPresentedAsPage());
        giveItLights(process, QDir(m_folder.path()).filePath(QStringLiteral("from-window")));
        m_gate = std::make_shared<RunGate>();
        process.setRunPipeline(fixturePipelineWithAStack(m_gate));
        QPushButton* const run = runButton(process);
        ASSERT_NE(run, nullptr);
        ASSERT_TRUE(run->isEnabled());
        run->click();
        const QPointer<WorkflowProgressDialogV2> dialog = process.findChild<WorkflowProgressDialogV2*>();
        ASSERT_FALSE(dialog.isNull()) << "a window's run is shown by the Processing window";
        m_gate->open();
        ASSERT_TRUE(pumpUntil([&process]() { return process.runState().ended(); }, 30000));
        pump();
        closeMessageBoxes();
        ASSERT_FALSE(dialog.isNull());
        inWindow.phase = process.runState().phase;
        inWindow.stages = stagesOf(*dialog->runView());
        ASSERT_NE(banner(process), nullptr);
        inWindow.tone = banner(process)->tone();
        inWindow.said = banner(process)->message();
        dialog->close();
        pump();
    }
    ASSERT_EQ(inWindow.phase, an::ui::ProcessRunState::Phase::Succeeded)
        << inWindow.said.toStdString() << " - the fixture run ends well";
    ASSERT_FALSE(inWindow.stages.empty());

    ImageIntegrationLiteProcess& process = open(true);
    ASSERT_TRUE(process.isPresentedAsPage());
    giveItLights(process, QDir(m_folder.path()).filePath(QStringLiteral("from-page")));
    m_gate = std::make_shared<RunGate>();
    process.setRunPipeline(fixturePipelineWithAStack(m_gate));
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(runButton(process)->isEnabled());
    EXPECT_EQ(process.runView(), nullptr);

    ASSERT_TRUE(process.triggerPrimaryAction()) << "the page's primary action is Run Integration";
    const QPointer<WorkflowRunView> view = process.runView();
    ASSERT_FALSE(view.isNull()) << "a page shows the run in itself";
    EXPECT_EQ(process.findChild<WorkflowProgressDialogV2*>(), nullptr) << "no Processing window on a page";
    EXPECT_TRUE(m_page.host.isAncestorOf(view.data()));
    EXPECT_TRUE(view->isVisibleTo(&process));
    EXPECT_FALSE(page->fullArea()->isVisibleTo(&process)) << "the run's view takes the process's place";
    EXPECT_TRUE(process.runState().running());
    EXPECT_FALSE(process.triggerPrimaryAction()) << "not while a run is on its way";

    m_gate->open();
    ASSERT_TRUE(pumpUntil([&process]() { return process.runState().ended(); }, 30000));
    pump();
    ASSERT_FALSE(view.isNull()) << "the view stays, with how the run ended, until it is closed";
    EXPECT_TRUE(view->isFinished());
    EXPECT_EQ(process.runState().phase, inWindow.phase);
    EXPECT_EQ(stagesOf(*view), inWindow.stages) << "the same stages, each ended the same way";
    ASSERT_NE(banner(process), nullptr);
    EXPECT_EQ(banner(process)->tone(), inWindow.tone);
    EXPECT_EQ(banner(process)->message(), inWindow.said);

    // Close on the view gives the page back to the process.
    auto* const close = view->findChild<QAbstractButton*>(QStringLiteral("btnClose"));
    ASSERT_NE(close, nullptr);
    ASSERT_FALSE(close->isHidden());
    close->click();
    pump();
    EXPECT_EQ(process.runView(), nullptr);
    EXPECT_TRUE(page->fullArea()->isVisibleTo(&process));
    EXPECT_TRUE(runButton(process)->isVisibleTo(&process));
}

// The page's Cancel - Esc's, and a shell's - stops a run on its way through the run's own view: the run ends stopped.
TEST_F(LitePage, ARunOnThePageIsCancelledThroughItsView)
{
    ImageIntegrationLiteProcess& process = open(true);
    giveItLights(process, QDir(m_folder.path()).filePath(QStringLiteral("cancelled")));
    m_gate = std::make_shared<RunGate>();
    process.setRunPipeline(fixturePipelineWithAStack(m_gate));
    EXPECT_FALSE(process.cancelRun()) << "nothing to stop before a run";
    ASSERT_TRUE(process.triggerPrimaryAction());
    ASSERT_NE(process.runView(), nullptr);
    ASSERT_TRUE(process.runState().running());

    EXPECT_TRUE(process.cancelRun());
    ASSERT_TRUE(pumpUntil([&process]() { return process.runState().ended(); }, 30000));
    EXPECT_EQ(process.runState().phase, an::ui::ProcessRunState::Phase::Cancelled);
    ASSERT_NE(process.runView(), nullptr);
    EXPECT_TRUE(process.runView()->isFinished());
}

} // namespace an::tests::integration
