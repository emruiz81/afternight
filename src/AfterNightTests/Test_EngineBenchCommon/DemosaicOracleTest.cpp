// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The demosaic residual's kept sites, VNG4's definition and the capture rows.
#include "DemosaicCaptureScenarios.h"
#include "DemosaicOracle.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{

/// RGGB as the oracle spells it: red, green on red's row, green on blue's row, blue.
constexpr std::array<std::uint8_t, 4u> kRggb = {0u, 1u, 1u, 2u};

/// A picture large enough for the shared border and a few bands of interior.
constexpr std::uint32_t kSide = 48u;

/// Three planes and the mosaic they were made from.
struct Picture
{
    std::vector<float> red;
    std::vector<float> green;
    std::vector<float> blue;
    std::vector<float> mosaic;
};

/// The mosaic of a flat field whose two green phases differ, and the planes VNG4 makes of it: the
/// measured red and blue everywhere, and the mean of the two greens.
Picture imbalancedFlatField()
{
    const std::size_t pixels = static_cast<std::size_t>(kSide) * kSide;
    Picture picture{std::vector<float>(pixels, 0.25f), std::vector<float>(pixels, 0.4375f),
                    std::vector<float>(pixels, 0.375f), std::vector<float>(pixels)};
    for (std::uint32_t y = 0u; y < kSide; ++y)
    {
        for (std::uint32_t x = 0u; x < kSide; ++x)
        {
            const bool evenRow = (y & 1u) == 0u;
            const bool evenColumn = (x & 1u) == 0u;
            picture.mosaic[static_cast<std::size_t>(y) * kSide + x] =
                evenRow ? (evenColumn ? 0.25f : 0.375f) : (evenColumn ? 0.5f : 0.375f);
        }
    }
    return picture;
}

MosaicBlock rggb()
{
    return MosaicBlock{.entries = kRggb, .size = 2u, .originX = 0u, .originY = 0u};
}

} // namespace

TEST(DemosaicOracleTest, OnlyVng4LeavesItsGreenSitesOutOfTheResidual)
{
    EXPECT_EQ(keptMeasurements("Vng4"), KeptMeasurements::RedAndBlue);
    for (const char* method : {"Fast", "Rcd", "Amaze", "Lmmse", "LuminanceAhd", "Bilinear", ""})
    {
        EXPECT_EQ(keptMeasurements(method), KeptMeasurements::Every) << method;
    }

    // The mixed green moves both green phases by an eighth and keeps red and blue.
    const Picture picture = imbalancedFlatField();
    const MosaicBlock block = rggb();
    const double every = demosaicResidual(picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide, block,
                                          kDemosaicResidualBorder, KeptMeasurements::Every);
    EXPECT_NEAR(every, 0.0625 / std::sqrt(2.0), 1e-12);
    EXPECT_EQ(demosaicResidual(picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide, block,
                               kDemosaicResidualBorder, KeptMeasurements::RedAndBlue),
              0.0);

    // A red site that moved still counts.
    Picture moved = picture;
    moved.red[static_cast<std::size_t>(20u) * kSide + 20u] = 0.5f;
    EXPECT_GT(demosaicResidual(moved.red, moved.green, moved.blue, moved.mosaic, kSide, kSide, block,
                               kDemosaicResidualBorder, KeptMeasurements::RedAndBlue),
              0.0);
}

TEST(DemosaicOracleTest, Vng4DefinitionMixesTheTwoGreenPhasesOfAFlatField)
{
    const Picture picture = imbalancedFlatField();
    const std::optional<Vng4DefinitionEvidence> evidence = vng4DefinitionEvidence(
        picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide, rggb(), kDemosaicResidualBorder);
    ASSERT_TRUE(evidence.has_value());
    const std::uint64_t interior =
        std::uint64_t{kSide - 2u * kDemosaicResidualBorder} * (kSide - 2u * kDemosaicResidualBorder) * 3u;
    EXPECT_EQ(evidence->interiorSamples, interior);
    // A flat field has no gradient anywhere, so every direction is taken and nothing is a tie.
    EXPECT_EQ(evidence->decidedSamples, interior);
    EXPECT_EQ(evidence->tiedPixels, 0u);
    EXPECT_EQ(evidence->largestDecidedError, 0.0);
    EXPECT_DOUBLE_EQ(evidence->greenSiteAdjustmentRms, 0.0625);

    // An output that kept each measured green is not VNG4's.
    Picture kept = picture;
    for (std::uint32_t y = 0u; y < kSide; ++y)
    {
        for (std::uint32_t x = 0u; x < kSide; ++x)
        {
            if (((x + y) & 1u) == 1u)
            {
                kept.green[static_cast<std::size_t>(y) * kSide + x] =
                    kept.mosaic[static_cast<std::size_t>(y) * kSide + x];
            }
        }
    }
    const auto keptEvidence = vng4DefinitionEvidence(kept.red, kept.green, kept.blue, kept.mosaic, kSide, kSide, rggb(),
                                                     kDemosaicResidualBorder);
    ASSERT_TRUE(keptEvidence.has_value());
    EXPECT_DOUBLE_EQ(keptEvidence->largestDecidedError, 0.0625);
    EXPECT_EQ(keptEvidence->greenSiteAdjustmentRms, 0.0);
}

TEST(DemosaicOracleTest, Vng4DefinitionReproducesAColourRampAndJudgesEveryColour)
{
    // Each colour a different plane ramp, so a colour taken from the wrong neighbours shows.
    const std::size_t pixels = static_cast<std::size_t>(kSide) * kSide;
    std::array<std::vector<float>, 3u> planes;
    std::vector<float> mosaic(pixels);
    const std::array<float, 3u> offsets = {0.125f, 0.25f, 0.375f};
    const std::array<float, 3u> slopesX = {0.0078125f, 0.00390625f, 0.001953125f};
    const std::array<float, 3u> slopesY = {0.00390625f, 0.0078125f, 0.0009765625f};
    for (std::size_t plane = 0u; plane < 3u; ++plane)
    {
        planes[plane].resize(pixels);
        for (std::uint32_t y = 0u; y < kSide; ++y)
        {
            for (std::uint32_t x = 0u; x < kSide; ++x)
            {
                planes[plane][static_cast<std::size_t>(y) * kSide + x] =
                    offsets[plane] + slopesX[plane] * static_cast<float>(x) + slopesY[plane] * static_cast<float>(y);
            }
        }
    }
    for (std::uint32_t y = 0u; y < kSide; ++y)
    {
        for (std::uint32_t x = 0u; x < kSide; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kSide + x;
            mosaic[index] = planes[kRggb[(y & 1u) * 2u + (x & 1u)]][index];
        }
    }
    // The directions taken are symmetric, so every average of a linear field is exact.
    const auto evidence =
        vng4DefinitionEvidence(planes[0], planes[1], planes[2], mosaic, kSide, kSide, rggb(), kDemosaicResidualBorder);
    ASSERT_TRUE(evidence.has_value());
    EXPECT_EQ(evidence->tiedPixels, 0u);
    EXPECT_EQ(evidence->decidedSamples, evidence->interiorSamples);
    EXPECT_LT(evidence->largestDecidedError, 1e-12);

    // An interpolated sample that moved is found, whichever plane it is in.
    for (std::size_t plane = 0u; plane < 3u; ++plane)
    {
        std::array<std::vector<float>, 3u> movedPlanes = planes;
        // (21, 20) is a green site, where both red and blue are interpolated and green is mixed.
        movedPlanes[plane][static_cast<std::size_t>(20u) * kSide + 21u] += 0.001f;
        const auto moved = vng4DefinitionEvidence(movedPlanes[0], movedPlanes[1], movedPlanes[2], mosaic, kSide, kSide,
                                                  rggb(), kDemosaicResidualBorder);
        ASSERT_TRUE(moved.has_value());
        EXPECT_NEAR(moved->largestDecidedError, 0.001, 1e-6) << plane;
    }
}

TEST(DemosaicOracleTest, Vng4DefinitionAgreesWithTheVendoredImplementationOnATexture)
{
    // A textured mosaic of whole sixteen-bit codes, on which the vendored librtprocess VNG4 decides every
    // direction exactly. Its answers for one block of four by four pixels, rounded to
    // nine digits, are below; the definition reproduces them within single-precision rounding.
    std::vector<float> mosaic(static_cast<std::size_t>(kSide) * kSide);
    for (std::uint32_t y = 0u; y < kSide; ++y)
    {
        for (std::uint32_t x = 0u; x < kSide; ++x)
        {
            const std::uint32_t code = 4096u + ((x * x * 37u + y * y * 91u + x * y * 13u + 7u * x) % 2048u) * 8u;
            mosaic[static_cast<std::size_t>(y) * kSide + x] = static_cast<float>(code) / 65535.0f;
        }
    }
    constexpr float kVendored[16][3] = {
        {2.143587470e-01f, 2.112458944e-01f, 2.262526453e-01f}, {1.820139587e-01f, 1.788967699e-01f, 1.812365055e-01f},
        {1.589379758e-01f, 1.558164209e-01f, 1.578204334e-01f}, {1.443465650e-01f, 1.415528655e-01f, 1.432211846e-01f},
        {2.046478987e-01f, 1.977569312e-01f, 2.072012424e-01f}, {1.734164655e-01f, 1.682968289e-01f, 1.709010452e-01f},
        {1.500383317e-01f, 1.466900557e-01f, 1.492942721e-01f}, {1.370236874e-01f, 1.341166198e-01f, 1.367208362e-01f},
        {2.109407187e-01f, 2.002716064e-01f, 2.164431214e-01f}, {1.855479628e-01f, 1.784259230e-01f, 1.843972951e-01f},
        {1.618677080e-01f, 1.582927257e-01f, 1.621634364e-01f}, {1.514357775e-01f, 1.484153569e-01f, 1.501854062e-01f},
        {2.697428763e-01f, 2.230258584e-01f, 2.459245622e-01f}, {2.455996126e-01f, 2.057526559e-01f, 2.150911689e-01f},
        {2.283076644e-01f, 1.953307390e-01f, 2.004679292e-01f}, {2.182290107e-01f, 1.863228381e-01f, 1.872587204e-01f}};
    const std::optional<Vng4Definition> definition =
        vng4Definition(mosaic, kSide, kSide, rggb(), kDemosaicResidualBorder);
    ASSERT_TRUE(definition.has_value());
    for (std::uint32_t y = 20u; y < 24u; ++y)
    {
        for (std::uint32_t x = 20u; x < 24u; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * kSide + x;
            const float* expected = kVendored[(y - 20u) * 4u + (x - 20u)];
            EXPECT_EQ(definition->decided[index], 7u) << x << ", " << y;
            EXPECT_NEAR(definition->red[index], expected[0], 1e-7) << x << ", " << y;
            EXPECT_NEAR(definition->green[index], expected[1], 1e-7) << x << ", " << y;
            EXPECT_NEAR(definition->blue[index], expected[2], 1e-7) << x << ", " << y;
        }
    }
    // Outside the interior the definition says nothing.
    EXPECT_EQ(definition->decided[0], 0u);
    EXPECT_EQ(definition->green[static_cast<std::size_t>(kDemosaicResidualBorder - 1u) * kSide + 20u], 0.0);
    EXPECT_FALSE(
        vng4Definition(std::span<const float>(mosaic).first(100u), kSide, kSide, rggb(), kDemosaicResidualBorder)
            .has_value());
}

TEST(DemosaicOracleTest, Vng4DefinitionLeavesATiedChoiceUndecided)
{
    // One bright sample on a flat field: around it, directions score exact multiples of its contrast and
    // some fall exactly on the threshold, where the arithmetic that evaluates them decides the choice.
    const std::size_t pixels = static_cast<std::size_t>(kSide) * kSide;
    std::vector<float> impulse(pixels, 0.25f);
    impulse[static_cast<std::size_t>(24u) * kSide + 24u] = 0.75f;
    const std::vector<float> zero(pixels, 0.0f);
    const auto evidence =
        vng4DefinitionEvidence(zero, zero, zero, impulse, kSide, kSide, rggb(), kDemosaicResidualBorder);
    ASSERT_TRUE(evidence.has_value());
    EXPECT_GT(evidence->tiedPixels, 0u);
    EXPECT_LT(evidence->decidedSamples, evidence->interiorSamples);
    // Far from the impulse every sample is decided and none is zero.
    EXPECT_GT(evidence->largestDecidedError, 0.2);
}

TEST(DemosaicOracleTest, Vng4DefinitionRefusesWhatItCannotJudge)
{
    const Picture picture = imbalancedFlatField();
    const std::array<std::uint8_t, 4u> notBayer = {0u, 1u, 1u, 1u};
    EXPECT_FALSE(vng4DefinitionEvidence(picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide,
                                        MosaicBlock{.entries = notBayer, .size = 2u}, kDemosaicResidualBorder)
                     .has_value());
    std::vector<std::uint8_t> sixBySix(36u, 1u);
    EXPECT_FALSE(vng4DefinitionEvidence(picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide,
                                        MosaicBlock{.entries = sixBySix, .size = 6u}, kDemosaicResidualBorder)
                     .has_value());
    EXPECT_FALSE(
        vng4DefinitionEvidence(picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide, rggb(), 3u)
            .has_value());
    EXPECT_FALSE(vng4DefinitionEvidence(std::span<const float>(picture.red).first(10u), picture.green, picture.blue,
                                        picture.mosaic, kSide, kSide, rggb(), kDemosaicResidualBorder)
                     .has_value());
    EXPECT_FALSE(vng4DefinitionEvidence(picture.red, picture.green, picture.blue, picture.mosaic, kSide, 32u, rggb(),
                                        kDemosaicResidualBorder)
                     .has_value());
}

TEST(DemosaicOracleTest, RecordedEvidenceIsWhatTheRowsRulesJudge)
{
    ScenarioSpec row;
    row.id = "demosaic.vng4.f32";
    row.group = "demosaic";
    row.tolerance = 1e-3;
    row.parameters["method"] = "Vng4";
    row.parameters[kDemosaicQualityOracleParameter] = kVng4DefinitionOracle;
    EXPECT_TRUE(requiresDemosaicEvidence(row));

    const Picture picture = imbalancedFlatField();
    ResultRecord record;
    recordDemosaicEvidence(row, picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide, rggb(), record);
    ASSERT_TRUE(record.correctnessMetric.has_value());
    EXPECT_EQ(*record.correctnessMetric, 0.0);
    EXPECT_EQ(record.counters.at("vng4LargestDecidedError"), 0.0);
    EXPECT_TRUE(demosaicEvidenceValid(row, record));

    // Each part of the evidence is required.
    for (const char* counter : {"vng4LargestDecidedError", "vng4DecidedSamples", "vng4InteriorSamples"})
    {
        ResultRecord missing = record;
        missing.counters.erase(counter);
        EXPECT_FALSE(demosaicEvidenceValid(row, missing)) << counter;
    }
    ResultRecord wrong = record;
    wrong.counters["vng4LargestDecidedError"] = 2.0 * kVng4DecidedTolerance;
    EXPECT_FALSE(demosaicEvidenceValid(row, wrong));
    ResultRecord mostlyTied = record;
    mostlyTied.counters["vng4DecidedSamples"] = mostlyTied.counters.at("vng4InteriorSamples") * 0.4;
    EXPECT_FALSE(demosaicEvidenceValid(row, mostlyTied));
    ResultRecord moved = record;
    moved.correctnessMetric = 2e-3;
    EXPECT_FALSE(demosaicEvidenceValid(row, moved));
    ResultRecord none = record;
    none.correctnessMetric.reset();
    EXPECT_FALSE(demosaicEvidenceValid(row, none));

    // The definition a row names is the only one it can be held to.
    ScenarioSpec unknown = row;
    unknown.parameters[kDemosaicQualityOracleParameter] = "something-else";
    EXPECT_FALSE(demosaicEvidenceValid(unknown, record));

    // A method that keeps every site is judged at its green sites as well, and rounded samples a method
    // states it evaluated stand in for the mosaic only in the definition.
    ScenarioSpec amaze = row;
    amaze.parameters["method"] = "Amaze";
    amaze.parameters.erase(kDemosaicQualityOracleParameter);
    ResultRecord amazeRecord;
    recordDemosaicEvidence(amaze, picture.red, picture.green, picture.blue, picture.mosaic, kSide, kSide, rggb(),
                           amazeRecord, picture.mosaic);
    EXPECT_NEAR(*amazeRecord.correctnessMetric, 0.0625 / std::sqrt(2.0), 1e-12);
    EXPECT_FALSE(amazeRecord.counters.contains("vng4LargestDecidedError"));
    EXPECT_FALSE(demosaicEvidenceValid(amaze, amazeRecord));

    ScenarioSpec superPixel = row;
    superPixel.parameters.clear();
    EXPECT_FALSE(requiresDemosaicEvidence(superPixel));
}

TEST(DemosaicOracleTest, TheCaptureRowsAreTheSyntheticRowsOverTheCapture)
{
    std::vector<ScenarioSpec> catalogue;
    for (const char* method : {"fast", "rcd", "amaze", "lmmse", "vng4", "luminance_ahd", "bilinear"})
    {
        ScenarioSpec row;
        row.id = std::string("demosaic.") + method + ".f32";
        row.group = "demosaic";
        row.inputSource = InputSource::Synthetic;
        row.corpusId = "synthetic.stars";
        row.input.width = 1024u;
        row.input.height = 1024u;
        row.input.channels = 1u;
        row.input.sampleType = SampleType::Float32;
        row.parameters["method"] = method;
        if (std::string(method) == "vng4")
        {
            row.parameters[kDemosaicQualityOracleParameter] = kVng4DefinitionOracle;
            row.correctnessClass = CorrectnessClass::RmsPsnr;
            row.tolerance = 1e-3;
        }
        catalogue.push_back(row);
    }
    const std::vector<ScenarioSpec> rows = demosaicCaptureScenarios(catalogue);
    ASSERT_EQ(rows.size(), 6u);
    const std::array<const char*, 6u> ids = {"demosaic.fast.ngc253.f32",  "demosaic.rcd.ngc253.f32",
                                             "demosaic.amaze.ngc253.f32", "demosaic.lmmse.ngc253.f32",
                                             "demosaic.vng4.ngc253.f32",  "demosaic.luminance_ahd.ngc253.f32"};
    for (std::size_t index = 0u; index < rows.size(); ++index)
    {
        const ScenarioSpec& row = rows[index];
        EXPECT_EQ(row.id, ids[index]);
        EXPECT_EQ(row.inputSource, InputSource::Corpus);
        EXPECT_EQ(row.corpusId, kDemosaicCaptureCorpusId);
        EXPECT_EQ(row.input.width, kDemosaicCaptureWidth);
        EXPECT_EQ(row.input.height, kDemosaicCaptureHeight);
        EXPECT_EQ(row.input.sampleType, SampleType::UInt16);
        EXPECT_EQ(row.parameters.at("samples"), "codes-over-65535");
        EXPECT_EQ(row.elementsProcessed, std::uint64_t{kDemosaicCaptureWidth} * kDemosaicCaptureHeight);
        EXPECT_EQ(row.parameters.at("method"), catalogue[index].parameters.at("method"));
    }
    EXPECT_EQ(rows[4].parameters.at(kDemosaicQualityOracleParameter), kVng4DefinitionOracle);
    EXPECT_EQ(rows[4].correctnessClass, CorrectnessClass::RmsPsnr);
    EXPECT_DOUBLE_EQ(rows[4].tolerance, 1e-3);
    EXPECT_TRUE(demosaicCaptureScenarios({}).empty());
}
