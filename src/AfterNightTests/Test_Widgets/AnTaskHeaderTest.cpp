// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A task's header: a strip high in every look, the process's name, "on" and the image's name elided in the middle, the
// state in its colour with the run's bar along its foot, and its buttons - Float as window or Attach, Close, and the
// menu button of a full page.
//
// And the header of a task that holds a job: three lines - the eyebrow, the job's name, its facts - with the job's
// measure and its bar, its acts as icon buttons that follow their actions and its primary act as the primary button,
// before the ways out; the measure left out where the name would keep too little room.

#include "RenderTestSupport.h"

#include "components/AnIconButton.h"
#include "components/AnTaskHeader.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAction>
#include <QFontMetrics>
#include <QImage>
#include <QSignalSpy>

namespace
{

using an::tests::widgets::forEachLook;
using an::tests::widgets::pumpFor;
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

/// A colour laid over an opaque one, as a painter lays it.
[[nodiscard]] QColor laidOver(const QColor& top, const QColor& under)
{
    const double alpha = top.alphaF();
    return QColor::fromRgbF(top.redF() * alpha + under.redF() * (1.0 - alpha),
                            top.greenF() * alpha + under.greenF() * (1.0 - alpha),
                            top.blueF() * alpha + under.blueF() * (1.0 - alpha));
}

/// A job as an integration's header says it: a measure a quarter used, Undo and Redo as its acts, Execute as its
/// primary act.
[[nodiscard]] an::ui::JobHeading jobOf(QAction* undo, QAction* redo, QAction* execute)
{
    an::ui::JobHeading job;
    job.eyebrow = QStringLiteral("Image Integration - Job");
    job.title = QStringLiteral("Carina Nebula LRGB");
    job.facts = QStringLiteral("718 frames - 11 groups - 20h 2m");
    job.meter = an::ui::JobHeading::Meter{.label = QStringLiteral("Disk space"),
                                          .text = QStringLiteral("253.75 GB free - need ~62.56 GB"),
                                          .share = 0.25,
                                          .exceeded = false,
                                          .toolTip = QStringLiteral("Available: 253.75 GB")};
    job.acts = {an::ui::JobHeading::Act{undo, QStringLiteral("undo")},
                an::ui::JobHeading::Act{redo, QStringLiteral("redo")}};
    job.primary = execute;
    return job;
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
    header.resize(1000, header.sizeHint().height());
    // Long in the host's font: wider than the whole header, whatever a character measures there.
    QString longName = QStringLiteral("M20_trifid_nebula");
    const QFontMetrics metrics(header.font());
    while (metrics.horizontalAdvance(longName) < 2 * header.width())
    {
        longName += QStringLiteral("_integration_drizzle_2x_final");
    }
    longName += QStringLiteral("_PNG_05");
    header.setTargetName(longName);
    showOffScreen(header);
    const QString shown = header.shownTargetName();
    EXPECT_NE(shown, longName);
    EXPECT_TRUE(shown.startsWith(QStringLiteral("M20"))) << shown.toStdString();
    EXPECT_TRUE(shown.endsWith(QStringLiteral("05"))) << shown.toStdString();
}

// Under Night the header paints no green or blue, the target's picture too: it is shown in its Night form once that is
// made off the GUI thread, and its place keeps the well's colour until then.
TEST(TaskHeader, NightShowsTheThumbnailInItsNightForm)
{
    an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[2]);
    scope.setNight(true);
    AnTaskHeader header;
    header.setProcessName(QStringLiteral("Curves Transformation"));
    header.setTargetName(QStringLiteral("M20"));
    header.setState(AnTaskHeader::State::UpToDate, QStringLiteral("preview up to date"));
    header.setThumbnail(thumbnailOf(QColor(0, 255, 0)));
    header.resize(900, header.sizeHint().height());
    showOffScreen(header);

    (void)an::ui::testing::RenderedPicture::ofWidget(header, 1.0, an::ui::tokens().layers.surface0);
    pumpFor(300);
    const an::ui::testing::RenderedPicture picture =
        an::ui::testing::RenderedPicture::ofWidget(header, 1.0, an::ui::tokens().layers.surface0);
    const QImage& image = picture.image();
    int hued = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            hued += pixel.green() != 0 || pixel.blue() != 0 ? 1 : 0;
        }
    }
    EXPECT_EQ(hued, 0);

    // The picture is there, in red: pure green's luma, 0.587 of 255, in the middle of the thumbnail.
    const QPoint middle(an::ui::parts::kTaskHeaderPaddingStart + an::ui::parts::kTaskHeaderThumbnail / 2,
                        header.height() / 2);
    const QColor centre = image.pixelColor(middle);
    EXPECT_EQ(centre.green(), 0);
    EXPECT_EQ(centre.blue(), 0);
    EXPECT_NEAR(centre.red(), 150, 2) << "the thumbnail's Night form, not the well's colour";
    scope.setNight(false);
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

// A header that says a job is three lines high in every look: the eyebrow in the accent's text, the job's name at its
// own size in semibold, its facts in the secondary text; the measure's bar filled by its share in the success colour,
// or whole in the error colour once the need is over what there is; and, from the left, the job's acts, the menu
// button, the primary act, Float as window and Close.
TEST(TaskHeader, AJobIsSaidOnThreeLinesWithItsMeasure)
{
    forEachLook([](const DesignTokens& t) {
        QAction undo(QStringLiteral("&Undo"));
        QAction redo(QStringLiteral("&Redo"));
        QAction execute(QStringLiteral("Execute Integration"));
        AnTaskHeader header;
        header.setJob(jobOf(&undo, &redo, &execute));
        header.setMenuButtonVisible(true);
        // Wide enough for the measure beside the name, whatever a character measures on this host.
        header.resize(3000, header.sizeHint().height());
        EXPECT_GT(header.sizeHint().height(), t.metrics.strip) << "three lines, not a strip";
        showOffScreen(header);

        EXPECT_EQ(recordedText(header, QStringLiteral("Image Integration")).colour, t.colors.textAccent);
        const an::ui::testing::RecordedText title = recordedText(header, QStringLiteral("Carina Nebula"));
        EXPECT_EQ(title.colour, t.colors.textPrimary);
        EXPECT_EQ(title.font.weight(), QFont::DemiBold);
        EXPECT_EQ(an::tests::widgets::logicalPixels(title.font), an::ui::parts::kJobHeaderTitleFont);
        const an::ui::testing::RecordedText facts = recordedText(header, QStringLiteral("718 frames"));
        EXPECT_EQ(facts.colour, t.colors.textSecondary);
        EXPECT_EQ(facts.font.family(), t.typography.monospace.family());
        EXPECT_EQ(recordedText(header, QStringLiteral("Disk space")).colour, t.colors.textSecondary);
        EXPECT_FALSE(recordedText(header, QStringLiteral("253.75 GB free")).text.isEmpty());
        EXPECT_TRUE(an::tests::widgets::accessibleName(header).contains(QStringLiteral("Carina Nebula LRGB")));

        const QRect bar = header.jobMeterRect();
        ASSERT_EQ(bar.size(), QSize(an::ui::parts::kJobHeaderMeterWidth, an::ui::parts::kJobHeaderMeterBar));
        an::ui::testing::RenderedPicture picture =
            an::ui::testing::RenderedPicture::ofWidget(header, 1.0, t.layers.surface1);
        const qreal middle = bar.top() + bar.height() / 2.0;
        an::tests::widgets::expectColour(picture.at(bar.left() + bar.width() * 0.125, middle), t.colors.statusSuccess,
                                         "the share of the measure that is used");
        an::tests::widgets::expectColour(picture.at(bar.left() + bar.width() * 0.75, middle),
                                         laidOver(t.colors.statusSuccessSoft, t.layers.surface1),
                                         "the rest of the measure");

        an::ui::JobHeading over = jobOf(&undo, &redo, &execute);
        over.meter->share = 1.0;
        over.meter->exceeded = true;
        header.setJob(over);
        picture = an::ui::testing::RenderedPicture::ofWidget(header, 1.0, t.layers.surface1);
        an::tests::widgets::expectColour(picture.at(bar.left() + bar.width() * 0.75, middle), t.colors.statusError,
                                         "a need over what there is");

        const QList<QAbstractButton*> acts = header.jobActButtons();
        ASSERT_EQ(acts.size(), 2);
        ASSERT_NE(header.jobPrimaryButton(), nullptr);
        EXPECT_GT(acts[0]->x(), bar.right()) << "the measure is before the buttons";
        EXPECT_LT(acts[0]->x(), acts[1]->x());
        EXPECT_LT(acts[1]->x(), header.menuButton()->x());
        EXPECT_LT(header.menuButton()->x(), header.jobPrimaryButton()->x());
        EXPECT_LT(header.jobPrimaryButton()->x(), header.floatButton()->x());
        EXPECT_LT(header.floatButton()->x(), header.closeButton()->x());
        EXPECT_LE(header.closeButton()->geometry().right(), header.width() - an::ui::parts::kJobHeaderPaddingEnd);
    });
}

// A job's acts are their actions: a button is enabled while its action is, says what the action says and triggers it;
// the primary button reads the primary act's text and triggers it.
TEST(TaskHeader, AJobsButtonsFollowTheirActions)
{
    QAction undo(QStringLiteral("&Undo"));
    undo.setToolTip(QStringLiteral("Undo the last change."));
    undo.setEnabled(false);
    QAction redo(QStringLiteral("&Redo"));
    QAction execute(QStringLiteral("Execute Integration"));
    execute.setToolTip(QStringLiteral("Run the integration workflow using the current settings."));
    execute.setEnabled(false);
    AnTaskHeader header;
    header.setJob(jobOf(&undo, &redo, &execute));
    header.resize(1492, header.sizeHint().height());
    showOffScreen(header);
    const QList<QAbstractButton*> acts = header.jobActButtons();
    ASSERT_EQ(acts.size(), 2);
    QAbstractButton* const primary = header.jobPrimaryButton();
    ASSERT_NE(primary, nullptr);

    EXPECT_FALSE(acts[0]->isEnabled()) << "nothing to undo";
    EXPECT_TRUE(acts[1]->isEnabled());
    EXPECT_EQ(acts[0]->toolTip(), QStringLiteral("Undo the last change."));
    EXPECT_EQ(acts[1]->toolTip(), QStringLiteral("Redo")) << "an action that says nothing more is named by its text";
    EXPECT_EQ(primary->text(), QStringLiteral("Execute Integration"));
    EXPECT_EQ(primary->toolTip(), QStringLiteral("Run the integration workflow using the current settings."));
    EXPECT_FALSE(primary->isEnabled());

    QSignalSpy undone(&undo, &QAction::triggered);
    QSignalSpy redone(&redo, &QAction::triggered);
    QSignalSpy executed(&execute, &QAction::triggered);
    acts[0]->click();
    acts[1]->click();
    primary->click();
    EXPECT_EQ(undone.count(), 0) << "a disabled act does nothing";
    EXPECT_EQ(redone.count(), 1);
    EXPECT_EQ(executed.count(), 0);

    undo.setEnabled(true);
    execute.setEnabled(true);
    execute.setText(QStringLiteral("Execute Again"));
    EXPECT_TRUE(acts[0]->isEnabled());
    EXPECT_TRUE(primary->isEnabled());
    EXPECT_EQ(primary->text(), QStringLiteral("Execute Again"));
    acts[0]->click();
    primary->click();
    EXPECT_EQ(undone.count(), 1);
    EXPECT_EQ(executed.count(), 1);
}

// A header too narrow for the name beside the measure leaves the measure out, and elides the name where it still does
// not fit; a job's run draws its bar along the header's foot as a process's does.
TEST(TaskHeader, ANarrowJobHeaderLeavesTheMeasureOutAndARunDrawsItsBar)
{
    QAction undo(QStringLiteral("&Undo"));
    QAction redo(QStringLiteral("&Redo"));
    QAction execute(QStringLiteral("Execute Integration"));
    an::ui::JobHeading job = jobOf(&undo, &redo, &execute);
    AnTaskHeader header;
    header.setJob(job);
    // Wide enough for the whole of it, whatever a character measures on this host.
    header.resize(6000, header.sizeHint().height());
    showOffScreen(header);
    EXPECT_FALSE(header.jobMeterRect().isEmpty());
    EXPECT_EQ(header.shownJobTitle(), job.title);

    // Narrow by what the header itself needs, whatever a character measures on this host.
    const int buttonsLeft = header.jobActButtons().front()->x();
    const int withoutRoom = header.width() - buttonsLeft + an::ui::parts::kJobHeaderPaddingStart +
                            an::ui::parts::kJobHeaderTextLeast + an::ui::parts::kJobHeaderGap;
    header.resize(withoutRoom, header.height());
    EXPECT_TRUE(header.jobMeterRect().isEmpty()) << "the name keeps its room before the measure does";

    const QFontMetrics metrics(header.font());
    while (metrics.horizontalAdvance(job.title) < 2 * header.width())
    {
        job.title += QStringLiteral(" and the Gabriela Mistral nebula");
    }
    header.setJob(job);
    EXPECT_NE(header.shownJobTitle(), job.title);
    EXPECT_TRUE(header.shownJobTitle().startsWith(QStringLiteral("Carina"))) << header.shownJobTitle().toStdString();

    header.setState(AnTaskHeader::State::Running, QStringLiteral("running"), 0.5);
    const QRect bar = header.runBarRect();
    EXPECT_EQ(bar.bottom(), header.height() - 1);
    EXPECT_EQ(bar.width(), header.width() / 2);
}
