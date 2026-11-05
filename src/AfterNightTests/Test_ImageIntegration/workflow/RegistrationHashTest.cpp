// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The registration hash: every setting that changes what a registration computes moves it - the reference, the
/// method, the model, the solver, the catalog enrichment, the saved comet observations a fit uses, the control points
/// and the chromatic correction - and a setting that only the stack reads does not. And the chromatic correction's
/// settings travelling through the manifest's own record.

#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/workflow/ManifestDiff.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QPointF>

#include <gtest/gtest.h>

namespace
{

using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

TEST(RegistrationHashUtils, TransformHashChangesWhenAutoReferenceOrReferenceImageChanges)
{
    RegistrationSettings settings;

    const QString h1 = registrationTransformHash(settings);
    ASSERT_FALSE(h1.isEmpty());

    settings.autoReferenceDuringRegistration = !settings.autoReferenceDuringRegistration;
    const QString h2 = registrationTransformHash(settings);
    EXPECT_NE(h1, h2);

    settings.autoReferenceDuringRegistration = !settings.autoReferenceDuringRegistration;
    settings.referenceImage = QStringLiteral("C:/dummy/reference.fit");
    const QString h3 = registrationTransformHash(settings);
    EXPECT_NE(h1, h3);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenAutoSolveUnsolvedFramesChanges)
{
    RegistrationSettings settings;

    const QString disabled_hash = registrationTransformHash(settings);
    ASSERT_FALSE(disabled_hash.isEmpty());

    settings.autoSolveUnsolvedFrames = true;
    const QString enabled_hash = registrationTransformHash(settings);
    EXPECT_NE(disabled_hash, enabled_hash);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenGaiaEnrichmentChanges)
{
    RegistrationSettings settings;

    const QString enabled_hash = registrationTransformHash(settings);
    ASSERT_FALSE(enabled_hash.isEmpty());

    settings.enrichWithGaiaCatalog = false;
    const QString disabled_hash = registrationTransformHash(settings);
    EXPECT_NE(enabled_hash, disabled_hash);
}

// The most stars a frame is detected with bounds the detection, so it moves the hash the cached detections are read
// under; a method that detects no stars is not moved by it.
TEST(RegistrationHashUtils, DetectionHashChangesWhenTheStarCapChanges)
{
    RegistrationSettings settings;
    const QString capped = registrationDetectionHash(settings);
    settings.maxStarPairs += 500;
    EXPECT_NE(registrationDetectionHash(settings), capped);

    settings.registrationMethod = QStringLiteral("phase_correlation");
    const QString correlated = registrationDetectionHash(settings);
    settings.maxStarPairs = 0;
    EXPECT_EQ(registrationDetectionHash(settings), correlated);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenTransformContractFieldsChange)
{
    RegistrationSettings settings;

    const QString h1 = registrationTransformHash(settings);
    ASSERT_FALSE(h1.isEmpty());

    settings.transformMode = QStringLiteral("linear");
    const QString h2 = registrationTransformHash(settings);
    EXPECT_NE(h1, h2);

    settings.transformMode = QStringLiteral("polynomial");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = true;
    const QString h3 = registrationTransformHash(settings);
    EXPECT_NE(h1, h3);

    settings.transformMode = QStringLiteral("Auto");
    settings.transformOrder = 3;
    settings.twoPassRefinementEnabled = false;
    const QString h4 = registrationTransformHash(settings);
    EXPECT_NE(h1, h4);

    // Two-pass refinement follows the mode and is read by no run, so it changes no transform.
    settings.transformOrder = 0;
    settings.twoPassRefinementEnabled = true;
    const QString h5 = registrationTransformHash(settings);
    EXPECT_EQ(h1, h5);
}

// Two-pass refinement is persisted and reported but read by no run - the mode decides - so it moves neither the
// registration's fingerprints nor the diff, and the manifest's reader takes it from the mode as the model's does.
TEST(RegistrationHashUtils, TwoPassRefinementFollowsTheModeAndMovesNoFingerprint)
{
    RegistrationSettings refined;
    refined.registrationMethod = QStringLiteral("star_matching");
    refined.transformMode = QStringLiteral("polynomial");
    refined.transformOrder = 2;
    refined.twoPassRefinementEnabled = true;
    RegistrationSettings flagless = refined;
    flagless.twoPassRefinementEnabled = false;

    WorkflowManifest previous;
    previous.outputDirectory = QStringLiteral("D:/Output");
    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.lightFiles = {FrameId(QStringLiteral("/tmp/two_pass_a.fit"))};
    registration.settings = refined;
    previous.registrationGroups.push_back(registration);
    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|M42|L")}};
    integration.outputFilename = QStringLiteral("stacked_M42_L.fit");
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    previous.integrationGroups.push_back(integration);
    WorkflowManifest current = previous;
    current.registrationGroups.front().settings = flagless;
    EXPECT_EQ(computeIntegrationGroupHash(previous, previous.integrationGroups.front()),
              computeIntegrationGroupHash(current, current.integrationGroups.front()));
    const ManifestDiff diff = compareManifests(previous, current);
    ASSERT_EQ(diff.registrationChanges.size(), 1u);
    EXPECT_FALSE(diff.registrationChanges.front().settingsChanged);

    WorkflowManifest mixed = previous;
    mixed.registrationGroups.front().settings.transformMode = QStringLiteral("linear");
    mixed.registrationGroups.front().settings.transformOrder = 3;
    const WorkflowManifest read = WorkflowManifest::fromJson(mixed.toJson());
    ASSERT_EQ(read.registrationGroups.size(), 1u);
    EXPECT_EQ(read.registrationGroups.front().settings.transformOrder, 0);
    EXPECT_FALSE(read.registrationGroups.front().settings.twoPassRefinementEnabled);
    const WorkflowManifest polynomial = WorkflowManifest::fromJson(current.toJson());
    EXPECT_EQ(polynomial.registrationGroups.front().settings.transformOrder, 2);
    EXPECT_TRUE(polynomial.registrationGroups.front().settings.twoPassRefinementEnabled);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenRegistrationMethodChanges)
{
    RegistrationSettings settings;

    const QString h1 = registrationTransformHash(settings);
    ASSERT_FALSE(h1.isEmpty());

    settings.registrationMethod = QStringLiteral("phase_correlation");
    const QString h2 = registrationTransformHash(settings);
    EXPECT_NE(h1, h2);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenCometTrackingSettingsChange)
{
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    RegistrationSettings::CometTrackingSettings comet_settings;
    comet_settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/b.fit"), QPointF(20.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/disabled.fit"), QPointF(1.0, 2.0),
                                                       RegistrationSettings::CometObservationOrigin::Imported, false,
                                                       4.0, QStringLiteral("visible but excluded")}};
    settings.cometTracking = comet_settings;

    const QString h1 = registrationTransformHash(settings);
    ASSERT_FALSE(h1.isEmpty());

    settings.cometTracking->observations[0].sourcePosition.setX(21.0);
    const QString h2 = registrationTransformHash(settings);
    EXPECT_NE(h1, h2);

    settings.cometTracking->observations[0].sourcePosition.setX(20.0);
    settings.cometTracking->observations[2].note = QStringLiteral("disabled edit");
    settings.cometTracking->observations[2].sourcePosition.setX(99.0);
    const QString disabled_edit_hash = registrationTransformHash(settings);
    EXPECT_EQ(h1, disabled_edit_hash);

    settings.cometTracking->observations[2].enabled = true;
    const QString enabled_hash = registrationTransformHash(settings);
    EXPECT_NE(h1, enabled_hash);

    settings.cometTracking->observations[2].enabled = false;
    settings.cometTracking->trackingOptions.searchRadiusPixels = 64.0;
    const QString tracking_option_hash = registrationTransformHash(settings);
    EXPECT_NE(h1, tracking_option_hash);

    settings.cometTracking->trackingOptions.searchRadiusPixels = comet_settings.trackingOptions.searchRadiusPixels;
    settings.cometTracking->fitOptions.outlierSigma = 2.0;
    const QString fit_option_hash = registrationTransformHash(settings);
    EXPECT_NE(h1, fit_option_hash);
}

TEST(RegistrationHashUtils, CometTransformHashIncludesResolvedReferenceContext)
{
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.autoReferenceDuringRegistration = true;
    RegistrationSettings::CometTrackingSettings comet_settings;
    comet_settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/b.fit"), QPointF(20.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    settings.cometTracking = comet_settings;

    const QString ref_a_hash = registrationTransformHash(settings, QStringLiteral("C:/dummy/ref_a.fit"));
    const QString ref_b_hash = registrationTransformHash(settings, QStringLiteral("C:/dummy/ref_b.fit"));
    ASSERT_FALSE(ref_a_hash.isEmpty());
    EXPECT_NE(ref_a_hash, ref_b_hash);
    EXPECT_NE(registrationTransformHash(settings), ref_a_hash);
}

TEST(RegistrationHashUtils, CometStackingModeOnlyChangesStackingHash)
{
    RegistrationSettings registration_settings;
    registration_settings.registrationMethod = QStringLiteral("comet_tracking");
    RegistrationSettings::CometTrackingSettings comet_settings;
    comet_settings.observations = {
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/a.fit"), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{QStringLiteral("C:/dummy/b.fit"), QPointF(20.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    registration_settings.cometTracking = comet_settings;

    const QString registration_hash = registrationTransformHash(registration_settings);
    StackingSettings stacking_settings;
    const QString stars_hash = computeStackingSettingsHash(stacking_settings);
    stacking_settings.cometStackingMode = QStringLiteral("Comet");
    const QString comet_hash = computeStackingSettingsHash(stacking_settings);

    EXPECT_EQ(registration_hash, registrationTransformHash(registration_settings));
    EXPECT_NE(stars_hash, comet_hash);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenManualControlPointsChange)
{
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("manual_points");
    settings.referenceImage = QStringLiteral("C:/dummy/reference.fit");
    settings.manualControlPointsByFrame.insert(
        QStringLiteral("C:/dummy/target.fit"),
        RegistrationSettings::ManualControlPointList{{QPointF(14.0, 14.0), QPointF(12.0, 12.0)},
                                                     {QPointF(22.0, 16.0), QPointF(20.0, 14.0)}});

    const QString h1 = registrationTransformHash(settings);
    ASSERT_FALSE(h1.isEmpty());

    settings.manualControlPointsByFrame[QStringLiteral("C:/dummy/target.fit")][0].sourcePoint.setX(15.0);
    const QString h2 = registrationTransformHash(settings);
    EXPECT_NE(h1, h2);
}

TEST(RegistrationHashUtils, TransformHashChangesWhenChromaticAberrationSettingsChange)
{
    RegistrationSettings settings;

    const QString disabled_hash = registrationTransformHash(settings);
    ASSERT_FALSE(disabled_hash.isEmpty());

    settings.chromaticAberration.enabled = true;
    const QString enabled_hash = registrationTransformHash(settings);
    EXPECT_NE(disabled_hash, enabled_hash);

    settings.chromaticAberration.edgeFraction = 0.70;
    const QString edge_hash = registrationTransformHash(settings);
    EXPECT_NE(enabled_hash, edge_hash);

    settings.chromaticAberration.transformType = QStringLiteral("Affine");
    const QString transform_hash = registrationTransformHash(settings);
    EXPECT_NE(edge_hash, transform_hash);
}

TEST(WorkflowManifest, RegistrationChromaticAberrationSettingsRoundTrip)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/afternight_manifest_ca");

    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{QStringLiteral("reg|ca|roundtrip")};
    group.lightFiles = {FrameId(QStringLiteral("/tmp/a.fit")), FrameId(QStringLiteral("/tmp/b.fit"))};
    group.referenceFrame = FrameId(QStringLiteral("/tmp/a.fit"));
    group.settings.chromaticAberration.enabled = true;
    group.settings.chromaticAberration.edgeFraction = 0.65;
    group.settings.chromaticAberration.transformType = QStringLiteral("Affine");
    group.settings.chromaticAberration.minMatchedStars = 9;
    group.settings.chromaticAberration.interpolation = QStringLiteral("Bicubic");
    manifest.registrationGroups.push_back(group);

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.registrationGroups.size(), 1u);

    const auto& ca = round_trip.registrationGroups.front().settings.chromaticAberration;
    EXPECT_TRUE(ca.enabled);
    EXPECT_DOUBLE_EQ(ca.edgeFraction, 0.65);
    EXPECT_EQ(ca.transformType, QStringLiteral("Affine"));
    EXPECT_EQ(ca.minMatchedStars, 9);
    EXPECT_EQ(ca.interpolation, QStringLiteral("Bicubic"));
}

} // namespace
