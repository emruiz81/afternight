// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The rules of the source tree no case of behaviour can hold, each read from the sources themselves: no bare
// std::min or std::max, which the Windows SDK's macros would expand; no stylesheet, since the look is painted from the
// design tokens; and no QSettings but the product's own settings file, since any other is the native store, which no
// test storage root redirects. A guard fails naming the file and the line, and says what to write instead.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

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

// The test tree, from the repository's root: its cases read what a guard forbids the product to write - a widget's
// stylesheet, to prove it empty; a settings file of their own in a temporary folder.
constexpr const char* kTestTree = "src/AfterNightTests/";

// The source text with every comment blanked, its line breaks kept so a position still names its line. String and
// character literals stay as they are: a name inside one is not a comment.
QString withoutComments(const QString& source)
{
    enum class In
    {
        Code,
        LineComment,
        BlockComment,
        String,
        Character,
        RawString
    };
    QString text = source;
    In state = In::Code;
    QString rawEnd;
    const qsizetype size = text.size();
    for (qsizetype index = 0; index < size; ++index)
    {
        const QChar character = text[index];
        const QChar next = index + 1 < size ? text[index + 1] : QChar();
        switch (state)
        {
        case In::Code:
            if (character == QLatin1Char('/') && next == QLatin1Char('/'))
            {
                state = In::LineComment;
                text[index] = QLatin1Char(' ');
            }
            else if (character == QLatin1Char('/') && next == QLatin1Char('*'))
            {
                state = In::BlockComment;
                text[index] = QLatin1Char(' ');
                text[index + 1] = QLatin1Char(' ');
                ++index;
            }
            else if (character == QLatin1Char('"'))
            {
                // R"delimiter( opens a raw string, which ends at )delimiter" and holds no escape.
                if (index > 0 && text[index - 1] == QLatin1Char('R'))
                {
                    const qsizetype open = text.indexOf(QLatin1Char('('), index);
                    if (open > index && open - index <= 17)
                    {
                        rawEnd = QLatin1Char(')') + text.mid(index + 1, open - index - 1) + QLatin1Char('"');
                        state = In::RawString;
                        index = open;
                        break;
                    }
                }
                state = In::String;
            }
            else if (character == QLatin1Char('\''))
            {
                // A quote between two digits separates them; any other opens a character literal.
                const bool separator = index > 0 && text[index - 1].isLetterOrNumber() && next.isLetterOrNumber() &&
                                       index + 2 < size && text[index + 2] != QLatin1Char('\'');
                if (!separator)
                {
                    state = In::Character;
                }
            }
            break;
        case In::LineComment:
            if (character == QLatin1Char('\n'))
            {
                state = In::Code;
            }
            else if (character != QLatin1Char('\r'))
            {
                text[index] = QLatin1Char(' ');
            }
            break;
        case In::BlockComment:
            if (character == QLatin1Char('*') && next == QLatin1Char('/'))
            {
                text[index] = QLatin1Char(' ');
                text[index + 1] = QLatin1Char(' ');
                ++index;
                state = In::Code;
            }
            else if (character != QLatin1Char('\n') && character != QLatin1Char('\r'))
            {
                text[index] = QLatin1Char(' ');
            }
            break;
        case In::String:
        case In::Character:
            if (character == QLatin1Char('\\'))
            {
                ++index;
            }
            else if (character == (state == In::String ? QLatin1Char('"') : QLatin1Char('\'')) ||
                     character == QLatin1Char('\n'))
            {
                state = In::Code;
            }
            break;
        case In::RawString:
            if (text.mid(index, rawEnd.size()) == rawEnd)
            {
                index += rawEnd.size() - 1;
                state = In::Code;
            }
            break;
        }
    }
    return text;
}

// What a guard found: the file from the repository's root, the line, and the line's text.
struct Finding
{
    QString file;
    int line = 0;
    QString text;

    [[nodiscard]] QString described() const
    {
        return QStringLiteral("%1:%2: %3").arg(file).arg(line).arg(text);
    }
};

// Every match of a pattern in the C and C++ sources under `src`, comments left out, the test tree read only when
// asked: the file, the line and the line's text of each.
QList<Finding> findingsInSources(const QString& root, const QRegularExpression& pattern, bool withTestTree)
{
    QList<Finding> findings;
    const QDir rootFolder(root);
    QDirIterator sources(rootFolder.filePath(QStringLiteral("src")),
                         {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"),
                          QStringLiteral("*.inl"), QStringLiteral("*.cxx"), QStringLiteral("*.cc")},
                         QDir::Files, QDirIterator::Subdirectories);
    while (sources.hasNext())
    {
        const QString path = sources.next();
        const QString relative = rootFolder.relativeFilePath(path);
        if (!withTestTree && relative.startsWith(QLatin1String(kTestTree)))
        {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }
        QString source = QString::fromUtf8(file.readAll());
        source.remove(QLatin1Char('\r'));
        const QString code = withoutComments(source);
        QRegularExpressionMatchIterator matches = pattern.globalMatch(code);
        if (!matches.hasNext())
        {
            continue;
        }
        const QStringList lines = source.split(QLatin1Char('\n'));
        while (matches.hasNext())
        {
            const QRegularExpressionMatch match = matches.next();
            const int line = static_cast<int>(code.left(match.capturedStart()).count(QLatin1Char('\n'))) + 1;
            findings.append(Finding{relative, line, lines.value(line - 1).trimmed()});
        }
    }
    return findings;
}

QString described(const QList<Finding>& findings)
{
    QStringList lines;
    for (const Finding& finding : findings)
    {
        lines.append(finding.described());
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

// No source calls std::min or std::max by its bare name: <windows.h> defines both as macros, which would expand the
// call. The name goes in parentheses - (std::max)(a, b).
TEST(SourceGuards, NoRawStdMinMaxCallsInSrc)
{
    const QString repoRoot = repositoryRoot();
    ASSERT_FALSE(repoRoot.isEmpty());

    const QRegularExpression rawMinMax(QStringLiteral(R"(std::(?:max|min)(?:<[^>\r\n]+>)?\()"));
    QStringList violations;

    QDirIterator it(QDir(repoRoot).filePath(QStringLiteral("src")),
                    {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.cxx"),
                     QStringLiteral("*.cc")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString path = it.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            continue;
        }

        const QString content = QString::fromUtf8(file.readAll());
        const QRegularExpressionMatchIterator matches = rawMinMax.globalMatch(content);
        if (matches.hasNext())
        {
            violations.append(QDir(repoRoot).relativeFilePath(path));
            if (violations.size() >= 20)
            {
                break;
            }
        }
    }

    EXPECT_TRUE(violations.isEmpty()) << violations.join(QStringLiteral("\n")).toStdString();
}

// The product has no stylesheet: no source sets or reads one, none names the stylesheet style, none names a .qss file
// and the tree holds none. A widget's look is painted from the design tokens - by the proxy style, by the dock
// decorations or by its own paintEvent - so that a theme change repaints it; a stylesheet replaces that style for the
// widget and everything under it. A comment may name a stylesheet. The test tree is not read: its cases read a
// widget's stylesheet to prove it empty.
TEST(SourceGuards, NoStyleSheetsInSrc)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty());

    const QRegularExpression styleSheet(
        QStringLiteral(R"(\bsetStyleSheet\b|\bstyleSheet\s*\(|\bQStyleSheetStyle\b|\.qss\b)"));
    const QList<Finding> findings = findingsInSources(root, styleSheet, false);
    EXPECT_TRUE(findings.isEmpty())
        << "a stylesheet in the product's sources; paint the widget from an::ui::tokens() instead, or give it a "
           "property the proxy style reads:\n"
        << described(findings).toStdString();

    QStringList files;
    QDirIterator sheets(QDir(root).filePath(QStringLiteral("src")), {QStringLiteral("*.qss")}, QDir::Files,
                        QDirIterator::Subdirectories);
    while (sheets.hasNext())
    {
        files.append(QDir(root).relativeFilePath(sheets.next()));
    }
    EXPECT_TRUE(files.isEmpty()) << "a stylesheet file under src:\n" << files.join(QLatin1Char('\n')).toStdString();
}

// The product makes no QSettings of its own: every one is what an::ui::applicationSettings() returns, the settings
// file under the application's local data. A QSettings made by default, or from an organization and an application,
// is the platform's native store - the registry on Windows - which no test storage root redirects. The guard finds
// every QSettings object a source makes - declared with arguments or with none, made as a temporary, allocated - and
// every function that returns one by value, and allows the two functions named here. A QSettings a source receives
// by reference, or copies from one of the two functions, is neither. The test tree is not read: a case makes a
// settings file of its own in a temporary folder.
TEST(SourceGuards, NoDefaultQSettingsInSrc)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty());

    // The product's settings file, and the one wrapper of it.
    const QStringList allowed = {
        QStringLiteral("src/AfterNightControls/app/ApplicationStorage.h: [[nodiscard]] inline QSettings "
                       "applicationSettings()"),
        QStringLiteral("src/AfterNightControls/app/ApplicationStorage.h: return "
                       "QSettings(applicationSettingsFilePath(), QSettings::IniFormat);"),
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/dialogs/BlinkComparatorDialog.cpp: "
                       "[[nodiscard]] QSettings windowSettings()"),
    };

    // `QSettings name;`, `QSettings name(...)`, `QSettings name{...}` and a function `QSettings name()`; a temporary
    // `QSettings(...)` or `QSettings{...}`; `new QSettings`; and `make_unique<QSettings>(...)` and its kin.
    const QRegularExpression made(QStringLiteral(
        R"(\bQSettings\s+[A-Za-z_]\w*\s*[;({]|\bQSettings\s*[({]|\bnew\s+QSettings\b|<\s*QSettings\s*>\s*\()"));
    QList<Finding> unexpected;
    QStringList met;
    for (const Finding& finding : findingsInSources(root, made, false))
    {
        const QString entry = finding.file + QStringLiteral(": ") + finding.text;
        if (allowed.contains(entry))
        {
            met.append(entry);
        }
        else
        {
            unexpected.append(finding);
        }
    }
    EXPECT_TRUE(unexpected.isEmpty())
        << "a QSettings the product makes itself; read and write the interface's settings through "
           "an::ui::applicationSettings() (app/ApplicationStorage.h) instead:\n"
        << described(unexpected).toStdString();
    for (const QString& entry : allowed)
    {
        EXPECT_TRUE(met.contains(entry)) << "the guard allows a line the sources no longer hold: "
                                         << entry.toStdString();
    }
}
