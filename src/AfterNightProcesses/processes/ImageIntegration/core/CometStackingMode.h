// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The comet stacking modes as the workflow persists and compares them: the spellings a session, a preset or a
/// registration record may hold, reduced to the one each means.
///
/// Engine-free, so the model's own serialization reads a registration record's comet output mode without reaching the
/// stacking translation helpers beside it.

#include <QString>

struct RegistrationSettings;

namespace ImageIntegration
{

/// The mode a spelling means: "Stars" for an empty spelling or a star-aligned one, "Comet" for a comet-aligned one,
/// the hidden stars-plus-comet token for that token, and any other spelling trimmed and otherwise as it was.
///
/// @param mode the spelling a record holds.
/// @return the mode's one spelling.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString normalizeCometStackingMode(const QString& mode);

/// The hidden token of the experimental stars-plus-comet mode, which no user-facing list offers.
///
/// @return the token.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString starsPlusCometExperimentalStackingModeToken();

/// Whether a spelling is the experimental stars-plus-comet token, space aside.
///
/// @param mode the spelling.
/// @return true for the token.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool isStarsPlusCometExperimentalStackingMode(const QString& mode);

/// Whether a spelling means the experimental stars-plus-comet mode.
///
/// @param mode the spelling.
/// @return true for the hidden token.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool usesStarsPlusCometExperimentalStackingMode(const QString& mode);

/// Whether a spelling means the comet-only mode.
///
/// @param mode the spelling.
/// @return true when the spelling normalizes to "Comet".
[[nodiscard]] AFTERNIGHTPROCESSES_API bool usesCometOnlyStackingMode(const QString& mode);

/// Whether a spelling means a mode that stacks the comet - comet-only or stars-plus-comet.
///
/// @param mode the spelling.
/// @return true for either.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool usesCometStackingMode(const QString& mode);

/// Whether a registration record asks for the experimental stars-plus-comet output.
///
/// @param settings the registration settings.
/// @return true when comet tracking is configured and its output mode is the hidden token.
[[nodiscard]] bool registrationRequestsStarsPlusCometExperimentalOutput(const RegistrationSettings& settings);

} // namespace ImageIntegration
