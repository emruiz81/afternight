// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The normalization dialog's texts: the reference strategy and evaluation aliases the sessions store, and the local
/// request the stored local settings resolve to - each choice read through its token, an unknown one the default.

#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"

#include <gtest/gtest.h>

TEST(NormalizationOptionParsing, SupportsIntegrationBestFramesAlias)
{
    using ImageIntegration::LocalNormReferenceStrategy;
    using ImageIntegration::parseLocalNormReferenceStrategy;

    EXPECT_EQ(parseLocalNormReferenceStrategy(QStringLiteral("Integrate Best Frames")),
              LocalNormReferenceStrategy::IntegrateBestFrames);
    EXPECT_EQ(parseLocalNormReferenceStrategy(QStringLiteral("Integration Best Frames")),
              LocalNormReferenceStrategy::IntegrateBestFrames);
    EXPECT_EQ(parseLocalNormReferenceStrategy(QStringLiteral("Best Frames")),
              LocalNormReferenceStrategy::IntegrateBestFrames);
}

TEST(NormalizationOptionParsing, SupportsEvaluationCriteriaAliases)
{
    using ImageIntegration::LocalNormEvaluationCriteria;
    using ImageIntegration::parseLocalNormEvaluationCriteria;

    EXPECT_EQ(parseLocalNormEvaluationCriteria(QStringLiteral("PSF Signal Weight")),
              LocalNormEvaluationCriteria::PsfSignalWeight);
    EXPECT_EQ(parseLocalNormEvaluationCriteria(QStringLiteral("Quality Score")),
              LocalNormEvaluationCriteria::QualityScore);
    EXPECT_EQ(parseLocalNormEvaluationCriteria(QStringLiteral("FWHM")), LocalNormEvaluationCriteria::QualityScore);
    EXPECT_EQ(parseLocalNormEvaluationCriteria(QStringLiteral("Star Count")), LocalNormEvaluationCriteria::StarCount);
}

TEST(NormalizationOptionParsing, TheLocalRequestReadsEachStoredChoice)
{
    LocalNormalizationSettings settings;
    settings.enabled = true;
    settings.gridSize = 6;
    settings.gridInterpolation = QStringLiteral("Bilinear");
    settings.backgroundEstimator = QStringLiteral("Percentile");
    settings.scaleMode = QStringLiteral("Per Channel");

    const an::LocalNormalizationRequest request = ImageIntegration::localNormalizationRequestFor(settings);
    EXPECT_EQ(request.gridSize, 6u);
    EXPECT_EQ(request.interpolation, an::LocalGridInterpolation::Bilinear);
    EXPECT_EQ(request.estimator, an::LocalBackgroundEstimator::Percentile);
    EXPECT_EQ(request.scaleMode, an::LocalScaleMode::PerChannel);

    settings.gridInterpolation = QStringLiteral("Something Else");
    settings.backgroundEstimator = QStringLiteral("Sigma Clipped Mode");
    settings.scaleMode = QStringLiteral("Linked RGB");
    const an::LocalNormalizationRequest fallback = ImageIntegration::localNormalizationRequestFor(settings);
    EXPECT_EQ(fallback.interpolation, an::LocalGridInterpolation::Bicubic);
    EXPECT_EQ(fallback.estimator, an::LocalBackgroundEstimator::SigmaClippedMode);
    EXPECT_EQ(fallback.scaleMode, an::LocalScaleMode::Linked);
}
