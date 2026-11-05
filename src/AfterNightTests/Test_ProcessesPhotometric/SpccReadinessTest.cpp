// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SPCC window's readiness and what it shows: whether a run can start from a catalog, a response repository, the
/// profile a camera and filter resolve to - through an alias, a saved mapping or a person's choice - and an image's
/// declared state and plate solution; the readiness panel's selectors, banner tone and actions over a report; the
/// diagnostics and status line a run's summary formats; the ImageIntegration hand-off found from a stacked image's
/// file and resolved through a spectrum catalog; and the process shell's layout at 720p and 1080p.
///
/// Catalog packs and response repositories are written as the product installs them, and readiness is decided in an
/// admitted operation of a session of the case's own, from inputs a case states as the window snapshots them.

#include "Test_ProcessesPhotometric/PhotometricTestSupport.h"

#include "Test_EngineColor/SpectrumCatalogTestSupport.h"

#include "app/CatalogDownloadService.h"
#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/SpectrophotometricColorCalibration/SpccHandoffSubsetSpectrophotometricCatalog.h"
#include "processes/SpectrophotometricColorCalibration/SpccProcessReadinessPanel.h"
#include "processes/SpectrophotometricColorCalibration/SpectrophotometricColorCalibrationUiBuilder.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::photometric;

using an::ui::processes::SpccProcessReadinessPanel;
using an::ui::processes::SpccProcessReadinessReport;
using an::ui::processes::SpccProcessReadinessSeverity;
using an::ui::processes::SpccReadinessImage;
using an::ui::processes::SpccReadinessInputs;
using an::ui::processes::SpccResponseProfileComponentSet;
using an::ui::processes::SpccRunSummary;

/// The rights review a managed catalog pack is approved under.
constexpr const char kApprovedCatalogRights[] = "approved-for-managed-download";

/// The regional catalog pack most readiness cases install.
constexpr const char kRegionalManifestId[] = "spcc-gaia-dr3-xp-sampled-m31-region-20260510";

/// The access profile of an installed regional cache.
constexpr const char kRegionalAccessProfile[] = "installed-regional-cache";

/// The one chunk file a written catalog pack declares, relative to the pack.
constexpr const char kCatalogChunkFile[] = "chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp";

/// The spectrum catalog the ImageIntegration hand-off's recorded matches name.
constexpr const char kHandoffManifestId[] = "spcc-gaia-dr3-xp-sampled-fixture-v1";

/// The size of the image a readiness case decides for, which its plate solution describes.
constexpr std::uint32_t kFieldWidth = 320u;
constexpr std::uint32_t kFieldHeight = 240u;

/// Has `QStandardPaths` answer its test locations for a case, and its real ones again when the case ends.
class StandardPathsTestMode final
{
public:
    StandardPathsTestMode()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    ~StandardPathsTestMode()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    StandardPathsTestMode(const StandardPathsTestMode&) = delete;
    StandardPathsTestMode& operator=(const StandardPathsTestMode&) = delete;
};

/// Writes a file, its directory created; fails the case when it cannot.
void writeFixtureFile(const QString& path, const QByteArray& contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        ADD_FAILURE() << "could not create the directory of " << path.toStdString();
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        ADD_FAILURE() << "could not write " << path.toStdString();
        return;
    }
    EXPECT_EQ(file.write(contents), contents.size()) << path.toStdString();
}

/// The SHA-256 of a file's bytes, as lower-case hexadecimal.
[[nodiscard]] QString fixtureSha256(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

/// Whether a capture holds any painted pixel, sampled every sixteen pixels.
[[nodiscard]] bool imageHasPaintedPixels(const QImage& image)
{
    for (int y = 0; y < image.height(); y += 16)
    {
        for (int x = 0; x < image.width(); x += 16)
        {
            if (qAlpha(image.pixel(x, y)) != 0)
            {
                return true;
            }
        }
    }
    return false;
}

/// Holds every visible widget of a shell inside it, and a widget inside a scroll area inside that area's viewport.
void expectVisibleChildrenInside(QWidget& root)
{
    const QRect rootRect(QPoint(0, 0), root.size());
    const QRect tolerantRootRect = rootRect.adjusted(-1, -1, 1, 1);
    const QList<QWidget*> widgets = root.findChildren<QWidget*>();
    for (QWidget* widget : widgets)
    {
        if (!widget || widget->isWindow() || !widget->isVisibleTo(&root) || widget->width() <= 0 ||
            widget->height() <= 0)
        {
            continue;
        }

        const QRect mappedRect(widget->mapTo(&root, QPoint(0, 0)), widget->size());
        QScrollArea* clippingScrollArea = nullptr;
        for (QWidget* ancestor = widget->parentWidget(); ancestor && ancestor != &root;
             ancestor = ancestor->parentWidget())
        {
            if (auto* scrollArea = qobject_cast<QScrollArea*>(ancestor))
            {
                clippingScrollArea = scrollArea;
                break;
            }
        }

        if (clippingScrollArea && clippingScrollArea->viewport())
        {
            bool isScrollContentWidget = false;
            if (QWidget* scrollContent = clippingScrollArea->widget())
            {
                for (QWidget* ancestor = widget; ancestor; ancestor = ancestor->parentWidget())
                {
                    if (ancestor == scrollContent)
                    {
                        isScrollContentWidget = true;
                        break;
                    }
                }
            }
            if (!isScrollContentWidget)
            {
                EXPECT_TRUE(tolerantRootRect.contains(mappedRect))
                    << "Scroll-area chrome outside SPCC visual QA viewport: " << widget->objectName().toStdString()
                    << " rect=" << mappedRect.x() << "," << mappedRect.y() << " " << mappedRect.width() << "x"
                    << mappedRect.height() << " root=" << root.width() << "x" << root.height();
                continue;
            }

            const QRect mappedViewport(clippingScrollArea->viewport()->mapTo(&root, QPoint(0, 0)),
                                       clippingScrollArea->viewport()->size());
            const QRect visibleRect = mappedRect.intersected(mappedViewport);
            EXPECT_GE(mappedRect.left(), mappedViewport.left() - 1)
                << "Widget overflows SPCC scroll viewport horizontally: " << widget->objectName().toStdString();
            EXPECT_LE(mappedRect.right(), mappedViewport.right() + 1)
                << "Widget overflows SPCC scroll viewport horizontally: " << widget->objectName().toStdString();
            EXPECT_TRUE(visibleRect.isEmpty() || tolerantRootRect.contains(visibleRect))
                << "Visible part of SPCC scroll widget is outside viewport: " << widget->objectName().toStdString()
                << " rect=" << mappedRect.x() << "," << mappedRect.y() << " " << mappedRect.width() << "x"
                << mappedRect.height() << " viewport=" << mappedViewport.x() << "," << mappedViewport.y() << " "
                << mappedViewport.width() << "x" << mappedViewport.height() << " root=" << root.width() << "x"
                << root.height();
            continue;
        }

        EXPECT_TRUE(tolerantRootRect.contains(mappedRect))
            << "Widget outside SPCC visual QA viewport: " << widget->objectName().toStdString()
            << " rect=" << mappedRect.x() << "," << mappedRect.y() << " " << mappedRect.width() << "x"
            << mappedRect.height() << " minHint=" << widget->minimumSizeHint().width() << "x"
            << widget->minimumSizeHint().height() << " hint=" << widget->sizeHint().width() << "x"
            << widget->sizeHint().height() << " root=" << root.width() << "x" << root.height();
    }
}

/// Holds the visible widgets a layout places to rectangles that do not overlap.
void expectLayoutItemsDoNotOverlap(QLayout* layout, QWidget& root)
{
    ASSERT_NE(layout, nullptr);

    QVector<QRect> mappedRects;
    QVector<QString> names;
    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem* item = layout->itemAt(index);
        QWidget* widget = item ? item->widget() : nullptr;
        if (!widget || !widget->isVisibleTo(&root) || widget->width() <= 0 || widget->height() <= 0)
        {
            continue;
        }

        mappedRects.append(QRect(widget->mapTo(&root, QPoint(0, 0)), widget->size()));
        names.append(widget->objectName());
    }

    for (int left = 0; left < mappedRects.size(); ++left)
    {
        for (int right = left + 1; right < mappedRects.size(); ++right)
        {
            EXPECT_FALSE(mappedRects[left].intersects(mappedRects[right]))
                << "SPCC layout items overlap: " << names[left].toStdString() << " rect=" << mappedRects[left].x()
                << "," << mappedRects[left].y() << " " << mappedRects[left].width() << "x" << mappedRects[left].height()
                << " with " << names[right].toStdString() << " rect=" << mappedRects[right].x() << ","
                << mappedRects[right].y() << " " << mappedRects[right].width() << "x" << mappedRects[right].height();
        }
    }
}

/// Writes an SPCC catalog pack in a directory as the managed download installs one: the manifest, one chunk and its
/// digest, the four sidecars and, unless asked otherwise, the marker of a finished install. A chunk tile range and the
/// HEALPix orders are written only when they are stated.
void createSpccCatalogPack(const QString& root, const std::string& manifestId, const std::string& accessProfile,
                           const bool installComplete = true, const std::string& rightsReviewStatus = "private-local",
                           const int chunkFirstTile = -1, const int chunkTileCount = -1, const int healpixOrder = -1,
                           const int chunkParentHealpixOrder = -1)
{
    const QDir directory(root);
    EXPECT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("chunks"))));

    const std::string chunkFile = kCatalogChunkFile;
    const std::string chunkPayload = "spcc-anxp-settings-test";
    const QString chunkPath = directory.filePath(QString::fromStdString(chunkFile));
    writeFixtureFile(chunkPath, QByteArray::fromStdString(chunkPayload));
    const std::string chunkSha = fixtureSha256(chunkPath).toStdString();
    const std::string sidecarIdentity =
        "{\"catalog_family_id\":\"spcc_gaia_dr3_xp_sampled\",\"manifest_id\":\"" + manifestId + "\"";
    writeFixtureFile(directory.filePath(QStringLiteral("chunk_manifest.json")),
                     QByteArray::fromStdString(sidecarIdentity + ",\"chunks\":[]}"));
    writeFixtureFile(directory.filePath(QStringLiteral("coverage_summary.json")),
                     QByteArray::fromStdString(sidecarIdentity + ",\"total_records\":1}"));
    writeFixtureFile(directory.filePath(QStringLiteral("source_id_index.json")),
                     QByteArray::fromStdString(sidecarIdentity + ",\"source_count\":1,\"sources\":{}}"));
    writeFixtureFile(directory.filePath(QStringLiteral("source_count_transcript.json")),
                     QByteArray::fromStdString("{\"manifest_id\":\"" + manifestId + "\"}"));
    if (installComplete)
    {
        writeFixtureFile(directory.filePath(QStringLiteral(".install_complete")), QByteArrayLiteral("ok\n"));
    }

    const std::string formatFields =
        healpixOrder >= 0 && chunkParentHealpixOrder >= 0
            ? "\"format\":{\"healpix_order\":" + std::to_string(healpixOrder) +
                  ",\"chunk_parent_healpix_order\":" + std::to_string(chunkParentHealpixOrder) + "},"
            : "";
    const std::string chunkCoverageFields = chunkFirstTile >= 0 && chunkTileCount > 0
                                                ? "\"first_tile\":" + std::to_string(chunkFirstTile) +
                                                      ",\"tile_count\":" + std::to_string(chunkTileCount) + ","
                                                : "";
    const std::string chunkChecksumField = "\"sha256\":\"" + chunkSha + "\",";
    const std::string manifest = "{"
                                 "\"schema_version\":\"spcc-catalog-meta-v1\","
                                 "\"catalog_family_id\":\"spcc_gaia_dr3_xp_sampled\","
                                 "\"manifest_id\":\"" +
                                 manifestId +
                                 "\","
                                 "\"access_profile\":\"" +
                                 accessProfile +
                                 "\","
                                 "\"rights\":{\"review_status\":\"" +
                                 rightsReviewStatus + "\"}," + formatFields +
                                 "\"files\":{"
                                 "\"chunk_manifest\":\"chunk_manifest.json\","
                                 "\"coverage_summary\":\"coverage_summary.json\","
                                 "\"source_id_index\":\"source_id_index.json\","
                                 "\"source_count_transcript\":\"source_count_transcript.json\""
                                 "},"
                                 "\"chunks\":[{"
                                 "\"chunk_index\":0," +
                                 chunkCoverageFields + chunkChecksumField + "\"file\":\"" + chunkFile +
                                 "\","
                                 "\"size_bytes\":" +
                                 std::to_string(chunkPayload.size()) +
                                 "}]"
                                 "}";
    writeFixtureFile(directory.filePath(QStringLiteral("catalog_meta.json")), QByteArray::fromStdString(manifest));
}

/// Writes a response repository in a directory: three flat sensor components, a D65 white reference and the profile
/// `profile.synthetic.rgb` composed of them; optionally the components' and references' files left out, the exact alias
/// for "Unit Test Camera" through "UV/IR Cut" left out or given to a second profile too, a second profile
/// `profile.synthetic.rgb.alt` - camera RGB only - without the alias, and a second white reference, G2V.
void createSpccResponseRepository(const QString& root, const bool includeAssetFiles = true,
                                  const bool includeExactAlias = true, const bool duplicateExactAlias = false,
                                  const bool includeAlternateProfileWithoutAlias = false,
                                  const bool includeAlternateWhiteReference = false)
{
    const QDir directory(root);
    EXPECT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("components"))));
    EXPECT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("white_references"))));

    const QString redPath = directory.filePath(QStringLiteral("components/sensor_synthetic_r.json"));
    const QString greenPath = directory.filePath(QStringLiteral("components/sensor_synthetic_g.json"));
    const QString bluePath = directory.filePath(QStringLiteral("components/sensor_synthetic_b.json"));
    const QString whitePath = directory.filePath(QStringLiteral("white_references/white_synthetic_d65.json"));
    const QString alternateWhitePath = directory.filePath(QStringLiteral("white_references/white_synthetic_g2v.json"));

    const auto componentPayload = [](const std::string& componentId, const std::string& channel,
                                     const std::string& dataSha, const double value) {
        const std::string sample = QString::number(value, 'g', 17).toStdString();
        return "{\"schema_version\":\"spcc-response-asset-v1\",\"asset_kind\":\"component\","
               "\"component_id\":\"" +
               componentId + "\",\"version\":\"1\",\"channel\":\"" + channel +
               "\",\"wavelength_unit\":\"nm\","
               "\"value_semantics\":\"dimensionless-throughput\",\"review_status\":\"approved-for-bundling\","
               "\"data_sha256\":\"" +
               dataSha + "\",\"samples\":[{\"wavelength_nm\":400.0,\"value\":" + sample +
               "},{\"wavelength_nm\":700.0,\"value\":" + sample + "}]}";
    };

    const std::string redPayload = componentPayload("sensor.synthetic.r", "red", "red-data-sha", 0.8);
    const std::string greenPayload = componentPayload("sensor.synthetic.g", "green", "green-data-sha", 1.0);
    const std::string bluePayload = componentPayload("sensor.synthetic.b", "blue", "blue-data-sha", 0.6);
    const std::string whitePayload =
        "{\"schema_version\":\"spcc-response-asset-v1\",\"asset_kind\":\"white_reference\","
        "\"white_reference_id\":\"white.synthetic.d65\",\"version\":\"1\",\"wavelength_unit\":\"nm\","
        "\"value_semantics\":\"relative-sed\",\"review_status\":\"approved-for-bundling\","
        "\"data_sha256\":\"white-data-sha\","
        "\"samples\":[{\"wavelength_nm\":400.0,\"value\":1.0},{\"wavelength_nm\":700.0,\"value\":1.0}]}";
    const std::string alternateWhitePayload =
        "{\"schema_version\":\"spcc-response-asset-v1\",\"asset_kind\":\"white_reference\","
        "\"white_reference_id\":\"white.synthetic.g2v\",\"version\":\"1\",\"wavelength_unit\":\"nm\","
        "\"value_semantics\":\"relative-sed\",\"review_status\":\"approved-for-bundling\","
        "\"data_sha256\":\"alternate-white-data-sha\","
        "\"samples\":[{\"wavelength_nm\":400.0,\"value\":2.0},{\"wavelength_nm\":700.0,\"value\":2.0}]}";

    if (includeAssetFiles)
    {
        writeFixtureFile(redPath, QByteArray::fromStdString(redPayload));
        writeFixtureFile(greenPath, QByteArray::fromStdString(greenPayload));
        writeFixtureFile(bluePath, QByteArray::fromStdString(bluePayload));
        writeFixtureFile(whitePath, QByteArray::fromStdString(whitePayload));
        if (includeAlternateWhiteReference)
        {
            writeFixtureFile(alternateWhitePath, QByteArray::fromStdString(alternateWhitePayload));
        }
    }

    const QString redSha = includeAssetFiles ? fixtureSha256(redPath) : QStringLiteral("missing-red");
    const QString greenSha = includeAssetFiles ? fixtureSha256(greenPath) : QStringLiteral("missing-green");
    const QString blueSha = includeAssetFiles ? fixtureSha256(bluePath) : QStringLiteral("missing-blue");
    const QString whiteSha = includeAssetFiles ? fixtureSha256(whitePath) : QStringLiteral("missing-white");
    const QString alternateWhiteSha = includeAssetFiles && includeAlternateWhiteReference
                                          ? fixtureSha256(alternateWhitePath)
                                          : QStringLiteral("missing-alternate-white");
    const std::string alias =
        includeExactAlias ? ",\"aliases\":[{\"camera_model\":\"Unit Test Camera\",\"filter\":\"UV/IR Cut\"}]" : "";
    const std::string alternateWhiteReference =
        includeAlternateWhiteReference
            ? ",{\"white_reference_id\":\"white.synthetic.g2v\",\"version\":\"1\","
              "\"file\":\"white_references/white_synthetic_g2v.json\",\"wavelength_unit\":\"nm\","
              "\"value_semantics\":\"relative-sed\",\"review_status\":\"approved-for-bundling\","
              "\"data_sha256\":\"alternate-white-data-sha\",\"file_sha256\":\"" +
                  alternateWhiteSha.toStdString() + "\"}"
            : "";
    const std::string profile =
        "{\"profile_id\":\"profile.synthetic.rgb\",\"version\":\"1\","
        "\"component_ids\":[\"sensor.synthetic.r\",\"sensor.synthetic.g\",\"sensor.synthetic.b\"],"
        "\"channel_component_ids\":{\"red\":[\"sensor.synthetic.r\"],\"green\":[\"sensor.synthetic.g\"],"
        "\"blue\":[\"sensor.synthetic.b\"]},"
        "\"white_reference_id\":\"white.synthetic.d65\","
        "\"compatible_channel_bases\":[\"camera-rgb\",\"linear-rgb\"],"
        "\"models_raw_white_balance\":false,\"models_camera_matrix\":false" +
        alias + "}";
    const std::string duplicateProfile =
        (duplicateExactAlias || includeAlternateProfileWithoutAlias)
            ? ",{\"profile_id\":\"profile.synthetic.rgb.alt\",\"version\":\"1\","
              "\"component_ids\":[\"sensor.synthetic.r\",\"sensor.synthetic.g\",\"sensor.synthetic.b\"],"
              "\"channel_component_ids\":{\"red\":[\"sensor.synthetic.r\"],\"green\":[\"sensor.synthetic.g\"],"
              "\"blue\":[\"sensor.synthetic.b\"]},"
              "\"white_reference_id\":\"white.synthetic.d65\","
              "\"compatible_channel_bases\":[\"camera-rgb\"],"
              "\"models_raw_white_balance\":false,\"models_camera_matrix\":false" +
                  (duplicateExactAlias ? alias : std::string()) + "}"
            : "";

    const std::string manifest =
        "{\"schema_version\":\"spcc-response-repository-v1\","
        "\"repository_id\":\"spcc-response-fixture\",\"version\":\"2026a\","
        "\"alias_registry_version\":\"aliases-2026a\","
        "\"rights\":{\"review_status\":\"approved-for-bundling\",\"license_name\":\"synthetic-fixture\"},"
        "\"deployment_scopes\":[\"deployed-runtime\",\"user-private\"],"
        "\"components\":[{\"component_id\":\"sensor.synthetic.r\",\"version\":\"1\","
        "\"role\":\"sensor-red-response\",\"file\":\"components/sensor_synthetic_r.json\","
        "\"wavelength_unit\":\"nm\",\"value_semantics\":\"dimensionless-throughput\","
        "\"review_status\":\"approved-for-bundling\",\"data_sha256\":\"red-data-sha\",\"file_sha256\":\"" +
        redSha.toStdString() +
        "\"},{\"component_id\":\"sensor.synthetic.g\",\"version\":\"1\","
        "\"role\":\"sensor-green-response\",\"file\":\"components/sensor_synthetic_g.json\","
        "\"wavelength_unit\":\"nm\",\"value_semantics\":\"dimensionless-throughput\","
        "\"review_status\":\"approved-for-bundling\",\"data_sha256\":\"green-data-sha\",\"file_sha256\":\"" +
        greenSha.toStdString() +
        "\"},{\"component_id\":\"sensor.synthetic.b\",\"version\":\"1\","
        "\"role\":\"sensor-blue-response\",\"file\":\"components/sensor_synthetic_b.json\","
        "\"wavelength_unit\":\"nm\",\"value_semantics\":\"dimensionless-throughput\","
        "\"review_status\":\"approved-for-bundling\",\"data_sha256\":\"blue-data-sha\",\"file_sha256\":\"" +
        blueSha.toStdString() +
        "\"}],"
        "\"white_references\":[{\"white_reference_id\":\"white.synthetic.d65\",\"version\":\"1\","
        "\"file\":\"white_references/white_synthetic_d65.json\",\"wavelength_unit\":\"nm\","
        "\"value_semantics\":\"relative-sed\",\"review_status\":\"approved-for-bundling\","
        "\"data_sha256\":\"white-data-sha\",\"file_sha256\":\"" +
        whiteSha.toStdString() + "\"}" + alternateWhiteReference +
        "],"
        "\"throughput_profiles\":[" +
        profile + duplicateProfile + "]}";
    writeFixtureFile(directory.filePath(QStringLiteral("response_meta.json")), QByteArray::fromStdString(manifest));
}

/// The plate solution of the field a regional catalog request is made for: 320 by 240 pixels of one arcsecond, north
/// up, centred on RA 83.6331 and Dec 22.0145 degrees.
[[nodiscard]] std::optional<an::WcsSolution> regionalRequestSolution()
{
    constexpr double kScaleDegrees = 1.0 / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * kFieldWidth, .y = 0.5 * kFieldHeight};
    request.referencePoint = {.rightAscensionDegrees = 83.6331, .declinationDegrees = 22.0145};
    request.linear = {.cd11 = -kScaleDegrees, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScaleDegrees};
    request.imageWidth = kFieldWidth;
    request.imageHeight = kFieldHeight;
    an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::optional<an::WcsSolution>(std::move(*made)) : std::nullopt;
}

/// What the image a readiness case decides for states about itself.
struct ReadinessImageOptions
{
    /// Whether the cards state the regional request's plate solution.
    bool solved = false;

    /// The processing state the cards record; nothing for no state cards.
    std::optional<an::ProcessingStateRecord> state =
        an::ProcessingStateRecord{.linearity = an::RecordedLinearity::Linear,
                                  .channelBasis = an::RecordedChannelBasis::LinearRgb,
                                  .assignmentSource = an::StateAssignmentSource::Process,
                                  .operation = "unit-test"};

    /// Whether the cards record a photometric colour calibration over that state.
    bool colorCalibrated = false;

    /// The filter the acquisition record names; empty for none.
    std::string filter;
};

/// The metadata of a readiness case's image: the cards its options state and an acquisition record naming its filter.
[[nodiscard]] an::ImageMetadata readinessMetadata(const ReadinessImageOptions& options)
{
    an::Result<an::CodecMetadata> created =
        an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{});
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    if (!created.hasValue())
    {
        return an::ImageMetadata{};
    }
    an::CodecMetadata codec = std::move(*created);
    if (options.solved)
    {
        const std::optional<an::WcsSolution> solution = regionalRequestSolution();
        if (solution.has_value())
        {
            an::Result<an::CodecMetadata> solved = an::withWcsCards(codec, *solution);
            EXPECT_TRUE(solved.hasValue()) << solved.status().message();
            if (solved.hasValue())
            {
                codec = std::move(*solved);
            }
        }
    }
    if (options.state.has_value())
    {
        an::Result<an::CodecMetadata> stated = an::withProcessingStateCards(codec, *options.state);
        EXPECT_TRUE(stated.hasValue()) << stated.status().message();
        if (stated.hasValue())
        {
            codec = std::move(*stated);
        }
    }
    if (options.colorCalibrated)
    {
        an::Result<an::CodecMetadata> calibrated = an::withColorCalibrationCards(
            codec, an::ColorCalibrationRecord{.calibration = an::RecordedColorCalibration::Photometric,
                                              .gains = {1.1, 1.0, 0.9}});
        EXPECT_TRUE(calibrated.hasValue()) << calibrated.status().message();
        if (calibrated.hasValue())
        {
            codec = std::move(*calibrated);
        }
    }
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.filterName = options.filter});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    return an::ImageMetadata::create(acquisition.hasValue() ? std::move(*acquisition) : an::AcquisitionMetadata{},
                                     an::ImageSemantics{}, an::AstrometricSolution{}, an::Provenance{},
                                     std::move(codec), std::nullopt);
}

/// The image a readiness case decides for: a Float32 RGB image of the field's size carrying its metadata.
[[nodiscard]] std::optional<SpccReadinessImage> readinessImage(const an::consumer::EngineSession& session,
                                                               const ReadinessImageOptions& options)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        hold->engine(), an::kSampleTypeOf<float>, kFieldWidth, kFieldHeight, an::tests::viewer::Channels::Rgb);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    return SpccReadinessImage{.descriptor = *descriptor, .metadata = readinessMetadata(options)};
}

/// The readiness the window decides for these inputs, in an admitted operation of a session.
[[nodiscard]] SpccProcessReadinessReport decideReadiness(const an::consumer::EngineSession& session,
                                                         const SpccReadinessInputs& inputs)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return {};
    }
    return an::ui::processes::evaluateSpccProcessReadiness(scope->execution(), inputs);
}

/// The inputs of a readiness decided from directories, a camera and filter and a profile a person chose, with no image.
[[nodiscard]] SpccReadinessInputs directoryInputs(const QString& catalogDirectory, const QString& responseDirectory,
                                                  const QString& cameraModel = QString(),
                                                  const QString& filterName = QString(),
                                                  const QString& explicitResponseProfileId = QString())
{
    SpccReadinessInputs inputs;
    inputs.catalogDirectory = catalogDirectory;
    inputs.responseRepositoryDirectory = responseDirectory;
    inputs.cameraModel = cameraModel;
    inputs.filterName = filterName;
    inputs.explicitResponseProfileId = explicitResponseProfileId;
    return inputs;
}

/// One star a registration recorded matching, against the hand-off's spectrum catalog, under a solution and an epoch.
[[nodiscard]] an::MatchedSourceReference recordedMatch(const std::uint64_t sourceIdentifier,
                                                       const QString& solutionIdentity, const double epochYear)
{
    an::MatchedSourceReference reference;
    reference.sourceIdentifier = sourceIdentifier;
    reference.astrometricCatalogFamilyId = "gaia_dr3";
    reference.astrometricCatalogManifestId = "gaia-dr3-main-2026-05";
    reference.spectrumCatalogFamilyId = an::kXpCatalogFamilyId;
    reference.spectrumCatalogManifestId = kHandoffManifestId;
    reference.wcsSolutionId = solutionIdentity.toStdString();
    reference.observationEpoch = an::ui::processes::spccObservationEpochText(epochYear).toStdString();
    reference.properMotionModelVersion = an::ui::processes::kSpccProperMotionModelVersion;
    reference.separationArcseconds = 0.37;
    reference.residualPixels = 0.21;
    return reference;
}

/// The hand-off's spectrum catalog as an installed regional cache holding one star of each identifier given.
[[nodiscard]] an::SpectrumCatalog handoffCatalog(const an::ExecutionContext& execution,
                                                 const std::vector<std::uint64_t>& sourceIdentifiers)
{
    spectrum_test::ManifestOptions options;
    options.manifestId = kHandoffManifestId;
    options.accessProfile = kRegionalAccessProfile;
    spectrum_test::FixtureChunk chunk;
    for (std::size_t index = 0u; index < sourceIdentifiers.size(); ++index)
    {
        spectrum_test::FixtureSpectrumStar star;
        star.sourceIdentifier = sourceIdentifiers[index];
        star.rightAscensionDegrees = 10.6847 + 0.01 * static_cast<double>(index);
        star.declinationDegrees = 41.2690;
        chunk.stars.push_back(star);
    }
    return spectrum_test::buildOrFail(execution, std::vector<spectrum_test::FixtureChunk>{chunk}, options);
}

/// Writes a workflow archive in a history run whose one integration group names an output file of an output directory
/// and a registration group, as ImageIntegration archives a run.
void writeHistoryWorkflowArchive(const QString& runDirectory, const QString& outputDirectory,
                                 const QString& outputFilename, const QString& registrationGroupKey)
{
    const QJsonObject group{{QStringLiteral("groupKey"), QStringLiteral("L|M31")},
                            {QStringLiteral("outputFilename"), outputFilename},
                            {QStringLiteral("registrationGroupKey"), registrationGroupKey}};
    const QJsonObject manifest{{QStringLiteral("outputDirectory"), outputDirectory},
                               {QStringLiteral("integrationGroups"), QJsonArray{group}}};
    writeFixtureFile(QDir(runDirectory).filePath(QStringLiteral("workflow_manifest.json")),
                     QJsonDocument(QJsonObject{{QStringLiteral("manifest"), manifest}}).toJson(QJsonDocument::Compact));
}

/// A readiness case: a session its readiness is decided in, the settings the window reads, and a temporary root its
/// catalog packs and repository are installed under.
class ReadinessCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
    }

    void TearDown() override
    {
        m_context.reset();
    }

    /// The application's settings, in memory.
    [[nodiscard]] ProcessTestContext& settings() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// The temporary root everything the case installs is under.
    [[nodiscard]] QString rootDirectory() const
    {
        return m_root.path();
    }

    /// The directory of a catalog pack under the root.
    [[nodiscard]] QString catalogDirectory(const char* manifestId = kRegionalManifestId) const
    {
        return QDir(m_root.path()).filePath(QString::fromLatin1(manifestId));
    }

    /// The directory of the response repository under the root.
    [[nodiscard]] QString responseDirectory() const
    {
        return QDir(m_root.path()).filePath(QStringLiteral("spcc_responses"));
    }

    /// Installs the rights-approved regional catalog pack most cases read.
    void installApprovedRegionalCatalog() const
    {
        createSpccCatalogPack(catalogDirectory(), kRegionalManifestId, kRegionalAccessProfile, true,
                              kApprovedCatalogRights);
    }

    /// Names the root the regional pack is under, the pack and the repository in the settings, as Catalogs & Data
    /// stores them.
    void nameInstalledDirectoriesInSettings() const
    {
        EXPECT_TRUE(
            settings().upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY), rootDirectory()));
        EXPECT_TRUE(settings().upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_CATALOG_PACK),
                                      QString::fromLatin1(kRegionalManifestId)));
        EXPECT_TRUE(settings().upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY),
                                      responseDirectory()));
    }

    /// The readiness decided for these inputs.
    [[nodiscard]] SpccProcessReadinessReport decide(const SpccReadinessInputs& inputs) const
    {
        return decideReadiness(session(), inputs);
    }

    /// The image a case decides for, stating what the options say.
    [[nodiscard]] std::optional<SpccReadinessImage> image(const ReadinessImageOptions& options = {}) const
    {
        return readinessImage(session(), options);
    }

private:
    QTemporaryDir m_root;
    std::unique_ptr<ProcessTestContext> m_context;
};

class SpccProcessReadinessTest : public ReadinessCase
{
};

class SpccResponseSettingsTest : public ReadinessCase
{
};

class SpccImageIntegrationHandoffTest : public ReadinessCase
{
};

} // namespace

// settings: a saved camera and filter mapping round-trips through the settings and resolves the profile and white
// reference it names when the repository's aliases name none.
TEST_F(SpccResponseSettingsTest, SavedResponseProfileMappingRoundTripsAndDrivesReadiness)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory(), true, false, false, false, true);

    nameInstalledDirectoriesInSettings();

    EXPECT_TRUE(an::ui::saveSpccResponseProfileMapping(
        &settings(), QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"),
        QStringLiteral("spcc-response-fixture"), QStringLiteral("aliases-2026a"),
        QStringLiteral("profile.synthetic.rgb"), QStringLiteral("white.synthetic.g2v")));

    const QVector<an::ui::SpccResponseProfileMapping> mappings = an::ui::loadSpccResponseProfileMappings(&settings());
    ASSERT_EQ(mappings.size(), 1);
    EXPECT_EQ(mappings.front().profileId, QStringLiteral("profile.synthetic.rgb"));
    EXPECT_EQ(mappings.front().whiteReferenceId, QStringLiteral("white.synthetic.g2v"));

    SpccReadinessInputs inputs = an::ui::processes::spccReadinessInputs(&settings());
    inputs.cameraModel = QStringLiteral("Unit Test Camera");
    inputs.filterName = QStringLiteral("UV/IR Cut");
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.responseProfileSavedMapping);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(report.responseProfileId, QStringLiteral("profile.synthetic.rgb"));
    EXPECT_EQ(report.whiteReferenceId, QStringLiteral("white.synthetic.g2v"));
    EXPECT_TRUE(report.responseProfileMessage.contains(QStringLiteral("saved"), Qt::CaseInsensitive));
}

// settings: a saved mapping wins over the profile the repository's exact alias for the same camera and filter names.
TEST_F(SpccResponseSettingsTest, SavedResponseProfileMappingOverridesExactMetadataAlias)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory(), true, true, false, true);

    nameInstalledDirectoriesInSettings();

    EXPECT_TRUE(an::ui::saveSpccResponseProfileMapping(
        &settings(), QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"),
        QStringLiteral("spcc-response-fixture"), QStringLiteral("aliases-2026a"),
        QStringLiteral("profile.synthetic.rgb.alt")));

    SpccReadinessInputs inputs = an::ui::processes::spccReadinessInputs(&settings());
    inputs.cameraModel = QStringLiteral("Unit Test Camera");
    inputs.filterName = QStringLiteral("UV/IR Cut");
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.responseProfileSavedMapping);
    EXPECT_EQ(report.responseProfileId, QStringLiteral("profile.synthetic.rgb.alt"));
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
}

// hand-off: a stacked image's latest workflow archive names its registration group, whose recorded matches are read
// and resolved through the SPCC catalog - one accepted, one a cache miss of the regional cache - with the region query
// still allowed.
TEST_F(SpccImageIntegrationHandoffTest, ResolvesLatestWorkflowArchiveMatchedSourcesThroughSpccCatalog)
{
    const QDir outputDir(rootDirectory());
    const QString outputFilename = QStringLiteral("stacked_L_M31.fit");
    const QString targetPath = outputDir.filePath(outputFilename);
    writeFixtureFile(targetPath, QByteArrayLiteral("target"));

    const QString registrationGroupKey = QStringLiteral("reg|resolution=bin1|object=M31|filter=L");
    writeHistoryWorkflowArchive(outputDir.filePath(QStringLiteral("history/20260511_010000_run")),
                                outputDir.absolutePath(), outputFilename, registrationGroupKey);
    const QString metricsDatabasePath = outputDir.filePath(QStringLiteral("registration_metrics.db"));

    // The recorded matches name the solution and the epoch the calibration runs under, which every match is held to;
    // the registration database is written through the processes library's own writer.
    const std::optional<an::WcsSolution> solution = regionalRequestSolution();
    ASSERT_TRUE(solution.has_value());
    const QString solutionIdentity = an::ui::processes::spccSolutionIdentity(*solution);
    ASSERT_FALSE(solutionIdentity.isEmpty());
    constexpr double kEpochYear = 2025.25;
    constexpr std::uint64_t kCatalogued = 589012345678901234ull;
    constexpr std::uint64_t kUncatalogued = 589012345678901235ull;
    RecordedHandoff recorded;
    recorded.metricsDatabasePath = metricsDatabasePath;
    recorded.registrationGroupKey = registrationGroupKey;
    recorded.settingsVersion = 3;
    recorded.references = {recordedMatch(kCatalogued, solutionIdentity, kEpochYear),
                           recordedMatch(kUncatalogued, solutionIdentity, kEpochYear)};
    writeHandoffDatabase(recorded);

    const an::ui::processes::SpccIntegrationHandoff handoff =
        an::ui::processes::locateSpccIntegrationHandoff(targetPath);

    EXPECT_TRUE(handoff.located);
    EXPECT_TRUE(handoff.loaded);
    EXPECT_EQ(handoff.outputDirectory, outputDir.absolutePath());
    EXPECT_EQ(handoff.registrationGroupKey, registrationGroupKey);
    EXPECT_EQ(handoff.settingsVersion, 3);
    EXPECT_EQ(handoff.references.size(), 2u);
    EXPECT_TRUE(handoff.fallbackRegionQueryAllowed);

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const an::SpectrumCatalog catalog = handoffCatalog(scope->execution(), {kCatalogued});
    ASSERT_TRUE(catalog.valid());
    const an::Result<an::MatchedSourceResolution> resolution =
        an::resolveMatchedSources(scope->execution(), handoff.references, catalog,
                                  an::ui::processes::spccHandoffRequirements(solutionIdentity, kEpochYear));
    ASSERT_TRUE(resolution.hasValue()) << resolution.status().message();

    EXPECT_EQ(resolution->acceptedCount, 1u);
    EXPECT_EQ(resolution->cacheMissCount, 1u);
    EXPECT_EQ(an::ui::processes::acceptedHandoffSources(*resolution, catalog), std::vector<std::uint64_t>{kCatalogued});
    // The fallback sentence is the resolution's own, not a line of the search's diagnostics.
    EXPECT_TRUE(an::ui::processes::spccHandoffResolutionText(handoff, *resolution, catalog.manifest())
                    .contains(QStringLiteral("region-query fallback"), Qt::CaseInsensitive));
}

// report: a run is refused while its response profiles are missing, however much the catalog and the profiles could
// be downloaded first.
TEST(SpccProcessReadinessReportTest, MissingResponseProfilesBlockEvenWhenDownloadsAreAvailable)
{
    SpccProcessReadinessReport report;
    report.catalogChunkDownloadRequired = true;
    report.catalogChunkAutoDownloadAvailable = true;
    report.responseAssetsDownloadRequired = true;
    report.responseAssetsAutoDownloadAvailable = true;
    report.imageStateChecked = true;
    report.imageStateCompatible = true;

    EXPECT_FALSE(report.canExecute());
}

// report: missing response data that no managed download can prepare refuses the run.
TEST(SpccProcessReadinessReportTest, MissingResponseDataWithoutManagedPreparationBlocksRun)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsDownloadRequired = true;
    report.responseAssetsAutoDownloadAvailable = false;

    EXPECT_FALSE(report.canExecute());
}

// readiness: with no catalog and no repository, a run is blocked and the action names the Catalogs & Data page.
TEST_F(SpccProcessReadinessTest, MissingCatalogBlocksAndTargetsCatalogsData)
{
    const SpccProcessReadinessReport report = decide(SpccReadinessInputs{});

    EXPECT_FALSE(report.catalogReady);
    EXPECT_FALSE(report.responseAssetsReady);
    EXPECT_FALSE(report.canExecute());
    EXPECT_EQ(report.settingsPageTitle, QStringLiteral("Catalogs & Data"));
    EXPECT_EQ(report.actionText, QStringLiteral("Open Catalogs & Data..."));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("catalog"), Qt::CaseInsensitive));
}

// readiness: an approved catalog with no response repository is blocked until the response profiles are downloaded,
// which the managed repository offers.
TEST_F(SpccProcessReadinessTest, ReadyCatalogBlocksUntilResponseAssetsAreDownloaded)
{
    installApprovedRegionalCatalog();

    const SpccProcessReadinessReport report = decide(directoryInputs(catalogDirectory(), QString()));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_FALSE(report.responseAssetsReady);
    EXPECT_TRUE(report.responseAssetsDownloadRequired);
    EXPECT_TRUE(report.responseAssetsAutoDownloadAvailable);
    EXPECT_FALSE(report.canExecute());
    EXPECT_EQ(report.catalogManifestId, QString::fromLatin1(kRegionalManifestId));
    EXPECT_EQ(report.catalogAccessProfile, QString::fromLatin1(kRegionalAccessProfile));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("response profiles"), Qt::CaseInsensitive));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("download"), Qt::CaseInsensitive));
    EXPECT_EQ(report.severity(), SpccProcessReadinessSeverity::Blocked);
}

// readiness: a ready catalog and repository with no camera or filter to resolve a profile from waits for a person's
// choice, a warning rather than a refusal.
TEST_F(SpccProcessReadinessTest, ReadyCatalogAndResponseRepositoryBlocksWithoutResponseProfileMetadata)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    const SpccProcessReadinessReport report = decide(directoryInputs(catalogDirectory(), responseDirectory()));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_FALSE(report.responseProfileResolved);
    EXPECT_FALSE(report.canExecute());
    EXPECT_EQ(report.responseRepositoryId, QStringLiteral("spcc-response-fixture"));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("response profile"), Qt::CaseInsensitive));
    EXPECT_EQ(report.severity(), SpccProcessReadinessSeverity::Warning);
}

// readiness: while the catalog can be prepared when the run starts, the summary asks for the response profile rather
// than describing the catalog's download.
TEST_F(SpccProcessReadinessTest, ResponseProfileSelectionMessageTakesPriorityOverCatalogAutoDownload)
{
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(QString(), responseDirectory());
    inputs.matchRadiusArcsec = 4.0;
    inputs.image = image({.solved = true});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_FALSE(report.catalogReady);
    EXPECT_TRUE(report.catalogChunkDownloadRequired);
    EXPECT_TRUE(report.catalogChunkAutoDownloadAvailable);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_FALSE(report.responseProfileResolved);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.summary.contains(QStringLiteral("response profile"), Qt::CaseInsensitive));
    EXPECT_FALSE(report.summary.contains(QStringLiteral("catalog metadata"), Qt::CaseInsensitive));
    EXPECT_EQ(report.severity(), SpccProcessReadinessSeverity::Warning);
}

// readiness: with no catalog installed, a plate-solved image's run prepares the managed local-full catalog on demand,
// whose descriptor has a usable manifest address.
TEST_F(SpccProcessReadinessTest, MissingCatalogAutoDownloadUsesUsableLocalFullManifestDescriptor)
{
    const StandardPathsTestMode testMode;
    QFile::remove(an::ui::spccCatalogMirrorConfigFilePath());
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(QString(), responseDirectory(), QStringLiteral("Unit Test Camera"),
                                                 QStringLiteral("UV/IR Cut"));
    inputs.matchRadiusArcsec = 4.0;
    inputs.image = image({.solved = true});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_FALSE(report.catalogReady);
    EXPECT_TRUE(report.catalogRegionChecked);
    EXPECT_FALSE(report.catalogRegionAvailable);
    EXPECT_TRUE(report.catalogChunkDownloadRequired);
    EXPECT_TRUE(report.catalogChunkAutoDownloadAvailable);
    EXPECT_EQ(report.catalogManifestId, QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b"));
    EXPECT_EQ(report.catalogAccessProfile, QStringLiteral("local-full"));
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();

    const QVector<an::ui::CatalogPackDescriptor> packs = an::ui::availableSpccCatalogPacks();
    const auto it = std::find_if(packs.begin(), packs.end(), [&report](const an::ui::CatalogPackDescriptor& pack) {
        return pack.id.compare(report.catalogManifestId, Qt::CaseInsensitive) == 0;
    });
    ASSERT_TRUE(it != packs.end());
    EXPECT_TRUE(an::ui::isUsableCatalogManifestUrl(it->manifestUrl));
    EXPECT_TRUE(an::ui::isDownloadableManagedSpccLocalFullCatalogDescriptor(*it));
}

// readiness: an approved catalog and a repository whose exact alias names the camera and filter resolve the profile
// and its white reference, and the run can start.
TEST_F(SpccProcessReadinessTest, ReadyCatalogAndExactResponseProfileCanExecute)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    const SpccProcessReadinessReport report = decide(directoryInputs(
        catalogDirectory(), responseDirectory(), QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut")));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(report.responseProfileId, QStringLiteral("profile.synthetic.rgb"));
    EXPECT_EQ(report.whiteReferenceId, QStringLiteral("white.synthetic.d65"));
    EXPECT_EQ(report.responseAliasRegistryVersion, QStringLiteral("aliases-2026a"));
    EXPECT_EQ(report.severity(), SpccProcessReadinessSeverity::Ready);
}

// readiness: the window's readiness inspects the catalog without verifying its checksums, so a chunk a full
// inspection finds corrupt still reads as ready.
TEST_F(SpccProcessReadinessTest, ReadinessUsesFastCatalogInspectionForLaunchPath)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());
    writeFixtureFile(QDir(catalogDirectory()).filePath(QString::fromLatin1(kCatalogChunkFile)),
                     QByteArrayLiteral("same-size-corrupt-data!"));

    const an::ui::SpccCatalogDirectoryInspection verifiedInspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(catalogDirectory());
    ASSERT_EQ(verifiedInspection.status, an::ui::SpccCatalogDirectoryStatus::PartialCache);
    ASSERT_FALSE(verifiedInspection.checksumsValid);

    const SpccProcessReadinessReport report = decide(directoryInputs(
        catalogDirectory(), responseDirectory(), QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut")));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.catalogRightsApproved);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_TRUE(report.catalogMessage.contains(QStringLiteral("SPCC catalog ready"), Qt::CaseInsensitive));
    EXPECT_FALSE(report.catalogMessage.contains(QStringLiteral("Cached chunks"), Qt::CaseInsensitive));
}

// readiness: a decision a newer one overtook stops after the catalog, before the response repository's assets are
// hashed and the repository loaded - the window shows only the latest, so the rest would be read for nothing.
TEST_F(SpccProcessReadinessTest, AnOvertakenDecisionStopsBeforeTheResponseRepository)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());
    const SpccReadinessInputs inputs = directoryInputs(catalogDirectory(), responseDirectory(),
                                                       QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));

    const SpccProcessReadinessReport decided = decide(inputs);
    ASSERT_FALSE(decided.stopped);
    ASSERT_TRUE(decided.responseAssetsReady);
    ASSERT_FALSE(decided.responseRepositoryId.isEmpty());

    std::stop_source overtaken;
    an::consumer::OperationRequest request;
    request.purpose = an::consumer::OperationPurpose::Background;
    request.stopToken = overtaken.get_token();
    an::Result<an::consumer::OperationScope> scope = session().tryAdmit(request);
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    overtaken.request_stop();
    const SpccProcessReadinessReport stopped =
        an::ui::processes::evaluateSpccProcessReadiness(scope->execution(), inputs);
    EXPECT_TRUE(stopped.stopped);
    EXPECT_TRUE(stopped.catalogReady) << "the catalog is decided before the first step boundary";
    EXPECT_FALSE(stopped.responseAssetsReady);
    EXPECT_TRUE(stopped.responseRepositoryId.isEmpty()) << "the repository was inspected after the stop";
    EXPECT_FALSE(stopped.responseProfileResolved);
}

// readiness: a local-full catalog missing the chunk a plate-solved image's field needs can still start, downloading
// that chunk first.
TEST_F(SpccProcessReadinessTest, PartialLocalFullCacheCanAutoDownloadNeededChunks)
{
    constexpr const char kFullManifestId[] = "spcc-gaia-dr3-xp-sampled-full-2026b";
    createSpccCatalogPack(catalogDirectory(kFullManifestId), kFullManifestId, "local-full", false,
                          kApprovedCatalogRights, 0, 49152, an::kXpCatalogHealpixOrder, an::kXpChunkParentHealpixOrder);
    EXPECT_TRUE(
        QFile::remove(QDir(catalogDirectory(kFullManifestId)).filePath(QString::fromLatin1(kCatalogChunkFile))));
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(kFullManifestId), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.matchRadiusArcsec = 4.0;
    inputs.image = image({.solved = true});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.catalogRegionChecked);
    EXPECT_FALSE(report.catalogRegionAvailable);
    EXPECT_TRUE(report.catalogChunkDownloadRequired);
    EXPECT_TRUE(report.catalogChunkAutoDownloadAvailable);
    EXPECT_EQ(report.catalogMissingChunkCount, 1);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_TRUE(report.summary.contains(QStringLiteral("download"), Qt::CaseInsensitive));
}

// readiness: a catalog whose rights review is private-local refuses the run and says why, though the profile and the
// image would serve.
TEST_F(SpccProcessReadinessTest, PrivateLocalCatalogBlocksExecution)
{
    createSpccCatalogPack(catalogDirectory(), kRegionalManifestId, kRegionalAccessProfile);
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.image = image();
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.catalogReady);
    EXPECT_FALSE(report.catalogRightsApproved);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.imageStateCompatible);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.catalogMessage.contains(QStringLiteral("rights"), Qt::CaseInsensitive));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("rights"), Qt::CaseInsensitive));
}

// readiness: an image declared linear RGB is compatible, and the run can start.
TEST_F(SpccProcessReadinessTest, CompatibleImageStateCanExecute)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.image = image();
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.imageStateChecked);
    EXPECT_TRUE(report.imageStateCompatible);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_TRUE(report.summary.contains(QStringLiteral("ready"), Qt::CaseInsensitive));
}

// readiness: a regional cache whose declared tiles miss the plate-solved field refuses the run before it starts.
TEST_F(SpccProcessReadinessTest, RegionalCatalogMissingTargetFootprintBlocksBeforeExecution)
{
    constexpr const char kMissManifestId[] = "spcc-gaia-dr3-xp-sampled-orion-miss-20260510";
    createSpccCatalogPack(catalogDirectory(kMissManifestId), kMissManifestId, kRegionalAccessProfile, true,
                          kApprovedCatalogRights, 0, 64, an::kXpCatalogHealpixOrder, an::kXpChunkParentHealpixOrder);
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(kMissManifestId), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.matchRadiusArcsec = 4.0;
    inputs.image = image({.solved = true});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.catalogRightsApproved);
    EXPECT_TRUE(report.catalogRegionChecked);
    EXPECT_FALSE(report.catalogRegionAvailable);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.summary.contains(QStringLiteral("regional cache"), Qt::CaseInsensitive));
}

// readiness: a profile a person chose that models only camera RGB is refused for a linear RGB image, naming the
// channel basis.
TEST_F(SpccProcessReadinessTest, IncompatibleManualResponseProfileBlocksLinearRgbImage)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory(), true, true, false, true);

    SpccReadinessInputs inputs =
        directoryInputs(catalogDirectory(), responseDirectory(), QStringLiteral("Unit Test Camera"),
                        QStringLiteral("UV/IR Cut"), QStringLiteral("profile.synthetic.rgb.alt"));
    inputs.image = image();
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.catalogRightsApproved);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_FALSE(report.responseProfileResolved);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.responseProfileMessage.contains(QStringLiteral("channel basis"), Qt::CaseInsensitive));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("channel basis"), Qt::CaseInsensitive));
}

// readiness: the inputs the settings state find the catalog pack under its root and the repository, and with a linear
// RGB image the run can start.
TEST_F(SpccProcessReadinessTest, GlobalSettingsProviderReadinessResolvesCatalogResponseAndImageState)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    nameInstalledDirectoriesInSettings();

    SpccReadinessInputs inputs = an::ui::processes::spccReadinessInputs(&settings());
    inputs.cameraModel = QStringLiteral("Unit Test Camera");
    inputs.filterName = QStringLiteral("UV/IR Cut");
    inputs.image = image();
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.imageStateChecked);
    EXPECT_TRUE(report.imageStateCompatible);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(QDir::cleanPath(report.catalogDirectory), QDir::cleanPath(catalogDirectory()));
    EXPECT_EQ(QDir::cleanPath(report.responseRepositoryDirectory), QDir::cleanPath(responseDirectory()));
}

// readiness: an image that declares no processing state has an unknown linearity, which refuses the run.
TEST_F(SpccProcessReadinessTest, UnknownImageStateBlocksExecution)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.image = image({.state = std::nullopt});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.imageStateChecked);
    EXPECT_FALSE(report.imageStateCompatible);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.imageStateMessage.contains(QStringLiteral("unknown linearity"), Qt::CaseInsensitive));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("image state"), Qt::CaseInsensitive));
}

// readiness: an image declared nonlinear refuses the run.
TEST_F(SpccProcessReadinessTest, NonlinearImageStateBlocksExecution)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.image = image({.state = an::ProcessingStateRecord{.linearity = an::RecordedLinearity::Nonlinear,
                                                             .channelBasis = an::RecordedChannelBasis::DisplayRgb,
                                                             .assignmentSource = an::StateAssignmentSource::Process,
                                                             .operation = "unit-test-stretch"}});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.imageStateChecked);
    EXPECT_FALSE(report.imageStateCompatible);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.imageStateMessage.contains(QStringLiteral("nonlinear"), Qt::CaseInsensitive));
}

// readiness: an image already colour calibrated refuses the run as a warning, and the summary says so rather than
// asking for a response profile.
TEST_F(SpccProcessReadinessTest, AlreadyCalibratedImageStateBlocksExecution)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs = directoryInputs(catalogDirectory(), responseDirectory(),
                                                 QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut"));
    inputs.image = image({.colorCalibrated = true});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.imageStateChecked);
    EXPECT_FALSE(report.imageStateCompatible);
    EXPECT_TRUE(report.imageStateAlreadyColorCalibrated);
    EXPECT_FALSE(report.canExecute());
    EXPECT_EQ(report.severity(), SpccProcessReadinessSeverity::Warning);
    EXPECT_TRUE(report.imageStateMessage.contains(QStringLiteral("already color calibrated"), Qt::CaseInsensitive));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("already color calibrated"), Qt::CaseInsensitive));
    EXPECT_FALSE(report.summary.contains(QStringLiteral("response profile"), Qt::CaseInsensitive));
}

// readiness: an image whose filter is narrowband refuses the run while narrowband passbands are not enabled.
TEST_F(SpccProcessReadinessTest, NarrowbandImageStateBlocksExecution)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    SpccReadinessInputs inputs =
        directoryInputs(catalogDirectory(), responseDirectory(), QStringLiteral("Unit Test Camera"),
                        QStringLiteral("Ha"), QStringLiteral("profile.synthetic.rgb"));
    inputs.image = image({.filter = "Ha"});
    ASSERT_TRUE(inputs.image.has_value());
    const SpccProcessReadinessReport report = decide(inputs);

    EXPECT_TRUE(report.imageStateChecked);
    EXPECT_FALSE(report.imageStateCompatible);
    EXPECT_FALSE(report.canExecute());
    EXPECT_TRUE(report.imageStateMessage.contains(QStringLiteral("narrowband"), Qt::CaseInsensitive));
    EXPECT_TRUE(report.summary.contains(QStringLiteral("narrowband"), Qt::CaseInsensitive));
}

// readiness: two profiles whose exact aliases both name the camera and filter are ambiguous, and the run waits for a
// choice between them.
TEST_F(SpccProcessReadinessTest, AmbiguousResponseProfileAliasBlocksExecution)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory(), true, true, true);

    const SpccProcessReadinessReport report = decide(directoryInputs(
        catalogDirectory(), responseDirectory(), QStringLiteral("Unit Test Camera"), QStringLiteral("UV/IR Cut")));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_FALSE(report.responseProfileResolved);
    EXPECT_TRUE(report.responseProfileAmbiguous);
    EXPECT_FALSE(report.canExecute());
    EXPECT_EQ(report.candidateResponseProfileIds.size(), 2);
    EXPECT_TRUE(report.summary.contains(QStringLiteral("ambiguous"), Qt::CaseInsensitive));
}

// readiness: a profile a person chose resolves with its own white reference when the image names no camera or filter.
TEST_F(SpccProcessReadinessTest, ManualResponseProfileSelectionResolvesMissingMetadata)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory());

    const SpccProcessReadinessReport report = decide(directoryInputs(
        catalogDirectory(), responseDirectory(), QString(), QString(), QStringLiteral("profile.synthetic.rgb")));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.responseProfileManualSelection);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(report.responseProfileId, QStringLiteral("profile.synthetic.rgb"));
    EXPECT_EQ(report.whiteReferenceId, QStringLiteral("white.synthetic.d65"));
    EXPECT_EQ(report.severity(), SpccProcessReadinessSeverity::Ready);
}

// readiness: a profile a person chose settles an ambiguous alias.
TEST_F(SpccProcessReadinessTest, ManualResponseProfileSelectionResolvesAmbiguousMetadata)
{
    installApprovedRegionalCatalog();
    createSpccResponseRepository(responseDirectory(), true, true, true);

    const SpccProcessReadinessReport report =
        decide(directoryInputs(catalogDirectory(), responseDirectory(), QStringLiteral("Unit Test Camera"),
                               QStringLiteral("UV/IR Cut"), QStringLiteral("profile.synthetic.rgb.alt")));

    EXPECT_TRUE(report.catalogReady);
    EXPECT_TRUE(report.responseAssetsReady);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.responseProfileManualSelection);
    EXPECT_FALSE(report.responseProfileAmbiguous);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(report.responseProfileId, QStringLiteral("profile.synthetic.rgb.alt"));
    EXPECT_EQ(report.whiteReferenceId, QStringLiteral("white.synthetic.d65"));
}

// panel: an ambiguous alias lists its candidates in the profile selector, and choosing one reports it.
TEST(SpccProcessReadinessPanelTest, ManualResponseProfileSelectorListsCandidatesAndReportsSelection)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = false;
    report.responseProfileAmbiguous = true;
    report.candidateResponseProfileIds = {QStringLiteral("profile.synthetic.rgb"),
                                          QStringLiteral("profile.synthetic.rgb.alt")};
    report.actionText = QStringLiteral("Open Catalogs & Data...");

    SpccProcessReadinessPanel panel;
    QString selectedProfileId;
    panel.setManualResponseProfileSelectedCallback(
        [&selectedProfileId](const QString& profileId) { selectedProfileId = profileId; });
    panel.setReport(report);

    auto* selector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseProfileCombo"));
    ASSERT_NE(selector, nullptr);
    EXPECT_FALSE(selector->isHidden());

    const int primaryIndex = selector->findData(QStringLiteral("profile.synthetic.rgb"));
    const int alternateIndex = selector->findData(QStringLiteral("profile.synthetic.rgb.alt"));
    EXPECT_GE(primaryIndex, 0);
    ASSERT_GE(alternateIndex, 0);

    selector->setCurrentIndex(alternateIndex);
    EXPECT_EQ(selectedProfileId, QStringLiteral("profile.synthetic.rgb.alt"));
}

// panel: a resolved profile stays selectable among the repository's, the resolved one current.
TEST(SpccProcessReadinessPanelTest, ResolvedResponseProfileSelectorRemainsAvailableAndReportsSelection)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.responseProfileSavedMapping = true;
    report.responseProfileId = QStringLiteral("profile.synthetic.rgb");
    report.availableResponseProfileIds = {QStringLiteral("profile.synthetic.rgb"),
                                          QStringLiteral("profile.synthetic.rgb.alt")};

    SpccProcessReadinessPanel panel;
    QString selectedProfileId;
    panel.setManualResponseProfileSelectedCallback(
        [&selectedProfileId](const QString& profileId) { selectedProfileId = profileId; });
    panel.setReport(report);

    auto* selector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseProfileCombo"));
    ASSERT_NE(selector, nullptr);
    EXPECT_FALSE(selector->isHidden());
    EXPECT_EQ(selector->currentData().toString(), QStringLiteral("profile.synthetic.rgb"));

    const int alternateIndex = selector->findData(QStringLiteral("profile.synthetic.rgb.alt"));
    ASSERT_GE(alternateIndex, 0);
    selector->setCurrentIndex(alternateIndex);
    EXPECT_EQ(selectedProfileId, QStringLiteral("profile.synthetic.rgb.alt"));
}

// panel: structured profile identifiers offer a sensor selector and, per sensor, the colour sensor's filter and DSLR
// window selectors or the monochrome sensor's red, green and blue filter selectors, and a choice reports the profile
// it composes.
TEST(SpccProcessReadinessPanelTest, StructuredResponseProfilesExposeSensorAndFilterSelectors)
{
    const QString canonFilterProfile = QStringLiteral(
        "afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.osc_filter.antila_rgb_ultra_ii.default");
    const QString canonLpfProfile =
        QStringLiteral("afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.osc_lpf.nikon_d90.default");
    const QString canonFilterLpfProfile = QStringLiteral("afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.osc_"
                                                         "filter.antila_rgb_ultra_ii.osc_lpf.nikon_d90.default");
    const QString monoProfile =
        QStringLiteral("afternight.spcc.profile.mono_sensor.icx694.mono_filter.chroma_rgb.default");
    const QString monoAltBlueProfile =
        QStringLiteral("afternight.spcc.profile.mono_sensor.icx694.mono_filter.chroma_rgb_baader_blue.default");

    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.responseProfileId = canonFilterLpfProfile;
    report.whiteReferenceId = QStringLiteral("afternight.spcc.white_reference.avgspiral");
    report.availableResponseProfileIds = {canonFilterProfile, canonLpfProfile, canonFilterLpfProfile, monoProfile,
                                          monoAltBlueProfile};
    report.availableResponseProfileComponents.insert(
        monoProfile, SpccResponseProfileComponentSet{
                         .red = {QStringLiteral("afternight.spcc.mono_sensor.icx694"),
                                 QStringLiteral("afternight.spcc.mono_filter.baader_red.red")},
                         .green = {QStringLiteral("afternight.spcc.mono_sensor.icx694"),
                                   QStringLiteral("afternight.spcc.mono_filter.astronomik_green.green")},
                         .blue = {QStringLiteral("afternight.spcc.mono_sensor.icx694"),
                                  QStringLiteral("afternight.spcc.mono_filter.chroma_blue.blue")},
                     });
    report.availableResponseProfileComponents.insert(
        monoAltBlueProfile, SpccResponseProfileComponentSet{
                                .red = {QStringLiteral("afternight.spcc.mono_sensor.icx694"),
                                        QStringLiteral("afternight.spcc.mono_filter.baader_red.red")},
                                .green = {QStringLiteral("afternight.spcc.mono_sensor.icx694"),
                                          QStringLiteral("afternight.spcc.mono_filter.astronomik_green.green")},
                                .blue = {QStringLiteral("afternight.spcc.mono_sensor.icx694"),
                                         QStringLiteral("afternight.spcc.mono_filter.baader_blue.blue")},
                            });
    report.availableWhiteReferenceIds = {QStringLiteral("afternight.spcc.white_reference.avgspiral"),
                                         QStringLiteral("afternight.spcc.white_reference.g2v")};

    SpccProcessReadinessPanel panel;
    QString selectedProfileId;
    QString selectedWhiteReferenceId;
    panel.setManualResponseProfileSelectedCallback(
        [&selectedProfileId](const QString& profileId) { selectedProfileId = profileId; });
    panel.setManualWhiteReferenceSelectedCallback(
        [&selectedWhiteReferenceId](const QString& whiteReferenceId) { selectedWhiteReferenceId = whiteReferenceId; });
    panel.setReport(report);

    auto* profileSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseProfileCombo"));
    auto* sensorSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseSensorCombo"));
    auto* filterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseFilterCombo"));
    auto* lpfSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseLpfCombo"));
    auto* redFilterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseMonoRedFilterCombo"));
    auto* greenFilterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseMonoGreenFilterCombo"));
    auto* blueFilterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseMonoBlueFilterCombo"));
    auto* whiteSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessWhiteReferenceCombo"));
    auto* sensorLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseSensorLabel"));
    auto* filterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseFilterLabel"));
    auto* lpfLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseLpfLabel"));
    auto* redFilterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseMonoRedFilterLabel"));
    auto* greenFilterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseMonoGreenFilterLabel"));
    auto* blueFilterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseMonoBlueFilterLabel"));
    auto* whiteLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessWhiteReferenceLabel"));
    auto* detailsLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseProfileDetailsLabel"));
    ASSERT_NE(profileSelector, nullptr);
    ASSERT_NE(sensorSelector, nullptr);
    ASSERT_NE(filterSelector, nullptr);
    ASSERT_NE(lpfSelector, nullptr);
    ASSERT_NE(redFilterSelector, nullptr);
    ASSERT_NE(greenFilterSelector, nullptr);
    ASSERT_NE(blueFilterSelector, nullptr);
    ASSERT_NE(whiteSelector, nullptr);
    ASSERT_NE(sensorLabel, nullptr);
    ASSERT_NE(filterLabel, nullptr);
    ASSERT_NE(lpfLabel, nullptr);
    ASSERT_NE(redFilterLabel, nullptr);
    ASSERT_NE(greenFilterLabel, nullptr);
    ASSERT_NE(blueFilterLabel, nullptr);
    ASSERT_NE(whiteLabel, nullptr);
    ASSERT_NE(detailsLabel, nullptr);

    EXPECT_TRUE(profileSelector->isHidden());
    EXPECT_FALSE(sensorSelector->isHidden());
    EXPECT_FALSE(filterSelector->isHidden());
    EXPECT_FALSE(lpfSelector->isHidden());
    EXPECT_TRUE(redFilterSelector->isHidden());
    EXPECT_TRUE(greenFilterSelector->isHidden());
    EXPECT_TRUE(blueFilterSelector->isHidden());
    EXPECT_FALSE(whiteSelector->isHidden());
    EXPECT_FALSE(sensorLabel->isHidden());
    EXPECT_FALSE(filterLabel->isHidden());
    EXPECT_FALSE(lpfLabel->isHidden());
    EXPECT_TRUE(redFilterLabel->isHidden());
    EXPECT_TRUE(greenFilterLabel->isHidden());
    EXPECT_TRUE(blueFilterLabel->isHidden());
    EXPECT_FALSE(whiteLabel->isHidden());
    EXPECT_EQ(sensorLabel->text(), QStringLiteral("Sensor:"));
    EXPECT_EQ(filterLabel->text(), QStringLiteral("OSC filter:"));
    EXPECT_EQ(whiteLabel->text(), QStringLiteral("White reference:"));
    EXPECT_EQ(sensorSelector->property("anComboPopupAnchored").toString(), QStringLiteral("true"));
    EXPECT_LE(sensorSelector->maxVisibleItems(), 12);
    EXPECT_LE(filterSelector->maxVisibleItems(), 12);
    EXPECT_LE(lpfSelector->maxVisibleItems(), 12);
    EXPECT_LE(whiteSelector->maxVisibleItems(), 12);
    EXPECT_TRUE(sensorSelector->currentText().contains(QStringLiteral("Canon EOS 1D Mark III")));
    EXPECT_TRUE(filterSelector->currentText().contains(QStringLiteral("Antila RGB Ultra II")));
    EXPECT_TRUE(lpfSelector->currentText().contains(QStringLiteral("Nikon D90")));
    EXPECT_EQ(whiteSelector->currentData().toString(), QStringLiteral("afternight.spcc.white_reference.avgspiral"));
    EXPECT_TRUE(whiteSelector->currentText().contains(QStringLiteral("Average Spiral Galaxy")));
    EXPECT_TRUE(detailsLabel->text().contains(QStringLiteral("Average Spiral Galaxy")));

    const int g2vIndex = whiteSelector->findData(QStringLiteral("afternight.spcc.white_reference.g2v"));
    ASSERT_GE(g2vIndex, 0);
    whiteSelector->setCurrentIndex(g2vIndex);
    EXPECT_EQ(selectedWhiteReferenceId, QStringLiteral("afternight.spcc.white_reference.g2v"));

    const int noLpfIndex = lpfSelector->findData(QString());
    ASSERT_GE(noLpfIndex, 0);
    lpfSelector->setCurrentIndex(noLpfIndex);
    EXPECT_EQ(selectedProfileId, canonFilterProfile);

    selectedProfileId.clear();
    const int nikonLpfIndex = lpfSelector->findData(QStringLiteral("nikon_d90"));
    ASSERT_GE(nikonLpfIndex, 0);
    lpfSelector->setCurrentIndex(nikonLpfIndex);
    EXPECT_EQ(selectedProfileId, canonFilterLpfProfile);

    int monoSensorIndex = -1;
    for (int index = 0; index < sensorSelector->count(); ++index)
    {
        if (sensorSelector->itemText(index).contains(QStringLiteral("ICX694")))
        {
            monoSensorIndex = index;
            break;
        }
    }
    ASSERT_GE(monoSensorIndex, 0);
    sensorSelector->setCurrentIndex(monoSensorIndex);

    EXPECT_TRUE(filterSelector->isHidden());
    EXPECT_TRUE(lpfSelector->isHidden());
    EXPECT_FALSE(redFilterSelector->isHidden());
    EXPECT_FALSE(greenFilterSelector->isHidden());
    EXPECT_FALSE(blueFilterSelector->isHidden());
    EXPECT_TRUE(filterLabel->isHidden());
    EXPECT_TRUE(lpfLabel->isHidden());
    EXPECT_FALSE(redFilterLabel->isHidden());
    EXPECT_FALSE(greenFilterLabel->isHidden());
    EXPECT_FALSE(blueFilterLabel->isHidden());
    EXPECT_TRUE(redFilterSelector->currentText().contains(QStringLiteral("Baader Red")));
    EXPECT_TRUE(greenFilterSelector->currentText().contains(QStringLiteral("Astronomik Green")));
    EXPECT_TRUE(blueFilterSelector->currentText().contains(QStringLiteral("Chroma Blue")));

    const int baaderBlueIndex = blueFilterSelector->findData(QStringLiteral("baader_blue"));
    ASSERT_GE(baaderBlueIndex, 0);
    blueFilterSelector->setCurrentIndex(baaderBlueIndex);
    EXPECT_EQ(selectedProfileId, monoAltBlueProfile);
}

// panel: a colour sensor's filter and DSLR window selectors are independent, and a combination the repository does
// not hold is reported by the identifier the two compose.
TEST(SpccProcessReadinessPanelTest, OscFilterAndDslrLpfSelectorsRemainIndependentWithoutPrebuiltCombination)
{
    const QString canonBaseProfile = QStringLiteral("afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.default");
    const QString canonFilterProfile = QStringLiteral(
        "afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.osc_filter.antila_rgb_ultra_ii.default");
    const QString canonLpfProfile =
        QStringLiteral("afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.osc_lpf.nikon_d90.default");
    const QString canonFilterLpfProfile = QStringLiteral("afternight.spcc.profile.osc_sensor.canon_eos_1d_mark_iii.osc_"
                                                         "filter.antila_rgb_ultra_ii.osc_lpf.nikon_d90.default");

    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.responseProfileId = canonFilterProfile;
    report.availableResponseProfileIds = {canonBaseProfile, canonFilterProfile, canonLpfProfile};

    SpccProcessReadinessPanel panel;
    QString selectedProfileId;
    panel.setManualResponseProfileSelectedCallback(
        [&selectedProfileId](const QString& profileId) { selectedProfileId = profileId; });
    panel.setReport(report);

    auto* filterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseFilterCombo"));
    auto* lpfSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseLpfCombo"));
    ASSERT_NE(filterSelector, nullptr);
    ASSERT_NE(lpfSelector, nullptr);

    EXPECT_FALSE(filterSelector->isHidden());
    EXPECT_FALSE(lpfSelector->isHidden());
    EXPECT_EQ(filterSelector->currentData().toString(), QStringLiteral("antila_rgb_ultra_ii"));
    EXPECT_EQ(lpfSelector->currentData().toString(), QString());
    EXPECT_GE(filterSelector->findData(QStringLiteral("antila_rgb_ultra_ii")), 0);
    EXPECT_GE(lpfSelector->findData(QStringLiteral("nikon_d90")), 0);

    const int lpfIndex = lpfSelector->findData(QStringLiteral("nikon_d90"));
    ASSERT_GE(lpfIndex, 0);
    lpfSelector->setCurrentIndex(lpfIndex);
    EXPECT_EQ(selectedProfileId, canonFilterLpfProfile);
}

// panel: a monochrome sensor's channel selectors list every filter its profiles' components name, and a choice of all
// three reports the profile that has them.
TEST(SpccProcessReadinessPanelTest, MonoSensorFilterSelectorsListAllAvailableChannelFilters)
{
    const QString antliaProfile = QStringLiteral(
        "afternight.spcc.profile.mono_sensor.zwo_asi1600mm.mono_filter.antlia_lrgb_v_pro_astronomy_filters."
        "default");
    const QString astrodonProfile =
        QStringLiteral("afternight.spcc.profile.mono_sensor.zwo_asi1600mm.mono_filter.astrodon_gen_2_rgb.default");
    const QString chromaProfile =
        QStringLiteral("afternight.spcc.profile.mono_sensor.zwo_asi1600mm.mono_filter.chroma_rgb.default");

    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.responseProfileId = antliaProfile;
    report.availableResponseProfileIds = {antliaProfile, astrodonProfile, chromaProfile};
    report.availableResponseProfileComponents.insert(
        antliaProfile,
        SpccResponseProfileComponentSet{
            .red = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                    QStringLiteral("afternight.spcc.mono_filter.antlia_lrgb_v_pro_astronomy_filters.red")},
            .green = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                      QStringLiteral("afternight.spcc.mono_filter.antlia_lrgb_v_pro_astronomy_filters.green")},
            .blue = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                     QStringLiteral("afternight.spcc.mono_filter.antlia_lrgb_v_pro_astronomy_filters.blue")},
        });
    report.availableResponseProfileComponents.insert(
        astrodonProfile, SpccResponseProfileComponentSet{
                             .red = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                                     QStringLiteral("afternight.spcc.mono_filter.astrodon_gen_2_rgb.red.2")},
                             .green = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                                       QStringLiteral("afternight.spcc.mono_filter.astrodon_gen_2_rgb.green.2")},
                             .blue = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                                      QStringLiteral("afternight.spcc.mono_filter.astrodon_gen_2_rgb.blue")},
                         });
    report.availableResponseProfileComponents.insert(
        chromaProfile, SpccResponseProfileComponentSet{
                           .red = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                                   QStringLiteral("afternight.spcc.mono_filter.chroma_rgb.red")},
                           .green = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                                     QStringLiteral("afternight.spcc.mono_filter.chroma_rgb.green")},
                           .blue = {QStringLiteral("afternight.spcc.mono_sensor.zwo_asi1600mm"),
                                    QStringLiteral("afternight.spcc.mono_filter.chroma_rgb.blue")},
                       });

    SpccProcessReadinessPanel panel;
    QString selectedProfileId;
    panel.setManualResponseProfileSelectedCallback(
        [&selectedProfileId](const QString& profileId) { selectedProfileId = profileId; });
    panel.setReport(report);

    auto* redFilterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseMonoRedFilterCombo"));
    auto* greenFilterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseMonoGreenFilterCombo"));
    auto* blueFilterSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseMonoBlueFilterCombo"));
    auto* redFilterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseMonoRedFilterLabel"));
    auto* greenFilterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseMonoGreenFilterLabel"));
    auto* blueFilterLabel = panel.findChild<QLabel*>(QStringLiteral("spccProcessResponseMonoBlueFilterLabel"));
    ASSERT_NE(redFilterSelector, nullptr);
    ASSERT_NE(greenFilterSelector, nullptr);
    ASSERT_NE(blueFilterSelector, nullptr);
    ASSERT_NE(redFilterLabel, nullptr);
    ASSERT_NE(greenFilterLabel, nullptr);
    ASSERT_NE(blueFilterLabel, nullptr);
    EXPECT_FALSE(redFilterSelector->isHidden());
    EXPECT_FALSE(greenFilterSelector->isHidden());
    EXPECT_FALSE(blueFilterSelector->isHidden());
    EXPECT_FALSE(redFilterLabel->isHidden());
    EXPECT_FALSE(greenFilterLabel->isHidden());
    EXPECT_FALSE(blueFilterLabel->isHidden());
    EXPECT_EQ(redFilterLabel->text(), QStringLiteral("Red filter:"));
    EXPECT_EQ(greenFilterLabel->text(), QStringLiteral("Green filter:"));
    EXPECT_EQ(blueFilterLabel->text(), QStringLiteral("Blue filter:"));

    EXPECT_GE(redFilterSelector->findData(QStringLiteral("antlia_lrgb_v_pro_astronomy_filters")), 0);
    EXPECT_GE(redFilterSelector->findData(QStringLiteral("astrodon_gen_2_rgb")), 0);
    EXPECT_GE(redFilterSelector->findData(QStringLiteral("chroma_rgb")), 0);
    EXPECT_GE(greenFilterSelector->findData(QStringLiteral("astrodon_gen_2_rgb")), 0);
    EXPECT_GE(blueFilterSelector->findData(QStringLiteral("chroma_rgb")), 0);

    const int astrodonRedIndex = redFilterSelector->findData(QStringLiteral("astrodon_gen_2_rgb"));
    const int astrodonGreenIndex = greenFilterSelector->findData(QStringLiteral("astrodon_gen_2_rgb"));
    const int astrodonBlueIndex = blueFilterSelector->findData(QStringLiteral("astrodon_gen_2_rgb"));
    ASSERT_GE(astrodonRedIndex, 0);
    ASSERT_GE(astrodonGreenIndex, 0);
    ASSERT_GE(astrodonBlueIndex, 0);
    redFilterSelector->setCurrentIndex(astrodonRedIndex);
    greenFilterSelector->setCurrentIndex(astrodonGreenIndex);
    blueFilterSelector->setCurrentIndex(astrodonBlueIndex);

    EXPECT_EQ(selectedProfileId, astrodonProfile);
}

// panel: with no catalog, the manage action shows and opens the Catalogs & Data page.
TEST(SpccProcessReadinessPanelTest, ManageActionOpensCatalogsDataSettingsTarget)
{
    const std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);

    SpccProcessReadinessPanel panel;
    QString openedPage;
    panel.setOpenSettingsPageCallback([&openedPage](const QString& pageName) { openedPage = pageName; });
    panel.setReport(decideReadiness(context->engineSession(), SpccReadinessInputs{}));

    auto* manageButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessManageCatalogsDataButton"));
    ASSERT_NE(manageButton, nullptr);
    EXPECT_FALSE(manageButton->isHidden());
    EXPECT_EQ(manageButton->text(), QStringLiteral("Open Catalogs & Data..."));

    manageButton->click();
    EXPECT_EQ(openedPage, QStringLiteral("Catalogs & Data"));
}

// panel: missing response profiles that can be downloaded show only the download action, which asks for them.
TEST(SpccProcessReadinessPanelTest, MissingResponseAssetsShowsProcessDownloadActionOnly)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = false;
    report.responseAssetsDownloadRequired = true;
    report.responseAssetsAutoDownloadAvailable = true;
    report.responseAssetsMessage =
        QStringLiteral("Download SPCC response profiles to choose the camera/sensor/filter response before running.");
    report.settingsPageTitle = QStringLiteral("Catalogs & Data");
    report.actionText = QStringLiteral("Open Catalogs & Data...");
    report.summary = report.responseAssetsMessage;

    SpccProcessReadinessPanel panel;
    bool responseDownloadRequested = false;
    panel.setDownloadResponseProfilesCallback([&responseDownloadRequested]() { responseDownloadRequested = true; });
    panel.setReport(report);

    auto* manageButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessManageCatalogsDataButton"));
    auto* fullButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadFullCatalogButton"));
    auto* regionButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadNeededRegionButton"));
    auto* responseButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadResponseProfilesButton"));
    auto* sensorSelector = panel.findChild<QComboBox*>(QStringLiteral("spccProcessResponseSensorCombo"));
    ASSERT_NE(manageButton, nullptr);
    ASSERT_NE(fullButton, nullptr);
    ASSERT_NE(regionButton, nullptr);
    ASSERT_NE(responseButton, nullptr);
    ASSERT_NE(sensorSelector, nullptr);

    EXPECT_FALSE(panel.report().canExecute());
    EXPECT_TRUE(manageButton->isHidden());
    EXPECT_TRUE(fullButton->isHidden());
    EXPECT_TRUE(regionButton->isHidden());
    EXPECT_FALSE(responseButton->isHidden());
    EXPECT_TRUE(responseButton->isEnabled());
    EXPECT_TRUE(sensorSelector->isHidden());

    responseButton->click();
    EXPECT_TRUE(responseDownloadRequested);
}

// panel: a profile choice still pending, with the catalog preparable when the run starts, shows a warning banner.
TEST(SpccProcessReadinessPanelTest, PendingResponseProfileSelectionUsesWarningTone)
{
    SpccProcessReadinessReport report;
    report.catalogReady = false;
    report.catalogRightsApproved = true;
    report.catalogRegionChecked = true;
    report.catalogRegionAvailable = false;
    report.catalogChunkDownloadRequired = true;
    report.catalogChunkAutoDownloadAvailable = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = false;
    report.availableResponseProfileIds = {QStringLiteral("profile.synthetic.rgb")};
    report.responseProfileMessage =
        QStringLiteral("SPCC response profile is unresolved. Choose a response profile before calibration.");
    report.summary = report.responseProfileMessage;

    SpccProcessReadinessPanel panel;
    panel.setReport(report);

    auto* bannerWidget = panel.findChild<QWidget*>(QStringLiteral("spccProcessReadinessBanner"));
    auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(bannerWidget);
    ASSERT_NE(banner, nullptr);

    EXPECT_FALSE(panel.report().canExecute());
    EXPECT_EQ(panel.report().severity(), SpccProcessReadinessSeverity::Warning);
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
}

// panel: a regional catalog that misses the field and an image state to assign show the catalog, region and image
// state actions, each calling what was set for it.
TEST(SpccProcessReadinessPanelTest, ProcessActionsExposeCatalogAndImageStateCallbacks)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.catalogRegionChecked = true;
    report.catalogRegionAvailable = false;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.imageStateChecked = true;
    report.imageStateCompatible = false;
    report.settingsPageTitle = QStringLiteral("Catalogs & Data");
    report.actionText = QStringLiteral("Open Catalogs & Data...");
    report.summary = QStringLiteral("Needs regional catalog and image state assignment.");

    SpccProcessReadinessPanel panel;
    bool fullRequested = false;
    bool regionRequested = false;
    bool imageStateRequested = false;
    panel.setDownloadFullCatalogCallback([&fullRequested]() { fullRequested = true; });
    panel.setDownloadNeededRegionCallback([&regionRequested]() { regionRequested = true; });
    panel.setAssignImageStateCallback([&imageStateRequested]() { imageStateRequested = true; });
    panel.setReport(report);

    auto* fullButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadFullCatalogButton"));
    auto* regionButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadNeededRegionButton"));
    auto* assignButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessAssignImageStateButton"));
    ASSERT_NE(fullButton, nullptr);
    ASSERT_NE(regionButton, nullptr);
    ASSERT_NE(assignButton, nullptr);
    EXPECT_FALSE(fullButton->isHidden());
    EXPECT_FALSE(regionButton->isHidden());
    EXPECT_FALSE(assignButton->isHidden());

    fullButton->click();
    regionButton->click();
    assignButton->click();

    EXPECT_TRUE(fullRequested);
    EXPECT_TRUE(regionRequested);
    EXPECT_TRUE(imageStateRequested);
}

// panel: an image already colour calibrated shows a warning banner and offers no state assignment.
TEST(SpccProcessReadinessPanelTest, AlreadyCalibratedImageStateUsesWarningToneWithoutAssignmentAction)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.imageStateChecked = true;
    report.imageStateCompatible = false;
    report.imageStateAlreadyColorCalibrated = true;
    report.responseProfileId = QStringLiteral("profile.synthetic.rgb");
    report.whiteReferenceId = QStringLiteral("afternight.spcc.white_reference.avgspiral");
    report.summary = QStringLiteral("SPCC input image is already color calibrated.");

    SpccProcessReadinessPanel panel;
    panel.setReport(report);

    auto* bannerWidget = panel.findChild<QWidget*>(QStringLiteral("spccProcessReadinessBanner"));
    auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(bannerWidget);
    auto* assignButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessAssignImageStateButton"));
    ASSERT_NE(banner, nullptr);
    ASSERT_NE(assignButton, nullptr);

    EXPECT_FALSE(panel.report().canExecute());
    EXPECT_EQ(panel.report().severity(), SpccProcessReadinessSeverity::Warning);
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(assignButton->isHidden());
}

// panel: catalog chunks the run downloads by itself show no catalog, region or manage action.
TEST(SpccProcessReadinessPanelTest, AutoDownloadableCatalogChunksDoNotShowRegionAction)
{
    SpccProcessReadinessReport report;
    report.catalogReady = false;
    report.catalogRightsApproved = true;
    report.catalogRegionChecked = true;
    report.catalogRegionAvailable = false;
    report.catalogChunkDownloadRequired = true;
    report.catalogChunkAutoDownloadAvailable = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.summary = QStringLiteral("SPCC can start; needed catalog chunks will download first.");

    SpccProcessReadinessPanel panel;
    panel.setReport(report);

    auto* fullButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadFullCatalogButton"));
    auto* regionButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadNeededRegionButton"));
    auto* manageButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessManageCatalogsDataButton"));
    ASSERT_NE(fullButton, nullptr);
    ASSERT_NE(regionButton, nullptr);
    ASSERT_NE(manageButton, nullptr);

    EXPECT_TRUE(panel.report().canExecute());
    EXPECT_TRUE(fullButton->isHidden());
    EXPECT_TRUE(regionButton->isHidden());
    EXPECT_TRUE(manageButton->isHidden());
}

// panel: while the catalog can be downloaded when the run starts, missing response profiles show only their own
// download action.
TEST(SpccProcessReadinessPanelTest, MissingResponseAssetsOnlyShowsResponseActionWhenCatalogCanDownloadOnRun)
{
    SpccProcessReadinessReport report;
    report.catalogReady = false;
    report.catalogRightsApproved = true;
    report.catalogRegionChecked = true;
    report.catalogRegionAvailable = false;
    report.catalogChunkDownloadRequired = true;
    report.catalogChunkAutoDownloadAvailable = true;
    report.responseAssetsReady = false;
    report.responseAssetsDownloadRequired = true;
    report.responseAssetsAutoDownloadAvailable = true;
    report.summary = QStringLiteral(
        "SPCC will download needed catalog chunks when Run starts. Download SPCC response profiles first.");

    SpccProcessReadinessPanel panel;
    panel.setReport(report);

    auto* fullButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadFullCatalogButton"));
    auto* regionButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadNeededRegionButton"));
    auto* manageButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessManageCatalogsDataButton"));
    auto* responseButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadResponseProfilesButton"));
    ASSERT_NE(fullButton, nullptr);
    ASSERT_NE(regionButton, nullptr);
    ASSERT_NE(manageButton, nullptr);
    ASSERT_NE(responseButton, nullptr);

    EXPECT_FALSE(panel.report().canExecute());
    EXPECT_TRUE(fullButton->isHidden());
    EXPECT_TRUE(regionButton->isHidden());
    EXPECT_TRUE(manageButton->isHidden());
    EXPECT_FALSE(responseButton->isHidden());
    EXPECT_TRUE(responseButton->isEnabled());
}

// panel: a regional cache that misses the field, with no managed download for it, shows the region action.
TEST(SpccProcessReadinessPanelTest, TargetMissingRegionalCoverageShowsRegionAction)
{
    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.catalogRegionChecked = true;
    report.catalogRegionAvailable = false;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.summary = QStringLiteral("The installed SPCC regional cache does not cover this WCS footprint.");

    SpccProcessReadinessPanel panel;
    panel.setReport(report);

    auto* regionButton = panel.findChild<QPushButton*>(QStringLiteral("spccProcessDownloadNeededRegionButton"));
    ASSERT_NE(regionButton, nullptr);
    EXPECT_FALSE(regionButton->isHidden());
    EXPECT_TRUE(regionButton->isEnabled());
    EXPECT_FALSE(panel.report().canExecute());
}

// diagnostics: a refused fit's summary gives the quality score, the sample counts, the gains, the fit's verdict, the
// sparse-field guidance, the rejected sources and the run's diagnostics.
TEST(SpccProcessDiagnosticsTest, FormatsFitSummaryWarningsAndSparseFieldGuidance)
{
    SpccRunSummary summary;
    summary.calibrated = false;
    summary.message = QStringLiteral("fit failed");
    summary.wcsMatches = 18u;
    summary.rmsResidualPixels = 1.25;
    summary.rmsResidualArcseconds = 2.5;
    summary.acceptedSamples = 12u;
    summary.rejectedSamples = 6u;
    summary.gains = {1.12, 1.0, 0.92};
    summary.fitVerdict = QString::fromLatin1(
        an::spectrophotometricFitVerdictName(an::SpectrophotometricFitVerdict::ExcessiveGainUncertainty));
    summary.rejectedOutlierSources = {42u, 84u};
    // The preflight's warnings and the fit's diagnostics reach the text as the run's workflow diagnostics: the engine's
    // result carries no warning list of its own.
    summary.workflowDiagnostics = {QStringLiteral("stale external WCS"), QStringLiteral("red fit scatter high")};

    const QString diagnostics = an::ui::processes::formatSpccWorkflowDiagnostics(summary);

    EXPECT_TRUE(diagnostics.contains(QStringLiteral("Quality score"), Qt::CaseInsensitive));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("accepted samples: 12"), Qt::CaseInsensitive));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("Gains R=1.120"), Qt::CaseInsensitive));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("excessive gain uncertainty"), Qt::CaseInsensitive));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("sparse"), Qt::CaseInsensitive));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("42")));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("stale external WCS"), Qt::CaseInsensitive));
}

// diagnostics: an accepted fit on fewer inliers than is comfortable still gives the sparse-field guidance.
TEST(SpccProcessDiagnosticsTest, FormatsLowSupportGuidanceForSuccessfulFitsBelowComfortThreshold)
{
    SpccRunSummary summary;
    summary.calibrated = true;
    summary.message = QStringLiteral("fit succeeded");
    summary.wcsMatches = 44u;
    summary.rmsResidualPixels = 0.75;
    summary.rmsResidualArcseconds = 1.1;
    summary.acceptedSamples = 42u;
    summary.gains = {1.03, 1.0, 0.97};
    summary.usableFitSamples = 42u;
    summary.fitSupport = 42u;

    const QString diagnostics = an::ui::processes::formatSpccWorkflowDiagnostics(summary);

    EXPECT_TRUE(diagnostics.contains(QStringLiteral("Sparse-field guidance"), Qt::CaseInsensitive));
}

// diagnostics: a run the catalog's coverage failed gives the catalog coverage guidance, not the sparse-field one.
TEST(SpccProcessDiagnosticsTest, FormatsCatalogCoverageGuidanceForCatalogFootprintFailures)
{
    SpccRunSummary summary;
    summary.calibrated = false;
    summary.message =
        QStringLiteral("SPCC catalog readiness rejected: SPCC catalog does not cover the requested image footprint.");
    summary.workflowDiagnostics = {
        QStringLiteral("SPCC catalog coverage: installed catalog does not declare data chunks.")};
    // A coverage failure is a fact the run states rather than words the formatter searches its messages for.
    summary.catalogCoverageFailure = true;

    const QString diagnostics = an::ui::processes::formatSpccWorkflowDiagnostics(summary);

    EXPECT_TRUE(diagnostics.contains(QStringLiteral("Catalog coverage guidance"), Qt::CaseInsensitive));
    EXPECT_FALSE(diagnostics.contains(QStringLiteral("Sparse-field guidance"), Qt::CaseInsensitive));
}

// status: a run's status line names a completion with its samples and gains, a target lost before the result landed,
// a cancellation, and a failure with its reason.
TEST(SpccProcessDiagnosticsTest, ARefusedRunShowsItsReasonWithoutFigures)
{
    an::ui::processes::SpccRunSummary summary;
    summary.calibrated = false;
    summary.message = QStringLiteral("the spectrophotometric fit was refused: insufficient-samples");
    summary.workflowDiagnostics = {QStringLiteral("ImageIntegration handoff: no workflow archive matched this target")};

    const QString diagnostics = an::ui::processes::formatSpccWorkflowDiagnostics(summary);

    EXPECT_TRUE(diagnostics.contains(summary.message));
    EXPECT_TRUE(diagnostics.contains(QStringLiteral("no workflow archive matched")));
    EXPECT_FALSE(diagnostics.contains(QStringLiteral("Quality score"), Qt::CaseInsensitive));
    EXPECT_FALSE(diagnostics.contains(QStringLiteral("Gains R="), Qt::CaseInsensitive));
    EXPECT_FALSE(diagnostics.contains(QStringLiteral("Sparse-field guidance"), Qt::CaseInsensitive));
}

TEST(SpccProcessDiagnosticsTest, FormatsLowSupportGuidanceTheEngineFlagged)
{
    an::ui::processes::SpccRunSummary summary;
    summary.calibrated = true;
    summary.acceptedSamples = 65u;
    summary.usableFitSamples = 65u;
    summary.fitSupport = 65u;
    summary.lowSupport = true;
    summary.gains = {1.0, 1.0, 1.0};

    const QString diagnostics = an::ui::processes::formatSpccWorkflowDiagnostics(summary);

    EXPECT_TRUE(diagnostics.contains(QStringLiteral("Sparse-field guidance"), Qt::CaseInsensitive))
        << "the engine's low-support count is the dialog's minimum when that is the larger";
}

TEST(SpccProcessDiagnosticsTest, FormatsFinalizationStatusForSuccessCancellationAndLostTarget)
{
    SpccRunSummary summary;
    summary.acceptedSamples = 42u;
    summary.gains = {1.125, 1.0, 0.875};

    const QString successStatus = an::ui::processes::formatSpccProcessFinalizationStatus(summary, true, false, true);
    EXPECT_TRUE(successStatus.contains(QStringLiteral("completed"), Qt::CaseInsensitive));
    EXPECT_TRUE(successStatus.contains(QStringLiteral("Samples: 42"), Qt::CaseInsensitive));
    EXPECT_TRUE(successStatus.contains(QStringLiteral("R=1.125"), Qt::CaseInsensitive));

    const QString lostTargetStatus =
        an::ui::processes::formatSpccProcessFinalizationStatus(summary, true, false, false);
    EXPECT_TRUE(lostTargetStatus.contains(QStringLiteral("closed or changed"), Qt::CaseInsensitive));

    const QString cancelledStatus = an::ui::processes::formatSpccProcessFinalizationStatus(summary, false, true, false);
    EXPECT_EQ(cancelledStatus, QStringLiteral("SPCC cancelled."));

    const QString failedStatus = an::ui::processes::formatSpccProcessFinalizationStatus(
        summary, false, false, false, QStringLiteral("catalog unavailable"));
    EXPECT_TRUE(failedStatus.contains(QStringLiteral("failed"), Qt::CaseInsensitive));
    EXPECT_TRUE(failedStatus.contains(QStringLiteral("catalog unavailable"), Qt::CaseInsensitive));
}

// shell: the process shell keeps a 720p-friendly size, the status and progress above the scrolled options, two
// collapsed sections and no observing-conditions or narrowband section, the dialog's defaults and a read-only
// diagnostics panel.
TEST(SpectrophotometricColorCalibrationProcessUiTest, ProcessShellKeeps720pFriendlyMinimumAndDiagnosticsPanel)
{
    QWidget shell;
    const an::ui::processes::SpectrophotometricColorCalibrationUiElements ui =
        an::ui::processes::buildSpectrophotometricColorCalibrationProcessUi(&shell);

    ASSERT_NE(ui.optionsScrollArea, nullptr);
    ASSERT_NE(ui.optionsScrollContent, nullptr);
    ASSERT_NE(ui.statusLabel, nullptr);
    ASSERT_NE(ui.progressBar, nullptr);
    EXPECT_EQ(shell.size(), QSize(560, 720));
    EXPECT_EQ(shell.maximumSize(), QSize(620, 820));
    EXPECT_EQ(ui.statusLabel->parentWidget(), &shell);
    EXPECT_EQ(ui.progressBar->parentWidget(), &shell);
    ASSERT_NE(shell.layout(), nullptr);
    EXPECT_EQ(shell.layout()->indexOf(ui.statusLabel), 0);
    EXPECT_EQ(shell.layout()->indexOf(ui.progressBar), 1);
    EXPECT_EQ(shell.layout()->indexOf(ui.optionsScrollArea), 2);
    EXPECT_EQ(ui.optionsScrollArea, shell.findChild<QScrollArea*>(QStringLiteral("spccProcessOptionsScrollArea")));
    EXPECT_EQ(ui.optionsScrollContent, shell.findChild<QWidget*>(QStringLiteral("spccProcessOptionsScrollContent")));
    EXPECT_EQ(shell.findChild<QLabel*>(QStringLiteral("spccProcessIntroLabel")), nullptr);
    ASSERT_NE(ui.optionsScrollContent->layout(), nullptr);
    const QMargins optionsMargins = ui.optionsScrollContent->layout()->contentsMargins();
    EXPECT_GT(optionsMargins.left(), 0);
    EXPECT_GT(optionsMargins.top(), 0);
    EXPECT_GT(optionsMargins.right(), 0);
    EXPECT_GT(optionsMargins.bottom(), 0);
    ASSERT_NE(ui.catalogWcsPreflightSection, nullptr);
    ASSERT_NE(ui.photometryFitSection, nullptr);
    auto* catalogSection = dynamic_cast<an::ui::AnCollapsibleSection*>(
        shell.findChild<QWidget*>(QStringLiteral("spccCatalogWcsPreflightSection")));
    auto* photometrySection = dynamic_cast<an::ui::AnCollapsibleSection*>(
        shell.findChild<QWidget*>(QStringLiteral("spccPhotometryFitSection")));
    EXPECT_EQ(ui.catalogWcsPreflightSection, catalogSection);
    EXPECT_EQ(ui.photometryFitSection, photometrySection);
    EXPECT_EQ(ui.observingConditionsSection, nullptr);
    EXPECT_EQ(ui.narrowbandSection, nullptr);
    EXPECT_EQ(ui.atmosphereCorrectionCheck, nullptr);
    EXPECT_EQ(ui.narrowbandModeCheck, nullptr);
    EXPECT_EQ(shell.findChild<QWidget*>(QStringLiteral("spccObservingConditionsSection")), nullptr);
    EXPECT_EQ(shell.findChild<QWidget*>(QStringLiteral("spccNarrowbandSection")), nullptr);
    EXPECT_FALSE(ui.catalogWcsPreflightSection->isExpanded());
    EXPECT_FALSE(ui.photometryFitSection->isExpanded());
    EXPECT_LE(shell.minimumSize().height(), 720);
    ASSERT_NE(ui.minimumSamplesSpin, nullptr);
    EXPECT_EQ(ui.minimumSamplesSpin->value(), static_cast<int>(an::kDefaultSpectrophotometricMinimumSamples));
    ASSERT_NE(ui.autoMagnitudeLimitCheck, nullptr);
    EXPECT_TRUE(ui.autoMagnitudeLimitCheck->isChecked());
    ASSERT_NE(ui.magnitudeLimitSpin, nullptr);
    EXPECT_DOUBLE_EQ(ui.magnitudeLimitSpin->value(), 17.6);
    EXPECT_FALSE(ui.magnitudeLimitSpin->isEnabled());
    ASSERT_NE(ui.apertureScaleSpin, nullptr);
    EXPECT_DOUBLE_EQ(ui.apertureScaleSpin->value(), 2.0);
    ASSERT_NE(ui.fitOutlierPolicyCombo, nullptr);
    EXPECT_EQ(ui.fitOutlierPolicyCombo->currentData().toInt(),
              static_cast<int>(an::SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit));
    EXPECT_GE(ui.fitOutlierPolicyCombo->findData(
                  static_cast<int>(an::SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask)),
              0);
    ASSERT_NE(ui.diagnosticsText, nullptr);
    EXPECT_NE(shell.findChild<QPlainTextEdit*>(QStringLiteral("spccDiagnosticsText")), nullptr);
    EXPECT_TRUE(ui.diagnosticsText->isReadOnly());
}

// shell: with both sections expanded at 620 by 720, each section shows its whole content and the scrolled options'
// items do not overlap.
TEST(SpectrophotometricColorCalibrationProcessUiTest, ExpandedProcessShellKeepsSectionsReadableAndNonOverlapping)
{
    QWidget shell;
    const an::ui::processes::SpectrophotometricColorCalibrationUiElements ui =
        an::ui::processes::buildSpectrophotometricColorCalibrationProcessUi(&shell);
    ASSERT_NE(ui.optionsScrollArea, nullptr);
    ASSERT_NE(ui.optionsScrollContent, nullptr);
    ASSERT_NE(ui.catalogWcsPreflightSection, nullptr);
    ASSERT_NE(ui.photometryFitSection, nullptr);

    SpccProcessReadinessReport report;
    report.catalogReady = true;
    report.catalogRightsApproved = true;
    report.responseAssetsReady = true;
    report.responseProfileResolved = true;
    report.imageStateChecked = true;
    report.imageStateCompatible = true;
    report.responseProfileId = QStringLiteral("afternight.spcc.profile.mono_sensor.zwo_asi1600mm.mono_filter."
                                              "antlia_lrgb_v_pro_astronomy_filters.default");
    report.whiteReferenceId = QStringLiteral("afternight.spcc.white_reference.avgspiral");
    report.availableResponseProfileIds = {report.responseProfileId};
    report.availableWhiteReferenceIds = {report.whiteReferenceId};
    report.summary = QStringLiteral(
        "SPCC prerequisites are ready. Response profile resolved from the AfterNight response baseline with Gaia XP "
        "catalog "
        "coverage available for the selected target.");
    ui.readinessPanel->setReport(report);
    ui.statusLabel->setText(report.summary);
    ui.diagnosticsText->setPlainText(QStringLiteral(
        "Quality score: 82/100\nWCS matches: 416, RMS residual: 2.648 px / 2.350 arcsec\nAccepted samples: 128"));
    ui.catalogWcsPreflightSection->setExpanded(true);
    ui.photometryFitSection->setExpanded(true);

    shell.resize(620, 720);
    shell.show();
    QApplication::processEvents();

    EXPECT_GE(ui.catalogWcsPreflightSection->contentWidget()->height(),
              ui.catalogWcsPreflightSection->contentLayout()->sizeHint().height());
    EXPECT_GE(ui.photometryFitSection->contentWidget()->height(),
              ui.photometryFitSection->contentLayout()->sizeHint().height());
    EXPECT_GE(ui.optionsScrollContent->height(), ui.optionsScrollArea->viewport()->height());
    expectLayoutItemsDoNotOverlap(ui.optionsScrollContent->layout(), shell);
}

// shell: a blocked and a ready report each render inside a 720p and a 1080p screen, every visible widget inside the
// shell or its scrolled viewport, and paint something.
TEST(SpectrophotometricColorCalibrationProcessUiTest, ProcessShellRendersReadyAndBlockedStatesAt720pAnd1080p)
{
    const auto renderShell = [](const QSize& viewport, const SpccProcessReadinessReport& report) {
        QWidget shell;
        const an::ui::processes::SpectrophotometricColorCalibrationUiElements ui =
            an::ui::processes::buildSpectrophotometricColorCalibrationProcessUi(&shell);
        ASSERT_NE(ui.readinessPanel, nullptr);
        ASSERT_NE(ui.statusLabel, nullptr);
        ASSERT_NE(ui.diagnosticsText, nullptr);

        ui.readinessPanel->setReport(report);
        ui.statusLabel->setText(report.summary);
        ui.diagnosticsText->setPlainText(QStringLiteral(
            "Quality score: 82/100\nWCS matches: 64, RMS residual: 0.8 px / 1.1 arcsec\nAccepted samples: 52"));

        shell.show();
        QApplication::processEvents();

        EXPECT_LE(shell.width(), viewport.width());
        EXPECT_LE(shell.height(), viewport.height());
        expectVisibleChildrenInside(shell);

        QImage capture(shell.size(), QImage::Format_ARGB32_Premultiplied);
        capture.fill(Qt::transparent);
        shell.render(&capture);
        EXPECT_TRUE(imageHasPaintedPixels(capture));
    };

    SpccProcessReadinessReport blockedReport;
    blockedReport.catalogReady = false;
    blockedReport.responseAssetsReady = true;
    blockedReport.responseProfileResolved = false;
    blockedReport.imageStateChecked = true;
    blockedReport.imageStateCompatible = false;
    blockedReport.settingsPageTitle = QStringLiteral("Catalogs & Data");
    blockedReport.actionText = QStringLiteral("Open Catalogs & Data...");
    blockedReport.availableResponseProfileIds = {QStringLiteral("asi533mc-visible"),
                                                 QStringLiteral("generic-rgb-visible")};
    blockedReport.summary = QStringLiteral(
        "SPCC needs a local Gaia XP sampled catalog and a confirmed linear RGB image state before calibration.");

    SpccProcessReadinessReport readyReport;
    readyReport.catalogReady = true;
    readyReport.responseAssetsReady = true;
    readyReport.responseProfileResolved = true;
    readyReport.imageStateChecked = true;
    readyReport.imageStateCompatible = true;
    readyReport.responseProfileId = QStringLiteral("asi533mc-visible");
    readyReport.whiteReferenceId = QStringLiteral("d65");
    readyReport.summary = QStringLiteral("SPCC prerequisites are ready.");

    renderShell(QSize(1280, 720), blockedReport);
    renderShell(QSize(1280, 720), readyReport);
    renderShell(QSize(1920, 1080), blockedReport);
    renderShell(QSize(1920, 1080), readyReport);
}
