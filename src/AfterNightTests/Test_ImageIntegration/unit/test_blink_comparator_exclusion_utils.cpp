// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QDateTime>
#include <QKeyEvent>
#include <QUndoStack>

#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{

void ensureApp()
{
    if (QApplication::instance())
    {
        return;
    }

    static int argc = 0;
    static char* argv[] = {nullptr};
    static QApplication app(argc, argv);
    (void)app;
}

FrameInfo createFrame(const FrameType type, const QString& path)
{
    FrameMetadata metadata(type, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, QStringLiteral("L"),
                           QStringLiteral("M42"), QDateTime::currentDateTimeUtc(), QStringLiteral(""));
    return FrameInfo(FrameId(path), path, metadata);
}

} // namespace

TEST(BlinkComparatorExclusionUtils, BuildExclusionChangesCanonicalizesDeduplicatesAndSkipsNoops)
{
    IntegrationDataModel model;

    const QString lightA = QStringLiteral("C:/lights/light_a.fit");
    const QString lightB = QStringLiteral("C:/lights/light_b.fit");
    const QString darkA = QStringLiteral("C:/darks/dark_a.fit");

    model.addFrames(FrameType::Light, {createFrame(FrameType::Light, lightA), createFrame(FrameType::Light, lightB)});
    model.addFrames(FrameType::Dark, {createFrame(FrameType::Dark, darkA)});
    model.updateFrameAttribute(lightB, FrameAttribute::Excluded, true);

    const auto changes = ImageIntegration::blink::buildExclusionChanges(
        &model, FrameType::Light,
        {QStringLiteral("C:/lights/./light_a.fit"), QStringLiteral("C:/lights/light_a.fit"),
         QStringLiteral("C:/lights/light_b.fit"), QStringLiteral("C:/darks/dark_a.fit"),
         QStringLiteral("C:/lights/missing.fit")});

    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes.front().path, FrameId(lightA).canonicalPath());
    EXPECT_EQ(changes.front().oldValue.toBool(), false);
    EXPECT_EQ(changes.front().newValue.toBool(), true);
}

TEST(BlinkComparatorExclusionUtils, ApplyExclusionsUsesUndoStackWhenAvailable)
{
    IntegrationDataModel model;
    QUndoStack undoStack;

    const QString lightA = QStringLiteral("C:/lights/light_a.fit");
    model.addFrames(FrameType::Light, {createFrame(FrameType::Light, lightA)});

    EXPECT_TRUE(ImageIntegration::blink::applyExclusions(&model, &undoStack, FrameType::Light, {lightA}));

    auto frame = model.frames(FrameType::Light).findById(FrameId(lightA));
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->excluded);
    EXPECT_EQ(undoStack.count(), 1);

    undoStack.undo();
    frame = model.frames(FrameType::Light).findById(FrameId(lightA));
    ASSERT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->excluded);

    undoStack.redo();
    frame = model.frames(FrameType::Light).findById(FrameId(lightA));
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->excluded);
}

TEST(BlinkComparatorExclusionUtils, RatingsRemainDialogLocalWhenNoExclusionsAreApplied)
{
    ensureApp();

    IntegrationDataModel model;
    const QString lightA = QStringLiteral("C:/lights/light_a.fit");
    model.addFrames(FrameType::Light, {createFrame(FrameType::Light, lightA)});

    BlinkComparatorDialog dialog({lightA}, nullptr, false);
    QKeyEvent ratingEvent(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &ratingEvent);

    EXPECT_EQ(dialog.ratingFor(0), 5);
    EXPECT_FALSE(ImageIntegration::blink::applyExclusions(&model, nullptr, FrameType::Light,
                                                          dialog.comparisonResult().excludedPaths));

    const auto frame = model.frames(FrameType::Light).findById(FrameId(lightA));
    ASSERT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->excluded);
}
