// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The comparison view's difference image: how far apart two images are, sample by sample, computed by the engine.
///
/// The engine's image difference reads each sample as the number its domain says it is and writes Float32 in the
/// images' common domain, so two identical images differ by zero everywhere, signed ones included.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>

#include <stop_token>

namespace an::ui
{

enum class ComparisonMode
{
    None,
    Overlay,
    Difference
};

struct ComparisonDifferenceOptions
{
    /// One channel, the mean of every channel's absolute difference, rather than one difference per channel.
    bool grayscale = false;
};

/// Builds the difference of two images of one extent, schema and domain as a new Float32 asset of the session. A
/// sample is written where both images' samples are valid; one that is not is left not a number, and the result reads
/// such a sample as invalid.
///
/// @param session the process's session.
/// @param left one image.
/// @param right the other.
/// @param options what is measured.
/// @param stop the request's stop.
/// @return the difference; `ErrorCode::Cancelled` when stopped; or what the engine refused - two images of different
///         extents, schemas or domains among it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
buildDifferenceImage(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& left,
                     const an::consumer::ImageAsset& right, const ComparisonDifferenceOptions& options,
                     std::stop_token stop);

} // namespace an::ui
