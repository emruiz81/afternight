// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QString>

namespace ImageIntegration
{
enum class LocalNormReferenceStrategy
{
    ExplicitReference,
    SingleBestFrame,
    IntegrateBestFrames
};

enum class LocalNormEvaluationCriteria
{
    PsfSignalWeight,
    QualityScore,
    PsfSnr,
    MStar,
    Median,
    StarCount
};

[[nodiscard]] QString normalizeNormalizationToken(QString value);

[[nodiscard]] AFTERNIGHTPROCESSES_API LocalNormReferenceStrategy parseLocalNormReferenceStrategy(const QString& value);

[[nodiscard]] bool isExplicitLocalNormReferenceStrategy(const QString& value);

[[nodiscard]] bool isIntegrateBestFramesLocalNormReferenceStrategy(const QString& value);

[[nodiscard]] AFTERNIGHTPROCESSES_API LocalNormEvaluationCriteria
parseLocalNormEvaluationCriteria(const QString& value);

/// Whether a global normalization method measures a scale, and so uses the scale estimator.
[[nodiscard]] bool normalizationUsesScaleEstimator(GlobalNormalizationMethod method) noexcept;
} // namespace ImageIntegration
