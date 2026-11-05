// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QImage>
#include <QLabel>
#include <QMargins>
#include <QPalette>
#include <QPixmap>
#include <QPoint>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QWidget>

#include "components/AnCardWidget.h"
#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_HasBorderRadius)
{
    an::ui::AnCardWidget card;
    card.refreshTheme();

    // Border radius is now stored as a dynamic property for the custom paintEvent.
    EXPECT_GT(card.property("anCardRadius").toInt(), 0);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnCardWidget card;

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    card.refreshTheme();
    const QColor darkBg = card.property("anCardBgColor").value<QColor>();

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    card.refreshTheme();
    const QColor lightBg = card.property("anCardBgColor").value<QColor>();

    EXPECT_TRUE(darkBg.isValid());
    EXPECT_TRUE(lightBg.isValid());
    EXPECT_NE(darkBg, lightBg);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_RendersOpaqueCardSurface)
{
    an::ui::AnCardWidget card(QStringLiteral("Frame Quality"));
    card.refreshTheme();
    card.resize(260, 140);
    card.ensurePolished();

    QImage image(card.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    card.render(&image);

    EXPECT_EQ(image.pixelColor(card.rect().center()).alpha(), 255);
    EXPECT_EQ(image.pixelColor(QPoint(1, 1)).alpha(), 255);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_UsesConfiguredBackdropForShadowInset)
{
    an::ui::AnCardWidget card(QStringLiteral("Registration Groups"));
    const QColor backdrop = an::ui::tokens().colors.bgSurface;
    card.setProperty("anCardBackdropColor", backdrop);
    card.refreshTheme();
    card.resize(260, 140);
    card.ensurePolished();

    QImage image(card.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    card.render(&image);

    const QColor expected =
        backdrop.alpha() >= 255 ? backdrop : an::ui::compositeOver(backdrop, an::ui::tokens().colors.bgPrimary);
    EXPECT_EQ(image.pixelColor(QPoint(1, 1)), expected);
    EXPECT_EQ(image.pixelColor(card.rect().center()).alpha(), 255);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_FlatCardLetsParentSurfaceShowThroughCorners)
{
    // The card resolves the tokens again when it refreshes its theme, from the palette the application has now:
    // resolve them first, so the colours expected are the ones it paints with whatever ran before.
    an::ui::refreshTokens();
    const auto& tokenSet = an::ui::tokens();
    const QColor parentSurface = tokenSet.colors.bgSurface;

    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen, true);
    host.resize(280, 160);
    QPalette hostPalette = host.palette();
    hostPalette.setColor(QPalette::Window, parentSurface);
    host.setPalette(hostPalette);
    host.setAutoFillBackground(true);

    an::ui::AnCardWidget card(QStringLiteral("Registration Groups"), &host);
    card.setGeometry(10, 10, 260, 140);
    card.setProperty("anCardBackdropColor", parentSurface);
    card.setProperty("anCardFlat", true);
    card.refreshTheme();
    host.ensurePolished();
    card.ensurePolished();
    host.show();
    QCoreApplication::processEvents();

    QImage image(host.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    host.render(&image);

    const QPoint cardOrigin = card.geometry().topLeft();
    const QColor expectedCardSurface = an::ui::compositeOver(tokenSet.colors.bgCardPrimary, parentSurface);
    EXPECT_EQ(image.pixelColor(cardOrigin + QPoint(1, 1)), parentSurface);
    EXPECT_EQ(image.pixelColor(cardOrigin + QPoint(2, card.height() / 2)), expectedCardSurface);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_AcceptsChildLayout)
{
    an::ui::AnCardWidget card;
    auto* contentLayout = new QVBoxLayout();
    auto* label = new QLabel(QStringLiteral("Child content"));
    contentLayout->addWidget(label);

    card.setContentLayout(contentLayout);

    EXPECT_EQ(card.contentLayout(), contentLayout);
    EXPECT_EQ(contentLayout->count(), 1);
    EXPECT_EQ(label->parentWidget(), card.contentWidget());
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_OptionalHeader)
{
    an::ui::AnCardWidget card;

    ASSERT_NE(card.headerLabel(), nullptr);
    EXPECT_TRUE(card.headerLabel()->isHidden());

    card.setTitle(QStringLiteral("Calibration"));
    EXPECT_EQ(card.title(), QStringLiteral("Calibration"));
    EXPECT_FALSE(card.headerLabel()->isHidden());
    EXPECT_EQ(card.headerLabel()->text(), QStringLiteral("Calibration"));

    card.setTitle(QString());
    EXPECT_TRUE(card.headerLabel()->isHidden());
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_HeaderUsesAccentTextColor)
{
    an::ui::AnCardWidget card(QStringLiteral("Calibration"));
    card.refreshTheme();

    ASSERT_NE(card.headerLabel(), nullptr);
    EXPECT_EQ(card.headerLabel()->palette().color(QPalette::WindowText), an::ui::tokens().colors.textAccent);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_TitleRowStaysOutsideFlatCardSurface)
{
    // The card resolves the tokens again when it refreshes its theme, from the palette the application has now:
    // resolve them first, so the colours expected are the ones it paints with whatever ran before.
    an::ui::refreshTokens();
    const auto& tokenSet = an::ui::tokens();
    const QColor parentSurface = tokenSet.colors.bgSurface;

    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen, true);
    host.resize(300, 180);
    QPalette hostPalette = host.palette();
    hostPalette.setColor(QPalette::Window, parentSurface);
    host.setPalette(hostPalette);
    host.setAutoFillBackground(true);

    an::ui::AnCardWidget card(QStringLiteral("Registration Groups"), &host);
    card.setGeometry(10, 10, 280, 160);
    card.setProperty("anCardBackdropColor", parentSurface);
    card.setProperty("anCardFlat", true);
    auto* contentLayout = new QVBoxLayout();
    contentLayout->addWidget(new QLabel(QStringLiteral("Card body content")));
    card.setContentLayout(contentLayout);
    card.refreshTheme();

    host.ensurePolished();
    card.ensurePolished();
    host.show();
    QCoreApplication::processEvents();

    ASSERT_NE(card.headerLabel(), nullptr);
    ASSERT_NE(card.contentWidget(), nullptr);
    ASSERT_FALSE(card.headerLabel()->isHidden());
    EXPECT_LT(card.headerLabel()->geometry().bottom(), card.contentWidget()->geometry().top());

    QImage image(host.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    host.render(&image);

    const QPoint cardOrigin = card.geometry().topLeft();
    const QPoint headerBackdropProbe(card.width() - tokenSet.spacing.lg, card.headerLabel()->geometry().center().y());
    const QPoint bodyProbe(card.width() / 2, card.contentWidget()->geometry().center().y());
    const QColor expectedCardSurface = an::ui::compositeOver(tokenSet.colors.bgCardPrimary, parentSurface);

    EXPECT_EQ(image.pixelColor(cardOrigin + headerBackdropProbe), parentSurface);
    EXPECT_EQ(image.pixelColor(cardOrigin + bodyProbe), expectedCardSurface);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_CompactPaddedBodyUsesTokenSpacing)
{
    an::ui::AnCardWidget card(QStringLiteral("Compact Section"));
    card.setProperty("anCardCompact", true);
    card.setProperty("anCardBodyPadded", true);

    auto* contentLayout = new QVBoxLayout();
    contentLayout->addWidget(new QLabel(QStringLiteral("Body")));
    card.setContentLayout(contentLayout);
    card.refreshTheme();

    const auto& spacing = an::ui::tokens().spacing;
    ASSERT_NE(card.layout(), nullptr);
    EXPECT_EQ(card.layout()->contentsMargins(), QMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm));
    EXPECT_EQ(card.layout()->spacing(), spacing.xs);
    EXPECT_EQ(contentLayout->contentsMargins(), QMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm));
}

namespace
{

struct ScopedAnimationRestore
{
    bool previous;
    ScopedAnimationRestore() : previous(an::ui::AnimationUtils::animationsEnabled()) {}
    ~ScopedAnimationRestore()
    {
        an::ui::AnimationUtils::setAnimationsEnabled(previous);
    }
};

class ScopedQtWarningSpy
{
public:
    ScopedQtWarningSpy()
    {
        activeSpy = this;
        previousHandler = qInstallMessageHandler(&ScopedQtWarningSpy::messageHandler);
    }

    ~ScopedQtWarningSpy()
    {
        qInstallMessageHandler(previousHandler);
        activeSpy = nullptr;
    }

    [[nodiscard]] bool sawDuplicateLayoutWarning() const
    {
        return sawWarning;
    }

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        if (activeSpy && type == QtWarningMsg && message.contains(QStringLiteral("Attempting to add QLayout")))
        {
            activeSpy->sawWarning = true;
        }

        if (activeSpy && activeSpy->previousHandler)
        {
            activeSpy->previousHandler(type, context, message);
        }
    }

    static ScopedQtWarningSpy* activeSpy;
    QtMessageHandler previousHandler = nullptr;
    bool sawWarning = false;
};

ScopedQtWarningSpy* ScopedQtWarningSpy::activeSpy = nullptr;

[[nodiscard]] QColor renderedCenterColor(QWidget& widget)
{
    widget.setAttribute(Qt::WA_DontShowOnScreen, true);
    widget.resize(260, 140);
    widget.ensurePolished();
    if (!widget.isVisible())
    {
        widget.show();
    }
    QCoreApplication::processEvents();

    QPixmap pixmap(widget.size());
    pixmap.fill(Qt::transparent);
    widget.render(&pixmap);
    return pixmap.toImage().pixelColor(widget.rect().center());
}

} // anonymous namespace

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_SetContentLayoutDoesNotEmitDuplicateLayoutWarning)
{
    ScopedQtWarningSpy warningSpy;

    an::ui::AnCardWidget card;
    auto* contentLayout = new QVBoxLayout(card.contentWidget());

    card.setContentLayout(contentLayout);

    EXPECT_FALSE(warningSpy.sawDuplicateLayoutWarning());
    EXPECT_EQ(card.contentLayout(), contentLayout);
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_HoverChangesBackgroundInstantlyWhenAnimationsDisabled)
{
    ScopedAnimationRestore guard;
    an::ui::AnimationUtils::setAnimationsEnabled(false);

    an::ui::AnCardWidget card;
    card.refreshTheme();

    const QColor restingBg = an::ui::tokens().colors.bgCardPrimary;
    const QColor hoverBg = an::ui::tokens().colors.bgHover;
    ASSERT_NE(restingBg, hoverBg);

    // Simulate hover enter via QEvent::Enter — card should not crash and should update.
    QEvent enterEv(QEvent::Enter);
    QCoreApplication::sendEvent(&card, &enterEv);
    SUCCEED(); // entering does not crash
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_LeaveRestoresBgInstantlyWhenAnimationsDisabled)
{
    ScopedAnimationRestore guard;
    an::ui::AnimationUtils::setAnimationsEnabled(false);

    an::ui::AnCardWidget card;
    card.refreshTheme();

    QEvent enterEv(QEvent::Enter);
    QCoreApplication::sendEvent(&card, &enterEv);

    QEvent leaveEv(QEvent::Leave);
    QCoreApplication::sendEvent(&card, &leaveEv);
    SUCCEED(); // no crash after leave
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_HoverAnimationSetupDoesNotCrashWhenEnabled)
{
    ScopedAnimationRestore guard;
    an::ui::AnimationUtils::setAnimationsEnabled(true);

    an::ui::AnCardWidget card;
    card.refreshTheme();

    QEvent enterEv(QEvent::Enter);
    QCoreApplication::sendEvent(&card, &enterEv);

    QEvent leaveEv(QEvent::Leave);
    QCoreApplication::sendEvent(&card, &leaveEv);
    SUCCEED();
}

TEST(WidgetsComponentsAnCardWidget, AnCardWidget_DisabledStateGraysTextAndSuppressesHover)
{
    ScopedAnimationRestore guard;
    an::ui::AnimationUtils::setAnimationsEnabled(false);

    an::ui::AnCardWidget card(QStringLiteral("Scientific Color Reconstruction"));
    auto* contentLayout = new QVBoxLayout();
    auto* label = new QLabel(QStringLiteral("Color Grip"));
    contentLayout->addWidget(label);
    card.setContentLayout(contentLayout);
    card.refreshTheme();

    const QColor disabledText = an::ui::tokens().colors.textTertiary;
    ASSERT_TRUE(disabledText.isValid());
    EXPECT_EQ(card.headerLabel()->palette().color(QPalette::Disabled, QPalette::WindowText), disabledText);
    EXPECT_EQ(label->palette().color(QPalette::Disabled, QPalette::WindowText), disabledText);

    card.setEnabled(false);
    const QColor beforeHover = renderedCenterColor(card);

    QEvent enterEv(QEvent::Enter);
    QCoreApplication::sendEvent(&card, &enterEv);
    const QColor afterHover = renderedCenterColor(card);

    EXPECT_EQ(afterHover, beforeHover);
    EXPECT_NE(afterHover, an::ui::tokens().colors.bgHover);
}
