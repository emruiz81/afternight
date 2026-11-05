// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's persistence contracts: the registration record, written and read at format 5 in engine
/// pixel coordinates and refused at any other, the drizzle descriptor documents and their dense payloads, the fixed
/// tables a stored setting is read through, and the bounded envelope every document is published in.

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{

using namespace ImageIntegration;

QJsonArray matrixJson(const std::array<double, 9>& m)
{
    QJsonArray rows;
    for (int r = 0; r < 3; ++r)
    {
        rows.append(QJsonArray{m[static_cast<std::size_t>(r * 3)], m[static_cast<std::size_t>(r * 3 + 1)],
                               m[static_cast<std::size_t>(r * 3 + 2)]});
    }
    return rows;
}

an::RegistrationTransform parsed(const QString& json)
{
    an::RegistrationTransform transform;
    QString error;
    EXPECT_TRUE(stage_utils::parseRegistrationTransformJson(json, transform, error)) << error.toStdString();
    return transform;
}

/// A record as this version's writer writes it for the identity, with one field set to another value.
QString recordWith(const QString& field, const QJsonValue& value)
{
    QJsonObject object = QJsonDocument::fromJson(stage_utils::serializeTransformJson({}).toUtf8()).object();
    object.insert(field, value);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// A record of format 4 as its writer wrote it: its kind, its linear type and its matrix, no pixel convention stated.
QJsonObject formatFourRecord()
{
    return QJsonObject{{QStringLiteral("schemaVersion"), 4},
                       {QStringLiteral("kind"), QStringLiteral("affine")},
                       {QStringLiteral("type"), 1},
                       {QStringLiteral("matrix"), matrixJson({1.0, 0.0, 12.25, 0.0, 1.0, -3.5, 0.0, 0.0, 1.0})},
                       {QStringLiteral("sourceDistortionPolicy"), QStringLiteral("InheritMetadata")}};
}

/// A distortion of order two in each component, about a reference pixel.
an::SipDistortion distortionModel()
{
    an::SipDistortionRequest request;
    request.referencePixel = {2048.5, 1536.5};
    const auto set = [](an::SipPolynomial& polynomial, const double c20, const double c11, const double c02) {
        polynomial.order = 2u;
        polynomial.coefficients[2][0] = c20;
        polynomial.coefficients[1][1] = c11;
        polynomial.coefficients[0][2] = c02;
    };
    set(request.forwardX, 1.0e-6, -2.0e-7, 3.0e-7);
    set(request.forwardY, -4.0e-7, 5.0e-7, 1.0e-6);
    set(request.inverseX, -1.0e-6, 2.0e-7, -3.0e-7);
    set(request.inverseY, 4.0e-7, -5.0e-7, -1.0e-6);
    const an::Result<an::SipDistortion> sip = an::SipDistortion::create(request);
    EXPECT_TRUE(sip.hasValue()) << sip.status().message();
    return *sip;
}

// ---- The registration record.

TEST(RegistrationTransformRecordTest, ARecordWrittenNowRoundTrips)
{
    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = {1.0, 0.01, 3.5, -0.01, 1.0, -2.25, 0.0, 0.0, 1.0};
    request.polynomial.order = 2;
    request.polynomial.x = {0.0, 1.0, 0.0, 1.0e-4, 0.0, -1.0e-4};
    request.polynomial.y = {0.0, 0.0, 1.0, 0.0, 2.0e-4, 0.0};
    request.polynomial.normalizationOrigin = {100.5, 50.5};
    request.polynomial.normalizationScale = {100.0, 50.0};
    const auto transform = an::RegistrationTransform::create(request);
    ASSERT_TRUE(transform.hasValue()) << transform.status().message();
    const stage_utils::SourceDistortionProvenance provenance =
        stage_utils::SourceDistortionProvenance::explicitModel(distortionModel());

    const QString json = stage_utils::serializeTransformJson(*transform, provenance);
    QString error;
    const auto record = stage_utils::parseTransformRecord(json, error);
    ASSERT_TRUE(record.has_value()) << error.toStdString();
    EXPECT_EQ(record->schemaVersion, stage_utils::kRegistrationTransformSchemaVersion);
    EXPECT_EQ(record->transform, *transform);
    EXPECT_EQ(record->provenance, provenance);
    EXPECT_EQ(stage_utils::serializeTransformJson(record->transform, record->provenance), json);
}

// A record states its format; one of another format, or one that states none, is refused naming the format found and
// the one this version reads, and nothing is read from it - not even a record of format 4 its writer wrote whole.
TEST(RegistrationTransformRecordTest, ARecordOfAnotherFormatIsRefusedNamingBothFormats)
{
    for (const int format : {2, 3, 4, 6})
    {
        QString error;
        EXPECT_FALSE(stage_utils::parseTransformRecord(recordWith(QStringLiteral("schemaVersion"), format), error))
            << format;
        EXPECT_EQ(error.toStdString(), QStringLiteral("The transform record was written in format %1; this version of "
                                                      "AfterNight reads format 5.")
                                           .arg(format)
                                           .toStdString());
    }

    QString error;
    EXPECT_FALSE(
        stage_utils::parseTransformRecord(QString::fromUtf8(QJsonDocument(formatFourRecord()).toJson()), error));
    EXPECT_TRUE(error.contains(QStringLiteral("written in format 4"))) << error.toStdString();

    QJsonObject unstated = QJsonDocument::fromJson(stage_utils::serializeTransformJson({}).toUtf8()).object();
    unstated.remove(QStringLiteral("schemaVersion"));
    EXPECT_FALSE(stage_utils::parseTransformRecord(QString::fromUtf8(QJsonDocument(unstated).toJson()), error));
    EXPECT_EQ(error.toStdString(),
              "The transform record states no format version; this version of AfterNight reads format 5.");
}

TEST(RegistrationTransformRecordTest, ARecordThisVersionCannotReadIsRefusedNotGuessed)
{
    QString error;
    QJsonObject untagged = QJsonDocument::fromJson(stage_utils::serializeTransformJson({}).toUtf8()).object();
    untagged.remove(QStringLiteral("pixelConvention"));
    EXPECT_FALSE(stage_utils::parseTransformRecord(QString::fromUtf8(QJsonDocument(untagged).toJson()), error));

    // A kind its payload contradicts.
    EXPECT_FALSE(
        stage_utils::parseTransformRecord(recordWith(QStringLiteral("kind"), QStringLiteral("polynomial2")), error));
    EXPECT_FALSE(stage_utils::parseTransformRecord(QStringLiteral("not json"), error));
}

// ---- The drizzle documents.

DrizzleDescriptorDocument descriptorDocument()
{
    DrizzleDescriptorDocument document;
    document.frameId = QStringLiteral("D:/lights/light_001.fit");
    document.sourceWidth = 64;
    document.sourceHeight = 48;
    document.outputWidth = 128;
    document.outputHeight = 96;
    document.scale = 2.0;
    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = {1.0, 0.0, 1.5, 0.0, 1.0, -2.5, 0.0, 0.0, 1.0};
    document.transform = *an::RegistrationTransform::create(request);
    document.sourceDistortion = stage_utils::SourceDistortionProvenance::inheritMetadata();
    document.postTranslation = {3.0, 4.0};
    document.engineIdentity = QByteArray(2048, '\x5a');
    return document;
}

TEST(DrizzleDescriptorDocumentTest, ADescriptorRoundTripsThroughItsEnvelope)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    DrizzleDescriptorDocument document = descriptorDocument();
    document.densePayload = DrizzleDensePayloadReference{QStringLiteral("light_001.anmap"), 77u, QString(64, 'a')};
    const QString path = dir.filePath(QStringLiteral("light_001.andesc"));
    ASSERT_TRUE(publishDocument(path, drizzleDescriptorEnvelope(document)).ok());

    QString error;
    const auto read = readDrizzleDescriptor(path, error);
    ASSERT_TRUE(read.has_value()) << error.toStdString();
    EXPECT_EQ(read->frameId, document.frameId);
    EXPECT_EQ(read->outputWidth, 128u);
    EXPECT_EQ(read->transform, document.transform);
    EXPECT_EQ(read->postTranslation, document.postTranslation);
    EXPECT_EQ(read->engineIdentity, document.engineIdentity);
    ASSERT_TRUE(read->densePayload.has_value());
    EXPECT_EQ(*read->densePayload, *document.densePayload);
    EXPECT_EQ(drizzleMappingRequestOf(*read).transform, document.transform);
}

TEST(DrizzleDescriptorDocumentTest, ADescriptorWhoseEnvelopeCarriesAnotherIdentityIsRefused)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    DocumentEnvelope envelope = drizzleDescriptorEnvelope(descriptorDocument());
    envelope.identity = QString(64, 'f');
    const QString path = dir.filePath(QStringLiteral("light.andesc"));
    ASSERT_TRUE(publishDocument(path, envelope).ok());
    QString error;
    EXPECT_FALSE(readDrizzleDescriptor(path, error).has_value());
}

TEST(DrizzleDescriptorDocumentTest, ADescriptorWhoseTransformIsOfAnotherFormatOrOutOfBoundsIsRefused)
{
    QJsonObject object = QJsonDocument::fromJson(formatDrizzleDescriptor(descriptorDocument())).object();
    QJsonObject mapping = object.value(QStringLiteral("mapping")).toObject();
    mapping.insert(QStringLiteral("transform"), formatFourRecord());
    object.insert(QStringLiteral("mapping"), mapping);
    QString error;
    EXPECT_FALSE(parseDrizzleDescriptor(QJsonDocument(object).toJson(), error).has_value());

    QJsonObject wide = QJsonDocument::fromJson(formatDrizzleDescriptor(descriptorDocument())).object();
    wide.insert(QStringLiteral("sourceWidth"), static_cast<double>(kMaxDrizzleSourceSide) + 1.0);
    EXPECT_FALSE(parseDrizzleDescriptor(QJsonDocument(wide).toJson(), error).has_value());

    QJsonObject identity = QJsonDocument::fromJson(formatDrizzleDescriptor(descriptorDocument())).object();
    identity.insert(QStringLiteral("engineIdentity"), QString::fromLatin1(QByteArray(100, 'x').toBase64()));
    EXPECT_FALSE(parseDrizzleDescriptor(QJsonDocument(identity).toJson(), error).has_value());
}

TEST(DrizzleDescriptorDocumentTest, ADensePayloadIsReadOnlyBesideTheDescriptorItWasWrittenFor)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    DrizzleDescriptorDocument document = descriptorDocument();
    const auto sampleBytes = drizzleDensePayloadSampleBytes(document.sourceWidth, document.sourceHeight);
    ASSERT_TRUE(sampleBytes.has_value());
    QByteArray samples(static_cast<qsizetype>(*sampleBytes), '\0');
    for (qsizetype i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<char>(i % 251);
    }
    const auto payload = drizzleDensePayloadEnvelope(document, samples);
    ASSERT_TRUE(payload.has_value());
    const QString payloadPath = dir.filePath(QStringLiteral("light.anmap"));
    const QByteArray payloadBytes = encodeDocumentEnvelope(*payload);
    document.densePayload = DrizzleDensePayloadReference{
        QStringLiteral("light.anmap"), static_cast<std::uint64_t>(payloadBytes.size()),
        QString::fromLatin1(QCryptographicHash::hash(payloadBytes, QCryptographicHash::Sha256).toHex())};
    const QString descriptorPath = dir.filePath(QStringLiteral("light.andesc"));
    ASSERT_TRUE(publishDocumentPair(payloadPath, *payload, descriptorPath, drizzleDescriptorEnvelope(document)).ok());

    QString error;
    const auto descriptor = readDrizzleDescriptor(descriptorPath, error);
    ASSERT_TRUE(descriptor.has_value()) << error.toStdString();
    const auto read = readDrizzleDensePayload(payloadPath, *descriptor, error);
    ASSERT_TRUE(read.has_value()) << error.toStdString();
    EXPECT_EQ(*read, samples);

    // The same samples written for another light are not this descriptor's.
    DrizzleDescriptorDocument other = *descriptor;
    other.frameId = QStringLiteral("D:/lights/light_002.fit");
    EXPECT_FALSE(readDrizzleDensePayload(payloadPath, other, error).has_value());

    // A payload changed in place, same length, is refused by its checksum.
    {
        QFile file(payloadPath);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        file.seek(file.size() - 1);
        const char flipped = static_cast<char>(samples.back() ^ 0x01);
        file.write(&flipped, 1);
    }
    EXPECT_FALSE(readDrizzleDensePayload(payloadPath, *descriptor, error).has_value());

    // Samples that do not cover the source extent are refused before anything is written.
    EXPECT_FALSE(drizzleDensePayloadEnvelope(document, samples.left(samples.size() - 8)).has_value());
}

// ---- The fixed tables.

TEST(PersistedChoiceMapsTest, EveryKeyReadsAsItselfAndNamesAnEngineValue)
{
    const std::array choices{PersistedChoice::StackMethod,       PersistedChoice::RejectionMethod,
                             PersistedChoice::Weighting,         PersistedChoice::StarCountScaling,
                             PersistedChoice::DrizzleKernel,     PersistedChoice::CfaPattern,
                             PersistedChoice::DemosaicMethod,    PersistedChoice::Interpolation,
                             PersistedChoice::AlignmentMode,     PersistedChoice::TransformModel,
                             PersistedChoice::NormalizationMode, PersistedChoice::GlobalNormalizationMethod,
                             PersistedChoice::ScaleEstimator,    PersistedChoice::NormalizationReferenceMode,
                             PersistedChoice::ExecutionMode};
    for (const PersistedChoice choice : choices)
    {
        const QStringList keys = persistedChoiceKeys(choice);
        ASSERT_FALSE(keys.isEmpty()) << persistedChoiceName(choice);
        for (const QString& key : keys)
        {
            const PersistedChoiceValue value = persistedChoiceFromText(choice, key);
            EXPECT_TRUE(value.known) << persistedChoiceName(choice) << " " << key.toStdString();
            EXPECT_EQ(value.key, key);
        }
    }
    for (const QString& key : persistedChoiceKeys(PersistedChoice::DrizzleKernel))
        EXPECT_TRUE(drizzleKernelForKey(key).has_value()) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::Weighting))
        EXPECT_TRUE(stackWeightingForKey(key).has_value()) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::RejectionMethod))
        EXPECT_TRUE(stackRejectionForKey(key).has_value()) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::StackMethod))
        EXPECT_TRUE(stackCombinationForKey(key).has_value() || stackRejectionForKey(key).has_value())
            << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::AlignmentMode))
        EXPECT_TRUE(canvasModeForKey(key).has_value()) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::GlobalNormalizationMethod))
        EXPECT_TRUE(normalizationMethodForKey(key).has_value()) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::ScaleEstimator))
        EXPECT_TRUE(scaleEstimatorForKey(key).has_value()) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::Interpolation))
        EXPECT_EQ(interpolationForKey(key).has_value(), key != QStringLiteral("auto")) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::DemosaicMethod))
        EXPECT_EQ(demosaicMethodForKey(key).has_value(), key != QStringLiteral("auto")) << key.toStdString();
    for (const QString& key : persistedChoiceKeys(PersistedChoice::CfaPattern))
        EXPECT_EQ(cfaPatternForKey(key).has_value(), key != QStringLiteral("auto")) << key.toStdString();
}

TEST(PersistedChoiceMapsTest, TheIntegersTheModelStoresAreTheTablesIntegers)
{
    for (const NormalizationMode mode : kPersistedNormalizationModes)
    {
        const auto value = persistedChoiceFromInteger(PersistedChoice::NormalizationMode, static_cast<int>(mode));
        ASSERT_TRUE(value.known);
        EXPECT_EQ(integerForKey(PersistedChoice::NormalizationMode, value.key), static_cast<int>(mode));
    }
    for (const GlobalNormalizationMethod method : kPersistedGlobalNormalizationMethods)
    {
        const auto value =
            persistedChoiceFromInteger(PersistedChoice::GlobalNormalizationMethod, static_cast<int>(method));
        ASSERT_TRUE(value.known);
        EXPECT_EQ(integerForKey(PersistedChoice::GlobalNormalizationMethod, value.key), static_cast<int>(method));
    }
    for (const ScaleEstimatorMethod estimator : kPersistedScaleEstimatorMethods)
        EXPECT_TRUE(persistedChoiceFromInteger(PersistedChoice::ScaleEstimator, static_cast<int>(estimator)).known);
    for (const NormalizationReferenceMode mode : kPersistedNormalizationReferenceModes)
        EXPECT_TRUE(
            persistedChoiceFromInteger(PersistedChoice::NormalizationReferenceMode, static_cast<int>(mode)).known);
    for (const ExecutionMode mode : kPersistedExecutionModes)
        EXPECT_TRUE(persistedChoiceFromInteger(PersistedChoice::ExecutionMode, static_cast<int>(mode)).known);

    EXPECT_EQ(persistedChoiceFromInteger(PersistedChoice::NormalizationMode, 1).key, QStringLiteral("global"));
    EXPECT_EQ(persistedChoiceFromInteger(PersistedChoice::GlobalNormalizationMethod, 3).key,
              QStringLiteral("multiplicative-scaling"));
    EXPECT_FALSE(persistedChoiceFromInteger(PersistedChoice::NormalizationMode, 7).known);
    EXPECT_FALSE(persistedChoiceFromInteger(PersistedChoice::StackMethod, 0).known);
}

TEST(PersistedChoiceMapsTest, TheDialogsSpellingsAreKnownAndAnyOtherNameIsNot)
{
    EXPECT_EQ(
        persistedChoiceFromText(PersistedChoice::RejectionMethod, QStringLiteral("Winsorized Sigma Clipping")).key,
        QStringLiteral("winsorized-sigma-clipping"));
    EXPECT_EQ(persistedChoiceFromText(PersistedChoice::RejectionMethod, QStringLiteral("MAD Clipping")).key,
              QStringLiteral("mad-sigma-clipping"));
    EXPECT_EQ(persistedChoiceFromText(PersistedChoice::RejectionMethod, QStringLiteral("Linear Fit")).key,
              QStringLiteral("linear-fit-clipping"));
    EXPECT_EQ(persistedChoiceFromText(PersistedChoice::Weighting, QStringLiteral("Number of Stars")).key,
              QStringLiteral("star-count"));
    EXPECT_EQ(persistedChoiceFromText(PersistedChoice::Interpolation, QStringLiteral("Pixel Area Relation")).key,
              QStringLiteral("area"));
    EXPECT_EQ(persistedChoiceFromText(PersistedChoice::TransformModel, QStringLiteral("Homography")).key,
              QStringLiteral("homography"));
    // A name no table holds - a demosaic method the engine does not have among them - is not known.
    for (const char* name : {"DCB", "HPHD", "IGV"})
    {
        const PersistedChoiceValue unknown =
            persistedChoiceFromText(PersistedChoice::DemosaicMethod, QLatin1String(name));
        EXPECT_FALSE(unknown.known) << name;
        EXPECT_TRUE(unknown.key.isEmpty()) << name;
    }
    EXPECT_FALSE(
        persistedChoiceFromText(PersistedChoice::RejectionMethod, QStringLiteral("Linear Fit Clipping")).known);
    EXPECT_FALSE(persistedChoiceFromText(PersistedChoice::DrizzleKernel, QStringLiteral("cubic")).known);
}

// ---- The document envelope and its publication.

DocumentEnvelope sampleEnvelope(const QByteArray& payload = QByteArrayLiteral("{\"a\":1}"))
{
    return DocumentEnvelope{QStringLiteral("test-document"), 2, QStringLiteral("identity-1"), payload};
}

DocumentReadRequest sampleRequest()
{
    DocumentReadRequest request;
    request.documentType = QStringLiteral("test-document");
    request.maximumSchemaVersion = 2;
    request.expectedIdentity = QStringLiteral("identity-1");
    request.maxPayloadBytes = 1024;
    return request;
}

TEST(DocumentEnvelopeTest, AnEnvelopeReadsBackAndRefusesEverythingThatIsNotIt)
{
    const QByteArray bytes = encodeDocumentEnvelope(sampleEnvelope());
    const DocumentReadResult read = decodeDocumentEnvelope(bytes, sampleRequest());
    ASSERT_TRUE(read.ok()) << read.message.toStdString();
    EXPECT_EQ(read.envelope.payload, sampleEnvelope().payload);
    EXPECT_EQ(read.envelope.schemaVersion, 2);

    DocumentReadRequest request = sampleRequest();
    EXPECT_EQ(decodeDocumentEnvelope(QByteArrayLiteral("plain text"), request).failure,
              DocumentReadFailure::NotAnEnvelope);
    request.documentType = QStringLiteral("other");
    EXPECT_EQ(decodeDocumentEnvelope(bytes, request).failure, DocumentReadFailure::WrongType);
    request = sampleRequest();
    request.maximumSchemaVersion = 1;
    EXPECT_EQ(decodeDocumentEnvelope(bytes, request).failure, DocumentReadFailure::UnknownSchema);
    request = sampleRequest();
    request.expectedIdentity = QStringLiteral("identity-2");
    EXPECT_EQ(decodeDocumentEnvelope(bytes, request).failure, DocumentReadFailure::IdentityMismatch);
    request = sampleRequest();
    request.maxPayloadBytes = 3;
    EXPECT_EQ(decodeDocumentEnvelope(bytes, request).failure, DocumentReadFailure::TooLarge);
    EXPECT_EQ(decodeDocumentEnvelope(bytes.left(bytes.size() - 1), sampleRequest()).failure,
              DocumentReadFailure::LengthMismatch);
    QByteArray changed = bytes;
    changed[changed.size() - 2] = '2';
    EXPECT_EQ(decodeDocumentEnvelope(changed, sampleRequest()).failure, DocumentReadFailure::ChecksumMismatch);
    QByteArray future = bytes;
    future[8] = static_cast<char>(kDocumentEnvelopeVersion + 1);
    EXPECT_EQ(decodeDocumentEnvelope(future, sampleRequest()).failure, DocumentReadFailure::UnknownEnvelopeVersion);
    EXPECT_EQ(readDocument(QStringLiteral("Z:/no/such/document"), sampleRequest()).failure,
              DocumentReadFailure::Missing);
}

/// File operations over the real file system with one step made to fail.
enum class FailingStep
{
    None,
    Create,
    Write,
    DiskFull,
    Flush,
    Close,
    Replace
};

DocumentFileOperations failingAt(const FailingStep step, int* replaceCount = nullptr)
{
    DocumentFileOperations operations = systemDocumentFileOperations();
    const DocumentFileOperations real = operations;
    if (step == FailingStep::Create)
        operations.create = [](const QString&, QString& error) -> DocumentFileOperations::Handle {
            error = QStringLiteral("refused");
            return nullptr;
        };
    if (step == FailingStep::Write || step == FailingStep::DiskFull)
        operations.write = [real, step](DocumentFileOperations::Handle handle, const QByteArray& bytes, bool& diskFull,
                                        QString& error) {
            QString ignored;
            (void)real.write(handle, bytes.left(bytes.size() / 2), diskFull, ignored);
            diskFull = step == FailingStep::DiskFull;
            error = QStringLiteral("short write");
            return false;
        };
    if (step == FailingStep::Flush)
        operations.flush = [](DocumentFileOperations::Handle, QString& error) {
            error = QStringLiteral("flush refused");
            return false;
        };
    if (step == FailingStep::Close)
        operations.close = [real](DocumentFileOperations::Handle handle, QString& error) {
            QString ignored;
            (void)real.close(handle, ignored);
            error = QStringLiteral("close refused");
            return false;
        };
    if (step == FailingStep::Replace)
        operations.replace = [](const QString&, const QString&, QString& error) {
            error = QStringLiteral("replace refused");
            return false;
        };
    if (replaceCount != nullptr)
    {
        const auto replace = operations.replace;
        operations.replace = [replace, replaceCount](const QString& from, const QString& to, QString& error) {
            ++*replaceCount;
            return replace(from, to, error);
        };
    }
    return operations;
}

QStringList pendingIn(const QString& directory)
{
    QStringList pending;
    for (const QString& name : QDir(directory).entryList(QDir::Files | QDir::Hidden))
    {
        if (isPendingDocumentPath(name))
            pending.append(name);
    }
    return pending;
}

QByteArray contentsOf(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

TEST(DocumentPublicationTest, APublicationReplacesItsDestinationWhole)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sidecar.json"));
    ASSERT_TRUE(publishDocument(path, sampleEnvelope(QByteArrayLiteral("first"))).ok());
    ASSERT_TRUE(publishDocument(path, sampleEnvelope(QByteArrayLiteral("second"))).ok());
    const DocumentReadResult read = readDocument(path, sampleRequest());
    ASSERT_TRUE(read.ok());
    EXPECT_EQ(read.envelope.payload, QByteArrayLiteral("second"));
    EXPECT_TRUE(pendingIn(dir.path()).isEmpty());
}

TEST(DocumentPublicationTest, AFailureAtAnyStepLeavesTheDestinationAsItWasAndNothingBeside)
{
    const std::array steps{FailingStep::Create, FailingStep::Write, FailingStep::DiskFull,
                           FailingStep::Flush,  FailingStep::Close, FailingStep::Replace};
    const std::array expected{DocumentPublicationFailure::CreateFailed, DocumentPublicationFailure::WriteFailed,
                              DocumentPublicationFailure::DiskFull,     DocumentPublicationFailure::FlushFailed,
                              DocumentPublicationFailure::CloseFailed,  DocumentPublicationFailure::ReplaceFailed};
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sidecar.json"));
        ASSERT_TRUE(publishDocument(path, sampleEnvelope(QByteArrayLiteral("before"))).ok());
        const QByteArray before = contentsOf(path);

        const DocumentPublicationResult result =
            publishDocument(path, sampleEnvelope(QByteArrayLiteral("after")), {}, failingAt(steps[i]));
        EXPECT_EQ(result.failure, expected[i]) << documentPublicationFailureName(result.failure);
        EXPECT_EQ(contentsOf(path), before) << documentPublicationFailureName(expected[i]);
        EXPECT_TRUE(pendingIn(dir.path()).isEmpty()) << documentPublicationFailureName(expected[i]);
    }
}

TEST(DocumentPublicationTest, AStopBeforeTheReplacePublishesNothing)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sidecar.json"));
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(publishDocument(path, sampleEnvelope(), stop.get_token()).failure, DocumentPublicationFailure::Cancelled);
    EXPECT_FALSE(QFileInfo::exists(path));

    // A stop that arrives while the bytes are written is observed before the replace.
    std::stop_source late;
    DocumentFileOperations operations = systemDocumentFileOperations();
    const auto write = operations.write;
    operations.write = [write, &late](DocumentFileOperations::Handle handle, const QByteArray& bytes, bool& diskFull,
                                      QString& error) {
        const bool written = write(handle, bytes, diskFull, error);
        late.request_stop();
        return written;
    };
    EXPECT_EQ(publishDocument(path, sampleEnvelope(), late.get_token(), operations).failure,
              DocumentPublicationFailure::Cancelled);
    EXPECT_FALSE(QFileInfo::exists(path));
    EXPECT_TRUE(pendingIn(dir.path()).isEmpty());
}

TEST(DocumentPublicationTest, APairPublishesItsPayloadBeforeTheManifestThatNamesIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString payload = dir.filePath(QStringLiteral("map.anmap"));
    const QString manifest = dir.filePath(QStringLiteral("map.andesc"));
    int replaces = 0;
    DocumentFileOperations operations = failingAt(FailingStep::None, &replaces);
    const auto replace = operations.replace;
    operations.replace = [replace, &manifest](const QString& from, const QString& to, QString& error) {
        if (to == manifest)
        {
            error = QStringLiteral("manifest refused");
            return false;
        }
        return replace(from, to, error);
    };
    const DocumentPublicationResult result =
        publishDocumentPair(payload, sampleEnvelope(QByteArrayLiteral("payload")), manifest,
                            sampleEnvelope(QByteArrayLiteral("manifest")), {}, operations);
    EXPECT_EQ(result.failure, DocumentPublicationFailure::ReplaceFailed);
    EXPECT_TRUE(QFileInfo::exists(payload));
    EXPECT_FALSE(QFileInfo::exists(manifest));
    EXPECT_EQ(replaces, 1) << "the payload was published and the manifest was refused before its replace";

    // Recovery removes the orphan it is told nothing references, and only that.
    const QString unrelated = dir.filePath(QStringLiteral("light.fits"));
    ASSERT_TRUE(QFile(unrelated).open(QIODevice::WriteOnly));
    ASSERT_TRUE(QFile(dir.filePath(QStringLiteral("x.anpending"))).open(QIODevice::WriteOnly));
    const DocumentRecoveryReport report =
        recoverDocumentDirectory(dir.path(), [](const QString& name) { return name == QStringLiteral("map.anmap"); });
    EXPECT_TRUE(report.failed.isEmpty());
    EXPECT_EQ(report.removed.size(), 2);
    EXPECT_FALSE(QFileInfo::exists(payload));
    EXPECT_TRUE(QFileInfo::exists(unrelated));
}

} // namespace
