// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The rules of the source tree no case of behaviour can hold, each read from the sources themselves: no bare
// std::min or std::max, which the Windows SDK's macros would expand; no stylesheet, since the look is painted from the
// design tokens; no QSettings but the product's own settings file, since any other is the native store, which no
// test storage root redirects; no shell reached from below it or from another shell; and, in the shells and the
// reusable components, no colour or font size written as a literal and no design token read in a constructor, where it
// would be kept past a change of theme. A guard fails naming the file and the line, and says what to write instead.

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QString>
#include <QSet>
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
// character literals stay as they are - a name inside one is not a comment - unless `blankLiterals` asks for what is
// inside them blanked too, their quotes kept, so that a brace in a string is not counted as code.
QString withoutComments(const QString& source, const bool blankLiterals = false)
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
                if (blankLiterals)
                {
                    text[index] = QLatin1Char(' ');
                    if (index + 1 < size && text[index + 1] != QLatin1Char('\n'))
                    {
                        text[index + 1] = QLatin1Char(' ');
                    }
                }
                ++index;
            }
            else if (character == (state == In::String ? QLatin1Char('"') : QLatin1Char('\'')) ||
                     character == QLatin1Char('\n'))
            {
                state = In::Code;
            }
            else if (blankLiterals && character != QLatin1Char('\r'))
            {
                text[index] = QLatin1Char(' ');
            }
            break;
        case In::RawString:
            if (text.mid(index, rawEnd.size()) == rawEnd)
            {
                index += rawEnd.size() - 1;
                state = In::Code;
            }
            else if (blankLiterals && character != QLatin1Char('\n') && character != QLatin1Char('\r'))
            {
                text[index] = QLatin1Char(' ');
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

// The trees whose look is held to the design tokens: the shell core, which every shell is built on, and the reusable
// components. The theme's own tree is where the look is decided, and is not among them; the classic shell is the
// interface of AfterNight 1.1, kept as it was until it leaves or stays whole.
const QStringList& lookTrees()
{
    static const QStringList trees{QStringLiteral("src/AfterNightShellCore/"),
                                   QStringLiteral("src/AfterNightControls/components/")};
    return trees;
}

// A C or C++ source of a guarded tree: its path from the repository's root, its text, and its text without comments.
struct GuardedSource
{
    QString file;
    QString source;
    QString code;
};

// Every C and C++ source under the trees, its line breaks made `\n`.
QList<GuardedSource> sourcesUnder(const QString& root, const QStringList& trees)
{
    QList<GuardedSource> sources;
    const QDir rootFolder(root);
    for (const QString& tree : trees)
    {
        QDirIterator files(
            rootFolder.filePath(tree),
            {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.inl")},
            QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            const QString path = files.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                continue;
            }
            QString source = QString::fromUtf8(file.readAll());
            source.remove(QLatin1Char('\r'));
            sources.append(GuardedSource{rootFolder.relativeFilePath(path), source, withoutComments(source)});
        }
    }
    return sources;
}

// The line of a position in a text, counted from one.
int lineAt(const QString& text, const qsizetype position)
{
    return static_cast<int>(text.left(position).count(QLatin1Char('\n'))) + 1;
}

// The position of the bracket that closes the one at `open` - a parenthesis, a brace or an angle bracket - or -1.
qsizetype closingBracket(const QString& code, const qsizetype open)
{
    const QChar opening = code[open];
    const QChar closing = opening == QLatin1Char('(')   ? QLatin1Char(')')
                          : opening == QLatin1Char('{') ? QLatin1Char('}')
                          : opening == QLatin1Char('[') ? QLatin1Char(']')
                                                        : QLatin1Char('>');
    int depth = 0;
    for (qsizetype index = open; index < code.size(); ++index)
    {
        if (code[index] == opening)
        {
            ++depth;
        }
        else if (code[index] == closing && --depth == 0)
        {
            return index;
        }
    }
    return -1;
}

// The first position at or after `from` that is not a space.
qsizetype skipSpaces(const QString& code, qsizetype from)
{
    while (from < code.size() && code[from].isSpace())
    {
        ++from;
    }
    return from;
}

// Where a constructor whose parameters open at `open` is defined: from the parameters' end to the end of its body,
// the initializers included. Empty when the text there declares the constructor but does not define it, or is no
// constructor at all.
std::optional<std::pair<qsizetype, qsizetype>> constructorDefinition(const QString& code, const qsizetype open)
{
    const qsizetype parametersEnd = closingBracket(code, open);
    if (parametersEnd < 0)
    {
        return std::nullopt;
    }
    qsizetype at = skipSpaces(code, parametersEnd + 1);
    if (code.mid(at, 8) == QLatin1String("noexcept"))
    {
        at = skipSpaces(code, at + 8);
        if (at < code.size() && code[at] == QLatin1Char('('))
        {
            at = skipSpaces(code, closingBracket(code, at) + 1);
        }
    }
    if (at < code.size() && code[at] == QLatin1Char(':') && code.mid(at, 2) != QLatin1String("::"))
    {
        // The initializers: each a name, perhaps qualified or with template arguments, and its value in parentheses or
        // braces, separated by commas, up to the brace that opens the body.
        at = skipSpaces(code, at + 1);
        while (at < code.size())
        {
            while (at < code.size() &&
                   (code[at].isLetterOrNumber() || code[at] == QLatin1Char('_') || code[at] == QLatin1Char(':')))
            {
                ++at;
            }
            at = skipSpaces(code, at);
            if (at < code.size() && code[at] == QLatin1Char('<'))
            {
                at = skipSpaces(code, closingBracket(code, at) + 1);
            }
            if (at >= code.size() || (code[at] != QLatin1Char('(') && code[at] != QLatin1Char('{')))
            {
                return std::nullopt;
            }
            const qsizetype valueEnd = closingBracket(code, at);
            if (valueEnd < 0)
            {
                return std::nullopt;
            }
            at = skipSpaces(code, valueEnd + 1);
            if (at < code.size() && code[at] == QLatin1Char(','))
            {
                at = skipSpaces(code, at + 1);
                continue;
            }
            break;
        }
    }
    if (at >= code.size() || code[at] != QLatin1Char('{'))
    {
        return std::nullopt; // a declaration, `= default`, `= delete`, or no constructor
    }
    const qsizetype bodyEnd = closingBracket(code, at);
    if (bodyEnd < 0)
    {
        return std::nullopt;
    }
    return std::make_pair(parametersEnd, bodyEnd);
}

// Whether the text before a name at `position` lets it begin a declaration: the start of the file, the end of a
// statement or a block, an access specifier's colon, or `explicit`, `inline` or `constexpr`.
bool beginsADeclaration(const QString& code, const qsizetype position)
{
    qsizetype before = position - 1;
    while (before >= 0 && code[before].isSpace())
    {
        --before;
    }
    if (before < 0)
    {
        return true;
    }
    const QChar last = code[before];
    if (last == QLatin1Char(';') || last == QLatin1Char('{') || last == QLatin1Char('}'))
    {
        return true;
    }
    if (last == QLatin1Char(':'))
    {
        return before == 0 || code[before - 1] != QLatin1Char(':');
    }
    qsizetype wordStart = before;
    while (wordStart > 0 && (code[wordStart - 1].isLetterOrNumber() || code[wordStart - 1] == QLatin1Char('_')))
    {
        --wordStart;
    }
    const QString word = code.mid(wordStart, before - wordStart + 1);
    return word == QLatin1String("explicit") || word == QLatin1String("inline") || word == QLatin1String("constexpr");
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

// A shell is reached only by the application that composes it and by the suites that drive it. No unit of Controls,
// the processes library or the shell core includes a header of a shell - the shell core is what every shell is built
// on, and the libraries below it know no window - no suite below them includes one, and no shell includes another's:
// two shells are two ways to show the same application, and one that reached into the other would carry it into every
// run of its own. An include is a shell's when it names a file of the shell's tree and nothing beside the including
// file; the shell's own tree, the application and the shell's suites are left out.
TEST(SourceGuards, ShellsDoNotReachEachOther)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty());
    const QDir rootFolder(root);

    struct Shell
    {
        QString tree;
        QStringList reachers;
    };
    // Each shell's tree, and the trees that may include it: the application and the suites that drive it.
    const QList<Shell> shells = {
        {QStringLiteral("src/AfterNightShellClassic/"),
         {QStringLiteral("src/AfterNightApp/"), QStringLiteral("src/AfterNightTests/Test_ShellClassic/"),
          QStringLiteral("src/AfterNightTests/Test_ShellParity/"),
          QStringLiteral("src/AfterNightTests/Test_AfterNightApp/")}},
    };

    const QRegularExpression include(QStringLiteral(R"(^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"])"),
                                     QRegularExpression::MultilineOption);
    QList<Finding> findings;
    int read = 0;
    for (const Shell& shell : shells)
    {
        // The names a source includes the shell's headers by: their paths inside the tree.
        QSet<QString> headers;
        QDirIterator files(rootFolder.filePath(shell.tree), {QStringLiteral("*.h"), QStringLiteral("*.hpp")},
                           QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            headers.insert(QDir(rootFolder.filePath(shell.tree)).relativeFilePath(files.next()));
        }
        // Every library precompiles a header of that name of its own, which its sources reach in its own root.
        headers.remove(QStringLiteral("stdafx.hpp"));
        ASSERT_FALSE(headers.isEmpty()) << shell.tree.toStdString() << " holds no header";

        QDirIterator sources(
            rootFolder.filePath(QStringLiteral("src")),
            {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.hpp"), QStringLiteral("*.inl")},
            QDir::Files, QDirIterator::Subdirectories);
        while (sources.hasNext())
        {
            const QString path = sources.next();
            const QString relative = rootFolder.relativeFilePath(path);
            const bool reaches = relative.startsWith(shell.tree) ||
                                 std::any_of(shell.reachers.cbegin(), shell.reachers.cend(),
                                             [&relative](const QString& tree) { return relative.startsWith(tree); });
            if (reaches)
            {
                continue;
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                continue;
            }
            ++read;
            QString source = QString::fromUtf8(file.readAll());
            source.remove(QLatin1Char('\r'));
            const QString code = withoutComments(source);
            QRegularExpressionMatchIterator matches = include.globalMatch(code);
            while (matches.hasNext())
            {
                const QRegularExpressionMatch match = matches.next();
                QString name = match.captured(1);
                const QString prefix = shell.tree.mid(QStringLiteral("src/").size());
                if (name.startsWith(prefix))
                {
                    name = name.mid(prefix.size());
                }
                else if (QFileInfo::exists(QFileInfo(path).dir().filePath(name)))
                {
                    continue; // a file beside the includer, of the same name
                }
                if (!headers.contains(name))
                {
                    continue;
                }
                const int line = static_cast<int>(code.left(match.capturedStart()).count(QLatin1Char('\n'))) + 1;
                findings.append(Finding{relative, line, source.split(QLatin1Char('\n')).value(line - 1).trimmed()});
            }
        }
    }

    EXPECT_GT(read, 1000) << "the guard read too few sources to have read the tree";
    EXPECT_TRUE(findings.isEmpty())
        << "a shell's header included from below it or from another shell; reach the application through the shell "
           "core's units and contract (core/IShell.h) instead:\n"
        << described(findings).toStdString();
}

// In the shell core and the reusable components no colour and no font size is written as a literal: a colour comes
// from the design tokens in force and a size from their metrics, so that a theme, a preset, Night or a density reaches
// every part. The guard finds a QColor made from numbers, a name or a string, in any of the forms a source makes one -
// a temporary, a variable, QColor::from..., qRgb - a Qt colour constant but Qt::transparent, a "#RRGGBB" string, and a
// point or pixel size set from a number. A comment may name one. The theme's tree, where the look is decided, and the
// test tree are not read.
TEST(SourceGuards, NoLookLiteralsInShellsAndComponents)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty());

    const QString literal = QStringLiteral(R"re((?:[-+]?[0-9.]|"|Qt::|QStringLiteral|QLatin1String|u"))re");
    const QRegularExpression look(
        QStringLiteral(R"(\bQColor\s*[({]\s*)") + literal + QStringLiteral(R"(|\bQColor\s+[A-Za-z_]\w*\s*[({]\s*)") +
        literal + QStringLiteral(R"(|\bQColor::from\w+\s*\(\s*)") + literal +
        QStringLiteral(R"(|\bqRgba?\s*\(\s*[0-9])") +
        QStringLiteral(R"(|\bQt::(?:color0|color1|black|white|darkGray|gray|lightGray|red|green|blue|cyan|magenta|)"
                       R"(yellow|darkRed|darkGreen|darkBlue|darkCyan|darkMagenta|darkYellow)\b)") +
        QStringLiteral(R"re(|"#(?:[0-9A-Fa-f]{8}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{3,4})")re") +
        QStringLiteral(R"(|\bset(?:PointSize|PointSizeF|PixelSize)\s*\(\s*[-+]?[0-9.])"));
    ASSERT_TRUE(look.isValid()) << look.errorString().toStdString();

    QList<Finding> findings;
    const QList<GuardedSource> sources = sourcesUnder(root, lookTrees());
    for (const GuardedSource& source : sources)
    {
        QRegularExpressionMatchIterator matches = look.globalMatch(source.code);
        while (matches.hasNext())
        {
            const int line = lineAt(source.code, matches.next().capturedStart());
            findings.append(
                Finding{source.file, line, source.source.split(QLatin1Char('\n')).value(line - 1).trimmed()});
        }
    }

    EXPECT_GT(sources.size(), 20) << "the guard read too few sources to have read its trees";
    EXPECT_TRUE(findings.isEmpty())
        << "a colour or a font size written as a literal in a shell or a component; read it from an::ui::tokens() - "
           "its colours, its metrics and its type scale - where the part paints or sizes itself:\n"
        << described(findings).toStdString();
}

// In the shell core and the reusable components no constructor reads the design tokens: a colour, a size or a font
// taken there would be kept past a change of theme, preset, Night or density. A part reads them where it paints, in
// sizeHint(), or where it is polished and told of a change of style. The guard finds a call of tokens() - or of the
// dark or light set - in a constructor's initializers or body, a lambda made there included, in every constructor a
// source of the trees defines, in a class or out of one.
TEST(SourceGuards, TokensAreNotCachedInConstructors)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty());

    const QRegularExpression className(
        QStringLiteral(R"(\b(?:class|struct)\s+(?:[A-Z][A-Z0-9_]*_API\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?[:{])"));
    const QRegularExpression outOfClass(QStringLiteral(R"(\b([A-Za-z_]\w*)\s*::\s*\1\s*\()"));
    const QRegularExpression tokensCall(
        QStringLiteral(R"(\b(?:tokens|darkTokens|lightTokens|resolvedDarkTokens|resolvedLightTokens)\s*\(\s*\))"));

    QList<Finding> findings;
    int constructors = 0;
    const QList<GuardedSource> sources = sourcesUnder(root, lookTrees());
    for (const GuardedSource& source : sources)
    {
        // Braces and parentheses inside a string are not code.
        const QString code = withoutComments(source.source, true);
        // Where each constructor's parameters open: Name::Name( anywhere, and Name( where a declaration may begin, for
        // every class the file declares.
        QList<qsizetype> openings;
        QRegularExpressionMatchIterator defined = outOfClass.globalMatch(code);
        while (defined.hasNext())
        {
            const QRegularExpressionMatch match = defined.next();
            openings.append(match.capturedEnd() - 1);
        }
        QRegularExpressionMatchIterator classes = className.globalMatch(code);
        while (classes.hasNext())
        {
            const QString name = classes.next().captured(1);
            const QRegularExpression inClass(QStringLiteral(R"((?<![\w:~.>])%1\s*\()").arg(name));
            QRegularExpressionMatchIterator named = inClass.globalMatch(code);
            while (named.hasNext())
            {
                const QRegularExpressionMatch match = named.next();
                if (beginsADeclaration(code, match.capturedStart()))
                {
                    openings.append(match.capturedEnd() - 1);
                }
            }
        }
        std::sort(openings.begin(), openings.end());
        openings.erase(std::unique(openings.begin(), openings.end()), openings.end());
        for (const qsizetype open : openings)
        {
            const std::optional<std::pair<qsizetype, qsizetype>> definition = constructorDefinition(code, open);
            if (!definition)
            {
                continue;
            }
            ++constructors;
            const QString inside = code.mid(definition->first, definition->second - definition->first);
            QRegularExpressionMatchIterator calls = tokensCall.globalMatch(inside);
            while (calls.hasNext())
            {
                const int line = lineAt(code, definition->first + calls.next().capturedStart());
                findings.append(
                    Finding{source.file, line, source.source.split(QLatin1Char('\n')).value(line - 1).trimmed()});
            }
        }
    }

    EXPECT_GT(constructors, 20) << "the guard found too few constructors to have read its trees";
    EXPECT_TRUE(findings.isEmpty())
        << "the design tokens read in a constructor of a shell or a component; read them in paintEvent(), in "
           "sizeHint(), or where the widget is polished or told of a change of style (QEvent::Polish, "
           "QEvent::StyleChange):\n"
        << described(findings).toStdString();
}
