// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How the command-line tool decides what it writes: the representation of the output's samples and the compression
/// the container is written with.
///
/// The engine writes what it is given and refuses what a codec cannot hold; choosing a representation for a file is
/// the tool's decision, and this is where it is written down. A run states it to the engine as an explicit output
/// intent rather than leaving a codec to pick.
///
/// **The representation.** The one `--sample-type` names, when given; otherwise the input's own. When the container
/// cannot hold the input's representation the tool writes the narrowest one it holds that is at least as wide, or the
/// widest it holds when none is, preferring the input's own kind of number - floating point, unsigned or signed -
/// then unsigned integers, signed integers and floating point in that order. So a PNG holds sixteen bits for every
/// input wider than eight and eight bits otherwise, and a JPEG eight bits for every input.
#pragma once

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace an::cli
{

/// The resident representations a codec's encode set holds, in the order the engine enumerates them.
///
/// @param encodeSamples the codec's encode set, an OR of `codecSampleBit` values.
/// @return every one of the eight it holds.
[[nodiscard]] std::vector<an::SampleType> encodableSampleTypes(std::uint32_t encodeSamples);

/// The representation a run writes.
///
/// @param source the input's representation.
/// @param requested what `--sample-type` named, or empty.
/// @param encodeSamples the output codec's encode set.
/// @return the representation; empty when one was requested and the container cannot hold it, or when the container
///         holds none of the eight.
[[nodiscard]] std::optional<an::SampleType>
chooseOutputSampleType(an::SampleType source, std::optional<an::SampleType> requested, std::uint32_t encodeSamples);

/// Whether a container can state a mosaic's colour filter array, and so hold a mosaic of camera codes as the mosaic it
/// is: XISF and TIFF state a two-by-two pattern and an X-Trans sensor's six-by-six table alike; FITS states a
/// two-by-two pattern only, in its one pattern card, and its codec refuses a tabulated mosaic; PNG and JPEG state none,
/// and their codecs refuse such a frame.
///
/// @param format the container.
/// @param pattern the mosaic's filter array.
/// @return true when the container holds the mosaic.
[[nodiscard]] bool holdsFilterArrayMosaic(an::CodecFormat format, an::CfaPattern pattern) noexcept;

/// The compression the tool writes a container with: the container's own when it always compresses - deflate for
/// PNG, JPEG's for JPEG - and none for the others.
///
/// @param format the container.
/// @return the compression the output intent states.
[[nodiscard]] an::OutputCompression outputCompression(an::CodecFormat format) noexcept;

/// The representations a container holds, as the tool spells them, for a message.
///
/// @param encodeSamples the codec's encode set.
/// @return the names joined with commas and a final "and"; "nothing" when it holds none of the eight.
[[nodiscard]] std::string describeEncodableSampleTypes(std::uint32_t encodeSamples);

} // namespace an::cli
