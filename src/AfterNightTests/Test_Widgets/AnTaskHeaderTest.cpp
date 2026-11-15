// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A task's header: a strip high in every look, the process's name, "on" and the image's name elided in the middle, the
// state in its colour with the run's bar along its foot, and its buttons - Float as window or Attach, Close, and the
// menu button of a full page.

#include "RenderTestSupport.h"

#include "components/AnIconButton.h"
#include "components/AnTaskHeader.h"
#include "theme/ComponentParts.h"

#include <QSignalSpy>

namespace
{

using an::tests::widgets::forEachLook;
using an::tests::widgets::recordedText;
using an::tests::widgets::showOffScreen;
using an::ui::AnTaskHeader;
using an::ui::DesignTokens;

/// The text drawn that is exactly `text`; a default one when none is.
[[nodiscard]] an::ui::testing::RecordedText drawnExactly(QWidget& widget, const QString& text)
{
    for (const an::ui::testing::RecordedText& drawn : an::tests::widgets::recordedTexts(widget))
    {
        if (drawn.text == text)
        {
            return drawn;
        }
    }
    ADD_FAILURE() << "no text drawn is " << text.toStdString();
    return {};
}

[[nodiscard]] QImage thumbnailOf(const QColor& colour)
{
    QImage picture(60, 60, QImage::Format_ARGB32_Premultiplied);
    picture.fill(colour);
    return picture;
}

} // namespace

// A strip high in every look, on the first surface: the process's name in the primary text and semibold, "on" in the
// tertiary text, the image's name.
TEST(TaskHeader, ItNamesTheProcessAndItsImageInEveryLook)
{
    forEachLook([](const DesignTokens& t) {
        AnTaskHeader header;
        header.setProcessName(QStringLiteral("Curves Transformation"));
        header.setTargetName(QStringLiteral("M20_FINAL_PNG_05"));
        header.setThumbnail(thumbnailOf(Qt::transparent));
        header.resize(1200, header.sizeHint().height());
        EXPECT_EQ(header.sizeHint().height(), t.metrics.strip);
        showOffScreen(header);
        const an::ui::testing::RecordedText name = recordedText(header, QStringLiteral("Curves"));
        EXPECT_EQ(name.colour, t.colors.textPrimary);
        EXPECT_EQ(name.font.weight(), QFont::DemiBold);
        EXPECT_EQ(drawnExactly(header, QStringLiteral("on")).colour, t.colors.textTertiary);
        EXPECT_EQ(recordedText(header, QStringLiteral("M20_FINAL")).colour, t.colors.textPrimary);
        EXPECT_EQ(header.shownTargetName(), QStringLiteral("M20_FINAL_PNG_05"));
        EXPECT_TRUE(an::tests::widgets::accessibleName(header).contains(QStringLiteral("M20_FINAL_PNG_05")));
    });
}

// A task with no single target is named by its process alone.
TEST(TaskHeader, ATaskWithNoTargetIsNamedByItsProcessAlone)
{
    AnTaskHeader header;
    header.setProcessName(QStringLiteral("Channel Composition"));
    header.resize(900, header.sizeHint().height());
    showOffScreen(header);
    EXPECT_TRUE(header.shownTargetName().isEmpty());
    EXPECT_EQ(an::tests::widgets::accessibleName(header), QStringLiteral("Channel Composition"));
}

// An image's name too long for the header is elided in the middle: its start and its end stay.
TEST(TaskHeader, ALongNameIsElidedInTheMiddle)
{
    AnTaskHeader header;
    header.setProcessName(QStringLiteral("Curves Transformation"));
    const QString longName = QStringLiteral("M20_trifid_nebula_integration_drizzle_2x_final_stretched_PNG_05");
    header.setTargetName(longName);
    header.resize(1000, header.sizeHint().height());
    showOffScreen(header);
    const QString shown = header.shownTargetName();
    EXPECT_NE(shown, longName);
    EXPECT_TRUE(shown.startsWith(QStringLiteral("M20"))) << shown.toStdString();
    EXPECT_TRUE(shown.endsWith(QStringLiteral("05"))) << shown.toStdString();
}

// The state in its colour: up to date in the secondary text, a run in the accent's text and semibold with the bar of
// its progress along the foot, a failure in the error colour.
TEST(TaskHeader, TheStateIsToldInItsColour)
{
    forEachLook([](const DesignTokens& t) {
        AnTaskHeader header;
        header.setProcessName(QStringLiteral("Curves Transformation"));
        header.setTargetName(QStringLiteral("M20"));
        header.resize(1000, header.sizeHint().height());
        showOffScreen(header);

        header.setState(AnTaskHeader::State::UpToDate, QStringLiteral("preview up to date"));
        EXPECT_EQ(recordedText(header, QStringLiteral("preview up to date")).colour, t.colors.textSecondary);
        EXPECT_TRUE(header.runBarRect().isEmpty()) << "no bar without a run";

        header.setState(AnTaskHeader::State::Running, QStringLiteral("applying - 42 %"), 0.42);
        const an::ui::testing::RecordedText running = recordedText(header, QStringLiteral("applying"));
        EXPECT_EQ(running.colour, t.colors.textAccent);
        EXPECT_EQ(running.font.weight(), QFont::DemiBold);
        const QRect bar = header.runBarRect();
        EXPECT_EQ(bar.height(), an::ui::parts::kTaskHeaderRunBar);
        EXPECT_EQ(bar.bottom(), header.height() - 1);
        EXPECT_EQ(bar.width(), static_cast<int>(header.width() * 0.42));
        const an::ui::testing::RenderedPicture picture =
            an::ui::testing::RenderedPicture::ofWidget(header, 1.0, t.layers.surface1);
        an::tests::widgets::expectColour(picture.at(bar.width() / 2.0, header.height() - 1.0), t.colors.accentPrimary,
                                         "the run's bar");

        header.setState(AnTaskHeader::State::Running, QStringLiteral("applying"));
        EXPECT_FALSE(header.runBarRect().isEmpty()) << "a run that cannot tell slides its bar";

        header.setState(AnTaskHeader::State::Failed, QStringLiteral("failed"));
        EXPECT_EQ(recordedText(header, QStringLiteral("failed")).colour, t.colors.statusError);
        EXPECT_TRUE(header.runBarRect().isEmpty());
    });
}

// Float as window floats the task, Attach in a floating one; Close closes it; the menu button is a full page's.
TEST(TaskHeader, ItsButtons)
{
    AnTaskHeader header;
    header.setProcessName(QStringLiteral("Curves Transformation"));
    header.resize(1000, header.sizeHint().height());
    showOffScreen(header);
    EXPECT_TRUE(header.menuButton()->isHidden());
    EXPECT_EQ(header.floatButton()->text(), QStringLiteral("Float as window"));
    EXPECT_EQ(header.closeButton()->text(), QStringLiteral("Close (Esc)"));
    EXPECT_GT(header.closeButton()->x(), header.floatButton()->x()) << "Close is the last";

    QSignalSpy floated(&header, &AnTaskHeader::floatRequested);
    QSignalSpy closed(&header, &AnTaskHeader::closeRequested);
    QSignalSpy menus(&header, &AnTaskHeader::menuRequested);
    header.floatButton()->click();
    header.closeButton()->click();
    EXPECT_EQ(floated.count(), 1);
    EXPECT_EQ(closed.count(), 1);

    header.setFloating(true);
    EXPECT_EQ(header.floatButton()->text(), QStringLiteral("Attach"));
    header.setMenuButtonVisible(true);
    EXPECT_FALSE(header.menuButton()->isHidden());
    EXPECT_LT(header.menuButton()->x(), header.floatButton()->x());
    header.menuButton()->click();
    EXPECT_EQ(menus.count(), 1);
}
