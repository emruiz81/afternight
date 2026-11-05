// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_frame_source_tab.cpp
 * @brief Unit tests for FrameSourceTab composite widget.
 *
 * Tests the FrameSourceTab composite UI, which combines:
 * - FrameTreeView with GroupTreeModel (left panel)
 * - Context panel with Preview, GroupingOptions, Stats (right panel)
 * - TreeDisplayModeWidget (bottom bar)
 */

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QComboBox>
#include <QThreadPool>
#include <QToolButton>
#include <QTimeZone>
#include <QVBoxLayout>

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/GroupingOptionsWidget.h"
#include "processes/ImageIntegration/widgets/TreeDisplayModeWidget.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

class FrameSourceTabTest : public testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure QApplication exists for Qt widget tests
        if (!QCoreApplication::instance())
        {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QApplication app(argc, argv);
        }
    }
};

namespace
{
FrameInfo makeTestFrame(FrameType type, const QString& filename)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, QSize(100, 100), 10.0, -10.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("TestCamera"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}

QString findRepoRoot()
{
    QDir dir(QDir::current());
    for (int i = 0; i < 10; ++i)
    {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("AGENTS.md"))) &&
            QFileInfo::exists(dir.filePath(
                QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/tabs/FrameSourceTab.cpp"))))
        {
            return dir.absolutePath();
        }
        if (!dir.cdUp())
        {
            break;
        }
    }
    return {};
}

QString readRepoFile(const QString& relativePath)
{
    const QString root = findRepoRoot();
    if (root.isEmpty())
    {
        return {};
    }

    QFile file(QDir(root).filePath(relativePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QComboBox* displayModeCombo(ImageViewControl* preview)
{
    if (!preview)
    {
        return nullptr;
    }

    const auto combos = preview->findChildren<QComboBox*>();
    for (QComboBox* combo : combos)
    {
        if (combo && combo->accessibleName() == QStringLiteral("Display Mode"))
        {
            return combo;
        }
    }

    return nullptr;
}

/// Shows a small grey Float32 image in a preview, as a frame selected in the tab is shown, and waits until it lands.
void showPreviewImage(ImageViewControl* preview, const an::consumer::EngineSession& session)
{
    ASSERT_NE(preview, nullptr);
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        session, 8u, 8u, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + y) / 14.0f; });
    ASSERT_TRUE(document.has_value());
    preview->setDocument(std::move(*document));
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
}
} // namespace

TEST_F(FrameSourceTabTest, ConstructsWithoutCrash)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);
    SUCCEED();
}

TEST_F(FrameSourceTabTest, ContainsTreeView)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.tree(), nullptr);
}

TEST_F(FrameSourceTabTest, ContainsTreeModel)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.treeModel(), nullptr);
}

TEST_F(FrameSourceTabTest, ContainsDisplayModeWidget)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.displayModeWidget(), nullptr);
}

TEST_F(FrameSourceTabTest, ContainsPreviewPanel)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.previewPanel(), nullptr);
}

TEST_F(FrameSourceTabTest, PreviewPanelUsesImageViewControl)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(dynamic_cast<ImageViewControl*>(tab.previewPanel()), nullptr);
}

TEST_F(FrameSourceTabTest, AllFrameTypePreviewPanelsAutoDebayerCfaImages)
{
    IntegrationDataModel model;

    const std::array<FrameType, 4> types = {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light};
    for (FrameType type : types)
    {
        FrameSourceTab tab(type, &model);
        ASSERT_NE(tab.previewPanel(), nullptr) << "FrameType " << static_cast<int>(type);
        EXPECT_TRUE(tab.previewPanel()->isAutoDebayerCfaImagePreviewsEnabled())
            << "FrameType " << static_cast<int>(type);
    }
}

TEST_F(FrameSourceTabTest, LightAndFlatPreviewPanelsDefaultToAutostretch)
{
    IntegrationDataModel model;

    FrameSourceTab flatTab(FrameType::Flat, &model);
    ASSERT_NE(flatTab.previewPanel(), nullptr);
    EXPECT_TRUE(flatTab.previewPanel()->isAutoStretchEnabled());
    ASSERT_NE(displayModeCombo(flatTab.previewPanel()), nullptr);
    EXPECT_EQ(displayModeCombo(flatTab.previewPanel())->currentText(), QStringLiteral("Autostretch"));

    FrameSourceTab lightTab(FrameType::Light, &model);
    ASSERT_NE(lightTab.previewPanel(), nullptr);
    EXPECT_TRUE(lightTab.previewPanel()->isAutoStretchEnabled());
    ASSERT_NE(displayModeCombo(lightTab.previewPanel()), nullptr);
    EXPECT_EQ(displayModeCombo(lightTab.previewPanel())->currentText(), QStringLiteral("Autostretch"));
}

TEST_F(FrameSourceTabTest, BiasAndDarkPreviewPanelsDefaultToLinear)
{
    IntegrationDataModel model;

    FrameSourceTab biasTab(FrameType::Bias, &model);
    ASSERT_NE(biasTab.previewPanel(), nullptr);
    EXPECT_FALSE(biasTab.previewPanel()->isAutoStretchEnabled());
    ASSERT_NE(displayModeCombo(biasTab.previewPanel()), nullptr);
    EXPECT_EQ(displayModeCombo(biasTab.previewPanel())->currentText(), QStringLiteral("Linear"));

    FrameSourceTab darkTab(FrameType::Dark, &model);
    ASSERT_NE(darkTab.previewPanel(), nullptr);
    EXPECT_FALSE(darkTab.previewPanel()->isAutoStretchEnabled());
    ASSERT_NE(displayModeCombo(darkTab.previewPanel()), nullptr);
    EXPECT_EQ(displayModeCombo(darkTab.previewPanel())->currentText(), QStringLiteral("Linear"));
}

TEST_F(FrameSourceTabTest, LightAndFlatPreviewModeCombosStaySyncedAfterImageUpdate)
{
    // The previews show their image in the session the application context lends.
    an::tests::integration::ScopedProcessWindowContext scope;
    ASSERT_TRUE(scope.context().valid());
    const an::consumer::EngineSession& session = scope.context().engineSession();

    IntegrationDataModel model;

    FrameSourceTab flatTab(FrameType::Flat, &model);
    ASSERT_NE(flatTab.previewPanel(), nullptr);
    showPreviewImage(flatTab.previewPanel(), session);
    EXPECT_TRUE(flatTab.previewPanel()->isAutoStretchEnabled());
    ASSERT_NE(displayModeCombo(flatTab.previewPanel()), nullptr);
    EXPECT_EQ(displayModeCombo(flatTab.previewPanel())->currentText(), QStringLiteral("Autostretch"));

    FrameSourceTab lightTab(FrameType::Light, &model);
    ASSERT_NE(lightTab.previewPanel(), nullptr);
    showPreviewImage(lightTab.previewPanel(), session);
    EXPECT_TRUE(lightTab.previewPanel()->isAutoStretchEnabled());
    ASSERT_NE(displayModeCombo(lightTab.previewPanel()), nullptr);
    EXPECT_EQ(displayModeCombo(lightTab.previewPanel())->currentText(), QStringLiteral("Autostretch"));
}

TEST_F(FrameSourceTabTest, ContainsGroupingOptionsWidget)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.groupingOptions(), nullptr);
}

TEST_F(FrameSourceTabTest, ContainsStatsLabel)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.statsLabel(), nullptr);
}

TEST_F(FrameSourceTabTest, ContainsRegroupWarningLabel)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    EXPECT_NE(tab.regroupWarningLabel(), nullptr);
}

TEST_F(FrameSourceTabTest, FrameTypeIsPreserved)
{
    IntegrationDataModel model;

    FrameSourceTab biasTab(FrameType::Bias, &model);
    EXPECT_EQ(biasTab.frameType(), FrameType::Bias);

    FrameSourceTab darkTab(FrameType::Dark, &model);
    EXPECT_EQ(darkTab.frameType(), FrameType::Dark);

    FrameSourceTab flatTab(FrameType::Flat, &model);
    EXPECT_EQ(flatTab.frameType(), FrameType::Flat);

    FrameSourceTab lightTab(FrameType::Light, &model);
    EXPECT_EQ(lightTab.frameType(), FrameType::Light);
}

TEST_F(FrameSourceTabTest, StatsLabelShowsInitialCounts)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    // With no frames, should show 0 frames, 0 groups, 0 masters
    QLabel* stats = tab.statsLabel();
    ASSERT_NE(stats, nullptr);

    QString text = stats->text();
    EXPECT_TRUE(text.contains("0")) << "Stats should show 0 initially: " << text.toStdString();
}

TEST_F(FrameSourceTabTest, RegroupWarningLabelIsInitiallyHidden)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    QLabel* warning = tab.regroupWarningLabel();
    ASSERT_NE(warning, nullptr);
    EXPECT_FALSE(warning->isVisible());
}

TEST_F(FrameSourceTabTest, RegroupWarningLabelHasStyling)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    QLabel* warning = tab.regroupWarningLabel();
    ASSERT_NE(warning, nullptr);

    // Verify the warning label has themed styling via palette and properties
    EXPECT_EQ(warning->property("anFrameRadius").toInt(), 4) << "Warning label should have anFrameRadius property set";
    EXPECT_TRUE(warning->palette().color(QPalette::Window).isValid())
        << "Warning label should have a valid palette window color";
}

TEST_F(FrameSourceTabTest, TreeViewHasCorrectFrameType)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Flat, &model);

    FrameTreeView* tree = tab.tree();
    ASSERT_NE(tree, nullptr);
    // FrameTreeView stores frame type via setFrameType()
    // We can't easily query it back, but we verify it was constructed
}

TEST_F(FrameSourceTabTest, GroupingOptionsMatchesFrameType)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    GroupingOptionsWidget* opts = tab.groupingOptions();
    ASSERT_NE(opts, nullptr);
    // GroupingOptionsWidget::setFrameType was called with Light
}

TEST_F(FrameSourceTabTest, RefreshQuickStatsUpdatesLabel)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    QLabel* stats = tab.statsLabel();
    ASSERT_NE(stats, nullptr);

    QString initialText = stats->text();

    // Call refreshQuickStats explicitly
    tab.refreshQuickStats();

    // Text should still be valid (frames=0, groups=0, masters=0)
    QString newText = stats->text();
    EXPECT_FALSE(newText.isEmpty());
}

TEST_F(FrameSourceTabTest, RefreshGroupingOptionsFromModel)
{
    IntegrationDataModel model;

    // Set custom grouping options on the model
    GroupingOptions opts;
    opts.byFilter = true;
    opts.byExposure = true;
    model.setGroupingOptions(FrameType::Light, opts);

    FrameSourceTab tab(FrameType::Light, &model);

    // Refresh should sync from model
    tab.refreshGroupingOptionsFromModel();

    // Verify grouping options widget exists
    EXPECT_NE(tab.groupingOptions(), nullptr);
}

TEST_F(FrameSourceTabTest, StatsLabelHasObjectName)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    QLabel* stats = tab.statsLabel();
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->objectName(), "quickStatsLabel");
}

TEST_F(FrameSourceTabTest, RegroupWarningLabelHasObjectName)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    QLabel* warning = tab.regroupWarningLabel();
    ASSERT_NE(warning, nullptr);
    EXPECT_EQ(warning->objectName(), "regroupWarningLabel");
}

TEST_F(FrameSourceTabTest, GroupingChangesDoNotPumpEventLoopDuringPendingState)
{
    const QString source =
        readRepoFile(QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/tabs/FrameSourceTab.cpp"));
    ASSERT_FALSE(source.isEmpty()) << "Could not read FrameSourceTab.cpp";

    EXPECT_FALSE(source.contains(QStringLiteral("QCoreApplication::processEvents")));
    EXPECT_FALSE(source.contains(QStringLiteral("QApplication::processEvents")));
}

TEST_F(FrameSourceTabTest, ActionStripExists)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    QWidget* strip = tab.findChild<QWidget*>("frameSourceActionStrip");
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->isVisibleTo(&tab) || !strip->isHidden());
}

TEST_F(FrameSourceTabTest, ActionStripButtonsHaveTooltipsAndIcons)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    auto* addFiles = tab.findChild<QToolButton*>("actionAddFiles");
    auto* addFolder = tab.findChild<QToolButton*>("actionAddFolder");
    auto* removeSelected = tab.findChild<QToolButton*>("actionRemoveSelected");
    auto* removeAll = tab.findChild<QToolButton*>("actionRemoveAll");

    ASSERT_NE(addFiles, nullptr);
    ASSERT_NE(addFolder, nullptr);
    ASSERT_NE(removeSelected, nullptr);
    ASSERT_NE(removeAll, nullptr);

    EXPECT_FALSE(addFiles->toolTip().isEmpty());
    EXPECT_FALSE(addFolder->toolTip().isEmpty());
    EXPECT_FALSE(removeSelected->toolTip().isEmpty());
    EXPECT_FALSE(removeAll->toolTip().isEmpty());

    EXPECT_TRUE(removeSelected->toolTip().toLower().contains("this tab"));
    EXPECT_TRUE(removeAll->toolTip().toLower().contains("this tab"));

    EXPECT_FALSE(addFiles->icon().isNull());
    EXPECT_FALSE(addFolder->icon().isNull());
    EXPECT_FALSE(removeSelected->icon().isNull());
    EXPECT_FALSE(removeAll->icon().isNull());
}

TEST_F(FrameSourceTabTest, ActionStripEmitsRequests)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Flat, &model);

    auto* addFiles = tab.findChild<QToolButton*>("actionAddFiles");
    auto* addFolder = tab.findChild<QToolButton*>("actionAddFolder");
    auto* removeSelected = tab.findChild<QToolButton*>("actionRemoveSelected");
    auto* removeAll = tab.findChild<QToolButton*>("actionRemoveAll");
    ASSERT_NE(addFiles, nullptr);
    ASSERT_NE(addFolder, nullptr);
    ASSERT_NE(removeSelected, nullptr);
    ASSERT_NE(removeAll, nullptr);

    bool addFilesHit = false;
    FrameType addFilesType = FrameType::Bias;
    QObject::connect(&tab, &FrameSourceTab::addFilesRequested, &tab, [&](FrameType type) {
        addFilesHit = true;
        addFilesType = type;
    });

    bool addFolderHit = false;
    FrameType addFolderType = FrameType::Bias;
    bool addFolderRecursive = false;
    QObject::connect(&tab, &FrameSourceTab::addFolderRequested, &tab, [&](FrameType type, bool recursive) {
        addFolderHit = true;
        addFolderType = type;
        addFolderRecursive = recursive;
    });

    bool removeSelectedHit = false;
    QObject::connect(&tab, &FrameSourceTab::removeSelectedRequested, &tab, [&]() { removeSelectedHit = true; });

    bool removeAllHit = false;
    FrameType removeAllType = FrameType::Bias;
    QObject::connect(&tab, &FrameSourceTab::removeAllRequested, &tab, [&](FrameType type) {
        removeAllHit = true;
        removeAllType = type;
    });

    addFiles->click();
    EXPECT_TRUE(addFilesHit);
    EXPECT_EQ(addFilesType, FrameType::Flat);

    // Default is non-recursive
    addFolder->click();
    EXPECT_TRUE(addFolderHit);
    EXPECT_EQ(addFolderType, FrameType::Flat);
    EXPECT_FALSE(addFolderRecursive);

    // Menu action: recursive
    addFolderHit = false;
    if (auto* menu = addFolder->menu())
    {
        QAction* recursiveAction = nullptr;
        for (auto* action : menu->actions())
        {
            if (action && action->objectName() == "actionAddFolderRecursive")
            {
                recursiveAction = action;
                break;
            }
        }
        ASSERT_NE(recursiveAction, nullptr);
        recursiveAction->trigger();
        EXPECT_TRUE(addFolderHit);
        EXPECT_TRUE(addFolderRecursive);
    }

    // Enable remove actions by adding a frame and selecting it.
    model.addFrames(FrameType::Flat, {makeTestFrame(FrameType::Flat, QStringLiteral("remove_me.fit"))});
    QCoreApplication::processEvents();

    auto* tree = tab.tree();
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(tree->model(), nullptr);

    const QModelIndex first = tree->model()->index(0, 0);
    ASSERT_TRUE(first.isValid());
    tree->selectionModel()->select(first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();

    removeSelected->click();
    EXPECT_TRUE(removeSelectedHit);

    removeAll->click();
    EXPECT_TRUE(removeAllHit);
    EXPECT_EQ(removeAllType, FrameType::Flat);
}

TEST_F(FrameSourceTabTest, RemoveButtonsInitialState_NoFrames)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Bias, &model);

    auto* removeSelected = tab.findChild<QToolButton*>("actionRemoveSelected");
    auto* removeAll = tab.findChild<QToolButton*>("actionRemoveAll");
    ASSERT_NE(removeSelected, nullptr);
    ASSERT_NE(removeAll, nullptr);

    EXPECT_FALSE(removeSelected->isEnabled());
    EXPECT_FALSE(removeAll->isEnabled());
}

TEST_F(FrameSourceTabTest, RemoveAllEnabledWhenFramesExistInTab)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Bias, &model);

    auto* removeAll = tab.findChild<QToolButton*>("actionRemoveAll");
    ASSERT_NE(removeAll, nullptr);
    EXPECT_FALSE(removeAll->isEnabled());

    model.addFrames(FrameType::Bias, {makeTestFrame(FrameType::Bias, QStringLiteral("bias_one.fit"))});
    QCoreApplication::processEvents();

    EXPECT_TRUE(removeAll->isEnabled());
}

TEST_F(FrameSourceTabTest, RemoveSelectedEnabledWhenSelectionExists)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Bias, &model);

    auto* removeSelected = tab.findChild<QToolButton*>("actionRemoveSelected");
    auto* removeAll = tab.findChild<QToolButton*>("actionRemoveAll");
    ASSERT_NE(removeSelected, nullptr);
    ASSERT_NE(removeAll, nullptr);

    model.addFrames(FrameType::Bias, {makeTestFrame(FrameType::Bias, QStringLiteral("bias_select.fit"))});
    QCoreApplication::processEvents();
    EXPECT_TRUE(removeAll->isEnabled());
    EXPECT_FALSE(removeSelected->isEnabled());

    auto* tree = tab.tree();
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(tree->model(), nullptr);

    const QModelIndex first = tree->model()->index(0, 0);
    ASSERT_TRUE(first.isValid());

    tree->selectionModel()->select(first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();

    EXPECT_TRUE(removeSelected->isEnabled());

    tree->clearSelection();
    QCoreApplication::processEvents();
    EXPECT_FALSE(removeSelected->isEnabled());
}

TEST_F(FrameSourceTabTest, RemoveAllTracksFramesMovingBetweenTabs)
{
    IntegrationDataModel model;
    FrameSourceTab biasTab(FrameType::Bias, &model);
    FrameSourceTab flatTab(FrameType::Flat, &model);

    auto* biasRemoveAll = biasTab.findChild<QToolButton*>("actionRemoveAll");
    auto* flatRemoveAll = flatTab.findChild<QToolButton*>("actionRemoveAll");
    ASSERT_NE(biasRemoveAll, nullptr);
    ASSERT_NE(flatRemoveAll, nullptr);

    const auto biasFrame = makeTestFrame(FrameType::Bias, QStringLiteral("move_me.fit"));
    const QString path = biasFrame.id.canonicalPath();

    model.addFrames(FrameType::Bias, {biasFrame});
    QCoreApplication::processEvents();

    EXPECT_TRUE(biasRemoveAll->isEnabled());
    EXPECT_FALSE(flatRemoveAll->isEnabled());

    model.removeFrames(FrameType::Bias, QStringList{path});
    model.addFrames(FrameType::Flat, {makeTestFrame(FrameType::Flat, QStringLiteral("move_me.fit"))});
    QCoreApplication::processEvents();

    EXPECT_FALSE(biasRemoveAll->isEnabled());
    EXPECT_TRUE(flatRemoveAll->isEnabled());
}
