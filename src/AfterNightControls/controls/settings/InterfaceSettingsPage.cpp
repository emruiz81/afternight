// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "InterfaceSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "../GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "app/ThemeManager.h"
#include "theme/AnimationUtils.h"

#include <AfterNightPlatform/Logging.h>

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QStandardItemModel>

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
        tr("Lock"),          tr("Window"),        tr("Size")};
}

void InterfaceSettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === Appearance Section ===
    auto* appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearanceLayout = new QVBoxLayout(appearanceGroup);
    appearanceLayout->setSpacing(12);

    auto* themeFormLayout = new QFormLayout();
    m_ThemeCombo = new QComboBox(appearanceGroup);
    m_ThemeCombo->addItem(tr("Follow System"), "system");
    m_ThemeCombo->addItem(tr("Light"), "light");
    m_ThemeCombo->addItem(tr("Dark"), "dark");
    m_ThemeCombo->setToolTip(tr("Choose the application color theme"));
    themeFormLayout->addRow(tr("Theme:"), m_ThemeCombo);

    m_ContrastProfileCombo = new QComboBox(appearanceGroup);
    m_ContrastProfileCombo->addItem(tr("Balanced (Recommended)"), "balanced");
    m_ContrastProfileCombo->addItem(tr("Soft"), "soft");
    m_ContrastProfileCombo->addItem(tr("High"), "high");
    m_ContrastProfileCombo->addItem(tr("OLED"), "oled");
    m_ContrastProfileCombo->setToolTip(tr("Controls dark-theme visual contrast.\n"
                                          "Soft reduces contrast and saturation, High increases both.\n"
                                          "OLED uses pure black backgrounds for AMOLED screens."));
    themeFormLayout->addRow(tr("Theme Contrast:"), m_ContrastProfileCombo);

    m_AccentFamilyCombo = new QComboBox(appearanceGroup);
    m_AccentFamilyCombo->addItem(tr("Blue (Default)"), "blue");
    m_AccentFamilyCombo->addItem(tr("Teal"), "teal");
    m_AccentFamilyCombo->addItem(tr("Amber"), "amber");
    m_AccentFamilyCombo->addItem(tr("Amethyst"), "amethyst");
    m_AccentFamilyCombo->addItem(tr("Rose"), "rose");
    m_AccentFamilyCombo->setToolTip(tr("Selects the primary accent color family used across interactive controls."));
    themeFormLayout->addRow(tr("Accent Family:"), m_AccentFamilyCombo);

    m_SurfaceTintFamilyCombo = new QComboBox(appearanceGroup);
    m_SurfaceTintFamilyCombo->addItem(tr("Neutral (Default)"), "neutral");
    m_SurfaceTintFamilyCombo->addItem(tr("Ocean"), "ocean");
    m_SurfaceTintFamilyCombo->addItem(tr("Mint"), "mint");
    m_SurfaceTintFamilyCombo->addItem(tr("Amethyst"), "amethyst");
    m_SurfaceTintFamilyCombo->addItem(tr("Rose"), "rose");
    m_SurfaceTintFamilyCombo->addItem(tr("Amber"), "amber");
    m_SurfaceTintFamilyCombo->setToolTip(tr("Applies a subtle color tint to card/groupbox surfaces."));
    themeFormLayout->addRow(tr("Card Surface Tint:"), m_SurfaceTintFamilyCombo);
    appearanceLayout->addLayout(themeFormLayout);

    m_AnimationsEnabledCheck = new QCheckBox(tr("Enable UI animations"), appearanceGroup);
    m_AnimationsEnabledCheck->setToolTip(tr("Enables subtle animations such as card hover highlights and collapsible "
                                            "section transitions. Disable for accessibility or performance."));
    appearanceLayout->addWidget(m_AnimationsEnabledCheck);

    mainLayout->addWidget(appearanceGroup);

    // === Image Viewing Section ===
    auto* viewingGroup = new QGroupBox(tr("Image Viewing"), this);
    auto* viewingLayout = new QVBoxLayout(viewingGroup);
    viewingLayout->setSpacing(12);

    // Zoom sensitivity slider
    auto* sensitivityLayout = new QHBoxLayout();
    m_SlowLabel = new QLabel(tr("Slow"), viewingGroup);
    applySecondaryTextStyle(m_SlowLabel);
    sensitivityLayout->addWidget(m_SlowLabel);

    m_ZoomSensitivitySlider = new QSlider(Qt::Horizontal, viewingGroup);
    m_ZoomSensitivitySlider->setRange(1, 100);
    m_ZoomSensitivitySlider->setTickPosition(QSlider::TicksBelow);
    m_ZoomSensitivitySlider->setTickInterval(10);
    m_ZoomSensitivitySlider->setToolTip(tr("Mouse wheel zoom speed"));
    sensitivityLayout->addWidget(m_ZoomSensitivitySlider, 1);

    m_FastLabel = new QLabel(tr("Fast"), viewingGroup);
    applySecondaryTextStyle(m_FastLabel);
    sensitivityLayout->addWidget(m_FastLabel);

    m_ZoomSensitivityLabel = new QLabel(viewingGroup);
    m_ZoomSensitivityLabel->setMinimumWidth(40);
    m_ZoomSensitivityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sensitivityLayout->addWidget(m_ZoomSensitivityLabel);

    auto* sensitivityFormLayout = new QFormLayout();
    sensitivityFormLayout->addRow(tr("Mouse Wheel Zoom Sensitivity:"), sensitivityLayout);
    viewingLayout->addLayout(sensitivityFormLayout);

    // Zoom requires Ctrl key checkbox
    m_ZoomRequiresCtrlCheck = new QCheckBox(tr("Hold Ctrl key to zoom with mouse wheel"), viewingGroup);
    m_ZoomRequiresCtrlCheck->setToolTip(tr("When enabled, zooming with the mouse wheel requires holding the Ctrl key. "
                                           "When disabled, the mouse wheel always zooms."));
    viewingLayout->addWidget(m_ZoomRequiresCtrlCheck);

    mainLayout->addWidget(viewingGroup);

    // === File Browser Section ===
    auto* fileBrowserGroup = new QGroupBox(tr("File Browser"), this);
    auto* fileBrowserLayout = new QVBoxLayout(fileBrowserGroup);
    fileBrowserLayout->setSpacing(8);

    m_UseSystemFileBrowserCheck = new QCheckBox(tr("Use system file browser"), fileBrowserGroup);
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

    mainLayout->addWidget(fileBrowserGroup);

    // === Window Layout Section ===
    auto* layoutGroup = new QGroupBox(tr("Window Layout"), this);
    auto* layoutLayout = new QVBoxLayout(layoutGroup);
    layoutLayout->setSpacing(8);

    m_LockLayoutByDefaultCheck = new QCheckBox(tr("Lock dock layout by default"), layoutGroup);
    m_LockLayoutByDefaultCheck->setToolTip(tr("Prevent accidental dock panel movement when starting the application"));
    layoutLayout->addWidget(m_LockLayoutByDefaultCheck);

    m_RememberWindowSizeCheck = new QCheckBox(tr("Remember window size and position"), layoutGroup);
    m_RememberWindowSizeCheck->setToolTip(tr("Restore the window geometry from the previous session"));
    layoutLayout->addWidget(m_RememberWindowSizeCheck);

    auto* layoutNote = createInfoLabel(tr("Window layout is automatically saved when closing the application."));
    layoutLayout->addWidget(layoutNote);

    mainLayout->addWidget(layoutGroup);

    // === Shell Section: shown when the application can make more than one shell ===
    m_ShellGroup = new QGroupBox(tr("Shell"), this);
    m_ShellGroup->setObjectName(QStringLiteral("anInterfaceSettingsShellGroup"));
    auto* shellLayout = new QFormLayout(m_ShellGroup);
    m_ShellCombo = new QComboBox(m_ShellGroup);
    m_ShellCombo->setObjectName(QStringLiteral("anInterfaceSettingsShellCombo"));
    shellLayout->addRow(tr("Shell:"), m_ShellCombo);
    shellLayout->addRow(createInfoLabel(tr("Takes effect when AfterNight restarts.")));
    m_ShellGroup->setVisible(false);
    mainLayout->addWidget(m_ShellGroup);

    // Add stretch to push content to top
    mainLayout->addStretch(1);

    // Initialize sensitivity label
    updateSensitivityLabel();
}

void InterfaceSettingsPage::connectSignals()
{
    trackChanges(m_ThemeCombo);
    trackChanges(m_ContrastProfileCombo);
    trackChanges(m_AccentFamilyCombo);

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

    if (!m_Settings)
    {
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

    updateSensitivityLabel();
    clearModified();
    syncOledAvailability();
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

    // Apply the theme at runtime only if the setting was persisted.
    if (allSaved)
    {
        an::ui::setThemeContrastProfile(an::ui::resolveThemeContrastProfile(newContrastProfile));
        an::ui::setThemeAccentFamily(an::ui::resolveThemeAccentFamily(newAccentFamily));
        an::ui::setThemeSurfaceTintFamily(an::ui::resolveThemeSurfaceTintFamily(newSurfaceTintFamily));

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

    // Remember window size
    upsertSetting(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE, m_RememberWindowSizeCheck->isChecked() ? "1" : "0");

    if (allSaved)
    {
        clearModified();
    }
}

void InterfaceSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();

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

    updateSensitivityLabel();
    markAsModified();
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
