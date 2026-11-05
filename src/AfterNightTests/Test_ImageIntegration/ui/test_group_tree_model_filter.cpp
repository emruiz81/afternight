// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/FrameTreeFilter.h"
#include "processes/ImageIntegration/model/FrameId.h"

#include <QDateTime>
#include <QTimeZone>

namespace
{
FrameInfo makeLight(const QString& name)
{
    const QString path = QStringLiteral("C:/test/%1").arg(name);
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

TEST(GroupTreeModel, AppliesFilter)
{
    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    std::vector<FrameInfo> frames;
    frames.reserve(100);
    for (int i = 1; i <= 100; ++i)
    {
        frames.push_back(makeLight(QStringLiteral("frame_%1.fit").arg(i, 2, 10, QLatin1Char('0'))));
    }
    model.addFrames(FrameType::Light, frames);

    FrameTreeFilter filter;
    filter.setSearchText(QStringLiteral("frame_01"));
    treeModel.setFilter(filter);

    EXPECT_LT(treeModel.visibleFrameCount(), 100);
}

TEST(GroupTreeModel, FilterShowsMatchCount)
{
    IntegrationDataModel model;
    GroupTreeModel treeModel(FrameType::Light, &model);

    std::vector<FrameInfo> frames;
    frames.reserve(100);
    for (int i = 1; i <= 100; ++i)
    {
        frames.push_back(makeLight(QStringLiteral("frame_%1.fit").arg(i, 2, 10, QLatin1Char('0'))));
    }
    model.addFrames(FrameType::Light, frames);

    FrameTreeFilter filter;
    filter.setSearchText(QStringLiteral("frame_0")); // Matches frame_01 through frame_09
    treeModel.setFilter(filter);

    EXPECT_EQ(treeModel.matchCount(), 9);
    EXPECT_EQ(treeModel.totalCount(), 100);
}
