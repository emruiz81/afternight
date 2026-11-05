// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>

#include <algorithm>
#include <cmath>

namespace ImageIntegration::RegistrationSettingsUtils
{
/// The largest star search radius a registration runs with, in pixels: the detector refuses a larger one.
inline constexpr int kLargestStarSearchRadius = 32;

/// The smallest roundness threshold a registration runs with: the detector needs a positive one.
inline constexpr double kSmallestStarRoundness = 0.01;

/// The smallest Moffat exponent a registration runs with: the detector needs a positive one.
inline constexpr double kSmallestMoffatBeta = 0.05;

/// The largest nucleus search radius a comet is tracked with, in pixels: the engine's centre refinement measures no
/// larger region.
inline constexpr int kLargestCometSearchRadius = 64;

/// The smallest multiple of a comet track's residual beyond which its fit rejects an observation.
inline constexpr double kSmallestCometOutlierSigma = 1.0;

/// The whole pixels a stored nucleus search radius is tracked with: rounded, and held between one and the largest the
/// centre refinement measures.
[[nodiscard]] inline int effectiveCometSearchRadius(const double searchRadiusPixels)
{
    return static_cast<int>(std::clamp<long>(std::lround(searchRadiusPixels), 1L, kLargestCometSearchRadius));
}

/// The track model a stored comet fit option names: "linear" holds the comet to a constant velocity, "quadratic" - also
/// spelled "quadratic-preferred" and "prefer_quadratic" - prefers an acceleration, and any other text lets the fit
/// choose, "automatic".
[[nodiscard]] inline QString cometFitModelKey(const QString& model)
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("linear"))
    {
        return normalized;
    }
    if (normalized == QStringLiteral("quadratic") || normalized == QStringLiteral("quadratic-preferred") ||
        normalized == QStringLiteral("prefer_quadratic"))
    {
        return QStringLiteral("quadratic");
    }
    return QStringLiteral("automatic");
}

/// Whether the integration's registration may send a light to the configured plate solver - a light its method could
/// not register, or the reference it is placed against, when its own cards state no solution: when a solver is
/// configured, and its group asks for unsolved frames to be solved or is aligned onto the mosaic canvas, where a panel
/// sharing too few stars with the reference is placed by the sky. The standalone registration solves a frame only
/// when it is asked.
///
/// @param autoSolveUnsolvedFrames whether the group asks for unsolved frames to be solved.
/// @param mosaicCanvas whether the group is aligned onto the mosaic canvas.
/// @param solverConfigured whether any plate solver is configured (`an::ui::hasAnyConfiguredPlateSolveSolver`).
/// @return whether the plate solver may be called.
[[nodiscard]] inline bool allowsPlateSolveDuringRegistration(const bool autoSolveUnsolvedFrames,
                                                             const bool mosaicCanvas, const bool solverConfigured)
{
    return solverConfigured && (autoSolveUnsolvedFrames || mosaicCanvas);
}

[[nodiscard]] inline bool productionCometTrackingEnabled()
{
    return true;
}

[[nodiscard]] inline bool productionPureCometTrackingEnabled()
{
    return true;
}

[[nodiscard]] inline QString cometTrackingTemporarilyDisabledMessage()
{
    return QStringLiteral("Comet tracking requires accepted observations, finite timestamps, and an explicit "
                          "reference frame.");
}

[[nodiscard]] inline QString normalizeRegistrationMethod(QString value)
{
    value = value.trimmed().toLower();
    if (value.isEmpty() || value == QStringLiteral("star") || value == QStringLiteral("stars") ||
        value == QStringLiteral("star matching"))
    {
        return QStringLiteral("star_matching");
    }
    if (value == QStringLiteral("phase correlation") || value == QStringLiteral("phase_correlation"))
    {
        return QStringLiteral("phase_correlation");
    }
    if (value == QStringLiteral("manual") || value == QStringLiteral("manual points") ||
        value == QStringLiteral("manual point") || value == QStringLiteral("manual n-point") ||
        value == QStringLiteral("manual_n_point") || value == QStringLiteral("manual_points"))
    {
        return QStringLiteral("manual_points");
    }
    if (value == QStringLiteral("comet") || value == QStringLiteral("comet tracking") ||
        value == QStringLiteral("comet_tracking") || value == QStringLiteral("asteroid") ||
        value == QStringLiteral("asteroid tracking") || value == QStringLiteral("asteroid_tracking"))
    {
        return QStringLiteral("comet_tracking");
    }
    return value;
}

[[nodiscard]] inline bool usesCometTracking(const QString& method)
{
    return normalizeRegistrationMethod(method) == QStringLiteral("comet_tracking");
}

[[nodiscard]] inline bool usesStarDetection(const QString& method)
{
    const QString normalized = normalizeRegistrationMethod(method);
    return normalized == QStringLiteral("star_matching");
}

[[nodiscard]] inline bool usesTransformContract(const QString& method)
{
    const QString normalized = normalizeRegistrationMethod(method);
    return normalized == QStringLiteral("star_matching") || normalized == QStringLiteral("manual_points");
}

[[nodiscard]] inline bool usesManualControlPoints(const QString& method)
{
    return normalizeRegistrationMethod(method) == QStringLiteral("manual_points");
}

[[nodiscard]] inline bool requiresFixedReference(const QString& method)
{
    const QString normalized = normalizeRegistrationMethod(method);
    return normalized == QStringLiteral("phase_correlation") || normalized == QStringLiteral("manual_points") ||
           normalized == QStringLiteral("comet_tracking");
}

/// The transform a registration dialog selects - "similarity", "affine", "homography", "polynomial2" to
/// "polynomial4" or "auto" - for a stored contract, read by its mode as the star matcher reads it: "auto" for an
/// automatic mode, the polynomial of the stated order for a polynomial mode, and otherwise the linear model.
[[nodiscard]] inline QString transformSelectionKey(const QString& transformMode, const int transformOrder,
                                                   const QString& transformation)
{
    const QString mode = transformMode.trimmed().toLower();
    if (mode == QStringLiteral("auto"))
    {
        return QStringLiteral("auto");
    }
    if (mode == QStringLiteral("polynomial") && transformOrder >= 2 && transformOrder <= 4)
    {
        return QStringLiteral("polynomial%1").arg(transformOrder);
    }
    const QString linear = transformation.trimmed().toLower();
    if (linear == QStringLiteral("similarity") || linear == QStringLiteral("affine"))
    {
        return linear;
    }
    return QStringLiteral("homography");
}

// What a front end stores for the transform it selects. The integration's dialog and Lite both write through these, so
// one choice is one stored contract.

/// The linear model a selection stores as its transformation: a polynomial or automatic selection refines a homography.
[[nodiscard]] inline QString baseTransformForSelectionKey(const QString& selectionKey)
{
    const QString normalized = selectionKey.trimmed().toLower();
    if (normalized == QStringLiteral("similarity"))
    {
        return QStringLiteral("Similarity");
    }
    if (normalized == QStringLiteral("affine"))
    {
        return QStringLiteral("Affine");
    }
    return QStringLiteral("Homography");
}

/// The mode a selection stores: "Auto", "polynomial" or "linear".
[[nodiscard]] inline QString transformModeForSelectionKey(const QString& selectionKey)
{
    const QString normalized = selectionKey.trimmed().toLower();
    if (normalized == QStringLiteral("auto"))
    {
        return QStringLiteral("Auto");
    }
    if (normalized.startsWith(QStringLiteral("polynomial")))
    {
        return QStringLiteral("polynomial");
    }
    return QStringLiteral("linear");
}

/// The polynomial order a selection stores: two to four for a polynomial, zero otherwise.
[[nodiscard]] inline int transformOrderForSelectionKey(const QString& selectionKey)
{
    const QString normalized = selectionKey.trimmed().toLower();
    if (normalized == QStringLiteral("polynomial2"))
    {
        return 2;
    }
    if (normalized == QStringLiteral("polynomial3"))
    {
        return 3;
    }
    if (normalized == QStringLiteral("polynomial4"))
    {
        return 4;
    }
    return 0;
}

/// Whether a selection refines in a second pass: a polynomial or an automatic one.
[[nodiscard]] inline bool twoPassForSelectionKey(const QString& selectionKey)
{
    const QString normalized = selectionKey.trimmed().toLower();
    return normalized == QStringLiteral("auto") || normalized.startsWith(QStringLiteral("polynomial"));
}

[[nodiscard]] inline QString registrationMethodDisplayName(const QString& method)
{
    const QString normalized = normalizeRegistrationMethod(method);
    if (normalized == QStringLiteral("phase_correlation"))
    {
        return QStringLiteral("Phase Correlation");
    }
    if (normalized == QStringLiteral("manual_points"))
    {
        return QStringLiteral("Manual Points");
    }
    if (normalized == QStringLiteral("comet_tracking"))
    {
        return QStringLiteral("Comet Tracking");
    }
    return QStringLiteral("Star Matching");
}
} // namespace ImageIntegration::RegistrationSettingsUtils
