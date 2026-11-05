// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a plate solve is asked for and what it answers, shared by the plate-solve window and the workflows that solve
/// their frames.
///
/// A solve reads an image's metadata - its header cards state the position and scale hints and any solution it already
/// carries, its acquisition record the epoch the catalog is carried to - and answers the engine's `WcsSolution`, in
/// engine pixel coordinates of the solved frame, with the figures a window shows derived from it. Writing the solution
/// into an image is the caller's: the card bridge's `withWcsCards` puts it into the image's own cards.
#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QMetaType>
#include <QString>
#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace an::ui
{

/// The card a solved image names its solver in.
inline constexpr const char kPlateSolveSolverKeyword[] = "PLTSOLVR";

namespace detail
{

/// The FITS card of a keyword, or null.
[[nodiscard]] inline const an::CodecCard* fitsCardOf(std::span<const an::CodecCard> cards,
                                                     const std::string_view keyword)
{
    for (const an::CodecCard& card : cards)
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword)
        {
            return &card;
        }
    }
    return nullptr;
}

/// Whether cards state the J2000 equinox - `EQUINOX`, or the older `EPOCH` where there is none - and no reference
/// frame.
[[nodiscard]] inline bool statesJ2000WithoutFrame(std::span<const an::CodecCard> cards)
{
    if (fitsCardOf(cards, "RADESYS") != nullptr || fitsCardOf(cards, "RADECSYS") != nullptr)
    {
        return false;
    }
    const an::CodecCard* equinox = fitsCardOf(cards, "EQUINOX");
    if (equinox == nullptr)
    {
        equinox = fitsCardOf(cards, "EPOCH");
    }
    if (equinox == nullptr)
    {
        return false;
    }
    bool numeric = false;
    const double year = QString::fromStdString(equinox->value()).trimmed().toDouble(&numeric);
    return numeric && year == 2000.0;
}

} // namespace detail

/// The solution an image's cards state, read by the engine's card bridge.
///
/// astrometry.net and ASTAP write the J2000 equinox and no frame into a file they solve, and the product's plate solve
/// once kept their cards as written; the bridge reads that as FK5 and refuses it rather than assume a frame. FK5 at
/// J2000 and the ICRS differ by tens of milliarcseconds, a small fraction of a pixel of any image the product solves or
/// shows, so such cards are read with `RADESYS = 'ICRS'` stated. A frame the cards state is never overridden, and any
/// other equinox is read as the bridge reads it.
///
/// @param cards the image's cards.
/// @param width the image's width.
/// @param height the image's height.
/// @return the solution, or what the bridge refused.
[[nodiscard]] inline an::Result<an::WcsSolution>
readImageWcsSolution(std::span<const an::CodecCard> cards, const std::uint32_t width, const std::uint32_t height)
{
    an::Result<an::WcsSolution> parsed = an::parseWcsCards(cards, width, height);
    if (parsed.hasValue() || !detail::statesJ2000WithoutFrame(cards))
    {
        return parsed;
    }
    an::Result<an::CodecCard> frame = an::CodecCard::create(an::CodecNamespace::Fits, "RADESYS", "'ICRS'", "");
    if (!frame.hasValue())
    {
        return parsed;
    }
    std::vector<an::CodecCard> stated(cards.begin(), cards.end());
    stated.push_back(std::move(*frame));
    return an::parseWcsCards(stated, width, height);
}

[[nodiscard]] inline QString normalizePlateSolveDefaultSolver(QString solver)
{
    solver = solver.trimmed().toLower();
    if (solver.isEmpty())
    {
        return QString();
    }

    if (solver == QStringLiteral("internal_gaia") || solver == QStringLiteral("internal gaia") ||
        solver == QStringLiteral("internal-gaia"))
    {
        return QStringLiteral("internal_gaia");
    }

    if (solver == QStringLiteral("astap"))
    {
        return QStringLiteral("astap");
    }

    if (solver == QStringLiteral("astrometry_web") || solver == QStringLiteral("astrometry web") ||
        solver == QStringLiteral("astrometry.net (web)") || solver == QStringLiteral("astrometry.net web"))
    {
        return QStringLiteral("astrometry_web");
    }

    if (solver == QStringLiteral("astrometry_local") || solver == QStringLiteral("astrometry local") ||
        solver == QStringLiteral("astrometry.net (local)") || solver == QStringLiteral("solve-field") ||
        solver == QStringLiteral("astrometry.net"))
    {
        return QStringLiteral("astrometry_local");
    }

    return QString();
}

struct PlateSolveSettings
{
    QString defaultSolver;
    QString astapPath;
    QString astrometryLocalPath;
    QString astrometryApiKey;
    QString indexPath;
    QString gaiaCatalogDirectory;
    QString gaiaCatalogPack = QStringLiteral("gaia_dr3_g14");
    double searchRadius = 10.0;
    int downsample = 2;
    int timeoutSeconds = 120;
    bool useHints = true;

    // Equipment hints for scale estimation (used by the workflow registration path).
    // When set, these override values extracted from FITS keywords.
    std::optional<double> focalLengthMm;
    std::optional<double> pixelSizeUm;
};

struct PlateSolveRequest
{
    QString imagePath;

    /// The image's metadata: its header cards state the hints and any solution it carries, its acquisition record the
    /// epoch. Absent, the solve reads the file's.
    std::optional<an::ImageMetadata> existingMetadata;

    int imageWidth = 0;
    int imageHeight = 0;
    QString workDirectoryRoot;
    QString requestTag;
    // Gates all automatic solver backends. Embedded WCS can still be returned when present.
    bool allowExternalSolve = true;
    bool forceExternalSolve = false;
    // When true, ignore any embedded WCS solution and always run a solver.
    bool ignoreEmbeddedWcs = false;
    // When false, only explicit request position hints may constrain the solve.
    bool allowMetadataPositionHints = true;
    // Workflow safety gates for automatic final-master recovery.
    bool requirePositionHintForInternalGaia = false;
    bool validateSolutionAgainstHints = false;

    // User-provided hints that override metadata-extracted values.
    std::optional<double> hintRaDeg;
    std::optional<double> hintDecDeg;
    std::optional<double> focalLengthMm;
    std::optional<double> pixelSizeUm;
};

struct PlateSolveResult
{
    bool succeeded = false;
    bool cancelled = false;

    /// The solution, in engine pixel coordinates of the solved frame - the first pixel's centre at one half - with its
    /// polynomial distortion when the solver fitted one.
    std::optional<an::WcsSolution> solution;

    QString solverUsed;
    double solveTimeMs = 0.0;
    QString errorMessage;

    /// The scale of a square pixel covering the same solid angle as the solution's pixel.
    double imageScaleArcsecPerPixel = 0.0;
    std::optional<double> derivedFocalLengthMm;
    double fieldCenterRaDeg = 0.0;
    double fieldCenterDecDeg = 0.0;

    /// Which way celestial north points: the solution's orientation, measured from increasing y toward decreasing x.
    double rotationDeg = 0.0;
    double fovWidthArcmin = 0.0;
    double fovHeightArcmin = 0.0;
};

} // namespace an::ui

Q_DECLARE_METATYPE(an::ui::PlateSolveResult)
W_REGISTER_ARGTYPE(an::ui::PlateSolveResult)
