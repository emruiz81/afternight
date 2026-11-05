// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the filter-array windows make of a mosaic: its four planes and the mosaic they join back into, its measured
/// H-alpha and green, the compatibility H-alpha and OIII pair, and the demosaiced colour image with or without its
/// three channels - each an engine operation over the run's image, the result a new asset of the run's session.
///
/// A mosaic is the two-by-two arrangement its description states, with its phase; nothing is guessed. A tabulated
/// mosaic - a four-colour or six-by-six arrangement - is refused by name, as the engine refuses to split it.
#pragma once

#include "AfterNightProcessesExport.h"

#include "app/DemosaicTextMaps.h"
#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/NarrowbandExtraction.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace an::ui::processes
{

/// A representation's rank, lowest first: a narrower before a wider, a signed before an unsigned of the same width, an
/// integer before a floating one.
///
/// @param type a representation.
/// @return its rank, from zero for Int8 to seven for Float64.
[[nodiscard]] AFTERNIGHTPROCESSES_API int representationRank(an::SampleType type) noexcept;

/// Whether an image of a description is a mosaic the filter-array windows take apart: one channel, under one of the
/// four two-by-two arrangements.
///
/// @param descriptor the description, or nothing for an image that holds nothing.
/// @return success; `ErrorCode::IncompatibleArguments` for an image that states no filter array or has more than one
///         channel; or `ErrorCode::Unsupported` for a tabulated arrangement, named.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status checkBayerMosaic(const std::optional<an::ImageDescriptor>& descriptor);

/// What each of a mosaic's four planes is named after its position and the filter over it: "CFA0 - Red",
/// "CFA1 - Green1", "CFA2 - Green2", "CFA3 - Blue" under RGGB, the greens numbered in plane order.
///
/// @param cfa the mosaic's filter array and its phase.
/// @return the four names, in plane order.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::array<std::string, 4u> mosaicPlaneNames(const an::CfaDescriptor& cfa);

/// The four planes of a mosaic, one per position of its two-by-two block, through the engine's split: plane
/// `row * 2 + column` holds the samples at that position of every block, half the mosaic's width and height rounded
/// down, in its representation. Each keeps the mosaic's metadata and meaning, states no filter array and no solution,
/// and carries no world-coordinate card and no solver's card.
///
/// @param scope the run's admitted operation.
/// @param source the mosaic.
/// @return the planes, in plane order; what `checkBayerMosaic` refuses; `ErrorCode::Unsupported` for a mosaic whose
///         validity is stated in a mask; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::vector<an::consumer::ImageAsset>>
splitMosaicPlanes(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source);

/// A mosaic joined from four grey planes, through the engine's join: each plane converted to the highest of the four's
/// representations, and placed at its position of every block under the stated arrangement. The result keeps the
/// first plane's metadata and states the arrangement at zero phase, no solution, no world-coordinate card and no
/// solver's card.
///
/// @param scope the run's admitted operation.
/// @param planes the four planes, in plane order.
/// @param pattern one of the four two-by-two arrangements.
/// @return the mosaic; `ErrorCode::IncompatibleArguments` for planes of different widths or heights or of more than
///         one channel; `ErrorCode::InvalidArgument` for another arrangement; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
joinMosaicPlanes(const an::ui::ExecutionScope& scope, const std::array<const an::ui::ProcessSource*, 4u>& planes,
                 an::CfaPattern pattern);

/// The measured H-alpha of a mosaic: the red site of every block, in its representation.
///
/// @param scope the run's admitted operation.
/// @param source the mosaic.
/// @return the H-alpha image; what `checkBayerMosaic` refuses; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
extractHydrogenAlpha(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source);

/// The measured green of a mosaic: the mean of the two green sites of every block, in its representation.
///
/// @param scope the run's admitted operation.
/// @param source the mosaic.
/// @return the green image; what `checkBayerMosaic` refuses; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
extractGreen(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source);

/// The compatibility H-alpha and OIII pair of a mosaic, worked in fractions of full scale and written back in its
/// representation: H-alpha the red sites, OIII the equalized green and blue sites with the red ones filled from their
/// neighbours, one of them resampled by a factor of two when asked.
///
/// @param scope the run's admitted operation.
/// @param source the mosaic.
/// @param scaling which product, if either, is resampled.
/// @return H-alpha then OIII; what `checkBayerMosaic` refuses; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::vector<an::consumer::ImageAsset>>
extractHydrogenAlphaAndOxygenIII(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                                 an::NarrowbandExtractionScaling scaling);

/// What a debayer opens.
enum class DebayerOutput : std::uint8_t
{
    /// The colour image.
    Combined,

    /// Its red, green and blue channels.
    Separate,

    /// The colour image, then its three channels.
    Both
};

/// The images a debayer makes of one image: the colour image the method demosaics under the filter array the choice
/// names - the image's own for "From metadata", which an image that states none refuses - and its three stored
/// channels, as the output asks. Over an X-Trans array the method runs as its tabulated counterpart
/// (`demosaicMethodForMosaic`), and a named two-by-two pattern over an image that states one is refused by name.
///
/// @param scope the run's admitted operation.
/// @param source the mosaic: one channel.
/// @param method the demosaic method chosen.
/// @param pattern the filter array the choice names.
/// @param output what the run opens.
/// @return the colour image and then its channels, as the output asks; `ErrorCode::IncompatibleArguments` for an image
///         of more than one channel, one that states no filter array when the choice reads it from the image, or one
///         that states an X-Trans array when the choice names a two-by-two pattern; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::vector<an::consumer::ImageAsset>>
debayerImage(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, an::DemosaicMethod method,
             const an::ui::BayerPatternChoice& pattern, DebayerOutput output);

} // namespace an::ui::processes
