// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RawImageSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "../GlobalSettingsPersistence.h"
#include "app/DemosaicTextMaps.h"
#include "app/FilePolicies.h"
#include "controls/IAppContext.h"

#include <AfterNightEngine/Core/Status.h>

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>

#include <utility>

W_OBJECT_IMPL(RawImageSettingsPage)

namespace
{

/// The white-balance modes the page offers, in the order it lists them.
QList<std::pair<an::ui::RawWhiteBalanceSetting, QString>> whiteBalanceModes()
{
    return {
        {an::ui::RawWhiteBalanceSetting::None, QObject::tr("None (No correction)")},
        {an::ui::RawWhiteBalanceSetting::Daylight, QObject::tr("Daylight")},
        {an::ui::RawWhiteBalanceSetting::CameraAsShot, QObject::tr("Camera As-Shot")},
        {an::ui::RawWhiteBalanceSetting::Automatic, QObject::tr("Auto White Balance")},
        {an::ui::RawWhiteBalanceSetting::Custom, QObject::tr("Custom")},
    };
}

/// The demosaic methods the page offers, by the key the setting stores: the engine's six. A stored method the table
/// does not hold reads as RCD, the setting's default.
QList<std::pair<QString, QString>> demosaicMethods()
{
    return {
        {QStringLiteral("rcd"), QObject::tr("RCD (Ratio Corrected Demosaicing) - Recommended")},
        {QStringLiteral("amaze"), QObject::tr("AMaZE (Aliasing Minimization and Zipper Elimination)")},
        {QStringLiteral("lmmse"), QObject::tr("LMMSE (Linear Minimum Mean Square Error)")},
        {QStringLiteral("ahd"), QObject::tr("AHD (Adaptive Homogeneity-Directed)")},
        {QStringLiteral("vng4"), QObject::tr("VNG4 (Variable Number of Gradients)")},
        {QStringLiteral("bilinear"), QObject::tr("Bilinear (Fast, Low Quality)")},
    };
}

/// The key the page stores for a method the engine has.
QString demosaicKeyOf(const an::DemosaicMethod method)
{
    switch (method)
    {
    case an::DemosaicMethod::Amaze:
        return QStringLiteral("amaze");
    case an::DemosaicMethod::Lmmse:
        return QStringLiteral("lmmse");
    case an::DemosaicMethod::LuminanceAhd:
        return QStringLiteral("ahd");
    case an::DemosaicMethod::Vng4:
        return QStringLiteral("vng4");
    case an::DemosaicMethod::Fast:
        return QStringLiteral("bilinear");
    default:
        return QStringLiteral("rcd");
    }
}

an::ui::IRawOpenPolicyService* rawOpenPolicyService()
{
    auto* context = an::ui::appContext();
    return context ? context->getRawOpenPolicyService() : nullptr;
}

} // namespace

RawImageSettingsPage::RawImageSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString RawImageSettingsPage::pageTitle() const
{
    return tr("Raw Images");
}

QIcon RawImageSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("camera"));
}

QStringList RawImageSettingsPage::searchKeywords() const
{
    return QStringList{tr("Raw"),     tr("RAW"),         tr("Camera"), tr("White Balance"), tr("Demosaic"),
                       tr("Debayer"), tr("Color Space"), tr("sRGB"),   tr("Adobe RGB"),     tr("ProPhoto"),
                       tr("AHD"),     tr("VNG"),         tr("Rotate")};
}

void RawImageSettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === White Balance Section ===
    auto* wbGroup = new QGroupBox(tr("White Balance"), this);
    auto* wbLayout = new QVBoxLayout(wbGroup);
    wbLayout->setSpacing(12);

    // White balance mode dropdown
    auto* wbModeLayout = new QFormLayout();
    m_WhiteBalanceModeCombo = new QComboBox(wbGroup);
    for (const auto& [mode, display] : whiteBalanceModes())
    {
        m_WhiteBalanceModeCombo->addItem(display, an::ui::storedRawWhiteBalance(mode));
    }
    wbModeLayout->addRow(tr("Mode:"), m_WhiteBalanceModeCombo);
    wbLayout->addLayout(wbModeLayout);

    // Custom white balance multipliers (only visible when mode is "Custom")
    m_CustomWbContainer = new QWidget(wbGroup);
    auto* customWbLayout = new QHBoxLayout(m_CustomWbContainer);
    customWbLayout->setContentsMargins(0, 0, 0, 0);
    customWbLayout->setSpacing(16);

    auto createWbSpinBox = [this](const QString& label) {
        auto* container = new QWidget(m_CustomWbContainer);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* lbl = new QLabel(label, container);
        auto* spin = new QDoubleSpinBox(container);
        spin->setRange(0.1, 10.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setValue(1.0);

        layout->addWidget(lbl);
        layout->addWidget(spin);
        return qMakePair(container, spin);
    };

    auto [rContainer, rSpin] = createWbSpinBox(tr("R:"));
    auto [gContainer, gSpin] = createWbSpinBox(tr("G:"));
    auto [bContainer, bSpin] = createWbSpinBox(tr("B:"));

    m_CustomWbR = rSpin;
    m_CustomWbG = gSpin;
    m_CustomWbB = bSpin;

    customWbLayout->addWidget(rContainer);
    customWbLayout->addWidget(gContainer);
    customWbLayout->addWidget(bContainer);
    customWbLayout->addStretch();

    wbLayout->addWidget(m_CustomWbContainer);

    wbLayout->addWidget(createInfoLabel(
        tr("On a colour-filter-array sensor the balance is applied to the mosaic when a file is read; on "
           "a full-colour sensor the decoder applies it. A change reaches files read after it is "
           "applied; an image already open, or on its way, keeps the balance it was read with.")));

    mainLayout->addWidget(wbGroup);

    // === Behavior Section ===
    auto* behaviorGroup = new QGroupBox(tr("Loading Behavior"), this);
    auto* behaviorLayout = new QVBoxLayout(behaviorGroup);
    behaviorLayout->setSpacing(8);

    m_AutoRotateCheck = new QCheckBox(tr("Auto-rotate based on EXIF orientation"), behaviorGroup);
    m_AutoRotateCheck->setToolTip(tr("Automatically rotate images according to camera orientation metadata"));
    behaviorLayout->addWidget(m_AutoRotateCheck);

    mainLayout->addWidget(behaviorGroup);

    // === Demosaic Section ===
    auto* demosaicGroup = new QGroupBox(tr("Demosaic Settings"), this);
    auto* demosaicLayout = new QFormLayout(demosaicGroup);
    demosaicLayout->setSpacing(12);

    // Demosaic algorithm dropdown
    m_DemosaicAlgorithmCombo = new QComboBox(demosaicGroup);
    for (const auto& [value, display] : demosaicMethods())
    {
        m_DemosaicAlgorithmCombo->addItem(display, value);
    }
    demosaicLayout->addRow(tr("Demosaic Algorithm:"), m_DemosaicAlgorithmCombo);

    // Warning about demosaic
    auto* demosaicInfo = createInfoLabel(tr("Higher quality algorithms may significantly increase loading time."));
    demosaicLayout->addRow("", demosaicInfo);

    mainLayout->addWidget(demosaicGroup);

    // Add stretch to push content to top
    mainLayout->addStretch(1);

    // Initialize custom WB visibility
    updateCustomWbEnabled();
}

void RawImageSettingsPage::connectSignals()
{
    // Track changes for all controls
    trackChanges(m_WhiteBalanceModeCombo);
    trackChanges(m_CustomWbR);
    trackChanges(m_CustomWbG);
    trackChanges(m_CustomWbB);
    trackChanges(m_AutoRotateCheck);
    trackChanges(m_DemosaicAlgorithmCombo);

    // Show/hide custom WB when mode changes
    connect(m_WhiteBalanceModeCombo, &QComboBox::currentIndexChanged, this,
            &RawImageSettingsPage::updateCustomWbEnabled);
}

void RawImageSettingsPage::updateCustomWbEnabled()
{
    const bool isCustom = m_WhiteBalanceModeCombo->currentData().toString() ==
                          an::ui::storedRawWhiteBalance(an::ui::RawWhiteBalanceSetting::Custom);
    m_CustomWbContainer->setVisible(isCustom);
}

void RawImageSettingsPage::showSettings(const an::ui::RawOpenSettings& settings)
{
    const int wbModeIndex = m_WhiteBalanceModeCombo->findData(an::ui::storedRawWhiteBalance(settings.whiteBalance));
    m_WhiteBalanceModeCombo->setCurrentIndex(wbModeIndex >= 0 ? wbModeIndex : 1); // Daylight

    m_CustomWbR->setValue(settings.customGains[0]);
    m_CustomWbG->setValue(settings.customGains[1]);
    m_CustomWbB->setValue(settings.customGains[2]);

    m_AutoRotateCheck->setChecked(settings.autoRotate);
}

void RawImageSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();

    // The settings store is the authority, read through the table every open reads it through; without one the page
    // shows the policy published last.
    an::ui::RawOpenSettings settings;
    if (m_Settings)
    {
        settings = an::ui::readRawOpenSettings(
            [this](const QString& key, const QString& fallback) { return m_Settings->select(key, fallback); });
    }
    else if (auto* service = rawOpenPolicyService())
    {
        if (const std::shared_ptr<const an::ui::RawOpenPolicy> policy = service->rawOpenPolicy())
        {
            settings = policy->settings();
        }
    }
    showSettings(settings);

    const QString demosaic =
        m_Settings ? m_Settings->select(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, SettingsDefaults::RAW_DEMOSAIC_ALGORITHM)
                   : QString::fromLatin1(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
    const an::ui::DemosaicChoice choice = an::ui::demosaicMethodFromSettingsKey(demosaic);
    const int dmIndex = m_DemosaicAlgorithmCombo->findData(demosaicKeyOf(choice.method));
    m_DemosaicAlgorithmCombo->setCurrentIndex(dmIndex >= 0 ? dmIndex : 0);

    updateCustomWbEnabled();
    clearModified();
}

an::ui::RawOpenSettings RawImageSettingsPage::stagedSettings() const
{
    an::ui::RawOpenSettings settings;
    settings.whiteBalance = an::ui::rawWhiteBalanceFromStored(m_WhiteBalanceModeCombo->currentData().toString());
    settings.customGains = {m_CustomWbR->value(), m_CustomWbG->value(), m_CustomWbB->value()};
    settings.autoRotate = m_AutoRotateCheck->isChecked();
    return settings;
}

void RawImageSettingsPage::saveSettings()
{
    const an::ui::RawOpenSettings settings = stagedSettings();

    if (m_Settings)
    {
        an::ui::writeRawOpenSettings(
            settings, [this](const QString& key, const QString& value) { m_Settings->upsert(key, value); });
        m_Settings->upsert(SettingsKeys::RAW_DEMOSAIC_ALGORITHM, m_DemosaicAlgorithmCombo->currentData().toString());
    }

    // The files read from now on use the new policy; a read on its way keeps the one it was asked under.
    if (auto* service = rawOpenPolicyService())
    {
        const an::Status published = service->setRawOpenSettings(settings);
        if (!published.ok())
        {
            LOG_WARNING("The RAW settings were stored and could not be applied to new reads: {}", published.message());
        }
    }

    clearModified();
}

void RawImageSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();

    // White balance mode - Reset Page selects None, as it always has; a fresh profile reads Daylight.
    int wbIndex =
        m_WhiteBalanceModeCombo->findData(an::ui::storedRawWhiteBalance(an::ui::RawWhiteBalanceSetting::None));
    m_WhiteBalanceModeCombo->setCurrentIndex(wbIndex >= 0 ? wbIndex : 1);

    // Custom white balance - all 1.0
    m_CustomWbR->setValue(SettingsDefaults::RAW_CUSTOM_WB_R);
    m_CustomWbG->setValue(SettingsDefaults::RAW_CUSTOM_WB_G);
    m_CustomWbB->setValue(SettingsDefaults::RAW_CUSTOM_WB_B);

    // Behavior
    m_AutoRotateCheck->setChecked(SettingsDefaults::RAW_AUTO_ROTATE);

    // Demosaic algorithm
    int dmIndex = m_DemosaicAlgorithmCombo->findData(QString(SettingsDefaults::RAW_DEMOSAIC_ALGORITHM));
    m_DemosaicAlgorithmCombo->setCurrentIndex(dmIndex >= 0 ? dmIndex : 0);

    updateCustomWbEnabled();
    markAsModified();
}

ValidationResult RawImageSettingsPage::validateSettings() const
{
    // Validate custom white balance values
    if (stagedSettings().whiteBalance == an::ui::RawWhiteBalanceSetting::Custom)
    {
        const double r = m_CustomWbR->value();
        const double g = m_CustomWbG->value();
        const double b = m_CustomWbB->value();

        if (r < 0.1 || r > 10.0 || g < 0.1 || g > 10.0 || b < 0.1 || b > 10.0)
        {
            return ValidationResult::invalid(tr("Custom white balance multipliers must be between 0.1 and 10.0"),
                                             "customWb");
        }
    }

    return ValidationResult::valid();
}
