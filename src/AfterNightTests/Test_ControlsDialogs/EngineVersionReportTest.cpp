// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The versions the about dialog, the system information dialog and the dashboard show: the product's, which the
/// application states, and the engine's and its libraries', as the loaded engine reports them; and what the about
/// dialog's licence says of the processes derived from other people's work and of the one that is not.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/AboutDialog.h"
#include "controls/DashboardIdentityBar.h"
#include "controls/EngineVersionReport.h"
#include "controls/SystemInformationDialog.h"

#include <AfterNightEngine/Core/DependencyReport.h>
#include <AfterNightEngine/Core/Version.h>

#include <QCoreApplication>
#include <QLabel>
#include <QTextBrowser>
#include <QTextEdit>

#include <gtest/gtest.h>

namespace
{

using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;

/// Sets the application's version for a case and restores it after.
class StatedVersion
{
public:
    explicit StatedVersion(const QString& version) : m_previous(QCoreApplication::applicationVersion())
    {
        QCoreApplication::setApplicationVersion(version);
    }
    ~StatedVersion()
    {
        QCoreApplication::setApplicationVersion(m_previous);
    }
    StatedVersion(const StatedVersion&) = delete;
    StatedVersion& operator=(const StatedVersion&) = delete;

private:
    QString m_previous;
};

TEST(EngineVersionReportTest, TheProductsVersionIsTheOneTheApplicationStates)
{
    {
        const StatedVersion stated(QStringLiteral(" 1.4.2 "));
        EXPECT_EQ(an::ui::productVersion(), QStringLiteral("1.4.2"));
    }
    const StatedVersion none{QString()};
    EXPECT_TRUE(an::ui::productVersion().isEmpty());
}

TEST(EngineVersionReportTest, TheEnginesVersionIsTheLoadedEnginesWithItsInterfaceGeneration)
{
    const QString text = an::ui::engineVersionText();
    EXPECT_TRUE(text.startsWith(QString::fromLatin1(an::engineVersionString()))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("interface generation %1").arg(an::engineAbiGeneration())))
        << text.toStdString();
}

TEST(EngineVersionReportTest, TheLibrariesAreTheEnginesReportInItsOrderEachShownAndLinked)
{
    std::vector<an::DependencyVersion> report(an::engineDependencyCount());
    ASSERT_TRUE(an::engineDependencies(report).ok());
    const std::vector<an::ui::EngineLibrary> libraries = an::ui::engineLibraries();

    ASSERT_EQ(libraries.size(), report.size());
    for (std::size_t index = 0u; index < report.size(); ++index)
    {
        EXPECT_EQ(libraries[index].name, QString::fromLatin1(report[index].name));
        // Every library the engine reports today has a name to show and a page to link.
        EXPECT_FALSE(libraries[index].label.isEmpty()) << report[index].name;
        EXPECT_TRUE(libraries[index].url.startsWith(QStringLiteral("http"))) << report[index].name;
        EXPECT_EQ(libraries[index].compiled, QString::fromLatin1(report[index].compiled.text).trimmed());
        EXPECT_EQ(libraries[index].loaded, QString::fromLatin1(report[index].loaded.text).trimmed());
    }
}

TEST(EngineVersionReportTest, ALibrarysVersionShowsBothVersionsWhenTheyDifferAndADashWhenNeitherIsKnown)
{
    an::ui::EngineLibrary library;
    EXPECT_EQ(an::ui::engineLibraryVersionText(library), QStringLiteral("—"));
    library.loaded = QStringLiteral("4.7");
    EXPECT_EQ(an::ui::engineLibraryVersionText(library), QStringLiteral("4.7"));
    library.compiled = QStringLiteral("4.7");
    EXPECT_EQ(an::ui::engineLibraryVersionText(library), QStringLiteral("4.7"));
    library.loaded = QStringLiteral("4.8");
    EXPECT_EQ(an::ui::engineLibraryVersionText(library), QStringLiteral("4.7 (loaded 4.8)"));
    library.loaded.clear();
    EXPECT_EQ(an::ui::engineLibraryVersionText(library), QStringLiteral("4.7"));
}

class VersionDialogsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
    }

    void TearDown() override
    {
        installed.reset();
        context.reset();
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

TEST_F(VersionDialogsTest, TheAboutDialogShowsTheProductsVersionTheEngineAndTheEnginesLibraries)
{
    const StatedVersion stated(QStringLiteral("3.2.1"));
    AboutDialog dialog;

    bool versionShown = false;
    for (QLabel* label : dialog.findChildren<QLabel*>())
    {
        versionShown = versionShown || label->text() == QStringLiteral("Version 3.2.1");
    }
    EXPECT_TRUE(versionShown);

    QString about;
    QString libraries;
    for (QTextBrowser* browser : dialog.findChildren<QTextBrowser*>())
    {
        const QString text = browser->toPlainText();
        if (text.contains(QStringLiteral("Version 3.2.1")))
        {
            about = text;
        }
        if (text.contains(QStringLiteral("Third")))
        {
            libraries = text;
        }
    }
    EXPECT_TRUE(about.contains(an::ui::engineVersionText())) << about.toStdString();
    ASSERT_FALSE(libraries.isEmpty());
    EXPECT_TRUE(libraries.contains(an::ui::engineVersionText())) << libraries.toStdString();
    for (const an::ui::EngineLibrary& library : an::ui::engineLibraries())
    {
        EXPECT_TRUE(libraries.contains(library.label)) << library.label.toStdString();
        EXPECT_TRUE(libraries.contains(an::ui::engineLibraryVersionText(library))) << library.label.toStdString();
    }
    EXPECT_TRUE(libraries.contains(QStringLiteral("Python and G'MIC are not part of the engine")));
}

TEST_F(VersionDialogsTest, TheSystemInformationListsTheProductsVersionTheEngineAndItsLibraries)
{
    const StatedVersion stated(QStringLiteral("3.2.1"));
    SystemInformationDialog dialog;

    auto* version = dialog.findChild<QLabel*>(QStringLiteral("appVersionValue"));
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(version->text(), QStringLiteral("3.2.1"));

    QString dependencies;
    for (QTextEdit* edit : dialog.findChildren<QTextEdit*>())
    {
        if (edit->toPlainText().contains(QStringLiteral("AfterNight engine")))
        {
            dependencies = edit->toPlainText();
        }
    }
    ASSERT_FALSE(dependencies.isEmpty());
    EXPECT_TRUE(dependencies.contains(an::ui::engineVersionText())) << dependencies.toStdString();
    for (const an::ui::EngineLibrary& library : an::ui::engineLibraries())
    {
        EXPECT_TRUE(dependencies.contains(library.label)) << library.label.toStdString();
    }
}

TEST_F(VersionDialogsTest, TheDashboardShowsTheProductsVersionAndDevWhenNoneIsStated)
{
    {
        const StatedVersion stated(QStringLiteral("3.2.1"));
        an::ui::DashboardIdentityBar bar;
        auto* label = bar.findChild<QLabel*>(QStringLiteral("dashboardIdentityBarVersion"));
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->text(), QStringLiteral("v3.2.1"));
    }
    const StatedVersion none{QString()};
    an::ui::DashboardIdentityBar bar;
    auto* label = bar.findChild<QLabel*>(QStringLiteral("dashboardIdentityBarVersion"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QStringLiteral("dev"));
}

/// The licence's paragraph under one provenance heading, up to the next heading of its kind.
QString provenanceSection(const QString& licence, const QString& heading)
{
    const qsizetype start = licence.indexOf(heading);
    if (start < 0)
    {
        return {};
    }
    const qsizetype end = licence.indexOf(QStringLiteral(" Provenance"), start + heading.size());
    return end < 0 ? licence.mid(start) : licence.mid(start, end - start);
}

TEST_F(VersionDialogsTest, TheLicenceCreditsEachDerivedProcessAndDescribesTheOriginalOneAsItIsNow)
{
    AboutDialog dialog;
    auto* browser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(browser, nullptr);
    const QString licence = browser->toPlainText();

    // NBRGB is AfterNight's own, and it calls no Linear Match.
    const QString nbrgb = provenanceSection(licence, QStringLiteral("NBRGB Combination Provenance"));
    ASSERT_FALSE(nbrgb.isEmpty()) << licence.toStdString();
    EXPECT_TRUE(nbrgb.contains(QStringLiteral("original AfterNight GPL code"))) << nbrgb.toStdString();
    EXPECT_TRUE(nbrgb.contains(QStringLiteral("does not include copied or translated Cosmic Photons")))
        << nbrgb.toStdString();
    EXPECT_FALSE(nbrgb.contains(QStringLiteral("Linear Match"))) << nbrgb.toStdString();

    // Each derived process says it is one, whose work it is and under which licence it is distributed.
    struct Derived
    {
        const char* heading;
        const char* author;
        const char* licence;
    };
    const Derived derived[] = {
        {"NBColorMapper Provenance", "Copyright (C) 2023, 2024 Mike Cranfield", "GPLv3"},
        {"Narrowband Palette Composer Provenance", "Franklin Marek", "GPL-3.0"},
        {"AutoGradientRemoval Provenance", "Copyright (C) 2026 Cyril Richard", "GPL-3.0-or-later"},
        // The engine's ports from copyleft works, which every process that demosaics, denoises, finds stars or
        // models a background reaches.
        {"RawTherapee Provenance", "Copyright (c) 2004-2010 Gabor Horvath", "GPL-3.0-or-later"},
        {"NL-Bayes Provenance", "Copyright (c) 2013, Marc Lebrun", "GPL-3.0-or-later"},
        {"Star Detection Provenance", "Copyright (C) 2005-2011 Francois Meyer", "GPL-3.0-or-later"},
        {"Background Sampling Provenance", "Copyright (C) 2005-2011 Francois Meyer", "GPL-3.0-or-later"},
    };
    for (const Derived& process : derived)
    {
        const QString section = provenanceSection(licence, QString::fromLatin1(process.heading));
        ASSERT_FALSE(section.isEmpty()) << process.heading;
        EXPECT_TRUE(section.contains(QStringLiteral("is a derived work of"))) << section.toStdString();
        EXPECT_TRUE(section.contains(QString::fromLatin1(process.author))) << section.toStdString();
        EXPECT_TRUE(section.contains(QString::fromLatin1(process.licence))) << section.toStdString();
        EXPECT_TRUE(section.contains(QStringLiteral("distributed under that licence"))) << section.toStdString();
    }
}

// A permissive licence lets the product carry the work under the GPL, and asks in return that its notice reach every
// binary copy: the three-clause BSD licence notices of DeepSkyStacker and of the STScI drizzle code, and the MIT
// notice of the waiting spinner. Each is credited where its provenance is, and reproduced among the third-party
// notices.
TEST_F(VersionDialogsTest, TheLicenceReproducesTheNoticeOfEveryPermissiveWorkTheProductCarries)
{
    AboutDialog dialog;
    auto* browser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(browser, nullptr);
    const QString licence = browser->toPlainText();

    struct Permissive
    {
        const char* heading;
        const char* credit;
        const char* licence;
    };
    const Permissive permissive[] = {
        {"DeepSkyStacker Provenance", "DeepSkyStacker", "BSD 3-Clause"},
        {"Drizzle Provenance", "Association of Universities for Research in Astronomy", "BSD 3-Clause"},
        {"Waiting Spinner Provenance", "QtWaitingSpinner", "MIT"},
    };
    for (const Permissive& work : permissive)
    {
        const QString section = provenanceSection(licence, QString::fromLatin1(work.heading));
        ASSERT_FALSE(section.isEmpty()) << work.heading << "\n" << licence.toStdString();
        EXPECT_TRUE(section.contains(QString::fromLatin1(work.credit))) << section.toStdString();
        EXPECT_TRUE(section.contains(QString::fromLatin1(work.licence))) << section.toStdString();
        EXPECT_TRUE(section.contains(QStringLiteral("notice"))) << section.toStdString();
    }

    const qsizetype notices = licence.indexOf(QStringLiteral("Third-Party Notices"));
    ASSERT_GE(notices, 0) << licence.toStdString();
    const QString section = licence.mid(notices);
    for (const char* line :
         {"Copyright (c) 2006-2019, LucCoiffier",
          "Copyright (c) 2018-2023, David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali Pelenjow, Michal "
          "Schulz, Martin Toeltsch",
          "Neither the name of the copyright holder nor the names of its contributors may be used to endorse "
          "or promote products derived from this software without specific prior written permission.",
          "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"",
          "Copyright (C) 2011,2014 Association of Universities for Research in Astronomy (AURA)",
          "The name of AURA and its representatives may not be used to endorse or promote products derived from this "
          "software without specific prior written permission.",
          "THIS SOFTWARE IS PROVIDED BY AURA ``AS IS''", "Original Work Copyright (c) 2012-2014 Alexander Turkin",
          "Modified 2014 by William Hallatt", "Modified 2015 by Jacob Dawid",
          "Permission is hereby granted, free of charge, to any person obtaining a copy of this software",
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND"})
    {
        EXPECT_TRUE(section.contains(QString::fromUtf8(line))) << line << "\n" << section.toStdString();
    }
}

// The Apache License asks a redistribution to carry a readable copy of the attribution a library's NOTICE file
// states, where the product shows its third-party notices. OpenFITS and OpenXISF, behind the FITS and XISF codecs,
// have one each; the part of OpenXISF's that speaks of the library's test data does not pertain to what the product
// ships.
TEST_F(VersionDialogsTest, TheLicenceCarriesTheNoticeOfEveryLibraryThatStatesOne)
{
    AboutDialog dialog;
    auto* browser = dialog.findChild<QTextBrowser*>(QStringLiteral("licenseBrowser"));
    ASSERT_NE(browser, nullptr);
    const QString licence = browser->toPlainText();

    const qsizetype notices = licence.indexOf(QStringLiteral("Third-Party Notices"));
    ASSERT_GE(notices, 0) << licence.toStdString();
    const QString section = licence.mid(notices);
    for (const char* line : {"OpenFITS",
                             "This product includes software developed by Ezequiel Ruiz and the "
                             "contributors to OpenFITS.",
                             "OpenFITS is an independent implementation of FITS, the Flexible Image Transport System, "
                             "written from the standard of the IAU FITS Working Group.",
                             "no code of CFITSIO is included. OpenFITS is not affiliated with or endorsed by the "
                             "International Astronomical Union or by NASA.",
                             "OpenXISF", "Copyright 2026 Ezequiel Ruiz",
                             "This product includes software developed by Ezequiel Ruiz and the contributors to "
                             "OpenXISF.",
                             "OpenXISF is an independent implementation of the XISF format. It is not affiliated with "
                             "or endorsed by Pleiades Astrophoto S.L. (PixInsight).",
                             "Apache License 2.0"})
    {
        EXPECT_TRUE(section.contains(QString::fromUtf8(line))) << line << "\n" << section.toStdString();
    }
    EXPECT_FALSE(section.contains(QStringLiteral("tests/data"))) << section.toStdString();
    EXPECT_FALSE(licence.contains(QStringLiteral("LibXISF"))) << licence.toStdString();
}

} // namespace
