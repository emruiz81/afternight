// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The integration's normalization settings as the engine's normalization requests: the global method and scale
/// estimator a group names, and the local-normalization request its settings resolve to - every field the engine reads
/// stated, so the coefficients' fingerprint carries each of them, the ones no dialog offers among them.

#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>

#include <QJsonObject>

namespace ImageIntegration
{

/// The engine's method for a group's global normalization method.
[[nodiscard]] an::NormalizationMethod normalizationMethodFor(GlobalNormalizationMethod method) noexcept;

/// The engine's estimator for a group's scale estimator.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ScaleEstimator scaleEstimatorFor(ScaleEstimatorMethod method) noexcept;

/// The local-normalization request a group's settings resolve to.
///
/// A value outside the engine's interval is held to it, as the dialog holds it, so the request is always one the engine
/// plans. The sampling region's smallest side, how far it reaches past its cell and how many positions the scale is
/// estimated from are not settings; they are constants and are stated here.
///
/// @param settings the group's settings.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::LocalNormalizationRequest
localNormalizationRequestFor(const LocalNormalizationSettings& settings);

/// Every field of a local-normalization request, by name, for a fingerprint.
///
/// @param request the request.
/// @return the object.
[[nodiscard]] AFTERNIGHTPROCESSES_API QJsonObject
localNormalizationRequestJson(const an::LocalNormalizationRequest& request);

} // namespace ImageIntegration
