// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The engine session the integration model's suite reads its files in: the FITS, TIFF and XISF codecs and the stated
/// thread budget, as the product's session reads frames - the product's own registers more formats, none of which
/// these cases write.

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/Result.h>

#include <cstdint>

namespace an::tests::integration
{

/// A session holding the FITS, TIFF and XISF codecs and `width` threads.
[[nodiscard]] an::Result<an::consumer::EngineSession> openImportSession(std::uint32_t width = 2u);

} // namespace an::tests::integration
