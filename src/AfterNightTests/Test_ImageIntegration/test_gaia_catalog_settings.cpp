// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "app/ApplicationStorage.h"
#include "app/GaiaCatalogSettings.h"
#include "app/PlateSolveTypes.h"
#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"
#include "controls/IAppContext.h"
#include "controls/settings/CatalogsDataSettingsPage.h"
#include "controls/settings/PathPickerWidget.h"
#include "dialogs/CatalogDownloadDialog.h"

#include "Test_ImageIntegration/ImportSession.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr const char* kApprovedSpccCatalogRightsReviewStatus = "approved-for-managed-download";

class MemoryGlobalSettings final : public an::ui::IGlobalSettingsProvider
{
public:
    bool upsert(const QString& key, const QString& value) override
    {
        values.insert(key, value);
        return true;
    }

    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return values.value(key, defaultValue);
    }

    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const auto it = values.constFind(key);
        if (it == values.constEnd())
        {
            return std::nullopt;
        }
        return *it;
    }

    QHash<QString, QString> values;
};

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
};

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(stream.is_open()) << path.string();
    stream << contents;
    ASSERT_TRUE(stream.good()) << path.string();
}

QString sha256Hex(const std::filesystem::path& path)
{
    QFile file(QString::fromStdString(path.string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << path.string();
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

QString readSourceText(const QString& relativePath)
{
    const QStringList roots = {QDir::currentPath(), QCoreApplication::applicationDirPath()};
    for (const QString& root : roots)
    {
        QDir dir(root);
        for (int depth = 0; depth < 8; ++depth)
        {
            QFile file(dir.filePath(relativePath));
            if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                return QString::fromUtf8(file.readAll());
            }
            if (!dir.cdUp())
            {
                break;
            }
        }
    }

    ADD_FAILURE() << "Could not read " << relativePath.toStdString();
    return QString();
}

/// The solution a plate-solved 320 x 240 frame of the Crab's field states in its tangent-plane cards, read as the
/// engine reads a header.
std::optional<an::WcsSolution> makeSpccRegionalRequestWcsSolution()
{
    const std::vector<std::pair<std::string, std::string>> values{{"CTYPE1", "'RA---TAN'"},
                                                                  {"CTYPE2", "'DEC--TAN'"},
                                                                  {"CUNIT1", "'deg'"},
                                                                  {"CUNIT2", "'deg'"},
                                                                  {"CRPIX1", "160.5"},
                                                                  {"CRPIX2", "120.5"},
                                                                  {"CRVAL1", "83.633100000000"},
                                                                  {"CRVAL2", "22.014500000000"},
                                                                  {"CD1_1", "-0.000277777778"},
                                                                  {"CD1_2", "0.0"},
                                                                  {"CD2_1", "0.0"},
                                                                  {"CD2_2", "0.000277777778"}};
    std::vector<an::CodecCard> cards;
    for (const auto& [keyword, value] : values)
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, {});
        EXPECT_TRUE(card.hasValue()) << card.status().message();
        if (!card.hasValue())
        {
            return std::nullopt;
        }
        cards.push_back(std::move(*card));
    }
    an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, 320u, 240u);
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    if (!solution.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*solution);
}

void createCatalogPack(const std::filesystem::path& root, const std::string& packId, const std::string& packName)
{
    std::filesystem::create_directories(root);

    const std::string chunkFile = packId + "_chunk_00.ansp";
    writeFile(root / chunkFile, std::string(64, '\0'));

    const std::string manifest = "{"
                                 "\"catalog_id\":\"" +
                                 packId +
                                 "\","
                                 "\"catalog_name\":\"" +
                                 packName +
                                 "\","
                                 "\"storage\":\"chunks\","
                                 "\"format_version\":3,"
                                 "\"chunk_count\":1,"
                                 "\"chunks\":[{"
                                 "\"file\":\"" +
                                 chunkFile +
                                 "\","
                                 "\"size_bytes\":64"
                                 "}]"
                                 "}";
    writeFile(root / "catalog_meta.json", manifest);
}

void createSpccCatalogPack(const std::filesystem::path& root, const std::string& manifestId,
                           const std::string& accessProfile, const bool installComplete = true,
                           const std::string& rightsReviewStatus = "private-local", const int chunkFirstTile = -1,
                           const int chunkTileCount = -1, const int healpixOrder = -1,
                           const int chunkParentHealpixOrder = -1)
{
    std::filesystem::create_directories(root / "chunks");

    const std::string chunkFile = "chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp";
    const std::string chunkPayload = "spcc-anxp-settings-test";
    writeFile(root / chunkFile, chunkPayload);
    const std::string chunkSha = sha256Hex(root / chunkFile).toStdString();
    writeFile(root / "chunk_manifest.json", "{\"catalog_family_id\":\"spcc_gaia_dr3_xp_sampled\",\"manifest_id\":\"" +
                                                manifestId + "\",\"chunks\":[]}");
    writeFile(root / "coverage_summary.json", "{\"catalog_family_id\":\"spcc_gaia_dr3_xp_sampled\",\"manifest_id\":\"" +
                                                  manifestId + "\",\"total_records\":1}");
    writeFile(root / "source_id_index.json", "{\"catalog_family_id\":\"spcc_gaia_dr3_xp_sampled\",\"manifest_id\":\"" +
                                                 manifestId + "\",\"source_count\":1,\"sources\":{}}");
    writeFile(root / "source_count_transcript.json", "{\"manifest_id\":\"" + manifestId + "\"}");
    if (installComplete)
    {
        writeFile(root / ".install_complete", "ok\n");
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
    writeFile(root / "catalog_meta.json", manifest);
}

[[nodiscard]] QVector<an::ui::CatalogPackDescriptor> testSpccCatalogPackDescriptors(const QString& baseUrl)
{
    const QString base = baseUrl.endsWith(QLatin1Char('/')) ? baseUrl.chopped(1) : baseUrl;
    return {
        an::ui::CatalogPackDescriptor{
            .id = QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510"),
            .displayName = QStringLiteral("SPCC Gaia DR3 XP Sampled M31 Regional Test"),
            .description = QStringLiteral("Synthetic test descriptor for the SPCC catalog download dialog."),
            .manifestUrl = QUrl(base + QStringLiteral("/spcc_gaia_dr3_xp_sampled/installed-regional-cache/"
                                                      "spcc-gaia-dr3-xp-sampled-m31-region-20260510/"
                                                      "catalog_meta.json")),
            .catalogMetaSha256 = QString(64, QLatin1Char('a')),
            .distributionType = QStringLiteral("chunks"),
            .recommended = false,
            .downloadSizeBytes = 359053,
            .installedSizeBytes = 359053,
        },
    };
}

void createSpccResponseRepository(const std::filesystem::path& root, const bool includeAssetFiles = true,
                                  const bool includeExactAlias = true, const bool duplicateExactAlias = false,
                                  const bool includeAlternateProfileWithoutAlias = false,
                                  const bool includeAlternateWhiteReference = false)
{
    std::filesystem::create_directories(root / "components");
    std::filesystem::create_directories(root / "white_references");

    const std::filesystem::path redPath = root / "components" / "sensor_synthetic_r.json";
    const std::filesystem::path greenPath = root / "components" / "sensor_synthetic_g.json";
    const std::filesystem::path bluePath = root / "components" / "sensor_synthetic_b.json";
    const std::filesystem::path whitePath = root / "white_references" / "white_synthetic_d65.json";
    const std::filesystem::path alternateWhitePath = root / "white_references" / "white_synthetic_g2v.json";

    const auto componentPayload = [](const std::string& componentId, const std::string& channel,
                                     const std::string& dataSha, const double value) {
        return "{\"schema_version\":\"spcc-response-asset-v1\",\"asset_kind\":\"component\","
               "\"component_id\":\"" +
               componentId + "\",\"version\":\"1\",\"channel\":\"" + channel +
               "\",\"wavelength_unit\":\"nm\","
               "\"value_semantics\":\"dimensionless-throughput\",\"review_status\":\"approved-for-bundling\","
               "\"data_sha256\":\"" +
               dataSha +
               "\",\"samples\":[{\"wavelength_nm\":400.0,\"value\":" + QString::number(value, 'g', 17).toStdString() +
               "},{\"wavelength_nm\":700.0,\"value\":" + QString::number(value, 'g', 17).toStdString() + "}]}";
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
        writeFile(redPath, redPayload);
        writeFile(greenPath, greenPayload);
        writeFile(bluePath, bluePayload);
        writeFile(whitePath, whitePayload);
        if (includeAlternateWhiteReference)
        {
            writeFile(alternateWhitePath, alternateWhitePayload);
        }
    }

    const QString redSha = includeAssetFiles ? sha256Hex(redPath) : QStringLiteral("missing-red");
    const QString greenSha = includeAssetFiles ? sha256Hex(greenPath) : QStringLiteral("missing-green");
    const QString blueSha = includeAssetFiles ? sha256Hex(bluePath) : QStringLiteral("missing-blue");
    const QString whiteSha = includeAssetFiles ? sha256Hex(whitePath) : QStringLiteral("missing-white");
    const QString alternateWhiteSha = includeAssetFiles && includeAlternateWhiteReference
                                          ? sha256Hex(alternateWhitePath)
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
    writeFile(root / "response_meta.json", manifest);
}

TEST(GaiaCatalogSettingsTest, ScanCatalogRootFindsChildPacks)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    createCatalogPack(root / "gaia_dr3_g12", "gaia_dr3_g12", "Gaia DR3 G<=12");
    createCatalogPack(root / "gaia_dr3_g14", "gaia_dr3_g14", "Gaia DR3 G<=14");

    const QVector<an::ui::GaiaCatalogPackLocation> packs = an::ui::scanGaiaCatalogPacks(tempDir.path());

    ASSERT_EQ(packs.size(), 2);
    EXPECT_EQ(packs[0].packId, QStringLiteral("gaia_dr3_g12"));
    EXPECT_EQ(packs[0].directoryPath,
              QDir::fromNativeSeparators(QString::fromStdString((root / "gaia_dr3_g12").string())));
    EXPECT_EQ(packs[1].packId, QStringLiteral("gaia_dr3_g14"));
    EXPECT_EQ(packs[1].directoryPath,
              QDir::fromNativeSeparators(QString::fromStdString((root / "gaia_dr3_g14").string())));
}

TEST(GaiaCatalogSettingsTest, ScanCatalogRootIgnoresTransientGaiaDownloadDirectories)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    createCatalogPack(root / "gaia_dr3_g14.installing", "gaia_dr3_g14", "Gaia DR3 G<=14");
    createCatalogPack(root / "gaia_dr3_g12.backup", "gaia_dr3_g12", "Gaia DR3 G<=12");

    const QVector<an::ui::GaiaCatalogPackLocation> packs = an::ui::scanGaiaCatalogPacks(tempDir.path());

    EXPECT_TRUE(packs.isEmpty());
}

TEST(GaiaCatalogSettingsTest, ResolveCatalogDirectoryUsesSelectedPackInsideCatalogRoot)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    createCatalogPack(root / "gaia_dr3_g12", "gaia_dr3_g12", "Gaia DR3 G<=12");
    createCatalogPack(root / "gaia_dr3_g14", "gaia_dr3_g14", "Gaia DR3 G<=14");

    an::ui::PlateSolveSettings settings;
    settings.gaiaCatalogDirectory = tempDir.path();
    settings.gaiaCatalogPack = QStringLiteral("gaia_dr3_g12");

    EXPECT_EQ(an::ui::resolveGaiaCatalogDirectory(settings),
              QDir::fromNativeSeparators(QString::fromStdString((root / "gaia_dr3_g12").string())));
}

TEST(GaiaCatalogSettingsTest, ResolveCatalogDirectoryPreservesDirectPackDirectory)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path packDir = std::filesystem::path(tempDir.path().toStdString()) / "gaia_dr3_g16";
    createCatalogPack(packDir, "gaia_dr3_g16", "Gaia DR3 G<=16");

    an::ui::PlateSolveSettings settings;
    settings.gaiaCatalogDirectory = QString::fromStdString(packDir.string());
    settings.gaiaCatalogPack = QStringLiteral("gaia_dr3_g12");

    EXPECT_EQ(an::ui::resolveGaiaCatalogDirectory(settings), QString::fromStdString(packDir.string()));
}

TEST(GaiaCatalogSettingsTest, InspectCatalogPackInstallChecksPackUnderCatalogRoot)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    createCatalogPack(root / "gaia_dr3_g14", "gaia_dr3_g14", "Gaia DR3 G<=14");

    const an::ui::GaiaCatalogDirectoryInspection readyInspection =
        an::ui::inspectGaiaCatalogPackInstall(tempDir.path(), QStringLiteral("gaia_dr3_g14"));
    EXPECT_EQ(readyInspection.status, an::ui::GaiaCatalogDirectoryStatus::Ready);
    EXPECT_EQ(readyInspection.catalogId, QStringLiteral("gaia_dr3_g14"));

    const an::ui::GaiaCatalogDirectoryInspection missingInspection =
        an::ui::inspectGaiaCatalogPackInstall(tempDir.path(), QStringLiteral("gaia_dr3_g16"));
    EXPECT_EQ(missingInspection.status, an::ui::GaiaCatalogDirectoryStatus::Missing);
}

TEST(SpccCatalogSettingsTest, AdvertisesApprovedFullCatalogPackByDefault)
{
    const StandardPathsTestMode testMode;
    QFile::remove(an::ui::spccCatalogMirrorConfigFilePath());

    const QVector<an::ui::CatalogPackDescriptor> packs =
        an::ui::availableSpccCatalogPacksForBaseUrl(QStringLiteral("https://example.test/spcc/"));

    ASSERT_EQ(packs.size(), 1);
    EXPECT_EQ(packs.front().id, QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b"));
    EXPECT_EQ(packs.front().displayName, QStringLiteral("SPCC Gaia DR3 XP Sampled Local Full"));
    EXPECT_TRUE(packs.front().description.contains(QStringLiteral("29,648,017")));
    EXPECT_EQ(packs.front().catalogFamilyId, QStringLiteral("spcc_gaia_dr3_xp_sampled"));
    EXPECT_EQ(packs.front().accessProfile, QStringLiteral("local-full"));
    EXPECT_EQ(packs.front().rightsReviewStatus, QStringLiteral("approved-for-managed-download"));
    EXPECT_TRUE(packs.front().recommended);
    EXPECT_EQ(packs.front().downloadSizeBytes, 45539563436LL);
    EXPECT_EQ(packs.front().installedSizeBytes, 47690985471LL);
    EXPECT_EQ(packs.front().catalogMetaSha256,
              QStringLiteral("1fb5af737d0673c0cb34b4ae7d070094c1c66760d7b9c0739bf2785bbfafc364"));
    EXPECT_EQ(packs.front().manifestUrl,
              QUrl(QStringLiteral("https://example.test/spcc/spcc_gaia_dr3_xp_sampled/local-full/"
                                  "spcc-gaia-dr3-xp-sampled-full-2026b/catalog_meta.json")));
    EXPECT_FALSE(an::ui::availableSpccCatalogPacks().isEmpty());
    EXPECT_TRUE(an::ui::defaultSpccCatalogPack().isEmpty());
}

TEST(SpccCatalogSettingsTest, FallsBackToHostedFullPackWhenLocalMirrorConfigHasNoApprovedPacks)
{
    const StandardPathsTestMode testMode;

    const QString dataDir = an::ui::applicationLocalDataPath();
    ASSERT_FALSE(dataDir.isEmpty());
    ASSERT_TRUE(QDir().mkpath(dataDir));

    QFile mirrorConfig(an::ui::spccCatalogMirrorConfigFilePath());
    ASSERT_TRUE(mirrorConfig.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/spcc/")}},
        {QStringLiteral("catalog_packs"), QJsonArray{}},
    };
    ASSERT_GT(mirrorConfig.write(QJsonDocument(config).toJson(QJsonDocument::Compact)), 0);
    mirrorConfig.close();

    const QVector<an::ui::CatalogPackDescriptor> packs = an::ui::availableSpccCatalogPacks();

    ASSERT_EQ(packs.size(), 1);
    EXPECT_EQ(packs.front().id, QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b"));
    EXPECT_EQ(packs.front().manifestUrl,
              QUrl(QStringLiteral("https://example.test/spcc/spcc_gaia_dr3_xp_sampled/local-full/"
                                  "spcc-gaia-dr3-xp-sampled-full-2026b/catalog_meta.json")));
}

TEST(SpccCatalogSettingsTest, FallsBackToHostedFullPackWhenMirrorDescriptorHasNoUsableManifestUrl)
{
    const StandardPathsTestMode testMode;

    const QString dataDir = an::ui::applicationLocalDataPath();
    ASSERT_FALSE(dataDir.isEmpty());
    ASSERT_TRUE(QDir().mkpath(dataDir));

    QFile mirrorConfig(an::ui::spccCatalogMirrorConfigFilePath());
    ASSERT_TRUE(mirrorConfig.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject descriptor{
        {QStringLiteral("id"), QStringLiteral("spcc-gaia-dr3-xp-sampled-invalid-region-20260511")},
        {QStringLiteral("display_name"), QStringLiteral("Invalid regional cache")},
        {QStringLiteral("description"), QStringLiteral("Descriptor with a relative manifest URL")},
        {QStringLiteral("manifest_url"), QStringLiteral("catalog_meta.json")},
        {QStringLiteral("catalog_meta_sha256"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("distribution_type"), QStringLiteral("chunks")},
        {QStringLiteral("catalog_family_id"), QStringLiteral("spcc_gaia_dr3_xp_sampled")},
        {QStringLiteral("access_profile"), QStringLiteral("installed-regional-cache")},
        {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("recommended"), true},
        {QStringLiteral("download_size_bytes"), 123456},
        {QStringLiteral("installed_size_bytes"), 234567},
        {QStringLiteral("coverage"),
         QJsonObject{
             {QStringLiteral("center_ra_deg"), 83.6331},
             {QStringLiteral("center_dec_deg"), 22.0145},
             {QStringLiteral("radius_deg"), 1.25},
             {QStringLiteral("magnitude_limit"), 17.0},
         }},
    };
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/spcc/")}},
        {QStringLiteral("catalog_packs"), QJsonArray{descriptor}},
    };
    ASSERT_GT(mirrorConfig.write(QJsonDocument(config).toJson(QJsonDocument::Compact)), 0);
    mirrorConfig.close();

    const QVector<an::ui::CatalogPackDescriptor> packs = an::ui::availableSpccCatalogPacks();

    ASSERT_EQ(packs.size(), 1);
    EXPECT_EQ(packs.front().id, QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b"));
    EXPECT_TRUE(an::ui::isUsableCatalogManifestUrl(packs.front().manifestUrl));
    EXPECT_TRUE(an::ui::isDownloadableManagedSpccLocalFullCatalogDescriptor(packs.front()));
}

TEST(SpccCatalogSettingsTest, KeepsHostedFullPackAvailableWhenMirrorConfigOnlyListsRegionalPacks)
{
    const StandardPathsTestMode testMode;

    const QString dataDir = an::ui::applicationLocalDataPath();
    ASSERT_FALSE(dataDir.isEmpty());
    ASSERT_TRUE(QDir().mkpath(dataDir));

    QFile mirrorConfig(an::ui::spccCatalogMirrorConfigFilePath());
    ASSERT_TRUE(mirrorConfig.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject descriptor{
        {QStringLiteral("id"), QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260511")},
        {QStringLiteral("display_name"), QStringLiteral("Orion regional cache")},
        {QStringLiteral("description"), QStringLiteral("Approved regional descriptor")},
        {QStringLiteral("manifest_path"),
         QStringLiteral("spcc_gaia_dr3_xp_sampled/installed-regional-cache/"
                        "spcc-gaia-dr3-xp-sampled-orion-region-20260511/catalog_meta.json")},
        {QStringLiteral("catalog_meta_sha256"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("distribution_type"), QStringLiteral("chunks")},
        {QStringLiteral("catalog_family_id"), QStringLiteral("spcc_gaia_dr3_xp_sampled")},
        {QStringLiteral("access_profile"), QStringLiteral("installed-regional-cache")},
        {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("recommended"), true},
        {QStringLiteral("download_size_bytes"), 123456},
        {QStringLiteral("installed_size_bytes"), 234567},
        {QStringLiteral("coverage"),
         QJsonObject{
             {QStringLiteral("center_ra_deg"), 83.6331},
             {QStringLiteral("center_dec_deg"), 22.0145},
             {QStringLiteral("radius_deg"), 1.25},
             {QStringLiteral("magnitude_limit"), 17.0},
         }},
    };
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/spcc/")}},
        {QStringLiteral("catalog_packs"), QJsonArray{descriptor}},
    };
    ASSERT_GT(mirrorConfig.write(QJsonDocument(config).toJson(QJsonDocument::Compact)), 0);
    mirrorConfig.close();

    const QVector<an::ui::CatalogPackDescriptor> packs = an::ui::availableSpccCatalogPacks();

    ASSERT_EQ(packs.size(), 2);
    EXPECT_TRUE(std::any_of(packs.begin(), packs.end(), [](const an::ui::CatalogPackDescriptor& pack) {
        return pack.id == QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260511") &&
               an::ui::isUsableCatalogManifestUrl(pack.manifestUrl);
    }));
    EXPECT_TRUE(std::any_of(packs.begin(), packs.end(), [](const an::ui::CatalogPackDescriptor& pack) {
        return pack.id == QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b") &&
               an::ui::isDownloadableManagedSpccLocalFullCatalogDescriptor(pack);
    }));
}

TEST(SpccCatalogSettingsTest, MirrorConfigExposesOnlyApprovedDescriptors)
{
    const QString approvedManifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260511");
    const QString privateManifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-private-region-20260511");
    const QString wrongFamilyManifestId = QStringLiteral("not-spcc-gaia-dr3-xp-sampled-region-20260511");
    const QString unsafeManifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-unsafe-region-20260511");
    const QString unsafeDescriptorId = QStringLiteral("../spcc-gaia-dr3-xp-sampled-escaped-region-20260511");

    const auto descriptorObject = [](const QString& manifestId, const QString& catalogFamilyId,
                                     const QString& accessProfile, const QString& rightsReviewStatus,
                                     const QString& manifestPath) {
        return QJsonObject{
            {QStringLiteral("id"), manifestId},
            {QStringLiteral("display_name"), QStringLiteral("Orion regional cache")},
            {QStringLiteral("description"), QStringLiteral("Approved test descriptor")},
            {QStringLiteral("manifest_path"), manifestPath},
            {QStringLiteral("catalog_meta_sha256"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("distribution_type"), QStringLiteral("chunks")},
            {QStringLiteral("catalog_family_id"), catalogFamilyId},
            {QStringLiteral("access_profile"), accessProfile},
            {QStringLiteral("rights_review_status"), rightsReviewStatus},
            {QStringLiteral("recommended"), true},
            {QStringLiteral("download_size_bytes"), 123456},
            {QStringLiteral("installed_size_bytes"), 234567},
            {QStringLiteral("coverage"),
             QJsonObject{
                 {QStringLiteral("center_ra_deg"), 83.6331},
                 {QStringLiteral("center_dec_deg"), 22.0145},
                 {QStringLiteral("radius_deg"), 1.25},
                 {QStringLiteral("magnitude_limit"), 17.0},
             }},
        };
    };

    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/spcc/")}},
        {QStringLiteral("catalog_packs"),
         QJsonArray{
             descriptorObject(approvedManifestId, QStringLiteral("spcc_gaia_dr3_xp_sampled"),
                              QStringLiteral("installed-regional-cache"),
                              QStringLiteral("approved-for-managed-download"),
                              QStringLiteral("spcc_gaia_dr3_xp_sampled/installed-regional-cache/"
                                             "spcc-gaia-dr3-xp-sampled-orion-region-20260511/catalog_meta.json")),
             descriptorObject(privateManifestId, QStringLiteral("spcc_gaia_dr3_xp_sampled"),
                              QStringLiteral("installed-regional-cache"), QStringLiteral("private-local"),
                              QStringLiteral("spcc_gaia_dr3_xp_sampled/installed-regional-cache/private/"
                                             "catalog_meta.json")),
             descriptorObject(wrongFamilyManifestId, QStringLiteral("gaia_dr3"), QStringLiteral("local-full"),
                              QStringLiteral("approved-for-managed-download"),
                              QStringLiteral("gaia_dr3/local-full/not-spcc/catalog_meta.json")),
             descriptorObject(unsafeManifestId, QStringLiteral("spcc_gaia_dr3_xp_sampled"),
                              QStringLiteral("installed-regional-cache"),
                              QStringLiteral("approved-for-managed-download"), QStringLiteral("../catalog_meta.json")),
             descriptorObject(unsafeDescriptorId, QStringLiteral("spcc_gaia_dr3_xp_sampled"),
                              QStringLiteral("installed-regional-cache"),
                              QStringLiteral("approved-for-managed-download"),
                              QStringLiteral("spcc_gaia_dr3_xp_sampled/installed-regional-cache/"
                                             "spcc-gaia-dr3-xp-sampled-escaped-region-20260511/catalog_meta.json")),
         }},
    };

    QString diagnostic;
    const QVector<an::ui::CatalogPackDescriptor> packs =
        an::ui::availableSpccCatalogPacksForMirrorConfig(config, &diagnostic);

    ASSERT_EQ(packs.size(), 1) << diagnostic.toStdString();
    const an::ui::CatalogPackDescriptor& pack = packs.front();
    EXPECT_EQ(pack.id, approvedManifestId);
    EXPECT_EQ(pack.displayName, QStringLiteral("Orion regional cache"));
    EXPECT_EQ(pack.description, QStringLiteral("Approved test descriptor"));
    EXPECT_EQ(pack.manifestUrl,
              QUrl(QStringLiteral("https://example.test/spcc/spcc_gaia_dr3_xp_sampled/installed-regional-cache/"
                                  "spcc-gaia-dr3-xp-sampled-orion-region-20260511/catalog_meta.json")));
    EXPECT_EQ(pack.distributionType, QStringLiteral("chunks"));
    EXPECT_EQ(pack.catalogFamilyId, QStringLiteral("spcc_gaia_dr3_xp_sampled"));
    EXPECT_EQ(pack.accessProfile, QStringLiteral("installed-regional-cache"));
    EXPECT_EQ(pack.rightsReviewStatus, QStringLiteral("approved-for-managed-download"));
    EXPECT_EQ(pack.catalogMetaSha256, QString(64, QLatin1Char('a')));
    EXPECT_EQ(pack.releaseStatus, QStringLiteral("current"));
    EXPECT_TRUE(pack.recommended);
    EXPECT_EQ(pack.downloadSizeBytes, 123456);
    EXPECT_EQ(pack.installedSizeBytes, 234567);
    EXPECT_TRUE(pack.hasRegionCoverage);
    EXPECT_NEAR(pack.centerRaDeg, 83.6331, 0.000001);
    EXPECT_NEAR(pack.centerDecDeg, 22.0145, 0.000001);
    EXPECT_NEAR(pack.coverageRadiusDeg, 1.25, 0.000001);
    EXPECT_NEAR(pack.magnitudeLimit, 17.0, 0.000001);
}

TEST(SpccCatalogSettingsTest, MirrorConfigExposesOnlyCurrentCatalogDescriptors)
{
    const QString supersededManifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260511");
    const QString currentManifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260512");

    const auto descriptorObject = [](const QString& manifestId, const QString& releaseStatus, bool recommended) {
        return QJsonObject{
            {QStringLiteral("id"), manifestId},
            {QStringLiteral("display_name"), QStringLiteral("Orion regional cache")},
            {QStringLiteral("description"), QStringLiteral("Approved test descriptor")},
            {QStringLiteral("manifest_path"),
             QStringLiteral("spcc_gaia_dr3_xp_sampled/installed-regional-cache/%1/catalog_meta.json").arg(manifestId)},
            {QStringLiteral("catalog_meta_sha256"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("distribution_type"), QStringLiteral("chunks")},
            {QStringLiteral("catalog_family_id"), QStringLiteral("spcc_gaia_dr3_xp_sampled")},
            {QStringLiteral("access_profile"), QStringLiteral("installed-regional-cache")},
            {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
            {QStringLiteral("release_status"), releaseStatus},
            {QStringLiteral("recommended"), recommended},
            {QStringLiteral("download_size_bytes"), 123456},
            {QStringLiteral("installed_size_bytes"), 234567},
            {QStringLiteral("coverage"),
             QJsonObject{
                 {QStringLiteral("center_ra_deg"), 83.6331},
                 {QStringLiteral("center_dec_deg"), 22.0145},
                 {QStringLiteral("radius_deg"), 1.25},
                 {QStringLiteral("magnitude_limit"), 17.0},
             }},
        };
    };

    QJsonObject supersededDescriptor = descriptorObject(supersededManifestId, QStringLiteral("superseded"), false);
    supersededDescriptor.insert(QStringLiteral("superseded_by"), currentManifestId);
    QJsonObject currentDescriptor = descriptorObject(currentManifestId, QStringLiteral("current"), true);
    currentDescriptor.insert(QStringLiteral("supersedes"), QJsonArray{supersededManifestId});

    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("publication_policy"),
         QJsonObject{
             {QStringLiteral("channel"), QStringLiteral("preview")},
             {QStringLiteral("published_at_utc"), QStringLiteral("2026-05-12T00:00:00Z")},
             {QStringLiteral("update_policy"), QStringLiteral("immutable-versioned")},
         }},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/spcc/")}},
        {QStringLiteral("catalog_packs"), QJsonArray{supersededDescriptor, currentDescriptor}},
    };

    QString diagnostic;
    const QVector<an::ui::CatalogPackDescriptor> packs =
        an::ui::availableSpccCatalogPacksForMirrorConfig(config, &diagnostic);

    ASSERT_EQ(packs.size(), 1) << diagnostic.toStdString();
    const an::ui::CatalogPackDescriptor& pack = packs.front();
    EXPECT_EQ(pack.id, currentManifestId);
    EXPECT_EQ(pack.releaseStatus, QStringLiteral("current"));
    EXPECT_TRUE(pack.recommended);
    EXPECT_TRUE(diagnostic.contains(QStringLiteral("ignored"), Qt::CaseInsensitive));
}

TEST(SpccCatalogSettingsTest, InspectSpccCatalogDirectoryRequiresSidecarsChunksAndInstallMarker)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache");

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::Ready);
    EXPECT_EQ(inspection.catalogFamilyId, QStringLiteral("spcc_gaia_dr3_xp_sampled"));
    EXPECT_EQ(inspection.manifestId, QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510"));
    EXPECT_EQ(inspection.accessProfile, QStringLiteral("installed-regional-cache"));
    EXPECT_EQ(inspection.rightsReviewStatus, QStringLiteral("private-local"));
    EXPECT_TRUE(inspection.hasInstallMarker);
    EXPECT_EQ(inspection.sidecarFilesPresent, 4);
    EXPECT_EQ(inspection.chunkFilesPresent, 1);
}

TEST(SpccCatalogSettingsTest, InspectSpccCatalogDirectoryRejectsUnsafeManifestPaths)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache");

    QFile metaFile(QString::fromStdString((root / "catalog_meta.json").string()));
    ASSERT_TRUE(metaFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QString metaText = QString::fromUtf8(metaFile.readAll());
    metaFile.close();

    const QString validMetaText = metaText;
    metaText.replace(QStringLiteral("\"chunk_manifest\":\"chunk_manifest.json\""),
                     QStringLiteral("\"chunk_manifest\":\"../chunk_manifest.json\""));
    writeFile(root / "catalog_meta.json", metaText.toStdString());

    an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));
    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::Invalid);
    EXPECT_TRUE(inspection.diagnostic.contains(QStringLiteral("unsafe"), Qt::CaseInsensitive));

    metaText = validMetaText;
    metaText.replace(QStringLiteral("\"file\":\"chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp\""),
                     QStringLiteral("\"file\":\"../chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp\""));
    writeFile(root / "catalog_meta.json", metaText.toStdString());

    inspection = an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));
    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::Invalid);
    EXPECT_TRUE(inspection.diagnostic.contains(QStringLiteral("unsafe"), Qt::CaseInsensitive));
}

TEST(SpccCatalogSettingsTest, ManualFullRepoWithoutInstallMarkerIsReady)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache", false);

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::Ready);
    EXPECT_FALSE(inspection.hasInstallMarker);
}

TEST(SpccCatalogSettingsTest, MissingChunkIsPartialCache)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache", false);
    std::filesystem::remove(root / "chunks" / "spcc_gaia_dr3_xp_sampled_chunk_0000.anxp");

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::PartialCache);
    EXPECT_EQ(inspection.chunkFilesPresent, 0);
    EXPECT_EQ(inspection.chunkFilesMissing, 1);
}

TEST(SpccCatalogSettingsTest, ChunkChecksumMismatchIsPartialCache)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache", false);
    writeFile(root / "chunks" / "spcc_gaia_dr3_xp_sampled_chunk_0000.anxp", "same-size-corrupt-data!");

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::PartialCache);
    EXPECT_FALSE(inspection.checksumsValid);
    EXPECT_EQ(inspection.chunkFilesPresent, 0);
    EXPECT_EQ(inspection.chunkFilesMissing, 1);
    EXPECT_TRUE(inspection.diagnostic.contains(QStringLiteral("invalid"), Qt::CaseInsensitive));
}

TEST(SpccCatalogSettingsTest, FastInspectionSkipsChunkChecksumVerification)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache", false);
    writeFile(root / "chunks" / "spcc_gaia_dr3_xp_sampled_chunk_0000.anxp", "same-size-corrupt-data!");

    const an::ui::SpccCatalogDirectoryInspection fastInspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()), false);
    const an::ui::SpccCatalogDirectoryInspection verifiedInspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    EXPECT_EQ(fastInspection.status, an::ui::SpccCatalogDirectoryStatus::Ready);
    EXPECT_TRUE(fastInspection.checksumsValid);
    EXPECT_EQ(fastInspection.chunkFilesPresent, 1);
    EXPECT_EQ(verifiedInspection.status, an::ui::SpccCatalogDirectoryStatus::PartialCache);
    EXPECT_FALSE(verifiedInspection.checksumsValid);
}

TEST(SpccCatalogSettingsTest, RequiredRegionReportsMissingCachedChunks)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-region-20260510";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-region-20260510", "installed-regional-cache", false,
                          "approved-for-managed-download", 0, 64, 6, 3);
    std::filesystem::remove(root / "chunks" / "spcc_gaia_dr3_xp_sampled_chunk_0000.anxp");

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    // The engine selects the region's chunks in an operation of its own.
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(1u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    an::Result<an::consumer::OperationScope> scope = opened->tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const an::ui::SpccCatalogRequiredChunkAvailability availability =
        an::ui::spccCatalogRequiredChunkAvailabilityForRegion(scope->execution(), inspection, 45.0, 1.0, 0.05);

    ASSERT_TRUE(availability.valid) << availability.diagnostic.toStdString();
    EXPECT_EQ(availability.requiredChunkIndices, QVector<int>{0});
    EXPECT_TRUE(availability.cachedChunkIndices.isEmpty());
    EXPECT_EQ(availability.missingChunkIndices, QVector<int>{0});
    EXPECT_GT(availability.missingBytes, 0);
}

TEST(SpccCatalogSettingsTest, LocalFullWithPartialTileCoverageIsIncomplete)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root =
        std::filesystem::path(tempDir.path().toStdString()) / "spcc-gaia-dr3-xp-sampled-full-2026a";
    createSpccCatalogPack(root, "spcc-gaia-dr3-xp-sampled-full-2026a", "local-full", true,
                          "approved-for-managed-download", 0, 12288, 6, 3);

    const an::ui::SpccCatalogDirectoryInspection inspection =
        an::ui::inspectSpccCatalogDirectoryDetailed(QString::fromStdString(root.string()));

    EXPECT_EQ(inspection.status, an::ui::SpccCatalogDirectoryStatus::IncompleteInstall);
    EXPECT_EQ(inspection.declaredTileCount, 12288);
    EXPECT_EQ(inspection.expectedTileCount, 49152);
    EXPECT_EQ(inspection.expectedParentChunkCount, 768);
    EXPECT_TRUE(inspection.diagnostic.contains(QStringLiteral("12288/49152")));
}

TEST(SpccCatalogSettingsTest, ScanSpccCatalogPacksFindsNestedDatasetLayout)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    const std::filesystem::path packDir =
        root / "spcc_gaia_dr3_xp_sampled" / "installed-regional-cache" / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(packDir, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache");

    const QVector<an::ui::SpccCatalogPackLocation> packs = an::ui::scanSpccCatalogPacks(tempDir.path());

    ASSERT_EQ(packs.size(), 1);
    EXPECT_EQ(packs.front().manifestId, QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510"));
    EXPECT_EQ(packs.front().inspection.status, an::ui::SpccCatalogDirectoryStatus::Ready);
    EXPECT_EQ(packs.front().directoryPath, QDir::fromNativeSeparators(QString::fromStdString(packDir.string())));
}

TEST(SpccCatalogSettingsTest, RegionalCacheRequestUsesWcsFootprintAndRoundTripsThroughSettings)
{
    MemoryGlobalSettings settings;
    QString diagnostic;

    const std::optional<an::WcsSolution> solution = makeSpccRegionalRequestWcsSolution();
    ASSERT_TRUE(solution.has_value());
    const std::optional<an::ui::SpccRegionalCatalogRequest> request = an::ui::makeSpccRegionalCatalogRequestFromWcs(
        *solution, 4.0, 16.5, QStringLiteral("Orion test frame"), &diagnostic);

    ASSERT_TRUE(request.has_value()) << diagnostic.toStdString();
    EXPECT_EQ(request->schemaVersion, QStringLiteral("spcc-regional-cache-request-v1"));
    EXPECT_EQ(request->catalogFamilyId, QStringLiteral("spcc_gaia_dr3_xp_sampled"));
    EXPECT_EQ(request->accessProfile, QStringLiteral("installed-regional-cache"));
    EXPECT_EQ(request->targetName, QStringLiteral("Orion test frame"));
    EXPECT_NEAR(request->centerRaDeg, 83.6331, 0.001);
    EXPECT_NEAR(request->centerDecDeg, 22.0145, 0.001);
    EXPECT_GT(request->radiusDeg, 0.0);
    EXPECT_EQ(request->magnitudeLimit, 16.5);
    EXPECT_EQ(request->imageWidth, 320);
    EXPECT_EQ(request->imageHeight, 240);

    EXPECT_TRUE(an::ui::saveSpccRegionalCatalogRequest(&settings, *request));
    const QString serialized = settings.values.value(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST);
    EXPECT_TRUE(serialized.contains(QStringLiteral("spcc-regional-cache-request-v1")));

    const std::optional<an::ui::SpccRegionalCatalogRequest> loaded = an::ui::loadSpccRegionalCatalogRequest(&settings);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->schemaVersion, request->schemaVersion);
    EXPECT_EQ(loaded->targetName, request->targetName);
    EXPECT_NEAR(loaded->centerRaDeg, request->centerRaDeg, 0.000001);
    EXPECT_NEAR(loaded->centerDecDeg, request->centerDecDeg, 0.000001);
    EXPECT_NEAR(loaded->radiusDeg, request->radiusDeg, 0.000001);
}

TEST(SpccCatalogSettingsTest, RegionalCacheRequestMatchesOnlyCoveringApprovedDescriptors)
{
    QString diagnostic;
    const std::optional<an::WcsSolution> solution = makeSpccRegionalRequestWcsSolution();
    ASSERT_TRUE(solution.has_value());
    const std::optional<an::ui::SpccRegionalCatalogRequest> request = an::ui::makeSpccRegionalCatalogRequestFromWcs(
        *solution, 4.0, 16.5, QStringLiteral("Orion test frame"), &diagnostic);
    ASSERT_TRUE(request.has_value()) << diagnostic.toStdString();

    an::ui::CatalogPackDescriptor coveringPack{
        .id = QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260511"),
        .displayName = QStringLiteral("Orion regional cache"),
        .manifestUrl = QUrl(QStringLiteral("https://example.test/orion/catalog_meta.json")),
        .distributionType = QStringLiteral("chunks"),
        .catalogFamilyId = QStringLiteral("spcc_gaia_dr3_xp_sampled"),
        .accessProfile = QStringLiteral("installed-regional-cache"),
        .rightsReviewStatus = QStringLiteral("approved-for-managed-download"),
        .hasRegionCoverage = true,
        .centerRaDeg = request->centerRaDeg,
        .centerDecDeg = request->centerDecDeg,
        .coverageRadiusDeg = request->radiusDeg + 0.25,
        .magnitudeLimit = 17.0,
    };
    an::ui::CatalogPackDescriptor wrongSkyPack = coveringPack;
    wrongSkyPack.id = QStringLiteral("spcc-gaia-dr3-xp-sampled-far-region-20260511");
    wrongSkyPack.centerRaDeg = 110.0;
    wrongSkyPack.centerDecDeg = -10.0;

    an::ui::CatalogPackDescriptor shallowPack = coveringPack;
    shallowPack.id = QStringLiteral("spcc-gaia-dr3-xp-sampled-shallow-region-20260511");
    shallowPack.magnitudeLimit = 12.0;

    an::ui::CatalogPackDescriptor privatePack = coveringPack;
    privatePack.id = QStringLiteral("spcc-gaia-dr3-xp-sampled-private-region-20260511");
    privatePack.rightsReviewStatus = QStringLiteral("private-local");

    QString rejectionReason;
    EXPECT_TRUE(an::ui::spccCatalogPackDescriptorCoversRegionalRequest(coveringPack, *request, &rejectionReason))
        << rejectionReason.toStdString();
    EXPECT_FALSE(an::ui::spccCatalogPackDescriptorCoversRegionalRequest(wrongSkyPack, *request, &rejectionReason));
    EXPECT_TRUE(rejectionReason.contains(QStringLiteral("coverage"), Qt::CaseInsensitive));
    EXPECT_FALSE(an::ui::spccCatalogPackDescriptorCoversRegionalRequest(shallowPack, *request, &rejectionReason));
    EXPECT_TRUE(rejectionReason.contains(QStringLiteral("magnitude"), Qt::CaseInsensitive));
    EXPECT_FALSE(an::ui::spccCatalogPackDescriptorCoversRegionalRequest(privatePack, *request, &rejectionReason));
    EXPECT_TRUE(rejectionReason.contains(QStringLiteral("rights"), Qt::CaseInsensitive));

    const QVector<an::ui::CatalogPackDescriptor> matches = an::ui::spccRegionalCatalogPacksForRequest(
        QVector<an::ui::CatalogPackDescriptor>{wrongSkyPack, shallowPack, privatePack, coveringPack}, *request);
    ASSERT_EQ(matches.size(), 1);
    EXPECT_EQ(matches.front().id, coveringPack.id);
}

TEST(SpccResponseSettingsTest, InspectReadyResponseRepositoryValidatesAssetsAndProfiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    createSpccResponseRepository(std::filesystem::path(tempDir.path().toStdString()));

    const an::ui::SpccResponseRepositoryInspection inspection =
        an::ui::inspectSpccResponseRepositoryDetailed(tempDir.path());

    EXPECT_EQ(inspection.status, an::ui::SpccResponseRepositoryStatus::Ready);
    EXPECT_EQ(inspection.repositoryId, QStringLiteral("spcc-response-fixture"));
    EXPECT_EQ(inspection.version, QStringLiteral("2026a"));
    EXPECT_EQ(inspection.rightsReviewStatus, QStringLiteral("approved-for-bundling"));
    EXPECT_EQ(inspection.componentCount, 3);
    EXPECT_EQ(inspection.whiteReferenceCount, 1);
    EXPECT_EQ(inspection.throughputProfileCount, 1);
}

TEST(SpccResponseSettingsTest, MissingResponseAssetIsIncomplete)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    createSpccResponseRepository(std::filesystem::path(tempDir.path().toStdString()), false);

    const an::ui::SpccResponseRepositoryInspection inspection =
        an::ui::inspectSpccResponseRepositoryDetailed(tempDir.path());

    EXPECT_EQ(inspection.status, an::ui::SpccResponseRepositoryStatus::IncompleteInstall);
    EXPECT_EQ(inspection.repositoryId, QStringLiteral("spcc-response-fixture"));
}

TEST(SpccResponseSettingsTest, RepositoryConfigExposesOnlyApprovedDescriptors)
{
    const QString approvedDescriptorId = QStringLiteral("spcc-response-baseline-2026a");
    const QString privateDescriptorId = QStringLiteral("spcc-response-private-2026a");
    const QString unsafeDescriptorId = QStringLiteral("spcc-response-unsafe-2026a");
    const QString unsupportedScopeDescriptorId = QStringLiteral("spcc-response-unsupported-scope-2026a");
    const QString invalidChecksumDescriptorId = QStringLiteral("spcc-response-invalid-checksum-2026a");

    const auto descriptorObject = [](const QString& descriptorId, const QString& rightsReviewStatus,
                                     const QString& deploymentScope, const QString& manifestPath,
                                     const QString& responseMetaSha256) {
        return QJsonObject{
            {QStringLiteral("id"), descriptorId},
            {QStringLiteral("repository_id"), QStringLiteral("spcc-response-baseline")},
            {QStringLiteral("version"), QStringLiteral("2026a")},
            {QStringLiteral("display_name"), QStringLiteral("SPCC response baseline")},
            {QStringLiteral("description"), QStringLiteral("Approved response repository test descriptor")},
            {QStringLiteral("manifest_path"), manifestPath},
            {QStringLiteral("rights_review_status"), rightsReviewStatus},
            {QStringLiteral("deployment_scope"), deploymentScope},
            {QStringLiteral("response_meta_sha256"), responseMetaSha256},
            {QStringLiteral("recommended"), true},
            {QStringLiteral("download_size_bytes"), 1234},
            {QStringLiteral("installed_size_bytes"), 5678},
        };
    };

    const QString validManifestSha(64, QLatin1Char('a'));
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-repository-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/responses/")}},
        {QStringLiteral("response_repositories"),
         QJsonArray{
             descriptorObject(
                 approvedDescriptorId, QStringLiteral("approved-for-managed-download"), QStringLiteral("managed-cache"),
                 QStringLiteral("responses/spcc-response-baseline/2026a/response_meta.json"), validManifestSha),
             descriptorObject(privateDescriptorId, QStringLiteral("private-local"), QStringLiteral("managed-cache"),
                              QStringLiteral("responses/private/response_meta.json"), validManifestSha),
             descriptorObject(unsafeDescriptorId, QStringLiteral("approved-for-managed-download"),
                              QStringLiteral("managed-cache"), QStringLiteral("../response_meta.json"),
                              validManifestSha),
             descriptorObject(unsupportedScopeDescriptorId, QStringLiteral("approved-for-managed-download"),
                              QStringLiteral("source-tree"),
                              QStringLiteral("responses/unsupported-scope/response_meta.json"), validManifestSha),
             descriptorObject(invalidChecksumDescriptorId, QStringLiteral("approved-for-managed-download"),
                              QStringLiteral("managed-cache"),
                              QStringLiteral("responses/invalid-checksum/response_meta.json"),
                              QStringLiteral("not-a-sha")),
         }},
    };

    QString diagnostic;
    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories =
        an::ui::availableSpccResponseRepositoriesForConfig(config, &diagnostic);

    ASSERT_EQ(repositories.size(), 1) << diagnostic.toStdString();
    const an::ui::SpccResponseRepositoryDescriptor& repository = repositories.front();
    EXPECT_EQ(repository.id, approvedDescriptorId);
    EXPECT_EQ(repository.repositoryId, QStringLiteral("spcc-response-baseline"));
    EXPECT_EQ(repository.version, QStringLiteral("2026a"));
    EXPECT_EQ(repository.displayName, QStringLiteral("SPCC response baseline"));
    EXPECT_EQ(repository.description, QStringLiteral("Approved response repository test descriptor"));
    EXPECT_EQ(repository.manifestUrl,
              QUrl(QStringLiteral("https://example.test/responses/responses/spcc-response-baseline/2026a/"
                                  "response_meta.json")));
    EXPECT_EQ(repository.rightsReviewStatus, QStringLiteral("approved-for-managed-download"));
    EXPECT_EQ(repository.deploymentScope, QStringLiteral("managed-cache"));
    EXPECT_EQ(repository.responseMetaSha256, validManifestSha);
    EXPECT_TRUE(repository.recommended);
    EXPECT_EQ(repository.downloadSizeBytes, 1234);
    EXPECT_EQ(repository.installedSizeBytes, 5678);
    EXPECT_TRUE(diagnostic.contains(QStringLiteral("ignored"), Qt::CaseInsensitive));
}

TEST(SpccResponseSettingsTest, AdvertisesApprovedResponseRepositoryByDefault)
{
    const StandardPathsTestMode testMode;
    QFile::remove(an::ui::spccResponseRepositoryConfigFilePath());

    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories = an::ui::availableSpccResponseRepositories();

    ASSERT_EQ(repositories.size(), 1);
    const an::ui::SpccResponseRepositoryDescriptor& repository = repositories.front();
    EXPECT_EQ(repository.id, QStringLiteral("afternight-spcc-response-baseline-2026a"));
    EXPECT_EQ(repository.repositoryId, QStringLiteral("afternight-spcc-response-baseline"));
    EXPECT_EQ(repository.version, QStringLiteral("2026a"));
    EXPECT_EQ(repository.displayName, QStringLiteral("AfterNight SPCC Response Baseline 2026a"));
    EXPECT_EQ(repository.manifestUrl,
              QUrl(QStringLiteral("https://huggingface.co/datasets/emruiz81/afternight-spcc-responses/resolve/main/"
                                  "responses/afternight-spcc-response-baseline/2026a/response_meta.json")));
    EXPECT_EQ(repository.rightsReviewStatus, QStringLiteral("approved-for-managed-download"));
    EXPECT_EQ(repository.deploymentScope, QStringLiteral("managed-cache"));
    EXPECT_EQ(repository.responseMetaSha256,
              QStringLiteral("24bfdcb7eefdf6c89ccd568db11ebbfc625e6138fc4b5ebcba7d78558b006bc9"));
    EXPECT_TRUE(repository.recommended);
    EXPECT_EQ(repository.downloadSizeBytes, 27573635LL);
    EXPECT_EQ(repository.installedSizeBytes, 27573635LL);
}

TEST(SpccResponseSettingsTest, FallsBackToHostedResponseRepositoryWhenLocalConfigHasNoApprovedRepositories)
{
    const StandardPathsTestMode testMode;

    const QString configPath = an::ui::spccResponseRepositoryConfigFilePath();
    ASSERT_FALSE(configPath.trimmed().isEmpty());
    ASSERT_TRUE(QDir().mkpath(QFileInfo(configPath).absolutePath()));

    QFile configFile(configPath);
    ASSERT_TRUE(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-repository-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/responses")}},
        {QStringLiteral("response_repositories"), QJsonArray{}},
    };
    ASSERT_GT(configFile.write(QJsonDocument(config).toJson(QJsonDocument::Compact)), 0);
    configFile.close();

    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories = an::ui::availableSpccResponseRepositories();

    ASSERT_EQ(repositories.size(), 1);
    const an::ui::SpccResponseRepositoryDescriptor& repository = repositories.front();
    EXPECT_EQ(repository.id, QStringLiteral("afternight-spcc-response-baseline-2026a"));
    EXPECT_EQ(
        repository.manifestUrl,
        QUrl(QStringLiteral(
            "https://example.test/responses/responses/afternight-spcc-response-baseline/2026a/response_meta.json")));
}

TEST(SpccResponseSettingsTest, AppConfigLoaderUsesHostedStagingConfigAndIgnoresSupersededDescriptors)
{
    const StandardPathsTestMode testMode;
    QTemporaryDir hostedDir;
    ASSERT_TRUE(hostedDir.isValid());

    const QString configPath = an::ui::spccResponseRepositoryConfigFilePath();
    ASSERT_FALSE(configPath.trimmed().isEmpty());
    ASSERT_TRUE(QDir().mkpath(QFileInfo(configPath).absolutePath()));
    QFile::remove(configPath);

    const QString mirrorBaseUrl = QUrl::fromLocalFile(hostedDir.path()).toString();
    const QJsonObject supersededDescriptor{
        {QStringLiteral("id"), QStringLiteral("spcc-response-fixture-2026a")},
        {QStringLiteral("repository_id"), QStringLiteral("spcc-response-fixture")},
        {QStringLiteral("version"), QStringLiteral("2026a")},
        {QStringLiteral("display_name"), QStringLiteral("SPCC response fixture 2026a")},
        {QStringLiteral("description"), QStringLiteral("Superseded staged response repository descriptor.")},
        {QStringLiteral("manifest_path"), QStringLiteral("responses/spcc-response-fixture/2026a/response_meta.json")},
        {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("deployment_scope"), QStringLiteral("managed-cache")},
        {QStringLiteral("response_meta_sha256"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("download_size_bytes"), 1234},
        {QStringLiteral("installed_size_bytes"), 5678},
        {QStringLiteral("release_status"), QStringLiteral("superseded")},
        {QStringLiteral("recommended"), false},
        {QStringLiteral("superseded_by"), QStringLiteral("spcc-response-fixture-2026b")},
    };
    const QJsonObject currentDescriptor{
        {QStringLiteral("id"), QStringLiteral("spcc-response-fixture-2026b")},
        {QStringLiteral("repository_id"), QStringLiteral("spcc-response-fixture")},
        {QStringLiteral("version"), QStringLiteral("2026b")},
        {QStringLiteral("display_name"), QStringLiteral("SPCC response fixture 2026b")},
        {QStringLiteral("description"), QStringLiteral("Current staged response repository descriptor.")},
        {QStringLiteral("manifest_path"), QStringLiteral("responses/spcc-response-fixture/2026b/response_meta.json")},
        {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("deployment_scope"), QStringLiteral("managed-cache")},
        {QStringLiteral("response_meta_sha256"), QString(64, QLatin1Char('b'))},
        {QStringLiteral("download_size_bytes"), 2234},
        {QStringLiteral("installed_size_bytes"), 6678},
        {QStringLiteral("release_status"), QStringLiteral("current")},
        {QStringLiteral("recommended"), true},
        {QStringLiteral("supersedes"), QJsonArray{QStringLiteral("spcc-response-fixture-2026a")}},
    };
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-repository-config-v1")},
        {QStringLiteral("publication_policy"),
         QJsonObject{
             {QStringLiteral("channel"), QStringLiteral("preview")},
             {QStringLiteral("published_at_utc"), QStringLiteral("2026-05-12T00:00:00Z")},
             {QStringLiteral("update_policy"), QStringLiteral("immutable-versioned")},
         }},
        {QStringLiteral("mirrors"), QJsonArray{mirrorBaseUrl}},
        {QStringLiteral("response_repositories"), QJsonArray{supersededDescriptor, currentDescriptor}},
    };

    QFile configFile(configPath);
    ASSERT_TRUE(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    configFile.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
    configFile.close();

    const QVector<an::ui::SpccResponseRepositoryDescriptor> repositories = an::ui::availableSpccResponseRepositories();

    ASSERT_EQ(repositories.size(), 1);
    const an::ui::SpccResponseRepositoryDescriptor& repository = repositories.front();
    EXPECT_EQ(repository.id, QStringLiteral("spcc-response-fixture-2026b"));
    EXPECT_EQ(repository.version, QStringLiteral("2026b"));
    EXPECT_EQ(repository.releaseStatus, QStringLiteral("current"));
    EXPECT_TRUE(repository.recommended);
    EXPECT_EQ(repository.manifestUrl,
              QUrl(mirrorBaseUrl + QStringLiteral("/responses/spcc-response-fixture/2026b/response_meta.json")));

    QFile::remove(configPath);
}

TEST(CatalogDownloadDialogTest, DefaultGaiaInspectorMarksInstalledPackAsDownloaded)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    createCatalogPack(root / "gaia_dr3_g14", "gaia_dr3_g14", "Gaia DR3 G<=14");

    an::ui::CatalogDownloadDialog dialog(an::ui::availableGaiaCatalogPacks());
    auto* targetEdit = dialog.findChild<QLineEdit*>(QStringLiteral("catalogTargetDirectoryEdit"));
    auto* packCombo = dialog.findChild<QComboBox*>(QStringLiteral("catalogPackCombo"));
    auto* downloadButton = dialog.findChild<QPushButton*>(QStringLiteral("catalogDownloadButton"));
    ASSERT_NE(targetEdit, nullptr);
    ASSERT_NE(packCombo, nullptr);
    ASSERT_NE(downloadButton, nullptr);

    targetEdit->setText(tempDir.path());
    QApplication::processEvents();

    const int packIndex = packCombo->findData(QStringLiteral("gaia_dr3_g14"));
    ASSERT_GE(packIndex, 0);
    EXPECT_TRUE(packCombo->itemText(packIndex).contains(QStringLiteral("Downloaded")));

    packCombo->setCurrentIndex(packIndex);
    QApplication::processEvents();
    EXPECT_EQ(downloadButton->text(), QStringLiteral("Verify && Repair"));
}

TEST(CatalogDownloadDialogTest, SpccInspectorMarksInstalledPackAsDownloaded)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    createSpccCatalogPack(root / "spcc-gaia-dr3-xp-sampled-m31-region-20260510",
                          "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache");

    an::ui::CatalogDownloadDialog dialog(
        testSpccCatalogPackDescriptors(QStringLiteral("https://example.test/spcc")),
        an::ui::CatalogDownloadInstallInspector(an::ui::spccCatalogDownloadInstallInspection));
    auto* targetEdit = dialog.findChild<QLineEdit*>(QStringLiteral("catalogTargetDirectoryEdit"));
    auto* packCombo = dialog.findChild<QComboBox*>(QStringLiteral("catalogPackCombo"));
    auto* downloadButton = dialog.findChild<QPushButton*>(QStringLiteral("catalogDownloadButton"));
    ASSERT_NE(targetEdit, nullptr);
    ASSERT_NE(packCombo, nullptr);
    ASSERT_NE(downloadButton, nullptr);

    targetEdit->setText(tempDir.path());
    QApplication::processEvents();

    const int packIndex = packCombo->findData(QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510"));
    ASSERT_GE(packIndex, 0);
    EXPECT_TRUE(packCombo->itemText(packIndex).contains(QStringLiteral("Downloaded")));

    packCombo->setCurrentIndex(packIndex);
    QApplication::processEvents();
    EXPECT_EQ(downloadButton->text(), QStringLiteral("Verify && Repair"));
}

TEST(CatalogDownloadDialogTest, SpccInspectorSkipsChunkChecksumVerificationWhileOpening)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const std::filesystem::path root = tempDir.path().toStdString();
    const std::filesystem::path packDir = root / "spcc-gaia-dr3-xp-sampled-m31-region-20260510";
    createSpccCatalogPack(packDir, "spcc-gaia-dr3-xp-sampled-m31-region-20260510", "installed-regional-cache");
    writeFile(packDir / "chunks" / "spcc_gaia_dr3_xp_sampled_chunk_0000.anxp", "same-size-corrupt-data!");

    const an::ui::CatalogDownloadInstallInspection inspection = an::ui::spccCatalogDownloadInstallInspection(
        tempDir.path(), testSpccCatalogPackDescriptors(QStringLiteral("https://example.test/spcc")).front());

    EXPECT_EQ(inspection.status, an::ui::CatalogDownloadInstallStatus::Ready);
    EXPECT_EQ(inspection.cachedChunks, 1);
    EXPECT_EQ(inspection.totalChunks, 1);
}

TEST(CatalogDownloadDialogTest, SynchronousDownloadFailureDoesNotLeaveDialogBusy)
{
    const QVector<an::ui::CatalogPackDescriptor> packs{
        an::ui::CatalogPackDescriptor{
            .id = QStringLiteral("unsupported-test-pack"),
            .displayName = QStringLiteral("Unsupported Test Pack"),
            .manifestUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/unsupported-test/catalog_meta.json")),
            .distributionType = QStringLiteral("unsupported"),
        },
    };

    an::ui::CatalogDownloadDialog dialog(packs);
    auto* downloadButton = dialog.findChild<QPushButton*>(QStringLiteral("catalogDownloadButton"));
    auto* cancelButton = dialog.findChild<QPushButton*>(QStringLiteral("catalogCancelDownloadButton"));
    auto* closeButton = dialog.findChild<QPushButton*>(QStringLiteral("catalogCloseButton"));
    ASSERT_NE(downloadButton, nullptr);
    ASSERT_NE(cancelButton, nullptr);
    ASSERT_NE(closeButton, nullptr);

    dialog.show();
    QApplication::processEvents();
    ASSERT_TRUE(downloadButton->isVisible());
    ASSERT_TRUE(downloadButton->isEnabled());

    downloadButton->click();
    QApplication::processEvents();

    EXPECT_TRUE(downloadButton->isVisible());
    EXPECT_TRUE(downloadButton->isEnabled());
    EXPECT_FALSE(cancelButton->isVisible());
    EXPECT_TRUE(closeButton->isEnabled());
}

TEST(CatalogsDataSettingsPageTest, ShowsApprovedDefaultSpccCatalogPack)
{
    const StandardPathsTestMode testMode;
    QFile::remove(an::ui::spccCatalogMirrorConfigFilePath());
    QFile::remove(an::ui::spccResponseRepositoryConfigFilePath());

    CatalogsDataSettingsPage page(nullptr);
    page.loadSettings();

    auto* gaiaCatalogPicker = page.findChild<PathPickerWidget*>(QStringLiteral("gaiaCatalogDirectoryPicker"));
    auto* gaiaPackCombo = page.findChild<QComboBox*>(QStringLiteral("gaiaCatalogPackCombo"));
    auto* gaiaDownloadButton = page.findChild<QPushButton*>(QStringLiteral("gaiaCatalogDownloadButton"));
    auto* gaiaEvictButton = page.findChild<QPushButton*>(QStringLiteral("gaiaCatalogEvictButton"));
    auto* targetPicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccCatalogDirectoryPicker"));
    auto* targetStatusLabel =
        targetPicker ? targetPicker->findChild<QLabel*>(QStringLiteral("pathPickerStatusLabel")) : nullptr;
    auto* responsePicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccResponseRepositoryPicker"));
    auto* responseStatusLabel = page.findChild<QLabel*>(QStringLiteral("spccResponseManagedRepositoriesLabel"));
    auto* packCombo = page.findChild<QComboBox*>(QStringLiteral("spccCatalogPackCombo"));
    auto* downloadButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogDownloadButton"));
    auto* repairButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogRepairButton"));
    auto* evictButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogEvictButton"));
    ASSERT_NE(gaiaCatalogPicker, nullptr);
    ASSERT_NE(gaiaPackCombo, nullptr);
    ASSERT_NE(gaiaDownloadButton, nullptr);
    ASSERT_NE(gaiaEvictButton, nullptr);
    ASSERT_NE(targetPicker, nullptr);
    ASSERT_NE(targetStatusLabel, nullptr);
    ASSERT_NE(packCombo, nullptr);
    ASSERT_NE(downloadButton, nullptr);
    ASSERT_NE(repairButton, nullptr);
    ASSERT_NE(evictButton, nullptr);
    EXPECT_EQ(responsePicker, nullptr);
    EXPECT_EQ(responseStatusLabel, nullptr);

    EXPECT_EQ(page.pageTitle(), QStringLiteral("Catalogs & Data"));
    EXPECT_TRUE(page.searchKeywords().contains(QStringLiteral("SPCC")));
    EXPECT_FALSE(gaiaCatalogPicker->path().trimmed().isEmpty());
    EXPECT_GE(gaiaPackCombo->findData(QStringLiteral("gaia_dr3_g14")), 0);
    EXPECT_EQ(gaiaPackCombo->currentData().toString(), QStringLiteral("gaia_dr3_g14"));
    EXPECT_TRUE(gaiaDownloadButton->isEnabled());
    EXPECT_FALSE(gaiaEvictButton->isEnabled());
    EXPECT_FALSE(targetPicker->path().trimmed().isEmpty());
    const int fullPackIndex = packCombo->findData(QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b"));
    ASSERT_GE(fullPackIndex, 0);
    EXPECT_EQ(packCombo->itemText(fullPackIndex),
              QStringLiteral("SPCC Gaia DR3 XP Sampled Local Full (spcc-gaia-dr3-xp-sampled-full-2026b)"));
    EXPECT_TRUE(downloadButton->isEnabled());
    EXPECT_TRUE(downloadButton->text().contains(QStringLiteral("Offline"), Qt::CaseInsensitive));
    EXPECT_TRUE(repairButton->isEnabled());
    EXPECT_TRUE(targetStatusLabel->wordWrap());
    EXPECT_EQ(targetStatusLabel->minimumWidth(), 0);
    EXPECT_EQ(targetStatusLabel->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    EXPECT_TRUE(targetStatusLabel->text().contains(QStringLiteral("SPCC catalog cache is empty"), Qt::CaseInsensitive));
}

TEST(CatalogsDataSettingsPageTest, DoesNotDuplicateSpccCatalogPackIdWhenDisplayNameAlreadyIncludesIt)
{
    const StandardPathsTestMode testMode;

    const QString dataDir = an::ui::applicationLocalDataPath();
    ASSERT_FALSE(dataDir.isEmpty());
    ASSERT_TRUE(QDir().mkpath(dataDir));

    QFile mirrorConfig(an::ui::spccCatalogMirrorConfigFilePath());
    ASSERT_TRUE(mirrorConfig.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject descriptor{
        {QStringLiteral("id"), QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b")},
        {QStringLiteral("display_name"),
         QStringLiteral("SPCC Gaia DR3 XP Sampled Local Full (spcc-gaia-dr3-xp-sampled-full-2026b)")},
        {QStringLiteral("description"), QStringLiteral("Approved test descriptor")},
        {QStringLiteral("manifest_path"), QStringLiteral("spcc_gaia_dr3_xp_sampled/local-full/"
                                                         "spcc-gaia-dr3-xp-sampled-full-2026b/catalog_meta.json")},
        {QStringLiteral("distribution_type"), QStringLiteral("chunks")},
        {QStringLiteral("catalog_family_id"), QStringLiteral("spcc_gaia_dr3_xp_sampled")},
        {QStringLiteral("access_profile"), QStringLiteral("local-full")},
        {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("catalog_meta_sha256"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("recommended"), true},
        {QStringLiteral("download_size_bytes"), 7067349374LL},
        {QStringLiteral("installed_size_bytes"), 7394478036LL},
    };
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{QStringLiteral("https://example.test/spcc/")}},
        {QStringLiteral("catalog_packs"), QJsonArray{descriptor}},
    };
    ASSERT_GT(mirrorConfig.write(QJsonDocument(config).toJson(QJsonDocument::Compact)), 0);
    mirrorConfig.close();

    CatalogsDataSettingsPage page(nullptr);
    page.loadSettings();

    auto* packCombo = page.findChild<QComboBox*>(QStringLiteral("spccCatalogPackCombo"));
    ASSERT_NE(packCombo, nullptr);
    const int fullPackIndex = packCombo->findData(QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b"));
    ASSERT_GE(fullPackIndex, 0);
    EXPECT_EQ(packCombo->itemText(fullPackIndex),
              QStringLiteral("SPCC Gaia DR3 XP Sampled Local Full (spcc-gaia-dr3-xp-sampled-full-2026b)"));
}

TEST(CatalogsDataSettingsPageTest, EvictsSelectedAstrometricGaiaCatalogPack)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString packId = QStringLiteral("gaia_dr3_g14");
    const std::filesystem::path root = tempDir.path().toStdString();
    const std::filesystem::path packDir = root / packId.toStdString();
    createCatalogPack(packDir, packId.toStdString(), "Gaia DR3 G<=14");

    CatalogsDataSettingsPage page(nullptr);
    page.loadSettings();

    auto* gaiaCatalogPicker = page.findChild<PathPickerWidget*>(QStringLiteral("gaiaCatalogDirectoryPicker"));
    auto* gaiaPackCombo = page.findChild<QComboBox*>(QStringLiteral("gaiaCatalogPackCombo"));
    auto* gaiaEvictButton = page.findChild<QPushButton*>(QStringLiteral("gaiaCatalogEvictButton"));
    ASSERT_NE(gaiaCatalogPicker, nullptr);
    ASSERT_NE(gaiaPackCombo, nullptr);
    ASSERT_NE(gaiaEvictButton, nullptr);

    gaiaCatalogPicker->setPath(tempDir.path());
    QApplication::processEvents();

    EXPECT_EQ(gaiaPackCombo->currentData().toString(), packId);
    ASSERT_TRUE(gaiaEvictButton->isEnabled());

    gaiaEvictButton->click();
    QApplication::processEvents();

    EXPECT_FALSE(QDir(QString::fromStdString(packDir.string())).exists());
    EXPECT_TRUE(page.hasUnsavedChanges());
}

TEST(PlateSolvingSettingsPageTest, DoesNotDuplicateGaiaCatalogManagementControls)
{
    const QString source =
        readSourceText(QStringLiteral("src/AfterNightControls/controls/settings/PlateSolvingSettingsPage.cpp"));

    EXPECT_FALSE(source.contains(QStringLiteral("gaiaCatalogDirectoryPicker")));
    EXPECT_FALSE(source.contains(QStringLiteral("gaiaCatalogPackCombo")));
    EXPECT_FALSE(source.contains(QStringLiteral("gaiaCatalogDownloadButton")));
    EXPECT_TRUE(source.contains(QStringLiteral("SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY")));
    EXPECT_TRUE(source.contains(QStringLiteral("SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK")));
    EXPECT_FALSE(source.contains(QStringLiteral("upsert(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY")));
    EXPECT_FALSE(source.contains(QStringLiteral("upsert(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK")));
}

TEST(CatalogDownloadServiceTest, SupportsHuggingFaceTokenEnvironmentForPrivateStaging)
{
    const QString source = readSourceText(QStringLiteral("src/AfterNightControls/app/CatalogDownloadService.cpp"));

    EXPECT_TRUE(source.contains(QStringLiteral("HF_TOKEN")));
    EXPECT_TRUE(source.contains(QStringLiteral("HUGGINGFACE_HUB_TOKEN")));
    EXPECT_TRUE(source.contains(QStringLiteral("Authorization")));
    EXPECT_TRUE(source.contains(QStringLiteral("Make the dataset public")));
}

class CatalogsDataSettingsPageHarness final : public CatalogsDataSettingsPage
{
public:
    using CatalogsDataSettingsPage::CatalogsDataSettingsPage;

    QString selectedPackId;
    QString selectedRootDirectory;
    QVector<an::ui::CatalogPackDescriptor> downloadablePacks;
    QVector<an::ui::SpccResponseRepositoryDescriptor> downloadableResponseRepositories;
    QStringList offeredPackIds;
    std::optional<an::ui::SpccRegionalCatalogRequest> pendingRegionalRequest;
    int managedResponseInstallRequests = 0;

protected:
    [[nodiscard]] QVector<an::ui::CatalogPackDescriptor> availableSpccCatalogPacksForDownload() const override
    {
        return downloadablePacks;
    }

    [[nodiscard]] QVector<an::ui::SpccResponseRepositoryDescriptor>
    availableSpccResponseRepositoriesForDownload() const override
    {
        return downloadableResponseRepositories;
    }

    [[nodiscard]] std::optional<an::ui::SpccRegionalCatalogRequest>
    loadPendingSpccRegionalCatalogRequest() const override
    {
        return pendingRegionalRequest;
    }

    bool clearPendingSpccRegionalCatalogRequest() override
    {
        pendingRegionalRequest.reset();
        return true;
    }

    std::optional<SpccCatalogDownloadSelection> runSpccCatalogDownloadAssistant() override
    {
        offeredPackIds.clear();
        return SpccCatalogDownloadSelection{
            .packId = selectedPackId,
            .installDirectory = selectedRootDirectory,
        };
    }

    std::optional<SpccCatalogDownloadSelection>
    runSpccCatalogDownloadAssistant(const QVector<an::ui::CatalogPackDescriptor>& packs) override
    {
        offeredPackIds.clear();
        for (const an::ui::CatalogPackDescriptor& pack : packs)
        {
            offeredPackIds.push_back(pack.id);
        }

        return SpccCatalogDownloadSelection{
            .packId = selectedPackId,
            .installDirectory = selectedRootDirectory,
        };
    }

    bool confirmDeleteSelectedSpccCatalogPack(const QString&, bool) override
    {
        return true;
    }

    bool confirmDeleteAllDownloadedSpccCatalogPacks(const QString&, int) override
    {
        return true;
    }

    void installManagedSpccResponseRepository() override
    {
        ++managedResponseInstallRequests;
    }
};

TEST(CatalogsDataSettingsPageTest, DownloadSelectionUpdatesSpccRootAndPack)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    CatalogsDataSettingsPageHarness page(nullptr);
    page.loadSettings();
    page.selectedPackId = QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510");
    page.selectedRootDirectory = tempDir.path();

    auto* targetPicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccCatalogDirectoryPicker"));
    auto* packCombo = page.findChild<QComboBox*>(QStringLiteral("spccCatalogPackCombo"));
    auto* downloadButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogDownloadButton"));
    ASSERT_NE(targetPicker, nullptr);
    ASSERT_NE(packCombo, nullptr);
    ASSERT_NE(downloadButton, nullptr);

    EXPECT_FALSE(downloadButton->isEnabled());
    EXPECT_TRUE(packCombo->currentData().toString().isEmpty());
    EXPECT_FALSE(page.hasUnsavedChanges());
}

TEST(CatalogsDataSettingsPageTest, FullOfflineSpccCatalogDownloadAlsoQueuesResponseProfiles)
{
    const StandardPathsTestMode testMode;
    QDir(an::ui::defaultSpccResponseRepositoryDirectory()).removeRecursively();

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString packId = QStringLiteral("spcc-gaia-dr3-xp-sampled-full-test");
    CatalogsDataSettingsPageHarness page(nullptr);
    page.downloadablePacks = {
        an::ui::CatalogPackDescriptor{
            .id = packId,
            .displayName = QStringLiteral("SPCC Full Offline Test Pack"),
            .description = QStringLiteral("Synthetic full offline SPCC catalog pack."),
            .manifestUrl = QUrl(QStringLiteral("https://example.test/spcc/catalog_meta.json")),
            .catalogMetaSha256 = QString(64, QLatin1Char('a')),
            .distributionType = QStringLiteral("chunks"),
            .catalogFamilyId = QStringLiteral("spcc_gaia_dr3_xp_sampled"),
            .accessProfile = QString::fromLatin1(an::ui::SPCC_LOCAL_FULL_ACCESS_PROFILE),
            .rightsReviewStatus = QString::fromLatin1(kApprovedSpccCatalogRightsReviewStatus),
            .recommended = true,
            .downloadSizeBytes = 1024,
            .installedSizeBytes = 2048,
        },
    };
    page.downloadableResponseRepositories = {
        an::ui::SpccResponseRepositoryDescriptor{
            .id = QStringLiteral("spcc-response-baseline-test"),
            .repositoryId = QStringLiteral("spcc-response-baseline-test"),
            .version = QStringLiteral("2026a"),
            .displayName = QStringLiteral("SPCC Response Baseline Test"),
            .description = QStringLiteral("Approved managed response repository fixture."),
            .manifestUrl = QUrl(QStringLiteral("https://example.test/responses/response_meta.json")),
            .rightsReviewStatus = QStringLiteral("approved-for-managed-download"),
            .deploymentScope = QStringLiteral("managed-cache"),
            .responseMetaSha256 = QString(64, QLatin1Char('a')),
            .recommended = true,
            .downloadSizeBytes = 1234,
            .installedSizeBytes = 5678,
        },
    };
    page.loadSettings();
    page.selectedPackId = packId;
    page.selectedRootDirectory = tempDir.path();

    auto* downloadButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogDownloadButton"));
    ASSERT_NE(downloadButton, nullptr);
    ASSERT_TRUE(downloadButton->isEnabled());

    downloadButton->click();
    QApplication::processEvents();

    EXPECT_EQ(page.managedResponseInstallRequests, 1);
}

TEST(CatalogsDataSettingsPageTest, HidesManagedResponseRepositorySection)
{
    CatalogsDataSettingsPageHarness page(nullptr);
    page.downloadableResponseRepositories = {
        an::ui::SpccResponseRepositoryDescriptor{
            .id = QStringLiteral("spcc-response-baseline-2026a"),
            .repositoryId = QStringLiteral("spcc-response-baseline"),
            .version = QStringLiteral("2026a"),
            .displayName = QStringLiteral("SPCC Response Baseline"),
            .description = QStringLiteral("Approved managed response repository fixture."),
            .manifestUrl = QUrl(QStringLiteral("https://example.test/responses/response_meta.json")),
            .rightsReviewStatus = QStringLiteral("approved-for-managed-download"),
            .deploymentScope = QStringLiteral("managed-cache"),
            .responseMetaSha256 = QString(64, QLatin1Char('a')),
            .recommended = true,
            .downloadSizeBytes = 1234,
            .installedSizeBytes = 5678,
        },
    };
    page.loadSettings();

    auto* responsePicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccResponseRepositoryPicker"));
    auto* statusLabel = page.findChild<QLabel*>(QStringLiteral("spccResponseManagedRepositoriesLabel"));
    auto* installButton = page.findChild<QPushButton*>(QStringLiteral("spccResponseInstallButton"));

    EXPECT_EQ(responsePicker, nullptr);
    EXPECT_EQ(statusLabel, nullptr);
    EXPECT_EQ(installButton, nullptr);
}

TEST(CatalogsDataSettingsPageTest, RepairAndEvictControlsManageSelectedSpccPack)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString packId = QStringLiteral("spcc-gaia-dr3-xp-sampled-m31-region-20260510");
    const std::filesystem::path root = tempDir.path().toStdString();
    const std::filesystem::path packDir = root / packId.toStdString();
    createSpccCatalogPack(packDir, packId.toStdString(), "installed-regional-cache", false);

    CatalogsDataSettingsPageHarness page(nullptr);
    page.loadSettings();
    page.selectedPackId = packId;
    page.selectedRootDirectory = tempDir.path();

    auto* targetPicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccCatalogDirectoryPicker"));
    auto* repairButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogRepairButton"));
    auto* evictButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogEvictButton"));
    ASSERT_NE(targetPicker, nullptr);
    ASSERT_NE(repairButton, nullptr);
    ASSERT_NE(evictButton, nullptr);

    targetPicker->setPath(tempDir.path());
    QApplication::processEvents();

    EXPECT_FALSE(repairButton->isEnabled());
    repairButton->click();
    QApplication::processEvents();
    EXPECT_EQ(targetPicker->path(), tempDir.path());

    ASSERT_TRUE(QDir(QString::fromStdString(packDir.string())).exists());
    EXPECT_TRUE(evictButton->isEnabled());
    evictButton->click();
    QApplication::processEvents();
    EXPECT_FALSE(QDir(QString::fromStdString(packDir.string())).exists());
    EXPECT_TRUE(page.hasUnsavedChanges());
}

TEST(CatalogsDataSettingsPageTest, DeleteAllDownloadedSpccCatalogsRemovesScannedPacks)
{
    const StandardPathsTestMode testMode;

    const QString managedRootPath = an::ui::defaultSpccCatalogDirectory();
    QDir(managedRootPath).removeRecursively();
    ASSERT_TRUE(QDir().mkpath(managedRootPath));

    const std::filesystem::path root = managedRootPath.toStdString();
    createSpccCatalogPack(root / "spcc-pack-a", "spcc-pack-a", "installed-regional-cache");
    createSpccCatalogPack(root / "spcc-pack-b", "spcc-pack-b", "installed-regional-cache");

    QTemporaryDir externalDir;
    ASSERT_TRUE(externalDir.isValid());
    const std::filesystem::path externalRoot = externalDir.path().toStdString();
    createSpccCatalogPack(externalRoot / "spcc-pack-external", "spcc-pack-external", "installed-regional-cache");

    CatalogsDataSettingsPageHarness page(nullptr);
    page.loadSettings();

    auto* targetPicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccCatalogDirectoryPicker"));
    auto* deleteAllButton = page.findChild<QPushButton*>(QStringLiteral("spccCatalogDeleteAllButton"));
    ASSERT_NE(targetPicker, nullptr);
    ASSERT_NE(deleteAllButton, nullptr);

    targetPicker->setPath(externalDir.path());
    QApplication::processEvents();

    ASSERT_TRUE(deleteAllButton->isEnabled());
    deleteAllButton->click();
    QApplication::processEvents();

    EXPECT_FALSE(QDir(QString::fromStdString((root / "spcc-pack-a").string())).exists());
    EXPECT_FALSE(QDir(QString::fromStdString((root / "spcc-pack-b").string())).exists());
    EXPECT_TRUE(QDir(QString::fromStdString((externalRoot / "spcc-pack-external").string())).exists());
    EXPECT_TRUE(page.hasUnsavedChanges());

    QDir(managedRootPath).removeRecursively();
}

TEST(CatalogsDataSettingsPageTest, PendingRegionalRequestInstallsMatchingDescriptorAndClearsAfterSave)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString diagnostic;
    const std::optional<an::WcsSolution> solution = makeSpccRegionalRequestWcsSolution();
    ASSERT_TRUE(solution.has_value());
    const std::optional<an::ui::SpccRegionalCatalogRequest> request = an::ui::makeSpccRegionalCatalogRequestFromWcs(
        *solution, 4.0, 16.5, QStringLiteral("Orion test frame"), &diagnostic);
    ASSERT_TRUE(request.has_value()) << diagnostic.toStdString();

    const QString packId = QStringLiteral("spcc-gaia-dr3-xp-sampled-orion-region-20260511");
    const std::filesystem::path root = tempDir.path().toStdString();
    createSpccCatalogPack(root / packId.toStdString(), packId.toStdString(), "installed-regional-cache", true,
                          "approved-for-managed-download");

    an::ui::CatalogPackDescriptor coveringPack{
        .id = packId,
        .displayName = QStringLiteral("Orion regional cache"),
        .manifestUrl = QUrl(QStringLiteral("file:///tmp/orion/catalog_meta.json")),
        .distributionType = QStringLiteral("chunks"),
        .catalogFamilyId = QStringLiteral("spcc_gaia_dr3_xp_sampled"),
        .accessProfile = QStringLiteral("installed-regional-cache"),
        .rightsReviewStatus = QStringLiteral("approved-for-managed-download"),
        .hasRegionCoverage = true,
        .centerRaDeg = request->centerRaDeg,
        .centerDecDeg = request->centerDecDeg,
        .coverageRadiusDeg = request->radiusDeg + 0.25,
        .magnitudeLimit = 17.0,
    };
    an::ui::CatalogPackDescriptor wrongSkyPack = coveringPack;
    wrongSkyPack.id = QStringLiteral("spcc-gaia-dr3-xp-sampled-far-region-20260511");
    wrongSkyPack.centerRaDeg = 120.0;
    wrongSkyPack.centerDecDeg = -20.0;

    CatalogsDataSettingsPageHarness page(nullptr);
    page.downloadablePacks = {wrongSkyPack, coveringPack};
    page.selectedPackId = packId;
    page.selectedRootDirectory = tempDir.path();
    page.pendingRegionalRequest = *request;
    page.loadSettings();

    auto* regionalLabel = page.findChild<QLabel*>(QStringLiteral("spccRegionalCacheRequestLabel"));
    auto* regionalInstallButton = page.findChild<QPushButton*>(QStringLiteral("spccRegionalCacheInstallButton"));
    auto* targetPicker = page.findChild<PathPickerWidget*>(QStringLiteral("spccCatalogDirectoryPicker"));
    auto* packCombo = page.findChild<QComboBox*>(QStringLiteral("spccCatalogPackCombo"));
    ASSERT_NE(regionalLabel, nullptr);
    ASSERT_NE(regionalInstallButton, nullptr);
    ASSERT_NE(targetPicker, nullptr);
    ASSERT_NE(packCombo, nullptr);

    EXPECT_FALSE(regionalLabel->isHidden());
    EXPECT_TRUE(regionalLabel->text().contains(QStringLiteral("Orion regional cache")));
    ASSERT_TRUE(regionalInstallButton->isEnabled());

    regionalInstallButton->click();
    QApplication::processEvents();

    EXPECT_EQ(page.offeredPackIds, QStringList{packId});
    EXPECT_EQ(targetPicker->path(), tempDir.path());
    EXPECT_EQ(packCombo->currentData().toString(), packId);
    EXPECT_TRUE(page.hasUnsavedChanges());
    EXPECT_TRUE(page.pendingRegionalRequest.has_value());

    page.saveSettings();

    EXPECT_FALSE(page.pendingRegionalRequest.has_value());
}

} // namespace
