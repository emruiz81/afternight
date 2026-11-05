// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A star field the internal plate solve can solve: a rendered frame of forty Gaussian stars at irregular positions,
/// where a known solution puts them, and a Gaia chunk catalog that holds those stars - installed as the downloader
/// installs one - with the target and optics the frame's cards state.
#pragma once

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "Test_EngineAstrometry/GaiaTestSupport.h"

#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>

#include <QCryptographicHash>
#include <QDir>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace an::tests::astrometry::field
{

constexpr std::uint32_t kWidth = 512u;
constexpr std::uint32_t kHeight = 384u;
constexpr double kScaleArcsec = 2.4;
constexpr std::uint32_t kStars = 40u;
constexpr an::SkyCoordinate kCenter{.rightAscensionDegrees = 83.6331, .declinationDegrees = 22.0145};

/// The pixelisation the fixture catalog is written in: order 5, ring numbering, 12,288 tiles.
constexpr an::HealpixGrid kGrid{.order = 5u, .ordering = an::HealpixOrdering::Ring};

/// Where the rendered field points: square pixels of 2.4 arcseconds, turned 17 degrees, about the frame's centre.
[[nodiscard]] inline an::WcsSolution truthSolution()
{
    constexpr double angle = 17.0 * std::numbers::pi / 180.0;
    const double scaleDegrees = kScaleArcsec / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * kWidth, .y = 0.5 * kHeight};
    request.referencePoint = kCenter;
    request.linear = {.cd11 = -scaleDegrees * std::cos(angle),
                      .cd12 = scaleDegrees * std::sin(angle),
                      .cd21 = scaleDegrees * std::sin(angle),
                      .cd22 = scaleDegrees * std::cos(angle)};
    request.imageWidth = kWidth;
    request.imageHeight = kHeight;
    const an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// The field's stars: irregular positions inside a margin, the first the brightest.
[[nodiscard]] inline std::vector<an::PixelCoordinate> starPositions()
{
    std::vector<an::PixelCoordinate> positions;
    std::uint32_t state = 0x6d2b79f5u;
    const auto next = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (std::uint32_t ordinal = 0u; ordinal < kStars; ++ordinal)
    {
        positions.push_back({.x = 24.5 + static_cast<double>(next() % 463000u) / 1000.0,
                             .y = 24.5 + static_cast<double>(next() % 335000u) / 1000.0});
    }
    return positions;
}

/// A deterministic speckle of a few thousandths, so the background has a noise to measure.
[[nodiscard]] inline float speckle(const std::uint32_t x, const std::uint32_t y) noexcept
{
    const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u) ^ 0x9e3779b9u;
    return (static_cast<float>(hash % 1000u) / 1000.0f - 0.5f) * 0.004f;
}

/// The rendered field: Gaussian stars of a sigma of 1.6 pixels over a speckled background, fading with their ordinal.
[[nodiscard]] inline float fieldSample(const std::vector<an::PixelCoordinate>& stars, const std::uint32_t x,
                                       const std::uint32_t y)
{
    // A sample's value is its pixel's centre, at one half past each whole pixel.
    const double px = static_cast<double>(x) + 0.5;
    const double py = static_cast<double>(y) + 0.5;
    double value = 0.05 + static_cast<double>(speckle(x, y));
    for (std::uint32_t ordinal = 0u; ordinal < stars.size(); ++ordinal)
    {
        const double dx = px - stars[ordinal].x;
        const double dy = py - stars[ordinal].y;
        const double squared = dx * dx + dy * dy;
        if (squared < 100.0)
        {
            value += (0.9 - 0.018 * static_cast<double>(ordinal)) * std::exp(-squared / (2.0 * 1.6 * 1.6));
        }
    }
    return static_cast<float>(value);
}

/// One catalog star of the fixture.
struct CatalogStar
{
    an::SkyCoordinate direction{};
    float magnitude = 0.0f;
};

/// The catalog's stars: every star of the field where the truth places it, its magnitude rising with its ordinal, and
/// a few stars far from the field.
[[nodiscard]] inline std::vector<CatalogStar> catalogStars()
{
    const an::WcsSolution truth = truthSolution();
    std::vector<CatalogStar> stars;
    const std::vector<an::PixelCoordinate> positions = starPositions();
    for (std::uint32_t ordinal = 0u; ordinal < positions.size(); ++ordinal)
    {
        const an::Result<an::SkyCoordinate> direction = truth.pixelToWorld(positions[ordinal]);
        EXPECT_TRUE(direction.hasValue()) << direction.status().message();
        stars.push_back(CatalogStar{.direction = *direction, .magnitude = 8.0f + 0.12f * static_cast<float>(ordinal)});
    }
    // A faint ring seven tenths of a degree out, which a half-degree cone's tiles reach and the cone itself does not.
    for (int step = 0; step < 6; ++step)
    {
        const double angle = static_cast<double>(step) * std::numbers::pi / 3.0;
        const double cosDeclination = std::cos(kCenter.declinationDegrees * std::numbers::pi / 180.0);
        stars.push_back(
            CatalogStar{.direction = {.rightAscensionDegrees =
                                          kCenter.rightAscensionDegrees + 0.7 * std::cos(angle) / cosDeclination,
                                      .declinationDegrees = kCenter.declinationDegrees + 0.7 * std::sin(angle)},
                        .magnitude = 13.5f});
    }
    stars.push_back(
        CatalogStar{.direction = {.rightAscensionDegrees = 200.0, .declinationDegrees = -40.0}, .magnitude = 6.0f});
    stars.push_back(
        CatalogStar{.direction = {.rightAscensionDegrees = 10.0, .declinationDegrees = 60.0}, .magnitude = 7.0f});
    return stars;
}

/// Writes a one-chunk Gaia catalog of the stars into a directory, as the downloader installs one: the chunk, its
/// manifest and the marker of a finished install.
[[nodiscard]] inline bool writeGaiaCatalog(const QString& directory, const std::vector<CatalogStar>& stars)
{
    const std::uint32_t tileCount = an::healpixPixelCount(kGrid);
    std::vector<gaia_test::FixtureTile> tiles(tileCount);
    std::uint64_t identifier = 1u;
    for (const CatalogStar& star : stars)
    {
        const an::Result<an::HealpixPixelIndex> tile = an::skyCoordinateToHealpixPixel(kGrid, star.direction);
        if (!tile.hasValue())
        {
            return false;
        }
        gaia_test::FixtureStar record;
        record.rightAscensionDegrees = star.direction.rightAscensionDegrees;
        record.declinationDegrees = star.direction.declinationDegrees;
        record.sourceIdentifier = identifier++;
        record.magnitudeG = star.magnitude;
        record.magnitudeBp = star.magnitude + 0.4f;
        record.magnitudeRp = star.magnitude - 0.5f;
        tiles[tile->value].stars.push_back(record);
    }
    const std::vector<std::byte> chunk = gaia_test::makeChunkFile(tiles);
    const QByteArray chunkBytes(reinterpret_cast<const char*>(chunk.data()), static_cast<qsizetype>(chunk.size()));
    const QString chunkName = QStringLiteral("gaia_dr3_chunk_00.ansp");

    an::GaiaChunkDescriptor descriptor;
    descriptor.index = 0;
    descriptor.file = chunkName.toStdString();
    descriptor.firstTile = 0;
    descriptor.tileCount = static_cast<std::int32_t>(tileCount);
    descriptor.recordCount = stars.size();
    descriptor.sizeBytes = chunk.size();
    descriptor.checksum = QCryptographicHash::hash(chunkBytes, QCryptographicHash::Sha256).toHex().toStdString();
    const std::string manifest =
        gaia_test::makeChunkManifestText(std::span<const an::GaiaChunkDescriptor>(&descriptor, 1u));

    return writeFileBytes(QDir(directory).filePath(chunkName), chunkBytes) &&
           writeFileBytes(QDir(directory).filePath(QStringLiteral("catalog_meta.json")),
                          QByteArray::fromStdString(manifest)) &&
           writeFileBytes(QDir(directory).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n"));
}

/// The target a few arcminutes from the field's centre and optics of 2.4 arcseconds a pixel, as a capture program
/// writes them.
[[nodiscard]] inline std::vector<an::CodecCard> hintCards()
{
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJCTRA", "'05 34 45'");
    addCard(cards, "OBJCTDEC", "'+22 03 00'");
    addCard(cards, "FOCALLEN", "323.15");
    addCard(cards, "XPIXSZ", "3.76");
    return cards;
}

/// The rendered field as an asset of a session carrying metadata; fails the case when it cannot be built.
[[nodiscard]] inline std::optional<an::consumer::ImageAsset> makeFieldAsset(const an::consumer::EngineSession& session,
                                                                            an::ImageMetadata metadata)
{
    const std::vector<an::PixelCoordinate> stars = starPositions();
    return makeAssetWithMetadata<float>(
        session, kWidth, kHeight, Channels::Grey,
        [&stars](const std::uint32_t x, const std::uint32_t y, std::uint32_t) { return fieldSample(stars, x, y); },
        std::move(metadata));
}

} // namespace an::tests::astrometry::field
