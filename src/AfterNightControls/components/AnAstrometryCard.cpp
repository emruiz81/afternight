// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnAstrometryCard.h"

#include "components/AnChip.h"
#include "components/AnFlowLayout.h"
#include "components/AnIconButton.h"
#include "components/AnReadoutGrid.h"
#include "theme/ComponentParts.h"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnAstrometryCard)

namespace an::ui
{
namespace
{

/// Each overlay's chip: its text and what its tooltip says it toggles, with the view's shortcut.
struct OverlaySpec
{
    const char* text;
    const char* tip;
};

constexpr std::array<OverlaySpec, kAstrometryOverlays> kOverlays{{
    {"Grid", "Equatorial grid (Ctrl+1)"},
    {"Annotations", "Deep-sky annotations (Ctrl+2)"},
    {"Star labels", "Star labels (Ctrl+3)"},
    {"Detected stars", "Detected stars (Ctrl+4)"},
    {"SIP map", "SIP distortion map (Ctrl+5)"},
}};

void keepLayout(QLayout* layout)
{
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
}

QPushButton* button(const QString& text, const QString& tip, const char* variant, QWidget* parent)
{
    auto* made = new QPushButton(text, parent);
    made->setToolTip(tip);
    made->setProperty("anButtonVariant", QString::fromLatin1(variant));
    return made;
}

} // namespace

AnAstrometryCard::AnAstrometryCard(QWidget* parent) : AnReadoutCard(tr("Astrometry"), parent)
{
    m_settings =
        addHeaderTool(QStringLiteral("tune"), tr("Astrometry Settings: level of detail, labels, palette, text size"));
    m_settings->setEnabled(false);

    m_grid = new AnReadoutGrid(2, body());
    bodyLayout()->addWidget(m_grid);
    m_unsolved = new AnReadoutText(AnReadoutText::Style::Caption, tr("No astrometric solution"), body());
    bodyLayout()->addWidget(m_unsolved);

    m_chips = new QWidget(body());
    auto* flow = new AnFlowLayout(m_chips);
    keepLayout(flow);
    flow->setContentsMargins(0, 0, 0, 0);
    flow->setSpacing(parts::kChipGap);
    for (std::size_t index = 0u; index < kAstrometryOverlays; ++index)
    {
        auto* chip = new AnChip(tr(kOverlays[index].text), m_chips);
        chip->setCheckable(true);
        chip->setToolTip(tr(kOverlays[index].tip));
        chip->setEnabled(false);
        flow->addWidget(chip);
        m_chip[index] = chip;
        QObject::connect(chip, &QAbstractButton::toggled, this, [this, index](const bool on) {
            QAction* const action = m_bindings[index].action.data();
            if (action != nullptr && action->isChecked() != on)
            {
                action->setChecked(on);
            }
            overlayToggled(static_cast<int>(index), on);
        });
    }
    bodyLayout()->addWidget(m_chips);

    // The buttons wrap as the chips do: a row wider than the column would make the card claim more width than it has,
    // and its rows' heights would be reckoned at that width.
    auto* buttonRow = new QWidget(body());
    auto* buttons = new AnFlowLayout(buttonRow);
    keepLayout(buttons);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(parts::kReadoutToolGap * 2);
    m_plateSolve = button(tr("Plate Solve"), tr("Plate Solve (Ctrl+P)"), "tonal", buttonRow);
    m_annotate = button(tr("Annotate..."), tr("Annotate Image (Ctrl+Shift+L)"), "quiet", buttonRow);
    m_info = button(tr("Info"), tr("Astrometry Info (Ctrl+Shift+P)"), "quiet", buttonRow);
    buttons->addWidget(m_plateSolve);
    buttons->addWidget(m_annotate);
    buttons->addWidget(m_info);
    bodyLayout()->addWidget(buttonRow);

    QObject::connect(m_plateSolve, &QPushButton::clicked, this, [this]() { plateSolveRequested(); });
    QObject::connect(m_annotate, &QPushButton::clicked, this, [this]() { annotateRequested(); });
    QObject::connect(m_info, &QPushButton::clicked, this, [this]() { infoRequested(); });
    QObject::connect(m_settings, &QAbstractButton::clicked, this, [this]() {
        if (m_settingsMenu)
        {
            m_settingsMenu->popup(m_settings->mapToGlobal(QPoint(0, m_settings->height())));
        }
    });
    refill();
}

AnAstrometryCard::~AnAstrometryCard()
{
    for (Binding& binding : m_bindings)
    {
        QObject::disconnect(binding.follows);
        QObject::disconnect(binding.ticked);
    }
}

void AnAstrometryCard::setReadout(const AstrometryReadout& readout)
{
    if (m_readout == readout)
    {
        return;
    }
    m_readout = readout;
    refill();
}

const AstrometryReadout& AnAstrometryCard::readout() const noexcept
{
    return m_readout;
}

void AnAstrometryCard::bindOverlay(const AstrometryOverlay overlay, QAction* action)
{
    const auto index = static_cast<std::size_t>(overlay);
    Binding& binding = m_bindings[index];
    QObject::disconnect(binding.follows);
    QObject::disconnect(binding.ticked);
    binding.action = action;
    AnChip* const chip = m_chip[index];
    chip->setEnabled(action != nullptr);
    {
        const QSignalBlocker quiet(chip);
        chip->setChecked(action != nullptr && action->isChecked());
    }
    if (action != nullptr)
    {
        // The chip follows the action however it is toggled: by the chip, by the view's menu or by its shortcut.
        binding.follows = QObject::connect(action, &QAction::toggled, chip, [chip](const bool on) {
            const QSignalBlocker quiet(chip);
            chip->setChecked(on);
        });
        // Ticked in the settings menu, the overlay is turned on or off as a click on its chip turns it: its owner shows
        // the view's overlays for one turned on. The view's shortcut toggles the action and triggers nothing.
        binding.ticked = QObject::connect(action, &QAction::triggered, this, [this, index](const bool on) {
            overlayToggled(static_cast<int>(index), on);
        });
    }
}

void AnAstrometryCard::setSettingsMenu(QMenu* menu)
{
    m_settingsMenu = menu;
    m_settings->setEnabled(menu != nullptr);
}

QMenu* AnAstrometryCard::settingsMenu() const
{
    return m_settingsMenu.data();
}

AnChip* AnAstrometryCard::overlayChip(const AstrometryOverlay overlay) const
{
    return m_chip[static_cast<std::size_t>(overlay)];
}

AnIconButton* AnAstrometryCard::settingsButton() const noexcept
{
    return m_settings;
}

QPushButton* AnAstrometryCard::plateSolveButton() const noexcept
{
    return m_plateSolve;
}

QPushButton* AnAstrometryCard::annotateButton() const noexcept
{
    return m_annotate;
}

QPushButton* AnAstrometryCard::infoButton() const noexcept
{
    return m_info;
}

AnReadoutGrid* AnAstrometryCard::readouts() const noexcept
{
    return m_grid;
}

AnReadoutText* AnAstrometryCard::unsolvedText() const noexcept
{
    return m_unsolved;
}

void AnAstrometryCard::staleChanged()
{
    m_grid->setStale(isStale());
}

void AnAstrometryCard::refill()
{
    const bool solved = m_readout.solved;
    m_grid->setVisible(solved);
    m_chips->setVisible(solved);
    m_annotate->setVisible(solved);
    m_info->setVisible(solved);
    m_unsolved->setVisible(!solved);
    if (!solved)
    {
        setSummary(tr("Not solved"));
        return;
    }
    m_grid->setCells({ReadoutCell{.label = tr("Field centre RA"), .value = m_readout.rightAscension},
                      ReadoutCell{.label = tr("Field centre Dec"), .value = m_readout.declination},
                      ReadoutCell{.label = tr("Scale"), .value = m_readout.scale, .unit = QStringLiteral("″/px")},
                      ReadoutCell{.label = tr("Rotation"), .value = m_readout.rotation, .unit = tr("deg")}});
    QStringList summary{tr("Solved")};
    if (!m_readout.solver.isEmpty())
    {
        summary.append(m_readout.solver);
    }
    if (!m_readout.distortion.isEmpty())
    {
        summary.append(m_readout.distortion);
    }
    setSummary(summary.join(QStringLiteral(" · ")));
}

} // namespace an::ui
