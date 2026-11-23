// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Settings dialog as a shell lists it: its pages in their order, Projects after General, each page laid out with
/// nothing clipped at the dialog's least size in both densities, the search finding a page by any row it shows, Reset
/// Page and Reset All to Defaults bringing every key the dialog shows back to its default, and the dialog held within
/// the screen's available area as it is first shown.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "app/WindowPlacement.h"
#include "components/AnWindowHeading.h"
#include "controls/SettingsDialog.h"
#include "controls/settings/ProjectSettings.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsPage.h"
#include "controls/settings/SettingsPages.h"
#include "fixtures/ClippedControls.h"
#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMargins>
#include <QMessageBox>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::SettingsStore;
using an::ui::SettingsPagesOptions;

/// The dialogs' context, installed for a case.
class Installed final
{
public:
    Installed() : m_context(an::tests::dialogs::openDialogsContext())
    {
        EXPECT_NE(m_context, nullptr);
        if (m_context != nullptr)
        {
            m_installed = std::make_unique<InstalledContext>(*m_context);
        }
    }

    ~Installed()
    {
        m_installed.reset();
        m_context.reset();
    }

    Installed(const Installed&) = delete;
    Installed& operator=(const Installed&) = delete;

private:
    std::unique_ptr<DialogsTestContext> m_context;
    std::unique_ptr<InstalledContext> m_installed;
};

/// The choices in force, put back when a case ends.
class ChoicesRestored final
{
public:
    ChoicesRestored()
        : m_contrast(an::ui::themeContrastProfile()), m_accent(an::ui::themeAccentFamily()),
          m_tint(an::ui::themeSurfaceTintFamily()), m_strength(an::ui::themeTintStrength()),
          m_density(an::ui::themeDensity()), m_night(an::ui::nightModeSet())
    {
    }

    ~ChoicesRestored()
    {
        an::ui::setThemeContrastProfile(m_contrast);
        an::ui::setThemeAccentFamily(m_accent);
        an::ui::setThemeSurfaceTintFamily(m_tint);
        an::ui::setThemeTintStrength(m_strength);
        an::ui::setThemeDensity(m_density);
        an::ui::setNightMode(m_night);
    }

    ChoicesRestored(const ChoicesRestored&) = delete;
    ChoicesRestored& operator=(const ChoicesRestored&) = delete;

private:
    an::ui::ThemeContrastProfile m_contrast;
    an::ui::ThemeAccentFamily m_accent;
    an::ui::ThemeSurfaceTintFamily m_tint;
    an::ui::ThemeTintStrength m_strength;
    an::ui::ThemeDensity m_density;
    bool m_night;
};

/// Lets the dialog settle: posted work delivered, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// What the Observatory shell hands the dialog: two shells to choose among - itself and a stand-in, so that the
/// Interface page shows its Shell row - and a menu bar shown on Alt.
SettingsPagesOptions observatoryOptions()
{
    SettingsPagesOptions options;
    options.shells = {
        an::ui::ShellOption{.id = QStringLiteral("observatory"), .displayName = QStringLiteral("Observatory")},
        an::ui::ShellOption{.id = QStringLiteral("nebula"), .displayName = QStringLiteral("Nebula")}};
    options.keepMenuBarShown = [](bool) {};
    return options;
}

/// The pages a dialog lists, by their titles, in the order its navigation lists them.
QStringList listedPages(const SettingsDialog& dialog)
{
    QStringList titles;
    for (int index = 0; index < dialog.pageCount(); ++index)
    {
        titles.append(dialog.page(index) != nullptr ? dialog.page(index)->pageTitle() : QString());
    }
    return titles;
}

/// The navigation's entries a search leaves shown.
QStringList shownEntries(const SettingsDialog& dialog)
{
    QStringList shown;
    const auto* const navigation = dialog.findChild<QListWidget*>(QStringLiteral("settingsNavigationList"));
    EXPECT_NE(navigation, nullptr);
    if (navigation == nullptr)
    {
        return shown;
    }
    for (int row = 0; row < navigation->count(); ++row)
    {
        if (!navigation->item(row)->isHidden())
        {
            shown.append(navigation->item(row)->text());
        }
    }
    return shown;
}

/// Answers the next message box a modal loop shows with a button.
void pressAtTheNextMessageBox(const QMessageBox::StandardButton answer)
{
    auto* const poll = new QTimer(qApp);
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, poll, [poll, answer]() {
        if (auto* const box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            if (QAbstractButton* const button = box->button(answer))
            {
                button->click();
            }
            poll->deleteLater();
        }
    });
    poll->start();
}

/// The store holding, for every key of the new interface the dialog shows, a value that is not its default.
void storeOtherThanTheDefaults(const SettingsStore& store, const QString& elsewhere)
{
    store.store(SettingsKeys::INTERFACE_SHELL, QStringLiteral("nebula"));
    store.store(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY, QStringLiteral("rose"));
    store.store(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY, QStringLiteral("amethyst"));
    store.store(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH, QStringLiteral("strong"));
    store.store(SettingsKeys::INTERFACE_DENSITY, QStringLiteral("comfortable"));
    store.store(SettingsKeys::INTERFACE_THEME_PRESET, QStringLiteral("custom"));
    store.store(SettingsKeys::INTERFACE_START_IN_NIGHT, QStringLiteral("1"));
    store.store(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR, QStringLiteral("1"));
    store.store(SettingsKeys::PROJECTS_DEFAULT_LOCATION, elsewhere);
    store.store(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS, QStringLiteral("0"));
    store.store(SettingsKeys::HISTORY_COMPRESSION, QStringLiteral("zstd"));
    store.store(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB, QStringLiteral("25"));
    store.store(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP, QStringLiteral("1"));
}

/// Every key of the new interface the dialog shows holds its default, each failure naming its key.
void expectTheDefaults(const SettingsStore& store, const char* how)
{
    const std::vector<std::pair<const char*, QString>> defaults = {
        {SettingsKeys::INTERFACE_SHELL, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)},
        {SettingsKeys::INTERFACE_THEME_TINT_STRENGTH,
         QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH)},
        {SettingsKeys::INTERFACE_DENSITY, QString::fromLatin1(SettingsDefaults::INTERFACE_DENSITY)},
        {SettingsKeys::INTERFACE_THEME_PRESET, QString::fromLatin1(SettingsDefaults::INTERFACE_THEME_PRESET)},
        {SettingsKeys::INTERFACE_START_IN_NIGHT, QStringLiteral("0")},
        {SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR, QStringLiteral("0")},
        {SettingsKeys::PROJECTS_DEFAULT_LOCATION, an::ui::defaultProjectsLocation()},
        {SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS, QStringLiteral("1")},
        {SettingsKeys::HISTORY_COMPRESSION, QString::fromLatin1(SettingsDefaults::HISTORY_COMPRESSION)},
        {SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB,
         QString::number(SettingsDefaults::PROJECTS_COMPACT_ASK_THRESHOLD_GB)},
        {SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP, QStringLiteral("0")},
    };
    for (const auto& [key, value] : defaults)
    {
        EXPECT_EQ(store.stored(key), std::optional<QString>(value)) << how << ": " << key;
    }
}

/// A page of one row wider than the page's least width: a check box whose text runs long, beside the form's labels.
class WideRowPage final : public SettingsPage
{
public:
    WideRowPage()
    {
        auto* const rows = new QFormLayout(this);
        rows->setContentsMargins(0, 0, 0, 0);
        m_box = new QCheckBox(QStringLiteral("A choice whose text runs on past the width the page states for itself, "
                                             "which every page is given at the least"),
                              this);
        rows->addRow(QStringLiteral("Default compression:"), m_box);
    }

    [[nodiscard]] QString pageTitle() const override
    {
        return QStringLiteral("Wide");
    }
    [[nodiscard]] QIcon pageIcon() const override
    {
        return {};
    }
    void loadSettings() override {}
    void saveSettings() override {}
    void resetToDefaults() override {}

    [[nodiscard]] QCheckBox* box() const
    {
        return m_box;
    }

private:
    QCheckBox* m_box = nullptr;
};

} // namespace

// The Settings list General, Projects, Interface, Processing, Raw Images, Plate Solving, Catalogs & Data, Export,
// External Tools and Logging.
TEST(SettingsDialog, PagesInOrder)
{
    const Installed installed;
    SettingsDialog dialog;
    an::ui::addSettingsPages(dialog, nullptr, observatoryOptions());
    EXPECT_EQ(listedPages(dialog),
              (QStringList{QStringLiteral("General"), QStringLiteral("Projects"), QStringLiteral("Interface"),
                           QStringLiteral("Processing"), QStringLiteral("Raw Images"), QStringLiteral("Plate Solving"),
                           QStringLiteral("Catalogs & Data"), QStringLiteral("Export"),
                           QStringLiteral("External Tools"), QStringLiteral("Logging")}));
    EXPECT_EQ(shownEntries(dialog), listedPages(dialog)) << "the navigation lists another order than the pages";
}

// In the Observatory look, drawn by the product's style in both densities, the dialog takes its least size - which a
// 1280 x 720 display at 150 % holds with the window's title bar - and there no control of any page is clipped and every
// scroll area's content is as tall as its width needs. Whether a control is clipped depends on how wide text is, and
// the suites' own platform draws text in a box font about twice the interface's width: there the sizes are asserted and
// the rest is reported skipped; started under the native platform (`QT_QPA_PLATFORM=windows`) the case measures text as
// the interface does.
TEST(SettingsDialog, NoControlIsClipped)
{
    const Installed installed;
    bool measured = true;
    SettingsStore store;
    const auto& looks = an::ui::testing::observatoryLooks();
    for (const an::ui::testing::ObservatoryLook& look : {looks[0], looks[3]})
    {
        const an::ui::testing::ObservatoryStyleScope scope(look);
        settle();
        measured = an::tests::look::textIsMeasured();
        const char* const named = look.name;
        SettingsDialog dialog;
        an::ui::addSettingsPages(dialog, store.persistence(), observatoryOptions());
        dialog.show();
        settle();
        dialog.resize(1, 1);
        // A window manager answers the resize when it comes to it: under X11 the size comes back with its configure
        // event, after the request is sent.
        const QDeadlineTimer answered(2000);
        do
        {
            settle();
        } while (dialog.size() != dialog.minimumSize() && !answered.hasExpired());
        const QSize least = dialog.size();
        EXPECT_EQ(least, dialog.minimumSize()) << named;
        // 858 x 600, or wider where a page's rows need more than the page's 600 px in the font in force (860 in the
        // comfortable density in Noto Sans).
        EXPECT_GE(least.width(), 858) << named;
        EXPECT_EQ(least.height(), 600) << named;
        ::testing::Test::RecordProperty(std::string(named) + "_least",
                                        std::to_string(least.width()) + " x " + std::to_string(least.height()));
        if (measured)
        {
            // The area a 1280 x 720 display at 150 % leaves a window under its taskbar, less a title bar.
            EXPECT_LE(least.width(), 1280) << named;
            EXPECT_LE(least.height(), 688 - 32) << named;
        }
        for (int index = 0; index < dialog.pageCount(); ++index)
        {
            dialog.setCurrentPage(index);
            settle();
            const QString page = dialog.page(index)->pageTitle();
            const QStringList unsized = an::tests::look::unsizedScrollContents(dialog);
            EXPECT_TRUE(unsized.isEmpty())
                << named << ", " << page.toStdString() << ": " << unsized.join(QStringLiteral("\n  ")).toStdString();
            if (measured)
            {
                const QStringList clipped = an::tests::look::clippedControls(dialog);
                EXPECT_TRUE(clipped.isEmpty()) << named << ", " << page.toStdString() << ": "
                                               << clipped.join(QStringLiteral("\n  ")).toStdString();
            }
        }
        dialog.hide();
    }
    if (!measured && !HasFailure())
    {
        GTEST_SKIP()
            << "the least sizes hold; whether anything is clipped is measured under the native platform, where "
               "text is as wide as the interface draws it";
    }
}

// Each page scrolls only as far as its own rows need, in either density: the pages' stack is as tall as the page it
// shows, not as its tallest page, so a short page leaves no empty room under its rows to scroll into.
TEST(SettingsDialog, APageScrollsOnlyAsFarAsItsOwnRowsNeed)
{
    const Installed installed;
    SettingsStore store;
    const auto& looks = an::ui::testing::observatoryLooks();
    for (const an::ui::testing::ObservatoryLook& look : {looks[0], looks[3]})
    {
        const an::ui::testing::ObservatoryStyleScope scope(look);
        settle();
        const std::string named = look.name;
        SettingsDialog dialog;
        an::ui::addSettingsPages(dialog, store.persistence(), observatoryOptions());
        dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
        dialog.show();
        settle();
        dialog.resize(900, 700);
        settle();
        for (int index = 0; index < dialog.pageCount(); ++index)
        {
            dialog.setCurrentPage(index);
            settle();
            const QWidget* const page = dialog.page(index);
            const QWidget* const stack = page->parentWidget();
            ASSERT_NE(stack, nullptr);
            ASSERT_NE(stack->parentWidget(), nullptr);
            const auto* const area = qobject_cast<const QScrollArea*>(stack->parentWidget()->parentWidget());
            ASSERT_NE(area, nullptr) << named << ": the page lies in no scroll area";
            const QMargins margins = stack->contentsMargins();
            const int rows = (std::max)(page->minimumSizeHint().height(),
                                        page->hasHeightForWidth() ? page->heightForWidth(page->width()) : 0);
            const int needed = rows + margins.top() + margins.bottom();
            const int room = area->viewport()->height();
            const int scrolls = area->verticalScrollBar()->maximum();
            EXPECT_LE(scrolls, (std::max)(0, needed - room) + 1)
                << named << ", " << dialog.page(index)->pageTitle().toStdString() << ": the page scrolls " << scrolls
                << " px where its rows need " << needed << " px in " << room;
        }
        dialog.hide();
    }
}

// The search finds a page by any row it shows, whole or in part and in any case: every row the new interface adds to
// the Interface and Projects pages leaves its page listed and the others that show no such row out.
TEST(SettingsDialog, SearchFindsNewRows)
{
    const Installed installed;
    SettingsDialog dialog;
    an::ui::addSettingsPages(dialog, nullptr, observatoryOptions());
    auto* const search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearchEdit"));
    ASSERT_NE(search, nullptr);
    const QStringList all = shownEntries(dialog);

    const std::vector<std::pair<QString, QString>> rows = {
        {QStringLiteral("Colour preset"), QStringLiteral("Interface")},
        {QStringLiteral("Theme"), QStringLiteral("Interface")},
        {QStringLiteral("Contrast"), QStringLiteral("Interface")},
        {QStringLiteral("Accent"), QStringLiteral("Interface")},
        {QStringLiteral("Surface tint"), QStringLiteral("Interface")},
        {QStringLiteral("Tint strength"), QStringLiteral("Interface")},
        {QStringLiteral("Density"), QStringLiteral("Interface")},
        {QStringLiteral("Night"), QStringLiteral("Interface")},
        {QStringLiteral("Animations"), QStringLiteral("Interface")},
        {QStringLiteral("Always show the menu bar"), QStringLiteral("Interface")},
        {QStringLiteral("menu BAR"), QStringLiteral("Interface")},
        {QStringLiteral("Shell"), QStringLiteral("Interface")},
        {QStringLiteral("Default location"), QStringLiteral("Projects")},
        {QStringLiteral("Create a project for every integration job"), QStringLiteral("Projects")},
        {QStringLiteral("History compression"), QStringLiteral("Projects")},
        {QStringLiteral("Ask to compact a history above"), QStringLiteral("Projects")},
        {QStringLiteral("Open the last project on startup"), QStringLiteral("Projects")},
    };
    for (const auto& [row, page] : rows)
    {
        search->setText(row);
        settle();
        const QStringList shown = shownEntries(dialog);
        EXPECT_TRUE(shown.contains(page)) << "\"" << row.toStdString() << "\" leaves " << page.toStdString()
                                          << " out; shown: " << shown.join(QStringLiteral(", ")).toStdString();
        EXPECT_LT(shown.size(), all.size()) << "\"" << row.toStdString() << "\" leaves every page listed";
        EXPECT_FALSE(shown.contains(QStringLiteral("Logging"))) << "\"" << row.toStdString() << "\"";
    }
    search->clear();
    settle();
    EXPECT_EQ(shownEntries(dialog), all);
}

// Reset All to Defaults, then OK, and Reset Page on the Interface and Projects pages, then Apply, each store the
// default of every key of the new interface the dialog shows - the shell, the tint strength, the density, the preset,
// starting in Night, the menu bar, and the five of the Projects page - whatever was stored before.
TEST(SettingsDialog, ResetCoversNewKeys)
{
    const Installed installed;
    const ChoicesRestored restored;
    const an::tests::dialogs::CaseDirectory folder;
    const QString elsewhere = QDir::cleanPath(folder.path(QStringLiteral("Elsewhere")));
    {
        SettingsStore store;
        storeOtherThanTheDefaults(store, elsewhere);
        SettingsDialog dialog;
        an::ui::addSettingsPages(dialog, store.persistence(), observatoryOptions());
        dialog.show();
        settle();
        auto* const resetAll = dialog.findChild<QPushButton*>(QStringLiteral("settingsResetAllButton"));
        ASSERT_NE(resetAll, nullptr);
        pressAtTheNextMessageBox(QMessageBox::Yes);
        resetAll->click();
        settle();
        dialog.accept();
        expectTheDefaults(store, "Reset All to Defaults");
    }
    SettingsStore store;
    storeOtherThanTheDefaults(store, elsewhere);
    SettingsDialog dialog;
    an::ui::addSettingsPages(dialog, store.persistence(), observatoryOptions());
    dialog.show();
    settle();
    auto* const header = dialog.findChild<QWidget*>(QStringLiteral("settingsPageSectionHeader"));
    ASSERT_NE(header, nullptr);
    for (const QString& page : {QStringLiteral("Interface"), QStringLiteral("Projects")})
    {
        dialog.setCurrentPage(page);
        settle();
        ASSERT_EQ(dialog.currentPage()->pageTitle(), page);
        auto* const resetPage = dialog.findChild<QPushButton*>(QStringLiteral("settingsResetPageButton"));
        ASSERT_NE(resetPage, nullptr);
        pressAtTheNextMessageBox(QMessageBox::Yes);
        resetPage->click();
        settle();
    }
    auto* const apply = dialog.findChild<QPushButton*>(QStringLiteral("settingsApplyButton"));
    ASSERT_NE(apply, nullptr);
    apply->click();
    settle();
    expectTheDefaults(store, "Reset Page");
}

// A window is held within the area a screen leaves it, its frame included: the Settings dialog as it opened on a
// 1280 x 720 display at 150 % - 900 x 700 centred, its title bar above the screen - takes the area's height and comes
// down into it; a window that fits is left where it is; one that fits but stands out is moved in; and an area that
// cannot hold the window at its least size bounds nothing.
TEST(WindowPlacement, AWindowIsHeldWithinTheAreaItsScreenLeavesIt)
{
    const QRect area(0, 0, 1280, 688);
    const QMargins frame(1, 31, 1, 1);
    const QSize least(858, 600);

    const QRect asked(190, -6, 900, 700);
    const QRect held = an::ui::geometryWithinArea(asked, frame, least, area);
    EXPECT_TRUE(area.contains(held.marginsAdded(frame))) << "the window stands out of the area";
    EXPECT_EQ(held.size(), QSize(900, 688 - 32));
    EXPECT_EQ(held.left(), 190) << "a window held moved sideways";

    const QRect fits(100, 40, 900, 620);
    EXPECT_EQ(an::ui::geometryWithinArea(fits, frame, least, area), fits);

    const QRect standsOut(800, 60, 900, 620);
    const QRect movedIn = an::ui::geometryWithinArea(standsOut, frame, least, area);
    EXPECT_EQ(movedIn.size(), standsOut.size());
    EXPECT_TRUE(area.contains(movedIn.marginsAdded(frame)));

    const QRect small(0, 0, 640, 480);
    EXPECT_EQ(an::ui::geometryWithinArea(asked, frame, least, small), asked)
        << "an area that cannot hold the least size bounded the window";

    // An area that does not start at the screen's corner - a taskbar at the top - is held to as it is.
    const QRect lowered(0, 40, 1280, 680);
    EXPECT_TRUE(lowered.contains(an::ui::geometryWithinArea(asked, frame, least, lowered).marginsAdded(frame)));
}

// A window that asked to be held is held within the available area of its screen as it is first shown - larger than the
// area and standing out of it, it comes to lie inside, its frame included - and is not held again: shown a second time
// where a person left it, standing out of the screen, it stays there.
TEST(WindowPlacement, AWindowIsHeldAsItIsFirstShownAndNotAgain)
{
    const QScreen* const screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect area = screen->availableGeometry();
    QWidget window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.setMinimumSize(120, 90);
    window.setGeometry(area.left() + 40, area.top() + 40, area.width() + 200, area.height() + 160);
    an::ui::holdWithinAvailableAreaWhenFirstShown(window);
    EXPECT_GT(window.width(), area.width()) << "the window was held before it was shown";

    window.show();
    settle();
    EXPECT_TRUE(area.contains(window.frameGeometry()))
        << "the window's frame (" << window.frameGeometry().x() << ", " << window.frameGeometry().y() << ", "
        << window.frameGeometry().width() << " x " << window.frameGeometry().height()
        << ") stands out of the screen's area (" << area.x() << ", " << area.y() << ", " << area.width() << " x "
        << area.height() << ")";
    EXPECT_GE(window.width(), window.minimumWidth());
    EXPECT_GE(window.height(), window.minimumHeight());

    window.hide();
    const QRect theirs(area.right() - 60, area.top() + 48, 160, 120);
    window.setGeometry(theirs);
    window.show();
    settle();
    EXPECT_EQ(window.geometry(), theirs) << "a window shown again was moved from where it was left";
}

// A window held at its first show is sized for the look first: a field stated narrower than it needs takes what it
// needs, a window stated smaller than its layout needs takes its layout's least size, and a body that scrolls is shown
// whole where the screen has room for it.
TEST(WindowPlacement, AWindowIsSizedForTheLookAsItIsFirstShown)
{
    struct Built
    {
        QSpinBox* field = nullptr;
        QLabel* label = nullptr;
        QScrollArea* body = nullptr;
    };
    const auto build = [](QDialog& window) {
        Built built;
        window.setAttribute(Qt::WA_DontShowOnScreen, true);
        auto* const column = new QVBoxLayout(&window);
        auto* const above = new QLabel(QStringLiteral("Above"), &window);
        column->addWidget(above);
        built.field = new QSpinBox(&window);
        built.field->setRange(0, 100000);
        built.field->setMinimumWidth(12);
        built.field->setMaximumWidth(12);
        column->addWidget(built.field);
        for (int row = 0; row < 6; ++row)
        {
            column->addWidget(new QLabel(QStringLiteral("Row %1").arg(row), &window));
        }
        // A label stated narrower than its text, beside a field that takes the rest of its row.
        auto* const named = new QHBoxLayout();
        built.label = new QLabel(QStringLiteral("A label stated narrower than its text"), &window);
        built.label->setMinimumSize(QSize(10, 0));
        named->addWidget(built.label);
        named->addWidget(new QLineEdit(&window));
        column->addLayout(named);
        auto* const footer = new QPushButton(QStringLiteral("Close"), &window);
        column->addWidget(footer);
        built.body = an::ui::scrollWindowBody(*column, above, footer);
        window.setMinimumSize(40, 20);
        window.resize(220, 60);
        an::ui::holdWithinAvailableAreaWhenFirstShown(window);
        return built;
    };
    const an::ui::testing::ObservatoryStyleScope observatory(an::ui::testing::observatoryLooks()[0]);
    QDialog window;
    const Built built = build(window);
    window.show();
    settle();
    EXPECT_GE(built.field->width(), built.field->minimumSizeHint().width()) << "the field kept the width stated for it";
    EXPECT_GE(window.minimumHeight(), window.layout()->minimumSize().height())
        << "the window's least size is under what its layout needs";
    ASSERT_NE(built.body, nullptr);
    const int whole = built.body->widget()->sizeHint().height();
    const QRect area = window.screen()->availableGeometry();
    const int heightShowingAll = window.frameGeometry().height() - built.body->viewport()->height() + whole;
    if (heightShowingAll <= area.height())
    {
        EXPECT_GE(built.body->viewport()->height(), whole) << "the body is not shown whole";
    }
    else
    {
        EXPECT_TRUE(area.contains(window.frameGeometry())) << "the window stands out of its screen";
    }
    window.resize(1, 1);
    settle();
    EXPECT_GE(built.label->width(), built.label->minimumSizeHint().width())
        << "at the window's least size the label kept the width stated for it, under its text's";
}

// A window whose text wraps is, at its least size, as high as its rows need at its least width: Qt works a window's
// least size out without the height its layout needs for a width, so a card of wrapping values under a wrapping
// description had its rows squeezed under their lines there, by a few pixels each in a font whose lines are taller
// (Noto Sans). How a form lays its wrapping values out depends on how wide text is, and the suites' own platform on
// Windows draws a box font about twice the interface's width: there the window's least height is asserted and the
// labels' lines are reported skipped; started under the native platform (`QT_QPA_PLATFORM=windows`), or under
// `offscreen`, the case measures them as the interface does.
TEST(WindowPlacement, AWindowWhoseTextWrapsIsAsHighAsItsLinesAtItsLeastSize)
{
    const an::ui::testing::ObservatoryStyleScope observatory(an::ui::testing::observatoryLooks()[0]);
    const bool measured = an::tests::look::textIsMeasured();
    QDialog window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto* const column = new QVBoxLayout(&window);
    auto* const description =
        new QLabel(QStringLiteral("A description long enough to take several lines where the window is as narrow as "
                                  "it can be made, which is narrower than this sentence"),
                   &window);
    description->setWordWrap(true);
    column->addWidget(description);
    auto* const card = new QWidget(&window);
    auto* const rows = new QFormLayout(card);
    for (int row = 0; row < 8; ++row)
    {
        auto* const value = new QLabel(QStringLiteral("A value that wraps where it is narrow, %1").arg(row), card);
        value->setWordWrap(true);
        rows->addRow(QStringLiteral("Key %1").arg(row), value);
    }
    column->addWidget(card);
    an::ui::holdWithinAvailableAreaWhenFirstShown(window);
    window.show();
    settle();
    window.resize(1, 1);
    settle();
    const auto expectLinesWhole = [&window, measured](const char* when) {
        ASSERT_TRUE(window.layout()->hasHeightForWidth());
        EXPECT_GE(window.height(), window.layout()->totalHeightForWidth(window.width()))
            << when << ", at its least size, " << window.width() << " x " << window.height();
        if (!measured)
        {
            return;
        }
        for (const QLabel* label : window.findChildren<QLabel*>())
        {
            const int lines = label->wordWrap() ? label->heightForWidth(label->width()) : label->sizeHint().height();
            EXPECT_GE(label->height(), lines) << when << ": " << label->text().toStdString();
        }
    };
    expectLinesWhole("as it was shown");

    // What it shows grows after its first show - a banner, a row - and its least height follows, then comes back.
    auto* const banner = new QLabel(QStringLiteral("A banner shown later, as long as the description and wrapping as "
                                                   "it does where the window is narrow"),
                                    &window);
    banner->setWordWrap(true);
    column->insertWidget(0, banner);
    banner->show();
    settle();
    window.resize(1, 1);
    settle();
    expectLinesWhole("with a banner shown later");
    const int withTheBanner = window.minimumHeight();
    delete banner;
    settle();
    EXPECT_LT(window.minimumHeight(), withTheBanner) << "the least height did not come back as the banner went";
    window.resize(1, 1);
    settle();
    expectLinesWhole("once the banner went");
    if (!measured && !HasFailure())
    {
        GTEST_SKIP() << "the least heights hold; whether each label's lines are whole is measured under the native "
                        "platform, where text is drawn as the interface draws it";
    }
}

// A page whose rows need more than the page's least width widens the dialog's least size: the pages' stack takes its
// scroll area's width whatever its pages need, so a row wider than the 600 px stated was cut short at the dialog's
// least size - the Export page's byte-shuffle check box, 2 px short in Noto Sans.
TEST(SettingsDialog, APageWhoseRowsNeedMoreThanTheLeastWidthWidensTheDialog)
{
    const Installed installed;
    const an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    settle();
    SettingsDialog dialog;
    auto* const page = new WideRowPage();
    dialog.addPage(page);
    dialog.show();
    settle();
    dialog.resize(1, 1);
    settle();
    EXPECT_GE(page->box()->width(), page->box()->minimumSizeHint().width())
        << "at the dialog's least size, " << dialog.width() << " x " << dialog.height();
    EXPECT_GE(page->width(), page->minimumSizeHint().width());
    dialog.hide();
}

// The Settings dialog opens within the available area of the screen it is shown on, its frame included, where that area
// holds its least size. The suites' own platform has a screen smaller than the least size, which bounds nothing: there
// the least size is asserted and the rest is reported skipped; natively the case measures the screen it opens on.
TEST(SettingsDialog, OpensNoLargerThanTheScreen)
{
    const Installed installed;
    SettingsDialog dialog;
    an::ui::addSettingsPages(dialog, nullptr, observatoryOptions());
    dialog.show();
    settle();
    const QScreen* const screen = dialog.screen();
    ASSERT_NE(screen, nullptr);
    const QRect area = screen->availableGeometry();
    const QRect frame = dialog.frameGeometry();
    const QMargins margins(dialog.geometry().left() - frame.left(), dialog.geometry().top() - frame.top(),
                           frame.right() - dialog.geometry().right(), frame.bottom() - dialog.geometry().bottom());
    EXPECT_GE(dialog.width(), dialog.minimumWidth());
    EXPECT_GE(dialog.height(), dialog.minimumHeight());
    if (area.width() < dialog.minimumWidth() + margins.left() + margins.right() ||
        area.height() < dialog.minimumHeight() + margins.top() + margins.bottom())
    {
        GTEST_SKIP() << "the screen (" << area.width() << " x " << area.height()
                     << ") cannot hold the dialog's least size, and so bounds nothing";
    }
    EXPECT_TRUE(area.contains(frame)) << "the dialog's frame (" << frame.x() << ", " << frame.y() << ", "
                                      << frame.width() << " x " << frame.height()
                                      << ") stands out of the screen's area (" << area.x() << ", " << area.y() << ", "
                                      << area.width() << " x " << area.height() << ")";
}
