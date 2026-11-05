// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// A stacked master's world coordinates and the cards it carries, all decided before it is written.
///
/// **The solution is chosen, and solved if it must be, before the master is encoded.** The master is never written,
/// reloaded, solved and written again through a temporary copy when its propagated solution is missing:
/// `buildMasterWcsCandidate` chooses the solution - the registration's output plane, moved onto a drizzled canvas when
/// the stack drizzled, or the reference light's own for a stack no registration placed - and when it gives none and the
/// group asks for one, `solveFinalMasterWcs` plate-solves the master as it is in memory, through a scratch copy the
/// solver reads, so the master is written once, whole, with whatever solution it carries.
///
/// **The cards are stated, not inherited.** The master states its exposure - the sum of its lights' - how many frames
/// it combines, its drizzle, its solution and, for a mosaic, where it points; and the observing context of its
/// reference light is carried forward by the engine's rule for a derived product (`withObservationCards`). A mosaic's
/// pointing is the spherical mean of its lights' pointings, each read by the engine's angle reader, until a solution
/// says where the master points.

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>

#include <QDir>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace an::ui
{
class PlateSolveService;
} // namespace an::ui

namespace ImageIntegration
{
class WorkflowStageServices;
} // namespace ImageIntegration

namespace ImageIntegration::detail
{

/// Append only the warnings not already present, dropping blanks.
void appendUniqueWarnings(QStringList& warnings, const QStringList& additions);

/// A card whose value is a FITS string: quoted, with each quote inside it doubled.
[[nodiscard]] std::string fitsString(QString value);

/// Metadata's cards with one FITS card set: the first card of its keyword replaced, its later ones removed, or the card
/// appended.
[[nodiscard]] an::Result<an::CodecMetadata> withFitsCard(const an::CodecMetadata& metadata, const std::string& keyword,
                                                         const std::string& value, const std::string& comment = {});

/// Metadata's cards without any FITS card of a keyword.
[[nodiscard]] an::Result<an::CodecMetadata> withoutFitsCard(const an::CodecMetadata& metadata,
                                                            std::string_view keyword);

/// Whether a solution is one the workflow publishes: its centre on the sky, a positive scale on both axes.
[[nodiscard]] bool hasValidAstrometricWcs(const an::WcsSolution& solution, QString* error = nullptr);

/// How a drizzled master's plane relates to the registration's.
struct DrizzleMasterGeometry
{
    /// The registration plane's position on the drizzle canvas's analysis plane: the analysis plane's origin less the
    /// registration plane's, in pixels.
    double shiftX = 0.0;
    double shiftY = 0.0;

    /// The analysis plane's extent.
    std::uint32_t analysisWidth = 0u;
    std::uint32_t analysisHeight = 0u;

    /// The enlargement.
    double scale = 1.0;

    /// The whole pixels the finished stack crops from every edge.
    std::uint32_t edgeGuard = 0u;
};

/// What decides a master's solution.
struct MasterWcsRequest
{
    /// Whether the group is registered: its solution is then the registration's output plane, and no other.
    bool registered = false;

    /// The registration's output-plane record, when it has one.
    std::optional<QString> outputPlaneWcsJson;

    /// The registration's reference, and the canvas and extent its plane should have.
    QString registrationReference;
    QString alignmentMode;
    std::uint32_t planeWidth = 0u;
    std::uint32_t planeHeight = 0u;
    std::optional<bool> expectedSipCorrection;

    /// The reference light's own cards, for a group no registration placed.
    std::vector<an::CodecCard> referenceCards;
    std::uint32_t referenceWidth = 0u;
    std::uint32_t referenceHeight = 0u;

    /// How the drizzled master's plane relates to the registration's, when the stack drizzled.
    std::optional<DrizzleMasterGeometry> drizzle;

    /// The master's extent.
    std::uint32_t masterWidth = 0u;
    std::uint32_t masterHeight = 0u;
};

/// The solution chosen for a master.
struct MasterWcsCandidate
{
    std::optional<an::WcsSolution> solution;
    QString sourceDescription;
    QString diagnostic;

    /// Whether the group's registration recorded no output-plane solution at all.
    bool missingOutputPlaneRecord = false;
};

/// Chooses a master's solution.
[[nodiscard]] MasterWcsCandidate buildMasterWcsCandidate(const MasterWcsRequest& request);

/// What a master's cards say beside its solution.
struct MasterCardsRequest
{
    /// The reference light's cards, whose observing context is carried forward.
    const an::CodecMetadata* referenceCards = nullptr;

    /// The total exposure of the lights stacked, in seconds; nothing when none states one.
    std::optional<double> integrationSeconds;

    /// How many frames the master combines, stated as `STACKCNT`: the sum of each light's own count
    /// (`combinedFrameCount`), so a master of masters states every frame beneath it; nothing states no count.
    std::optional<std::uint64_t> combinedFrames;

    /// The drizzle, when the stack drizzled, and how many lights it drizzled.
    std::optional<WorkflowManifest::IntegrationGroup::DrizzleSettings> drizzle;
    std::size_t drizzledLights = 0u;

    /// The master's solution, when it has one.
    std::optional<an::WcsSolution> solution;

    /// A mosaic's pointing cards, from `mosaicPointingCards`.
    std::vector<an::CodecCard> pointing;
};

/// A master's cards.
///
/// @param request what the master states.
/// @return the cards; or what writing a card or the solution refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::CodecMetadata>
masterCodecMetadata(const MasterCardsRequest& request);

/// A mosaic master's pointing: the reference light's own pointing kept as `ANREFRA`, `ANREFDEC`, `ANREFSRC` and
/// `ANREFROT`, and where the master points - its solution's centre and orientation when it has one (`ANPOSMTH` is
/// `WCS`), otherwise the spherical mean of every light's pointing (`SPHAVG`, with `ANPOSCNT` lights) - as `RA`,
/// `DEC`, `OBJCTRA`, `OBJCTDEC` and `OBJCTROT`.
///
/// @param reference the reference light's cards.
/// @param lights every light's cards.
/// @param solution the master's solution, when it has one.
/// @return the cards; empty when nothing states a pointing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<an::CodecCard>
mosaicPointingCards(std::span<const an::CodecCard> reference, const std::vector<std::vector<an::CodecCard>>& lights,
                    const std::optional<an::WcsSolution>& solution);

/// The solutions a run solved for its masters, so masters of one registration solve once.
struct FinalMasterWcsCache
{
    bool noConfiguredSolver = false;
    std::unordered_map<QString, std::pair<an::WcsSolution, QString>> solvedByKey;
};

/// What solving a master found.
struct FinalMasterWcsOutcome
{
    bool cancelled = false;
    bool reusedCachedSolution = false;
    std::optional<an::WcsSolution> solution;
    QString solver;
    QString warning;
    QString detail;
};

/// Plate-solves a master that has no usable solution, as it is in memory: written to a scratch FITS file the solver
/// reads - its cards without any solution, so its pointing and optics are the solver's hints, with the group's object
/// looked up in the bundled catalogs when its cards state no pointing - and removed afterwards. A solution found for
/// a master of the same registration and size earlier in the run is reused.
///
/// @param service the plate solve.
/// @param services the run's services.
/// @param manifest the run's manifest.
/// @param group the group.
/// @param master the master, its cards the hints.
/// @param workDirectory where the scratch file and the solver's work go.
/// @param currentIssue why the master has no solution, for the warning.
/// @param token the run's cancellation.
/// @param cache the run's solutions; may be null.
/// @return the outcome.
[[nodiscard]] FinalMasterWcsOutcome
solveFinalMasterWcs(an::ui::PlateSolveService& service, const WorkflowStageServices& services,
                    const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group,
                    const an::Image<float>& master, const QString& workDirectory, const QString& currentIssue,
                    CancellationToken& token, FinalMasterWcsCache* cache);

/// Removes the scratch and temporary files an interrupted master solve or rewrite left in the output directory.
void cleanupLeftoverWcsTempFiles(const QDir& outputRoot);

} // namespace ImageIntegration::detail
