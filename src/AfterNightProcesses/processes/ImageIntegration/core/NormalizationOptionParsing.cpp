// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "NormalizationOptionParsing.h"

namespace ImageIntegration
{
QString normalizeNormalizationToken(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    value.replace(QLatin1Char('-'), QLatin1Char(' '));
    while (value.contains(QStringLiteral("  ")))
    {
        value.replace(QStringLiteral("  "), QStringLiteral(" "));
    }
    return value;
}

LocalNormReferenceStrategy parseLocalNormReferenceStrategy(const QString& value)
{
    const QString normalized = normalizeNormalizationToken(value);

    if (normalized == QStringLiteral("explicit") || normalized == QStringLiteral("explicit reference") ||
        normalized == QStringLiteral("manual") || normalized == QStringLiteral("manual reference"))
    {
        return LocalNormReferenceStrategy::ExplicitReference;
    }

    if (normalized == QStringLiteral("single best") || normalized == QStringLiteral("single best frame") ||
        normalized == QStringLiteral("best frame"))
    {
        return LocalNormReferenceStrategy::SingleBestFrame;
    }

    if (normalized == QStringLiteral("integrate best frames") ||
        normalized == QStringLiteral("integration best frames") ||
        normalized == QStringLiteral("integrated best frames") || normalized == QStringLiteral("best frames") ||
        normalized == QStringLiteral("auto") || normalized == QStringLiteral("automatic"))
    {
        return LocalNormReferenceStrategy::IntegrateBestFrames;
    }

    return LocalNormReferenceStrategy::IntegrateBestFrames;
}

bool isExplicitLocalNormReferenceStrategy(const QString& value)
{
    return parseLocalNormReferenceStrategy(value) == LocalNormReferenceStrategy::ExplicitReference;
}

bool isIntegrateBestFramesLocalNormReferenceStrategy(const QString& value)
{
    return parseLocalNormReferenceStrategy(value) == LocalNormReferenceStrategy::IntegrateBestFrames;
}

LocalNormEvaluationCriteria parseLocalNormEvaluationCriteria(const QString& value)
{
    const QString normalized = normalizeNormalizationToken(value);

    if (normalized == QStringLiteral("psf signal weight") || normalized == QStringLiteral("psf weight"))
    {
        return LocalNormEvaluationCriteria::PsfSignalWeight;
    }

    if (normalized == QStringLiteral("quality score") || normalized == QStringLiteral("quality") ||
        normalized == QStringLiteral("score") || normalized == QStringLiteral("fwhm"))
    {
        return LocalNormEvaluationCriteria::QualityScore;
    }

    if (normalized == QStringLiteral("psf snr") || normalized == QStringLiteral("snr"))
    {
        return LocalNormEvaluationCriteria::PsfSnr;
    }

    if (normalized.contains(QStringLiteral("m*")) || normalized.contains(QStringLiteral("m star")) ||
        normalized.contains(QStringLiteral("background")))
    {
        return LocalNormEvaluationCriteria::MStar;
    }

    if (normalized == QStringLiteral("median"))
    {
        return LocalNormEvaluationCriteria::Median;
    }

    if (normalized == QStringLiteral("star count") || normalized == QStringLiteral("stars"))
    {
        return LocalNormEvaluationCriteria::StarCount;
    }

    return LocalNormEvaluationCriteria::PsfSignalWeight;
}
bool normalizationUsesScaleEstimator(const GlobalNormalizationMethod method) noexcept
{
    return method == GlobalNormalizationMethod::AdditiveScaling ||
           method == GlobalNormalizationMethod::MultiplicativeScaling;
}

} // namespace ImageIntegration
