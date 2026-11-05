// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star profilers' fields, catalog, targets and window helpers.

#include "Test_ProcessesStarProfilers/StarProfilerTestSupport.h"

#include "Test_EngineAstrometry/GaiaTestSupport.h"

#include "controls/SequenceContainers.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/IO/StarProfileFile.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTableWidgetItem>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <utility>

namespace an::tests::processes::stars
{

namespace
{

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

/// A deterministic speckle of a few thousandths, so the sky has a noise the detector measures.
[[nodiscard]] double speckle(std::uint32_t x, std::uint32_t y) noexcept
{
    const double a = unit((static_cast<std::uint64_t>(y) << 24u) ^ x);
    const double b = unit((static_cast<std::uint64_t>(x + 7u) << 24u) ^ y);
    return (a + b - 1.0) * 0.0025;
}

/// The solution of a field: 2.4 arcseconds a pixel, turned 17 degrees about the frame's centre.
[[nodiscard]] std::optional<an::WcsSolution> fieldSolution(std::uint32_t width, std::uint32_t height)
{
    constexpr double angle = 17.0 * std::numbers::pi / 180.0;
    const double scaleDegrees = 2.4 / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * width, .y = 0.5 * height};
    request.referencePoint = {.rightAscensionDegrees = 83.6331, .declinationDegrees = 22.0145};
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

void render(StarField& field)
{
    const std::size_t plane = static_cast<std::size_t>(field.width) * field.height;
    field.samples.assign(plane * field.channels, 0.0f);
    for (std::uint32_t channel = 0u; channel < field.channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < field.height; ++y)
        {
            for (std::uint32_t x = 0u; x < field.width; ++x)
            {
                field.samples[channel * plane + static_cast<std::size_t>(y) * field.width + x] =
                    static_cast<float>(field.background + speckle(x, y));
            }
        }
    }
    for (const PlantedStar& star : field.stars)
    {
        const double reach = 5.0 * star.sigma;
        const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
        const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
        const auto right = static_cast<std::int64_t>(std::ceil(star.x + reach));
        const auto bottom = static_cast<std::int64_t>(std::ceil(star.y + reach));
        for (std::int64_t y = (std::max)(std::int64_t{0}, top);
             y <= (std::min)(static_cast<std::int64_t>(field.height) - 1, bottom); ++y)
        {
            for (std::int64_t x = (std::max)(std::int64_t{0}, left);
                 x <= (std::min)(static_cast<std::int64_t>(field.width) - 1, right); ++x)
            {
                // A sample's value is its pixel's centre, at one half past each whole pixel.
                const double dx = static_cast<double>(x) + 0.5 - star.x;
                const double dy = static_cast<double>(y) + 0.5 - star.y;
                const double shape = std::exp(-(dx * dx + dy * dy) / (2.0 * star.sigma * star.sigma));
                for (std::uint32_t channel = 0u; channel < field.channels; ++channel)
                {
                    field.samples[channel * plane + static_cast<std::size_t>(y) * field.width +
                                  static_cast<std::size_t>(x)] += static_cast<float>(star.amplitude * shape);
                }
            }
        }
    }
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

template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

} // namespace

StarField makeStarField(const std::uint32_t width, const std::uint32_t height, const std::uint32_t channels)
{
    StarField field;
    field.width = width;
    field.height = height;
    field.channels = channels;
    field.solution = fieldSolution(width, height);
    std::uint32_t state = 0x2545f491u;
    const auto next = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    constexpr double kMargin = 20.0;
    constexpr double kSeparation = 16.0;
    const std::uint32_t wanted = (width * height) / 2600u;
    for (std::uint32_t attempt = 0u; attempt < wanted * 20u && field.stars.size() < wanted; ++attempt)
    {
        PlantedStar star;
        star.x = kMargin + static_cast<double>(next() % 100000u) / 100000.0 * (width - 2.0 * kMargin);
        star.y = kMargin + static_cast<double>(next() % 100000u) / 100000.0 * (height - 2.0 * kMargin);
        const bool crowded = std::any_of(field.stars.begin(), field.stars.end(), [&star](const PlantedStar& other) {
            return std::hypot(other.x - star.x, other.y - star.y) < kSeparation;
        });
        if (crowded)
        {
            continue;
        }
        const std::size_t ordinal = field.stars.size();
        star.amplitude = 0.25 + 0.35 * static_cast<double>(ordinal % 7u) / 6.0;
        star.sigma = 1.4 + 0.1 * static_cast<double>(ordinal % 4u);
        star.magnitude = 9.0f + 0.1f * static_cast<float>(ordinal);
        star.sourceIdentifier = 1000u + ordinal;
        if (field.solution.has_value())
        {
            const an::Result<an::SkyCoordinate> direction = field.solution->pixelToWorld({.x = star.x, .y = star.y});
            EXPECT_TRUE(direction.hasValue()) << direction.status().message();
            if (direction.hasValue())
            {
                star.direction = *direction;
            }
        }
        field.stars.push_back(star);
    }
    render(field);
    return field;
}

NearestStar nearestPlanted(const StarField& field, const double x, const double y)
{
    NearestStar nearest;
    nearest.distance = std::numeric_limits<double>::infinity();
    for (const PlantedStar& star : field.stars)
    {
        const double distance = std::hypot(star.x - x, star.y - y);
        if (distance < nearest.distance)
        {
            nearest.distance = distance;
            nearest.star = &star;
        }
    }
    return nearest;
}

std::size_t plantedStarsFound(const StarField& field, const an::ui::StarList& stars, const double tolerance)
{
    return static_cast<std::size_t>(
        std::count_if(field.stars.begin(), field.stars.end(), [&stars, tolerance](const PlantedStar& planted) {
            return std::any_of(stars.begin(), stars.end(), [&planted, tolerance](const an::DetectedStar& star) {
                return std::hypot(star.positionX - planted.x, star.positionY - planted.y) <= tolerance;
            });
        }));
}

bool writeGaiaCatalog(const QString& directory, const StarField& field)
{
    const an::HealpixGrid grid{.order = 5u, .ordering = an::HealpixOrdering::Ring};
    const std::uint32_t tileCount = an::healpixPixelCount(grid);
    std::vector<gaia_test::FixtureTile> tiles(tileCount);
    for (const PlantedStar& star : field.stars)
    {
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
        record.magnitudeBp = star.magnitude + 0.3f;
        record.magnitudeRp = star.magnitude - 0.3f;
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
    descriptor.recordCount = field.stars.size();
    descriptor.sizeBytes = chunk.size();
    descriptor.checksum =
        QString::fromLatin1(QCryptographicHash::hash(chunkBytes, QCryptographicHash::Sha256).toHex()).toStdString();
    const std::string manifest =
        gaia_test::makeChunkManifestText(std::span<const an::GaiaChunkDescriptor>(&descriptor, 1u));
    return writeFile(QDir(directory).filePath(chunkName), chunkBytes) &&
           writeFile(QDir(directory).filePath(QStringLiteral("catalog_meta.json")),
                     QByteArray::fromStdString(manifest)) &&
           writeFile(QDir(directory).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n"));
}

an::ImageMetadata fieldMetadata(const StarField& field, const bool solved)
{
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{});
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    if (!codec.hasValue())
    {
        return an::ImageMetadata{};
    }
    an::CodecMetadata written = std::move(*codec);
    if (solved && field.solution.has_value())
    {
        an::Result<an::CodecMetadata> withSolution = an::withWcsCards(written, *field.solution);
        EXPECT_TRUE(withSolution.hasValue()) << withSolution.status().message();
        if (withSolution.hasValue())
        {
            written = std::move(*withSolution);
        }
    }
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.instrumentName = "Test Camera"});
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
        hold->engine(), an::kSampleTypeOf<SampleT>, field.width, field.height,
        field.channels == 3u ? an::tests::viewer::Channels::Rgb : an::tests::viewer::Channels::Grey);
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
        for (std::uint32_t channel = 0u; channel < field.channels; ++channel)
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
                        const double span = static_cast<double>((std::numeric_limits<SampleT>::max)()) - lowest;
                        (*row)[x] = static_cast<SampleT>(std::llround(value * span + lowest));
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

template <typename SampleT>
ViewHandle fieldView(const an::consumer::EngineSession& session, const StarField& field,
                     const an::ImageMetadata& metadata, const std::string& name)
{
    std::optional<an::consumer::ImageAsset> asset = fieldAsset<SampleT>(session, field, metadata);
    if (!asset.has_value())
    {
        return ViewHandle();
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return ViewHandle();
    }
    return an::tests::processes::makeTargetView(name, std::move(*document));
}

#define AN_STAR_FIELD_REPRESENTATION(SampleT)                                                                  \
    template std::optional<an::consumer::ImageAsset> fieldAsset<SampleT>(const an::consumer::EngineSession&,   \
                                                                         const StarField&, an::ImageMetadata); \
    template ViewHandle fieldView<SampleT>(const an::consumer::EngineSession&, const StarField&,               \
                                           const an::ImageMetadata&, const std::string&);
AN_STAR_FIELD_REPRESENTATION(float)
AN_STAR_FIELD_REPRESENTATION(double)
AN_STAR_FIELD_REPRESENTATION(std::uint8_t)
AN_STAR_FIELD_REPRESENTATION(std::int8_t)
AN_STAR_FIELD_REPRESENTATION(std::uint16_t)
AN_STAR_FIELD_REPRESENTATION(std::int16_t)
AN_STAR_FIELD_REPRESENTATION(std::uint32_t)
AN_STAR_FIELD_REPRESENTATION(std::int32_t)
#undef AN_STAR_FIELD_REPRESENTATION

ViewHandle fieldSequenceView(const an::consumer::EngineSession& session, const QString& path, const StarField& field,
                             const std::uint32_t frameCount)
{
    {
        an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
            path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frameCount});
        EXPECT_TRUE(writer.hasValue()) << writer.status().message();
        if (!writer.hasValue())
        {
            return {};
        }
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
        EXPECT_TRUE(scope.has_value());
        if (!scope.has_value())
        {
            return {};
        }
        for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
        {
            StarField brighter = field;
            for (float& sample : brighter.samples)
            {
                sample = (std::min)(1.0f, sample + 0.01f * static_cast<float>(frame));
            }
            std::optional<an::consumer::ImageAsset> asset = fieldAsset<float>(session, brighter, an::ImageMetadata{});
            if (!asset.has_value())
            {
                return {};
            }
            an::Result<an::consumer::AssetReadLease> lease = asset->read();
            EXPECT_TRUE(lease.hasValue()) << lease.status().message();
            if (!lease.hasValue())
            {
                return {};
            }
            const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
            EXPECT_TRUE(appended.ok()) << appended.message();
        }
        an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
        EXPECT_TRUE(published.hasValue()) << published.status().message();
    }
    std::optional<an::consumer::ImageAsset> first = fieldAsset<float>(session, field, an::ImageMetadata{});
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

bool settleProfiler(const StarProfilerWindow& window)
{
    if (!pumpUntil([&window]() { return window.isRunIdle() && !window.isDetecting(); }))
    {
        return false;
    }
    pumpQuietly();
    return window.isRunIdle() && !window.isDetecting();
}

bool detectAndSettle(StarProfilerWindow& window)
{
    if (!window.detectStars())
    {
        return false;
    }
    return settleProfiler(window);
}

an::ui::StarList heldStars(StarProfilerWindow& window)
{
    const an::ui::StarListPtr stars = window.getCurrentStarList();
    return stars ? *stars : an::ui::StarList{};
}

QString cellText(const TableWidget& table, const int row, const int column)
{
    const QTableWidgetItem* item = table.item(row, column);
    return item != nullptr ? item->text() : QString();
}

InstalledCatalog::InstalledCatalog(ProcessTestContext& context) : m_context(context)
{
    EXPECT_TRUE(m_root.isValid());
}

QString InstalledCatalog::directory() const
{
    return m_root.filePath(QStringLiteral("gaia"));
}

bool InstalledCatalog::install(const StarField& field)
{
    if (!writeGaiaCatalog(directory(), field))
    {
        return false;
    }
    (void)m_context.upsert(QString::fromLatin1(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY), directory());
    return true;
}

void ProfilerCase::SetUp()
{
    m_context = openProcessContext(4u);
    ASSERT_NE(m_context, nullptr);
    m_installed.emplace(*m_context);
    ASSERT_TRUE(m_files.isValid());
}

void ProfilerCase::TearDown()
{
    m_installed.reset();
    m_context.reset();
}

QString ProfilerCase::temporaryPath(const QString& name) const
{
    return m_files.filePath(name);
}

QString ProfilerCase::writtenProfile(const QString& name, const an::StarProfileFile& document) const
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    if (!scope.has_value())
    {
        return {};
    }
    const QString path = temporaryPath(name);
    const an::Result<an::StarProfileWriteReport> written =
        an::writeStarProfileFile(scope->engine(), scope->execution(),
                                 an::StarProfileWriteRequest{.destinationPath = path.toStdString()}, document);
    EXPECT_TRUE(written.hasValue()) << written.status().message();
    return written.hasValue() ? path : QString();
}

an::StarProfileFile plantedProfile(const an::StarProfileGeneration generation, const StarField& field)
{
    // A Gaussian's full width at half maximum, by its standard deviation.
    const double widthPerSigma = 2.0 * std::sqrt(2.0 * std::numbers::ln2);
    if (generation == an::StarProfileGeneration::Version1)
    {
        an::StarProfileV1 payload;
        for (const PlantedStar& planted : field.stars)
        {
            const double fullWidth = widthPerSigma * planted.sigma;
            an::StarProfileV1Star star;
            star.intensity = planted.amplitude;
            star.percentage = 1.0;
            star.deltaRadius = 1.0;
            star.quality = planted.amplitude / 0.05;
            star.meanRadius = 0.5 * fullWidth + 0.5;
            star.radius = 2.0 * fullWidth;
            star.x = planted.x;
            star.y = planted.y;
            star.largeMajorAxis = 0.5 * fullWidth + 0.25;
            star.smallMajorAxis = 0.5 * fullWidth;
            star.largeMinorAxis = 0.5 * fullWidth;
            star.smallMinorAxis = 0.5 * fullWidth - 0.25;
            star.majorAxisAngle = 0.0;
            payload.stars.push_back(star);
        }
        return an::StarProfileFile{.generation = generation, .payload = std::move(payload)};
    }

    an::StarProfileV2 payload;
    constexpr std::int32_t kBoxRadius = 11;
    for (std::size_t index = 0u; index < field.stars.size(); ++index)
    {
        const PlantedStar& planted = field.stars[index];
        const double fullWidth = widthPerSigma * planted.sigma;
        an::StarProfileV2Star star;
        star.position = {.x = planted.x, .y = planted.y};
        star.psf.background = field.background;
        star.psf.amplitude = planted.amplitude;
        star.psf.centreX = kBoxRadius + (planted.x - std::floor(planted.x));
        star.psf.centreY = kBoxRadius + (planted.y - std::floor(planted.y));
        star.psf.scaleX = planted.sigma * std::numbers::sqrt2;
        star.psf.scaleY = planted.sigma * std::numbers::sqrt2;
        star.psf.fullWidthX = fullWidth * 1.02;
        star.psf.fullWidthY = fullWidth;
        star.psf.angle = -75.0 + 15.0 * static_cast<double>(index % 11u);
        star.psf.boxRadius = kBoxRadius;
        star.psf.saturated = index % 5u == 0u;
        // A Gaussian's exponent is written as minus one.
        star.psf.beta = -1.0;
        star.psf.profile = an::PersistedPsfModel::Gaussian;
        star.metrics.fullWidth = 0.5 * (star.psf.fullWidthX + star.psf.fullWidthY);
        star.metrics.roundness = star.psf.fullWidthY / star.psf.fullWidthX;
        star.metrics.circularity = 0.8;
        payload.stars.push_back(star);
    }
    return an::StarProfileFile{.generation = generation, .payload = std::move(payload)};
}

} // namespace an::tests::processes::stars
