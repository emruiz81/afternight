// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/StorageArrangement.h>

// The names of the three arrangements. Which one a layout is, is a constant expression in the
// header; what it is called is read by a person, so it costs a call and lives here.
//
// The switch has no `default` label, so an arrangement added to the enumeration without a name is a
// compile failure rather than a blank in a log.

namespace an
{

const char* storageArrangementName(StorageArrangement arrangement) noexcept
{
    switch (arrangement)
    {
    case StorageArrangement::Planar:
        return "Planar";
    case StorageArrangement::Interleaved:
        return "Interleaved";
    case StorageArrangement::SemiPlanar:
        return "SemiPlanar";
    }
    return "UnknownStorageArrangement";
}

} // namespace an
