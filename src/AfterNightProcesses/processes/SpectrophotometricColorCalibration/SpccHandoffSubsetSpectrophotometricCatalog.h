// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The ImageIntegration hand-off to SPCC: the Gaia sources the integration's registration matched in the frames a
/// stacked image was made of, found from the image's file, held to the solution and the epoch the calibration runs
/// under, and the catalog confined to the ones that still hold.
///
/// **Where the sources are.** ImageIntegration writes its stacked image beside a workflow archive -
/// `workflow_manifest.json` in the output directory or one of its `history` runs - whose integration groups name their
/// output file and their registration group, and a registration database, `registration_metrics.db`, holding the
/// sources each registration group matched under each settings version. `locateSpccIntegrationHandoff` finds the
/// group whose output is the image and reads the sources of its latest settings through
/// `readIntegrationMatchedSources`, which the registration metrics loader defines beside the database it reads.
///
/// **Which sources still hold.** A recorded match names the catalog, the solution, the epoch and the proper-motion
/// model it was made under, and its residual and separation; `spccHandoffRequirements` states what the calibration
/// runs under and the engine's `resolveMatchedSources` decides each one. The calibration runs over a catalog of the
/// accepted sources alone (`buildSpccSubsetCatalog`) when there are enough of them for the fit, and over the whole
/// region otherwise, or when that run fails and the region query is allowed.
///
/// **The solution's identity is its own.** `spccSolutionIdentity` is a digest of the solution the cards resolve to, not
/// of the cards as a file spells them, so two spellings of one solution are one identity and the registration can
/// state it from the solution it computed.
#pragma once

#include "AfterNightProcessesExport.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Core/Result.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace an::ui::processes
{

/// The proper-motion model the registration's matches and the calibration's preflight name their epoch correction by.
inline constexpr const char kSpccProperMotionModelVersion[] = "gaia_epoch_correction_v1";

/// The largest residual a recorded match may carry and still be used, in pixels.
inline constexpr double kSpccHandoffMaximumResidualPixels = 3.0;

/// The largest separation a recorded match may carry and still be used, in arcseconds.
inline constexpr double kSpccHandoffMaximumSeparationArcseconds = 5.0;

/// The sources a registration group recorded under one settings version.
struct IntegrationMatchedSources
{
    /// The settings version the sources were recorded under.
    int settingsVersion = -1;

    /// The recorded matches.
    std::vector<an::MatchedSourceReference> references;
};

/// Reads the sources a registration group matched, under its latest settings version, from an integration's
/// registration database. Defined beside the registration metrics loader, which reads that database.
///
/// @param metricsDatabasePath the registration database.
/// @param registrationGroupKey the registration group.
/// @return the sources; `ErrorCode::ExternalFailure` naming the database when it cannot be opened or read;
///         `ErrorCode::EmptySelection` when the group recorded no settings version.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<IntegrationMatchedSources>
readIntegrationMatchedSources(const QString& metricsDatabasePath, const QString& registrationGroupKey);

/// The identity a world-coordinate solution is recorded and required under: a digest of the cards the solution writes
/// and the image size it states.
///
/// @param solution the solution.
/// @return the identity, `an-wcs:` and the digest; empty when the solution writes no cards.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString spccSolutionIdentity(const an::WcsSolution& solution);

/// The observation epoch as a recorded match names it: the Julian year with six decimals.
///
/// @param epochYear the epoch, or nothing.
/// @return the text; empty for no epoch.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString spccObservationEpochText(const std::optional<double>& epochYear);

/// What the hand-off of an image found.
struct SpccIntegrationHandoff
{
    /// Whether a workflow archive's integration group named the image as its output.
    bool located = false;

    /// Whether the registration database's sources were read.
    bool loaded = false;

    /// Whether a run that fails over the sources alone runs again over the region.
    bool fallbackRegionQueryAllowed = true;

    QString targetPath;
    QString outputDirectory;
    QString metricsDatabasePath;
    QString manifestPath;
    QString integrationGroupKey;
    QString registrationGroupKey;
    int settingsVersion = -1;

    /// The recorded matches.
    std::vector<an::MatchedSourceReference> references;

    /// What the search found and did, for the diagnostics a person reads.
    QStringList diagnostics;
};

/// Finds the hand-off of an image ImageIntegration wrote and reads the sources its registration recorded.
///
/// @param targetPath the image's file; empty for an image that came from none.
/// @param fallbackRegionQueryAllowed whether a failed run over the sources alone runs again over the region.
/// @return what was found; nothing found is a hand-off with no references and diagnostics saying why.
[[nodiscard]] AFTERNIGHTPROCESSES_API SpccIntegrationHandoff
locateSpccIntegrationHandoff(const QString& targetPath, bool fallbackRegionQueryAllowed = true);

/// What a recorded match must agree with to be used by a calibration under a solution and an epoch.
///
/// @param solutionIdentity the solution's identity, from `spccSolutionIdentity`.
/// @param epochYear the observation epoch, or nothing.
/// @return the requirements: the calibration's own catalog, the proper-motion model, the solution, the epoch, and the
///         residual and separation bounds.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::MatchedSourceRequirements
spccHandoffRequirements(const QString& solutionIdentity, const std::optional<double>& epochYear);

/// The Gaia sources a resolution accepted, ascending and unique.
///
/// @param resolution the resolution.
/// @param catalog the catalog it was resolved against.
/// @return the sources.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<std::uint64_t>
acceptedHandoffSources(const an::MatchedSourceResolution& resolution, const an::SpectrumCatalog& catalog);

/// The sentence a resolution adds to the diagnostics: how many recorded sources were accepted, against which manifest,
/// and whether the region query may follow.
///
/// @param handoff the hand-off.
/// @param resolution its resolution.
/// @param manifest the catalog's manifest.
/// @return the sentence.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString spccHandoffResolutionText(const SpccIntegrationHandoff& handoff,
                                                                        const an::MatchedSourceResolution& resolution,
                                                                        const an::SpectrumCatalogManifest& manifest);

} // namespace an::ui::processes
