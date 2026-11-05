// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>

#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include <QDateTime>
#include <QTimeZone>

namespace
{
FrameInfo makeLight(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

// The window registers itself and keeps its settings in an application context of the case's own.
class ProjectManagerIntegration : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());
    }

    an::tests::integration::ScopedProcessWindowContext m_scope;
};

TEST_F(ProjectManagerIntegration, WindowTitleIncludesProjectName)
{
    ImageIntegrationProcess process(nullptr);
    process.setProjectName(QStringLiteral("M42 Winter 2025"));
    EXPECT_TRUE(process.windowTitle().contains(QStringLiteral("M42 Winter 2025")));
}

TEST_F(ProjectManagerIntegration, WindowTitleShowsModifiedIndicator)
{
    ImageIntegrationProcess process(nullptr);
    process.setProjectName(QStringLiteral("Test"));

    ASSERT_TRUE(process.model() != nullptr);
    process.model()->addFrames(FrameType::Light, {makeLight(QStringLiteral("C:/test/light1.fit"))});

    EXPECT_TRUE(process.windowTitle().contains(QLatin1Char('*')));
}
