// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QDateTime>
#include <QSize>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest/QTest>

#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

namespace
{
FrameInfo createTestLight(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 60.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("TestObject"), QDateTime::currentDateTimeUtc(), QString());

    return FrameInfo(FrameId(path), path, meta, QStringLiteral("TestSession"));
}

FrameTreeView* findFrameTreeUnder(QWidget* root)
{
    if (!root)
    {
        return nullptr;
    }
    const auto trees = root->findChildren<FrameTreeView*>();
    return trees.isEmpty() ? nullptr : trees.first();
}
} // namespace

// The window registers itself and keeps its settings in an application context of the case's own.
class ShowInLightsTreeActionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());

        process = new ImageIntegrationProcess(nullptr);
        ASSERT_NE(process, nullptr);

        model = process->model();
        ASSERT_NE(model, nullptr);

        model->reset();
        QApplication::processEvents();

        rootTabs = process->findChild<QTabWidget*>(QStringLiteral("rootTabWidget"));
        ASSERT_NE(rootTabs, nullptr);

        workflowTabs = process->findChild<QTabWidget*>(QStringLiteral("workflowTabWidget"));
        ASSERT_NE(workflowTabs, nullptr);

        sourceTabs = process->findChild<QTabWidget*>(QStringLiteral("sourceTabWidget"));
        ASSERT_NE(sourceTabs, nullptr);

        lightTab = process->findChild<QWidget*>(QStringLiteral("lightTab"));
        ASSERT_NE(lightTab, nullptr);

        lightTree = findFrameTreeUnder(lightTab);
        ASSERT_NE(lightTree, nullptr);

        const auto dashboards = process->findChildren<FrameQualityDashboard*>();
        ASSERT_FALSE(dashboards.isEmpty());
        dashboard = dashboards.first();
        ASSERT_NE(dashboard, nullptr);

        process->show();
        QApplication::processEvents();
    }

    void TearDown() override
    {
        delete process;
        process = nullptr;
    }

    an::tests::integration::ScopedProcessWindowContext m_scope;
    ImageIntegrationProcess* process = nullptr;
    IntegrationDataModel* model = nullptr;
    FrameQualityDashboard* dashboard = nullptr;
    QTabWidget* rootTabs = nullptr;
    QTabWidget* workflowTabs = nullptr;
    QTabWidget* sourceTabs = nullptr;
    QWidget* lightTab = nullptr;
    FrameTreeView* lightTree = nullptr;
};

TEST_F(ShowInLightsTreeActionTest, ContextMenuNavigatesToLightsTree)
{
    const FrameInfo light = createTestLight(QStringLiteral("C:/images/light_show_tree.fit"));
    model->addFrames(FrameType::Light, {light});

    FrameQualityMetrics metrics{};
    metrics.stars = 100;
    metrics.fwhm = 2.2;
    metrics.snr = 25.0;
    metrics.eccentricity = 0.45;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(light.id, metrics);

    const QString canonical = light.id.canonicalPath();
    dashboard->setFrameIdFilter({canonical});
    dashboard->refresh();

    QApplication::processEvents();

    auto* table = dashboard->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_GT(table->rowCount(), 0);

    rootTabs->setCurrentIndex(1); // Workflow
    workflowTabs->setCurrentIndex(2); // Stacking

    QApplication::processEvents();

    table->selectRow(0);
    table->scrollToItem(table->item(0, 0));

    dashboard->showInLightsTree(canonical);
    QTest::qWait(50);
    QApplication::processEvents();

    EXPECT_EQ(rootTabs->currentIndex(), 0); // Frame Sources
    EXPECT_EQ(sourceTabs->currentIndex(), static_cast<int>(FrameType::Light));

    QStringList selectedPaths = lightTree->selectedFilePaths();
    EXPECT_TRUE(selectedPaths.contains(canonical)) << selectedPaths.join(", ").toStdString();
}
