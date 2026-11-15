// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The readout's astrometry: where the image points, and the overlays a view draws of its solution.

#include "AfterNightControlsExport.h"
#include "components/AnReadoutCard.h"
#include "components/ReadoutFigures.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>

#include <verdigris/wobjectdefs.h>

class QAction;
class QMenu;
class QPushButton;

namespace an::ui
{

class AnChip;
class AnIconButton;
class AnReadoutGrid;
class AnReadoutText;

/// An overlay of the solution a chip turns on or off, in the chips' order.
enum class AstrometryOverlay : std::uint8_t
{
    Grid,
    Annotations,
    StarLabels,
    DetectedStars,
    SipMap
};

/// How many overlays there are.
inline constexpr std::size_t kAstrometryOverlays = 5u;

/// The Astrometry card. Solved, it reads out the field centre's RA and Dec, the scale and the rotation; a chip for each
/// overlay - Grid, Annotations, Star labels, Detected stars, SIP map - each a toggle bound to the action that draws it
/// on the view, so a chip turns its overlay on and follows the view's shortcut; and Plate Solve, Annotate... and Info.
/// Its header says "Solved", the solver and the distortion, and its tool opens the view's astrometry settings. Not
/// solved, it says "No astrometric solution" and offers Plate Solve.
class AFTERNIGHTCONTROLS_API AnAstrometryCard final : public AnReadoutCard
{
    W_OBJECT(AnAstrometryCard)

public:
    explicit AnAstrometryCard(QWidget* parent = nullptr);
    ~AnAstrometryCard() override;

    void setReadout(const AstrometryReadout& readout);
    [[nodiscard]] const AstrometryReadout& readout() const noexcept;

    /// Binds an overlay's chip to the action that draws the overlay: the chip shows the action's state and a click
    /// toggles the action. Null leaves the chip disabled.
    void bindOverlay(AstrometryOverlay overlay, QAction* action);

    /// The menu the header's tool opens - the view's astrometry settings; null disables the tool.
    void setSettingsMenu(QMenu* menu);
    [[nodiscard]] QMenu* settingsMenu() const;

    [[nodiscard]] AnChip* overlayChip(AstrometryOverlay overlay) const;
    [[nodiscard]] AnIconButton* settingsButton() const noexcept;
    [[nodiscard]] QPushButton* plateSolveButton() const noexcept;
    [[nodiscard]] QPushButton* annotateButton() const noexcept;
    [[nodiscard]] QPushButton* infoButton() const noexcept;
    [[nodiscard]] AnReadoutGrid* readouts() const noexcept;
    [[nodiscard]] AnReadoutText* unsolvedText() const noexcept;

    /// Told when the person turns an overlay's chip on or off, after its action was toggled.
void overlayToggled(int overlay, bool on) W_SIGNAL(overlayToggled, overlay, on)

    void plateSolveRequested() W_SIGNAL(plateSolveRequested) void annotateRequested()
        W_SIGNAL(annotateRequested) void infoRequested() W_SIGNAL(infoRequested)

            protected : void staleChanged() override;

private:
    struct Binding
    {
        QPointer<QAction> action;
        QMetaObject::Connection follows;
    };

    void refill();

    AstrometryReadout m_readout;
    AnReadoutGrid* m_grid = nullptr;
    AnReadoutText* m_unsolved = nullptr;
    QWidget* m_chips = nullptr;
    std::array<AnChip*, kAstrometryOverlays> m_chip{};
    std::array<Binding, kAstrometryOverlays> m_bindings{};
    QWidget* m_solvedButtons = nullptr;
    QPushButton* m_plateSolve = nullptr;
    QPushButton* m_annotate = nullptr;
    QPushButton* m_info = nullptr;
    AnIconButton* m_settings = nullptr;
    QPointer<QMenu> m_settingsMenu;
};

} // namespace an::ui
