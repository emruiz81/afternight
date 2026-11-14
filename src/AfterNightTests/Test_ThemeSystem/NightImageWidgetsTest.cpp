// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The list of the widgets that paint a picture, held to the sources: every source of the Controls library that paints a
// picture - a drawImage, a drawPixmap, a label's setPixmap - is on the list, every entry's source paints one, and an
// entry shows its picture under Night the way it says: through nightImage or a NightPicture, through the presentation's
// Night stage, or in colours Night maps.

#include "theme/NightImageWidgets.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

namespace
{

// The repository the suite was built from: the first folder at or above the working directory, the suite's own folder
// or this source file that holds both `src` and `build`. Empty when there is none.
QString repositoryRoot()
{
    QStringList seeds = {QDir::currentPath()};
    if (QCoreApplication::instance() != nullptr)
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }
    seeds.append(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    for (const QString& seed : seeds)
    {
        QDir folder(seed);
        do
        {
            if (QFileInfo::exists(folder.filePath(QStringLiteral("src"))) &&
                QFileInfo::exists(folder.filePath(QStringLiteral("build"))))
            {
                return folder.absolutePath();
            }
        } while (folder.cdUp());
    }
    return {};
}

/// The text with its line and block comments blanked, so a call named in a comment is not a call.
QString withoutComments(QString text)
{
    static const QRegularExpression block(QStringLiteral("/\\*.*?\\*/"),
                                          QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression line(QStringLiteral("//[^\\n]*"));
    text.replace(block, QStringLiteral(" "));
    text.replace(line, QString());
    return text;
}

QString readSource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }
    return withoutComments(QString::fromUtf8(file.readAll()));
}

/// Whether source text paints a picture: draws an image or a pixmap, sets a label's pixmap, or hands a picture to a
/// NightPicture or a NightPictureLabel.
bool paintsAPicture(const QString& code)
{
    static const QRegularExpression call(
        QStringLiteral("\\b(drawImage|drawPixmap|drawTiledPixmap|setPixmap|setPicture)\\s*\\("));
    return call.match(code).hasMatch();
}

/// A source and the header beside it, which may declare what the source paints with.
QString readSourceAndHeader(const QString& path)
{
    QString header = path;
    header.chop(4);
    header += QStringLiteral(".h");
    return readSource(path) + QLatin1Char('\n') + readSource(header);
}

} // namespace

TEST(NightImage, EveryWidgetThatPaintsAPictureIsOnTheList)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty()) << "the repository's sources were not found";
    const QDir library(QDir(root).filePath(QStringLiteral("src/AfterNightControls")));
    ASSERT_TRUE(library.exists());

    QSet<QString> listed;
    for (const an::ui::NightImageWidget& entry : an::ui::kNightImageWidgets)
    {
        const QString source = QString::fromLatin1(entry.source);
        EXPECT_FALSE(listed.contains(source)) << entry.source << " is listed twice";
        listed.insert(source);
        const QString code = readSourceAndHeader(library.filePath(source));
        ASSERT_FALSE(code.isEmpty()) << entry.source << " is listed and cannot be read";
        EXPECT_TRUE(paintsAPicture(code)) << entry.widget << " (" << entry.source << ") paints no picture: drop it";
        EXPECT_TRUE(code.contains(QString::fromLatin1(entry.widget))) << entry.source << " has no " << entry.widget;
        switch (entry.use)
        {
        case an::ui::NightImageUse::Picture:
            EXPECT_TRUE(code.contains(QStringLiteral("NightPicture")) || code.contains(QStringLiteral("nightImage(")) ||
                        code.contains(QStringLiteral("ImagePreviewWidget"), Qt::CaseInsensitive))
                << entry.widget << " says it shows a Night picture and asks for none";
            break;
        case an::ui::NightImageUse::Presentation:
            EXPECT_TRUE(code.contains(QStringLiteral("isNightMode()")))
                << entry.widget << " says it presents in Night's form and never asks for Night";
            break;
        case an::ui::NightImageUse::Colours:
            EXPECT_TRUE(code.contains(QStringLiteral("colorAsShown(")) || code.contains(QStringLiteral("tokens()")))
                << entry.widget << " says it draws in colours Night maps and reads none";
            break;
        case an::ui::NightImageUse::Handed:
        case an::ui::NightImageUse::Icon:
        case an::ui::NightImageUse::NeverShown:
            break;
        }
    }

    QStringList unlisted;
    QDirIterator sources(library.absolutePath(), {QStringLiteral("*.cpp")}, QDir::Files, QDirIterator::Subdirectories);
    while (sources.hasNext())
    {
        const QString path = sources.next();
        const QString source = library.relativeFilePath(path);
        // The Night pictures themselves, which every entry shows its picture through.
        if (source == QStringLiteral("theme/NightImage.cpp"))
        {
            continue;
        }
        if (paintsAPicture(readSource(path)) && !listed.contains(source))
        {
            unlisted.append(source);
        }
    }
    EXPECT_TRUE(unlisted.isEmpty())
        << "these paint a picture and are not on theme/NightImageWidgets.h, which says how each shows it under Night: "
        << unlisted.join(QStringLiteral(", ")).toStdString();
}
