// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The session console: the log viewer the application docks, fed by the session console bridge. It renders the
/// colours an entry's ANSI sequences ask for unless the person turns them off - a choice it keeps between sessions -
/// never shows the sequences themselves, and with word wrap off it keeps the start of each line in view as entries
/// arrive. Its settings are written under the suite's storage root.

#include "app/ApplicationStorage.h"
#include "app/SessionConsoleBridge.h"
#include "controls/LogViewerControl.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QObject>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <gtest/gtest.h>

#include <memory>

namespace
{

/// Where the console keeps its choice to render ANSI colours, and its word wrap.
constexpr auto kAnsiColoursKey = "log/render_ansi_colors";
constexpr auto kLineWrapKey = "log/line_wrap";

/// An ANSI sequence that colours a word in an exact colour, then resets.
const QString kColouredEntry = QStringLiteral("\x1b[38;2;250;10;10mred\x1b[0m plain");

/// The console's settings as a case leaves them: forgotten before it starts and again when it ends, passing or failing.
class ForgottenConsoleSettings
{
public:
    ForgottenConsoleSettings()
    {
        forget();
    }
    ~ForgottenConsoleSettings()
    {
        forget();
    }
    ForgottenConsoleSettings(const ForgottenConsoleSettings&) = delete;
    ForgottenConsoleSettings& operator=(const ForgottenConsoleSettings&) = delete;

private:
    static void forget()
    {
        QSettings settings = an::ui::applicationSettings();
        settings.remove(QString::fromLatin1(kAnsiColoursKey));
        settings.remove(QString::fromLatin1(kLineWrapKey));
        settings.sync();
    }
};

/// The session console bridge the application installs, made for the case when none is: it goes with the owner.
[[nodiscard]] an::ui::SessionConsoleBridge* consoleBridge(QObject& owner)
{
    return an::ui::SessionConsoleBridge::initialize(&owner);
}

/// A console as the application opens one, owned by the case rather than deleted when it closes.
[[nodiscard]] std::unique_ptr<LogViewerControl> openConsole()
{
    auto console = std::make_unique<LogViewerControl>();
    console->initialize();
    console->setAttribute(Qt::WA_DeleteOnClose, false);
    return console;
}

/// The console's toolbar action of a text, or null.
[[nodiscard]] QAction* actionNamed(const LogViewerControl& console, const QString& text)
{
    for (QAction* action : console.findChildren<QAction*>())
    {
        if (action->text() == text)
        {
            return action;
        }
    }
    return nullptr;
}

/// The colour the console's last line shows a character in.
[[nodiscard]] QColor colourOfCharacter(const QPlainTextEdit& text, int index)
{
    const QTextBlock line = text.document()->lastBlock();
    QTextCursor cursor(line);
    cursor.setPosition(line.position() + index + 1);
    return cursor.charFormat().foreground().color();
}

} // namespace

// An entry's ANSI colours are rendered and its sequences never shown; turning the colours off shows the same text in
// the console's own colour and is remembered, so the next console opens with them off.
TEST(SessionConsoleViewerTest, AnsiColoursAreRenderedUntilTurnedOffAndTheChoiceIsKept)
{
    const ForgottenConsoleSettings forgotten;
    QObject owner;
    an::ui::SessionConsoleBridge* bridge = consoleBridge(owner);
    ASSERT_NE(bridge, nullptr);

    std::unique_ptr<LogViewerControl> console = openConsole();
    QAction* colours = actionNamed(*console, QStringLiteral("ANSI Colors"));
    ASSERT_NE(colours, nullptr);
    EXPECT_TRUE(colours->isCheckable());
    EXPECT_TRUE(colours->isChecked()) << "ANSI colours are rendered until the person turns them off";
    EXPECT_TRUE(an::ui::applicationSettings().value(QString::fromLatin1(kAnsiColoursKey)).toBool());

    auto* text = console->findChild<QPlainTextEdit*>();
    ASSERT_NE(text, nullptr);
    bridge->appendEntry(kColouredEntry, spdlog::level::info);
    EXPECT_EQ(text->document()->lastBlock().text(), QStringLiteral("red plain"));
    EXPECT_EQ(colourOfCharacter(*text, 0), QColor(250, 10, 10));
    EXPECT_NE(colourOfCharacter(*text, 4), QColor(250, 10, 10));

    colours->trigger();
    EXPECT_FALSE(colours->isChecked());
    EXPECT_FALSE(an::ui::applicationSettings().value(QString::fromLatin1(kAnsiColoursKey)).toBool());
    EXPECT_EQ(text->document()->lastBlock().text(), QStringLiteral("red plain"));
    EXPECT_EQ(colourOfCharacter(*text, 0), colourOfCharacter(*text, 4)) << "without colours the console's own shows";

    console.reset();
    std::unique_ptr<LogViewerControl> reopened = openConsole();
    QAction* reopenedColours = actionNamed(*reopened, QStringLiteral("ANSI Colors"));
    ASSERT_NE(reopenedColours, nullptr);
    EXPECT_FALSE(reopenedColours->isChecked());
}

// With word wrap off, an entry wider than the console scrolls the console down to it and leaves the start of the line
// in view rather than its end.
TEST(SessionConsoleViewerTest, WithoutWordWrapANewEntryKeepsTheStartOfTheLineInView)
{
    const ForgottenConsoleSettings forgotten;
    an::ui::applicationSettings().setValue(QString::fromLatin1(kLineWrapKey), static_cast<int>(QPlainTextEdit::NoWrap));
    QObject owner;
    an::ui::SessionConsoleBridge* bridge = consoleBridge(owner);
    ASSERT_NE(bridge, nullptr);

    std::unique_ptr<LogViewerControl> console = openConsole();
    auto* text = console->findChild<QPlainTextEdit*>();
    ASSERT_NE(text, nullptr);
    ASSERT_EQ(text->lineWrapMode(), QPlainTextEdit::NoWrap);
    console->resize(640, 240);
    console->show();
    QCoreApplication::processEvents();

    const QString wide = QStringLiteral("entry ") + QString(600, QLatin1Char('x')) + QStringLiteral(" end");
    bridge->appendEntry(wide, spdlog::level::info);
    QCoreApplication::processEvents();
    QScrollBar* horizontal = text->horizontalScrollBar();
    if (horizontal->maximum() <= horizontal->minimum())
    {
        GTEST_SKIP() << "this platform lays out no text width, so no line is wider than the console";
    }
    horizontal->setValue(horizontal->maximum());

    bridge->appendEntry(wide, spdlog::level::info);
    QCoreApplication::processEvents();
    EXPECT_EQ(text->document()->lastBlock().text(), wide);
    EXPECT_EQ(horizontal->value(), horizontal->minimum());
    console->hide();
}
