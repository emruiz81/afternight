// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The floating toolbar: a view's actions and choices in the order of a workspace's toolbar, each button the view's own
// action - its state, check and tool tip, and a click triggers it - its choices mirroring the view's, the view's name
// shown when there is more than one view, the small form of a floating image window, where a host places it, and its
// look: a rounded scrim with an edge under it and nothing outside it.

#include "RenderTestSupport.h"

#include "components/AnActionButton.h"
#include "components/AnFloatingToolbar.h"
#include "controls/ViewChrome.h"
#include "theme/ComponentParts.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QSignalSpy>
#include <QWidget>

#include <memory>
#include <vector>

namespace
{

using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::RenderedPicture;
using an::tests::widgets::showOffScreen;
using an::ui::AnFloatingToolbar;
using an::ui::DesignTokens;
using an::ui::ViewChromeActions;

/// A view's chrome as the view would hand it over, made of plain actions and combos a case can steer.
class StandInChrome
{
public:
    StandInChrome()
    {
        const auto action = [this](const QString& text, const QString& tip = QString(), const bool checkable = false) {
            auto* made = new QAction(text, &m_owner);
            made->setToolTip(tip.isEmpty() ? text : tip);
            made->setCheckable(checkable);
            return made;
        };
        actions.fit = action(QStringLiteral("Fit To Window"), QStringLiteral("Fit To Window (Ctrl+Shift+F)"));
        actions.zoomIn = action(QStringLiteral("Zoom In"));
        actions.zoomOut = action(QStringLiteral("Zoom Out"));
        actions.zoom100 = action(QStringLiteral("Zoom 100%"));
        actions.maximize = action(QStringLiteral("Maximize image view"), QStringLiteral("Maximize image view (F11)"));
        actions.autostretchSettings = action(QStringLiteral("Autostretch Settings"));
        actions.undo = action(QStringLiteral("Undo"));
        actions.redo = action(QStringLiteral("Redo"));
        actions.history = action(QStringLiteral("Image History"));
        actions.statistics = action(QStringLiteral("Statistics"));
        actions.showAstrometry = action(QStringLiteral("Show Astrometry"), QString(), true);
        actions.statisticsOverlay = action(QStringLiteral("Show statistics"), QString(), true);
        actions.takeSnapshot = action(QStringLiteral("Take snapshot"));

        m_channel = std::make_unique<QComboBox>();
        m_channel->setAccessibleName(QStringLiteral("View Channel"));
        m_channel->addItems(
            {QStringLiteral("RGB"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")});
        m_mode = std::make_unique<QComboBox>();
        m_mode->setAccessibleName(QStringLiteral("Display Mode"));
        m_mode->addItems({QStringLiteral("Linear"), QStringLiteral("Autostretch")});
        actions.channel = m_channel.get();
        actions.displayMode = m_mode.get();
    }

    /// A workspace view's chrome: no snapshot and no statistics overlay, as a standard view has none.
    [[nodiscard]] ViewChromeActions workspaceView() const
    {
        ViewChromeActions view = actions;
        view.takeSnapshot = nullptr;
        view.statisticsOverlay = nullptr;
        return view;
    }

    ViewChromeActions actions;

private:
    QObject m_owner;
    std::unique_ptr<QComboBox> m_channel;
    std::unique_ptr<QComboBox> m_mode;
};

} // namespace

// From the left: Fit To Window, Zoom Out, the zoom, Zoom In, Zoom 100 %, Maximize; the channel, the display mode,
// Autostretch Settings; the snapshot, the statistics overlay and Show Astrometry where the view has them; More - a
// separator between two groups shown, none at an end.
TEST(FloatingToolbar, ShowsTheViewsActionsInTheOrderOfAWorkspace)
{
    StandInChrome chrome;
    AnFloatingToolbar toolbar;
    toolbar.setActions(chrome.workspaceView());
    toolbar.setZoom(25.364);
    EXPECT_EQ(toolbar.shownParts(),
              (QStringList{QStringLiteral("Fit To Window"), QStringLiteral("Zoom Out"), QStringLiteral("zoom"),
                           QStringLiteral("Zoom In"), QStringLiteral("Zoom 100%"),
                           QStringLiteral("Maximize image view"), QStringLiteral("|"), QStringLiteral("channel"),
                           QStringLiteral("display mode"), QStringLiteral("Autostretch Settings"), QStringLiteral("|"),
                           QStringLiteral("Show Astrometry"), QStringLiteral("|"), QStringLiteral("more")}));
    EXPECT_EQ(toolbar.zoomText(), QStringLiteral("25.36 %")) << "the zoom as the status bar writes it";

    // A preview's set: the zoom group, the channel, the display mode, its settings, the snapshot, the overlay, More.
    ViewChromeActions preview = chrome.actions;
    preview.maximize = nullptr;
    preview.showAstrometry = nullptr;
    toolbar.setActions(preview);
    EXPECT_EQ(toolbar.shownParts(),
              (QStringList{QStringLiteral("Fit To Window"), QStringLiteral("Zoom Out"), QStringLiteral("zoom"),
                           QStringLiteral("Zoom In"), QStringLiteral("Zoom 100%"), QStringLiteral("|"),
                           QStringLiteral("channel"), QStringLiteral("display mode"),
                           QStringLiteral("Autostretch Settings"), QStringLiteral("|"), QStringLiteral("Take snapshot"),
                           QStringLiteral("Show statistics"), QStringLiteral("|"), QStringLiteral("more")}));

    toolbar.setActions({});
    EXPECT_TRUE(toolbar.shownParts().isEmpty()) << "with no view, nothing";
}

// Each button is the view's action: its tool tip and text, enabled, shown and checked as the action is, following each
// change, and a click triggers it. A choice mirrors the view's own and chooses on it.
TEST(FloatingToolbar, StatesMirrorTheViewsActions)
{
    StandInChrome chrome;
    AnFloatingToolbar toolbar;
    toolbar.setActions(chrome.workspaceView());
    showOffScreen(toolbar);

    QAbstractButton* fit = toolbar.buttonFor(chrome.actions.fit);
    ASSERT_NE(fit, nullptr);
    EXPECT_EQ(fit->toolTip(), QStringLiteral("Fit To Window (Ctrl+Shift+F)"));
    EXPECT_EQ(an::tests::widgets::accessibleName(*fit), QStringLiteral("Fit To Window"));
    QSignalSpy triggered(chrome.actions.fit, &QAction::triggered);
    fit->click();
    EXPECT_EQ(triggered.count(), 1);

    chrome.actions.fit->setEnabled(false);
    EXPECT_FALSE(fit->isEnabled());
    chrome.actions.fit->setEnabled(true);
    EXPECT_TRUE(fit->isEnabled());
    chrome.actions.fit->setToolTip(QStringLiteral("Fit"));
    EXPECT_EQ(fit->toolTip(), QStringLiteral("Fit"));

    QAbstractButton* astrometry = toolbar.buttonFor(chrome.actions.showAstrometry);
    ASSERT_NE(astrometry, nullptr);
    EXPECT_TRUE(astrometry->isCheckable());
    chrome.actions.showAstrometry->setChecked(true);
    EXPECT_TRUE(astrometry->isChecked());
    astrometry->click();
    EXPECT_FALSE(chrome.actions.showAstrometry->isChecked()) << "a click toggles the view's action";
    EXPECT_FALSE(astrometry->isChecked());

    const int width = toolbar.sizeHint().width();
    chrome.actions.showAstrometry->setVisible(false);
    EXPECT_FALSE(astrometry->isVisibleTo(&toolbar)) << "hidden as the view hides it";
    EXPECT_LT(toolbar.sizeHint().width(), width);
    EXPECT_FALSE(toolbar.shownParts().contains(QStringLiteral("Show Astrometry")));
    EXPECT_EQ(toolbar.shownParts().count(QStringLiteral("|")), 2) << "no separator is left with nothing after it";

    QAbstractButton* channel = toolbar.channelField();
    ASSERT_NE(channel, nullptr);
    EXPECT_EQ(channel->text(), QStringLiteral("RGB"));
    chrome.actions.channel->setCurrentIndex(2);
    EXPECT_EQ(channel->text(), QStringLiteral("Green")) << "the field follows the view's channel";
    chrome.actions.channel->setEnabled(false);
    EXPECT_FALSE(channel->isEnabled());

    QAbstractButton* linear = toolbar.displayModeSegment(0);
    QAbstractButton* stretched = toolbar.displayModeSegment(1);
    ASSERT_NE(linear, nullptr);
    ASSERT_NE(stretched, nullptr);
    EXPECT_EQ(linear->text(), QStringLiteral("Linear"));
    EXPECT_TRUE(linear->isChecked());
    stretched->click();
    EXPECT_EQ(chrome.actions.displayMode->currentIndex(), 1) << "a segment chooses on the view";
    EXPECT_TRUE(stretched->isChecked());
    chrome.actions.displayMode->setCurrentIndex(0);
    EXPECT_TRUE(linear->isChecked()) << "and follows what the view chose";
    chrome.actions.displayMode->setEnabled(false);
    EXPECT_FALSE(toolbar.displayModeSegments()->isEnabled());

    QSignalSpy more(&toolbar, &AnFloatingToolbar::moreRequested);
    toolbar.moreButton()->click();
    EXPECT_EQ(more.count(), 1) << "More asks the host for the view's menu";
}

// With more than one view the toolbar says which it acts on: the view's name, a separator, then its tools.
TEST(FloatingToolbar, TheNameAppearsWithMoreThanOneView)
{
    StandInChrome chrome;
    AnFloatingToolbar toolbar;
    toolbar.setActions(chrome.workspaceView());
    toolbar.setViewName(QStringLiteral("M20_FINAL_PNG_05"));
    toolbar.setViewCount(1);
    EXPECT_FALSE(toolbar.shownParts().contains(QStringLiteral("name")));

    toolbar.setViewCount(2);
    const QStringList parts = toolbar.shownParts();
    ASSERT_GE(parts.size(), 3);
    EXPECT_EQ(parts[0], QStringLiteral("name"));
    EXPECT_EQ(parts[1], QStringLiteral("|"));
    EXPECT_EQ(parts[2], QStringLiteral("Fit To Window"));

    // A long name is elided at most 220 px wide, its whole text the tool tip.
    toolbar.setViewName(QString(200, QLatin1Char('M')));
    auto* name = toolbar.findChild<QWidget*>(QStringLiteral("anFloatingToolbarName"));
    ASSERT_NE(name, nullptr);
    EXPECT_LE(name->sizeHint().width(), an::ui::parts::kFloatingToolbarNameMaximum);
    EXPECT_EQ(name->toolTip(), QString(200, QLatin1Char('M')));
}

// The small form of a floating image window: 32 px high, Fit, Zoom Out, the zoom, Zoom In, a separator, the display
// mode as one toggle, More.
TEST(FloatingToolbar, TheSmallForm)
{
    StandInChrome chrome;
    AnFloatingToolbar toolbar;
    toolbar.setActions(chrome.workspaceView());
    toolbar.setForm(AnFloatingToolbar::Form::Small);
    EXPECT_EQ(toolbar.barHeight(), an::ui::parts::kFloatingToolbarSmallHeight);
    EXPECT_EQ(toolbar.sizeHint().height(), an::ui::parts::kFloatingToolbarSmallHeight + 1) << "and the edge under it";
    EXPECT_EQ(toolbar.barRadius(), an::ui::parts::kFloatingToolbarSmallRadius);
    EXPECT_EQ(toolbar.shownParts(), (QStringList{QStringLiteral("Fit To Window"), QStringLiteral("Zoom Out"),
                                                 QStringLiteral("zoom"), QStringLiteral("Zoom In"), QStringLiteral("|"),
                                                 QStringLiteral("display mode"), QStringLiteral("more")}))
        << "the display mode and More are one group";

    QAbstractButton* toggle = toolbar.displayModeToggle();
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isChecked());
    toggle->click();
    EXPECT_EQ(chrome.actions.displayMode->currentIndex(), 1) << "on, the view shows its image stretched";
    EXPECT_TRUE(toggle->isChecked());
    EXPECT_EQ(toggle->toolTip(), QStringLiteral("Display mode: Autostretch"));
    toggle->click();
    EXPECT_EQ(chrome.actions.displayMode->currentIndex(), 0);

    showOffScreen(toolbar);
    toolbar.resize(toolbar.sizeHint());
    QAbstractButton* fit = toolbar.buttonFor(chrome.actions.fit);
    ASSERT_NE(fit, nullptr);
    EXPECT_EQ(fit->width(), an::ui::parts::kFloatingToolbarSmallButton);

    toolbar.setForm(AnFloatingToolbar::Form::Full);
    EXPECT_TRUE(toolbar.shownParts().contains(QStringLiteral("Maximize image view")));
}

// A host places the toolbar over the bottom of the area it floats over: centred, its bar the form's lift above the
// area's bottom, and placed again when what it shows changes its width.
TEST(FloatingToolbar, AHostPlacesItOverTheBottomOfAnArea)
{
    forEachLook([](const DesignTokens& t) {
        StandInChrome chrome;
        QWidget area;
        area.resize(1200, 700);
        auto* toolbar = new AnFloatingToolbar(&area);
        toolbar->setActions(chrome.workspaceView());
        toolbar->placeOver(area.rect());
        const QRect placed = toolbar->geometry();
        EXPECT_EQ(toolbar->barHeight(), t.metrics.floatingToolbar);
        EXPECT_EQ(placed.size(), toolbar->sizeHint());
        EXPECT_LE(std::abs(placed.center().x() - area.rect().center().x()), 1);
        EXPECT_EQ(placed.top() + toolbar->barHeight(), area.height() - an::ui::parts::kFloatingToolbarLift);

        chrome.actions.showAstrometry->setVisible(false);
        EXPECT_LT(toolbar->width(), placed.width()) << "narrower, it is placed again";
        EXPECT_LE(std::abs(toolbar->geometry().center().x() - area.rect().center().x()), 1);

        toolbar->setForm(AnFloatingToolbar::Form::Small);
        EXPECT_EQ(toolbar->geometry().top() + toolbar->barHeight(),
                  area.height() - an::ui::parts::kFloatingToolbarSmallLift);
    });
}

// A sequence keeps its player strip at the bottom of its own view, where the toolbar would cover it: told what to keep
// clear of, the toolbar is lifted until its bar is 8 px above the highest of the strips it would cover - one under
// either end of it too, and one it would cover once lifted above another - in either form, and stays so when what it
// shows changes its width; a strip beside it or above it does not move it, and it is never lifted above the area.
TEST(FloatingToolbar, ItIsLiftedAboveWhatItWouldCover)
{
    forEachLook([](const DesignTokens&) {
        StandInChrome chrome;
        QWidget area;
        area.resize(1200, 700);
        auto* toolbar = new AnFloatingToolbar(&area);
        toolbar->setActions(chrome.workspaceView());
        const QRect alone = toolbar->placementIn(area.rect());
        const auto barBottom = [toolbar](const QRect& placed) { return placed.top() + toolbar->barHeight(); };
        const int clearance = an::ui::parts::kFloatingToolbarClearance;

        // A strip across the area's bottom.
        const QRect strip(0, 656, 1200, 44);
        toolbar->placeOver(area.rect(), {strip});
        EXPECT_FALSE(toolbar->geometry().intersects(strip)) << "the toolbar covers the strip";
        EXPECT_EQ(barBottom(toolbar->geometry()), strip.top() - clearance);
        EXPECT_EQ(toolbar->geometry().x(), alone.x()) << "lifted, it stays centred";

        // One under its right end only, as a view beside the centre.
        const QRect underItsEnd(alone.right() - 40, 656, 400, 44);
        EXPECT_EQ(barBottom(toolbar->placementIn(area.rect(), {underItsEnd})), underItsEnd.top() - clearance);

        // Beside it, or high above it, it covers nothing and is not moved.
        EXPECT_EQ(toolbar->placementIn(area.rect(), {QRect(0, 656, alone.left() - 20, 44)}), alone);
        EXPECT_EQ(toolbar->placementIn(area.rect(), {QRect(0, 0, 1200, 44)}), alone);

        // Lifted above one strip it would cover another: it is lifted above that one too.
        const QRect higher(0, strip.top() - clearance - toolbar->barHeight() + 10, 1200, 30);
        const QRect twice = toolbar->placementIn(area.rect(), {strip, higher});
        EXPECT_FALSE(twice.intersects(strip));
        EXPECT_FALSE(twice.intersects(higher));
        EXPECT_EQ(barBottom(twice), higher.top() - clearance);

        // Never above the area: with no room above the strip it sits at the area's top.
        const QRect tall(0, 20, 1200, 680);
        EXPECT_EQ(toolbar->placementIn(area.rect(), {tall}).top(), 0);

        // Placed again when its width changes, still lifted.
        chrome.actions.showAstrometry->setVisible(false);
        EXPECT_FALSE(toolbar->geometry().intersects(strip)) << "placed again, it covers the strip";
        EXPECT_EQ(barBottom(toolbar->geometry()), strip.top() - clearance);

        // The small form keeps the same room.
        toolbar->setForm(AnFloatingToolbar::Form::Small);
        EXPECT_FALSE(toolbar->geometry().intersects(strip));
        EXPECT_EQ(barBottom(toolbar->geometry()), strip.top() - clearance);
    });
}

// The bar is the scrim in a rounded shape with the 1 px edge under it; nothing is drawn outside the shape, so the image
// shows at its corners. Its icons and text are the text over images.
TEST(FloatingToolbar, RendersARoundedScrimAndNothingOutsideIt)
{
    forEachLook([](const DesignTokens& t) {
        StandInChrome chrome;
        AnFloatingToolbar toolbar;
        toolbar.setActions(chrome.workspaceView());
        toolbar.setZoom(100.0);
        showOffScreen(toolbar);
        toolbar.resize(toolbar.sizeHint());
        const QColor image(30, 120, 60);
        const int bar = toolbar.barHeight();
        forEachRatio(toolbar, image, [&](const RenderedPicture& picture) {
            an::tests::widgets::expectColour(picture.at(0.2, 0.2), image, "outside the rounded corner");
            an::tests::widgets::expectColour(picture.at(toolbar.width() - 0.3, bar - 0.3), image,
                                             "outside the bottom right corner");
            const QColor scrim = an::ui::compositeOver(t.colors.scrim, image);
            an::tests::widgets::expectColour(picture.at(2.5, bar / 2.0), scrim, "the scrim at the left padding");
            const QColor edge = an::ui::compositeOver(t.colors.edge, image);
            an::tests::widgets::expectColour(picture.at(toolbar.width() / 2.0, bar + 0.5), edge, "the edge under it");
        });
        const an::ui::testing::RecordedText zoom = an::tests::widgets::recordedText(toolbar, QStringLiteral("100.00"));
        EXPECT_EQ(zoom.colour, t.colors.textOnImage) << an::tests::widgets::describedTexts(toolbar).toStdString();
    });
}
