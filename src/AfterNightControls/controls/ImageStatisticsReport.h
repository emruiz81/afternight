// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The figures the statistics window shows for each channel of an image (`controls/ImageStatisticsFigures.h`), computed
/// through the engine's basic and advanced statistics over the samples the image's validity admits.
///
/// Every figure is read through the representation's display range (`widgets/DisplayNormalization.h`), as the window
/// always showed it: a location as a fraction of the range, a spread as a share of it, and a variance as a share of its
/// square. A scale multiplies them back: a location and a spread by the scale's full scale and a variance by its
/// square.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/ImageStatisticsFigures.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>

namespace an::ui
{

/// Computes the figures of an image read under its lease.
///
/// @param scope the admitted operation the statistics run in.
/// @param image the image.
/// @param excludeClipped whether the samples at either end of the range - at or below zero and at or above one - are
///        left out as well as those the image's validity leaves out.
/// @return the figures; a channel whose selection admits nothing has a count of zero and zero figures;
///         `ErrorCode::NotInitialized` for an image that holds nothing; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ImageStatisticsReport>
computeImageStatisticsReport(const an::consumer::OperationScope& scope, const an::consumer::AssetReadLease& image,
                             bool excludeClipped);

} // namespace an::ui
