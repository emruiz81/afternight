// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The fixed tables an integration setting is read through: every integer and every piece of text the model, a
/// dialog or a manifest holds for an engine-facing choice, mapped to a stable key, and the key to what the engine is
/// asked for.
///
/// The model holds some choices as the position of an enumerator and others as the text its dialog shows. Stored as
/// they are, a reordered enumerator or a renamed label would change a saved project silently, so each choice is one
/// closed table: a value either names one of the table's keys or is reported as not known, so a reader can refuse it
/// with context rather than guess. A session, a preset and a project file store the key. The engine's own names are
/// never parsed: the table is the only path from text to an engine value.

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Image/CfaPattern.h>

#include <QString>
#include <QStringList>

#include <optional>

namespace ImageIntegration
{

/// The format the session and preset writers store, with every choice as its stable key.
inline constexpr int kSessionSchemaVersion = 3;

/// An engine-facing choice a session, a preset or a manifest stores.
enum class PersistedChoice
{
    /// The stack's combination, stored as text ("Average", "Median", or a rejection method's name).
    StackMethod,
    /// The rejection beside an average, stored as text.
    RejectionMethod,
    /// The stack's weighting, stored as text.
    Weighting,
    /// How a star count becomes a weight, stored as text.
    StarCountScaling,
    /// The drizzle kernel, stored as text.
    DrizzleKernel,
    /// A filter array's arrangement, stored as text; "Auto" is the image's own.
    CfaPattern,
    /// A demosaic method, stored as text; empty and "Auto" choose none.
    DemosaicMethod,
    /// A registration's sampling interpolation, stored as text; empty and "Auto" choose for the operation.
    Interpolation,
    /// A registration's output canvas, stored as text.
    AlignmentMode,
    /// A registration's transform model, stored as text.
    TransformModel,
    /// The normalization mode, stored as an integer.
    NormalizationMode,
    /// The global normalization method, stored as an integer.
    GlobalNormalizationMethod,
    /// The global normalization's scale estimator, stored as an integer.
    ScaleEstimator,
    /// How the normalization reference is chosen, stored as an integer.
    NormalizationReferenceMode,
    /// What a run executes, stored as an integer.
    ExecutionMode,
    /// Whether calibrated lights are written to disk or held in memory, stored as an integer.
    LightCalibrationCacheMode,
    /// Whether a group's registered lights are kept after its stack, stored as an integer.
    RegisteredOutputPolicy
};

/// A choice's stable name, for a message.
///
/// @param choice the choice.
/// @return a static string; "Unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTPROCESSES_API const char* persistedChoiceName(PersistedChoice choice) noexcept;

/// What a stored value reads as.
struct PersistedChoiceValue
{
    /// The stable key, or empty when the value names none.
    QString key;

    /// Whether the value named one of the table's keys.
    bool known = false;
};

/// What a piece of stored text reads as, case and surrounding space aside.
///
/// @param choice the choice the text was stored for; an integer choice reads its integer's text.
/// @param text the text.
/// @return the key and whether it was known.
[[nodiscard]] AFTERNIGHTPROCESSES_API PersistedChoiceValue persistedChoiceFromText(PersistedChoice choice,
                                                                                   const QString& text);

/// What an integer reads as, for the choices the model holds as an enumerator's position.
///
/// @param choice the choice.
/// @param value the integer.
/// @return the key and whether it was known; never known for a choice stored as text.
[[nodiscard]] AFTERNIGHTPROCESSES_API PersistedChoiceValue persistedChoiceFromInteger(PersistedChoice choice,
                                                                                      int value);

/// The integer the model holds for a key: its enumerator's position.
///
/// @param choice an integer choice.
/// @param key one of its keys.
/// @return the integer, or nothing for a key the choice does not hold or a choice stored as text.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<int> integerForKey(PersistedChoice choice, const QString& key);

/// Every key a choice holds, in the order its dialog offers them.
///
/// @param choice the choice.
/// @return the keys.
[[nodiscard]] AFTERNIGHTPROCESSES_API QStringList persistedChoiceKeys(PersistedChoice choice);

/// What a registration's output canvas is.
enum class RegistrationCanvasMode
{
    /// The reference frame's own canvas.
    Standard,
    /// The union of every registered frame.
    Mosaic,
    /// The part every registered frame covers.
    Intersection
};

/// The stack's combination a key names.
///
/// @param key a `StackMethod` key that is not a rejection method.
/// @return the combination, or nothing for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::StackCombination> stackCombinationForKey(const QString& key);

/// The stack's rejection a key names.
///
/// @param key a `StackMethod` or `RejectionMethod` key.
/// @return the rejection; `StackRejection::None` for "none"; nothing for a key that names no rejection.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::StackRejection> stackRejectionForKey(const QString& key);

/// The stack's weighting a key names.
///
/// @param key a `Weighting` key.
/// @return the weighting, or nothing for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::StackWeighting> stackWeightingForKey(const QString& key);

/// The drizzle kernel a key names.
///
/// @param key a `DrizzleKernel` key.
/// @return the kernel, or nothing for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::DrizzleKernel> drizzleKernelForKey(const QString& key);

/// The filter arrangement a key names.
///
/// @param key a `CfaPattern` key.
/// @return the arrangement; nothing for "auto", which is the image's own, and for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::CfaPattern> cfaPatternForKey(const QString& key);

/// The demosaic method a key names.
///
/// @param key a `DemosaicMethod` key.
/// @return the method; nothing for "auto" and for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::DemosaicMethod> demosaicMethodForKey(const QString& key);

/// The sampling interpolation a key names.
///
/// @param key an `Interpolation` key.
/// @return the interpolation; nothing for "auto", which the operation chooses, and for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::KernelInterpolation> interpolationForKey(const QString& key);

/// The canvas a key names.
///
/// @param key an `AlignmentMode` key.
/// @return the canvas, or nothing for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<RegistrationCanvasMode> canvasModeForKey(const QString& key);

/// The linear transform model a key names; "auto" chooses none.
///
/// @param key a `TransformModel` key.
/// @return the model, or nothing for "auto" and for another key.
[[nodiscard]] std::optional<an::RegistrationTransformKind> transformKindForKey(const QString& key);

/// The global normalization method a key names.
///
/// @param key a `GlobalNormalizationMethod` key.
/// @return the method, or nothing for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::NormalizationMethod>
normalizationMethodForKey(const QString& key);

/// The scale estimator a key names.
///
/// @param key a `ScaleEstimator` key.
/// @return the estimator, or nothing for another key.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::ScaleEstimator> scaleEstimatorForKey(const QString& key);

} // namespace ImageIntegration
