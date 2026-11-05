// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour calibrations' fields, catalogs, repository, targets and window helpers.

#include "Test_ProcessesPhotometric/PhotometricTestSupport.h"

#include "processes/core/RegistrationMetricsWriter.h"
#include "Test_EngineAstrometry/GaiaTestSupport.h"

#include "controls/ImageViewFiles.h"
#include "controls/SequenceContainers.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QFileInfo>
#include <QDateTime>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <thread>
#include <type_traits>
#include <utility>

namespace an::tests::processes::photometric
{

namespace
{

constexpr double kStartNm = an::kXpSampledWavelengthStartNm;
constexpr double kEndNm = an::kXpSampledWavelengthEndNm;

/// A deterministic value in [0, 1) from a key.
[[nodiscard]] double unit(std::uint64_t key) noexcept
{
    key ^= key >> 33u;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33u;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33u;
    return static_cast<double>(key >> 11u) * (1.0 / 9007199254740992.0);
}

/// A deterministic speckle of a few thousandths, so every star carries a signal-to-noise figure.
[[nodiscard]] double speckle(std::uint32_t x, std::uint32_t y, std::uint32_t channel) noexcept
{
    const double a = unit((static_cast<std::uint64_t>(channel) << 48u) ^ (static_cast<std::uint64_t>(y) << 24u) ^ x);
    const double b =
        unit((static_cast<std::uint64_t>(channel + 7u) << 48u) ^ (static_cast<std::uint64_t>(x) << 24u) ^ y);
    return (a + b - 1.0) * 0.0025;
}

/// The solution of a field: 2.4 arcseconds a pixel, turned 17 degrees about the frame's centre.
[[nodiscard]] std::optional<an::WcsSolution> fieldSolution(std::uint32_t width, std::uint32_t height,
                                                           an::SkyCoordinate center)
{
    constexpr double angle = 17.0 * std::numbers::pi / 180.0;
    const double scaleDegrees = kScaleArcsec / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * width, .y = 0.5 * height};
    request.referencePoint = center;
    request.linear = {.cd11 = -scaleDegrees * std::cos(angle),
                      .cd12 = scaleDegrees * std::sin(angle),
                      .cd21 = scaleDegrees * std::sin(angle),
                      .cd22 = scaleDegrees * std::cos(angle)};
    request.imageWidth = width;
    request.imageHeight = height;
    an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::optional<an::WcsSolution>(std::move(*made)) : std::nullopt;
}

/// Renders a field's stars, round Gaussians of a sigma of 1.6 pixels, over its sky and its speckle.
void render(StarField& field)
{
    const std::size_t plane = static_cast<std::size_t>(field.width) * field.height;
    field.samples.assign(plane * 3u, 0.0f);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < field.height; ++y)
        {
            for (std::uint32_t x = 0u; x < field.width; ++x)
            {
                field.samples[channel * plane + static_cast<std::size_t>(y) * field.width + x] =
                    static_cast<float>(field.background[channel] + speckle(x, y, channel));
            }
        }
    }
    constexpr double sigma = 1.6;
    constexpr double reach = 8.0;
    for (const PlantedStar& star : field.stars)
    {
        const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
        const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
        for (std::int64_t y = (std::max)(std::int64_t{0}, top);
             y <= (std::min)(static_cast<std::int64_t>(field.height) - 1, top + 2 * static_cast<std::int64_t>(reach));
             ++y)
        {
            for (std::int64_t x = (std::max)(std::int64_t{0}, left);
                 x <=
                 (std::min)(static_cast<std::int64_t>(field.width) - 1, left + 2 * static_cast<std::int64_t>(reach));
                 ++x)
            {
                // A sample's value is its pixel's centre, at one half past each whole pixel.
                const double dx = static_cast<double>(x) + 0.5 - star.x;
                const double dy = static_cast<double>(y) + 0.5 - star.y;
                const double shape = std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    field.samples[channel * plane + static_cast<std::size_t>(y) * field.width +
                                  static_cast<std::size_t>(x)] += static_cast<float>(star.amplitude[channel] * shape);
                }
            }
        }
    }
}

/// The colour model the photometric calibration predicts each star's channel ratios from.
[[nodiscard]] std::array<double, 2u> colorModel(double colorIndex)
{
    const double clamped = std::clamp(colorIndex, -0.5, 3.5);
    return {std::exp(0.32 * clamped), std::exp(-0.40 * clamped)};
}

/// The XP grid's wavelength of a sample.
[[nodiscard]] double gridWavelength(std::uint32_t index)
{
    return kStartNm + an::kXpSampledWavelengthStepNm * static_cast<double>(index);
}

/// A bandpass sampled where its author sampled it.
struct Bandpass
{
    const char* id;
    const char* channel;
    std::vector<std::pair<double, double>> samples;
};

/// The repository's three components: broad red, green and blue passbands that tell a star's colour apart.
[[nodiscard]] const std::array<Bandpass, 3u>& bandpasses()
{
    static const std::array<Bandpass, 3u> curves{
        Bandpass{"afternight.spcc.osc_sensor.testcam.red",
                 "red",
                 {{kStartNm, 0.0}, {600.0, 0.0}, {680.0, 1.0}, {820.0, 1.0}, {kEndNm, 0.0}}},
        Bandpass{"afternight.spcc.osc_sensor.testcam.green",
                 "green",
                 {{kStartNm, 0.0}, {480.0, 0.0}, {540.0, 1.0}, {620.0, 1.0}, {700.0, 0.0}, {kEndNm, 0.0}}},
        Bandpass{"afternight.spcc.osc_sensor.testcam.blue",
                 "blue",
                 {{kStartNm, 0.0}, {380.0, 1.0}, {500.0, 1.0}, {560.0, 0.0}, {kEndNm, 0.0}}},
    };
    return curves;
}

/// The response the repository's profile composes to, built directly, which the field's colours are integrated through.
[[nodiscard]] an::ResponseProfile directProfile()
{
    an::ResponseProfile profile;
    profile.identifier = kSpccProfileId;
    profile.version = "1";
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        profile.channels[channel].identifier = bandpasses()[channel].id;
        profile.channels[channel].version = "1";
        for (const auto& [wavelength, value] : bandpasses()[channel].samples)
        {
            profile.channels[channel].samples.push_back({.wavelengthNm = wavelength, .value = value});
        }
    }
    return profile;
}

/// An engine and an execution of the fixture's own, for the integrations that state each star's colour.
struct FixtureEngine
{
    an::EngineContext engine;
    an::ExecutionContext execution;
};

[[nodiscard]] FixtureEngine& fixtureEngine()
{
    static FixtureEngine made = [] {
        an::EngineContextOptions options;
        options.maxConcurrency = 2u;
        options.limits.memoryBudgetBytes = 256u * 1024u * 1024u;
        an::Result<an::EngineContext> engine = an::EngineContext::create(options);
        EXPECT_TRUE(engine.hasValue()) << engine.status().message();
        an::ExecutionOptions executionOptions;
        executionOptions.maxConcurrency = 2u;
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, executionOptions);
        EXPECT_TRUE(execution.hasValue()) << execution.status().message();
        return FixtureEngine{.engine = std::move(*engine), .execution = std::move(*execution)};
    }();
    return made;
}

/// The bytes per record the format stores, and the quantization the fixture writes spectra at.
constexpr float kFluxScale = 1.0e-4f;
constexpr float kFluxErrorScale = 1.0e-5f;

void appendUnsigned(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t width)
{
    for (std::size_t index = 0u; index < width; ++index)
    {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xFFu));
    }
}

void appendFloat(std::vector<std::byte>& bytes, float value)
{
    appendUnsigned(bytes, std::bit_cast<std::uint32_t>(value), 4u);
}

void appendDouble(std::vector<std::byte>& bytes, double value)
{
    appendUnsigned(bytes, std::bit_cast<std::uint64_t>(value), 8u);
}

/// The whole sky at order six, which one chunk of a local full catalog declares.
constexpr std::int32_t kSkyTiles = 49152;

/// The spectrum chunk container of a field's stars.
[[nodiscard]] std::vector<std::byte> spectrumChunkBytes(const StarField& field)
{
    const std::string header = std::string("{\"catalog_family_id\":\"") + an::kXpCatalogFamilyId +
                               "\",\"manifest_id\":\"" + kSpccManifestId +
                               "\",\"chunk_index\":0,\"record_count\":" + std::to_string(field.stars.size()) +
                               ",\"record_size_bytes\":" + std::to_string(an::kXpChunkRecordBytes) +
                               ",\"first_tile\":0,\"tile_count\":" + std::to_string(kSkyTiles) + "}";
    std::vector<std::byte> bytes;
    for (const char value : std::string(an::kXpChunkMagic))
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    appendUnsigned(bytes, an::kXpChunkFormatVersion, 2u);
    appendUnsigned(bytes, an::kXpChunkRecordVersion, 2u);
    appendUnsigned(bytes, header.size(), 4u);
    appendUnsigned(bytes, field.stars.size(), 4u);
    for (const char value : header)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    for (const PlantedStar& star : field.stars)
    {
        const std::size_t begin = bytes.size();
        appendUnsigned(bytes, star.sourceIdentifier, 8u);
        appendDouble(bytes, star.direction.rightAscensionDegrees);
        appendDouble(bytes, star.direction.declinationDegrees);
        appendFloat(bytes, std::numeric_limits<float>::quiet_NaN());
        appendFloat(bytes, std::numeric_limits<float>::quiet_NaN());
        appendFloat(bytes, 2016.0f);
        appendFloat(bytes, star.magnitude);
        appendFloat(bytes, star.magnitude + 0.4f);
        appendFloat(bytes, star.magnitude - 0.5f);
        appendFloat(bytes, kFluxScale);
        appendFloat(bytes, kFluxErrorScale);
        appendUnsigned(bytes, 0u, 2u);
        appendUnsigned(bytes, an::kXpSampledWavelengthCount, 2u);
        bytes.push_back(std::byte{0});
        bytes.push_back(std::byte{0});
        for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; ++index)
        {
            const auto quantized = static_cast<std::int16_t>(std::lround(star.spectrum[index] / kFluxScale));
            appendUnsigned(bytes, static_cast<std::uint16_t>(quantized), 2u);
        }
        for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; ++index)
        {
            appendUnsigned(bytes, 100u, 2u);
        }
        while (bytes.size() - begin < an::kXpChunkRecordBytes)
        {
            bytes.push_back(std::byte{0});
        }
    }
    return bytes;
}

[[nodiscard]] QString sha256Of(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

/// The manifest of a field's spectrum catalog, with the chunk's size and digest.
[[nodiscard]] QJsonObject spectrumManifest(const StarField& field, const QByteArray& chunk,
                                           const std::map<QString, QString>& sidecarDigests)
{
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema_version"), QString::fromLatin1(an::kXpCatalogManifestSchema));
    manifest.insert(QStringLiteral("catalog_family_id"), QString::fromLatin1(an::kXpCatalogFamilyId));
    manifest.insert(QStringLiteral("manifest_id"), QString::fromLatin1(kSpccManifestId));
    manifest.insert(QStringLiteral("access_profile"), QStringLiteral("local-full"));
    manifest.insert(QStringLiteral("source_kind"), QStringLiteral("gaia_dr3_xp_sampled"));
    manifest.insert(QStringLiteral("payload_kind"), QStringLiteral("sampled_spectra"));
    manifest.insert(QStringLiteral("total_records"), static_cast<qint64>(field.stars.size()));
    manifest.insert(QStringLiteral("flux_unit"), QStringLiteral("W m-2 nm-1"));
    manifest.insert(QStringLiteral("rights"),
                    QJsonObject{{QStringLiteral("review_status"), QStringLiteral("approved-for-bundling")}});
    manifest.insert(
        QStringLiteral("files"),
        QJsonObject{{QStringLiteral("chunk_manifest"), QStringLiteral("chunk_manifest.json")},
                    {QStringLiteral("coverage_summary"), QStringLiteral("coverage_summary.json")},
                    {QStringLiteral("source_id_index"), QStringLiteral("source_id_index.json")},
                    {QStringLiteral("source_count_transcript"), QStringLiteral("source_count_transcript.json")}});
    for (const auto& [key, digest] : sidecarDigests)
    {
        manifest.insert(key, digest);
    }
    manifest.insert(
        QStringLiteral("format"),
        QJsonObject{{QStringLiteral("magic"), QString::fromLatin1(an::kXpChunkMagic)},
                    {QStringLiteral("format_version"), 1},
                    {QStringLiteral("record_version"), 1},
                    {QStringLiteral("byte_order"), QStringLiteral("little-endian")},
                    {QStringLiteral("record_size_bytes"), static_cast<int>(an::kXpChunkRecordBytes)},
                    {QStringLiteral("healpix_order"), static_cast<int>(an::kXpCatalogHealpixOrder)},
                    {QStringLiteral("chunk_parent_healpix_order"), static_cast<int>(an::kXpChunkParentHealpixOrder)}});
    manifest.insert(QStringLiteral("wavelength_grid"),
                    QJsonObject{{QStringLiteral("sample_count"), static_cast<int>(an::kXpSampledWavelengthCount)},
                                {QStringLiteral("start_nm"), kStartNm},
                                {QStringLiteral("step_nm"), an::kXpSampledWavelengthStepNm},
                                {QStringLiteral("end_nm"), kEndNm}});
    QJsonObject chunkEntry{{QStringLiteral("chunk_index"), 0},
                           {QStringLiteral("file"), QStringLiteral("chunks/chunk-0000.anxp")},
                           {QStringLiteral("first_tile"), 0},
                           {QStringLiteral("tile_count"), kSkyTiles},
                           {QStringLiteral("record_count"), static_cast<qint64>(field.stars.size())},
                           {QStringLiteral("size_bytes"), static_cast<qint64>(chunk.size())},
                           {QStringLiteral("sha256"), sha256Of(chunk)}};
    manifest.insert(QStringLiteral("chunks"), QJsonArray{chunkEntry});
    return manifest;
}

[[nodiscard]] bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

[[nodiscard]] std::optional<an::CodecCard> fitsCard(std::string keyword, std::string value)
{
    an::Result<an::CodecCard> card =
        an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::string());
    EXPECT_TRUE(card.hasValue()) << card.status().message();
    return card.hasValue() ? std::optional<an::CodecCard>(std::move(*card)) : std::nullopt;
}

template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

} // namespace

StarField makePhotometricField(const double redCast, const double blueCast)
{
    StarField field;
    field.width = 512u;
    field.height = 384u;
    field.solution =
        fieldSolution(field.width, field.height, {.rightAscensionDegrees = 83.6331, .declinationDegrees = 22.0145});
    std::uint32_t state = 0x6d2b79f5u;
    const auto next = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    constexpr std::uint32_t kStars = 60u;
    for (std::uint32_t ordinal = 0u; ordinal < kStars; ++ordinal)
    {
        PlantedStar star;
        star.x = 24.5 + static_cast<double>(next() % 463000u) / 1000.0;
        star.y = 24.5 + static_cast<double>(next() % 335000u) / 1000.0;
        star.colorIndex = -0.3 + 2.4 * static_cast<double>(ordinal) / static_cast<double>(kStars - 1u);
        star.magnitude = 8.0f + 0.07f * static_cast<float>(ordinal);
        const double green = 0.5 - 0.005 * static_cast<double>(ordinal);
        const std::array<double, 2u> model = colorModel(star.colorIndex);
        star.amplitude = {green * model[0] * redCast, green, green * model[1] * blueCast};
        if (field.solution.has_value())
        {
            const an::Result<an::SkyCoordinate> direction = field.solution->pixelToWorld({.x = star.x, .y = star.y});
            EXPECT_TRUE(direction.hasValue()) << direction.status().message();
            if (direction.hasValue())
            {
                star.direction = *direction;
            }
        }
        star.sourceIdentifier = ordinal + 1u;
        field.stars.push_back(std::move(star));
    }
    // Two stars close together would be one detection; the field keeps the first of any pair nearer than nine pixels.
    std::vector<PlantedStar> kept;
    for (PlantedStar& star : field.stars)
    {
        const bool crowded = std::any_of(kept.begin(), kept.end(), [&star](const PlantedStar& other) {
            return std::hypot(other.x - star.x, other.y - star.y) < 9.0;
        });
        if (!crowded)
        {
            kept.push_back(std::move(star));
        }
    }
    field.stars = std::move(kept);
    render(field);
    return field;
}

bool writeGaiaCatalog(const QString& directory, const StarField& field, const std::optional<std::size_t> starCount)
{
    const an::HealpixGrid grid{.order = 5u, .ordering = an::HealpixOrdering::Ring};
    const std::uint32_t tileCount = an::healpixPixelCount(grid);
    std::vector<gaia_test::FixtureTile> tiles(tileCount);
    const std::size_t count = (std::min)(starCount.value_or(field.stars.size()), field.stars.size());
    for (std::size_t index = 0u; index < count; ++index)
    {
        const PlantedStar& star = field.stars[index];
        const an::Result<an::HealpixPixelIndex> tile = an::skyCoordinateToHealpixPixel(grid, star.direction);
        if (!tile.hasValue())
        {
            return false;
        }
        gaia_test::FixtureStar record;
        record.rightAscensionDegrees = star.direction.rightAscensionDegrees;
        record.declinationDegrees = star.direction.declinationDegrees;
        record.sourceIdentifier = star.sourceIdentifier;
        record.magnitudeG = star.magnitude;
        record.magnitudeBp = star.magnitude + static_cast<float>(0.5 * star.colorIndex);
        record.magnitudeRp = star.magnitude - static_cast<float>(0.5 * star.colorIndex);
        record.properMotionRightAscension = 0.0f;
        record.properMotionDeclination = 0.0f;
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
    descriptor.recordCount = count;
    descriptor.sizeBytes = chunk.size();
    descriptor.checksum = sha256Of(chunkBytes).toStdString();
    const std::string manifest =
        gaia_test::makeChunkManifestText(std::span<const an::GaiaChunkDescriptor>(&descriptor, 1u));
    return writeFile(QDir(directory).filePath(chunkName), chunkBytes) &&
           writeFile(QDir(directory).filePath(QStringLiteral("catalog_meta.json")),
                     QByteArray::fromStdString(manifest)) &&
           writeFile(QDir(directory).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n"));
}

StarField makeSpectrophotometricField(const double redCast, const double blueCast)
{
    StarField field;
    field.width = 768u;
    field.height = 576u;
    field.solution =
        fieldSolution(field.width, field.height, {.rightAscensionDegrees = 180.0, .declinationDegrees = 12.0});
    constexpr std::uint32_t kCell = 48u;
    std::uint64_t ordinal = 0u;
    for (std::uint32_t cellY = 1u; cellY + 1u < field.height / kCell; ++cellY)
    {
        for (std::uint32_t cellX = 1u; cellX + 1u < field.width / kCell; ++cellX)
        {
            const std::uint64_t key = 977u + (static_cast<std::uint64_t>(cellY) << 24u) + (cellX << 4u);
            PlantedStar star;
            star.x = (static_cast<double>(cellX) + 0.3 + 0.4 * unit(key + 1u)) * kCell;
            star.y = (static_cast<double>(cellY) + 0.3 + 0.4 * unit(key + 2u)) * kCell;
            const double slope = -0.55 + 1.1 * unit(key + 4u);
            star.spectrum.reserve(an::kXpSampledWavelengthCount);
            for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; ++index)
            {
                const double normalized = (gridWavelength(index) - kStartNm) / (kEndNm - kStartNm) * 2.0 - 1.0;
                star.spectrum.push_back(static_cast<float>(1.0 + slope * normalized));
            }
            star.magnitude = 12.0f;
            if (field.solution.has_value())
            {
                const an::Result<an::SkyCoordinate> direction =
                    field.solution->pixelToWorld({.x = star.x, .y = star.y});
                EXPECT_TRUE(direction.hasValue()) << direction.status().message();
                if (direction.hasValue())
                {
                    star.direction = *direction;
                }
            }
            const an::Result<an::HealpixPixelIndex> tile =
                an::skyCoordinateToHealpixPixel(an::HealpixGrid{.order = an::kGaiaSourceIdentifierHealpixOrder,
                                                                .ordering = an::HealpixOrdering::Nested},
                                                star.direction);
            EXPECT_TRUE(tile.hasValue());
            star.sourceIdentifier =
                (tile.hasValue() ? tile->value : 0u) * an::kGaiaSourceIdentifierTileDivisor + ordinal + 1u;
            ++ordinal;
            field.stars.push_back(std::move(star));
        }
    }

    // Each star's catalog colour is what the engine integrates of the spectrum the catalog stores, read back from the
    // very container the catalog is written as, through the response the repository composes.
    FixtureEngine& fixture = fixtureEngine();
    const std::vector<std::byte> bytes = spectrumChunkBytes(field);
    const QJsonObject manifest = spectrumManifest(
        field, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())), {});
    const QByteArray manifestText = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    an::Result<an::SpectrumCatalogManifest> parsed = an::parseSpectrumCatalogManifest(
        fixture.execution, std::string_view(manifestText.constData(), static_cast<std::size_t>(manifestText.size())));
    EXPECT_TRUE(parsed.hasValue()) << parsed.status().message();
    an::Result<an::SpectrumChunk> decoded = parsed.hasValue()
                                                ? an::decodeSpectrumChunk(fixture.execution, bytes, *parsed, 0u)
                                                : an::Result<an::SpectrumChunk>(parsed.status());
    EXPECT_TRUE(decoded.hasValue()) << decoded.status().message();
    an::Result<an::PreparedResponse> prepared = an::prepareResponse(fixture.execution, directProfile());
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    if (decoded.hasValue() && prepared.hasValue())
    {
        const std::size_t samples = an::kXpSampledWavelengthCount;
        for (std::size_t index = 0u; index < field.stars.size(); ++index)
        {
            const std::span<const float> flux(decoded->flux.data() + index * samples, samples);
            const an::Result<an::SyntheticPhotometry> predicted =
                an::integrateSyntheticPhotometry(fixture.execution, *prepared, flux);
            EXPECT_TRUE(predicted.hasValue()) << predicted.status().message();
            if (predicted.hasValue())
            {
                field.stars[index].catalogRatios = predicted->relativeToGreen;
            }
        }
    }
    for (std::size_t index = 0u; index < field.stars.size(); ++index)
    {
        PlantedStar& star = field.stars[index];
        const double green = 0.15 + 0.3 * unit(9000u + index);
        star.amplitude = {green * star.catalogRatios[0] * redCast, green, green * star.catalogRatios[2] * blueCast};
    }
    render(field);
    return field;
}

std::array<double, 3u> expectedSpectrophotometricGains(const double redCast, const double blueCast)
{
    FixtureEngine& fixture = fixtureEngine();
    an::Result<an::PreparedResponse> prepared = an::prepareResponse(fixture.execution, directProfile());
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    an::WhiteReferenceSpectrum white;
    white.identifier = kSpccWhiteReferenceId;
    white.version = "1";
    white.samples = {{.wavelengthNm = kStartNm, .value = 1.0}, {.wavelengthNm = kEndNm, .value = 1.0}};
    const an::Result<std::array<double, 3u>> whiteGains =
        prepared.hasValue() ? an::whiteReferenceResponseGains(fixture.execution, *prepared, white)
                            : an::Result<std::array<double, 3u>>(prepared.status());
    EXPECT_TRUE(whiteGains.hasValue()) << whiteGains.status().message();
    if (!whiteGains.hasValue())
    {
        return {1.0, 1.0, 1.0};
    }
    const double whiteRed = (*whiteGains)[1] / (*whiteGains)[0];
    const double whiteBlue = (*whiteGains)[1] / (*whiteGains)[2];
    const std::array<double, 3u> raw{1.0 / (whiteRed * redCast), 1.0, 1.0 / (whiteBlue * blueCast)};
    const double largest = (std::max)((std::max)(raw[0], raw[1]), raw[2]);
    return {raw[0] / largest, raw[1] / largest, raw[2] / largest};
}

bool writeSpectrumCatalog(const QString& directory, const StarField& field)
{
    const QDir root(directory);
    const std::vector<std::byte> bytes = spectrumChunkBytes(field);
    const QByteArray chunk(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    std::map<QString, QString> digests;
    const std::array<std::pair<const char*, const char*>, 4u> sidecars{{
        {"chunk_manifest.json", "chunk_manifest_sha256"},
        {"coverage_summary.json", "coverage_summary_sha256"},
        {"source_id_index.json", "source_id_index_sha256"},
        {"source_count_transcript.json", "source_count_transcript_sha256"},
    }};
    for (const auto& [file, key] : sidecars)
    {
        const QByteArray content = QByteArrayLiteral("{\"manifest_id\":\"") + kSpccManifestId + "\"}";
        if (!writeFile(root.filePath(QString::fromLatin1(file)), content))
        {
            return false;
        }
        digests[QString::fromLatin1(key)] = sha256Of(content);
    }
    const QJsonObject manifest = spectrumManifest(field, chunk, digests);
    return writeFile(root.filePath(QStringLiteral("chunks/chunk-0000.anxp")), chunk) &&
           writeFile(root.filePath(QStringLiteral("catalog_meta.json")),
                     QJsonDocument(manifest).toJson(QJsonDocument::Compact)) &&
           writeFile(root.filePath(QStringLiteral(".install_complete")), QByteArray("ok\n"));
}

bool writeResponseRepository(const QString& directory, const bool reviewed)
{
    const QDir root(directory);
    QJsonArray components;
    QJsonArray componentIds;
    QJsonObject channelIds;
    for (const Bandpass& bandpass : bandpasses())
    {
        QJsonArray samples;
        for (const auto& [wavelength, value] : bandpass.samples)
        {
            samples.append(
                QJsonObject{{QStringLiteral("wavelength_nm"), wavelength}, {QStringLiteral("value"), value}});
        }
        const QString id = QString::fromLatin1(bandpass.id);
        const QString dataDigest = id + QStringLiteral("-data");
        const QJsonObject payload{{QStringLiteral("schema_version"), QStringLiteral("spcc-response-asset-v1")},
                                  {QStringLiteral("component_id"), id},
                                  {QStringLiteral("version"), QStringLiteral("1")},
                                  {QStringLiteral("channel"), QString::fromLatin1(bandpass.channel)},
                                  {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
                                  {QStringLiteral("value_semantics"), QStringLiteral("dimensionless-throughput")},
                                  {QStringLiteral("data_sha256"), dataDigest},
                                  {QStringLiteral("samples"), samples}};
        const QString file = QStringLiteral("components/%1.json").arg(QString::fromLatin1(bandpass.channel));
        const QByteArray content = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        if (!writeFile(root.filePath(file), content))
        {
            return false;
        }
        components.append(QJsonObject{{QStringLiteral("component_id"), id},
                                      {QStringLiteral("version"), QStringLiteral("1")},
                                      {QStringLiteral("file"), file},
                                      {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
                                      {QStringLiteral("value_semantics"), QStringLiteral("dimensionless-throughput")},
                                      {QStringLiteral("data_sha256"), dataDigest},
                                      {QStringLiteral("file_sha256"), sha256Of(content)}});
        componentIds.append(id);
        channelIds.insert(QString::fromLatin1(bandpass.channel), QJsonArray{id});
    }
    const QJsonObject whitePayload{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-asset-v1")},
        {QStringLiteral("white_reference_id"), QString::fromLatin1(kSpccWhiteReferenceId)},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("display_name"), QStringLiteral("Flat White")},
        {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
        {QStringLiteral("value_semantics"), QStringLiteral("relative-sed")},
        {QStringLiteral("data_sha256"), QStringLiteral("white-data")},
        {QStringLiteral("samples"),
         QJsonArray{QJsonObject{{QStringLiteral("wavelength_nm"), kStartNm}, {QStringLiteral("value"), 1.0}},
                    QJsonObject{{QStringLiteral("wavelength_nm"), kEndNm}, {QStringLiteral("value"), 1.0}}}}};
    const QByteArray whiteContent = QJsonDocument(whitePayload).toJson(QJsonDocument::Compact);
    if (!writeFile(root.filePath(QStringLiteral("white_references/flat.json")), whiteContent))
    {
        return false;
    }
    const QJsonObject warmPayload{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-asset-v1")},
        {QStringLiteral("white_reference_id"), QString::fromLatin1(kSpccWarmWhiteReferenceId)},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("display_name"), QStringLiteral("Warm White")},
        {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
        {QStringLiteral("value_semantics"), QStringLiteral("relative-sed")},
        {QStringLiteral("data_sha256"), QStringLiteral("warm-data")},
        {QStringLiteral("samples"),
         QJsonArray{QJsonObject{{QStringLiteral("wavelength_nm"), kStartNm}, {QStringLiteral("value"), 0.5}},
                    QJsonObject{{QStringLiteral("wavelength_nm"), kEndNm}, {QStringLiteral("value"), 1.0}}}}};
    const QByteArray warmContent = QJsonDocument(warmPayload).toJson(QJsonDocument::Compact);
    if (!writeFile(root.filePath(QStringLiteral("white_references/warm.json")), warmContent))
    {
        return false;
    }
    const QString rights = reviewed ? QStringLiteral("approved-for-bundling") : QStringLiteral("private-local");
    const QJsonObject profile{
        {QStringLiteral("profile_id"), QString::fromLatin1(kSpccProfileId)},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("component_ids"), componentIds},
        {QStringLiteral("channel_component_ids"), channelIds},
        {QStringLiteral("white_reference_id"), QString::fromLatin1(kSpccWhiteReferenceId)},
        {QStringLiteral("compatible_channel_bases"),
         QJsonArray{QStringLiteral("linear-rgb"), QStringLiteral("camera-rgb")}},
        {QStringLiteral("models_raw_white_balance"), false},
        {QStringLiteral("models_camera_matrix"), false},
        {QStringLiteral("rights"), QJsonObject{{QStringLiteral("review_status"), rights}}},
        {QStringLiteral("aliases"),
         QJsonArray{QJsonObject{{QStringLiteral("camera_model"), QString::fromLatin1(kSpccCameraModel)},
                                {QStringLiteral("filter"), QString()}}}}};
    const QJsonObject meta{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-repository-v1")},
        {QStringLiteral("repository_id"), QStringLiteral("spcc-response-test")},
        {QStringLiteral("version"), QStringLiteral("2026a")},
        {QStringLiteral("alias_registry_version"), QStringLiteral("aliases-2026a")},
        {QStringLiteral("rights"),
         QJsonObject{{QStringLiteral("review_status"), QStringLiteral("approved-for-bundling")}}},
        {QStringLiteral("deployment_scopes"), QJsonArray{QStringLiteral("deployed-runtime")}},
        {QStringLiteral("components"), components},
        {QStringLiteral("white_references"),
         QJsonArray{QJsonObject{{QStringLiteral("white_reference_id"), QString::fromLatin1(kSpccWhiteReferenceId)},
                                {QStringLiteral("version"), QStringLiteral("1")},
                                {QStringLiteral("file"), QStringLiteral("white_references/flat.json")},
                                {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
                                {QStringLiteral("value_semantics"), QStringLiteral("relative-sed")},
                                {QStringLiteral("data_sha256"), QStringLiteral("white-data")},
                                {QStringLiteral("file_sha256"), sha256Of(whiteContent)}},
                    QJsonObject{{QStringLiteral("white_reference_id"), QString::fromLatin1(kSpccWarmWhiteReferenceId)},
                                {QStringLiteral("version"), QStringLiteral("1")},
                                {QStringLiteral("file"), QStringLiteral("white_references/warm.json")},
                                {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
                                {QStringLiteral("value_semantics"), QStringLiteral("relative-sed")},
                                {QStringLiteral("data_sha256"), QStringLiteral("warm-data")},
                                {QStringLiteral("file_sha256"), sha256Of(warmContent)}}}},
        {QStringLiteral("throughput_profiles"), QJsonArray{profile}}};
    return writeFile(root.filePath(QStringLiteral("response_meta.json")),
                     QJsonDocument(meta).toJson(QJsonDocument::Compact));
}

an::ImageMetadata fieldMetadata(const StarField& field, const FieldMetadataOptions& options)
{
    std::vector<an::CodecCard> cards;
    if (!options.camera.empty())
    {
        if (std::optional<an::CodecCard> card = fitsCard("INSTRUME", "'" + options.camera + "'"); card.has_value())
        {
            cards.push_back(std::move(*card));
        }
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    if (!codec.hasValue())
    {
        return an::ImageMetadata{};
    }
    an::CodecMetadata written = std::move(*codec);
    if (options.solved && field.solution.has_value())
    {
        an::Result<an::CodecMetadata> solved = an::withWcsCards(written, *field.solution);
        EXPECT_TRUE(solved.hasValue()) << solved.status().message();
        if (solved.hasValue())
        {
            written = std::move(*solved);
        }
    }
    if (options.state.has_value())
    {
        an::Result<an::CodecMetadata> stated = an::withProcessingStateCards(written, *options.state);
        EXPECT_TRUE(stated.hasValue()) << stated.status().message();
        if (stated.hasValue())
        {
            written = std::move(*stated);
        }
    }
    an::Result<an::AcquisitionMetadata> acquisition = an::AcquisitionMetadata::create(
        an::AcquisitionMetadataRequest{.instrumentName = options.camera, .filterName = options.filter});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    return an::ImageMetadata::create(acquisition.hasValue() ? std::move(*acquisition) : an::AcquisitionMetadata{},
                                     an::ImageSemantics{}, an::AstrometricSolution{}, an::Provenance{},
                                     std::move(written), std::nullopt);
}

template <typename SampleT>
std::optional<an::consumer::ImageAsset> fieldAsset(const an::consumer::EngineSession& session, const StarField& field,
                                                   an::ImageMetadata metadata)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        hold->engine(), an::kSampleTypeOf<SampleT>, field.width, field.height, an::tests::viewer::Channels::Rgb);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Zeroed, std::move(metadata));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < field.height; ++y)
            {
                const auto row = access->view().row(channel, y);
                if (!row.has_value())
                {
                    return std::nullopt;
                }
                for (std::uint32_t x = 0u; x < field.width; ++x)
                {
                    const double value = std::clamp(static_cast<double>(field.value(x, y, channel)), 0.0, 1.0);
                    if constexpr (std::is_floating_point_v<SampleT>)
                    {
                        (*row)[x] = static_cast<SampleT>(field.value(x, y, channel));
                    }
                    else
                    {
                        // An integer is a fraction of its range: unsigned from zero, signed counted from its
                        // smallest value.
                        const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
                        const double width = static_cast<double>((std::numeric_limits<SampleT>::max)()) - lowest;
                        (*row)[x] = static_cast<SampleT>(std::llround(value * width + lowest));
                    }
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

template std::optional<an::consumer::ImageAsset> fieldAsset<float>(const an::consumer::EngineSession&, const StarField&,
                                                                   an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<double>(const an::consumer::EngineSession&,
                                                                    const StarField&, an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<std::uint8_t>(const an::consumer::EngineSession&,
                                                                          const StarField&, an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<std::int8_t>(const an::consumer::EngineSession&,
                                                                         const StarField&, an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<std::uint16_t>(const an::consumer::EngineSession&,
                                                                           const StarField&, an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<std::int16_t>(const an::consumer::EngineSession&,
                                                                          const StarField&, an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<std::uint32_t>(const an::consumer::EngineSession&,
                                                                           const StarField&, an::ImageMetadata);
template std::optional<an::consumer::ImageAsset> fieldAsset<std::int32_t>(const an::consumer::EngineSession&,
                                                                          const StarField&, an::ImageMetadata);

namespace
{

/// A read lease on what a document holds now, taken without blocking the GUI thread: while a writer holds the
/// document - a run publishes through the event loop - the loop turns until the lease is given or the patience ends,
/// so a case that reads too early fails rather than waits for ever.
[[nodiscard]] an::Result<an::consumer::AssetReadLease> readPatiently(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.tryRead();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!lease.hasValue() && lease.status().code() == an::ErrorCode::Busy &&
           std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        lease = document.tryRead();
    }
    return lease;
}

} // namespace

std::vector<double> documentValues(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = readPatiently(document);
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return {};
    }
    return std::visit(
        [](const auto& typed) -> std::vector<double> {
            using SampleT = SampleOf<decltype(typed)>;
            const auto view = typed.view();
            if (!view.has_value())
            {
                return {};
            }
            std::vector<double> values;
            values.reserve(static_cast<std::size_t>(view->width()) * view->height() * view->channelCount());
            for (std::uint32_t channel = 0u; channel < view->channelCount(); ++channel)
            {
                for (std::uint32_t y = 0u; y < view->height(); ++y)
                {
                    const auto row = view->row(channel, y);
                    if (!row.has_value())
                    {
                        return {};
                    }
                    for (std::uint32_t x = 0u; x < view->width(); ++x)
                    {
                        const double code = static_cast<double>((*row)[x]);
                        if constexpr (std::is_floating_point_v<SampleT>)
                        {
                            values.push_back(code);
                        }
                        else
                        {
                            const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
                            const double width = static_cast<double>((std::numeric_limits<SampleT>::max)()) - lowest;
                            values.push_back((code - lowest) / width);
                        }
                    }
                }
            }
            return values;
        },
        lease->image());
}

std::optional<an::ImageMetadata> documentMetadata(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = readPatiently(document);
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease->image());
}

std::optional<std::string> cardValue(const an::CodecMetadata& codec, const std::string_view keyword)
{
    for (const an::CodecCard& card : codec.cards())
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword)
        {
            return card.value();
        }
    }
    return std::nullopt;
}

double skyMedian(const StarField& field, const std::vector<double>& values, const std::uint32_t channel)
{
    std::vector<double> sky;
    for (std::uint32_t y = 0u; y < field.height; y += 3u)
    {
        for (std::uint32_t x = 0u; x < field.width; x += 3u)
        {
            const bool nearStar = std::any_of(field.stars.begin(), field.stars.end(), [x, y](const PlantedStar& star) {
                return std::hypot(static_cast<double>(x) + 0.5 - star.x, static_cast<double>(y) + 0.5 - star.y) < 10.0;
            });
            if (!nearStar)
            {
                sky.push_back(values[(static_cast<std::size_t>(channel) * field.height + y) * field.width + x]);
            }
        }
    }
    if (sky.empty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::nth_element(sky.begin(), sky.begin() + static_cast<std::ptrdiff_t>(sky.size() / 2u), sky.end());
    return sky[sky.size() / 2u];
}

void writeFieldRecording(const an::consumer::EngineSession& session, const QString& path, const StarField& field,
                         std::uint32_t frameCount)
{
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frameCount});
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    ASSERT_TRUE(scope.has_value());
    for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
    {
        StarField brighter = field;
        for (float& sample : brighter.samples)
        {
            sample = (std::min)(1.0f, sample + 0.01f * static_cast<float>(frame));
        }
        std::optional<an::consumer::ImageAsset> asset = fieldAsset<float>(session, brighter, an::ImageMetadata{});
        ASSERT_TRUE(asset.has_value());
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
        ASSERT_TRUE(appended.ok()) << appended.message();
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    ASSERT_TRUE(published.hasValue()) << published.status().message();
}

ViewHandle fieldSequenceView(const an::consumer::EngineSession& session, const QString& path, const StarField& field,
                             const an::ImageMetadata& metadata, std::uint32_t frameCount)
{
    std::optional<an::consumer::ImageAsset> first = fieldAsset<float>(session, field, metadata);
    EXPECT_TRUE(first.has_value());
    if (!first.has_value())
    {
        return {};
    }
    an::Result<an::ui::ImageDocument> made = an::ui::ImageDocument::create(std::move(*first));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return {};
    }
    an::ui::ImageDocument shown = std::move(*made);
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = frameCount;
    sequence.currentFrame = 0u;
    shown.setSequence(sequence);
    ViewHandle view(new ImageViewControl());
    view->initialize("sequence", ViewControlMode::standard);
    view->resize(640, 480);
    view->setSequenceDocument(std::move(shown), false);
    view->initializeNewImageHistory("Sequence opened");
    EXPECT_TRUE(an::tests::processes::settleView(*view));
    return view;
}

ViewHandle greyFieldView(const an::consumer::EngineSession& session, const StarField& field,
                         const an::ImageMetadata& metadata, const std::string& name)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return {};
    }
    const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        hold->engine(), an::SampleType::Float32, field.width, field.height, an::tests::viewer::Channels::Grey);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return {};
    }
    an::Result<an::Image<float>> image = an::Image<float>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return {};
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        if (!access.has_value())
        {
            return {};
        }
        for (std::uint32_t y = 0u; y < field.height; ++y)
        {
            const auto row = access->view().row(0u, y);
            if (!row.has_value())
            {
                return {};
            }
            for (std::uint32_t x = 0u; x < field.width; ++x)
            {
                (*row)[x] = field.value(x, y, 1u);
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return {};
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return {};
    }
    return an::tests::processes::makeTargetView(name, std::move(*document));
}

std::optional<an::ImageMetadata> writtenAndReadBack(const an::consumer::EngineSession& session,
                                                    const an::ui::ImageDocument& document, const QString& path)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document.asset();
    EXPECT_NE(asset, nullptr);
    if (asset == nullptr)
    {
        return std::nullopt;
    }
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32});
    EXPECT_TRUE(intent.hasValue()) << intent.status().message();
    if (!intent.hasValue())
    {
        return std::nullopt;
    }
    const an::Status written = an::ui::writeViewImage(session, *asset, path, *intent, {});
    EXPECT_TRUE(written.ok()) << written.message();
    if (!written.ok())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = path}, {}, nullptr);
    EXPECT_TRUE(read.hasValue()) << read.status().message();
    if (!read.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return std::visit([](const auto& typed) -> an::ImageMetadata { return typed.metadata(); }, lease->image());
}

void dropView(ProcessTestContext& context, ViewHandle& view)
{
    if (view == nullptr)
    {
        return;
    }
    context.removeViewControl(view.get(), false);
    view.reset();
    QCoreApplication::processEvents();
}

template <typename WindowT> void WindowDeleter<WindowT>::operator()(WindowT* window) const
{
    if (window == nullptr)
    {
        return;
    }
    (void)pumpUntil([window]() { return window->isRunIdle(); });
    delete window;
    QCoreApplication::processEvents();
}

template struct WindowDeleter<PhotometricColorCalibrationProcess>;
template struct WindowDeleter<SpectrophotometricColorCalibrationProcess>;

InstalledCatalogs::InstalledCatalogs(ProcessTestContext& context) : m_context(context)
{
    EXPECT_TRUE(m_root.isValid());
}

QString InstalledCatalogs::gaiaDirectory() const
{
    return m_root.filePath(QStringLiteral("gaia"));
}

QString InstalledCatalogs::spectrumDirectory() const
{
    return m_root.filePath(QStringLiteral("spcc"));
}

QString InstalledCatalogs::responseDirectory() const
{
    return m_root.filePath(QStringLiteral("response"));
}

void InstalledCatalogs::useGaiaCatalog()
{
    (void)m_context.upsert(QString::fromLatin1(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY), gaiaDirectory());
}

void InstalledCatalogs::useSpectrumCatalogAndRepository()
{
    (void)m_context.upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY), spectrumDirectory());
    (void)m_context.upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY),
                           responseDirectory());
}

WindowHandle<PhotometricColorCalibrationProcess> openPhotometric(ImageViewControl& target)
{
    WindowHandle<PhotometricColorCalibrationProcess> window(new PhotometricColorCalibrationProcess(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    return window;
}

WindowHandle<SpectrophotometricColorCalibrationProcess> openSpectrophotometric(ImageViewControl& target)
{
    WindowHandle<SpectrophotometricColorCalibrationProcess> window(
        new SpectrophotometricColorCalibrationProcess(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    EXPECT_TRUE(settleReadiness(*window));
    return window;
}

bool settlePhotometric(const PhotometricColorCalibrationProcess& window, const ImageViewControl& target)
{
    if (!pumpUntil([&window]() { return window.isRunIdle(); }))
    {
        return false;
    }
    return an::tests::processes::settleView(target);
}

bool settleReadiness(const SpectrophotometricColorCalibrationProcess& window)
{
    if (!pumpUntil([&window]() { return !window.readinessPending() && window.isRunIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return !window.readinessPending();
}

bool applyPhotometric(PhotometricColorCalibrationProcess& window, const ImageViewControl& target)
{
    window.applyCalibration();
    return settlePhotometric(window, target);
}

bool applySpectrophotometric(SpectrophotometricColorCalibrationProcess& window, const ImageViewControl& target)
{
    if (!settleReadiness(window))
    {
        return false;
    }
    window.executeCalibration();
    if (!pumpUntil([&window]() { return window.isProcessIdle() && window.isRunIdle(); }))
    {
        return false;
    }
    return an::tests::processes::settleView(target) && settleReadiness(window);
}

void writeWorkflowArchive(const QString& directory, const QString& outputFilename, const QString& registrationGroupKey)
{
    const QJsonObject group{{QStringLiteral("groupKey"), QStringLiteral("L|M31")},
                            {QStringLiteral("outputFilename"), outputFilename},
                            {QStringLiteral("registrationGroupKey"), registrationGroupKey}};
    const QJsonObject manifest{{QStringLiteral("outputDirectory"), directory},
                               {QStringLiteral("integrationGroups"), QJsonArray{group}}};
    EXPECT_TRUE(writeFile(QDir(directory).filePath(QStringLiteral("workflow_manifest.json")),
                          QJsonDocument(QJsonObject{{QStringLiteral("manifest"), manifest}}).toJson()));
}

void writeHandoffDatabase(const RecordedHandoff& recorded)
{
    an::ui::RegistrationMetricsWriter writer(recorded.metricsDatabasePath, QStringLiteral("PhotometricHandoffCase"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    an::ui::RegistrationGroupState state;
    state.groupKey = recorded.registrationGroupKey;
    state.settingsVersion = recorded.settingsVersion;
    // The hashes a registration states are not what the hand-off reads; each is stated, as a null text is no value.
    state.detectionHash = QStringLiteral("detection");
    state.registrationHash = QStringLiteral("registration");
    state.referenceFrameId = QStringLiteral("light_0");
    state.lightFilesHash = QStringLiteral("lights");
    state.calibrationHash = QStringLiteral("calibration");
    state.outputWcsJson = QStringLiteral("{}");
    state.updatedAt = now;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
    const QString frame =
        QDir(QFileInfo(recorded.metricsDatabasePath).absolutePath()).filePath(QStringLiteral("light_0.fits"));
    const an::ui::RegistrationMatchedSourceReplacement matched{
        .frameId = FrameId(frame), .references = recorded.references, .updatedAt = now};
    ASSERT_TRUE(writer.replaceMatchedSourcesForGroup(recorded.registrationGroupKey, recorded.settingsVersion, {matched},
                                                     &error))
        << error.toStdString();
}

} // namespace an::tests::processes::photometric
