// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Interface page's rows of the Observatory profile - the colour presets, the tint strength, the density, Night and
/// starting in it - shown under that profile alone: a preset's tile sets its three parts, a part changed by hand shows
/// Custom, every change acts at once, Apply keeps it and Cancel puts back what was in force; and the settings file
/// carries the new keys out and in.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/SettingsDialog.h"
#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/InterfaceSettingsPage.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/ScopedNightMode.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

namespace
{

using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::SettingsStore;
using an::ui::StyleProfile;
using an::ui::testing::ScopedStyleProfile;

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

class InterfacePage : public ::testing::Test
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

template <typename T> T* named(QWidget& page, const char* name)
{
    return page.findChild<T*>(QString::fromLatin1(name));
}

/// A preset's tile, by the preset's settings name.
QAbstractButton* tile(QWidget& page, const char* preset)
{
    return page.findChild<QAbstractButton*>(QStringLiteral("anInterfaceSettingsPreset_%1").arg(QLatin1String(preset)));
}

/// Answers the next message box a modal loop shows with Discard.
void discardAtTheNextMessageBox()
{
    auto* const poll = new QTimer(qApp);
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, poll, [poll]() {
        if (auto* const box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            box->button(QMessageBox::Discard)->click();
            poll->deleteLater();
        }
    });
    poll->start();
}

} // namespace

// The rows are shown under the Observatory profile alone: the Classic profile has neither presets, a strength, a
// density nor Night.
TEST_F(InterfacePage, ObservatoryRowsShownUnderObservatoryAndAbsentUnderClassic)
{
    {
        const ScopedStyleProfile classic(StyleProfile::Classic);
        InterfaceSettingsPage page(nullptr);
        page.loadSettings();
        auto* const group = named<QGroupBox>(page, "anInterfaceSettingsObservatoryGroup");
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(group->isHidden()) << "the Classic profile shows the Observatory profile's rows";
    }
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    InterfaceSettingsPage page(nullptr);
    page.loadSettings();
    auto* const group = named<QGroupBox>(page, "anInterfaceSettingsObservatoryGroup");
    ASSERT_NE(group, nullptr);
    EXPECT_FALSE(group->isHidden());
    for (const char* preset : {"slate", "observatory", "deepsea", "graphite", "nebula", "aurora"})
    {
        ASSERT_NE(tile(page, preset), nullptr) << preset;
        EXPECT_FALSE(tile(page, preset)->isHidden()) << preset;
    }
    ASSERT_NE(tile(page, "custom"), nullptr);
    EXPECT_TRUE(tile(page, "custom")->isHidden()) << "Custom is shown only when the parts are no preset's";
    EXPECT_TRUE(tile(page, "slate")->isChecked());
    for (const char* strength : {"off", "subtle", "balanced", "strong"})
    {
        EXPECT_NE(page.findChild<QAbstractButton*>(
                      QStringLiteral("anInterfaceSettingsTintStrength_%1").arg(QLatin1String(strength))),
                  nullptr)
            << strength;
    }
    for (const char* density : {"compact", "comfortable"})
    {
        EXPECT_NE(page.findChild<QAbstractButton*>(
                      QStringLiteral("anInterfaceSettingsDensity_%1").arg(QLatin1String(density))),
                  nullptr)
            << density;
    }
    EXPECT_NE(named<QAbstractButton>(page, "anInterfaceSettingsNight"), nullptr);
    EXPECT_NE(named<QAbstractButton>(page, "anInterfaceSettingsStartInNight"), nullptr);
}

// A tile sets the preset's three parts at once - the accent, the tint and the strength, which the page's rows show -
// and Apply keeps them with the preset's name.
TEST_F(InterfacePage, ObservatoryRowsATileSetsThreeSettings)
{
    const ChoicesRestored restored;
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    SettingsStore store;
    InterfaceSettingsPage page(store.persistence());
    page.loadSettings();
    ASSERT_NE(tile(page, "nebula"), nullptr);

    tile(page, "nebula")->click();
    EXPECT_EQ(an::ui::themeAccentFamily(), an::ui::ThemeAccentFamily::Rose) << "the tile did not act at once";
    EXPECT_EQ(an::ui::themeSurfaceTintFamily(), an::ui::ThemeSurfaceTintFamily::Amethyst);
    EXPECT_EQ(an::ui::themeTintStrength(), an::ui::ThemeTintStrength::Balanced);
    EXPECT_TRUE(tile(page, "nebula")->isChecked());
    auto* const strength = page.findChild<QAbstractButton*>(QStringLiteral("anInterfaceSettingsTintStrength_balanced"));
    ASSERT_NE(strength, nullptr);
    EXPECT_TRUE(strength->isChecked());
    EXPECT_TRUE(page.hasUnsavedChanges());
    EXPECT_FALSE(store.stored(SettingsKeys::INTERFACE_THEME_PRESET).has_value()) << "a tile wrote before Apply";

    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY), QStringLiteral("rose"));
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY), QStringLiteral("amethyst"));
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH), QStringLiteral("balanced"));
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_PRESET), QStringLiteral("nebula"));
    EXPECT_FALSE(page.hasUnsavedChanges());
}

// A part changed by hand leaves the preset: the Custom tile appears, chosen, and Apply keeps the preset as Custom.
TEST_F(InterfacePage, ObservatoryRowsAHandChangeShowsCustom)
{
    const ChoicesRestored restored;
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    SettingsStore store;
    InterfaceSettingsPage page(store.persistence());
    page.loadSettings();
    ASSERT_NE(tile(page, "custom"), nullptr);
    ASSERT_TRUE(tile(page, "custom")->isHidden());

    auto* const strong = page.findChild<QAbstractButton*>(QStringLiteral("anInterfaceSettingsTintStrength_strong"));
    ASSERT_NE(strong, nullptr);
    strong->click();
    EXPECT_EQ(an::ui::themeTintStrength(), an::ui::ThemeTintStrength::Strong) << "the strength did not act at once";
    EXPECT_FALSE(tile(page, "custom")->isHidden());
    EXPECT_TRUE(tile(page, "custom")->isChecked());
    EXPECT_FALSE(tile(page, "slate")->isChecked());

    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_PRESET), QStringLiteral("custom"));
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH), QStringLiteral("strong"));
}

// Every row acts at once and is kept by Apply as the page's other rows are; Cancel puts back what was in force and
// keeps nothing. Night is never kept, but starting in it is.
TEST_F(InterfacePage, ObservatoryRowsApplyAndCancelBehaveAsThePagesOtherRows)
{
    const ChoicesRestored restored;
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    SettingsStore store;
    InterfaceSettingsPage page(store.persistence());
    page.loadSettings();
    auto* const comfortable =
        page.findChild<QAbstractButton*>(QStringLiteral("anInterfaceSettingsDensity_comfortable"));
    auto* const night = named<QAbstractButton>(page, "anInterfaceSettingsNight");
    auto* const startInNight = named<QAbstractButton>(page, "anInterfaceSettingsStartInNight");
    ASSERT_NE(comfortable, nullptr);
    ASSERT_NE(night, nullptr);
    ASSERT_NE(startInNight, nullptr);

    comfortable->click();
    night->click();
    EXPECT_EQ(an::ui::themeDensity(), an::ui::ThemeDensity::Comfortable) << "the density did not act at once";
    EXPECT_TRUE(an::ui::isNightMode()) << "Night did not act at once";
    EXPECT_TRUE(page.hasUnsavedChanges());

    page.discardChanges();
    EXPECT_EQ(an::ui::themeDensity(), an::ui::ThemeDensity::Compact) << "Cancel left the density in force";
    EXPECT_FALSE(an::ui::nightModeSet()) << "Cancel left Night on";
    EXPECT_FALSE(store.stored(SettingsKeys::INTERFACE_DENSITY).has_value());
    EXPECT_FALSE(page.hasUnsavedChanges());
    EXPECT_FALSE(comfortable->isChecked()) << "the page does not show what is in force again";

    comfortable->click();
    night->click();
    startInNight->click();
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_DENSITY), QStringLiteral("comfortable"));
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_START_IN_NIGHT), QStringLiteral("1"));
    EXPECT_TRUE(an::ui::isNightMode()) << "Apply switched Night off";
    page.discardChanges();
    EXPECT_EQ(an::ui::themeDensity(), an::ui::ThemeDensity::Comfortable) << "Cancel after Apply undid what was kept";
    EXPECT_TRUE(an::ui::isNightMode());
}

// The settings dialog's Cancel, once the person agrees to discard, tells each page to put back what it showed at once.
TEST_F(InterfacePage, TheDialogsCancelPutsBackWhatThePageShowedAtOnce)
{
    const ChoicesRestored restored;
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    SettingsStore store;
    SettingsDialog dialog(nullptr);
    auto* const page = new InterfaceSettingsPage(store.persistence());
    dialog.addPage(page);
    dialog.show();
    QApplication::processEvents();
    auto* const strong = page->findChild<QAbstractButton*>(QStringLiteral("anInterfaceSettingsTintStrength_strong"));
    ASSERT_NE(strong, nullptr);
    strong->click();
    ASSERT_EQ(an::ui::themeTintStrength(), an::ui::ThemeTintStrength::Strong);

    discardAtTheNextMessageBox();
    dialog.reject();
    EXPECT_EQ(an::ui::themeTintStrength(), an::ui::ThemeTintStrength::Balanced) << "Cancel left the strength in force";
    EXPECT_FALSE(store.stored(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH).has_value());
}

// The settings file carries the tint strength, the density, the preset and starting in Night with the theme's other
// keys, and an imported one stores them.
TEST_F(InterfacePage, TheSettingsFileCarriesTheNewInterfaceKeys)
{
    SettingsStore store;
    store.store(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH, QStringLiteral("strong"));
    store.store(SettingsKeys::INTERFACE_DENSITY, QStringLiteral("comfortable"));
    store.store(SettingsKeys::INTERFACE_THEME_PRESET, QStringLiteral("observatory"));
    store.store(SettingsKeys::INTERFACE_START_IN_NIGHT, QStringLiteral("1"));
    GeneralSettingsPage page(store.persistence());
    page.loadSettings();
    const QJsonObject exported = page.exportedSettings().value(QStringLiteral("interface")).toObject();
    EXPECT_EQ(exported.value(QStringLiteral("themeTintStrength")).toString(), QStringLiteral("strong"));
    EXPECT_EQ(exported.value(QStringLiteral("density")).toString(), QStringLiteral("comfortable"));
    EXPECT_EQ(exported.value(QStringLiteral("themePreset")).toString(), QStringLiteral("observatory"));
    EXPECT_TRUE(exported.value(QStringLiteral("startInNight")).toBool());

    SettingsStore other;
    GeneralSettingsPage importing(other.persistence());
    importing.loadSettings();
    ASSERT_TRUE(importing.stageImportedSettings(QJsonObject{{QStringLiteral("interface"), exported}}));
    importing.saveSettings();
    EXPECT_EQ(other.stored(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH), QStringLiteral("strong"));
    EXPECT_EQ(other.stored(SettingsKeys::INTERFACE_DENSITY), QStringLiteral("comfortable"));
    EXPECT_EQ(other.stored(SettingsKeys::INTERFACE_THEME_PRESET), QStringLiteral("observatory"));
    EXPECT_EQ(other.stored(SettingsKeys::INTERFACE_START_IN_NIGHT), QStringLiteral("1"));
}

namespace
{

/// The two shells the application registers.
std::vector<an::ui::ShellOption> bothShells()
{
    return {an::ui::ShellOption{.id = QStringLiteral("classic"), .displayName = QStringLiteral("Classic")},
            an::ui::ShellOption{.id = QStringLiteral("observatory"), .displayName = QStringLiteral("Observatory")}};
}

/// The titles of the groups a page shows, in the order its layout holds them.
QStringList shownGroups(QWidget& page)
{
    QStringList titles;
    QLayout* const layout = page.layout();
    if (layout == nullptr)
    {
        return titles;
    }
    for (int index = 0; index < layout->count(); ++index)
    {
        if (auto* const group = qobject_cast<QGroupBox*>(layout->itemAt(index)->widget());
            group != nullptr && !group->isHidden())
        {
            titles.append(group->title());
        }
    }
    return titles;
}

/// The labels of a form's rows, in order, each without its colon; empty for a row with no label.
QStringList rowLabels(const QFormLayout& form)
{
    QStringList labels;
    for (int row = 0; row < form.rowCount(); ++row)
    {
        QString label;
        if (const QLayoutItem* const item = form.itemAt(row, QFormLayout::LabelRole);
            item != nullptr && item->widget() != nullptr)
        {
            if (const auto* const text = qobject_cast<const QLabel*>(item->widget()))
            {
                label = text->text();
            }
        }
        if (label.endsWith(QLatin1Char(':')))
        {
            label.chop(1);
        }
        labels.append(label);
    }
    return labels;
}

/// A button of the page by the prefix of its row and the setting's value.
QAbstractButton* choice(QWidget& page, const char* row, const char* value)
{
    return page.findChild<QAbstractButton*>(
        QStringLiteral("anInterfaceSettings%1_%2").arg(QLatin1String(row), QLatin1String(value)));
}

} // namespace

// Under the Observatory profile the page's first group holds the choices of the new interface in their order - the
// colour preset, the theme, the contrast, the accent, the surface tint, the tint strength, the density, Night and
// starting in it, the animations and the menu bar - with the Shell row right after it, above the rows 1.1 had, and the
// combo boxes of 1.1's Appearance are not shown. Under the Classic profile the page is as 1.1 drew it, the Shell row
// last.
TEST_F(InterfacePage, ObservatoryRowsHoldEveryChoiceInItsOrder)
{
    {
        const ScopedStyleProfile classic(StyleProfile::Classic);
        InterfaceSettingsPage page(nullptr);
        page.setAvailableShells(bothShells());
        page.loadSettings();
        auto* const appearance = named<QGroupBox>(page, "anInterfaceSettingsAppearanceGroup");
        ASSERT_NE(appearance, nullptr);
        EXPECT_FALSE(appearance->isHidden());
        EXPECT_EQ(shownGroups(page), (QStringList{QStringLiteral("Appearance"), QStringLiteral("Image Viewing"),
                                                  QStringLiteral("File Browser"), QStringLiteral("Window Layout"),
                                                  QStringLiteral("Shell")}));
    }
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    InterfaceSettingsPage page(nullptr);
    page.setAvailableShells(bothShells());
    page.setMenuBarKeeper([](bool) {});
    page.loadSettings();
    auto* const appearance = named<QGroupBox>(page, "anInterfaceSettingsAppearanceGroup");
    ASSERT_NE(appearance, nullptr);
    EXPECT_TRUE(appearance->isHidden()) << "the Observatory profile shows 1.1's combo boxes";
    EXPECT_EQ(shownGroups(page),
              (QStringList{QStringLiteral("Colours and Size"), QStringLiteral("Shell"), QStringLiteral("Image Viewing"),
                           QStringLiteral("File Browser"), QStringLiteral("Window Layout")}));
    auto* const group = named<QGroupBox>(page, "anInterfaceSettingsObservatoryGroup");
    ASSERT_NE(group, nullptr);
    auto* const form = qobject_cast<QFormLayout*>(group->layout());
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(rowLabels(*form),
              (QStringList{QStringLiteral("Colour preset"), QStringLiteral("Theme"), QStringLiteral("Contrast"),
                           QStringLiteral("Accent"), QStringLiteral("Surface tint"), QStringLiteral("Tint strength"),
                           QStringLiteral("Density"), QStringLiteral("Night"), QString(), QStringLiteral("Animations"),
                           QStringLiteral("Menu bar")}));
    for (const char* theme : {"system", "light", "dark"})
    {
        EXPECT_NE(choice(page, "Theme", theme), nullptr) << theme;
    }
    for (const char* contrast : {"soft", "balanced", "high", "oled"})
    {
        EXPECT_NE(choice(page, "Contrast", contrast), nullptr) << contrast;
    }
    for (const char* accent : {"blue", "teal", "amber", "amethyst", "rose"})
    {
        EXPECT_NE(choice(page, "Accent", accent), nullptr) << accent;
    }
    for (const char* tint : {"neutral", "ocean", "mint", "amethyst", "rose", "amber"})
    {
        EXPECT_NE(choice(page, "Tint", tint), nullptr) << tint;
    }
    EXPECT_NE(named<QAbstractButton>(page, "anInterfaceSettingsAnimations"), nullptr);
    EXPECT_NE(named<QAbstractButton>(page, "anInterfaceSettingsAlwaysShowMenuBar"), nullptr);
}

// Under the Observatory profile a theme's tile, a contrast's segment, an accent's disc and a tint's act at once, each
// shown chosen; Cancel puts back what was in force and Apply keeps what is chosen. The light theme offers no OLED
// contrast.
TEST_F(InterfacePage, ObservatoryPickersActAtOnce)
{
    const ChoicesRestored restored;
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    SettingsStore store;
    InterfaceSettingsPage page(store.persistence());
    page.loadSettings();
    for (const auto& [row, value] : std::vector<std::pair<const char*, const char*>>{
             {"Accent", "teal"}, {"Tint", "ocean"}, {"Contrast", "high"}, {"Theme", "dark"}})
    {
        ASSERT_NE(choice(page, row, value), nullptr) << row << " " << value;
    }
    EXPECT_TRUE(choice(page, "Accent", "blue")->isChecked());
    EXPECT_TRUE(choice(page, "Tint", "neutral")->isChecked());
    EXPECT_TRUE(choice(page, "Contrast", "balanced")->isChecked());
    EXPECT_TRUE(choice(page, "Theme", "dark")->isChecked());

    choice(page, "Accent", "teal")->click();
    choice(page, "Tint", "ocean")->click();
    choice(page, "Contrast", "high")->click();
    EXPECT_EQ(an::ui::themeAccentFamily(), an::ui::ThemeAccentFamily::Teal) << "the accent did not act at once";
    EXPECT_EQ(an::ui::themeSurfaceTintFamily(), an::ui::ThemeSurfaceTintFamily::Ocean);
    EXPECT_EQ(an::ui::themeContrastProfile(), an::ui::ThemeContrastProfile::High);
    EXPECT_TRUE(choice(page, "Accent", "teal")->isChecked());
    EXPECT_FALSE(choice(page, "Accent", "blue")->isChecked());
    EXPECT_TRUE(page.hasUnsavedChanges());
    EXPECT_FALSE(store.stored(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY).has_value()) << "a disc wrote before Apply";

    page.discardChanges();
    EXPECT_EQ(an::ui::themeAccentFamily(), an::ui::ThemeAccentFamily::Blue) << "Cancel left the accent in force";
    EXPECT_EQ(an::ui::themeSurfaceTintFamily(), an::ui::ThemeSurfaceTintFamily::Neutral);
    EXPECT_EQ(an::ui::themeContrastProfile(), an::ui::ThemeContrastProfile::Balanced);
    EXPECT_TRUE(choice(page, "Accent", "blue")->isChecked()) << "the page does not show what is in force again";

    ASSERT_NE(choice(page, "Theme", "light"), nullptr);
    ASSERT_NE(choice(page, "Contrast", "oled"), nullptr);
    EXPECT_TRUE(choice(page, "Contrast", "oled")->isEnabled());
    choice(page, "Theme", "light")->click();
    EXPECT_TRUE(choice(page, "Theme", "light")->isChecked());
    EXPECT_FALSE(choice(page, "Contrast", "oled")->isEnabled()) << "the light theme offers the OLED contrast";
    choice(page, "Accent", "amber")->click();
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME), QStringLiteral("light"));
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY), QStringLiteral("amber"));
    EXPECT_FALSE(page.hasUnsavedChanges());
}

// "Always show the menu bar" is offered where the shell shows its menu bar on Alt - where it is handed a keeper - and
// nowhere else. Switched, it keeps the bar shown at once; Cancel puts back what was in force; Apply stores it; Reset
// brings it back to its default, the bar shown on Alt.
TEST_F(InterfacePage, TheMenuBarRowIsOfferedWhereTheBarShowsOnAlt)
{
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    {
        InterfaceSettingsPage page(nullptr);
        page.loadSettings();
        auto* const row = named<QAbstractButton>(page, "anInterfaceSettingsAlwaysShowMenuBar");
        ASSERT_NE(row, nullptr);
        EXPECT_TRUE(row->isHidden()) << "a shell whose menu bar is always shown is offered the row";
    }
    SettingsStore store;
    std::vector<bool> kept;
    InterfaceSettingsPage page(store.persistence());
    page.setMenuBarKeeper([&kept](const bool shown) { kept.push_back(shown); });
    page.loadSettings();
    auto* const row = named<QAbstractButton>(page, "anInterfaceSettingsAlwaysShowMenuBar");
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->isHidden());
    EXPECT_FALSE(row->isChecked());

    row->click();
    EXPECT_EQ(kept, std::vector<bool>{true}) << "the bar was not kept shown at once";
    EXPECT_TRUE(page.hasUnsavedChanges());
    EXPECT_FALSE(store.stored(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR).has_value());
    page.discardChanges();
    EXPECT_EQ(kept, (std::vector<bool>{true, false})) << "Cancel left the bar kept shown";
    EXPECT_FALSE(row->isChecked());

    row->click();
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR), QStringLiteral("1"));
    page.resetToDefaults();
    EXPECT_FALSE(row->isChecked());
    EXPECT_FALSE(kept.empty());
    EXPECT_FALSE(kept.back()) << "Reset left the bar kept shown";
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR), QStringLiteral("0"));
}
