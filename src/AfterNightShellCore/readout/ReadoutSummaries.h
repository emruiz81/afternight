// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the readout writes of an image beyond its figures: its solution as the astrometry card reads it out, the line
/// a task's compact readout gives it, and the image card's facts of its description. Pure: no widget, no engine call.

#include "AfterNightShellCoreExport.h"

#include "components/ReadoutFigures.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QString>

#include <optional>
#include <span>

namespace an::ui::shell
{

/// The astrometry card's read-out of a solution: the field centre's RA and Dec as the astrometry window writes them,
/// the mean scale to three decimals, the rotation to two, the distortion's forward orders ("SIP 4/4") and the solver.
///
/// @param solution the solution the image's cards state, or nothing for an image with none.
/// @param solver the solver the cards name, or empty.
[[nodiscard]] AFTERNIGHTSHELLCORE_API an::ui::AstrometryReadout
astrometryReadout(const std::optional<an::WcsSolution>& solution, const QString& solver);

/// The solver an image's codec cards name, or empty.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QString solverNamed(std::span<const an::CodecCard> cards);

/// The compact readout's line of a solution: "0.520″/px · SIP 4/4", or the scale alone for a linear one; empty when
/// the image is not solved.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QString compactAstrometryLine(const an::ui::AstrometryReadout& readout);

/// The image card's facts of a description: "4432 × 3840", the status line's name of its samples, the memory its
/// samples take in MB to two decimals - in GB past 1024 MB - and "RGB" or "Grayscale"; the summary in one line with the
/// sample kind in short ("16 bit uint", "32 bit float").
[[nodiscard]] AFTERNIGHTSHELLCORE_API an::ui::ImageFactsReadout
imageFactsReadout(const an::ImageDescriptor& descriptor);

} // namespace an::ui::shell
