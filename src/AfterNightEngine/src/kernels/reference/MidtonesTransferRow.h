// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - one Float32 row through a midtones transfer, as the stretch writes it.
//
// `stretchImage` plans a screen transfer once per channel and writes each row of a finite, all-valid Float32 image
// through the compiled row of the execution's CPU table, or through its scalar authority where the table has none. A
// composition that stretches rows as it makes them - the display presentation, which promotes a row and stretches it
// while it is still in cache - plans and writes them through these two, so its rows are the stretch's bit for bit.
// Both are defined with the stretch, in `algorithms/pointwise/Stretch.cpp`.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>

#include <cstdint>

namespace an
{

/// The smallest destination a transfer writes with streaming stores: one this large cannot stay in the caches
/// anyway, so its lines are written without being read first.
inline constexpr std::uint64_t kMidtonesTransferStreamingBytes = std::uint64_t{32u} << 20u;

/// One channel's midtones transfer, planned as `stretchImage` plans a channel's screen transfer.
///
/// @param parameters the channel's transfer.
/// @return its planned coefficients.
[[nodiscard]] MidtonesTransferCoefficients
planScreenTransferChannel(const MidtonesTransferParameters& parameters) noexcept;

/// Writes one Float32 row through a channel's planned transfer, as `stretchImage` writes a channel's row of an
/// all-valid image: through `compiled` where it is not null, and otherwise through the scalar authority over each
/// sample held to the unit interval.
///
/// @param compiled the execution's compiled row, from `midtonesTransferFloat32RunFor`, or null.
/// @param plan the channel's planned transfer.
/// @param source the row; every sample is finite.
/// @param destination where the transferred row goes: the source itself, or memory that does not overlap it.
/// @param width how many samples the row holds.
/// @param stream whether the compiled row writes the destination with streaming stores; only where it is not the
///        source.
void transferScreenTransferRow(MidtonesTransferFloat32RunFn compiled, const MidtonesTransferCoefficients& plan,
                               const float* source, float* destination, std::uint32_t width, bool stream) noexcept;

} // namespace an
