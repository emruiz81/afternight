// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shell every top-level process window is shown in: a card holding the window's content, a header naming the
/// process, a button bar whose buttons carry their roles, a banner for messages and progress, colours that follow the
/// theme, a root widget laid out by geometry stretched with the window, sizes fitted by the window's multipliers, and
/// windows that register while open and store their settings when they close. A process the window started is stopped
/// by the shared helper.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/ProcessCommonUtils.h"
#include "theme/AnDesignTokens.h"

#include <QByteArray>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace
{

using namespace an::tests::processes;
using framework::ScopedAppContext;
using framework::WindowSettingsContext;

/// Sets an environment variable for its lifetime and puts back what was there.
class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, const QByteArray& value) : m_name(name)
    {
        m_hadOriginal = qEnvironmentVariableIsSet(m_name.constData());
        if (m_hadOriginal)
        {
            m_original = qgetenv(m_name.constData());
        }
        qputenv(m_name.constData(), value);
    }
    ~ScopedEnvVar()
    {
        if (m_hadOriginal)
        {
            qputenv(m_name.constData(), m_original);
        }
        else
        {
            qunsetenv(m_name.constData());
        }
    }
    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    QByteArray m_name;
    QByteArray m_original;
    bool m_hadOriginal = false;
};

/// Puts back the theme preference a case changed.
class ScopedThemePreference
{
public:
    ScopedThemePreference() : m_original(an::ui::themePreference()) {}
    ~ScopedThemePreference()
    {
        an::ui::setThemePreference(m_original);
    }
    ScopedThemePreference(const ScopedThemePreference&) = delete;
    ScopedThemePreference& operator=(const ScopedThemePreference&) = delete;

private:
    an::ui::ThemePreference m_original;
};

/// A top-level process window of a label and an Apply, Close and Reset button box.
class DummyProcessShell final : public TopLevelProcessBaseControl
{
public:
    explicit DummyProcessShell(const std::string& processName = "Dummy Process Shell")
        : TopLevelProcessBaseControl(processName)
    {
        auto* layout = new QVBoxLayout(this);
        m_label = new QLabel(QStringLiteral("Dummy"), this);
        layout->addWidget(m_label);
        m_buttonBox =
            new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, this);
        layout->addWidget(m_buttonBox);
    }

    [[nodiscard]] QDialogButtonBox* buttonBox() const
    {
        return m_buttonBox;
    }

    [[nodiscard]] QLabel* label() const
    {
        return m_label;
    }

    void setWindowSizeFitMultipliersForTest(double widthMultiplier, double heightMultiplier)
    {
        setWindowSizeFitMultipliers(widthMultiplier, heightMultiplier);
    }

private:
    QLabel* m_label = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

/// A top-level process window laid out by geometry: a root widget at a fixed geometry holding the content
/// and the button box, with no layout on the window itself.
class NestedButtonBoxShell final : public TopLevelProcessBaseControl
{
public:
    NestedButtonBoxShell() : TopLevelProcessBaseControl("Nested Button Box Shell")
    {
        m_fixedRoot = new QWidget(this);
        m_fixedRoot->setObjectName(QStringLiteral("fixedRootWidget"));
        m_fixedRoot->setGeometry(10, 10, 360, 220);
        auto* rootLayout = new QVBoxLayout(m_fixedRoot);
        rootLayout->setContentsMargins(10, 10, 10, 10);
        rootLayout->setSpacing(8);
        auto* label = new QLabel(QStringLiteral("Fixed content"), m_fixedRoot);
        rootLayout->addWidget(label, 1);
        m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Reset, m_fixedRoot);
        rootLayout->addWidget(m_buttonBox);
    }

    [[nodiscard]] QWidget* fixedRootWidget() const
    {
        return m_fixedRoot;
    }

    [[nodiscard]] QDialogButtonBox* buttonBox() const
    {
        return m_buttonBox;
    }

private:
    QWidget* m_fixedRoot = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

/// A window deleted with the case, without the delete-on-close the shell sets.
template <typename WindowT> struct KeptWindow
{
    template <typename... Args> explicit KeptWindow(Args&&... args) : window(new WindowT(std::forward<Args>(args)...))
    {
        window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    ~KeptWindow()
    {
        delete window;
        QCoreApplication::processEvents();
    }
    KeptWindow(const KeptWindow&) = delete;
    KeptWindow& operator=(const KeptWindow&) = delete;

    WindowT* operator->() const
    {
        return window;
    }

    WindowT* window;
};

/// Turns the event loop, deferred deletions included, until a window is gone or the patience runs out.
[[nodiscard]] bool waitUntilDeleted(const QPointer<QWidget>& window)
{
    for (int turn = 0; turn < 200 && !window.isNull(); ++turn)
    {
        QTest::qWait(10);
    }
    return window.isNull();
}

} // namespace

// The shared helper stops a program a window started: asked to terminate, killed when it does not, and gone when the
// helper returns.
TEST(ProcessShellModernization, StopProcess_TerminatesRunningChild)
{
    QProcess process;
#if defined(Q_OS_WIN)
    const QString program = QStandardPaths::findExecutable(QStringLiteral("ping"));
    const QStringList args = {QStringLiteral("-n"), QStringLiteral("30"), QStringLiteral("127.0.0.1")};
#else
    const QString program = QStandardPaths::findExecutable(QStringLiteral("sleep"));
    const QStringList args = {QStringLiteral("30")};
#endif
    ASSERT_FALSE(program.isEmpty());

    process.start(program, args);
    ASSERT_TRUE(process.waitForStarted(3000));
    ASSERT_NE(process.state(), QProcess::NotRunning);

    EXPECT_TRUE(process_utils::stopProcess(process, 200, 3000));
    EXPECT_EQ(process.state(), QProcess::NotRunning);
    EXPECT_TRUE(process_utils::stopProcess(process)) << "a program that is not running is stopped already";
}

// A shown window puts its content in the shell's card, inside the shell's content host.
TEST(ProcessShellModernization, ProcessShell_HasCardContainer)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    KeptWindow<DummyProcessShell> process;
    process->resize(420, 220);
    process->show();
    QTest::qWait(20);

    auto* card = process->findChild<QWidget*>(QStringLiteral("anProcessShellCard"));
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->isVisible());
    auto* contentHost = process->findChild<QWidget*>(QStringLiteral("anProcessShellContent"));
    ASSERT_NE(contentHost, nullptr);
    EXPECT_EQ(process->label()->parentWidget(), contentHost);
}

// A shown window's header names the process.
TEST(ProcessShellModernization, ProcessShell_HasStyledHeader)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    KeptWindow<DummyProcessShell> process("Calibrate Frames");
    process->show();
    QTest::qWait(20);

    ASSERT_NE(process->findChild<QWidget*>(QStringLiteral("anProcessShellHeader")), nullptr);
    auto* title = process->findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Calibrate Frames"));
}

// A shown window's buttons carry their roles: Apply the primary one, Close the secondary one, Reset a neutral one.
TEST(ProcessShellModernization, ProcessShell_HasStyledButtonBar)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    KeptWindow<DummyProcessShell> process;
    process->show();
    QTest::qWait(20);

    QDialogButtonBox* buttonBox = process->buttonBox();
    auto* applyButton = buttonBox->button(QDialogButtonBox::Apply);
    auto* closeButton = buttonBox->button(QDialogButtonBox::Close);
    auto* resetButton = buttonBox->button(QDialogButtonBox::Reset);
    ASSERT_TRUE(applyButton != nullptr && closeButton != nullptr && resetButton != nullptr);
    EXPECT_EQ(applyButton->property("anProcessButtonRole").toString(), QStringLiteral("primary"));
    EXPECT_EQ(closeButton->property("anProcessButtonRole").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(resetButton->property("anProcessButtonRole").toString(), QStringLiteral("neutral"));
}

// The shell's colours are the theme's: the header's title is drawn in the current theme's primary text colour, and
// follows a switch of theme when the window refreshes it.
TEST(ProcessShellModernization, ProcessShell_ThemeAware)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    const ScopedThemePreference restoreTheme;
    ASSERT_NE(an::ui::resolvedDarkTokens().colors.textPrimary, an::ui::resolvedLightTokens().colors.textPrimary);

    an::ui::setThemePreference(an::ui::ThemePreference::Dark);
    KeptWindow<DummyProcessShell> process;
    process->resize(420, 220);
    process->show();
    QTest::qWait(20);
    process->refreshTheme();
    EXPECT_TRUE(process->property("anProcessShell").toBool());
    auto* title = process->findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->palette().color(QPalette::WindowText), an::ui::tokens().colors.textPrimary);
    const QColor dark = title->palette().color(QPalette::WindowText);

    an::ui::setThemePreference(an::ui::ThemePreference::Light);
    process->refreshTheme();
    EXPECT_EQ(title->palette().color(QPalette::WindowText), an::ui::tokens().colors.textPrimary);
    EXPECT_NE(title->palette().color(QPalette::WindowText), dark);
}

// A message is shown in the banner as its title and text, and clearing it hides the banner.
TEST(ProcessShellModernization, ProcessShell_ShowsBannerMessages)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    KeptWindow<DummyProcessShell> process;
    process->show();
    QTest::qWait(20);

    process->showProcessMessage(QStringLiteral("Warning"), QStringLiteral("Calibration files are missing"),
                                TopLevelProcessBaseControl::ProcessMessageTone::Warning);
    auto* banner = process->findChild<QWidget*>(QStringLiteral("anProcessShellBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_TRUE(banner->isVisible());
    auto* bannerMessage = process->findChild<QLabel*>(QStringLiteral("anProcessShellBannerMessage"));
    ASSERT_NE(bannerMessage, nullptr);
    EXPECT_EQ(bannerMessage->text(), QStringLiteral("Warning: Calibration files are missing"));

    process->clearProcessMessage();
    EXPECT_FALSE(banner->isVisible());
}

// A message with a percentage shows it as a bar in tenths of a percent with no text; a message without one, or a
// cleared banner, hides the bar.
TEST(ProcessShellModernization, ProcessShell_ShowsBannerProgress)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    KeptWindow<DummyProcessShell> process;
    process->show();
    QTest::qWait(20);

    process->showProcessMessage(QStringLiteral("Working"), QStringLiteral("Sharpening"),
                                TopLevelProcessBaseControl::ProcessMessageTone::Info, 42.5);
    auto* progressBar = process->findChild<QProgressBar*>(QStringLiteral("anProcessShellBannerProgress"));
    ASSERT_NE(progressBar, nullptr);
    EXPECT_TRUE(progressBar->isVisible());
    EXPECT_FALSE(progressBar->isTextVisible());
    EXPECT_EQ(progressBar->minimum(), 0);
    EXPECT_EQ(progressBar->maximum(), 1000);
    EXPECT_EQ(progressBar->value(), 425);

    process->showProcessMessage(QStringLiteral("Info"), QStringLiteral("Waiting"),
                                TopLevelProcessBaseControl::ProcessMessageTone::Info);
    EXPECT_FALSE(progressBar->isVisible());

    process->clearProcessMessage();
    EXPECT_FALSE(progressBar->isVisible());
}

// A window laid out by geometry has its button box lifted out below the card and its root widget put in the card,
// where it grows with the window.
TEST(ProcessShellModernization, ProcessShell_ExtractsNestedButtonBoxAndStretchesFixedRootWidget)
{
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");
    KeptWindow<NestedButtonBoxShell> process;
    process->resize(460, 300);
    process->show();
    QTest::qWait(25);

    auto* card = process->findChild<QWidget*>(QStringLiteral("anProcessShellCard"));
    auto* contentHost = process->findChild<QWidget*>(QStringLiteral("anProcessShellContent"));
    ASSERT_TRUE(card != nullptr && contentHost != nullptr);
    EXPECT_EQ(process->fixedRootWidget()->parentWidget(), contentHost);
    EXPECT_EQ(process->buttonBox()->parentWidget(), process.window);
    EXPECT_FALSE(card->isAncestorOf(process->buttonBox()));
    const int cardIndex = process->layout() != nullptr ? process->layout()->indexOf(card) : -1;
    const int buttonIndex = process->layout() != nullptr ? process->layout()->indexOf(process->buttonBox()) : -1;
    EXPECT_GE(cardIndex, 0);
    EXPECT_GT(buttonIndex, cardIndex);

    const QSize before = process->fixedRootWidget()->size();
    process->resize(760, 520);
    QTest::qWait(25);
    const QSize after = process->fixedRootWidget()->size();
    EXPECT_GT(after.width(), before.width());
    EXPECT_GT(after.height(), before.height());
}

// A window is registered as open while it exists and closing it deletes it and unregisters it, one after another; each
// close stores the window's settings.
TEST(ProcessShellModernization, ProcessShell_DoesNotBreakExistingProcesses)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("1"));
    const ScopedEnvVar uiTokensDisabled("AN_UI_TOKENS_ENABLED", "0");
    const std::size_t openBefore = framework::suiteContext().openProcesses.size();

    for (int i = 0; i < 5; ++i)
    {
        const std::string name = "Dummy Process " + std::to_string(i + 1);
        auto* process = new DummyProcessShell(name);
        const QPointer<QWidget> window(process);
        process->resize(460, 240);
        process->initializeTopLevelProcess();
        process->show();
        QTest::qWait(10);
        EXPECT_EQ(framework::suiteContext().openProcesses.size(), openBefore + 1u);

        process->close();
        EXPECT_TRUE(waitUntilDeleted(window));
        EXPECT_EQ(framework::suiteContext().openProcesses.size(), openBefore);
        EXPECT_TRUE(settings.stored(QString::fromStdString(name)).has_value());
    }
    EXPECT_EQ(settings.windowWrites, 5);
}

// A fixed window is fitted to its content times its multipliers, and held there.
TEST(ProcessShellModernization, ProcessShell_WindowSizeFitMultiplierAddsExtraSpace)
{
    const ScopedEnvVar uiTokensDisabled("AN_UI_TOKENS_ENABLED", "0");
    KeptWindow<DummyProcessShell> baselineProcess;
    baselineProcess->setFixedSize(300, 200);
    baselineProcess->show();
    QTest::qWait(20);
    const QSize baselineSize = baselineProcess->size();

    KeptWindow<DummyProcessShell> process;
    process->setFixedSize(300, 200);
    process->setWindowSizeFitMultipliersForTest(1.3, 1.2);
    process->show();
    QTest::qWait(20);

    const QSize expectedSize(static_cast<int>(std::ceil(baselineSize.width() * 1.3)),
                             static_cast<int>(std::ceil(baselineSize.height() * 1.2)));
    EXPECT_EQ(process->size(), expectedSize);
    EXPECT_EQ(process->minimumSize(), expectedSize);
    EXPECT_EQ(process->maximumSize(), expectedSize);
}

// A resizable window opens wider by its multiplier but keeps its natural minimum, so it can still be made as narrow as
// without one.
TEST(ProcessShellModernization, ProcessShell_NonFixedMultiplierPreservesNaturalMinimum)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("0"));
    const ScopedEnvVar uiTokensEnabled("AN_UI_TOKENS_ENABLED", "1");

    int baselineMinWidth = 0;
    int baselineWidth = 0;
    {
        KeptWindow<DummyProcessShell> baselineProcess;
        baselineProcess->resize(140, 120);
        baselineProcess->show();
        QTest::qWait(60);
        baselineMinWidth = baselineProcess->minimumSize().width();
        baselineWidth = baselineProcess->width();
    }

    KeptWindow<DummyProcessShell> process;
    process->resize(140, 120);
    process->setWindowSizeFitMultipliersForTest(1.35, 1.0);
    process->show();
    QTest::qWait(60);
    EXPECT_EQ(process->minimumSize().width(), baselineMinWidth);
    EXPECT_GT(process->width(), baselineWidth);

    process->resize(baselineMinWidth, process->height());
    QTest::qWait(20);
    EXPECT_EQ(process->width(), baselineMinWidth);
}
