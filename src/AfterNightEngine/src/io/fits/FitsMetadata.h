// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a FITS image's typed metadata, built from its cards.
//
// Apart from the OpenFITS boundary of `io/fits/FitsFile.h`, because building metadata from cards reaches no library:
// the unit that builds it compiles without OpenFITS's headers.

#pragma once

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <string_view>

namespace an
{
/// Builds a FITS image's metadata from its cards: the acquisition projection, a supported WCS
/// solution - cropped when a region is given - and the file's provenance.
/// @param codec the cards and source encoding, consumed.
/// @param extent the whole image's extent, which the WCS solution is read against.
/// @param path the file's path, recorded in provenance when it fits.
/// @param integer64 the BITPIX=64 policy.
/// @param integerImage whether the image is BITPIX=64, whose lossy decode provenance records.
/// @param crop the decoded region, or null for the whole image.
/// @return the metadata, or the failure building it reported.
Result<ImageMetadata> fitsMetadata(CodecMetadata codec, const ImageExtent& extent, std::string_view path,
                                   FitsInteger64Policy integer64, bool integerImage, const CodecRegion* crop = nullptr);
} // namespace an
