// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Extract Sequence dialog as its builder makes it: its frame range controls, its labels left for the window to
/// fill, the theme spacing and Ok, Close and Reset.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>

#include "processes/ExtractSequenceProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

namespace
{

// The dialog opens at its size, laid out with the theme's spacing, with its frame range controls and its selection and
// size labels empty until the window fills them.
TEST(ExtractSequenceProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    auto ui = an::ui::processes::buildExtractSequenceProcessUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.startFrameValue, nullptr);
    ASSERT_NE(ui.startFrameSlider, nullptr);
    ASSERT_NE(ui.endFrameValue, nullptr);
    ASSERT_NE(ui.endFrameSlider, nullptr);
    ASSERT_NE(ui.selectedFramesLabel, nullptr);
    ASSERT_NE(ui.sequenceSizeLabel, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Extract Sequence"));
    EXPECT_EQ(host.size(), QSize(429, 203));
    EXPECT_EQ(host.minimumSize(), QSize(300, 150));
    EXPECT_EQ(ui.verticalLayout->contentsMargins(), QMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md));
    EXPECT_EQ(ui.selectedFramesLabel->minimumSize(), QSize(70, 0));
    EXPECT_EQ(ui.selectedFramesLabel->maximumWidth(), 70);
    EXPECT_EQ(ui.selectedFramesLabel->text(), QStringLiteral(""));
    EXPECT_EQ(ui.sequenceSizeLabel->text(), QStringLiteral(""));

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Ok), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

} // anonymous namespace
