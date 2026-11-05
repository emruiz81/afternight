// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The session document's format 3: every engine-facing choice stored by its key and read back as the text or integer
/// the model held, each set of image points stored and read in engine pixel coordinates with its convention stated; a
/// document of another format, or one that states none, refused with the format found and the one this version reads,
/// and a value no table knows refused with the field named.

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/SessionDocument.h"
#include "processes/core/RegistrationRun.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace
{

using namespace ImageIntegration;

const QString kContext = QStringLiteral("session 'test'");
const QString kLightGroup = QStringLiteral("4096x3072|-|-|-|Ha|NGC 7000|-");

/// A model's JSON as this version's model holds it: one light group's stacking settings set away from every default.
QJsonObject modelJson()
{
    StackingSettings stacking;
    stacking.stackMethod = QStringLiteral("Median");
    stacking.rejectionMethod = QStringLiteral("Linear Fit");
    stacking.sigmaLow = 2.5;
    stacking.sigmaHigh = 3.5;
    stacking.weighting = QStringLiteral("Noise");
    stacking.createRejectionMaps = true;
    stacking.starCountScaling = QStringLiteral("Linear");
    stacking.limitMemory = true;
    stacking.maxMemoryMb = 1024;
    stacking.normalizationMode = NormalizationMode::Local;
    stacking.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
    stacking.scaleEstimator = ScaleEstimatorMethod::MAD;
    stacking.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    stacking.manualNormalizationReference = QStringLiteral("D:/Fixture/lights/light_b.fit");
    stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    stacking.cometStackingMode = QStringLiteral("Comet");
    stacking.drizzle.enabled = true;
    stacking.drizzle.scale = 3.0;
    stacking.drizzle.pixelFraction = 0.7;
    stacking.drizzle.kernel = QStringLiteral("Gaussian");
    stacking.drizzle.cfaAware = true;
    stacking.drizzle.cfaPattern = QStringLiteral("GBRG");

    IntegrationDataModel model;
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);
    model.setStackingSettings(FrameType::Light, GroupKey{kLightGroup}, stacking);
    return model.toJson();
}

QJsonObject lightStacking(const QJsonObject& document)
{
    return document.value(QStringLiteral("stackingSettings"))
        .toObject()
        .value(QStringLiteral("LIGHT"))
        .toArray()
        .first()
        .toObject()
        .value(QStringLiteral("settings"))
        .toObject();
}

QJsonObject withLightStacking(QJsonObject document, const QJsonObject& settings)
{
    QJsonObject root = document.value(QStringLiteral("stackingSettings")).toObject();
    QJsonArray lights = root.value(QStringLiteral("LIGHT")).toArray();
    QJsonObject record = lights.first().toObject();
    record.insert(QStringLiteral("settings"), settings);
    lights.replace(0, record);
    root.insert(QStringLiteral("LIGHT"), lights);
    document.insert(QStringLiteral("stackingSettings"), root);
    return document;
}

QJsonObject withLightStackingValue(const QJsonObject& document, const QString& field, const QJsonValue& value)
{
    QJsonObject settings = lightStacking(document);
    settings.insert(field, value);
    return withLightStacking(document, settings);
}

QJsonObject withLightDrizzleValue(const QJsonObject& document, const QString& field, const QJsonValue& value)
{
    QJsonObject settings = lightStacking(document);
    QJsonObject drizzle = settings.value(QStringLiteral("drizzle")).toObject();
    drizzle.insert(field, value);
    settings.insert(QStringLiteral("drizzle"), drizzle);
    return withLightStacking(document, settings);
}

QJsonObject withRegistration(QJsonObject document, const QJsonObject& settings)
{
    QJsonObject record;
    record.insert(QStringLiteral("groupKey"),
                  QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("value"), QStringLiteral("reg|group")}});
    record.insert(QStringLiteral("settings"), settings);
    document.insert(QStringLiteral("registrationSettings"), QJsonArray{record});
    return document;
}

QJsonObject registrationOf(const QJsonObject& document)
{
    return document.value(QStringLiteral("registrationSettings"))
        .toArray()
        .first()
        .toObject()
        .value(QStringLiteral("settings"))
        .toObject();
}

/// The model's JSON written as a stored document.
QJsonObject storedDocument(const QJsonObject& model)
{
    const SessionDocumentResult written = writeSessionDocument(model, kContext);
    EXPECT_TRUE(written.ok()) << written.refusal.toStdString();
    return written.document.value_or(QJsonObject{});
}

/// Registration settings holding one manual control point and one comet observation, as the model holds them.
QJsonObject registrationWithImagePoints()
{
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("star_matching");
    RegistrationSettings::ManualControlPoint point;
    point.sourcePoint = QPointF(10.5, 20.5);
    point.referencePoint = QPointF(11.5, 21.5);
    settings.manualControlPointsByFrame.insert(QStringLiteral("D:/lights/a.fit"), {point});
    RegistrationSettings::CometTrackingSettings comet;
    RegistrationSettings::CometTrackingObservation observation;
    observation.frameId = QStringLiteral("D:/lights/a.fit");
    observation.sourcePosition = QPointF(100.5, 50.5);
    observation.apertureRadiusPixels = 8.0;
    comet.observations.push_back(observation);
    settings.cometTracking = comet;
    return RegistrationHashUtils::registrationSettingsToJson(settings);
}

TEST(SessionDocumentTest, ADocumentThatStatesNoFormatIsRefused)
{
    const SessionDocumentResult read = readSessionDocument(modelJson(), kContext);
    ASSERT_FALSE(read.ok());
    EXPECT_EQ(read.storedSchema, 0);
    EXPECT_EQ(read.refusal.toStdString(),
              "The session 'test' states no format version; this version of AfterNight reads format 3.");
}

TEST(SessionDocumentTest, ADocumentOfAnotherFormatIsRefusedNamingBothFormats)
{
    for (const int format : {1, 2, 4})
    {
        QJsonObject document = storedDocument(modelJson());
        document.insert(QLatin1String(kSessionSchemaKey), format);
        const SessionDocumentResult read = readSessionDocument(document, kContext);
        ASSERT_FALSE(read.ok()) << format;
        EXPECT_EQ(read.storedSchema, format);
        EXPECT_EQ(read.refusal.toStdString(),
                  QStringLiteral("The session 'test' was written in format %1; this version "
                                 "of AfterNight reads format 3.")
                      .arg(format)
                      .toStdString());
    }

    QJsonObject malformed = storedDocument(modelJson());
    malformed.insert(QLatin1String(kSessionSchemaKey), QStringLiteral("three"));
    const SessionDocumentResult read = readSessionDocument(malformed, kContext);
    EXPECT_FALSE(read.ok());
    EXPECT_EQ(storedSessionSchema(malformed), -1);
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("this version of AfterNight reads format 3")))
        << read.refusal.toStdString();
}

TEST(SessionDocumentTest, AFormatThreeDocumentStoresEveryChoiceByItsKey)
{
    const QJsonObject model = modelJson();
    const QJsonObject document = storedDocument(model);
    EXPECT_EQ(document.value(QLatin1String(kSessionSchemaKey)).toInt(), kSessionSchemaVersion);
    EXPECT_EQ(document.value(QStringLiteral("lightCalibrationCacheMode")).toString(), QStringLiteral("in-memory"));
    const QJsonObject settings = lightStacking(document);
    EXPECT_EQ(settings.value(QStringLiteral("stackMethod")).toString(), QStringLiteral("median"));
    EXPECT_EQ(settings.value(QStringLiteral("rejectionMethod")).toString(), QStringLiteral("linear-fit-clipping"));
    EXPECT_EQ(settings.value(QStringLiteral("weighting")).toString(), QStringLiteral("noise"));
    EXPECT_EQ(settings.value(QStringLiteral("starCountScaling")).toString(), QStringLiteral("linear"));
    EXPECT_EQ(settings.value(QStringLiteral("normalizationMode")).toString(), QStringLiteral("local"));
    EXPECT_EQ(settings.value(QStringLiteral("globalNormalizationMethod")).toString(),
              QStringLiteral("multiplicative-scaling"));
    EXPECT_EQ(settings.value(QStringLiteral("scaleEstimator")).toString(), QStringLiteral("mad"));
    EXPECT_EQ(settings.value(QStringLiteral("normalizationReferenceMode")).toString(), QStringLiteral("manual"));
    EXPECT_EQ(settings.value(QStringLiteral("registeredOutputPolicy")).toString(), QStringLiteral("discardable"));
    const QJsonObject drizzle = settings.value(QStringLiteral("drizzle")).toObject();
    EXPECT_EQ(drizzle.value(QStringLiteral("kernel")).toString(), QStringLiteral("gaussian"));
    EXPECT_EQ(drizzle.value(QStringLiteral("cfaPattern")).toString(), QStringLiteral("gbrg"));
    // What is not an engine-facing choice is kept as it was.
    EXPECT_EQ(settings.value(QStringLiteral("cometStackingMode")).toString(), QStringLiteral("Comet"));
    EXPECT_EQ(settings.value(QStringLiteral("manualNormalizationReference")).toString(),
              QStringLiteral("D:/Fixture/lights/light_b.fit"));

    // Reading it gives the model what it held.
    const SessionDocumentResult read = readSessionDocument(document, kContext);
    ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
    EXPECT_EQ(read.storedSchema, kSessionSchemaVersion);
    EXPECT_EQ(*read.document, model);
}

TEST(SessionDocumentTest, EveryStackingValueTheDialogsOfferRoundTripsAsTheSameText)
{
    const QJsonObject base = modelJson();
    const std::vector<std::pair<QString, QStringList>> offered = {
        {QStringLiteral("stackMethod"),
         {QStringLiteral("Average"), QStringLiteral("Sum"), QStringLiteral("Maximum"), QStringLiteral("Minimum"),
          QStringLiteral("Median"), QStringLiteral("Auto-Adaptive Weighted Average")}},
        {QStringLiteral("rejectionMethod"),
         {QStringLiteral("None"), QStringLiteral("Sigma Clipping"), QStringLiteral("MAD Clipping"),
          QStringLiteral("Median Sigma"), QStringLiteral("Winsorized Sigma Clipping"), QStringLiteral("Percentile"),
          QStringLiteral("GESDT"), QStringLiteral("Linear Fit")}},
        {QStringLiteral("weighting"),
         {QStringLiteral("None"), QStringLiteral("Number of stars"), QStringLiteral("Weighted FWHM"),
          QStringLiteral("Noise"), QStringLiteral("Number of images"), QStringLiteral("Photometric")}},
        {QStringLiteral("starCountScaling"),
         {QStringLiteral("Linear"), QStringLiteral("Quadratic"), QStringLiteral("Exponential"),
          QStringLiteral("Roundness Weighted")}}};
    for (const auto& [field, values] : offered)
    {
        for (const QString& value : values)
        {
            const QJsonObject model = withLightStackingValue(base, field, value);
            const SessionDocumentResult written = writeSessionDocument(model, kContext);
            ASSERT_TRUE(written.ok()) << field.toStdString() << " " << value.toStdString();
            const SessionDocumentResult read = readSessionDocument(*written.document, kContext);
            ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
            EXPECT_EQ(lightStacking(*read.document).value(field).toString(), value) << field.toStdString();
        }
    }
    // The drizzle's choices: the kernel the dialog offers, and the CFA pattern a session or a preset states.
    const std::vector<std::pair<QString, QStringList>> drizzleOffered = {
        {QStringLiteral("kernel"),
         {QStringLiteral("Square"), QStringLiteral("Point"), QStringLiteral("Turbo"), QStringLiteral("Gaussian"),
          QStringLiteral("Lanczos2"), QStringLiteral("Lanczos3")}},
        {QStringLiteral("cfaPattern"),
         {QStringLiteral("Auto"), QStringLiteral("RGGB"), QStringLiteral("BGGR"), QStringLiteral("GBRG"),
          QStringLiteral("GRBG")}}};
    for (const auto& [field, values] : drizzleOffered)
    {
        for (const QString& value : values)
        {
            const QJsonObject model = withLightDrizzleValue(base, field, value);
            const SessionDocumentResult written = writeSessionDocument(model, kContext);
            ASSERT_TRUE(written.ok()) << field.toStdString() << " " << value.toStdString();
            const SessionDocumentResult read = readSessionDocument(*written.document, kContext);
            ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
            EXPECT_EQ(lightStacking(*read.document).value(QStringLiteral("drizzle")).toObject().value(field).toString(),
                      value)
                << field.toStdString();
        }
    }
    for (int mode = 0; mode < 3; ++mode)
    {
        const QJsonObject model = withLightStackingValue(base, QStringLiteral("normalizationMode"), mode);
        const SessionDocumentResult read =
            readSessionDocument(*writeSessionDocument(model, kContext).document, kContext);
        ASSERT_TRUE(read.ok());
        EXPECT_EQ(lightStacking(*read.document).value(QStringLiteral("normalizationMode")).toInt(), mode);
    }
}

TEST(SessionDocumentTest, EveryRegistrationValueKeepsItsMeaningAndItsFingerprints)
{
    const RegistrationSettings defaults;
    QJsonObject settings = RegistrationHashUtils::registrationSettingsToJson(defaults);
    const std::vector<std::pair<QString, QStringList>> offered = {
        {QStringLiteral("interpolation"),
         {QStringLiteral("Auto"), QStringLiteral("Nearest Neighbor"), QStringLiteral("Bilinear"),
          QStringLiteral("Bicubic"), QStringLiteral("Pixel Area Relation"), QStringLiteral("Lanczos4"),
          QStringLiteral("Bit Exact Bilinear"), QStringLiteral("Bit Exact Nearest Neighbor")}},
        {QStringLiteral("cfaPattern"),
         {QStringLiteral("Auto"), QStringLiteral("RGGB"), QStringLiteral("BGGR"), QStringLiteral("GBRG"),
          QStringLiteral("GRBG")}},
        {QStringLiteral("debayerMethod"),
         {QStringLiteral("default"), QStringLiteral("Bilinear"), QStringLiteral("VNG4"), QStringLiteral("AHD"),
          QStringLiteral("AMaZE"), QStringLiteral("LMMSE"), QStringLiteral("RCD")}},
        {QStringLiteral("alignmentMode"),
         {QStringLiteral("standard"), QStringLiteral("mosaic"), QStringLiteral("intersection")}},
        {QStringLiteral("transformation"),
         {QStringLiteral("Homography"), QStringLiteral("similarity"), QStringLiteral("affine"),
          QStringLiteral("homography"), QStringLiteral("polynomial2"), QStringLiteral("polynomial3"),
          QStringLiteral("polynomial4"), QStringLiteral("auto")}}};
    const QJsonObject base = modelJson();
    for (const auto& [field, values] : offered)
    {
        for (const QString& value : values)
        {
            QJsonObject registration = settings;
            registration.insert(field, value);
            registration.insert(QStringLiteral("registrationMethod"), QStringLiteral("star_matching"));
            const QJsonObject model = withRegistration(base, registration);
            const SessionDocumentResult written = writeSessionDocument(model, kContext);
            ASSERT_TRUE(written.ok()) << written.refusal.toStdString();
            const SessionDocumentResult read = readSessionDocument(*written.document, kContext);
            ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
            const QJsonObject back = registrationOf(*read.document);
            if (field == QStringLiteral("transformation"))
            {
                // The integration dialog stores the key and the records' default the capitalized text; both keep
                // their meaning, and the fingerprints read the key.
                EXPECT_EQ(persistedChoiceFromText(PersistedChoice::TransformModel, back.value(field).toString()).key,
                          persistedChoiceFromText(PersistedChoice::TransformModel, value).key);
            }
            else
            {
                EXPECT_EQ(back.value(field).toString(), value) << field.toStdString();
            }
            const RegistrationSettings before = RegistrationHashUtils::registrationSettingsFromJson(registration);
            const RegistrationSettings after = RegistrationHashUtils::registrationSettingsFromJson(back);
            EXPECT_EQ(RegistrationHashUtils::registrationDetectionHash(before),
                      RegistrationHashUtils::registrationDetectionHash(after));
            EXPECT_EQ(RegistrationHashUtils::registrationTransformHash(before, QString()),
                      RegistrationHashUtils::registrationTransformHash(after, QString()))
                << field.toStdString() << " " << value.toStdString();
        }
    }
}

TEST(SessionDocumentTest, TheFingerprintsReadAChoiceByItsMeaningNotItsSpelling)
{
    // The record's default spells the transform "Homography" and the dialog stores "homography"; the stored
    // interpolation and pattern have their own spellings too. One meaning is one fingerprint.
    RegistrationSettings capitalized;
    capitalized.registrationMethod = QStringLiteral("star_matching");
    capitalized.transformation = QStringLiteral("Homography");
    capitalized.cfaPattern = QStringLiteral("Auto");
    capitalized.debayerMethod = QStringLiteral("default");
    capitalized.chromaticAberration.enabled = true;
    capitalized.chromaticAberration.transformType = QStringLiteral("Similarity");
    capitalized.chromaticAberration.interpolation = QStringLiteral("Lanczos4");
    RegistrationSettings keyed = capitalized;
    keyed.transformation = QStringLiteral("homography");
    keyed.cfaPattern = QStringLiteral("auto");
    keyed.debayerMethod = QStringLiteral("auto");
    keyed.chromaticAberration.transformType = QStringLiteral("similarity");
    keyed.chromaticAberration.interpolation = QStringLiteral("lanczos4");
    EXPECT_EQ(RegistrationHashUtils::registrationTransformHash(capitalized, QString()),
              RegistrationHashUtils::registrationTransformHash(keyed, QString()));
    EXPECT_EQ(RegistrationHashUtils::registrationDetectionHash(capitalized),
              RegistrationHashUtils::registrationDetectionHash(keyed));

    // Another meaning is another fingerprint.
    RegistrationSettings affine = keyed;
    affine.transformation = QStringLiteral("affine");
    EXPECT_NE(RegistrationHashUtils::registrationTransformHash(affine, QString()),
              RegistrationHashUtils::registrationTransformHash(keyed, QString()));
}

/// The dialog's selection for a contract, as the dialog, the tab and the report read it.
QString selectionOf(const RegistrationSettings& settings)
{
    return RegistrationSettingsUtils::transformSelectionKey(settings.transformMode, settings.transformOrder,
                                                            settings.transformation);
}

// A transform contract is read one way, by its mode. A linear mode with an order and two-pass refinement beside it -
// which no dialog writes, but a document edited by hand can hold - would be shown as a polynomial by the dialog, fitted
// as the linear model by the star matcher and as a polynomial by a manual fit.
TEST(SessionDocumentTest, ATransformContractThatMixesItsFieldsReadsAsItsModeEverywhere)
{
    RegistrationSettings mixed;
    mixed.registrationMethod = QStringLiteral("star_matching");
    mixed.transformation = QStringLiteral("Affine");
    mixed.transformMode = QStringLiteral("linear");
    mixed.transformOrder = 3;
    mixed.twoPassRefinementEnabled = true;

    // Even before a read reconciles it, the dialog and a manual fit take the mode as the star matcher does.
    EXPECT_EQ(selectionOf(mixed), QStringLiteral("affine"));
    EXPECT_EQ(an::ui::processes::manualRegistrationFit(mixed, 64u, 48u).polynomialPolicy,
              an::RegistrationPolynomialPolicy::LinearOnly);

    const QJsonObject model = withRegistration(modelJson(), RegistrationHashUtils::registrationSettingsToJson(mixed));
    const SessionDocumentResult written = writeSessionDocument(model, kContext);
    ASSERT_TRUE(written.ok()) << written.refusal.toStdString();
    const SessionDocumentResult read = readSessionDocument(*written.document, kContext);
    ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
    const RegistrationSettings settings =
        RegistrationHashUtils::registrationSettingsFromJson(registrationOf(*read.document));

    // The read keeps nothing the mode does not use, so the contract cannot say two things.
    EXPECT_EQ(settings.transformOrder, 0);
    EXPECT_FALSE(settings.twoPassRefinementEnabled);
    EXPECT_EQ(selectionOf(settings), QStringLiteral("affine"));
    const an::Result<an::ui::processes::RegistrationModel> run = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(run->fit.polynomialPolicy, an::RegistrationPolynomialPolicy::LinearOnly);
    EXPECT_EQ(run->fit.linearKind, an::RegistrationTransformKind::Affine);
    const an::RegistrationFitRequest manual = an::ui::processes::manualRegistrationFit(settings, 64u, 48u);
    EXPECT_EQ(manual.polynomialPolicy, an::RegistrationPolynomialPolicy::LinearOnly);
    EXPECT_EQ(manual.linearKind, an::RegistrationTransformKind::Affine);

    // A polynomial mode states its order and refines in a second pass, whatever the flag beside it said.
    RegistrationSettings polynomial = mixed;
    polynomial.transformation = QStringLiteral("Homography");
    polynomial.transformMode = QStringLiteral("polynomial");
    polynomial.twoPassRefinementEnabled = false;
    const RegistrationSettings polynomialRead = RegistrationHashUtils::registrationSettingsFromJson(
        RegistrationHashUtils::registrationSettingsToJson(polynomial));
    EXPECT_EQ(polynomialRead.transformOrder, 3);
    EXPECT_TRUE(polynomialRead.twoPassRefinementEnabled);
    EXPECT_EQ(selectionOf(polynomialRead), QStringLiteral("polynomial3"));
    const an::Result<an::ui::processes::RegistrationModel> polynomialRun =
        an::ui::processes::registrationModelOf(polynomialRead);
    ASSERT_TRUE(polynomialRun.hasValue());
    EXPECT_EQ(polynomialRun->fit.polynomialPolicy, an::RegistrationPolynomialPolicy::StatedOrder);
    EXPECT_EQ(polynomialRun->fit.polynomialOrder, 3u);
    const an::RegistrationFitRequest polynomialManual =
        an::ui::processes::manualRegistrationFit(polynomialRead, 64u, 48u);
    EXPECT_EQ(polynomialManual.polynomialPolicy, an::RegistrationPolynomialPolicy::StatedOrder);
    EXPECT_EQ(polynomialManual.polynomialOrder, 3u);

    // Two-pass refinement is read by no run, so it moves no fingerprint.
    RegistrationSettings flagged = polynomialRead;
    flagged.twoPassRefinementEnabled = false;
    EXPECT_EQ(RegistrationHashUtils::registrationTransformHash(flagged, QString()),
              RegistrationHashUtils::registrationTransformHash(polynomialRead, QString()));
}

// A demosaic method no table knows - DCB among them - is a value like any other unknown one: the write is refused,
// naming it, and nothing is read in its place.
TEST(SessionDocumentTest, AnUnknownDemosaicMethodRefusesTheWrite)
{
    QJsonObject registration = RegistrationHashUtils::registrationSettingsToJson(RegistrationSettings{});
    registration.insert(QStringLiteral("debayerMethod"), QStringLiteral("DCB"));
    const SessionDocumentResult written = writeSessionDocument(withRegistration(modelJson(), registration), kContext);
    EXPECT_FALSE(written.ok());
    EXPECT_TRUE(written.refusal.contains(QStringLiteral("'DCB'"))) << written.refusal.toStdString();
    EXPECT_TRUE(written.refusal.contains(QStringLiteral("debayerMethod"))) << written.refusal.toStdString();

    QJsonObject stored = storedDocument(
        withRegistration(modelJson(), RegistrationHashUtils::registrationSettingsToJson(RegistrationSettings{})));
    QJsonArray registrations = stored.value(QStringLiteral("registrationSettings")).toArray();
    QJsonObject record = registrations.first().toObject();
    QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
    settings.insert(QStringLiteral("debayerMethod"), QStringLiteral("dcb"));
    record.insert(QStringLiteral("settings"), settings);
    registrations.replace(0, record);
    stored.insert(QStringLiteral("registrationSettings"), registrations);
    const SessionDocumentResult read = readSessionDocument(stored, kContext);
    EXPECT_FALSE(read.ok());
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("'dcb'"))) << read.refusal.toStdString();
}

TEST(SessionDocumentTest, AFormatThreeDocumentHoldingSomethingOtherThanAKeyIsRefused)
{
    QJsonObject document = storedDocument(modelJson());
    // A label is not a key: a format 3 writer never stores one.
    document = withLightStackingValue(document, QStringLiteral("stackMethod"), QStringLiteral("Median"));
    const SessionDocumentResult read = readSessionDocument(document, kContext);
    EXPECT_FALSE(read.ok());
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("stackMethod"))) << read.refusal.toStdString();
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("LIGHT"))) << read.refusal.toStdString();
    EXPECT_TRUE(read.refusal.contains(kLightGroup)) << read.refusal.toStdString();

    QJsonObject integer = storedDocument(modelJson());
    integer = withLightStackingValue(integer, QStringLiteral("normalizationMode"), 2);
    EXPECT_FALSE(readSessionDocument(integer, kContext).ok());
}

TEST(SessionDocumentTest, AValueNoTableKnowsRefusesTheWrite)
{
    const QJsonObject model =
        withLightStackingValue(modelJson(), QStringLiteral("weighting"), QStringLiteral("Brightness"));
    const SessionDocumentResult written = writeSessionDocument(model, kContext);
    EXPECT_FALSE(written.ok());
    EXPECT_TRUE(written.refusal.contains(QStringLiteral("'Brightness'"))) << written.refusal.toStdString();

    const QJsonObject outOfRange = withLightStackingValue(modelJson(), QStringLiteral("scaleEstimator"), 7);
    EXPECT_FALSE(writeSessionDocument(outOfRange, kContext).ok());
}

TEST(SessionDocumentTest, ImagePointsAreStoredAndReadInEnginePixelCoordinates)
{
    const QJsonObject model = withRegistration(modelJson(), registrationWithImagePoints());
    const QJsonObject document = storedDocument(model);
    const QJsonObject stored = registrationOf(document);
    EXPECT_EQ(stored.value(QStringLiteral("manualControlPoints"))
                  .toArray()
                  .first()
                  .toObject()
                  .value(QLatin1String(RegistrationHashUtils::kImagePointPixelConventionKey))
                  .toString(),
              QLatin1String(RegistrationHashUtils::kImagePointPixelConvention));

    const SessionDocumentResult read = readSessionDocument(document, kContext);
    ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
    const RegistrationSettings settings =
        RegistrationHashUtils::registrationSettingsFromJson(registrationOf(*read.document));
    const auto points = settings.manualControlPointsByFrame.value(QStringLiteral("D:/lights/a.fit"));
    ASSERT_EQ(points.size(), 1);
    EXPECT_DOUBLE_EQ(points.front().sourcePoint.x(), 10.5);
    EXPECT_DOUBLE_EQ(points.front().sourcePoint.y(), 20.5);
    EXPECT_DOUBLE_EQ(points.front().referencePoint.x(), 11.5);
    EXPECT_DOUBLE_EQ(points.front().referencePoint.y(), 21.5);
    ASSERT_TRUE(settings.cometTracking.has_value());
    ASSERT_EQ(settings.cometTracking->observations.size(), 1u);
    EXPECT_DOUBLE_EQ(settings.cometTracking->observations.front().sourcePosition.x(), 100.5);
    EXPECT_DOUBLE_EQ(settings.cometTracking->observations.front().sourcePosition.y(), 50.5);
}

// A set of image points states the convention its positions are in; one that states none is no set this version
// wrote, and the document is refused rather than read half a pixel from where the points stand.
TEST(SessionDocumentTest, ASetOfImagePointsThatStatesNoConventionIsRefused)
{
    const QJsonObject document = storedDocument(withRegistration(modelJson(), registrationWithImagePoints()));
    const QString conventionKey = QLatin1String(RegistrationHashUtils::kImagePointPixelConventionKey);

    QJsonObject withoutPointConvention = document;
    {
        QJsonArray registrations = withoutPointConvention.value(QStringLiteral("registrationSettings")).toArray();
        QJsonObject record = registrations.first().toObject();
        QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
        QJsonArray sets = settings.value(QStringLiteral("manualControlPoints")).toArray();
        QJsonObject set = sets.first().toObject();
        set.remove(conventionKey);
        sets.replace(0, set);
        settings.insert(QStringLiteral("manualControlPoints"), sets);
        record.insert(QStringLiteral("settings"), settings);
        registrations.replace(0, record);
        withoutPointConvention.insert(QStringLiteral("registrationSettings"), registrations);
    }
    const SessionDocumentResult points = readSessionDocument(withoutPointConvention, kContext);
    EXPECT_FALSE(points.ok());
    EXPECT_TRUE(points.refusal.contains(QStringLiteral("manualControlPoints"))) << points.refusal.toStdString();

    QJsonObject withoutCometConvention = document;
    {
        QJsonArray registrations = withoutCometConvention.value(QStringLiteral("registrationSettings")).toArray();
        QJsonObject record = registrations.first().toObject();
        QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
        QJsonObject comet = settings.value(QStringLiteral("cometTracking")).toObject();
        comet.remove(conventionKey);
        settings.insert(QStringLiteral("cometTracking"), comet);
        record.insert(QStringLiteral("settings"), settings);
        registrations.replace(0, record);
        withoutCometConvention.insert(QStringLiteral("registrationSettings"), registrations);
    }
    const SessionDocumentResult comet = readSessionDocument(withoutCometConvention, kContext);
    EXPECT_FALSE(comet.ok());
    EXPECT_TRUE(comet.refusal.contains(QStringLiteral("cometTracking"))) << comet.refusal.toStdString();
}

} // namespace
