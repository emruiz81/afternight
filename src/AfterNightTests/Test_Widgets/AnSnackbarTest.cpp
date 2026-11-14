// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A transient message: a raised bar of the message colour, a large control high, its corners the controls' radius,
// standing on its elevation; an optional icon, the text, a detail in the monospace and one action in the accent drawn
// over an image. It leaves after its lifetime, read from a clock a case can set, and stays while the pointer is over
// it. The action is a button to assistive technology; the message is named by its text and its detail.

#include "RenderTestSupport.h"

#include "components/AnSnackbar.h"
#include "theme/ComponentParts.h"
#include "theme/Elevation.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QSignalSpy>
#include <QTest>

#include <chrono>

namespace
{

using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::logicalPixels;
using an::tests::widgets::pumpFor;
using an::tests::widgets::pumpUntil;
using an::tests::widgets::recordedText;
using an::tests::widgets::RenderedPicture;
using an::ui::AnSnackbar;
using an::ui::DesignTokens;
using namespace an::ui::parts;
using namespace std::chrono_literals;

void fill(AnSnackbar& bar)
{
    bar.setText(QStringLiteral("Curves Transformation applied"));
    bar.setDetail(QStringLiteral("2.8 s"));
    bar.setActionText(QStringLiteral("Undo"));
}

void sendEnter(QWidget& widget)
{
    QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), QPointF(4, 4));
    QApplication::sendEvent(&widget, &enter);
}

void sendLeave(QWidget& widget)
{
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&widget, &leave);
}

} // namespace

// The body is a large control high in both densities and holds its parts with their gaps; the widget is the body and
// the room its shadow is drawn in.
TEST(AnSnackbar, SizeIsTheBodyAndTheRoomOfItsShadow)
{
    forEachLook([](const DesignTokens& t) {
        AnSnackbar bar;
        fill(bar);
        an::tests::widgets::showOffScreen(bar);
        const QMargins shadow = an::ui::elevationWindowMargins();
        EXPECT_EQ(bar.shadowMargins(), shadow);
        bar.resize(bar.sizeHint());
        const QRect body = bar.bodyRect();
        EXPECT_EQ(body, bar.rect().marginsRemoved(shadow));
        EXPECT_EQ(body.height(), t.metrics.controlLarge);
        const an::ui::testing::RecordedText text = recordedText(bar, QStringLiteral("applied"));
        const an::ui::testing::RecordedText detail = recordedText(bar, QStringLiteral("2.8 s"));
        ASSERT_FALSE(text.text.isEmpty());
        ASSERT_FALSE(detail.text.isEmpty());
        EXPECT_EQ(logicalPixels(text.font), t.metrics.fontBody);
        EXPECT_EQ(logicalPixels(detail.font), t.metrics.fontCaption);
        EXPECT_EQ(detail.font.families(), t.typography.monospace.families());
        QAbstractButton* const action = bar.actionButton();
        ASSERT_NE(action, nullptr);
        EXPECT_TRUE(body.contains(action->geometry()));
        EXPECT_EQ(body.right() - action->geometry().right(), kSnackbarPaddingEnd);
        EXPECT_EQ(body.width(), kSnackbarPaddingStart + textWidth(text.font, text.text) + kSnackbarGap +
                                    textWidth(detail.font, detail.text) + kSnackbarGap + action->sizeHint().width() +
                                    kSnackbarPaddingEnd);
        EXPECT_EQ(action->geometry().center().y(), body.center().y());
    });
}

// The body is the message colour with its corners rounded, the text in the message's text, the detail in it at its
// share and the action in the accent drawn over an image, semibold.
TEST(AnSnackbar, PaintsTheBodyTheTextAndTheAction)
{
    forEachLook([](const DesignTokens& t) {
        const QColor bg = t.layers.surface0;
        AnSnackbar bar;
        fill(bar);
        an::tests::widgets::showOffScreen(bar);
        bar.resize(bar.sizeHint());
        const QRect body = bar.bodyRect();
        forEachRatio(bar, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(body.left() + 4, body.center().y()), an::ui::compositeOver(t.colors.bgSnack, bg),
                         "the body");
            EXPECT_FALSE(an::ui::testing::sameColour(picture.at(body.left() + 0.3, body.top() + 0.3),
                                                     an::ui::compositeOver(t.colors.bgSnack, bg), 2))
                << "the corner is rounded";
        });
        const an::ui::testing::RecordedText text = recordedText(bar, QStringLiteral("applied"));
        expectColour(text.colour, t.colors.textOnSnack, "the text", 0);
        const an::ui::testing::RecordedText detail = recordedText(bar, QStringLiteral("2.8 s"));
        expectColour(an::ui::compositeOver(atShare(detail.colour, detail.opacity), t.colors.bgSnack),
                     an::ui::compositeOver(atShare(t.colors.textOnSnack, kSnackbarDetailOpacity), t.colors.bgSnack),
                     "the detail at its share");
        const an::ui::testing::RecordedText action = recordedText(bar, QStringLiteral("Undo"));
        expectColour(action.colour, t.colors.accentOnImage, "the action", 0);
        EXPECT_EQ(action.font.weight(), QFont::DemiBold);
    });
}

// Presented, the message stays its lifetime and leaves; the pointer over it holds what is left, and it leaves once the
// rest has passed after the pointer went. Every reading is from the clock the case sets.
TEST(AnSnackbar, LeavesAfterItsLifetimeAndStaysWhileHovered)
{
    std::chrono::milliseconds now{1000};
    QWidget host;
    auto* const bar = new AnSnackbar(&host);
    fill(*bar);
    bar->setClock([&now] { return now; });
    EXPECT_EQ(bar->lifetime(), AnSnackbar::kDefaultLifetime);
    bar->setLifetime(4000ms);
    int dismissed = 0;
    QObject::connect(bar, &AnSnackbar::dismissed, [&dismissed] { ++dismissed; });

    bar->present();
    EXPECT_FALSE(bar->isHidden());
    EXPECT_EQ(bar->remainingLifetime(), 4000ms);
    now += 3990ms;
    pumpFor(300);
    EXPECT_FALSE(bar->isHidden()) << "ten milliseconds are left";
    EXPECT_EQ(bar->remainingLifetime(), 10ms);

    sendEnter(*bar);
    EXPECT_TRUE(bar->isPaused());
    now += 60000ms;
    pumpFor(300);
    EXPECT_FALSE(bar->isHidden()) << "the pointer holds it";
    EXPECT_EQ(bar->remainingLifetime(), 10ms);
    EXPECT_EQ(dismissed, 0);

    sendLeave(*bar);
    EXPECT_FALSE(bar->isPaused());
    now += 5ms;
    pumpFor(300);
    EXPECT_FALSE(bar->isHidden()) << "five milliseconds are left";
    now += 5ms;
    EXPECT_TRUE(pumpUntil([bar] { return bar->isHidden(); }));
    EXPECT_EQ(dismissed, 1);
    EXPECT_EQ(bar->remainingLifetime(), 0ms);

    // Presented again, it starts a whole lifetime.
    bar->present();
    EXPECT_EQ(bar->remainingLifetime(), 4000ms);
    now += 4000ms;
    EXPECT_TRUE(pumpUntil([bar] { return bar->isHidden(); }));
    EXPECT_EQ(dismissed, 2);
}

// The action tells its press and the message leaves; Esc dismisses it.
TEST(AnSnackbar, TheActionIsToldAndTheMessageLeaves)
{
    QWidget host;
    auto* const bar = new AnSnackbar(&host);
    fill(*bar);
    bar->resize(bar->sizeHint());
    QSignalSpy triggered(bar, &AnSnackbar::actionTriggered);
    QSignalSpy dismissed(bar, &AnSnackbar::dismissed);
    bar->present();
    QTest::mouseClick(bar->actionButton(), Qt::LeftButton);
    EXPECT_EQ(triggered.count(), 1);
    EXPECT_EQ(dismissed.count(), 1);
    EXPECT_TRUE(bar->isHidden());

    bar->present();
    QTest::keyClick(bar, Qt::Key_Escape);
    EXPECT_TRUE(bar->isHidden());
    EXPECT_EQ(dismissed.count(), 2);
    EXPECT_EQ(triggered.count(), 1);

    bar->setActionText(QString());
    EXPECT_TRUE(bar->actionButton()->isHidden()) << "a message with no action shows none";
}

// The message is named by its text and its detail; its action is a button named by its text.
TEST(AnSnackbar, IsNamedByItsTextAndItsActionIsAButton)
{
    AnSnackbar bar;
    fill(bar);
    EXPECT_EQ(an::tests::widgets::accessibleName(bar), QStringLiteral("Curves Transformation applied, 2.8 s"));
    EXPECT_EQ(an::tests::widgets::accessibleRole(*bar.actionButton()), QAccessible::Button);
    EXPECT_EQ(an::tests::widgets::accessibleName(*bar.actionButton()), QStringLiteral("Undo"));
}
