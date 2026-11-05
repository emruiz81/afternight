// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AboutDialog.h"
#include "app/UiStyleTokens.h"
#include "components/AnCardWidget.h"
#include "controls/EngineVersionReport.h"
#include "controls/ProcessTabStyling.h"
#include "theme/AnDesignTokens.h"
#include <verdigris/wobjectimpl.h>

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTextBrowser>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QDate>
#include <QTextStream>
#include <QTextDocument>
#include <QVariant>
#include <QWidget>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>

#if __has_include(<kddockwidgets/kddockwidgets_version.h>)
#include <kddockwidgets/kddockwidgets_version.h>
#endif

#if __has_include(<cmark-gfm_version.h>)
#include <cmark-gfm_version.h>
#elif __has_include(<cmark_version.h>)
#include <cmark_version.h>
#endif

W_OBJECT_IMPL(AboutDialog)

namespace
{
/// The copyright notice: from 2026, the year AfterNight was first published, to the current year.
QString copyrightNotice()
{
    const int year = QDate::currentDate().year();
    return year > 2026 ? QStringLiteral("Copyright © 2026-%1 Ezequiel Ruiz.").arg(year)
                       : QStringLiteral("Copyright © 2026 Ezequiel Ruiz.");
}

/// The product's version as the application states it; "dev" in a process that states none.
QString displayedProductVersion()
{
    const QString version = an::ui::productVersion();
    return version.isEmpty() ? QStringLiteral("dev") : version;
}

std::string getKDDockWidgetsVersion()
{
#ifdef KDDOCKWIDGETS_VERSION_STRING
    return KDDOCKWIDGETS_VERSION_STRING;
#else
    return {};
#endif
}

std::string querySqliteVersion()
{
    QString version;
    const QString connectionName =
        QStringLiteral("an_about_sqlite_version_%1").arg(reinterpret_cast<quintptr>(&version));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(QStringLiteral(":memory:"));

        if (db.open())
        {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("select sqlite_version()")) && query.next())
            {
                version = query.value(0).toString();
            }
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return version.toStdString();
}

std::string getMarkdownBackendVersion()
{
#ifdef CMARK_GFM_VERSION_STRING
    return CMARK_GFM_VERSION_STRING;
#elif defined(CMARK_VERSION_STRING)
    return CMARK_VERSION_STRING;
#else
    return {};
#endif
}

QString toCssColor(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString buildMarkdownCss(const QString& accent, const QString& border, const QString& headingSecondary,
                         const QString& textPrimary, const QString& accentHover, const QString& codeBackground,
                         const QString& accentSoft)
{
    const auto& typo = an::ui::tokens().typography;
    const auto bodyPt = QString::number(typo.body.pointSizeF(), 'f', 1);
    const auto h1Pt = QString::number(typo.heading1.pointSizeF(), 'f', 1);
    const auto h2Pt = QString::number(typo.heading2.pointSizeF(), 'f', 1);
    const auto h3Pt = QString::number(typo.heading3.pointSizeF(), 'f', 1);

    return QString("body { font-size: %8pt; line-height: 1.55; }\n"
                   "h1 { font-size: %9pt; font-weight: 700; margin: 4px 0 12px 0; color: %1; }\n"
                   "h2 { font-size: %10pt; font-weight: 700; margin: 18px 0 8px 0; color: %1; border-bottom: 2px solid "
                   "%2; padding-bottom: 6px; }\n"
                   "h3 { font-size: %11pt; font-weight: 600; margin: 14px 0 6px 0; color: %3; }\n"
                   "p { margin: 8px 0; color: %4; }\n"
                   "ul { margin: 8px 0; padding-left: 22px; }\n"
                   "li { margin: 6px 0; }\n"
                   "a { color: %1; text-decoration: none; font-weight: 600; }\n"
                   "a:hover { color: %5; text-decoration: underline; }\n"
                   "code { background: %6; color: %1; border: 1px solid %2; border-radius: 6px; padding: 2px 6px; }\n"
                   "pre { background: %6; color: %4; border: 1px solid %2; border-radius: 8px; padding: 10px 12px; }\n"
                   "blockquote { background: %7; color: %3; border-left: 4px solid %2; border-radius: 8px; margin: "
                   "12px 0; padding: 8px 12px; }\n"
                   "hr { border: none; border-top: 1px solid %2; margin: 16px 0; }\n")
        .arg(accent)
        .arg(border)
        .arg(headingSecondary)
        .arg(textPrimary)
        .arg(accentHover)
        .arg(codeBackground)
        .arg(accentSoft)
        .arg(bodyPt)
        .arg(h1Pt)
        .arg(h2Pt)
        .arg(h3Pt);
}

void configureMarkdownBrowser(QTextBrowser* browser, const QString& markdown, const QString& css)
{
    if (!browser)
    {
        return;
    }

    browser->setOpenExternalLinks(true);
    if (auto* doc = browser->document())
    {
        doc->setDefaultStyleSheet(css);
        doc->setDocumentMargin(an::ui::tokens().spacing.md);
    }

    browser->setMarkdown(markdown);
    browser->setFrameStyle(QFrame::NoFrame);
    browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

    QPalette pal = browser->palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    browser->setPalette(pal);
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent, bool darkMode) : QDialog(parent), m_DarkMode(darkMode)
{
    setupUi();
    setWindowModality(Qt::WindowModal);
    setModal(true);

    // Set minimum size and center on screen
    setMinimumSize(700, 550);
    resize(880, 640);

    // Center on parent or primary screen available geometry (exclude taskbar)
    const auto centerTo = [this](const QRect& area) {
        const QSize s = size();
        const int x = area.x() + (area.width() - s.width()) / 2;
        const int y = area.y() + (area.height() - s.height()) / 2;
        move(x, y);
    };

    if (parent)
    {
        centerTo(parent->window()->screen()->availableGeometry());
    }
    else if (const QScreen* scr = QApplication::primaryScreen())
    {
        centerTo(scr->availableGeometry());
    }
}

void AboutDialog::setupUi()
{
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About AfterNight"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const auto& tokenSet = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(tokenSet.spacing.md);
    mainLayout->setContentsMargins(tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.md);

    auto* headerCard = new an::ui::AnCardWidget(QString(), this);
    headerCard->setObjectName(QStringLiteral("aboutHeaderCard"));
    auto* headerLayout = new QVBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(tokenSet.spacing.xs);

    auto* titleLabel = new QLabel(tr("AfterNight"), headerCard);
    titleLabel->setObjectName(QStringLiteral("aboutTitleLabel"));
    QFont titleFont = tokenSet.typography.heading1;
    titleFont.setWeight(QFont::Bold);
    titleLabel->setFont(titleFont);
    QPalette titlePalette = titleLabel->palette();
    titlePalette.setColor(QPalette::WindowText, tokenSet.colors.textAccent);
    titleLabel->setPalette(titlePalette);

    const QString versionText = tr("Version %1").arg(displayedProductVersion());
    auto* versionLabel = new QLabel(versionText, headerCard);
    versionLabel->setObjectName(QStringLiteral("aboutVersionLabel"));
    versionLabel->setFont(tokenSet.typography.body);
    QPalette versionPalette = versionLabel->palette();
    versionPalette.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
    versionLabel->setPalette(versionPalette);

    auto* subtitleLabel = new QLabel(tr("Advanced astrophotography processing software"), headerCard);
    subtitleLabel->setObjectName(QStringLiteral("aboutSubtitleLabel"));
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setFont(tokenSet.typography.caption);
    QPalette subtitlePalette = subtitleLabel->palette();
    subtitlePalette.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
    subtitleLabel->setPalette(subtitlePalette);

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(versionLabel);
    headerLayout->addWidget(subtitleLabel);
    headerCard->setContentLayout(headerLayout);
    mainLayout->addWidget(headerCard);

    // Create tab widget
    m_TabWidget = new QTabWidget(this);
    m_TabWidget->setObjectName(QStringLiteral("aboutTabWidget"));
    an::ui::processes::applyStandardProcessTabBarSizing(m_TabWidget);
    mainLayout->addWidget(m_TabWidget, 1);

    setupAboutTab();
    setupLibrariesTab();
    setupLicenseTab();

    // Close button footer
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch();
    m_CloseButton = new QPushButton(tr("Close"), this);
    m_CloseButton->setObjectName(QStringLiteral("aboutCloseButton"));
    m_CloseButton->setProperty("buttonVariant", QStringLiteral("primary"));
    m_CloseButton->setDefault(true);
    m_CloseButton->setMinimumWidth(100);
    connect(m_CloseButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_CloseButton);

    mainLayout->addLayout(buttonLayout);
}

void AboutDialog::setupAboutTab()
{
    m_AboutBrowser = new QTextBrowser(this);
    m_AboutBrowser->setObjectName(QStringLiteral("aboutBrowser"));

    const auto c = resolveThemePalette();
    configureMarkdownBrowser(m_AboutBrowser, getAboutText(),
                             buildMarkdownCss(c.accent, c.surfaceBorder, c.textSecondary, c.textPrimary, c.accentHover,
                                              c.codeBackground, c.accentSoft));

    auto* page = new QWidget(m_TabWidget);
    auto* layout = new QVBoxLayout(page);
    const auto& tokenSet = an::ui::tokens();
    layout->setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm);
    layout->setSpacing(tokenSet.spacing.sm);

    auto* card = new an::ui::AnCardWidget(tr("Project Overview"), page);
    card->setObjectName(QStringLiteral("aboutTabCardAbout"));
    auto* cardLayout = new QVBoxLayout();
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(m_AboutBrowser);
    card->setContentLayout(cardLayout);
    layout->addWidget(card);

    m_TabWidget->addTab(page, tr("About"));
}

void AboutDialog::setupLibrariesTab()
{
    m_LibrariesBrowser = new QTextBrowser(this);
    m_LibrariesBrowser->setObjectName(QStringLiteral("librariesBrowser"));

    const auto c = resolveThemePalette();
    configureMarkdownBrowser(m_LibrariesBrowser, getLibrariesText(),
                             buildMarkdownCss(c.accent, c.surfaceBorder, c.textSecondary, c.textPrimary, c.accentHover,
                                              c.codeBackground, c.accentSoft));

    auto* page = new QWidget(m_TabWidget);
    auto* layout = new QVBoxLayout(page);
    const auto& tokenSet = an::ui::tokens();
    layout->setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm);
    layout->setSpacing(tokenSet.spacing.sm);

    auto* card = new an::ui::AnCardWidget(tr("Dependencies"), page);
    card->setObjectName(QStringLiteral("aboutTabCardLibraries"));
    auto* cardLayout = new QVBoxLayout();
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(m_LibrariesBrowser);
    card->setContentLayout(cardLayout);
    layout->addWidget(card);

    m_TabWidget->addTab(page, tr("Libraries"));
}

void AboutDialog::setupLicenseTab()
{
    m_LicenseBrowser = new QTextBrowser(this);
    m_LicenseBrowser->setObjectName(QStringLiteral("licenseBrowser"));

    const auto c = resolveThemePalette();
    configureMarkdownBrowser(m_LicenseBrowser, getLicenseText(),
                             buildMarkdownCss(c.accent, c.surfaceBorder, c.textSecondary, c.textPrimary, c.accentHover,
                                              c.codeBackground, c.accentSoft));

    auto* page = new QWidget(m_TabWidget);
    auto* layout = new QVBoxLayout(page);
    const auto& tokenSet = an::ui::tokens();
    layout->setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm);
    layout->setSpacing(tokenSet.spacing.sm);

    auto* card = new an::ui::AnCardWidget(tr("License Details"), page);
    card->setObjectName(QStringLiteral("aboutTabCardLicense"));
    auto* cardLayout = new QVBoxLayout();
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(m_LicenseBrowser);
    card->setContentLayout(cardLayout);
    layout->addWidget(card);

    m_TabWidget->addTab(page, tr("License"));
}

AboutDialog::ThemePalette AboutDialog::resolveThemePalette() const
{
    const auto themeVariant = m_DarkMode ? an::ui::styling::ThemeVariant::Dark : an::ui::styling::ThemeVariant::Light;
    const auto color = [themeVariant](const an::ui::styling::SemanticColorRole role) {
        return toCssColor(an::ui::styling::semanticColor(themeVariant, role));
    };

    return ThemePalette{color(an::ui::styling::SemanticColorRole::SurfaceWindow),
                        color(an::ui::styling::SemanticColorRole::SurfaceElevated),
                        color(an::ui::styling::SemanticColorRole::SurfaceBorder),
                        color(an::ui::styling::SemanticColorRole::TextPrimary),
                        color(an::ui::styling::SemanticColorRole::TextSecondary),
                        color(an::ui::styling::SemanticColorRole::TextMuted),
                        color(an::ui::styling::SemanticColorRole::AccentPrimary),
                        color(an::ui::styling::SemanticColorRole::AccentHover),
                        color(an::ui::styling::SemanticColorRole::AccentSoft),
                        color(an::ui::styling::SemanticColorRole::HighlightBackground),
                        color(an::ui::styling::SemanticColorRole::HighlightBorder),
                        color(an::ui::styling::SemanticColorRole::CodeBackground),
                        color(an::ui::styling::SemanticColorRole::WarningBackground),
                        color(an::ui::styling::SemanticColorRole::WarningBorder),
                        color(an::ui::styling::SemanticColorRole::WarningText),
                        color(an::ui::styling::SemanticColorRole::DangerBackground),
                        color(an::ui::styling::SemanticColorRole::DangerBorder),
                        color(an::ui::styling::SemanticColorRole::DangerText)};
}

QString AboutDialog::getAboutText() const
{
    const QString version = displayedProductVersion();
    const QString copyright = copyrightNotice();
    const QString licenseLine =
        QStringLiteral("Released under the GNU General Public License v3.0, with separately licensed "
                       "lite/protocol extension components.");

    QString md;
    QTextStream s(&md);
    s << "# AfterNight - Version " << version << "\n\n";
    s << "Advanced Astrophotography Processing Software\n\n";
    s << "Engine " << an::ui::engineVersionText() << "\n\n";
    s << "## About\n\n";
    s << "AfterNight is a comprehensive astrophotography processing platform designed for uncompromising image quality "
         "and a modern workflow.\n\n";
    s << "Built with performance‑focused C++20 code and trusted imaging libraries, it delivers precision and speed "
         "while remaining approachable night after night.\n\n";
    s << "## Developer\n\n";
    s << "**Ezequiel Ruiz**  ";
    s << "<" << "mailto:ezequiel.ruiz@gmail.com" << ">\n\n";
    s << copyright << "\n";
    s << licenseLine << "\n\n";
    s << "The protocol-only extension boundary is documented separately so non-engine extension integrations can be "
         "reviewed without implying access to AfterNight's GPL Engine or native controls.\n\n";
    s << "> ⚠️ **Development Status**\\\n";
    s << "> The project is under active development. Features evolve quickly and some capabilities remain "
         "experimental. Keep pristine backups of your data before processing.\n\n";
    s << "## Support & Feedback\n\n";
    s << "Have feedback or found a bug? Reach out via email while we prepare the public issue tracker.\n\n";
    s << "Your insight helps shape upcoming releases.\n\n";
    s << "---\n\n";
    s << "Thank you for using AfterNight! Clear skies! 🌙\n";
    return md;
}

QString AboutDialog::getLibrariesText() const
{
    const std::string qtVersion = qVersion();
    const std::string kddockwidgetsVersion = getKDDockWidgetsVersion();
    const std::string sqliteVersion = querySqliteVersion();
    const std::string crashpadVersion;
    const std::string cmarkVersion = getMarkdownBackendVersion();

    const auto sv = [](const std::string& v) {
        const QString q = QString::fromStdString(v).trimmed();
        return q.isEmpty() ? QStringLiteral("—") : q;
    };

    QString md;
    QTextStream s(&md);
    s << "# Third‑party Libraries\n\n";
    s << "AfterNight builds on a proven foundation of open‑source projects. We are grateful to the maintainers and "
         "communities that make these technologies possible.\n\n";

    s << "### Core Framework\n\n";
    s << "- [Qt Application Framework](https://www.qt.io/) — `" << sv(qtVersion) << "`\n";
    s << "- [KDDockWidgets](https://github.com/KDAB/KDDockWidgets) — `" << sv(kddockwidgetsVersion) << "`\n";
    s << "- [SQLite](https://www.sqlite.org/) — `" << sv(sqliteVersion) << "`\n\n";

    // The engine states its own libraries, each with the version its headers declared and the one loaded beside it.
    s << "### Engine " << an::ui::engineVersionText() << "\n\n";
    const std::vector<an::ui::EngineLibrary> libraries = an::ui::engineLibraries();
    if (libraries.empty())
    {
        s << "The engine did not report its libraries.\n";
    }
    for (const an::ui::EngineLibrary& library : libraries)
    {
        s << "- ";
        if (library.url.isEmpty())
        {
            s << library.label;
        }
        else
        {
            s << "[" << library.label << "](" << library.url << ")";
        }
        s << " — `" << an::ui::engineLibraryVersionText(library) << "`\n";
    }
    s << "\nPython and G'MIC are not part of the engine: the extension host runs Python, and G'MIC is reached through "
         "an optional adapter.\n\n";

    s << "### Runtime & Diagnostics\n\n";
    s << "- [Crashpad](https://chromium.googlesource.com/crashpad/crashpad/) — `" << sv(crashpadVersion) << "`\n";
    s << "- [cmark-gfm](https://github.com/github/cmark-gfm) / [cmark](https://github.com/commonmark/cmark) — `"
      << sv(cmarkVersion) << "`\n";

    return md;
}

QString AboutDialog::getLicenseText() const
{
    const QString copyright = copyrightNotice();

    QString md;
    QTextStream s(&md);
    s << "# GNU General Public License v3.0\n\n";
    s << "AfterNight is free software: you may redistribute it and/or modify it under the terms of the **GNU General "
         "Public License v3.0** as published by the Free Software Foundation.\n\n";
    s << "The main application, Engine, full extension host, native controls, and Engine-backed SDK modules remain "
         "GPLv3/GPL-compatible components.\n\n";
    s << "## Summary\n\n";
    s << "- Provide the source code and this license when you share the software.\n";
    s << "- Document your changes and keep derivative work under GPLv3.\n";
    s << "- Patent rights you grant must remain available to downstream users.\n\n";
    s << "## Component Licensing\n\n";
    s << "Some lite/protocol extension boundary components are intended to be distributed under **Apache-2.0** so "
         "protocol-only extensions can run without linking to AfterNight Engine or native-control code:\n\n";
    s << "- `AfterNightExtensionProtocol`\n";
    s << "- `AfterNightExtensionHostLite`\n";
    s << "- Protocol-only Python SDK modules such as `afternight.protocol`, `afternight.lite`, "
         "`afternight.ui_protocol`, and `afternight.sequences`\n\n";
    s << "Runtime/full-host extensions and Engine-backed SDK modules remain GPL-compatible because they link to or "
         "load "
         "AfterNight Engine and native-control components.\n\n";
    s << "## NBRGB Combination Provenance\n\n";
    s << "The native NBRGB Combination process is original AfterNight GPL code. Its continuum correction, "
         "narrowband boost and least-squares fit are AfterNight's own, and it does not include copied or translated "
         "Cosmic Photons, PixInsight/PJSR, or Pleiades source.\n\n";
    s << "## NBColorMapper Provenance\n\n";
    s << "The native NB Color Mapper process is a derived work of Cosmic Photons NBColorMapper v2.2 by Mike "
         "Cranfield, Copyright (C) 2023, 2024 Mike Cranfield, licensed under GPLv3: its layer mapping follows the "
         "script's method, and it is distributed under that licence. PixInsight/PJSR APIs and assets are not "
         "included; equivalent behavior is implemented with AfterNight Engine primitives and native Qt controls.\n\n";
    s << "## Narrowband Palette Composer Provenance\n\n";
    s << "The native Narrowband Palette Composer process is a derived work of the SetiAstro SASPro narrowband palette "
         "process by Franklin Marek, licensed under GPL-3.0: its palettes follow SASPro's formulas, and it is "
         "distributed under that licence. Python runtime objects, PyQt widgets, icons, and other SASPro UI assets "
         "are not included; equivalent behavior is implemented with AfterNight Engine primitives and native Qt "
         "controls.\n\n";
    s << "## AutoGradientRemoval Provenance\n\n";
    s << "The native Auto Gradient Removal process is a derived work of Siril AutoGradientRemoval 1.0.0 by Cyril "
         "Richard, Copyright (C) 2026 Cyril Richard, licensed under GPL-3.0-or-later: its background estimate follows "
         "the script's method, and it is distributed under that licence. The port uses AfterNight Engine "
         "image containers and native Qt controls; Siril UI/runtime objects are not included.\n\n";
    s << "## RawTherapee Provenance\n\n";
    s << "AfterNight Engine's demosaicing - the Fast, AMaZE, RCD, LMMSE, luminance-only AHD, VNG4, Markesteijn "
         "and fast X-Trans methods and the border rule they share - is a derived work of RawTherapee's raw "
         "processing code as librtprocess carries it, and so are the median networks of its median filter. That "
         "code is by Emil Martinec, Ingo Weyrich, Luis Sanz Rodriguez, Gabor Horvath, Jacques Desmis and "
         "Flössie, after Frank Markesteijn's X-Trans method; its files state the copyright lines Copyright (c) "
         "2004-2010 Gabor Horvath, copyright (c) 2008-2010 Emil Martinec, Copyright (c) 2017-2018 Luis Sanz "
         "Rodriguez and Ingo Weyrich, and Copyright (c) 2018 Ingo Weyrich, and it is licensed under "
         "GPL-3.0-or-later. AfterNight's derived work is distributed under that licence.\n\n";
    s << "## NL-Bayes Provenance\n\n";
    s << "AfterNight Engine's patch denoising estimator is a derived work of Marc Lebrun's implementation of NL-Bayes "
         "published on IPOL (Image Processing On Line), Copyright (c) 2013, Marc Lebrun, licensed under "
         "GPL-3.0-or-later: its parameter tables and patch arithmetic follow that implementation, and it is "
         "distributed under that licence.\n\n";
    s << "## Star Detection Provenance\n\n";
    s << "AfterNight Engine's star detection is a derived work of Siril's star finder and PSF fit, by Francois Meyer "
         "and team free-astro, Copyright (C) 2005-2011 Francois Meyer and Copyright (C) 2012-2026 team free-astro, "
         "licensed under GPL-3.0-or-later: its candidate scan, rejections, starting point and star models follow "
         "Siril's, and it is distributed under that licence.\n\n";
    s << "## Background Sampling Provenance\n\n";
    s << "AfterNight Engine's background sampling is a derived work of Siril's background extraction, Copyright (C) "
         "2005-2011 Francois Meyer and Copyright (C) 2012-2026 team free-astro, licensed under GPL-3.0-or-later: its "
         "sample grid, the grid's acceptance rule and the thin-plate spline's kernel and smoothing follow Siril's, "
         "and it is distributed under that licence.\n\n";
    s << "## Drizzle Provenance\n\n";
    s << "Drizzle's corner interpolation is adapted from Siril's interpolate_four_points, the single-precision form of "
         "the routine of the same name in the STScI drizzle library, Copyright (C) 2011,2014 Association of "
         "Universities for Research in Astronomy (AURA), licensed under the BSD 3-Clause terms. AfterNight "
         "distributes it under the GPL with that notice, which is reproduced under Third-Party Notices.\n\n";
    s << "## DeepSkyStacker Provenance\n\n";
    s << "The auto-adaptive weighted average and the star mask's six falloff shapes are adapted from DeepSkyStacker "
         "by Luc Coiffier and the DeepSkyStacker contributors, licensed under the BSD 3-Clause License. AfterNight "
         "distributes them under the GPL with that licence's notice, which is reproduced under Third-Party "
         "Notices.\n\n";
    s << "## Waiting Spinner Provenance\n\n";
    s << "The busy indicator is AfterNight's copy of QtWaitingSpinner by Alexander Turkin, William Hallatt and Jacob "
         "Dawid, licensed under the MIT License, changed to draw with the theme's colours; its notice is reproduced "
         "under Third-Party Notices.\n\n";
    s << "## Third-Party Notices\n\n";
    s << "AfterNight reads and writes XISF files through OpenXISF, distributed under the **Apache License 2.0**, "
         "whose NOTICE states:\n\n";
    s << "> OpenXISF\\\n";
    s << "> Copyright 2026 Ezequiel Ruiz\n>\n";
    s << "> This product includes software developed by Ezequiel Ruiz and the contributors to OpenXISF.\n>\n";
    s << "> OpenXISF is an independent implementation of the XISF format. It is not affiliated with or endorsed by "
         "Pleiades Astrophoto S.L. (PixInsight).\n\n";
    s << "AfterNight Engine adapts code from DeepSkyStacker, whose licence states:\n\n";
    s << "> BSD 3-Clause License\n>\n";
    s << "> Copyright (c) 2006-2019, LucCoiffier\\\n";
    s << "> Copyright (c) 2018-2023, David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali Pelenjow, "
         "Michal Schulz, Martin Toeltsch\n>\n";
    s << "> All rights reserved.\n>\n";
    s << "> Redistribution and use in source and binary forms, with or without modification, are permitted provided "
         "that the following conditions are met:\n>\n";
    s << "> - Redistributions of source code must retain the above copyright notice, this list of conditions and the "
         "following disclaimer.\n";
    s << "> - Redistributions in binary form must reproduce the above copyright notice, this list of conditions and "
         "the following disclaimer in the documentation and/or other materials provided with the distribution.\n";
    s << "> - Neither the name of the copyright holder nor the names of its contributors may be used to endorse or "
         "promote products derived from this software without specific prior written permission.\n>\n";
    s << "> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED "
         "WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A "
         "PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY "
         "DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, "
         "PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) "
         "HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING "
         "NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE "
         "POSSIBILITY OF SUCH DAMAGE.\n\n";
    s << "AfterNight Engine adapts code from the STScI drizzle library, by way of Siril, whose notice states:\n\n";
    s << "> Copyright (C) 2011,2014 Association of Universities for Research in Astronomy (AURA)\n>\n";
    s << "> Redistribution and use in source and binary forms, with or without modification, are permitted provided "
         "that the following conditions are met:\n>\n";
    s << "> 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the "
         "following disclaimer.\n";
    s << "> 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and "
         "the following disclaimer in the documentation and/or other materials provided with the distribution.\n";
    s << "> 3. The name of AURA and its representatives may not be used to endorse or promote products derived from "
         "this software without specific prior written permission.\n>\n";
    s << "> THIS SOFTWARE IS PROVIDED BY AURA \\`\\`AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT "
         "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. "
         "IN NO EVENT SHALL AURA BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL "
         "DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR "
         "PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, "
         "STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS "
         "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\n";
    s << "AfterNight's busy indicator is a copy of QtWaitingSpinner, whose notice states:\n\n";
    s << "> Original Work Copyright (c) 2012-2014 Alexander Turkin\\\n";
    s << "> Modified 2014 by William Hallatt\\\n";
    s << "> Modified 2015 by Jacob Dawid\n>\n";
    s << "> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and "
         "associated documentation files (the \"Software\"), to deal in the Software without restriction, including "
         "without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
         "copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the "
         "following conditions:\n>\n";
    s << "> The above copyright notice and this permission notice shall be included in all copies or substantial "
         "portions of the Software.\n>\n";
    s << "> THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT "
         "LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO "
         "EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER "
         "IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE "
         "USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n";
    s << "> ⚠️ **No Warranty**\\\n";
    s << "> This program is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even "
         "the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU GPL for "
         "details.\n\n";
    s << "The complete license text ships with the application in the `LICENSE` file and is available online at "
         "<https://www.gnu.org/licenses/gpl-3.0.html>.\n\n";
    s << "**Third‑Party Components:** AfterNight incorporates open‑source libraries governed by their respective "
         "licenses. See the \"Libraries\" tab for the full list.\n\n";
    s << "Questions about licensing? Contact <mailto:ezequiel.ruiz@gmail.com>.\n\n";
    s << copyright << "\n";
    return md;
}
