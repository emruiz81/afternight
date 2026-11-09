// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one reading of a FITS header's records into cards, which the FITS codec and the header-card
// reader of `IO/FitsHeaderCards.h` share.
//
// The codec reads a header's records from the file through OpenFITS, which keeps each record a card was read from, and
// hands them here; `parseFitsHeaderCards` hands the records of a header-only document. Both get the same cards from
// the same records, by the rules `IO/FitsHeaderCards.h` states.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <string_view>
#include <vector>

namespace an
{
/// The cards of a header's records, as the FITS codec reads them.
///
/// The records are eighty bytes each, laid end to end, the last of them an `END` record. A byte outside printable
/// ASCII is kept as the record holds it, as the codec has always read a file's header: only a document whose header is
/// read on its own refuses one.
/// @param records the header's records, borrowed for the call.
/// @return the cards in the header's order, each in the `CodecNamespace::Fits` namespace; or the failures
///         `parseFitsHeaderCards` returns for a header with no `END` record, a complex value with no closing
///         parenthesis, a long string longer than one card's text, or more cards than `kMaxCodecCards`.
Result<std::vector<CodecCard>> readFitsRecordCards(std::string_view records);
} // namespace an
