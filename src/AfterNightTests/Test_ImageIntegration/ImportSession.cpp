// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration model suite's engine session.

#include "Test_ImageIntegration/ImportSession.h"

#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>

#include <array>
#include <memory>

namespace an::tests::integration
{

an::Result<an::consumer::EngineSession> openImportSession(const std::uint32_t width)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    settings.codecRegistry = []() -> an::Result<std::shared_ptr<const an::CodecRegistry>> {
        an::Result<std::shared_ptr<const an::ImageCodec>> fits = an::createFitsCodec();
        if (!fits.hasValue())
        {
            return fits.status();
        }
        an::Result<std::shared_ptr<const an::ImageCodec>> tiff = an::createTiffCodec();
        if (!tiff.hasValue())
        {
            return tiff.status();
        }
        an::Result<std::shared_ptr<const an::ImageCodec>> xisf = an::createXisfCodec();
        if (!xisf.hasValue())
        {
            return xisf.status();
        }
        const std::array<std::shared_ptr<const an::ImageCodec>, 3u> codecs{*fits, *tiff, *xisf};
        return an::CodecRegistry::create(codecs);
    };
    return an::consumer::EngineSession::open(settings);
}

} // namespace an::tests::integration
