// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A picture's Night form: every pixel its luma in red with its alpha kept, made off the GUI thread - on the thread pool
// when the GUI thread asks, on the asking thread otherwise - and kept per picture, so a picture is mapped once whatever
// number of paints ask for it. A counter of the passes made on each kind of thread proves the GUI thread did no pixel
// work.

#include "theme/NightImage.h"

#include <QColor>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QImage>
#include <QObject>
#include <QThread>

#include <gtest/gtest.h>

#include <memory>
#include <thread>

namespace
{

/// A two by two picture with a pixel of each primary and one half-transparent white.
QImage primaries(const QImage::Format format = QImage::Format_ARGB32)
{
    QImage picture(2, 2, format);
    picture.setPixelColor(0, 0, QColor(255, 0, 0));
    picture.setPixelColor(1, 0, QColor(0, 255, 0));
    picture.setPixelColor(0, 1, QColor(0, 0, 255));
    picture.setPixelColor(1, 1, QColor(255, 255, 255, 128));
    return picture;
}

/// Pumps the GUI thread's events until `done` holds or two seconds pass.
template <typename Condition> bool pumpUntil(const Condition& done)
{
    const QDeadlineTimer deadline(2000);
    while (!done() && !deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return done();
}

} // namespace

// The mapping on known pixels: each primary becomes its share of the luma in red, the alpha stays, and the size and the
// device pixel ratio are the picture's.
TEST(NightImage, TheMappingOnKnownPixels)
{
    QImage picture = primaries();
    picture.setDevicePixelRatio(2.0);
    const QImage night = an::ui::nightForm(picture);
    ASSERT_EQ(night.size(), picture.size());
    EXPECT_DOUBLE_EQ(night.devicePixelRatio(), 2.0);
    EXPECT_EQ(night.pixelColor(0, 0), QColor(76, 0, 0));
    EXPECT_EQ(night.pixelColor(1, 0), QColor(150, 0, 0));
    EXPECT_EQ(night.pixelColor(0, 1), QColor(29, 0, 0));
    const QColor translucent = night.pixelColor(1, 1);
    EXPECT_EQ(translucent.alpha(), 128) << "the alpha is kept";
    EXPECT_EQ(translucent.green(), 0);
    EXPECT_EQ(translucent.blue(), 0);
    EXPECT_NEAR(translucent.red(), 255, 2) << "a translucent pixel keeps its luma";

    // A grey picture is shown in red too, and an opaque one stays opaque.
    QImage grey(1, 1, QImage::Format_Grayscale8);
    grey.setPixelColor(0, 0, QColor(90, 90, 90));
    EXPECT_EQ(an::ui::nightForm(grey).pixelColor(0, 0), QColor(90, 0, 0));
    EXPECT_FALSE(an::ui::nightForm(primaries(QImage::Format_RGB32)).hasAlphaChannel());
}

// Asked on the GUI thread, the form is made on the thread pool: the call answers nothing at first, the caller is told
// on the GUI thread when the form is kept, and asked again it answers the kept form without mapping again. The GUI
// thread maps no pixel.
TEST(NightImage, IsMadeOffTheGuiThreadAndKeptPerPicture)
{
    an::ui::clearNightImages();
    const an::ui::testing::NightPasses before = an::ui::testing::nightPasses();
    const QImage picture = primaries();
    auto context = std::make_unique<QObject>();
    int told = 0;
    bool toldOnTheGuiThread = false;

    const QImage first = an::ui::nightImage(picture, context.get(), [&]() {
        ++told;
        toldOnTheGuiThread = QThread::currentThread() == QCoreApplication::instance()->thread();
    });
    EXPECT_TRUE(first.isNull()) << "the GUI thread was handed a form it would have had to make";
    ASSERT_TRUE(pumpUntil([&]() { return told > 0; })) << "the caller was never told the form is kept";
    EXPECT_EQ(told, 1);
    EXPECT_TRUE(toldOnTheGuiThread);

    const QImage kept = an::ui::nightImage(picture, context.get(), [&]() { ++told; });
    ASSERT_FALSE(kept.isNull());
    EXPECT_EQ(kept, an::ui::nightForm(picture));
    EXPECT_EQ(an::ui::nightImage(picture), kept) << "a second paint maps again";

    const an::ui::testing::NightPasses after = an::ui::testing::nightPasses();
    // The comparison above made one form on this thread itself; the kept one was made elsewhere.
    EXPECT_EQ(after.onTheGuiThread - before.onTheGuiThread, 1u);
    EXPECT_EQ(after.elsewhere - before.elsewhere, 1u) << "the picture was mapped more than once";
    EXPECT_EQ(told, 1);
    EXPECT_EQ(an::ui::testing::keptNightImages(), 1u);

    // Another picture is mapped on its own.
    QImage other = picture;
    other.setPixelColor(0, 0, QColor(10, 20, 30));
    EXPECT_TRUE(an::ui::nightImage(other).isNull());
    ASSERT_TRUE(pumpUntil([&]() { return an::ui::testing::keptNightImages() == 2u; }));
    EXPECT_EQ(an::ui::testing::nightPasses().elsewhere - before.elsewhere, 2u);
    EXPECT_EQ(an::ui::testing::nightPasses().onTheGuiThread - before.onTheGuiThread, 1u);
    an::ui::clearNightImages();
}

// A caller that is gone before the form is kept is not told: the form is kept all the same.
TEST(NightImage, ACallerGoneIsNotTold)
{
    an::ui::clearNightImages();
    const QImage picture = primaries();
    int told = 0;
    auto context = std::make_unique<QObject>();
    EXPECT_TRUE(an::ui::nightImage(picture, context.get(), [&]() { ++told; }).isNull());
    context.reset();
    ASSERT_TRUE(pumpUntil([&]() { return an::ui::testing::keptNightImages() == 1u; }));
    QCoreApplication::processEvents();
    EXPECT_EQ(told, 0);
    an::ui::clearNightImages();
}

// Asked on another thread, the form is made there and answered at once.
TEST(NightImage, OnAnotherThreadItIsMadeThere)
{
    an::ui::clearNightImages();
    const an::ui::testing::NightPasses before = an::ui::testing::nightPasses();
    const QImage picture = primaries();
    QImage answered;
    std::thread worker([&]() { answered = an::ui::nightImage(picture); });
    worker.join();
    ASSERT_FALSE(answered.isNull());
    EXPECT_EQ(answered.pixelColor(1, 0), QColor(150, 0, 0));
    EXPECT_EQ(an::ui::testing::nightPasses().elsewhere - before.elsewhere, 1u);
    EXPECT_EQ(an::ui::testing::nightPasses().onTheGuiThread, before.onTheGuiThread);
    EXPECT_EQ(an::ui::nightImage(picture), answered) << "the form made there is kept";
    an::ui::clearNightImages();
}
