// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the plate solve does around its solvers: a run's deadline, its staging file, the image planes the internal
/// solver detects in, a solver's header-only answer read through the engine's card bridge, and a solution written into
/// - or withdrawn from - an image's own metadata.
///
/// **One bridge for the world-coordinate cards.** A solver's `.wcs` answer is read by the engine's header reader
/// (`parseFitsHeaderCards`) and turned into a solution by its card bridge (`parseWcsCards`); a solution reaches an
/// image through `withWcsCards` and leaves it through `withoutWcsCards`, whose vocabulary is `isWcsKeyword` and the
/// properties of an XISF image's astrometric solution, so a solve of a PixInsight-solved image replaces
/// PixInsight's solution as well as any keywords.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/PlateSolveTypes.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QByteArray>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

namespace an::ui
{

class AFTERNIGHTCONTROLS_API PlateSolveRunGuard
{
public:
    explicit PlateSolveRunGuard(std::chrono::milliseconds timeout);

    [[nodiscard]] bool shouldStop(bool externally_cancelled) const noexcept;
    [[nodiscard]] bool timedOut() const noexcept;

    /// When the run stops being allowed to work.
    [[nodiscard]] std::chrono::steady_clock::time_point deadline() const noexcept;

private:
    std::chrono::steady_clock::time_point m_deadline;
    mutable std::atomic<bool> m_timedOut{false};
};

[[nodiscard]] AFTERNIGHTCONTROLS_API QString createUniquePlateSolveInputPath(const QString& work_directory_root);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<int> internalGaiaDetectionChannels(int channel_count);

/// How an image is written for a solver to read: FITS in single precision, which every solver reads, its cards carried.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::OutputIntent> plateSolveInputIntent();

/// The cards of a solver's header-only answer, read as the FITS codec reads cards, with the reference frame stated.
///
/// astrometry.net and ASTAP solve against catalogs in the ICRS and write an equinox with no frame, which the card
/// bridge reads as FK5 and refuses; a header that names no frame is therefore given `RADESYS = 'ICRS'`. A header that
/// names one keeps it.
///
/// @param header the answer's bytes: eighty-byte records or text lines, as the header reader takes them.
/// @return the cards; or what `parseFitsHeaderCards` refused - a header with no `END` record among it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::vector<an::CodecCard>> solvedHeaderCards(std::string_view header);

/// The solution a solver's header-only answer states, for a frame of the solved image's size.
///
/// @param header the answer's bytes.
/// @param width the solved frame's width.
/// @param height the solved frame's height.
/// @return the solution; or what `solvedHeaderCards` or `parseWcsCards` refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::WcsSolution>
readSolvedWcsHeader(std::string_view header, std::uint32_t width, std::uint32_t height);

/// The solution a solver's header-only answer file states, read up to the header reader's bound.
///
/// @param path the answer's file.
/// @param width the solved frame's width.
/// @param height the solved frame's height.
/// @return the solution; `ErrorCode::ExternalFailure` naming the file when it cannot be read; or what
///         `readSolvedWcsHeader` refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::WcsSolution>
readSolvedWcsHeaderFile(const QString& path, std::uint32_t width, std::uint32_t height);

/// What the plate-solve window writes beside a solution.
struct PlateSolutionFacts
{
    /// The solver that found it, written as `PLTSOLVR`; empty writes none.
    QString solver;

    /// The focal length the solved scale implies, written as `FOCALLEN`.
    std::optional<double> focalLengthMillimetres;

    /// The pixel size the solve was given, written as `XPIXSZ` and `YPIXSZ`.
    std::optional<double> pixelSizeMicrometres;
};

/// An image's metadata carrying a solution and no other: its world-coordinate cards replaced by the solution's, the
/// facts written beside them, the summary that travels with an image set from the solution, and the change recorded.
///
/// @param metadata what the image has.
/// @param solution the solution.
/// @param facts what is written beside it.
/// @return the metadata; or what the card bridge, a card or the lineage refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::ImageMetadata>
metadataWithPlateSolution(const an::ImageMetadata& metadata, const an::WcsSolution& solution,
                          const PlateSolutionFacts& facts);

/// An image's metadata with its solution withdrawn: every world-coordinate card and the solver's card removed, the
/// summary withdrawn, and the change recorded.
///
/// @param metadata what the image has.
/// @return the metadata; or what the card bridge or the lineage refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::ImageMetadata>
metadataWithoutPlateSolution(const an::ImageMetadata& metadata);

/// A copy of an image, its mask included, carrying other metadata: what a metadata-only edit shows.
///
/// @param session the process's session.
/// @param image the image, under the writer the edit holds.
/// @param metadata the copy's metadata.
/// @param stop the edit's stop.
/// @return the copy, a new asset of the session; `ErrorCode::Cancelled` when stopped; or what the copy refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
copyImageWithMetadata(const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
                      an::ImageMetadata metadata, std::stop_token stop);

} // namespace an::ui
