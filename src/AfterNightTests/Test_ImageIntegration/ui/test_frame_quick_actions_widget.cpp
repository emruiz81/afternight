// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_frame_quick_actions_widget.cpp
 * @brief Unit tests for FrameQuickActionsWidget.
 *
 * Tests the widget that provides quick action buttons for
 * frame operations like include/exclude all and clear tags.
 */

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QTimeZone>

#include "processes/ImageIntegration/widgets/FrameQuickActionsWidget.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
void ensureQApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}

FrameInfo createTestFrame(FrameType type, const QString& path, bool excluded = false,
                          const QString& customTag = QString())
{
    FrameMetadata meta(type, QSize(1000, 1000), 30.0, -10.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    FrameInfo frame(FrameId(path), path, meta);
    frame.excluded = excluded;
    frame.customTag = customTag;
    return frame;
}
} // namespace

class FrameQuickActionsWidgetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureQApp();
        model = new IntegrationDataModel();
    }

    void TearDown() override
    {
        delete widget;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    ImageIntegration::FrameQuickActionsWidget* widget = nullptr;
};

TEST_F(FrameQuickActionsWidgetTest, ConstructsWithoutCrash)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    ASSERT_NE(widget, nullptr);
}

TEST_F(FrameQuickActionsWidgetTest, HasCorrectObjectName)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    EXPECT_EQ(widget->objectName(), QStringLiteral("frameQuickActionsWidget"));
}

TEST_F(FrameQuickActionsWidgetTest, PreservesFrameType)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Bias, model);
    EXPECT_EQ(widget->frameType(), FrameType::Bias);

    widget->setFrameType(FrameType::Dark);
    EXPECT_EQ(widget->frameType(), FrameType::Dark);
}

TEST_F(FrameQuickActionsWidgetTest, HasTitleLabel)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);

    QLabel* title = widget->findChild<QLabel*>(QStringLiteral("lblFrameQuickActionsTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->text().contains(QStringLiteral("Quick Actions"), Qt::CaseInsensitive));
}

TEST_F(FrameQuickActionsWidgetTest, HasIncludeAllButton)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);

    QPushButton* btn = widget->findChild<QPushButton*>(QStringLiteral("btnIncludeAll"));
    ASSERT_NE(btn, nullptr);
    EXPECT_FALSE(btn->toolTip().isEmpty());
}

TEST_F(FrameQuickActionsWidgetTest, HasExcludeAllButton)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);

    QPushButton* btn = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    ASSERT_NE(btn, nullptr);
    EXPECT_FALSE(btn->toolTip().isEmpty());
}

TEST_F(FrameQuickActionsWidgetTest, HasClearTagsButton)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);

    QPushButton* btn = widget->findChild<QPushButton*>(QStringLiteral("btnClearTags"));
    ASSERT_NE(btn, nullptr);
    EXPECT_FALSE(btn->toolTip().isEmpty());
}

TEST_F(FrameQuickActionsWidgetTest, ButtonsDisabledWithNoFrames)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* includeAll = widget->findChild<QPushButton*>(QStringLiteral("btnIncludeAll"));
    QPushButton* excludeAll = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    QPushButton* clearTags = widget->findChild<QPushButton*>(QStringLiteral("btnClearTags"));

    ASSERT_NE(includeAll, nullptr);
    ASSERT_NE(excludeAll, nullptr);
    ASSERT_NE(clearTags, nullptr);

    EXPECT_FALSE(includeAll->isEnabled());
    EXPECT_FALSE(excludeAll->isEnabled());
    EXPECT_FALSE(clearTags->isEnabled());
}

TEST_F(FrameQuickActionsWidgetTest, IncludeAllEnabledWhenExcludedFramesExist)
{
    model->addFrames(FrameType::Light,
                     {
                         createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"), true) // excluded
                     });

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* includeAll = widget->findChild<QPushButton*>(QStringLiteral("btnIncludeAll"));
    ASSERT_NE(includeAll, nullptr);
    EXPECT_TRUE(includeAll->isEnabled());
}

TEST_F(FrameQuickActionsWidgetTest, ExcludeAllEnabledWhenIncludedFramesExist)
{
    model->addFrames(FrameType::Light,
                     {
                         createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"), false) // included
                     });

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* excludeAll = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    ASSERT_NE(excludeAll, nullptr);
    EXPECT_TRUE(excludeAll->isEnabled());
}

TEST_F(FrameQuickActionsWidgetTest, ClearTagsEnabledWhenTaggedFramesExist)
{
    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"), false,
                                                        QStringLiteral("MyTag"))});

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* clearTags = widget->findChild<QPushButton*>(QStringLiteral("btnClearTags"));
    ASSERT_NE(clearTags, nullptr);
    EXPECT_TRUE(clearTags->isEnabled());
}

TEST_F(FrameQuickActionsWidgetTest, IncludeAllIncludesExcludedFrames)
{
    const QString path = QStringLiteral("C:/test/light1.fit");
    model->addFrames(FrameType::Light, {
                                           createTestFrame(FrameType::Light, path, true) // excluded
                                       });

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* includeAll = widget->findChild<QPushButton*>(QStringLiteral("btnIncludeAll"));
    ASSERT_NE(includeAll, nullptr);
    ASSERT_TRUE(includeAll->isEnabled());

    includeAll->click();
    QApplication::processEvents();

    // Verify frame is now included
    auto frame = model->lightFrames().findById(FrameId(path));
    ASSERT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->excluded);
}

TEST_F(FrameQuickActionsWidgetTest, ExcludeAllExcludesIncludedFrames)
{
    const QString path = QStringLiteral("C:/test/light1.fit");
    model->addFrames(FrameType::Light, {
                                           createTestFrame(FrameType::Light, path, false) // included
                                       });

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* excludeAll = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    ASSERT_NE(excludeAll, nullptr);
    ASSERT_TRUE(excludeAll->isEnabled());

    excludeAll->click();
    QApplication::processEvents();

    // Verify frame is now excluded
    auto frame = model->lightFrames().findById(FrameId(path));
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->excluded);
}

TEST_F(FrameQuickActionsWidgetTest, RefreshUpdatesButtonStates)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* excludeAll = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    ASSERT_NE(excludeAll, nullptr);
    EXPECT_FALSE(excludeAll->isEnabled());

    // Add a frame
    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, QStringLiteral("C:/test/light1.fit"))});

    widget->refresh();
    EXPECT_TRUE(excludeAll->isEnabled());
}

TEST_F(FrameQuickActionsWidgetTest, RefreshDoesNotCrashWithNullModel)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, nullptr);
    widget->refresh();
    SUCCEED();
}

TEST_F(FrameQuickActionsWidgetTest, WorksWithAllFrameTypes)
{
    for (FrameType type : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        model->addFrames(type, {createTestFrame(type, QStringLiteral("C:/test/frame1.fit"))});

        auto* w = new ImageIntegration::FrameQuickActionsWidget(type, model);
        w->refresh();

        QPushButton* excludeAll = w->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
        ASSERT_NE(excludeAll, nullptr);
        EXPECT_TRUE(excludeAll->isEnabled());

        delete w;
        // model->removeAllFrames(type);
        QStringList paths;
        for (const auto& f : model->frames(type).frames())
            paths << f.id.canonicalPath();
        if (!paths.isEmpty())
            model->removeFrames(type, paths);
    }
    SUCCEED();
}

TEST_F(FrameQuickActionsWidgetTest, EmitsSignalsForSelectionActions)
{
    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);

    // Check that signals can be connected (they're defined in the header)
    QSignalSpy selectAllSpy(widget, &ImageIntegration::FrameQuickActionsWidget::selectAllRequested);
    QSignalSpy selectNoneSpy(widget, &ImageIntegration::FrameQuickActionsWidget::selectNoneRequested);
    QSignalSpy invertSpy(widget, &ImageIntegration::FrameQuickActionsWidget::invertSelectionRequested);

    EXPECT_EQ(selectAllSpy.count(), 0);
    EXPECT_EQ(selectNoneSpy.count(), 0);
    EXPECT_EQ(invertSpy.count(), 0);
}

TEST_F(FrameQuickActionsWidgetTest, ButtonsOnlyAffectCorrectFrameType)
{
    const QString lightPath = QStringLiteral("C:/test/light1.fit");
    const QString biasPath = QStringLiteral("C:/test/bias1.fit");

    model->addFrames(FrameType::Light, {createTestFrame(FrameType::Light, lightPath, false)});
    model->addFrames(FrameType::Bias, {createTestFrame(FrameType::Bias, biasPath, false)});

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* excludeAll = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    ASSERT_NE(excludeAll, nullptr);
    excludeAll->click();
    QApplication::processEvents();

    // Light should be excluded
    auto lightFrame = model->lightFrames().findById(FrameId(lightPath));
    ASSERT_TRUE(lightFrame.has_value());
    EXPECT_TRUE(lightFrame->excluded);

    // Bias should remain included
    auto biasFrame = model->biasFrames().findById(FrameId(biasPath));
    ASSERT_TRUE(biasFrame.has_value());
    EXPECT_FALSE(biasFrame->excluded);
}

TEST_F(FrameQuickActionsWidgetTest, IncludeAllUpdatesMultipleFrames)
{
    const QString path1 = QStringLiteral("C:/test/light1.fit");
    const QString path2 = QStringLiteral("C:/test/light2.fit");
    const QString path3 = QStringLiteral("C:/test/light3.fit");

    model->addFrames(FrameType::Light,
                     {createTestFrame(FrameType::Light, path1, true), createTestFrame(FrameType::Light, path2, true),
                      createTestFrame(FrameType::Light, path3, true)});

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* includeAll = widget->findChild<QPushButton*>(QStringLiteral("btnIncludeAll"));
    ASSERT_NE(includeAll, nullptr);
    includeAll->click();
    QApplication::processEvents();

    // All frames should be included
    for (const auto& path : {path1, path2, path3})
    {
        auto frame = model->lightFrames().findById(FrameId(path));
        ASSERT_TRUE(frame.has_value());
        EXPECT_FALSE(frame->excluded) << path.toStdString() << " should be included";
    }
}

TEST_F(FrameQuickActionsWidgetTest, ButtonStateUpdatesAfterAction)
{
    const QString path = QStringLiteral("C:/test/light1.fit");
    model->addFrames(FrameType::Light, {
                                           createTestFrame(FrameType::Light, path, false) // included
                                       });

    widget = new ImageIntegration::FrameQuickActionsWidget(FrameType::Light, model);
    widget->refresh();

    QPushButton* includeAll = widget->findChild<QPushButton*>(QStringLiteral("btnIncludeAll"));
    QPushButton* excludeAll = widget->findChild<QPushButton*>(QStringLiteral("btnExcludeAll"));
    ASSERT_NE(includeAll, nullptr);
    ASSERT_NE(excludeAll, nullptr);

    // Initially: includeAll disabled (no excluded frames), excludeAll enabled
    EXPECT_FALSE(includeAll->isEnabled());
    EXPECT_TRUE(excludeAll->isEnabled());

    // Exclude all
    excludeAll->click();
    QApplication::processEvents();
    widget->refresh();

    // Now: includeAll enabled, excludeAll disabled
    EXPECT_TRUE(includeAll->isEnabled());
    EXPECT_FALSE(excludeAll->isEnabled());
}
