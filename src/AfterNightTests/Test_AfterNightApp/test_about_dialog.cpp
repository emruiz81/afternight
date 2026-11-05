// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// AboutDialog card-layout modernization tests.
#include <gtest/gtest.h>

#include <QDate>
#include <QFrame>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>

#include "controls/AboutDialog.h"

TEST(AboutDialogTest, AboutDialog_UsesCardLayoutForHeaderAndTabs)
{
    AboutDialog dialog(nullptr, false);

    auto* headerCard = dialog.findChild<QWidget*>(QStringLiteral("aboutHeaderCard"));
    ASSERT_NE(headerCard, nullptr);

    auto* tabWidget = dialog.findChild<QTabWidget*>(QStringLiteral("aboutTabWidget"));
    ASSERT_NE(tabWidget, nullptr);
    EXPECT_EQ(tabWidget->count(), 3);

    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aboutTabCardAbout")), nullptr);
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aboutTabCardLibraries")), nullptr);
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aboutTabCardLicense")), nullptr);
}

TEST(AboutDialogTest, AboutDialog_MarkdownBrowsersUseModernConfiguration)
{
    AboutDialog dialog(nullptr, true);

    auto* aboutBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("aboutBrowser"));
    auto* librariesBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("librariesBrowser"));
    auto* licenseBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));

    ASSERT_NE(aboutBrowser, nullptr);
    ASSERT_NE(librariesBrowser, nullptr);
    ASSERT_NE(licenseBrowser, nullptr);

    EXPECT_TRUE(aboutBrowser->openExternalLinks());
    EXPECT_TRUE(librariesBrowser->openExternalLinks());
    EXPECT_TRUE(licenseBrowser->openExternalLinks());

    EXPECT_EQ(aboutBrowser->frameShape(), QFrame::NoFrame);
    EXPECT_EQ(librariesBrowser->frameShape(), QFrame::NoFrame);
    EXPECT_EQ(licenseBrowser->frameShape(), QFrame::NoFrame);

    ASSERT_NE(aboutBrowser->document(), nullptr);
    EXPECT_FALSE(aboutBrowser->document()->defaultStyleSheet().isEmpty());
}

TEST(AboutDialogTest, AboutDialog_CloseButtonUsesPrimaryVariant)
{
    AboutDialog dialog;

    auto* closeButton = dialog.findChild<QPushButton*>(QStringLiteral("aboutCloseButton"));
    ASSERT_NE(closeButton, nullptr);

    EXPECT_EQ(closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(closeButton->isDefault());
}

TEST(AboutDialogTest, AboutDialog_LicenseMentionsNBRGBCleanRoomProvenance)
{
    AboutDialog dialog;

    auto* licenseBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(licenseBrowser, nullptr);

    const QString licenseText = licenseBrowser->toPlainText();
    EXPECT_TRUE(licenseText.contains(QStringLiteral("NBRGB Combination Provenance")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("original AfterNight GPL code")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("does not include copied or translated Cosmic Photons")));
}

TEST(AboutDialogTest, AboutDialog_LicenseMentionsNBColorMapperProvenance)
{
    AboutDialog dialog;

    auto* licenseBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(licenseBrowser, nullptr);

    const QString licenseText = licenseBrowser->toPlainText();
    EXPECT_TRUE(licenseText.contains(QStringLiteral("NBColorMapper")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("NB Color Mapper")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Mike Cranfield")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Cosmic Photons")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("GPLv3")));
}

TEST(AboutDialogTest, AboutDialog_LicenseMentionsNarrowbandPaletteComposerProvenance)
{
    AboutDialog dialog;

    auto* licenseBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(licenseBrowser, nullptr);

    const QString licenseText = licenseBrowser->toPlainText();
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Narrowband Palette Composer")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("SetiAstro SASPro")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Franklin Marek")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("GPL-3.0")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Python runtime objects")));
}

// AfterNight was first published in 2026, so its copyright runs from that year; a later year extends the range.
TEST(AboutDialogTest, AboutDialog_CopyrightRunsFromTheYearOfFirstPublication)
{
    AboutDialog dialog;

    const int year = QDate::currentDate().year();
    const QString expected = year > 2026 ? QStringLiteral("Copyright © 2026-%1 Ezequiel Ruiz.").arg(year)
                                         : QStringLiteral("Copyright © 2026 Ezequiel Ruiz.");
    for (const QString& name : {QStringLiteral("aboutBrowser"), QStringLiteral("licenseBrowser")})
    {
        auto* browser = dialog.findChild<QTextBrowser*>(name);
        ASSERT_NE(browser, nullptr) << name.toStdString();
        const QString text = browser->toPlainText();
        EXPECT_TRUE(text.contains(expected)) << name.toStdString();
        EXPECT_FALSE(text.contains(QStringLiteral("Copyright © 2024"))) << name.toStdString();
    }
}

TEST(AboutDialogTest, AboutDialog_LicenseMentionsAutoGradientRemovalProvenance)
{
    AboutDialog dialog;

    auto* licenseBrowser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(licenseBrowser, nullptr);

    const QString licenseText = licenseBrowser->toPlainText();
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Siril")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("AutoGradientRemoval")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("Cyril Richard")));
    EXPECT_TRUE(licenseText.contains(QStringLiteral("GPL-3.0-or-later")));
}
