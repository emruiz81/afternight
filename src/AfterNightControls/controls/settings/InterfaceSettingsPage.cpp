// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "InterfaceSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "../GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "app/ThemeManager.h"
#include "components/AnSegmentedControl.h"
#include "components/AnSwitch.h"
#include "controls/settings/ThemePresetTile.h"
#include "controls/settings/ThemeSwatch.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnimationUtils.h"
#include "widgets/FlowLayout.h"

#include <AfterNightPlatform/Logging.h>

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QPushButton>
#include <QSignalBlocker>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QStandardItemModel>
#include <QCoreApplication>

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

W_OBJECT_IMPL(InterfaceSettingsPage)

namespace
{

[[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* currentInterfaceSettingsRuntimeService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getInterfaceSettingsRuntimeService();
    }

    return nullptr;
}

/// Sets a flag for its scope: the page is setting its own rows, which is no person's change.
class Showing final
{
public:
    explicit Showing(bool& flag) : m_flag(flag), m_was(std::exchange(flag, true)) {}
    ~Showing()
    {
        m_flag = m_was;
    }
    Showing(const Showing&) = delete;
    Showing& operator=(const Showing&) = delete;

private:
    bool& m_flag;
    bool m_was;
};

/// The presets as the page shows them, in their order, with their names.
std::array<std::pair<an::ui::ThemePreset, QString>, 7u> presetNames()
{
    using an::ui::ThemePreset;
    return {{{ThemePreset::SlateBlue, QCoreApplication::translate("InterfaceSettingsPage", "Slate Blue")},
             {ThemePreset::Observatory, QCoreApplication::translate("InterfaceSettingsPage", "Observatory")},
             {ThemePreset::DeepSea, QCoreApplication::translate("InterfaceSettingsPage", "Deep Sea")},
             {ThemePreset::Graphite, QCoreApplication::translate("InterfaceSettingsPage", "Graphite")},
             {ThemePreset::Nebula, QCoreApplication::translate("InterfaceSettingsPage", "Nebula")},
             {ThemePreset::Aurora, QCoreApplication::translate("InterfaceSettingsPage", "Aurora")},
             {ThemePreset::Custom, QCoreApplication::translate("InterfaceSettingsPage", "Custom")}}};
}

/// The themes, in the order of their tiles and of the record's combo box, by the ids the settings store keeps.
constexpr std::array<const char*, 3u> kThemeIds{"system", "light", "dark"};
/// The contrast profiles, in the order of their segments.
constexpr std::array<const char*, 4u> kContrastIds{"soft", "balanced", "high", "oled"};
/// The accents and the surface tints, in the order of their swatches and of the record's combo boxes.
constexpr std::array<an::ui::ThemeAccentFamily, 5u> kAccents{
    an::ui::ThemeAccentFamily::Blue, an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
    an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose};
constexpr std::array<an::ui::ThemeSurfaceTintFamily, 6u> kTints{
    an::ui::ThemeSurfaceTintFamily::Neutral, an::ui::ThemeSurfaceTintFamily::Ocean,
    an::ui::ThemeSurfaceTintFamily::Mint,    an::ui::ThemeSurfaceTintFamily::Amethyst,
    an::ui::ThemeSurfaceTintFamily::Rose,    an::ui::ThemeSurfaceTintFamily::Amber};
/// The tint strengths and the densities, in the order of their segments.
constexpr std::array<an::ui::ThemeTintStrength, 4u> kStrengths{
    an::ui::ThemeTintStrength::Off, an::ui::ThemeTintStrength::Subtle, an::ui::ThemeTintStrength::Balanced,
    an::ui::ThemeTintStrength::Strong};
constexpr std::array<an::ui::ThemeDensity, 2u> kDensities{an::ui::ThemeDensity::Compact,
                                                          an::ui::ThemeDensity::Comfortable};

/// Where a value is in a list; -1 when it is not.
template <typename T, std::size_t N> int indexIn(const std::array<T, N>& values, const T& value)
{
    for (std::size_t index = 0; index < N; ++index)
    {
        if (values[index] == value)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

/// Where an id is in a list of ids; -1 when it is not.
template <std::size_t N> int indexOfId(const std::array<const char*, N>& ids, const QString& id)
{
    for (std::size_t index = 0; index < N; ++index)
    {
        if (id == QLatin1String(ids[index]))
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

/// Chooses the item of a combo box that holds an id, when it holds it.
void chooseIn(QComboBox* combo, const QString& id)
{
    const int index = combo->findData(id);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

/// The tokens the choices in force resolve to with one part changed: what a swatch shows.
an::ui::DesignTokens tokensWith(const std::function<void(an::ui::ThemeChoices&)>& change)
{
    an::ui::ThemeChoices choices{.theme =
                                     an::ui::isDarkTheme() ? an::ui::ThemeVariant::Dark : an::ui::ThemeVariant::Light,
                                 .contrast = an::ui::themeContrastProfile(),
                                 .accent = an::ui::themeAccentFamily(),
                                 .tint = an::ui::themeSurfaceTintFamily(),
                                 .tintStrength = an::ui::themeTintStrength(),
                                 .density = an::ui::themeDensity(),
                                 .night = an::ui::isNightMode()};
    change(choices);
    return an::ui::resolveDesignTokens(choices);
}

} // anonymous namespace

InterfaceSettingsPage::InterfaceSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString InterfaceSettingsPage::pageTitle() const
{
    return tr("Interface");
}

QIcon InterfaceSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("palette"));
}

QStringList InterfaceSettingsPage::searchKeywords() const
{
    return QStringList{
        tr("Interface"),     tr("Theme"),         tr("Dark"),         tr("Light"),        tr("Backdrop"),
        tr("Material"),      tr("Mica"),          tr("Acrylic"),      tr("Fluent"),       tr("Contrast"),
        tr("High Contrast"), tr("Soft Contrast"), tr("Accent"),       tr("Accent Color"), tr("Card Tint"),
        tr("Surface Tint"),  tr("GroupBox"),      tr("Card Surface"), tr("Blue"),         tr("Teal"),
        tr("Amber"),         tr("Plum"),          tr("Appearance"),   tr("Animations"),   tr("Animate"),
        tr("Zoom"),          tr("Mouse"),         tr("Wheel"),        tr("Sensitivity"),  tr("Ctrl"),
        tr("Control"),       tr("File Browser"),  tr("Native"),       tr("System"),       tr("Layout"),
        tr("Lock"),          tr("Window"),        tr("Size"),         tr("Preset"),       tr("Night"),
        tr("Red"),           tr("Density"),       tr("Compact"),      tr("Comfortable"),  tr("Tint Strength"),
        tr("Colour"),        tr("Menu Bar"),      tr("Alt"),          tr("Shell"),        tr("Restart")};
}

void InterfaceSettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === The appearance's choices, in their order ===
    setupObservatoryRows(this);

    // === The appearance's record: the combo boxes and the check box its pickers show, not shown themselves ===
    m_AppearanceGroup = new QGroupBox(tr("Appearance"), this);
    m_AppearanceGroup->setObjectName(QStringLiteral("anInterfaceSettingsAppearanceGroup"));
    auto* appearanceLayout = new QVBoxLayout(m_AppearanceGroup);
    appearanceLayout->setSpacing(12);

    auto* themeFormLayout = new QFormLayout();
    m_ThemeCombo = new QComboBox(m_AppearanceGroup);
    m_ThemeCombo->addItem(tr("Follow System"), "system");
    m_ThemeCombo->addItem(tr("Light"), "light");
    m_ThemeCombo->addItem(tr("Dark"), "dark");
    m_ThemeCombo->setToolTip(tr("Choose the application color theme"));
    themeFormLayout->addRow(tr("Theme:"), m_ThemeCombo);

    m_ContrastProfileCombo = new QComboBox(m_AppearanceGroup);
    m_ContrastProfileCombo->addItem(tr("Balanced (Recommended)"), "balanced");
    m_ContrastProfileCombo->addItem(tr("Soft"), "soft");
    m_ContrastProfileCombo->addItem(tr("High"), "high");
    m_ContrastProfileCombo->addItem(tr("OLED"), "oled");
    m_ContrastProfileCombo->setToolTip(tr("Controls dark-theme visual contrast.\n"
                                          "Soft reduces contrast and saturation, High increases both.\n"
                                          "OLED uses pure black backgrounds for AMOLED screens."));
    themeFormLayout->addRow(tr("Theme Contrast:"), m_ContrastProfileCombo);

    m_AccentFamilyCombo = new QComboBox(m_AppearanceGroup);
    m_AccentFamilyCombo->addItem(tr("Blue (Default)"), "blue");
    m_AccentFamilyCombo->addItem(tr("Teal"), "teal");
    m_AccentFamilyCombo->addItem(tr("Amber"), "amber");
    m_AccentFamilyCombo->addItem(tr("Amethyst"), "amethyst");
    m_AccentFamilyCombo->addItem(tr("Rose"), "rose");
    m_AccentFamilyCombo->setToolTip(tr("Selects the primary accent color family used across interactive controls."));
    themeFormLayout->addRow(tr("Accent Family:"), m_AccentFamilyCombo);

    m_SurfaceTintFamilyCombo = new QComboBox(m_AppearanceGroup);
    m_SurfaceTintFamilyCombo->addItem(tr("Neutral (Default)"), "neutral");
    m_SurfaceTintFamilyCombo->addItem(tr("Ocean"), "ocean");
    m_SurfaceTintFamilyCombo->addItem(tr("Mint"), "mint");
    m_SurfaceTintFamilyCombo->addItem(tr("Amethyst"), "amethyst");
    m_SurfaceTintFamilyCombo->addItem(tr("Rose"), "rose");
    m_SurfaceTintFamilyCombo->addItem(tr("Amber"), "amber");
    m_SurfaceTintFamilyCombo->setToolTip(tr("Applies a subtle color tint to card/groupbox surfaces."));
    themeFormLayout->addRow(tr("Card Surface Tint:"), m_SurfaceTintFamilyCombo);
    appearanceLayout->addLayout(themeFormLayout);

    m_AnimationsEnabledCheck = new QCheckBox(tr("Enable UI animations"), m_AppearanceGroup);
    m_AnimationsEnabledCheck->setToolTip(tr("Enables subtle animations such as card hover highlights and collapsible "
                                            "section transitions. Disable for accessibility or performance."));
    appearanceLayout->addWidget(m_AnimationsEnabledCheck);

    // === Image Viewing Section ===
    m_ViewingGroup = new QGroupBox(tr("Image Viewing"), this);
    auto* viewingLayout = new QVBoxLayout(m_ViewingGroup);
    viewingLayout->setSpacing(12);

    // Zoom sensitivity slider
    auto* sensitivityLayout = new QHBoxLayout();
    m_SlowLabel = new QLabel(tr("Slow"), m_ViewingGroup);
    applySecondaryTextStyle(m_SlowLabel);
    sensitivityLayout->addWidget(m_SlowLabel);

    m_ZoomSensitivitySlider = new QSlider(Qt::Horizontal, m_ViewingGroup);
    m_ZoomSensitivitySlider->setRange(1, 100);
    m_ZoomSensitivitySlider->setTickPosition(QSlider::TicksBelow);
    m_ZoomSensitivitySlider->setTickInterval(10);
    m_ZoomSensitivitySlider->setToolTip(tr("Mouse wheel zoom speed"));
    sensitivityLayout->addWidget(m_ZoomSensitivitySlider, 1);

    m_FastLabel = new QLabel(tr("Fast"), m_ViewingGroup);
    applySecondaryTextStyle(m_FastLabel);
    sensitivityLayout->addWidget(m_FastLabel);

    m_ZoomSensitivityLabel = new QLabel(m_ViewingGroup);
    m_ZoomSensitivityLabel->setMinimumWidth(40);
    m_ZoomSensitivityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sensitivityLayout->addWidget(m_ZoomSensitivityLabel);

    auto* sensitivityFormLayout = new QFormLayout();
    sensitivityFormLayout->addRow(tr("Mouse Wheel Zoom Sensitivity:"), sensitivityLayout);
    viewingLayout->addLayout(sensitivityFormLayout);

    // Zoom requires Ctrl key checkbox
    m_ZoomRequiresCtrlCheck = new QCheckBox(tr("Hold Ctrl key to zoom with mouse wheel"), m_ViewingGroup);
    m_ZoomRequiresCtrlCheck->setToolTip(tr("When enabled, zooming with the mouse wheel requires holding the Ctrl key. "
                                           "When disabled, the mouse wheel always zooms."));
    viewingLayout->addWidget(m_ZoomRequiresCtrlCheck);

    // === File Browser Section ===
    m_FileBrowserGroup = new QGroupBox(tr("File Browser"), this);
    auto* fileBrowserLayout = new QVBoxLayout(m_FileBrowserGroup);
    fileBrowserLayout->setSpacing(8);

    m_UseSystemFileBrowserCheck = new QCheckBox(tr("Use system file browser"), m_FileBrowserGroup);
#ifdef Q_OS_LINUX
    m_UseSystemFileBrowserCheck->setToolTip(
        tr("Use the operating system's native file dialog instead of the built-in one.\n\n"
           "Note: Some system file browsers (e.g. KDE/GTK dialogs) may have "
           "issues navigating to hidden folders (folders starting with a dot)."));
#else
    m_UseSystemFileBrowserCheck->setToolTip(
        tr("Use the operating system's native file dialog instead of the built-in one."));
#endif
    fileBrowserLayout->addWidget(m_UseSystemFileBrowserCheck);

    // === Window Layout Section ===
    m_LayoutGroup = new QGroupBox(tr("Window Layout"), this);
    auto* layoutLayout = new QVBoxLayout(m_LayoutGroup);
    layoutLayout->setSpacing(8);

    m_LockLayoutByDefaultCheck = new QCheckBox(tr("Lock dock layout by default"), m_LayoutGroup);
    m_LockLayoutByDefaultCheck->setToolTip(tr("Prevent accidental dock panel movement when starting the application"));
    layoutLayout->addWidget(m_LockLayoutByDefaultCheck);

    m_RememberWindowSizeCheck = new QCheckBox(tr("Remember window size and position"), m_LayoutGroup);
    m_RememberWindowSizeCheck->setToolTip(tr("Restore the window geometry from the previous session"));
    layoutLayout->addWidget(m_RememberWindowSizeCheck);

    auto* layoutNote = createInfoLabel(tr("Window layout is automatically saved when closing the application."));
    layoutLayout->addWidget(layoutNote);

    // === Shell Section: shown when the application can make more than one shell ===
    m_ShellGroup = new QGroupBox(tr("Shell"), this);
    m_ShellGroup->setObjectName(QStringLiteral("anInterfaceSettingsShellGroup"));
    auto* shellLayout = new QFormLayout(m_ShellGroup);
    m_ShellCombo = new QComboBox(m_ShellGroup);
    m_ShellCombo->setObjectName(QStringLiteral("anInterfaceSettingsShellCombo"));
    shellLayout->addRow(tr("Shell:"), m_ShellCombo);
    shellLayout->addRow(createInfoLabel(tr("Takes effect when AfterNight restarts.")));
    m_ShellGroup->setVisible(false);

    // The groups in their order: the appearance's choices first and the Shell row after them, above the image viewing,
    // the file browser and the window layout; the appearance's record is not shown.
    for (QWidget* group : {static_cast<QWidget*>(m_ObservatoryGroup), static_cast<QWidget*>(m_ShellGroup),
                           static_cast<QWidget*>(m_AppearanceGroup), static_cast<QWidget*>(m_ViewingGroup),
                           static_cast<QWidget*>(m_FileBrowserGroup), static_cast<QWidget*>(m_LayoutGroup)})
    {
        mainLayout->addWidget(group);
    }
    m_AppearanceGroup->setVisible(false);

    // Add stretch to push content to top
    mainLayout->addStretch(1);

    // Initialize sensitivity label
    updateSensitivityLabel();
}

void InterfaceSettingsPage::setupObservatoryRows(QWidget* parent)
{
    const an::ui::SpacingTokens& spacing = an::ui::tokens().spacing;
    m_ObservatoryGroup = new QGroupBox(tr("Colours and Size"), parent);
    m_ObservatoryGroup->setObjectName(QStringLiteral("anInterfaceSettingsObservatoryGroup"));
    m_ObservatoryForm = new QFormLayout(m_ObservatoryGroup);
    QFormLayout* const form = m_ObservatoryForm;

    // A row of pickers side by side, wrapped where the page is narrow.
    const auto flowRow = [this, &spacing]() {
        auto* const row = new QWidget(m_ObservatoryGroup);
        new FlowLayout(row, 0, spacing.sm, spacing.sm);
        return row;
    };

    // A tile per preset, and Custom, shown when the parts chosen are no preset's.
    QWidget* const tiles = flowRow();
    m_PresetButtons = new QButtonGroup(this);
    m_PresetButtons->setExclusive(true);
    const auto presets = presetNames();
    for (std::size_t index = 0; index < presets.size(); ++index)
    {
        const auto& [preset, name] = presets[index];
        auto* tile = new an::ui::ThemePresetTile(preset, name, tiles);
        tile->setObjectName(QStringLiteral("anInterfaceSettingsPreset_%1").arg(an::ui::themePresetId(preset)));
        m_PresetButtons->addButton(tile, static_cast<int>(preset));
        tiles->layout()->addWidget(tile);
        m_PresetTiles[index] = tile;
    }
    m_PresetTiles[static_cast<std::size_t>(an::ui::ThemePreset::Custom)]->setVisible(false);
    form->addRow(tr("Colour preset:"), tiles);

    // The theme, as tiles: the system's choice, light, dark.
    QWidget* const themes = flowRow();
    m_ThemeTiles = new QButtonGroup(this);
    m_ThemeTiles->setExclusive(true);
    using Mode = an::ui::ThemeModeTile::Mode;
    const std::array<std::pair<Mode, QString>, kThemeIds.size()> themeTiles{
        {{Mode::System, tr("Follow System")}, {Mode::Light, tr("Light")}, {Mode::Dark, tr("Dark")}}};
    for (std::size_t index = 0; index < themeTiles.size(); ++index)
    {
        auto* const tile = new an::ui::ThemeModeTile(themeTiles[index].first, themeTiles[index].second, themes);
        tile->setObjectName(QStringLiteral("anInterfaceSettingsTheme_%1").arg(QLatin1String(kThemeIds[index])));
        m_ThemeTiles->addButton(tile, static_cast<int>(index));
        themes->layout()->addWidget(tile);
    }
    form->addRow(tr("Theme:"), themes);

    // A row of segments, one of which is chosen, each segment named for its setting's value.
    const auto segments = [this](const QString& name, const QStringList& texts, const QStringList& ids) {
        auto* const control = new an::ui::AnSegmentedControl(texts, m_ObservatoryGroup);
        control->setObjectName(QStringLiteral("anInterfaceSettings") + name);
        control->setControlHeight(an::ui::AnSegmentedControl::ControlHeight::Small);
        for (int index = 0; index < control->count(); ++index)
        {
            control->segmentButton(index)->setObjectName(
                QStringLiteral("anInterfaceSettings%1_%2").arg(name, ids[index]));
        }
        return control;
    };
    m_ContrastSegments =
        segments(QStringLiteral("Contrast"), {tr("Soft"), tr("Balanced"), tr("High"), tr("OLED")},
                 {QStringLiteral("soft"), QStringLiteral("balanced"), QStringLiteral("high"), QStringLiteral("oled")});
    m_ContrastSegments->setAccessibleName(tr("Contrast"));
    m_ContrastSegments->setToolTip(tr("Soft reduces contrast and saturation, High increases both; OLED, with the dark "
                                      "theme, puts pure black behind everything."));
    form->addRow(tr("Contrast:"), m_ContrastSegments);

    // The accents as discs and the surface tints as squares, each its colour in the theme in force.
    QWidget* const accents = flowRow();
    m_AccentSwatches = new QButtonGroup(this);
    m_AccentSwatches->setExclusive(true);
    const std::array<QString, kAccents.size()> accentNames{tr("Blue"), tr("Teal"), tr("Amber"), tr("Amethyst"),
                                                           tr("Rose")};
    for (std::size_t index = 0; index < kAccents.size(); ++index)
    {
        const an::ui::ThemeAccentFamily family = kAccents[index];
        auto* const swatch = new an::ui::ThemeSwatch(
            an::ui::ThemeSwatch::Shape::Disc, accentNames[index],
            [family]() {
                return tokensWith([family](an::ui::ThemeChoices& choices) { choices.accent = family; })
                    .colors.accentPrimary;
            },
            accents);
        swatch->setObjectName(QStringLiteral("anInterfaceSettingsAccent_%1").arg(an::ui::themeAccentFamilyId(family)));
        m_AccentSwatches->addButton(swatch, static_cast<int>(index));
        accents->layout()->addWidget(swatch);
    }
    form->addRow(tr("Accent:"), accents);

    QWidget* const tints = flowRow();
    m_TintSwatches = new QButtonGroup(this);
    m_TintSwatches->setExclusive(true);
    const std::array<QString, kTints.size()> tintNames{tr("Neutral"),  tr("Ocean"), tr("Mint"),
                                                       tr("Amethyst"), tr("Rose"),  tr("Amber")};
    for (std::size_t index = 0; index < kTints.size(); ++index)
    {
        const an::ui::ThemeSurfaceTintFamily family = kTints[index];
        auto* const swatch = new an::ui::ThemeSwatch(
            an::ui::ThemeSwatch::Shape::Square, tintNames[index],
            [family]() {
                return tokensWith([family](an::ui::ThemeChoices& choices) {
                           choices.tint = family;
                           choices.tintStrength = an::ui::ThemeTintStrength::Strong;
                       })
                    .layers.surface3;
            },
            tints);
        swatch->setObjectName(
            QStringLiteral("anInterfaceSettingsTint_%1").arg(an::ui::themeSurfaceTintFamilyId(family)));
        m_TintSwatches->addButton(swatch, static_cast<int>(index));
        tints->layout()->addWidget(swatch);
    }
    form->addRow(tr("Surface tint:"), tints);

    m_TintStrengthSegments = segments(
        QStringLiteral("TintStrength"), {tr("Off"), tr("Subtle"), tr("Balanced"), tr("Strong")},
        {QStringLiteral("off"), QStringLiteral("subtle"), QStringLiteral("balanced"), QStringLiteral("strong")});
    m_TintStrengthSegments->setAccessibleName(tr("Tint strength"));
    form->addRow(tr("Tint strength:"), m_TintStrengthSegments);
    m_DensitySegments = segments(QStringLiteral("Density"), {tr("Compact"), tr("Comfortable")},
                                 {QStringLiteral("compact"), QStringLiteral("comfortable")});
    m_DensitySegments->setAccessibleName(tr("Density"));
    form->addRow(tr("Density:"), m_DensitySegments);

    m_NightSwitch = new an::ui::AnSwitch(tr("Red light for dark adaptation"), m_ObservatoryGroup);
    m_NightSwitch->setObjectName(QStringLiteral("anInterfaceSettingsNight"));
    m_NightSwitch->setToolTip(tr("Shows everything in red, pictures included, so the eyes stay adapted to the dark "
                                 "(Ctrl+Shift+N). It is not kept: AfterNight starts out of it unless asked to start in "
                                 "it."));
    form->addRow(tr("Night:"), m_NightSwitch);
    m_StartInNightSwitch = new an::ui::AnSwitch(tr("Start in Night"), m_ObservatoryGroup);
    m_StartInNightSwitch->setObjectName(QStringLiteral("anInterfaceSettingsStartInNight"));
    form->addRow(QString(), m_StartInNightSwitch);

    m_AnimationsSwitch = new an::ui::AnSwitch(tr("Enable UI animations"), m_ObservatoryGroup);
    m_AnimationsSwitch->setObjectName(QStringLiteral("anInterfaceSettingsAnimations"));
    m_AnimationsSwitch->setToolTip(tr("Enables subtle animations such as card hover highlights and collapsible section "
                                      "transitions. Disable for accessibility or performance."));
    form->addRow(tr("Animations:"), m_AnimationsSwitch);

    // Offered only where the shell shows its menu bar on Alt (setMenuBarKeeper).
    m_MenuBarSwitch = new an::ui::AnSwitch(tr("Always show the menu bar"), m_ObservatoryGroup);
    m_MenuBarSwitch->setObjectName(QStringLiteral("anInterfaceSettingsAlwaysShowMenuBar"));
    m_MenuBarSwitch->setToolTip(tr("Keeps the menu bar shown; otherwise it shows when Alt is pressed and released."));
    form->addRow(tr("Menu bar:"), m_MenuBarSwitch);
    form->setRowVisible(m_MenuBarSwitch, false);

    m_ObservatoryGroup->setVisible(true);
}

an::ui::ThemeTintStrength InterfaceSettingsPage::chosenTintStrength() const
{
    const int index = m_TintStrengthSegments->currentIndex();
    return index < 0 ? an::ui::ThemeTintStrength::Balanced : kStrengths[static_cast<std::size_t>(index)];
}

an::ui::ThemeDensity InterfaceSettingsPage::chosenDensity() const
{
    const int index = m_DensitySegments->currentIndex();
    return index < 0 ? an::ui::ThemeDensity::Compact : kDensities[static_cast<std::size_t>(index)];
}

an::ui::ThemePreset InterfaceSettingsPage::chosenPreset() const
{
    return an::ui::themePresetOf(
        an::ui::resolveThemeAccentFamily(m_AccentFamilyCombo->currentData().toString()),
        an::ui::resolveThemeSurfaceTintFamily(m_SurfaceTintFamilyCombo->currentData().toString()),
        chosenTintStrength());
}

void InterfaceSettingsPage::showTintStrength(const an::ui::ThemeTintStrength strength)
{
    m_TintStrengthSegments->setCurrentIndex(indexIn(kStrengths, strength));
}

void InterfaceSettingsPage::showDensity(const an::ui::ThemeDensity density)
{
    m_DensitySegments->setCurrentIndex(indexIn(kDensities, density));
}

void InterfaceSettingsPage::showPreset()
{
    const Showing showing(m_Showing);
    const an::ui::ThemePreset preset = chosenPreset();
    m_PresetTiles[static_cast<std::size_t>(an::ui::ThemePreset::Custom)]->setVisible(preset ==
                                                                                     an::ui::ThemePreset::Custom);
    if (QAbstractButton* tile = m_PresetButtons->button(static_cast<int>(preset)))
    {
        tile->setChecked(true);
    }
    for (an::ui::ThemePresetTile* tile : m_PresetTiles)
    {
        tile->update();
    }
}

void InterfaceSettingsPage::showAppearance()
{
    if (m_ThemeCombo == nullptr || m_ContrastProfileCombo == nullptr || m_AccentFamilyCombo == nullptr ||
        m_SurfaceTintFamilyCombo == nullptr || m_AnimationsEnabledCheck == nullptr)
    {
        return;
    }
    const Showing showing(m_Showing);
    const auto check = [](QButtonGroup* group, const int id) {
        if (QAbstractButton* const button = group->button(id))
        {
            button->setChecked(true);
        }
    };
    check(m_ThemeTiles, indexOfId(kThemeIds, m_ThemeCombo->currentData().toString()));
    m_ContrastSegments->setCurrentIndex(indexOfId(kContrastIds, m_ContrastProfileCombo->currentData().toString()));
    check(m_AccentSwatches,
          indexIn(kAccents, an::ui::resolveThemeAccentFamily(m_AccentFamilyCombo->currentData().toString())));
    check(m_TintSwatches,
          indexIn(kTints, an::ui::resolveThemeSurfaceTintFamily(m_SurfaceTintFamilyCombo->currentData().toString())));
    m_AnimationsSwitch->setChecked(m_AnimationsEnabledCheck->isChecked());
    // The tiles and the swatches show the parts in force, which a choice may have changed.
    for (QButtonGroup* group : {m_ThemeTiles, m_AccentSwatches, m_TintSwatches})
    {
        for (QAbstractButton* button : group->buttons())
        {
            button->update();
        }
    }
}

void InterfaceSettingsPage::choosePreset(const an::ui::ThemePreset preset)
{
    const std::optional<an::ui::ThemePresetParts> parts = an::ui::themePresetParts(preset);
    if (!parts.has_value())
    {
        return;
    }
    const Showing showing(m_Showing);
    const int accentIndex = m_AccentFamilyCombo->findData(an::ui::themeAccentFamilyId(parts->accent));
    if (accentIndex >= 0)
    {
        m_AccentFamilyCombo->setCurrentIndex(accentIndex);
    }
    const int tintIndex = m_SurfaceTintFamilyCombo->findData(an::ui::themeSurfaceTintFamilyId(parts->tint));
    if (tintIndex >= 0)
    {
        m_SurfaceTintFamilyCombo->setCurrentIndex(tintIndex);
    }
    showTintStrength(parts->strength);
}

void InterfaceSettingsPage::previewAppearance()
{
    an::ui::setThemeContrastProfile(
        an::ui::resolveThemeContrastProfile(m_ContrastProfileCombo->currentData().toString()));
    an::ui::setThemeAccentFamily(an::ui::resolveThemeAccentFamily(m_AccentFamilyCombo->currentData().toString()));
    an::ui::setThemeSurfaceTintFamily(
        an::ui::resolveThemeSurfaceTintFamily(m_SurfaceTintFamilyCombo->currentData().toString()));
    an::ui::setThemeTintStrength(chosenTintStrength());
    an::ui::setThemeDensity(chosenDensity());
    an::ui::setNightMode(m_NightSwitch->isChecked());
    an::ui::AnimationUtils::setAnimationsEnabled(m_AnimationsEnabledCheck->isChecked());
    if (auto* runtime = currentInterfaceSettingsRuntimeService())
    {
        runtime->applyTheme(m_ThemeCombo->currentData().toString());
    }
    showAppearance();
}

void InterfaceSettingsPage::appearanceChanged()
{
    if (m_Showing)
    {
        return;
    }
    markAsModified();
    showPreset();
    previewAppearance();
}

void InterfaceSettingsPage::showMenuBarKept(const bool kept)
{
    {
        const Showing showing(m_Showing);
        m_MenuBarSwitch->setChecked(kept);
    }
    if (m_KeepMenuBarShown)
    {
        m_KeepMenuBarShown(kept);
    }
}

void InterfaceSettingsPage::rememberInForce()
{
    m_InForce = InForce{
        .contrast = an::ui::themeContrastProfile(),
        .accent = an::ui::themeAccentFamily(),
        .tint = an::ui::themeSurfaceTintFamily(),
        .strength = an::ui::themeTintStrength(),
        .density = an::ui::themeDensity(),
        .night = an::ui::nightModeSet(),
        .animations = an::ui::AnimationUtils::animationsEnabled(),
        .menuBarKept = m_MenuBarSwitch->isChecked(),
        .theme = m_Settings != nullptr
                     ? m_Settings->select(SettingsKeys::INTERFACE_THEME, QString(SettingsDefaults::INTERFACE_THEME))
                     : m_ThemeCombo->currentData().toString(),
    };
}

void InterfaceSettingsPage::connectSignals()
{
    trackChanges(m_ThemeCombo);
    trackChanges(m_ContrastProfileCombo);
    trackChanges(m_AccentFamilyCombo);

    // The pickers show what the combo boxes record, and every appearance row acts at once.
    for (QComboBox* combo : {m_ThemeCombo, m_ContrastProfileCombo, m_AccentFamilyCombo, m_SurfaceTintFamilyCombo})
    {
        connect(combo, &QComboBox::currentIndexChanged, this, [this]() {
            showAppearance();
            appearanceChanged();
        });
    }
    connect(m_PresetButtons, &QButtonGroup::idClicked, this, [this](const int id) {
        if (m_Showing)
        {
            return;
        }
        choosePreset(static_cast<an::ui::ThemePreset>(id));
        appearanceChanged();
    });
    // A picker chooses in the combo box it shows, which then acts as a change of its own.
    connect(m_ThemeTiles, &QButtonGroup::idClicked, this, [this](const int id) {
        if (!m_Showing && id >= 0 && id < static_cast<int>(kThemeIds.size()))
        {
            chooseIn(m_ThemeCombo, QLatin1String(kThemeIds[static_cast<std::size_t>(id)]));
        }
    });
    connect(m_ContrastSegments, &an::ui::AnSegmentedControl::currentIndexChanged, this, [this](const int index) {
        if (!m_Showing && index >= 0 && index < static_cast<int>(kContrastIds.size()))
        {
            chooseIn(m_ContrastProfileCombo, QLatin1String(kContrastIds[static_cast<std::size_t>(index)]));
        }
    });
    connect(m_AccentSwatches, &QButtonGroup::idClicked, this, [this](const int id) {
        if (!m_Showing && id >= 0 && id < static_cast<int>(kAccents.size()))
        {
            chooseIn(m_AccentFamilyCombo, an::ui::themeAccentFamilyId(kAccents[static_cast<std::size_t>(id)]));
        }
    });
    connect(m_TintSwatches, &QButtonGroup::idClicked, this, [this](const int id) {
        if (!m_Showing && id >= 0 && id < static_cast<int>(kTints.size()))
        {
            chooseIn(m_SurfaceTintFamilyCombo, an::ui::themeSurfaceTintFamilyId(kTints[static_cast<std::size_t>(id)]));
        }
    });
    connect(m_TintStrengthSegments, &an::ui::AnSegmentedControl::currentIndexChanged, this,
            [this](int) { appearanceChanged(); });
    connect(m_DensitySegments, &an::ui::AnSegmentedControl::currentIndexChanged, this,
            [this](int) { appearanceChanged(); });
    connect(m_NightSwitch, &QAbstractButton::toggled, this, [this](bool) { appearanceChanged(); });
    trackChanges(m_StartInNightSwitch);
    connect(m_AnimationsSwitch, &QAbstractButton::toggled, this, [this](const bool on) {
        if (!m_Showing)
        {
            m_AnimationsEnabledCheck->setChecked(on);
        }
    });
    connect(m_AnimationsEnabledCheck, &QCheckBox::toggled, this, [this](bool) {
        showAppearance();
        appearanceChanged();
    });
    connect(m_MenuBarSwitch, &QAbstractButton::toggled, this, [this](const bool kept) {
        if (m_Showing)
        {
            return;
        }
        markAsModified();
        if (m_KeepMenuBarShown)
        {
            m_KeepMenuBarShown(kept);
        }
    });

    // OLED contrast is a dark-only concept. Disable the combo item when
    // the user explicitly selects the Light theme, and fall back to
    // Balanced if OLED was previously selected.
    connect(m_ThemeCombo, &QComboBox::currentIndexChanged, this, &InterfaceSettingsPage::syncOledAvailability);
    syncOledAvailability();
    trackChanges(m_SurfaceTintFamilyCombo);
    trackChanges(m_AnimationsEnabledCheck);

    connect(m_ZoomSensitivitySlider, &QSlider::valueChanged, this, [this]() {
        updateSensitivityLabel();
        markAsModified();
    });

    trackChanges(m_ZoomRequiresCtrlCheck);
    trackChanges(m_UseSystemFileBrowserCheck);
    trackChanges(m_LockLayoutByDefaultCheck);
    trackChanges(m_RememberWindowSizeCheck);
    trackChanges(m_ShellCombo);
    showAppearance();
}

void InterfaceSettingsPage::setAvailableShells(std::vector<an::ui::ShellOption> shells)
{
    m_Shells = std::move(shells);
    {
        const QSignalBlocker blocker(m_ShellCombo);
        m_ShellCombo->clear();
        for (const an::ui::ShellOption& shell : m_Shells)
        {
            m_ShellCombo->addItem(shell.displayName, shell.id);
        }
    }
    m_ShellGroup->setVisible(m_Shells.size() > 1);
    if (m_Settings != nullptr)
    {
        selectShell(
            m_Settings->select(SettingsKeys::INTERFACE_SHELL, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)));
    }
}

void InterfaceSettingsPage::setMenuBarKeeper(std::function<void(bool)> keepShown)
{
    m_KeepMenuBarShown = std::move(keepShown);
    m_ObservatoryForm->setRowVisible(m_MenuBarSwitch, static_cast<bool>(m_KeepMenuBarShown));
    if (m_Settings != nullptr)
    {
        const Showing showing(m_Showing);
        m_MenuBarSwitch->setChecked(m_Settings->select(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR,
                                                       SettingsDefaults::INTERFACE_ALWAYS_SHOW_MENU_BAR ? "1" : "0") ==
                                    QStringLiteral("1"));
    }
}

void InterfaceSettingsPage::selectShell(const QString& id)
{
    int index = m_ShellCombo->findData(id);
    if (index < 0)
    {
        index = m_ShellCombo->findData(QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL));
    }
    if (index >= 0)
    {
        m_ShellCombo->setCurrentIndex(index);
    }
}

void InterfaceSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();
    const Showing showing(m_Showing);

    if (!m_Settings)
    {
        rememberInForce();
        resetToDefaults();
        return;
    }

    // Theme
    QString theme = m_Settings->select(SettingsKeys::INTERFACE_THEME, QString(SettingsDefaults::INTERFACE_THEME));
    int themeIndex = m_ThemeCombo->findData(theme);
    m_ThemeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    QString contrastProfile = m_Settings->select(SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE,
                                                 QString(SettingsDefaults::INTERFACE_THEME_CONTRAST_PROFILE));
    int contrastIndex = m_ContrastProfileCombo->findData(contrastProfile);
    m_ContrastProfileCombo->setCurrentIndex(contrastIndex >= 0 ? contrastIndex : 0);

    QString accentFamily = m_Settings->select(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY,
                                              QString(SettingsDefaults::INTERFACE_THEME_ACCENT_FAMILY));
    int accentFamilyIndex = m_AccentFamilyCombo->findData(accentFamily);
    m_AccentFamilyCombo->setCurrentIndex(accentFamilyIndex >= 0 ? accentFamilyIndex : 0);

    const QString surfaceTintFamily =
        m_Settings->select(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY,
                           QString(SettingsDefaults::INTERFACE_THEME_SURFACE_TINT_FAMILY));

    int surfaceTintIndex = m_SurfaceTintFamilyCombo->findData(surfaceTintFamily);
    m_SurfaceTintFamilyCombo->setCurrentIndex(surfaceTintIndex >= 0 ? surfaceTintIndex : 0);

    // Zoom sensitivity
    QString sensitivityStr = m_Settings->select(SettingsKeys::INTERFACE_ZOOM_SENSITIVITY,
                                                QString::number(SettingsDefaults::INTERFACE_ZOOM_SENSITIVITY));
    int sensitivity = sensitivityStr.toInt();
    m_ZoomSensitivitySlider->setValue(qBound(1, sensitivity, 100));

    // Zoom requires Ctrl key
    QString zoomRequiresCtrlStr = m_Settings->select(SettingsKeys::INTERFACE_ZOOM_REQUIRES_CTRL,
                                                     SettingsDefaults::INTERFACE_ZOOM_REQUIRES_CTRL ? "1" : "0");
    m_ZoomRequiresCtrlCheck->setChecked(zoomRequiresCtrlStr == "1");

    // Animations enabled
    QString animationsStr = m_Settings->select(SettingsKeys::INTERFACE_ANIMATIONS_ENABLED,
                                               SettingsDefaults::INTERFACE_ANIMATIONS_ENABLED ? "1" : "0");
    m_AnimationsEnabledCheck->setChecked(animationsStr == "1");

    // Use system file browser
    QString useSystemFileBrowserStr =
        m_Settings->select(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER,
                           SettingsDefaults::INTERFACE_USE_SYSTEM_FILE_BROWSER ? "1" : "0");
    m_UseSystemFileBrowserCheck->setChecked(useSystemFileBrowserStr == "1");

    // Lock layout by default
    QString lockLayoutStr = m_Settings->select(SettingsKeys::INTERFACE_LOCK_LAYOUT_BY_DEFAULT,
                                               SettingsDefaults::INTERFACE_LOCK_LAYOUT_BY_DEFAULT ? "1" : "0");
    m_LockLayoutByDefaultCheck->setChecked(lockLayoutStr == "1");

    selectShell(
        m_Settings->select(SettingsKeys::INTERFACE_SHELL, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL)));

    // Remember window size
    QString rememberSizeStr = m_Settings->select(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE,
                                                 SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE ? "1" : "0");
    m_RememberWindowSizeCheck->setChecked(rememberSizeStr == "1");

    // The strength, the density and Night; Night is never stored, so the switch shows whether it is set.
    showTintStrength(an::ui::resolveThemeTintStrength(m_Settings->select(
        SettingsKeys::INTERFACE_THEME_TINT_STRENGTH, QString(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH))));
    showDensity(an::ui::resolveThemeDensity(
        m_Settings->select(SettingsKeys::INTERFACE_DENSITY, QString(SettingsDefaults::INTERFACE_DENSITY))));
    m_StartInNightSwitch->setChecked(m_Settings->select(SettingsKeys::INTERFACE_START_IN_NIGHT,
                                                        SettingsDefaults::INTERFACE_START_IN_NIGHT ? "1" : "0") ==
                                     QStringLiteral("1"));
    m_NightSwitch->setChecked(an::ui::nightModeSet());
    m_MenuBarSwitch->setChecked(m_Settings->select(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR,
                                                   SettingsDefaults::INTERFACE_ALWAYS_SHOW_MENU_BAR ? "1" : "0") ==
                                QStringLiteral("1"));
    showPreset();

    updateSensitivityLabel();
    clearModified();
    syncOledAvailability();
    showAppearance();
    rememberInForce();
}

void InterfaceSettingsPage::syncOledAvailability()
{
    const bool isLightTheme = m_ThemeCombo->currentData().toString() == QStringLiteral("light");
    const int oledIndex = m_ContrastProfileCombo->findData(QStringLiteral("oled"));
    if (oledIndex < 0)
    {
        return;
    }

    auto* model = qobject_cast<QStandardItemModel*>(m_ContrastProfileCombo->model());
    if (model)
    {
        if (auto* item = model->item(oledIndex))
        {
            item->setEnabled(!isLightTheme);
        }
    }
    if (QAbstractButton* const oled =
            m_ContrastSegments->segmentButton(indexOfId(kContrastIds, QStringLiteral("oled"))))
    {
        oled->setEnabled(!isLightTheme);
        m_ContrastSegments->update();
    }

    if (isLightTheme && m_ContrastProfileCombo->currentIndex() == oledIndex)
    {
        const int balancedIndex = m_ContrastProfileCombo->findData(QStringLiteral("balanced"));
        m_ContrastProfileCombo->setCurrentIndex(balancedIndex >= 0 ? balancedIndex : 0);
    }
}

void InterfaceSettingsPage::saveSettings()
{
    if (!m_Settings)
    {
        return;
    }

    bool allSaved = true;

    const auto upsertSetting = [this, &allSaved](const char* key, const QString& value) {
        if (!m_Settings->upsert(key, value))
        {
            allSaved = false;
            LOG_ERROR("InterfaceSettingsPage: failed to persist setting key='{}'", key);
        }
    };

    // Theme - save and apply immediately
    QString newTheme = m_ThemeCombo->currentData().toString();
    upsertSetting(SettingsKeys::INTERFACE_THEME, newTheme);

    QString newContrastProfile = m_ContrastProfileCombo->currentData().toString();
    upsertSetting(SettingsKeys::INTERFACE_THEME_CONTRAST_PROFILE, newContrastProfile);

    QString newAccentFamily = m_AccentFamilyCombo->currentData().toString();
    upsertSetting(SettingsKeys::INTERFACE_THEME_ACCENT_FAMILY, newAccentFamily);

    QString newSurfaceTintFamily = m_SurfaceTintFamilyCombo->currentData().toString();
    upsertSetting(SettingsKeys::INTERFACE_THEME_SURFACE_TINT_FAMILY, newSurfaceTintFamily);

    // The strength, the density, the preset and starting in Night; Night itself is never kept.
    upsertSetting(SettingsKeys::INTERFACE_THEME_TINT_STRENGTH, an::ui::themeTintStrengthId(chosenTintStrength()));
    upsertSetting(SettingsKeys::INTERFACE_DENSITY, an::ui::themeDensityId(chosenDensity()));
    upsertSetting(SettingsKeys::INTERFACE_THEME_PRESET, an::ui::themePresetId(chosenPreset()));
    upsertSetting(SettingsKeys::INTERFACE_START_IN_NIGHT, m_StartInNightSwitch->isChecked() ? "1" : "0");

    // Apply the theme at runtime only if the setting was persisted.
    if (allSaved)
    {
        an::ui::setThemeContrastProfile(an::ui::resolveThemeContrastProfile(newContrastProfile));
        an::ui::setThemeAccentFamily(an::ui::resolveThemeAccentFamily(newAccentFamily));
        an::ui::setThemeSurfaceTintFamily(an::ui::resolveThemeSurfaceTintFamily(newSurfaceTintFamily));
        an::ui::setThemeTintStrength(chosenTintStrength());
        an::ui::setThemeDensity(chosenDensity());
        an::ui::setNightMode(m_NightSwitch->isChecked());

        if (auto* runtime = currentInterfaceSettingsRuntimeService())
        {
            runtime->applyTheme(newTheme);
        }
    }

    // Zoom sensitivity
    upsertSetting(SettingsKeys::INTERFACE_ZOOM_SENSITIVITY, QString::number(m_ZoomSensitivitySlider->value()));

    // Zoom requires Ctrl key
    upsertSetting(SettingsKeys::INTERFACE_ZOOM_REQUIRES_CTRL, m_ZoomRequiresCtrlCheck->isChecked() ? "1" : "0");

    // Animations enabled — apply immediately so open components respond
    const bool animationsEnabled = m_AnimationsEnabledCheck->isChecked();
    upsertSetting(SettingsKeys::INTERFACE_ANIMATIONS_ENABLED, animationsEnabled ? "1" : "0");
    an::ui::AnimationUtils::setAnimationsEnabled(animationsEnabled);

    // Refresh cached zoom settings in all image view controls
    if (auto* runtime = currentInterfaceSettingsRuntimeService())
    {
        runtime->refreshAllZoomSettings();
    }

    // Use system file browser
    upsertSetting(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER,
                  m_UseSystemFileBrowserCheck->isChecked() ? "1" : "0");

    // Lock layout by default
    upsertSetting(SettingsKeys::INTERFACE_LOCK_LAYOUT_BY_DEFAULT, m_LockLayoutByDefaultCheck->isChecked() ? "1" : "0");

    // A shell is chosen only where there is a choice to make.
    if (m_Shells.size() > 1 && m_ShellCombo->currentIndex() >= 0)
    {
        upsertSetting(SettingsKeys::INTERFACE_SHELL, m_ShellCombo->currentData().toString());
    }

    // The menu bar is kept shown only where the shell shows it on Alt.
    if (m_KeepMenuBarShown)
    {
        const bool kept = m_MenuBarSwitch->isChecked();
        upsertSetting(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR, kept ? "1" : "0");
        m_KeepMenuBarShown(kept);
    }

    // Remember window size
    upsertSetting(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE, m_RememberWindowSizeCheck->isChecked() ? "1" : "0");

    if (allSaved)
    {
        clearModified();
        rememberInForce();
    }
}

void InterfaceSettingsPage::discardChanges()
{
    // Every appearance row acted at once: what was in force when the page last loaded or applied is put back.
    an::ui::setThemeContrastProfile(m_InForce.contrast);
    an::ui::setThemeAccentFamily(m_InForce.accent);
    an::ui::setThemeSurfaceTintFamily(m_InForce.tint);
    an::ui::setThemeTintStrength(m_InForce.strength);
    an::ui::setThemeDensity(m_InForce.density);
    an::ui::setNightMode(m_InForce.night);
    an::ui::AnimationUtils::setAnimationsEnabled(m_InForce.animations);
    if (m_KeepMenuBarShown)
    {
        m_KeepMenuBarShown(m_InForce.menuBarKept);
    }
    if (auto* runtime = currentInterfaceSettingsRuntimeService())
    {
        runtime->applyTheme(m_InForce.theme);
    }
    loadSettings();
}

void InterfaceSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();
    const Showing showing(m_Showing);

    // Theme
    int themeIndex = m_ThemeCombo->findData(QString(SettingsDefaults::INTERFACE_THEME));
    m_ThemeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    int contrastIndex = m_ContrastProfileCombo->findData(QString(SettingsDefaults::INTERFACE_THEME_CONTRAST_PROFILE));
    m_ContrastProfileCombo->setCurrentIndex(contrastIndex >= 0 ? contrastIndex : 0);

    int accentFamilyIndex = m_AccentFamilyCombo->findData(QString(SettingsDefaults::INTERFACE_THEME_ACCENT_FAMILY));
    m_AccentFamilyCombo->setCurrentIndex(accentFamilyIndex >= 0 ? accentFamilyIndex : 0);

    int surfaceTintFamilyIndex =
        m_SurfaceTintFamilyCombo->findData(QString(SettingsDefaults::INTERFACE_THEME_SURFACE_TINT_FAMILY));
    m_SurfaceTintFamilyCombo->setCurrentIndex(surfaceTintFamilyIndex >= 0 ? surfaceTintFamilyIndex : 0);

    // Zoom sensitivity
    m_ZoomSensitivitySlider->setValue(SettingsDefaults::INTERFACE_ZOOM_SENSITIVITY);

    // Zoom requires Ctrl key
    m_ZoomRequiresCtrlCheck->setChecked(SettingsDefaults::INTERFACE_ZOOM_REQUIRES_CTRL);

    // Animations enabled
    m_AnimationsEnabledCheck->setChecked(SettingsDefaults::INTERFACE_ANIMATIONS_ENABLED);

    // File browser
    m_UseSystemFileBrowserCheck->setChecked(SettingsDefaults::INTERFACE_USE_SYSTEM_FILE_BROWSER);

    // Layout options
    m_LockLayoutByDefaultCheck->setChecked(SettingsDefaults::INTERFACE_LOCK_LAYOUT_BY_DEFAULT);
    selectShell(QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL));
    m_RememberWindowSizeCheck->setChecked(SettingsDefaults::INTERFACE_REMEMBER_WINDOW_SIZE);

    // The strength, the density and Night; Night is no setting and is left as it is.
    showTintStrength(an::ui::resolveThemeTintStrength(QString(SettingsDefaults::INTERFACE_THEME_TINT_STRENGTH)));
    showDensity(an::ui::resolveThemeDensity(QString(SettingsDefaults::INTERFACE_DENSITY)));
    m_StartInNightSwitch->setChecked(SettingsDefaults::INTERFACE_START_IN_NIGHT);
    m_NightSwitch->setChecked(an::ui::nightModeSet());
    showMenuBarKept(SettingsDefaults::INTERFACE_ALWAYS_SHOW_MENU_BAR);
    showPreset();
    showAppearance();

    updateSensitivityLabel();
    markAsModified();
    // The defaults are shown at once, as any change is.
    previewAppearance();
}

ValidationResult InterfaceSettingsPage::validateSettings() const
{
    // No validation required - all settings have valid ranges enforced by controls
    return ValidationResult::valid();
}

void InterfaceSettingsPage::updateSensitivityLabel()
{
    m_ZoomSensitivityLabel->setText(QString("%1%").arg(m_ZoomSensitivitySlider->value()));
}

void InterfaceSettingsPage::refreshStyles()
{
    SettingsPage::refreshStyles();
    applySecondaryTextStyle(m_SlowLabel);
    applySecondaryTextStyle(m_FastLabel);
}
