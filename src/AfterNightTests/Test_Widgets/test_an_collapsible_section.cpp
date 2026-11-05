// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include "components/AnCollapsibleSection.h"
#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_StartsCollapsed)
{
    an::ui::AnCollapsibleSection section(QStringLiteral("Advanced"));

    ASSERT_NE(section.toggleButton(), nullptr);
    ASSERT_NE(section.contentWidget(), nullptr);

    EXPECT_FALSE(section.isExpanded());
    EXPECT_EQ(section.toggleButton()->arrowType(), Qt::RightArrow);
    EXPECT_TRUE(section.contentWidget()->isHidden());
    EXPECT_EQ(section.contentWidget()->maximumHeight(), 0);
}

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_ExpandsAndCollapses)
{
    an::ui::AnCollapsibleSection section(QStringLiteral("Registration"));
    auto* contentLayout = new QVBoxLayout();
    contentLayout->addWidget(new QLabel(QStringLiteral("Detection threshold")));
    section.setContentLayout(contentLayout);
    section.setAnimationDuration(0);

    EXPECT_EQ(section.contentLayout(), contentLayout);

    section.setExpanded(true);
    EXPECT_TRUE(section.isExpanded());
    EXPECT_EQ(section.toggleButton()->arrowType(), Qt::DownArrow);
    EXPECT_FALSE(section.contentWidget()->isHidden());
    EXPECT_GT(section.contentWidget()->maximumHeight(), 0);

    section.setExpanded(false);
    EXPECT_FALSE(section.isExpanded());
    EXPECT_EQ(section.toggleButton()->arrowType(), Qt::RightArrow);
    EXPECT_TRUE(section.contentWidget()->isHidden());
    EXPECT_EQ(section.contentWidget()->maximumHeight(), 0);
}

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnCollapsibleSection section(QStringLiteral("Histogram"));

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    section.refreshTheme();
    const QColor darkBtnColor = section.toggleButton()->palette().color(QPalette::ButtonText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    section.refreshTheme();
    const QColor lightBtnColor = section.toggleButton()->palette().color(QPalette::ButtonText);

    EXPECT_TRUE(darkBtnColor.isValid());
    EXPECT_TRUE(lightBtnColor.isValid());
    EXPECT_NE(darkBtnColor, lightBtnColor);
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

} // anonymous namespace

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_ExpandsImmediatelyWhenAnimationsDisabled)
{
    ScopedAnimationRestore guard;
    an::ui::AnimationUtils::setAnimationsEnabled(false);

    an::ui::AnCollapsibleSection section(QStringLiteral("Stacking"));
    auto* contentLayout = new QVBoxLayout();
    contentLayout->addWidget(new QLabel(QStringLiteral("Kappa sigma")));
    section.setContentLayout(contentLayout);
    // Keep animation object duration > 0 to ensure it is the toggle — not duration — that skips it.
    section.setAnimationDuration(170);

    section.setExpanded(true);

    // With animations disabled, expanded state should be applied synchronously.
    EXPECT_TRUE(section.isExpanded());
    EXPECT_FALSE(section.contentWidget()->isHidden());
    EXPECT_GT(section.contentWidget()->maximumHeight(), 0);
}

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_CollapsesImmediatelyWhenAnimationsDisabled)
{
    ScopedAnimationRestore guard;
    an::ui::AnimationUtils::setAnimationsEnabled(false);

    an::ui::AnCollapsibleSection section(QStringLiteral("Advanced"));
    auto* contentLayout = new QVBoxLayout();
    contentLayout->addWidget(new QLabel(QStringLiteral("Option")));
    section.setContentLayout(contentLayout);
    section.setAnimationDuration(170);
    section.setExpanded(true);

    section.setExpanded(false);

    EXPECT_FALSE(section.isExpanded());
    EXPECT_TRUE(section.contentWidget()->isHidden());
    EXPECT_EQ(section.contentWidget()->maximumHeight(), 0);
}

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_DisabledStateGraysTitleAndLabels)
{
    an::ui::AnCollapsibleSection section(QStringLiteral("Scientific Color Reconstruction"));
    auto* contentLayout = new QVBoxLayout();
    auto* label = new QLabel(QStringLiteral("Color Grip"));
    contentLayout->addWidget(label);
    section.setContentLayout(contentLayout);
    section.setExpanded(true);
    section.refreshTheme();

    const QColor disabledText = an::ui::tokens().colors.textTertiary;
    ASSERT_TRUE(disabledText.isValid());
    EXPECT_EQ(section.toggleButton()->palette().color(QPalette::Disabled, QPalette::ButtonText), disabledText);
    EXPECT_EQ(label->palette().color(QPalette::Disabled, QPalette::WindowText), disabledText);
}

TEST(WidgetsComponentsAnCollapsibleSection, AnCollapsibleSection_ExpandedHeightTracksLateWrappedTextLayout)
{
    an::ui::AnCollapsibleSection section(QStringLiteral("Stacking"));
    section.resize(360, 240);

    auto* contentLayout = new QVBoxLayout();
    auto* label = new QLabel(QStringLiteral("Short help text"));
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    contentLayout->addWidget(label);

    section.setContentLayout(contentLayout);
    section.contentWidget()->resize(300, 1);
    section.setAnimationDuration(0);
    section.setExpanded(true);

    const int initialHeight = section.contentWidget()->minimumHeight();
    label->setText(QStringLiteral(
        "Buckets frames by exposure time, stacks each bucket, then blends highlights from shorter exposures into the "
        "long-exposure master. This mirrors the Stacking dialog text that is applied after sections are expanded."));
    label->updateGeometry();

    QEvent layoutRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(section.contentWidget(), &layoutRequest);
    QCoreApplication::processEvents();

    ASSERT_GT(section.contentWidget()->width(), 0);
    ASSERT_TRUE(section.contentLayout()->hasHeightForWidth());

    const int expectedHeight = section.contentLayout()->heightForWidth(section.contentWidget()->width());
    EXPECT_GT(section.contentWidget()->minimumHeight(), initialHeight);
    EXPECT_GE(section.contentWidget()->minimumHeight(), expectedHeight);
}
