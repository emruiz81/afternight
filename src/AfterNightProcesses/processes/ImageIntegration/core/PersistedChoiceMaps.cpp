// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The fixed tables stored integration settings are read through.

#include "stdafx.hpp"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"

#include <array>
#include <initializer_list>
#include <span>

namespace ImageIntegration
{

namespace
{

/// One key of a text table: the key and the spellings that name it, lower case.
struct TextEntry
{
    const char* key;
    std::initializer_list<const char*> spellings;
};

/// One key of an integer table: the key and the integer the model holds for it.
struct IntegerEntry
{
    const char* key;
    int value;
};

// The stack's combination. The rejection methods are stack methods too: the single stacking method a setting names is a
// combination or a rejection, and the "Average" setting with a rejection beside it is read as that rejection.
const std::array<TextEntry, 13> kStackMethods{{
    {"average", {"average"}},
    {"sum", {"sum"}},
    {"median", {"median"}},
    {"minimum", {"minimum"}},
    {"maximum", {"maximum"}},
    {"auto-adaptive-weighted-average",
     {"auto-adaptive weighted average", "auto adaptive weighted average", "auto_adaptive_weighted_average"}},
    {"sigma-clipping", {"sigma clipping", "sigma_clipping"}},
    {"mad-sigma-clipping", {"mad sigma clipping", "mad_sigma_clipping", "mad clipping", "mad_clipping"}},
    {"median-sigma", {"median sigma", "median_sigma"}},
    {"winsorized-sigma-clipping",
     {"winsorized sigma", "winsorized_sigma", "winsorized sigma clipping", "winsorized_sigma_clipping"}},
    {"linear-fit-clipping", {"linear fit", "linear_fit"}},
    {"percentile-clipping", {"percentile"}},
    {"gesdt", {"gesdt"}},
}};

const std::array<TextEntry, 8> kRejectionMethods{{
    {"none", {"", "none"}},
    {"sigma-clipping", {"sigma clipping", "sigma_clipping"}},
    {"mad-sigma-clipping", {"mad sigma clipping", "mad_sigma_clipping", "mad clipping", "mad_clipping"}},
    {"median-sigma", {"median sigma", "median_sigma"}},
    {"winsorized-sigma-clipping",
     {"winsorized sigma", "winsorized_sigma", "winsorized sigma clipping", "winsorized_sigma_clipping"}},
    {"linear-fit-clipping", {"linear fit", "linear_fit"}},
    {"percentile-clipping", {"percentile"}},
    {"gesdt", {"gesdt"}},
}};

const std::array<TextEntry, 6> kWeightings{{
    {"none", {"", "none"}},
    {"noise", {"noise"}},
    {"star-count", {"number of stars", "stars"}},
    {"weighted-fwhm", {"weighted fwhm", "weighted_fwhm"}},
    {"image-count", {"number of images", "number of frames", "image count", "frame count"}},
    {"photometric", {"photometric"}},
}};

const std::array<TextEntry, 4> kStarCountScalings{{
    {"linear", {"linear"}},
    {"quadratic", {"quadratic"}},
    {"exponential", {"exponential"}},
    {"roundness-weighted", {"roundness-weighted", "roundness weighted", "roundness_weighted"}},
}};

const std::array<TextEntry, 6> kDrizzleKernels{{
    {"square", {"square"}},
    {"point", {"point"}},
    {"turbo", {"turbo"}},
    {"gaussian", {"gaussian"}},
    {"lanczos2", {"lanczos2", "lanczos-2"}},
    {"lanczos3", {"lanczos3", "lanczos-3"}},
}};

const std::array<TextEntry, 5> kCfaPatterns{{
    {"auto", {"auto"}},
    {"rggb", {"rggb"}},
    {"bggr", {"bggr"}},
    {"gbrg", {"gbrg"}},
    {"grbg", {"grbg"}},
}};

// The engine's demosaic methods a setting names; "default" is the registration dialog's own spelling of no choice.
const std::array<TextEntry, 7> kDemosaicMethods{{
    {"auto", {"", "auto", "default"}},
    {"fast", {"fast", "bilinear"}},
    {"vng4", {"vng4"}},
    {"ahd", {"ahd"}},
    {"amaze", {"amaze"}},
    {"rcd", {"rcd"}},
    {"lmmse", {"lmmse"}},
}};

const std::array<TextEntry, 8> kInterpolations{{
    {"auto", {"", "auto"}},
    {"nearest", {"nearest neighbor", "nearest-neighbor"}},
    {"bilinear", {"bilinear"}},
    {"bicubic", {"bicubic"}},
    {"area", {"pixel area relation", "area"}},
    {"lanczos4", {"lanczos4"}},
    {"bilinear-exact", {"bit exact bilinear", "bit-exact bilinear"}},
    {"nearest-exact",
     {"bit exact nearest neighbor", "bit exact nearest-neighbor", "bit-exact nearest neighbor",
      "bit-exact nearest-neighbor"}},
}};

const std::array<TextEntry, 3> kAlignmentModes{{
    {"standard", {"standard"}},
    {"mosaic", {"mosaic"}},
    {"intersection", {"intersection"}},
}};

const std::array<TextEntry, 7> kTransformModels{{
    {"similarity", {"similarity"}},
    {"affine", {"affine"}},
    {"homography", {"homography"}},
    {"polynomial2", {"polynomial2", "polynomial 2"}},
    {"polynomial3", {"polynomial3", "polynomial 3"}},
    {"polynomial4", {"polynomial4", "polynomial 4"}},
    {"auto", {"auto"}},
}};

const std::array<IntegerEntry, 3> kNormalizationModes{{{"none", 0}, {"global", 1}, {"local", 2}}};
const std::array<IntegerEntry, 4> kGlobalNormalizationMethods{
    {{"additive", 0}, {"multiplicative", 1}, {"additive-scaling", 2}, {"multiplicative-scaling", 3}}};
const std::array<IntegerEntry, 2> kScaleEstimators{{{"mad", 0}, {"ikss", 1}}};
const std::array<IntegerEntry, 2> kNormalizationReferenceModes{{{"automatic", 0}, {"manual", 1}}};
const std::array<IntegerEntry, 3> kExecutionModes{{{"full", 0}, {"registration-only", 2}, {"incremental", 5}}};
const std::array<IntegerEntry, 2> kLightCalibrationCacheModes{{{"disk", 0}, {"in-memory", 1}}};
const std::array<IntegerEntry, 2> kRegisteredOutputPolicies{{{"retained", 0}, {"discardable", 1}}};

[[nodiscard]] std::span<const TextEntry> textTable(const PersistedChoice choice)
{
    switch (choice)
    {
    case PersistedChoice::StackMethod:
        return kStackMethods;
    case PersistedChoice::RejectionMethod:
        return kRejectionMethods;
    case PersistedChoice::Weighting:
        return kWeightings;
    case PersistedChoice::StarCountScaling:
        return kStarCountScalings;
    case PersistedChoice::DrizzleKernel:
        return kDrizzleKernels;
    case PersistedChoice::CfaPattern:
        return kCfaPatterns;
    case PersistedChoice::DemosaicMethod:
        return kDemosaicMethods;
    case PersistedChoice::Interpolation:
        return kInterpolations;
    case PersistedChoice::AlignmentMode:
        return kAlignmentModes;
    case PersistedChoice::TransformModel:
        return kTransformModels;
    case PersistedChoice::NormalizationMode:
    case PersistedChoice::GlobalNormalizationMethod:
    case PersistedChoice::ScaleEstimator:
    case PersistedChoice::NormalizationReferenceMode:
    case PersistedChoice::ExecutionMode:
    case PersistedChoice::LightCalibrationCacheMode:
    case PersistedChoice::RegisteredOutputPolicy:
        break;
    }
    return {};
}

[[nodiscard]] std::span<const IntegerEntry> integerTable(const PersistedChoice choice)
{
    switch (choice)
    {
    case PersistedChoice::NormalizationMode:
        return kNormalizationModes;
    case PersistedChoice::GlobalNormalizationMethod:
        return kGlobalNormalizationMethods;
    case PersistedChoice::ScaleEstimator:
        return kScaleEstimators;
    case PersistedChoice::NormalizationReferenceMode:
        return kNormalizationReferenceModes;
    case PersistedChoice::ExecutionMode:
        return kExecutionModes;
    case PersistedChoice::LightCalibrationCacheMode:
        return kLightCalibrationCacheModes;
    case PersistedChoice::RegisteredOutputPolicy:
        return kRegisteredOutputPolicies;
    default:
        break;
    }
    return {};
}

[[nodiscard]] bool keyIs(const QString& key, const char* expected)
{
    return key == QLatin1String(expected);
}

} // namespace

const char* persistedChoiceName(const PersistedChoice choice) noexcept
{
    switch (choice)
    {
    case PersistedChoice::StackMethod:
        return "StackMethod";
    case PersistedChoice::RejectionMethod:
        return "RejectionMethod";
    case PersistedChoice::Weighting:
        return "Weighting";
    case PersistedChoice::StarCountScaling:
        return "StarCountScaling";
    case PersistedChoice::DrizzleKernel:
        return "DrizzleKernel";
    case PersistedChoice::CfaPattern:
        return "CfaPattern";
    case PersistedChoice::DemosaicMethod:
        return "DemosaicMethod";
    case PersistedChoice::Interpolation:
        return "Interpolation";
    case PersistedChoice::AlignmentMode:
        return "AlignmentMode";
    case PersistedChoice::TransformModel:
        return "TransformModel";
    case PersistedChoice::NormalizationMode:
        return "NormalizationMode";
    case PersistedChoice::GlobalNormalizationMethod:
        return "GlobalNormalizationMethod";
    case PersistedChoice::ScaleEstimator:
        return "ScaleEstimator";
    case PersistedChoice::NormalizationReferenceMode:
        return "NormalizationReferenceMode";
    case PersistedChoice::ExecutionMode:
        return "ExecutionMode";
    case PersistedChoice::LightCalibrationCacheMode:
        return "LightCalibrationCacheMode";
    case PersistedChoice::RegisteredOutputPolicy:
        return "RegisteredOutputPolicy";
    }
    return "Unknown";
}

PersistedChoiceValue persistedChoiceFromText(const PersistedChoice choice, const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    for (const TextEntry& entry : textTable(choice))
    {
        // A key reads as itself: it is what a stored document holds.
        if (normalized == QLatin1String(entry.key))
        {
            return {QString::fromLatin1(entry.key), true};
        }
        for (const char* spelling : entry.spellings)
        {
            if (normalized == QLatin1String(spelling))
            {
                return {QString::fromLatin1(entry.key), true};
            }
        }
    }
    for (const IntegerEntry& entry : integerTable(choice))
    {
        if (normalized == QLatin1String(entry.key))
        {
            return {QString::fromLatin1(entry.key), true};
        }
    }
    if (!integerTable(choice).empty())
    {
        bool parsed = false;
        const int value = normalized.toInt(&parsed);
        if (parsed)
        {
            return persistedChoiceFromInteger(choice, value);
        }
    }
    return {};
}

PersistedChoiceValue persistedChoiceFromInteger(const PersistedChoice choice, const int value)
{
    for (const IntegerEntry& entry : integerTable(choice))
    {
        if (entry.value == value)
        {
            return {QString::fromLatin1(entry.key), true};
        }
    }
    return {};
}

std::optional<int> integerForKey(const PersistedChoice choice, const QString& key)
{
    for (const IntegerEntry& entry : integerTable(choice))
    {
        if (keyIs(key, entry.key))
        {
            return entry.value;
        }
    }
    return std::nullopt;
}

QStringList persistedChoiceKeys(const PersistedChoice choice)
{
    QStringList keys;
    for (const TextEntry& entry : textTable(choice))
    {
        const QString key = QString::fromLatin1(entry.key);
        if (!keys.contains(key))
        {
            keys.append(key);
        }
    }
    for (const IntegerEntry& entry : integerTable(choice))
    {
        keys.append(QString::fromLatin1(entry.key));
    }
    return keys;
}

std::optional<an::StackCombination> stackCombinationForKey(const QString& key)
{
    if (keyIs(key, "average"))
        return an::StackCombination::Average;
    if (keyIs(key, "sum"))
        return an::StackCombination::Sum;
    if (keyIs(key, "median"))
        return an::StackCombination::Median;
    if (keyIs(key, "minimum"))
        return an::StackCombination::Minimum;
    if (keyIs(key, "maximum"))
        return an::StackCombination::Maximum;
    if (keyIs(key, "auto-adaptive-weighted-average"))
        return an::StackCombination::AutoAdaptiveWeightedAverage;
    return std::nullopt;
}

std::optional<an::StackRejection> stackRejectionForKey(const QString& key)
{
    if (keyIs(key, "none"))
        return an::StackRejection::None;
    if (keyIs(key, "sigma-clipping"))
        return an::StackRejection::SigmaClipping;
    if (keyIs(key, "mad-sigma-clipping"))
        return an::StackRejection::MadSigmaClipping;
    if (keyIs(key, "median-sigma"))
        return an::StackRejection::MedianSigmaReplacement;
    if (keyIs(key, "winsorized-sigma-clipping"))
        return an::StackRejection::WinsorizedSigmaClipping;
    if (keyIs(key, "linear-fit-clipping"))
        return an::StackRejection::LinearFitClipping;
    if (keyIs(key, "percentile-clipping"))
        return an::StackRejection::PercentileClipping;
    if (keyIs(key, "gesdt"))
        return an::StackRejection::GeneralizedEsdTest;
    return std::nullopt;
}

std::optional<an::StackWeighting> stackWeightingForKey(const QString& key)
{
    if (keyIs(key, "none"))
        return an::StackWeighting::Uniform;
    if (keyIs(key, "noise"))
        return an::StackWeighting::Noise;
    if (keyIs(key, "star-count"))
        return an::StackWeighting::StarCount;
    if (keyIs(key, "weighted-fwhm"))
        return an::StackWeighting::Fwhm;
    if (keyIs(key, "image-count"))
        return an::StackWeighting::ImageCount;
    if (keyIs(key, "photometric"))
        return an::StackWeighting::Photometric;
    return std::nullopt;
}

std::optional<an::DrizzleKernel> drizzleKernelForKey(const QString& key)
{
    if (keyIs(key, "square"))
        return an::DrizzleKernel::Square;
    if (keyIs(key, "point"))
        return an::DrizzleKernel::Point;
    if (keyIs(key, "turbo"))
        return an::DrizzleKernel::Turbo;
    if (keyIs(key, "gaussian"))
        return an::DrizzleKernel::Gaussian;
    if (keyIs(key, "lanczos2"))
        return an::DrizzleKernel::Lanczos2;
    if (keyIs(key, "lanczos3"))
        return an::DrizzleKernel::Lanczos3;
    return std::nullopt;
}

std::optional<an::CfaPattern> cfaPatternForKey(const QString& key)
{
    if (keyIs(key, "rggb"))
        return an::CfaPattern::Rggb;
    if (keyIs(key, "bggr"))
        return an::CfaPattern::Bggr;
    if (keyIs(key, "gbrg"))
        return an::CfaPattern::Gbrg;
    if (keyIs(key, "grbg"))
        return an::CfaPattern::Grbg;
    return std::nullopt;
}

std::optional<an::DemosaicMethod> demosaicMethodForKey(const QString& key)
{
    if (keyIs(key, "fast"))
        return an::DemosaicMethod::Fast;
    if (keyIs(key, "vng4"))
        return an::DemosaicMethod::Vng4;
    if (keyIs(key, "ahd"))
        return an::DemosaicMethod::LuminanceAhd;
    if (keyIs(key, "amaze"))
        return an::DemosaicMethod::Amaze;
    if (keyIs(key, "rcd"))
        return an::DemosaicMethod::Rcd;
    if (keyIs(key, "lmmse"))
        return an::DemosaicMethod::Lmmse;
    return std::nullopt;
}

std::optional<an::KernelInterpolation> interpolationForKey(const QString& key)
{
    if (keyIs(key, "nearest"))
        return an::KernelInterpolation::Nearest;
    if (keyIs(key, "bilinear"))
        return an::KernelInterpolation::Linear;
    if (keyIs(key, "bicubic"))
        return an::KernelInterpolation::Cubic;
    if (keyIs(key, "area"))
        return an::KernelInterpolation::Area;
    if (keyIs(key, "lanczos4"))
        return an::KernelInterpolation::Lanczos4;
    if (keyIs(key, "bilinear-exact"))
        return an::KernelInterpolation::LinearExact;
    if (keyIs(key, "nearest-exact"))
        return an::KernelInterpolation::NearestExact;
    return std::nullopt;
}

std::optional<RegistrationCanvasMode> canvasModeForKey(const QString& key)
{
    if (keyIs(key, "standard"))
        return RegistrationCanvasMode::Standard;
    if (keyIs(key, "mosaic"))
        return RegistrationCanvasMode::Mosaic;
    if (keyIs(key, "intersection"))
        return RegistrationCanvasMode::Intersection;
    return std::nullopt;
}

std::optional<an::RegistrationTransformKind> transformKindForKey(const QString& key)
{
    if (keyIs(key, "similarity"))
        return an::RegistrationTransformKind::Similarity;
    if (keyIs(key, "affine"))
        return an::RegistrationTransformKind::Affine;
    if (keyIs(key, "homography"))
        return an::RegistrationTransformKind::Projective;
    if (keyIs(key, "polynomial2"))
        return an::RegistrationTransformKind::Polynomial2;
    if (keyIs(key, "polynomial3"))
        return an::RegistrationTransformKind::Polynomial3;
    if (keyIs(key, "polynomial4"))
        return an::RegistrationTransformKind::Polynomial4;
    return std::nullopt;
}

std::optional<an::NormalizationMethod> normalizationMethodForKey(const QString& key)
{
    if (keyIs(key, "additive"))
        return an::NormalizationMethod::Additive;
    if (keyIs(key, "multiplicative"))
        return an::NormalizationMethod::Multiplicative;
    if (keyIs(key, "additive-scaling"))
        return an::NormalizationMethod::AdditiveScaling;
    if (keyIs(key, "multiplicative-scaling"))
        return an::NormalizationMethod::MultiplicativeScaling;
    return std::nullopt;
}

std::optional<an::ScaleEstimator> scaleEstimatorForKey(const QString& key)
{
    if (keyIs(key, "mad"))
        return an::ScaleEstimator::MedianAbsoluteDeviation;
    if (keyIs(key, "ikss"))
        return an::ScaleEstimator::IterativeKSigma;
    return std::nullopt;
}

} // namespace ImageIntegration
