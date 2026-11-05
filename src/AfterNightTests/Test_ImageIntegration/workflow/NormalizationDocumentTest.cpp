// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The normalization coefficient document and its sidecar: a group's matches published as a pair and read back whole,
/// refused when the inputs, the settings or the payload no longer agree with the sidecar, an older sidecar recomputed
/// and a newer one flagged rather than replaced, what an interrupted publication left removed, and the stack handed the
/// matches of the frames it reads in its own order.

#include "processes/ImageIntegration/core/NormalizationAnalysis.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/StackNormalizationArtifacts.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <optional>
#include <vector>

namespace
{

using ImageIntegration::NormalizationCoefficientMode;
using ImageIntegration::NormalizationCoefficientSet;
using ImageIntegration::NormalizationMeasurementPlane;

[[nodiscard]] an::FrameNormalization matchOf(const double scale, const double offset)
{
    an::Result<an::FrameNormalization> match = an::identityNormalization(an::NormalizationMethod::AdditiveScaling, 1u,
                                                                         an::ScaleEstimator::MedianAbsoluteDeviation);
    EXPECT_TRUE(match.hasValue());
    match->channels[0].scale = scale;
    match->channels[0].offset = offset;
    return *match;
}

[[nodiscard]] NormalizationCoefficientSet globalSet()
{
    NormalizationCoefficientSet set;
    set.mode = NormalizationCoefficientMode::Global;
    set.plane = NormalizationMeasurementPlane::RegisteredCanvas;
    set.channelCount = 1u;
    set.width = 64u;
    set.height = 48u;
    set.referenceIndex = 0u;
    set.logicalFrames = {QStringLiteral("C:/lights/a.fits"), QStringLiteral("C:/lights/b.fits"),
                         QStringLiteral("C:/lights/c.fits")};
    set.sourceFrames = {QStringLiteral("C:/out/a_reg.fits"), QStringLiteral("C:/out/b_reg.fits"),
                        QStringLiteral("C:/out/c_reg.fits")};
    set.global = {matchOf(1.0, 0.0), matchOf(0.5, -0.025), matchOf(2.0, 0.1)};
    return set;
}

[[nodiscard]] NormalizationCoefficientSet localSet()
{
    LocalNormalizationSettings settings;
    settings.enabled = true;
    const an::LocalNormalizationRequest request = ImageIntegration::localNormalizationRequestFor(settings);
    const an::Result<an::LocalNormalizationPlan> plan = an::planLocalNormalization(request, 128u, 96u, 1u);
    EXPECT_TRUE(plan.hasValue());
    const auto values = static_cast<std::size_t>(plan->shape().valueCount());

    NormalizationCoefficientSet set;
    set.mode = NormalizationCoefficientMode::LocalThenGlobal;
    set.plane = NormalizationMeasurementPlane::Frames;
    set.channelCount = 1u;
    set.width = 128u;
    set.height = 96u;
    set.referenceIndex = 1u;
    set.logicalFrames = {QStringLiteral("C:/lights/a.fits"), QStringLiteral("C:/lights/b.fits")};
    set.sourceFrames = set.logicalFrames;
    an::LocalNormalizationDocument document;
    document.request = request;
    document.shape = plan->shape();
    document.validity = an::ValidityRule::AllValid;
    document.referenceIndex = 1u;
    document.referenceBackground.assign(values, 0.1);
    document.frames.resize(2u);
    for (std::uint32_t index = 0; index < 2u; ++index)
    {
        document.frames[index].frameIndex = index;
        document.frames[index].background.assign(values, 0.1 + 0.05 * index);
        document.frames[index].global = matchOf(index == 1u ? 1.0 : 0.8, 0.0);
    }
    document.frames[0].scale[0] = 1.25;
    set.local = std::move(document);
    return set;
}

class NormalizationDocumentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_artifact = QDir(m_root.path()).filePath(QStringLiteral("normalization/group/coefficients.json"));
        ASSERT_TRUE(QDir().mkpath(QFileInfo(m_artifact).absolutePath()));
    }

    void publish(const NormalizationCoefficientSet& set)
    {
        const QString identity = ImageIntegration::normalizationArtifactIdentity(m_inputs, m_settings);
        const QJsonObject sidecar =
            ImageIntegration::normalizationSidecar(m_group, m_inputs, m_settings, QString(), set);
        const auto published = ImageIntegration::publishNormalizationArtifact(m_artifact, set, identity, sidecar);
        ASSERT_TRUE(published.ok()) << published.message.toStdString();
    }

    void rewriteSidecar(const std::function<void(QJsonObject&)>& edit)
    {
        QFile file(ImageIntegration::normalizationSidecarPath(m_artifact));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        QJsonObject sidecar = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        edit(sidecar);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(sidecar).toJson());
    }

    [[nodiscard]] ImageIntegration::NormalizationArtifactReuse check() const
    {
        return ImageIntegration::checkNormalizationArtifact(m_artifact, m_group, m_inputs, m_settings);
    }

    QTemporaryDir m_root;
    QString m_artifact;
    IntegrationGroupKey m_group{GroupKey{QStringLiteral("int|64x48|Lum")}};
    QString m_inputs = QStringLiteral("inputs-fingerprint");
    QString m_settings = QStringLiteral("settings-fingerprint");
};

TEST_F(NormalizationDocumentTest, AGlobalSetIsPublishedAsAPairAndReadBackWhole)
{
    const NormalizationCoefficientSet set = globalSet();
    ASSERT_TRUE(ImageIntegration::checkNormalizationCoefficientSet(set).isEmpty());
    publish(set);

    const auto reuse = check();
    ASSERT_TRUE(reuse.reuse) << reuse.reason.toStdString();
    ASSERT_TRUE(reuse.coefficients.has_value());
    EXPECT_EQ(reuse.coefficients->logicalFrames, set.logicalFrames);
    EXPECT_EQ(reuse.coefficients->sourceFrames, set.sourceFrames);
    ASSERT_EQ(reuse.coefficients->global.size(), 3u);
    EXPECT_DOUBLE_EQ(reuse.coefficients->global[1].channels[0].scale, 0.5);
    EXPECT_DOUBLE_EQ(reuse.coefficients->global[1].channels[0].offset, -0.025);
    EXPECT_EQ(reuse.coefficients->global[2].method, an::NormalizationMethod::AdditiveScaling);

    // The sidecar is what the planner reads: the schema, the family and the payload's digest.
    EXPECT_EQ(reuse.sidecar.value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::schema::kNormalizationArtifactSchemaVersion);
    EXPECT_EQ(reuse.sidecar.value(QStringLiteral("coefficientPayloadSha256")).toString().size(), 64);
}

TEST_F(NormalizationDocumentTest, ALocalDocumentKeepsItsSurfacesScalesAndMatches)
{
    const NormalizationCoefficientSet set = localSet();
    ASSERT_TRUE(ImageIntegration::checkNormalizationCoefficientSet(set).isEmpty())
        << ImageIntegration::checkNormalizationCoefficientSet(set).toStdString();
    publish(set);

    const auto reuse = check();
    ASSERT_TRUE(reuse.reuse) << reuse.reason.toStdString();
    ASSERT_TRUE(reuse.coefficients.has_value());
    ASSERT_TRUE(reuse.coefficients->local.has_value());
    const an::LocalNormalizationDocument& local = *reuse.coefficients->local;
    EXPECT_EQ(local.shape, set.local->shape);
    EXPECT_EQ(local.referenceIndex, std::optional<std::uint32_t>(1u));
    ASSERT_EQ(local.frames.size(), 2u);
    EXPECT_DOUBLE_EQ(local.frames[0].scale[0], 1.25);
    EXPECT_EQ(local.frames[0].background, set.local->frames[0].background);
    ASSERT_TRUE(local.frames[0].global.has_value());
    EXPECT_DOUBLE_EQ(local.frames[0].global->channels[0].scale, 0.8);
}

TEST_F(NormalizationDocumentTest, AnInconsistentSetIsRefusedBeforeItIsWritten)
{
    NormalizationCoefficientSet set = globalSet();
    set.global.pop_back();
    EXPECT_FALSE(ImageIntegration::checkNormalizationCoefficientSet(set).isEmpty());
    const auto published = ImageIntegration::publishNormalizationArtifact(
        m_artifact, set, ImageIntegration::normalizationArtifactIdentity(m_inputs, m_settings),
        ImageIntegration::normalizationSidecar(m_group, m_inputs, m_settings, QString(), set));
    EXPECT_FALSE(published.ok());
    EXPECT_FALSE(QFileInfo::exists(m_artifact));

    NormalizationCoefficientSet drizzle = globalSet();
    drizzle.plane = NormalizationMeasurementPlane::DrizzleAnalysisPlane;
    EXPECT_FALSE(ImageIntegration::checkNormalizationCoefficientSet(drizzle).isEmpty());
}

TEST_F(NormalizationDocumentTest, ChangedInputsOrSettingsAreNotReused)
{
    publish(globalSet());
    EXPECT_FALSE(
        ImageIntegration::checkNormalizationArtifact(m_artifact, m_group, QStringLiteral("other"), m_settings).reuse);
    EXPECT_FALSE(
        ImageIntegration::checkNormalizationArtifact(m_artifact, m_group, m_inputs, QStringLiteral("other")).reuse);
    EXPECT_FALSE(ImageIntegration::checkNormalizationArtifact(
                     m_artifact, IntegrationGroupKey{GroupKey{QStringLiteral("int|other")}}, m_inputs, m_settings)
                     .reuse);
}

TEST_F(NormalizationDocumentTest, ACorruptPayloadIsNotReused)
{
    publish(globalSet());
    QFile file(m_artifact);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    QByteArray bytes = file.readAll();
    bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x5a);
    file.seek(0);
    file.write(bytes);
    file.close();

    const auto reuse = check();
    EXPECT_FALSE(reuse.reuse);
    EXPECT_FALSE(reuse.newerSchema);
}

TEST_F(NormalizationDocumentTest, AnOlderSidecarIsRecomputedAndANewerOneIsFlagged)
{
    publish(globalSet());
    rewriteSidecar([](QJsonObject& sidecar) { sidecar.insert(QStringLiteral("schemaVersion"), 5); });
    auto reuse = check();
    EXPECT_FALSE(reuse.reuse);
    EXPECT_FALSE(reuse.newerSchema);

    rewriteSidecar([](QJsonObject& sidecar) {
        sidecar.insert(QStringLiteral("schemaVersion"),
                       ImageIntegration::schema::kNormalizationArtifactSchemaVersion + 1);
    });
    reuse = check();
    EXPECT_FALSE(reuse.reuse);
    EXPECT_TRUE(reuse.newerSchema);
}

TEST_F(NormalizationDocumentTest, APayloadWithoutAnEnvelopeIsNotRead)
{
    publish(globalSet());
    QFile file(m_artifact);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"schemaVersion":5,"frames":[]})");
    file.close();
    const auto read = ImageIntegration::readNormalizationCoefficients(m_artifact, std::nullopt);
    EXPECT_FALSE(read.coefficients.has_value());
    EXPECT_NE(read.failure, ImageIntegration::NormalizationDocumentFailure::None);
    EXPECT_FALSE(check().reuse);
}

TEST_F(NormalizationDocumentTest, RecoveryRemovesWhatAnInterruptedPublicationLeftAndKeepsThePair)
{
    publish(globalSet());
    const QString name = QFileInfo(m_artifact).fileName();
    const QDir directory = QFileInfo(m_artifact).dir();
    const QStringList leftovers{name + QStringLiteral(".pending.1234"), name + QStringLiteral(".rollback.1234"),
                                name + QStringLiteral(".publish.lock")};
    for (const QString& leftover : leftovers)
    {
        QFile file(directory.filePath(leftover));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    (void)ImageIntegration::recoverNormalizationArtifact(m_artifact);
    for (const QString& leftover : leftovers)
    {
        EXPECT_FALSE(QFileInfo::exists(directory.filePath(leftover))) << leftover.toStdString();
    }
    EXPECT_TRUE(check().reuse);
}

TEST_F(NormalizationDocumentTest, TheStackIsHandedTheMatchesOfTheFramesItReadsInItsOrder)
{
    const NormalizationCoefficientSet set = globalSet();
    ImageIntegration::StackNormalizationExpectation expectation;
    expectation.logicalFrames = {set.logicalFrames[2], set.logicalFrames[0]};
    expectation.plane = NormalizationMeasurementPlane::RegisteredCanvas;
    expectation.channelCount = 1u;
    expectation.width = 64u;
    expectation.height = 48u;
    QString reason;
    const auto inputs = ImageIntegration::stackNormalizationInputsFor(set, expectation, reason);
    ASSERT_TRUE(inputs.has_value()) << reason.toStdString();
    ASSERT_EQ(inputs->global.size(), 2u);
    EXPECT_DOUBLE_EQ(inputs->global[0].channels[0].scale, 2.0);
    EXPECT_DOUBLE_EQ(inputs->global[1].channels[0].scale, 1.0);

    expectation.logicalFrames.push_back(QStringLiteral("C:/lights/unmeasured.fits"));
    EXPECT_FALSE(ImageIntegration::stackNormalizationInputsFor(set, expectation, reason).has_value());
    EXPECT_FALSE(reason.isEmpty());

    expectation.logicalFrames.pop_back();
    expectation.plane = NormalizationMeasurementPlane::Frames;
    EXPECT_FALSE(ImageIntegration::stackNormalizationInputsFor(set, expectation, reason).has_value());
}

TEST_F(NormalizationDocumentTest, AStackOverASubsetOfALocalDocumentIsRenumbered)
{
    const NormalizationCoefficientSet set = localSet();
    ImageIntegration::StackNormalizationExpectation expectation;
    expectation.logicalFrames = {set.logicalFrames[1], set.logicalFrames[0]};
    expectation.plane = NormalizationMeasurementPlane::Frames;
    expectation.channelCount = 1u;
    expectation.width = 128u;
    expectation.height = 96u;
    QString reason;
    const auto inputs = ImageIntegration::stackNormalizationInputsFor(set, expectation, reason);
    ASSERT_TRUE(inputs.has_value()) << reason.toStdString();
    ASSERT_TRUE(inputs->local.has_value());
    ASSERT_EQ(inputs->local->frames.size(), 2u);
    EXPECT_EQ(inputs->local->frames[0].frameIndex, 0u);
    EXPECT_EQ(inputs->local->frames[1].frameIndex, 1u);
    EXPECT_DOUBLE_EQ(inputs->local->frames[1].scale[0], 1.25);
    EXPECT_EQ(inputs->local->referenceIndex, std::optional<std::uint32_t>(0u));
}

// The engine applies a local document over the frame it names as the stack's reference, so the stack takes the
// document's reference as its own when it reads that light, whichever light the stack would have taken.
TEST_F(NormalizationDocumentTest, TheStackTakesTheReferenceALocalDocumentNamesAmongTheFramesItReads)
{
    const NormalizationCoefficientSet set = localSet();
    ImageIntegration::StackNormalizationExpectation expectation;
    expectation.logicalFrames = set.logicalFrames;
    expectation.referenceFrame = 0u;
    expectation.plane = NormalizationMeasurementPlane::Frames;
    expectation.channelCount = 1u;
    expectation.width = 128u;
    expectation.height = 96u;
    QString reason;
    const auto inputs = ImageIntegration::stackNormalizationInputsFor(set, expectation, reason);
    ASSERT_TRUE(inputs.has_value()) << reason.toStdString();
    ASSERT_TRUE(inputs->local.has_value());
    EXPECT_EQ(inputs->local->referenceIndex, std::optional<std::uint32_t>(1u));
    EXPECT_EQ(inputs->referenceFrame, 1u);
    ASSERT_TRUE(inputs->local->frames[0].global.has_value());
    EXPECT_DOUBLE_EQ(inputs->local->frames[0].global->channels[0].scale, 0.8);
}

// A document that names no light the stack reads - several lights integrated, or a reference the stack left out - is
// applied over the stack's own reference, which the engine matches by the identity after its surface: each light's
// match is restated against that light's, so the reference keeps its level and every other light is brought to it.
// The stack handed the engine the document's matches against the integrated reference, and the engine refused them.
TEST_F(NormalizationDocumentTest, ADocumentNamingNoReferenceTheStackReadsIsRestatedAgainstTheStacksReference)
{
    NormalizationCoefficientSet set = localSet();
    set.local->referenceIndex.reset();
    set.local->frames[0].global = matchOf(0.8, 0.02);
    set.local->frames[1].global = matchOf(1.25, -0.01);
    ImageIntegration::StackNormalizationExpectation expectation;
    expectation.logicalFrames = set.logicalFrames;
    expectation.referenceFrame = 1u;
    expectation.plane = NormalizationMeasurementPlane::Frames;
    expectation.channelCount = 1u;
    expectation.width = 128u;
    expectation.height = 96u;
    QString reason;
    const auto integrated = ImageIntegration::stackNormalizationInputsFor(set, expectation, reason);
    ASSERT_TRUE(integrated.has_value()) << reason.toStdString();
    ASSERT_TRUE(integrated->local.has_value());
    EXPECT_FALSE(integrated->local->referenceIndex.has_value());
    EXPECT_EQ(integrated->referenceFrame, 1u);
    ASSERT_TRUE(integrated->local->frames[1].global.has_value());
    EXPECT_EQ(integrated->local->frames[1].global->channels[0].scale, 1.0);
    EXPECT_EQ(integrated->local->frames[1].global->channels[0].offset, 0.0);
    // The first light's match followed by the reference's is the match the document measured.
    ASSERT_TRUE(integrated->local->frames[0].global.has_value());
    const an::NormalizationChannelCoefficients& restated = integrated->local->frames[0].global->channels[0];
    EXPECT_DOUBLE_EQ(restated.scale, 0.8 / 1.25);
    EXPECT_DOUBLE_EQ(restated.offset, (0.02 + 0.01) / 1.25);
    EXPECT_DOUBLE_EQ(restated.scale * 1.25, 0.8);
    EXPECT_DOUBLE_EQ(restated.offset * 1.25 - 0.01, 0.02);

    // A reference the stack does not read is no reference of its stack either.
    const NormalizationCoefficientSet named = localSet();
    expectation.logicalFrames = {named.logicalFrames[0]};
    expectation.referenceFrame = 0u;
    const auto withoutItsReference = ImageIntegration::stackNormalizationInputsFor(named, expectation, reason);
    ASSERT_TRUE(withoutItsReference.has_value()) << reason.toStdString();
    ASSERT_TRUE(withoutItsReference->local.has_value());
    EXPECT_FALSE(withoutItsReference->local->referenceIndex.has_value());
    EXPECT_EQ(withoutItsReference->referenceFrame, 0u);
    ASSERT_TRUE(withoutItsReference->local->frames[0].global.has_value());
    EXPECT_EQ(withoutItsReference->local->frames[0].global->channels[0].scale, 1.0);
    EXPECT_EQ(withoutItsReference->local->frames[0].global->channels[0].offset, 0.0);
}

// A drizzled group's coefficients are measured at its lights' level on the canvas's plane before the scale, and the
// stack restates every drizzled frame at that level, so they apply at any scale: only the plane's origin and extent
// must be the stack's. A group whose scale alone changed kept coefficients both fingerprints called current, and the
// stack refused them on every run as measured on another canvas.
TEST_F(NormalizationDocumentTest, ADrizzledGroupsCoefficientsApplyAtAnyScaleOfTheirAnalysisPlane)
{
    NormalizationCoefficientSet set = globalSet();
    set.plane = NormalizationMeasurementPlane::DrizzleAnalysisPlane;
    set.drizzlePlane = ImageIntegration::DrizzleAnalysisPlaneRecord{-3.5, 2.0, 64u, 48u, 2.0};
    ImageIntegration::StackNormalizationExpectation expectation;
    expectation.logicalFrames = set.logicalFrames;
    expectation.plane = NormalizationMeasurementPlane::DrizzleAnalysisPlane;
    expectation.channelCount = 1u;
    expectation.width = 64u;
    expectation.height = 48u;
    expectation.drizzlePlane = ImageIntegration::DrizzleAnalysisPlaneRecord{-3.5, 2.0, 64u, 48u, 3.0};
    QString reason;
    const auto inputs = ImageIntegration::stackNormalizationInputsFor(set, expectation, reason);
    ASSERT_TRUE(inputs.has_value()) << reason.toStdString();
    EXPECT_EQ(inputs->global.size(), 3u);

    expectation.drizzlePlane->originX = -4.5;
    EXPECT_FALSE(ImageIntegration::stackNormalizationInputsFor(set, expectation, reason).has_value());
    EXPECT_FALSE(reason.isEmpty());
}

TEST(NormalizationFluxScaleTest, ALightsScaleIsTheMedianRatioOfItsPairedStarsFittedVolumes)
{
    const auto starAt = [](const double x, const double y, const double amplitude) {
        an::DetectedStar star;
        star.positionX = x;
        star.positionY = y;
        star.psf.amplitude = amplitude;
        star.psf.scaleX = 1.5;
        star.psf.scaleY = 1.5;
        star.signalToNoise = 50.0;
        return star;
    };
    ImageIntegration::LocalNormalizationStars reference;
    ImageIntegration::LocalNormalizationStars light;
    reference.medianFullWidth = 3.0;
    light.medianFullWidth = 3.0;
    for (int index = 0; index < 20; ++index)
    {
        const double x = 10.0 + 9.0 * index;
        const double y = 20.0 + 4.0 * (index % 5);
        reference.stars.push_back(starAt(x, y, 0.4));
        // Each light star half as bright, a fraction of a pixel away.
        light.stars.push_back(starAt(x + 0.3, y - 0.2, 0.2));
    }
    const std::optional<double> scale = ImageIntegration::localNormalizationFluxScale(reference, light);
    ASSERT_TRUE(scale.has_value());
    EXPECT_NEAR(*scale, 2.0, 1e-9);

    // Too few pairs leave the residual ratio to decide.
    light.stars.resize(10u);
    EXPECT_FALSE(ImageIntegration::localNormalizationFluxScale(reference, light).has_value());
}

TEST(NormalizationSettingsFingerprintTest, TheLocalNormalizationConstantsEnterTheSettingsFingerprint)
{
    // The region extent, its overlap and the scale's sample count are no setting a person changes, but they decide
    // the grids: the request states them, and the request is what the settings fingerprint hashes.
    LocalNormalizationSettings settings;
    settings.enabled = true;
    const an::LocalNormalizationRequest request = ImageIntegration::localNormalizationRequestFor(settings);
    const QJsonObject stated = ImageIntegration::localNormalizationRequestJson(request);
    EXPECT_EQ(stated.value(QStringLiteral("backgroundMinRoiExtent")).toInteger(), 32);
    EXPECT_DOUBLE_EQ(stated.value(QStringLiteral("backgroundRoiOverlap")).toDouble(), 0.5);
    EXPECT_EQ(stated.value(QStringLiteral("scaleSamples")).toInteger(), 8192);

    an::LocalNormalizationRequest moved = request;
    moved.backgroundMinRoiExtent = 64u;
    EXPECT_NE(ImageIntegration::localNormalizationRequestJson(moved), stated);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|64x48|Lum")}};
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization = settings;
    const std::vector<FrameId> references{FrameId(QStringLiteral("C:/lights/a.fits"))};
    const QString local = computeNormalizationSettingsFingerprint(group, references);
    WorkflowManifest::IntegrationGroup smoothed = group;
    smoothed.stacking.localNormalization.backgroundSmoothingRadius += 1;
    EXPECT_NE(computeNormalizationSettingsFingerprint(smoothed, references), local);
    EXPECT_EQ(computeNormalizationSettingsFingerprint(group, references), local);
}

// A Local group's global match is Additive With Scaling whatever global method the group stores, so the stored method
// is no part of its coefficients' fingerprint, and changing it measures nothing again; a Global group's method is.
TEST(NormalizationSettingsFingerprintTest, ALocalGroupsFingerprintHoldsNoGlobalMethodItDoesNotRun)
{
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|64x48|Lum")}};
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    const std::vector<FrameId> references{FrameId(QStringLiteral("C:/lights/a.fits"))};
    WorkflowManifest::IntegrationGroup multiplicative = group;
    multiplicative.stacking.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
    EXPECT_EQ(computeNormalizationSettingsFingerprint(multiplicative, references),
              computeNormalizationSettingsFingerprint(group, references));

    WorkflowManifest::IntegrationGroup scaled = group;
    scaled.stacking.scaleEstimator = ScaleEstimatorMethod::MAD;
    EXPECT_NE(computeNormalizationSettingsFingerprint(scaled, references),
              computeNormalizationSettingsFingerprint(group, references))
        << "the estimator scales local normalization's global match";

    group.stacking.normalizationMode = NormalizationMode::Global;
    group.stacking.localNormalization.enabled = false;
    multiplicative.stacking.normalizationMode = NormalizationMode::Global;
    multiplicative.stacking.localNormalization.enabled = false;
    EXPECT_NE(computeNormalizationSettingsFingerprint(multiplicative, references),
              computeNormalizationSettingsFingerprint(group, references));
}

} // namespace
